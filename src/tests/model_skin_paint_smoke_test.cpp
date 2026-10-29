// Painting skins on the model (UVSKIN-09, core/model_skin_paint.h, model
// paint-skin) on a cube whose six faces are separate UV islands: a stroke on
// the visible face changes only its texels and never the occluded back face, a
// stroke across an edge paints both islands, the mirror axis paints the
// mirrored texels, indexed skins take exact palette indices, cancellation
// leaves the skin alone, and the command replays strokes deterministically.
// Round 2: a perspective view (as the Mesh Editor viewport paints) still never
// paints through the model, stamps and smudge, the painted MDL skin goes back
// into the model as one edit (and through model paint-skin --model-output),
// painted skins encode by extension and stage into a package.
#include "core/idtech_image.h"
#include "core/model_document.h"
#include "core/model_mdl.h"
#include "core/model_skin_paint.h"
#include "core/package_archive.h"
#include "core/package_staging.h"

#include <QCoreApplication>
#include <QCryptographicHash>
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

// Faces +X, -X, +Y, -Y, +Z, -Z; face k's island is column k % 3, row k / 3
// of a 3 x 2 atlas.
ModelMesh cube()
{
	const ModelVec3 faces[6][4] = {{{1, -1, -1}, {1, 1, -1}, {1, 1, 1}, {1, -1, 1}},
		{{-1, 1, -1}, {-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}},
		{{1, 1, -1}, {-1, 1, -1}, {-1, 1, 1}, {1, 1, 1}},
		{{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},
		{{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},
		{{-1, 1, -1}, {1, 1, -1}, {1, -1, -1}, {-1, -1, -1}}};
	const ModelVec3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	ModelSurface surface;
	surface.name = "cube";
	ModelFrameGeometry pose;
	for (int k = 0; k < 6; ++k)
	{
		const float u0 = float(k % 3) / 3.0f, u1 = float(k % 3 + 1) / 3.0f, v0 = float(k / 3) / 2.0f, v1 = float(k / 3 + 1) / 2.0f;
		const int base = int(pose.positions.size());
		for (int c = 0; c < 4; ++c)
		{
			pose.positions.append(faces[k][c]);
			pose.normals.append(normals[k]);
		}
		surface.texCoords.append({u0, v1});
		surface.texCoords.append({u1, v1});
		surface.texCoords.append({u1, v0});
		surface.texCoords.append({u0, v0});
		surface.triangles.append({base, base + 1, base + 2});
		surface.triangles.append({base, base + 2, base + 3});
	}
	surface.frames = {pose};
	surface.vertexCount = int(pose.positions.size());
	ModelMesh mesh;
	mesh.geometryAvailable = true;
	mesh.surfaces = {surface};
	mesh.frames = {ModelFrameInfo{0, "frame", {}, {}, {}, 0}};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
QImage white()
{
	QImage image(96, 64, QImage::Format_ARGB32);
	image.fill(Qt::white);
	return image;
}
// Painted texels (not white) inside a face's island.
int paintedIn(const QImage &image, int face)
{
	int count = 0;
	for (int y = (face / 3) * 32; y < (face / 3 + 1) * 32; ++y)
		for (int x = (face % 3) * 32; x < (face % 3 + 1) * 32; ++x)
			count += image.pixel(x, y) != qRgb(255, 255, 255) ? 1 : 0;
	return count;
}
ModelSkinPaintView viewFrom(const ModelMesh &mesh, double yaw)
{
	ModelSkinPaintCamera camera;
	camera.yaw = yaw;
	camera.pitch = 0;
	camera.size = {128, 128};
	ModelSkinPaintView view;
	QString error;
	buildModelSkinPaintView(mesh, 0, camera, &view, &error);
	return view;
}
// A perspective camera at `position` looking along `forward`, as the Mesh
// Editor viewport describes itself (model_viewport_skin_paint.cpp).
ModelSkinPaintProjection perspective(ModelVec3 position, ModelVec3 forward, ModelVec3 right, ModelVec3 up, double focal, int size)
{
	const auto dot = [](ModelVec3 a, ModelVec3 b) { return double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z; };
	const double o = size / 2.0;
	ModelSkinPaintProjection projection;
	projection.size = {size, size};
	projection.perspective = true;
	projection.eye = {-forward.x, -forward.y, -forward.z};
	projection.matrix = {o * forward.x + focal * right.x, o * forward.y + focal * right.y, o * forward.z + focal * right.z,
		-(o * dot(forward, position) + focal * dot(right, position)),
		o * forward.x - focal * up.x, o * forward.y - focal * up.y, o * forward.z - focal * up.z, -(o * dot(forward, position) - focal * dot(up, position)),
		0, 0, 0, 1,
		forward.x, forward.y, forward.z, -dot(forward, position)};
	return projection;
}
// The cube with an indexed MDL skin (a grey ramp palette), as the Mesh Editor
// holds a Quake model.
ModelMesh mdlCube()
{
	ModelMesh mesh = cube();
	ModelEdit add;
	add.kind = ModelEditKind::AddMdlSkin;
	add.mdlSkin.size = {96, 64};
	add.mdlSkin.pixels = QByteArray(96 * 64, char(0));
	for (int i = 0; i < 256; ++i)
		add.mdlSkin.palette.append(QByteArray(3, char(i)));
	QString error;
	applyModelEdit(&mesh, add, nullptr, &error);
	return mesh;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const ModelMesh mesh = cube();
	QString error;
	ModelSkinPaintStroke stroke;
	stroke.brush.radius = 20;
	stroke.brush.padding = 0;
	stroke.brush.mode = ModelSkinPaintMode::Replace;
	stroke.samples = {{64, 64}};

	// From +X: only the +X island changes; -X behind it is untouched.
	const ModelSkinPaintView front = viewFrom(mesh, 0);
	expect(front.ids.size() == 128 * 128 && front.triangles.size() == 12, "the paint view sees every triangle");
	QImage skin = white();
	ModelSkinPaintResult result;
	if (expect(paintModelSkinStroke(mesh, 0, 0, front, stroke, &skin, &result, &error), "a stroke on the visible face paints"))
	{
		expect(paintedIn(skin, 0) > 50 && result.texels == paintedIn(skin, 0), "the stroke paints the +X island");
		expect(paintedIn(skin, 1) == 0, "the occluded back face (-X) is untouched");
		expect(paintedIn(skin, 2) + paintedIn(skin, 3) + paintedIn(skin, 4) + paintedIn(skin, 5) == 0, "no other island changes");
		expect(skin.pixel(16, 16) == stroke.brush.colour.rgb(), "the centre of the face takes the brush colour");
	}

	// Across the +X / +Y edge, seen from between them: both islands, continuously.
	const ModelSkinPaintView corner = viewFrom(mesh, 45);
	ModelSkinPaintStroke across = stroke;
	across.brush.radius = 8;
	across.samples.clear();
	for (int x = 44; x <= 84; x += 2)
		across.samples.append(QPointF(x, 64));
	QImage seam = white();
	expect(paintModelSkinStroke(mesh, 0, 0, corner, across, &seam, nullptr, &error) && paintedIn(seam, 0) > 20 && paintedIn(seam, 2) > 20 &&
			   paintedIn(seam, 1) == 0 && paintedIn(seam, 3) == 0,
		   "a stroke across the edge paints both islands and nothing behind");

	// Mirror across X, seen from +Y: the dot and its mirror on the +Y island.
	const ModelSkinPaintView side = viewFrom(mesh, 90);
	ModelSkinPaintStroke dot = stroke;
	dot.brush.radius = 5;
	dot.samples = {{64 - 0.5 * 128 * 0.45 / std::sqrt(3.0), 64}};
	QImage plain = white(), mirrored = white();
	expect(paintModelSkinStroke(mesh, 0, 0, side, dot, &plain, nullptr, &error) && plain.pixel(72, 16) != qRgb(255, 255, 255) &&
			   plain.pixel(88, 16) == qRgb(255, 255, 255),
		   "without a mirror only the dot is painted");
	dot.brush.mirrorAxis = 0;
	expect(paintModelSkinStroke(mesh, 0, 0, side, dot, &mirrored, nullptr, &error) && mirrored.pixel(72, 16) != qRgb(255, 255, 255) &&
			   mirrored.pixel(88, 16) != qRgb(255, 255, 255),
		   "the X mirror also paints the dot's mirror image");

	// Indexed skins take exact palette indices.
	QImage indexed(96, 64, QImage::Format_Indexed8);
	QVector<QRgb> table;
	for (int i = 0; i < 256; ++i)
		table << qRgb(i, i, i);
	indexed.setColorTable(table);
	indexed.fill(0);
	ModelSkinPaintStroke index = stroke;
	index.brush.paletteIndex = 7;
	index.brush.mode = ModelSkinPaintMode::Mix;
	if (expect(paintModelSkinStroke(mesh, 0, 0, front, index, &indexed, nullptr, &error) && indexed.format() == QImage::Format_Indexed8,
			   "an indexed skin stays indexed"))
	{
		bool exact = true;
		int sevens = 0;
		for (int y = 0; y < 64; ++y)
			for (int x = 0; x < 96; ++x)
			{
				const int value = indexed.pixelIndex(x, y);
				exact &= value == 0 || value == 7;
				sevens += value == 7 ? 1 : 0;
			}
		expect(exact && sevens > 50, "painting writes palette index 7 and nothing in between");
	}

	// Cancellation leaves the skin as it was.
	QImage kept = white();
	ModelWorkControl cancel;
	cancel.cancelled = [] { return true; };
	expect(!paintModelSkinStroke(mesh, 0, 0, front, stroke, &kept, nullptr, &error, cancel) && kept == white(), "a cancelled stroke changes nothing");
	ModelSkinPaintStroke bad = stroke;
	bad.brush.radius = -1;
	expect(!paintModelSkinStroke(mesh, 0, 0, front, bad, &kept, nullptr, &error), "a negative radius is refused");

	// Strokes files round-trip, and the command replays them deterministically.
	ModelSkinPaintCamera camera;
	camera.yaw = 0;
	camera.pitch = 0;
	camera.size = {128, 128};
	const QJsonObject strokes = modelSkinPaintStrokesJson(camera, {stroke, across});
	ModelSkinPaintCamera parsedCamera;
	QVector<ModelSkinPaintStroke> parsed;
	expect(parseModelSkinPaintStrokes(strokes, &parsedCamera, &parsed, &error) && parsed.size() == 2 && parsed[1].samples.size() == across.samples.size() &&
			   parsedCamera.size == camera.size,
		   "a strokes file round-trips");
	// A perspective view from in front of +X paints the +X island and never
	// the -X face behind it.
	ModelSkinPaintView closeView;
	expect(buildModelSkinPaintView(mesh, 0, perspective({5, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 100, 128), &closeView, &error) &&
			   closeView.projection.perspective && closeView.depthVisible(0.25, 0.25) && !closeView.depthVisible(0.1, 0.25),
		   "a perspective view builds and compares distances");
	QImage throughPerspective = white();
	expect(paintModelSkinStroke(mesh, 0, 0, closeView, stroke, &throughPerspective, nullptr, &error) && paintedIn(throughPerspective, 0) > 50 &&
			   paintedIn(throughPerspective, 1) == 0 && throughPerspective.pixel(16, 16) == stroke.brush.colour.rgb(),
		   "a perspective stroke paints the visible face and not the one behind");
	ModelSkinPaintView behind;
	QImage untouched = white();
	expect(buildModelSkinPaintView(mesh, 0, perspective({5, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 0, 1}, 100, 128), &behind, &error) &&
			   paintModelSkinStroke(mesh, 0, 0, behind, stroke, &untouched, nullptr, &error) && untouched == white(),
		   "nothing behind a perspective camera is painted");

	// A stamp: its left half red and opaque, its right half transparent.
	QImage stamp(8, 8, QImage::Format_ARGB32);
	stamp.fill(Qt::transparent);
	for (int y = 0; y < 8; ++y)
		for (int x = 0; x < 4; ++x)
			stamp.setPixel(x, y, qRgb(255, 0, 0));
	ModelSkinPaintStroke stamped = stroke;
	stamped.brush.colour = Qt::blue;
	stamped.brush.stamp = stamp;
	QImage stampedSkin = white();
	if (expect(paintModelSkinStroke(mesh, 0, 0, front, stamped, &stampedSkin, &result, &error), "a stamp stroke paints"))
	{
		bool onlyRed = true;
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
				onlyRed &= stampedSkin.pixel(x, y) == qRgb(255, 255, 255) || stampedSkin.pixel(x, y) == qRgb(255, 0, 0);
		expect(onlyRed && result.texels > 20 && result.texels < paintedIn(skin, 0) * 3 / 4,
			   "the stamp paints its own colour, and only where it is opaque");
	}

	// Smudge drags the black half of the +X island into its white half.
	QImage halves = white();
	for (int y = 0; y < 32; ++y)
		for (int x = 0; x < 16; ++x)
			halves.setPixel(x, y, qRgb(0, 0, 0));
	ModelSkinPaintStroke smudge;
	smudge.brush.radius = 10;
	smudge.brush.padding = 0;
	smudge.brush.mode = ModelSkinPaintMode::Smudge;
	for (int x = 44; x <= 90; x += 2)
		smudge.samples.append(QPointF(x, 64));
	QImage smudged = halves;
	if (expect(paintModelSkinStroke(mesh, 0, 0, front, smudge, &smudged, &result, &error), "a smudge stroke runs"))
	{
		bool grey = true;
		int darker = 0;
		for (int y = 0; y < 32; ++y)
			for (int x = 16; x < 32; ++x)
			{
				const QRgb texel = smudged.pixel(x, y);
				grey &= qRed(texel) == qGreen(texel) && qGreen(texel) == qBlue(texel);
				darker += qRed(texel) < 255 ? 1 : 0;
			}
		expect(result.texels > 0 && darker > 10 && grey, "smudging carries the dark half along the stroke, as blends of what was there");
		expect(paintedIn(smudged, 1) == 0, "smudging leaves the hidden face alone");
	}
	ModelSkinPaintStroke dab = smudge;
	dab.samples = {{64, 64}};
	QImage still = halves;
	expect(paintModelSkinStroke(mesh, 0, 0, front, dab, &still, &result, &error) && still == halves && result.texels == 0,
		   "a smudge without movement changes nothing");
	QImage indexedHalves(96, 64, QImage::Format_Indexed8);
	indexedHalves.setColorTable(table);
	indexedHalves.fill(255);
	for (int y = 0; y < 32; ++y)
		for (int x = 0; x < 16; ++x)
			indexedHalves.setPixel(x, y, 0);
	QImage indexedSmudge = indexedHalves;
	if (expect(paintModelSkinStroke(mesh, 0, 0, front, smudge, &indexedSmudge, &result, &error) && indexedSmudge.format() == QImage::Format_Indexed8,
			   "an indexed skin smudges"))
	{
		bool known = true;
		for (int y = 0; y < 64; ++y)
			for (int x = 0; x < 96; ++x)
				known &= indexedSmudge.pixelIndex(x, y) == 0 || indexedSmudge.pixelIndex(x, y) == 255;
		expect(known && result.texels > 0, "indexed smudging moves palette indices without inventing new ones");
	}

	// Back into the model: the painted MDL skin replaces its slot as one edit.
	const ModelMesh quake = mdlCube();
	QImage own = modelSurfaceIndexedSkin(quake, 0);
	expect(!own.isNull() && modelSkinPaintMdlSlot(quake, 0) == 0 && modelSkinPaintMdlSlot(mesh, 0) == -1, "the MDL cube has a paintable skin slot");
	ModelSkinPaintStroke red = stroke;
	red.brush.paletteIndex = 200;
	ModelEdit apply;
	ModelMesh applied = quake;
	if (expect(paintModelSkinStroke(quake, 0, 0, front, red, &own, nullptr, &error) && modelSkinPaintMdlEdit(quake, 0, own, &apply, &error) &&
				   apply.kind == ModelEditKind::ReplaceMdlSkinMember && applyModelEdit(&applied, apply, nullptr, &error),
			   "the painted skin applies as one ReplaceMdlSkinMember edit"))
		expect(modelSurfaceIndexedSkin(applied, 0) == own && applied.embeddedSkins.size() == quake.embeddedSkins.size(),
			   "the model's skin is now the painted one");
	QImage recoloured = own;
	QVector<QRgb> other = table;
	other[200] = qRgb(1, 2, 3);
	recoloured.setColorTable(other);
	expect(!modelSkinPaintMdlEdit(quake, 0, recoloured, &apply, &error) && !modelSkinPaintMdlEdit(mesh, 0, own, &apply, &error),
		   "a skin with another palette, or a model without an MDL skin, is refused");

	// Encoding by extension.
	QByteArray encoded;
	expect(encodeModelPaintedSkin(skin, "skin.png", &encoded, &error) && QImage::fromData(encoded, "PNG").convertToFormat(QImage::Format_ARGB32) == skin,
		   "PNG keeps the painted colours");
	if (expect(encodeModelPaintedSkin(own, "skin.pcx", &encoded, &error), "an indexed skin encodes as PCX"))
	{
		const auto decoded = decodeIdTechImage("skin.pcx", encoded, generatedIdTechPalette("quake2"));
		expect(decoded.decoded && decoded.image.format() == QImage::Format_Indexed8 && decoded.image.pixelIndex(16, 16) == 200 &&
				   decoded.image.pixelIndex(60, 50) == 0,
			   "the PCX keeps the painted palette indices");
	}
	if (expect(encodeModelPaintedSkin(skin, "skin.tga", &encoded, &error), "a colour skin encodes as TGA"))
	{
		const auto decoded = decodeIdTechImage("skin.tga", encoded, generatedIdTechPalette("quake"));
		expect(decoded.decoded && decoded.image.convertToFormat(QImage::Format_ARGB32).pixel(16, 16) == skin.pixel(16, 16), "the TGA keeps the colours");
	}
	expect(!encodeModelPaintedSkin(skin, "skin.pcx", &encoded, &error) && !encodeModelPaintedSkin(skin, "skin.wal", &encoded, &error),
		   "PCX needs an indexed skin, and unknown types are refused");

	// Strokes with a stamp, a smudge and a viewport projection round-trip.
	ModelSkinPaintStroke viewStroke = stamped;
	viewStroke.hasProjection = true;
	viewStroke.projection = closeView.projection;
	QVector<ModelSkinPaintStroke> reread;
	expect(parseModelSkinPaintStrokes(modelSkinPaintStrokesJson(camera, {viewStroke, smudge}), nullptr, &reread, &error) && reread.size() == 2 &&
			   reread[0].hasProjection && reread[0].projection == closeView.projection && !reread[0].brush.stamp.isNull() &&
			   reread[1].brush.mode == ModelSkinPaintMode::Smudge,
		   "a viewport stroke keeps its view and stamp, and smudge its mode");

	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (!root.isEmpty() && QDir().mkpath(root))
	{
		// Staging the painted skin back at its package path.
		QTemporaryDir folder(QDir(root).filePath("skin-paint-package-XXXXXX"));
		QDir(folder.path()).mkpath("models/cube");
		QFile original(QDir(folder.path()).filePath("models/cube/skin.png"));
		expect(original.open(QIODevice::WriteOnly) && white().save(&original, "PNG"), "write the package skin");
		original.close();
		PackageArchive archive;
		PackageStagingModel staging;
		QByteArray png;
		if (expect(archive.load(folder.path(), &error) && staging.loadBaseArchive(archive, &error) &&
						   encodeModelPaintedSkin(skin, "models/cube/skin.png", &png, &error) &&
						   stageModelPaintedSkin(png, "models/cube/skin.png", &staging, &error),
				   "the painted skin stages over the package's own"))
		{
			QByteArray stagedBytes;
			bool found = false;
			for (const auto &entry : staging.plannedEntries())
				if (entry.virtualPath == QStringLiteral("models/cube/skin.png"))
					found = staging.entryBytes(entry, &stagedBytes);
			expect(found && stagedBytes == png && staging.operations().size() == 1, "one package step holds the painted skin");
		}
		expect(!stageModelPaintedSkin(png, "../outside.png", &staging, &error), "an unsafe package path is refused");
	}
	if (argc == 2 && !root.isEmpty() && QDir().mkpath(root))
	{
		QTemporaryDir temporary(QDir(root).filePath("skin-paint-XXXXXX"));
		const QString program = QString::fromLocal8Bit(argv[1]);
		const QString source = temporary.filePath("cube.mesh.json"), strokesPath = temporary.filePath("strokes.json"),
					  texture = temporary.filePath("white.png");
		QFile sourceFile(source), strokesFile(strokesPath);
		const QByteArray bytes = serializeEditableModel(mesh, &error);
		expect(sourceFile.open(QIODevice::WriteOnly) && sourceFile.write(bytes) == bytes.size() && strokesFile.open(QIODevice::WriteOnly) &&
				   strokesFile.write(QJsonDocument(strokes).toJson()) > 0 && white().save(texture),
			   "write the cube, the strokes and the texture");
		sourceFile.close();
		strokesFile.close();
		const auto run = [&](const QString &output, QJsonObject *json) {
			QProcess process;
			process.start(program, {"--cli", "model", "paint-skin", source, "--strokes", strokesPath, "--texture", texture, "--output", output, "--json"});
			if (!process.waitForFinished(120000))
				return -1;
			*json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
			return process.exitCode();
		};
		QJsonObject first, second;
		expect(run(temporary.filePath("a.png"), &first) == 0 && run(temporary.filePath("b.png"), &second) == 0 &&
				   first.value("sha256").toString() == second.value("sha256").toString() && first.value("texels").toInt() > 50,
			   "model paint-skin replays strokes deterministically");
		QImage expected = white();
		const ModelSkinPaintView view = viewFrom(mesh, 0);
		paintModelSkinStroke(mesh, 0, 0, view, stroke, &expected, nullptr, &error);
		paintModelSkinStroke(mesh, 0, 0, view, across, &expected, nullptr, &error);
		QImage written(temporary.filePath("a.png"));
		expect(written.convertToFormat(QImage::Format_ARGB32) == expected, "the command paints what the service paints");
		QJsonObject refused;
		expect(run(temporary.filePath("a.png"), &refused) == 1, "an existing output needs --overwrite");

		// --model-output puts the painted MDL skin back into the model.
		const QString quakeSource = temporary.filePath("quake.mesh.json"), quakeOutput = temporary.filePath("quake-painted.mesh.json");
		QFile quakeFile(quakeSource);
		const QByteArray quakeBytes = serializeEditableModel(quake, &error);
		expect(quakeFile.open(QIODevice::WriteOnly) && quakeFile.write(quakeBytes) == quakeBytes.size(), "write the MDL cube");
		quakeFile.close();
		QProcess process;
		process.start(program, {"--cli", "model", "paint-skin", quakeSource, "--strokes", strokesPath, "--output", temporary.filePath("quake.png"),
								   "--model-output", quakeOutput, "--json"});
		QJsonObject painted;
		if (expect(process.waitForFinished(120000) && process.exitCode() == 0, "model paint-skin --model-output runs"))
			painted = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
		QFile writtenModel(quakeOutput);
		ModelMesh reparsed;
		expect(writtenModel.open(QIODevice::ReadOnly) && parseEditableModel(writtenModel.readAll(), &reparsed, &error) && painted.value("skinSlot").toInt() == 0,
			   "the painted model parses");
		QImage expectedIndexed = modelSurfaceIndexedSkin(quake, 0);
		paintModelSkinStroke(quake, 0, 0, view, stroke, &expectedIndexed, nullptr, &error);
		paintModelSkinStroke(quake, 0, 0, view, across, &expectedIndexed, nullptr, &error);
		expect(modelSurfaceIndexedSkin(reparsed, 0) == expectedIndexed, "the written model carries the painted skin");
		QProcess plainModel;
		plainModel.start(program, {"--cli", "model", "paint-skin", source, "--strokes", strokesPath, "--output", temporary.filePath("c.png"),
									  "--model-output", temporary.filePath("cube-painted.mesh.json")});
		expect(plainModel.waitForFinished(120000) && plainModel.exitCode() == 2 && !QFile::exists(temporary.filePath("c.png")),
			   "--model-output on a model without an MDL skin is refused before anything is written");
	}
	std::cout << "model_skin_paint_smoke_test: " << checks << " checks, " << failures << " failure(s)\n";
	return failures == 0 ? 0 : 1;
}
