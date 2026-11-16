#pragma once
#include <QJsonObject>
#include <QStringList>

namespace slopstudio::cli {
struct LevelBookmarksCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
LevelBookmarksCliResult runLevelBookmarks(const QStringList& arguments);
} // namespace slopstudio::cli
