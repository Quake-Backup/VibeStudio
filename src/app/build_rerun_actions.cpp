// Repeating builds and the launch settings of the user's own.
//
// Run Last Build: NetRadiant Custom's Build_runRecentExecutedBuild (F5, at
// 449778b3, GPL-2.0-or-later) runs the build menu entry used last on the
// current map. Here it is the build pipeline (with its launch, when Build and
// Launch ran it) or the Levels compiler profile run last for the project.
// Re-run Last Compile: TrenchBroom's Re-run compilation (MapWindow::
// rerunLastCompilation at 4c756be1, GPL-3.0-or-later), enabled only when
// there is something to run; here it replays the open map's newest saved
// compiler run. The Levels profile is remembered for each game, as
// TrenchBroom remembers the last compilation profile per game. Original Qt
// implementations; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/game_launch_profiles_dialog.h"
#include "app/studio_actions.h"
#include "core/entity_builtin_catalogue.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QThread>

namespace vibestudio {

namespace {

// The game a Levels compile is remembered for.
QString levelCompilerGameKey(const LevelMapDocument& document)
{
	switch (document.format) {
	case LevelMapFormat::DoomWad:
		return QStringLiteral("doom");
	case LevelMapFormat::QuakeMap:
	case LevelMapFormat::Quake3Map:
		return builtinEntityGameId(builtinEntityGameForMap(document));
	case LevelMapFormat::Unknown:
		break;
	}
	return {};
}

QString levelMapEngineFamily(const LevelMapDocument& document)
{
	switch (document.format) {
	case LevelMapFormat::DoomWad:
		return QStringLiteral("idTech1");
	case LevelMapFormat::QuakeMap:
		return QStringLiteral("idTech2");
	case LevelMapFormat::Quake3Map:
		return QStringLiteral("idTech3");
	case LevelMapFormat::Unknown:
		break;
	}
	return {};
}

// Two spellings of one file, as the Build page compares them.
bool sameFilePathForRerun(const QString& left, const QString& right)
{
	if (left.trimmed().isEmpty() || right.trimmed().isEmpty()) {
		return false;
	}
	const QFileInfo leftInfo(left);
	const QFileInfo rightInfo(right);
	QString a = leftInfo.canonicalFilePath();
	QString b = rightInfo.canonicalFilePath();
	if (a.isEmpty() || b.isEmpty()) {
		a = QDir::cleanPath(leftInfo.absoluteFilePath());
		b = QDir::cleanPath(rightInfo.absoluteFilePath());
	}
#ifdef Q_OS_WIN
	return a.compare(b, Qt::CaseInsensitive) == 0;
#else
	return a == b;
#endif
}

bool extensionListed(const QStringList& extensions, const QString& path)
{
	if (extensions.isEmpty()) {
		return true;
	}
	const QString suffix = QFileInfo(path).suffix();
	for (QString extension : extensions) {
		if (extension.startsWith(QLatin1Char('.'))) {
			extension.remove(0, 1);
		}
		if (extension.compare(suffix, Qt::CaseInsensitive) == 0) {
			return true;
		}
	}
	return false;
}

// A compiler profile that can compile the open map: one of its game's, for
// its kind of file.
bool profileSuitsMap(const CompilerProfileDescriptor& profile, const LevelMapDocument& document, const QString& mapPath)
{
	const QString family = levelMapEngineFamily(document);
	return !family.isEmpty() && profile.engineFamily.compare(family, Qt::CaseInsensitive) == 0 && profile.inputRequired
		&& extensionListed(profile.inputExtensions, mapPath);
}

} // namespace

QStringList ApplicationShell::levelCompileManifestRoots() const
{
	// Where Run Profile keeps its runs: the open project, else the map's folder.
	QStringList roots;
	const QString project = m_settings.currentProjectPath();
	if (!project.trimmed().isEmpty()) {
		roots << project;
	}
	const QString mapPath = openLevelMapBuildPath();
	if (!mapPath.isEmpty()) {
		roots << QFileInfo(mapPath).absolutePath();
	}
	return roots;
}

void ApplicationShell::noteBuildRunStarted(const QString& kind, const QString& id, bool launch, const QString& inputPath)
{
	StudioBuildRunRecord record;
	record.kind = kind;
	record.id = id;
	record.launch = launch;
	record.inputPath = inputPath;
	record.timeUtc = QDateTime::currentDateTimeUtc();
	m_settings.setLastBuildRun(m_settings.currentProjectPath(), record);
	if (kind == QLatin1String("profile")) {
		const QString game = levelCompilerGameKey(m_levelMapDocument);
		if (!game.isEmpty()) {
			m_settings.setLevelCompilerProfile(game, id);
		}
	}
	refreshBuildRerunCommands();
}

void ApplicationShell::selectLevelCompilerProfileForMap()
{
	if (!m_levelMapCompilerProfile || m_levelMapDocument.format == LevelMapFormat::Unknown) {
		return;
	}
	QString wanted = compilerRequestForLevelMap(m_levelMapDocument, QString()).profileId;
	const QString remembered = m_settings.levelCompilerProfile(levelCompilerGameKey(m_levelMapDocument));
	CompilerProfileDescriptor profile;
	if (!remembered.isEmpty() && compilerProfileForId(remembered, &profile) && profileSuitsMap(profile, m_levelMapDocument, openLevelMapBuildPath())) {
		wanted = profile.id;
	}
	const int index = m_levelMapCompilerProfile->findData(wanted);
	if (index >= 0) {
		m_levelMapCompilerProfile->setCurrentIndex(index);
	}
}

void ApplicationShell::refreshBuildRerunCommands()
{
	if (!m_commands) {
		return;
	}
	const bool idle = m_buildPipelineThread == nullptr && m_compilerRunThread == nullptr;
	const StudioBuildRunRecord record = m_settings.lastBuildRun(m_settings.currentProjectPath());
	// A profile runs on the open map; a pipeline on the Build page's input.
	const bool canRunLast = record.isValid() && (record.kind == QLatin1String("pipeline") || m_levelMapDocument.format != LevelMapFormat::Unknown);
	m_commands->setEnabled(QStringLiteral("build.runLast"), idle && canRunLast);

	// Looked up again only when the map, the project or the saved runs change.
	const QString mapPath = openLevelMapBuildPath();
	const QString key = QStringList {mapPath, m_settings.currentProjectPath(), QString::number(m_compilerRunsFinished), QString::number(m_levelMapLoadSerial)}
		.join(QLatin1Char('|'));
	if (key != m_lastCompileManifestKey) {
		m_lastCompileManifestKey = key;
		// The same newest runs of each folder the command reads, so the
		// command is enabled exactly when it can find a run.
		CompilerRunManifestMatch match;
		m_lastCompileManifestPath = !mapPath.isEmpty() && findNewestCompilerRunManifest(levelCompileManifestRoots(), mapPath, &match) ? match.path : QString();
	}
	m_commands->setEnabled(QStringLiteral("compiler.rerunLast"), m_compilerRunThread == nullptr && !m_lastCompileManifestPath.isEmpty());
}

void ApplicationShell::runLastBuild()
{
	if (m_buildPipelineThread || m_compilerRunThread) {
		statusBar()->showMessage(tr("A build is already running."));
		return;
	}
	const StudioBuildRunRecord record = m_settings.lastBuildRun(m_settings.currentProjectPath());
	if (!record.isValid()) {
		statusBar()->showMessage(tr("Nothing has been built here yet. Run a build pipeline, or Run Profile in Levels, and Run Last Build repeats it."));
		return;
	}
	if (record.kind == QLatin1String("profile")) {
		CompilerProfileDescriptor profile;
		if (!compilerProfileForId(record.id, &profile)) {
			statusBar()->showMessage(tr("The last build used compiler profile %1, which this version does not have.").arg(record.id));
			return;
		}
		if (m_levelMapDocument.format == LevelMapFormat::Unknown) {
			statusBar()->showMessage(tr("Open a map to compile it with %1 again.").arg(profile.displayName));
			return;
		}
		if (!profileSuitsMap(profile, m_levelMapDocument, openLevelMapBuildPath().isEmpty() ? m_levelMapDocument.sourcePath : openLevelMapBuildPath())) {
			statusBar()->showMessage(tr("The last build, %1, is for another game. Choose Run Profile to compile this map.").arg(profile.displayName));
			return;
		}
		if (m_levelMapCompilerProfile) {
			const int index = m_levelMapCompilerProfile->findData(profile.id);
			if (index >= 0) {
				m_levelMapCompilerProfile->setCurrentIndex(index);
			}
		}
		planLevelMapCompile();
		return;
	}

	BuildPipelineDescriptor pipeline;
	refreshBuildPipelineChoices();
	if (!buildPipelineDescriptorForId(record.id, &pipeline) || !m_buildPipelineChoice || !m_buildPipelineInput) {
		statusBar()->showMessage(tr("The last build used pipeline %1, which this version does not have.").arg(record.id));
		return;
	}
	followOpenMapInBuild();
	const QString input = m_buildPipelineInput->text().trimmed();
	const QString family = m_buildInputFollowsMap && sameFilePathForRerun(input, openLevelMapBuildPath()) ? levelMapEngineFamily(m_levelMapDocument) : QString();
	if ((!family.isEmpty() && pipeline.engineFamily.compare(family, Qt::CaseInsensitive) != 0)
		|| (!input.isEmpty() && !extensionListed(pipeline.inputExtensions, input))) {
		statusBar()->showMessage(tr("The last build, %1, is for another game. Choose a pipeline for this map on the Build page.").arg(pipeline.displayName));
		return;
	}
	const int index = m_buildPipelineChoice->findData(pipeline.id);
	if (index >= 0 && index != m_buildPipelineChoice->currentIndex()) {
		m_buildPipelineChoice->setCurrentIndex(index);
	}
	if (record.launch) {
		buildAndLaunch();
	} else {
		runSelectedBuildPipeline();
	}
}

void ApplicationShell::rerunLastCompile()
{
	if (m_compilerRunThread) {
		statusBar()->showMessage(tr("A compiler task is already running"));
		return;
	}
	const QString mapPath = openLevelMapBuildPath();
	CompilerRunManifestMatch match;
	if (mapPath.isEmpty() || !findNewestCompilerRunManifest(levelCompileManifestRoots(), mapPath, &match)) {
		statusBar()->showMessage(mapPath.isEmpty() ? tr("Open a map to re-run its last compile.")
			: tr("%1 has no saved compiler run yet. Run Profile compiles it and saves one.").arg(QFileInfo(mapPath).fileName()));
		m_lastCompileManifestKey.clear();
		refreshBuildRerunCommands();
		return;
	}
	// The compiler reads the file on disk, so unsaved edits would be left out.
	if (!confirmLevelMapEditsHandled(tr("The compiler reads the saved file, so it would not see them. Save and compile the current map?"),
			tr("Compile Saved File"))) {
		return;
	}
	// Saving can write the edits to a new file, which the saved run does not
	// name: that file is compiled with the same profile instead.
	if (!sameFilePathForRerun(openLevelMapBuildPath(), mapPath)) {
		if (m_levelMapCompilerProfile) {
			const int index = m_levelMapCompilerProfile->findData(match.manifest.profileId);
			if (index >= 0) {
				m_levelMapCompilerProfile->setCurrentIndex(index);
			}
		}
		runLevelMapCompile(m_levelMapDocument);
		return;
	}

	// The new run goes beside the one it repeats, and becomes the newest.
	const QString runsFolder = QFileInfo(match.path).absolutePath();
	const QString root = QDir::cleanPath(QDir(runsFolder).filePath(QStringLiteral("../..")));
	// The saved run names its program, and the folder it lies in can come from
	// someone else: only a compiler of the user's own runs without asking
	// (build_pipeline_actions.cpp).
	if (!confirmBuildProgramsTrusted({compilerManifestProgramUse(match.manifest)}, root)) {
		return;
	}
	CompilerManifestRerunRequest rerun;
	rerun.manifestPath = newCompilerRunManifestPath(root, match.manifest.profileId);
	rerun.registerOutputs = true;
	noteBuildRunStarted(QStringLiteral("profile"), match.manifest.profileId, false, mapPath);

	m_compilerRunCancelRequested.store(false);
	m_compilerRunActivityId = m_activity.createTask(tr("Re-run Last Compile"), match.manifest.profileId, tr("level-map"), OperationState::Running, true);
	m_activity.setProgress(m_compilerRunActivityId, 0, 4, tr("Replaying the saved compiler command."));
	m_activity.appendLog(m_compilerRunActivityId, OperationState::Running, tr("Replaying %1").arg(QDir::toNativeSeparators(match.path)));
	refreshActivityCenter(m_compilerRunActivityId);
	statusBar()->showMessage(tr("Re-running the last compile of %1: %2").arg(QFileInfo(mapPath).fileName(), match.manifest.commandLine));

	const QString taskId = m_compilerRunActivityId;
	const QString projectPath = m_settings.currentProjectPath();
	const CompilerCommandManifest manifest = match.manifest;
	auto* thread = QThread::create([this, manifest, rerun, taskId, projectPath]() {
		CompilerRunCallbacks callbacks;
		callbacks.cancellationRequested = [this]() {
			return m_compilerRunCancelRequested.load();
		};
		callbacks.logEntry = [this, taskId](const CompilerTaskLogEntry& entry) {
			QMetaObject::invokeMethod(this, [this, taskId, entry]() {
				m_activity.appendLog(taskId, OperationState::Running, entry.message);
				if (selectedActivityTaskId() == taskId) {
					refreshActivityDetails(taskId);
				}
			}, Qt::QueuedConnection);
		};
		const CompilerRunResult result = rerunCompilerCommandManifest(manifest, rerun, callbacks);
		QMetaObject::invokeMethod(this, [this, result, taskId, projectPath]() {
			finishCompilerRun(result, taskId, projectPath);
		}, Qt::QueuedConnection);
	});
	m_compilerRunThread = thread;
	connect(thread, &QThread::finished, thread, &QObject::deleteLater);
	thread->start();
	refreshCommandEnablement();
}

void ApplicationShell::refreshLaunchProfileChoices()
{
	if (!m_launchProfileChoice) {
		return;
	}
	// The user's profiles made for the selected installation's engine, or for
	// any game; the rest stay saved for the installations they suit.
	GameInstallationProfile installation;
	const GameEngineFamily engine = selectedGameInstallation(&installation) ? installation.engineFamily : GameEngineFamily::Unknown;
	QVector<GameLaunchProfile> mine;
	for (const GameLaunchProfile& profile : m_settings.userGameLaunchProfiles()) {
		const GameEngineFamily family = gameEngineFamilyFromId(profile.engineFamily);
		if (family == GameEngineFamily::Unknown || engine == GameEngineFamily::Unknown || family == engine) {
			mine.push_back(profile);
		}
	}
	// Rebuilt only when the list itself changed, so the choice is kept.
	QStringList wanted;
	for (const GameLaunchProfile& profile : gameLaunchProfiles() + mine) {
		wanted << profile.id + QLatin1Char('|') + profile.displayName + QLatin1Char('|') + profile.argumentLine;
	}
	if (m_launchProfileChoice->property("vibestudioProfiles").toStringList() == wanted && m_launchProfileChoice->count() > 0) {
		return;
	}
	m_launchProfileChoice->setProperty("vibestudioProfiles", wanted);
	const QString current = m_launchProfileChoice->currentData().toString();
	const QSignalBlocker blocker(m_launchProfileChoice);
	m_launchProfileChoice->clear();
	// Automatic uses the profile made for the installation's engine, so a
	// Quake III installation is not started with Quake's switches.
	m_launchProfileChoice->addItem(tr("Automatic (matches the installation)"), QString());
	for (const GameLaunchProfile& profile : gameLaunchProfiles()) {
		m_launchProfileChoice->addItem(profile.displayName, profile.id);
		m_launchProfileChoice->setItemData(m_launchProfileChoice->count() - 1, profile.description, Qt::ToolTipRole);
	}
	if (!mine.isEmpty()) {
		m_launchProfileChoice->insertSeparator(m_launchProfileChoice->count());
	}
	for (const GameLaunchProfile& profile : mine) {
		m_launchProfileChoice->addItem(profile.displayName, profile.id);
		m_launchProfileChoice->setItemData(m_launchProfileChoice->count() - 1, tr("Your launch profile: %1").arg(profile.argumentLine), Qt::ToolTipRole);
	}
	const int index = m_launchProfileChoice->findData(current);
	m_launchProfileChoice->setCurrentIndex(index < 0 ? 0 : index);
}

void ApplicationShell::editGameLaunchProfiles()
{
	// New profiles start from the arguments Automatic would use.
	GameInstallationProfile installation;
	const bool installed = selectedGameInstallation(&installation);
	GameLaunchProfile starter;
	QString starterArguments;
	if (gameLaunchProfileForId(defaultGameLaunchProfileId(installed ? installation.engineFamily : GameEngineFamily::Unknown), &starter)) {
		starterArguments = starter.argumentTemplates.join(QLatin1Char(' '));
	}
	GameLaunchProfilesDialog dialog(m_settings.userGameLaunchProfiles(), starterArguments, this);
	QMap<QString, QString> tokens;
	followOpenMapInBuild();
	const QString built = (m_buildPipelineChoice && m_buildPipelineInput) ? planBuildPipeline(currentBuildPipelineRequest(true)).finalOutputPath : QString();
	const QString typedMap = m_launchMapName ? m_launchMapName->text().trimmed() : QString();
	tokens.insert(QStringLiteral("map"), typedMap.isEmpty() ? derivedLaunchMapName(built) : typedMap);
	tokens.insert(QStringLiteral("mod"), m_launchGameDirectory ? m_launchGameDirectory->text().trimmed() : QString());
	tokens.insert(QStringLiteral("basedir"), installed ? QDir::toNativeSeparators(installation.rootPath) : QString());
	tokens.insert(QStringLiteral("bsp"), QDir::toNativeSeparators(built));
	tokens.insert(QStringLiteral("iwad"), installed ? QDir::toNativeSeparators(doomIwadPath(installation, installation.rootPath)) : QString());
	tokens.insert(QStringLiteral("warp"), doomWarpArguments(tokens.value(QStringLiteral("map"))));
	dialog.setPreviewTokens(tokens);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	const QVector<GameLaunchProfile> profiles = dialog.profiles();
	m_settings.setUserGameLaunchProfiles(profiles);
	m_settings.sync();
	refreshLaunchProfileChoices();
	if (m_launchProfileChoice && !dialog.currentProfileId().isEmpty()) {
		const int index = m_launchProfileChoice->findData(dialog.currentProfileId());
		if (index >= 0) {
			m_launchProfileChoice->setCurrentIndex(index);
		}
	}
	const int count = static_cast<int>(profiles.size());
	recordActivity(tr("Launch profiles saved"), tr("%n launch profile(s) of your own.", nullptr, count), tr("game"), OperationState::Completed);
	refreshBuildSurface();
	statusBar()->showMessage(tr("%n launch profile(s) of your own saved.", nullptr, count));
}

} // namespace vibestudio
