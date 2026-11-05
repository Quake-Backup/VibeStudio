// Map Info and the Map tab inspector's services: NetRadiant Custom's class
// table with group and in-game counts in levelMapStatistics, TrenchBroom's
// soft bounds and mods as worldspawn keys (one undo step each, exact undo and
// redo, refusals, no step when unchanged), the layer summary, and the
// `map inspect --json` and `map properties` routes.
#include "core/level_document.h"
#include "core/level_map.h"
#include "core/level_map_inspector.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_map_info_test_fixture.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;

namespace {
bool ok = true;

bool expect(bool pass, const char* label, const QString& detail = {})
{
	if (!pass) {
		ok = false;
		std::cerr << "FAIL: " << label << ": " << qPrintable(detail) << std::endl;
	}
	return pass;
}

LevelMapDocument fixture()
{
	LevelMapDocument document;
	QString error;
	expect(loadLevelMapBytes({QStringLiteral("map-info.map"), {}, {}}, tests::mapInfoFixture(), &document, &error), "fixture loads", error);
	return document;
}

QString worldValue(const LevelMapDocument& document, const QString& key, bool* present = nullptr)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.className == QLatin1String("worldspawn")) {
			for (const LevelMapProperty& property : entity.properties) {
				if (property.key == key) {
					if (present) {
						*present = true;
					}
					return property.value;
				}
			}
		}
	}
	if (present) {
		*present = false;
	}
	return {};
}

const LevelMapEntityClassCount* classRow(const LevelMapStatistics& statistics, const QString& name)
{
	for (const LevelMapEntityClassCount& row : statistics.entityClasses) {
		if (row.className == name) {
			return &row;
		}
	}
	return nullptr;
}

int entityId(const LevelMapDocument& document, const QString& className)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.className == className) {
			return entity.id;
		}
	}
	return -1;
}

void statisticsTests()
{
	const LevelMapDocument document = fixture();
	expect(document.format == LevelMapFormat::QuakeMap && document.brushes.size() == 4 && document.entities.size() == 10, "fixture shape",
		QString::number(document.entities.size()));
	const LevelMapStatistics statistics = levelMapStatistics(document);
	expect(statistics.entityCount == 10 && statistics.groupEntityCount == 4 && statistics.inGameGroupEntityCount == 2 && statistics.inGameEntityCount == 4,
		"group and in-game totals",
		QStringLiteral("%1 %2 %3").arg(statistics.groupEntityCount).arg(statistics.inGameGroupEntityCount).arg(statistics.inGameEntityCount));
	QStringList names;
	for (const LevelMapEntityClassCount& row : statistics.entityClasses) {
		names << row.className;
	}
	expect(names
			== QStringList {"func_detail", "func_door", "func_group", "info_null", "info_player_start", "item_shells", "light", "misc_model", "worldspawn"},
		"classes sorted by name", names.join(' '));
	const auto* light = classRow(statistics, QStringLiteral("light"));
	const auto* door = classRow(statistics, QStringLiteral("func_door"));
	const auto* detail = classRow(statistics, QStringLiteral("func_detail"));
	const auto* world = classRow(statistics, QStringLiteral("worldspawn"));
	expect(light && light->count == 2 && light->groupCount == 0 && light->inGameCount == 0, "lights are dropped");
	expect(door && door->count == 1 && door->groupCount == 1 && door->inGameCount == 1, "a door is an in-game group entity");
	expect(detail && detail->groupCount == 1 && detail->inGameCount == 0, "func_detail merges into the world");
	expect(world && world->groupCount == 1 && world->inGameCount == 1, "worldspawn is an in-game group entity, as NetRadiant Custom counts it");

	// NetRadiant Custom's rule, any case.
	expect(!levelMapEntityReachesGame(QStringLiteral("light_torch_small_walltorch"), false) && !levelMapEntityReachesGame(QStringLiteral("LIGHT"), false)
			&& !levelMapEntityReachesGame(QStringLiteral("misc_model"), false) && !levelMapEntityReachesGame(QStringLiteral("info_null"), false)
			&& levelMapEntityReachesGame(QStringLiteral("weapon_supershotgun"), false) && levelMapEntityReachesGame(QStringLiteral("func_door"), true)
			&& !levelMapEntityReachesGame(QStringLiteral("func_detail_wall"), true) && !levelMapEntityReachesGame(QStringLiteral("_decal"), true)
			&& !levelMapEntityReachesGame(QStringLiteral("FUNC_GROUP"), true),
		"in-game rule");

	const QStringList lines = levelMapStatisticsLines(document, statistics);
	expect(lines.contains(QStringLiteral("In-game entities: 4")) && lines.contains(QStringLiteral("Group entities: 4 (2 in game)"))
			&& lines.contains(QStringLiteral("Entity classes: 9")),
		"statistics lines", lines.join('\n'));
	const QStringList classLines = levelMapEntityClassLines(document, statistics);
	expect(classLines.contains(QStringLiteral("light: 2 (0 in game)")) && classLines.size() == 9, "class lines", classLines.join('\n'));
	expect(levelMapReportText(document).contains(QStringLiteral("Entity classes:\n- func_detail: 1 (0 in game)")), "report lists the classes");

	// Doom maps count thing types, every thing in the game.
	LevelMapDocument doom;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("doom");
	QString error;
	if (expect(createLevelMap(create, &doom, &error), "Doom map", error)) {
		expect(addLevelMapDoomThing(&doom, 3001, 64, 64, 0, nullptr, &error) && addLevelMapDoomThing(&doom, 3001, 96, 64, 0, nullptr, &error), "imps", error);
		const LevelMapStatistics things = levelMapStatistics(doom);
		const auto* imps = classRow(things, QStringLiteral("thing:3001"));
		expect(imps && imps->count == 2 && imps->inGameCount == 2 && things.groupEntityCount == 0 && things.inGameEntityCount == things.entityCount,
			"Doom thing types", QString::number(things.entityCount));
	}
}

void softBoundsTests()
{
	LevelMapDocument document = fixture();
	QString error;
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	expect(parseLevelMapSoftBounds(QStringLiteral("-8 -8 -8 8 8 8"), &mins, &maxs) && mins.x == -8 && maxs.z == 8, "six numbers");
	expect(parseLevelMapSoftBounds(QStringLiteral(" 1,2,3, 4,5,6 "), &mins, &maxs) && mins.y == 2 && maxs.x == 4, "commas");
	expect(!parseLevelMapSoftBounds(QStringLiteral("1 2 3"), &mins, &maxs) && !parseLevelMapSoftBounds(QStringLiteral("1 2 3 0 5 6"), &mins, &maxs)
			&& !parseLevelMapSoftBounds(QStringLiteral("a b c d e f"), &mins, &maxs),
		"bad boxes refused");
	LevelMapVec3 corner;
	expect(parseLevelMapSoftBoundsCorner(QStringLiteral("64"), &corner) && corner.x == 64 && corner.z == 64, "one number for all three");
	expect(parseLevelMapSoftBoundsCorner(QStringLiteral("1 2 3"), &corner) && corner.y == 2 && !parseLevelMapSoftBoundsCorner(QStringLiteral("1 2"), &corner),
		"corners");
	expect(levelMapSoftBoundsCornerText({64, 64, 64, true}) == QStringLiteral("64") && levelMapSoftBoundsCornerText({1, 2.5, 3, true}) == QStringLiteral("1 2.5 3"),
		"corner text");

	// The game's bounds by format.
	LevelMapSoftBounds bounds = levelMapSoftBounds(document);
	expect(bounds.mode == LevelMapSoftBoundsMode::Game && bounds.valid && bounds.maxs.x == 4096 && bounds.mins.z == -4096, "Quake takes ±4096");
	LevelMapDocument quake3;
	expect(loadLevelMapBytes({QStringLiteral("q3.map"), {}, QStringLiteral("idTech3")}, tests::mapInfoFixture(), &quake3, &error)
			&& levelMapGameSoftBounds(quake3).maxs.y == 65536,
		"Quake III takes ±65536", error);
	QStringList outside = levelMapObjectsOutsideBounds(document, bounds.mins, bounds.maxs);
	const QString nullSelector = QStringLiteral("entity:%1").arg(entityId(document, QStringLiteral("info_null")));
	expect(outside.size() == 2 && outside.contains(nullSelector) && outside.contains(QStringLiteral("brush:3")), "objects outside", outside.join(' '));

	// Custom: one undo step named after the setting; the selection stays.
	const QByteArray original = serializeLevelMap(document).bytes;
	selectLevelMapObject(&document, QStringLiteral("brush:1"), &error);
	const auto selection = document.selection;
	auto revision = document.revision;
	const int depth = static_cast<int>(document.undoStack.size());
	LevelMapSoftBoundsChange change;
	change.mode = LevelMapSoftBoundsMode::Custom;
	change.mins = {-1024, -1024, -512, true};
	change.maxs = {1024, 1024, 512, true};
	expect(setLevelMapSoftBounds(&document, change, &error) && worldValue(document, QString::fromLatin1(kLevelMapSoftBoundsKey))
			== QStringLiteral("-1024 -1024 -512 1024 1024 512"),
		"custom bounds written", error);
	expect(document.revision != revision && document.undoStack.size() == depth + 1 && document.undoStack.constLast().description == QStringLiteral("Set soft map bounds"),
		"one named undo step", document.undoStack.isEmpty() ? QString() : document.undoStack.constLast().description);
	expect(document.selection == selection, "the selection stays");
	bounds = levelMapSoftBounds(document);
	expect(bounds.mode == LevelMapSoftBoundsMode::Custom && bounds.valid && bounds.maxs.z == 512, "custom bounds read back");
	expect(levelMapObjectsOutsideBounds(document, bounds.mins, bounds.maxs).size() == 2, "the same two outside");
	revision = document.revision;
	expect(setLevelMapSoftBounds(&document, change, &error) && document.revision == revision, "unchanged bounds make no step");
	const QByteArray custom = serializeLevelMap(document).bytes;
	expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == original, "undo restores the map exactly", error);
	expect(redoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == custom, "redo", error);

	change.mode = LevelMapSoftBoundsMode::Disabled;
	expect(setLevelMapSoftBounds(&document, change, &error) && worldValue(document, QString::fromLatin1(kLevelMapSoftBoundsKey)) == QStringLiteral("none")
			&& levelMapSoftBounds(document).mode == LevelMapSoftBoundsMode::Disabled && !levelMapSoftBounds(document).valid,
		"disabled writes none", error);
	change.mode = LevelMapSoftBoundsMode::Game;
	bool present = true;
	expect(setLevelMapSoftBounds(&document, change, &error) && (worldValue(document, QString::fromLatin1(kLevelMapSoftBoundsKey), &present), !present)
			&& levelMapSoftBounds(document).mode == LevelMapSoftBoundsMode::Game,
		"game takes the key away", error);
	revision = document.revision;
	expect(setLevelMapSoftBounds(&document, change, &error) && document.revision == revision, "game again makes no step");

	change.mode = LevelMapSoftBoundsMode::Custom;
	change.mins = {0, 0, 0, true};
	change.maxs = {0, 64, 64, true};
	expect(!setLevelMapSoftBounds(&document, change, &error) && !error.isEmpty() && document.revision == revision, "min not below max refused");

	// A value TrenchBroom could not read: custom, malformed, nothing in force.
	const int world = entityId(document, QStringLiteral("worldspawn"));
	expect(setLevelMapEntityProperty(&document, world, QString::fromLatin1(kLevelMapSoftBoundsKey), QStringLiteral("big"), &error), "malformed value", error);
	bounds = levelMapSoftBounds(document);
	expect(bounds.mode == LevelMapSoftBoundsMode::Custom && bounds.malformed && !bounds.valid, "malformed value reads as malformed");

	// A locked layer elsewhere does not lock worldspawn's map properties.
	LevelMapDocument locked = fixture();
	QString layer;
	expect(createLevelSceneNode(&locked, LevelSceneNodeKind::Layer, QStringLiteral("Doors"), {}, &layer, &error)
			&& assignLevelSceneObjects(&locked, layer, {QStringLiteral("entity:%1").arg(entityId(locked, QStringLiteral("func_door")))}, &error)
			&& setLevelSceneLocked(&locked, layer, true, &error),
		"locked layer", error);
	change.mode = LevelMapSoftBoundsMode::Disabled;
	expect(setLevelMapSoftBounds(&locked, change, &error), "soft bounds change beside a locked layer", error);

	// Doom maps have none.
	LevelMapDocument doom;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("doom");
	expect(createLevelMap(create, &doom, &error) && !levelMapGameSoftBounds(doom).valid && !setLevelMapSoftBounds(&doom, change, &error) && !error.isEmpty(),
		"Doom maps refuse soft bounds");
}

void modsTests(const QString& folder)
{
	LevelMapDocument document = fixture();
	QString error;
	expect(levelMapEnabledMods(document).isEmpty(), "no mods at first");
	const QByteArray original = serializeLevelMap(document).bytes;
	expect(setLevelMapEnabledMods(&document, {QStringLiteral("hipnotic"), QStringLiteral(" ctf ")}, &error)
			&& worldValue(document, QString::fromLatin1(kLevelMapModsKey)) == QStringLiteral("hipnotic;ctf")
			&& levelMapEnabledMods(document) == QStringList({QStringLiteral("hipnotic"), QStringLiteral("ctf")})
			&& document.undoStack.constLast().description == QStringLiteral("Set enabled mods"),
		"mods written in order", error);
	const auto revision = document.revision;
	expect(setLevelMapEnabledMods(&document, {QStringLiteral("hipnotic"), QStringLiteral("ctf")}, &error) && document.revision == revision, "unchanged mods make no step");
	expect(!setLevelMapEnabledMods(&document, {QStringLiteral("ctf"), QStringLiteral("CTF")}, &error) && error.contains(QStringLiteral("twice")),
		"repeated mods refused", error);
	for (const QString& bad : {QStringLiteral("a;b"), QStringLiteral("../up"), QStringLiteral(".."), QStringLiteral(" "), QStringLiteral("c:\\games")}) {
		expect(!setLevelMapEnabledMods(&document, {bad}, &error) && !error.isEmpty() && document.revision == revision, "bad mod name refused", bad);
	}
	bool present = true;
	expect(setLevelMapEnabledMods(&document, {}, &error) && (worldValue(document, QString::fromLatin1(kLevelMapModsKey), &present), !present), "no mods takes the key away",
		error);
	expect(undoLevelMapEdit(&document, &error) && undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == original, "undo restores", error);

	// The folders beside the game's own.
	const QString root = QDir(folder).filePath(QStringLiteral("quake"));
	for (const char* name : {"id1", "rogue", "Hipnotic", ".git"}) {
		QDir().mkpath(QDir(root).filePath(QLatin1String(name)));
	}
	expect(levelMapAvailableMods(root, {QStringLiteral("ID1")}) == QStringList({QStringLiteral("Hipnotic"), QStringLiteral("rogue")}), "available mods",
		levelMapAvailableMods(root, {QStringLiteral("ID1")}).join(' '));
	expect(levelMapAvailableMods(QDir(folder).filePath(QStringLiteral("missing")), {}).isEmpty(), "no root, no mods");
	expect(levelMapBaseGameFolders().contains(QStringLiteral("id1")) && levelMapBaseGameFolders().contains(QStringLiteral("baseq3")), "base game folders");
}

void layerTests()
{
	LevelMapDocument document = fixture();
	QString error;
	QString layer;
	QString group;
	const QString door = QStringLiteral("entity:%1").arg(entityId(document, QStringLiteral("func_door")));
	expect(createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("Details"), {}, &layer, &error)
			&& createLevelSceneNode(&document, LevelSceneNodeKind::Group, QStringLiteral("Door group"), layer, &group, &error)
			&& assignLevelSceneObjects(&document, group, {door}, &error) && assignLevelSceneObjects(&document, layer, {QStringLiteral("brush:3")}, &error)
			&& setLevelSceneVisible(&document, layer, false, &error) && setLevelSceneOmitFromExport(&document, layer, true, &error),
		"layer fixture", error);
	QVector<LevelMapLayerSummary> layers = levelMapLayerSummaries(document);
	const int objects = static_cast<int>(levelSceneObjects(document).size());
	if (expect(layers.size() == 2, "Default and Details", QString::number(layers.size()))) {
		expect(layers[0].id.isEmpty() && layers[0].name == QStringLiteral("Default") && layers[0].creation && layers[0].visible, "Default first, new objects there");
		expect(layers[1].id == layer && layers[1].name == QStringLiteral("Details") && !layers[1].visible && layers[1].omitFromExport && !layers[1].locked
				&& layers[1].groups == 1 && !layers[1].creation,
			"Details summed up");
		// The door brings its brush; brush 3 is the detail one.
		expect(layers[1].objects == 3 && layers[0].objects + layers[1].objects == objects, "every object counted once",
			QStringLiteral("%1 + %2 of %3").arg(layers[0].objects).arg(layers[1].objects).arg(objects));
	}
	expect(setLevelSceneLocked(&document, layer, true, &error), "lock", error);
	document.activeSceneNode = group;
	layers = levelMapLayerSummaries(document);
	expect(layers.size() == 2 && layers[1].locked && layers[1].creation && !layers[0].creation, "locked, and the creation group's layer");
	const QJsonObject json = levelMapInspectorJson(document);
	expect(json[QStringLiteral("layers")].toArray().size() == 2 && json[QStringLiteral("layers")].toArray()[1].toObject()[QStringLiteral("name")] == QStringLiteral("Details")
			&& json[QStringLiteral("softBounds")].toObject()[QStringLiteral("outsideCount")] == 2 && json[QStringLiteral("mods")].toArray().isEmpty(),
		"inspector JSON", QString::fromUtf8(QJsonDocument(json).toJson()));
	const QStringList lines = levelMapInspectorLines(document);
	expect(lines.size() == 5 && lines[0].startsWith(QStringLiteral("Soft bounds: the game's")) && lines[1] == QStringLiteral("Objects outside the soft bounds: 2"),
		"inspector lines", lines.join('\n'));
}

// Map settings with a group open: TrenchBroom's Map Properties and Mods
// change the world node whatever group is open, and so do the soft bounds,
// the mods and the Worldspawn list here. The group stays open, each change is
// one undo step, and edits outside the group are still refused.
void groupOpenTests()
{
	LevelMapDocument document = fixture();
	QString error;
	QString group;
	const QString door = QStringLiteral("entity:%1").arg(entityId(document, QStringLiteral("func_door")));
	expect(createLevelSceneNode(&document, LevelSceneNodeKind::Group, QStringLiteral("Door group"), {}, &group, &error)
			&& assignLevelSceneObjects(&document, group, {door}, &error) && changeLevelSceneGroupContext(&document, group, false, &error),
		"group opened", error);
	expect(document.openSceneGroups == QStringList {group}, "the door's group is open");
	const QByteArray original = serializeLevelMap(document).bytes;
	const int depth = static_cast<int>(document.undoStack.size());

	// Outside the group, an entity edit is still refused.
	const int light = entityId(document, QStringLiteral("light"));
	expect(!setLevelMapEntityProperty(&document, light, QStringLiteral("light"), QStringLiteral("500"), &error) && !error.isEmpty(),
		"an edit outside the open group is refused", error);

	LevelMapSoftBoundsChange change;
	change.mode = LevelMapSoftBoundsMode::Custom;
	change.mins = {-2048, -2048, -512, true};
	change.maxs = {2048, 2048, 512, true};
	expect(setLevelMapSoftBounds(&document, change, &error), "soft bounds change with a group open", error);
	expect(setLevelMapEnabledMods(&document, {QStringLiteral("hipnotic")}, &error), "mods change with a group open", error);
	const int world = entityId(document, QStringLiteral("worldspawn"));
	expect(editLevelMapWorldSettings(&document, [&](LevelMapDocument* edited) {
		return setLevelMapEntityProperty(edited, world, QStringLiteral("message"), QStringLiteral("Open group"), &error);
	}), "a Worldspawn key changes with a group open", error);
	expect(document.openSceneGroups == QStringList {group} && document.activeSceneNode == group, "the group stays open and active");
	expect(document.undoStack.size() == depth + 3 && document.undoStack.constLast().description != QString(), "three undo steps",
		QString::number(document.undoStack.size() - depth));
	expect(worldValue(document, QString::fromLatin1(kLevelMapSoftBoundsKey)) == QStringLiteral("-2048 -2048 -512 2048 2048 512")
			&& worldValue(document, QString::fromLatin1(kLevelMapModsKey)) == QStringLiteral("hipnotic")
			&& worldValue(document, QStringLiteral("message")) == QStringLiteral("Open group"),
		"worldspawn holds the settings");
	const QByteArray edited = serializeLevelMap(document).bytes;
	for (int step = 0; step < 3; ++step) {
		expect(undoLevelMapEdit(&document, &error), "undo with the group open", error);
	}
	expect(serializeLevelMap(document).bytes == original && document.openSceneGroups == QStringList {group}, "undo restores the map exactly, group still open");
	for (int step = 0; step < 3; ++step) {
		expect(redoLevelMapEdit(&document, &error), "redo with the group open", error);
	}
	expect(serializeLevelMap(document).bytes == edited, "redo", error);
	// The open group still keeps new objects and other edits inside it.
	expect(!setLevelMapEntityProperty(&document, light, QStringLiteral("light"), QStringLiteral("500"), &error), "the boundary is back after the edit");
}

// TrenchBroom-format maps keep each layer and group as a func_group entity.
// Map Info counts them as layers and groups, not entities (NetRadiant Custom
// counts them as func_group group entities), and names them in a line of
// their own. Doom thing types sort by number.
void containerAndThingTests()
{
	LevelMapDocument document = fixture();
	QString error;
	QString layer;
	QString group;
	expect(createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("Lights"), {}, &layer, &error)
			&& createLevelSceneNode(&document, LevelSceneNodeKind::Group, QStringLiteral("Door group"), layer, &group, &error)
			&& assignLevelSceneObjects(&document, group, {QStringLiteral("entity:%1").arg(entityId(document, QStringLiteral("func_door")))}, &error),
		"layer and group", error);
	const QByteArray trenchBroom = serializeLevelMap(document, LevelSceneStorage::TrenchBroom).bytes;
	expect(trenchBroom.count("\"_tb_type\"") == 2, "two containers written", QString::number(trenchBroom.count("\"_tb_type\"")));
	LevelMapDocument reloaded;
	if (expect(loadLevelMapBytes({QStringLiteral("layered.map"), {}, {}}, trenchBroom, &reloaded, &error), "TrenchBroom map loads", error)) {
		const LevelMapStatistics statistics = levelMapStatistics(reloaded);
		const auto* groups = classRow(statistics, QStringLiteral("func_group"));
		expect(reloaded.sceneStorage == LevelSceneStorage::TrenchBroom && statistics.entityCount == 10 && groups && groups->count == 1,
			"containers are not entities", QString::number(statistics.entityCount));
		const QStringList lines = levelMapStatisticsLines(reloaded, statistics);
		expect(lines.contains(QStringLiteral("Layer and group entities (func_group, not counted above): 2")), "containers named", lines.join('\n'));
		expect(!levelMapStatisticsLines(document).join('\n').contains(QStringLiteral("Layer and group entities")), "no line for VibeStudio storage");
	}

	LevelMapDocument doom;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("doom");
	if (expect(createLevelMap(create, &doom, &error), "Doom map", error)) {
		for (const int type : {3001, 9, 2001, 9}) {
			expect(addLevelMapDoomThing(&doom, type, 64, 64, 0, nullptr, &error), "thing", error);
		}
		QStringList names;
		for (const LevelMapEntityClassCount& row : levelMapStatistics(doom).entityClasses) {
			names << row.className;
		}
		const qsizetype nine = names.indexOf(QStringLiteral("thing:9"));
		const qsizetype shotgun = names.indexOf(QStringLiteral("thing:2001"));
		const qsizetype imp = names.indexOf(QStringLiteral("thing:3001"));
		expect(nine >= 0 && nine < shotgun && shotgun < imp, "thing types sort by number", names.join(' '));
	}
}

void cliTests(const QString& executable, const QString& folder)
{
	const auto run = [&](const QStringList& args, int code = 0, QByteArray* text = nullptr) {
		QProcess process;
		process.setWorkingDirectory(folder);
		const bool json = !text;
		process.start(executable, QStringList {"--cli"} + (json ? QStringList {"--json"} : QStringList {}) + QStringList {"--settings-file", QDir(folder).filePath("settings.ini")} + args);
		const bool done = process.waitForStarted() && process.waitForFinished(120000);
		const auto out = process.readAllStandardOutput();
		const auto err = process.readAllStandardError();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code, "CLI status", args.join(' ') + '\n' + QString::fromUtf8(out + err));
		if (!done) {
			process.kill();
			process.waitForFinished();
		}
		if (text) {
			*text = out;
		}
		return QJsonDocument::fromJson(out).object();
	};
	const QString mapPath = QDir(folder).filePath(QStringLiteral("info.map"));
	QFile file(mapPath);
	expect(file.open(QIODevice::WriteOnly) && file.write(tests::mapInfoFixture()) > 0, "CLI fixture");
	file.close();

	QJsonObject report = run({"map", "inspect", mapPath});
	const QJsonObject statistics = report["map"].toObject()["statistics"].toObject();
	const QJsonArray classes = statistics["entityClasses"].toArray();
	QJsonObject light;
	for (const QJsonValue& value : classes) {
		if (value.toObject()["className"] == QStringLiteral("light")) {
			light = value.toObject();
		}
	}
	expect(statistics["groupEntityCount"] == 4 && statistics["inGameEntityCount"] == 4 && statistics["inGameGroupEntityCount"] == 2 && classes.size() == 9
			&& light["count"] == 2 && light["inGameCount"] == 0 && statistics["patchCount"] == 0,
		"map inspect --json statistics", QString::fromUtf8(QJsonDocument(statistics).toJson()));
	const QJsonObject properties = report["map"].toObject()["mapProperties"].toObject();
	expect(properties["softBounds"].toObject()["mode"] == QStringLiteral("game") && properties["softBounds"].toObject()["outsideCount"] == 2
			&& properties["layers"].toArray().size() == 1,
		"map inspect --json map properties", QString::fromUtf8(QJsonDocument(properties).toJson()));
	QByteArray text;
	run({"map", "inspect", mapPath}, 0, &text);
	expect(text.contains("Entity classes:") && text.contains("- light: 2 (0 in game)") && text.contains("In-game entities: 4"), "map inspect text", QString::fromUtf8(text.left(800)));

	// Report, with an installation's mod folders.
	const QString root = QDir(folder).filePath(QStringLiteral("game"));
	for (const char* name : {"id1", "baseq2", "hipnotic", "rogue"}) {
		QDir().mkpath(QDir(root).filePath(QLatin1String(name)));
	}
	report = run({"map", "properties", mapPath, "--game-root", root});
	expect(report["availableMods"].toArray() == QJsonArray({"hipnotic", "rogue"}) && report["softBounds"].toObject()["mode"] == QStringLiteral("game")
			&& report["mapWritten"] == false,
		"map properties report", QString::fromUtf8(QJsonDocument(report).toJson()));
	report = run({"map", "properties", mapPath, "--game-root", root, "--game", "quake2"});
	expect(report["availableMods"].toArray() == QJsonArray({"hipnotic", "id1", "rogue"}), "--game leaves out only that game's folder",
		QString::fromUtf8(QJsonDocument(report).toJson()));
	run({"map", "properties", mapPath, "--game-root", root, "--game", "nonsense"}, 2);
	run({"map", "properties", mapPath}, 0, &text);
	expect(text.contains("Soft bounds: the game's") && text.contains("Mods: none") && text.contains("Layer Default"), "map properties text", QString::fromUtf8(text));

	// Changes, written safely.
	const QString first = QDir(folder).filePath(QStringLiteral("props.map"));
	report = run({"map", "properties", mapPath, "--soft-bounds", "-2048 -2048 -512 2048 2048 1024", "--enable-mod", "hipnotic", "--enable-mod", "ctf", "--output", first});
	LevelMapDocument written;
	QString error;
	expect(report["mapWritten"] == true && report["changed"].toArray() == QJsonArray({"softBounds", "mods"})
			&& loadLevelMap({first, {}, {}}, &written, &error) && levelMapSoftBounds(written).mode == LevelMapSoftBoundsMode::Custom
			&& levelMapSoftBounds(written).maxs.z == 1024 && levelMapEnabledMods(written) == QStringList({QStringLiteral("hipnotic"), QStringLiteral("ctf")}),
		"properties written", QString::fromUtf8(QJsonDocument(report).toJson()) + error);
	const QString second = QDir(folder).filePath(QStringLiteral("props2.map"));
	report = run({"map", "properties", first, "--soft-bounds", "none", "--disable-mod", "CTF", "--output", second});
	expect(report["mapWritten"] == true && loadLevelMap({second, {}, {}}, &written, &error) && levelMapSoftBounds(written).mode == LevelMapSoftBoundsMode::Disabled
			&& levelMapEnabledMods(written) == QStringList({QStringLiteral("hipnotic")}),
		"none and a disabled mod", error);
	const QString third = QDir(folder).filePath(QStringLiteral("props3.map"));
	report = run({"map", "properties", second, "--no-mods", "--soft-bounds", "game", "--output", third, "--dry-run"});
	expect(report["mapWritten"] == false && report["dryRun"] == true && !QFileInfo::exists(third), "dry run writes nothing");
	report = run({"map", "properties", second, "--mods", "rogue;hipnotic", "--output", third});
	expect(report["mapWritten"] == true && loadLevelMap({third, {}, {}}, &written, &error)
			&& levelMapEnabledMods(written) == QStringList({QStringLiteral("rogue"), QStringLiteral("hipnotic")}),
		"--mods sets the whole list", error);
	report = run({"map", "properties", third, "--mods", "rogue;hipnotic", "--output", QDir(folder).filePath(QStringLiteral("same.map"))});
	expect(report["changed"].toArray().isEmpty() && report["mapWritten"] == true, "an unchanged list changes nothing");

	// Refusals.
	run({"map", "properties", mapPath, "--soft-bounds", "1 2 3", "--output", third, "--overwrite"}, 2);
	run({"map", "properties", mapPath, "--soft-bounds", "game"}, 2);
	run({"map", "properties", mapPath, "--output", third}, 2);
	run({"map", "properties", mapPath, "--mods", "a", "--no-mods", "--output", third}, 2);
	run({"map", "properties", mapPath, "--disable-mod", "zzz", "--output", third, "--overwrite"}, 4);
	run({"map", "properties", mapPath, "--enable-mod", "a;b", "--output", third, "--overwrite"}, 4);
	run({"map", "properties", mapPath, "--soft-bounds", "none", "--output", third}, 4);
	run({"map", "properties", QDir(folder).filePath(QStringLiteral("missing.map"))}, 3);
	run({"map", "properties"}, 2);
	// Doom maps report, but refuse changes.
	LevelMapDocument doom;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("doom");
	const QString wad = QDir(folder).filePath(QStringLiteral("doom.wad"));
	LevelDocumentSaveRequest save;
	save.path = wad;
	save.keepBackup = false;
	if (expect(createLevelMap(create, &doom, &error) && writeLevelDocument(doom, save).succeeded(), "Doom WAD", error)) {
		report = run({"map", "properties", wad});
		expect(report["softBounds"].toObject()["applies"] == false && report["mods"].toArray().isEmpty(), "Doom report");
		run({"map", "properties", wad, "--enable-mod", "x", "--output", QDir(folder).filePath(QStringLiteral("doom2.wad"))}, 4);
	}
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "temporary folder")) {
		return 1;
	}
	statisticsTests();
	softBoundsTests();
	modsTests(temp.path());
	layerTests();
	groupOpenTests();
	containerAndThingTests();
	if (argc >= 2) {
		cliTests(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath(), temp.path());
	}
	std::cout << (ok ? "Map Info smoke test passed." : "Map Info smoke test failed.") << std::endl;
	return ok ? 0 : 1;
}
