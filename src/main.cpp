#include "app/application_shell.h"
#include "app/studio_progress.h"
#include "app/studio_runtime.h"
#include "app/studio_splash.h"
#include "app/studio_theme.h"
#include "cli/cli.h"
#include "core/legacy_names.h"
#include "core/package_import_store.h"
#include "core/localization.h"
#include "core/package_copy_store.h"
#include "core/render_device.h"
#include "core/studio_manifest.h"
#include "core/studio_settings.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QString>
#include <QStringList>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QTimer>
#include <QScopeGuard>
#include <QSessionManager>
#include <QThreadPool>

#include <cstdio>
#include <iostream>
#include <memory>

#if defined(Q_OS_WIN)
// Windows only: the wide command line (CommandLineToArgvW) and the console
// output code page. Every use below is guarded, and other systems keep the
// portable path, where argv and the terminal are already UTF-8.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

// The brand icon resource lives in the app library (assets/branding/slopstudio.qrc).
// Q_INIT_RESOURCE must run from the global namespace, and calling it here makes
// the linker keep the resource in the executable.
static void initBrandingResources()
{
	Q_INIT_RESOURCE(slopstudio_branding);
}

namespace {

QStringList rawArguments(int argc, char** argv)
{
#if defined(Q_OS_WIN)
	// argv arrives in the ANSI code page, which replaces every character
	// outside it: a --settings-file or --open path in another script would
	// name a different file. The wide command line keeps them, and is what
	// QCoreApplication::arguments() reads on Windows too.
	int wideCount = 0;
	if (LPWSTR* wide = ::CommandLineToArgvW(::GetCommandLineW(), &wideCount)) {
		QStringList wideArgs;
		for (int index = 0; index < wideCount; ++index) {
			wideArgs.push_back(QString::fromWCharArray(wide[index]));
		}
		::LocalFree(wide);
		if (!wideArgs.isEmpty()) {
			return wideArgs;
		}
	}
#endif
	QStringList args;
	for (int index = 0; index < argc; ++index) {
		args.push_back(QString::fromLocal8Bit(argv[index]));
	}
	return args;
}

void configureApplicationMetadata(QCoreApplication& app)
{
	app.setOrganizationName(QStringLiteral("DarkMatterProductions"));
	app.setApplicationName(QStringLiteral("SlopStudio"));
	app.setApplicationVersion(slopstudio::versionString());
	// SlopStudio was called VibeStudio until 2026-11-15. On the first run after
	// the rename, carry the per-user settings and data folders over once. A
	// --settings-file run is isolated, so it never touches either store.
	if (slopstudio::StudioSettings::overrideFilePath().isEmpty()) {
		slopstudio::migrateLegacySettingsStore();
		slopstudio::migrateLegacyDataDirectories();
	}
}

// `--settings-file <path>` keeps automation, CI, and scripted runs away from
// the user's real preference store. It is resolved before any QSettings access
// so every later reader sees the override.
QString flagValue(const QStringList& args, const QString& flag)
{
	for (int index = 0; index < args.size(); ++index) {
		const QString& argument = args.at(index);
		if (argument == flag && index + 1 < args.size()) {
			return args.at(index + 1);
		}
		if (argument.startsWith(flag + QLatin1Char('='))) {
			return argument.mid(flag.size() + 1);
		}
	}
	return QString();
}

QString settingsFileOverride(const QStringList& args)
{
	return flagValue(args, QStringLiteral("--settings-file"));
}

// Whether `--settings-file` was given without a usable path: nothing after
// it, an empty or blank value (an unset shell variable), or another option
// where the path should be. Such a run must stop rather than fall through to
// the user's real preference store, which is the one thing the option exists
// to keep scripts away from.
bool settingsFileValueMissing(const QStringList& args)
{
	const QString flag = QStringLiteral("--settings-file");
	const QString inlinePrefix = flag + QLatin1Char('=');
	for (int index = 1; index < args.size(); ++index) {
		const QString& argument = args.at(index);
		QString value;
		if (argument == flag) {
			value = args.value(index + 1);
		} else if (argument.startsWith(inlinePrefix)) {
			value = argument.mid(inlinePrefix.size());
		} else {
			continue;
		}
		if (value.trimmed().isEmpty() || value.startsWith(QStringLiteral("--"))) {
			return true;
		}
	}
	return false;
}

// Every `--open <path>` in order. Each is handled exactly like a drop onto the
// window, so a map opens in Levels, an archive in Packages, and so on.
QStringList pathsToOpen(const QStringList& args)
{
	const QString flag = QStringLiteral("--open");
	QStringList paths;
	for (int index = 0; index < args.size(); ++index) {
		const QString& argument = args.at(index);
		if (argument == flag && index + 1 < args.size()) {
			paths.push_back(args.at(++index));
		} else if (argument.startsWith(flag + QLatin1Char('='))) {
			paths.push_back(argument.mid(flag.size() + 1));
		}
	}
	return paths;
}

// Whether a command-line run that draws in 3D can have a GUI application,
// which Qt needs before it offers OpenGL. On X11 and Wayland systems that
// takes a display; without one the run stays console-only and draws with
// Vulkan. macOS keeps the process out of the Dock, and a display that will
// not open falls back to the offscreen platform (Vulkan only) rather than
// ending the run.
bool prepareGraphicsCommandLine()
{
#if defined(Q_OS_MACOS)
	if (qEnvironmentVariableIsEmpty("QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM")) {
		qputenv("QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM", "1");
	}
	return true;
#elif defined(Q_OS_UNIX)
	const bool wayland = !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
	const bool x11 = !qEnvironmentVariableIsEmpty("DISPLAY");
	if (!qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
		return true;
	}
	if (!wayland && !x11) {
		return false;
	}
	QByteArrayList platforms;
	if (wayland) {
		platforms << QByteArrayLiteral("wayland");
	}
	if (x11) {
		platforms << QByteArrayLiteral("xcb");
	}
	platforms << QByteArrayLiteral("offscreen");
	qputenv("QT_QPA_PLATFORM", platforms.join(';'));
	return true;
#else
	return true;
#endif
}

} // namespace

int main(int argc, char** argv)
{
	const QStringList args = rawArguments(argc, argv);

	// Before anything reads settings, in the studio and on the command line
	// alike. Plain English: no application, and so no translator, exists yet.
	if (settingsFileValueMissing(args)) {
		std::fputs("--settings-file requires a path.\n", stderr);
		return 2;
	}
	const QString settingsOverride = settingsFileOverride(args);
	if (!settingsOverride.isEmpty()) {
		slopstudio::StudioSettings::setOverrideFilePath(settingsOverride);
	}

	if (args.contains(QStringLiteral("--cli"))) {
#if defined(Q_OS_WIN)
		// The command line writes UTF-8. A Windows console shows it as such
		// only while its output code page says so; the console's own code page
		// comes back when the command ends. Zero means there is no console.
		const UINT previousConsoleCodePage = ::GetConsoleOutputCP();
		const bool consoleCodePageChanged = previousConsoleCodePage != 0 && previousConsoleCodePage != CP_UTF8
			&& ::SetConsoleOutputCP(CP_UTF8) != 0;
#endif
		// Commands that draw in 3D get a GUI application without a window, so
		// they can use OpenGL as well as Vulkan; the rest stay console-only.
		std::unique_ptr<QCoreApplication> app;
		if (slopstudio::cli::commandUsesRenderer(args) && prepareGraphicsCommandLine()) {
			app = std::make_unique<QGuiApplication>(argc, argv);
			slopstudio::prepareRenderBackends();
		} else {
			app = std::make_unique<QCoreApplication>(argc, argv);
		}
		const auto imports = qScopeGuard([=] {
			QThreadPool::globalInstance()->waitForDone();
			slopstudio::waitForPackageImportCleanup();
			slopstudio::waitForPackageCopyCleanup();
#if defined(Q_OS_WIN)
			if (consoleCodePageChanged) {
				// Everything written so far was UTF-8: send it before the
				// console goes back to its own code page.
				std::cout.flush();
				std::cerr.flush();
				std::fflush(nullptr);
				::SetConsoleOutputCP(previousConsoleCodePage);
			}
#endif
		});
		configureApplicationMetadata(*app);
		return slopstudio::cli::run(app->arguments());
	}

	slopstudio::configureHighDpiBehavior();

	QApplication app(argc, argv);
	// Declared before the shell: first release document/worker ownership, then
	// finish queued import deletion and unlock, while Qt is still available.
	const auto imports = qScopeGuard([] {
		QThreadPool::globalInstance()->waitForDone();
		slopstudio::waitForPackageImportCleanup();
		slopstudio::waitForPackageCopyCleanup();
	});
	configureApplicationMetadata(app);
	// Matches packaging/linux/*.desktop, so Wayland and freedesktop launchers
	// pair the window with the installed icon.
	QGuiApplication::setDesktopFileName(QStringLiteral("io.github.themuffinator.SlopStudio"));
	initBrandingResources();
	QApplication::setWindowIcon(slopstudio::studioApplicationIcon());
	slopstudio::installSessionLogging();

	// The offscreen platform (CI self-test, documentation snapshots) has no
	// system UI font, so pick a real one when it is installed.
	if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
		// Its font database reads only .ttf, .otf, and Type 1 files from
		// QT_QPA_FONTDIR, leaving out TrueType collections, which is where
		// Windows keeps its Chinese, Japanese, and Indic interface fonts.
		const QString fontDirectory = qEnvironmentVariable("QT_QPA_FONTDIR");
		if (!fontDirectory.isEmpty()) {
			const QFileInfoList collections = QDir(fontDirectory).entryInfoList({QStringLiteral("*.ttc")}, QDir::Files);
			for (const QFileInfo& collection : collections) {
				QFontDatabase::addApplicationFont(collection.absoluteFilePath());
			}
		}
		for (const QString& family : {QStringLiteral("Segoe UI"), QStringLiteral("Helvetica Neue"), QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans")}) {
			if (QFontDatabase::hasFamily(family)) {
				QFont font = QApplication::font();
				font.setFamily(family);
				QApplication::setFont(font);
				break;
			}
		}
	}

	// A self-test or a snapshot run is not a working session: it starts clean,
	// records nothing, and neither arms crash capture nor consumes the marker a
	// real session that crashed left behind.
	const bool interactive = !args.contains(QStringLiteral("--self-test")) && flagValue(args, QStringLiteral("--ui-snapshot")).isEmpty();

	// The start-up screen is up before the window is built, and names each
	// stage of start-up as it comes (docs/UX_DESIGN.md, "Visible Work"). A
	// scripted run or a headless platform has nobody to show it to, and
	// `--no-splash` leaves it out on request.
	QPointer<slopstudio::StudioSplash> splash;
	const bool showSplash = interactive && !args.contains(QStringLiteral("--no-splash")) && slopstudio::StudioSplash::platformShowsSplash();

	{
		slopstudio::StudioSettings settings;
		const slopstudio::AccessibilityPreferences preferences = settings.accessibilityPreferences();
		slopstudio::installStudioTranslations(app, preferences.localeName);
		slopstudio::applyLayoutDirectionForLocale(preferences.localeName);
		// Numbers, dates, and sizes follow the region format preference, which
		// is the operating system's regional settings unless the user chose.
		QLocale::setDefault(slopstudio::regionFormatLocale(preferences.formatLocaleName, preferences.localeName));
		// Theme before the first widget exists, so nothing is built with the
		// platform style and then repolished.
		slopstudio::applyStudioTheme(app, slopstudio::studioThemeTokens(preferences));
		if (showSplash) {
			splash = new slopstudio::StudioSplash;
			// The shell's stages, the renderer before it, the session after.
			splash->setExpectedStages(slopstudio::ApplicationShell::startupStageCount() + 2);
			splash->setReducedMotion(preferences.reducedMotion);
			splash->listenForStartupStages();
			splash->present();
		}
		slopstudio::reportStartupStage(QCoreApplication::translate("SlopStudioMain", "Starting the 3D renderer"));
		// The 3D renderer the user chose, starting on its own thread now so
		// the first 3D view does not wait for it.
		slopstudio::RenderBackendChoice renderer = slopstudio::RenderBackendChoice::Automatic;
		slopstudio::renderBackendChoiceFromId(settings.renderBackendPreference(), &renderer);
		slopstudio::setRenderBackendChoice(renderer);
		slopstudio::prepareRenderBackends(true);
		// Before the shell exists, so it can tell whether the last session
		// crashed. Turned off in Preferences, nothing is written, though a
		// crash from before is still offered once.
		if (interactive) {
			slopstudio::CrashHandlerOptions crashOptions;
			crashOptions.enabled = settings.crashReports();
			// A run with its own settings file is a scripted or isolated one: its
			// markers and reports stay beside that file, never the user's.
			if (!settingsOverride.isEmpty()) {
				crashOptions.directory = QFileInfo(settingsOverride).absoluteDir().filePath(QStringLiteral("crash-reports"));
			}
			slopstudio::installCrashHandling(crashOptions);
		}
	}

	slopstudio::ApplicationShell shell;
	shell.show();

#ifndef QT_NO_SESSIONMANAGER
	// Logging off or shutting the system down ends the process without a close
	// event or atexit, so nothing would ask about unsaved work, the window
	// state would be lost, and the next start would report a crash. When the
	// session manager lets the studio ask, close the window as the user would:
	// its own guards save, discard or refuse. A refusal cancels the shutdown;
	// otherwise the session is recorded as ended cleanly at once, because the
	// system may end the process before main() returns. Direct, as Qt requires
	// for this signal: the answer is read when the slot returns.
	QObject::connect(&app, &QGuiApplication::commitDataRequest, &shell, [&shell](QSessionManager& manager) {
		if (!manager.allowsInteraction()) {
			return;
		}
		const bool closed = shell.close();
		manager.release();
		if (closed) {
			slopstudio::markSessionEndedCleanly();
		} else {
			manager.cancel();
		}
	}, Qt::DirectConnection);
#endif

	const QStringList paths = pathsToOpen(args);
	if (!paths.isEmpty()) {
		slopstudio::reportStartupStage(QCoreApplication::translate("SlopStudioMain", "Opening the files from the command line"));
	}
	for (const QString& path : paths) {
		shell.openPathFromCommandLine(path);
	}
	// Once the window is up the session begins: a crash last time is offered
	// back; otherwise the last session reopens, unless a path given on the
	// command line already says what the user wants. The start-up screen
	// stays over the window until that is done, naming it.
	if (interactive) {
		const bool reopenLast = paths.isEmpty();
		QTimer::singleShot(0, &shell, [&shell, splash, reopenLast]() {
			if (reopenLast) {
				slopstudio::reportStartupStage(QCoreApplication::translate("SlopStudioMain", "Restoring your session"));
			}
			shell.beginSession(reopenLast);
			if (splash) {
				splash->finish(&shell);
			}
		});
	} else if (splash) {
		splash->finish(&shell);
	}

	// `--ui-snapshot <dir>` renders every work surface to PNG and exits. Run it
	// under the offscreen platform plugin with an isolated --settings-file to
	// produce documentation screenshots without touching the user's session.
	const QString snapshotDirectory = flagValue(args, QStringLiteral("--ui-snapshot"));
	if (!snapshotDirectory.isEmpty()) {
		const QString sizeText = flagValue(args, QStringLiteral("--ui-snapshot-size"));
		const QStringList sizeParts = sizeText.split(QLatin1Char('x'));
		if (sizeParts.size() == 2 && sizeParts.at(0).toInt() > 0 && sizeParts.at(1).toInt() > 0) {
			shell.resize(sizeParts.at(0).toInt(), sizeParts.at(1).toInt());
		}
		QTimer::singleShot(0, &app, [&shell, snapshotDirectory]() {
			const QStringList written = shell.captureUiSnapshots(snapshotDirectory);
			for (const QString& path : written) {
				qInfo("%s", qPrintable(path));
			}
			QCoreApplication::exit(written.isEmpty() ? 1 : 0);
		});
		return app.exec();
	}

	// `--self-test` builds the whole shell, pumps the event loop briefly, then
	// exits. It gives CI a real GUI smoke check (under an offscreen platform
	// plugin) instead of only ever exercising the CLI.
	if (args.contains(QStringLiteral("--self-test"))) {
		QTimer::singleShot(0, &app, [&shell]() {
			shell.runSelfTest();
		});
		QTimer::singleShot(2500, &app, []() {
			QCoreApplication::exit(0);
		});
	}

	const int exitCode = app.exec();
	// A language change asked for a restart, and the shell has closed through
	// its unsaved-work guards: the new studio starts in the new language.
	slopstudio::startRequestedStudioRestart();
	return exitCode;
}
