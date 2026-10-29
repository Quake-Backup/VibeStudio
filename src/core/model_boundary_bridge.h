#pragma once

#include "core/model_boundary_fill.h"

namespace vibestudio
{
struct ModelBoundaryBridgeReport
{
	int firstAnchor = -1, secondAnchor = -1;
	QVector<ModelEdge> boundaryEdges;
	QVector<int> faces;
};

struct ModelBoundaryBridgeOptions
{
	int referenceFrame = 0;
	// Advances the second side's starting vertex (closed loops only).
	int twist = 0;
	// Rings of new vertices between the two sides (0-64), interpolated in
	// every pose with their UVs, normals and skin weights.
	int segments = 0;
	// Bridge exactly the selected edges as two open, unbranched boundary
	// chains instead of the closed loops they seed.
	bool openChains = false;
	// Model units per texture repeat for a new UV strip: the bridge gets its
	// own vertices along both sides with strip UVs and seams at its border.
	// Zero reuses the boundary vertices and interpolates their attributes.
	double tileSize = 0;
};

// One or more seed edges from each of exactly two disjoint closed boundaries.
// Unequal vertex counts are supported. The reference pose chooses a closest
// anchor pair and deterministic minimum-cross-edge-cost strips within two
// seam orientations; twist advances the second anchor around its aligned loop. Existing vertex
// attributes stay exact, and the new strip must be valid in every stored pose.
// No source/result/report mutation on failure, including result == &source.
bool bridgeModelBoundaryLoops(const ModelSurface &source, const QSet<ModelEdge> &selected, int referenceFrame, int twist,
	int triangleLimit, ModelSurface *result, ModelBoundaryBridgeReport *report, QString *error,
	const ModelWorkControl &control = {});
// The same bridge with segments, open chains and UV strips. Open chains run
// opposite to their faces, so the strip winds with its neighbours; their
// first vertices pair up. vertexLimit caps the surface's vertex count.
bool bridgeModelBoundaries(const ModelSurface &source, const QSet<ModelEdge> &selected, const ModelBoundaryBridgeOptions &options,
	int triangleLimit, int vertexLimit, ModelSurface *result, ModelBoundaryBridgeReport *report, QString *error,
	const ModelWorkControl &control = {});
// Bridge Faces: removes two selected face regions, each surrounded by other
// faces and bounded by one loop, and bridges the two holes in one step.
// Vertices the regions alone used are left unused for the caller to compact.
bool bridgeModelFaceRegions(const ModelSurface &source, const QSet<int> &faces, const ModelBoundaryBridgeOptions &options,
	int triangleLimit, int vertexLimit, ModelSurface *result, ModelBoundaryBridgeReport *report, QString *error,
	const ModelWorkControl &control = {});
} // namespace vibestudio
