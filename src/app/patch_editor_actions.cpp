#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/patch_editor_dialog.h"
#include "core/level_shapes.h"

#include <QComboBox>
#include <QStatusBar>

namespace vibestudio {
bool ApplicationShell::applyLevelPatch(const LevelMapPatch& patch, int patchId, QString* error)
{
	const auto previousRevision = m_levelMapDocument.revision;
	int created = -1;
	const bool succeeded = patchId < 0 ? addLevelMapPatch(&m_levelMapDocument, patch, &created, error)
	                                   : replaceLevelMapPatch(&m_levelMapDocument, patchId, patch, error);
	if (!succeeded) {
		return false;
	}
	if (previousRevision == m_levelMapDocument.revision) {
		return true;
	}
	recordActivity(patchId < 0 ? tr("Level patch added") : tr("Level patch edited"),
	               QStringLiteral("patch:%1").arg(patchId < 0 ? created : patchId), QStringLiteral("level-map"), OperationState::Warning,
	               tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Patch updated. Save the map to include it in the next build."));
	return true;
}

void ApplicationShell::editLevelMapPatchFromUi(bool creating)
{
	if (m_levelMapDocument.format != LevelMapFormat::Quake3Map) {
		statusBar()->showMessage(tr("Open a Quake III-family map to author patches."));
		return;
	}
	LevelMapPatch patch;
	int patchId = -1;
	QString error;
	PatchEditorCreateOptions options;
	if (creating) {
		// NetRadiant Custom's Simple Patch Mesh: a plane across the top of the
		// selection's bounds (or the work zone), square to the active view's
		// axis, in the Textures material with Cap Texture mapping.
		LevelShapeRequest request;
		request.shape = QStringLiteral("patch-plane");
		levelPatchCreationBox(&request.mins, &request.maxs);
		request.axis = levelShapeViewAxis();
		request.texture = levelBrushMaterial();
		request.patchMapping = LevelPatchCreateMapping::Radiant;
		request.patchImageSize = levelPatchImageSize(request.texture);
		LevelPatchCreateRequest create;
		if (!levelShapePatchRequest(request, &create, &error) || !createLevelPatch(create, &patch, &error)) {
			statusBar()->showMessage(error);
			return;
		}
		options.mins = request.mins;
		options.maxs = request.maxs;
		options.axis = request.axis;
		for (const LevelMapSelectionRef& ref : std::as_const(m_levelMapDocument.selection)) {
			options.canReplace |= ref.kind == LevelMapSelectionKind::QuakeBrush || ref.kind == LevelMapSelectionKind::QuakePatch;
		}
		options.imageSize = [this](const QString& material) { return levelPatchImageSize(material); };
	} else {
		if (m_levelMapDocument.selectionKind != LevelMapSelectionKind::QuakePatch) {
			statusBar()->showMessage(tr("Select a patch to edit its control points."));
			return;
		}
		for (const auto& candidate : m_levelMapDocument.patches) {
			if (candidate.id == m_levelMapDocument.selectedObjectId) {
				patch = candidate;
				patchId = candidate.id;
				break;
			}
		}
		if (patchId < 0 || !validateLevelPatch(patch, &error)) {
			statusBar()->showMessage(error);
			return;
		}
		LevelMapDocument probe = m_levelMapDocument;
		if (!replaceLevelMapPatch(&probe, patchId, patch, &error)) {
			statusBar()->showMessage(error);
			return;
		}
	}
	PatchEditorDialog dialog(patch, creating, m_levelMapViewport ? m_levelMapViewport->gridSize() : 8, this, options);
	if (auto* textures = dialog.findChild<QComboBox*>(QStringLiteral("patchTexture"))) {
		for (const auto& use : levelMapTextureUsage(m_levelMapDocument)) {
			if (textures->findText(use.name) < 0) {
				textures->addItem(use.name, use.name);
			}
		}
	}
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	if (!creating || !dialog.replaceSelection()) {
		if (!applyLevelPatch(dialog.patch(), patchId, &error)) {
			statusBar()->showMessage(error);
		}
		return;
	}
	// The patch takes the selected brushes' and patches' place in one step.
	int created = -1;
	if (!replaceLevelMapSelectionWithPatch(&m_levelMapDocument, dialog.patch(), &created, &error)) {
		statusBar()->showMessage(tr("Could not replace the selection with the patch: %1").arg(error), 6000);
		return;
	}
	rememberLevelMaterial(dialog.patch().textureName);
	recordActivity(tr("Level patch added"), QStringLiteral("patch:%1").arg(created), QStringLiteral("level-map"), OperationState::Warning,
		tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Patch %1 replaced the selection. Save the map to include it in the next build.").arg(created), 6000);
}
} // namespace vibestudio
