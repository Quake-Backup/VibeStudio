#include "core/model_frame_tools.h"

#include "core/model_document.h"
#include "core/model_mesh_tools_p.h"
#include "core/model_pose.h"
#include "core/model_transform.h"
#include "core/model_transform_axes.h"

#include <QCoreApplication>

#include <cmath>
#include <numbers>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelFrameTools)
};

bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}

bool finiteVector(ModelVec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
} // namespace

double modelFrameFalloffWeight(ModelFrameFalloff falloff, int index, int count)
{
	if (count <= 0 || index < 0 || index >= count)
		return 0;
	switch (falloff)
	{
	case ModelFrameFalloff::Constant:
		return 1;
	case ModelFrameFalloff::RampIn:
		return double(index + 1) / count;
	case ModelFrameFalloff::RampOut:
		return double(count - index) / count;
	case ModelFrameFalloff::Smooth:
	{
		const double s = std::sin(std::numbers::pi * (index + 1) / (count + 1));
		return s * s;
	}
	}
	return 1;
}

bool isModelPoseRangeEdit(ModelEditKind kind)
{
	return kind == ModelEditKind::TransformFrameRange || kind == ModelEditKind::BlendFramePose || kind == ModelEditKind::ApplyRelativePose;
}

bool applyModelPoseRangeEdit(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!mesh || !isModelPoseRangeEdit(edit.kind) || !work.check())
		return fail(error, Text::tr("Choose a range transform or pose blend."));
	const auto &o = edit.poseRange;
	const int frames = int(mesh->frames.size());
	if (o.first < 0 || o.last < o.first || o.last >= frames)
		return fail(error, Text::tr("Choose a frame range inside the model, with the first frame no later than the last."));
	if (int(o.falloff) < int(ModelFrameFalloff::Constant) || int(o.falloff) > int(ModelFrameFalloff::Smooth))
		return fail(error, Text::tr("Choose a constant, ramp-in, ramp-out or smooth falloff."));
	if (edit.frame != -1)
		return fail(error, Text::tr("Range edits use their frame range, not one frame."));
	const bool transform = edit.kind == ModelEditKind::TransformFrameRange, blend = edit.kind == ModelEditKind::BlendFramePose;
	const bool relative = edit.kind == ModelEditKind::ApplyRelativePose;
	if ((transform && (o.sourceFrame != -1 || o.baseFrame != -1 || o.amount != 1)) || (blend && o.baseFrame != -1))
		return fail(error, Text::tr("These pose range settings do not apply to this operation."));
	if (!transform && (o.sourceFrame < 0 || o.sourceFrame >= frames))
		return fail(error, Text::tr("Choose an existing frame to take the pose from."));
	if (relative && (o.baseFrame < 0 || o.baseFrame >= frames || o.baseFrame == o.sourceFrame))
		return fail(error, Text::tr("Choose two different existing frames: the pose and the base it differs from."));
	if (!std::isfinite(o.amount) || (blend && (o.amount < 0 || o.amount > 1)) || (relative && std::abs(o.amount) > 10))
		return fail(error, Text::tr("The blend amount must be from 0 to 1, and a difference weight from -10 to 10."));
	if (!transform && (edit.translation.x != 0 || edit.translation.y != 0 || edit.translation.z != 0 || edit.rotation.x != 0 ||
					   edit.rotation.y != 0 || edit.rotation.z != 0 || edit.scale.x != 1 || edit.scale.y != 1 || edit.scale.z != 1))
		return fail(error, Text::tr("Move, rotate and scale values apply only to range transforms."));
	auto &surface = mesh->surfaces[edit.selection.surface];
	const auto vertices = model_tools::selectedVertexSet(surface, edit.selection);
	if (vertices.isEmpty())
		return fail(error, Text::tr("Select the vertices, edges or faces to edit."));
	const bool whole = vertices.size() == surface.vertexCount;
	// Every corner of a face that touches a moved vertex gets a new normal.
	QSet<int> affected;
	for (const auto &t : std::as_const(surface.triangles))
		if (vertices.contains(t.a) || vertices.contains(t.b) || vertices.contains(t.c))
			affected << t.a << t.b << t.c;
	ModelTransform resolved;
	if (transform)
	{
		if (!finiteVector(edit.translation) || !finiteVector(edit.rotation) || !finiteVector(edit.scale))
			return fail(error, Text::tr("Transform values must be finite."));
		if (double(edit.scale.x) * edit.scale.y * edit.scale.z <= 0)
			return fail(error, Text::tr("Range transforms cannot mirror or collapse geometry: use positive scale factors."));
		if (edit.pivotFrame < 0 || edit.pivotFrame >= frames ||
			!modelTransformPivot(surface.frames[edit.pivotFrame].positions, vertices, edit.pivotMode, edit.pivot, &resolved.pivot))
			return fail(error, Text::tr("Choose a valid transform pivot and reference frame."));
		// World, selection or custom axes, as the Geometry page and the
		// viewport's modal transform choose them.
		if (!resolveModelTransformAxes(*mesh, edit, &resolved.basis, error, control))
			return false;
	}
	const ModelMesh source = *mesh;
	const auto &original = source.surfaces[edit.selection.surface];
	const int count = o.last - o.first + 1;
	for (int k = 0; k < count; ++k)
	{
		const int f = o.first + k;
		const double weight = modelFrameFalloffWeight(o.falloff, k, count) * (transform ? 1.0 : o.amount);
		if (weight == 0)
			continue;
		auto &pose = surface.frames[f];
		const auto &before = original.frames[f];
		ModelTransform step = resolved;
		if (transform)
		{
			step.translation = {float(edit.translation.x * weight), float(edit.translation.y * weight), float(edit.translation.z * weight)};
			step.rotation = {float(edit.rotation.x * weight), float(edit.rotation.y * weight), float(edit.rotation.z * weight)};
			step.scale = {float(1 + (edit.scale.x - 1) * weight), float(1 + (edit.scale.y - 1) * weight),
						  float(1 + (edit.scale.z - 1) * weight)};
		}
		for (int v : vertices)
		{
			if (!work.step())
				return false;
			if (transform)
			{
				pose.positions[v] = transformModelPoint(before.positions[v], step);
				if (whole)
					pose.normals[v] = transformModelNormal(before.normals[v], step);
			}
			else if (blend)
			{
				pose.positions[v] = interpolateModelPosition(before.positions[v], original.frames[o.sourceFrame].positions[v], weight);
				ModelVec3 normal;
				if (whole && interpolateModelNormal(before.normals[v], original.frames[o.sourceFrame].normals[v], weight, &normal))
					pose.normals[v] = normal;
			}
			else
			{
				const auto a = original.frames[o.sourceFrame].positions[v], b = original.frames[o.baseFrame].positions[v];
				pose.positions[v] = {float(before.positions[v].x + (a.x - b.x) * weight), float(before.positions[v].y + (a.y - b.y) * weight),
									 float(before.positions[v].z + (a.z - b.z) * weight)};
			}
			const auto p = pose.positions[v];
			if (!finiteVector(p) || std::abs(p.x) > 1000000 || std::abs(p.y) > 1000000 || std::abs(p.z) > 1000000)
				return fail(error, Text::tr("The edit would move a vertex outside the supported coordinate range."));
		}
		if ((!whole || relative) && !model_tools::refreshNormals(&surface, affected, work, f))
			return false;
	}
	return work.check();
}
} // namespace vibestudio
