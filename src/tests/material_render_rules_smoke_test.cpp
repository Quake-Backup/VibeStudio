// The material renderer's engine rules.
// No game data: every file is generated here.
//
// The CPU checks run anywhere: Quake III texture coordinates and colours,
// Doom 3 texture matrices, stage pairing, implicit stages, repeat rules and
// the conditioned-off rule, safe table indices, random frame durations,
// upload keys and cancellation. The drawing checks need OpenGL or Vulkan and
// are skipped without one.

#include "core/material_eval.h"
#include "core/material_images.h"
#include "core/material_library.h"
#include "core/material_render.h"
#include "core/material_render_p.h"
#include "core/material_script.h"
#include "core/package_archive.h"
#include "tests/render_test_support.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QtEndian>

#include <atomic>
#include <cmath>
#include <iostream>
#include <string>

using namespace slopstudio;
using namespace slopstudio::material_render;

namespace {

int failures = 0;

bool expect(bool condition, const std::string& message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return condition;
}

bool near(double a, double b, double tolerance = 1.0e-3)
{
	return std::abs(a - b) <= tolerance;
}

MaterialDefinition parsed(const QString& text, MaterialEngine engine, const QString& name)
{
	const MaterialScript script = parseMaterialScript(text, engine, engine == MaterialEngine::Doom3 ? QStringLiteral("materials/t.mtr")
																									  : QStringLiteral("scripts/t.shader"));
	const MaterialDefinition* definition = script.find(name);
	expect(definition != nullptr, "material " + name.toStdString() + " parses");
	return definition ? *definition : MaterialDefinition();
}

Doom3Registers registersOf(const MaterialDefinition& definition, const MaterialEvalContext& context = {})
{
	Doom3Registers registers;
	registers.values = evaluateMaterialExpressions(definition, MaterialTableSet(), context);
	return registers;
}

// --- Quake III ---------------------------------------------------------------

void checkQuake3Rules()
{
	const Mesh mesh = buildMesh(MaterialPreviewShape::Wall, 64.0, 64.0, 1.0, 64.0);
	MaterialRenderOptions options;
	options.eval.parms = {1.0, 1.0, 1.0, 0.25, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
	const Camera camera = makeCamera(mesh, options);
	Quake3Frame frame;
	frame.options = &options;
	frame.time = 1.0;
	const auto coordinates = [&](const MaterialDefinition& definition, double s, double t, double time = 1.0) {
		Vertex vertex;
		vertex.s = s;
		vertex.t = t;
		Quake3Frame at = frame;
		at.time = time;
		double outS = 0.0;
		double outT = 0.0;
		quake3TexCoords(definition.stages.value(0), vertex, camera, at, &outS, &outT);
		return std::pair<double, double>(outS, outT);
	};
	const QString text = QStringLiteral(
		"t/rotate\n{\n\t{\n\t\tmap t/a.tga\n\t\ttcMod rotate 90\n\t}\n}\n"
		"t/transform\n{\n\t{\n\t\tmap t/a.tga\n\t\ttcMod transform 0 1 1 0 0 0\n\t}\n}\n"
		"t/order\n{\n\t{\n\t\tmap t/a.tga\n\t\ttcMod scale 2 2\n\t\ttcMod scroll 0.5 0\n\t}\n}\n"
		"t/turb\n{\n\t{\n\t\tmap t/a.tga\n\t\ttcMod turb 0 0.25 0 1e8\n\t}\n}\n"
		"t/entity\n{\n\t{\n\t\tmap t/a.tga\n\t\trgbGen identity\n\t\talphaGen entity\n\t}\n}\n"
		"t/entityconst\n{\n\t{\n\t\tmap t/a.tga\n\t\trgbGen const ( 1 1 1 )\n\t\talphaGen entity\n\t}\n}\n");
	// RB_CalcRotateTexCoords at -90 degrees through the sine table: s' = t,
	// t' = 1 - s about the centre.
	const MaterialDefinition rotate = parsed(text, MaterialEngine::Quake3, QStringLiteral("t/rotate"));
	const auto r1 = coordinates(rotate, 1.0, 0.0);
	const auto r2 = coordinates(rotate, 0.0, 0.0);
	expect(near(r1.first, 0.0) && near(r1.second, 0.0) && near(r2.first, 0.0) && near(r2.second, 1.0),
		"tcMod rotate 90 at one second turns (1,0) to (0,0) and (0,0) to (0,1)");
	const auto rHuge = coordinates(rotate, 0.3, 0.6, 1.0e12);
	expect(std::isfinite(rHuge.first) && std::isfinite(rHuge.second) && std::abs(rHuge.first) < 3.0 && std::abs(rHuge.second) < 3.0,
		"tcMod rotate stays finite and in range at a huge time");
	const auto swapped = coordinates(parsed(text, MaterialEngine::Quake3, QStringLiteral("t/transform")), 0.25, 0.75);
	expect(near(swapped.first, 0.75) && near(swapped.second, 0.25), "tcMod transform 0 1 1 0 0 0 swaps s and t");
	const auto ordered = coordinates(parsed(text, MaterialEngine::Quake3, QStringLiteral("t/order")), 0.25, 0.25);
	expect(near(ordered.first, 1.0) && near(ordered.second, 0.5), "tcMods apply in written order: scale, then scroll");
	const auto turb = coordinates(parsed(text, MaterialEngine::Quake3, QStringLiteral("t/turb")), 0.5, 0.5, 1.0e9);
	expect(std::isfinite(turb.first) && std::abs(turb.first - 0.5) <= 0.2501, "tcMod turb at a huge frequency and time stays in its amplitude");

	Vertex vertex;
	const Color identity = quake3VertexColor(parsed(text, MaterialEngine::Quake3, QStringLiteral("t/entity")),
		parsed(text, MaterialEngine::Quake3, QStringLiteral("t/entity")).stages.value(0), vertex, mesh, camera, frame);
	expect(near(identity.a, 1.0), "alphaGen entity beside rgbGen identity is skipped (the CGEN/AGEN mix-up)");
	const MaterialDefinition constant = parsed(text, MaterialEngine::Quake3, QStringLiteral("t/entityconst"));
	const Color entity = quake3VertexColor(constant, constant.stages.value(0), vertex, mesh, camera, frame);
	expect(near(entity.a, 0.25), "alphaGen entity reads parm3 beside rgbGen const");
}

// --- Safe indices and noise --------------------------------------------------

void checkBoundedValues()
{
	expect(quake3TableIndex(std::nan("")) == 0 && quake3TableIndex(INFINITY) == 0, "table indices of NaN and infinity are 0");
	const int big = quake3TableIndex(1.0e300);
	expect(big >= 0 && big < kQuake3FunctionTableSize, "a huge value gives a table index in range");
	expect(quake3TableIndex(-1.0) == 1023 && quake3TableIndex(1025.7) == 1, "indices truncate, then mask, like ioquake3");
	expect(quake3TableIndex(5.0e9 + 3.0) == quake3TableIndex(3.0 + std::fmod(5.0e9, 1024.0)), "huge indices wrap consistently");
	const double noise = quake3Noise(0.0, 0.0, 0.0, 1.0e12);
	expect(std::isfinite(noise) && noise >= -1.0 && noise <= 1.0, "noise at a huge time is finite and in range");
	expect(near(quake3Noise(1.25, 2.5, 3.75, 4.5), quake3Noise(1.25 + 256.0, 2.5 - 512.0, 3.75, 4.5 + 2560.0), 1.0e-9),
		"the noise lattice repeats every 256 units");
	MaterialWave wave;
	wave.function = MaterialWaveFunction::Noise;
	wave.base = 0.0;
	wave.amplitude = 1.0;
	wave.frequency = 1.0e9;
	expect(std::isfinite(evaluateQuake3Wave(wave, 1.0e7)), "rgbGen wave noise with a huge frequency stays finite");
	const int style = quakeLightStyleRaw(QStringLiteral("az"), 1.0e30);
	expect(style == 0 || style == 25 * 22, "a light style at a huge time still reads a letter");
	expect(quakeLightStyleRaw(QStringLiteral("abc"), 0.17) == 22, "light styles step at 10 Hz");

	// Doom 3: the ±40 wrap keeps the fraction and survives huge offsets.
	const QString text = QStringLiteral(
		"t/scaled { { blend add map _white scale 2, 2 translate 0.25, 0 } }\n"
		"t/far { { blend add map _white translate 41.5, time * 1000000 } }\n");
	const MaterialDefinition scaled = parsed(text, MaterialEngine::Doom3, QStringLiteral("t/scaled"));
	const Doom3TextureMatrix matrix = doom3StageMatrix(scaled.stages.value(0), registersOf(scaled), MaterialTableSet());
	expect(near(matrix.m[0][0], 2.0) && near(matrix.m[0][2], 0.5) && near(matrix.m[1][2], 0.0),
		"Doom 3 scale then translate: the newest transform acts on the raw coordinates");
	const MaterialDefinition far = parsed(text, MaterialEngine::Doom3, QStringLiteral("t/far"));
	MaterialEvalContext late;
	late.time = 1.0e25;
	const Doom3TextureMatrix farMatrix = doom3StageMatrix(far.stages.value(0), registersOf(far, late), MaterialTableSet());
	expect(near(farMatrix.m[0][2], 0.5) && std::isfinite(farMatrix.m[1][2]) && std::abs(farMatrix.m[1][2]) <= 40.0,
		"scrolls past 40 keep their fraction, even huge ones");
}

// --- Random frame durations --------------------------------------------------

void checkRandomFrames()
{
	QVector<MaterialFrame> frames(2);
	frames[0].name = QStringLiteral("A");
	frames[0].duration = 2.0 / 35.0;
	frames[0].maximumDuration = 6.0 / 35.0;
	frames[1].name = QStringLiteral("B");
	frames[1].duration = 2.0 / 35.0;
	frames[1].maximumDuration = 6.0 / 35.0;
	const quint32 seed = 7;
	const double step = 1.0 / 700.0;
	// Each frame shows for 2..6 tics, alternately, anywhere on the timeline:
	// near the start, after two hours, and across where the old walk gave up
	// (200,000 frames, about six hours here).
	for (const double start : {0.0, 7200.0, 22800.0}) {
		int current = materialFrameIndexAt(frames, start, seed);
		double since = -1.0;
		bool alternates = true;
		bool bounded = true;
		for (double time = start + step; time < start + 30.0; time += step) {
			const int index = materialFrameIndexAt(frames, time, seed);
			if (index == current) {
				continue;
			}
			alternates &= index == 1 - current;
			if (since >= 0.0) {
				const double shown = time - since;
				bounded &= shown >= 2.0 / 35.0 - 2 * step && shown <= 6.0 / 35.0 + 2 * step;
			}
			since = time;
			current = index;
		}
		expect(alternates, "random frames alternate at " + std::to_string(start) + " s");
		expect(bounded, "random frames last between their tics at " + std::to_string(start) + " s");
	}
	const int late = materialFrameIndexAt(frames, 7200.0, seed);
	expect(materialFrameIndexAt(frames, 3.0, seed) >= 0 && materialFrameIndexAt(frames, 7200.0, seed) == late, "random frames are deterministic");
	frames.resize(1);
	expect(materialFrameIndexAt(frames, 99.0, seed) == 0, "one random frame is always frame 0");
}

// --- Doom 3 stage rules ------------------------------------------------------

void checkDoom3Rules()
{
	const QString text = QStringLiteral(
		"t/pairs\n{\n"
		"\t{ blend bumpmap map _flat }\n"
		"\t{ blend diffusemap map t/a }\n"
		"\t{ blend specularmap map t/s }\n"
		"\t{ blend diffusemap map t/b }\n"
		"}\n"
		"t/bumponly { bumpmap t/n }\n"
		"t/diffuseonly { diffusemap t/a }\n"
		"t/switched { { blend add map _white if parm7 } }\n"
		"t/switchedlit { { blend diffusemap map t/a if parm7 } }\n"
		"t/clamped\n{\n"
		"\tclamp\n"
		"\tdiffusemap t/a\n"
		"\t{ blend add map t/b noclamp }\n"
		"\t{ blend add map t/c zeroclamp }\n"
		"\t{ blend filter map t/d alphazeroclamp }\n"
		"}\n"
		"t/repeats { diffusemap t/a }\n");
	const MaterialDefinition pairs = parsed(text, MaterialEngine::Doom3, QStringLiteral("t/pairs"));
	const QVector<int> order = doom3SortedStageOrder(pairs);
	expect(order == QVector<int>({0, 1, 3, 2}), "SortInteractionStages puts diffuse before specular after the bump");
	bool implicitBump = true;
	const QVector<Doom3Interaction> interactions = doom3PairInteractions(pairs, order, registersOf(pairs), nullptr, &implicitBump);
	expect(!implicitBump && interactions.size() == 2, "a second diffuse flushes an interaction");
	if (interactions.size() == 2) {
		expect(interactions.at(0).diffuse && interactions.at(0).diffuse->imagePath == QStringLiteral("t/a") && !interactions.at(0).specular,
			"the first interaction is the first diffuse alone");
		expect(interactions.at(1).diffuse && interactions.at(1).diffuse->imagePath == QStringLiteral("t/b") && interactions.at(1).specular
				&& interactions.at(1).bump == interactions.at(0).bump,
			"the second keeps the bump and takes the specular");
	}

	const MaterialDefinition bumpOnly = parsed(text, MaterialEngine::Doom3, QStringLiteral("t/bumponly"));
	expect(doom3NeedsImplicitDiffuse(bumpOnly) && !doom3NeedsImplicitBump(bumpOnly), "a bump alone gets diffusemap _white");
	const MaterialStage white = doom3ImplicitDiffuseStage();
	const QVector<Doom3Interaction> lit = doom3PairInteractions(bumpOnly, doom3SortedStageOrder(bumpOnly), registersOf(bumpOnly), &white, &implicitBump);
	expect(lit.size() == 1 && lit.first().diffuse == &white && lit.first().bump == &bumpOnly.stages.at(0),
		"the implicit _white diffuse pairs with the bump");
	const MaterialDefinition diffuseOnly = parsed(text, MaterialEngine::Doom3, QStringLiteral("t/diffuseonly"));
	expect(doom3NeedsImplicitBump(diffuseOnly) && !doom3NeedsImplicitDiffuse(diffuseOnly), "a diffuse alone gets bumpmap _flat");

	MaterialEvalContext off;
	off.parms[7] = 0.0;
	MaterialEvalContext on;
	on.parms[7] = 1.0;
	const MaterialDefinition switched = parsed(text, MaterialEngine::Doom3, QStringLiteral("t/switched"));
	expect(!doom3AnyStageActive(switched, registersOf(switched, off)) && doom3AnyStageActive(switched, registersOf(switched, on)),
		"a material whose only stage is conditioned off is not drawn");
	const MaterialDefinition switchedLit = parsed(text, MaterialEngine::Doom3, QStringLiteral("t/switchedlit"));
	expect(doom3AnyStageActive(switchedLit, registersOf(switchedLit, off)), "the implicit _flat stage is never conditioned off");

	const MaterialDefinition clamped = parsed(text, MaterialEngine::Doom3, QStringLiteral("t/clamped"));
	if (expect(clamped.stages.size() == 4, "the clamped material has four stages")) {
		expect(doom3StageWrap(clamped.stages.at(0), clamped) == Wrap::Clamp, "the material's clamp reaches a diffusemap shorthand");
		expect(doom3StageWrap(clamped.stages.at(1), clamped) == Wrap::Repeat, "a stage's noclamp repeats under a clamped material");
		expect(doom3StageWrap(clamped.stages.at(2), clamped) == Wrap::ZeroClamp, "zeroclamp");
		expect(doom3StageWrap(clamped.stages.at(3), clamped) == Wrap::AlphaZeroClamp, "alphazeroclamp is its own rule");
	}
	const MaterialDefinition repeats = parsed(text, MaterialEngine::Doom3, QStringLiteral("t/repeats"));
	expect(doom3StageWrap(repeats.stages.value(0), repeats) == Wrap::Repeat, "stages repeat by default");
}

// --- Upload keys and cancellation --------------------------------------------

MaterialTexturePtr solidTexture(QRgb color, const QString& status = QStringLiteral("image"))
{
	QImage image(8, 8, QImage::Format_ARGB32);
	image.fill(color);
	return std::make_shared<MaterialTexture>(materialTextureFromImage(image, QStringLiteral("t/a"), QStringLiteral("t/a.tga"), status));
}

void checkUploadKeys()
{
	const Mesh mesh = buildMesh(MaterialPreviewShape::Wall, 64.0, 64.0, 1.0, 64.0);
	MaterialRenderOptions options;
	const Camera camera = makeCamera(mesh, options);
	MaterialTexturePtr texture = solidTexture(qRgb(200, 10, 10));
	const auto keyIn = [&](const MaterialTexturePtr& candidate, quint64* owner = nullptr) {
		MaterialGpuFrame frame(camera, mesh, options);
		const int index = frame.texture(candidate);
		if (owner) {
			*owner = frame.frame().owner;
		}
		return index < 0 ? quint64(0) : frame.frame().textures.at(index).cacheKey;
	};
	quint64 owner = 0;
	const quint64 first = keyIn(texture, &owner);
	expect(owner != 0 && first != 0 && keyIn(texture) == first, "a texture keeps its upload key from frame to frame");
	MaterialGpuFrame frame(camera, mesh, options);
	const int index = frame.texture(texture);
	expect(index >= 0 && frame.frame().textures.at(index).levels.value(0).constBits() == reinterpret_cast<const uchar*>(texture->pixels.constData()),
		"the frame's image points into the texture's pixels");
	expect(frame.frame().buffers.value(frame.meshIndices()).cacheKey != 0, "the mesh's indices are kept on the device");
	const auto copy = std::make_shared<MaterialTexture>(*texture);
	expect(keyIn(copy) != first, "a copy of a texture gets its own key");
	texture.reset();
	for (int i = 0; i < 8; ++i) {
		const MaterialTexturePtr next = solidTexture(qRgb(10, 200, 10));
		expect(keyIn(next) != first, "a new texture never takes a freed texture's key");
	}
	expect(keyIn(solidTexture(qRgb(1, 2, 3), QStringLiteral("video"))) == 0, "video frames stay out of the cache");
	MaterialGpuFrame palettes(camera, mesh, options);
	const QVector<QRgb> grey(256, qRgb(128, 128, 128));
	QVector<QRgb> other = grey;
	other[5] = qRgb(1, 2, 3);
	const quint64 a = palettes.frame().textures.value(palettes.palette(grey)).cacheKey;
	const quint64 b = palettes.frame().textures.value(palettes.palette(grey)).cacheKey;
	const quint64 c = palettes.frame().textures.value(palettes.palette(other)).cacheKey;
	expect(a != 0 && a == b && a != c, "palettes are keyed by their colours");

	// A set cancel flag stops the frame before the device, with no image.
	const MaterialDefinition definition = parsed(QStringLiteral("t/plain\n{\n\t{\n\t\tmap $whiteimage\n\t}\n}\n"), MaterialEngine::Quake3,
		QStringLiteral("t/plain"));
	std::atomic_bool cancel {true};
	MaterialRenderOptions cancelled;
	cancelled.size = QSize(32, 32);
	cancelled.cancelFlag = &cancel;
	const MaterialRenderResult result = renderMaterial(definition, MaterialImageSet(), MaterialTableSet(), cancelled);
	expect(result.cancelled && result.image.isNull(), "a set cancel flag cancels the render");
	const MaterialRenderResult swatch = renderMaterialSwatch(definition, MaterialImageSet(), MaterialTableSet(), 32, 0.0, {}, &cancel);
	expect(swatch.cancelled && swatch.image.isNull(), "a set cancel flag cancels a swatch");
	MaterialRenderOptions base;
	base.cancelFlag = &cancel;
	const MaterialRenderResult baseSwatch = renderMaterialSwatch(definition, MaterialImageSet(), MaterialTableSet(), 32, 0.0, base);
	expect(baseSwatch.cancelled && baseSwatch.image.isNull(), "a swatch from base options keeps their cancel flag");
}

// One loop of a material, shared by the page's timeline, Export Animation
// Sheet and `material render --cycle`.
void checkCycleSeconds()
{
	const QString text = QStringLiteral(
		"t/still\n{\n\t{\n\t\tmap t/a.tga\n\t}\n}\n"
		"t/quick\n{\n\t{\n\t\tanimMap 4 t/a.tga t/b.tga\n\t}\n}\n"
		"t/slow\n{\n\t{\n\t\tanimMap 0.5 t/a.tga t/b.tga t/c.tga t/d.tga\n\t}\n}\n"
		"t/glacial\n{\n\t{\n\t\tanimMap 0.01 t/a.tga t/b.tga\n\t}\n}\n"
		"t/mixed\n{\n\t{\n\t\tanimMap 2 t/a.tga t/b.tga t/c.tga\n\t}\n\t{\n\t\tanimMap 1 t/a.tga t/b.tga\n\t}\n}\n");
	const MaterialImageSet none;
	const auto cycleOf = [&](const char* name) {
		return materialCycleSeconds(parsed(text, MaterialEngine::Quake3, QString::fromLatin1(name)), none);
	};
	expect(near(cycleOf("t/still"), 10.0), "a material that does not animate by frames loops over 10 seconds");
	expect(near(cycleOf("t/quick"), 1.0), "a loop shorter than a second is drawn over one second");
	expect(near(cycleOf("t/slow"), 8.0), "animMap 0.5 over four images loops in 8 seconds");
	expect(near(cycleOf("t/glacial"), 60.0), "a loop is at most 60 seconds");
	expect(near(cycleOf("t/mixed"), 2.0), "the longest stage's animMap sets the loop");
}

// --- Drawing -----------------------------------------------------------------

void writeFile(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(bytes);
	}
}

QByteArray tga(const QImage& source)
{
	// Uncompressed 32-bit, top-left origin.
	const QImage image = source.convertToFormat(QImage::Format_ARGB32);
	QByteArray bytes(18, '\0');
	bytes[2] = 2;
	qToLittleEndian<quint16>(static_cast<quint16>(image.width()), bytes.data() + 12);
	qToLittleEndian<quint16>(static_cast<quint16>(image.height()), bytes.data() + 14);
	bytes[16] = 32;
	bytes[17] = 0x28;
	for (int y = 0; y < image.height(); ++y) {
		const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
		for (int x = 0; x < image.width(); ++x) {
			bytes.append(static_cast<char>(qBlue(line[x])));
			bytes.append(static_cast<char>(qGreen(line[x])));
			bytes.append(static_cast<char>(qRed(line[x])));
			bytes.append(static_cast<char>(qAlpha(line[x])));
		}
	}
	return bytes;
}

QImage solid(int size, QRgb color)
{
	QImage image(size, size, QImage::Format_ARGB32);
	image.fill(color);
	return image;
}

QImage checker(int size, int cell)
{
	QImage image(size, size, QImage::Format_ARGB32);
	for (int y = 0; y < size; ++y) {
		for (int x = 0; x < size; ++x) {
			image.setPixel(x, y, ((x / cell + y / cell) % 2) != 0 ? qRgb(255, 255, 255) : qRgb(0, 0, 0));
		}
	}
	return image;
}

QRgb centre(const QImage& image)
{
	return image.isNull() ? 0u : image.pixel(image.width() / 2, image.height() / 2);
}

void checkDrawing(const QString& root)
{
	writeFile(root + QStringLiteral("/textures/t/red.tga"), tga(solid(16, qRgb(255, 0, 0))));
	writeFile(root + QStringLiteral("/textures/t/green.tga"), tga(solid(16, qRgb(0, 255, 0))));
	writeFile(root + QStringLiteral("/textures/t/checker.tga"), tga(checker(256, 1)));
	QImage cutout = solid(16, qRgba(255, 255, 255, 255));
	writeFile(root + QStringLiteral("/textures/t/cutout.tga"), tga(cutout));
	writeFile(root + QStringLiteral("/scripts/t.shader"), QByteArray(
		"textures/t/red\n{\n\t{\n\t\tmap textures/t/red.tga\n\t\trgbGen identity\n\t}\n}\n"
		"textures/t/green\n{\n\t{\n\t\tmap textures/t/green.tga\n\t\trgbGen identity\n\t}\n}\n"
		"textures/t/mipped\n{\n\t{\n\t\tmap textures/t/checker.tga\n\t\trgbGen identity\n\t}\n}\n"
		"textures/t/unmipped\n{\n\tnomipmaps\n\t{\n\t\tmap textures/t/checker.tga\n\t\trgbGen identity\n\t}\n}\n"));
	writeFile(root + QStringLiteral("/materials/t.mtr"), QByteArray(
		"textures/t/bumped\n{\n\tbumpmap _flat\n}\n"
		"textures/t/litglass\n{\n\ttranslucent\n\tdiffusemap textures/t/red\n}\n"
		"textures/t/fence\n{\n\t{\n\t\tblend diffusemap\n\t\tmap textures/t/cutout\n\t\talphaTest 0.5\n\t\tif parm7\n\t}\n}\n"
		"textures/t/screen\n{\n\t{\n\t\tblend add\n\t\tmap textures/t/red\n\t\tif parm7\n\t}\n}\n"
		"textures/t/filtered\n{\n\ttranslucent\n\t{\n\t\tblend filter\n\t\tmap textures/t/red\n\t\talphazeroclamp\n\t\ttranslate 0, 1\n\t}\n}\n"
		"textures/t/zeroed\n{\n\ttranslucent\n\t{\n\t\tblend filter\n\t\tmap textures/t/red\n\t\tzeroclamp\n\t\ttranslate 0, 1\n\t}\n}\n"));
	PackageArchive archive;
	QString error;
	if (!expect(archive.load(root, &error), "the test folder loads")) {
		return;
	}
	const MaterialLibrary library = scanMaterialLibrary(archive);
	MaterialImageSource source;
	source.archive = std::make_shared<PackageArchive>(archive);
	const auto definitionOf = [&](const QString& name, MaterialEngine engine) { return library.definition(library.indexOf(name, engine)); };
	MaterialRenderOptions flat;
	flat.size = QSize(96, 96);
	flat.orthographic = true;
	flat.tiling = 1.0;
	flat.checker = false;
	flat.background = QColor(0, 0, 0);
	flat.lighting.lightmapSpot = false;
	flat.lighting.lightOrbit = false;
	flat.lighting.lightAngle = 0.0;
	const auto render = [&](const QString& name, MaterialEngine engine, const MaterialRenderOptions& options) {
		const MaterialDefinition definition = definitionOf(name, engine);
		const MaterialImageSet images = resolveMaterialImages(definition, source);
		const MaterialRenderResult result = renderMaterial(definition, images, library.tables, options);
		if (qEnvironmentVariableIsSet("MATERIAL_TEST_PIXELS")) {
			std::cerr << name.toStdString() << ": stages " << result.stagesDrawn << ", " << result.notes.join(QStringLiteral(" | ")).toStdString()
					  << " missing=" << images.missing().join(QLatin1Char(',')).toStdString() << '\n';
		}
		return result;
	};

	// D1: kept uploads draw the same, and a new decode is never mistaken
	// for a freed one.
	const QImage red1 = render(QStringLiteral("textures/t/red"), MaterialEngine::Quake3, flat).image;
	const QImage red2 = render(QStringLiteral("textures/t/red"), MaterialEngine::Quake3, flat).image;
	expect(!red1.isNull() && red1 == red2 && qRed(centre(red1)) > 240, "a texture drawn twice from the device's copy looks the same");
	for (int round = 0; round < 4; ++round) {
		const QImage green = render(QStringLiteral("textures/t/green"), MaterialEngine::Quake3, flat).image;
		const QImage red = render(QStringLiteral("textures/t/red"), MaterialEngine::Quake3, flat).image;
		expect(qGreen(centre(green)) > 240 && qRed(centre(green)) < 16 && qRed(centre(red)) > 240 && qGreen(centre(red)) < 16,
			"fresh decodes never draw another texture's upload");
	}

	// D8: nomipmaps samples the full-size image when minified.
	MaterialRenderOptions small = flat;
	small.size = QSize(16, 16);
	const QImage mipped = render(QStringLiteral("textures/t/mipped"), MaterialEngine::Quake3, small).image;
	const QImage unmipped = render(QStringLiteral("textures/t/unmipped"), MaterialEngine::Quake3, small).image;
	expect(!mipped.isNull() && !unmipped.isNull() && mipped != unmipped, "nomipmaps changes a minified image");

	// D2: a bump alone is lit through the implicit _white diffuse.
	MaterialRenderOptions lit = flat;
	lit.orthographic = false;
	lit.tiling = 0.0;
	const MaterialRenderResult bumped = render(QStringLiteral("textures/t/bumped"), MaterialEngine::Doom3, lit);
	expect(qRed(centre(bumped.image)) > 40, "a material with only a bump stage is lit white");
	// D3: translucent lighting stages are lit.
	const MaterialRenderResult glass = render(QStringLiteral("textures/t/litglass"), MaterialEngine::Doom3, lit);
	expect(qRed(centre(glass.image)) > 40 && qGreen(centre(glass.image)) < 20, "a translucent diffusemap is lit by the preview light");

	// D4: conditioned-off stages.
	MaterialRenderOptions grey = flat;
	grey.background = QColor(128, 128, 128);
	grey.eval.parms[7] = 0.0;
	const QRgb fence = centre(render(QStringLiteral("textures/t/fence"), MaterialEngine::Doom3, grey).image);
	expect(qRed(fence) < 20 && qGreen(fence) < 20, "a perforated material whose alpha-tested stage is off fills solid black");
	const MaterialRenderResult screen = render(QStringLiteral("textures/t/screen"), MaterialEngine::Doom3, grey);
	expect(std::abs(qRed(centre(screen.image)) - 128) <= 4, "a material whose every stage is off is not drawn");
	grey.eval.parms[7] = 1.0;
	expect(qRed(centre(render(QStringLiteral("textures/t/screen"), MaterialEngine::Doom3, grey).image)) > 240, "switched on, it draws");

	// F6: a swatch from base options takes their parms and background and
	// forces only its framing (a square of `side`).
	{
		const MaterialDefinition screenDefinition = definitionOf(QStringLiteral("textures/t/screen"), MaterialEngine::Doom3);
		const MaterialImageSet screenImages = resolveMaterialImages(screenDefinition, source);
		MaterialRenderOptions base = grey;
		base.size = QSize(200, 50);
		base.shape = MaterialPreviewShape::Sphere;
		base.eval.parms[7] = 1.0;
		const QImage on = renderMaterialSwatch(screenDefinition, screenImages, library.tables, 24, 0.0, base).image;
		expect(on.size() == QSize(24, 24), "a swatch is a square of its side whatever the base size");
		expect(qRed(centre(on)) > 240 && std::abs(qGreen(centre(on)) - 128) <= 6, "a swatch adds onto the base background with the base parms");
		base.eval.parms[7] = 0.0;
		const QImage off = renderMaterialSwatch(screenDefinition, screenImages, library.tables, 24, 0.0, base).image;
		expect(std::abs(qRed(centre(off)) - 128) <= 6, "a swatch honours a base parm that switches every stage off");
	}

	// D6: the zero clamps' borders, over white at two repeats. A wall's t
	// runs down from 0 like a brush face, so the stages move it up by one.
	MaterialRenderOptions white = flat;
	white.background = QColor(255, 255, 255);
	white.tiling = 2.0;
	const auto quadrants = [](const QImage& image, const std::function<bool(QRgb)>& test) {
		int count = 0;
		for (const QPoint& point : {QPoint(24, 24), QPoint(72, 24), QPoint(24, 72), QPoint(72, 72)}) {
			const QRgb pixel = image.isNull() ? 0u : image.pixel(point);
			if (qEnvironmentVariableIsSet("MATERIAL_TEST_PIXELS")) {
				std::cerr << "pixel " << point.x() << ',' << point.y() << ": " << qRed(pixel) << ' ' << qGreen(pixel) << ' ' << qBlue(pixel) << '\n';
			}
			count += !image.isNull() && test(pixel) ? 1 : 0;
		}
		return count;
	};
	const auto isRed = [](QRgb c) { return qRed(c) > 200 && qGreen(c) < 40; };
	const auto isWhite = [](QRgb c) { return qRed(c) > 200 && qGreen(c) > 200; };
	const auto isBlack = [](QRgb c) { return qRed(c) < 40 && qGreen(c) < 40; };
	MaterialRenderOptions once = white;
	once.tiling = 1.0;
	const QRgb inside = centre(render(QStringLiteral("textures/t/filtered"), MaterialEngine::Doom3, once).image);
	if (qEnvironmentVariableIsSet("MATERIAL_TEST_PIXELS")) {
		std::cerr << "inside: " << qRed(inside) << ' ' << qGreen(inside) << ' ' << qBlue(inside) << '\n';
	}
	expect(isRed(inside), "inside the image an alphazeroclamp stage draws its colour");
	const QImage filtered = render(QStringLiteral("textures/t/filtered"), MaterialEngine::Doom3, white).image;
	expect(quadrants(filtered, isRed) == 1 && quadrants(filtered, isWhite) == 3, "alphazeroclamp leaves the colour beyond the image under blend filter");
	const QImage zeroed = render(QStringLiteral("textures/t/zeroed"), MaterialEngine::Doom3, white).image;
	expect(quadrants(zeroed, isRed) == 1 && quadrants(zeroed, isBlack) == 3, "zeroclamp is opaque black beyond the image");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	checkQuake3Rules();
	checkBoundedValues();
	checkRandomFrames();
	checkDoom3Rules();
	checkUploadKeys();
	checkCycleSeconds();
	// Drawing needs OpenGL or Vulkan; every other check runs anywhere.
	const int renderSkip = slopstudio::test_support::exitCodeWithoutRenderer("material-render-rules-smoke");
	QTemporaryDir directory;
	if (renderSkip == 1) {
		++failures;
	} else if (renderSkip < 0 && expect(directory.isValid(), "a temporary folder")) {
		checkDrawing(directory.path());
	}
	if (failures > 0) {
		std::cerr << failures << " material render rules check(s) failed\n";
		return 1;
	}
	std::cout << "material render rules passed\n";
	return 0;
}
