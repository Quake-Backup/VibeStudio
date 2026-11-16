#pragma once
#include <QJsonObject>
#include <QStringList>

namespace slopstudio::cli
{
struct ModelMdlCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
ModelMdlCliResult runModelMdl(const QStringList &arguments);
} // namespace slopstudio::cli
