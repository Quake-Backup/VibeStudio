#include "core/model_ik.h"
#include "core/model_animation_constraints.h"
#include "core/model_document.h"
#include "core/model_skeleton.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <numbers>

// The chain solvers follow FABRIK (A. Aristidou and J. Lasenby, "FABRIK: A fast,
// iterative solver for the Inverse Kinematics problem", Graphical Models 73(5),
// 2011) and cyclic coordinate descent (L.-C. T. Wang and C. C. Chen, IEEE
// Transactions on Robotics and Automation 7(4), 1991), implemented from those
// published descriptions; see docs/CREDITS.md.

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelIk)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
// Double precision construction; retained poses use the normal float format.
struct V
{
	double x = 0, y = 0, z = 0;
	V operator+(V b) const
	{
		return {x + b.x, y + b.y, z + b.z};
	}
	V operator-(V b) const
	{
		return {x - b.x, y - b.y, z - b.z};
	}
	V operator*(double n) const
	{
		return {x * n, y * n, z * n};
	}
	double dot(V b) const
	{
		return x * b.x + y * b.y + z * b.z;
	}
	V cross(V b) const
	{
		return {y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x};
	}
	double length() const
	{
		return std::sqrt(dot(*this));
	}
	V unit() const
	{
		return *this * (1 / length());
	}
	ModelVec3 vec() const
	{
		return {float(x), float(y), float(z)};
	}
};
V v(ModelVec3 p)
{
	return {p.x, p.y, p.z};
}
V origin(const ModelJointMatrix &m)
{
	return v(modelJointTranslation(m));
}
bool finite(ModelVec3 p)
{
	return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::abs(p.x) <= 1000000 && std::abs(p.y) <= 1000000 &&
		   std::abs(p.z) <= 1000000;
}
double tolerance(double a, double b)
{
	return std::max(0.00001, (a + b) * 0.000002);
}
// Chain solves converge iteratively: accept ten parts per million of the
// chain's length, at least 0.0001 units, unless a tolerance is given.
double chainTolerance(const ModelIkOptions &o, double total)
{
	return o.tolerance > 0 ? o.tolerance : std::max(0.0001, total * 0.00001);
}
int endIndex(const ModelMesh &mesh, const ModelIkOptions &o)
{
	const int end = modelJointIndex(mesh.skeleton, o.endJoint, Qt::CaseSensitive);
	if (end < 0 || o.clip < 0 || o.clip >= mesh.skeleton.clips.size() || o.first < 0 || o.last < o.first ||
		o.last >= mesh.skeleton.clips[o.clip].frames.size())
		return -1;
	const int middle = mesh.skeleton.joints[end].parent;
	if (middle < 0 || middle >= end || mesh.skeleton.joints[middle].parent < 0 || mesh.skeleton.joints[middle].parent >= middle)
		return -1;
	return end;
}
// The joints from `root` down to `end`, or empty when root is not an ancestor
// at least two bones above end.
QVector<int> chainJoints(const ModelSkeleton &skeleton, int root, int end)
{
	QVector<int> chain;
	for (int j = end; j >= 0; j = skeleton.joints[j].parent)
	{
		chain.prepend(j);
		if (j == root)
			return chain.size() >= 3 ? chain : QVector<int>{};
		if (chain.size() > 256)
			break;
	}
	return {};
}
QVector<bool> subtree(const ModelSkeleton &skeleton, int joint)
{
	QVector<bool> inside(skeleton.joints.size(), false);
	for (int j = joint; j < inside.size(); ++j)
		inside[j] = j == joint || (skeleton.joints[j].parent >= 0 && inside[skeleton.joints[j].parent]);
	return inside;
}
// Shortest rigid swing, with an explicit deterministic half-turn axis.
// Left-multiplication preserves the original affine basis (including scale).
ModelJointMatrix swing(V from, V to, V preferredAxis)
{
	from = from.unit();
	to = to.unit();
	const double cosine = std::clamp(from.dot(to), -1.0, 1.0);
	V axis = from.cross(to);
	const double sine = axis.length();
	if (sine < 1e-12)
	{
		if (cosine > 0)
			return modelJointIdentity();
		axis = preferredAxis - from * preferredAxis.dot(from);
		if (axis.length() < 1e-12)
		{
			const V basis = std::abs(from.x) <= std::abs(from.y) && std::abs(from.x) <= std::abs(from.z) ? V{1, 0, 0}
							: std::abs(from.y) <= std::abs(from.z)										 ? V{0, 1, 0}
																										 : V{0, 0, 1};
			axis = from.cross(basis);
		}
	}
	axis = axis.unit();
	const double q[3] = {axis.x, axis.y, axis.z};
	const double skew[3][3] = {{0, -axis.z, axis.y}, {axis.z, 0, -axis.x}, {-axis.y, axis.x, 0}};
	ModelJointMatrix result;
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c)
			result.m[r * 4 + c] = float((r == c ? cosine : 0) + (1 - cosine) * q[r] * q[c] + sine * skew[r][c]);
	return result;
}
ModelJointMatrix moved(const ModelJointMatrix &matrix, const ModelJointMatrix &rotation, V from, V to)
{
	auto result = modelJointMultiply(rotation, matrix);
	const auto position = to + v(modelJointTransformVector(rotation, (origin(matrix) - from).vec()));
	result.m[3] = float(position.x);
	result.m[7] = float(position.y);
	result.m[11] = float(position.z);
	return result;
}
bool solve(const ModelSkeleton &skeleton, QVector<ModelJointMatrix> *pose, int end, const ModelIkOptions &o, V target, QString *error,
		   ModelWorkProgress &work)
{
	const int middle = skeleton.joints[end].parent, root = skeleton.joints[middle].parent;
	const auto before = *pose;
	const V a = origin(before[root]), b = origin(before[middle]), c = origin(before[end]), pole = v(o.pole);
	const double upper = (b - a).length(), lower = (c - b).length(), distance = (target - a).length();
	if (!std::isfinite(upper + lower + distance) || upper <= 1e-6 || lower <= 1e-6)
		return fail(error, Text::tr("Inverse kinematics needs two nonzero bone lengths in every selected sample."));
	const double tol = tolerance(upper, lower), low = std::abs(upper - lower), high = upper + lower;
	if (!o.nearestReachable && (distance < low - tol || distance > high + tol))
		return fail(error, Text::tr("The target is outside this sample's reach (%1–%2 model units). Choose nearest reachable to clamp it.")
							   .arg(low, 0, 'g', 9)
							   .arg(high, 0, 'g', 9));
	const double reach = std::clamp(distance, low, high);
	V elbow, tip, normal;
	if (reach < 1e-12)
	{
		if ((pole - a).length() < 1e-6)
			return fail(error, Text::tr("The pole must differ from the root for a completely folded chain."));
		elbow = a + (pole - a).unit() * upper;
		tip = a;
		normal = (b - a).cross(elbow - a);
	}
	else
	{
		V axis = target - a;
		if (axis.length() < 1e-12)
			axis = (c - a).length() > 1e-12 ? c - a : b - a;
		axis = axis.unit();
		const double along = (upper * upper - lower * lower + reach * reach) / (2 * reach);
		const double height = std::sqrt(std::max(0.0, upper * upper - along * along));
		V bend = (pole - a) - axis * (pole - a).dot(axis);
		if (height > tol && bend.length() < 1e-6)
			return fail(error, Text::tr("The pole lies on the root-to-target line. Move it off the line to choose a bend direction."));
		bend = bend.length() > 1e-12 ? bend.unit() : V{};
		elbow = a + axis * along + bend * height;
		tip = a + axis * reach;
		normal = axis.cross(bend);
	}
	// Repeating a solved positional constraint must not accumulate float noise.
	if ((elbow - b).length() <= tol && (tip - c).length() <= tol)
		return true;
	const auto upperRotation = swing(b - a, elbow - a, normal), lowerRotation = swing(c - b, tip - elbow, normal);
	const auto endRotation = o.keepEndOrientation ? modelJointIdentity() : lowerRotation;
	QVector<int> anchors(skeleton.joints.size(), -1);
	for (int j = 0; j < pose->size(); ++j)
	{
		if (!work.step())
			return false;
		if (j == root || j == middle || j == end)
			anchors[j] = j;
		else if (skeleton.joints[j].parent >= 0)
			anchors[j] = anchors[skeleton.joints[j].parent];
		const int anchor = anchors[j];
		if (anchor < 0)
			continue;
		(*pose)[j] = anchor == root		? moved(before[j], upperRotation, a, a)
					 : anchor == middle ? moved(before[j], lowerRotation, b, elbow)
										: moved(before[j], endRotation, c, tip);
	}
	const auto actualMiddle = origin((*pose)[middle]), actualTip = origin((*pose)[end]);
	if (std::abs((actualMiddle - a).length() - upper) > tol || std::abs((actualTip - actualMiddle).length() - lower) > tol ||
		(actualTip - tip).length() > tol)
		return fail(
			error,
			Text::tr("The solved pose cannot preserve bone lengths at this coordinate precision. Move the model closer to its origin."));
	return true;
}

// --- Joint limits ----------------------------------------------------------

// X-then-Y-then-Z Euler degrees of a rotation (R = Rz Ry Rx), the curve
// channels' convention, with Y in [-90, 90].
std::array<double, 3> eulerDegrees(const ModelQuat &q)
{
	const double x = q.x, y = q.y, z = q.z, w = q.w;
	const double r00 = 1 - 2 * (y * y + z * z), r10 = 2 * (x * y + w * z), r20 = 2 * (x * z - w * y);
	const double r21 = 2 * (y * z + w * x), r22 = 1 - 2 * (x * x + y * y), r11 = 1 - 2 * (x * x + z * z), r12 = 2 * (y * z - w * x);
	const double degrees = 180 / std::numbers::pi;
	const double pitch = std::asin(std::clamp(-r20, -1.0, 1.0));
	if (std::cos(pitch) > 1e-7)
		return {std::atan2(r21, r22) * degrees, pitch * degrees, std::atan2(r10, r00) * degrees};
	return {std::atan2(-r12, r11) * degrees, pitch * degrees, 0};
}
ModelQuat quatFromEuler(const std::array<double, 3> &angles)
{
	const double half = std::numbers::pi / 360;
	const auto axis = [half](double degrees, int index)
	{
		ModelQuat result{0, 0, 0, float(std::cos(degrees * half))};
		const float s = float(std::sin(degrees * half));
		(index == 0 ? result.x : index == 1 ? result.y : result.z) = s;
		return result;
	};
	return modelQuatNormalized(modelQuatMultiply(axis(angles[2], 2), modelQuatMultiply(axis(angles[1], 1), axis(angles[0], 0))));
}
ModelJointMatrix bindLocal(const ModelSkeleton &skeleton, int joint)
{
	const int parent = skeleton.joints[joint].parent;
	return parent < 0 ? skeleton.joints[joint].bind
					  : modelJointMultiply(modelJointInverse(skeleton.joints[parent].bind), skeleton.joints[joint].bind);
}
ModelJointMatrix localPose(const ModelSkeleton &skeleton, const QVector<ModelJointMatrix> &pose, int joint)
{
	const int parent = skeleton.joints[joint].parent;
	return parent < 0 ? pose[joint] : modelJointMultiply(modelJointInverse(pose[parent]), pose[joint]);
}
// Swing-twist limits: the relative rotation q = swing * twist, where twist
// turns about the joint's own axis `twistAxis` and swing moves that axis. The
// coordinates are the twist angle on the twist axis and the swing's rotation
// vector (degrees) on the other two.
ModelQuat aboutAxis(int axis, double degrees)
{
	const double half = degrees * std::numbers::pi / 360;
	ModelQuat result{0, 0, 0, float(std::cos(half))};
	(axis == 0 ? result.x : axis == 1 ? result.y : result.z) = float(std::sin(half));
	return result;
}
std::array<double, 3> swingTwistDegrees(const ModelQuat &rotation, int axis)
{
	double q[4]{rotation.x, rotation.y, rotation.z, rotation.w};
	if (q[3] < 0)
		for (double &value : q)
			value = -value;
	const double degrees = 180 / std::numbers::pi;
	std::array<double, 3> result{};
	const double twistLength = std::sqrt(q[axis] * q[axis] + q[3] * q[3]);
	double twist[4]{0, 0, 0, 1};
	if (twistLength > 1e-12)
	{
		twist[axis] = q[axis] / twistLength;
		twist[3] = q[3] / twistLength;
		result[axis] = 2 * std::atan2(twist[axis], twist[3]) * degrees;
	}
	// swing = q * conjugate(twist)
	const ModelQuat swing = modelQuatMultiply(rotation.w < 0 ? ModelQuat{-rotation.x, -rotation.y, -rotation.z, -rotation.w} : rotation,
											  ModelQuat{float(-twist[0]), float(-twist[1]), float(-twist[2]), float(twist[3])});
	double s[4]{swing.x, swing.y, swing.z, swing.w};
	if (s[3] < 0)
		for (double &value : s)
			value = -value;
	const double length = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
	if (length > 1e-12)
	{
		const double angle = 2 * std::atan2(length, s[3]) * degrees;
		for (int i = 0; i < 3; ++i)
			if (i != axis)
				result[i] = s[i] / length * angle;
	}
	return result;
}
ModelQuat quatFromSwingTwist(const std::array<double, 3> &coordinates, int axis)
{
	double vector[3]{};
	double angle = 0;
	for (int i = 0; i < 3; ++i)
		if (i != axis)
		{
			vector[i] = coordinates[i];
			angle += coordinates[i] * coordinates[i];
		}
	angle = std::sqrt(angle);
	ModelQuat swing{};
	if (angle > 1e-12)
	{
		const double half = angle * std::numbers::pi / 360, s = std::sin(half) / angle;
		swing = {float(vector[0] * s), float(vector[1] * s), float(vector[2] * s), float(std::cos(half))};
	}
	return modelQuatNormalized(modelQuatMultiply(swing, aboutAxis(axis, coordinates[axis])));
}
// The joint's limit coordinates for its mode: Euler degrees, or swing-twist.
std::array<double, 3> limitCoordinates(const ModelJointLimits &limits, const ModelQuat &relative)
{
	return limits.swingTwist ? swingTwistDegrees(relative, limits.twistAxis) : eulerDegrees(relative);
}
ModelQuat limitRotation(const ModelJointLimits &limits, const std::array<double, 3> &coordinates)
{
	return limits.swingTwist ? quatFromSwingTwist(coordinates, limits.twistAxis) : quatFromEuler(coordinates);
}
// The nearest coordinates inside the limits: Euler bounds clamp each axis; the
// swing scales towards zero onto its ellipse (an axis with no room keeps zero).
std::array<double, 3> clampLimitCoordinates(const ModelJointLimits &limits, std::array<double, 3> c)
{
	if (!limits.swingTwist)
	{
		for (int i = 0; i < 3; ++i)
			c[i] = std::clamp(c[i], limits.minimum[i], limits.maximum[i]);
		return c;
	}
	const int t = limits.twistAxis, u = (t + 1) % 3, v = (t + 2) % 3;
	c[t] = std::clamp(c[t], limits.minimum[t], limits.maximum[t]);
	const double a = c[u] >= 0 ? limits.maximum[u] : -limits.minimum[u];
	const double b = c[v] >= 0 ? limits.maximum[v] : -limits.minimum[v];
	if (a <= 0)
		c[u] = 0;
	if (b <= 0)
		c[v] = 0;
	if (a > 0 && b > 0)
	{
		const double r = (c[u] / a) * (c[u] / a) + (c[v] / b) * (c[v] / b);
		if (r > 1)
		{
			c[u] /= std::sqrt(r);
			c[v] /= std::sqrt(r);
		}
	}
	return c;
}
bool rigid(const ModelJointMatrix &matrix)
{
	const auto rebuilt = modelJointMatrix(modelJointRotation(matrix), modelJointTranslation(matrix), modelJointScale(matrix));
	double largest = 1;
	for (float value : matrix.m)
		largest = std::max(largest, double(std::abs(value)));
	return modelJointMatrixDistance(matrix, rebuilt) <= .0001 * largest;
}
// Turns the joint back inside its limits, carrying its subtree.
bool clampJoint(const ModelSkeleton &skeleton, QVector<ModelJointMatrix> *pose, int joint, const QVector<bool> &inside, QString *error)
{
	const auto &limits = skeleton.joints[joint].limits;
	if (!limits.enabled)
		return true;
	const auto local = localPose(skeleton, *pose, joint);
	const auto bind = modelJointRotation(bindLocal(skeleton, joint));
	const auto relative = modelQuatMultiply(modelQuatConjugate(bind), modelJointRotation(local));
	const auto angles = limitCoordinates(limits, relative);
	const auto clamped = clampLimitCoordinates(limits, angles);
	bool changed = false;
	for (int i = 0; i < 3; ++i)
		changed = changed || std::abs(clamped[i] - angles[i]) > 1e-9;
	if (!changed)
		return true;
	if (!rigid(local))
		return fail(error, Text::tr("Joint %1 has shear, so its limits cannot be applied.").arg(skeleton.joints[joint].name));
	const auto rotation = modelQuatMultiply(bind, limitRotation(limits, clamped));
	const auto updatedLocal = modelJointMatrix(rotation, modelJointTranslation(local), modelJointScale(local));
	const int parent = skeleton.joints[joint].parent;
	const auto updated = parent < 0 ? updatedLocal : modelJointMultiply((*pose)[parent], updatedLocal);
	bool ok = false;
	const auto delta = modelJointMultiply(updated, modelJointInverse((*pose)[joint], &ok));
	if (!ok)
		return fail(error, Text::tr("Joint %1 has a singular pose.").arg(skeleton.joints[joint].name));
	for (int j = joint; j < pose->size(); ++j)
		if (inside[j])
			(*pose)[j] = j == joint ? updated : modelJointMultiply(delta, (*pose)[j]);
	return true;
}

// --- Chain solvers ---------------------------------------------------------

struct Chain
{
	QVector<int> joints;
	QVector<double> lengths;
	QVector<QVector<bool>> inside;
	double total = 0;
};
// Places the chain's joint origins at `points` by swinging each bone in turn;
// subtrees follow rigidly.
void place(const ModelSkeleton &, QVector<ModelJointMatrix> *pose, const Chain &chain, const QVector<V> &points, V hint)
{
	for (int k = 0; k + 1 < chain.joints.size(); ++k)
	{
		const V a = origin((*pose)[chain.joints[k]]), b = origin((*pose)[chain.joints[k + 1]]);
		const V from = b - a, to = points[k + 1] - points[k];
		if (from.length() < 1e-12 || to.length() < 1e-12)
			continue;
		const auto rotation = swing(from, to, hint);
		for (int j = chain.joints[k]; j < pose->size(); ++j)
			if (chain.inside[k][j])
				(*pose)[j] = moved((*pose)[j], rotation, a, points[k]);
	}
}
QVector<V> origins(const QVector<ModelJointMatrix> &pose, const Chain &chain)
{
	QVector<V> points;
	for (int j : chain.joints)
		points.append(origin(pose[j]));
	return points;
}
// Turns interior joints about the line through their neighbours towards the pole.
void bendTowards(QVector<V> *points, V pole)
{
	for (int k = 1; k + 1 < points->size(); ++k)
	{
		const V a = (*points)[k - 1], c = (*points)[k + 1];
		V axis = c - a;
		if (axis.length() < 1e-12)
			continue;
		axis = axis.unit();
		const V offset = (*points)[k] - a;
		const double along = offset.dot(axis);
		const V radial = offset - axis * along;
		const V wanted = (pole - a) - axis * (pole - a).dot(axis);
		if (radial.length() < 1e-12 || wanted.length() < 1e-12)
			continue;
		(*points)[k] = a + axis * along + wanted.unit() * radial.length();
	}
}
bool solveChain(const ModelSkeleton &skeleton, QVector<ModelJointMatrix> *pose, const Chain &chain, const ModelIkOptions &o, V target,
				QString *error, ModelWorkProgress &work)
{
	const auto before = *pose;
	const int n = int(chain.joints.size()) - 1;
	const V root = origin(before[chain.joints[0]]);
	const double tol = chainTolerance(o, chain.total);
	double longest = 0;
	for (double length : chain.lengths)
		longest = std::max(longest, length);
	const double distance = (target - root).length(), low = std::max(0.0, 2 * longest - chain.total);
	if (!o.nearestReachable && (distance > chain.total + tol || distance < low - tol))
		return fail(error, Text::tr("The target is outside this sample's reach (%1–%2 model units). Choose nearest reachable to clamp it.")
							   .arg(low, 0, 'g', 9)
							   .arg(chain.total, 0, 'g', 9));
	const V pole = v(o.pole);
	const bool usePole = o.usePole && o.solver == ModelIkSolver::Fabrik;
	auto points = origins(*pose, chain);
	// A straight chain has no bend to start from: lean its interior joints
	// slightly towards the pole (or a fixed perpendicular) so FABRIK can fold it.
	{
		V axis = points[n] - points[0];
		if (axis.length() > 1e-12)
		{
			axis = axis.unit();
			bool straight = true;
			for (int k = 1; k < n && straight; ++k)
			{
				const V offset = points[k] - points[0];
				straight = (offset - axis * offset.dot(axis)).length() <= 1e-6 * chain.total;
			}
			if (straight)
			{
				V side = usePole ? (pole - points[0]) - axis * (pole - points[0]).dot(axis) : V{};
				if (side.length() < 1e-12)
				{
					const V basis = std::abs(axis.x) <= std::abs(axis.y) && std::abs(axis.x) <= std::abs(axis.z) ? V{1, 0, 0}
									: std::abs(axis.y) <= std::abs(axis.z)										 ? V{0, 1, 0}
																												 : V{0, 0, 1};
					side = basis - axis * basis.dot(axis);
				}
				side = side.unit();
				for (int k = 1; k < n; ++k)
					points[k] = points[k] + side * (chain.lengths[k - 1] * 1e-3);
				for (int k = 0; k < n; ++k)
					points[k + 1] = points[k] + (points[k + 1] - points[k]).unit() * chain.lengths[k];
			}
		}
	}
	const V hint = usePole ? pole - root : V{0, 0, 1};
	double error2 = (points[n] - target).length();
	for (int iteration = 0; iteration < o.iterations && (error2 > tol || iteration == 0); ++iteration)
	{
		if (!work.step())
			return false;
		if (o.solver == ModelIkSolver::Fabrik)
		{
			points[n] = target;
			for (int k = n - 1; k >= 0; --k)
			{
				V direction = points[k] - points[k + 1];
				if (direction.length() < 1e-12)
					direction = origin(before[chain.joints[k]]) - origin(before[chain.joints[k + 1]]);
				points[k] = points[k + 1] + direction.unit() * chain.lengths[k];
			}
			points[0] = root;
			for (int k = 0; k < n; ++k)
			{
				V direction = points[k + 1] - points[k];
				if (direction.length() < 1e-12)
					direction = origin(before[chain.joints[k + 1]]) - origin(before[chain.joints[k]]);
				points[k + 1] = points[k] + direction.unit() * chain.lengths[k];
			}
			if (usePole)
				bendTowards(&points, pole);
			if (o.limits)
			{
				place(skeleton, pose, chain, points, hint);
				for (int k = 0; k < n; ++k)
					if (!clampJoint(skeleton, pose, chain.joints[k], chain.inside[k], error))
						return false;
				points = origins(*pose, chain);
			}
		}
		else
		{
			if (iteration == 0)
				place(skeleton, pose, chain, points, hint);
			for (int k = n - 1; k >= 0; --k)
			{
				const V a = origin((*pose)[chain.joints[k]]), end = origin((*pose)[chain.joints[n]]);
				if ((end - a).length() < 1e-12 || (target - a).length() < 1e-12)
					continue;
				const auto rotation = swing(end - a, target - a, hint);
				for (int j = chain.joints[k]; j < pose->size(); ++j)
					if (chain.inside[k][j])
						(*pose)[j] = moved((*pose)[j], rotation, a, a);
				if (o.limits && !clampJoint(skeleton, pose, chain.joints[k], chain.inside[k], error))
					return false;
			}
			points = origins(*pose, chain);
		}
		error2 = (points[n] - target).length();
	}
	if (o.solver == ModelIkSolver::Fabrik && !o.limits)
		place(skeleton, pose, chain, points, hint);
	const auto solved = origins(*pose, chain);
	error2 = (solved[n] - target).length();
	if (error2 > tol && !o.nearestReachable)
		return fail(error, o.limits ? Text::tr("The target cannot be reached within the joint limits (%1 model units away). Choose nearest "
											   "reachable to accept the closest pose.")
										  .arg(error2, 0, 'g', 9)
									: Text::tr("The chain did not reach the target within %n iteration(s) (%1 model units away). Raise the "
											   "iterations or choose nearest reachable.",
											   nullptr, o.iterations)
										  .arg(error2, 0, 'g', 9));
	for (int k = 0; k < n; ++k)
		if (std::abs((solved[k + 1] - solved[k]).length() - chain.lengths[k]) > std::max(tol, chain.lengths[k] * 1e-5))
			return fail(error, Text::tr("The solved pose cannot preserve bone lengths at this coordinate precision. Move the model closer to "
										"its origin."));
	if (o.keepEndOrientation)
	{
		const V end = origin(before[chain.joints[n]]);
		for (int j = chain.joints[n]; j < pose->size(); ++j)
			if (chain.inside[n][j])
				(*pose)[j] = moved(before[j], modelJointIdentity(), end, solved[n]);
	}
	return true;
}
struct Resolved
{
	Chain chain;
	int end = -1;
	int follow = -1;
};
bool resolve(const ModelMesh &mesh, const ModelIkOptions &o, bool chained, Resolved *result, QString *error)
{
	const auto &skeleton = mesh.skeleton;
	Resolved r;
	if (chained)
	{
		r.end = modelJointIndex(skeleton, o.endJoint, Qt::CaseSensitive);
		const int root = modelJointIndex(skeleton, o.rootJoint, Qt::CaseSensitive);
		if (r.end < 0 || root < 0)
			return fail(error, Text::tr("Choose the chain's root and end joints by their exact names."));
		r.chain.joints = chainJoints(skeleton, root, r.end);
		if (r.chain.joints.isEmpty())
			return fail(error, Text::tr("The root joint must be an ancestor of the end joint, at least two bones and at most 256 joints up."));
		if (o.clip < 0 || o.clip >= skeleton.clips.size() || o.first < 0 || o.last < o.first || o.last >= skeleton.clips[o.clip].frames.size())
			return fail(error, Text::tr("Choose a retained clip and a valid inclusive sample range."));
	}
	else
	{
		r.end = endIndex(mesh, o);
		if (r.end < 0)
			return fail(error,
						Text::tr("Choose a retained clip, a valid inclusive sample range and an end joint with a parent and grandparent."));
		const int middle = skeleton.joints[r.end].parent;
		r.chain.joints = {skeleton.joints[middle].parent, middle, r.end};
	}
	for (int j : r.chain.joints)
		r.chain.inside.append(subtree(skeleton, j));
	if (!finite(o.target) || !finite(o.pole) || !finite(o.targetOffset))
		return fail(error, Text::tr("Target, pole and offset coordinates must be finite and within one million model units."));
	if (!o.targets.isEmpty() && (!o.targetJoint.isEmpty() || o.targets.size() != o.last - o.first + 1))
		return fail(error, Text::tr("Per-sample targets need one point for every sample from first to last, without a followed joint."));
	for (const auto &point : o.targets)
		if (!finite(point))
			return fail(error, Text::tr("Target, pole and offset coordinates must be finite and within one million model units."));
	if (!o.targetJoint.isEmpty())
	{
		r.follow = modelJointIndex(skeleton, o.targetJoint, Qt::CaseSensitive);
		if (r.follow < 0)
			return fail(error, Text::tr("Choose the followed target joint by its exact name."));
		if (r.chain.inside[0][r.follow])
			return fail(error, Text::tr("The target joint %1 moves with the chain it would steer. Follow a joint outside the chain's branch.")
								   .arg(o.targetJoint));
	}
	else if (o.targetOffset.x != 0 || o.targetOffset.y != 0 || o.targetOffset.z != 0)
		return fail(error, Text::tr("A target offset needs a followed target joint."));
	if (!o.targetKeys.isEmpty() && (!o.targetJoint.isEmpty() || !o.targets.isEmpty()))
		return fail(error, Text::tr("Keyed targets cannot be combined with a followed joint or per-sample targets."));
	const int frames = int(skeleton.clips[o.clip].frames.size());
	for (const auto *keys : {&o.targetKeys, &o.poleKeys})
	{
		int previous = -1;
		if (keys->size() > frames)
			return fail(error, Text::tr("Keyed targets and poles hold at most one key per clip sample."));
		for (const auto &key : *keys)
		{
			if (key.sample <= previous || key.sample >= frames || !finite(key.point))
				return fail(error, Text::tr("Target and pole keys need increasing samples inside the clip and finite points within one million "
											"model units."));
			previous = key.sample;
		}
	}
	*result = std::move(r);
	return true;
}
V sampleTarget(const ModelIkOptions &o, const Resolved &r, int sample, const QVector<ModelJointMatrix> &pose)
{
	if (!o.targetKeys.isEmpty())
		return v(modelIkKeyedPoint(o.targetKeys, sample));
	if (!o.targets.isEmpty())
		return v(o.targets[sample - o.first]);
	if (r.follow >= 0)
		return v(modelJointTransformPoint(pose[r.follow], o.targetOffset));
	return v(o.target);
}
bool defaultChainSettings(const ModelIkOptions &o)
{
	return o.rootJoint.isEmpty() && o.solver == ModelIkSolver::Fabrik && o.iterations == 32 && o.tolerance == 0 && o.usePole && !o.limits;
}
} // namespace

bool ModelIkOptions::operator==(const ModelIkOptions &o) const
{
	const auto same = [](ModelVec3 a, ModelVec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
	if (targets.size() != o.targets.size() || targetKeys.size() != o.targetKeys.size() || poleKeys.size() != o.poleKeys.size())
		return false;
	for (int i = 0; i < targets.size(); ++i)
		if (!same(targets[i], o.targets[i]))
			return false;
	for (int i = 0; i < targetKeys.size(); ++i)
		if (targetKeys[i].sample != o.targetKeys[i].sample || !same(targetKeys[i].point, o.targetKeys[i].point))
			return false;
	for (int i = 0; i < poleKeys.size(); ++i)
		if (poleKeys[i].sample != o.poleKeys[i].sample || !same(poleKeys[i].point, o.poleKeys[i].point))
			return false;
	return clip == o.clip && first == o.first && last == o.last && endJoint == o.endJoint && same(target, o.target) && same(pole, o.pole) &&
		   nearestReachable == o.nearestReachable && keepEndOrientation == o.keepEndOrientation && rootJoint == o.rootJoint &&
		   solver == o.solver && iterations == o.iterations && tolerance == o.tolerance && usePole == o.usePole && limits == o.limits &&
		   targetJoint == o.targetJoint && same(targetOffset, o.targetOffset);
}
QVector<ModelIkSample> modelIkSamples(const ModelMesh &mesh, const ModelIkOptions &o, bool chained)
{
	QVector<ModelIkSample> result;
	Resolved r;
	if (!resolve(mesh, o, chained, &r, nullptr))
		return result;
	for (int f = o.first; f <= o.last; ++f)
	{
		const auto &pose = mesh.skeleton.clips[o.clip].frames[f];
		if (pose.size() != mesh.skeleton.joints.size())
			return {};
		double total = 0;
		for (int k = 0; k + 1 < r.chain.joints.size(); ++k)
			total += (origin(pose[r.chain.joints[k + 1]]) - origin(pose[r.chain.joints[k]])).length();
		const V end = origin(pose[r.end]), target = sampleTarget(o, r, f, pose);
		const double tol = chained ? chainTolerance(o, total) : tolerance(total, 0), residual = (end - target).length();
		result.append({f, end.vec(), residual, tol, residual <= tol, target.vec()});
	}
	return result;
}
bool applyModelIk(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!mesh || !work.check())
		return false;
	const auto &o = edit.ik;
	const bool chained = edit.kind == ModelEditKind::SolveChainIk;
	if ((!chained && edit.kind != ModelEditKind::SolveTwoBoneIk) || !defaultModelSkeletalOptions(edit.skeletal))
		return fail(error, Text::tr("Inverse kinematics accepts only its own chain, sample and target settings."));
	if (!chained && !defaultChainSettings(o))
		return fail(error, Text::tr("Root joint, solver, iterations, tolerance, pole use and joint limits apply only to chain inverse kinematics."));
	if (chained && (o.iterations < 1 || o.iterations > 1000 || !std::isfinite(o.tolerance) || o.tolerance < 0 || o.tolerance > 1000000 ||
					int(o.solver) < 0 || int(o.solver) > int(ModelIkSolver::Ccd)))
		return fail(error, Text::tr("Chain inverse kinematics needs 1–1000 iterations and a finite, nonnegative tolerance."));
	if (!validateModelSkeleton(*mesh, error))
		return false;
	Resolved r;
	if (!resolve(*mesh, o, chained, &r, error))
		return false;
	QVector<QVector<ModelJointMatrix>> samples;
	bool changed = false;
	for (int frame = o.first; frame <= o.last; ++frame)
	{
		if (!work.check())
			return false;
		// A clip with constraints or layers is solved on its base samples.
		const auto &editable = modelClipEditableSamples(mesh->skeleton.clips[o.clip]);
		auto pose = editable[frame];
		const V target = sampleTarget(o, r, frame, pose);
		// Keyed poles give each sample its own bend direction.
		auto sampled = o;
		if (!o.poleKeys.isEmpty())
			sampled.pole = modelIkKeyedPoint(o.poleKeys, frame);
		if (chained)
		{
			r.chain.lengths.clear();
			r.chain.total = 0;
			for (int k = 0; k + 1 < r.chain.joints.size(); ++k)
			{
				const double length = (origin(pose[r.chain.joints[k + 1]]) - origin(pose[r.chain.joints[k]])).length();
				if (!std::isfinite(length) || length <= 1e-6)
				{
					if (error)
						*error = Text::tr("Sample %1: %2")
									 .arg(frame)
									 .arg(Text::tr("Inverse kinematics needs nonzero bone lengths along the whole chain."));
					return false;
				}
				r.chain.lengths.append(length);
				r.chain.total += length;
			}
		}
		if (!(chained ? solveChain(mesh->skeleton, &pose, r.chain, sampled, target, error, work)
					  : solve(mesh->skeleton, &pose, r.end, sampled, target, error, work)))
		{
			if (error && !error->isEmpty())
				*error = Text::tr("Sample %1: %2").arg(frame).arg(*error);
			return false;
		}
		for (int j = 0; j < pose.size(); ++j)
			changed = changed || modelJointMatrixDistance(pose[j], editable[frame][j]) != 0;
		samples.append(std::move(pose));
	}
	if (!changed)
		return work.check();
	return replaceModelSkeletalSamples(mesh, o.clip, o.first, samples, error, control);
}

bool solveModelConstraintIk(const ModelSkeleton &skeleton, QVector<ModelJointMatrix> *pose, const QString &rootJoint, const QString &endJoint,
							const ModelVec3 &target, const ModelVec3 &pole, bool usePole, bool limits, QString *error)
{
	const int end = modelJointIndex(skeleton, endJoint, Qt::CaseSensitive);
	if (!pose || end < 0 || pose->size() != skeleton.joints.size())
		return fail(error, Text::tr("Choose an existing end joint."));
	int root = rootJoint.isEmpty() ? -1 : modelJointIndex(skeleton, rootJoint, Qt::CaseSensitive);
	if (rootJoint.isEmpty() && skeleton.joints[end].parent >= 0)
		root = skeleton.joints[skeleton.joints[end].parent].parent;
	Chain chain;
	chain.joints = root < 0 ? QVector<int>{} : chainJoints(skeleton, root, end);
	if (chain.joints.isEmpty())
		return fail(error, Text::tr("The chain's root must be an ancestor of %1 at least two bones up.").arg(endJoint));
	for (int j : chain.joints)
		chain.inside.append(subtree(skeleton, j));
	for (int k = 0; k + 1 < chain.joints.size(); ++k)
	{
		const double length = (origin((*pose)[chain.joints[k + 1]]) - origin((*pose)[chain.joints[k]])).length();
		if (!std::isfinite(length) || length <= 1e-6)
			return fail(error, Text::tr("Inverse kinematics needs nonzero bone lengths along the whole chain."));
		chain.lengths.append(length);
		chain.total += length;
	}
	ModelIkOptions options;
	options.nearestReachable = true;
	options.keepEndOrientation = true;
	options.iterations = 64;
	options.usePole = usePole;
	options.pole = pole;
	options.limits = limits;
	ModelWorkProgress work({}, ModelWorkPhase::Editing, error);
	return solveChain(skeleton, pose, chain, options, v(target), error, work);
}

ModelVec3 modelIkKeyedPoint(const QVector<ModelIkKey> &keys, int sample)
{
	if (keys.isEmpty())
		return {};
	if (sample <= keys.first().sample)
		return keys.first().point;
	if (sample >= keys.last().sample)
		return keys.last().point;
	const auto next = std::upper_bound(keys.cbegin(), keys.cend(), sample, [](int s, const ModelIkKey &key) { return s < key.sample; });
	const auto &b = *next, &a = *(next - 1);
	const double t = double(sample - a.sample) / double(b.sample - a.sample);
	return {float(a.point.x + (double(b.point.x) - a.point.x) * t), float(a.point.y + (double(b.point.y) - a.point.y) * t),
			float(a.point.z + (double(b.point.z) - a.point.z) * t)};
}
bool parseModelIkKeys(const QString &text, QVector<ModelIkKey> *keys, QString *error)
{
	if (!keys)
		return false;
	QVector<ModelIkKey> result;
	for (const auto &entry : text.split(QRegularExpression(QStringLiteral("[;\\s]+")), Qt::SkipEmptyParts))
	{
		const auto parts = entry.split(QLatin1Char(':'));
		const auto point = parts.value(1).split(QLatin1Char(','));
		bool ok = false;
		ModelIkKey key;
		key.sample = parts.value(0).toInt(&ok);
		ok = ok && parts.size() == 2 && point.size() == 3 && key.sample >= 0;
		double xyz[3]{};
		for (int i = 0; ok && i < 3; ++i)
		{
			xyz[i] = point[i].toDouble(&ok);
			ok = ok && std::isfinite(xyz[i]) && std::abs(xyz[i]) <= 1000000;
		}
		if (!ok)
			return fail(error, Text::tr("Write keys as sample:x,y,z entries separated by semicolons, such as 0:1,2,3; 12:4,5,6."));
		key.point = {float(xyz[0]), float(xyz[1]), float(xyz[2])};
		result.append(key);
	}
	std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.sample < b.sample; });
	for (int i = 1; i < result.size(); ++i)
		if (result[i].sample == result[i - 1].sample)
			return fail(error, Text::tr("Each sample holds at most one key."));
	*keys = std::move(result);
	return true;
}
QString modelIkKeysText(const QVector<ModelIkKey> &keys)
{
	QStringList parts;
	for (const auto &key : keys)
		parts << QStringLiteral("%1:%2,%3,%4").arg(key.sample).arg(key.point.x, 0, 'g', 9).arg(key.point.y, 0, 'g', 9).arg(key.point.z, 0, 'g', 9);
	return parts.join(QStringLiteral("; "));
}
std::array<double, 3> modelJointLimitAngles(const ModelSkeleton &skeleton, const QVector<ModelJointMatrix> &pose, int joint)
{
	if (joint < 0 || joint >= skeleton.joints.size() || pose.size() != skeleton.joints.size())
		return {};
	const auto bind = modelJointRotation(bindLocal(skeleton, joint));
	return limitCoordinates(skeleton.joints[joint].limits,
							modelQuatMultiply(modelQuatConjugate(bind), modelJointRotation(localPose(skeleton, pose, joint))));
}
bool modelPoseWithinJointLimits(const ModelSkeleton &skeleton, const QVector<ModelJointMatrix> &pose, double tolerance, QString *problem)
{
	for (int j = 0; j < skeleton.joints.size(); ++j)
	{
		const auto &limits = skeleton.joints[j].limits;
		if (!limits.enabled)
			continue;
		const auto angles = modelJointLimitAngles(skeleton, pose, j);
		const auto clamped = clampLimitCoordinates(limits, angles);
		const QStringList axes{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
		for (int axis = 0; axis < 3; ++axis)
		{
			if (std::abs(clamped[axis] - angles[axis]) <= tolerance)
				continue;
			if (limits.swingTwist && axis != limits.twistAxis)
				return fail(problem, Text::tr("Joint %1 swings %2 degrees about %3, outside its swing limits.")
										 .arg(skeleton.joints[j].name)
										 .arg(angles[axis], 0, 'g', 6)
										 .arg(axes[axis]));
			return fail(problem, Text::tr("Joint %1 turns %2 degrees about %3, outside its limits of %4 to %5.")
									 .arg(skeleton.joints[j].name)
									 .arg(angles[axis], 0, 'g', 6)
									 .arg(axes[axis])
									 .arg(limits.minimum[axis])
									 .arg(limits.maximum[axis]));
		}
	}
	return true;
}
bool validModelJointLimits(const ModelJointLimits &limits, QString *error)
{
	for (int axis = 0; axis < 3; ++axis)
		if (!std::isfinite(limits.minimum[axis]) || !std::isfinite(limits.maximum[axis]) || limits.minimum[axis] < -180 ||
			limits.maximum[axis] > 180 || limits.minimum[axis] > limits.maximum[axis])
			return fail(error, Text::tr("Joint limits need a minimum no greater than the maximum on each axis, both from -180 to 180 degrees."));
	if (limits.twistAxis < 0 || limits.twistAxis > 2 || (!limits.swingTwist && limits.twistAxis != 0))
		return fail(error, Text::tr("Swing-twist limits need a twist axis of X, Y or Z."));
	if (limits.swingTwist)
		for (int axis = 0; axis < 3; ++axis)
			if (axis != limits.twistAxis && (limits.minimum[axis] > 0 || limits.maximum[axis] < 0))
				return fail(error, Text::tr("Swing limits must include zero on both swing axes, so the bind pose stays inside them."));
	return true;
}
QJsonValue modelJointLimitsJson(const ModelJointLimits &limits)
{
	QJsonObject object{{QStringLiteral("minimum"), QJsonArray{limits.minimum[0], limits.minimum[1], limits.minimum[2]}},
					   {QStringLiteral("maximum"), QJsonArray{limits.maximum[0], limits.maximum[1], limits.maximum[2]}}};
	if (limits.swingTwist)
	{
		object.insert(QStringLiteral("mode"), QStringLiteral("swing-twist"));
		object.insert(QStringLiteral("twistAxis"), limits.twistAxis);
	}
	return object;
}
bool modelJointLimitsFromJson(const QJsonValue &value, ModelJointLimits *limits)
{
	auto object = value.toObject();
	ModelJointLimits result;
	result.enabled = true;
	if (object.contains(QStringLiteral("mode")))
	{
		const auto axis = object.take(QStringLiteral("twistAxis"));
		if (object.take(QStringLiteral("mode")) != QJsonValue(QStringLiteral("swing-twist")) || !axis.isDouble() || axis.toDouble() != axis.toInt())
			return false;
		result.swingTwist = true;
		result.twistAxis = axis.toInt();
	}
	for (const auto &field : {qMakePair(QStringLiteral("minimum"), &result.minimum), qMakePair(QStringLiteral("maximum"), &result.maximum)})
	{
		const auto values = object.value(field.first).toArray();
		if (values.size() != 3)
			return false;
		for (int i = 0; i < 3; ++i)
		{
			if (!values.at(i).isDouble())
				return false;
			(*field.second)[i] = values.at(i).toDouble();
		}
	}
	if (!value.isObject() || object.size() != 2 || !validModelJointLimits(result))
		return false;
	*limits = result;
	return true;
}
} // namespace vibestudio
