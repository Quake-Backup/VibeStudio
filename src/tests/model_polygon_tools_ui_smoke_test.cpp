// Mesh Editor coverage for the polygon modelling tools: Individual Origins
// and Normal axes (modal and gizmo), Array and Screw, extrude variants and
// bridging.
// Each scenario runs in the dark theme at 100%, and in high-contrast themes at
// 200% text with expanded translations, the second right to left.
#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_boundary_bridge_test_helpers.h"
#include "tests/model_polygon_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
PolygonChecks expect;
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *text, const char *, int) const override
	{
		const auto name = QByteArray(context);
		if (name != "VibeStudioModelTools" && name != "VibeStudioModelEditor")
			return {};
		const auto s = QString::fromUtf8(text);
		return QString("[%1 %2]").arg(s, QString("~~ ").repeated(s.size() / 6));
	}
};
void type(ModelEditorTools &tools, const QString &number)
{
	for (QChar c : number)
		tools.typeModal(QString(c));
}
QByteArray bytes(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}
ModelMesh threeSquares()
{
	QVector<ModelVec3> p;
	QVector<ModelTriangle> t;
	for (float x : {0.f, 20.f, 40.f})
		appendSquare(&p, &t, {x, 0, 0}, {1, 0, 0}, {0, 1, 0});
	return polygonMesh(p, t);
}
ModelMesh twoFacings()
{
	QVector<ModelVec3> p;
	QVector<ModelTriangle> t;
	appendSquare(&p, &t, {0, 0, 0}, {1, 0, 0}, {0, 1, 0});
	appendSquare(&p, &t, {20, 0, 0}, {1, 0, 0}, {0, 0, 1});
	return polygonMesh(p, t);
}
ModelSelection facesOf(const ModelMesh &mesh)
{
	ModelSelection selection;
	selection.faces = allFaces(mesh.surfaces[0]);
	return selection;
}

// Individual Origins and Normal axes through the modal keys and the gizmo.
void individualOrigins(QApplication &app, ModelEditorDialog &editor, ModelEditorTools &tools, ModelViewport &view)
{
	QString error;
	auto *pivot = editor.findChild<QComboBox *>("meshTransformPivotMode");
	auto *space = editor.findChild<QComboBox *>("meshTransformSpace");
	if (!expect(pivot && space && pivot->count() == 4 && space->count() == 4, "pivot and axes choices include individual origins and normal"))
		return;
	expect(!pivot->itemText(3).isEmpty() && !space->itemText(3).isEmpty() && !pivot->accessibleDescription().isEmpty(),
		   "the new choices are named and the pivot choice is described");
	auto *snap = tools.menuBar()->findChild<QMenu *>("meshMenuSnap");
	expect(snap && snap->actions().contains(tools.action("meshPivotIndividual")) && snap->actions().contains(tools.action("meshAxesNormal")) &&
			   tools.searchableActions().contains(tools.action("meshPivotIndividual")),
		   "the Snap menu and command search offer individual origins and normal axes");
	tools.action("meshPivotIndividual")->trigger();
	expect(pivot->currentIndex() == int(ModelTransformPivot::IndividualOrigins), "the command chooses individual origins");

	// Modal scale: S, type 0.5, Enter.
	const auto squares = threeSquares();
	expect(editor.setMesh(squares, &error), "open three separate squares", error);
	tools.applySelection(facesOf(squares));
	view.frameModel();
	expect(settleModelViewport(view), "squares settle");
	const QPointF start = view.rect().center() + QPointF(80, 0);
	expect(tools.beginModal(ModelModalOperator::Scale, start), "start modal scale");
	type(tools, "0.5");
	ModelEdit scale;
	scale.kind = ModelEditKind::Transform;
	scale.selection = facesOf(squares);
	scale.pivotMode = ModelTransformPivot::IndividualOrigins;
	scale.scale = {.5f, .5f, .5f};
	auto expected = squares;
	expect(applyModelEdit(&expected, scale, nullptr, &error), "prepare the shared-service result", error);
	expect(tools.modalPreviewValid() && tools.modalPreview() && samePoses(tools.modalPreview()->surfaces[0], expected.surfaces[0]),
		   "the scale preview turns each square about its own centre");
	expect(tools.finishModal(true, &error) && samePoses(editor.document().mesh().surfaces[0], expected.surfaces[0]),
		   "the committed scale matches the preview", error);
	editor.findChild<QAction *>("undoMesh")->trigger();
	app.processEvents();
	expect(bytes(editor.document().mesh()) == bytes(squares) && !editor.document().canUndo(), "the scale is one undo step");

	// Modal move along each piece's own normal: G, Z, Z, type 2.
	tools.action("meshAxesNormal")->trigger();
	expect(space->currentIndex() == int(ModelTransformSpace::Normal), "the command chooses normal axes");
	const auto facings = twoFacings();
	expect(editor.setMesh(facings, &error), "open two squares facing different ways", error);
	tools.applySelection(facesOf(facings));
	expect(settleModelViewport(view), "facings settle");
	expect(tools.beginModal(ModelModalOperator::Move, start), "start modal move");
	tools.constrainModal(2, false);
	tools.constrainModal(2, false);
	type(tools, "2");
	ModelEdit move;
	move.kind = ModelEditKind::Transform;
	move.selection = facesOf(facings);
	move.pivotMode = ModelTransformPivot::IndividualOrigins;
	move.transformSpace = ModelTransformSpace::Normal;
	move.axesFrame = 0;
	move.translation = {0, 0, 2};
	expected = facings;
	expect(applyModelEdit(&expected, move, nullptr, &error), "prepare the normal move", error);
	expect(tools.modalPreviewValid() && samePoses(tools.modalPreview()->surfaces[0], expected.surfaces[0], 1e-4) &&
			   tools.modalText().contains(QStringLiteral("Z")),
		   "a second Z press moves each piece along its own normal");
	expect(tools.finishModal(true, &error) && samePoses(editor.document().mesh().surfaces[0], expected.surfaces[0], 1e-4),
		   "the normal move commits", error);

	// Gizmo: a uniform scale previews and commits per piece.
	space->setCurrentIndex(int(ModelTransformSpace::World));
	expect(editor.setMesh(squares, &error), "reopen the squares for the gizmo", error);
	tools.applySelection(facesOf(squares));
	editor.findChild<QCheckBox *>("meshMoveGizmo")->setChecked(true);
	editor.findChild<QComboBox *>("meshTransformTool")->setCurrentIndex(2);
	view.frameModel();
	expect(settleModelViewport(view), "gizmo view settles");
	const auto handles = view.transformGizmoPoints();
	scale.scale = {2, 2, 2};
	expected = squares;
	expect(applyModelEdit(&expected, scale, nullptr, &error), "prepare the gizmo scale", error);
	expect(view.beginEditTransform(handles[3]) && view.updateEditTransform(handles[3] + QPointF(96, 0)), "drag the uniform scale handle");
	QPointF projected;
	expect(view.projectToView(expected.surfaces[0].frames[0].positions[4], &projected) &&
			   QLineF(view.vertexScreenPosition(0, 4), projected).length() < 1,
		   "the gizmo preview scales each square about its own centre");
	view.finishEditTransform(true);
	app.processEvents();
	expect(samePoses(editor.document().mesh().surfaces[0], expected.surfaces[0]) && editor.document().canUndo(),
		   "the gizmo commit matches its preview");
	editor.findChild<QCheckBox *>("meshMoveGizmo")->setChecked(false);
	editor.findChild<QComboBox *>("meshTransformTool")->setCurrentIndex(0);
	pivot->setCurrentIndex(int(ModelTransformPivot::SelectionCentre));
	expect(editor.setMesh(squares, &error), "restore the squares", error);
}

// Array and Screw from the menus, adjusted with F9, each one undo step.
void arraysAndScrews(QApplication &app, ModelEditorDialog &editor, ModelEditorTools &tools, ModelViewport &view)
{
	QString error;
	QVector<ModelVec3> p;
	QVector<ModelTriangle> t;
	appendSquare(&p, &t, {4, 4, 0}, {1, 0, 0}, {0, 1, 0});
	const auto square = polygonMesh(p, t);
	expect(editor.setMesh(square, &error), "open a square for the array", error);
	tools.applySelection(facesOf(square));
	auto *face = tools.menuBar()->findChild<QMenu *>("meshMenuFace");
	expect(face && face->actions().contains(tools.action("meshArray")) && face->actions().contains(tools.action("meshScrew")) &&
			   tools.searchableActions().contains(tools.action("meshArray")),
		   "the Face menu and command search offer Array and Screw");
	tools.action("meshArray")->trigger();
	app.processEvents();
	expect(editor.document().mesh().surfaces[0].triangles.size() == 6 && editor.document().selection().faces.size() == 6,
		   "Array makes three copies in a row and selects them");
	tools.action("meshAdjustLast")->trigger();
	app.processEvents();
	auto *panel = tools.lastOperationPanel();
	auto fields = panel ? panel->findChildren<QDoubleSpinBox *>() : QList<QDoubleSpinBox *>{};
	if (!expect(panel && panel->isVisible() && fields.size() == 15, "Array's count, offsets, rotation, scale, centre and merge are adjustable"))
		return;
	for (auto *field : fields)
	{
		auto *accessible = QAccessible::queryAccessibleInterface(field);
		expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && (field->focusPolicy() & Qt::TabFocus),
			   "each array setting is named and reachable by keyboard");
	}
	fields[0]->setValue(5);
	app.processEvents();
	expect(editor.document().mesh().surfaces[0].triangles.size() == 10, "F9 changes the count and replays once");
	fields = panel->findChildren<QDoubleSpinBox *>();
	fields[14]->setValue(.01);
	app.processEvents();
	ModelGeometricTopology topology;
	expect(buildModelGeometricTopology(editor.document().mesh().surfaces[0], &topology) && topology.edgeFaces.size() == 21,
		   "F9's merge distance joins the copies into one strip");
	editor.findChild<QAction *>("undoMesh")->trigger();
	app.processEvents();
	expect(bytes(editor.document().mesh()) == bytes(square) && !editor.document().canUndo(), "the adjusted array is one undo step");
	tools.action("meshAdjustLast")->trigger();
	app.processEvents();

	// Screw a vertical profile around the 3D cursor.
	p.clear();
	t.clear();
	appendSquare(&p, &t, {20, 0, 4}, {1, 0, 0}, {0, 0, 1});
	const auto profile = polygonMesh(p, t);
	expect(editor.setMesh(profile, &error), "open a profile for the screw", error);
	tools.applySelection(facesOf(profile));
	tools.setCursor3d({0, 0, 0});
	tools.action("meshScrew")->trigger();
	app.processEvents();
	ModelEdit screw;
	screw.kind = ModelEditKind::Spin;
	screw.selection = facesOf(profile);
	screw.tool.spinAngle = 360;
	screw.tool.spinSteps = 16;
	screw.tool.spinAxialOffset = 16;
	auto expected = profile;
	expect(applyModelEdit(&expected, screw, nullptr, &error) && bytes(editor.document().mesh()) == bytes(expected),
		   "Screw climbs one full turn around the cursor through the shared spin service", error);
	tools.action("meshAdjustLast")->trigger();
	app.processEvents();
	fields = panel->findChildren<QDoubleSpinBox *>();
	if (!expect(fields.size() == 10, "Spin's adjustment adds the axial offset"))
		return;
	fields[9]->setValue(32);
	app.processEvents();
	screw.tool.spinAxialOffset = 32;
	expected = profile;
	expect(applyModelEdit(&expected, screw, nullptr, &error) && bytes(editor.document().mesh()) == bytes(expected),
		   "F9 changes the climb", error);
	tools.action("meshAdjustLast")->trigger();
	app.processEvents();
	expect(editor.setMesh(profile, &error), "restore the profile", error);
	(void)view;
}

// Alt+E: Extrude Along Normals with a typed distance, F9 mode and wall UVs.
void extrudeVariants(QApplication &app, ModelEditorDialog &editor, ModelEditorTools &tools, ModelViewport &view)
{
	QString error;
	const auto grid = polygonGrid(2, 1);
	expect(editor.setMesh(grid, &error), "open a grid to extrude", error);
	tools.applySelection(facesOf(grid));
	expect(settleModelViewport(view), "grid settles");
	auto *menu = tools.menuBar()->findChild<QMenu *>("meshMenuExtrude");
	expect(menu && menu->actions().contains(tools.action("meshExtrudeNormals")) && menu->actions().contains(tools.action("meshExtrudeIndividual")) &&
			   tools.action("meshExtrudeMenu")->shortcut() == QKeySequence(QStringLiteral("Alt+E")),
		   "Alt+E opens the extrude variants");
	const QPointF start = view.rect().center() + QPointF(40, 0);
	expect(tools.beginModal(ModelModalOperator::ExtrudeNormals, start), "start extrude along normals");
	tools.constrainModal(0, false);
	type(tools, "2");
	ModelEdit expectedEdit;
	expectedEdit.kind = ModelEditKind::ExtrudeFaces;
	expectedEdit.selection = facesOf(grid);
	expectedEdit.tool.offset = 2;
	expectedEdit.tool.extrudeMode = ModelExtrudeMode::AlongNormals;
	expectedEdit.tool.extrudeTileSize = 64;
	auto expected = grid;
	expect(applyModelEdit(&expected, expectedEdit, nullptr, &error), "prepare the shared extrusion", error);
	expect(tools.modalPreviewValid() && bytes(*tools.modalPreview()) == bytes(expected) && tools.modalText().contains(QStringLiteral("2")),
		   "the typed distance previews the shared result; axis keys do not apply");
	expect(tools.finishModal(true, &error) && bytes(editor.document().mesh()) == bytes(expected) &&
			   editor.document().selection().faces.size() == 4,
		   "the extrusion commits with its caps selected", error);
	tools.action("meshAdjustLast")->trigger();
	app.processEvents();
	auto *panel = tools.lastOperationPanel();
	auto *mode = panel ? panel->findChild<QComboBox *>() : nullptr;
	if (!expect(mode && mode->count() == 3 && panel->findChildren<QCheckBox *>().size() == 1, "F9 offers the mode and even thickness"))
		return;
	mode->setCurrentIndex(int(ModelExtrudeMode::Individual));
	app.processEvents();
	expectedEdit.tool.extrudeMode = ModelExtrudeMode::Individual;
	expected = grid;
	expect(applyModelEdit(&expected, expectedEdit, nullptr, &error) && bytes(editor.document().mesh()) == bytes(expected) &&
			   editor.document().mesh().surfaces[0].triangles.size() == 28,
		   "F9 switches to individual faces and replays once", error);
	editor.findChild<QAction *>("undoMesh")->trigger();
	app.processEvents();
	expect(bytes(editor.document().mesh()) == bytes(grid) && !editor.document().canUndo(), "the extrusion is one undo step");
	tools.action("meshAdjustLast")->trigger();
	app.processEvents();
	// Without faces the variants refuse with a reason.
	tools.applySelection({});
	expect(!tools.beginModal(ModelModalOperator::ExtrudeIndividual, start), "individual extrusion needs faces");
}

// Bridge Edge Loops from the menu, with F9 segments; Bridge Faces is offered.
void bridges(QApplication &app, ModelEditorDialog &editor, ModelEditorTools &tools)
{
	QString error;
	const auto disks = bridgeDisks();
	expect(editor.setMesh(disks, &error), "open two disks to bridge", error);
	ModelSelection edges;
	edges.edges = {modelEdge(0, 1), modelEdge(4, 5)};
	tools.applySelection(edges);
	auto *edge = tools.menuBar()->findChild<QMenu *>("meshMenuEdge");
	auto *face = tools.menuBar()->findChild<QMenu *>("meshMenuFace");
	expect(edge && face && edge->actions().contains(tools.action("meshBridgeChains")) && face->actions().contains(tools.action("meshBridgeFaces")),
		   "the Edge and Face menus offer chain and face bridging");
	tools.action("meshBridge")->trigger();
	app.processEvents();
	expect(editor.document().mesh().surfaces[0].triangles.size() == 12, "Bridge Edge Loops joins the disks");
	tools.action("meshAdjustLast")->trigger();
	app.processEvents();
	auto *panel = tools.lastOperationPanel();
	auto fields = panel ? panel->findChildren<QDoubleSpinBox *>() : QList<QDoubleSpinBox *>{};
	if (!expect(fields.size() == 3 && panel->findChildren<QCheckBox *>().size() == 1, "F9 offers twist, segments, open chains and the UV strip"))
		return;
	fields[1]->setValue(2);
	app.processEvents();
	expect(editor.document().mesh().surfaces[0].triangles.size() == 44, "F9 adds two rings and replays once");
	fields = panel->findChildren<QDoubleSpinBox *>();
	fields[2]->setValue(4);
	app.processEvents();
	expect(editor.document().mesh().surfaces[0].vertexCount == 8 + 18 + 10, "F9 gives the bridge its own UV strip");
	editor.findChild<QAction *>("undoMesh")->trigger();
	app.processEvents();
	expect(bytes(editor.document().mesh()) == bytes(disks) && !editor.document().canUndo(), "the adjusted bridge is one undo step");
	tools.action("meshAdjustLast")->trigger();
	app.processEvents();
}

// Symmetry X: E on one side extrudes both; Mirror Faces is in the Symmetrize menu.
void symmetry(QApplication &app, ModelEditorDialog &editor, ModelEditorTools &tools, ModelViewport &view)
{
	QString error;
	QVector<ModelVec3> p;
	QVector<ModelTriangle> t;
	for (int j = 0; j <= 2; ++j)
		for (int i = 0; i <= 4; ++i)
			p << ModelVec3{float(-16 + 8 * i), float(8 * j), 0};
	for (int j = 0; j < 2; ++j)
		for (int i = 0; i < 4; ++i)
			t << ModelTriangle{j * 5 + i, j * 5 + i + 1, (j + 1) * 5 + i + 1} << ModelTriangle{j * 5 + i, (j + 1) * 5 + i + 1, (j + 1) * 5 + i};
	const auto grid = polygonMesh(p, t);
	expect(editor.setMesh(grid, &error), "open a symmetric grid", error);
	auto *mirrorX = editor.findChild<QToolButton *>("meshMirrorX");
	if (!expect(mirrorX != nullptr, "the Symmetry X button exists"))
		return;
	mirrorX->setChecked(true);
	ModelSelection selected;
	selected.faces = {6, 7};
	tools.applySelection(selected);
	expect(settleModelViewport(view), "grid settles");
	expect(tools.beginModal(ModelModalOperator::Extrude, view.rect().center() + QPointF(40, 0)), "start a symmetric extrusion");
	type(tools, "4");
	ModelEdit expectedEdit;
	expectedEdit.kind = ModelEditKind::Extrude;
	expectedEdit.selection = selected;
	expectedEdit.translation = {0, 0, 4};
	expectedEdit.tool.symmetryAxes = 1;
	auto expected = grid;
	expect(applyModelEdit(&expected, expectedEdit, nullptr, &error), "prepare the symmetric extrusion", error);
	expect(tools.modalPreviewValid() && samePoses(tools.modalPreview()->surfaces[0], expected.surfaces[0], 1e-4) &&
			   tools.modalPreview()->surfaces[0].triangles.size() == 32,
		   "the preview extrudes the mirrored quad too");
	expect(tools.finishModal(true, &error) && editor.document().mesh().surfaces[0].triangles.size() == 32 &&
			   editor.document().selection().faces.size() == 4,
		   "the commit mirrors the extrusion and selects both caps", error);
	editor.findChild<QAction *>("undoMesh")->trigger();
	app.processEvents();
	expect(bytes(editor.document().mesh()) == bytes(grid), "the symmetric extrusion is one undo step");
	mirrorX->setChecked(false);
	auto *symmetrize = tools.menuBar()->findChild<QMenu *>("meshMenuSymmetrize");
	expect(symmetrize && symmetrize->actions().contains(tools.action("meshMirrorFacesX")), "Mirror Faces is in the Symmetrize menu");
	tools.applySelection(selected);
	tools.action("meshMirrorFacesX")->trigger();
	app.processEvents();
	expect(bytes(editor.document().mesh()) == bytes(grid) && !editor.document().canUndo(), "Mirror Faces refuses faces attached to the rest");

	// Gizmo drags: Symmetry X, then proportional editing, preview and commit
	// what the matching Weighted Transform does, each as one undo step.
	auto *gizmo = editor.findChild<QCheckBox *>("meshMoveGizmo");
	auto *proportional = editor.findChild<QToolButton *>("meshProportional");
	auto *radius = editor.findChild<QDoubleSpinBox *>("meshProportionalRadius");
	if (!expect(gizmo && proportional && radius, "the gizmo and proportional controls exist"))
		return;
	gizmo->setChecked(true);
	editor.findChild<QComboBox *>("meshTransformTool")->setCurrentIndex(0);
	for (int pass = 0; pass < 2; ++pass)
	{
		mirrorX->setChecked(pass == 0);
		proportional->setChecked(pass == 1);
		radius->setValue(10);
		ModelSelection vertex;
		vertex.vertices = {3};
		tools.applySelection(vertex);
		view.frameModel();
		expect(settleModelViewport(view), "gizmo view settles");
		const auto handles = view.transformGizmoPoints();
		const QPointF along = handles[0] - handles[3];
		const double reach = std::hypot(along.x(), along.y());
		expect(reach > 1 && view.beginEditTransform(handles[0]) && view.updateEditTransform(handles[0] + along * (40 / reach)),
			   "drag the X move handle");
		ModelEdit weighted;
		weighted.kind = ModelEditKind::WeightedTransform;
		weighted.selection = vertex;
		weighted.translation = view.editTransform().translation;
		weighted.tool.mirrorAxes = pass == 0 ? 1 : 0;
		weighted.tool.proportionalRadius = pass == 1 ? 10 : 0;
		auto expected = grid;
		expect(weighted.translation.x > 0.5f && applyModelEdit(&expected, weighted, nullptr, &error), "prepare the weighted move", error);
		// Vertex 1 is the counterpart across X; vertex 4 lies inside the radius.
		const int follower = pass == 0 ? 1 : 4;
		QPointF projected;
		expect(view.projectToView(expected.surfaces[0].frames[0].positions[follower], &projected) &&
				   QLineF(view.vertexScreenPosition(0, follower), projected).length() < 1 &&
				   !nearPoint(expected.surfaces[0].frames[0].positions[follower], grid.surfaces[0].frames[0].positions[follower], 0.1),
			   pass == 0 ? "the gizmo preview moves the mirror counterpart the other way" : "the gizmo preview drags nearby vertices with the falloff");
		view.finishEditTransform(true);
		app.processEvents();
		expect(samePoses(editor.document().mesh().surfaces[0], expected.surfaces[0], 1e-4) && editor.document().canUndo(),
			   pass == 0 ? "the symmetric gizmo commit matches its preview" : "the proportional gizmo commit matches its preview");
		editor.findChild<QAction *>("undoMesh")->trigger();
		app.processEvents();
		expect(bytes(editor.document().mesh()) == bytes(grid), "the gizmo drag is one undo step");
	}
	mirrorX->setChecked(false);
	proportional->setChecked(false);
	gizmo->setChecked(false);
}

// Poly Build in the front view: three clicks make a face, the fourth carries
// on as a strip, a click near a vertex reuses it, each face is one undo step.
void polyBuild(QApplication &app, ModelEditorDialog &editor, ModelEditorTools &tools, ModelViewport &view)
{
	QString error;
	QVector<ModelVec3> p;
	QVector<ModelTriangle> t;
	appendSquare(&p, &t, {-20, 0, 0}, {1, 0, 0}, {0, 0, 1});
	const auto base = polygonMesh(p, t, 1);
	expect(editor.setMesh(base, &error), "open a square to build beside", error);
	editor.findChild<QComboBox *>("meshViewPreset")->setCurrentIndex(2);
	tools.setCursor3d({0, 0, 0});
	view.frameModel();
	expect(settleModelViewport(view), "front view settles");
	auto *vertexMenu = tools.menuBar()->findChild<QMenu *>("meshMenuVertex");
	expect(vertexMenu && vertexMenu->actions().contains(tools.action("meshPolyBuild")), "the Vertex menu offers Poly Build");
	expect(tools.beginModal(ModelModalOperator::PolyBuild, view.rect().center()), "start Poly Build");
	const auto at = [&](ModelVec3 world)
	{
		QPointF screen;
		view.projectToView(world, &screen);
		return screen;
	};
	expect(tools.addPolyBuildPoint(at({0, 0, 0})) && tools.addPolyBuildPoint(at({8, 0, 0})) && tools.modalText().contains(QStringLiteral("2")),
		   "two corners wait for a third");
	expect(tools.addPolyBuildPoint(at({0, 0, 8})), "the third corner makes a face");
	const auto &mesh = editor.document().mesh().surfaces[0];
	expect(mesh.triangles.size() == 3 && mesh.vertexCount == 7 && editor.document().canUndo(), "one face of three new vertices");
	bool placed = false;
	for (const auto &q : mesh.frames[0].positions)
		placed = placed || nearPoint(q, {8, 0, 0}, 0.05);
	expect(placed, "a click lands on the view plane through the 3D cursor");
	expect(tools.addPolyBuildPoint(at({8, 0, 8})) && editor.document().mesh().surfaces[0].triangles.size() == 4 &&
			   editor.document().mesh().surfaces[0].vertexCount == 8 && consistentlyWound(editor.document().mesh().surfaces[0], true),
		   "the fourth corner continues the strip, wound with its neighbour");
	tools.newPolyBuildFace();
	// Snap to the square's corner (-16, 0, -4) and two new points.
	expect(tools.addPolyBuildPoint(at({-16, 0, -4}) + QPointF(3, 2)) && tools.addPolyBuildPoint(at({-10, 0, -4})) &&
			   tools.addPolyBuildPoint(at({-10, 0, 4})) && editor.document().mesh().surfaces[0].vertexCount == 10,
		   "a click near a vertex reuses it");
	tools.newPolyBuildFace();
	tools.addPolyBuildPoint(at({-12, 0, 0}));
	tools.addPolyBuildPoint(at({-12, 0, 2}));
	tools.undoPolyBuildPoint();
	expect(tools.modalText().contains(QStringLiteral("1")) && editor.document().mesh().surfaces[0].triangles.size() == 5,
		   "Backspace takes back the last corner without making a face");
	tools.finishModal(false);
	expect(tools.modalOperator() == ModelModalOperator::None && editor.document().mesh().surfaces[0].triangles.size() == 5,
		   "ending Poly Build keeps every face");
	for (int step = 0; step < 3; ++step)
		editor.findChild<QAction *>("undoMesh")->trigger();
	app.processEvents();
	expect(bytes(editor.document().mesh()) == bytes(base) && !editor.document().canUndo(), "each face is one undo step");
	editor.findChild<QComboBox *>("meshViewPreset")->setCurrentIndex(0);
}
} // namespace
int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont("Segoe UI", 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temp(QDir(root).filePath("polygon-ui-XXXXXX"));
	if (!temp.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("polygon tools UI"); unavailable >= 0)
		return unavailable;
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario)
			app.installTranslator(&expansion);
		app.setLayoutDirection(scenario == 2 ? Qt::RightToLeft : Qt::LeftToRight);
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario ? 200 : 100));
		ModelEditorDialog editor;
		editor.setAttribute(Qt::WA_DeleteOnClose, false);
		editor.resize(1500, 1000);
		editor.show();
		app.processEvents();
		auto *tools = editor.tools();
		auto *view = editor.findChild<ModelViewport *>("meshPreview");
		if (!expect(tools && view, "editor exposes tools and viewport"))
			return 1;
		tools->applyControls(modelEditorControlsForProfile("studio"));
		individualOrigins(app, editor, *tools, *view);
		arraysAndScrews(app, editor, *tools, *view);
		extrudeVariants(app, editor, *tools, *view);
		bridges(app, editor, *tools);
		symmetry(app, editor, *tools, *view);
		polyBuild(app, editor, *tools, *view);
		editor.close();
		app.processEvents();
		if (scenario)
			app.removeTranslator(&expansion);
	}
	std::cout << expect.checks << " polygon tool UI checks; " << expect.failures << " failures\n";
	return expect.failures ? 1 : 0;
}
