// UV > Bake Maps… (UVSKIN-06, app/model_bake_dialog.h): the dialog's
// controls are named and keyboard reachable, a bake runs on its worker with
// progress and a preview, Cancel stops a long bake and keeps the previous
// map, Save writes PNG and TGA, at 100% and 200% text, LTR and RTL; the
// Mesh Editor's UV menu opens it.
#include "app/model_bake_dialog.h"
#include "app/model_editor_dialog.h"
#include "app/studio_theme.h"
#include "core/model_document.h"
#include "core/model_skin_paint.h"
#include "core/studio_settings.h"
#include "tests/model_q3_animation_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QThread>

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
bool waitFor(const std::function<bool()> &condition, int timeout = 60000)
{
	QElapsedTimer timer;
	timer.start();
	while (!condition() && timer.elapsed() < timeout)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(1);
	}
	return condition();
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
	QTemporaryDir temporary(QDir(root).filePath("model-bake-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(temporary.filePath("settings.ini"));
	bool ok = true;
	const ModelMesh mesh = tests::q3Model();
	for (int scenario = 0; scenario < 2; ++scenario)
	{
		applyStudioTheme(app, studioThemeTokens(scenario ? StudioTheme::HighContrastLight : StudioTheme::Dark, UiDensity::Standard, scenario ? 200 : 100));
		ModelBakeDialog dialog(mesh, 0, 0);
		dialog.setLayoutDirection(scenario ? Qt::RightToLeft : Qt::LeftToRight);
		dialog.show();
		auto *type = dialog.findChild<QComboBox *>("modelBakeType");
		auto *size = dialog.findChild<QComboBox *>("modelBakeSize");
		auto *samples = dialog.findChild<QSpinBox *>("modelBakeSamples");
		auto *bake = dialog.findChild<QPushButton *>("modelBakeStart");
		auto *cancel = dialog.findChild<QPushButton *>("modelBakeCancel");
		auto *save = dialog.findChild<QPushButton *>("modelBakeSave");
		auto *status = dialog.findChild<QLabel *>("modelBakeStatus");
		auto *progress = dialog.findChild<QProgressBar *>("modelBakeProgress");
		ok &= expect(type && size && samples && bake && cancel && save && status && progress, "the dialog has its controls");
		if (!(type && size && samples && bake && cancel && save && status && progress))
			break;
		for (QWidget *widget : std::initializer_list<QWidget *>{type, size, samples, bake, cancel, save, status, progress})
			ok &= expect(!widget->accessibleName().isEmpty(), "every bake control is named");
		for (QWidget *widget : std::initializer_list<QWidget *>{type, size, samples, bake, save})
			ok &= expect(widget->focusPolicy() != Qt::NoFocus, "bake controls are keyboard reachable");
		ok &= expect(!save->isEnabled() && !cancel->isEnabled(), "nothing to save or cancel before a bake");
		auto *apply = dialog.findChild<QPushButton *>("modelBakeApply");
		ok &= expect(apply && !apply->isEnabled() && !apply->toolTip().isEmpty() && !apply->accessibleName().isEmpty(),
					 "Apply to Model is named and says why it is off without a skin to hand the map to");
		size->setCurrentIndex(0);
		samples->setValue(16);
		bake->click();
		ok &= expect(dialog.running() && cancel->isEnabled(), "a running bake offers Cancel");
		ok &= expect(waitFor([&] { return !dialog.running(); }) && !dialog.result().image.isNull() && save->isEnabled(), "the bake finishes with a map to save");
		const QImage first = dialog.result().image;
		ok &= expect(dialog.saveTo(temporary.filePath(QStringLiteral("ao%1.png").arg(scenario))) &&
						 dialog.saveTo(temporary.filePath(QStringLiteral("ao%1.tga").arg(scenario))) &&
						 QFileInfo::exists(temporary.filePath(QStringLiteral("ao%1.tga").arg(scenario))),
					 "Save writes PNG and TGA");
		// A long bake, cancelled at once, keeps the previous map.
		size->setCurrentIndex(size->count() - 2);
		samples->setValue(1024);
		ok &= expect(dialog.start(), "a second bake starts");
		dialog.cancel();
		ok &= expect(waitFor([&] { return !dialog.running(); }) && dialog.result().image == first && status->text().contains(QStringLiteral("cancel"), Qt::CaseInsensitive),
					 "Cancel stops the bake and keeps the previous map");
	}
	// The UV menu opens it.
	{
		ModelEditorDialog editor;
		editor.resize(1400, 900);
		editor.show();
		QString error;
		ok &= expect(editor.setMesh(mesh, &error), "load the editor");
		auto *action = editor.findChild<QAction *>("meshBakeMaps");
		ok &= expect(action != nullptr, "UV > Bake Maps… exists");
		if (action)
		{
			action->trigger();
			auto *opened = editor.findChild<QDialog *>("modelBakeDialog");
			ok &= expect(opened && opened->isVisible(), "Bake Maps… opens the dialog");
			if (opened)
				opened->close();
		}
		editor.close();
	}
	// Apply to Model shades an MDL skin as one undo step.
	{
		ModelMesh quake = mesh;
		ModelEdit add;
		add.kind = ModelEditKind::AddMdlSkin;
		add.mdlSkin.size = {32, 32};
		add.mdlSkin.pixels = QByteArray(32 * 32, char(200));
		for (int i = 0; i < 256; ++i)
			add.mdlSkin.palette.append(QByteArray(3, char(i)));
		QString error;
		ok &= expect(applyModelEdit(&quake, add, nullptr, &error), "give the model an MDL skin");
		ModelEditorDialog editor;
		editor.resize(1400, 900);
		editor.show();
		ok &= expect(editor.setMesh(quake, &error), "load the MDL model");
		auto *action = editor.findChild<QAction *>("meshBakeMaps");
		auto *undoMesh = editor.findChild<QAction *>("undoMesh");
		if (action && undoMesh)
		{
			action->trigger();
			auto *dialog = static_cast<ModelBakeDialog *>(editor.findChild<QDialog *>("modelBakeDialog"));
			auto *apply = dialog ? dialog->findChild<QPushButton *>("modelBakeApply") : nullptr;
			auto *type = dialog ? dialog->findChild<QComboBox *>("modelBakeType") : nullptr;
			auto *size = dialog ? dialog->findChild<QComboBox *>("modelBakeSize") : nullptr;
			ok &= expect(dialog && apply && type && size && apply->toolTip().contains(QStringLiteral("MDL")), "Apply to Model shades the MDL skin");
			if (dialog && apply && type && size)
			{
				// Lighting from the default light darkens a face turned away a little.
				type->setCurrentIndex(type->findData(int(ModelBakeType::Lighting)));
				size->setCurrentIndex(0);
				ok &= expect(dialog->start() && waitFor([&] { return !dialog->running(); }) && apply->isEnabled(), "a bake makes Apply to Model available");
				const QByteArray revision = editor.document().revisionFingerprint();
				const QImage before = modelSurfaceIndexedSkin(editor.document().mesh(), 0);
				apply->click();
				ok &= expect(editor.document().revisionFingerprint() != revision && modelSurfaceIndexedSkin(editor.document().mesh(), 0) != before,
							 "Apply shades the model's skin");
				undoMesh->trigger();
				ok &= expect(editor.document().revisionFingerprint() == revision, "one Undo takes the shading out again");
				dialog->close();
				QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			}
		}
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
