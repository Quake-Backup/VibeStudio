// Vertex editing in the 2D views, after Radiant's vertex, edge and face
// modes and TrenchBroom's vertex tool, and Radiant's Region Set XY drawing.
// The viewport only picks and previews; the shell commits a move through
// moveLevelMapComponents() as one undo step.

#include "app/map_viewport.h"
#include "app/map_viewport_p.h"

#include "core/level_brush.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace vibestudio {

namespace {

constexpr double kHandlePickPixels = 7.0;
constexpr double kHandleStackPixels = 0.75;
constexpr double kComponentDragPixels = 4.0;
constexpr int kComponentObjectLimit = 128;
constexpr int kComponentHandleLimit = 16384;

bool sameRef(const LevelMapSelectionRef& a, const LevelMapSelectionRef& b) { return a.kind == b.kind && a.objectId == b.objectId; }

bool samePosition(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return std::abs(a.x - b.x) < 0.01 && std::abs(a.y - b.y) < 0.01 && std::abs(a.z - b.z) < 0.01;
}

} // namespace

void MapViewport::setComponentMode(ComponentMode mode)
{
	if (mode != ComponentMode::Off && (!m_hasDocument || m_document.format == LevelMapFormat::DoomWad)) {
		mode = ComponentMode::Off;
	}
	if (mode == m_componentMode) {
		return;
	}
	// Picks carry over between vertices, edges and faces only as positions,
	// which rarely match; start each mode clean.
	m_componentMode = mode;
	m_componentArmed = m_componentDragging = m_componentBanding = false;
	m_componentGrabbed = -1;
	m_componentRepick.clear();
	m_componentRepickFallback.clear();
	m_pickedHandles.clear();
	if (mode != ComponentMode::Off) {
		cancelInteraction();
		setCursor(Qt::CrossCursor);
	} else {
		unsetCursor();
	}
	rebuildComponentHandles();
	setAccessibleDescription(accessibleSummary());
	Q_EMIT hoverChanged(mode == ComponentMode::Off ? hoverSummary() : componentSummary());
	update();
	Q_EMIT componentModeChanged(static_cast<int>(mode));
	Q_EMIT componentPicksChanged(0);
}

QString MapViewport::componentModeName() const
{
	switch (m_componentMode) {
	case ComponentMode::Vertices: return tr("Vertices");
	case ComponentMode::Edges: return tr("Edges");
	case ComponentMode::Faces: return tr("Faces");
	case ComponentMode::Off: break;
	}
	return {};
}

void MapViewport::rebuildComponentHandles()
{
	// Keep what is picked by object and position, so picks survive an edit
	// that renumbers a brush's vertices.
	QVector<QPair<LevelMapSelectionRef, LevelMapVec3>> wanted = m_componentRepick;
	QVector<QPair<LevelMapSelectionRef, LevelMapVec3>> fallback = m_componentRepickFallback;
	if (wanted.isEmpty() && fallback.isEmpty()) {
		wanted = pickedComponentPositions();
	}
	m_componentRepick.clear();
	m_componentRepickFallback.clear();
	m_componentHandles.clear();
	m_pickedHandles.clear();
	if (m_componentMode == ComponentMode::Off || !m_hasDocument || m_document.format == LevelMapFormat::DoomWad) {
		return;
	}
	QSet<int> brushes, patches, entities;
	for (const LevelMapSelectionRef& ref : std::as_const(m_selection)) {
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			brushes.insert(ref.objectId);
		} else if (ref.kind == LevelMapSelectionKind::QuakePatch) {
			patches.insert(ref.objectId);
		} else if (ref.kind == LevelMapSelectionKind::Entity) {
			entities.insert(ref.objectId);
		}
	}
	int objects = 0;
	const auto full = [this, &objects]() {
		return objects >= kComponentObjectLimit || m_componentHandles.size() >= kComponentHandleLimit;
	};
	for (const LevelMapBrush& brush : std::as_const(m_document.brushes)) {
		if (full()) {
			break;
		}
		if (!brushes.contains(brush.id) && !entities.contains(brush.entityId)) {
			continue;
		}
		LevelBrushTopology topology;
		if (!levelBrushTopology(brush, &topology)) {
			continue;
		}
		++objects;
		const LevelMapSelectionRef ref {LevelMapSelectionKind::QuakeBrush, brush.id};
		if (m_componentMode == ComponentMode::Vertices) {
			for (int i = 0; i < topology.vertices.size(); ++i) {
				m_componentHandles.push_back({ref, i, topology.vertices.at(i)});
			}
		} else if (m_componentMode == ComponentMode::Edges) {
			for (int i = 0; i < topology.edges.size(); ++i) {
				const LevelMapVec3& a = topology.vertices.at(topology.edges.at(i)[0]);
				const LevelMapVec3& b = topology.vertices.at(topology.edges.at(i)[1]);
				m_componentHandles.push_back({ref, i, {(a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2, true}});
			}
		} else {
			for (int i = 0; i < topology.faces.size(); ++i) {
				LevelMapVec3 centre {0, 0, 0, true};
				const QVector<int>& face = topology.faces.at(i);
				for (const int vertex : face) {
					centre.x += topology.vertices.at(vertex).x / face.size();
					centre.y += topology.vertices.at(vertex).y / face.size();
					centre.z += topology.vertices.at(vertex).z / face.size();
				}
				if (!face.isEmpty()) {
					m_componentHandles.push_back({ref, i, centre});
				}
			}
		}
	}
	// A patch shows its control points in every mode.
	for (const LevelMapPatch& patch : std::as_const(m_document.patches)) {
		if (full()) {
			break;
		}
		if (!patches.contains(patch.id) && !entities.contains(patch.entityId)) {
			continue;
		}
		++objects;
		for (int i = 0; i < patch.controlPoints.size(); ++i) {
			m_componentHandles.push_back({{LevelMapSelectionKind::QuakePatch, patch.id}, i, patch.controlPoints.at(i)});
		}
	}
	const auto repick = [this](const QVector<QPair<LevelMapSelectionRef, LevelMapVec3>>& picks) {
		for (int i = 0; i < m_componentHandles.size(); ++i) {
			for (const auto& pick : picks) {
				if (sameRef(pick.first, m_componentHandles.at(i).object) && samePosition(pick.second, m_componentHandles.at(i).position)) {
					m_pickedHandles.insert(i);
					break;
				}
			}
		}
	};
	repick(wanted);
	if (m_pickedHandles.isEmpty()) {
		repick(fallback);
	}
}

QPointF MapViewport::componentHandlePosition(int index) const
{
	if (index < 0 || index >= m_componentHandles.size()) {
		return {};
	}
	const QPointF plane = projectPoint(m_componentHandles.at(index).position);
	return worldToView(plane.x(), plane.y());
}

QVector<LevelMapComponentSet> MapViewport::pickedComponents() const
{
	QVector<LevelMapComponentSet> sets;
	QList<int> picked(m_pickedHandles.cbegin(), m_pickedHandles.cend());
	std::sort(picked.begin(), picked.end());
	const LevelBrushComponent kind = m_componentMode == ComponentMode::Edges ? LevelBrushComponent::Edge
		: m_componentMode == ComponentMode::Faces ? LevelBrushComponent::Face : LevelBrushComponent::Vertex;
	for (const int index : std::as_const(picked)) {
		const ComponentHandle& handle = m_componentHandles.at(index);
		auto set = std::find_if(sets.begin(), sets.end(), [&handle](const LevelMapComponentSet& s) { return sameRef(s.object, handle.object); });
		if (set == sets.end()) {
			LevelMapComponentSet added;
			added.object = handle.object;
			added.kind = handle.object.kind == LevelMapSelectionKind::QuakePatch ? LevelBrushComponent::Vertex : kind;
			sets.push_back(added);
			set = sets.end() - 1;
		}
		set->components.push_back(handle.component);
	}
	return sets;
}

QVector<QPair<LevelMapSelectionRef, LevelMapVec3>> MapViewport::pickedComponentPositions() const
{
	QVector<QPair<LevelMapSelectionRef, LevelMapVec3>> picks;
	for (const int index : std::as_const(m_pickedHandles)) {
		if (index >= 0 && index < m_componentHandles.size()) {
			picks.push_back({m_componentHandles.at(index).object, m_componentHandles.at(index).position});
		}
	}
	return picks;
}

void MapViewport::setPickedComponentPositions(const QVector<QPair<LevelMapSelectionRef, LevelMapVec3>>& picks)
{
	m_pickedHandles.clear();
	for (int i = 0; i < m_componentHandles.size(); ++i) {
		for (const auto& pick : picks) {
			if (sameRef(pick.first, m_componentHandles.at(i).object) && samePosition(pick.second, m_componentHandles.at(i).position)) {
				m_pickedHandles.insert(i);
				break;
			}
		}
	}
	setAccessibleDescription(accessibleSummary());
	update();
}

void MapViewport::pickAllComponents()
{
	for (int i = 0; i < m_componentHandles.size(); ++i) {
		m_pickedHandles.insert(i);
	}
	Q_EMIT hoverChanged(componentSummary());
	Q_EMIT componentPicksChanged(pickedComponentCount());
	update();
}

int MapViewport::componentHandleAt(const QPointF& viewPoint) const
{
	int best = -1;
	double bestDistance = kHandlePickPixels * (m_highContrast ? 1.3 : 1.0);
	for (int i = 0; i < m_componentHandles.size(); ++i) {
		const QPointF at = componentHandlePosition(i);
		const double distance = std::hypot(at.x() - viewPoint.x(), at.y() - viewPoint.y());
		// Ties go to a handle already picked, so a drag can start on any
		// picked one of a stack.
		if (distance < bestDistance || (best >= 0 && distance == bestDistance && m_pickedHandles.contains(i) && !m_pickedHandles.contains(best))) {
			best = i;
			bestDistance = distance;
		}
	}
	return best;
}

QVector<int> MapViewport::componentHandlesStackedAt(int handle) const
{
	// Every handle this view draws at the same spot: a box's top and bottom
	// corners from above, or a corner two brushes share.
	QVector<int> stacked;
	const QPointF at = componentHandlePosition(handle);
	for (int i = 0; i < m_componentHandles.size(); ++i) {
		const QPointF other = componentHandlePosition(i);
		if (std::hypot(other.x() - at.x(), other.y() - at.y()) <= kHandleStackPixels) {
			stacked.push_back(i);
		}
	}
	return stacked;
}

QPointF MapViewport::componentDragDelta() const
{
	if (!m_componentDragging) {
		return {};
	}
	return m_componentDragPlane - m_componentGrabPlane;
}

bool MapViewport::componentMousePress(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton) {
		return false;
	}
	const Qt::KeyboardModifiers keys = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
	const bool toggle = modifiersHeld(keys, m_controls.toggleModifiers);
	const bool extend = modifiersHeld(keys, m_controls.addModifiers) || (m_controls.bandModifiers != Qt::NoModifier && keys == m_controls.bandModifiers);
	m_componentPressView = event->position();
	m_componentModifiers = keys;
	const int hit = componentHandleAt(event->position());
	if (hit < 0) {
		// Over empty space a drag bands handles; a click (decided on release)
		// may pick an object instead.
		m_componentBanding = true;
		m_componentBandView = event->position();
		update();
		event->accept();
		return true;
	}
	const QVector<int> stacked = componentHandlesStackedAt(hit);
	bool changed = false;
	if (toggle) {
		const bool allPicked = std::all_of(stacked.cbegin(), stacked.cend(), [this](int i) { return m_pickedHandles.contains(i); });
		for (const int i : stacked) {
			if (allPicked) {
				m_pickedHandles.remove(i);
			} else {
				m_pickedHandles.insert(i);
			}
		}
		changed = true;
	} else if (extend || m_pickedHandles.contains(hit)) {
		for (const int i : stacked) {
			if (!m_pickedHandles.contains(i)) {
				m_pickedHandles.insert(i);
				changed = true;
			}
		}
	} else {
		m_pickedHandles.clear();
		for (const int i : stacked) {
			m_pickedHandles.insert(i);
		}
		changed = true;
	}
	if (m_pickedHandles.contains(hit)) {
		m_componentArmed = true;
		m_componentGrabbed = hit;
		m_componentGrabPlane = projectPoint(m_componentHandles.at(hit).position);
		m_componentDragPlane = m_componentGrabPlane;
	}
	if (changed) {
		Q_EMIT componentPicksChanged(pickedComponentCount());
	}
	Q_EMIT hoverChanged(componentSummary());
	setAccessibleDescription(accessibleSummary());
	update();
	event->accept();
	return true;
}

bool MapViewport::componentMouseMove(QMouseEvent* event)
{
	const QPointF position = event->position();
	if (m_componentBanding) {
		m_componentBandView = position;
		update();
		event->accept();
		return true;
	}
	if (m_componentArmed && !m_componentDragging) {
		const QPointF travel = position - m_componentPressView;
		if (std::hypot(travel.x(), travel.y()) < kComponentDragPixels) {
			event->accept();
			return true;
		}
		m_componentArmed = false;
		m_componentDragging = true;
	}
	if (m_componentDragging) {
		// The grabbed handle follows the pointer, landing on the grid.
		QPointF target = m_componentGrabPlane + (viewToWorld(position) - viewToWorld(m_componentPressView));
		if (m_snapToGrid && m_gridSize > 0) {
			target = QPointF(snapLevelMapCoordinate(target.x(), m_gridSize), snapLevelMapCoordinate(target.y(), m_gridSize));
		}
		if (target != m_componentDragPlane) {
			m_componentDragPlane = target;
			Q_EMIT hoverChanged(componentSummary());
			update();
		}
		event->accept();
		return true;
	}
	// Hovering: say which handle the pointer rests on.
	const int hit = componentHandleAt(position);
	if (hit >= 0) {
		const ComponentHandle& handle = m_componentHandles.at(hit);
		const QString kind = handle.object.kind == LevelMapSelectionKind::QuakePatch ? tr("Control point %1 of patch %2")
			: m_componentMode == ComponentMode::Edges ? tr("Edge %1 of brush %2")
			: m_componentMode == ComponentMode::Faces ? tr("Face %1 of brush %2") : tr("Vertex %1 of brush %2");
		Q_EMIT hoverChanged(kind.arg(handle.component).arg(handle.object.objectId) + QStringLiteral(" · ")
			+ tr("X %1, Y %2, Z %3").arg(handle.position.x, 0, 'g', 8).arg(handle.position.y, 0, 'g', 8).arg(handle.position.z, 0, 'g', 8));
	} else {
		Q_EMIT hoverChanged(componentSummary());
	}
	event->accept();
	return true;
}

bool MapViewport::componentMouseRelease(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton) {
		return false;
	}
	if (m_componentBanding) {
		m_componentBanding = false;
		const QPointF travel = m_componentBandView - m_componentPressView;
		if (std::hypot(travel.x(), travel.y()) < kComponentDragPixels) {
			// A click on nothing: pick the brush or patch there, else drop the
			// picks.
			const MapViewportHit hit = hitTest(event->position());
			if ((hit.kind == LevelMapSelectionKind::QuakeBrush || hit.kind == LevelMapSelectionKind::QuakePatch)
				&& !selectionContains(hit.kind, hit.objectId)) {
				if (m_componentModifiers == Qt::NoModifier) {
					replaceSelection(hit.kind, hit.objectId);
				} else {
					addToSelection(hit.kind, hit.objectId);
				}
				announceSelection();
				rebuildComponentHandles();
			} else if (m_componentModifiers == Qt::NoModifier && !m_pickedHandles.isEmpty()) {
				m_pickedHandles.clear();
				Q_EMIT componentPicksChanged(0);
			}
		} else {
			const QRectF band = QRectF(m_componentPressView, m_componentBandView).normalized();
			const bool toggle = modifiersHeld(m_componentModifiers, m_controls.toggleModifiers);
			const bool extend = toggle || modifiersHeld(m_componentModifiers, m_controls.addModifiers)
				|| (m_controls.bandModifiers != Qt::NoModifier && m_componentModifiers == m_controls.bandModifiers);
			if (!extend) {
				m_pickedHandles.clear();
			}
			for (int i = 0; i < m_componentHandles.size(); ++i) {
				if (!band.contains(componentHandlePosition(i))) {
					continue;
				}
				if (toggle && m_pickedHandles.contains(i)) {
					m_pickedHandles.remove(i);
				} else {
					m_pickedHandles.insert(i);
				}
			}
			Q_EMIT componentPicksChanged(pickedComponentCount());
		}
		Q_EMIT hoverChanged(componentSummary());
		setAccessibleDescription(accessibleSummary());
		update();
		event->accept();
		return true;
	}
	if (m_componentDragging) {
		const QPointF delta = componentDragDelta();
		m_componentDragging = false;
		m_componentGrabbed = -1;
		if (!delta.isNull()) {
			requestComponentMove(delta);
		}
		update();
		event->accept();
		return true;
	}
	if (m_componentArmed) {
		m_componentArmed = false;
		m_componentGrabbed = -1;
		event->accept();
		return true;
	}
	return false;
}

bool MapViewport::componentKeyPress(QKeyEvent* event)
{
	switch (event->key()) {
	case Qt::Key_Left:
	case Qt::Key_Right:
	case Qt::Key_Up:
	case Qt::Key_Down: {
		const auto modifiers = event->modifiers() & ~(Qt::KeypadModifier | Qt::ShiftModifier);
		if (m_pickedHandles.isEmpty() || modifiers != Qt::NoModifier) {
			return false;
		}
		if (m_componentDragging || m_componentBanding) {
			event->accept();
			return true;
		}
		const double sign = (event->key() == Qt::Key_Left || event->key() == Qt::Key_Down) ? -1.0 : 1.0;
		const bool horizontal = event->key() == Qt::Key_Left || event->key() == Qt::Key_Right;
		const double grid = m_gridSize > 0 ? static_cast<double>(m_gridSize) : 1.0;
		const double step = sign * grid * ((event->modifiers() & Qt::ShiftModifier) != 0 ? 8 : 1);
		requestComponentMove(horizontal ? QPointF(step, 0.0) : QPointF(0.0, step));
		event->accept();
		return true;
	}
	case Qt::Key_Escape:
		if (m_componentDragging || m_componentBanding || m_componentArmed) {
			m_componentDragging = m_componentBanding = m_componentArmed = false;
			m_componentGrabbed = -1;
			Q_EMIT hoverChanged(componentSummary());
			update();
		} else if (!m_pickedHandles.isEmpty()) {
			m_pickedHandles.clear();
			Q_EMIT componentPicksChanged(0);
			Q_EMIT hoverChanged(componentSummary());
			setAccessibleDescription(accessibleSummary());
			update();
		} else {
			setComponentMode(ComponentMode::Off);
		}
		event->accept();
		return true;
	default:
		return false;
	}
}

void MapViewport::requestComponentMove(const QPointF& planeDelta)
{
	double dx = 0.0;
	double dy = 0.0;
	double dz = 0.0;
	planeDeltaToWorld(planeDelta, &dx, &dy, &dz);
	if (dx == 0.0 && dy == 0.0 && dz == 0.0) {
		return;
	}
	const QVector<LevelMapComponentSet> sets = pickedComponents();
	if (sets.isEmpty()) {
		return;
	}
	// Find the picks where the move takes them once the shell has made it,
	// or where they were if it is refused.
	m_componentRepickFallback = pickedComponentPositions();
	m_componentRepick.clear();
	for (const auto& pick : std::as_const(m_componentRepickFallback)) {
		m_componentRepick.push_back({pick.first, {pick.second.x + dx, pick.second.y + dy, pick.second.z + dz, true}});
	}
	Q_EMIT componentMoveRequested(sets, {dx, dy, dz, true});
}

QString MapViewport::componentSummary() const
{
	if (m_componentMode == ComponentMode::Off) {
		return {};
	}
	if (m_componentHandles.isEmpty()) {
		return tr("Vertex editing: select brushes or patches to show their handles.");
	}
	if (m_componentDragging) {
		const QPointF delta = componentDragDelta();
		return tr("Moving %n handle(s) by %1 %2, %3 %4", nullptr, pickedComponentCount())
			.arg(m_projection == MapViewportProjection::SideZY ? QStringLiteral("Y") : QStringLiteral("X"))
			.arg(delta.x(), 0, 'g', 8)
			.arg(m_projection == MapViewportProjection::TopXY ? QStringLiteral("Y") : QStringLiteral("Z"))
			.arg(delta.y(), 0, 'g', 8);
	}
	return tr("%1: %2 of %n handle(s) picked; drag a picked handle or use the arrow keys to move them.", nullptr, componentHandleCount())
		.arg(componentModeName())
		.arg(pickedComponentCount());
}

void MapViewport::paintComponents(QPainter& painter, const Palette& palette) const
{
	if (m_componentMode == ComponentMode::Off || m_componentHandles.isEmpty()) {
		return;
	}
	painter.save();
	const double lineWidth = m_highContrast ? 2.0 : 1.2;
	// A patch's control lattice, so its rows and columns read as a grid.
	QSet<int> patches;
	for (const ComponentHandle& handle : m_componentHandles) {
		if (handle.object.kind == LevelMapSelectionKind::QuakePatch) {
			patches.insert(handle.object.objectId);
		}
	}
	painter.setPen(QPen(palette.patch, m_highContrast ? 1.5 : 1.0, Qt::DotLine));
	for (const LevelMapPatch& patch : m_document.patches) {
		if (!patches.contains(patch.id) || patch.controlPoints.size() != patch.width * patch.height) {
			continue;
		}
		const auto view = [this](const LevelMapVec3& point) {
			const QPointF plane = projectPoint(point);
			return worldToView(plane.x(), plane.y());
		};
		for (int row = 0; row < patch.height; ++row) {
			for (int column = 0; column < patch.width; ++column) {
				const QPointF at = view(patch.controlPoints.at(row * patch.width + column));
				if (column + 1 < patch.width) {
					painter.drawLine(at, view(patch.controlPoints.at(row * patch.width + column + 1)));
				}
				if (row + 1 < patch.height) {
					painter.drawLine(at, view(patch.controlPoints.at((row + 1) * patch.width + column)));
				}
			}
		}
	}
	const QPointF moved = componentDragDelta() * m_zoom;
	for (int i = 0; i < m_componentHandles.size(); ++i) {
		const ComponentHandle& handle = m_componentHandles.at(i);
		const bool picked = m_pickedHandles.contains(i);
		QPointF at = componentHandlePosition(i);
		if (picked && m_componentDragging) {
			// Where the drag takes it, with a line back to where it was.
			const QPointF to = at + QPointF(moved.x(), -moved.y());
			painter.setPen(QPen(palette.selection, 1.0, Qt::DashLine));
			painter.drawLine(at, to);
			at = to;
		}
		// Picked handles are filled and larger; brush vertices are squares,
		// edges diamonds, faces and patch points circles, so the shapes
		// carry the meaning as well as the colours.
		const double size = picked ? 4.5 : 3.25;
		painter.setPen(QPen(picked ? palette.selection : palette.vertex, lineWidth));
		painter.setBrush(picked ? QBrush(palette.selection) : QBrush(palette.background));
		if (handle.object.kind == LevelMapSelectionKind::QuakePatch || m_componentMode == ComponentMode::Faces) {
			painter.drawEllipse(at, size, size);
		} else if (m_componentMode == ComponentMode::Edges) {
			QPainterPath diamond;
			diamond.moveTo(at.x(), at.y() - size - 1);
			diamond.lineTo(at.x() + size + 1, at.y());
			diamond.lineTo(at.x(), at.y() + size + 1);
			diamond.lineTo(at.x() - size - 1, at.y());
			diamond.closeSubpath();
			painter.drawPath(diamond);
		} else {
			painter.drawRect(QRectF(at.x() - size, at.y() - size, size * 2, size * 2));
		}
	}
	if (m_componentBanding) {
		painter.setPen(QPen(palette.selection, 1.0, Qt::DashLine));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(QRectF(m_componentPressView, m_componentBandView).normalized());
	}
	painter.restore();
}

void MapViewport::setRegionDrawMode(bool enabled)
{
	enabled = enabled && m_hasDocument && m_document.format != LevelMapFormat::DoomWad;
	if (enabled == m_regionDrawMode) {
		return;
	}
	cancelInteraction();
	m_regionDrawMode = enabled;
	if (enabled) {
		setCursor(Qt::CrossCursor);
	} else if (m_componentMode == ComponentMode::Off) {
		unsetCursor();
	}
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT regionDrawModeChanged(enabled);
}

} // namespace vibestudio
