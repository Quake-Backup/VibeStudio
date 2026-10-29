// The project's own build pipelines and the program trust check, below the
// GUI: pipeline ids, problems, JSON and the project manifest; planning with
// argument lines, variables and stages turned off; a run through a fake
// compiler; which programs are known, trusted or need asking, kept per user
// and per project with the program's hash; and the `build pipeline-set`,
// `build pipeline-remove`, `build list`, `build plan` and `build run` routes
// with --workspace-root, --variable and --trust-programs.
#include "core/build_pipeline.h"
#include "core/build_program_trust.h"
#include "core/compiler_runner.h"
#include "core/project_manifest.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
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

// A stand-in for vibemap2-bsp: a minimal Quake BSP beside the .map it is given.
int fakeCompiler(const QStringList& arguments)
{
	QString output;
	for (const QString& argument : arguments) {
		if (argument.endsWith(QStringLiteral(".map"), Qt::CaseInsensitive)) {
			const QFileInfo map(argument);
			output = map.absolutePath() + QLatin1Char('/') + map.completeBaseName() + QStringLiteral(".bsp");
		}
	}
	QByteArray bytes(4 + 15 * 8, '\0');
	qToLittleEndian<qint32>(29, reinterpret_cast<uchar*>(bytes.data()));
	std::cout << "fake compiler " << qPrintable(arguments.join(QLatin1Char(' '))) << std::endl;
	return !output.isEmpty() && writeFile(output, bytes) ? 0 : 1;
}

QByteArray mapText()
{
	return QByteArrayLiteral("{\n\"classname\" \"worldspawn\"\n{\n( 0 0 0 ) ( 0 1 0 ) ( 1 0 0 ) base/floor 0 0 0 1 1\n"
							 "( 0 0 64 ) ( 1 0 64 ) ( 0 1 64 ) base/floor 0 0 0 1 1\n( 0 0 0 ) ( 1 0 0 ) ( 0 0 1 ) base/floor 0 0 0 1 1\n"
							 "( 0 64 0 ) ( 0 64 1 ) ( 1 64 0 ) base/floor 0 0 0 1 1\n( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) base/floor 0 0 0 1 1\n"
							 "( 64 0 0 ) ( 64 1 0 ) ( 64 0 1 ) base/floor 0 0 0 1 1\n}\n}\n{\n\"classname\" \"info_player_start\"\n\"origin\" \"32 32 24\"\n}\n");
}

QStringList jsonStrings(const QJsonValue& value)
{
	QStringList strings;
	for (const QJsonValue& item : value.toArray()) {
		strings << item.toString();
	}
	return strings;
}

// A project pipeline: BSP with typed arguments, VIS off, LIGHT with a
// variable of its own.
BuildPipelineDescriptor fastLight()
{
	BuildPipelineDescriptor source;
	buildPipelineForId(QStringLiteral("quake-full"), &source);
	BuildPipelineDescriptor pipeline = projectBuildPipelineFrom(source, QStringLiteral("Fast light"), {});
	pipeline.variables.insert(QStringLiteral("QUALITY"), QStringLiteral("high ${MAP_NAME}"));
	pipeline.stages[0].argumentLine = QStringLiteral("--fake-compiler -threads ${CPU_COUNT} -name ${MAP_NAME} -mod ${MOD} -note \"two words\"");
	pipeline.stages[1].enabledByDefault = false;
	pipeline.stages[2].argumentLine = QStringLiteral("-extra ${QUALITY}");
	normalizeProjectBuildPipeline(&pipeline);
	return pipeline;
}

void pipelineTests(const QString& folder)
{
	expect(buildPipelineVariableNames().contains(QStringLiteral("MAP_NAME")) && buildPipelineVariableNames().size() == 8, "built-in variables");
	expect(projectBuildPipelineId(QStringLiteral("Fast Light!"), {}) == QStringLiteral("fast-light"), "id from a name");
	expect(projectBuildPipelineId(QStringLiteral("Quake full"), {}) == QStringLiteral("quake-full-2"), "a built-in id is not taken");
	expect(projectBuildPipelineId(QStringLiteral("***"), {}) == QStringLiteral("pipeline"), "a name without letters");

	const BuildPipelineDescriptor pipeline = fastLight();
	expect(pipeline.id == QStringLiteral("fast-light") && pipeline.projectDefined && pipeline.engineFamily == QStringLiteral("idTech2")
			&& pipeline.outputExtension == QStringLiteral("bsp") && pipeline.stages.size() == 3,
		"a pipeline from quake-full", pipeline.id);
	expect(!pipeline.stages[0].inputFromPrevious && pipeline.stages[1].inputFromPrevious && pipeline.stages[2].inputFromStageId == QStringLiteral("vis")
			&& pipeline.stages[2].optional,
		"stages read the previous output; every stage can be turned off");
	expect(projectBuildPipelineProblems(pipeline, {pipeline}).isEmpty(), "a good pipeline", projectBuildPipelineProblems(pipeline, {pipeline}).join('\n'));
	expect(projectBuildPipelineStageId(QStringLiteral("vibemap2-light"), pipeline) == QStringLiteral("light-2"), "stage ids stay unique",
		projectBuildPipelineStageId(QStringLiteral("vibemap2-light"), pipeline));

	const auto problemOf = [](BuildPipelineDescriptor broken, const QVector<BuildPipelineDescriptor>& all = {}) {
		normalizeProjectBuildPipeline(&broken);
		return projectBuildPipelineProblems(broken, all.isEmpty() ? QVector<BuildPipelineDescriptor> {broken} : all).join(QLatin1Char('\n'));
	};
	BuildPipelineDescriptor broken = pipeline;
	broken.id = QStringLiteral("quake-full");
	expect(problemOf(broken).contains(QStringLiteral("built-in")), "a built-in id is refused");
	broken = pipeline;
	broken.id = QStringLiteral("Fast Light");
	expect(!problemOf(broken).isEmpty(), "an id with capitals and spaces is refused");
	broken = pipeline;
	broken.displayName.clear();
	expect(!problemOf(broken).isEmpty(), "a pipeline needs a name");
	expect(problemOf(pipeline, {pipeline, pipeline}).contains(QStringLiteral("fast-light")), "two pipelines with one id");
	broken = pipeline;
	broken.id = QStringLiteral("other");
	expect(problemOf(broken, {pipeline, broken}).contains(QStringLiteral("Fast light")), "two pipelines with one name");
	broken = pipeline;
	broken.stages.clear();
	expect(!problemOf(broken).isEmpty(), "a pipeline needs a stage");
	broken = pipeline;
	broken.stages[2].profileId = QStringLiteral("nope");
	expect(problemOf(broken).contains(QStringLiteral("nope")), "an unknown compiler is named");
	broken = pipeline;
	broken.stages[2].profileId = QStringLiteral("vibemap3-light");
	expect(problemOf(broken).contains(QStringLiteral("idTech3")), "one game's compilers per pipeline");
	broken = pipeline;
	broken.stages[0].argumentLine = QStringLiteral("-note \"open");
	expect(problemOf(broken).contains(QStringLiteral("quote")), "an unclosed quote");
	broken = pipeline;
	broken.stages[2].argumentLine = QStringLiteral("-extra ${NOPE}");
	expect(problemOf(broken).contains(QStringLiteral("${NOPE}")), "an unknown variable is named");
	broken = pipeline;
	broken.variables.insert(QStringLiteral("lower"), QStringLiteral("x"));
	expect(!problemOf(broken).isEmpty(), "a variable name in capitals");
	broken = pipeline;
	broken.variables.insert(QStringLiteral("MAP"), QStringLiteral("x"));
	expect(problemOf(broken).contains(QStringLiteral("${MAP}")), "a built-in variable cannot be redefined");
	broken = pipeline;
	broken.stages[1].id = QStringLiteral("qbsp");
	expect(!problemOf(broken).isEmpty(), "two stages with one id");

	BuildPipelineDescriptor loaded;
	expect(projectBuildPipelineFromJson(projectBuildPipelineJson(pipeline), &loaded) && loaded.id == pipeline.id && loaded.displayName == pipeline.displayName
			&& loaded.variables == pipeline.variables && loaded.stages.size() == 3 && loaded.stages[0].argumentLine == pipeline.stages[0].argumentLine
			&& !loaded.stages[1].enabledByDefault && loaded.stages[2].inputFromPrevious && loaded.projectDefined,
		"JSON round trip");

	// The manifest keeps them; one with problems is left out with a warning.
	const QString project = QDir(folder).filePath(QStringLiteral("project"));
	QDir().mkpath(project);
	ProjectManifest manifest = defaultProjectManifest(project, QStringLiteral("Pipelines"));
	QString error;
	expect(saveProjectManifest(manifest, &error), "an empty manifest", error);
	{
		QFile file(projectManifestPath(project));
		expect(file.open(QIODevice::ReadOnly) && !QJsonDocument::fromJson(file.readAll()).object().contains(QStringLiteral("buildPipelines")), "no pipelines, no key");
	}
	BuildPipelineDescriptor invalid = pipeline;
	invalid.id = QStringLiteral("bad");
	invalid.displayName = QStringLiteral("Bad");
	invalid.stages[0].argumentLine = QStringLiteral("${NOPE}");
	setProjectBuildPipelines(&manifest, {pipeline, invalid});
	expect(saveProjectManifest(manifest, &error), "pipelines saved", error);
	ProjectManifest reloaded;
	expect(loadProjectManifest(project, &reloaded, &error) && reloaded.schemaVersion == ProjectManifest::kSchemaVersion, "manifest reloads", error);
	QStringList warnings;
	const QVector<BuildPipelineDescriptor> usable = projectBuildPipelines(reloaded, &warnings);
	expect(usable.size() == 1 && usable[0].id == pipeline.id && usable[0].stages[2].argumentLine == pipeline.stages[2].argumentLine, "the good pipeline is read",
		QString::number(usable.size()));
	expect(warnings.size() == 1 && warnings[0].contains(QStringLiteral("Bad")), "the bad one is named", warnings.join('\n'));
	expect(projectBuildPipelines(reloaded, nullptr, true).size() == 2, "both are kept for editing");
	reloaded.extraFields.insert(QStringLiteral("futureKey"), 7);
	expect(saveProjectManifest(reloaded, &error) && loadProjectManifest(project, &reloaded) && reloaded.buildPipelines.size() == 2
			&& reloaded.extraFields.value(QStringLiteral("futureKey")).toInt() == 7,
		"saving keeps the pipelines and unknown keys");
	BuildPipelineDescriptor found;
	expect(buildPipelineForId(QStringLiteral("fast-light"), usable, &found) && found.projectDefined && buildPipelineForId(QStringLiteral("quake-full"), usable, &found)
			&& !found.projectDefined && !buildPipelineForId(QStringLiteral("fast-light")),
		"project pipelines are found first, built-in ones still");
}

void planTests(const QString& folder)
{
	const QString project = QDir(folder).filePath(QStringLiteral("plan"));
	const QString map = QDir(project).filePath(QStringLiteral("maps/start.map"));
	expect(writeFile(map, mapText()), "map written");
	BuildPipelineRequest request;
	request.pipelineId = QStringLiteral("fast-light");
	request.projectPipelines = {fastLight()};
	request.inputPath = map;
	request.workspaceRootPath = project;
	request.dryRun = true;
	for (const char* tool : {"vibemap2-bsp", "vibemap2-vis", "vibemap2-light"}) {
		request.executableOverrides.push_back({QString::fromLatin1(tool), QCoreApplication::applicationFilePath()});
	}
	BuildPipelineResult plan = planBuildPipeline(request);
	const QStringList bsp = plan.stages.value(0).plan.arguments;
	expect(plan.pipeline.projectDefined && plan.stages.size() == 3, "a project pipeline plans", buildPipelineResultText(plan));
	expect(bsp.contains(QStringLiteral("-threads")) && bsp.contains(QStringLiteral("-name")) && bsp.contains(QStringLiteral("start"))
			&& bsp.contains(QStringLiteral("two words")) && !bsp.contains(QStringLiteral("-mod")),
		"arguments split, variables filled, an empty ${MOD} drops its switch", bsp.join(QStringLiteral(" | ")));
	expect(plan.warnings.join(' ').contains(QStringLiteral("${MOD}")), "the dropped switch is named", plan.warnings.join('\n'));
	expect(plan.stages.value(1).skipped && plan.stages.value(1).skipReason.contains(QStringLiteral("turned off")), "a stage turned off is skipped",
		plan.stages.value(1).skipReason);
	const QStringList light = plan.stages.value(2).plan.arguments;
	expect(light.contains(QStringLiteral("-extra")) && light.contains(QStringLiteral("high start")), "a variable of the pipeline's own, using a built-in one",
		light.join(QStringLiteral(" | ")));
	expect(QFileInfo(plan.stages.value(2).inputPath) == QFileInfo(plan.stages.value(0).outputPath), "light reads the BSP stage's output");
	expect(buildPipelineResultJson(plan).value(QStringLiteral("source")).toString() == QStringLiteral("project"), "JSON names the source");

	request.variables.insert(QStringLiteral("MOD"), QStringLiteral("mymod"));
	request.variables.insert(QStringLiteral("QUALITY"), QStringLiteral("draft"));
	plan = planBuildPipeline(request);
	expect(plan.stages.value(0).plan.arguments.join(' ').contains(QStringLiteral("-mod mymod")) && plan.stages.value(2).plan.arguments.contains(QStringLiteral("draft")),
		"request variables fill ${MOD} and win over the pipeline's", plan.stages.value(2).plan.arguments.join(' '));

	// An unknown variable stops a run before anything starts.
	BuildPipelineDescriptor broken = fastLight();
	broken.stages[0].argumentLine = QStringLiteral("${NOPE}");
	request.projectPipelines = {broken};
	request.dryRun = false;
	const BuildPipelineResult refused = runBuildPipeline(request);
	expect(refused.state == OperationState::Failed && !refused.stages.value(0).run.started && refused.errors.join(' ').contains(QStringLiteral("${NOPE}")),
		"an unknown variable runs nothing", buildPipelineResultText(refused));

	// A real run of the BSP stage alone, through the fake compiler.
	BuildPipelineDescriptor bspOnly = fastLight();
	bspOnly.stages[2].enabledByDefault = false;
	request.projectPipelines = {bspOnly};
	request.stageTimeoutMs = 60000;
	const BuildPipelineResult run = runBuildPipeline(request);
	expect(run.succeeded() && run.completedStageCount == 1 && QFileInfo::exists(QDir(project).filePath(QStringLiteral("maps/start.bsp"))), "the project pipeline runs",
		buildPipelineResultText(run));
}

void trustTests(const QString& folder)
{
	const QString project = QDir(folder).filePath(QStringLiteral("trust"));
	const QString program = QDir(project).filePath(QStringLiteral("tools/evil.exe"));
	expect(writeFile(program, QByteArrayLiteral("MZ not really a compiler")), "a program in the project");
	const QString self = QCoreApplication::applicationFilePath();

	const QVector<KnownCompilerProgram> known = knownCompilerPrograms(userCompilerRegistryOptions({{QStringLiteral("vibemap2-bsp"), self}}));
	bool configured = false;
	for (const KnownCompilerProgram& compiler : known) {
		configured = configured || (QFileInfo(compiler.path) == QFileInfo(self) && compiler.configured && compiler.engineFamily == QStringLiteral("idTech2"));
	}
	expect(configured, "a Toolchain path is a known compiler");

	BuildProgramUse mine {self, QStringLiteral("vibemap2-bsp start.map"), project, QStringLiteral("bsp"), QStringLiteral("idTech2")};
	BuildProgramUse theirs {program, QStringLiteral("evil.exe start.map"), project, QStringLiteral("bsp"), QStringLiteral("idTech2")};
	BuildProgramUse gone {QDir(project).filePath(QStringLiteral("tools/missing.exe")), QStringLiteral("missing"), project, QString(), QString()};
	BuildProgramReview review = reviewBuildPrograms({mine, theirs, gone}, project, known, {});
	expect(review.trust == QVector<BuildProgramTrust>({BuildProgramTrust::Known, BuildProgramTrust::Untrusted, BuildProgramTrust::Missing}) && review.needsConfirmation(),
		"known, untrusted and missing programs");
	expect(review.untrustedPrograms().size() == 1 && QFileInfo(review.untrustedPrograms()[0]) == QFileInfo(program), "the untrusted program is named once");
	const QString text = buildProgramReviewText(review);
	expect(text.contains(QStringLiteral("evil.exe start.map")) && text.contains(QStringLiteral("evil.exe")) && !text.contains(QStringLiteral("vibemap2-bsp start.map")),
		"the text shows the untrusted command line only", text);
	expect(buildProgramReviewJson(review).value(QStringLiteral("programs")).toArray().size() == 3, "JSON lists every program");

	// The compiler of another game is not known for this one.
	mine.engineFamily = QStringLiteral("idTech3");
	expect(reviewBuildPrograms({mine}, project, known, {}).trust.value(0) == BuildProgramTrust::Untrusted, "a known compiler of another game asks");

	// Discovery from inside the project's folder proves nothing.
	KnownCompilerProgram discovered {QStringLiteral("vibemap2-bsp"), QStringLiteral("idTech2"), program, false};
	expect(reviewBuildPrograms({theirs}, project, {discovered}, {}).trust.value(0) == BuildProgramTrust::Untrusted
			&& reviewBuildPrograms({theirs}, folder + QStringLiteral("/elsewhere"), {discovered}, {}).trust.value(0) == BuildProgramTrust::Known,
		"a discovered compiler inside the project is not known");

	// Trust, per project and per program's bytes.
	QVector<BuildProgramTrustRecord> records = withTrustedPrograms(review, {});
	expect(records.size() == 1 && records[0].sha256 == buildProgramFingerprint(program) && records[0].sha256.size() == 64, "trust records the hash");
	expect(withTrustedPrograms(review, records).size() == 1, "trusting again keeps one record");
	expect(reviewBuildPrograms({theirs}, project, known, records).trust.value(0) == BuildProgramTrust::Trusted, "trusted for the project");
	expect(reviewBuildPrograms({theirs}, QDir(folder).filePath(QStringLiteral("other")), known, records).trust.value(0) == BuildProgramTrust::Untrusted,
		"not for another project");
	expect(writeFile(program, QByteArrayLiteral("MZ changed")), "the program changes");
	expect(reviewBuildPrograms({theirs}, project, known, records).trust.value(0) == BuildProgramTrust::Untrusted, "a changed program asks again");

	// The records live in the user's settings.
	StudioSettings::setOverrideFilePath(QDir(folder).filePath(QStringLiteral("trust-settings.ini")));
	{
		StudioSettings settings;
		BuildProgramTrustRecord bad;
		bad.scope = project;
		bad.program = program;
		bad.sha256 = QStringLiteral("short");
		settings.setTrustedBuildPrograms(records + QVector<BuildProgramTrustRecord> {bad});
		settings.sync();
	}
	const QVector<BuildProgramTrustRecord> saved = StudioSettings().trustedBuildPrograms();
	expect(saved.size() == 1 && saved[0].program == records[0].program && saved[0].sha256 == records[0].sha256 && saved[0].trustedUtc.isValid(),
		"trust persists; a bad record is dropped", QString::number(saved.size()));

	// A saved run claims a profile: its program must be that game's compiler.
	CompilerCommandManifest manifest;
	manifest.profileId = QStringLiteral("vibemap2-bsp");
	manifest.program = program;
	manifest.arguments = {QStringLiteral("start.map")};
	manifest.workingDirectory = project;
	const BuildProgramUse replay = compilerManifestProgramUse(manifest);
	expect(replay.engineFamily == QStringLiteral("idTech2") && replay.commandLine.contains(QStringLiteral("start.map")) && replay.label == QStringLiteral("vibemap2-bsp"),
		"a saved run's program use");
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
	const QString project = QDir(folder).filePath(QStringLiteral("cli-project"));
	const QString map = QDir(project).filePath(QStringLiteral("maps/start.map"));
	expect(writeFile(map, mapText()), "CLI map");
	// The project names the test program as its BSP compiler: project data.
	ProjectManifest manifest = defaultProjectManifest(project, QStringLiteral("CLI pipelines"));
	manifest.compilerToolOverrides = {{QStringLiteral("vibemap2-bsp"), QCoreApplication::applicationFilePath()}};
	expect(saveProjectManifest(manifest), "CLI project");

	run({"build", "pipeline-set", "fast"}, 2);
	run({"build", "pipeline-set", "fast", "--workspace-root", QDir(folder).filePath(QStringLiteral("nowhere"))}, 3);
	run({"build", "pipeline-set", "fast", "--workspace-root", project}, 2);
	QJsonObject result = run({"build", "pipeline-set", "fast", "--workspace-root", project, "--name", "Fast", "--from", "quake-full", "--disable-stage", "vis",
		"--stage-args", "light=-extra ${QUALITY}", "--variable", "QUALITY=high", "--dry-run"});
	expect(result["dryRun"].toBool() && !result["saved"].toBool() && result["pipeline"].toObject()["source"] == QStringLiteral("project"), "pipeline-set dry run");
	ProjectManifest reread;
	expect(loadProjectManifest(project, &reread) && reread.buildPipelines.isEmpty(), "a dry run saves nothing");
	result = run({"build", "pipeline-set", "fast", "--workspace-root", project, "--name", "Fast", "--from", "quake-full", "--disable-stage", "vis",
		"--stage-args", "light=-extra ${QUALITY}", "--variable", "QUALITY=high"});
	expect(result["saved"].toBool() && !result["replaced"].toBool(), "pipeline added");
	result = run({"build", "pipeline-set", "fast", "--workspace-root", project, "--enable-stage", "vis", "--description", "Mine"});
	expect(result["replaced"].toBool() && result["stored"].toObject()["description"] == QStringLiteral("Mine"), "pipeline changed");
	run({"build", "pipeline-set", "quake-full", "--workspace-root", project, "--name", "Mine", "--from", "quake-bsp-only"}, 4);
	run({"build", "pipeline-set", "fast", "--workspace-root", project, "--stage-args", "nope=-x"}, 4);
	run({"build", "pipeline-set", "fast", "--workspace-root", project, "--stage", "bad"}, 2);
	run({"build", "pipeline-set", "fast", "--workspace-root", project, "--stage-args", "light=-extra ${NOPE}"}, 4);
	result = run({"build", "pipeline-set", "mine", "--workspace-root", project, "--name", "BSP only", "--stage", "bsp=vibemap2-bsp", "--stage-args",
		"bsp=--fake-compiler -name ${MAP_NAME}"});
	expect(result["saved"].toBool() && result["pipeline"].toObject()["stages"].toArray().size() == 1, "a pipeline from --stage");

	result = run({"build", "list", "--workspace-root", project});
	QStringList ids;
	for (const QJsonValue& value : result["pipelines"].toArray()) {
		ids << value.toObject()["id"].toString() + QLatin1Char(':') + value.toObject()["source"].toString();
	}
	expect(ids.contains(QStringLiteral("fast:project")) && ids.contains(QStringLiteral("mine:project")) && ids.contains(QStringLiteral("quake-full:built-in")),
		"build list shows the project's pipelines", ids.join(' '));
	expect(!run({"build", "list"})["pipelines"].toArray().isEmpty(), "build list without a project");

	result = run({"build", "plan", "fast", "--workspace-root", project, "--input", map, "--variable", "QUALITY=draft"});
	const QJsonObject planned = result["pipeline"].toObject();
	const QJsonArray stages = planned["stages"].toArray();
	expect(planned["source"] == QStringLiteral("project") && stages.size() == 3 && !stages[1].toObject()["skipped"].toBool()
			&& jsonStrings(stages[2].toObject()["arguments"]).contains(QStringLiteral("draft")),
		"build plan reads the project pipeline and --variable", QString::fromUtf8(QJsonDocument(planned).toJson()));
	expect(result["programTrust"].toObject()["needsConfirmation"].toBool(), "the plan says the project's compiler needs trusting");

	// build run: the project's compiler path names the test program.
	const QString bsp = QDir(project).filePath(QStringLiteral("maps/start.bsp"));
	result = run({"build", "run", "mine", "--workspace-root", project, "--input", map, "--dry-run"});
	expect(result["ok"].toBool() && !QFileInfo::exists(bsp), "a dry run never asks and runs nothing");
	result = run({"build", "run", "mine", "--workspace-root", project, "--input", map}, 4);
	expect(result["programTrust"].toObject()["needsConfirmation"].toBool() && result["message"].toString().contains(QStringLiteral("--trust-programs"))
			&& result["message"].toString().contains(QStringLiteral("--fake-compiler")) && !QFileInfo::exists(bsp),
		"an untrusted program is refused with its command line, and nothing runs", result["message"].toString());
	result = run({"build", "run", "mine", "--workspace-root", project, "--input", map, "--trust-programs"});
	expect(result["pipeline"].toObject()["succeeded"].toBool() && QFileInfo::exists(bsp), "--trust-programs trusts and runs");
	QFile::remove(bsp);
	result = run({"build", "run", "mine", "--workspace-root", project, "--input", map});
	expect(result["pipeline"].toObject()["succeeded"].toBool() && QFileInfo::exists(bsp), "trusted once, it runs without asking");
	{
		// Closed again before anything rewrites them (Windows cannot replace an open file).
		QFile manifestFile(projectManifestPath(project));
		expect(manifestFile.open(QIODevice::ReadOnly) && !manifestFile.readAll().contains("sha256"), "the trust is not kept in the project");
		QFile settingsText(settingsFile);
		expect(settingsText.open(QIODevice::ReadOnly) && settingsText.readAll().contains("trustedPrograms"), "the trust is kept in the user's settings");
	}

	result = run({"build", "pipeline-remove", "mine", "--workspace-root", project, "--dry-run"});
	expect(result["dryRun"].toBool() && result["pipelineCount"].toInt() == 1, "remove dry run");
	run({"build", "pipeline-remove", "mine", "--workspace-root", project});
	run({"build", "pipeline-remove", "mine", "--workspace-root", project}, 3);
	run({"build", "pipeline-remove", "quake-full", "--workspace-root", project}, 3);
	run({"build", "pipeline-remove", "fast", "--workspace-root", project, "--name", "x"}, 2);
	expect(loadProjectManifest(project, &reread) && projectBuildPipelines(reread).size() == 1, "one pipeline left");
	run({"build", "plan", "mine", "--workspace-root", project, "--input", map}, 3);
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
	pipelineTests(temp.path());
	planTests(temp.path());
	trustTests(temp.path());
	if (argc >= 2) {
		cliTests(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath(), temp.path());
	}
	std::cout << (ok ? "Build project pipelines smoke test passed." : "Build project pipelines smoke test failed.") << std::endl;
	return ok ? 0 : 1;
}
