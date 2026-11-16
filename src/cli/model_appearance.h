#pragma once

#include <QJsonObject>
#include <QStringList>

namespace slopstudio::cli
{
struct ModelAppearanceCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject data;
	QString text;
};
ModelAppearanceCliResult runModelAppearance(const QStringList &arguments);
} // namespace slopstudio::cli
