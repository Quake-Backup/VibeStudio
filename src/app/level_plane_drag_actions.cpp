// NetRadiant Custom's drag-tool gestures and Build tool in the shell: the
// plan views' and the camera's plane drags, vertex drags and extrusions, the
// Build tool (B in the NetRadiant Custom profile) and its copy chains
// (core/level_plane_drag.h). Plane drags, vertex drags and extrusions go
// through applyLevelPlaneDrag(); a chain goes through Duplicate with Offset's
// array placement and is recorded for Repeat Last Commands.
//
// Workflow reference: NetRadiant Custom radiant/tools.cpp MouseDrag and
// MouseBuild and radiant/selection_mtor_drag.cpp DragManipulator at 449778b3
// (GPL-2.0-or-later). Original Qt implementation; see docs/CREDITS.md.
#include "app/application_shell.h"
#include "app/level_plane_drag_controller.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_object_manipulator.h"
#include "core/level_placement.h"
#include "core/level_plane_drag.h"

#include <QAction>
#include <QSignalBlocker>
#include <QStatusBar>

namespace vibestudio {
namespace {
bool quakeFamily(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
}
} // namespace

void ApplicationShell::registerLevelPlaneDragCommands()
{
	StudioCommandRegistration command;
	command.commandId = QStringLiteral("map.buildTool");
	command.label = tr("Build Tool");
	command.group = StudioCommandGroup::Edit;
	command.menuSection = tr("Manipulators");
	command.iconName = QStringLiteral("extrude");
	command.checkable = true;
	command.statusTip = tr("Drag a selected brush's face out into a new brush, or in to split a slab off; Shift and a drag on the selection lay "
						   "copies of it end to end, which Repeat Last Commands lays again. Choose it again to return to Select.");
	command.handler = [this] {
		setMode(StudioMode::Levels);
		chooseLevelTool(m_levelBuildToolOn ? QStringLiteral("select") : QStringLiteral("build"));
	};
	m_commands->registerCommand(command);
	m_commands->setEnabled(command.commandId, false);
}

bool ApplicationShell::levelBuildToolActive() const
{
	return m_levelBuildToolOn && quakeFamily(m_levelMapDocument);
}

void ApplicationShell::setLevelBuildTool(bool enabled)
{
	if (enabled && (!quakeFamily(m_levelMapDocument) || !m_levelMapViewport || !m_levelMap3D)) {
		statusBar()->showMessage(tr("The Build tool works on brushes. Open a Quake, Quake II or Quake III map."), 6000);
		enabled = false;
	}
	// One tool at a time: Build ends the Shear Tool, as the object tools do.
	if (enabled && m_levelMapViewport && m_levelMapViewport->shearMode()) {
		setLevelShearMode(false);
	}
	const bool changed = enabled != m_levelBuildToolOn;
	m_levelBuildToolOn = enabled;
	refreshLevelPlaneDrags();
	// The Build tool replaces the camera's resize box while it is on.
	refreshLevelCameraResize();
	if (enabled && changed) {
		statusBar()->showMessage(tr("Build tool: drag a selected brush's face out to extrude a new brush, or in to split a slab off; Shift and a "
									"drag on the selection lay copies end to end."),
			8000);
	}
}

void ApplicationShell::refreshLevelPlaneDrags()
{
	if (!m_levelMapViewport || !m_levelMap3D) {
		return;
	}
	const bool quake = quakeFamily(m_levelMapDocument);
	const bool build = levelBuildToolActive();
	// Asked at each press, so a refresh costs nothing: the whole selection
	// must be editable here, as the object manipulators require.
	const auto availability = [this]() -> QString {
		if (!quakeFamily(m_levelMapDocument)) {
			return tr("Plane drags work on brushes in Quake-family maps.");
		}
		if (levelComponentsActive()) {
			return tr("Leave the vertex, edge or face mode to drag brush planes.");
		}
		const LevelObjectManipulatorTarget target = levelObjectManipulatorTarget(m_levelMapDocument);
		return target.available ? QString() : target.reason;
	};
	const bool textureLock = m_settings.levelTextureLock();
	for (auto* view : std::as_const(m_levelPlanViews)) {
		view->planeDrag()->setContext(availability, view->snapToGrid() ? view->gridSize() : 0, textureLock);
		view->setBuildMode(build);
	}
	m_levelMap3D->setPlaneDragDocument(quake ? &m_levelMapDocument : nullptr);
	m_levelMap3D->planeDrag()->setContext(availability, m_levelMapViewport->snapToGrid() ? m_levelMapViewport->gridSize() : 0, textureLock);
	m_levelMap3D->setBuildMode(build);
	if (m_commands) {
		m_commands->setEnabled(QStringLiteral("map.buildTool"), quake);
		if (QAction* action = m_commands->action(QStringLiteral("map.buildTool"))) {
			const QSignalBlocker blocker(action);
			action->setChecked(build);
		}
	}
}

void ApplicationShell::connectLevelPlaneDrag(LevelPlaneDragController* controller, MapViewport* plan)
{
	connect(controller, &LevelPlaneDragController::activeChanged, this, [this, plan](bool active) {
		if (active && plan) {
			activateLevelPlanViewport(plan);
		}
	});
	connect(controller, &LevelPlaneDragController::commitRequested, this,
		[this, plan](const LevelPlaneDragRequest& request) { applyLevelPlaneDragFromView(request, plan); });
	connect(controller, &LevelPlaneDragController::chainRequested, this,
		[this, plan](const LevelMapVec3& step, int copies) { buildLevelCloneChainFromView(step, copies, plan); });
}

void ApplicationShell::applyLevelPlaneDragFromView(const LevelPlaneDragRequest& request, MapViewport* plan)
{
	if (plan) {
		activateLevelPlanViewport(plan);
	}
	// Texture Lock as it is now: it decides an extruded face's mapping.
	LevelPlaneDragRequest made = request;
	made.textureLock = m_settings.levelTextureLock();
	LevelPlaneDragReport report;
	QString error;
	if (!applyLevelPlaneDrag(&m_levelMapDocument, made, &report, &error)) {
		statusBar()->showMessage(tr("Brushes unchanged: %1").arg(error), 6000);
		refreshLevelMapViewport();
		return;
	}
	QString message;
	QString activity;
	switch (request.kind) {
	case LevelPlaneDragKind::Extrude:
		activity = tr("Level map faces extruded");
		message = request.distance > 0
			? tr("Extruded %n face(s) %1 units into %2 new brush(es). Undo restores them.", nullptr, report.planes)
				  .arg(request.distance).arg(report.createdBrushes)
			: tr("Pushed %n face(s) %1 units in, splitting %2 slab(s) off. Undo restores them.", nullptr, report.planes)
				  .arg(-request.distance).arg(report.createdBrushes);
		break;
	case LevelPlaneDragKind::Vertices:
		activity = tr("Level map vertices dragged");
		message = tr("Dragged %n vertex(es) by %1, %2, %3. Undo restores them.", nullptr, report.vertices)
			.arg(request.translation.x).arg(request.translation.y).arg(request.translation.z);
		break;
	case LevelPlaneDragKind::FacingPlanes:
	case LevelPlaneDragKind::Plane:
		activity = tr("Level map brush planes dragged");
		message = tr("Dragged %n brush plane(s) by %1, %2, %3. Undo restores them.", nullptr, report.planes)
			.arg(request.translation.x).arg(request.translation.y).arg(request.translation.z);
		if (report.clampedBrushes > 0) {
			message += QLatin1Char(' ') + tr("%n brush(es) stopped short so that none of their faces vanished.", nullptr, report.clampedBrushes);
		}
		break;
	}
	recordActivity(activity, tr("%n brush(es)", nullptr, report.brushes), QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	refreshLevelObjectGizmos();
	refreshLevelCameraResize();
	statusBar()->showMessage(message, 5000);
}

void ApplicationShell::buildLevelCloneChainFromView(const LevelMapVec3& step, int copies, MapViewport* plan)
{
	if (plan) {
		activateLevelPlanViewport(plan);
	}
	if (!canDuplicateLevelMapSelection()) {
		statusBar()->showMessage(tr("Select duplicable map objects to build a chain of copies."), 6000);
		return;
	}
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::Duplicate;
	request.offset = step;
	request.copies = copies;
	request.textures = {m_settings.levelTextureLock(), m_settings.levelAllowValve220()};
	QString error;
	if (!runLevelPlacementFromUi(request, &error)) {
		if (!error.isEmpty()) {
			statusBar()->showMessage(tr("Could not build the chain: %1").arg(error), 6000);
		}
		return;
	}
	// One array, one undo step; Repeat Last Commands lays the same chain on.
	recordLevelCloneChainRepeat(&m_levelMapDocument, step, copies);
	const QString offset = tr("%1, %2, %3").arg(step.x).arg(step.y).arg(step.z);
	recordActivity(tr("Level map chain built"), offset, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	refreshCommandEnablement();
	statusBar()->showMessage(tr("Laid %n copy(ies) of the selection end to end, each %1 from the last; the last copy is selected. Repeat Last "
								"Commands carries the chain on; Undo removes it.",
								 nullptr, copies)
			.arg(offset),
		6000);
}

} // namespace vibestudio
