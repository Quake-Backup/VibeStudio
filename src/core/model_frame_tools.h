#pragma once

// Frame-range editing for vertex animation (MDL, MD2, MD3 and editable
// sources): move, duplicate, delete, reverse and ping-pong a range of frames,
// and name frames after their clips so the clips survive native files.
//
// Every operation is one frame permutation (remapModelFrames): each resulting
// frame copies one frame of the model before the edit. The permutation keeps
// everything indexed by frame in step: surface poses, frame names and origins,
// attachment tags, animated collision poses, Quake MDL native frame groups and
// their timing, and animation clip ranges. On skeletal models the frames
// baked from the skeleton keep their clip mapping through
// ModelSkinReconcileOptions::frameOrigins (core/model_vertex_origins.h).
//
// MDL, MD2 and MD3 have no clip table: readers rebuild clips from frame names
// (stand1, stand2 ...; modelAnimationsFromFrameNames). modelNativeClipRoundTrip
// says which clips a native file would not bring back, and
// RenameFramesFromClips names every frame of a clip `<stem><number>` within
// the 15 printable ASCII characters those formats hold.

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio
{
struct ModelEdit;
enum class ModelEditKind;

// ModelEdit::frameRange. Defaults are required by every other edit.
struct ModelFrameRangeOptions
{
	// The inclusive frame range the operation works on.
	int first = -1;
	int last = -1;
	// MoveFrames: the index the first moved frame has after the move.
	int destination = -1;
	// ReverseFrameRange: false reverses the range in place; true keeps it and
	// inserts the frames between its ends in reverse after it (a ping-pong
	// loop: 1 2 3 4 3 2), so the clip loops back to its first pose.
	bool pingPong = false;
	// RenameFramesFromClips: the clip to name (-1 names every clip), the
	// number its first frame gets (stand1 starts at 1), and whether clips
	// whose names are not a usable stem are renamed to the stem their frames
	// use, so the clip name comes back from native files too.
	int clip = -1;
	int startNumber = 1;
	bool renameClips = true;
	bool operator==(const ModelFrameRangeOptions &) const = default;
};

// How resampling fills a sample that falls between two stored frames.
enum class ModelResampleMethod
{
	// Blend positions and origins linearly, normals by normalised blending,
	// attachment tags and collision orientations along the shortest arc, and
	// skeletal joints in parent space (rotation slerp, translation and scale
	// lerp).
	Linear,
	// Copy the nearer stored frame exactly.
	Nearest,
};
// How CloseAnimationLoop and CloseSkeletalLoop make a clip loop seamlessly.
enum class ModelLoopClose
{
	// The last frame becomes a copy of the first pose.
	ReplaceLast,
	// A copy of the first pose is added after the last frame.
	AppendFirst,
	// The last `blendFrames` frames blend toward the first pose with weights
	// k / (blendFrames + 1), so the motion eases into the loop.
	CrossFade,
};

// ModelEdit::resample, for ResampleAnimation, CloseAnimationLoop,
// ResampleSkeletalClip and CloseSkeletalLoop. Defaults are required by every
// other edit.
struct ModelFrameResampleOptions
{
	// A vertex clip (ModelMesh::animations) or, for the skeletal kinds, a
	// skeletal clip (ModelSkeleton::clips).
	int clip = -1;
	// The clip's current rate; zero uses its saved FPS.
	double sourceFps = 0;
	// The new rate, or zero to give targetFrames instead.
	double targetFps = 0;
	int targetFrames = 0;
	ModelResampleMethod method = ModelResampleMethod::Linear;
	// The clip loops: the step from its last frame back to its first is
	// sampled too, and the samples divide the whole loop evenly.
	bool cyclic = false;
	// Skeletal clips with curves: sample the motion into dense poses and
	// remove the curves, instead of requiring every key to land on a whole
	// frame of the result.
	bool bakeCurves = false;
	ModelLoopClose loopClose = ModelLoopClose::ReplaceLast;
	int blendFrames = 0;
	bool operator==(const ModelFrameResampleOptions &) const = default;
};

// The frames a resample produces from `frames` stored frames: their count
// and, for each, the fractional source frame it samples (0 .. frames - 1, or
// up to `frames` for a cyclic clip, where the last step wraps to frame 0).
// False with a translated reason when the rates or counts are invalid or the
// result would hold no frame or more than 1024.
bool modelResamplePositions(int frames, double sourceFps, const ModelFrameResampleOptions &options, QVector<double> *positions,
							QString *error = nullptr);

// How strongly a range edit acts on each frame of its range.
enum class ModelFrameFalloff
{
	// Every frame fully.
	Constant,
	// Growing toward the last frame: k / n for the k-th of n frames.
	RampIn,
	// Fading after the first frame: (n - k + 1) / n.
	RampOut,
	// A smooth bump, strongest in the middle and gentle at both ends:
	// sin^2(pi * k / (n + 1)).
	Smooth,
};
// The weight of the `index`-th frame (zero-based) of a `count`-frame range.
[[nodiscard]] double modelFrameFalloffWeight(ModelFrameFalloff falloff, int index, int count);

// ModelEdit::poseRange, for TransformFrameRange, BlendFramePose and
// ApplyRelativePose. Defaults are required by every other edit.
struct ModelPoseRangeOptions
{
	// The inclusive frame range edited.
	int first = -1;
	int last = -1;
	ModelFrameFalloff falloff = ModelFrameFalloff::Constant;
	// BlendFramePose: the frame whose pose the selection blends toward.
	// ApplyRelativePose: frame A of the difference A - B.
	int sourceFrame = -1;
	// ApplyRelativePose: frame B of the difference A - B.
	int baseFrame = -1;
	// BlendFramePose: 0 (unchanged) to 1 (the source pose); ApplyRelativePose:
	// the weight of the difference, -10 to 10. Both scale with the falloff.
	double amount = 1;
	bool operator==(const ModelPoseRangeOptions &) const = default;
};
// Range transforms, partial pose blends and relative (additive) poses on the
// selected vertices of one surface: TransformFrameRange takes ModelEdit's
// translation, rotation, scale and pivot (resolved once, in pivotFrame) and
// scales them per frame by the falloff; BlendFramePose moves the selection
// toward another frame's pose; ApplyRelativePose adds the difference between
// two poses. Normals are rebuilt only around the moved vertices; unselected
// vertices, frames outside the range, tags and collision stay exact.
[[nodiscard]] bool isModelPoseRangeEdit(ModelEditKind kind);
bool applyModelPoseRangeEdit(ModelMesh *candidate, const ModelEdit &edit, QString *error = nullptr, const ModelWorkControl &control = {});

[[nodiscard]] bool isModelFrameRangeEdit(ModelEditKind kind);

// One frame permutation. order[r] is the frame of the model before the edit
// that resulting frame r copies (frames may repeat or be left out);
// animations is the complete resulting clip list. Without explicit groups,
// MDL native groups are rebuilt from the order: consecutive resulting frames
// copied from one timed group stay one group with their own durations, and
// every other frame is a single native frame.
struct ModelFrameRemap
{
	QVector<int> order;
	QVector<ModelAnimation> animations;
	bool explicitMdlGroups = false;
	QVector<ModelMdlFrameGroup> mdlGroups;
};
// Rewrites every frame-indexed record of `mesh` (see the top of this file).
// Refuses, leaving `mesh` unchanged, an order outside the model or over the
// frame and frame-vertex limits, and MDL timing that would not validate.
// Does not touch skeleton.bakedClipForFrame: applyModelEdit reconciles it.
bool remapModelFrames(ModelMesh *mesh, const ModelFrameRemap &remap, QString *error = nullptr, const ModelWorkControl &control = {});

// The resulting frame order of a range edit (see ModelFrameRemap::order).
// False with a translated reason for an invalid range or destination.
// `generated`, when given, marks resulting frames whose poses the edit
// computes rather than copies (resampled and blended frames).
bool modelFrameRangeOrder(const ModelMesh &mesh, const ModelEdit &edit, QVector<int> *order, QString *error = nullptr,
						  QVector<bool> *generated = nullptr);
// Skeletal models: a translated reason when the edit would move, copy,
// remove or reorder a frame that follows the skeleton (the bind frame or a
// frame baked from a skeletal clip) while a surface is skinned; empty when
// it is allowed. Once every surface is unbound, the frames are ordinary
// vertex animation and nothing is refused.
[[nodiscard]] QString modelFrameRangeSkeletalRefusal(const ModelMesh &mesh, const ModelEdit &edit);
// ModelSkinReconcileOptions::frameOrigins for a range edit: the frame before
// the edit each resulting frame came from, -1 for a copy and for every frame
// of a skeletal clip whose frames no longer play in order.
[[nodiscard]] QVector<int> modelFrameRangeSkeletalOrigins(const ModelMesh &mesh, const ModelEdit &edit);
// Skeletal clip resampling and loop closing (ResampleSkeletalClip,
// CloseSkeletalLoop) on ModelSkeleton::clips: dense poses interpolate in
// parent space; curves are evaluated at the new sample times and their keys
// rescaled, which must land every key on a whole frame unless bakeCurves.
[[nodiscard]] bool isModelSkeletalResampleEdit(ModelEditKind kind);
bool applyModelSkeletalResampleEdit(ModelMesh *candidate, const ModelEdit &edit, QString *error = nullptr,
									const ModelWorkControl &control = {});
// Vertex clip resampling and loop closing (ResampleAnimation,
// CloseAnimationLoop); called by applyModelFrameRangeEdit.
bool applyModelFrameResampleEdit(ModelMesh *candidate, const ModelEdit &edit, QString *error = nullptr, const ModelWorkControl &control = {});
// Candidate-only implementation; applyModelEdit validates and adopts.
bool applyModelFrameRangeEdit(ModelMesh *candidate, const ModelEdit &edit, QString *error = nullptr, const ModelWorkControl &control = {});

// The frame-name stem a clip's frames use: ASCII letters, digits, '_' and
// '-' from the clip name, without trailing separators, short enough that
// the stem, a '_' when the stem ends in a digit, and `highestNumber` fit 15
// characters. Never empty ("frame").
[[nodiscard]] QString modelClipFrameStem(const QString &clipName, int highestNumber);
// The stem a reader of a format without a clip table groups this frame name
// under: trailing digits, then trailing separators, removed ("walk_3" ->
// "walk"); the trimmed name itself when nothing else is left.
[[nodiscard]] QString modelFrameNameStem(const QString &name);
// stem + number, with the '_' that keeps a stem ending in a digit apart from
// the number ("attack2" -> "attack2_1").
[[nodiscard]] QString modelClipFrameName(const QString &stem, int number);

// One authored clip and what a reader of a format without a clip table
// (MDL, MD2, MD3) rebuilds in its place.
struct ModelNativeClipCheck
{
	int clip = -1;
	QString name;
	int first = 0, last = 0;
	// The clip comes back with this name and range.
	bool survives = false;
	// The clip's saved FPS, which those formats never keep.
	bool rateLost = false;
	// What the frame names give at the clip's first frame instead.
	QString rebuiltName;
	int rebuiltFirst = 0, rebuiltLast = 0;
};
[[nodiscard]] QVector<ModelNativeClipCheck> modelNativeClipRoundTrip(const ModelMesh &mesh);
// Translated one-line summary of the checks for the editor and CLI text:
// how many clips survive and which do not. Empty for a model without clips.
[[nodiscard]] QString modelNativeClipSummary(const QVector<ModelNativeClipCheck> &checks);
} // namespace vibestudio
