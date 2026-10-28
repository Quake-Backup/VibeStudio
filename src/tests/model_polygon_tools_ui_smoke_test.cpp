// Mesh Editor coverage for the polygon modelling tools: Individual Origins
// and Normal axes (modal and gizmo).
// Each scenario runs in the dark theme at 100%, and in high-contrast themes at
// 200% text with expanded translations, the second right to left.
#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
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
		editor.close();
		app.processEvents();
		if (scenario)
			app.removeTranslator(&expansion);
	}
	std::cout << expect.checks << " polygon tool UI checks; " << expect.failures << " failures\n";
	return expect.failures ? 1 : 0;
}
