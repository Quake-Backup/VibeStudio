#pragma once
#include "core/level_brush.h"
#include "core/level_map.h"

#include <QPair>
#include <QString>
#include <QVector>

// NetRadiant Custom's QE4 Drag tool as data: which brush planes a press picks
// and how a drag moves them, shared by the 2D views, the camera, the Build
// tool and `map drag-planes`.
//
// - Facing planes: a press beside the selection picks, on every selected
//   brush, the planes that face the press point and are seen most nearly
//   edge-on, plus the planes of other selected brushes that lie against
//   them back to back; the drag translates those planes (NRC's plain drag
//   outside the selection). The camera picks one plane per brush.
// - Plane: the one plane under the pointer, or behind the edge nearest it,
//   and the planes lying on it either way round on the other selected
//   brushes, moved along its normal (NRC's Alt drag in the camera).
// - Vertices: the selected brushes' vertices under the pointer, or else the
//   corners of the faces on the plane under it, moved freely (NRC's Alt drag
//   in a 2D view).
// - Extrude: the faces on the plane under the pointer, pulled out into new
//   brushes or pushed in to split a slab off, along its normal (NRC's Ctrl+Alt
//   drag, and the Build tool's face drag).
//
// Workflow reference: NetRadiant Custom radiant/selection_mtor_drag.cpp
// DragManipulator, Scene_forEachPlaneSelectable_selectPlanes,
// Scene_forEachPlaneSelectable_bestPlane and selection_selectVerticesOrFaceVertices,
// radiant/brush.h BrushInstance::selectPlanes, FaceInstance::trySelectPlane,
// EdgeInstance::bestPlaneIndirect and BrushInstance::transformComponents (the
// grid search for a drag that would collapse a brush), and
// radiant/selection_mtable_extrude.h DragExtrudeFaces at 449778b3
// (GPL-2.0-or-later). Original implementation; see docs/CREDITS.md.

namespace vibestudio {

enum class LevelPlaneDragKind { FacingPlanes, Plane, Vertices, Extrude };
// "facing-planes", "plane", "vertices", "extrude".
QString levelPlaneDragKindId(LevelPlaneDragKind kind);
bool levelPlaneDragKindFromId(const QString& id, LevelPlaneDragKind* kind);

// Where a press looks from. `origin` is a point on the press ray and
// `direction` the ray's direction away from the viewer: a 2D view's axis, or
// the camera's ray through the pointer. `forward` is the view's direction
// (the camera's line of sight; a 2D view's axis), which decides which planes
// are seen edge-on. `camera` marks a perspective view: hits must lie ahead of
// `origin`, and the facing-planes pick keeps one plane per brush.
// `tolerance` is how far, in map units across the view, a vertex may lie from
// the press point and still be under it (2D views).
struct LevelPlaneDragProbe {
	LevelMapVec3 origin;
	LevelMapVec3 direction;
	LevelMapVec3 forward;
	bool camera = false;
	double tolerance = 4;
};

struct LevelPlaneDragPick {
	LevelPlaneDragKind kind = LevelPlaneDragKind::FacingPlanes;
	// Faces (planes, extrusion) or vertices (Vertices), with the ids
	// levelBrushTopology() gives them.
	QVector<LevelMapComponentSet> sets;
	// The brushes the sets name, as they were when picked, for previews.
	QVector<LevelMapBrush> brushes;
	// Plane and Extrude: the picked plane's outward normal, and the point on
	// it nearest the press (where an axis drag starts). Facing planes: the
	// press point.
	LevelMapVec3 normal, anchor;
	// Corners of the picked faces, for snapping an extrusion.
	QVector<LevelMapVec3> corners;
	int planes = 0;
	// Why nothing was picked; empty on success.
	QString reason;
	[[nodiscard]] bool valid() const { return !sets.isEmpty(); }
};

// The brushes a drag may act on: selected brushes and the brushes of selected
// entities, in Quake-family maps, leaving out hidden ones; at most 256.
QVector<int> levelPlaneDragBrushIds(const LevelMapDocument& document, const QVector<LevelMapSelectionRef>& selection);

LevelPlaneDragPick pickLevelPlaneDrag(const LevelMapDocument& document, const QVector<LevelMapSelectionRef>& selection,
	LevelPlaneDragKind kind, const LevelPlaneDragProbe& probe);

struct LevelPlaneDragRequest {
	LevelPlaneDragKind kind = LevelPlaneDragKind::FacingPlanes;
	QVector<LevelMapComponentSet> sets;
	// Facing planes, Plane and Vertices: how far the drag went.
	LevelMapVec3 translation {0, 0, 0, true};
	// Extrude: the distance along `normal` (out is positive).
	double distance = 0;
	LevelMapVec3 normal;
	// A plane drag that would leave a face without a polygon is cut back,
	// one axis after another, to the furthest step of this size that keeps
	// every face; 0 searches in 64 steps. NetRadiant Custom searches whole
	// grid steps the same way.
	double grid = 0;
	// Extrude only: Texture Lock for the moved face (core/level_brush.h).
	bool textureLock = false;
};

struct LevelPlaneDragReport {
	int brushes = 0, planes = 0, vertices = 0;
	// Brushes whose planes stopped short so that no face vanished.
	int clampedBrushes = 0;
	int createdBrushes = 0;
	int collapsedVertices = 0;
	QVector<int> brushIds;
};

// Translates `faces` of one brush by `translation`: each face's plane points
// move by it, so each plane moves by its normal's share of it; textures stay
// where they lie in the world, as NetRadiant Custom's drag leaves them. Where
// a face would lose its polygon the translation is searched back per axis to
// the furthest `grid` step (0: 64 steps) that keeps every face. `applied`
// receives what was used (zero when nothing could move). False for an
// invalid brush or face.
bool moveLevelBrushPlanes(const LevelMapBrush& brush, const QVector<int>& faces, const LevelMapVec3& translation, double grid,
	LevelMapBrush* moved, LevelMapVec3* applied = nullptr, QString* error = nullptr);

// The brushes as the drag would leave them (moved, split or new), from the
// brushes the pick copied, with their polygon edges, for previews. No
// document is needed or changed.
bool previewLevelPlaneDrag(const QVector<LevelMapBrush>& brushes, const LevelPlaneDragRequest& request,
	QVector<QPair<LevelMapVec3, LevelMapVec3>>* edges, LevelPlaneDragReport* report = nullptr, QString* error = nullptr);

// Applies the drag as one undo step. Every set must name a selected brush or
// a brush of a selected entity; hidden, scene-locked and out-of-group brushes
// refuse the whole drag, leaving the document and its history untouched.
// Plane drags go through replaceLevelMapComponentGeometry(), vertex drags
// through transformLevelMapComponents() and extrusions through
// extrudeLevelMapFaces() (split, so a pull makes new brushes and a push cuts
// a slab off), so linked groups follow as they do for those edits.
bool applyLevelPlaneDrag(LevelMapDocument* document, const LevelPlaneDragRequest& request, LevelPlaneDragReport* report = nullptr,
	QString* error = nullptr);

// The Build tool's chain: copies of the selection laid end to end along the
// drag's main axis, each one box length on, as many as the drag covers (half
// a length rounds up), at most `maximum`. A box with no length along that
// axis steps by `grid` (or one unit). `step` is the offset between copies.
struct LevelCloneChain {
	LevelMapVec3 step {0, 0, 0, true};
	int axis = -1;
	int copies = 0;
};
LevelCloneChain levelCloneChain(const LevelMapVec3& mins, const LevelMapVec3& maxs, const LevelMapVec3& travel, double grid, int maximum = 64);
// Finishes a chain the Build tool laid with one array placement (Duplicate
// with Offset's copies): only the last copy stays selected, as a duplicate
// leaves its copy, and the chain is recorded for Repeat Last Commands as
// `copies` duplicate steps of `step`, so a repeat carries the chain on from
// that copy. Call it right after the array is published; a single copy is
// already selected alone and records itself as an ordinary duplicate, so this
// does nothing for it. Arrays made elsewhere stay unrecorded.
void recordLevelCloneChainRepeat(LevelMapDocument* document, const LevelMapVec3& step, int copies);

} // namespace vibestudio
