#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli
{
struct ModelAuditCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
// model audit: decodes every model in a package, folder or installation and
// reports decode issues, editability and optional same-format round trips
// (core/model_audit.h). Never writes into the source.
ModelAuditCliResult runModelAudit(const QStringList &arguments);
} // namespace vibestudio::cli
