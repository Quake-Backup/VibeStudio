// Mark Sharp and Clear Sharp: explicit hard edges for idTech vertex normals.
//
// Original implementation following Blender's documented Mark Sharp / Clear
// Sharp edge operators
// (https://docs.blender.org/manual/en/latest/modeling/meshes/editing/edge/edge_data.html)
// and the smoothing behaviour of MilkShape 3D and 3ds Max smoothing groups; no
// code from either is used. MD3, MD2 and MDL store one normal per vertex, so a
// hard edge is two vertices with different normals: marking splits corner
// indices where sharp edges separate the faces around a vertex, and clearing
// welds them back where the UVs allow. Normals are the angle-weighted average
// of each fan's faces in every pose.
#include "core/model_topology_tools.h"

#include "core/model_mesh_tools_p.h"
#include "core/model_vertex_origins.h"

#include <QMap>

#include <algorithm>
#include <numeric>

namespace vibestudio
{
using namespace model_tools;

bool markModelSharpEdges(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	const bool mark = edit.kind == ModelEditKind::MarkSharp;
	auto &surface = candidate->surfaces[selection->surface];
	const int reference = edit.tool.referenceFrame;
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error, control))
		return false;
	QSet<ModelEdge> chosen;
	for (auto edge : selection->edges)
		chosen.insert(topology.geometricEdge(edge.first, edge.second));
	if (chosen.isEmpty())
		return fail(error, mark ? Text::tr("Select the edges to mark sharp.") : Text::tr("Select the sharp edges to clear."));
	QSet<int> groups;
	for (auto edge : std::as_const(chosen))
		if (topology.edgeFaces.value(edge).size() >= 2)
			groups << edge.first << edge.second;
	if (groups.isEmpty())
		return fail(error, Text::tr("Only edges between two faces can be sharp; border edges are always hard."));
	// The corner of `face` at `group`, before anything changes.
	const auto cornerAt = [&](int face, int group)
	{
		const auto &t = surface.triangles[face];
		return topology.group(t.a) == group ? t.a : topology.group(t.b) == group ? t.b : t.c;
	};
	struct Fan
	{
		int group = -1;
		QVector<int> faces;
		QHash<int, int> corners; // face -> its corner index at the group
	};
	QVector<Fan> fans;
	for (int group : std::as_const(groups))
	{
		if (!work.step())
			return false;
		const auto around = topology.groupFaces[group];
		QVector<int> parent(around.size());
		std::iota(parent.begin(), parent.end(), 0);
		const auto root = [&](int i)
		{
			while (parent[i] != i)
				i = parent[i] = parent[parent[i]];
			return i;
		};
		for (int i = 0; i < around.size(); ++i)
			for (int j = i + 1; j < around.size(); ++j)
			{
				// Neighbours share a geometric edge at this vertex.
				const auto &a = surface.triangles[around[i]], &b = surface.triangles[around[j]];
				int other = -1;
				for (int x : {a.a, a.b, a.c})
					for (int y : {b.a, b.b, b.c})
						if (topology.group(x) == topology.group(y) && topology.group(x) != group)
							other = topology.group(x);
				if (other < 0)
					continue;
				const bool selected = chosen.contains(modelEdge(group, other));
				// An edge is smooth now when both corners share one normal.
				const int ca = cornerAt(around[i], group), cb = cornerAt(around[j], group);
				const auto na = surface.frames[reference].normals[ca], nb = surface.frames[reference].normals[cb];
				const bool smooth = ca == cb || dot(unit(point(na)), unit(point(nb))) >= 0.9999;
				if (mark ? (smooth && !selected) : (smooth || selected))
					parent[root(i)] = root(j);
			}
		QMap<int, Fan> sets;
		for (int i = 0; i < around.size(); ++i)
		{
			auto &fan = sets[root(i)];
			fan.group = group;
			fan.faces.append(around[i]);
			fan.corners.insert(around[i], cornerAt(around[i], group));
		}
		for (const auto &fan : std::as_const(sets))
			fans.append(fan);
	}
	// New indices: Mark gives each extra fan that shares an index its own
	// copy (the fan with the lowest face keeps it); Clear welds copies in one
	// fan that share a UV to the lowest index.
	QVector<QHash<int, int>> remaps(fans.size());
	QSet<int> merged;
	if (mark)
	{
		qint64 added = 0;
		QHash<QPair<int, int>, int> keeper; // (group, index) -> fan keeping it
		for (int f = 0; f < fans.size(); ++f)
			for (int corner : std::as_const(fans[f].corners))
			{
				const auto key = qMakePair(fans[f].group, corner);
				if (!keeper.contains(key))
					keeper.insert(key, f);
				else if (keeper.value(key) != f && !remaps[f].contains(corner))
				{
					remaps[f].insert(corner, -1);
					++added;
				}
			}
		if (!reserveCapacity(*candidate, selection->surface, added, 0, error))
			return false;
		for (auto &remap : remaps)
			for (auto it = remap.begin(); it != remap.end(); ++it)
				it.value() = appendCopy(&surface, it.key());
	}
	else
	{
		for (int f = 0; f < fans.size(); ++f)
		{
			const auto corners = fans[f].corners.values();
			for (int corner : corners)
			{
				int target = corner;
				for (int other : corners)
					if (other < target && surface.texCoords[other].u == surface.texCoords[corner].u && surface.texCoords[other].v == surface.texCoords[corner].v)
						target = other;
				if (target != corner)
				{
					remaps[f].insert(corner, target);
					merged.insert(corner);
				}
			}
		}
	}
	bool changed = false;
	QSet<int> touched;
	for (int f = 0; f < fans.size(); ++f)
	{
		if (!work.step())
			return false;
		auto &fan = fans[f];
		for (int face : std::as_const(fan.faces))
		{
			const int corner = fan.corners.value(face), replacement = remaps[f].value(corner, corner);
			if (replacement == corner)
				continue;
			auto &t = surface.triangles[face];
			for (int *slot : {&t.a, &t.b, &t.c})
				if (*slot == corner)
					*slot = replacement;
			fan.corners.insert(face, replacement);
			changed = true;
		}
		// The fan's normal, angle-weighted, in every pose.
		QSet<int> corners;
		for (int corner : std::as_const(fan.corners))
			corners.insert(corner);
		for (int frame = 0; frame < surface.frames.size(); ++frame)
		{
			P3 sum{0, 0, 0};
			for (int face : std::as_const(fan.faces))
				sum = sum + matchingCornerNormal(surface, frame, surface.triangles[face], [&](int v) { return v == fan.corners.value(face); });
			if (length(sum) <= 1e-12)
				continue;
			const auto normal = vec(unit(sum));
			for (int v : std::as_const(corners))
			{
				auto &n = surface.frames[frame].normals[v];
				changed = changed || n.x != normal.x || n.y != normal.y || n.z != normal.z;
				n = normal;
			}
		}
		touched += corners;
	}
	if (!changed)
		return fail(error, mark ? Text::tr("The selected edges are already sharp. Nothing changed.")
								: Text::tr("The selected edges are already smooth. Nothing changed."));
	for (int v : std::as_const(touched))
		markModelVertexOriginNormals(&surface, v);
	// Seam marks follow split copies onto the edges they now form.
	if (mark && !surface.uvSeams.isEmpty())
	{
		QHash<int, int> original;
		for (const auto &remap : std::as_const(remaps))
			for (auto it = remap.cbegin(); it != remap.cend(); ++it)
				original.insert(it.value(), it.key());
		const auto seams = surface.uvSeams;
		for (const auto &t : std::as_const(surface.triangles))
			for (const auto &[p, q] : {std::pair{t.a, t.b}, std::pair{t.b, t.c}, std::pair{t.c, t.a}})
				if ((original.contains(p) || original.contains(q)) && seams.contains(modelEdge(original.value(p, p), original.value(q, q))))
					surface.uvSeams.insert(modelEdge(p, q));
	}
	pruneSeams(&surface);
	// Welded copies that no face uses any more are removed.
	ModelSelection kept = *selection;
	if (!compactUnused(&surface, merged, &kept, work))
		return false;
	const auto edges = surfaceEdgeSet(surface);
	kept.edges.removeIf([&](ModelEdge edge) { return !edges.contains(edge); });
	*selection = kept;
	return work.check();
}
} // namespace vibestudio
