// The UV Editor's texture-space view. Workflow reference: NetRadiant
// Custom's TexTool plugin (plugins/textool/TexTool.cpp,
// ControlPointsManager.cpp and 2DView.cpp at 449778b3, GPL-2.0-or-later):
// points drawn over the tiled texture, a dragged face point that becomes the
// anchor for turns, a right-drag pan, Insert and Delete zoom and a
// right-click menu. Original Qt implementation; see docs/CREDITS.md.

#include "app/level_uv_view.h"
#include "app/locale_number.h"

#include <QAccessible>
#include <QAccessibleWidget>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QResizeEvent>
#include <QSet>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace vibestudio {
namespace {

constexpr double kMinZoom = 1e-4, kMaxZoom = 1e4;
// Arrow keys with Shift nudge this many snap steps.
constexpr double kCoarseNudge = 8;
// Ctrl+arrows pan this many pixels.
constexpr double kKeyPan = 32;
// At most this many repeat or grid lines each way.
constexpr int kLineLimit = 400;

bool finitePoint(QPointF p)
{
	return std::isfinite(p.x()) && std::isfinite(p.y());
}

bool sameEdit(const LevelUvEdit& a, const LevelUvEdit& b)
{
	const LevelUvTransform& x = a.transform;
	const LevelUvTransform& y = b.transform;
	return x.xx == y.xx && x.xy == y.xy && x.yx == y.yx && x.yy == y.yy && x.dx == y.dx && x.dy == y.dy && a.patchUv == b.patchUv && a.anchor == b.anchor;
}

// The keys the view handles itself, so window shortcuts do not take them.
// With handles on, undo, redo and Select All stay the map's.
bool viewKey(const QKeyEvent* event, bool dragging, bool handles = false)
{
	if (dragging) {
		return true;
	}
	const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
	const bool plain = modifiers == Qt::NoModifier, shift = modifiers == Qt::ShiftModifier, control = modifiers == Qt::ControlModifier;
	switch (event->key()) {
	case Qt::Key_Left:
	case Qt::Key_Right:
	case Qt::Key_Up:
	case Qt::Key_Down:
		return plain || shift || control;
	case Qt::Key_BracketLeft:
	case Qt::Key_BracketRight:
	case Qt::Key_BraceLeft:
	case Qt::Key_BraceRight:
	case Qt::Key_Plus:
	case Qt::Key_Equal:
	case Qt::Key_Minus:
	case Qt::Key_Underscore:
		return plain || shift;
	case Qt::Key_Insert:
	case Qt::Key_Delete:
	case Qt::Key_Home:
	case Qt::Key_F:
		return plain;
	case Qt::Key_N:
		return plain || shift;
	case Qt::Key_A:
		return plain || (control && !handles);
	case Qt::Key_Z:
		return !handles && (control || modifiers == (Qt::ControlModifier | Qt::ShiftModifier));
	case Qt::Key_Y:
		return !handles && control;
	default:
		return false;
	}
}

} // namespace

LevelUvView::LevelUvView(QWidget* parent) : QWidget(parent)
{
	static const bool accessibleInstalled = [] {
		QAccessible::installFactory([](const QString&, QObject* object) -> QAccessibleInterface* {
			if (auto* view = qobject_cast<LevelUvView*>(object)) {
				return new QAccessibleWidget(view, QAccessible::Graphic);
			}
			return nullptr;
		});
		return true;
	}();
	Q_UNUSED(accessibleInstalled);
	setObjectName(QStringLiteral("levelUvView"));
	setFocusPolicy(Qt::StrongFocus);
	// Texture space reads the same way in every layout direction.
	setLayoutDirection(Qt::LeftToRight);
	setAccessibleName(tr("UV Editor texture space"));
	setToolTip(tr("Drag points over the texture. A face's texture follows the dragged point and turns about the anchor, the point dragged "
				  "last; a patch's points move on their own. Arrow keys nudge, [ and ] turn, the wheel zooms, a right-drag pans and a "
				  "right-click opens a menu."));
	m_checker = QImage(2, 2, QImage::Format_RGB32);
	m_checker.setPixelColor(0, 0, QColor(92, 98, 106));
	m_checker.setPixelColor(1, 1, QColor(92, 98, 106));
	m_checker.setPixelColor(1, 0, QColor(150, 156, 164));
	m_checker.setPixelColor(0, 1, QColor(150, 156, 164));
	buildMenu();
	announce();
}

LevelUvView::~LevelUvView() = default;

void LevelUvView::buildMenu()
{
	m_menu = new QMenu(this);
	m_menu->setObjectName(QStringLiteral("levelUvViewMenu"));
	const auto add = [this](const QString& name, const QString& text, const std::function<void()>& run) {
		QAction* action = m_menu->addAction(text);
		action->setObjectName(name);
		connect(action, &QAction::triggered, this, run);
		return action;
	};
	add(QStringLiteral("levelUvUndo"), tr("Undo"), [this] { undo(); });
	add(QStringLiteral("levelUvRedo"), tr("Redo"), [this] { redo(); });
	add(QStringLiteral("levelUvReset"), tr("Reset"), [this] { resetEdit(); });
	m_menu->addSeparator();
	add(QStringLiteral("levelUvAnchor"), tr("Set Anchor at Current Point"), [this] {
		if (m_handles) {
			setPivotTexel(m_result.uv.value(m_current, pivotTexel()));
		} else {
			setAnchor(m_anchor == m_current ? -1 : m_current);
		}
	});
	add(QStringLiteral("levelUvTurnAnticlockwise"), tr("Turn Anticlockwise"), [this] { m_handles ? turnTexture(-m_angleStep) : rotate(-m_angleStep); });
	add(QStringLiteral("levelUvTurnClockwise"), tr("Turn Clockwise"), [this] { m_handles ? turnTexture(m_angleStep) : rotate(m_angleStep); });
	m_menu->addSeparator();
	add(QStringLiteral("levelUvFrame"), tr("Frame All"), [this] { frameAll(); });
	add(QStringLiteral("levelUvZoomIn"), tr("Zoom In"), [this] { zoomBy(2); });
	add(QStringLiteral("levelUvZoomOut"), tr("Zoom Out"), [this] { zoomBy(0.5); });
	connect(m_menu, &QMenu::aboutToShow, this, [this] {
		const auto find = [this](const char* name) { return m_menu->findChild<QAction*>(QString::fromLatin1(name)); };
		find("levelUvUndo")->setEnabled(canUndo());
		find("levelUvRedo")->setEnabled(canRedo());
		find("levelUvReset")->setEnabled(m_hasSource && !sameEdit(m_edit, LevelUvEdit {}));
		// With handles, each change is already a map undo step.
		for (const char* name : {"levelUvUndo", "levelUvRedo", "levelUvReset"}) {
			find(name)->setVisible(!m_handles);
		}
		QAction* anchor = find("levelUvAnchor");
		anchor->setEnabled(m_hasSource && (m_current >= 0 || m_anchor >= 0));
		anchor->setText(m_handles ? tr("Put Pivot at Current Point")
			: m_anchor >= 0 && m_anchor == m_current ? tr("Clear Anchor") : tr("Set Anchor at Current Point"));
	});
}

void LevelUvView::setSource(const LevelUvLayout& layout, const QImage& image, bool keepView)
{
	// The same face or patch again, as after an edit: the camera, the current
	// point and the pivot stay, the pivot only while the face has not moved.
	const bool same = keepView && m_hasSource && layout.target == m_layout.target && layout.points.size() == m_layout.points.size();
	const bool samePoints = same
		&& std::equal(layout.points.cbegin(), layout.points.cend(), m_layout.points.cbegin(), [](const LevelMapVec3& a, const LevelMapVec3& b) {
			   return a.x == b.x && a.y == b.y && a.z == b.z;
		   });
	const QPointF centre = m_centre;
	const double zoom = m_zoom;
	const bool userCamera = m_userCamera;
	const int current = m_current;
	++m_sourceSerial;
	m_message.clear();
	m_handle = Handle::None;
	m_handleEdit = {};
	m_drag = Drag::None;
	m_layout = layout;
	m_hasSource = true;
	m_image = image;
	m_edit = {};
	m_edit.allowValve220 = m_allowValve220;
	m_result = {};
	m_result.face = layout.face;
	m_result.patch = layout.patch;
	m_result.uv = layout.uv;
	m_error.clear();
	m_undo.clear();
	m_redo.clear();
	m_anchor = -1;
	m_current = layout.uv.isEmpty() ? -1 : 0;
	m_selected.clear();
	m_userCamera = false;
	if (same) {
		m_centre = centre;
		m_zoom = zoom;
		m_userCamera = userCamera;
		m_current = current >= 0 && current < layout.uv.size() ? current : m_current;
	} else {
		frameAll();
	}
	if (!samePoints || !m_pivot.valid) {
		resetPivot();
	}
	announce();
	update();
	Q_EMIT editChanged();
	Q_EMIT stateChanged();
}

void LevelUvView::clearSource(const QString& message)
{
	++m_sourceSerial;
	m_drag = Drag::None;
	m_handle = Handle::None;
	m_handleEdit = {};
	m_hasSource = false;
	m_layout = {};
	m_result = {};
	m_edit = {};
	m_error.clear();
	m_undo.clear();
	m_redo.clear();
	m_anchor = m_current = -1;
	m_selected.clear();
	m_pivot = {};
	m_message = message;
	unsetCursor();
	announce();
	update();
	Q_EMIT editChanged();
	Q_EMIT stateChanged();
}

void LevelUvView::setHandlesEnabled(bool enabled)
{
	if (enabled == m_handles) {
		return;
	}
	m_handles = enabled;
	setToolTip(enabled ? tr("Drag inside the face or one of its points to move the texture. Drag the ring's knob to turn it and the square knobs "
							"to stretch it along U, V or both, about the pivot; drag the pivot to move it. Arrow keys move, [ and ] turn, A "
							"puts the pivot at the current point, the wheel zooms and a right-drag pans. Each change is one undo step.")
					   : tr("Drag points over the texture. A face's texture follows the dragged point and turns about the anchor, the point "
							"dragged last; a patch's points move on their own. Arrow keys nudge, [ and ] turn, the wheel zooms, a right-drag "
							"pans and a right-click opens a menu."));
	announce();
	update();
}

bool LevelUvView::faceValues(LevelTextureValues* values) const
{
	return m_hasSource && !m_layout.isPatch() && levelTextureValues(m_layout.face, m_layout.imageSize, values);
}

void LevelUvView::resetPivot()
{
	m_pivot = {};
	if (!m_hasSource || m_layout.isPatch() || m_layout.uv.isEmpty()) {
		return;
	}
	// The face's point nearest the lower left of its texture-space bounds, as
	// TrenchBroom's UV origin starts there.
	double left = std::numeric_limits<double>::infinity(), bottom = -left;
	for (const QPointF& point : std::as_const(m_layout.uv)) {
		left = std::min(left, point.x());
		bottom = std::max(bottom, point.y());
	}
	int best = 0;
	double distance = std::numeric_limits<double>::infinity();
	for (int i = 0; i < m_layout.uv.size(); ++i) {
		const QPointF delta = m_layout.uv[i] - QPointF(left, bottom);
		const double length = QPointF::dotProduct(delta, delta);
		if (length < distance) {
			distance = length;
			best = i;
		}
	}
	m_pivot = m_layout.points.value(best);
	m_pivot.valid = true;
}

QPointF LevelUvView::pivotTexel() const
{
	QPointF texel(std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN());
	if (m_hasSource && !m_layout.isPatch() && m_pivot.valid) {
		levelFaceTexelAt(m_result.face, m_layout.imageSize, m_pivot, &texel);
	}
	return texel;
}

bool LevelUvView::setPivotTexel(QPointF texel)
{
	LevelMapVec3 world;
	if (!m_handles || !m_hasSource || m_layout.isPatch() || !levelFaceWorldAtTexel(m_layout.points, m_result.uv, texel, &world)) {
		return false;
	}
	m_pivot = world;
	announce();
	update();
	Q_EMIT stateChanged();
	return true;
}

QPointF LevelUvView::handlePosition(Handle handle) const
{
	const QPointF hidden(std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN());
	const QPointF pivot = pivotTexel();
	if (!m_handles || !m_hasSource || m_layout.isPatch() || !finitePoint(pivot) || m_result.uv.size() < 2) {
		return hidden;
	}
	const QPointF centre = texelToScreen(pivot);
	if (handle == Handle::Pivot) {
		return centre;
	}
	if (handle == Handle::Turn) {
		// On a ring about the pivot, along the face's first edge, so the knob
		// turns with the face.
		const QPointF direction = m_result.uv[1] - m_result.uv[0];
		const double length = std::hypot(direction.x(), direction.y());
		if (!std::isfinite(length) || length < 1e-12) {
			return hidden;
		}
		return centre + direction / length * ringRadius();
	}
	if (handle == Handle::None) {
		return hidden;
	}
	// The stretch knobs sit just outside the face's texture-space bounds, on
	// the side farther from the pivot, at least a few dozen pixels out, so a
	// knob never covers a corner's marker.
	double left = std::numeric_limits<double>::infinity(), top = left, right = -left, bottom = -left;
	for (const QPointF& point : std::as_const(m_result.uv)) {
		left = std::min(left, point.x());
		right = std::max(right, point.x());
		top = std::min(top, point.y());
		bottom = std::max(bottom, point.y());
	}
	const double reach = std::max(28.0, fontMetrics().height() * 2.0) / m_zoom;
	const double clear = 12.0 / m_zoom;
	const auto far = [reach, clear](double from, double low, double high) {
		const double edge = high - from >= from - low ? high : low;
		const double sign = edge >= from ? 1.0 : -1.0;
		return std::abs(edge - from) + clear < reach ? from + sign * reach : edge + sign * clear;
	};
	const QPointF knob(far(pivot.x(), left, right), far(pivot.y(), top, bottom));
	if (handle == Handle::StretchU) {
		return texelToScreen(QPointF(knob.x(), pivot.y()));
	}
	if (handle == Handle::StretchV) {
		return texelToScreen(QPointF(pivot.x(), knob.y()));
	}
	return texelToScreen(knob);
}

double LevelUvView::ringRadius() const
{
	// A few lines of text across, but never more than about a fifth of the
	// view's shorter side: at 200% text the knob stays inside a narrow
	// sidebar instead of on its edge.
	const double wanted = std::max(36.0, fontMetrics().height() * 2.5);
	return std::min(wanted, std::max(20.0, std::min(width(), height()) * 0.22));
}

LevelUvView::Handle LevelUvView::handleAt(QPointF screen) const
{
	if (!m_handles) {
		return Handle::None;
	}
	Handle found = Handle::None;
	double best = 10.0 * 10.0;
	for (const Handle handle : {Handle::Pivot, Handle::StretchBoth, Handle::StretchU, Handle::StretchV, Handle::Turn}) {
		const QPointF at = handlePosition(handle);
		if (!finitePoint(at)) {
			continue;
		}
		const QPointF delta = at - screen;
		const double distance = QPointF::dotProduct(delta, delta);
		if (distance < best) {
			best = distance;
			found = handle;
		}
	}
	return found;
}

double LevelUvView::turnRate(const LevelTextureValues& values) const
{
	// Turn the rotation value by one degree and read how far, and which way,
	// the face's outline turns as drawn: a mirrored texture turns the other
	// way, and unequal scales turn it unevenly.
	if (m_layout.points.size() < 2 || !m_pivot.valid) {
		return 1;
	}
	LevelFaceAttributesEdit edit;
	edit.setRotation = true;
	edit.rotation = values.rotation + 1;
	edit.keepPivot = true;
	edit.pivot = m_pivot;
	LevelMapBrushFace turned;
	QPointF a0, a1, b0, b1;
	if (!applyLevelFaceAttributesEdit(m_layout.face, edit, m_layout.imageSize, &turned)
		|| !levelFaceTexelAt(m_layout.face, m_layout.imageSize, m_layout.points[0], &a0)
		|| !levelFaceTexelAt(m_layout.face, m_layout.imageSize, m_layout.points[1], &a1)
		|| !levelFaceTexelAt(turned, m_layout.imageSize, m_layout.points[0], &b0)
		|| !levelFaceTexelAt(turned, m_layout.imageSize, m_layout.points[1], &b1)) {
		return 1;
	}
	const double rate = levelUvTurnAngle(QPointF(), a1 - a0, b1 - b0);
	return std::abs(rate) < 0.05 ? (rate < 0 ? -1.0 : 1.0) : rate;
}

bool LevelUvView::previewHandleEdit(const LevelFaceAttributesEdit& edit)
{
	if (!m_hasSource || m_layout.isPatch()) {
		return false;
	}
	LevelMapBrushFace face = m_layout.face;
	QString error;
	if (edit.changesSomething() && !applyLevelFaceAttributesEdit(m_layout.face, edit, m_layout.imageSize, &face, &error)) {
		m_error = error;
		announce();
		update();
		Q_EMIT editChanged();
		return false;
	}
	QVector<QPointF> uv;
	uv.reserve(m_layout.points.size());
	for (const LevelMapVec3& point : std::as_const(m_layout.points)) {
		QPointF texel;
		if (!levelFaceTexelAt(face, m_layout.imageSize, point, &texel)) {
			m_error = tr("The texture values give the face a mapping that is not finite.");
			announce();
			update();
			Q_EMIT editChanged();
			return false;
		}
		uv.append(texel);
	}
	m_error.clear();
	m_handleEdit = edit;
	m_result.face = face;
	m_result.uv = uv;
	m_result.fit = uv == m_layout.uv ? LevelUvFit::Unchanged : LevelUvFit::Exact;
	announce();
	update();
	Q_EMIT editChanged();
	return true;
}

void LevelUvView::reportHandleEdit(const LevelFaceAttributesEdit& edit)
{
	const quint64 serial = m_sourceSerial;
	if (edit.changesSomething() && handleEdited) {
		handleEdited(edit);
	}
	// A commit gives the view the face again; otherwise show it unchanged.
	if (serial == m_sourceSerial && m_hasSource) {
		previewHandleEdit({});
		m_handleEdit = {};
	}
}

bool LevelUvView::turnTexture(double clockwiseDegrees)
{
	LevelTextureValues values;
	if (!m_handles || dragging() || !std::isfinite(clockwiseDegrees) || clockwiseDegrees == 0 || !faceValues(&values) || !m_pivot.valid) {
		return false;
	}
	LevelFaceAttributesEdit edit;
	edit.setRotation = true;
	edit.rotation = std::round((values.rotation + clockwiseDegrees / turnRate(values)) * 10000.0) / 10000.0;
	edit.keepPivot = true;
	edit.pivot = m_pivot;
	if (!previewHandleEdit(edit)) {
		return false;
	}
	reportHandleEdit(edit);
	return true;
}

LevelFaceAttributesEdit LevelUvView::handleDragEdit(QPointF screen, Qt::KeyboardModifiers modifiers) const
{
	LevelFaceAttributesEdit edit;
	edit.keepPivot = true;
	edit.pivot = m_pivot;
	const QPointF at = screenToTexel(screen);
	switch (m_handle) {
	case Handle::Turn: {
		double rotation = m_dragValues.rotation + levelUvTurnAngle(m_dragPivot, screenToTexel(m_press), at) / m_dragRate;
		// Shift snaps the value to the turn step; otherwise to 1/10000 degree.
		rotation = modifiers.testFlag(Qt::ShiftModifier) ? std::round(rotation / m_angleStep) * m_angleStep : std::round(rotation * 10000.0) / 10000.0;
		edit.setRotation = true;
		edit.rotation = rotation == 0 ? 0.0 : rotation;
		break;
	}
	case Handle::StretchU:
	case Handle::StretchV:
	case Handle::StretchBoth: {
		const QPointF to = m_snap > 0 ? snapLevelUv(at, m_snap) : at;
		const QPointF from = m_dragKnob - m_dragPivot, now = to - m_dragPivot;
		// Never through the pivot, nor down to nothing: flips have their own
		// commands.
		const auto factor = [](double start, double current) { return std::abs(start) < 1e-9 ? 1.0 : std::max(current / start, 1.0 / 64.0); };
		double u = 1, v = 1;
		if (m_handle == Handle::StretchU) {
			u = factor(from.x(), now.x());
		} else if (m_handle == Handle::StretchV) {
			v = factor(from.y(), now.y());
		} else {
			const double length = QPointF::dotProduct(from, from);
			u = v = length < 1e-18 ? 1.0 : std::max(QPointF::dotProduct(now, from) / length, 1.0 / 64.0);
		}
		// More texels across the face means a smaller texture on it; values
		// keep 1/10000, as TrenchBroom's do.
		const auto scaled = [](double value, double by) {
			const double result = std::round(value / by * 10000.0) / 10000.0;
			return result == 0 ? value / by : result;
		};
		if (u != 1) {
			edit.setScaleU = true;
			edit.scaleU = scaled(m_dragValues.scaleU, u);
		}
		if (v != 1) {
			edit.setScaleV = true;
			edit.scaleV = scaled(m_dragValues.scaleV, v);
		}
		break;
	}
	case Handle::Pivot:
	case Handle::None:
		break;
	}
	return edit;
}

void LevelUvView::setImage(const QImage& image)
{
	m_image = image;
	update();
}

QSizeF LevelUvView::tile() const
{
	// Classic and Valve 220 faces without a known image show 64-texel tiles.
	return m_layout.imageSize.width() > 0 && m_layout.imageSize.height() > 0 ? QSizeF(m_layout.imageSize) : QSizeF(64, 64);
}

void LevelUvView::setAllowValve220(bool allow)
{
	if (allow == m_allowValve220) {
		return;
	}
	m_allowValve220 = allow;
	if (m_hasSource) {
		setWorkingEdit(m_edit);
	}
}

void LevelUvView::setSnapStep(double texels)
{
	m_snap = std::isfinite(texels) && texels > 0 ? texels : 0;
	update();
}

void LevelUvView::setAngleStep(double degrees)
{
	if (std::isfinite(degrees) && degrees > 0 && degrees <= 360) {
		m_angleStep = degrees;
	}
}

bool LevelUvView::setAnchor(int point)
{
	if (!m_hasSource || point < -1 || point >= m_result.uv.size()) {
		return false;
	}
	m_anchor = point;
	if (point >= 0) {
		m_current = point;
	}
	announce();
	update();
	Q_EMIT stateChanged();
	return true;
}

void LevelUvView::setCurrentPoint(int point)
{
	if (!m_hasSource || point < 0 || point >= m_result.uv.size() || point == m_current) {
		return;
	}
	m_current = point;
	announce();
	update();
	Q_EMIT stateChanged();
}

void LevelUvView::setSelectedPoints(const QVector<int>& points)
{
	QVector<int> valid;
	for (const int point : points) {
		if (point >= 0 && point < m_result.uv.size() && !valid.contains(point)) {
			valid.append(point);
		}
	}
	if (!m_layout.isPatch() || valid == m_selected) {
		return;
	}
	m_selected = valid;
	if (!valid.isEmpty() && !valid.contains(m_current)) {
		m_current = valid.first();
	}
	announce();
	update();
	Q_EMIT stateChanged();
}

bool LevelUvView::setWorkingEdit(const LevelUvEdit& edit)
{
	LevelUvEdit candidate = edit;
	candidate.allowValve220 = m_allowValve220;
	LevelUvResult result;
	QString error;
	if (!applyLevelUvEdit(m_layout, candidate, &result, &error)) {
		m_error = error;
		announce();
		update();
		Q_EMIT editChanged();
		return false;
	}
	m_error.clear();
	m_edit = candidate;
	m_result = std::move(result);
	announce();
	update();
	Q_EMIT editChanged();
	return true;
}

void LevelUvView::record(const LevelUvEdit& before)
{
	m_undo.append(before);
	if (m_undo.size() > 256) {
		m_undo.removeFirst();
	}
	m_redo.clear();
	Q_EMIT stateChanged();
}

QVector<int> LevelUvView::turningPoints() const
{
	if (m_layout.isPatch() && !m_selected.isEmpty()) {
		return m_selected;
	}
	QVector<int> all;
	for (int i = 0; i < m_result.uv.size(); ++i) {
		all.append(i);
	}
	return all;
}

QPointF LevelUvView::pivotFor(const QVector<int>& points) const
{
	if (m_anchor >= 0 && m_anchor < m_result.uv.size()) {
		return m_result.uv[m_anchor];
	}
	QPointF sum;
	for (const int point : points) {
		sum += m_result.uv.value(point);
	}
	return points.isEmpty() ? sum : sum / static_cast<double>(points.size());
}

bool LevelUvView::nudge(QPointF texels)
{
	if (!m_hasSource || dragging() || !finitePoint(texels) || (texels.x() == 0 && texels.y() == 0)) {
		return false;
	}
	const LevelUvEdit before = m_edit;
	LevelUvEdit next = m_edit;
	const LevelUvTransform move = LevelUvTransform::translation(texels);
	if (m_layout.isPatch()) {
		next.patchUv = levelUvPatchTransformed(m_result.uv, m_selected, move);
	} else {
		next.transform = m_edit.transform.then(move);
	}
	if (!setWorkingEdit(next)) {
		return false;
	}
	record(before);
	return true;
}

bool LevelUvView::rotate(double clockwiseDegrees)
{
	if (!m_hasSource || dragging() || !std::isfinite(clockwiseDegrees) || clockwiseDegrees == 0) {
		return false;
	}
	const LevelUvEdit before = m_edit;
	LevelUvEdit next = m_edit;
	const QVector<int> points = turningPoints();
	const LevelUvTransform turn = LevelUvTransform::rotation(pivotFor(points), clockwiseDegrees);
	if (m_layout.isPatch()) {
		next.patchUv = levelUvPatchTransformed(m_result.uv, m_selected, turn);
	} else {
		next.transform = m_edit.transform.then(turn);
		next.anchor = m_anchor;
	}
	if (!setWorkingEdit(next)) {
		return false;
	}
	record(before);
	return true;
}

bool LevelUvView::movePointTo(int point, QPointF texel)
{
	if (!m_hasSource || dragging() || point < 0 || point >= m_result.uv.size() || !finitePoint(texel) || m_result.uv[point] == texel) {
		return false;
	}
	const LevelUvEdit before = m_edit;
	LevelUvEdit next = m_edit;
	if (m_layout.isPatch()) {
		next.patchUv = m_result.uv;
		next.patchUv[point] = texel;
	} else {
		next.transform = m_edit.transform.then(LevelUvTransform::translation(texel - m_result.uv[point]));
	}
	if (!setWorkingEdit(next)) {
		return false;
	}
	m_current = point;
	record(before);
	return true;
}

bool LevelUvView::resetEdit()
{
	if (!m_hasSource || dragging() || sameEdit(m_edit, LevelUvEdit {})) {
		return false;
	}
	const LevelUvEdit before = m_edit;
	if (!setWorkingEdit({})) {
		return false;
	}
	record(before);
	return true;
}

bool LevelUvView::undo()
{
	if (m_undo.isEmpty() || dragging()) {
		return false;
	}
	const LevelUvEdit previous = m_undo.takeLast();
	const LevelUvEdit current = m_edit;
	if (!setWorkingEdit(previous)) {
		m_undo.append(previous);
		return false;
	}
	m_redo.append(current);
	Q_EMIT stateChanged();
	return true;
}

bool LevelUvView::redo()
{
	if (m_redo.isEmpty() || dragging()) {
		return false;
	}
	const LevelUvEdit next = m_redo.takeLast();
	const LevelUvEdit current = m_edit;
	if (!setWorkingEdit(next)) {
		m_redo.append(next);
		return false;
	}
	m_undo.append(current);
	Q_EMIT stateChanged();
	return true;
}

QRectF LevelUvView::contentBounds() const
{
	// QRectF::united() skips empty rectangles, so gather the extremes.
	double left = std::numeric_limits<double>::infinity(), top = left, right = -left, bottom = -left;
	for (const QVector<QPointF>* list : {&m_layout.uv, &m_result.uv}) {
		for (const QPointF& point : *list) {
			if (finitePoint(point)) {
				left = std::min(left, point.x());
				right = std::max(right, point.x());
				top = std::min(top, point.y());
				bottom = std::max(bottom, point.y());
			}
		}
	}
	return left <= right ? QRectF(QPointF(left, top), QPointF(right, bottom)) : QRectF();
}

void LevelUvView::frameAll()
{
	if (!m_hasSource) {
		return;
	}
	QRectF bounds = contentBounds();
	// At least a few texels, and a fifth more than the points, as
	// TexTool's view frames them.
	const QPointF centre = bounds.center();
	const double width = std::max(bounds.width(), 8.0) * 1.2, height = std::max(bounds.height(), 8.0) * 1.2;
	bounds = QRectF(centre.x() - width / 2, centre.y() - height / 2, width, height);
	m_centre = centre;
	// Framed again on every resize until the user zooms or pans.
	m_userCamera = false;
	if (this->width() < 8 || this->height() < 8) {
		return;
	}
	// With handles, room for the turn ring about a pivot on the outline.
	const double margin = m_handles && !m_layout.isPatch() ? std::min(ringRadius() + 10.0, std::min(this->width(), this->height()) / 4.0) : 0.0;
	m_zoom = std::clamp(std::min((this->width() - 2 * margin) / bounds.width(), (this->height() - 2 * margin) / bounds.height()), kMinZoom, kMaxZoom);
	update();
}

void LevelUvView::zoomBy(double factor, QPointF screenPoint)
{
	if (!std::isfinite(factor) || factor <= 0) {
		return;
	}
	if (!finitePoint(screenPoint)) {
		screenPoint = QRectF(rect()).center();
	}
	const QPointF anchored = screenToTexel(screenPoint);
	m_zoom = std::clamp(m_zoom * factor, kMinZoom, kMaxZoom);
	m_centre = anchored - (screenPoint - QRectF(rect()).center()) / m_zoom;
	m_userCamera = true;
	update();
}

void LevelUvView::zoomBy(double factor)
{
	zoomBy(factor, QPointF(std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()));
}

void LevelUvView::panBy(QPointF screenDelta)
{
	if (!finitePoint(screenDelta)) {
		return;
	}
	m_centre -= screenDelta / m_zoom;
	m_userCamera = true;
	update();
}

QPointF LevelUvView::texelToScreen(QPointF texel) const
{
	return (texel - m_centre) * m_zoom + QRectF(rect()).center();
}

QPointF LevelUvView::screenToTexel(QPointF screen) const
{
	return (screen - QRectF(rect()).center()) / m_zoom + m_centre;
}

int LevelUvView::pointAt(QPointF screen, double tolerance) const
{
	int found = -1;
	double best = tolerance * tolerance;
	for (int i = 0; i < m_result.uv.size(); ++i) {
		const QPointF delta = texelToScreen(m_result.uv[i]) - screen;
		const double distance = QPointF::dotProduct(delta, delta);
		if (distance < best || (distance == best && i == m_current)) {
			best = distance;
			found = i;
		}
	}
	return found;
}

QString LevelUvView::summary() const
{
	if (!m_hasSource) {
		return m_message.isEmpty() ? tr("No face or patch is open in the UV Editor.") : m_message;
	}
	const int count = static_cast<int>(m_result.uv.size());
	if (m_handles && !m_layout.isPatch()) {
		// The Face section: where the pivot is, and whether a drag is pending.
		QString text = tr("Brush %1, face %2: %n point(s) over %3.", nullptr, count).arg(m_layout.target.objectId).arg(m_layout.target.faceIndex + 1).arg(m_layout.material);
		const QPointF pivot = pivotTexel();
		if (finitePoint(pivot)) {
			text += QLatin1Char(' ') + tr("Pivot at %1, %2 texels.").arg(localeNumber(pivot.x(), 7), localeNumber(pivot.y(), 7));
		}
		if (m_current >= 0 && m_current < count) {
			const QPointF at = m_result.uv[m_current];
			text += QLatin1Char(' ') + tr("Point %1 at %2, %3 texels.").arg(m_current + 1).arg(localeNumber(at.x(), 7), localeNumber(at.y(), 7));
		}
		if (!m_error.isEmpty()) {
			text += QLatin1Char(' ') + m_error;
		}
		return text;
	}
	QString text = m_layout.isPatch()
		? tr("Patch %1, %2 by %3 control points, over %4.").arg(m_layout.target.objectId).arg(m_layout.columns).arg(m_layout.rows).arg(m_layout.material)
		: tr("Brush %1, face %2: %n point(s) over %3.", nullptr, count).arg(m_layout.target.objectId).arg(m_layout.target.faceIndex + 1).arg(m_layout.material);
	if (m_current >= 0 && m_current < count) {
		const QPointF at = m_result.uv[m_current];
		text += QLatin1Char(' ') + tr("Point %1 at %2, %3 texels.").arg(m_current + 1).arg(localeNumber(at.x(), 7), localeNumber(at.y(), 7));
	}
	if (m_anchor >= 0) {
		text += QLatin1Char(' ') + tr("Anchor: point %1.").arg(m_anchor + 1);
	}
	if (m_layout.isPatch()) {
		const int selected = static_cast<int>(m_selected.size());
		text += QLatin1Char(' ') + tr("%n point(s) selected.", nullptr, selected);
	}
	if (!m_error.isEmpty()) {
		return text + QLatin1Char(' ') + m_error;
	}
	switch (m_result.fit) {
	case LevelUvFit::Unchanged:
		text += QLatin1Char(' ') + tr("No change yet.");
		break;
	case LevelUvFit::Exact:
		text += QLatin1Char(' ') + tr("Changed; Apply makes it one undo step.");
		break;
	case LevelUvFit::Converted:
		text += QLatin1Char(' ') + tr("Changed; this classic face becomes a Valve 220 face to hold the turn.");
		break;
	case LevelUvFit::Approximated:
		text += QLatin1Char(' ')
			+ tr("Changed, as nearly as a classic face can hold the turn. Allow Valve 220 conversion to keep it exactly.");
		break;
	}
	return text;
}

void LevelUvView::announce()
{
	setAccessibleDescription(summary());
}

QSize LevelUvView::minimumSizeHint() const
{
	return {240, 200};
}

QSize LevelUvView::sizeHint() const
{
	return {640, 520};
}

void LevelUvView::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.fillRect(rect(), palette().color(QPalette::Base));
	if (!m_hasSource) {
		painter.setPen(palette().color(QPalette::Text));
		painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, summary());
		return;
	}
	// The image, tiled as the map repeats it: image pixels to texels to the
	// screen. Nearest sampling keeps texels crisp when zoomed in.
	const QSizeF tileSize = tile();
	const QTransform toScreen = QTransform::fromTranslate(-m_centre.x(), -m_centre.y()) * QTransform::fromScale(m_zoom, m_zoom)
		* QTransform::fromTranslate(width() / 2.0, height() / 2.0);
	const QImage& source = m_image.isNull() ? m_checker : m_image;
	QBrush brush(source);
	brush.setTransform(QTransform::fromScale(tileSize.width() / source.width(), tileSize.height() / source.height()) * toScreen);
	painter.fillRect(rect(), brush);

	const QPointF topLeft = screenToTexel(QPointF(0, 0)), bottomRight = screenToTexel(QPointF(width(), height()));
	const auto lines = [&](double step, const QPen& pen) {
		if (step * m_zoom < 8 || (bottomRight.x() - topLeft.x()) / step > kLineLimit || (bottomRight.y() - topLeft.y()) / step > kLineLimit) {
			return;
		}
		painter.setPen(pen);
		for (double s = std::ceil(topLeft.x() / step) * step; s <= bottomRight.x(); s += step) {
			const double x = texelToScreen(QPointF(s, 0)).x();
			painter.drawLine(QPointF(x, 0), QPointF(x, height()));
		}
		for (double t = std::ceil(topLeft.y() / step) * step; t <= bottomRight.y(); t += step) {
			const double y = texelToScreen(QPointF(0, t)).y();
			painter.drawLine(QPointF(0, y), QPointF(width(), y));
		}
	};
	// The snap grid faintly, then each repeat's edge.
	if (m_snap > 0 && m_snap * m_zoom >= 12) {
		lines(m_snap, QPen(QColor(255, 255, 255, 40), 0));
	}
	if (tileSize.width() == tileSize.height()) {
		lines(tileSize.width(), QPen(QColor(0, 0, 0, 120), 0, Qt::DashLine));
	} else {
		// Unequal repeats: each axis on its own.
		painter.setPen(QPen(QColor(0, 0, 0, 120), 0, Qt::DashLine));
		if (tileSize.width() * m_zoom >= 8 && (bottomRight.x() - topLeft.x()) / tileSize.width() <= kLineLimit) {
			for (double s = std::ceil(topLeft.x() / tileSize.width()) * tileSize.width(); s <= bottomRight.x(); s += tileSize.width()) {
				const double x = texelToScreen(QPointF(s, 0)).x();
				painter.drawLine(QPointF(x, 0), QPointF(x, height()));
			}
		}
		if (tileSize.height() * m_zoom >= 8 && (bottomRight.y() - topLeft.y()) / tileSize.height() <= kLineLimit) {
			for (double t = std::ceil(topLeft.y() / tileSize.height()) * tileSize.height(); t <= bottomRight.y(); t += tileSize.height()) {
				const double y = texelToScreen(QPointF(0, t)).y();
				painter.drawLine(QPointF(0, y), QPointF(width(), y));
			}
		}
	}

	painter.setRenderHint(QPainter::Antialiasing, true);
	const auto outline = [&](const QVector<QPointF>& uv) {
		QVector<QLineF> segments;
		if (m_layout.isPatch()) {
			for (int row = 0; row < m_layout.rows; ++row) {
				for (int column = 0; column < m_layout.columns; ++column) {
					const int at = row * m_layout.columns + column;
					if (column + 1 < m_layout.columns) {
						segments.append({texelToScreen(uv.value(at)), texelToScreen(uv.value(at + 1))});
					}
					if (row + 1 < m_layout.rows) {
						segments.append({texelToScreen(uv.value(at)), texelToScreen(uv.value(at + m_layout.columns))});
					}
				}
			}
		} else {
			for (int i = 0; i < uv.size(); ++i) {
				segments.append({texelToScreen(uv[i]), texelToScreen(uv[(i + 1) % uv.size()])});
			}
		}
		return segments;
	};
	// Where the points started, dashed, once they have moved.
	if (changed()) {
		painter.setPen(QPen(QColor(0, 0, 0, 160), 1.5, Qt::DashLine));
		painter.drawLines(outline(m_layout.uv));
	}
	// The current winding or grid: dark under light, readable over any image.
	const QVector<QLineF> current = outline(m_result.uv);
	painter.setPen(QPen(QColor(0, 0, 0, 220), 3.5));
	painter.drawLines(current);
	painter.setPen(QPen(QColor(255, 255, 255), 1.5));
	painter.drawLines(current);

	const QColor highlight = palette().color(QPalette::Highlight);
	const QColor text = palette().color(QPalette::Text);
	const QColor base = palette().color(QPalette::Base);
	QFont labelFont = font();
	labelFont.setPointSizeF(std::max(6.0, font().pointSizeF() * 0.85));
	painter.setFont(labelFont);
	for (int i = 0; i < m_result.uv.size(); ++i) {
		const QPointF at = texelToScreen(m_result.uv[i]);
		const bool selected = m_layout.isPatch() ? m_selected.contains(i) : (m_drag == Drag::Point && i == m_dragPoint);
		const double half = selected ? 5.5 : 4;
		// Selected points are filled; others hollow, so state never rests on
		// colour alone.
		painter.setPen(QPen(QColor(0, 0, 0), 3));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(QRectF(at.x() - half, at.y() - half, half * 2, half * 2));
		painter.setPen(QPen(selected ? highlight : text, 1.5));
		painter.setBrush(selected ? QBrush(highlight) : QBrush(base));
		painter.drawRect(QRectF(at.x() - half, at.y() - half, half * 2, half * 2));
		if (i == m_anchor) {
			// The anchor is a ringed cross.
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(QColor(0, 0, 0), 4));
			painter.drawEllipse(at, 10, 10);
			painter.setPen(QPen(highlight, 2));
			painter.drawEllipse(at, 10, 10);
			painter.drawLine(at + QPointF(-14, 0), at + QPointF(-7, 0));
			painter.drawLine(at + QPointF(7, 0), at + QPointF(14, 0));
			painter.drawLine(at + QPointF(0, -14), at + QPointF(0, -7));
			painter.drawLine(at + QPointF(0, 7), at + QPointF(0, 14));
		}
		if (i == m_current && hasFocus()) {
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(text, 1, Qt::DotLine));
			painter.drawRect(QRectF(at.x() - half - 4, at.y() - half - 4, half * 2 + 8, half * 2 + 8));
		}
		// Face points are numbered, as the point list and map edit-uv count them.
		if (!m_layout.isPatch()) {
			const QString label = QString::number(i + 1);
			const QRectF box = painter.fontMetrics().boundingRect(label).adjusted(-3, -1, 3, 1).translated((at + QPointF(9, -9)).toPoint());
			painter.setPen(Qt::NoPen);
			painter.setBrush(base);
			painter.drawRect(box);
			painter.setPen(text);
			painter.drawText(box, Qt::AlignCenter, label);
		}
	}
	if (m_drag == Drag::Band && m_moved) {
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(highlight, 1, Qt::DashLine));
		painter.drawRect(QRectF(m_press, m_last).normalized());
	}
	if (m_handles && !m_layout.isPatch()) {
		// The pivot is a ringed cross, the turn knob a disc on a ring about it,
		// and the stretch knobs squares on guide lines along U and V and a
		// diamond for both: shapes as well as colours tell them apart. Each is
		// drawn dark under light, readable over any image; the one in use is
		// filled with the highlight.
		const QPointF pivot = handlePosition(Handle::Pivot);
		if (finitePoint(pivot)) {
			const auto twice = [&painter](const QColor& over, double width, const std::function<void()>& draw) {
				painter.setPen(QPen(QColor(0, 0, 0, 220), width + 2.5));
				draw();
				painter.setPen(QPen(over, width));
				draw();
			};
			const auto fill = [&](Handle handle) { return m_handle == handle && m_drag == Drag::Handle ? QBrush(highlight) : QBrush(base); };
			painter.setBrush(Qt::NoBrush);
			const double ring = ringRadius();
			twice(QColor(255, 255, 255, 200), 1, [&] { painter.drawEllipse(pivot, ring, ring); });
			for (const Handle handle : {Handle::StretchU, Handle::StretchV}) {
				const QPointF knob = handlePosition(handle);
				if (finitePoint(knob)) {
					twice(QColor(255, 255, 255, 200), 1, [&] { painter.drawLine(pivot, knob); });
				}
			}
			twice(highlight, 2, [&] {
				painter.drawEllipse(pivot, 9, 9);
				painter.drawLine(pivot + QPointF(-14, 0), pivot + QPointF(14, 0));
				painter.drawLine(pivot + QPointF(0, -14), pivot + QPointF(0, 14));
			});
			const QPointF turn = handlePosition(Handle::Turn);
			if (finitePoint(turn)) {
				painter.setBrush(fill(Handle::Turn));
				twice(text, 1.5, [&] { painter.drawEllipse(turn, 6.5, 6.5); });
			}
			for (const Handle handle : {Handle::StretchU, Handle::StretchV}) {
				const QPointF knob = handlePosition(handle);
				if (finitePoint(knob)) {
					painter.setBrush(fill(handle));
					twice(text, 1.5, [&] { painter.drawRect(QRectF(knob.x() - 5.5, knob.y() - 5.5, 11, 11)); });
				}
			}
			const QPointF both = handlePosition(Handle::StretchBoth);
			if (finitePoint(both)) {
				painter.setBrush(fill(Handle::StretchBoth));
				const QPolygonF diamond({both + QPointF(0, -7.5), both + QPointF(7.5, 0), both + QPointF(0, 7.5), both + QPointF(-7.5, 0)});
				twice(text, 1.5, [&] { painter.drawPolygon(diamond); });
			}
			painter.setBrush(Qt::NoBrush);
		}
	}
	if (hasFocus()) {
		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(highlight, 2, Qt::DashLine));
		painter.drawRect(rect().adjusted(1, 1, -2, -2));
	}
}

void LevelUvView::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	if (!m_userCamera) {
		frameAll();
	}
}

void LevelUvView::mousePressEvent(QMouseEvent* event)
{
	setFocus(Qt::MouseFocusReason);
	if (!m_hasSource || m_drag != Drag::None) {
		event->accept();
		return;
	}
	m_press = m_last = event->position();
	m_button = event->button();
	m_moved = false;
	const bool control = event->modifiers().testFlag(Qt::ControlModifier);
	if (m_button == Qt::RightButton || m_button == Qt::MiddleButton) {
		m_drag = Drag::Pan;
		event->accept();
		return;
	}
	if (m_button != Qt::LeftButton) {
		QWidget::mousePressEvent(event);
		return;
	}
	if (m_handles && !m_layout.isPatch()) {
		// A handle first, then a point or the inside of the face, which move
		// the texture.
		const Handle handle = handleAt(m_press);
		if (handle != Handle::None && (handle == Handle::Pivot || faceValues(&m_dragValues))) {
			m_handle = handle;
			m_drag = Drag::Handle;
			m_dragPivot = pivotTexel();
			m_dragKnob = screenToTexel(handlePosition(handle));
			m_dragRate = handle == Handle::Turn ? turnRate(m_dragValues) : 1;
		} else if (const int point = pointAt(m_press); point >= 0) {
			m_drag = Drag::Point;
			m_dragPoint = point;
			m_current = point;
			announce();
			Q_EMIT stateChanged();
		} else if (QPolygonF(m_result.uv).containsPoint(screenToTexel(m_press), Qt::OddEvenFill)) {
			m_drag = Drag::Grab;
		}
		m_dragStartEdit = m_edit;
		m_dragStartUv = m_result.uv;
		update();
		event->accept();
		return;
	}
	const int hit = pointAt(m_press);
	if (m_layout.isPatch()) {
		if (hit < 0) {
			m_drag = Drag::Band;
			m_bandAdd = control;
		} else if (control) {
			QVector<int> selected = m_selected;
			if (!selected.removeOne(hit)) {
				selected.append(hit);
			}
			m_current = hit;
			m_selected = selected;
			announce();
			update();
			Q_EMIT stateChanged();
		} else {
			if (!m_selected.contains(hit)) {
				m_selected = {hit};
			}
			m_current = hit;
			m_drag = Drag::Point;
			m_dragPoint = hit;
			announce();
			Q_EMIT stateChanged();
		}
	} else if (hit >= 0) {
		m_drag = Drag::Point;
		m_dragPoint = hit;
		m_turning = m_anchor >= 0 && hit != m_anchor;
		m_current = hit;
		announce();
		Q_EMIT stateChanged();
	} else if (QPolygonF(m_result.uv).containsPoint(screenToTexel(m_press), Qt::OddEvenFill)) {
		m_drag = Drag::Grab;
	}
	m_dragStartEdit = m_edit;
	m_dragStartUv = m_result.uv;
	update();
	event->accept();
}

void LevelUvView::mouseMoveEvent(QMouseEvent* event)
{
	if (m_drag == Drag::None) {
		QWidget::mouseMoveEvent(event);
		return;
	}
	const QPointF position = event->position();
	if (!m_moved && (position - m_press).manhattanLength() < QApplication::startDragDistance()) {
		event->accept();
		return;
	}
	m_moved = true;
	switch (m_drag) {
	case Drag::Pan:
		setCursor(Qt::ClosedHandCursor);
		panBy(position - m_last);
		break;
	case Drag::Handle:
		if (m_handle == Handle::Pivot) {
			// The pivot snaps to the face's points, or else to the snap grid.
			setCursor(Qt::CrossCursor);
			const int point = pointAt(position, 10);
			const QPointF to = screenToTexel(position);
			setPivotTexel(point >= 0 ? m_result.uv[point] : m_snap > 0 ? snapLevelUv(to, m_snap) : to);
		} else {
			setCursor(m_handle == Handle::Turn ? Qt::ClosedHandCursor : Qt::SizeAllCursor);
			previewHandleEdit(handleDragEdit(position, event->modifiers()));
		}
		break;
	case Drag::Point: {
		setCursor(Qt::SizeAllCursor);
		QPointF to = screenToTexel(position);
		if (m_snap > 0) {
			to = snapLevelUv(to, m_snap);
		}
		if (m_handles) {
			LevelFaceAttributesEdit move;
			move.moveTexels = to - m_dragStartUv.value(m_dragPoint);
			previewHandleEdit(move);
			break;
		}
		LevelUvEdit next = m_dragStartEdit;
		if (m_layout.isPatch()) {
			next.patchUv = levelUvPatchDrag(m_dragStartUv, m_selected, m_dragPoint, to);
		} else {
			const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? m_angleStep : 0;
			next.transform = m_dragStartEdit.transform.then(levelUvFaceDrag(m_dragStartUv, m_dragPoint, m_turning ? m_anchor : -1, to, step));
			if (m_turning) {
				next.anchor = m_anchor;
			}
		}
		setWorkingEdit(next);
		break;
	}
	case Drag::Grab: {
		setCursor(Qt::ClosedHandCursor);
		QPointF delta = screenToTexel(position) - screenToTexel(m_press);
		if (m_snap > 0) {
			delta = snapLevelUv(delta, m_snap);
		}
		if (m_handles) {
			LevelFaceAttributesEdit move;
			move.moveTexels = delta;
			previewHandleEdit(move);
			break;
		}
		LevelUvEdit next = m_dragStartEdit;
		next.transform = m_dragStartEdit.transform.then(LevelUvTransform::translation(delta));
		setWorkingEdit(next);
		break;
	}
	case Drag::Band:
		update();
		break;
	case Drag::None:
		break;
	}
	m_last = position;
	event->accept();
}

void LevelUvView::finishDrag(bool commit)
{
	const Drag drag = m_drag;
	m_drag = Drag::None;
	m_turning = false;
	unsetCursor();
	if (m_handles && (drag == Drag::Point || drag == Drag::Grab || drag == Drag::Handle)) {
		// Cancelled: the face as it was. A finished drag is reported on release.
		m_handle = Handle::None;
		if (!commit) {
			previewHandleEdit({});
			m_handleEdit = {};
		}
		update();
		return;
	}
	if (drag == Drag::Point || drag == Drag::Grab) {
		if (!commit) {
			setWorkingEdit(m_dragStartEdit);
		} else if (!sameEdit(m_edit, m_dragStartEdit)) {
			record(m_dragStartEdit);
		}
	}
	update();
}

void LevelUvView::mouseReleaseEvent(QMouseEvent* event)
{
	if (m_drag == Drag::None || event->button() != m_button) {
		QWidget::mouseReleaseEvent(event);
		return;
	}
	const Drag drag = m_drag;
	const bool moved = m_moved;
	if (drag == Drag::Band) {
		// Points inside the band, added to the selection with Ctrl.
		QVector<int> selected = m_bandAdd ? m_selected : QVector<int> {};
		if (moved) {
			const QRectF band = QRectF(m_press, event->position()).normalized();
			for (int i = 0; i < m_result.uv.size(); ++i) {
				if (band.contains(texelToScreen(m_result.uv[i])) && !selected.contains(i)) {
					selected.append(i);
				}
			}
		}
		m_drag = Drag::None;
		if (selected != m_selected) {
			m_selected = selected;
			if (!selected.isEmpty() && !selected.contains(m_current)) {
				m_current = selected.first();
			}
			announce();
			Q_EMIT stateChanged();
		}
		update();
	} else if (drag == Drag::Pan) {
		m_drag = Drag::None;
		unsetCursor();
		if (!moved && m_button == Qt::RightButton) {
			m_menu->popup(event->globalPosition().toPoint());
		}
	} else if (m_handles) {
		// One change per drag, committed by the inspector; the pivot only moves.
		const Handle handle = m_handle;
		const LevelFaceAttributesEdit edit = m_handleEdit;
		finishDrag(true);
		if (moved && handle != Handle::Pivot) {
			reportHandleEdit(edit);
		}
		announce();
		Q_EMIT stateChanged();
	} else {
		const int point = m_dragPoint;
		finishDrag(true);
		if (drag == Drag::Point && !m_layout.isPatch()) {
			// The point dragged last is the anchor for the next turn; a click
			// on the anchor clears it, as a click elsewhere moves it.
			m_anchor = moved || m_anchor != point ? point : -1;
		}
		announce();
		Q_EMIT stateChanged();
	}
	m_button = Qt::NoButton;
	event->accept();
}

void LevelUvView::wheelEvent(QWheelEvent* event)
{
	if (m_hasSource && m_drag == Drag::None && event->angleDelta().y() != 0) {
		zoomBy(std::pow(1.2, event->angleDelta().y() / 120.0), event->position());
	}
	event->accept();
}

void LevelUvView::keyPressEvent(QKeyEvent* event)
{
	if (m_drag != Drag::None) {
		if (event->key() == Qt::Key_Escape) {
			finishDrag(false);
			m_button = Qt::NoButton;
			announce();
		}
		event->accept();
		return;
	}
	if (!m_hasSource || !viewKey(event, false, m_handles)) {
		QWidget::keyPressEvent(event);
		return;
	}
	const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
	const bool shift = modifiers == Qt::ShiftModifier, control = modifiers.testFlag(Qt::ControlModifier);
	const int count = static_cast<int>(m_result.uv.size());
	if (m_handles && !m_layout.isPatch()) {
		// Moves and turns are map edits here, each one undo step; A puts the
		// pivot at the current point.
		const int key = event->key();
		if ((key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down) && !control) {
			const QPointF direction(key == Qt::Key_Left ? -1 : key == Qt::Key_Right ? 1 : 0, key == Qt::Key_Up ? -1 : key == Qt::Key_Down ? 1 : 0);
			LevelFaceAttributesEdit move;
			move.moveTexels = direction * (m_snap > 0 ? m_snap : 1) * (shift ? kCoarseNudge : 1);
			if (previewHandleEdit(move)) {
				reportHandleEdit(move);
			}
			event->accept();
			return;
		}
		if (key == Qt::Key_BracketLeft || key == Qt::Key_BracketRight || key == Qt::Key_BraceLeft || key == Qt::Key_BraceRight) {
			const double step = key == Qt::Key_BracketLeft || key == Qt::Key_BracketRight ? m_angleStep : 1;
			turnTexture(key == Qt::Key_BracketLeft || key == Qt::Key_BraceLeft ? -step : step);
			event->accept();
			return;
		}
		if (key == Qt::Key_A && modifiers == Qt::NoModifier) {
			if (m_current >= 0 && m_current < count) {
				setPivotTexel(m_result.uv[m_current]);
			}
			event->accept();
			return;
		}
	}
	switch (event->key()) {
	case Qt::Key_Left:
	case Qt::Key_Right:
	case Qt::Key_Up:
	case Qt::Key_Down: {
		const int key = event->key();
		const QPointF direction(key == Qt::Key_Left ? -1 : key == Qt::Key_Right ? 1 : 0, key == Qt::Key_Up ? -1 : key == Qt::Key_Down ? 1 : 0);
		if (control) {
			panBy(-direction * kKeyPan);
		} else {
			nudge(direction * (m_snap > 0 ? m_snap : 1) * (shift ? kCoarseNudge : 1));
		}
		break;
	}
	case Qt::Key_BracketLeft:
		rotate(-m_angleStep);
		break;
	case Qt::Key_BracketRight:
		rotate(m_angleStep);
		break;
	case Qt::Key_BraceLeft:
		rotate(-1);
		break;
	case Qt::Key_BraceRight:
		rotate(1);
		break;
	case Qt::Key_Plus:
	case Qt::Key_Equal:
		zoomBy(1.25);
		break;
	case Qt::Key_Minus:
	case Qt::Key_Underscore:
		zoomBy(0.8);
		break;
	case Qt::Key_Insert:
		// TexTool's keys: Insert zooms out and Delete zooms in.
		zoomBy(0.5);
		break;
	case Qt::Key_Delete:
		zoomBy(2);
		break;
	case Qt::Key_Home:
	case Qt::Key_F:
		frameAll();
		break;
	case Qt::Key_N:
		if (count > 0) {
			const int next = ((m_current < 0 ? 0 : m_current) + (shift ? count - 1 : 1)) % count;
			if (m_layout.isPatch()) {
				m_selected = {next};
			}
			m_current = -1;
			setCurrentPoint(next);
		}
		break;
	case Qt::Key_A:
		if (control) {
			QVector<int> all;
			for (int i = 0; i < count; ++i) {
				all.append(i);
			}
			setSelectedPoints(all);
		} else if (m_current >= 0) {
			setAnchor(m_anchor == m_current ? -1 : m_current);
		}
		break;
	case Qt::Key_Z:
		if (modifiers.testFlag(Qt::ShiftModifier)) {
			redo();
		} else {
			undo();
		}
		break;
	case Qt::Key_Y:
		redo();
		break;
	default:
		break;
	}
	event->accept();
}

void LevelUvView::focusOutEvent(QFocusEvent* event)
{
	if (m_drag == Drag::Point || m_drag == Drag::Grab || m_drag == Drag::Handle) {
		finishDrag(false);
	}
	m_drag = Drag::None;
	m_handle = Handle::None;
	m_button = Qt::NoButton;
	unsetCursor();
	update();
	QWidget::focusOutEvent(event);
}

void LevelUvView::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::PaletteChange || event->type() == QEvent::FontChange || event->type() == QEvent::LanguageChange) {
		announce();
		update();
	}
}

bool LevelUvView::event(QEvent* event)
{
	if (event->type() == QEvent::ShortcutOverride && m_hasSource) {
		const auto* key = static_cast<QKeyEvent*>(event);
		if (viewKey(key, m_drag != Drag::None, m_handles)) {
			event->accept();
			return true;
		}
	}
	return QWidget::event(event);
}

} // namespace vibestudio
