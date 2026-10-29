// Mesh Editor commands for Align Vertices and the topology tools of the
// polygon modelling round: profile keys, menus, Adjust Last Operation, the Item
// page's selection position row, snapped drags and one undo step per command.
#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_viewport.h"
#include "core/model_design.h"
#include "core/studio_settings.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFont>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>
#include <QTimer>

#include <cmath>
#include <cstdlib>
#include <iostream>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

using namespace vibestudio;
namespace
{
int checks = 0, failures = 0;
bool expect(bool condition, const char *message)
{
	++checks;
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return condition;
}
bool near(double a, double b, double tolerance = 1e-3)
{
	return std::abs(a - b) <= tolerance;
}
// A 4x4 grid of 8-unit quads with uneven heights, two poses (the second raised by 8).
ModelMesh gridMesh(int cells = 4, double size = 8, bool flat = false)
{
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	auto mesh = buildModelDesignMesh(design);
	mesh.frames << mesh.frames[0];
	mesh.frames[1].name = QStringLiteral("pose2");
	auto &surface = mesh.surfaces[0];
	QVector<ModelVec3> positions;
	QVector<ModelTriangle> triangles;
	for (int j = 0; j <= cells; ++j)
		for (int i = 0; i <= cells; ++i)
			positions.append(flat ? ModelVec3{float(i * size), float(j * size), 0}
								  : ModelVec3{float(i * size + 0.3 * j), float(j * size - 0.2 * i), float((i * 7 + j * 3) % 5) * 0.5f});
	const auto at = [&](int i, int j) { return j * (cells + 1) + i; };
	for (int j = 0; j < cells; ++j)
		for (int i = 0; i < cells; ++i)
		{
			triangles.append({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
			triangles.append({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
		}
	surface.triangles = triangles;
	surface.vertexCount = positions.size();
	surface.texCoords.clear();
	for (const auto &p : positions)
		surface.texCoords.append({p.x / 64.0f, p.y / 64.0f});
	ModelFrameGeometry frame;
	frame.positions = positions;
	frame.normals.fill({0, 0, 1}, positions.size());
	surface.frames = {frame, frame};
	for (auto &p : surface.frames[1].positions)
		p.z += 8;
	surface.uvSeams.clear();
	surface.topologyVertices.clear();
	updateEditableModelMetadata(&mesh);
	return mesh;
}
QDoubleSpinBox *lastField(ModelEditorTools *tools, const QString &name)
{
	for (auto *spin : tools->lastOperationPanel()->findChildren<QDoubleSpinBox *>())
		if (spin->accessibleName() == name)
			return spin;
	return nullptr;
}

void selectChecks(QApplication &app, ModelEditorDialog &editor)
{
	auto *tools = editor.tools();
	const auto &document = editor.document();
	const auto revision = document.revisionFingerprint();
	tools->action(QStringLiteral("meshModeFace"))->trigger();
	tools->applySelection({0, {}, {0}});
	tools->action(QStringLiteral("meshAdjustLast"))->trigger();
	tools->action(QStringLiteral("meshSelectSimilarNormal"))->trigger();
	app.processEvents();
	const int narrow = document.selection().faces.size();
	auto *threshold = lastField(tools, QStringLiteral("Threshold (degrees)"));
	if (expect(threshold && near(threshold->value(), 5), "Adjust Last Operation shows Select Similar's threshold"))
	{
		threshold->setValue(60);
		app.processEvents();
		const int wide = document.selection().faces.size();
		expect(wide > narrow && document.revisionFingerprint() == revision && document.undoSteps() == 0,
			   "a wider threshold selects more faces without touching the document or its history");
		threshold = lastField(tools, QStringLiteral("Threshold (degrees)"));
		if (expect(threshold, "the threshold stays adjustable"))
		{
			threshold->setValue(5);
			app.processEvents();
			expect(int(document.selection().faces.size()) == narrow, "the same threshold gives the same selection again");
		}
	}
	// Random: the seed and ratio reproduce a selection exactly.
	tools->applySelection({0, {}, {}});
	tools->action(QStringLiteral("meshSelectRandom"))->trigger();
	app.processEvents();
	auto *seed = lastField(tools, QStringLiteral("Seed"));
	if (expect(seed && lastField(tools, QStringLiteral("Ratio")), "Adjust Last Operation shows Random's ratio and seed"))
	{
		seed->setValue(7);
		app.processEvents();
		const auto seven = document.selection().faces;
		lastField(tools, QStringLiteral("Seed"))->setValue(8);
		app.processEvents();
		const auto eight = document.selection().faces;
		lastField(tools, QStringLiteral("Seed"))->setValue(7);
		app.processEvents();
		expect(seven != eight && document.selection().faces == seven, "a seed reproduces its random selection");
		lastField(tools, QStringLiteral("Ratio"))->setValue(1);
		app.processEvents();
		expect(int(document.selection().faces.size()) == int(document.mesh().surfaces[0].triangles.size()), "a ratio of one selects every face");
	}
	expect(document.revisionFingerprint() == revision, "selection adjustments never edit the document");
	tools->action(QStringLiteral("meshAdjustLast"))->trigger();
	tools->applySelection({0, {}, {}});
}

void alignChecks(QApplication &app, ModelEditorDialog &editor)
{
	auto *tools = editor.tools();
	const auto &document = editor.document();
	QString error;
	tools->applyControls(modelEditorControlsForProfile(QStringLiteral("milkshape-3d")));
	expect(tools->action(QStringLiteral("meshFlattenX"))->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X) &&
			   tools->action(QStringLiteral("meshSnapSelectionToGrid"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_G),
		   "MilkShape binds Flatten X to Ctrl+Shift+X and Snap to Grid to Ctrl+G");
	const QSet<int> row{0, 1, 2, 3, 4};
	tools->applySelection({0, row, {}});
	const int steps = document.undoSteps();
	tools->action(QStringLiteral("meshFlattenX"))->trigger();
	app.processEvents();
	const auto &surface = document.mesh().surfaces[0];
	bool flat = true;
	double mean = 0;
	for (int v : row)
		mean += 0.2 * (v * 8);
	for (int v : row)
		flat &= near(surface.frames[0].positions[v].x, mean) && near(surface.frames[1].positions[v].x, mean);
	expect(flat && document.undoSteps() == steps + 1, "MilkShape Flatten X gives the row one X, its centroid, as one undo step");
	// Adjust Last Operation: an exact value instead of the centroid.
	auto *target = tools->lastOperationPanel()->findChild<QComboBox *>();
	if (expect(target && target->accessibleName() == QStringLiteral("Align to"), "Adjust Last Operation shows the alignment target"))
	{
		target->setCurrentIndex(0);
		app.processEvents();
		app.processEvents();
		auto *value = lastField(tools, QStringLiteral("Value X"));
		if (expect(value, "choosing Value shows the value fields"))
		{
			value->setValue(3);
			app.processEvents();
			bool exact = true;
			for (int v : row)
				exact &= document.mesh().surfaces[0].frames[0].positions[v].x == 3.0f;
			expect(exact && document.undoSteps() == steps + 1, "the adjusted value replaces the flatten in place");
		}
	}
	editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();
	expect(document.undoSteps() == steps && near(document.mesh().surfaces[0].frames[0].positions[1].x, 8),
		   "one undo restores the row");
	// Snap Selection to Grid: one step, every coordinate on the grid.
	tools->applySelection({0, {6, 7, 8, 12}, {}});
	tools->action(QStringLiteral("meshSnapSelectionToGrid"))->trigger();
	app.processEvents();
	const double grid = editor.findChild<QDoubleSpinBox *>(QStringLiteral("meshTranslationGrid"))
							? editor.findChild<QDoubleSpinBox *>(QStringLiteral("meshTranslationGrid"))->value()
							: 0;
	bool snapped = grid > 0;
	for (int v : {6, 7, 8, 12})
		for (const auto &frame : document.mesh().surfaces[0].frames)
		{
			const auto p = frame.positions[v];
			for (double c : {double(p.x), double(p.y), double(p.z)})
				snapped &= near(std::remainder(c, grid), 0, 1e-4);
		}
	expect(snapped && document.undoSteps() == steps + 1, "Snap Selection to Grid snaps every selected vertex as one undo step");
	editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	// The Snap menu reaches the same commands.
	auto *snap = editor.findChild<QMenu *>(QStringLiteral("meshMenuSnap"));
	expect(snap && snap->actions().contains(tools->action(QStringLiteral("meshSnapSelectionToGrid"))) &&
			   editor.findChild<QMenu *>(QStringLiteral("meshMenuAlign")),
		   "the Snap menu lists Snap Selection to Grid and the Align submenu");
	// The Item page's selection position row moves the selection's median.
	tools->applySelection({0, {10, 15}, {}});
	app.processEvents();
	auto *positionX = editor.findChild<QDoubleSpinBox *>(QStringLiteral("meshSelectionPositionX"));
	if (expect(positionX && positionX->isEnabled() && !positionX->accessibleName().isEmpty(), "the Item page shows the selection position"))
	{
		// The selected X coordinates are 0.6 and 0.9, so the median moves by -4.75.
		positionX->setValue(-4);
		app.processEvents();
		expect(near(document.mesh().surfaces[0].frames[0].positions[10].x, -4.15) && near(document.mesh().surfaces[0].frames[1].positions[15].x, -3.85) &&
				   document.undoSteps() == steps + 1,
			   "typing a selection position moves the selection so its median takes the value, in one step");
		editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		app.processEvents();
		expect(near(positionX->value(), (0.3 * 2 + 0.3 * 3) / 2), "the row shows the restored centroid after undo");
	}
	tools->applyControls(modelEditorControlsForProfile(QStringLiteral("studio")));
}

void transformModelChecks(QApplication &app, ModelEditorDialog &editor)
{
	auto *tools = editor.tools();
	const auto &document = editor.document();
	const int steps = document.undoSteps();
	const auto lowest = [&](int frame)
	{
		double low = INFINITY;
		for (const auto &p : document.mesh().surfaces[0].frames[frame].positions)
			low = std::min(low, double(p.z));
		return low;
	};
	tools->applySelection({0, {3}, {}});
	tools->action(QStringLiteral("meshOriginBoundsBottom"))->trigger();
	app.processEvents();
	expect(near(lowest(0), 0) && document.undoSteps() == steps + 1 && document.selection().vertices == QSet<int>{3},
		   "Origin to Bounds Bottom puts the feet on the origin in one step and keeps the selection");
	auto *move = lastField(tools, QStringLiteral("Move Z"));
	expect(move && tools->lastOperationPanel()->findChildren<QDoubleSpinBox *>().size() == 12,
		   "Adjust Last Operation offers move, rotate, scale and pivot");
	editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();
	const auto before = document.mesh().surfaces[0].frames[0].positions[1];
	tools->action(QStringLiteral("meshTurnModelLeft"))->trigger();
	app.processEvents();
	const auto after = document.mesh().surfaces[0].frames[0].positions[1];
	expect(near(after.x, -before.y) && near(after.y, before.x) && document.undoSteps() == steps + 1, "Turn Model 90° Left turns about Z");
	editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();
	// The numeric dialog, with a live preview in the view.
	auto *viewport = editor.findChild<ModelViewport *>(QStringLiteral("meshPreview"));
	bool previewed = false, refused = false, restored = false;
	QTimer::singleShot(0, &app,
					   [&]
					   {
						   for (auto *widget : QApplication::topLevelWidgets())
							   if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->objectName() == QStringLiteral("meshTransformModelDialog"))
							   {
								   auto *offset = dialog->findChild<QDoubleSpinBox *>(QStringLiteral("meshTransformModelOffsetX"));
								   auto *scale = dialog->findChild<QDoubleSpinBox *>(QStringLiteral("meshTransformModelScaleX"));
								   auto *problem = dialog->findChild<QLabel *>(QStringLiteral("meshTransformModelProblem"));
								   if (offset && scale && problem && viewport)
								   {
									   offset->setValue(10);
									   previewed = near(viewport->mesh().surfaces[0].frames[0].positions[1].x, before.x + 10) &&
												   near(document.mesh().surfaces[0].frames[0].positions[1].x, before.x) && document.undoSteps() == steps;
									   scale->setValue(0);
									   refused = !problem->isHidden() && !problem->text().isEmpty();
									   scale->setValue(1);
									   restored = problem->isHidden();
								   }
								   dialog->accept();
							   }
					   });
	tools->action(QStringLiteral("meshTransformModel"))->trigger();
	app.processEvents();
	expect(previewed, "the view previews the dialog's offset before the document changes");
	expect(refused && restored, "the dialog names a refused value and clears it once the value is valid");
	expect(near(document.mesh().surfaces[0].frames[0].positions[1].x, before.x + 10) && document.undoSteps() == steps + 1,
		   "Transform Model… applies the dialog's offset as one step");
	editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();
	// Cancel leaves the document and its view as they were.
	QTimer::singleShot(0, &app,
					   [&]
					   {
						   for (auto *widget : QApplication::topLevelWidgets())
							   if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->objectName() == QStringLiteral("meshTransformModelDialog"))
							   {
								   if (auto *offset = dialog->findChild<QDoubleSpinBox *>(QStringLiteral("meshTransformModelOffsetX")))
									   offset->setValue(5);
								   dialog->reject();
							   }
					   });
	tools->action(QStringLiteral("meshTransformModel"))->trigger();
	app.processEvents();
	expect(viewport && near(viewport->mesh().surfaces[0].frames[0].positions[1].x, before.x) && document.undoSteps() == steps,
		   "cancelling the dialog restores the view and changes nothing");
	auto *menu = editor.findChild<QMenu *>(QStringLiteral("meshMenuTransformModel"));
	expect(menu && menu->actions().contains(tools->action(QStringLiteral("meshScaleModelHeight"))) &&
			   editor.findChild<QMenu *>(QStringLiteral("meshMenuSnap"))->actions().contains(tools->action(QStringLiteral("meshOriginCursor"))),
		   "Mesh > Transform Model and the Snap menu list the model commands");
}

void connectChecks(QApplication &app, ModelEditorDialog &editor)
{
	auto *tools = editor.tools();
	const auto &document = editor.document();
	QString error;
	expect(editor.setMesh(gridMesh(3, 8, true), &error), "open a flat grid");
	expect(tools->action(QStringLiteral("meshConnectVertexPath"))->shortcut() == QKeySequence(Qt::Key_J), "J connects vertices in the studio profile");
	// Opposite corners of the first quad: 1 (8,0) and 4 (0,8).
	tools->applySelection({0, {1, 4}, {}});
	tools->action(QStringLiteral("meshConnectVertexPath"))->trigger();
	app.processEvents();
	const auto &surface = document.mesh().surfaces[0];
	bool joined = false;
	for (const auto &t : surface.triangles)
		for (const auto &[a, b] : {std::pair{t.a, t.b}, std::pair{t.b, t.c}, std::pair{t.c, t.a}})
			joined |= modelEdge(a, b) == modelEdge(1, 4);
	expect(joined && surface.vertexCount == 16 && document.undoSteps() == 1, "Connect Vertex Path turns the quad's diagonal as one step");
	editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();
	// Three vertices picked one at a time join in that order, as the CLI's
	// --vertices 0,6,13 path does.
	auto expected = gridMesh(3, 8, true);
	ModelEdit path;
	path.kind = ModelEditKind::ConnectVertices;
	path.selection = {0, {0, 6, 13}, {}};
	path.tool.topology.vertices = {0, 6, 13};
	expect(applyModelEdit(&expected, path, nullptr, &error) && expected.surfaces[0].vertexCount > 16, "the core cuts the path 0, 6, 13");
	for (const QSet<int> &picked : {QSet<int>{0}, QSet<int>{0, 6}, QSet<int>{0, 6, 13}})
	{
		tools->applySelection({0, picked, {}});
		app.processEvents();
	}
	tools->action(QStringLiteral("meshConnectVertexPath"))->trigger();
	app.processEvents();
	expect(document.undoSteps() == 1 && document.mesh().surfaces[0].vertexCount == expected.surfaces[0].vertexCount &&
			   document.mesh().surfaces[0].triangles.size() == expected.surfaces[0].triangles.size(),
		   "Connect Vertex Path follows the picking order for three vertices");
	editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();
	// Picked all at once (as a box selection does), the order is unknown and
	// no path is cut through them.
	tools->applySelection({0, {}, {}});
	tools->applySelection({0, {0, 6, 13}, {}});
	app.processEvents();
	tools->action(QStringLiteral("meshConnectVertexPath"))->trigger();
	app.processEvents();
	expect(document.mesh().surfaces[0].vertexCount == 16, "three vertices selected at once are not joined as a path");
	if (document.undoSteps() > 0)
		editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();
	// Connect Edges in vertex mode: the left and right sides of the first quad.
	tools->applySelection({0, {}, {}, {modelEdge(0, 4), modelEdge(1, 5)}});
	tools->action(QStringLiteral("meshConnectEdges"))->trigger();
	app.processEvents();
	expect(document.mesh().surfaces[0].vertexCount == 18 && document.undoSteps() == 1, "Connect Edges adds a cut between opposite sides");
	auto *cuts = lastField(tools, QStringLiteral("Number of cuts"));
	if (expect(cuts, "Adjust Last Operation shows the number of cuts"))
	{
		cuts->setValue(3);
		app.processEvents();
		expect(document.mesh().surfaces[0].vertexCount == 22 && document.undoSteps() == 1, "three cuts replace one in place");
	}
	editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();
	auto *edgeMenu = editor.findChild<QMenu *>(QStringLiteral("meshMenuEdge"));
	auto *vertexMenu = editor.findChild<QMenu *>(QStringLiteral("meshMenuVertex"));
	expect(edgeMenu && edgeMenu->actions().contains(tools->action(QStringLiteral("meshConnectEdges"))) && vertexMenu &&
			   vertexMenu->actions().contains(tools->action(QStringLiteral("meshConnectVertexPath"))),
		   "the Vertex and Edge menus list Connect");
	tools->applyControls(modelEditorControlsForProfile(QStringLiteral("3ds-max")));
	expect(tools->action(QStringLiteral("meshConnectEdges"))->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E) &&
			   tools->action(QStringLiteral("meshLoopCut"))->shortcut().isEmpty(),
		   "3ds Max's Ctrl+Shift+E runs Connect rather than a loop cut");
	tools->applyControls(modelEditorControlsForProfile(QStringLiteral("studio")));
}

void dissolveChecks(QApplication &app, ModelEditorDialog &editor)
{
	auto *tools = editor.tools();
	const auto &document = editor.document();
	QString error;
	expect(editor.setMesh(gridMesh(3, 8, true), &error), "open a flat grid for dissolving");
	auto *remove = editor.findChild<QMenu *>(QStringLiteral("meshMenuDelete"));
	expect(remove && remove->actions().contains(tools->action(QStringLiteral("meshDeleteVertices"))) &&
			   remove->actions().contains(tools->action(QStringLiteral("meshDeleteEdges"))) &&
			   remove->actions().contains(tools->action(QStringLiteral("meshDissolveEdges"))) &&
			   remove->actions().contains(tools->action(QStringLiteral("meshLimitedDissolve"))),
		   "the Delete menu offers Delete Vertices, Delete Edges, Dissolve Edges and Limited Dissolve");
	// Delete Vertices removes the vertex's fan.
	tools->applySelection({0, {5}, {}});
	tools->action(QStringLiteral("meshDeleteVertices"))->trigger();
	app.processEvents();
	expect(document.mesh().surfaces[0].triangles.size() == 12 && document.undoSteps() == 1, "Delete Vertices removes six faces as one step");
	auto *mode = tools->lastOperationPanel()->findChild<QComboBox *>();
	if (expect(mode && mode->accessibleName() == QStringLiteral("Delete"), "Adjust Last Operation offers the delete mode"))
	{
		mode->setCurrentIndex(2);
		app.processEvents();
		expect(document.undoSteps() == 1, "switching to Faces with no face selected keeps the history unchanged");
	}
	editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();
	// Limited Dissolve with nothing selected simplifies the whole flat grid.
	tools->applySelection({0, {}, {}});
	tools->action(QStringLiteral("meshLimitedDissolve"))->trigger();
	app.processEvents();
	expect(document.mesh().surfaces[0].triangles.size() == 2 && document.undoSteps() == 1, "Limited Dissolve leaves two triangles in one step");
	auto *angle = lastField(tools, QStringLiteral("Max angle"));
	expect(angle && near(angle->value(), 5), "Adjust Last Operation shows the angle");
	editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();
	expect(document.mesh().surfaces[0].triangles.size() == 18, "one undo restores the grid");
}

// Faces whose winding points away from the bounds centre.
int outwardFaces(const ModelMesh &mesh)
{
	const auto &s = mesh.surfaces[0];
	double centre[3]{0, 0, 0};
	for (const auto &p : s.frames[0].positions)
	{
		centre[0] += p.x / s.vertexCount;
		centre[1] += p.y / s.vertexCount;
		centre[2] += p.z / s.vertexCount;
	}
	int count = 0;
	for (const auto &t : s.triangles)
	{
		const auto a = s.frames[0].positions[t.a], b = s.frames[0].positions[t.b], c = s.frames[0].positions[t.c];
		const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
		const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
		count += nx * ((a.x + b.x + c.x) / 3 - centre[0]) + ny * ((a.y + b.y + c.y) / 3 - centre[1]) + nz * ((a.z + b.z + c.z) / 3 - centre[2]) > 0;
	}
	return count;
}

void outsideChecks(QApplication &app, ModelEditorDialog &editor)
{
	auto *tools = editor.tools();
	const auto &document = editor.document();
	QString error;
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("box");
	design.parts << part;
	auto cube = buildModelDesignMesh(design);
	const int faces = cube.surfaces[0].triangles.size();
	expect(faces > 0 && outwardFaces(cube) == faces, "the design box faces outward");
	for (int face = 0; face < faces; face += 3)
		std::swap(cube.surfaces[0].triangles[face].b, cube.surfaces[0].triangles[face].c);
	expect(editor.setMesh(cube, &error) && outwardFaces(document.mesh()) < faces, "open a cube with some faces inside out");
	auto *normals = editor.findChild<QMenu *>(QStringLiteral("meshMenuNormals"));
	expect(normals && normals->actions().contains(tools->action(QStringLiteral("meshRecalculateOutside"))) &&
			   normals->actions().contains(tools->action(QStringLiteral("meshRecalculateInside"))),
		   "the Normals menu offers Recalculate Outside and Inside");
	tools->applySelection({0, {}, {}});
	tools->action(QStringLiteral("meshRecalculateOutside"))->trigger();
	app.processEvents();
	expect(outwardFaces(document.mesh()) == faces && document.undoSteps() == 1, "Recalculate Outside turns every face outward in one step");
	editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();
	tools->applyControls(modelEditorControlsForProfile(QStringLiteral("blender")));
	expect(tools->action(QStringLiteral("meshRecalculateOutside"))->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_N) &&
			   tools->action(QStringLiteral("meshRecalculateInside"))->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N) &&
			   tools->action(QStringLiteral("meshRecalculateNormals"))->shortcut().isEmpty(),
		   "Blender's Shift+N recalculates outside and Ctrl+Shift+N inside");
	tools->applyControls(modelEditorControlsForProfile(QStringLiteral("studio")));
	expect(tools->action(QStringLiteral("meshRecalculateNormals"))->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_N),
		   "the studio profile keeps Shift+N for vertex normals");
}

void sharpChecks(QApplication &app, ModelEditorDialog &editor)
{
	auto *tools = editor.tools();
	const auto &document = editor.document();
	QString error;
	// A smooth, fully shared cube.
	const float h = 8;
	const QVector<ModelVec3> p{{-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h}, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}};
	auto cube = gridMesh(1, 8, true);
	auto &s = cube.surfaces[0];
	s.triangles = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4}, {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}};
	s.vertexCount = 8;
	s.texCoords.clear();
	for (const auto &q : p)
		s.texCoords.append({q.x / 64, q.y / 64});
	for (auto &frame : s.frames)
	{
		frame.positions = p;
		frame.normals.clear();
		for (const auto &q : p)
			frame.normals.append({q.x / (h * 1.7320508f), q.y / (h * 1.7320508f), q.z / (h * 1.7320508f)});
	}
	updateEditableModelMetadata(&cube);
	expect(editor.setMesh(cube, &error), "open a smooth cube");
	auto *normals = editor.findChild<QMenu *>(QStringLiteral("meshMenuNormals"));
	auto *edgeMenu = editor.findChild<QMenu *>(QStringLiteral("meshMenuEdge"));
	expect(normals && normals->actions().contains(tools->action(QStringLiteral("meshMarkSharp"))) && edgeMenu &&
			   edgeMenu->actions().contains(tools->action(QStringLiteral("meshClearSharp"))),
		   "Mark Sharp and Clear Sharp are in the Normals and Edge menus");
	// The top face's corners, in vertex mode: Mark Sharp uses the edges between them.
	tools->applySelection({0, {4, 5, 6, 7}, {}});
	tools->action(QStringLiteral("meshMarkSharp"))->trigger();
	app.processEvents();
	expect(document.mesh().surfaces[0].vertexCount == 12 && document.undoSteps() == 1 && document.mesh().surfaces[0].sharpEdges.size() == 4,
		   "Mark Sharp splits the top corners and stores the edges as one step");
	auto *status = editor.findChild<QLabel *>(QStringLiteral("meshStatus"));
	expect(status && status->text().contains(QStringLiteral("4")), "the banner counts the vertices the hard edges cost");
	editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();
	// A smoothing group of the top face does the same from a face selection.
	tools->applySelection({0, {}, {2, 3}});
	tools->action(QStringLiteral("meshSmoothingGroup"))->trigger();
	app.processEvents();
	expect(document.mesh().surfaces[0].vertexCount == 12 && document.mesh().surfaces[0].sharpEdges.size() == 4 && document.undoSteps() == 1,
		   "Make Smoothing Group hardens the region's border in one step");
	editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();
	expect(document.mesh().surfaces[0].vertexCount == 8, "one undo makes the cube smooth again");
	// An inset pushed through the bottom: a warning, then the crossing faces selected.
	ModelEdit inset;
	inset.kind = ModelEditKind::InsetFaces;
	inset.selection = {0, {}, {2, 3}};
	inset.tool.insetThickness = 2;
	inset.tool.insetDepth = -20;
	expect(tools->runTool(inset, &error), "an inset pushed through the cube is kept with a warning");
	tools->action(QStringLiteral("meshSelectIntroducedIntersections"))->trigger();
	app.processEvents();
	expect(!document.selection().faces.isEmpty(), "Select New Intersections selects the crossing faces");
	editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();
	tools->action(QStringLiteral("meshRefuseIntersections"))->trigger();
	inset.tool.topology.refuseIntersections = tools->toolEdit(ModelEditKind::InsetFaces).tool.topology.refuseIntersections;
	expect(inset.tool.topology.refuseIntersections && !tools->runTool(inset, &error) && document.undoSteps() == 0,
		   "with Refuse New Intersections on, the same inset is refused");
	tools->action(QStringLiteral("meshRefuseIntersections"))->trigger();
}

// Select Linked's limits in Adjust Last Operation: sharp edges and a normal angle.
void linkedChecks(QApplication &app, ModelEditorDialog &editor)
{
	auto *tools = editor.tools();
	const auto &document = editor.document();
	QString error;
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("box");
	design.parts << part;
	if (!expect(editor.setMesh(buildModelDesignMesh(design), &error), "open a box for Select Linked"))
		return;
	const int faces = document.mesh().surfaces[0].triangles.size();
	tools->action(QStringLiteral("meshModeFace"))->trigger();
	tools->applySelection({0, {}, {0}});
	tools->action(QStringLiteral("meshAdjustLast"))->trigger();
	tools->action(QStringLiteral("meshSelectLinked"))->trigger();
	app.processEvents();
	expect(int(document.selection().faces.size()) == faces, "Select Linked reaches the whole box");
	QCheckBox *sharp = nullptr;
	for (auto *box : tools->lastOperationPanel()->findChildren<QCheckBox *>())
		if (box->accessibleName() == QStringLiteral("Stop at sharp edges"))
			sharp = box;
	auto *angle = lastField(tools, QStringLiteral("Stop above angle (degrees, 0 = off)"));
	if (expect(sharp && angle, "Adjust Last Operation offers Stop at sharp edges and an angle limit"))
	{
		angle->setValue(45);
		app.processEvents();
		const int side = document.selection().faces.size();
		expect(side >= 2 && side < faces && document.undoSteps() == 0, "a 45-degree limit keeps the selection on the box's first side");
	}
	tools->action(QStringLiteral("meshAdjustLast"))->trigger();
	tools->applySelection({0, {}, {}});
}

// A flat 4x4 grid at z = 0 and, above it, a 2x2 grid at z = 10 offset by (3, 5).
ModelMesh layeredMesh()
{
	auto mesh = gridMesh(4, 8, true);
	auto upper = gridMesh(2, 8, true).surfaces[0];
	upper.name = QStringLiteral("upper");
	for (auto &frame : upper.frames)
		for (auto &p : frame.positions)
			p = {p.x + 3, p.y + 5, p.z + 10};
	mesh.surfaces << upper;
	updateEditableModelMetadata(&mesh);
	return mesh;
}

void snapChecks(QApplication &app, ModelEditorDialog &editor)
{
	auto *tools = editor.tools();
	const auto &document = editor.document();
	auto *viewport = editor.findChild<ModelViewport *>(QStringLiteral("meshPreview"));
	auto *element = editor.findChild<QComboBox *>(QStringLiteral("meshSnapElement"));
	auto *snap = editor.findChild<QCheckBox *>(QStringLiteral("meshSnapTranslation"));
	QString error;
	if (!expect(viewport && element && snap && editor.setMesh(layeredMesh(), &error), "open two layered grids for snapping"))
		return;
	tools->alignView(2, true);
	viewport->frameModel();
	expect(tests::settleModelViewport(*viewport), "the top view settles");
	const auto view = [&](ModelVec3 point)
	{
		QPointF screen;
		viewport->projectToView(point, &screen);
		return screen;
	};
	const auto upper = [&](int frame = 0) { return document.mesh().surfaces[1].frames[frame].positions; };
	QSet<int> everything;
	for (int v = 0; v < 9; ++v)
		everything << v;
	snap->setChecked(true);
	// Edge Midpoint: the upper grid's nearest corner lands on the midpoint of
	// the lower edge from (16, 16) to (24, 16).
	element->setCurrentIndex(2);
	tools->applySelection({1, everything, {}});
	expect(tools->beginModal(ModelModalOperator::Move, view({11, 13, 10})), "start a snapped move");
	tools->moveModal(view({20, 16, 0}));
	app.processEvents();
	expect(tools->modalText().contains(QStringLiteral("midpoint")), "the move reports an edge midpoint snap");
	expect(tools->finishModal(true, &error), "confirm the edge-midpoint move");
	bool landed = false;
	for (const auto &p : upper())
		landed |= near(p.x, 20) && near(p.y, 16);
	expect(landed && document.undoSteps() == 1, "an upper corner lands over the edge midpoint in one step");
	editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();
	// Face Project: every moving vertex lands on the lower grid behind it.
	element->setCurrentIndex(4);
	tools->applySelection({1, everything, {}});
	expect(tools->beginModal(ModelModalOperator::Move, view({11, 13, 10})), "start a projected move");
	tools->moveModal(view({11, 13, 10}) + QPointF(6, 0));
	app.processEvents();
	expect(tools->modalText().contains(QStringLiteral("projected")), "the move reports face projection");
	expect(tools->finishModal(true, &error), "confirm the projected move");
	bool flat = true;
	for (const auto &p : upper())
		flat &= near(p.z, 0, 1e-3);
	bool offsets = true;
	for (const auto &p : upper(1))
		offsets &= near(p.z, 8, 1e-3);
	expect(flat && offsets && document.undoSteps() == 1, "Face Project drops every upper vertex onto the lower grid and keeps the second pose's offset");
	expect(lastField(tools, QStringLiteral("Position X")) == nullptr, "Adjust Last Operation shows no single position for a projection");
	editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();
	// Extrude depth: the upper grid's first face extrudes down to the depth of
	// the lower vertex at (16, 16, 0), though the view looks along the normal.
	element->setCurrentIndex(1);
	tools->applySelection({1, {}, {0}});
	expect(tools->beginModal(ModelModalOperator::Extrude, view({7, 9, 10})), "start a snapped face extrusion");
	tools->moveModal(view({16, 16, 0}));
	app.processEvents();
	expect(tools->modalText().contains(QStringLiteral("snapped")), "the extrusion reports a snap");
	expect(tools->finishModal(true, &error), "confirm the snapped extrusion");
	int down = 0;
	for (const auto &p : upper())
		down += near(p.z, 0, 1e-3);
	expect(down == 3 && document.undoSteps() == 1, "the extruded face reaches z = 0");
	editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();
	// Pivot at Target Under Pointer, then a rotation that turns the nearest
	// corner, (19, 13, 10), toward the lower vertex at (32, 16, 0).
	expect(tools->pivotAtTarget(view({11, 13, 10})), "Pivot at Target Under Pointer finds the upper centre vertex");
	const auto pivot = tools->toolEdit(ModelEditKind::Transform).pivot;
	expect(near(pivot.x, 11) && near(pivot.y, 13) && near(pivot.z, 10), "the custom pivot moves to the vertex");
	auto *snapMenu = editor.findChild<QMenu *>(QStringLiteral("meshMenuSnap"));
	expect(snapMenu && snapMenu->actions().contains(tools->action(QStringLiteral("meshPivotTarget"))) &&
			   snapMenu->actions().contains(tools->action(QStringLiteral("meshPlaceVertex"))),
		   "the Snap menu lists Pivot at Target Under Pointer and Place Vertex");
	tools->applySelection({1, everything, {}});
	expect(tools->beginModal(ModelModalOperator::Rotate, view({19, 5, 10})), "start a snapped rotation");
	tools->moveModal(view({32, 16, 0}));
	app.processEvents();
	expect(tools->finishModal(true, &error), "confirm the snapped rotation");
	// Vertex 5 is the upper grid's right middle vertex, (19, 13, 10).
	const int corner = 5;
	const auto turned = upper()[corner];
	const double ux = turned.x - 11, uy = turned.y - 13;
	expect(near(ux * 3 - uy * 21, 0, 1e-2) && ux > 0 && near(std::hypot(ux, uy), 8, 1e-3) && document.undoSteps() == 1,
		   "the rotation turns the nearest corner onto the target's direction from the pivot");
	editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();
	// Scale along X: the nearest corner stretches out to the target's X.
	tools->applySelection({1, everything, {}});
	expect(tools->beginModal(ModelModalOperator::Scale, view({3, 5, 10})), "start a snapped scale");
	tools->constrainModal(0, false);
	tools->moveModal(view({32, 16, 0}));
	app.processEvents();
	expect(tools->finishModal(true, &error), "confirm the snapped scale");
	expect(near(upper()[corner].x, 32, 1e-3) && near(upper()[corner].y, 13, 1e-3) && document.undoSteps() == 1,
		   "Scale along X stretches the nearest corner to the target's X");
	editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();
	snap->setChecked(false);
	element->setCurrentIndex(0);
	// Merge at Target: the selected upper centre welds onto the vertex under the point.
	tools->applySelection({1, {4}, {}});
	expect(!tools->mergeAtTarget(QPointF(-100, -100)) && document.undoSteps() == 0, "Merge at Target with no vertex under the point is refused");
	expect(tools->mergeAtTarget(view({3, 5, 10})) && document.mesh().surfaces[1].vertexCount == 8 && document.undoSteps() == 1,
		   "Merge at Target welds the centre onto the corner under the point");
	bool kept = false;
	for (const auto &p : upper())
		kept |= near(p.x, 3) && near(p.y, 5) && near(p.z, 10);
	expect(kept, "the target keeps its position");
	editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();
	// Place Vertex: exact coordinates through the dialog, as one step.
	tools->applySelection({1, {4}, {}});
	QTimer::singleShot(0, &app,
					   [&]
					   {
						   for (auto *widget : QApplication::topLevelWidgets())
							   if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->objectName() == QStringLiteral("meshPlaceVertexDialog"))
							   {
								   if (auto *z = dialog->findChild<QDoubleSpinBox *>(QStringLiteral("meshPlaceVertexZ")))
									   z->setValue(-2.5);
								   dialog->accept();
							   }
					   });
	tools->action(QStringLiteral("meshPlaceVertex"))->trigger();
	app.processEvents();
	expect(near(upper()[4].z, -2.5) && near(upper(1)[4].z, 5.5) && near(upper()[4].x, 11) && document.undoSteps() == 1,
		   "Place Vertex moves the vertex in the shown pose and keeps the other pose's offset");
	auto *placeZ = lastField(tools, QStringLiteral("Position Z"));
	if (expect(placeZ, "Adjust Last Operation shows the placed position"))
	{
		placeZ->setValue(1);
		app.processEvents();
		expect(near(upper()[4].z, 1) && document.undoSteps() == 1, "adjusting the position replaces the placement in place");
	}
	editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();
	tools->applySelection({1, {3, 4}, {}});
	tools->action(QStringLiteral("meshPlaceVertex"))->trigger();
	app.processEvents();
	expect(document.undoSteps() == 0, "Place Vertex with two vertices selected is refused");
}
} // namespace

int main(int argc, char **argv)
{
#if defined(_MSC_VER) && defined(_DEBUG)
	for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
	{
		_CrtSetReportMode(type, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
	}
#endif
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("model-topology-tools-ui-smoke"); skip >= 0)
		return skip;
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("topology-tools-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	QString error;
	{
		ModelEditorDialog editor;
		editor.setAttribute(Qt::WA_DeleteOnClose, false);
		editor.resize(1500, 950);
		editor.show();
		app.processEvents();
		if (!expect(editor.tools() && editor.setMesh(gridMesh(), &error), "open the uneven grid"))
			return EXIT_FAILURE;
		selectChecks(app, editor);
		alignChecks(app, editor);
		transformModelChecks(app, editor);
		connectChecks(app, editor);
		dissolveChecks(app, editor);
		outsideChecks(app, editor);
		sharpChecks(app, editor);
		snapChecks(app, editor);
		linkedChecks(app, editor);
	}
	std::cout << checks << " topology tool UI checks; " << failures << " failures\n";
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
