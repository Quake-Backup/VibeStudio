#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "core/level_object_manipulator.h"
#include "core/level_scene_locks.h"
#include <QStatusBar>
#include <algorithm>
#include <utility>

namespace vibestudio {

void ApplicationShell::connectLevelCameraResize()
{
	struct Source {
		quint64 revision = 0, loadSerial = 0;
		QVector<LevelMapSelectionRef> selection;
	};
	const auto source = std::make_shared<Source>();
	connect(m_levelMap3D,&ModelViewport::selectionResizeActiveChanged,this,[this,source](bool active) {
		if (!active) { return; }
		source->revision = m_levelMapDocument.revision; source->loadSerial = m_levelMapLoadSerial;
		source->selection = m_levelMapDocument.selection;
		// The Shear Tool previews the selection's wire, measured once as it begins.
		if (m_levelMap3D->selectionShearMode()) {
			m_levelMap3D->setSelectionShearPreviewEdges(levelObjectManipulatorEdges(m_levelMapDocument,m_levelMapDocument.selection));
		}
	});
	// TrenchBroom's Shear Tool in the camera: the side slid, the opposite one kept.
	connect(m_levelMap3D,&ModelViewport::selectionShearRequested,this,[this,source](int axis,int side,double dx,double dy,double dz) {
		if (source->revision != m_levelMapDocument.revision || source->loadSerial != m_levelMapLoadSerial
			|| source->selection != m_levelMapDocument.selection) {
			statusBar()->showMessage(tr("The map or selection changed. The shear was cancelled."));
			refreshLevelCameraResize(); return;
		}
		shearLevelObjectsFromView(axis,side,{dx,dy,dz,true},{},nullptr);
	});
	connect(m_levelMap3D,&ModelViewport::selectionResizeRequested,this,[this,source](const ResizeBox& box) {
		if (source->revision != m_levelMapDocument.revision || source->loadSerial != m_levelMapLoadSerial
			|| source->selection != m_levelMapDocument.selection) {
			statusBar()->showMessage(tr("The map or selection changed. The resize was cancelled."));
			refreshLevelCameraResize(); return;
		}
		resizeLevelMapSelectionFromViewport({box.mins[0],box.mins[1],box.mins[2],true},
			{box.maxs[0],box.maxs[1],box.maxs[2],true});
	});
}

void ApplicationShell::refreshLevelCameraResize()
{
	if (!m_levelMap3D) { return; }
	LevelMapVec3 low,high;
	// The Translate, Rotate and UV tools replace the resize box, as NetRadiant
	// Custom's do; the Scale tool adds TrenchBroom's edge and corner handles.
	const bool scaleTool = m_levelObjectTool == static_cast<int>(LevelObjectTool::Scale);
	m_levelMap3D->setSelectionResizeBoxHandles(scaleTool);
	// The Shear Tool turns the faces into TrenchBroom's shear handles.
	const bool quake = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	m_levelMap3D->setSelectionShearMode(quake && m_levelMapViewport && m_levelMapViewport->shearMode());
	if (!levelMap3DShowing() || !m_levelMap3D->isEnabled() || !m_levelMap3D->hasMesh() || m_levelMap3D->componentMode() || m_levelMap3D->clipMode()
		|| (levelObjectToolActive() && !scaleTool) || m_levelUvToolOn || levelBuildToolActive() || !levelMapSelectionResizable() || !levelMapSelectionBounds(m_levelMapDocument,&low,&high)) {
		m_levelMap3D->clearSelectionResizeBox(); return;
	}
	// Doom geometry can share vertices with unselected walls and sectors.
	// Its camera resize needs a topology rebuild, not a selected-triangle
	// affine preview. Keep native plan/numeric editing available meanwhile.
	if (m_levelMapDocument.format == LevelMapFormat::DoomWad
		&& std::any_of(m_levelMapDocument.selection.cbegin(),m_levelMapDocument.selection.cend(),[](const auto& ref) {
			return ref.kind != LevelMapSelectionKind::DoomThing;
		})) { m_levelMap3D->clearSelectionResizeBox(); return; }
	const auto locked = levelSceneLockedObjects(m_levelMapDocument);
	QSet<int> owners,selectedThings;
	for (const auto& selected : std::as_const(m_levelMapDocument.selection)) {
		if (locked.contains(levelMapSelectionRefId(selected))) { m_levelMap3D->clearSelectionResizeBox(); return; }
		if (selected.kind == LevelMapSelectionKind::Entity) { owners.insert(selected.objectId); }
		if (selected.kind == LevelMapSelectionKind::DoomThing) { selectedThings.insert(selected.objectId); }
	}
	for (const auto& brush : std::as_const(m_levelMapDocument.brushes)) {
		if (owners.contains(brush.entityId) && locked.contains(levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush,brush.id}))) {
			m_levelMap3D->clearSelectionResizeBox(); return;
		}
	}
	for (const auto& patch : std::as_const(m_levelMapDocument.patches)) {
		if (owners.contains(patch.entityId) && locked.contains(levelMapSelectionRefId({LevelMapSelectionKind::QuakePatch,patch.id}))) {
			m_levelMap3D->clearSelectionResizeBox(); return;
		}
	}
	// Source origins are indexed once, then matched to the same owner IDs used
	// for camera picking. Entity/thing visual size is independent of map spacing.
	QHash<int,BoxResizePoint> entities,things;
	for (const auto& entity : std::as_const(m_levelMapDocument.entities)) {
		if (owners.contains(entity.id) && entity.origin.valid) { entities.insert(entity.id,{entity.origin.x,entity.origin.y,entity.origin.z}); }
	}
	for (const auto& thing : std::as_const(m_levelMapDocument.doomThings)) {
		if (selectedThings.contains(thing.id)) { things.insert(thing.id,{thing.x,thing.y,thing.z}); }
	}
	QHash<int,BoxResizePoint> origins;
	if (!entities.isEmpty() || !things.isEmpty()) {
		for (int triangle = 0; triangle < m_levelMap3DOwners.size(); ++triangle) {
			const auto owner = m_levelMap3DOwners.at(triangle);
			if (owner.kind == LevelMapSelectionKind::Entity && entities.contains(owner.objectId)) { origins.insert(triangle,entities.value(owner.objectId)); }
			else if (owner.kind == LevelMapSelectionKind::DoomThing && things.contains(owner.objectId)) { origins.insert(triangle,things.value(owner.objectId)); }
		}
	}
	m_levelMap3D->setSelectionResizeBox({{low.x,low.y,low.z},{high.x,high.y,high.z}},origins,
		m_levelMapDocument.format == LevelMapFormat::DoomWad ? 3 : 7);
}

} // namespace vibestudio
