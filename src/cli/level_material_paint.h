#pragma once
#include <QJsonObject>
#include <QStringList>

namespace slopstudio::cli {
struct LevelMaterialPaintCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
LevelMaterialPaintCliResult runLevelMaterialPaint(const QStringList& arguments);
} // namespace slopstudio::cli
