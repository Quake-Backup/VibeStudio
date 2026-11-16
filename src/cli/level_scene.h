#pragma once
#include <QJsonObject>
#include <QStringList>
namespace slopstudio::cli {
struct LevelSceneCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
LevelSceneCliResult runLevelScene(const QStringList& arguments);
} // namespace slopstudio::cli
