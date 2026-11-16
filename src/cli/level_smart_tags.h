#pragma once
#include <QJsonObject>
#include <QStringList>

namespace slopstudio::cli {
struct LevelSmartTagsCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
LevelSmartTagsCliResult runLevelSmartTags(const QStringList& arguments);
} // namespace slopstudio::cli
