#pragma once

// A bounding volume hierarchy over triangles for ray queries (UVSKIN-06
// baking): nearest hit and any hit within a distance. Original VibeStudio
// code: median splits on the longest axis of the centroid bounds, eight
// triangles a leaf, the Moeller-Trumbore ray-triangle test (T. Moeller and
// B. Trumbore, "Fast, Minimum Storage Ray/Triangle Intersection", Journal of
// Graphics Tools 2(1), 1997) and the slab test for boxes.

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QVector>

#include <array>

namespace vibestudio {

struct ModelBvhTriangle {
	std::array<ModelVec3, 3> corners;
	// Caller data: which surface and triangle this is.
	int surface = -1;
	int triangle = -1;
};

struct ModelBvhHit {
	bool hit = false;
	double distance = 0;
	// Barycentric weights of corners 1 and 2 (corner 0 takes the rest).
	double u = 0, v = 0;
	int index = -1; // into the triangles the tree was built from
};

class ModelBvh {
public:
	// False with an error when cancelled.
	bool build(QVector<ModelBvhTriangle> triangles, QString* error = nullptr, const ModelWorkControl& control = {});
	[[nodiscard]] const QVector<ModelBvhTriangle>& triangles() const { return m_triangles; }
	[[nodiscard]] bool isEmpty() const { return m_triangles.isEmpty(); }
	// The nearest triangle the ray (origin + t * direction, direction unit
	// length) hits with minDistance < t < maxDistance, front or back face.
	[[nodiscard]] ModelBvhHit nearest(const ModelVec3& origin, const ModelVec3& direction, double minDistance, double maxDistance) const;
	// Whether anything is hit in that range.
	[[nodiscard]] bool occluded(const ModelVec3& origin, const ModelVec3& direction, double minDistance, double maxDistance) const;
	// Bytes the tree holds.
	[[nodiscard]] qint64 memoryBytes() const;

private:
	struct Node {
		float low[3];
		float high[3];
		// Leaf: first and count into m_order; inner: left child (right is
		// left + 1) and count 0.
		int first = 0;
		int count = 0;
	};
	ModelBvhHit query(const ModelVec3& origin, const ModelVec3& direction, double minDistance, double maxDistance, bool any) const;
	QVector<ModelBvhTriangle> m_triangles;
	QVector<int> m_order;
	QVector<Node> m_nodes;
};

} // namespace vibestudio
