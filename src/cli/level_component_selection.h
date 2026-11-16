#pragma once
#include <QJsonObject>
#include <QStringList>
namespace slopstudio::cli {
struct LevelComponentSelectionCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
LevelComponentSelectionCliResult runLevelComponentSelection(const QStringList& arguments);
}
