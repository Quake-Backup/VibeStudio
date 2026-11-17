#include "core/ai_workflows.h"

#include "core/compiler_profiles.h"
#include "core/package_archive.h"
#include "core/project_manifest.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>

namespace slopstudio {

namespace {

QString workflowId()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QJsonArray stringArrayJson(const QStringList& values)
{
	QJsonArray array;
	for (const QString& value : values) {
		array.append(value);
	}
	return array;
}

QStringList firstNonEmptyLines(const QString& text, int limit)
{
	QStringList lines;
	for (const QString& line : text.split('\n')) {
		const QString trimmed = line.trimmed();
		if (!trimmed.isEmpty()) {
			lines.push_back(trimmed);
		}
		if (lines.size() >= limit) {
			break;
		}
	}
	return lines;
}

bool containsAny(const QString& text, const QStringList& needles)
{
	for (const QString& needle : needles) {
		if (text.contains(needle, Qt::CaseInsensitive)) {
			return true;
		}
	}
	return false;
}

QString selectedProviderId(const AiAutomationPreferences& preferences, const QString& requestedProviderId, const QString& capabilityId)
{
	AiConnectorDescriptor connector;
	if (!requestedProviderId.trimmed().isEmpty() && aiConnectorForId(requestedProviderId, &connector) && connector.capabilities.contains(normalizedAiId(capabilityId))) {
		return connector.id;
	}

	const AiAutomationPreferences normalized = normalizedAiAutomationPreferences(preferences);
	QString preferred;
	const QString capability = normalizedAiId(capabilityId);
	if (capability == QStringLiteral("coding")) {
		preferred = normalized.preferredCodingConnectorId;
	} else if (capability == QStringLiteral("vision")) {
		preferred = normalized.preferredVisionConnectorId;
	} else if (capability == QStringLiteral("image")) {
		preferred = normalized.preferredImageConnectorId;
	} else if (capability == QStringLiteral("audio")) {
		preferred = normalized.preferredAudioConnectorId;
	} else if (capability == QStringLiteral("voice")) {
		preferred = normalized.preferredVoiceConnectorId;
	} else if (capability == QStringLiteral("three-d")) {
		preferred = normalized.preferredThreeDConnectorId;
	} else if (capability == QStringLiteral("embeddings")) {
		preferred = normalized.preferredEmbeddingsConnectorId;
	} else {
		preferred = normalized.preferredReasoningConnectorId;
	}
	if (!preferred.isEmpty() && aiConnectorSupportsCapability(preferred, capability)) {
		return preferred;
	}
	if ((normalized.aiFreeMode || normalized.projectAiFree) && aiConnectorSupportsCapability(QStringLiteral("local-offline"), capability)) {
		return QStringLiteral("local-offline");
	}
	return aiConnectorSupportsCapability(defaultAiReasoningConnectorId(), capability) ? defaultAiReasoningConnectorId() : QStringLiteral("local-offline");
}

QString selectedModelId(const AiAutomationPreferences& preferences, const QString& providerId, const QString& requestedModelId, const QString& capabilityId)
{
	const QString capability = normalizedAiId(capabilityId);
	if (!requestedModelId.trimmed().isEmpty() && aiModelSupportsCapability(requestedModelId, capability)) {
		return normalizedAiModelId(requestedModelId, capability);
	}

	const AiAutomationPreferences normalized = normalizedAiAutomationPreferences(preferences);
	QString preferred;
	if (capability == QStringLiteral("coding")) {
		preferred = normalized.preferredCodingModelId;
	} else if (capability == QStringLiteral("vision")) {
		preferred = normalized.preferredVisionModelId;
	} else if (capability == QStringLiteral("image")) {
		preferred = normalized.preferredImageModelId;
	} else if (capability == QStringLiteral("audio")) {
		preferred = normalized.preferredAudioModelId;
	} else if (capability == QStringLiteral("voice")) {
		preferred = normalized.preferredVoiceModelId;
	} else if (capability == QStringLiteral("three-d")) {
		preferred = normalized.preferredThreeDModelId;
	} else if (capability == QStringLiteral("embeddings")) {
		preferred = normalized.preferredEmbeddingsModelId;
	} else {
		preferred = normalized.preferredTextModelId;
	}
	if (!preferred.isEmpty() && aiModelSupportsCapability(preferred, capability)) {
		return normalizedAiModelId(preferred, capability);
	}
	return defaultAiModelId(providerId, capability);
}

AiWorkflowToolCall toolCall(const QString& toolId, const QString& summary, const QStringList& inputs = {}, const QStringList& outputs = {}, const QStringList& warnings = {})
{
	AiWorkflowToolCall call;
	call.toolId = normalizedAiId(toolId);
	call.summary = summary;
	call.inputs = inputs;
	call.outputs = outputs;
	call.warnings = warnings;
	AiToolDescriptor descriptor;
	if (aiToolForId(call.toolId, &descriptor)) {
		call.requiresApproval = descriptor.requiresApproval;
		call.writesFiles = descriptor.writesFiles;
	}
	call.state = warnings.isEmpty() ? OperationState::Completed : OperationState::Warning;
	return call;
}

void applyCredentialWarning(AiWorkflowManifest* manifest, const AiAutomationPreferences& preferences)
{
	if (!manifest) {
		return;
	}
	AiConnectorDescriptor connector;
	if (!aiConnectorForId(manifest->providerId, &connector) || !connector.cloudBased) {
		return;
	}
	const AiCredentialStatus status = aiCredentialStatusForConnector(connector.id, preferences);
	if (!status.configured) {
		manifest->warnings.push_back(QCoreApplication::translate("SlopStudioAiWorkflows", "Provider credential is not configured; the workflow remains a local, reviewable proposal."));
		manifest->state = OperationState::Warning;
	}
}

QString inferredInputPath(const QString& prompt)
{
	static const QRegularExpression pathPattern(QStringLiteral(R"((\"[^\"]+\.(?:map|bsp|wad|pak|pk3|zip)\"|'[^']+\.(?:map|bsp|wad|pak|pk3|zip)'|\S+\.(?:map|bsp|wad|pak|pk3|zip)))"), QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch match = pathPattern.match(prompt);
	if (!match.hasMatch()) {
		return {};
	}
	return match.captured(1).trimmed().remove('"').remove('\'');
}

QString chooseCompilerProfileId(const QString& prompt)
{
	const QString lower = prompt.toLower();
	auto firstProfile = [](const QStringList& ids) {
		for (const QString& id : ids) {
			CompilerProfileDescriptor descriptor;
			if (compilerProfileForId(id, &descriptor)) {
				return descriptor.id;
			}
		}
		return QString();
	};

	if (containsAny(lower, {QStringLiteral("q3"), QStringLiteral("quake 3"), QStringLiteral("q3map2"), QStringLiteral("slopmap3")})) {
		if (lower.contains(QStringLiteral("light"))) {
			return firstProfile({QStringLiteral("slopmap3-light")});
		}
		if (lower.contains(QStringLiteral("vis"))) {
			return firstProfile({QStringLiteral("slopmap3-vis")});
		}
		return firstProfile({QStringLiteral("slopmap3-bsp")});
	}
	if (containsAny(lower, {QStringLiteral("doom"), QStringLiteral("node"), QStringLiteral("zdbsp")})) {
		return firstProfile({QStringLiteral("zdbsp"), QStringLiteral("zokumbsp")});
	}
	if (lower.contains(QStringLiteral("zokum"))) {
		return firstProfile({QStringLiteral("zokumbsp"), QStringLiteral("zdbsp")});
	}
	if (lower.contains(QStringLiteral("light"))) {
		return firstProfile({QStringLiteral("slopmap2-light")});
	}
	if (lower.contains(QStringLiteral("vis"))) {
		return firstProfile({QStringLiteral("slopmap2-vis")});
	}
	return firstProfile({QStringLiteral("slopmap2-bsp")});
}

QString quotedCliPath(const QString& value)
{
	if (value.trimmed().isEmpty()) {
		return {};
	}
	QString escaped = value;
	escaped.replace('"', QStringLiteral("\\\""));
	return QStringLiteral("\"%1\"").arg(escaped);
}

QString projectManifestPreview(const ProjectManifest& manifest)
{
	QStringList lines;
	lines << QStringLiteral("{");
	lines << QStringLiteral("  \"schemaVersion\": %1,").arg(ProjectManifest::kSchemaVersion);
	lines << QStringLiteral("  \"projectId\": \"%1\",").arg(manifest.projectId);
	lines << QStringLiteral("  \"displayName\": \"%1\",").arg(manifest.displayName);
	lines << QStringLiteral("  \"sourceFolders\": [\"%1\"],").arg(manifest.sourceFolders.value(0, QStringLiteral(".")));
	lines << QStringLiteral("  \"packageFolders\": [],");
	lines << QStringLiteral("  \"outputFolder\": \"%1\",").arg(manifest.outputFolder);
	lines << QStringLiteral("  \"tempFolder\": \"%1\"").arg(manifest.tempFolder);
	lines << QStringLiteral("}");
	return lines.join('\n');
}

} // namespace

QVector<AiToolDescriptor> aiToolDescriptors()
{
	return {
		{QStringLiteral("project-summary"), QCoreApplication::translate("SlopStudioAiWorkflows", "Project Summary"), QCoreApplication::translate("SlopStudioAiWorkflows", "Summarizes project manifests, folders, overrides, and health without mutating project files."), {QStringLiteral("reasoning"), QStringLiteral("tool-calls")}, {QStringLiteral("ProjectManifest")}, {QStringLiteral("summary")}, false, false},
		{QStringLiteral("package-metadata-search"), QCoreApplication::translate("SlopStudioAiWorkflows", "Package Metadata Search"), QCoreApplication::translate("SlopStudioAiWorkflows", "Searches package entries, warnings, types, and likely dependency hints without extracting content."), {QStringLiteral("reasoning"), QStringLiteral("tool-calls")}, {QStringLiteral("PackageArchive")}, {QStringLiteral("dependency-hints")}, false, false},
		{QStringLiteral("compiler-profile-list"), QCoreApplication::translate("SlopStudioAiWorkflows", "Compiler Profile Listing"), QCoreApplication::translate("SlopStudioAiWorkflows", "Lists registered compiler profiles and tool readiness for command proposals."), {QStringLiteral("coding"), QStringLiteral("tool-calls")}, {QStringLiteral("CompilerProfileDescriptor")}, {QStringLiteral("profile-summary")}, false, false},
		{QStringLiteral("compiler-command-proposal"), QCoreApplication::translate("SlopStudioAiWorkflows", "Compiler Command Proposal"), QCoreApplication::translate("SlopStudioAiWorkflows", "Builds reviewable compiler commands and manifests before any process is started."), {QStringLiteral("coding"), QStringLiteral("tool-calls")}, {QStringLiteral("prompt"), QStringLiteral("CompilerCommandRequest")}, {QStringLiteral("command-line"), QStringLiteral("manifest")}, true, false},
		{QStringLiteral("staged-text-edit"), QCoreApplication::translate("SlopStudioAiWorkflows", "Staged Text Edit"), QCoreApplication::translate("SlopStudioAiWorkflows", "Produces text edits as staged previews and diffs before any file write is applied."), {QStringLiteral("coding"), QStringLiteral("tool-calls")}, {QStringLiteral("prompt"), QStringLiteral("file-summary")}, {QStringLiteral("staged-diff")}, true, true},
		{QStringLiteral("staged-asset-generation-request"), QCoreApplication::translate("SlopStudioAiWorkflows", "Staged Asset Generation Request"), QCoreApplication::translate("SlopStudioAiWorkflows", "Creates provider-specific asset generation requests that remain staged until imported by the user."), {QStringLiteral("image"), QStringLiteral("audio"), QStringLiteral("voice"), QStringLiteral("three-d"), QStringLiteral("tool-calls")}, {QStringLiteral("prompt"), QStringLiteral("asset-kind")}, {QStringLiteral("staged-asset-request")}, true, true},
	};
}

QStringList aiToolIds()
{
	QStringList ids;
	for (const AiToolDescriptor& descriptor : aiToolDescriptors()) {
		ids.push_back(descriptor.id);
	}
	return ids;
}

bool aiToolForId(const QString& id, AiToolDescriptor* out)
{
	const QString normalized = normalizedAiId(id);
	for (const AiToolDescriptor& descriptor : aiToolDescriptors()) {
		if (descriptor.id == normalized) {
			if (out) {
				*out = descriptor;
			}
			return true;
		}
	}
	return false;
}

AiWorkflowManifest defaultAiWorkflowManifest(const QString& workflowIdValue, const QString& providerId, const QString& modelId, const QString& prompt)
{
	AiWorkflowManifest manifest;
	manifest.manifestId = workflowId();
	manifest.createdUtc = QDateTime::currentDateTimeUtc();
	manifest.workflowId = normalizedAiId(workflowIdValue);
	manifest.providerId = normalizedAiId(providerId);
	manifest.modelId = normalizedAiModelId(modelId);
	manifest.prompt = prompt.trimmed();
	manifest.redactionSummary = QCoreApplication::translate("SlopStudioAiWorkflows", "Secrets are not stored; credentials are represented only by source and redacted lookup status.");
	manifest.validationSummary = QCoreApplication::translate("SlopStudioAiWorkflows", "Preview generated. No files were written and no compiler/package mutation was started.");
	manifest.costUsageSummary = QCoreApplication::translate("SlopStudioAiWorkflows", "No provider cost is recorded for deterministic local preview output.");
	return manifest;
}

AiWorkflowManifest cancelledAiWorkflowManifest(AiWorkflowManifest manifest)
{
	manifest.state = OperationState::Cancelled;
	manifest.cancellable = false;
	manifest.validationSummary = QCoreApplication::translate("SlopStudioAiWorkflows", "Workflow was cancelled before staged outputs were applied.");
	return manifest;
}

AiWorkflowManifest retryAiWorkflowManifest(AiWorkflowManifest manifest)
{
	manifest.state = OperationState::Queued;
	manifest.cancellable = true;
	++manifest.retryCount;
	manifest.validationSummary = QCoreApplication::translate("SlopStudioAiWorkflows", "Workflow is queued for supervised retry.");
	return manifest;
}

QJsonObject aiToolDescriptorJson(const AiToolDescriptor& tool)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), tool.id);
	object.insert(QStringLiteral("displayName"), tool.displayName);
	object.insert(QStringLiteral("description"), tool.description);
	object.insert(QStringLiteral("capabilities"), stringArrayJson(tool.capabilities));
	object.insert(QStringLiteral("contextInputs"), stringArrayJson(tool.contextInputs));
	object.insert(QStringLiteral("stagedOutputs"), stringArrayJson(tool.stagedOutputs));
	object.insert(QStringLiteral("requiresApproval"), tool.requiresApproval);
	object.insert(QStringLiteral("writesFiles"), tool.writesFiles);
	return object;
}

QJsonObject aiWorkflowManifestJson(const AiWorkflowManifest& manifest)
{
	QJsonObject object;
	object.insert(QStringLiteral("schemaVersion"), manifest.schemaVersion);
	object.insert(QStringLiteral("manifestId"), manifest.manifestId);
	object.insert(QStringLiteral("createdUtc"), manifest.createdUtc.toUTC().toString(Qt::ISODate));
	object.insert(QStringLiteral("workflowId"), manifest.workflowId);
	object.insert(QStringLiteral("providerId"), manifest.providerId);
	object.insert(QStringLiteral("modelId"), manifest.modelId);
	object.insert(QStringLiteral("prompt"), manifest.prompt);
	object.insert(QStringLiteral("contextSummary"), manifest.contextSummary);
	object.insert(QStringLiteral("redactionSummary"), manifest.redactionSummary);
	object.insert(QStringLiteral("approvalState"), manifest.approvalState);
	object.insert(QStringLiteral("validationSummary"), manifest.validationSummary);
	object.insert(QStringLiteral("costUsageSummary"), manifest.costUsageSummary);
	object.insert(QStringLiteral("state"), operationStateId(manifest.state));
	object.insert(QStringLiteral("cancellable"), manifest.cancellable);
	object.insert(QStringLiteral("retryCount"), manifest.retryCount);
	object.insert(QStringLiteral("writesApplied"), manifest.writesApplied);
	object.insert(QStringLiteral("warnings"), stringArrayJson(manifest.warnings));

	QJsonArray calls;
	for (const AiWorkflowToolCall& call : manifest.toolCalls) {
		QJsonObject callObject;
		callObject.insert(QStringLiteral("toolId"), call.toolId);
		callObject.insert(QStringLiteral("summary"), call.summary);
		callObject.insert(QStringLiteral("state"), operationStateId(call.state));
		callObject.insert(QStringLiteral("inputs"), stringArrayJson(call.inputs));
		callObject.insert(QStringLiteral("outputs"), stringArrayJson(call.outputs));
		callObject.insert(QStringLiteral("warnings"), stringArrayJson(call.warnings));
		callObject.insert(QStringLiteral("requiresApproval"), call.requiresApproval);
		callObject.insert(QStringLiteral("writesFiles"), call.writesFiles);
		calls.append(callObject);
	}
	object.insert(QStringLiteral("toolCalls"), calls);

	QJsonArray staged;
	for (const AiStagedOutput& output : manifest.stagedOutputs) {
		QJsonObject outputObject;
		outputObject.insert(QStringLiteral("id"), output.id);
		outputObject.insert(QStringLiteral("kind"), output.kind);
		outputObject.insert(QStringLiteral("label"), output.label);
		outputObject.insert(QStringLiteral("proposedPath"), output.proposedPath);
		outputObject.insert(QStringLiteral("summary"), output.summary);
		outputObject.insert(QStringLiteral("previewLines"), stringArrayJson(output.previewLines));
		outputObject.insert(QStringLiteral("writesFile"), output.writesFile);
		staged.append(outputObject);
	}
	object.insert(QStringLiteral("stagedOutputs"), staged);
	return object;
}

QString aiWorkflowManifestText(const AiWorkflowManifest& manifest)
{
	QStringList lines;
	lines << QCoreApplication::translate("SlopStudioAiWorkflows", "AI workflow manifest: %1").arg(manifest.workflowId);
	lines << QCoreApplication::translate("SlopStudioAiWorkflows", "Provider/model: %1 / %2").arg(manifest.providerId, manifest.modelId.isEmpty() ? QCoreApplication::translate("SlopStudioAiWorkflows", "provider default") : manifest.modelId);
	lines << QCoreApplication::translate("SlopStudioAiWorkflows", "State: %1").arg(operationStateId(manifest.state));
	lines << QCoreApplication::translate("SlopStudioAiWorkflows", "Approval: %1").arg(manifest.approvalState);
	lines << QCoreApplication::translate("SlopStudioAiWorkflows", "Context: %1").arg(manifest.contextSummary);
	lines << QCoreApplication::translate("SlopStudioAiWorkflows", "Redaction: %1").arg(manifest.redactionSummary);
	lines << QCoreApplication::translate("SlopStudioAiWorkflows", "Validation: %1").arg(manifest.validationSummary);
	lines << QCoreApplication::translate("SlopStudioAiWorkflows", "Cost/usage: %1").arg(manifest.costUsageSummary);
	lines << QCoreApplication::translate("SlopStudioAiWorkflows", "Tool calls:");
	for (const AiWorkflowToolCall& call : manifest.toolCalls) {
		lines << QStringLiteral("- %1: %2").arg(call.toolId, call.summary);
	}
	lines << QCoreApplication::translate("SlopStudioAiWorkflows", "Staged outputs:");
	for (const AiStagedOutput& output : manifest.stagedOutputs) {
		lines << QStringLiteral("- %1 [%2]: %3").arg(output.label, output.kind, output.summary);
		if (!output.proposedPath.isEmpty()) {
			lines << QStringLiteral("  %1").arg(QDir::toNativeSeparators(output.proposedPath));
		}
	}
	if (!manifest.warnings.isEmpty()) {
		lines << QCoreApplication::translate("SlopStudioAiWorkflows", "Warnings:");
		for (const QString& warning : manifest.warnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	return lines.join('\n');
}

bool saveAiWorkflowManifest(const AiWorkflowManifest& manifest, const QString& path, QString* error)
{
	if (error) {
		error->clear();
	}
	const QString trimmedPath = path.trimmed();
	if (trimmedPath.isEmpty()) {
		if (error) {
			*error = QCoreApplication::translate("SlopStudioAiWorkflows", "Manifest path is empty.");
		}
		return false;
	}

	QFileInfo info(trimmedPath);
	QDir dir = info.absoluteDir();
	if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
		if (error) {
			*error = QCoreApplication::translate("SlopStudioAiWorkflows", "Unable to create manifest directory.");
		}
		return false;
	}

	// Written beside the target and renamed over it, so a full disk or a failed
	// write is reported and never leaves a cut-off manifest in its place.
	QSaveFile file(info.absoluteFilePath());
	const QByteArray bytes = QJsonDocument(aiWorkflowManifestJson(manifest)).toJson(QJsonDocument::Indented);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
		if (error) {
			*error = QCoreApplication::translate("SlopStudioAiWorkflows", "Unable to write AI workflow manifest.");
		}
		return false;
	}
	return true;
}

AiWorkflowResult explainCompilerLogAiExperiment(const QString& logText, const AiAutomationPreferences& preferences, const QString& providerId, const QString& modelId)
{
	const QString provider = selectedProviderId(preferences, providerId, QStringLiteral("reasoning"));
	const QString model = selectedModelId(preferences, provider, modelId, QStringLiteral("reasoning"));
	AiWorkflowResult result;
	result.title = QCoreApplication::translate("SlopStudioAiWorkflows", "Compiler Log Explanation");
	result.manifest = defaultAiWorkflowManifest(QStringLiteral("explain-compiler-log"), provider, model, QCoreApplication::translate("SlopStudioAiWorkflows", "Explain compiler log"));
	result.manifest.contextSummary = QCoreApplication::translate("SlopStudioAiWorkflows", "Compiler log text only; no files are read or written.");
	result.manifest.toolCalls.push_back(toolCall(QStringLiteral("compiler-profile-list"), QCoreApplication::translate("SlopStudioAiWorkflows", "Referenced compiler profile families to make diagnostics actionable."), {}, {QCoreApplication::translate("SlopStudioAiWorkflows", "Compiler profiles available: %1").arg(compilerProfileDescriptors().size())}));

	const QStringList lines = firstNonEmptyLines(logText, 8);
	const QString lower = logText.toLower();
	const int errorCount = lower.count(QStringLiteral("error")) + lower.count(QStringLiteral("fatal"));
	const int warningCount = lower.count(QStringLiteral("warn"));
	QStringList findings;
	if (errorCount > 0) {
		findings << QCoreApplication::translate("SlopStudioAiWorkflows", "The log contains blocking error or fatal markers.");
	}
	if (warningCount > 0) {
		findings << QCoreApplication::translate("SlopStudioAiWorkflows", "The log contains warning markers that may still produce usable output.");
	}
	if (containsAny(lower, {QStringLiteral("leak"), QStringLiteral("portal") })) {
		findings << QCoreApplication::translate("SlopStudioAiWorkflows", "Leak or portal wording suggests a map sealing/visibility problem.");
	}
	if (containsAny(lower, {QStringLiteral("texture"), QStringLiteral("shader"), QStringLiteral("wad") })) {
		findings << QCoreApplication::translate("SlopStudioAiWorkflows", "Texture, shader, or WAD wording suggests a missing asset or mount/dependency issue.");
	}
	if (containsAny(lower, {QStringLiteral("entity"), QStringLiteral("classname"), QStringLiteral("spawn") })) {
		findings << QCoreApplication::translate("SlopStudioAiWorkflows", "Entity wording suggests checking class names, definitions, or map entity keys.");
	}
	if (findings.isEmpty()) {
		findings << QCoreApplication::translate("SlopStudioAiWorkflows", "No obvious fatal marker was found. Review the first compiler messages and rerun with verbose output if the tool failed.");
	}

	result.summary = QCoreApplication::translate("SlopStudioAiWorkflows", "%1 findings from %2 non-empty log lines.").arg(findings.size()).arg(lines.size());
	result.reviewableText = QStringLiteral("%1\n\n%2\n\n%3")
		.arg(QCoreApplication::translate("SlopStudioAiWorkflows", "Findings:"), QStringLiteral("- %1").arg(findings.join(QStringLiteral("\n- "))), QCoreApplication::translate("SlopStudioAiWorkflows", "First log lines:"));
	result.reviewableText += QStringLiteral("\n- %1").arg(lines.isEmpty() ? QCoreApplication::translate("SlopStudioAiWorkflows", "No log text supplied.") : lines.join(QStringLiteral("\n- ")));
	result.nextActions = {
		QCoreApplication::translate("SlopStudioAiWorkflows", "Open the compiler manifest or captured stdout/stderr for the full context."),
		QCoreApplication::translate("SlopStudioAiWorkflows", "Run ai propose-command with the desired target to get a reviewable next command."),
		QCoreApplication::translate("SlopStudioAiWorkflows", "Keep AI-free mode enabled if you want deterministic local explanations only."),
	};
	result.diagnostics = findings;
	result.manifest.stagedOutputs.push_back({QStringLiteral("compiler-log-explanation"), QStringLiteral("text"), QCoreApplication::translate("SlopStudioAiWorkflows", "Compiler log explanation"), QString(), result.summary, findings, false});
	applyCredentialWarning(&result.manifest, preferences);
	return result;
}

AiWorkflowResult proposeCompilerCommandAiExperiment(const QString& prompt, const QString& workspaceRootPath, const AiAutomationPreferences& preferences, const QString& providerId, const QString& modelId)
{
	const QString provider = selectedProviderId(preferences, providerId, QStringLiteral("coding"));
	const QString model = selectedModelId(preferences, provider, modelId, QStringLiteral("coding"));
	const QString profileId = chooseCompilerProfileId(prompt);
	const QString input = inferredInputPath(prompt).isEmpty() ? QStringLiteral("<map-or-wad-path>") : inferredInputPath(prompt);

	AiWorkflowResult result;
	result.title = QCoreApplication::translate("SlopStudioAiWorkflows", "Compiler Command Proposal");
	result.manifest = defaultAiWorkflowManifest(QStringLiteral("propose-compiler-command"), provider, model, prompt);
	result.manifest.contextSummary = workspaceRootPath.trimmed().isEmpty()
		? QCoreApplication::translate("SlopStudioAiWorkflows", "Natural-language prompt and compiler profile registry only.")
		: QCoreApplication::translate("SlopStudioAiWorkflows", "Natural-language prompt, compiler profile registry, and workspace root path.");
	result.manifest.toolCalls.push_back(toolCall(QStringLiteral("compiler-profile-list"), QCoreApplication::translate("SlopStudioAiWorkflows", "Matched prompt intent to a compiler profile."), {prompt}, {profileId}));
	result.manifest.toolCalls.push_back(toolCall(QStringLiteral("compiler-command-proposal"), QCoreApplication::translate("SlopStudioAiWorkflows", "Produced a reviewable CLI command without starting a compiler process."), {profileId, input}, {}, {}));

	result.commandLine = QStringLiteral("slopstudio --cli compiler plan %1 --input %2 --dry-run").arg(profileId, quotedCliPath(input));
	if (!workspaceRootPath.trimmed().isEmpty()) {
		result.commandLine += QStringLiteral(" --workspace-root %1").arg(quotedCliPath(workspaceRootPath));
	}
	result.summary = QCoreApplication::translate("SlopStudioAiWorkflows", "Reviewable compiler command proposal for profile %1.").arg(profileId);
	result.reviewableText = QStringLiteral("%1\n%2").arg(result.summary, result.commandLine);
	result.nextActions = {
		QCoreApplication::translate("SlopStudioAiWorkflows", "Review the profile and input path."),
		QCoreApplication::translate("SlopStudioAiWorkflows", "Run the plan command first; add --manifest when you want a reproducible command record."),
		QCoreApplication::translate("SlopStudioAiWorkflows", "Switch to compiler run only after the plan is correct."),
	};
	result.manifest.stagedOutputs.push_back({QStringLiteral("compiler-command"), QStringLiteral("command"), QCoreApplication::translate("SlopStudioAiWorkflows", "Compiler command"), QString(), result.commandLine, {result.commandLine}, false});
	applyCredentialWarning(&result.manifest, preferences);
	return result;
}

AiWorkflowResult draftProjectManifestAiExperiment(const QString& projectRootPath, const QString& displayName, const AiAutomationPreferences& preferences, const QString& providerId, const QString& modelId)
{
	const QString provider = selectedProviderId(preferences, providerId, QStringLiteral("reasoning"));
	const QString model = selectedModelId(preferences, provider, modelId, QStringLiteral("reasoning"));
	const QString rootPath = projectRootPath.trimmed().isEmpty() ? QDir::currentPath() : projectRootPath;
	const ProjectManifest draft = defaultProjectManifest(rootPath, displayName);

	AiWorkflowResult result;
	result.title = QCoreApplication::translate("SlopStudioAiWorkflows", "Project Manifest Draft");
	result.summary = QCoreApplication::translate("SlopStudioAiWorkflows", "Draft .slopstudio/project.json preview for %1.").arg(QDir::toNativeSeparators(draft.rootPath));
	result.reviewableText = projectManifestPreview(draft);
	result.manifest = defaultAiWorkflowManifest(QStringLiteral("draft-project-manifest"), provider, model, QCoreApplication::translate("SlopStudioAiWorkflows", "Draft project manifest"));
	result.manifest.contextSummary = QCoreApplication::translate("SlopStudioAiWorkflows", "Project root path and folder naming only; manifest is staged and not written.");
	result.manifest.toolCalls.push_back(toolCall(QStringLiteral("project-summary"), QCoreApplication::translate("SlopStudioAiWorkflows", "Built a default project manifest draft from the requested folder."), {draft.rootPath}, {draft.projectId}));
	result.manifest.stagedOutputs.push_back({QStringLiteral("project-manifest-draft"), QStringLiteral("json"), QCoreApplication::translate("SlopStudioAiWorkflows", "Project manifest draft"), projectManifestPath(draft.rootPath), result.summary, result.reviewableText.split('\n'), true});
	result.nextActions = {
		QCoreApplication::translate("SlopStudioAiWorkflows", "Review source, package, output, temp, and AI-free settings before saving."),
		QCoreApplication::translate("SlopStudioAiWorkflows", "Use project init when you want SlopStudio to write the manifest."),
	};
	applyCredentialWarning(&result.manifest, preferences);
	return result;
}

AiWorkflowResult suggestPackageDependenciesAiExperiment(const QString& packagePath, const AiAutomationPreferences& preferences, const QString& providerId, const QString& modelId)
{
	const QString provider = selectedProviderId(preferences, providerId, QStringLiteral("reasoning"));
	const QString model = selectedModelId(preferences, provider, modelId, QStringLiteral("reasoning"));

	AiWorkflowResult result;
	result.title = QCoreApplication::translate("SlopStudioAiWorkflows", "Package Dependency Suggestions");
	result.manifest = defaultAiWorkflowManifest(QStringLiteral("suggest-package-dependencies"), provider, model, QCoreApplication::translate("SlopStudioAiWorkflows", "Suggest missing package dependencies"));
	result.manifest.contextSummary = QCoreApplication::translate("SlopStudioAiWorkflows", "Package metadata only; entries are listed without extraction.");

	QStringList suggestions;
	PackageArchive archive;
	QString error;
	if (!packagePath.trimmed().isEmpty() && archive.load(packagePath, &error)) {
		bool hasMaps = false;
		bool hasShaders = false;
		bool hasTextures = false;
		bool hasSounds = false;
		for (const PackageEntry& entry : archive.entries()) {
			const QString path = entry.virtualPath.toLower();
			hasMaps = hasMaps || path.endsWith(QStringLiteral(".bsp")) || path.endsWith(QStringLiteral(".map"));
			hasShaders = hasShaders || path.endsWith(QStringLiteral(".shader"));
			hasTextures = hasTextures || path.startsWith(QStringLiteral("textures/")) || path.endsWith(QStringLiteral(".wad"));
			hasSounds = hasSounds || path.startsWith(QStringLiteral("sound/")) || path.startsWith(QStringLiteral("sounds/"));
		}
		if (hasMaps && !hasTextures) {
			suggestions << QCoreApplication::translate("SlopStudioAiWorkflows", "Map entries were found but no obvious texture folder or WAD reference was present; verify base texture packages.");
		}
		if (hasShaders && !hasTextures) {
			suggestions << QCoreApplication::translate("SlopStudioAiWorkflows", "Shader scripts were found without obvious texture assets; verify shader texture dependencies.");
		}
		if (hasMaps && !hasSounds) {
			suggestions << QCoreApplication::translate("SlopStudioAiWorkflows", "Map entries were found without sound assets; verify whether the map expects base-game ambient or trigger sounds.");
		}
		if (!archive.warnings().isEmpty()) {
			suggestions << QCoreApplication::translate("SlopStudioAiWorkflows", "Package loader warnings should be reviewed before release.");
		}
		if (suggestions.isEmpty()) {
			suggestions << QCoreApplication::translate("SlopStudioAiWorkflows", "No obvious missing package dependency pattern was detected from metadata alone.");
		}
		result.manifest.toolCalls.push_back(toolCall(QStringLiteral("package-metadata-search"), QCoreApplication::translate("SlopStudioAiWorkflows", "Scanned package metadata for map, shader, texture, and sound hints."), {packagePath}, suggestions));
	} else {
		suggestions << (packagePath.trimmed().isEmpty() ? QCoreApplication::translate("SlopStudioAiWorkflows", "No package path was supplied.") : QCoreApplication::translate("SlopStudioAiWorkflows", "Package could not be loaded: %1").arg(error));
		result.manifest.toolCalls.push_back(toolCall(QStringLiteral("package-metadata-search"), QCoreApplication::translate("SlopStudioAiWorkflows", "Package metadata scan could not complete."), {packagePath}, {}, suggestions));
	}

	result.summary = QCoreApplication::translate("SlopStudioAiWorkflows", "Package dependency suggestions: %1.").arg(suggestions.size());
	result.reviewableText = QStringLiteral("- %1").arg(suggestions.join(QStringLiteral("\n- ")));
	result.diagnostics = suggestions;
	result.nextActions = {
		QCoreApplication::translate("SlopStudioAiWorkflows", "Open package info/list for the full entry set."),
		QCoreApplication::translate("SlopStudioAiWorkflows", "Add detected base packages through project or installation settings before release validation."),
	};
	result.manifest.stagedOutputs.push_back({QStringLiteral("package-dependency-suggestions"), QStringLiteral("text"), QCoreApplication::translate("SlopStudioAiWorkflows", "Package dependency suggestions"), QString(), result.summary, suggestions, false});
	applyCredentialWarning(&result.manifest, preferences);
	return result;
}

AiWorkflowResult generateCliCommandAiExperiment(const QString& prompt, const AiAutomationPreferences& preferences, const QString& providerId, const QString& modelId)
{
	const QString provider = selectedProviderId(preferences, providerId, QStringLiteral("coding"));
	const QString model = selectedModelId(preferences, provider, modelId, QStringLiteral("coding"));
	const QString lower = prompt.toLower();
	const QString input = inferredInputPath(prompt);
	QString command = QStringLiteral("slopstudio --cli --help");
	if (containsAny(lower, {QStringLiteral("validate package"), QStringLiteral("check package")})) {
		command = QStringLiteral("slopstudio --cli package validate %1").arg(quotedCliPath(input.isEmpty() ? QStringLiteral("<package-path>") : input));
	} else if (containsAny(lower, {QStringLiteral("list package"), QStringLiteral("show package")})) {
		command = QStringLiteral("slopstudio --cli package list %1").arg(quotedCliPath(input.isEmpty() ? QStringLiteral("<package-path>") : input));
	} else if (containsAny(lower, {QStringLiteral("project info"), QStringLiteral("show project")})) {
		command = QStringLiteral("slopstudio --cli project info %1").arg(quotedCliPath(input.isEmpty() ? QStringLiteral("<project-root>") : input));
	} else if (containsAny(lower, {QStringLiteral("compiler"), QStringLiteral("compile"), QStringLiteral("build map")})) {
		const QString profileId = chooseCompilerProfileId(prompt);
		command = QStringLiteral("slopstudio --cli compiler plan %1 --input %2 --dry-run").arg(profileId, quotedCliPath(input.isEmpty() ? QStringLiteral("<map-or-wad-path>") : input));
	} else if (containsAny(lower, {QStringLiteral("explain log"), QStringLiteral("compiler log")})) {
		command = QStringLiteral("slopstudio --cli ai explain-log --log %1").arg(quotedCliPath(QStringLiteral("<compiler-log-path>")));
	}

	AiWorkflowResult result;
	result.title = QCoreApplication::translate("SlopStudioAiWorkflows", "CLI Command Proposal");
	result.summary = QCoreApplication::translate("SlopStudioAiWorkflows", "Reviewable CLI command generated from the requested workflow.");
	result.commandLine = command;
	result.reviewableText = command;
	result.manifest = defaultAiWorkflowManifest(QStringLiteral("generate-cli-command"), provider, model, prompt);
	result.manifest.contextSummary = QCoreApplication::translate("SlopStudioAiWorkflows", "Prompt text and CLI command registry only.");
	result.manifest.toolCalls.push_back(toolCall(QStringLiteral("compiler-command-proposal"), QCoreApplication::translate("SlopStudioAiWorkflows", "Mapped user intent to a safe CLI command proposal."), {prompt}, {command}));
	result.manifest.stagedOutputs.push_back({QStringLiteral("cli-command"), QStringLiteral("command"), QCoreApplication::translate("SlopStudioAiWorkflows", "CLI command"), QString(), command, {command}, false});
	result.nextActions = {QCoreApplication::translate("SlopStudioAiWorkflows", "Review placeholder paths before running the command."), QCoreApplication::translate("SlopStudioAiWorkflows", "Add --json for machine-readable output or --verbose for timing diagnostics.")};
	applyCredentialWarning(&result.manifest, preferences);
	return result;
}

AiWorkflowResult fixAndRetryPlanAiExperiment(const QString& compilerLogText, const QString& commandLine, const AiAutomationPreferences& preferences, const QString& providerId, const QString& modelId)
{
	AiWorkflowResult explanation = explainCompilerLogAiExperiment(compilerLogText, preferences, providerId, modelId);
	AiWorkflowResult result = explanation;
	result.title = QCoreApplication::translate("SlopStudioAiWorkflows", "Supervised Fix And Retry Plan");
	result.manifest.workflowId = QStringLiteral("fix-and-retry-plan");
	result.manifest.prompt = QCoreApplication::translate("SlopStudioAiWorkflows", "Generate supervised fix and retry plan");
	result.manifest.contextSummary = QCoreApplication::translate("SlopStudioAiWorkflows", "Compiler log text and optional prior command line. No files are written.");
	result.manifest.toolCalls.push_back(toolCall(QStringLiteral("compiler-command-proposal"), QCoreApplication::translate("SlopStudioAiWorkflows", "Prepared a retry command review step without executing it."), {commandLine}, {commandLine.trimmed().isEmpty() ? QCoreApplication::translate("SlopStudioAiWorkflows", "No prior command supplied.") : commandLine}));
	QStringList steps;
	steps << QCoreApplication::translate("SlopStudioAiWorkflows", "1. Inspect the first fatal/error message and linked file path, if present.");
	steps << QCoreApplication::translate("SlopStudioAiWorkflows", "2. Fix the smallest likely cause: missing asset mount, map leak, bad entity key, or unsupported compiler argument.");
	steps << QCoreApplication::translate("SlopStudioAiWorkflows", "3. Re-run the command as a dry-run plan before executing.");
	steps << QCoreApplication::translate("SlopStudioAiWorkflows", "4. Save a compiler manifest when the retry command is ready.");
	if (!commandLine.trimmed().isEmpty()) {
		steps << QCoreApplication::translate("SlopStudioAiWorkflows", "Retry command to review: %1").arg(commandLine);
	}
	result.summary = QCoreApplication::translate("SlopStudioAiWorkflows", "Supervised retry plan generated from compiler diagnostics.");
	result.reviewableText = steps.join('\n');
	result.nextActions = steps;
	result.manifest.stagedOutputs.push_back({QStringLiteral("fix-and-retry-plan"), QStringLiteral("text"), QCoreApplication::translate("SlopStudioAiWorkflows", "Fix and retry plan"), QString(), result.summary, steps, false});
	return result;
}

AiWorkflowResult stageAssetGenerationRequestAiExperiment(const QString& connectorId, const QString& kind, const QString& prompt, const AiAutomationPreferences& preferences, const QString& modelId)
{
	const QString normalizedKind = normalizedAiId(kind);
	const QString capability = (normalizedKind == QStringLiteral("voice")) ? QStringLiteral("voice")
		: (normalizedKind == QStringLiteral("sound") || normalizedKind == QStringLiteral("audio")) ? QStringLiteral("audio")
		: (normalizedKind == QStringLiteral("model") || normalizedKind == QStringLiteral("mesh") || normalizedKind == QStringLiteral("three-d")) ? QStringLiteral("three-d")
		: QStringLiteral("image");
	const QString provider = selectedProviderId(preferences, connectorId, capability);
	const QString model = selectedModelId(preferences, provider, modelId, capability);

	AiWorkflowResult result;
	result.title = QCoreApplication::translate("SlopStudioAiWorkflows", "Staged Asset Generation Request");
	result.summary = QCoreApplication::translate("SlopStudioAiWorkflows", "Provider-specific asset request staged for review; no generated file is imported.");
	result.reviewableText = QStringLiteral("%1: %2\n%3: %4").arg(QCoreApplication::translate("SlopStudioAiWorkflows", "Provider"), provider, QCoreApplication::translate("SlopStudioAiWorkflows", "Prompt"), prompt);
	result.manifest = defaultAiWorkflowManifest(QStringLiteral("stage-asset-generation-request"), provider, model, prompt);
	result.manifest.contextSummary = QCoreApplication::translate("SlopStudioAiWorkflows", "Asset prompt, requested kind, connector metadata, and credential redaction status.");
	result.manifest.toolCalls.push_back(toolCall(QStringLiteral("staged-asset-generation-request"), QCoreApplication::translate("SlopStudioAiWorkflows", "Prepared generated asset request for review before import."), {provider, normalizedKind, prompt}, {QCoreApplication::translate("SlopStudioAiWorkflows", "staged request")}));
	const QString proposedPath = QDir::cleanPath(QStringLiteral(".slopstudio/ai-staged/%1-%2-request.json").arg(provider, normalizedKind.isEmpty() ? QStringLiteral("asset") : normalizedKind));
	result.manifest.stagedOutputs.push_back({QStringLiteral("asset-generation-request"), normalizedKind.isEmpty() ? QStringLiteral("asset") : normalizedKind, QCoreApplication::translate("SlopStudioAiWorkflows", "Asset generation request"), proposedPath, result.summary, {result.reviewableText}, true});
	result.nextActions = {
		QCoreApplication::translate("SlopStudioAiWorkflows", "Review provider, prompt, cost expectations, and generated-asset provenance before sending."),
		QCoreApplication::translate("SlopStudioAiWorkflows", "Import generated assets only through a staged package/project workflow."),
	};
	applyCredentialWarning(&result.manifest, preferences);
	return result;
}

AiWorkflowResult compareProviderOutputsAiExperiment(const QString& prompt, const QString& providerA, const QString& providerB, const AiAutomationPreferences& preferences, const QString& modelA, const QString& modelB)
{
	const QString firstProvider = selectedProviderId(preferences, providerA, QStringLiteral("reasoning"));
	const QString secondProvider = selectedProviderId(preferences, providerB, QStringLiteral("reasoning"));
	const QString firstModel = selectedModelId(preferences, firstProvider, modelA, QStringLiteral("reasoning"));
	const QString secondModel = selectedModelId(preferences, secondProvider, modelB, QStringLiteral("reasoning"));

	AiWorkflowResult result;
	result.title = QCoreApplication::translate("SlopStudioAiWorkflows", "Provider Output Comparison");
	result.summary = QCoreApplication::translate("SlopStudioAiWorkflows", "Comparison manifest prepared for two reasoning providers.");
	result.reviewableText = QStringLiteral("%1 / %2\n- %3\n\n%4 / %5\n- %6")
		.arg(firstProvider, firstModel, QCoreApplication::translate("SlopStudioAiWorkflows", "Would answer with the selected primary provider once configured."), secondProvider, secondModel, QCoreApplication::translate("SlopStudioAiWorkflows", "Would answer with the selected comparison provider once configured."));
	result.manifest = defaultAiWorkflowManifest(QStringLiteral("compare-provider-outputs"), firstProvider, firstModel, prompt);
	result.manifest.contextSummary = QCoreApplication::translate("SlopStudioAiWorkflows", "Prompt text and provider/model selections for side-by-side review.");
	result.manifest.toolCalls.push_back(toolCall(QStringLiteral("project-summary"), QCoreApplication::translate("SlopStudioAiWorkflows", "Captured prompt context for provider comparison."), {prompt}, {firstProvider, secondProvider}));
	result.manifest.stagedOutputs.push_back({QStringLiteral("provider-a-output"), QStringLiteral("text"), QCoreApplication::translate("SlopStudioAiWorkflows", "Primary provider output placeholder"), QString(), firstProvider, {QCoreApplication::translate("SlopStudioAiWorkflows", "Awaiting configured provider call.")}, false});
	result.manifest.stagedOutputs.push_back({QStringLiteral("provider-b-output"), QStringLiteral("text"), QCoreApplication::translate("SlopStudioAiWorkflows", "Comparison provider output placeholder"), QString(), secondProvider, {QCoreApplication::translate("SlopStudioAiWorkflows", "Awaiting configured provider call.")}, false});
	applyCredentialWarning(&result.manifest, preferences);
	return result;
}

} // namespace slopstudio
