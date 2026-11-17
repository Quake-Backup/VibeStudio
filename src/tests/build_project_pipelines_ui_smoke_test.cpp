// The project's own build pipelines and the program trust check in the
// offscreen shell: Build Pipelines… makes a pipeline from the Build page's
// choice (a stage turned off, arguments with a variable, problems named and OK
// held until they are fixed, a live preview) and keeps it in the project
// manifest; the Pipeline list offers it; running it asks first because the
// project's own compiler path names the program, shows the command line,
// runs nothing when cancelled (the default), runs and remembers the trust in
// the user's settings when trusted, and runs without asking after that; Re-run
// Last Compile asks before replaying a saved run whose program is not a
// configured compiler. Passes: NetRadiant Custom at 100%, TrenchBroom at 200%
// with high contrast, an expanded translator and right to left.
#include "app/application_shell.h"
#include "app/build_pipelines_dialog.h"
#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "app/ui_primitives.h"
#include "core/build_program_trust.h"
#include "core/compiler_runner.h"
#include "core/studio_semantics.h"

#include <QAbstractButton>
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QtEndian>

#include <functional>
#include <iostream>

using namespace slopstudio;
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

// A stand-in for slopmap2-bsp: a minimal Quake BSP beside the map it is given.
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
	if (qEnvironmentVariableIsSet("SLOPSTUDIO_PIPELINES_FAKE_SLOW")) {
		// A compiler at work: a line every tenth of a second until it is
		// stopped, and nothing written.
		for (int line = 0; line < 600; ++line) {
			std::cout << "fake slopmap2-bsp working " << line << std::endl;
			QThread::msleep(100);
		}
		return 1;
	}
	QByteArray bytes(4 + 15 * 8, '\0');
	qToLittleEndian<qint32>(29, reinterpret_cast<uchar*>(bytes.data()));
	QFile file(output);
	std::cout << "fake slopmap2-bsp" << std::endl;
	return !output.isEmpty() && file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() ? 0 : 1;
}

bool accessible(QWidget* widget)
{
	return widget && !widget->accessibleName().isEmpty() && widget->focusPolicy() != Qt::NoFocus && QAccessible::queryAccessibleInterface(widget);
}

QByteArray mapText()
{
	return QByteArrayLiteral("{\n\"classname\" \"worldspawn\"\n\"wad\" \"gfx/base.wad\"\n{\n( 0 0 0 ) ( 0 1 0 ) ( 1 0 0 ) floor 0 0 0 1 1\n"
							 "( 0 0 64 ) ( 1 0 64 ) ( 0 1 64 ) floor 0 0 0 1 1\n( 0 0 0 ) ( 1 0 0 ) ( 0 0 1 ) floor 0 0 0 1 1\n"
							 "( 0 64 0 ) ( 0 64 1 ) ( 1 64 0 ) floor 0 0 0 1 1\n( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) floor 0 0 0 1 1\n"
							 "( 64 0 0 ) ( 64 1 0 ) ( 64 0 1 ) floor 0 0 0 1 1\n}\n}\n{\n\"classname\" \"info_player_start\"\n\"origin\" \"32 32 24\"\n}\n");
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

// Watches for the trust question and answers it; records what it showed.
struct TrustAnswer {
	bool trust = false;
	int asked = 0;
	QString text;
	QString defaultButton;
};

QTimer* answerTrust(QWidget* host, TrustAnswer* answer)
{
	auto* timer = new QTimer(host);
	timer->setInterval(30);
	QObject::connect(timer, &QTimer::timeout, host, [answer]() {
		auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
		if (!box || box->objectName() != QLatin1String("buildProgramTrustReview")) {
			return;
		}
		++answer->asked;
		answer->text = box->text();
		answer->defaultButton = box->defaultButton() ? box->defaultButton()->text() : QString();
		QAbstractButton* run = box->findChild<QPushButton*>(QStringLiteral("buildProgramTrustRun"));
		if (answer->trust && run) {
			run->click();
		} else if (box->escapeButton()) {
			box->escapeButton()->click();
		} else {
			box->reject();
		}
	});
	timer->start();
	return timer;
}
} // namespace

int main(int argc, char** argv)
{
	if (qEnvironmentVariableIsSet("SLOPSTUDIO_PIPELINES_FAKE_COMPILER")) {
		return fakeCompiler(argc, argv);
	}
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
	// The compiler runner starts this program again as slopmap2-bsp.
	qputenv("SLOPSTUDIO_PIPELINES_FAKE_COMPILER", "1");
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	for (const char* id : {"build.edit-pipelines"}) {
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
		std::cout << "Build pipelines UI " << qPrintable(phase) << std::endl;
		// Each pass has a project of its own; its manifest names this program
		// as its BSP compiler, which is project data, not the user's choice.
		const QString project = QFileInfo(temp.filePath(QStringLiteral("project-%1").arg(pass.scale))).absoluteFilePath();
		const QString mapPath = QDir(project).filePath(QStringLiteral("maps/start.map"));
		const QString bspPath = QDir(project).filePath(QStringLiteral("maps/start.bsp"));
		const QString strangerTool = QDir(project).filePath(QStringLiteral("tools/stranger.cmd"));
		if (!expect(writeFile(mapPath, mapText()) && writeFile(strangerTool, QByteArrayLiteral("@echo off\r\necho not a compiler\r\n")), "project files")) {
			return 1;
		}
		ProjectManifest manifest = defaultProjectManifest(project, QStringLiteral("Pipelines"));
		manifest.compilerToolOverrides = {{QStringLiteral("slopmap2-bsp"), QCoreApplication::applicationFilePath()}};
		QString error;
		if (!expect(saveProjectManifest(manifest, &error), "project manifest", error)) {
			return 1;
		}
		// A saved run beside it whose program is not a compiler at all.
		CompilerCommandManifest stranger;
		stranger.profileId = QStringLiteral("slopmap2-bsp");
		stranger.toolId = QStringLiteral("slopmap2-bsp");
		stranger.createdUtc = QDateTime::currentDateTimeUtc();
		stranger.program = strangerTool;
		stranger.arguments = {mapPath};
		stranger.commandLine = compilerCommandLineText(strangerTool, stranger.arguments);
		stranger.workingDirectory = QFileInfo(mapPath).absolutePath();
		stranger.inputPaths = {mapPath};
		const QString strangerRun = newCompilerRunManifestPath(project, stranger.profileId);
		if (!expect(saveCompilerCommandManifest(stranger, strangerRun, &error), "a stranger's saved run", error)) {
			return 1;
		}
		const auto runCount = [&] { return static_cast<int>(QDir(compilerRunManifestDirectory(project)).entryList({QStringLiteral("*.json")}, QDir::Files).size()); };

		{
			StudioSettings settings;
			settings.setRestoreSession(false);
			settings.setReducedMotion(true);
			settings.clearCompilerToolPathOverrides();
			settings.setTrustedBuildPrograms({});
			settings.setCurrentProjectPath(project);
			settings.setSelectedEditorProfileId(QString::fromLatin1(pass.profile));
			settings.setTextScalePercent(pass.scale);
			settings.setTheme(pass.scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
			settings.sync();
			applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, pass.scale));
		}
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
			QAction* editPipelines = action("build.editPipelines");
			QAction* runPipeline = action("build.runPipeline");
			QAction* rerunLast = action("compiler.rerunLast");
			auto* choice = shell.findChild<QComboBox*>(QStringLiteral("buildPipelineChoice"));
			auto* editButton = shell.findChild<QToolButton*>(QStringLiteral("buildEditPipelines"));
			if (!expect(editPipelines && runPipeline && rerunLast && choice && editButton, "commands and controls")) {
				return 1;
			}
			expect(!editPipelines->text().isEmpty() && !editPipelines->toolTip().isEmpty() && editPipelines->isEnabled(), "Build Pipelines is labelled and ready");
			expect(accessible(editButton), "the Build page's Pipelines button is accessible");

			// Re-run Last Compile: a saved run whose program is no compiler asks
			// first, shows the command line and, cancelled, runs nothing.
			phase = QStringLiteral("%1 %2%: Re-run Last Compile").arg(QLatin1String(pass.profile)).arg(pass.scale);
			TrustAnswer refuse;
			QTimer* refuser = answerTrust(&shell, &refuse);
			expect(until([&] { return rerunLast->isEnabled(); }, 10000), "the saved run enables Re-run Last Compile");
			const int runsBefore = runCount();
			rerunLast->trigger();
			until([&] { return refuse.asked > 0; }, 10000);
			settle();
			expect(refuse.asked == 1 && refuse.text.contains(QStringLiteral("stranger.cmd")) && refuse.text.contains(QStringLiteral("start.map")),
				"the question names the program and its command line", refuse.text);
			expect(!refuse.defaultButton.isEmpty() && !refuse.defaultButton.contains(QStringLiteral("Trust")), "not running is the default", refuse.defaultButton);
			expect(runCount() == runsBefore && action("compiler.run")->isEnabled() && StudioSettings().trustedBuildPrograms().isEmpty(), "nothing ran, nothing trusted",
				QString::number(runCount()));
			refuser->stop();
			delete refuser;

			// Build Pipelines…: a pipeline of the project's own.
			phase = QStringLiteral("%1 %2%: Build Pipelines").arg(QLatin1String(pass.profile)).arg(pass.scale);
			action("shell.mode.build")->trigger();
			settle();
			const int quakeFull = choice->findData(QStringLiteral("quake-full"));
			if (quakeFull >= 0) {
				choice->setCurrentIndex(quakeFull);
			}
			bool dialogSeen = false;
			QTimer::singleShot(100, &shell, [&] {
				auto* dialog = qobject_cast<BuildPipelinesDialog*>(QApplication::activeModalWidget());
				if (!expect(dialog != nullptr, "the Build Pipelines window opens")) {
					if (QApplication::activeModalWidget()) {
						QApplication::activeModalWidget()->close();
					}
					return;
				}
				dialogSeen = true;
				auto* list = dialog->findChild<QListWidget*>(QStringLiteral("buildPipelinesList"));
				auto* add = dialog->findChild<QPushButton*>(QStringLiteral("buildPipelinesAdd"));
				auto* name = dialog->findChild<QLineEdit*>(QStringLiteral("buildPipelineName"));
				auto* stages = dialog->findChild<QListWidget*>(QStringLiteral("buildPipelineStages"));
				auto* profile = dialog->findChild<QComboBox*>(QStringLiteral("buildPipelineStageProfile"));
				auto* input = dialog->findChild<QComboBox*>(QStringLiteral("buildPipelineStageInput"));
				auto* arguments = dialog->findChild<QLineEdit*>(QStringLiteral("buildPipelineStageArguments"));
				auto* variables = dialog->findChild<QTableWidget*>(QStringLiteral("buildPipelineVariables"));
				auto* addVariable = dialog->findChild<QPushButton*>(QStringLiteral("buildPipelineVariableAdd"));
				auto* preview = dialog->findChild<QPlainTextEdit*>(QStringLiteral("buildPipelinePreview"));
				auto* problem = dialog->findChild<QLabel*>(QStringLiteral("buildPipelineProblem"));
				auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("buildPipelinesButtons"));
				if (!expect(list && add && name && stages && profile && input && arguments && variables && addVariable && preview && problem && buttons,
						"dialog controls")) {
					dialog->reject();
					return;
				}
				for (QWidget* widget : std::initializer_list<QWidget*> {list, add, name, stages, profile, input, arguments, variables, addVariable, preview}) {
					expect(accessible(widget), "dialog control is accessible", widget->objectName());
				}
				expect(arguments->layoutDirection() == Qt::LeftToRight && !arguments->toolTip().isEmpty(), "arguments are typed left to right, with help");
				expect(list->count() == 0 && !name->isEnabled(), "no pipelines of the project's own yet");
				add->click();
				expect(list->count() == 1 && name->isEnabled() && stages->count() == 3, "a new pipeline starts from Quake full compile", QString::number(stages->count()));
				name->setText(QStringLiteral("Fast BSP"));
				Q_EMIT name->textEdited(name->text());
				auto* id = dialog->findChild<QLabel*>(QStringLiteral("buildPipelineId"));
				expect(id && id->text() == QStringLiteral("fast-bsp"), "a new pipeline's id follows its name", id ? id->text() : QString());
				// Turn VIS and LIGHT off; LIGHT gets an argument with a variable.
				stages->item(1)->setCheckState(Qt::Unchecked);
				stages->setCurrentRow(2);
				stages->item(2)->setCheckState(Qt::Unchecked);
				expect(input->isEnabled() && input->currentData().toString() == QStringLiteral("previous"), "a later stage reads the previous output");
				arguments->setText(QStringLiteral("-extra ${QUALITY}"));
				Q_EMIT arguments->textEdited(arguments->text());
				expect(!buttons->button(QDialogButtonBox::Ok)->isEnabled() && problem->isVisibleTo(dialog) && problem->text().contains(QStringLiteral("${QUALITY}")),
					"an unknown variable blocks OK and is named", problem->text());
				addVariable->click();
				const int row = variables->rowCount() - 1;
				if (expect(row >= 0 && variables->item(row, 0) && variables->item(row, 1), "a variable row")) {
					// Add opens the name for typing; type into whatever editor is open,
					// or set the cell when none is.
					const auto type = [&](int column, const QString& text) {
						settle();
						if (auto* editor = variables->viewport()->findChild<QLineEdit*>(); editor && editor->isVisible()) {
							editor->setText(text);
							QTest::keyClick(editor, Qt::Key_Return);
						} else {
							variables->item(row, column)->setText(text);
						}
						settle();
					};
					type(0, QStringLiteral("QUALITY"));
					expect(variables->item(row, 0)->text() == QStringLiteral("QUALITY"), "the name is typed", variables->item(row, 0)->text());
					variables->item(row, 1)->setText(QStringLiteral("high"));
				}
				expect(buttons->button(QDialogButtonBox::Ok)->isEnabled() && !problem->isVisibleTo(dialog), "defining it clears the problem", problem->text());
				arguments->setText(QStringLiteral("-extra \"open"));
				Q_EMIT arguments->textEdited(arguments->text());
				expect(!buttons->button(QDialogButtonBox::Ok)->isEnabled() && problem->text().contains(QStringLiteral("quote")), "an unclosed quote blocks OK", problem->text());
				arguments->setText(QStringLiteral("-extra ${QUALITY}"));
				Q_EMIT arguments->textEdited(arguments->text());
				stages->setCurrentRow(0);
				expect(!input->isEnabled(), "the first stage always reads the map");
				expect(until([&] { return preview->toPlainText().contains(QStringLiteral("start.map")) && preview->toPlainText().contains(QStringLiteral("turned off")); }, 5000),
					"the preview shows the command lines and the stages turned off", preview->toPlainText());
				buttons->button(QDialogButtonBox::Ok)->click();
			});
			editPipelines->trigger();
			settle();
			expect(dialogSeen, "Build Pipelines ran");
			ProjectManifest saved;
			loadProjectManifest(project, &saved);
			const QVector<BuildPipelineDescriptor> mine = projectBuildPipelines(saved);
			expect(mine.size() == 1 && mine[0].id == QStringLiteral("fast-bsp") && mine[0].stages.size() == 3 && mine[0].stages[0].enabledByDefault
					&& !mine[0].stages[1].enabledByDefault && !mine[0].stages[2].enabledByDefault && mine[0].stages[2].argumentLine == QStringLiteral("-extra ${QUALITY}")
					&& mine[0].variables.value(QStringLiteral("QUALITY")) == QStringLiteral("high"),
				"the pipeline is kept in the project manifest", QString::number(mine.size()));
			expect(choice->currentData().toString() == QStringLiteral("fast-bsp") && choice->findData(QStringLiteral("quake-full")) >= 0,
				"the Pipeline list offers it and chooses it", choice->currentData().toString());

			// Run Pipeline: the project's compiler path names this program, so
			// the build asks first; cancelled, nothing runs.
			phase = QStringLiteral("%1 %2%: program trust").arg(QLatin1String(pass.profile)).arg(pass.scale);
			TrustAnswer cancel;
			QTimer* canceller = answerTrust(&shell, &cancel);
			runPipeline->trigger();
			until([&] { return cancel.asked > 0; }, 15000);
			settle();
			expect(cancel.asked == 1 && cancel.text.contains(QFileInfo(QCoreApplication::applicationFilePath()).fileName()) && cancel.text.contains(QStringLiteral("start.map")),
				"the question shows the stage's command line", cancel.text);
			expect(!QFileInfo::exists(bspPath) && StudioSettings().trustedBuildPrograms().isEmpty() && runPipeline->isEnabled(), "cancelled, nothing runs");
			canceller->stop();
			delete canceller;

			TrustAnswer trust;
			trust.trust = true;
			QTimer* truster = answerTrust(&shell, &trust);
			runPipeline->trigger();
			const bool built = until([&] { return QFileInfo::exists(bspPath) && runPipeline->isEnabled(); });
			expect(trust.asked == 1 && built, "trusted, the pipeline runs", shell.statusBar()->currentMessage());
			const QVector<BuildProgramTrustRecord> records = StudioSettings().trustedBuildPrograms();
			expect(records.size() == 1 && QFileInfo(records[0].program) == QFileInfo(QCoreApplication::applicationFilePath())
					&& QFileInfo(records[0].scope) == QFileInfo(project),
				"the trust is kept in the user's settings for this project", QString::number(records.size()));
			{
				// Closed again before the next run rewrites it.
				QFile manifestFile(projectManifestPath(project));
				expect(manifestFile.open(QIODevice::ReadOnly) && !manifestFile.readAll().contains("sha256"), "and never in the project");
			}
			QFile::remove(bspPath);
			runPipeline->trigger();
			const bool again = until([&] { return QFileInfo::exists(bspPath) && runPipeline->isEnabled(); });
			expect(again && trust.asked == 1, "trusted once, the next run does not ask", QString::number(trust.asked));
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

			if (pass.scale == 100) {
				// A build that prints as it goes. Its lines reach the Activity log
				// while it runs; the task stays Running, and so can be cancelled,
				// whatever a line's level or a stage's result; cancelling ends it
				// as Cancelled, by the user.
				phase = QStringLiteral("%1 %2%: a running build").arg(QLatin1String(pass.profile)).arg(pass.scale);
				auto* tasks = shell.findChild<QListWidget*>(QStringLiteral("activityTasks"));
				auto* profiles = shell.findChild<QListWidget*>(QStringLiteral("compilerPipeline"));
				DetailDrawer* details = nullptr;
				for (QFrame* frame : shell.findChildren<QFrame*>(QStringLiteral("detailDrawer"))) {
					if (frame->accessibleName() == QLatin1String("Activity detail drawer")) {
						details = dynamic_cast<DetailDrawer*>(frame);
					}
				}
				QAbstractButton* cancelButton = nullptr;
				for (QAbstractButton* button : shell.findChildren<QAbstractButton*>()) {
					if (button->accessibleName() == QLatin1String("Cancel selected activity")) {
						cancelButton = button;
					}
				}
				QAction* cancelTask = action("activity.cancel");
				QAction* runProfile = action("compiler.run");
				if (!expect(tasks && profiles && details && cancelButton && cancelTask && runProfile, "activity controls")) {
					return 1;
				}
				const auto section = [&](const char* id) {
					for (const DetailSection& entry : details->sections()) {
						if (entry.id == QLatin1String(id)) {
							return entry.content;
						}
					}
					return QString();
				};
				// The newest row of that title: the list is newest first.
				const auto rowState = [&](const QString& title) {
					for (int row = 0; row < tasks->count(); ++row) {
						if (tasks->item(row)->text().contains(title)) {
							return tasks->item(row)->data(Qt::UserRole + 1).toString();
						}
					}
					return QString();
				};
				qputenv("SLOPSTUDIO_PIPELINES_FAKE_SLOW", "1");
				QFile::remove(bspPath);
				expect(!cancelTask->isEnabled(), "nothing to cancel before the build");
				runPipeline->trigger();
				expect(until([&] { return section("log").contains(QStringLiteral("working 3")); }, 30000),
					"the compiler's lines reach the Activity log while it runs", section("log").right(400));
				expect(section("raw").contains(QStringLiteral("State id: running")) && rowState(QStringLiteral("Build Pipeline")) == QStringLiteral("running"),
					"a printed line leaves the task running", section("raw"));
				expect(cancelButton->isEnabled() && cancelTask->isEnabled() && !runPipeline->isEnabled(), "and it can be cancelled");
				// The section the reader chose holds while more lines arrive.
				details->showSection(QStringLiteral("raw"));
				const qsizetype seen = section("log").size();
				expect(until([&] { return section("log").size() > seen + 60; }, 30000) && details->currentSectionId() == QStringLiteral("raw"),
					"streamed lines leave the reader on the section they chose", details->currentSectionId());
				cancelTask->trigger();
				expect(until([&] { return runPipeline->isEnabled(); }, 30000), "the cancelled build stops");
				settle();
				expect(rowState(QStringLiteral("Build Pipeline")) == QStringLiteral("cancelled") && section("raw").contains(QStringLiteral("State id: cancelled"))
						&& section("summary").contains(QStringLiteral("Cancelled by the user.")),
					"it ends Cancelled, by the user", section("summary"));
				expect(!QFileInfo::exists(bspPath) && !cancelTask->isEnabled(), "and wrote nothing");

				// Run Selected Compiler Profile: Cancel Running Task is offered as
				// soon as it starts, not after some other refresh happens by.
				phase = QStringLiteral("%1 %2%: a running compiler profile").arg(QLatin1String(pass.profile)).arg(pass.scale);
				for (int row = 0; row < profiles->count(); ++row) {
					if (profiles->item(row)->data(Qt::UserRole).toString() == QStringLiteral("slopmap2-bsp")) {
						profiles->setCurrentRow(row);
					}
				}
				expect(profiles->currentItem() && profiles->currentItem()->data(Qt::UserRole).toString() == QStringLiteral("slopmap2-bsp"), "the BSP profile is selected");
				runProfile->trigger();
				expect(!runProfile->isEnabled() && cancelTask->isEnabled(), "a running compiler profile offers Cancel Running Task at once",
					shell.statusBar()->currentMessage());
				expect(until([&] { return section("log").contains(QStringLiteral("working 1")); }, 30000), "its lines reach the Activity log too", section("log").right(400));
				cancelTask->trigger();
				expect(until([&] { return runProfile->isEnabled(); }, 30000), "the cancelled profile stops");
				settle();
				expect(rowState(QStringLiteral("Compiler Run")) == QStringLiteral("cancelled"), "and ends Cancelled", rowState(QStringLiteral("Compiler Run")));

				// Closing while a build runs: the user is asked first. Declined,
				// the window and the build stay; accepted, the build is told to
				// stop and the window closes once it has.
				phase = QStringLiteral("%1 %2%: closing during a build").arg(QLatin1String(pass.profile)).arg(pass.scale);
				runPipeline->trigger();
				expect(until([&] { return section("log").contains(QStringLiteral("working 1")); }, 30000), "the build to close on is under way");
				bool stopBuild = false;
				int asked = 0;
				QString question;
				QTimer closeAnswer;
				closeAnswer.setInterval(30);
				QObject::connect(&closeAnswer, &QTimer::timeout, &shell, [&] {
					auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
					if (!box || box->windowTitle() != QLatin1String("Stop the Build")) {
						return;
					}
					++asked;
					question = box->text();
					for (QAbstractButton* button : box->buttons()) {
						if (box->buttonRole(button) == (stopBuild ? QMessageBox::AcceptRole : QMessageBox::RejectRole)) {
							button->click();
							return;
						}
					}
				});
				closeAnswer.start();
				shell.close();
				expect(asked == 1 && question.contains(QStringLiteral("still running")), "closing during a build asks first", question);
				expect(shell.isVisible() && !runPipeline->isEnabled() && rowState(QStringLiteral("Build Pipeline")) == QStringLiteral("running"),
					"declined, the window and the build stay");
				stopBuild = true;
				discard.start();
				shell.close();
				expect(asked == 2 && shell.isVisible(), "accepted, the window waits for the build to stop", QString::number(asked));
				expect(until([&] { return !shell.isVisible(); }, 30000), "and closes once it has");
				expect(asked == 2 && !QFileInfo::exists(bspPath), "without asking again or leaving output behind", QString::number(asked));
				closeAnswer.stop();
				phase = QStringLiteral("%1 %2%").arg(QLatin1String(pass.profile)).arg(pass.scale);
			}
			truster->stop();
			delete truster;
			if (pass.scale == 200) {
				// The Activity summary is written in the interface language: under
				// the expanding translator every count and the separator between
				// them come back wrapped, which the model's plain English would not.
				QString summary;
				for (QLabel* label : shell.findChildren<QLabel*>()) {
					if (label->accessibleName().contains(QStringLiteral("Activity summary"))) {
						summary = label->text();
					}
				}
				expect(summary.startsWith(QLatin1Char('[')) && summary.contains(QStringLiteral(" completed~")) && !summary.contains(QStringLiteral("], ")),
					"the Activity summary is translated, counts and separators alike", summary);
			}

			discard.start();
			if (shell.isVisible()) {
				shell.close();
			}
			discard.stop();
			std::cout << "  pass done after " << clock.elapsed() << " ms" << std::endl;
		}
		if (pass.scale == 100) {
			qunsetenv("SLOPSTUDIO_PIPELINES_FAKE_SLOW");
			// Finished tasks are written to the settings a moment after they end
			// and, at the latest, as the window goes: each with its result, its
			// progress and its timing, and a note of the log lines left with
			// the session's Activity panel.
			bool kept = false;
			int builds = 0;
			for (const RecentActivityTask& task : StudioSettings().recentActivityTasks()) {
				if (task.title != QStringLiteral("Build Pipeline")) {
					continue;
				}
				++builds;
				kept = kept || (task.state == OperationState::Cancelled && task.durationMs > 0 && task.progress.total > 0
					&& task.resultSummary == QStringLiteral("Cancelled by the user.") && task.log.isEmpty() && task.logTruncated
					&& task.droppedLogEntryCount > 3);
			}
			expect(kept, "the cancelled build is in the recent activity with its result, progress and timing", QString::number(builds));
		}
		if (pass.scale == 200) {
			app.removeTranslator(&expanded);
			app.setLayoutDirection(Qt::LeftToRight);
		}
	}
	QEventLoop drain;
	QTimer::singleShot(200, &drain, &QEventLoop::quit);
	drain.exec();
	std::cout << (ok ? "Build pipelines UI smoke test passed." : "Build pipelines UI smoke test failed.") << std::endl;
	return ok ? 0 : 1;
}
