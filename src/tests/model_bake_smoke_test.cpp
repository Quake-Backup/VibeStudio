// Texture map baking (UVSKIN-06, core/model_bake.h, model bake): ambient
// occlusion of an open plane and an inside corner, tangent-space normals
// projected from a tilted high-poly plane in Doom 3's convention, lighting,
// determinism by seed, cancellation and the memory budget, and the command.
#include "core/model_bake.h"
#include "core/model_document.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
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
struct Corner
{
	ModelVec3 p;
	float u, v;
};
// A quad surface a b c d (counter-clockwise from its front) with one normal.
ModelSurface quad(const QString &name, Corner a, Corner b, Corner c, Corner d, ModelVec3 normal)
{
	ModelSurface surface;
	surface.name = name;
	surface.texCoords = {{a.u, a.v}, {b.u, b.v}, {c.u, c.v}, {d.u, d.v}};
	surface.triangles = {{0, 1, 2}, {0, 2, 3}};
	ModelFrameGeometry pose;
	pose.positions = {a.p, b.p, c.p, d.p};
	pose.normals.fill(normal, 4);
	surface.frames = {pose};
	surface.vertexCount = 4;
	return surface;
}
ModelMesh meshOf(QVector<ModelSurface> surfaces)
{
	ModelMesh mesh;
	mesh.geometryAvailable = true;
	mesh.surfaces = std::move(surfaces);
	mesh.frames = {ModelFrameInfo{0, "frame", {}, {}, {}, 0}};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
// A floor with UVs over the 0-1 square, s along +X and t along -Y (down
// the image).
ModelSurface floorQuad(float z = 0)
{
	return quad("floor", {{0, 10, z}, 0, 0}, {{0, 0, z}, 0, 1}, {{10, 0, z}, 1, 1}, {{10, 10, z}, 1, 0}, {0, 0, 1});
}
ModelMesh corner()
{
	// Two walls standing on the floor along x = 0 and y = 0.
	ModelSurface west = quad("west", {{0, 0, 0}, 0, 0}, {{0, 10, 0}, 0, 0}, {{0, 10, 10}, 0, 0}, {{0, 0, 10}, 0, 0}, {1, 0, 0});
	ModelSurface south = quad("south", {{10, 0, 0}, 0, 0}, {{0, 0, 0}, 0, 0}, {{0, 0, 10}, 0, 0}, {{10, 0, 10}, 0, 0}, {0, 1, 0});
	return meshOf({floorQuad(), west, south});
}
int value(const QImage &image, int x, int y)
{
	return qRed(image.pixel(x, y));
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	ModelBakeOptions options;
	options.size = {32, 32};
	options.samples = 128;
	options.padding = 0;

	// An open plane is unoccluded.
	ModelBakeResult result;
	const ModelMesh plane = meshOf({floorQuad()});
	if (expect(bakeModelMaps(plane, {}, 0, nullptr, options, &result, &error), "an open plane bakes"))
	{
		bool white = true;
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
				white &= value(result.image, x, y) == 255;
		expect(white && result.texels == 32 * 32, "ambient occlusion of an open plane is white everywhere");
	}

	// The inside corner darkens towards the walls, monotonically.
	const ModelMesh room = corner();
	ModelBakeResult ao;
	if (expect(bakeModelMaps(room, {0}, 0, nullptr, options, &ao, &error), "the corner floor bakes"))
	{
		// Floor texel (x, y) sits at world (x + 0.5, 32 - y - 0.5) * 10 / 32:
		// the corner (0, 0) is the bottom-left texel.
		int previous = -1;
		bool monotonic = true;
		for (int step = 0; step < 16; ++step)
		{
			const int current = value(ao.image, step, 31 - step);
			monotonic &= current >= previous - 6;
			previous = std::max(previous, current);
		}
		expect(monotonic && value(ao.image, 0, 31) < 160 && value(ao.image, 28, 3) == 255,
			   "occlusion fades from the corner outwards and is gone far from the walls");
		ModelBakeResult again;
		expect(bakeModelMaps(room, {0}, 0, nullptr, options, &again, &error) && again.image == ao.image, "the same seed bakes the same map");
		ModelBakeOptions reseeded = options;
		reseeded.seed = 99;
		expect(bakeModelMaps(room, {0}, 0, nullptr, reseeded, &again, &error) && again.image != ao.image, "another seed samples other rays");
	}

	// Tangent normals from a high-poly plane tilted towards +X (+s) and +Y (-t).
	ModelBakeOptions normals = options;
	normals.type = ModelBakeType::TangentNormal;
	normals.cageDistance = 2.0;
	const auto tilted = [](float slopeX, float slopeY) {
		ModelVec3 n{-slopeX, -slopeY, 1};
		const float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
		n = {n.x / length, n.y / length, n.z / length};
		const auto height = [&](float x, float y) { return 0.5f + slopeX * (x - 5) + slopeY * (y - 5); };
		return meshOf({quad("high", {{0, 10, height(0, 10)}, 0, 0}, {{0, 0, height(0, 0)}, 0, 0}, {{10, 0, height(10, 0)}, 0, 0},
							{{10, 10, height(10, 10)}, 0, 0}, n)});
	};
	ModelBakeResult bumped;
	const ModelMesh towardX = tilted(0.0f, 0.0f);
	const ModelMesh slopeX = tilted(-0.1f, 0.0f);
	if (expect(bakeModelMaps(plane, {}, 0, &towardX, normals, &bumped, &error), "a flat high-poly bakes"))
	{
		const QRgb c = bumped.image.pixel(16, 16);
		expect(std::abs(qRed(c) - 128) <= 2 && std::abs(qGreen(c) - 128) <= 2 && qBlue(c) >= 253 && bumped.misses == 0,
			   "a high-poly parallel to the low-poly bakes the flat normal (128, 128, 255)");
	}
	if (expect(bakeModelMaps(plane, {}, 0, &slopeX, normals, &bumped, &error), "a tilted high-poly bakes"))
	{
		// Height falling along +X turns the normal towards +X: +s is red.
		const QRgb c = bumped.image.pixel(16, 16);
		expect(qRed(c) > 135 && std::abs(qGreen(c) - 128) <= 2 && qBlue(c) > 200, "a normal leaning along +s decodes as more red");
	}
	const ModelMesh slopeY = tilted(0.0f, -0.1f);
	if (expect(bakeModelMaps(plane, {}, 0, &slopeY, normals, &bumped, &error), "a high-poly tilted along Y bakes"))
	{
		// Leaning towards +Y world is towards -t (up the image): less green.
		const QRgb c = bumped.image.pixel(16, 16);
		expect(qGreen(c) < 121 && std::abs(qRed(c) - 128) <= 2, "a normal leaning up the image decodes as less green, as Doom 3 reads +t down");
	}
	ModelBakeOptions object = normals;
	object.type = ModelBakeType::ObjectNormal;
	expect(bakeModelMaps(plane, {}, 0, nullptr, object, &bumped, &error) && qBlue(bumped.image.pixel(5, 5)) >= 253 &&
			   std::abs(qRed(bumped.image.pixel(5, 5)) - 128) <= 1,
		   "object-space normals of a floor point along +Z");

	// Lighting: a plane facing the light is lit, ambient is the floor.
	ModelBakeOptions lighting = options;
	lighting.type = ModelBakeType::Lighting;
	lighting.lightDirection = {0, 0, 1};
	lighting.ambient = 0.2;
	expect(bakeModelMaps(plane, {}, 0, nullptr, lighting, &result, &error) && value(result.image, 10, 10) == 255, "a floor facing the light bakes full light");
	lighting.lightDirection = {0, 0, -1};
	expect(bakeModelMaps(plane, {}, 0, nullptr, lighting, &result, &error) && std::abs(value(result.image, 10, 10) - 51) <= 1,
		   "a floor facing away keeps the ambient level");

	// Refusals leave the result alone.
	ModelBakeResult untouched;
	ModelWorkControl cancel;
	cancel.cancelled = [] { return true; };
	expect(!bakeModelMaps(room, {0}, 0, nullptr, options, &untouched, &error, cancel) && untouched.image.isNull(), "a cancelled bake publishes nothing");
	ModelBakeOptions tight = options;
	tight.memoryBudget = 1024;
	expect(!bakeModelMaps(room, {0}, 0, nullptr, tight, &untouched, &error) && error.contains(QStringLiteral("MiB")) && untouched.image.isNull(),
		   "a bake over its memory budget is refused");
	expect(!bakeModelMaps(room, {7}, 0, nullptr, options, &untouched, &error), "a missing surface is refused");

	// The command.
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (argc == 2 && !root.isEmpty() && QDir().mkpath(root))
	{
		QTemporaryDir temporary(QDir(root).filePath("model-bake-XXXXXX"));
		const QString program = QString::fromLocal8Bit(argv[1]);
		const QString source = temporary.filePath("corner.mesh.json");
		{
			QFile file(source);
			const QByteArray bytes = serializeEditableModel(room, &error);
			expect(!bytes.isEmpty() && file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write the corner source");
		}
		const auto run = [&](const QStringList &extra, QJsonObject *json) {
			QProcess process;
			process.start(program, QStringList{"--cli", "model", "bake", source, "--json"} + extra);
			if (!process.waitForFinished(180000))
				return -1;
			*json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
			return process.exitCode();
		};
		QJsonObject first, second, dry;
		const QString png = temporary.filePath("ao.png"), tga = temporary.filePath("ao.tga");
		expect(run({"--type", "ao", "--surface", "0", "--size", "32x32", "--samples", "64", "--output", png}, &first) == 0 && QFileInfo::exists(png) &&
				   first.value("texels").toInt() == 1024,
			   "model bake writes an occlusion map");
		expect(run({"--type", "ao", "--surface", "0", "--size", "32x32", "--samples", "64", "--output", tga}, &second) == 0 && QFileInfo::exists(tga) &&
				   second.value("sha256").toString() == first.value("sha256").toString(),
			   "the command is deterministic and writes TGA too");
		expect(run({"--type", "normal", "--dry-run", "--output", temporary.filePath("n.png")}, &dry) == 0 && !QFileInfo::exists(temporary.filePath("n.png")) &&
				   dry.value("dryRun").toBool(),
			   "a dry run bakes without writing");
		expect(run({"--type", "ao", "--output", png}, &dry) == 1, "an existing output needs --overwrite");
		expect(run({"--type", "glossy", "--output", temporary.filePath("x.png")}, &dry) == 2, "an unknown map type is a usage error");
	}
	std::cout << "model_bake_smoke_test: " << checks << " checks, " << failures << " failure(s)\n";
	return failures == 0 ? 0 : 1;
}
