// View > Camera Display: how the Levels camera draws the map. Textures in
// Camera and Edges in Camera are saved choices, also on the View tab (and the
// Textures box above the views); 3D Wireframe hides the faces and keeps those
// choices for when it is turned off. Show Textures in Camera, Hide Textures in
// Camera and Hide Faces in Camera set the face mode outright, as TrenchBroom's
// View Filter does, and Next and Previous Camera Mode step NetRadiant Custom's
// four-mode cycle, saying in the status bar which mode is now drawn. They
// change only the picture: the map, its undo history, the selection, scene
// locks, hidden objects and the open group are untouched.
//
// Shade Faces in Camera, Fog in Camera (with its start and end distances on
// the View tab) and Grid on Camera Faces are three more saved choices, as
// TrenchBroom's View Filter has them. The face grid uses the current grid
// size and, like the camera's ground grid, shows only while Show Grid is on.
//
// Far Clip in Camera is NetRadiant Custom's cubic clip: a saved draw distance
// (the Draw distance list on the View tab) beyond which the camera draws
// nothing, stepped by Shorter and Longer Draw Distance whether or not it is
// on. While both are on, the fog ends no farther than the clip distance
// (core/level_camera_render.h). The camera's corner shows the distance.
//
// Workflow reference: TB MapViewBase::showMaterials, hideMaterials, hideFaces,
// toggleShowEdges, toggleShadeFaces and toggleShowFog, and the grid drawn on
// brush faces while the document grid is visible, at 4c756be1
// (GPL-3.0-or-later) / NRC camwindow.cpp CameraModeNext, CameraModePrev,
// Camera_ToggleFarClip, Camera_CubeIn and Camera_CubeOut at 449778b3
// (GPL-2.0-or-later). Original Qt implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_camera_render.h"

#include <QAction>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>

#include <functional>
#include <utility>

namespace vibestudio {

namespace {

const QString kTexturedKey = QStringLiteral("levelPreviewTextured");
const QString kWireframeKey = QStringLiteral("levelPreviewWireframe");
const QString kEdgesKey = QStringLiteral("levelPreviewEdges");
const QString kShadingKey = QStringLiteral("levelPreviewShading");
const QString kFogKey = QStringLiteral("levelPreviewFog");
const QString kFogRangeKey = QStringLiteral("levelPreviewFogRange");
const QString kFaceGridKey = QStringLiteral("levelPreviewFaceGrid");

QByteArray onOff(bool on)
{
	return on ? QByteArrayLiteral("on") : QByteArrayLiteral("off");
}

} // namespace

void ApplicationShell::registerLevelCameraRenderCommands()
{
	if (!m_commands) {
		return;
	}
	// Textures were on unless turned off; faces hidden and edges only when
	// they were turned on.
	m_levelCameraRender.textures = m_settings.shellLayoutState(kTexturedKey) != QByteArrayLiteral("off");
	m_levelCameraRender.facesHidden = m_settings.shellLayoutState(kWireframeKey) == QByteArrayLiteral("on");
	m_levelCameraRender.edges = m_settings.shellLayoutState(kEdgesKey) == QByteArrayLiteral("on");
	// Shading was always on; fog and the face grid are new and start off. A
	// fog range that does not read keeps the defaults.
	m_levelCameraShading.shadeFaces = m_settings.shellLayoutState(kShadingKey) != QByteArrayLiteral("off");
	m_levelCameraShading.fog = m_settings.shellLayoutState(kFogKey) == QByteArrayLiteral("on");
	if (!levelCameraFogRangeFromText(m_settings.shellLayoutState(kFogRangeKey), &m_levelCameraShading)) {
		m_levelCameraShading = levelCameraWithFogRange(m_levelCameraShading, kLevelCameraFogDefaultStart, kLevelCameraFogDefaultEnd);
	}
	m_levelCameraShading.faceGrid = m_settings.shellLayoutState(kFaceGridKey) == QByteArrayLiteral("on");
	// The far clip starts off at NetRadiant Custom's step 14 until saved.
	if (!levelCameraFarClipFromText(m_settings.shellLayoutState(levelCameraFarClipSettingsKey()), &m_levelCameraFarClip)) {
		m_levelCameraFarClip = {};
	}

	const auto add = [this](const QString& id, const QString& label, const QString& tip, const QString& icon, bool checkable,
		std::function<void()> handler) {
		StudioCommandRegistration command;
		command.commandId = id;
		command.group = StudioCommandGroup::View;
		command.label = label;
		command.statusTip = tip;
		command.iconName = icon;
		command.checkable = checkable;
		command.menuSection = tr("Camera Display");
		command.handler = std::move(handler);
		return m_commands->registerCommand(command);
	};
	m_levelPreviewTextured = add(QStringLiteral("map.previewTextures"), tr("Textures in Camera"),
		tr("Draw the camera's faces with package images and static shader previews; untick for flat-shaded colours. The choice is kept."),
		QStringLiteral("image"), true, [this]() {
			LevelCameraRenderState state = m_levelCameraRender;
			state.textures = !state.textures;
			QString message = levelCameraRenderSummary(state);
			if (state.facesHidden) {
				message = state.textures ? tr("Textures in Camera on: the faces show their textures when 3D Wireframe is turned off.")
										 : tr("Textures in Camera off: the faces are flat-shaded when 3D Wireframe is turned off.");
			}
			setLevelCameraRender(state, message);
		});
	add(QStringLiteral("map.previewEdges"), tr("Edges in Camera"),
		tr("Outline every brush face and patch quad over the camera's faces, without the triangles they are drawn with. The choice is kept."),
		QStringLiteral("grid"), true, [this]() {
			LevelCameraRenderState state = m_levelCameraRender;
			state.edges = !state.edges;
			QString message = levelCameraRenderSummary(state);
			if (state.facesHidden) {
				message = state.edges ? tr("Edges in Camera on: edges are drawn over the faces when 3D Wireframe is turned off.")
									  : tr("Edges in Camera off: the faces are drawn without edges when 3D Wireframe is turned off.");
			}
			setLevelCameraRender(state, message);
		});
	add(QStringLiteral("map.showCameraTextures"), tr("Show Textures in Camera"),
		tr("Draw the camera's faces textured, turning 3D Wireframe off if it is on."), QStringLiteral("image"), false, [this]() {
			const LevelCameraRenderState state = levelCameraWithFaceMode(m_levelCameraRender, LevelCameraFaceMode::Textured);
			setLevelCameraRender(state, levelCameraRenderSummary(state));
		});
	add(QStringLiteral("map.hideCameraTextures"), tr("Hide Textures in Camera"),
		tr("Draw the camera's faces in flat-shaded colours, turning 3D Wireframe off if it is on."), QStringLiteral("cube"), false, [this]() {
			const LevelCameraRenderState state = levelCameraWithFaceMode(m_levelCameraRender, LevelCameraFaceMode::Flat);
			setLevelCameraRender(state, levelCameraRenderSummary(state));
		});
	add(QStringLiteral("map.hideCameraFaces"), tr("Hide Faces in Camera"),
		tr("Draw no faces in the camera, only the brush and patch edges, as 3D Wireframe does."), QStringLiteral("cube"), false, [this]() {
			const LevelCameraRenderState state = levelCameraWithFaceMode(m_levelCameraRender, LevelCameraFaceMode::Hidden);
			setLevelCameraRender(state, levelCameraRenderSummary(state));
		});
	add(QStringLiteral("map.nextCameraMode"), tr("Next Camera Mode"),
		tr("Step the camera to the next of Wireframe, Flat Shaded, Textured and Textured with Edges, after the last back to the first."),
		QStringLiteral("cube"), false, [this]() { stepLevelCameraDrawMode(1); });
	add(QStringLiteral("map.previousCameraMode"), tr("Previous Camera Mode"),
		tr("Step the camera back through Textured with Edges, Textured, Flat Shaded and Wireframe, before the first round to the last."),
		QStringLiteral("cube"), false, [this]() { stepLevelCameraDrawMode(-1); });
	add(QStringLiteral("map.previewShading"), tr("Shade Faces in Camera"),
		tr("Darken the camera's faces by the way they face, so neighbouring planes read apart; untick to draw every face at full brightness. The choice is kept."),
		QStringLiteral("cube"), true, [this]() {
			LevelCameraShadingState state = m_levelCameraShading;
			state.shadeFaces = !state.shadeFaces;
			setLevelCameraShading(state, levelCameraShadingSummary(state));
		});
	add(QStringLiteral("map.previewFog"), tr("Fog in Camera"),
		tr("Fade the camera's faces towards the background with distance, between the fog start and end on the View tab, as a depth cue. The choice is kept."),
		QStringLiteral("cube"), true, [this]() {
			LevelCameraShadingState state = m_levelCameraShading;
			state.fog = !state.fog;
			QString message = levelCameraFogSummary(state);
			if (levelCameraFogWithinFarClip(state, m_levelCameraFarClip) != state) {
				message += QLatin1Char(' ')
					+ tr("Far Clip in Camera ends it at %1.").arg(levelCameraFarClipDistanceText(m_levelCameraFarClip.step));
			}
			setLevelCameraShading(state, message);
		});
	add(QStringLiteral("map.previewFaceGrid"), tr("Grid on Camera Faces"),
		tr("Draw the grid at the current grid size on brush faces in the camera while Show Grid is on. The choice is kept."), QStringLiteral("grid"), true,
		[this]() {
			LevelCameraShadingState state = m_levelCameraShading;
			state.faceGrid = !state.faceGrid;
			const bool showGrid = !m_levelMapShowGrid || m_levelMapShowGrid->isChecked();
			setLevelCameraShading(state, levelCameraFaceGridSummary(state, showGrid, levelMapGridUnits()));
		});
	add(QStringLiteral("map.previewFarClip"), tr("Far Clip in Camera"),
		tr("Draw nothing beyond the camera's draw distance, to speed up and declutter dense maps, as NetRadiant Custom's cubic clip does. The choice is kept."),
		QStringLiteral("cube"), true, [this]() {
			LevelCameraFarClipState state = m_levelCameraFarClip;
			state.enabled = !state.enabled;
			setLevelCameraFarClip(state, levelCameraFarClipSummary(state, m_levelCameraShading));
		});
	add(QStringLiteral("map.shorterDrawDistance"), tr("Shorter Draw Distance"),
		tr("Bring Far Clip in Camera's draw distance one step nearer, down to %1. The distance is kept.")
			.arg(levelCameraFarClipDistanceText(kLevelCameraFarClipMinimumStep)),
		QStringLiteral("cube"), false, [this]() { stepLevelCameraFarClip(-1); });
	add(QStringLiteral("map.longerDrawDistance"), tr("Longer Draw Distance"),
		tr("Push Far Clip in Camera's draw distance one step farther, up to %1. The distance is kept.")
			.arg(levelCameraFarClipDistanceText(kLevelCameraFarClipMaximumStep)),
		QStringLiteral("cube"), false, [this]() { stepLevelCameraFarClip(1); });
	refreshLevelCameraRenderCommands();
}

void ApplicationShell::refreshLevelCameraRenderCommands()
{
	if (!m_commands) {
		return;
	}
	// The saved choices can change at any time; what changes the picture at
	// once waits for the camera to be showing, as 3D Wireframe always has.
	const bool showing = m_levelMapDocument.format != LevelMapFormat::Unknown && m_levelMap3D && levelMap3DShowing();
	for (const QString& id : {QStringLiteral("map.previewWireframe"), QStringLiteral("map.showCameraTextures"), QStringLiteral("map.hideCameraTextures"),
			 QStringLiteral("map.hideCameraFaces"), QStringLiteral("map.nextCameraMode"), QStringLiteral("map.previousCameraMode")}) {
		m_commands->setEnabled(id, showing);
	}
	for (const QString& id : {QStringLiteral("map.previewTextures"), QStringLiteral("map.previewEdges"), QStringLiteral("map.previewShading"),
			 QStringLiteral("map.previewFog"), QStringLiteral("map.previewFaceGrid"), QStringLiteral("map.previewFarClip"),
			 QStringLiteral("map.shorterDrawDistance"), QStringLiteral("map.longerDrawDistance")}) {
		m_commands->setEnabled(id, true);
	}
	const std::pair<const char*, bool> checks[] = {
		{"map.previewTextures", m_levelCameraRender.textures},
		{"map.previewEdges", m_levelCameraRender.edges},
		{"map.previewWireframe", m_levelCameraRender.facesHidden},
		{"map.previewShading", m_levelCameraShading.shadeFaces},
		{"map.previewFog", m_levelCameraShading.fog},
		{"map.previewFaceGrid", m_levelCameraShading.faceGrid},
		{"map.previewFarClip", m_levelCameraFarClip.enabled},
		// Show Grid is checkable, as TrenchBroom's is; the Show menu holds the state.
		{"map.toggleGrid", !m_levelMapShowGrid || m_levelMapShowGrid->isChecked()},
	};
	for (const auto& [id, checked] : checks) {
		if (QAction* action = m_commands->action(QString::fromLatin1(id)); action && action->isChecked() != checked) {
			action->setChecked(checked);
		}
	}
}

void ApplicationShell::applyLevelCameraRender()
{
	if (!m_levelMap3D) {
		return;
	}
	// Brush faces and patch quads are polygon fans in the preview mesh, so
	// their outlines leave the triangle diagonals out.
	m_levelMap3D->setPolygonEdges(true);
	m_levelMap3D->setShowEdges(levelCameraDrawsEdgeOverlay(m_levelCameraRender));
	switch (levelCameraFaceMode(m_levelCameraRender)) {
	case LevelCameraFaceMode::Hidden:
		m_levelMap3D->setRenderMode(ModelViewportRenderMode::Wireframe);
		break;
	case LevelCameraFaceMode::Flat:
		m_levelMap3D->setRenderMode(ModelViewportRenderMode::FlatShaded);
		break;
	case LevelCameraFaceMode::Textured:
		m_levelMap3D->setRenderMode(ModelViewportRenderMode::Textured);
		break;
	}
	applyLevelCameraShading();
}

void ApplicationShell::applyLevelCameraShading()
{
	if (!m_levelMap3D) {
		return;
	}
	m_levelMap3D->setFaceShading(m_levelCameraShading.shadeFaces);
	// With the far clip on too, the fog ends no farther than the clip.
	const LevelCameraShadingState fog = levelCameraFogWithinFarClip(m_levelCameraShading, m_levelCameraFarClip);
	m_levelMap3D->setDistanceFog(fog.fog, fog.fogStart, fog.fogEnd, kLevelCameraFogStrength);
	m_levelMap3D->setFarClip(levelCameraFarClipPlane(m_levelCameraFarClip));
	// The ground grid and the face grid follow Show Grid; the face grid also
	// follows the grid size.
	const bool showGrid = !m_levelMapShowGrid || m_levelMapShowGrid->isChecked();
	m_levelMap3D->setShowGrid(showGrid);
	m_levelMap3D->setFaceGrid(levelCameraFaceGridSpacing(m_levelCameraShading, showGrid, levelMapGridUnits()));
}

void ApplicationShell::setLevelCameraRender(const LevelCameraRenderState& state, const QString& message)
{
	m_levelCameraRender = state;
	m_settings.setShellLayoutState(kTexturedKey, onOff(state.textures));
	m_settings.setShellLayoutState(kWireframeKey, onOff(state.facesHidden));
	m_settings.setShellLayoutState(kEdgesKey, onOff(state.edges));
	applyLevelCameraRender();
	refreshLevelCameraRenderCommands();
	if (!message.isEmpty()) {
		statusBar()->showMessage(message, 5000);
	}
}

void ApplicationShell::stepLevelCameraDrawMode(int steps)
{
	const LevelCameraDrawMode mode = levelCameraSteppedDrawMode(levelCameraDrawMode(m_levelCameraRender), steps);
	setLevelCameraRender(levelCameraWithDrawMode(m_levelCameraRender, mode), levelCameraDrawModeAnnouncement(mode));
}

void ApplicationShell::setLevelCameraShading(const LevelCameraShadingState& state, const QString& message)
{
	m_levelCameraShading = state;
	m_settings.setShellLayoutState(kShadingKey, onOff(state.shadeFaces));
	m_settings.setShellLayoutState(kFogKey, onOff(state.fog));
	m_settings.setShellLayoutState(kFogRangeKey, levelCameraFogRangeText(state));
	m_settings.setShellLayoutState(kFaceGridKey, onOff(state.faceGrid));
	applyLevelCameraShading();
	refreshLevelCameraRenderCommands();
	if (!message.isEmpty()) {
		statusBar()->showMessage(message, 5000);
	}
}

void ApplicationShell::setLevelCameraFarClip(const LevelCameraFarClipState& state, const QString& message)
{
	m_levelCameraFarClip = state;
	// The CLI reads and writes the same key (editor far-clip).
	m_settings.setShellLayoutState(levelCameraFarClipSettingsKey(), levelCameraFarClipText(state));
	applyLevelCameraShading();
	refreshLevelCameraRenderCommands();
	if (!message.isEmpty()) {
		statusBar()->showMessage(message, 5000);
	}
}

void ApplicationShell::stepLevelCameraFarClip(int steps)
{
	const LevelCameraFarClipState before = m_levelCameraFarClip;
	const LevelCameraFarClipState after = levelCameraWithFarClipStep(before, before.step + steps);
	setLevelCameraFarClip(after, levelCameraFarClipStepAnnouncement(before, after));
}

QWidget* ApplicationShell::buildLevelCameraFarClipRange()
{
	// The draw distance, shown beneath Far Clip in Camera while it is on.
	auto* row = new QWidget;
	row->setObjectName(QStringLiteral("levelViewFarClipRange"));
	auto* form = new QFormLayout(row);
	form->setContentsMargins(0, 0, 0, 0);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	auto* distance = new QComboBox;
	distance->setObjectName(QStringLiteral("levelViewFarClipDistance"));
	distance->setAccessibleName(tr("Camera draw distance"));
	distance->setToolTip(tr("How far the camera draws while Far Clip in Camera is on. Shorter and Longer Draw Distance step through the same list."));
	for (int step = kLevelCameraFarClipMinimumStep; step <= kLevelCameraFarClipMaximumStep; ++step) {
		distance->addItem(levelCameraFarClipDistanceText(step), step);
	}
	const auto value = [this, distance]() {
		const QSignalBlocker blocker(distance);
		distance->setCurrentIndex(distance->findData(m_levelCameraFarClip.step));
	};
	const auto sync = [this, row, value]() {
		value();
		row->setVisible(m_levelCameraFarClip.enabled);
	};
	// As with the fog distances, the row has no parent yet, so it is only
	// ever hidden here.
	value();
	if (!m_levelCameraFarClip.enabled) {
		row->hide();
	}
	connect(distance, &QComboBox::currentIndexChanged, row, [this, distance](int index) {
		const LevelCameraFarClipState before = m_levelCameraFarClip;
		const LevelCameraFarClipState after = levelCameraWithFarClipStep(before, distance->itemData(index).toInt());
		if (after != before) {
			setLevelCameraFarClip(after, levelCameraFarClipStepAnnouncement(before, after));
		}
	});
	for (const QString& id : {QStringLiteral("map.previewFarClip"), QStringLiteral("map.shorterDrawDistance"), QStringLiteral("map.longerDrawDistance")}) {
		if (QAction* command = m_commands ? m_commands->action(id) : nullptr) {
			// Each command's own handler runs first, so the row shows its result.
			connect(command, &QAction::triggered, row, sync);
			connect(command, &QAction::toggled, row, sync);
		}
	}
	auto* label = new QLabel(tr("Draw distance"));
	label->setObjectName(QStringLiteral("sidebarFieldLabel"));
	label->setBuddy(distance);
	form->addRow(label, distance);
	return row;
}

QWidget* ApplicationShell::buildLevelCameraFogRange()
{
	// The fog's start and end, shown beneath Fog in Camera while it is on.
	auto* row = new QWidget;
	row->setObjectName(QStringLiteral("levelViewFogRange"));
	auto* form = new QFormLayout(row);
	form->setContentsMargins(0, 0, 0, 0);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	const auto distance = [](const QString& name, const QString& accessibleName, const QString& tip, int low, int high) {
		auto* spin = new QSpinBox;
		spin->setObjectName(name);
		spin->setRange(low, high);
		spin->setSingleStep(64);
		spin->setSuffix(tr(" units"));
		// Typing commits on Enter or leaving the box, not on every digit.
		spin->setKeyboardTracking(false);
		spin->setAccessibleName(accessibleName);
		spin->setToolTip(tip);
		return spin;
	};
	QSpinBox* start = distance(QStringLiteral("levelViewFogStart"), tr("Fog start distance"),
		tr("How far from the camera faces start to fade towards the background."), 0, kLevelCameraFogMaximumDistance - kLevelCameraFogMinimumGap);
	QSpinBox* end = distance(QStringLiteral("levelViewFogEnd"), tr("Fog end distance"),
		tr("How far from the camera the fog is at its thickest; beyond it faces keep a fifth of their colour."), kLevelCameraFogMinimumGap,
		kLevelCameraFogMaximumDistance);
	const auto values = [this, start, end]() {
		const QSignalBlocker startBlocker(start);
		const QSignalBlocker endBlocker(end);
		start->setValue(m_levelCameraShading.fogStart);
		end->setValue(m_levelCameraShading.fogEnd);
	};
	const auto sync = [this, row, values]() {
		values();
		row->setVisible(m_levelCameraShading.fog);
	};
	// The row has no parent yet, so it is only ever hidden here: showing it
	// now would open it as a window of its own.
	values();
	if (!m_levelCameraShading.fog) {
		row->hide();
	}
	const auto changed = [this, start, end, sync](bool keepEnd) {
		const LevelCameraShadingState state = levelCameraWithFogRange(m_levelCameraShading, start->value(), end->value(), keepEnd);
		if (state != m_levelCameraShading) {
			setLevelCameraShading(state, levelCameraFogSummary(state));
		}
		sync();
	};
	connect(start, &QSpinBox::valueChanged, row, [changed]() { changed(false); });
	connect(end, &QSpinBox::valueChanged, row, [changed]() { changed(true); });
	if (QAction* fog = m_commands ? m_commands->action(QStringLiteral("map.previewFog")) : nullptr) {
		// A trigger toggles the check before the command changes the state, so
		// the row also follows the trigger, which comes after.
		connect(fog, &QAction::toggled, row, sync);
		connect(fog, &QAction::triggered, row, sync);
	}
	auto* startLabel = new QLabel(tr("Fog start"));
	startLabel->setObjectName(QStringLiteral("sidebarFieldLabel"));
	startLabel->setBuddy(start);
	auto* endLabel = new QLabel(tr("Fog end"));
	endLabel->setObjectName(QStringLiteral("sidebarFieldLabel"));
	endLabel->setBuddy(end);
	form->addRow(startLabel, start);
	form->addRow(endLabel, end);
	return row;
}

} // namespace vibestudio
