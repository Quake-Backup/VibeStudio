// Small topology tools of core/model_topology_tools.h: Recalculate Outside.
// Larger ones live in their own files (model_connect.cpp, model_dissolve.cpp).
#include "core/model_topology_tools.h"

#include "core/model_mesh_tools_p.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace vibestudio
{
using namespace model_tools;
namespace
{
// Where the line o + t d meets the triangle, if it does (Moller-Trumbore,
// both directions); `grazing` when it meets it on or next to an edge.
bool lineHit(P3 o, P3 d, const std::array<P3, 3> &corners, double *t, bool *grazing)
{
	const P3 e1 = corners[1] - corners[0], e2 = corners[2] - corners[0];
	const P3 p = cross(d, e2);
	const double det = dot(e1, p);
	if (std::abs(det) <= 1e-12 * length(e1) * length(e2) * length(d))
		return false;
	const double inverse = 1 / det;
	const P3 s = o - corners[0];
	const double u = dot(s, p) * inverse;
	if (u < 0 || u > 1)
		return false;
	const P3 q = cross(s, e1);
	const double v = dot(d, q) * inverse;
	if (v < 0 || u + v > 1)
		return false;
	*t = dot(e2, q) * inverse;
	*grazing = u < 1e-7 || v < 1e-7 || u + v > 1 - 1e-7;
	return std::isfinite(*t);
}
} // namespace

// Recalculate Outside, as Blender's Normals > Recalculate Outside
// (https://docs.blender.org/manual/en/latest/modeling/meshes/editing/mesh/normals.html):
// winding is made consistent across geometric edges (through UV seams), then
// each connected piece faces away from its inside, judged by signed volume
// when it is closed. An open piece is judged by ray parity: from sampled faces
// a line along the normal crosses the piece an even number of times in front
// and an odd number behind when the face points out of a hollow, and the
// reverse when it points in; faces whose lines escape through an opening tell
// nothing. Only when no face tells, an area-weighted vote against the piece's
// centre decides. Original implementation; no Blender code is used.
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
		if (!closed)
		{
			// Ray parity, from at most 512 faces spread over the piece.
			QVector<std::array<P3, 3>> corners;
			corners.reserve(members.size());
			double scale = 0;
			for (int face : members)
			{
				const auto t = oriented(face);
				corners.append({position(surface, frame, t.a), position(surface, frame, t.b), position(surface, frame, t.c)});
				for (const auto &p : corners.last())
					scale = std::max({scale, std::abs(p.x), std::abs(p.y), std::abs(p.z)});
			}
			const double epsilon = std::max(1e-9, scale * 1e-7);
			const int stride = std::max(1, int(corners.size() / 512));
			double parity = 0;
			for (int i = 0; i < corners.size(); i += stride)
			{
				const auto &own = corners[i];
				const P3 n = cross(own[1] - own[0], own[2] - own[0]);
				const double twice = length(n);
				if (twice <= 0)
					continue;
				// An uneven point inside the face, so symmetric models do not
				// send every line along a neighbour's diagonal.
				const P3 o = own[0] * 0.2113 + own[1] * 0.3247 + own[2] * 0.464, d = n * (1 / twice);
				int front = 0, back = 0;
				bool grazed = false;
				for (int j = 0; j < corners.size() && !grazed; ++j)
				{
					if (!work.step())
						return false;
					double t = 0;
					bool grazing = false;
					if (j != i && lineHit(o, d, corners[j], &t, &grazing))
					{
						grazed = grazing && std::abs(t) > epsilon;
						front += t > epsilon;
						back += t < -epsilon;
					}
				}
				// A line through an edge or corner is not counted.
				if (grazed)
					continue;
				if (front % 2 == 0 && back % 2 == 1)
					parity += twice;
				else if (front % 2 == 1 && back % 2 == 0)
					parity -= twice;
			}
			if (parity != 0)
				vote = parity;
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
