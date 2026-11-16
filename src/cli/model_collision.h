#pragma once
#include <QJsonObject>
#include <QStringList>

namespace slopstudio::cli
{
struct ModelCollisionCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
ModelCollisionCliResult runModelCollision(const QStringList &arguments);
} // namespace slopstudio::cli
