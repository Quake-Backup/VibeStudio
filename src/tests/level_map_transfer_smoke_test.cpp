// Import Map, Export Selection as Map, Export Map and Omit From Export: core
// services and the map import / map export / map copy --standalone CLI routes.
#include "core/level_document.h"
#include "core/level_linked_groups.h"
#include "core/level_map_transfer.h"
#include "core/level_paste.h"
#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_placement_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool pass, const char* label, const QString& detail = {})
{
	if (!pass) {
		std::cerr << "FAIL: " << label << ": " << qPrintable(detail) << '\n';
		ok = false;
	}
	return pass;
}

int entityByKey(const LevelMapDocument& document, const QString& key, const QString& value)
{
	for (const auto& entity : document.entities) {
		for (const auto& property : entity.properties) {
			if (property.key == key && property.value == value) {
				return entity.id;
			}
		}
	}
	return -1;
}

QString keyOf(const LevelMapDocument& document, int entityId, const QString& key)
{
	for (const auto& entity : document.entities) {
		if (entity.id != entityId) {
			continue;
		}
		for (const auto& property : entity.properties) {
			if (property.key == key) {
				return property.value;
			}
		}
	}
	return {};
}

int worldspawnId(const LevelMapDocument& document)
{
	for (const auto& entity : document.entities) {
		if (entity.className == QLatin1String("worldspawn")) {
			return entity.id;
		}
	}
	return -1;
}

int countClass(const LevelMapDocument& document, const QString& className)
{
	int count = 0;
	for (const auto& entity : document.entities) {
		count += entity.className == className ? 1 : 0;
	}
	return count;
}

int lastBrush(const LevelMapDocument& document)
{
	return document.brushes.isEmpty() ? -1 : document.brushes.last().id;
}

LevelMapDocument blank(const QString& game, const QString& message)
{
	LevelMapDocument document;
	LevelMapCreateRequest request;
	request.game = game;
	request.starterRoom = false;
	QString error;
	expect(createLevelMap(request, &document, &error), "create map", error);
	expect(setLevelMapEntityProperty(&document, worldspawnId(document), QStringLiteral("message"), message, &error), "worldspawn message", error);
	return document;
}

bool box(LevelMapDocument* document, double x, double y, double size = 64)
{
	QString error;
	return expect(addLevelMapBoxBrush(document, {x, y, 0, true}, {x + size, y + size, size, true}, QStringLiteral("WALL"), nullptr, &error),
		"add box", error);
}

bool tie(LevelMapDocument* document, double x, const QString& className, const QVector<LevelMapProperty>& properties)
{
	QString error;
	return box(document, x, 0) && expect(setLevelMapSelection(document, {{LevelMapSelectionKind::QuakeBrush, lastBrush(*document)}}, &error), "select box", error)
		&& expect(tieLevelMapSelectionToEntity(document, className, properties, nullptr, &error), "tie entity", error);
}

bool save(const LevelMapDocument& document, const QString& path, std::optional<LevelSceneStorage> storage = std::nullopt)
{
	LevelDocumentSaveRequest request;
	request.path = path;
	request.overwrite = true;
	request.keepBackup = false;
	request.sceneStorage = storage;
	const auto report = writeLevelDocument(document, request);
	return expect(report.succeeded() && report.written, "save fixture", report.errors.join('\n'));
}

LevelMapDocument load(const QString& path)
{
	LevelMapDocument document;
	QString error;
	expect(loadLevelMap({path, {}, {}}, &document, &error), "load map", error);
	return document;
}

QByteArray bytes(const LevelMapDocument& document)
{
	return serializeLevelMap(document).bytes;
}

QString nodeNamed(const LevelSceneState& scene, const QString& name)
{
	for (const auto& node : scene.nodes) {
		if (node.name == name) {
			return node.id;
		}
	}
	return {};
}

// A Quake map whose names, groups and layer an import has to deal with.
LevelMapDocument otherMap()
{
	auto document = blank(QStringLiteral("quake"), QStringLiteral("Other"));
	QString error;
	expect(setLevelMapEntityProperty(&document, worldspawnId(document), QStringLiteral("wad"), QStringLiteral("other.wad"), &error), "wad key", error);
	box(&document, 0, 0);
	box(&document, 128, 0);
	tie(&document, 256, QStringLiteral("func_door"), {{QStringLiteral("targetname"), QStringLiteral("door"), 0}});
	tie(&document, 384, QStringLiteral("func_button"), {{QStringLiteral("target"), QStringLiteral("door"), 0}});
	expect(addLevelMapEntity(&document, QStringLiteral("info_null"), {32, 32, 96, true}, {{QStringLiteral("targetname"), QStringLiteral("t3"), 0}},
		nullptr, &error), "info_null", error);
	QString group, layer;
	expect(createLevelSceneNode(&document, LevelSceneNodeKind::Group, QStringLiteral("Pair"), {}, &group, &error)
		&& assignLevelSceneObjects(&document, group, {QStringLiteral("brush:0"), QStringLiteral("brush:1")}, &error), "other group", error);
	const int info = entityByKey(document, QStringLiteral("targetname"), QStringLiteral("t3"));
	expect(createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("Extra"), {}, &layer, &error)
		&& assignLevelSceneObjects(&document, layer, {QStringLiteral("entity:%1").arg(info)}, &error), "other layer", error);
	return document;
}

LevelMapDocument destinationMap()
{
	auto document = blank(QStringLiteral("quake"), QStringLiteral("Dest"));
	QString error;
	box(&document, -512, -512);
	for (const auto& name : {QStringLiteral("door"), QStringLiteral("door1"), QStringLiteral("t3")}) {
		expect(addLevelMapEntity(&document, QStringLiteral("info_notnull"), {-256, -256, 32, true}, {{QStringLiteral("targetname"), name, 0}},
			nullptr, &error), "destination name", error);
	}
	return document;
}

// A classic map with `count` brushes: more blocks than a paste accepts.
QByteArray bulkMap(int count)
{
	QByteArray text("{\n\"classname\" \"worldspawn\"\n");
	const auto point = [](double x, double y, double z) { return QStringLiteral("( %1 %2 %3 )").arg(x).arg(y).arg(z); };
	for (int index = 0; index < count; ++index) {
		const double x0 = (index % 100) * 32, y0 = (index / 100) * 32, z0 = 0, x1 = x0 + 16, y1 = y0 + 16, z1 = 16;
		QStringList faces{
			point(x0, y0, z1) + ' ' + point(x0, y0, z0) + ' ' + point(x0, y1, z0),
			point(x1, y1, z0) + ' ' + point(x1, y0, z0) + ' ' + point(x1, y0, z1),
			point(x1, y0, z0) + ' ' + point(x0, y0, z0) + ' ' + point(x0, y0, z1),
			point(x0, y1, z1) + ' ' + point(x0, y1, z0) + ' ' + point(x1, y1, z0),
			point(x0, y1, z0) + ' ' + point(x0, y0, z0) + ' ' + point(x1, y0, z0),
			point(x1, y0, z1) + ' ' + point(x0, y0, z1) + ' ' + point(x0, y1, z1)};
		text += "{\n";
		for (const auto& face : faces) {
			text += (face + QStringLiteral(" WALL 0 0 0 1 1\n")).toUtf8();
		}
		text += "}\n";
	}
	text += "}\n";
	return text;
}

void importTests(const QString& folder)
{
	QString error;
	const auto otherPath = QDir(folder).filePath("other.map"), destPath = QDir(folder).filePath("dest.map");
	save(otherMap(), otherPath);
	save(destinationMap(), destPath);
	auto destination = load(destPath);
	LevelMapDocument imported;
	if (!expect(readLevelMapImport(otherPath, destination.engineFamily, &imported, &error), "read import", error)) {
		return;
	}
	const auto preview = previewLevelMapImport(destination, imported);
	expect(preview.importable() && preview.entities == 3 && preview.brushes == 4 && preview.patches == 0 && preview.groups == 1 && preview.layers == 1
		&& preview.worldspawnKeys == 2, "preview counts", QString::fromUtf8(QJsonDocument(levelMapImportPreviewJson(preview)).toJson()));
	expect(preview.renamedTargets == QMap<QString, QString>{{QStringLiteral("door"), QStringLiteral("door2")}, {QStringLiteral("t3"), QStringLiteral("t4")}},
		"clashing names take the next free number");
	LevelMapImportOptions keep;
	keep.renameTargets = false;
	expect(previewLevelMapImport(destination, imported, keep).renamedTargets.isEmpty(), "names can be kept");

	const auto before = bytes(destination);
	const auto undoDepth = destination.undoStack.size();
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::ImportMap;
	request.importSource = std::make_shared<const LevelMapDocument>(imported);
	const auto result = prepareLevelPlacement(destination, request);
	if (!expect(result.succeeded, "import placement", result.error)) {
		return;
	}
	auto after = result.document;
	expect(after.undoStack.size() == undoDepth + 1 && after.undoStack.last().description.contains(QStringLiteral("other.map")), "one named undo step");
	expect(after.brushes.size() == destination.brushes.size() + 4 && countClass(after, QStringLiteral("func_door")) == 1
		&& countClass(after, QStringLiteral("func_button")) == 1 && countClass(after, QStringLiteral("info_null")) == 1, "objects imported");
	expect(after.selection.size() == 5 && result.importReport.inserted == after.selection, "imported objects selected");
	const int door = entityByKey(after, QStringLiteral("classname"), QStringLiteral("func_door"));
	const int button = entityByKey(after, QStringLiteral("classname"), QStringLiteral("func_button"));
	const int info = entityByKey(after, QStringLiteral("classname"), QStringLiteral("info_null"));
	expect(keyOf(after, door, QStringLiteral("targetname")) == QStringLiteral("door2") && keyOf(after, button, QStringLiteral("target")) == QStringLiteral("door2")
		&& keyOf(after, info, QStringLiteral("targetname")) == QStringLiteral("t4"), "renamed links keep pointing at each other");
	expect(entityByKey(after, QStringLiteral("targetname"), QStringLiteral("door")) >= 0 && keyOf(after, worldspawnId(after), QStringLiteral("message")) == QStringLiteral("Dest")
		&& keyOf(after, worldspawnId(after), QStringLiteral("wad")).isEmpty(), "open map keeps its names and worldspawn keys");
	LevelMapVec3 mins, maxs;
	const auto firstImported = after.brushes.at(destination.brushes.size()).id;
	expect(levelMapObjectsBounds(after, {{LevelMapSelectionKind::QuakeBrush, firstImported}}, &mins, &maxs)
		&& mins.x == 0 && mins.y == 0 && maxs.x == 64 && maxs.z == 64, "original coordinates");
	const auto pair = nodeNamed(after.scene, QStringLiteral("Pair"));
	expect(!pair.isEmpty() && levelSceneNode(after.scene, pair)->objects.size() == 2 && nodeNamed(after.scene, QStringLiteral("Extra")).isEmpty(),
		"complete groups keep their structure; layers are not recreated");
	const auto afterBytes = bytes(after);
	expect(undoLevelMapEdit(&after, &error) && bytes(after) == before, "undo restores the map exactly", error);
	expect(redoLevelMapEdit(&after, &error) && bytes(after) == afterBytes, "redo imports again", error);

	// The open group is the creation context.
	auto grouped = destination;
	QString group;
	expect(createLevelSceneNode(&grouped, LevelSceneNodeKind::Group, QStringLiteral("Room"), {}, &group, &error)
		&& assignLevelSceneObjects(&grouped, group, {QStringLiteral("brush:0")}, &error) && openLevelSceneGroup(&grouped, group, &error), "open group", error);
	expect(importLevelMap(&grouped, imported, {}, nullptr, &error), "import into an open group", error);
	const int groupedDoor = entityByKey(grouped, QStringLiteral("classname"), QStringLiteral("func_door"));
	const auto importedPair = nodeNamed(grouped.scene, QStringLiteral("Pair"));
	expect(levelSceneMembership(grouped.scene, QStringLiteral("entity:%1").arg(groupedDoor)) == group && !importedPair.isEmpty()
		&& levelSceneNode(grouped.scene, importedPair)->parentId == group, "imported objects and groups join the open group");

	// A locked creation layer refuses the import and changes nothing.
	auto locked = destination;
	QString layer;
	expect(createLevelSceneNode(&locked, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error), "locked layer", error);
	locked.activeSceneNode = layer;
	expect(setLevelSceneLocked(&locked, layer, true, &error), "lock layer", error);
	const auto lockedBefore = bytes(locked);
	const auto lockedDepth = locked.undoStack.size();
	expect(!importLevelMap(&locked, imported, {}, nullptr, &error) && bytes(locked) == lockedBefore && locked.undoStack.size() == lockedDepth,
		"scene locks refuse the import", error);

	// Mismatched brush formats are refused before anything changes.
	LevelMapDocument valve;
	expect(loadLevelMapBytes({QStringLiteral("valve.map"), {}, QStringLiteral("idTech2")}, tests::placementFixture(QStringLiteral("valve220")), &valve, &error),
		"valve fixture", error);
	const auto mismatch = previewLevelMapImport(destination, valve);
	auto unchanged = destination;
	expect(!mismatch.importable() && !importLevelMap(&unchanged, valve, {}, nullptr, &error) && error == mismatch.problem
		&& bytes(unchanged) == before, "texture format mismatch refused", error);

	// File input is not bound by the clipboard's block limit.
	const auto bulkPath = QDir(folder).filePath("bulk.map");
	const auto bulk = bulkMap(8200);
	QFile file(bulkPath);
	expect(file.open(QIODevice::WriteOnly) && file.write(bulk) == bulk.size(), "bulk fixture");
	file.close();
	auto pasted = destination;
	expect(!pasteLevelMapText(&pasted, QString::fromUtf8(bulk), &error), "paste keeps its block limit");
	QElapsedTimer timer;
	timer.start();
	LevelMapDocument large;
	auto big = destination;
	expect(readLevelMapImport(bulkPath, big.engineFamily, &large, &error) && large.brushes.size() == 8200, "read a large map", error);
	expect(importLevelMap(&big, large, {}, nullptr, &error) && big.brushes.size() == destination.brushes.size() + 8200
		&& big.undoStack.size() == destination.undoStack.size() + 1, "import beyond the clipboard limits", error);
	std::cout << "Large import: " << timer.elapsed() << " ms" << std::endl;

	// A WAD and a .bsp are not read as maps.
	expect(!readLevelMapImport(QDir(folder).filePath("missing.wad"), {}, &imported, &error), "WAD refused");
	expect(levelMapImportNeedsConversion(QStringLiteral("old.BSP")) && !readLevelMapImport(QStringLiteral("old.bsp"), {}, &imported, &error), "bsp needs conversion");
	const auto conversion = levelMapImportConversionRequest(QDir(folder).filePath("old.bsp"), QDir(folder).filePath("old_converted.map"), QStringLiteral("valve220"));
	expect(conversion.profileId == QStringLiteral("vibemap3-convert") && conversion.extraArguments.mid(0, 2) == QStringList{"-format", "map_220"}
		&& conversion.extraArguments.value(2) == QStringLiteral("-o"), "VibeMap3 convert request matches the brush format");
	const auto plan = buildCompilerCommandPlan(conversion);
	expect(plan.arguments.value(0) == QStringLiteral("-convert") && plan.arguments.contains(QStringLiteral("map_220"))
		&& QDir::cleanPath(plan.arguments.last()) == QDir::cleanPath(conversion.inputPath), "convert plan keeps the stage first and the BSP last");
}

void exportSelectionTests(const QString& folder)
{
	QString error;
	auto document = load(QDir(folder).filePath("other.map"));
	const int door = entityByKey(document, QStringLiteral("classname"), QStringLiteral("func_door"));
	const int info = entityByKey(document, QStringLiteral("classname"), QStringLiteral("info_null"));
	int doorBrush = -1;
	for (const auto& brush : document.brushes) {
		if (brush.entityId == door) {
			doorBrush = brush.id;
		}
	}
	expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 1}, {LevelMapSelectionKind::QuakeBrush, doorBrush},
		{LevelMapSelectionKind::Entity, info}}, &error), "select for export", error);
	const auto text = levelMapSelectionExportText(document, true, &error);
	LevelMapDocument exported;
	expect(!text.isEmpty() && loadLevelMapBytes({QStringLiteral("exported.map"), {}, {}}, text.toUtf8(), &exported, &error), "standalone text loads", error);
	expect(exported.entities.size() == 3 && exported.brushes.size() == 2 && exported.entities.first().className == QStringLiteral("worldspawn")
		&& keyOf(exported, worldspawnId(exported), QStringLiteral("message")) == QStringLiteral("Other")
		&& keyOf(exported, worldspawnId(exported), QStringLiteral("wad")) == QStringLiteral("other.wad"), "worldspawn wrapper with keys", text);
	const auto bare = levelMapSelectionExportText(document, false, &error);
	expect(loadLevelMapBytes({QStringLiteral("bare.map"), {}, {}}, bare.toUtf8(), &exported, &error) && exported.entities.first().properties.size() == 1
		&& exported.brushes.size() == 2, "wrapper without keys", bare);
	const auto clipboard = levelMapSelectionText(document, &error);
	expect(!clipboard.startsWith(QStringLiteral("{\n\"classname\" \"worldspawn\"")), "clipboard copies stay bare", clipboard.left(80));

	const auto path = QDir(folder).filePath("selection.map");
	LevelMapSelectionExportRequest request;
	request.save.path = path;
	request.save.dryRun = true;
	auto report = exportLevelMapSelection(document, request);
	expect(report.save.succeeded() && !report.save.written && !QFileInfo::exists(path), "dry run writes nothing", report.save.errors.join('\n'));
	request.save.dryRun = false;
	report = exportLevelMapSelection(document, request);
	expect(report.save.succeeded() && report.save.written && report.objects == 3 && load(path).brushes.size() == 2, "selection written",
		report.save.errors.join('\n'));
	report = exportLevelMapSelection(document, request);
	expect(!report.save.succeeded(), "existing output needs overwrite");
	request.save.path = document.sourcePath;
	request.save.overwrite = true;
	const auto sourceBytes = [&] { QFile source(document.sourcePath); return source.open(QIODevice::ReadOnly) ? source.readAll() : QByteArray(); };
	const auto original = sourceBytes();
	report = exportLevelMapSelection(document, request);
	expect(!report.save.succeeded() && sourceBytes() == original, "the open map's file is refused");

	// Complete groups keep their scene note.
	expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakeBrush, 1}}, &error), "select group", error);
	const auto grouped = levelMapSelectionExportText(document, true, &error);
	expect(grouped.contains(QStringLiteral("// VibeStudioScene: "))
		&& loadLevelMapBytes({QStringLiteral("grouped.map"), {}, {}}, grouped.toUtf8(), &exported, &error) && exported.scene.problem.isEmpty()
		&& !nodeNamed(exported.scene, QStringLiteral("Pair")).isEmpty(), "groups survive the export", exported.scene.problem);

	// A Quake II map keeps its target note.
	LevelMapDocument quake2;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake2");
	expect(createLevelMap(create, &quake2, &error) && setLevelMapSelection(&quake2, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error), "quake2 map", error);
	expect(levelMapSelectionExportText(quake2, true, &error).startsWith(QString::fromLatin1(kQuake2MapTargetHeader).trimmed()), "Quake II target kept", error);
}

void exportMapTests(const QString& folder)
{
	QString error;
	auto document = load(QDir(folder).filePath("dest.map"));
	auto other = load(QDir(folder).filePath("other.map"));
	expect(importLevelMap(&document, other, {}, nullptr, &error), "import for export", error);
	const auto sourcePath = QDir(folder).filePath("combined.map");
	save(document, sourcePath);
	document = load(sourcePath);
	const int button = entityByKey(document, QStringLiteral("classname"), QStringLiteral("func_button"));
	QString notes;
	expect(createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("Notes"), {}, &notes, &error)
		&& assignLevelSceneObjects(&document, notes, {QStringLiteral("entity:%1").arg(button), QStringLiteral("brush:0")}, &error), "notes layer", error);
	const auto depth = document.undoStack.size();
	expect(setLevelSceneOmitFromExport(&document, notes, true, &error) && document.undoStack.size() == depth + 1
		&& levelSceneOmittedFromExport(*levelSceneNode(document.scene, notes)), "omit flag is one undo step", error);
	expect(setLevelSceneOmitFromExport(&document, notes, true, &error) && document.undoStack.size() == depth + 1, "unchanged flag adds no step", error);
	const auto pair = nodeNamed(document.scene, QStringLiteral("Pair"));
	expect(!setLevelSceneOmitFromExport(&document, pair, true, &error), "groups cannot be omitted");
	expect(undoLevelMapEdit(&document, &error) && !levelSceneOmittedFromExport(*levelSceneNode(document.scene, notes))
		&& redoLevelMapEdit(&document, &error) && levelSceneOmittedFromExport(*levelSceneNode(document.scene, notes)), "omit flag undo and redo", error);
	expect(levelSceneJson(document)[QStringLiteral("nodes")].toArray().size() >= 2, "scene JSON");

	// The flag is saved in the scene note and as TrenchBroom's key.
	const auto flagged = QDir(folder).filePath("flagged.map"), trenchBroom = QDir(folder).filePath("flagged-tb.map");
	save(document, flagged);
	save(document, trenchBroom, LevelSceneStorage::TrenchBroom);
	const auto reread = load(flagged), rereadTb = load(trenchBroom);
	QFile tbFile(trenchBroom);
	expect(tbFile.open(QIODevice::ReadOnly) && tbFile.readAll().contains("\"_tb_layer_omit_from_export\" \"1\""), "TrenchBroom key written");
	expect(levelSceneOmittedFromExport(*levelSceneNode(reread.scene, nodeNamed(reread.scene, QStringLiteral("Notes"))))
		&& !nodeNamed(rereadTb.scene, QStringLiteral("Notes")).isEmpty()
		&& levelSceneOmittedFromExport(*levelSceneNode(rereadTb.scene, nodeNamed(rereadTb.scene, QStringLiteral("Notes")))), "flag survives reopening");

	// A linked group split across a kept and an omitted layer.
	expect(reparentLevelSceneNode(&document, pair, notes, &error), "pair into notes", error);
	QString duplicate;
	expect(createLinkedLevelGroup(&document, pair, {0, 1024, 0, true}, &duplicate, &error) && reparentLevelSceneNode(&document, duplicate, {}, &error),
		"linked duplicate in Default", error);
	expect(setLevelSceneLocked(&document, notes, true, &error), "lock omitted layer", error);
	const auto documentBytes = bytes(document);
	const auto revision = document.revision;
	const auto undo = document.undoStack.size();

	const auto exportPath = QDir(folder).filePath("export.map");
	LevelMapExportRequest request;
	request.save.path = exportPath;
	request.save.dryRun = true;
	auto report = exportLevelMapCopy(document, request);
	expect(report.save.succeeded() && !QFileInfo::exists(exportPath) && report.omittedLayers == QStringList{QStringLiteral("Notes")}, "export dry run",
		report.save.errors.join('\n'));
	request.save.dryRun = false;
	report = exportLevelMapCopy(document, request);
	expect(report.save.succeeded() && report.save.written, "export written", report.save.errors.join('\n'));
	const auto exported = load(exportPath);
	// Left out: brush:0, the button with its brush and the pair's two brushes.
	expect(countClass(exported, QStringLiteral("func_button")) == 0 && exported.brushes.size() == document.brushes.size() - 4
		&& nodeNamed(exported.scene, QStringLiteral("Notes")).isEmpty()
		&& std::count_if(exported.scene.nodes.cbegin(), exported.scene.nodes.cend(), [](const LevelSceneNode& node) { return node.kind == LevelSceneNodeKind::Group; }) == 1
		&& exported.scene.problem.isEmpty(), "omitted layer left out, linked duplicate kept", exported.scene.problem);
	expect(report.omittedObjects >= 3, "omitted objects counted");
	expect(bytes(document) == documentBytes && document.revision == revision && document.undoStack.size() == undo
		&& document.sourcePath == QFileInfo(sourcePath).absoluteFilePath(), "export leaves the document alone");

	request.includeOmittedLayers = true;
	request.save.overwrite = true;
	report = exportLevelMapCopy(document, request);
	expect(report.save.succeeded() && load(exportPath).brushes.size() == document.brushes.size() && report.omittedLayers.isEmpty(), "include omitted layers",
		report.save.errors.join('\n'));
	request.includeOmittedLayers = false;
	request.omitLayers = {QStringLiteral("nowhere")};
	expect(!exportLevelMapCopy(document, request).save.succeeded(), "unknown layer refused");
	request.omitLayers = {QStringLiteral("Pair")};
	expect(!exportLevelMapCopy(document, request).save.succeeded(), "groups are not layers");
	request.omitLayers.clear();
	request.save.path = document.sourcePath;
	expect(!exportLevelMapCopy(document, request).save.succeeded(), "export refuses the open map's file");
}

void cliTests(const QString& executable, const QString& folder)
{
	const auto run = [&](const QStringList& args, int code = 0) {
		QProcess process;
		process.setWorkingDirectory(folder);
		process.start(executable, QStringList{"--cli", "--json", "--settings-file", QDir(folder).filePath("settings.ini")} + args);
		const bool done = process.waitForStarted() && process.waitForFinished(120000);
		const auto out = process.readAllStandardOutput(), err = process.readAllStandardError();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code, "CLI status", args.join(' ') + '\n' + QString::fromUtf8(out + err));
		if (!done) {
			process.kill();
			process.waitForFinished();
		}
		return QJsonDocument::fromJson(out).object();
	};
	const auto dest = QDir(folder).filePath("dest.map"), other = QDir(folder).filePath("other.map");
	const auto imported = QDir(folder).filePath("cli-imported.map");
	auto report = run({"map", "import", dest, "--from", other, "--output", imported, "--dry-run"});
	expect(!QFileInfo::exists(imported) && report["preview"].toObject()["entities"] == 3, "CLI import dry run");
	report = run({"map", "import", dest, "--from", other, "--output", imported});
	expect(QFileInfo::exists(imported) && report["imported"].toArray().size() == 5 && report["written"] == true
		&& report["preview"].toObject()["renamedTargets"].toObject()["door"] == QStringLiteral("door2"), "CLI import");
	const auto importedMap = load(imported);
	expect(entityByKey(importedMap, QStringLiteral("targetname"), QStringLiteral("door2")) >= 0, "CLI import renamed names");
	report = run({"map", "import", dest, "--from", other, "--output", QDir(folder).filePath("cli-kept.map"), "--keep-target-names"});
	expect(report["preview"].toObject()["renamedTargets"].toObject().isEmpty(), "CLI import keeps names");
	QFile fake(QDir(folder).filePath("fake.bsp"));
	expect(fake.open(QIODevice::WriteOnly) && fake.write("IBSP") == 4, "fake bsp");
	fake.close();
	report = run({"map", "import", dest, "--from", fake.fileName(), "--output", QDir(folder).filePath("cli-bsp.map")}, 4);
	expect(report["message"].toString().contains(QStringLiteral("VibeMap3")) && !QFileInfo::exists(QDir(folder).filePath("cli-bsp.map")), "CLI bsp conversion failure",
		report["message"].toString());
	run({"map", "import", dest, "--output", imported, "--overwrite"}, 2);

	// Omit From Export through editor scene, then map export.
	const auto withLayer = QDir(folder).filePath("cli-layer.map");
	report = run({"editor", "scene", "create", imported, "--kind", "layer", "--name", "Notes", "--output", withLayer});
	auto layered = load(withLayer);
	const auto notes = nodeNamed(layered.scene, QStringLiteral("Notes"));
	const auto assigned = QDir(folder).filePath("cli-assigned.map"), marked = QDir(folder).filePath("cli-marked.map");
	run({"editor", "scene", "assign", withLayer, "--id", notes, "--objects", "brush:0", "--output", assigned});
	run({"editor", "scene", "omit-from-export", assigned, "--id", notes, "--omit", "maybe", "--output", marked}, 2);
	run({"editor", "scene", "omit-from-export", assigned, "--id", notes, "--omit", "true", "--output", marked});
	const auto exported = QDir(folder).filePath("cli-export.map");
	report = run({"map", "export", marked, "--output", exported});
	expect(report["omittedLayers"].toArray() == QJsonArray{"Notes"} && load(exported).brushes.size() == load(marked).brushes.size() - 1, "CLI export omits marked layers");
	report = run({"map", "export", marked, "--output", exported, "--overwrite", "--include-omitted"});
	expect(report["omittedLayers"].toArray().isEmpty() && load(exported).brushes.size() == load(marked).brushes.size(), "CLI export keeps marked layers on request");
	report = run({"map", "export", assigned, "--output", QDir(folder).filePath("cli-named.map"), "--omit-layer", "notes", "--dry-run"});
	expect(report["omittedLayers"].toArray() == QJsonArray{"Notes"} && report["written"] == false, "CLI export omits named layers");
	run({"map", "export", marked, "--output", marked, "--overwrite"}, 4);
	run({"map", "export", marked, "--output", exported, "--from", other}, 2);

	// map copy --standalone.
	const auto fragment = QDir(folder).filePath("cli-fragment.map");
	report = run({"map", "copy", other, "--object", "brush:0", "--standalone", "--worldspawn-keys", "--output", fragment});
	QFile written(fragment);
	expect(report["standalone"] == true && written.open(QIODevice::ReadOnly) && written.readAll().contains("\"message\" \"Other\""), "CLI standalone copy");
	run({"map", "copy", other, "--object", "brush:0", "--worldspawn-keys", "--output", QDir(folder).filePath("cli-bad.map")}, 2);
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "temporary folder")) {
		return 1;
	}
	importTests(temp.path());
	exportSelectionTests(temp.path());
	exportMapTests(temp.path());
	if (argc >= 2) {
		cliTests(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath(), temp.path());
	}
	std::cout << (ok ? "Map transfer smoke test passed." : "Map transfer smoke test failed.") << std::endl;
	return ok ? 0 : 1;
}
