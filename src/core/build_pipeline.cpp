#include "core/build_pipeline.h"
#include "core/level_doom_nodes.h"
#include "core/project_manifest.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QThread>

namespace vibestudio {

namespace {

// Matches the literal produced by buildCompilerCommandPlan() in
// src/core/compiler_profiles.cpp. A chained stage legitimately plans against an
// input its predecessor has not produced yet, so that single plan error is
// downgraded to a warning while planning and while dry-running. If the upstream
// literal ever changes, the comparison simply stops matching and the condition
// is reported as a hard error again.
QString missingInputErrorText()
{
	return QCoreApplication::translate("VibeStudioCompilerProfiles", "Input file does not exist.");
}

QString normalizedId(const QString& value)
{
	return value.trimmed().toLower().replace('_', '-');
}

QString absoluteCleanPath(const QString& path)
{
	if (path.trimmed().isEmpty()) {
		return {};
	}
	return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString nativePath(const QString& path)
{
	return path.isEmpty() ? QCoreApplication::translate("VibeStudioBuildPipeline", "(not resolved)") : QDir::toNativeSeparators(path);
}

void appendUnique(QStringList* values, const QString& value)
{
	if (!values || value.trimmed().isEmpty()) {
		return;
	}
	const QString trimmed = value.trimmed();
	if (!values->contains(trimmed)) {
		values->push_back(trimmed);
	}
}

bool extensionMatches(const QString& path, const QStringList& extensions)
{
	if (extensions.isEmpty()) {
		return true;
	}
	const QString suffix = QFileInfo(path).suffix().toLower();
	for (const QString& extension : extensions) {
		QString normalized = extension.toLower();
		if (normalized.startsWith('.')) {
			normalized.remove(0, 1);
		}
		if (suffix == normalized) {
			return true;
		}
	}
	return false;
}

bool containsNormalizedId(const QStringList& values, const QString& id)
{
	const QString normalized = normalizedId(id);
	for (const QString& value : values) {
		if (normalizedId(value) == normalized) {
			return true;
		}
	}
	return false;
}

QStringList stageArgumentsFor(const QMap<QString, QStringList>& arguments, const QString& stageId)
{
	if (arguments.contains(stageId)) {
		return arguments.value(stageId);
	}
	const QString normalized = normalizedId(stageId);
	for (auto it = arguments.cbegin(); it != arguments.cend(); ++it) {
		if (normalizedId(it.key()) == normalized) {
			return it.value();
		}
	}
	return {};
}

BuildPipelineStage pipelineStage(
	const QString& id,
	const QString& profileId,
	const QString& displayName,
	const QString& description,
	const QString& inputFromStageId,
	bool optional,
	bool enabledByDefault,
	const QStringList& extraArguments = {})
{
	BuildPipelineStage stage;
	stage.id = id;
	stage.profileId = profileId;
	stage.displayName = displayName;
	stage.description = description;
	stage.extraArguments = extraArguments;
	stage.inputFromStageId = inputFromStageId;
	stage.optional = optional;
	stage.enabledByDefault = enabledByDefault;
	return stage;
}

BuildPipelineDescriptor pipelineDescriptor(
	const QString& id,
	const QString& displayName,
	const QString& engineFamily,
	const QString& description,
	const QStringList& inputExtensions,
	const QString& outputExtension,
	const QVector<BuildPipelineStage>& stages)
{
	BuildPipelineDescriptor descriptor;
	descriptor.id = id;
	descriptor.displayName = displayName;
	descriptor.engineFamily = engineFamily;
	descriptor.description = description;
	descriptor.inputExtensions = inputExtensions;
	descriptor.outputExtension = outputExtension;
	descriptor.stages = stages;
	return descriptor;
}

GameLaunchProfile launchProfile(
	const QString& id,
	const QString& displayName,
	const QString& engineFamily,
	const QString& description,
	const QStringList& argumentTemplates,
	bool requiresMap)
{
	GameLaunchProfile profile;
	profile.id = id;
	profile.displayName = displayName;
	profile.engineFamily = engineFamily;
	profile.description = description;
	profile.argumentTemplates = argumentTemplates;
	profile.requiresMap = requiresMap;
	return profile;
}

// Resolves one argument group (a template's words) into concrete process
// arguments. The words are split before the tokens are replaced, so resolved
// values may contain spaces. A group whose tokens do not all resolve is
// dropped as a unit.
bool resolveArgumentWords(
	const QStringList& parts,
	const QMap<QString, QString>& tokens,
	QStringList* argumentsOut,
	QStringList* missingTokensOut)
{
	QStringList resolvedParts;
	bool complete = true;
	for (const QString& part : parts) {
		QString value = part;
		for (auto it = tokens.cbegin(); it != tokens.cend(); ++it) {
			const QString token = QStringLiteral("{%1}").arg(it.key());
			if (!value.contains(token)) {
				continue;
			}
			if (it.value().trimmed().isEmpty()) {
				complete = false;
				if (missingTokensOut && !missingTokensOut->contains(it.key())) {
					missingTokensOut->push_back(it.key());
				}
			}
			value.replace(token, it.value());
		}
		// {warp} on its own may stand for two arguments: -warp 2 3 for E2M3.
		if (part == QStringLiteral("{warp}")) {
			resolvedParts += value.split(QLatin1Char(' '), Qt::SkipEmptyParts);
			continue;
		}
		if (!value.isEmpty()) {
			resolvedParts.push_back(value);
		}
	}
	if (!complete || resolvedParts.isEmpty()) {
		return false;
	}
	if (argumentsOut) {
		*argumentsOut = resolvedParts;
	}
	return true;
}

// The {name} tokens a word uses, known or not.
QStringList tokensInWord(const QString& word)
{
	static const QRegularExpression token(QStringLiteral("\\{([A-Za-z_]+)\\}"));
	QStringList names;
	for (auto match = token.globalMatch(word); match.hasNext();) {
		const QString name = match.next().captured(1);
		if (!names.contains(name)) {
			names << name;
		}
	}
	return names;
}

QJsonArray stringArrayJson(const QStringList& values)
{
	QJsonArray array;
	for (const QString& value : values) {
		array.append(value);
	}
	return array;
}

// Splits one stage's plan/run diagnostics into the pipeline aggregate.
// Returns true when at least one genuine (non-downgraded) error was seen.
// When `errorsOut` is given the errors are collected there instead of being merged straight into
// the pipeline errors, so the caller can route them once it knows whether the stage actually failed.
bool appendStageDiagnostics(
	BuildPipelineResult* result,
	const BuildPipelineStage& stage,
	const QStringList& warnings,
	const QStringList& errors,
	const QString& inputPath,
	bool allowMissingInputDowngrade,
	QStringList* errorsOut = nullptr)
{
	if (!result) {
		return false;
	}
	for (const QString& warning : warnings) {
		appendUnique(&result->warnings, QCoreApplication::translate("VibeStudioBuildPipeline", "Stage \"%1\": %2").arg(stage.id, warning));
	}
	bool hadRealError = false;
	const QString missingInput = missingInputErrorText();
	for (const QString& error : errors) {
		if (allowMissingInputDowngrade && error.trimmed() == missingInput) {
			appendUnique(&result->warnings, QCoreApplication::translate("VibeStudioBuildPipeline", "Stage \"%1\" input %2 does not exist yet; it is produced by an earlier stage of this pipeline.").arg(stage.id, nativePath(inputPath)));
			continue;
		}
		const QString entry = QCoreApplication::translate("VibeStudioBuildPipeline", "Stage \"%1\": %2").arg(stage.id, error);
		appendUnique(errorsOut ? errorsOut : &result->errors, entry);
		hadRealError = true;
	}
	return hadRealError;
}

OperationState aggregatePipelineState(const BuildPipelineResult& result)
{
	// A stage that genuinely failed outranks a cancellation, and a cancellation outranks leftover
	// error text: a late cancel must not hide a stage that had already failed, and error-shaped
	// output from a stage that exited 0 must not mask the cancellation either.
	if (result.failedStageCount > 0) {
		return OperationState::Failed;
	}
	if (result.cancelled) {
		return OperationState::Cancelled;
	}
	if (!result.errors.isEmpty()) {
		return OperationState::Failed;
	}
	if (!result.warnings.isEmpty()) {
		return OperationState::Warning;
	}
	for (const BuildPipelineStageResult& stage : result.stages) {
		if (stage.state == OperationState::Warning) {
			return OperationState::Warning;
		}
	}
	return OperationState::Completed;
}

// The ${NAME} variables a word of a project pipeline's arguments uses.
QStringList variablesInWord(const QString& word)
{
	static const QRegularExpression variable(QStringLiteral("\\$\\{([A-Za-z_][A-Za-z0-9_]*)\\}"));
	QStringList names;
	for (auto match = variable.globalMatch(word); match.hasNext();) {
		const QString name = match.next().captured(1);
		if (!names.contains(name)) {
			names << name;
		}
	}
	return names;
}

// Every variable a project pipeline's stages can use, with its value for
// this request: the built-in ones, then the pipeline's own (which may use the
// built-in ones), then the request's, which win.
QMap<QString, QString> projectPipelineVariables(const BuildPipelineDescriptor& pipeline, const BuildPipelineRequest& request, const QString& inputPath)
{
	QMap<QString, QString> values;
	const QFileInfo input(inputPath);
	values.insert(QStringLiteral("MAP"), inputPath);
	values.insert(QStringLiteral("MAP_DIR"), inputPath.isEmpty() ? QString() : QDir::cleanPath(input.absolutePath()));
	values.insert(QStringLiteral("MAP_NAME"), inputPath.isEmpty() ? QString() : input.completeBaseName());
	QString output = absoluteCleanPath(request.outputPath);
	if (output.isEmpty() && !inputPath.isEmpty() && !pipeline.outputExtension.isEmpty()) {
		output = QDir::cleanPath(input.absolutePath() + QLatin1Char('/') + input.completeBaseName() + QLatin1Char('.') + pipeline.outputExtension);
	}
	values.insert(QStringLiteral("OUTPUT"), output);
	values.insert(QStringLiteral("PROJECT_DIR"), absoluteCleanPath(request.workspaceRootPath));
	values.insert(QStringLiteral("GAME_DIR"), QString());
	values.insert(QStringLiteral("MOD"), QString());
	values.insert(QStringLiteral("CPU_COUNT"), QString::number(std::max(1, QThread::idealThreadCount())));
	for (auto it = request.variables.cbegin(); it != request.variables.cend(); ++it) {
		if (buildPipelineVariableNames().contains(it.key().trimmed())) {
			values.insert(it.key().trimmed(), it.value());
		}
	}
	const QMap<QString, QString> builtIn = values;
	for (auto it = pipeline.variables.cbegin(); it != pipeline.variables.cend(); ++it) {
		QString value = it.value();
		for (auto known = builtIn.cbegin(); known != builtIn.cend(); ++known) {
			value.replace(QStringLiteral("${%1}").arg(known.key()), known.value());
		}
		values.insert(it.key(), value);
	}
	for (auto it = request.variables.cbegin(); it != request.variables.cend(); ++it) {
		values.insert(it.key().trimmed(), it.value());
	}
	return values;
}

// A project stage's arguments: split as a shell would, grouped from switch to
// switch, each ${NAME} filled in. A group with an empty variable is left out
// with a warning; an unknown variable is an error.
QStringList projectStageArguments(const BuildPipelineStage& stage, const QMap<QString, QString>& variables, QStringList* warnings, QStringList* errors)
{
	QStringList arguments;
	for (const QStringList& group : gameLaunchArgumentGroups(stage.argumentLine)) {
		QStringList resolved;
		QStringList empty;
		for (const QString& word : group) {
			QString value = word;
			for (const QString& name : variablesInWord(word)) {
				if (!variables.contains(name)) {
					if (errors) {
						errors->push_back(QCoreApplication::translate("VibeStudioBuildPipeline", "Stage \"%1\" uses ${%2}, which is not a variable of this pipeline.").arg(stage.id, name));
					}
					continue;
				}
				if (variables.value(name).isEmpty() && !empty.contains(name)) {
					empty << name;
				}
				value.replace(QStringLiteral("${%1}").arg(name), variables.value(name));
			}
			resolved << value;
		}
		if (!empty.isEmpty()) {
			if (warnings) {
				const QString names = QStringLiteral("${%1}").arg(empty.join(QStringLiteral("}, ${")));
				warnings->push_back(QCoreApplication::translate("VibeStudioBuildPipeline", "Stage \"%1\" leaves out \"%2\" because %3 is empty.")
					.arg(stage.id, group.join(QLatin1Char(' ')), names));
			}
			continue;
		}
		arguments += resolved;
	}
	return arguments;
}

// Resolves stages, inputs, and outputs. `stageRequestsOut` receives one entry
// per stage result (a default-constructed request for skipped stages) so the
// run phase can reuse the resolved commands without re-deriving them.
// `argumentsInvalidOut` is set when a project stage's arguments cannot be
// resolved, which stops a run before any stage starts.
BuildPipelineResult planPipelineInternal(const BuildPipelineRequest& request, QVector<CompilerCommandRequest>* stageRequestsOut, bool* argumentsInvalidOut = nullptr)
{
	BuildPipelineResult result;
	result.dryRun = request.dryRun;
	result.inputPath = absoluteCleanPath(request.inputPath);
	result.finalOutputPath = result.inputPath;
	if (argumentsInvalidOut) {
		*argumentsInvalidOut = false;
	}

	if (!buildPipelineForId(request.pipelineId, request.projectPipelines, &result.pipeline)) {
		result.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "Build pipeline \"%1\" is not known.").arg(request.pipelineId.trimmed());
		result.state = OperationState::Failed;
		return result;
	}

	if (result.inputPath.isEmpty()) {
		result.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "Pipeline input path is required.");
	} else {
		if (!QFileInfo(result.inputPath).isFile()) {
			result.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "Pipeline input file does not exist: %1").arg(nativePath(result.inputPath));
		}
		if (!extensionMatches(result.inputPath, result.pipeline.inputExtensions)) {
			result.warnings << QCoreApplication::translate("VibeStudioBuildPipeline", "Pipeline input extension does not match the expected source type for \"%1\".").arg(result.pipeline.id);
		}
	}

	const int stageCount = result.pipeline.stages.size();
	QVector<CompilerProfileDescriptor> profiles(stageCount);
	QVector<bool> skipped(stageCount, false);
	QVector<QString> skipReasons(stageCount);

	for (int index = 0; index < stageCount; ++index) {
		const BuildPipelineStage& stage = result.pipeline.stages.at(index);
		if (containsNormalizedId(request.disabledStageIds, stage.id)) {
			skipped[index] = true;
			skipReasons[index] = QCoreApplication::translate("VibeStudioBuildPipeline", "Stage was disabled for this run.");
			continue;
		}
		if (stage.optional && !stage.enabledByDefault) {
			skipped[index] = true;
			skipReasons[index] = result.pipeline.projectDefined
				? QCoreApplication::translate("VibeStudioBuildPipeline", "Stage is turned off in this project pipeline.")
				: QCoreApplication::translate("VibeStudioBuildPipeline", "Optional stage is disabled by default for this pipeline.");
			continue;
		}
		if (!compilerProfileForId(stage.profileId, &profiles[index])) {
			skipped[index] = true;
			skipReasons[index] = QCoreApplication::translate("VibeStudioBuildPipeline", "Compiler profile \"%1\" is not registered in this build.").arg(stage.profileId);
			appendUnique(&result.warnings, QCoreApplication::translate("VibeStudioBuildPipeline", "Stage \"%1\" was skipped: %2").arg(stage.id, skipReasons[index]));
			continue;
		}
	}

	// The pipeline output path belongs to the last surviving stage that can
	// actually be told where to write. Later in-place stages (vis/light) then
	// operate on that artifact.
	int outputStageIndex = -1;
	for (int index = 0; index < stageCount; ++index) {
		if (!skipped[index] && profiles[index].outputPathArgumentSupported) {
			outputStageIndex = index;
		}
	}
	const QString requestedOutputPath = absoluteCleanPath(request.outputPath);
	if (!requestedOutputPath.isEmpty() && outputStageIndex < 0) {
		appendUnique(&result.warnings, QCoreApplication::translate("VibeStudioBuildPipeline", "No stage in this pipeline accepts an explicit output path; the requested output path is ignored."));
	}

	QMap<QString, QString> stageOutputs;
	QString latestOutputPath = result.inputPath;
	const QMap<QString, QString> variables
		= result.pipeline.projectDefined ? projectPipelineVariables(result.pipeline, request, result.inputPath) : QMap<QString, QString>();

	for (int index = 0; index < stageCount; ++index) {
		const BuildPipelineStage& stage = result.pipeline.stages.at(index);
		BuildPipelineStageResult stageResult;
		stageResult.stage = stage;

		if (skipped[index]) {
			stageResult.skipped = true;
			stageResult.skipReason = skipReasons[index];
			stageResult.state = OperationState::Idle;
			stageResult.inputPath = latestOutputPath;
			result.skippedStageCount++;
			result.stages.push_back(stageResult);
			if (stageRequestsOut) {
				stageRequestsOut->push_back(CompilerCommandRequest());
			}
			continue;
		}

		QString stageInputPath;
		const QString chainedFrom = normalizedId(stage.inputFromStageId);
		if (chainedFrom.isEmpty()) {
			stageInputPath = result.inputPath;
		} else if (stageOutputs.contains(chainedFrom)) {
			stageInputPath = stageOutputs.value(chainedFrom);
		} else {
			stageInputPath = latestOutputPath;
			appendUnique(&result.warnings, QCoreApplication::translate("VibeStudioBuildPipeline", "Stage \"%1\" now consumes %2 because stage \"%3\" is not part of this run.").arg(stage.id, nativePath(stageInputPath), stage.inputFromStageId));
		}

		CompilerCommandRequest command;
		command.profileId = stage.profileId;
		command.inputPath = stageInputPath;
		command.outputPath = (index == outputStageIndex) ? requestedOutputPath : QString();
		command.workingDirectory = request.workingDirectory;
		command.workspaceRootPath = request.workspaceRootPath;
		command.extraArguments = stage.extraArguments;
		if (result.pipeline.projectDefined) {
			QStringList argumentWarnings;
			QStringList argumentErrors;
			command.extraArguments += projectStageArguments(stage, variables, &argumentWarnings, &argumentErrors);
			for (const QString& warning : std::as_const(argumentWarnings)) {
				appendUnique(&result.warnings, warning);
			}
			for (const QString& error : std::as_const(argumentErrors)) {
				appendUnique(&result.errors, error);
			}
			if (!argumentErrors.isEmpty() && argumentsInvalidOut) {
				*argumentsInvalidOut = true;
			}
		}
		command.extraArguments += stageArgumentsFor(request.stageExtraArguments, stage.id);
		command.extraSearchPaths = request.extraSearchPaths;
		command.executableOverrides = request.executableOverrides;

		stageResult.plan = buildCompilerCommandPlan(command);
		stageResult.inputPath = stageResult.plan.inputPath.isEmpty() ? stageInputPath : stageResult.plan.inputPath;
		stageResult.outputPath = stageResult.plan.expectedOutputPath;

		const bool chainedInput = !chainedFrom.isEmpty();
		const bool hadRealError = appendStageDiagnostics(
			&result,
			stage,
			stageResult.plan.warnings,
			stageResult.plan.errors,
			stageResult.inputPath,
			chainedInput && !QFileInfo(stageResult.inputPath).isFile());
		stageResult.state = stageResult.plan.state();
		if (stageResult.state == OperationState::Failed && !hadRealError) {
			stageResult.state = OperationState::Warning;
		}

		if (!stageResult.outputPath.isEmpty()) {
			stageOutputs.insert(normalizedId(stage.id), stageResult.outputPath);
			latestOutputPath = stageResult.outputPath;
		}
		result.plannedStageCount++;
		result.stages.push_back(stageResult);
		if (stageRequestsOut) {
			stageRequestsOut->push_back(command);
		}
	}

	result.finalOutputPath = latestOutputPath;
	if (result.plannedStageCount == 0) {
		appendUnique(&result.warnings, QCoreApplication::translate("VibeStudioBuildPipeline", "No stage of this pipeline is enabled; nothing would be compiled."));
	}
	result.state = aggregatePipelineState(result);
	return result;
}

} // namespace

bool BuildPipelineResult::succeeded() const
{
	if (failedStageCount > 0 || cancelled) {
		return false;
	}
	return state == OperationState::Completed || state == OperationState::Warning;
}

OperationState GameLaunchPlan::state() const
{
	if (cancelled) { return OperationState::Cancelled; }
	if (!errors.isEmpty()) {
		return OperationState::Failed;
	}
	if (runnable) {
		return warnings.isEmpty() ? OperationState::Completed : OperationState::Warning;
	}
	return profileFound ? OperationState::Warning : OperationState::Idle;
}

QVector<BuildPipelineDescriptor> buildPipelineDescriptors()
{
	return {
		pipelineDescriptor(
			QStringLiteral("quake-full"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake full compile"),
			QStringLiteral("idTech2"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Runs the complete Quake-family loop: BSP, visibility, and lighting through VibeMap2."),
			{QStringLiteral("map")},
			QStringLiteral("bsp"),
			{
				pipelineStage(
					QStringLiteral("qbsp"),
					QStringLiteral("vibemap2-bsp"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "QBSP"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Compiles the .map source into a Quake-family BSP."),
					QString(),
					false,
					true),
				pipelineStage(
					QStringLiteral("vis"),
					QStringLiteral("vibemap2-vis"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "VIS"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Computes the potentially visible set for the compiled BSP, in place."),
					QStringLiteral("qbsp"),
					true,
					true),
				pipelineStage(
					QStringLiteral("light"),
					QStringLiteral("vibemap2-light"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "LIGHT"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Computes lightmaps and light data for the compiled BSP, in place."),
					QStringLiteral("vis"),
					true,
					true),
			}),
		pipelineDescriptor(
			QStringLiteral("quake-fast"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake fast iteration"),
			QStringLiteral("idTech2"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quick edit/test loop: BSP then lighting. Visibility is disabled by default; pass fast-mode switches through stageExtraArguments."),
			{QStringLiteral("map")},
			QStringLiteral("bsp"),
			{
				pipelineStage(
					QStringLiteral("qbsp"),
					QStringLiteral("vibemap2-bsp"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "QBSP"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Compiles the .map source into a Quake-family BSP."),
					QString(),
					false,
					true),
				pipelineStage(
					QStringLiteral("vis"),
					QStringLiteral("vibemap2-vis"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "VIS"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Visibility processing, disabled by default for fast iteration."),
					QStringLiteral("qbsp"),
					true,
					false),
				pipelineStage(
					QStringLiteral("light"),
					QStringLiteral("vibemap2-light"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "LIGHT"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Lighting pass; the caller supplies fast-mode arguments."),
					QStringLiteral("vis"),
					true,
					true),
			}),
		pipelineDescriptor(
			QStringLiteral("quake-bsp-only"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake BSP only"),
			QStringLiteral("idTech2"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Compiles geometry only, without visibility or lighting."),
			{QStringLiteral("map")},
			QStringLiteral("bsp"),
			{
				pipelineStage(
					QStringLiteral("qbsp"),
					QStringLiteral("vibemap2-bsp"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "QBSP"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Compiles the .map source into a Quake-family BSP."),
					QString(),
					false,
					true),
			}),
		pipelineDescriptor(
			QStringLiteral("quake3-full"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake III full compile"),
			QStringLiteral("idTech3"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Runs the complete Quake III-family loop: BSP, visibility, and lighting through VibeMap3."),
			{QStringLiteral("map")},
			QStringLiteral("bsp"),
			{
				pipelineStage(
					QStringLiteral("bsp"),
					QStringLiteral("vibemap3-bsp"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "BSP"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Builds the Quake III-family BSP from the .map source."),
					QString(),
					false,
					true),
				pipelineStage(
					QStringLiteral("vis"),
					QStringLiteral("vibemap3-vis"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "VIS"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Computes the potentially visible set for the compiled BSP, in place."),
					QStringLiteral("bsp"),
					true,
					true),
				pipelineStage(
					QStringLiteral("light"),
					QStringLiteral("vibemap3-light"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "LIGHT"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Computes lightmaps for the compiled BSP, in place."),
					QStringLiteral("vis"),
					true,
					true),
			}),
		pipelineDescriptor(
			QStringLiteral("quake3-bsp-only"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake III BSP only"),
			QStringLiteral("idTech3"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Compiles Quake III-family geometry only, without visibility or lighting."),
			{QStringLiteral("map")},
			QStringLiteral("bsp"),
			{
				pipelineStage(
					QStringLiteral("bsp"),
					QStringLiteral("vibemap3-bsp"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "BSP"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Builds the Quake III-family BSP from the .map source."),
					QString(),
					false,
					true),
			}),
		pipelineDescriptor(
			QStringLiteral("doom-zdbsp"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Doom nodes (ZDBSP)"),
			QStringLiteral("idTech1"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Builds Doom-family map nodes with ZDBSP."),
			{QStringLiteral("wad")},
			QStringLiteral("wad"),
			{
				pipelineStage(
					QStringLiteral("nodes"),
					QStringLiteral("zdbsp-nodes"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Nodes"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Builds nodes, blockmap, and reject data for a Doom-family WAD."),
					QString(),
					false,
					true),
			}),
		pipelineDescriptor(
			QStringLiteral("doom-zokumbsp"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Doom nodes (ZokumBSP)"),
			QStringLiteral("idTech1"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Builds Doom-family map nodes with ZokumBSP."),
			{QStringLiteral("wad")},
			QStringLiteral("wad"),
			{
				pipelineStage(
					QStringLiteral("nodes"),
					QStringLiteral("zokumbsp-nodes"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Nodes"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Builds nodes, blockmap, and reject data for a Doom-family WAD."),
					QString(),
					false,
					true),
			}),
	};
}

QStringList buildPipelineIds()
{
	QStringList ids;
	for (const BuildPipelineDescriptor& descriptor : buildPipelineDescriptors()) {
		ids.push_back(descriptor.id);
	}
	return ids;
}

bool buildPipelineForId(const QString& id, BuildPipelineDescriptor* out)
{
	return buildPipelineForId(id, {}, out);
}

bool buildPipelineForId(const QString& id, const QVector<BuildPipelineDescriptor>& projectPipelines, BuildPipelineDescriptor* out)
{
	const QString normalized = normalizedId(id);
	if (normalized.isEmpty()) {
		return false;
	}
	for (const BuildPipelineDescriptor& descriptor : projectPipelines + buildPipelineDescriptors()) {
		if (normalizedId(descriptor.id) == normalized) {
			if (out) {
				*out = descriptor;
			}
			return true;
		}
	}
	return false;
}

QStringList buildPipelineIdsForInput(const QString& inputPath)
{
	QStringList ids;
	if (inputPath.trimmed().isEmpty()) {
		return ids;
	}
	for (const BuildPipelineDescriptor& descriptor : buildPipelineDescriptors()) {
		if (!descriptor.inputExtensions.isEmpty() && extensionMatches(inputPath, descriptor.inputExtensions)) {
			ids.push_back(descriptor.id);
		}
	}
	return ids;
}

// Project build pipelines. Workflow reference: NetRadiant Custom's build menu
// (DoBuildMenu and build_commands_parse/write with [RadiantPath]-style
// variables in radiant/build.cpp at 449778b3, GPL-2.0-or-later) and
// TrenchBroom's compilation profiles (CompilationProfileManager and the
// ${MAP_BASE_NAME}, ${CPU_COUNT} variables of CompilationVariables at
// 4c756be1, GPL-3.0-or-later): named lists of compiler commands with
// arguments and variables. Original Qt implementation over VibeStudio's
// compiler profiles, kept in the project manifest; see docs/CREDITS.md.
QStringList buildPipelineVariableNames()
{
	return {QStringLiteral("MAP"), QStringLiteral("MAP_DIR"), QStringLiteral("MAP_NAME"), QStringLiteral("OUTPUT"), QStringLiteral("PROJECT_DIR"),
		QStringLiteral("GAME_DIR"), QStringLiteral("MOD"), QStringLiteral("CPU_COUNT")};
}

QString projectBuildPipelineId(const QString& name, const QVector<BuildPipelineDescriptor>& existing)
{
	QString slug;
	for (const QChar character : name.trimmed().toLower()) {
		if ((character >= QLatin1Char('a') && character <= QLatin1Char('z')) || (character >= QLatin1Char('0') && character <= QLatin1Char('9'))) {
			slug += character;
		} else if (!slug.isEmpty() && !slug.endsWith(QLatin1Char('-'))) {
			slug += QLatin1Char('-');
		}
	}
	while (slug.endsWith(QLatin1Char('-'))) {
		slug.chop(1);
	}
	const QString base = slug.isEmpty() ? QStringLiteral("pipeline") : slug.left(48);
	const auto taken = [&existing](const QString& id) {
		for (const BuildPipelineDescriptor& pipeline : existing) {
			if (normalizedId(pipeline.id) == normalizedId(id)) {
				return true;
			}
		}
		return buildPipelineForId(id);
	};
	QString id = base;
	for (int copy = 2; taken(id); ++copy) {
		id = QStringLiteral("%1-%2").arg(base).arg(copy);
	}
	return id;
}

QString projectBuildPipelineStageId(const QString& profileId, const BuildPipelineDescriptor& pipeline)
{
	CompilerProfileDescriptor profile;
	QString base = compilerProfileForId(profileId, &profile) && !profile.stageId.trimmed().isEmpty() ? normalizedId(profile.stageId) : QStringLiteral("stage");
	static const QRegularExpression unsafe(QStringLiteral("[^a-z0-9-]+"));
	base.replace(unsafe, QStringLiteral("-"));
	if (base.isEmpty()) {
		base = QStringLiteral("stage");
	}
	const auto taken = [&pipeline](const QString& id) {
		for (const BuildPipelineStage& stage : pipeline.stages) {
			if (normalizedId(stage.id) == id) {
				return true;
			}
		}
		return false;
	};
	QString id = base;
	for (int copy = 2; taken(id); ++copy) {
		id = QStringLiteral("%1-%2").arg(base).arg(copy);
	}
	return id;
}

void normalizeProjectBuildPipeline(BuildPipelineDescriptor* pipeline)
{
	if (!pipeline) {
		return;
	}
	pipeline->projectDefined = true;
	CompilerProfileDescriptor first;
	if (!pipeline->stages.isEmpty() && compilerProfileForId(pipeline->stages.constFirst().profileId, &first)) {
		pipeline->engineFamily = first.engineFamily;
		pipeline->inputExtensions = first.inputExtensions;
		pipeline->outputExtension = first.defaultOutputExtension;
	}
	for (qsizetype index = 0; index < pipeline->stages.size(); ++index) {
		BuildPipelineStage& stage = pipeline->stages[index];
		// Every stage can be turned off; the first one always reads the map.
		stage.optional = true;
		stage.inputFromPrevious = index > 0 && stage.inputFromPrevious;
		stage.inputFromStageId = stage.inputFromPrevious ? pipeline->stages.at(index - 1).id : QString();
		stage.extraArguments.clear();
		if (stage.displayName.trimmed().isEmpty()) {
			CompilerProfileDescriptor profile;
			stage.displayName = compilerProfileForId(stage.profileId, &profile) ? profile.displayName : stage.id;
		}
	}
}

BuildPipelineDescriptor projectBuildPipelineFrom(const BuildPipelineDescriptor& source, const QString& name, const QVector<BuildPipelineDescriptor>& existing)
{
	BuildPipelineDescriptor pipeline;
	pipeline.id = projectBuildPipelineId(name, existing);
	pipeline.displayName = name.trimmed();
	pipeline.description = source.description;
	pipeline.variables = source.variables;
	for (const BuildPipelineStage& from : source.stages) {
		BuildPipelineStage stage;
		stage.id = from.id;
		stage.profileId = from.profileId;
		stage.displayName = from.displayName;
		stage.description = from.description;
		stage.enabledByDefault = from.enabledByDefault;
		// A built-in stage's fixed arguments become its typed line.
		stage.argumentLine = from.argumentLine.isEmpty() ? compilerCommandLineText(QString(), from.extraArguments).trimmed() : from.argumentLine;
		stage.inputFromPrevious = from.inputFromPrevious || !from.inputFromStageId.trimmed().isEmpty();
		pipeline.stages.push_back(stage);
	}
	normalizeProjectBuildPipeline(&pipeline);
	return pipeline;
}

QStringList projectBuildPipelineProblems(const BuildPipelineDescriptor& pipeline, const QVector<BuildPipelineDescriptor>& projectPipelines)
{
	QStringList problems;
	static const QRegularExpression idPattern(QStringLiteral("^[a-z0-9][a-z0-9-]{0,63}$"));
	static const QRegularExpression variablePattern(QStringLiteral("^[A-Z][A-Z0-9_]{0,31}$"));
	if (!idPattern.match(pipeline.id).hasMatch()) {
		problems << QCoreApplication::translate("VibeStudioBuildPipeline", "A pipeline id uses lower-case letters, digits and hyphens.");
	} else if (buildPipelineForId(pipeline.id)) {
		problems << QCoreApplication::translate("VibeStudioBuildPipeline", "\"%1\" is a built-in pipeline; choose another id.").arg(pipeline.id);
	}
	if (pipeline.displayName.trimmed().isEmpty()) {
		problems << QCoreApplication::translate("VibeStudioBuildPipeline", "Give the pipeline a name.");
	}
	int sameId = 0;
	int sameName = 0;
	for (const BuildPipelineDescriptor& other : projectPipelines) {
		sameId += normalizedId(other.id) == normalizedId(pipeline.id) ? 1 : 0;
		sameName += !pipeline.displayName.trimmed().isEmpty() && other.displayName.trimmed().compare(pipeline.displayName.trimmed(), Qt::CaseInsensitive) == 0 ? 1 : 0;
	}
	if (sameId > 1) {
		problems << QCoreApplication::translate("VibeStudioBuildPipeline", "Another pipeline of the project has the id \"%1\".").arg(pipeline.id);
	}
	if (sameName > 1) {
		problems << QCoreApplication::translate("VibeStudioBuildPipeline", "Another pipeline of the project is called \"%1\".").arg(pipeline.displayName.trimmed());
	}
	const QStringList builtInVariables = buildPipelineVariableNames();
	for (auto it = pipeline.variables.cbegin(); it != pipeline.variables.cend(); ++it) {
		if (!variablePattern.match(it.key()).hasMatch()) {
			problems << QCoreApplication::translate("VibeStudioBuildPipeline", "Variable \"%1\": a name uses capital letters, digits and underscores, starting with a letter.").arg(it.key());
		} else if (builtInVariables.contains(it.key())) {
			problems << QCoreApplication::translate("VibeStudioBuildPipeline", "${%1} is a built-in variable; choose another name.").arg(it.key());
		}
	}
	if (pipeline.stages.isEmpty()) {
		problems << QCoreApplication::translate("VibeStudioBuildPipeline", "Add at least one stage.");
	}
	QString family;
	QStringList stageIds;
	for (const BuildPipelineStage& stage : pipeline.stages) {
		const QString label = stage.displayName.trimmed().isEmpty() ? stage.id : stage.displayName.trimmed();
		if (!idPattern.match(stage.id).hasMatch()) {
			problems << QCoreApplication::translate("VibeStudioBuildPipeline", "Stage \"%1\": a stage id uses lower-case letters, digits and hyphens.").arg(label);
		} else if (stageIds.contains(stage.id)) {
			problems << QCoreApplication::translate("VibeStudioBuildPipeline", "Two stages have the id \"%1\".").arg(stage.id);
		}
		stageIds << stage.id;
		CompilerProfileDescriptor profile;
		if (!compilerProfileForId(stage.profileId, &profile)) {
			problems << QCoreApplication::translate("VibeStudioBuildPipeline", "Stage \"%1\": compiler profile \"%2\" is not known.").arg(label, stage.profileId);
		} else if (family.isEmpty()) {
			family = profile.engineFamily;
		} else if (profile.engineFamily.compare(family, Qt::CaseInsensitive) != 0) {
			problems << QCoreApplication::translate("VibeStudioBuildPipeline", "Stage \"%1\" is a %2 compiler, but the pipeline starts with a %3 one.").arg(label, profile.engineFamily, family);
		}
		if (stage.argumentLine.count(QLatin1Char('"')) % 2 != 0) {
			problems << QCoreApplication::translate("VibeStudioBuildPipeline", "Stage \"%1\": the arguments have an unclosed quote.").arg(label);
		}
		for (const QString& word : splitGameLaunchArguments(stage.argumentLine)) {
			for (const QString& name : variablesInWord(word)) {
				if (!builtInVariables.contains(name) && !pipeline.variables.contains(name)) {
					problems << QCoreApplication::translate("VibeStudioBuildPipeline", "Stage \"%1\" uses ${%2}, which is not a variable of this pipeline.").arg(label, name);
				}
			}
		}
	}
	problems.removeDuplicates();
	return problems;
}

QJsonObject projectBuildPipelineJson(const BuildPipelineDescriptor& pipeline)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), pipeline.id);
	object.insert(QStringLiteral("name"), pipeline.displayName);
	if (!pipeline.description.trimmed().isEmpty()) {
		object.insert(QStringLiteral("description"), pipeline.description);
	}
	if (!pipeline.variables.isEmpty()) {
		QJsonObject variables;
		for (auto it = pipeline.variables.cbegin(); it != pipeline.variables.cend(); ++it) {
			variables.insert(it.key(), it.value());
		}
		object.insert(QStringLiteral("variables"), variables);
	}
	QJsonArray stages;
	for (qsizetype index = 0; index < pipeline.stages.size(); ++index) {
		const BuildPipelineStage& stage = pipeline.stages.at(index);
		QJsonObject stageObject;
		stageObject.insert(QStringLiteral("id"), stage.id);
		stageObject.insert(QStringLiteral("name"), stage.displayName);
		stageObject.insert(QStringLiteral("profile"), stage.profileId);
		stageObject.insert(QStringLiteral("arguments"), stage.argumentLine);
		stageObject.insert(QStringLiteral("enabled"), stage.enabledByDefault);
		stageObject.insert(QStringLiteral("input"), index > 0 && stage.inputFromPrevious ? QStringLiteral("previous") : QStringLiteral("map"));
		stages.append(stageObject);
	}
	object.insert(QStringLiteral("stages"), stages);
	return object;
}

bool projectBuildPipelineFromJson(const QJsonObject& object, BuildPipelineDescriptor* out)
{
	BuildPipelineDescriptor pipeline;
	pipeline.id = object.value(QStringLiteral("id")).toString().trimmed();
	pipeline.displayName = object.value(QStringLiteral("name")).toString().trimmed();
	pipeline.description = object.value(QStringLiteral("description")).toString().trimmed();
	const QJsonObject variables = object.value(QStringLiteral("variables")).toObject();
	for (auto it = variables.constBegin(); it != variables.constEnd(); ++it) {
		pipeline.variables.insert(it.key(), it.value().toString());
	}
	for (const QJsonValue& value : object.value(QStringLiteral("stages")).toArray()) {
		const QJsonObject stageObject = value.toObject();
		BuildPipelineStage stage;
		stage.id = stageObject.value(QStringLiteral("id")).toString().trimmed();
		stage.displayName = stageObject.value(QStringLiteral("name")).toString().trimmed();
		stage.profileId = stageObject.value(QStringLiteral("profile")).toString().trimmed();
		stage.argumentLine = stageObject.value(QStringLiteral("arguments")).toString().trimmed();
		stage.enabledByDefault = stageObject.value(QStringLiteral("enabled")).toBool(true);
		stage.inputFromPrevious = stageObject.value(QStringLiteral("input")).toString() == QLatin1String("previous");
		pipeline.stages.push_back(stage);
	}
	normalizeProjectBuildPipeline(&pipeline);
	if (out) {
		*out = pipeline;
	}
	return !pipeline.id.isEmpty() || !pipeline.displayName.isEmpty() || !pipeline.stages.isEmpty();
}

QVector<BuildPipelineDescriptor> projectBuildPipelines(const ProjectManifest& manifest, QStringList* warnings, bool keepInvalid)
{
	QVector<BuildPipelineDescriptor> all;
	for (const QJsonValue& value : manifest.buildPipelines) {
		BuildPipelineDescriptor pipeline;
		if (projectBuildPipelineFromJson(value.toObject(), &pipeline)) {
			all.push_back(pipeline);
		}
	}
	if (keepInvalid) {
		return all;
	}
	QVector<BuildPipelineDescriptor> usable;
	for (const BuildPipelineDescriptor& pipeline : std::as_const(all)) {
		const QStringList problems = projectBuildPipelineProblems(pipeline, all);
		if (problems.isEmpty()) {
			usable.push_back(pipeline);
		} else if (warnings) {
			warnings->push_back(QCoreApplication::translate("VibeStudioBuildPipeline", "Project pipeline \"%1\" is left out: %2")
				.arg(pipeline.displayName.isEmpty() ? pipeline.id : pipeline.displayName, problems.join(QLatin1Char(' '))));
		}
	}
	return usable;
}

void setProjectBuildPipelines(ProjectManifest* manifest, const QVector<BuildPipelineDescriptor>& pipelines)
{
	if (!manifest) {
		return;
	}
	QJsonArray array;
	for (BuildPipelineDescriptor pipeline : pipelines) {
		normalizeProjectBuildPipeline(&pipeline);
		array.append(projectBuildPipelineJson(pipeline));
	}
	manifest->buildPipelines = array;
}

QJsonObject buildPipelineDescriptorJson(const BuildPipelineDescriptor& pipeline)
{
	QJsonObject entry;
	entry.insert(QStringLiteral("id"), pipeline.id);
	entry.insert(QStringLiteral("displayName"), pipeline.displayName);
	entry.insert(QStringLiteral("engineFamily"), pipeline.engineFamily);
	entry.insert(QStringLiteral("description"), pipeline.description);
	entry.insert(QStringLiteral("inputExtensions"), QJsonArray::fromStringList(pipeline.inputExtensions));
	entry.insert(QStringLiteral("source"), pipeline.projectDefined ? QStringLiteral("project") : QStringLiteral("built-in"));
	if (pipeline.projectDefined) {
		QJsonObject variables;
		for (auto it = pipeline.variables.cbegin(); it != pipeline.variables.cend(); ++it) {
			variables.insert(it.key(), it.value());
		}
		entry.insert(QStringLiteral("variables"), variables);
	}
	QJsonArray stages;
	for (const BuildPipelineStage& stage : pipeline.stages) {
		QJsonObject stageObject;
		stageObject.insert(QStringLiteral("id"), stage.id);
		stageObject.insert(QStringLiteral("profileId"), stage.profileId);
		stageObject.insert(QStringLiteral("displayName"), stage.displayName);
		stageObject.insert(QStringLiteral("optional"), stage.optional);
		stageObject.insert(QStringLiteral("enabledByDefault"), stage.enabledByDefault);
		stageObject.insert(QStringLiteral("inputFromStageId"), stage.inputFromStageId);
		if (pipeline.projectDefined) {
			stageObject.insert(QStringLiteral("arguments"), stage.argumentLine);
		}
		stages.append(stageObject);
	}
	entry.insert(QStringLiteral("stages"), stages);
	return entry;
}

BuildPipelineResult planBuildPipeline(const BuildPipelineRequest& request)
{
	return planPipelineInternal(request, nullptr);
}

BuildPipelineResult runBuildPipeline(const BuildPipelineRequest& request, const BuildPipelineCallbacks& callbacks)
{
	QVector<CompilerCommandRequest> stageRequests;
	bool argumentsInvalid = false;
	BuildPipelineResult result = planPipelineInternal(request, &stageRequests, &argumentsInvalid);
	if (result.pipeline.id.isEmpty()) {
		return result;
	}
	if (argumentsInvalid) {
		// A stage would run with an argument that cannot be resolved: nothing starts.
		result.state = OperationState::Failed;
		return result;
	}

	QElapsedTimer timer;
	timer.start();

	QString manifestDirectory = absoluteCleanPath(request.manifestDirectory);
	if (!manifestDirectory.isEmpty() && !QDir().mkpath(manifestDirectory)) {
		appendUnique(&result.warnings, QCoreApplication::translate("VibeStudioBuildPipeline", "Could not create the manifest directory %1; stage manifests will not be written.").arg(nativePath(manifestDirectory)));
		manifestDirectory.clear();
	}

	bool stopped = false;
	for (int index = 0; index < result.stages.size(); ++index) {
		BuildPipelineStageResult& stageResult = result.stages[index];
		if (stageResult.skipped) {
			continue;
		}

		if (!stopped && callbacks.cancellationRequested && callbacks.cancellationRequested()) {
			result.cancelled = true;
			stopped = true;
		}

		if (stopped) {
			stageResult.skipped = true;
			stageResult.state = OperationState::Cancelled;
			stageResult.skipReason = result.cancelled
				? QCoreApplication::translate("VibeStudioBuildPipeline", "Pipeline was cancelled before this stage started.")
				: QCoreApplication::translate("VibeStudioBuildPipeline", "An earlier stage failed and the pipeline stops on failure.");
			result.plannedStageCount = qMax(0, result.plannedStageCount - 1);
			result.skippedStageCount++;
			if (callbacks.stageFinished) {
				callbacks.stageFinished(index, stageResult);
			}
			continue;
		}

		if (callbacks.stageStarted) {
			callbacks.stageStarted(index, stageResult.stage);
		}
		stageResult.state = OperationState::Running;

		CompilerRunRequest runRequest;
		runRequest.command = stageRequests.value(index);
		runRequest.dryRun = request.dryRun;
		runRequest.registerOutputs = request.registerOutputs;
		runRequest.timeoutMs = request.stageTimeoutMs;
		if (!manifestDirectory.isEmpty()) {
			runRequest.manifestPath = QDir(manifestDirectory).filePath(QStringLiteral("%1.%2.json").arg(result.pipeline.id, stageResult.stage.id));
		}

		CompilerRunCallbacks runCallbacks;
		runCallbacks.cancellationRequested = callbacks.cancellationRequested;
		runCallbacks.logEntry = callbacks.logEntry;

		stageResult.run = runCompilerCommand(runRequest, runCallbacks);
		stageResult.plan = stageResult.run.plan;
		stageResult.manifestPath = stageResult.run.manifestPath;
		if (!stageResult.plan.inputPath.isEmpty()) {
			stageResult.inputPath = stageResult.plan.inputPath;
		}
		if (!stageResult.plan.expectedOutputPath.isEmpty()) {
			stageResult.outputPath = stageResult.plan.expectedOutputPath;
		}

		const bool chainedInput = !stageResult.stage.inputFromStageId.trimmed().isEmpty();
		QStringList stageErrors;
		const bool hadRealError = appendStageDiagnostics(
			&result,
			stageResult.stage,
			stageResult.run.manifest.warnings,
			stageResult.run.manifest.errors,
			stageResult.inputPath,
			request.dryRun && chainedInput && !QFileInfo(stageResult.inputPath).isFile(),
			&stageErrors);

		for (const CompilerDiagnostic& diagnostic : stageResult.run.diagnostics) {
			result.diagnostics.push_back(diagnostic);
		}
		for (const QString& output : stageResult.run.registeredOutputPaths) {
			appendUnique(&result.registeredOutputPaths, output);
		}

		stageResult.state = stageResult.run.state;
		const bool stageCancelled = stageResult.run.cancelled || stageResult.run.state == OperationState::Cancelled;
		bool stageFailed = false;
		if (!stageCancelled) {
			if (!stageResult.run.error.trimmed().isEmpty() || stageResult.run.timedOut) {
				stageFailed = true;
			} else if (stageResult.run.started && stageResult.run.exitCode != 0) {
				stageFailed = true;
			} else if (stageResult.run.state == OperationState::Failed && hadRealError) {
				stageFailed = true;
			} else if (stageResult.run.state == OperationState::Failed) {
				// The only errors were downgraded chained-input notices.
				stageResult.state = OperationState::Warning;
			}
		}

		if (stageCancelled) {
			result.cancelled = true;
			stageResult.state = OperationState::Cancelled;
			stopped = true;
		} else if (stageFailed) {
			stageResult.state = OperationState::Failed;
			result.failedStageCount++;
			if (request.stopOnFailure) {
				stopped = true;
			}
		} else {
			result.completedStageCount++;
		}

		// A stage that exited 0 still produced its artifacts. VibeMap2 prints non-fatal
		// "ERROR: ..." notices and carries on (common/bspfile_common.cc, common/bspxfile.cc), so
		// those lines are reported as warnings instead of failing a pipeline whose every stage
		// succeeded. The same applies to the partial output of a cancelled stage, which would
		// otherwise be reported as a failure rather than a cancellation.
		for (const QString& error : stageErrors) {
			appendUnique(stageFailed ? &result.errors : &result.warnings, error);
		}

		if (callbacks.stageFinished) {
			callbacks.stageFinished(index, stageResult);
		}
	}

	if (!result.cancelled && callbacks.cancellationRequested && callbacks.cancellationRequested()) {
		result.cancelled = true;
	}

	// The final artifact is the output of the last stage that actually survived.
	QString finalOutputPath = result.inputPath;
	for (const BuildPipelineStageResult& stageResult : result.stages) {
		if (!stageResult.skipped && !stageResult.outputPath.isEmpty()) {
			finalOutputPath = stageResult.outputPath;
		}
	}
	result.finalOutputPath = finalOutputPath;
	result.durationMs = timer.elapsed();
	result.state = aggregatePipelineState(result);
	return result;
}

QString buildPipelineResultText(const BuildPipelineResult& result)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Build pipeline result");
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Pipeline: %1").arg(result.pipeline.id.isEmpty() ? QCoreApplication::translate("VibeStudioBuildPipeline", "(unknown)") : result.pipeline.id);
	if (!result.pipeline.displayName.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Name: %1").arg(result.pipeline.displayName);
	}
	if (!result.pipeline.engineFamily.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Engine: %1").arg(result.pipeline.engineFamily);
	}
	if (result.pipeline.projectDefined) {
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Source: the project's own pipeline");
	}
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "State: %1").arg(operationStateId(result.state));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Succeeded: %1").arg(result.succeeded() ? QCoreApplication::translate("VibeStudioBuildPipeline", "yes") : QCoreApplication::translate("VibeStudioBuildPipeline", "no"));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Dry run: %1").arg(result.dryRun ? QCoreApplication::translate("VibeStudioBuildPipeline", "yes") : QCoreApplication::translate("VibeStudioBuildPipeline", "no"));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Cancelled: %1").arg(result.cancelled ? QCoreApplication::translate("VibeStudioBuildPipeline", "yes") : QCoreApplication::translate("VibeStudioBuildPipeline", "no"));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Input: %1").arg(nativePath(result.inputPath));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Final output: %1").arg(nativePath(result.finalOutputPath));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Stages: %1 planned, %2 completed, %3 failed, %4 skipped")
		.arg(result.plannedStageCount)
		.arg(result.completedStageCount)
		.arg(result.failedStageCount)
		.arg(result.skippedStageCount);
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Duration: %1").arg(result.durationMs >= 0 ? QCoreApplication::translate("VibeStudioBuildPipeline", "%1 ms").arg(result.durationMs) : QCoreApplication::translate("VibeStudioBuildPipeline", "not run"));

	for (const BuildPipelineStageResult& stageResult : result.stages) {
		lines << QString();
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Stage %1 (%2)").arg(stageResult.stage.id, stageResult.stage.profileId);
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "  State: %1").arg(operationStateId(stageResult.state));
		if (stageResult.skipped) {
			lines << QCoreApplication::translate("VibeStudioBuildPipeline", "  Skipped: %1").arg(stageResult.skipReason);
			continue;
		}
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "  Input: %1").arg(nativePath(stageResult.inputPath));
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "  Output: %1").arg(nativePath(stageResult.outputPath));
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "  Command line: %1").arg(stageResult.plan.commandLine);
		if (!stageResult.manifestPath.isEmpty()) {
			lines << QCoreApplication::translate("VibeStudioBuildPipeline", "  Manifest: %1").arg(nativePath(stageResult.manifestPath));
		}
		if (stageResult.run.exitCode >= 0) {
			lines << QCoreApplication::translate("VibeStudioBuildPipeline", "  Exit code: %1").arg(stageResult.run.exitCode);
		}
	}

	if (!result.registeredOutputPaths.isEmpty()) {
		lines << QString();
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Registered outputs");
		for (const QString& output : result.registeredOutputPaths) {
			lines << QStringLiteral("- %1").arg(nativePath(output));
		}
	}
	if (!result.warnings.isEmpty()) {
		lines << QString();
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Warnings");
		for (const QString& warning : result.warnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	if (!result.errors.isEmpty()) {
		lines << QString();
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Errors");
		for (const QString& error : result.errors) {
			lines << QStringLiteral("- %1").arg(error);
		}
	}
	return lines.join('\n');
}

QJsonObject buildPipelineResultJson(const BuildPipelineResult& result)
{
	QJsonObject object;
	object.insert(QStringLiteral("pipelineId"), result.pipeline.id);
	object.insert(QStringLiteral("displayName"), result.pipeline.displayName);
	object.insert(QStringLiteral("engineFamily"), result.pipeline.engineFamily);
	object.insert(QStringLiteral("source"), result.pipeline.projectDefined ? QStringLiteral("project") : QStringLiteral("built-in"));
	object.insert(QStringLiteral("state"), operationStateId(result.state));
	object.insert(QStringLiteral("succeeded"), result.succeeded());
	object.insert(QStringLiteral("dryRun"), result.dryRun);
	object.insert(QStringLiteral("cancelled"), result.cancelled);
	object.insert(QStringLiteral("inputPath"), result.inputPath);
	object.insert(QStringLiteral("finalOutputPath"), result.finalOutputPath);
	object.insert(QStringLiteral("durationMs"), static_cast<double>(result.durationMs));
	object.insert(QStringLiteral("plannedStageCount"), result.plannedStageCount);
	object.insert(QStringLiteral("completedStageCount"), result.completedStageCount);
	object.insert(QStringLiteral("failedStageCount"), result.failedStageCount);
	object.insert(QStringLiteral("skippedStageCount"), result.skippedStageCount);

	QJsonArray stages;
	for (const BuildPipelineStageResult& stageResult : result.stages) {
		QJsonObject stageObject;
		stageObject.insert(QStringLiteral("id"), stageResult.stage.id);
		stageObject.insert(QStringLiteral("profileId"), stageResult.stage.profileId);
		stageObject.insert(QStringLiteral("displayName"), stageResult.stage.displayName);
		stageObject.insert(QStringLiteral("state"), operationStateId(stageResult.state));
		stageObject.insert(QStringLiteral("skipped"), stageResult.skipped);
		if (!stageResult.skipReason.isEmpty()) {
			stageObject.insert(QStringLiteral("skipReason"), stageResult.skipReason);
		}
		stageObject.insert(QStringLiteral("inputPath"), stageResult.inputPath);
		stageObject.insert(QStringLiteral("outputPath"), stageResult.outputPath);
		stageObject.insert(QStringLiteral("program"), stageResult.plan.program);
		stageObject.insert(QStringLiteral("arguments"), stringArrayJson(stageResult.plan.arguments));
		stageObject.insert(QStringLiteral("commandLine"), stageResult.plan.commandLine);
		stageObject.insert(QStringLiteral("runnable"), stageResult.plan.isRunnable());
		if (!stageResult.manifestPath.isEmpty()) {
			stageObject.insert(QStringLiteral("manifestPath"), stageResult.manifestPath);
		}
		stageObject.insert(QStringLiteral("started"), stageResult.run.started);
		stageObject.insert(QStringLiteral("exitCode"), stageResult.run.exitCode);
		stageObject.insert(QStringLiteral("durationMs"), static_cast<double>(stageResult.run.durationMs));
		stages.append(stageObject);
	}
	object.insert(QStringLiteral("stages"), stages);

	QJsonArray diagnostics;
	for (const CompilerDiagnostic& diagnostic : result.diagnostics) {
		QJsonObject diagnosticObject;
		diagnosticObject.insert(QStringLiteral("level"), diagnostic.level);
		diagnosticObject.insert(QStringLiteral("message"), diagnostic.message);
		if (!diagnostic.filePath.isEmpty()) {
			diagnosticObject.insert(QStringLiteral("filePath"), diagnostic.filePath);
			diagnosticObject.insert(QStringLiteral("line"), diagnostic.line);
			diagnosticObject.insert(QStringLiteral("column"), diagnostic.column);
		}
		diagnostics.append(diagnosticObject);
	}
	object.insert(QStringLiteral("diagnostics"), diagnostics);
	object.insert(QStringLiteral("registeredOutputPaths"), stringArrayJson(result.registeredOutputPaths));
	object.insert(QStringLiteral("warnings"), stringArrayJson(result.warnings));
	object.insert(QStringLiteral("errors"), stringArrayJson(result.errors));
	return object;
}

QVector<GameLaunchProfile> gameLaunchProfiles()
{
	// Command-line switches below are the documented public options of the
	// respective engines and their source ports:
	// - Quake / QuakeWorld engines: https://quakewiki.org/wiki/Command_line_parameters
	// - Quake II engines: https://www.quake2.com/q2guide/q2cmdline.html
	// - Quake III / ioquake3: https://ioquake3.org/help/command-line-options/
	// - Doom source ports (ZDoom family): https://zdoom.org/wiki/Command_line_parameters
	return {
		launchProfile(
			QStringLiteral("quake-source-port"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake source port"),
			QStringLiteral("idTech2"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake-family engines and source ports. {mod} is a game directory name such as id1."),
			{
				QStringLiteral("-basedir {basedir}"),
				QStringLiteral("-game {mod}"),
				QStringLiteral("+map {map}"),
			},
			true),
		launchProfile(
			QStringLiteral("quake2-source-port"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake II source port"),
			QStringLiteral("idTech2"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake II engines and source ports. {mod} is a game directory name such as baseq2."),
			{
				// Quake-2 qcommon/files.c FS_InitFilesystem reads a cvar; common.c
				// applies early +set commands before filesystem initialization.
				QStringLiteral("+set basedir {basedir}"),
				QStringLiteral("+set game {mod}"),
				QStringLiteral("+map {map}"),
			},
			true),
		launchProfile(
			QStringLiteral("quake3-source-port"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake III source port"),
			QStringLiteral("idTech3"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake III-family engines and source ports. {mod} is an fs_game directory such as baseq3."),
			{
				QStringLiteral("+set fs_basepath {basedir}"),
				QStringLiteral("+set fs_game {mod}"),
				QStringLiteral("+devmap {map}"),
			},
			true),
		launchProfile(
			QStringLiteral("doom-source-port"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Doom source port"),
			QStringLiteral("idTech1"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Doom-family source ports. {iwad} is the installation's IWAD, {bsp} the built PWAD, and {warp} the map to start on in -warp's numbers: MAP07 becomes 07 and E2M3 becomes 2 3."),
			{
				QStringLiteral("-iwad {iwad}"),
				QStringLiteral("-file {bsp}"),
				QStringLiteral("-warp {warp}"),
			},
			true),
		launchProfile(
			QStringLiteral("custom"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Custom launch"),
			QStringLiteral("unknown"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Passes only the caller's extra arguments; nothing is added by VibeStudio."),
			{},
			false),
	};
}

QStringList gameLaunchProfileIds()
{
	QStringList ids;
	for (const GameLaunchProfile& profile : gameLaunchProfiles()) {
		ids.push_back(profile.id);
	}
	return ids;
}

bool gameLaunchProfileForId(const QString& id, GameLaunchProfile* out)
{
	return gameLaunchProfileForId(id, {}, out);
}

bool gameLaunchProfileForId(const QString& id, const QVector<GameLaunchProfile>& userProfiles, GameLaunchProfile* out)
{
	const QString normalized = normalizedId(id);
	if (normalized.isEmpty()) {
		return false;
	}
	for (const GameLaunchProfile& profile : userProfiles + gameLaunchProfiles()) {
		if (normalizedId(profile.id) == normalized) {
			if (out) {
				*out = profile;
			}
			return true;
		}
	}
	return false;
}

QString defaultGameLaunchProfileId(GameEngineFamily family)
{
	switch (family) {
	case GameEngineFamily::IdTech1:
		return QStringLiteral("doom-source-port");
	case GameEngineFamily::IdTech2:
		return QStringLiteral("quake-source-port");
	case GameEngineFamily::IdTech3:
		return QStringLiteral("quake3-source-port");
	case GameEngineFamily::Unknown:
		break;
	}
	return QStringLiteral("custom");
}

// User launch profiles. Workflow reference: TrenchBroom GameEngineProfile and
// LaunchGameEngineDialog at 4c756be1 (GPL-3.0-or-later): a named engine with
// a parameter line of variables. Original Qt implementation with VibeStudio's
// own tokens; see docs/CREDITS.md.
QStringList gameLaunchTokens()
{
	return {QStringLiteral("map"), QStringLiteral("mod"), QStringLiteral("basedir"), QStringLiteral("bsp"), QStringLiteral("iwad"), QStringLiteral("warp")};
}

QStringList splitGameLaunchArguments(const QString& text)
{
	return QProcess::splitCommand(text);
}

QVector<QStringList> gameLaunchArgumentGroups(const QString& argumentLine)
{
	QVector<QStringList> groups;
	for (const QString& word : splitGameLaunchArguments(argumentLine)) {
		// A switch starts a group; a number such as -1 is a value, not a switch.
		bool number = false;
		word.toDouble(&number);
		const bool startsSwitch = !number && (word.startsWith(QLatin1Char('-')) || word.startsWith(QLatin1Char('+')));
		if (groups.isEmpty() || startsSwitch) {
			groups.push_back({word});
		} else {
			groups.last().push_back(word);
		}
	}
	return groups;
}

QString userGameLaunchProfileId(const QString& name, const QVector<GameLaunchProfile>& existing)
{
	QString slug;
	for (const QChar character : name.trimmed().toLower()) {
		if ((character >= QLatin1Char('a') && character <= QLatin1Char('z')) || (character >= QLatin1Char('0') && character <= QLatin1Char('9'))) {
			slug += character;
		} else if (!slug.isEmpty() && !slug.endsWith(QLatin1Char('-'))) {
			slug += QLatin1Char('-');
		}
	}
	while (slug.endsWith(QLatin1Char('-'))) {
		slug.chop(1);
	}
	const QString base = QStringLiteral("user-%1").arg(slug.isEmpty() ? QStringLiteral("profile") : slug.left(40));
	const auto taken = [&existing](const QString& id) {
		for (const GameLaunchProfile& profile : existing) {
			if (normalizedId(profile.id) == normalizedId(id)) {
				return true;
			}
		}
		return gameLaunchProfileIds().contains(id);
	};
	QString id = base;
	for (int copy = 2; taken(id); ++copy) {
		id = QStringLiteral("%1-%2").arg(base).arg(copy);
	}
	return id;
}

QString userGameLaunchProfileProblem(const GameLaunchProfile& profile)
{
	static const QRegularExpression idPattern(QStringLiteral("^[a-z0-9][a-z0-9-]{0,63}$"));
	if (!idPattern.match(profile.id).hasMatch()) {
		return QCoreApplication::translate("VibeStudioBuildPipeline", "A launch profile id uses lower-case letters, digits and hyphens.");
	}
	if (gameLaunchProfileIds().contains(normalizedId(profile.id))) {
		return QCoreApplication::translate("VibeStudioBuildPipeline", "\"%1\" is a built-in launch profile; choose another id.").arg(profile.id);
	}
	if (profile.displayName.trimmed().isEmpty()) {
		return QCoreApplication::translate("VibeStudioBuildPipeline", "Give the launch profile a name.");
	}
	if (profile.argumentLine.count(QLatin1Char('"')) % 2 != 0) {
		return QCoreApplication::translate("VibeStudioBuildPipeline", "The arguments have an unclosed quote.");
	}
	const QStringList known = gameLaunchTokens();
	for (const QStringList& group : gameLaunchArgumentGroups(profile.argumentLine)) {
		for (const QString& word : group) {
			for (const QString& name : tokensInWord(word)) {
				if (!known.contains(name)) {
					return QCoreApplication::translate("VibeStudioBuildPipeline", "{%1} is not a launch token. Use {map}, {mod}, {basedir}, {bsp}, {iwad} or {warp}.").arg(name);
				}
			}
		}
	}
	if (!profile.engineFamily.isEmpty() && profile.engineFamily != QStringLiteral("unknown")
		&& gameEngineFamilyFromId(profile.engineFamily) == GameEngineFamily::Unknown) {
		return QCoreApplication::translate("VibeStudioBuildPipeline", "Engine \"%1\" is not idTech1, idTech2 or idTech3.").arg(profile.engineFamily);
	}
	return {};
}

QStringList resolveGameLaunchArgumentLine(const QString& argumentLine, const QMap<QString, QString>& tokens, QStringList* droppedGroups)
{
	// The same resolution buildGameLaunchPlan applies to a user profile.
	QStringList arguments;
	for (const QStringList& group : gameLaunchArgumentGroups(argumentLine)) {
		QStringList resolved;
		if (resolveArgumentWords(group, tokens, &resolved, nullptr)) {
			arguments += resolved;
		} else if (droppedGroups) {
			droppedGroups->push_back(group.join(QLatin1Char(' ')));
		}
	}
	return arguments;
}

QJsonObject gameLaunchProfileJson(const GameLaunchProfile& profile)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), profile.id);
	object.insert(QStringLiteral("displayName"), profile.displayName);
	object.insert(QStringLiteral("engineFamily"), profile.engineFamily);
	object.insert(QStringLiteral("description"), profile.description);
	object.insert(QStringLiteral("requiresMap"), profile.requiresMap);
	object.insert(QStringLiteral("userDefined"), profile.userDefined);
	if (profile.userDefined) {
		object.insert(QStringLiteral("arguments"), profile.argumentLine);
	} else {
		object.insert(QStringLiteral("arguments"), profile.argumentTemplates.join(QLatin1Char(' ')));
	}
	return object;
}

bool gameLaunchProfileFromJson(const QJsonObject& object, GameLaunchProfile* out)
{
	GameLaunchProfile profile;
	profile.id = object.value(QStringLiteral("id")).toString().trimmed();
	profile.displayName = object.value(QStringLiteral("displayName")).toString().trimmed();
	profile.engineFamily = object.value(QStringLiteral("engineFamily")).toString().trimmed();
	profile.description = object.value(QStringLiteral("description")).toString();
	profile.requiresMap = object.value(QStringLiteral("requiresMap")).toBool(true);
	profile.userDefined = true;
	profile.argumentLine = object.value(QStringLiteral("arguments")).toString().trimmed();
	if (!userGameLaunchProfileProblem(profile).isEmpty()) {
		return false;
	}
	if (out) {
		*out = profile;
	}
	return true;
}

QString doomIwadPath(const GameInstallationProfile& installation, const QString& baseDirectory)
{
	// A base directory that is itself a file is the IWAD the caller chose.
	if (!baseDirectory.trimmed().isEmpty() && QFileInfo(baseDirectory).isFile()) {
		return absoluteCleanPath(baseDirectory);
	}
	const QDir root(baseDirectory.trimmed().isEmpty() ? installation.rootPath : baseDirectory);
	for (const QString& package : installation.basePackagePaths) {
		const QString candidate = QFileInfo(package).isAbsolute() ? package : root.filePath(package);
		if (QFileInfo(candidate).isFile()) {
			return absoluteCleanPath(candidate);
		}
	}
	// IWAD names differ in case between stores (DOOM2.WAD, doom2.wad), so the
	// folder is matched without regard to case.
	const QStringList present = root.entryList(QDir::Files);
	for (const QString& expected : gameDefinitionForKey(installation.gameKey).expectedBasePackages) {
		for (const QString& name : present) {
			if (name.compare(expected, Qt::CaseInsensitive) == 0) {
				return absoluteCleanPath(root.filePath(name));
			}
		}
	}
	return {};
}

QString doomWarpArguments(const QString& mapName)
{
	// Doom-family ports read -warp as numbers: the map for MAPxx games, the
	// episode and the map for ExMy games (the -warp switch of vanilla Doom and
	// its source ports, https://doomwiki.org/wiki/Parameters#-warp).
	static const QRegularExpression mapLump(QStringLiteral("^MAP(\\d{1,2})$"), QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression episodeLump(QStringLiteral("^E(\\d)M(\\d)$"), QRegularExpression::CaseInsensitiveOption);
	const QString name = mapName.trimmed();
	if (const QRegularExpressionMatch match = mapLump.match(name); match.hasMatch()) {
		return match.captured(1);
	}
	if (const QRegularExpressionMatch match = episodeLump.match(name); match.hasMatch()) {
		return QStringLiteral("%1 %2").arg(match.captured(1), match.captured(2));
	}
	// Already numbers, or a name the user chose on purpose.
	return name;
}

GameLaunchPlan buildGameLaunchPlan(const GameLaunchRequest& request, const GameInstallationProfile& installation,
	const std::function<bool()>& isCancelled, bool deferArtifactValidation)
{
	GameLaunchPlan plan;
	const QString requestedProfileId = request.launchProfileId.trimmed().isEmpty()
		? defaultGameLaunchProfileId(installation.engineFamily)
		: request.launchProfileId;
	plan.profileFound = gameLaunchProfileForId(requestedProfileId, request.userProfiles, &plan.profile);
	if (!plan.profileFound) {
		plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "Game launch profile \"%1\" is not known.").arg(requestedProfileId.trimmed());
		plan.commandLine = compilerCommandLineText(plan.program, plan.arguments);
		return plan;
	}

	// A profile of the user's own made for another engine still runs, as typed,
	// but its switches are probably not this engine's.
	if (plan.profile.userDefined && !plan.profile.engineFamily.isEmpty() && plan.profile.engineFamily != QLatin1String("unknown")
		&& installation.engineFamily != GameEngineFamily::Unknown && gameEngineFamilyFromId(plan.profile.engineFamily) != installation.engineFamily) {
		plan.warnings << QCoreApplication::translate("VibeStudioBuildPipeline", "Launch profile \"%1\" is for %2, but the installation is %3.")
			.arg(plan.profile.displayName, plan.profile.engineFamily, gameEngineFamilyDisplayName(installation.engineFamily));
	}

	plan.program = absoluteCleanPath(request.executablePath.trimmed().isEmpty() ? installation.executablePath : request.executablePath);
	if (plan.program.isEmpty()) {
		plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "No game executable is configured for this installation.");
	} else {
		const QFileInfo programInfo(plan.program);
		if (!programInfo.exists()) {
			plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "Game executable does not exist: %1").arg(nativePath(plan.program));
		} else if (!programInfo.isFile()) {
			plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "Game executable path is not a file: %1").arg(nativePath(plan.program));
		} else if (!programInfo.isExecutable()) {
			plan.warnings << QCoreApplication::translate("VibeStudioBuildPipeline", "Game executable is not marked executable: %1").arg(nativePath(plan.program));
		}
	}

	const QString baseDirectory = request.baseDirectory.trimmed().isEmpty() ? installation.rootPath.trimmed() : request.baseDirectory.trimmed();
	const QString mapName = request.mapName.trimmed();
	const QString artifactPath = absoluteCleanPath(request.bspPath.trimmed());
	if (plan.profile.requiresMap && mapName.isEmpty()) {
		plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "This launch profile requires a map name.");
	}

	QMap<QString, QString> tokens;
	tokens.insert(QStringLiteral("map"), mapName);
	tokens.insert(QStringLiteral("warp"), doomWarpArguments(mapName));
	tokens.insert(QStringLiteral("iwad"), doomIwadPath(installation, baseDirectory));
	tokens.insert(QStringLiteral("mod"), request.modDirectory.trimmed());
	tokens.insert(QStringLiteral("basedir"), baseDirectory);
	tokens.insert(QStringLiteral("bsp"), artifactPath);

	// A user profile's line is split into switch groups now, so a quoted word
	// keeps its spaces; a built-in template is one group of plain words.
	QVector<QStringList> groups;
	if (plan.profile.userDefined) {
		groups = gameLaunchArgumentGroups(plan.profile.argumentLine);
	} else {
		for (const QString& argumentTemplate : std::as_const(plan.profile.argumentTemplates)) {
			groups << argumentTemplate.split(' ', Qt::SkipEmptyParts);
		}
	}
	const QStringList known = gameLaunchTokens();
	for (const QStringList& group : std::as_const(groups)) {
		for (const QString& word : group) {
			for (const QString& name : tokensInWord(word)) {
				if (!known.contains(name)) {
					plan.warnings << QCoreApplication::translate("VibeStudioBuildPipeline", "Argument \"%1\" uses {%2}, which is not a launch token, so it is passed as written.")
						.arg(word, name);
				}
			}
		}
		QStringList resolved;
		QStringList missingTokens;
		if (resolveArgumentWords(group, tokens, &resolved, &missingTokens)) {
			plan.arguments += resolved;
			continue;
		}
		// No mod folder means the base game, which is a choice rather than
		// something missing, so that argument is dropped without a warning.
		if (missingTokens.isEmpty() || missingTokens == QStringList {QStringLiteral("mod")}) {
			continue;
		}
		plan.warnings << QCoreApplication::translate("VibeStudioBuildPipeline", "Argument \"%1\" was dropped because %2 is not set.")
			.arg(group.join(QLatin1Char(' ')), missingTokens.join(QStringLiteral(", ")));
	}
	plan.arguments += request.extraArguments;

	if (!baseDirectory.isEmpty() && !QFileInfo(baseDirectory).isDir()) {
		plan.warnings << QCoreApplication::translate("VibeStudioBuildPipeline", "Base directory does not exist: %1").arg(nativePath(baseDirectory));
	}
	if (!request.bspPath.trimmed().isEmpty() && !QFileInfo(request.bspPath.trimmed()).isFile()) {
		plan.warnings << QCoreApplication::translate("VibeStudioBuildPipeline", "Built map artifact does not exist yet: %1").arg(nativePath(request.bspPath.trimmed()));
	}

	plan.workingDirectory = request.workingDirectory.trimmed().isEmpty()
		? (plan.program.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(plan.program).absolutePath()))
		: absoluteCleanPath(request.workingDirectory);
	if (plan.workingDirectory.isEmpty()) {
		plan.warnings << QCoreApplication::translate("VibeStudioBuildPipeline", "No working directory could be resolved for the launch.");
	} else if (!QFileInfo(plan.workingDirectory).isDir()) {
		plan.warnings << QCoreApplication::translate("VibeStudioBuildPipeline", "Launch working directory does not exist: %1").arg(nativePath(plan.workingDirectory));
	}

	if (plan.profile.id == QStringLiteral("doom-source-port") && !request.bspPath.trimmed().isEmpty() &&
		QFileInfo(request.bspPath.trimmed()).suffix().compare("wad", Qt::CaseInsensitive) == 0) {
		if (deferArtifactValidation) { plan.artifactValidationPending = true; }
		else {
			// Preserve the native -warp shorthand while inspecting its actual WAD
			// marker. The explicit MAPxx / ExMy spelling remains unchanged.
			QString marker = mapName;
			static const QRegularExpression numeric(QStringLiteral("^[0-9]{1,2}$"));
			static const QRegularExpression episode(QStringLiteral("^([0-9])\\s+([0-9])$"));
			const auto episodeMatch = episode.match(mapName);
			if (numeric.match(mapName).hasMatch()) { marker = QStringLiteral("MAP%1").arg(mapName.toInt(), 2, 10, QLatin1Char('0')); }
			else if (episodeMatch.hasMatch()) { marker = QStringLiteral("E%1M%2").arg(episodeMatch.captured(1), episodeMatch.captured(2)); }
			const auto nodes = inspectLevelDoomWadNodes(artifactPath, marker.isEmpty() ? QStringList{} : QStringList{marker}, isCancelled);
			plan.cancelled = nodes.cancelled;
			plan.errors += nodes.errors;
			plan.warnings += nodes.warnings;
			for (auto it = nodes.maps.cbegin(); it != nodes.maps.cend(); ++it) { plan.nodeBuild.insert(it.key(), levelDoomNodeReportJson(it.value())); }
			if (nodes.errors.isEmpty() && !nodes.sourceHash.isEmpty()) {
				plan.validatedArtifactPath = artifactPath;
				plan.validatedArtifactHash = nodes.sourceHash;
			}
		}
	}
	if (isCancelled && isCancelled()) { plan.cancelled = true; }
	plan.commandLine = compilerCommandLineText(plan.program, plan.arguments);
	plan.runnable = plan.profileFound && plan.errors.isEmpty() && !plan.program.isEmpty() && !plan.artifactValidationPending && !plan.cancelled;
	return plan;
}

bool startGameLaunch(const GameLaunchPlan& plan, qint64* pidOut, QString* error, const std::function<bool()>& isCancelled)
{
	if (pidOut) {
		*pidOut = 0;
	}
	if (!plan.runnable || plan.artifactValidationPending || plan.cancelled) {
		if (error) {
			*error = plan.errors.isEmpty()
				? QCoreApplication::translate("VibeStudioBuildPipeline", "Game launch plan is not runnable.")
				: plan.errors.join(QStringLiteral("; "));
		}
		return false;
	}

	const auto cancelled = [&] {
		if (isCancelled && isCancelled()) {
			if (error) { *error = QCoreApplication::translate("VibeStudioBuildPipeline", "Game launch cancelled."); }
			return true;
		}
		return false;
	};
	if (cancelled()) { return false; }
	if (!plan.validatedArtifactPath.isEmpty()) {
		QFile file(plan.validatedArtifactPath);
		QCryptographicHash hash(QCryptographicHash::Sha256);
		bool complete = file.open(QIODevice::ReadOnly) && file.size() <= kLevelMapMaxDocumentBytes;
		qint64 read = 0;
		while (complete && !file.atEnd()) {
			if (cancelled()) { return false; }
			const auto chunk = file.read(1024 * 1024);
			read += chunk.size();
			complete = !chunk.isEmpty() && file.error() == QFileDevice::NoError && read <= kLevelMapMaxDocumentBytes;
			if (complete) { hash.addData(chunk); }
		}
		if (!complete || plan.validatedArtifactHash.isEmpty() || hash.result() != plan.validatedArtifactHash) {
			if (error) { *error = QCoreApplication::translate("VibeStudioBuildPipeline", "The validated WAD changed or is unavailable. Prepare the launch again before testing."); }
			return false;
		}
	}
	if (cancelled()) { return false; }
	qint64 pid = 0;
	const bool started = QProcess::startDetached(plan.program, plan.arguments, plan.workingDirectory, &pid);
	if (!started) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioBuildPipeline", "Failed to start the game process: %1").arg(nativePath(plan.program));
		}
		return false;
	}
	if (pidOut) {
		*pidOut = pid;
	}
	return true;
}

QString gameLaunchPlanText(const GameLaunchPlan& plan)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Game launch plan");
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Profile: %1").arg(plan.profileFound ? plan.profile.id : QCoreApplication::translate("VibeStudioBuildPipeline", "(unknown)"));
	if (plan.profileFound) {
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Name: %1").arg(plan.profile.displayName);
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Engine: %1").arg(plan.profile.engineFamily);
	}
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "State: %1").arg(operationStateId(plan.state()));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Runnable: %1").arg(plan.runnable ? QCoreApplication::translate("VibeStudioBuildPipeline", "yes") : QCoreApplication::translate("VibeStudioBuildPipeline", "no"));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Program: %1").arg(nativePath(plan.program));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Working directory: %1").arg(nativePath(plan.workingDirectory));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Command line: %1").arg(plan.commandLine);
	if (plan.artifactValidationPending) { lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Node validation is required before launch."); }
	if (!plan.validatedArtifactHash.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Validated WAD SHA-256: %1").arg(QString::fromLatin1(plan.validatedArtifactHash.toHex()));
	}
	if (!plan.warnings.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Warnings");
		for (const QString& warning : plan.warnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	if (!plan.errors.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Errors");
		for (const QString& error : plan.errors) {
			lines << QStringLiteral("- %1").arg(error);
		}
	}
	return lines.join('\n');
}

QJsonObject gameLaunchPlanJson(const GameLaunchPlan& plan)
{
	QJsonObject object;
	object.insert(QStringLiteral("profileId"), plan.profile.id);
	object.insert(QStringLiteral("displayName"), plan.profile.displayName);
	object.insert(QStringLiteral("engineFamily"), plan.profile.engineFamily);
	object.insert(QStringLiteral("profileFound"), plan.profileFound);
	object.insert(QStringLiteral("runnable"), plan.runnable);
	object.insert(QStringLiteral("artifactValidationPending"), plan.artifactValidationPending);
	object.insert(QStringLiteral("cancelled"), plan.cancelled);
	object.insert(QStringLiteral("validatedArtifactPath"), plan.validatedArtifactPath);
	object.insert(QStringLiteral("validatedArtifactSha256"), QString::fromLatin1(plan.validatedArtifactHash.toHex()));
	object.insert(QStringLiteral("nodeBuild"), plan.nodeBuild);
	object.insert(QStringLiteral("state"), operationStateId(plan.state()));
	object.insert(QStringLiteral("program"), plan.program);
	object.insert(QStringLiteral("arguments"), stringArrayJson(plan.arguments));
	object.insert(QStringLiteral("commandLine"), plan.commandLine);
	object.insert(QStringLiteral("workingDirectory"), plan.workingDirectory);
	object.insert(QStringLiteral("warnings"), stringArrayJson(plan.warnings));
	object.insert(QStringLiteral("errors"), stringArrayJson(plan.errors));
	return object;
}

namespace {

// Size first, then a streamed SHA-256, so an unchanged multi-megabyte BSP is
// recognised without holding two copies in memory.
bool sameFileBytes(const QString& left, const QString& right)
{
	const QFileInfo leftInfo(left);
	const QFileInfo rightInfo(right);
	if (!leftInfo.isFile() || !rightInfo.isFile() || leftInfo.size() != rightInfo.size()) {
		return false;
	}
	auto digest = [](const QString& path) {
		QFile file(path);
		QCryptographicHash hash(QCryptographicHash::Sha256);
		if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) {
			return QByteArray();
		}
		return hash.result();
	};
	const QByteArray leftDigest = digest(left);
	return !leftDigest.isEmpty() && leftDigest == digest(right);
}

} // namespace

bool GameMapDeployPlan::upToDate() const
{
	if (!required || files.isEmpty()) {
		return false;
	}
	for (const GameMapDeployFile& file : files) {
		if (!file.upToDate) {
			return false;
		}
	}
	return true;
}

bool GameMapDeployPlan::runnable() const
{
	return required && allowed && errors.isEmpty() && !files.isEmpty();
}

GameMapDeployPlan planGameMapDeploy(const GameInstallationProfile& installation, const QString& modDirectory, const QString& builtMapPath)
{
	GameMapDeployPlan plan;
	// Doom-family ports read the built PWAD in place with -file.
	plan.required = installation.engineFamily == GameEngineFamily::IdTech2 || installation.engineFamily == GameEngineFamily::IdTech3;
	if (!plan.required) {
		return plan;
	}
	plan.allowed = !installation.readOnly;
	if (!plan.allowed) {
		plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "The installation profile is read-only, so the built map cannot be copied into it. Allow test maps for this installation first.");
	}

	const QString builtMap = absoluteCleanPath(builtMapPath);
	if (builtMap.isEmpty()) {
		plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "No built map was given to copy.");
	} else if (!QFileInfo(builtMap).isFile()) {
		plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "The built map does not exist yet: %1").arg(nativePath(builtMap));
	}

	// A single folder name keeps every write inside the installation root.
	plan.gameDirectory = modDirectory.trimmed().isEmpty() ? defaultGameDirectory(installation) : modDirectory.trimmed();
	static const QRegularExpression folderName(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.-]*$"));
	const bool folderValid = folderName.match(plan.gameDirectory).hasMatch() && !plan.gameDirectory.contains(QStringLiteral(".."));
	if (plan.gameDirectory.isEmpty()) {
		plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "Name the game folder the map goes into, for example id1.");
	} else if (!folderValid) {
		plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "The game folder must be a single folder name inside the installation: %1").arg(plan.gameDirectory);
	}
	const QString root = absoluteCleanPath(installation.rootPath);
	const bool rootValid = !root.isEmpty() && QFileInfo(root).isDir();
	if (!rootValid) {
		plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "The installation folder does not exist: %1").arg(nativePath(root));
	}
	// Without a map, a root, and a valid folder there is no destination to show.
	if (builtMap.isEmpty() || !rootValid || !folderValid) {
		return plan;
	}

	plan.targetDirectory = QDir::cleanPath(QDir(root).filePath(plan.gameDirectory + QStringLiteral("/maps")));
	if (!QFileInfo(QDir(root).filePath(plan.gameDirectory)).isDir()) {
		plan.warnings << QCoreApplication::translate("VibeStudioBuildPipeline", "The game folder %1 does not exist yet and will be created.").arg(plan.gameDirectory);
	}
	// VibeMap2 writes coloured light to .lit and deluxemaps to .lux beside
	// the BSP; engines look for them beside the copied map.
	const QFileInfo mapInfo(builtMap);
	QStringList sources {builtMap};
	for (const QString& companion : {QStringLiteral("lit"), QStringLiteral("lux")}) {
		const QString path = QDir(mapInfo.absolutePath()).filePath(QStringLiteral("%1.%2").arg(mapInfo.completeBaseName(), companion));
		if (QFileInfo(path).isFile()) {
			sources << QDir::cleanPath(path);
		}
	}
	for (const QString& source : sources) {
		GameMapDeployFile file;
		file.sourcePath = source;
		file.destinationPath = QDir(plan.targetDirectory).filePath(QFileInfo(source).fileName());
		if (QFileInfo(file.destinationPath).exists()) {
			file.upToDate = sameFileBytes(source, file.destinationPath);
			file.replacesExisting = !file.upToDate;
		}
		plan.files.push_back(file);
	}
	return plan;
}

bool deployGameMap(const GameMapDeployPlan& plan, QStringList* written, QString* error)
{
	auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!plan.runnable()) {
		return fail(plan.errors.isEmpty() ? QCoreApplication::translate("VibeStudioBuildPipeline", "The map copy plan is not runnable.") : plan.errors.join(QStringLiteral(" ")));
	}
	if (!QDir().mkpath(plan.targetDirectory)) {
		return fail(QCoreApplication::translate("VibeStudioBuildPipeline", "Unable to create %1.").arg(nativePath(plan.targetDirectory)));
	}
	for (const GameMapDeployFile& file : plan.files) {
		if (file.upToDate) {
			continue;
		}
		QFile source(file.sourcePath);
		if (!source.open(QIODevice::ReadOnly)) {
			return fail(QCoreApplication::translate("VibeStudioBuildPipeline", "Unable to read %1: %2").arg(nativePath(file.sourcePath), source.errorString()));
		}
		QSaveFile destination(file.destinationPath);
		if (!destination.open(QIODevice::WriteOnly)) {
			return fail(QCoreApplication::translate("VibeStudioBuildPipeline", "Unable to write %1: %2").arg(nativePath(file.destinationPath), destination.errorString()));
		}
		while (!source.atEnd()) {
			const QByteArray chunk = source.read(1 << 20);
			if (chunk.isEmpty() && source.error() != QFileDevice::NoError) {
				destination.cancelWriting();
				return fail(QCoreApplication::translate("VibeStudioBuildPipeline", "Unable to read %1: %2").arg(nativePath(file.sourcePath), source.errorString()));
			}
			if (destination.write(chunk) != chunk.size()) {
				destination.cancelWriting();
				return fail(QCoreApplication::translate("VibeStudioBuildPipeline", "Unable to write %1: %2").arg(nativePath(file.destinationPath), destination.errorString()));
			}
		}
		if (!destination.commit()) {
			return fail(QCoreApplication::translate("VibeStudioBuildPipeline", "Unable to write %1: %2").arg(nativePath(file.destinationPath), destination.errorString()));
		}
		if (written) {
			written->push_back(file.destinationPath);
		}
	}
	return true;
}

QString gameMapDeployPlanText(const GameMapDeployPlan& plan)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Test map copy");
	if (!plan.required) {
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Not needed: the game loads the built file directly.");
		return lines.join('\n');
	}
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Game folder: %1").arg(plan.gameDirectory.isEmpty() ? QCoreApplication::translate("VibeStudioBuildPipeline", "(not resolved)") : plan.gameDirectory);
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Target: %1").arg(nativePath(plan.targetDirectory));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Allowed: %1").arg(plan.allowed ? QCoreApplication::translate("VibeStudioBuildPipeline", "yes") : QCoreApplication::translate("VibeStudioBuildPipeline", "no, the installation is read-only"));
	for (const GameMapDeployFile& file : plan.files) {
		const QString status = file.upToDate ? QCoreApplication::translate("VibeStudioBuildPipeline", "already up to date")
			: (file.replacesExisting ? QCoreApplication::translate("VibeStudioBuildPipeline", "replaces the existing file") : QCoreApplication::translate("VibeStudioBuildPipeline", "new"));
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "- %1 -> %2 (%3)").arg(QFileInfo(file.sourcePath).fileName(), nativePath(file.destinationPath), status);
	}
	if (!plan.warnings.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Warnings");
		for (const QString& warning : plan.warnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	if (!plan.errors.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Errors");
		for (const QString& message : plan.errors) {
			lines << QStringLiteral("- %1").arg(message);
		}
	}
	return lines.join('\n');
}

QJsonObject gameMapDeployPlanJson(const GameMapDeployPlan& plan)
{
	QJsonObject object;
	object.insert(QStringLiteral("required"), plan.required);
	object.insert(QStringLiteral("allowed"), plan.allowed);
	object.insert(QStringLiteral("runnable"), plan.runnable());
	object.insert(QStringLiteral("upToDate"), plan.upToDate());
	object.insert(QStringLiteral("gameDirectory"), plan.gameDirectory);
	object.insert(QStringLiteral("targetDirectory"), plan.targetDirectory);
	QJsonArray files;
	for (const GameMapDeployFile& file : plan.files) {
		QJsonObject entry;
		entry.insert(QStringLiteral("source"), file.sourcePath);
		entry.insert(QStringLiteral("destination"), file.destinationPath);
		entry.insert(QStringLiteral("replacesExisting"), file.replacesExisting);
		entry.insert(QStringLiteral("upToDate"), file.upToDate);
		files.push_back(entry);
	}
	object.insert(QStringLiteral("files"), files);
	object.insert(QStringLiteral("warnings"), stringArrayJson(plan.warnings));
	object.insert(QStringLiteral("errors"), stringArrayJson(plan.errors));
	return object;
}

} // namespace vibestudio
