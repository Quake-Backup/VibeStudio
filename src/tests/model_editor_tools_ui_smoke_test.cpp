// Mesh Editor interaction layer: Blender-style menus, select modes, modal
// operators with typed values and axis constraints, region and loop picking,
// the 3D cursor, Add primitives, Adjust Last Operation, proportional and
// mirrored editing, view alignment and command search.
#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_viewport.h"
#include "core/model_design.h"
#include "core/studio_settings.h"
#include "tests/model_rig_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFont>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <tuple>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

using namespace slopstudio;
namespace
{
int failures = 0;
bool expect(bool condition, const char *message)
{
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
// A flat 4x4 grid of quads at z = 0, two poses (the second raised by 8).
ModelMesh gridMesh(int cells = 4, double size = 8, double origin = 0)
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
	{
		for (int i = 0; i <= cells; ++i)
			positions.append({float(origin + i * size), float(origin + j * size), 0});
	}
	const auto at = [&](int i, int j) { return j * (cells + 1) + i; };
	for (int j = 0; j < cells; ++j)
	{
		for (int i = 0; i < cells; ++i)
		{
			triangles.append({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
			triangles.append({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
		}
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
	updateEditableModelMetadata(&mesh);
	return mesh;
}
int vertexAt(const ModelSurface &surface, float x, float y, float z = 0)
{
	for (int v = 0; v < surface.vertexCount; ++v)
	{
		const auto p = surface.frames[0].positions[v];
		if (near(p.x, x) && near(p.y, y) && near(p.z, z))
			return v;
	}
	return -1;
}
QSet<int> allFaces(const ModelMesh &mesh)
{
	QSet<int> faces;
	for (int face = 0; face < mesh.surfaces[0].triangles.size(); ++face)
		faces.insert(face);
	return faces;
}
void key(QWidget *widget, int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier, const QString &text = {})
{
	QKeyEvent press(QEvent::KeyPress, code, modifiers, text);
	QCoreApplication::sendEvent(widget, &press);
	QKeyEvent release(QEvent::KeyRelease, code, modifiers, text);
	QCoreApplication::sendEvent(widget, &release);
}
// One pose of the first surface, positions and normals, as bytes.
QByteArray pose(const ModelMesh &mesh, int frame)
{
	const auto &geometry = mesh.surfaces[0].frames[frame];
	return QByteArray(reinterpret_cast<const char *>(geometry.positions.constData()), geometry.positions.size() * qsizetype(sizeof(ModelVec3))) +
		   QByteArray(reinterpret_cast<const char *>(geometry.normals.constData()), geometry.normals.size() * qsizetype(sizeof(ModelVec3)));
}
// A slanted 4x4 grid (z = x/4 + y/8), a flat 2x2 grid at z = 10 over its
// corner and, above both, a 2x2 grid at z = 20 offset by (3, 5).
ModelMesh stackedMesh()
{
	auto mesh = gridMesh(4, 8);
	for (auto &frame : mesh.surfaces[0].frames)
		for (auto &p : frame.positions)
			p.z += p.x / 4 + p.y / 8;
	for (const auto &[name, lift, offset] : {std::tuple{QStringLiteral("middle"), 10.0f, QPointF(0, 0)}, std::tuple{QStringLiteral("upper"), 20.0f, QPointF(3, 5)}})
	{
		auto surface = gridMesh(2, 8).surfaces[0];
		surface.name = name;
		for (auto &frame : surface.frames)
			for (auto &p : frame.positions)
				p = {float(p.x + offset.x()), float(p.y + offset.y()), p.z + lift};
		mesh.surfaces << surface;
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}

// What a change of the document does to the tools around it: a running modal
// tool ends when Undo, Redo or another edit changes the mesh under it; a move
// over every pose previews the shown pose as it commits; picking keeps the
// delivery-target menu, the joint tree and a typed material path; the shell's
// delivery context is not read on every pick; Face Project lands each vertex
// on the nearest still face behind it.
void documentChangeChecks(QApplication &app, ModelEditorDialog *editor, ModelEditorTools *tools, ModelViewport *viewport, const QString &folder)
{
	QString error;
	const auto &document = editor->document();
	auto *undo = editor->findChild<QAction *>(QStringLiteral("undoMesh"));
	auto *redo = editor->findChild<QAction *>(QStringLiteral("redoMesh"));
	auto *frame = editor->findChild<QComboBox *>(QStringLiteral("meshFrame"));
	if (!expect(undo && redo && frame && editor->setMesh(gridMesh(), &error), "reload a grid for the document-change checks"))
		return;
	tools->alignView(2, true);
	viewport->frameModel();
	expect(tests::settleModelViewport(*viewport), "viewport settles before the document-change checks");

	// Undo from the editing header takes no focus from the view: the move
	// ends, and nothing commits with indices from the undone mesh.
	tools->applySelection({0, {}, {0}});
	expect(tools->beginModal(ModelModalOperator::Move, QPointF(400, 300)), "start a move to undo later");
	tools->constrainModal(2, false);
	tools->typeModal(QStringLiteral("4"));
	expect(tools->finishModal(true, &error), "raise a face");
	const auto raised = document.revisionFingerprint();
	tools->applySelection({0, {}, {2, 3}});
	expect(tools->beginModal(ModelModalOperator::Move, QPointF(400, 300)), "start a move before Undo");
	tools->constrainModal(2, false);
	tools->typeModal(QStringLiteral("2"));
	expect(tools->modalPreviewValid(), "the move previews before Undo");
	undo->trigger();
	app.processEvents();
	const auto undone = document.revisionFingerprint();
	expect(undone != raised && tools->modalOperator() == ModelModalOperator::None, "Undo ends a running modal move");
	expect(!tools->finishModal(true, &error) && document.revisionFingerprint() == undone, "nothing commits after Undo ended the move");
	redo->trigger();
	app.processEvents();
	// An edit from a panel or button ends a running operator too.
	tools->applySelection({0, {}, {4, 5}});
	expect(tools->beginModal(ModelModalOperator::Scale, QPointF(420, 310)), "start a scale before another edit");
	tools->typeModal(QStringLiteral("2"));
	ModelEdit lift;
	lift.kind = ModelEditKind::Transform;
	lift.selection = {0, {}, {6}};
	lift.translation = {0, 0, 1};
	expect(editor->applyEdit(lift, &error) && tools->modalOperator() == ModelModalOperator::None, "an edit from elsewhere ends a running scale");
	undo->trigger();
	undo->trigger();
	app.processEvents();
	expect(!document.isModified(), "the grid is back as it was loaded");

	// A rotation and a proportional move over every pose: the preview of the
	// shown pose is exactly what the commit makes of it; the commit moves
	// every pose.
	auto *proportional = editor->findChild<QToolButton *>(QStringLiteral("meshProportional"));
	for (int shown : {0, 1})
	{
		for (bool weighted : {false, true})
		{
			if (proportional)
				proportional->setChecked(weighted);
			frame->setCurrentIndex(shown);
			app.processEvents();
			const auto before = document.mesh();
			tools->applySelection({0, {}, {0, 1, 2}});
			const bool started = weighted ? tools->beginModal(ModelModalOperator::Move, QPointF(410, 305))
										  : tools->beginModal(ModelModalOperator::Rotate, QPointF(410, 305));
			expect(started, "start a transform over every pose");
			if (weighted)
				tools->constrainModal(2, false);
			tools->typeModal(weighted ? QStringLiteral("5") : QStringLiteral("30"));
			// What the view shows while the tool runs, and the whole preview.
			const auto viewed = tools->modalPreviewValid() ? pose(viewport->mesh(), shown) : QByteArray();
			const auto *preview = tools->modalPreview();
			const auto whole = preview ? pose(*preview, 1 - shown) : QByteArray();
			expect(!viewed.isEmpty() && tools->finishModal(true, &error), "the transform previews and commits");
			expect(pose(document.mesh(), shown) == viewed, "the view's preview of the shown pose is the committed pose");
			expect(pose(document.mesh(), 1 - shown) == whole, "the preview asked for holds every pose as committed");
			expect(pose(document.mesh(), 1 - shown) != pose(before, 1 - shown), "the commit transforms the other pose too");
			undo->trigger();
			app.processEvents();
		}
	}
	if (proportional)
		proportional->setChecked(false);
	frame->setCurrentIndex(0);
	app.processEvents();

	// Picking keeps what depends on the mesh alone: the delivery-target menu
	// and the typed material path. An edit refreshes both.
	auto *budget = editor->findChild<QToolButton *>(QStringLiteral("meshExportBudget"));
	auto *material = editor->findChild<QLineEdit *>(QStringLiteral("meshMaterial"));
	if (expect(budget && budget->menu() && !budget->menu()->actions().isEmpty() && material, "the target menu and material path exist"))
	{
		const QPointer<QAction> entry = budget->menu()->actions().first();
		const auto text = budget->text();
		material->setText(QStringLiteral("textures/typed/skin"));
		tools->applySelection({0, {}, {9}});
		tools->applySelection({0, {3}, {}});
		expect(entry && budget->text() == text, "picking keeps the delivery-target menu");
		expect(material->text() == QStringLiteral("textures/typed/skin"), "picking keeps a typed material path");
		expect(editor->applyEdit(lift, &error), "edit the grid");
		expect(!entry && material->text() != QStringLiteral("textures/typed/skin"), "an edit rebuilds the menu and shows the slot's path again");
		undo->trigger();
		app.processEvents();
	}

	// The shell's delivery context (which reads the project manifest) is
	// read when it is pushed and before exports, not on picks or frames.
	int reads = 0;
	editor->deliveryContext = [&reads]
	{
		++reads;
		return ModelEditorDeliveryContext{};
	};
	editor->refreshContext();
	expect(reads == 1, "a pushed context is read");
	reads = 0;
	tools->applySelection({0, {}, {1}});
	tools->applySelection({0, {}, {1, 2}});
	frame->setCurrentIndex(1);
	frame->setCurrentIndex(0);
	app.processEvents();
	expect(reads == 0, "picks and frame steps do not read the delivery context");
	expect(tools->exportDetailLevels(QDir(folder).filePath(QStringLiteral("context.md3")), 1, 0.5, &error) && reads > 0,
		   "an export reads the delivery context first");
	editor->deliveryContext = {};

	// The joint tree is rebuilt for a new mesh, not for a pick: its items,
	// scroll position and folded joints stay, and the current joint follows.
	auto *joints = editor->findChild<QTreeWidget *>(QStringLiteral("meshSkeletonJoints"));
	if (expect(joints && editor->setMesh(tests::rigFixture(), &error) && joints->topLevelItemCount() == 1, "open a rig"))
	{
		const QPointer<QObject> guard(joints);
		auto *root = joints->topLevelItem(0);
		root->setExpanded(false);
		tools->applySelection({0, {1}, {}});
		expect(joints->topLevelItem(0) == root && !root->isExpanded(), "a component pick keeps the joint tree and its folded joints");
		// Picking a joint in the tree selects it and keeps the tree.
		joints->setCurrentItem(root->child(0));
		app.processEvents();
		expect(document.selection().joint == QStringLiteral("tip") && joints->topLevelItem(0) == root && joints->currentItem() &&
				   joints->currentItem()->text(0) == QStringLiteral("tip"),
			   "a joint pick keeps the joint tree and moves its current joint");
		tools->applySelection({0, {0}, {}});
		expect(joints->topLevelItem(0) == root && !joints->currentItem(), "a component pick leaves no current joint");
	}

	// Face Project: each upper vertex lands on the nearest still face behind
	// it, the flat middle grid over its corner, else the slanted grid.
	auto *element = editor->findChild<QComboBox *>(QStringLiteral("meshSnapElement"));
	auto *snap = editor->findChild<QCheckBox *>(QStringLiteral("meshSnapTranslation"));
	if (expect(element && snap && editor->setMesh(stackedMesh(), &error), "open stacked grids for face projection"))
	{
		tools->alignView(2, true);
		viewport->frameModel();
		expect(tests::settleModelViewport(*viewport), "the stacked grids settle");
		QSet<int> upper;
		for (int v = 0; v < 9; ++v)
			upper << v;
		element->setCurrentIndex(4);
		snap->setChecked(true);
		tools->applySelection({2, upper, {}});
		QPointF start;
		viewport->projectToView({11, 13, 20}, &start);
		expect(tools->beginModal(ModelModalOperator::Move, start) && tools->modalPreviewValid(), "start a projected move");
		if (!expect(tools->finishModal(true, &error), "confirm the projected move"))
			std::cerr << qPrintable(error) << '\n';
		bool landed = true;
		for (const auto &p : document.mesh().surfaces[2].frames[0].positions)
		{
			const bool overMiddle = p.x < 16 && p.y < 16;
			landed &= near(p.z, overMiddle ? 10 : p.x / 4 + p.y / 8, 1e-3);
		}
		expect(landed, "every projected vertex lands on the nearest still face");
		element->setCurrentIndex(0);
		snap->setChecked(false);
		undo->trigger();
		app.processEvents();
	}
	expect(editor->setMesh(gridMesh(), &error), "reload the grid after the document-change checks");
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
	// Draws in 3D: skip where no OpenGL or Vulkan renderer starts.
	if (const int skip = slopstudio::test_support::exitCodeWithoutRenderer("model-editor-tools-ui-smoke"); skip >= 0) {
		return skip;
	}
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("SLOPSTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("mesh-tools-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	QString error;
	auto *editor = new ModelEditorDialog;
	editor->resize(1500, 950);
	editor->show();
	app.processEvents();
	auto *tools = editor->tools();
	auto *viewport = editor->findChild<ModelViewport *>(QStringLiteral("meshPreview"));
	if (!expect(tools && viewport, "the editor creates its interaction layer and keeps the viewport"))
		return EXIT_FAILURE;
	expect(editor->setMesh(gridMesh(), &error), "load a grid");
	const auto &document = editor->document();

	// Menus, shelf and shortcuts.
	auto *menus = editor->findChild<QMenuBar *>(QStringLiteral("meshMenuBar"));
	expect(menus && menus->actions().size() >= 7, "View, Select, Add, Mesh, Vertex, Edge and Face menus exist");
	expect(editor->findChild<QToolBar *>(QStringLiteral("meshToolShelf")) && !editor->findChild<QToolBar *>(QStringLiteral("meshToolShelf"))->actions().isEmpty(),
		   "the tool shelf lists tools");
	expect(tools->action(QStringLiteral("meshMove"))->shortcut() == QKeySequence(Qt::Key_G) &&
			   tools->action(QStringLiteral("meshLoopCut"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_R) &&
			   tools->action(QStringLiteral("meshOperatorSearch"))->shortcut() == QKeySequence(Qt::Key_F3),
		   "Blender's G, Ctrl+R and F3 keys are bound");
	expect(tools->blenderNavigation() && viewport->cameraControls().orbitButton == Qt::MiddleButton,
		   "Blender navigation orbits with the middle button");

	// Top view, framed, for deterministic projections.
	tools->alignView(2, true);
	viewport->frameModel();
	expect(tests::settleModelViewport(*viewport), "viewport settles");

	// Select all, select modes and conversion.
	tools->action(QStringLiteral("meshSelectAll"))->trigger();
	expect(document.selection().faces.size() == 32, "A selects every face");
	editor->findChild<QToolButton *>(QStringLiteral("meshSelectMode0"))->click();
	expect(document.selection().vertices.size() == 25 && document.selection().faces.isEmpty(), "vertex mode converts faces to their corners");
	tools->action(QStringLiteral("meshModeFace"))->trigger();
	expect(document.selection().faces.size() == 32, "face mode converts fully selected corners back to faces");
	tools->action(QStringLiteral("meshSelectNone"))->trigger();
	expect(document.selection().faces.isEmpty(), "Alt+A clears the selection");

	// Modal move with a typed value along Z, driven by keys.
	auto selectFaces = [&](const QSet<int> &faces) { tools->applySelection({0, {}, faces}); };
	selectFaces({0});
	expect(tools->beginModal(ModelModalOperator::Move, QPointF(400, 300)), "G starts a modal move");
	key(viewport, Qt::Key_Z, Qt::NoModifier, QStringLiteral("z"));
	key(viewport, Qt::Key_8, Qt::NoModifier, QStringLiteral("8"));
	expect(tools->modalPreviewValid() && tools->modalText().contains(QStringLiteral("8")), "typed values preview along the constraint");
	key(viewport, Qt::Key_Return);
	expect(tools->modalOperator() == ModelModalOperator::None, "Enter confirms");
	const auto &surface = document.mesh().surfaces[0];
	const auto raised = surface.triangles[0];
	expect(near(surface.frames[0].positions[raised.a].z, 8) && near(surface.frames[1].positions[raised.a].z, 16),
		   "the move applies to every pose");
	const auto moved = document.mesh();
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	expect(document.mesh().surfaces[0].frames[0].positions[raised.a].z == 0, "the move is one undo step");

	// Escape cancels without touching the document.
	const auto before = document.revisionFingerprint();
	expect(tools->beginModal(ModelModalOperator::Rotate, QPointF(400, 300)), "R starts a modal rotation");
	tools->typeModal(QStringLiteral("30"));
	if (!expect(tools->modalPreviewValid(), "rotation previews"))
		std::cerr << tools->modalText().toStdString() << '\n';
	key(viewport, Qt::Key_Escape);
	expect(tools->modalOperator() == ModelModalOperator::None && document.revisionFingerprint() == before, "Escape cancels the preview");

	// Extrude a face along its normal by a typed distance.
	selectFaces({0, 1});
	const int triangles = document.mesh().surfaces[0].triangles.size();
	expect(tools->beginModal(ModelModalOperator::Extrude, QPointF(400, 300)), "E starts an extrusion");
	tools->typeModal(QStringLiteral("4"));
	expect(tools->finishModal(true, &error), "confirm the extrusion");
	expect(document.mesh().surfaces[0].triangles.size() == triangles + 8, "extruding a quad adds its four side walls");
	double top = -1;
	for (const auto &p : document.mesh().surfaces[0].frames[0].positions)
		top = std::max(top, double(p.z));
	expect(near(top, 4), "extrusion follows the face normal");
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();

	// Inset with a typed thickness, then adjust it from the last-operation panel.
	selectFaces(allFaces(document.mesh()));
	expect(tools->beginModal(ModelModalOperator::Inset, QPointF(400, 300)), "I starts an inset");
	tools->typeModal(QStringLiteral("2"));
	expect(tools->finishModal(true, &error), "confirm the inset");
	expect(vertexAt(document.mesh().surfaces[0], 2, 2) >= 0, "inner corners sit two units in");
	auto *panel = tools->lastOperationPanel();
	auto spins = panel->findChildren<QDoubleSpinBox *>();
	expect(!spins.isEmpty(), "Adjust Last Operation shows the inset settings");
	if (!spins.isEmpty())
	{
		spins.first()->setValue(4);
		app.processEvents();
		expect(vertexAt(document.mesh().surfaces[0], 4, 4) >= 0 && vertexAt(document.mesh().surfaces[0], 2, 2) < 0,
			   "changing the thickness redoes the inset in place");
	}
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	expect(document.mesh().surfaces[0].vertexCount == 25, "one undo removes the adjusted inset");

	// Loop cut by pointing at an edge.
	viewport->frameModel();
	expect(tests::settleModelViewport(*viewport), "viewport settles before the loop cut");
	QPointF a, b;
	const auto &grid = document.mesh().surfaces[0];
	expect(viewport->projectToView(grid.frames[0].positions[vertexAt(grid, 8, 0)], &a) &&
			   viewport->projectToView(grid.frames[0].positions[vertexAt(grid, 8, 8)], &b),
		   "project an edge");
	const QPointF middle = (a + b) / 2;
	expect(tools->beginModal(ModelModalOperator::LoopCut, middle), "Ctrl+R starts a loop cut");
	tools->moveModal(middle);
	app.processEvents();
	expect(tools->modalPreviewValid(), "the ring under the pointer previews");
	tools->adjustModal(1);
	expect(tools->finishModal(true, &error), "confirm two cuts");
	expect(document.mesh().surfaces[0].triangles.size() == 32 + 16, "two cuts across a four-quad ring add sixteen triangles");
	expect(document.selection().edges.size() == 8, "the new loops are selected");
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();

	// A selected edge seeds Loop Cut; a pointer over another edge takes over,
	// and away from every edge the selected one comes back.
	{
		const auto &flat = document.mesh().surfaces[0];
		const auto selected = modelEdge(vertexAt(flat, 8, 0), vertexAt(flat, 8, 8));
		const auto other = modelEdge(vertexAt(flat, 0, 8), vertexAt(flat, 8, 8));
		QPointF c, d;
		expect(viewport->projectToView(flat.frames[0].positions[other.first], &c) && viewport->projectToView(flat.frames[0].positions[other.second], &d),
			   "project another edge");
		tools->applySelection({0, {}, {}, {selected}});
		expect(tests::settleModelViewport(*viewport), "viewport settles before the seeded loop cut");
		QPointF empty(-1, -1);
		for (const QPointF corner : {QPointF(2, 2), QPointF(viewport->width() - 3, 2), QPointF(2, viewport->height() - 3),
									 QPointF(viewport->width() - 3, viewport->height() - 3)})
		{
			if (empty.x() < 0 && !viewport->hitAt(corner).valid)
				empty = corner;
		}
		const QString seeded = QStringLiteral("across the selected edge %1:%2").arg(selected.first).arg(selected.second);
		expect(tools->beginModal(ModelModalOperator::LoopCut, empty, false) && tools->modalText().contains(seeded),
			   "Loop Cut starts across the selected edge");
		tools->moveModal((c + d) / 2);
		expect(tools->modalText().contains(QStringLiteral("across edge %1:%2").arg(other.first).arg(other.second)), "the pointer over another edge takes over");
		if (expect(empty.x() >= 0, "a corner of the view is clear of the model"))
		{
			tools->moveModal(empty);
			expect(tools->modalText().contains(seeded), "away from every edge the selected edge seeds the cut again");
		}
		tools->finishModal(false);
		expect(tools->modalOperator() == ModelModalOperator::None && document.mesh().surfaces[0].triangles.size() == 32, "Esc leaves the grid uncut");
	}

	// Box select every visible face.
	expect(tests::settleModelViewport(*viewport), "viewport settles before box select");
	tools->action(QStringLiteral("meshModeFace"))->trigger();
	expect(tools->selectRegion(QPolygonF(QRectF(viewport->rect())), false, false) && document.selection().faces.size() == 32,
		   "a box around the view selects every visible face");
	expect(tools->selectRegion(QPolygonF(QRectF(QPointF(0, 0), QPointF(viewport->width() / 2.0, viewport->height()))), false, true) &&
			   !document.selection().faces.isEmpty() && document.selection().faces.size() < 32,
		   "Ctrl+drag subtracts a region");

	// Loop picking under the pointer.
	tools->action(QStringLiteral("meshModeEdge"))->trigger();
	expect(tests::settleModelViewport(*viewport), "viewport settles before loop picking");
	expect(tools->pickLoop(middle, false, false) && document.selection().edges.size() == 4, "Alt+click selects an edge loop");
	expect(tests::settleModelViewport(*viewport), "viewport settles before ring picking");
	expect(tools->pickLoop(middle, true, false) && document.selection().edges.size() == 5, "Ctrl+Alt+click selects an edge ring");

	// The 3D cursor and Add.
	tools->action(QStringLiteral("meshModeFace"))->trigger();
	QPointF centre;
	expect(viewport->projectToView({16, 16, 0}, &centre) && tools->placeCursor(centre), "Shift+right-click places the 3D cursor");
	expect(near(tools->cursor3d().x, 16, 0.5) && near(tools->cursor3d().y, 16, 0.5) && near(tools->cursor3d().z, 0, 1e-3),
		   "the cursor lands on the surface under the pointer");
	tools->setCursor3d({100, 0, 0});
	tools->action(QStringLiteral("meshAddCube"))->trigger();
	const auto &withCube = document.mesh().surfaces[0];
	expect(withCube.triangles.size() == 32 + 12 && document.selection().faces.size() == 12, "Add Cube adds a selected cube at the cursor");
	spins = tools->lastOperationPanel()->findChildren<QDoubleSpinBox *>();
	if (expect(spins.size() >= 6, "Adjust Last Operation exposes the primitive's location and size"))
	{
		spins[3]->setValue(64);
		app.processEvents();
		double maxX = -1e9;
		for (const auto &p : document.mesh().surfaces[0].frames[0].positions)
			maxX = std::max(maxX, double(p.x));
		expect(near(maxX, 132), "resizing the last cube rebuilds it");
	}
	// Select > Facing Axis, as model select --operation facing (45 degrees):
	// only the cube's two +X triangles face +X; the flat grid faces Z.
	tools->action(QStringLiteral("meshSelectFacingPositiveX"))->trigger();
	expect(document.selection().faces.size() == 2 && tools->action(QStringLiteral("meshSelectFacingNegativeZ")) != nullptr,
		   "Facing +X selects the faces whose normals lie within 45 degrees of +X");
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();

	// Proportional editing.
	editor->findChild<QToolButton *>(QStringLiteral("meshProportional"))->setChecked(true);
	editor->findChild<QComboBox *>(QStringLiteral("meshProportionalFalloff"))->setCurrentIndex(int(ModelFalloff::Linear));
	editor->findChild<QDoubleSpinBox *>(QStringLiteral("meshProportionalRadius"))->setValue(16);
	tools->action(QStringLiteral("meshModeVertex"))->trigger();
	const auto &g = document.mesh().surfaces[0];
	const int centreVertex = vertexAt(g, 16, 16), neighbour = vertexAt(g, 24, 16);
	tools->applySelection({0, {centreVertex}, {}});
	expect(tools->beginModal(ModelModalOperator::Move, QPointF(400, 300)), "proportional move starts");
	tools->constrainModal(2, false);
	tools->typeModal(QStringLiteral("10"));
	expect(tools->finishModal(true, &error), "confirm the proportional move");
	expect(near(document.mesh().surfaces[0].frames[0].positions[centreVertex].z, 10) &&
			   near(document.mesh().surfaces[0].frames[0].positions[neighbour].z, 5),
		   "neighbours follow the falloff");
	editor->findChild<QToolButton *>(QStringLiteral("meshProportional"))->setChecked(false);
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();

	// Mirror editing on a grid centred on X = 0.
	expect(editor->setMesh(gridMesh(4, 8, -16), &error), "load a centred grid");
	const auto &m = document.mesh().surfaces[0];
	const int right = vertexAt(m, 8, 0), left = vertexAt(m, -8, 0);
	editor->findChild<QToolButton *>(QStringLiteral("meshMirrorX"))->setChecked(true);
	tools->applySelection({0, {right}, {}});
	expect(tools->beginModal(ModelModalOperator::Move, QPointF(400, 300)), "mirrored move starts");
	tools->constrainModal(0, false);
	tools->typeModal(QStringLiteral("2"));
	expect(tools->finishModal(true, &error), "confirm the mirrored move");
	expect(near(document.mesh().surfaces[0].frames[0].positions[right].x, 10) && near(document.mesh().surfaces[0].frames[0].positions[left].x, -10),
		   "the counterpart moves symmetrically");
	editor->findChild<QToolButton *>(QStringLiteral("meshMirrorX"))->setChecked(false);
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	expect(!document.isModified(), "undo returns to the loaded source");

	// Views, framing and the navigation gizmo.
	tools->alignView(1, false);
	expect(editor->findChild<QComboBox *>(QStringLiteral("meshViewPreset"))->currentIndex() == 2, "Numpad 1 looks from the front");
	tools->alignView(2, true);
	expect(editor->findChild<QComboBox *>(QStringLiteral("meshViewPreset"))->currentIndex() == 1, "Numpad 7 looks from the top");
	tools->frameSelection();

	// Export budget, modal vertex bevel and Quake III detail levels.
	{
		auto *budget = editor->findChild<QToolButton *>(QStringLiteral("meshExportBudget"));
		expect(budget && budget->text().contains(QStringLiteral("MD3")), "the header reports the export budget");
		expect(editor->setMesh(gridMesh(), &error), "reload a grid for bevel");
		const auto &b = document.mesh().surfaces[0];
		tools->applySelection({0, {vertexAt(b, 16, 16)}, {}});
		expect(tools->beginModal(ModelModalOperator::BevelVertices, QPointF(400, 300)), "Ctrl+Shift+B starts a vertex bevel");
		tools->typeModal(QStringLiteral("2"));
		expect(tools->finishModal(true, &error), "confirm the bevel");
		expect(vertexAt(document.mesh().surfaces[0], 16, 16) < 0 && vertexAt(document.mesh().surfaces[0], 18, 16) >= 0,
			   "the bevelled vertex becomes a ring of points");
		editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		const auto lodBase = QDir(temporary.path()).filePath(QStringLiteral("prop.md3"));
		expect(tools->exportDetailLevels(lodBase, 2, 0.5, &error) && QFile::exists(QDir(temporary.path()).filePath(QStringLiteral("prop_1.md3"))) &&
				   QFile::exists(QDir(temporary.path()).filePath(QStringLiteral("prop_2.md3"))),
			   "the editor exports the base model and its detail levels");
		tools->applySelection({0, {}, allFaces(document.mesh())});
		tools->action(QStringLiteral("meshSolidify"))->trigger();
		expect(document.mesh().surfaces[0].triangles.size() == 32 + 32 + 32, "Solidify gives the grid a shell");
		editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		expect(!document.isModified(), "the grid returns to its loaded state");
	}

	// Vertex snapping: a moved cube's nearest corner lands on a grid vertex.
	{
		expect(editor->setMesh(gridMesh(), &error), "reload a grid for snapping");
		tools->alignView(2, true);
		viewport->frameModel();
		tools->setCursor3d({40, 40, 0});
		tools->action(QStringLiteral("meshAddCube"))->trigger();
		viewport->frameModel();
		expect(tests::settleModelViewport(*viewport), "viewport settles before snapping");
		QPointF start, target;
		const auto &s = document.mesh().surfaces[0];
		expect(viewport->projectToView({40, 40, 0}, &start) && viewport->projectToView(s.frames[0].positions[vertexAt(s, 16, 16)], &target),
			   "project the cube and a grid vertex");
		tools->setSnapToVertices(true);
		editor->findChild<QCheckBox *>(QStringLiteral("meshSnapTranslation"))->setChecked(true);
		expect(tools->beginModal(ModelModalOperator::Move, start), "move the new cube");
		tools->moveModal(target);
		app.processEvents();
		expect(tools->modalText().contains(QStringLiteral("snapped")), "the move reports a vertex snap");
		expect(tools->finishModal(true, &error), "confirm the snapped move");
		int onTarget = 0;
		for (const auto &p : document.mesh().surfaces[0].frames[0].positions)
			onTarget += near(p.x, 16) && near(p.y, 16) && near(p.z, 0);
		expect(onTarget >= 2, "a cube corner lands exactly on the grid vertex");
		editor->findChild<QCheckBox *>(QStringLiteral("meshSnapTranslation"))->setChecked(false);
		tools->setSnapToVertices(false);
		editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		expect(!document.isModified(), "snapping test returns to the loaded grid");
	}

	// Command search finds tools by name.
	tools->showOperatorSearch();
	app.processEvents();
	auto *field = editor->findChild<QLineEdit *>(QStringLiteral("meshOperatorSearchField"));
	auto *results = editor->findChild<QListWidget *>(QStringLiteral("meshOperatorSearchResults"));
	if (expect(field && results, "F3 opens command search"))
	{
		field->setText(QStringLiteral("inset"));
		bool found = false;
		for (int row = 0; row < results->count(); ++row)
			found |= results->item(row)->text().contains(QStringLiteral("Inset"));
		expect(found, "searching 'inset' lists Inset Faces");
		field->window()->close();
	}

	// Selection operators through the menus.
	tools->action(QStringLiteral("meshModeFace"))->trigger();
	tools->applySelection({0, {}, {0}});
	tools->action(QStringLiteral("meshSelectLinked"))->trigger();
	expect(document.selection().faces.size() == 32, "Ctrl+L selects linked faces");
	tools->applySelection({0, {}, {0}});
	tools->action(QStringLiteral("meshSelectMore"))->trigger();
	expect(document.selection().faces.size() > 1, "Ctrl+= grows the selection");

	documentChangeChecks(app, editor, tools, viewport, temporary.path());

	// The editor deletes itself on close.
	editor->close();
	app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
	if (failures == 0)
		std::cout << "mesh editor tools UI checks passed\n";
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
