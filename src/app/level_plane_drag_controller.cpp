#include "app/level_plane_drag_controller.h"

#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <utility>

// Workflow reference: NetRadiant Custom radiant/selection_mtor_drag.cpp
// DragManipulator (Construct, Transform and highlight) and radiant/tools.cpp
// MouseBuild at 449778b3 (GPL-2.0-or-later). Original Qt implementation; see
// docs/CREDITS.md.
namespace vibestudio {
namespace {
LevelMapVec3 mapVector(const ModelVec3& v) { return {v.x, v.y, v.z, true}; }
ModelVec3 modelVector(const LevelMapVec3& v) { return {float(v.x), float(v.y), float(v.z)}; }
bool finitePoint(const QPointF& p) { return std::isfinite(p.x()) && std::isfinite(p.y()); }
bool nonZero(const LevelMapVec3& v) { return v.x != 0 || v.y != 0 || v.z != 0; }
void addBoxEdges(const LevelMapVec3& low, const LevelMapVec3& high, QVector<QPair<LevelMapVec3, LevelMapVec3>>* edges)
{
	const auto corner = [&](int i) {
		return LevelMapVec3 {(i & 1) ? high.x : low.x, (i & 2) ? high.y : low.y, (i & 4) ? high.z : low.z, true};
	};
	for (int i = 0; i < 8; ++i) {
		for (const int bit : {1, 2, 4}) {
			if ((i & bit) == 0) { edges->append({corner(i), corner(i | bit)}); }
		}
	}
}
int componentCount(const LevelPlaneDragPick& pick)
{
	int count = 0;
	for (const auto& set : pick.sets) { count += static_cast<int>(set.components.size()); }
	return count;
}
} // namespace

void LevelPlaneDragController::setContext(std::function<QString()> availability, double grid, bool textureLock)
{
	m_availability = std::move(availability);
	m_textureLock = textureLock;
	const double next = std::isfinite(grid) && grid > 0 ? grid : 0;
	if (next != m_grid && m_active) { finish(false); }
	m_grid = next;
}

bool LevelPlaneDragController::begin(LevelPlaneDragKind kind, const QPointF& point, const LevelComponentGizmoFrame& frame,
	const LevelMapDocument& document, const QVector<LevelMapSelectionRef>& selection, double tolerancePixels)
{
	finish(false);
	m_error.clear();
	if (const QString reason = m_availability ? m_availability() : tr("Open a Quake-family map to drag brush planes."); !reason.isEmpty()) {
		m_error = reason;
		return false;
	}
	if (!frame.ray || !frame.project) { return false; }
	const ModelPickRay ray = frame.ray(point);
	LevelPlaneDragProbe probe;
	probe.origin = mapVector(ray.origin);
	probe.direction = mapVector(ray.direction);
	probe.forward = frame.perspective ? mapVector(frame.viewDirection) : probe.direction;
	probe.camera = frame.perspective;
	// How many map units the pointer's grab reaches across the view.
	const LevelMapVec3 view = mapVector(frame.viewDirection);
	LevelMapVec3 across = std::abs(view.z) < 0.9 ? LevelMapVec3 {-view.y, view.x, 0, true} : LevelMapVec3 {1, 0, 0, true};
	const double length = std::hypot(across.x, across.y, across.z);
	if (length > 0) { across = {across.x / length, across.y / length, across.z / length, true}; }
	const QPointF a = frame.project(probe.origin);
	const QPointF b = frame.project({probe.origin.x + across.x, probe.origin.y + across.y, probe.origin.z + across.z, true});
	const double pixelsPerUnit = finitePoint(a) && finitePoint(b) ? std::hypot(b.x() - a.x(), b.y() - a.y()) : 0;
	probe.tolerance = pixelsPerUnit > 1e-9 ? tolerancePixels / pixelsPerUnit : 0;
	LevelPlaneDragPick pick = pickLevelPlaneDrag(document, selection, kind, probe);
	if (!pick.valid()) {
		m_error = pick.reason;
		return false;
	}
	const bool axisDrag = kind == LevelPlaneDragKind::Plane || kind == LevelPlaneDragKind::Extrude;
	ModelMoveDrag drag;
	const bool started = axisDrag ? beginModelAxisMoveDrag(modelVector(pick.anchor), frame.viewDirection, modelVector(pick.normal), ray, &drag)
								  : beginModelMoveDrag(modelVector(pick.anchor), frame.viewDirection, ModelMoveConstraint::ViewPlane, ray, m_grid, &drag);
	if (!started) {
		m_error = axisDrag ? tr("This view looks straight along the plane under the pointer. Drag it from another view.")
						   : tr("The pointer cannot drag across this view here.");
		return false;
	}
	m_pick = std::move(pick);
	m_frame = frame;
	m_drag = drag;
	m_press = point;
	m_axisDrag = axisDrag;
	m_chain = false;
	m_translation = {0, 0, 0, true};
	m_distance = 0;
	m_picked.clear();
	if (kind != LevelPlaneDragKind::Vertices) {
		for (const auto& set : std::as_const(m_pick.sets)) {
			for (const auto& brush : std::as_const(m_pick.brushes)) {
				if (brush.id != set.object.objectId) { continue; }
				LevelBrushTopology topology;
				if (!levelBrushTopology(brush, &topology)) { break; }
				for (const int face : set.components) {
					if (face < 0 || face >= topology.geometry.faces.size()) { continue; }
					const auto& points = topology.geometry.faces[face].points;
					for (qsizetype i = 0; i < points.size(); ++i) { m_picked.append({points[i], points[(i + 1) % points.size()]}); }
				}
				break;
			}
		}
	}
	m_active = true;
	refreshPreview();
	Q_EMIT activeChanged(true);
	Q_EMIT changed();
	return true;
}

bool LevelPlaneDragController::beginChain(const QPointF& point, const LevelComponentGizmoFrame& frame, const LevelMapVec3& mins, const LevelMapVec3& maxs)
{
	finish(false);
	m_error.clear();
	if (const QString reason = m_availability ? m_availability() : tr("Open a Quake-family map to build a chain of copies."); !reason.isEmpty()) {
		m_error = reason;
		return false;
	}
	if (!frame.ray || !frame.project || !mins.valid || !maxs.valid) { return false; }
	const LevelMapVec3 centre {(mins.x + maxs.x) / 2, (mins.y + maxs.y) / 2, (mins.z + maxs.z) / 2, true};
	ModelMoveDrag drag;
	if (!beginModelMoveDrag(modelVector(centre), frame.viewDirection, ModelMoveConstraint::ViewPlane, frame.ray(point), 0, &drag)) {
		m_error = tr("The pointer cannot drag across this view here.");
		return false;
	}
	m_pick = {};
	m_frame = frame;
	m_drag = drag;
	m_press = point;
	m_axisDrag = false;
	m_chain = true;
	m_mins = mins;
	m_maxs = maxs;
	m_cloneChain = {};
	m_picked.clear();
	addBoxEdges(mins, maxs, &m_picked);
	m_active = true;
	refreshPreview();
	Q_EMIT activeChanged(true);
	Q_EMIT changed();
	return true;
}

bool LevelPlaneDragController::updateDrag(const QPointF& point)
{
	if (!m_active || !m_frame.ray) { return false; }
	const ModelPickRay ray = m_frame.ray(point);
	if (m_chain) {
		ModelVec3 travel;
		if (!modelMoveDragDelta(m_drag, ray, &travel)) { return false; }
		const LevelCloneChain next = levelCloneChain(m_mins, m_maxs, mapVector(travel), m_grid);
		if (next.copies == m_cloneChain.copies && next.axis == m_cloneChain.axis && next.step.x == m_cloneChain.step.x
			&& next.step.y == m_cloneChain.step.y && next.step.z == m_cloneChain.step.z) {
			return true;
		}
		m_cloneChain = next;
	} else if (m_axisDrag) {
		double distance = 0;
		if (!modelAxisMoveDragDistance(m_drag, ray, &distance)) { return false; }
		const double snapped = snapLevelBrushExtrusionDistance(distance, m_pick.normal, m_pick.corners, m_grid);
		if (std::isfinite(snapped)) { distance = snapped; }
		if (std::abs(distance) < 1e-9) { distance = 0; }
		if (distance == m_distance) { return true; }
		m_distance = distance;
		m_translation = {m_pick.normal.x * distance, m_pick.normal.y * distance, m_pick.normal.z * distance, true};
	} else {
		ModelVec3 delta;
		if (!modelMoveDragDelta(m_drag, ray, &delta)) { return false; }
		const LevelMapVec3 translation {delta.x == 0 ? 0.0 : double(delta.x), delta.y == 0 ? 0.0 : double(delta.y), delta.z == 0 ? 0.0 : double(delta.z), true};
		if (translation.x == m_translation.x && translation.y == m_translation.y && translation.z == m_translation.z) { return true; }
		m_translation = translation;
	}
	refreshPreview();
	Q_EMIT changed();
	return true;
}

bool LevelPlaneDragController::moved() const
{
	if (!m_active) { return false; }
	if (m_chain) { return m_cloneChain.copies > 0; }
	return m_pick.kind == LevelPlaneDragKind::Extrude ? m_distance != 0 : nonZero(m_translation);
}

LevelPlaneDragRequest LevelPlaneDragController::request() const
{
	LevelPlaneDragRequest request;
	request.kind = m_pick.kind;
	request.sets = m_pick.sets;
	request.normal = m_pick.normal;
	request.translation = m_translation;
	request.distance = m_distance;
	request.grid = m_grid;
	request.textureLock = m_textureLock;
	return request;
}

void LevelPlaneDragController::refreshPreview()
{
	m_edges.clear();
	m_report = {};
	if (!m_active) { return; }
	if (m_chain) {
		for (int copy = 1; copy <= m_cloneChain.copies; ++copy) {
			const LevelMapVec3 offset {m_cloneChain.step.x * copy, m_cloneChain.step.y * copy, m_cloneChain.step.z * copy, true};
			addBoxEdges({m_mins.x + offset.x, m_mins.y + offset.y, m_mins.z + offset.z, true},
				{m_maxs.x + offset.x, m_maxs.y + offset.y, m_maxs.z + offset.z, true}, &m_edges);
		}
		m_error.clear();
		return;
	}
	if (!moved()) {
		m_error.clear();
		return;
	}
	QString error;
	if (!previewLevelPlaneDrag(m_pick.brushes, request(), &m_edges, &m_report, &error)) {
		m_edges.clear();
		m_error = error;
		return;
	}
	m_error.clear();
}

void LevelPlaneDragController::reset()
{
	m_active = false;
	m_chain = false;
	m_axisDrag = false;
	m_pick = {};
	m_translation = {0, 0, 0, true};
	m_distance = 0;
	m_cloneChain = {};
	m_picked.clear();
	m_edges.clear();
	m_report = {};
}

void LevelPlaneDragController::finish(bool commit)
{
	if (!m_active) { return; }
	const bool chain = m_chain;
	const bool valid = commit && moved() && m_error.isEmpty();
	const LevelPlaneDragRequest made = request();
	const LevelCloneChain cloneChain = m_cloneChain;
	reset();
	Q_EMIT activeChanged(false);
	Q_EMIT changed();
	if (!valid) { return; }
	if (chain) {
		Q_EMIT chainRequested(cloneChain.step, cloneChain.copies);
	} else {
		Q_EMIT commitRequested(made);
	}
}

QString LevelPlaneDragController::summary() const
{
	if (!m_active) { return m_error; }
	QString text;
	if (m_chain) {
		const double step = std::hypot(m_cloneChain.step.x, m_cloneChain.step.y, m_cloneChain.step.z);
		text = m_cloneChain.copies > 0
			? tr("Building a chain of %n copy(ies), each %1 units along %2 from the last. Release to add them; Escape cancels.", nullptr, m_cloneChain.copies)
				  .arg(step, 0, 'g', 8)
				  .arg(QLatin1Char("XYZ"[std::clamp(m_cloneChain.axis, 0, 2)]))
			: tr("Drag along an axis to lay copies of the selection end to end. Escape cancels.");
		return text;
	}
	const int count = componentCount(m_pick);
	switch (m_pick.kind) {
	case LevelPlaneDragKind::FacingPlanes:
		text = tr("Dragging %n brush plane(s) by %1, %2, %3. Escape cancels.", nullptr, count)
				   .arg(m_translation.x, 0, 'g', 8).arg(m_translation.y, 0, 'g', 8).arg(m_translation.z, 0, 'g', 8);
		break;
	case LevelPlaneDragKind::Plane:
		text = tr("Dragging %n brush plane(s) %1 units along the picked plane's normal. Escape cancels.", nullptr, count).arg(m_distance, 0, 'g', 8);
		break;
	case LevelPlaneDragKind::Vertices:
		text = tr("Dragging %n vertex(es) by %1, %2, %3. Escape cancels.", nullptr, count)
				   .arg(m_translation.x, 0, 'g', 8).arg(m_translation.y, 0, 'g', 8).arg(m_translation.z, 0, 'g', 8);
		break;
	case LevelPlaneDragKind::Extrude:
		text = m_distance >= 0
			? tr("Extruding %n face(s) %1 units out into new brushes. Escape cancels.", nullptr, count).arg(m_distance, 0, 'g', 8)
			: tr("Pushing %n face(s) %1 units in to split slabs off. Escape cancels.", nullptr, count).arg(-m_distance, 0, 'g', 8);
		break;
	}
	if (m_report.clampedBrushes > 0) {
		text += QLatin1Char(' ') + tr("%n brush(es) stop short so that no face vanishes.", nullptr, m_report.clampedBrushes);
	}
	if (!m_error.isEmpty()) {
		text += QLatin1Char(' ') + tr("Refused: %1").arg(m_error);
	}
	return text;
}

void LevelPlaneDragController::paint(QPainter& painter, const LevelComponentGizmoFrame& frame, bool highContrast) const
{
	if (!m_active || !frame.project) { return; }
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setBrush(Qt::NoBrush);
	const QColor picked = highContrast ? QColor(Qt::yellow) : QColor(255, 170, 40);
	const QColor result = highContrast ? QColor(Qt::cyan) : QColor(120, 235, 210);
	const auto draw = [&](const QVector<QPair<LevelMapVec3, LevelMapVec3>>& edges) {
		for (const auto& edge : edges) {
			const QPointF a = frame.project(edge.first), b = frame.project(edge.second);
			if (finitePoint(a) && finitePoint(b)) { painter.drawLine(a, b); }
		}
	};
	// The picked planes (or the chain's box) solid and heavy, the result
	// dashed, so the two differ by more than colour.
	painter.setPen(QPen(picked, highContrast ? 4.0 : 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	draw(m_picked);
	if (m_pick.kind == LevelPlaneDragKind::Vertices && !m_chain) {
		for (const auto& corner : m_pick.corners) {
			const QPointF p = frame.project(corner);
			if (finitePoint(p)) { painter.drawRect(QRectF(p.x() - 4, p.y() - 4, 8, 8)); }
		}
	}
	QPen dashed(result, highContrast ? 3.0 : 2.0, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
	painter.setPen(dashed);
	draw(m_edges);
	painter.restore();
}

} // namespace vibestudio
