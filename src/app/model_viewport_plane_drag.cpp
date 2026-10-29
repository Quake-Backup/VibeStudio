// The level camera's drag-tool gestures and Build tool: NetRadiant Custom's
// Alt drag of one brush plane and Ctrl+Alt extrusion in the camera, and the
// Build tool's face drags and copy chains (core/level_plane_drag.h). The
// shared controller (app/level_plane_drag_controller.h) picks, previews and
// asks for the edit; this file routes the pointer to it and draws it.
//
// Workflow reference: NetRadiant Custom radiant/selection_mtor_drag.cpp
// DragManipulator::testSelect (Scene_forEachPlaneSelectable_selectPlanes2 and
// Scene_forEachBrush_setupExtrude) and radiant/tools.cpp MouseBuild at
// 449778b3 (GPL-2.0-or-later). Original Qt implementation; see
// docs/CREDITS.md.
#include "app/model_viewport.h"
#include "core/level_map.h"

#include <QMouseEvent>
#include <QPainter>

#include <cmath>

namespace vibestudio {
namespace {
constexpr double kPlaneDragThreshold = 4.0;
bool quakeFamily(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
}
} // namespace

void ModelViewport::setPlaneDragDocument(const LevelMapDocument* document)
{
	if (document == m_planeDragDocument) { return; }
	m_planeDrag->finish(false);
	m_planeDragDocument = document;
}

void ModelViewport::setBuildMode(bool enabled)
{
	if (enabled == m_buildMode) { return; }
	m_planeDrag->finish(false);
	m_planeDragClick = false;
	m_buildMode = enabled;
	setAccessibleDescription(accessibleSummary());
	update();
}

bool ModelViewport::planeDragMousePress(QMouseEvent* event)
{
	// Another button during the drag calls it off, as Escape does.
	if (m_planeDrag->active()) {
		m_planeDrag->finish(false);
		m_planeDragClick = false;
		event->accept();
		return true;
	}
	if (event->button() != Qt::LeftButton || m_looking || m_componentMode || m_clipMode || m_assembleTool || m_brushDrawTool
		|| m_surfaceTool != ModelViewportSurfaceTool::None || !m_planeDragDocument || !quakeFamily(*m_planeDragDocument)
		|| m_planeDragDocument->selection.isEmpty() || isRendering()) {
		return false;
	}
	const auto keys = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
	const QPointF point = event->position();
	const LevelMapDocument& document = *m_planeDragDocument;
	bool started = false;
	if (m_buildMode && keys == Qt::ShiftModifier) {
		// The Build tool's chain: Shift and a drag that starts on the selection.
		ensureProjection();
		const ModelViewportHit hit = hitTest(point);
		LevelMapVec3 mins, maxs;
		if (hit.valid && hit.triangle >= 0 && hit.triangle < m_highlighted.size() && m_highlighted.at(hit.triangle)
			&& levelMapObjectsBounds(document, document.selection, &mins, &maxs)) {
			started = m_planeDrag->beginChain(point, componentGizmoFrame(), mins, maxs);
		}
	} else if (m_controls.extrudeDragModifiers != Qt::NoModifier && keys == m_controls.extrudeDragModifiers) {
		started = m_planeDrag->begin(LevelPlaneDragKind::Extrude, point, componentGizmoFrame(), document, document.selection);
	} else if (m_controls.planeDragModifiers != Qt::NoModifier && keys == m_controls.planeDragModifiers) {
		started = m_planeDrag->begin(LevelPlaneDragKind::Plane, point, componentGizmoFrame(), document, document.selection);
	} else if (m_buildMode && keys == Qt::NoModifier) {
		started = m_planeDrag->begin(LevelPlaneDragKind::Extrude, point, componentGizmoFrame(), document, document.selection);
	}
	if (!started) {
		if (!m_planeDrag->lastError().isEmpty() && (m_buildMode || keys != Qt::NoModifier)) { Q_EMIT hoverChanged(m_planeDrag->lastError()); }
		return false;
	}
	m_planeDragClick = m_buildMode;
	m_planeDragKeys = keys;
	m_planeDragPress = point;
	m_heldKeys.clear();
	setCursor(Qt::SizeAllCursor);
	Q_EMIT hoverChanged(m_planeDrag->summary());
	update();
	event->accept();
	return true;
}

bool ModelViewport::planeDragMouseMove(QMouseEvent* event)
{
	if (!m_planeDrag->active()) { return false; }
	if (!event->buttons().testFlag(Qt::LeftButton)) {
		m_planeDrag->finish(false);
		m_planeDragClick = false;
		event->accept();
		return true;
	}
	const QPointF travel = event->position() - m_planeDragPress;
	if (m_planeDragClick && std::hypot(travel.x(), travel.y()) < kPlaneDragThreshold) {
		event->accept();
		return true;
	}
	m_planeDragClick = false;
	m_planeDrag->updateDrag(event->position());
	event->accept();
	return true;
}

bool ModelViewport::planeDragMouseRelease(QMouseEvent* event)
{
	if (!m_planeDrag->active() || event->button() != Qt::LeftButton) { return false; }
	if (!m_looking) { unsetCursor(); }
	if (m_planeDragClick) {
		// A click with the Build tool picks as the Select tool's does.
		m_planeDragClick = false;
		m_planeDrag->finish(false);
		pickAt(event->position(), m_planeDragKeys == Qt::NoModifier ? Qt::KeyboardModifiers(Qt::NoModifier) : m_controls.toggleModifiers);
		event->accept();
		return true;
	}
	m_planeDrag->updateDrag(event->position());
	m_planeDrag->finish(true);
	if (!m_planeDrag->lastError().isEmpty()) { Q_EMIT hoverChanged(m_planeDrag->lastError()); }
	event->accept();
	return true;
}

void ModelViewport::paintPlaneDrag(QPainter& painter)
{
	if (!m_planeDrag->active()) { return; }
	m_planeDrag->paint(painter, componentGizmoFrame(m_planeDrag->pick().anchor), m_highContrast);
}

} // namespace vibestudio
