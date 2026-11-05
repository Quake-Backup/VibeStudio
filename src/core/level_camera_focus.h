#pragma once

// The level camera's levelling, focusing and floor stepping, shared by the
// Levels View > Camera commands, the held free-focus key and
// `map camera-focus`.
//
// Level Camera keeps the camera where it stands, drops its pitch to zero and
// turns its heading to the nearest 22.5-degree step, as Radiant's Center
// command (End) does. Focus Camera on Selection keeps the camera's heading
// and pitch and moves it along that view until every corner of the
// selection's box is inside the view, at least 64 units from the box's
// centre; the plan views stay where they are. While the free-focus key is
// held during mouse look, each wheel notch scales that distance by a
// constant step, nearer or farther.
//
// Workflow reference: NRC camwindow.cpp GlobalCamera_ResetAngles,
// Camera_getFocusPos and GlobalCamera_FocusOnSelected at 449778b3
// (GPL-2.0-or-later); Q3Radiant MainFrm.cpp OnViewCenter at dbe4ddb1
// (GPL-2.0-or-later). Original Qt implementation with its own frustum fit and
// offset curve; see docs/CREDITS.md.

#include "core/box_resize.h"
#include "core/level_map.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

namespace vibestudio {

// The closest Focus Camera on Selection places the camera to the box's
// centre before any wheel offset, in map units.
inline constexpr double kLevelCameraFocusMinimumDistance = 64.0;
// The box keeps at least this far in front of the camera, in map units, so
// no corner reaches the camera's near plane.
inline constexpr double kLevelCameraFocusNearClearance = 8.0;
// Wheel notches the free focus accepts each way; eight notches halve or
// double the distance.
inline constexpr int kLevelCameraFocusOffsetLimit = 48;

// The nearest 22.5-degree heading to `yawDegrees`, from 0 up to but not
// including 360; NaN when the input is not finite.
[[nodiscard]] double levelCameraLevelledYaw(double yawDegrees);

// Scale a wheel offset applies to the fitted distance: 2^(-steps/8), with
// `steps` clamped to the offset limit. Positive steps come nearer.
[[nodiscard]] double levelCameraFocusOffsetScale(int offsetSteps);

struct LevelCameraFocusRequest {
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	// Where the camera looks: yaw 0 along +X, counter-clockwise; pitch up
	// positive, both in degrees.
	double yawDegrees = 0.0;
	double pitchDegrees = 0.0;
	// The camera's vertical field of view in degrees and its width / height.
	double fieldOfViewDegrees = 90.0;
	double aspect = 4.0 / 3.0;
	// Wheel notches while the free-focus key is held; 0 fits exactly.
	int offsetSteps = 0;
};

struct LevelCameraFocus {
	bool valid = false;
	QString error;
	// Where the camera goes; it keeps the request's yaw and pitch.
	LevelMapVec3 position;
	LevelMapVec3 centre;
	// Distance from the box centre that fits the box (never below the
	// minimum), and the distance after the wheel offset.
	double fitDistance = 0.0;
	double distance = 0.0;
};

// The camera position that fits the box in the view. Fails, with a
// translated reason, for an invalid box, angles, field of view or aspect.
[[nodiscard]] LevelCameraFocus levelCameraFocusPosition(const LevelCameraFocusRequest& request);

// The box the focus fits around `objects`: those that still exist and are not
// hidden. False when none of them can be measured.
bool levelCameraFocusObjectsBounds(const LevelMapDocument& document, const QVector<LevelMapSelectionRef>& objects,
	LevelMapVec3* mins, LevelMapVec3* maxs);

// Camera Floor Up and Camera Floor Down: the next height the camera can stand
// at above or below where it is, keeping its x and y.
//
// On Quake-family maps every visible brush, patch and point entity that is
// selected, or whose box spans the camera's x and y (strictly inside), offers
// a standing height 32 units above its top and an underside 16 units below its
// bottom. Brush entities and worldspawn count through their brushes and
// patches; a point entity is its origin plus its definition box (from
// `entityBounds`, by case-folded class name), or a 16-unit cube without one.
// Up takes the nearest standing height more than 0.1 unit above the camera,
// unless the camera is below the lowest underside, which it then goes to
// first; Down takes the nearest standing height more than 0.1 unit below the
// camera, or else the lowest underside when that is lower.
//
// On Doom maps the visible sectors under the camera, and the selected ones,
// offer their floor plus Doom's 41-unit eye height, kept at least 4 units
// below the ceiling as the game does (never below the floor). There is no
// underside: Up and Down take the nearest such height above or below.
//
// Workflow reference: NRC camwindow.cpp FloorHeightWalker and
// CamWnd::Cam_ChangeFloor at 449778b3 (GPL-2.0-or-later); id Software DOOM
// linuxdoom-1.10 p_local.h VIEWHEIGHT and p_user.c P_CalcHeight at a77dfb96
// (GPL-2.0-or-later) for the eye height and ceiling gap. Original Qt
// implementation; see docs/CREDITS.md.
inline constexpr double kLevelCameraFloorStandHeight = 32.0;
inline constexpr double kLevelCameraFloorUndersideClearance = 16.0;
inline constexpr double kLevelCameraFloorEpsilon = 0.1;
inline constexpr double kLevelCameraDoomEyeHeight = 41.0;
inline constexpr double kLevelCameraDoomCeilingGap = 4.0;

enum class LevelCameraFloorDirection { Up, Down };

// What gave the camera its new height.
enum class LevelCameraFloorSource { None, StandingHeight, Underside, SectorEyeHeight };

struct LevelCameraFloorRequest {
	// Where the camera stands now.
	LevelMapVec3 position;
	LevelCameraFloorDirection direction = LevelCameraFloorDirection::Up;
	// Objects the views hide besides the map's own hidden ones (view filters,
	// a region), as selectors such as "brush:4"; they never count.
	QSet<QString> excludedObjects;
	// Point entity definition boxes by case-folded class name, relative to the
	// origin, as for paste (core/level_paste.h).
	QHash<QString, ResizeBox> entityBounds;
};

struct LevelCameraFloorStep {
	// False, with a translated reason, for an unusable request.
	bool valid = false;
	QString error;
	// True when the camera changes height; `position` is where it goes (the
	// request's position when it stays).
	bool moved = false;
	LevelMapVec3 position;
	LevelCameraFloorSource source = LevelCameraFloorSource::None;
	// The object whose top, underside or sector floor gave the height.
	LevelMapSelectionRef object;
	// How many visible objects under the camera or selected were measured.
	int candidates = 0;
};

[[nodiscard]] LevelCameraFloorStep levelCameraFloorStep(const LevelMapDocument& document, const LevelCameraFloorRequest& request);

// "up" or "down", and back; false for anything else.
[[nodiscard]] QString levelCameraFloorDirectionId(LevelCameraFloorDirection direction);
bool levelCameraFloorDirectionFromId(const QString& id, LevelCameraFloorDirection* direction);
// "standing-height", "underside", "sector-eye-height" or "none".
[[nodiscard]] QString levelCameraFloorSourceId(LevelCameraFloorSource source);

} // namespace vibestudio
