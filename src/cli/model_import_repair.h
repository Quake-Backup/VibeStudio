#pragma once
#include <QJsonObject>
#include <QStringList>
namespace slopstudio::cli
{
struct ModelImportRepairCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
ModelImportRepairCliResult runModelImportRepair(const QStringList &arguments);
} // namespace slopstudio::cli
