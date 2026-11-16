#pragma once
#include <QJsonObject>
#include <QStringList>

namespace slopstudio::cli
{
struct ModelSkinBindingsCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
ModelSkinBindingsCliResult runModelSkinBindings(const QStringList &arguments);
} // namespace slopstudio::cli
