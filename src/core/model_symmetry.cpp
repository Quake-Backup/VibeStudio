#include "core/model_symmetry.h"

#include "core/model_mesh_tools_p.h"

#include <QMap>
#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>

namespace vibestudio
{
namespace
{
struct SymmetryText
{
	Q_DECLARE_TR_FUNCTIONS(ModelSymmetry)
};
using Key = std::tuple<float, float, float>;
Key keyOf(ModelVec3 p)
{
	return {p.x, p.y, p.z};
}
double along(ModelVec3 p, int axis)
{
	return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
}
ModelVec3 reflected(ModelVec3 p, int axes)
{
	if (axes & 1)
		p.x = -p.x;
	if (axes & 2)
		p.y = -p.y;
	if (axes & 4)
		p.z = -p.z;
	return p;
}
const char *axisName(int axis)
{
	return axis == 0 ? "X" : axis == 1 ? "Y" : "Z";
}
// Every face of the pieces (faces joined through shared geometric vertices)
// that contain one of the seed faces.
QSet<int> pieceFaces(const ModelSurface &surface, const QSet<int> &seeds)
{
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology))
		return {};
	QSet<int> faces, groups;
	QVector<int> stack(seeds.cbegin(), seeds.cend());
	while (!stack.isEmpty())
	{
		const int face = stack.takeLast();
		if (face < 0 || face >= surface.triangles.size() || faces.contains(face))
			continue;
		faces.insert(face);
		const auto &t = surface.triangles[face];
		for (int v : {t.a, t.b, t.c})
		{
			const int group = topology.group(v);
			if (groups.contains(group))
				continue;
			groups.insert(group);
			for (int other : topology.groupFaces.value(group))
				stack.append(other);
		}
	}
	return faces;
}
ModelVec3 centroid(const ModelSurface &surface, int frame, const ModelTriangle &t)
{
	const auto &p = surface.frames[frame].positions;
	return {(p[t.a].x + p[t.b].x + p[t.c].x) / 3, (p[t.a].y + p[t.b].y + p[t.c].y) / 3, (p[t.a].z + p[t.b].z + p[t.c].z) / 3};
}
bool near(ModelVec3 a, ModelVec3 b, double tolerance)
{
	return std::abs(double(a.x) - b.x) <= tolerance && std::abs(double(a.y) - b.y) <= tolerance && std::abs(double(a.z) - b.z) <= tolerance;
}
} // namespace

bool modelSymmetrySupported(ModelEditKind kind)
{
	switch (kind)
	{
	case ModelEditKind::Extrude:
	case ModelEditKind::DuplicateFaces:
	case ModelEditKind::Subdivide:
	case ModelEditKind::DeleteFaces:
	case ModelEditKind::ExtrudeEdges:
	case ModelEditKind::ExtrudeFaces:
	case ModelEditKind::InsetFaces:
	case ModelEditKind::LoopCut:
	case ModelEditKind::SlideEdges:
	case ModelEditKind::BevelVertices:
	case ModelEditKind::BevelEdges:
	case ModelEditKind::Knife:
	case ModelEditKind::MergeVertices:
	case ModelEditKind::DissolveVertices:
	case ModelEditKind::DissolveFaces:
	case ModelEditKind::PokeFaces:
	case ModelEditKind::MakeFace:
	case ModelEditKind::RotateEdges:
	case ModelEditKind::BeautifyFaces:
	case ModelEditKind::ShrinkFatten:
	case ModelEditKind::SmoothVertices:
	case ModelEditKind::Solidify:
	case ModelEditKind::Spin:
	case ModelEditKind::Rip:
	case ModelEditKind::Array:
		return true;
	default:
		return false;
	}
}

bool applySymmetricModelEdit(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *resultingSelection, QString *error,
							 const ModelWorkControl &control, ModelEditReport *report, const ModelEditApplier &apply)
{
	using model_tools::fail;
	const int axes = edit.tool.symmetryAxes;
	if (!mesh || axes < 1 || axes > 7 || !modelSymmetrySupported(edit.kind))
		return fail(error, SymmetryText::tr("Mesh symmetry applies to topology and shape tools on mesh components. Turn Symmetry off for this tool."));
	if (edit.frame >= 0)
		return fail(error, SymmetryText::tr("Symmetric editing changes every animation pose. Use the All frames scope, or turn Symmetry off."));
	const int s = edit.selection.surface;
	if (s < 0 || s >= mesh->surfaces.size() || !edit.selection.surfaces.isEmpty())
		return fail(error, SymmetryText::tr("Select mesh components on a valid surface."));
	const auto &before = mesh->surfaces[s];
	const int frame = isModelMeshToolEdit(edit.kind) ? edit.tool.referenceFrame : edit.pivotFrame;
	if (frame < 0 || frame >= before.frames.size())
		return fail(error, SymmetryText::tr("Choose an existing reference pose."));
	QSet<int> vertices = edit.selection.vertices;
	for (const auto edge : edit.selection.edges)
		vertices << edge.first << edge.second;
	QSet<int> seedFaces;
	for (int face : edit.selection.faces)
	{
		if (face < 0 || face >= before.triangles.size())
			return fail(error, SymmetryText::tr("A selected face no longer exists."));
		const auto &t = before.triangles[face];
		vertices << t.a << t.b << t.c;
		seedFaces.insert(face);
	}
	for (int v : std::as_const(vertices))
	{
		if (v < 0 || v >= before.vertexCount)
			return fail(error, SymmetryText::tr("The selection names a vertex the surface does not have."));
	}
	if (vertices.isEmpty())
		return fail(error, SymmetryText::tr("Select the components to edit symmetrically."));
	double scale = 1;
	for (const auto &p : before.frames[frame].positions)
		scale = std::max({scale, double(std::abs(p.x)), double(std::abs(p.y)), double(std::abs(p.z))});
	const double tolerance = std::max(edit.tool.mergeThreshold, scale * 1e-6);
	const auto &positions = before.frames[frame].positions;
	// Which planes to mirror, and from which side.
	QVector<QPair<int, bool>> planes;
	for (int axis = 0; axis < 3; ++axis)
	{
		if (!(axes & (1 << axis)))
			continue;
		bool positive = false, negative = false;
		for (int v : std::as_const(vertices))
		{
			const double c = along(positions[v], axis);
			positive = positive || c > tolerance;
			negative = negative || c < -tolerance;
		}
		if (!positive && !negative)
			continue;
		if (positive && negative)
		{
			// A selection that mirrors itself edits both halves already.
			std::set<Key> keys;
			for (int v : std::as_const(vertices))
				keys.insert(keyOf(positions[v]));
			bool symmetric = true;
			for (int v : std::as_const(vertices))
			{
				const auto mirror = reflected(positions[v], 1 << axis);
				bool found = keys.count(keyOf(mirror)) > 0;
				for (auto w = vertices.cbegin(); !found && w != vertices.cend(); ++w)
					found = near(positions[*w], mirror, tolerance);
				symmetric = symmetric && found;
				if (!symmetric)
					break;
			}
			if (symmetric)
				continue;
			return fail(error, SymmetryText::tr("The selection lies on both sides of the %1 mirror plane. Select one side, or turn Symmetry %1 off.")
								   .arg(QLatin1String(axisName(axis))));
		}
		planes.append(qMakePair(axis, positive));
	}
	// The pieces the selection belongs to, remembered by position so they can
	// be found again after the tool renumbers the surface.
	if (seedFaces.isEmpty())
	{
		for (int face = 0; face < before.triangles.size(); ++face)
		{
			const auto &t = before.triangles[face];
			if (vertices.contains(t.a) || vertices.contains(t.b) || vertices.contains(t.c))
				seedFaces.insert(face);
		}
	}
	std::set<Key> anchors;
	for (int face : pieceFaces(before, seedFaces))
	{
		const auto &t = before.triangles[face];
		for (int v : {t.a, t.b, t.c})
			anchors.insert(keyOf(positions[v]));
	}
	auto candidate = *mesh;
	auto inner = edit;
	inner.tool.symmetryAxes = 0;
	inner.tool.mirrorJoints.clear();
	inner.tool.keepJointNames = false;
	ModelSelection selection;
	ModelEditReport innerReport;
	if (!apply(&candidate, inner, &selection, error, control, report ? &innerReport : nullptr))
		return false;
	if (!planes.isEmpty())
	{
		if (s >= candidate.surfaces.size())
			return fail(error, SymmetryText::tr("The tool changed the surface list; edit symmetrically on one surface."));
		const auto &edited = candidate.surfaces[s];
		const auto &moved = edited.frames[frame].positions;
		// The tool's result, by position, to select on both sides afterwards.
		QVector<ModelVec3> resultVertices, resultFaces;
		QVector<QPair<ModelVec3, ModelVec3>> resultEdges;
		for (int v : std::as_const(selection.vertices))
			resultVertices << moved[v];
		for (const auto edge : std::as_const(selection.edges))
			resultEdges << qMakePair(moved[edge.first], moved[edge.second]);
		for (int face : std::as_const(selection.faces))
			resultFaces << centroid(edited, frame, edited.triangles[face]);
		QSet<int> seeds = selection.faces;
		for (int face = 0; face < edited.triangles.size(); ++face)
		{
			const auto &t = edited.triangles[face];
			for (int v : {t.a, t.b, t.c})
			{
				if (selection.vertices.contains(v) || anchors.count(keyOf(moved[v])))
					seeds.insert(face);
			}
			for (const auto edge : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
			{
				if (selection.edges.contains(edge))
					seeds.insert(face);
			}
		}
		auto region = pieceFaces(edited, seeds);
		int mirroredAxes = 0;
		for (const auto &[axis, positive] : std::as_const(planes))
		{
			ModelEdit mirror;
			mirror.kind = ModelEditKind::Symmetrize;
			mirror.selection = {s, {}, region};
			mirror.tool.axis = axis;
			mirror.tool.positiveToNegative = positive;
			mirror.tool.mergeThreshold = edit.tool.mergeThreshold;
			mirror.tool.referenceFrame = frame;
			mirror.tool.mirrorJoints = edit.tool.mirrorJoints;
			mirror.tool.keepJointNames = edit.tool.keepJointNames;
			ModelSelection mirrored;
			if (!apply(&candidate, mirror, &mirrored, error, control, nullptr))
				return false;
			region = pieceFaces(candidate.surfaces[s], mirrored.faces);
			mirroredAxes |= 1 << axis;
		}
		// Select the tool's result and its mirror images.
		const auto &surface = candidate.surfaces[s];
		const auto &now = surface.frames[frame].positions;
		const auto matches = [&](ModelVec3 p, const QVector<ModelVec3> &targets)
		{
			for (int combination = 0; combination <= mirroredAxes; ++combination)
			{
				if ((combination & ~mirroredAxes) != 0)
					continue;
				for (const auto &target : targets)
				{
					if (near(p, reflected(target, combination), tolerance))
						return true;
				}
			}
			return false;
		};
		ModelSelection result{s, {}, {}};
		for (int v = 0; v < surface.vertexCount && !resultVertices.isEmpty(); ++v)
		{
			if (matches(now[v], resultVertices))
				result.vertices.insert(v);
		}
		for (int face = 0; face < surface.triangles.size(); ++face)
		{
			const auto &t = surface.triangles[face];
			if (!resultFaces.isEmpty() && matches(centroid(surface, frame, t), resultFaces))
				result.faces.insert(face);
			for (const auto edge : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
			{
				for (int combination = 0; combination <= mirroredAxes && !resultEdges.isEmpty(); ++combination)
				{
					if ((combination & ~mirroredAxes) != 0)
						continue;
					for (const auto &[a, b] : std::as_const(resultEdges))
					{
						const auto x = reflected(a, combination), y = reflected(b, combination);
						if ((near(now[edge.first], x, tolerance) && near(now[edge.second], y, tolerance)) ||
							(near(now[edge.first], y, tolerance) && near(now[edge.second], x, tolerance)))
							result.edges.insert(edge);
					}
				}
			}
		}
		selection = result;
		// Face provenance no longer follows the tool's own report.
		innerReport.sourceFaces.remove(s);
	}
	*mesh = std::move(candidate);
	if (resultingSelection)
		*resultingSelection = std::move(selection);
	if (report)
		*report = std::move(innerReport);
	return true;
}

namespace model_tools
{
bool mirrorFaces(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	const auto &options = edit.tool;
	if (selection->faces.isEmpty())
		return fail(error, SymmetryText::tr("Select the faces to mirror."));
	auto &surface = mesh->surfaces[selection->surface];
	const int axis = options.axis;
	const double plane = options.point[axis];
	if (!surface.skinning.isEmpty() && plane != 0)
		return fail(error, SymmetryText::tr("Mirror skinned faces across the model's own planes: put the mirror point (the 3D cursor) on the %1 = 0 plane.")
							   .arg(QLatin1String(axisName(axis))));
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error))
		return false;
	QSet<int> groups;
	for (int face : selection->faces)
	{
		if (face < 0 || face >= surface.triangles.size())
			return fail(error, SymmetryText::tr("A selected face no longer exists."));
		const auto &t = surface.triangles[face];
		groups << topology.group(t.a) << topology.group(t.b) << topology.group(t.c);
	}
	// A face outside the selection that shares a corner would be torn or folded.
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!work.step())
			return false;
		if (selection->faces.contains(face))
			continue;
		const auto &t = surface.triangles[face];
		if (groups.contains(topology.group(t.a)) || groups.contains(topology.group(t.b)) || groups.contains(topology.group(t.c)))
			return fail(error, SymmetryText::tr("Face %1 shares a corner with the selection but is not selected. Select the whole piece (Select Linked) "
												"or separate it before mirroring.")
								   .arg(face));
	}
	QSet<int> vertices;
	for (int group : std::as_const(groups))
	{
		for (int copy : topology.copies[group])
			vertices.insert(copy);
	}
	for (auto &pose : surface.frames)
	{
		for (int v : std::as_const(vertices))
		{
			if (!work.step())
				return false;
			auto &p = pose.positions[v];
			auto &n = pose.normals[v];
			(axis == 0 ? p.x : axis == 1 ? p.y : p.z) = float(2 * plane - along(p, axis));
			(axis == 0 ? n.x : axis == 1 ? n.y : n.z) *= -1.0f;
		}
	}
	// Skinned vertices take their mirrored joints through the joint mirror map.
	for (int v : std::as_const(vertices))
		mirrorModelVertexOrigin(&surface, v, axis);
	for (int face : selection->faces)
		std::swap(surface.triangles[face].b, surface.triangles[face].c);
	return work.check();
}
} // namespace model_tools
} // namespace vibestudio
