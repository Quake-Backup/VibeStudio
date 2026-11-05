#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace vibestudio::cli {

struct LevelMapPropertiesCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};

// map properties: the Levels Map tab's map properties (core/level_map_inspector.h).
// With no change it reports the soft bounds (the game's, the map's or none,
// and how many objects lie outside), the enabled mods and the layer summary,
// and with --game-root the mod folders an installation offers. --soft-bounds
// game|none|"minX minY minZ maxX maxY maxZ", --mods "a;b", --no-mods,
// repeatable --enable-mod and --disable-mod change worldspawn's
// "_tb_soft_map_bounds" and "_tb_mod" keys, one undo step each, and write the
// result to --output (with --dry-run and --overwrite) as other map commands do.
LevelMapPropertiesCliResult runLevelMapProperties(const QStringList& arguments);

} // namespace vibestudio::cli
