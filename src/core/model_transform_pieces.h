#pragma once

// Individual Origins pivots and Normal transform axes for mesh selections.
//
// Individual Origins transforms each connected piece of a selection about its
// own centre, as Blender's Individual Origins pivot and 3ds Max's Local pivot
// for separate elements do: scaling twelve separate teeth shrinks each tooth
// in place instead of pulling them all towards the middle of the jaw. A piece
// is a connected part of the selected vertices: two selected vertices belong
// to one piece when a triangle edge joins them or when they are seam copies at
// one position. Each piece pivots on the centre of its bounds in the pivot
// pose, the same rule Selection Centre uses for the whole selection.
//
// Normal axes orient a transform by a selection's surface: Z is the
// area-weighted normal of the faces whose corners are all selected (the
// average vertex normal when no such face exists), X follows the longest
// border edge of those faces (the longest selected edge when there are no
// faces) projected onto the plane, and Y completes a right-handed basis. When
// no edge gives a direction, X is the world axis least aligned with Z.
// Combined with Individual Origins, every piece uses its own normal axes.
//
// The definitions follow the documented behaviour of Blender's pivot points
// and transform orientations
// (https://docs.blender.org/manual/en/latest/editors/3dview/controls/pivot_point/individual_origins.html,
// https://docs.blender.org/manual/en/latest/editors/3dview/controls/orientation.html);
// no Blender code is used.

#include "core/model_transform.h"
#include "core/model_work.h"

#include <QHash>
#include <QSet>
#include <QVector>

namespace vibestudio
{
struct ModelEdit;
struct ModelGeometricTopology;
struct ModelMeshToolOptions;
class ModelWorkProgress;

struct ModelTransformPiece
{
	// Ascending vertex indices.
	QVector<int> vertices;
	// The centre of the piece's bounds in the pivot pose.
	ModelVec3 pivot;
	// The piece's own normal axes, used when ownAxes is set.
	ModelTransformBasis basis;
	bool ownAxes = false;
	bool operator==(const ModelTransformPiece &other) const;
};

// The connected pieces of `vertices` on `surface`, in order of their lowest
// vertex, each with its pivot from `pivotFrame`.
bool modelTransformPieces(const ModelSurface &surface, const QSet<int> &vertices, int pivotFrame, QVector<ModelTransformPiece> *pieces,
						  QString *error = nullptr, const ModelWorkControl &control = {});
// Normal axes of `vertices` in `frame`. False when the vertices have no usable
// normal (no faces with area and only zero vertex normals).
bool modelNormalTransformBasis(const ModelSurface &surface, int frame, const QSet<int> &vertices, ModelTransformBasis *result);
// `transform` about the piece's pivot, in the piece's axes when it has its own.
ModelTransform modelPieceTransform(const ModelTransform &transform, const ModelTransformPiece &piece);

// Proportional editing and symmetry (Weighted Transform): how one vertex
// follows a transform.
struct ModelTransformWeight
{
	// 1 for the selection, the falloff weight for vertices near it.
	double weight = 1;
	// A symmetry counterpart: the mirror axes that map it onto its source. It
	// takes the transform reflected across them.
	int mirror = 0;
	// The active mirror planes the vertex lies on. Both reflections of the
	// transform are averaged, so the vertex stays on them.
	int plane = 0;
	// The piece (Individual Origins) whose pivot and axes it follows.
	int piece = 0;
	bool operator==(const ModelTransformWeight &) const = default;
};
// The vertices a Weighted Transform moves, keyed by geometric group (see
// ModelGeometricTopology): the selected `vertices` at weight 1, the groups
// within options.proportionalRadius at their falloff weight (straight-line or,
// with options.connectedOnly, along edges), and with options.mirrorAxes the
// symmetry counterparts of all of them, matched by reflected position within
// options.mergeThreshold. Distances and counterparts come from `reference`.
// `pieceOf` maps selected vertices to their Individual Origins piece; nearby
// vertices follow the piece of their nearest selected vertex and counterparts
// the piece of their source.
bool modelTransformWeights(const ModelSurface &surface, const ModelGeometricTopology &topology, const QSet<int> &vertices, int reference,
						   const ModelMeshToolOptions &options, const QHash<int, int> &pieceOf, QHash<int, ModelTransformWeight> *weights,
						   ModelWorkProgress &work);
// `point` after `transform` (already about the vertex's piece) at the weight's
// strength: translation and rotation scaled by the weight, scale blended from
// 1; reflected for counterparts and kept on mirror planes.
ModelVec3 modelWeightedTransformPoint(const ModelVec3 &point, const ModelTransform &transform, const ModelTransformWeight &weight);
// The matching direction for a vertex normal.
ModelVec3 modelWeightedTransformNormal(const ModelVec3 &normal, const ModelTransform &transform, const ModelTransformWeight &weight);

// The pieces a viewport preview transforms: the vertices of the edited surface
// or, for a whole-surface selection, each surface as one piece. Empty unless
// the preview uses Individual Origins, proportional editing or symmetry.
struct ModelTransformPieceMap
{
	QVector<ModelTransformPiece> pieces;
	// Vertex of the edited surface (or whole surface) -> piece.
	QHash<int, int> pieceOf;
	bool wholeSurfaces = false;
	// Weighted previews (proportional editing, symmetry): every vertex of the
	// edited surface the transform moves, seam copies included.
	QHash<int, ModelTransformWeight> weights;
	[[nodiscard]] bool isEmpty() const { return pieces.isEmpty() && weights.isEmpty(); }
	// Whether a weighted preview moves `vertex` of the edited surface.
	[[nodiscard]] bool moves(int vertex) const { return weights.contains(vertex); }
	// `transform` for one vertex: about its piece's pivot, or unchanged.
	[[nodiscard]] ModelTransform transformFor(const ModelTransform &transform, int surface, int vertex) const;
	// The previewed position and normal of one vertex: the weighted result for
	// weighted previews, else `transform` about the vertex's piece.
	[[nodiscard]] ModelVec3 apply(const ModelTransform &transform, int surface, int vertex, const ModelVec3 &point) const;
	[[nodiscard]] ModelVec3 applyNormal(const ModelTransform &transform, int surface, int vertex, const ModelVec3 &normal) const;
	bool operator==(const ModelTransformPieceMap &) const = default;
};
// The map for the vertices of `surface` (or, when `surfaces` is not empty,
// for those whole surfaces) with pivots, and with `normalAxes` each piece's
// own normal axes, from `frame`: what a core Transform with Individual Origins
// applies.
bool modelTransformPieceMap(const ModelMesh &mesh, int surface, const QSet<int> &vertices, const QSet<int> &surfaces, int frame,
							bool normalAxes, ModelTransformPieceMap *map);
// The weighted preview map for `vertices` of `surface` (what a Weighted
// Transform with `options` applies), from `frame`. With `individualOrigins`
// the pieces (and with `normalAxes` their own axes) come first, as
// modelTransformPieceMap makes them.
bool modelWeightedTransformPieceMap(const ModelMesh &mesh, int surface, const QSet<int> &vertices, int frame, bool individualOrigins,
									bool normalAxes, const ModelMeshToolOptions &options, ModelTransformPieceMap *map);

// The transforms a mesh Transform or Weighted Transform applies to `vertices`:
// `transform` itself unless the edit uses Individual Origins, in which case one
// transform per piece (in each piece's normal axes when the edit uses Normal
// axes) and `pieceOf` maps every vertex to its piece. `pivotFrame` is the pose
// that places the pivots and normals.
bool modelPieceTransforms(const ModelSurface &surface, const ModelEdit &edit, const ModelTransform &transform, const QSet<int> &vertices,
						  int pivotFrame, QVector<ModelTransform> *transforms, QHash<int, int> *pieceOf, QString *error = nullptr,
						  const ModelWorkControl &control = {});
} // namespace vibestudio
