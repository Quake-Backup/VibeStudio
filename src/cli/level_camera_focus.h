#pragma once
#include <QJsonObject>
#include <QStringList>

namespace slopstudio::cli {
struct LevelCameraFocusCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
LevelCameraFocusCliResult runLevelCameraFocus(const QStringList& arguments);
} // namespace slopstudio::cli
