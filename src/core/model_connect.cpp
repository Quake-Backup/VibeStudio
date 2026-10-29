// Connect Vertex Path (Blender's J), Connect Vertices and Connect Edges
// (3ds Max's Editable Poly Connect) for triangle meshes.
//
// Original implementation following the documented behaviour of Blender's
// Connect Vertex Path
// (https://docs.blender.org/manual/en/latest/modeling/meshes/editing/vertex/connect_vertex_path.html)
// and 3ds Max's Connect Edges (Autodesk 3ds Max help, Editable Poly > Edit
// Edges > Connect); no code from either is used. Planned cuts go through the
// knife's apply stage (core/model_knife.cpp), which interpolates every pose,
// UVs, normals and skin influences and keeps seams and identities.
#include "core/model_topology_tools.h"

#include "core/model_mesh_tools_p.h"

#include <QMap>

#include <algorithm>
#include <cmath>
#include <deque>
#include <numeric>

namespace vibestudio
{
using namespace model_tools;
namespace
{
constexpr double nearCorner = 1e-4;

// A point of a cut path on the original surface: a corner (geometric group)
// or a point on a geometric edge at `fraction` from its lower group.
struct PathPoint
{
	int group = -1;
	ModelEdge edge{-1, -1};
	double fraction = 0;
};
// Barycentric (B, C) weights of a path point in a face, or false when the
// point is not on that face.
bool weightsIn(const ModelGeometricTopology &topology, const ModelTriangle &t, const PathPoint &point, std::array<double, 2> *result)
{
	const int groups[3]{topology.group(t.a), topology.group(t.b), topology.group(t.c)};
	double w[3]{0, 0, 0};
	if (point.group >= 0)
	{
		bool found = false;
		for (int i = 0; i < 3; ++i)
			if (groups[i] == point.group)
			{
				w[i] = 1;
				found = true;
			}
		if (!found)
			return false;
	}
	else
	{
		int low = -1, high = -1;
		for (int i = 0; i < 3; ++i)
		{
			if (groups[i] == point.edge.first)
				low = i;
			if (groups[i] == point.edge.second)
				high = i;
		}
		if (low < 0 || high < 0)
			return false;
		w[low] = 1 - point.fraction;
		w[high] = point.fraction;
	}
	*result = {w[1], w[2]};
	return true;
}

struct P2d
{
	double x = 0, y = 0;
};
// 2D position of the third corner of a triangle whose base runs from a to b,
// on the side `side` (+1 left, -1 right), from its edge lengths.
P2d apex(P2d a, P2d b, double toA, double toB, double side)
{
	const double dx = b.x - a.x, dy = b.y - a.y, base = std::hypot(dx, dy);
	if (base <= 1e-300)
		return a;
	const double along = (toA * toA - toB * toB + base * base) / (2 * base);
	const double height = std::sqrt(std::max(0.0, toA * toA - along * along));
	const double ux = dx / base, uy = dy / base;
	return {a.x + ux * along - uy * height * side, a.y + uy * along + ux * height * side};
}
double cross2(P2d o, P2d a, P2d b)
{
	return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

class Planner
{
  public:
	Planner(const ModelSurface &surface, const ModelGeometricTopology &topology, int frame) : surface(surface), topology(topology), frame(frame)
	{
	}
	// Faces sharing a geometric edge, by group pair.
	[[nodiscard]] QVector<int> neighbours(int face) const
	{
		QVector<int> result;
		const auto &t = surface.triangles[face];
		for (auto edge : {topology.geometricEdge(t.a, t.b), topology.geometricEdge(t.b, t.c), topology.geometricEdge(t.c, t.a)})
			for (int other : topology.edgeFaces.value(edge))
				if (other != face && !result.contains(other))
					result.append(other);
		std::sort(result.begin(), result.end());
		return result;
	}
	[[nodiscard]] bool hasGroup(int face, int group) const
	{
		const auto &t = surface.triangles[face];
		return topology.group(t.a) == group || topology.group(t.b) == group || topology.group(t.c) == group;
	}
	// The fewest faces from one around `from` to one around `to`, crossing
	// geometric edges; lowest face indices win ties. Empty when unreachable.
	[[nodiscard]] QVector<int> strip(int from, int to, ModelWorkProgress &work) const
	{
		QHash<int, int> previous;
		std::deque<int> queue;
		for (int face : topology.groupFaces[from])
		{
			previous.insert(face, -1);
			queue.push_back(face);
		}
		while (!queue.empty())
		{
			if (!work.step())
				return {};
			const int face = queue.front();
			queue.pop_front();
			if (hasGroup(face, to))
			{
				QVector<int> path;
				for (int at = face; at >= 0; at = previous.value(at))
					path.prepend(at);
				return path;
			}
			for (int next : neighbours(face))
			{
				if (previous.contains(next))
					continue;
				previous.insert(next, face);
				queue.push_back(next);
			}
		}
		return {};
	}
	// Straight cut from corner group `from` to corner group `to` across the
	// strip, unfolded into a plane in the reference pose. False with a reason
	// when the straight line leaves the strip or grazes a vertex.
	bool cut(const QVector<int> &faces, int from, int to, QVector<ModelKnifeCut> *cuts, QString *error) const
	{
		QHash<int, P2d> placed;
		const auto positionOf = [&](int group) { return position(surface, frame, group); };
		const auto groupsOf = [&](int face)
		{
			const auto &t = surface.triangles[face];
			return std::array<int, 3>{topology.group(t.a), topology.group(t.b), topology.group(t.c)};
		};
		// The first face, in its own winding.
		{
			const auto g = groupsOf(faces[0]);
			placed.insert(g[0], {0, 0});
			placed.insert(g[1], {length(positionOf(g[1]) - positionOf(g[0])), 0});
			placed.insert(g[2], apex(placed[g[0]], placed[g[1]], length(positionOf(g[2]) - positionOf(g[0])), length(positionOf(g[2]) - positionOf(g[1])), 1));
		}
		QVector<ModelEdge> shared;
		for (int i = 1; i < faces.size(); ++i)
		{
			const auto a = groupsOf(faces[i - 1]), b = groupsOf(faces[i]);
			QVector<int> common;
			int previousApex = -1, nextApex = -1;
			for (int g : a)
			{
				if (std::find(b.cbegin(), b.cend(), g) != b.cend())
					common.append(g);
				else
					previousApex = g;
			}
			for (int g : b)
				if (std::find(a.cbegin(), a.cend(), g) == a.cend())
					nextApex = g;
			if (common.size() != 2 || previousApex < 0 || nextApex < 0 || placed.contains(nextApex))
				return fail(error, Text::tr("Connect cannot unfold the faces between the vertices into a plane. Connect through nearer vertices."));
			const P2d p = placed[common[0]], q = placed[common[1]];
			const double side = cross2(p, q, placed[previousApex]) > 0 ? -1 : 1;
			placed.insert(nextApex, apex(p, q, length(positionOf(nextApex) - positionOf(common[0])), length(positionOf(nextApex) - positionOf(common[1])), side));
			shared.append(modelEdge(common[0], common[1]));
		}
		const P2d start = placed[from], end = placed[to];
		PathPoint entry;
		entry.group = from;
		double last = 0;
		for (int i = 0; i < faces.size(); ++i)
		{
			PathPoint exit;
			if (i + 1 < faces.size())
			{
				const auto edge = shared[i];
				const P2d p = placed[edge.first], q = placed[edge.second];
				// start + λ (end - start) = p + s (q - p)
				const double rx = end.x - start.x, ry = end.y - start.y, sx = q.x - p.x, sy = q.y - p.y;
				const double denominator = rx * sy - ry * sx;
				if (std::abs(denominator) <= 1e-12)
					return fail(error, Text::tr("The straight path between the vertices runs along an edge. Connect through nearer vertices."));
				const double lambda = ((p.x - start.x) * sy - (p.y - start.y) * sx) / denominator;
				const double s = ((p.x - start.x) * ry - (p.y - start.y) * rx) / denominator;
				if (s < nearCorner || s > 1 - nearCorner || lambda <= last || lambda >= 1)
					return fail(error, Text::tr("The straight path between the vertices leaves the faces between them or passes too close to "
												"another vertex. Connect through that vertex, or through nearer vertices."));
				last = lambda;
				exit.edge = edge;
				exit.fraction = s;
			}
			else
				exit.group = to;
			ModelKnifeCut planned;
			planned.face = faces[i];
			if (!weightsIn(topology, surface.triangles[faces[i]], entry, &planned.a) || !weightsIn(topology, surface.triangles[faces[i]], exit, &planned.b))
				return fail(error, Text::tr("Connect could not place its cut on a face."));
			cuts->append(planned);
			entry = exit;
		}
		return true;
	}

  private:
	const ModelSurface &surface;
	const ModelGeometricTopology &topology;
	int frame;
};

// Turns the diagonal shared by two triangles when the vertices to connect are
// their outer corners: the quad keeps its two triangles, now split along the
// new edge, as connecting opposite corners of a quad does. Needs the shared
// edge on one UV island and a convex quad in every pose.
bool turnDiagonal(ModelSurface *surface, const ModelGeometricTopology &topology, const QVector<int> &faces, int from, int to)
{
	if (faces.size() != 2)
		return false;
	const auto t1 = surface->triangles[faces[0]], t2 = surface->triangles[faces[1]];
	const int c1[3]{t1.a, t1.b, t1.c}, c2[3]{t2.a, t2.b, t2.c};
	for (int i = 0; i < 3; ++i)
	{
		const int u = c1[i], p = c1[(i + 1) % 3], q = c1[(i + 2) % 3];
		if (topology.group(u) != from)
			continue;
		// t2 must run q -> p by index (no seam across the diagonal), with v opposite.
		for (int j = 0; j < 3; ++j)
		{
			if (c2[j] != q || c2[(j + 1) % 3] != p || topology.group(c2[(j + 2) % 3]) != to)
				continue;
			const int v = c2[(j + 2) % 3];
			const ModelTriangle first{u, p, v}, second{v, q, u};
			if (collapsedInAnyFrame(*surface, first) || collapsedInAnyFrame(*surface, second) || flipsInAnyFrame(*surface, t1, first) ||
				flipsInAnyFrame(*surface, t1, second) || flipsInAnyFrame(*surface, t2, first) || flipsInAnyFrame(*surface, t2, second))
				return false;
			surface->triangles[faces[0]] = first;
			surface->triangles[faces[1]] = second;
			return true;
		}
	}
	return false;
}

bool finish(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, const QVector<ModelKnifeCut> &cuts, const QSet<ModelEdge> &turned,
			QString *error, const ModelWorkControl &control, ModelEditReport *report)
{
	if (cuts.isEmpty())
	{
		selection->faces.clear();
		selection->vertices.clear();
		selection->edges = turned;
		if (report)
		{
			QVector<int> sources(mesh->surfaces[selection->surface].triangles.size());
			std::iota(sources.begin(), sources.end(), 0);
			report->sourceFaces.insert(selection->surface, sources);
		}
		return true;
	}
	if (!applyKnifeCuts(mesh, edit, selection, cuts, error, control, report))
		return false;
	selection->edges += turned;
	return true;
}
} // namespace

bool connectModelVertices(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control,
						  ModelEditReport *report)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	auto &surface = candidate->surfaces[selection->surface];
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error, control))
		return false;
	const auto &path = edit.tool.topology.vertices;
	for (int v : path)
		if (v < 0 || v >= surface.vertexCount)
			return fail(error, Text::tr("A vertex in the connect path does not exist."));
	// The pairs to connect: the given path in order, the two selected
	// vertices, or every pair of selected opposite corners of a quad.
	QVector<QPair<int, int>> pairs;
	QSet<int> groups;
	for (int v : selectedVertexSet(surface, *selection))
		groups.insert(topology.group(v));
	if (!path.isEmpty())
	{
		for (int i = 0; i + 1 < path.size(); ++i)
			if (topology.group(path[i]) != topology.group(path[i + 1]))
				pairs.append({topology.group(path[i]), topology.group(path[i + 1])});
	}
	else if (groups.size() == 2)
	{
		auto ordered = groups.values();
		std::sort(ordered.begin(), ordered.end());
		pairs.append({ordered[0], ordered[1]});
	}
	else if (groups.size() > 2)
	{
		ModelQuadTopology quads;
		if (!buildModelQuadTopology(surface, edit.tool.referenceFrame, edit.tool.maxAngle, &quads, error, control))
			return false;
		for (const auto &element : quads.elements)
		{
			if (!element.quad())
				continue;
			for (int side = 0; side < 2; ++side)
			{
				const int a = quads.geometry.group(element.corners[side]), b = quads.geometry.group(element.corners[side + 2]);
				if (groups.contains(a) && groups.contains(b))
					pairs.append({std::min(a, b), std::max(a, b)});
			}
		}
	}
	if (pairs.isEmpty())
		return fail(error, Text::tr("Select two vertices to connect, a path of vertices, or opposite corners of quads."));
	Planner planner(surface, topology, edit.tool.referenceFrame);
	QVector<ModelKnifeCut> cuts;
	QSet<ModelEdge> turned;
	QSet<int> busy;
	int connected = 0;
	for (const auto &[from, to] : pairs)
	{
		if (!work.step())
			return false;
		// Vertices on one face already share an edge.
		const auto around = topology.groupFaces[from];
		if (std::any_of(around.cbegin(), around.cend(), [&](int face) { return planner.hasGroup(face, to); }))
			continue;
		const auto faces = planner.strip(from, to, work);
		if (faces.isEmpty())
			return work.check() && fail(error, Text::tr("Vertices %1 and %2 are not on connected faces of this surface.").arg(from).arg(to));
		const bool free = std::none_of(faces.cbegin(), faces.cend(), [&](int face) { return busy.contains(face); });
		if (free && turnDiagonal(&surface, topology, faces, from, to))
		{
			const auto &t = surface.triangles[faces[0]];
			turned.insert(modelEdge(t.a, t.c));
			// Later pairs plan on the turned faces.
			if (!buildModelGeometricTopology(surface, &topology, error, control))
				return false;
		}
		else if (!planner.cut(faces, from, to, &cuts, error))
			return false;
		for (int face : faces)
			busy.insert(face);
		++connected;
	}
	if (connected == 0)
		return fail(error, Text::tr("The selected vertices already share edges. Nothing to connect."));
	return finish(candidate, edit, selection, cuts, turned, error, control, report);
}

bool connectModelEdges(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control,
					   ModelEditReport *report)
{
	Q_UNUSED(report);
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	auto &surface = candidate->surfaces[selection->surface];
	ModelQuadTopology quads;
	if (!buildModelQuadTopology(surface, edit.tool.referenceFrame, edit.tool.maxAngle, &quads, error, control))
		return false;
	const auto &topology = quads.geometry;
	QSet<ModelEdge> chosen;
	for (auto edge : selection->edges)
		chosen.insert(topology.geometricEdge(edge.first, edge.second));
	if (chosen.isEmpty())
		return fail(error, Text::tr("Select edges on opposite sides of quads to connect."));
	// Each quad with exactly one pair of opposite sides selected gets cuts.
	struct Task
	{
		int element = -1, side = 0;
	};
	QVector<Task> tasks;
	for (int e = 0; e < quads.elements.size(); ++e)
	{
		const auto &element = quads.elements[e];
		if (!element.quad())
			continue;
		const auto sideEdge = [&](int side) { return topology.geometricEdge(element.corners[side], element.corners[(side + 1) % 4]); };
		const bool first = chosen.contains(sideEdge(0)) && chosen.contains(sideEdge(2));
		const bool second = chosen.contains(sideEdge(1)) && chosen.contains(sideEdge(3));
		if (first && second)
			return fail(error, Text::tr("A quad has all four sides selected, so Connect cannot tell which way to cut. Select one pair of "
										"opposite sides per quad."));
		if (first || second)
			tasks.append({e, first ? 0 : 1});
	}
	if (tasks.isEmpty())
		return fail(error, Text::tr("Select edges on opposite sides of quads to connect. Connect works across quads, not single triangles."));
	// Orient every selected edge along its ring so a slide moves every cut the
	// same way: in a quad, side s runs corners[s] -> corners[s+1] and the
	// parallel side o runs corners[o+1] -> corners[o]. forward[edge] says the
	// ring runs from the edge's lower group.
	QHash<ModelEdge, QVector<int>> tasksOf;
	const auto sides = [&](const Task &task)
	{
		const auto &element = quads.elements[task.element];
		const int s = task.side, o = task.side + 2;
		return std::pair{topology.geometricEdge(element.corners[s], element.corners[s + 1]),
						 topology.geometricEdge(element.corners[o], element.corners[(o + 1) % 4])};
	};
	for (int i = 0; i < tasks.size(); ++i)
	{
		const auto [first, second] = sides(tasks[i]);
		tasksOf[first].append(i);
		tasksOf[second].append(i);
	}
	QHash<ModelEdge, bool> forward;
	for (int start = 0; start < tasks.size(); ++start)
	{
		const auto seed = sides(tasks[start]).first;
		if (forward.contains(seed))
			continue;
		forward.insert(seed, true);
		QVector<ModelEdge> queue{seed};
		while (!queue.isEmpty())
		{
			if (!work.step())
				return false;
			const auto edge = queue.takeLast();
			for (int index : tasksOf.value(edge))
			{
				const auto &task = tasks[index];
				const auto &element = quads.elements[task.element];
				const int s = task.side, o = task.side + 2;
				const auto [first, second] = sides(task);
				// Whether each side's lower group is where the ring enters it.
				const bool firstLow = topology.group(element.corners[s]) == first.first;
				const bool secondLow = topology.group(element.corners[(o + 1) % 4]) == second.first;
				const bool fromFirst = edge == first;
				const bool aligned = forward.value(edge) == (fromFirst ? firstLow : secondLow);
				const auto other = fromFirst ? second : first;
				const bool want = aligned ? (fromFirst ? secondLow : firstLow) : !(fromFirst ? secondLow : firstLow);
				if (!forward.contains(other))
				{
					forward.insert(other, want);
					queue.append(other);
				}
				else if (forward.value(other) != want && edit.tool.slide != 0)
					return fail(error, Text::tr("The selected edges twist around a ring, so a slide has no single direction. Use a slide of zero."));
			}
		}
	}
	// Evenly spaced points on every selected side, slid along the ring.
	const int count = edit.tool.cuts;
	const double spacing = 1.0 / (count + 1);
	QHash<ModelEdge, QVector<double>> fractions; // geometric edge -> ascending fractions from its lower group
	for (auto it = forward.cbegin(); it != forward.cend(); ++it)
	{
		QVector<double> values;
		for (int k = 1; k <= count; ++k)
		{
			const double u = std::clamp((k + edit.tool.slide) * spacing, nearCorner, 1 - nearCorner);
			values.append(it.value() ? u : 1 - u);
		}
		std::sort(values.begin(), values.end());
		for (int k = 1; k < values.size(); ++k)
			if (values[k] - values[k - 1] < nearCorner)
				return fail(error, Text::tr("The cuts are too close together. Use fewer cuts or a smaller slide."));
		fractions.insert(it.key(), values);
	}
	QSet<int> taskFaces;
	for (const auto &task : tasks)
		taskFaces << quads.elements[task.element].faces[0] << quads.elements[task.element].faces[1];
	// Faces outside the cut quads that use a split edge are re-triangulated
	// with the new points, so no T-junction remains.
	QSet<int> neighbours;
	for (auto it = fractions.cbegin(); it != fractions.cend(); ++it)
		for (int face : topology.edgeFaces.value(it.key()))
			if (!taskFaces.contains(face))
				neighbours.insert(face);
	if (!reserveCapacity(*candidate, selection->surface, qint64(fractions.size()) * count * 2,
						 qint64(tasks.size()) * (2 * count) + qint64(neighbours.size()) * count * 2, error))
		return false;
	// One vertex per index edge and fraction; seam copies on other index
	// edges of the same geometric edge share its position and identity.
	QHash<ModelEdge, QVector<int>> created; // index edge -> vertices from its lower-group end
	QHash<QPair<ModelEdge, int>, int> identities;
	const auto pointsAlong = [&](int from, int to)
	{
		const auto geometric = topology.geometricEdge(from, to);
		const auto key = modelEdge(from, to);
		const int low = topology.group(from) == geometric.first ? from : to, high = low == from ? to : from;
		if (!created.contains(key))
		{
			QVector<int> points;
			const auto values = fractions.value(geometric);
			for (int k = 0; k < values.size(); ++k)
			{
				const int index = appendLerp(&surface, low, high, values[k]);
				if (!surface.topologyVertices.isEmpty())
				{
					const auto identity = qMakePair(geometric, k);
					if (identities.contains(identity))
						surface.topologyVertices[index] = identities.value(identity);
					else
						identities.insert(identity, surface.topologyVertices[index]);
				}
				points.append(index);
			}
			created.insert(key, points);
		}
		auto points = created.value(key);
		if (from != low)
			std::reverse(points.begin(), points.end());
		return points;
	};
	const int reference = edit.tool.referenceFrame;
	QVector<QPair<int, QVector<ModelTriangle>>> replacements; // original face -> its new triangles
	QHash<int, QPair<int, int>> quadOf;							 // face -> both faces of its quad
	QSet<ModelEdge> joined;
	for (const auto &task : tasks)
	{
		if (!work.step())
			return false;
		const auto &element = quads.elements[task.element];
		const int s = task.side, o = task.side + 2;
		const int cs = element.corners[s], cs1 = element.corners[s + 1], co = element.corners[o], co1 = element.corners[(o + 1) % 4];
		// Side s runs cs -> cs1 and, parallel to it, side o runs co1 -> co;
		// strip i lies between the i-th and (i+1)-th points of both.
		QVector<int> a = pointsAlong(cs, cs1), b = pointsAlong(co1, co);
		a.prepend(cs);
		a.append(cs1);
		b.prepend(co1);
		b.append(co);
		QVector<ModelTriangle> triangles;
		for (int i = 0; i + 1 < a.size(); ++i)
		{
			const int p = a[i], q = a[i + 1], r = b[i + 1], w = b[i];
			if (i > 0)
				joined.insert(modelEdge(p, w));
			// The shorter diagonal of each strip, in the reference pose.
			if (length(position(surface, reference, p) - position(surface, reference, r)) <=
				length(position(surface, reference, q) - position(surface, reference, w)))
				triangles << ModelTriangle{p, q, r} << ModelTriangle{p, r, w};
			else
				triangles << ModelTriangle{p, q, w} << ModelTriangle{q, r, w};
		}
		replacements.append(qMakePair(element.faces[0], triangles));
		replacements.append(qMakePair(element.faces[1], QVector<ModelTriangle>{}));
		quadOf.insert(element.faces[0], {element.faces[0], element.faces[1]});
	}
	for (int face : std::as_const(neighbours))
	{
		if (!work.step())
			return false;
		const auto t = surface.triangles[face];
		QVector<int> polygon;
		for (const auto &[from, to] : {std::pair{t.a, t.b}, std::pair{t.b, t.c}, std::pair{t.c, t.a}})
		{
			polygon.append(from);
			if (fractions.contains(topology.geometricEdge(from, to)))
				polygon += pointsAlong(from, to);
		}
		QVector<ModelTriangle> triangles;
		if (!triangulatePolygon(surface, reference, polygon, unit(faceCross(surface, reference, t)), &triangles))
			return fail(error, Text::tr("A face beside the connected edges cannot take the new points. Connect fewer edges."));
		replacements.append(qMakePair(face, triangles));
	}
	// Every new triangle keeps the facing of the face(s) it replaces, in every pose.
	for (const auto &[face, triangles] : std::as_const(replacements))
		for (const auto &triangle : triangles)
		{
			bool bad = collapsedInAnyFrame(surface, triangle);
			if (quadOf.contains(face))
			{
				const auto [first, second] = quadOf.value(face);
				bad = bad || flipsInAnyFrame(surface, surface.triangles[first], triangle) || flipsInAnyFrame(surface, surface.triangles[second], triangle);
			}
			else
				bad = bad || flipsInAnyFrame(surface, surface.triangles[face], triangle);
			if (bad)
				return fail(error, Text::tr("A connecting cut would collapse or fold a face in some pose. Use fewer cuts or a smaller slide."));
		}
	// Faces keep their indices where they can; the rest are appended.
	QVector<int> freed;
	for (const auto &[face, triangles] : std::as_const(replacements))
		if (triangles.isEmpty())
			freed.append(face);
	for (const auto &[face, triangles] : std::as_const(replacements))
	{
		if (triangles.isEmpty())
			continue;
		surface.triangles[face] = triangles.first();
		for (int i = 1; i < triangles.size(); ++i)
		{
			if (!freed.isEmpty())
				surface.triangles[freed.takeLast()] = triangles[i];
			else
				surface.triangles.append(triangles[i]);
		}
	}
	// Every quad yields at least two triangles, so no freed slot is left over.
	if (!freed.isEmpty())
		return fail(error, Text::tr("Connect could not place its cut on a face."));
	// Split seam edges stay seams along their new pieces.
	QSet<ModelEdge> seams = surface.uvSeams;
	for (auto it = created.cbegin(); it != created.cend(); ++it)
	{
		if (!surface.uvSeams.contains(it.key()))
			continue;
		const auto geometric = topology.geometricEdge(it.key().first, it.key().second);
		const int low = topology.group(it.key().first) == geometric.first ? it.key().first : it.key().second;
		QVector<int> chain{low};
		chain += it.value();
		chain.append(low == it.key().first ? it.key().second : it.key().first);
		for (int i = 0; i + 1 < chain.size(); ++i)
			seams.insert(modelEdge(chain[i], chain[i + 1]));
	}
	surface.uvSeams = seams;
	pruneSeams(&surface);
	selection->faces.clear();
	selection->vertices.clear();
	selection->edges = joined;
	return work.check();
}
} // namespace vibestudio
