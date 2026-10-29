// The plan views' object manipulator: NetRadiant Custom's Translate, Rotate
// and Scale tools on whole brushes, patches and entities
// (core/level_object_manipulator.h).
// The shared controller (app/level_component_gizmo.h) owns the drag; this
// file routes the pointer and keys to it and draws the moved selection (the
// controller draws a turned one itself).
//
// Workflow reference: NetRadiant Custom radiant/tools.cpp MouseTranslate and
// MouseRotate, and radiant/selection_mtor_translate.cpp and
// selection_mtor_rotate.cpp at 449778b3 (GPL-2.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.
#include "app/map_viewport_p.h"
#include "app/map_viewport_projection.h"
#include "core/level_component_drag.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <array>

namespace vibestudio {

bool MapViewport::objectGizmoShown() const
{
	return m_hasDocument && m_componentMode == ComponentMode::Off && !m_clipMode && !m_assembleTool && !m_drawMode && !m_shapeDrawMode
		&& !m_shearMode && !m_makeSectorMode && !m_regionDrawMode && m_objectGizmo->objectToolOn() && m_objectGizmo->available();
}

bool MapViewport::beginObjectTransform(const QPointF& point)
{
	if (!isEnabled() || !objectGizmoShown() || m_dragging || m_resizing || m_banding || m_shearing || m_panning) { return false; }
	// The Scale tool keeps the box's resize handles; aimed at, they win.
	if (m_objectGizmo->tool() == LevelComponentTool::Scale && resizeHandleAt(point) != 0) { return false; }
	return m_objectGizmo->begin(point, componentGizmoFrame(), font());
}

bool MapViewport::objectGizmoMousePress(QMouseEvent* event)
{
	// Another button during the drag calls it off, as Escape does.
	if (m_objectGizmo->active()) {
		m_objectGizmo->finish(false);
		unsetCursor();
		m_swallowContextMenu = event->button() == Qt::RightButton;
		event->accept();
		return true;
	}
	const auto keys = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
	if (event->button() != Qt::LeftButton || keys != Qt::NoModifier || !beginObjectTransform(event->position())) { return false; }
	setCursor(Qt::SizeAllCursor);
	event->accept();
	return true;
}

bool MapViewport::objectGizmoMouseMove(QMouseEvent* event)
{
	if (!m_objectGizmo->active()) {
		// Over a handle the status line explains the tool instead of the map.
		if (event->buttons() == Qt::NoButton && objectGizmoShown() && m_objectGizmo->hitTest(event->position(), componentGizmoFrame(), font()) >= 0
			&& !(m_objectGizmo->tool() == LevelComponentTool::Scale && resizeHandleAt(event->position()) != 0)) {
			Q_EMIT hoverChanged(m_objectGizmo->summary());
			event->accept();
			return true;
		}
		return false;
	}
	if (!m_objectGizmo->releasePending() && !event->buttons().testFlag(Qt::LeftButton)) {
		m_objectGizmo->finish(false);
	} else if (!m_objectGizmo->releasePending()) {
		m_objectGizmo->updateDrag(event->position(), event->modifiers());
	}
	event->accept();
	return true;
}

bool MapViewport::objectGizmoMouseRelease(QMouseEvent* event)
{
	if (!m_objectGizmo->active() || event->button() != Qt::LeftButton) { return false; }
	m_objectGizmo->updateDrag(event->position(), event->modifiers());
	m_objectGizmo->finish(true);
	unsetCursor();
	event->accept();
	return true;
}

bool MapViewport::objectGizmoKey(QKeyEvent* event, bool pressed)
{
	// Every key belongs to the drag while it lasts: Escape calls it off, and
	// Shift and Ctrl change its snapping at once.
	if (!m_objectGizmo->active()) { return false; }
	if (pressed && event->key() == Qt::Key_Escape) {
		m_objectGizmo->finish(false);
		unsetCursor();
	} else if (levelComponentModifierKey(event->key())) {
		m_objectGizmo->updateModifiers(levelComponentModifierState(event->key(), event->modifiers(), pressed));
	}
	event->accept();
	return true;
}

void MapViewport::paintObjectGizmo(QPainter& painter, const Palette& palette) const
{
	if (!objectGizmoShown()) { return; }
	const LevelMapVec3 delta = m_objectGizmo->translation();
	if (m_objectGizmo->active() && (delta.x != 0 || delta.y != 0 || delta.z != 0)) {
		painter.save();
		const QPointF plane = mapViewportProjectPoint(m_projection, delta);
		const QPointF shift(plane.x() * m_zoom, -plane.y() * m_zoom);
		// The selection's own wires, where the move would take them.
		const MapPlanWireFrame& frame = m_selectionWireFrame;
		if (!frame.image.isNull() && frame.zoom > 0 && frame.pixelRatio > 0) {
			const double scale = m_zoom / frame.zoom;
			const QSizeF target(frame.viewport.width() * scale, frame.viewport.height() * scale);
			const QPointF offset(width() * 0.5 - target.width() * 0.5 + (frame.center.x() - m_worldCenter.x()) * m_zoom,
				height() * 0.5 - target.height() * 0.5 - (frame.center.y() - m_worldCenter.y()) * m_zoom);
			painter.setOpacity(0.6);
			painter.drawImage(QRectF(offset + shift - frame.pixelPhase * (scale / frame.pixelRatio), frame.image.deviceIndependentSize() * scale), frame.image);
			painter.setOpacity(1.0);
		}
		// And its box, dashed, so the landing place shows at any zoom.
		LevelMapVec3 mins, maxs;
		if (selectionTransformBounds(&mins, &maxs)) {
			QPointF low, high;
			for (int corner = 0; corner < 8; ++corner) {
				const LevelMapVec3 point {((corner & 1) ? maxs.x : mins.x) + delta.x, ((corner & 2) ? maxs.y : mins.y) + delta.y,
					((corner & 4) ? maxs.z : mins.z) + delta.z, true};
				const QPointF projected = mapViewportProjectPoint(m_projection, point);
				const QPointF view = worldToView(projected.x(), projected.y());
				low = corner == 0 ? view : QPointF(std::min(low.x(), view.x()), std::min(low.y(), view.y()));
				high = corner == 0 ? view : QPointF(std::max(high.x(), view.x()), std::max(high.y(), view.y()));
			}
			const QRectF box(low, high);
			QPen ghost(palette.hover, m_highContrast ? 2.0 : 1.4, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
			ghost.setDashPattern({4.0, 3.0});
			painter.setPen(ghost);
			painter.setBrush(Qt::NoBrush);
			painter.drawRect(box.normalized());
		}
		painter.restore();
	}
	m_objectGizmo->paint(painter, componentGizmoFrame(), m_highContrast);
}

} // namespace vibestudio
