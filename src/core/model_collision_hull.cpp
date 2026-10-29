#include "core/model_collision_hull.h"

#include "core/model_collision.h"
#include "core/model_transform.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelCollision)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
struct P
{
	double x = 0, y = 0, z = 0;
};
P operator+(P a, P b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}
P operator-(P a, P b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}
P operator*(P a, double s)
{
	return {a.x * s, a.y * s, a.z * s};
}
double dot(P a, P b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
P cross(P a, P b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double length(P a)
{
	return std::sqrt(dot(a, a));
}
P point(ModelVec3 v)
{
	return {v.x, v.y, v.z};
}
ModelVec3 vec(P p)
{
	return {float(p.x), float(p.y), float(p.z)};
}
bool operator<(const P &a, const P &b)
{
	return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z;
}

// Incremental convex hull over deduplicated points (beneath-beyond with an
// explicit horizon). Original implementation for the modeller.
struct Triangle
{
	int a, b, c;
	P normal;
	double offset;
};
bool triangleFor(const QVector<P> &points, int a, int b, int c, Triangle *out)
{
	const P normal = cross(points[b] - points[a], points[c] - points[a]);
	const double magnitude = length(normal);
	if (!(magnitude > 0))
		return false;
	out->a = a;
	out->b = b;
	out->c = c;
	out->normal = normal * (1 / magnitude);
	out->offset = dot(out->normal, points[a]);
	return true;
}
bool exactHull(const QVector<P> &points, double tolerance, QVector<Triangle> *result, QString *error, const ModelWorkControl &control)
{
	const auto degenerate = [&] {
		return fail(error, Text::tr("The points are coplanar or collinear, so they enclose no volume. Select vertices that span a "
									"solid."));
	};
	if (points.size() < 4)
		return degenerate();
	int a = 0, b = 0;
	for (int i = 1; i < points.size(); ++i)
	{
		if (points[i].x < points[a].x)
			a = i;
		if (points[i].x > points[b].x)
			b = i;
	}
	if (length(points[b] - points[a]) <= tolerance)
	{
		for (int i = 0; i < points.size(); ++i)
			if (length(points[i] - points[a]) > length(points[b] - points[a]))
				b = i;
	}
	const P axis = points[b] - points[a];
	if (length(axis) <= tolerance)
		return degenerate();
	int c = -1;
	double best = tolerance;
	for (int i = 0; i < points.size(); ++i)
	{
		const double distance = length(cross(axis, points[i] - points[a])) / length(axis);
		if (distance > best)
		{
			best = distance;
			c = i;
		}
	}
	if (c < 0)
		return degenerate();
	Triangle base;
	triangleFor(points, a, b, c, &base);
	int d = -1;
	best = tolerance;
	for (int i = 0; i < points.size(); ++i)
	{
		const double distance = std::abs(dot(base.normal, points[i]) - base.offset);
		if (distance > best)
		{
			best = distance;
			d = i;
		}
	}
	if (d < 0)
		return degenerate();
	const P inside = (points[a] + points[b] + points[c] + points[d]) * .25;
	QVector<Triangle> triangles;
	for (const auto &face : {std::array<int, 3>{a, b, c}, std::array<int, 3>{a, d, b}, std::array<int, 3>{a, c, d},
							 std::array<int, 3>{b, d, c}})
	{
		Triangle t;
		triangleFor(points, face[0], face[1], face[2], &t);
		if (dot(t.normal, inside) > t.offset)
			triangleFor(points, face[0], face[2], face[1], &t);
		triangles << t;
	}
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	for (int i = 0; i < points.size(); ++i)
	{
		if (i == a || i == b || i == c || i == d)
			continue;
		if (!work.step())
			return false;
		QVector<Triangle> kept;
		kept.reserve(triangles.size() + 8);
		std::map<std::pair<int, int>, int> directed;
		bool visible = false;
		for (const auto &t : triangles)
		{
			if (dot(t.normal, points[i]) - t.offset <= tolerance)
			{
				kept << t;
				continue;
			}
			visible = true;
			for (const auto &[from, to] : {std::pair{t.a, t.b}, std::pair{t.b, t.c}, std::pair{t.c, t.a}})
				directed[{from, to}] += 1;
		}
		if (!visible)
			continue;
		// A horizon edge belongs to one visible triangle only; keep its direction
		// so the new triangle faces outwards like the one it replaces.
		for (const auto &[edge, count] : directed)
		{
			if (directed.count({edge.second, edge.first}))
				continue;
			Triangle t;
			if (!triangleFor(points, edge.first, edge.second, i, &t))
				return fail(error, Text::tr("The hull could not be built reliably from these points."));
			kept << t;
		}
		triangles = std::move(kept);
	}
	*result = std::move(triangles);
	return true;
}

// A convex polytope as planar polygons, for conservative simplification.
struct Face
{
	P normal;
	double offset;
	QVector<P> polygon;
};
double polytopeVolume(const QVector<Face> &faces)
{
	double volume = 0;
	for (const auto &face : faces)
	{
		P area;
		for (int i = 0; i < face.polygon.size(); ++i)
			area = area + cross(face.polygon[i], face.polygon[(i + 1) % face.polygon.size()]);
		volume += face.offset * dot(face.normal, area) * .5;
	}
	return volume / 3;
}
QVector<P> sortedAround(QVector<P> points, P normal, double tolerance)
{
	QVector<P> unique;
	for (const auto &p : points)
		if (std::none_of(unique.cbegin(), unique.cend(), [&](const P &q) { return length(p - q) <= tolerance; }))
			unique << p;
	if (unique.size() < 3)
		return {};
	P centre;
	for (const auto &p : unique)
		centre = centre + p;
	centre = centre * (1.0 / unique.size());
	P u = unique[0] - centre;
	u = u * (1 / std::max(length(u), 1e-300));
	const P v = cross(normal, u);
	std::sort(unique.begin(), unique.end(), [&](const P &p, const P &q) {
		return std::atan2(dot(p - centre, v), dot(p - centre, u)) < std::atan2(dot(q - centre, v), dot(q - centre, u));
	});
	return unique;
}
// Keeps dot(normal, x) <= offset. False when nothing would change.
bool clip(QVector<Face> *faces, P normal, double offset, double tolerance)
{
	bool cuts = false;
	for (const auto &face : *faces)
		for (const auto &p : face.polygon)
			cuts |= dot(normal, p) - offset > tolerance;
	if (!cuts)
		return false;
	QVector<Face> result;
	QVector<P> cap;
	for (const auto &face : *faces)
	{
		QVector<P> polygon;
		for (int i = 0; i < face.polygon.size(); ++i)
		{
			const P a = face.polygon[i], b = face.polygon[(i + 1) % face.polygon.size()];
			const double da = dot(normal, a) - offset, db = dot(normal, b) - offset;
			if (da <= tolerance)
				polygon << a;
			if (std::abs(da) <= tolerance)
				cap << a;
			if ((da < -tolerance && db > tolerance) || (da > tolerance && db < -tolerance))
			{
				const P crossing = a + (b - a) * (da / (da - db));
				polygon << crossing;
				cap << crossing;
			}
		}
		polygon = sortedAround(polygon, face.normal, tolerance);
		if (polygon.size() >= 3)
			result << Face{face.normal, face.offset, polygon};
	}
	const auto capPolygon = sortedAround(cap, normal, tolerance);
	if (capPolygon.size() >= 3)
		result << Face{normal, offset, capPolygon};
	*faces = std::move(result);
	return true;
}
void countPolytope(const QVector<Face> &faces, double tolerance, int *vertices, int *edges)
{
	QVector<P> unique;
	for (const auto &face : faces)
		for (const auto &p : face.polygon)
			if (std::none_of(unique.cbegin(), unique.cend(), [&](const P &q) { return length(p - q) <= tolerance; }))
				unique << p;
	*vertices = int(unique.size());
	// Euler's formula for a closed convex polytope.
	*edges = int(unique.size() + faces.size()) - 2;
}
QVector<P> polytopeVertices(const QVector<Face> &faces, double tolerance)
{
	QVector<P> unique;
	for (const auto &face : faces)
		for (const auto &p : face.polygon)
			if (std::none_of(unique.cbegin(), unique.cend(), [&](const P &q) { return length(p - q) <= tolerance; }))
				unique << p;
	std::sort(unique.begin(), unique.end());
	return unique;
}
QVector<P> deduplicated(const QVector<ModelVec3> &input)
{
	QVector<P> points;
	points.reserve(input.size());
	for (const auto &v : input)
		points << point(v);
	std::sort(points.begin(), points.end());
	points.erase(std::unique(points.begin(), points.end(), [](const P &a, const P &b) { return a.x == b.x && a.y == b.y && a.z == b.z; }),
				 points.end());
	return points;
}
double extentOf(const QVector<P> &points)
{
	double extent = 0;
	for (const auto &p : points)
		extent = std::max({extent, std::abs(p.x), std::abs(p.y), std::abs(p.z)});
	return std::max(1.0, extent);
}
} // namespace

bool modelCollisionHullBudget(const QString &profile, int maxFaces, ModelCollisionHullBudget *budget, QString *error)
{
	ModelCollisionHullBudget result;
	result.profile = profile;
	if (profile == QStringLiteral("quake") || profile == QStringLiteral("quake2") || profile == QStringLiteral("quake3"))
		result.faces = 128;
	else if (profile == QStringLiteral("doom3-trace"))
	{
		// MAX_TRACEMODEL_VERTS, MAX_TRACEMODEL_EDGES, MAX_TRACEMODEL_POLYS in
		// id Software's Doom 3 idlib/geometry/TraceModel.h (GPL-3.0-or-later).
		result.faces = 16;
		result.edges = 32;
		result.vertices = 32;
	}
	else
		return fail(error, Text::tr("Choose a hull profile: quake, quake2, quake3 or doom3-trace."));
	if (maxFaces != 0)
	{
		if (maxFaces < 4 || maxFaces > result.faces)
			return fail(error, Text::tr("The face budget must be from 4 to %1 for this profile.").arg(result.faces));
		result.faces = maxFaces;
	}
	if (budget)
		*budget = result;
	return true;
}

bool modelCollisionHullShape(const QVector<ModelVec3> &input, ModelCollisionHullShape *shape, QString *error, const ModelWorkControl &control)
{
	const auto points = deduplicated(input);
	const double tolerance = 1e-9 * extentOf(points) * 1000;
	QVector<Triangle> triangles;
	if (!exactHull(points, tolerance, &triangles, error, control))
		return false;
	// Merge triangles that share a plane into one face.
	ModelCollisionHullShape result;
	QVector<int> faceOf(triangles.size(), -1);
	QVector<std::array<double, 4>> planes;
	for (int t = 0; t < triangles.size(); ++t)
	{
		for (int f = 0; f < planes.size(); ++f)
		{
			const auto &plane = planes[f];
			// Single-precision vertices bend a face by rounding; within about 0.1 degree
			// and the point tolerance, triangles share one face.
			if (dot(triangles[t].normal, {plane[0], plane[1], plane[2]}) > 1 - 1e-6 && std::abs(triangles[t].offset - plane[3]) <= tolerance * 10)
			{
				faceOf[t] = f;
				break;
			}
		}
		if (faceOf[t] < 0)
		{
			faceOf[t] = int(planes.size());
			planes.push_back({triangles[t].normal.x, triangles[t].normal.y, triangles[t].normal.z, triangles[t].offset});
		}
	}
	QVector<int> used;
	for (const auto &t : triangles)
		for (int index : {t.a, t.b, t.c})
			if (!used.contains(index))
				used << index;
	std::sort(used.begin(), used.end());
	QHash<int, int> remap;
	for (int i = 0; i < used.size(); ++i)
	{
		remap.insert(used[i], i);
		result.vertices << vec(points[used[i]]);
	}
	for (int f = 0; f < planes.size(); ++f)
	{
		const P normal{planes[f][0], planes[f][1], planes[f][2]};
		QVector<P> polygon;
		QVector<int> indices;
		for (int t = 0; t < triangles.size(); ++t)
		{
			if (faceOf[t] != f)
				continue;
			for (int index : {triangles[t].a, triangles[t].b, triangles[t].c})
				if (!indices.contains(index))
				{
					indices << index;
					polygon << points[index];
				}
		}
		P centre;
		for (const auto &p : polygon)
			centre = centre + p;
		centre = centre * (1.0 / polygon.size());
		P u = polygon[0] - centre;
		u = u * (1 / std::max(length(u), 1e-300));
		const P v = cross(normal, u);
		QVector<int> order(indices.size());
		std::iota(order.begin(), order.end(), 0);
		std::sort(order.begin(), order.end(), [&](int p, int q) {
			return std::atan2(dot(polygon[p] - centre, v), dot(polygon[p] - centre, u)) <
				   std::atan2(dot(polygon[q] - centre, v), dot(polygon[q] - centre, u));
		});
		QVector<int> face;
		for (int i : order)
			face << remap.value(indices[i]);
		result.faces << face;
		result.planes << planes[f];
	}
	QSet<QPair<int, int>> edges;
	for (const auto &face : result.faces)
		for (int i = 0; i < face.size(); ++i)
		{
			const int a = face[i], b = face[(i + 1) % face.size()];
			edges.insert({std::min(a, b), std::max(a, b)});
		}
	result.edges = QVector<QPair<int, int>>(edges.cbegin(), edges.cend());
	std::sort(result.edges.begin(), result.edges.end());
	QVector<Face> polytope;
	for (int f = 0; f < result.faces.size(); ++f)
	{
		Face face{{result.planes[f][0], result.planes[f][1], result.planes[f][2]}, result.planes[f][3], {}};
		for (int index : result.faces[f])
			face.polygon << point(result.vertices[index]);
		polytope << face;
	}
	result.volume = polytopeVolume(polytope);
	if (shape)
		*shape = std::move(result);
	return true;
}

bool fitModelCollisionHull(const QVector<ModelVec3> &input, const ModelCollisionHullBudget &budget, ModelCollisionHullFit *fit, QString *error,
						   const ModelWorkControl &control)
{
	ModelCollisionHullShape exact;
	if (!modelCollisionHullShape(input, &exact, error, control))
		return false;
	const auto points = deduplicated(input);
	const double tolerance = 1e-6 * extentOf(points);
	ModelCollisionHullFit result;
	result.exactVolume = exact.volume;
	const int exactEdges = int(exact.edges.size());
	if (int(exact.faces.size()) <= budget.faces && (budget.edges <= 0 || exactEdges <= budget.edges) &&
		(budget.vertices <= 0 || exact.vertices.size() <= budget.vertices) && exact.vertices.size() <= modelCollisionHullMaxPoints)
	{
		result.vertices = exact.vertices;
		result.faces = int(exact.faces.size());
		result.edges = exactEdges;
		result.vertexCount = int(exact.vertices.size());
		result.volume = exact.volume;
		if (fit)
			*fit = std::move(result);
		return true;
	}
	// Conservative simplification: begin with the bounding box, whose planes
	// touch the points, then add the exact hull's planes (also touching the
	// points) one at a time, each time the one that removes the most volume
	// while the result stays within budget. Every point stays inside.
	P low{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
	P high = low * -1;
	for (const auto &p : points)
	{
		low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
		high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
	}
	const auto corner = [&](int i) { return P{i & 1 ? high.x : low.x, i & 2 ? high.y : low.y, i & 4 ? high.z : low.z}; };
	QVector<Face> polytope{{{-1, 0, 0}, -low.x, {corner(0), corner(4), corner(6), corner(2)}},
						   {{1, 0, 0}, high.x, {corner(1), corner(3), corner(7), corner(5)}},
						   {{0, -1, 0}, -low.y, {corner(0), corner(1), corner(5), corner(4)}},
						   {{0, 1, 0}, high.y, {corner(2), corner(6), corner(7), corner(3)}},
						   {{0, 0, -1}, -low.z, {corner(0), corner(2), corner(3), corner(1)}},
						   {{0, 0, 1}, high.z, {corner(4), corner(5), corner(7), corner(6)}}};
	// Candidates: the exact hull's planes, largest faces first, at most 512.
	QVector<std::pair<double, int>> order;
	for (int f = 0; f < exact.faces.size(); ++f)
	{
		P area;
		const auto &face = exact.faces[f];
		for (int i = 0; i < face.size(); ++i)
			area = area + cross(point(exact.vertices[face[i]]), point(exact.vertices[face[(i + 1) % face.size()]]));
		order.push_back({-length(area), f});
	}
	std::sort(order.begin(), order.end());
	if (order.size() > 512)
		order.resize(512);
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	double volume = polytopeVolume(polytope);
	// Lazy greedy: a plane removes no more volume from a smaller polytope, so a
	// stale estimate is an upper bound. Re-evaluate only the best estimate and
	// take it when it still beats every other estimate.
	const auto evaluate = [&](int c, QVector<Face> *candidate) {
		const auto &plane = exact.planes[order[c].second];
		*candidate = polytope;
		if (!clip(candidate, {plane[0], plane[1], plane[2]}, plane[3], tolerance) || candidate->size() < 4)
			return 0.0;
		return volume - polytopeVolume(*candidate);
	};
	QVector<std::pair<double, int>> estimates;
	for (int c = 0; c < order.size(); ++c)
	{
		if (!work.step())
			return false;
		QVector<Face> candidate;
		const double reduction = evaluate(c, &candidate);
		if (reduction > tolerance)
			estimates.push_back({reduction, c});
	}
	// Ties keep the earlier (larger) face first, so the result is deterministic.
	const auto better = [](const std::pair<double, int> &a, const std::pair<double, int> &b) {
		return a.first < b.first || (a.first == b.first && a.second > b.second);
	};
	std::make_heap(estimates.begin(), estimates.end(), better);
	while (!estimates.isEmpty())
	{
		if (!work.step())
			return false;
		std::pop_heap(estimates.begin(), estimates.end(), better);
		const int c = estimates.last().second;
		estimates.removeLast();
		QVector<Face> candidate;
		const double reduction = evaluate(c, &candidate);
		if (reduction <= tolerance)
			continue;
		if (!estimates.isEmpty() && reduction < estimates.first().first)
		{
			estimates.push_back({reduction, c});
			std::push_heap(estimates.begin(), estimates.end(), better);
			continue;
		}
		int vertices = 0, edges = 0;
		countPolytope(candidate, tolerance, &vertices, &edges);
		if (candidate.size() > budget.faces || (budget.edges > 0 && edges > budget.edges) ||
			(budget.vertices > 0 && vertices > budget.vertices) || vertices > modelCollisionHullMaxPoints)
			continue; // Over budget now; a later, smaller polytope keeps the earlier choice.
		polytope = std::move(candidate);
		volume = polytopeVolume(polytope);
	}
	const auto vertices = polytopeVertices(polytope, tolerance);
	for (const auto &v : vertices)
		result.vertices << vec(v);
	int vertexCount = 0, edges = 0;
	countPolytope(polytope, tolerance, &vertexCount, &edges);
	result.faces = int(polytope.size());
	result.edges = edges;
	result.vertexCount = vertexCount;
	result.volume = volume;
	result.simplified = true;
	if (!work.check())
		return false;
	if (fit)
		*fit = std::move(result);
	return true;
}

bool isModelCollisionHull(const ModelCollisionBox &box)
{
	return !box.hull.isEmpty();
}

QVector<ModelVec3> modelCollisionPoints(const ModelCollisionBox &box)
{
	QVector<ModelVec3> result;
	if (!isModelCollisionHull(box))
	{
		for (const auto corner : modelCollisionCorners(box))
			result << corner;
		return result;
	}
	for (const auto &p : box.hull)
	{
		const auto r = rotateModelVector({p.x * box.size.x, p.y * box.size.y, p.z * box.size.z}, box.rotation);
		result << ModelVec3{r.x + box.centre.x, r.y + box.centre.y, r.z + box.centre.z};
	}
	return result;
}

QVector<QPair<ModelVec3, ModelVec3>> modelCollisionEdges(const ModelCollisionBox &box)
{
	QVector<QPair<ModelVec3, ModelVec3>> result;
	if (!isModelCollisionHull(box))
	{
		const auto corners = modelCollisionCorners(box);
		for (int i = 0; i < 8; ++i)
			for (int axis = 0; axis < 3; ++axis)
			{
				const int j = i ^ (1 << axis);
				if (i < j)
					result << QPair<ModelVec3, ModelVec3>{corners[i], corners[j]};
			}
		return result;
	}
	ModelCollisionHullShape shape;
	if (!modelCollisionHullShape(box.hull, &shape))
		return result;
	const auto points = modelCollisionPoints({box.name, box.centre, box.size, box.rotation, {}, shape.vertices});
	for (const auto &edge : shape.edges)
		result << QPair<ModelVec3, ModelVec3>{points[edge.first], points[edge.second]};
	return result;
}

bool makeModelCollisionHull(const QString &name, const QVector<ModelVec3> &vertices, ModelCollisionBox *box, QString *error)
{
	if (vertices.size() < 4 || vertices.size() > modelCollisionHullMaxPoints)
		return fail(error, Text::tr("A collision hull needs 4 to 256 vertices."));
	ModelVec3 low = vertices[0], high = vertices[0];
	for (const auto &v : vertices)
	{
		low = {std::min(low.x, v.x), std::min(low.y, v.y), std::min(low.z, v.z)};
		high = {std::max(high.x, v.x), std::max(high.y, v.y), std::max(high.z, v.z)};
	}
	const ModelVec3 size{high.x - low.x, high.y - low.y, high.z - low.z};
	if (size.x < 1 || size.y < 1 || size.z < 1)
		return fail(error, Text::tr("A collision hull must be at least one unit across on every axis."));
	ModelCollisionBox result;
	result.name = name;
	result.centre = {(low.x + high.x) * .5f, (low.y + high.y) * .5f, (low.z + high.z) * .5f};
	result.size = size;
	for (const auto &v : vertices)
		result.hull << ModelVec3{std::clamp((v.x - result.centre.x) / size.x, -.5f, .5f), std::clamp((v.y - result.centre.y) / size.y, -.5f, .5f),
								 std::clamp((v.z - result.centre.z) / size.z, -.5f, .5f)};
	if (box)
		*box = std::move(result);
	return true;
}

QString validateModelCollisionHull(const ModelCollisionBox &box)
{
	if (box.hull.isEmpty())
		return {};
	if (box.hull.size() < 4 || box.hull.size() > modelCollisionHullMaxPoints)
		return Text::tr("A collision hull needs 4 to 256 points.");
	for (const auto &p : box.hull)
		if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || std::abs(p.x) > .5001f || std::abs(p.y) > .5001f ||
			std::abs(p.z) > .5001f)
			return Text::tr("Collision hull points must lie within the volume's box, from -0.5 to 0.5 on each axis.");
	ModelCollisionHullShape shape;
	QString problem;
	if (!modelCollisionHullShape(box.hull, &shape, &problem))
		return problem;
	if (shape.faces.size() > 128)
		return Text::tr("A collision hull may have at most 128 faces.");
	return {};
}
} // namespace vibestudio
