// The Mesh Editor notices when its .mesh.json changes on disk: a notice with
// Reload from Disk, Compare and Keep Editing; Save asks before replacing a
// changed file; the editor's own saves stay quiet; a removed file says so.
// Polling uses DocumentWatcher::pollAt through pollSourceWatch(), so no test
// waits on the real clock. A design save with a stale expected hash is
// refused (core/model_design.h).
#include "app/model_editor_dialog.h"
#include "app/studio_layout.h"
#include "app/studio_theme.h"
#include "core/model_design.h"
#include "core/model_file_io.h"
#include "core/studio_settings.h"
#include "tests/render_test_support.h"

#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QPushButton>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

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
ModelMesh planeWith(int extraTriangles)
{
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	for (int index = 0; index < extraTriangles; ++index)
	{
		ModelDesignPart box;
		box.primitive = QStringLiteral("box");
		box.name = QStringLiteral("box%1").arg(index);
		box.origin = {float(16 * (index + 1)), 0, 0};
		design.parts << box;
	}
	return buildModelDesignMesh(design);
}
QByteArray readBytes(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool writeSource(const QString &path, const ModelMesh &mesh)
{
	ModelDocument document;
	QString error;
	return document.setMesh(mesh, &error) && document.save(path, true, &error);
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("model-editor-external-change-ui-smoke"); skip >= 0)
	{
		return skip;
	}
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("mesh-external-XXXXXX")));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	bool ok = true;
	QString error;
	const auto path = QDir(temporary.path()).filePath(QStringLiteral("prop.mesh.json"));
	const auto original = planeWith(0), external = planeWith(1), later = planeWith(2);
	ok &= expect(writeSource(path, original), "write the source fixture");

	for (const bool rightToLeft : {false, true})
	{
		for (const int scale : {100, 200})
		{
			QApplication::setLayoutDirection(rightToLeft ? Qt::RightToLeft : Qt::LeftToRight);
			applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, scale));
			ok &= expect(writeSource(path, original), "reset the source fixture");
			ModelEditorDialog editor;
			editor.show();
			ok &= expect(editor.openSource(path, &error) && editor.document().path() == QFileInfo(path).absoluteFilePath(),
						 "open the source");
			auto *notice = editor.findChild<NoticeBar *>(QStringLiteral("meshSourceChangedNotice"));
			if (!notice)
			{
				return EXIT_FAILURE;
			}
			qint64 clock = 1000;
			const auto settle = [&]
			{
				editor.pollSourceWatch(clock += 1000);
				editor.pollSourceWatch(clock += 1000);
				app.processEvents();
				// Replaced notice actions are deleted later.
				app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
			};
			settle();
			ok &= expect(notice->isHidden(), "an unchanged source shows no notice");

			// An external write shows the notice with accessible actions.
			ok &= expect(writeSource(path, external), "write an external change");
			settle();
			auto *reload = editor.findChild<QPushButton *>(QStringLiteral("meshSourceReload"));
			auto *keep = editor.findChild<QPushButton *>(QStringLiteral("meshSourceKeepEditing"));
			ok &= expect(!notice->isHidden() && !notice->title().isEmpty() && notice->text().contains(QStringLiteral("prop.mesh.json")) &&
							 reload && keep && !reload->text().isEmpty() && !keep->accessibleDescription().isEmpty() &&
							 editor.sourceChangedOnDisk(),
						 "an external change shows the notice with Reload and Keep Editing");
			ok &= expect(editor.sourceComparisonText().contains(QStringLiteral("unsaved")) == editor.document().isModified() &&
							 editor.sourceComparisonText().count(QLatin1Char('\n')) == 1,
						 "Compare summarises the document and the file on disk");

			// Keep Editing suppresses the same change; Save then asks first.
			if (keep)
				keep->click();
			settle();
			ok &= expect(notice->isHidden(), "Keep Editing hides the notice until the file changes again");
			int asked = 0;
			editor.sourceConflictChoice = [&](const QString &) { return ++asked, 0; };
			const auto onDisk = readBytes(path);
			auto *saveAction = editor.findChild<QAction *>(QStringLiteral("saveMesh"));
			if (!saveAction)
			{
				return EXIT_FAILURE;
			}
			saveAction->trigger();
			ok &= expect(asked == 1 && readBytes(path) == onDisk, "Save over an ignored change asks and can be cancelled");
			editor.sourceConflictChoice = [&](const QString &) { return ++asked, 2; };
			saveAction->trigger();
			ok &= expect(asked == 2 && readBytes(path) != onDisk, "Replace Changed File saves over the changed source");
			ModelDocument saved;
			ok &= expect(saved.load(path, &error) && saved.mesh().triangleCount == original.triangleCount,
						 "the replaced file holds the document");
			settle();
			ok &= expect(notice->isHidden() && !editor.sourceChangedOnDisk(), "the editor's own save produces no notice");

			// A further change shows again; Reload replaces the document.
			ok &= expect(writeSource(path, later), "write a second external change");
			settle();
			reload = editor.findChild<QPushButton *>(QStringLiteral("meshSourceReload"));
			ok &= expect(!notice->isHidden() && reload, "a new change shows the notice again");
			if (reload)
				reload->click();
			app.processEvents();
			ok &= expect(editor.document().mesh().triangleCount == later.triangleCount && !editor.document().isModified() &&
							 notice->isHidden(),
						 "Reload from Disk replaces the document");

			// Reload with unsaved edits asks; discarding reloads.
			ModelEdit edit;
			edit.kind = ModelEditKind::Transform;
			edit.selection.surface = 0;
			edit.selection.faces = {0};
			edit.translation = {0, 0, 4};
			ok &= expect(editor.applyEdit(edit, &error) && editor.document().isModified(), "make an unsaved edit");
			ok &= expect(writeSource(path, external), "write a third external change");
			settle();
			int reloadAsked = 0;
			editor.sourceReloadChoice = [&](const QString &) { return ++reloadAsked, 0; };
			ok &= expect(!editor.reloadSource(&error) && reloadAsked == 1 && editor.document().isModified(),
						 "cancelling the reload keeps the edits");
			editor.sourceReloadChoice = [&](const QString &) { return ++reloadAsked, 2; };
			ok &= expect(editor.reloadSource(&error) && reloadAsked == 2 && editor.document().mesh().triangleCount == external.triangleCount &&
							 !editor.document().isModified(),
						 "discarding the edits reloads the file");

			// A removed source says so and offers Save As.
			ok &= expect(QFile::remove(path), "remove the source");
			settle();
			ok &= expect(!notice->isHidden() && editor.findChild<QPushButton *>(QStringLiteral("meshSourceSaveAs")) &&
							 !editor.findChild<QPushButton *>(QStringLiteral("meshSourceReload")),
						 "a removed source shows the removed notice");
			ok &= expect(writeSource(path, original), "restore the source");
			editor.hide();
		}
	}
	QApplication::setLayoutDirection(Qt::LeftToRight);

	// Design: a save that expects the opened bytes refuses a changed file.
	const auto designPath = QDir(temporary.path()).filePath(QStringLiteral("prop.model.json"));
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("box");
	design.parts << part;
	const auto bytes = QJsonDocument(modelDesignJson(design)).toJson();
	ModelDesign loaded;
	QByteArray opened;
	ok &= expect(saveModelDesignBytes(designPath, bytes, false, &error) && loadModelDesign(designPath, &loaded, &error, &opened) &&
					 opened == QCryptographicHash::hash(bytes, QCryptographicHash::Sha256),
				 "a loaded design reports the SHA-256 it was read with");
	ok &= expect(saveModelDesignBytes(designPath, bytes, true, &error, {}, {}, false, opened), "a design saves over its unchanged file");
	QFile changed(designPath);
	ok &= expect(changed.open(QIODevice::WriteOnly) && changed.write(bytes + "\n") > 0, "change the design on disk");
	changed.close();
	ok &= expect(!saveModelDesignBytes(designPath, bytes, true, &error, {}, {}, false, opened) && !error.isEmpty() &&
					 readBytes(designPath) == bytes + "\n",
				 "a design save with a stale expected hash is refused without writing");
	StudioSettings::setOverrideFilePath(QString());
	if (!ok)
	{
		std::cerr << qPrintable(error) << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
