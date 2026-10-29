#include "core/model_whole_transform.h"

#include "core/model_animation_curves.h"
#include "core/model_document.h"
#include "core/model_skeleton.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelWholeTransform)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
using Matrix = std::array<std::array<double, 3>, 3>;
using Vector = std::array<double, 3>;
Matrix multiply(const Matrix &a, const Matrix &b)
{
	Matrix result{};
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c)
			result[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c];
	return result;
}
Vector multiply(const Matrix &a, const Vector &v)
{
	return {a[0][0] * v[0] + a[0][1] * v[1] + a[0][2] * v[2], a[1][0] * v[0] + a[1][1] * v[1] + a[1][2] * v[2],
			a[2][0] * v[0] + a[2][1] * v[1] + a[2][2] * v[2]};
}
double determinant(const Matrix &m)
{
	return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
		   m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}
// The inverse transpose, for normals.
Matrix normalMatrix(const Matrix &m)
{
	const double d = determinant(m);
	Matrix cofactor{};
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c)
		{
			const int r1 = (r + 1) % 3, r2 = (r + 2) % 3, c1 = (c + 1) % 3, c2 = (c + 2) % 3;
			cofactor[r][c] = (m[r1][c1] * m[r2][c2] - m[r1][c2] * m[r2][c1]) / d;
		}
	return cofactor;
}
double dot(const Vector &a, const Vector &b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vector cross(const Vector &a, const Vector &b)
{
	return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double length(const Vector &a)
{
	return std::sqrt(dot(a, a));
}
Vector scaled(const Vector &a, double s)
{
	return {a[0] * s, a[1] * s, a[2] * s};
}
Vector normalized(const Vector &a)
{
	const double l = length(a);
	return l > 1e-300 ? scaled(a, 1 / l) : Vector{0, 0, 0};
}
Vector vectorOf(ModelVec3 v)
{
	return {v.x, v.y, v.z};
}
ModelVec3 vec3(const Vector &v)
{
	return {float(v[0]), float(v[1]), float(v[2])};
}
// Rz * Ry * Rx for degrees applied X, then Y, then Z, as rotateModelVector.
Matrix eulerMatrix(ModelVec3 degrees)
{
	const double rx = degrees.x * std::numbers::pi / 180, ry = degrees.y * std::numbers::pi / 180, rz = degrees.z * std::numbers::pi / 180;
	const Matrix x{{{1, 0, 0}, {0, std::cos(rx), -std::sin(rx)}, {0, std::sin(rx), std::cos(rx)}}};
	const Matrix y{{{std::cos(ry), 0, std::sin(ry)}, {0, 1, 0}, {-std::sin(ry), 0, std::cos(ry)}}};
	const Matrix z{{{std::cos(rz), -std::sin(rz), 0}, {std::sin(rz), std::cos(rz), 0}, {0, 0, 1}}};
	return multiply(z, multiply(y, x));
}
// Degrees for an orthonormal right-handed frame with these axes, the inverse
// of eulerMatrix (Z = 0 at the Y-axis singularity, as collision boxes do).
ModelVec3 eulerDegrees(const Vector &x, const Vector &y, const Vector &z)
{
	const double horizontal = std::hypot(x[0], x[1]);
	const double degrees = 180.0 / std::numbers::pi;
	return {float((horizontal > 1e-6 ? std::atan2(y[2], z[2]) : std::atan2(-z[1], y[1])) * degrees), float(std::atan2(-x[2], horizontal) * degrees),
			float((horizontal > 1e-6 ? std::atan2(x[1], x[0]) : 0) * degrees)};
}
struct Affine
{
	Matrix linear{};
	Vector translation{};
	[[nodiscard]] Vector apply(const Vector &p) const
	{
		const auto q = multiply(linear, p);
		return {q[0] + translation[0], q[1] + translation[1], q[2] + translation[2]};
	}
	[[nodiscard]] ModelVec3 apply(ModelVec3 p) const { return vec3(apply(vectorOf(p))); }
};
// A joint matrix as rotation part and translation.
Matrix jointLinear(const ModelJointMatrix &m)
{
	return {{{m.m[0], m.m[1], m.m[2]}, {m.m[4], m.m[5], m.m[6]}, {m.m[8], m.m[9], m.m[10]}}};
}
Vector jointTranslation(const ModelJointMatrix &m)
{
	return {m.m[3], m.m[7], m.m[11]};
}
ModelJointMatrix jointMatrix(const Matrix &linear, const Vector &translation)
{
	ModelJointMatrix m;
	for (int r = 0; r < 3; ++r)
	{
		for (int c = 0; c < 3; ++c)
			m.m[r * 4 + c] = float(linear[r][c]);
		m.m[r * 4 + 3] = float(translation[r]);
	}
	return m;
}
bool transformCollisionPose(const Affine &affine, ModelVec3 *centre, ModelVec3 *size, ModelVec3 *rotation, QString *error)
{
	const auto axes = eulerMatrix(*rotation);
	Vector columns[3];
	double lengths[3];
	for (int i = 0; i < 3; ++i)
	{
		columns[i] = multiply(affine.linear, Vector{axes[0][i], axes[1][i], axes[2][i]});
		lengths[i] = length(columns[i]);
	}
	for (int i = 0; i < 3; ++i)
		for (int j = i + 1; j < 3; ++j)
			if (std::abs(dot(columns[i], columns[j])) > 1e-5 * lengths[i] * lengths[j])
				return fail(error, Text::tr("A rotated collision box would shear under this non-uniform scale. Scale the model uniformly, or "
											"align the box with the scaled axes first."));
	Vector x = normalized(columns[0]), y = normalized(columns[1]), z = normalized(columns[2]);
	// A box is symmetric: a mirrored box is the same box with one axis reversed.
	if (dot(cross(x, y), z) < 0)
		y = scaled(y, -1);
	*rotation = eulerDegrees(x, y, z);
	*size = {float(size->x * lengths[0]), float(size->y * lengths[1]), float(size->z * lengths[2])};
	*centre = affine.apply(*centre);
	return true;
}
bool bounds(const ModelMesh &mesh, int frame, Vector *low, Vector *high, QString *error)
{
	*low = {INFINITY, INFINITY, INFINITY};
	*high = {-INFINITY, -INFINITY, -INFINITY};
	bool any = false;
	for (const auto &surface : mesh.surfaces)
	{
		if (frame < 0 || frame >= surface.frames.size())
			continue;
		for (const auto &p : surface.frames[frame].positions)
		{
			const Vector v = vectorOf(p);
			for (int axis = 0; axis < 3; ++axis)
			{
				(*low)[axis] = std::min((*low)[axis], v[axis]);
				(*high)[axis] = std::max((*high)[axis], v[axis]);
			}
			any = true;
		}
	}
	return any || fail(error, Text::tr("The model has no vertices in that pose to measure."));
}
} // namespace

bool modelOriginOffset(const ModelMesh &mesh, int frame, ModelOriginTarget target, ModelVec3 point, ModelVec3 *offset, QString *error)
{
	if (target == ModelOriginTarget::Point)
	{
		*offset = {-point.x, -point.y, -point.z};
		return true;
	}
	Vector low, high;
	if (!bounds(mesh, frame, &low, &high, error))
		return false;
	const Vector centre{(low[0] + high[0]) / 2, (low[1] + high[1]) / 2, target == ModelOriginTarget::BoundsBottom ? low[2] : (low[2] + high[2]) / 2};
	*offset = vec3(scaled(centre, -1));
	return true;
}

bool modelHeightScale(const ModelMesh &mesh, int frame, double height, double *factor, QString *error)
{
	if (!std::isfinite(height) || height <= 0 || height > 1000000)
		return fail(error, Text::tr("Choose a height from above zero to 1,000,000 units."));
	Vector low, high;
	if (!bounds(mesh, frame, &low, &high, error))
		return false;
	const double extent = high[2] - low[2];
	if (extent < 1e-6)
		return fail(error, Text::tr("The model is flat along Z in that pose, so it has no height to scale."));
	*factor = height / extent;
	return true;
}

bool applyModelWholeTransform(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (edit.frame != -1)
		return fail(error, Text::tr("Transform Model moves every frame. Use the All frames scope."));
	if (edit.pivotMode != ModelTransformPivot::Custom)
		return fail(error, Text::tr("Transform Model pivots about an explicit point; the editor and CLI supply it."));
	ModelVec3 checked;
	if (!snapModelTranslation(edit.translation, 0, &checked, error) || !snapModelRotation(edit.rotation, 0, &checked, error) ||
		!snapModelScale(edit.scale, 0, &checked, error))
		return false;
	const auto finite = [](ModelVec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::abs(v.x) <= 1000000 && std::abs(v.y) <= 1000000 && std::abs(v.z) <= 1000000; };
	if (!finite(edit.pivot) || !finite(edit.translation))
		return fail(error, Text::tr("Tool coordinates must be finite and within ±1,000,000 units."));
	Affine affine;
	const Matrix scale{{{edit.scale.x, 0, 0}, {0, edit.scale.y, 0}, {0, 0, edit.scale.z}}};
	const Matrix rotation = eulerMatrix(edit.rotation);
	affine.linear = multiply(rotation, scale);
	const Vector pivot = vectorOf(edit.pivot), turned = multiply(affine.linear, pivot);
	affine.translation = {pivot[0] + edit.translation.x - turned[0], pivot[1] + edit.translation.y - turned[1], pivot[2] + edit.translation.z - turned[2]};
	const double det = determinant(affine.linear);
	if (!std::isfinite(det) || std::abs(det) < 1e-12)
		return fail(error, Text::tr("Transform Model needs a nonzero scale on every axis."));
	const bool mirrored = det < 0;
	const auto normals = normalMatrix(affine.linear);
	// Skeletal models: rotation, positive uniform scale and translation only,
	// so joint matrices stay rotations and skeletal formats can store them.
	const bool uniform = std::abs(edit.scale.x - edit.scale.y) <= 1e-6 * std::abs(edit.scale.x) &&
						 std::abs(edit.scale.x - edit.scale.z) <= 1e-6 * std::abs(edit.scale.x) && edit.scale.x > 0;
	if (!candidate->skeleton.isEmpty() && !uniform)
		return fail(error, Text::tr("Skeletal models take rotation, a positive uniform scale and translation only, so the joints stay "
									"rigid. Mirror or stretch the model before binding it."));
	const double s = edit.scale.x;
	const bool rotated = edit.rotation.x != 0 || edit.rotation.y != 0 || edit.rotation.z != 0;
	// Surfaces: every pose's positions and normals; a mirror reverses winding.
	for (auto &surface : candidate->surfaces)
	{
		for (auto &frame : surface.frames)
		{
			if (!work.step())
				return false;
			for (auto &p : frame.positions)
				p = affine.apply(p);
			for (auto &n : frame.normals)
			{
				const auto turnedNormal = normalized(multiply(normals, vectorOf(n)));
				n = length(turnedNormal) > 0 ? vec3(turnedNormal) : n;
			}
		}
		if (mirrored)
			for (auto &t : surface.triangles)
				std::swap(t.b, t.c);
		for (auto &influence : surface.skinning.influences)
			influence.offset = {float(influence.offset.x * s), float(influence.offset.y * s), float(influence.offset.z * s)};
	}
	// Tags: origins move, axes turn and stay orthonormal with their handedness.
	for (auto &tag : candidate->tags)
	{
		if (!work.step())
			return false;
		const Vector forward{tag.axis[0], tag.axis[1], tag.axis[2]}, left{tag.axis[3], tag.axis[4], tag.axis[5]}, up{tag.axis[6], tag.axis[7], tag.axis[8]};
		const double handed = dot(cross(forward, left), up) < 0 ? -1 : 1;
		const Vector f = normalized(multiply(affine.linear, forward));
		const Vector lifted = multiply(affine.linear, up);
		const Vector u = normalized({lifted[0] - dot(lifted, f) * f[0], lifted[1] - dot(lifted, f) * f[1], lifted[2] - dot(lifted, f) * f[2]});
		const Vector l = scaled(cross(u, f), handed);
		if (length(f) == 0 || length(u) == 0)
			return fail(error, Text::tr("Attachment tag %1 would lose its orientation.").arg(tag.name));
		for (int i = 0; i < 3; ++i)
		{
			tag.axis[i] = float(f[i]);
			tag.axis[3 + i] = float(l[i]);
			tag.axis[6 + i] = float(u[i]);
		}
		tag.origin = affine.apply(tag.origin);
	}
	// Collision boxes and every collision pose.
	for (auto &box : candidate->collisionBoxes)
	{
		if (!transformCollisionPose(affine, &box.centre, &box.size, &box.rotation, error))
			return fail(error, QStringLiteral("%1 %2").arg(Text::tr("Collision box %1:").arg(box.name), error ? *error : QString()));
		for (auto &pose : box.framePoses)
			if (!transformCollisionPose(affine, &pose.centre, &pose.size, &pose.rotation, error))
				return false;
	}
	// The skeleton: model-space joints turn and move; bind-space offsets scale.
	auto &skeleton = candidate->skeleton;
	if (!skeleton.isEmpty())
	{
		Matrix turn{};
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c)
				turn[r][c] = affine.linear[r][c] / s;
		const auto moveJoint = [&](ModelJointMatrix *m) { *m = jointMatrix(multiply(turn, jointLinear(*m)), affine.apply(jointTranslation(*m))); };
		for (auto &joint : skeleton.joints)
			moveJoint(&joint.bind);
		for (auto &tag : skeleton.tags)
			for (int r = 0; r < 3; ++r)
				tag.offset.m[r * 4 + 3] = float(tag.offset.m[r * 4 + 3] * s);
		for (auto &clip : skeleton.clips)
		{
			if (!work.step())
				return false;
			for (auto &frame : clip.frames)
				for (auto &m : frame)
					moveJoint(&m);
			for (int f = 0; f < std::min(clip.frameMins.size(), clip.frameMaxs.size()); ++f)
			{
				Vector low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
				for (int corner = 0; corner < 8; ++corner)
				{
					const Vector p = affine.apply(Vector{(corner & 1) ? clip.frameMaxs[f].x : clip.frameMins[f].x,
														 (corner & 2) ? clip.frameMaxs[f].y : clip.frameMins[f].y,
														 (corner & 4) ? clip.frameMaxs[f].z : clip.frameMins[f].z});
					for (int axis = 0; axis < 3; ++axis)
					{
						low[axis] = std::min(low[axis], p[axis]);
						high[axis] = std::max(high[axis], p[axis]);
					}
				}
				clip.frameMins[f] = vec3(low);
				clip.frameMaxs[f] = vec3(high);
			}
			// Curves hold parent-local channels: translations scale, and a root
			// joint's translation also moves. A root's rotation curves cannot
			// take a fixed turn exactly through Euler interpolation.
			for (auto &group : clip.curves)
			{
				int joint = -1;
				for (int j = 0; j < skeleton.joints.size(); ++j)
					if (skeleton.joints[j].name == group.joint)
						joint = j;
				const bool root = joint < 0 || skeleton.joints[joint].parent < 0;
				if (root && rotated)
					return fail(error, Text::tr("Root joint %1 has animation curves in clip %2, which a rotation cannot follow exactly. Bake or "
												"clear the root's curves, or rotate before animating.")
										   .arg(group.joint, clip.name));
				for (int channel = 0; channel < 3; ++channel)
					for (auto &key : group.channels[channel])
					{
						key.value = key.value * s + (root ? affine.translation[channel] : 0);
						key.inSlope *= s;
						key.outSlope *= s;
					}
			}
		}
	}
	// The Quake MDL eye position is a model-space point.
	candidate->mdl.eyePosition = affine.apply(candidate->mdl.eyePosition);
	if (!skeleton.isEmpty() && (!validateModelSkeleton(*candidate, error, control) || !validateModelAnimationCurves(*candidate, error, control)))
		return false;
	return work.check();
}
} // namespace vibestudio
