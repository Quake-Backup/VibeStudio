#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace vibestudio::cli {

struct LevelCameraFarClipCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};

// editor far-clip: shows the Levels camera's saved far clip (Far Clip in
// Camera and its draw distance, NetRadiant Custom's cubic clip;
// core/level_camera_render.h), or changes it with --clip on|off and one of
// --step 1-23, --distance UNITS (the nearest step), --shorter or --longer
// (one step either way). --dry-run previews a change without saving it. The
// studio reads the same settings key when it starts.
LevelCameraFarClipCliResult runLevelCameraFarClip(const QStringList& arguments);

} // namespace vibestudio::cli
