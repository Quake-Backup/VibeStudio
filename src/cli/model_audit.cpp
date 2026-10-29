#include "cli/model_audit.h"
#include "core/model_audit.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelAuditCli)
};
ModelAuditCliResult fail(int code, const QString &message)
{
	return {code, message, {}, {}};
}
} // namespace

ModelAuditCliResult runModelAudit(const QStringList &arguments)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--round-trip", "--targets", "--timings", "--strict", "--overwrite"};
	const QSet<QString> valued{"--formats", "--max-entries", "--max-mib", "--report", "--settings-file", "--locale", "--catalog-root"};
	QStringList positional;
	QHash<QString, QString> values;
	QSet<QString> seen;
	for (int i = 1; i < arguments.size(); ++i)
	{
		const auto arg = arguments[i];
		if (!arg.startsWith('-'))
		{
			positional << arg;
			continue;
		}
		const int equal = int(arg.indexOf('='));
		const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key))
			return fail(2, Text::tr("Repeated option: %1.").arg(key));
		seen.insert(key);
		if (flags.contains(key) && equal < 0)
			continue;
		if (!valued.contains(key))
			return fail(2, Text::tr("Unexpected option: %1.").arg(arg));
		const auto value = equal >= 0 ? arg.mid(equal + 1) : i + 1 < arguments.size() && !arguments[i + 1].startsWith("--") ? arguments[++i] : QString();
		if (value.trimmed().isEmpty())
			return fail(2, Text::tr("Missing value for %1.").arg(key));
		values.insert(key, value);
	}
	if (positional.size() != 3 || positional[0] != "model" || positional[1] != "audit")
		return fail(2, Text::tr("Use model audit <package, folder or installation> with one path."));
	const QString path = positional[2];
	if (!QFileInfo::exists(path))
		return fail(3, Text::tr("%1 does not exist.").arg(QDir::toNativeSeparators(path)));
	ModelAuditOptions options;
	options.roundTrip = seen.contains("--round-trip");
	options.targets = seen.contains("--targets");
	if (values.contains("--formats"))
	{
		const QStringList known = [] {
			QStringList ids;
			for (const auto &capability : modelFormatCapabilities())
				ids << capability.id;
			return ids;
		}();
		for (const QString &format : values.value("--formats").split(',', Qt::SkipEmptyParts))
		{
			const auto id = format.trimmed().toLower();
			if (!known.contains(id))
				return fail(2, Text::tr("Unknown model format %1; model formats lists them.").arg(format));
			options.formats << id;
		}
	}
	bool ok = true;
	if (values.contains("--max-entries"))
	{
		options.maxEntries = values.value("--max-entries").toInt(&ok);
		if (!ok || options.maxEntries < 1 || options.maxEntries > 1000000)
			return fail(2, Text::tr("--max-entries takes 1 to 1000000."));
	}
	if (values.contains("--max-mib"))
	{
		const int mib = values.value("--max-mib").toInt(&ok);
		if (!ok || mib < 1 || mib > 1024)
			return fail(2, Text::tr("--max-mib takes 1 to 1024."));
		options.maxEntryBytes = qint64(mib) * 1024 * 1024;
	}
	// The report file must not land inside what is audited.
	const QString reportPath = values.value("--report");
	if (!reportPath.isEmpty())
	{
		const QFileInfo target(reportPath), source(path);
		const QString targetPath = target.absoluteFilePath(), sourcePath = source.absoluteFilePath();
		if (targetPath.compare(sourcePath, Qt::CaseInsensitive) == 0 ||
			(source.isDir() && targetPath.startsWith(sourcePath + QLatin1Char('/'), Qt::CaseInsensitive)))
			return fail(2, Text::tr("Write the --report outside the audited package or folder; the audit never writes into its source."));
		if (target.exists() && !seen.contains("--overwrite"))
			return fail(1, Text::tr("%1 exists; add --overwrite to replace it.").arg(QDir::toNativeSeparators(reportPath)));
	}
	ModelAuditReport report;
	QString error;
	if (!auditModels(path, options, &report, &error))
		return fail(1, error);
	const bool timings = seen.contains("--timings");
	const QJsonObject json = modelAuditJson(report, timings);
	if (!reportPath.isEmpty())
	{
		QSaveFile file(reportPath);
		if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(json).toJson(QJsonDocument::Indented)) < 0 || !file.commit())
			return fail(1, Text::tr("The report could not be written: %1").arg(file.errorString()));
	}
	ModelAuditCliResult result;
	result.payload = json;
	result.payload.insert("reportPath", reportPath.isEmpty() ? QJsonValue() : QJsonValue(QFileInfo(reportPath).absoluteFilePath()));
	result.lines = modelAuditLines(report);
	if (result.lines.isEmpty())
		result.lines << Text::tr("No models were found.");
	// --strict turns damage, decode failures and round trips outside their
	// tolerance into a validation failure, for regression runs.
	if (seen.contains("--strict"))
	{
		for (const auto &summary : report.formats)
		{
			int outside = summary.roundTrips - summary.roundTripsWithin;
			if (summary.failed > 0 || summary.damaged > 0 || outside > 0)
			{
				result.exitCode = 4;
				result.error = Text::tr("The audit found decode failures, damage or round trips outside their tolerance.");
				break;
			}
		}
	}
	return result;
}
} // namespace vibestudio::cli
