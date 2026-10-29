#include "app/application_shell.h"
#include "app/material_workbench.h"
#include "app/model_design_dialog.h"
#include "app/model_editor_dialog.h"
#include "app/studio_theme.h"
#include "app/texture_editor_dialog.h"
#include "core/idtech_image.h"
#include "core/model_mdl.h"
#include "core/studio_settings.h"
#include "tests/model_mdl_test_helpers.h"

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdlib>
#include <functional>
#include <iostream>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

// The shell's routes from the Mesh Editor: staging files in the open package
// (stageFile, Export UV Layout), Edit Skin opening its texture project in
// Textures and the save coming back to the model, and Show Material opening
// a shader in Materials.
using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *message, const QString &detail = {})
{
	++checks;
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << message << ": " << qPrintable(detail) << '\n';
	}
	return value;
}
bool until(const std::function<bool()> &ready, int milliseconds = 20000)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < milliseconds)
	{
		QEventLoop loop;
		QTimer::singleShot(15, &loop, &QEventLoop::quit);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}
bool write(const QString &path, const QByteArray &bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray png(QSize size, QColor colour)
{
	QImage image(size, QImage::Format_ARGB32);
	image.fill(colour);
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	return bytes;
}
QWidget *dialogNamed(const QString &name)
{
	for (auto *widget : QApplication::topLevelWidgets())
		if (widget->objectName() == name && widget->isVisible())
			return widget;
	return nullptr;
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
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temp(QDir(root).filePath(QStringLiteral("skin-shell-XXXXXX")));
	if (!temp.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	{
		StudioSettings settings;
		settings.setRestoreSession(false);
		settings.setReducedMotion(true);
		settings.sync();
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	// A folder package with a skin image and a shader that uses it.
	const auto package = temp.filePath(QStringLiteral("package"));
	expect(write(QDir(package).filePath(QStringLiteral("textures/test/skin.png")), png({8, 8}, Qt::gray)) &&
			   write(QDir(package).filePath(QStringLiteral("scripts/test.shader")),
					 "models/test/shiny\n{\n\t{\n\t\tmap textures/test/skin.png\n\t}\n}\n"),
		   "write the package fixture");
	QString error;
	auto palette = generatedIdTechPalette(QStringLiteral("quake"));
	ModelMesh mdl;
	expect(importEditableModel("fixture.mdl", groupedMdlFixture().bytes, &mdl, &error, &palette) && mdl.mdl.enabled, "import the MDL fixture", error);
	{
		ApplicationShell shell;
		shell.resize(1600, 1000);
		shell.show();
		shell.openPathFromCommandLine(package);
		auto *designButton = shell.findChild<QPushButton *>(QStringLiteral("openModelDesigner"));
		if (!expect(designButton != nullptr, "Models exposes the designer"))
			return EXIT_FAILURE;
		designButton->click();
		auto *designer = static_cast<ModelDesignDialog *>(shell.findChild<QDialog *>(QStringLiteral("modelDesignDialog")));
		if (!expect(designer && designer->editMesh, "the designer hands meshes to the shell"))
			return EXIT_FAILURE;
		designer->editMesh(mdl);
		auto *editor = static_cast<ModelEditorDialog *>(shell.findChild<QDialog *>(QStringLiteral("modelEditorDialog")));
		if (!expect(editor && until([&] { return !editor->operationBusy(); }), "the shell opens the Mesh Editor"))
			return EXIT_FAILURE;
		designer->close();
		expect(editor->editSkinFile && editor->showShader && editor->stageFile && editor->readPackageFiles, "the shell connects every skin route");
		// Round-trip files go to this test's project folder.
		editor->deliveryContext = [&]
		{
			ModelEditorDeliveryContext context;
			context.projectRoot = temp.filePath(QStringLiteral("project"));
			return context;
		};
		const auto skinPath = QStringLiteral("textures/test/skin.png");
		expect(until([&] { return editor->readPackageFiles({skinPath}).contains(skinPath); }), "the package opens in the shell");

		// stageFile: one package step; replacing needs replace, unsafe paths are refused.
		const auto layout = png({4, 4}, Qt::red);
		expect(editor->stageFile(QStringLiteral("textures/test/layout.png"), layout, false, &error) &&
				   editor->readPackageFiles({QStringLiteral("textures/test/layout.png")}).value(QStringLiteral("textures/test/layout.png")) == layout,
			   "a staged file is in the package", error);
		// Replacing a file of the open package stages the new bytes over it.
		expect(editor->stageFile(skinPath, png({8, 8}, Qt::blue), true, &error) &&
				   editor->readPackageFiles({skinPath}).value(skinPath) == png({8, 8}, Qt::blue),
			   "replace stages over a package file", error);
		expect(!editor->stageFile(QStringLiteral("../outside.png"), layout, true, &error), "an unsafe package path is refused");
		// Export UV Layout stages through the same route.
		QTimer::singleShot(0, [&]
						   {
							   auto *dialog = qobject_cast<QDialog *>(dialogNamed(QStringLiteral("modelUvLayoutDialog")));
							   if (!expect(dialog != nullptr, "Export UV Layout opens"))
								   return;
							   auto *destination = dialog->findChild<QComboBox *>(QStringLiteral("uvLayoutDestination"));
							   expect(destination && destination->count() == 2, "staging is offered with a package open");
							   destination->setCurrentIndex(1);
							   dialog->findChild<QLineEdit *>(QStringLiteral("uvLayoutPath"))->setText(QStringLiteral("skins/fixture_uv.png"));
							   dialog->accept();
						   });
		editor->findChild<QAction *>(QStringLiteral("exportMeshUvLayout"))->trigger();
		const auto staged = editor->readPackageFiles({QStringLiteral("skins/fixture_uv.png")}).value(QStringLiteral("skins/fixture_uv.png"));
		expect(!staged.isEmpty() && QImage::fromData(staged).size() == mdl.mdl.skinSize, "Export UV Layout stages the template in the package");

		// Edit Skin opens the texture project in Textures; its save comes back.
		expect(editor->editSurfaceSkin(&error), "Edit Skin", error);
		const auto files = editor->skinRoundTripFiles();
		auto *textures = shell.findChild<TextureEditorDialog *>();
		expect(files.size() == 1 && textures &&
				   until([&] { return !textures->isBusy() && textures->document().layers().size() == 2; }) && textures->isVisible(),
			   "Textures opens the skin with its UV guide layer");
		textures->applyOperations(QJsonArray{QJsonObject{{"op", "fill"}, {"x", 0}, {"y", 0}, {"color", "#ff0000"}}});
		expect(until([&] { return !textures->isBusy(); }) && textures->hasUnsavedChanges(), "paint in Textures");
		textures->saveProjectToPath(files.value(0), true);
		expect(until([&] { return !textures->isBusy() && !textures->hasUnsavedChanges(); }), "Textures saves the project");
		const int steps = editor->document().undoSteps();
		const auto member = editor->document().mesh().embeddedSkins[0].indexedFrames[0];
		editor->checkSkinRoundTrips(true);
		expect(editor->hasPendingSkin(), "the Mesh Editor sees the save");
		expect(editor->applySkinFromTextures(&error) && editor->document().undoSteps() == steps + 1 &&
				   editor->document().mesh().embeddedSkins[0].indexedFrames[0] != member,
			   "the saved skin replaces the member as one undo step", error);

		// Show Material on a shader opens it in Materials.
		auto *materials = shell.findChild<MaterialWorkbench *>();
		editor->showShader(QStringLiteral("models/test/shiny"));
		expect(materials && until([&] { return materials->currentMaterial() == QStringLiteral("models/test/shiny"); }) && materials->isVisible(),
			   "Show Material reveals the shader in Materials", materials ? materials->currentMaterial() : QString());
	}
	app.processEvents();
	StudioSettings::setOverrideFilePath({});
	std::cout << checks << " skin shell checks; " << failures << " failures\n";
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
