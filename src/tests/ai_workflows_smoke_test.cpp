#include "core/ai_workflows.h"

#include <QCoreApplication>
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

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);

	const QVector<slopstudio::AiToolDescriptor> tools = slopstudio::aiToolDescriptors();
	if (tools.size() < 6 || !slopstudio::aiToolIds().contains(QStringLiteral("compiler-command-proposal"))) {
		return fail("Expected AI-callable tool descriptors.");
	}
	slopstudio::AiToolDescriptor stagedEdit;
	if (!slopstudio::aiToolForId(QStringLiteral("STAGED_TEXT_EDIT"), &stagedEdit) || !stagedEdit.requiresApproval || !stagedEdit.writesFiles) {
		return fail("Expected staged text edit tool safety metadata.");
	}

	slopstudio::AiAutomationPreferences preferences;
	preferences.aiFreeMode = false;
	preferences.cloudConnectorsEnabled = true;
	preferences.preferredReasoningConnectorId = QStringLiteral("openai");
	preferences.preferredCodingConnectorId = QStringLiteral("openai");
	preferences.preferredTextModelId = QStringLiteral("openai-text-default");

	const slopstudio::AiWorkflowResult explanation = slopstudio::explainCompilerLogAiExperiment(QStringLiteral("ERROR: maps/start.map:12 leak detected\nwarning: missing texture"), preferences);
	if (explanation.manifest.workflowId != QStringLiteral("explain-compiler-log") || explanation.manifest.toolCalls.isEmpty() || explanation.manifest.stagedOutputs.isEmpty()) {
		return fail("Expected compiler-log explanation manifest.");
	}
	if (!explanation.reviewableText.contains(QStringLiteral("Findings"))) {
		return fail("Expected reviewable compiler-log explanation text.");
	}

	const slopstudio::AiWorkflowResult proposal = slopstudio::proposeCompilerCommandAiExperiment(QStringLiteral("build quake map maps/start.map with qbsp"), QString(), preferences);
	if (!proposal.commandLine.contains(QStringLiteral("compiler plan")) || !proposal.commandLine.contains(QStringLiteral("--dry-run"))) {
		return fail("Expected reviewable compiler command proposal.");
	}

	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected temporary directory.");
	}
	const slopstudio::AiWorkflowResult manifestDraft = slopstudio::draftProjectManifestAiExperiment(tempDir.path(), QStringLiteral("AI Draft"), preferences);
	if (!manifestDraft.reviewableText.contains(QStringLiteral("AI Draft")) || !manifestDraft.manifest.stagedOutputs.front().writesFile) {
		return fail("Expected staged project manifest draft.");
	}

	const QString packageRoot = tempDir.filePath(QStringLiteral("package"));
	if (!QDir().mkpath(QDir(packageRoot).filePath(QStringLiteral("maps")))) {
		return fail("Expected package fixture directory.");
	}
	QFile mapFile(QDir(packageRoot).filePath(QStringLiteral("maps/start.bsp")));
	if (!mapFile.open(QIODevice::WriteOnly)) {
		return fail("Expected package fixture map file.");
	}
	mapFile.write("dummy");
	mapFile.close();
	const slopstudio::AiWorkflowResult deps = slopstudio::suggestPackageDependenciesAiExperiment(packageRoot, preferences);
	if (deps.manifest.workflowId != QStringLiteral("suggest-package-dependencies") || deps.diagnostics.isEmpty()) {
		return fail("Expected package dependency suggestions.");
	}

	const slopstudio::AiWorkflowResult cli = slopstudio::generateCliCommandAiExperiment(QStringLiteral("validate package release.pk3"), preferences);
	if (!cli.commandLine.contains(QStringLiteral("package validate"))) {
		return fail("Expected generated CLI command proposal.");
	}

	const slopstudio::AiWorkflowResult asset = slopstudio::stageAssetGenerationRequestAiExperiment(QStringLiteral("meshy"), QStringLiteral("texture"), QStringLiteral("rust panel"), preferences);
	if (asset.manifest.workflowId != QStringLiteral("stage-asset-generation-request") || asset.manifest.stagedOutputs.isEmpty()) {
		return fail("Expected staged asset generation request.");
	}

	const slopstudio::AiWorkflowManifest retried = slopstudio::retryAiWorkflowManifest(asset.manifest);
	if (retried.retryCount != asset.manifest.retryCount + 1 || !retried.cancellable) {
		return fail("Expected retry state metadata.");
	}
	if (slopstudio::cancelledAiWorkflowManifest(retried).state != slopstudio::OperationState::Cancelled) {
		return fail("Expected cancellation state metadata.");
	}

	// Saving a manifest: the whole file or an error, never a claim of success
	// over a file that was not written.
	const QString savedPath = tempDir.filePath(QStringLiteral("manifests/run/asset.json"));
	const QByteArray expectedBytes = QJsonDocument(slopstudio::aiWorkflowManifestJson(asset.manifest)).toJson(QJsonDocument::Indented);
	const auto contents = [](const QString& path) {
		QFile file(path);
		return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
	};
	QString saveError = QStringLiteral("stale");
	if (!slopstudio::saveAiWorkflowManifest(asset.manifest, savedPath, &saveError) || !saveError.isEmpty()) {
		return fail("Expected the workflow manifest to save into a new folder.");
	}
	if (expectedBytes.isEmpty() || contents(savedPath) != expectedBytes) {
		return fail("Expected the saved manifest to hold the manifest's JSON, whole.");
	}
	if (!slopstudio::saveAiWorkflowManifest(retried, savedPath, &saveError)
		|| QJsonDocument::fromJson(contents(savedPath)).object().value(QStringLiteral("retryCount")).toInt() != retried.retryCount) {
		return fail("Expected a second save to replace the manifest.");
	}
	if (QDir(QFileInfo(savedPath).absolutePath()).entryList(QDir::Files | QDir::Hidden).size() != 1) {
		return fail("Expected saving to leave only the manifest in its folder.");
	}
	if (slopstudio::saveAiWorkflowManifest(asset.manifest, QString(), &saveError) || saveError.isEmpty()) {
		return fail("Expected an empty manifest path to be refused with a reason.");
	}
	saveError.clear();
	if (slopstudio::saveAiWorkflowManifest(asset.manifest, packageRoot, &saveError) || saveError.isEmpty() || !QFileInfo(packageRoot).isDir()) {
		return fail("Expected a manifest path that is a folder to be refused with a reason.");
	}
#ifdef Q_OS_WIN
	{
		// Windows will not replace a file another handle holds open: the save
		// has to say so, and the manifest already there has to stay whole.
		const QByteArray before = contents(savedPath);
		QFile held(savedPath);
		if (!held.open(QIODevice::ReadOnly)) {
			return fail("Expected to hold the saved manifest open.");
		}
		saveError.clear();
		const bool replaced = slopstudio::saveAiWorkflowManifest(asset.manifest, savedPath, &saveError);
		held.close();
		if (replaced || saveError.isEmpty()) {
			return fail("Expected a manifest that could not be replaced to be reported, not called saved.");
		}
		if (contents(savedPath) != before) {
			return fail("Expected a failed save to leave the earlier manifest whole.");
		}
	}
#endif

	return EXIT_SUCCESS;
}
