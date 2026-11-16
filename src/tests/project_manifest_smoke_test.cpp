#include "core/game_installation.h"
#include "core/project_manifest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

} // namespace

int main()
{
	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected temporary project directory.");
	}

	QDir root(tempDir.path());
	if (!root.mkpath(QStringLiteral("src")) || !root.mkpath(QStringLiteral("packages")) || !root.mkpath(QStringLiteral("build"))) {
		return fail("Expected project fixture directories.");
	}

	slopstudio::ProjectManifest manifest = slopstudio::defaultProjectManifest(tempDir.path(), QStringLiteral("Manifest Test"));
	manifest.sourceFolders = {QStringLiteral("src")};
	manifest.packageFolders = {QStringLiteral("packages")};
	manifest.selectedInstallationId = QStringLiteral("quake-test");
	manifest.compilerSearchPaths = {QStringLiteral("tools")};
	manifest.compilerToolOverrides = {{QStringLiteral("slopmap2-bsp"), QStringLiteral("tools/qbsp")}};
	manifest.settingsOverrides.selectedInstallationId = QStringLiteral("quake-test");
	manifest.settingsOverrides.editorProfileId = QStringLiteral("trenchbroom");
	manifest.settingsOverrides.paletteId = QStringLiteral("quake");
	manifest.settingsOverrides.compilerProfileId = QStringLiteral("slopmap2-bsp");
	manifest.settingsOverrides.aiFreeModeSet = true;
	manifest.settingsOverrides.aiFreeMode = true;

	QString error;
	if (!slopstudio::saveProjectManifest(manifest, &error)) {
		std::cerr << qUtf8Printable(error) << "\n";
		return fail("Expected project manifest save.");
	}
	if (!QFileInfo::exists(slopstudio::projectManifestPath(tempDir.path()))) {
		return fail("Expected project manifest file.");
	}

	slopstudio::ProjectManifest loaded;
	if (!slopstudio::loadProjectManifest(tempDir.path(), &loaded, &error)) {
		std::cerr << qUtf8Printable(error) << "\n";
		return fail("Expected project manifest load.");
	}
	if (loaded.displayName != QStringLiteral("Manifest Test") || loaded.projectId.isEmpty()) {
		return fail("Expected loaded manifest metadata.");
	}
	if (loaded.sourceFolders != QStringList {QStringLiteral("src")} || loaded.packageFolders != QStringList {QStringLiteral("packages")}) {
		return fail("Expected loaded manifest folders.");
	}
	if (loaded.settingsOverrides.editorProfileId != QStringLiteral("trenchbroom") || !loaded.settingsOverrides.aiFreeModeSet || !loaded.settingsOverrides.aiFreeMode) {
		return fail("Expected loaded project-local settings overrides.");
	}
	if (loaded.compilerSearchPaths != QStringList {QStringLiteral("tools")} || loaded.compilerToolOverrides.size() != 1) {
		return fail("Expected loaded project compiler overrides.");
	}
	if (slopstudio::effectiveProjectInstallationId(loaded, QStringLiteral("global")) != QStringLiteral("quake-test")) {
		return fail("Expected project installation override to win over global fallback.");
	}
	if (slopstudio::effectiveProjectEditorProfileId(loaded, QStringLiteral("radiant")) != QStringLiteral("trenchbroom")) {
		return fail("Expected project editor profile override.");
	}
	if (slopstudio::effectiveProjectPaletteId(loaded, QStringLiteral("doom")) != QStringLiteral("quake")) {
		return fail("Expected project palette override.");
	}
	if (slopstudio::effectiveProjectCompilerProfileId(loaded, QStringLiteral("zdbsp")) != QStringLiteral("slopmap2-bsp")) {
		return fail("Expected project compiler profile override.");
	}
	if (!slopstudio::effectiveProjectAiFreeMode(loaded, false)) {
		return fail("Expected project AI-free override to win over global fallback.");
	}
	if (slopstudio::effectiveProjectCompilerSearchPaths(loaded).isEmpty()) {
		return fail("Expected effective project compiler search paths.");
	}
	QVector<slopstudio::CompilerToolPathOverride> fallbackOverrides = {{QStringLiteral("slopmap2-vis"), QStringLiteral("global-vis")}};
	if (slopstudio::effectiveProjectCompilerToolOverrides(loaded, fallbackOverrides).size() != 2) {
		return fail("Expected project compiler executable overrides to merge with global overrides.");
	}
	if (slopstudio::registerProjectOutputPath(&loaded, root.filePath(QStringLiteral("build/start.bsp"))) != true || slopstudio::registerProjectOutputPath(&loaded, root.filePath(QStringLiteral("build/start.bsp"))) != false) {
		return fail("Expected project output registration to de-duplicate paths.");
	}

	// Missing output and temporary folders appear when first needed: they are
	// information, not warnings.
	const slopstudio::ProjectHealthSummary health = slopstudio::buildProjectHealthSummary(loaded);
	if (health.overallState() != slopstudio::OperationState::Completed || health.failedCount != 0 || health.warningCount != 0 || health.readyCount == 0) {
		return fail("Expected a healthy project without folder warnings.");
	}
	bool sawTemp = false;
	for (const slopstudio::ProjectHealthCheck& check : health.checks) {
		if (check.id == QStringLiteral("temp-folder")) {
			sawTemp = true;
			if (check.state != slopstudio::OperationState::Idle || !check.detail.contains(QStringLiteral("created when first needed"))) {
				return fail("Expected a missing temp folder to be reported as created when first needed.");
			}
		}
	}
	if (!sawTemp) {
		return fail("Expected a temp folder check.");
	}
	if (!slopstudio::projectManifestToText(loaded).contains(QStringLiteral("Manifest Test"))) {
		return fail("Expected manifest text report.");
	}

	// Schema 2: the game, release settings, and keys this version does not know.
	if (loaded.schemaVersion != slopstudio::ProjectManifest::kSchemaVersion || slopstudio::ProjectManifest::kSchemaVersion != 2) {
		return fail("Expected saved manifests to use schema 2.");
	}
	if (slopstudio::effectiveProjectGameKey(loaded) != QStringLiteral("custom")) {
		return fail("Expected a manifest without a game or installation to be custom.");
	}
	slopstudio::GameInstallationProfile quakeInstall;
	quakeInstall.gameKey = QStringLiteral("quake");
	if (slopstudio::effectiveProjectGameKey(loaded, &quakeInstall) != QStringLiteral("quake")) {
		return fail("Expected the installation's game when the manifest names none.");
	}
	const slopstudio::ProjectReleaseSettings defaults = slopstudio::effectiveProjectReleaseSettings(loaded, QStringLiteral("quake3"));
	if (defaults.title != QStringLiteral("Manifest Test") || defaults.version != QStringLiteral("1.0.0") || defaults.packageName != QStringLiteral("manifest-test")
		|| !defaults.packageFormat.isEmpty() || defaults.gameFolder != QStringLiteral("baseq3") || defaults.outputFolder != QStringLiteral("build/releases")
		|| defaults.changelogFile != QStringLiteral("CHANGELOG.md")) {
		return fail("Expected release defaults from the project and game.");
	}
	if (slopstudio::defaultReleasePackageFormat(QStringLiteral("quake")) != QStringLiteral("pak") || slopstudio::defaultReleasePackageFormat(QStringLiteral("doom")) != QStringLiteral("wad")
		|| slopstudio::defaultReleasePackageFormat(QStringLiteral("custom")) != QStringLiteral("zip")) {
		return fail("Expected per-game default package formats.");
	}
	if (slopstudio::releaseSlug(QStringLiteral("  Ünïcode Map: The Pit!  ")) != QStringLiteral("unicode-map-the-pit")) {
		return fail("Expected a file-name-safe release slug.");
	}
	loaded.gameKey = QStringLiteral("quake3");
	loaded.release.title = QStringLiteral("Arena Pack");
	loaded.release.version = QStringLiteral("1.2.0");
	loaded.release.authors = {QStringLiteral("Ada <ada@example.com>"), QStringLiteral("Lin")};
	loaded.release.packageFormat = QStringLiteral("pk3");
	loaded.release.requirements = {{QStringLiteral("Team Arena"), QStringLiteral("missionpack"), QStringLiteral("https://example.com/ta")}};
	loaded.release.include = {QStringLiteral("docs/*.txt")};
	loaded.release.exclude = {QStringLiteral("**/*.psd")};
	loaded.release.includeSources = true;
	if (!slopstudio::saveProjectManifest(loaded, &error)) {
		return fail("Expected the schema 2 manifest to save.");
	}
	// Keys from a newer studio survive load and save, top level and in release.
	{
		QFile file(slopstudio::projectManifestPath(tempDir.path()));
		if (!file.open(QIODevice::ReadOnly)) {
			return fail("Expected to read the saved manifest.");
		}
		QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
		file.close();
		object.insert(QStringLiteral("futureSetting"), QJsonObject {{QStringLiteral("kept"), true}});
		QJsonObject release = object.value(QStringLiteral("release")).toObject();
		release.insert(QStringLiteral("futureRelease"), 7);
		object.insert(QStringLiteral("release"), release);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			return fail("Expected to rewrite the manifest.");
		}
		file.write(QJsonDocument(object).toJson());
	}
	slopstudio::ProjectManifest upgraded;
	if (!slopstudio::loadProjectManifest(tempDir.path(), &upgraded, &error) || !slopstudio::saveProjectManifest(upgraded, &error)
		|| !slopstudio::loadProjectManifest(tempDir.path(), &upgraded, &error)) {
		return fail("Expected the manifest with unknown keys to round trip.");
	}
	if (upgraded.gameKey != QStringLiteral("quake3") || upgraded.release.title != QStringLiteral("Arena Pack") || upgraded.release.version != QStringLiteral("1.2.0")
		|| upgraded.release.authors.size() != 2 || upgraded.release.requirements.size() != 1 || upgraded.release.requirements.first().path != QStringLiteral("missionpack")
		|| upgraded.release.include != QStringList {QStringLiteral("docs/*.txt")} || upgraded.release.exclude != QStringList {QStringLiteral("**/*.psd")}
		|| !upgraded.release.includeSources) {
		return fail("Expected the release settings to round trip.");
	}
	if (!upgraded.extraFields.contains(QStringLiteral("futureSetting")) || upgraded.release.extraFields.value(QStringLiteral("futureRelease")).toInt() != 7) {
		return fail("Expected unknown manifest keys to be kept.");
	}
	const slopstudio::ProjectHealthSummary releaseHealth = slopstudio::buildProjectHealthSummary(upgraded);
	bool sawGame = false;
	bool sawRelease = false;
	for (const slopstudio::ProjectHealthCheck& check : releaseHealth.checks) {
		sawGame = sawGame || (check.id == QStringLiteral("game") && check.state == slopstudio::OperationState::Completed);
		sawRelease = sawRelease || (check.id == QStringLiteral("release") && check.detail.contains(QStringLiteral("1.2.0")));
	}
	if (!sawGame || !sawRelease) {
		return fail("Expected game and release health checks.");
	}

	slopstudio::ProjectManifest missing = slopstudio::defaultProjectManifest(root.filePath(QStringLiteral("missing")));
	if (slopstudio::saveProjectManifest(missing, &error)) {
		return fail("Expected missing project root save to fail.");
	}

	return EXIT_SUCCESS;
}
