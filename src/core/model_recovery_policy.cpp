#include "core/model_recovery_policy.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace vibestudio
{
namespace
{
bool stopped(const std::function<bool()> &cancelled)
{
	return cancelled && cancelled();
}
} // namespace

QString modelRecoverySourceStateId(ModelRecoverySourceState state)
{
	switch (state)
	{
	case ModelRecoverySourceState::Unsaved:
		return QStringLiteral("unsaved");
	case ModelRecoverySourceState::Unchanged:
		return QStringLiteral("unchanged");
	case ModelRecoverySourceState::Changed:
		return QStringLiteral("changed");
	case ModelRecoverySourceState::Missing:
		return QStringLiteral("missing");
	case ModelRecoverySourceState::Unreadable:
		return QStringLiteral("unreadable");
	}
	return {};
}

QString modelRecoverySourceStateText(ModelRecoverySourceState state)
{
	switch (state)
	{
	case ModelRecoverySourceState::Unsaved:
		return QCoreApplication::translate("VibeStudioModelRecovery", "No saved source");
	case ModelRecoverySourceState::Unchanged:
		return QCoreApplication::translate("VibeStudioModelRecovery", "Source unchanged");
	case ModelRecoverySourceState::Changed:
		return QCoreApplication::translate("VibeStudioModelRecovery", "Source changed since the copy");
	case ModelRecoverySourceState::Missing:
		return QCoreApplication::translate("VibeStudioModelRecovery", "Source missing");
	case ModelRecoverySourceState::Unreadable:
		return QCoreApplication::translate("VibeStudioModelRecovery", "Source unreadable");
	}
	return {};
}

QByteArray modelRecoveryFileSha256(const QString &path, qint64 limit, const std::function<bool()> &cancelled)
{
	const QFileInfo info(path);
	if (!info.isFile() || info.size() > limit)
		return {};
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	QCryptographicHash hash(QCryptographicHash::Sha256);
	qint64 total = 0;
	while (!file.atEnd())
	{
		if (stopped(cancelled))
			return {};
		const auto block = file.read(256 * 1024);
		if (block.isEmpty())
			break;
		total += block.size();
		if (total > limit)
			return {};
		hash.addData(block);
	}
	return file.error() == QFileDevice::NoError ? hash.result() : QByteArray();
}

ModelRecoverySourceState modelRecoverySourceState(const ModelRecoveryRecord &record, const std::function<bool()> &cancelled)
{
	// Imports and new documents have no editable source to compare.
	if (record.sourcePath.isEmpty() || record.sourceSha256.isEmpty())
		return ModelRecoverySourceState::Unsaved;
	if (!QFileInfo::exists(record.sourcePath))
		return ModelRecoverySourceState::Missing;
	const auto digest = modelRecoveryFileSha256(record.sourcePath, modelDocumentMaxSourceBytes, cancelled);
	if (digest.isEmpty())
		return ModelRecoverySourceState::Unreadable;
	return digest == record.sourceSha256 ? ModelRecoverySourceState::Unchanged : ModelRecoverySourceState::Changed;
}

ModelRecoveryPruneReport pruneModelRecoveries(const QString &directory, const ModelRecoveryRetention &policy,
											  const std::function<bool(qint64)> &ownerRunning, const std::function<bool()> &cancelled,
											  bool dryRun)
{
	ModelRecoveryPruneReport report;
	if (directory.isEmpty() || !QFileInfo(directory).isDir())
		return report;
	const auto cancelledText = QCoreApplication::translate("VibeStudioModelRecovery", "Recovery clean-up was cancelled.");
	QVector<QFileInfo> files;
	QDirIterator entries(directory, {QStringLiteral("*.vsmeshrecovery")}, QDir::Files | QDir::NoDotAndDotDot);
	while (entries.hasNext())
	{
		if (stopped(cancelled))
		{
			report.error = cancelledText;
			return report;
		}
		files << QFileInfo(entries.next());
	}
	// Newest first; the oldest go first when the policy is exceeded.
	std::sort(files.begin(), files.end(),
			  [](const QFileInfo &a, const QFileInfo &b)
			  { return a.lastModified() == b.lastModified() ? a.fileName() < b.fileName() : a.lastModified() > b.lastModified(); });
	qint64 bytes = 0;
	for (const auto &info : std::as_const(files))
	{
		if (stopped(cancelled))
		{
			report.error = cancelledText;
			break;
		}
		if (report.kept < policy.maxCopies && bytes + info.size() <= policy.maxBytes)
		{
			++report.kept;
			bytes += info.size();
			continue;
		}
		// Header only: who wrote it. A running owner may still be checkpointing.
		const auto record = inspectModelRecovery(info.absoluteFilePath(), nullptr, cancelled);
		const qint64 owner = record.ownerProcessId;
		if (owner > 0 && ownerRunning && ownerRunning(owner))
		{
			++report.keptLive;
			continue;
		}
		const QString id = info.completeBaseName();
		if (modelRecoveryPath(directory, id).isEmpty())
		{
			++report.kept;
			continue;
		}
		QString error;
		if (!dryRun && !removeModelRecovery(directory, id, &error))
		{
			report.error = error;
			continue;
		}
		report.removed.prepend(id);
	}
	return report;
}

} // namespace vibestudio
