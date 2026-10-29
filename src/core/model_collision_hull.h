#pragma once

#include "core/model_mesh.h"

#include <array>

namespace vibestudio
{
// Convex collision hulls. A hull is a collision volume whose `hull` points
// (ModelCollisionBox::hull) lie in its box frame: world = centre + R(rotation)
// * (point * size), each coordinate within -0.5..0.5. The box frame moves,
// turns, scales and animates the hull with the ordinary box operations; the
// shape itself stays the same in every pose. Planes are derived from the points.
inline constexpr int modelCollisionHullMaxPoints = 256;

// How many faces, edges and vertices a hull may have. Zero means no limit.
struct ModelCollisionHullBudget
{
	QString profile;
	int faces = 128, edges = 0, vertices = 0;
};
// Profiles: "quake", "quake2", "quake3" (a map brush: 128 faces, the level
// editor's limit) and "doom3-trace" (a Doom 3 trace model: 16 polygons, 32
// edges and 32 vertices, MAX_TRACEMODEL_* in idlib/geometry/TraceModel.h).
// maxFaces > 0 lowers the face budget further (4 at least).
bool modelCollisionHullBudget(const QString &profile, int maxFaces, ModelCollisionHullBudget *budget, QString *error = nullptr);

// The exact convex hull of a point set, with planar faces merged.
struct ModelCollisionHullShape
{
	QVector<ModelVec3> vertices;
	QVector<QPair<int, int>> edges;
	// Vertex indices around each face, counter-clockwise seen from outside.
	QVector<QVector<int>> faces;
	// Outward unit normal and offset per face: dot(normal, x) <= offset inside.
	QVector<std::array<double, 4>> planes;
	double volume = 0;
};
// Refuses fewer than four points, and coplanar or collinear sets.
bool modelCollisionHullShape(const QVector<ModelVec3> &points, ModelCollisionHullShape *shape, QString *error = nullptr,
							 const ModelWorkControl &control = {});

struct ModelCollisionHullFit
{
	// World-space hull vertices; every input point lies inside the hull.
	QVector<ModelVec3> vertices;
	int faces = 0, edges = 0, vertexCount = 0;
	double volume = 0, exactVolume = 0;
	// True when the exact hull exceeded the budget and was conservatively
	// simplified: fewer tangent planes, so the hull only grows.
	bool simplified = false;
};
// Exact hull of the points when it fits the budget; otherwise the smallest
// hull a greedy choice of the exact hull's own tangent planes (starting from
// the bounding box) reaches within the budget. Deterministic.
bool fitModelCollisionHull(const QVector<ModelVec3> &points, const ModelCollisionHullBudget &budget, ModelCollisionHullFit *fit,
						   QString *error = nullptr, const ModelWorkControl &control = {});

bool isModelCollisionHull(const ModelCollisionBox &box);
// World points of a volume at its pose: a hull's points, else the 8 corners.
QVector<ModelVec3> modelCollisionPoints(const ModelCollisionBox &box);
// World-space edges of a volume at its pose, for overlays.
QVector<QPair<ModelVec3, ModelVec3>> modelCollisionEdges(const ModelCollisionBox &box);
// Turns fitted world vertices into a static hull volume (rotation zero; the
// frame is their bounding box). Refuses extents under one unit.
bool makeModelCollisionHull(const QString &name, const QVector<ModelVec3> &vertices, ModelCollisionBox *box, QString *error = nullptr);
QString validateModelCollisionHull(const ModelCollisionBox &box);
} // namespace vibestudio
