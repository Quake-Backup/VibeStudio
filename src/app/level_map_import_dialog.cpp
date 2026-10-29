#include "app/level_map_import_dialog.h"

#include "core/compiler_runner.h"
#include "core/level_texture_mapping.h"
#include "core/studio_settings.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QMutex>
#include <QMutexLocker>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>

// Workflow reference: NetRadiant Custom ImportMap and Map_ImportFile at
// 449778b3 (GPL-2.0-or-later). Original Qt implementation; see docs/CREDITS.md.
namespace vibestudio {
struct LevelMapImportTask {
	std::atomic_bool cancel{false};
	std::atomic_bool converting{false};
	QMutex mutex;
	LevelMapLoadPhase phase = LevelMapLoadPhase::Reading;
	qint64 done = 0;
	qint64 total = 0;
	LevelMapDocument imported;
	bool succeeded = false;
	QString error;
	QString commandLine;
	QString log;
	qint64 bytes = -1;
	QThread* worker = nullptr;
};

LevelMapImportDialog::LevelMapImportDialog(const LevelMapDocument& destination, const QString& path, const LevelMapBspConversionContext& conversion,
	QWidget* parent)
	: QDialog(parent), m_destination(destination), m_path(QFileInfo(path).absoluteFilePath()), m_conversion(conversion)
{
	setObjectName(QStringLiteral("levelMapImportDialog"));
	setWindowTitle(tr("Import Map"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(tr("Preview what importing %1 adds to the open map.").arg(QFileInfo(m_path).fileName()));
	setWindowModality(Qt::WindowModal);
	setMinimumWidth(480);
	auto* layout = new QVBoxLayout(this);
	auto* file = new QLabel(QDir::toNativeSeparators(m_path), this);
	file->setObjectName(QStringLiteral("levelMapImportPath"));
	file->setAccessibleName(tr("Map to import"));
	file->setWordWrap(true);
	file->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(file);
	m_status = new QLabel(this);
	m_status->setObjectName(QStringLiteral("levelMapImportStatus"));
	m_status->setAccessibleName(tr("Import status"));
	m_status->setWordWrap(true);
	layout->addWidget(m_status);
	m_progress = new QProgressBar(this);
	m_progress->setObjectName(QStringLiteral("levelMapImportProgress"));
	m_progress->setAccessibleName(tr("Reading progress"));
	m_progress->setTextVisible(false);
	layout->addWidget(m_progress);
	m_summary = new QLabel(this);
	m_summary->setObjectName(QStringLiteral("levelMapImportSummary"));
	m_summary->setAccessibleName(tr("What the import adds"));
	m_summary->setWordWrap(true);
	layout->addWidget(m_summary);
	m_warnings = new QLabel(this);
	m_warnings->setObjectName(QStringLiteral("levelMapImportWarnings"));
	m_warnings->setAccessibleName(tr("Import notes"));
	m_warnings->setWordWrap(true);
	m_warnings->hide();
	layout->addWidget(m_warnings);
	m_rename = new QCheckBox(tr("Rename entity names the open map already uses"), this);
	m_rename->setObjectName(QStringLiteral("levelMapImportRename"));
	m_rename->setAccessibleName(m_rename->text());
	m_rename->setToolTip(tr("Give each clashing targetname and target the next free number, as NetRadiant Custom does, so imported links "
		"keep pointing at each other and not at the open map's entities."));
	m_rename->setChecked(true);
	connect(m_rename, &QCheckBox::toggled, this, [this] { refreshPreview(); });
	layout->addWidget(m_rename);
	m_details = new QPlainTextEdit(this);
	m_details->setObjectName(QStringLiteral("levelMapImportDetails"));
	m_details->setAccessibleName(tr("Import details"));
	m_details->setAccessibleDescription(tr("Renamed entity names and, for a compiled map, the VibeMap3 conversion command and its output."));
	m_details->setReadOnly(true);
	m_details->setMaximumHeight(fontMetrics().lineSpacing() * 8);
	m_details->hide();
	layout->addWidget(m_details);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
	m_import = buttons->addButton(tr("Import"), QDialogButtonBox::AcceptRole);
	m_import->setObjectName(QStringLiteral("levelMapImportButton"));
	m_import->setAccessibleName(tr("Import the map"));
	m_import->setAccessibleDescription(tr("Add the previewed objects at their own coordinates as one undo step."));
	m_import->setEnabled(false);
	m_import->setDefault(true);
	auto* cancel = buttons->button(QDialogButtonBox::Cancel);
	cancel->setObjectName(QStringLiteral("levelMapImportCancel"));
	cancel->setAccessibleName(tr("Cancel import"));
	connect(buttons, &QDialogButtonBox::accepted, this, [this] {
		if (m_ready) {
			accept();
		}
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	m_poll = new QTimer(this);
	m_poll->setInterval(60);
	connect(m_poll, &QTimer::timeout, this, &LevelMapImportDialog::poll);
	start();
}

LevelMapImportDialog::~LevelMapImportDialog()
{
	if (m_task && m_task->worker) {
		m_task->cancel = true;
		m_task->worker->wait();
		delete m_task->worker;
		m_task->worker = nullptr;
	}
}

LevelMapImportOptions LevelMapImportDialog::options() const
{
	LevelMapImportOptions options;
	options.renameTargets = m_rename->isChecked();
	return options;
}

void LevelMapImportDialog::reject()
{
	if (m_task) {
		m_task->cancel = true;
	}
	QDialog::reject();
}

void LevelMapImportDialog::start()
{
	m_task = std::make_shared<LevelMapImportTask>();
	const bool reducedMotion = StudioSettings().accessibilityPreferences().reducedMotion;
	m_progress->setRange(0, reducedMotion ? 1 : 0);
	const bool convert = levelMapImportNeedsConversion(m_path);
	QString converted;
	if (convert) {
		m_converted = std::make_unique<QTemporaryDir>();
		if (!m_converted->isValid()) {
			m_problem = tr("Could not create a folder for the converted map.");
			m_status->setText(m_problem);
			m_progress->setRange(0, 1);
			return;
		}
		converted = m_converted->filePath(QFileInfo(m_path).completeBaseName() + QStringLiteral("_converted.map"));
		m_status->setText(tr("Converting %1 to a map with VibeMap3…").arg(QFileInfo(m_path).fileName()));
	} else {
		m_status->setText(tr("Reading %1…").arg(QFileInfo(m_path).fileName()));
	}
	m_status->setAccessibleDescription(m_status->text());
	auto task = m_task;
	const auto path = m_path;
	const auto engine = m_destination.engineFamily;
	const auto dialect = levelMapBrushDialect(m_destination);
	const auto context = m_conversion;
	task->worker = QThread::create([task, path, engine, dialect, context, converted, convert] {
		QString source = path;
		if (convert) {
			// The header picks VibeMap3's game; Quake and Quake II BSPs are
			// refused before anything runs.
			CompilerCommandRequest request;
			QString refusal;
			if (!prepareLevelMapImportConversion(path, converted, dialect, context, &request, &refusal)) {
				task->error = refusal;
				return;
			}
			task->converting = true;
			CompilerRunRequest run;
			run.command = request;
			CompilerRunCallbacks callbacks;
			callbacks.cancellationRequested = [task] { return task->cancel.load(); };
			const auto result = runCompilerCommand(run, callbacks);
			task->converting = false;
			task->commandLine = result.plan.commandLine;
			task->log = (result.stdoutText + result.stderrText).trimmed();
			if (task->cancel) {
				return;
			}
			const auto problem = levelMapImportConversionProblem(result, converted);
			if (!problem.isEmpty()) {
				task->error = LevelMapImportDialog::tr("VibeMap3 could not convert the compiled map: %1").arg(problem);
				return;
			}
			source = converted;
		}
		LevelMapDocument document;
		QString error;
		const bool read = readLevelMapImport(source, engine, &document, &error, [task] { return task->cancel.load(); },
			[task](LevelMapLoadPhase phase, qint64 done, qint64 total) {
				QMutexLocker lock(&task->mutex);
				task->phase = phase;
				task->done = done;
				task->total = total;
			});
		if (!read) {
			task->error = error;
			return;
		}
		// Measured here, so the preview never copies the text on the UI thread.
		task->bytes = QFileInfo(source).size();
		// The undo step and reports name the file the user chose.
		document.sourcePath = path;
		task->imported = std::move(document);
		task->succeeded = true;
	});
	connect(task->worker, &QThread::finished, this, &LevelMapImportDialog::finish);
	task->worker->start();
	m_poll->start();
}

void LevelMapImportDialog::poll()
{
	if (!m_task || m_task->converting) {
		return;
	}
	LevelMapLoadPhase phase;
	qint64 done = 0, total = 0;
	{
		QMutexLocker lock(&m_task->mutex);
		phase = m_task->phase;
		done = m_task->done;
		total = m_task->total;
	}
	QString text;
	switch (phase) {
	case LevelMapLoadPhase::Reading:
		text = tr("Reading %1…").arg(QFileInfo(m_path).fileName());
		break;
	case LevelMapLoadPhase::Indexing:
	case LevelMapLoadPhase::Tokenizing:
	case LevelMapLoadPhase::Parsing:
		text = tr("Reading entities, brushes and patches…");
		break;
	case LevelMapLoadPhase::Solving:
		text = tr("Building brush geometry…");
		break;
	case LevelMapLoadPhase::Validating:
	case LevelMapLoadPhase::Hashing:
	case LevelMapLoadPhase::Complete:
		text = tr("Checking the map…");
		break;
	}
	if (total > 0) {
		m_progress->setRange(0, 1000);
		m_progress->setValue(static_cast<int>(1000.0 * static_cast<double>(std::clamp(done, qint64(0), total)) / static_cast<double>(total)));
	}
	if (m_status->text() != text) {
		m_status->setText(text);
		m_status->setAccessibleDescription(text);
	}
}

void LevelMapImportDialog::finish()
{
	m_poll->stop();
	auto task = m_task;
	if (!task) {
		return;
	}
	task->worker->deleteLater();
	task->worker = nullptr;
	m_progress->setRange(0, 1);
	m_progress->setValue(1);
	if (!task->commandLine.isEmpty()) {
		m_details->setPlainText(tr("Conversion: %1").arg(task->commandLine) + (task->log.isEmpty() ? QString() : QStringLiteral("\n\n") + task->log));
		m_details->show();
	}
	if (task->cancel) {
		m_problem = tr("The import was cancelled; the map was not changed.");
		m_status->setText(m_problem);
		return;
	}
	if (!task->succeeded) {
		m_problem = task->error.isEmpty() ? tr("The map could not be read.") : task->error;
		m_status->setText(m_problem);
		m_status->setAccessibleDescription(m_problem);
		return;
	}
	m_imported = std::make_shared<const LevelMapDocument>(std::move(task->imported));
	m_bytes = task->bytes;
	refreshPreview();
}

void LevelMapImportDialog::refreshPreview()
{
	if (!m_imported) {
		return;
	}
	m_preview = previewLevelMapImport(m_destination, *m_imported, options(), m_bytes);
	const auto number = [this](qint64 value) { return locale().toString(value); };
	m_summary->setText(tr("Entities: %1\nBrushes: %2\nPatches: %3\nGroups: %4, linked: %5\nLayers: %6\nFormat: %7, %8")
						   .arg(number(m_preview.entities), number(m_preview.brushes), number(m_preview.patches), number(m_preview.groups),
								number(m_preview.linkedGroups), number(m_preview.layers), m_preview.engineFamily,
								m_preview.dialect.isEmpty() ? m_preview.format : m_preview.dialect));
	m_summary->setAccessibleDescription(m_summary->text());
	QStringList notes = m_preview.warnings;
	if (!m_preview.problem.isEmpty()) {
		notes.prepend(m_preview.problem);
	}
	m_warnings->setText(notes.join(QLatin1Char('\n')));
	m_warnings->setAccessibleDescription(m_warnings->text());
	m_warnings->setVisible(!notes.isEmpty());
	QStringList details;
	for (auto it = m_preview.renamedTargets.cbegin(); it != m_preview.renamedTargets.cend(); ++it) {
		details << tr("Rename %1 to %2").arg(it.key(), it.value());
	}
	if (!details.isEmpty() || m_details->isVisible()) {
		QString text = details.join(QLatin1Char('\n'));
		if (m_task && !m_task->commandLine.isEmpty()) {
			text += (text.isEmpty() ? QString() : QStringLiteral("\n\n")) + tr("Conversion: %1").arg(m_task->commandLine)
				+ (m_task->log.isEmpty() ? QString() : QStringLiteral("\n\n") + m_task->log);
		}
		m_details->setPlainText(text);
		m_details->setVisible(!text.isEmpty());
	}
	m_problem = m_preview.problem;
	m_ready = m_preview.importable();
	m_import->setEnabled(m_ready);
	const auto status = m_ready ? tr("Ready to import %1 at its own coordinates as one undo step.").arg(QFileInfo(m_path).fileName())
								: m_preview.problem;
	m_status->setText(status);
	m_status->setAccessibleDescription(status);
}
} // namespace vibestudio
