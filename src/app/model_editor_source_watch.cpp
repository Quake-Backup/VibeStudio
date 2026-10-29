// The Mesh Editor's watch over its own .mesh.json: another tool (Blender, a
// script, a second studio, version control) can change the file while it is
// open. The shared core/document_watch service compares content fingerprints
// once a second; a substantive change shows a notice with Reload from Disk,
// Compare and Keep Editing, and Save then asks before replacing the file.

#include "app/model_editor_dialog.h"

#include "app/studio_accessibility.h"
#include "app/studio_layout.h"
#include "core/document_watch.h"
#include "core/model_file_io.h"
#include "core/model_recovery_policy.h"

#include <QBoxLayout>
#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

namespace vibestudio
{
struct ModelSourceWatchState
{
	DocumentWatcher watcher;
	NoticeBar *notice = nullptr;
	QTimer *timer = nullptr;
	QString path;
	// The file as it was when the user chose Keep Editing; the same content
	// is not reported again.
	DocumentFingerprint ignored;
	bool hasIgnored = false;
	// Set while the notice is replaced or cleared by the editor itself, so
	// its dismissed() signal is not taken as Keep Editing.
	bool quiet = false;
};

void ModelEditorDialog::buildSourceWatch(QBoxLayout *layout)
{
	m_sourceWatch = std::make_shared<ModelSourceWatchState>();
	auto *notice = new NoticeBar(this);
	notice->setObjectName(QStringLiteral("meshSourceChangedNotice"));
	notice->hide();
	layout->addWidget(notice);
	m_sourceWatch->notice = notice;
	// Closing the notice is the same as Keep Editing.
	connect(notice, &NoticeBar::dismissed, this,
			[this]()
			{
				if (m_sourceWatch && !m_sourceWatch->quiet)
					keepEditingOverSourceChange();
			});
	m_sourceWatch->timer = new QTimer(this);
	m_sourceWatch->timer->setObjectName(QStringLiteral("meshSourceWatchTimer"));
	m_sourceWatch->timer->setInterval(1000);
	connect(m_sourceWatch->timer, &QTimer::timeout, this, [this]() { pollSourceWatch(); });
}

void ModelEditorDialog::sourceBindingChanged()
{
	const QString path = m_document.path();
	if (sourcePathChanged && !path.isEmpty())
		sourcePathChanged(path);
	// The package path starts where the source came from; Outputs follow the file.
	if (m_virtualPath->text() == QStringLiteral("models/props/edited.md3"))
		prefillPackagePath();
	refreshOutputs();
	if (!m_sourceWatch)
		return;
	auto &watch = *m_sourceWatch;
	watch.watcher.clear();
	watch.hasIgnored = false;
	watch.path = path;
	watch.quiet = true;
	watch.notice->dismiss();
	watch.quiet = false;
	if (path.isEmpty())
	{
		watch.timer->stop();
		return;
	}
	watch.watcher.registerPath(path, DocumentWatchRole::Auxiliary, QStringLiteral("model-source"));
	watch.timer->start();
}

void ModelEditorDialog::pollSourceWatch(qint64 monotonicMsecs)
{
	// The editor's own saves run on the worker and then take a new baseline.
	if (!m_sourceWatch || m_sourceWatch->path.isEmpty() || m_working)
		return;
	auto &watch = *m_sourceWatch;
	const auto events = monotonicMsecs < 0 ? watch.watcher.poll() : watch.watcher.pollAt(monotonicMsecs);
	for (const auto &event : events)
	{
		if (!documentChangeKindIsSubstantive(event.kind) || !event.differsFromRegistered)
			continue;
		if (watch.hasIgnored && event.current.exists && watch.ignored.exists && event.current.contentMatches(watch.ignored))
			continue;
		showSourceChange(int(event.kind), event.path);
	}
}

bool ModelEditorDialog::sourceChangedOnDisk() const
{
	return m_sourceWatch && !m_sourceWatch->path.isEmpty() && m_sourceWatch->watcher.hasChangedSinceRegistered(m_sourceWatch->path);
}

void ModelEditorDialog::showSourceChange(int kind, const QString &path)
{
	auto &watch = *m_sourceWatch;
	const QString name = QFileInfo(path).fileName();
	const bool removed = DocumentChangeKind(kind) == DocumentChangeKind::Removed;
	watch.quiet = true;
	if (removed)
	{
		watch.notice->showNotice(QStringLiteral("warning"), QCoreApplication::translate("VibeStudioModelEditor", "Source file removed"),
								 QCoreApplication::translate("VibeStudioModelEditor", "%1 was deleted or moved outside the studio. Your edits stay open; Save As keeps them.").arg(name));
		auto *saveAs = watch.notice->addAction(QCoreApplication::translate("VibeStudioModelEditor", "Save As…"), QStringLiteral("save"), true);
		saveAs->setObjectName(QStringLiteral("meshSourceSaveAs"));
		connect(saveAs, &QPushButton::clicked, this, [this]() { save(true); });
	}
	else
	{
		watch.notice->showNotice(QStringLiteral("warning"), QCoreApplication::translate("VibeStudioModelEditor", "Source changed on disk"),
								 QCoreApplication::translate("VibeStudioModelEditor", "%1 was changed outside the studio. Reload it, compare it with this document, or keep "
									  "editing; Save then asks before replacing it.")
									 .arg(name));
		auto *reload = watch.notice->addAction(QCoreApplication::translate("VibeStudioModelEditor", "Reload from Disk"), QStringLiteral("refresh"), true);
		reload->setObjectName(QStringLiteral("meshSourceReload"));
		reload->setAccessibleDescription(QCoreApplication::translate("VibeStudioModelEditor", "Replace this document with the file on disk."));
		connect(reload, &QPushButton::clicked, this,
				[this]()
				{
					QString error;
					if (!reloadSource(&error) && !error.isEmpty())
						announceStatus(m_status, error, LiveStatus::Problem);
				});
		auto *compare = watch.notice->addAction(QCoreApplication::translate("VibeStudioModelEditor", "Compare"), QStringLiteral("info"));
		compare->setObjectName(QStringLiteral("meshSourceCompare"));
		connect(compare, &QPushButton::clicked, this,
				[this]()
				{
					QMessageBox::information(this, QCoreApplication::translate("VibeStudioModelEditor", "Compare with Disk"), sourceComparisonText());
				});
	}
	auto *keep = watch.notice->addAction(QCoreApplication::translate("VibeStudioModelEditor", "Keep Editing"));
	keep->setObjectName(QStringLiteral("meshSourceKeepEditing"));
	keep->setAccessibleDescription(QCoreApplication::translate("VibeStudioModelEditor", "Hide this notice until the file changes again. Save asks before replacing the file."));
	connect(keep, &QPushButton::clicked, this, [this]() { keepEditingOverSourceChange(); });
	watch.quiet = false;
	const QString message = removed ? QCoreApplication::translate("VibeStudioModelEditor", "The mesh source %1 was removed on disk.").arg(name)
									: QCoreApplication::translate("VibeStudioModelEditor", "The mesh source %1 changed on disk.").arg(name);
	m_status->setText(message);
	announceStatus(m_status, message, LiveStatus::Problem);
}

void ModelEditorDialog::keepEditingOverSourceChange()
{
	if (!m_sourceWatch || m_sourceWatch->path.isEmpty())
		return;
	auto &watch = *m_sourceWatch;
	watch.ignored = fingerprintDocumentFile(watch.path, watch.watcher.hashSizeLimit());
	watch.hasIgnored = true;
	watch.quiet = true;
	watch.notice->dismiss();
	watch.quiet = false;
	m_status->setText(QCoreApplication::translate("VibeStudioModelEditor", "Kept editing. The source on disk differs; Save asks before replacing it."));
}

int ModelEditorDialog::sourceSaveDecision(const QString &path)
{
	// A recovered draft goes back over its source only while that file still
	// holds what the copy started from.
	const bool draftOverSource = m_document.path().isEmpty() && !m_recoverySourceHash.isEmpty() && QFileInfo::exists(path) &&
								 modelPathsReferToSameFile(path, m_recoverySourcePath) &&
								 modelRecoveryFileSha256(path, modelDocumentMaxSourceBytes) != m_recoverySourceHash;
	if (!draftOverSource &&
		(m_document.path().isEmpty() || !modelPathsReferToSameFile(path, m_document.path()) || !sourceChangedOnDisk()))
		return 2;
	const QString name = QFileInfo(path).fileName();
	int choice = 0;
	if (sourceConflictChoice)
	{
		choice = sourceConflictChoice(path);
	}
	else
	{
		QMessageBox box(QMessageBox::Warning, QCoreApplication::translate("VibeStudioModelEditor", "Source Changed on Disk"),
						QCoreApplication::translate("VibeStudioModelEditor", "%1 changed outside the studio after you opened it. Replacing it loses those changes.").arg(name),
						QMessageBox::NoButton, this);
		box.setInformativeText(draftOverSource ? QCoreApplication::translate("VibeStudioModelEditor",
																			 "The file was saved or replaced after this recovery copy was taken.")
											   : sourceComparisonText());
		auto *saveAs = box.addButton(QCoreApplication::translate("VibeStudioModelEditor", "Save As…"), QMessageBox::AcceptRole);
		auto *replace = box.addButton(QCoreApplication::translate("VibeStudioModelEditor", "Replace Changed File"), QMessageBox::DestructiveRole);
		box.addButton(QMessageBox::Cancel);
		box.setDefaultButton(saveAs);
		box.exec();
		choice = box.clickedButton() == saveAs ? 1 : box.clickedButton() == replace ? 2 : 0;
	}
	if (choice != 2 || draftOverSource)
		return choice;
	// Re-inspects the file as it is now; a later change is refused again.
	QString error;
	if (!m_document.adoptSourceOnDisk(&error))
	{
		announceStatus(m_status, error, LiveStatus::Problem);
		return 0;
	}
	return 2;
}

bool ModelEditorDialog::reloadSource(QString *error)
{
	const QString path = m_sourceWatch ? m_sourceWatch->path : QString();
	if (path.isEmpty() || m_working)
		return false;
	if (!QFileInfo(path).isFile())
	{
		if (error)
			*error = QCoreApplication::translate("VibeStudioModelEditor", "%1 no longer exists. Use Save As to keep this document.").arg(QFileInfo(path).fileName());
		return false;
	}
	if (m_document.isModified())
	{
		int choice = 0;
		if (sourceReloadChoice)
		{
			choice = sourceReloadChoice(path);
		}
		else
		{
			QMessageBox box(QMessageBox::Warning, QCoreApplication::translate("VibeStudioModelEditor", "Reload Mesh Source"),
							QCoreApplication::translate("VibeStudioModelEditor", "Reloading replaces your unsaved edits with the file on disk."), QMessageBox::NoButton, this);
			auto *saveAs = box.addButton(QCoreApplication::translate("VibeStudioModelEditor", "Save Edits As…"), QMessageBox::AcceptRole);
			auto *discard = box.addButton(QCoreApplication::translate("VibeStudioModelEditor", "Discard Edits and Reload"), QMessageBox::DestructiveRole);
			box.addButton(QMessageBox::Cancel);
			box.setDefaultButton(saveAs);
			box.exec();
			choice = box.clickedButton() == saveAs ? 1 : box.clickedButton() == discard ? 2 : 0;
		}
		if (choice == 0 || (choice == 1 && !save(true)))
			return false;
		m_discardConfirmed = choice == 2;
	}
	const bool reloaded = openSource(path, error);
	m_discardConfirmed = false;
	if (reloaded)
		announceStatus(m_status, QCoreApplication::translate("VibeStudioModelEditor", "Reloaded %1 from disk.").arg(QFileInfo(path).fileName()), LiveStatus::Result);
	return reloaded;
}

QString ModelEditorDialog::sourceComparisonText() const
{
	const QString path = m_sourceWatch ? m_sourceWatch->path : QString();
	const QLocale locale;
	const auto summary = [&](const ModelMesh &mesh)
	{
		return QStringList{QCoreApplication::translate("VibeStudioModelEditor", "%Ln surface(s)", nullptr, mesh.surfaceCount),
					   QCoreApplication::translate("VibeStudioModelEditor", "%Ln vert(ex)(ices)", nullptr, mesh.vertexCount),
					   QCoreApplication::translate("VibeStudioModelEditor", "%Ln triangle(s)", nullptr, mesh.triangleCount),
					   QCoreApplication::translate("VibeStudioModelEditor", "%Ln frame(s)", nullptr, mesh.frameCount)}
			.join(QStringLiteral(", "));
	};
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioModelEditor", "This document: %1%2.")
				 .arg(summary(m_document.mesh()), m_document.isModified() ? QCoreApplication::translate("VibeStudioModelEditor", ", with unsaved edits") : QString());
	const QFileInfo info(path);
	if (path.isEmpty() || !info.isFile())
	{
		lines << QCoreApplication::translate("VibeStudioModelEditor", "On disk: the file is missing.");
		return lines.join(QLatin1Char('\n'));
	}
	const QString when = locale.toString(info.lastModified(), QLocale::ShortFormat);
	// Larger files are summarised by size and time; reading them belongs on the worker.
	QByteArray bytes;
	ModelMesh disk;
	if (info.size() <= 16LL * 1024 * 1024 && readModelFile(path, &bytes) && parseEditableModel(bytes, &disk))
	{
		updateEditableModelMetadata(&disk);
		lines << QCoreApplication::translate("VibeStudioModelEditor", "On disk: %1, saved %2.").arg(summary(disk), when);
	}
	else
	{
		lines << QCoreApplication::translate("VibeStudioModelEditor", "On disk: %1, saved %2; the file was not read here.").arg(locale.formattedDataSize(info.size()), when);
	}
	return lines.join(QLatin1Char('\n'));
}
} // namespace vibestudio
