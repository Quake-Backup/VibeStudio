#pragma once

#include "core/level_editor_controls.h"
#include <QByteArray>
#include <QJsonObject>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QVector>
#include <array>

namespace vibestudio {

// Navigation is editor metadata: it never changes geometry, selection or undo.
struct PlanViewState {
	int projection = 0; // Top, front, side, in the same order as MapViewportProjection.
	QPointF center;
	double zoom = 1.0;
};

// Shared by the Levels and Models cameras. Store the orbit target in world
// space rather than widget pixels so resizing or rebuilding bounds is safe.
struct CameraViewState {
	bool perspective = false;
	std::array<double, 3> position {};
	double yaw = 0.0;
	double pitch = 0.0;
	double fieldOfView = 90.0;
	double focusDistance = 256.0;
	std::array<double, 3> orbitTarget {};
	double orbitYaw = 30.0;
	double orbitPitch = 30.0;
	double orbitScale = 1.0;
};

struct LevelViewLinks {
	bool centers = false;
	bool zoom = false;
	bool followCamera = false;
	bool operator==(const LevelViewLinks&) const = default;
};

struct LevelViewState {
	LevelViewLayout layout = LevelViewLayout::Single2D;
	int activePlan = 0;
	bool cameraVisible = false;
	std::array<PlanViewState, 3> plans {{{0, {}, 1.0}, {1, {}, 1.0}, {2, {}, 1.0}}};
	CameraViewState camera;
	LevelViewLinks links;
};

// MapViewport's historical SideZY identifier displays Y horizontally and Z vertically.
// A source pane supplies its two axes; the first other pane containing the
// remaining axis supplies its depth. Repeated projections are supported.
// These operations validate the entire candidate and leave invalid inputs intact.
bool linkLevelPlanNavigation(std::array<PlanViewState, 3>* plans, int source, const LevelViewLinks& links, QString* error = nullptr);
bool centerLevelPlanNavigation(std::array<PlanViewState, 3>* plans, const std::array<double, 3>& point, QString* error = nullptr);
// The centre a plan of `projection` takes to show `point` in its middle.
[[nodiscard]] QPointF levelPlanCentreFor(int projection, const std::array<double, 3>& point);
// TrenchBroom's Reset 2D Cameras: every other plan takes the source plan's
// zoom, and its centre on the axes the two show; an axis the source does not
// show keeps the plan's own value, so unlike linkLevelPlanNavigation no value
// comes from a third pane. Validates everything first; atomic.
//
// Workflow reference: TB MapView2D::reset2dCameras at 4c756be1
// (GPL-3.0-or-later). Original Qt implementation; see docs/CREDITS.md.
bool resetLevelPlanNavigation(std::array<PlanViewState, 3>* plans, int source, QString* error = nullptr);

// Radiant's plan focus: the box's centre in the middle of a pane of
// `viewSize` (widget pixels), at the scale that shows three times the box's
// larger half-extent on the pane's axes, never less than 128 units, so the
// box fills two-thirds of the pane and the pane spans at least 384 units.
// The zoom is clamped to the plan views' range. With nothing selected
// Radiant frames a box of kLevelPlanFocusCameraHalfExtent round the camera.
//
// Workflow reference: NRC XYWnd::FocusOnBounds, XY_Focus and GetCenterBbox
// at 449778b3 (GPL-2.0-or-later). Original Qt implementation; see
// docs/CREDITS.md.
inline constexpr double kLevelPlanFocusMinimumHalfExtent = 128.0;
inline constexpr double kLevelPlanFocusCameraHalfExtent = 128.0;
bool levelPlanFocusState(int projection, const std::array<double, 3>& mins, const std::array<double, 3>& maxs, const QSizeF& viewSize,
	PlanViewState* state, QString* error = nullptr);

struct LevelViewBookmark {
	QString id;
	QString name;
	LevelViewState view;
};
using LevelViewBookmarks = QVector<LevelViewBookmark>;

inline constexpr int LevelBookmarkLimit = 128;
inline constexpr qint64 LevelBookmarkBytesLimit = 1024 * 1024;

bool validatePlanViewState(const PlanViewState& state, QString* error = nullptr);
bool validateCameraViewState(const CameraViewState& state, QString* error = nullptr);
bool validateLevelViewState(const LevelViewState& state, QString* error = nullptr);
bool validateLevelBookmarks(const LevelViewBookmarks& bookmarks, QString* error = nullptr);
QJsonObject levelBookmarksJson(const LevelViewBookmarks& bookmarks);
bool parseLevelBookmarks(const QByteArray& bytes, LevelViewBookmarks* bookmarks, QString* error = nullptr);

// Empty source paths have no durable identity. WAD callers include the map name;
// text maps pass an empty name so a title change does not lose their views.
QString levelBookmarkStorePath(const QString& sourcePath, const QString& wadMapName = {});

// Missing files read as an empty collection with an empty revision. Revisions
// hash the exact bytes. Writes are bounded, locked, atomic, and compare the
// caller's revision before replacing anything. Outputs change only on success.
bool readLevelBookmarks(const QString& path, LevelViewBookmarks* bookmarks, QByteArray* revision,
	QString* error = nullptr);
bool writeLevelBookmarks(const QString& path, const LevelViewBookmarks& bookmarks, const QByteArray& expectedRevision,
	QByteArray* newRevision = nullptr, QString* error = nullptr);

} // namespace vibestudio
