#include "core/model_mesh_tools_p.h"

#include <algorithm>
#include <cmath>
#include <map>

// Add Face builds one triangle from existing vertices and new points, the
// building block of Poly Build and of MilkShape 3D's vertex and face creation
// tools (Blender manual 4.0, modeling/meshes/tools/poly_build.html; MilkShape
// 3D 1.8 Vertex and Face tools; behaviour only, reviewed 2026-10-28).
namespace vibestudio::model_tools
{
namespace
{
struct AddFaceText
{
	Q_DECLARE_TR_FUNCTIONS(ModelAddFace)
};
} // namespace

bool addFace(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	const auto &options = edit.tool;
	auto &surface = mesh->surfaces[selection->surface];
	if (!selection->faces.isEmpty() || !selection->edges.isEmpty())
		return fail(error, AddFaceText::tr("Add Face uses selected vertices and new points, not faces or edges."));
	if (options.facePointCount < 0 || options.facePointCount > 3)
		return fail(error, AddFaceText::tr("Add Face takes up to three new points."));
	QVector<int> existing(selection->vertices.cbegin(), selection->vertices.cend());
	std::sort(existing.begin(), existing.end());
	if (existing.size() + options.facePointCount != 3)
		return fail(error, AddFaceText::tr("A face needs three different corners: selected vertices and new points together."));
	for (int v : std::as_const(existing))
	{
		if (v < 0 || v >= surface.vertexCount)
			return fail(error, AddFaceText::tr("A corner names a vertex the surface does not have."));
	}
	const int frame = options.referenceFrame;
	// No two corners may share a position.
	QVector<P3> places;
	for (int v : std::as_const(existing))
		places << position(surface, frame, v);
	for (int i = 0; i < options.facePointCount; ++i)
	{
		const auto &p = options.facePoints[i];
		if (!std::all_of(p.begin(), p.end(), [](double value) { return std::isfinite(value) && std::abs(value) <= 1000000; }))
			return fail(error, AddFaceText::tr("New points must be finite and within ±1,000,000 units."));
		places << P3{p[0], p[1], p[2]};
	}
	for (int i = 0; i < 3; ++i)
	{
		for (int j = i + 1; j < 3; ++j)
		{
			if (length(places[i] - places[j]) <= 1e-6)
				return fail(error, AddFaceText::tr("Two corners of the new face are at the same place. Pick three different points."));
		}
	}
	if (length(cross(places[1] - places[0], places[2] - places[0])) <= 1e-9)
		return fail(error, AddFaceText::tr("The three corners are in a line; the face would have no area."));
	if (!reserveCapacity(*mesh, selection->surface, options.facePointCount, 1, error))
		return false;
	// New corners: the same place in every pose, their own identity, planar
	// UVs on the face's dominant axis and the face's normal.
	const P3 normal = unit(cross(places[1] - places[0], places[2] - places[0]));
	const int dominant = std::abs(normal.x) >= std::abs(normal.y) && std::abs(normal.x) >= std::abs(normal.z) ? 0
						 : std::abs(normal.y) >= std::abs(normal.z)										? 1
																										: 2;
	QVector<int> corners = existing;
	for (int i = 0; i < options.facePointCount; ++i)
	{
		const P3 p = places[existing.size() + i];
		const int index = surface.vertexCount;
		const auto uv = project(p, dominant);
		surface.texCoords.append({float(uv.x / options.uvTileSize), float(uv.y / options.uvTileSize)});
		if (!surface.topologyVertices.isEmpty())
			surface.topologyVertices.append(index);
		appendModelVertexOriginNone(&surface);
		for (auto &pose : surface.frames)
		{
			pose.positions.append(vec(p));
			pose.normals.append(vec(normal));
		}
		surface.vertexCount = surface.texCoords.size();
		corners << index;
	}
	ModelTriangle face{corners[0], corners[1], corners[2]};
	ModelTriangle reversed{corners[0], corners[2], corners[1]};
	// Winding: agree with neighbouring faces, else face the requested side.
	QHash<QPair<int, int>, int> directed;
	for (const auto &t : std::as_const(surface.triangles))
	{
		directed.insert({t.a, t.b}, 1);
		directed.insert({t.b, t.c}, 1);
		directed.insert({t.c, t.a}, 1);
	}
	const auto conflicts = [&](const ModelTriangle &t)
	{
		return int(directed.contains({t.a, t.b})) + int(directed.contains({t.b, t.c})) + int(directed.contains({t.c, t.a}));
	};
	const int forward = conflicts(face), backward = conflicts(reversed);
	if (forward > 0 && backward > 0)
		return fail(error, AddFaceText::tr("The new face would share an edge with two faces. Pick corners on an open border."));
	if (forward > 0)
		face = reversed;
	else if (backward == 0)
	{
		if (dot(faceCross(surface, frame, face), arrayPoint(options.normal)) < 0)
			face = reversed;
	}
	std::array<int, 3> key{face.a, face.b, face.c};
	std::sort(key.begin(), key.end());
	for (const auto &t : std::as_const(surface.triangles))
	{
		std::array<int, 3> other{t.a, t.b, t.c};
		std::sort(other.begin(), other.end());
		if (other == key)
			return fail(error, AddFaceText::tr("A face already uses these corners."));
	}
	if (collapsedInAnyFrame(surface, face))
		return fail(error, AddFaceText::tr("The new face collapses in an animation pose, where its corners fall in a line."));
	const int created = surface.triangles.size();
	surface.triangles.append(face);
	if (!refreshNormals(&surface, {face.a, face.b, face.c}, work))
		return false;
	selection->vertices.clear();
	selection->edges.clear();
	selection->faces = {created};
	return work.check();
}
} // namespace vibestudio::model_tools
