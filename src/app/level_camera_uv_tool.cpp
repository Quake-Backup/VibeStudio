// The Levels camera's UV tool: NetRadiant Custom's UV manipulator (G) for one
// brush face or patch, drawn over the camera and dragged there, with the edits
// computed by core/level_uv_tool.h.
// Workflow reference: NRC radiant/selection_mtor_uv.cpp UVManipulator (render,
// testSelect and Transform) at 449778b3 (GPL-2.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.

#include "app/level_camera_uv_tool.h"

#include "app/model_viewport.h"

#include <QFontMetricsF>
#include <QLineF>
#include <QLocale>
#include <QPainter>
#include <QPalette>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>

namespace vibestudio {
namespace {

ModelVec3 toModel(const LevelMapVec3& v)
{
	return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

LevelMapVec3 fromModel(const ModelVec3& v)
{
	return {static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z), true};
}

LevelMapVec3 plus(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z, true};
}

LevelMapVec3 times(const LevelMapVec3& a, double s)
{
	return {a.x * s, a.y * s, a.z * s, true};
}

LevelMapVec3 crossed(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, true};
}

LevelMapVec3 unit(const LevelMapVec3& a)
{
	const double length = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
	return length > 1e-12 ? times(a, 1.0 / length) : LevelMapVec3 {};
}

bool finitePoint(QPointF p)
{
	return std::isfinite(p.x()) && std::isfinite(p.y());
}

QPointF hiddenPoint()
{
	return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
}

double segmentDistance(QPointF p, QPointF a, QPointF b)
{
	const QPointF ab = b - a;
	const double length = QPointF::dotProduct(ab, ab);
	const double along = length > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / length, 0.0, 1.0) : 0.0;
	return QLineF(p, a + ab * along).length();
}

// Even-odd: inside a polygon of texels.
bool insidePolygon(QPointF p, const QVector<QPointF>& polygon)
{
	bool inside = false;
	for (qsizetype i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
		const QPointF a = polygon[i], b = polygon[j];
		if ((a.y() > p.y()) != (b.y() > p.y()) && p.x() < (b.x() - a.x()) * (p.y() - a.y()) / (b.y() - a.y()) + a.x()) {
			inside = !inside;
		}
	}
	return inside;
}

// A patch's outline: its first row, last column, last row and first column.
QVector<QPointF> patchOutline(const LevelUvLayout& layout, const QVector<QPointF>& uv)
{
	QVector<QPointF> outline;
	const int columns = layout.columns, rows = layout.rows;
	if (columns < 2 || rows < 2 || uv.size() != qsizetype(columns) * rows) {
		return outline;
	}
	for (int c = 0; c < columns; ++c) {
		outline.append(uv[c]);
	}
	for (int r = 1; r < rows; ++r) {
		outline.append(uv[r * columns + columns - 1]);
	}
	for (int c = columns - 2; c >= 0; --c) {
		outline.append(uv[(rows - 1) * columns + c]);
	}
	for (int r = rows - 2; r > 0; --r) {
		outline.append(uv[r * columns]);
	}
	return outline;
}

QString number(double value)
{
	return QLocale().toString(std::abs(value) < 5e-5 ? 0.0 : value, 'f', 2);
}

} // namespace

LevelCameraUvTool::LevelCameraUvTool(ModelViewport* camera)
	: m_camera(camera)
{
}

void LevelCameraUvTool::setSource(const LevelUvLayout& layout)
{
	const bool same = m_hasSource && layout.target == m_layout.target && m_pivot.valid;
	m_layout = layout;
	m_frame = levelUvToolFrame(layout.points, layout.uv);
	m_hover = {};
	if (!m_frame.valid) {
		m_hasSource = false;
		m_reason = tr("The texture runs along this surface, so the UV tool cannot lay its lines over it. Reset its mapping first.");
		return;
	}
	m_hasSource = true;
	m_reason.clear();
	if (same) {
		// The pivot keeps its place on the surface's plane.
		m_pivot = m_frame.worldAt(m_frame.texelAt(m_pivot));
		if (!std::isfinite(m_pivot.x) || !std::isfinite(m_pivot.y) || !std::isfinite(m_pivot.z)) {
			resetPivot();
		}
	} else {
		resetPivot();
	}
	rebuild();
}

void LevelCameraUvTool::clearSource(const QString& reason)
{
	cancelDrag();
	m_hasSource = false;
	m_layout = {};
	m_frame = {};
	m_grid = {};
	m_edges.clear();
	m_hover = {};
	m_pivot = {};
	m_reason = reason;
}

void LevelCameraUvTool::rebuild()
{
	m_grid = levelUvToolGrid(m_layout.uv, levelUvToolRepeat(m_layout), pivotTexel());
	m_edges = levelUvToolEdges(m_layout);
}

QPointF LevelCameraUvTool::pivotTexel() const
{
	return m_hasSource && m_pivot.valid ? m_frame.texelAt(m_pivot) : hiddenPoint();
}

bool LevelCameraUvTool::setPivotTexel(QPointF texel)
{
	if (!m_hasSource || !finitePoint(texel)) {
		return false;
	}
	m_pivot = m_frame.worldAt(texel);
	rebuild();
	return true;
}

void LevelCameraUvTool::resetPivot()
{
	if (!m_frame.valid || m_layout.uv.isEmpty()) {
		m_pivot = {};
		return;
	}
	// The surface's texture-space lower-left corner, where NetRadiant
	// Custom's UV tool and the Face inspector start the pivot.
	double left = std::numeric_limits<double>::infinity(), bottom = -left;
	for (const QPointF& point : std::as_const(m_layout.uv)) {
		left = std::min(left, point.x());
		bottom = std::max(bottom, point.y());
	}
	m_pivot = m_frame.worldAt({left, bottom});
	if (m_hasSource) {
		rebuild();
	}
}

double LevelCameraUvTool::pickRadius() const
{
	const QFontMetricsF metrics(m_camera ? m_camera->font() : QFont());
	return std::max(6.0, metrics.height() * 0.5);
}

bool LevelCameraUvTool::screenPoint(const LevelMapVec3& world, QPointF* screen) const
{
	return m_camera && world.valid && m_camera->projectToView(toModel(world), screen);
}

QPointF LevelCameraUvTool::texelScreen(QPointF texel) const
{
	QPointF screen;
	return m_hasSource && finitePoint(texel) && screenPoint(m_frame.worldAt(texel), &screen) ? screen : hiddenPoint();
}

bool LevelCameraUvTool::texelUnder(QPointF screen, QPointF* texel) const
{
	if (!m_camera || !m_hasSource) {
		return false;
	}
	const ModelPickRay ray = m_camera->viewRay(screen);
	LevelMapVec3 hit;
	if (!m_frame.hit(fromModel(ray.origin), fromModel(ray.direction), &hit, ray.forwardOnly)) {
		return false;
	}
	const QPointF at = m_frame.texelAt(hit);
	if (!finitePoint(at)) {
		return false;
	}
	*texel = at;
	return true;
}

bool LevelCameraUvTool::segmentScreen(const LevelMapVec3& a, const LevelMapVec3& b, QPointF* screenA, QPointF* screenB) const
{
	return m_camera && m_camera->projectSegmentToView(toModel(a), toModel(b), screenA, screenB);
}

double LevelCameraUvTool::ringRadiusWorld(const LevelUvToolFrame& frame, const LevelMapVec3& centre) const
{
	if (!m_camera || !frame.valid) {
		return 0;
	}
	// A ring a few lines of text across, never more than a third of the view.
	const double room = std::max(24.0, std::min(m_camera->width(), m_camera->height()) * 0.3);
	const double wanted = std::min(std::max(56.0, QFontMetricsF(m_camera->font()).height() * 3.5), room);
	const LevelMapVec3 e1 = unit(frame.alongS), e2 = unit(crossed(frame.normal, e1));
	QPointF centreScreen;
	if (!e1.valid || !e2.valid || !screenPoint(centre, &centreScreen)) {
		return 0;
	}
	double pixels = 0;
	for (const LevelMapVec3& e : {e1, e2}) {
		QPointF along;
		if (screenPoint(plus(centre, times(e, 8)), &along)) {
			pixels = std::max(pixels, QLineF(centreScreen, along).length() / 8.0);
		}
	}
	return pixels > 1e-9 ? wanted / pixels : 0;
}

QVector<QPointF> LevelCameraUvTool::ringScreen(const LevelUvToolFrame& frame, const LevelMapVec3& centre) const
{
	QVector<QPointF> ring;
	const double radius = ringRadiusWorld(frame, centre);
	if (!(radius > 0)) {
		return ring;
	}
	const LevelMapVec3 e1 = unit(frame.alongS), e2 = unit(crossed(frame.normal, e1));
	constexpr int segments = 48;
	for (int i = 0; i <= segments; ++i) {
		const double angle = 2 * std::numbers::pi * i / segments;
		QPointF screen;
		ring.append(screenPoint(plus(centre, plus(times(e1, radius * std::cos(angle)), times(e2, radius * std::sin(angle)))), &screen) ? screen
				: hiddenPoint());
	}
	return ring;
}

LevelUvToolSnap LevelCameraUvTool::snapAt(QPointF texel, bool hard) const
{
	LevelUvToolSnap snap;
	snap.hard = hard;
	const double pixels = pickRadius();
	const QPointF at = texelScreen(texel);
	const auto tolerance = [&](QPointF step) {
		const QPointF there = texelScreen(texel + step);
		const double length = std::hypot(step.x(), step.y());
		const double perTexel = finitePoint(at) && finitePoint(there) ? QLineF(at, there).length() / length : 0;
		return perTexel > 1e-9 ? pixels / perTexel : 0.0;
	};
	const double s = std::max(1.0, m_grid.stepS / 8), t = std::max(1.0, m_grid.stepT / 8);
	snap.tolerance = {tolerance({s, 0}), tolerance({0, t})};
	return snap;
}

LevelCameraUvTool::Hit LevelCameraUvTool::hitAt(QPointF screen, Qt::KeyboardModifiers modifiers) const
{
	Hit hit;
	if (!m_hasSource || !m_camera) {
		return hit;
	}
	const double pick = pickRadius();
	const bool alt = modifiers.testFlag(Qt::AltModifier);
	if (!alt) {
		QPointF pivot;
		if (screenPoint(m_pivot, &pivot) && QLineF(screen, pivot).length() <= pick * 1.4) {
			hit.handle = LevelUvToolHandle::Pivot;
			return hit;
		}
		if (m_layout.isPatch()) {
			double best = pick;
			for (qsizetype i = 0; i < m_layout.uv.size(); ++i) {
				const QPointF point = texelScreen(m_layout.uv[i]);
				if (finitePoint(point) && QLineF(screen, point).length() <= best) {
					best = QLineF(screen, point).length();
					hit.handle = LevelUvToolHandle::PatchPoint;
					hit.index = static_cast<int>(i);
				}
			}
			if (hit.handle != LevelUvToolHandle::None) {
				return hit;
			}
			best = pick * 0.8;
			const int columns = m_layout.columns, rows = m_layout.rows;
			for (int r = 0; r < rows; ++r) {
				for (int c = 0; c + 1 < columns; ++c) {
					const double distance = segmentDistance(screen, texelScreen(m_layout.uv[r * columns + c]), texelScreen(m_layout.uv[r * columns + c + 1]));
					if (std::isfinite(distance) && distance <= best) {
						best = distance;
						hit.handle = LevelUvToolHandle::PatchRow;
						hit.index = r;
					}
				}
			}
			for (int c = 0; c < columns; ++c) {
				for (int r = 0; r + 1 < rows; ++r) {
					const double distance = segmentDistance(screen, texelScreen(m_layout.uv[r * columns + c]), texelScreen(m_layout.uv[(r + 1) * columns + c]));
					if (std::isfinite(distance) && distance <= best) {
						best = distance;
						hit.handle = LevelUvToolHandle::PatchColumn;
						hit.index = c;
					}
				}
			}
			if (hit.handle != LevelUvToolHandle::None) {
				return hit;
			}
		}
		const QVector<QPointF> ring = ringScreen(m_frame, m_pivot);
		for (qsizetype i = 0; i + 1 < ring.size(); ++i) {
			if (finitePoint(ring[i]) && finitePoint(ring[i + 1]) && segmentDistance(screen, ring[i], ring[i + 1]) <= pick * 0.8) {
				hit.handle = LevelUvToolHandle::Turn;
				return hit;
			}
		}
	}
	QPointF texel;
	if (!texelUnder(screen, &texel)) {
		return hit;
	}
	// A repeat line, where the pointer is nearer it than the middle of the
	// repeat beside it: a dense grid still leaves the texture to drag. A line
	// through the pivot neither stretches nor skews, so it is left to the move.
	const QPointF pivotAt = pivotTexel();
	const auto throughPivot = [](double line, double pivot) { return std::abs(line - pivot) <= 1e-6 * std::max(1.0, std::abs(line)); };
	double bestS = pick, bestT = pick;
	bool onS = false, onT = false;
	for (const double s : m_grid.s) {
		QPointF a, b;
		if (throughPivot(s, pivotAt.x()) || std::abs(texel.x() - s) >= m_grid.stepS / 4 || !segmentScreen(m_frame.worldAt({s, m_grid.minT}), m_frame.worldAt({s, m_grid.maxT}), &a, &b)) {
			continue;
		}
		const double distance = segmentDistance(screen, a, b);
		if (distance <= bestS) {
			bestS = distance;
			hit.lineS = s;
			onS = true;
		}
	}
	for (const double t : m_grid.t) {
		QPointF a, b;
		if (throughPivot(t, pivotAt.y()) || std::abs(texel.y() - t) >= m_grid.stepT / 4 || !segmentScreen(m_frame.worldAt({m_grid.minS, t}), m_frame.worldAt({m_grid.maxS, t}), &a, &b)) {
			continue;
		}
		const double distance = segmentDistance(screen, a, b);
		if (distance <= bestT) {
			bestT = distance;
			hit.lineT = t;
			onT = true;
		}
	}
	if (alt) {
		// A line of constant t runs along U, and skews along U; a line of
		// constant s skews along V.
		if (onS || onT) {
			hit.handle = onT && (!onS || bestT <= bestS) ? LevelUvToolHandle::SkewS : LevelUvToolHandle::SkewT;
		}
		return hit;
	}
	if (onS && onT) {
		hit.handle = LevelUvToolHandle::StretchBoth;
		return hit;
	}
	if (onS || onT) {
		hit.handle = onS ? LevelUvToolHandle::StretchS : LevelUvToolHandle::StretchT;
		return hit;
	}
	const QVector<QPointF> outline = m_layout.isPatch() ? patchOutline(m_layout, m_layout.uv) : m_layout.uv;
	if (insidePolygon(texel, outline)) {
		hit.handle = LevelUvToolHandle::Move;
	}
	return hit;
}

bool LevelCameraUvTool::beginDrag(const Hit& hit, QPointF screen)
{
	QPointF start;
	if (!m_hasSource || hit.handle == LevelUvToolHandle::None || !texelUnder(screen, &start)) {
		return false;
	}
	// A skew measures from the line pressed.
	if (hit.handle == LevelUvToolHandle::SkewS) {
		start.setY(hit.lineT);
	} else if (hit.handle == LevelUvToolHandle::SkewT) {
		start.setX(hit.lineS);
	}
	m_drag = hit;
	m_press = screen;
	m_startTexel = start;
	m_moved = false;
	m_edit = {};
	m_edit.handle = hit.handle;
	m_edit.pivot = pivotTexel();
	m_edit.allowValve220 = m_allowValve220;
	m_edit.points = levelUvToolPatchPoints(m_layout, hit.handle, hit.index);
	m_preview = {};
	m_previewReady = false;
	m_previewError.clear();
	return true;
}

bool LevelCameraUvTool::updateDrag(QPointF screen, Qt::KeyboardModifiers modifiers)
{
	if (!dragging()) {
		return false;
	}
	// A click is not a drag: snapping alone must never edit.
	if (!m_moved && QLineF(m_press, screen).length() < 3) {
		return false;
	}
	m_moved = true;
	QPointF current;
	if (!texelUnder(screen, &current)) {
		return true;
	}
	const bool oneAxis = modifiers.testFlag(Qt::ShiftModifier);
	const bool hard = modifiers.testFlag(Qt::ControlModifier);
	const LevelUvToolSnap snap = snapAt(current, hard);
	const QPointF pivot = m_edit.pivot;
	switch (m_drag.handle) {
	case LevelUvToolHandle::Pivot: {
		const QPointF to = levelUvToolSnapPivot(m_layout.uv, m_grid, current, snap);
		m_pivot = m_frame.worldAt(to);
		m_grid = levelUvToolGrid(m_layout.uv, levelUvToolRepeat(m_layout), to);
		return true;
	}
	case LevelUvToolHandle::Move:
		m_edit.move = levelUvToolMove(m_layout.uv, m_grid, pivot, m_startTexel, current, snap, oneAxis, m_frame);
		break;
	case LevelUvToolHandle::Turn:
		m_edit.turn = levelUvToolSnapTurn(m_frame, m_edges, m_frame.turnDegrees(pivot, m_startTexel, current), oneAxis ? m_angleStep : 0, 2.0, hard);
		break;
	case LevelUvToolHandle::StretchS:
	case LevelUvToolHandle::StretchT:
	case LevelUvToolHandle::StretchBoth: {
		QVector<double> s, t;
		for (const QPointF& point : std::as_const(m_layout.uv)) {
			s.append(point.x());
			t.append(point.y());
		}
		double factorS = 1, factorT = 1;
		if (m_drag.handle != LevelUvToolHandle::StretchT) {
			factorS = levelUvToolStretch(s, pivot.x(), m_drag.lineS, current.x(), snap.tolerance.x(), hard);
		}
		if (m_drag.handle != LevelUvToolHandle::StretchS) {
			factorT = levelUvToolStretch(t, pivot.y(), m_drag.lineT, current.y(), snap.tolerance.y(), hard);
		}
		if (oneAxis) {
			// Shift keeps the texture's proportions.
			if (m_drag.handle == LevelUvToolHandle::StretchS) {
				factorT = factorS;
			} else if (m_drag.handle == LevelUvToolHandle::StretchT) {
				factorS = factorT;
			} else {
				factorS = factorT = std::abs(std::log(factorS)) >= std::abs(std::log(factorT)) ? factorS : factorT;
			}
		}
		m_edit.stretch = {factorS, factorT};
		break;
	}
	case LevelUvToolHandle::SkewS:
		m_edit.skew = {levelUvToolSkew(m_drag.handle, m_layout.uv, m_edges, m_grid, pivot, m_startTexel, current, snap), 0};
		break;
	case LevelUvToolHandle::SkewT:
		m_edit.skew = {0, levelUvToolSkew(m_drag.handle, m_layout.uv, m_edges, m_grid, pivot, m_startTexel, current, snap)};
		break;
	case LevelUvToolHandle::PatchPoint:
	case LevelUvToolHandle::PatchRow:
	case LevelUvToolHandle::PatchColumn: {
		QVector<QPointF> moved;
		for (const int index : std::as_const(m_edit.points)) {
			moved.append(m_layout.uv.value(index));
		}
		m_edit.move = levelUvToolPointMove(moved, m_grid, pivot, m_startTexel, current, snap, oneAxis, m_frame);
		break;
	}
	case LevelUvToolHandle::None:
		return false;
	}
	m_previewReady = previewLevelUvToolEdit(m_layout, m_edit, &m_preview, &m_previewError);
	return true;
}

void LevelCameraUvTool::cancelDrag()
{
	if (m_drag.handle == LevelUvToolHandle::Pivot && m_hasSource) {
		// The pivot goes back to where the drag found it.
		m_pivot = m_frame.worldAt(m_edit.pivot);
		rebuild();
	}
	m_drag = {};
	m_moved = false;
	m_edit = {};
	m_preview = {};
	m_previewReady = false;
	m_previewError.clear();
}

bool LevelCameraUvTool::finishDrag()
{
	const bool moved = dragging() && m_moved;
	if (!moved) {
		cancelDrag();
		return false;
	}
	m_drag = {};
	m_moved = false;
	if (m_hasSource) {
		rebuild();
	}
	return true;
}

QString LevelCameraUvTool::dragSummary() const
{
	QString text;
	switch (m_drag.handle) {
	case LevelUvToolHandle::Pivot: {
		const QPointF pivot = pivotTexel();
		text = tr("Pivot at %1, %2 texels.").arg(number(pivot.x()), number(pivot.y()));
		break;
	}
	case LevelUvToolHandle::Move:
		text = tr("Move the texture %1, %2 texels.").arg(number(m_edit.move.x()), number(m_edit.move.y()));
		break;
	case LevelUvToolHandle::Turn:
		text = tr("Turn the texture %1 degrees.").arg(number(m_edit.turn));
		break;
	case LevelUvToolHandle::StretchS:
	case LevelUvToolHandle::StretchT:
	case LevelUvToolHandle::StretchBoth:
		text = tr("Stretch the texture's coordinates %1 times along U and %2 times along V.").arg(number(m_edit.stretch.x()), number(m_edit.stretch.y()));
		break;
	case LevelUvToolHandle::SkewS:
		text = tr("Skew the texture along U by %1.").arg(number(m_edit.skew.x()));
		break;
	case LevelUvToolHandle::SkewT:
		text = tr("Skew the texture along V by %1.").arg(number(m_edit.skew.y()));
		break;
	case LevelUvToolHandle::PatchPoint:
	case LevelUvToolHandle::PatchRow:
	case LevelUvToolHandle::PatchColumn: {
		const int count = static_cast<int>(m_edit.points.size());
		text = tr("Move %n control point(s) %1, %2 texels.", nullptr, count).arg(number(m_edit.move.x()), number(m_edit.move.y()));
		break;
	}
	case LevelUvToolHandle::None:
		return {};
	}
	if (m_moved && !m_previewReady && !m_previewError.isEmpty()) {
		text = tr("%1 %2").arg(text, m_previewError);
	}
	return text;
}

QString LevelCameraUvTool::hitHelp(const Hit& hit) const
{
	switch (hit.handle) {
	case LevelUvToolHandle::Pivot:
		return tr("Drag the pivot: it snaps to the repeat lines and the surface's corners, and Ctrl always snaps.");
	case LevelUvToolHandle::Move:
		return tr("Drag to move the texture: Shift keeps one direction, and a corner lands on a repeat line when close (Ctrl always).");
	case LevelUvToolHandle::Turn:
		return tr("Drag the ring to turn the texture about the pivot: Shift turns in steps, and it lines up with an edge when close (Ctrl always).");
	case LevelUvToolHandle::StretchS:
	case LevelUvToolHandle::StretchT:
	case LevelUvToolHandle::StretchBoth:
		return tr("Drag the line to stretch the texture about the pivot: Shift keeps its proportions, and a corner meets the line when close "
				  "(Ctrl always). Alt and a line skews.");
	case LevelUvToolHandle::SkewS:
	case LevelUvToolHandle::SkewT:
		return tr("Alt-drag the line to skew the texture about the pivot: it lines up with an edge when close (Ctrl always).");
	case LevelUvToolHandle::PatchPoint:
		return tr("Drag the control point to move its texture coordinates: Shift keeps one direction, and Ctrl lands it on a repeat line.");
	case LevelUvToolHandle::PatchRow:
		return tr("Drag the row to move its control points' texture coordinates: Shift keeps one direction, and Ctrl lands them on a repeat line.");
	case LevelUvToolHandle::PatchColumn:
		return tr("Drag the column to move its control points' texture coordinates: Shift keeps one direction, and Ctrl lands them on a repeat "
				  "line.");
	case LevelUvToolHandle::None:
		break;
	}
	return {};
}

QString LevelCameraUvTool::describe() const
{
	if (!m_hasSource) {
		return m_reason.isEmpty() ? tr("UV Tool: pick one brush face or select one patch.") : tr("UV Tool: %1").arg(m_reason);
	}
	const QPointF pivot = pivotTexel();
	const QString surface = m_layout.isPatch() ? tr("patch %1").arg(m_layout.target.objectId)
											   : tr("brush %1, face %2").arg(m_layout.target.objectId).arg(m_layout.target.faceIndex + 1);
	return tr("UV Tool on %1 (%2): pivot at %3, %4 texels; repeat lines every %5 by %6 texels.")
		.arg(surface, m_layout.material, number(pivot.x()), number(pivot.y()), number(m_grid.stepS), number(m_grid.stepT));
}

void LevelCameraUvTool::paint(QPainter& painter) const
{
	if (!m_hasSource || !m_camera) {
		return;
	}
	const bool previewing = dragging() && m_moved && m_previewReady && m_drag.handle != LevelUvToolHandle::Pivot;
	const LevelUvToolFrame& content = previewing ? m_preview.frame : m_frame;
	const QVector<QPointF>& uv = previewing ? m_preview.uv : m_layout.uv;
	const QPointF pivotTexelNow = content.texelAt(m_pivot);
	const LevelUvToolGrid grid = previewing ? levelUvToolGrid(uv, levelUvToolRepeat(m_layout), pivotTexelNow) : m_grid;
	const QColor highlight = m_camera->palette().color(QPalette::Highlight);
	const QColor light(255, 255, 255, 200), dark(0, 0, 0, 160);
	const Hit& active = dragging() ? m_drag : m_hover;
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setBrush(Qt::NoBrush);
	// Each mark is drawn dark under light, readable over any texture; the one
	// held or under the pointer is thicker and in the highlight colour.
	const auto twice = [&painter, dark](const QColor& over, double width, const std::function<void()>& draw) {
		painter.setPen(QPen(dark, width + 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		draw();
		painter.setPen(QPen(over, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		draw();
	};
	const auto line = [&](const LevelMapVec3& a, const LevelMapVec3& b, const QColor& colour, double width) {
		QPointF sa, sb;
		if (segmentScreen(a, b, &sa, &sb)) {
			twice(colour, width, [&] { painter.drawLine(sa, sb); });
		}
	};
	// The repeat lines, moving with the texture.
	const bool stretchS = active.handle == LevelUvToolHandle::StretchS || active.handle == LevelUvToolHandle::StretchBoth
		|| active.handle == LevelUvToolHandle::SkewT;
	const bool stretchT = active.handle == LevelUvToolHandle::StretchT || active.handle == LevelUvToolHandle::StretchBoth
		|| active.handle == LevelUvToolHandle::SkewS;
	for (const double s : grid.s) {
		const bool held = stretchS && s == active.lineS;
		line(content.worldAt({s, grid.minT}), content.worldAt({s, grid.maxT}), held ? highlight : light, held ? 2.5 : 1.0);
	}
	for (const double t : grid.t) {
		const bool held = stretchT && t == active.lineT;
		line(content.worldAt({grid.minS, t}), content.worldAt({grid.maxS, t}), held ? highlight : light, held ? 2.5 : 1.0);
	}
	// The surface: a face's outline, a patch's control points and their
	// rows and columns where they read the texture.
	const bool moving = active.handle == LevelUvToolHandle::Move;
	if (!m_layout.isPatch()) {
		for (qsizetype i = 0; i < m_layout.points.size(); ++i) {
			line(m_layout.points[i], m_layout.points[(i + 1) % m_layout.points.size()], highlight, moving ? 3.0 : 2.0);
		}
	} else {
		const int columns = m_layout.columns, rows = m_layout.rows;
		if (uv.size() == qsizetype(columns) * rows) {
			for (int r = 0; r < rows; ++r) {
				const bool held = active.handle == LevelUvToolHandle::PatchRow && active.index == r;
				for (int c = 0; c + 1 < columns; ++c) {
					line(content.worldAt(uv[r * columns + c]), content.worldAt(uv[r * columns + c + 1]), highlight, held ? 3.0 : 1.5);
				}
			}
			for (int c = 0; c < columns; ++c) {
				const bool held = active.handle == LevelUvToolHandle::PatchColumn && active.index == c;
				for (int r = 0; r + 1 < rows; ++r) {
					line(content.worldAt(uv[r * columns + c]), content.worldAt(uv[(r + 1) * columns + c]), highlight, held ? 3.0 : 1.5);
				}
			}
			const QColor base = m_camera->palette().color(QPalette::Base);
			for (qsizetype i = 0; i < uv.size(); ++i) {
				QPointF at;
				if (!screenPoint(content.worldAt(uv[i]), &at)) {
					continue;
				}
				const bool held = (active.handle == LevelUvToolHandle::PatchPoint || active.handle == LevelUvToolHandle::PatchRow
									  || active.handle == LevelUvToolHandle::PatchColumn)
					&& levelUvToolPatchPoints(m_layout, active.handle, active.index).contains(static_cast<int>(i));
				painter.setBrush(held ? QBrush(highlight) : QBrush(base));
				twice(highlight, 1.5, [&] { painter.drawRect(QRectF(at.x() - 4.5, at.y() - 4.5, 9, 9)); });
				painter.setBrush(Qt::NoBrush);
			}
		}
	}
	// The ring about the pivot, and the texture's U direction across it.
	QPointF pivot;
	if (screenPoint(m_pivot, &pivot)) {
		const bool turning = active.handle == LevelUvToolHandle::Turn;
		const QVector<QPointF> ring = ringScreen(m_frame, m_pivot);
		QPolygonF stroke;
		const auto flush = [&] {
			if (stroke.size() > 1) {
				twice(turning ? highlight : light, turning ? 2.5 : 1.25, [&] { painter.drawPolyline(stroke); });
			}
			stroke.clear();
		};
		for (const QPointF& point : ring) {
			if (finitePoint(point)) {
				stroke << point;
			} else {
				flush();
			}
		}
		flush();
		const double radius = ringRadiusWorld(m_frame, m_pivot);
		const LevelMapVec3 along = unit(content.alongS);
		QPointF knob;
		if (radius > 0 && along.valid && screenPoint(plus(m_pivot, times(along, radius)), &knob)) {
			twice(light, 1.0, [&] { painter.drawLine(pivot, knob); });
			painter.setBrush(turning ? QBrush(highlight) : QBrush(m_camera->palette().color(QPalette::Base)));
			twice(highlight, 1.5, [&] { painter.drawEllipse(knob, 5.0, 5.0); });
			painter.setBrush(Qt::NoBrush);
		}
		// The pivot: a ringed cross.
		const double arm = active.handle == LevelUvToolHandle::Pivot ? 16.0 : 13.0;
		twice(highlight, active.handle == LevelUvToolHandle::Pivot ? 2.5 : 2.0, [&] {
			painter.drawEllipse(pivot, 7.0, 7.0);
			painter.drawLine(pivot + QPointF(-arm, 0), pivot + QPointF(arm, 0));
			painter.drawLine(pivot + QPointF(0, -arm), pivot + QPointF(0, arm));
		});
	}
	painter.restore();
}

} // namespace vibestudio
