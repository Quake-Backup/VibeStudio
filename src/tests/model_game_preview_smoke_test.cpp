// The Game preview's skins (UVSKIN-08, core/model_game_preview.h) and the
// model game-skin command: Quake fullbrights per renderer, point sampling,
// player colour translation, GLQuake's flood fill, skin group timing, and
// Quake III stages composed in texture space (an additive glow stage, an
// alphaFunc cut-out, tcMod scroll over time). Fixtures are built here.
#include "core/material_images.h"
#include "core/material_script.h"
#include "core/model_document.h"
#include "core/model_game_preview.h"
#include "tests/model_legacy_fuzz_seeds.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;

namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *message)
{
	++checks;
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}

// A 4x4 single-triangle MDL mesh with one indexed skin (or a group).
ModelMesh quakeMesh(const QVector<QByteArray> &members, const QVector<float> &intervals = {})
{
	ModelMesh mesh;
	mesh.geometryAvailable = true;
	mesh.mdl.enabled = true;
	mesh.mdl.skinSize = {4, 4};
	// Gray ramp, black at 0, and red fullbrights from 224.
	for (int index = 0; index < 256; ++index)
	{
		const char value = char(index);
		mesh.mdl.palette.append(index >= 224 ? char(255) : value);
		mesh.mdl.palette.append(index >= 224 ? char(0) : value);
		mesh.mdl.palette.append(index >= 224 ? char(0) : value);
	}
	ModelEmbeddedSkin skin;
	skin.index = 0;
	skin.indexedFrames = members;
	skin.intervals = intervals;
	skin.groupFrameCount = int(members.size());
	mesh.embeddedSkins.append(skin);
	ModelSurface surface;
	surface.name = "body";
	surface.triangles = {{0, 1, 2}};
	surface.texCoords = {{0, 0}, {1, 0}, {0, 1}};
	surface.vertexCount = 3;
	ModelFrameGeometry pose;
	pose.positions = {{0, 0, 0}, {8, 0, 0}, {0, 8, 0}};
	pose.normals.fill({0, 0, 1}, 3);
	surface.frames = {pose};
	mesh.surfaces = {surface};
	mesh.frames = {ModelFrameInfo{0, "frame", {}, {}, {}, 0}};
	return mesh;
}
QByteArray filled(int index)
{
	return QByteArray(16, char(index));
}
int texel(const QImage &image, int x, int y)
{
	return qRed(image.pixel(x, y));
}
QImage solid(int w, int h, QRgb colour)
{
	QImage image(w, h, QImage::Format_ARGB32);
	image.fill(colour);
	return image;
}
void addTexture(MaterialImageSet *images, const QString &path, const QImage &image)
{
	images->textures.insert(materialImageKey(path), std::make_shared<MaterialTexture>(materialTextureFromImage(image, path, path, QStringLiteral("image"))));
}
MaterialDefinition shader(const char *text)
{
	const MaterialScript script = parseMaterialScript(QString::fromLatin1(text), MaterialEngine::Quake3, QStringLiteral("scripts/test.shader"));
	return script.materials.value(0);
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QString error;

	// Fullbrights: unlit in modern ports and the software renderer, lit in GLQuake.
	QByteArray mixed = filled(100);
	mixed[5] = char(240);
	const ModelMesh fullbright = quakeMesh({mixed});
	ModelGamePreviewOptions modern;
	// The whole skin is the top-left texel's colour, so flood fill would
	// repaint it; these checks are about fullbrights alone.
	modern.floodFill = false;
	ModelGameSkin skin;
	if (expect(composeModelQuakeSkin(fullbright, 0, modern, &skin, &error), "a Quake skin composes"))
	{
		expect(qRed(skin.base.pixel(1, 1)) == 0 && !skin.glow.isNull() && qRed(skin.glow.pixel(1, 1)) == 255 && qRed(skin.base.pixel(0, 0)) == 100 &&
				   qRed(skin.glow.pixel(0, 0)) == 0 && !skin.nearest,
			   "a modern port draws fullbright index 240 in the unlit layer only");
	}
	ModelGamePreviewOptions gl;
	gl.quakeRenderer = MaterialQuakeRenderer::GLQuake;
	gl.floodFill = false;
	expect(composeModelQuakeSkin(fullbright, 0, gl, &skin, &error) && skin.glow.isNull() && qRed(skin.base.pixel(1, 1)) == 255,
		   "GLQuake lights fullbright texels like the rest");
	ModelGamePreviewOptions software;
	software.quakeRenderer = MaterialQuakeRenderer::Software;
	expect(composeModelQuakeSkin(fullbright, 0, software, &skin, &error) && skin.nearest && !skin.glow.isNull(),
		   "the software renderer samples point-wise and keeps fullbrights unlit");

	// Player colours: top 16-31, bottom 96-111.
	const QByteArray table = modelQuakePlayerTranslation(4, 12);
	expect(quint8(table[16]) == 64 && quint8(table[31]) == 79 && quint8(table[96]) == 192 + 15 && quint8(table[111]) == 192 &&
			   quint8(table[15]) == 15 && quint8(table[32]) == 32 && quint8(table[200]) == 200,
		   "the translation remaps only indices 16-31 and 96-111, light to dark by row");
	QByteArray ranges = filled(50);
	ranges[0] = char(16);
	ranges[1] = char(96);
	ModelGamePreviewOptions coloured;
	coloured.topColor = 4;
	coloured.bottomColor = 12;
	coloured.floodFill = false;
	expect(composeModelQuakeSkin(quakeMesh({ranges}), 0, coloured, &skin, &error) && texel(skin.base, 0, 0) == 64 && texel(skin.base, 1, 0) == 207 &&
			   texel(skin.base, 2, 0) == 50,
		   "player colours show in the composed skin");

	// GLQuake's flood fill: only the first texel's connected region changes.
	QByteArray background = filled(9);
	background[0] = char(5);
	background[1] = char(5);
	background[15] = char(5); // the same colour, not connected
	ModelGamePreviewOptions filling;
	filling.quakeRenderer = MaterialQuakeRenderer::GLQuake;
	expect(composeModelQuakeSkin(quakeMesh({background}), 0, filling, &skin, &error) && texel(skin.base, 0, 0) == 9 && texel(skin.base, 1, 0) == 9 &&
			   texel(skin.base, 3, 3) == 5,
		   "flood fill replaces the first texel's region with its neighbours' colour");
	filling.floodFill = false;
	expect(composeModelQuakeSkin(quakeMesh({background}), 0, filling, &skin, &error) && texel(skin.base, 0, 0) == 5, "flood fill can be turned off");
	filling.floodFill = true;
	filling.quakeRenderer = MaterialQuakeRenderer::Software;
	expect(composeModelQuakeSkin(quakeMesh({background}), 0, filling, &skin, &error) && texel(skin.base, 0, 0) == 5, "the software renderer does not flood fill");

	// Skin groups follow their cumulative end times.
	const ModelMesh group = quakeMesh({filled(10), filled(20)}, {0.1f, 0.2f});
	ModelGamePreviewOptions at;
	at.floodFill = false;
	int shown[3] = {-1, -1, -1};
	const double times[3] = {0.05, 0.15, 0.25};
	for (int i = 0; i < 3; ++i)
	{
		at.time = times[i];
		if (composeModelQuakeSkin(group, 0, at, &skin, &error))
			shown[i] = texel(skin.base, 2, 2);
	}
	expect(shown[0] == 10 && shown[1] == 20 && shown[2] == 10, "skin group members show by their end times and loop");

	// Quake III: an opaque lit base and an additive identity glow stage.
	MaterialImageSet images;
	QImage base = solid(8, 8, qRgba(100, 100, 100, 255));
	QImage glow = solid(8, 8, qRgba(0, 0, 0, 255));
	glow.setPixel(2, 2, qRgba(200, 200, 200, 255));
	QImage cut = solid(8, 8, qRgba(50, 50, 50, 255));
	for (int x = 0; x < 8; ++x)
		cut.setPixel(x, 0, qRgba(50, 50, 50, 20));
	QImage stripe = solid(8, 8, qRgba(0, 0, 0, 255));
	for (int y = 0; y < 8; ++y)
		stripe.setPixel(0, y, qRgba(255, 255, 255, 255));
	addTexture(&images, "models/test/base.tga", base);
	addTexture(&images, "models/test/glow.tga", glow);
	addTexture(&images, "models/test/cut.tga", cut);
	addTexture(&images, "models/test/stripe.tga", stripe);
	const MaterialDefinition glowing = shader("models/test/glowing\n{\n\t{\n\t\tmap models/test/base.tga\n\t\trgbGen lightingDiffuse\n\t}\n"
											  "\t{\n\t\tmap models/test/glow.tga\n\t\tblendFunc add\n\t\trgbGen identity\n\t}\n}\n");
	ModelGamePreviewOptions q3;
	if (expect(glowing.stages.size() == 2 && composeModelQuake3Skin(glowing, images, q3, &skin, &error), "a two-stage Quake III shader composes"))
	{
		expect(skin.stagesDrawn == 2 && skin.base.width() == 8 && qRed(skin.base.pixel(5, 5)) == 100 && qAlpha(skin.base.pixel(5, 5)) == 255,
			   "the lightingDiffuse stage is the lit layer");
		expect(!skin.glow.isNull() && qRed(skin.glow.pixel(2, 2)) > 150 && qRed(skin.glow.pixel(5, 5)) == 0,
			   "the additive identity stage brightens only its texels, unlit");
	}
	const MaterialDefinition cutout = shader("models/test/cutout\n{\n\t{\n\t\tmap models/test/cut.tga\n\t\talphaFunc GE128\n\t\trgbGen lightingDiffuse\n\t}\n}\n");
	expect(composeModelQuake3Skin(cutout, images, q3, &skin, &error) && qAlpha(skin.base.pixel(3, 0)) == 0 && qAlpha(skin.base.pixel(3, 4)) == 255,
		   "alphaFunc GE128 cuts out texels below half alpha");
	const MaterialDefinition scrolling = shader("models/test/scroll\n{\n\t{\n\t\tmap models/test/stripe.tga\n\t\ttcMod scroll 0.5 0\n\t\trgbGen lightingDiffuse\n\t}\n}\n");
	ModelGameSkin early, late;
	ModelGamePreviewOptions later;
	later.time = 0.5;
	expect(composeModelQuake3Skin(scrolling, images, q3, &early, &error) && composeModelQuake3Skin(scrolling, images, later, &late, &error) &&
			   early.base != late.base,
		   "tcMod scroll moves the texture between t = 0 and t = 0.5");
	expect(composeModelQuake3Skin(scrolling, images, q3, &late, &error) && early.base == late.base, "the same time composes the same image");

	// The command: a rich MDL with a skin group and a fullbright row.
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (argc == 2 && !root.isEmpty() && QDir().mkpath(root))
	{
		QTemporaryDir temporary(QDir(root).filePath("game-skin-XXXXXX"));
		const QString program = QString::fromLocal8Bit(argv[1]);
		const QString model = temporary.filePath("rich.mdl");
		QFile file(model);
		const QByteArray bytes = tests::richQuakeMdl();
		expect(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write the MDL");
		file.close();
		const auto run = [&](const QStringList &extra, QJsonObject *json) {
			QProcess process;
			process.start(program, QStringList{"--cli", "model", "game-skin", model, "--json", "--overwrite"} + extra);
			if (!process.waitForFinished(120000))
				return -1;
			*json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
			return process.exitCode();
		};
		QJsonObject first, second, software;
		expect(run({"--time", "0.05", "--output", temporary.filePath("a.png")}, &first) == 0 && run({"--time", "0.15", "--output", temporary.filePath("b.png")}, &second) == 0 &&
				   first.value("frame").toInt() == 0 && second.value("frame").toInt() == 1 &&
				   first.value("baseSha256").toString() != second.value("baseSha256").toString() && QFileInfo::exists(temporary.filePath("a.png")),
			   "model game-skin follows the skin group over time");
		expect(run({"--quake-renderer", "software", "--top", "3", "--output", temporary.filePath("c.png"), "--glow-output", temporary.filePath("g.png")}, &software) == 0 &&
				   software.value("nearest").toBool() && QFileInfo::exists(temporary.filePath("g.png")),
			   "model game-skin reports point sampling for the software renderer and writes the unlit layer");
		QJsonObject refused;
		expect(run({"--quake-renderer", "voodoo", "--output", temporary.filePath("d.png")}, &refused) == 2, "an unknown renderer is a usage error");
	}
	std::cout << "model_game_preview_smoke_test: " << checks << " checks, " << failures << " failure(s)\n";
	return failures == 0 ? 0 : 1;
}
