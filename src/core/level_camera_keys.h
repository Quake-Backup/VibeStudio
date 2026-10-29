#pragma once
#include "core/level_editor_controls.h"
#include <QHash>
#include <QPointF>
#include <QVector>

namespace vibestudio {
struct EditorProfileDescriptor;
struct CameraKeyMotion {
	double forward = 0.0;
	double right = 0.0;
	double up = 0.0;
	double turn = 0.0;
	double pitch = 0.0;
};

// Position steering in normalized view coordinates (-1..1; Y points down).
// Returns forward/turn rates in units/degrees per second, on the ground plane.
CameraKeyMotion cameraPointerDriveMotion(const QPointF& normalizedPosition);

// Portable keys shared by preferences, diagnostics and the viewport. "none"
// disables a binding; direction keys are unmodified, toggles may use modifiers.
bool normalizeCameraKey(const QString& input, bool toggle, QString* normalized, QString* error = nullptr);
// The held free-focus key: one unmodified key as above, except that Tab is
// allowed, since the key only acts while mouse look holds the pointer.
bool normalizeCameraFocusKey(const QString& input, QString* normalized, QString* error = nullptr);
// True when `key` is the profile's free-focus key, pressed without modifiers,
// on a perspective camera.
bool cameraFocusKeyMatches(const CameraViewControls& controls, int key, Qt::KeyboardModifiers modifiers);
QStringList cameraKeyProblems(const CameraViewControls& controls);
QHash<int, CameraKeyMotion> cameraKeyMotions(const CameraViewControls& controls, bool looking,
	Qt::MouseButton pressedButton = Qt::NoButton, Qt::KeyboardModifiers pressModifiers = Qt::NoModifier,
	Qt::KeyboardModifiers modifiers = Qt::NoModifier);
// Fixed steps apply only to drive keys while idle. An active fly binding
// keeps its continuous motion when both sets use the same key.
bool cameraKeyUsesFixedStep(const CameraViewControls& controls, int key, bool looking,
	Qt::MouseButton pressedButton = Qt::NoButton, Qt::KeyboardModifiers pressModifiers = Qt::NoModifier,
	Qt::KeyboardModifiers modifiers = Qt::NoModifier);
bool cameraMotionModifiersAccepted(const CameraViewControls& controls, Qt::KeyboardModifiers modifiers,
	Qt::MouseButton pressedButton = Qt::NoButton, Qt::KeyboardModifiers pressModifiers = Qt::NoModifier, bool looking = false);
bool cameraLookToggleMatches(const CameraViewControls& controls, int key, Qt::KeyboardModifiers modifiers);
bool navigationHoldKeyMatches(const QString& binding, int key, Qt::KeyboardModifiers modifiers);
// Reports camera and temporary-plan-navigation overlaps with command bindings.
// Existing built-in navigation overlaps are omitted unless the command was
// explicitly rebound by the user. Command registration/scopes stay unchanged.
// A camera drive key a 2D view passes on (planRoutedCameraDriveKeys()) is
// reported the other way round: there the command keeps the key while it can
// act.
QStringList levelCameraShortcutWarnings(const EditorProfileDescriptor& profile, const LevelEditorControls& controls,
	const QHash<QString, QStringList>& userShortcuts = {});

// The motion a key pressed in a 2D view drives the camera by: one of
// planRoutedCameraDriveKeys(), pressed without modifiers (the keypad flag
// aside). Whether a command takes the key first is the shell's to decide.
bool levelPlanCameraDriveKey(const LevelEditorControls& controls, int key, Qt::KeyboardModifiers modifiers,
	CameraKeyMotion* motion = nullptr);

// The saved camera speeds, in units per second, that held camera keys fly and
// drive at: NetRadiant Custom's CamMoveSpeed steps, 50, then 250 to 9750 in
// steps of 250, then 9999 (camwindow.cpp CAM_MIN_SPEED, CAM_SPEED_STEP and
// CAM_MAX_SPEED at 449778b3). CameraViewControls::moveSpeed 0 instead scales
// the speed with the map.
inline constexpr int kLevelCameraMinMoveSpeed = 50;
inline constexpr int kLevelCameraMaxMoveSpeed = 9999;
inline constexpr int kLevelCameraMoveSpeedStep = 250;
[[nodiscard]] const QVector<int>& levelCameraMoveSpeeds();
// The listed speed one step faster (direction > 0) or slower than `speed`,
// or than `automaticSpeed`, the map-scaled speed in use, when `speed` is 0.
// Returns `speed` unchanged when no listed speed lies that way.
[[nodiscard]] int levelCameraSteppedMoveSpeed(int speed, int direction, double automaticSpeed);
} // namespace vibestudio
