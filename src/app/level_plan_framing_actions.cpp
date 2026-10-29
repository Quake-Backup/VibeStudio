// Plan framing in the Levels views: a one-plan projection change keeps the
// zoom and centres the new plane on the selection or the camera, Zoom 100%,
// Reset 2D Views, and the editor profile's Radiant framing for Frame Selection
// and the three-plan Top, Front, Side and Next 2D View. Navigation only: the
// map, its selection and its undo history never change
// (core/level_navigation.h).
//
// Workflow reference: NRC xywindow.cpp XY_SetViewType, XY_Centralize,
// XY_Focus, XYWnd::FocusOnBounds and XY_Zoom100 at 449778b3
// (GPL-2.0-or-later) and TB MapWindow::reset2dCameras and
// MapView2D::reset2dCameras at 4c756be1 (GPL-3.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_map.h"
#include "core/level_navigation.h"

#include <QComboBox>
#include <QScopedValueRollback>
#include <QStatusBar>

#include <algorithm>
#include <functional>

namespace vibestudio {

void ApplicationShell::registerLevelPlanFramingCommands()
{
	if (!m_commands) {
		return;
	}
	const auto add = [this](const QString& id, const QString& label, const QString& tip, const QString& icon, std::function<void()> handler) {
		StudioCommandRegistration command;
		command.commandId = id;
		command.group = StudioCommandGroup::View;
		command.label = label;
		command.statusTip = tip;
		command.iconName = icon;
		command.handler = std::move(handler);
		m_commands->registerCommand(command);
	};
	add(QStringLiteral("map.zoomActualSize"), tr("Zoom 100%"),
		tr("Show every 2D view at one screen pixel per map unit, each about its own centre. The camera and the map are unchanged."),
		QStringLiteral("fit"), [this]() { zoomLevelPlanViewsActualSize(); });
	add(QStringLiteral("map.resetPlanViews"), tr("Reset 2D Views"),
		tr("Give the other 2D views the focused one's zoom and its position on the axes they share, once, without linking them."),
		QStringLiteral("map"), [this]() { resetLevelPlanViews(); });
}

void ApplicationShell::refreshLevelPlanFramingCommands()
{
	if (!m_commands) {
		return;
	}
	m_commands->setEnabled(QStringLiteral("map.zoomActualSize"),
		m_levelMapDocument.format != LevelMapFormat::Unknown && m_levelMapViewport && m_levelMapViewport->hasDocument());
	m_commands->setEnabled(QStringLiteral("map.resetPlanViews"), levelPlanViewsResettable());
}

bool ApplicationShell::levelPlanCameraPoint(std::array<double, 3>* point) const
{
	// The camera of this map load only, as Centre Plan Views on Camera uses it:
	// the first-person camera's eye, or the orbit camera's target.
	if (!point || !m_levelMap3D || !m_levelMap3D->isEnabled() || !m_levelMap3D->hasMesh()
		|| m_levelMap3DSource != QString::number(m_levelMapLoadSerial)) {
		return false;
	}
	const CameraViewState camera = m_levelMap3D->navigationState();
	if (!validateCameraViewState(camera)) {
		return false;
	}
	*point = camera.perspective ? camera.position : camera.orbitTarget;
	return true;
}

bool ApplicationShell::levelPlanFocusPoint(std::array<double, 3>* point, bool* selection) const
{
	// NRC GetCenterBbox: the selection's bounds, else the camera.
	if (!point) {
		return false;
	}
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	if (levelMapSelectionBounds(m_levelMapDocument, &mins, &maxs)) {
		*point = {(mins.x + maxs.x) * 0.5, (mins.y + maxs.y) * 0.5, (mins.z + maxs.z) * 0.5};
		if (selection) {
			*selection = true;
		}
		return true;
	}
	if (selection) {
		*selection = false;
	}
	return levelPlanCameraPoint(point);
}

bool ApplicationShell::recentreLevelPlanForProjection(MapViewport* view, int projection)
{
	// NRC XY_SetViewType outside the split layout: the plane changes, the zoom
	// stays and the view centres on the selection or the camera. With neither,
	// the caller's own fit stands.
	if (!view || m_buildingUi || m_applyingLevelViewLayout || projection < 0 || projection > 2 || !view->hasDocument()
		|| m_levelMapDocument.format == LevelMapFormat::Unknown) {
		return false;
	}
	std::array<double, 3> point {};
	if (!levelPlanFocusPoint(&point)) {
		return false;
	}
	const PlanViewState state {projection, levelPlanCentreFor(projection, point), view->zoom()};
	{
		const QScopedValueRollback<bool> linking(m_applyingLevelViewNavigation, true);
		const bool applied = view->projection() == static_cast<MapViewportProjection>(projection) ? view->applyLinkedNavigation(state)
			: view->restoreNavigationState(state);
		if (!applied) {
			return false;
		}
	}
	synchronizeLevelViewNavigation(view);
	return true;
}

bool ApplicationShell::centreLevelPlanViewsOnFocus()
{
	// NRC XY_Centralize: every 2D view on the selection or the camera, each
	// keeping its zoom.
	if (m_levelMapDocument.format == LevelMapFormat::Unknown) {
		return false;
	}
	std::array<double, 3> point {};
	if (!levelPlanFocusPoint(&point)) {
		return false;
	}
	{
		const QScopedValueRollback<bool> linking(m_applyingLevelViewNavigation, true);
		for (MapViewport* view : std::as_const(m_levelPlanViews)) {
			if (view->QWidget::isHidden() || !view->hasDocument()) {
				continue;
			}
			PlanViewState state = view->navigationState();
			state.center = levelPlanCentreFor(state.projection, point);
			view->applyLinkedNavigation(state);
		}
	}
	synchronizeLevelViewNavigation(m_levelMapViewport);
	return true;
}

void ApplicationShell::frameLevelMapSelectionFromUi()
{
	if (m_levelControls.plan.radiantFraming) {
		focusLevelPlanViewsRadiant();
		return;
	}
	frameLevelMapSelection();
}

void ApplicationShell::focusLevelPlanViewsRadiant()
{
	// NRC XY_Focus: every 2D view frames the selection's bounds, or a 256-unit
	// box round the camera, with a third of the view to spare; the camera
	// stays where it is.
	if (m_levelMapDocument.format == LevelMapFormat::Unknown) {
		return;
	}
	std::array<double, 3> mins {};
	std::array<double, 3> maxs {};
	LevelMapVec3 low;
	LevelMapVec3 high;
	std::array<double, 3> camera {};
	const bool selection = levelMapSelectionBounds(m_levelMapDocument, &low, &high);
	if (selection) {
		mins = {low.x, low.y, low.z};
		maxs = {high.x, high.y, high.z};
	} else if (levelPlanCameraPoint(&camera)) {
		for (std::size_t axis = 0; axis < 3; ++axis) {
			mins[axis] = camera[axis] - kLevelPlanFocusCameraHalfExtent;
			maxs[axis] = camera[axis] + kLevelPlanFocusCameraHalfExtent;
		}
	} else {
		frameLevelPlanViews(false);
		statusBar()->showMessage(tr("Nothing is selected and the camera is not ready, so the 2D views frame the whole map."), 4000);
		return;
	}
	{
		const QScopedValueRollback<bool> linking(m_applyingLevelViewNavigation, true);
		for (MapViewport* view : std::as_const(m_levelPlanViews)) {
			if (view->QWidget::isHidden() || !view->hasDocument()) {
				continue;
			}
			PlanViewState state;
			if (levelPlanFocusState(static_cast<int>(view->projection()), mins, maxs, QSizeF(view->width(), view->height()), &state)) {
				view->applyLinkedNavigation(state);
			}
		}
	}
	// Linked zoom takes the smallest framed scale, so every view keeps it all.
	synchronizeLevelViewNavigation(m_levelMapViewport, true);
	statusBar()->showMessage(selection ? tr("The 2D views frame the selection; the camera stays where it is.")
									   : tr("Nothing is selected, so the 2D views frame the camera's surroundings."),
		3000);
}

void ApplicationShell::zoomLevelPlanViewsActualSize()
{
	// NRC XY_Zoom100: one screen pixel per map unit in every 2D view, each
	// about its own centre.
	if (m_levelMapDocument.format == LevelMapFormat::Unknown) {
		return;
	}
	{
		const QScopedValueRollback<bool> linking(m_applyingLevelViewNavigation, true);
		for (MapViewport* view : std::as_const(m_levelPlanViews)) {
			if (view->QWidget::isHidden() || !view->hasDocument()) {
				continue;
			}
			PlanViewState state = view->navigationState();
			state.zoom = 1.0;
			view->applyLinkedNavigation(state);
		}
	}
	synchronizeLevelViewNavigation(m_levelMapViewport);
	statusBar()->showMessage(tr("The 2D views show one screen pixel per map unit (100%)."), 3000);
}

bool ApplicationShell::levelPlanViewsResettable() const
{
	// TB disables Reset 2D Cameras while its 2D cameras are linked; here
	// that is centres and zoom both linked. A one-plan layout has no other
	// view to reset.
	if (m_levelMapDocument.format == LevelMapFormat::Unknown || m_levelPlanViews.size() != 3 || !m_levelMapViewport
		|| !m_levelMapViewport->hasDocument() || !m_levelPlanViews.contains(m_levelMapViewport) || !levelViewLayoutHasThreePlans(m_levelViewLayout)
		|| (m_levelViewLinks.centers && m_levelViewLinks.zoom)) {
		return false;
	}
	return levelPlanPanesShownCount(m_levelPlanPanes) >= 2;
}

void ApplicationShell::resetLevelPlanViews()
{
	if (!levelPlanViewsResettable()) {
		statusBar()->showMessage(tr("Reset 2D Views needs two or more 2D views showing, with their centres and zoom not both linked."), 4000);
		return;
	}
	const int source = static_cast<int>(m_levelPlanViews.indexOf(m_levelMapViewport));
	std::array<PlanViewState, 3> states;
	for (int index = 0; index < 3; ++index) {
		states[static_cast<std::size_t>(index)] = m_levelPlanViews[index]->navigationState();
	}
	QString error;
	if (!resetLevelPlanNavigation(&states, source, &error)) {
		statusBar()->showMessage(error, 4000);
		return;
	}
	{
		const QScopedValueRollback<bool> linking(m_applyingLevelViewNavigation, true);
		for (int index = 0; index < 3; ++index) {
			m_levelPlanViews[index]->applyLinkedNavigation(states[static_cast<std::size_t>(index)]);
		}
	}
	const QString name = m_levelMapProjection ? m_levelMapProjection->itemText(static_cast<int>(m_levelMapViewport->projection())) : QString();
	statusBar()->showMessage(tr("The other 2D views take the %1 view's zoom and its position on the axes they share.").arg(name), 4000);
}

} // namespace vibestudio
