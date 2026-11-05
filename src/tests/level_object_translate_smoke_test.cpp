// The Translate tool's core service and CLI route (core/level_object_manipulator.h,
// map move with --to, --pivot, --snap, --context or several --object):
// NetRadiant Custom's grid and bounds snapping, dominant-axis moves and pivot
// snapping; one undo step; refusal of hidden, locked and out-of-group
// selections without touching the document; and the CLI writing exactly what
// the core writes.
#include "core/level_document.h"
#include "core/level_map.h"
#include "core/level_object_manipulator.h"
#include "core/level_patch.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << label << ": " << detail.toUtf8().constData() << std::endl;
	}
	return value;
}
bool at(const LevelMapVec3& v, double x, double y, double z)
{
	return v.valid && std::abs(v.x - x) < 1e-9 && std::abs(v.y - y) < 1e-9 && std::abs(v.z - z) < 1e-9;
}
QString text(const LevelMapVec3& v) { return QStringLiteral("%1 %2 %3 %4").arg(v.x).arg(v.y).arg(v.z).arg(v.valid); }
LevelMapSelectionRef brush(int id) { return {LevelMapSelectionKind::QuakeBrush, id}; }

// Brushes 0 (0..24 cubed) and 1 (32..40 by 0..16 by 0..16), and an entity.
LevelMapDocument fixture()
{
	LevelMapDocument map;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	QString error;
	expect(createLevelMap(create, &map, &error) && addLevelMapBoxBrush(&map, {0, 0, 0, true}, {24, 24, 24, true}, "studio/checker", nullptr, &error)
			&& addLevelMapBoxBrush(&map, {32, 0, 0, true}, {40, 16, 16, true}, "studio/checker", nullptr, &error)
			&& addLevelMapEntity(&map, QStringLiteral("info_player_start"), {100, 100, 8, true}, {}, nullptr, &error),
		"fixture", error);
	setLevelMapSelection(&map, {});
	map.undoStack.clear();
	return map;
}

void tools()
{
	for (const auto tool : {LevelObjectTool::Select, LevelObjectTool::Translate, LevelObjectTool::Rotate, LevelObjectTool::Scale, LevelObjectTool::Transform}) {
		expect(levelObjectToolFromId(levelObjectToolId(tool)) == tool, "tool identifiers round trip", levelObjectToolId(tool));
	}
	expect(levelObjectToolFromId(QStringLiteral("unknown")) == LevelObjectTool::Select && levelObjectToolFromId(QStringLiteral("TRANSLATE")) == LevelObjectTool::Translate,
		"unknown identifiers select, case is ignored");
}

void snapping()
{
	const LevelMapVec3 mins {0, 0, 0, true}, maxs {24, 24, 24, true};
	// Grid: the delta itself lands on the grid.
	expect(at(snapLevelObjectTranslation({5, -9, 23, true}, mins, maxs, 16, LevelObjectTranslateSnap::Grid), 0, -16, 16), "grid snapping rounds the delta");
	expect(at(snapLevelObjectTranslation({5, 0, 0, true}, mins, maxs, 0, LevelObjectTranslateSnap::Grid), 5, 0, 0), "no grid leaves the delta");
	expect(at(snapLevelObjectTranslation({5, 0, 0, true}, mins, maxs, 16, LevelObjectTranslateSnap::None), 5, 0, 0), "no snapping leaves the delta");
	// Bounds: the nearer face lands on the grid (NRC aabb_snap_translation).
	auto snapped = snapLevelObjectTranslation({5, 0, 0, true}, mins, maxs, 16, LevelObjectTranslateSnap::Bounds);
	expect(at(snapped, 8, 0, 0), "bounds snapping lands the maximum face", text(snapped));
	snapped = snapLevelObjectTranslation({12, 0, -3, true}, mins, maxs, 16, LevelObjectTranslateSnap::Bounds);
	expect(at(snapped, 16, 0, 0), "a tie lands the minimum face; an unmoved axis stays", text(snapped));
	snapped = snapLevelObjectTranslation({0.005, 0, 0, true}, mins, maxs, 16, LevelObjectTranslateSnap::Bounds);
	expect(at(snapped, 0.005, 0, 0), "an axis within 0.01 is left alone", text(snapped));
	expect(at(snapLevelObjectTranslation({5, 0, 0, true}, {}, {}, 16, LevelObjectTranslateSnap::Bounds), 5, 0, 0), "invalid bounds leave the delta");
	// Shift's dominant axis.
	expect(at(levelObjectDominantAxis({3, -7, 2, true}), 0, -7, 0) && at(levelObjectDominantAxis({5, 5, 0, true}), 5, 0, 0), "dominant axis, X first on a tie");
	// The pivot: nearest of centre, maximum, minimum and grid, per moved axis.
	const LevelMapVec3 low {0, 0, 0, true}, high {64, 32, 16, true}, start {32, 16, 8, true};
	auto pivot = snapLevelObjectPivot({61, 3, 30, true}, start, 0b011, low, high, 16);
	expect(at(pivot, 64, 0, 8), "pivot lands on the bounds or the grid on moved axes only", text(pivot));
	pivot = snapLevelObjectPivot({37, 15, 8, true}, start, 0b011, low, high, 0);
	expect(at(pivot, 32, 16, 8), "without a grid only the bounds attract the pivot", text(pivot));
	pivot = snapLevelObjectPivot({45, 16, 8, true}, start, 0b001, low, high, 16);
	expect(at(pivot, 48, 16, 8), "a grid line can win over the bounds", text(pivot));
	expect(at(snapLevelObjectPivot({std::nan(""), 0, 0, true}, start, 7, low, high, 16), 32, 16, 8), "non-finite input keeps the start");
}

void targets()
{
	auto map = fixture();
	auto target = levelObjectManipulatorTarget(map);
	expect(!target.available && !target.reason.isEmpty(), "nothing selected is not a target");
	setLevelMapSelection(&map, {brush(0), brush(1)});
	target = levelObjectManipulatorTarget(map);
	expect(target.available && target.objects.size() == 2 && at(target.mins, 0, 0, 0) && at(target.maxs, 40, 24, 24) && at(target.centre, 20, 12, 12),
		"the selection's bounds and centre", text(target.centre));
	expect(at(levelObjectManipulatorPivot(target, {}), 20, 12, 12) && at(levelObjectManipulatorPivot(target, {1, 2, 3, true}), 1, 2, 3),
		"the pivot follows the centre unless placed");
	map.hiddenObjects = {QStringLiteral("brush:1")};
	target = levelObjectManipulatorTarget(map);
	expect(!target.available && target.hidden == 1 && !target.reason.isEmpty(), "a hidden selected object refuses the target", target.reason);
	map.hiddenObjects.clear();
	QString group, error;
	expect(createLevelSceneNode(&map, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &group, &error)
			&& assignLevelSceneObjects(&map, group, {QStringLiteral("brush:1")}, &error) && setLevelSceneLocked(&map, group, true, &error),
		"lock fixture", error);
	setLevelMapSelection(&map, {brush(0), brush(1)});
	target = levelObjectManipulatorTarget(map);
	expect(!target.available && target.locked == 1, "a locked selected object refuses the target", target.reason);
	// An open group: objects outside it cannot be manipulated.
	map = fixture();
	expect(createLevelSceneNode(&map, LevelSceneNodeKind::Group, QStringLiteral("Room"), {}, &group, &error)
			&& assignLevelSceneObjects(&map, group, {QStringLiteral("brush:0")}, &error) && openLevelSceneGroup(&map, group, &error),
		"group fixture", error);
	map.selection = {brush(0), brush(1)};
	target = levelObjectManipulatorTarget(map);
	expect(!target.available && target.outsideGroup == 1, "an object outside the open group refuses the target", target.reason);
	// Doom maps have no object manipulators.
	LevelMapDocument doom;
	LevelMapCreateRequest doomRequest;
	doomRequest.game = QStringLiteral("doom");
	expect(createLevelMap(doomRequest, &doom, &error), "Doom fixture", error);
	expect(!levelObjectManipulatorTarget(doom).available, "Doom maps are refused");
}

void translate()
{
	auto map = fixture();
	setLevelMapSelection(&map, {brush(0), brush(1)});
	const auto before = serializeLevelMap(map).bytes;
	const auto depth = map.undoStack.size();
	LevelObjectTranslateRequest request;
	request.delta = {5, 0, 0, true};
	request.grid = 16;
	request.snap = LevelObjectTranslateSnap::Bounds;
	LevelObjectTranslateReport report;
	QString error;
	expect(translateLevelMapObjects(&map, request, &report, &error), "bounds-snapped translate", error);
	// The union's X runs 0 to 40: moved by 5, its maximum (45) is 3 from the
	// grid line at 48 and its minimum (5) 5 from 0, so the maximum lands: 8.
	expect(at(report.delta, 8, 0, 0) && report.objects == 2 && at(report.pivotBefore, 20, 12, 12) && at(report.pivotAfter, 28, 12, 12)
			&& at(report.mins, 8, 0, 0) && at(report.maxs, 48, 24, 24),
		"report: snapped delta, pivot and bounds", text(report.delta));
	expect(map.undoStack.size() == depth + 1, "one undo step");
	expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == before, "undo restores the map exactly", error);
	expect(redoLevelMapEdit(&map, &error), "redo", error);

	// A grid-snapped move that rounds to zero is refused untouched.
	map = fixture();
	setLevelMapSelection(&map, {brush(0)});
	const auto still = serializeLevelMap(map).bytes;
	request = {};
	request.delta = {3, 2, 0, true};
	request.grid = 16;
	request.snap = LevelObjectTranslateSnap::Grid;
	expect(!translateLevelMapObjects(&map, request, &report, &error) && !error.isEmpty() && serializeLevelMap(map).bytes == still && map.undoStack.isEmpty(),
		"a zero move is refused without a history step", error);

	// --to: the pivot (the bounds' centre, or a placed one) lands there.
	request = {};
	request.destination = {100, 0, 12, true};
	expect(translateLevelMapObjects(&map, request, &report, &error) && at(report.delta, 88, -12, 0) && at(report.pivotAfter, 100, 0, 12),
		"destination moves the centre there", text(report.delta));
	request.destination = {0, 0, 0, true};
	request.pivot = {100, 0, 0, true};
	expect(translateLevelMapObjects(&map, request, &report, &error) && at(report.delta, -100, 0, 0) && at(report.pivotBefore, 100, 0, 0),
		"destination moves a placed pivot there", text(report.delta));

	// Refusals leave the document and its history untouched.
	map = fixture();
	setLevelMapSelection(&map, {brush(0), brush(1)});
	map.hiddenObjects = {QStringLiteral("brush:0")};
	const auto hidden = serializeLevelMap(map).bytes;
	request = {};
	request.delta = {16, 0, 0, true};
	expect(!translateLevelMapObjects(&map, request, &report, &error) && serializeLevelMap(map).bytes == hidden && map.undoStack.isEmpty(),
		"hidden selection refused", error);
	request.grid = -1;
	expect(!translateLevelMapObjects(&map, request, &report, &error), "a negative grid is refused");

	// Texture lock: the same move with and without it differs only in UVs.
	map = fixture();
	setLevelMapSelection(&map, {brush(0)});
	auto unlocked = map;
	request = {};
	request.delta = {8, 0, 0, true};
	request.textures.enabled = true;
	expect(translateLevelMapObjects(&map, request, &report, &error), "locked move", error);
	request.textures.enabled = false;
	expect(translateLevelMapObjects(&unlocked, request, &report, &error), "unlocked move", error);
	expect(map.brushes[0].faces.size() == unlocked.brushes[0].faces.size() && serializeLevelMap(map).bytes != serializeLevelMap(unlocked).bytes,
		"texture lock changes the moved faces' texture parameters");
}

// A patch and a point entity move with a brush, by the core and by the CLI.
void mixed(const QString& executable)
{
	auto map = fixture();
	QString error;
	LevelMapPatch patch;
	LevelPatchCreateRequest patchRequest;
	patchRequest.center = {200, 0, 8, true};
	patchRequest.size = {32, 32, 32, true};
	int patchId = -1;
	expect(createLevelPatch(patchRequest, &patch, &error) && addLevelMapPatch(&map, patch, &patchId, &error), "patch fixture", error);
	map.undoStack.clear();
	const LevelMapSelectionRef patchRef {LevelMapSelectionKind::QuakePatch, patchId};
	const auto entityAt = [](const LevelMapDocument& document) {
		for (const auto& entity : document.entities) {
			if (entity.className == QLatin1String("info_player_start")) { return entity.origin; }
		}
		return LevelMapVec3 {};
	};
	const auto patchPoints = [patchId](const LevelMapDocument& document) {
		for (const auto& candidate : document.patches) {
			if (candidate.id == patchId) { return candidate.controlPoints; }
		}
		return QVector<LevelMapVec3> {};
	};
	int entityId = -1;
	for (const auto& entity : map.entities) {
		if (entity.className == QLatin1String("info_player_start")) { entityId = entity.id; }
	}
	setLevelMapSelection(&map, {brush(0), patchRef, {LevelMapSelectionKind::Entity, entityId}});
	const auto before = serializeLevelMap(map).bytes;
	const auto points = patchPoints(map);
	const auto origin = entityAt(map);
	auto unchanged = map;
	LevelObjectTranslateRequest request;
	request.delta = {20, 0, 0, true};
	request.grid = 16;
	request.snap = LevelObjectTranslateSnap::Grid;
	LevelObjectTranslateReport report;
	expect(translateLevelMapObjects(&map, request, &report, &error) && report.objects == 3 && at(report.delta, 16, 0, 0), "brush, patch and entity move", error);
	const auto moved = patchPoints(map);
	bool patchMoved = moved.size() == points.size() && !points.isEmpty();
	for (qsizetype i = 0; patchMoved && i < moved.size(); ++i) { patchMoved = at(moved[i], points[i].x + 16, points[i].y, points[i].z); }
	expect(patchMoved, "every patch control point moves by the snapped delta");
	expect(at(entityAt(map), origin.x + 16, origin.y, origin.z), "the entity's origin moves", text(entityAt(map)));
	expect(map.undoStack.size() == 1 && undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == before, "one undo step restores all three", error);
	if (executable.isEmpty()) { return; }
	// The CLI writes what the core writes for the same mixed selection.
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "mixed CLI folder")) { return; }
	const QString path = temp.filePath("mixed.map");
	QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(serializeLevelMap(unchanged).bytes) > 0, "mixed CLI fixture")) { return; }
	file.close();
	QProcess process;
	process.setWorkingDirectory(temp.path());
	process.start(executable, {"--cli", "--json", "--settings-file", temp.filePath("settings.ini"), "map", "move", path, "--object", "brush:0", "--object",
		QStringLiteral("patch:%1").arg(patchId), "--object", QStringLiteral("entity:%1").arg(entityId), "--delta", "20,0,0", "--snap", "grid", "--grid", "16",
		"--output", temp.filePath("moved.map")});
	expect(process.waitForStarted() && process.waitForFinished(60000) && process.exitCode() == 0, "mixed CLI move",
		QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError()));
	LevelMapDocument source, written;
	expect(loadLevelMap({path, {}, {}}, &source, &error) && loadLevelMap({temp.filePath("moved.map"), {}, {}}, &written, &error), "mixed CLI maps load", error);
	setLevelMapSelection(&source, {brush(0), patchRef, {LevelMapSelectionKind::Entity, entityId}});
	expect(translateLevelMapObjects(&source, request, &report, &error) && serializeLevelMap(written).bytes == serializeLevelMap(source).bytes,
		"the CLI moves the patch and the entity as the core does", error);
}

void cli(const QString& executable)
{
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "CLI temporary folder")) { return; }
	auto map = fixture();
	QString group, error;
	expect(createLevelSceneNode(&map, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &group, &error)
			&& assignLevelSceneObjects(&map, group, {QStringLiteral("entity:1")}, &error) && setLevelSceneLocked(&map, group, true, &error),
		"CLI lock fixture", error);
	const QString path = temp.filePath("translate.map");
	QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(serializeLevelMap(map).bytes) > 0, "CLI fixture written")) { return; }
	file.close();
	const auto run = [&](const QStringList& args, int code = 0) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(executable, QStringList {"--cli", "--json", "--settings-file", temp.filePath("settings.ini"), "map"} + args);
		const bool done = process.waitForStarted() && process.waitForFinished(60000);
		const auto out = process.readAllStandardOutput(), err = process.readAllStandardError();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code, "CLI status", QString::fromUtf8(out + err) + args.join(' '));
		if (!done) {
			process.kill();
			process.waitForFinished();
		}
		return QJsonDocument::fromJson(out).object();
	};
	const auto load = [&](const QString& output) {
		LevelMapDocument result;
		QString problem;
		expect(loadLevelMap({output, {}, {}}, &result, &problem), "CLI output loads", problem);
		return result;
	};
	// The CLI and the core write the same map.
	LevelMapDocument source;
	expect(loadLevelMap({path, {}, {}}, &source, &error), "CLI source loads", error);
	const QString output = temp.filePath("bounds.map");
	auto report = run({"move", path, "--object", "brush:0", "--object", "brush:1", "--delta", "5,0,0", "--snap", "bounds", "--grid", "16", "--output", output});
	auto expected = source;
	setLevelMapSelection(&expected, {brush(0), brush(1)});
	LevelObjectTranslateRequest request;
	request.delta = {5, 0, 0, true};
	request.grid = 16;
	request.snap = LevelObjectTranslateSnap::Bounds;
	LevelObjectTranslateReport core;
	expect(translateLevelMapObjects(&expected, request, &core, &error), "core reference", error);
	expect(report["objects"].toInt() == 2 && report["snap"].toString() == QStringLiteral("bounds") && report["delta"].toObject()["x"].toDouble() == core.delta.x
			&& report["pivotAfter"].toObject()["x"].toDouble() == core.pivotAfter.x && report["textureLockPolicy"].toString() == QStringLiteral("locked"),
		"CLI report matches the core", QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));
	expect(serializeLevelMap(load(output)).bytes == serializeLevelMap(expected).bytes, "CLI writes what the core writes");
	// --to with a pivot, and the plain one-object move still takes its old route.
	report = run({"move", path, "--object", "brush:0", "--to", "64,0,0", "--pivot", "0,0,0", "--output", temp.filePath("to.map")});
	expect(report["placement"].toString() == QStringLiteral("pivot") && report["delta"].toObject()["x"].toDouble() == 64, "CLI --to and --pivot");
	expect(load(temp.filePath("to.map")).brushes[0].faces.size() == 6, "CLI --to output");
	report = run({"move", path, "--object", "brush:0", "--delta", "16,0,0", "--output", temp.filePath("plain.map")});
	expect(report.contains(QStringLiteral("map")) && !report.contains(QStringLiteral("pivotAfter")), "a plain one-object move keeps its route");
	// Refusals and usage errors.
	run({"move", path, "--object", "brush:0", "--object", "entity:1", "--delta", "16,0,0", "--output", temp.filePath("locked.map")}, 4);
	expect(!QFileInfo::exists(temp.filePath("locked.map")), "a locked object writes nothing");
	run({"move", path, "--object", "brush:0", "--object", "brush:1", "--delta", "3,0,0", "--snap", "grid", "--grid", "16", "--output", temp.filePath("zero.map")}, 4);
	for (const auto& bad : QVector<QStringList> {
			 {"--object", "brush:0", "--object", "brush:1", "--delta", "1,0,0", "--to", "1,0,0"},
			 {"--object", "brush:0", "--object", "brush:1", "--snap", "bounds", "--delta", "1,0,0"},
			 {"--object", "brush:0", "--snap", "sideways", "--grid", "16", "--delta", "1,0,0"},
			 {"--object", "brush:0", "--pivot", "0,0,0", "--delta", "1,0,0", "--snap", "grid", "--grid", "8"},
			 {"--object", "brush:0", "--object", "brush:1", "--delta", "1,0"},
			 {"--object", "brush:0", "--object", "brush:1", "--delta", "1,0,0", "--texture-lock", "maybe"},
		 }) {
		run(QStringList {"move", path} + bad + QStringList {"--output", temp.filePath("bad.map")}, 2);
	}
	expect(!QFileInfo::exists(temp.filePath("bad.map")), "usage errors write nothing");
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	tools();
	snapping();
	targets();
	translate();
	mixed(argc == 2 ? QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath() : QString());
	if (argc == 2) { cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath()); }
	return ok ? 0 : 1;
}
