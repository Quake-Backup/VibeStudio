// Painting skins on the model (UVSKIN-09, core/model_skin_paint.h, model
// paint-skin) on a cube whose six faces are separate UV islands: a stroke on
// the visible face changes only its texels and never the occluded back face, a
// stroke across an edge paints both islands, the mirror axis paints the
// mirrored texels, indexed skins take exact palette indices, cancellation
// leaves the skin alone, and the command replays strokes deterministically.
#include "core/model_document.h"
#include "core/model_skin_paint.h"

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
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
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
	}
	std::cout << "model_skin_paint_smoke_test: " << checks << " checks, " << failures << " failure(s)\n";
	return failures == 0 ? 0 : 1;
}
