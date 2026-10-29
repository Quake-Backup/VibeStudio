#include "app/application_shell.h"
#include "app/material_workbench.h"
#include "app/model_design_dialog.h"
#include "app/model_editor_dialog.h"
#include "app/model_assembly_dialog.h"
#include "app/studio_theme.h"
#include "core/model_assembly.h"
#include "core/model_recovery.h"
#include "core/studio_settings.h"
#include "core/workspace_document.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QJsonDocument>
#include <QListWidget>
#include <QStackedWidget>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdlib>
#include <functional>
#include <iostream>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

using namespace vibestudio;
namespace
{
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return condition;
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
	// A child run writes one mesh recovery copy and exits, so the copy's owner is
	// a studio that is no longer running, as after a crash.
	if (argc == 4 && QByteArray(argv[1]) == "--write-recovery")
	{
		QCoreApplication child(argc, argv);
		StudioSettings::setOverrideFilePath(QString::fromLocal8Bit(argv[2]));
		ModelDesign design;
		design.parts << ModelDesignPart{};
		ModelRecoverySnapshot snapshot;
		snapshot.mesh = buildModelDesignMesh(design);
		snapshot.title = QStringLiteral("Crashed prop");
		return writeModelRecovery(snapshot, modelRecoveryDirectory(), QString::fromLocal8Bit(argv[3])).isEmpty() ? EXIT_FAILURE : EXIT_SUCCESS;
	}
	// Public widget signals only: this does not inject user input or capture the screen.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont("Segoe UI", 10));
#endif
	QElapsedTimer elapsed;
	elapsed.start();
	const auto timing = [&](const char *stage)
	{
		if (qEnvironmentVariableIsSet("VIBESTUDIO_MODELLER_TIMING"))
		{
			std::cout << "TIMING " << stage << ' ' << elapsed.elapsed() << " ms" << std::endl;
		}
	};
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("mesh-shell-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	bool ok = true;
	QString error;
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	const auto plane = buildModelDesignMesh(design);
	{
		// Match main.cpp: apply the saved theme before constructing the shell.
		const auto preferences = StudioSettings().accessibilityPreferences();
		applyStudioTheme(app, studioThemeTokens(preferences.theme, preferences.density, preferences.textScalePercent));
		timing("shell-construction-start");
		ApplicationShell shell;
		timing("shell-construction-done");
		shell.show();
		auto *assemblyButton = shell.findChild<QPushButton *>(QStringLiteral("openModelAssembly"));
		ok &= expect(assemblyButton != nullptr, "Models exposes linked assembly authoring");
		if (assemblyButton) { assemblyButton->click(); }
		auto *assembly = static_cast<ModelAssemblyDialog *>(shell.findChild<QDialog *>(QStringLiteral("modelAssemblyDialog")));
		ok &= expect(assembly && bool(assembly->editBakedPose), "assembly provides the ordinary mesh editor handoff");
		if (assembly) { assembly->close(); }
		app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
		auto *open = shell.findChild<QPushButton *>(QStringLiteral("openModelEditor"));
		ok &= expect(open != nullptr, "Models exposes an editor action");
		if (open)
		{
			open->click();
		}
		app.processEvents();
		ok &= expect(shell.findChild<QDialog *>(QStringLiteral("modelEditorDialog")) != nullptr,
					 "Models opens the mesh editor through the shell");
		timing("shell-editor-open");
		if (auto *editor = shell.findChild<QDialog *>(QStringLiteral("modelEditorDialog")))
		{
			editor->close();
		}
		app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
		auto *designButton = shell.findChild<QPushButton *>(QStringLiteral("openModelDesigner"));
		ok &= expect(designButton != nullptr, "Models exposes primitive-to-mesh authoring");
		if (designButton)
		{
			designButton->click();
		}
		QPointer<ModelDesignDialog> designer =
			static_cast<ModelDesignDialog *>(shell.findChild<QDialog *>(QStringLiteral("modelDesignDialog")));
		ok &= expect(designer && bool(designer->editMesh), "primitive designer provides the shared mesh handoff");
		if (designer && designer->editMesh && open)
		{
			auto busyMesh = plane;
			busyMesh.surfaces[0].triangles.fill(plane.surfaces[0].triangles[0], 4096);
			updateEditableModelMetadata(&busyMesh);
			bool reentered = false;
			QTimer::singleShot(0, &shell,
							   [&]
							   {
								   auto *preparing =
									   static_cast<ModelEditorDialog *>(shell.findChild<QDialog *>(QStringLiteral("modelEditorDialog")));
								   reentered = preparing && preparing->operationBusy();
								   open->click();
								   ok &= expect(shell.findChildren<QDialog *>(QStringLiteral("modelEditorDialog")).size() == 1,
												"shell reentry finds the modeller before initial preparation finishes");
							   });
			designer->editMesh(busyMesh);
			timing("shell-baked-mesh-open");
			QPointer<ModelEditorDialog> editor =
				static_cast<ModelEditorDialog *>(shell.findChild<QDialog *>(QStringLiteral("modelEditorDialog")));
			ok &= expect(reentered && editor && editor->document().mesh().triangleCount == 4096,
						 "baked geometry is prepared and adopted through the registered editor");
			if (editor)
			{
				bool deferred = false;
				QTimer::singleShot(0, &shell, [&] { deferred = !shell.close() && editor && editor->operationBusy(); });
				ModelEdit normals;
				normals.kind = ModelEditKind::RecalculateNormals;
				ok &= expect(!editor->applyEdit(normals, &error) && deferred,
							 "shell close cancels document work before closing other authoring dialogs");
				for (int step = 0; step < 4; ++step)
				{
					app.processEvents();
					app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
				}
				ok &= expect(!editor && !designer && !shell.isVisible(),
							 "one studio-close request resumes after mesh work and closes the other surfaces");
			}
			if (designer)
			{
				designer->close();
			}
			app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
		}
	}
	{
		// Show in Materials: a folder package whose shader declares
		// models/props/crate over a differently named image, and an image that
		// no material declares.
		const auto package = QDir(temporary.path()).filePath(QStringLiteral("materials-package"));
		QDir().mkpath(package + QStringLiteral("/scripts"));
		QDir().mkpath(package + QStringLiteral("/models/props"));
		QImage pixel(2, 2, QImage::Format_RGB32);
		pixel.fill(Qt::red);
		QFile shader(package + QStringLiteral("/scripts/test.shader"));
		ok &= expect(pixel.save(package + QStringLiteral("/models/props/crate_diffuse.png")) &&
						 pixel.save(package + QStringLiteral("/models/props/plain.png")) && shader.open(QIODevice::WriteOnly) &&
						 shader.write("models/props/crate\n{\n\t{\n\t\tmap models/props/crate_diffuse.png\n\t}\n}\n") > 0,
					 "write the material fixture package");
		shader.close();
		const auto waitFor = [&](const std::function<bool()> &condition)
		{
			QElapsedTimer clock;
			clock.start();
			while (!condition() && clock.elapsed() < 60000)
			{
				app.processEvents(QEventLoop::AllEvents, 20);
			}
			return condition();
		};
		ApplicationShell shell;
		shell.show();
		shell.openPathFromCommandLine(package);
		auto *open = shell.findChild<QPushButton *>(QStringLiteral("openModelEditor"));
		if (open)
		{
			open->click();
		}
		QPointer<ModelEditorDialog> editor = static_cast<ModelEditorDialog *>(shell.findChild<QDialog *>(QStringLiteral("modelEditorDialog")));
		auto *workbench = shell.findChild<MaterialWorkbench *>();
		auto *stack = shell.findChild<QStackedWidget *>(QStringLiteral("modeStack"));
		ok &= expect(editor && bool(editor->showInMaterials) && workbench && stack &&
						 waitFor([&] { return editor->context && !editor->context().packagePath.isEmpty(); }),
					 "the Mesh Editor sees the open package and offers Show in Materials");
		if (editor && workbench && stack)
		{
			editor->showInMaterials(QStringLiteral("models/props/crate"));
			ok &= expect(waitFor([&] { return !workbench->isBusy() && workbench->currentMaterial() == QStringLiteral("models/props/crate"); }) &&
							 stack->currentIndex() == static_cast<int>(StudioMode::Shaders),
						 "Show in Materials reveals the declared shader in the Materials workbench");
			editor->showInMaterials(QStringLiteral("models/props/plain.tga"));
			ok &= expect(waitFor([&] { return stack->currentIndex() == static_cast<int>(StudioMode::Textures); }),
						 "a material that no script declares falls back to its image in Textures");
			editor->close();
		}
		app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
	}
	{
		// Model sources are documents: a dropped or Quick Open .mesh.json opens
		// in the Mesh Editor, .model.json in Design Prop and .assembly.json in
		// Assemble; each is remembered under recent models. Mesh Editor on a
		// game model with an editable source beside it offers the source.
		const QDir folder(QDir(temporary.path()).filePath(QStringLiteral("source-package")));
		QDir().mkpath(folder.filePath(QStringLiteral("models/props")));
		auto withClip = plane;
		withClip.animations << ModelAnimation{QStringLiteral("idle"), 0, 1, 10};
		ModelDocument sourceDocument;
		const auto meshPath = folder.filePath(QStringLiteral("models/props/crate.mesh.json"));
		const auto md3 = exportEditableModel(withClip, QStringLiteral("md3"), 0, &error);
		QFile native(folder.filePath(QStringLiteral("models/props/crate.md3")));
		ok &= expect(sourceDocument.setMesh(withClip, &error) && sourceDocument.save(meshPath, false, &error) && !md3.isEmpty() &&
						 native.open(QIODevice::WriteOnly) && native.write(md3) == md3.size(),
					 "write a source and its MD3 into a folder package");
		native.close();
		const auto designPath = QDir(temporary.path()).filePath(QStringLiteral("prop.model.json"));
		ok &= expect(saveModelDesignBytes(designPath, QJsonDocument(modelDesignJson(design)).toJson(), false, &error), "write a design");
		ModelAssembly recipe;
		recipe.name = QStringLiteral("crate");
		ModelAssemblyPart rootPart;
		rootPart.id = QStringLiteral("root");
		rootPart.source = folder.filePath(QStringLiteral("models/props/crate.md3"));
		recipe.parts << rootPart;
		const auto assemblyPath = QDir(temporary.path()).filePath(QStringLiteral("crate.assembly.json"));
		QFile assemblyFile(assemblyPath);
		ok &= expect(assemblyFile.open(QIODevice::WriteOnly) && assemblyFile.write(QJsonDocument(modelAssemblyJson(recipe)).toJson()) > 0,
					 "write an assembly");
		assemblyFile.close();

		ApplicationShell shell;
		shell.show();
		shell.openPathFromCommandLine(meshPath);
		app.processEvents();
		QPointer<ModelEditorDialog> editor = static_cast<ModelEditorDialog *>(shell.findChild<QDialog *>(QStringLiteral("modelEditorDialog")));
		ok &= expect(editor && shell.findChildren<QDialog *>(QStringLiteral("modelEditorDialog")).size() == 1 &&
						 QFileInfo(editor->document().path()) == QFileInfo(meshPath),
					 "opening a .mesh.json path opens it in the Mesh Editor, not the code editor");
		ok &= expect(StudioSettings().recentFiles(QStringLiteral("model")).contains(QFileInfo(meshPath).absoluteFilePath()),
					 "the opened source is a recent model file");
		// A workspace keeps the open source and reopens it in the Mesh Editor.
		const auto workspacePath = QDir(temporary.path()).filePath(QStringLiteral("models.vibeworkspace"));
		WorkspaceDocument saved;
		ok &= expect(shell.saveWorkspaceTo(workspacePath, true, &error) && readWorkspaceDocument(workspacePath, &saved, nullptr, &error) &&
						 saved.modelSources.size() == 1 && QFileInfo(saved.modelSources.first()) == QFileInfo(meshPath),
					 "a saved workspace lists the open model source");
		if (editor)
		{
			editor->close();
		}
		app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
		ok &= expect(shell.openWorkspaceFrom(workspacePath, &error), "open the workspace");
		app.processEvents();
		editor = static_cast<ModelEditorDialog *>(shell.findChild<QDialog *>(QStringLiteral("modelEditorDialog")));
		ok &= expect(editor && QFileInfo(editor->document().path()) == QFileInfo(meshPath), "the workspace reopens the model source in the Mesh Editor");
		if (editor)
		{
			editor->close();
		}
		app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
		shell.openPathFromCommandLine(designPath);
		app.processEvents();
		auto *designer = static_cast<ModelDesignDialog *>(shell.findChild<QDialog *>(QStringLiteral("modelDesignDialog")));
		ok &= expect(designer && QFileInfo(designer->path()) == QFileInfo(designPath), "a .model.json opens in Design Prop");
		shell.openPathFromCommandLine(assemblyPath);
		app.processEvents();
		ok &= expect(shell.findChild<QDialog *>(QStringLiteral("modelAssemblyDialog")) != nullptr &&
						 StudioSettings().recentFiles(QStringLiteral("model")).size() >= 3,
					 "an .assembly.json opens in Assemble and every source is remembered");

		// The game model offers its editable source, which keeps the clip.
		shell.openPathFromCommandLine(folder.path());
		auto *entries = shell.findChild<QListWidget *>(QStringLiteral("modelEntries"));
		const auto waitFor = [&](const std::function<bool()> &condition)
		{
			QElapsedTimer clock;
			clock.start();
			while (!condition() && clock.elapsed() < 60000)
				app.processEvents(QEventLoop::AllEvents, 20);
			return condition();
		};
		const auto findEntry = [&](const QString &path) -> QListWidgetItem *
		{
			for (int row = 0; entries && row < entries->count(); ++row)
				if (entries->item(row)->data(Qt::UserRole).toString() == path)
					return entries->item(row);
			return nullptr;
		};
		ok &= expect(waitFor([&] { return findEntry(QStringLiteral("models/props/crate.md3")) != nullptr; }),
					 "the folder package lists the native model");
		bool listedSource = false;
		for (int row = 0; entries && row < entries->count(); ++row)
			listedSource = listedSource || entries->item(row)->toolTip().contains(QStringLiteral("crate.mesh.json"));
		ok &= expect(listedSource, "the Models list shows the package's editable sources");
		if (auto *item = findEntry(QStringLiteral("models/props/crate.md3")))
			entries->setCurrentItem(item);
		QString offered;
		shell.modelSourcePrompt = [&](const QString &source)
		{
			offered = source;
			return 1;
		};
		auto *openEditor = shell.findChild<QPushButton *>(QStringLiteral("openModelEditor"));
		// The command waits for the model preview to finish decoding.
		ok &= expect(openEditor && waitFor([&] { return openEditor->isEnabled(); }), "Mesh Editor is available once the preview is ready");
		if (openEditor)
			openEditor->click();
		app.processEvents();
		editor = static_cast<ModelEditorDialog *>(shell.findChild<QDialog *>(QStringLiteral("modelEditorDialog")));
		ok &= expect(QFileInfo(offered) == QFileInfo(meshPath) && editor && QFileInfo(editor->document().path()) == QFileInfo(meshPath) &&
						 editor->document().mesh().animations.size() == 1,
					 "Mesh Editor on the game model offers and opens its editable source with its clip");
		if (editor)
		{
			editor->close();
		}
		app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
		shell.modelSourcePrompt = {};
	}
	{
		// A copy left by a studio that is no longer running is offered at
		// startup with Review Models; the setting turns the notice off.
		QProcess writer;
		writer.start(QCoreApplication::applicationFilePath(),
					 {QStringLiteral("--write-recovery"), QDir(temporary.path()).filePath(QStringLiteral("settings.ini")),
					  QStringLiteral("0f0e0d0c-0b0a-4908-8706-050403020100")});
		ok &= expect(writer.waitForFinished(60000) && writer.exitCode() == 0, "a finished process leaves a recovery copy");
		const auto waitFor = [&](const std::function<bool()> &condition)
		{
			QElapsedTimer clock;
			clock.start();
			while (!condition() && clock.elapsed() < 60000)
				app.processEvents(QEventLoop::AllEvents, 20);
			return condition();
		};
		{
			ApplicationShell shell;
			shell.show();
			shell.beginSession(false);
			QPushButton *review = nullptr;
			ok &= expect(waitFor([&] { return (review = shell.findChild<QPushButton *>(QStringLiteral("noticeReviewModels"))) != nullptr; }) &&
							 !review->accessibleName().isEmpty(),
						 "the startup notice offers Review Models with an accessible name");
		}
		app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
		StudioSettings().setModelRecoveryNotifyAtStartup(false);
		{
			ApplicationShell shell;
			shell.show();
			shell.beginSession(false);
			QElapsedTimer settle;
			settle.start();
			while (settle.elapsed() < 2000)
				app.processEvents(QEventLoop::AllEvents, 20);
			ok &= expect(shell.findChild<QPushButton *>(QStringLiteral("noticeReviewModels")) == nullptr,
						 "turning off Offer mesh and assembly copies at startup suppresses the notice");
		}
		app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
	}
	StudioSettings::setOverrideFilePath(QString());
	timing("complete");
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
