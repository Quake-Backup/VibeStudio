#pragma once
#include <QJsonObject>
#include <QStringList>

namespace slopstudio::cli {
struct LevelSurfaceClipboardCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
LevelSurfaceClipboardCliResult runLevelSurfaceClipboard(const QStringList& arguments);
} // namespace slopstudio::cli
