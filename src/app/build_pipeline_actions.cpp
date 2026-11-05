// The project's own build pipelines on the Build page, and the check before a
// build starts a program that project data names.
//
// Build Pipelines…: NetRadiant Custom's Build > Customize… (DoBuildMenu in
// radiant/build.cpp, the BuildMenuCustomize command at
// radiant/mainframe.cpp:2089, 449778b3, GPL-2.0-or-later) edits named builds
// of command lines with variables, saved per game; TrenchBroom's compilation
// profiles (CompilationProfileManager at 4c756be1, GPL-3.0-or-later) are
// saved per game too. Here they
// are compiler profile stages kept in the project manifest, offered in the
// Build page's Pipeline list after the built-in ones and read by `build list`,
// `build plan` and `build run`. Original Qt implementation; see
// docs/CREDITS.md.
//
// Program trust: a project folder, or the saved runs beside a map, can come
// from someone else, so a program that is not a compiler from the user's own
// configuration starts only after the user has seen its command line and
// trusted it for that project (core/build_program_trust.h). The trust is kept
// in the user's settings, never in the project.

#include "app/application_shell.h"
#include "app/build_pipelines_dialog.h"
#include "app/studio_actions.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>

namespace vibestudio {

QVector<BuildPipelineDescriptor> ApplicationShell::projectBuildPipelinesForBuild() const
{
	const QString project = m_settings.currentProjectPath();
	ProjectManifest manifest;
	if (project.trimmed().isEmpty() || !loadProjectManifest(project, &manifest)) {
		return {};
	}
	return projectBuildPipelines(manifest);
}

bool ApplicationShell::buildPipelineDescriptorForId(const QString& id, BuildPipelineDescriptor* out) const
{
	return buildPipelineForId(id, projectBuildPipelinesForBuild(), out);
}

void ApplicationShell::refreshBuildPipelineChoices()
{
	if (!m_buildPipelineChoice) {
		return;
	}
	const QVector<BuildPipelineDescriptor> builtIn = buildPipelineDescriptors();
	const QVector<BuildPipelineDescriptor> mine = projectBuildPipelinesForBuild();
	// Rebuilt only when the list itself changed, so the choice is kept.
	QStringList wanted;
	for (const BuildPipelineDescriptor& pipeline : builtIn + mine) {
		wanted << pipeline.id + QLatin1Char('|') + pipeline.displayName;
	}
	QStringList shown;
	for (int index = 0; index < m_buildPipelineChoice->count(); ++index) {
		const QString id = m_buildPipelineChoice->itemData(index).toString();
		if (!id.isEmpty()) {
			shown << id + QLatin1Char('|') + m_buildPipelineChoice->itemText(index);
		}
	}
	if (wanted == shown) {
		return;
	}
	const QString current = m_buildPipelineChoice->currentData().toString();
	const QSignalBlocker blocker(m_buildPipelineChoice);
	m_buildPipelineChoice->clear();
	for (const BuildPipelineDescriptor& pipeline : builtIn) {
		m_buildPipelineChoice->addItem(pipeline.displayName, pipeline.id);
		m_buildPipelineChoice->setItemData(m_buildPipelineChoice->count() - 1, pipeline.description, Qt::ToolTipRole);
	}
	if (!mine.isEmpty()) {
		m_buildPipelineChoice->insertSeparator(m_buildPipelineChoice->count());
	}
	for (const BuildPipelineDescriptor& pipeline : mine) {
		QStringList stages;
		for (const BuildPipelineStage& stage : pipeline.stages) {
			stages << (stage.enabledByDefault ? stage.displayName : tr("%1 (off)").arg(stage.displayName));
		}
		m_buildPipelineChoice->addItem(pipeline.displayName, pipeline.id);
		m_buildPipelineChoice->setItemData(m_buildPipelineChoice->count() - 1,
			tr("The project's own pipeline: %1").arg(stages.join(QStringLiteral(", "))), Qt::ToolTipRole);
	}
	const int index = m_buildPipelineChoice->findData(current);
	m_buildPipelineChoice->setCurrentIndex(index < 0 ? 0 : index);
}

void ApplicationShell::editBuildPipelines()
{
	const QString project = m_settings.currentProjectPath();
	if (project.trimmed().isEmpty()) {
		statusBar()->showMessage(tr("Open a project to keep build pipelines with it."));
		return;
	}
	ProjectManifest manifest;
	QString error;
	if (!loadProjectManifest(project, &manifest, &error)) {
		statusBar()->showMessage(tr("The project manifest could not be read: %1").arg(error));
		return;
	}
	followOpenMapInBuild();
	refreshBuildPipelineChoices();
	// Pipelines with problems are loaded too, so they can be put right.
	const QVector<BuildPipelineDescriptor> pipelines = projectBuildPipelines(manifest, nullptr, true);
	const QString chosen = m_buildPipelineChoice ? m_buildPipelineChoice->currentData().toString() : QString();
	BuildPipelineDescriptor starter;
	if (!buildPipelineForId(chosen, pipelines, &starter)) {
		buildPipelineForId(buildPipelineIds().value(0), &starter);
	}
	BuildPipelinesDialog dialog(pipelines, starter, this);
	dialog.setPreviewRequest(currentBuildPipelineRequest(true));
	dialog.selectPipeline(chosen);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	const QVector<BuildPipelineDescriptor> saved = dialog.pipelines();
	// Read again, so anything else that changed in the manifest meanwhile is kept.
	ProjectManifest latest;
	if (!loadProjectManifest(project, &latest, &error)) {
		latest = manifest;
	}
	setProjectBuildPipelines(&latest, saved);
	if (!saveProjectManifest(latest, &error)) {
		statusBar()->showMessage(tr("The build pipelines could not be saved: %1").arg(error));
		return;
	}
	refreshBuildPipelineChoices();
	if (m_buildPipelineChoice && !dialog.currentPipelineId().isEmpty()) {
		const int index = m_buildPipelineChoice->findData(dialog.currentPipelineId());
		if (index >= 0) {
			m_buildPipelineChoice->setCurrentIndex(index);
		}
	}
	const int count = static_cast<int>(saved.size());
	recordActivity(tr("Build pipelines saved"), tr("%n pipeline(s) of the project's own.", nullptr, count), tr("build"), OperationState::Completed);
	refreshBuildSurface();
	statusBar()->showMessage(tr("%n build pipeline(s) of the project's own saved.", nullptr, count));
}

QString ApplicationShell::buildProgramScopeFor(const QString& inputPath) const
{
	const QString project = m_settings.currentProjectPath();
	if (!project.trimmed().isEmpty()) {
		return project;
	}
	return inputPath.trimmed().isEmpty() ? QString() : QFileInfo(inputPath).absolutePath();
}

bool ApplicationShell::confirmBuildProgramsTrusted(const QVector<BuildProgramUse>& uses, const QString& scope)
{
	const BuildProgramReview review = reviewBuildPrograms(uses, scope,
		knownCompilerPrograms(userCompilerRegistryOptions(m_settings.compilerToolPathOverrides())), m_settings.trustedBuildPrograms());
	if (!review.needsConfirmation()) {
		return true;
	}
	const int count = static_cast<int>(review.untrustedPrograms().size());
	QMessageBox box(this);
	box.setObjectName(QStringLiteral("buildProgramTrustReview"));
	box.setIcon(QMessageBox::Warning);
	box.setWindowTitle(tr("Run a Program from the Project?"));
	box.setTextFormat(Qt::PlainText);
	// Plain text throughout: paths and arguments are shown exactly as they are.
	box.setText(tr("This build would start %n program(s) that are not among your configured compilers. A project, or a saved run beside a map, "
				   "can come from someone else, so check the command line before running it.", nullptr, count)
		+ QStringLiteral("\n\n") + buildProgramReviewText(review) + QStringLiteral("\n\n")
		+ tr("Trust and Run remembers your choice for %1 in your own settings, until the program changes.").arg(QDir::toNativeSeparators(review.scope)));
	box.setAccessibleName(box.windowTitle());
	box.setAccessibleDescription(box.text());
	QPushButton* run = box.addButton(tr("Trust and Run"), QMessageBox::AcceptRole);
	run->setObjectName(QStringLiteral("buildProgramTrustRun"));
	QPushButton* cancel = box.addButton(QMessageBox::Cancel);
	box.setDefaultButton(cancel);
	box.setEscapeButton(cancel);
	box.exec();
	if (box.clickedButton() != run) {
		recordActivity(tr("Build not started"), tr("%n program(s) not trusted.", nullptr, count), tr("build"), OperationState::Cancelled,
			tr("Nothing was run."), review.untrustedPrograms());
		statusBar()->showMessage(tr("Nothing was run: the program is not one of your configured compilers. Set it on the Toolchain tab, or choose Trust and Run."));
		return false;
	}
	m_settings.setTrustedBuildPrograms(withTrustedPrograms(review, m_settings.trustedBuildPrograms()));
	m_settings.sync();
	recordActivity(tr("Program trusted"), QDir::toNativeSeparators(review.scope), tr("build"), OperationState::Completed,
		tr("%n program(s) trusted for this project.", nullptr, count), review.untrustedPrograms());
	return true;
}

} // namespace vibestudio
