#pragma once

// Copying skeletal poses and animation keys out of one clip and pasting them
// into another clip, joint or document, optionally mirrored left to right.
//
// A copy is a small versioned JSON payload (MIME type
// application/x-vibestudio-pose+json, also written as a file by the CLI): the
// whole source rig (joint names, parents and binds, which identify it), the
// copied joints, and either their parent-local poses for a sample range or
// their animation curve keys over that range. Pasting is the document edit
// ModelEditKind::PastePose, so it is one undo step, works in the CLI and is
// validated like every other skeletal edit.

#include "core/model_animation_curves.h"
#include "core/model_work.h"

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio
{
struct ModelEdit;

inline constexpr char modelPoseClipboardMimeType[] = "application/x-vibestudio-pose+json";
inline constexpr int modelPoseClipboardVersion = 1;
inline constexpr qint64 modelPoseClipboardMaxBytes = 16LL * 1024 * 1024;
// Copied joints times samples.
inline constexpr qint64 modelPoseClipboardMaxMatrices = 1048576;

enum class ModelPoseClipboardKind
{
	Pose,
	Keys
};

struct ModelPoseClipboardJoint
{
	QString name;
	// Empty for a root.
	QString parent;
	// The joint's model-space bind pose in the source rig.
	ModelJointMatrix bind;
};

struct ModelPoseClipboard
{
	ModelPoseClipboardKind kind = ModelPoseClipboardKind::Pose;
	// The whole source rig, in joint order.
	QVector<ModelPoseClipboardJoint> rig;
	// The source clip's name (empty for the bind pose), first sample and rate,
	// for display only.
	QString clip;
	int first = 0;
	double framesPerSecond = 0;
	// Samples copied, from one.
	int samples = 1;
	// The copied joints, in rig order.
	QStringList joints;
	// Pose: [sample][copied joint] parent-local matrices, and the same poses in
	// model space, which a paste onto the same rig uses where a joint's whole
	// parent chain is pasted too, so such a round trip is exact.
	QVector<QVector<ModelJointMatrix>> local;
	QVector<QVector<ModelJointMatrix>> world;
	// Keys: one group per copied joint, timed from sample zero, and the
	// channels (0-8, see modelCurveChannelId) the paste applies.
	QVector<ModelPoseCurves> curves;
	QVector<int> channels;
};

struct ModelPoseCopyOptions
{
	// -1 copies the bind pose (poses only).
	int clip = -1;
	int first = 0, last = 0;
	// Exact names; empty copies every joint.
	QStringList joints;
	bool keys = false;
	// Keys only; empty copies all nine channels.
	QVector<int> channels;
};

// A stable identity for a joint hierarchy: names and parents, not poses.
[[nodiscard]] QString modelRigFingerprint(const ModelSkeleton &skeleton);
[[nodiscard]] QString modelPoseClipboardFingerprint(const ModelPoseClipboard &clipboard);
bool copyModelPose(const ModelMesh &mesh, const ModelPoseCopyOptions &options, ModelPoseClipboard *result, QString *error = nullptr,
				   const ModelWorkControl &control = {});
[[nodiscard]] QByteArray modelPoseClipboardBytes(const ModelPoseClipboard &clipboard);
bool parseModelPoseClipboard(const QByteArray &bytes, ModelPoseClipboard *result, QString *error = nullptr);
// "pose of 3 joints, 1 sample from clip walk" style summary for status lines.
[[nodiscard]] QString modelPoseClipboardSummary(const ModelPoseClipboard &clipboard);

struct ModelPosePasteOptions
{
	// modelPoseClipboardBytes output.
	QByteArray clipboard;
	// The destination clip and its sample that receives the first copied sample.
	int clip = -1, first = 0;
	// Source joint name to destination joint name. Unlisted joints keep their
	// names. A clipboard from a different rig needs a map.
	QMap<QString, QString> joints;
	// Pose clipboards only: -1 pastes as copied; 0, 1 or 2 mirrors through the
	// plane at `plane` across X, Y or Z, onto the partners `mirrorJoints`
	// names in the destination rig (both directions, see
	// completeModelMirrorJointMap). Unpaired joints must sit on the plane.
	int mirrorAxis = -1;
	double plane = 0;
	QMap<QString, QString> mirrorJoints;
	// Keys already at the pasted samples (or inside a pasted key range) are
	// replaced when set; otherwise the paste is refused.
	bool replaceKeys = false;
	bool operator==(const ModelPosePasteOptions &) const = default;
};

// The axis (0-2) the rig's detected left/right joint pairs mirror across, with
// those pairs; -1 and no pairs when none are found.
[[nodiscard]] int detectModelPoseMirrorAxis(const ModelMesh &mesh, QMap<QString, QString> *pairs = nullptr);
// A JSON object mapping exact source joint names to destination names.
bool parseModelJointMapJson(const QByteArray &bytes, QMap<QString, QString> *map, QString *error = nullptr);
// Key paste into one channel, shared by PastePose and the Animation Curves
// dialog: `copied`'s samples 0..last, moved to start at `first`, replace the
// keys of `destination` inside [first, first + last]. Both ends keep the
// destination's outward tangent sides, as range transfer does. `frames` is the
// destination clip's sample count; the pasted range must fit inside it.
bool spliceModelCurveKeys(const QVector<ModelPoseCurveKey> &destination, const QVector<ModelPoseCurveKey> &copied, int last, int first,
						  int frames, QVector<ModelPoseCurveKey> *result, QString *error = nullptr, const ModelWorkControl &control = {});
// Candidate-only: use applyModelEdit/ModelDocument for transactional publication.
bool applyModelPosePaste(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control = {});
} // namespace vibestudio
