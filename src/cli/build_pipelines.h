#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace vibestudio::cli {

struct BuildPipelinesCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};

// The project's own build pipelines, kept in its manifest (core/build_pipeline.h),
// as the Build page's Build Pipelines… window edits them:
// - build pipeline-set <id> --workspace-root DIR [--name NAME]
//   [--description TEXT] [--from PIPELINE] [--stage ID=PROFILE]...
//   [--stage-args ID=ARGS]... [--disable-stage ID]... [--enable-stage ID]...
//   [--map-input ID]... [--variable NAME=VALUE]... adds or changes one. A new
//   pipeline starts from --from (built in or the project's) or is empty;
//   --stage, given in order, replaces the stage list, and each later stage
//   reads the previous one's output unless --map-input names it.
// - build pipeline-remove <id> --workspace-root DIR removes one.
// Both support --dry-run, which reports without saving. Exit codes: 2 usage,
// 3 no project or pipeline, 4 a pipeline with problems.
BuildPipelinesCliResult runBuildPipelines(const QStringList& arguments);

} // namespace vibestudio::cli
