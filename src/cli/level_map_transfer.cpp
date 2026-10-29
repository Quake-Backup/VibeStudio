#include "cli/level_map_transfer.h"

#include "core/compiler_runner.h"
#include "core/game_installation.h"
#include "core/level_document.h"
#include "core/level_map_obj_export.h"
#include "core/level_map_transfer.h"
#include "core/level_materials.h"
#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_trenchbroom.h"
#include "core/level_texture_mapping.h"
#include "core/package_archive.h"
#include "core/project_manifest.h"
#include "core/release_plan.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <QTemporaryDir>

namespace vibestudio::cli {
namespace {
struct Text {
	Q_DECLARE_TR_FUNCTIONS(LevelMapTransferCli)
};

struct ParsedArguments {
	QSet<QString> seen;
	QHash<QString, QString> values;
	QHash<QString, QStringList> repeated;
	QStringList positional;
};

// Flags take no value; options take one, as --key value or --key=value, and
// only the `repeatable` ones may be given more than once.
bool parseArguments(const QStringList& arguments, const QSet<QString>& flags, const QSet<QString>& options, const QSet<QString>& repeatable,
	ParsedArguments* parsed, QString* error)
{
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const auto arg = arguments[i];
		if (!arg.startsWith('-')) {
			parsed->positional << arg;
			continue;
		}
		const auto equal = arg.indexOf('=');
		const auto key = equal < 0 ? arg : arg.left(equal);
		if (parsed->seen.contains(key) && !repeatable.contains(key)) {
			*error = Text::tr("Repeated option: %1").arg(key);
			return false;
		}
		parsed->seen.insert(key);
		if (flags.contains(key) && equal < 0) {
			continue;
		}
		if (!options.contains(key)) {
			*error = Text::tr("Unexpected option: %1").arg(arg);
			return false;
		}
		QString value;
		if (equal >= 0) {
			value = arg.mid(equal + 1);
		} else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith('-')) {
			value = arguments[++i];
		}
		if (value.trimmed().isEmpty()) {
			*error = Text::tr("Missing value for %1.").arg(key);
			return false;
		}
		if (repeatable.contains(key)) {
			parsed->repeated[key] << value;
		} else {
			parsed->values.insert(key, value);
		}
	}
	return true;
}

// The compiler context `compiler run` uses (user overrides, then a project's)
// and the game a .bsp is converted for: --game, else the project's game, with
// its installation's folders when the installation is for that game.
LevelMapBspConversionContext conversionContext(const QString& workspaceRoot, const QString& game)
{
	const StudioSettings settings(StudioSettings::AccessMode::ReadOnly);
	LevelMapBspConversionContext context;
	context.workspaceRootPath = workspaceRoot;
	context.executableOverrides = settings.compilerToolPathOverrides();
	ProjectManifest manifest;
	const bool hasManifest = !workspaceRoot.trimmed().isEmpty() && loadProjectManifest(workspaceRoot, &manifest);
	GameInstallationProfile installation;
	bool hasInstallation = false;
	if (hasManifest) {
		context.extraSearchPaths = effectiveProjectCompilerSearchPaths(manifest, context.extraSearchPaths);
		context.executableOverrides = effectiveProjectCompilerToolOverrides(manifest, context.executableOverrides);
		hasInstallation = releaseInstallationFor(manifest, settings.gameInstallations(), settings.selectedGameInstallationId(), &installation);
		context.gameKey = effectiveProjectGameKey(manifest, hasInstallation ? &installation : nullptr);
	}
	if (!game.trimmed().isEmpty()) {
		context.gameKey = normalizedGameKey(game);
	}
	if (hasInstallation && normalizedGameKey(installation.gameKey) == context.gameKey) {
		context.basePath = installation.rootPath;
		context.gameDirectory = defaultGameDirectory(installation);
	}
	return context;
}
} // namespace

LevelMapTransferCliResult runLevelMapTransfer(const QStringList& arguments)
{
	LevelMapTransferCliResult result;
	result.payload = {{"written", false}};
	const auto failure = [&](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite", "--keep-target-names", "--include-omitted"};
	const QSet<QString> options{"--from", "--output", "--context", "--omit-layer", "--scene-format", "--map-name", "--workspace-root", "--game",
		"--settings-file", "--locale", "--catalog-root"};
	ParsedArguments parsed;
	QString parseError;
	if (!parseArguments(arguments, flags, options, {QStringLiteral("--omit-layer")}, &parsed, &parseError)) {
		return failure(2, parseError);
	}
	const auto& seen = parsed.seen;
	const auto& values = parsed.values;
	const auto& positional = parsed.positional;
	const auto omitLayers = parsed.repeated.value(QStringLiteral("--omit-layer"));
	if (positional.size() != 3 || !QStringList{"map", "maps", "level"}.contains(positional[0])
		|| (positional[1] != QLatin1String("import") && positional[1] != QLatin1String("export"))) {
		return failure(2, Text::tr("Expected map import <map> --from <other.map|other.bsp> --output <path>, or map export <map> --output <path>."));
	}
	const bool importing = positional[1] == QLatin1String("import");
	const auto importOnly = {QStringLiteral("--from"), QStringLiteral("--context"), QStringLiteral("--keep-target-names"), QStringLiteral("--workspace-root"),
		QStringLiteral("--game")};
	const auto exportOnly = {QStringLiteral("--omit-layer"), QStringLiteral("--include-omitted"), QStringLiteral("--scene-format")};
	for (const auto& key : importing ? exportOnly : importOnly) {
		if (seen.contains(key)) {
			return failure(2, Text::tr("%1 does not apply to map %2.").arg(key, positional[1]));
		}
	}
	if (!values.contains("--output") || (importing && !values.contains("--from"))) {
		return failure(2, importing ? Text::tr("map import requires --from <other.map|other.bsp> and --output <path>.")
								 : Text::tr("map export requires --output <path>."));
	}
	const auto source = QFileInfo(positional[2]).absoluteFilePath();
	const auto output = QFileInfo(values.value("--output")).absoluteFilePath();
	const bool dryRun = seen.contains("--dry-run"), overwrite = seen.contains("--overwrite");
	result.payload.insert("map", source);
	result.payload.insert("output", output);
	result.payload.insert("dryRun", dryRun);
	if (!QFileInfo(source).isFile()) {
		return failure(3, Text::tr("The map does not exist: %1").arg(source));
	}
	LevelMapLoadRequest load;
	load.path = source;
	load.mapName = values.value("--map-name");
	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(load, &document, &error)) {
		return failure(4, error);
	}

	if (!importing) {
		LevelMapExportRequest request;
		request.save.path = output;
		request.save.dryRun = dryRun;
		request.save.overwrite = overwrite;
		request.omitLayers = omitLayers;
		request.includeOmittedLayers = seen.contains("--include-omitted");
		if (values.contains("--scene-format")) {
			LevelSceneStorage storage = LevelSceneStorage::VibeStudio;
			if (!levelSceneStorageFromId(values.value("--scene-format"), &storage) || document.format == LevelMapFormat::DoomWad) {
				return failure(2, Text::tr("--scene-format takes vibestudio or trenchbroom, for Quake-family maps."));
			}
			request.save.sceneStorage = storage;
		}
		const auto report = exportLevelMapCopy(document, request);
		const auto json = levelMapExportReportJson(report);
		for (auto it = json.begin(); it != json.end(); ++it) {
			result.payload.insert(it.key(), it.value());
		}
		if (!report.save.succeeded()) {
			return failure(4, report.save.errors.join('\n'));
		}
		const int layers = static_cast<int>(report.omittedLayers.size());
		result.lines << (dryRun ? Text::tr("Would export the map to %1.").arg(output) : Text::tr("Exported the map to %1.").arg(output));
		if (layers > 0) {
			result.lines << Text::tr("Left out %n layer(s): %1.", nullptr, layers).arg(report.omittedLayers.join(QStringLiteral(", ")));
		}
		result.lines += report.save.warnings;
		return result;
	}

	if (values.contains("--context")) {
		const auto context = values.value("--context");
		const auto* node = levelSceneNode(document.scene, context);
		if (context == QLatin1String("default")) {
			document.activeSceneNode.clear();
		} else if (!node) {
			return failure(2, Text::tr("The import destination layer or group does not exist."));
		} else if (node->kind == LevelSceneNodeKind::Group) {
			if (!openLevelSceneGroup(&document, context, &error)) {
				return failure(4, error);
			}
		} else {
			document.activeSceneNode = context;
		}
	}
	auto from = QFileInfo(values.value("--from")).absoluteFilePath();
	result.payload.insert("from", from);
	if (!QFileInfo(from).isFile()) {
		return failure(3, Text::tr("The map to import does not exist: %1").arg(from));
	}
	QTemporaryDir conversion;
	if (levelMapImportNeedsConversion(from)) {
		if (!conversion.isValid()) {
			return failure(4, Text::tr("Could not create a folder for the converted map."));
		}
		CompilerCommandRequest convert;
		const auto converted = conversion.filePath(QFileInfo(from).completeBaseName() + QStringLiteral("_converted.map"));
		if (!prepareLevelMapImportConversion(from, converted, levelMapBrushDialect(document),
				conversionContext(values.value("--workspace-root"), values.value("--game")), &convert, &error)) {
			return failure(4, error);
		}
		CompilerRunRequest run;
		run.command = convert;
		const auto ran = runCompilerCommand(run);
		result.payload.insert("conversion", QJsonObject{{"commandLine", ran.plan.commandLine}, {"exitCode", ran.exitCode}});
		const auto problem = levelMapImportConversionProblem(ran, convert.outputPath);
		if (!problem.isEmpty()) {
			return failure(4, Text::tr("VibeMap3 could not convert %1 to a map: %2").arg(from, problem));
		}
		from = convert.outputPath;
	}
	LevelMapDocument imported;
	if (!readLevelMapImport(from, document.engineFamily, &imported, &error)) {
		return failure(4, error);
	}
	LevelMapImportOptions importOptions;
	importOptions.renameTargets = !seen.contains("--keep-target-names");
	const auto preview = previewLevelMapImport(document, imported, importOptions);
	result.payload.insert("preview", levelMapImportPreviewJson(preview));
	if (!preview.importable()) {
		return failure(4, preview.problem);
	}
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::ImportMap;
	request.importSource = std::make_shared<const LevelMapDocument>(std::move(imported));
	request.importOptions = importOptions;
	const auto prepared = prepareLevelPlacement(document, request);
	if (!prepared.succeeded) {
		return failure(4, prepared.error);
	}
	QJsonArray added;
	for (const auto& ref : prepared.importReport.inserted) {
		added.append(levelMapSelectionRefId(ref));
	}
	result.payload.insert("imported", added);
	LevelDocumentSaveRequest save;
	save.path = output;
	save.dryRun = dryRun;
	save.overwrite = overwrite;
	const auto report = writeLevelDocument(prepared.document, save);
	result.payload.insert("written", report.written);
	result.payload.insert("backup", report.backupPath);
	result.payload.insert("warnings", QJsonArray::fromStringList(preview.warnings + report.warnings));
	if (!report.succeeded()) {
		return failure(4, report.errors.join('\n'));
	}
	const int count = static_cast<int>(added.size());
	result.lines << (dryRun ? Text::tr("Would import %n object(s) and save the map to %1.", nullptr, count).arg(output)
							: Text::tr("Imported %n object(s) and saved the map to %1.", nullptr, count).arg(output));
	for (auto it = preview.renamedTargets.cbegin(); it != preview.renamedTargets.cend(); ++it) {
		result.lines << Text::tr("Renamed %1 to %2.").arg(it.key(), it.value());
	}
	result.lines += preview.warnings;
	return result;
}

LevelMapTransferCliResult runLevelMapObjExport(const QStringList& arguments)
{
	LevelMapTransferCliResult result;
	result.payload = {{"written", false}};
	const auto failure = [&](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite", "--groups", "--no-weld", "--triangulate", "--no-mtl",
		"--short-material-names", "--z-up", "--no-default-ignores", "--include-omitted"};
	const QSet<QString> valued{"--output", "--object", "--package", "--palette", "--group", "--material-paths", "--material-root", "--ignore", "--omit-layer",
		"--engine", "--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> repeatable{"--object", "--ignore", "--omit-layer"};
	ParsedArguments parsed;
	QString error;
	if (!parseArguments(arguments, flags, valued, repeatable, &parsed, &error)) {
		return failure(2, error);
	}
	const auto& seen = parsed.seen;
	const auto& values = parsed.values;
	if (parsed.positional.size() != 3 || !QStringList{"map", "maps", "level"}.contains(parsed.positional[0])
		|| parsed.positional[1] != QLatin1String("export-obj")) {
		return failure(2, Text::tr("Expected map export-obj <map> --output <file.obj>."));
	}
	if (!values.contains("--output")) {
		return failure(2, Text::tr("map export-obj requires --output <file.obj>."));
	}
	const auto objects = parsed.repeated.value(QStringLiteral("--object"));
	if (!objects.isEmpty() && (seen.contains("--omit-layer") || seen.contains("--include-omitted"))) {
		return failure(2, Text::tr("--omit-layer and --include-omitted apply to whole-map exports, not to --object selections."));
	}
	if (seen.contains("--material-root") && values.value("--material-paths") != QLatin1String("export")) {
		return failure(2, Text::tr("--material-root needs --material-paths export."));
	}
	auto options = levelMapObjDefaultOptions();
	if (values.contains("--group") && !levelMapObjGroupingFromId(values.value("--group"), &options.grouping)) {
		return failure(2, Text::tr("--group takes object, material or single."));
	}
	if (values.contains("--material-paths")) {
		const auto paths = values.value("--material-paths");
		if (paths != QLatin1String("game") && paths != QLatin1String("export")) {
			return failure(2, Text::tr("--material-paths takes game or export."));
		}
		options.materialPaths = paths == QLatin1String("export") ? LevelMapObjMaterialPaths::Export : LevelMapObjMaterialPaths::Game;
	}
	options.objects = !seen.contains("--groups");
	options.weld = !seen.contains("--no-weld");
	options.triangulate = seen.contains("--triangulate");
	options.materialLibrary = !seen.contains("--no-mtl");
	options.shortMaterialNames = seen.contains("--short-material-names");
	options.keepZUp = seen.contains("--z-up");
	if (seen.contains("--no-default-ignores")) {
		options.ignoredMaterials.clear();
	}
	options.ignoredMaterials += parsed.repeated.value(QStringLiteral("--ignore"));
	options.omitLayers = parsed.repeated.value(QStringLiteral("--omit-layer"));
	options.includeOmittedLayers = seen.contains("--include-omitted");
	options.selectionOnly = !objects.isEmpty();

	const auto source = QFileInfo(parsed.positional[2]).absoluteFilePath();
	const auto output = QFileInfo(values.value("--output")).absoluteFilePath();
	const bool dryRun = seen.contains("--dry-run");
	result.payload.insert("map", source);
	result.payload.insert("output", output);
	result.payload.insert("dryRun", dryRun);
	result.payload.insert("objects", QJsonArray::fromStringList(objects));
	result.payload.insert("grouping", levelMapObjGroupingId(options.grouping));
	if (!QFileInfo(source).isFile()) {
		return failure(3, Text::tr("The map does not exist: %1").arg(source));
	}
	LevelMapDocument document;
	// --engine idTech3 reads a classic-format Quake III map with no Quake III
	// evidence of its own as Quake III, so its textures/ images resolve.
	if (!loadLevelMap({source, {}, values.value("--engine")}, &document, &error)) {
		return failure(4, error);
	}
	if (!objects.isEmpty()) {
		QVector<LevelMapSelectionRef> selection;
		for (const auto& object : objects) {
			if ((!object.startsWith("entity:") && !object.startsWith("brush:") && !object.startsWith("patch:")) || !selectLevelMapObject(&document, object, &error)) {
				return failure(2, Text::tr("Invalid selected object %1: %2").arg(object, error));
			}
			for (const auto& ref : document.selection) {
				if (!selection.contains(ref)) {
					selection << ref;
				}
			}
		}
		if (!setLevelMapSelection(&document, selection, &error)) {
			return failure(2, error);
		}
	}
	// Image paths, sizes and colours from a package or folder, as the Levels
	// camera resolves them.
	if (values.contains("--package")) {
		const auto packagePath = QFileInfo(values.value("--package")).absoluteFilePath();
		result.payload.insert("package", packagePath);
		PackageArchive archive;
		if (!QFileInfo::exists(packagePath) || !archive.load(packagePath, &error)) {
			return failure(3, Text::tr("The package cannot be opened: %1").arg(error.isEmpty() ? packagePath : error));
		}
		LevelMapDocument exported;
		if (!prepareLevelMapObjExport(document, options, &exported, nullptr, &error)) {
			return failure(4, error);
		}
		LevelPreviewAssetOptions assetOptions;
		assetOptions.paletteId = values.value("--palette");
		assetOptions.materialLimit = 4096;
		assetOptions.modelLimit = 0;
		assetOptions.previewDimension = 32;
		assetOptions.totalReadByteLimit = 512LL * 1024 * 1024;
		options.materials = levelMapObjMaterials(resolveLevelPreviewAssets(exported, archive, assetOptions));
		options.materialRoot = QFileInfo(packagePath).isDir() ? packagePath : QFileInfo(packagePath).absolutePath();
	}
	if (values.contains("--material-root")) {
		options.materialRoot = QFileInfo(values.value("--material-root")).absoluteFilePath();
	}
	LevelMapObjExportRequest request;
	request.save.path = output;
	request.save.dryRun = dryRun;
	request.save.overwrite = seen.contains("--overwrite");
	request.options = options;
	const auto report = exportLevelMapObj(document, request);
	const auto json = levelMapObjExportReportJson(report);
	for (auto it = json.begin(); it != json.end(); ++it) {
		result.payload.insert(it.key(), it.value());
	}
	if (!report.succeeded()) {
		return failure(4, report.errors.join('\n'));
	}
	const int count = report.brushes + report.patches;
	const auto files = report.mtlPath.isEmpty() ? output : Text::tr("%1 and %2").arg(output, report.mtlPath);
	result.lines << (dryRun ? Text::tr("Would export %n brush(es) and patch(es) to %1.", nullptr, count).arg(files)
							: Text::tr("Exported %n brush(es) and patch(es) to %1.", nullptr, count).arg(files));
	if (report.ignoredFaces > 0) {
		const int ignored = report.ignoredFaces;
		result.lines << Text::tr("Left out %n face(s) with ignored materials.", nullptr, ignored);
	}
	const int layers = static_cast<int>(report.omittedLayers.size());
	if (layers > 0) {
		result.lines << Text::tr("Left out %n layer(s): %1.", nullptr, layers).arg(report.omittedLayers.join(QStringLiteral(", ")));
	}
	result.lines += report.warnings;
	return result;
}
} // namespace vibestudio::cli
