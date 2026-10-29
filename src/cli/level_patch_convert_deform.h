#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli {
struct LevelPatchToolCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
// map convert-patches: TrenchBroom's Convert Selection to Patches through
// the Levels page's service (core/level_patch_convert.h).
LevelPatchToolCliResult runLevelPatchConvert(const QStringList& arguments);
// map deform-patch: NetRadiant Custom's Deform through the Levels page's
// service (core/level_patch_tools.h deformLevelMapPatches).
LevelPatchToolCliResult runLevelPatchDeform(const QStringList& arguments);
} // namespace vibestudio::cli
