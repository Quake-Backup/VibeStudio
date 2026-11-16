// UV > Paint Skin… (UVSKIN-09, app/model_skin_paint_dialog.h): brush settings
// are named and keyboard reachable, a stroke paints the skin and the preview,
// Escape cancels a stroke in progress, Undo and Redo step through strokes, the
// recorded strokes replay through the core service to the same skin, and the
// Mesh Editor's UV menu opens it; 100% and 200% text, LTR and RTL. In the
// editor, an MDL model's painted skin applies as one undo step, and Paint in
// View paints by dragging in the editor's own viewport, orthographic and
// perspective, with Escape cancelling there too.
#include "app/model_editor_dialog.h"
#include "app/model_skin_paint_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_q3_animation_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFont>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace slopstudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << "FAIL: " << message << '\n';
	return value;
}
// A long soft stroke painted as the pointer moves replays to the same skin,
// and closing with strokes neither applied nor saved asks first.
bool longStrokeAndDiscard(const ModelMesh &mesh, const QImage &skin, const QString &savePath)
{
	bool ok = true;
	ModelSkinPaintDialog dialog(mesh, 0, 0, skin);
	dialog.show();
	auto *pitch = dialog.findChild<QDoubleSpinBox *>("modelSkinPaintPitch");
	auto *radius = dialog.findChild<QSpinBox *>("modelSkinPaintRadius");
	auto *opacity = dialog.findChild<QDoubleSpinBox *>("modelSkinPaintOpacity");
	auto *hardness = dialog.findChild<QDoubleSpinBox *>("modelSkinPaintHardness");
	auto *undo = dialog.findChild<QPushButton *>("modelSkinPaintUndo");
	if (!expect(pitch && radius && opacity && hardness && undo, "the long-stroke dialog has its controls"))
		return false;
	pitch->setValue(89);
	radius->setValue(7);
	opacity->setValue(0.4);
	hardness->setValue(0.2);
	double sumX = 0, sumY = 0;
	int count = 0;
	for (int y = 0; y < dialog.view().size.height(); ++y)
		for (int x = 0; x < dialog.view().size.width(); ++x)
			if (dialog.view().ids.at(qsizetype(y) * dialog.view().size.width() + x) >= 0)
			{
				sumX += x + 0.5;
				sumY += y + 0.5;
				++count;
			}
	if (!expect(count > 0, "the model shows in the long-stroke view"))
		return false;
	const QPointF centre(sumX / count, sumY / count);
	const QImage before = dialog.skin();
	ok &= expect(!dialog.hasUnsavedStrokes(), "a freshly opened skin has nothing to lose");
	ok &= dialog.beginStroke(centre);
	for (int i = 1; i <= 30; ++i)
		ok &= dialog.extendStroke(centre + QPointF(30 * std::cos(i / 5.0), 20 * std::sin(i / 3.0)));
	ok &= expect(dialog.finishStroke(true) && dialog.skin() != before && dialog.hasUnsavedStrokes(), "a long soft stroke paints");
	ModelSkinPaintCamera camera;
	QVector<ModelSkinPaintStroke> strokes;
	QString error;
	ModelSkinPaintView view;
	QImage replay = before;
	ok &= expect(parseModelSkinPaintStrokes(dialog.strokesJson(), &camera, &strokes, &error) && strokes.size() == 1 &&
					 buildModelSkinPaintView(mesh, 0, camera, &view, &error) && paintModelSkinStroke(mesh, 0, 0, view, strokes.first(), &replay, nullptr, &error) &&
					 replay == dialog.skin(),
				 "the stroke painted move by move replays to the same skin");
	ok &= expect(dialog.preview() == renderModelSkinPaintPreview(mesh, 0, dialog.view(), {{0, dialog.skin()}}, dialog.palette().color(QPalette::Base)),
				 "the preview shows the painted skin as the model draws it");
	int asked = 0;
	bool answer = false;
	dialog.discardPrompt = [&] {
		++asked;
		return answer;
	};
	dialog.reject();
	ok &= expect(asked == 1 && dialog.isVisible() && dialog.skin() != before, "Close with unsaved strokes asks, and keeping them keeps the dialog open");
	QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
	QCoreApplication::sendEvent(&dialog, &escape);
	ok &= expect(asked == 2 && dialog.isVisible(), "Escape asks the same question");
	ok &= expect(dialog.saveTo(savePath) && !dialog.hasUnsavedStrokes(), "saving the skin keeps the strokes");
	dialog.reject();
	ok &= expect(asked == 2 && !dialog.isVisible(), "a saved skin closes without asking");
	dialog.show();
	ok &= expect(dialog.beginStroke(centre) && dialog.finishStroke(true) && dialog.hasUnsavedStrokes(), "a further stroke is unsaved again");
	undo->click();
	ok &= expect(!dialog.hasUnsavedStrokes(), "undoing back to the saved skin leaves nothing to lose");
	ok &= expect(dialog.beginStroke(centre) && dialog.finishStroke(true), "paint once more");
	answer = true;
	dialog.reject();
	ok &= expect(asked == 3 && !dialog.isVisible(), "discarding closes the dialog");
	return ok;
}
// Paint Skin stays open while the model is edited: Apply refuses when the
// skin was changed by something else since it opened (it would discard
// that change) or the surface's UVs moved, and applies once they are back.
bool staleApplyRefused(const ModelMesh &mesh)
{
	bool ok = true;
	ModelMesh quake = mesh;
	ModelEdit add;
	add.kind = ModelEditKind::AddMdlSkin;
	add.mdlSkin.size = {64, 64};
	add.mdlSkin.pixels = QByteArray(64 * 64, char(0));
	for (int i = 0; i < 256; ++i)
		add.mdlSkin.palette.append(QByteArray(3, char(i)));
	QString error;
	ok &= expect(applyModelEdit(&quake, add, nullptr, &error), "give the stale-apply model an MDL skin");
	ModelEditorDialog editor;
	editor.resize(1400, 900);
	editor.show();
	ok &= expect(editor.setMesh(quake, &error), "load the stale-apply model");
	auto *open = editor.findChild<QAction *>("meshPaintSkin");
	auto *undoMesh = editor.findChild<QAction *>("undoMesh");
	if (!expect(open && undoMesh, "the editor has Paint Skin and Undo"))
		return false;
	open->trigger();
	auto *dialog = static_cast<ModelSkinPaintDialog *>(editor.findChild<QDialog *>("modelSkinPaintDialog"));
	auto *apply = dialog ? dialog->findChild<QPushButton *>("modelSkinPaintApply") : nullptr;
	auto *status = dialog ? dialog->findChild<QWidget *>("modelSkinPaintStatus") : nullptr;
	if (!expect(dialog && apply && status, "Paint Skin opens with Apply to Model"))
		return false;
	dialog->findChild<QDoubleSpinBox *>("modelSkinPaintPitch")->setValue(89);
	dialog->findChild<QSpinBox *>("modelSkinPaintRadius")->setValue(30);
	double sumX = 0, sumY = 0;
	int count = 0;
	for (int y = 0; y < dialog->view().size.height(); ++y)
		for (int x = 0; x < dialog->view().size.width(); ++x)
			if (dialog->view().ids.at(qsizetype(y) * dialog->view().size.width() + x) >= 0)
			{
				sumX += x + 0.5;
				sumY += y + 0.5;
				++count;
			}
	if (!expect(count > 0, "the stale-apply model shows in the paint view"))
		return false;
	const QPointF centre(sumX / count, sumY / count);
	ok &= expect(dialog->beginStroke(centre) && dialog->finishStroke(true), "paint a stroke");
	// Something else changes the skin: a fill of index 7.
	QImage other = modelSurfaceIndexedSkin(editor.document().mesh(), 0);
	other.fill(7);
	ModelEdit replace;
	ok &= expect(modelSkinPaintMdlEdit(editor.document().mesh(), 0, other, &replace, &error) && editor.applyEdit(replace, &error),
				 "another change fills the skin");
	QByteArray revision = editor.document().revisionFingerprint();
	apply->click();
	ok &= expect(editor.document().revisionFingerprint() == revision && status->accessibleDescription().contains(QStringLiteral("changed")),
				 "Apply refuses to discard a skin change made since Paint Skin opened");
	undoMesh->trigger();
	// The UVs move: the painting no longer lines up.
	ModelEdit uv;
	uv.kind = ModelEditKind::TransformUv;
	uv.selection = {0, {}, {0}};
	uv.uvOffset = {0.25f, 0};
	ok &= expect(editor.applyEdit(uv, &error), "move the surface's UVs");
	revision = editor.document().revisionFingerprint();
	apply->click();
	ok &= expect(editor.document().revisionFingerprint() == revision, "Apply refuses a painting for UVs that have moved");
	undoMesh->trigger();
	revision = editor.document().revisionFingerprint();
	apply->click();
	ok &= expect(editor.document().revisionFingerprint() != revision && modelSurfaceIndexedSkin(editor.document().mesh(), 0) == dialog->skin(),
				 "with the skin and UVs back, Apply puts the painting in");
	dialog->close();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	return ok;
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
	const auto root = qEnvironmentVariable("SLOPSTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("skin-paint-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(temporary.filePath("settings.ini"));
	bool ok = true;
	// The synthetic triangle seen face-on from above.
	const ModelMesh mesh = tests::q3Model();
	QImage skin(64, 64, QImage::Format_ARGB32);
	skin.fill(Qt::white);
	for (int scenario = 0; scenario < 2; ++scenario)
	{
		applyStudioTheme(app, studioThemeTokens(scenario ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scenario ? 200 : 100));
		ModelSkinPaintDialog dialog(mesh, 0, 0, skin);
		dialog.setLayoutDirection(scenario ? Qt::RightToLeft : Qt::LeftToRight);
		dialog.show();
		auto *canvas = dialog.findChild<QWidget *>("modelSkinPaintCanvas");
		auto *radius = dialog.findChild<QSpinBox *>("modelSkinPaintRadius");
		auto *pitch = dialog.findChild<QDoubleSpinBox *>("modelSkinPaintPitch");
		auto *mode = dialog.findChild<QComboBox *>("modelSkinPaintMode");
		auto *mirror = dialog.findChild<QComboBox *>("modelSkinPaintMirror");
		auto *undo = dialog.findChild<QPushButton *>("modelSkinPaintUndo");
		auto *redo = dialog.findChild<QPushButton *>("modelSkinPaintRedo");
		auto *stamp = dialog.findChild<QPushButton *>("modelSkinPaintStamp");
		auto *apply = dialog.findChild<QPushButton *>("modelSkinPaintApply");
		auto *inView = dialog.findChild<QPushButton *>("modelSkinPaintInView");
		ok &= expect(canvas && radius && pitch && mode && mirror && undo && redo && stamp && apply && inView, "the dialog has its controls");
		if (!(canvas && radius && pitch && mode && mirror && undo && redo && stamp && apply && inView))
			break;
		for (QWidget *widget : std::initializer_list<QWidget *>{canvas, radius, pitch, mode, mirror, undo, redo, stamp, apply, inView})
			ok &= expect(!widget->accessibleName().isEmpty() && widget->focusPolicy() != Qt::NoFocus, "paint controls are named and keyboard reachable");
		ok &= expect(mode->count() == 3 && !apply->isEnabled() && !apply->toolTip().isEmpty(),
					 "Smudge is a blend, and Apply to Model says why it is off without a target");
		pitch->setValue(89);
		radius->setValue(30);
		mode->setCurrentIndex(1);
		// Find a pixel of the triangle in the view.
		// The centroid of the triangle's pixels lies inside it.
		QPointF onModel(-1, -1);
		double sumX = 0, sumY = 0;
		int count = 0;
		for (int y = 0; y < dialog.view().size.height(); ++y)
			for (int x = 0; x < dialog.view().size.width(); ++x)
				if (dialog.view().ids.at(qsizetype(y) * dialog.view().size.width() + x) >= 0)
				{
					sumX += x + 0.5;
					sumY += y + 0.5;
					++count;
				}
		if (count > 0)
			onModel = QPointF(sumX / count, sumY / count);
		ok &= expect(onModel.x() >= 0, "the model shows in the paint view");
		const QImage before = dialog.skin();
		ok &= expect(dialog.beginStroke(onModel) && dialog.strokeActive() && dialog.skin() != before, "a stroke paints as it goes");
		QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
		QCoreApplication::sendEvent(&dialog, &escape);
		ok &= expect(!dialog.strokeActive() && dialog.skin() == before && dialog.isVisible(), "Escape cancels the stroke and keeps the dialog open");
		ok &= expect(dialog.beginStroke(onModel) && dialog.extendStroke(onModel + QPointF(10, 0)) && dialog.finishStroke(true) && dialog.skin() != before,
					 "a finished stroke stays");
		const QImage painted = dialog.skin();
		ok &= expect(undo->isEnabled(), "Undo is available after a stroke");
		undo->click();
		ok &= expect(dialog.skin() == before && redo->isEnabled(), "Undo restores the skin");
		redo->click();
		ok &= expect(dialog.skin() == painted, "Redo paints the stroke again");
		// The recorded strokes replay to the same skin through the core service.
		ModelSkinPaintCamera camera;
		QVector<ModelSkinPaintStroke> strokes;
		QString error;
		QImage replay = before;
		ModelSkinPaintView view;
		bool replayed = parseModelSkinPaintStrokes(dialog.strokesJson(), &camera, &strokes, &error) && strokes.size() == 1 &&
						buildModelSkinPaintView(mesh, 0, camera, &view, &error);
		for (const auto &stroke : std::as_const(strokes))
			replayed = replayed && paintModelSkinStroke(mesh, 0, 0, view, stroke, &replay, nullptr, &error);
		ok &= expect(replayed && replay == painted, "the recorded strokes replay to the same skin");
		ok &= expect(dialog.saveTo(temporary.filePath(QStringLiteral("painted%1.png").arg(scenario))), "Save Skin writes the image");
	}
	ok &= longStrokeAndDiscard(mesh, skin, temporary.filePath(QStringLiteral("long-stroke.png")));
	{
		ModelEditorDialog editor;
		editor.resize(1400, 900);
		editor.show();
		QString error;
		ok &= expect(editor.setMesh(mesh, &error), "load the editor");
		auto *action = editor.findChild<QAction *>("meshPaintSkin");
		ok &= expect(action != nullptr, "UV > Paint Skin… exists");
		if (action)
		{
			action->trigger();
			auto *opened = editor.findChild<QDialog *>("modelSkinPaintDialog");
			ok &= expect(opened && opened->isVisible(), "Paint Skin… opens the dialog");
			if (opened)
				opened->close();
		}
		editor.close();
	}
	{
		// An MDL model in the editor: paint in its viewport, then apply.
		ModelMesh quake = mesh;
		ModelEdit add;
		add.kind = ModelEditKind::AddMdlSkin;
		add.mdlSkin.size = {64, 64};
		add.mdlSkin.pixels = QByteArray(64 * 64, char(0));
		for (int i = 0; i < 256; ++i)
			add.mdlSkin.palette.append(QByteArray(3, char(i)));
		QString error;
		ok &= expect(applyModelEdit(&quake, add, nullptr, &error), "give the model an MDL skin");
		ModelEditorDialog editor;
		editor.resize(1400, 900);
		editor.show();
		ok &= expect(editor.setMesh(quake, &error), "load the MDL model");
		auto *viewport = editor.findChild<ModelViewport *>("meshPreview");
		auto *top = editor.findChild<QAction *>("meshViewTop");
		auto *perspective = editor.findChild<QAction *>("meshViewPerspective");
		auto *undoMesh = editor.findChild<QAction *>("undoMesh");
		auto *open = editor.findChild<QAction *>("meshPaintSkin");
		ok &= expect(viewport && top && perspective && undoMesh && open, "the editor has its view, undo and paint actions");
		if (viewport && top && perspective && undoMesh && open)
		{
			top->trigger();
			open->trigger();
			auto *dialog = static_cast<ModelSkinPaintDialog *>(editor.findChild<QDialog *>("modelSkinPaintDialog"));
			auto *apply = dialog ? dialog->findChild<QPushButton *>("modelSkinPaintApply") : nullptr;
			auto *inView = dialog ? dialog->findChild<QPushButton *>("modelSkinPaintInView") : nullptr;
			ok &= expect(dialog && apply && inView && apply->isEnabled() && apply->toolTip().contains(QStringLiteral("MDL")),
						 "Apply to Model replaces the model's MDL skin");
			if (dialog && apply && inView)
			{
				inView->click();
				ok &= expect(viewport->skinPaintEnabled() && dialog->paintingInView(), "Paint in View hooks the editor's viewport");
				const auto send = [&](QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons buttons) {
					QMouseEvent event(type, at, viewport->mapToGlobal(at), button, buttons, Qt::NoModifier);
					QCoreApplication::sendEvent(viewport, &event);
				};
				const auto strokeInView = [&](const char *label) {
					tests::settleModelViewport(*viewport);
					QPointF point;
					if (!expect(viewport->projectToView({10.0f / 3.0f, 10.0f / 3.0f, 0.0f}, &point), "the model shows in the viewport"))
						return;
					const QImage before = dialog->skin();
					const int strokes = int(dialog->strokesJson().value("strokes").toArray().size());
					send(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
					send(QEvent::MouseMove, point + QPointF(4, 0), Qt::NoButton, Qt::LeftButton);
					send(QEvent::MouseButtonRelease, point + QPointF(4, 0), Qt::LeftButton, Qt::NoButton);
					const auto list = dialog->strokesJson().value("strokes").toArray();
					ok &= expect(dialog->skin() != before && list.size() == strokes + 1 && list.last().toObject().contains("view"), label);
				};
				strokeInView("a drag in the orthographic viewport paints through its camera");
				// Escape in the viewport cancels the stroke in progress.
				const QImage painted = dialog->skin();
				QPointF point;
				if (viewport->projectToView({10.0f / 3.0f, 10.0f / 3.0f, 0.0f}, &point))
				{
					send(QEvent::MouseButtonPress, point + QPointF(0, 6), Qt::LeftButton, Qt::LeftButton);
					QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
					QCoreApplication::sendEvent(viewport, &escape);
					send(QEvent::MouseButtonRelease, point + QPointF(0, 6), Qt::LeftButton, Qt::NoButton);
					ok &= expect(dialog->skin() == painted && !dialog->strokeActive(), "Escape in the viewport cancels the stroke");
				}
				perspective->trigger();
				strokeInView("a drag in the perspective viewport paints through its camera");
				// Apply to Model is one undo step.
				const QByteArray revision = editor.document().revisionFingerprint();
				apply->click();
				ok &= expect(modelSurfaceIndexedSkin(editor.document().mesh(), 0) == dialog->skin(), "Apply puts the painted skin into the model");
				undoMesh->trigger();
				ok &= expect(editor.document().revisionFingerprint() == revision, "one Undo takes the painted skin out again");
				inView->click();
				ok &= expect(!viewport->skinPaintEnabled(), "turning Paint in View off frees the viewport");
				dialog->close();
				QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			}
		}
	}
	ok &= staleApplyRefused(mesh);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
