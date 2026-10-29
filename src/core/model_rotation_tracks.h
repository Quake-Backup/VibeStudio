#pragma once

// Quaternion rotation tracks for animation curves (ModelPoseCurves::rotation).
//
// A joint's curves hold its parent-local rotation either as three Euler
// channels (rx, ry, rz: degrees, X then Y then Z) or as quaternion keys. With
// quaternion keys, Linear slerps between neighbouring keys along the shorter
// arc, Cubic follows Shoemake's squad (spherical quadrangle interpolation)
// through the neighbouring keys, Step holds a key and StepNext the next; a key
// time returns its own key, and samples outside the keys hold the end keys.
// rx, ry and rz then each keep one zero key at sample 0 as placeholders, so
// code that reads nine channels still works; edits that would change Euler
// rotation keys refuse such joints until they are converted back.
//
// Squad follows K. Shoemake, "Animating rotation with quaternion curves",
// SIGGRAPH 1985, and its usual intermediate-quaternion construction; see
// docs/CREDITS.md. Original code.

#include "core/model_mesh.h"

#include <QJsonArray>
#include <QJsonValue>
#include <array>

namespace vibestudio
{
[[nodiscard]] bool modelPoseCurvesUseQuaternion(const ModelPoseCurves &group);
// Keys ordered inside the clip, unit length (within 0.0001), valid modes.
bool validateModelQuatTrack(const QVector<ModelPoseQuatKey> &keys, int frameCount, QString *error = nullptr);
// A validated track's normalised rotation at `frame`, as x, y, z, w.
[[nodiscard]] std::array<double, 4> sampleModelQuatTrack(const QVector<ModelPoseQuatKey> &keys, double frame);
// XYZ Euler degrees (R = Rz Ry Rx, the curves' convention) to a quaternion and
// back; the angles nearest `previous` when given, so a track stays continuous.
[[nodiscard]] std::array<double, 4> modelQuatFromEulerDegrees(const std::array<double, 3> &degrees);
[[nodiscard]] std::array<double, 3> modelEulerDegreesFromQuat(const std::array<double, 4> &q, const std::array<double, 3> *previous = nullptr);
// The angle between two rotations, in degrees (0-180).
[[nodiscard]] double modelQuatAngleDegrees(const std::array<double, 4> &a, const std::array<double, 4> &b);
// The angle view of a quaternion track: XYZ degrees at each key, unwrapped
// from one key to the next.
[[nodiscard]] QVector<std::array<double, 3>> modelQuatTrackEuler(const QVector<ModelPoseQuatKey> &keys);

enum class ModelRotationKeyTimes
{
	// At the samples that hold rotation keys.
	Keys,
	// At every sample, so every sample keeps its rotation exactly.
	EverySample
};
// Converts a group's rotation between Euler channels and quaternion keys.
// Keys: Euler to quaternion keys at every sample any of rx, ry, rz keys
// (cubic where any of them is cubic, step where all of them hold), and
// quaternion keys to Euler keys at the quaternion keys' samples (with the same
// interpolation). Interpolation between keys differs, so samples between keys
// can move; `maximumChange` reports the largest change at any sample, in
// degrees. Translation and scale channels are unchanged.
bool convertModelPoseCurvesRotation(ModelPoseCurves *group, bool toQuaternion, ModelRotationKeyTimes times, int frameCount,
									double *maximumChange = nullptr, QString *error = nullptr);
// Inserts or replaces one quaternion key (the value is normalised).
bool setModelQuatKey(ModelPoseCurves *group, const ModelPoseQuatKey &key, int frameCount, QString *error = nullptr);
// Removes the key at `frame`; a track keeps at least one key.
bool removeModelQuatKey(ModelPoseCurves *group, int frame, QString *error = nullptr);
// Sets one Euler axis (0-2) of the rotation at `frame` to `degrees`, keeping
// the other two axes as the track gives them there, as a quaternion key with
// `interpolation`. The rotation-channel editor of the curves dialog uses it.
bool setModelQuatKeyAxis(ModelPoseCurves *group, int frame, int axis, double degrees, ModelCurveInterpolation interpolation, int frameCount,
						 QString *error = nullptr);
// [[frame, x, y, z, w, "linear"], ...]; the reader validates against frameCount.
[[nodiscard]] QJsonArray modelQuatKeysJson(const QVector<ModelPoseQuatKey> &keys);
bool modelQuatKeysFromJson(const QJsonValue &value, int frameCount, QVector<ModelPoseQuatKey> *keys);
} // namespace vibestudio
