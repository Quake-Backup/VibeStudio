#include "core/model_tag_follow.h"

#include "core/model_document.h"
#include "core/model_mesh_tools_p.h"
#include "core/model_tags.h"

#include <QCoreApplication>
#include <QJsonObject>

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelTagFollow)
};

bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}

using Vec = std::array<double, 3>;
using Mat = std::array<double, 9>; // row-major

Vec vec(ModelVec3 p) { return {p.x, p.y, p.z}; }
Vec sub(Vec a, Vec b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec add(Vec a, Vec b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec scaled(Vec a, double s) { return {a[0] * s, a[1] * s, a[2] * s}; }
double dot(Vec a, Vec b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec cross(Vec a, Vec b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
double length(Vec a) { return std::sqrt(dot(a, a)); }
Vec rotateVector(const Mat &m, Vec v)
{
	return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2], m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}
Mat identity() { return {1, 0, 0, 0, 1, 0, 0, 0, 1}; }

// Eigen-decomposition of a symmetric N x N matrix by cyclic Jacobi
// rotations; `values` ascending order is not guaranteed.
template <int N>
void jacobi(std::array<double, N * N> a, std::array<double, N> *values, std::array<double, N * N> *vectors)
{
	auto &v = *vectors;
	v.fill(0);
	for (int i = 0; i < N; ++i)
		v[i * N + i] = 1;
	for (int sweep = 0; sweep < 64; ++sweep)
	{
		double off = 0;
		for (int p = 0; p < N; ++p)
			for (int q = p + 1; q < N; ++q)
				off += a[p * N + q] * a[p * N + q];
		if (off < 1e-30)
			break;
		for (int p = 0; p < N; ++p)
			for (int q = p + 1; q < N; ++q)
			{
				const double apq = a[p * N + q];
				if (std::abs(apq) < 1e-300)
					continue;
				const double theta = (a[q * N + q] - a[p * N + p]) / (2 * apq);
				const double t = (theta >= 0 ? 1.0 : -1.0) / (std::abs(theta) + std::sqrt(theta * theta + 1));
				const double c = 1 / std::sqrt(t * t + 1), s = t * c;
				for (int k = 0; k < N; ++k)
				{
					const double akp = a[k * N + p], akq = a[k * N + q];
					a[k * N + p] = c * akp - s * akq;
					a[k * N + q] = s * akp + c * akq;
				}
				for (int k = 0; k < N; ++k)
				{
					const double apk = a[p * N + k], aqk = a[q * N + k];
					a[p * N + k] = c * apk - s * aqk;
					a[q * N + k] = s * apk + c * aqk;
				}
				for (int k = 0; k < N; ++k)
				{
					const double vkp = v[k * N + p], vkq = v[k * N + q];
					v[k * N + p] = c * vkp - s * vkq;
					v[k * N + q] = s * vkp + c * vkq;
				}
			}
	}
	for (int i = 0; i < N; ++i)
		(*values)[i] = a[i * N + i];
}

// Horn (1987): the proper rotation R minimising sum |R a_i - b_i|^2 for
// centred point sets, from the dominant eigenvector of a 4 x 4 matrix.
// False when the top eigenvalue is not unique (collinear or empty sets).
bool hornRotation(const QVector<Vec> &a, const QVector<Vec> &b, Mat *rotation)
{
	double s[3][3] = {};
	for (int i = 0; i < a.size(); ++i)
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c)
				s[r][c] += a[i][r] * b[i][c];
	const double sxx = s[0][0], sxy = s[0][1], sxz = s[0][2], syx = s[1][0], syy = s[1][1], syz = s[1][2], szx = s[2][0], szy = s[2][1],
				 szz = s[2][2];
	const std::array<double, 16> n{sxx + syy + szz, syz - szy,		  szx - sxz,		sxy - syx,		  syz - szy,		sxx - syy - szz,
								   sxy + syx,		szx + sxz,		  szx - sxz,		sxy + syx,		  -sxx + syy - szz, syz + szy,
								   sxy - syx,		szx + sxz,		  syz + szy,		-sxx - syy + szz};
	std::array<double, 4> values{};
	std::array<double, 16> vectors{};
	jacobi<4>(n, &values, &vectors);
	int best = 0;
	for (int i = 1; i < 4; ++i)
		if (values[i] > values[best])
			best = i;
	double second = -1e300;
	for (int i = 0; i < 4; ++i)
		if (i != best)
			second = std::max(second, values[i]);
	const double scale = std::max({std::abs(values[0]), std::abs(values[1]), std::abs(values[2]), std::abs(values[3]), 1e-30});
	if (values[best] - second <= 1e-9 * scale)
		return false;
	double w = vectors[0 * 4 + best], x = vectors[1 * 4 + best], y = vectors[2 * 4 + best], z = vectors[3 * 4 + best];
	const double norm = std::sqrt(w * w + x * x + y * y + z * z);
	w /= norm;
	x /= norm;
	y /= norm;
	z /= norm;
	*rotation = {1 - 2 * (y * y + z * z), 2 * (x * y - w * z),	   2 * (x * z + w * y),
				 2 * (x * y + w * z),	  1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
				 2 * (x * z - w * y),	  2 * (y * z + w * x),	   1 - 2 * (x * x + y * y)};
	return true;
}

// The largest and second-largest spreads of a centred point set.
bool spread(const QVector<Vec> &points)
{
	std::array<double, 9> c{};
	for (const auto &p : points)
		for (int r = 0; r < 3; ++r)
			for (int k = 0; k < 3; ++k)
				c[r * 3 + k] += p[r] * p[k];
	std::array<double, 3> values{};
	std::array<double, 9> vectors{};
	jacobi<3>(c, &values, &vectors);
	std::sort(values.begin(), values.end());
	return values[2] > 1e-18 && values[1] > 1e-10 * values[2];
}

bool surfaceBasis(const ModelSurface &surface, const QVector<int> &faces, int frame, Mat *basis)
{
	const auto &positions = surface.frames[frame].positions;
	Vec normal{};
	for (int face : faces)
	{
		const auto &t = surface.triangles[face];
		const auto a = vec(positions[t.a]), b = vec(positions[t.b]), c = vec(positions[t.c]);
		normal = add(normal, cross(sub(b, a), sub(c, a)));
	}
	const double size = length(normal);
	if (size < 1e-12)
		return false;
	normal = scaled(normal, 1 / size);
	const auto &first = surface.triangles[faces.first()];
	auto edge = sub(vec(positions[first.b]), vec(positions[first.a]));
	edge = sub(edge, scaled(normal, dot(edge, normal)));
	const double edgeLength = length(edge);
	if (edgeLength < 1e-12)
		return false;
	edge = scaled(edge, 1 / edgeLength);
	const auto side = cross(normal, edge);
	*basis = {edge[0], side[0], normal[0], edge[1], side[1], normal[1], edge[2], side[2], normal[2]};
	return true;
}
Mat multiplyTransposed(const Mat &a, const Mat &b) // a * b^T
{
	Mat result{};
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c)
			result[r * 3 + c] = a[r * 3 + 0] * b[c * 3 + 0] + a[r * 3 + 1] * b[c * 3 + 1] + a[r * 3 + 2] * b[c * 3 + 2];
	return result;
}
bool nativeName(const QString &name)
{
	return !name.isEmpty() && name.size() <= 63 && name == name.trimmed() &&
		   std::all_of(name.cbegin(), name.cend(), [](QChar c) { return c.unicode() >= 32 && c.unicode() <= 126; });
}
} // namespace

bool solveModelTagFollow(const ModelMesh &mesh, const ModelSelection &selection, const ModelTagFollowOptions &options,
						 ModelTagFollowResult *result, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!result || !work.check())
		return false;
	*result = {};
	const int frames = int(mesh.frames.size());
	if (!nativeName(options.tag))
		return fail(error, Text::tr("Name the tag with 1 to 63 printable ASCII characters, without surrounding spaces."));
	if (options.referenceFrame < 0 || options.referenceFrame >= frames)
		return fail(error, Text::tr("Choose an existing reference frame."));
	const bool all = options.first == -1 && options.last == -1;
	const int first = all ? 0 : options.first, last = all ? frames - 1 : options.last;
	if (first < 0 || last < first || last >= frames)
		return fail(error, Text::tr("Choose a frame range inside the model, or every frame."));
	if (int(options.mode) < 0 || int(options.mode) > int(ModelTagFollowMode::Surface))
		return fail(error, Text::tr("Choose rigid, centroid or surface following."));
	if (selection.surface < 0 || selection.surface >= mesh.surfaces.size() || !selection.tag.isEmpty() || !selection.collision.isEmpty() ||
		!selection.joint.isEmpty() || !selection.surfaces.isEmpty())
		return fail(error, Text::tr("Select the vertices, edges or faces the tag should follow."));
	const auto &surface = mesh.surfaces[selection.surface];
	const auto chosen = model_tools::selectedVertexSet(surface, selection);
	auto vertices = chosen.values();
	std::sort(vertices.begin(), vertices.end());
	if (vertices.isEmpty())
		return fail(error, Text::tr("Select the vertices, edges or faces the tag should follow."));
	QVector<int> faces = selection.faces.values().toVector();
	std::sort(faces.begin(), faces.end());
	if (options.mode == ModelTagFollowMode::Surface && faces.isEmpty())
		return fail(error, Text::tr("Surface following needs selected faces."));
	const auto centroid = [&](int frame)
	{
		Vec sum{};
		for (int v : vertices)
			sum = add(sum, vec(surface.frames[frame].positions[v]));
		return scaled(sum, 1.0 / vertices.size());
	};
	const int reference = options.referenceFrame;
	const Vec origin = centroid(reference);
	QVector<Vec> referencePoints;
	for (int v : vertices)
		referencePoints.append(sub(vec(surface.frames[reference].positions[v]), origin));
	if (options.mode != ModelTagFollowMode::Centroid && !spread(referencePoints))
		return fail(error, Text::tr("Rigid and surface following need at least three selected points that are not in a line."));
	Mat referenceBasis = identity();
	if (options.mode == ModelTagFollowMode::Surface && !surfaceBasis(surface, faces, reference, &referenceBasis))
		return fail(error, Text::tr("The selected faces have no area or direction in reference frame %1.").arg(reference));
	ModelTag base;
	if (const auto *existing = findModelTag(mesh, options.tag, reference))
		base = *existing;
	else
	{
		if (mesh.tagCount >= 16 || modelTagNames(mesh).size() >= 16)
			return fail(error, Text::tr("A model holds at most 16 attachment tags."));
		base.name = options.tag;
		base.origin = {float(origin[0]), float(origin[1]), float(origin[2])};
		result->created = true;
	}
	result->start = base;
	result->start.frameIndex = reference;
	const Vec tagOrigin = sub(vec(base.origin), origin);
	result->points = int(vertices.size());
	for (int frame = first; frame <= last; ++frame)
	{
		if (!work.step())
			return false;
		const Vec centre = centroid(frame);
		QVector<Vec> current;
		for (int v : vertices)
			current.append(sub(vec(surface.frames[frame].positions[v]), centre));
		Mat rotation = identity();
		if (options.mode == ModelTagFollowMode::Rigid && !hornRotation(referencePoints, current, &rotation))
			return fail(error, Text::tr("In frame %1 the selected points collapse onto a line, so their rotation is undefined. No tag "
										"was changed.")
								   .arg(frame));
		if (options.mode == ModelTagFollowMode::Surface)
		{
			Mat basis{};
			if (!surfaceBasis(surface, faces, frame, &basis))
				return fail(error, Text::tr("The selected faces have no area or direction in frame %1. No tag was changed.").arg(frame));
			rotation = multiplyTransposed(basis, referenceBasis);
		}
		double squared = 0;
		for (int i = 0; i < current.size(); ++i)
		{
			const auto moved = rotateVector(rotation, referencePoints[i]);
			const auto difference = sub(moved, current[i]);
			squared += dot(difference, difference);
		}
		const double residual = std::sqrt(squared / current.size());
		ModelTag pose = base;
		pose.frameIndex = frame;
		const auto placed = add(rotateVector(rotation, tagOrigin), centre);
		pose.origin = {float(placed[0]), float(placed[1]), float(placed[2])};
		for (int row = 0; row < 3; ++row)
		{
			const auto axis = rotateVector(rotation, {base.axis[row * 3], base.axis[row * 3 + 1], base.axis[row * 3 + 2]});
			for (int c = 0; c < 3; ++c)
				pose.axis[row * 3 + c] = float(axis[c]);
		}
		result->poses.append(pose);
		result->residuals.append(residual);
		if (result->worstFrame < 0 || residual > result->maxResidual)
		{
			result->maxResidual = residual;
			result->worstFrame = frame;
		}
	}
	return work.check();
}

QString modelTagFollowSummary(const ModelTagFollowOptions &options, const ModelTagFollowResult &result, const QLocale &locale)
{
	const int frames = int(result.poses.size());
	return Text::tr("%1 follows %2 points across %n frames; largest fit residual %3 model units in frame %4.", nullptr, frames)
		.arg(options.tag)
		.arg(locale.toString(result.points))
		.arg(locale.toString(result.maxResidual, 'g', 4))
		.arg(locale.toString(result.worstFrame));
}

QString modelTagAnchorProblem(const ModelMesh &mesh, const ModelTagAnchor &anchor)
{
	if (!findModelTag(mesh, anchor.tag, 0))
		return Text::tr("Anchored tag %1 no longer exists.").arg(anchor.tag);
	if (anchor.surface < 0 || anchor.surface >= mesh.surfaces.size())
		return Text::tr("The surface tag %1 follows no longer exists. Select its components and choose Follow Selection again.").arg(anchor.tag);
	const auto &surface = mesh.surfaces[anchor.surface];
	if (surface.name != anchor.surfaceName || surface.vertexCount != anchor.vertexCount || surface.triangles.size() != anchor.triangleCount)
		return Text::tr("Surface %1 gained or lost vertices or faces since tag %2 was fitted, so its components cannot be found again. "
						"Select them and choose Follow Selection again.")
			.arg(surface.name, anchor.tag);
	const int frames = int(mesh.frames.size());
	if (anchor.referenceFrame >= frames || anchor.last >= frames)
		return Text::tr("The frames tag %1 was fitted over no longer exist. Choose Follow Selection again.").arg(anchor.tag);
	return {};
}

bool solveModelTagAnchor(const ModelMesh &mesh, const ModelTagAnchor &anchor, ModelTagFollowResult *result, QString *error,
						 const ModelWorkControl &control)
{
	const auto problem = modelTagAnchorProblem(mesh, anchor);
	if (!problem.isEmpty())
		return fail(error, problem);
	ModelSelection selection;
	selection.surface = anchor.surface;
	selection.vertices = QSet<int>(anchor.vertices.cbegin(), anchor.vertices.cend());
	selection.faces = QSet<int>(anchor.faces.cbegin(), anchor.faces.cend());
	ModelTagFollowOptions options;
	options.tag = anchor.tag;
	options.mode = ModelTagFollowMode(anchor.mode);
	options.referenceFrame = anchor.referenceFrame;
	options.first = anchor.first;
	options.last = anchor.last;
	return solveModelTagFollow(mesh, selection, options, result, error, control);
}

QJsonArray modelTagAnchorsJson(const QVector<ModelTagAnchor> &anchors)
{
	static const char *const modes[]{"rigid", "centroid", "surface"};
	QJsonArray result;
	for (const auto &anchor : anchors)
	{
		QJsonArray vertices, faces;
		for (int vertex : anchor.vertices)
			vertices.append(vertex);
		for (int face : anchor.faces)
			faces.append(face);
		result.append(QJsonObject{{QStringLiteral("tag"), anchor.tag},
								  {QStringLiteral("surface"), anchor.surface},
								  {QStringLiteral("surfaceName"), anchor.surfaceName},
								  {QStringLiteral("vertices"), vertices},
								  {QStringLiteral("faces"), faces},
								  {QStringLiteral("mode"), QString::fromLatin1(modes[std::clamp(anchor.mode, 0, 2)])},
								  {QStringLiteral("referenceFrame"), anchor.referenceFrame},
								  {QStringLiteral("first"), anchor.first},
								  {QStringLiteral("last"), anchor.last},
								  {QStringLiteral("vertexCount"), anchor.vertexCount},
								  {QStringLiteral("triangleCount"), anchor.triangleCount}});
	}
	return result;
}

bool parseModelTagAnchors(const QJsonValue &value, QVector<ModelTagAnchor> *anchors, QString *error)
{
	const auto malformed = [&] { return fail(error, Text::tr("The editable source's tag anchors are malformed.")); };
	if (!value.isArray() || value.toArray().size() > 16)
		return malformed();
	QVector<ModelTagAnchor> result;
	const auto integer = [](const QJsonValue &v, int minimum, int maximum, int *out)
	{
		if (!v.isDouble() || v.toDouble() != double(v.toInt(minimum - 1)) || v.toInt() < minimum || v.toInt() > maximum)
			return false;
		*out = v.toInt();
		return true;
	};
	for (const auto &item : value.toArray())
	{
		const auto object = item.toObject();
		ModelTagAnchor anchor;
		anchor.tag = object.value(QStringLiteral("tag")).toString();
		anchor.surfaceName = object.value(QStringLiteral("surfaceName")).toString();
		const auto mode = object.value(QStringLiteral("mode")).toString();
		anchor.mode = mode == QStringLiteral("rigid") ? 0 : mode == QStringLiteral("centroid") ? 1 : mode == QStringLiteral("surface") ? 2 : -1;
		if (!item.isObject() || !nativeName(anchor.tag) || anchor.mode < 0 || !object.value(QStringLiteral("surfaceName")).isString() ||
			!integer(object.value(QStringLiteral("surface")), 0, 31, &anchor.surface) ||
			!integer(object.value(QStringLiteral("referenceFrame")), 0, 1023, &anchor.referenceFrame) ||
			!integer(object.value(QStringLiteral("first")), -1, 1023, &anchor.first) ||
			!integer(object.value(QStringLiteral("last")), -1, 1023, &anchor.last) ||
			!integer(object.value(QStringLiteral("vertexCount")), 0, 65536, &anchor.vertexCount) ||
			!integer(object.value(QStringLiteral("triangleCount")), 0, 131072, &anchor.triangleCount) ||
			(anchor.first == -1) != (anchor.last == -1) || anchor.last < anchor.first)
			return malformed();
		for (const auto &[key, list, limit] : {std::tuple{"vertices", &anchor.vertices, anchor.vertexCount},
											   std::tuple{"faces", &anchor.faces, anchor.triangleCount}})
		{
			const auto array = object.value(QLatin1String(key)).toArray();
			if (!object.value(QLatin1String(key)).isArray() || array.size() > limit)
				return malformed();
			for (const auto &entry : array)
			{
				int index = 0;
				if (!integer(entry, 0, limit - 1, &index) || (!list->isEmpty() && index <= list->last()))
					return malformed();
				list->append(index);
			}
		}
		if (anchor.vertices.isEmpty() || std::any_of(result.cbegin(), result.cend(), [&](const ModelTagAnchor &other) { return other.tag == anchor.tag; }))
			return malformed();
		result.append(anchor);
	}
	*anchors = std::move(result);
	return true;
}

bool applyModelTagFollowEdit(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	const bool refit = edit.kind == ModelEditKind::RefitAnchoredTags;
	if (!candidate || (edit.kind != ModelEditKind::FollowTagComponents && !refit))
		return fail(error, Text::tr("Choose Follow Selection."));
	if (edit.frame != -1)
		return fail(error, Text::tr("Tag following uses its frame range, not one frame."));
	auto &tags = candidate->tags;
	const auto write = [&](const ModelTagFollowResult &result)
	{
		for (const auto &pose : std::as_const(result.poses))
		{
			const auto found = std::find_if(tags.begin(), tags.end(),
											[&](const ModelTag &tag) { return tag.name == pose.name && tag.frameIndex == pose.frameIndex; });
			if (found == tags.end())
				return fail(error, Text::tr("Attachment tag %1 is missing in frame %2.").arg(pose.name).arg(pose.frameIndex));
			*found = pose;
		}
		return true;
	};
	if (refit)
	{
		ModelTagFollowOptions only;
		only.tag = edit.tagFollow.tag;
		if (!(edit.tagFollow == only))
			return fail(error, Text::tr("Refit Anchored Tags takes only the name of one anchored tag, or none for every one."));
		QVector<ModelTagAnchor> chosen;
		for (const auto &anchor : std::as_const(candidate->tagAnchors))
			if (only.tag.isEmpty() || anchor.tag == only.tag)
				chosen.append(anchor);
		if (chosen.isEmpty())
			return fail(error, only.tag.isEmpty() ? Text::tr("No tag is anchored to mesh components yet. Fit one with Follow Selection first.")
												  : Text::tr("Tag %1 is not anchored to mesh components.").arg(only.tag));
		// Fit every anchor before changing any tag.
		QVector<ModelTagFollowResult> results;
		for (const auto &anchor : std::as_const(chosen))
		{
			ModelTagFollowResult result;
			if (!solveModelTagAnchor(*candidate, anchor, &result, error, control))
				return false;
			results.append(result);
		}
		for (const auto &result : std::as_const(results))
			if (!write(result))
				return false;
		return true;
	}
	ModelTagFollowResult result;
	if (!solveModelTagFollow(*candidate, edit.selection, edit.tagFollow, &result, error, control))
		return false;
	if (result.created)
	{
		// The new tag exists in every frame: fitted inside the range, its
		// starting pose elsewhere.
		for (int frame = 0; frame < candidate->frames.size(); ++frame)
		{
			auto tag = result.start;
			tag.frameIndex = frame;
			tags.append(tag);
		}
	}
	if (!write(result))
		return false;
	auto &anchors = candidate->tagAnchors;
	anchors.removeIf([&](const ModelTagAnchor &anchor) { return anchor.tag == edit.tagFollow.tag; });
	if (edit.tagFollow.keepAnchor)
	{
		const auto &surface = candidate->surfaces[edit.selection.surface];
		ModelTagAnchor anchor;
		anchor.tag = edit.tagFollow.tag;
		anchor.surface = edit.selection.surface;
		anchor.surfaceName = surface.name;
		const auto vertices = model_tools::selectedVertexSet(surface, edit.selection);
		anchor.vertices = QVector<int>(vertices.cbegin(), vertices.cend());
		std::sort(anchor.vertices.begin(), anchor.vertices.end());
		anchor.faces = QVector<int>(edit.selection.faces.cbegin(), edit.selection.faces.cend());
		std::sort(anchor.faces.begin(), anchor.faces.end());
		anchor.mode = int(edit.tagFollow.mode);
		anchor.referenceFrame = edit.tagFollow.referenceFrame;
		anchor.first = edit.tagFollow.first;
		anchor.last = edit.tagFollow.last;
		anchor.vertexCount = surface.vertexCount;
		anchor.triangleCount = int(surface.triangles.size());
		anchors.append(anchor);
	}
	return true;
}
} // namespace vibestudio
