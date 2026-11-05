#pragma once

// Chained build pipelines and game launch plans.
//
// A pipeline turns the single-profile compiler wrappers into the loop a mapper
// actually runs: source map -> node/BSP stage -> visibility stage -> lighting
// stage -> packaged output -> launch in the configured game. Every stage still
// goes through the shared compiler runner, so logs, diagnostics, hashes, and
// command manifests stay identical to a single-profile run.

#include "core/compiler_profiles.h"
#include "core/compiler_runner.h"
#include "core/game_installation.h"
#include "core/operation_state.h"

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace vibestudio {

struct ProjectManifest;

struct BuildPipelineStage {
	QString id;
	QString profileId;
	QString displayName;
	QString description;
	QStringList extraArguments;
	// Empty means "use the pipeline input". Otherwise the id of an earlier
	// stage whose expected output feeds this stage.
	QString inputFromStageId;
	bool optional = false;
	bool enabledByDefault = true;
	// A project pipeline's stage: its arguments as typed, split as a shell
	// would when the pipeline is planned, with ${NAME} variables filled in;
	// and whether it reads the previous stage's output rather than the map
	// (inputFromStageId follows the order, see normalizeProjectBuildPipeline).
	QString argumentLine;
	bool inputFromPrevious = false;
};

struct BuildPipelineDescriptor {
	QString id;
	QString displayName;
	QString engineFamily;
	QString description;
	QStringList inputExtensions;
	QString outputExtension;
	QVector<BuildPipelineStage> stages;
	// A pipeline of the project's own (projectBuildPipelines), and its
	// variables: ${NAME} in its stages' arguments.
	bool projectDefined = false;
	QMap<QString, QString> variables;
};

struct BuildPipelineRequest {
	QString pipelineId;
	QString inputPath;
	QString outputPath;
	QString workingDirectory;
	QString workspaceRootPath;
	QStringList disabledStageIds;
	QMap<QString, QStringList> stageExtraArguments;
	QStringList extraSearchPaths;
	QVector<CompilerToolPathOverride> executableOverrides;
	QString manifestDirectory;
	// The project's pipelines, looked up before the built-in ones.
	QVector<BuildPipelineDescriptor> projectPipelines;
	// Values for ${GAME_DIR}, ${MOD} or any variable of a project pipeline;
	// they win over the pipeline's own.
	QMap<QString, QString> variables;
	bool dryRun = false;
	bool stopOnFailure = true;
	bool registerOutputs = false;
	int stageTimeoutMs = 10 * 60 * 1000;
};

struct BuildPipelineStageResult {
	BuildPipelineStage stage;
	CompilerCommandPlan plan;
	CompilerRunResult run;
	OperationState state = OperationState::Idle;
	bool skipped = false;
	QString skipReason;
	QString inputPath;
	QString outputPath;
	QString manifestPath;
};

struct BuildPipelineResult {
	BuildPipelineDescriptor pipeline;
	QString inputPath;
	QString finalOutputPath;
	OperationState state = OperationState::Idle;
	bool dryRun = false;
	bool cancelled = false;
	qint64 durationMs = -1;
	int plannedStageCount = 0;
	int completedStageCount = 0;
	int failedStageCount = 0;
	int skippedStageCount = 0;
	QVector<BuildPipelineStageResult> stages;
	QVector<CompilerDiagnostic> diagnostics;
	QStringList registeredOutputPaths;
	QStringList warnings;
	QStringList errors;

	[[nodiscard]] bool succeeded() const;
};

struct BuildPipelineCallbacks {
	std::function<bool()> cancellationRequested;
	std::function<void(int stageIndex, const BuildPipelineStage& stage)> stageStarted;
	std::function<void(int stageIndex, const BuildPipelineStageResult& result)> stageFinished;
	std::function<void(const CompilerTaskLogEntry& entry)> logEntry;
};

QVector<BuildPipelineDescriptor> buildPipelineDescriptors();
QStringList buildPipelineIds();
bool buildPipelineForId(const QString& id, BuildPipelineDescriptor* out = nullptr);
// The project's pipelines first, then the built-in ones.
bool buildPipelineForId(const QString& id, const QVector<BuildPipelineDescriptor>& projectPipelines, BuildPipelineDescriptor* out);
QStringList buildPipelineIdsForInput(const QString& inputPath);

// ---------------------------------------------------------------------------
// Project build pipelines
// ---------------------------------------------------------------------------
//
// Pipelines of the project's own, kept in its manifest (`buildPipelines`):
// a name, an ordered list of compiler profile stages, each with arguments,
// an on/off switch and its input (the map or the previous stage's output),
// and variables. Arguments may use ${MAP}, ${MAP_DIR}, ${MAP_NAME},
// ${OUTPUT}, ${PROJECT_DIR}, ${GAME_DIR}, ${MOD}, ${CPU_COUNT} and the
// pipeline's own variables; a switch whose variable is empty is left out
// with its value. Every stage still runs a compiler profile, so its program
// comes from the compiler configuration (see core/build_program_trust.h for
// programs a project's compiler paths name).

// The built-in variable names, without ${}.
QStringList buildPipelineVariableNames();
// A new project pipeline id from a name: its letters and digits, unique among
// `existing` and the built-in pipelines.
QString projectBuildPipelineId(const QString& name, const QVector<BuildPipelineDescriptor>& existing);
// A new stage id from its profile's stage, unique in the pipeline.
QString projectBuildPipelineStageId(const QString& profileId, const BuildPipelineDescriptor& pipeline);
// Engine, input and output types from the first stage, and each stage's
// input chained to the stage before it when it reads the previous output.
void normalizeProjectBuildPipeline(BuildPipelineDescriptor* pipeline);
// A new project pipeline with the stages, arguments and variables of
// `source` (built in or the project's), named `name`.
BuildPipelineDescriptor projectBuildPipelineFrom(const BuildPipelineDescriptor& source, const QString& name,
	const QVector<BuildPipelineDescriptor>& existing);
// What stops a project pipeline from being saved or run, in words; empty
// when it can. `projectPipelines` is the whole list it belongs to, for unique
// ids and names.
QStringList projectBuildPipelineProblems(const BuildPipelineDescriptor& pipeline, const QVector<BuildPipelineDescriptor>& projectPipelines = {});
QJsonObject projectBuildPipelineJson(const BuildPipelineDescriptor& pipeline);
bool projectBuildPipelineFromJson(const QJsonObject& object, BuildPipelineDescriptor* out);
// The manifest's pipelines. Without `keepInvalid` a pipeline with problems is
// left out and named in `warnings`.
QVector<BuildPipelineDescriptor> projectBuildPipelines(const ProjectManifest& manifest, QStringList* warnings = nullptr, bool keepInvalid = false);
void setProjectBuildPipelines(ProjectManifest* manifest, const QVector<BuildPipelineDescriptor>& pipelines);
// A pipeline as `build list --json` shows it, built in or the project's.
QJsonObject buildPipelineDescriptorJson(const BuildPipelineDescriptor& pipeline);

// Resolves stages, inputs, and outputs without running anything.
BuildPipelineResult planBuildPipeline(const BuildPipelineRequest& request);
BuildPipelineResult runBuildPipeline(const BuildPipelineRequest& request, const BuildPipelineCallbacks& callbacks = {});

QString buildPipelineResultText(const BuildPipelineResult& result);
QJsonObject buildPipelineResultJson(const BuildPipelineResult& result);

// ---------------------------------------------------------------------------
// Launch / in-game testing
// ---------------------------------------------------------------------------

struct GameLaunchProfile {
	QString id;
	QString displayName;
	QString engineFamily;
	QString description;
	// Argument templates. Supported tokens: {map}, {mod}, {basedir}, {bsp}.
	QStringList argumentTemplates;
	bool requiresMap = true;
	// A profile the user made (StudioSettings::userGameLaunchProfiles). Its
	// arguments are kept as typed in `argumentLine` and split into switch
	// groups when a launch is planned (gameLaunchArgumentGroups).
	bool userDefined = false;
	QString argumentLine;
};

struct GameLaunchRequest {
	QString launchProfileId;
	QString executablePath;
	QString mapName;
	QString modDirectory;
	QString baseDirectory;
	QString bspPath;
	QString workingDirectory;
	QStringList extraArguments;
	// The user's own launch profiles, looked up before the built-in ones.
	QVector<GameLaunchProfile> userProfiles;
	bool dryRun = true;
};

struct GameLaunchPlan {
	GameLaunchProfile profile;
	bool profileFound = false;
	bool runnable = false;
	QString program;
	QStringList arguments;
	QString commandLine;
	QString workingDirectory;
	QStringList warnings;
	QStringList errors;
	// A deferred preview is never runnable. Real preparation validates a WAD
	// snapshot; startGameLaunch rechecks its hash before starting the process.
	bool artifactValidationPending = false;
	bool cancelled = false;
	QString validatedArtifactPath;
	QByteArray validatedArtifactHash;
	QJsonObject nodeBuild;

	[[nodiscard]] OperationState state() const;
};

QVector<GameLaunchProfile> gameLaunchProfiles();
QStringList gameLaunchProfileIds();
bool gameLaunchProfileForId(const QString& id, GameLaunchProfile* out = nullptr);
// The user's profiles first, then the built-in ones.
bool gameLaunchProfileForId(const QString& id, const QVector<GameLaunchProfile>& userProfiles, GameLaunchProfile* out);
QString defaultGameLaunchProfileId(GameEngineFamily family);

// User launch profiles (after TrenchBroom's game engine profiles): a name and
// a line of arguments with the tokens {map}, {mod}, {basedir}, {bsp}, {iwad}
// and {warp}. Words are split as a shell would (quotes keep spaces), then
// grouped from each switch (a word starting with - or +) to the next; a group
// whose token has no value is left out of the launch, silently for {mod}
// (the base game), with a warning otherwise.
QStringList gameLaunchTokens();
QVector<QStringList> gameLaunchArgumentGroups(const QString& argumentLine);
// Splits typed extra arguments as a shell would; quotes keep spaces.
QStringList splitGameLaunchArguments(const QString& text);
// An id for a new user profile: "user-" and the name's letters and digits,
// made unique among `existing`.
QString userGameLaunchProfileId(const QString& name, const QVector<GameLaunchProfile>& existing);
// Why a user profile cannot be saved: a missing or built-in id, no name, an
// unknown token or unbalanced quotes. Empty when it can.
QString userGameLaunchProfileProblem(const GameLaunchProfile& profile);
// A user profile's argument line with its tokens filled in, as a launch
// passes it: {warp} may stand for two words, and a group whose token is empty
// is left out (named in `droppedGroups`).
QStringList resolveGameLaunchArgumentLine(const QString& argumentLine, const QMap<QString, QString>& tokens, QStringList* droppedGroups = nullptr);
QJsonObject gameLaunchProfileJson(const GameLaunchProfile& profile);
bool gameLaunchProfileFromJson(const QJsonObject& object, GameLaunchProfile* out);

// The IWAD a Doom-family launch loads: `baseDirectory` when it names a file,
// else the installation's first base package that exists, else the first of
// its game's known IWADs found in the folder. Empty when there is none.
QString doomIwadPath(const GameInstallationProfile& installation, const QString& baseDirectory);
// The -warp arguments for a Doom map lump: "07" for MAP07, "2 3" for E2M3.
// Anything else comes back as given.
QString doomWarpArguments(const QString& mapName);
GameLaunchPlan buildGameLaunchPlan(const GameLaunchRequest& request, const GameInstallationProfile& installation,
	const std::function<bool()>& isCancelled = {}, bool deferArtifactValidation = false);
// Starts the planned process detached. Never runs when `plan.runnable` is false.
bool startGameLaunch(const GameLaunchPlan& plan, qint64* pidOut = nullptr, QString* error = nullptr,
	const std::function<bool()>& isCancelled = {});
QString gameLaunchPlanText(const GameLaunchPlan& plan);
QJsonObject gameLaunchPlanJson(const GameLaunchPlan& plan);

// ---------------------------------------------------------------------------
// Test-map deployment
// ---------------------------------------------------------------------------
//
// Quake-family engines load a map only from a game folder's maps directory
// (`<root>/id1/maps/start.bsp`), so testing a build means copying it there.
// Doom-family ports load the built PWAD directly with -file, so nothing is
// copied for them. Copying writes into the user's game installation, so a plan
// is only runnable for an installation profile that is not read-only.

struct GameMapDeployFile {
	QString sourcePath;
	QString destinationPath;
	// A file of that name is already there with different bytes.
	bool replacesExisting = false;
	// The destination already holds these exact bytes.
	bool upToDate = false;
};

struct GameMapDeployPlan {
	// The engine loads maps from a game folder, so a copy is needed at all.
	bool required = false;
	// The installation profile allows the studio to write into it.
	bool allowed = false;
	QString gameDirectory;
	QString targetDirectory;
	QVector<GameMapDeployFile> files;
	QStringList warnings;
	QStringList errors;

	[[nodiscard]] bool upToDate() const;
	[[nodiscard]] bool runnable() const;
};

// Plans copying `builtMapPath` and its lighting companions (`.lit`, `.lux`)
// into `<root>/<game folder>/maps`. The game folder is `modDirectory` when it
// is given, else the game's base folder; it must be a single folder name.
GameMapDeployPlan planGameMapDeploy(const GameInstallationProfile& installation, const QString& modDirectory, const QString& builtMapPath);
// Copies every file of a runnable plan through a temporary file and a rename,
// so a failed copy never leaves a half-written map. Nothing else in the
// installation is touched. `written` receives the destinations written.
bool deployGameMap(const GameMapDeployPlan& plan, QStringList* written = nullptr, QString* error = nullptr);
QString gameMapDeployPlanText(const GameMapDeployPlan& plan);
QJsonObject gameMapDeployPlanJson(const GameMapDeployPlan& plan);

} // namespace vibestudio
