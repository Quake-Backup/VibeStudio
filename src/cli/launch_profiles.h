#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace vibestudio::cli {

struct LaunchProfilesCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};

// The Build page's launch settings of the user's own (core/build_pipeline.h
// and StudioSettings):
// - launch profiles lists the built-in launch profiles and the user's.
// - launch profile-set <id> --name NAME --arguments "LINE" [--engine idTech1|
//   idTech2|idTech3|any] adds or replaces one of the user's profiles; the line
//   may use {map}, {mod}, {basedir}, {bsp}, {iwad} and {warp}.
// - launch profile-remove <id> removes one.
// - launch extra-args [--installation ID] [--arguments "ARGS" | --clear]
//   shows or sets the extra arguments every launch of an installation adds.
// Changes support --dry-run, which reports without saving.
LaunchProfilesCliResult runLaunchProfiles(const QStringList& arguments);

} // namespace vibestudio::cli
