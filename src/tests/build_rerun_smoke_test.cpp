// Run Last Build, Re-run Last Compile and the launch settings of the user's
// own, below the GUI: the newest saved compiler run of a map, a replay of it,
// user launch profiles (argument groups, tokens, validation, JSON), the extra
// launch arguments, the settings that remember the last build and the Levels
// profile, and the `compiler rerun --last`, `launch profiles`,
// `launch profile-set`, `launch profile-remove`, `launch extra-args` and
// `launch plan` routes.
#include "core/build_pipeline.h"
#include "core/compiler_runner.h"
#include "core/editor_profiles.h"
#include "core/level_editor_controls.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>

#include <iostream>

using namespace vibestudio;

namespace {
bool ok = true;

bool expect(bool pass, const char* label, const QString& detail = {})
{
	if (!pass) {
		ok = false;
		std::cerr << "FAIL: " << label << ": " << qPrintable(detail) << std::endl;
	}
	return pass;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

// A stand-in for vibemap2-bsp: writes a minimal Quake BSP (version 29 and an
// empty lump directory) to the .bsp it is given, or beside the .map.
int fakeCompiler(const QStringList& arguments)
{
	QString output;
	for (const QString& argument : arguments) {
		if (argument.endsWith(QStringLiteral(".bsp"), Qt::CaseInsensitive)) {
			output = argument;
		} else if (output.isEmpty() && argument.endsWith(QStringLiteral(".map"), Qt::CaseInsensitive)) {
			const QFileInfo map(argument);
			output = map.absolutePath() + QLatin1Char('/') + map.completeBaseName() + QStringLiteral(".bsp");
		}
	}
	QByteArray bytes(4 + 15 * 8, '\0');
	qToLittleEndian<qint32>(29, reinterpret_cast<uchar*>(bytes.data()));
	std::cout << "fake compiler run " << qPrintable(QFileInfo(output).fileName()) << std::endl;
	return !output.isEmpty() && writeFile(output, bytes) ? 0 : 1;
}

QByteArray mapText()
{
	return QByteArrayLiteral("{\n\"classname\" \"worldspawn\"\n{\n( 0 0 0 ) ( 0 1 0 ) ( 1 0 0 ) base/floor 0 0 0 1 1\n"
							 "( 0 0 64 ) ( 1 0 64 ) ( 0 1 64 ) base/floor 0 0 0 1 1\n( 0 0 0 ) ( 1 0 0 ) ( 0 0 1 ) base/floor 0 0 0 1 1\n"
							 "( 0 64 0 ) ( 0 64 1 ) ( 1 64 0 ) base/floor 0 0 0 1 1\n( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) base/floor 0 0 0 1 1\n"
							 "( 64 0 0 ) ( 64 1 0 ) ( 64 0 1 ) base/floor 0 0 0 1 1\n}\n}\n{\n\"classname\" \"info_player_start\"\n\"origin\" \"32 32 24\"\n}\n");
}

// One saved run of `mapPath`, written where Run Profile writes it.
CompilerRunResult compileOnce(const QString& root, const QString& mapPath)
{
	CompilerRunRequest request;
	request.command.profileId = QStringLiteral("vibemap2-bsp");
	request.command.inputPath = mapPath;
	request.command.workingDirectory = QFileInfo(mapPath).absolutePath();
	request.command.extraArguments = {QStringLiteral("--fake-compiler")};
	request.command.executableOverrides.push_back({QStringLiteral("vibemap2-bsp"), QCoreApplication::applicationFilePath()});
	request.manifestPath = newCompilerRunManifestPath(root, request.command.profileId);
	request.timeoutMs = 60000;
	return runCompilerCommand(request);
}

void setModified(const QString& path, int secondsAgo)
{
	QFile file(path);
	if (file.open(QIODevice::ReadWrite)) {
		file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-secondsAgo), QFileDevice::FileModificationTime);
	}
}

void manifestTests(const QString& folder)
{
	const QString root = QDir(folder).filePath(QStringLiteral("project"));
	const QString start = QDir(root).filePath(QStringLiteral("maps/start.map"));
	const QString other = QDir(root).filePath(QStringLiteral("maps/other.map"));
	expect(writeFile(start, mapText()) && writeFile(other, mapText()), "maps written");
	expect(compilerRunManifestDirectory(root) == QDir::cleanPath(QDir(root).filePath(QStringLiteral(".vibestudio/compiler-runs"))), "runs folder");
	expect(compilerRunManifestDirectory(QString()).isEmpty() && newCompilerRunManifestPath(QString(), QStringLiteral("x")).isEmpty(), "no root, no folder");
	CompilerRunManifestMatch match;
	expect(!findNewestCompilerRunManifest({root}, start, &match), "nothing saved yet");

	const CompilerRunResult first = compileOnce(root, start);
	expect(first.started && QFileInfo::exists(first.manifestPath), "first run saved", compilerRunResultText(first));
	const CompilerRunResult second = compileOnce(root, other);
	const CompilerRunResult third = compileOnce(root, start);
	expect(third.manifestPath != first.manifestPath && QFileInfo::exists(third.manifestPath), "a second run in the same second keeps both", third.manifestPath);
	setModified(first.manifestPath, 300);
	setModified(second.manifestPath, 100);
	setModified(third.manifestPath, 200);
	// A file that is not a manifest is passed over.
	expect(writeFile(QDir(compilerRunManifestDirectory(root)).filePath(QStringLiteral("zz-broken.json")), "{ not json"), "broken file");

	expect(findNewestCompilerRunManifest({root}, start, &match) && QFileInfo(match.path) == QFileInfo(third.manifestPath)
			&& match.manifest.profileId == QStringLiteral("vibemap2-bsp"),
		"the newest run of start.map", match.path);
	expect(findNewestCompilerRunManifest({root}, QString(), &match) && QFileInfo(match.path) == QFileInfo(second.manifestPath), "the newest run of any map",
		match.path);
	expect(findNewestCompilerRunManifest({QDir(root).filePath(QStringLiteral("elsewhere")), root, root}, QDir(root).filePath(QStringLiteral("maps/../maps/start.map")),
			   &match)
			&& QFileInfo(match.path) == QFileInfo(third.manifestPath),
		"other spellings of the map and repeated roots", match.path);
	expect(!findNewestCompilerRunManifest({root}, QDir(root).filePath(QStringLiteral("maps/missing.map"))), "no run of another map");
	expect(!findNewestCompilerRunManifest({root}, start, nullptr, 0), "the scan limit is kept");

	// The replay repeats the stored command and becomes the newest run.
	CompilerManifestRerunRequest rerun;
	rerun.manifestPath = newCompilerRunManifestPath(root, match.manifest.profileId);
	rerun.timeoutMs = 60000;
	const CompilerRunResult replayed = rerunCompilerCommandManifest(match.manifest, rerun);
	expect(replayed.started && replayed.manifest.commandLine == match.manifest.commandLine && QFileInfo::exists(rerun.manifestPath), "replay runs the same command",
		compilerRunResultText(replayed));
	expect(findNewestCompilerRunManifest({root}, start, &match) && QFileInfo(match.path) == QFileInfo(rerun.manifestPath), "the replay is the newest run now",
		match.path);
}

void profileTests(const QString& folder)
{
	const QVector<QStringList> groups = gameLaunchArgumentGroups(QStringLiteral("+set fs_game {mod} +set hostname \"My Server\" +set r_mode -1 +devmap {map}"));
	expect(groups.size() == 4 && groups[0] == QStringList({"+set", "fs_game", "{mod}"}) && groups[1] == QStringList({"+set", "hostname", "My Server"})
			&& groups[2] == QStringList({"+set", "r_mode", "-1"}) && groups[3] == QStringList({"+devmap", "{map}"}),
		"switch groups, quotes and numbers", QString::number(groups.size()));
	expect(splitGameLaunchArguments(QStringLiteral("-window +set name \"a b\"")) == QStringList({"-window", "+set", "name", "a b"}), "extra arguments split");
	expect(gameLaunchTokens() == QStringList({"map", "mod", "basedir", "bsp", "iwad", "warp"}), "tokens");

	expect(userGameLaunchProfileId(QStringLiteral("My Dev Profile!"), {}) == QStringLiteral("user-my-dev-profile"), "id from a name");
	GameLaunchProfile profile;
	profile.userDefined = true;
	profile.id = QStringLiteral("user-dev");
	profile.displayName = QStringLiteral("Developer");
	profile.engineFamily = QStringLiteral("idTech3");
	profile.argumentLine = QStringLiteral("+set fs_basepath {basedir} +set fs_game {mod} +set hostname \"My Server\" +devmap {map} +set bsp {bsp}");
	expect(userGameLaunchProfileProblem(profile).isEmpty(), "a good profile", userGameLaunchProfileProblem(profile));
	expect(userGameLaunchProfileId(QStringLiteral("dev"), {profile}) == QStringLiteral("user-dev-2"), "ids stay unique");
	expect(userGameLaunchProfileId(QStringLiteral("!!!"), {}) == QStringLiteral("user-profile"), "a name without letters");
	auto broken = profile;
	broken.id = QStringLiteral("custom");
	expect(!userGameLaunchProfileProblem(broken).isEmpty(), "a built-in id is refused");
	broken = profile;
	broken.id = QStringLiteral("User Dev");
	expect(!userGameLaunchProfileProblem(broken).isEmpty(), "an id with spaces is refused");
	broken = profile;
	broken.displayName.clear();
	expect(!userGameLaunchProfileProblem(broken).isEmpty(), "a profile needs a name");
	broken = profile;
	broken.argumentLine = QStringLiteral("+map {mapname}");
	expect(userGameLaunchProfileProblem(broken).contains(QStringLiteral("{mapname}")), "an unknown token is named", userGameLaunchProfileProblem(broken));
	broken = profile;
	broken.argumentLine = QStringLiteral("+set name \"open");
	expect(!userGameLaunchProfileProblem(broken).isEmpty(), "an unclosed quote is refused");
	broken = profile;
	broken.engineFamily = QStringLiteral("idTech9");
	expect(!userGameLaunchProfileProblem(broken).isEmpty(), "an unknown engine is refused");

	GameLaunchProfile loaded;
	expect(gameLaunchProfileFromJson(gameLaunchProfileJson(profile), &loaded) && loaded.id == profile.id && loaded.argumentLine == profile.argumentLine
			&& loaded.userDefined && loaded.engineFamily == profile.engineFamily && loaded.displayName == profile.displayName,
		"JSON round trip");
	expect(!gameLaunchProfileFromJson(gameLaunchProfileJson(broken), &loaded), "a bad profile does not load");
	GameLaunchProfile found;
	expect(gameLaunchProfileForId(QStringLiteral("user-dev"), {profile}, &found) && found.userDefined && !gameLaunchProfileForId(QStringLiteral("user-dev"))
			&& gameLaunchProfileForId(QStringLiteral("quake3-source-port"), {profile}, &found) && !found.userDefined,
		"user profiles are found first, built-in ones still");

	// A plan with the profile: no game folder drops the fs_game group quietly,
	// the quoted hostname stays one argument, extra arguments come last.
	GameInstallationProfile installation;
	installation.id = QStringLiteral("rerun-fixture");
	installation.gameKey = QStringLiteral("quake3");
	installation.engineFamily = GameEngineFamily::IdTech3;
	installation.rootPath = QDir(folder).filePath(QStringLiteral("game root"));
	QDir().mkpath(installation.rootPath);
	installation.executablePath = QCoreApplication::applicationFilePath();
	GameLaunchRequest request;
	request.launchProfileId = profile.id;
	request.userProfiles = {profile};
	request.mapName = QStringLiteral("start");
	request.baseDirectory = installation.rootPath;
	request.extraArguments = splitGameLaunchArguments(QStringLiteral("+set developer 1"));
	GameLaunchPlan plan = buildGameLaunchPlan(request, installation);
	const QStringList expected {"+set", "fs_basepath", installation.rootPath, "+set", "hostname", "My Server", "+devmap", "start", "+set", "developer", "1"};
	expect(plan.profileFound && plan.profile.userDefined && plan.arguments == expected, "user profile arguments", plan.arguments.join(QStringLiteral(" | ")));
	expect(plan.warnings.join(' ').contains(QStringLiteral("bsp")) && !plan.warnings.join(' ').contains(QStringLiteral("fs_game")),
		"a missing {bsp} warns, a missing {mod} does not", plan.warnings.join('\n'));
	request.modDirectory = QStringLiteral("mymod");
	plan = buildGameLaunchPlan(request, installation);
	expect(plan.arguments.mid(3, 3) == QStringList({"+set", "fs_game", "mymod"}), "a game folder fills {mod}", plan.arguments.join(' '));
	auto unknown = profile;
	unknown.argumentLine = QStringLiteral("+map {mapname}");
	request.userProfiles = {unknown};
	plan = buildGameLaunchPlan(request, installation);
	expect(plan.arguments.contains(QStringLiteral("{mapname}")) && plan.warnings.join(' ').contains(QStringLiteral("{mapname}")),
		"an unknown token is passed as written, with a warning", plan.warnings.join('\n'));
	// Built-in templates resolve as before.
	request.launchProfileId = QStringLiteral("quake3-source-port");
	plan = buildGameLaunchPlan(request, installation);
	expect(plan.arguments.mid(0, 2) == QStringList({"+set", "fs_basepath"}) && plan.arguments.contains(QStringLiteral("+devmap")), "built-in profile unchanged",
		plan.arguments.join(' '));
}

void settingsTests(const QString& folder)
{
	StudioSettings::setOverrideFilePath(QDir(folder).filePath(QStringLiteral("core-settings.ini")));
	{
		StudioSettings settings;
		settings.setLaunchExtraArguments(QStringLiteral("quake-1"), QStringLiteral(" +set developer 1 "));
		settings.setLaunchExtraArguments(QStringLiteral("quake3-2"), QStringLiteral("+set a,b \"c d\""));
		settings.setLaunchExtraArguments(QStringLiteral("quake3-2"), QString());
		settings.setLaunchExtraArguments(QStringLiteral("bad/id"), QStringLiteral("ignored"));
		GameLaunchProfile profile;
		profile.id = QStringLiteral("user-dev");
		profile.displayName = QStringLiteral("Developer, with commas");
		profile.argumentLine = QStringLiteral("+set hostname \"A, B\" +map {map}");
		GameLaunchProfile bad = profile;
		bad.id = QStringLiteral("custom");
		settings.setUserGameLaunchProfiles({profile, bad});
		StudioBuildRunRecord record;
		record.kind = QStringLiteral("pipeline");
		record.id = QStringLiteral("quake-full");
		record.launch = true;
		record.inputPath = QStringLiteral("C:/maps/start.map");
		settings.setLastBuildRun(QDir(folder).filePath(QStringLiteral("projectA")), record);
		record.kind = QStringLiteral("profile");
		record.id = QStringLiteral("vibemap2-bsp");
		record.launch = false;
		settings.setLastBuildRun(QString(), record);
		StudioBuildRunRecord invalid;
		invalid.kind = QStringLiteral("other");
		invalid.id = QStringLiteral("x");
		settings.setLastBuildRun(QDir(folder).filePath(QStringLiteral("projectB")), invalid);
		settings.setLevelCompilerProfile(QStringLiteral("quake2"), QStringLiteral("vibemap2-bsp"));
		settings.setLevelCompilerProfile(QStringLiteral("quake"), QStringLiteral("vibemap2-light"));
		settings.setLevelCompilerProfile(QStringLiteral("quake"), QString());
		settings.sync();
	}
	StudioSettings reloaded;
	expect(reloaded.launchExtraArguments(QStringLiteral("quake-1")) == QStringLiteral("+set developer 1"), "extra arguments per installation");
	expect(reloaded.launchExtraArguments(QStringLiteral("quake3-2")).isEmpty() && reloaded.launchExtraArguments(QStringLiteral("bad/id")).isEmpty(),
		"cleared and refused extra arguments");
	const QVector<GameLaunchProfile> profiles = reloaded.userGameLaunchProfiles();
	expect(profiles.size() == 1 && profiles[0].id == QStringLiteral("user-dev") && profiles[0].displayName == QStringLiteral("Developer, with commas")
			&& profiles[0].argumentLine == QStringLiteral("+set hostname \"A, B\" +map {map}") && profiles[0].userDefined,
		"user profiles persist; a bad one is not kept", QString::number(profiles.size()));
	const StudioBuildRunRecord a = reloaded.lastBuildRun(QDir(folder).filePath(QStringLiteral("projectA")));
	expect(a.isValid() && a.kind == QStringLiteral("pipeline") && a.id == QStringLiteral("quake-full") && a.launch && a.inputPath == QStringLiteral("C:/maps/start.map")
			&& a.timeUtc.isValid(),
		"the last build of a project");
	expect(reloaded.lastBuildRun(QDir(folder).filePath(QStringLiteral("projectA/../projectA"))).id == QStringLiteral("quake-full"), "the same project, spelt another way");
	const StudioBuildRunRecord none = reloaded.lastBuildRun(QString());
	expect(none.kind == QStringLiteral("profile") && none.id == QStringLiteral("vibemap2-bsp") && !none.launch, "the last build with no project");
	expect(!reloaded.lastBuildRun(QDir(folder).filePath(QStringLiteral("projectB"))).isValid(), "an invalid record is not kept");
	expect(reloaded.levelCompilerProfile(QStringLiteral("QUAKE2")) == QStringLiteral("vibemap2-bsp") && reloaded.levelCompilerProfile(QStringLiteral("quake")).isEmpty(),
		"the Levels profile per game");
}

// F5 runs the last build in NetRadiant Custom (Build_runRecentExecutedBuild)
// and in NetRadiant, which binds it and Map Info's M too; Build and Launch
// gives F5 up there. Other profiles keep F5 for Build and Launch.
void keyTests()
{
	const auto keysOf = [](const char* profile, const char* command, bool* bound = nullptr) {
		if (bound) {
			*bound = false;
		}
		for (const LevelEditorKeyBinding& binding : levelEditorControlsForProfile(QString::fromLatin1(profile)).keys) {
			if (binding.commandId == QLatin1String(command)) {
				if (bound) {
					*bound = true;
				}
				return binding.keys;
			}
		}
		return QStringList();
	};
	for (const char* profile : {"netradiant-custom", "netradiant"}) {
		bool cleared = false;
		expect(keysOf(profile, "build.runLast") == QStringList {QStringLiteral("F5")}, "F5 runs the last build", QString::fromLatin1(profile));
		expect(keysOf(profile, "build.runAndLaunch", &cleared).isEmpty() && cleared, "Build and Launch gives F5 up", QString::fromLatin1(profile));
		expect(keysOf(profile, "map.mapInfo") == QStringList {QStringLiteral("M")}, "M opens Map Info", QString::fromLatin1(profile));
	}
	for (const char* profile : {"trenchbroom", "vibestudio-default", "gtkradiant-1-6", "quark"}) {
		bool bound = true;
		keysOf(profile, "build.runLast", &bound);
		expect(!bound, "no key for Run Last Build", QString::fromLatin1(profile));
	}
}

void cliTests(const QString& executable, const QString& folder)
{
	const QString settingsFile = QDir(folder).filePath(QStringLiteral("cli-settings.ini"));
	const auto run = [&](const QStringList& args, int code = 0) {
		QProcess process;
		process.setWorkingDirectory(folder);
		process.start(executable, QStringList {"--cli", "--json", "--settings-file", settingsFile} + args);
		const bool done = process.waitForStarted() && process.waitForFinished(120000);
		const auto out = process.readAllStandardOutput();
		const auto err = process.readAllStandardError();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code, "CLI status",
			args.join(' ') + QStringLiteral(" -> %1\n").arg(process.exitCode()) + QString::fromUtf8(out + err));
		if (!done) {
			process.kill();
			process.waitForFinished();
		}
		return QJsonDocument::fromJson(out).object();
	};

	// Launch profiles.
	QJsonObject result = run({"launch", "profiles"});
	expect(result["builtIn"].toArray().size() >= 5 && result["user"].toArray().isEmpty() && result["tokens"].toArray().size() == 6, "launch profiles lists");
	const QString line = QStringLiteral("+set fs_game {mod} +set hostname \"My Server\" +devmap {map}");
	result = run({"launch", "profile-set", "user-dev", "--name", "Developer", "--arguments", line, "--engine", "idTech3", "--dry-run"});
	expect(result["dryRun"].toBool() && !result["saved"].toBool(), "profile-set dry run");
	expect(run({"launch", "profiles"})["user"].toArray().isEmpty(), "a dry run saves nothing");
	result = run({"launch", "profile-set", "user-dev", "--name", "Developer", "--arguments", line, "--engine", "idTech3"});
	expect(result["profile"].toObject()["arguments"].toString() == line && !result["replaced"].toBool(), "profile added");
	result = run({"launch", "profile-set", "user-dev", "--name", "Developer", "--arguments", line, "--engine", "quake3"});
	expect(result["replaced"].toBool(), "profile replaced");
	run({"launch", "profile-set", "custom", "--name", "Mine", "--arguments", "+map {map}"}, 4);
	run({"launch", "profile-set", "user-x", "--name", "X", "--arguments", "+map {mapname}"}, 4);
	run({"launch", "profile-set", "user-y", "--name", "developer", "--arguments", "+map {map}"}, 4);
	run({"launch", "profile-set", "user-z", "--arguments", "+map {map}"}, 2);
	run({"launch", "profile-set", "user-z", "--name", "Z", "--arguments", "+map {map}", "--engine", "idTech9"}, 2);
	const QJsonArray mine = run({"launch", "profiles"})["user"].toArray();
	expect(mine.size() == 1 && mine[0].toObject()["id"] == QStringLiteral("user-dev") && mine[0].toObject()["engineFamily"] == QStringLiteral("idTech3"),
		"one profile of your own");

	// An installation to launch, and its extra arguments.
	const QString gameRoot = QDir(folder).filePath(QStringLiteral("q3"));
	QDir().mkpath(gameRoot);
	run({"install", "add", gameRoot, "--install-game", "quake3", "--install-engine", "idtech3", "--install-name", "Rerun fixture", "--install-executable",
		QCoreApplication::applicationFilePath()});
	result = run({"launch", "extra-args"});
	const QString installation = result["installationId"].toString();
	expect(!installation.isEmpty() && result["extraArguments"].toString().isEmpty() && !result["changed"].toBool(), "no extra arguments at first");
	result = run({"launch", "extra-args", "--arguments", "+set developer 1", "--dry-run"});
	expect(result["dryRun"].toBool() && result["arguments"].toArray().size() == 3, "extra-args dry run");
	expect(run({"launch", "extra-args"})["extraArguments"].toString().isEmpty(), "the dry run saved nothing");
	result = run({"launch", "extra-args", "--installation", installation, "--arguments", "+set developer 1"});
	expect(result["changed"].toBool() && result["extraArguments"].toString() == QStringLiteral("+set developer 1"), "extra arguments saved");
	run({"launch", "extra-args", "--installation", "nobody"}, 3);
	run({"launch", "extra-args", "--arguments", "x", "--clear"}, 2);
	run({"launch", "extra-args", "--arguments", "+set a \"b"}, 4);

	result = run({"launch", "plan", "--map", "start", "--launch-profile", "user-dev"});
	QStringList arguments;
	for (const QJsonValue& value : result["plan"].toObject()["arguments"].toArray()) {
		arguments << value.toString();
	}
	expect(arguments == QStringList({"+set", "hostname", "My Server", "+devmap", "start", "+set", "developer", "1"}), "launch plan uses the profile and saved arguments",
		arguments.join(QStringLiteral(" | ")));
	result = run({"launch", "plan", "--map", "start", "--launch-profile", "user-dev", "--mod", "mymod", "--extra-args", "+set sv_cheats \"1 2\""});
	arguments.clear();
	for (const QJsonValue& value : result["plan"].toObject()["arguments"].toArray()) {
		arguments << value.toString();
	}
	expect(arguments == QStringList({"+set", "fs_game", "mymod", "+set", "hostname", "My Server", "+devmap", "start", "+set", "sv_cheats", "1 2"}),
		"--extra-args replaces the saved ones, quotes keep spaces", arguments.join(QStringLiteral(" | ")));
	result = run({"launch", "extra-args", "--clear"});
	expect(result["changed"].toBool() && result["extraArguments"].toString().isEmpty(), "extra arguments cleared");

	run({"launch", "profile-remove", "user-dev", "--dry-run"});
	expect(run({"launch", "profiles"})["user"].toArray().size() == 1, "remove dry run keeps it");
	run({"launch", "profile-remove", "user-dev"});
	expect(run({"launch", "profiles"})["user"].toArray().isEmpty(), "profile removed");
	run({"launch", "profile-remove", "quake-source-port"}, 3);
	run({"launch", "plan", "--map", "start", "--launch-profile", "user-dev"}, 5);

	// compiler rerun --last: the map's newest saved run, replayed beside it.
	const QString project = QDir(folder).filePath(QStringLiteral("project"));
	const QString start = QDir(project).filePath(QStringLiteral("maps/start.map"));
	CompilerRunManifestMatch newest;
	expect(findNewestCompilerRunManifest({project}, start, &newest), "a saved run to replay");
	const auto runCount = [&] { return static_cast<int>(QDir(compilerRunManifestDirectory(project)).entryList({QStringLiteral("*.json")}, QDir::Files).size()); };
	const int before = runCount();
	// The saved run names this test program, which is not a compiler of the
	// user's own: the replay is refused with its command line until trusted.
	result = run({"compiler", "rerun", "--last", start, "--workspace-root", project}, 4);
	expect(result["programTrust"].toObject()["needsConfirmation"].toBool() && result["message"].toString().contains(QStringLiteral("--fake-compiler"))
			&& runCount() == before,
		"an unknown program is not replayed", result["message"].toString());
	result = run({"compiler", "rerun", "--last", start, "--workspace-root", project, "--trust-programs"});
	expect(QFileInfo(result["replayedManifestPath"].toString()) == QFileInfo(newest.path) && result["run"].toObject()["started"].toBool(),
		"compiler rerun --last replays the newest run", QString::fromUtf8(QJsonDocument(result).toJson()));
	const int after = runCount();
	expect(after == before + 1, "the replay is saved beside it", QStringLiteral("%1 -> %2").arg(before).arg(after));
	result = run({"compiler", "rerun", "--last", start, "--workspace-root", project});
	expect(result["run"].toObject()["started"].toBool() && runCount() == after + 1, "trusted for the project, it replays without asking");
	// A configured compiler needs no trust.
	run({"compiler", "set-path", "vibemap2-bsp", "--executable", QCoreApplication::applicationFilePath()});
	result = run({"compiler", "rerun", newest.path});
	expect(result["run"].toObject()["started"].toBool(), "a Toolchain path replays without asking");
	run({"compiler", "rerun", "--last", QDir(project).filePath(QStringLiteral("maps/missing.map"))}, 3);
	run({"compiler", "rerun", "--last"}, 2);
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	if (app.arguments().contains(QStringLiteral("--fake-compiler"))) {
		return fakeCompiler(app.arguments());
	}
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "temporary folder")) {
		return 1;
	}
	manifestTests(temp.path());
	profileTests(temp.path());
	settingsTests(temp.path());
	keyTests();
	if (argc >= 2) {
		cliTests(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath(), temp.path());
	}
	std::cout << (ok ? "Build rerun smoke test passed." : "Build rerun smoke test failed.") << std::endl;
	return ok ? 0 : 1;
}
