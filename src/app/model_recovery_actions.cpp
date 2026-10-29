// Model recovery in the studio shell: the startup notice that says mesh and
// assembly recovery copies are waiting (and whether their sources changed
// since), the setting that turns it off, and Review Models, which opens the
// Mesh Editor's recovery chooser. Mirrors the audio recovery discovery.

#include "app/application_shell.h"

#include "app/model_assembly_dialog.h"
#include "app/model_editor_dialog.h"
#include "app/model_recovery_discovery.h"
#include "app/studio_layout.h"
#include "core/model_assembly_recovery.h"
#include "core/model_recovery.h"

#include <QCheckBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>

#include <utility>

namespace vibestudio
{
QWidget *ApplicationShell::buildModelRecoveryPreferences()
{
	auto *panel = new QGroupBox(tr("Model Recovery"));
	panel->setObjectName(QStringLiteral("modelRecoveryPreferences"));
	panel->setAccessibleName(tr("Model recovery preferences"));
	auto *layout = new QFormLayout(panel);
	layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_modelRecoveryNotify = new QCheckBox(tr("Offer mesh and assembly copies at startup"));
	m_modelRecoveryNotify->setObjectName(QStringLiteral("setupModelRecoveryNotify"));
	m_modelRecoveryNotify->setAccessibleName(tr("Offer model recovery copies at startup"));
	m_modelRecoveryNotify->setToolTip(tr("Check local mesh and assembly recovery copies in the background at startup and say which "
										 "sources changed since. The oldest mesh copies beyond 32, or beyond 2 GiB, are removed. "
										 "Nothing opens automatically."));
	m_modelRecoveryNotify->setAccessibleDescription(m_modelRecoveryNotify->toolTip());
	m_modelRecoveryNotify->setChecked(m_settings.modelRecoveryNotifyAtStartup());
	layout->addRow(m_modelRecoveryNotify);
	auto *folder = new QLineEdit(QDir::toNativeSeparators(modelRecoveryDirectory()));
	folder->setReadOnly(true);
	folder->setAccessibleName(tr("Local mesh recovery folder"));
	folder->setToolTip(folder->text());
	folder->setCursorPosition(0);
	layout->addRow(tr("Folder"), folder);
	auto *review = new QPushButton(tr("Review Copies"));
	review->setObjectName(QStringLiteral("setupReviewModelRecovery"));
	review->setAccessibleName(tr("Review mesh recovery copies"));
	review->setToolTip(tr("Verify local mesh copies, see whether each source changed since, restore an unsaved draft, or discard "
						  "a reviewed copy."));
	layout->addRow(review);
	connect(review, &QPushButton::clicked, this, &ApplicationShell::reviewModelRecoveries);
	connect(m_modelRecoveryNotify, &QCheckBox::toggled, this,
			[this](bool enabled)
			{
				m_settings.setModelRecoveryNotifyAtStartup(enabled);
				m_settings.sync();
				if (!enabled)
				{
					m_modelRecoveryNotice.clear();
					if (m_modelRecoveryDiscovery)
						m_modelRecoveryDiscovery->cancel();
					if (m_modelRecoveryNoticeVisible)
						m_noticeBar->dismiss();
				}
			});
	return panel;
}

void ApplicationShell::discoverModelRecoveryAtStartup()
{
	if (m_modelRecoveryDiscoveryStarted)
		return;
	m_modelRecoveryDiscoveryStarted = true;
	if (!m_settings.modelRecoveryNotifyAtStartup())
		return;
	m_modelRecoveryDiscovery = new ModelRecoveryDiscovery(this);
	m_modelRecoveryDiscovery->setObjectName(QStringLiteral("modelRecoveryDiscovery"));
	connect(m_noticeBar, &NoticeBar::dismissed, this,
			[this]()
			{
				m_modelRecoveryNoticeVisible = false;
				// Another notice may take the bar in the same pass; offer ours after it.
				QTimer::singleShot(0, this, &ApplicationShell::offerModelRecoveryNotice);
			});
	m_modelRecoveryDiscovery->finished = [this](const ModelRecoveryInventory &inventory)
	{
		if (inventory.cancelled || !m_settings.modelRecoveryNotifyAtStartup())
			return;
		const int copies = inventory.meshCopies + inventory.assemblyCopies;
		if (!inventory.error.isEmpty())
		{
			m_modelRecoveryNotice = tr("Model recovery discovery needs attention. Open Review Models to inspect the folder.");
		}
		else if (copies > 0 || inventory.limited)
		{
			m_modelRecoveryNotice = tr("%n model recovery copy(s) found. Review them before restoring.", nullptr, copies);
			if (inventory.changedSources > 0)
			{
				const int changed = inventory.changedSources;
				m_modelRecoveryNotice += QLatin1Char(' ') + tr("%n source(s) changed or went missing since.", nullptr, changed);
			}
			if (inventory.limited)
				m_modelRecoveryNotice += QLatin1Char(' ') + tr("The folder scan reached its limit.");
		}
		else
		{
			return;
		}
		recordActivity(tr("Model recovery available"), modelRecoveryDirectory(), QStringLiteral("model"), OperationState::Warning,
					   inventory.error.isEmpty() ? m_modelRecoveryNotice : inventory.error);
		m_modelRecoveryAssemblyCopies = inventory.assemblyCopies;
		m_modelRecoveryMeshCopies = inventory.meshCopies;
		offerModelRecoveryNotice();
	};
	m_modelRecoveryDiscovery->start(modelRecoveryDirectory(), modelAssemblyRecoveryDirectory());
}

void ApplicationShell::offerModelRecoveryNotice()
{
	if (m_modelRecoveryNotice.isEmpty() || !m_settings.modelRecoveryNotifyAtStartup() || !m_noticeBar->isHidden())
		return;
	const QString text = std::exchange(m_modelRecoveryNotice, {});
	m_noticeBar->showNotice(QStringLiteral("warning"), tr("Local model recovery"), text);
	m_modelRecoveryNoticeVisible = true;
	if (m_modelRecoveryMeshCopies > 0 || m_modelRecoveryAssemblyCopies == 0)
	{
		auto *review = m_noticeBar->addAction(tr("Review Models"), QStringLiteral("history"), true);
		review->setObjectName(QStringLiteral("noticeReviewModels"));
		review->setAccessibleName(tr("Review mesh recovery copies"));
		connect(review, &QPushButton::clicked, this,
				[this]()
				{
					m_noticeBar->dismiss();
					reviewModelRecoveries();
				});
	}
	if (m_modelRecoveryAssemblyCopies > 0)
	{
		auto *assemblies = m_noticeBar->addAction(tr("Review Assemblies"), QStringLiteral("history"), m_modelRecoveryMeshCopies == 0);
		assemblies->setObjectName(QStringLiteral("noticeReviewAssemblies"));
		assemblies->setAccessibleName(tr("Review assembly recovery copies"));
		connect(assemblies, &QPushButton::clicked, this,
				[this]()
				{
					m_noticeBar->dismiss();
					showModelAssembly();
					if (m_modelAssemblyDialog)
						m_modelAssemblyDialog->reviewRecoveryCopies();
				});
	}
}

void ApplicationShell::reviewModelRecoveries()
{
	if (!m_modelEditorDialog)
	{
		// An empty editor: the chooser replaces its document with the draft.
		m_modelEditorBlank = true;
		showModelEditor();
		m_modelEditorBlank = false;
	}
	else
	{
		showStudioEditor(StudioMode::Models, m_modelEditorDialog);
	}
	if (m_modelEditorDialog)
		m_modelEditorDialog->reviewRecoveryCopies();
}
} // namespace vibestudio
