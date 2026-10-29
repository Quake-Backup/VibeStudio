// Export Map as OBJ and Export Selection as OBJ in the offscreen shell: the
// options window, its accessibility, Cancel, the written files, refusals and
// enablement, for NetRadiant Custom and TrenchBroom at 100% and NetRadiant
// Custom at 200% text with high contrast, an expanded translator and right to
// left.
#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "core/level_document.h"
#include "core/level_map_obj_export.h"
#include "core/studio_semantics.h"

#include <QAbstractButton>
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
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

QByteArray readAll(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// A Quake III map: a metal box, a caulk box and a crate box.
bool fixture(const QString& path)
{
	LevelMapDocument document;
	LevelMapCreateRequest request;
	request.game = QStringLiteral("quake3");
	request.starterRoom = false;
	QString error;
	if (!expect(createLevelMap(request, &document, &error)
				&& addLevelMapBoxBrush(&document, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("base_wall/metal"), nullptr, &error)
				&& addLevelMapBoxBrush(&document, {128, 0, 0, true}, {192, 64, 64, true}, QStringLiteral("common/caulk"), nullptr, &error)
				&& addLevelMapBoxBrush(&document, {256, 0, 0, true}, {320, 64, 64, true}, QStringLiteral("base_wall/crate"), nullptr, &error),
			"fixture", error)) {
		return false;
	}
	LevelDocumentSaveRequest save;
	save.path = path;
	save.overwrite = true;
	save.keepBackup = false;
	const auto report = writeLevelDocument(document, save);
	return expect(report.succeeded(), "fixture written", report.errors.join('\n'));
}

// Drives the OBJ export window once it is open: checks its controls, then
// types `path`, groups by material and exports, or cancels.
void driveDialog(QWidget* shell, const QString& path, bool accept, bool* seen)
{
	auto* poll = new QTimer(shell);
	poll->setInterval(20);
	QObject::connect(poll, &QTimer::timeout, shell, [poll, shell, path, accept, seen] {
		auto* dialog = shell->findChild<QDialog*>(QStringLiteral("levelObjExportDialog"));
		if (!dialog || !dialog->isVisible()) {
			return;
		}
		poll->stop();
		poll->deleteLater();
		*seen = true;
		auto* file = dialog->findChild<QLineEdit*>(QStringLiteral("levelObjExportPath"));
		auto* grouping = dialog->findChild<QComboBox*>(QStringLiteral("levelObjExportGrouping"));
		auto* ignored = dialog->findChild<QPlainTextEdit*>(QStringLiteral("levelObjExportIgnored"));
		auto* library = dialog->findChild<QCheckBox*>(QStringLiteral("levelObjExportLibrary"));
		auto* paths = dialog->findChild<QComboBox*>(QStringLiteral("levelObjExportMaterialPaths"));
		auto* exportButton = dialog->findChild<QPushButton*>(QStringLiteral("levelObjExportButton"));
		auto* cancel = dialog->findChild<QPushButton*>(QStringLiteral("levelObjExportCancel"));
		if (!expect(file && grouping && ignored && library && paths && exportButton && cancel, "OBJ window controls")) {
			dialog->reject();
			return;
		}
		bool accessible = !dialog->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(exportButton);
		for (auto* widget : dialog->findChildren<QWidget*>()) {
			const bool control = qobject_cast<QAbstractButton*>(widget) || qobject_cast<QComboBox*>(widget) || qobject_cast<QLineEdit*>(widget)
				|| qobject_cast<QPlainTextEdit*>(widget);
			if (control && widget->parentWidget() && widget->objectName().startsWith(QStringLiteral("levelObjExport"))) {
				accessible &= !widget->accessibleName().isEmpty() && widget->focusPolicy() != Qt::NoFocus;
			}
		}
		expect(accessible, "every option has an accessible name and takes focus");
		expect(ignored->toPlainText().contains(QStringLiteral("caulk")) && library->isChecked() && paths->isEnabled() && !file->text().isEmpty(),
			"tool materials ignored, library on, a suggested file", file->text());
		library->setChecked(false);
		expect(!paths->isEnabled(), "material paths follow the library box");
		library->setChecked(true);
		if (!accept) {
			cancel->click();
			return;
		}
		file->setText(QDir::toNativeSeparators(path));
		grouping->setCurrentIndex(grouping->findData(static_cast<int>(LevelMapObjGrouping::Material)));
		expect(exportButton->isEnabled(), "Export enabled with a file");
		exportButton->click();
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
	const auto mapPath = temp.filePath("q3.map");
	if (!fixture(mapPath)) {
		return 1;
	}
	for (const char* id : {"map.export-map-as-obj", "map.export-selection-as-obj"}) {
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(QString::fromLatin1(id), &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(), "palette entry",
			QString::fromLatin1(id));
	}
	// The shell takes a minute or more to open on a loaded machine.
	const QVector<QPair<const char*, int>> passes{{"netradiant-custom", 100}, {"trenchbroom", 100}, {"netradiant-custom", 200}};
	for (const auto& [profile, scale] : passes) {
		phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
		std::cout << "Map OBJ export UI " << qPrintable(phase) << std::endl;
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
			shell.openPathFromCommandLine(mapPath);
			const auto action = [&](const char* id) { return shell.findChild<QAction*>(QLatin1String(id)); };
			action("shell.mode.levels")->trigger();
			if (!expect(until([&] { return shell.levelDocument().sourcePath == QFileInfo(mapPath).absoluteFilePath(); }), "map open")) {
				return 1;
			}
			auto* exportMap = action("map.exportMapAsObj");
			auto* exportSelection = action("map.exportSelectionAsObj");
			if (!expect(exportMap && exportSelection, "commands registered")) {
				return 1;
			}
			expect(exportMap->shortcuts().isEmpty() && exportSelection->shortcuts().isEmpty(), "no profile binds the OBJ exports, as upstream");
			expect(exportMap->isEnabled() && !exportSelection->isEnabled(), "enablement without a selection");
			expect(!exportMap->toolTip().isEmpty() && !exportSelection->statusTip().isEmpty(), "tool tips");
			auto* fileMenu = shell.findChild<QMenu*>(QStringLiteral("menu-file"));
			expect(fileMenu && fileMenu->actions().contains(exportMap) && fileMenu->actions().contains(exportSelection), "File menu entries");
			auto* create = shell.findChild<QToolButton*>(QStringLiteral("levelCreateTools"));
			auto* createMenu = create && create->defaultAction() ? create->defaultAction()->menu() : nullptr;
			expect(createMenu && createMenu->actions().contains(exportMap) && createMenu->actions().contains(exportSelection), "Create menu entries");
			for (const char* id : {"map.exportMapAsObj", "map.exportSelectionAsObj"}) {
				auto* tile = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-%1").arg(QLatin1String(id)));
				expect(tile && !tile->accessibleName().isEmpty() && tile->focusPolicy() != Qt::NoFocus, "Tools tab tile", QLatin1String(id));
			}
			std::cout << "  shell ready after " << clock.elapsed() << " ms" << std::endl;
			const auto original = serializeLevelMap(shell.levelDocument()).bytes;
			const auto revision = shell.levelDocument().revision;

			// Cancel writes nothing.
			const auto objPath = temp.filePath(QStringLiteral("out-%1-%2/map.obj").arg(QLatin1String(profile)).arg(scale));
			bool seen = false;
			driveDialog(&shell, objPath, false, &seen);
			exportMap->trigger();
			expect(until([&] { return seen; }) && !QFileInfo::exists(objPath), "Cancel writes nothing");

			// Export Map as OBJ, grouped by material, leaves caulk out.
			seen = false;
			driveDialog(&shell, objPath, true, &seen);
			exportMap->trigger();
			expect(until([&] { return seen && QFileInfo::exists(objPath); }), "OBJ written through the window");
			const auto obj = readAll(objPath), mtl = readAll(QFileInfo(objPath).absoluteDir().filePath(QStringLiteral("map.mtl")));
			expect(obj.contains("mtllib map.mtl\n") && obj.contains("\no base_wall/metal\n") && obj.contains("\no base_wall/crate\n") && !obj.contains("caulk")
					&& mtl.contains("newmtl base_wall/metal\n"),
				"material objects without caulk", QString::fromUtf8(obj.left(300)));
			expect(shell.statusBar()->currentMessage().contains(QStringLiteral("map.obj")), "status names the file", shell.statusBar()->currentMessage());
			expect(serializeLevelMap(shell.levelDocument()).bytes == original && shell.levelDocument().revision == revision
					&& shell.levelDocument().sourcePath == QFileInfo(mapPath).absoluteFilePath(),
				"the map is unchanged");

			// View filters do not apply: with world brushes filtered out of the
			// views the export still writes them, as `map export-obj` does.
			if (QAction* world = action("map.filterWorld"); expect(world && world->isCheckable(), "World Brushes filter command")) {
				const bool shown = world->isChecked();
				world->trigger();
				expect(world->isChecked() != shown, "World Brushes filter toggled");
				const auto filtered = temp.filePath(QStringLiteral("filtered-%1-%2.obj").arg(QLatin1String(profile)).arg(scale));
				QString error;
				expect(shell.exportLevelMapObjFile(filtered, levelMapObjDefaultOptions(), &error) && readAll(filtered).contains("base_wall/metal"),
					"filtered world brushes are still exported", error);
				world->trigger();
				expect(world->isChecked() == shown, "World Brushes filter restored");
			}

			// Export Selection as OBJ follows the selection.
			action("map.selectAll")->trigger();
			expect(until([&] { return exportSelection->isEnabled(); }), "a selection enables the selection export");
			auto options = levelMapObjDefaultOptions();
			options.selectionOnly = true;
			options.objects = false;
			const auto selectionPath = temp.filePath(QStringLiteral("selection-%1-%2.obj").arg(QLatin1String(profile)).arg(scale));
			QString error;
			expect(shell.exportLevelMapObjFile(selectionPath, options, &error) && readAll(selectionPath).contains("\ng entity"), "selection written as groups", error);
			expect(!shell.exportLevelMapObjFile(temp.filePath(QStringLiteral("wrong.txt")), options, &error) && !error.isEmpty()
					&& shell.statusBar()->currentMessage() == error,
				"a file without .obj is refused in the status bar", error);
			for (auto* view : shell.findChildren<MapViewport*>()) {
				view->setSelectionSet({});
				Q_EMIT view->selectionSetChanged({});
				break;
			}
			expect(until([&] { return !exportSelection->isEnabled(); }), "no selection disables the selection export");
			expect(!shell.exportLevelMapObjFile(selectionPath, options, &error) && !error.isEmpty(), "selection export refused without a selection");
			std::cout << "  pass done after " << clock.elapsed() << " ms" << std::endl;
		}
		if (scale == 200) {
			app.removeTranslator(&expanded);
			app.setLayoutDirection(Qt::LeftToRight);
		}
	}
	std::cout << (ok ? "Map OBJ export UI smoke test passed." : "Map OBJ export UI smoke test failed.") << std::endl;
	return ok ? 0 : 1;
}
