#include "app/model_audit_dialog.h"

#include "core/model_decode_issues.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QTableWidget>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace vibestudio
{
ModelAuditDialog::ModelAuditDialog(const QString &path, QWidget *parent) : QDialog(parent)
{
	setObjectName(QStringLiteral("modelAuditDialog"));
	setWindowTitle(QCoreApplication::translate("VibeStudioModelAudit", "Audit Models"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(QCoreApplication::translate("VibeStudioModelAudit",
		"Decode every model in a package, folder or installation and report decode issues, editability and round trips. Nothing is written to the source."));
	auto *layout = new QVBoxLayout(this);
	auto *form = new QFormLayout;
	m_path = new QLineEdit(path);
	m_path->setObjectName(QStringLiteral("modelAuditPath"));
	m_path->setAccessibleName(QCoreApplication::translate("VibeStudioModelAudit", "Package, folder or installation to audit"));
	m_browsePackage = new QPushButton(QCoreApplication::translate("VibeStudioModelAudit", "Package…"));
	m_browsePackage->setObjectName(QStringLiteral("modelAuditBrowsePackage"));
	m_browsePackage->setAccessibleName(QCoreApplication::translate("VibeStudioModelAudit", "Choose a package to audit"));
	m_browseFolder = new QPushButton(QCoreApplication::translate("VibeStudioModelAudit", "Folder…"));
	m_browseFolder->setObjectName(QStringLiteral("modelAuditBrowseFolder"));
	m_browseFolder->setAccessibleName(QCoreApplication::translate("VibeStudioModelAudit", "Choose a folder or game installation to audit"));
	auto *pathRow = new QHBoxLayout;
	pathRow->addWidget(m_path, 1);
	pathRow->addWidget(m_browsePackage);
	pathRow->addWidget(m_browseFolder);
	form->addRow(QCoreApplication::translate("VibeStudioModelAudit", "&Source"), pathRow);
	m_formats = new QLineEdit;
	m_formats->setObjectName(QStringLiteral("modelAuditFormats"));
	m_formats->setPlaceholderText(QCoreApplication::translate("VibeStudioModelAudit", "Every format, or ids such as mdl,md3"));
	m_formats->setAccessibleName(QCoreApplication::translate("VibeStudioModelAudit", "Formats to audit"));
	form->addRow(QCoreApplication::translate("VibeStudioModelAudit", "&Formats"), m_formats);
	m_roundTrip = new QCheckBox(QCoreApplication::translate("VibeStudioModelAudit", "Write each model back in its own format and compare"));
	m_roundTrip->setObjectName(QStringLiteral("modelAuditRoundTrip"));
	m_roundTrip->setAccessibleDescription(QCoreApplication::translate("VibeStudioModelAudit",
		"In memory only: MDL, MD2, MD3, MD5, IQM, ASE and OBJ models are written and decoded again, and every corner is compared within the format's quantisation."));
	form->addRow(QString(), m_roundTrip);
	layout->addLayout(form);

	// The task strip: what is happening, how far, and Cancel.
	auto *strip = new QHBoxLayout;
	m_status = new QLabel(QCoreApplication::translate("VibeStudioModelAudit", "Choose a package, folder or installation, then Audit."));
	m_status->setObjectName(QStringLiteral("modelAuditStatus"));
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	m_status->setAccessibleName(QCoreApplication::translate("VibeStudioModelAudit", "Audit status"));
	m_progress = new QProgressBar;
	m_progress->setObjectName(QStringLiteral("modelAuditProgress"));
	m_progress->setAccessibleName(QCoreApplication::translate("VibeStudioModelAudit", "Audit progress"));
	m_progress->setRange(0, 1);
	m_progress->setValue(0);
	m_progress->setTextVisible(true);
	strip->addWidget(m_status, 1);
	strip->addWidget(m_progress);
	layout->addLayout(strip);

	m_summary = new QTableWidget(0, 9);
	m_summary->setObjectName(QStringLiteral("modelAuditSummary"));
	m_summary->setAccessibleName(QCoreApplication::translate("VibeStudioModelAudit", "Summary by format"));
	m_summary->setHorizontalHeaderLabels({QCoreApplication::translate("VibeStudioModelAudit", "Format"),
		QCoreApplication::translate("VibeStudioModelAudit", "Models"), QCoreApplication::translate("VibeStudioModelAudit", "Decoded"),
		QCoreApplication::translate("VibeStudioModelAudit", "Clean"), QCoreApplication::translate("VibeStudioModelAudit", "Advisories"),
		QCoreApplication::translate("VibeStudioModelAudit", "Omissions"), QCoreApplication::translate("VibeStudioModelAudit", "Damaged"),
		QCoreApplication::translate("VibeStudioModelAudit", "Editable"), QCoreApplication::translate("VibeStudioModelAudit", "Round trips")});
	m_summary->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_summary->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_summary->verticalHeader()->setVisible(false);
	m_summary->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

	auto *entriesPane = new QWidget;
	auto *entriesLayout = new QVBoxLayout(entriesPane);
	entriesLayout->setContentsMargins(0, 0, 0, 0);
	m_show = new QComboBox;
	m_show->setObjectName(QStringLiteral("modelAuditShow"));
	m_show->setAccessibleName(QCoreApplication::translate("VibeStudioModelAudit", "Entries to show"));
	m_show->addItem(QCoreApplication::translate("VibeStudioModelAudit", "Models that need attention"));
	m_show->addItem(QCoreApplication::translate("VibeStudioModelAudit", "Every model"));
	entriesLayout->addWidget(m_show);
	m_entries = new QTreeWidget;
	m_entries->setObjectName(QStringLiteral("modelAuditEntries"));
	m_entries->setAccessibleName(QCoreApplication::translate("VibeStudioModelAudit", "Audited models"));
	m_entries->setAccessibleDescription(QCoreApplication::translate("VibeStudioModelAudit", "Select a model to read its decode issues and round trip in the details."));
	m_entries->setHeaderLabels({QCoreApplication::translate("VibeStudioModelAudit", "Model"), QCoreApplication::translate("VibeStudioModelAudit", "Format"),
		QCoreApplication::translate("VibeStudioModelAudit", "Verdict"), QCoreApplication::translate("VibeStudioModelAudit", "Editable")});
	m_entries->setRootIsDecorated(false);
	m_entries->setUniformRowHeights(true);
	entriesLayout->addWidget(m_entries, 1);
	m_details = new QPlainTextEdit;
	m_details->setObjectName(QStringLiteral("modelAuditDetails"));
	m_details->setReadOnly(true);
	m_details->setAccessibleName(QCoreApplication::translate("VibeStudioModelAudit", "Model details"));
	auto *split = new QSplitter(Qt::Vertical);
	split->setChildrenCollapsible(false);
	split->addWidget(m_summary);
	split->addWidget(entriesPane);
	split->addWidget(m_details);
	layout->addWidget(split, 1);

	auto *buttons = new QDialogButtonBox;
	m_start = buttons->addButton(QCoreApplication::translate("VibeStudioModelAudit", "&Audit"), QDialogButtonBox::ActionRole);
	m_start->setObjectName(QStringLiteral("modelAuditStart"));
	m_start->setDefault(true);
	m_cancel = buttons->addButton(QCoreApplication::translate("VibeStudioModelAudit", "&Cancel Audit"), QDialogButtonBox::ActionRole);
	m_cancel->setObjectName(QStringLiteral("modelAuditCancel"));
	m_save = buttons->addButton(QCoreApplication::translate("VibeStudioModelAudit", "&Save Report…"), QDialogButtonBox::ActionRole);
	m_save->setObjectName(QStringLiteral("modelAuditSave"));
	m_close = buttons->addButton(QDialogButtonBox::Close);
	m_close->setObjectName(QStringLiteral("modelAuditClose"));
	for (auto *button : {m_start, m_cancel, m_save, m_close, m_browsePackage, m_browseFolder})
	{
		if (button->accessibleName().isEmpty())
			button->setAccessibleName(button->text().remove(QLatin1Char('&')));
	}
	layout->addWidget(buttons);

	connect(m_start, &QPushButton::clicked, this, [this] { start(); });
	connect(m_cancel, &QPushButton::clicked, this, [this] { cancel(); });
	connect(m_save, &QPushButton::clicked, this, [this] { saveReport(); });
	connect(m_close, &QPushButton::clicked, this, [this] { reject(); });
	connect(m_browsePackage, &QPushButton::clicked, this, [this] {
		const auto chosen = QFileDialog::getOpenFileName(this, QCoreApplication::translate("VibeStudioModelAudit", "Audit a Package"), m_path->text(),
			QCoreApplication::translate("VibeStudioModelAudit", "Packages (*.pak *.pk3 *.pk4 *.wad *.zip);;All files (*)"));
		if (!chosen.isEmpty())
			m_path->setText(QDir::toNativeSeparators(chosen));
	});
	connect(m_browseFolder, &QPushButton::clicked, this, [this] {
		const auto chosen = QFileDialog::getExistingDirectory(this, QCoreApplication::translate("VibeStudioModelAudit", "Audit a Folder or Installation"), m_path->text());
		if (!chosen.isEmpty())
			m_path->setText(QDir::toNativeSeparators(chosen));
	});
	connect(m_show, &QComboBox::currentIndexChanged, this, [this] { showReport(); });
	connect(m_entries, &QTreeWidget::currentItemChanged, this, [this] { showEntry(); });
	setRunning(false);
	resize(900, 640);
}

ModelAuditDialog::~ModelAuditDialog()
{
	finished = {};
	waitForWorker();
}

void ModelAuditDialog::waitForWorker()
{
	if (!m_thread)
		return;
	if (m_cancelled)
		m_cancelled->store(true);
	m_thread->wait();
	delete m_thread;
	m_thread = nullptr;
}

void ModelAuditDialog::setRunning(bool running)
{
	m_start->setEnabled(!running);
	m_cancel->setEnabled(running);
	m_save->setEnabled(!running && !m_report.source.isEmpty());
	m_path->setEnabled(!running);
	m_formats->setEnabled(!running);
	m_roundTrip->setEnabled(!running);
	m_browsePackage->setEnabled(!running);
	m_browseFolder->setEnabled(!running);
	setProperty("operationRunning", running);
}

bool ModelAuditDialog::start()
{
	if (m_thread)
		return false;
	const QString path = m_path->text().trimmed();
	if (path.isEmpty())
	{
		m_status->setText(QCoreApplication::translate("VibeStudioModelAudit", "Choose a package, folder or installation first."));
		return false;
	}
	ModelAuditOptions options;
	options.roundTrip = m_roundTrip->isChecked();
	for (const QString &format : m_formats->text().split(QLatin1Char(','), Qt::SkipEmptyParts))
		options.formats << format.trimmed().toLower();
	m_cancelled = std::make_shared<std::atomic_bool>(false);
	const QPointer<ModelAuditDialog> self(this);
	options.progress = [self](int done, int total, const QString &label) {
		QMetaObject::invokeMethod(
			qApp, [self, done, total, label] {
				if (self)
					self->progress(done, total, label);
			},
			Qt::QueuedConnection);
	};
	m_report = {};
	m_summary->setRowCount(0);
	m_entries->clear();
	m_details->clear();
	m_progress->setRange(0, 0);
	m_status->setText(QCoreApplication::translate("VibeStudioModelAudit", "Opening %1…").arg(path));
	setRunning(true);
	const auto cancelled = m_cancelled;
	m_thread = QThread::create([self, path, options, cancelled] {
		ModelWorkControl control;
		control.cancelled = [cancelled] { return cancelled->load(); };
		auto report = std::make_shared<ModelAuditReport>();
		QString error;
		const bool ok = auditModels(path, options, report.get(), &error, control);
		QMetaObject::invokeMethod(
			qApp, [self, report, error, ok] {
				if (self)
					self->completed(*report, error, ok);
			},
			Qt::QueuedConnection);
	});
	m_thread->start();
	return true;
}

void ModelAuditDialog::cancel()
{
	if (m_cancelled && m_thread)
	{
		m_cancelled->store(true);
		m_status->setText(QCoreApplication::translate("VibeStudioModelAudit", "Cancelling the audit…"));
		m_cancel->setEnabled(false);
	}
}

void ModelAuditDialog::progress(int done, int total, const QString &label)
{
	if (!m_thread)
		return;
	m_progress->setRange(0, std::max(1, total));
	m_progress->setValue(done);
	if (!label.isEmpty() && !(m_cancelled && m_cancelled->load()))
		m_status->setText(QCoreApplication::translate("VibeStudioModelAudit", "Auditing %1 of %2: %3")
							  .arg(QLocale().toString(done + 1), QLocale().toString(total), label));
}

void ModelAuditDialog::completed(const ModelAuditReport &report, const QString &error, bool ok)
{
	if (m_thread)
	{
		m_thread->wait();
		delete m_thread;
		m_thread = nullptr;
	}
	m_report = report;
	m_progress->setRange(0, 1);
	m_progress->setValue(1);
	const int models = int(report.entries.size());
	if (!ok)
		m_status->setText(QCoreApplication::translate("VibeStudioModelAudit", "The audit could not start: %1").arg(error));
	else if (report.cancelled)
		m_status->setText(QCoreApplication::translate("VibeStudioModelAudit", "Cancelled after %n model(s); the results so far are shown.", nullptr, models));
	else
	{
		int attention = 0;
		for (const auto &entry : report.entries)
			attention += entry.verdict != QStringLiteral("clean") ? 1 : 0;
		m_status->setText(QCoreApplication::translate("VibeStudioModelAudit", "Audited %n model(s): %1 need attention.", nullptr, models)
							  .arg(QLocale().toString(attention)));
	}
	m_status->setAccessibleDescription(m_status->text());
	setRunning(false);
	showReport();
	if (finished)
		finished(m_report, ok ? QString() : error);
}

void ModelAuditDialog::showReport()
{
	m_summary->setRowCount(int(m_report.formats.size()));
	const QLocale locale;
	for (int row = 0; row < m_report.formats.size(); ++row)
	{
		const auto &summary = m_report.formats.at(row);
		const QStringList cells{summary.format,
			locale.toString(summary.entries),
			locale.toString(summary.decoded),
			locale.toString(summary.clean),
			locale.toString(summary.advisories),
			locale.toString(summary.needsAcceptance),
			locale.toString(summary.damaged),
			locale.toString(summary.editable),
			summary.roundTrips > 0 ? QCoreApplication::translate("VibeStudioModelAudit", "%1 of %2 within tolerance")
										 .arg(locale.toString(summary.roundTripsWithin), locale.toString(summary.roundTrips))
								   : QString()};
		for (int column = 0; column < cells.size(); ++column)
			m_summary->setItem(row, column, new QTableWidgetItem(cells.at(column)));
	}
	m_entries->clear();
	const bool everything = m_show->currentIndex() == 1;
	for (int index = 0; index < m_report.entries.size(); ++index)
	{
		const auto &entry = m_report.entries.at(index);
		const bool attention = entry.verdict != QStringLiteral("clean") || (entry.roundTrip.attempted && !entry.roundTrip.withinTolerance());
		if (!everything && !attention)
			continue;
		// Verdicts in words, never colour alone.
		QString verdict = entry.verdict;
		if (entry.verdict == QStringLiteral("clean"))
			verdict = QCoreApplication::translate("VibeStudioModelAudit", "Clean");
		else if (entry.verdict == QStringLiteral("advisories"))
			verdict = QCoreApplication::translate("VibeStudioModelAudit", "Advisories");
		else if (entry.verdict == QStringLiteral("accept-omissions"))
			verdict = QCoreApplication::translate("VibeStudioModelAudit", "Omissions to accept");
		else if (entry.verdict == QStringLiteral("damaged"))
			verdict = QCoreApplication::translate("VibeStudioModelAudit", "Damaged");
		else if (entry.verdict == QStringLiteral("failed"))
			verdict = QCoreApplication::translate("VibeStudioModelAudit", "Does not decode");
		else if (entry.verdict == QStringLiteral("skipped"))
			verdict = QCoreApplication::translate("VibeStudioModelAudit", "Skipped");
		auto *item = new QTreeWidgetItem(m_entries, {entry.path, entry.format, verdict,
			entry.editable ? QCoreApplication::translate("VibeStudioModelAudit", "Yes") : QCoreApplication::translate("VibeStudioModelAudit", "No")});
		item->setData(0, Qt::UserRole, index);
		item->setToolTip(0, entry.container);
	}
	m_entries->resizeColumnToContents(0);
	if (m_entries->topLevelItemCount() > 0)
		m_entries->setCurrentItem(m_entries->topLevelItem(0));
	else
		m_details->setPlainText(m_report.entries.isEmpty() && !m_report.source.isEmpty()
									? QCoreApplication::translate("VibeStudioModelAudit", "No models were found.")
									: QString());
}

void ModelAuditDialog::showEntry()
{
	const auto *item = m_entries->currentItem();
	if (!item)
		return;
	const int index = item->data(0, Qt::UserRole).toInt();
	if (index < 0 || index >= m_report.entries.size())
		return;
	const auto &entry = m_report.entries.at(index);
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioModelAudit", "%1 in %2").arg(entry.path, entry.container);
	if (!entry.error.isEmpty())
		lines << entry.error;
	if (entry.skipped)
		lines << entry.skipReason;
	if (entry.decoded)
		lines << QCoreApplication::translate("VibeStudioModelAudit", "%1 surface(s), %2 frame(s), %3 triangle(s).")
					 .arg(QLocale().toString(entry.surfaces), QLocale().toString(entry.frames), QLocale().toString(entry.triangles));
	lines += modelDecodeIssueLines(entry.issues);
	if (!entry.editable && !entry.editableError.isEmpty())
		lines << QCoreApplication::translate("VibeStudioModelAudit", "Not editable: %1").arg(entry.editableError);
	else if (entry.needsAcceptance)
		lines << QCoreApplication::translate("VibeStudioModelAudit", "Editable once its omissions are accepted.");
	if (entry.roundTrip.attempted)
	{
		const auto &trip = entry.roundTrip;
		lines << (!trip.error.isEmpty() ? QCoreApplication::translate("VibeStudioModelAudit", "Round trip as %1 failed: %2").arg(trip.format, trip.error)
				  : trip.withinTolerance() ? QCoreApplication::translate("VibeStudioModelAudit", "Round trip as %1: every corner within %2.")
												 .arg(trip.format, QLocale().toString(trip.tolerance, 'g', 4))
										   : QCoreApplication::translate("VibeStudioModelAudit", "Round trip as %1: a corner moved %2 (tolerance %3), %4 triangle(s) and %5 frame(s) changed.")
												 .arg(trip.format, QLocale().toString(trip.maxPositionError, 'g', 4), QLocale().toString(trip.tolerance, 'g', 4),
													  QLocale().toString(trip.triangleDelta), QLocale().toString(trip.frameDelta)));
	}
	m_details->setPlainText(lines.join(QLatin1Char('\n')));
}

void ModelAuditDialog::saveReport()
{
	if (m_report.source.isEmpty())
		return;
	const auto path = QFileDialog::getSaveFileName(this, QCoreApplication::translate("VibeStudioModelAudit", "Save Audit Report"), QString(),
		QCoreApplication::translate("VibeStudioModelAudit", "Audit reports (*.json)"));
	if (path.isEmpty())
		return;
	const QFileInfo target(path), source(m_report.source);
	if (source.isDir() ? target.absoluteFilePath().startsWith(source.absoluteFilePath() + QLatin1Char('/'), Qt::CaseInsensitive)
					   : target.absoluteFilePath().compare(source.absoluteFilePath(), Qt::CaseInsensitive) == 0)
	{
		m_status->setText(QCoreApplication::translate("VibeStudioModelAudit", "Save the report outside the audited source; the audit never writes into it."));
		return;
	}
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(modelAuditJson(m_report, true)).toJson(QJsonDocument::Indented)) < 0 || !file.commit())
	{
		m_status->setText(QCoreApplication::translate("VibeStudioModelAudit", "The report could not be saved: %1").arg(file.errorString()));
		return;
	}
	m_status->setText(QCoreApplication::translate("VibeStudioModelAudit", "Saved the report to %1.").arg(QDir::toNativeSeparators(path)));
}

void ModelAuditDialog::closeEvent(QCloseEvent *event)
{
	waitForWorker();
	QDialog::closeEvent(event);
}

void ModelAuditDialog::reject()
{
	waitForWorker();
	QDialog::reject();
}

} // namespace vibestudio
