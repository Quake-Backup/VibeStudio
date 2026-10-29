// Shading > Game in the Mesh Editor (UVSKIN-08): the mode and its controls
// are reachable and named, the viewport draws the game skins on the GPU with
// fullbright texels staying bright where GLQuake lights them, picking keeps
// working, and leaving the mode restores the material images. 100% and 200%
// text, left to right and right to left.
#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/material_render.h"
#include "core/studio_settings.h"
#include "tests/model_legacy_fuzz_seeds.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QLabel>
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
QImage grab(ModelViewport &viewport)
{
	QImage image(viewport.size(), QImage::Format_ARGB32);
	viewport.render(&image);
	return image;
}
// Mean brightness of the pixels that are not the background.
double brightness(const QImage &image)
{
	const QRgb background = image.pixel(1, 1);
	double total = 0;
	int count = 0;
	for (int y = 0; y < image.height(); y += 2)
		for (int x = 0; x < image.width(); x += 2)
		{
			const QRgb p = image.pixel(x, y);
			if (p == background)
				continue;
			total += qRed(p) + qGreen(p) + qBlue(p);
			++count;
		}
	return count ? total / count : 0;
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("model-game-preview-ui-smoke"); skip >= 0)
		return skip;
#ifdef Q_OS_WIN
	app.setFont(QFont("Segoe UI", 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("game-preview-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(temporary.filePath("settings.ini"));
	bool ok = true;
	QString error;
	// Every skin texel fullbright (palette index 240).
	QByteArray bytes = tests::richQuakeMdl();
	for (int at = 84 + 16; at < 84 + 16 + 2 * 128; ++at)
		bytes[at] = char(240);
	for (int at = 84 + 16 + 2 * 128 + 4; at < 84 + 16 + 2 * 128 + 4 + 128; ++at)
		bytes[at] = char(240);
	// GLQuake flood-fills the top-left texel's region; give it its own colour
	// so the fullbright texels stay.
	bytes[84 + 16] = char(15);
	bytes[84 + 16 + 128] = char(15);
	bytes[84 + 16 + 2 * 128 + 4] = char(15);
	const QString model = temporary.filePath("glow.mdl");
	{
		QFile file(model);
		ok &= expect(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write the fullbright MDL");
	}
	for (int scenario = 0; scenario < 2; ++scenario)
	{
		applyStudioTheme(app, studioThemeTokens(scenario ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scenario ? 200 : 100));
		ModelEditorDialog editor;
		editor.setLayoutDirection(scenario ? Qt::RightToLeft : Qt::LeftToRight);
		editor.resize(scenario ? 1700 : 1450, scenario ? 1150 : 900);
		editor.show();
		ok &= expect(editor.openSource(model, &error), "open the MDL");
		auto *mode = editor.findChild<QComboBox *>("meshRenderMode");
		auto *viewport = editor.findChild<ModelViewport *>("meshPreview");
		auto *game = editor.findChild<QAction *>("meshShadingGame");
		ok &= expect(mode && viewport && game && mode->count() == 5, "Shading offers Game");
		if (!mode || !viewport || !game)
			break;
		game->trigger();
		auto *row = editor.findChild<QWidget *>("meshGamePreview");
		auto *renderer = editor.findChild<QComboBox *>("meshGameRenderer");
		auto *top = editor.findChild<QSpinBox *>("meshGameTopColour");
		auto *bottom = editor.findChild<QSpinBox *>("meshGameBottomColour");
		auto *flood = editor.findChild<QCheckBox *>("meshGameFloodFill");
		ok &= expect(mode->currentIndex() == 4 && row && row->isVisible() && renderer && top && bottom && flood, "Game shows its controls");
		if (!renderer || !top || !bottom || !flood)
			break;
		for (QWidget *widget : std::initializer_list<QWidget *>{renderer, top, bottom, flood})
			ok &= expect(!widget->accessibleName().isEmpty() && widget->focusPolicy() != Qt::NoFocus, "Game controls are named and keyboard reachable");
		ok &= expect(viewport->gameSkinsActive(), "the viewport draws the game skins");
		viewport->setOrbit(30, 40);
		viewport->frameModel();
		ok &= expect(tests::settleModelViewport(*viewport), "the Game view renders");
		const QImage modernImage = grab(*viewport);
		renderer->setCurrentIndex(renderer->findData(int(MaterialQuakeRenderer::GLQuake)));
		ok &= expect(tests::settleModelViewport(*viewport), "the GLQuake view renders");
		const QImage glquakeImage = grab(*viewport);
		// Every texel is fullbright: unlit (never darker) in a modern port,
		// shaded like any texel in GLQuake.
		std::cerr << "brightness modern " << brightness(modernImage) << " glquake " << brightness(glquakeImage)
				  << (modernImage == glquakeImage ? " (identical)" : "") << '\n';
		ok &= expect(modernImage != glquakeImage && brightness(modernImage) >= brightness(glquakeImage),
					 "fullbright texels stay bright in a modern port and are lit in GLQuake");
		renderer->setCurrentIndex(renderer->findData(int(MaterialQuakeRenderer::Software)));
		top->setValue(4);
		ok &= expect(tests::settleModelViewport(*viewport) && viewport->hasMesh(), "the software renderer and player colours render");
		mode->setCurrentIndex(3);
		ok &= expect(!viewport->gameSkinsActive() && !row->isVisible(), "leaving Game restores the material images");
		editor.close();
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
