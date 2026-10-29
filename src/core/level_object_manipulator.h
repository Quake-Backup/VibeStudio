#pragma once
#include "core/level_map.h"

#include <QPair>
#include <QString>
#include <QVector>

#include <array>

namespace vibestudio {
// The Levels object manipulators, after NetRadiant Custom's Translate (W),
// Rotate, Scale and Transform tools: one tool at a time on the tool bar, all
// turning, moving and sizing about one shared pivot. Select keeps the views'
// ordinary select-and-drag behaviour. Transform is NetRadiant Custom's box
// manipulator (MouseTransform): its sides skew, its corners turn, inside it
// moves and just outside a side or corner scales.
enum class LevelObjectTool { Select, Translate, Rotate, Scale, Transform };
// Stable identifiers: "select", "translate", "rotate", "scale", "transform".
QString levelObjectToolId(LevelObjectTool tool);
// Unknown identifiers map to Select.
LevelObjectTool levelObjectToolFromId(const QString& id);

// What an object manipulator acts on: the whole selection, when every object
// in it can be edited here. Quake-family maps only. Hidden objects (including
// the brushes and patches of a selected entity), scene-locked objects and
// objects outside the open group make the target unavailable, with a
// translated reason, rather than moving part of the selection.
struct LevelObjectManipulatorTarget {
	QVector<LevelMapSelectionRef> objects;
	// levelMapSelectionBounds(): what a transform of the selection moves.
	LevelMapVec3 mins, maxs, centre;
	int hidden = 0, locked = 0, outsideGroup = 0;
	bool available = false;
	QString reason;
};
LevelObjectManipulatorTarget levelObjectManipulatorTarget(const LevelMapDocument& document);
// The point the tools move and turn about: `custom` when it is valid, else
// the centre of the target's bounds. Invalid when the target has no bounds.
LevelMapVec3 levelObjectManipulatorPivot(const LevelObjectManipulatorTarget& target, const LevelMapVec3& custom);

// NetRadiant Custom's pivot drag: each axis named in `axes` (bit 0 X, bit 1
// Y, bit 2 Z) lands on whichever is nearest of the bounds' minimum, centre and
// maximum and the nearest grid line (no grid line when `grid` <= 0). The other
// axes keep `start`'s values. Non-finite input returns `start`.
LevelMapVec3 snapLevelObjectPivot(const LevelMapVec3& wanted, const LevelMapVec3& start, int axes, const LevelMapVec3& mins,
	const LevelMapVec3& maxs, double grid);

enum class LevelObjectTranslateSnap { None, Grid, Bounds };
// NetRadiant Custom's translate snapping. Grid rounds the delta to the grid,
// so relative spacing on and off the grid is kept. Bounds moves each changed
// axis so that whichever of the bounds' faces ends nearer a grid line lands on
// it, as Ctrl does in NetRadiant Custom. An axis that does not change, a grid
// <= 0 or invalid bounds leave the delta as it is.
LevelMapVec3 snapLevelObjectTranslation(const LevelMapVec3& delta, const LevelMapVec3& mins, const LevelMapVec3& maxs, double grid,
	LevelObjectTranslateSnap snap);
// Shift on NetRadiant Custom's free-move square: only the largest component
// survives (X before Y before Z on a tie).
LevelMapVec3 levelObjectDominantAxis(const LevelMapVec3& delta);

struct LevelObjectTranslateRequest {
	LevelMapVec3 delta {0, 0, 0, true};
	// When valid, the move takes `pivot` (or the target's pivot when `pivot`
	// is invalid) to `destination` instead, and `delta` is ignored.
	LevelMapVec3 destination;
	LevelMapVec3 pivot;
	double grid = 0;
	LevelObjectTranslateSnap snap = LevelObjectTranslateSnap::None;
	LevelMapTextureLockOptions textures;
};
struct LevelObjectTranslateReport {
	// The translation applied, after snapping, and where the pivot and the
	// selection's bounds were and went.
	LevelMapVec3 delta, pivotBefore, pivotAfter, mins, maxs;
	int objects = 0;
};
// The Translate tool's edit: the selection moves by one translation as one
// undo step through moveLevelMapSelection(), with texture lock, scene locks,
// the open group and linked groups as every move has them. Refuses an
// unavailable target (see above) and a translation that snaps to zero; a
// refusal leaves the document and its history untouched.
bool translateLevelMapObjects(LevelMapDocument* document, const LevelObjectTranslateRequest& request,
	LevelObjectTranslateReport* report = nullptr, QString* error = nullptr);

// The Rotate tool's turn, after NetRadiant Custom's Rotate (R) and
// TrenchBroom's Rotate Tool: `degrees` right-handed about the direction
// `axis` (any length but zero) through the pivot.
struct LevelObjectRotateRequest {
	LevelMapVec3 axis {0, 0, 1, true};
	double degrees = 0;
	// Invalid means the target's pivot, the centre of its bounds.
	LevelMapVec3 pivot;
	// Above 0, `degrees` rounds to a multiple of this step first.
	double angleStep = 0;
	LevelMapTextureLockOptions textures;
};
struct LevelObjectRotateReport {
	// The unit axis and the angle after snapping, and the pivot used.
	LevelMapVec3 axis, pivot;
	double degrees = 0;
	// 0, 1 or 2 when the axis is a world axis, so the turn is one numeric
	// rotation about it; -1 when it is the Euler turn below instead.
	int worldAxis = -1;
	// The fixed-axis X, then Y, then Z degrees an Euler turn applies.
	std::array<double, 3> eulerXyz {0, 0, 0};
	// The selection's bounds after the turn.
	LevelMapVec3 mins, maxs;
	int objects = 0;
};
// Rounds to the nearest multiple of `step` (no rounding when `step` <= 0).
double snapLevelObjectAngle(double degrees, double step);
// The columns of the right-handed rotation by `degrees` about `axis`
// (normalised here); the identity for a zero or non-finite axis.
std::array<LevelMapVec3, 3> levelObjectRotationColumns(const LevelMapVec3& axis, double degrees);
// The fixed-axis X, then Y, then Z turns (degrees) whose product Rz*Ry*Rx is
// the rotation with these columns, as NetRadiant Custom's Arbitrary Rotation
// applies them. At gimbal lock the Z turn is 0.
std::array<double, 3> levelObjectEulerXyz(const std::array<LevelMapVec3, 3>& columns);
// NetRadiant Custom's free rotation: where the line through `origin` along
// `direction` meets the sphere about `centre`, on the viewer's side, as a
// unit vector from the centre; a line that misses it gives the sphere's
// nearest edge. Invalid when the input is degenerate.
LevelMapVec3 levelObjectSpherePoint(const LevelMapVec3& origin, const LevelMapVec3& direction, const LevelMapVec3& centre, double radius);
// The turn taking unit vector `from` to unit vector `to` (axis from x to,
// degrees from 0 to 180). False when they are parallel or invalid.
bool levelObjectArcball(const LevelMapVec3& from, const LevelMapVec3& to, LevelMapVec3* axis, double* degrees);
// The numeric rotation the Rotate tool commits: one plain turn about a world
// axis, or NetRadiant Custom's Euler X, Y, Z turn for any other axis, about
// the target's pivot (or request.pivot), with request.textures. The GUI
// publishes it through the shared Rotate placement; rotateLevelMapObjects()
// below applies it. Refuses a non-finite axis or angle, a zero axis and a
// turn that snaps to a whole number of turns.
bool levelObjectRotation(const LevelObjectManipulatorTarget& target, const LevelObjectRotateRequest& request, LevelMapRotationRequest* rotation,
	LevelObjectRotateReport* report = nullptr, QString* error = nullptr);
// The Rotate tool's edit: levelObjectRotation() through
// rotateLevelMapSelection() as one undo step, with texture lock, the Valve 220
// policy, scene locks and the open group as every rotation has them. Refuses
// an unavailable target; a refusal leaves the document and its history untouched.
bool rotateLevelMapObjects(LevelMapDocument* document, const LevelObjectRotateRequest& request, LevelObjectRotateReport* report = nullptr,
	QString* error = nullptr);
// The Scale tool, after NetRadiant Custom's Scale: X, Y and Z handles and a
// square across the view, about the shared pivot.
struct LevelObjectScaleDrag {
	// 0, 1 or 2: that axis' handle; 3: the square that scales across the view.
	int handle = 0;
	// How far the pointer has moved since the drag began, in map units: along
	// the axis, or in the view plane for the square.
	LevelMapVec3 delta {0, 0, 0, true};
	// The selection's bounds and the pivot it scales about.
	LevelMapVec3 mins, maxs, pivot;
	// The axis nearest the line of sight, which the square's Shift leaves
	// alone; -1 when none is.
	int viewAxis = -1;
	// The travel rounds to `grid` (0: free); Ctrl lands each scaled size on a
	// multiple of `sizeGrid` (0: off).
	double grid = 0, sizeGrid = 0;
	// Shift: an axis handle scales all three axes alike, the square its two
	// view axes alike (by whichever changes most).
	bool equal = false;
};
// NetRadiant Custom's ScaleAxis and ScaleFree: an axis' factor is (e + d) / e,
// where e is the farther of the bounds' sides from the pivot along it and d
// the travel, so the farther side follows the pointer. An axis the selection
// has no extent along keeps 1. A size never drops below the grid (1 unit
// without one, the size grid with Ctrl) or its own size, so factors stay
// above zero.
std::array<double, 3> levelObjectScaleFactors(const LevelObjectScaleDrag& drag);
struct LevelObjectScaleRequest {
	std::array<double, 3> factors {1, 1, 1};
	// Invalid means the target's pivot, the centre of its bounds.
	LevelMapVec3 pivot;
	// Texture Scale Lock: enabled stretches brush textures with the brushes.
	LevelMapTextureLockOptions textures {false, false};
};
struct LevelObjectScaleReport {
	std::array<double, 3> factors {1, 1, 1};
	// The pivot used and the selection's bounds after scaling.
	LevelMapVec3 pivot, mins, maxs;
	int objects = 0;
};
// The numeric scale the Scale tool commits: core/level_map.h
// scaleLevelMapSelection() by the factors about the pivot. Refuses an
// unavailable target, factors that are not above zero and at most 1024 (Flip
// and map resize --factors mirror) and factors that are all 1.
bool levelObjectScale(const LevelObjectManipulatorTarget& target, const LevelObjectScaleRequest& request, LevelMapScaleRequest* scale,
	LevelObjectScaleReport* report = nullptr, QString* error = nullptr);
// The Scale tool's edit: levelObjectScale() through scaleLevelMapSelection()
// as one undo step, with Texture Scale Lock, scene locks and the open group as
// every scale has them. A refusal leaves the document and its history untouched.
bool scaleLevelMapObjects(LevelMapDocument* document, const LevelObjectScaleRequest& request, LevelObjectScaleReport* report = nullptr,
	QString* error = nullptr);

// The Transform tool's pivot for a handle while no pivot is placed, as
// NetRadiant Custom's skew manipulator sets it: on each axis `sides` names
// (-1 the box's minimum, +1 its maximum) the opposite side, and the box's
// centre on the others. Invalid for an invalid box.
LevelMapVec3 levelObjectTransformPivot(const LevelMapVec3& mins, const LevelMapVec3& maxs, const std::array<int, 3>& sides);
// The Transform tool's scale, dragged from just outside the sides named in
// `sides` (-1 minimum, +1 maximum, 0 none per axis).
struct LevelObjectSideScaleDrag {
	std::array<int, 3> sides {0, 0, 0};
	// How far the pointer has moved since the drag began, in map units.
	LevelMapVec3 delta {0, 0, 0, true};
	LevelMapVec3 mins, maxs, pivot;
	// The travel rounds to `grid` (0: free); `sizeGrid` lands each scaled
	// size on its multiple (0: off).
	double grid = 0, sizeGrid = 0;
	// Every axis takes the factor that changes most, keeping proportions.
	bool equal = false;
};
// Each named side follows the pointer about the pivot: its factor is
// (e + d) / e, where e is the side's signed distance from the pivot and d the
// travel along that axis; other axes keep 1. A side (almost) on the pivot
// keeps 1. Sizes never drop below the grid (1 unit without one, the size grid
// when set) or their own size, so factors stay above zero.
std::array<double, 3> levelObjectSideScaleFactors(const LevelObjectSideScaleDrag& drag);

// A side shear, after TrenchBroom's Shear Tool and the side lines of
// NetRadiant Custom's Transform tool: one side of the selection's box slides
// along itself, or anywhere within its plane, while the plane opposite it
// stays where it is, slanting the selection.
struct LevelObjectShearRequest {
	// The side that slides: `axis` 0, 1 or 2 and `side` -1 (its minimum) or
	// +1 (its maximum).
	int axis = 2, side = 1;
	// How far it slides; the component along `axis` is ignored.
	LevelMapVec3 offset {0, 0, 0, true};
	// Valid: the plane through this point across `axis` stays where it is
	// instead of the opposite side (the Transform tool's placed pivot).
	LevelMapVec3 pivot;
	LevelMapTextureLockOptions textures;
};
struct LevelObjectShearReport {
	// Each axis' slide per unit along `axis` (0 for `axis` itself), and the
	// coordinate along `axis` that stays put.
	std::array<double, 3> factors {0, 0, 0};
	double anchor = 0;
	// The 3x4 affine, row by row with the translation last.
	std::array<double, 12> matrix {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
	// The selection's bounds after the shear.
	LevelMapVec3 mins, maxs;
	int objects = 0;
};
// The shear a side's slide makes. Refuses an unavailable target, a bad side,
// a non-finite offset or pivot, a side that does not move, a side lying on the
// plane that stays put and a slide above 16 units per unit.
bool levelObjectShear(const LevelObjectManipulatorTarget& target, const LevelObjectShearRequest& request, LevelObjectShearReport* report,
	QString* error = nullptr);
// The side shear as one undo step, with texture lock, scene locks and the open
// group as every shear has them: a slide along one axis through
// shearLevelMapSelectionAbout() (so Repeat replays it), one within the side's
// plane through transformLevelMapSelectionAffine(). A refusal leaves the
// document and its history untouched.
bool shearLevelMapObjects(LevelMapDocument* document, const LevelObjectShearRequest& request, LevelObjectShearReport* report = nullptr,
	QString* error = nullptr);
// A wire of the objects for a manipulator's preview: brush polygon edges,
// patch control nets and a 16-unit box about each point entity's origin
// (selected entities' own brushes and patches included). At most `limit`
// edges; past it the bounds' twelve edges stand in and `limited` is set.
QVector<QPair<LevelMapVec3, LevelMapVec3>> levelObjectManipulatorEdges(const LevelMapDocument& document,
	const QVector<LevelMapSelectionRef>& objects, int limit = 16384, bool* limited = nullptr);
} // namespace vibestudio
