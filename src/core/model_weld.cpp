// Target weld, auto-merge and explicit vertex positions.
//
// Original implementation following 3ds Max's Editable Poly Target Weld,
// MilkShape 3D's Snap Together and Blender's Auto Merge Vertices
// (https://docs.blender.org/manual/en/latest/modeling/meshes/tools/tool_settings.html);
// no code from any of them is used. A weld moves every face corner of a
// source vertex onto the target vertex and keeps the target's position in
// every pose, its UVs (the target copy nearest each corner's UV) and its
// normal, so seam copies on the target side survive.
#include "core/model_topology_tools.h"

#include "core/model_mesh_tools_p.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace vibestudio
{
using namespace model_tools;
namespace
{
double uvDistance(const ModelSurface &surface, int a, int b)
{
	return std::hypot(double(surface.texCoords[a].u) - surface.texCoords[b].u, double(surface.texCoords[a].v) - surface.texCoords[b].v);
}
// Welds every vertex of each source group onto its target group. Faces that
// collapse onto an edge or point go; duplicate faces go; unused source copies
// are removed. Refuses a weld that leaves an edge with more than two faces.
// Each copy of a source group takes the copy of its target group with the
// nearest UV, so seam copies on the target side survive.
QHash<int, int> groupRemap(const ModelSurface &surface, const ModelGeometricTopology &topology, const QMap<int, int> &targets)
{
	QHash<int, int> remap;
	for (auto it = targets.cbegin(); it != targets.cend(); ++it)
	{
		if (it.key() == it.value())
			continue;
		const auto &targetCopies = topology.copies[it.value()];
		for (int copy : topology.copies[it.key()])
		{
			int best = targetCopies.first();
			for (int candidate : targetCopies)
				if (uvDistance(surface, copy, candidate) < uvDistance(surface, copy, best))
					best = candidate;
			remap.insert(copy, best);
		}
	}
	return remap;
}

// Moves every face corner of each source index onto its target index.
bool weldIndices(ModelMesh *mesh, int surfaceIndex, const QHash<int, int> &remap, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	auto &surface = mesh->surfaces[surfaceIndex];
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error))
		return false;
	QSet<int> sources;
	for (auto it = remap.cbegin(); it != remap.cend(); ++it)
		sources.insert(it.key());
	if (remap.isEmpty())
		return fail(error, Text::tr("Nothing to weld: the source and target are the same vertex."));
	QVector<ModelTriangle> kept;
	QSet<int> touched;
	for (const auto &original : std::as_const(surface.triangles))
	{
		if (!work.step())
			return false;
		const ModelTriangle t{remap.value(original.a, original.a), remap.value(original.b, original.b), remap.value(original.c, original.c)};
		if (topology.group(t.a) == topology.group(t.b) || topology.group(t.b) == topology.group(t.c) || topology.group(t.a) == topology.group(t.c))
			continue;
		kept.append(t);
		if (!(t.a == original.a && t.b == original.b && t.c == original.c))
			touched << t.a << t.b << t.c;
	}
	if (kept.isEmpty())
		return fail(error, Text::tr("Welding would remove every face of the surface."));
	surface.triangles = std::move(kept);
	removeDuplicateFaces(&surface, touched);
	QSet<ModelEdge> seams;
	for (auto seam : std::as_const(surface.uvSeams))
		seams.insert(modelEdge(remap.value(seam.first, seam.first), remap.value(seam.second, seam.second)));
	surface.uvSeams = seams;
	pruneSeams(&surface);
	// Welds must keep each edge between at most two faces.
	ModelGeometricTopology after;
	if (!buildModelGeometricTopology(surface, &after, error))
		return false;
	for (auto it = after.edgeFaces.cbegin(); it != after.edgeFaces.cend(); ++it)
		if (it.value().size() > 2 && (touched.contains(it.key().first) || touched.contains(it.key().second) ||
									  std::any_of(touched.cbegin(), touched.cend(), [&](int v) { return after.group(v) == it.key().first || after.group(v) == it.key().second; })))
			return fail(error, Text::tr("Welding would join more than two faces at one edge. Weld onto a vertex on the other side of the gap."));
	ModelSelection result{surfaceIndex, {}, {}};
	for (auto it = remap.cbegin(); it != remap.cend(); ++it)
		result.vertices.insert(it.value());
	if (!compactUnused(&surface, sources, &result, work))
		return false;
	*selection = result;
	return work.check();
}
} // namespace

namespace model_tools
{
// Merge at Target: every selected vertex joins ModelTopologyToolOptions::targetVertex.
bool weldModelToTarget(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	auto &surface = candidate->surfaces[selection->surface];
	const int target = edit.tool.topology.targetVertex;
	if (target < 0 || target >= surface.vertexCount)
		return fail(error, Text::tr("Choose an existing target vertex to weld onto."));
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error, control))
		return false;
	QMap<int, int> targets;
	for (int v : selectedVertexSet(surface, *selection))
		if (topology.group(v) != topology.group(target))
			targets.insert(topology.group(v), topology.group(target));
	if (targets.isEmpty())
		return fail(error, Text::tr("Select the vertices to weld onto the target, other than the target itself."));
	return weldIndices(candidate, selection->surface, groupRemap(surface, topology, targets), selection, error, work);
}

/// Auto-merge after a proportional transform: each moved vertex that lands
// within the distance of an unmoved one (reference pose) welds onto it. Moved
// and unmoved vertices are told apart by index, since a weld target can
// coincide exactly with the vertex brought onto it.
bool autoMergeModelVertices(ModelMesh *candidate, const ModelSurface &before, const ModelEdit &edit, ModelSelection *selection, QString *error,
							const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	const double distance = edit.tool.topology.autoMergeDistance;
	if (distance <= 0)
		return true;
	if (edit.frame >= 0)
		return fail(error, Text::tr("Auto Merge welds vertices in every pose. Use the All frames scope."));
	auto &surface = candidate->surfaces[selection->surface];
	const int reference = edit.tool.referenceFrame;
	QSet<int> moved;
	for (int v = 0; v < surface.vertexCount; ++v)
	{
		const auto a = before.frames[reference].positions[v], b = surface.frames[reference].positions[v];
		if (a.x != b.x || a.y != b.y || a.z != b.z)
			moved.insert(v);
	}
	if (moved.isEmpty())
		return true;
	// Unmoved vertices in a grid of cells as wide as the distance.
	const double cell = std::max(distance, 1e-6);
	const auto cellOf = [&](P3 p)
	{ return std::array<qint64, 3>{qint64(std::floor(p.x / cell)), qint64(std::floor(p.y / cell)), qint64(std::floor(p.z / cell))}; };
	std::map<std::array<qint64, 3>, QVector<int>> grid;
	const auto used = usedVertices(surface);
	for (int v = 0; v < surface.vertexCount; ++v)
		if (!moved.contains(v) && used.contains(v))
			grid[cellOf(position(surface, reference, v))].append(v);
	QHash<int, int> remap;
	for (int v : std::as_const(moved))
	{
		if (!work.step())
			return false;
		const P3 p = position(surface, reference, v);
		const auto centre = cellOf(p);
		int best = -1;
		double nearest = distance, bestUv = INFINITY;
		for (qint64 dx = -1; dx <= 1; ++dx)
			for (qint64 dy = -1; dy <= 1; ++dy)
				for (qint64 dz = -1; dz <= 1; ++dz)
				{
					const auto found = grid.find({centre[0] + dx, centre[1] + dy, centre[2] + dz});
					if (found == grid.end())
						continue;
					for (int target : found->second)
					{
						// The nearest position wins; among seam copies there, the nearest UV.
						const double d = length(position(surface, reference, target) - p), uv = uvDistance(surface, v, target);
						if (d < nearest - 1e-12 || (std::abs(d - nearest) <= 1e-12 && (uv < bestUv || (uv == bestUv && target < best))))
						{
							nearest = std::min(nearest, d);
							bestUv = uv;
							best = target;
						}
					}
				}
		if (best >= 0)
			remap.insert(v, best);
	}
	if (remap.isEmpty())
		return true;
	return weldIndices(candidate, selection->surface, remap, selection, error, work);
}
} // namespace model_tools

bool setModelVertexPositions(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	const auto &options = edit.tool.topology;
	if (options.vertices.isEmpty() || options.vertices.size() != options.positions.size())
		return fail(error, Text::tr("Give one position for each vertex to place."));
	auto &surface = candidate->surfaces[selection->surface];
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error, control))
		return false;
	const int reference = edit.frame >= 0 ? edit.frame : edit.tool.referenceFrame;
	QHash<int, P3> wanted;
	for (int i = 0; i < options.vertices.size(); ++i)
	{
		const int v = options.vertices[i];
		if (v < 0 || v >= surface.vertexCount)
			return fail(error, Text::tr("A vertex to place does not exist."));
		for (double c : options.positions[i])
			if (!std::isfinite(c) || std::abs(c) > 1000000)
				return fail(error, Text::tr("Tool coordinates must be finite and within ±1,000,000 units."));
		const int group = topology.group(v);
		const P3 p = arrayPoint(options.positions[i]);
		if (wanted.contains(group) && length(wanted.value(group) - p) > 0)
			return fail(error, Text::tr("Vertex %1 is given two different positions.").arg(v));
		wanted.insert(group, p);
	}
	QSet<int> faces;
	QSet<int> collapsedBefore;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		const auto &t = surface.triangles[face];
		if (wanted.contains(topology.group(t.a)) || wanted.contains(topology.group(t.b)) || wanted.contains(topology.group(t.c)))
		{
			faces.insert(face);
			if (collapsedInAnyFrame(surface, t))
				collapsedBefore.insert(face);
		}
	}
	// The reference pose takes the position; every other pose in scope keeps
	// its offset from it, so animation moves with the vertex.
	QSet<int> moved;
	for (auto it = wanted.cbegin(); it != wanted.cend(); ++it)
	{
		if (!work.step())
			return false;
		const P3 delta = it.value() - position(surface, reference, it.key());
		for (int frame = 0; frame < surface.frames.size(); ++frame)
		{
			if (edit.frame >= 0 && frame != edit.frame)
				continue;
			const P3 p = frame == reference ? it.value() : position(surface, frame, it.key()) + delta;
			for (int copy : topology.copies[it.key()])
			{
				surface.frames[frame].positions[copy] = vec(p);
				moved.insert(copy);
			}
		}
	}
	for (int face : std::as_const(faces))
		if (!collapsedBefore.contains(face) && collapsedInAnyFrame(surface, surface.triangles[face]))
			return fail(error, Text::tr("Placing the vertices there would collapse a face to a line or point."));
	QSet<int> around = moved;
	for (int face : std::as_const(faces))
	{
		const auto &t = surface.triangles[face];
		around << t.a << t.b << t.c;
	}
	Q_UNUSED(selection);
	return refreshNormals(&surface, around, work, edit.frame);
}
} // namespace vibestudio
