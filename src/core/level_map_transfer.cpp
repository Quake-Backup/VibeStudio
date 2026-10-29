#include "core/level_map_transfer.h"

#include "core/compiler_runner.h"
#include "core/level_paste.h"
#include "core/level_placement_control_p.h"
#include "core/level_scene.h"
#include "core/level_scene_layers.h"
#include "core/level_texture_mapping.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <exception>

namespace vibestudio {
namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioLevelMapTransfer)
};

bool fail(QString* error, const QString& message)
{
	if (error) {
		*error = message;
	}
	return false;
}

bool textMap(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
}

bool worldspawn(const LevelMapEntity& entity)
{
	return entity.className.compare(QLatin1String("worldspawn"), Qt::CaseInsensitive) == 0;
}

// The keys whose values name entities: targetname and the target keys.
bool nameKey(const QString& key)
{
	return key.compare(QLatin1String("targetname"), Qt::CaseInsensitive) == 0 || levelMapTargetPropertyKeys().contains(key.toLower());
}

QSet<QString> entityNames(const LevelMapDocument& document)
{
	QSet<QString> names;
	for (const auto& entity : document.entities) {
		for (const auto& property : entity.properties) {
			if (nameKey(property.key) && !property.value.isEmpty()) {
				names.insert(property.value);
			}
		}
	}
	return names;
}

// door -> door1, t3 -> t4: the trailing number goes up until the name is free,
// as NetRadiant Custom's UniqueNames makes a name unique.
QString uniqueName(const QString& name, const QSet<QString>& used)
{
	static const QRegularExpression trailingDigits(QStringLiteral("^(.*?)(\\d+)$"));
	const auto match = trailingDigits.match(name);
	const QString base = match.hasMatch() ? match.captured(1) : name;
	qint64 number = match.hasMatch() ? match.captured(2).toLongLong() + 1 : 1;
	for (int attempt = 0; attempt < 1000000; ++attempt, ++number) {
		const QString candidate = base + QString::number(number);
		if (!used.contains(candidate)) {
			return candidate;
		}
	}
	return {};
}

bool samePath(const QString& left, const QString& right)
{
	if (left.isEmpty() || right.isEmpty()) {
		return false;
	}
	const auto normalized = [](const QString& path) {
		const QFileInfo info(path);
		return QDir::cleanPath(info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath());
	};
#ifdef Q_OS_WIN
	return normalized(left).compare(normalized(right), Qt::CaseInsensitive) == 0;
#else
	return normalized(left) == normalized(right);
#endif
}

// The UTF-8 size of `text`, counted without making a UTF-8 copy.
qint64 utf8Bytes(const QString& text)
{
	qint64 bytes = 0;
	const auto* data = text.utf16();
	const qsizetype size = text.size();
	for (qsizetype i = 0; i < size; ++i) {
		const char16_t c = data[i];
		if (c < 0x80) {
			bytes += 1;
		} else if (c < 0x800) {
			bytes += 2;
		} else if (QChar::isHighSurrogate(c) && i + 1 < size && QChar::isLowSurrogate(data[i + 1])) {
			bytes += 4;
			++i;
		} else {
			bytes += 3;
		}
	}
	return bytes;
}

void applyRenames(LevelMapDocument* document, const QMap<QString, QString>& renames)
{
	if (renames.isEmpty()) {
		return;
	}
	// Pasted entities are written again from their keys, so the values alone
	// change; the imported brushes keep their own text.
	for (auto& entity : document->entities) {
		for (auto& property : entity.properties) {
			if (nameKey(property.key) && renames.contains(property.value)) {
				property.value = renames.value(property.value);
			}
		}
	}
}

} // namespace

// ---- Import Map ------------------------------------------------------------

bool levelMapImportNeedsConversion(const QString& path)
{
	return QFileInfo(path).suffix().compare(QLatin1String("bsp"), Qt::CaseInsensitive) == 0;
}

// Workflow reference: NetRadiant Custom Map_ImportFile's q3map2 -convert
// fallback at 449778b3 (GPL-2.0-or-later). Original implementation.
CompilerCommandRequest levelMapImportConversionRequest(const QString& bspPath, const QString& outputPath, const QString& dialect)
{
	CompilerCommandRequest request;
	request.profileId = QStringLiteral("vibemap3-convert");
	request.inputPath = QFileInfo(bspPath).absoluteFilePath();
	request.outputPath = QFileInfo(outputPath).absoluteFilePath();
	request.workingDirectory = QFileInfo(bspPath).absolutePath();
	const QString format = dialect == QLatin1String("valve220") ? QStringLiteral("map_220")
		: (dialect == QLatin1String("brushDef") || dialect == QLatin1String("brushDef3")) ? QStringLiteral("map_bp")
		: QStringLiteral("map");
	request.extraArguments = {QStringLiteral("-format"), format, QStringLiteral("-o"), request.outputPath};
	return request;
}

namespace {
// VibeStudio game keys and aliases with the VibeMap3 -game profile and the BSP
// header that profile reads (tools/quake3/q3map2/games.cpp in VibeMap3).
struct BspGame {
	const char* key;
	const char* game;
	const char* ident;
	int version;
};
// The first entry for each header is the game a project without one of
// these games converts for.
constexpr BspGame kBspGames[] = {
	{"quake3", "quake3", "IBSP", 46}, {"q3", "quake3", "IBSP", 46}, {"q3a", "quake3", "IBSP", 46}, {"openarena", "quake3", "IBSP", 46},
	{"rtcw", "wolf", "IBSP", 47}, {"wolf", "wolf", "IBSP", 47}, {"wolfsp", "wolf", "IBSP", 47}, {"wolfmp", "wolf", "IBSP", 47},
	{"et", "et", "IBSP", 47}, {"wolfet", "et", "IBSP", 47}, {"etut", "etut", "IBSP", 47},
	{"quakelive", "quakelive", "IBSP", 47}, {"ql", "quakelive", "IBSP", 47},
	{"ef", "ef", "IBSP", 46}, {"stvef", "ef", "IBSP", 46},
	{"jka", "ja", "RBSP", 1}, {"ja", "ja", "RBSP", 1}, {"jk2", "jk2", "RBSP", 1}, {"sof2", "sof2", "RBSP", 1},
	{"warsow", "qfusion", "FBSP", 1}, {"qfusion", "qfusion", "FBSP", 1},
	{"xonotic", "xonotic", "IBSP", 46}, {"nexuiz", "nexuiz", "IBSP", 46}, {"tremulous", "tremulous", "IBSP", 46},
	{"unvanquished", "unvanquished", "IBSP", 46}, {"darkplaces", "darkplaces", "IBSP", 46}, {"reaction", "reaction", "IBSP", 46},
};
} // namespace

// Workflow reference: NetRadiant Custom Map_ImportFile's -game, -fs_basepath
// and -fs_game at 449778b3 (GPL-2.0-or-later). Original implementation.
QString levelMapBspConversionGame(const QString& bspPath, const QString& gameKey, QString* error)
{
	if (error) {
		error->clear();
	}
	const QString name = QFileInfo(bspPath).fileName();
	QFile file(bspPath);
	if (!file.open(QIODevice::ReadOnly)) {
		fail(error, Text::tr("VibeMap3 cannot convert %1: the file cannot be read.").arg(name));
		return {};
	}
	const QByteArray header = file.read(8);
	if (header.size() < 8) {
		fail(error, Text::tr("VibeMap3 cannot convert %1: it is too short to be a compiled map.").arg(name));
		return {};
	}
	const QByteArray ident = header.left(4);
	const auto version = static_cast<qint32>(qFromLittleEndian<quint32>(header.constData() + 4));
	const auto first = static_cast<qint32>(qFromLittleEndian<quint32>(header.constData()));
	if (ident == "IBSP" && version == 38) {
		fail(error, Text::tr("VibeMap3 converts Quake III-family BSPs, and %1 is a Quake II BSP. Import its source .map instead.").arg(name));
		return {};
	}
	if (first == 29 || first == 30 || ident == "BSP2" || ident == "2PSB") {
		fail(error, Text::tr("VibeMap3 converts Quake III-family BSPs, and %1 is a Quake BSP. Import its source .map instead.").arg(name));
		return {};
	}
	const QString wanted = gameKey.trimmed().toLower();
	const char* fallback = nullptr;
	for (const auto& game : kBspGames) {
		if (ident != game.ident || version != game.version) {
			continue;
		}
		if (wanted == QLatin1String(game.key)) {
			return QString::fromLatin1(game.game);
		}
		if (!fallback) {
			fallback = game.game;
		}
	}
	if (!fallback) {
		fail(error, Text::tr("VibeMap3 cannot convert %1: its header (%2, version %3) is not a BSP format VibeMap3 reads.")
						.arg(name, QString::fromLatin1(ident.toHex()), QString::number(version)));
		return {};
	}
	return QString::fromLatin1(fallback);
}

bool prepareLevelMapImportConversion(const QString& bspPath, const QString& outputPath, const QString& dialect,
	const LevelMapBspConversionContext& context, CompilerCommandRequest* request, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!request) {
		return false;
	}
	const auto game = levelMapBspConversionGame(bspPath, context.gameKey, error);
	if (game.isEmpty()) {
		return false;
	}
	auto prepared = levelMapImportConversionRequest(bspPath, outputPath, dialect);
	prepared.extraArguments << QStringLiteral("-game") << game;
	if (!context.basePath.trimmed().isEmpty()) {
		prepared.extraArguments << QStringLiteral("-fs_basepath") << QDir::cleanPath(QFileInfo(context.basePath).absoluteFilePath());
	}
	if (!context.gameDirectory.trimmed().isEmpty()) {
		prepared.extraArguments << QStringLiteral("-fs_game") << context.gameDirectory.trimmed();
	}
	prepared.workspaceRootPath = context.workspaceRootPath;
	prepared.extraSearchPaths = context.extraSearchPaths;
	prepared.executableOverrides = context.executableOverrides;
	*request = std::move(prepared);
	return true;
}

QString levelMapImportConversionProblem(const CompilerRunResult& result, const QString& convertedPath)
{
	if (result.started && result.exitCode == 0 && QFileInfo(convertedPath).isFile()) {
		return {};
	}
	if (!result.error.isEmpty()) {
		return result.error;
	}
	if (!result.plan.errors.isEmpty()) {
		return result.plan.errors.join(QLatin1Char('\n'));
	}
	if (!result.started) {
		return Text::tr("VibeMap3 did not start. Build it or set its path in Compilers.");
	}
	// Error output first; q3map2-family tools print their errors to standard
	// output, so its error lines come next.
	QStringList lines;
	const QStringList outputs{result.stderrText, result.stdoutText};
	for (int source = 0; source < outputs.size() && lines.isEmpty(); ++source) {
		const auto all = outputs.at(source).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
		for (const auto& line : all) {
			const auto trimmed = line.trimmed();
			if (!trimmed.isEmpty() && (source == 0 || trimmed.contains(QLatin1String("error"), Qt::CaseInsensitive))) {
				lines << trimmed;
			}
		}
	}
	const auto code = result.exitCode;
	// Windows reports a process that failed to load or crashed as an NTSTATUS.
	const QString exit = code < 0 || code > 0xFFFF
		? QStringLiteral("0x") + QString::number(static_cast<quint32>(code), 16).toUpper().rightJustified(8, QLatin1Char('0'))
		: QString::number(code);
	if (code != 0 && lines.isEmpty()) {
		return Text::tr("VibeMap3 exited with code %1 without printing an error. Check that the VibeMap3 in Compilers runs on this system.").arg(exit);
	}
	if (code != 0) {
		return Text::tr("VibeMap3 exited with code %1: %2").arg(exit, lines.mid(std::max<qsizetype>(0, lines.size() - 3)).join(QLatin1Char('\n')));
	}
	return Text::tr("VibeMap3 finished but wrote no map to %1.").arg(QDir::toNativeSeparators(convertedPath));
}

bool readLevelMapImport(const QString& path, const QString& engineHint, LevelMapDocument* imported, QString* error,
	const std::function<bool()>& isCancelled, const std::function<void(LevelMapLoadPhase, qint64, qint64)>& progress)
{
	if (error) {
		error->clear();
	}
	if (!imported) {
		return false;
	}
	const QFileInfo info(path);
	if (levelMapImportNeedsConversion(path)) {
		return fail(error, Text::tr("Convert the compiled .bsp to a .map with VibeMap3 before importing it."));
	}
	if (!info.isFile()) {
		return fail(error, Text::tr("Choose an existing .map file to import."));
	}
	if (info.suffix().compare(QLatin1String("wad"), Qt::CaseInsensitive) == 0) {
		return fail(error, Text::tr("Import Map reads Quake-family .map files; Doom WADs cannot be imported into a map."));
	}
	LevelMapLoadRequest request;
	request.path = info.absoluteFilePath();
	request.engineHint = engineHint;
	request.isCancelled = isCancelled;
	request.progress = progress;
	LevelMapDocument document;
	QString loadError;
	if (!loadLevelMap(request, &document, &loadError)) {
		return fail(error, loadError.isEmpty() ? Text::tr("The map could not be read.") : loadError);
	}
	if (!textMap(document)) {
		return fail(error, Text::tr("Import Map reads Quake-family .map files; Doom WADs cannot be imported into a map."));
	}
	for (const auto& issue : document.issues) {
		if (issue.severity == LevelMapIssueSeverity::Error) {
			return fail(error, Text::tr("The imported map could not be read: %1").arg(issue.message));
		}
	}
	*imported = std::move(document);
	return true;
}

QMap<QString, QString> levelMapImportTargetRenames(const LevelMapDocument& destination, const LevelMapDocument& imported)
{
	QMap<QString, QString> renames;
	const auto existing = entityNames(destination);
	const auto incoming = entityNames(imported);
	auto used = existing;
	used.unite(incoming);
	auto names = incoming.values();
	std::sort(names.begin(), names.end());
	for (const auto& name : std::as_const(names)) {
		if (!existing.contains(name)) {
			continue;
		}
		const auto renamed = uniqueName(name, used);
		if (!renamed.isEmpty()) {
			used.insert(renamed);
			renames.insert(name, renamed);
		}
	}
	return renames;
}

LevelMapImportPreview previewLevelMapImport(const LevelMapDocument& destination, const LevelMapDocument& imported,
	const LevelMapImportOptions& options, qint64 sourceBytes)
{
	LevelMapImportPreview preview;
	preview.path = imported.sourcePath;
	preview.bytes = sourceBytes >= 0 ? sourceBytes : utf8Bytes(imported.originalText);
	preview.engineFamily = imported.engineFamily;
	preview.format = levelMapFormatId(imported.format);
	preview.dialect = levelMapBrushDialect(imported);
	for (const auto& entity : imported.entities) {
		if (!worldspawn(entity)) {
			++preview.entities;
			continue;
		}
		for (const auto& property : entity.properties) {
			if (property.key.compare(QLatin1String("classname"), Qt::CaseInsensitive) != 0) {
				++preview.worldspawnKeys;
			}
		}
	}
	preview.brushes = static_cast<int>(imported.brushes.size());
	preview.patches = static_cast<int>(imported.patches.size());
	for (const auto& node : imported.scene.nodes) {
		if (node.kind == LevelSceneNodeKind::Layer) {
			++preview.layers;
		} else {
			++preview.groups;
			if (!node.linkId.isEmpty()) {
				++preview.linkedGroups;
			}
		}
	}
	if (options.renameTargets) {
		preview.renamedTargets = levelMapImportTargetRenames(destination, imported);
	}
	const auto destinationDialect = levelMapBrushDialect(destination);
	if (!textMap(destination)) {
		preview.problem = Text::tr("Open a Quake-family .map before importing another map into it.");
	} else if (!textMap(imported)) {
		preview.problem = Text::tr("Import Map reads Quake-family .map files; Doom WADs cannot be imported into a map.");
	} else if (preview.dialect == QLatin1String("mixed")) {
		preview.problem = Text::tr("The imported map mixes brush texture formats. Convert it to one format first.");
	} else if (!preview.dialect.isEmpty() && !destinationDialect.isEmpty() && destinationDialect != QLatin1String("mixed")
		&& preview.dialect != destinationDialect) {
		preview.problem = Text::tr("The imported map writes its brushes as %1 and the open map as %2. Convert one to the other's format first.")
							  .arg(preview.dialect, destinationDialect);
	} else if (preview.patches > 0 && destination.format != LevelMapFormat::Quake3Map) {
		preview.problem = Text::tr("The imported map has patches, which need a Quake III-family map.");
	} else if (preview.entities + preview.brushes + preview.patches == 0) {
		preview.problem = Text::tr("The imported map holds no brushes, patches or entities to import.");
	}
	if (!imported.engineFamily.isEmpty() && !destination.engineFamily.isEmpty()
		&& imported.engineFamily.compare(destination.engineFamily, Qt::CaseInsensitive) != 0) {
		preview.warnings << Text::tr("The imported map is for %1 and the open map for %2; check its entities and textures after importing.")
								.arg(imported.engineFamily, destination.engineFamily);
	}
	if (preview.worldspawnKeys > 0) {
		const int keys = preview.worldspawnKeys;
		preview.warnings << Text::tr("%n worldspawn key(s) of the imported map are left out; the open map keeps its own.", nullptr, keys);
	}
	if (preview.layers > 0) {
		const int layers = preview.layers;
		preview.warnings << Text::tr("%n layer(s) of the imported map are not recreated; everything joins the current layer or open group.", nullptr, layers);
	}
	if (!imported.scene.problem.isEmpty() || !imported.scene.opaqueMetadata.isEmpty()) {
		preview.warnings << Text::tr("The imported map's layers and groups could not be read, so its objects arrive ungrouped.");
	}
	if (!preview.renamedTargets.isEmpty()) {
		const int renamed = static_cast<int>(preview.renamedTargets.size());
		preview.warnings << Text::tr("%n entity name(s) the open map already uses are renamed so imported links stay separate.", nullptr, renamed);
	}
	return preview;
}

bool importLevelMap(LevelMapDocument* destination, const LevelMapDocument& imported, const LevelMapImportOptions& options,
	LevelMapImportReport* report, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!destination) {
		return fail(error, Text::tr("Open a Quake-family .map before importing another map into it."));
	}
	auto preview = previewLevelMapImport(*destination, imported, options);
	if (!preview.importable()) {
		return fail(error, preview.problem);
	}
	detail::placementCheckpoint(LevelPlacementPhase::Inserting);
	auto source = imported;
	applyRenames(&source, preview.renamedTargets);
	auto candidate = *destination;
	LevelPastePlacement placement;
	placement.mode = LevelPasteMode::Offset;
	if (!pasteLevelMapDocumentAt(&candidate, source, placement, {false, false}, error)) {
		return false;
	}
	if (!candidate.undoStack.isEmpty()) {
		const auto name = QFileInfo(imported.sourcePath).fileName();
		auto& command = candidate.undoStack.last();
		command.description = Text::tr("Import %1").arg(name);
		command.undoDescription = Text::tr("Remove imported %1").arg(name);
	}
	if (report) {
		report->preview = std::move(preview);
		report->inserted = candidate.selection;
	}
	*destination = std::move(candidate);
	return true;
}

QJsonObject levelMapImportPreviewJson(const LevelMapImportPreview& preview)
{
	QJsonObject renamed;
	for (auto it = preview.renamedTargets.cbegin(); it != preview.renamedTargets.cend(); ++it) {
		renamed.insert(it.key(), it.value());
	}
	return {{QStringLiteral("path"), preview.path},
			{QStringLiteral("bytes"), preview.bytes},
			{QStringLiteral("engineFamily"), preview.engineFamily},
			{QStringLiteral("format"), preview.format},
			{QStringLiteral("dialect"), preview.dialect},
			{QStringLiteral("entities"), preview.entities},
			{QStringLiteral("brushes"), preview.brushes},
			{QStringLiteral("patches"), preview.patches},
			{QStringLiteral("layers"), preview.layers},
			{QStringLiteral("groups"), preview.groups},
			{QStringLiteral("linkedGroups"), preview.linkedGroups},
			{QStringLiteral("worldspawnKeysLeftOut"), preview.worldspawnKeys},
			{QStringLiteral("renamedTargets"), renamed},
			{QStringLiteral("warnings"), QJsonArray::fromStringList(preview.warnings)},
			{QStringLiteral("importable"), preview.importable()},
			{QStringLiteral("problem"), preview.problem}};
}

// ---- Export Selection as Map -----------------------------------------------

QString levelMapSelectionExportText(const LevelMapDocument& document, bool worldspawnKeys, QString* error)
{
	LevelMapSelectionTextOptions options;
	options.worldspawnWrapper = true;
	options.worldspawnKeys = worldspawnKeys;
	options.fileOutput = true;
	return levelMapSelectionText(document, options, error);
}

LevelMapSelectionExportReport exportLevelMapSelection(const LevelMapDocument& document, const LevelMapSelectionExportRequest& request)
{
	LevelMapSelectionExportReport report;
	report.save.sourcePath = document.sourcePath;
	report.save.mapName = document.mapName;
	report.save.format = document.format;
	report.save.outputPath = QFileInfo(request.save.path).absoluteFilePath();
	report.save.dryRun = request.save.dryRun;
	if (!textMap(document) || document.selection.isEmpty()) {
		report.save.errors << Text::tr("Select brushes, patches or entities of a Quake-family map to export them.");
		return report;
	}
	if (samePath(request.save.path, document.sourcePath)) {
		report.save.errors << Text::tr("An export cannot replace the open map. Choose a different file name to export to.");
		return report;
	}
	QString error, exported;
	LevelPlacementControl control;
	control.isCancelled = request.save.isCancelled;
	try {
		detail::PlacementControlScope scope(control);
		exported = levelMapSelectionExportText(document, request.worldspawnKeys, &error);
	} catch (const detail::PlacementCancelled&) {
		report.cancelled = true;
		report.save.errors << Text::tr("The export was cancelled; nothing was written.");
		return report;
	} catch (const std::exception&) {
		report.save.errors << Text::tr("The selection could not be prepared for export.");
		return report;
	}
	if (exported.isEmpty()) {
		report.save.errors << (error.isEmpty() ? Text::tr("The selection could not be prepared for export.") : error);
		return report;
	}
	const auto bytes = exported.toUtf8();
	auto saved = writeLevelMapBytes(bytes, request.save, document.sourcePath);
	saved.sourcePath = document.sourcePath;
	saved.mapName = document.mapName;
	saved.format = document.format;
	report.save = std::move(saved);
	report.objects = static_cast<int>(document.selection.size());
	report.bytes = bytes.size();
	return report;
}

// ---- Export Map ------------------------------------------------------------

bool levelMapExportOmittedLayers(const LevelMapDocument& document, const LevelMapExportRequest& request, QStringList* layerIds, QString* error)
{
	if (error) {
		error->clear();
	}
	QStringList ids;
	for (const auto& node : document.scene.nodes) {
		if (!request.includeOmittedLayers && levelSceneOmittedFromExport(node)) {
			ids << node.id;
		}
	}
	for (const auto& wanted : request.omitLayers) {
		const auto name = wanted.trimmed();
		const LevelSceneNode* found = nullptr;
		for (const auto& node : document.scene.nodes) {
			if (node.kind == LevelSceneNodeKind::Layer
				&& (node.id.compare(name, Qt::CaseInsensitive) == 0 || node.name.compare(name, Qt::CaseInsensitive) == 0)) {
				found = &node;
				break;
			}
		}
		if (!found) {
			return fail(error, Text::tr("No layer is called %1. Name a layer by its name or id; the Default layer is always exported.").arg(name));
		}
		if (!ids.contains(found->id)) {
			ids << found->id;
		}
	}
	if (!ids.isEmpty() && !textMap(document)) {
		return fail(error, Text::tr("Layers can be left out of Quake-family .map exports only."));
	}
	// Scene order, so reports list layers as the Scene panel does.
	QStringList ordered;
	for (const auto& node : document.scene.nodes) {
		if (ids.contains(node.id)) {
			ordered << node.id;
		}
	}
	if (layerIds) {
		*layerIds = ordered;
	}
	return true;
}

bool prepareLevelMapExport(const LevelMapDocument& document, const LevelMapExportRequest& request, LevelMapDocument* copy,
	LevelMapExportReport* report, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!copy || document.format == LevelMapFormat::Unknown) {
		return fail(error, Text::tr("Open a map before exporting it."));
	}
	QStringList omitted;
	if (!levelMapExportOmittedLayers(document, request, &omitted, error)) {
		return false;
	}
	auto result = document;
	result.undoStack.clear();
	result.redoStack.clear();
	QStringList names;
	int removedObjects = 0;
	if (!omitted.isEmpty()) {
		if (!result.scene.opaqueMetadata.isEmpty() || !result.scene.problem.isEmpty()) {
			return fail(error, Text::tr("Reset the unrecognized scene metadata before leaving layers out of an export."));
		}
		const QSet<QString> layers(omitted.cbegin(), omitted.cend());
		const auto nodeLayers = levelSceneNodeLayers(result.scene);
		QHash<QString, bool> locks;
		for (auto& node : result.scene.nodes) {
			locks.insert(node.id, node.locked);
			// Only this throwaway copy changes: nothing here is locked and the
			// left-out groups no longer pass edits to their linked groups.
			node.locked = false;
			if (layers.contains(nodeLayers.value(node.id))) {
				node.linkId.clear();
				node.linkObjects.clear();
			}
			if (layers.contains(node.id)) {
				names << node.name;
			}
		}
		const auto objectLayers = levelSceneObjectLayers(result, result.scene);
		QSet<int> entities;
		QVector<LevelMapSelectionRef> doomed;
		for (auto it = objectLayers.cbegin(); it != objectLayers.cend(); ++it) {
			if (!layers.contains(it.value())) {
				continue;
			}
			const auto colon = it.key().indexOf(QLatin1Char(':'));
			const LevelMapSelectionRef ref{levelMapSelectionKindFromId(it.key().left(colon)), it.key().mid(colon + 1).toInt()};
			if (ref.kind == LevelMapSelectionKind::Entity) {
				entities.insert(ref.objectId);
			}
			doomed << ref;
		}
		// A brush entity takes its brushes and patches with it.
		QSet<int> ownedBrushes, ownedPatches;
		for (const auto& brush : result.brushes) {
			if (entities.contains(brush.entityId)) {
				ownedBrushes.insert(brush.id);
			}
		}
		for (const auto& patch : result.patches) {
			if (entities.contains(patch.entityId)) {
				ownedPatches.insert(patch.id);
			}
		}
		doomed.removeIf([&](const LevelMapSelectionRef& ref) {
			return (ref.kind == LevelMapSelectionKind::QuakeBrush && ownedBrushes.contains(ref.objectId))
				|| (ref.kind == LevelMapSelectionKind::QuakePatch && ownedPatches.contains(ref.objectId));
		});
		std::sort(doomed.begin(), doomed.end(), [](const LevelMapSelectionRef& a, const LevelMapSelectionRef& b) {
			return a.kind != b.kind ? static_cast<int>(a.kind) < static_cast<int>(b.kind) : a.objectId < b.objectId;
		});
		removedObjects = static_cast<int>(doomed.size() + ownedBrushes.size() + ownedPatches.size());
		if (!doomed.isEmpty()) {
			result.selection.clear();
			if (!deleteLevelMapObjects(&result, doomed, error)) {
				return false;
			}
		}
		const auto remainingLayers = levelSceneNodeLayers(result.scene);
		result.scene.nodes.removeIf([&](const LevelSceneNode& node) {
			return layers.contains(node.id) || layers.contains(remainingLayers.value(node.id));
		});
		for (auto& node : result.scene.nodes) {
			node.locked = locks.value(node.id, node.locked);
		}
		const auto objects = levelSceneObjects(result);
		const QSet<QString> remaining(objects.cbegin(), objects.cend());
		for (auto it = result.scene.protectedProperties.begin(); it != result.scene.protectedProperties.end();) {
			it = remaining.contains(it.key()) ? std::next(it) : result.scene.protectedProperties.erase(it);
		}
		if (!validateLevelScene(result, result.scene, error)) {
			return false;
		}
		result.selection.clear();
		result.undoStack.clear();
		result.redoStack.clear();
	}
	if (report) {
		report->omittedLayers = names;
		report->omittedObjects = removedObjects;
	}
	*copy = std::move(result);
	return true;
}

LevelMapExportReport exportLevelMapCopy(const LevelMapDocument& document, const LevelMapExportRequest& request)
{
	LevelMapExportReport report;
	report.save.sourcePath = document.sourcePath;
	report.save.mapName = document.mapName;
	report.save.format = document.format;
	report.save.outputPath = QFileInfo(request.save.path).absoluteFilePath();
	report.save.dryRun = request.save.dryRun;
	if (request.save.path.trimmed().isEmpty()) {
		report.save.errors << Text::tr("Choose a file to export the map to.");
		return report;
	}
	// TrenchBroom refuses to export over the open document; so does VibeStudio.
	if (samePath(request.save.path, document.sourcePath)) {
		report.save.errors << Text::tr("An export cannot replace the open map. Choose a different file name to export to.");
		return report;
	}
	LevelMapDocument copy;
	QString error;
	LevelPlacementControl control;
	control.isCancelled = request.save.isCancelled;
	try {
		detail::PlacementControlScope scope(control);
		if (!prepareLevelMapExport(document, request, &copy, &report, &error)) {
			report.save.errors << error;
			return report;
		}
	} catch (const detail::PlacementCancelled&) {
		report.cancelled = true;
		report.save.errors << Text::tr("The export was cancelled; nothing was written.");
		return report;
	} catch (const std::exception&) {
		report.save.errors << Text::tr("The map could not be prepared for export.");
		return report;
	}
	const auto omittedLayers = report.omittedLayers;
	const int omittedObjects = report.omittedObjects;
	report.save = writeLevelDocument(copy, request.save);
	report.omittedLayers = omittedLayers;
	report.omittedObjects = omittedObjects;
	return report;
}

QJsonObject levelMapExportReportJson(const LevelMapExportReport& report)
{
	return {{QStringLiteral("output"), report.save.outputPath},
			{QStringLiteral("written"), report.save.written},
			{QStringLiteral("dryRun"), report.save.dryRun},
			{QStringLiteral("backup"), report.save.backupPath},
			{QStringLiteral("sha256"), QString::fromLatin1(report.save.contentHash.toHex())},
			{QStringLiteral("omittedLayers"), QJsonArray::fromStringList(report.omittedLayers)},
			{QStringLiteral("omittedObjects"), report.omittedObjects},
			{QStringLiteral("warnings"), QJsonArray::fromStringList(report.save.warnings)}};
}

} // namespace vibestudio
