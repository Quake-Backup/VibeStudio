#include "core/model_frame_tools.h"

#include "core/model_collision.h"
#include "core/model_document.h"
#include "core/model_mdl.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <utility>

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

bool followsSkeleton(const ModelSurface &surface)
{
	return !surface.skinning.isEmpty() && surface.skinning.first.size() == surface.vertexCount &&
		   surface.skinning.count.size() == surface.vertexCount;
}

void chopSeparators(QString *text)
{
	while (!text->isEmpty() && (text->back() == QLatin1Char('_') || text->back() == QLatin1Char('-')))
		text->chop(1);
}

// MDL native groups rebuilt from a frame order: consecutive resulting frames
// copied from one timed group stay one group, each with its own duration.
QVector<ModelMdlFrameGroup> derivedMdlGroups(const ModelMesh &mesh, const QVector<int> &order)
{
	const auto &groups = mesh.mdl.frameGroups;
	QVector<int> groupOf(mesh.frames.size(), -1);
	QVector<double> duration(mesh.frames.size(), 0);
	for (int g = 0; g < groups.size(); ++g)
		for (int member = 0; member < groups[g].frameCount(); ++member)
		{
			const int frame = groups[g].firstFrame + member;
			if (frame < 0 || frame >= groupOf.size())
				continue;
			groupOf[frame] = g;
			if (!groups[g].intervals.isEmpty())
				duration[frame] = double(groups[g].intervals[member]) - (member ? double(groups[g].intervals[member - 1]) : 0.0);
		}
	QVector<ModelMdlFrameGroup> result;
	int previous = -1;
	double elapsed = 0;
	for (int r = 0; r < order.size(); ++r)
	{
		const int source = order[r];
		const int group = groupOf.value(source, -1);
		if (group < 0 || groups[group].intervals.isEmpty())
		{
			result.append(ModelMdlFrameGroup{r, {}});
			previous = -1;
			continue;
		}
		if (group != previous)
		{
			result.append(ModelMdlFrameGroup{r, {}});
			elapsed = 0;
		}
		elapsed += duration[source];
		result.last().intervals.append(float(elapsed));
		previous = group;
	}
	return result;
}

// Clips after removing frames first..last.
QVector<ModelAnimation> clipsWithout(const QVector<ModelAnimation> &clips, int first, int last)
{
	const int removed = last - first + 1;
	QVector<ModelAnimation> result;
	for (const auto &clip : clips)
	{
		const int a = clip.firstFrame, b = clip.firstFrame + clip.frameCount - 1;
		const int overlap = std::max(0, std::min(b, last) - std::max(a, first) + 1);
		const int count = clip.frameCount - overlap;
		if (count <= 0)
			continue;
		auto kept = clip;
		kept.firstFrame = a < first ? a : (a > last ? a - removed : first);
		kept.frameCount = count;
		result.append(kept);
	}
	return result;
}

// Clips after inserting `count` frames directly after frame `after`: a clip
// holding that frame grows, and clips after it move.
QVector<ModelAnimation> clipsInsertedAfter(QVector<ModelAnimation> clips, int after, int count)
{
	for (auto &clip : clips)
	{
		const int a = clip.firstFrame, b = clip.firstFrame + clip.frameCount - 1;
		if (a > after)
			clip.firstFrame += count;
		else if (b >= after)
			clip.frameCount += count;
	}
	return clips;
}

// Clips after moving first..last so the first moved frame lands at
// `destination`. A clip inside the block moves with it; a clip partly inside
// keeps its other frames and takes the whole block when the block lands next
// to or inside what is left of it; a clip the block lands strictly inside
// grows by the block.
QVector<ModelAnimation> clipsMoved(const QVector<ModelAnimation> &clips, int first, int last, int destination)
{
	const int n = last - first + 1;
	QVector<ModelAnimation> result;
	for (auto clip : clips)
	{
		const int a = clip.firstFrame, b = clip.firstFrame + clip.frameCount - 1;
		if (a >= first && b <= last)
		{
			clip.firstFrame = destination + (a - first);
			result.append(clip);
			continue;
		}
		const bool disjoint = b < first || a > last;
		const int overlap = disjoint ? 0 : std::min(b, last) - std::max(a, first) + 1;
		int start = disjoint ? (a > last ? a - n : a) : std::min(a, first);
		int count = clip.frameCount - overlap;
		const int end = start + count - 1;
		if (disjoint)
		{
			if (start >= destination)
				start += n;
			else if (destination <= end)
				count += n;
		}
		else if (start <= destination && destination <= end + 1)
			count += n;
		else if (start > destination)
			start += n;
		clip.firstFrame = start;
		clip.frameCount = count;
		result.append(clip);
	}
	return result;
}

QString frameRole(const ModelMesh &mesh, int frame)
{
	const int clip = mesh.skeleton.bakedClipForFrame.value(frame, -2);
	if (clip == -1)
		return Text::tr("Frame %1 is the skeleton's bind pose.").arg(frame);
	const auto name = clip >= 0 && clip < mesh.skeleton.clips.size() ? mesh.skeleton.clips[clip].name : QString::number(clip);
	return Text::tr("Frame %1 is baked from skeletal clip %2.").arg(frame).arg(name);
}

bool renameFramesFromClips(ModelMesh *candidate, const ModelFrameRangeOptions &options, QString *error, ModelWorkProgress &work)
{
	auto &clips = candidate->animations;
	if (clips.isEmpty())
		return fail(error, Text::tr("The model has no animation clips to name frames after. Add a clip first."));
	if (options.clip < -1 || options.clip >= clips.size())
		return fail(error, Text::tr("Choose an existing clip, or every clip."));
	if (options.startNumber < 0 || options.startNumber > 1000)
		return fail(error, Text::tr("The first frame number must be from 0 to 1000."));
	QVector<int> chosen;
	if (options.clip >= 0)
		chosen.append(options.clip);
	else
		for (int i = 0; i < clips.size(); ++i)
			chosen.append(i);
	auto byStart = chosen;
	std::sort(byStart.begin(), byStart.end(), [&](int x, int y) { return clips[x].firstFrame < clips[y].firstFrame; });
	for (int i = 1; i < byStart.size(); ++i)
	{
		const auto &left = clips[byStart[i - 1]], &right = clips[byStart[i]];
		if (right.firstFrame < left.firstFrame + left.frameCount)
			return fail(error, Text::tr("Clips %1 and %2 share frames, and a frame has one name. Name one of them at a time, or change "
										"the clip ranges so they do not overlap.")
								   .arg(left.name, right.name));
	}
	const auto before = modelNativeClipRoundTrip(*candidate);
	QHash<QString, int> stems;
	for (int index : std::as_const(chosen))
	{
		if (!work.step())
			return false;
		auto &clip = clips[index];
		const auto stem = modelClipFrameStem(clip.name, options.startNumber + clip.frameCount - 1);
		const auto twin = stems.constFind(stem);
		if (twin != stems.cend())
			return fail(error, Text::tr("Clips %1 and %2 would both name their frames %3, so a reader could not tell them apart. Rename "
										"one clip first.")
								   .arg(clips[twin.value()].name, clip.name, stem));
		stems.insert(stem, index);
		for (int i = 0; i < clip.frameCount; ++i)
			candidate->frames[clip.firstFrame + i].name = modelClipFrameName(stem, options.startNumber + i);
	}
	if (options.renameClips)
	{
		QStringList names;
		for (const auto &clip : std::as_const(clips))
			names.append(clip.name);
		for (auto it = stems.cbegin(); it != stems.cend(); ++it)
			names[it.value()] = it.key();
		for (auto it = stems.cbegin(); it != stems.cend(); ++it)
			if (names.count(it.key()) > 1)
				return fail(error, Text::tr("Clip %1 would be renamed %2, which another clip already uses. Rename one of them first.")
									   .arg(clips[it.value()].name, it.key()));
		for (auto it = stems.cbegin(); it != stems.cend(); ++it)
			clips[it.value()].name = it.key();
	}
	const auto after = modelNativeClipRoundTrip(*candidate);
	for (int index : std::as_const(chosen))
	{
		const auto &check = after[index];
		const bool kept = options.renameClips ? check.survives : check.rebuiltFirst == check.first && check.rebuiltLast == check.last;
		if (!kept)
			return fail(error, Text::tr("Clip %1 would still not come back from MDL, MD2 or MD3 frame names: they give %2 for frames "
										"%3–%4. Rename or move the frames next to the clip first.")
								   .arg(check.name, check.rebuiltName)
								   .arg(check.rebuiltFirst)
								   .arg(check.rebuiltLast));
	}
	for (int index = 0; index < before.size(); ++index)
		if (before[index].survives && !after[index].survives)
			return fail(error, Text::tr("Naming these frames would break clip %1, which now comes back from MDL, MD2 and MD3 frame names. "
										"Choose another clip name first.")
								   .arg(before[index].name));
	return work.check();
}
} // namespace

bool isModelFrameRangeEdit(ModelEditKind kind)
{
	return kind == ModelEditKind::MoveFrames || kind == ModelEditKind::DuplicateFrameRange || kind == ModelEditKind::DeleteFrameRange ||
		   kind == ModelEditKind::ReverseFrameRange || kind == ModelEditKind::RenameFramesFromClips ||
		   kind == ModelEditKind::ResampleAnimation || kind == ModelEditKind::CloseAnimationLoop || kind == ModelEditKind::ImportFrames;
}

bool remapModelFrames(ModelMesh *mesh, const ModelFrameRemap &remap, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	if (!mesh)
		return fail(error, Text::tr("No editable model is open."));
	const int before = mesh->frames.size(), after = remap.order.size();
	if (after < 1 || after > modelDocumentMaxFrames)
		return fail(error, Text::tr("A model keeps from 1 to 1024 frames."));
	qint64 vertices = 0;
	for (const auto &surface : mesh->surfaces)
		vertices += surface.vertexCount;
	if (vertices * after > modelDocumentMaxFrameVertices)
		return fail(error, Text::tr("The frames would pass the animation storage limit of 1,048,576 frame vertices."));
	for (int source : remap.order)
		if (source < 0 || source >= before)
			return fail(error, Text::tr("The frame order names a frame the model does not have."));
	for (const auto &clip : remap.animations)
		if (clip.firstFrame < 0 || clip.frameCount < 1 || clip.firstFrame + clip.frameCount > after)
			return fail(error, Text::tr("A clip range would fall outside the resulting frames."));
	ModelMesh result = *mesh;
	result.frames.clear();
	result.frames.reserve(after);
	for (int r = 0; r < after; ++r)
	{
		auto info = mesh->frames[remap.order[r]];
		info.index = r;
		result.frames.append(info);
	}
	for (int s = 0; s < mesh->surfaces.size(); ++s)
	{
		QVector<ModelFrameGeometry> frames;
		frames.reserve(after);
		for (int r = 0; r < after; ++r)
		{
			if (!work.step())
				return false;
			frames.append(mesh->surfaces[s].frames.value(remap.order[r]));
		}
		result.surfaces[s].frames = std::move(frames);
	}
	QVector<QVector<int>> tagsOf(before);
	for (int i = 0; i < mesh->tags.size(); ++i)
		if (mesh->tags[i].frameIndex >= 0 && mesh->tags[i].frameIndex < before)
			tagsOf[mesh->tags[i].frameIndex].append(i);
	result.tags.clear();
	for (int r = 0; r < after; ++r)
		for (int i : std::as_const(tagsOf[remap.order[r]]))
		{
			if (!work.step())
				return false;
			auto tag = mesh->tags[i];
			tag.frameIndex = r;
			result.tags.append(tag);
		}
	for (int b = 0; b < result.collisionBoxes.size(); ++b)
	{
		const auto &source = mesh->collisionBoxes[b].framePoses;
		if (source.isEmpty())
			continue;
		QVector<ModelCollisionPose> poses;
		poses.reserve(after);
		for (int r = 0; r < after; ++r)
			poses.append(source.value(remap.order[r]));
		setModelCollisionFrames(&result.collisionBoxes[b], std::move(poses));
	}
	result.animations = remap.animations;
	if (mesh->mdl.enabled)
	{
		result.mdl.frameGroups = remap.explicitMdlGroups ? remap.mdlGroups : derivedMdlGroups(*mesh, remap.order);
		if (!validateModelMdl(result).isEmpty())
			return fail(error, Text::tr("The frame edit would leave MDL native frame groups with times that are not strictly increasing. "
										"Adjust the native frame timing first."));
	}
	if (!work.check())
		return false;
	*mesh = std::move(result);
	return true;
}

bool modelFrameRangeOrder(const ModelMesh &mesh, const ModelEdit &edit, QVector<int> *order, QString *error, QVector<bool> *generated)
{
	const auto &options = edit.frameRange;
	const int count = mesh.frames.size();
	if (!order)
		return false;
	order->clear();
	if (generated)
		generated->clear();
	const auto identity = [&]
	{
		for (int f = 0; f < count; ++f)
			order->append(f);
		if (generated)
			generated->fill(false, count);
	};
	if (edit.kind == ModelEditKind::RenameFramesFromClips)
	{
		identity();
		return true;
	}
	if (edit.kind == ModelEditKind::ResampleAnimation || edit.kind == ModelEditKind::CloseAnimationLoop)
	{
		const auto &o = edit.resample;
		if (o.clip < 0 || o.clip >= mesh.animations.size())
			return fail(error, Text::tr("Choose an existing clip."));
		const auto &clip = mesh.animations[o.clip];
		const int a = clip.firstFrame, n = clip.frameCount;
		if (edit.kind == ModelEditKind::ResampleAnimation)
		{
			QVector<double> positions;
			if (!modelResamplePositions(n, o.sourceFps > 0 ? o.sourceFps : clip.framesPerSecond, o, &positions, error))
				return false;
			for (int f = 0; f < a; ++f)
				order->append(f);
			for (double u : std::as_const(positions))
				order->append(a + int(std::lround(u)) % n);
			for (int f = a + n; f < count; ++f)
				order->append(f);
			if (generated)
			{
				generated->fill(false, order->size());
				for (int j = 0; j < positions.size(); ++j)
					(*generated)[a + j] = true;
			}
			return true;
		}
		identity();
		if (n < 2)
			return fail(error, Text::tr("Closing a loop needs a clip of at least two frames."));
		const int last = a + n - 1;
		if (o.loopClose == ModelLoopClose::ReplaceLast)
		{
			(*order)[last] = a;
			if (generated)
				(*generated)[last] = true;
		}
		else if (o.loopClose == ModelLoopClose::AppendFirst)
		{
			order->insert(last + 1, a);
			if (generated)
				generated->insert(last + 1, true);
		}
		else if (generated)
			for (int k = 1; k <= o.blendFrames && last - o.blendFrames + k >= a; ++k)
				(*generated)[last - o.blendFrames + k] = true;
		return true;
	}
	if (edit.kind == ModelEditKind::ImportFrames)
	{
		const auto &o = edit.frameImport;
		const int added = modelFrameSourceCount(o.sources);
		if (added < 1)
			return fail(error, Text::tr("Choose at least one file with frames to import."));
		if (o.replaceFirst >= 0)
		{
			if (o.replaceFirst + added > count)
				return fail(error, Text::tr("The %n imported frames do not fit from that frame; the model has %1 frames.", nullptr, added)
									   .arg(count));
			identity();
			if (generated)
				for (int i = 0; i < added; ++i)
					(*generated)[o.replaceFirst + i] = true;
			return true;
		}
		const int after = o.insertAfter < 0 ? count - 1 : o.insertAfter;
		if (after >= count)
			return fail(error, Text::tr("Choose an existing frame to insert after."));
		if (count + added > modelDocumentMaxFrames)
			return fail(error, Text::tr("Importing %n frames would pass the limit of 1024 frames.", nullptr, added));
		for (int f = 0; f <= after; ++f)
			order->append(f);
		for (int i = 0; i < added; ++i)
			order->append(after);
		for (int f = after + 1; f < count; ++f)
			order->append(f);
		if (generated)
		{
			generated->fill(false, order->size());
			for (int i = 0; i < added; ++i)
				(*generated)[after + 1 + i] = true;
		}
		return true;
	}
	if (options.first < 0 || options.last < options.first || options.last >= count)
		return fail(error, Text::tr("Choose a frame range inside the model, with the first frame no later than the last."));
	const int first = options.first, last = options.last, n = last - first + 1;
	// Range operations copy every frame: `generated` stays empty.
	switch (edit.kind)
	{
	case ModelEditKind::MoveFrames:
	{
		if (options.destination < 0 || options.destination > count - n)
			return fail(error, Text::tr("Choose a destination from 0 to %1: the index the first moved frame takes.").arg(count - n));
		if (options.destination == first)
			return fail(error, Text::tr("The frames are already at that position."));
		QVector<int> rest;
		for (int f = 0; f < count; ++f)
			if (f < first || f > last)
				rest.append(f);
		for (int i = 0; i < options.destination; ++i)
			order->append(rest[i]);
		for (int f = first; f <= last; ++f)
			order->append(f);
		for (int i = options.destination; i < rest.size(); ++i)
			order->append(rest[i]);
		return true;
	}
	case ModelEditKind::DuplicateFrameRange:
		if (count + n > modelDocumentMaxFrames)
			return fail(error, Text::tr("Copying %n frames would pass the limit of 1024 frames.", nullptr, n));
		for (int f = 0; f <= last; ++f)
			order->append(f);
		for (int f = first; f <= last; ++f)
			order->append(f);
		for (int f = last + 1; f < count; ++f)
			order->append(f);
		return true;
	case ModelEditKind::DeleteFrameRange:
		if (n >= count)
			return fail(error, Text::tr("A model must keep at least one frame."));
		for (int f = 0; f < count; ++f)
			if (f < first || f > last)
				order->append(f);
		return true;
	case ModelEditKind::ReverseFrameRange:
		if (options.pingPong)
		{
			if (n < 3)
				return fail(error, Text::tr("A ping-pong loop needs a range of at least three frames."));
			const int added = n - 2;
			if (count + added > modelDocumentMaxFrames)
				return fail(error, Text::tr("Adding %n return frames would pass the limit of 1024 frames.", nullptr, added));
			for (int f = 0; f <= last; ++f)
				order->append(f);
			for (int f = last - 1; f > first; --f)
				order->append(f);
			for (int f = last + 1; f < count; ++f)
				order->append(f);
			return true;
		}
		if (n < 2)
			return fail(error, Text::tr("Reversing needs a range of at least two frames."));
		for (int f = 0; f < first; ++f)
			order->append(f);
		for (int f = last; f >= first; --f)
			order->append(f);
		for (int f = last + 1; f < count; ++f)
			order->append(f);
		return true;
	default:
		break;
	}
	return fail(error, Text::tr("Choose a frame range operation."));
}

QString modelFrameRangeSkeletalRefusal(const ModelMesh &mesh, const ModelEdit &edit)
{
	const auto &map = mesh.skeleton.bakedClipForFrame;
	if (mesh.skeleton.isEmpty() || map.size() != mesh.frames.size() || edit.kind == ModelEditKind::RenameFramesFromClips)
		return {};
	if (std::none_of(mesh.surfaces.cbegin(), mesh.surfaces.cend(), followsSkeleton))
		return {};
	QVector<int> order;
	QVector<bool> generated;
	if (!modelFrameRangeOrder(mesh, edit, &order, nullptr, &generated))
		return {};
	// Frames that follow the skeleton must each stay exactly once, in order,
	// with every clip's frames still together. Generated frames are new,
	// independent geometry wherever they land.
	QVector<int> seen(map.size(), 0);
	int offending = -1, previous = -1, previousClip = -3, previousPosition = -2;
	QHash<int, int> lastPositionOfClip;
	for (int r = 0; r < order.size() && offending < 0; ++r)
	{
		const int source = order[r];
		if (map[source] < -1 || generated.value(r, false))
			continue;
		if (++seen[source] > 1 || source < previous)
			offending = source;
		const int clip = map[source];
		if (clip == previousClip && r != previousPosition + 1)
			offending = source;
		if (clip != previousClip && lastPositionOfClip.contains(clip))
			offending = source;
		lastPositionOfClip.insert(clip, r);
		previous = source;
		previousClip = clip;
		previousPosition = r;
	}
	for (int f = 0; f < map.size() && offending < 0; ++f)
		if (map[f] >= -1 && seen[f] == 0)
			offending = f;
	if (offending < 0)
		return {};
	return frameRole(mesh, offending) + QLatin1Char(' ') +
		   Text::tr("Moving, copying, removing or reordering frames that follow the skeleton would detach them from it. Use Skeleton > Edit "
					"Skeletal Animation (model pose on the command line) to change skeletal clips, or unbind every surface first (model "
					"skinning --operation unbind) to edit the frames freely.");
}

QVector<int> modelFrameRangeSkeletalOrigins(const ModelMesh &mesh, const ModelEdit &edit)
{
	QVector<int> order;
	QVector<bool> generated;
	if (!modelFrameRangeOrder(mesh, edit, &order, nullptr, &generated))
		return {};
	QVector<int> origins(order.size(), -1);
	QVector<bool> used(mesh.frames.size(), false);
	for (int r = 0; r < order.size(); ++r)
	{
		if (generated.value(r, false))
			continue;
		origins[r] = used[order[r]] ? -1 : order[r];
		used[order[r]] = true;
	}
	const auto &map = mesh.skeleton.bakedClipForFrame;
	if (map.size() != mesh.frames.size())
		return origins;
	// A clip baked into frames is posed in frame order: one whose frames no
	// longer follow each other in order becomes independent frames.
	QHash<int, QVector<int>> positions;
	for (int r = 0; r < origins.size(); ++r)
		if (origins[r] >= 0 && map[origins[r]] >= 0)
			positions[map[origins[r]]].append(r);
	for (auto it = positions.cbegin(); it != positions.cend(); ++it)
	{
		const auto &list = it.value();
		bool inOrder = true;
		for (int i = 1; i < list.size() && inOrder; ++i)
			inOrder = list[i] == list[i - 1] + 1 && origins[list[i]] == origins[list[i - 1]] + 1;
		if (!inOrder)
			for (int r : list)
				origins[r] = -1;
	}
	return origins;
}

bool applyModelFrameRangeEdit(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	if (!candidate || !isModelFrameRangeEdit(edit.kind))
		return fail(error, Text::tr("Choose a frame range operation."));
	const auto &options = edit.frameRange;
	const ModelFrameRangeOptions defaults;
	const bool naming = edit.kind == ModelEditKind::RenameFramesFromClips;
	if ((edit.kind != ModelEditKind::MoveFrames && options.destination != defaults.destination) ||
		(edit.kind != ModelEditKind::ReverseFrameRange && options.pingPong) ||
		(!naming && (options.clip != defaults.clip || options.startNumber != defaults.startNumber || options.renameClips != defaults.renameClips)) ||
		(naming && (options.first != defaults.first || options.last != defaults.last)))
		return fail(error, Text::tr("These frame range settings do not apply to this operation."));
	if (naming)
		return renameFramesFromClips(candidate, options, error, work);
	if (edit.kind == ModelEditKind::ResampleAnimation || edit.kind == ModelEditKind::CloseAnimationLoop)
		return !(options == defaults) ? fail(error, Text::tr("These frame range settings do not apply to this operation."))
									  : applyModelFrameResampleEdit(candidate, edit, error, control);
	if (edit.kind == ModelEditKind::ImportFrames)
		return !(options == defaults) ? fail(error, Text::tr("These frame range settings do not apply to this operation."))
									  : applyModelFrameImportEdit(candidate, edit, error, control);
	QVector<int> order;
	if (!modelFrameRangeOrder(*candidate, edit, &order, error))
		return false;
	const int first = options.first, last = options.last, n = last - first + 1;
	ModelFrameRemap remap;
	remap.order = order;
	switch (edit.kind)
	{
	case ModelEditKind::MoveFrames:
		remap.animations = clipsMoved(candidate->animations, first, last, options.destination);
		break;
	case ModelEditKind::DuplicateFrameRange:
		remap.animations = clipsInsertedAfter(candidate->animations, last, n);
		break;
	case ModelEditKind::DeleteFrameRange:
		remap.animations = clipsWithout(candidate->animations, first, last);
		break;
	case ModelEditKind::ReverseFrameRange:
		remap.animations = options.pingPong ? clipsInsertedAfter(candidate->animations, last, n - 2) : candidate->animations;
		break;
	default:
		break;
	}
	return remapModelFrames(candidate, remap, error, control);
}

QString modelClipFrameStem(const QString &clipName, int highestNumber)
{
	QString stem;
	for (const QChar character : clipName.trimmed())
	{
		const ushort code = character.unicode();
		const bool kept = (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9') || code == '_' || code == '-';
		if (kept)
			stem.append(character);
		else if (!stem.isEmpty() && !stem.endsWith(QLatin1Char('_')))
			stem.append(QLatin1Char('_'));
	}
	chopSeparators(&stem);
	if (stem.isEmpty())
		stem = QStringLiteral("frame");
	const int digits = int(QString::number(std::max(0, highestNumber)).size());
	// Frame names hold 15 characters; a stem ending in a digit needs a '_'
	// so readers do not take its digit for part of the frame number.
	for (;;)
	{
		const int separator = stem.back().isDigit() ? 1 : 0;
		const int room = std::max(1, 15 - digits - separator);
		if (stem.size() <= room)
			break;
		stem.truncate(room);
		chopSeparators(&stem);
		if (stem.isEmpty())
			stem = QStringLiteral("f");
	}
	return stem;
}

QString modelFrameNameStem(const QString &name)
{
	// The same rule as inferAnimations in core/model_mesh.cpp: trailing
	// digits, then trailing separators and spaces, go.
	QString stem = name.trimmed();
	while (!stem.isEmpty() && stem.back().isDigit())
		stem.chop(1);
	while (!stem.isEmpty() && (stem.back() == QLatin1Char('_') || stem.back() == QLatin1Char('-') || stem.back().isSpace()))
		stem.chop(1);
	return stem.isEmpty() ? name.trimmed() : stem;
}

QString modelClipFrameName(const QString &stem, int number)
{
	const bool separate = !stem.isEmpty() && stem.back().isDigit();
	return stem + (separate ? QStringLiteral("_") : QString()) + QString::number(number);
}

QVector<ModelNativeClipCheck> modelNativeClipRoundTrip(const ModelMesh &mesh)
{
	const auto rebuilt = modelAnimationsFromFrameNames(mesh.frames);
	QVector<ModelNativeClipCheck> checks;
	for (int i = 0; i < mesh.animations.size(); ++i)
	{
		const auto &clip = mesh.animations[i];
		ModelNativeClipCheck check;
		check.clip = i;
		check.name = clip.name;
		check.first = clip.firstFrame;
		check.last = clip.firstFrame + clip.frameCount - 1;
		check.rateLost = clip.framesPerSecond > 0;
		check.survives = std::any_of(rebuilt.cbegin(), rebuilt.cend(), [&](const ModelAnimation &candidate)
			{ return candidate.name == clip.name && candidate.firstFrame == clip.firstFrame && candidate.frameCount == clip.frameCount; });
		for (const auto &candidate : rebuilt)
			if (candidate.firstFrame <= clip.firstFrame && clip.firstFrame < candidate.firstFrame + candidate.frameCount)
			{
				check.rebuiltName = candidate.name;
				check.rebuiltFirst = candidate.firstFrame;
				check.rebuiltLast = candidate.firstFrame + candidate.frameCount - 1;
				break;
			}
		checks.append(check);
	}
	return checks;
}

QString modelNativeClipSummary(const QVector<ModelNativeClipCheck> &checks)
{
	if (checks.isEmpty())
		return {};
	QStringList lost;
	bool rates = false;
	for (const auto &check : checks)
	{
		if (!check.survives)
			lost.append(check.name);
		rates = rates || check.rateLost;
	}
	const int total = int(checks.size());
	QString text;
	if (lost.isEmpty())
		text = Text::tr("All %n clips come back from MDL, MD2 and MD3 frame names.", nullptr, total);
	else
	{
		const int kept = total - int(lost.size());
		text = Text::tr("%1 of %n clips come back from MDL, MD2 and MD3 frame names. Not: %2. Name Frames From Clips fixes the names.",
						nullptr, total)
				   .arg(kept)
				   .arg(lost.join(QStringLiteral(", ")));
	}
	if (rates)
		text += QLatin1Char(' ') + Text::tr("Saved clip FPS stays in the editable source; set timing in the game's own animation files.");
	return text;
}
} // namespace vibestudio
