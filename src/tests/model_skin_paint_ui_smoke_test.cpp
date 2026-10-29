// UV > Paint Skin… (UVSKIN-09, app/model_skin_paint_dialog.h): brush settings
// are named and keyboard reachable, a stroke paints the skin and the preview,
// Escape cancels a stroke in progress, Undo and Redo step through strokes, the
// recorded strokes replay through the core service to the same skin, and the
// Mesh Editor's UV menu opens it; 100% and 200% text, LTR and RTL.
#include "app/model_editor_dialog.h"
#include "app/model_skin_paint_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_q3_animation_test_helpers.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFont>
#include <QKeyEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << "FAIL: " << message << '\n';
	return value;
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
		ok &= expect(canvas && radius && pitch && mode && mirror && undo && redo, "the dialog has its controls");
		if (!(canvas && radius && pitch && mode && mirror && undo && redo))
			break;
		for (QWidget *widget : std::initializer_list<QWidget *>{canvas, radius, pitch, mode, mirror, undo, redo})
			ok &= expect(!widget->accessibleName().isEmpty() && widget->focusPolicy() != Qt::NoFocus, "paint controls are named and keyboard reachable");
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
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
