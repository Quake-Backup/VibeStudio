// The Levels camera's object manipulator: NetRadiant Custom's Translate,
// Rotate and Scale tools on whole brushes, patches and entities
// (core/level_object_manipulator.h). The shared controller
// (app/level_component_gizmo.h) owns the drag; the camera previews a move by
// moving the selected triangles, as its own selection drag does, and the
// controller draws a turn as a dashed wire.
//
// Workflow reference: NetRadiant Custom radiant/tools.cpp MouseTranslate and
// MouseRotate, and radiant/selection_mtor_translate.cpp and
// selection_mtor_rotate.cpp at 449778b3 (GPL-2.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.
#include "app/model_viewport.h"

#include <QPainter>
#include <QTimer>

namespace vibestudio {

bool ModelViewport::objectGizmoShown() const
{
	return m_hasMesh && !m_componentMode && !m_clipMode && !m_assembleTool && !m_brushDrawTool && m_surfaceTool == ModelViewportSurfaceTool::None
		&& !m_looking && m_objectGizmo->objectToolOn() && m_objectGizmo->available();
}

bool ModelViewport::beginObjectTransform(const QPointF& point)
{
	if (!isEnabled() || !objectGizmoShown() || m_pressButton != Qt::NoButton || isResizingSelection() || isMovingSelection()) { return false; }
	ensureProjection();
	if (isRendering()) { return false; }
	// The Scale tool keeps the resize box; its handles, aimed at, win.
	if (m_objectGizmo->tool() == LevelComponentTool::Scale && selectionResizeHandleAt(point) >= 0) { return false; }
	if (!m_objectGizmo->begin(point, componentGizmoFrame(m_objectGizmo->pivot()), font())) { return false; }
	m_heldKeys.clear();
	m_flyTimer->stop();
	setCursor(Qt::SizeAllCursor);
	return true;
}

void ModelViewport::syncObjectGizmoPreview()
{
	// The camera's own selection drag owns the offset while it runs.
	if (m_dragStarted && m_dragAction == DragAction::Move) { return; }
	const LevelMapVec3 delta = m_objectGizmo->translation();
	const ModelVec3 offset {float(delta.x), float(delta.y), float(delta.z)};
	if (offset.x == m_moveOffset.x && offset.y == m_moveOffset.y && offset.z == m_moveOffset.z) { return; }
	m_moveOffset = offset;
	invalidateProjection();
}

void ModelViewport::paintObjectGizmo(QPainter& painter)
{
	if (!objectGizmoShown()) { return; }
	m_objectGizmo->paint(painter, componentGizmoFrame(m_objectGizmo->pivot()), m_highContrast);
}

} // namespace vibestudio
