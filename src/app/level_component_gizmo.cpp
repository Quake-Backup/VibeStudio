#include "app/level_component_gizmo.h"
#include "core/level_object_manipulator.h"
#include <QApplication>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <limits>

namespace vibestudio {
namespace {
bool finite(QPointF p) { return std::isfinite(p.x()) && std::isfinite(p.y()); }
bool finite(LevelMapVec3 p) { return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::abs(p.x) <= 32768 && std::abs(p.y) <= 32768 && std::abs(p.z) <= 32768; }
bool same(LevelMapVec3 a,LevelMapVec3 b) { return a.valid == b.valid && a.x == b.x && a.y == b.y && a.z == b.z; }
double squared(QPointF p) { return QPointF::dotProduct(p,p); }
double coordinate(LevelMapVec3 p,int axis) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; }
void setCoordinate(LevelMapVec3& p,int axis,double value) { if (axis == 0) { p.x = value; } else if (axis == 1) { p.y = value; } else { p.z = value; } }
LevelMapVec3 offset(LevelMapVec3 p,int axis,double distance) { setCoordinate(p,axis,coordinate(p,axis)+distance); return p; }
ModelVec3 model(LevelMapVec3 p) { return {float(p.x),float(p.y),float(p.z)}; }
double snapped(double value,double step) { return step > 0 ? std::round(value/step)*step : value; }
QRectF labelRect(QPointF point,const QFont& font,const QString& text) {
	const QFontMetricsF metrics(font); const QSizeF size(std::max(20.0,metrics.horizontalAdvance(text)+10),std::max(20.0,metrics.height()+4));
	return {point-QPointF(size.width()/2,size.height()/2),size};
}
constexpr std::array<std::array<int,2>,6> shearAxes{{{0,1},{0,2},{1,0},{1,2},{2,0},{2,1}}};
// The object tools' pivot point: a placed pivot is filled and ringed, the
// automatic one plain.
void paintPivotPoint(QPainter& painter,QPointF centre,bool hot,bool placed,bool highContrast,double scale,const QColor& selected)
{
	painter.setPen(QPen(Qt::black,1.5)); painter.setBrush(hot ? selected : placed ? QColor(255,232,0) : QColor(Qt::white));
	painter.drawEllipse(centre,4.5*scale,4.5*scale);
	if (placed) { painter.setBrush(Qt::NoBrush); painter.setPen(QPen(highContrast ? QColor(Qt::yellow) : QColor(0,125,255),1.5)); painter.drawEllipse(centre,7.5*scale,7.5*scale); }
}
// Translate's view-plane square, in pixels from the pivot, the pivot point's
// grab radius and an arrow's or ring's pointer tolerance, all at 100% text
// scale. NetRadiant Custom draws a 16-pixel screen square.
constexpr double kTranslateSquare = 12, kTranslatePivot = 6, kObjectTolerance = 8;
// The object Rotate tool's outer ring, as a share of the rings' radius
// (NetRadiant Custom draws its screen ring 15% outside the sphere).
constexpr double kScreenRing = 1.15;
// The axis-direction view component past which Translate and Scale leave an
// axis handle out, as NetRadiant Custom hides one pointing along the view.
constexpr double kAxisAlongView = .95;
// The object Transform tool's handle ids: the free move inside the box (6),
// the side lines (10 + slide axis * 4 + j * 2 + side: the line along the slide
// axis on the minimum (0) or maximum (1) side of axis (slide + j + 1) % 3,
// through the middle of the third axis), the corner points (30 + axis * 4 +
// j * 2 + k: turning about the axis, on the (axis + 1) % 3 side j and the
// (axis + 2) % 3 side k) and the scales from outside (50 + the sides as base-3
// digits, X first: 1 the minimum, 2 the maximum).
constexpr int kTransformMove = 6, kTransformSkew = 10, kTransformTurn = 30, kTransformScale = 50;
// A corner point's grab radius and the band outside the box that scales, in
// pixels at 100% text scale.
constexpr double kTransformCorner = 7, kTransformBand = 32;
struct SkewLine { int slide = 0, by = 1, side = 1; };
SkewLine skewLine(int handle) { const int n = handle-kTransformSkew; return {n/4,(n/4+(n/2)%2+1)%3,n%2 ? 1 : -1}; }
struct TurnPoint { int axis = 0; std::array<int,3> sides{0,0,0}; };
TurnPoint turnPoint(int handle)
{
	const int n = handle-kTransformTurn; TurnPoint point; point.axis = n/4;
	point.sides[static_cast<size_t>((point.axis+1)%3)] = (n/2)%2 ? 1 : -1; point.sides[static_cast<size_t>((point.axis+2)%3)] = n%2 ? 1 : -1;
	return point;
}
std::array<int,3> scaleSides(int handle)
{
	int code = handle-kTransformScale; std::array<int,3> sides{0,0,0};
	for (auto& side : sides) { const int digit = code%3; code /= 3; side = digit == 1 ? -1 : digit == 2 ? 1 : 0; }
	return sides;
}
int scaleHandle(const std::array<int,3>& sides)
{
	int code = 0, weight = 1;
	for (const int side : sides) { code += (side < 0 ? 1 : side > 0 ? 2 : 0)*weight; weight *= 3; }
	return kTransformScale+code;
}
LevelMapVec3 plus(LevelMapVec3 a,LevelMapVec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z,a.valid && b.valid}; }
LevelMapVec3 minus(LevelMapVec3 a,LevelMapVec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z,a.valid && b.valid}; }
LevelMapVec3 times(LevelMapVec3 a,double s) { return {a.x*s,a.y*s,a.z*s,a.valid}; }
double dot(LevelMapVec3 a,LevelMapVec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
LevelMapVec3 cross(LevelMapVec3 a,LevelMapVec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x,a.valid && b.valid}; }
bool finiteVector(LevelMapVec3 p) { return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
LevelMapVec3 unit(LevelMapVec3 v) { const double length = std::sqrt(dot(v,v)); return finiteVector(v) && std::isfinite(length) && length > 1e-12 ? times(v,1/length) : LevelMapVec3{}; }
LevelMapVec3 mapVector(ModelVec3 p) { return {p.x,p.y,p.z,true}; }
bool sameColumns(const std::array<LevelMapVec3,3>& a,const std::array<LevelMapVec3,3>& b) { return same(a[0],b[0]) && same(a[1],b[1]) && same(a[2],b[2]); }
// The box's centre with each axis `sides` names on its minimum (-1) or maximum (+1).
LevelMapVec3 boxPoint(LevelMapVec3 low,LevelMapVec3 high,const std::array<int,3>& sides)
{
	LevelMapVec3 point{(low.x+high.x)/2,(low.y+high.y)/2,(low.z+high.z)/2,true};
	for (int axis = 0; axis < 3; ++axis) {
		const int side = sides[static_cast<size_t>(axis)];
		if (side) { setCoordinate(point,axis,coordinate(side > 0 ? high : low,axis)); }
	}
	return point;
}
// Where the pointer's line meets the box: where it enters for the camera's
// forward ray, mid-box for a plan view's line. False when it misses.
bool lineHitsBox(const ModelPickRay& ray,LevelMapVec3 low,LevelMapVec3 high,LevelMapVec3* hit)
{
	const LevelMapVec3 origin{ray.origin.x,ray.origin.y,ray.origin.z,true}, direction{ray.direction.x,ray.direction.y,ray.direction.z,true};
	double nearest = ray.forwardOnly ? 0.0 : -std::numeric_limits<double>::infinity(), farthest = std::numeric_limits<double>::infinity();
	for (int axis = 0; axis < 3; ++axis) {
		const double o = coordinate(origin,axis), d = coordinate(direction,axis), a = coordinate(low,axis), b = coordinate(high,axis);
		if (!std::isfinite(o) || !std::isfinite(d)) { return false; }
		if (std::abs(d) < 1e-12) {
			if (o < a-1e-9 || o > b+1e-9) { return false; }
			continue;
		}
		double t0 = (a-o)/d, t1 = (b-o)/d;
		if (t0 > t1) { std::swap(t0,t1); }
		nearest = std::max(nearest,t0); farthest = std::min(farthest,t1);
		if (nearest > farthest+1e-9) { return false; }
	}
	const double t = ray.forwardOnly ? nearest : (nearest+farthest)/2;
	if (!std::isfinite(t)) { return false; }
	if (hit) { *hit = {origin.x+direction.x*t,origin.y+direction.y*t,origin.z+direction.z*t,true}; }
	return true;
}
// The sides a press just outside the box scales, after NetRadiant Custom's
// DragPlanes: those whose outside holds the point of the pointer's line
// nearest the box's centre, leaving out an axis along the line of sight and a
// side with no extent behind it; at most two, the farthest out.
std::array<int,3> outsideSides(const ModelPickRay& ray,LevelMapVec3 low,LevelMapVec3 high)
{
	std::array<int,3> sides{0,0,0};
	const LevelMapVec3 origin{ray.origin.x,ray.origin.y,ray.origin.z,true};
	const double length = std::sqrt(double(ray.direction.x)*ray.direction.x+double(ray.direction.y)*ray.direction.y+double(ray.direction.z)*ray.direction.z);
	if (!std::isfinite(length) || length < 1e-12) { return sides; }
	const LevelMapVec3 direction{ray.direction.x/length,ray.direction.y/length,ray.direction.z/length,true};
	const LevelMapVec3 centre{(low.x+high.x)/2,(low.y+high.y)/2,(low.z+high.z)/2,true};
	const double t = (centre.x-origin.x)*direction.x+(centre.y-origin.y)*direction.y+(centre.z-origin.z)*direction.z;
	if (!std::isfinite(t) || (ray.forwardOnly && t < 0)) { return sides; }
	const LevelMapVec3 nearest{origin.x+direction.x*t,origin.y+direction.y*t,origin.z+direction.z*t,true};
	std::array<double,3> beyond{0,0,0};
	for (int axis = 0; axis < 3; ++axis) {
		const double a = coordinate(low,axis), b = coordinate(high,axis), value = coordinate(nearest,axis);
		if (std::abs(coordinate(direction,axis)) > .9 || b-a <= 1e-6) { continue; }
		if (value > b) { sides[static_cast<size_t>(axis)] = 1; beyond[static_cast<size_t>(axis)] = value-b; }
		else if (value < a) { sides[static_cast<size_t>(axis)] = -1; beyond[static_cast<size_t>(axis)] = a-value; }
	}
	if (sides[0] && sides[1] && sides[2]) { sides[static_cast<size_t>(std::min_element(beyond.begin(),beyond.end())-beyond.begin())] = 0; }
	return sides;
}
double handleScale(const LevelComponentGizmoFrame& frame) { return std::isfinite(frame.handleScale) ? std::clamp(frame.handleScale,1.0,4.0) : 1.0; }
// The object Transform tool's pointer targets for the box as the view shows
// it. They grow with the text scale but never past a share of the box's
// smaller side on screen (3 pixels at least), and a zone round the box's
// centre always moves the selection, so a box small on screen, or a large
// text scale, keeps room to move it. Below kTransformSmallBox pixels the side
// lines and corners give way altogether.
constexpr double kTransformTargetShare = .15, kTransformZoneShare = .2, kTransformSmallBox = 16, kTransformShortLine = 6;
struct TransformTargets {
	double pivot = 0, corner = 0, line = 0, zone = 0, size = 0;
	QPointF centre, low, high;
	bool shown = false;
};
TransformTargets transformTargets(LevelMapVec3 low,LevelMapVec3 high,const LevelComponentGizmoFrame& frame)
{
	TransformTargets targets;
	if (!frame.project) { return targets; }
	QPointF least(std::numeric_limits<double>::infinity(),std::numeric_limits<double>::infinity()), most = -least;
	for (int corner = 0; corner < 8; ++corner) {
		const auto screen = frame.project({corner&1 ? high.x : low.x,corner&2 ? high.y : low.y,corner&4 ? high.z : low.z,true});
		if (!finite(screen)) { continue; }
		least = {std::min(least.x(),screen.x()),std::min(least.y(),screen.y())}; most = {std::max(most.x(),screen.x()),std::max(most.y(),screen.y())};
	}
	targets.shown = finite(least) && finite(most);
	if (!targets.shown) { return targets; }
	targets.low = least; targets.high = most;
	targets.size = std::min(most.x()-least.x(),most.y()-least.y());
	const double scale = handleScale(frame);
	const auto capped = [&](double wanted) { return std::max(3.0,std::min(wanted*scale,targets.size*kTransformTargetShare)); };
	targets.pivot = capped(kTranslatePivot); targets.corner = capped(kTransformCorner); targets.line = capped(kObjectTolerance);
	targets.zone = std::max(targets.pivot+4,targets.size*kTransformZoneShare);
	targets.centre = frame.project({(low.x+high.x)/2,(low.y+high.y)/2,(low.z+high.z)/2,true});
	return targets;
}
// The line of sight through the pivot, away from the viewer: the view
// direction in a parallel view, from the eye to the pivot in a perspective one.
LevelMapVec3 sightAxis(const LevelComponentGizmoFrame& frame,LevelMapVec3 pivot)
{
	if (frame.perspective && frame.ray && frame.project) {
		const auto screen = frame.project(pivot);
		if (finite(screen)) { if (const auto axis = unit(minus(pivot,mapVector(frame.ray(screen).origin))); axis.valid) { return axis; } }
	}
	return unit({frame.viewDirection.x,frame.viewDirection.y,frame.viewDirection.z,true});
}
// Where the pointer's ray meets the plane through `pivot` across `normal`, as
// a vector from the pivot; invalid when the ray runs along the plane.
LevelMapVec3 planeVector(const ModelPickRay& ray,LevelMapVec3 pivot,LevelMapVec3 normal)
{
	const auto origin = mapVector(ray.origin), direction = mapVector(ray.direction);
	const double facing = dot(direction,normal);
	if (!finiteVector(origin) || !finiteVector(direction) || !std::isfinite(facing) || std::abs(facing) < 1e-9) { return {}; }
	const auto point = plus(origin,times(direction,dot(minus(pivot,origin),normal)/facing));
	const auto vector = minus(point,pivot);
	return finiteVector(vector) && dot(vector,vector) > 1e-12 ? vector : LevelMapVec3{};
}
// How far, in pixels, the rings' radius reaches across the line of sight.
double screenRadius(const LevelComponentGizmoFrame& frame,LevelMapVec3 pivot,QPointF centre,LevelMapVec3 sight)
{
	int least = 0;
	for (int axis = 1; axis < 3; ++axis) { if (std::abs(coordinate(sight,axis)) < std::abs(coordinate(sight,least))) { least = axis; } }
	LevelMapVec3 other{0,0,0,true}; setCoordinate(other,least,1);
	const auto across = unit(cross(sight,other)); if (!across.valid) { return 0; }
	const auto edge = frame.project(plus(pivot,times(across,frame.radius)));
	return finite(edge) ? std::sqrt(squared(edge-centre)) : 0;
}
}
LevelComponentGizmo::LevelComponentGizmo(QObject* parent) : QObject(parent) {}

void LevelComponentGizmo::setContext(LevelComponentTool tool,const LevelMapVec3& pivot,bool available,double grid,
	double angleStep,double scaleStep,double shearStep)
{
	available = available && finite(pivot) && std::isfinite(grid) && grid >= 0 && grid <= 32768
		&& std::isfinite(angleStep) && angleStep >= 0 && angleStep <= 360 && std::isfinite(scaleStep) && scaleStep >= 0 && scaleStep <= 1
		&& std::isfinite(shearStep) && shearStep >= 0 && shearStep <= 1;
	if (tool == m_tool && same(pivot,m_pivot) && available == m_available && grid == m_grid && angleStep == m_angleStep && scaleStep == m_scaleStep && shearStep == m_shearStep) { return; }
	finish(false); m_tool = tool; m_pivot = pivot; m_available = available; m_grid = grid;
	m_angleStep = angleStep; m_scaleStep = scaleStep; m_shearStep = shearStep; Q_EMIT changed();
}
void LevelComponentGizmo::setObjectContext(const LevelMapVec3& mins,const LevelMapVec3& maxs,double boundsGrid,bool customPivot)
{
	boundsGrid = std::isfinite(boundsGrid) && boundsGrid > 0 && boundsGrid <= 32768 ? boundsGrid : 0;
	if (m_objects && same(mins,m_mins) && same(maxs,m_maxs) && boundsGrid == m_boundsGrid && customPivot == m_customPivot) { return; }
	finish(false); m_objects = true; m_mins = mins; m_maxs = maxs; m_boundsGrid = boundsGrid; m_customPivot = customPivot; Q_EMIT changed();
}
LevelMapVec3 LevelComponentGizmo::translation() const
{
	if (!m_active || !m_valid || m_handle == 4 || !m_request.delta.valid
		|| (m_tool != LevelComponentTool::Translate && transformKind() != LevelObjectTransformKind::Move)) { return {0,0,0,true}; }
	return m_request.delta;
}
LevelObjectTransformKind LevelComponentGizmo::transformKindOf(int handle)
{
	if (handle == 4) { return LevelObjectTransformKind::Pivot; }
	if (handle == kTransformMove) { return LevelObjectTransformKind::Move; }
	if (handle >= kTransformSkew && handle < kTransformSkew+12) { return LevelObjectTransformKind::Skew; }
	if (handle >= kTransformTurn && handle < kTransformTurn+12) { return LevelObjectTransformKind::Rotate; }
	if (handle > kTransformScale && handle < kTransformScale+27) { return LevelObjectTransformKind::Scale; }
	return LevelObjectTransformKind::None;
}
LevelObjectTransformKind LevelComponentGizmo::transformKind() const
{
	// Still known while finish() asks for the drag's commit.
	return (m_active || m_finishing) && m_objects && m_tool == LevelComponentTool::Transform ? transformKindOf(m_handle) : LevelObjectTransformKind::None;
}
bool LevelComponentGizmo::objectSkew(int* axis,int* side,LevelMapVec3* offset) const
{
	if (transformKind() != LevelObjectTransformKind::Skew || !m_transformPivot.valid) { return false; }
	const auto line = skewLine(m_handle);
	const double reach = coordinate(line.side > 0 ? m_maxs : m_mins,line.by)-coordinate(m_transformPivot,line.by);
	LevelMapVec3 slide{0,0,0,true}; setCoordinate(slide,line.slide,m_request.amount*reach);
	if (axis) { *axis = line.by; }
	if (side) { *side = line.side; }
	if (offset) { *offset = slide; }
	return true;
}
QString LevelComponentGizmo::toolName() const
{
	if (m_tool == LevelComponentTool::Translate) { return tr("Translate"); }
	if (m_objects && m_tool == LevelComponentTool::Rotate) { return tr("Rotate"); }
	if (m_objects && m_tool == LevelComponentTool::Scale) { return tr("Scale"); }
	if (m_objects && m_tool == LevelComponentTool::Transform) { return tr("Transform"); }
	return {};
}
void LevelComponentGizmo::setFreeTurn(bool on)
{
	if (on == m_freeTurn) { return; }
	finish(false); m_freeTurn = on; Q_EMIT changed();
}
LevelMapVec3 LevelComponentGizmo::scaleFactors() const
{
	if (!m_active || !m_valid || !m_objects || m_handle == 4 || !m_request.scale.valid
		|| (m_tool != LevelComponentTool::Scale && transformKind() != LevelObjectTransformKind::Scale)) { return {1,1,1,true}; }
	return m_request.scale;
}
void LevelComponentGizmo::setObjectPreviewEdges(const QVector<QPair<LevelMapVec3,LevelMapVec3>>& edges)
{
	if (!m_active || !m_objects) { return; }
	m_objectEdges = edges; Q_EMIT changed();
}
QVector<LevelComponentGizmoHandle> LevelComponentGizmo::handles(const LevelComponentGizmoFrame& frame) const
{
	QVector<LevelComponentGizmoHandle> result;
	if (!m_available || m_tool == LevelComponentTool::Move || !frame.project || !std::isfinite(frame.radius) || frame.radius <= 0) { return result; }
	if (m_objects && m_tool == LevelComponentTool::Transform) { return transformHandles(frame); }
	// A dragged pivot carries the handles; a turn about the pivot leaves it.
	const auto pivot = m_active && (m_tool == LevelComponentTool::Pivot || m_tool == LevelComponentTool::Translate || objectPivotHandle()) ? transformed(m_pivot) : m_pivot;
	const auto centre = frame.project(pivot); if (!finite(centre)) { return result; }
	const double scale = handleScale(frame);
	if (m_objects && m_tool == LevelComponentTool::Rotate) {
		// NetRadiant Custom's Rotate manipulator: X, Y and Z rings, an outer
		// ring about the line of sight, and the sphere inside them that turns
		// freely. A ring seen face-on would only repeat the outer one.
		const auto sight = sightAxis(frame,pivot);
		for (int axis = 0; axis < 3; ++axis) {
			if (sight.valid && std::abs(coordinate(sight,axis)) >= .999) { continue; }
			LevelComponentGizmoHandle handle; handle.id = handle.axis = axis; handle.text = QString(QLatin1Char("XYZ"[axis]));
			for (int i = 0; i <= 96; ++i) {
				const double radians = 2*std::numbers::pi*i/96;
				handle.points << frame.project(offset(offset(pivot,(axis+1)%3,frame.radius*std::cos(radians)),(axis+2)%3,frame.radius*std::sin(radians)));
			}
			handle.label = handle.points[(axis*17+12)%96]; result << handle;
		}
		const double radius = sight.valid ? screenRadius(frame,pivot,centre,sight) : 0;
		if (std::isfinite(radius) && radius >= 8) {
			LevelComponentGizmoHandle ring; ring.id = 3; ring.axis = 3; ring.label = centre;
			LevelComponentGizmoHandle sphere; sphere.id = 5; sphere.axis = 3; sphere.label = centre;
			for (int i = 0; i <= 96; ++i) {
				const double radians = 2*std::numbers::pi*i/96; const QPointF direction(std::cos(radians),std::sin(radians));
				ring.points << centre+direction*radius*kScreenRing; sphere.points << centre+direction*radius;
			}
			result << ring;
			if (m_freeTurn) { result << sphere; }
		}
		LevelComponentGizmoHandle point; point.id = 4; point.axis = 3; point.label = centre; point.points << centre; point.text = tr("Pivot");
		result << point;
		return result;
	}
	if (m_tool == LevelComponentTool::Translate || (m_objects && m_tool == LevelComponentTool::Scale)) {
		// Translate's arrows and object Scale's axis handles, the square and
		// the pivot point, as NetRadiant Custom lays both manipulators out.
		const double direction[]{frame.viewDirection.x,frame.viewDirection.y,frame.viewDirection.z};
		const double length = std::sqrt(direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2]);
		for (int axis = 0; axis < 3; ++axis) {
			// NetRadiant Custom hides an arrow that points nearly along the view.
			if (length > 0 && std::abs(direction[axis]/length) >= kAxisAlongView) { continue; }
			const auto tip = frame.project(offset(pivot,axis,frame.radius));
			if (!finite(tip) || squared(tip-centre) < 400) { continue; }
			const auto unit = (tip-centre)/std::sqrt(squared(tip-centre));
			LevelComponentGizmoHandle handle; handle.id = handle.axis = axis; handle.text = QString(QLatin1Char("XYZ"[axis]));
			handle.points << centre << tip; handle.label = tip+unit*16; result << handle;
		}
		LevelComponentGizmoHandle square; square.id = square.axis = 3; square.label = centre;
		for (const QPointF corner : {QPointF(-1,-1),QPointF(1,-1),QPointF(1,1),QPointF(-1,1),QPointF(-1,-1)}) { square.points << centre+corner*kTranslateSquare*scale; }
		result << square;
		LevelComponentGizmoHandle point; point.id = 4; point.axis = 3; point.label = centre; point.points << centre; point.text = tr("Pivot");
		result << point;
		return result;
	}
	if (m_tool == LevelComponentTool::Rotate) {
		for (int axis = 0; axis < 3; ++axis) {
			if(frame.rotationAxis>=0 && axis!=frame.rotationAxis) { continue; }
			LevelComponentGizmoHandle handle; handle.id = handle.axis = axis; handle.text = QString(QLatin1Char("XYZ"[axis]));
			for (int i = 0; i <= 96; ++i) {
				const double radians = 2*std::numbers::pi*i/96;
				handle.points << frame.project(offset(offset(pivot,(axis+1)%3,frame.radius*std::cos(radians)),(axis+2)%3,frame.radius*std::sin(radians)));
			}
			handle.label = handle.points[(axis*17+12)%96]; result << handle;
		}
	} else if (m_tool == LevelComponentTool::Shear) {
		for (int id = 0; id < 6; ++id) {
			const auto axes = shearAxes[id];
			const auto anchor = offset(pivot,axes[1],frame.radius);
			LevelComponentGizmoHandle handle; handle.id = id; handle.axis = axes[0];
			handle.label = frame.project(offset(anchor,axes[0],frame.radius*.4));
			handle.points << frame.project(anchor) << handle.label;
			handle.text = QStringLiteral("%1/%2").arg(QLatin1Char("XYZ"[axes[0]])).arg(QLatin1Char("XYZ"[axes[1]]));
			if (finite(handle.points[0]) && finite(handle.label) && squared(handle.label-handle.points[0]) >= 64) { result << handle; }
		}
	} else {
		for (int axis = 0; axis < 3; ++axis) {
			LevelComponentGizmoHandle handle; handle.id = handle.axis = axis; handle.text = QString(QLatin1Char("XYZ"[axis]));
			handle.label = frame.project(offset(pivot,axis,frame.radius)); handle.points << centre << handle.label;
			if (finite(handle.label) && squared(handle.label-centre) >= 100) { result << handle; }
		}
		LevelComponentGizmoHandle centreHandle; centreHandle.id = 3; centreHandle.axis = 3; centreHandle.label = centre;
		centreHandle.text = m_tool == LevelComponentTool::Scale ? tr("All") : tr("Pivot"); result << centreHandle;
	}
	return result;
}
int LevelComponentGizmo::hitTest(const QPointF& point,const LevelComponentGizmoFrame& frame,const QFont& font) const
{
	if (m_active || !finite(point)) { return -1; }
	if (m_objects && m_tool == LevelComponentTool::Transform) { return m_available ? transformHitTest(point,frame) : -1; }
	int result = -1; double nearest = 100;
	const double scale = handleScale(frame), tolerance = kObjectTolerance*scale, grab = kTranslatePivot*scale;
	if (m_tool == LevelComponentTool::Translate || (m_objects && m_tool == LevelComponentTool::Scale)) {
		// The pivot point first, as NetRadiant Custom tests its transform
		// origin before the manipulator, then the square, then the arrows.
		const auto all = handles(frame); if (all.size() < 2) { return -1; }
		const auto centre = all.last().label;
		if (squared(point-centre) <= grab*grab) { return 4; }
		if (std::abs(point.x()-centre.x()) <= kTranslateSquare*scale && std::abs(point.y()-centre.y()) <= kTranslateSquare*scale) { return 3; }
		for (const auto& handle : all) {
			if (handle.id > 2) { continue; }
			if (labelRect(handle.label,font,handle.text).contains(point)) { return handle.id; }
			const auto a = handle.points[0], line = handle.points[1]-a;
			const double t = std::clamp(QPointF::dotProduct(point-a,line)/squared(line),0.0,1.0);
			const double distance = squared(point-a-line*t);
			if (distance < tolerance*tolerance && distance < nearest) { nearest = distance; result = handle.id; }
		}
		return result;
	}
	if (m_objects && m_tool == LevelComponentTool::Rotate) {
		// The pivot point, then the nearest ring or its label, then the free
		// turn anywhere inside the rings.
		const auto all = handles(frame);
		QPointF centre(std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::quiet_NaN()); double sphere = 0;
		for (const auto& handle : all) {
			if (handle.id == 4) { centre = handle.label; }
			if (handle.id == 5 && !handle.points.isEmpty()) { sphere = std::sqrt(squared(handle.points.first()-handle.label)); }
		}
		if (!finite(centre)) { return -1; }
		if (squared(point-centre) <= grab*grab) { return 4; }
		nearest = tolerance*tolerance;
		for (const auto& handle : all) {
			if (handle.id > 3) { continue; }
			if (!handle.text.isEmpty() && finite(handle.label) && labelRect(handle.label,font,handle.text).contains(point)) { return handle.id; }
			for (int i = 1; i < handle.points.size(); ++i) {
				const auto a = handle.points[i-1], b = handle.points[i]; if (!finite(a) || !finite(b)) { continue; }
				const auto line = b-a; const double length = squared(line);
				const double t = length < .01 ? 0.0 : std::clamp(QPointF::dotProduct(point-a,line)/length,0.0,1.0);
				const double distance = squared(point-a-line*t);
				if (distance < nearest) { nearest = distance; result = handle.id; }
			}
		}
		if (result >= 0) { return result; }
		return sphere > 0 && squared(point-centre) < sphere*sphere ? 5 : -1;
	}
	for (const auto& handle : handles(frame)) {
		if (finite(handle.label) && labelRect(handle.label,font,handle.text).contains(point)) { return handle.id; }
		for (int i = 1; i < handle.points.size(); ++i) {
			const auto a = handle.points[i-1], b = handle.points[i]; if (!finite(a) || !finite(b)) { continue; }
			const auto line = b-a; if (squared(line) < .01) { continue; }
			const double t = std::clamp(QPointF::dotProduct(point-a,line)/squared(line),m_tool == LevelComponentTool::Rotate ? 0.0 : .3,1.0);
			const double distance = squared(point-a-line*t);
			if (distance < nearest) { nearest = distance; result = handle.id; }
		}
	}
	return result;
}
bool LevelComponentGizmo::begin(const QPointF& point,const LevelComponentGizmoFrame& frame,const QFont& font)
{
	if (m_active || !frame.ray || !frame.project || !finite(point)) { return false; }
	const int handle = hitTest(point,frame,font); if (handle < 0) { return false; }
	m_linearRotation = false; m_transformPivot = {};
	if (m_objects && m_tool == LevelComponentTool::Transform) {
		if (!beginTransform(point,frame,handle)) { m_transformPivot = {}; return false; }
	} else if (m_tool == LevelComponentTool::Translate || (m_objects && m_tool == LevelComponentTool::Scale)) {
		const auto constraint = handle < 3 ? ModelMoveConstraint(handle) : ModelMoveConstraint::ViewPlane;
		if (!beginModelMoveDrag(model(m_pivot),frame.viewDirection,constraint,frame.ray(point),0,&m_move)) { return false; }
		// The square's Shift leaves the axis nearest the line of sight alone.
		const auto sight = sightAxis(frame,m_pivot); m_scaleViewAxis = -1;
		for (int axis = 0; axis < 3 && sight.valid; ++axis) {
			if (m_scaleViewAxis < 0 || std::abs(coordinate(sight,axis)) > std::abs(coordinate(sight,m_scaleViewAxis))) { m_scaleViewAxis = axis; }
		}
	} else if (m_objects && m_tool == LevelComponentTool::Rotate && handle >= 3) {
		if (handle == 4) {
			if (!beginModelMoveDrag(model(m_pivot),frame.viewDirection,ModelMoveConstraint::ViewPlane,frame.ray(point),0,&m_move)) { return false; }
		} else if (handle == 3) {
			// The outer ring turns about the line of sight through the pivot.
			m_screenAxis = sightAxis(frame,m_pivot);
			m_screenPrevious = m_screenAxis.valid ? planeVector(frame.ray(point),m_pivot,m_screenAxis) : LevelMapVec3{};
			m_screenAngle = 0;
			if (!m_screenPrevious.valid) { return false; }
		} else {
			// The free turn follows the point under the pointer on the sphere.
			const auto ray = frame.ray(point);
			m_sphereStart = levelObjectSpherePoint(mapVector(ray.origin),mapVector(ray.direction),m_pivot,frame.radius);
			if (!m_sphereStart.valid) { return false; }
		}
	} else if (m_tool == LevelComponentTool::Rotate) {
		const double direction[]{frame.viewDirection.x,frame.viewDirection.y,frame.viewDirection.z};
		m_linearRotation = std::abs(direction[handle]) < .15 || !beginModelRotateDrag(model(m_pivot),handle,frame.ray(point),0,&m_rotate);
		if (m_linearRotation) {
			// By id: the object tool leaves a face-on ring out of the list.
			QPolygonF ring; for (const auto& candidate : handles(frame)) { if (candidate.id == handle) { ring = candidate.points; } }
			if (ring.size() < 96) { return false; }
			double closest = std::numeric_limits<double>::infinity(); m_tangent = {};
			for (int i = 0; i < 96; ++i) {
				const auto tangent = (ring[(i+1)%96]-ring[(i+95)%96])/(4*std::numbers::pi/96);
				if (!finite(ring[i]) || !finite(tangent) || squared(tangent) < 1) { continue; }
				const double distance = squared(ring[i]-point); if (distance < closest) { closest = distance; m_tangent = tangent; }
			}
			if (squared(m_tangent) < 1) { return false; }
		}
	} else if (m_tool != LevelComponentTool::Scale || handle < 3) {
		const int axis = m_tool == LevelComponentTool::Shear ? shearAxes[handle][0] : handle;
		if (!beginModelMoveDrag(model(m_pivot),frame.viewDirection,ModelMoveConstraint(axis),frame.ray(point),m_tool == LevelComponentTool::Pivot ? m_grid : 0,&m_move)) { return false; }
	}
	m_frame = frame; m_handle = handle; m_press = point; ++m_serial;
	m_request = {}; m_request.pivot = m_pivot;
	m_request.operation = m_tool == LevelComponentTool::Rotate ? LevelComponentTransformKind::Rotate : m_tool == LevelComponentTool::Scale ? LevelComponentTransformKind::Scale
		: m_tool == LevelComponentTool::Shear ? LevelComponentTransformKind::Shear : LevelComponentTransformKind::Move;
	m_request.axis = handle; if (m_tool == LevelComponentTool::Shear) { m_request.axis = shearAxes[handle][0]; m_request.along = shearAxes[handle][1]; }
	m_rotationAxis = {};
	if (m_objects && m_tool == LevelComponentTool::Scale && handle == 4) { m_request.operation = LevelComponentTransformKind::Move; }
	if (m_objects && m_tool == LevelComponentTool::Rotate) {
		// The pivot moves; the outer ring and the free turn are any-axis turns.
		if (handle == 4) { m_request.operation = LevelComponentTransformKind::Move; }
		else if (handle >= 3) { m_request.operation = LevelComponentTransformKind::Affine; m_request.axis = 2; }
		if (handle < 3) { m_rotationAxis = {0,0,0,true}; setCoordinate(m_rotationAxis,handle,1); }
		else if (handle == 3) { m_rotationAxis = m_screenAxis; }
	}
	if (m_objects && m_tool == LevelComponentTool::Transform) {
		// Each kind about the drag's own pivot: a skew is a shear, a corner a
		// turn about its world axis, a side or corner outside a scale.
		const auto kind = transformKindOf(handle);
		m_request.pivot = pivot(); m_request.axis = 2;
		if (kind == LevelObjectTransformKind::Skew) {
			const auto line = skewLine(handle);
			m_request.operation = LevelComponentTransformKind::Shear; m_request.axis = line.slide; m_request.along = line.by;
		} else if (kind == LevelObjectTransformKind::Rotate) {
			const auto corner = turnPoint(handle);
			m_request.operation = LevelComponentTransformKind::Rotate; m_request.axis = corner.axis;
			m_rotationAxis = {0,0,0,true}; setCoordinate(m_rotationAxis,corner.axis,1);
		} else if (kind == LevelObjectTransformKind::Scale) {
			m_request.operation = LevelComponentTransformKind::Scale;
		}
	}
	m_last = point; m_modifiers = Qt::NoModifier; m_objectEdges.clear();
	m_active = m_valid = true; m_pending = m_releasePending = m_travelled = false; m_edges.clear(); m_error.clear();
	Q_EMIT activeChanged(true); Q_EMIT changed(); return m_active;
}
void LevelComponentGizmo::refuse(const QString& error)
{
	++m_serial; m_valid = m_pending = false; m_edges.clear(); m_error = error; Q_EMIT changed();
}
bool LevelComponentGizmo::updateModifiers(Qt::KeyboardModifiers modifiers)
{
	if (!m_active || m_releasePending || !m_objects) { return false; }
	return updateDrag(m_last,modifiers);
}
bool LevelComponentGizmo::updateDrag(const QPointF& point,Qt::KeyboardModifiers modifiers)
{
	if (!m_active || m_releasePending) { return false; }
	if (!finite(point)) { refuse(tr("Cannot reach the manipulator here.")); return false; }
	m_last = point; m_modifiers = modifiers;
	if (!m_travelled && squared(point-m_press) < std::pow(QApplication::startDragDistance(),2)) { return true; }
	m_travelled = true; auto next = m_request; bool valid = true;
	auto turnAxis = m_rotationAxis;
	const bool shift = modifiers.testFlag(Qt::ShiftModifier), ctrl = modifiers.testFlag(Qt::ControlModifier);
	if (objectPivotHandle()) {
		// NetRadiant Custom's pivot drag: Shift keeps the main axis; a moved
		// axis lands on the bounds' minimum, centre or maximum or on the grid,
		// whichever is nearest. Snapping off moves freely.
		ModelVec3 delta; valid = modelMoveDragDelta(m_move,m_frame.ray(point),&delta);
		LevelMapVec3 moved{delta.x,delta.y,delta.z,true};
		if (shift) { moved = levelObjectDominantAxis(moved); }
		int axes = 0;
		for (int axis = 0; axis < 3; ++axis) { if (std::abs(coordinate(moved,axis)) >= 1e-3) { axes |= 1 << axis; } }
		const double grid = m_grid > 0 ? m_grid : ctrl ? m_boundsGrid : 0;
		const auto wanted = plus(m_pivot,moved);
		const auto placed = grid > 0 || ctrl ? snapLevelObjectPivot(wanted,m_pivot,axes,m_mins,m_maxs,grid) : wanted;
		next.delta = minus(placed,m_pivot); valid = valid && finite(placed);
	} else if (m_objects && m_tool == LevelComponentTool::Transform) {
		const auto kind = transformKindOf(m_handle); const auto ray = m_frame.ray(point);
		if (kind == LevelObjectTransformKind::Move) {
			// NetRadiant Custom's free move: level in the camera, where Alt
			// lifts it instead; Shift keeps the main axis and Ctrl lands the
			// bounds on the grid, as the Translate tool's do.
			const bool lift = modifiers.testFlag(Qt::AltModifier) && m_moveVerticalValid;
			ModelVec3 delta; valid = modelMoveDragDelta(lift ? m_moveVertical : m_move,ray,&delta);
			LevelMapVec3 moved{delta.x,delta.y,delta.z,true};
			if (lift) { moved.x = moved.y = 0; }
			if (shift) { moved = levelObjectDominantAxis(moved); }
			moved = ctrl ? snapLevelObjectTranslation(moved,m_mins,m_maxs,m_boundsGrid > 0 ? m_boundsGrid : m_grid,LevelObjectTranslateSnap::Bounds)
				: snapLevelObjectTranslation(moved,m_mins,m_maxs,m_grid,LevelObjectTranslateSnap::Grid);
			next.delta = moved; valid = valid && finite(plus(m_pivot,moved));
		} else if (kind == LevelObjectTransformKind::Skew) {
			// The side slides along itself by the grid-snapped travel; the
			// plane through the pivot across it stays.
			const auto line = skewLine(m_handle);
			ModelVec3 delta; valid = modelMoveDragDelta(m_move,ray,&delta);
			const double travel = snapped(coordinate(mapVector(delta),line.slide),m_grid);
			const double reach = coordinate(line.side > 0 ? m_maxs : m_mins,line.by)-coordinate(m_transformPivot,line.by);
			next.amount = travel/reach;
			if (valid && std::abs(next.amount) > 16) {
				refuse(tr("The slide is more than 16 units for each unit across the box. Slant it in smaller steps.")); return false;
			}
		} else if (kind == LevelObjectTransformKind::Rotate) {
			// About the corner's axis, by the rotation step while snapping is
			// on; Shift snaps to 15 degrees.
			double angle = 0;
			if (m_linearRotation) { angle = QPointF::dotProduct(point-m_press,m_tangent)/squared(m_tangent)*180/std::numbers::pi; }
			else { valid = modelRotateDragAngle(&m_rotate,ray,&angle); }
			next.amount = snapLevelObjectAngle(angle,shift ? 15.0 : m_angleStep);
		} else if (kind == LevelObjectTransformKind::Scale) {
			// The dragged sides follow the pointer about the pivot; Shift keeps
			// proportions and Ctrl lands the size on the grid.
			ModelVec3 delta; valid = modelMoveDragDelta(m_move,ray,&delta);
			LevelObjectSideScaleDrag drag;
			drag.sides = m_transformSides; drag.delta = mapVector(delta); drag.mins = m_mins; drag.maxs = m_maxs; drag.pivot = m_transformPivot;
			drag.grid = m_grid; drag.sizeGrid = ctrl ? (m_boundsGrid > 0 ? m_boundsGrid : m_grid) : 0; drag.equal = shift;
			const auto factors = levelObjectSideScaleFactors(drag);
			next.scale = {factors[0],factors[1],factors[2],true};
		}
	} else if (m_tool == LevelComponentTool::Translate) {
		ModelVec3 delta; valid = modelMoveDragDelta(m_move,m_frame.ray(point),&delta);
		LevelMapVec3 moved{delta.x,delta.y,delta.z,true};
		if (m_handle == 3 && shift) { moved = levelObjectDominantAxis(moved); }
		// Ctrl lands the bounds on the grid; otherwise the delta snaps.
		moved = ctrl ? snapLevelObjectTranslation(moved,m_mins,m_maxs,m_boundsGrid > 0 ? m_boundsGrid : m_grid,LevelObjectTranslateSnap::Bounds)
			: snapLevelObjectTranslation(moved,m_mins,m_maxs,m_grid,LevelObjectTranslateSnap::Grid);
		next.delta = moved; valid = valid && finite(plus(m_pivot,moved));
	} else if (m_objects && m_tool == LevelComponentTool::Scale) {
		// NetRadiant Custom's Scale: the travel along the axis, or across the
		// view, moves the bounds' farther side; Shift scales the axes equally
		// and Ctrl lands the size on the grid (core/level_object_manipulator.h).
		ModelVec3 delta; valid = modelMoveDragDelta(m_move,m_frame.ray(point),&delta);
		LevelObjectScaleDrag drag;
		drag.handle = m_handle; drag.delta = {delta.x,delta.y,delta.z,true};
		drag.mins = m_mins; drag.maxs = m_maxs; drag.pivot = m_pivot; drag.viewAxis = m_scaleViewAxis;
		drag.grid = m_grid; drag.sizeGrid = ctrl ? (m_boundsGrid > 0 ? m_boundsGrid : m_grid) : 0; drag.equal = shift;
		const auto factors = levelObjectScaleFactors(drag);
		next.scale = {factors[0],factors[1],factors[2],true};
	} else if (m_objects && m_tool == LevelComponentTool::Rotate) {
		// NetRadiant Custom snaps any turn to 15 degrees with Shift. The rings
		// also follow the rotation step while snapping is on (m_angleStep is 0
		// otherwise), as TrenchBroom's do; the free turn is free.
		const double step = shift ? 15.0 : m_handle == 5 ? 0.0 : m_angleStep;
		double angle = 0;
		if (m_handle <= 2) {
			if (m_linearRotation) { angle = QPointF::dotProduct(point-m_press,m_tangent)/squared(m_tangent)*180/std::numbers::pi; }
			else { valid = modelRotateDragAngle(&m_rotate,m_frame.ray(point),&angle); }
			next.amount = snapLevelObjectAngle(angle,step);
		} else if (m_handle == 3) {
			// The turn so far, summed step by step so it can pass half a turn.
			const auto current = planeVector(m_frame.ray(point),m_pivot,m_screenAxis);
			valid = current.valid;
			if (valid) {
				m_screenAngle += std::atan2(dot(m_screenAxis,cross(m_screenPrevious,current)),dot(m_screenPrevious,current))*180/std::numbers::pi;
				m_screenPrevious = current;
			}
			next.amount = snapLevelObjectAngle(m_screenAngle,step);
			next.linear = levelObjectRotationColumns(m_screenAxis,next.amount);
			turnAxis = m_screenAxis;
		} else {
			const auto ray = m_frame.ray(point);
			const auto current = levelObjectSpherePoint(mapVector(ray.origin),mapVector(ray.direction),m_pivot,m_frame.radius);
			LevelMapVec3 axis; double degrees = 0;
			valid = current.valid;
			if (valid && levelObjectArcball(m_sphereStart,current,&axis,&degrees)) { turnAxis = axis; } else { degrees = 0; }
			next.amount = turnAxis.valid ? snapLevelObjectAngle(degrees,step) : 0;
			next.linear = levelObjectRotationColumns(turnAxis.valid ? turnAxis : LevelMapVec3{0,0,1,true},next.amount);
		}
	} else if (m_tool == LevelComponentTool::Rotate) {
		double angle = 0;
		if (m_linearRotation) { angle = QPointF::dotProduct(point-m_press,m_tangent)/squared(m_tangent)*180/std::numbers::pi; }
		else { valid = modelRotateDragAngle(&m_rotate,m_frame.ray(point),&angle); }
		next.amount = snapped(angle,m_angleStep);
	} else if (m_tool == LevelComponentTool::Scale && m_handle == 3) {
		const auto travel = point-m_press; const double factor = 1+snapped(std::exp2((travel.x()-travel.y())/96.0)-1,m_scaleStep);
		next.scale = {factor,factor,factor,true};
	} else {
		ModelVec3 delta; valid = modelMoveDragDelta(m_move,m_frame.ray(point),&delta);
		const LevelMapVec3 moved{delta.x,delta.y,delta.z,true};
		if (m_tool == LevelComponentTool::Pivot) { next.delta = moved; valid = valid && finite({m_pivot.x+delta.x,m_pivot.y+delta.y,m_pivot.z+delta.z,true}); }
		else if (m_tool == LevelComponentTool::Scale) { setCoordinate(next.scale,m_handle,1+snapped(coordinate(moved,m_handle)/m_frame.radius,m_scaleStep)); }
		else { next.amount = snapped(coordinate(moved,next.axis)/m_frame.radius,m_shearStep); }
	}
	LevelTextureAffine matrix; QString error;
	if (!valid || !levelComponentTransformMatrix(next,m_pivot,&matrix,&error)) { refuse(error.isEmpty() ? tr("Cannot reach the manipulator plane here.") : error); return false; }
	if (same(next.delta,m_request.delta) && same(next.scale,m_request.scale) && next.amount == m_request.amount && sameColumns(next.linear,m_request.linear)
		&& m_valid) { return true; }
	m_request = next; m_rotationAxis = turnAxis; ++m_serial; m_valid = true; m_error.clear(); m_edges.clear();
	// A translation keeps every brush valid, and whole objects turn rigidly;
	// only the component tools need the owner's checked geometry before they
	// can be applied.
	m_pending = !m_objects && m_tool != LevelComponentTool::Pivot && m_tool != LevelComponentTool::Translate;
	Q_EMIT changed(); if (m_pending) { Q_EMIT previewRequested(m_serial); } return true;
}
void LevelComponentGizmo::setPreview(quint64 serial,const QVector<QPair<LevelMapVec3,LevelMapVec3>>& edges,const QString& error)
{
	if (!m_active || !m_pending || serial != m_serial) { return; }
	m_pending = false; m_valid = error.isEmpty(); m_error = error; m_edges = m_valid ? edges : QVector<QPair<LevelMapVec3,LevelMapVec3>>{};
	Q_EMIT changed(); if (m_releasePending) { finish(true); }
}
void LevelComponentGizmo::finish(bool commit)
{
	if (!m_active) { return; }
	if (commit && m_pending) { m_releasePending = true; Q_EMIT changed(); return; }
	const bool altered = m_request.delta.x != 0 || m_request.delta.y != 0 || m_request.delta.z != 0 || m_request.amount != 0
		|| m_request.scale.x != 1 || m_request.scale.y != 1 || m_request.scale.z != 1;
	const bool apply = commit && m_valid && m_travelled && altered; const auto serial = m_serial;
	const auto pivot = transformed(m_pivot);
	const bool pivotHandle = m_tool == LevelComponentTool::Pivot || objectPivotHandle();
	const bool toggle = commit && !m_travelled && objectPivotHandle();
	m_active = m_pending = m_releasePending = false; m_edges.clear(); m_objectEdges.clear();
	// A Transform drag's kind and pivot stay readable while its commit is asked for.
	m_finishing = true;
	if (apply) { if (pivotHandle) { Q_EMIT pivotChanged(pivot); } else { Q_EMIT commitRequested(serial); } }
	else if (toggle) { Q_EMIT pivotToggleRequested(); }
	m_finishing = false; m_transformPivot = {};
	++m_serial; Q_EMIT activeChanged(false); Q_EMIT changed();
}
LevelMapVec3 LevelComponentGizmo::transformed(const LevelMapVec3& point) const
{
	LevelTextureAffine matrix;
	if (!m_active || !levelComponentTransformMatrix(m_request,m_pivot,&matrix)) { return point; }
	return transformedLevelComponentPoint(point,matrix);
}
QString LevelComponentGizmo::summary() const
{
	if (!m_available || m_tool == LevelComponentTool::Move) { return {}; }
	if (m_tool == LevelComponentTool::Translate) {
		if (!m_valid && m_active) { return tr("Selection unchanged: %1").arg(m_error); }
		if (m_active && m_handle == 4) {
			const auto p = transformed(m_pivot);
			return tr("Pivot: %1, %2, %3").arg(p.x,0,'g',8).arg(p.y,0,'g',8).arg(p.z,0,'g',8) + QStringLiteral(" · ") + tr("Release to set the pivot; Escape cancels");
		}
		if (m_active) {
			const auto d = translation();
			return tr("Translate: %1, %2, %3").arg(d.x,0,'g',8).arg(d.y,0,'g',8).arg(d.z,0,'g',8) + QStringLiteral(" · ") + tr("Release to apply; Escape cancels");
		}
		const QString pivot = (m_customPivot ? tr("Placed pivot: %1, %2, %3") : tr("Pivot at the selection centre: %1, %2, %3"))
			.arg(m_pivot.x,0,'g',8).arg(m_pivot.y,0,'g',8).arg(m_pivot.z,0,'g',8);
		return tr("Translate") + QStringLiteral(" · ") + pivot + QStringLiteral(" · ")
			+ tr("Drag X, Y or Z along an axis, the square across the view, or the pivot point to place the pivot. Shift keeps the main axis; Ctrl lands the bounds on the grid.");
	}
	if (m_objects && m_tool == LevelComponentTool::Rotate) {
		if (!m_valid && m_active) { return tr("Selection unchanged: %1").arg(m_error); }
		if (m_active && m_handle == 4) {
			const auto p = transformed(m_pivot);
			return tr("Pivot: %1, %2, %3").arg(p.x,0,'g',8).arg(p.y,0,'g',8).arg(p.z,0,'g',8) + QStringLiteral(" · ") + tr("Release to set the pivot; Escape cancels");
		}
		if (m_active) {
			const double degrees = m_travelled ? m_request.amount : 0;
			const QString turn = m_handle <= 2 ? tr("Rotate about %1: %2°").arg(QLatin1Char("XYZ"[std::clamp(m_handle,0,2)])).arg(degrees,0,'g',6)
				: m_handle == 3 ? tr("Rotate in the view: %1°").arg(degrees,0,'g',6)
				: m_rotationAxis.valid ? tr("Rotate freely: %1° about %2, %3, %4").arg(degrees,0,'g',6).arg(m_rotationAxis.x,0,'f',3).arg(m_rotationAxis.y,0,'f',3).arg(m_rotationAxis.z,0,'f',3)
				: tr("Rotate freely: %1°").arg(degrees,0,'g',6);
			return turn + QStringLiteral(" · ") + tr("Release to apply; Escape cancels");
		}
		const QString pivot = (m_customPivot ? tr("Placed pivot: %1, %2, %3") : tr("Pivot at the selection centre: %1, %2, %3"))
			.arg(m_pivot.x,0,'g',8).arg(m_pivot.y,0,'g',8).arg(m_pivot.z,0,'g',8);
		return tr("Rotate") + QStringLiteral(" · ") + pivot + QStringLiteral(" · ")
			+ (m_freeTurn ? tr("Drag X, Y or Z to turn about that axis, the outer ring to turn in the view, inside the rings to turn freely, or the pivot point to place the pivot. Shift snaps the angle to 15°.")
				: tr("Drag X, Y or Z to turn about that axis, the outer ring to turn in the view, or the pivot point to place the pivot. Shift snaps the angle to 15°."));
	}
	if (m_objects && m_tool == LevelComponentTool::Scale) {
		if (!m_valid && m_active) { return tr("Selection unchanged: %1").arg(m_error); }
		if (m_active && m_handle == 4) {
			const auto p = transformed(m_pivot);
			return tr("Pivot: %1, %2, %3").arg(p.x,0,'g',8).arg(p.y,0,'g',8).arg(p.z,0,'g',8) + QStringLiteral(" · ") + tr("Release to set the pivot; Escape cancels");
		}
		if (m_active) {
			const auto f = scaleFactors();
			return tr("Scale: %1 × %2 × %3, to %4 × %5 × %6 units").arg(f.x,0,'g',6).arg(f.y,0,'g',6).arg(f.z,0,'g',6)
				.arg((m_maxs.x-m_mins.x)*f.x,0,'g',8).arg((m_maxs.y-m_mins.y)*f.y,0,'g',8).arg((m_maxs.z-m_mins.z)*f.z,0,'g',8)
				+ QStringLiteral(" · ") + tr("Release to apply; Escape cancels");
		}
		const QString pivot = (m_customPivot ? tr("Placed pivot: %1, %2, %3") : tr("Pivot at the selection centre: %1, %2, %3"))
			.arg(m_pivot.x,0,'g',8).arg(m_pivot.y,0,'g',8).arg(m_pivot.z,0,'g',8);
		return tr("Scale") + QStringLiteral(" · ") + pivot + QStringLiteral(" · ")
			+ tr("Drag X, Y or Z to scale along that axis, the square to scale across the view, or the pivot point to place the pivot. Shift scales the axes equally; Ctrl lands the size on the grid.");
	}
	if (m_objects && m_tool == LevelComponentTool::Transform) {
		if (!m_valid && m_active) { return tr("Selection unchanged: %1").arg(m_error); }
		if (m_active && m_handle == 4) {
			const auto p = transformed(m_pivot);
			return tr("Pivot: %1, %2, %3").arg(p.x,0,'g',8).arg(p.y,0,'g',8).arg(p.z,0,'g',8) + QStringLiteral(" · ") + tr("Release to set the pivot; Escape cancels");
		}
		if (m_active) {
			const auto kind = transformKindOf(m_handle);
			QString what;
			if (kind == LevelObjectTransformKind::Move) {
				const auto d = translation();
				what = tr("Move: %1, %2, %3").arg(d.x,0,'g',8).arg(d.y,0,'g',8).arg(d.z,0,'g',8);
			} else if (kind == LevelObjectTransformKind::Skew) {
				const auto line = skewLine(m_handle);
				what = tr("Skew along %1 by %2 for each unit of %3").arg(QLatin1Char("XYZ"[std::clamp(line.slide,0,2)]))
					.arg(m_travelled ? m_request.amount : 0,0,'g',6).arg(QLatin1Char("XYZ"[std::clamp(line.by,0,2)]));
			} else if (kind == LevelObjectTransformKind::Rotate) {
				what = tr("Rotate about %1: %2°").arg(QLatin1Char("XYZ"[std::clamp(m_request.axis,0,2)])).arg(m_travelled ? m_request.amount : 0,0,'g',6);
			} else {
				const auto f = scaleFactors();
				what = tr("Scale: %1 × %2 × %3, to %4 × %5 × %6 units").arg(f.x,0,'g',6).arg(f.y,0,'g',6).arg(f.z,0,'g',6)
					.arg((m_maxs.x-m_mins.x)*f.x,0,'g',8).arg((m_maxs.y-m_mins.y)*f.y,0,'g',8).arg((m_maxs.z-m_mins.z)*f.z,0,'g',8);
			}
			const auto p = pivot();
			return what + QStringLiteral(" · ") + tr("about %1, %2, %3").arg(p.x,0,'g',8).arg(p.y,0,'g',8).arg(p.z,0,'g',8)
				+ QStringLiteral(" · ") + tr("Release to apply; Escape cancels");
		}
		const QString pivot = (m_customPivot ? tr("Placed pivot: %1, %2, %3") : tr("Pivot at the selection centre: %1, %2, %3"))
			.arg(m_pivot.x,0,'g',8).arg(m_pivot.y,0,'g',8).arg(m_pivot.z,0,'g',8);
		return tr("Transform") + QStringLiteral(" · ") + pivot + QStringLiteral(" · ")
			+ tr("Drag a side to skew, a corner to turn, inside the box to move, just outside a side or corner to scale, or the pivot point "
				 "to place the pivot. Without a placed pivot each works about the opposite side or corner.");
	}
	if (!m_valid && m_active) { return tr("Components unchanged: %1").arg(m_error); }
	QString operation;
	if (m_tool == LevelComponentTool::Rotate) { operation = tr("Rotate %1: %2°").arg(QLatin1Char("XYZ"[std::clamp(m_request.axis,0,2)])).arg(m_active ? m_request.amount : 0,0,'g',6); }
	else if (m_tool == LevelComponentTool::Scale) { operation = tr("Scale: %1, %2, %3").arg(m_active ? m_request.scale.x : 1,0,'g',6).arg(m_active ? m_request.scale.y : 1,0,'g',6).arg(m_active ? m_request.scale.z : 1,0,'g',6); }
	else if (m_tool == LevelComponentTool::Shear) { operation = tr("Shear: %1").arg(m_active ? m_request.amount : 0,0,'g',6); }
	else { const auto p = transformed(m_pivot); operation = tr("Pivot: %1, %2, %3").arg(p.x,0,'g',8).arg(p.y,0,'g',8).arg(p.z,0,'g',8); }
	if (!m_active) { return operation + QStringLiteral(" · ") + tr("Drag a labelled handle; Transform Components provides numeric editing."); }
	return operation + QStringLiteral(" · ") + (m_pending ? tr("Checking geometry… Escape cancels") : m_tool == LevelComponentTool::Pivot ? tr("Release to set the pivot; Escape cancels") : tr("Valid preview · Release to apply"));
}
void LevelComponentGizmo::paintTranslate(QPainter& painter,const LevelComponentGizmoFrame& frame,bool highContrast) const
{
	const auto all = handles(frame); if (all.isEmpty()) { return; }
	painter.save(); painter.setRenderHint(QPainter::Antialiasing,true);
	const QColor selected = highContrast ? QColor(Qt::yellow) : QColor(255,190,65);
	const std::array<QColor,3> colours{QColor(255,110,110),QColor(120,255,150),QColor(140,180,255)};
	const QColor screen = highContrast ? QColor(Qt::white) : QColor(120,220,255);
	const double scale = handleScale(frame);
	// The move so far, from where the drag began: a dashed trace, as the
	// component moves draw theirs.
	if (m_active && m_travelled) {
		const auto from = frame.project(m_pivot), to = frame.project(transformed(m_pivot));
		if (finite(from) && finite(to)) { painter.setPen(QPen(selected,highContrast ? 2.5 : 1.5,Qt::DashLine)); painter.drawLine(from,to); }
	}
	for (const auto& handle : all) {
		const bool hot = m_active && handle.id == m_handle;
		if (handle.id <= 2) {
			const QColor colour = hot ? selected : colours[static_cast<size_t>(handle.axis)];
			const auto a = handle.points[0], b = handle.points[1];
			painter.setPen(QPen(Qt::black,5)); painter.drawLine(a,b);
			painter.setPen(QPen(colour,highContrast ? 3 : 2)); painter.drawLine(a,b);
			const auto unit = (b-a)/std::sqrt(squared(b-a)); const QPointF side(-unit.y(),unit.x());
			const QPolygonF head{b+unit*9*scale,b-unit*3*scale+side*5*scale,b-unit*3*scale-side*5*scale};
			painter.setPen(QPen(Qt::black,1.5)); painter.setBrush(colour); painter.drawPolygon(head);
			const auto rect = labelRect(handle.label,painter.font(),handle.text); painter.setBrush(Qt::black); painter.setPen(QPen(colour,1.5));
			painter.drawRect(rect); painter.drawText(rect,Qt::AlignCenter|Qt::TextForceLeftToRight,handle.text);
		} else if (handle.id == 3) {
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(Qt::black,5)); painter.drawPolyline(handle.points);
			painter.setPen(QPen(hot ? selected : screen,highContrast ? 3 : 2)); painter.drawPolyline(handle.points);
		} else {
			paintPivotPoint(painter,handle.label,hot,m_customPivot,highContrast,scale,selected);
		}
	}
	painter.restore();
}
void LevelComponentGizmo::paintObjectRotate(QPainter& painter,const LevelComponentGizmoFrame& frame,bool highContrast) const
{
	const auto all = handles(frame); if (all.isEmpty()) { return; }
	painter.save(); painter.setRenderHint(QPainter::Antialiasing,true);
	const QColor selected = highContrast ? QColor(Qt::yellow) : QColor(255,190,65);
	const std::array<QColor,3> colours{QColor(255,110,110),QColor(120,255,150),QColor(140,180,255)};
	const QColor screen = highContrast ? QColor(Qt::white) : QColor(120,220,255);
	const double scale = handleScale(frame);
	paintObjectWire(painter,frame,highContrast);
	const auto polyline = [&painter](const QPolygonF& points,const QPen& pen) {
		painter.setPen(pen);
		for (int i = 1; i < points.size(); ++i) { if (finite(points[i-1]) && finite(points[i])) { painter.drawLine(points[i-1],points[i]); } }
	};
	painter.setBrush(Qt::NoBrush);
	for (const auto& handle : all) {
		const bool hot = m_active && handle.id == m_handle;
		if (handle.id == 5) {
			// The free turn's sphere: a quiet outline, brighter while it turns.
			QColor outline = hot ? selected : screen; outline.setAlpha(hot || highContrast ? 255 : 110);
			polyline(handle.points,QPen(outline,highContrast ? 1.5 : 1,hot ? Qt::SolidLine : Qt::DotLine));
		} else if (handle.id <= 3) {
			const QColor colour = hot ? selected : handle.id == 3 ? screen : colours[static_cast<size_t>(handle.axis)];
			polyline(handle.points,QPen(Qt::black,5));
			polyline(handle.points,QPen(colour,highContrast ? 3 : 2));
			if (!handle.text.isEmpty() && finite(handle.label)) {
				const auto rect = labelRect(handle.label,painter.font(),handle.text); painter.setBrush(Qt::black); painter.setPen(QPen(colour,1.5));
				painter.drawRect(rect); painter.drawText(rect,Qt::AlignCenter|Qt::TextForceLeftToRight,handle.text); painter.setBrush(Qt::NoBrush);
			}
		}
	}
	// The pivot on top, and while turning a spoke from it to the pointer.
	for (const auto& handle : all) {
		if (handle.id != 4) { continue; }
		if (m_active && m_travelled && !objectPivotHandle()) { painter.setPen(QPen(selected,highContrast ? 2.0 : 1.2,Qt::DashLine)); painter.drawLine(handle.label,m_last); }
		paintPivotPoint(painter,handle.label,m_active && m_handle == 4,m_customPivot,highContrast,scale,selected);
	}
	painter.restore();
}
void LevelComponentGizmo::paintObjectWire(QPainter& painter,const LevelComponentGizmoFrame& frame,bool highContrast) const
{
	// The selection where the turn or scale would take it, as a dashed wire.
	LevelTextureAffine matrix;
	if (!m_active || !m_travelled || !m_valid || objectPivotHandle() || m_objectEdges.isEmpty() || !levelComponentTransformMatrix(m_request,m_pivot,&matrix)) { return; }
	painter.save();
	painter.setPen(QPen(highContrast ? QColor(Qt::yellow) : QColor(255,190,65),highContrast ? 2.0 : 1.2,Qt::DashLine)); painter.setBrush(Qt::NoBrush);
	for (const auto& edge : m_objectEdges) {
		const auto a = frame.project(transformedLevelComponentPoint(edge.first,matrix)), b = frame.project(transformedLevelComponentPoint(edge.second,matrix));
		if (finite(a) && finite(b)) { painter.drawLine(a,b); }
	}
	painter.restore();
}
void LevelComponentGizmo::paintObjectScale(QPainter& painter,const LevelComponentGizmoFrame& frame,bool highContrast) const
{
	const auto all = handles(frame); if (all.isEmpty()) { return; }
	painter.save(); painter.setRenderHint(QPainter::Antialiasing,true);
	paintObjectWire(painter,frame,highContrast);
	const QColor selected = highContrast ? QColor(Qt::yellow) : QColor(255,190,65);
	const std::array<QColor,3> colours{QColor(255,110,110),QColor(120,255,150),QColor(140,180,255)};
	const QColor screen = highContrast ? QColor(Qt::white) : QColor(120,220,255);
	const double scale = handleScale(frame);
	for (const auto& handle : all) {
		const bool hot = m_active && handle.id == m_handle;
		if (handle.id <= 2) {
			// A line ending in a filled square, unlike Translate's arrowheads.
			const QColor colour = hot ? selected : colours[static_cast<size_t>(handle.axis)];
			const auto a = handle.points[0], b = handle.points[1];
			painter.setPen(QPen(Qt::black,5)); painter.drawLine(a,b);
			painter.setPen(QPen(colour,highContrast ? 3 : 2)); painter.drawLine(a,b);
			painter.setPen(QPen(Qt::black,1.5)); painter.setBrush(colour);
			painter.drawRect(QRectF(b-QPointF(4.5,4.5)*scale,QSizeF(9,9)*scale));
			const auto rect = labelRect(handle.label,painter.font(),handle.text); painter.setBrush(Qt::black); painter.setPen(QPen(colour,1.5));
			painter.drawRect(rect); painter.drawText(rect,Qt::AlignCenter|Qt::TextForceLeftToRight,handle.text);
		} else if (handle.id == 3) {
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(Qt::black,5)); painter.drawPolyline(handle.points);
			painter.setPen(QPen(hot ? selected : screen,highContrast ? 3 : 2,Qt::DashLine)); painter.drawPolyline(handle.points);
		} else {
			paintPivotPoint(painter,handle.label,hot,m_customPivot,highContrast,scale,selected);
		}
	}
	painter.restore();
}
QVector<LevelComponentGizmoHandle> LevelComponentGizmo::transformHandles(const LevelComponentGizmoFrame& frame) const
{
	QVector<LevelComponentGizmoHandle> result;
	if (!finite(m_mins) || !finite(m_maxs) || !frame.project) { return result; }
	// A move carries the box with it.
	const auto shift = transformKind() == LevelObjectTransformKind::Move && m_valid ? m_request.delta : LevelMapVec3{0,0,0,true};
	const auto low = plus(m_mins,shift), high = plus(m_maxs,shift);
	const auto view = unit({frame.viewDirection.x,frame.viewDirection.y,frame.viewDirection.z,true});
	const bool parallel = !frame.perspective && view.valid;
	const auto alongView = [&](int axis) { return parallel && std::abs(coordinate(view,axis)) >= kAxisAlongView; };
	// A view straight along an axis, as a plan view is; an oblique parallel
	// camera shows every handle, as a perspective one does.
	const bool plan = alongView(0) || alongView(1) || alongView(2);
	const auto extent = [&](int axis) { return coordinate(high,axis)-coordinate(low,axis); };
	// On a box this small on screen only the pivot, the move inside and the
	// scale outside remain.
	const auto targets = transformTargets(low,high,frame);
	const bool roomy = targets.shown && targets.size >= kTransformSmallBox;
	// The side lines run through the middle of the box, as NetRadiant
	// Custom's do, so a plan view shows the ones across its line of sight as
	// the box's outline and leaves out the rest.
	for (int slide = 0; slide < 3 && roomy; ++slide) {
		for (int j = 0; j < 2; ++j) {
			const int by = (slide+j+1)%3;
			if (alongView(slide) || alongView(by) || extent(slide) <= 1e-6 || extent(by) <= 1e-6) { continue; }
			for (int k = 0; k < 2; ++k) {
				std::array<int,3> from{0,0,0}, to{0,0,0};
				from[static_cast<size_t>(slide)] = -1; to[static_cast<size_t>(slide)] = 1;
				from[static_cast<size_t>(by)] = to[static_cast<size_t>(by)] = k ? 1 : -1;
				LevelComponentGizmoHandle line; line.id = kTransformSkew+slide*4+j*2+k; line.axis = slide;
				line.points << frame.project(boxPoint(low,high,from)) << frame.project(boxPoint(low,high,to));
				if (!finite(line.points[0]) || !finite(line.points[1]) || squared(line.points[1]-line.points[0]) < kTransformShortLine*kTransformShortLine) { continue; }
				line.label = (line.points[0]+line.points[1])/2; result << line;
			}
		}
	}
	// The corner points; a plan view has only those that turn in it.
	for (int axis = 0; axis < 3 && roomy; ++axis) {
		if ((plan && !alongView(axis)) || (extent((axis+1)%3) <= 1e-6 && extent((axis+2)%3) <= 1e-6)) { continue; }
		for (int j = 0; j < 2; ++j) {
			for (int k = 0; k < 2; ++k) {
				std::array<int,3> sides{0,0,0};
				sides[static_cast<size_t>((axis+1)%3)] = j ? 1 : -1; sides[static_cast<size_t>((axis+2)%3)] = k ? 1 : -1;
				LevelComponentGizmoHandle corner; corner.id = kTransformTurn+axis*4+j*2+k; corner.axis = axis;
				corner.label = frame.project(boxPoint(low,high,sides)); corner.points << corner.label;
				if (finite(corner.label)) { result << corner; }
			}
		}
	}
	LevelComponentGizmoHandle point; point.id = 4; point.axis = 3; point.text = tr("Pivot");
	point.label = frame.project(m_active && objectPivotHandle() ? transformed(m_pivot) : pivot()); point.points << point.label;
	if (finite(point.label)) { result << point; }
	return result;
}
int LevelComponentGizmo::transformHitTest(const QPointF& point,const LevelComponentGizmoFrame& frame) const
{
	// NetRadiant Custom's order: the pivot, the corners, the side lines, the
	// box to move, then the sides outside it to scale; with the zone round
	// the box's centre moving ahead of the corners and lines.
	const double scale = handleScale(frame);
	const auto all = transformHandles(frame);
	const auto targets = transformTargets(m_mins,m_maxs,frame);
	if (!targets.shown || !frame.ray) { return -1; }
	for (const auto& handle : all) {
		if (handle.id == 4 && squared(point-handle.label) <= targets.pivot*targets.pivot) { return 4; }
	}
	const auto ray = frame.ray(point);
	const bool inside = lineHitsBox(ray,m_mins,m_maxs,nullptr);
	if (inside && finite(targets.centre) && squared(point-targets.centre) <= targets.zone*targets.zone) { return kTransformMove; }
	int result = -1; double nearest = targets.corner*targets.corner;
	for (const auto& handle : all) {
		if (transformKindOf(handle.id) != LevelObjectTransformKind::Rotate) { continue; }
		const double distance = squared(point-handle.label);
		if (distance <= nearest) { nearest = distance; result = handle.id; }
	}
	if (result >= 0) { return result; }
	nearest = targets.line*targets.line;
	for (const auto& handle : all) {
		if (transformKindOf(handle.id) != LevelObjectTransformKind::Skew || handle.points.size() < 2) { continue; }
		const auto a = handle.points[0], line = handle.points[1]-a; const double length = squared(line);
		const double t = length < .01 ? 0.0 : std::clamp(QPointF::dotProduct(point-a,line)/length,0.0,1.0);
		const double distance = squared(point-a-line*t);
		if (distance < nearest) { nearest = distance; result = handle.id; }
	}
	if (result >= 0) { return result; }
	if (inside) { return kTransformMove; }
	const auto sides = outsideSides(ray,m_mins,m_maxs);
	if (!sides[0] && !sides[1] && !sides[2]) { return -1; }
	// Only just outside: within a band round the box as the view shows it.
	const double band = kTransformBand*scale;
	const auto low = targets.low, high = targets.high;
	if (point.x() < low.x()-band || point.x() > high.x()+band || point.y() < low.y()-band || point.y() > high.y()+band) { return -1; }
	return scaleHandle(sides);
}
bool LevelComponentGizmo::beginTransform(const QPointF& point,const LevelComponentGizmoFrame& frame,int handle)
{
	const auto kind = transformKindOf(handle); const auto ray = frame.ray(point);
	m_transformSides = {0,0,0}; m_moveVerticalValid = false;
	if (kind == LevelObjectTransformKind::Pivot) {
		return beginModelMoveDrag(model(m_pivot),frame.viewDirection,ModelMoveConstraint::ViewPlane,ray,0,&m_move);
	}
	if (kind == LevelObjectTransformKind::Move) {
		// From the point under the pointer: across a plan view, level in the
		// camera (across the view when it looks along the floor), with a
		// vertical plane facing the viewer for Alt. A parallel camera that
		// looks straight along an axis works as a plan view.
		LevelMapVec3 hit; if (!lineHitsBox(ray,m_mins,m_maxs,&hit)) { return false; }
		const auto view = unit({frame.viewDirection.x,frame.viewDirection.y,frame.viewDirection.z,true});
		const bool plan = !frame.perspective && view.valid
			&& (std::abs(view.x) >= kAxisAlongView || std::abs(view.y) >= kAxisAlongView || std::abs(view.z) >= kAxisAlongView);
		bool begun = !plan && beginModelMoveDrag(model(hit),{0,0,1},ModelMoveConstraint::ViewPlane,ray,0,&m_move);
		if (!begun) { begun = beginModelMoveDrag(model(hit),frame.viewDirection,ModelMoveConstraint::ViewPlane,ray,0,&m_move); }
		if (begun && !plan) {
			const auto toward = unit({-double(ray.direction.x),-double(ray.direction.y),0,true});
			m_moveVerticalValid = toward.valid && beginModelMoveDrag(model(hit),model(toward),ModelMoveConstraint::ViewPlane,ray,0,&m_moveVertical);
		}
		return begun;
	}
	LevelMapVec3 pivot = m_pivot;
	if (kind == LevelObjectTransformKind::Skew) {
		const auto line = skewLine(handle); m_transformSides[static_cast<size_t>(line.by)] = line.side;
		pivot = m_customPivot ? m_pivot : levelObjectTransformPivot(m_mins,m_maxs,m_transformSides);
		const double reach = coordinate(line.side > 0 ? m_maxs : m_mins,line.by)-coordinate(pivot,line.by);
		if (!finite(pivot) || std::abs(reach) < 1e-6) { return false; }
		if (!beginModelMoveDrag(model(boxPoint(m_mins,m_maxs,m_transformSides)),frame.viewDirection,ModelMoveConstraint(line.slide),ray,0,&m_move)) { return false; }
	} else if (kind == LevelObjectTransformKind::Rotate) {
		const auto corner = turnPoint(handle); m_transformSides = corner.sides;
		pivot = m_customPivot ? m_pivot : levelObjectTransformPivot(m_mins,m_maxs,corner.sides);
		if (!finite(pivot)) { return false; }
		const auto sight = sightAxis(frame,pivot);
		m_linearRotation = !sight.valid || std::abs(coordinate(sight,corner.axis)) < .15 || !beginModelRotateDrag(model(pivot),corner.axis,ray,0,&m_rotate);
		if (m_linearRotation) {
			// Seen edge-on, the turn follows the corner's own path on screen.
			LevelMapVec3 axis{0,0,0,true}; setCoordinate(axis,corner.axis,1);
			const double step = 1e-3;
			const auto columns = levelObjectRotationColumns(axis,step*180/std::numbers::pi);
			const auto at = boxPoint(m_mins,m_maxs,corner.sides), arm = minus(at,pivot);
			const auto turned = plus(pivot,plus(plus(times(columns[0],arm.x),times(columns[1],arm.y)),times(columns[2],arm.z)));
			const auto from = frame.project(at), to = frame.project(turned);
			m_tangent = (to-from)/step;
			if (!finite(from) || !finite(to) || squared(m_tangent) < 1) { return false; }
		}
	} else if (kind == LevelObjectTransformKind::Scale) {
		m_transformSides = scaleSides(handle);
		pivot = m_customPivot ? m_pivot : levelObjectTransformPivot(m_mins,m_maxs,m_transformSides);
		if (!finite(pivot)) { return false; }
		const auto origin = model(boxPoint(m_mins,m_maxs,m_transformSides));
		int count = 0, single = 0, across = 0;
		for (int axis = 0; axis < 3; ++axis) {
			if (m_transformSides[static_cast<size_t>(axis)]) { ++count; single = axis; } else { across = axis; }
		}
		if (count == 1) {
			if (!beginModelMoveDrag(origin,frame.viewDirection,ModelMoveConstraint(single),ray,0,&m_move)) { return false; }
		} else {
			// Two sides move across the plane they share, or across the view
			// when that plane is edge-on.
			const ModelVec3 normal{across == 0 ? 1.0f : 0.0f,across == 1 ? 1.0f : 0.0f,across == 2 ? 1.0f : 0.0f};
			if (!beginModelMoveDrag(origin,normal,ModelMoveConstraint::ViewPlane,ray,0,&m_move)
				&& !beginModelMoveDrag(origin,frame.viewDirection,ModelMoveConstraint::ViewPlane,ray,0,&m_move)) { return false; }
		}
	} else {
		return false;
	}
	m_transformPivot = pivot;
	return true;
}
void LevelComponentGizmo::paintObjectTransform(QPainter& painter,const LevelComponentGizmoFrame& frame,bool highContrast) const
{
	const auto all = transformHandles(frame); if (all.isEmpty()) { return; }
	painter.save(); painter.setRenderHint(QPainter::Antialiasing,true);
	const auto kind = transformKind();
	if (kind != LevelObjectTransformKind::Move) { paintObjectWire(painter,frame,highContrast); }
	const QColor selected = highContrast ? QColor(Qt::yellow) : QColor(255,190,65);
	const QColor screen = highContrast ? QColor(Qt::white) : QColor(120,220,255);
	// Markers shrink with their pointer targets on a box small on screen.
	const auto shift = kind == LevelObjectTransformKind::Move && m_valid ? m_request.delta : LevelMapVec3{0,0,0,true};
	const auto targets = transformTargets(plus(m_mins,shift),plus(m_maxs,shift),frame);
	const double scale = std::min(handleScale(frame),targets.pivot/kTranslatePivot);
	const double cornerSize = std::min(3.5*handleScale(frame),targets.corner/2);
	painter.setBrush(Qt::NoBrush);
	for (const auto& handle : all) {
		if (transformKindOf(handle.id) != LevelObjectTransformKind::Skew || handle.points.size() < 2) { continue; }
		// A skew lights its own line, a scale the sides it moves.
		const auto line = skewLine(handle.id);
		const bool hot = m_active && (handle.id == m_handle || (kind == LevelObjectTransformKind::Scale && m_transformSides[static_cast<size_t>(line.by)] == line.side));
		painter.setPen(QPen(Qt::black,4)); painter.drawLine(handle.points[0],handle.points[1]);
		painter.setPen(QPen(hot ? selected : screen,highContrast ? 2.5 : 1.5)); painter.drawLine(handle.points[0],handle.points[1]);
	}
	for (const auto& handle : all) {
		if (transformKindOf(handle.id) != LevelObjectTransformKind::Rotate) { continue; }
		const double size = cornerSize;
		painter.setPen(QPen(Qt::black,1.5)); painter.setBrush(m_active && handle.id == m_handle ? selected : screen);
		painter.drawRect(QRectF(handle.label-QPointF(size,size),QSizeF(size*2,size*2)));
	}
	for (const auto& handle : all) {
		if (handle.id != 4) { continue; }
		// While turning, a spoke from the pivot to the pointer.
		if (m_active && m_travelled && kind == LevelObjectTransformKind::Rotate) {
			painter.setPen(QPen(selected,highContrast ? 2.0 : 1.2,Qt::DashLine)); painter.drawLine(handle.label,m_last);
		}
		paintPivotPoint(painter,handle.label,m_active && m_handle == 4,m_customPivot,highContrast,scale,selected);
	}
	painter.restore();
}
void LevelComponentGizmo::paint(QPainter& painter,const LevelComponentGizmoFrame& frame,bool highContrast) const
{
	if (!m_available || m_tool == LevelComponentTool::Move || !frame.project) { return; }
	if (m_tool == LevelComponentTool::Translate) { paintTranslate(painter,frame,highContrast); return; }
	if (m_objects && m_tool == LevelComponentTool::Rotate) { paintObjectRotate(painter,frame,highContrast); return; }
	if (m_objects && m_tool == LevelComponentTool::Scale) { paintObjectScale(painter,frame,highContrast); return; }
	if (m_objects && m_tool == LevelComponentTool::Transform) { paintObjectTransform(painter,frame,highContrast); return; }
	painter.save();
	const QColor selected = highContrast ? QColor(Qt::yellow) : QColor(255,190,65);
	painter.setPen(QPen(selected,highContrast ? 2.5 : 1.5,Qt::DashLine));
	for (const auto& edge : m_edges) { const auto a = frame.project(edge.first),b = frame.project(edge.second); if (finite(a) && finite(b)) { painter.drawLine(a,b); } }
	const std::array<QColor,4> colours{QColor(255,110,110),QColor(120,255,150),QColor(140,180,255),selected};
	for (const auto& handle : handles(frame)) {
		for (int i = 1; i < handle.points.size(); ++i) {
			if (!finite(handle.points[i-1]) || !finite(handle.points[i])) { continue; }
			painter.setPen(QPen(Qt::black,5)); painter.drawLine(handle.points[i-1],handle.points[i]);
			painter.setPen(QPen(m_active && handle.id == m_handle ? selected : colours[handle.axis],highContrast ? 3 : 2)); painter.drawLine(handle.points[i-1],handle.points[i]);
		}
		if (finite(handle.label)) {
			const auto rect = labelRect(handle.label,painter.font(),handle.text); painter.setBrush(Qt::black); painter.setPen(QPen(colours[handle.axis],1.5));
			painter.drawRect(rect); painter.drawText(rect,Qt::AlignCenter|Qt::TextForceLeftToRight,handle.text);
		}
	}
	painter.restore();
}
} // namespace vibestudio
