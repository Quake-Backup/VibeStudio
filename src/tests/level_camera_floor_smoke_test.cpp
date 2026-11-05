// Camera Floor Up and Down (core/level_camera_focus.h): standing heights 32
// units over the tops of the visible brushes, patches and point entities
// whose boxes hold the camera's x and y strictly inside or that are selected,
// the rise to the lowest underside from below everything and the fall back
// to it, the 0.1-unit dead band, hidden and filtered objects left out,
// selected brush entities counting through their brushes, definition boxes
// for point entities, and on Doom maps the sector eye height with its
// ceiling gap. The profile keys (Page Up and Page Down in the Radiant
// profiles, none in TrenchBroom) and `map camera-focus --floor` over the same
// service are checked too; the CLI part runs with the vibestudio binary as
// the first argument.

#include "core/editor_profiles.h"
#include "core/level_camera_focus.h"
#include "core/level_editor_controls.h"
#include "core/level_map.h"
#include "core/level_visibility.h"
#include "core/studio_semantics.h"
#include "tests/doom_preview_test_helpers.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <array>
#include <cmath>
#include <iostream>
#include <tuple>
#include <utility>

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

bool nearly(double a, double b)
{
	return std::abs(a - b) < 1.0e-9;
}

QString box(std::array<double, 3> lo, std::array<double, 3> hi)
{
	QStringList lines {QStringLiteral("{")};
	const auto point = [](const std::array<double, 3>& v) {
		return QStringLiteral("( %1 %2 %3 )").arg(v[0], 0, 'g', 10).arg(v[1], 0, 'g', 10).arg(v[2], 0, 'g', 10);
	};
	for (int axis = 0; axis < 3; ++axis) {
		for (const bool positive : {false, true}) {
			std::array<double, 3> p = lo;
			p[axis] = positive ? hi[axis] : lo[axis];
			auto a = p;
			auto b = p;
			int first = (axis + 1) % 3;
			int second = (axis + 2) % 3;
			if (positive) {
				std::swap(first, second);
			}
			a[first] += 1;
			b[second] += 1;
			lines << QStringLiteral("%1 %2 %3 base/wall 0 0 0 0.5 0.5 0 0 0").arg(point(p), point(a), point(b));
		}
	}
	lines << QStringLiteral("}");
	return lines.join(QLatin1Char('\n'));
}

// Standing heights over (128, 128): brush:0 slab top 0 (32, underside -32),
// patch:0 flat at 100 (132, underside 84), brush:1 platform top 144 (176),
// brush:2 ceiling slab top 272 (304), the light at 400 with a 16-unit cube
// (440, or 480 with a 64-unit-tall definition box). brush:3 lies elsewhere
// (top 564, 596) and brush:4, a func_door's, elsewhere too (top 700, 732).
QByteArray fixture()
{
	const QString patch = QStringLiteral(
		"{\npatchDef2\n{\nbase/floor\n( 3 3 0 0 0 )\n(\n"
		"( ( 96 96 100 0 0 ) ( 96 128 100 0 0.5 ) ( 96 160 100 0 1 ) )\n"
		"( ( 128 96 100 0.5 0 ) ( 128 128 100 0.5 0.5 ) ( 128 160 100 0.5 1 ) )\n"
		"( ( 160 96 100 1 0 ) ( 160 128 100 1 0.5 ) ( 160 160 100 1 1 ) )\n"
		")\n}\n}");
	return QStringLiteral("{\n\"classname\" \"worldspawn\"\n%1\n%2\n%3\n%4\n%5\n}\n{\n\"classname\" \"light\"\n\"origin\" \"128 128 400\"\n}\n"
						  "{\n\"classname\" \"func_door\"\n%6\n}\n")
		.arg(box({0, 0, -16}, {256, 256, 0}), patch, box({64, 64, 128}, {192, 192, 144}), box({0, 0, 256}, {256, 256, 272}),
			box({1000, 0, 500}, {1064, 64, 564}), box({2000, 0, 600}, {2064, 64, 700}))
		.toUtf8();
}

LevelCameraFloorStep step(const LevelMapDocument& map, double x, double y, double z, LevelCameraFloorDirection direction,
	const QSet<QString>& excluded = {}, const QHash<QString, ResizeBox>& bounds = {})
{
	LevelCameraFloorRequest request;
	request.position = {x, y, z, true};
	request.direction = direction;
	request.excludedObjects = excluded;
	request.entityBounds = bounds;
	return levelCameraFloorStep(map, request);
}

QString describe(const LevelCameraFloorStep& result)
{
	return QStringLiteral("moved %1 z %2 source %3 object %4 candidates %5 %6")
		.arg(result.moved)
		.arg(result.position.z)
		.arg(levelCameraFloorSourceId(result.source), levelMapSelectionRefId(result.object))
		.arg(result.candidates)
		.arg(result.error);
}

bool lands(const LevelCameraFloorStep& result, double height, LevelCameraFloorSource source, const QString& object)
{
	return result.valid && result.moved && std::abs(result.position.z - height) < 1.0e-9 && result.source == source
		&& levelMapSelectionRefId(result.object) == object;
}

constexpr auto Up = LevelCameraFloorDirection::Up;
constexpr auto Down = LevelCameraFloorDirection::Down;

void quake()
{
	LevelMapDocument map;
	QString error;
	if (!expect(loadLevelMapBytes({QStringLiteral("floors.map"), {}, QStringLiteral("idtech3")}, fixture(), &map, &error), "fixture", error)) {
		return;
	}
	expect(map.brushes.size() == 5 && map.patches.size() == 1, "fixture objects");
	const auto standing = LevelCameraFloorSource::StandingHeight;
	const auto underside = LevelCameraFloorSource::Underside;
	// Up through every floor over the camera, then nowhere higher.
	LevelCameraFloorStep result = step(map, 128, 128, 64, Up);
	expect(lands(result, 132, standing, QStringLiteral("patch:0")) && result.candidates == 5 && nearly(result.position.x, 128),
		"up from 64 stands on the patch", describe(result));
	expect(lands(step(map, 128, 128, 132, Up), 176, standing, QStringLiteral("brush:1")), "up to the platform");
	expect(lands(step(map, 128, 128, 176, Up), 304, standing, QStringLiteral("brush:2")), "up over the ceiling slab");
	expect(lands(step(map, 128, 128, 304, Up), 440, standing, QStringLiteral("entity:1")), "up over the light's 16-unit cube");
	result = step(map, 128, 128, 440, Up);
	expect(result.valid && !result.moved && result.source == LevelCameraFloorSource::None && result.position.z == 440 && result.candidates == 5,
		"nothing higher: the camera stays", describe(result));
	// Down to the slab, then to its underside, then nowhere lower.
	expect(lands(step(map, 128, 128, 64, Down), 32, standing, QStringLiteral("brush:0")), "down from 64 to the slab");
	expect(lands(step(map, 128, 128, 32, Down), -32, underside, QStringLiteral("brush:0")), "down from the lowest floor goes under it");
	result = step(map, 128, 128, -32, Down);
	expect(result.valid && !result.moved, "nothing lower than the underside", describe(result));
	// Below everything, Up first rises to the lowest underside.
	expect(lands(step(map, 128, 128, -500, Up), -32, underside, QStringLiteral("brush:0")), "up from below everything");
	expect(lands(step(map, 128, 128, -32, Up), 32, standing, QStringLiteral("brush:0")), "up from the underside to the slab");
	// The 0.1-unit dead band: a hair under a floor still steps past it.
	expect(lands(step(map, 128, 128, 131.95, Up), 176, standing, QStringLiteral("brush:1")), "within 0.1 of a floor counts as on it");
	expect(lands(step(map, 128, 128, 131.85, Up), 132, standing, QStringLiteral("patch:0")), "more than 0.1 below a floor rises to it");
	// Strictly inside: on the slab's edge nothing is under the camera.
	result = step(map, 0, 128, 64, Up);
	expect(result.valid && !result.moved && result.candidates == 0, "a box edge does not hold the camera", describe(result));
	// Filtered and hidden objects never count.
	expect(lands(step(map, 128, 128, 64, Up, {QStringLiteral("patch:0"), QStringLiteral("brush:1")}), 304, standing, QStringLiteral("brush:2")),
		"filtered objects are left out");
	{
		LevelMapDocument hidden = map;
		QString failure;
		expect(setLevelMapSelection(&hidden, {{LevelMapSelectionKind::QuakeBrush, 2}}, &failure)
				&& changeLevelMapVisibility(&hidden, {LevelVisibilityOperation::HideSelected}, nullptr, &failure)
				&& setLevelMapSelection(&hidden, {}, &failure),
			"hide brush:2", failure);
		expect(lands(step(hidden, 128, 128, 176, Up), 440, standing, QStringLiteral("entity:1")), "a hidden object is left out");
	}
	// Point entities take their definition box.
	expect(lands(step(map, 128, 128, 304, Up, {}, {{QStringLiteral("light"), ResizeBox {{-16, -16, -16}, {16, 16, 48}}}}), 480, standing,
			   QStringLiteral("entity:1")),
		"a definition box sizes the light");
	// Selected objects count wherever they are; a selected brush entity
	// counts through its brushes.
	{
		LevelMapDocument selected = map;
		QString failure;
		expect(setLevelMapSelection(&selected, {{LevelMapSelectionKind::QuakeBrush, 3}}, &failure), "select brush:3", failure);
		expect(lands(step(selected, 128, 128, 440, Up), 596, standing, QStringLiteral("brush:3")), "a selected brush elsewhere counts");
		int door = -1;
		for (const LevelMapEntity& entity : selected.entities) {
			if (entity.className == QLatin1String("func_door")) {
				door = entity.id;
			}
		}
		expect(setLevelMapSelection(&selected, {{LevelMapSelectionKind::Entity, door}}, &failure), "select the door", failure);
		expect(lands(step(selected, 128, 128, 440, Up), 732, standing, QStringLiteral("brush:4")), "a selected brush entity counts through its brushes");
	}
	result = step(map, std::nan(""), 0, 0, Up);
	expect(!result.valid && !result.error.isEmpty(), "a camera that is not finite is refused");
	LevelCameraFloorDirection direction = Up;
	expect(levelCameraFloorDirectionFromId(QStringLiteral(" Down "), &direction) && direction == Down
			&& !levelCameraFloorDirectionFromId(QStringLiteral("sideways"), &direction) && levelCameraFloorDirectionId(Up) == QLatin1String("up"),
		"direction ids");
}

void doom()
{
	// Three one-sided rooms: floor 0 / ceiling 128, floor 64 / ceiling 96 (the
	// eye stops 4 under the ceiling) and a shut sector at 32.
	LevelMapDocument map = tests::doom::topology();
	LevelMapDoomSector low = map.doomSectors.first();
	low.id = 1;
	low.floorHeight = 64;
	low.ceilingHeight = 96;
	LevelMapDoomSector shut = low;
	shut.id = 2;
	shut.floorHeight = 32;
	shut.ceilingHeight = 32;
	map.doomSectors << low << shut;
	tests::doom::loop(map, {{0, 0}, {256, 0}, {256, 256}, {0, 256}}, 0);
	tests::doom::loop(map, {{256, 0}, {512, 0}, {512, 256}, {256, 256}}, 1);
	tests::doom::loop(map, {{512, 0}, {768, 0}, {768, 256}, {512, 256}}, 2);
	const auto eye = LevelCameraFloorSource::SectorEyeHeight;
	LevelCameraFloorStep result = step(map, 128, 128, 0, Up);
	expect(lands(result, 41, eye, QStringLiteral("sector:0")) && result.candidates == 1, "Doom eye height over the floor", describe(result));
	result = step(map, 128, 128, 41, Up);
	expect(result.valid && !result.moved, "nothing else under the camera", describe(result));
	expect(lands(step(map, 384, 128, 0, Up), 92, eye, QStringLiteral("sector:1")), "the eye stays 4 units under a low ceiling");
	expect(lands(step(map, 640, 128, 100, Down), 32, eye, QStringLiteral("sector:2")), "a shut sector's eye is its floor");
	QString failure;
	expect(setLevelMapSelection(&map, {{LevelMapSelectionKind::DoomSector, 1}}, &failure), "select sector:1", failure);
	expect(lands(step(map, 128, 128, 41, Up), 92, eye, QStringLiteral("sector:1")), "a selected sector counts from anywhere");
	expect(lands(step(map, 128, 128, 200, Down), 92, eye, QStringLiteral("sector:1")), "down to the selected sector's eye");
	expect(lands(step(map, 128, 128, 92, Down), 41, eye, QStringLiteral("sector:0")), "then to the room's");
	result = step(map, 128, 128, 41, Down);
	expect(result.valid && !result.moved && result.source == LevelCameraFloorSource::None, "Doom has no underside", describe(result));
	result = step(map, 128, 128, 41, Up, {QStringLiteral("sector:1")});
	expect(result.valid && !result.moved, "a filtered sector is left out", describe(result));
}

void profiles()
{
	const auto keysOf = [](const char* profile, const char* command) {
		for (const LevelEditorKeyBinding& binding : levelEditorControlsForProfile(QString::fromLatin1(profile)).keys) {
			if (binding.commandId == QLatin1String(command)) {
				return binding.keys;
			}
		}
		return QStringList();
	};
	for (const char* profile : {"netradiant-custom", "netradiant", "gtkradiant-1-6", "gtkradiant-1-5", "gtkradiant-1-4", "q3radiant", "doomedit", "qeradiant"}) {
		expect(keysOf(profile, "map.cameraFloorUp") == QStringList {QStringLiteral("PgUp")}
				&& keysOf(profile, "map.cameraFloorDown") == QStringList {QStringLiteral("PgDown")},
			"Page Up and Page Down step floors in the Radiant profiles", QString::fromLatin1(profile));
	}
	for (const char* profile : {"trenchbroom", "vibestudio-default", "hammer", "darkradiant"}) {
		expect(keysOf(profile, "map.cameraFloorUp").isEmpty() && keysOf(profile, "map.cameraFloorDown").isEmpty(), "no floor keys",
			QString::fromLatin1(profile));
	}
	for (const EditorProfileDescriptor& profile : editorProfileDescriptors()) {
		QString conflict;
		expect(!editorProfileHasShortcutConflict(profile, &conflict) && levelEditorControlProblems(profile.controls).isEmpty(), "profiles stay consistent",
			profile.id + QLatin1String(": ") + conflict + levelEditorControlProblems(profile.controls).join(QLatin1Char(' ')));
	}
	for (const char* id : {"map.camera-floor-up", "map.camera-floor-down"}) {
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(QString::fromLatin1(id), &entry) && entry.summary.contains(QStringLiteral("floor")), "palette entry",
			QString::fromLatin1(id));
	}
}

void cli(const QString& executable)
{
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "CLI temporary directory")) {
		return;
	}
	const QString path = temp.filePath(QStringLiteral("floors.map"));
	const QByteArray source = fixture();
	QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(), "CLI fixture")) {
		return;
	}
	file.close();
	const QString wadPath = temp.filePath(QStringLiteral("room.wad"));
	const QByteArray wad = tests::doom::fixture();
	expect(!wad.isEmpty() && tests::doom::write(wadPath, wad), "Doom fixture");
	const QString settingsPath = temp.filePath(QStringLiteral("settings.ini"));
	const auto run = [&](const QStringList& args, int code = 0, bool json = true) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		QStringList command {QStringLiteral("--cli")};
		if (json) {
			command << QStringLiteral("--json");
		}
		command << QStringLiteral("--settings-file") << settingsPath << QStringLiteral("map") << QStringLiteral("camera-focus");
		process.start(executable, command + args);
		const bool done = process.waitForStarted() && process.waitForFinished(30000);
		const QByteArray out = process.readAllStandardOutput();
		const QByteArray err = process.readAllStandardError();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code, "CLI exit status",
			args.join(QLatin1Char(' ')) + QLatin1String(" -> ") + QString::fromUtf8(out + err));
		if (!done) {
			process.kill();
			process.waitForFinished();
		}
		return std::make_pair(QJsonDocument::fromJson(out).object(), QString::fromUtf8(out + err));
	};
	const auto height = [](const QJsonObject& report) {
		return report.value(QStringLiteral("camera")).toObject().value(QStringLiteral("position")).toArray().at(2).toDouble();
	};
	auto [report, text] = run({path, QStringLiteral("--floor"), QStringLiteral("up"), QStringLiteral("--camera-position"), QStringLiteral("128,128,64")});
	QJsonObject floor = report.value(QStringLiteral("floor")).toObject();
	expect(report.value(QStringLiteral("queryOnly")) == true && report.value(QStringLiteral("mapWritten")) == false && height(report) == 132
			&& floor.value(QStringLiteral("object")) == QLatin1String("patch:0") && floor.value(QStringLiteral("source")) == QLatin1String("standing-height")
			&& floor.value(QStringLiteral("moved")) == true && floor.value(QStringLiteral("direction")) == QLatin1String("up")
			&& floor.value(QStringLiteral("candidates")).toInt() == 5,
		"--floor up stands on the patch", text);
	std::tie(report, text) = run({path, QStringLiteral("--floor"), QStringLiteral("down"), QStringLiteral("--camera-position"), QStringLiteral("128,128,32")});
	expect(height(report) == -32 && report.value(QStringLiteral("floor")).toObject().value(QStringLiteral("source")) == QLatin1String("underside"),
		"--floor down goes under the lowest floor", text);
	// Named objects count as the selection; the built-in Quake III light has
	// no size, so the 16-unit cube applies.
	std::tie(report, text) = run({path, QStringLiteral("--floor"), QStringLiteral("up"), QStringLiteral("--object"), QStringLiteral("brush:3"),
		QStringLiteral("--camera-position"), QStringLiteral("128,128,440")});
	expect(height(report) == 596 && report.contains(QStringLiteral("focus")), "a named object counts as selected after the focus", text);
	// --selected counts as the selection without focusing the camera on it,
	// so the step starts where --camera-position puts the camera, as with a
	// selection elsewhere in the Levels views.
	std::tie(report, text) = run({path, QStringLiteral("--floor"), QStringLiteral("up"), QStringLiteral("--selected"), QStringLiteral("brush:3"),
		QStringLiteral("--camera-position"), QStringLiteral("128,128,440")});
	{
		const QJsonArray position = report.value(QStringLiteral("camera")).toObject().value(QStringLiteral("position")).toArray();
		const QJsonObject stepped = report.value(QStringLiteral("floor")).toObject();
		expect(height(report) == 596 && !report.contains(QStringLiteral("focus")) && position.at(0).toDouble() == 128 && position.at(1).toDouble() == 128
				&& stepped.value(QStringLiteral("object")) == QLatin1String("brush:3")
				&& stepped.value(QStringLiteral("selection")).toArray() == QJsonArray {QStringLiteral("brush:3")},
			"--selected counts as selected and leaves the camera where it stands", text);
	}
	std::tie(report, text) = run({path, QStringLiteral("--floor"), QStringLiteral("up"), QStringLiteral("--selected"), QStringLiteral("brush:3"),
		QStringLiteral("--selected"), QStringLiteral("brush:3"), QStringLiteral("--camera-position"), QStringLiteral("128,128,64")});
	expect(height(report) == 132 && report.value(QStringLiteral("floor")).toObject().value(QStringLiteral("selection")).toArray().size() == 1,
		"a repeated --selected counts once, and a selection elsewhere does not block a nearer floor", text);
	std::tie(report, text) = run({path, QStringLiteral("--floor"), QStringLiteral("up"), QStringLiteral("--camera-position"), QStringLiteral("128,128,440")});
	expect(height(report) == 440 && report.value(QStringLiteral("floor")).toObject().value(QStringLiteral("moved")) == false, "nothing higher", text);
	std::tie(report, text) = run({path, QStringLiteral("--floor"), QStringLiteral("up"), QStringLiteral("--camera-position"), QStringLiteral("128,128,64")}, 0, false);
	expect(text.contains(QStringLiteral("not changed")) && text.contains(QStringLiteral("patch:0")) && text.contains(QStringLiteral("Camera at")), "CLI text report",
		text);
	// Doom: the room's floor plus the 41-unit eye height.
	LevelMapDocument room;
	QString error;
	if (expect(loadLevelMapBytes({QStringLiteral("room.wad"), QStringLiteral("MAP01"), {}}, wad, &room, &error) && !room.doomSectors.isEmpty()
				&& !room.doomVertices.isEmpty(),
			"Doom room", error)) {
		double x = 0;
		double y = 0;
		for (const LevelMapDoomVertex& vertex : room.doomVertices) {
			x += vertex.x / room.doomVertices.size();
			y += vertex.y / room.doomVertices.size();
		}
		const LevelMapDoomSector& sector = room.doomSectors.first();
		const double eye = std::max(sector.floorHeight, std::min(sector.floorHeight + 41.0, sector.ceilingHeight - 4.0));
		std::tie(report, text) = run({wadPath, QStringLiteral("--map"), QStringLiteral("MAP01"), QStringLiteral("--floor"), QStringLiteral("up"),
			QStringLiteral("--camera-position"), QStringLiteral("%1,%2,%3").arg(x).arg(y).arg(sector.floorHeight - 100)});
		expect(std::abs(height(report) - eye) < 1.0e-9
				&& report.value(QStringLiteral("floor")).toObject().value(QStringLiteral("source")) == QLatin1String("sector-eye-height"),
			"--floor up on a Doom map", text);
	}
	for (const QStringList& bad : QVector<QStringList> {{path, QStringLiteral("--floor"), QStringLiteral("sideways")},
			 {path, QStringLiteral("--floor")}, {path, QStringLiteral("--level"), QStringLiteral("--definitions"), QStringLiteral("x.def")},
			 {path, QStringLiteral("--floor"), QStringLiteral("up"), QStringLiteral("--floor"), QStringLiteral("down")},
			 {path, QStringLiteral("--level"), QStringLiteral("--selected"), QStringLiteral("brush:3")},
			 {path, QStringLiteral("--floor"), QStringLiteral("up"), QStringLiteral("--selected"), QStringLiteral("brush:99")}}) {
		run(bad, 2);
	}
	run({path, QStringLiteral("--floor"), QStringLiteral("up"), QStringLiteral("--definitions"), temp.filePath(QStringLiteral("missing.def"))}, 4);
	expect(file.open(QIODevice::ReadOnly) && file.readAll() == source, "the CLI never changes the map");
	file.close();
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	quake();
	doom();
	profiles();
	if (argc == 2) {
		cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath());
	}
	if (ok) {
		std::cout << "Level camera floor: standing heights, undersides, hidden and selected objects, Doom eye height, keys" << (argc == 2 ? " and CLI" : "")
				  << " passed." << std::endl;
	}
	return ok ? 0 : 1;
}
