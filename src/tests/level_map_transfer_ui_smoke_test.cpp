// Import Map, Export Selection as Map, Export Map and the Scene panel's Omit
// from export box in the offscreen shell: five editor profiles at 100%, and two
// of them at 200% text with high contrast, an expanded translator and right to
// left.
#include "app/application_shell.h"
#include "app/level_scene_panel.h"
#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "core/level_document.h"
#include "core/level_scene.h"
#include "core/studio_semantics.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QTreeWidget>
#include <functional>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
QString phase;
bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << qPrintable(phase) << ": " << label << ": " << qPrintable(detail) << std::endl;
	}
	return value;
}
bool until(const std::function<bool()>& ready, int milliseconds = 30000)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < milliseconds) {
		QEventLoop loop;
		QTimer::singleShot(15, &loop, &QEventLoop::quit);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}
class Expanded final : public QTranslator {
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, '~'));
	}
};

int worldspawnId(const LevelMapDocument& document)
{
	for (const auto& entity : document.entities) {
		if (entity.className == QLatin1String("worldspawn")) {
			return entity.id;
		}
	}
	return -1;
}

bool writeMap(const LevelMapDocument& document, const QString& path)
{
	LevelDocumentSaveRequest request;
	request.path = path;
	request.overwrite = true;
	request.keepBackup = false;
	const auto report = writeLevelDocument(document, request);
	return expect(report.succeeded(), "fixture written", report.errors.join('\n'));
}

// The open map: two world brushes, one of them in a layer called Notes.
bool destination(const QString& path)
{
	LevelMapDocument document;
	LevelMapCreateRequest request;
	request.game = QStringLiteral("quake");
	request.starterRoom = false;
	QString error, layer;
	return expect(createLevelMap(request, &document, &error)
		&& setLevelMapEntityProperty(&document, worldspawnId(document), QStringLiteral("message"), QStringLiteral("Dest"), &error)
		&& addLevelMapBoxBrush(&document, {-256, -256, 0, true}, {-192, -192, 64, true}, QStringLiteral("WALL"), nullptr, &error)
		&& addLevelMapBoxBrush(&document, {256, 256, 0, true}, {320, 320, 64, true}, QStringLiteral("WALL"), nullptr, &error)
		&& addLevelMapEntity(&document, QStringLiteral("info_notnull"), {0, 0, 32, true}, {{QStringLiteral("targetname"), QStringLiteral("door"), 0}}, nullptr, &error)
		&& createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("Notes"), {}, &layer, &error)
		&& assignLevelSceneObjects(&document, layer, {QStringLiteral("brush:1")}, &error), "destination fixture", error) && writeMap(document, path);
}

// The map to import: a world brush and a door its button targets.
bool other(const QString& path)
{
	LevelMapDocument document;
	LevelMapCreateRequest request;
	request.game = QStringLiteral("quake");
	request.starterRoom = false;
	QString error;
	return expect(createLevelMap(request, &document, &error)
		&& addLevelMapBoxBrush(&document, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("WALL"), nullptr, &error)
		&& addLevelMapBoxBrush(&document, {128, 0, 0, true}, {192, 64, 64, true}, QStringLiteral("WALL"), nullptr, &error)
		&& setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 1}}, &error)
		&& tieLevelMapSelectionToEntity(&document, QStringLiteral("func_door"), {{QStringLiteral("targetname"), QStringLiteral("door"), 0}}, nullptr, &error)
		&& addLevelMapEntity(&document, QStringLiteral("trigger_relay"), {32, 32, 96, true}, {{QStringLiteral("target"), QStringLiteral("door"), 0}}, nullptr, &error),
		"import fixture", error) && writeMap(document, path);
}

QTreeWidgetItem* nodeItem(QTreeWidget* tree, const QString& id)
{
	for (QTreeWidgetItemIterator it(tree); *it; ++it) {
		if ((*it)->data(0, Qt::UserRole).toString() == id) {
			return *it;
		}
	}
	return nullptr;
}

QString nodeNamed(const LevelSceneState& scene, const QString& name)
{
	for (const auto& node : scene.nodes) {
		if (node.name == name) {
			return node.id;
		}
	}
	return {};
}

// Clicks through the Import Map window once it has read the map.
void driveImport(QWidget* shell, bool accept, bool* seen)
{
	auto* poll = new QTimer(shell);
	poll->setInterval(20);
	QObject::connect(poll, &QTimer::timeout, shell, [poll, shell, accept, seen] {
		auto* dialog = shell->findChild<QDialog*>(QStringLiteral("levelMapImportDialog"));
		if (!dialog || !dialog->isVisible()) {
			return;
		}
		auto* import = dialog->findChild<QPushButton*>(QStringLiteral("levelMapImportButton"));
		auto* cancel = dialog->findChild<QPushButton*>(QStringLiteral("levelMapImportCancel"));
		auto* summary = dialog->findChild<QLabel*>(QStringLiteral("levelMapImportSummary"));
		auto* rename = dialog->findChild<QCheckBox*>(QStringLiteral("levelMapImportRename"));
		if (!import || !cancel || !summary || !rename) {
			return;
		}
		if (accept && !import->isEnabled()) {
			return;
		}
		poll->stop();
		poll->deleteLater();
		*seen = true;
		expect(!dialog->accessibleName().isEmpty() && !import->accessibleName().isEmpty() && !cancel->accessibleName().isEmpty()
			&& rename->isChecked() && !rename->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(import), "accessible import window");
		if (accept) {
			expect(summary->accessibleDescription() == summary->text() && summary->text().contains(QLatin1Char('2')), "preview counts shown", summary->text());
			import->click();
		} else {
			cancel->click();
		}
	});
	poll->start();
}
} // namespace

int main(int argc, char** argv)
{
	// Owned offscreen Qt widgets and semantic events only.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	const auto destPath = temp.filePath("dest.map"), otherPath = temp.filePath("other.map");
	if (!destination(destPath) || !other(otherPath)) {
		return 1;
	}
	for (const char* id : {"map.import-map", "map.export-selection-as-map", "map.export-map"}) {
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(QString::fromLatin1(id), &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(), "palette entry",
			QString::fromLatin1(id));
	}
	// Every profile at 100%, and the 200% high-contrast, expanded right-to-left
	// pass for two of them: the shell takes a minute to open on a loaded machine.
	const QVector<QPair<const char*, int>> passes{{"netradiant-custom", 100}, {"trenchbroom", 100}, {"gtkradiant-1-6", 100}, {"quark", 100},
		{"vibestudio-default", 100}, {"netradiant-custom", 200}, {"trenchbroom", 200}};
	for (const auto& [profile, scale] : passes) {
		{
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Map transfer UI " << qPrintable(phase) << std::endl;
			settings.setSelectedEditorProfileId(profile);
			settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
			settings.sync();
			applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, scale));
			Expanded expanded;
			if (scale == 200) {
				app.installTranslator(&expanded);
				app.setLayoutDirection(Qt::RightToLeft);
			}
			QElapsedTimer clock;
			clock.start();
			{
				ApplicationShell shell;
				shell.resize(1600, 1000);
				shell.show();
				shell.openPathFromCommandLine(destPath);
				const auto action = [&](const char* id) { return shell.findChild<QAction*>(QLatin1String(id)); };
				action("shell.mode.levels")->trigger();
				if (!expect(until([&] { return shell.levelDocument().sourcePath == QFileInfo(destPath).absoluteFilePath(); }), "map open")) {
					return 1;
				}
				auto* importMap = action("map.importMap");
				auto* exportSelection = action("map.exportSelectionAsMap");
				auto* exportMap = action("map.exportMap");
				if (!expect(importMap && exportSelection && exportMap, "commands registered")) {
					return 1;
				}
				expect(importMap->shortcuts().isEmpty() && exportSelection->shortcuts().isEmpty() && exportMap->shortcuts().isEmpty(),
					"no profile binds the file commands, as upstream");
				expect(importMap->isEnabled() && exportMap->isEnabled() && !exportSelection->isEnabled(), "enablement without a selection");
				expect(!importMap->toolTip().isEmpty() && !exportMap->statusTip().isEmpty(), "tool tips");
				auto* fileMenu = shell.findChild<QMenu*>(QStringLiteral("menu-file"));
				expect(fileMenu && fileMenu->actions().contains(importMap) && fileMenu->actions().contains(exportMap)
					&& fileMenu->actions().contains(exportSelection), "File menu entries");
				for (const char* id : {"map.importMap", "map.exportSelectionAsMap", "map.exportMap"}) {
					auto* tile = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-%1").arg(QLatin1String(id)));
					expect(tile && !tile->accessibleName().isEmpty() && tile->focusPolicy() != Qt::NoFocus, "Tools tab tile", QLatin1String(id));
				}
				std::cout << "  shell ready after " << clock.elapsed() << " ms" << std::endl;
				const auto original = serializeLevelMap(shell.levelDocument()).bytes;

				// Cancelling the import window changes nothing.
				bool seen = false;
				driveImport(&shell, false, &seen);
				QString error;
				expect(!shell.importLevelMapFile(otherPath, &error) && seen && serializeLevelMap(shell.levelDocument()).bytes == original, "cancelled import", error);

				// Import: one undo step that renames the clashing door.
				seen = false;
				driveImport(&shell, true, &seen);
				const auto entities = shell.levelDocument().entities.size();
				expect(shell.importLevelMapFile(otherPath, &error) && seen, "import", error);
				const auto& imported = shell.levelDocument();
				expect(imported.entities.size() == entities + 2 && imported.brushes.size() == 4 && imported.selection.size() == 3, "imported and selected");
				bool renamed = false;
				for (const auto& entity : imported.entities) {
					for (const auto& property : entity.properties) {
						renamed |= entity.className == QLatin1String("func_door") && property.key == QLatin1String("targetname") && property.value == QLatin1String("door1");
					}
				}
				expect(renamed && shell.statusBar()->currentMessage().contains(QStringLiteral("other.map")), "door renamed and reported",
					shell.statusBar()->currentMessage());
				const auto afterImport = serializeLevelMap(shell.levelDocument()).bytes;
				action("map.undo")->trigger();
				expect(until([&] { return serializeLevelMap(shell.levelDocument()).bytes == original; }), "undo removes the import");
				action("map.redo")->trigger();
				expect(until([&] { return serializeLevelMap(shell.levelDocument()).bytes == afterImport; }), "redo imports again");

				// Export Selection as Map follows the selection.
				expect(until([&] { return exportSelection->isEnabled(); }), "selection enables export");
				const auto selectionPath = temp.filePath(QStringLiteral("selection-%1-%2.map").arg(QLatin1String(profile)).arg(scale));
				expect(shell.exportLevelMapSelectionFile(selectionPath, true, &error) && QFileInfo::exists(selectionPath)
					&& shell.levelDocument().sourcePath == QFileInfo(destPath).absoluteFilePath(), "selection exported", error);
				LevelMapDocument exported;
				expect(loadLevelMap({selectionPath, {}, {}}, &exported, &error) && exported.brushes.size() == 2 && exported.entities.size() == 3, "exported selection reads back",
					error);
				for (auto* view : shell.findChildren<MapViewport*>()) {
					view->setSelectionSet({});
					Q_EMIT view->selectionSetChanged({});
					break;
				}
				expect(until([&] { return !exportSelection->isEnabled(); }), "no selection disables export");

				// Omit from export in the Scene panel, then Export Map.
				auto* panel = shell.findChild<LevelScenePanel*>();
				auto* tree = panel ? panel->findChild<QTreeWidget*>(QStringLiteral("levelSceneTree")) : nullptr;
				auto* omit = panel ? panel->findChild<QCheckBox*>(QStringLiteral("levelSceneOmitFromExport")) : nullptr;
				const auto notes = nodeNamed(shell.levelDocument().scene, QStringLiteral("Notes"));
				if (expect(tree && omit && !notes.isEmpty(), "Scene panel omit box")) {
					tree->setCurrentItem(nodeItem(tree, QString()));
					expect(!omit->isEnabled(), "Default layer cannot be omitted");
					tree->setCurrentItem(nodeItem(tree, notes));
					expect(omit->isEnabled() && !omit->isChecked() && !omit->accessibleName().isEmpty() && !omit->toolTip().isEmpty(), "omit box for a layer");
					const auto depth = shell.levelDocument().undoStack.size();
					const auto kindBefore = nodeItem(tree, notes)->text(1);
					omit->click();
					expect(levelSceneOmittedFromExport(*levelSceneNode(shell.levelDocument().scene, notes)) && shell.levelDocument().undoStack.size() == depth + 1,
						"omit flag is one undo step");
					auto* item = nodeItem(tree, notes);
					expect(item && omit->isChecked() && item->text(1) != kindBefore && item->text(1).startsWith(kindBefore), "tree names the omitted layer",
						item ? item->text(1) : QString());
				}
				const auto exportPath = temp.filePath(QStringLiteral("export-%1-%2.map").arg(QLatin1String(profile)).arg(scale));
				const auto revision = shell.levelDocument().revision;
				expect(shell.exportLevelMapCopyFile(exportPath, std::nullopt, &error) && shell.levelDocument().sourcePath == QFileInfo(destPath).absoluteFilePath()
					&& shell.levelDocument().revision == revision, "map exported without taking its path", error);
				LevelMapDocument copy;
				expect(loadLevelMap({exportPath, {}, {}}, &copy, &error) && copy.brushes.size() == shell.levelDocument().brushes.size() - 1
					&& nodeNamed(copy.scene, QStringLiteral("Notes")).isEmpty(), "omitted layer left out of the copy", error);
				expect(!shell.exportLevelMapCopyFile(destPath, std::nullopt, &error) && !error.isEmpty(), "export refuses the open map's file");
				std::cout << "  pass done after " << clock.elapsed() << " ms" << std::endl;
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}
	std::cout << (ok ? "Map transfer UI smoke test passed." : "Map transfer UI smoke test failed.") << std::endl;
	return ok ? 0 : 1;
}
