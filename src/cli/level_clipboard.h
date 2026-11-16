#pragma once
#include <QJsonObject>
#include <QStringList>

namespace slopstudio::cli {
struct LevelClipboardCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
LevelClipboardCliResult runLevelClipboard(const QStringList& arguments);
} // namespace slopstudio::cli
