// The camera UV tool's service: NetRadiant Custom's UV tool (G), which lays a
// face's or a patch's texture grid over the surface and moves, turns,
// stretches and skews the texture about a movable pivot, snapping to the
// surface's points and edges and to the repeat lines, and moves a patch's
// control points one by one or a row or column at a time.
// Workflow reference: NRC radiant/selection_mtor_uv.cpp UVManipulator
// (testSelect, Transform, its snapping and freezeTransform) and
// radiant/tools.cpp MouseUV at 449778b3 (GPL-2.0-or-later). Original
// implementation over VibeStudio's Face inspector and UV Editor services; see
// docs/CREDITS.md.

#include "core/level_uv_tool.h"

#include "core/level_surface.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace vibestudio {
namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioLevelUvTool)
};

bool fail(QString* error, const QString& message)
{
	if (error) {
		*error = message;
	}
	return false;
}

using Vec = LevelMapVec3;

Vec add(const Vec& a, const Vec& b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z, true};
}

Vec sub(const Vec& a, const Vec& b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z, true};
}

Vec mul(const Vec& a, double s)
{
	return {a.x * s, a.y * s, a.z * s, true};
}

double dot(const Vec& a, const Vec& b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec cross(const Vec& a, const Vec& b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, true};
}

double length(const Vec& a)
{
	return std::sqrt(dot(a, a));
}

bool finiteVec(const Vec& a)
{
	return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}

bool finitePoint(QPointF p)
{
	return std::isfinite(p.x()) && std::isfinite(p.y());
}

constexpr double kDegrees = 180.0 / std::numbers::pi;

// Degrees from -180 to 180.
double wrapped(double degrees)
{
	if (!std::isfinite(degrees)) {
		return 0;
	}
	double result = std::fmod(degrees, 360.0);
	if (result > 180.0) {
		result -= 360.0;
	} else if (result <= -180.0) {
		result += 360.0;
	}
	return result;
}

// `v` turned `degrees` anticlockwise about the unit `axis` (Rodrigues).
Vec rotated(const Vec& v, const Vec& axis, double degrees)
{
	const double radians = degrees / kDegrees, c = std::cos(radians), s = std::sin(radians);
	return add(add(mul(v, c), mul(cross(axis, v), s)), mul(axis, dot(axis, v) * (1 - c)));
}

// The angle about the unit `axis` from `a` to `b`, both projected onto the
// plane across it, in degrees from -180 to 180.
double signedAngle(const Vec& a, const Vec& b, const Vec& axis)
{
	const Vec pa = sub(a, mul(axis, dot(a, axis))), pb = sub(b, mul(axis, dot(b, axis)));
	if (length(pa) < 1e-12 || length(pb) < 1e-12) {
		return 0;
	}
	return std::atan2(dot(cross(pa, pb), axis), dot(pa, pb)) * kDegrees;
}

// Texture values keep 1/10000, as the Face inspector's do.
double rounded4(double value)
{
	const double result = std::round(value * 10000.0) / 10000.0;
	return result == 0 ? 0.0 : result;
}

bool samePoints(const QVector<Vec>& a, const QVector<Vec>& b)
{
	return a.size() == b.size()
		&& std::equal(a.cbegin(), a.cend(), b.cbegin(), [](const Vec& x, const Vec& y) { return x.x == y.x && x.y == y.y && x.z == y.z; });
}

// Every point's texels on `face`.
bool faceUv(const LevelUvLayout& layout, const LevelMapBrushFace& face, QVector<QPointF>* uv)
{
	uv->clear();
	uv->reserve(layout.points.size());
	for (const Vec& point : layout.points) {
		QPointF texel;
		if (!levelFaceTexelAt(face, layout.imageSize, point, &texel)) {
			return false;
		}
		uv->append(texel);
	}
	return true;
}

// The nearest of `candidates` to `value`: its offset from `value`.
double nearestOffset(const QVector<double>& candidates, double value)
{
	double best = std::numeric_limits<double>::infinity();
	for (const double candidate : candidates) {
		const double offset = candidate - value;
		if (std::isfinite(offset) && std::abs(offset) < std::abs(best)) {
			best = offset;
		}
	}
	return best;
}

// The offset from `value` to the nearest line `step` apart, lines lying on
// whole multiples of it.
double lineOffset(double value, double step)
{
	if (!(step > 0) || !std::isfinite(value)) {
		return std::numeric_limits<double>::infinity();
	}
	return std::round(value / step) * step - value;
}

LevelUvTransform aboutPivot(QPointF pivot, double xx, double xy, double yx, double yy)
{
	LevelUvTransform result;
	result.xx = xx;
	result.xy = xy;
	result.yx = yx;
	result.yy = yy;
	result.dx = pivot.x() - (xx * pivot.x() + xy * pivot.y());
	result.dy = pivot.y() - (yx * pivot.x() + yy * pivot.y());
	return result;
}

bool wholeSurfaceHandle(LevelUvToolHandle handle)
{
	return handle == LevelUvToolHandle::Move || handle == LevelUvToolHandle::Turn || handle == LevelUvToolHandle::StretchS
		|| handle == LevelUvToolHandle::StretchT || handle == LevelUvToolHandle::StretchBoth || handle == LevelUvToolHandle::SkewS
		|| handle == LevelUvToolHandle::SkewT;
}

// The texture-space transform of a whole-surface edit: new texels of every
// world point from the old ones. A turn's sense follows the frame's normal.
LevelUvTransform wholeTransform(const LevelUvToolEdit& edit, const LevelUvToolFrame& frame)
{
	switch (edit.handle) {
	case LevelUvToolHandle::Move:
		return LevelUvTransform::translation(edit.move);
	case LevelUvToolHandle::Turn: {
		// The texture turning anticlockwise about the normal turns every
		// point's texels the other way; a frame whose s and t run clockwise
		// about the normal turns them the same way.
		const double sense = dot(cross(frame.alongS, frame.alongT), frame.normal) >= 0 ? -1.0 : 1.0;
		return LevelUvTransform::rotation(edit.pivot, sense * edit.turn);
	}
	case LevelUvToolHandle::StretchS:
	case LevelUvToolHandle::StretchT:
	case LevelUvToolHandle::StretchBoth:
		return aboutPivot(edit.pivot, edit.stretch.x(), 0, 0, edit.stretch.y());
	case LevelUvToolHandle::SkewS:
		return aboutPivot(edit.pivot, 1, edit.skew.x(), 0, 1);
	case LevelUvToolHandle::SkewT:
		return aboutPivot(edit.pivot, 1, 0, edit.skew.y(), 1);
	default:
		return {};
	}
}

// The rotation value that turns a face's texture `degrees` about the
// frame's normal, keeping `pivot`: a rotation value turns faces of each
// format, and mirrored or unevenly scaled ones, at its own rate, so the
// rate is measured and the turn refined until the texture's U direction has
// turned as far as asked.
bool faceTurnValue(const LevelUvLayout& layout, const LevelUvToolFrame& frame, const LevelTextureValues& current, const Vec& pivot, double degrees,
	double* rotation, QString* error)
{
	const auto measure = [&](double delta, double* turned) {
		LevelFaceAttributesEdit edit;
		edit.setRotation = true;
		edit.rotation = current.rotation + delta;
		edit.keepPivot = true;
		edit.pivot = pivot;
		LevelMapBrushFace face;
		QVector<QPointF> uv;
		if (!applyLevelFaceAttributesEdit(layout.face, edit, layout.imageSize, &face, error) || !faceUv(layout, face, &uv)) {
			return false;
		}
		const LevelUvToolFrame after = levelUvToolFrame(layout.points, uv);
		if (!after.valid) {
			return false;
		}
		*turned = signedAngle(frame.alongS, after.alongS, frame.normal);
		return true;
	};
	double rate = 0;
	if (!measure(1, &rate)) {
		return error && !error->isEmpty() ? false : fail(error, Text::tr("The face's texture cannot turn: its mapping collapses."));
	}
	if (std::abs(rate) < 0.05) {
		rate = rate < 0 ? -1.0 : 1.0;
	}
	double delta = degrees / rate;
	for (int pass = 0; pass < 4; ++pass) {
		double turned = 0;
		if (!measure(delta, &turned)) {
			break;
		}
		const double residual = wrapped(degrees - turned);
		if (std::abs(residual) < 1e-7) {
			break;
		}
		delta += residual / rate;
	}
	double value = current.rotation + delta;
	if (std::abs(value) >= 360.0) {
		value = std::fmod(value, 360.0);
	}
	*rotation = rounded4(value);
	return true;
}

} // namespace

LevelMapVec3 LevelUvToolFrame::worldAt(QPointF texel) const
{
	return add(origin, add(mul(alongS, texel.x()), mul(alongT, texel.y())));
}

QPointF LevelUvToolFrame::texelAt(const LevelMapVec3& world) const
{
	const Vec d = sub(world, origin);
	const double aa = dot(alongS, alongS), ab = dot(alongS, alongT), bb = dot(alongT, alongT);
	const double determinant = aa * bb - ab * ab;
	if (!valid || !(std::abs(determinant) > 0)) {
		return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
	}
	const double ad = dot(alongS, d), bd = dot(alongT, d);
	return {(bb * ad - ab * bd) / determinant, (aa * bd - ab * ad) / determinant};
}

bool LevelUvToolFrame::hit(const LevelMapVec3& from, const LevelMapVec3& direction, LevelMapVec3* point, bool forwardOnly) const
{
	const double facing = dot(direction, normal);
	if (!valid || !finiteVec(from) || !finiteVec(direction) || std::abs(facing) < 1e-12 * std::max(1.0, length(direction))) {
		return false;
	}
	const double along = dot(sub(origin, from), normal) / facing;
	if (!std::isfinite(along) || (forwardOnly && along < 0)) {
		return false;
	}
	if (point) {
		*point = add(from, mul(direction, along));
	}
	return true;
}

double LevelUvToolFrame::turnDegrees(QPointF pivot, QPointF from, QPointF to) const
{
	const QPointF a = from - pivot, b = to - pivot;
	if (!valid || !finitePoint(a) || !finitePoint(b)) {
		return 0;
	}
	return signedAngle(add(mul(alongS, a.x()), mul(alongT, a.y())), add(mul(alongS, b.x()), mul(alongT, b.y())), normal);
}

LevelUvToolFrame LevelUvToolFrame::transformed(const LevelUvTransform& transform) const
{
	LevelUvToolFrame result = *this;
	const double determinant = transform.xx * transform.yy - transform.xy * transform.yx;
	if (!valid || !transform.isFinite() || !(std::abs(determinant) > 1e-300)) {
		result.valid = false;
		return result;
	}
	// Old texels from new: the inverse transform.
	const double ixx = transform.yy / determinant, ixy = -transform.xy / determinant;
	const double iyx = -transform.yx / determinant, iyy = transform.xx / determinant;
	const double idx = -(ixx * transform.dx + ixy * transform.dy), idy = -(iyx * transform.dx + iyy * transform.dy);
	result.origin = add(origin, add(mul(alongS, idx), mul(alongT, idy)));
	result.alongS = add(mul(alongS, ixx), mul(alongT, iyx));
	result.alongT = add(mul(alongS, ixy), mul(alongT, iyy));
	return result;
}

LevelUvToolFrame levelUvToolFrame(const QVector<LevelMapVec3>& points, const QVector<QPointF>& uv)
{
	LevelUvToolFrame frame;
	const qsizetype count = points.size();
	if (count < 3 || uv.size() != count) {
		return frame;
	}
	// Least squares about the means: world - mean = (s - ms) A + (t - mt) B.
	double ms = 0, mt = 0;
	Vec mp {0, 0, 0, true};
	for (qsizetype i = 0; i < count; ++i) {
		if (!finitePoint(uv[i]) || !finiteVec(points[i])) {
			return frame;
		}
		ms += uv[i].x();
		mt += uv[i].y();
		mp = add(mp, points[i]);
	}
	ms /= static_cast<double>(count);
	mt /= static_cast<double>(count);
	mp = mul(mp, 1.0 / static_cast<double>(count));
	double ss = 0, st = 0, tt = 0;
	Vec sp {0, 0, 0, true}, tp {0, 0, 0, true};
	for (qsizetype i = 0; i < count; ++i) {
		const double s = uv[i].x() - ms, t = uv[i].y() - mt;
		const Vec p = sub(points[i], mp);
		ss += s * s;
		st += s * t;
		tt += t * t;
		sp = add(sp, mul(p, s));
		tp = add(tp, mul(p, t));
	}
	const double determinant = ss * tt - st * st;
	if (!(determinant > 1e-12 * std::max(1e-300, ss * tt))) {
		return frame;
	}
	frame.alongS = mul(sub(mul(sp, tt), mul(tp, st)), 1.0 / determinant);
	frame.alongT = mul(sub(mul(tp, ss), mul(sp, st)), 1.0 / determinant);
	frame.origin = sub(mp, add(mul(frame.alongS, ms), mul(frame.alongT, mt)));
	const Vec normal = cross(frame.alongS, frame.alongT);
	const double size = length(normal);
	if (!finiteVec(frame.origin) || !finiteVec(frame.alongS) || !finiteVec(frame.alongT) || !(size > 1e-12 * length(frame.alongS) * length(frame.alongT))) {
		return frame;
	}
	frame.normal = mul(normal, 1.0 / size);
	frame.valid = true;
	return frame;
}

QSizeF levelUvToolRepeat(const LevelUvLayout& layout)
{
	if (layout.imageSize.width() > 0 && layout.imageSize.height() > 0) {
		return QSizeF(layout.imageSize);
	}
	return {64, 64};
}

LevelUvToolGrid levelUvToolGrid(const QVector<QPointF>& uv, QSizeF repeat, QPointF pivot, int maxLines)
{
	LevelUvToolGrid grid;
	if (uv.isEmpty() || !(repeat.width() > 0) || !(repeat.height() > 0)) {
		return grid;
	}
	double lowS = std::numeric_limits<double>::infinity(), highS = -lowS, lowT = lowS, highT = -lowS;
	const auto include = [&](QPointF point) {
		if (!finitePoint(point)) {
			return;
		}
		lowS = std::min(lowS, point.x());
		highS = std::max(highS, point.x());
		lowT = std::min(lowT, point.y());
		highT = std::max(highT, point.y());
	};
	for (const QPointF& point : uv) {
		include(point);
	}
	include(pivot);
	if (!std::isfinite(lowS) || !std::isfinite(lowT)) {
		return grid;
	}
	const int limit = std::max(2, maxLines);
	const auto axis = [limit](double low, double high, double step, double* first, double* last, double* used, QVector<double>* lines) {
		double from = std::floor(low / step), to = std::ceil(high / step);
		if (to <= from) {
			to = from + 1;
		}
		if (to - from + 1 > limit) {
			step *= std::ceil((to - from + 1) / limit);
			from = std::floor(low / step);
			to = std::ceil(high / step);
			if (to <= from) {
				to = from + 1;
			}
		}
		for (double k = from; k <= to && lines->size() <= limit; k += 1) {
			lines->append(k * step);
		}
		*first = from * step;
		*last = to * step;
		*used = step;
	};
	axis(lowS, highS, repeat.width(), &grid.minS, &grid.maxS, &grid.stepS, &grid.s);
	axis(lowT, highT, repeat.height(), &grid.minT, &grid.maxT, &grid.stepT, &grid.t);
	return grid;
}

QVector<QPair<QPointF, QPointF>> levelUvToolEdges(const LevelUvLayout& layout)
{
	QVector<QPair<QPointF, QPointF>> edges;
	const auto edge = [&edges](QPointF a, QPointF b) {
		if (finitePoint(a) && finitePoint(b) && a != b) {
			edges.append({a, b});
		}
	};
	if (layout.isPatch()) {
		const int columns = layout.columns, rows = layout.rows;
		if (columns <= 0 || rows <= 0 || layout.uv.size() != qsizetype(columns) * rows) {
			return edges;
		}
		for (int row = 0; row < rows; ++row) {
			for (int column = 0; column + 1 < columns; ++column) {
				edge(layout.uv[row * columns + column], layout.uv[row * columns + column + 1]);
			}
		}
		for (int column = 0; column < columns; ++column) {
			for (int row = 0; row + 1 < rows; ++row) {
				edge(layout.uv[row * columns + column], layout.uv[(row + 1) * columns + column]);
			}
		}
		return edges;
	}
	for (qsizetype i = 0; i < layout.uv.size(); ++i) {
		edge(layout.uv[i], layout.uv[(i + 1) % layout.uv.size()]);
	}
	return edges;
}

QPointF levelUvToolSnapPivot(const QVector<QPointF>& uv, const LevelUvToolGrid& grid, QPointF at, const LevelUvToolSnap& snap)
{
	if (!finitePoint(at)) {
		return at;
	}
	QVector<double> s = grid.s, t = grid.t;
	for (const QPointF& point : uv) {
		s.append(point.x());
		t.append(point.y());
	}
	QPointF result = at;
	const double alongS = nearestOffset(s, at.x()), alongT = nearestOffset(t, at.y());
	if (std::isfinite(alongS) && (snap.hard || std::abs(alongS) <= snap.tolerance.x())) {
		result.rx() += alongS;
	}
	if (std::isfinite(alongT) && (snap.hard || std::abs(alongT) <= snap.tolerance.y())) {
		result.ry() += alongT;
	}
	return result;
}

QPointF levelUvToolMove(const QVector<QPointF>& uv, const LevelUvToolGrid& grid, QPointF pivot, QPointF start, QPointF current,
	const LevelUvToolSnap& snap, bool oneAxis, const LevelUvToolFrame& frame)
{
	if (!finitePoint(start) || !finitePoint(current)) {
		return {};
	}
	const QPointF raw = start - current;
	QPointF move = raw;
	// A surface point or the pivot onto a repeat line, along each axis.
	QVector<QPointF> candidates = uv;
	if (finitePoint(pivot)) {
		candidates.append(pivot);
	}
	double bestS = std::numeric_limits<double>::infinity(), bestT = bestS;
	for (const QPointF& point : std::as_const(candidates)) {
		const double s = lineOffset(point.x() + raw.x(), grid.stepS), t = lineOffset(point.y() + raw.y(), grid.stepT);
		if (std::abs(s) < std::abs(bestS)) {
			bestS = s;
		}
		if (std::abs(t) < std::abs(bestT)) {
			bestT = t;
		}
	}
	if (std::isfinite(bestS) && (snap.hard || std::abs(bestS) <= snap.tolerance.x())) {
		move.rx() += bestS;
	}
	if (std::isfinite(bestT) && (snap.hard || std::abs(bestT) <= snap.tolerance.y())) {
		move.ry() += bestT;
	}
	if (oneAxis) {
		// Shift keeps the axis the pointer moved further along on the surface.
		const double s = std::abs(raw.x()) * (frame.valid ? length(frame.alongS) : 1.0);
		const double t = std::abs(raw.y()) * (frame.valid ? length(frame.alongT) : 1.0);
		(s < t ? move.rx() : move.ry()) = 0;
	}
	return move;
}

QPointF levelUvToolPointMove(const QVector<QPointF>& moved, const LevelUvToolGrid& grid, QPointF pivot, QPointF start, QPointF current,
	const LevelUvToolSnap& snap, bool oneAxis, const LevelUvToolFrame& frame)
{
	if (!finitePoint(start) || !finitePoint(current)) {
		return {};
	}
	const QPointF raw = current - start;
	QPointF move = raw;
	double bestS = std::numeric_limits<double>::infinity(), bestT = bestS;
	for (const QPointF& point : moved) {
		const QPointF at = point + raw;
		for (const double s : {lineOffset(at.x(), grid.stepS), finitePoint(pivot) ? pivot.x() - at.x() : bestS}) {
			if (std::abs(s) < std::abs(bestS)) {
				bestS = s;
			}
		}
		for (const double t : {lineOffset(at.y(), grid.stepT), finitePoint(pivot) ? pivot.y() - at.y() : bestT}) {
			if (std::abs(t) < std::abs(bestT)) {
				bestT = t;
			}
		}
	}
	if (std::isfinite(bestS) && (snap.hard || std::abs(bestS) <= snap.tolerance.x())) {
		move.rx() += bestS;
	}
	if (std::isfinite(bestT) && (snap.hard || std::abs(bestT) <= snap.tolerance.y())) {
		move.ry() += bestT;
	}
	if (oneAxis) {
		const double s = std::abs(raw.x()) * (frame.valid ? length(frame.alongS) : 1.0);
		const double t = std::abs(raw.y()) * (frame.valid ? length(frame.alongT) : 1.0);
		(s < t ? move.rx() : move.ry()) = 0;
	}
	return move;
}

double levelUvToolStretch(const QVector<double>& values, double pivot, double line, double current, double tolerance, bool hard)
{
	if (!std::isfinite(pivot) || !std::isfinite(line) || !std::isfinite(current) || std::abs(line - pivot) < 1e-9) {
		return 1;
	}
	// A surface point onto the dragged line, as NetRadiant Custom snaps it.
	double at = current;
	double best = std::numeric_limits<double>::infinity();
	for (const double value : values) {
		if (std::isfinite(value) && std::abs(value - pivot) > 1e-9 && std::abs(value - current) < std::abs(best)) {
			best = value - current;
		}
	}
	if (std::isfinite(best) && (hard || std::abs(best) <= tolerance)) {
		at = current + best;
	}
	const double arm = at - pivot;
	double factor = std::abs(arm) < 1e-9 ? 64.0 : (line - pivot) / arm;
	// Past the pivot is as small as the texture goes.
	if (!std::isfinite(factor) || factor <= 0) {
		factor = 64;
	}
	return std::clamp(factor, 1.0 / 64.0, 64.0);
}

double levelUvToolSkew(LevelUvToolHandle handle, const QVector<QPointF>& uv, const QVector<QPair<QPointF, QPointF>>& edges,
	const LevelUvToolGrid& grid, QPointF pivot, QPointF start, QPointF current, const LevelUvToolSnap& snap)
{
	const bool alongS = handle == LevelUvToolHandle::SkewS;
	if ((!alongS && handle != LevelUvToolHandle::SkewT) || !finitePoint(pivot) || !finitePoint(start) || !finitePoint(current)) {
		return 0;
	}
	// SkewS: the line pressed runs along s, at t = start.t; the pointer's s
	// must read start.s again. SkewT likewise along t.
	const double arm = alongS ? start.y() - pivot.y() : start.x() - pivot.x();
	if (std::abs(arm) < 1e-9) {
		return 0;
	}
	double factor = alongS ? (start.x() - current.x()) / arm : (start.y() - current.y()) / arm;
	// The skewed lines run along (-factor, 1) for SkewS and (1, -factor) for
	// SkewT; a direction (ds, dt) gives the factor that runs them along it.
	double best = std::numeric_limits<double>::infinity(), snapped = factor;
	const auto consider = [&](QPointF direction) {
		const double across = alongS ? direction.y() : direction.x();
		if (std::abs(across) < 1e-9) {
			return;
		}
		const double candidate = -(alongS ? direction.x() : direction.y()) / across;
		const double distance = std::abs(candidate - factor) * std::abs(arm);
		if (std::isfinite(candidate) && distance < best) {
			best = distance;
			snapped = candidate;
		}
	};
	for (const auto& edge : edges) {
		consider(edge.second - edge.first);
	}
	// From a repeat line's foot on the pivot's row (or column), or from the
	// pivot, to each surface point.
	for (const QPointF& point : uv) {
		for (const double line : alongS ? grid.s : grid.t) {
			consider(point - (alongS ? QPointF(line, pivot.y()) : QPointF(pivot.x(), line)));
		}
		consider(point - pivot);
	}
	if (std::isfinite(best) && (snap.hard || best <= (alongS ? snap.tolerance.x() : snap.tolerance.y()))) {
		factor = snapped;
	}
	if (!std::isfinite(factor)) {
		return 0;
	}
	return std::clamp(factor, -64.0, 64.0);
}

double levelUvToolSnapTurn(const LevelUvToolFrame& frame, const QVector<QPair<QPointF, QPointF>>& edges, double degrees, double step,
	double tolerance, bool hard)
{
	if (!std::isfinite(degrees)) {
		return 0;
	}
	if (std::isfinite(step) && step > 0) {
		return wrapped(std::round(degrees / step) * step);
	}
	if (!frame.valid) {
		return wrapped(degrees);
	}
	// The texture's U and V directions after the turn, against each edge.
	const Vec axes[2] = {rotated(frame.alongS, frame.normal, degrees), rotated(frame.alongT, frame.normal, degrees)};
	double best = std::numeric_limits<double>::infinity();
	for (const auto& edge : edges) {
		const QPointF d = edge.second - edge.first;
		const Vec direction = add(mul(frame.alongS, d.x()), mul(frame.alongT, d.y()));
		if (length(direction) < 1e-12) {
			continue;
		}
		for (const Vec& axis : axes) {
			double offset = signedAngle(axis, direction, frame.normal);
			// Either way along the edge.
			while (offset > 90.0) {
				offset -= 180.0;
			}
			while (offset <= -90.0) {
				offset += 180.0;
			}
			if (std::abs(offset) < std::abs(best)) {
				best = offset;
			}
		}
	}
	if (std::isfinite(best) && (hard || std::abs(best) <= tolerance)) {
		degrees += best;
	}
	return wrapped(degrees);
}

QVector<int> levelUvToolPatchPoints(const LevelUvLayout& layout, LevelUvToolHandle handle, int index)
{
	QVector<int> points;
	const int columns = layout.columns, rows = layout.rows;
	if (!layout.isPatch() || columns <= 0 || rows <= 0 || layout.uv.size() != qsizetype(columns) * rows || index < 0) {
		return points;
	}
	if (handle == LevelUvToolHandle::PatchPoint && index < layout.uv.size()) {
		points.append(index);
	} else if (handle == LevelUvToolHandle::PatchRow && index < rows) {
		for (int column = 0; column < columns; ++column) {
			points.append(index * columns + column);
		}
	} else if (handle == LevelUvToolHandle::PatchColumn && index < columns) {
		for (int row = 0; row < rows; ++row) {
			points.append(row * columns + index);
		}
	}
	return points;
}

bool LevelUvToolEdit::changesSomething() const
{
	switch (handle) {
	case LevelUvToolHandle::Move:
	case LevelUvToolHandle::PatchPoint:
	case LevelUvToolHandle::PatchRow:
	case LevelUvToolHandle::PatchColumn:
		return move.x() != 0 || move.y() != 0;
	case LevelUvToolHandle::Turn:
		return turn != 0;
	case LevelUvToolHandle::StretchS:
	case LevelUvToolHandle::StretchT:
	case LevelUvToolHandle::StretchBoth:
		return stretch.x() != 1 || stretch.y() != 1;
	case LevelUvToolHandle::SkewS:
		return skew.x() != 0;
	case LevelUvToolHandle::SkewT:
		return skew.y() != 0;
	case LevelUvToolHandle::None:
	case LevelUvToolHandle::Pivot:
		break;
	}
	return false;
}

bool previewLevelUvToolEdit(const LevelUvLayout& layout, const LevelUvToolEdit& edit, LevelUvToolResult* result, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!result) {
		return fail(error, Text::tr("Missing UV tool result."));
	}
	LevelUvToolResult out;
	out.face = layout.face;
	out.patch = layout.patch;
	out.uv = layout.uv;
	const LevelUvToolFrame base = levelUvToolFrame(layout.points, layout.uv);
	if (!base.valid) {
		return fail(error, Text::tr("The texture runs along the surface, so the UV tool cannot lay its lines over it. Reset the mapping first."));
	}
	out.frame = base;
	if (!edit.changesSomething()) {
		*result = std::move(out);
		return true;
	}
	if (!finitePoint(edit.pivot) || !finitePoint(edit.move) || !std::isfinite(edit.turn) || !finitePoint(edit.stretch) || !finitePoint(edit.skew)
		|| std::abs(edit.move.x()) > 1e9 || std::abs(edit.move.y()) > 1e9 || std::abs(edit.turn) > 360000) {
		return fail(error, Text::tr("The UV tool's edit must be finite and in range."));
	}
	if (edit.stretch.x() < 1.0 / 4096.0 || edit.stretch.y() < 1.0 / 4096.0 || edit.stretch.x() > 4096.0 || edit.stretch.y() > 4096.0) {
		return fail(error, Text::tr("A stretch must keep the texture between 1/4096 and 4096 times its size."));
	}
	const bool skew = edit.handle == LevelUvToolHandle::SkewS || edit.handle == LevelUvToolHandle::SkewT;
	if (skew && (std::abs(edit.skew.x()) > 4096.0 || std::abs(edit.skew.y()) > 4096.0)) {
		return fail(error, Text::tr("A skew must stay within 4096 texels per texel."));
	}

	if (!layout.isPatch()) {
		if (!wholeSurfaceHandle(edit.handle)) {
			return fail(error, Text::tr("Rows, columns and control points belong to patches; a face's texture moves as a whole."));
		}
		if (skew) {
			// Only a skew needs a transform no texture value holds.
			if (layout.format == LevelUvFormat::Classic && !edit.allowValve220) {
				return fail(error, Text::tr("A classic face cannot hold a skew. Turn on Allow Valve 220 Conversion to skew it."));
			}
			LevelUvEdit uvEdit;
			uvEdit.transform = wholeTransform(edit, base);
			uvEdit.allowValve220 = edit.allowValve220;
			LevelUvResult edited;
			if (!applyLevelUvEdit(layout, uvEdit, &edited, error)) {
				return false;
			}
			out.face = edited.face;
			out.uv = edited.uv;
			out.fit = edited.fit;
		} else {
			// A move, turn or stretch sets the face's own texture values, so
			// a classic face stays classic, as the Face inspector's handles do.
			LevelFaceAttributesEdit values;
			if (edit.handle == LevelUvToolHandle::Move) {
				values.moveTexels = edit.move;
			} else {
				LevelTextureValues current;
				if (!levelTextureValues(layout.face, layout.imageSize, &current, error)) {
					return false;
				}
				values.keepPivot = true;
				values.pivot = base.worldAt(edit.pivot);
				if (edit.handle == LevelUvToolHandle::Turn) {
					values.setRotation = true;
					if (!faceTurnValue(layout, base, current, values.pivot, wrapped(edit.turn), &values.rotation, error)) {
						return false;
					}
				} else {
					// More texels across the face is a smaller texture on it.
					// Classic and Valve 220 scales keep 1/10000, as the Face
					// inspector's do; a brush primitive's are read from its
					// matrix, so they stay exact.
					const bool exact = layout.face.explicitTextureMatrix;
					const auto scaled = [exact](double value, double by) {
						const double result = exact ? value / by : rounded4(value / by);
						return result == 0 ? value / by : result;
					};
					if (edit.stretch.x() != 1) {
						values.setScaleU = true;
						values.scaleU = scaled(current.scaleU, edit.stretch.x());
					}
					if (edit.stretch.y() != 1) {
						values.setScaleV = true;
						values.scaleV = scaled(current.scaleV, edit.stretch.y());
					}
				}
			}
			LevelMapBrushFace face;
			QVector<QPointF> uv;
			if (!applyLevelFaceAttributesEdit(layout.face, values, layout.imageSize, &face, error)) {
				return false;
			}
			if (!faceUv(layout, face, &uv)) {
				return fail(error, Text::tr("The new texture values give the face a mapping that is not finite."));
			}
			out.face = face;
			out.uv = uv;
			out.values = true;
			out.valuesEdit = values;
			out.fit = uv == layout.uv ? LevelUvFit::Unchanged : LevelUvFit::Exact;
		}
		out.frame = out.fit == LevelUvFit::Unchanged ? base : levelUvToolFrame(layout.points, out.uv);
		if (!out.frame.valid) {
			return fail(error, Text::tr("The edit collapses the texture onto a line."));
		}
		*result = std::move(out);
		return true;
	}

	// A patch: its control points' coordinates, in texture space.
	for (const int index : edit.points) {
		if (index < 0 || index >= layout.uv.size()) {
			const int count = static_cast<int>(layout.uv.size());
			return fail(error, Text::tr("The patch has %n control point(s); the edit names one it does not have.", nullptr, count));
		}
	}
	LevelUvEdit uvEdit;
	LevelUvToolFrame frame = base;
	if (wholeSurfaceHandle(edit.handle)) {
		const LevelUvTransform transform = wholeTransform(edit, base);
		uvEdit.patchUv = levelUvPatchTransformed(layout.uv, edit.points, transform);
		if (edit.points.isEmpty()) {
			// The lines move with the texture while the points stay.
			frame = base.transformed(transform);
		}
	} else if (edit.handle == LevelUvToolHandle::PatchPoint || edit.handle == LevelUvToolHandle::PatchRow
		|| edit.handle == LevelUvToolHandle::PatchColumn) {
		if (edit.points.isEmpty()) {
			return fail(error, Text::tr("Name the control points the move takes."));
		}
		uvEdit.patchUv = levelUvPatchTransformed(layout.uv, edit.points, LevelUvTransform::translation(edit.move));
	}
	LevelUvResult edited;
	if (!applyLevelUvEdit(layout, uvEdit, &edited, error)) {
		return false;
	}
	out.patch = edited.patch;
	out.uv = edited.uv;
	out.fit = edited.fit;
	out.frame = frame;
	if (!out.frame.valid) {
		return fail(error, Text::tr("The edit collapses the texture onto a line."));
	}
	*result = std::move(out);
	return true;
}

bool prepareLevelUvToolEdit(const LevelMapDocument& document, const LevelUvLayout& layout, const LevelUvToolEdit& edit, LevelSurfaceEditPlan* plan,
	LevelUvToolResult* result, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!plan) {
		return fail(error, Text::tr("Missing UV tool plan."));
	}
	LevelUvToolResult preview;
	if (!previewLevelUvToolEdit(layout, edit, &preview, error)) {
		return false;
	}
	if (preview.values && preview.changed()) {
		// The values apply to the face as the document holds it, so it must
		// still be the face the tool read.
		if (const QString problem = levelUvTargetProblem(document, layout.target); !problem.isEmpty()) {
			return fail(error, problem);
		}
		LevelUvLayout now;
		if (!readLevelUvLayout(document, layout.target, layout.imageSize, &now) || now.material != layout.material || now.uv != layout.uv
			|| !samePoints(now.points, layout.points)) {
			return fail(error, Text::tr("The surface changed after the UV tool read it. Pick it again."));
		}
		QHash<QString, QSize> sizes;
		if (layout.imageSize.isValid()) {
			sizes.insert(layout.material, layout.imageSize);
		}
		if (!prepareLevelFaceAttributesEdit(document, {LevelSurfaceFace {layout.target.objectId, layout.target.faceIndex}}, preview.valuesEdit, sizes,
				plan, error)) {
			return false;
		}
	} else {
		LevelUvEdit uvEdit;
		if (preview.changed()) {
			if (layout.isPatch()) {
				uvEdit.patchUv = preview.uv;
			} else {
				uvEdit.transform = wholeTransform(edit, levelUvToolFrame(layout.points, layout.uv));
				uvEdit.allowValve220 = edit.allowValve220;
			}
		}
		if (!prepareLevelUvEdit(document, layout, uvEdit, plan, error)) {
			return false;
		}
	}
	if (result) {
		*result = std::move(preview);
	}
	return true;
}

bool applyLevelUvToolEdit(LevelMapDocument* document, const LevelUvLayout& layout, const LevelUvToolEdit& edit, LevelUvToolResult* result,
	QString* error)
{
	if (!document) {
		return fail(error, Text::tr("Missing map."));
	}
	LevelSurfaceEditPlan plan;
	return prepareLevelUvToolEdit(*document, layout, edit, &plan, result, error) && commitLevelSurfaceEdit(document, plan, error);
}

} // namespace vibestudio
