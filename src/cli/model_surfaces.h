#pragma once
#include <QJsonObject>
#include <QStringList>

namespace slopstudio::cli
{
struct ModelSurfacesCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
ModelSurfacesCliResult runModelSurfaces(const QStringList &arguments);
} // namespace slopstudio::cli
