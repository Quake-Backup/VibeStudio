#include "core/model_clip_import.h"
#include "core/model_document.h"
#include "core/model_pose_clipboard.h"
#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

// Retargeting transfers each mapped joint's parent-local rotation relative to
// its bind, R_t = Rbind_t * Rbind_s^-1 * R_s, the common bind-compensated
// rotation transfer (as described for example in the Blender and Maya
// retargeting documentation); destination bone lengths come from its bind and
// only the root's motion is carried, scaled. Original VibeStudio code.

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelClipImport)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
ModelJointMatrix bindLocal(const QVector<ModelPoseClipboardJoint> &rig, const QHash<QString, int> &index, int joint)
{
	const auto &parent = rig[joint].parent;
	return parent.isEmpty() ? rig[joint].bind : modelJointMultiply(modelJointInverse(rig[index.value(parent)].bind), rig[joint].bind);
}
ModelJointMatrix bindLocal(const ModelSkeleton &skeleton, int joint)
{
	const int parent = skeleton.joints[joint].parent;
	return parent < 0 ? skeleton.joints[joint].bind : modelJointMultiply(modelJointInverse(skeleton.joints[parent].bind), skeleton.joints[joint].bind);
}
double height(const ModelJointMatrix &bind)
{
	const auto t = modelJointTranslation(bind);
	return std::sqrt(double(t.x) * t.x + double(t.y) * t.y + double(t.z) * t.z);
}
struct Prepared
{
	ModelPoseClipboard clip;
	QHash<QString, int> sourceIndex;
	// Per destination joint: its source joint in the copy's rig, or -1.
	QVector<int> source;
	// Per destination joint: its column in the copy's samples, or -1.
	QVector<int> column;
	QVector<bool> translation;
	int root = -1;
	double rootScale = 1;
};
bool prepare(const ModelMesh &mesh, const ModelClipImportOptions &o, Prepared *result, ModelClipImportPlan *plan, QString *error)
{
	const auto &skeleton = mesh.skeleton;
	if (skeleton.isEmpty())
		return fail(error, Text::tr("Importing a clip needs a model with joints. Add a skeleton first."));
	Prepared p;
	if (!parseModelPoseClipboard(o.clip, &p.clip, error))
		return false;
	if (p.clip.kind != ModelPoseClipboardKind::Pose || p.clip.joints.size() != p.clip.rig.size())
		return fail(error, Text::tr("The imported clip must hold every joint of its rig."));
	if (!std::isfinite(o.framesPerSecond) || o.framesPerSecond < 0 || o.framesPerSecond > 1000 || !std::isfinite(o.rootScale) ||
		o.rootScale < 0 || o.rootScale > 1000 || (!o.retarget && (o.rootScale != 0 || !o.copyTranslation.isEmpty())))
		return fail(error, Text::tr("Use a rate from 0 to 1000 FPS; root scale and copied translations apply only to retargeting."));
	static const QRegularExpression pattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_.-]{0,62}$"));
	if (!pattern.match(o.name).hasMatch())
		return fail(error, Text::tr("Use a clip name of 1–63 ASCII letters, digits, underscores, dots or hyphens, starting with a letter or underscore."));
	for (const auto &clip : skeleton.clips)
		if (clip.name.compare(o.name, Qt::CaseInsensitive) == 0)
			return fail(error, Text::tr("That skeletal clip name is already used."));
	for (int i = 0; i < p.clip.rig.size(); ++i)
		p.sourceIndex.insert(p.clip.rig[i].name, i);
	for (auto it = o.joints.constBegin(); it != o.joints.constEnd(); ++it)
		if (!p.sourceIndex.contains(it.key()) || modelJointIndex(skeleton, it.value(), Qt::CaseSensitive) < 0)
			return fail(error, Text::tr("Joint map entry %1 = %2 must name a joint of the imported clip and a joint of this rig exactly.")
								   .arg(it.key(), it.value()));
	p.source.fill(-1, skeleton.joints.size());
	p.column.fill(-1, skeleton.joints.size());
	p.translation.fill(false, skeleton.joints.size());
	// Mapped joints first; an unlisted joint keeps its name unless a mapped
	// joint already took that destination, and then goes unused.
	QVector<int> assigned(p.clip.rig.size(), -1);
	for (int pass = 0; pass < 2; ++pass)
		for (int s = 0; s < p.clip.rig.size(); ++s)
		{
			const auto &name = p.clip.rig[s].name;
			if (o.joints.contains(name) != (pass == 0))
				continue;
			const int d = modelJointIndex(skeleton, o.joints.value(name, name), Qt::CaseSensitive);
			if (d < 0 || (pass == 1 && p.source[d] >= 0))
				continue;
			if (p.source[d] >= 0)
				return fail(error, Text::tr("Two imported joints map onto joint %1. Map each one to its own joint.").arg(skeleton.joints[d].name));
			p.source[d] = s;
			p.column[d] = int(p.clip.joints.indexOf(name));
			assigned[s] = d;
		}
	for (const auto &name : o.copyTranslation)
	{
		const int d = modelJointIndex(skeleton, name, Qt::CaseSensitive);
		if (d < 0 || p.source[d] < 0)
			return fail(error, Text::tr("Copied translations must name mapped joints of this rig exactly; %1 is not one.").arg(name));
		p.translation[d] = true;
	}
	for (int d = 0; d < skeleton.joints.size(); ++d)
	{
		if (p.source[d] < 0)
		{
			if (!o.fillFromBind)
				return fail(error, Text::tr("Joint %1 has no joint in the imported clip. Map one to it, or keep unmatched joints in their bind "
											"pose.")
									   .arg(skeleton.joints[d].name));
			continue;
		}
		if (!o.retarget)
		{
			// Exact imports recompose parent-local poses: the parents must match.
			const auto &sourceParent = p.clip.rig[p.source[d]].parent;
			const int parent = skeleton.joints[d].parent;
			const int sourceParentIndex = sourceParent.isEmpty() ? -1 : p.sourceIndex.value(sourceParent);
			const auto expected = sourceParentIndex < 0			  ? QString()
								  : assigned[sourceParentIndex] < 0 ? sourceParent
																  : skeleton.joints[assigned[sourceParentIndex]].name;
			const auto actual = parent < 0 ? QString() : skeleton.joints[parent].name;
			if (expected != actual)
				return fail(error, Text::tr("Joint %1 has parent %2 here but %3 in the imported clip. Retarget to import between different "
											"hierarchies.")
									   .arg(skeleton.joints[d].name, actual.isEmpty() ? Text::tr("none") : actual,
											expected.isEmpty() ? Text::tr("none") : expected));
		}
		else if (p.root < 0)
		{
			// The first mapped joint with no mapped ancestor carries the motion.
			bool mappedAncestor = false;
			for (int a = skeleton.joints[d].parent; a >= 0 && !mappedAncestor; a = skeleton.joints[a].parent)
				mappedAncestor = p.source[a] >= 0;
			if (!mappedAncestor)
				p.root = d;
		}
	}
	if (o.retarget && p.root >= 0)
	{
		const double sourceHeight = height(p.clip.rig[p.source[p.root]].bind), destinationHeight = height(skeleton.joints[p.root].bind);
		p.rootScale = o.rootScale > 0 ? o.rootScale : sourceHeight > 1e-6 && destinationHeight > 1e-6 ? destinationHeight / sourceHeight : 1;
	}
	if (plan)
	{
		ModelClipImportPlan result;
		result.samples = p.clip.samples;
		result.rootScale = p.rootScale;
		for (int d = 0; d < skeleton.joints.size(); ++d)
			result.joints.append({skeleton.joints[d].name, p.source[d] < 0 ? QString() : p.clip.rig[p.source[d]].name,
								  !o.retarget || p.translation[d] || d == p.root});
		for (int s = 0; s < p.clip.rig.size(); ++s)
			if (assigned[s] < 0)
				result.unusedSourceJoints.append(p.clip.rig[s].name);
		*plan = std::move(result);
	}
	if (result)
		*result = std::move(p);
	return true;
}
// The new clip's model-space samples.
bool samples(const ModelSkeleton &skeleton, const ModelClipImportOptions &o, const Prepared &p, QVector<QVector<ModelJointMatrix>> *frames,
			 QString *error, ModelWorkProgress &work)
{
	const auto parents = modelJointParents(skeleton);
	QVector<ModelJointMatrix> destinationBind;
	for (int d = 0; d < skeleton.joints.size(); ++d)
		destinationBind.append(bindLocal(skeleton, d));
	for (int i = 0; i < p.clip.samples; ++i)
	{
		QVector<ModelJointMatrix> local(skeleton.joints.size());
		for (int d = 0; d < skeleton.joints.size(); ++d)
		{
			if (!work.step())
				return false;
			if (p.source[d] < 0)
			{
				local[d] = destinationBind[d];
				continue;
			}
			const auto &copied = p.clip.local[i][p.column[d]];
			if (!o.retarget)
			{
				local[d] = copied;
				continue;
			}
			const auto sourceBind = bindLocal(p.clip.rig, p.sourceIndex, p.source[d]);
			const auto rotation = modelQuatNormalized(modelQuatMultiply(
				modelJointRotation(destinationBind[d]), modelQuatMultiply(modelQuatConjugate(modelJointRotation(sourceBind)), modelJointRotation(copied))));
			auto translation = modelJointTranslation(destinationBind[d]);
			if (p.translation[d])
				translation = modelJointTranslation(copied);
			else if (d == p.root)
			{
				const auto motion = modelJointTranslation(copied), rest = modelJointTranslation(sourceBind);
				translation = {float(translation.x + (double(motion.x) - rest.x) * p.rootScale),
							   float(translation.y + (double(motion.y) - rest.y) * p.rootScale),
							   float(translation.z + (double(motion.z) - rest.z) * p.rootScale)};
			}
			local[d] = modelJointMatrix(rotation, translation, modelJointScale(destinationBind[d]));
		}
		auto world = modelJointsToModelSpace(parents, local);
		if (world.size() != skeleton.joints.size())
			return fail(error, Text::tr("The rig's hierarchy is not ordered parents first."));
		frames->append(std::move(world));
	}
	return true;
}
} // namespace

bool modelClipImportFromSource(const ModelMesh &source, int clip, const QString &sourcePath, ModelClipImportOptions *options, QString *error)
{
	if (!options)
		return false;
	if (source.skeleton.isEmpty() || source.skeleton.clips.isEmpty())
		return fail(error, Text::tr("The source holds no skeletal animation to import."));
	if (clip < 0 || clip >= source.skeleton.clips.size() || source.skeleton.clips[clip].frames.isEmpty())
		return fail(error, Text::tr("Choose an existing clip with poses in the source."));
	ModelPoseCopyOptions copy;
	copy.clip = clip;
	copy.first = 0;
	copy.last = int(source.skeleton.clips[clip].frames.size()) - 1;
	ModelPoseClipboard clipboard;
	if (!copyModelPose(source, copy, &clipboard, error))
		return false;
	const auto &from = source.skeleton.clips[clip];
	options->clip = modelPoseClipboardBytes(clipboard);
	if (options->name.isEmpty())
	{
		static const QRegularExpression invalid(QStringLiteral("[^A-Za-z0-9_.-]"));
		auto name = from.name;
		name.replace(invalid, QStringLiteral("_"));
		if (name.isEmpty() || !(name[0].isLetter() || name[0] == QLatin1Char('_')))
			name.prepend(QLatin1Char('_'));
		options->name = name.left(63);
	}
	if (options->framesPerSecond == 0)
		options->framesPerSecond = from.framesPerSecond > 0 ? from.framesPerSecond : 24;
	options->loops = from.loops;
	if (options->sourcePath.isEmpty())
		options->sourcePath = !from.sourcePath.isEmpty() ? from.sourcePath : sourcePath;
	return true;
}

bool planModelClipImport(const ModelMesh &mesh, const ModelClipImportOptions &options, ModelClipImportPlan *plan, QString *error)
{
	return prepare(mesh, options, nullptr, plan, error);
}

bool applyModelClipImport(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!mesh || !work.check())
		return false;
	const auto &o = edit.animation.import;
	if (edit.kind != ModelEditKind::ImportSkeletalClip || !defaultModelSkeletalOptions(edit.skeletal))
		return fail(error, Text::tr("Import Animation accepts only its own clip, mapping and timing settings."));
	if (!validateModelSkeleton(*mesh, error, control))
		return false;
	Prepared p;
	if (!prepare(*mesh, o, &p, nullptr, error))
		return false;
	QVector<QVector<ModelJointMatrix>> frames;
	if (!samples(mesh->skeleton, o, p, &frames, error, work))
		return false;
	const double rate = o.framesPerSecond > 0 ? o.framesPerSecond : p.clip.framesPerSecond > 0 ? p.clip.framesPerSecond : 24;
	// A clip that fits the editable frame budget joins the working set, as Add
	// skeletal clip does; a longer one stays in the skeleton for the skeletal
	// tools and exports, outside the working set.
	qint64 vertices = 0;
	for (const auto &surface : mesh->surfaces)
		vertices += surface.vertexCount;
	const qint64 total = qint64(mesh->frames.size()) + frames.size();
	if (total <= modelDocumentMaxFrames && vertices * total <= modelDocumentMaxFrameVertices)
	{
		ModelEdit add;
		add.kind = ModelEditKind::AddSkeletalClip;
		add.skeletal.name = o.name;
		add.skeletal.frameCount = int(frames.size());
		add.skeletal.rate = rate;
		add.skeletal.loops = o.loops;
		if (!applyModelSkeletalEdit(mesh, add, error, control))
			return false;
		const int clip = int(mesh->skeleton.clips.size()) - 1;
		if (!replaceModelSkeletalSamples(mesh, clip, 0, frames, error, control))
			return false;
		mesh->skeleton.clips[clip].sourcePath = o.sourcePath;
		return work.check();
	}
	if (mesh->skeleton.clips.size() >= modelDocumentMaxAnimations)
		return fail(error, Text::tr("A skeleton supports at most 1024 authored clips."));
	qint64 matrices = qint64(frames.size()) * mesh->skeleton.joints.size();
	for (const auto &existing : mesh->skeleton.clips)
		matrices += qint64(existing.frames.size()) * mesh->skeleton.joints.size();
	if (matrices > 16LL * 1024 * 1024)
		return fail(error, Text::tr("The authored animation exceeds the source format's joint-pose storage limit."));
	ModelSkeletalClip clip;
	clip.name = o.name;
	clip.sourcePath = o.sourcePath;
	clip.framesPerSecond = rate;
	clip.loops = o.loops;
	clip.frames = std::move(frames);
	mesh->skeleton.clips.append(std::move(clip));
	return validateModelSkeleton(*mesh, error, control) && work.check();
}
} // namespace vibestudio
