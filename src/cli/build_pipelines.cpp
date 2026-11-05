#include "cli/build_pipelines.h"

#include "core/build_pipeline.h"
#include "core/compiler_profiles.h"
#include "core/project_manifest.h"

#include <QCoreApplication>
#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QSet>

namespace vibestudio::cli {
namespace {
struct Text {
	Q_DECLARE_TR_FUNCTIONS(BuildPipelinesCli)
};

// "ID=VALUE" as a pair; false without a name before the '='.
bool splitAssignment(const QString& text, QString* name, QString* value)
{
	const qsizetype equals = text.indexOf(QLatin1Char('='));
	if (equals <= 0) {
		return false;
	}
	*name = text.left(equals).trimmed();
	*value = text.mid(equals + 1);
	return !name->isEmpty();
}

BuildPipelineStage* stageWithId(BuildPipelineDescriptor* pipeline, const QString& id)
{
	for (BuildPipelineStage& stage : pipeline->stages) {
		if (stage.id == id) {
			return &stage;
		}
	}
	return nullptr;
}

} // namespace

BuildPipelinesCliResult runBuildPipelines(const QStringList& arguments)
{
	BuildPipelinesCliResult result;
	const auto fail = [&](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QSet<QString> flags {"--cli", "--json", "--quiet", "--verbose", "--dry-run"};
	const QSet<QString> single {"--workspace-root", "--name", "--description", "--from", "--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> repeated {"--stage", "--stage-args", "--disable-stage", "--enable-stage", "--map-input", "--variable"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QVector<QPair<QString, QString>> ordered;
	QStringList positional;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const QString arg = arguments[i];
		if (!arg.startsWith(QStringLiteral("--"))) {
			positional << arg;
			continue;
		}
		const auto equal = arg.indexOf(QLatin1Char('='));
		const QString name = equal < 0 ? arg : arg.left(equal);
		if (flags.contains(name) && equal < 0) {
			seen.insert(name);
			continue;
		}
		if (!single.contains(name) && !repeated.contains(name)) {
			return fail(2, Text::tr("Unexpected option: %1.").arg(arg));
		}
		if (single.contains(name) && seen.contains(name)) {
			return fail(2, Text::tr("Repeated option: %1.").arg(name));
		}
		seen.insert(name);
		QString value;
		if (equal >= 0) {
			value = arg.mid(equal + 1);
		} else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith(QStringLiteral("--"))) {
			value = arguments[++i];
		}
		// A description and a stage's arguments may be empty; nothing else.
		if (value.trimmed().isEmpty() && name != QLatin1String("--description")) {
			return fail(2, Text::tr("Missing value for %1.").arg(name));
		}
		if (single.contains(name)) {
			values.insert(name, value);
		} else {
			ordered.push_back({name, value});
		}
	}
	const QString action = positional.value(1);
	const QString id = positional.value(2).trimmed();
	const bool dryRun = seen.contains(QStringLiteral("--dry-run"));
	if (id.isEmpty()) {
		return fail(2, Text::tr("build %1 needs the pipeline id and --workspace-root, the project folder.").arg(action));
	}
	const QString project = values.value(QStringLiteral("--workspace-root"));
	if (project.trimmed().isEmpty()) {
		return fail(2, Text::tr("build %1 needs --workspace-root, the project folder whose manifest keeps the pipelines.").arg(action));
	}
	ProjectManifest manifest;
	QString error;
	if (!loadProjectManifest(project, &manifest, &error)) {
		return fail(3, Text::tr("No project manifest in %1: %2").arg(QDir::toNativeSeparators(project), error));
	}
	QVector<BuildPipelineDescriptor> pipelines = projectBuildPipelines(manifest, nullptr, true);
	qsizetype existing = -1;
	for (qsizetype index = 0; index < pipelines.size(); ++index) {
		existing = pipelines.at(index).id == id ? index : existing;
	}

	if (action == QLatin1String("pipeline-remove")) {
		bool changes = !ordered.isEmpty();
		for (const QString& option : {QStringLiteral("--name"), QStringLiteral("--description"), QStringLiteral("--from")}) {
			changes = changes || values.contains(option);
		}
		if (changes) {
			return fail(2, Text::tr("build pipeline-remove takes only the pipeline id, --workspace-root and --dry-run."));
		}
		if (existing < 0) {
			return fail(3, buildPipelineForId(id) ? Text::tr("%1 is a built-in pipeline; only the project's own can be removed.").arg(id)
												  : Text::tr("The project has no pipeline %1.").arg(id));
		}
		const BuildPipelineDescriptor removed = pipelines.takeAt(existing);
		if (!dryRun) {
			setProjectBuildPipelines(&manifest, pipelines);
			if (!saveProjectManifest(manifest, &error)) {
				return fail(1, Text::tr("The project manifest could not be saved: %1").arg(error));
			}
		}
		result.payload.insert(QStringLiteral("removed"), projectBuildPipelineJson(removed));
		result.payload.insert(QStringLiteral("dryRun"), dryRun);
		result.payload.insert(QStringLiteral("saved"), !dryRun);
		result.payload.insert(QStringLiteral("pipelineCount"), static_cast<int>(pipelines.size()));
		result.lines << (dryRun ? Text::tr("Would remove pipeline %1 (dry run).").arg(id) : Text::tr("Removed pipeline %1.").arg(id));
		return result;
	}

	// pipeline-set: the existing pipeline, else a copy of --from, else empty.
	BuildPipelineDescriptor pipeline;
	const bool replacing = existing >= 0;
	if (replacing) {
		pipeline = pipelines.at(existing);
	} else if (values.contains(QStringLiteral("--from"))) {
		BuildPipelineDescriptor source;
		if (!buildPipelineForId(values.value(QStringLiteral("--from")), pipelines, &source)) {
			return fail(3, Text::tr("There is no pipeline %1 to start from.").arg(values.value(QStringLiteral("--from"))));
		}
		pipeline = projectBuildPipelineFrom(source, values.value(QStringLiteral("--name"), source.displayName), pipelines);
	} else if (!values.contains(QStringLiteral("--name"))) {
		return fail(2, Text::tr("A new pipeline needs --name, and --from or --stage."));
	}
	pipeline.id = id;
	pipeline.projectDefined = true;
	if (values.contains(QStringLiteral("--name"))) {
		pipeline.displayName = values.value(QStringLiteral("--name")).trimmed();
	}
	if (values.contains(QStringLiteral("--description"))) {
		pipeline.description = values.value(QStringLiteral("--description")).trimmed();
	}
	// --stage, in order, replaces the stages.
	QVector<BuildPipelineStage> stages;
	for (const auto& [option, value] : std::as_const(ordered)) {
		if (option != QLatin1String("--stage")) {
			continue;
		}
		QString stageId;
		QString profileId;
		if (!splitAssignment(value, &stageId, &profileId)) {
			return fail(2, Text::tr("--stage takes ID=PROFILE, such as bsp=vibemap2-bsp: %1").arg(value));
		}
		BuildPipelineStage stage;
		stage.id = stageId;
		stage.profileId = profileId.trimmed();
		stage.inputFromPrevious = !stages.isEmpty();
		stages.push_back(stage);
	}
	if (!stages.isEmpty()) {
		pipeline.stages = stages;
	}
	for (const auto& [option, value] : std::as_const(ordered)) {
		if (option == QLatin1String("--stage")) {
			continue;
		}
		if (option == QLatin1String("--variable")) {
			QString name;
			QString text;
			if (!splitAssignment(value, &name, &text)) {
				return fail(2, Text::tr("--variable takes NAME=VALUE: %1").arg(value));
			}
			pipeline.variables.insert(name, text);
			continue;
		}
		QString stageId = value.trimmed();
		QString argumentLine;
		if (option == QLatin1String("--stage-args") && !splitAssignment(value, &stageId, &argumentLine)) {
			return fail(2, Text::tr("--stage-args takes ID=ARGUMENTS: %1").arg(value));
		}
		BuildPipelineStage* stage = stageWithId(&pipeline, stageId);
		if (!stage) {
			return fail(4, Text::tr("%1 names stage %2, which the pipeline does not have.").arg(option, stageId));
		}
		if (option == QLatin1String("--stage-args")) {
			stage->argumentLine = argumentLine.trimmed();
		} else if (option == QLatin1String("--disable-stage")) {
			stage->enabledByDefault = false;
		} else if (option == QLatin1String("--enable-stage")) {
			stage->enabledByDefault = true;
		} else if (option == QLatin1String("--map-input")) {
			stage->inputFromPrevious = false;
		}
	}
	normalizeProjectBuildPipeline(&pipeline);
	if (replacing) {
		pipelines[existing] = pipeline;
	} else {
		pipelines.push_back(pipeline);
	}
	const QStringList problems = projectBuildPipelineProblems(pipeline, pipelines);
	if (!problems.isEmpty()) {
		result.payload.insert(QStringLiteral("problems"), QJsonArray::fromStringList(problems));
		return fail(4, Text::tr("Pipeline %1 cannot be saved: %2").arg(id, problems.join(QLatin1Char(' '))));
	}
	if (!dryRun) {
		setProjectBuildPipelines(&manifest, pipelines);
		if (!saveProjectManifest(manifest, &error)) {
			return fail(1, Text::tr("The project manifest could not be saved: %1").arg(error));
		}
	}
	result.payload.insert(QStringLiteral("pipeline"), buildPipelineDescriptorJson(pipeline));
	result.payload.insert(QStringLiteral("stored"), projectBuildPipelineJson(pipeline));
	result.payload.insert(QStringLiteral("replaced"), replacing);
	result.payload.insert(QStringLiteral("dryRun"), dryRun);
	result.payload.insert(QStringLiteral("saved"), !dryRun);
	result.lines << (dryRun ? Text::tr("Would save pipeline %1, %2 (dry run).").arg(id, pipeline.displayName)
							: Text::tr("Saved pipeline %1, %2.").arg(id, pipeline.displayName));
	for (const BuildPipelineStage& stage : std::as_const(pipeline.stages)) {
		result.lines << Text::tr("  %1 -> %2%3 %4").arg(stage.id, stage.profileId, stage.enabledByDefault ? QString() : Text::tr(" (off)"), stage.argumentLine);
	}
	return result;
}

} // namespace vibestudio::cli
