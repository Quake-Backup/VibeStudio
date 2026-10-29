// The Mesh Editor's Outputs: the game files delivered from this source
// (exports, staged entries), read from the project's receipts
// (core/model_outputs.h), each with a status that says it in words, and
// Refresh Stale, which re-exports the stale ones with their recorded settings
// on a worker. Successful exports and stages leave a receipt through the
// shell, which owns the project manifest.

#include "app/model_editor_dialog.h"

#include "app/studio_accessibility.h"
#include "app/studio_icons.h"
#include "core/model_delivery.h"
#include "core/model_outputs.h"
#include "core/package_archive.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QThread>

#include <atomic>
#include <memory>

namespace vibestudio
{
struct ModelOutputsState
{
	QListWidget *list = nullptr;
	QPushButton *check = nullptr;
	QPushButton *refresh = nullptr;
	QThread *thread = nullptr;
	std::shared_ptr<std::atomic_bool> cancel = std::make_shared<std::atomic_bool>(false);
	QVector<ModelOutputRecord> records;
	QVector<ModelOutputStatus> states;
	QString projectRoot;
	// The last receipt that could not be saved.
	QString problem;
};

namespace
{
QString outputsProjectRoot(const QString &sourcePath, const std::function<ModelEditorDeliveryContext()> &delivery)
{
	const QString root = delivery ? delivery().projectRoot : QString();
	return !root.isEmpty() ? root : sourcePath.isEmpty() ? QString() : findModelProjectRoot(sourcePath);
}
} // namespace

void ModelEditorDialog::addOutputControls(QFormLayout *page)
{
	m_outputs = std::make_shared<ModelOutputsState>();
	auto &state = *m_outputs;
	state.list = new QListWidget;
	state.list->setObjectName(QStringLiteral("meshOutputs"));
	state.list->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Delivered outputs"));
	state.list->setAccessibleDescription(QCoreApplication::translate(
		"VibeStudioModelEditor", "The game files exported or staged from this source, each with whether it is current or stale."));
	state.list->setWordWrap(true);
	state.list->setMinimumHeight(fontMetrics().lineSpacing() * 4);
	state.list->setLayoutDirection(Qt::LeftToRight);
	page->addRow(QCoreApplication::translate("VibeStudioModelEditor", "Outputs"), state.list);
	auto *row = new QWidget;
	auto *buttons = new QHBoxLayout(row);
	buttons->setContentsMargins(0, 0, 0, 0);
	state.check = new QPushButton(QCoreApplication::translate("VibeStudioModelEditor", "Check Outputs"));
	state.check->setObjectName(QStringLiteral("meshCheckOutputs"));
	state.check->setAccessibleDescription(
		QCoreApplication::translate("VibeStudioModelEditor", "Compare every delivered output and its source with their receipts."));
	state.check->setToolTip(state.check->accessibleDescription());
	state.refresh = new QPushButton(QCoreApplication::translate("VibeStudioModelEditor", "Refresh Stale"));
	state.refresh->setObjectName(QStringLiteral("meshRefreshOutputs"));
	state.refresh->setAccessibleDescription(QCoreApplication::translate(
		"VibeStudioModelEditor",
		"Export the stale outputs again from the saved source with the settings they were made with. An output changed elsewhere is "
		"never replaced."));
	state.refresh->setToolTip(state.refresh->accessibleDescription());
	buttons->addWidget(state.check);
	buttons->addWidget(state.refresh);
	buttons->addStretch(1);
	page->addRow(row);
	connect(state.check, &QPushButton::clicked, this, [this]() { refreshOutputs(); });
	connect(state.refresh, &QPushButton::clicked, this, [this]() { refreshStaleOutputs(); });
	refreshOutputs();
}

void ModelEditorDialog::cancelOutputsWork()
{
	if (!m_outputs || !m_outputs->thread)
		return;
	m_outputs->cancel->store(true);
	m_outputs->thread->wait();
}

void ModelEditorDialog::showOutputs()
{
	auto &state = *m_outputs;
	state.list->clear();
	if (!state.problem.isEmpty())
		state.list->addItem(QCoreApplication::translate("VibeStudioModelEditor", "The last receipt was not saved: %1").arg(state.problem));
	if (state.projectRoot.isEmpty())
	{
		state.list->addItem(QCoreApplication::translate("VibeStudioModelEditor", "Open a project to keep receipts of exports and staged models."));
	}
	else if (state.records.isEmpty())
	{
		state.list->addItem(QCoreApplication::translate("VibeStudioModelEditor", "Nothing has been exported or staged from this source yet."));
	}
	const auto staged = m_materialSource.archive;
	for (int index = 0; index < state.records.size(); ++index)
	{
		const auto &record = state.records.at(index);
		const auto status = state.states.value(index);
		QString words = state.states.size() == state.records.size() ? modelOutputStateText(status.state)
																	 : QCoreApplication::translate("VibeStudioModelEditor", "Checking…");
		// A staged entry is in the package plan before the package is saved.
		if (status.state == ModelOutputState::Missing && staged && !record.outputs.isEmpty() && !record.outputs.first().package.isEmpty())
		{
			const auto entries = staged->entries();
			const auto entry = record.outputs.first().entry;
			if (std::any_of(entries.cbegin(), entries.cend(), [&](const PackageEntry &e) { return e.virtualPath.compare(entry, Qt::CaseInsensitive) == 0; }))
				words = QCoreApplication::translate("VibeStudioModelEditor", "Staged; save the package");
		}
		const auto *first = record.outputs.isEmpty() ? nullptr : &record.outputs.first();
		auto *item = new QListWidgetItem(QStringLiteral("%1 · %2").arg(words, first ? first->display() : record.kind));
		// The state is in the words; the icon only repeats it.
		item->setIcon(studioIcon(status.state == ModelOutputState::Current ? QStringLiteral("check")
								 : modelOutputStateIsStale(status.state) ? QStringLiteral("warning")
																		  : QStringLiteral("info")));
		item->setToolTip(status.detail);
		item->setData(Qt::AccessibleDescriptionRole, status.detail);
		state.list->addItem(item);
	}
	const bool busy = state.thread != nullptr;
	const bool stale = std::any_of(state.states.cbegin(), state.states.cend(), [](const ModelOutputStatus &s) { return modelOutputStateIsStale(s.state); });
	state.check->setEnabled(!busy && !state.projectRoot.isEmpty());
	state.refresh->setEnabled(!busy && stale && !m_document.path().isEmpty());
}

void ModelEditorDialog::refreshOutputs()
{
	if (!m_outputs || m_outputs->thread)
		return;
	auto &state = *m_outputs;
	const QString source = m_document.path();
	state.projectRoot = outputsProjectRoot(source, deliveryContext);
	state.records.clear();
	state.states.clear();
	if (!state.projectRoot.isEmpty() && !source.isEmpty())
	{
		ProjectManifest manifest;
		if (loadProjectManifest(state.projectRoot, &manifest))
			state.records = modelOutputsForSource(projectModelOutputs(manifest), source);
	}
	if (state.records.isEmpty())
	{
		showOutputs();
		return;
	}
	// Hashing outputs can take a while: on a worker, values only.
	state.cancel = std::make_shared<std::atomic_bool>(false);
	auto records = state.records;
	auto result = std::make_shared<QVector<ModelOutputStatus>>();
	state.thread = QThread::create([records, result, cancel = state.cancel]()
								   {
									   for (const auto &record : records)
										   result->append(modelOutputStatus(record, nullptr, [cancel] { return cancel->load(); }));
								   });
	state.thread->setParent(this);
	connect(state.thread, &QThread::finished, this,
			[this, result, records]()
			{
				auto &current = *m_outputs;
				current.thread->deleteLater();
				current.thread = nullptr;
				if (!current.cancel->load() && result->size() == records.size())
					current.states = *result;
				showOutputs();
			});
	showOutputs();
	state.thread->start();
}

void ModelEditorDialog::refreshStaleOutputs()
{
	if (!m_outputs || m_outputs->thread || m_working)
		return;
	if (m_document.isModified())
	{
		m_status->setText(QCoreApplication::translate("VibeStudioModelEditor", "Save the source first: Refresh Stale builds outputs from the saved file."),
						  LiveStatus::Problem);
		return;
	}
	auto &state = *m_outputs;
	QVector<ModelOutputRecord> stale;
	for (int index = 0; index < state.records.size() && index < state.states.size(); ++index)
	{
		if (modelOutputStateIsStale(state.states.at(index).state))
			stale.append(state.records.at(index));
	}
	if (stale.isEmpty())
		return;
	state.cancel = std::make_shared<std::atomic_bool>(false);
	auto results = std::make_shared<QVector<ModelOutputRefresh>>();
	state.thread = QThread::create([stale, results, cancel = state.cancel]()
								   {
									   ModelWorkControl control;
									   control.cancelled = [cancel] { return cancel->load(); };
									   for (const auto &record : stale)
										   results->append(refreshModelOutput(record, false, control));
								   });
	state.thread->setParent(this);
	connect(state.thread, &QThread::finished, this,
			[this, results]()
			{
				auto &current = *m_outputs;
				current.thread->deleteLater();
				current.thread = nullptr;
				int refreshed = 0;
				QStringList problems;
				for (const auto &refresh : std::as_const(*results))
				{
					if (!refresh.error.isEmpty())
					{
						problems << refresh.error;
						continue;
					}
					QString error;
					if (recordOutput && !recordOutput(refresh.record, &error))
						problems << error;
					++refreshed;
				}
				const QString message =
					problems.isEmpty()
						? QCoreApplication::translate("VibeStudioModelEditor", "Refreshed %n output(s) from the saved source.", nullptr, refreshed)
						: QCoreApplication::translate("VibeStudioModelEditor", "Refreshed %n output(s); %1", nullptr, refreshed).arg(problems.join(QLatin1Char(' ')));
				m_status->setText(message, problems.isEmpty() ? LiveStatus::Result : LiveStatus::Problem);
				refreshOutputs();
			});
	showOutputs();
	state.thread->start();
}

void ModelEditorDialog::recordDeliveredOutput(const QString &format, const ModelExportOptions &options, const ModelOutputFile &output,
											  const QByteArray &bytes)
{
	if (!recordOutput)
		return;
	// Exports of unsaved edits are recorded without a source hash: they cannot be judged.
	const QByteArray sourceSha256 = m_document.isModified() ? QByteArray() : m_document.sourceFingerprint();
	const auto record = modelExportOutputRecord(m_document.path(), sourceSha256, format, options, output, bytes);
	QString error;
	if (!recordOutput(record, &error) && !error.isEmpty())
		m_exportReport->setToolTip(m_exportReport->toolTip() + QLatin1Char('\n') + error);
	refreshOutputs();
}

void ModelEditorDialog::prefillPackagePath()
{
	// The game path the source came from (an imported progs/player.mdl), when
	// it is a model path a package can hold.
	const QString source = QDir::fromNativeSeparators(m_document.mesh().sourcePath);
	const QString suffix = QFileInfo(source).suffix().toLower();
	static const QStringList stageable{QStringLiteral("mdl"), QStringLiteral("md2"), QStringLiteral("md3"), QStringLiteral("md5mesh"),
									   QStringLiteral("md5anim"), QStringLiteral("iqm"), QStringLiteral("ase")};
	if (source.isEmpty() || QDir::isAbsolutePath(source) || source.startsWith(QLatin1Char('/')) || source.contains(QStringLiteral("..")) ||
		source.contains(QLatin1Char(':')) || !stageable.contains(suffix))
		return;
	m_virtualPath->setText(source);
}
} // namespace vibestudio
