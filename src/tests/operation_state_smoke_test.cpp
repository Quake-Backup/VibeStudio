#include "core/operation_state.h"

#include <QCoreApplication>

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

	slopstudio::OperationStateModel model;
	if (model.summaryText() != QStringLiteral("No activity yet.")) {
		return fail("Expected empty activity summary.");
	}
	if (slopstudio::operationStateFromId(QStringLiteral("canceled")) != slopstudio::OperationState::Cancelled) {
		return fail("Expected operation state id normalization.");
	}
	if (!slopstudio::operationStateAllowsCancellation(slopstudio::OperationState::Queued) || !slopstudio::operationStateAllowsCancellation(slopstudio::OperationState::Loading) || slopstudio::operationStateAllowsCancellation(slopstudio::OperationState::Completed)) {
		return fail("Expected cancellation policy.");
	}

	const QString scanId = model.createTask(QStringLiteral("Package Scan"), QStringLiteral("Read package entries"), QStringLiteral("package"), slopstudio::OperationState::Queued, true);
	if (!model.contains(scanId) || model.task(scanId).state != slopstudio::OperationState::Queued) {
		return fail("Expected created queued task.");
	}
	if (model.task(scanId).transitions.size() != 1 || model.task(scanId).durationMs != 0) {
		return fail("Expected task timing to start with one zero-duration transition.");
	}
	model.transitionTask(scanId, slopstudio::OperationState::Running, QStringLiteral("Scan started."));
	model.transitionTask(scanId, slopstudio::OperationState::Loading, QStringLiteral("Entries loading."));
	model.setProgress(scanId, 5, 10, QStringLiteral("Halfway."));
	if (model.task(scanId).state != slopstudio::OperationState::Loading) {
		return fail("Expected loading state.");
	}
	if (model.task(scanId).progress.current != 5 || slopstudio::operationProgressPercent(model.task(scanId).progress) != 50) {
		return fail("Expected progress to be stored and normalized.");
	}
	model.appendWarning(scanId, QStringLiteral("Unknown lump type."));
	if (model.task(scanId).state != slopstudio::OperationState::Warning || model.task(scanId).warnings.size() != 1) {
		return fail("Expected warning state and warning list.");
	}
	model.completeTask(scanId, QStringLiteral("Scan completed with warnings."));
	if (model.task(scanId).state != slopstudio::OperationState::Warning || model.task(scanId).progress.current != model.task(scanId).progress.total) {
		return fail("Expected warning task to remain warning after completion.");
	}
	const slopstudio::OperationTask scanTask = model.task(scanId);
	if (scanTask.durationMs < 0 || slopstudio::operationTaskElapsedMs(scanTask) < 0 || scanTask.transitions.size() < 5) {
		return fail("Expected task elapsed timing and transition history.");
	}
	const QString scanTimeline = slopstudio::operationTaskTimelineText(scanTask);
	if (!scanTimeline.contains(QStringLiteral("[queued]")) || !scanTimeline.contains(QStringLiteral("[running]")) || !scanTimeline.contains(QStringLiteral("[warning]"))) {
		return fail("Expected timeline text to include timed state transitions.");
	}

	const QString compilerId = model.createTask(QStringLiteral("Compiler Probe"), QStringLiteral("Run q3map2 help"), QStringLiteral("compiler"), slopstudio::OperationState::Running, true);
	if (!model.cancelTask(compilerId, QStringLiteral("Probe cancelled by user."))) {
		return fail("Expected cancellable task to cancel.");
	}
	if (model.task(compilerId).state != slopstudio::OperationState::Cancelled || model.cancelTask(compilerId)) {
		return fail("Expected cancelled task to be terminal.");
	}

	const QString failedId = model.createTask(QStringLiteral("Validation"), QStringLiteral("Validate package"), QStringLiteral("qa"));
	model.failTask(failedId, QStringLiteral("Validation failed."));
	if (model.task(failedId).state != slopstudio::OperationState::Failed || model.task(failedId).finishedUtc.isNull()) {
		return fail("Expected failed terminal task.");
	}
	if (model.task(failedId).durationMs < 0 || model.task(failedId).transitions.constLast().state != slopstudio::OperationState::Failed) {
		return fail("Expected terminal transition timing.");
	}

	const QMap<slopstudio::OperationState, int> counts = model.stateCounts();
	if (counts.value(slopstudio::OperationState::Warning) != 1 || counts.value(slopstudio::OperationState::Cancelled) != 1 || counts.value(slopstudio::OperationState::Failed) != 1) {
		return fail("Expected state counts.");
	}
	if (!model.summaryText().contains(QStringLiteral("1 warning")) || !model.summaryText().contains(QStringLiteral("1 failed"))) {
		return fail("Expected summary text to include terminal states.");
	}
	if (!model.clearTerminalTasks() || !model.tasks().isEmpty()) {
		return fail("Expected terminal tasks to clear.");
	}

	// A streamed log line carries a level ("info", "warning", "error"), which is
	// not a task state: read as one it is Idle or Warning, and appendLog() gives
	// the task the state it is handed. A caller that streams a running task's
	// output therefore logs each line as Running, whatever its level.
	if (slopstudio::operationStateFromId(QStringLiteral("info")) != slopstudio::OperationState::Idle
		|| slopstudio::operationStateFromId(QStringLiteral("error")) != slopstudio::OperationState::Idle) {
		return fail("Expected log levels not to be task states.");
	}
	const QString buildId = model.createTask(QStringLiteral("Build Pipeline"), QStringLiteral("maps/start.map"), QStringLiteral("build"), slopstudio::OperationState::Queued, true);
	model.transitionTask(buildId, slopstudio::OperationState::Running, QStringLiteral("Running build pipeline."));
	model.appendLog(buildId, slopstudio::OperationState::Running, QStringLiteral("light: 12 lights"));
	model.appendLog(buildId, slopstudio::OperationState::Running, QStringLiteral("Stage finished: BSP (Warning)"));
	if (model.task(buildId).state != slopstudio::OperationState::Running || !slopstudio::operationStateAllowsCancellation(model.task(buildId).state)
		|| model.task(buildId).log.size() != 4) {
		return fail("Expected streamed lines logged as Running to leave the task running and cancellable.");
	}
	// A warning leaves the task in a terminal state, and a terminal task refuses
	// to be cancelled: a cancelled run is put back to Running first.
	model.appendWarning(buildId, QStringLiteral("Stage BSP leaked."));
	if (model.cancelTask(buildId, QStringLiteral("Cancelled by the user."))) {
		return fail("Expected a task holding a warning to refuse cancellation as it stands.");
	}
	model.transitionTask(buildId, slopstudio::OperationState::Running);
	if (!model.cancelTask(buildId, QStringLiteral("Cancelled by the user.")) || model.task(buildId).state != slopstudio::OperationState::Cancelled
		|| model.task(buildId).resultSummary != QStringLiteral("Cancelled by the user.") || model.task(buildId).warnings.size() != 1) {
		return fail("Expected a cancelled run to end Cancelled with its reason and keep its warning.");
	}
	model.clearTerminalTasks();

	// A long session's history is bounded: the newest finished tasks stay, with
	// every unfinished one and the ones the caller names.
	QStringList finishedIds;
	for (int index = 0; index < 6; ++index) {
		const QString id = model.createTask(QStringLiteral("Edit %1").arg(index), QString(), QStringLiteral("level-map"));
		model.completeTask(id, QStringLiteral("Unsaved map edit"));
		finishedIds << id;
	}
	const QString runningId = model.createTask(QStringLiteral("Compile"), QString(), QStringLiteral("compiler"), slopstudio::OperationState::Running, true);
	for (int index = 6; index < 9; ++index) {
		const QString id = model.createTask(QStringLiteral("Edit %1").arg(index), QString(), QStringLiteral("level-map"));
		model.completeTask(id, QStringLiteral("Unsaved map edit"));
		finishedIds << id;
	}
	if (!model.pruneTerminalTasks(9).isEmpty() || model.tasks().size() != 10) {
		return fail("Expected nothing to be pruned at the limit.");
	}
	const QStringList pruned = model.pruneTerminalTasks(4, {finishedIds.at(1)});
	// The four newest finished tasks stay (5 to 8), the named one (1) stays, and
	// the running task is never dropped.
	if (pruned != QStringList({finishedIds.at(4), finishedIds.at(3), finishedIds.at(2), finishedIds.at(0)}) || model.tasks().size() != 6
		|| !model.contains(runningId) || !model.contains(finishedIds.at(1)) || !model.contains(finishedIds.at(5)) || model.contains(finishedIds.at(4))) {
		return fail("Expected pruning to keep the newest finished tasks, the named one and the running one.");
	}
	if (model.tasks().first().id != finishedIds.at(8) || model.tasks().last().id != finishedIds.at(1)) {
		return fail("Expected pruning to keep the tasks newest first.");
	}
	if (!model.pruneTerminalTasks(0).contains(finishedIds.at(8)) || model.tasks().size() != 1 || !model.contains(runningId)) {
		return fail("Expected a limit of none to leave only the unfinished task.");
	}

	return EXIT_SUCCESS;
}
