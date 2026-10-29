// Delete Vertices / Edges / Faces, Dissolve Edges and Limited Dissolve.
//
// Original implementation following the documented behaviour of Blender's
// Delete, Dissolve Edges and Limited Dissolve
// (https://docs.blender.org/manual/en/latest/modeling/meshes/editing/mesh/delete.html)
// and 3ds Max's Editable Poly Remove; no code from either is used. Regions
// merge through indexed edges, so each merged polygon keeps one UV island,
// and polygons are filled again by ear clipping (G. H. Meisters, "Polygons
// Have Ears", 1975) in the reference pose and checked in every pose.
#include "core/model_topology_tools.h"

#include "core/model_mesh_tools_p.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace vibestudio
{
using namespace model_tools;
namespace
{
QVector<int> sortedSet(const QSet<int> &values)
{
	auto list = values.values();
	std::sort(list.begin(), list.end());
	return QVector<int>(list.cbegin(), list.cend());
}

struct Dissolve
{
	// Face sets, each merged into one polygon.
	QVector<QVector<int>> regions;
	// Border vertices (geometric groups) that may be removed when two edges
	// remain at them; with straightOnly, only where those edges are within
	// `angle` degrees of a straight line.
	QSet<int> removable;
	bool straightOnly = false;
	double angle = 0;
	// Regions that are not a disk with one border are skipped rather than refused.
	bool skipInvalid = false;
};

// Merges each region into one polygon and fills it again. Vertices inside a
// region are removed; border vertices left between two edges are removed when
// allowed. Faces outside the regions keep their indices' relative order.
bool dissolveRegions(ModelMesh *mesh, ModelSelection *selection, const Dissolve &request, int frame, QString *error, ModelWorkProgress &work,
					 int *merged)
{
	auto &surface = mesh->surfaces[selection->surface];
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error))
		return false;
	QHash<ModelEdge, QVector<int>> uses;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		const auto &t = surface.triangles[face];
		for (auto edge : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
			uses[edge].append(face);
	}
	QHash<int, int> regionOf;
	for (int r = 0; r < request.regions.size(); ++r)
		for (int face : request.regions[r])
			regionOf.insert(face, r);
	struct Polygon
	{
		QVector<int> corners;
		QVector<int> members;
		P3 normal{0, 0, 0};
	};
	QVector<Polygon> polygons;
	QSet<int> interior;
	for (int r = 0; r < request.regions.size(); ++r)
	{
		if (!work.step())
			return false;
		const auto &members = request.regions[r];
		QHash<int, int> next;
		QSet<int> regionVertices;
		P3 normal{0, 0, 0};
		bool valid = true;
		for (int face : members)
		{
			const auto &t = surface.triangles[face];
			normal = normal + faceCross(surface, frame, t);
			regionVertices << t.a << t.b << t.c;
			for (const auto &edge : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
			{
				const auto edgeUses = uses.value(modelEdge(edge.first, edge.second));
				int inRegion = 0;
				for (int other : edgeUses)
					inRegion += regionOf.value(other, -1) == r;
				if (edgeUses.size() > 2 || (inRegion == 1 && next.contains(edge.first)))
					valid = false;
				else if (inRegion == 1)
					next.insert(edge.first, edge.second);
			}
		}
		QVector<int> polygon;
		if (valid && !next.isEmpty())
		{
			auto keys = next.keys();
			const int start = *std::min_element(keys.cbegin(), keys.cend());
			polygon.append(start);
			int current = next.value(start);
			while (current != start && polygon.size() <= next.size())
			{
				polygon.append(current);
				current = next.value(current, start);
			}
			valid = polygon.size() == next.size();
		}
		else
			valid = false;
		// Vertices inside the region must belong to it alone.
		QSet<int> inside;
		if (valid)
		{
			const QSet<int> border(polygon.cbegin(), polygon.cend());
			for (int v : std::as_const(regionVertices))
			{
				if (border.contains(v))
					continue;
				for (int face : topology.groupFaces[topology.group(v)])
					if (regionOf.value(face, -1) != r)
						valid = false;
				inside.insert(v);
			}
		}
		if (!valid)
		{
			if (request.skipInvalid)
				continue;
			return fail(error, Text::tr("The faces to merge must form one piece with a single border, no holes and no pinched corners, "
										"on one UV island."));
		}
		interior += inside;
		polygons.append({polygon, members, normal});
	}
	if (polygons.isEmpty())
		return fail(error, Text::tr("Nothing to dissolve: no group of faces forms a single polygon."));
	// Border vertices left between two edges.
	QSet<int> merging;
	for (const auto &polygon : std::as_const(polygons))
		for (int face : polygon.members)
			merging.insert(face);
	QSet<int> removed;
	for (int group : request.removable)
	{
		if (group < 0 || group >= topology.copies.size() || topology.copies[group].isEmpty())
			continue;
		const auto &around = topology.groupFaces[group];
		if (std::any_of(around.cbegin(), around.cend(), [&](int face) { return !merging.contains(face); }))
			continue;
		// Neighbours along the polygons' borders.
		QSet<int> neighbours;
		for (const auto &polygon : std::as_const(polygons))
		{
			const int n = polygon.corners.size();
			for (int i = 0; i < n; ++i)
				if (topology.group(polygon.corners[i]) == group)
					neighbours << topology.group(polygon.corners[(i + n - 1) % n]) << topology.group(polygon.corners[(i + 1) % n]);
		}
		if (neighbours.size() != 2)
			continue;
		if (request.straightOnly)
		{
			const auto ends = neighbours.values();
			const P3 p = position(surface, frame, group), a = position(surface, frame, ends[0]) - p, b = position(surface, frame, ends[1]) - p;
			if (length(a) <= 0 || length(b) <= 0)
				continue;
			const double bend = 180 - std::acos(std::clamp(dot(unit(a), unit(b)), -1.0, 1.0)) * 180 / std::numbers::pi;
			if (bend > request.angle + 1e-9)
				continue;
		}
		removed.insert(group);
	}
	QVector<ModelTriangle> added;
	QSet<int> unused = interior;
	for (auto &polygon : polygons)
	{
		if (!work.step())
			return false;
		QVector<int> corners;
		for (int v : std::as_const(polygon.corners))
		{
			if (removed.contains(topology.group(v)))
				unused.insert(v);
			else
				corners.append(v);
		}
		if (corners.size() < 3)
			return fail(error, Text::tr("Dissolving would leave a face with fewer than three corners."));
		QVector<ModelTriangle> fill;
		if (!triangulatePolygon(surface, frame, corners, polygon.normal, &fill))
			return fail(error, Text::tr("A merged polygon folds over itself in the reference pose and cannot be filled again."));
		for (const auto &t : std::as_const(fill))
			if (collapsedInAnyFrame(surface, t))
				return fail(error, Text::tr("A dissolved face would collapse in an animation pose."));
		added += fill;
	}
	QVector<ModelTriangle> triangles;
	for (int face = 0; face < surface.triangles.size(); ++face)
		if (!merging.contains(face))
			triangles.append(surface.triangles[face]);
	const int firstAdded = triangles.size();
	triangles += added;
	surface.triangles = std::move(triangles);
	pruneSeams(&surface);
	selection->vertices.clear();
	selection->edges.clear();
	selection->faces.clear();
	for (int face = firstAdded; face < surface.triangles.size(); ++face)
		selection->faces.insert(face);
	if (!compactUnused(&surface, unused, selection, work))
		return false;
	QSet<int> refresh;
	for (int face : std::as_const(selection->faces))
	{
		const auto &t = surface.triangles[face];
		refresh << t.a << t.b << t.c;
	}
	if (merged)
		*merged = polygons.size();
	return refreshNormals(&surface, refresh, work);
}
} // namespace

bool deleteModelGeometry(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control,
						 ModelEditReport *report)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	auto &surface = candidate->surfaces[selection->surface];
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error, control))
		return false;
	const auto mode = edit.tool.topology.deleteMode;
	QSet<int> doomed;
	if (mode == ModelDeleteMode::Vertices)
	{
		// Every face touching a selected vertex or any of its seam copies.
		for (int v : selectedVertexSet(surface, *selection))
			for (int face : topology.groupFaces.value(topology.group(v)))
				doomed.insert(face);
	}
	else if (mode == ModelDeleteMode::Edges)
	{
		QSet<ModelEdge> edges;
		for (auto edge : selection->edges)
			edges.insert(topology.geometricEdge(edge.first, edge.second));
		for (int face : selection->faces)
		{
			const auto &t = surface.triangles[face];
			edges << topology.geometricEdge(t.a, t.b) << topology.geometricEdge(t.b, t.c) << topology.geometricEdge(t.c, t.a);
		}
		for (auto edge : std::as_const(edges))
			for (int face : topology.edgeFaces.value(edge))
				doomed.insert(face);
	}
	else
	{
		doomed = selection->faces;
		const auto vertices = expandToCopies(topology, selection->vertices);
		for (int face = 0; face < surface.triangles.size(); ++face)
		{
			const auto &t = surface.triangles[face];
			if (vertices.contains(t.a) && vertices.contains(t.b) && vertices.contains(t.c))
				doomed.insert(face);
		}
	}
	if (doomed.isEmpty())
		return fail(error, mode == ModelDeleteMode::Vertices ? Text::tr("Select the vertices to delete.")
						   : mode == ModelDeleteMode::Edges	  ? Text::tr("Select the edges to delete.")
															  : Text::tr("Select the faces to delete."));
	if (doomed.size() == surface.triangles.size())
		return fail(error, Text::tr("Deleting would leave the surface without faces. Delete the surface instead."));
	QVector<ModelTriangle> kept;
	QVector<int> sources;
	QSet<int> corners;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!work.step())
			return false;
		const auto &t = surface.triangles[face];
		if (doomed.contains(face))
		{
			corners << t.a << t.b << t.c;
			continue;
		}
		kept.append(t);
		sources.append(face);
	}
	surface.triangles = std::move(kept);
	pruneSeams(&surface);
	ModelSelection none{selection->surface, {}, {}};
	if (!compactUnused(&surface, corners, &none, work))
		return false;
	*selection = none;
	if (report)
		report->sourceFaces.insert(selection->surface, sources);
	return work.check();
}

bool dissolveModelEdges(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control,
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
		return fail(error, Text::tr("Select the edges to dissolve."));
	// Union the quad view's polygons across each dissolved edge.
	QVector<int> parent(quads.elements.size());
	std::iota(parent.begin(), parent.end(), 0);
	const auto root = [&](int element)
	{
		while (parent[element] != element)
			element = parent[element] = parent[parent[element]];
		return element;
	};
	QSet<int> touched, ends;
	for (auto edge : std::as_const(chosen))
	{
		if (!work.step())
			return false;
		if (quads.diagonals.contains(edge))
			continue;
		const auto faces = topology.edgeFaces.value(edge);
		const auto sides = quads.sideUses.value(edge);
		if (faces.size() != 2 || sides.size() != 2 || sides[0].first == sides[1].first)
			return fail(error, Text::tr("Edge %1–%2 is on a border or joins more than two faces, so it cannot be dissolved.").arg(edge.first).arg(edge.second));
		// Both faces use the same indices: the edge is not a UV seam.
		const auto indexEdges = [&](int face)
		{
			const auto &t = surface.triangles[face];
			QSet<ModelEdge> result;
			for (auto e : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
				if (topology.geometricEdge(e.first, e.second) == edge)
					result.insert(e);
			return result;
		};
		if (!indexEdges(faces[0]).intersects(indexEdges(faces[1])))
			return fail(error, Text::tr("Edge %1–%2 is a UV seam. Dissolving it would merge two UV islands; join the islands first.").arg(edge.first).arg(edge.second));
		parent[root(sides[0].first)] = root(sides[1].first);
		touched << sides[0].first << sides[1].first;
		ends << edge.first << edge.second;
	}
	QMap<int, QVector<int>> regions;
	for (int element : std::as_const(touched))
		for (int face : quads.elements[element].faces)
			if (face >= 0)
				regions[root(element)].append(face);
	if (regions.isEmpty())
		return fail(error, Text::tr("Select edges between faces to dissolve; a quad's hidden diagonal is already dissolved."));
	Dissolve request;
	for (auto it = regions.cbegin(); it != regions.cend(); ++it)
	{
		auto faces = it.value();
		std::sort(faces.begin(), faces.end());
		request.regions.append(faces);
	}
	if (edit.tool.topology.dissolveVertices)
		request.removable = ends;
	return dissolveRegions(candidate, selection, request, edit.tool.referenceFrame, error, work, nullptr);
}

bool limitedDissolveModel(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control,
						  ModelEditReport *report)
{
	Q_UNUSED(report);
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	auto &surface = candidate->surfaces[selection->surface];
	const int frame = edit.tool.referenceFrame;
	QSet<int> chosen = selection->faces;
	if (chosen.isEmpty())
		for (int face = 0; face < surface.triangles.size(); ++face)
			chosen.insert(face);
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error, control))
		return false;
	QHash<ModelEdge, QVector<int>> uses;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		const auto &t = surface.triangles[face];
		for (auto edge : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
			uses[edge].append(face);
	}
	const double limit = std::cos(edit.tool.topology.dissolveAngle * std::numbers::pi / 180);
	QVector<P3> normals(surface.triangles.size());
	for (int face = 0; face < surface.triangles.size(); ++face)
		normals[face] = unit(faceCross(surface, frame, surface.triangles[face]));
	// Grow regions through indexed edges between nearly coplanar faces.
	QHash<int, int> regionOf;
	QVector<QVector<int>> regions;
	for (int seed : sortedSet(chosen))
	{
		if (regionOf.contains(seed))
			continue;
		if (!work.step())
			return false;
		const int index = regions.size();
		QVector<int> members{seed}, stack{seed};
		regionOf.insert(seed, index);
		while (!stack.isEmpty())
		{
			const int face = stack.takeLast();
			const auto &t = surface.triangles[face];
			for (auto edge : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
			{
				const auto edgeUses = uses.value(edge);
				if (edgeUses.size() != 2)
					continue;
				const int other = edgeUses[0] == face ? edgeUses[1] : edgeUses[0];
				if (!chosen.contains(other) || regionOf.contains(other) || dot(normals[face], normals[other]) < limit - 1e-12)
					continue;
				regionOf.insert(other, index);
				members.append(other);
				stack.append(other);
			}
		}
		std::sort(members.begin(), members.end());
		regions.append(members);
	}
	Dissolve request;
	request.straightOnly = true;
	request.angle = edit.tool.topology.dissolveAngle;
	request.skipInvalid = true;
	// Single faces count too: they can still lose straight border vertices.
	for (const auto &members : std::as_const(regions))
	{
		request.regions.append(members);
		for (int face : members)
		{
			const auto &t = surface.triangles[face];
			request.removable << topology.group(t.a) << topology.group(t.b) << topology.group(t.c);
		}
	}
	const int before = surface.triangles.size();
	int merged = 0;
	if (!dissolveRegions(candidate, selection, request, frame, error, work, &merged))
		return false;
	if (candidate->surfaces[selection->surface].triangles.size() >= before)
		return fail(error, Text::tr("Nothing to simplify: no neighbouring faces are within %1° of each other, and no border vertex lies on a "
									"straight line.")
							   .arg(edit.tool.topology.dissolveAngle));
	return true;
}
} // namespace vibestudio
