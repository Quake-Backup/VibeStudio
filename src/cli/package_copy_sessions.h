#pragma once

#include <QJsonObject>
#include <QStringList>

namespace slopstudio::cli {
struct PackageCopySessionsCliResult {
	int exitCode = 0;
	QString error, summary;
	QJsonObject payload;
};
PackageCopySessionsCliResult runPackageCopySessions(const QStringList& arguments);
} // namespace slopstudio::cli
