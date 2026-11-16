#pragma once
#include <QJsonObject>
#include <QStringList>

namespace slopstudio::cli
{
struct ModelMaterialSlotsCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
ModelMaterialSlotsCliResult runModelMaterialSlots(const QStringList &arguments);
} // namespace slopstudio::cli
