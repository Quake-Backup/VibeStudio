// Map Info and the Map tab inspector in the offscreen shell: the Map Info
// command (M in the NetRadiant Custom profile) at the Statistics section's
// sortable class table, Show Map (Ctrl+1 in the TrenchBroom profile, with
// Ctrl+1 still choosing Workspace outside Levels), the layer summary leading
// to the Outliner, Map Properties' soft bounds and the Mods list, each change
// one undo step, with accessible names, for NetRadiant Custom and
// TrenchBroom at 100% and TrenchBroom at 200% text with high contrast, an
// expanded translator and right to left.
#include "app/application_shell.h"
#include "app/level_map_inspector.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_sidebar.h"
#include "app/studio_theme.h"
#include "core/level_document.h"
#include "core/level_map_inspector.h"
#include "core/level_scene.h"
#include "core/studio_semantics.h"
#include "tests/level_map_info_test_fixture.h"

#include <QAbstractButton>
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
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
void settle()
{
	QEventLoop loop;
	QTimer::singleShot(60, &loop, &QEventLoop::quit);
	loop.exec();
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

QString worldValue(const LevelMapDocument& document, const char* key, bool* present = nullptr)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.className == QLatin1String("worldspawn")) {
			for (const LevelMapProperty& property : entity.properties) {
				if (property.key == QLatin1String(key)) {
					if (present) {
						*present = true;
					}
					return property.value;
				}
			}
		}
	}
	if (present) {
		*present = false;
	}
	return {};
}

// The fixture with the door in a layer of its own.
bool writeFixture(const QString& path)
{
	LevelMapDocument document;
	QString error;
	QString layer;
	if (!expect(loadLevelMapBytes({QStringLiteral("fixture.map"), {}, {}}, tests::mapInfoFixture(), &document, &error), "fixture loads", error)) {
		return false;
	}
	int door = -1;
	for (const LevelMapEntity& entity : document.entities) {
		door = entity.className == QLatin1String("func_door") ? entity.id : door;
	}
	if (!expect(createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("Details"), {}, &layer, &error)
				&& assignLevelSceneObjects(&document, layer, {QStringLiteral("entity:%1").arg(door)}, &error),
			"fixture layer", error)) {
		return false;
	}
	LevelDocumentSaveRequest save;
	save.path = path;
	save.overwrite = true;
	save.keepBackup = false;
	const auto report = writeLevelDocument(document, save);
	return expect(report.succeeded(), "fixture written", report.errors.join('\n'));
}

bool accessible(QWidget* widget)
{
	return widget && !widget->accessibleName().isEmpty() && widget->focusPolicy() != Qt::NoFocus && QAccessible::queryAccessibleInterface(widget);
}
} // namespace

int main(int argc, char** argv)
{
	// Owned offscreen Qt widgets and Qt Test key presses only.
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
	const auto mapPath = QFileInfo(temp.filePath("info.map")).absoluteFilePath();
	if (!writeFixture(mapPath)) {
		return 1;
	}
	for (const char* id : {"map.map-info", "map.sidebar.map", "map.statistics"}) {
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(QString::fromLatin1(id), &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(), "palette entry",
			QString::fromLatin1(id));
	}
	struct Pass {
		const char* profile;
		int scale;
	};
	// The shell takes a minute or more to open on a loaded machine.
	for (const Pass& pass : {Pass {"netradiant-custom", 100}, Pass {"trenchbroom", 100}, Pass {"trenchbroom", 200}}) {
		phase = QStringLiteral("%1 %2%").arg(QLatin1String(pass.profile)).arg(pass.scale);
		std::cout << "Map Info UI " << qPrintable(phase) << std::endl;
		const bool trenchBroom = QLatin1String(pass.profile) == QLatin1String("trenchbroom");
		settings.setSelectedEditorProfileId(QString::fromLatin1(pass.profile));
		settings.setTextScalePercent(pass.scale);
		settings.setTheme(pass.scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
		settings.sync();
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, pass.scale));
		Expanded expanded;
		if (pass.scale == 200) {
			app.installTranslator(&expanded);
			app.setLayoutDirection(Qt::RightToLeft);
		}
		QElapsedTimer clock;
		clock.start();
		{
			ApplicationShell shell;
			shell.resize(pass.scale == 100 ? 1650 : 2400, pass.scale == 100 ? 1000 : 1500);
			shell.show();
			shell.openPathFromCommandLine(mapPath);
			const auto action = [&](const char* id) { return shell.findChild<QAction*>(QLatin1String(id)); };
			action("shell.mode.levels")->trigger();
			if (!expect(until([&] { return shell.levelDocument().sourcePath == mapPath && shell.findChildren<MapViewport*>().size() >= 1; }), "map open")) {
				return 1;
			}
			std::cout << "  shell ready after " << clock.elapsed() << " ms" << std::endl;
			auto* stack = shell.findChild<QStackedWidget*>(QStringLiteral("modeStack"));
			const auto mode = [&] { return stack ? static_cast<StudioMode>(stack->currentIndex()) : StudioMode::Workspace; };
			QAction* info = action("map.mapInfo");
			QAction* showMap = action("map.sidebar.map");
			QAction* undo = action("map.undo");
			QAction* redo = action("map.redo");
			auto* table = shell.findChild<LevelMapClassTable*>(QStringLiteral("levelMapEntityClasses"));
			auto* layers = shell.findChild<LevelMapLayerList*>(QStringLiteral("levelMapLayers"));
			auto* statistics = shell.findChild<QListWidget*>(QStringLiteral("levelMapStatistics"));
			auto* mapPage = shell.findChild<SidebarPage*>(QStringLiteral("levelSidebarPage-map"));
			if (!expect(info && showMap && undo && redo && table && layers && statistics && mapPage && stack, "commands and Map tab widgets")) {
				return 1;
			}
			StudioSidebar* holder = shell.levelSidebar(true)->indexOfPage(QStringLiteral("map")) >= 0 ? shell.levelSidebar(true) : shell.levelSidebar(false);
			const QKeySequence m(QStringLiteral("M"));
			const QKeySequence ctrl1(QStringLiteral("Ctrl+1"));
			expect(trenchBroom ? info->shortcuts().isEmpty() : info->shortcuts().contains(m), "NetRadiant Custom binds M to Map Info",
				QKeySequence::listToString(info->shortcuts()));
			expect(trenchBroom ? showMap->shortcuts().contains(ctrl1) : !showMap->shortcuts().contains(ctrl1), "TrenchBroom binds Ctrl+1 to Show Map",
				QKeySequence::listToString(showMap->shortcuts()));
			expect(action("shell.mode.workspace")->shortcuts().contains(ctrl1), "Workspace keeps Ctrl+1");
			expect(!info->text().isEmpty() && !info->toolTip().isEmpty() && !info->statusTip().isEmpty(), "Map Info is labelled");
			auto* tile = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-map.mapInfo"));
			expect(tile && !tile->accessibleName().isEmpty() && tile->focusPolicy() != Qt::NoFocus, "Tools tab tile");

			// Map Info: the Statistics section at its class table.
			phase = QStringLiteral("%1 %2%: Map Info").arg(QLatin1String(pass.profile)).arg(pass.scale);
			SidebarSection* statisticsSection = mapPage->section(QStringLiteral("map.statistics"));
			if (statisticsSection) {
				statisticsSection->setExpanded(false);
			}
			shell.activateWindow();
			until([&] { return QApplication::activeWindow() == &shell; });
			info->trigger();
			settle();
			expect(holder->currentPageId() == QLatin1String("map") && !holder->isFolded() && statisticsSection && statisticsSection->isExpanded(),
				"Map tab open at Statistics", holder->currentPageId());
			expect(QApplication::focusWidget() == table && table->currentItem() && table->topLevelItemCount() == 9 && !table->isColumnHidden(2),
				"keyboard in the class table", QApplication::focusWidget() ? QApplication::focusWidget()->objectName() : QStringLiteral("none"));
			expect(accessible(table) && table->isSortingEnabled() && table->headerItem()->text(1).contains(QStringLiteral("Count")), "accessible, sortable table");
			if (pass.scale == 100) {
				QStringList lines;
				for (int row = 0; row < statistics->count(); ++row) {
					lines << statistics->item(row)->text();
				}
				expect(lines.contains(QStringLiteral("In-game entities: 4")) && lines.contains(QStringLiteral("Group entities: 4 (2 in game)")), "statistics totals",
					lines.join('\n'));
			}
			table->sortByColumn(LevelMapClassTable::CountColumn, Qt::DescendingOrder);
			expect(table->topLevelItem(0)->data(0, Qt::UserRole).toString() == QStringLiteral("light") && table->topLevelItem(0)->text(1) == QStringLiteral("2")
					&& table->topLevelItem(0)->text(2) == QStringLiteral("0"),
				"sorted by count, lights first");
			table->sortByColumn(LevelMapClassTable::ClassColumn, Qt::AscendingOrder);
			expect(table->topLevelItem(0)->data(0, Qt::UserRole).toString() == QStringLiteral("func_detail"), "sorted by class");
			QTreeWidgetItem* start = nullptr;
			for (int row = 0; row < table->topLevelItemCount(); ++row) {
				if (table->topLevelItem(row)->data(0, Qt::UserRole).toString() == QStringLiteral("info_player_start")) {
					start = table->topLevelItem(row);
				}
			}
			if (expect(start != nullptr, "player start row")) {
				expect(!start->data(0, Qt::AccessibleTextRole).toString().isEmpty(), "rows are spoken");
				table->setCurrentItem(start);
				Q_EMIT table->itemActivated(start, 0);
				settle();
				auto* filter = shell.findChild<QLineEdit*>(QStringLiteral("levelObjectFilter"));
				expect(filter && filter->text() == QStringLiteral("kind=entity class=info_player_start"), "Enter lists the class in the Outliner",
					filter ? filter->text() : QString());
				if (filter) {
					filter->clear();
				}
			}

			// The layer summary leads to the Outliner's layer tools.
			phase = QStringLiteral("%1 %2%: layers").arg(QLatin1String(pass.profile)).arg(pass.scale);
			showMap->trigger();
			settle();
			QString detailsId;
			for (const LevelSceneNode& node : shell.levelDocument().scene.nodes) {
				detailsId = node.name == QLatin1String("Details") ? node.id : detailsId;
			}
			expect(accessible(layers) && layers->count() == 2 && layers->item(1)->text().contains(QStringLiteral("Details"))
					&& layers->item(0)->data(Qt::UserRole).toString().isEmpty() && layers->item(1)->data(Qt::UserRole).toString() == detailsId,
				"Default and Details", layers->count() > 1 ? layers->item(1)->text() : QString());
			if (layers->count() == 2) {
				Q_EMIT layers->itemActivated(layers->item(1));
				settle();
				auto* tree = shell.findChild<QTreeWidget*>(QStringLiteral("levelSceneTree"));
				expect(tree && tree->currentItem() && tree->currentItem()->data(0, Qt::UserRole).toString() == detailsId && tree->isVisible(),
					"the layer is chosen in the Outliner");
			}

			// Map Properties: TrenchBroom's soft bounds, one undo step each.
			phase = QStringLiteral("%1 %2%: soft bounds").arg(QLatin1String(pass.profile)).arg(pass.scale);
			showMap->trigger();
			settle();
			auto* none = shell.findChild<QRadioButton*>(QStringLiteral("levelMapSoftBoundsNone"));
			auto* game = shell.findChild<QRadioButton*>(QStringLiteral("levelMapSoftBoundsGame"));
			auto* custom = shell.findChild<QRadioButton*>(QStringLiteral("levelMapSoftBoundsCustom"));
			auto* minimum = shell.findChild<QLineEdit*>(QStringLiteral("levelMapSoftBoundsMin"));
			auto* maximum = shell.findChild<QLineEdit*>(QStringLiteral("levelMapSoftBoundsMax"));
			auto* status = shell.findChild<QLabel*>(QStringLiteral("levelMapSoftBoundsStatus"));
			auto* gameValue = shell.findChild<QLabel*>(QStringLiteral("levelMapSoftBoundsGameValue"));
			if (expect(none && game && custom && minimum && maximum && status && gameValue, "soft bounds controls")) {
				expect(accessible(none) && accessible(game) && accessible(custom) && accessible(minimum) && accessible(maximum) && !status->accessibleName().isEmpty(),
					"soft bounds controls are accessible");
				expect(game->isChecked() && !minimum->isEnabled() && gameValue->text().contains(QStringLiteral("4096")) && status->text().contains(QLatin1Char('2'))
						&& custom->isVisible(),
					"the game's bounds, two objects outside", status->text());
				const QByteArray original = serializeLevelMap(shell.levelDocument()).bytes;
				auto revision = shell.levelDocument().revision;
				minimum->setText(QStringLiteral("-2048"));
				maximum->setText(QStringLiteral("2048 2048 1024"));
				custom->click();
				settle();
				expect(worldValue(shell.levelDocument(), kLevelMapSoftBoundsKey) == QStringLiteral("-2048 -2048 -2048 2048 2048 1024") && custom->isChecked()
						&& minimum->isEnabled() && shell.levelDocument().revision != revision,
					"Custom commits the typed corners", worldValue(shell.levelDocument(), kLevelMapSoftBoundsKey));
				const QByteArray customBytes = serializeLevelMap(shell.levelDocument()).bytes;
				revision = shell.levelDocument().revision;
				minimum->setText(QStringLiteral("4096"));
				Q_EMIT minimum->editingFinished();
				settle();
				expect(shell.levelDocument().revision == revision && !status->text().isEmpty(), "a minimum above its maximum is refused in place", status->text());
				undo->trigger();
				settle();
				bool present = true;
				worldValue(shell.levelDocument(), kLevelMapSoftBoundsKey, &present);
				expect(!present && game->isChecked() && serializeLevelMap(shell.levelDocument()).bytes == original, "undo restores the game's bounds");
				redo->trigger();
				settle();
				expect(serializeLevelMap(shell.levelDocument()).bytes == customBytes && custom->isChecked()
						&& minimum->text() == QStringLiteral("-2048"),
					"redo", minimum->text());
				none->click();
				settle();
				expect(worldValue(shell.levelDocument(), kLevelMapSoftBoundsKey) == QStringLiteral("none") && !minimum->isEnabled(), "No soft bounds writes none");
				game->click();
				settle();
				worldValue(shell.levelDocument(), kLevelMapSoftBoundsKey, &present);
				expect(!present && game->isChecked(), "the game's bounds take the key away");
				for (int step = 0; step < 3; ++step) {
					undo->trigger();
					settle();
				}
				expect(serializeLevelMap(shell.levelDocument()).bytes == original, "three undos restore the map");
			}

			// Mods, one undo step each.
			phase = QStringLiteral("%1 %2%: mods").arg(QLatin1String(pass.profile)).arg(pass.scale);
			if (SidebarSection* mods = mapPage->section(QStringLiteral("map.mods"))) {
				mods->setExpanded(true);
				settle();
			}
			auto* name = shell.findChild<QComboBox*>(QStringLiteral("levelMapModName"));
			auto* enable = shell.findChild<QPushButton*>(QStringLiteral("levelMapModEnable"));
			auto* list = shell.findChild<QListWidget*>(QStringLiteral("levelMapModList"));
			auto* remove = shell.findChild<QToolButton*>(QStringLiteral("levelMapModRemove"));
			auto* up = shell.findChild<QToolButton*>(QStringLiteral("levelMapModUp"));
			auto* down = shell.findChild<QToolButton*>(QStringLiteral("levelMapModDown"));
			auto* modsStatus = shell.findChild<QLabel*>(QStringLiteral("levelMapModsStatus"));
			if (expect(name && enable && list && remove && up && down && modsStatus, "mods controls")) {
				expect(accessible(name) && accessible(enable) && accessible(list) && accessible(remove) && accessible(up) && accessible(down) && list->isVisible(),
					"mods controls are accessible");
				const QByteArray original = serializeLevelMap(shell.levelDocument()).bytes;
				const auto mods = [&] { return levelMapEnabledMods(shell.levelDocument()); };
				name->setEditText(QStringLiteral("hipnotic"));
				enable->click();
				settle();
				name->setEditText(QStringLiteral("ctf"));
				enable->click();
				settle();
				expect(mods() == QStringList({QStringLiteral("hipnotic"), QStringLiteral("ctf")}) && list->count() == 2 && name->currentText().isEmpty(),
					"two mods enabled in order", mods().join(' '));
				list->setCurrentRow(1);
				expect(up->isEnabled() && !down->isEnabled(), "only up for the last mod");
				up->click();
				settle();
				expect(mods() == QStringList({QStringLiteral("ctf"), QStringLiteral("hipnotic")}) && list->currentRow() == 0, "moved up and still chosen",
					mods().join(' '));
				const auto revision = shell.levelDocument().revision;
				name->setEditText(QStringLiteral("bad;name"));
				enable->click();
				settle();
				expect(shell.levelDocument().revision == revision && !modsStatus->text().isEmpty(), "a bad name is refused in place", modsStatus->text());
				name->setEditText(QStringLiteral("CTF"));
				enable->click();
				settle();
				expect(shell.levelDocument().revision == revision, "an enabled mod is not added twice");
				list->setCurrentRow(0);
				remove->click();
				settle();
				expect(mods() == QStringList({QStringLiteral("hipnotic")}), "removed", mods().join(' '));
				undo->trigger();
				settle();
				expect(mods() == QStringList({QStringLiteral("ctf"), QStringLiteral("hipnotic")}), "undo puts the mod back", mods().join(' '));
				for (int step = 0; step < 3; ++step) {
					undo->trigger();
					settle();
				}
				expect(serializeLevelMap(shell.levelDocument()).bytes == original, "every mods step undoes exactly");
			}

			// The keys: NetRadiant Custom's M and TrenchBroom's Ctrl+1 from a
			// view; outside Levels Ctrl+1 still chooses Workspace.
			phase = QStringLiteral("%1 %2%: keys").arg(QLatin1String(pass.profile)).arg(pass.scale);
			// A plan view, or the camera where the profile shows only that.
			QWidget* view = nullptr;
			for (auto* pane : shell.findChildren<MapViewport*>()) {
				view = pane->isVisible() ? pane : view;
			}
			for (auto* camera : shell.findChildren<ModelViewport*>()) {
				view = !view && camera->isVisible() && stack->widget(static_cast<int>(StudioMode::Levels))->isAncestorOf(camera) ? camera : view;
			}
			if (expect(view != nullptr, "a visible view")) {
				shell.showLevelSidebarTab(QStringLiteral("inspector"));
				settle();
				shell.activateWindow();
				until([&] { return QApplication::activeWindow() == &shell; });
				view->setFocus(Qt::OtherFocusReason);
				expect(until([&] { return view->hasFocus(); }), "the view takes focus");
				if (trenchBroom) {
					QTest::keyClick(view, Qt::Key_1, Qt::ControlModifier);
					expect(until([&] { return holder->currentPageId() == QLatin1String("map"); }, 5000) && mode() == StudioMode::Levels,
						"Ctrl+1 in a view shows the Map tab and stays in Levels", holder->currentPageId());
					action("shell.mode.models")->trigger();
					settle();
					QWidget* elsewhere = QApplication::focusWidget() ? QApplication::focusWidget() : static_cast<QWidget*>(&shell);
					QTest::keyClick(elsewhere, Qt::Key_1, Qt::ControlModifier);
					expect(until([&] { return mode() == StudioMode::Workspace; }, 5000), "outside Levels Ctrl+1 still opens Workspace");
					action("shell.mode.levels")->trigger();
					settle();
				} else {
					if (statisticsSection) {
						statisticsSection->setExpanded(false);
					}
					QTest::keyClick(view, Qt::Key_M);
					expect(until([&] { return QApplication::focusWidget() == table; }, 5000) && holder->currentPageId() == QLatin1String("map")
							&& statisticsSection && statisticsSection->isExpanded(),
						"M opens Map Info", holder->currentPageId());
				}
			}

			expect(shell.levelDocument().editState != QStringLiteral("modified"), "every edit was undone", shell.levelDocument().editState);
			QTimer discard;
			discard.setInterval(50);
			QObject::connect(&discard, &QTimer::timeout, &shell, [] {
				if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
					for (QAbstractButton* button : box->buttons()) {
						if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
							button->click();
						}
					}
				}
			});
			discard.start();
			shell.close();
			discard.stop();
			std::cout << "  pass done after " << clock.elapsed() << " ms" << std::endl;
		}
		if (pass.scale == 200) {
			app.removeTranslator(&expanded);
			app.setLayoutDirection(Qt::LeftToRight);
		}
	}
	QEventLoop drain;
	QTimer::singleShot(200, &drain, &QEventLoop::quit);
	drain.exec();
	std::cout << (ok ? "Map Info UI smoke test passed." : "Map Info UI smoke test failed.") << std::endl;
	return ok ? 0 : 1;
}
