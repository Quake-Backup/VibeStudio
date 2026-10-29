#pragma once

// Moving whole maps and parts of maps between files: NetRadiant Custom's
// Import Map and Export Selection (radiant/map.cpp ImportMap, Map_ImportFile,
// ExportMap and Map_SaveSelected at 449778b3, GPL-2.0-or-later) and
// TrenchBroom's File > Export > Map with layers marked Omit From Export
// (lib/TbUiLib/src/MapWindow.cpp exportDocumentAsMap at 4c756be1,
// GPL-3.0-or-later). Workflow references only; original Qt implementation, see
// docs/CREDITS.md.

#include "core/compiler_profiles.h"
#include "core/level_document.h"
#include "core/level_map.h"

#include <QJsonObject>
#include <QMap>
#include <functional>

namespace vibestudio {

struct CompilerRunResult;

// ---- Import Map ------------------------------------------------------------

struct LevelMapImportOptions {
	// Give names the imported map shares with the open map (targetname and the
	// target keys of levelMapTargetPropertyKeys()) the next free number, as
	// NetRadiant Custom does, so imported links keep pointing at each other.
	bool renameTargets = true;
};

// What an import would add, worked out before anything changes.
struct LevelMapImportPreview {
	QString path;
	qint64 bytes = 0;
	QString engineFamily;
	QString format;
	// levelMapBrushDialect() of the imported brushes.
	QString dialect;
	int entities = 0;
	int brushes = 0;
	int patches = 0;
	int layers = 0;
	int groups = 0;
	int linkedGroups = 0;
	// Worldspawn keys other than its class, which the open map's worldspawn keeps instead.
	int worldspawnKeys = 0;
	// Old name -> new name, empty when names are kept or none clash.
	QMap<QString, QString> renamedTargets;
	QStringList warnings;
	// Why the map cannot be imported; empty when it can.
	QString problem;
	[[nodiscard]] bool importable() const { return problem.isEmpty(); }
};

struct LevelMapImportReport {
	LevelMapImportPreview preview;
	QVector<LevelMapSelectionRef> inserted;
};

// True for a compiled .bsp, which VibeMap3 converts to a .map before import.
bool levelMapImportNeedsConversion(const QString& path);
// VibeMap3's convert profile (vibemap3-convert): -convert -format map, map_220
// or map_bp to match `dialect` (levelMapBrushDialect of the open map), writing
// `outputPath`. The caller adds tool overrides and search paths.
CompilerCommandRequest levelMapImportConversionRequest(const QString& bspPath, const QString& outputPath, const QString& dialect);

// The game a .bsp conversion runs VibeMap3 for, as NetRadiant Custom's
// Map_ImportFile passes -game, -fs_basepath and -fs_game, and the compiler
// context Build uses.
struct LevelMapBspConversionContext {
	// The project's game (effectiveProjectGameKey): quake3, or a key such as
	// quakelive, rtcw, et, ef, jk2 or jka.
	QString gameKey;
	// The game installation's folder (-fs_basepath) and its base or mod
	// folder (-fs_game); empty leaves VibeMap3's own defaults.
	QString basePath;
	QString gameDirectory;
	QString workspaceRootPath;
	QStringList extraSearchPaths;
	QVector<CompilerToolPathOverride> executableOverrides;
};
// VibeMap3's -game profile for a compiled map: the project's game when the
// file's header (IBSP 46 or 47, RBSP, FBSP) is that game's format, otherwise
// the usual game for the header. Empty, with `error`, for Quake and Quake II
// BSPs, which VibeMap3 cannot read, and for files that are not BSPs.
QString levelMapBspConversionGame(const QString& bspPath, const QString& gameKey, QString* error = nullptr);
// levelMapImportConversionRequest() with -game, -fs_basepath and -fs_game and
// the compiler paths of `context`; false, with `error`, when VibeMap3 cannot
// convert the file.
bool prepareLevelMapImportConversion(const QString& bspPath, const QString& outputPath, const QString& dialect,
	const LevelMapBspConversionContext& context, CompilerCommandRequest* request, QString* error = nullptr);
// Why a finished conversion gave no map, or empty when it did: the runner's
// error, VibeMap3's error output, or its exit code when it printed nothing.
QString levelMapImportConversionProblem(const CompilerRunResult& result, const QString& convertedPath);

// Reads a Quake-family .map with the map loader (no clipboard limits; the
// loader's 512 MiB limit applies), on the calling thread. `engineHint` is the
// open map's engine family, so ambiguous text parses the way the open map's
// does. Doom WADs, unreadable maps and maps with read errors are refused.
bool readLevelMapImport(const QString& path, const QString& engineHint, LevelMapDocument* imported, QString* error = nullptr,
	const std::function<bool()>& isCancelled = {}, const std::function<void(LevelMapLoadPhase, qint64, qint64)>& progress = {});

// Names in `imported` that `destination` already uses, each with the next
// free number: door -> door1, t3 -> t4, skipping names either map uses.
QMap<QString, QString> levelMapImportTargetRenames(const LevelMapDocument& destination, const LevelMapDocument& imported);
// `sourceBytes` is the imported file's size when the caller measured it on a
// worker; -1 counts the UTF-8 bytes of the text without copying it.
LevelMapImportPreview previewLevelMapImport(const LevelMapDocument& destination, const LevelMapDocument& imported,
	const LevelMapImportOptions& options = {}, qint64 sourceBytes = -1);
// Adds every object of `imported` at its own coordinates through the paste
// service (pasteLevelMapDocumentAt), with clashing names renamed, as one undo
// step that selects the imported objects. Worldspawn's brushes and patches
// join the open map's worldspawn; its keys are left out. Complete groups and
// linked groups keep their structure; layers are not recreated, and
// everything joins the creation context (the active layer or open group).
// Scene locks apply. Fails, changing nothing, when the preview has a problem.
bool importLevelMap(LevelMapDocument* destination, const LevelMapDocument& imported, const LevelMapImportOptions& options = {},
	LevelMapImportReport* report = nullptr, QString* error = nullptr);
QJsonObject levelMapImportPreviewJson(const LevelMapImportPreview& preview);

// ---- Export Selection as Map -----------------------------------------------

// The selection as a map of its own: a worldspawn block holding the selected
// world brushes and patches, with the open map's worldspawn keys when asked,
// then the selected entities, a brush entity's selected brushes only. Complete
// closed groups keep a scene note. No clipboard limits.
QString levelMapSelectionExportText(const LevelMapDocument& document, bool worldspawnKeys, QString* error = nullptr);

struct LevelMapSelectionExportRequest {
	// path, dryRun, overwrite, backups and cancellation.
	LevelDocumentSaveRequest save;
	bool worldspawnKeys = true;
};
struct LevelMapSelectionExportReport {
	LevelMapSaveReport save;
	int objects = 0;
	qint64 bytes = 0;
	bool cancelled = false;
};
// Writes levelMapSelectionExportText() through writeLevelMapBytes(), refusing
// the open map's own file. The document is never changed.
LevelMapSelectionExportReport exportLevelMapSelection(const LevelMapDocument& document, const LevelMapSelectionExportRequest& request);

// ---- Export Map ------------------------------------------------------------

struct LevelMapExportRequest {
	// path, dryRun, overwrite, backups, scene storage and cancellation.
	LevelDocumentSaveRequest save;
	// Further layers to leave out, by id or name (any case).
	QStringList omitLayers;
	// Write layers marked Omit From Export as well.
	bool includeOmittedLayers = false;
};
struct LevelMapExportReport {
	LevelMapSaveReport save;
	// Names of the layers left out, in scene order.
	QStringList omittedLayers;
	int omittedObjects = 0;
	bool cancelled = false;
};
// The layers an export leaves out: those marked Omit From Export (unless
// `includeOmittedLayers`) and those `omitLayers` names. False, with `error`,
// for a name that is not a layer.
bool levelMapExportOmittedLayers(const LevelMapDocument& document, const LevelMapExportRequest& request, QStringList* layerIds,
	QString* error = nullptr);
// The document as Export Map writes it: without the omitted layers, their
// nested groups and every object they hold. The copy's history is dropped.
bool prepareLevelMapExport(const LevelMapDocument& document, const LevelMapExportRequest& request, LevelMapDocument* copy,
	LevelMapExportReport* report = nullptr, QString* error = nullptr);
// Writes a copy of the map to another file without changing the document, its
// path or its saved state, and refuses the open map's own file, as
// TrenchBroom's Export Map does. Layers are left out on Quake-family maps only.
LevelMapExportReport exportLevelMapCopy(const LevelMapDocument& document, const LevelMapExportRequest& request);
QJsonObject levelMapExportReportJson(const LevelMapExportReport& report);

} // namespace vibestudio
