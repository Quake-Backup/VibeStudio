#include "core/game_installation.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

bool writeFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}
	return file.write("fixture") > 0;
}

// The games a scan offered for one folder, in the order it reported them.
QStringList gameKeysAt(const QVector<slopstudio::GameInstallationDetectionCandidate>& candidates, const QString& rootPath)
{
	QStringList keys;
	for (const slopstudio::GameInstallationDetectionCandidate& candidate : candidates) {
		if (QFileInfo(candidate.profile.rootPath) == QFileInfo(rootPath)) {
			keys << candidate.profile.gameKey;
		}
	}
	return keys;
}

const slopstudio::GameInstallationDetectionCandidate* candidateAt(const QVector<slopstudio::GameInstallationDetectionCandidate>& candidates, const QString& rootPath)
{
	for (const slopstudio::GameInstallationDetectionCandidate& candidate : candidates) {
		if (QFileInfo(candidate.profile.rootPath) == QFileInfo(rootPath)) {
			return &candidate;
		}
	}
	return nullptr;
}

} // namespace

int main()
{
	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected temporary directory.");
	}

	QDir root(tempDir.path());
	if (!root.mkpath(QStringLiteral("id1")) || !writeFile(root.filePath(QStringLiteral("id1/pak0.pak")))) {
		return fail("Expected Quake package fixture.");
	}

	slopstudio::GameInstallationProfile profile;
	profile.rootPath = tempDir.path();
	profile.gameKey = QStringLiteral("quake");
	profile.displayName = QStringLiteral("Manual Quake");
	profile.basePackagePaths = {QStringLiteral("id1/pak0.pak")};
	profile = slopstudio::normalizedGameInstallationProfile(profile);

	if (profile.id.isEmpty() || profile.gameKey != QStringLiteral("quake")) {
		return fail("Expected stable Quake installation id and key.");
	}
	if (profile.engineFamily != slopstudio::GameEngineFamily::IdTech2) {
		return fail("Expected Quake to normalize to idTech2.");
	}
	if (profile.basePackagePaths.size() != 1 || !QFile::exists(profile.basePackagePaths.front())) {
		return fail("Expected relative base package path to normalize under root.");
	}
	if (profile.paletteId != QStringLiteral("quake") || profile.compilerProfileId != QStringLiteral("slopmap2")) {
		return fail("Expected Quake defaults.");
	}

	const slopstudio::GameInstallationValidation validation = slopstudio::validateGameInstallationProfile(profile);
	if (!validation.isUsable() || !validation.errors.isEmpty()) {
		return fail("Expected valid manual profile.");
	}

	slopstudio::GameInstallationProfile missingRoot;
	missingRoot.rootPath = root.filePath(QStringLiteral("missing"));
	const slopstudio::GameInstallationValidation missingValidation = slopstudio::validateGameInstallationProfile(missingRoot);
	if (missingValidation.isUsable() || missingValidation.errors.isEmpty()) {
		return fail("Expected missing root to block validation.");
	}

	if (slopstudio::gameEngineFamilyFromId(QStringLiteral("QUAKE3")) != slopstudio::GameEngineFamily::IdTech3) {
		return fail("Expected engine family aliases to normalize.");
	}
	if (!slopstudio::knownGameKeys().contains(QStringLiteral("doom")) || !slopstudio::knownGameKeys().contains(QStringLiteral("quake3"))) {
		return fail("Expected known idTech game keys.");
	}

	QTemporaryDir steamDir;
	if (!steamDir.isValid()) {
		return fail("Expected temporary Steam directory.");
	}
	QDir steamRoot(steamDir.path());
	if (!steamRoot.mkpath(QStringLiteral("steamapps/common/Quake/id1")) || !writeFile(steamRoot.filePath(QStringLiteral("steamapps/common/Quake/id1/pak0.pak")))) {
		return fail("Expected Steam Quake fixture.");
	}
	// Executables a folder or two below the root (bin/x64) are still found.
	const QString steamQuakeRoot = steamRoot.filePath(QStringLiteral("steamapps/common/Quake"));
	const QString steamQuakeExecutable = steamRoot.filePath(QStringLiteral("steamapps/common/Quake/bin/x64/quake.exe"));
	if (!steamRoot.mkpath(QStringLiteral("steamapps/common/Quake/bin/x64")) || !writeFile(steamQuakeExecutable)) {
		return fail("Expected nested Steam Quake executable fixture.");
	}
	const QVector<slopstudio::GameInstallationDetectionCandidate> steamCandidates = slopstudio::detectSteamGameInstallations({steamDir.path()});
	if (steamCandidates.isEmpty()) {
		return fail("Expected Steam detection candidate.");
	}
	bool foundSteamQuake = false;
	for (const slopstudio::GameInstallationDetectionCandidate& candidate : steamCandidates) {
		if (candidate.sourceId == QStringLiteral("steam") && candidate.profile.gameKey == QStringLiteral("quake") && QFileInfo(candidate.profile.rootPath) == QFileInfo(steamQuakeRoot)) {
			foundSteamQuake = true;
			if (candidate.profile.manual || candidate.confidencePercent < 80) {
				return fail("Expected detected Steam profile metadata.");
			}
			if (QFileInfo(candidate.profile.executablePath) != QFileInfo(steamQuakeExecutable)) {
				return fail("Expected the executable two folders below the Steam root.");
			}
			break;
		}
	}
	if (!foundSteamQuake) {
		return fail("Expected Steam Quake detection.");
	}
	// "Quake" is part of the names "Quake 2" and "Quake III", but a folder called
	// "Quake" is not those games: one folder, one candidate.
	if (gameKeysAt(steamCandidates, steamQuakeRoot) != QStringList {QStringLiteral("quake")}) {
		return fail("Expected a folder named Quake to be offered as Quake only, not as Quake II or Quake III too.");
	}

	QTemporaryDir gogDir;
	if (!gogDir.isValid()) {
		return fail("Expected temporary GOG directory.");
	}
	QDir gogRoot(gogDir.path());
	if (!gogRoot.mkpath(QStringLiteral("Quake III Arena/baseq3")) || !writeFile(gogRoot.filePath(QStringLiteral("Quake III Arena/baseq3/pak0.pk3")))) {
		return fail("Expected GOG Quake III fixture.");
	}
	// The executable search stops two folders below the root, so detection
	// never walks a whole install that only shares a name hint.
	const QString gogQuake3Root = gogRoot.filePath(QStringLiteral("Quake III Arena"));
	if (!gogRoot.mkpath(QStringLiteral("Quake III Arena/tools/build/x64")) || !writeFile(gogRoot.filePath(QStringLiteral("Quake III Arena/tools/build/x64/quake3.exe")))) {
		return fail("Expected deeply nested GOG Quake III executable fixture.");
	}
	const QVector<slopstudio::GameInstallationDetectionCandidate> gogCandidates = slopstudio::detectGogGameInstallations({gogDir.path()});
	if (gogCandidates.isEmpty()) {
		return fail("Expected GOG detection candidate.");
	}
	bool foundGogQuake3 = false;
	for (const slopstudio::GameInstallationDetectionCandidate& candidate : gogCandidates) {
		if (candidate.sourceId == QStringLiteral("gog") && candidate.profile.gameKey == QStringLiteral("quake3") && QFileInfo(candidate.profile.rootPath) == QFileInfo(gogQuake3Root)) {
			foundGogQuake3 = true;
			if (!candidate.profile.executablePath.isEmpty()) {
				return fail("Expected no executable search three folders below the GOG root.");
			}
			break;
		}
	}
	if (!foundGogQuake3) {
		return fail("Expected GOG Quake III detection.");
	}
	// "Quake III Arena" contains "Quake" and "Quake II" as well. The game whose
	// package is in the folder wins and the name-only guesses are dropped.
	if (gameKeysAt(gogCandidates, gogQuake3Root) != QStringList {QStringLiteral("quake3")}) {
		return fail("Expected a Quake III Arena folder with its package to be offered as Quake III only.");
	}

	// Folder names decide only when no package is found, and then the closest
	// name alone; packages found under another case still count.
	QTemporaryDir namesDir;
	if (!namesDir.isValid()) {
		return fail("Expected temporary folder-name directory.");
	}
	QDir namesRoot(namesDir.path());
	const QString emptyQuake2Root = namesRoot.filePath(QStringLiteral("Quake II"));
	const QString shortQuake3Root = namesRoot.filePath(QStringLiteral("Quake 3"));
	const QString mixedCaseRoot = namesRoot.filePath(QStringLiteral("retro"));
	if (!namesRoot.mkpath(QStringLiteral("Quake II"))
		|| !namesRoot.mkpath(QStringLiteral("Quake 3/baseq3")) || !writeFile(namesRoot.filePath(QStringLiteral("Quake 3/baseq3/pak0.pk3")))
		|| !namesRoot.mkpath(QStringLiteral("retro/Id1")) || !writeFile(namesRoot.filePath(QStringLiteral("retro/Id1/PAK0.PAK")))) {
		return fail("Expected folder-name fixtures.");
	}
	const QVector<slopstudio::GameInstallationDetectionCandidate> nameCandidates = slopstudio::detectGogGameInstallations({namesDir.path()});
	if (gameKeysAt(nameCandidates, emptyQuake2Root) != QStringList {QStringLiteral("quake2")}) {
		return fail("Expected an empty Quake II folder to be offered as Quake II only, not as Quake as well.");
	}
	const slopstudio::GameInstallationDetectionCandidate* emptyQuake2 = candidateAt(nameCandidates, emptyQuake2Root);
	if (!emptyQuake2 || emptyQuake2->confidencePercent >= 80 || emptyQuake2->warnings.isEmpty() || !emptyQuake2->matchedPaths.isEmpty()) {
		return fail("Expected a name-only candidate to stay a low-confidence guess with a warning.");
	}
	if (gameKeysAt(nameCandidates, shortQuake3Root) != QStringList {QStringLiteral("quake3")}) {
		return fail("Expected a folder named Quake 3 with its package to be offered as Quake III only.");
	}
	const slopstudio::GameInstallationDetectionCandidate* shortQuake3 = candidateAt(nameCandidates, shortQuake3Root);
	if (!shortQuake3 || shortQuake3->confidencePercent < 90) {
		return fail("Expected a folder named Quake 3 to match by name and by package.");
	}
	// On Windows and macOS the filesystem already ignores case, so this only
	// proves the lookup on a case-sensitive one.
	if (gameKeysAt(nameCandidates, mixedCaseRoot) != QStringList {QStringLiteral("quake")}) {
		return fail("Expected Id1/PAK0.PAK to be found as Quake's id1/pak0.pak.");
	}
	const slopstudio::GameInstallationDetectionCandidate* mixedCase = candidateAt(nameCandidates, mixedCaseRoot);
	if (!mixedCase || mixedCase->matchedPaths.size() != 1 || !QFileInfo(mixedCase->matchedPaths.front()).isFile()
		|| mixedCase->profile.basePackagePaths.size() != 1) {
		return fail("Expected the mixed-case package to be recorded as an existing file.");
	}
	const QString foundMixedCase = slopstudio::findInstallationFile(mixedCaseRoot, QStringLiteral("id1/pak0.pak"));
	if (foundMixedCase.isEmpty() || !QFileInfo(foundMixedCase).isFile()
		|| QFileInfo(foundMixedCase) != QFileInfo(namesRoot.filePath(QStringLiteral("retro/Id1/PAK0.PAK")))) {
		return fail("Expected the installation file lookup to ignore case.");
	}
	if (!slopstudio::findInstallationFile(mixedCaseRoot, QStringLiteral("id1/pak1.pak")).isEmpty()
		|| !slopstudio::findInstallationFile(mixedCaseRoot, QStringLiteral("id1")).isEmpty()
		|| !slopstudio::findInstallationFile(mixedCaseRoot, QString()).isEmpty()) {
		return fail("Expected the installation file lookup to refuse missing files, folders and empty paths.");
	}
	slopstudio::GameInstallationProfile mixedCaseProfile;
	mixedCaseProfile.rootPath = mixedCaseRoot;
	mixedCaseProfile.gameKey = QStringLiteral("quake");
	const slopstudio::GameInstallationValidation mixedCaseValidation = slopstudio::validateGameInstallationProfile(mixedCaseProfile);
	if (!mixedCaseValidation.isUsable() || !mixedCaseValidation.warnings.isEmpty()) {
		return fail("Expected validation to find the base package whatever its case.");
	}

	return EXIT_SUCCESS;
}
