#pragma once
#include <QJsonObject>
#include <QStringList>

namespace slopstudio::cli {
struct LevelObjectSelectionCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
LevelObjectSelectionCliResult runLevelObjectSelection(const QStringList& arguments);
} // namespace slopstudio::cli
