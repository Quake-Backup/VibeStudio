#include "core/model_bvh.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {

namespace {

constexpr int kLeafSize = 8;

double component(const ModelVec3& v, int axis)
{
	return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}

} // namespace

bool ModelBvh::build(QVector<ModelBvhTriangle> triangles, QString* error, const ModelWorkControl& control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	m_triangles = std::move(triangles);
	m_order.resize(m_triangles.size());
	m_nodes.clear();
	if (m_triangles.isEmpty()) {
		return work.check();
	}
	QVector<std::array<float, 3>> centroids(m_triangles.size());
	for (int index = 0; index < m_triangles.size(); ++index) {
		m_order[index] = index;
		const auto& c = m_triangles.at(index).corners;
		centroids[index] = {float((c[0].x + c[1].x + c[2].x) / 3.0), float((c[0].y + c[1].y + c[2].y) / 3.0), float((c[0].z + c[1].z + c[2].z) / 3.0)};
	}
	m_nodes.reserve(m_triangles.size() / 2 + 2);
	struct Task {
		int node, first, count;
	};
	m_nodes.append(Node{});
	QVector<Task> stack{{0, 0, int(m_triangles.size())}};
	while (!stack.isEmpty()) {
		if (!work.step()) {
			m_nodes.clear();
			return false;
		}
		const Task task = stack.takeLast();
		Node node;
		float centroidLow[3] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
		float centroidHigh[3] = {-centroidLow[0], -centroidLow[1], -centroidLow[2]};
		for (int axis = 0; axis < 3; ++axis) {
			node.low[axis] = std::numeric_limits<float>::max();
			node.high[axis] = -std::numeric_limits<float>::max();
		}
		for (int i = task.first; i < task.first + task.count; ++i) {
			const int t = m_order.at(i);
			for (const ModelVec3& corner : m_triangles.at(t).corners) {
				for (int axis = 0; axis < 3; ++axis) {
					const float value = float(component(corner, axis));
					node.low[axis] = std::min(node.low[axis], value);
					node.high[axis] = std::max(node.high[axis], value);
				}
			}
			for (int axis = 0; axis < 3; ++axis) {
				centroidLow[axis] = std::min(centroidLow[axis], centroids.at(t)[size_t(axis)]);
				centroidHigh[axis] = std::max(centroidHigh[axis], centroids.at(t)[size_t(axis)]);
			}
		}
		int axis = 0;
		for (int a = 1; a < 3; ++a) {
			if (centroidHigh[a] - centroidLow[a] > centroidHigh[axis] - centroidLow[axis]) {
				axis = a;
			}
		}
		if (task.count <= kLeafSize || centroidHigh[axis] - centroidLow[axis] <= 0.0f) {
			node.first = task.first;
			node.count = task.count;
			m_nodes[task.node] = node;
			continue;
		}
		const int middle = task.first + task.count / 2;
		std::nth_element(m_order.begin() + task.first, m_order.begin() + middle, m_order.begin() + task.first + task.count,
			[&](int a, int b) { return centroids.at(a)[size_t(axis)] < centroids.at(b)[size_t(axis)]; });
		node.first = int(m_nodes.size());
		node.count = 0;
		m_nodes[task.node] = node;
		m_nodes.append(Node{});
		m_nodes.append(Node{});
		stack.append({node.first, task.first, middle - task.first});
		stack.append({node.first + 1, middle, task.first + task.count - middle});
	}
	return work.check();
}

ModelBvhHit ModelBvh::query(const ModelVec3& origin, const ModelVec3& direction, double minDistance, double maxDistance, bool any) const
{
	ModelBvhHit best;
	if (m_nodes.isEmpty()) {
		return best;
	}
	const double inverse[3] = {1.0 / (direction.x != 0.0f ? direction.x : 1e-30), 1.0 / (direction.y != 0.0f ? direction.y : 1e-30),
		1.0 / (direction.z != 0.0f ? direction.z : 1e-30)};
	const double o[3] = {origin.x, origin.y, origin.z};
	double limit = maxDistance;
	int stack[128];
	int top = 0;
	stack[top++] = 0;
	while (top > 0) {
		const Node& node = m_nodes.at(stack[--top]);
		// Slab test.
		double enter = minDistance, leave = limit;
		bool missed = false;
		for (int axis = 0; axis < 3 && !missed; ++axis) {
			double t0 = (node.low[axis] - o[axis]) * inverse[axis];
			double t1 = (node.high[axis] - o[axis]) * inverse[axis];
			if (t0 > t1) {
				std::swap(t0, t1);
			}
			enter = std::max(enter, t0);
			leave = std::min(leave, t1);
			missed = enter > leave;
		}
		if (missed) {
			continue;
		}
		if (node.count == 0) {
			if (top + 2 <= 128) {
				stack[top++] = node.first;
				stack[top++] = node.first + 1;
			}
			continue;
		}
		for (int i = node.first; i < node.first + node.count; ++i) {
			const int index = m_order.at(i);
			const auto& c = m_triangles.at(index).corners;
			// Moeller-Trumbore.
			const double e1[3] = {double(c[1].x) - c[0].x, double(c[1].y) - c[0].y, double(c[1].z) - c[0].z};
			const double e2[3] = {double(c[2].x) - c[0].x, double(c[2].y) - c[0].y, double(c[2].z) - c[0].z};
			const double d[3] = {direction.x, direction.y, direction.z};
			const double p[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
			const double det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
			if (std::abs(det) < 1e-14) {
				continue;
			}
			const double invDet = 1.0 / det;
			const double s[3] = {o[0] - c[0].x, o[1] - c[0].y, o[2] - c[0].z};
			const double u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * invDet;
			if (u < 0.0 || u > 1.0) {
				continue;
			}
			const double q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
			const double v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * invDet;
			if (v < 0.0 || u + v > 1.0) {
				continue;
			}
			const double t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * invDet;
			if (t <= minDistance || t >= limit) {
				continue;
			}
			best.hit = true;
			best.distance = t;
			best.u = u;
			best.v = v;
			best.index = index;
			if (any) {
				return best;
			}
			limit = t;
		}
	}
	return best;
}

ModelBvhHit ModelBvh::nearest(const ModelVec3& origin, const ModelVec3& direction, double minDistance, double maxDistance) const
{
	return query(origin, direction, minDistance, maxDistance, false);
}

bool ModelBvh::occluded(const ModelVec3& origin, const ModelVec3& direction, double minDistance, double maxDistance) const
{
	return query(origin, direction, minDistance, maxDistance, true).hit;
}

qint64 ModelBvh::memoryBytes() const
{
	return qint64(m_triangles.size()) * qint64(sizeof(ModelBvhTriangle)) + qint64(m_order.size()) * 4 + qint64(m_nodes.size()) * qint64(sizeof(Node));
}

} // namespace vibestudio
