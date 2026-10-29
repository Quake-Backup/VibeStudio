#include "core/model_rotation_tracks.h"
#include "core/model_animation_curves.h"
#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <numbers>

// Squad: K. Shoemake, "Animating rotation with quaternion curves", SIGGRAPH
// 1985. Implemented from the published description; original code.

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelRotationTracks)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
using Q = std::array<double, 4>;
const QStringList &modes()
{
	static const QStringList names{QStringLiteral("step"), QStringLiteral("linear"), QStringLiteral("cubic"), QStringLiteral("step-next")};
	return names;
}
double dot(const Q &a, const Q &b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}
Q scaled(const Q &a, double s)
{
	return {a[0] * s, a[1] * s, a[2] * s, a[3] * s};
}
Q normalised(const Q &a)
{
	const double length = std::sqrt(dot(a, a));
	return length > 0 ? scaled(a, 1 / length) : Q{0, 0, 0, 1};
}
Q negated(const Q &a)
{
	return {-a[0], -a[1], -a[2], -a[3]};
}
Q conjugate(const Q &a)
{
	return {-a[0], -a[1], -a[2], a[3]};
}
Q multiply(const Q &a, const Q &b)
{
	return {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
			a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
}
// The same rotation on b's side of the 4D sphere as a.
Q aligned(const Q &a, const Q &b)
{
	return dot(a, b) < 0 ? negated(b) : b;
}
Q slerp(const Q &a, Q b, double t)
{
	b = aligned(a, b);
	const double cosine = std::clamp(dot(a, b), -1.0, 1.0);
	if (cosine > 0.9999995)
		return normalised({a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t, a[3] + (b[3] - a[3]) * t});
	const double angle = std::acos(cosine), s = std::sin(angle);
	const double wa = std::sin((1 - t) * angle) / s, wb = std::sin(t * angle) / s;
	return normalised({a[0] * wa + b[0] * wb, a[1] * wa + b[1] * wb, a[2] * wa + b[2] * wb, a[3] * wa + b[3] * wb});
}
// Logarithm and exponential of unit and pure quaternions.
Q logarithm(const Q &q)
{
	const double v = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
	if (v < 1e-12)
		return {0, 0, 0, 0};
	const double angle = std::atan2(v, q[3]) / v;
	return {q[0] * angle, q[1] * angle, q[2] * angle, 0};
}
Q exponential(const Q &q)
{
	const double v = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
	if (v < 1e-12)
		return {0, 0, 0, 1};
	const double s = std::sin(v) / v;
	return {q[0] * s, q[1] * s, q[2] * s, std::cos(v)};
}
// Shoemake's intermediate quaternion at q with neighbours before and after
// (already on q's side): q exp(-(log(q^-1 after) + log(q^-1 before)) / 4).
Q intermediate(const Q &before, const Q &q, const Q &after)
{
	const auto inverse = conjugate(q);
	const auto a = logarithm(multiply(inverse, after)), b = logarithm(multiply(inverse, before));
	return normalised(multiply(q, exponential({-(a[0] + b[0]) / 4, -(a[1] + b[1]) / 4, -(a[2] + b[2]) / 4, 0})));
}
Q value(const ModelPoseQuatKey &key)
{
	return normalised(key.value);
}
std::array<double, 3> eulerOf(const QVector<ModelPoseCurveKey> &x, const QVector<ModelPoseCurveKey> &y, const QVector<ModelPoseCurveKey> &z,
							  double frame)
{
	return {evaluateModelCurve(x, frame), evaluateModelCurve(y, frame), evaluateModelCurve(z, frame)};
}
QVector<ModelPoseCurveKey> placeholder()
{
	return {ModelPoseCurveKey{0, 0.0}};
}
} // namespace

bool modelPoseCurvesUseQuaternion(const ModelPoseCurves &group)
{
	return !group.rotation.isEmpty();
}
bool validateModelQuatTrack(const QVector<ModelPoseQuatKey> &keys, int frameCount, QString *error)
{
	if (keys.isEmpty() || keys.size() > frameCount)
		return fail(error, Text::tr("A quaternion rotation track needs at least one key and at most one key per sample."));
	int previous = -1;
	for (const auto &key : keys)
	{
		if (key.frame <= previous || key.frame >= frameCount || int(key.interpolation) < 0 || int(key.interpolation) > 3)
			return fail(error, Text::tr("Quaternion rotation keys need increasing samples inside the clip and a valid interpolation."));
		for (double component : key.value)
			if (!std::isfinite(component))
				return fail(error, Text::tr("Quaternion rotation keys must be finite."));
		if (std::abs(std::sqrt(dot(key.value, key.value)) - 1) > .0001)
			return fail(error, Text::tr("Quaternion rotation keys must have unit length."));
		previous = key.frame;
	}
	return true;
}
std::array<double, 4> sampleModelQuatTrack(const QVector<ModelPoseQuatKey> &keys, double frame)
{
	if (keys.isEmpty())
		return {0, 0, 0, 1};
	if (frame <= keys.first().frame)
		return value(keys.first());
	if (frame >= keys.last().frame)
		return value(keys.last());
	const int next = int(std::upper_bound(keys.cbegin(), keys.cend(), frame, [](double f, const ModelPoseQuatKey &k) { return f < k.frame; }) -
						 keys.cbegin());
	const int i = next - 1;
	const auto &left = keys[i];
	if (frame == left.frame)
		return value(left);
	const double t = (frame - left.frame) / double(keys[next].frame - left.frame);
	const Q a = value(left), b = aligned(a, value(keys[next]));
	switch (left.interpolation)
	{
	case ModelCurveInterpolation::Step:
		return a;
	case ModelCurveInterpolation::StepNext:
		return b;
	case ModelCurveInterpolation::Linear:
		return slerp(a, b, t);
	case ModelCurveInterpolation::Cubic:
		break;
	}
	// Neighbours outside the track repeat the end keys.
	const Q before = i > 0 ? aligned(a, value(keys[i - 1])) : a;
	const Q after = next + 1 < keys.size() ? aligned(b, value(keys[next + 1])) : b;
	const Q sa = intermediate(before, a, b), sb = intermediate(a, b, after);
	return slerp(slerp(a, b, t), slerp(sa, sb, t), 2 * t * (1 - t));
}
std::array<double, 4> modelQuatFromEulerDegrees(const std::array<double, 3> &degrees)
{
	const double half = std::numbers::pi / 360;
	const auto axis = [half](double angle, int index)
	{
		Q result{0, 0, 0, std::cos(angle * half)};
		result[index] = std::sin(angle * half);
		return result;
	};
	return normalised(multiply(axis(degrees[2], 2), multiply(axis(degrees[1], 1), axis(degrees[0], 0))));
}
std::array<double, 3> modelEulerDegreesFromQuat(const std::array<double, 4> &q, const std::array<double, 3> *previous)
{
	const auto r = normalised(q);
	const ModelJointMatrix matrix = modelJointMatrix(ModelQuat{float(r[0]), float(r[1]), float(r[2]), float(r[3])}, {});
	std::array<double, 9> values{};
	if (!modelPoseCurveValues(matrix, previous, &values))
		return previous ? *previous : std::array<double, 3>{};
	return {values[3], values[4], values[5]};
}
double modelQuatAngleDegrees(const std::array<double, 4> &a, const std::array<double, 4> &b)
{
	// atan2 of the relative rotation's parts stays accurate for nearly equal
	// rotations, where acos of their dot product would not.
	const auto relative = multiply(conjugate(normalised(a)), normalised(b));
	const double sine = std::sqrt(relative[0] * relative[0] + relative[1] * relative[1] + relative[2] * relative[2]);
	return 2 * std::atan2(sine, std::abs(relative[3])) * 180 / std::numbers::pi;
}
QVector<std::array<double, 3>> modelQuatTrackEuler(const QVector<ModelPoseQuatKey> &keys)
{
	QVector<std::array<double, 3>> result;
	for (const auto &key : keys)
		result.append(modelEulerDegreesFromQuat(key.value, result.isEmpty() ? nullptr : &result.last()));
	return result;
}
bool convertModelPoseCurvesRotation(ModelPoseCurves *group, bool toQuaternion, ModelRotationKeyTimes times, int frameCount,
									double *maximumChange, QString *error)
{
	if (!group || frameCount < 1 || !validateModelCurveGroup(*group, frameCount, error))
		return group && frameCount >= 1 ? false : fail(error, Text::tr("Choose a joint with valid curves in a nonempty clip."));
	if (toQuaternion == modelPoseCurvesUseQuaternion(*group))
		return fail(error, toQuaternion ? Text::tr("Joint %1 already keeps quaternion rotation keys.").arg(group->joint)
										: Text::tr("Joint %1 already keeps Euler rotation channels.").arg(group->joint));
	auto candidate = *group;
	QVector<int> frames;
	if (times == ModelRotationKeyTimes::EverySample)
		for (int f = 0; f < frameCount; ++f)
			frames.append(f);
	else if (toQuaternion)
	{
		QSet<int> seen;
		for (int c = 3; c < 6; ++c)
			for (const auto &key : group->channels[c])
				seen.insert(key.frame);
		frames = QVector<int>(seen.cbegin(), seen.cend());
		std::sort(frames.begin(), frames.end());
	}
	else
		for (const auto &key : group->rotation)
			frames.append(key.frame);
	if (toQuaternion)
	{
		candidate.rotation.clear();
		for (int frame : frames)
		{
			ModelPoseQuatKey key;
			key.frame = frame;
			key.value = modelQuatFromEulerDegrees(eulerOf(group->channels[3], group->channels[4], group->channels[5], frame));
			if (times == ModelRotationKeyTimes::Keys)
			{
				// The outgoing modes of the Euler keys at this sample: cubic
				// wins, then linear; step only where every channel holds.
				bool cubic = false, linear = false, next = true, held = true;
				for (int c = 3; c < 6; ++c)
				{
					const auto mode = sampleModelCurveKey(group->channels[c], frame).interpolation;
					cubic = cubic || mode == ModelCurveInterpolation::Cubic;
					linear = linear || mode == ModelCurveInterpolation::Linear;
					next = next && mode == ModelCurveInterpolation::StepNext;
					held = held && mode == ModelCurveInterpolation::Step;
				}
				key.interpolation = cubic ? ModelCurveInterpolation::Cubic
								  : linear || (!next && !held) ? ModelCurveInterpolation::Linear
								  : next ? ModelCurveInterpolation::StepNext
										 : ModelCurveInterpolation::Step;
			}
			candidate.rotation.append(key);
		}
		for (int c = 3; c < 6; ++c)
			candidate.channels[c] = placeholder();
	}
	else
	{
		std::array<double, 3> previous{};
		for (int c = 3; c < 6; ++c)
			candidate.channels[c].clear();
		for (int i = 0; i < frames.size(); ++i)
		{
			const auto rotation = sampleModelQuatTrack(group->rotation, frames[i]);
			previous = modelEulerDegreesFromQuat(rotation, i ? &previous : nullptr);
			ModelCurveInterpolation mode = ModelCurveInterpolation::Linear;
			if (times == ModelRotationKeyTimes::Keys)
				mode = group->rotation[i].interpolation;
			for (int c = 0; c < 3; ++c)
			{
				ModelPoseCurveKey key;
				key.frame = frames[i];
				key.value = previous[c];
				key.interpolation = mode;
				key.tangent = ModelCurveTangent::Clamped;
				candidate.channels[3 + c].append(key);
			}
		}
		candidate.rotation.clear();
	}
	if (!validateModelCurveGroup(candidate, frameCount, error))
		return false;
	if (maximumChange)
	{
		double change = 0;
		for (int f = 0; f < frameCount; ++f)
		{
			const auto before = modelJointRotation(evaluateModelPoseCurves(*group, f)), after = modelJointRotation(evaluateModelPoseCurves(candidate, f));
			change = std::max(change, modelQuatAngleDegrees({before.x, before.y, before.z, before.w}, {after.x, after.y, after.z, after.w}));
		}
		*maximumChange = change;
	}
	*group = std::move(candidate);
	return true;
}
bool setModelQuatKey(ModelPoseCurves *group, const ModelPoseQuatKey &key, int frameCount, QString *error)
{
	if (!group || !modelPoseCurvesUseQuaternion(*group))
		return fail(error, Text::tr("Convert the joint's rotation to quaternion keys before setting quaternion keys."));
	auto candidate = *group;
	auto normalisedKey = key;
	normalisedKey.value = normalised(key.value);
	auto &keys = candidate.rotation;
	auto at = std::lower_bound(keys.begin(), keys.end(), key.frame, [](const ModelPoseQuatKey &k, int f) { return k.frame < f; });
	if (at != keys.end() && at->frame == key.frame)
		*at = normalisedKey;
	else
		keys.insert(at, normalisedKey);
	if (!validateModelCurveGroup(candidate, frameCount, error))
		return false;
	*group = std::move(candidate);
	return true;
}
bool removeModelQuatKey(ModelPoseCurves *group, int frame, QString *error)
{
	if (!group || !modelPoseCurvesUseQuaternion(*group))
		return fail(error, Text::tr("The joint keeps no quaternion rotation keys."));
	auto &keys = group->rotation;
	const auto at = std::find_if(keys.begin(), keys.end(), [&](const ModelPoseQuatKey &k) { return k.frame == frame; });
	if (at == keys.end())
		return fail(error, Text::tr("There is no rotation key at that sample."));
	if (keys.size() == 1)
		return fail(error, Text::tr("Keep at least one rotation key. Convert the rotation to Euler channels or bake the joint instead."));
	keys.erase(at);
	return true;
}
bool setModelQuatKeyAxis(ModelPoseCurves *group, int frame, int axis, double degrees, ModelCurveInterpolation interpolation, int frameCount,
						 QString *error)
{
	if (!group || axis < 0 || axis > 2 || !std::isfinite(degrees) || std::abs(degrees) > 1000000)
		return fail(error, Text::tr("Choose a rotation axis and a finite angle."));
	// The other axes as the angle view shows them at this sample.
	const auto view = modelQuatTrackEuler(group->rotation);
	const std::array<double, 3> *nearest = nullptr;
	for (int i = 0; i < group->rotation.size(); ++i)
		if (group->rotation[i].frame <= frame)
			nearest = &view[i];
	auto angles = modelEulerDegreesFromQuat(sampleModelQuatTrack(group->rotation, frame), nearest);
	angles[axis] = degrees;
	ModelPoseQuatKey key;
	key.frame = frame;
	key.value = modelQuatFromEulerDegrees(angles);
	key.interpolation = interpolation;
	return setModelQuatKey(group, key, frameCount, error);
}
QJsonArray modelQuatKeysJson(const QVector<ModelPoseQuatKey> &keys)
{
	QJsonArray result;
	for (const auto &key : keys)
		result.append(QJsonArray{key.frame, key.value[0], key.value[1], key.value[2], key.value[3], modes().value(int(key.interpolation))});
	return result;
}
bool modelQuatKeysFromJson(const QJsonValue &value, int frameCount, QVector<ModelPoseQuatKey> *keys)
{
	if (!keys || !value.isArray() || value.toArray().size() > frameCount)
		return false;
	QVector<ModelPoseQuatKey> result;
	for (const auto &entry : value.toArray())
	{
		const auto key = entry.toArray();
		if (key.size() != 6 || !key[0].isDouble() || std::floor(key[0].toDouble()) != key[0].toDouble())
			return false;
		ModelPoseQuatKey parsed;
		parsed.frame = key[0].toInt();
		for (int i = 0; i < 4; ++i)
		{
			if (!key[1 + i].isDouble())
				return false;
			parsed.value[i] = key[1 + i].toDouble();
		}
		const int mode = modes().indexOf(key[5].toString());
		if (mode < 0)
			return false;
		parsed.interpolation = ModelCurveInterpolation(mode);
		result.append(parsed);
	}
	if (!validateModelQuatTrack(result, frameCount))
		return false;
	*keys = std::move(result);
	return true;
}
} // namespace vibestudio
