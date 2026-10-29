// Assemble's watch over the files it holds: the .assembly.json and the loose
// model files its parts link. A part rewritten by another tool reloads the
// inputs (they are read-only snapshots, so nothing of the user's is lost); a
// changed assembly source says so, and Save already refuses to replace it
// (ModelAssemblyDocument::save compares its SHA-256).

#include "app/model_assembly_dialog.h"

#include "core/document_watch.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTimer>

namespace vibestudio
{
struct ModelAssemblyWatchState
{
	DocumentWatcher watcher;
	QTimer *timer = nullptr;
	bool reloading = false;
};

void ModelAssemblyDialog::watchAssemblyFiles()
{
	if (!m_watch)
	{
		m_watch = std::make_shared<ModelAssemblyWatchState>();
		m_watch->timer = new QTimer(this);
		m_watch->timer->setObjectName(QStringLiteral("assemblyWatchTimer"));
		m_watch->timer->setInterval(1000);
		connect(m_watch->timer, &QTimer::timeout, this, [this]() { pollAssemblyWatch(); });
	}
	auto &watch = *m_watch;
	// Every adopt reads the inputs again, so the baseline is what was just read.
	watch.watcher.clear();
	if (!m_document.path().isEmpty())
		watch.watcher.registerPath(m_document.path(), DocumentWatchRole::Auxiliary, QStringLiteral("assembly-source"));
	for (const auto &part : m_document.assembly().parts)
	{
		if (part.sourceKind == ModelAssemblySource::File && !part.source.isEmpty())
			watch.watcher.registerPath(QDir(m_document.directory()).absoluteFilePath(part.source), DocumentWatchRole::Auxiliary,
									   QStringLiteral("assembly-part"));
		if (part.skin && part.skin->sourceKind == ModelAssemblySource::File && !part.skin->source.isEmpty())
			watch.watcher.registerPath(QDir(m_document.directory()).absoluteFilePath(part.skin->source), DocumentWatchRole::Auxiliary,
									   QStringLiteral("assembly-part"));
	}
	if (watch.watcher.watchedCount() > 0)
		watch.timer->start();
	else
		watch.timer->stop();
}

void ModelAssemblyDialog::pollAssemblyWatch(qint64 monotonicMsecs)
{
	if (!m_watch || m_working || m_watch->reloading)
		return;
	const auto events = monotonicMsecs < 0 ? m_watch->watcher.poll() : m_watch->watcher.pollAt(monotonicMsecs);
	bool partChanged = false;
	for (const auto &event : events)
	{
		if (!documentChangeKindIsSubstantive(event.kind) || !event.differsFromRegistered)
			continue;
		if (event.documentId == QStringLiteral("assembly-part"))
		{
			partChanged = true;
			continue;
		}
		report(event.kind == DocumentChangeKind::Removed
				   ? QCoreApplication::translate("ModelAssemblyDialog", "The assembly file %1 was removed on disk. Save As keeps this version.")
						 .arg(QFileInfo(event.path).fileName())
				   : QCoreApplication::translate("ModelAssemblyDialog",
												 "The assembly file %1 changed on disk. Open it again to load the change, or Save As to keep this version.")
						 .arg(QFileInfo(event.path).fileName()),
			   LiveStatus::Problem);
	}
	if (!partChanged)
		return;
	// Linked parts are inputs: reading them again changes nothing the user made.
	m_watch->reloading = true;
	QString error;
	const bool reloaded = reloadInputs(&error);
	m_watch->reloading = false;
	report(reloaded ? QCoreApplication::translate("ModelAssemblyDialog", "A linked part changed on disk; the inputs were read again.")
					: QCoreApplication::translate("ModelAssemblyDialog", "A linked part changed on disk and could not be read again: %1").arg(error),
		   reloaded ? LiveStatus::Result : LiveStatus::Problem);
}
} // namespace vibestudio
