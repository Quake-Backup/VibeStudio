#pragma once

// The clip evaluation stack: a skeletal clip's layers and persistent
// constraints (ModelClipLayer, ModelClipConstraint in core/model_mesh.h),
// evaluated in order over the clip's base samples into its dense frames:
//
//     frames = constraints(layers(base))
//
// A clip with a stack keeps `base`; `frames` must equal the evaluation, which
// validation checks the way stored curves are checked against samples, so
// exporters (which read `frames`) always see the evaluated motion. Pose and
// curve edits of such a clip edit `base` and evaluate again; edits that cannot
// (bind changes, resizing) are refused until the stack is baked.

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QJsonObject>

namespace vibestudio
{
struct ModelEdit;

inline constexpr int modelClipMaxConstraints = 64;
inline constexpr int modelClipMaxLayers = 16;

[[nodiscard]] QString modelClipConstraintKindId(ModelClipConstraintKind kind);
// -1 for an unknown id.
[[nodiscard]] int modelClipConstraintKindFromId(const QString &id);
[[nodiscard]] bool modelClipHasStack(const ModelSkeletalClip &clip);
// The joints a constraint or layer moves, as a per-joint mask: the constrained
// joint (and the IK chain) with their descendants.
[[nodiscard]] QVector<bool> modelClipConstraintAffectedJoints(const ModelSkeleton &skeleton, const ModelClipConstraint &constraint);
// Checks names, ranges, weights and that no constraint follows a joint that
// it or an earlier stack entry moves (a dependency cycle).
bool validModelClipStack(const ModelSkeleton &skeleton, const ModelSkeletalClip &clip, QString *error = nullptr);
// Evaluates `base` (one model-space pose per sample) through the stack.
bool evaluateModelClipStack(const ModelSkeleton &skeleton, const ModelSkeletalClip &clip, const QVector<QVector<ModelJointMatrix>> &base,
							QVector<QVector<ModelJointMatrix>> *frames, QString *error = nullptr, const ModelWorkControl &control = {});
// Every clip with a stack has a base parallel to its frames and frames equal
// to the stack's evaluation.
bool validateModelClipStacks(const ModelMesh &mesh, QString *error = nullptr, const ModelWorkControl &control = {});
// Candidate-only EditClipStack.
bool applyModelClipStack(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control = {});
// Called by replaceModelSkeletalSamples: `samples` (every sample, computed by
// the edit from modelClipEditableSamples) become the clip's base, and its
// frames their evaluation.
bool rebaseModelClipStack(const ModelSkeleton &skeleton, ModelSkeletalClip *clip, const QVector<QVector<ModelJointMatrix>> &samples,
						  QString *error, const ModelWorkControl &control = {});
// The samples edits of a clip work on: its base when it has a stack, else its
// frames.
[[nodiscard]] const QVector<QVector<ModelJointMatrix>> &modelClipEditableSamples(const ModelSkeletalClip &clip);
// The IK solve an Ik or FootLock constraint runs on one pose (defined in
// core/model_ik.cpp): FABRIK from rootJoint (or the end's grandparent) to
// endJoint, steered by `pole` when usePole, clamping unreachable targets and
// keeping the end's orientation.
bool solveModelConstraintIk(const ModelSkeleton &skeleton, QVector<ModelJointMatrix> *pose, const QString &rootJoint, const QString &endJoint,
							const ModelVec3 &target, const ModelVec3 &pole, bool usePole, QString *error);
// Source JSON (mesh version 15), inside a clip's entry.
void writeModelClipStackJson(const ModelSkeletalClip &clip, bool packed, QJsonObject *entry);
bool readModelClipStackJson(const QJsonObject &entry, int jointCount, bool packedArrays, ModelSkeletalClip *clip);
} // namespace vibestudio
