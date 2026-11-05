#pragma once

// The Levels Map tab's map-wide settings and summaries, shared by the Map
// tab, `map properties` and `map inspect --json`: TrenchBroom's soft map
// bounds and enabled mods, kept as the worldspawn keys TrenchBroom writes
// ("_tb_soft_map_bounds" and "_tb_mod"), and a summary of the map's layers.
//
// Workflow reference: TrenchBroom MapInspector, MapPropertiesEditor and
// ModEditor (lib/TbUiLib/src/MapInspector.cpp and ModEditor.cpp at 4c756be1,
// GPL-3.0-or-later): soft bounds from the game, from the map or disabled,
// each corner typed as three numbers or one for all three, and an ordered
// list of mods chosen from the folders beside the game's own. Original Qt
// implementation; see docs/CREDITS.md.
//
// The Map Info counts (NetRadiant Custom's DoMapInfo) live with the other
// statistics in core/level_map.h: LevelMapStatistics::entityClasses.

#include "core/level_map.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace vibestudio {

inline constexpr auto kLevelMapSoftBoundsKey = "_tb_soft_map_bounds";
inline constexpr auto kLevelMapModsKey = "_tb_mod";

// Where a map's soft bounds come from: the game's default (no key), the map's
// own box, or none at all (the key says "none").
enum class LevelMapSoftBoundsMode {
	Game,
	Custom,
	Disabled,
};

struct LevelMapSoftBounds {
	LevelMapSoftBoundsMode mode = LevelMapSoftBoundsMode::Game;
	// Whether `mins` and `maxs` hold bounds in force: false while disabled,
	// for a game with no default, or for a value that does not read.
	bool valid = false;
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	// The worldspawn value as written; empty for Game.
	QString value;
	// A custom value that is not six numbers with each minimum below its
	// maximum. Nothing is in force, as TrenchBroom treats it.
	bool malformed = false;
};

// The game's soft bounds for the map's format: Quake, Quake II and the other
// idTech2 games send coordinates as 1/8-unit shorts, so ±4096; Quake III maps
// take q3map2's MIN_WORLD_COORD and MAX_WORLD_COORD, ±65536. Doom maps have
// none.
[[nodiscard]] LevelMapSoftBounds levelMapGameSoftBounds(const LevelMapDocument& document);
// The map's soft bounds as worldspawn sets them, else the game's.
[[nodiscard]] LevelMapSoftBounds levelMapSoftBounds(const LevelMapDocument& document);
[[nodiscard]] QString levelMapSoftBoundsModeId(LevelMapSoftBoundsMode mode);
[[nodiscard]] bool levelMapSoftBoundsModeFromId(const QString& id, LevelMapSoftBoundsMode* mode);
// Six numbers, separated by spaces or commas: the minimum corner, then the
// maximum. False unless every minimum is below its maximum.
[[nodiscard]] bool parseLevelMapSoftBounds(const QString& text, LevelMapVec3* mins, LevelMapVec3* maxs);
// One corner as the Map tab's fields take it: three numbers, or one number
// for all three.
[[nodiscard]] bool parseLevelMapSoftBoundsCorner(const QString& text, LevelMapVec3* corner);
// One corner as those fields show it: one number when all three agree.
[[nodiscard]] QString levelMapSoftBoundsCornerText(const LevelMapVec3& corner);
// The worldspawn value for a box: "minX minY minZ maxX maxY maxZ".
[[nodiscard]] QString levelMapSoftBoundsValue(const LevelMapVec3& mins, const LevelMapVec3& maxs);

struct LevelMapSoftBoundsChange {
	LevelMapSoftBoundsMode mode = LevelMapSoftBoundsMode::Game;
	// For Custom.
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
};
// Sets the soft bounds on a Quake-family map's worldspawn as one undo step:
// Game takes the key away, Disabled writes "none", Custom writes the box. No
// step when nothing changes. Scene locks apply to worldspawn as to any
// entity; the selection is left as it is.
bool setLevelMapSoftBounds(LevelMapDocument* document, const LevelMapSoftBoundsChange& change, QString* error = nullptr);
// Brushes and patches that reach past the box, and point entities whose
// origin lies outside it, as selectors in map order.
[[nodiscard]] QStringList levelMapObjectsOutsideBounds(const LevelMapDocument& document, const LevelMapVec3& mins, const LevelMapVec3& maxs);

// The mods worldspawn enables, in order: "_tb_mod" split at semicolons.
[[nodiscard]] QStringList levelMapEnabledMods(const LevelMapDocument& document);
// Why `name` cannot be a mod folder name, or empty when it can.
[[nodiscard]] QString levelMapModNameProblem(const QString& name);
// Sets the enabled mods as one undo step, in the order given; an empty list
// takes the key away. Repeated names (in any case) are refused. No step when
// nothing changes.
bool setLevelMapEnabledMods(LevelMapDocument* document, const QStringList& mods, QString* error = nullptr);
// The folders directly inside a game installation's root that could be mods,
// sorted without case, leaving out `excluded` (the game's own folders) and
// hidden folders. Empty when the root is not a folder.
[[nodiscard]] QStringList levelMapAvailableMods(const QString& gameRoot, const QStringList& excluded);
// Every known game's base folder (id1, baseq2, baseq3, ...): never a mod.
[[nodiscard]] QStringList levelMapBaseGameFolders();

// Runs an edit of worldspawn's keys (the soft bounds, the mods, the Map tab's
// Worldspawn list) as a map setting: it applies whatever group is open, as
// TrenchBroom's Map Properties and Mods change the world node with a group
// open, and the group stays open. Only the open-group boundary is set aside,
// so scene locks still apply; `edit` must change nothing but worldspawn.
bool editLevelMapWorldSettings(LevelMapDocument* document, const std::function<bool(LevelMapDocument*)>& edit);

// One layer of the map, as the Map tab sums it up.
struct LevelMapLayerSummary {
	// Empty for the implicit Default layer.
	QString id;
	// Translated "Default" for the implicit layer.
	QString name;
	// Scene objects (entities, brushes, patches, Doom objects) in the layer,
	// through its groups.
	int objects = 0;
	// Groups in the layer, at any depth.
	int groups = 0;
	bool visible = true;
	bool locked = false;
	bool omitFromExport = false;
	// New objects go to this layer (the creation destination's layer).
	bool creation = false;
};
// Default first, then the layers in scene order.
[[nodiscard]] QVector<LevelMapLayerSummary> levelMapLayerSummaries(const LevelMapDocument& document);

[[nodiscard]] QJsonObject levelMapSoftBoundsJson(const LevelMapDocument& document);
[[nodiscard]] QJsonArray levelMapLayerSummariesJson(const QVector<LevelMapLayerSummary>& layers);
// Soft bounds, mods and layers together, for `map inspect --json` and
// `map properties`.
[[nodiscard]] QJsonObject levelMapInspectorJson(const LevelMapDocument& document);
// The same for text reports.
[[nodiscard]] QStringList levelMapInspectorLines(const LevelMapDocument& document);

} // namespace vibestudio
