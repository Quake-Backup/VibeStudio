// The Rotate tool's core service and CLI route (core/level_object_manipulator.h,
// map rotate with --around, --angle-step or --context): angle snapping, the
// any-axis turn as one plain turn about a world axis or NetRadiant Custom's
// Euler X, Y, Z turn, the free turn's sphere and arcball, the preview wire;
// one undo step on brushes, a patch and a point entity; refusal of hidden,
// locked and out-of-group selections and of whole turns without touching the
// document; and the CLI writing exactly what the core writes.
#include "core/level_document.h"
#include "core/level_map.h"
#include "core/level_object_manipulator.h"
#include "core/level_patch.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_texture_mapping.h"

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
QString text(const LevelMapVec3& v) { return QStringLiteral("%1 %2 %3 %4").arg(v.x).arg(v.y).arg(v.z).arg(v.valid); }
LevelMapSelectionRef brush(int id) { return {LevelMapSelectionKind::QuakeBrush, id}; }

// Brushes 0 (0..32 by 0..16 by 0..16) and 1 (48..64 by 0..16 by 0..16), a
// 3x3 patch and an info_player_start with an angle, on a Quake III map.
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

// R = Rz(c) Ry(b) Rx(a) applied to a vector, by the numeric service's turns.
LevelMapVec3 euler(const std::array<double, 3>& degrees, LevelMapVec3 v)
{
	for (int axis = 0; axis < 3; ++axis) { v = rotateLevelVector(v, axis, degrees[static_cast<size_t>(axis)]); }
	return v;
}

void maths()
{
	expect(near(snapLevelObjectAngle(37, 15), 30) && near(snapLevelObjectAngle(38, 15), 45) && near(snapLevelObjectAngle(-8, 15), -15)
			&& near(snapLevelObjectAngle(37, 0), 37),
		"angles round to the step; no step leaves them");
	// Rodrigues columns: a quarter turn about Z, and the third of a turn about
	// (1,1,1) that cycles the axes.
	auto columns = levelObjectRotationColumns({0, 0, 2, true}, 90);
	expect(at(columns[0], 0, 1, 0) && at(columns[1], -1, 0, 0) && at(columns[2], 0, 0, 1), "quarter turn about Z", text(columns[0]));
	columns = levelObjectRotationColumns({1, 1, 1, true}, 120);
	expect(at(columns[0], 0, 1, 0) && at(columns[1], 0, 0, 1) && at(columns[2], 1, 0, 0), "a third of a turn about 1,1,1 cycles the axes", text(columns[0]));
	columns = levelObjectRotationColumns({0, 0, 0, true}, 90);
	expect(at(columns[0], 1, 0, 0) && at(columns[1], 0, 1, 0), "a zero axis is the identity");
	// The Euler X, Y, Z turns rebuild every rotation, gimbal lock included.
	const QVector<QPair<LevelMapVec3, double>> turns {{{1, 1, 1, true}, 120}, {{0.3, -0.5, 0.8, true}, 37}, {{0, 1, 0, true}, 90}, {{0, 1, 0, true}, -90},
		{{1, 0, 0, true}, 200}, {{-2, 0.5, 0.1, true}, -71}};
	for (const auto& turn : turns) {
		const auto wanted = levelObjectRotationColumns(turn.first, turn.second);
		const auto angles = levelObjectEulerXyz(wanted);
		bool same = true;
		for (int axis = 0; axis < 3; ++axis) {
			LevelMapVec3 basis {0, 0, 0, true};
			(axis == 0 ? basis.x : axis == 1 ? basis.y : basis.z) = 1;
			const auto rebuilt = euler(angles, basis);
			same = same && at(rebuilt, wanted[static_cast<size_t>(axis)].x, wanted[static_cast<size_t>(axis)].y, wanted[static_cast<size_t>(axis)].z, 1e-9);
		}
		expect(same, "Euler X, Y, Z turns rebuild the rotation", QStringLiteral("%1 %2 -> %3 %4 %5").arg(text(turn.first)).arg(turn.second)
			.arg(angles[0]).arg(angles[1]).arg(angles[2]));
	}
	// The free turn's sphere, on the viewer's side, and its edge for a miss.
	expect(at(levelObjectSpherePoint({0, 0, 100, true}, {0, 0, -1, true}, {0, 0, 0, true}, 10), 0, 0, 1), "a central ray meets the sphere's top");
	expect(at(levelObjectSpherePoint({5, 0, 100, true}, {0, 0, -3, true}, {0, 0, 0, true}, 10), 0.5, 0, std::sqrt(0.75)), "an offset ray meets it nearer the edge");
	expect(at(levelObjectSpherePoint({20, 0, 100, true}, {0, 0, -1, true}, {0, 0, 0, true}, 10), 1, 0, 0), "a miss gives the nearest edge");
	expect(!levelObjectSpherePoint({0, 0, 0, true}, {0, 0, 0, true}, {0, 0, 0, true}, 10).valid, "a zero direction is invalid");
	LevelMapVec3 axis;
	double degrees = 0;
	expect(levelObjectArcball({1, 0, 0, true}, {0, 1, 0, true}, &axis, &degrees) && at(axis, 0, 0, 1) && near(degrees, 90), "arcball: X to Y turns 90 about Z");
	expect(!levelObjectArcball({1, 0, 0, true}, {1, 0, 0, true}, &axis, &degrees), "parallel vectors give no turn");
}

void requests()
{
	auto data = fixture();
	setLevelMapSelection(&data.map, {brush(0), brush(1)});
	const auto target = levelObjectManipulatorTarget(data.map);
	LevelObjectRotateRequest request;
	request.axis = {0, 0, -3, true};
	request.degrees = 37;
	request.angleStep = 15;
	LevelMapRotationRequest rotation;
	LevelObjectRotateReport report;
	QString error;
	expect(levelObjectRotation(target, request, &rotation, &report, &error) && !rotation.euler && rotation.axis == 2 && near(rotation.degrees, -30)
			&& report.worldAxis == 2 && near(report.degrees, 30) && at(report.axis, 0, 0, -1) && at(report.pivot, 32, 8, 8) && report.objects == 2,
		"a world axis is one plain turn; a negative axis turns the other way", error);
	request = {};
	request.axis = {1, 1, 0, true};
	request.degrees = 45;
	request.pivot = {0, 0, 0, true};
	request.textures = {true, true};
	expect(levelObjectRotation(target, request, &rotation, &report, &error) && rotation.euler && report.worldAxis == -1 && at(rotation.pivot, 0, 0, 0)
			&& rotation.textureLock && rotation.allowValve220,
		"any other axis is an Euler turn about the placed pivot with the texture policy", error);
	const auto columns = levelObjectRotationColumns(request.axis, 45);
	expect(at(euler(rotation.eulerDegrees, {1, 0, 0, true}), columns[0].x, columns[0].y, columns[0].z, 1e-9), "the Euler turn is the requested rotation");
	request.degrees = 7;
	request.angleStep = 15;
	expect(!levelObjectRotation(target, request, &rotation, &report, &error) && !error.isEmpty(), "a turn that snaps to nothing is refused", error);
	request.degrees = 720;
	request.angleStep = 0;
	expect(!levelObjectRotation(target, request, &rotation, &report, &error), "whole turns are refused");
	request.degrees = 30;
	request.axis = {0, 0, 0, true};
	expect(!levelObjectRotation(target, request, &rotation, &report, &error), "a zero axis is refused");
	request.axis = {0, 0, 1, true};
	request.angleStep = 400;
	expect(!levelObjectRotation(target, request, &rotation, &report, &error), "an angle step above 360 is refused");
	request.angleStep = 0;
	request.pivot = {std::nan(""), 0, 0, true};
	expect(!levelObjectRotation(target, request, &rotation, &report, &error), "a non-finite pivot is refused");
}

void rotate()
{
	// Two brushes a quarter turn about Z through their centre (32, 8, 8).
	auto data = fixture();
	setLevelMapSelection(&data.map, {brush(0), brush(1)});
	const auto before = serializeLevelMap(data.map).bytes;
	auto reference = data.map;
	LevelObjectRotateRequest request;
	request.axis = {0, 0, 1, true};
	request.degrees = 80;
	request.angleStep = 45;
	request.textures = {true, false};
	LevelObjectRotateReport report;
	QString error;
	expect(rotateLevelMapObjects(&data.map, request, &report, &error), "quarter turn", error);
	expect(near(report.degrees, 90) && at(report.mins, 24, -24, 0) && at(report.maxs, 40, 40, 16) && data.map.undoStack.size() == 1,
		"bounds after the turn, one undo step", text(report.mins) + QStringLiteral(" / ") + text(report.maxs));
	LevelMapRotationRequest plain;
	plain.axis = 2;
	plain.degrees = 90;
	plain.pivot = {32, 8, 8, true};
	plain.textureLock = true;
	expect(rotateLevelMapSelection(&reference, plain, &error) && serializeLevelMap(reference).bytes == serializeLevelMap(data.map).bytes,
		"the tool's turn is Rotate Selection's turn", error);
	expect(undoLevelMapEdit(&data.map, &error) && serializeLevelMap(data.map).bytes == before, "undo restores the map exactly", error);

	// A patch and a point entity turn with the brushes, about a placed pivot.
	data = fixture();
	const LevelMapSelectionRef patch {LevelMapSelectionKind::QuakePatch, data.patch}, entity {LevelMapSelectionKind::Entity, data.entity};
	setLevelMapSelection(&data.map, {brush(0), patch, entity});
	reference = data.map;
	const auto start = serializeLevelMap(data.map).bytes;
	const auto patches = data.map.patches;
	request = {};
	request.axis = {0, 0, 1, true};
	request.degrees = 90;
	request.pivot = {0, 0, 0, true};
	expect(rotateLevelMapObjects(&data.map, request, &report, &error) && report.objects == 3, "brush, patch and entity turn", error);
	plain.pivot = {0, 0, 0, true};
	expect(rotateLevelMapSelection(&reference, plain, &error) && serializeLevelMap(reference).bytes == serializeLevelMap(data.map).bytes,
		"they turn as Rotate Selection turns them", error);
	const auto moved = std::find_if(data.map.entities.cbegin(), data.map.entities.cend(), [&](const auto& e) { return e.id == data.entity; });
	expect(moved != data.map.entities.cend() && at(moved->origin, -8, -40, 24), "the entity's origin turns about the pivot",
		moved != data.map.entities.cend() ? text(moved->origin) : QString());
	// Each control point (x, y, z) goes to (-y, x, z).
	const auto original = std::find_if(patches.cbegin(), patches.cend(), [&](const auto& p) { return p.id == data.patch; });
	const auto turned = std::find_if(data.map.patches.cbegin(), data.map.patches.cend(), [&](const auto& p) { return p.id == data.patch; });
	bool patchTurned = original != patches.cend() && turned != data.map.patches.cend() && turned->controlPoints.size() == original->controlPoints.size();
	for (qsizetype i = 0; patchTurned && i < turned->controlPoints.size(); ++i) {
		const auto& p = original->controlPoints[i];
		patchTurned = at(turned->controlPoints[i], -p.y, p.x, p.z, 1e-6);
	}
	expect(patchTurned, "the patch's control points turn too");
	expect(data.map.undoStack.size() == 1 && undoLevelMapEdit(&data.map, &error) && serializeLevelMap(data.map).bytes == start, "one undo step restores all three", error);

	// Any axis: the Euler turn, with Valve 220 conversion allowed; without
	// it, a refusal leaves the map and its history untouched.
	data = fixture();
	setLevelMapSelection(&data.map, {brush(0)});
	reference = data.map;
	request = {};
	request.axis = {1, 1, 0, true};
	request.degrees = 30;
	request.textures = {true, true};
	expect(rotateLevelMapObjects(&data.map, request, &report, &error) && report.worldAxis == -1 && data.map.undoStack.size() == 1, "any-axis turn", error);
	LevelMapRotationRequest numeric;
	numeric.euler = true;
	numeric.eulerDegrees = report.eulerXyz;
	numeric.pivot = report.pivot;
	numeric.textureLock = true;
	numeric.allowValve220 = true;
	expect(rotateLevelMapSelection(&reference, numeric, &error) && serializeLevelMap(reference).bytes == serializeLevelMap(data.map).bytes,
		"the any-axis turn is Arbitrary Rotation's Euler turn", error);
	data = fixture();
	setLevelMapSelection(&data.map, {brush(0)});
	const auto classic = serializeLevelMap(data.map).bytes;
	request.textures = {true, false};
	// Classic faces cannot hold an any-axis turn under Texture Lock without
	// Valve 220 conversion: the refusal leaves the map and its history alone.
	expect(!rotateLevelMapObjects(&data.map, request, &report, &error) && !error.isEmpty() && serializeLevelMap(data.map).bytes == classic
			&& data.map.undoStack.isEmpty(),
		"a texture-lock refusal changes nothing", error);

	// Refusals: hidden, locked, outside the open group, Doom.
	data = fixture();
	setLevelMapSelection(&data.map, {brush(0), brush(1)});
	data.map.hiddenObjects = {QStringLiteral("brush:1")};
	const auto hidden = serializeLevelMap(data.map).bytes;
	request = {};
	request.degrees = 90;
	expect(!rotateLevelMapObjects(&data.map, request, &report, &error) && !error.isEmpty() && serializeLevelMap(data.map).bytes == hidden
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
	expect(!rotateLevelMapObjects(&data.map, request, &report, &error) && serializeLevelMap(data.map).bytes == locked && data.map.undoStack.size() == lockedDepth,
		"a locked object is refused untouched", error);
	data = fixture();
	QString group;
	expect(createLevelSceneNode(&data.map, LevelSceneNodeKind::Group, QStringLiteral("Room"), {}, &group, &error)
			&& assignLevelSceneObjects(&data.map, group, {QStringLiteral("brush:0")}, &error) && openLevelSceneGroup(&data.map, group, &error),
		"group fixture", error);
	data.map.selection = {brush(0), brush(1)};
	expect(!rotateLevelMapObjects(&data.map, request, &report, &error), "an object outside the open group is refused", error);
	LevelMapDocument doom;
	LevelMapCreateRequest doomRequest;
	doomRequest.game = QStringLiteral("doom");
	expect(createLevelMap(doomRequest, &doom, &error), "Doom fixture", error);
	expect(!rotateLevelMapObjects(&doom, request, &report, &error), "Doom maps are refused");
}

void edges()
{
	auto data = fixture();
	bool limited = true;
	auto wire = levelObjectManipulatorEdges(data.map, {brush(0)}, 16384, &limited);
	expect(wire.size() == 24 && !limited, "a box brush: four edges on each of six faces", QString::number(wire.size()));
	wire = levelObjectManipulatorEdges(data.map, {{LevelMapSelectionKind::QuakePatch, data.patch}}, 16384, &limited);
	expect(wire.size() == 12, "a 3x3 patch's control net", QString::number(wire.size()));
	wire = levelObjectManipulatorEdges(data.map, {{LevelMapSelectionKind::Entity, data.entity}}, 16384, &limited);
	expect(wire.size() == 12 && at(wire.first().first, -48, 0, 16), "a point entity's 16-unit box", QString::number(wire.size()));
	wire = levelObjectManipulatorEdges(data.map, {brush(0), brush(1)}, 30, &limited);
	expect(wire.size() == 12 && limited, "past the limit the bounds stand in", QString::number(wire.size()));
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
	const QString path = temp.filePath("rotate.map");
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
	// A world axis with angle snapping: the CLI writes what the core writes.
	const QString output = temp.filePath("world.map");
	auto report = run({"rotate", path, "--object", "brush:0", "--object", "brush:1", "--around", "0,0,1", "--degrees", "100", "--angle-step", "45", "--output", output});
	auto expected = source;
	setLevelMapSelection(&expected, {brush(0), brush(1)});
	LevelObjectRotateRequest request;
	request.degrees = 100;
	request.angleStep = 45;
	LevelObjectRotateReport core;
	expect(rotateLevelMapObjects(&expected, request, &core, &error), "core reference", error);
	expect(report["objects"].toInt() == 2 && report["worldAxis"].toString() == QStringLiteral("z") && report["degrees"].toDouble() == 90
			&& report["pivot"].toObject()["x"].toDouble() == 32 && report["textureLockPolicy"].toString() == QStringLiteral("locked") && !report.contains(QStringLiteral("eulerXyz")),
		"CLI report matches the core", QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));
	expect(serializeLevelMap(load(output)).bytes == serializeLevelMap(expected).bytes, "CLI writes what the core writes");
	// Any axis with Valve 220 conversion, and a patch with a placed pivot.
	report = run({"rotate", path, "--object", "brush:0", "--object", QStringLiteral("patch:%1").arg(data.patch), "--around", "1,1,0", "--degrees", "30",
		"--pivot", "0,0,0", "--allow-valve220", "--output", temp.filePath("any.map")});
	expected = source;
	setLevelMapSelection(&expected, {brush(0), {LevelMapSelectionKind::QuakePatch, data.patch}});
	request = {};
	request.axis = {1, 1, 0, true};
	request.degrees = 30;
	request.pivot = {0, 0, 0, true};
	request.textures = {true, true};
	expect(rotateLevelMapObjects(&expected, request, &core, &error), "core any-axis reference", error);
	expect(report["eulerXyz"].toArray().size() == 3 && !report.contains(QStringLiteral("worldAxis")) && report["allowValve220"].toBool(), "CLI any-axis report",
		QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));
	expect(serializeLevelMap(load(temp.filePath("any.map"))).bytes == serializeLevelMap(expected).bytes, "CLI any-axis turn equals the core");
	// --axis with --angle-step, and the plain map rotate keeps its route.
	report = run({"rotate", path, "--object", "brush:1", "--axis", "x", "--degrees", "100", "--angle-step", "15", "--texture-lock", "off", "--output",
		temp.filePath("axis.map")});
	expect(report["degrees"].toDouble() == 105 && report["worldAxis"].toString() == QStringLiteral("x")
			&& report["textureLockPolicy"].toString() == QStringLiteral("source-parameters"),
		"--axis with --angle-step");
	report = run({"rotate", path, "--object", "brush:0", "--axis", "z", "--degrees", "90", "--output", temp.filePath("plain.map")});
	expect(!report.contains(QStringLiteral("placement")) && report.contains(QStringLiteral("degrees")), "the plain map rotate keeps its route");
	// Refusals and usage errors write nothing.
	run({"rotate", path, "--object", "brush:0", "--object", QStringLiteral("entity:%1").arg(data.entity), "--around", "0,0,1", "--degrees", "90", "--output",
		temp.filePath("locked.map")}, 4);
	expect(!QFileInfo::exists(temp.filePath("locked.map")), "a locked object writes nothing");
	run({"rotate", path, "--object", "brush:0", "--around", "0,0,0", "--degrees", "90", "--output", temp.filePath("zero.map")}, 4);
	run({"rotate", path, "--object", "brush:0", "--around", "0,0,1", "--degrees", "7", "--angle-step", "15", "--output", temp.filePath("zero.map")}, 4);
	expect(!QFileInfo::exists(temp.filePath("zero.map")), "a zero axis or a turn that snaps away writes nothing");
	for (const auto& bad : QVector<QStringList> {
			 {"--object", "brush:0", "--around", "0,0,1", "--axis", "z", "--degrees", "30"},
			 {"--object", "brush:0", "--around", "0,1", "--degrees", "30"},
			 {"--object", "brush:0", "--around", "0,0,1"},
			 {"--object", "brush:0", "--around", "0,0,1", "--degrees", "30", "--angle-step", "400"},
			 {"--object", "brush:0", "--around", "0,0,1", "--degrees", "30", "--turns", "1"},
			 {"--object", "brush:0", "--around", "0,0,1", "--degrees", "30", "--texture-lock", "maybe"},
			 {"--around", "0,0,1", "--degrees", "30"},
		 }) {
		run(QStringList {"rotate", path} + bad + QStringList {"--output", temp.filePath("bad.map")}, 2);
	}
	expect(!QFileInfo::exists(temp.filePath("bad.map")), "usage errors write nothing");
	// map move with several objects but neither --delta nor --to names them.
	QString message;
	run({"move", path, "--object", "brush:0", "--object", "brush:1", "--output", temp.filePath("bad.map")}, 2, &message);
	expect(message.contains(QStringLiteral("--delta")) && !message.contains(QStringLiteral("not both")), "map move names the missing option", message);
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	maths();
	requests();
	rotate();
	edges();
	if (argc == 2) { cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath()); }
	return ok ? 0 : 1;
}
