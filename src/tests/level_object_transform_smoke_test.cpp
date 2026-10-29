// The Transform tool's and Shear Tool's core services and CLI routes
// (core/level_object_manipulator.h; map shear with --side and --offset, and
// map shear --about): the Transform tool's automatic pivots, its scale from
// outside a side or corner with grid travel, Shift's proportions and Ctrl's
// size grid; the side shear's slant and affine, one undo step equal to
// Shear's service along one axis and to the affine service within a side's
// plane, on brushes, a patch and a point entity; refusal of hidden, locked and
// out-of-group selections, of a side that does not move, of one lying on the
// plane that stays and of steep slants without touching the document; and the
// CLI writing exactly what the core writes, with --about no longer taken for
// the global About flag.
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

#include <algorithm>
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
bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) <= tolerance; }
bool at(const LevelMapVec3& v, double x, double y, double z, double tolerance = 1e-9)
{
	return v.valid && near(v.x, x, tolerance) && near(v.y, y, tolerance) && near(v.z, z, tolerance);
}
bool factors(const std::array<double, 3>& f, double x, double y, double z) { return near(f[0], x) && near(f[1], y) && near(f[2], z); }
QString text(const std::array<double, 3>& f) { return QStringLiteral("%1 %2 %3").arg(f[0]).arg(f[1]).arg(f[2]); }
QString text(const LevelMapVec3& v) { return QStringLiteral("%1 %2 %3").arg(v.x).arg(v.y).arg(v.z); }
LevelMapSelectionRef brush(int id) { return {LevelMapSelectionKind::QuakeBrush, id}; }

// Brushes 0 (0..32 by 0..16 by 0..16) and 1 (48..64 by 0..16 by 0..16), a
// 3x3 patch and an info_player_start, on a Quake III map.
struct Fixture {
	LevelMapDocument map;
	int patch = -1, entity = -1;
};
Fixture fixture()
{
	Fixture result;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	QString error;
	LevelMapPatch patch;
	LevelPatchCreateRequest patchRequest;
	patchRequest.center = {100, 0, 8, true};
	patchRequest.size = {32, 32, 32, true};
	expect(createLevelMap(create, &result.map, &error) && addLevelMapBoxBrush(&result.map, {0, 0, 0, true}, {32, 16, 16, true}, "studio/checker", nullptr, &error)
			&& addLevelMapBoxBrush(&result.map, {48, 0, 0, true}, {64, 16, 16, true}, "studio/checker", nullptr, &error)
			&& createLevelPatch(patchRequest, &patch, &error) && addLevelMapPatch(&result.map, patch, &result.patch, &error)
			&& addLevelMapEntity(&result.map, QStringLiteral("info_player_start"), {-40, 8, 24, true}, {{QStringLiteral("angle"), QStringLiteral("0"), 0}},
				&result.entity, &error),
		"fixture", error);
	setLevelMapSelection(&result.map, {});
	result.map.undoStack.clear();
	return result;
}

LevelObjectSideScaleDrag box(std::array<int, 3> sides, const LevelMapVec3& delta)
{
	LevelObjectSideScaleDrag drag;
	drag.sides = sides;
	drag.delta = delta;
	drag.mins = {0, 0, 0, true};
	drag.maxs = {32, 16, 16, true};
	drag.pivot = levelObjectTransformPivot(drag.mins, drag.maxs, sides);
	return drag;
}

void maths()
{
	// The automatic pivot sits on the opposite side of each named axis.
	const LevelMapVec3 low {0, 0, 0, true}, high {32, 16, 16, true};
	expect(at(levelObjectTransformPivot(low, high, {0, 0, 0}), 16, 8, 8), "no side: the centre");
	expect(at(levelObjectTransformPivot(low, high, {1, 0, 0}), 0, 8, 8), "the maximum X side: the minimum opposite");
	expect(at(levelObjectTransformPivot(low, high, {-1, 1, 0}), 32, 0, 8), "a corner: the opposite corner");
	expect(!levelObjectTransformPivot({}, high, {1, 0, 0}).valid, "an invalid box has no pivot");

	// Outside the maximum X side, 16 units out: the far side follows, from 32 to 48.
	auto drag = box({1, 0, 0}, {16, 5, 0, true});
	expect(factors(levelObjectSideScaleFactors(drag), 1.5, 1, 1), "one side follows the pointer about the opposite side", text(levelObjectSideScaleFactors(drag)));
	// The minimum side dragged outward (negative) grows too.
	drag = box({-1, 0, 0}, {-16, 0, 0, true});
	expect(factors(levelObjectSideScaleFactors(drag), 1.5, 1, 1), "the minimum side grows outward", text(levelObjectSideScaleFactors(drag)));
	// A corner region scales both of its axes.
	drag = box({1, -1, 0}, {16, -8, 0, true});
	expect(factors(levelObjectSideScaleFactors(drag), 1.5, 1.5, 1), "a corner scales both axes", text(levelObjectSideScaleFactors(drag)));
	// Grid travel and Ctrl's size grid.
	drag = box({1, 0, 0}, {10, 0, 0, true});
	drag.grid = 16;
	expect(factors(levelObjectSideScaleFactors(drag), 1.5, 1, 1), "the travel rounds to the grid", text(levelObjectSideScaleFactors(drag)));
	drag = box({1, 0, 0}, {9, 0, 0, true});
	drag.sizeGrid = 16;
	expect(factors(levelObjectSideScaleFactors(drag), 1.5, 1, 1), "Ctrl lands the size on the grid (41 to 48)", text(levelObjectSideScaleFactors(drag)));
	// Shift: every axis by the largest change.
	drag = box({0, 1, 0}, {0, 8, 0, true});
	drag.equal = true;
	expect(factors(levelObjectSideScaleFactors(drag), 1.5, 1.5, 1.5), "Shift keeps proportions", text(levelObjectSideScaleFactors(drag)));
	// A placed pivot inside the box: the side's distance from it counts.
	drag = box({1, 0, 0}, {8, 0, 0, true});
	drag.pivot = {24, 8, 8, true};
	expect(factors(levelObjectSideScaleFactors(drag), 2, 1, 1), "about a placed pivot 8 units in", text(levelObjectSideScaleFactors(drag)));
	// A size never vanishes; bad input and a side on the pivot change nothing.
	drag = box({1, 0, 0}, {-100, 0, 0, true});
	expect(factors(levelObjectSideScaleFactors(drag), 1.0 / 32, 1, 1), "shrinking stops at 1 unit", text(levelObjectSideScaleFactors(drag)));
	drag.grid = 16;
	expect(factors(levelObjectSideScaleFactors(drag), 0.5, 1, 1), "or at the grid", text(levelObjectSideScaleFactors(drag)));
	drag = box({1, 0, 0}, {std::nan(""), 0, 0, true});
	expect(factors(levelObjectSideScaleFactors(drag), 1, 1, 1), "non-finite travel changes nothing");
	drag = box({1, 0, 0}, {16, 0, 0, true});
	drag.pivot = {32, 8, 8, true};
	expect(factors(levelObjectSideScaleFactors(drag), 1, 1, 1), "a side on the pivot keeps its size");
}

void requests()
{
	auto data = fixture();
	setLevelMapSelection(&data.map, {brush(0), brush(1)});
	const auto target = levelObjectManipulatorTarget(data.map);
	// The top side (z+, 16) slides 8 along X and 4 along Y; the bottom (0) stays.
	LevelObjectShearRequest request;
	request.axis = 2;
	request.side = 1;
	request.offset = {8, 4, 99, true};
	LevelObjectShearReport report;
	QString error;
	expect(levelObjectShear(target, request, &report, &error) && factors(report.factors, 0.5, 0.25, 0) && near(report.anchor, 0) && report.objects == 2,
		"the top side's slide slants X and Y by Z about the bottom; the slide along Z is ignored", error + text(report.factors));
	const std::array<double, 12> slanted {1, 0, 0.5, 0, 0, 1, 0.25, 0, 0, 0, 1, 0};
	expect(report.matrix == slanted, "the affine slides X and Y by Z");
	// The minimum X side about a placed pivot at X 16.
	request = {};
	request.axis = 0;
	request.side = -1;
	request.offset = {0, 0, 8, true};
	request.pivot = {16, 0, 0, true};
	expect(levelObjectShear(target, request, &report, &error) && factors(report.factors, 0, 0, -0.5) && near(report.anchor, 16)
			&& near(report.matrix[8], -0.5) && near(report.matrix[11], 8),
		"about the plane through a placed pivot", error + text(report.factors));
	// Refusals.
	for (const auto& bad : {std::pair {3, 1}, std::pair {0, 0}, std::pair {-1, -1}}) {
		request = {};
		request.axis = bad.first;
		request.side = bad.second;
		request.offset = {0, 8, 0, true};
		expect(!levelObjectShear(target, request, &report, &error) && !error.isEmpty(), "a bad side is refused");
	}
	request = {};
	request.axis = 2;
	request.offset = {0, 0, 16, true};
	expect(!levelObjectShear(target, request, &report, &error) && !error.isEmpty(), "a side that does not slide is refused", error);
	request.offset = {std::nan(""), 0, 0, true};
	expect(!levelObjectShear(target, request, &report, &error), "a non-finite slide is refused");
	request.offset = {8, 0, 0, true};
	request.pivot = {0, 0, 16, true};
	expect(!levelObjectShear(target, request, &report, &error) && !error.isEmpty(), "a side on the plane that stays is refused", error);
	request.pivot = {};
	request.offset = {400, 0, 0, true};
	expect(!levelObjectShear(target, request, &report, &error) && !error.isEmpty(), "a slant over 16 units per unit is refused", error);
	request.offset = {8, 0, 0, true};
	request.pivot = {std::nan(""), 0, 0, true};
	expect(!levelObjectShear(target, request, &report, &error), "a non-finite pivot is refused");
}

void shear()
{
	// One axis: the top side slides 8 along X, as Shear's service slants it
	// about the bottom.
	auto data = fixture();
	setLevelMapSelection(&data.map, {brush(0), brush(1)});
	const auto before = serializeLevelMap(data.map).bytes;
	auto reference = data.map;
	LevelObjectShearRequest request;
	request.axis = 2;
	request.side = 1;
	request.offset = {8, 0, 0, true};
	request.textures = {false, false};
	LevelObjectShearReport report;
	QString error;
	expect(shearLevelMapObjects(&data.map, request, &report, &error), "a one-axis shear", error);
	expect(at(report.mins, 0, 0, 0) && at(report.maxs, 72, 16, 16) && data.map.undoStack.size() == 1, "the top slid 8 along X, one undo step",
		text(report.mins) + QStringLiteral(" / ") + text(report.maxs));
	expect(shearLevelMapSelectionAbout(&reference, 0, 2, 0.5, 0, {false, false}, &error)
			&& serializeLevelMap(reference).bytes == serializeLevelMap(data.map).bytes,
		"the side shear is Shear's shear about the opposite side", error);
	expect(undoLevelMapEdit(&data.map, &error) && serializeLevelMap(data.map).bytes == before, "undo restores the map exactly", error);

	// Within the top's plane: the affine service, with a patch and an entity.
	data = fixture();
	const LevelMapSelectionRef patch {LevelMapSelectionKind::QuakePatch, data.patch}, entity {LevelMapSelectionKind::Entity, data.entity};
	setLevelMapSelection(&data.map, {brush(0), patch, entity});
	reference = data.map;
	const auto start = serializeLevelMap(data.map).bytes;
	const auto target = levelObjectManipulatorTarget(data.map);
	request.offset = {8, -4, 0, true};
	LevelObjectShearReport planned;
	expect(levelObjectShear(target, request, &planned, &error), "the planned slant", error);
	expect(shearLevelMapObjects(&data.map, request, &report, &error) && report.objects == 3, "brush, patch and entity slant", error);
	expect(transformLevelMapSelectionAffine(&reference, planned.matrix, {false, false}, &error)
			&& serializeLevelMap(reference).bytes == serializeLevelMap(data.map).bytes,
		"a slide within the side's plane is the affine service's transform", error);
	const auto moved = std::find_if(data.map.entities.cbegin(), data.map.entities.cend(), [&](const auto& e) { return e.id == data.entity; });
	const double reach = target.maxs.z - target.mins.z, share = (24 - target.mins.z) / reach;
	expect(moved != data.map.entities.cend() && at(moved->origin, -40 + 8 * share, 8 - 4 * share, 24, 1e-6), "the entity's origin slants with its height",
		moved != data.map.entities.cend() ? text(moved->origin) : QString());
	expect(data.map.undoStack.size() == 1 && undoLevelMapEdit(&data.map, &error) && serializeLevelMap(data.map).bytes == start, "one undo step restores all three",
		error);

	// Texture lock behaves as Shear's own does: both succeed alike or both refuse.
	data = fixture();
	setLevelMapSelection(&data.map, {brush(0)});
	reference = data.map;
	request = {};
	request.axis = 2;
	request.offset = {8, 0, 0, true};
	request.textures = {true, false};
	const bool locked = shearLevelMapObjects(&data.map, request, &report, &error);
	QString referenceError;
	const bool referenceLocked = shearLevelMapSelectionAbout(&reference, 0, 2, 0.5, 0, {true, false}, &referenceError);
	expect(locked == referenceLocked && serializeLevelMap(reference).bytes == serializeLevelMap(data.map).bytes, "texture lock as Shear's service",
		error + QStringLiteral(" / ") + referenceError);

	// Refusals leave the document and its history untouched.
	data = fixture();
	setLevelMapSelection(&data.map, {brush(0), brush(1)});
	data.map.hiddenObjects = {QStringLiteral("brush:1")};
	const auto hidden = serializeLevelMap(data.map).bytes;
	request = {};
	request.axis = 2;
	request.offset = {8, 0, 0, true};
	request.textures = {false, false};
	expect(!shearLevelMapObjects(&data.map, request, &report, &error) && !error.isEmpty() && serializeLevelMap(data.map).bytes == hidden
			&& data.map.undoStack.isEmpty(),
		"a hidden object is refused untouched", error);
	data = fixture();
	QString layer;
	expect(createLevelSceneNode(&data.map, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
			&& assignLevelSceneObjects(&data.map, layer, {QStringLiteral("brush:1")}, &error) && setLevelSceneLocked(&data.map, layer, true, &error),
		"lock fixture", error);
	setLevelMapSelection(&data.map, {brush(0), brush(1)});
	const auto lockedMap = serializeLevelMap(data.map).bytes;
	const auto lockedDepth = data.map.undoStack.size();
	expect(!shearLevelMapObjects(&data.map, request, &report, &error) && serializeLevelMap(data.map).bytes == lockedMap
			&& data.map.undoStack.size() == lockedDepth,
		"a locked object is refused untouched", error);
	data = fixture();
	QString group;
	expect(createLevelSceneNode(&data.map, LevelSceneNodeKind::Group, QStringLiteral("Room"), {}, &group, &error)
			&& assignLevelSceneObjects(&data.map, group, {QStringLiteral("brush:0")}, &error) && openLevelSceneGroup(&data.map, group, &error),
		"group fixture", error);
	data.map.selection = {brush(0), brush(1)};
	expect(!shearLevelMapObjects(&data.map, request, &report, &error), "an object outside the open group is refused", error);
	data.map.selection = {brush(0)};
	const auto grouped = serializeLevelMap(data.map).bytes;
	request.offset = {0, 0, 0, true};
	expect(!shearLevelMapObjects(&data.map, request, &report, &error) && serializeLevelMap(data.map).bytes == grouped, "a side that does not slide is refused",
		error);
	request.offset = {8, 0, 0, true};
	expect(shearLevelMapObjects(&data.map, request, &report, &error) && report.objects == 1, "an object inside the open group slants", error);
	LevelMapDocument doom;
	LevelMapCreateRequest doomRequest;
	doomRequest.game = QStringLiteral("doom");
	expect(createLevelMap(doomRequest, &doom, &error), "Doom fixture", error);
	expect(!shearLevelMapObjects(&doom, request, &report, &error), "Doom maps are refused");
}

void cli(const QString& executable)
{
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "CLI temporary folder")) { return; }
	auto data = fixture();
	QString layer, error;
	expect(createLevelSceneNode(&data.map, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
			&& assignLevelSceneObjects(&data.map, layer, {QStringLiteral("entity:%1").arg(data.entity)}, &error) && setLevelSceneLocked(&data.map, layer, true, &error),
		"CLI lock fixture", error);
	const QString path = temp.filePath("shear.map");
	QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(serializeLevelMap(data.map).bytes) > 0, "CLI fixture written")) { return; }
	file.close();
	const auto run = [&](const QStringList& args, int code = 0, bool map = true) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(executable, QStringList {"--cli", "--json", "--settings-file", temp.filePath("settings.ini")} + (map ? QStringList {"map"} : QStringList {}) + args);
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
	LevelMapDocument source;
	expect(loadLevelMap({path, {}, {}}, &source, &error), "CLI source loads", error);

	// The top side of two brushes slides 16, 8 within its plane.
	const QString output = temp.filePath("side.map");
	auto report = run({"shear", path, "--object", "brush:0", "--object", "brush:1", "--side", "z+", "--offset", "16,8,0", "--allow-valve220", "--output", output});
	auto expected = source;
	setLevelMapSelection(&expected, {brush(0), brush(1)});
	LevelObjectShearRequest request;
	request.axis = 2;
	request.side = 1;
	request.offset = {16, 8, 0, true};
	request.textures = {true, true};
	LevelObjectShearReport core;
	expect(shearLevelMapObjects(&expected, request, &core, &error), "core reference", error);
	const auto reported = report["factors"].toArray();
	expect(report["command"].toString() == QStringLiteral("map shear") && report["placement"].toString() == QStringLiteral("side-shear")
			&& report["side"].toString() == QStringLiteral("z+") && report["objects"].toInt() == 2 && reported.size() == 3 && reported[0].toDouble() == 1
			&& reported[1].toDouble() == 0.5 && report["anchor"].toDouble() == 0 && report["matrix"].toArray().size() == 12
			&& report["textureLockPolicy"].toString() == QStringLiteral("locked") && report["allowValve220"].toBool(),
		"CLI report matches the core", QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));
	expect(serializeLevelMap(load(output)).bytes == serializeLevelMap(expected).bytes, "CLI writes what the core writes");

	// The minimum X side about a placed pivot, along one axis, with a patch.
	report = run({"shear", path, "--object", "brush:0", "--object", QStringLiteral("patch:%1").arg(data.patch), "--side", "x-", "--offset", "0,0,8", "--pivot",
		"16,0,0", "--texture-lock", "off", "--output", temp.filePath("pivot.map")});
	expected = source;
	setLevelMapSelection(&expected, {brush(0), {LevelMapSelectionKind::QuakePatch, data.patch}});
	request = {};
	request.axis = 0;
	request.side = -1;
	request.offset = {0, 0, 8, true};
	request.pivot = {16, 0, 0, true};
	request.textures = {false, false};
	expect(shearLevelMapObjects(&expected, request, &core, &error), "core pivot reference", error);
	expect(report["anchor"].toDouble() == 16 && report["pivot"].toObject()["x"].toDouble() == 16
			&& report["textureLockPolicy"].toString() == QStringLiteral("source-parameters"),
		"CLI pivot report", QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));
	expect(serializeLevelMap(load(temp.filePath("pivot.map"))).bytes == serializeLevelMap(expected).bytes, "CLI pivot shear equals the core");

	// --about is map shear's own again: it slants about Z 0, as the core does,
	// instead of printing the About screen; the global flag still works.
	report = run({"shear", path, "--object", "brush:0", "--axis", "x", "--along", "z", "--factor", "0.5", "--about", "0", "--texture-lock", "off", "--output",
		temp.filePath("about.map")});
	expected = source;
	setLevelMapSelection(&expected, {brush(0)});
	expect(shearLevelMapSelectionAbout(&expected, 0, 2, 0.5, 0, {false, false}, &error), "core about reference", error);
	expect(report["command"].toString() == QStringLiteral("map shear") && report["about"].isDouble() && report["about"].toDouble() == 0
			&& !report.contains(QStringLiteral("placement")),
		"map shear --about shears", QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));
	expect(serializeLevelMap(load(temp.filePath("about.map"))).bytes == serializeLevelMap(expected).bytes, "map shear --about writes the core's shear");
	report = run({"shear", path, "--object", "brush:0", "--axis", "x", "--along", "z", "--factor", "0.5", "--about", "-16", "--texture-lock", "off", "--output",
		temp.filePath("about-negative.map")});
	expect(report["about"].toDouble() == -16, "a negative --about is a coordinate too", QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));
	for (const auto& global : QVector<QStringList> {{"--about"}, {"--about=1"}, {"--about", "5"}}) {
		report = run(global, 0, false);
		expect(report["command"].toString() == QStringLiteral("--about") && report["about"].isObject(), "the global --about still prints About",
			global.join(' ') + QStringLiteral(": ") + QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));
	}
	// Under map shear a missing or unusable --about value is a usage error,
	// not the About screen.
	for (const auto& bad : QVector<QStringList> {{"--about"}, {"--about", "abc"}, {"--about", "1e400"}, {"--about", "inf"},
			 {"--about", "--texture-lock", "off"}}) {
		report = run(QStringList {"shear", path, "--object", "brush:0", "--axis", "x", "--along", "z", "--factor", "0.5", "--output",
						 temp.filePath("about-bad.map")} + bad, 2);
		expect(report["command"].toString() == QStringLiteral("map shear"), "a bad --about is map shear's usage error", bad.join(' '));
	}
	expect(!QFileInfo::exists(temp.filePath("about-bad.map")), "a bad --about writes nothing");

	// Refusals and usage errors write nothing.
	run({"shear", path, "--object", "brush:0", "--object", QStringLiteral("entity:%1").arg(data.entity), "--side", "z+", "--offset", "8,0,0", "--output",
		temp.filePath("locked.map")}, 4);
	expect(!QFileInfo::exists(temp.filePath("locked.map")), "a locked object writes nothing");
	run({"shear", path, "--object", "brush:0", "--side", "z+", "--offset", "0,0,8", "--output", temp.filePath("still.map")}, 4);
	expect(!QFileInfo::exists(temp.filePath("still.map")), "a side that does not slide writes nothing");
	for (const auto& bad : QVector<QStringList> {
			 {"--object", "brush:0", "--side", "w+", "--offset", "8,0,0"},
			 {"--object", "brush:0", "--side", "z", "--offset", "8,0,0"},
			 {"--object", "brush:0", "--side", "z+", "--offset", "8,0"},
			 {"--object", "brush:0", "--side", "z+"},
			 {"--object", "brush:0", "--side", "z+", "--offset", "8,0,0", "--pivot", "1,2"},
			 {"--object", "brush:0", "--side", "z+", "--offset", "8,0,0", "--texture-lock", "maybe"},
			 {"--object", "brush:0", "--side", "z+", "--offset", "8,0,0", "--texture-lock", "off", "--allow-valve220"},
			 {"--side", "z+", "--offset", "8,0,0"},
		 }) {
		run(QStringList {"shear", path} + bad + QStringList {"--output", temp.filePath("bad.map")}, 2);
	}
	expect(!QFileInfo::exists(temp.filePath("bad.map")), "usage errors write nothing");
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	maths();
	requests();
	shear();
	if (argc == 2) { cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath()); }
	return ok ? 0 : 1;
}
