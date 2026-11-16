// SlopStudio reads what VibeStudio, its former name, wrote: ids, extensions,
// the project folder, the settings store and environment variables.

#include "core/compiler_registry.h"
#include "core/editor_profiles.h"
#include "core/legacy_names.h"
#include "core/project_manifest.h"
#include "core/workspace_document.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << "\n";
		++failures;
	}
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	app.setOrganizationName(QStringLiteral("DarkMatterProductions"));
	app.setApplicationName(QStringLiteral("SlopStudio"));
	using namespace slopstudio;

	// Names.
	expect(legacyStudioName() == QStringLiteral("VibeStudio"), "The former name is VibeStudio.");
	expect(withCurrentStudioName(QStringLiteral("vibestudio-workspace")) == QStringLiteral("slopstudio-workspace"), "Lower-case ids map.");
	expect(withCurrentStudioName(QStringLiteral("VibeStudio VIBESTUDIO_X VibeMap2")) == QStringLiteral("SlopStudio SLOPSTUDIO_X SlopMap2"), "Case is kept letter by letter.");
	expect(withCurrentStudioName(QStringLiteral("slopstudio.mesh")) == QStringLiteral("slopstudio.mesh"), "Current ids are unchanged.");
	expect(withCurrentStudioName(QStringLiteral("v\u0131be")) == QStringLiteral("v\u0131be"), "Only ASCII letters match.");
	expect(legacyStudioId(QStringLiteral("slopstudio.extension.json")) == QStringLiteral("vibestudio.extension.json"), "legacyStudioId inverts the mapping.");
	expect(legacyStudioId(QStringLiteral("vmt-bsp")) == QStringLiteral("vmt-bsp"), "Names without the syllable are unchanged.");
	expect(isCurrentOrLegacyStudioName(QStringLiteral("vibestudio.mesh"), QStringLiteral("slopstudio.mesh")), "A legacy id matches.");
	expect(isCurrentOrLegacyStudioName(QStringLiteral("slopstudio.mesh"), QStringLiteral("slopstudio.mesh")), "The current id matches.");
	expect(!isCurrentOrLegacyStudioName(QStringLiteral("other.mesh"), QStringLiteral("slopstudio.mesh")), "Other ids do not match.");

	// Extensions.
	expect(isPackageDraftPath(QStringLiteral("C:/work/a.sloppackage")) && isPackageDraftPath(QStringLiteral("/work/a.VIBEPACKAGE")), "Both draft suffixes are drafts.");
	expect(!isPackageDraftPath(QStringLiteral("/work/a.pk3")), "An archive is not a draft.");
	expect(isWorkspaceDocumentPath(QStringLiteral("a.slopworkspace")) && isWorkspaceDocumentPath(QStringLiteral("a.vibeworkspace")), "Both workspace suffixes are workspaces.");

	// Environment variables.
	qunsetenv("SLOPSTUDIO_LEGACY_TEST");
	qputenv("VIBESTUDIO_LEGACY_TEST", "7");
	expect(studioEnvironmentVariableIsSet("LEGACY_TEST"), "A VIBESTUDIO_ variable counts as set.");
	expect(studioEnvironmentVariable("LEGACY_TEST") == QStringLiteral("7") && studioEnvironmentVariableIntValue("LEGACY_TEST") == 7, "The VIBESTUDIO_ value is read.");
	qputenv("SLOPSTUDIO_LEGACY_TEST", "9");
	expect(studioEnvironmentVariable("LEGACY_TEST") == QStringLiteral("9") && studioEnvironmentVariableIntValue("LEGACY_TEST") == 9, "The SLOPSTUDIO_ value wins.");
	qunsetenv("SLOPSTUDIO_LEGACY_TEST");
	qunsetenv("VIBESTUDIO_LEGACY_TEST");
	expect(studioEnvironmentVariable("LEGACY_TEST", QStringLiteral("fallback")) == QStringLiteral("fallback"), "The default applies when neither is set.");

	QTemporaryDir temp;
	expect(temp.isValid(), "A temporary folder is available.");

	// Project folder: renamed once, and the manifest inside is found.
	const QString project = QDir(temp.path()).filePath(QStringLiteral("project"));
	const QByteArray manifestJson = R"({"schemaVersion": 1, "projectId": "legacy", "displayName": "Legacy", "sourceFolders": ["."], "outputFolder": "build", "tempFolder": ".vibestudio/tmp"})";
	expect(writeFile(QDir(project).filePath(QStringLiteral(".vibestudio/project.json")), manifestJson), "The legacy manifest is written.");
	ProjectManifest manifest;
	QString error;
	expect(loadProjectManifest(project, &manifest, &error), "A VibeStudio project loads.");
	expect(QFileInfo(QDir(project).filePath(QStringLiteral(".slopstudio/project.json"))).isFile(), "The project folder is renamed to .slopstudio.");
	expect(!QFileInfo::exists(QDir(project).filePath(QStringLiteral(".vibestudio"))), "The .vibestudio folder is gone after the move.");
	expect(manifest.tempFolder == QStringLiteral(".slopstudio/tmp"), "A temp folder under .vibestudio follows the move.");
	// With both folders present, neither is touched.
	expect(QDir(project).mkpath(QStringLiteral(".vibestudio")), "A second legacy folder is made.");
	expect(migrateLegacyProjectDirectory(project), "Nothing to move is not a failure.");
	expect(QFileInfo(QDir(project).filePath(QStringLiteral(".vibestudio"))).isDir(), "An old folder beside a new one is left alone.");

	// Settings store: copied only into an empty store.
	{
		QSettings legacy(QDir(temp.path()).filePath(QStringLiteral("legacy.ini")), QSettings::IniFormat);
		legacy.setValue(QStringLiteral("preferences/theme"), QStringLiteral("light"));
		legacy.setValue(QStringLiteral("editor/profile"), QStringLiteral("vibestudio-default"));
		legacy.sync();
		QSettings empty(QDir(temp.path()).filePath(QStringLiteral("current.ini")), QSettings::IniFormat);
		expect(copyLegacySettings(legacy, empty) == 2, "Every key is copied into an empty store.");
		expect(empty.value(QStringLiteral("preferences/theme")).toString() == QStringLiteral("light"), "A copied value is readable.");
		QSettings used(QDir(temp.path()).filePath(QStringLiteral("used.ini")), QSettings::IniFormat);
		used.setValue(QStringLiteral("preferences/theme"), QStringLiteral("dark"));
		expect(copyLegacySettings(legacy, used) == 0 && used.value(QStringLiteral("preferences/theme")).toString() == QStringLiteral("dark"), "A store in use is never overwritten.");
	}

	// Data folders.
	expect(legacyDataDirectory(QStringLiteral("C:/Users/u/AppData/Local/DarkMatterProductions/SlopStudio")) == QStringLiteral("C:/Users/u/AppData/Local/DarkMatterProductions/VibeStudio"), "The data folder maps to VibeStudio's.");
	expect(legacyDataDirectory(QStringLiteral("/home/u/.cache/DarkMatterProductions/SlopStudio/cache")) == QStringLiteral("/home/u/.cache/DarkMatterProductions/VibeStudio/cache"), "A nested cache folder maps too.");
	expect(legacyDataDirectory(QStringLiteral("/home/u/.cache/SlopStudioOther")).isEmpty(), "A longer folder name is not the application folder.");

	// Documents and ids.
	WorkspaceDocument workspaceDocument;
	const QString workspacePath = QDir(temp.path()).filePath(QStringLiteral("old.vibeworkspace"));
	QJsonObject json = workspaceDocumentJson(workspaceDocument, workspacePath);
	json.insert(QStringLiteral("format"), QStringLiteral("vibestudio-workspace"));
	WorkspaceDocument parsed;
	expect(parseWorkspaceDocument(QJsonDocument(json).toJson(), workspacePath, &parsed, &error), "A VibeStudio workspace parses.");
	expect(writeFile(workspacePath, QJsonDocument(json).toJson()) && readWorkspaceDocument(workspacePath, &parsed, nullptr, &error), "A .vibeworkspace file reads.");
	CompilerToolDescriptor tool;
	expect(compilerToolDescriptorForId(QStringLiteral("vibemap2-bsp"), &tool) && tool.id == QStringLiteral("slopmap2-bsp"), "A VibeMap2 tool id resolves to SlopMap2.");
	expect(tool.executableNames.join(QLatin1Char(' ')).contains(QStringLiteral("vibemap2-bsp")), "VibeMap2's executable name is still searched.");
	expect(compilerToolDescriptorForId(QStringLiteral("vibemap3")), "A VibeMap3 tool id resolves.");
	EditorProfileDescriptor profile;
	expect(editorProfileForId(QStringLiteral("vibestudio-default"), &profile) && profile.id == QStringLiteral("slopstudio-default"), "The VibeStudio default profile id resolves.");

	if (failures > 0) {
		std::cerr << failures << " legacy name check(s) failed.\n";
		return EXIT_FAILURE;
	}
	std::cout << "Legacy name checks passed.\n";
	return EXIT_SUCCESS;
}
