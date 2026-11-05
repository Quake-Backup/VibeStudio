// The camera's selection resize box. Its edge and corner handles and the
// Shift and Alt modifiers follow TrenchBroom's Scale tool (ui/ScaleTool and
// ui/ScaleToolController at 4c756be1, GPL-3.0-or-later); the geometry is
// core/box_resize.h. Its shear mode follows TrenchBroom's Shear Tool
// (ui/ShearTool and ui/ShearToolController at 4c756be1): a face slides
// sideways, or up and down with Alt, and a top or bottom face within its
// plane. Original Qt implementation; see docs/CREDITS.md.
#include "app/model_viewport.h"
#include <QAccessible>
#include <QPainter>
#include <QPolygonF>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {
namespace {
ModelVec3 modelPoint(BoxResizePoint point) { return {float(point[0]),float(point[1]),float(point[2])}; }
BoxResizePoint coordinates(ModelVec3 point) { return {point.x,point.y,point.z}; }
bool finite(QPointF point) { return std::isfinite(point.x()) && std::isfinite(point.y()); }
QPointF absent() { return {std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::quiet_NaN()}; }
// "X+" for a face, "X+Y−" for an edge, "X+Y+Z−" for a corner.
QString axisLabel(int handle)
{
	const auto sides = boxResizeHandleSides(handle); QString text;
	for (int axis = 0; axis < 3; ++axis) {
		const int side = sides[static_cast<size_t>(axis)];
		if (side) { text += QString(QLatin1Char("XYZ"[axis])) + (side > 0 ? QStringLiteral("+") : QStringLiteral("−")); }
	}
	return text;
}
int movedMask(int handle)
{
	const auto sides = boxResizeHandleSides(handle); int mask = 0;
	for (int axis = 0; axis < 3; ++axis) { if (sides[static_cast<size_t>(axis)]) { mask |= 1 << axis; } }
	return mask;
}
// A face handle's axis and side (-1 its minimum, +1 its maximum).
int faceAxis(int handle)
{
	const auto sides = boxResizeHandleSides(handle);
	for (int axis = 0; axis < 3; ++axis) { if (sides[static_cast<size_t>(axis)]) { return axis; } }
	return 2;
}
int faceSide(int handle) { const auto sides = boxResizeHandleSides(handle); return sides[static_cast<size_t>(faceAxis(handle))] < 0 ? -1 : 1; }
// A point of `box` slanted by the dragged face's slide: every axis but the
// face's moves by its share of the slide, by how far the point lies from the
// opposite face.
BoxResizePoint shearedPoint(const ResizeBox& box, int handle, const BoxResizePoint& offset, BoxResizePoint point)
{
	const auto a = static_cast<size_t>(faceAxis(handle));
	const int side = faceSide(handle);
	const double moved = side > 0 ? box.maxs[a] : box.mins[a], anchor = side > 0 ? box.mins[a] : box.maxs[a];
	const double reach = moved-anchor;
	if (std::abs(reach) < 1e-9) { return point; }
	const double share = (point[a]-anchor)/reach;
	for (size_t other = 0; other < 3; ++other) { if (other != a) { point[other] += offset[other]*share; } }
	return point;
}
}

void ModelViewport::setSelectionShearMode(bool on)
{
	if (on == m_selectionShearMode) { return; }
	finishSelectionResize(false); m_selectionShearMode = on;
	setAccessibleDescription(accessibleSummary()); update();
}

void ModelViewport::setSelectionShearPreviewEdges(const QVector<QPair<LevelMapVec3,LevelMapVec3>>& edges)
{
	if (!isResizingSelection() || !m_selectionShearMode) { return; }
	m_selectionShearEdges = edges; update();
}

bool ModelViewport::beginSelectionShearDrag(const QPointF& point, bool vertical)
{
	// From the face's centre, moved by the slide settled so far: a top or
	// bottom face slides within its plane, a side face sideways or, with
	// Alt, up and down.
	const int axis = faceAxis(m_selectionResizeHandle), side = faceSide(m_selectionResizeHandle);
	const auto& box = m_selectionResizeBox;
	BoxResizePoint centre;
	for (size_t i = 0; i < 3; ++i) { centre[i] = (box.mins[i]+box.maxs[i])/2+m_selectionShearBase[i]; }
	const auto a = static_cast<size_t>(axis);
	centre[a] = side > 0 ? box.maxs[a] : box.mins[a];
	const auto direction = m_perspective ? m_camera.forward : ModelVec3{-m_camera.eye.x,-m_camera.eye.y,-m_camera.eye.z};
	const auto ray = editRay(point);
	ModelMoveDrag drag;
	const bool begun = axis == 2 ? beginModelMoveDrag(modelPoint(centre),{0,0,1},ModelMoveConstraint::ViewPlane,ray,0,&drag)
		: beginModelMoveDrag(modelPoint(centre),direction,vertical ? ModelMoveConstraint::Z : ModelMoveConstraint(1-axis),ray,0,&drag);
	if (!begun) { return false; }
	m_selectionShearDrag = drag; m_selectionShearVertical = vertical && axis != 2;
	return true;
}

void ModelViewport::setSelectionResizeBoxHandles(bool all)
{
	if (all == m_selectionResizeAllHandles) { return; }
	finishSelectionResize(false); m_selectionResizeAllHandles = all;
	setAccessibleDescription(accessibleSummary()); update();
}

void ModelViewport::setSelectionResizeBox(const ResizeBox& box, const QHash<int,BoxResizePoint>& pointOrigins, int axes)
{
	if (!validResizeBox(box)) { clearSelectionResizeBox(); return; }
	if (m_selectionResizeEnabled && m_selectionResizeBox == box && m_selectionResizeOrigins == pointOrigins && m_selectionResizeAxes == axes) { return; }
	finishSelectionResize(false);
	m_selectionResizeBox = box; m_selectionResizeOrigins = pointOrigins; m_selectionResizeEnabled = true;
	m_selectionResizeAxes = axes & 7;
	setAccessibleDescription(accessibleSummary()); update();
}

void ModelViewport::clearSelectionResizeBox()
{
	finishSelectionResize(false); m_selectionResizeEnabled = false; m_selectionResizeOrigins.clear();
	setAccessibleDescription(accessibleSummary()); update();
}

BoxResizeRay ModelViewport::selectionResizeRay(const QPointF& point) const
{
	const auto ray = editRay(point);
	return {coordinates(ray.origin),coordinates(ray.direction),ray.forwardOnly};
}

std::array<QPointF,kBoxResizeHandles> ModelViewport::selectionResizeHandles()
{
	std::array<QPointF,kBoxResizeHandles> points; points.fill(absent());
	if (!m_selectionResizeEnabled || !isEnabled() || !m_hasMesh || m_looking || m_brushDrawTool || m_surfaceTool != ModelViewportSurfaceTool::None) { return points; }
	ensureProjection();
	const auto box = selectionResizeBox();
	const auto direction = coordinates(m_perspective ? m_camera.forward : ModelVec3{-m_camera.eye.x,-m_camera.eye.y,-m_camera.eye.z});
	// Only the faces shear.
	const int count = m_selectionResizeAllHandles && !m_selectionShearMode ? kBoxResizeHandles : kBoxResizeFaceHandles;
	for (int handle = 0; handle < count; ++handle) {
		BoxResizePoint world;
		if ((movedMask(handle) & ~m_selectionResizeAxes) != 0) { continue; }
		if (!boxResizeHandle(box,handle,&world)) { continue; }
		const auto point = projectPoint(m_camera,modelPoint(world),nullptr);
		if (!finite(point) || !QRectF(rect()).adjusted(8,8,-8,-8).contains(point)) { continue; }
		BoxResizeDrag probe;
		if (beginBoxResize(box,handle,direction,selectionResizeRay(point),m_moveGrid,&probe)) { points[handle] = point; }
	}
	// Nearly coincident opposing faces cannot be picked unambiguously.
	for (int axis = 0; axis < 3; ++axis) {
		const auto delta = points[axis*2]-points[axis*2+1];
		if (finite(delta) && QPointF::dotProduct(delta,delta) < 144) { points[axis*2] = points[axis*2+1] = absent(); }
	}
	// An edge or corner crowding a face handle, or another edge or corner,
	// gives way, so every handle shown can be aimed at.
	std::array<bool,kBoxResizeHandles> crowded{};
	for (int handle = kBoxResizeFaceHandles; handle < count; ++handle) {
		if (!finite(points[handle])) { continue; }
		for (int other = 0; other < count; ++other) {
			if (other == handle || !finite(points[other])) { continue; }
			const auto delta = points[handle]-points[other];
			if (QPointF::dotProduct(delta,delta) < 144) { crowded[handle] = true; }
		}
	}
	for (int handle = kBoxResizeFaceHandles; handle < count; ++handle) { if (crowded[handle]) { points[handle] = absent(); } }
	return points;
}

std::array<QRectF,6> ModelViewport::selectionResizeLabels()
{
	std::array<QRectF,6> labels;
	const auto handles = selectionResizeHandles();
	const QFontMetricsF metrics(font());
	const QRectF bounds = QRectF(rect()).adjusted(3,metrics.height()*2.5+8,-3,-3);
	QPointF centre; int count = 0; QVector<QRectF> occupied;
	for (const auto& point : handles) {
		if (!finite(point)) { continue; }
		centre += point; ++count;
		occupied.append(QRectF(point-QPointF(11,11),QSizeF(22,22)));
	}
	if (!count) { return labels; }
	centre /= count;
	// Place short, fixed-direction axis names outside the handle cluster.
	// Leader lines preserve the face association when large fonts need room.
	// Only the faces carry a name; edges and corners read from their place.
	for (int handle = 0; handle < kBoxResizeFaceHandles; ++handle) {
		const auto point = handles[handle];
		if (!finite(point)) { continue; }
		const QSizeF size(metrics.horizontalAdvance(axisLabel(handle))+8,metrics.height()+4);
		if (size.width() > bounds.width() || size.height() > bounds.height()) { continue; }
		auto direction = point-centre;
		const double length = std::hypot(direction.x(),direction.y());
		direction = length > 1 ? direction/length : QPointF(handle%2 ? 1 : -1,0);
		for (int attempt = 0; attempt < 32; ++attempt) {
			const auto at = point + direction*(16+size.height()*0.5+attempt*(size.height()+8));
			QRectF candidate(QPointF(std::clamp(at.x()-size.width()*0.5,bounds.left(),bounds.right()-size.width()),
				std::clamp(at.y()-size.height()*0.5,bounds.top(),bounds.bottom()-size.height())),size);
			if (std::any_of(occupied.cbegin(),occupied.cend(),[&](const auto& other) { return other.intersects(candidate); })) { continue; }
			labels[handle] = candidate; occupied.append(candidate.adjusted(-4,-4,4,4)); break;
		}
	}
	return labels;
}

int ModelViewport::selectionResizeHandleAt(const QPointF& point)
{
	if (!finite(point) || isResizingSelection() || m_editMoveActive || m_surfaceStrokeActive || m_looking) { return -1; }
	const auto handles = selectionResizeHandles(); int result = -1; double nearest = 81;
	for (int handle = 0; handle < kBoxResizeHandles; ++handle) {
		if (!finite(handles[handle])) { continue; }
		const auto delta = handles[handle]-point; const double distance = QPointF::dotProduct(delta,delta);
		if (distance < nearest) { result = handle; nearest = distance; }
	}
	if (result >= 0) { return result; }
	const auto labels = selectionResizeLabels();
	for (int handle = 0; handle < 6; ++handle) { if (labels[handle].contains(point)) { return handle; } }
	return -1;
}

bool ModelViewport::beginSelectionResize(const QPointF& point)
{
	const int handle = selectionResizeHandleAt(point);
	if (handle < 0 || m_pressButton != Qt::NoButton) { return false; }
	if (m_selectionShearMode) {
		if (handle >= kBoxResizeFaceHandles) { return false; }
		m_selectionResizeHandle = handle; m_selectionShearBase = m_selectionShearOffset = {}; m_selectionShearEdges.clear();
		if (!beginSelectionShearDrag(point,false)) { m_selectionResizeHandle = -1; return false; }
	} else {
		const auto direction = coordinates(m_perspective ? m_camera.forward : ModelVec3{-m_camera.eye.x,-m_camera.eye.y,-m_camera.eye.z});
		if (!beginBoxResize(m_selectionResizeBox,handle,direction,selectionResizeRay(point),m_moveGrid,&m_selectionResizeDrag)) { return false; }
	}
	m_selectionResizeHandle = handle; m_selectionResizePreview = m_selectionResizeBox; m_selectionResizeValid = true;
	m_selectionResizePoint = point; m_selectionResizeModifiers = Qt::NoModifier;
	m_heldKeys.clear(); m_flyTimer->stop();
	setAccessibleDescription(accessibleSummary()); update();
	QAccessibleEvent announcement(this,QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&announcement);
	Q_EMIT selectionResizeActiveChanged(true);
	return isResizingSelection();
}

bool ModelViewport::updateSelectionResizeModifiers(Qt::KeyboardModifiers modifiers)
{
	return isResizingSelection() && updateSelectionResize(m_selectionResizePoint,modifiers);
}

bool ModelViewport::updateSelectionResize(const QPointF& point, Qt::KeyboardModifiers modifiers)
{
	if (!isResizingSelection()) { return false; }
	m_selectionResizePoint = point; m_selectionResizeModifiers = modifiers & (Qt::ShiftModifier|Qt::AltModifier);
	if (m_selectionShearMode) {
		// TrenchBroom keeps the slide so far when Alt changes, and goes on
		// along the new line from where the pointer is.
		const bool vertical = m_selectionResizeModifiers.testFlag(Qt::AltModifier) && faceAxis(m_selectionResizeHandle) != 2;
		if (vertical != m_selectionShearVertical && finite(point)) {
			const auto settled = m_selectionShearOffset, before = m_selectionShearBase;
			m_selectionShearBase = settled;
			if (!beginSelectionShearDrag(point,vertical)) { m_selectionShearBase = before; }
		}
		ModelVec3 delta;
		const bool valid = finite(point) && modelMoveDragDelta(m_selectionShearDrag,editRay(point),&delta);
		if (valid) {
			const auto axis = static_cast<size_t>(faceAxis(m_selectionResizeHandle));
			const std::array<double,3> parts{delta.x,delta.y,delta.z};
			BoxResizePoint next = m_selectionShearBase;
			for (size_t i = 0; i < 3; ++i) {
				if (i != axis) { next[i] += m_moveGrid > 0 ? std::round(parts[i]/m_moveGrid)*m_moveGrid : parts[i]; }
			}
			if (next != m_selectionShearOffset) { m_selectionShearOffset = next; }
		}
		m_selectionResizeValid = valid;
		setAccessibleDescription(accessibleSummary());
		Q_EMIT hoverChanged(selectionResizeSummary(false));
		Q_EMIT selectionResizePreviewChanged(); update();
		return valid;
	}
	// Alt: about the centre. Shift: the axes the handle leaves keep their
	// proportion (TrenchBroom's 3D views scale them all).
	BoxResizeModifiers keys;
	keys.centre = m_selectionResizeModifiers.testFlag(Qt::AltModifier);
	keys.proportionalAxes = m_selectionResizeModifiers.testFlag(Qt::ShiftModifier) ? m_selectionResizeAxes & ~movedMask(m_selectionResizeHandle) : 0;
	ResizeBox next;
	const bool valid = finite(point) && updateBoxResize(m_selectionResizeDrag,selectionResizeRay(point),keys,&next);
	const bool changed = valid && next != m_selectionResizePreview;
	m_selectionResizeValid = valid;
	if (changed) { m_selectionResizePreview = next; ++m_projectionRevision; invalidateRaster(); }
	setAccessibleDescription(accessibleSummary());
	Q_EMIT hoverChanged(selectionResizeSummary(false));
	Q_EMIT selectionResizePreviewChanged(); update();
	return valid;
}

void ModelViewport::finishSelectionResize(bool commit)
{
	if (!isResizingSelection()) { return; }
	const auto box = m_selectionResizePreview;
	const bool shear = m_selectionShearMode;
	const auto offset = m_selectionShearOffset;
	const int handle = m_selectionResizeHandle;
	const bool changed = commit && m_selectionResizeValid
		&& (shear ? offset[0] != 0 || offset[1] != 0 || offset[2] != 0 : box != m_selectionResizeBox);
	m_selectionResizeHandle = -1; m_selectionResizeValid = false; m_pressButton = Qt::NoButton;
	m_selectionShearOffset = m_selectionShearBase = {}; m_selectionShearVertical = false; m_selectionShearEdges.clear();
	++m_projectionRevision; invalidateRaster(); unsetCursor();
	setAccessibleDescription(accessibleSummary()); update();
	QAccessibleEvent announcement(this,QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&announcement);
	Q_EMIT selectionResizeActiveChanged(false);
	if (changed && shear) { Q_EMIT selectionShearRequested(faceAxis(handle),faceSide(handle),offset[0],offset[1],offset[2]); }
	else if (changed) { Q_EMIT selectionResizeRequested(box); }
}

QString ModelViewport::selectionResizeSummary(bool describeControls) const
{
	if (!m_selectionResizeEnabled) { return {}; }
	if (m_selectionShearMode) {
		if (!isResizingSelection()) {
			return tr("Shear handles: drag a labelled face to slide that side of the selection while the opposite side stays; Alt slides a side face up and down. Shear… slants by a typed angle.");
		}
		if (!m_selectionResizeValid) { return describeControls
			? tr("Shear unavailable at this pointer position. Releasing cancels the edit; Escape cancels.") : tr("Shear unavailable"); }
		const auto slide = tr("Shear %1: the side slides %2, %3, %4")
			.arg(QString(QChar(0x2066))+axisLabel(m_selectionResizeHandle)+QChar(0x2069))
			.arg(m_selectionShearOffset[0],0,'g',8).arg(m_selectionShearOffset[1],0,'g',8).arg(m_selectionShearOffset[2],0,'g',8);
		return describeControls ? slide + QStringLiteral(". ") + tr("Release applies one undo step; Escape cancels.") : slide;
	}
	if (!isResizingSelection()) {
		return m_selectionResizeAllHandles
			? tr("Selection resize handles: drag a labelled face, an edge or a corner to resize; Shift keeps proportions and Alt resizes about the centre. Resize Selection provides numeric keyboard editing.")
			: tr("Selection resize handles: drag a labelled face to resize along its axis; Shift keeps proportions and Alt resizes about the centre. Resize Selection provides numeric keyboard editing.");
	}
	if (!m_selectionResizeValid) { return describeControls
		? tr("Resize preview unavailable at this pointer position. Releasing cancels the edit; Escape cancels.") : tr("Resize unavailable"); }
	const auto dimensions = tr("Resize %1: %2 × %3 × %4 units")
		.arg(QString(QChar(0x2066))+axisLabel(m_selectionResizeHandle)+QChar(0x2069))
		.arg(m_selectionResizePreview.maxs[0]-m_selectionResizePreview.mins[0],0,'g',8)
		.arg(m_selectionResizePreview.maxs[1]-m_selectionResizePreview.mins[1],0,'g',8)
		.arg(m_selectionResizePreview.maxs[2]-m_selectionResizePreview.mins[2],0,'g',8);
	return describeControls ? dimensions + QStringLiteral(". ") + tr("Release applies one undo step; Escape cancels.") : dimensions;
}

void ModelViewport::paintSelectionResize(QPainter& painter)
{
	if (!m_selectionResizeEnabled || !isEnabled() || m_looking || m_brushDrawTool || m_surfaceTool != ModelViewportSurfaceTool::None) { return; }
	const auto box = selectionResizeBox();
	const QColor color = m_highContrast ? QColor(Qt::yellow) : QColor(255,205,70);
	painter.save(); painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(color,m_highContrast ? 2.0 : 1.2,Qt::DashLine));
	for (int corner = 0; corner < 8; ++corner) {
		BoxResizePoint from;
		for (int axis = 0; axis < 3; ++axis) { from[axis] = corner & (1<<axis) ? box.maxs[axis] : box.mins[axis]; }
		for (int axis = 0; axis < 3; ++axis) {
			if (corner & (1<<axis)) { continue; }
			auto to = from; to[axis] = box.maxs[axis]; QPointF a,b;
			if (projectSegment(modelPoint(from),modelPoint(to),&a,&b)) { painter.drawLine(a,b); }
		}
	}
	if (m_selectionShearMode && isResizingSelection()) {
		// The box and the selection where the slide would take them.
		const QColor slanted = m_highContrast ? QColor(Qt::white) : QColor(255,190,65);
		painter.setPen(QPen(slanted,m_highContrast ? 2.0 : 1.4,Qt::DashLine));
		const auto through = [&](const LevelMapVec3& p) {
			const auto moved = shearedPoint(m_selectionResizeBox,m_selectionResizeHandle,m_selectionShearOffset,{p.x,p.y,p.z});
			return modelPoint(moved);
		};
		for (int corner = 0; corner < 8; ++corner) {
			LevelMapVec3 from{0,0,0,true};
			from.x = corner & 1 ? box.maxs[0] : box.mins[0]; from.y = corner & 2 ? box.maxs[1] : box.mins[1]; from.z = corner & 4 ? box.maxs[2] : box.mins[2];
			for (int axis = 0; axis < 3; ++axis) {
				if (corner & (1<<axis)) { continue; }
				auto to = from; (axis == 0 ? to.x : axis == 1 ? to.y : to.z) = box.maxs[static_cast<size_t>(axis)];
				QPointF a,b; if (projectSegment(through(from),through(to),&a,&b)) { painter.drawLine(a,b); }
			}
		}
		painter.setPen(QPen(slanted,m_highContrast ? 1.6 : 1.0,Qt::DotLine));
		for (const auto& edge : std::as_const(m_selectionShearEdges)) {
			QPointF a,b; if (projectSegment(through(edge.first),through(edge.second),&a,&b)) { painter.drawLine(a,b); }
		}
	}
	const auto handles = selectionResizeHandles(); const auto labels = selectionResizeLabels();
	// Edges as diamonds and corners as discs, under the faces' squares.
	painter.setRenderHint(QPainter::Antialiasing,true);
	for (int handle = kBoxResizeFaceHandles; handle < kBoxResizeHandles; ++handle) {
		if (!finite(handles[handle])) { continue; }
		const QPointF point = handles[handle]; const double size = m_highContrast ? 5.5 : 4.5;
		painter.setPen(QPen(Qt::black,1)); painter.setBrush(handle == m_selectionResizeHandle ? QColor(Qt::white) : color);
		if (handle < 18) { painter.drawPolygon(QPolygonF{point+QPointF(0,-size),point+QPointF(size,0),point+QPointF(0,size),point+QPointF(-size,0)}); }
		else { painter.drawEllipse(point,size-0.5,size-0.5); }
	}
	painter.setRenderHint(QPainter::Antialiasing,false);
	for (int handle = 0; handle < kBoxResizeFaceHandles; ++handle) {
		if (!finite(handles[handle])) { continue; }
		const QPointF point = handles[handle];
		painter.setPen(QPen(color,1));
		const auto text = labels[handle];
		if (!text.isEmpty()) { painter.drawLine(point,QPointF(std::clamp(point.x(),text.left(),text.right()),std::clamp(point.y(),text.top(),text.bottom()))); }
		painter.setPen(QPen(Qt::black,1)); painter.setBrush(color);
		painter.drawRect(QRectF(point-QPointF(4,4),QSizeF(8,8)));
		if (text.isEmpty()) { continue; }
		painter.setPen(QPen(color,1)); painter.setBrush(Qt::black); painter.drawRoundedRect(text,2,2);
		painter.drawText(text,Qt::AlignCenter|Qt::TextForceLeftToRight,axisLabel(handle));
	}
	painter.restore();
}

} // namespace vibestudio
