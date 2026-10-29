#include "core/model_animation_authoring.h"
#include "core/model_animation_constraints.h"
#include "core/model_animation_events.h"
#include "core/model_clip_import.h"
#include "core/model_document.h"
#include "core/model_ik.h"
#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <algorithm>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelAnimationAuthoring)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
bool setJointLimits(ModelMesh *mesh, const ModelEdit &edit, QString *error)
{
	const auto &o = edit.animation.limits;
	if (!defaultModelSkeletalOptions(edit.skeletal))
		return fail(error, Text::tr("Joint limits accept only a joint and its limits."));
	const int joint = modelJointIndex(mesh->skeleton, o.joint, Qt::CaseSensitive);
	if (joint < 0)
		return fail(error, Text::tr("Choose an existing joint by its exact name."));
	if (o.limits.enabled && !validModelJointLimits(o.limits, error))
		return false;
	mesh->skeleton.joints[joint].limits = o.limits.enabled ? o.limits : ModelJointLimits{};
	return true;
}
} // namespace

bool isModelAnimationAuthoringEdit(ModelEditKind kind)
{
	return kind == ModelEditKind::PastePose || kind == ModelEditKind::SolveChainIk || kind == ModelEditKind::SetJointLimits ||
		   kind == ModelEditKind::ImportSkeletalClip || kind == ModelEditKind::EditAnimationEvents || kind == ModelEditKind::EditClipStack;
}

bool validModelAnimationAuthoringOptions(const ModelEdit &edit, QString *error)
{
	const auto &o = edit.animation;
	if (edit.kind != ModelEditKind::PastePose && !(o.paste == ModelPosePasteOptions{}))
		return fail(error, Text::tr("Pose paste settings apply only to Paste Pose."));
	if (edit.kind != ModelEditKind::SetJointLimits && !(o.limits == ModelJointLimitsEdit{}))
		return fail(error, Text::tr("Joint limit settings apply only to setting joint limits."));
	if (edit.kind != ModelEditKind::ImportSkeletalClip && !(o.import == ModelClipImportOptions{}))
		return fail(error, Text::tr("Clip import settings apply only to importing a clip."));
	if (edit.kind != ModelEditKind::EditAnimationEvents && !(o.events == ModelAnimationEventOptions{}))
		return fail(error, Text::tr("Event settings apply only to animation event edits."));
	if (edit.kind != ModelEditKind::EditClipStack && !(o.stack == ModelClipStackOptions{}))
		return fail(error, Text::tr("Constraint and layer settings apply only to clip stack edits."));
	return true;
}

bool applyModelAnimationAuthoringEdit(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	if (!candidate)
		return false;
	switch (edit.kind)
	{
	case ModelEditKind::PastePose:
		return applyModelPosePaste(candidate, edit, error, control);
	case ModelEditKind::SolveChainIk:
		return applyModelIk(candidate, edit, error, control);
	case ModelEditKind::SetJointLimits:
		return setJointLimits(candidate, edit, error);
	case ModelEditKind::ImportSkeletalClip:
		return applyModelClipImport(candidate, edit, error, control);
	case ModelEditKind::EditAnimationEvents:
		return applyModelAnimationEvents(candidate, edit, error, control);
	case ModelEditKind::EditClipStack:
		return applyModelClipStack(candidate, edit, error, control);
	default:
		return fail(error, Text::tr("This is not an animation authoring operation."));
	}
}

int modelAnimationAuthoringSourceVersion(const ModelMesh &mesh)
{
	const auto limited = std::any_of(mesh.skeleton.joints.cbegin(), mesh.skeleton.joints.cend(), [](const auto &joint) { return joint.limits.enabled; });
	const auto clipData = std::any_of(mesh.skeleton.clips.cbegin(), mesh.skeleton.clips.cend(),
									  [](const auto &clip) { return !clip.events.isEmpty() || modelClipHasStack(clip); });
	const auto vertexEvents = std::any_of(mesh.animations.cbegin(), mesh.animations.cend(), [](const auto &clip) { return !clip.events.isEmpty(); });
	return limited || clipData || vertexEvents ? 15 : 0;
}

bool validateModelAnimationAuthoring(const ModelMesh &mesh, QString *error, const ModelWorkControl &control)
{
	for (const auto &joint : mesh.skeleton.joints)
		if (joint.limits.enabled && !validModelJointLimits(joint.limits, error))
			return false;
		else if (!joint.limits.enabled && !(joint.limits == ModelJointLimits{}))
			return fail(error, Text::tr("Joint %1 keeps limit values while its limits are off.").arg(joint.name));
	return validateModelAnimationEvents(mesh, error) && validateModelClipStacks(mesh, error, control);
}
} // namespace vibestudio
