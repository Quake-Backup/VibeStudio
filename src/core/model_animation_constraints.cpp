#include "core/model_animation_constraints.h"
#include "core/model_animation_curves.h"
#include "core/model_document.h"
#include "core/model_skeleton.h"
#include "core/model_source_encoding.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelAnimationConstraints)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
using Pose = QVector<ModelJointMatrix>;
struct D
{
	double x = 0, y = 0, z = 0;
	D operator-(D b) const
	{
		return {x - b.x, y - b.y, z - b.z};
	}
	D operator*(double n) const
	{
		return {x * n, y * n, z * n};
	}
	double dot(D b) const
	{
		return x * b.x + y * b.y + z * b.z;
	}
	D cross(D b) const
	{
		return {y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x};
	}
	double length() const
	{
		return std::sqrt(dot(*this));
	}
};
D d(ModelVec3 v)
{
	return {v.x, v.y, v.z};
}
ModelVec3 vec(const std::array<double, 3> &a)
{
	return {float(a[0]), float(a[1]), float(a[2])};
}
bool finite(const std::array<double, 3> &a)
{
	return std::all_of(a.cbegin(), a.cend(), [](double v) { return std::isfinite(v) && std::abs(v) <= 1000000; });
}
QVector<bool> subtree(const ModelSkeleton &skeleton, int joint)
{
	QVector<bool> inside(skeleton.joints.size(), false);
	for (int j = std::max(0, joint); joint >= 0 && j < inside.size(); ++j)
		inside[j] = j == joint || (skeleton.joints[j].parent >= 0 && inside[skeleton.joints[j].parent]);
	return inside;
}
ModelJointMatrix local(const ModelSkeleton &skeleton, const Pose &pose, int joint)
{
	const int parent = skeleton.joints[joint].parent;
	return parent < 0 ? pose[joint] : modelJointMultiply(modelJointInverse(pose[parent]), pose[joint]);
}
// Joints not in `fixed` keep their parent-local pose from `before` and follow
// their (possibly moved) parents.
void follow(const ModelSkeleton &skeleton, const Pose &before, Pose *pose, const QVector<bool> &fixed)
{
	for (int j = 0; j < pose->size(); ++j)
	{
		if (fixed[j])
			continue;
		const int parent = skeleton.joints[j].parent;
		if (parent < 0 || modelJointMatrixDistance((*pose)[parent], before[parent]) == 0)
			(*pose)[j] = before[j];
		else
			(*pose)[j] = modelJointMultiply(modelJointMultiply((*pose)[parent], modelJointInverse(before[parent])), before[j]);
	}
}
ModelQuat quatFromEuler(double x, double y, double z)
{
	const double half = std::numbers::pi / 360;
	const auto axis = [half](double degrees, int index)
	{
		ModelQuat result{0, 0, 0, float(std::cos(degrees * half))};
		const float s = float(std::sin(degrees * half));
		(index == 0 ? result.x : index == 1 ? result.y : result.z) = s;
		return result;
	};
	return modelQuatNormalized(modelQuatMultiply(axis(z, 2), modelQuatMultiply(axis(y, 1), axis(x, 0))));
}
ModelVec3 lerp(ModelVec3 a, ModelVec3 b, double t)
{
	return {float(a.x + (double(b.x) - a.x) * t), float(a.y + (double(b.y) - a.y) * t), float(a.z + (double(b.z) - a.z) * t)};
}
// Blends two parent-local poses: translation and scale linearly, rotation along
// the shortest arc.
ModelJointMatrix blend(const ModelJointMatrix &a, const ModelJointMatrix &b, double t)
{
	return modelJointMatrix(modelQuatSlerp(modelJointRotation(a), modelJointRotation(b), float(t)),
							lerp(modelJointTranslation(a), modelJointTranslation(b), t), lerp(modelJointScale(a), modelJointScale(b), t));
}
// A rotation taking direction `from` to `to` (both nonzero), as a quaternion.
ModelQuat arc(D from, D to)
{
	from = from * (1 / from.length());
	to = to * (1 / to.length());
	const double cosine = std::clamp(from.dot(to), -1.0, 1.0);
	D axis = from.cross(to);
	if (axis.length() < 1e-12)
	{
		if (cosine > 0)
			return {};
		const D basis = std::abs(from.x) < .9 ? D{1, 0, 0} : D{0, 1, 0};
		axis = from.cross(basis);
	}
	axis = axis * (1 / axis.length());
	const double half = std::acos(cosine) / 2;
	return modelQuatNormalized({float(axis.x * std::sin(half)), float(axis.y * std::sin(half)), float(axis.z * std::sin(half)), float(std::cos(half))});
}
// Turns a model-space joint pose about its own origin.
ModelJointMatrix turned(const ModelJointMatrix &matrix, const ModelQuat &rotation)
{
	auto result = modelJointMultiply(modelJointMatrix(rotation, {}), matrix);
	result.m[3] = matrix.m[3];
	result.m[7] = matrix.m[7];
	result.m[11] = matrix.m[11];
	return result;
}
D axisVector(int axis)
{
	const double sign = axis >= 3 ? -1 : 1;
	return {axis % 3 == 0 ? sign : 0, axis % 3 == 1 ? sign : 0, axis % 3 == 2 ? sign : 0};
}
int lastSample(const ModelClipConstraint &c, int frames)
{
	return c.last < 0 ? frames - 1 : c.last;
}
bool nearPose(const ModelJointMatrix &a, const ModelJointMatrix &b)
{
	for (int n = 0; n < 12; ++n)
		if (std::abs(double(a.m[n]) - b.m[n]) > std::max(.0001, std::abs(double(b.m[n])) * .000002))
			return false;
	return true;
}
const QStringList &weightModes()
{
	static const QStringList modes{QStringLiteral("step"), QStringLiteral("linear"), QStringLiteral("cubic"), QStringLiteral("step-next")};
	return modes;
}
const QStringList &weightTangents()
{
	static const QStringList tangents{QStringLiteral("auto"), QStringLiteral("clamped"), QStringLiteral("free"), QStringLiteral("flat")};
	return tangents;
}
QJsonArray weightKeysJson(const QVector<ModelPoseCurveKey> &keys)
{
	QJsonArray result;
	for (const auto &k : keys)
		result.append(QJsonArray{k.frame, k.value, weightModes().value(int(k.interpolation)), weightTangents().value(int(k.tangent)), k.inSlope,
								 k.outSlope});
	return result;
}
bool readWeightKeys(const QJsonValue &value, int frames, QVector<ModelPoseCurveKey> *keys)
{
	if (value.isUndefined())
		return true;
	if (!value.isArray() || value.toArray().size() > frames)
		return false;
	QVector<ModelPoseCurveKey> result;
	for (const auto &entry : value.toArray())
	{
		const auto key = entry.toArray();
		if (key.size() != 6 || !key[0].isDouble() || std::floor(key[0].toDouble()) != key[0].toDouble() || !key[1].isDouble() ||
			!key[4].isDouble() || !key[5].isDouble())
			return false;
		const int mode = weightModes().indexOf(key[2].toString()), tangent = weightTangents().indexOf(key[3].toString());
		if (mode < 0 || tangent < 0)
			return false;
		result.append({key[0].toInt(), key[1].toDouble(), key[4].toDouble(), key[5].toDouble(), ModelCurveInterpolation(mode),
					   ModelCurveTangent(tangent)});
	}
	if (!validModelWeightKeys(result, frames))
		return false;
	*keys = std::move(result);
	return true;
}
bool layer(const ModelSkeleton &skeleton, const ModelClipLayer &layer, int frame, Pose *pose, QString *error)
{
	const double w = layer.enabled ? std::min(1.0, modelClipStackWeight(layer.weight, layer.weightKeys, frame)) : 0.0;
	if (w <= 0)
		return true;
	const auto before = *pose;
	QVector<int> group(pose->size(), -1);
	for (int g = 0; g < layer.curves.size(); ++g)
		group[modelJointIndex(skeleton, layer.curves[g].joint, Qt::CaseSensitive)] = g;
	// Parents come first: keyed joints take the layer's local pose, the others
	// keep theirs and follow.
	for (int j = 0; j < pose->size(); ++j)
	{
		const int parent = skeleton.joints[j].parent;
		if (group[j] < 0)
		{
			if (parent >= 0 && modelJointMatrixDistance((*pose)[parent], before[parent]) != 0)
				(*pose)[j] = modelJointMultiply(modelJointMultiply((*pose)[parent], modelJointInverse(before[parent])), before[j]);
			continue;
		}
		const auto &curves = layer.curves[group[j]];
		double v[9];
		for (int c = 0; c < 9; ++c)
			v[c] = evaluateModelCurve(curves.channels[c], frame);
		const auto base = local(skeleton, before, j);
		const auto t = modelJointTranslation(base), s = modelJointScale(base);
		const auto r = modelJointRotation(base);
		ModelJointMatrix updated;
		if (layer.additive)
			updated = modelJointMatrix(modelQuatMultiply(r, quatFromEuler(w * v[3], w * v[4], w * v[5])),
									   {float(t.x + w * v[0]), float(t.y + w * v[1]), float(t.z + w * v[2])},
									   {float(s.x * (1 + w * (v[6] - 1))), float(s.y * (1 + w * (v[7] - 1))), float(s.z * (1 + w * (v[8] - 1)))});
		else
			updated = blend(base, modelJointMatrix(quatFromEuler(v[3], v[4], v[5]), {float(v[0]), float(v[1]), float(v[2])},
												   {float(v[6]), float(v[7]), float(v[8])}),
							w);
		if (!modelJointMatrixIsFinite(updated))
			return fail(error, Text::tr("Layer %1 gives joint %2 a pose that is not a finite number.").arg(layer.name, skeleton.joints[j].name));
		(*pose)[j] = parent < 0 ? updated : modelJointMultiply((*pose)[parent], updated);
	}
	return true;
}
// Evaluates one constraint over every sample of `poses` in its range.
bool constrain(const ModelSkeleton &skeleton, const ModelClipConstraint &c, QVector<Pose> *poses, QString *error, ModelWorkProgress &work)
{
	if (!c.enabled || c.weight <= 0 || poses->isEmpty())
		return true;
	const int joint = modelJointIndex(skeleton, c.joint, Qt::CaseSensitive);
	const int target = c.target.isEmpty() ? -1 : modelJointIndex(skeleton, c.target, Qt::CaseSensitive);
	const int first = c.first, last = lastSample(c, int(poses->size()));
	QVector<bool> affected = subtree(skeleton, joint);
	int root = joint;
	if (c.kind == ModelClipConstraintKind::Ik || c.kind == ModelClipConstraintKind::FootLock)
	{
		root = c.rootJoint.isEmpty() ? -1 : modelJointIndex(skeleton, c.rootJoint, Qt::CaseSensitive);
		if (root < 0 && skeleton.joints[joint].parent >= 0)
			root = skeleton.joints[skeleton.joints[joint].parent].parent;
	}
	// The joints the constraint sets itself: the joint, and an IK chain.
	QVector<bool> moved(skeleton.joints.size(), false);
	for (int j = joint; j >= 0; j = skeleton.joints[j].parent)
	{
		moved[j] = true;
		if (j == root)
			break;
	}
	const auto lockAt = modelJointTranslation((*poses)[first][joint]);
	const auto childOffset = target < 0 ? modelJointIdentity()
										: modelJointMultiply(modelJointInverse((*poses)[first][target]), (*poses)[first][joint]);
	for (int f = first; f <= last; ++f)
	{
		if (!work.step())
			return false;
		const double weight = std::min(1.0, modelClipStackWeight(c.weight, c.weightKeys, f));
		if (weight <= 0)
			continue;
		const auto before = (*poses)[f];
		auto pose = before;
		const auto goal = target < 0 ? vec(c.point) : modelJointTransformPoint(before[target], vec(c.point));
		QString problem;
		switch (c.kind)
		{
		case ModelClipConstraintKind::Ik:
			if (!solveModelConstraintIk(skeleton, &pose, c.rootJoint, c.joint, goal, vec(c.pole), c.usePole, c.limits, &problem))
				return fail(error, Text::tr("Constraint %1, sample %2: %3").arg(c.name).arg(f).arg(problem));
			break;
		case ModelClipConstraintKind::FootLock:
			if (!solveModelConstraintIk(skeleton, &pose, c.rootJoint, c.joint, lockAt, vec(c.pole), c.usePole, c.limits, &problem))
				return fail(error, Text::tr("Constraint %1, sample %2: %3").arg(c.name).arg(f).arg(problem));
			break;
		case ModelClipConstraintKind::Aim:
		{
			const auto at = modelJointTranslation(pose[joint]);
			const D direction = d(goal) - d(at);
			if (direction.length() < 1e-9)
				break;
			const auto rotation = modelJointRotation(pose[joint]);
			const D axis = axisVector(c.axis);
			const D aim = d(modelQuatRotate(rotation, {float(axis.x), float(axis.y), float(axis.z)}));
			auto turn = arc(aim, direction);
			if (c.usePole)
			{
				// Twist about the aim so the next joint axis leans towards `pole`.
				const D aimed = direction * (1 / direction.length());
				const auto upAxis = axisVector((c.axis % 3 + 1) % 3);
				const D up = d(modelQuatRotate(modelQuatMultiply(turn, rotation), {float(upAxis.x), float(upAxis.y), float(upAxis.z)}));
				const D wanted = d(vec(c.pole)) - aimed * d(vec(c.pole)).dot(aimed);
				const D current = up - aimed * up.dot(aimed);
				if (wanted.length() > 1e-9 && current.length() > 1e-9)
					turn = modelQuatMultiply(arc(current, wanted), turn);
			}
			pose[joint] = turned(pose[joint], turn);
			break;
		}
		case ModelClipConstraintKind::ChildOf:
			pose[joint] = modelJointMultiply(before[target], childOffset);
			break;
		case ModelClipConstraintKind::CopyTransform:
			pose[joint] = before[target];
			break;
		}
		// The constrained joint's descendants follow it.
		QVector<bool> fixed(pose.size(), false);
		for (int j = 0; j < pose.size(); ++j)
			fixed[j] = moved[j] || !affected[j];
		if (c.kind == ModelClipConstraintKind::Ik || c.kind == ModelClipConstraintKind::FootLock)
			for (int j = 0; j < pose.size(); ++j)
				fixed[j] = true;
		follow(skeleton, before, &pose, fixed);
		if (weight < 1)
		{
			auto blended = before;
			QVector<bool> set(pose.size(), false);
			for (int j = 0; j < pose.size(); ++j)
			{
				if (!moved[j])
					continue;
				const int parent = skeleton.joints[j].parent;
				const auto mixed = blend(local(skeleton, before, j), local(skeleton, pose, j), weight);
				blended[j] = parent < 0 ? mixed : modelJointMultiply(blended[parent], mixed);
				set[j] = true;
			}
			follow(skeleton, before, &blended, set);
			pose = blended;
		}
		(*poses)[f] = pose;
	}
	return true;
}
QJsonArray triple(const std::array<double, 3> &a)
{
	return {a[0], a[1], a[2]};
}
bool readTriple(const QJsonValue &value, std::array<double, 3> *a)
{
	const auto array = value.toArray();
	if (array.size() != 3)
		return false;
	for (int i = 0; i < 3; ++i)
	{
		if (!array.at(i).isDouble())
			return false;
		(*a)[i] = array.at(i).toDouble();
	}
	return true;
}
} // namespace

QString modelClipConstraintKindId(ModelClipConstraintKind kind)
{
	switch (kind)
	{
	case ModelClipConstraintKind::Ik:
		return QStringLiteral("ik");
	case ModelClipConstraintKind::Aim:
		return QStringLiteral("aim");
	case ModelClipConstraintKind::ChildOf:
		return QStringLiteral("child-of");
	case ModelClipConstraintKind::CopyTransform:
		return QStringLiteral("copy-transform");
	case ModelClipConstraintKind::FootLock:
		return QStringLiteral("foot-lock");
	}
	return {};
}
int modelClipConstraintKindFromId(const QString &id)
{
	for (int kind = 0; kind <= int(ModelClipConstraintKind::FootLock); ++kind)
		if (modelClipConstraintKindId(ModelClipConstraintKind(kind)) == id)
			return kind;
	return -1;
}
bool modelClipHasStack(const ModelSkeletalClip &clip)
{
	return !clip.constraints.isEmpty() || !clip.layers.isEmpty();
}
double modelClipStackWeight(double weight, const QVector<ModelPoseCurveKey> &keys, int frame)
{
	return keys.isEmpty() ? weight : weight * std::clamp(evaluateModelCurve(keys, frame), 0.0, 1.0);
}
bool validModelWeightKeys(const QVector<ModelPoseCurveKey> &keys, int frames, QString *error)
{
	if (keys.isEmpty())
		return true;
	// The shared curve rules (order, range, finite slopes) through a group
	// whose other channels hold one neutral key each.
	ModelPoseCurves group;
	group.joint = QStringLiteral("weight");
	for (int c = 0; c < 9; ++c)
		group.channels[c] = c == 0 ? keys : QVector<ModelPoseCurveKey>{ModelPoseCurveKey{0, c >= 6 ? 1.0 : 0.0}};
	QString problem;
	if (keys.size() > frames || !validateModelCurveGroup(group, frames, &problem))
		return fail(error, Text::tr("Weight keys must be ordered samples inside the clip. %1").arg(problem));
	for (const auto &key : keys)
		if (key.value < 0 || key.value > 1)
			return fail(error, Text::tr("Weight keys hold values from 0 to 1."));
	return true;
}
bool parseModelWeightKeys(const QString &text, int frames, QVector<ModelPoseCurveKey> *keys, QString *error)
{
	if (!keys)
		return false;
	QVector<ModelPoseCurveKey> result;
	const auto parts = text.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
	for (const auto &part : parts)
	{
		const auto pair = part.split(QLatin1Char(':'));
		bool frameOk = false, valueOk = false;
		const int frame = pair.value(0).toInt(&frameOk);
		const double value = pair.value(1).toDouble(&valueOk);
		if (pair.size() != 2 || !frameOk || !valueOk)
			return fail(error, Text::tr("Write weight keys as sample:value pairs, such as 0:0, 12:1."));
		ModelPoseCurveKey key;
		key.frame = frame;
		key.value = value;
		result.append(key);
	}
	std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.frame < b.frame; });
	if (!validModelWeightKeys(result, frames, error))
		return false;
	*keys = std::move(result);
	return true;
}
QString modelWeightKeysText(const QVector<ModelPoseCurveKey> &keys)
{
	QStringList parts;
	for (const auto &key : keys)
		parts << QStringLiteral("%1:%2").arg(key.frame).arg(key.value, 0, 'g', 9);
	return parts.join(QStringLiteral(", "));
}
const QVector<QVector<ModelJointMatrix>> &modelClipEditableSamples(const ModelSkeletalClip &clip)
{
	return modelClipHasStack(clip) && clip.base.size() == clip.frames.size() ? clip.base : clip.frames;
}
QVector<bool> modelClipConstraintAffectedJoints(const ModelSkeleton &skeleton, const ModelClipConstraint &c)
{
	int top = modelJointIndex(skeleton, c.joint, Qt::CaseSensitive);
	if (top >= 0 && (c.kind == ModelClipConstraintKind::Ik || c.kind == ModelClipConstraintKind::FootLock))
	{
		int root = c.rootJoint.isEmpty() ? -1 : modelJointIndex(skeleton, c.rootJoint, Qt::CaseSensitive);
		if (root < 0 && skeleton.joints[top].parent >= 0)
			root = skeleton.joints[skeleton.joints[top].parent].parent;
		if (root >= 0)
			top = root;
	}
	return subtree(skeleton, top);
}
bool validModelClipStack(const ModelSkeleton &skeleton, const ModelSkeletalClip &clip, QString *error)
{
	if (!modelClipHasStack(clip))
		return clip.base.isEmpty() || fail(error, Text::tr("Clip %1 keeps base samples without constraints or layers.").arg(clip.name));
	if (clip.constraints.size() > modelClipMaxConstraints || clip.layers.size() > modelClipMaxLayers)
		return fail(error, Text::tr("A clip holds at most 64 constraints and 16 layers."));
	if (clip.base.size() != clip.frames.size())
		return fail(error, Text::tr("Clip %1's base samples do not match its frames.").arg(clip.name));
	for (const auto &pose : clip.base)
		if (pose.size() != skeleton.joints.size())
			return fail(error, Text::tr("Clip %1's base samples do not match its joints.").arg(clip.name));
	const int frames = int(clip.frames.size());
	for (const auto &c : clip.constraints)
	{
		const auto title = c.name.isEmpty() ? modelClipConstraintKindId(c.kind) : c.name;
		const int joint = modelJointIndex(skeleton, c.joint, Qt::CaseSensitive);
		if (int(c.kind) < 0 || int(c.kind) > int(ModelClipConstraintKind::FootLock) || joint < 0 || c.name.size() > 63)
			return fail(error, Text::tr("Constraint %1 needs an existing joint by its exact name.").arg(title));
		const bool chain = c.kind == ModelClipConstraintKind::Ik || c.kind == ModelClipConstraintKind::FootLock;
		const bool follows = c.kind == ModelClipConstraintKind::ChildOf || c.kind == ModelClipConstraintKind::CopyTransform;
		if (c.first < 0 || c.first >= frames || (c.last >= 0 && (c.last < c.first || c.last >= frames)))
			return fail(error, Text::tr("Constraint %1 needs a sample range inside clip %2.").arg(title, clip.name));
		if (!std::isfinite(c.weight) || c.weight < 0 || c.weight > 1 || c.axis < 0 || c.axis > 5 || !finite(c.point) || !finite(c.pole))
			return fail(error, Text::tr("Constraint %1 needs a weight from 0 to 1, an aim axis and finite points.").arg(title));
		if ((!chain && (!c.rootJoint.isEmpty() || c.limits)) || (follows && c.target.isEmpty()) ||
			(c.kind == ModelClipConstraintKind::FootLock && !c.target.isEmpty()))
			return fail(error, Text::tr("Constraint %1 has settings its kind does not use, or lacks a target joint.").arg(title));
		QString weightProblem;
		if (!validModelWeightKeys(c.weightKeys, frames, &weightProblem))
			return fail(error, Text::tr("Constraint %1: %2").arg(title, weightProblem));
		if (chain)
		{
			int root = c.rootJoint.isEmpty() ? -1 : modelJointIndex(skeleton, c.rootJoint, Qt::CaseSensitive);
			if (root < 0 && c.rootJoint.isEmpty() && skeleton.joints[joint].parent >= 0)
				root = skeleton.joints[skeleton.joints[joint].parent].parent;
			int depth = 0;
			bool found = false;
			for (int j = joint; j >= 0 && !found; j = skeleton.joints[j].parent, ++depth)
				found = j == root;
			if (root < 0 || !found || depth < 3)
				return fail(error, Text::tr("Constraint %1 needs a chain root two or more bones above %2.").arg(title, c.joint));
		}
		if (!c.target.isEmpty())
		{
			const int target = modelJointIndex(skeleton, c.target, Qt::CaseSensitive);
			if (target < 0)
				return fail(error, Text::tr("Constraint %1 follows %2, which is not a joint of this rig.").arg(title, c.target));
			if (modelClipConstraintAffectedJoints(skeleton, c)[target])
				return fail(error, Text::tr("Constraint %1 would follow %2, which it moves itself. Choose a target outside the constrained "
											"branch.")
									   .arg(title, c.target));
		}
	}
	for (const auto &l : clip.layers)
	{
		if (!std::isfinite(l.weight) || l.weight < 0 || l.weight > 1 || l.name.size() > 63 || l.curves.isEmpty())
			return fail(error, Text::tr("Layer %1 needs a weight from 0 to 1 and at least one keyed joint.").arg(l.name));
		QString weightProblem;
		if (!validModelWeightKeys(l.weightKeys, frames, &weightProblem))
			return fail(error, Text::tr("Layer %1: %2").arg(l.name, weightProblem));
		QSet<QString> joints;
		for (const auto &group : l.curves)
		{
			QString problem;
			if (joints.contains(group.joint) || modelJointIndex(skeleton, group.joint, Qt::CaseSensitive) < 0 || !group.rotation.isEmpty() ||
				!validateModelCurveGroup(group, frames, &problem))
				return fail(error, Text::tr("Layer %1 keys joint %2 twice, keys a joint this rig lacks, or has invalid keys. %3")
									   .arg(l.name, group.joint, problem));
			joints.insert(group.joint);
		}
	}
	return true;
}
bool evaluateModelClipStack(const ModelSkeleton &skeleton, const ModelSkeletalClip &clip, const QVector<QVector<ModelJointMatrix>> &base,
							QVector<QVector<ModelJointMatrix>> *frames, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!frames)
		return false;
	auto poses = base;
	for (const auto &l : clip.layers)
		for (int f = 0; f < poses.size(); ++f)
		{
			if (!work.step())
				return false;
			if (!layer(skeleton, l, f, &poses[f], error))
				return false;
		}
	for (const auto &c : clip.constraints)
		if (!constrain(skeleton, c, &poses, error, work))
			return false;
	*frames = std::move(poses);
	return work.check();
}
bool validateModelClipStacks(const ModelMesh &mesh, QString *error, const ModelWorkControl &control)
{
	for (const auto &clip : mesh.skeleton.clips)
	{
		if (!modelClipHasStack(clip) && clip.base.isEmpty())
			continue;
		if (!validModelClipStack(mesh.skeleton, clip, error))
			return false;
		QVector<Pose> evaluated;
		if (!evaluateModelClipStack(mesh.skeleton, clip, clip.base, &evaluated, error, control))
			return false;
		for (int f = 0; f < evaluated.size(); ++f)
			for (int j = 0; j < evaluated[f].size(); ++j)
				if (!nearPose(clip.frames[f][j], evaluated[f][j]))
					return fail(error, Text::tr("Clip %1's samples no longer match its constraints and layers at sample %2, joint %3. Bake its "
												"stack before changing samples directly.")
										   .arg(clip.name)
										   .arg(f)
										   .arg(mesh.skeleton.joints[j].name));
	}
	return true;
}
bool rebaseModelClipStack(const ModelSkeleton &skeleton, ModelSkeletalClip *clip, const QVector<QVector<ModelJointMatrix>> &samples, QString *error,
						  const ModelWorkControl &control)
{
	if (!clip || samples.size() != clip->frames.size())
		return fail(error, Text::tr("The clip's base samples must match its frames."));
	QVector<Pose> evaluated;
	if (!evaluateModelClipStack(skeleton, *clip, samples, &evaluated, error, control))
		return false;
	clip->base = samples;
	clip->frames = std::move(evaluated);
	return true;
}
bool applyModelClipStack(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!mesh || !work.check())
		return false;
	const auto &o = edit.animation.stack;
	if (edit.kind != ModelEditKind::EditClipStack || !defaultModelSkeletalOptions(edit.skeletal))
		return fail(error, Text::tr("Constraint and layer edits accept only their own clip, constraint and layer settings."));
	if (o.clip < 0 || o.clip >= mesh->skeleton.clips.size())
		return fail(error, Text::tr("Choose an existing skeletal clip."));
	if (!validateModelSkeleton(*mesh, error, control))
		return false;
	const bool constraintAction = o.action == ModelClipStackAction::AddConstraint || o.action == ModelClipStackAction::UpdateConstraint ||
								  o.action == ModelClipStackAction::RemoveConstraint;
	const bool layerAction =
		o.action == ModelClipStackAction::AddLayer || o.action == ModelClipStackAction::UpdateLayer || o.action == ModelClipStackAction::RemoveLayer;
	const bool add = o.action == ModelClipStackAction::AddConstraint || o.action == ModelClipStackAction::AddLayer;
	const bool update = o.action == ModelClipStackAction::UpdateConstraint || o.action == ModelClipStackAction::UpdateLayer;
	if ((!constraintAction && !(o.constraint == ModelClipConstraint{})) || (!layerAction && !(o.layer == ModelClipLayer{})) ||
		((add || o.action == ModelClipStackAction::Bake) && o.index != -1) ||
		(o.action == ModelClipStackAction::RemoveConstraint && !(o.constraint == ModelClipConstraint{})) ||
		(o.action == ModelClipStackAction::RemoveLayer && !(o.layer == ModelClipLayer{})))
		return fail(error, Text::tr("The constraint and layer settings do not match the chosen operation."));
	auto &clip = mesh->skeleton.clips[o.clip];
	const bool had = modelClipHasStack(clip);
	// The base the stack applies to: the clip's current samples when it has none.
	const auto base = had ? clip.base : clip.frames;
	if (o.action == ModelClipStackAction::Bake)
	{
		if (!had)
			return fail(error, Text::tr("Clip %1 has no constraints or layers to bake.").arg(clip.name));
		// Curves describe the base; a joint the stack changed keeps its samples
		// and loses its curves, as Bake Joint Curves does.
		QVector<ModelPoseCurves> kept;
		for (const auto &group : clip.curves)
		{
			const int joint = modelJointIndex(mesh->skeleton, group.joint, Qt::CaseSensitive);
			bool same = true;
			for (int f = 0; f < clip.frames.size() && same; ++f)
				same = nearPose(local(mesh->skeleton, clip.frames[f], joint), local(mesh->skeleton, clip.base[f], joint));
			if (same)
				kept.append(group);
		}
		clip.curves = kept;
		clip.constraints.clear();
		clip.layers.clear();
		clip.base.clear();
		return work.check();
	}
	auto constraints = clip.constraints;
	auto layers = clip.layers;
	const int count = constraintAction ? int(constraints.size()) : int(layers.size());
	if (!add && (o.index < 0 || o.index >= count))
		return fail(error, Text::tr("Choose an existing constraint or layer by its index."));
	if (o.action == ModelClipStackAction::AddConstraint)
		constraints.append(o.constraint);
	else if (o.action == ModelClipStackAction::UpdateConstraint)
		constraints[o.index] = o.constraint;
	else if (o.action == ModelClipStackAction::RemoveConstraint)
		constraints.removeAt(o.index);
	else if (o.action == ModelClipStackAction::AddLayer)
		layers.append(o.layer);
	else if (update)
		layers[o.index] = o.layer;
	else
		layers.removeAt(o.index);
	clip.constraints = constraints;
	clip.layers = layers;
	if (!modelClipHasStack(clip))
	{
		// The last entry went: the clip returns to its base samples.
		clip.base.clear();
		return replaceModelSkeletalSamples(mesh, o.clip, 0, base, error, control);
	}
	clip.base = base;
	if (!validModelClipStack(mesh->skeleton, clip, error))
		return false;
	// The shared rebuild evaluates the stack over the base and re-bakes frames.
	return replaceModelSkeletalSamples(mesh, o.clip, 0, base, error, control);
}
void writeModelClipStackJson(const ModelSkeletalClip &clip, bool packed, QJsonObject *entry)
{
	if (!modelClipHasStack(clip) || !entry)
		return;
	QJsonArray constraints, layers, base;
	for (const auto &c : clip.constraints)
	{
		QJsonObject object{{QStringLiteral("kind"), modelClipConstraintKindId(c.kind)},
						   {QStringLiteral("joint"), c.joint},
						   {QStringLiteral("first"), c.first},
						   {QStringLiteral("last"), c.last},
						   {QStringLiteral("weight"), c.weight},
						   {QStringLiteral("enabled"), c.enabled}};
		if (!c.name.isEmpty())
			object.insert(QStringLiteral("name"), c.name);
		if (!c.rootJoint.isEmpty())
			object.insert(QStringLiteral("rootJoint"), c.rootJoint);
		if (!c.target.isEmpty())
			object.insert(QStringLiteral("target"), c.target);
		object.insert(QStringLiteral("point"), triple(c.point));
		object.insert(QStringLiteral("pole"), triple(c.pole));
		object.insert(QStringLiteral("usePole"), c.usePole);
		object.insert(QStringLiteral("axis"), c.axis);
		if (c.limits)
			object.insert(QStringLiteral("limits"), true);
		if (!c.weightKeys.isEmpty())
			object.insert(QStringLiteral("weightKeys"), weightKeysJson(c.weightKeys));
		constraints.append(object);
	}
	for (const auto &l : clip.layers)
	{
		QJsonObject object{{QStringLiteral("name"), l.name},
						   {QStringLiteral("additive"), l.additive},
						   {QStringLiteral("weight"), l.weight},
						   {QStringLiteral("enabled"), l.enabled},
						   {QStringLiteral("curves"), modelClipCurvesJson(l.curves)}};
		if (!l.weightKeys.isEmpty())
			object.insert(QStringLiteral("weightKeys"), weightKeysJson(l.weightKeys));
		layers.append(object);
	}
	if (!packed)
		for (const auto &pose : clip.base)
		{
			QJsonArray values;
			for (const auto &matrix : pose)
				for (float value : matrix.m)
					values.append(modelSourceNumber(value));
			base.append(values);
		}
	entry->insert(QStringLiteral("constraints"), constraints);
	entry->insert(QStringLiteral("layers"), layers);
	entry->insert(QStringLiteral("base"), packed ? packModelSourceMatrices(clip.base) : QJsonValue(base));
}
bool readModelClipStackJson(const QJsonObject &entry, int jointCount, bool packedArrays, ModelSkeletalClip *clip)
{
	const bool any = entry.contains(QStringLiteral("constraints")) || entry.contains(QStringLiteral("layers")) || entry.contains(QStringLiteral("base"));
	if (!any)
		return true;
	if (!clip || !entry.value(QStringLiteral("constraints")).isArray() || !entry.value(QStringLiteral("layers")).isArray())
		return false;
	QVector<ModelClipConstraint> constraints;
	for (const auto &value : entry.value(QStringLiteral("constraints")).toArray())
	{
		const auto object = value.toObject();
		ModelClipConstraint c;
		const int kind = modelClipConstraintKindFromId(object.value(QStringLiteral("kind")).toString());
		if (kind < 0 || !object.value(QStringLiteral("joint")).isString() || !object.value(QStringLiteral("first")).isDouble() ||
			!object.value(QStringLiteral("last")).isDouble() || !object.value(QStringLiteral("weight")).isDouble() ||
			!readTriple(object.value(QStringLiteral("point")), &c.point) || !readTriple(object.value(QStringLiteral("pole")), &c.pole))
			return false;
		c.kind = ModelClipConstraintKind(kind);
		c.name = object.value(QStringLiteral("name")).toString();
		c.joint = object.value(QStringLiteral("joint")).toString();
		c.rootJoint = object.value(QStringLiteral("rootJoint")).toString();
		c.target = object.value(QStringLiteral("target")).toString();
		c.first = object.value(QStringLiteral("first")).toInt(-1);
		c.last = object.value(QStringLiteral("last")).toInt(-2);
		c.weight = object.value(QStringLiteral("weight")).toDouble();
		c.enabled = object.value(QStringLiteral("enabled")).toBool(true);
		c.usePole = object.value(QStringLiteral("usePole")).toBool(true);
		c.axis = object.value(QStringLiteral("axis")).toInt(0);
		c.limits = object.value(QStringLiteral("limits")).toBool(false);
		if (!readWeightKeys(object.value(QStringLiteral("weightKeys")), int(clip->frames.size()), &c.weightKeys))
			return false;
		constraints.append(c);
	}
	QVector<ModelClipLayer> layers;
	for (const auto &value : entry.value(QStringLiteral("layers")).toArray())
	{
		const auto object = value.toObject();
		ModelClipLayer l;
		l.name = object.value(QStringLiteral("name")).toString();
		l.additive = object.value(QStringLiteral("additive")).toBool(true);
		l.weight = object.value(QStringLiteral("weight")).toDouble(-1);
		l.enabled = object.value(QStringLiteral("enabled")).toBool(true);
		QString problem;
		if (!modelClipCurvesFromJson(object.value(QStringLiteral("curves")), int(clip->frames.size()), &l.curves, &problem) ||
			!readWeightKeys(object.value(QStringLiteral("weightKeys")), int(clip->frames.size()), &l.weightKeys))
			return false;
		layers.append(l);
	}
	QVector<Pose> base;
	const auto baseValue = entry.value(QStringLiteral("base"));
	if (packedArrays && baseValue.isString())
	{
		if (!unpackModelSourceMatrices(baseValue, jointCount, 16LL * 1024 * 1024, &base))
			return false;
	}
	else
		for (const auto &value : baseValue.toArray())
		{
			const auto values = value.toArray();
			if (values.size() != jointCount * 12)
				return false;
			Pose pose(jointCount);
			for (int i = 0; i < values.size(); ++i)
			{
				if (!values.at(i).isDouble())
					return false;
				pose[i / 12].m[i % 12] = float(values.at(i).toDouble());
			}
			base.append(pose);
		}
	if (base.size() != clip->frames.size())
		return false;
	clip->constraints = constraints;
	clip->layers = layers;
	clip->base = base;
	return true;
}
} // namespace vibestudio
