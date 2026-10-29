// Mark Sharp, Clear Sharp and smoothing groups: explicit hard edges for idTech
// vertex normals, stored with the editable source.
//
// Original implementation following Blender's documented Mark Sharp / Clear
// Sharp edge operators
// (https://docs.blender.org/manual/en/latest/modeling/meshes/editing/edge/edge_data.html)
// and the smoothing groups of MilkShape 3D and 3ds Max; no code from either is
// used. MD3, MD2 and MDL store one normal per vertex, so a hard edge is two
// vertices with different normals: marking splits corner indices where sharp
// edges separate the faces around a vertex, and clearing welds them back where
// the UVs allow. Normals are the angle-weighted average of each fan's faces in
// every pose. The marks are kept as pairs of topology identities
// (ModelSurface::sharpEdges), so Recalculate Normals and Shade Smooth can split
// along them again after anything welded the vertices.
#include "core/model_topology_tools.h"

#include "core/model_mesh_tools_p.h"
#include "core/model_vertex_identity.h"
#include "core/model_vertex_origins.h"

#include <QMap>

#include <algorithm>
#include <numeric>

namespace vibestudio
{
using namespace model_tools;
namespace
{
// Splits (mark) or joins (clear) the fans around the ends of `chosen`
// geometric edges and gives each fan its normal. A fan breaks at a chosen
// edge when marking, and at an edge whose corners already differ in normal
// unless it is chosen when clearing. `frame` limits the normals to one pose.
// Sets *changed when an index or a normal changed. Copies a clear leaves
// unused are added to `merged` for the caller to compact once.
bool splitFans(ModelSurface *surface, const ModelGeometricTopology &topology, const QSet<ModelEdge> &chosen, bool mark, int reference, int frame,
			   bool *changed, QSet<int> *merged, QString *error, ModelWorkProgress &work)
{
	QSet<int> groups;
	for (auto edge : chosen)
		if (topology.edgeFaces.value(edge).size() >= 2)
			groups << edge.first << edge.second;
	const auto cornerAt = [&](int face, int group)
	{
		const auto &t = surface->triangles[face];
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
				const auto &a = surface->triangles[around[i]], &b = surface->triangles[around[j]];
				int other = -1;
				for (int x : {a.a, a.b, a.c})
					for (int y : {b.a, b.b, b.c})
						if (topology.group(x) == topology.group(y) && topology.group(x) != group)
							other = topology.group(x);
				if (other < 0)
					continue;
				const bool selected = chosen.contains(modelEdge(group, other));
				const int ca = cornerAt(around[i], group), cb = cornerAt(around[j], group);
				const auto na = surface->frames[reference].normals[ca], nb = surface->frames[reference].normals[cb];
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
	QVector<QHash<int, int>> remaps(fans.size());
	if (mark)
	{
		qint64 added = 0;
		QHash<QPair<int, int>, int> keeper; // (group, index) -> the fan keeping it
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
		if (surface->vertexCount + added > modelDocumentMaxVertices)
			return fail(error, Text::tr("The edit would exceed the model's vertex, animation storage or triangle limit."));
		for (auto &remap : remaps)
			for (auto it = remap.begin(); it != remap.end(); ++it)
				it.value() = appendCopy(surface, it.key());
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
					if (other < target && surface->texCoords[other].u == surface->texCoords[corner].u &&
						surface->texCoords[other].v == surface->texCoords[corner].v)
						target = other;
				if (target != corner)
				{
					remaps[f].insert(corner, target);
					merged->insert(corner);
				}
			}
		}
	}
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
			auto &t = surface->triangles[face];
			for (int *slot : {&t.a, &t.b, &t.c})
				if (*slot == corner)
					*slot = replacement;
			fan.corners.insert(face, replacement);
			*changed = true;
		}
		QSet<int> corners;
		for (int corner : std::as_const(fan.corners))
			corners.insert(corner);
		for (int pose = 0; pose < surface->frames.size(); ++pose)
		{
			if (frame >= 0 && pose != frame)
				continue;
			P3 sum{0, 0, 0};
			for (int face : std::as_const(fan.faces))
				sum = sum + matchingCornerNormal(*surface, pose, surface->triangles[face], [&](int v) { return v == fan.corners.value(face); });
			if (length(sum) <= 1e-12)
				continue;
			const auto normal = vec(unit(sum));
			for (int v : std::as_const(corners))
			{
				auto &n = surface->frames[pose].normals[v];
				*changed = *changed || n.x != normal.x || n.y != normal.y || n.z != normal.z;
				n = normal;
			}
		}
		touched += corners;
	}
	for (int v : std::as_const(touched))
		markModelVertexOriginNormals(surface, v);
	// Seam marks follow split copies onto the edges they now form.
	if (mark && !surface->uvSeams.isEmpty())
	{
		QHash<int, int> original;
		for (const auto &remap : std::as_const(remaps))
			for (auto it = remap.cbegin(); it != remap.cend(); ++it)
				original.insert(it.value(), it.key());
		const auto seams = surface->uvSeams;
		for (const auto &t : std::as_const(surface->triangles))
			for (const auto &[p, q] : {std::pair{t.a, t.b}, std::pair{t.b, t.c}, std::pair{t.c, t.a}})
				if ((original.contains(p) || original.contains(q)) && seams.contains(modelEdge(original.value(p, p), original.value(q, q))))
					surface->uvSeams.insert(modelEdge(p, q));
	}
	pruneSeams(surface);
	return work.check();
}

// The identity pair a geometric edge stores as.
QPair<int, int> identityPair(const ModelSurface &surface, ModelEdge geometric)
{
	const int a = surface.topologyVertices[geometric.first], b = surface.topologyVertices[geometric.second];
	return {std::min(a, b), std::max(a, b)};
}
// The stored sharp edges as geometric edges of the current surface.
QSet<ModelEdge> storedSharp(const ModelSurface &surface, const ModelGeometricTopology &topology)
{
	QSet<ModelEdge> result;
	if (surface.sharpEdges.isEmpty() || surface.topologyVertices.isEmpty())
		return result;
	for (auto it = topology.edgeFaces.cbegin(); it != topology.edgeFaces.cend(); ++it)
		if (surface.sharpEdges.contains(identityPair(surface, it.key())))
			result.insert(it.key());
	return result;
}
} // namespace

bool applyModelSharpEdges(ModelSurface *surface, int frame, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (surface->sharpEdges.isEmpty())
		return true;
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(*surface, &topology, error, control))
		return false;
	const auto sharp = storedSharp(*surface, topology);
	if (sharp.isEmpty())
		return true;
	bool changed = false;
	QSet<int> merged;
	return splitFans(surface, topology, sharp, true, frame >= 0 ? frame : 0, frame, &changed, &merged, error, work);
}

bool markModelSharpEdges(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	const bool mark = edit.kind == ModelEditKind::MarkSharp;
	const bool group = edit.tool.topology.smoothingGroup;
	auto &surface = candidate->surfaces[selection->surface];
	const int reference = edit.tool.referenceFrame;
	if (group && (!mark || selection->faces.isEmpty()))
		return fail(error, Text::tr("Select the faces that make up the smoothing group."));
	// Marks are stored by topology identity, so they survive renumbering.
	if (!materialiseModelVertexIdentities(&surface, error, control))
		return false;
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error, control))
		return false;
	QSet<ModelEdge> chosen, inside;
	if (group)
	{
		// A smoothing group: the selected faces are smooth among themselves and
		// hard against every other face.
		for (int face : std::as_const(selection->faces))
		{
			const auto &t = surface.triangles[face];
			for (auto edge : {topology.geometricEdge(t.a, t.b), topology.geometricEdge(t.b, t.c), topology.geometricEdge(t.c, t.a)})
			{
				const auto faces = topology.edgeFaces.value(edge);
				const int selected = int(std::count_if(faces.cbegin(), faces.cend(), [&](int f) { return selection->faces.contains(f); }));
				if (faces.size() < 2)
					continue;
				(selected == faces.size() ? inside : chosen).insert(edge);
			}
		}
	}
	else
		for (auto edge : selection->edges)
			chosen.insert(topology.geometricEdge(edge.first, edge.second));
	if (chosen.isEmpty() && inside.isEmpty())
		return fail(error, mark ? Text::tr("Select the edges to mark sharp.") : Text::tr("Select the sharp edges to clear."));
	if (std::none_of(chosen.cbegin(), chosen.cend(), [&](ModelEdge e) { return topology.edgeFaces.value(e).size() >= 2; }) && inside.isEmpty())
		return fail(error, Text::tr("Only edges between two faces can be sharp; border edges are always hard."));
	// Identity pairs are read before anything changes; nothing is compacted
	// (relabelling identities) until the end.
	QSet<QPair<int, int>> borderPairs, insidePairs;
	for (auto edge : std::as_const(chosen))
		if (topology.edgeFaces.value(edge).size() >= 2)
			borderPairs.insert(identityPair(surface, edge));
	for (auto edge : std::as_const(inside))
		insidePairs.insert(identityPair(surface, edge));
	const auto before = surface.sharpEdges;
	bool changed = false;
	QSet<int> merged;
	if (!inside.isEmpty())
	{
		if (!splitFans(&surface, topology, inside, false, reference, -1, &changed, &merged, error, work))
			return false;
		// Clearing may have welded copies: find the border again by identity.
		if (!buildModelGeometricTopology(surface, &topology, error, control))
			return false;
		chosen.clear();
		for (auto it = topology.edgeFaces.cbegin(); it != topology.edgeFaces.cend(); ++it)
			if (borderPairs.contains(identityPair(surface, it.key())))
				chosen.insert(it.key());
	}
	if (!chosen.isEmpty() && !splitFans(&surface, topology, chosen, mark, reference, -1, &changed, &merged, error, work))
		return false;
	for (const auto &pair : std::as_const(insidePairs))
		surface.sharpEdges.remove(pair);
	for (const auto &pair : std::as_const(borderPairs))
	{
		if (mark)
			surface.sharpEdges.insert(pair);
		else
			surface.sharpEdges.remove(pair);
	}
	changed = changed || surface.sharpEdges != before;
	// Welded copies that no face uses any more are removed; the marks follow
	// the identities' new labels.
	if (!compactUnused(&surface, merged, selection, work))
		return false;
	if (!changed)
		return fail(error, mark ? Text::tr("The selected edges are already sharp. Nothing changed.")
								: Text::tr("The selected edges are already smooth. Nothing changed."));
	const auto edges = surfaceEdgeSet(surface);
	selection->vertices.clear();
	selection->edges.removeIf([&](ModelEdge edge) { return !edges.contains(edge); });
	selection->faces.removeIf([&](int face) { return face >= surface.triangles.size(); });
	return work.check();
}
} // namespace vibestudio
