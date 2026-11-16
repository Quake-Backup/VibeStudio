#pragma once
#include <QJsonObject>
#include <QStringList>

namespace slopstudio::cli {
struct LevelRegionSelectionCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
LevelRegionSelectionCliResult runLevelRegionSelection(const QStringList& arguments);
} // namespace slopstudio::cli
