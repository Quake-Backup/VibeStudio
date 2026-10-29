// Collision placed to match a placed model instance, with a recorded source
// and refresh (ATT-COLL-03).
#include "core/level_brush.h"
#include "core/level_model_instance.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/model_collision_placement.h"
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
// Independent statement of the instance transform: T(origin) Rz(yaw) Ry(pitch) Rx(roll) S.
struct Matrix
{
	double m[3][3];
};
Matrix product(const Matrix &a, const Matrix &b)
{
	Matrix r{};
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			for (int k = 0; k < 3; ++k)
				r.m[i][j] += a.m[i][k] * b.m[k][j];
	return r;
}
LevelMapVec3 analytic(ModelVec3 p, double pitch, double yaw, double roll, const double scale[3], const double origin[3])
{
	const double d = 3.14159265358979323846 / 180;
	const double cy = std::cos(yaw * d), sy = std::sin(yaw * d), cp = std::cos(pitch * d), sp = std::sin(pitch * d),
				 cr = std::cos(roll * d), sr = std::sin(roll * d);
	const Matrix z{{{cy, -sy, 0}, {sy, cy, 0}, {0, 0, 1}}}, y{{{cp, 0, sp}, {0, 1, 0}, {-sp, 0, cp}}},
		x{{{1, 0, 0}, {0, cr, -sr}, {0, sr, cr}}};
	const auto r = product(product(z, y), x);
	const double v[3]{p.x * scale[0], p.y * scale[1], p.z * scale[2]};
	return {r.m[0][0] * v[0] + r.m[0][1] * v[1] + r.m[0][2] * v[2] + origin[0],
			r.m[1][0] * v[0] + r.m[1][1] * v[1] + r.m[1][2] * v[2] + origin[1],
			r.m[2][0] * v[0] + r.m[2][1] * v[1] + r.m[2][2] * v[2] + origin[2], true};
}
const LevelMapEntity *entityOfClass(const LevelMapDocument &map, const QString &className)
{
	for (const auto &entity : map.entities)
		if (entity.className == className)
			return &entity;
	return nullptr;
}
int countClass(const LevelMapDocument &map, const QString &className)
{
	int count = 0;
	for (const auto &entity : map.entities)
		count += entity.className == className ? 1 : 0;
	return count;
}
QVector<LevelMapVec3> groupVertices(const LevelMapDocument &map, int entityId)
{
	QVector<LevelMapVec3> vertices;
	for (const auto &brush : map.brushes)
	{
		LevelBrushTopology topology;
		if (brush.entityId == entityId && levelBrushTopology(brush, &topology))
			vertices += topology.vertices;
	}
	return vertices;
}
bool contains(const QVector<LevelMapVec3> &vertices, const LevelMapVec3 &point, double tolerance)
{
	return std::any_of(vertices.cbegin(), vertices.cend(), [&](const LevelMapVec3 &v) {
		return std::abs(v.x - point.x) <= tolerance && std::abs(v.y - point.y) <= tolerance && std::abs(v.z - point.z) <= tolerance;
	});
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("collision-placement-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	temporary.setAutoRemove(qEnvironmentVariableIsEmpty("VIBESTUDIO_KEEP_TEST_FILES"));
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	bool ok = true;
	QString error;

	auto mesh = tests::assemblyModel();
	ModelCollisionBox box;
	box.name = QStringLiteral("body");
	box.centre = {2, -1, 3};
	box.size = {8, 6, 4};
	box.rotation = {0, 0, 20};
	mesh.collisionBoxes = {box};
	ok &= expect(validateEditableModel(mesh).isEmpty() && write(path("prop.mesh.json"), serializeEditableModel(mesh, &error)),
				 "collision fixture written", error);
	const auto source = path("prop.mesh.json");

	// The shared instance transform: analytic, and the preview's float matrix agrees.
	const QByteArray mapText = "// test\n{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"misc_model\"\n"
							   "\"model\" \"models/props/crate.md3\"\n\"origin\" \"64 32 16\"\n\"angles\" \"10 30 5\"\n"
							   "\"modelscale_vec\" \"2 1 0.5\"\n}\n";
	LevelMapDocument map;
	ok &= expect(write(path("level.map"), mapText) && loadLevelMap({path("level.map"), {}, "idtech3"}, &map, &error), "level loads", error);
	const auto *instance = entityOfClass(map, QStringLiteral("misc_model"));
	ok &= expect(instance && levelEntityPlacesModel(*instance), "misc_model places a model");
	if (!ok)
		return 1;
	const int instanceId = instance->id;
	const double scale[3]{2, 1, .5}, origin[3]{64, 32, 16};
	const auto transform = levelModelInstanceTransform(*instance);
	const auto affine = levelModelInstanceAffine(transform);
	const auto matrix = levelModelInstanceMatrix(transform);
	bool agrees = true;
	for (const ModelVec3 p : {ModelVec3{1, 0, 0}, ModelVec3{0, 1, 0}, ModelVec3{0, 0, 1}, ModelVec3{3, -4, 5}})
	{
		const auto expected = analytic(p, 10, 30, 5, scale, origin);
		const auto exact = applyLevelModelInstanceAffine(affine, p.x, p.y, p.z);
		const auto preview = matrix.map(QVector3D(p.x, p.y, p.z));
		agrees &= std::abs(exact.x - expected.x) < 1e-9 && std::abs(exact.y - expected.y) < 1e-9 && std::abs(exact.z - expected.z) < 1e-9 &&
				  std::abs(preview.x() - expected.x) < 1e-3 && std::abs(preview.y() - expected.y) < 1e-3 &&
				  std::abs(preview.z() - expected.z) < 1e-3;
	}
	ok &= expect(agrees, "instance transform matches the analytic yaw-pitch-roll-scale order, in preview and collision");

	// Placement through the instance: one func_group whose corners follow the transform.
	ModelCollisionExport request{QStringLiteral("quake3"), QStringLiteral("common/clip"), {}, -1};
	request.sourcePath = source;
	ok &= expect(!modelCollisionInstanceFromSelection(map, &request, -1, &error), "no selection, no instance");
	auto worldspawn = entityOfClass(map, QStringLiteral("worldspawn"));
	ok &= expect(worldspawn && !modelCollisionInstanceFromSelection(map, &request, worldspawn->id, &error), "worldspawn is not an instance");
	ok &= expect(setLevelMapSelection(&map, {{LevelMapSelectionKind::Entity, instanceId}}, &error) &&
					 modelCollisionInstanceFromSelection(map, &request, -1, &error) && request.instanceModel == "models/props/crate.md3",
				 "the selected model entity supplies the instance", error);
	const auto placed = prepareModelCollisionPlacement(mesh, request, map);
	ok &= expect(placed.succeeded && countClass(placed.document, "func_group") == 1, "placement creates one recorded func_group", placed.error);
	if (!ok)
		return 1;
	const int groupId = entityOfClass(placed.document, "func_group")->id;
	const auto vertices = groupVertices(placed.document, groupId);
	bool corners = vertices.size() == 8;
	for (const auto corner : modelCollisionCorners(box))
		corners &= contains(vertices, analytic(corner, 10, 30, 5, scale, origin), 1e-4);
	ok &= expect(corners, "rotated, non-uniformly scaled brush corners match the analytic instance transform within 1e-4");
	auto records = levelCollisionRecords(placed.document, &mesh, source);
	ok &= expect(records.size() == 1 && records[0].status == ModelCollisionRecordStatus::Current && records[0].brushCount == 1 &&
					 records[0].frame == -1 && records[0].target == "quake3" && records[0].material == "common/clip" &&
					 records[0].model == "models/props/crate.md3" && records[0].source == "prop.mesh.json" &&
					 records[0].revision == modelCollisionFingerprint(mesh),
				 "the record names a map-relative source, the revision, frame, target and instance model");
	ok &= expect(levelCollisionRecords(placed.document, &mesh, path("other.mesh.json")).value(0).status ==
					 ModelCollisionRecordStatus::OtherSource,
				 "another mesh sees the record as another source");

	// Save and reload keep the record; a box edit makes it stale.
	const auto saved = serializeLevelMap(placed.document);
	LevelMapDocument reloaded;
	ok &= expect(saved.succeeded() && write(path("placed.map"), saved.bytes) && loadLevelMap({path("placed.map"), {}, "idtech3"}, &reloaded, &error) &&
					 levelCollisionRecords(reloaded, &mesh, source).value(0).status == ModelCollisionRecordStatus::Current,
				 "the record survives save and reload", error);
	auto edited = mesh;
	edited.collisionBoxes[0].size = {10, 6, 4};
	edited.collisionBoxes << box;
	edited.collisionBoxes[1].name = QStringLiteral("lid");
	edited.collisionBoxes[1].centre = {2, -1, 9};
	ok &= expect(levelCollisionRecords(reloaded, &edited, source).value(0).status == ModelCollisionRecordStatus::Stale,
				 "a collision edit makes the record stale");
	auto renderOnly = mesh;
	renderOnly.surfaces[0].frames[0].positions[0].x += 1;
	ok &= expect(levelCollisionRecords(reloaded, &renderOnly, source).value(0).status == ModelCollisionRecordStatus::Current,
				 "a render-only edit keeps the record current");

	// Health from disk, as the level checklist and map inspect read it: each
	// record against the source it names.
	auto health = levelCollisionRecordHealth(reloaded);
	ok &= expect(health.records.size() == 1 && health.current == 1 && health.stale == 0 && health.missing == 0 &&
					 modelCollisionRecordHealthJson(health)[0].toObject().value("status").toString() == "current",
				 "from disk the record is current");
	ok &= expect(write(source, serializeEditableModel(edited, &error)), "a collision edit is saved to the source", error);
	health = levelCollisionRecordHealth(reloaded);
	ok &= expect(health.stale == 1 && health.current == 0 && modelCollisionRecordHealthJson(health)[0].toObject().value("status").toString() == "stale" &&
					 modelCollisionRecordHealthText(health).join('\n').contains("1 stale"),
				 "the saved collision edit shows the record as stale");
	ok &= expect(QFile::rename(source, path("moved.mesh.json")), "the source moves away");
	health = levelCollisionRecordHealth(reloaded);
	ok &= expect(health.missing == 1 && health.records.value(0).sourceMissing &&
					 modelCollisionRecordHealthJson(health)[0].toObject().value("status").toString() == "source-missing" &&
					 health.records.value(0).problem.contains("prop.mesh.json"),
				 "a moved source is reported as missing");
	ok &= expect(QFile::remove(path("moved.mesh.json")) && write(source, serializeEditableModel(mesh, &error)), "the source is restored", error);

	// Refresh replaces rather than duplicates, as one undo step.
	const int reloadedGroup = entityOfClass(reloaded, "func_group")->id;
	const auto beforeRefresh = serializeLevelMap(reloaded).bytes;
	auto refreshed = prepareModelCollisionRefresh(edited, source, reloaded, reloadedGroup);
	ok &= expect(refreshed.succeeded && countClass(refreshed.document, "func_group") == 1, "refresh keeps one recorded group",
				 refreshed.error);
	if (refreshed.succeeded)
	{
		const auto &document = refreshed.document;
		const int newGroup = entityOfClass(document, "func_group")->id;
		int brushes = 0;
		for (const auto &brush : document.brushes)
			brushes += brush.entityId == newGroup ? 1 : 0;
		ok &= expect(brushes == 2 && levelCollisionRecords(document, &edited, source).value(0).status == ModelCollisionRecordStatus::Current,
					 "refreshed brushes follow the edited boxes and the record is current again");
		const auto newVertices = groupVertices(document, newGroup);
		bool follows = true;
		for (const auto &edit : edited.collisionBoxes)
			for (const auto corner : modelCollisionCorners(edit))
				follows &= contains(newVertices, analytic(corner, 10, 30, 5, scale, origin), 1e-4);
		ok &= expect(follows, "refresh reuses the recorded instance transform");
		auto undone = document;
		ok &= expect(undoLevelMapEdit(&undone, &error) && serializeLevelMap(undone).bytes == beforeRefresh &&
						 redoLevelMapEdit(&undone, &error) && countClass(undone, "func_group") == 1,
					 "one undo restores the previous brushes and record", error);
	}
	ok &= expect(!prepareModelCollisionRefresh(edited, path("other.mesh.json"), reloaded, reloadedGroup).succeeded,
				 "another mesh cannot refresh this record");
	ok &= expect(!prepareModelCollisionRefresh(edited, source, reloaded, instanceId).succeeded, "a model entity is not a record");

	// Following a moved instance.
	auto moved = reloaded;
	const int movedId = entityOfClass(moved, "misc_model") ? entityOfClass(moved, "misc_model")->id : -1;
	ok &= expect(movedId >= 0 && setLevelMapEntityProperty(&moved, movedId, "angles", "0 90 0", &error) &&
					 setLevelMapEntityProperty(&moved, movedId, "modelscale_vec", "1 1 1", &error),
				 "instance turned and rescaled", error);
	if (movedId >= 0)
	{
		const auto follow = prepareModelCollisionRefresh(mesh, source, moved, reloadedGroup, movedId);
		const double unit[3]{1, 1, 1};
		bool followed = follow.succeeded;
		if (followed)
		{
			const auto followVertices = groupVertices(follow.document, entityOfClass(follow.document, "func_group")->id);
			for (const auto corner : modelCollisionCorners(box))
				followed &= contains(followVertices, analytic(corner, 0, 90, 0, unit, origin), 1e-4);
		}
		ok &= expect(followed, "refresh can follow the instance's current transform", follow.error);
	}

	// Scene: membership kept; a locked layer refuses.
	auto layered = reloaded;
	QString layer;
	QStringList members{levelMapSelectionRefId({LevelMapSelectionKind::Entity, reloadedGroup})};
	for (const auto &brush : layered.brushes)
		if (brush.entityId == reloadedGroup)
			members << levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush, brush.id});
	ok &= expect(createLevelSceneNode(&layered, LevelSceneNodeKind::Layer, "Clip", {}, &layer, &error) &&
					 assignLevelSceneObjects(&layered, layer, members, &error),
				 "record placed in a layer", error);
	const auto kept = prepareModelCollisionRefresh(edited, source, layered, reloadedGroup);
	ok &= expect(kept.succeeded &&
					 levelSceneMembership(kept.document.scene,
										  levelMapSelectionRefId({LevelMapSelectionKind::Entity, entityOfClass(kept.document, "func_group")->id})) ==
						 layer,
				 "refresh keeps the record in its layer", kept.error);
	ok &= expect(setLevelSceneLocked(&layered, layer, true, &error), "layer locked", error);
	const auto lockedBefore = serializeLevelMap(layered).bytes;
	const auto locked = prepareModelCollisionRefresh(edited, source, layered, reloadedGroup);
	ok &= expect(!locked.succeeded && !locked.error.isEmpty() && serializeLevelMap(layered).bytes == lockedBefore,
				 "a locked layer refuses the refresh");

	// Malformed records are reported and never refreshed.
	auto damaged = reloaded;
	ok &= expect(setLevelMapEntityProperty(&damaged, reloadedGroup, QString::fromLatin1(modelCollisionRecordTransform), "1 2 3", &error) &&
					 levelCollisionRecords(damaged, &mesh, source).value(0).status == ModelCollisionRecordStatus::Invalid &&
					 !prepareModelCollisionRefresh(mesh, source, damaged, reloadedGroup).succeeded,
				 "a malformed record is reported and refused", error);

	// Origin-only placement records its translation too.
	ModelCollisionExport plain{QStringLiteral("quake3"), QStringLiteral("common/clip"), {8, 0, 0}, -1};
	plain.sourcePath = source;
	const auto plainPlaced = prepareModelCollisionPlacement(mesh, plain, map);
	ok &= expect(plainPlaced.succeeded && levelCollisionRecords(plainPlaced.document, &mesh, source).value(0).transform[3] == 8,
				 "origin placement records its translation", plainPlaced.error);
	plain.instance = affine;
	ModelCollisionMap refused;
	ok &= expect(!exportModelCollisionMap(mesh, plain, &refused, &error), "an instance cannot be combined with an origin");

	if (argc > 1)
	{
		QJsonObject output;
		const auto run = [&](QStringList args, int code = 0) {
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--settings-file", path("settings.ini")} + args + QStringList{"--json"});
			if (!process.waitForFinished(120000))
			{
				std::cerr << "timeout: " << qPrintable(args.join(' ')) << std::endl;
				process.kill();
				process.waitForFinished();
				return false;
			}
			output = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
			if (process.exitCode() != code)
				std::cerr << qPrintable(QString::fromUtf8(process.readAllStandardError())) << '\n';
			return process.exitStatus() == QProcess::NormalExit && process.exitCode() == code;
		};
		const auto instanceArg = QStringLiteral("entity:%1").arg(instanceId);
		ok &= expect(run({"model", "collision", "prop.mesh.json", "--operation", "place", "--map", "level.map", "--instance", instanceArg,
						  "--target", "quake3", "--material", "common/clip", "--output", "cli-placed.map"}) &&
						 output.value("instance").toString() == instanceArg,
					 "CLI places through an instance");
		ok &= expect(run({"model", "collision", "prop.mesh.json", "--operation", "place", "--map", "level.map", "--instance", instanceArg,
						  "--origin", "1,2,3", "--target", "quake3", "--material", "common/clip", "--output", "bad.map"},
						 2),
					 "CLI refuses --instance with --origin");
		ok &= expect(run({"model", "collision", "prop.mesh.json", "--operation", "placed", "--map", "cli-placed.map"}) &&
						 output.value("placed").toArray().size() == 1 &&
						 output.value("placed").toArray()[0].toObject().value("status").toString() == "current",
					 "CLI lists the record as current");
		ok &= expect(run({"model", "collision", "prop.mesh.json", "--operation", "refresh", "--map", "cli-placed.map", "--output",
						  "cli-refreshed.map"},
						 4),
					 "CLI refresh with nothing stale fails clearly");
		ok &= expect(run({"model", "collision", "prop.mesh.json", "--operation", "update", "--box", "body", "--size", "10,6,4", "--output",
						  "prop.mesh.json"}),
					 "CLI edits the source's box");
		ok &= expect(run({"model", "collision", "prop.mesh.json", "--operation", "placed", "--map", "cli-placed.map"}) &&
						 output.value("placed").toArray()[0].toObject().value("status").toString() == "stale",
					 "CLI sees the record as stale after the edit");
		ok &= expect(run({"map", "inspect", "cli-placed.map", "--engine", "idtech3"}) &&
						 output.value("collisionRecords").toArray().size() == 1 &&
						 output.value("collisionRecords").toArray()[0].toObject().value("status").toString() == "stale" &&
						 run({"map", "inspect", "level.map", "--engine", "idtech3"}) && !output.contains("collisionRecords"),
					 "map inspect reports placed collision against its recorded source, only when there is some");
		const auto beforeCli = read(path("cli-placed.map"));
		ok &= expect(run({"model", "collision", "prop.mesh.json", "--operation", "refresh", "--map", "cli-placed.map", "--output",
						  "cli-refreshed.map", "--dry-run"}) &&
						 !QFileInfo::exists(path("cli-refreshed.map")),
					 "CLI refresh dry run writes nothing");
		ok &= expect(run({"model", "collision", "prop.mesh.json", "--operation", "refresh", "--map", "cli-placed.map", "--output",
						  "cli-refreshed.map"}) &&
						 output.value("refreshed").toInt() == 1 && read(path("cli-placed.map")) == beforeCli,
					 "CLI refreshes the stale record into a separate map");
		ok &= expect(run({"model", "collision", "prop.mesh.json", "--operation", "placed", "--map", "cli-refreshed.map"}) &&
						 output.value("placed").toArray().size() == 1 &&
						 output.value("placed").toArray()[0].toObject().value("status").toString() == "current",
					 "CLI refreshed map holds one current record");
		ok &= expect(run({"model", "collision", "prop.mesh.json", "--operation", "refresh", "--map", "cli-placed.map", "--group", "entity:0",
						  "--output", "cli-placed.map", "--overwrite"},
						 4) &&
						 read(path("cli-placed.map")) == beforeCli,
					 "CLI protects the input map and refuses a non-record group");
	}
	std::cout << checks << " collision placement checks\n";
	return ok ? 0 : 1;
}
