// Convex hull collision volumes with per-target budgets (ATT-COLL-01).
#include "core/level_brush.h"
#include "core/model_collision.h"
#include "core/model_collision_hull.h"
#include "core/model_document.h"
#include "tests/model_assembly_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace
{
int checks = 0;
bool expect(bool value, const char *message, const QString &detail = {})
{
	++checks;
	if (!value)
		std::cerr << "FAIL: " << message << ' ' << qPrintable(detail) << '\n';
	return value;
}
bool write(const QString &path, const QByteArray &data)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
// Fibonacci points on an ellipsoid plus interior points: deterministic.
QVector<ModelVec3> cloud(int count)
{
	QVector<ModelVec3> points;
	const double golden = 3.14159265358979323846 * (3 - std::sqrt(5.0));
	for (int i = 0; i < count; ++i)
	{
		const double y = 1 - 2 * (i + .5) / count, r = std::sqrt(1 - y * y), angle = golden * i;
		points << ModelVec3{float(20 * r * std::cos(angle)), float(12 * y), float(15 * r * std::sin(angle))};
		points << ModelVec3{float(5 * r * std::cos(angle)), float(3 * y), float(4 * r * std::sin(angle))};
	}
	return points;
}
bool contains(const ModelCollisionHullShape &shape, const QVector<ModelVec3> &points, double tolerance)
{
	for (const auto &plane : shape.planes)
		for (const auto &p : points)
			if (plane[0] * p.x + plane[1] * p.y + plane[2] * p.z - plane[3] > tolerance)
				return false;
	return true;
}
bool sameHull(const QVector<ModelVec3> &a, const QVector<ModelVec3> &b)
{
	return std::equal(a.cbegin(), a.cend(), b.cbegin(), b.cend(),
					  [](ModelVec3 x, ModelVec3 y) { return x.x == y.x && x.y == y.y && x.z == y.z; });
}
bool closedConvex(const ModelCollisionHullShape &shape)
{
	return int(shape.vertices.size()) - int(shape.edges.size()) + int(shape.faces.size()) == 2;
}
ModelMesh octahedronModel()
{
	auto mesh = tests::assemblyModel();
	auto &surface = mesh.surfaces[0];
	surface.vertexCount = 6;
	surface.triangles = {{0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4}, {2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5}};
	surface.texCoords = {{0, 0}, {1, 0}, {0, 1}, {1, 1}, {.5, .5}, {.5, 0}};
	for (int f = 0; f < surface.frames.size(); ++f)
	{
		auto &frame = surface.frames[f];
		frame.positions = {{10, 0, 0}, {-10, 0, 0}, {0, 12, 0}, {0, -12, 0}, {0, 0, 8}, {0, 0, -8}};
		frame.positions[0].x += 4 * f; // the second frame stretches +X
		frame.normals.clear();
		for (const auto &p : frame.positions)
		{
			const double l = std::sqrt(double(p.x) * p.x + double(p.y) * p.y + double(p.z) * p.z);
			frame.normals << ModelVec3{float(p.x / l), float(p.y / l), float(p.z / l)};
		}
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("collision-hull-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	bool ok = true;
	QString error;

	// Exact hull: closed, convex, containing every point.
	const auto points = cloud(150);
	ModelCollisionHullShape exact;
	ok &= expect(modelCollisionHullShape(points, &exact, &error) && closedConvex(exact) && contains(exact, points, 1e-3) &&
					 exact.volume > 0 && exact.volume < 4.0 / 3 * 3.14159265358979323846 * 20 * 12 * 15,
				 "the exact hull is closed (Euler 2), convex and contains every point", error);
	ok &= expect(exact.faces.size() > 128, "the fixture's exact hull exceeds the map brush budget");

	// Budgets: the hull only grows, every point stays inside, the budget holds.
	struct Case
	{
		const char *profile;
		int maxFaces, faces, edges, vertices;
	};
	for (const auto &item : {Case{"quake3", 0, 128, 0, 0}, Case{"doom3-trace", 0, 16, 32, 32}, Case{"quake", 8, 8, 0, 0}})
	{
		ModelCollisionHullBudget budget;
		ModelCollisionHullFit fit, again;
		ok &= expect(modelCollisionHullBudget(item.profile, item.maxFaces, &budget, &error) &&
						 fitModelCollisionHull(points, budget, &fit, &error) && fitModelCollisionHull(points, budget, &again, &error),
					 "the hull fits", error);
		ModelCollisionHullShape shape;
		ok &= expect(modelCollisionHullShape(fit.vertices, &shape, &error) && closedConvex(shape) && contains(shape, points, 1e-3),
					 "the simplified hull is closed, convex and still contains every source point", error);
		ok &= expect(shape.faces.size() <= item.faces && fit.faces <= item.faces && (item.edges == 0 || shape.edges.size() <= item.edges) &&
						 (item.vertices == 0 || shape.vertices.size() <= item.vertices),
					 "the profile budget is respected");
		ok &= expect(fit.simplified && fit.volume >= fit.exactVolume - 1e-6 && std::abs(fit.exactVolume - exact.volume) < 1e-6,
					 "simplification is conservative and reports the exact volume");
		bool same = fit.vertices.size() == again.vertices.size();
		for (int i = 0; same && i < fit.vertices.size(); ++i)
			same = fit.vertices[i].x == again.vertices[i].x && fit.vertices[i].y == again.vertices[i].y &&
				   fit.vertices[i].z == again.vertices[i].z;
		ok &= expect(same, "fitting is deterministic");
	}
	ModelCollisionHullBudget budget;
	ok &= expect(!modelCollisionHullBudget("doom3-trace", 17, &budget, &error) && !modelCollisionHullBudget("quake3", 3, &budget, &error) &&
					 !modelCollisionHullBudget("hexen", 0, &budget, &error),
				 "budgets outside the profile are refused");
	ModelCollisionHullFit refused;
	ok &= expect(!fitModelCollisionHull({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {2, 3, 0}}, budget, &refused, &error) &&
					 !fitModelCollisionHull({{0, 0, 0}, {1, 1, 1}, {2, 2, 2}, {3, 3, 3}}, budget, &refused, &error) &&
					 !fitModelCollisionHull({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, budget, &refused, &error),
				 "coplanar, collinear and too few points are refused");
	ModelWorkControl cancel;
	cancel.cancelled = [] { return true; };
	modelCollisionHullBudget("quake3", 0, &budget);
	ok &= expect(!fitModelCollisionHull(points, budget, &refused, &error, cancel), "fitting can be cancelled");

	// Document: fit, undo, source round trip.
	ModelDocument document;
	ok &= expect(document.setMesh(octahedronModel(), &error), "octahedron fixture adopted", error);
	ModelEdit edit;
	edit.kind = ModelEditKind::FitCollisionHull;
	edit.collisionBox.name = QStringLiteral("shell");
	edit.collisionHullProfile = QStringLiteral("doom3-trace");
	edit.selection = document.selection();
	ok &= expect(document.edit(edit, &error), "Fit New Hull over all frames", error);
	const auto *hull = findModelCollisionBox(document.mesh(), "shell");
	ok &= expect(hull && isModelCollisionHull(*hull) && hull->hull.size() == 6 && hull->size.x == 24 && hull->centre.x == 2,
				 "the hull spans the union of frames and keeps the octahedron's six corners");
	if (!ok)
		return 1;
	const auto withHull = editableModelJson(document.mesh());
	ModelMesh parsed;
	ok &= expect(parseEditableModel(QJsonDocument(withHull).toJson(), &parsed, &error) && parsed.collisionBoxes.size() == 1 &&
					 sameHull(parsed.collisionBoxes[0].hull, hull->hull),
				 "the source round trips the hull exactly", error);
	auto broken = withHull;
	auto volumes = broken.value("collisionBoxes").toArray();
	auto volume = volumes[0].toObject();
	volume.insert("hull", QJsonArray{QJsonArray{0, 0, 0}, QJsonArray{.4, 0, 0}, QJsonArray{0, .4, 0}});
	volumes[0] = volume;
	broken.insert("collisionBoxes", volumes);
	ok &= expect(!parseEditableModel(QJsonDocument(broken).toJson(), &parsed, &error), "a hull with too few points is refused");
	volume.insert("hull", QJsonArray{QJsonArray{0, 0, 0}, QJsonArray{.4, 0, 0}, QJsonArray{0, .4, 0}, QJsonArray{.4, .4, 0}});
	volumes[0] = volume;
	broken.insert("collisionBoxes", volumes);
	ok &= expect(!parseEditableModel(QJsonDocument(broken).toJson(), &parsed, &error), "a flat hull is refused");
	ok &= expect(document.undo() && document.mesh().collisionBoxes.isEmpty() && document.redo() &&
					 isModelCollisionHull(document.mesh().collisionBoxes.value(0)),
				 "undo and redo restore the hull");

	// One frame only.
	edit.collisionBox.name = QStringLiteral("rest");
	edit.frame = 0;
	ok &= expect(document.edit(edit, &error) && findModelCollisionBox(document.mesh(), "rest")->size.x == 20,
				 "a single-frame fit uses that frame", error);

	// Transform with local non-uniform scale keeps the shape in the frame.
	ModelEdit scale;
	scale.kind = ModelEditKind::TransformCollisionBox;
	scale.selection = document.selection();
	scale.selection.collision = QStringLiteral("rest");
	scale.scale = {2, 1, .5};
	scale.pivotMode = ModelTransformPivot::SelectionCentre;
	ok &= expect(document.edit(scale, &error), "hull volume scales on its local axes", error);
	const auto *scaled = findModelCollisionBox(document.mesh(), "rest");
	const auto world = modelCollisionPoints(*scaled);
	bool expected = scaled && scaled->size.x == 40 && scaled->size.z == 8;
	for (const ModelVec3 corner : {ModelVec3{20, 0, 0}, ModelVec3{-20, 0, 0}, ModelVec3{0, 12, 0}, ModelVec3{0, 0, 4}})
		expected &= std::any_of(world.cbegin(), world.cend(), [&](const ModelVec3 &p) {
			return std::abs(p.x - corner.x) < 1e-3 && std::abs(p.y - corner.y) < 1e-3 && std::abs(p.z - corner.z) < 1e-3;
		});
	ok &= expect(expected, "world hull points follow the scaled frame");
	const auto scaledHull = scaled ? scaled->hull : QVector<ModelVec3>();

	// Animate and freeze keep the shape.
	ModelEdit animate;
	animate.kind = ModelEditKind::AnimateCollisionBox;
	animate.selection = document.selection();
	animate.selection.collision = QStringLiteral("rest");
	ok &= expect(document.edit(animate, &error) && findModelCollisionBox(document.mesh(), "rest")->framePoses.size() == 2 &&
					 sameHull(findModelCollisionBox(document.mesh(), "rest")->hull, scaledHull),
				 "an animated hull keeps its shape", error);
	ModelCollisionBox sampled;
	ok &= expect(sampleModelCollisionBox(*findModelCollisionBox(document.mesh(), "rest"), 0, 1, .5, &sampled) && isModelCollisionHull(sampled),
				 "sampled poses keep the hull");
	ok &= expect(parseEditableModel(QJsonDocument(editableModelJson(document.mesh())).toJson(), &parsed, &error) &&
					 sameHull(parsed.collisionBoxes.value(1).hull, findModelCollisionBox(document.mesh(), "rest")->hull) &&
					 !parsed.collisionBoxes.value(1).framePoses.isEmpty(),
				 "an animated hull round trips", error);

	// Map export: one closed convex brush per hull, inside the budget.
	ModelCollisionExport request{QStringLiteral("quake3"), QStringLiteral("common/clip"), {}, 0};
	ModelCollisionMap map;
	ok &= expect(exportModelCollisionMap(document.mesh(), request, &map, &error) && map.brushCount == 2, "hulls export as brushes", error);
	LevelMapDocument level;
	ok &= expect(loadLevelMapBytes({path("hull.map"), {}, "idtech3"}, map.bytes, &level, &error) && level.brushes.size() == 2,
				 "the map parser reads the hull brushes", error);
	for (const auto &brush : level.brushes)
	{
		LevelBrushTopology topology;
		ok &= expect(levelBrushTopology(brush, &topology, &error) &&
						 int(topology.vertices.size()) - int(topology.edges.size()) + int(topology.faces.size()) == 2 &&
						 topology.faces.size() <= 16,
					 "each hull brush is closed (Euler 2) and within the trace budget", error);
	}

	if (argc > 1)
	{
		ok &= expect(write(path("prop.mesh.json"), serializeEditableModel(octahedronModel(), &error)), "CLI fixture written", error);
		QJsonObject output;
		const auto run = [&](QStringList args, int code = 0) {
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--settings-file", path("settings.ini")} + args + QStringList{"--json"});
			if (!process.waitForFinished(60000))
			{
				process.kill();
				process.waitForFinished();
				return false;
			}
			output = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
			if (process.exitCode() != code)
				std::cerr << qPrintable(QString::fromUtf8(process.readAllStandardError())) << '\n';
			return process.exitStatus() == QProcess::NormalExit && process.exitCode() == code;
		};
		ok &= expect(run({"model", "collision", "prop.mesh.json", "--operation", "fit-hull", "--name", "shell", "--profile", "doom3-trace",
						  "--max-faces", "6", "--frame", "0", "--output", "hull.mesh.json"}) &&
						 output.value("hull").toObject().value("faces").toInt() <= 6 &&
						 output.value("hull").toObject().value("profile").toString() == "doom3-trace",
					 "CLI fits a hull within the face budget");
		ok &= expect(run({"model", "collision", "prop.mesh.json", "--operation", "fit-hull", "--name", "shell", "--profile", "hexen",
						  "--output", "bad.mesh.json"},
						 2) &&
						 run({"model", "collision", "prop.mesh.json", "--operation", "fit-hull", "--name", "shell", "--max-faces", "200",
							  "--output", "bad.mesh.json"},
							 2) &&
						 !QFileInfo::exists(path("bad.mesh.json")),
					 "CLI refuses unknown profiles and impossible budgets");
		ok &= expect(run({"model", "collision", "hull.mesh.json", "--operation", "transform", "--box", "shell", "--scale", "1,2,1", "--output",
						  "hull2.mesh.json"}) &&
						 run({"model", "collision", "hull2.mesh.json", "--operation", "export-map", "--target", "quake2", "--output",
							  "hull.map"}) &&
						 output.value("brushes").toInt() == 1,
					 "CLI transforms and exports a hull with the box operations");
		ModelMesh written;
		ok &= expect(parseEditableModel(read(path("hull2.mesh.json")), &written, &error) && isModelCollisionHull(written.collisionBoxes.value(0)),
					 "the CLI source keeps the hull", error);
	}
	std::cout << checks << " collision hull checks\n";
	return ok ? 0 : 1;
}
