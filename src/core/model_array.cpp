#include "core/model_mesh_tools_p.h"

#include <QMap>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

// Array copies the selected faces along a per-copy transform, the edit-mode
// counterpart of Blender's Array modifier (count, constant and relative
// offsets, object-offset rotation and scale, merge distance) and 3ds Max's
// Array tool. Behaviour follows their documentation (Blender manual 4.0,
// modeling/modifiers/generate/array.html; Autodesk 3ds Max Array dialog,
// reviewed 2026-10-28); no code is used.
namespace vibestudio::model_tools
{
namespace
{
struct ArrayText
{
	Q_DECLARE_TR_FUNCTIONS(ModelArray)
};
QVector<int> sortedSet(const QSet<int> &values)
{
	auto result = values.values().toVector();
	std::sort(result.begin(), result.end());
	return result;
}
// XYZ Euler rotation in degrees, as rotateModelVector, in double precision.
P3 rotateXyz(P3 p, P3 degrees)
{
	const double rx = degrees.x * std::numbers::pi / 180, ry = degrees.y * std::numbers::pi / 180, rz = degrees.z * std::numbers::pi / 180;
	const P3 x{p.x, p.y * std::cos(rx) - p.z * std::sin(rx), p.y * std::sin(rx) + p.z * std::cos(rx)};
	const P3 y{x.x * std::cos(ry) + x.z * std::sin(ry), x.y, -x.x * std::sin(ry) + x.z * std::cos(ry)};
	return {y.x * std::cos(rz) - y.y * std::sin(rz), y.x * std::sin(rz) + y.y * std::cos(rz), y.z};
}
} // namespace

bool arrayFaces(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control,
				ModelEditReport *report)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	const auto &options = edit.tool;
	const auto finite = [](const std::array<double, 3> &values, double limit)
	{ return std::all_of(values.begin(), values.end(), [&](double v) { return std::isfinite(v) && std::abs(v) <= limit; }); };
	if (options.arrayCount < 2 || options.arrayCount > 256 || !finite(options.arrayOffset, 1000000) || !finite(options.arrayRelative, 1000) ||
		!finite(options.arrayRotation, 360) || !std::isfinite(options.arrayScale) || options.arrayScale < 0.001 || options.arrayScale > 1000 ||
		!std::isfinite(options.arrayMerge) || options.arrayMerge < 0 || options.arrayMerge > 1000)
		return fail(error, ArrayText::tr("Array needs 2–256 copies, offsets within ±1,000,000 units (relative ±1,000), rotations within ±360 degrees, "
										 "a scale from 0.001 to 1,000 and a merge distance from 0 to 1,000."));
	const auto faces = sortedSet(selection->faces);
	if (faces.isEmpty())
		return fail(error, ArrayText::tr("Select the faces to copy in an array."));
	auto &surface = mesh->surfaces[selection->surface];
	for (int face : faces)
	{
		if (face < 0 || face >= surface.triangles.size())
			return fail(error, ArrayText::tr("A selected face no longer exists."));
	}
	const int reference = options.referenceFrame;
	QSet<int> cornerSet;
	for (int face : faces)
	{
		const auto &t = surface.triangles[face];
		cornerSet << t.a << t.b << t.c;
	}
	const auto corners = sortedSet(cornerSet);
	// The per-copy transform: rotate and scale about the centre, then move.
	double low[3]{INFINITY, INFINITY, INFINITY}, high[3]{-INFINITY, -INFINITY, -INFINITY};
	for (int v : corners)
	{
		const auto p = position(surface, reference, v);
		const double values[3]{p.x, p.y, p.z};
		for (int axis = 0; axis < 3; ++axis)
		{
			low[axis] = std::min(low[axis], values[axis]);
			high[axis] = std::max(high[axis], values[axis]);
		}
	}
	const P3 offset{options.arrayOffset[0] + options.arrayRelative[0] * (high[0] - low[0]),
					options.arrayOffset[1] + options.arrayRelative[1] * (high[1] - low[1]),
					options.arrayOffset[2] + options.arrayRelative[2] * (high[2] - low[2])};
	const P3 rotation = arrayPoint(options.arrayRotation), centre = arrayPoint(options.point);
	if (length(offset) < 1e-6 && length(rotation) < 1e-9 && options.arrayScale == 1)
		return fail(error, ArrayText::tr("Every copy would lie on the selection. Give the array an offset, a rotation or a scale."));
	const auto place = [&](P3 p) { return centre + rotateXyz((p - centre) * options.arrayScale, rotation) + offset; };
	const qint64 copies = options.arrayCount - 1;
	if (!reserveCapacity(*mesh, selection->surface, copies * corners.size(), copies * faces.size(), error))
		return false;
	if (!materialiseModelVertexIdentities(&surface, error, control))
		return false;
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error, control))
		return false;
	// Border groups of the selected region, for merging neighbouring copies.
	QHash<ModelEdge, int> uses;
	for (int face : faces)
	{
		const auto &t = surface.triangles[face];
		for (const auto edge : {topology.geometricEdge(t.a, t.b), topology.geometricEdge(t.b, t.c), topology.geometricEdge(t.c, t.a)})
			++uses[edge];
	}
	QSet<int> borderGroups;
	for (auto i = uses.cbegin(); i != uses.cend(); ++i)
	{
		if (i.value() == 1)
			borderGroups << i.key().first << i.key().second;
	}
	QMap<int, QVector<int>> groupCorners; // border group -> its selected corner indices
	for (int v : corners)
	{
		if (borderGroups.contains(topology.group(v)))
			groupCorners[topology.group(v)].append(v);
	}
	const auto seams = surface.uvSeams;
	QVector<QHash<int, int>> layers{{}};
	for (int v : corners)
		layers[0].insert(v, v);
	QVector<int> faceSources(surface.triangles.size());
	std::iota(faceSources.begin(), faceSources.end(), 0);
	QSet<int> touched;
	QSet<int> resultFaces(selection->faces);
	const int firstCopyFace = int(surface.triangles.size());
	for (int copy = 1; copy <= copies; ++copy)
	{
		QHash<int, int> layer, identities;
		for (int v : corners)
		{
			if (!work.step())
				return false;
			const int previous = layers[copy - 1].value(v);
			const int added = appendCopy(&surface, previous);
			detachModelVertexIdentity(&surface, added, &identities);
			for (auto &frame : surface.frames)
			{
				frame.positions[added] = vec(place(point(frame.positions[previous])));
				frame.normals[added] = vec(unit(rotateXyz(point(frame.normals[previous]), rotation)));
			}
			layer.insert(v, added);
			touched.insert(added);
		}
		for (int face : faces)
		{
			if (!work.step())
				return false;
			const auto &t = surface.triangles[face];
			resultFaces.insert(surface.triangles.size());
			surface.triangles.append({layer.value(t.a), layer.value(t.b), layer.value(t.c)});
			faceSources.append(face);
		}
		for (const auto edge : seams)
		{
			if (layer.contains(edge.first) && layer.contains(edge.second))
				surface.uvSeams.insert(modelEdge(layer.value(edge.first), layer.value(edge.second)));
		}
		layers.append(layer);
	}
	// Merge: a border vertex of each copy joins the nearest border vertex of
	// the copy before it when they lie within the distance in every pose. The
	// pair shares one identity and position; their UVs stay their own.
	if (options.arrayMerge > 0)
	{
		for (int copy = 1; copy <= copies; ++copy)
		{
			for (auto g = groupCorners.cbegin(); g != groupCorners.cend(); ++g)
			{
				if (!work.step())
					return false;
				const int moving = layers[copy].value(g.value().first());
				const auto here = position(surface, reference, moving);
				int target = -1;
				double best = options.arrayMerge;
				for (auto h = groupCorners.cbegin(); h != groupCorners.cend(); ++h)
				{
					const int candidate = layers[copy - 1].value(h.value().first());
					const double d = length(position(surface, reference, candidate) - here);
					if (d <= best)
					{
						best = d;
						target = candidate;
					}
				}
				if (target < 0)
					continue;
				bool everyPose = true;
				for (int frame = 0; frame < surface.frames.size() && everyPose; ++frame)
					everyPose = length(position(surface, frame, target) - position(surface, frame, moving)) <= options.arrayMerge;
				if (!everyPose)
					continue;
				for (int v : g.value())
				{
					const int index = layers[copy].value(v);
					for (auto &frame : surface.frames)
						frame.positions[index] = frame.positions[target];
					surface.topologyVertices[index] = surface.topologyVertices[target];
					touched << index << target;
				}
			}
		}
	}
	for (int v : corners)
		touched.insert(v);
	// A merge can leave a copy's face with no area; refuse rather than keep it.
	for (int face = firstCopyFace; face < surface.triangles.size(); ++face)
	{
		if (collapsedInAnyFrame(surface, surface.triangles[face]))
			return fail(error, ArrayText::tr("Merging the copies collapses a face. Lower the merge distance or change the offset."));
	}
	if (!refreshNormals(&surface, touched, work))
		return false;
	selection->faces = resultFaces;
	selection->vertices.clear();
	selection->edges.clear();
	if (report)
		report->sourceFaces.insert(selection->surface, faceSources);
	return work.check();
}
} // namespace vibestudio::model_tools
