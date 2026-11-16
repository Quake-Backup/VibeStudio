#pragma once
#include <QJsonObject>
#include <QStringList>

namespace slopstudio::cli {
struct LevelBuildCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
LevelBuildCliResult runLevelBuildWorkspaceCommand(const QStringList& arguments);
} // namespace slopstudio::cli
