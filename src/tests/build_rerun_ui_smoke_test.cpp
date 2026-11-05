// Run Last Build, Re-run Last Compile and the launch settings of the user's
// own in the offscreen shell: Run Profile in Levels records the build and the
// game's profile and saves a run; Re-run Last Compile replays it and Run Last
// Build repeats it (F5 in the NetRadiant Custom profile, where Build and
// Launch gives the key up) without leaving Levels; the Launch and Test tab's
// Extra arguments are kept for the installation; Launch Profiles makes a
// profile of the user's own; Launch Game from Levels shows its review over
// the map; and Map Properties and Mods change with a group open. Passes:
// NetRadiant Custom at 100%, TrenchBroom at 200% with high contrast, an
// expanded translator and right to left.
#include "app/application_shell.h"
#include "app/game_launch_profiles_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/compiler_runner.h"
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
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QTreeWidget>
#include <QtEndian>

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
bool until(const std::function<bool()>& ready, int milliseconds = 60000)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < milliseconds) {
		QEventLoop loop;
		QTimer::singleShot(15, &loop, &QEventLoop::quit);
		loop.exec();
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

// A stand-in for vibemap2-bsp, run by the shell's compiler runner: a minimal
// Quake BSP beside the map it is given.
int fakeCompiler(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QString output;
	for (const QString& argument : app.arguments().mid(1)) {
		if (argument.endsWith(QStringLiteral(".map"), Qt::CaseInsensitive)) {
			const QFileInfo map(argument);
			output = map.absolutePath() + QLatin1Char('/') + map.completeBaseName() + QStringLiteral(".bsp");
		}
	}
	QByteArray bytes(4 + 15 * 8, '\0');
	qToLittleEndian<qint32>(29, reinterpret_cast<uchar*>(bytes.data()));
	QFile file(output);
	std::cout << "fake vibemap2-bsp" << std::endl;
	return !output.isEmpty() && file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() ? 0 : 1;
}

bool accessible(QWidget* widget)
{
	return widget && !widget->accessibleName().isEmpty() && widget->focusPolicy() != Qt::NoFocus && QAccessible::queryAccessibleInterface(widget);
}

int savedRuns(const QString& mapPath)
{
	return static_cast<int>(QDir(compilerRunManifestDirectory(QFileInfo(mapPath).absolutePath())).entryList({QStringLiteral("*.json")}, QDir::Files).size());
}

// The fixture with the door in a group, for Map Properties with a group open.
bool writeFixture(const QString& path, QString* groupId)
{
	LevelMapDocument document;
	QString error;
	if (!expect(loadLevelMapBytes({QStringLiteral("fixture.map"), {}, {}}, tests::mapInfoFixture(), &document, &error), "fixture loads", error)) {
		return false;
	}
	int door = -1;
	for (const LevelMapEntity& entity : document.entities) {
		door = entity.className == QLatin1String("func_door") ? entity.id : door;
	}
	if (!expect(createLevelSceneNode(&document, LevelSceneNodeKind::Group, QStringLiteral("Door"), {}, groupId, &error)
				&& assignLevelSceneObjects(&document, *groupId, {QStringLiteral("entity:%1").arg(door)}, &error),
			"fixture group", error)) {
		return false;
	}
	LevelDocumentSaveRequest save;
	save.path = path;
	save.overwrite = true;
	save.keepBackup = false;
	const auto report = writeLevelDocument(document, save);
	return expect(report.succeeded(), "fixture written", report.errors.join('\n'));
}
} // namespace

int main(int argc, char** argv)
{
	if (qEnvironmentVariableIsSet("VIBESTUDIO_RERUN_FAKE_COMPILER")) {
		return fakeCompiler(argc, argv);
	}
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
	// The compiler runner starts this program again as vibemap2-bsp.
	qputenv("VIBESTUDIO_RERUN_FAKE_COMPILER", "1");
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.upsertCompilerToolPathOverride({QStringLiteral("vibemap2-bsp"), QCoreApplication::applicationFilePath()});
	GameInstallationProfile installation;
	installation.id = QStringLiteral("rerun-ui-fixture");
	installation.gameKey = QStringLiteral("quake");
	installation.engineFamily = GameEngineFamily::IdTech2;
	installation.displayName = QStringLiteral("Disposable Quake");
	installation.rootPath = QFileInfo(temp.filePath("game")).absoluteFilePath();
	QDir().mkpath(QDir(installation.rootPath).filePath(QStringLiteral("id1")));
	installation.executablePath = QCoreApplication::applicationFilePath();
	installation.readOnly = true;
	settings.upsertGameInstallation(installation);
	// A second installation with another mod folder, for the Mods list.
	QDir().mkpath(QDir(installation.rootPath).filePath(QStringLiteral("alpha")));
	GameInstallationProfile second = installation;
	second.id = QStringLiteral("rerun-ui-second");
	second.displayName = QStringLiteral("Second Quake");
	second.rootPath = QFileInfo(temp.filePath("game2")).absoluteFilePath();
	QDir().mkpath(QDir(second.rootPath).filePath(QStringLiteral("id1")));
	QDir().mkpath(QDir(second.rootPath).filePath(QStringLiteral("beta")));
	settings.upsertGameInstallation(second);
	settings.setSelectedGameInstallation(installation.id);
	settings.sync();
	const QString mapPath = QFileInfo(temp.filePath("maps/rerun.map")).absoluteFilePath();
	QDir().mkpath(QFileInfo(mapPath).absolutePath());
	QString groupId;
	if (!writeFixture(mapPath, &groupId)) {
		return 1;
	}
	// A Quake III map (its patch says so), for the profile remembered per game.
	const QString quake3Path = QFileInfo(temp.filePath("maps/arena.map")).absoluteFilePath();
	{
		QFile file(quake3Path);
		expect(file.open(QIODevice::WriteOnly)
				&& file.write("{\n\"classname\" \"worldspawn\"\n{\n( 0 0 0 ) ( 0 1 0 ) ( 1 0 0 ) common/caulk 0 0 0 1 1 0 0 0\n"
							  "( 0 0 64 ) ( 1 0 64 ) ( 0 1 64 ) common/caulk 0 0 0 1 1 0 0 0\n( 0 0 0 ) ( 1 0 0 ) ( 0 0 1 ) common/caulk 0 0 0 1 1 0 0 0\n"
							  "( 0 64 0 ) ( 0 64 1 ) ( 1 64 0 ) common/caulk 0 0 0 1 1 0 0 0\n( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) common/caulk 0 0 0 1 1 0 0 0\n"
							  "( 64 0 0 ) ( 64 1 0 ) ( 64 0 1 ) common/caulk 0 0 0 1 1 0 0 0\n}\n{\npatchDef2\n{\ncommon/caulk\n( 3 3 0 0 0 )\n(\n"
							  "( ( 0 0 96 0 0 ) ( 0 32 96 0 0.5 ) ( 0 64 96 0 1 ) )\n( ( 32 0 96 0.5 0 ) ( 32 32 96 0.5 0.5 ) ( 32 64 96 0.5 1 ) )\n"
							  "( ( 64 0 96 1 0 ) ( 64 32 96 1 0.5 ) ( 64 64 96 1 1 ) )\n)\n}\n}\n}\n")
					> 0,
			"Quake III map written");
	}
	for (const char* id : {"build.run-last", "compiler.rerun-last", "game.edit-launch-profiles"}) {
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(QString::fromLatin1(id), &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(), "palette entry",
			QString::fromLatin1(id));
	}

	struct Pass {
		const char* profile;
		int scale;
	};
	for (const Pass& pass : {Pass {"netradiant-custom", 100}, Pass {"trenchbroom", 200}}) {
		phase = QStringLiteral("%1 %2%").arg(QLatin1String(pass.profile)).arg(pass.scale);
		std::cout << "Build rerun UI " << qPrintable(phase) << std::endl;
		const bool netRadiant = QLatin1String(pass.profile) == QLatin1String("netradiant-custom");
		// Each pass starts with nothing built and nothing of the user's own.
		QDir(compilerRunManifestDirectory(QFileInfo(mapPath).absolutePath())).removeRecursively();
		settings.setLastBuildRun(QString(), {});
		settings.setLevelCompilerProfile(QStringLiteral("quake"), QString());
		// Quake III remembers a profile that is not its default one.
		settings.setLevelCompilerProfile(QStringLiteral("quake3"), QStringLiteral("vibemap3-convert"));
		settings.setSelectedGameInstallation(installation.id);
		settings.setUserGameLaunchProfiles({});
		settings.setLaunchExtraArguments(installation.id, QString());
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
			QAction* runLast = action("build.runLast");
			QAction* rerunLast = action("compiler.rerunLast");
			QAction* editProfiles = action("game.editLaunchProfiles");
			QAction* buildAndLaunch = action("build.runAndLaunch");
			QAction* launch = action("game.launch");
			if (!expect(runLast && rerunLast && editProfiles && buildAndLaunch && launch && stack, "commands")) {
				return 1;
			}
			for (QAction* command : {runLast, rerunLast, editProfiles}) {
				expect(!command->text().isEmpty() && !command->toolTip().isEmpty() && !command->statusTip().isEmpty(), "commands are labelled", command->objectName());
			}
			const QKeySequence f5(QStringLiteral("F5"));
			expect(netRadiant ? runLast->shortcuts().contains(f5) && !buildAndLaunch->shortcuts().contains(f5)
							  : runLast->shortcuts().isEmpty() && buildAndLaunch->shortcuts().contains(f5),
				"F5 runs the last build in NetRadiant Custom; elsewhere it builds and launches",
				QKeySequence::listToString(runLast->shortcuts()) + QStringLiteral(" / ") + QKeySequence::listToString(buildAndLaunch->shortcuts()));
			expect(!runLast->isEnabled() && !rerunLast->isEnabled(), "nothing to repeat yet");
			for (const char* id : {"build.runLast", "compiler.rerunLast", "game.launch"}) {
				auto* tile = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-") + QLatin1String(id));
				expect(tile && accessible(tile), "Tools tab tile", QLatin1String(id));
			}

			// Run Profile: a saved run, the last build and the game's profile.
			phase = QStringLiteral("%1 %2%: Run Profile").arg(QLatin1String(pass.profile)).arg(pass.scale);
			QToolButton* runProfile = nullptr;
			for (auto* button : shell.findChildren<QToolButton*>()) {
				runProfile = button->accessibleName().contains(QStringLiteral("Run level map compiler profile")) ? button : runProfile;
			}
			if (!expect(runProfile && runProfile->isEnabled(), "Run Profile button")) {
				return 1;
			}
			QElapsedTimer compileClock;
			compileClock.start();
			runProfile->click();
			const bool compiled = until([&] { return savedRuns(mapPath) == 1 && action("compiler.run")->isEnabled() && rerunLast->isEnabled(); });
			expect(compiled, "a saved run",
				QStringLiteral("%1 runs, Run enabled %2, Re-run enabled %3, %4 ms, status %5, project %6")
					.arg(savedRuns(mapPath))
					.arg(action("compiler.run")->isEnabled())
					.arg(rerunLast->isEnabled())
					.arg(compileClock.elapsed())
					.arg(shell.statusBar()->currentMessage(), StudioSettings().currentProjectPath()));
			const StudioBuildRunRecord record = StudioSettings().lastBuildRun(QString());
			expect(record.kind == QStringLiteral("profile") && record.id == QStringLiteral("vibemap2-bsp") && !record.launch, "the last build is the profile",
				record.kind + QLatin1Char(' ') + record.id);
			expect(StudioSettings().levelCompilerProfile(QStringLiteral("quake")) == QStringLiteral("vibemap2-bsp"), "the game's profile is remembered");
			expect(runLast->isEnabled() && mode() == StudioMode::Levels, "Run Last Build is ready, still in Levels");

			// Re-run Last Compile replays the saved run; it becomes the newest.
			phase = QStringLiteral("%1 %2%: Re-run Last Compile").arg(QLatin1String(pass.profile)).arg(pass.scale);
			rerunLast->trigger();
			const bool replayed = until([&] { return savedRuns(mapPath) == 2 && action("compiler.run")->isEnabled(); });
			expect(replayed, "the replay is saved", QString::number(savedRuns(mapPath)));
			expect(mode() == StudioMode::Levels, "the replay stays in Levels");

			// Run Last Build: F5 from a view in NetRadiant Custom.
			phase = QStringLiteral("%1 %2%: Run Last Build").arg(QLatin1String(pass.profile)).arg(pass.scale);
			until([&] { return runLast->isEnabled(); });
			if (netRadiant) {
				QWidget* view = nullptr;
				for (auto* pane : shell.findChildren<MapViewport*>()) {
					view = pane->isVisible() ? pane : view;
				}
				if (expect(view != nullptr, "a visible view")) {
					shell.activateWindow();
					until([&] { return QApplication::activeWindow() == &shell; });
					view->setFocus(Qt::OtherFocusReason);
					until([&] { return view->hasFocus(); }, 5000);
					QTest::keyClick(view, Qt::Key_F5);
				}
			} else {
				runLast->trigger();
			}
			const bool repeated = until([&] { return savedRuns(mapPath) == 3 && action("compiler.run")->isEnabled(); });
			expect(repeated, "Run Last Build compiles again", QString::number(savedRuns(mapPath)));
			expect(mode() == StudioMode::Levels, "Run Last Build stays in Levels");

			// Launch and Test: extra arguments for this installation.
			phase = QStringLiteral("%1 %2%: extra arguments").arg(QLatin1String(pass.profile)).arg(pass.scale);
			auto* extra = shell.findChild<QLineEdit*>(QStringLiteral("launchExtraArguments"));
			auto* deploy = shell.findChild<QAbstractButton*>(QStringLiteral("launchDeploy"));
			auto* choice = shell.findChild<QComboBox*>(QStringLiteral("launchProfileChoice"));
			auto* summary = shell.findChild<QTreeWidget*>(QStringLiteral("launchSummary"));
			if (expect(extra && deploy && choice && summary, "Launch and Test controls")) {
				action("shell.mode.build")->trigger();
				settle();
				expect(accessible(extra) && !extra->accessibleDescription().isEmpty() && !extra->placeholderText().isEmpty(), "Extra arguments is accessible");
				extra->setText(QStringLiteral("+set developer 1"));
				Q_EMIT extra->editingFinished();
				settle();
				expect(StudioSettings().launchExtraArguments(installation.id) == QStringLiteral("+set developer 1"), "kept for the installation");
				QStringList rows;
				for (QTreeWidgetItemIterator it(summary); *it; ++it) {
					rows << (*it)->text(1);
				}
				expect(rows.join(' ').contains(QStringLiteral("+set developer 1")), "the plan shows the extra arguments", rows.join(" | "));
			}

			// Launch Profiles: a profile of the user's own.
			phase = QStringLiteral("%1 %2%: launch profiles").arg(QLatin1String(pass.profile)).arg(pass.scale);
			bool dialogSeen = false;
			QTimer::singleShot(100, &shell, [&] {
				auto* dialog = qobject_cast<GameLaunchProfilesDialog*>(QApplication::activeModalWidget());
				if (!expect(dialog != nullptr, "the Launch Profiles window opens")) {
					if (QApplication::activeModalWidget()) {
						QApplication::activeModalWidget()->close();
					}
					return;
				}
				dialogSeen = true;
				auto* list = dialog->findChild<QWidget*>(QStringLiteral("launchProfilesList"));
				auto* name = dialog->findChild<QLineEdit*>(QStringLiteral("launchProfileName"));
				auto* arguments = dialog->findChild<QLineEdit*>(QStringLiteral("launchProfileArguments"));
				auto* preview = dialog->findChild<QLabel*>(QStringLiteral("launchProfilePreview"));
				auto* problem = dialog->findChild<QLabel*>(QStringLiteral("launchProfileProblem"));
				auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("launchProfilesButtons"));
				auto* add = dialog->findChild<QPushButton*>(QStringLiteral("launchProfilesAdd"));
				if (!expect(list && name && arguments && preview && problem && buttons && add, "dialog controls")) {
					dialog->reject();
					return;
				}
				expect(accessible(list) && accessible(name) && accessible(arguments) && accessible(add) && !arguments->toolTip().isEmpty(), "dialog is accessible");
				expect(!name->isEnabled() && preview->text().contains(QStringLiteral("Add")), "empty at first", preview->text());
				add->click();
				expect(name->isEnabled() && arguments->text().contains(QStringLiteral("+map {map}")), "a new profile starts from the game's arguments",
					arguments->text());
				name->setText(QStringLiteral("Developer"));
				Q_EMIT name->textEdited(name->text());
				arguments->setText(QStringLiteral("-game {mod} +map {mapname}"));
				Q_EMIT arguments->textEdited(arguments->text());
				expect(!buttons->button(QDialogButtonBox::Ok)->isEnabled() && problem->isVisibleTo(dialog) && problem->text().contains(QStringLiteral("{mapname}")),
					"an unknown token blocks OK and is named", problem->text());
				arguments->setText(QStringLiteral("-game {mod} +map {map} +set name \"Test Player\""));
				Q_EMIT arguments->textEdited(arguments->text());
				expect(buttons->button(QDialogButtonBox::Ok)->isEnabled() && preview->text().contains(QStringLiteral("+map rerun"))
						&& preview->text().contains(QStringLiteral("\"Test Player\"")),
					"the preview fills the tokens", preview->text());
				buttons->button(QDialogButtonBox::Ok)->click();
			});
			editProfiles->trigger();
			settle();
			expect(dialogSeen, "Launch Profiles ran");
			const QVector<GameLaunchProfile> mine = StudioSettings().userGameLaunchProfiles();
			expect(mine.size() == 1 && mine[0].displayName == QStringLiteral("Developer") && mine[0].argumentLine.contains(QStringLiteral("\"Test Player\"")),
				"the profile is saved", QString::number(mine.size()));
			if (choice && !mine.isEmpty()) {
				expect(choice->currentData().toString() == mine[0].id, "the new profile is chosen", choice->currentData().toString());
			}

			// Launch Game from Levels: the review opens over the map.
			phase = QStringLiteral("%1 %2%: launch from Levels").arg(QLatin1String(pass.profile)).arg(pass.scale);
			if (deploy && deploy->isChecked()) {
				deploy->click();
			}
			action("shell.mode.levels")->trigger();
			settle();
			QString reviewed;
			StudioMode modeDuringReview = StudioMode::Workspace;
			QTimer poll;
			poll.setInterval(30);
			QObject::connect(&poll, &QTimer::timeout, &shell, [&] {
				if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
					reviewed = box->text();
					modeDuringReview = mode();
					for (QAbstractButton* button : box->buttons()) {
						if (box->buttonRole(button) == QMessageBox::RejectRole) {
							button->click();
							return;
						}
					}
					box->reject();
				}
			});
			poll.start();
			launch->trigger();
			until([&] { return !reviewed.isEmpty(); }, 30000);
			poll.stop();
			expect(reviewed.contains(QStringLiteral("+set developer 1")) && reviewed.contains(QStringLiteral("\"Test Player\"")) && reviewed.contains(QStringLiteral("+map rerun")),
				"the review shows the user's profile and extra arguments", reviewed);
			expect(modeDuringReview == StudioMode::Levels && mode() == StudioMode::Levels, "the review opens over Levels");

			// Map Properties and Mods with a group open.
			phase = QStringLiteral("%1 %2%: map settings with a group open").arg(QLatin1String(pass.profile)).arg(pass.scale);
			auto* plan = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
			int door = -1;
			for (const LevelMapEntity& entity : shell.levelDocument().entities) {
				door = entity.className == QLatin1String("func_door") ? entity.id : door;
			}
			if (expect(plan && door >= 0, "plan view and door")) {
				plan->selectSceneObjects({{LevelMapSelectionKind::Entity, door}}, LevelScenePick::Replace);
				action("map.openGroup")->trigger();
				settle();
				expect(shell.levelDocument().openSceneGroups == QStringList {groupId}, "the door's group is open");
				const QByteArray original = serializeLevelMap(shell.levelDocument()).bytes;
				LevelMapSoftBoundsChange change;
				change.mode = LevelMapSoftBoundsMode::Disabled;
				QString error;
				expect(shell.applyLevelMapSoftBounds(change, &error), "soft bounds change with a group open", error);
				expect(shell.applyLevelMapEnabledMods({QStringLiteral("hipnotic")}, &error), "mods change with a group open", error);
				expect(shell.levelDocument().openSceneGroups == QStringList {groupId} && levelMapEnabledMods(shell.levelDocument()) == QStringList {QStringLiteral("hipnotic")}
						&& levelMapSoftBounds(shell.levelDocument()).mode == LevelMapSoftBoundsMode::Disabled,
					"both written; the group stays open");
				// The Worldspawn list's own edit path, with the group still open.
				auto* worldspawn = shell.findChild<QTreeWidget*>(QStringLiteral("levelWorldspawn"));
				QTreeWidgetItem* message = nullptr;
				for (int row = 0; worldspawn && row < worldspawn->topLevelItemCount(); ++row) {
					QTreeWidgetItem* item = worldspawn->topLevelItem(row);
					message = item->data(0, Qt::UserRole).toString() == QLatin1String("message") ? item : message;
				}
				int undos = 2;
				if (expect(message != nullptr, "the Worldspawn list shows message")) {
					message->setText(1, QStringLiteral("Edited with a group open"));
					settle();
					QString value;
					for (const LevelMapEntity& entity : shell.levelDocument().entities) {
						for (const LevelMapProperty& property : entity.properties) {
							value = entity.className == QLatin1String("worldspawn") && property.key == QLatin1String("message") ? property.value : value;
						}
					}
					expect(value == QStringLiteral("Edited with a group open") && shell.levelDocument().openSceneGroups == QStringList {groupId},
						"a Worldspawn key changes from the list with a group open; the group stays open", value);
					undos = 3;
				}
				for (int undo = 0; undo < undos; ++undo) {
					action("map.undo")->trigger();
				}
				settle();
				expect(serializeLevelMap(shell.levelDocument()).bytes == original, "the undos restore the map");
				action("map.closeAllGroups")->trigger();
				settle();
			}

			// The Mods list follows a change of installation.
			phase = QStringLiteral("%1 %2%: Mods follow the installation").arg(QLatin1String(pass.profile)).arg(pass.scale);
			auto* modName = shell.findChild<QComboBox*>(QStringLiteral("levelMapModName"));
			auto* installs = shell.findChild<QListWidget*>(QStringLiteral("gameInstallations"));
			QPushButton* use = nullptr;
			for (auto* button : shell.findChildren<QPushButton*>()) {
				use = button->accessibleName().contains(QStringLiteral("Select game installation")) ? button : use;
			}
			const auto offered = [&] {
				QStringList items;
				for (int index = 0; modName && index < modName->count(); ++index) {
					items << modName->itemText(index);
				}
				return items;
			};
			const auto choose = [&](const QString& id) {
				for (int row = 0; installs && row < installs->count(); ++row) {
					if (installs->item(row)->data(Qt::UserRole).toString() == id) {
						installs->setCurrentRow(row);
					}
				}
				use->click();
				settle();
			};
			if (expect(modName && installs && use, "Mods and installation controls")) {
				expect(offered().contains(QStringLiteral("alpha")) && !offered().contains(QStringLiteral("beta")), "the first installation's mods", offered().join(' '));
				choose(second.id);
				expect(offered().contains(QStringLiteral("beta")) && !offered().contains(QStringLiteral("alpha")), "the Mods list follows the installation",
					offered().join(' '));
				choose(installation.id);
				expect(offered().contains(QStringLiteral("alpha")), "and back", offered().join(' '));
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

			// A map opened afterwards starts with its game's remembered profile.
			phase = QStringLiteral("%1 %2%: profile per game").arg(QLatin1String(pass.profile)).arg(pass.scale);
			shell.openPathFromCommandLine(quake3Path);
			QComboBox* levelsProfile = nullptr;
			for (auto* combo : shell.findChildren<QComboBox*>()) {
				levelsProfile = combo->accessibleName().contains(QStringLiteral("Level compiler profile")) ? combo : levelsProfile;
			}
			expect(until([&] { return shell.levelDocument().sourcePath == quake3Path; }, 30000) && shell.levelDocument().format == LevelMapFormat::Quake3Map,
				"the Quake III map opens");
			expect(levelsProfile && levelsProfile->currentData().toString() == QStringLiteral("vibemap3-convert"),
				"the Quake III map starts with the profile remembered for Quake III", levelsProfile ? levelsProfile->currentData().toString() : QString());
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
	std::cout << (ok ? "Build rerun UI smoke test passed." : "Build rerun UI smoke test failed.") << std::endl;
	return ok ? 0 : 1;
}
