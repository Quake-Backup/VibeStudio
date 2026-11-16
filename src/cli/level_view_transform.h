#pragma once
#include <QJsonObject>
#include <QStringList>
namespace slopstudio::cli {
struct LevelViewTransformCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
// map view-transform: the Levels page's view-relative Roll, Yaw and Pitch
// turns and Flip Horizontal and Vertical (core/level_quick_transform.h)
// through the same Rotate and Mirror placement services.
LevelViewTransformCliResult runLevelViewTransform(const QStringList& arguments);
} // namespace slopstudio::cli
