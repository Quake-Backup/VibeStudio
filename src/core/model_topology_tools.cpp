// Small topology tools of core/model_topology_tools.h: Recalculate Outside.
// Larger ones live in their own files (model_connect.cpp, model_dissolve.cpp).
#include "core/model_topology_tools.h"

#include "core/model_mesh_tools_p.h"

#include <algorithm>
#include <cmath>

namespace vibestudio
{
using namespace model_tools;

// Recalculate Outside, as Blender's Normals > Recalculate Outside
// (https://docs.blender.org/manual/en/latest/modeling/meshes/editing/mesh/normals.html):
// winding is made consistent across geometric edges (through UV seams), then
// each connected piece faces away from its inside, judged by signed volume
// when it is closed and by an area-weighted vote against its centre when it is
// open. Original implementation; no Blender code is used.
bool recalculateModelOutside(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	auto &surface = candidate->surfaces[selection->surface];
	const int frame = edit.tool.referenceFrame;
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error, control))
		return false;
	QSet<int> chosen = selection->faces;
	if (chosen.isEmpty())
		for (int face = 0; face < surface.triangles.size(); ++face)
			chosen.insert(face);
	// Whether a face runs from group a to group b along one of its sides.
	const auto runs = [&](int face, int a, int b)
	{
		const auto &t = surface.triangles[face];
		const int g[3]{topology.group(t.a), topology.group(t.b), topology.group(t.c)};
		for (int i = 0; i < 3; ++i)
			if (g[i] == a && g[(i + 1) % 3] == b)
				return true;
		return false;
	};
	QHash<int, bool> flip;
	QHash<int, int> component;
	QVector<QVector<int>> components;
	auto ordered = chosen.values();
	std::sort(ordered.begin(), ordered.end());
	for (int seed : std::as_const(ordered))
	{
		if (component.contains(seed))
			continue;
		const int index = components.size();
		QVector<int> members{seed}, stack{seed};
		component.insert(seed, index);
		flip.insert(seed, false);
		while (!stack.isEmpty())
		{
			if (!work.step())
				return false;
			const int face = stack.takeLast();
			const auto &t = surface.triangles[face];
			const int g[3]{topology.group(t.a), topology.group(t.b), topology.group(t.c)};
			for (int i = 0; i < 3; ++i)
			{
				const int a = g[i], b = g[(i + 1) % 3];
				const auto uses = topology.edgeFaces.value(modelEdge(a, b));
				QVector<int> others;
				for (int other : uses)
					if (other != face && chosen.contains(other))
						others.append(other);
				// Orientation only propagates across manifold edges.
				if (others.size() != 1)
					continue;
				const int other = others.first();
				// Consistent neighbours run the shared edge the other way.
				const bool wanted = flip.value(face) != runs(other, a, b);
				if (component.contains(other))
				{
					if (flip.value(other) != wanted)
						return fail(error, Text::tr("Faces %1 and %2 cannot both face outward: the piece is one-sided, like a Möbius strip. "
													"Recalculate the faces of each side separately.")
											   .arg(face)
											   .arg(other));
					continue;
				}
				component.insert(other, index);
				flip.insert(other, wanted);
				members.append(other);
				stack.append(other);
			}
		}
		components.append(members);
	}
	const auto oriented = [&](int face)
	{
		const auto &t = surface.triangles[face];
		return flip.value(face) ? ModelTriangle{t.a, t.c, t.b} : t;
	};
	QSet<int> changed;
	for (const auto &members : std::as_const(components))
	{
		if (!work.step())
			return false;
		// Closed: every edge of the piece has a second face in it.
		bool closed = true;
		P3 centre{0, 0, 0};
		double area = 0;
		for (int face : members)
		{
			const auto &t = surface.triangles[face];
			const double a = length(faceCross(surface, frame, t)) / 2;
			centre = centre + (position(surface, frame, t.a) + position(surface, frame, t.b) + position(surface, frame, t.c)) * (a / 3);
			area += a;
			const int g[3]{topology.group(t.a), topology.group(t.b), topology.group(t.c)};
			for (int i = 0; i < 3 && closed; ++i)
			{
				int inside = 0;
				for (int other : topology.edgeFaces.value(modelEdge(g[i], g[(i + 1) % 3])))
					inside += component.value(other, -1) == component.value(face);
				closed = inside == 2;
			}
		}
		if (area <= 0)
			continue;
		centre = centre * (1 / area);
		double vote = 0;
		for (int face : members)
		{
			const auto t = oriented(face);
			const P3 a = position(surface, frame, t.a) - centre, b = position(surface, frame, t.b) - centre, c = position(surface, frame, t.c) - centre;
			if (closed)
				vote += dot(a, cross(b, c)); // six times the signed volume
			else
			{
				const P3 n = cross(b - a, c - a);
				vote += dot(n, (a + b + c) * (1.0 / 3)) >= 0 ? length(n) : -length(n);
			}
		}
		// Outward unless the piece's vote says inward; Inside reverses it.
		const bool reverse = (vote < 0) != edit.tool.topology.inside;
		for (int face : members)
			if (flip.value(face) != reverse)
				changed.insert(face);
	}
	if (changed.isEmpty())
		return fail(error, edit.tool.topology.inside ? Text::tr("Every face already points inward. Nothing changed.")
													 : Text::tr("Every face already points outward. Nothing changed."));
	QSet<int> corners;
	for (int face : std::as_const(changed))
	{
		auto &t = surface.triangles[face];
		std::swap(t.b, t.c);
		corners << t.a << t.b << t.c;
	}
	return refreshNormals(&surface, expandToCopies(topology, corners), work);
}
} // namespace vibestudio
