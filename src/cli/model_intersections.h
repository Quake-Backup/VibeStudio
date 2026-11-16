#pragma once
#include <QJsonObject>
#include <QStringList>

namespace slopstudio::cli
{
struct ModelIntersectionsCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
ModelIntersectionsCliResult runModelIntersections(const QStringList &arguments);
} // namespace slopstudio::cli
