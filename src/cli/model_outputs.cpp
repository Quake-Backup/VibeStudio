#include "cli/model_outputs.h"

#include "core/cli_cancellation.h"
#include "core/model_delivery.h"
#include "core/model_outputs.h"
#include "core/project_manifest.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QSet>

namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelOutputsCli)
};
ModelToolsCliResult fail(int code, const QString &message)
{
	return {code, message, {}, {}};
}
} // namespace

ModelToolsCliResult runModelOutputs(const QStringList &arguments)
{
	const QSet<QString> flags{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose"),
							  QStringLiteral("--dry-run"), QStringLiteral("--stale"), QStringLiteral("--progress")};
	const QSet<QString> options{QStringLiteral("--input"),  QStringLiteral("--project"),	  QStringLiteral("--refresh"),
								QStringLiteral("--forget"), QStringLiteral("--receipt"),	  QStringLiteral("--assembly"),
								QStringLiteral("--locale"), QStringLiteral("--settings-file"), QStringLiteral("--catalog-root")};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	for (int i = 1; i < arguments.size(); ++i)
	{
		const auto arg = arguments[i];
		if (!arg.startsWith(QStringLiteral("--")))
		{
			positional << arg;
			continue;
		}
		const auto equal = arg.indexOf(QLatin1Char('='));
		const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key))
			return fail(2, Text::tr("Repeated option: %1.").arg(key));
		seen.insert(key);
		if (flags.contains(key) && equal < 0)
			continue;
		if (!options.contains(key))
			return fail(2, Text::tr("Unknown or invalid option: %1.").arg(key));
		const auto value = equal < 0 ? arguments.value(++i) : arg.mid(equal + 1);
		if (value.isEmpty() || value.startsWith(QStringLiteral("--")))
			return fail(2, Text::tr("Option %1 requires a value.").arg(key));
		values.insert(key, value);
	}
	if (positional.value(0) != QStringLiteral("model") || positional.value(1) != QStringLiteral("outputs") || positional.size() > 3 ||
		(positional.size() == 3 && seen.contains(QStringLiteral("--input"))))
		return fail(2, Text::tr("Use model outputs [SOURCE] [--project DIR], with --refresh OUTPUT|stale, --forget OUTPUT or --receipt PACKAGE."));
	const QString source = seen.contains(QStringLiteral("--input")) ? values.value(QStringLiteral("--input")) : positional.value(2);
	const int actions = int(seen.contains(QStringLiteral("--refresh"))) + int(seen.contains(QStringLiteral("--forget"))) +
						int(seen.contains(QStringLiteral("--receipt")));
	if (actions > 1)
		return fail(2, Text::tr("Choose one of --refresh, --forget and --receipt."));
	if (seen.contains(QStringLiteral("--dry-run")) && !seen.contains(QStringLiteral("--refresh")) && !seen.contains(QStringLiteral("--forget")))
		return fail(2, Text::tr("--dry-run applies to --refresh and --forget."));
	if (seen.contains(QStringLiteral("--assembly")) && !seen.contains(QStringLiteral("--receipt")))
		return fail(2, Text::tr("--assembly applies to --receipt."));
	const bool dryRun = seen.contains(QStringLiteral("--dry-run"));
	ModelWorkControl control = cliModelWorkControl(seen.contains(QStringLiteral("--progress")));

	// A player package's embedded receipt needs no project.
	if (seen.contains(QStringLiteral("--receipt")))
	{
		ModelOutputRecord record;
		ModelOutputStatus status;
		QString error;
		if (!readModelPackageReceipt(values.value(QStringLiteral("--receipt")), &record, &status, &error, values.value(QStringLiteral("--assembly")),
									 control))
			return fail(cliCancellationRequested() ? cliCancelledExitCode : 4, error);
		ModelToolsCliResult result;
		result.payload = modelOutputStatusJson(record, status, {});
		result.payload.insert(QStringLiteral("package"), QFileInfo(values.value(QStringLiteral("--receipt"))).absoluteFilePath());
		result.lines << modelOutputStatusLine(record, status);
		if (status.state != ModelOutputState::Current)
		{
			result.exitCode = 4;
			result.error = status.detail;
		}
		return result;
	}

	const QString projectRoot = seen.contains(QStringLiteral("--project")) ? QFileInfo(values.value(QStringLiteral("--project"))).absoluteFilePath()
							   : !source.isEmpty()							? findModelProjectRoot(source)
																			: findModelProjectRoot(QDir::currentPath());
	if (projectRoot.isEmpty())
		return fail(2, Text::tr("Name the project with --project, or a source inside a project folder."));
	ProjectManifest manifest;
	QString error;
	if (!loadProjectManifest(projectRoot, &manifest, &error))
		return fail(QFileInfo::exists(projectManifestPath(projectRoot)) ? 1 : 3, error);
	QStringList warnings;
	auto records = projectModelOutputs(manifest, &warnings);
	if (!source.isEmpty())
		records = modelOutputsForSource(records, source);

	const auto matches = [](const ModelOutputRecord &record, const QString &wanted)
	{
		const auto key = record.key();
		const auto absolute = QFileInfo(wanted).absoluteFilePath();
#ifdef Q_OS_WIN
		constexpr auto sensitivity = Qt::CaseInsensitive;
#else
		constexpr auto sensitivity = Qt::CaseSensitive;
#endif
		return key.compare(QDir::cleanPath(wanted), sensitivity) == 0 || key.compare(QDir::cleanPath(absolute), sensitivity) == 0 ||
			   (!record.outputs.isEmpty() && record.outputs.first().entry.compare(wanted, Qt::CaseInsensitive) == 0);
	};

	if (seen.contains(QStringLiteral("--forget")))
	{
		const auto wanted = values.value(QStringLiteral("--forget"));
		const auto found = std::find_if(records.cbegin(), records.cend(), [&](const ModelOutputRecord &record) { return matches(record, wanted); });
		if (found == records.cend())
			return fail(3, Text::tr("No receipt names the output %1.").arg(wanted));
		if (!dryRun && !forgetModelOutput(projectRoot, found->key(), &error))
			return fail(1, error);
		ModelToolsCliResult result;
		result.payload = {{QStringLiteral("forgotten"), found->key()}, {QStringLiteral("dryRun"), dryRun}};
		result.lines << (dryRun ? Text::tr("Would forget the receipt of %1.") : Text::tr("Forgot the receipt of %1.")).arg(found->key());
		return result;
	}

	ModelToolsCliResult result;
	QJsonArray items;
	int stale = 0;
	if (seen.contains(QStringLiteral("--refresh")))
	{
		const auto wanted = values.value(QStringLiteral("--refresh"));
		const bool allStale = wanted == QStringLiteral("stale");
		int refreshed = 0, refused = 0;
		for (const auto &record : std::as_const(records))
		{
			if (cliCancellationRequested())
				return fail(cliCancelledExitCode, Text::tr("Cancelled; outputs not yet refreshed are unchanged."));
			const auto status = modelOutputStatus(record, nullptr, control.cancelled);
			if (allStale ? !modelOutputStateIsStale(status.state) : !matches(record, wanted))
				continue;
			const auto refresh = refreshModelOutput(record, dryRun, control);
			auto item = modelOutputStatusJson(record, status, projectRoot);
			item.insert(QStringLiteral("refreshed"), refresh.error.isEmpty());
			item.insert(QStringLiteral("written"), refresh.written);
			item.insert(QStringLiteral("outputs"), QJsonArray::fromStringList(refresh.outputs));
			item.insert(QStringLiteral("notes"), QJsonArray::fromStringList(refresh.notes));
			if (refresh.cancelled)
				return fail(cliCancelledExitCode, Text::tr("Cancelled; %1 is unchanged.").arg(record.key()));
			if (!refresh.error.isEmpty())
			{
				item.insert(QStringLiteral("error"), refresh.error);
				++refused;
				result.lines << Text::tr("Not refreshed: %1: %2").arg(record.key(), refresh.error);
			}
			else
			{
				++refreshed;
				if (!dryRun && !recordModelOutput(projectRoot, refresh.record, &error))
					result.lines << Text::tr("Refreshed %1, but its receipt was not saved: %2").arg(record.key(), error);
				else
					result.lines << (dryRun ? Text::tr("Would refresh %1.") : Text::tr("Refreshed %1.")).arg(record.key());
			}
			items.append(item);
		}
		if (!allStale && items.isEmpty())
			return fail(3, Text::tr("No receipt names the output %1.").arg(wanted));
		result.payload.insert(QStringLiteral("refreshed"), refreshed);
		result.payload.insert(QStringLiteral("refused"), refused);
		result.payload.insert(QStringLiteral("dryRun"), dryRun);
		if (refused > 0)
		{
			result.exitCode = 4;
			result.error = Text::tr("%n output(s) could not be refreshed.", nullptr, refused);
		}
	}
	else
	{
		for (const auto &record : std::as_const(records))
		{
			if (cliCancellationRequested())
				return fail(cliCancelledExitCode, Text::tr("Cancelled."));
			const auto status = modelOutputStatus(record, nullptr, control.cancelled);
			stale += modelOutputStateIsStale(status.state) || status.state == ModelOutputState::OutputChanged ? 1 : 0;
			items.append(modelOutputStatusJson(record, status, projectRoot));
			result.lines << modelOutputStatusLine(record, status);
		}
		if (records.isEmpty())
			result.lines << Text::tr("No model outputs are recorded for this project yet.");
		// Like asset index status: 4 says something needs attention.
		if (stale > 0)
		{
			result.exitCode = 4;
			result.error = Text::tr("%n model output(s) are stale or were changed elsewhere.", nullptr, stale);
		}
	}
	result.payload.insert(QStringLiteral("project"), projectRoot);
	if (!source.isEmpty())
		result.payload.insert(QStringLiteral("source"), QFileInfo(source).absoluteFilePath());
	result.payload.insert(QStringLiteral("records"), items);
	result.payload.insert(QStringLiteral("stale"), stale);
	result.payload.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(warnings));
	result.lines += warnings;
	return result;
}
} // namespace vibestudio::cli
