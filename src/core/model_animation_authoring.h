#pragma once

// Animation authoring edits added in the 2026-10 modeller round. Each is a
// ModelEditKind handled by applyModelEdit through the skeletal edit path, so
// undo, recovery, saves and the CLI treat them like every other skeletal edit.
// Their settings travel in ModelEdit::animation:
// - PastePose: pose and key paste (core/model_pose_clipboard.h);
// - SolveChainIk: long-chain IK, settings in ModelEdit::ik (core/model_ik.h);
// - SetJointLimits: ModelJoint::limits for chain IK;
// - ImportSkeletalClip: a clip from another file or rig (core/model_clip_import.h);
// - EditAnimationEvents: game-side frame commands (core/model_animation_events.h);
// - EditClipStack: clip constraints and layers (core/model_animation_constraints.h).

#include "core/model_pose_clipboard.h"

namespace vibestudio
{
struct ModelEdit;
enum class ModelEditKind;

struct ModelJointLimitsEdit
{
	QString joint;
	// enabled false clears the joint's limits.
	ModelJointLimits limits;
	bool operator==(const ModelJointLimitsEdit &) const = default;
};

struct ModelClipImportOptions
{
	// The whole source clip as a pose copy (modelPoseClipboardBytes): every
	// joint of the source rig and every sample.
	QByteArray clip;
	QString name;
	// Zero keeps the copy's rate.
	double framesPerSecond = 0;
	bool loops = true;
	// The file the clip came from, kept as ModelSkeletalClip::sourcePath.
	QString sourcePath;
	// Source joint name to destination joint name; unlisted joints keep their
	// names.
	QMap<QString, QString> joints;
	// Exact import needs matching parents; retargeting transfers each mapped
	// joint's rotation relative to its bind and keeps destination bone lengths.
	bool retarget = false;
	// Retargeting: the mapped root's motion scale; zero derives it from the
	// rigs' heights.
	double rootScale = 0;
	// Destination joints without a source keep their bind pose instead of
	// refusing the import.
	bool fillFromBind = false;
	// Retargeting: destination joints that copy their source's translation too.
	QStringList copyTranslation;
	bool operator==(const ModelClipImportOptions &) const = default;
};

enum class ModelAnimationEventAction
{
	Add,
	Remove,
	Move,
	Replace
};
struct ModelAnimationEventOptions
{
	ModelAnimationEventAction action = ModelAnimationEventAction::Add;
	// One of: a skeletal clip, or a vertex animation of a model without joints.
	int clip = -1, animation = -1;
	// Remove and Move: the event's index in the clip's list.
	int index = -1;
	// Add: the new event.
	ModelAnimationEvent event;
	// Move: the new frame.
	int frame = 0;
	// Replace: the clip's complete event list.
	QVector<ModelAnimationEvent> events;
	bool operator==(const ModelAnimationEventOptions &) const = default;
};

enum class ModelClipStackAction
{
	AddConstraint,
	UpdateConstraint,
	RemoveConstraint,
	AddLayer,
	UpdateLayer,
	RemoveLayer,
	// Keep the evaluated samples and drop the clip's layers and constraints.
	Bake
};
struct ModelClipStackOptions
{
	ModelClipStackAction action = ModelClipStackAction::AddConstraint;
	int clip = -1;
	// Update and Remove: the constraint's or layer's index.
	int index = -1;
	ModelClipConstraint constraint;
	ModelClipLayer layer;
	bool operator==(const ModelClipStackOptions &) const = default;
};

struct ModelAnimationAuthoringOptions
{
	ModelPosePasteOptions paste{};
	ModelJointLimitsEdit limits{};
	ModelClipImportOptions import{};
	ModelAnimationEventOptions events{};
	ModelClipStackOptions stack{};
	bool operator==(const ModelAnimationAuthoringOptions &) const = default;
};

[[nodiscard]] bool isModelAnimationAuthoringEdit(ModelEditKind kind);
// Refuses settings given to an edit they do not apply to.
bool validModelAnimationAuthoringOptions(const ModelEdit &edit, QString *error);
// Candidate-only dispatch for isModelAnimationAuthoringEdit kinds.
bool applyModelAnimationAuthoringEdit(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control = {});
// The oldest editable source version that holds this lane's additions the
// mesh uses (joint limits, events, clip stacks): 15, or 0 when it uses none.
[[nodiscard]] int modelAnimationAuthoringSourceVersion(const ModelMesh &mesh);
// Joint limits, animation events and clip stacks are valid (part of
// validateEditableModel).
bool validateModelAnimationAuthoring(const ModelMesh &mesh, QString *error = nullptr, const ModelWorkControl &control = {});
} // namespace vibestudio
