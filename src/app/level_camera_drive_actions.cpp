// View > Camera > Increase Camera Speed and Decrease Camera Speed, the camera
// speed readout beside Textures above the views, and the camera's drive keys
// passed on from the 2D views.
//
// The speed is the editor profile's camera.moveSpeed gesture, so the commands,
// Customize Gestures… and `editor gestures --set camera.moveSpeed=N` share
// one saved value per profile (core/level_camera_keys.h levelCameraMoveSpeeds).
// A 2D view hands a drive key to the camera, as Radiant's global camera keys
// reach it from any view, only while the view does not use the key itself and
// no command the key reaches there can act; the command keeps it otherwise.
// Neither changes the map, its selection or its undo history.
//
// Workflow reference: NRC camwindow.cpp CameraSpeed_increase,
// CameraSpeed_decrease, CamWnd_Move_Discrete_Enable and CamWnd_Move_Enable at
// 449778b3 (GPL-2.0-or-later). Original Qt implementation; see
// docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/level_component_gizmo.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/editor_profiles.h"
#include "core/level_camera_keys.h"

#include <QAction>
#include <QEvent>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLocale>
#include <QStatusBar>

#include <cmath>
#include <functional>
#include <utility>

namespace vibestudio {

namespace {

// Whether a command whose key this is reaches `plan`: a window-wide command,
// or one scoped to the plan view or a widget holding it. With `enabledOnly`
// only a command that can act now counts.
bool planCommandTakesKey(const StudioCommandRegistry& commands, const MapViewport* plan, const QKeyEvent* key, bool enabledOnly = true)
{
	QStringList texts;
	const auto plain = QKeyCombination(key->modifiers() & ~Qt::KeypadModifier, static_cast<Qt::Key>(key->key()));
	texts << QKeySequence(plain).toString(QKeySequence::PortableText);
	if (key->modifiers().testFlag(Qt::KeypadModifier)) {
		texts << QKeySequence(QKeyCombination(key->modifiers(), static_cast<Qt::Key>(key->key()))).toString(QKeySequence::PortableText);
	}
	for (const QString& text : std::as_const(texts)) {
		for (const QString& id : commands.commandsUsingShortcut(text, QString())) {
			QAction* candidate = commands.action(id);
			if (!candidate || (enabledOnly && !candidate->isEnabled())) {
				continue;
			}
			bool reaches = candidate->shortcutContext() == Qt::WindowShortcut || candidate->shortcutContext() == Qt::ApplicationShortcut;
			for (const QWidget* scope = plan; scope && !reaches; scope = scope->parentWidget()) {
				reaches = scope->actions().contains(candidate);
			}
			if (reaches) {
				return true;
			}
		}
	}
	return false;
}

} // namespace

void ApplicationShell::registerLevelCameraDriveCommands()
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
		command.menuSection = tr("Camera");
		command.handler = std::move(handler);
		m_commands->registerCommand(command);
	};
	add(QStringLiteral("map.cameraSpeedUp"), tr("Increase Camera Speed"),
		tr("Make the 3D camera's held keys fly and drive one step faster, up to 9999 units per second. The speed is kept with your editor profile's gestures."),
		QStringLiteral("plus"), [this]() { stepLevelCameraSpeed(1); });
	add(QStringLiteral("map.cameraSpeedDown"), tr("Decrease Camera Speed"),
		tr("Make the 3D camera's held keys fly and drive one step slower, down to 50 units per second. The speed is kept with your editor profile's gestures."),
		QStringLiteral("minus"), [this]() { stepLevelCameraSpeed(-1); });
}

void ApplicationShell::refreshLevelCameraDriveCommands()
{
	if (!m_commands) {
		return;
	}
	const bool firstPerson = m_levelMap3D && m_levelMap3D->isPerspective();
	const int speed = m_levelControls.camera.moveSpeed;
	const double current = firstPerson ? m_levelMap3D->cameraMoveSpeed() : 0.0;
	// Each command greys out at its end of the list, as Smaller and Larger
	// Grid do; with the map-scaled speed both directions are open.
	m_commands->setEnabled(QStringLiteral("map.cameraSpeedUp"), firstPerson && levelCameraSteppedMoveSpeed(speed, 1, current) != speed);
	m_commands->setEnabled(QStringLiteral("map.cameraSpeedDown"), firstPerson && levelCameraSteppedMoveSpeed(speed, -1, current) != speed);
	if (!m_levelCameraSpeedLabel) {
		return;
	}
	if (m_levelCameraSpeedLabel->isHidden() == firstPerson) {
		m_levelCameraSpeedLabel->setVisible(firstPerson);
	}
	if (!firstPerson) {
		return;
	}
	const int units = static_cast<int>(std::lround(current));
	const bool automatic = speed <= 0;
	const QString text = automatic ? tr("Speed: %1 (map)").arg(QLocale().toString(units)) : tr("Speed: %1").arg(QLocale().toString(units));
	if (m_levelCameraSpeedLabel->text() == text) {
		return;
	}
	m_levelCameraSpeedLabel->setText(text);
	const QString help = automatic
		? tr("Held camera keys fly and drive at %n unit(s) per second, scaled with the size of the map. Increase Camera Speed and Decrease Camera Speed choose a fixed speed.", nullptr, units)
		: tr("Held camera keys fly and drive at %n unit(s) per second. Increase Camera Speed and Decrease Camera Speed step it; it is kept with your editor profile's gestures.", nullptr, units);
	m_levelCameraSpeedLabel->setToolTip(help);
	m_levelCameraSpeedLabel->setAccessibleDescription(help);
}

void ApplicationShell::stepLevelCameraSpeed(int direction)
{
	if (!m_levelMap3D || !m_levelMap3D->isPerspective()) {
		statusBar()->showMessage(tr("Camera speed needs a first-person 3D camera; choose an editor profile with one, such as NetRadiant Custom or TrenchBroom."), 5000);
		return;
	}
	const int speed = m_levelControls.camera.moveSpeed;
	const int next = levelCameraSteppedMoveSpeed(speed, direction, m_levelMap3D->cameraMoveSpeed());
	if (next == speed) {
		const int units = static_cast<int>(std::lround(m_levelMap3D->cameraMoveSpeed()));
		statusBar()->showMessage(direction > 0 ? tr("The camera is already at its fastest, %n unit(s) per second.", nullptr, units)
											   : tr("The camera is already at its slowest, %n unit(s) per second.", nullptr, units), 4000);
		refreshLevelCameraDriveCommands();
		return;
	}
	m_levelControls.camera.moveSpeed = next;
	m_levelMap3D->setCameraMoveSpeed(next);
	// The camera's spoken controls name the speed, so a screen reader hears
	// the new one at once.
	m_levelMap3D->setControlsHelp(levelCameraControlsHelp());
	// Kept with the profile's other gestures, as NetRadiant Custom keeps
	// CamMoveSpeed; the profile's own speed leaves no override behind.
	EditorProfileDescriptor profile;
	if (!editorProfileForId(m_settings.selectedEditorProfileId(), &profile)) {
		editorProfileForId(defaultEditorProfileId(), &profile);
	}
	QString error;
	QJsonObject overrides = m_settings.editorGestureOverrides(profile.id, &error);
	bool saved = false;
	if (error.isEmpty()) {
		overrides.insert(QStringLiteral("camera.moveSpeed"), QString::number(next));
		saved = m_settings.setEditorGestureOverrides(profile.id, overrides, &error);
	}
	refreshLevelCameraDriveCommands();
	statusBar()->showMessage(saved ? tr("Camera speed: %n unit(s) per second.", nullptr, next)
								   : tr("Camera speed: %n unit(s) per second for this session; it could not be saved: %1", nullptr, next).arg(error), 4000);
}

bool ApplicationShell::handleLevelCameraDriveKey(QObject* watched, QEvent* event)
{
	const QEvent::Type type = event->type();
	if ((type != QEvent::ShortcutOverride && type != QEvent::KeyPress && type != QEvent::KeyRelease && type != QEvent::FocusOut) || !m_levelMap3D
		|| !m_commands) {
		return false;
	}
	MapViewport* plan = nullptr;
	for (auto* view : std::as_const(m_levelPlanViews)) {
		if (view == watched) {
			plan = view;
			break;
		}
	}
	if (!plan) {
		return false;
	}
	// A key let go elsewhere never comes back here, so nothing stays held.
	if (type == QEvent::FocusOut) {
		m_levelMap3D->releaseCameraDriveKeys();
		return false;
	}
	auto* key = static_cast<QKeyEvent*>(event);
	if (type == QEvent::KeyRelease) {
		if (!key->isAutoRepeat()) {
			m_levelMap3D->releaseCameraDriveKey(key->key());
		}
		return false;
	}
	// During mouse look the camera flies with its own keys, so a 2D view's
	// key is left to the view rather than claimed and dropped.
	if (!m_levelMap3D->isPerspective() || m_levelMap3D->isLooking() || !levelPlanCameraDriveKey(m_levelControls, key->key(), key->modifiers())) {
		return false;
	}
	// Availability is read fresh, so a command that a selection change has
	// just made able to act keeps its key (the prism keys refresh lazily).
	if (type == QEvent::ShortcutOverride && planCommandTakesKey(*m_commands, plan, key, false)) {
		refreshCommandEnablement();
	}
	// The view's own drags come first, and so does a command that can act
	// here: Draw Sector keeps D on a Doom map, a key the user gave a command
	// keeps it everywhere it reaches.
	if (plan->keepsKey(key->key(), key->modifiers()) || (plan->objectGizmo() && plan->objectGizmo()->active())
		|| planCommandTakesKey(*m_commands, plan, key)) {
		return false;
	}
	// Claiming the key keeps a disabled command's shortcut from swallowing it.
	event->accept();
	if (type == QEvent::ShortcutOverride) {
		return true;
	}
	if (!m_levelMap3D->driveCameraFromKey(key->key(), key->modifiers(), key->isAutoRepeat())) {
		// The camera could not take it after all: the view gets the key.
		event->ignore();
		return false;
	}
	return true;
}

} // namespace vibestudio
