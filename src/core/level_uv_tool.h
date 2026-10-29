#pragma once

#include "core/level_face_attributes.h"
#include "core/level_uv_edit.h"

#include <QPair>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QVector>

namespace vibestudio {

// The camera UV tool's service: NetRadiant Custom's UV tool (G), an
// on-surface manipulator for one brush face or one patch. It lays the
// texture's repeat lines over the surface; a drag on the surface moves the
// texture, a ring about a movable pivot turns it, dragging a repeat line
// stretches it along U, V or both, Alt and a line skews it, and a patch's
// control points move one by one or a row or column at a time. Drags snap to
// the surface's points and edges and to the repeat lines. A brush face's
// move, turn and stretch go through the Face inspector's values
// (core/level_face_attributes.h), so a classic face stays classic; a skew and
// every patch edit go through the UV Editor's texture-space transform
// (core/level_uv_edit.h). Each drag is one undo step.
// Workflow reference: NRC radiant/selection_mtor_uv.cpp UVManipulator and
// radiant/tools.cpp MouseUV at 449778b3 (GPL-2.0-or-later). Original
// implementation; see docs/CREDITS.md.

// The surface's plane as an affine map from texels:
// world = origin + s * alongS + t * alongT. Exact on a brush face; for a
// patch, the least-squares fit to its control points.
struct LevelUvToolFrame {
	bool valid = false;
	LevelMapVec3 origin, alongS, alongT;
	// A unit vector along alongS x alongT; for a face, its outward side.
	LevelMapVec3 normal;
	[[nodiscard]] LevelMapVec3 worldAt(QPointF texel) const;
	// The texel a world point reads once projected onto the plane.
	[[nodiscard]] QPointF texelAt(const LevelMapVec3& world) const;
	// Where a ray meets the plane; false when it runs along the plane or, with
	// `forwardOnly`, meets it behind `from`.
	bool hit(const LevelMapVec3& from, const LevelMapVec3& direction, LevelMapVec3* point, bool forwardOnly = true) const;
	// The angle, in degrees from -180 to 180 and anticlockwise about the
	// normal seen from its side, that turns the direction from `pivot` to
	// `from` onto the direction from `pivot` to `to` (all in texels).
	[[nodiscard]] double turnDegrees(QPointF pivot, QPointF from, QPointF to) const;
	// The frame after the texture-space transform `transform` (new texels of
	// every world point = transform of the old ones).
	[[nodiscard]] LevelUvToolFrame transformed(const LevelUvTransform& transform) const;
};
// Fitted to `points` and the texels `uv` they read. A brush face's normal
// follows its winding; invalid when the texels collapse onto a line.
LevelUvToolFrame levelUvToolFrame(const QVector<LevelMapVec3>& points, const QVector<QPointF>& uv);

// The repeat lines across the surface, in texels.
struct LevelUvToolGrid {
	// Texels between neighbouring lines: one repeat of the image, or a whole
	// number of repeats when the surface spans more than the line limit.
	double stepS = 0, stepT = 0;
	// Lines of constant s (they run along t) and of constant t.
	QVector<double> s, t;
	// The texel rectangle the lines cross.
	double minS = 0, maxS = 0, minT = 0, maxT = 0;
	[[nodiscard]] bool isEmpty() const { return s.isEmpty() || t.isEmpty(); }
};
// One repeat of the surface's image in texels: its image size, or 64 by 64
// where a classic or Valve 220 face's image size is unknown.
QSizeF levelUvToolRepeat(const LevelUvLayout& layout);
// Lines that cover `uv` and `pivot` (when finite), rounded out to whole
// steps, at most `maxLines` each way.
LevelUvToolGrid levelUvToolGrid(const QVector<QPointF>& uv, QSizeF repeat, QPointF pivot, int maxLines = 48);
// The surface's edges in texels: a face's outline, a patch's rows and
// columns of control points.
QVector<QPair<QPointF, QPointF>> levelUvToolEdges(const LevelUvLayout& layout);

// What a drag holds.
enum class LevelUvToolHandle { None, Pivot, Move, Turn, StretchS, StretchT, StretchBoth, SkewS, SkewT, PatchPoint, PatchRow, PatchColumn };

// Snapping takes hold within `tolerance` texels along s and t (the camera
// turns a few pixels into texels), or onto the nearest candidate however far
// with `hard` (Ctrl, as in NetRadiant Custom).
struct LevelUvToolSnap {
	QPointF tolerance;
	bool hard = false;
};

// The pivot dragged to `at`, each axis on the nearest repeat line or surface
// point when one is close enough.
QPointF levelUvToolSnapPivot(const QVector<QPointF>& uv, const LevelUvToolGrid& grid, QPointF at, const LevelUvToolSnap& snap);
// A move of the whole texture that follows the pointer from texel `start` to
// texel `current` (both read before the move): the texels every point gains,
// `start - current`, snapped along each axis so a surface point or the pivot
// lands on a repeat line. `oneAxis` (Shift) keeps only the axis that moved
// further on the surface, as `frame` measures it.
QPointF levelUvToolMove(const QVector<QPointF>& uv, const LevelUvToolGrid& grid, QPointF pivot, QPointF start, QPointF current,
	const LevelUvToolSnap& snap, bool oneAxis, const LevelUvToolFrame& frame);
// Patch control points that follow the pointer from `start` to `current`:
// `current - start`, snapped so one of `moved` lands on a repeat line or on
// the pivot.
QPointF levelUvToolPointMove(const QVector<QPointF>& moved, const LevelUvToolGrid& grid, QPointF pivot, QPointF start, QPointF current,
	const LevelUvToolSnap& snap, bool oneAxis, const LevelUvToolFrame& frame);
// The stretch along one axis, about `pivot`, that brings the repeat line at
// `line` under the pointer where it reads `current` before the stretch,
// snapping `current` onto one of `values` (the surface's coordinates on that
// axis). Kept between 1/64 and 64: never through the pivot.
double levelUvToolStretch(const QVector<double>& values, double pivot, double line, double current, double tolerance, bool hard);
// The skew that keeps the texel pressed at `start` under the pointer at
// `current`. SkewS: s gains factor * (t - pivot.t); SkewT: t gains
// factor * (s - pivot.s). Snapped so the skewed lines run along a surface
// edge, or from a repeat line's foot on the pivot's row or column to a
// surface point, within `tolerance` texels at the pressed line.
double levelUvToolSkew(LevelUvToolHandle handle, const QVector<QPointF>& uv, const QVector<QPair<QPointF, QPointF>>& edges,
	const LevelUvToolGrid& grid, QPointF pivot, QPointF start, QPointF current, const LevelUvToolSnap& snap);
// A turn of `degrees` (anticlockwise about the frame's normal) snapped to
// whole `step`s when `step` is above 0 (Shift), or else so a texture axis runs
// along the nearest surface edge when it comes within `tolerance` degrees of
// one, or always with `hard`.
double levelUvToolSnapTurn(const LevelUvToolFrame& frame, const QVector<QPair<QPointF, QPointF>>& edges, double degrees, double step,
	double tolerance, bool hard);
// The patch control points one point, a row or a column names (from 0).
QVector<int> levelUvToolPatchPoints(const LevelUvLayout& layout, LevelUvToolHandle handle, int index);

// One drag's edit, in the texels the surface read when the drag began.
struct LevelUvToolEdit {
	LevelUvToolHandle handle = LevelUvToolHandle::None;
	// The texel turns, stretches and skews keep in place.
	QPointF pivot;
	// Move: texels every point gains. PatchPoint, PatchRow and PatchColumn:
	// texels `points` gain.
	QPointF move;
	// Turn: degrees anticlockwise about the frame's normal.
	double turn = 0;
	// StretchS, StretchT and StretchBoth: factors along s and t.
	QPointF stretch {1, 1};
	// SkewS: s gains skew.x() * (t - pivot.t); SkewT: t gains
	// skew.y() * (s - pivot.s).
	QPointF skew;
	QVector<int> points;
	// A classic face takes a skew only by becoming a Valve 220 face.
	bool allowValve220 = false;
	[[nodiscard]] bool changesSomething() const;
};

struct LevelUvToolResult {
	LevelMapBrushFace face;
	LevelMapPatch patch;
	// Every point's texels after the edit.
	QVector<QPointF> uv;
	// Where the new texels lie on the surface's plane: the repeat lines drawn
	// through it move with the texture.
	LevelUvToolFrame frame;
	LevelUvFit fit = LevelUvFit::Unchanged;
	// A brush face's move, turn or stretch, as Face inspector values.
	bool values = false;
	LevelFaceAttributesEdit valuesEdit;
	[[nodiscard]] bool changed() const { return fit != LevelUvFit::Unchanged; }
};

// The surface as `edit` would leave it. Refuses a skew of a classic face
// without `allowValve220`, a collapsed or out-of-range result, and a turn or
// stretch of a brush-primitive face without the image size.
bool previewLevelUvToolEdit(const LevelUvLayout& layout, const LevelUvToolEdit& edit, LevelUvToolResult* result, QString* error = nullptr);
// The edit as a plan that commitLevelSurfaceEdit() makes one undo step:
// "Set texture values" for a face's move, turn or stretch, otherwise "Edit
// texture coordinates". Refuses a surface that changed since `layout` was
// read, and hidden, locked or out-of-group surfaces. An edit that changes
// nothing gives a ready plan that commits nothing.
bool prepareLevelUvToolEdit(const LevelMapDocument& document, const LevelUvLayout& layout, const LevelUvToolEdit& edit, LevelSurfaceEditPlan* plan,
	LevelUvToolResult* result = nullptr, QString* error = nullptr);
// Prepares and commits: one undo step, or none when nothing changes.
bool applyLevelUvToolEdit(LevelMapDocument* document, const LevelUvLayout& layout, const LevelUvToolEdit& edit, LevelUvToolResult* result = nullptr,
	QString* error = nullptr);

} // namespace vibestudio
