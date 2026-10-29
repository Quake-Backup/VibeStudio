#include "core/model_frame_tools.h"

#include "core/model_animation_curves.h"
#include "core/model_collision.h"
#include "core/model_document.h"
#include "core/model_mdl.h"
#include "core/model_pose.h"
#include "core/model_skeletal_edit.h"
#include "core/model_skeleton.h"
#include "core/model_tags.h"

#include <QCoreApplication>
#include <QHash>

#include <algorithm>
#include <cmath>

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

bool validRate(double rate) { return std::isfinite(rate) && rate >= 0 && rate <= 1000 && !(rate > 0 && rate < .001); }

// The stem and first number the frames of a clip are named with after a
// resample: the stem its frames share (else the clip name's), from the number
// the first frame carries (else 1).
void clipNaming(const ModelMesh &mesh, const ModelAnimation &clip, int count, QString *stem, int *start)
{
	const auto shared = modelFrameNameStem(mesh.frames[clip.firstFrame].name);
	bool same = !shared.isEmpty();
	for (int f = clip.firstFrame + 1; same && f < clip.firstFrame + clip.frameCount; ++f)
		same = modelFrameNameStem(mesh.frames[f].name) == shared;
	const auto &first = mesh.frames[clip.firstFrame].name;
	int digits = 0;
	while (digits < first.size() && first[first.size() - 1 - digits].isDigit())
		++digits;
	bool numbered = false;
	*start = digits > 0 && digits < 5 ? first.right(digits).toInt(&numbered) : 1;
	if (!numbered)
		*start = 1;
	*stem = modelClipFrameStem(same ? shared : clip.name, *start + count - 1);
}

// Blends frame `to` of `mesh` toward the pose between source frames a and b
// of `source` (both absolute), `amount` of the way from a to b.
bool blendFrame(ModelMesh *mesh, const ModelMesh &source, int to, int a, int b, double amount, QString *error)
{
	for (int s = 0; s < mesh->surfaces.size(); ++s)
	{
		const auto &first = source.surfaces[s].frames[a], &second = source.surfaces[s].frames[b];
		auto &target = mesh->surfaces[s].frames[to];
		for (int v = 0; v < target.positions.size(); ++v)
		{
			target.positions[v] = interpolateModelPosition(first.positions[v], second.positions[v], amount);
			ModelVec3 normal;
			if (!interpolateModelNormal(first.normals[v], second.normals[v], amount, &normal))
				return fail(error, Text::tr("Opposing normals cancel in a blended pose between frames %1 and %2. Adjust the normals first.")
									   .arg(a)
									   .arg(b));
			target.normals[v] = normal;
		}
	}
	mesh->frames[to].origin = interpolateModelPosition(source.frames[a].origin, source.frames[b].origin, amount);
	for (auto &tag : mesh->tags)
	{
		if (tag.frameIndex != to)
			continue;
		const auto *first = findModelTag(source, tag.name, a);
		const auto *second = findModelTag(source, tag.name, b);
		ModelTag blended;
		if (!first || !second || !interpolateModelTag(*first, *second, amount, &blended))
			return fail(error, Text::tr("Attachment tag %1 has opposite handedness in frames %2 and %3, so it cannot be blended.")
								   .arg(tag.name)
								   .arg(a)
								   .arg(b));
		tag.origin = blended.origin;
		std::copy(std::begin(blended.axis), std::end(blended.axis), std::begin(tag.axis));
	}
	for (int box = 0; box < mesh->collisionBoxes.size(); ++box)
	{
		const auto &original = source.collisionBoxes[box];
		if (original.framePoses.isEmpty())
			continue;
		ModelCollisionBox sampled;
		if (!sampleModelCollisionBox(original, a, b, amount, &sampled))
			return fail(error, Text::tr("Collision box %1 cannot be blended between frames %2 and %3.").arg(original.name).arg(a).arg(b));
		auto poses = mesh->collisionBoxes[box].framePoses;
		poses[to] = ModelCollisionPose{sampled.centre, sampled.size, sampled.rotation, sampled.hull};
		setModelCollisionFrames(&mesh->collisionBoxes[box], std::move(poses));
	}
	return true;
}

// The resulting clip list when clip `index`'s range [first, first + count)
// becomes `resulting` frames long. Clips with the same range follow it,
// clips holding it grow or shrink with it, and later clips move; a clip
// that partly overlaps it or lies strictly inside it is refused.
bool resizedClips(const QVector<ModelAnimation> &clips, int index, int resulting, double rate, QVector<ModelAnimation> *result,
				  QString *error)
{
	const auto &target = clips[index];
	const int a = target.firstFrame, end = a + target.frameCount, delta = resulting - target.frameCount;
	result->clear();
	for (int i = 0; i < clips.size(); ++i)
	{
		auto clip = clips[i];
		const int ca = clip.firstFrame, cb = ca + clip.frameCount;
		if (i == index || (ca == a && cb == end))
		{
			clip.frameCount = resulting;
			if (i == index)
				clip.framesPerSecond = rate;
		}
		else if (cb <= a)
		{
		}
		else if (ca >= end)
			clip.firstFrame += delta;
		else if (ca <= a && cb >= end)
			clip.frameCount += delta;
		else
			return fail(error, Text::tr("Clip %1 covers part of clip %2. Resample or close clips whose ranges do not partly overlap other "
										"clips, or change the ranges first.")
								   .arg(clip.name, target.name));
		result->append(clip);
	}
	return true;
}

// MDL native groups when the frames [first, first + count) become
// `resulting` frames: one timed group keeps its total time, split evenly;
// single native frames stay single.
bool resizedMdlGroups(const ModelMesh &mesh, int first, int count, int resulting, QVector<ModelMdlFrameGroup> *result, QString *error)
{
	const int end = first + count;
	QVector<ModelMdlFrameGroup> before, inside, after;
	for (auto group : mesh.mdl.frameGroups)
	{
		const int gs = group.firstFrame, ge = gs + group.frameCount();
		if (ge <= first)
			before.append(group);
		else if (gs >= end)
		{
			group.firstFrame += resulting - count;
			after.append(group);
		}
		else if (gs >= first && ge <= end)
			inside.append(group);
		else
			return fail(error, Text::tr("The clip's range splits an MDL native frame group. Make the clip match the group, or ungroup "
										"its frames first."));
	}
	const bool singles = std::all_of(inside.cbegin(), inside.cend(), [](const ModelMdlFrameGroup &group) { return group.intervals.isEmpty(); });
	*result = before;
	if (singles)
		for (int i = 0; i < resulting; ++i)
			result->append(ModelMdlFrameGroup{first + i, {}});
	else if (inside.size() == 1)
	{
		const double total = inside[0].intervals.last();
		ModelMdlFrameGroup group{first, {}};
		for (int i = 0; i < resulting; ++i)
			group.intervals.append(float(total * (i + 1) / resulting));
		result->append(group);
	}
	else
		return fail(error, Text::tr("Resample a clip whose range is one MDL native group or single native frames."));
	*result += after;
	return true;
}

bool resampleAnimation(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	const auto &o = edit.resample;
	if (o.clip < 0 || o.clip >= mesh->animations.size())
		return fail(error, Text::tr("Choose an existing clip to resample."));
	if (!validRate(o.sourceFps))
		return fail(error, Text::tr("The source rate must be zero (the clip's saved FPS) or from 0.001 to 1000 FPS."));
	const auto clip = mesh->animations[o.clip];
	const double sourceFps = o.sourceFps > 0 ? o.sourceFps : clip.framesPerSecond;
	QVector<double> positions;
	if (!modelResamplePositions(clip.frameCount, sourceFps, o, &positions, error))
		return false;
	const int a = clip.firstFrame, n = clip.frameCount, m = int(positions.size());
	double rate = o.targetFps;
	if (!(rate > 0) && sourceFps > 0)
		rate = o.cyclic ? sourceFps * m / n : (n > 1 && m > 1 ? sourceFps * (m - 1) / (n - 1) : sourceFps);
	if (rate > 1000)
		return fail(error, Text::tr("The resampled clip would play faster than 1000 FPS."));
	ModelFrameRemap remap;
	if (!resizedClips(mesh->animations, o.clip, m, rate, &remap.animations, error))
		return false;
	if (mesh->mdl.enabled)
	{
		remap.explicitMdlGroups = true;
		if (!resizedMdlGroups(*mesh, a, n, m, &remap.mdlGroups, error))
			return false;
	}
	for (int f = 0; f < a; ++f)
		remap.order.append(f);
	for (double u : std::as_const(positions))
		remap.order.append(a + int(std::lround(u)) % n);
	for (int f = a + n; f < mesh->frames.size(); ++f)
		remap.order.append(f);
	const ModelMesh source = *mesh;
	if (!remapModelFrames(mesh, remap, error, control))
		return false;
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (o.method == ModelResampleMethod::Linear)
		for (int j = 0; j < m; ++j)
		{
			if (!work.step())
				return false;
			const double u = positions[j];
			const int i0 = int(std::floor(u));
			const double amount = u - i0;
			if (amount < 1e-9 || amount > 1 - 1e-9)
				continue;
			if (!blendFrame(mesh, source, a + j, a + i0, a + (i0 + 1) % n, amount, error))
				return false;
		}
	QString stem;
	int start = 1;
	clipNaming(source, clip, m, &stem, &start);
	for (int j = 0; j < m; ++j)
		mesh->frames[a + j].name = modelClipFrameName(stem, start + j);
	return work.check();
}

bool closeAnimationLoop(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	const auto &o = edit.resample;
	if (o.clip < 0 || o.clip >= mesh->animations.size())
		return fail(error, Text::tr("Choose an existing clip to close its loop."));
	const auto clip = mesh->animations[o.clip];
	const int a = clip.firstFrame, n = clip.frameCount, last = a + n - 1;
	if (n < 2)
		return fail(error, Text::tr("Closing a loop needs a clip of at least two frames."));
	if (o.loopClose == ModelLoopClose::CrossFade && (o.blendFrames < 1 || o.blendFrames > n - 1))
		return fail(error, Text::tr("Cross-fade 1 to %1 frames of this clip toward its first pose.").arg(n - 1));
	if (o.loopClose != ModelLoopClose::CrossFade && o.blendFrames != 0)
		return fail(error, Text::tr("Blend frames apply only to a cross-fade."));
	const ModelMesh source = *mesh;
	ModelFrameRemap remap;
	for (int f = 0; f < mesh->frames.size(); ++f)
		remap.order.append(f);
	remap.animations = mesh->animations;
	if (o.loopClose == ModelLoopClose::ReplaceLast)
	{
		remap.order[last] = a;
		remap.explicitMdlGroups = true;
		remap.mdlGroups = mesh->mdl.frameGroups;
	}
	else if (o.loopClose == ModelLoopClose::AppendFirst)
	{
		if (!resizedClips(mesh->animations, o.clip, n + 1, clip.framesPerSecond, &remap.animations, error))
			return false;
		remap.order.insert(last + 1, a);
	}
	else
	{
		remap.explicitMdlGroups = true;
		remap.mdlGroups = mesh->mdl.frameGroups;
	}
	if (!remapModelFrames(mesh, remap, error, control))
		return false;
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (o.loopClose == ModelLoopClose::ReplaceLast)
		mesh->frames[last].name = source.frames[last].name;
	else if (o.loopClose == ModelLoopClose::AppendFirst)
	{
		QString stem;
		int start = 1;
		clipNaming(source, clip, n + 1, &stem, &start);
		const bool numbered = std::all_of(source.frames.cbegin() + a, source.frames.cbegin() + a + n,
										  [&](const ModelFrameInfo &frame) { return modelFrameNameStem(frame.name) == modelFrameNameStem(source.frames[a].name); });
		mesh->frames[last + 1].name = numbered ? modelClipFrameName(stem, start + n) : source.frames[a].name;
	}
	else
		for (int k = 1; k <= o.blendFrames; ++k)
		{
			if (!work.step())
				return false;
			const int frame = last - o.blendFrames + k;
			// Blend toward the first pose: amount k / (N + 1) from this frame.
			if (!blendFrame(mesh, source, frame, frame, a, double(k) / (o.blendFrames + 1), error))
				return false;
		}
	return work.check();
}

// Parent-space pose between two model-space poses.
QVector<ModelJointMatrix> blendPose(const QVector<int> &parents, const QVector<ModelJointMatrix> &first, const QVector<ModelJointMatrix> &second,
									double amount)
{
	const auto a = modelJointsToLocalSpace(parents, first), b = modelJointsToLocalSpace(parents, second);
	QVector<ModelJointMatrix> local;
	for (int joint = 0; joint < a.size(); ++joint)
		local.append(modelJointMatrix(modelQuatSlerp(modelJointRotation(a[joint]), modelJointRotation(b[joint]), float(amount)),
									  interpolateModelPosition(modelJointTranslation(a[joint]), modelJointTranslation(b[joint]), amount),
									  interpolateModelPosition(modelJointScale(a[joint]), modelJointScale(b[joint]), amount)));
	return modelJointsToModelSpace(parents, local);
}

// Resizes skeletal clip `clip` to `samples` and writes them, with `curves`
// (possibly none), through the shared skeletal services.
bool replaceSkeletalClip(ModelMesh *mesh, int clip, const QVector<QVector<ModelJointMatrix>> &samples, const QVector<ModelPoseCurves> &curves,
						 double rate, QString *error, const ModelWorkControl &control)
{
	mesh->skeleton.clips[clip].curves.clear();
	if (samples.size() != mesh->skeleton.clips[clip].frames.size())
	{
		ModelEdit resize;
		resize.kind = ModelEditKind::ResizeSkeletalClip;
		resize.skeletal.clip = clip;
		resize.skeletal.frameCount = int(samples.size());
		if (!applyModelSkeletalEdit(mesh, resize, error, control))
			return false;
	}
	if (!replaceModelSkeletalSamples(mesh, clip, 0, samples, error, control, &curves))
		return false;
	mesh->skeleton.clips[clip].framesPerSecond = rate;
	return true;
}

bool resampleSkeletalClip(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	const auto &o = edit.resample;
	auto &skeleton = mesh->skeleton;
	if (o.clip < 0 || o.clip >= skeleton.clips.size())
		return fail(error, Text::tr("Choose an existing skeletal clip to resample."));
	if (!validRate(o.sourceFps))
		return fail(error, Text::tr("The source rate must be zero (the clip's saved FPS) or from 0.001 to 1000 FPS."));
	const auto clip = skeleton.clips[o.clip];
	const int n = int(clip.frames.size());
	const double sourceFps = o.sourceFps > 0 ? o.sourceFps : clip.framesPerSecond;
	QVector<double> positions;
	if (!modelResamplePositions(n, sourceFps, o, &positions, error))
		return false;
	const int m = int(positions.size());
	double rate = o.targetFps;
	if (!(rate > 0))
		rate = sourceFps > 0 ? (o.cyclic ? sourceFps * m / n : (n > 1 && m > 1 ? sourceFps * (m - 1) / (n - 1) : sourceFps)) : 0;
	if (rate > 1000)
		return fail(error, Text::tr("The resampled clip would play faster than 1000 FPS."));
	const bool curved = !clip.curves.isEmpty() && !o.bakeCurves;
	QVector<ModelPoseCurves> curves;
	if (curved)
	{
		if (o.cyclic || n < 2 || m < 2)
			return fail(error, Text::tr("Clip %1 has animation curves. Resample it as a non-looping clip of at least two frames, or choose "
										"Bake curves to sample the motion and remove the curves.")
								   .arg(clip.name));
		const double factor = double(m - 1) / (n - 1);
		for (const auto &group : clip.curves)
		{
			ModelPoseCurves scaled;
			scaled.joint = group.joint;
			for (int channel = 0; channel < 9; ++channel)
			{
				QString why;
				if (!sliceModelCurve(group.channels[channel], 0, n - 1, 0, factor, false, m, &scaled.channels[channel], &why, control))
					return fail(error, Text::tr("A key of joint %1 would land between frames of the resampled clip. Choose a frame count "
												"that maps every key to a whole frame, or choose Bake curves.")
										   .arg(group.joint));
			}
			curves.append(scaled);
		}
	}
	const auto parents = modelJointParents(skeleton);
	QHash<int, int> curveOfJoint;
	for (int g = 0; g < curves.size(); ++g)
		curveOfJoint.insert(modelJointIndex(skeleton, curves[g].joint, Qt::CaseSensitive), g);
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	QVector<QVector<ModelJointMatrix>> samples;
	for (int j = 0; j < m; ++j)
	{
		if (!work.step())
			return false;
		const double u = positions[j];
		const int i0 = std::min(int(std::floor(u)), n - 1);
		const double amount = u - i0;
		QVector<ModelJointMatrix> pose;
		if (o.method == ModelResampleMethod::Nearest || amount < 1e-9)
			pose = clip.frames[int(std::lround(u)) % n];
		else
			pose = blendPose(parents, clip.frames[i0], clip.frames[(i0 + 1) % n], amount);
		if (!curveOfJoint.isEmpty())
		{
			// Keyed joints follow their rescaled curves exactly.
			auto local = modelJointsToLocalSpace(parents, pose);
			for (auto it = curveOfJoint.cbegin(); it != curveOfJoint.cend(); ++it)
				if (it.key() >= 0)
					local[it.key()] = evaluateModelPoseCurves(curves[it.value()], j);
			pose = modelJointsToModelSpace(parents, local);
		}
		samples.append(pose);
	}
	return replaceSkeletalClip(mesh, o.clip, samples, curves, rate, error, control) && work.check();
}

bool closeSkeletalLoop(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	const auto &o = edit.resample;
	auto &skeleton = mesh->skeleton;
	if (o.clip < 0 || o.clip >= skeleton.clips.size())
		return fail(error, Text::tr("Choose an existing skeletal clip to close its loop."));
	const auto clip = skeleton.clips[o.clip];
	const int n = int(clip.frames.size());
	if (n < 2)
		return fail(error, Text::tr("Closing a loop needs a clip of at least two frames."));
	if (o.loopClose == ModelLoopClose::CrossFade && (o.blendFrames < 1 || o.blendFrames > n - 1))
		return fail(error, Text::tr("Cross-fade 1 to %1 frames of this clip toward its first pose.").arg(n - 1));
	if (o.loopClose != ModelLoopClose::CrossFade && o.blendFrames != 0)
		return fail(error, Text::tr("Blend frames apply only to a cross-fade."));
	auto samples = clip.frames;
	const auto parents = modelJointParents(skeleton);
	if (o.loopClose == ModelLoopClose::ReplaceLast)
		samples.last() = samples.first();
	else if (o.loopClose == ModelLoopClose::AppendFirst)
		samples.append(samples.first());
	else
		for (int k = 1; k <= o.blendFrames; ++k)
		{
			const int frame = n - 1 - o.blendFrames + k;
			samples[frame] = blendPose(parents, clip.frames[frame], clip.frames.first(), double(k) / (o.blendFrames + 1));
		}
	QVector<ModelPoseCurves> curves;
	if (!clip.curves.isEmpty() && !o.bakeCurves)
	{
		// Curves keep authority: the same close is made on each channel's
		// keys, and keyed joints take their curves' values in every sample.
		const int count = int(samples.size());
		for (auto group : clip.curves)
		{
			for (int channel = 0; channel < 9; ++channel)
			{
				auto &keys = group.channels[channel];
				const double start = evaluateModelCurve(keys, 0);
				// Rotation channels close to the equivalent angle nearest the
				// current value, so the joint does not spin a full turn.
				const auto target = [&](double near)
				{ return channel >= 3 && channel <= 5 ? start + 360.0 * std::round((near - start) / 360.0) : start; };
				const auto setKey = [&](int frame, double value)
				{
					auto key = sampleModelCurveKey(keys, std::min(frame, n - 1));
					key.frame = frame;
					key.value = value;
					const auto at = std::lower_bound(keys.begin(), keys.end(), frame,
													 [](const ModelPoseCurveKey &existing, int f) { return existing.frame < f; });
					if (at != keys.end() && at->frame == frame)
						*at = key;
					else
						keys.insert(at, key);
				};
				if (o.loopClose == ModelLoopClose::ReplaceLast)
					setKey(n - 1, target(evaluateModelCurve(keys, n - 1)));
				else if (o.loopClose == ModelLoopClose::AppendFirst)
					setKey(n, target(evaluateModelCurve(keys, n - 1)));
				else
				{
					QVector<double> values;
					for (int k = 1; k <= o.blendFrames; ++k)
						values.append(evaluateModelCurve(keys, n - 1 - o.blendFrames + k));
					for (int k = 1; k <= o.blendFrames; ++k)
					{
						const double value = values[k - 1], amount = double(k) / (o.blendFrames + 1);
						setKey(n - 1 - o.blendFrames + k, value + (target(value) - value) * amount);
					}
				}
			}
			QString why;
			if (!validateModelCurveGroup(group, count, &why))
				return fail(error, Text::tr("Closing the loop on the curves of joint %1 failed: %2").arg(group.joint, why));
			curves.append(group);
		}
		QHash<int, int> curveOfJoint;
		for (int g = 0; g < curves.size(); ++g)
			curveOfJoint.insert(modelJointIndex(skeleton, curves[g].joint, Qt::CaseSensitive), g);
		for (int frame = 0; frame < count; ++frame)
		{
			auto local = modelJointsToLocalSpace(parents, samples[frame]);
			for (auto it = curveOfJoint.cbegin(); it != curveOfJoint.cend(); ++it)
				if (it.key() >= 0)
					local[it.key()] = evaluateModelPoseCurves(curves[it.value()], frame);
			samples[frame] = modelJointsToModelSpace(parents, local);
		}
	}
	return replaceSkeletalClip(mesh, o.clip, samples, curves, clip.framesPerSecond, error, control);
}

bool resampleOptionsOnly(const ModelEdit &edit, QString *error)
{
	const auto &o = edit.resample;
	const ModelFrameResampleOptions defaults;
	const bool resample = edit.kind == ModelEditKind::ResampleAnimation || edit.kind == ModelEditKind::ResampleSkeletalClip;
	const bool skeletal = isModelSkeletalResampleEdit(edit.kind);
	if ((!resample && (o.sourceFps != defaults.sourceFps || o.targetFps != defaults.targetFps || o.targetFrames != defaults.targetFrames ||
					   o.method != defaults.method || o.cyclic != defaults.cyclic)) ||
		(resample && (o.loopClose != defaults.loopClose || o.blendFrames != defaults.blendFrames)) || (!skeletal && o.bakeCurves))
		return fail(error, Text::tr("These resampling settings do not apply to this operation."));
	return true;
}
} // namespace

bool modelResamplePositions(int frames, double sourceFps, const ModelFrameResampleOptions &options, QVector<double> *positions, QString *error)
{
	if (!positions)
		return false;
	positions->clear();
	if (frames < 1)
		return fail(error, Text::tr("The clip has no frames to resample."));
	if (!validRate(options.targetFps) || options.targetFrames < 0 || options.targetFrames > modelDocumentMaxFrames ||
		(options.targetFps > 0) == (options.targetFrames > 0))
		return fail(error, Text::tr("Give either a new frame rate from 0.001 to 1000 FPS or a new frame count from 1 to 1024."));
	const double span = options.cyclic ? frames : frames - 1;
	int count = 0;
	double step = 0;
	if (options.targetFrames > 0)
	{
		count = options.targetFrames;
		step = options.cyclic ? span / count : (count > 1 ? span / (count - 1) : 0);
	}
	else
	{
		if (!(sourceFps > 0) || !validRate(sourceFps))
			return fail(error, Text::tr("Set the clip's FPS, or give its current rate, to resample it to a frame rate."));
		step = sourceFps / options.targetFps;
		if (options.cyclic)
		{
			count = std::max(1, int(std::lround(span / step)));
			step = span / count;
		}
		else
		{
			const double samples = std::floor(span / step + 1e-9) + 1;
			count = samples > modelDocumentMaxFrames ? modelDocumentMaxFrames + 1 : int(samples);
		}
	}
	if (count < 1 || count > modelDocumentMaxFrames)
		return fail(error, Text::tr("The resampled clip would hold more than 1024 frames."));
	for (int j = 0; j < count; ++j)
		positions->append(options.cyclic ? j * step : std::min(double(frames - 1), j * step));
	return true;
}

bool isModelSkeletalResampleEdit(ModelEditKind kind)
{
	return kind == ModelEditKind::ResampleSkeletalClip || kind == ModelEditKind::CloseSkeletalLoop;
}

bool applyModelSkeletalResampleEdit(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	if (!candidate || !isModelSkeletalResampleEdit(edit.kind))
		return fail(error, Text::tr("Choose a skeletal resampling operation."));
	if (!resampleOptionsOnly(edit, error))
		return false;
	return edit.kind == ModelEditKind::ResampleSkeletalClip ? resampleSkeletalClip(candidate, edit, error, control)
															: closeSkeletalLoop(candidate, edit, error, control);
}

bool applyModelFrameResampleEdit(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	if (!candidate)
		return fail(error, Text::tr("No editable model is open."));
	if (!resampleOptionsOnly(edit, error))
		return false;
	return edit.kind == ModelEditKind::ResampleAnimation ? resampleAnimation(candidate, edit, error, control)
														 : closeAnimationLoop(candidate, edit, error, control);
}
} // namespace vibestudio
