// Mesh Editor commands for alignment and topology tools that live outside the
// main action table: Align Vertices (flatten, set coordinate, snap to grid),
// and the menus and Item page row that reach them. Every edit runs through
// runTool, so it is one undo step with Adjust Last Operation and Repeat Last.
#include "app/model_editor_tools.h"

#include "app/locale_number.h"
#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"
#include "core/model_align.h"
#include "core/model_geometric_topology.h"
#include "core/model_targets.h"
#include "core/studio_settings.h"
#include "core/model_whole_transform.h"

#include <QAction>
#include <QScrollArea>
#include <QScopedValueRollback>
#include <QCheckBox>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineF>
#include <QMenu>
#include <QMenuBar>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <set>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelTools)
};
// Transform Model's numeric dialog: offset, rotation, scale and pivot.
class TransformModelDialog final : public QDialog
{
  public:
	explicit TransformModelDialog(const ModelVec3 &pivot, QWidget *parent) : QDialog(parent)
	{
		setObjectName(QStringLiteral("meshTransformModelDialog"));
		setWindowTitle(Text::tr("Transform Model"));
		auto *form = new QFormLayout(this);
		auto *intro = new QLabel(Text::tr("Moves every surface and pose, the attachment tags, collision boxes, the skeleton and the MDL "
										  "eye position together, as one undo step."));
		intro->setWordWrap(true);
		form->addRow(intro);
		const auto row = [&](const QString &label, const QString &name, double value, double low, double high, QDoubleSpinBox **fields)
		{
			auto *widget = new QWidget;
			auto *layout = new QHBoxLayout(widget);
			layout->setContentsMargins(0, 0, 0, 0);
			for (int axis = 0; axis < 3; ++axis)
			{
				const QString axisName = QStringLiteral("XYZ").mid(axis, 1);
				auto *spin = new QDoubleSpinBox;
				spin->setObjectName(name + axisName);
				spin->setRange(low, high);
				spin->setDecimals(4);
				spin->setValue(value);
				spin->setPrefix(axisName + QLatin1Char(' '));
				spin->setLayoutDirection(Qt::LeftToRight);
				spin->setAccessibleName(QStringLiteral("%1 %2").arg(label, axisName));
				layout->addWidget(spin);
				fields[axis] = spin;
			}
			form->addRow(label, widget);
		};
		row(Text::tr("Offset"), QStringLiteral("meshTransformModelOffset"), 0, -1000000, 1000000, m_offset);
		row(Text::tr("Rotation (degrees)"), QStringLiteral("meshTransformModelRotation"), 0, -36000, 36000, m_rotation);
		row(Text::tr("Scale"), QStringLiteral("meshTransformModelScale"), 1, -10000, 10000, m_scale);
		row(Text::tr("Pivot"), QStringLiteral("meshTransformModelPivot"), 0, -1000000, 1000000, m_pivot);
		m_pivot[0]->setValue(pivot.x);
		m_pivot[1]->setValue(pivot.y);
		m_pivot[2]->setValue(pivot.z);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
		form->addRow(buttons);
	}
	void fill(ModelEdit *edit) const
	{
		edit->translation = {float(m_offset[0]->value()), float(m_offset[1]->value()), float(m_offset[2]->value())};
		edit->rotation = {float(m_rotation[0]->value()), float(m_rotation[1]->value()), float(m_rotation[2]->value())};
		edit->scale = {float(m_scale[0]->value()), float(m_scale[1]->value()), float(m_scale[2]->value())};
		edit->pivot = {float(m_pivot[0]->value()), float(m_pivot[1]->value()), float(m_pivot[2]->value())};
	}

  private:
	QDoubleSpinBox *m_offset[3]{}, *m_rotation[3]{}, *m_scale[3]{}, *m_pivot[3]{};
};
} // namespace

ModelEdit ModelEditorTools::wholeModelEdit() const
{
	ModelEdit edit;
	edit.kind = ModelEditKind::TransformModel;
	// The selection stays as it is; attachments and joints are not components.
	edit.selection = m_editor->m_document.selection();
	edit.selection.tag.clear();
	edit.selection.collision.clear();
	edit.selection.joint.clear();
	if (!edit.selection.surfaces.isEmpty())
		edit.selection = {std::max(0, edit.selection.surface), {}, {}};
	edit.pivotMode = ModelTransformPivot::Custom;
	edit.pivotFrame = std::max(0, m_editor->m_frame->currentIndex());
	return edit;
}

bool ModelEditorTools::runSharp(const ModelEdit &edit)
{
	// Hard edges cost vertices: the banner says how many, against the budgets.
	const auto &mesh = m_editor->m_document.mesh();
	const int surface = edit.selection.surface;
	const int before = surface >= 0 && surface < mesh.surfaces.size() ? mesh.surfaces[surface].vertexCount : 0;
	QString error;
	if (!runTool(edit, &error))
		return false;
	const auto &after = m_editor->m_document.mesh();
	if (surface < 0 || surface >= after.surfaces.size())
		return true;
	const int count = after.surfaces[surface].vertexCount, added = count - before;
	QStringList limits;
	for (const auto &format : {QStringLiteral("md3"), QStringLiteral("md2"), QStringLiteral("mdl")})
	{
		ModelTargetProfile target;
		if (findModelTargetProfile(format, m_editor->deliveryTarget(format), &target) && target.limits.vertices > 0)
			limits << Text::tr("%1 %2 of %3").arg(format.toUpper(), localeNumber(count), localeNumber(target.limits.vertices));
	}
	const int change = std::abs(added);
	showBanner(added >= 0 ? Text::tr("%n vertex(es) added for hard edges.", nullptr, change) : Text::tr("%n vertex(es) welded back.", nullptr, change),
			   Text::tr("Surface vertices against the budgets: %1.").arg(limits.join(QStringLiteral("; "))));
	return true;
}

void ModelEditorTools::noteIntersections(const ModelEdit &edit, const ModelEditReport &report)
{
	m_lastIntersections = report.introducedIntersections;
	m_lastCrossings = report.introducedCrossings;
	m_lastIntersectionSurface = edit.selection.surface;
	if (report.introducedIntersections.isEmpty() && report.introducedCrossings.isEmpty())
		return;
	// A warning, not a refusal: intentional interpenetration is common.
	const int count = report.introducedIntersections.size() + report.introducedCrossings.size();
	const QString where = !report.introducedIntersections.isEmpty()
							  ? Text::tr("Faces %1 and %2 cross, among others.")
									.arg(report.introducedIntersections.first().first)
									.arg(report.introducedIntersections.first().second)
							  : Text::tr("Face %1 crosses face %2 of surface %3, among others.")
									.arg(report.introducedCrossings.first()[0])
									.arg(report.introducedCrossings.first()[2])
									.arg(report.introducedCrossings.first()[1]);
	showBanner(Text::tr("%n pair(s) of faces now cross each other.", nullptr, count),
			   Text::tr("%1 Mesh > Clean Up > Select New Intersections selects them; Refuse New Intersections refuses such edits.").arg(where),
			   LiveStatus::Problem);
}

bool ModelEditorTools::mergeAtTarget(const QPointF &point)
{
	const int target = vertexAt(point);
	if (target < 0)
	{
		showBanner(Text::tr("Point at the vertex to weld onto, then run Merge at Target."), {}, LiveStatus::Problem);
		return false;
	}
	auto edit = toolEdit(ModelEditKind::MergeVertices);
	edit.tool.merge = ModelMergeMode::Target;
	edit.tool.topology.targetVertex = target;
	QString error;
	return runTool(edit, &error);
}

bool ModelEditorTools::pivotAtTarget(const QPointF &point)
{
	const auto &mesh = m_editor->m_document.mesh();
	const int element = m_snapElement ? m_snapElement->currentIndex() : 1;
	const auto framed = [&](int s) { return std::clamp(m_editor->m_frame->currentIndex(), 0, std::max(0, int(mesh.surfaces[s].frames.size()) - 1)); };
	const auto at = [&](int s, int v) { return mesh.surfaces[s].frames[framed(s)].positions[v]; };
	const auto ray = m_viewport->viewRay(point);
	const auto dotOf = [](const ModelVec3 &a, const ModelVec3 &b) { return double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z; };
	const auto minus = [](const ModelVec3 &a, const ModelVec3 &b) { return ModelVec3{a.x - b.x, a.y - b.y, a.z - b.z}; };
	const auto along = [](const ModelVec3 &a, const ModelVec3 &d, double t) { return ModelVec3{float(a.x + d.x * t), float(a.y + d.y * t), float(a.z + d.z * t)}; };
	ModelVec3 target{};
	bool found = false;
	if (element == 3 || element == 4)
	{
		int s = -1;
		const int face = faceAt(point, &s);
		if (face >= 0 && s >= 0 && s < mesh.surfaces.size() && !mesh.surfaces[s].frames.isEmpty())
		{
			// Where the view ray meets the face's plane.
			const auto &t = mesh.surfaces[s].triangles[face];
			const ModelVec3 a = at(s, t.a), e1 = minus(at(s, t.b), a), e2 = minus(at(s, t.c), a);
			const ModelVec3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
			const double denominator = dotOf(ray.direction, n);
			if (std::abs(denominator) > 1e-12)
			{
				target = along(ray.origin, ray.direction, dotOf(minus(a, ray.origin), n) / denominator);
				found = std::isfinite(target.x) && std::isfinite(target.y) && std::isfinite(target.z);
			}
		}
	}
	else if (element == 2 || element == 5)
	{
		int s = -1;
		const auto edge = edgeNear(point, &s);
		if (edge.first >= 0 && s >= 0 && s < mesh.surfaces.size() && !mesh.surfaces[s].frames.isEmpty())
		{
			const ModelVec3 a = at(s, edge.first), d = minus(at(s, edge.second), a);
			double t = 0.5;
			if (element == 5)
			{
				// The point of the edge nearest the view ray.
				const ModelVec3 w = minus(a, ray.origin);
				const double aa = dotOf(d, d), b = dotOf(d, ray.direction), c = dotOf(ray.direction, ray.direction);
				const double denominator = aa * c - b * b;
				if (std::abs(denominator) > 1e-12)
					t = std::clamp((b * dotOf(ray.direction, w) - c * dotOf(d, w)) / denominator, 0.0, 1.0);
			}
			target = along(a, d, t);
			found = true;
		}
	}
	else
	{
		// The nearest vertex within 16 pixels, on any surface.
		double nearest = 16;
		for (int s = 0; s < mesh.surfaces.size(); ++s)
		{
			if (mesh.surfaces[s].frames.isEmpty())
				continue;
			for (int v = 0; v < mesh.surfaces[s].vertexCount; ++v)
			{
				QPointF screen;
				if (!m_viewport->projectToView(at(s, v), &screen))
					continue;
				const double d = QLineF(screen, point).length();
				if (d < nearest)
				{
					nearest = d;
					target = at(s, v);
					found = true;
				}
			}
		}
	}
	if (!found)
	{
		showBanner(element == 2	  ? Text::tr("Point at the edge whose midpoint the pivot moves to.")
				   : element == 5 ? Text::tr("Point at the edge the pivot moves onto.")
				   : element >= 3 ? Text::tr("Point at the face the pivot moves onto.")
								  : Text::tr("Point at the vertex the pivot moves to."),
				   {}, LiveStatus::Problem);
		return false;
	}
	m_editor->m_pivotMode->setCurrentIndex(2);
	m_editor->m_pivot[0]->setValue(target.x);
	m_editor->m_pivot[1]->setValue(target.y);
	m_editor->m_pivot[2]->setValue(target.z);
	showBanner(Text::tr("Transforms now pivot around %1.").arg(localeNumberList({target.x, target.y, target.z}, 4)));
	return true;
}

bool ModelEditorTools::placeVertex()
{
	const auto &mesh = m_editor->m_document.mesh();
	const auto selection = m_editor->m_document.selection();
	if (selection.surface < 0 || selection.surface >= mesh.surfaces.size())
		return false;
	const auto &surface = mesh.surfaces[selection.surface];
	QSet<int> vertices = selection.vertices;
	for (auto edge : selection.edges)
		vertices << edge.first << edge.second;
	for (int face : selection.faces)
		if (face >= 0 && face < surface.triangles.size())
			vertices << surface.triangles[face].a << surface.triangles[face].b << surface.triangles[face].c;
	ModelGeometricTopology topology;
	QSet<int> groups;
	if (buildModelGeometricTopology(surface, &topology))
		for (int v : std::as_const(vertices))
			groups.insert(topology.group(v));
	if (groups.size() != 1)
	{
		showBanner(Text::tr("Select one vertex to place. For several, use Selection position on the Item page."), {}, LiveStatus::Problem);
		return false;
	}
	const int vertex = *groups.cbegin();
	const int frame = std::clamp(m_editor->m_frame->currentIndex(), 0, std::max(0, int(surface.frames.size()) - 1));
	const auto at = surface.frames[frame].positions[vertex];
	QDialog dialog(m_editor);
	dialog.setObjectName(QStringLiteral("meshPlaceVertexDialog"));
	dialog.setWindowTitle(Text::tr("Place Vertex"));
	auto *form = new QFormLayout(&dialog);
	QDoubleSpinBox *fields[3]{};
	const float values[3]{at.x, at.y, at.z};
	for (int axis = 0; axis < 3; ++axis)
	{
		const QString name = QStringLiteral("XYZ").mid(axis, 1);
		auto *spin = new QDoubleSpinBox;
		spin->setObjectName(QStringLiteral("meshPlaceVertex%1").arg(name));
		spin->setRange(-1000000, 1000000);
		spin->setDecimals(4);
		spin->setValue(values[axis]);
		spin->setLayoutDirection(Qt::LeftToRight);
		spin->setAccessibleName(Text::tr("Vertex position %1").arg(name));
		form->addRow(name, spin);
		fields[axis] = spin;
	}
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	form->addRow(buttons);
	if (dialog.exec() != QDialog::Accepted)
		return false;
	auto edit = toolEdit(ModelEditKind::SetVertexPositions);
	edit.selection = {selection.surface, {vertex}, {}};
	edit.tool.topology.vertices = {vertex};
	edit.tool.topology.positions = {{fields[0]->value(), fields[1]->value(), fields[2]->value()}};
	QString error;
	return runTool(edit, &error);
}

void ModelEditorTools::trackSelectionOrder()
{
	const auto &mesh = m_editor->m_document.mesh();
	const auto selection = m_editor->m_document.selection();
	const auto revision = m_editor->m_document.revisionFingerprint();
	// Another surface, or an edit that may renumber vertices: start again
	// from the selection as it stands.
	if (selection.surface != m_vertexOrderSurface || revision != m_vertexOrderRevision)
	{
		m_vertexOrder.clear();
		m_vertexOrderKnown = true;
		m_vertexOrderSurface = selection.surface;
		m_vertexOrderRevision = revision;
	}
	QVector<int> order;
	for (int v : std::as_const(m_vertexOrder))
		if (selection.vertices.contains(v))
			order.append(v);
	const QSet<int> known(order.cbegin(), order.cend());
	QVector<int> added;
	for (int v : selection.vertices)
		if (!known.contains(v))
			added.append(v);
	std::sort(added.begin(), added.end());
	// A fresh selection starts with a known order again.
	if (order.isEmpty())
		m_vertexOrderKnown = true;
	// One pick adds one geometric vertex: its seam copies share a position.
	if (added.size() > 1 && selection.surface >= 0 && selection.surface < mesh.surfaces.size() && !mesh.surfaces[selection.surface].frames.isEmpty())
	{
		const auto &positions = mesh.surfaces[selection.surface].frames[0].positions;
		for (int v : std::as_const(added))
		{
			const auto a = positions.value(v), b = positions.value(added.first());
			if (a.x != b.x || a.y != b.y || a.z != b.z)
				m_vertexOrderKnown = false;
		}
	}
	m_vertexOrder = order + added;
}

QVector<int> ModelEditorTools::pickedVertexPath() const
{
	const auto selection = m_editor->m_document.selection();
	if (!m_vertexOrderKnown || selection.surface != m_vertexOrderSurface || !selection.faces.isEmpty() || !selection.edges.isEmpty() ||
		m_vertexOrder.size() != selection.vertices.size())
		return {};
	// Three or more geometric vertices make a path; two connect without one.
	const auto &mesh = m_editor->m_document.mesh();
	if (selection.surface < 0 || selection.surface >= mesh.surfaces.size() || mesh.surfaces[selection.surface].frames.isEmpty())
		return {};
	const auto &positions = mesh.surfaces[selection.surface].frames[0].positions;
	int distinct = 0;
	for (int i = 0; i < m_vertexOrder.size(); ++i)
	{
		const auto a = positions.value(m_vertexOrder[i]), b = positions.value(m_vertexOrder[std::max(0, i - 1)]);
		distinct += i == 0 || a.x != b.x || a.y != b.y || a.z != b.z;
	}
	return distinct >= 3 ? m_vertexOrder : QVector<int>{};
}

int ModelEditorTools::vertexAt(const QPointF &point)
{
	const auto &mesh = m_editor->m_document.mesh();
	const int surfaceIndex = m_editor->m_document.selection().surface;
	if (surfaceIndex < 0 || surfaceIndex >= mesh.surfaces.size())
		return -1;
	const auto &surface = mesh.surfaces[surfaceIndex];
	const int frame = std::clamp(m_editor->m_frame->currentIndex(), 0, std::max(0, int(surface.frames.size()) - 1));
	if (surface.frames.isEmpty())
		return -1;
	int best = -1;
	double nearest = 16;
	for (int v = 0; v < surface.vertexCount; ++v)
	{
		QPointF screen;
		if (!m_viewport->projectToView(surface.frames[frame].positions[v], &screen))
			continue;
		const double d = QLineF(screen, point).length();
		if (d < nearest)
		{
			nearest = d;
			best = v;
		}
	}
	return best;
}

bool ModelEditorTools::runOrigin(int target)
{
	auto edit = wholeModelEdit();
	ModelVec3 point{};
	if (target == 2)
		point = m_cursor;
	else if (target == 3 && !selectionCentre(&point))
	{
		showBanner(Text::tr("Select the components whose centre becomes the origin."), {}, LiveStatus::Problem);
		return false;
	}
	QString error;
	const auto kind = target == 0 ? ModelOriginTarget::BoundsCentre : target == 1 ? ModelOriginTarget::BoundsBottom : ModelOriginTarget::Point;
	if (!modelOriginOffset(m_editor->m_document.mesh(), edit.pivotFrame, kind, point, &edit.translation, &error))
	{
		showBanner(error, {}, LiveStatus::Problem);
		return false;
	}
	return runTool(edit, &error);
}

void ModelEditorTools::buildModelTransformActions()
{
	// Origin presets move the whole model so the chosen point lands on the
	// origin; the displayed pose measures the bounds.
	addAction(QStringLiteral("meshOriginBoundsCentre"), Text::tr("Origin to Bounds Centre"), {}, [this] { runOrigin(0); });
	addAction(QStringLiteral("meshOriginBoundsBottom"), Text::tr("Origin to Bounds Bottom (Feet)"), {}, [this] { runOrigin(1); });
	addAction(QStringLiteral("meshOriginCursor"), Text::tr("Origin to 3D Cursor"), {}, [this] { runOrigin(2); });
	addAction(QStringLiteral("meshOriginSelection"), Text::tr("Origin to Selection"), {}, [this] { runOrigin(3); });
	// idTech models face +X: a quarter turn about Z re-orients an import.
	addAction(QStringLiteral("meshTurnModelLeft"), Text::tr("Turn Model 90° Left"), {},
			  [this]
			  {
				  auto edit = wholeModelEdit();
				  edit.rotation = {0, 0, 90};
				  QString error;
				  runTool(edit, &error);
			  });
	addAction(QStringLiteral("meshTurnModelRight"), Text::tr("Turn Model 90° Right"), {},
			  [this]
			  {
				  auto edit = wholeModelEdit();
				  edit.rotation = {0, 0, -90};
				  QString error;
				  runTool(edit, &error);
			  });
	addAction(QStringLiteral("meshScaleModelHeight"), Text::tr("Scale Model to Height…"), {},
			  [this]
			  {
				  bool accepted = false;
				  // A Quake player is 56 units tall.
				  const double height = QInputDialog::getDouble(m_editor, Text::tr("Scale Model to Height"), Text::tr("Height in model units:"), 56,
																0.001, 1000000, 3, &accepted);
				  if (!accepted)
					  return;
				  auto edit = wholeModelEdit();
				  double factor = 1;
				  QString error;
				  if (!modelHeightScale(m_editor->m_document.mesh(), edit.pivotFrame, height, &factor, &error))
				  {
					  showBanner(error, {}, LiveStatus::Problem);
					  return;
				  }
				  edit.scale = {float(factor), float(factor), float(factor)};
				  runTool(edit, &error);
			  });
	addAction(QStringLiteral("meshTransformModel"), Text::tr("Transform Model…"), {},
			  [this]
			  {
				  TransformModelDialog dialog(m_cursor, m_editor);
				  if (dialog.exec() != QDialog::Accepted)
					  return;
				  auto edit = wholeModelEdit();
				  dialog.fill(&edit);
				  QString error;
				  runTool(edit, &error);
			  });
}

bool ModelEditorTools::runAlign(int axes, ModelAlignTarget target, const std::array<double, 3> &value, double grid, QString *error)
{
	auto edit = toolEdit(ModelEditKind::AlignVertices);
	edit.tool.align.axes = axes;
	edit.tool.align.target = target;
	edit.tool.align.value = value;
	edit.tool.align.grid = target == ModelAlignTarget::Grid ? grid : 0;
	QString failure;
	if (runTool(edit, &failure))
		return true;
	if (error)
		*error = failure;
	return false;
}

void ModelEditorTools::buildTopologyActions()
{
	buildModelTransformActions();
	// Connect: Blender's J joins the selected vertices (two, or opposite
	// corners of quads) with new edges; 3ds Max's Connect adds edges between
	// the selected opposite sides of quads.
	// Connect Vertex Path: three or more vertices picked one at a time join in
	// that order; otherwise two vertices, or opposite corners of quads.
	addAction(QStringLiteral("meshConnectVertexPath"), Text::tr("Connect Vertex Path"), {},
			  [this]
			  {
				  auto edit = toolEdit(ModelEditKind::ConnectVertices);
				  edit.tool.topology.vertices = pickedVertexPath();
				  QString error;
				  runTool(edit, &error);
			  });
	addAction(QStringLiteral("meshConnectEdges"), Text::tr("Connect Edges"), {},
			  [this]
			  {
				  auto edit = toolEdit(ModelEditKind::ConnectEdges);
				  // In vertex mode, the edges between selected vertices.
				  if (edit.selection.edges.isEmpty() && !edit.selection.vertices.isEmpty())
				  {
					  const auto &mesh = m_editor->m_document.mesh();
					  if (edit.selection.surface >= 0 && edit.selection.surface < mesh.surfaces.size())
						  for (const auto &t : mesh.surfaces[edit.selection.surface].triangles)
							  for (const auto &[a, b] : {std::pair{t.a, t.b}, std::pair{t.b, t.c}, std::pair{t.c, t.a}})
								  if (edit.selection.vertices.contains(a) && edit.selection.vertices.contains(b))
									  edit.selection.edges.insert(modelEdge(a, b));
					  edit.selection.vertices.clear();
				  }
				  edit.selection.faces.clear();
				  QString error;
				  runTool(edit, &error);
			  });
	// Merge at Target (3ds Max Target Weld): the selection joins the vertex
	// under the pointer, keeping that vertex's position, UVs and normal.
	addAction(QStringLiteral("meshMergeTarget"), Text::tr("Merge at Target Under Pointer"), {},
			  [this] { mergeAtTarget(pointerInViewport()); });
	// Place Vertex: exact coordinates for one vertex (every seam copy).
	addAction(QStringLiteral("meshPlaceVertex"), Text::tr("Place Vertex…"), {}, [this] { placeVertex(); });
	// Pivot at Target Under Pointer: the Snap to choice picks the target.
	addAction(QStringLiteral("meshPivotTarget"), Text::tr("Pivot at Target Under Pointer"), {}, [this] { pivotAtTarget(pointerInViewport()); });
	// Auto Merge: modal moves weld what they bring within the weld distance.
	auto *autoMerge = addAction(QStringLiteral("meshAutoMerge"), Text::tr("Auto Merge Vertices"), {},
								[this] { m_autoMerge = m_actions.value(QStringLiteral("meshAutoMerge"))->isChecked(); });
	autoMerge->setCheckable(true);
	// Intersection guard: select what the last tool made cross, or refuse such edits.
	addAction(QStringLiteral("meshSelectIntroducedIntersections"), Text::tr("Select New Intersections"), {},
			  [this]
			  {
				  if ((m_lastIntersections.isEmpty() && m_lastCrossings.isEmpty()) || m_lastIntersectionSurface < 0)
				  {
					  showBanner(Text::tr("The last tool made no faces cross."));
					  return;
				  }
				  // The edited surface's faces; other surfaces' faces are named in the banner.
				  ModelSelection selection{m_lastIntersectionSurface, {}, {}};
				  for (const auto &[first, second] : std::as_const(m_lastIntersections))
					  selection.faces << first << second;
				  for (const auto &crossing : std::as_const(m_lastCrossings))
					  selection.faces << crossing[0];
				  setSelectionMode(0);
				  applySelection(selection);
				  const int count = int(selection.faces.size());
				  showBanner(Text::tr("%n crossing face(s) selected.", nullptr, count));
			  });
	// The choice is kept between sessions (model/refuseIntersections).
	auto *refuse = addAction(QStringLiteral("meshRefuseIntersections"), Text::tr("Refuse New Intersections"), {},
							 [this]
							 {
								 m_refuseIntersections = m_actions.value(QStringLiteral("meshRefuseIntersections"))->isChecked();
								 StudioSettings settings;
								 settings.setModelRefuseIntersections(m_refuseIntersections);
								 settings.sync();
							 });
	refuse->setCheckable(true);
	m_refuseIntersections = StudioSettings(StudioSettings::AccessMode::ReadOnly).modelRefuseIntersections();
	refuse->setChecked(m_refuseIntersections);
	// Normals: wind faces outward (Blender's Shift+N) or inward.
	const auto recalculate = [this](bool inside)
	{
		auto edit = toolEdit(ModelEditKind::RecalculateOutside);
		edit.tool.topology.inside = inside;
		// The selected faces, those whose corners are all selected, or every face.
		const auto &mesh = m_editor->m_document.mesh();
		if (edit.selection.faces.isEmpty() && edit.selection.surface >= 0 && edit.selection.surface < mesh.surfaces.size())
		{
			QSet<int> vertices = edit.selection.vertices;
			for (auto edge : std::as_const(edit.selection.edges))
				vertices << edge.first << edge.second;
			const auto &surface = mesh.surfaces[edit.selection.surface];
			for (int face = 0; face < surface.triangles.size(); ++face)
			{
				const auto &t = surface.triangles[face];
				if (vertices.contains(t.a) && vertices.contains(t.b) && vertices.contains(t.c))
					edit.selection.faces.insert(face);
			}
		}
		edit.selection.vertices.clear();
		edit.selection.edges.clear();
		QString error;
		runTool(edit, &error);
	};
	addAction(QStringLiteral("meshRecalculateOutside"), Text::tr("Recalculate Outside"), {}, [recalculate] { recalculate(false); });
	// Hard edges: Mark Sharp splits vertex normals along the selected edges.
	const auto sharp = [this](ModelEditKind kind)
	{
		auto edit = toolEdit(kind);
		const auto &mesh = m_editor->m_document.mesh();
		// In vertex or face mode, the edges between selected vertices.
		if (edit.selection.edges.isEmpty() && edit.selection.surface >= 0 && edit.selection.surface < mesh.surfaces.size())
		{
			QSet<int> vertices = edit.selection.vertices;
			const auto &surface = mesh.surfaces[edit.selection.surface];
			for (int face : std::as_const(edit.selection.faces))
				vertices << surface.triangles[face].a << surface.triangles[face].b << surface.triangles[face].c;
			for (const auto &t : surface.triangles)
				for (const auto &[a, b] : {std::pair{t.a, t.b}, std::pair{t.b, t.c}, std::pair{t.c, t.a}})
					if (vertices.contains(a) && vertices.contains(b))
						edit.selection.edges.insert(modelEdge(a, b));
			// A quad's hidden diagonal is not an edge the user picked.
			ModelQuadTopology quads;
			if (buildModelQuadTopology(surface, std::max(0, m_editor->m_frame->currentIndex()), ModelMeshToolOptions{}.maxAngle, &quads))
				edit.selection.edges.removeIf([&](ModelEdge edge) { return quads.diagonals.contains(quads.geometry.geometricEdge(edge.first, edge.second)); });
		}
		edit.selection.faces.clear();
		edit.selection.vertices.clear();
		runSharp(edit);
	};
	addAction(QStringLiteral("meshMarkSharp"), Text::tr("Mark Sharp"), {}, [sharp] { sharp(ModelEditKind::MarkSharp); });
	// MilkShape 3D and 3ds Max smoothing groups: the selected faces become one
	// smooth region, hard against the faces around it.
	addAction(QStringLiteral("meshSmoothingGroup"), Text::tr("Make Smoothing Group"), {},
			  [this]
			  {
				  auto edit = toolEdit(ModelEditKind::MarkSharp);
				  const auto &mesh = m_editor->m_document.mesh();
				  if (edit.selection.faces.isEmpty() && edit.selection.surface >= 0 && edit.selection.surface < mesh.surfaces.size())
				  {
					  QSet<int> vertices = edit.selection.vertices;
					  for (auto e : std::as_const(edit.selection.edges))
						  vertices << e.first << e.second;
					  const auto &surface = mesh.surfaces[edit.selection.surface];
					  for (int face = 0; face < surface.triangles.size(); ++face)
					  {
						  const auto &t = surface.triangles[face];
						  if (vertices.contains(t.a) && vertices.contains(t.b) && vertices.contains(t.c))
							  edit.selection.faces.insert(face);
					  }
				  }
				  edit.selection.vertices.clear();
				  edit.selection.edges.clear();
				  edit.tool.topology.smoothingGroup = true;
				  runSharp(edit);
			  });
	addAction(QStringLiteral("meshClearSharp"), Text::tr("Clear Sharp"), {}, [sharp] { sharp(ModelEditKind::ClearSharp); });
	addAction(QStringLiteral("meshRecalculateInside"), Text::tr("Recalculate Inside"), {}, [recalculate] { recalculate(true); });
	// Delete and dissolve variants (Blender's X menu, 3ds Max's Remove).
	const auto runDelete = [this](ModelDeleteMode mode)
	{
		auto edit = toolEdit(ModelEditKind::DeleteGeometry);
		edit.tool.topology.deleteMode = mode;
		QString error;
		runTool(edit, &error);
	};
	addAction(QStringLiteral("meshDeleteVertices"), Text::tr("Delete Vertices"), {}, [runDelete] { runDelete(ModelDeleteMode::Vertices); });
	addAction(QStringLiteral("meshDeleteEdges"), Text::tr("Delete Edges"), {}, [runDelete] { runDelete(ModelDeleteMode::Edges); });
	addAction(QStringLiteral("meshDissolveEdges"), Text::tr("Dissolve Edges"), {},
			  [this]
			  {
				  auto edit = toolEdit(ModelEditKind::DissolveEdges);
				  // Edges between selected vertices, or between selected faces.
				  const auto &mesh = m_editor->m_document.mesh();
				  if (edit.selection.edges.isEmpty() && edit.selection.surface >= 0 && edit.selection.surface < mesh.surfaces.size())
				  {
					  const auto &surface = mesh.surfaces[edit.selection.surface];
					  QHash<ModelEdge, int> faceUses;
					  for (int face : std::as_const(edit.selection.faces))
					  {
						  const auto &t = surface.triangles[face];
						  for (auto edge : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
							  faceUses[edge]++;
					  }
					  for (auto it = faceUses.cbegin(); it != faceUses.cend(); ++it)
						  if (it.value() == 2)
							  edit.selection.edges.insert(it.key());
					  for (const auto &t : surface.triangles)
						  for (const auto &[a, b] : {std::pair{t.a, t.b}, std::pair{t.b, t.c}, std::pair{t.c, t.a}})
							  if (edit.selection.vertices.contains(a) && edit.selection.vertices.contains(b))
								  edit.selection.edges.insert(modelEdge(a, b));
				  }
				  edit.selection.faces.clear();
				  edit.selection.vertices.clear();
				  QString error;
				  runTool(edit, &error);
			  });
	addAction(QStringLiteral("meshLimitedDissolve"), Text::tr("Limited Dissolve"), {},
			  [this]
			  {
				  // The selected faces, those whose corners are all selected, or every face.
				  auto edit = toolEdit(ModelEditKind::LimitedDissolve);
				  const auto &mesh = m_editor->m_document.mesh();
				  if (edit.selection.faces.isEmpty() && edit.selection.surface >= 0 && edit.selection.surface < mesh.surfaces.size())
				  {
					  QSet<int> vertices = edit.selection.vertices;
					  for (auto edge : std::as_const(edit.selection.edges))
						  vertices << edge.first << edge.second;
					  const auto &surface = mesh.surfaces[edit.selection.surface];
					  for (int face = 0; face < surface.triangles.size(); ++face)
					  {
						  const auto &t = surface.triangles[face];
						  if (vertices.contains(t.a) && vertices.contains(t.b) && vertices.contains(t.c))
							  edit.selection.faces.insert(face);
					  }
				  }
				  edit.selection.vertices.clear();
				  edit.selection.edges.clear();
				  QString error;
				  runTool(edit, &error);
			  });
	// Snap: every selected vertex rounds to the Move snap step, as one edit.
	addAction(QStringLiteral("meshSnapSelectionToGrid"), Text::tr("Snap Selection to Grid"), {},
			  [this] { runAlign(7, ModelAlignTarget::Grid, {0, 0, 0}, std::max(0.000001, m_editor->m_translationGrid->value())); });
	for (int axis = 0; axis < 3; ++axis)
	{
		const QString name = QStringLiteral("XYZ").mid(axis, 1);
		// MilkShape's Flatten: one coordinate for the whole selection, the
		// selection's centroid in each pose. Adjust Last Operation can change it
		// to an exact value, the minimum or the maximum.
		addAction(QStringLiteral("meshFlatten%1").arg(name), Text::tr("Flatten %1").arg(name), {},
				  [this, axis] { runAlign(1 << axis, ModelAlignTarget::Centroid, {0, 0, 0}, 0); });
		addAction(QStringLiteral("meshAlignCursor%1").arg(name), Text::tr("Align %1 to 3D Cursor").arg(name), {},
				  [this, axis] { runAlign(1 << axis, ModelAlignTarget::Value, {m_cursor.x, m_cursor.y, m_cursor.z}, 0); });
	}
}

void ModelEditorTools::buildTopologyMenus()
{
	// Inserts a command into a menu before another, or at its end.
	const auto place = [this](const QString &menuName, const QString &before, const QString &command)
	{
		if (auto *menu = m_menuBar->findChild<QMenu *>(menuName))
			menu->insertAction(m_actions.value(before), m_actions.value(command));
	};
	place(QStringLiteral("meshMenuVertex"), QStringLiteral("meshBevelVertices"), QStringLiteral("meshConnectVertexPath"));
	place(QStringLiteral("meshMenuMerge"), QStringLiteral("meshMergeCollapse"), QStringLiteral("meshMergeTarget"));
	place(QStringLiteral("meshMenuVertex"), QStringLiteral("meshBevelVertices"), QStringLiteral("meshPlaceVertex"));
	place(QStringLiteral("meshMenuSnap"), QStringLiteral("meshPivotCursor"), QStringLiteral("meshPlaceVertex"));
	place(QStringLiteral("meshMenuSnap"), QStringLiteral("meshPivotCursor"), QStringLiteral("meshPivotTarget"));
	place(QStringLiteral("meshMenuCleanUp"), QStringLiteral("meshCheckTargets"), QStringLiteral("meshSelectIntroducedIntersections"));
	place(QStringLiteral("meshMenuCleanUp"), QStringLiteral("meshCheckTargets"), QStringLiteral("meshRefuseIntersections"));
	place(QStringLiteral("meshMenuSnap"), QStringLiteral("meshPivotCursor"), QStringLiteral("meshAutoMerge"));
	place(QStringLiteral("meshMenuEdge"), QStringLiteral("meshRotateEdge"), QStringLiteral("meshConnectEdges"));
	place(QStringLiteral("meshMenuDelete"), QStringLiteral("meshDeleteFaces"), QStringLiteral("meshDeleteVertices"));
	place(QStringLiteral("meshMenuDelete"), QStringLiteral("meshDeleteFaces"), QStringLiteral("meshDeleteEdges"));
	place(QStringLiteral("meshMenuDelete"), QStringLiteral("meshMergeCollapse"), QStringLiteral("meshDissolveEdges"));
	place(QStringLiteral("meshMenuDelete"), QStringLiteral("meshMergeCollapse"), QStringLiteral("meshLimitedDissolve"));
	place(QStringLiteral("meshMenuEdge"), QStringLiteral("meshBridge"), QStringLiteral("meshDissolveEdges"));
	place(QStringLiteral("meshMenuCleanUp"), QStringLiteral("meshBeautify"), QStringLiteral("meshLimitedDissolve"));
	place(QStringLiteral("meshMenuNormals"), QStringLiteral("meshRecalculateNormals"), QStringLiteral("meshRecalculateOutside"));
	place(QStringLiteral("meshMenuNormals"), QStringLiteral("meshRecalculateNormals"), QStringLiteral("meshRecalculateInside"));
	place(QStringLiteral("meshMenuNormals"), QString(), QStringLiteral("meshMarkSharp"));
	place(QStringLiteral("meshMenuNormals"), QString(), QStringLiteral("meshClearSharp"));
	place(QStringLiteral("meshMenuNormals"), QString(), QStringLiteral("meshSmoothingGroup"));
	place(QStringLiteral("meshMenuEdge"), QStringLiteral("meshMarkSeam"), QStringLiteral("meshMarkSharp"));
	place(QStringLiteral("meshMenuEdge"), QStringLiteral("meshMarkSeam"), QStringLiteral("meshClearSharp"));
	if (auto *snap = m_menuBar->findChild<QMenu *>(QStringLiteral("meshMenuSnap")))
	{
		auto *before = m_actions.value(QStringLiteral("meshPivotCursor"));
		auto *align = new QMenu(Text::tr("Align"), snap);
		align->setObjectName(QStringLiteral("meshMenuAlign"));
		for (const auto &name : {QStringLiteral("meshFlattenX"), QStringLiteral("meshFlattenY"), QStringLiteral("meshFlattenZ"), QString(),
								 QStringLiteral("meshAlignCursorX"), QStringLiteral("meshAlignCursorY"), QStringLiteral("meshAlignCursorZ")})
		{
			if (name.isEmpty())
				align->addSeparator();
			else if (auto *entry = m_actions.value(name))
				align->addAction(entry);
		}
		snap->insertAction(before, m_actions.value(QStringLiteral("meshSnapSelectionToGrid")));
		snap->insertMenu(before, align);
		snap->insertSeparator(before);
		for (const auto &name : {QStringLiteral("meshOriginBoundsCentre"), QStringLiteral("meshOriginBoundsBottom"), QStringLiteral("meshOriginCursor"),
								 QStringLiteral("meshOriginSelection")})
			snap->insertAction(before, m_actions.value(name));
		snap->insertSeparator(before);
	}
	if (auto *mesh = m_menuBar->findChild<QMenu *>(QStringLiteral("meshMenuMesh")))
	{
		auto *model = new QMenu(Text::tr("Transform Model"), mesh);
		model->setObjectName(QStringLiteral("meshMenuTransformModel"));
		for (const auto &name : {QStringLiteral("meshTransformModel"), QString(), QStringLiteral("meshOriginBoundsCentre"),
								 QStringLiteral("meshOriginBoundsBottom"), QStringLiteral("meshOriginCursor"), QStringLiteral("meshOriginSelection"), QString(),
								 QStringLiteral("meshTurnModelLeft"), QStringLiteral("meshTurnModelRight"), QStringLiteral("meshScaleModelHeight")})
		{
			if (name.isEmpty())
				model->addSeparator();
			else if (auto *entry = m_actions.value(name))
				model->addAction(entry);
		}
		auto *snapMenu = m_menuBar->findChild<QMenu *>(QStringLiteral("meshMenuSnap"));
		mesh->insertMenu(snapMenu ? snapMenu->menuAction() : nullptr, model);
	}
	buildAlignRow();
}

void ModelEditorTools::buildAlignRow()
{
	// Item page: the selection's position in the displayed pose. Typing a value
	// gives every selected vertex that coordinate (Align Vertices to a value).
	auto *contents = m_editor->findChild<QWidget *>(QStringLiteral("meshPageForm-item"));
	auto *form = contents ? qobject_cast<QFormLayout *>(contents->layout()) : nullptr;
	if (!form)
		return;
	auto *row = new QWidget;
	row->setObjectName(QStringLiteral("meshSelectionPosition"));
	// One labelled field per axis, stacked as the page's other XYZ rows are, so
	// a narrow sidebar at large text sizes keeps its width.
	auto *layout = new QVBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	for (int axis = 0; axis < 3; ++axis)
	{
		auto *spin = new QDoubleSpinBox;
		const QString name = QStringLiteral("XYZ").mid(axis, 1);
		spin->setObjectName(QStringLiteral("meshSelectionPosition%1").arg(name));
		spin->setRange(-1000000, 1000000);
		spin->setDecimals(4);
		spin->setKeyboardTracking(false);
		spin->setLayoutDirection(Qt::LeftToRight);
		spin->setMinimumWidth(0);
		spin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		spin->setAccessibleName(Text::tr("Selection position %1").arg(name));
		spin->setAccessibleDescription(Text::tr("The centroid of the selection in the displayed pose. Enter a value to give every selected "
												"vertex this %1 coordinate, as one undo step that follows the Apply to scope.")
										   .arg(name));
		spin->setToolTip(spin->accessibleDescription());
		connect(spin, &QDoubleSpinBox::valueChanged, this,
				[this, axis](double value)
				{
					if (m_editor->m_working || m_modal)
						return;
					std::array<double, 3> values{0, 0, 0};
					values[axis] = value;
					QString error;
					if (!runAlign(1 << axis, ModelAlignTarget::Value, values, 0, &error))
						refreshAlignRow();
				});
		auto *line = new QWidget;
		auto *lineLayout = new QHBoxLayout(line);
		lineLayout->setContentsMargins(0, 0, 0, 0);
		lineLayout->setSpacing(2);
		auto *label = new QLabel(name);
		label->setBuddy(spin);
		lineLayout->addWidget(label);
		lineLayout->addWidget(spin);
		layout->addWidget(line);
		m_alignFields[axis] = spin;
	}
	form->insertRow(std::min(1, form->rowCount()), Text::tr("Selection position"), row);
	refreshAlignRow();
}

void ModelEditorTools::refreshAlignRow()
{
	if (!m_alignFields[0])
		return;
	const auto &mesh = m_editor->m_document.mesh();
	const auto selection = m_editor->m_document.selection();
	bool valid = selection.surface >= 0 && selection.surface < mesh.surfaces.size() && selection.tag.isEmpty() && selection.collision.isEmpty() &&
				 selection.joint.isEmpty() && selection.surfaces.isEmpty();
	double sum[3]{0, 0, 0};
	int count = 0;
	if (valid)
	{
		const auto &surface = mesh.surfaces[selection.surface];
		const int frame = std::clamp(m_editor->m_frame->currentIndex(), 0, std::max(0, int(surface.frames.size()) - 1));
		QSet<int> vertices = selection.vertices;
		for (auto edge : selection.edges)
			vertices << edge.first << edge.second;
		for (int face : selection.faces)
		{
			if (face >= 0 && face < surface.triangles.size())
				vertices << surface.triangles[face].a << surface.triangles[face].b << surface.triangles[face].c;
		}
		// Seam copies share a position; count each position once.
		std::set<std::array<float, 3>> seen;
		for (int v : std::as_const(vertices))
		{
			if (v < 0 || v >= surface.vertexCount || surface.frames.isEmpty())
				continue;
			const auto p = surface.frames[frame].positions[v];
			if (!seen.insert({p.x, p.y, p.z}).second)
				continue;
			sum[0] += p.x;
			sum[1] += p.y;
			sum[2] += p.z;
			++count;
		}
	}
	valid = valid && count > 0;
	for (int axis = 0; axis < 3; ++axis)
	{
		auto *spin = m_alignFields[axis].data();
		if (!spin)
			continue;
		const QSignalBlocker blocker(spin);
		spin->setValue(valid ? sum[axis] / count : 0);
		spin->setEnabled(valid);
	}
}
// ---- Adjust Last Operation for selection operators ------------------------

namespace
{
struct SelectField
{
	QString label;
	double low = 0, high = 1;
	int decimals = 0;
	std::function<double(const ModelSelectRequest &)> get;
	std::function<void(ModelSelectRequest &, double)> set;
	bool toggle = false;
	QStringList choices;
};
QVector<SelectField> selectFields(const ModelSelectRequest &request)
{
	using R = ModelSelectRequest;
	const SelectField extend{Text::tr("Extend"), 0, 1, 0, [](const R &r) { return double(r.extend); }, [](R &r, double v) { r.extend = v > 0.5; }, true};
	const SelectField axis{Text::tr("Axis"), 0, 2, 0, [](const R &r) { return double(r.axis); }, [](R &r, double v) { r.axis = int(v); }, false,
						   {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")}};
	const SelectField negative{Text::tr("Negative side"), 0, 1, 0, [](const R &r) { return double(!r.positive); }, [](R &r, double v) { r.positive = v < 0.5; },
							   true};
	switch (request.operation)
	{
	case ModelSelectOperation::Similar:
		switch (request.similarity)
		{
		case ModelSimilarity::Area:
		case ModelSimilarity::Length:
			return {{Text::tr("Threshold (percent)"), 0, 100, 2, [](const R &r) { return r.threshold; }, [](R &r, double v) { r.threshold = v; }}, extend};
		case ModelSimilarity::Normal:
		case ModelSimilarity::Coplanar:
		case ModelSimilarity::Direction:
		case ModelSimilarity::FaceAngle:
			return {{Text::tr("Threshold (degrees)"), 0, 180, 2, [](const R &r) { return r.threshold; }, [](R &r, double v) { r.threshold = v; }}, extend};
		default:
			return {extend};
		}
	case ModelSelectOperation::Sharp:
		return {{Text::tr("Sharpness (degrees)"), 0, 180, 1, [](const R &r) { return r.threshold; }, [](R &r, double v) { r.threshold = v; }}, extend};
	case ModelSelectOperation::Random:
		return {{Text::tr("Ratio"), 0, 1, 3, [](const R &r) { return r.ratio; }, [](R &r, double v) { r.ratio = v; }},
				{Text::tr("Seed"), 0, 1000000, 0, [](const R &r) { return double(r.seed); }, [](R &r, double v) { r.seed = quint32(v); }}, extend};
	case ModelSelectOperation::Checker:
		return {{Text::tr("Every nth"), 2, 100, 0, [](const R &r) { return double(r.nth); }, [](R &r, double v) { r.nth = int(v); }},
				{Text::tr("Offset"), 0, 99, 0, [](const R &r) { return double(r.offset); }, [](R &r, double v) { r.offset = int(v); }}};
	case ModelSelectOperation::Side:
		return {axis, negative,
				{Text::tr("Margin"), -1000000, 1000000, 3, [](const R &r) { return r.threshold; }, [](R &r, double v) { r.threshold = v; }}, extend};
	case ModelSelectOperation::Facing:
		return {axis, negative,
				{Text::tr("Cone (degrees)"), 0, 180, 1, [](const R &r) { return r.threshold; }, [](R &r, double v) { r.threshold = v; }}, extend};
	case ModelSelectOperation::Mirror:
		return {axis, extend};
	case ModelSelectOperation::Linked:
		return {{Text::tr("Stop at UV seams"), 0, 1, 0, [](const R &r) { return double(r.delimitSeams); }, [](R &r, double v) { r.delimitSeams = v > 0.5; },
				 true}};
	default:
		return {};
	}
}
QString selectTitle(const ModelSelectRequest &request)
{
	switch (request.operation)
	{
	case ModelSelectOperation::Similar:
		return Text::tr("Select Similar");
	case ModelSelectOperation::Sharp:
		return Text::tr("Select Sharp Edges");
	case ModelSelectOperation::Random:
		return Text::tr("Select Random");
	case ModelSelectOperation::Checker:
		return Text::tr("Checker Deselect");
	case ModelSelectOperation::Side:
		return Text::tr("Select Side of Axis");
	case ModelSelectOperation::Facing:
		return Text::tr("Select Facing");
	case ModelSelectOperation::Mirror:
		return Text::tr("Mirror Selection");
	case ModelSelectOperation::Linked:
		return Text::tr("Select Linked");
	default:
		return Text::tr("Select");
	}
}
} // namespace

void ModelEditorTools::rememberSelect(const ModelSelectRequest &request, const ModelSelection &start)
{
	m_lastSelect = {request, start, m_editor->m_document.selection(), m_editor->m_document.revisionFingerprint(), !selectFields(request).isEmpty()};
	if (m_lastPanel && m_lastPanel->isVisible())
		refreshLastPanel();
}

bool ModelEditorTools::refreshSelectPanel()
{
	// Current only while the document and its selection are what the operator left.
	if (!m_lastSelect.active || m_lastSelect.revision != m_editor->m_document.revisionFingerprint() ||
		!(m_lastSelect.result == m_editor->m_document.selection()))
	{
		m_lastSelect.active = false;
		return false;
	}
	int focusedField = -1;
	for (auto *widget = QApplication::focusWidget(); widget && m_lastFields && m_lastFields->isAncestorOf(widget); widget = widget->parentWidget())
		if (widget->property("lastOperationField").isValid())
		{
			focusedField = widget->property("lastOperationField").toInt();
			break;
		}
	delete m_lastFields;
	m_lastFields = new QWidget;
	auto *form = new QFormLayout(m_lastFields);
	form->setContentsMargins(0, 4, 0, 0);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	const auto title = selectTitle(m_lastSelect.request);
	m_lastTitle->setText(title);
	m_lastTitle->setAccessibleName(Text::tr("Adjust Last Operation — %1").arg(title));
	m_lastTitle->setToolTip(m_lastTitle->accessibleName());
	const auto fields = selectFields(m_lastSelect.request);
	for (int index = 0; index < fields.size(); ++index)
	{
		const auto &field = fields[index];
		// Re-runs the operator from the selection it started with; the
		// document and its history are untouched.
		const auto apply = [this, index](double value)
		{
			if (m_adjustingLast || m_editor->m_working || !m_lastSelect.active || !(m_lastSelect.result == m_editor->m_document.selection()) ||
				m_lastSelect.revision != m_editor->m_document.revisionFingerprint())
				return;
			auto request = m_lastSelect.request;
			selectFields(request)[index].set(request, value);
			ModelSelection result;
			QString failure;
			if (!selectModelComponents(m_editor->m_document.mesh(), m_lastSelect.start, request, &result, &failure))
			{
				showBanner(failure, {}, LiveStatus::Problem);
				QMetaObject::invokeMethod(this, &ModelEditorTools::refreshLastPanel, Qt::QueuedConnection);
				return;
			}
			{
				const QScopedValueRollback<bool> adjusting(m_adjustingLast, true);
				applySelection(result);
			}
			m_lastSelect.request = request;
			m_lastSelect.result = m_editor->m_document.selection();
			const int count = int(result.faces.size() + result.vertices.size() + result.edges.size());
			showBanner(Text::tr("%n element(s) selected.", nullptr, count), {}, LiveStatus::Update);
			// Rebuilt after this signal returns, since it deletes the sender.
			QMetaObject::invokeMethod(this, &ModelEditorTools::refreshLastPanel, Qt::QueuedConnection);
		};
		QWidget *editor = nullptr;
		if (field.toggle)
		{
			auto *box = new QCheckBox;
			box->setChecked(field.get(m_lastSelect.request) > 0.5);
			connect(box, &QCheckBox::toggled, this, [apply](bool on) { apply(on ? 1 : 0); });
			editor = box;
		}
		else if (!field.choices.isEmpty())
		{
			auto *combo = new QComboBox;
			combo->addItems(field.choices);
			combo->setCurrentIndex(int(field.get(m_lastSelect.request)));
			connect(combo, &QComboBox::currentIndexChanged, this, [apply](int value) { apply(value); });
			editor = combo;
		}
		else
		{
			auto *spin = new QDoubleSpinBox;
			spin->setRange(field.low, field.high);
			spin->setDecimals(field.decimals);
			spin->setValue(field.get(m_lastSelect.request));
			spin->setKeyboardTracking(false);
			spin->setLayoutDirection(Qt::LeftToRight);
			connect(spin, &QDoubleSpinBox::valueChanged, this, apply);
			editor = spin;
		}
		editor->setAccessibleName(field.label);
		editor->setProperty("lastOperationField", index);
		editor->installEventFilter(this);
		form->addRow(field.label, editor);
		if (auto *label = qobject_cast<QLabel *>(form->labelForField(editor)))
			label->setWordWrap(true);
	}
	m_lastScroll->setWidget(m_lastFields);
	placeOverlays();
	if (focusedField >= 0)
	{
		for (auto *widget : m_lastFields->findChildren<QWidget *>())
			if (widget->property("lastOperationField").toInt() == focusedField && widget->property("lastOperationField").isValid())
			{
				widget->setFocus(Qt::OtherFocusReason);
				break;
			}
	}
	return true;
}
} // namespace vibestudio
