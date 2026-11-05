// The plan views' drag-tool gestures and Build tool: NetRadiant Custom's QE4
// plane drag beside the selection, its Alt vertex drag and Ctrl+Alt extrusion
// in a 2D view, and the Build tool's face drags and copy chains
// (core/level_plane_drag.h). The shared controller
// (app/level_plane_drag_controller.h) picks, previews and asks for the edit;
// this file routes the pointer and keys to it and draws it.
//
// Workflow reference: NetRadiant Custom radiant/selection_mtor_drag.cpp
// DragManipulator::testSelect and radiant/tools.cpp MouseBuild at 449778b3
// (GPL-2.0-or-later). Original Qt implementation; see docs/CREDITS.md.
#include "app/map_viewport_p.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace vibestudio {
namespace {
constexpr double kPlaneDragThreshold = 4.0;
bool quakeFamily(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
}
} // namespace

void MapViewport::setBuildMode(bool enabled)
{
	if (enabled == m_buildMode) { return; }
	m_planeDrag->finish(false);
	m_planeDragClick = false;
	m_buildMode = enabled;
	setAccessibleDescription(accessibleSummary());
	update();
}

bool MapViewport::beginPlaneDrag(LevelPlaneDragKind kind, const QPointF& point)
{
	if (!isEnabled() || !m_hasDocument || !quakeFamily(m_document) || m_componentMode != ComponentMode::Off || m_clipMode || m_assembleTool
		|| m_drawMode || m_shapeDrawMode || m_shearMode || m_makeSectorMode || m_regionDrawMode || m_selection.isEmpty()) {
		return false;
	}
	if (!m_planeDrag->begin(kind, point, componentGizmoFrame(), m_document, m_selection)) {
		if (!m_planeDrag->lastError().isEmpty()) { Q_EMIT hoverChanged(m_planeDrag->lastError()); }
		return false;
	}
	Q_EMIT hoverChanged(m_planeDrag->summary());
	return true;
}

bool MapViewport::beginPendingPlaneDrag(const QPointF& press, const QPointF& current)
{
	if (!m_pendingPlaneDrag) { return false; }
	m_pendingPlaneDrag = false;
	if (!beginPlaneDrag(LevelPlaneDragKind::FacingPlanes, press)) { return false; }
	// The box resize was only the fallback for a selection without planes.
	m_pendingResizeEdges = 0;
	m_planeDragClick = false;
	m_planeDrag->updateDrag(current);
	setCursor(Qt::SizeAllCursor);
	return true;
}

bool MapViewport::planeDragMousePress(QMouseEvent* event)
{
	// Another button during the drag calls it off, as Escape does.
	if (m_planeDrag->active()) {
		m_planeDrag->finish(false);
		m_planeDragClick = false;
		unsetCursor();
		m_swallowContextMenu = event->button() == Qt::RightButton;
		event->accept();
		return true;
	}
	if (event->button() != Qt::LeftButton || m_selection.isEmpty()) { return false; }
	const auto keys = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
	const QPointF point = event->position();
	bool started = false;
	if (m_buildMode && keys == Qt::ShiftModifier) {
		// The Build tool's chain: Shift and a drag that starts on the selection,
		// anywhere inside it as the stack under the pointer has it.
		const QVector<LevelMapSelectionRef> stack = objectsAt(point);
		const bool onSelection = std::any_of(stack.cbegin(), stack.cend(), [this](const LevelMapSelectionRef& ref) { return selectionContains(ref.kind, ref.objectId); });
		LevelMapVec3 mins, maxs;
		if (onSelection && selectionTransformBounds(&mins, &maxs) && quakeFamily(m_document)) {
			started = m_planeDrag->beginChain(point, componentGizmoFrame(), mins, maxs);
			if (!started && !m_planeDrag->lastError().isEmpty()) { Q_EMIT hoverChanged(m_planeDrag->lastError()); }
		}
	} else if (m_controls.extrudeDragModifiers != Qt::NoModifier && keys == m_controls.extrudeDragModifiers) {
		started = beginPlaneDrag(LevelPlaneDragKind::Extrude, point);
	} else if (m_controls.vertexDragModifiers != Qt::NoModifier && keys == m_controls.vertexDragModifiers) {
		started = beginPlaneDrag(LevelPlaneDragKind::Vertices, point);
	} else if (m_buildMode && keys == Qt::NoModifier) {
		// The Build tool's face drag: the plane under the pointer, or behind
		// the edge nearest it, pulled out or pushed in.
		started = beginPlaneDrag(LevelPlaneDragKind::Extrude, point);
	}
	if (!started) { return false; }
	// A Build press that never drags is a click, as the Select tool's.
	m_planeDragClick = m_buildMode;
	m_planeDragKeys = keys;
	m_pressViewPoint = point;
	setCursor(Qt::SizeAllCursor);
	update();
	event->accept();
	return true;
}

bool MapViewport::planeDragMouseMove(QMouseEvent* event)
{
	if (!m_planeDrag->active()) { return false; }
	if (!event->buttons().testFlag(Qt::LeftButton)) {
		m_planeDrag->finish(false);
		m_planeDragClick = false;
		unsetCursor();
		event->accept();
		return true;
	}
	const QPointF travel = event->position() - m_pressViewPoint;
	if (m_planeDragClick && std::hypot(travel.x(), travel.y()) < kPlaneDragThreshold) {
		event->accept();
		return true;
	}
	m_planeDragClick = false;
	m_planeDrag->updateDrag(event->position());
	event->accept();
	return true;
}

bool MapViewport::planeDragMouseRelease(QMouseEvent* event)
{
	if (!m_planeDrag->active() || event->button() != Qt::LeftButton) { return false; }
	unsetCursor();
	if (m_planeDragClick) {
		// A click with the Build tool selects as the Select tool's does.
		m_planeDragClick = false;
		m_planeDrag->finish(false);
		// What a plain click would take: the object whose outline is under the
		// pointer, else the nearest one the pointer is inside.
		MapViewportHit hit = hitTest(event->position());
		if (hit.kind == LevelMapSelectionKind::None) {
			if (const QVector<LevelMapSelectionRef> stack = objectsAt(event->position()); !stack.isEmpty()) {
				hit.kind = stack.first().kind;
				hit.objectId = stack.first().objectId;
			}
		}
		if (m_planeDragKeys == Qt::NoModifier) {
			if (hit.kind != LevelMapSelectionKind::None) {
				replaceSelection(hit.kind, hit.objectId);
			} else {
				m_selection.clear();
				syncPrimaryFromSelection();
			}
		} else if (hit.kind != LevelMapSelectionKind::None) {
			if (modifiersHeld(m_planeDragKeys, m_controls.toggleModifiers)) {
				toggleInSelection(hit.kind, hit.objectId);
			} else {
				addToSelection(hit.kind, hit.objectId);
			}
		}
		announceSelection();
		Q_EMIT hoverChanged(hoverSummary());
		event->accept();
		return true;
	}
	m_planeDrag->updateDrag(event->position());
	m_planeDrag->finish(true);
	Q_EMIT hoverChanged(m_planeDrag->lastError().isEmpty() ? hoverSummary() : m_planeDrag->lastError());
	event->accept();
	return true;
}

void MapViewport::paintPlaneDrag(QPainter& painter) const
{
	if (!m_planeDrag->active()) { return; }
	m_planeDrag->paint(painter, componentGizmoFrame(), m_highContrast);
}

} // namespace vibestudio
