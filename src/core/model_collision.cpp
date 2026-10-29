#include "core/model_collision.h"
#include "core/model_collision_placement.h"
#include "core/model_collision_hull.h"
#include "core/level_brush.h"
#include "core/model_document.h"
#include "core/model_pose.h"
#include "core/model_transform.h"
#include "core/model_transform_axes.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelCollision)
};
bool fail(QString *error, const QString &message)
{
	if (error)
	{
		*error = message;
	}
	return false;
}
bool finite(ModelVec3 v, double limit)
{
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
		   std::max({std::abs(double(v.x)), std::abs(double(v.y)), std::abs(double(v.z))}) <= limit;
}
bool nameOk(const QString &name)
{
	return !name.isEmpty() && name.size() <= 128 && name == name.trimmed() &&
		   std::none_of(name.cbegin(), name.cend(), [](QChar c) { return c.isNull() || c.category() == QChar::Other_Control; });
}
QJsonArray xyz(ModelVec3 p)
{
	return {modelSourceNumber(p.x), modelSourceNumber(p.y), modelSourceNumber(p.z)};
}
bool vector(const QJsonValue &value, ModelVec3 *result)
{
	if (!value.isArray() || value.toArray().size() != 3)
	{
		return false;
	}
	const auto array = value.toArray();
	float values[3]{};
	for (int i = 0; i < 3; ++i)
	{
		if (!array[i].isDouble() || !std::isfinite(array[i].toDouble()) || std::abs(array[i].toDouble()) > 65536)
		{
			return false;
		}
		values[i] = float(array[i].toDouble());
	}
	*result = {values[0], values[1], values[2]};
	return true;
}
QString point(LevelMapVec3 p)
{
	return QStringLiteral("( %1 %2 %3 )").arg(p.x, 0, 'g', 17).arg(p.y, 0, 'g', 17).arg(p.z, 0, 'g', 17);
}
ModelVec3 orientation(ModelVec3 x, ModelVec3 y, ModelVec3 z)
{
	const double horizontal = std::hypot(double(x.x), double(x.y));
	const double degrees = 180.0 / std::numbers::pi;
	// Decompose Rz * Ry * Rx, choosing Z=0 at the Y-axis singularity.
	return {float((horizontal > 1e-6 ? std::atan2(y.z, z.z) : std::atan2(-z.y, y.y)) * degrees),
			float(std::atan2(-double(x.z), horizontal) * degrees), float((horizontal > 1e-6 ? std::atan2(x.y, x.x) : 0) * degrees)};
}
ModelCollisionPose values(const ModelCollisionBox &box)
{
	return {box.centre, box.size, box.rotation};
}
bool same(ModelVec3 a, ModelVec3 b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}
bool sameHull(const QVector<ModelVec3> &a, const QVector<ModelVec3> &b)
{
	return std::equal(a.cbegin(), a.cend(), b.cbegin(), b.cend(), [](ModelVec3 x, ModelVec3 y) { return same(x, y); });
}
QString poseError(const ModelCollisionBox &box)
{
	if (!finite(box.centre, 32768) || !finite(box.size, 65536) || !finite(box.rotation, 36000) || box.size.x < 1 || box.size.y < 1 ||
		box.size.z < 1)
		return Text::tr(
			"Collision boxes need finite centres within ±32768, sizes of at least one unit, and rotations within ±36000 degrees.");
	for (const auto p : modelCollisionCorners(box))
		if (!finite(p, 32768))
			return Text::tr("Every rotated collision corner must remain within ±32768 model units.");
	return {};
}
QJsonObject poseJson(const ModelCollisionPose &pose)
{
	return {{"centre", xyz(pose.centre)}, {"size", xyz(pose.size)}, {"rotation", xyz(pose.rotation)}};
}
bool readPose(const QJsonObject &object, ModelCollisionPose *pose)
{
	return vector(object.value("centre"), &pose->centre) && vector(object.value("size"), &pose->size) &&
		   vector(object.value("rotation"), &pose->rotation);
}
} // namespace

void setModelCollisionFrames(ModelCollisionBox *box, QVector<ModelCollisionPose> poses)
{
	if (!box)
		return;
	if (!poses.isEmpty())
	{
		box->centre = poses[0].centre;
		box->size = poses[0].size;
		box->rotation = poses[0].rotation;
		// A per-pose hull shape: the volume's own shape mirrors pose zero.
		if (!poses[0].hull.isEmpty())
			box->hull = poses[0].hull;
	}
	box->framePoses = std::move(poses);
}
bool sampleModelCollisionBox(const ModelCollisionBox &box, int frame, int nextFrame, double amount, ModelCollisionBox *result)
{
	if (!result || frame < 0 || nextFrame < 0 || !std::isfinite(amount) || amount < 0 || amount > 1 ||
		(!box.framePoses.isEmpty() && (frame >= box.framePoses.size() || nextFrame >= box.framePoses.size())))
		return false;
	if (box.framePoses.isEmpty())
	{
		*result = box;
		return true;
	}
	const auto &a = box.framePoses[frame], &b = box.framePoses[nextFrame];
	if (amount == 0 || amount == 1 || frame == nextFrame)
	{
		const auto &pose = amount == 1 ? b : a;
		*result = {box.name, pose.centre, pose.size, pose.rotation};
		result->hull = pose.hull.isEmpty() ? box.hull : pose.hull;
		return true;
	}
	ModelTag first, second, blended;
	const auto tag = [](const ModelCollisionPose &pose, ModelTag *output) {
		const auto axes = modelCollisionAxes({{}, {}, pose.size, pose.rotation});
		for (int i = 0; i < 3; ++i)
		{
			output->axis[i * 3] = axes[i].x;
			output->axis[i * 3 + 1] = axes[i].y;
			output->axis[i * 3 + 2] = axes[i].z;
		}
	};
	tag(a, &first);
	tag(b, &second);
	if (!interpolateModelTag(first, second, amount, &blended))
		return false;
	const auto *axis = blended.axis;
	*result = {box.name, interpolateModelPosition(a.centre, b.centre, amount), interpolateModelPosition(a.size, b.size, amount),
			   orientation({axis[0], axis[1], axis[2]}, {axis[3], axis[4], axis[5]}, {axis[6], axis[7], axis[8]})};
	// Shapes cannot blend; between stored poses the nearer pose's shape holds.
	const auto &shape = amount < .5 ? a.hull : b.hull;
	result->hull = shape.isEmpty() ? box.hull : shape;
	return true;
}

std::array<ModelVec3, 8> modelCollisionCorners(const ModelCollisionBox &box)
{
	std::array<ModelVec3, 8> result;
	for (int i = 0; i < 8; ++i)
	{
		const auto p = rotateModelVector(
			{box.size.x * (i & 1 ? .5f : -.5f), box.size.y * (i & 2 ? .5f : -.5f), box.size.z * (i & 4 ? .5f : -.5f)}, box.rotation);
		result[i] = {p.x + box.centre.x, p.y + box.centre.y, p.z + box.centre.z};
	}
	return result;
}
const ModelCollisionBox *findModelCollisionBox(const ModelMesh &mesh, const QString &name)
{
	const auto it =
		std::find_if(mesh.collisionBoxes.cbegin(), mesh.collisionBoxes.cend(), [&](const auto &box) { return box.name == name; });
	return it == mesh.collisionBoxes.cend() ? nullptr : &*it;
}
std::array<ModelVec3, 3> modelCollisionAxes(const ModelCollisionBox &box)
{
	return {rotateModelVector({1, 0, 0}, box.rotation), rotateModelVector({0, 1, 0}, box.rotation),
			rotateModelVector({0, 0, 1}, box.rotation)};
}
bool transformModelCollisionBox(const ModelCollisionBox &box, const ModelTransform &transform, ModelCollisionBox *result, QString *error)
{
	ModelVec3 checked;
	if (!box.framePoses.isEmpty())
		return fail(error, Text::tr("Sample one collision pose before transforming it."));
	if (!result || !validModelTransformBasis(transform.basis) || !finite(transform.pivot, 1000000) ||
		!snapModelTranslation(transform.translation, 0, &checked, error) || !snapModelRotation(transform.rotation, 0, &checked, error) ||
		!snapModelScale(transform.scale, 0, &checked, error))
	{
		return fail(error, Text::tr("Use finite collision transforms and nonzero scale factors within the supported range."));
	}
	const auto axes = modelCollisionAxes(box);
	auto candidate = box;
	ModelVec3 offset{box.centre.x - transform.pivot.x, box.centre.y - transform.pivot.y, box.centre.z - transform.pivot.z};
	if (transform.scale.x != 1 || transform.scale.y != 1 || transform.scale.z != 1)
	{
		const auto dot = [offset](ModelVec3 axis) {
			return double(offset.x) * axis.x + double(offset.y) * axis.y + double(offset.z) * axis.z;
		};
		const double x = dot(axes[0]) * transform.scale.x, y = dot(axes[1]) * transform.scale.y, z = dot(axes[2]) * transform.scale.z;
		offset = {float(axes[0].x * x + axes[1].x * y + axes[2].x * z), float(axes[0].y * x + axes[1].y * y + axes[2].y * z),
				  float(axes[0].z * x + axes[1].z * y + axes[2].z * z)};
	}
	if (transform.rotation.x != 0 || transform.rotation.y != 0 || transform.rotation.z != 0)
	{
		offset = rotateModelTransformVector(offset, transform);
		const auto x = rotateModelTransformVector(axes[0], transform), y = rotateModelTransformVector(axes[1], transform),
				   z = rotateModelTransformVector(axes[2], transform);
		candidate.rotation = orientation(x, y, z);
	}
	const auto translation = modelBasisToWorld(transform.translation, transform.basis);
	candidate.centre = {transform.pivot.x + offset.x + translation.x, transform.pivot.y + offset.y + translation.y,
						transform.pivot.z + offset.z + translation.z};
	if (transform.scale.x == 1 && transform.scale.y == 1 && transform.scale.z == 1 && transform.rotation.x == 0 &&
		transform.rotation.y == 0 && transform.rotation.z == 0)
	{
		// Translation does not depend on the pivot; avoid subtract/add rounding
		// for a distant custom pivot, especially on an otherwise neutral edit.
		candidate.centre = {box.centre.x + translation.x, box.centre.y + translation.y, box.centre.z + translation.z};
	}
	candidate.size = {box.size.x * std::abs(transform.scale.x), box.size.y * std::abs(transform.scale.y),
					  box.size.z * std::abs(transform.scale.z)};
	ModelMesh validation;
	validation.collisionBoxes = {candidate};
	const auto errors = validateModelCollision(validation);
	if (!errors.isEmpty())
	{
		return fail(error, errors.join('\n'));
	}
	*result = candidate;
	return true;
}
bool composeModelCollisionVolume(const ModelCollisionBox &volume, const ModelTag &basis, double scale, ModelCollisionBox *result,
								 QString *error)
{
	if (!result || !volume.framePoses.isEmpty() || !std::isfinite(scale) || scale <= 0)
		return fail(error, Text::tr("Sample one collision pose and use a positive scale before composing it."));
	double axes[3][3];
	for (int row = 0; row < 3; ++row)
		for (int column = 0; column < 3; ++column)
			axes[row][column] = basis.axis[row * 3 + column];
	const auto dot = [](const double *a, const double *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
	for (int row = 0; row < 3; ++row)
		for (int other = row; other < 3; ++other)
			if (!std::isfinite(dot(axes[row], axes[other])) || std::abs(dot(axes[row], axes[other]) - (row == other ? 1 : 0)) > 0.01)
				return fail(error, Text::tr("Collision cannot follow a skewed or scaled attachment basis. Sample a stored frame, or use "
											"shortest-arc tag interpolation."));
	// Gram-Schmidt keeps the first axis's direction and straightens rounding.
	for (int row = 0; row < 3; ++row)
	{
		for (int previous = 0; previous < row; ++previous)
		{
			const double along = dot(axes[row], axes[previous]);
			for (int c = 0; c < 3; ++c)
				axes[row][c] -= along * axes[previous][c];
		}
		const double length = std::sqrt(dot(axes[row], axes[row]));
		for (int c = 0; c < 3; ++c)
			axes[row][c] /= length;
	}
	const double determinant = axes[0][0] * (axes[1][1] * axes[2][2] - axes[1][2] * axes[2][1]) -
							   axes[0][1] * (axes[1][0] * axes[2][2] - axes[1][2] * axes[2][0]) +
							   axes[0][2] * (axes[1][0] * axes[2][1] - axes[1][1] * axes[2][0]);
	const auto place = [&](ModelVec3 v) {
		return ModelVec3{float(v.x * axes[0][0] + v.y * axes[1][0] + v.z * axes[2][0]),
						 float(v.x * axes[0][1] + v.y * axes[1][1] + v.z * axes[2][1]),
						 float(v.x * axes[0][2] + v.y * axes[1][2] + v.z * axes[2][2])};
	};
	const auto local = modelCollisionAxes(volume);
	auto x = place(local[0]), y = place(local[1]), z = place(local[2]);
	auto composed = volume;
	if (determinant < 0)
	{
		// A mirrored frame: flip the volume's own Z so the rotation is proper.
		z = {-z.x, -z.y, -z.z};
		for (auto &point : composed.hull)
			point.z = -point.z;
	}
	const auto centre = place({float(volume.centre.x * scale), float(volume.centre.y * scale), float(volume.centre.z * scale)});
	composed.centre = {basis.origin.x + centre.x, basis.origin.y + centre.y, basis.origin.z + centre.z};
	composed.size = {float(volume.size.x * scale), float(volume.size.y * scale), float(volume.size.z * scale)};
	composed.rotation = orientation(x, y, z);
	*result = std::move(composed);
	return true;
}
QStringList validateModelCollision(const ModelMesh &mesh, const ModelWorkControl &control)
{
	QString error;
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Validating, 0, mesh.collisionBoxes.size(), &error))
	{
		return {error};
	}
	if (mesh.collisionBoxes.size() > modelCollisionBoxLimit)
	{
		return {Text::tr("A source supports at most 64 collision boxes.")};
	}
	QSet<QString> names;
	for (const auto &box : mesh.collisionBoxes)
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Validating, names.size(), mesh.collisionBoxes.size(), &error))
		{
			return {error};
		}
		if (!nameOk(box.name) || names.contains(box.name))
		{
			return {Text::tr("Collision box names must be unique, 1–128 characters, with no surrounding spaces or control characters.")};
		}
		names.insert(box.name);
		if (const auto hullProblem = validateModelCollisionHull(box); !hullProblem.isEmpty())
			return {Text::tr("Collision volume %1: %2").arg(box.name, hullProblem)};
		if (box.framePoses.isEmpty())
		{
			const auto problem = poseError(box);
			if (!problem.isEmpty())
				return {problem};
			continue;
		}
		if (box.framePoses.size() != mesh.frames.size() || box.framePoses.size() > modelDocumentMaxFrames ||
			!same(box.centre, box.framePoses[0].centre) || !same(box.size, box.framePoses[0].size) ||
			!same(box.rotation, box.framePoses[0].rotation) ||
			(!box.framePoses[0].hull.isEmpty() && !sameHull(box.hull, box.framePoses[0].hull)))
			return {Text::tr("Animated collision boxes need one pose per mesh frame and scalar fields matching pose zero.")};
		for (int frame = 0; frame < box.framePoses.size(); ++frame)
		{
			if (!modelWorkCheckpoint(control, ModelWorkPhase::Validating, frame, box.framePoses.size(), &error))
				return {error};
			const auto &pose = box.framePoses[frame];
			auto problem = poseError({box.name, pose.centre, pose.size, pose.rotation});
			if (problem.isEmpty() && !pose.hull.isEmpty())
				problem = box.hull.isEmpty() ? Text::tr("A box cannot take a hull shape in one pose only.")
											 : validateModelCollisionHull({box.name, pose.centre, pose.size, pose.rotation, {}, pose.hull});
			if (!problem.isEmpty())
				return {Text::tr("Collision box %1, frame %2: %3").arg(box.name).arg(frame).arg(problem)};
		}
	}
	return {};
}
bool validModelCollisionSelection(const ModelMesh &mesh, const ModelSelection &selection)
{
	return selection.collision.isEmpty() ||
		   (selection.joint.isEmpty() && selection.surfaces.isEmpty() && selection.tag.isEmpty() && selection.vertices.isEmpty() && selection.faces.isEmpty() &&
			selection.edges.isEmpty() && findModelCollisionBox(mesh, selection.collision));
}
bool isModelCollisionEdit(ModelEditKind kind)
{
	return kind == ModelEditKind::AddCollisionBox || kind == ModelEditKind::FitCollisionBox || kind == ModelEditKind::UpdateCollisionBox ||
		   kind == ModelEditKind::DuplicateCollisionBox || kind == ModelEditKind::DeleteCollisionBox ||
		   kind == ModelEditKind::TransformCollisionBox || kind == ModelEditKind::AnimateCollisionBox ||
		   kind == ModelEditKind::FreezeCollisionBox || kind == ModelEditKind::FitAnimatedCollisionBox ||
		   kind == ModelEditKind::FitCollisionHull || kind == ModelEditKind::FitAnimatedCollisionHull;
}
bool applyModelCollisionEdit(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error,
							 const ModelWorkControl &control)
{
	const auto kind = edit.kind;
	if (!candidate || !selection || !isModelCollisionEdit(kind) || edit.frame < -1 || edit.frame >= candidate->frames.size())
	{
		return fail(error, Text::tr("Choose a valid collision operation and frame."));
	}
	const auto existing = findModelCollisionBox(*candidate, edit.selection.collision);
	const bool add = kind == ModelEditKind::AddCollisionBox || kind == ModelEditKind::FitCollisionBox ||
					 kind == ModelEditKind::FitAnimatedCollisionBox || kind == ModelEditKind::FitCollisionHull ||
					 kind == ModelEditKind::FitAnimatedCollisionHull;
	const bool animated = existing && !existing->framePoses.isEmpty();
	if (!add && !existing)
	{
		return fail(error, Text::tr("Select an existing collision box."));
	}
	const bool scoped = kind == ModelEditKind::FitCollisionBox || kind == ModelEditKind::FitCollisionHull ||
						kind == ModelEditKind::FreezeCollisionBox ||
						(animated && (kind == ModelEditKind::UpdateCollisionBox || kind == ModelEditKind::TransformCollisionBox));
	if (edit.frame != -1 && !scoped)
		return fail(error,
					Text::tr("This collision operation uses all-frame scope. Animate a static box before editing individual poses."));
	if ((kind == ModelEditKind::AddCollisionBox || kind == ModelEditKind::UpdateCollisionBox) && !edit.collisionBox.framePoses.isEmpty())
		return fail(error, Text::tr("Supply one box pose for add or update; use Animate Box to create a track."));
	if (edit.collisionFields < 0 || edit.collisionFields > 7)
		return fail(error, Text::tr("Choose valid collision update fields."));
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	if (kind == ModelEditKind::DeleteCollisionBox)
	{
		candidate->collisionBoxes.removeIf([&](const auto &box) { return box.name == edit.selection.collision; });
		selection->collision.clear();
		return true;
	}
	if (kind == ModelEditKind::AnimateCollisionBox || kind == ModelEditKind::FreezeCollisionBox)
	{
		if ((kind == ModelEditKind::AnimateCollisionBox && animated) ||
			(kind == ModelEditKind::FreezeCollisionBox && (!animated || edit.frame < 0)))
			return fail(error, Text::tr("Animate a static box, or freeze an animated box at an explicit existing frame."));
		auto box = *existing;
		if (kind == ModelEditKind::AnimateCollisionBox)
			setModelCollisionFrames(&box, QVector<ModelCollisionPose>(candidate->frames.size(), values(box)));
		else if (!sampleModelCollisionBox(box, edit.frame, edit.frame, 0, &box))
			return false;
		for (auto &entry : candidate->collisionBoxes)
			if (entry.name == box.name)
			{
				entry = std::move(box);
				break;
			}
		return work.check();
	}
	if (kind == ModelEditKind::TransformCollisionBox)
	{
		ModelTransform transform;
		ModelCollisionBox reference;
		const int referenceFrame = edit.frame >= 0 ? edit.frame : edit.pivotFrame;
		if (!sampleModelCollisionBox(*existing, referenceFrame, referenceFrame, 0, &reference) ||
			!modelTransformPivot({reference.centre}, {0}, edit.pivotMode, edit.pivot, &transform.pivot))
		{
			return fail(error, Text::tr("Choose a valid collision transform pivot."));
		}
		if (!snapModelTranslation(edit.translation, edit.translationGrid, &transform.translation, error) ||
			!snapModelRotation(edit.rotation, edit.rotationGrid, &transform.rotation, error) ||
			!snapModelScale(edit.scale, edit.scaleGrid, &transform.scale, error) ||
			!resolveModelTransformAxes(*candidate, edit, &transform.basis, error, control))
		{
			return false;
		}
		auto box = *existing;
		if (!animated)
		{
			if (!transformModelCollisionBox(*existing, transform, &box, error))
				return false;
		}
		else
		{
			auto poses = box.framePoses;
			for (int frame = 0; frame < poses.size(); ++frame)
			{
				if (!work.step())
					return false;
				if (edit.frame >= 0 && edit.frame != frame)
					continue;
				ModelCollisionBox pose, transformed;
				if (!sampleModelCollisionBox(*existing, frame, frame, 0, &pose) ||
					!transformModelCollisionBox(pose, transform, &transformed, error))
					return false;
				auto moved = values(transformed);
				moved.hull = poses[frame].hull;
				poses[frame] = moved;
			}
			setModelCollisionFrames(&box, std::move(poses));
		}
		for (auto &entry : candidate->collisionBoxes)
		{
			if (entry.name == box.name)
			{
				entry = box;
				break;
			}
		}
		*selection = {edit.selection.surface, {}, {}, {}, {}, box.name};
		return work.check();
	}
	ModelCollisionBox box =
		kind == ModelEditKind::DuplicateCollisionBox || kind == ModelEditKind::UpdateCollisionBox ? *existing : edit.collisionBox;
	box.name = edit.collisionBox.name;
	if (kind == ModelEditKind::UpdateCollisionBox)
	{
		const auto update = [&](ModelCollisionPose pose) {
			if (edit.collisionFields & 1)
				pose.centre = edit.collisionBox.centre;
			if (edit.collisionFields & 2)
				pose.size = edit.collisionBox.size;
			if (edit.collisionFields & 4)
				pose.rotation = edit.collisionBox.rotation;
			return pose;
		};
		if (animated)
		{
			auto poses = box.framePoses;
			for (int frame = 0; frame < poses.size(); ++frame)
			{
				if (!work.step())
					return false;
				if (edit.frame < 0 || edit.frame == frame)
					poses[frame] = update(poses[frame]);
			}
			setModelCollisionFrames(&box, std::move(poses));
		}
		else
		{
			const auto pose = update(values(box));
			box = {box.name, pose.centre, pose.size, pose.rotation, {}, box.hull};
		}
	}
	if (kind == ModelEditKind::FitCollisionHull || kind == ModelEditKind::FitAnimatedCollisionHull)
	{
		// Every selected (or every) vertex of the chosen frame, or of all frames;
		// an animated hull fits each frame on its own.
		const bool components = !edit.selection.vertices.isEmpty() || !edit.selection.edges.isEmpty() || !edit.selection.faces.isEmpty();
		if (components && (edit.selection.surface < 0 || edit.selection.surface >= candidate->surfaces.size()))
			return fail(error, Text::tr("Select components of an existing surface."));
		QSet<int> selected = edit.selection.vertices;
		for (auto edge : edit.selection.edges)
			selected << edge.first << edge.second;
		for (int face : edit.selection.faces)
		{
			const auto t = candidate->surfaces[edit.selection.surface].triangles.value(face);
			selected << t.a << t.b << t.c;
		}
		const bool track = kind == ModelEditKind::FitAnimatedCollisionHull;
		const auto gather = [&](int frame, QVector<ModelVec3> *points) {
			for (int s = 0; s < candidate->surfaces.size(); ++s)
			{
				if (components && s != edit.selection.surface)
					continue;
				const auto &surface = candidate->surfaces[s];
				for (int f = 0; f < surface.frames.size(); ++f)
				{
					if (frame >= 0 && f != frame)
						continue;
					const auto &positions = surface.frames[f].positions;
					for (int v = 0; v < positions.size(); ++v)
					{
						if (!work.step())
							return false;
						if (!components || selected.contains(v))
							*points << positions[v];
					}
				}
			}
			return true;
		};
		ModelCollisionHullBudget budget;
		if (!modelCollisionHullBudget(edit.collisionHullProfile, edit.collisionHullMaxFaces, &budget, error))
			return false;
		const QString name = box.name;
		QVector<ModelCollisionPose> poses;
		for (int frame = track ? 0 : edit.frame; frame < (track ? int(candidate->frames.size()) : edit.frame + 1); ++frame)
		{
			QVector<ModelVec3> points;
			ModelCollisionHullFit fit;
			ModelCollisionBox shaped;
			if (!gather(frame, &points) || !fitModelCollisionHull(points, budget, &fit, error, control) ||
				!makeModelCollisionHull(name, fit.vertices, &shaped, error))
			{
				if (track && error && !error->isEmpty())
					*error = Text::tr("Frame %1: %2").arg(frame).arg(*error);
				return false;
			}
			if (!track)
			{
				box = shaped;
				break;
			}
			poses << ModelCollisionPose{shaped.centre, shaped.size, shaped.rotation, shaped.hull};
		}
		if (track)
		{
			box = {name, poses[0].centre, poses[0].size, poses[0].rotation, {}, poses[0].hull};
			setModelCollisionFrames(&box, std::move(poses));
		}
	}
	if (kind == ModelEditKind::FitCollisionBox || kind == ModelEditKind::FitAnimatedCollisionBox)
	{
		const bool components = !edit.selection.vertices.isEmpty() || !edit.selection.edges.isEmpty() || !edit.selection.faces.isEmpty();
		QSet<int> selected = edit.selection.vertices;
		for (auto edge : edit.selection.edges)
		{
			selected << edge.first << edge.second;
		}
		for (int face : edit.selection.faces)
		{
			const auto t = candidate->surfaces[edit.selection.surface].triangles[face];
			selected << t.a << t.b << t.c;
		}
		const float inf = std::numeric_limits<float>::infinity();
		const bool track = kind == ModelEditKind::FitAnimatedCollisionBox;
		const int count = track ? int(candidate->frames.size()) : 1;
		QVector<ModelVec3> lows(count, {inf, inf, inf}), highs(count, {-inf, -inf, -inf});
		for (int s = 0; s < candidate->surfaces.size(); ++s)
		{
			if (components && s != edit.selection.surface)
			{
				continue;
			}
			const auto &surface = candidate->surfaces[s];
			for (int f = 0; f < surface.frames.size(); ++f)
			{
				if (edit.frame >= 0 && f != edit.frame)
				{
					continue;
				}
				const auto &positions = surface.frames[f].positions;
				auto &lo = lows[track ? f : 0], &hi = highs[track ? f : 0];
				for (int v = 0; v < positions.size(); ++v)
				{
					if (!work.step())
					{
						return false;
					}
					if (components && !selected.contains(v))
					{
						continue;
					}
					const auto p = positions[v];
					lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
					hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
				}
			}
		}
		QVector<ModelCollisionPose> poses;
		for (int frame = 0; frame < count; ++frame)
		{
			if (!work.step())
				return false;
			const auto lo = lows[frame], hi = highs[frame];
			poses << ModelCollisionPose{{(lo.x + hi.x) * .5f, (lo.y + hi.y) * .5f, (lo.z + hi.z) * .5f},
										{std::max(1.f, hi.x - lo.x), std::max(1.f, hi.y - lo.y), std::max(1.f, hi.z - lo.z)},
										{}};
		}
		setModelCollisionFrames(&box, std::move(poses));
		if (!track)
			box.framePoses.clear();
		if (!work.check())
		{
			return false;
		}
	}
	if (!nameOk(box.name) || (findModelCollisionBox(*candidate, box.name) &&
							  (kind != ModelEditKind::UpdateCollisionBox || box.name != edit.selection.collision)))
	{
		return fail(error,
					Text::tr("Use a unique collision box name of 1–128 characters without surrounding spaces or control characters."));
	}
	if (kind == ModelEditKind::UpdateCollisionBox)
	{
		for (auto &entry : candidate->collisionBoxes)
		{
			if (entry.name == edit.selection.collision)
			{
				entry = box;
				break;
			}
		}
	}
	else
	{
		if (candidate->collisionBoxes.size() >= modelCollisionBoxLimit)
		{
			return fail(error, Text::tr("A source supports at most 64 collision boxes."));
		}
		candidate->collisionBoxes << box;
	}
	*selection = {edit.selection.surface, {}, {}, {}, {}, box.name};
	return true;
}
QJsonArray modelCollisionJson(const ModelMesh &mesh, QString *error, const ModelWorkControl &control)
{
	QJsonArray result;
	ModelWorkProgress work(control, ModelWorkPhase::Serializing, error);
	for (const auto &box : mesh.collisionBoxes)
	{
		if (!work.step())
			return {};
		QJsonArray hull;
		for (const auto &point : box.hull)
			hull << xyz(point);
		if (box.framePoses.isEmpty())
		{
			auto object = poseJson(values(box));
			object.insert("name", box.name);
			if (!hull.isEmpty())
				object.insert("hull", hull);
			result << object;
		}
		else
		{
			QJsonArray poses;
			for (const auto &pose : box.framePoses)
			{
				if (!work.step())
					return {};
				auto object = poseJson(pose);
				if (!pose.hull.isEmpty())
				{
					QJsonArray shape;
					for (const auto &point : pose.hull)
						shape << xyz(point);
					object.insert("hull", shape);
				}
				poses << object;
			}
			QJsonObject object{{"name", box.name}, {"poses", poses}};
			if (!hull.isEmpty())
				object.insert("hull", hull);
			result << object;
		}
	}
	return work.check() ? result : QJsonArray{};
}
bool parseModelCollision(const QJsonValue &value, ModelMesh *mesh, QString *error, const ModelWorkControl &control)
{
	if (!mesh || !value.isArray() || value.toArray().size() > modelCollisionBoxLimit)
	{
		return fail(error, Text::tr("The collision box array is missing, malformed, or exceeds 64 boxes."));
	}
	ModelWorkProgress work(control, ModelWorkPhase::Reading, error);
	QVector<ModelCollisionBox> boxes;
	for (const auto &entry : value.toArray())
	{
		if (!work.step())
			return false;
		const auto object = entry.toObject();
		ModelCollisionBox box;
		if (!entry.isObject() || !object.value("name").isString())
		{
			return fail(error, Text::tr("A collision box has malformed or unsupported fields."));
		}
		box.name = object.value("name").toString();
		// A convex hull adds "hull" to either shape. Releases before hulls refuse
		// such a volume rather than dropping its shape.
		QVector<ModelVec3> hull;
		const bool hasHull = object.contains("hull");
		if (hasHull)
		{
			const auto points = object.value("hull").toArray();
			if (!object.value("hull").isArray() || points.size() < 4 || points.size() > modelCollisionHullMaxPoints)
				return fail(error, Text::tr("A collision hull needs 4 to 256 points."));
			for (const auto &item : points)
			{
				ModelVec3 point;
				if (!vector(item, &point))
					return fail(error, Text::tr("A collision hull point is malformed."));
				hull << point;
			}
		}
		const int extra = hasHull ? 1 : 0;
		if (object.contains("poses"))
		{
			const auto array = object.value("poses").toArray();
			if (mesh->version < 7 || object.size() != 2 + extra || !object.value("poses").isArray() || array.isEmpty() ||
				array.size() != mesh->frames.size() || array.size() > modelDocumentMaxFrames)
				return fail(error, Text::tr("Animated collision requires source version 7 and exactly one pose per mesh frame."));
			QVector<ModelCollisionPose> poses;
			for (const auto &item : array)
			{
				if (!work.step())
					return false;
				ModelCollisionPose pose;
				const auto poseObject = item.toObject();
				const bool shaped = poseObject.contains("hull");
				if (!item.isObject() || poseObject.size() != (shaped ? 4 : 3) || !readPose(poseObject, &pose) || (shaped && !hasHull))
					return fail(error, Text::tr("A collision pose has malformed or unsupported fields."));
				if (shaped)
				{
					const auto points = poseObject.value("hull").toArray();
					if (!poseObject.value("hull").isArray() || points.size() < 4 || points.size() > modelCollisionHullMaxPoints)
						return fail(error, Text::tr("A collision hull needs 4 to 256 points."));
					for (const auto &value : points)
					{
						ModelVec3 point;
						if (!vector(value, &point))
							return fail(error, Text::tr("A collision hull point is malformed."));
						pose.hull << point;
					}
				}
				poses << pose;
			}
			setModelCollisionFrames(&box, std::move(poses));
		}
		else
		{
			ModelCollisionPose pose;
			if (object.size() != 4 + extra || !readPose(object, &pose))
				return fail(error, Text::tr("A collision box has malformed or unsupported fields."));
			box = {box.name, pose.centre, pose.size, pose.rotation};
		}
		box.hull = std::move(hull);
		boxes << box;
	}
	ModelMesh candidate;
	candidate.frames = mesh->frames;
	candidate.collisionBoxes = boxes;
	const auto errors = validateModelCollision(candidate, control);
	if (!errors.isEmpty())
	{
		return fail(error, errors.join('\n'));
	}
	if (!work.check())
		return false;
	mesh->collisionBoxes = std::move(boxes);
	return true;
}
QString modelCollisionOmissionNote()
{
	return Text::tr("Collision boxes remain in the editable mesh source. Native model formats do not export these volumes. Export or "
					"place collision brushes separately and compile the map.");
}
bool exportModelCollisionMap(const ModelMesh &mesh, const ModelCollisionExport &request, ModelCollisionMap *result, QString *error,
							 const ModelWorkControl &control, const QVector<LevelMapProperty> &groupProperties)
{
	if (error)
	{
		error->clear();
	}
	const auto errors = validateEditableModel(mesh, control);
	if (!errors.isEmpty())
	{
		return fail(error, errors.join('\n'));
	}
	if (!result || mesh.collisionBoxes.isEmpty())
	{
		return fail(error, Text::tr("Add at least one collision box before exporting or placing collision."));
	}
	if (request.target != "quake" && request.target != "quake2" && request.target != "quake3")
	{
		return fail(error, Text::tr("Choose the collision target explicitly: quake, quake2 or quake3."));
	}
	if (!finite(request.origin, 32768))
	{
		return fail(error, Text::tr("The collision placement origin must be finite and within ±32768 units."));
	}
	std::array<double, 12> affine{1, 0, 0, request.origin.x, 0, 1, 0, request.origin.y, 0, 0, 1, request.origin.z};
	if (request.instance)
	{
		affine = *request.instance;
		const bool usable = std::all_of(affine.cbegin(), affine.cend(), [](double v) { return std::isfinite(v) && std::abs(v) <= 1e6; });
		// The scale must not collapse the volume; a mirrored instance is fine,
		// the hull is rebuilt from the placed corners.
		const double determinant = affine[0] * (affine[5] * affine[10] - affine[6] * affine[9]) -
								   affine[1] * (affine[4] * affine[10] - affine[6] * affine[8]) +
								   affine[2] * (affine[4] * affine[9] - affine[5] * affine[8]);
		if (request.origin.x != 0 || request.origin.y != 0 || request.origin.z != 0 || !usable || std::abs(determinant) < 1e-9)
			return fail(error, Text::tr("A model instance placement needs a finite instance transform with a nonzero scale, and no separate "
										"origin."));
	}
	const bool animated =
		std::any_of(mesh.collisionBoxes.cbegin(), mesh.collisionBoxes.cend(), [](const auto &box) { return !box.framePoses.isEmpty(); });
	if (request.frame < -1 || request.frame >= mesh.frames.size() || (animated && request.frame < 0))
		return fail(
			error,
			Text::tr("Animated collision map handoff requires an explicit existing frame. The map stores that pose as static brushes."));
	ModelCollisionMap candidate;
	// Public compiler contracts, GPL-2.0-or-later, reviewed 2026-10-05:
	// ericw-tools qbsp/brush.cc, f80b1e216a415581aea7475cb52b16b8c4859084
	// (Quake clip hulls); Quake-2 game/q_shared.h
	// (CONTENTS_PLAYERCLIP); Quake-III-Arena q3map/map.c (shader contents).
	// https://github.com/id-Software/Quake-III-Arena/blob/master/q3map/map.c
	// Original VibeStudio serialization; upstream code is not copied.
	candidate.material = request.target == "quake3" ? request.material : QStringLiteral("clip");
	static const QRegularExpression material(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_./-]{0,53}$"));
	if ((request.target != "quake3" && !request.material.isEmpty()) || !material.match(candidate.material).hasMatch() ||
		candidate.material.contains("..") || candidate.material.contains("//") ||
		candidate.material.startsWith("textures/", Qt::CaseInsensitive))
	{
		return fail(error, Text::tr("Only Quake III accepts a material: choose a clip shader path below textures/ (omit that prefix), up "
									"to 54 ASCII path characters."));
	}
	if (groupProperties.isEmpty())
		candidate.notes << Text::tr("These are static world collision brushes. Animation, tags and model entities are not linked to them; move "
									"or regenerate the brushes when the prop changes. Save and compile the map.");
	else
		candidate.notes << Text::tr("These static collision brushes form one func_group that records the model source, collision revision, "
									"frame and placement. Refresh them from the modeller when the model's collision changes. The compiler "
									"merges func_group into the world. Save and compile the map.");
	if (request.instance)
		candidate.notes << Text::tr("Placed through the model instance's transform: origin, angles or angle, and modelscale or "
									"modelscale_vec, as level preview draws the instance.");
	if (animated)
		candidate.notes << Text::tr("Animated collision sampled at stored frame %1 (%2). No collision animation is exported.")
							   .arg(request.frame)
							   .arg(mesh.frames[request.frame].name);
	if (request.target == "quake3")
	{
		candidate.notes << Text::tr(
			"Quake III requires the selected shader in the compiler's assets with surfaceparm playerclip and nodraw. Numeric map flags "
			"cannot supply clip behavior. Shader availability and in-game collision are not verified by this export.");
	}
	else if (request.target == "quake2")
	{
		candidate.notes << Text::tr(
			"Quake II brushes use PLAYERCLIP contents (65536), for player movement. They do not add projectile or monster collision.");
	}
	else
	{
		candidate.notes << Text::tr(
			"Quake clip brushes contribute to expanded collision hulls. They do not add render surfaces or point-trace collision.");
	}
	QString text = QStringLiteral("// VibeStudio static collision brushes: %1\n{\n").arg(request.target);
	if (groupProperties.isEmpty())
		text += QStringLiteral("\"classname\" \"worldspawn\"\n");
	else
	{
		text += QStringLiteral("\"classname\" \"func_group\"\n");
		for (const auto &property : groupProperties)
		{
			if (property.key.contains('"') || property.value.contains('"') || property.key.contains('\n') ||
				property.value.contains('\n') || property.value.contains('\r'))
				return fail(error, Text::tr("Collision records cannot contain quotes or line breaks."));
			text += QStringLiteral("\"%1\" \"%2\"\n").arg(property.key, property.value);
		}
	}
	for (const auto &box : mesh.collisionBoxes)
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Serializing, candidate.brushCount, mesh.collisionBoxes.size(), error))
		{
			return false;
		}
		ModelCollisionBox pose;
		if (!sampleModelCollisionBox(box, std::max(0, request.frame), std::max(0, request.frame), 0, &pose))
			return fail(error, Text::tr("The requested collision pose is unavailable."));
		QVector<LevelMapVec3> points;
		for (const auto p : modelCollisionPoints(pose))
		{
			// An affine image of a box is a convex parallelepiped, so its eight
			// corners still span one closed convex brush.
			const auto placed = applyLevelModelInstanceAffine(affine, p.x, p.y, p.z);
			if (std::max({std::abs(placed.x), std::abs(placed.y), std::abs(placed.z)}) > 32768)
			{
				return fail(error, Text::tr("Collision placement would exceed ±32768 world units."));
			}
			points << placed;
		}
		LevelMapBrush brush;
		// A hull's points are its vertices; the envelope keeps the convex solid.
		if (!(isModelCollisionHull(pose) ? createLevelBrushEnvelope(points, candidate.material, &brush, error)
										 : createLevelBrushHull(points, candidate.material, &brush, error)))
		{
			return false;
		}
		text += QStringLiteral("{\n");
		for (const auto &face : brush.faces)
		{
			text += QStringLiteral("%1 %2 %3 %4 0 0 0 1 1").arg(point(face.p0), point(face.p1), point(face.p2), candidate.material);
			if (request.target != "quake")
			{
				text += request.target == "quake2" ? QStringLiteral(" 65536 0 0") : QStringLiteral(" 0 0 0");
			}
			text += '\n';
		}
		text += QStringLiteral("}\n");
		++candidate.brushCount;
	}
	text += QStringLiteral("}\n");
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Serializing, candidate.brushCount, mesh.collisionBoxes.size(), error))
	{
		return false;
	}
	candidate.bytes = text.toUtf8();
	*result = std::move(candidate);
	return true;
}
LevelPlacementResult prepareModelCollisionPlacement(const ModelMesh &mesh, const ModelCollisionExport &request, const LevelMapDocument &map,
													const ModelWorkControl &control)
{
	LevelPlacementResult result;
	const bool q3 = map.format == LevelMapFormat::Quake3Map;
	if ((map.format != LevelMapFormat::QuakeMap && !q3) || q3 != (request.target == "quake3"))
	{
		result.error = Text::tr("The collision export target must match the open Quake-family map.");
		return result;
	}
	ModelCollisionMap exported;
	// One func_group records the source, revision, frame and transform
	// (core/model_collision_placement.h), so the set can be found and refreshed.
	if (!exportModelCollisionMap(mesh, request, &exported, &result.error, control, modelCollisionRecordProperties(mesh, request, map)))
	{
		result.cancelled = control.cancelled && control.cancelled();
		return result;
	}
	LevelPlacementRequest placement;
	placement.operation = LevelPlacementOperation::Paste;
	placement.text = QString::fromUtf8(exported.bytes);
	return prepareLevelPlacement(map, placement, {control.cancelled, [progress = control.progress](const LevelPlacementProgress &p) {
													  if (progress)
													  {
														  progress(ModelWorkPhase::Editing, p.completed, p.total);
													  }
												  }});
}
} // namespace vibestudio
