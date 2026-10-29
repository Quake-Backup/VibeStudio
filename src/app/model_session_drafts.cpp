// Staged sessions kept beside the Mesh Editor's recovery copy (ROB-9,
// core/model_session_draft.h): Paint Skin Weights and Animation Curves write
// their staged strokes or curves while they are open, so a crash loses
// neither, and restoring the copy offers that work for review again.

#include "app/live_status_label.h"
#include "app/model_animation_curves_dialog.h"
#include "app/model_document_work.h"
#include "app/model_editor_dialog.h"
#include "app/model_session_draft_writer.h"
#include "app/model_viewport.h"
#include "app/model_weight_paint_dialog.h"
#include "app/status_line_label.h"
#include "core/model_animation_curves.h"
#include "core/model_session_draft.h"

#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPushButton>
#include <QWidget>

#include <memory>
#include <utility>

namespace vibestudio
{

// Paint Skin Weights: strokes recovered after a crash, staged again in order on
// the prepared document, each its own undo step as when it was painted.
void ModelWeightPaintDialog::restageRecovered()
{
	const auto strokes = std::exchange(m_restore, {});
	m_busy = true;
	m_content->setEnabled(false);
	buttons();
	m_status->setText(QCoreApplication::translate("ModelWeightPaintDialog", "Staging the recovered strokes again…"));
	QString error;
	const bool ok = runModelDocumentWork(
		this, QCoreApplication::translate("ModelWeightPaintDialog", "Restage Recovered Strokes"), &m_document,
		[strokes](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
		{
			for (const auto &stroke : strokes)
			{
				ModelEdit operation;
				operation.kind = ModelEditKind::PaintSkinWeights;
				operation.weightStrokes = {stroke};
				const auto before = candidate.revisionFingerprint();
				if (!candidate.edit(operation, failure, control))
					return false;
				if (candidate.revisionFingerprint() == before)
				{
					*failure = QCoreApplication::translate("ModelWeightPaintDialog",
														   "A recovered stroke no longer changes this model, so the strokes were not restored.");
					return false;
				}
			}
			return true;
		},
		&error, false, &m_cancel);
	m_busy = false;
	m_content->setEnabled(true);
	if (ok)
	{
		m_strokes = strokes;
		m_redoStrokes.clear();
		changed(true);
		m_status->setText(QCoreApplication::translate("ModelWeightPaintDialog",
													  "Recovered strokes: %1. Review them, undo any, or apply the batch.")
							  .arg(m_strokes.size()));
		if (stagedChanged)
			stagedChanged();
	}
	else
		m_status->setText(error);
	buttons();
}

bool ModelAnimationCurvesDialog::stagedDraft(int *clipIndex, ModelPoseCurves *group, bool *bake) const
{
	if (m_history.isEmpty() || clip() < 0 || draft() == m_initial)
		return false;
	*clipIndex = clip();
	*group = draft().group;
	*bake = draft().bake;
	return true;
}

bool ModelAnimationCurvesDialog::restoreDraft(int clipIndex, const ModelPoseCurves &group, bool bake, QString *error)
{
	const auto refuse = [&](const QString &text)
	{
		if (error)
			*error = text;
		m_status->setText(text);
		return false;
	};
	const int clipRow = m_clip->findData(clipIndex), jointRow = m_joint->findText(group.joint);
	if (m_busy || clipRow < 0 || jointRow < 0)
		return refuse(QCoreApplication::translate("ModelAnimationCurvesDialog",
												  "The recovered curves name a clip or joint this model does not have."));
	m_clip->setCurrentIndex(clipRow);
	m_joint->setCurrentIndex(jointRow);
	choose();
	QString problem;
	if (!group.channels[0].isEmpty() && !validateModelCurveGroup(group, frameCount(), &problem))
		return refuse(problem);
	stage(Draft{group, bake}, 0);
	m_status->setText(QCoreApplication::translate("ModelAnimationCurvesDialog", "Recovered staged curves. Choose Review before applying."));
	return true;
}

QString ModelEditorDialog::sessionDraftPath() const
{
	if (!m_sessionDraftActive)
		return {};
	for (const auto kind : {ModelSessionDraftKind::WeightPaint, ModelSessionDraftKind::AnimationCurves})
	{
		const auto path = modelSessionDraftPath(m_recoveryDirectory, m_sessionDraftOwner, kind);
		if (QFileInfo::exists(path))
			return path;
	}
	return {};
}

void ModelEditorDialog::beginSessionDraft()
{
	// Nothing is kept when the user turned recovery copies off.
	if (!m_recoveryEnabled->isChecked())
		return;
	if (!m_sessionDrafts)
	{
		m_sessionDrafts = new ModelSessionDraftWriter(m_recoveryDirectory, this);
		m_sessionDrafts->finished = [this](const QString &, const QString &error)
		{
			if (!error.isEmpty())
				m_recoveryStatus->setText(
					QCoreApplication::translate("VibeStudioModelEditor", "Staged work is not being kept: %1").arg(error));
		};
	}
	m_sessionDraftActive = true;
	m_sessionDraftBase = m_document.revisionFingerprint();
	// The copy the staged work belongs to; kept now even for an unmodified model.
	checkpointRecovery();
	m_sessionDraftOwner = m_recoveryId;
}

void ModelEditorDialog::checkpointSessionDraft(ModelSessionDraft draft)
{
	if (!m_sessionDraftActive || !m_sessionDrafts || m_sessionDraftOwner.isEmpty())
		return;
	const bool empty = draft.kind == ModelSessionDraftKind::WeightPaint ? draft.strokes.isEmpty() : draft.clip < 0;
	if (empty)
	{
		const auto kind = draft.kind;
		m_sessionDrafts->remove(m_sessionDraftOwner, nullptr, &kind);
		return;
	}
	draft.ownerId = m_sessionDraftOwner;
	draft.baseFingerprint = m_sessionDraftBase;
	const auto source = m_document.path().isEmpty() ? m_recoverySourcePath : m_document.path();
	draft.title = source.isEmpty() ? QCoreApplication::translate("VibeStudioModelEditor", "Unsaved mesh") : QFileInfo(source).fileName();
	m_sessionDrafts->write(std::move(draft));
}

void ModelEditorDialog::endSessionDraft()
{
	if (!m_sessionDraftActive)
		return;
	m_sessionDraftActive = false;
	QString error;
	if (m_sessionDrafts && !m_sessionDraftOwner.isEmpty() && !m_sessionDrafts->remove(m_sessionDraftOwner, &error))
		m_recoveryStatus->setText(error);
	m_sessionDraftOwner.clear();
	m_sessionDraftBase.clear();
	// An unmodified model needs no copy once the staged work is closed.
	checkpointRecovery();
}

void ModelEditorDialog::watchSessionDraft(ModelWeightPaintDialog &dialog)
{
	beginSessionDraft();
	auto *target = &dialog;
	dialog.stagedChanged = [this, target]
	{
		ModelSessionDraft draft;
		draft.kind = ModelSessionDraftKind::WeightPaint;
		draft.surface = target->surfaceIndex();
		draft.strokes = target->stagedStrokes();
		checkpointSessionDraft(std::move(draft));
	};
}

void ModelEditorDialog::watchSessionDraft(ModelAnimationCurvesDialog &dialog)
{
	beginSessionDraft();
	auto *target = &dialog;
	// The dialog reports every refresh; only a changed staged draft is written.
	auto last = std::make_shared<QByteArray>();
	dialog.draftChanged = [this, target, last]
	{
		ModelSessionDraft draft;
		draft.kind = ModelSessionDraftKind::AnimationCurves;
		if (!target->stagedDraft(&draft.clip, &draft.curves, &draft.bake))
		{
			if (!last->isEmpty())
			{
				last->clear();
				draft.clip = -1;
				checkpointSessionDraft(std::move(draft));
			}
			return;
		}
		const auto key = QJsonDocument(modelPoseCurvesJson(draft.curves)).toJson(QJsonDocument::Compact) + ' ' +
						 QByteArray::number(draft.clip) + (draft.bake ? " bake" : " keys");
		if (key == *last)
			return;
		*last = key;
		const auto &clips = m_document.mesh().skeleton.clips;
		draft.frameCount = draft.clip < clips.size() ? int(clips.at(draft.clip).frames.size()) : 0;
		checkpointSessionDraft(std::move(draft));
	};
}

void ModelEditorDialog::reviewRecoveredSessions(const QString &recoveryId)
{
	for (const auto &header : inspectModelSessionDrafts(m_recoveryDirectory, recoveryId))
	{
		ModelSessionDraft draft;
		QString error = header.error;
		if (!error.isEmpty() || !readModelSessionDraft(header.path, &draft, &error))
		{
			m_status->setText(QCoreApplication::translate("VibeStudioModelEditor", "Staged work kept with this copy could not be read: %1")
								  .arg(error),
							  LiveStatus::Problem);
			continue;
		}
		const auto summary = modelSessionDraftSummary(draft);
		if (draft.baseFingerprint != m_document.revisionFingerprint())
		{
			m_status->setText(QCoreApplication::translate("VibeStudioModelEditor",
														  "Not restored: %1. It was staged on another version of this model.")
								  .arg(summary),
							  LiveStatus::Problem);
			continue;
		}
		int choice = 0;
		if (sessionDraftPrompt)
			choice = sessionDraftPrompt(summary);
		else
		{
			QMessageBox box(QMessageBox::Question, QCoreApplication::translate("VibeStudioModelEditor", "Recovered Staged Work"),
							QCoreApplication::translate("VibeStudioModelEditor",
														"This copy also kept work that was staged but not applied: %1. Review it again now?")
								.arg(summary),
							QMessageBox::NoButton, this);
			box.setObjectName(QStringLiteral("meshRecoveredSessionPrompt"));
			auto *review = box.addButton(QCoreApplication::translate("VibeStudioModelEditor", "Review Staged Work"), QMessageBox::AcceptRole);
			box.addButton(QCoreApplication::translate("VibeStudioModelEditor", "Discard Staged Work"), QMessageBox::DestructiveRole);
			box.setDefaultButton(review);
			box.exec();
			choice = box.clickedButton() == review ? 1 : 0;
		}
		// The review keeps its own sidecar with the editor's current copy.
		const auto kind = draft.kind;
		removeModelSessionDrafts(m_recoveryDirectory, recoveryId, nullptr, &kind);
		if (choice != 1)
		{
			m_status->setText(QCoreApplication::translate("VibeStudioModelEditor", "Discarded: %1.").arg(summary));
			continue;
		}
		if (kind == ModelSessionDraftKind::WeightPaint)
			reviewRecoveredWeights(draft);
		else
			reviewRecoveredCurves(draft);
	}
}

bool ModelEditorDialog::reviewRecoveredWeights(const ModelSessionDraft &draft)
{
	if (m_working || draft.strokes.isEmpty())
		return false;
	m_preview->finishEditTransform(false);
	m_preview->pause();
	const auto revision = m_document.revisionFingerprint();
	const auto selection = m_document.selection();
	ModelSelection painted;
	painted.surface = draft.surface;
	ModelWeightPaintDialog dialog(m_document.mesh(), painted, draft.strokes.first().joint, m_preview->frame(), this);
	if (auto *preview = dialog.findChild<ModelViewport *>(QStringLiteral("weightPaintPreview")))
		preview->setCameraControls(m_preview->cameraControls());
	dialog.restoreStrokes(draft.strokes);
	watchSessionDraft(dialog);
	const bool accepted = dialog.exec() == QDialog::Accepted;
	endSessionDraft();
	if (!accepted)
		return false;
	if (revision != m_document.revisionFingerprint())
	{
		m_status->setText(QCoreApplication::translate("VibeStudioModelEditor",
													  "The model changed while painting weights. Reopen Paint Skin Weights."));
		return false;
	}
	QString error;
	if (!applyEdit(dialog.edit(), &error))
	{
		m_status->setText(error);
		return false;
	}
	m_document.setSelection(selection);
	refresh();
	return true;
}

bool ModelEditorDialog::reviewRecoveredCurves(const ModelSessionDraft &draft)
{
	if (m_working)
		return false;
	m_preview->finishEditTransform(false);
	m_preview->pause();
	const auto revision = m_document.revisionFingerprint();
	const auto selection = m_document.selection();
	ModelAnimationCurvesDialog dialog(m_document.mesh(), draft.curves.joint, m_preview->frame(), this);
	if (auto *preview = dialog.findChild<ModelViewport *>(QStringLiteral("curvesPreview")))
		preview->setCameraControls(m_preview->cameraControls());
	watchSessionDraft(dialog);
	QString error;
	if (!dialog.restoreDraft(draft.clip, draft.curves, draft.bake, &error))
	{
		endSessionDraft();
		m_status->setText(error, LiveStatus::Problem);
		return false;
	}
	const bool accepted = dialog.exec() == QDialog::Accepted;
	endSessionDraft();
	if (!accepted)
		return false;
	if (revision != m_document.revisionFingerprint())
	{
		m_status->setText(QCoreApplication::translate("VibeStudioModelEditor",
													  "The model changed while reviewing animation curves. Reopen Animation Curves."));
		return false;
	}
	if (!applyEdit(dialog.edit(), &error))
	{
		m_status->setText(error);
		return false;
	}
	m_document.setSelection(selection);
	refresh();
	return true;
}

} // namespace vibestudio
