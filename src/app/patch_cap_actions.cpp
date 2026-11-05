// Cap Patch…: VibeStudio's radial caps and NetRadiant Custom's Cap Selection
// seams (Bevel, Endcap, Inverted Bevel, Inverted Endcap and Cylinder) for
// every visible selected patch, as one undo step that selects the caps
// (core/level_patch_cap.h capLevelMapPatches). Workflow reference: NRC
// radiant/patchmanip.cpp Patch_Cap, DoCapDlg and Scene_PatchDoCap_Selected at
// 449778b3 (GPL-2.0-or-later). Original Qt implementation; see
// docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/patch_cap_dialog.h"
#include <QStatusBar>

namespace vibestudio
{
bool ApplicationShell::applyLevelPatchCaps(const QVector<int> &patchIds, const LevelPatchCapRequest &request, QString *error)
{
	LevelPatchCapResult result;
	if (!capLevelMapPatches(&m_levelMapDocument, patchIds, request, &result, error)) {
		return false;
	}
	if (!request.texture.isEmpty()) {
		rememberLevelMaterial(request.texture);
	}
	recordActivity(tr("Patch caps added"), m_levelMapDocument.sourcePath, QStringLiteral("level-map"), OperationState::Warning,
				   tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	const int caps = static_cast<int>(result.caps.size());
	QString message = tr("Added %n cap(s), selected. Save the map to include them in the next build.", nullptr, caps);
	if (!result.skipped.isEmpty()) {
		const int skipped = static_cast<int>(result.skipped.size());
		message += QLatin1Char(' ') + tr("%n patch(es) not capped: %1", nullptr, skipped).arg(result.skipped.join(QStringLiteral("; ")));
	}
	statusBar()->showMessage(message, 8000);
	return true;
}
void ApplicationShell::capLevelMapPatchFromUi()
{
	auto archive =
		m_packageStaging.isLoaded() ? std::make_shared<PackageStagingArchive>(m_packageStaging) : m_packageArchive.snapshotReader();
	if (!archive && m_packageArchive.isOpen()) {
		archive = std::make_shared<const PackageArchive>(m_packageArchive);
	}
	// Material previews read the game's base packages beneath it.
	archive = levelMaterialReader(std::move(archive));
	PatchCapDialog dialog(m_levelMapDocument, archive, activePaletteId(), this);
	dialog.setCurrentMaterial(levelBrushMaterial());
	const auto serial = m_levelMapLoadSerial, revision = m_levelMapDocument.revision, packageRevision = m_packageStaging.revision(),
			   reload = m_levelPreviewReload;
	const auto selection = m_levelMapDocument.selection;
	const auto palette = activePaletteId();
	dialog.setApplyHandler(
		[this, serial, revision, packageRevision, reload, selection, palette](const QVector<int> &ids, const auto &request, QString *error) {
			if (serial != m_levelMapLoadSerial || revision != m_levelMapDocument.revision || selection != m_levelMapDocument.selection ||
				packageRevision != m_packageStaging.revision() || reload != m_levelPreviewReload || palette != activePaletteId()) {
				if (error) {
					*error = tr("The map, selection or package changed during preview. Reopen Cap Patch from the current map.");
				}
				return false;
			}
			return applyLevelPatchCaps(ids, request, error);
		});
	dialog.exec();
}
} // namespace vibestudio
