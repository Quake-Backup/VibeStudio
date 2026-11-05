// The Scale tool's core service and CLI route (core/level_object_manipulator.h,
// map resize with --drag, --equal, --size-grid or --context): NetRadiant
// Custom's axis and square factors with grid travel, Shift's equal axes and
// Ctrl's grid sizes; the numeric request; one undo step on brushes, a patch
// and a point entity equal to Scale Selection's service; refusal of hidden,
// locked and out-of-group selections and of mirroring or unchanged factors
// without touching the document; and the CLI writing exactly what the core
// writes.
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

LevelObjectScaleDrag box(int handle, const LevelMapVec3& delta)
{
	LevelObjectScaleDrag drag;
	drag.handle = handle;
	drag.delta = delta;
	drag.mins = {0, 0, 0, true};
	drag.maxs = {32, 16, 16, true};
	drag.pivot = {16, 8, 8, true};
	return drag;
}

void maths()
{
	// The farther side follows the pointer: 16 units from the centre doubles 32.
	auto drag = box(0, {16, 0, 0, true});
	expect(factors(levelObjectScaleFactors(drag), 2, 1, 1), "an X handle dragged 16 units from the centre doubles X", text(levelObjectScaleFactors(drag)));
	drag.pivot = {0, 8, 8, true};
	expect(factors(levelObjectScaleFactors(drag), 1.5, 1, 1), "about the low side, the far side is 32 units away", text(levelObjectScaleFactors(drag)));
	drag = box(0, {10, 0, 0, true});
	drag.grid = 16;
	expect(factors(levelObjectScaleFactors(drag), 2, 1, 1), "the travel rounds to the grid");
	drag.delta = {7, 0, 0, true};
	expect(factors(levelObjectScaleFactors(drag), 1, 1, 1), "travel that rounds to nothing leaves the size");
	// Shift: every axis alike.
	drag = box(2, {4, 0, 0, true});
	drag.delta = {0, 0, 4, true};
	drag.equal = true;
	expect(factors(levelObjectScaleFactors(drag), 1.5, 1.5, 1.5), "Shift scales every axis by the handle's factor", text(levelObjectScaleFactors(drag)));
	// Ctrl: the size lands on the size grid (42 to 48).
	drag = box(0, {5, 0, 0, true});
	drag.sizeGrid = 16;
	expect(factors(levelObjectScaleFactors(drag), 1.5, 1, 1), "Ctrl lands the size on the grid", text(levelObjectScaleFactors(drag)));
	// A size never vanishes: 1 unit without a grid, the grid with one.
	drag = box(0, {-100, 0, 0, true});
	expect(factors(levelObjectScaleFactors(drag), 1.0 / 32, 1, 1), "shrinking stops at 1 unit", text(levelObjectScaleFactors(drag)));
	drag.grid = 16;
	expect(factors(levelObjectScaleFactors(drag), 0.5, 1, 1), "or at the grid", text(levelObjectScaleFactors(drag)));
	// The square scales the view's axes; Shift takes the larger change.
	drag = box(3, {16, 4, 0, true});
	drag.viewAxis = 2;
	expect(factors(levelObjectScaleFactors(drag), 2, 1.5, 1), "the square scales across the Top view", text(levelObjectScaleFactors(drag)));
	drag.equal = true;
	expect(factors(levelObjectScaleFactors(drag), 2, 2, 1), "Shift scales the view's axes alike by the larger change, leaving depth", text(levelObjectScaleFactors(drag)));
	// No extent along an axis keeps it; bad input changes nothing.
	drag = box(2, {0, 0, 8, true});
	drag.maxs.z = 0;
	drag.pivot.z = 0;
	expect(factors(levelObjectScaleFactors(drag), 1, 1, 1), "an axis without extent keeps 1");
	drag = box(0, {std::nan(""), 0, 0, true});
	expect(factors(levelObjectScaleFactors(drag), 1, 1, 1), "non-finite travel changes nothing");
	drag = box(7, {16, 0, 0, true});
	expect(factors(levelObjectScaleFactors(drag), 1, 1, 1), "an unknown handle changes nothing");
}

void requests()
{
	auto data = fixture();
	setLevelMapSelection(&data.map, {brush(0), brush(1)});
	const auto target = levelObjectManipulatorTarget(data.map);
	LevelObjectScaleRequest request;
	request.factors = {2, 1, 0.5};
	request.textures = {true, true};
	LevelMapScaleRequest scale;
	LevelObjectScaleReport report;
	QString error;
	expect(levelObjectScale(target, request, &scale, &report, &error) && factors(scale.factors, 2, 1, 0.5) && at(scale.pivot, 32, 8, 8)
			&& scale.textures.enabled && scale.textures.allowValve220 && report.objects == 2 && at(report.pivot, 32, 8, 8),
		"the numeric scale about the selection's centre with Texture Scale Lock", error);
	request.pivot = {0, 0, 0, true};
	expect(levelObjectScale(target, request, &scale, &report, &error) && at(scale.pivot, 0, 0, 0), "a placed pivot", error);
	for (const auto& bad : {std::array<double, 3> {1, 1, 1}, std::array<double, 3> {0, 1, 1}, std::array<double, 3> {-1, 1, 1},
			 std::array<double, 3> {2000, 1, 1}, std::array<double, 3> {std::nan(""), 1, 1}}) {
		request.factors = bad;
		expect(!levelObjectScale(target, request, &scale, &report, &error) && !error.isEmpty(), "unchanged, mirroring, huge or non-finite factors are refused",
			text(bad));
	}
	request.factors = {2, 1, 1};
	request.pivot = {std::nan(""), 0, 0, true};
	expect(!levelObjectScale(target, request, &scale, &report, &error), "a non-finite pivot is refused");
}

void scale()
{
	// Two brushes twice as wide about their centre (32, 8, 8).
	auto data = fixture();
	setLevelMapSelection(&data.map, {brush(0), brush(1)});
	const auto before = serializeLevelMap(data.map).bytes;
	auto reference = data.map;
	LevelObjectScaleRequest request;
	request.factors = {2, 1, 1};
	request.textures = {true, false};
	LevelObjectScaleReport report;
	QString error;
	expect(scaleLevelMapObjects(&data.map, request, &report, &error), "scale", error);
	expect(at(report.mins, -32, 0, 0) && at(report.maxs, 96, 16, 16) && data.map.undoStack.size() == 1, "bounds after scaling, one undo step",
		text(report.mins) + QStringLiteral(" / ") + text(report.maxs));
	LevelMapScaleRequest plain;
	plain.factors = {2, 1, 1};
	plain.pivot = {32, 8, 8, true};
	plain.textures = {true, false};
	expect(scaleLevelMapSelection(&reference, plain, &error) && serializeLevelMap(reference).bytes == serializeLevelMap(data.map).bytes,
		"the tool's scale is Scale Selection's scale", error);
	expect(undoLevelMapEdit(&data.map, &error) && serializeLevelMap(data.map).bytes == before, "undo restores the map exactly", error);

	// A patch and a point entity scale with a brush, about a placed pivot.
	data = fixture();
	const LevelMapSelectionRef patch {LevelMapSelectionKind::QuakePatch, data.patch}, entity {LevelMapSelectionKind::Entity, data.entity};
	setLevelMapSelection(&data.map, {brush(0), patch, entity});
	reference = data.map;
	const auto start = serializeLevelMap(data.map).bytes;
	const auto patches = data.map.patches;
	request = {};
	request.factors = {0.5, 2, 1};
	request.pivot = {0, 0, 0, true};
	expect(scaleLevelMapObjects(&data.map, request, &report, &error) && report.objects == 3, "brush, patch and entity scale", error);
	plain = {};
	plain.factors = {0.5, 2, 1};
	plain.pivot = {0, 0, 0, true};
	expect(scaleLevelMapSelection(&reference, plain, &error) && serializeLevelMap(reference).bytes == serializeLevelMap(data.map).bytes,
		"they scale as Scale Selection scales them", error);
	const auto moved = std::find_if(data.map.entities.cbegin(), data.map.entities.cend(), [&](const auto& e) { return e.id == data.entity; });
	expect(moved != data.map.entities.cend() && at(moved->origin, -20, 16, 24), "the entity's origin scales about the pivot",
		moved != data.map.entities.cend() ? text(moved->origin) : QString());
	const auto original = std::find_if(patches.cbegin(), patches.cend(), [&](const auto& p) { return p.id == data.patch; });
	const auto scaled = std::find_if(data.map.patches.cbegin(), data.map.patches.cend(), [&](const auto& p) { return p.id == data.patch; });
	bool patchScaled = original != patches.cend() && scaled != data.map.patches.cend() && scaled->controlPoints.size() == original->controlPoints.size();
	for (qsizetype i = 0; patchScaled && i < scaled->controlPoints.size(); ++i) {
		const auto& p = original->controlPoints[i];
		patchScaled = at(scaled->controlPoints[i], p.x * 0.5, p.y * 2, p.z, 1e-6);
	}
	expect(patchScaled, "the patch's control points scale too");
	expect(data.map.undoStack.size() == 1 && undoLevelMapEdit(&data.map, &error) && serializeLevelMap(data.map).bytes == start, "one undo step restores all three",
		error);

	// Refusals: hidden, locked, outside the open group, mirroring, Doom.
	data = fixture();
	setLevelMapSelection(&data.map, {brush(0), brush(1)});
	data.map.hiddenObjects = {QStringLiteral("brush:1")};
	const auto hidden = serializeLevelMap(data.map).bytes;
	request = {};
	request.factors = {2, 2, 2};
	expect(!scaleLevelMapObjects(&data.map, request, &report, &error) && !error.isEmpty() && serializeLevelMap(data.map).bytes == hidden
			&& data.map.undoStack.isEmpty(),
		"a hidden object is refused untouched", error);
	data = fixture();
	QString layer;
	expect(createLevelSceneNode(&data.map, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
			&& assignLevelSceneObjects(&data.map, layer, {QStringLiteral("brush:1")}, &error) && setLevelSceneLocked(&data.map, layer, true, &error),
		"lock fixture", error);
	setLevelMapSelection(&data.map, {brush(0), brush(1)});
	const auto locked = serializeLevelMap(data.map).bytes;
	const auto lockedDepth = data.map.undoStack.size();
	expect(!scaleLevelMapObjects(&data.map, request, &report, &error) && serializeLevelMap(data.map).bytes == locked && data.map.undoStack.size() == lockedDepth,
		"a locked object is refused untouched", error);
	data = fixture();
	QString group;
	expect(createLevelSceneNode(&data.map, LevelSceneNodeKind::Group, QStringLiteral("Room"), {}, &group, &error)
			&& assignLevelSceneObjects(&data.map, group, {QStringLiteral("brush:0")}, &error) && openLevelSceneGroup(&data.map, group, &error),
		"group fixture", error);
	data.map.selection = {brush(0), brush(1)};
	expect(!scaleLevelMapObjects(&data.map, request, &report, &error), "an object outside the open group is refused", error);
	data.map.selection = {brush(0)};
	const auto grouped = serializeLevelMap(data.map).bytes;
	request.factors = {-1, 1, 1};
	expect(!scaleLevelMapObjects(&data.map, request, &report, &error) && serializeLevelMap(data.map).bytes == grouped, "a mirroring factor is refused untouched",
		error);
	request.factors = {2, 1, 1};
	expect(scaleLevelMapObjects(&data.map, request, &report, &error) && report.objects == 1, "an object inside the open group scales", error);
	LevelMapDocument doom;
	LevelMapCreateRequest doomRequest;
	doomRequest.game = QStringLiteral("doom");
	expect(createLevelMap(doomRequest, &doom, &error), "Doom fixture", error);
	expect(!scaleLevelMapObjects(&doom, request, &report, &error), "Doom maps are refused");
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
	const QString path = temp.filePath("scale.map");
	QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(serializeLevelMap(data.map).bytes) > 0, "CLI fixture written")) { return; }
	file.close();
	const auto run = [&](const QStringList& args, int code = 0, QString* errors = nullptr) {
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
		if (errors) { *errors = QString::fromUtf8(out + err); }
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
	// The X handle dragged 10 units on a 16 grid with Shift: twice the size
	// on every axis, as the core scales it.
	const QString output = temp.filePath("axis.map");
	auto report = run({"resize", path, "--object", "brush:0", "--object", "brush:1", "--axis", "x", "--drag", "10", "--grid", "16", "--equal", "--output", output});
	auto expected = source;
	setLevelMapSelection(&expected, {brush(0), brush(1)});
	LevelObjectScaleRequest request;
	request.factors = {1.5, 1.5, 1.5};
	LevelObjectScaleReport core;
	expect(scaleLevelMapObjects(&expected, request, &core, &error), "core reference", error);
	const auto reported = report["factors"].toArray();
	expect(report["placement"].toString() == QStringLiteral("manipulator") && report["objects"].toInt() == 2 && reported.size() == 3
			&& reported[0].toDouble() == 1.5 && reported[2].toDouble() == 1.5 && report["handle"].toString() == QStringLiteral("x") && report["equal"].toBool()
			&& report["pivot"].toObject()["x"].toDouble() == 32 && report["textureLockPolicy"].toString() == QStringLiteral("source-parameters"),
		"CLI report matches the core", QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));
	expect(serializeLevelMap(load(output)).bytes == serializeLevelMap(expected).bytes, "CLI writes what the core writes");
	// The Top square with Ctrl's size grid and a placed pivot, and a patch.
	report = run({"resize", path, "--object", "brush:0", "--object", QStringLiteral("patch:%1").arg(data.patch), "--view", "top", "--drag", "5", "--size-grid",
		"16", "--pivot", "0,0,0", "--texture-lock", "on", "--output", temp.filePath("square.map")});
	expected = source;
	setLevelMapSelection(&expected, {brush(0), {LevelMapSelectionKind::QuakePatch, data.patch}});
	const auto target = levelObjectManipulatorTarget(expected);
	LevelObjectScaleDrag drag;
	drag.handle = 3;
	drag.delta = {5, 5, 0, true};
	drag.mins = target.mins;
	drag.maxs = target.maxs;
	drag.pivot = {0, 0, 0, true};
	drag.viewAxis = 2;
	drag.sizeGrid = 16;
	request = {};
	request.factors = levelObjectScaleFactors(drag);
	request.pivot = {0, 0, 0, true};
	request.textures = {true, false};
	expect(scaleLevelMapObjects(&expected, request, &core, &error), "core square reference", error);
	expect(report["handle"].toString() == QStringLiteral("top") && report["factors"].toArray()[2].toDouble() == 1 && report["sizeGrid"].toDouble() == 16
			&& report["textureLockPolicy"].toString() == QStringLiteral("locked"),
		"CLI square report", QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));
	expect(serializeLevelMap(load(temp.filePath("square.map"))).bytes == serializeLevelMap(expected).bytes, "CLI square scale equals the core");
	// Plain map resize --factors keeps its route.
	report = run({"resize", path, "--object", "brush:0", "--factors", "-1,2,1", "--output", temp.filePath("plain.map")});
	expect(!report.contains(QStringLiteral("placement")) && report["mirrored"].toBool(), "the plain map resize keeps its route");
	// Refusals and usage errors write nothing.
	run({"resize", path, "--object", "brush:0", "--object", QStringLiteral("entity:%1").arg(data.entity), "--axis", "x", "--drag", "16", "--output",
		temp.filePath("locked.map")}, 4);
	expect(!QFileInfo::exists(temp.filePath("locked.map")), "a locked object writes nothing");
	run({"resize", path, "--object", "brush:0", "--axis", "x", "--drag", "4", "--grid", "16", "--output", temp.filePath("zero.map")}, 4);
	expect(!QFileInfo::exists(temp.filePath("zero.map")), "a drag that rounds to nothing writes nothing");
	for (const auto& bad : QVector<QStringList> {
			 {"--object", "brush:0", "--drag", "16"},
			 {"--object", "brush:0", "--axis", "x", "--view", "top", "--drag", "16"},
			 {"--object", "brush:0", "--axis", "w", "--drag", "16"},
			 {"--object", "brush:0", "--view", "under", "--drag", "16"},
			 {"--object", "brush:0", "--axis", "x", "--drag", "far"},
			 {"--object", "brush:0", "--axis", "x"},
			 {"--object", "brush:0", "--factors", "2,1,1", "--equal"},
			 {"--object", "brush:0", "--axis", "x", "--drag", "16", "--size-grid", "0"},
			 {"--object", "brush:0", "--axis", "x", "--drag", "16", "--texture-lock", "maybe"},
			 {"--axis", "x", "--drag", "16"},
		 }) {
		run(QStringList {"resize", path} + bad + QStringList {"--output", temp.filePath("bad.map")}, 2);
	}
	expect(!QFileInfo::exists(temp.filePath("bad.map")), "usage errors write nothing");
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	maths();
	requests();
	scale();
	if (argc == 2) { cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath()); }
	return ok ? 0 : 1;
}
