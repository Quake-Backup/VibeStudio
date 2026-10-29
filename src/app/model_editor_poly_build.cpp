#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_viewport.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

// Poly Build: build faces corner by corner in any view, the way MilkShape
// 3D's vertex and face tools and Blender's Poly Build tool do (behaviour only,
// see docs/MODEL_TOOLS.md). Each face is one Add Face edit and one undo step.
namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelTools)
};
// How near, in pixels, a click must be to reuse an existing vertex.
constexpr double reach = 12;
bool viewPlaneHit(const ModelPickRay &ray, ModelVec3 point, ModelVec3 normal, ModelVec3 *hit)
{
	const double denominator = double(ray.direction.x) * normal.x + double(ray.direction.y) * normal.y + double(ray.direction.z) * normal.z;
	if (std::abs(denominator) < 1e-12)
		return false;
	const double t = ((double(point.x) - ray.origin.x) * normal.x + (double(point.y) - ray.origin.y) * normal.y +
					  (double(point.z) - ray.origin.z) * normal.z) /
					 denominator;
	*hit = {float(ray.origin.x + ray.direction.x * t), float(ray.origin.y + ray.direction.y * t), float(ray.origin.z + ray.direction.z * t)};
	return std::isfinite(hit->x) && std::isfinite(hit->y) && std::isfinite(hit->z);
}
} // namespace

bool ModelEditorTools::addPolyBuildPoint(const QPointF &point, Qt::KeyboardModifiers modifiers)
{
	if (!m_modal || m_modal->op != ModelModalOperator::PolyBuild || m_editor->m_working)
		return false;
	auto &m = *m_modal;
	const auto &mesh = m_editor->m_document.mesh();
	const int surfaceIndex = std::clamp(m_editor->m_document.selection().surface, 0, std::max(0, int(mesh.surfaces.size()) - 1));
	if (surfaceIndex >= mesh.surfaces.size())
		return false;
	const auto &surface = mesh.surfaces[surfaceIndex];
	const int frame = std::clamp(m_editor->m_frame->currentIndex(), 0, std::max(0, int(surface.frames.size()) - 1));
	const auto forward = m_viewport->viewForward();
	m.pointer = m.lastActual = point;
	if (modifiers & Qt::ControlModifier)
	{
		// Extrude the selected open border edges to the pointer, in the view plane
		// through their centre.
		auto edit = toolEdit(ModelEditKind::ExtrudeEdges);
		if (edit.selection.edges.isEmpty())
		{
			showBanner(Text::tr("Select open border edges first; Ctrl+click extrudes them to the pointer."), {}, LiveStatus::Problem);
			return false;
		}
		double x = 0, y = 0, z = 0;
		int count = 0;
		for (const auto edge : std::as_const(edit.selection.edges))
		{
			for (int v : {edge.first, edge.second})
			{
				if (v < 0 || v >= surface.vertexCount)
					continue;
				const auto p = surface.frames[frame].positions[v];
				x += p.x;
				y += p.y;
				z += p.z;
				++count;
			}
		}
		if (!count)
			return false;
		const ModelVec3 centre{float(x / count), float(y / count), float(z / count)};
		ModelVec3 hit;
		if (!viewPlaneHit(m_viewport->viewRay(point), centre, forward, &hit))
			return false;
		edit.translation = {hit.x - centre.x, hit.y - centre.y, hit.z - centre.z};
		edit.transformSpace = ModelTransformSpace::World;
		edit.axisRotation = {};
		edit.axesFrame = -1;
		m.buildCorners.clear();
		QString error;
		const bool applied = runTool(edit, &error);
		// The edit may move focus and end the operator.
		if (applied && m_modal)
			setSelectionMode(2);
		m_viewport->update();
		return applied;
	}
	// The nearest vertex of the active surface within reach, else a new point
	// on the view plane through the 3D cursor.
	int nearest = -1;
	double best = reach;
	for (int v = 0; v < surface.vertexCount; ++v)
	{
		QPointF screen;
		if (!m_viewport->projectToView(surface.frames[frame].positions[v], &screen))
			continue;
		const double d = QLineF(screen, point).length();
		if (d < best)
		{
			best = d;
			nearest = v;
		}
	}
	ModelVec3 place;
	if (nearest >= 0)
		place = surface.frames[frame].positions[nearest];
	else if (!viewPlaneHit(m_viewport->viewRay(point), m_cursor, forward, &place))
	{
		showBanner(Text::tr("This point is off the drawing plane. Turn the view or move the 3D cursor."), {}, LiveStatus::Problem);
		return false;
	}
	m.buildCorners.append(qMakePair(nearest, place));
	if (m.buildCorners.size() < 3)
	{
		showBanner(modalText(), {}, LiveStatus::Update);
		m_viewport->update();
		return true;
	}
	ModelEdit edit = toolEdit(ModelEditKind::AddFace);
	edit.selection = {surfaceIndex, {}, {}};
	for (const auto &[vertex, position] : std::as_const(m.buildCorners))
	{
		if (vertex >= 0)
			edit.selection.vertices.insert(vertex);
		else
			edit.tool.facePoints[edit.tool.facePointCount++] = {position.x, position.y, position.z};
	}
	// Without a neighbour to follow, the face turns towards the viewer.
	edit.tool.normal = {-forward.x, -forward.y, -forward.z};
	edit.tool.symmetryAxes = 0;
	if (!mesh.skeleton.isEmpty() && !surface.skinning.isEmpty() && edit.tool.facePointCount > 0)
	{
		if (!confirmPrimitiveBinding(edit))
		{
			if (m_modal)
				m_modal->buildCorners.removeLast();
			return false;
		}
		// New corners join the face's own surface.
		edit.tool.newSurface.clear();
		if (!m_modal)
			return false;
	}
	const auto last = m_modal->buildCorners.mid(1);
	QString error;
	const bool applied = runTool(edit, &error);
	// The edit may move focus and end the operator.
	if (!m_modal || m_modal->op != ModelModalOperator::PolyBuild)
		return applied;
	auto &build = *m_modal;
	if (!applied)
	{
		build.buildCorners.removeLast();
		m_viewport->update();
		return false;
	}
	// The last two corners start the next face, as the new face's vertices.
	build.buildCorners.clear();
	const auto &now = m_editor->m_document.mesh().surfaces[surfaceIndex];
	const auto faces = m_editor->m_document.selection().faces;
	if (!faces.isEmpty())
	{
		const auto &t = now.triangles[*faces.cbegin()];
		for (const auto &[vertex, position] : last)
		{
			for (int corner : {t.a, t.b, t.c})
			{
				const auto p = now.frames[frame].positions[corner];
				if (p.x == position.x && p.y == position.y && p.z == position.z)
				{
					build.buildCorners.append(qMakePair(corner, p));
					break;
				}
			}
		}
	}
	showBanner(modalText(), {}, LiveStatus::Update);
	m_viewport->update();
	return true;
}

void ModelEditorTools::newPolyBuildFace()
{
	if (!m_modal || m_modal->op != ModelModalOperator::PolyBuild)
		return;
	m_modal->buildCorners.clear();
	showBanner(modalText(), {}, LiveStatus::Update);
	m_viewport->update();
}

void ModelEditorTools::undoPolyBuildPoint()
{
	if (!m_modal || m_modal->op != ModelModalOperator::PolyBuild || m_modal->buildCorners.isEmpty())
		return;
	m_modal->buildCorners.removeLast();
	showBanner(modalText(), {}, LiveStatus::Update);
	m_viewport->update();
}

bool ModelEditorTools::handlePolyBuildMouse(QMouseEvent *event)
{
	const QPointF point = event->position();
	if (event->type() == QEvent::MouseMove)
	{
		m_modal->pointer = m_modal->lastActual = point;
		m_viewport->update();
		return false;
	}
	if (event->type() == QEvent::MouseButtonPress && event->button() == Qt::LeftButton)
	{
		addPolyBuildPoint(point, event->modifiers());
		m_swallowRelease = true;
		return true;
	}
	if (event->type() == QEvent::MouseButtonPress && event->button() == Qt::RightButton)
	{
		newPolyBuildFace();
		m_swallowRelease = true;
		return true;
	}
	if (event->type() == QEvent::MouseButtonRelease && m_swallowRelease && event->button() != Qt::MiddleButton)
	{
		m_swallowRelease = false;
		return true;
	}
	// Other buttons navigate the view while building.
	return false;
}

bool ModelEditorTools::handlePolyBuildKey(QKeyEvent *event)
{
	const int key = event->key();
	auto &m = *m_modal;
	const double step = event->modifiers() & Qt::ShiftModifier ? 1 : 8;
	if (key == Qt::Key_Escape || key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space)
		finishModal(false);
	else if (key == Qt::Key_Backspace)
		undoPolyBuildPoint();
	else if (key == Qt::Key_N)
		newPolyBuildFace();
	else if (key == Qt::Key_Insert)
		addPolyBuildPoint(m.pointer, event->modifiers());
	else if (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down)
	{
		// Arrow keys move the crosshair by screen direction, mirrored in a
		// right-to-left layout so Left always moves towards the reading start.
		const double flip = m_viewport->layoutDirection() == Qt::RightToLeft ? -1 : 1;
		const QPointF delta = key == Qt::Key_Left ? QPointF(-step * flip, 0) : key == Qt::Key_Right ? QPointF(step * flip, 0)
							  : key == Qt::Key_Up	 ? QPointF(0, -step)
													 : QPointF(0, step);
		m.pointer = m.lastActual = m.pointer + delta;
		m_viewport->update();
	}
	return true;
}

void ModelEditorTools::paintPolyBuild(QPainter &painter)
{
	const auto &m = *m_modal;
	const QColor accent = m_viewport->palette().highlight().color();
	const QColor text = m_viewport->palette().text().color();
	QVector<QPointF> placed;
	for (const auto &corner : m.buildCorners)
	{
		QPointF screen;
		if (m_viewport->projectToView(corner.second, &screen))
			placed << screen;
	}
	painter.setBrush(Qt::NoBrush);
	if (!placed.isEmpty())
	{
		// The face so far and the edge to the pointer, dashed so it reads
		// without colour.
		painter.setPen(QPen(accent, 2, Qt::DashLine));
		for (int i = 1; i < placed.size(); ++i)
			painter.drawLine(placed[i - 1], placed[i]);
		painter.drawLine(placed.last(), m.pointer);
		if (placed.size() == 2)
			painter.drawLine(m.pointer, placed.first());
		for (const auto &screen : std::as_const(placed))
		{
			painter.setPen(QPen(text, 1));
			painter.setBrush(accent);
			painter.drawRect(QRectF(screen - QPointF(4, 4), QSizeF(8, 8)));
		}
		painter.setBrush(Qt::NoBrush);
	}
	// The crosshair where the next corner goes.
	painter.setPen(QPen(text, 1));
	painter.drawEllipse(m.pointer, reach, reach);
	painter.drawLine(m.pointer + QPointF(-reach - 6, 0), m.pointer + QPointF(-reach + 4, 0));
	painter.drawLine(m.pointer + QPointF(reach - 4, 0), m.pointer + QPointF(reach + 6, 0));
	painter.drawLine(m.pointer + QPointF(0, -reach - 6), m.pointer + QPointF(0, -reach + 4));
	painter.drawLine(m.pointer + QPointF(0, reach - 4), m.pointer + QPointF(0, reach + 6));
}
} // namespace vibestudio
