#include "core/model_transform_pieces.h"

#include "core/model_document.h"
#include "core/model_geometric_topology.h"
#include "core/model_geometry_helpers.h"
#include "core/model_surface_selection.h"

#include <QCoreApplication>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace vibestudio
{
namespace
{
using namespace model_geometry;
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelTransformPieces)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
bool finitePoint(P3 p)
{
	return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
int findRoot(QVector<int> &parent, int v)
{
	while (parent[v] != v)
	{
		parent[v] = parent[parent[v]];
		v = parent[v];
	}
	return v;
}
void unite(QVector<int> &parent, int a, int b)
{
	a = findRoot(parent, a);
	b = findRoot(parent, b);
	if (a != b)
		parent[std::max(a, b)] = std::min(a, b);
}
} // namespace

bool ModelTransformPiece::operator==(const ModelTransformPiece &other) const
{
	if (vertices != other.vertices || ownAxes != other.ownAxes || pivot.x != other.pivot.x || pivot.y != other.pivot.y ||
		pivot.z != other.pivot.z || basis.world != other.basis.world)
		return false;
	for (int i = 0; i < 3; ++i)
	{
		if (basis.axes[i].x != other.basis.axes[i].x || basis.axes[i].y != other.basis.axes[i].y || basis.axes[i].z != other.basis.axes[i].z)
			return false;
	}
	return true;
}

bool modelTransformPieces(const ModelSurface &surface, const QSet<int> &vertices, int pivotFrame, QVector<ModelTransformPiece> *pieces,
						  QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!pieces)
		return fail(error, Text::tr("No transform pieces output was provided."));
	if (vertices.isEmpty())
		return fail(error, Text::tr("Select at least one vertex, edge, or face."));
	if (pivotFrame < 0 || pivotFrame >= surface.frames.size())
		return fail(error, Text::tr("Choose a valid transform pivot and reference frame."));
	const auto &positions = surface.frames[pivotFrame].positions;
	for (int v : vertices)
	{
		if (v < 0 || v >= positions.size())
			return fail(error, Text::tr("The selection names a vertex the surface does not have."));
	}
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error, control))
		return false;
	QVector<int> parent(surface.vertexCount);
	std::iota(parent.begin(), parent.end(), 0);
	// Seam copies at one position move as one vertex.
	for (int v : vertices)
	{
		const int group = topology.group(v);
		if (group >= 0)
		{
			for (int copy : topology.copies.value(group))
			{
				if (vertices.contains(copy))
					unite(parent, v, copy);
			}
		}
	}
	for (const auto &t : surface.triangles)
	{
		if (!work.step())
			return false;
		const std::array<int, 3> corners{t.a, t.b, t.c};
		for (int i = 0; i < 3; ++i)
		{
			const int a = corners[i], b = corners[(i + 1) % 3];
			if (vertices.contains(a) && vertices.contains(b))
				unite(parent, a, b);
		}
	}
	auto ordered = vertices.values();
	std::sort(ordered.begin(), ordered.end());
	QHash<int, int> pieceOfRoot;
	QVector<ModelTransformPiece> result;
	QVector<std::array<double, 6>> bounds;
	for (int v : std::as_const(ordered))
	{
		if (!work.step())
			return false;
		const int root = findRoot(parent, v);
		auto found = pieceOfRoot.constFind(root);
		int index = found == pieceOfRoot.cend() ? -1 : found.value();
		if (index < 0)
		{
			index = result.size();
			pieceOfRoot.insert(root, index);
			result.append(ModelTransformPiece{});
			bounds.append(std::array<double, 6>{INFINITY, INFINITY, INFINITY, -INFINITY, -INFINITY, -INFINITY});
		}
		const auto p = point(positions[v]);
		if (!finitePoint(p))
			return fail(error, Text::tr("Selected positions must be finite."));
		result[index].vertices.append(v);
		auto &box = bounds[index];
		const double values[3]{p.x, p.y, p.z};
		for (int axis = 0; axis < 3; ++axis)
		{
			box[axis] = std::min(box[axis], values[axis]);
			box[axis + 3] = std::max(box[axis + 3], values[axis]);
		}
	}
	for (int i = 0; i < result.size(); ++i)
	{
		const auto &box = bounds[i];
		result[i].pivot = {float((box[0] + box[3]) / 2), float((box[1] + box[4]) / 2), float((box[2] + box[5]) / 2)};
	}
	*pieces = std::move(result);
	return work.check();
}

bool modelNormalTransformBasis(const ModelSurface &surface, int frame, const QSet<int> &vertices, ModelTransformBasis *result)
{
	if (!result || vertices.isEmpty() || frame < 0 || frame >= surface.frames.size())
		return false;
	const auto &pose = surface.frames[frame];
	for (int v : vertices)
	{
		if (v < 0 || v >= pose.positions.size())
			return false;
	}
	// Z: the area-weighted normal of the faces inside the selection.
	P3 z{0, 0, 0};
	double area = 0;
	QHash<ModelEdge, int> faceUses;
	for (const auto &t : surface.triangles)
	{
		if (!vertices.contains(t.a) || !vertices.contains(t.b) || !vertices.contains(t.c))
			continue;
		const P3 a = point(pose.positions[t.a]), b = point(pose.positions[t.b]), c = point(pose.positions[t.c]);
		const P3 n = cross(b - a, c - a);
		z = z + n;
		area += length(n);
		for (const auto edge : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
			++faceUses[edge];
	}
	// Faces that cancel out (a closed shape) have no facing direction; neither
	// do vertex normals that cancel out.
	if (!finitePoint(z) || !std::isfinite(area) || area <= 0 || length(z) <= 1e-6 * area)
	{
		z = {0, 0, 0};
		double total = 0;
		if (pose.normals.size() == pose.positions.size())
		{
			for (int v : vertices)
			{
				const auto n = point(pose.normals[v]);
				z = z + n;
				total += length(n);
			}
		}
		if (!finitePoint(z) || !std::isfinite(total) || total <= 0 || length(z) <= 1e-4 * total)
			return false;
	}
	const double zLength = length(z);
	if (!finitePoint(z) || zLength <= 1e-12)
		return false;
	z = z * (1.0 / zLength);
	// X: the longest border edge of those faces, or the longest selected edge.
	QSet<ModelEdge> candidates;
	for (auto i = faceUses.cbegin(); i != faceUses.cend(); ++i)
	{
		if (i.value() == 1)
			candidates.insert(i.key());
	}
	if (candidates.isEmpty())
	{
		for (const auto &t : surface.triangles)
		{
			for (const auto edge : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
			{
				if (vertices.contains(edge.first) && vertices.contains(edge.second))
					candidates.insert(edge);
			}
		}
	}
	P3 x{0, 0, 0};
	double best = 0;
	ModelEdge chosen{-1, -1};
	for (const auto edge : std::as_const(candidates))
	{
		P3 direction = point(pose.positions[edge.second]) - point(pose.positions[edge.first]);
		const double full = length(direction);
		direction = direction - z * dot(direction, z);
		const double projected = length(direction);
		if (!std::isfinite(projected) || projected <= 1e-6 * std::max(full, 1e-12))
			continue;
		// Longest first; equal lengths fall to the lowest index pair.
		if (projected > best * (1 + 1e-9) || (std::abs(projected - best) <= best * 1e-9 && edge < chosen))
		{
			best = projected;
			chosen = edge;
			x = direction * (1.0 / projected);
		}
	}
	if (best <= 0)
	{
		// The world axis least aligned with Z, lowest axis first.
		const P3 axes[3]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
		int axis = 0;
		for (int i = 1; i < 3; ++i)
		{
			if (std::abs(dot(axes[i], z)) < std::abs(dot(axes[axis], z)) - 1e-12)
				axis = i;
		}
		x = axes[axis] - z * dot(axes[axis], z);
		const double l = length(x);
		if (l <= 1e-9)
			return false;
		x = x * (1.0 / l);
	}
	const P3 y = cross(z, x);
	ModelTransformBasis basis;
	basis.world = false;
	basis.axes = {{{float(x.x), float(x.y), float(x.z)}, {float(y.x), float(y.y), float(y.z)}, {float(z.x), float(z.y), float(z.z)}}};
	if (!validModelTransformBasis(basis))
		return false;
	*result = basis;
	return true;
}

ModelTransform modelPieceTransform(const ModelTransform &transform, const ModelTransformPiece &piece)
{
	auto result = transform;
	result.pivot = piece.pivot;
	if (piece.ownAxes)
		result.basis = piece.basis;
	return result;
}

ModelTransform ModelTransformPieceMap::transformFor(const ModelTransform &transform, int surface, int vertex) const
{
	if (pieces.isEmpty())
		return transform;
	const auto found = pieceOf.constFind(wholeSurfaces ? surface : vertex);
	return found == pieceOf.cend() ? transform : modelPieceTransform(transform, pieces[found.value()]);
}

bool modelTransformPieceMap(const ModelMesh &mesh, int surface, const QSet<int> &vertices, const QSet<int> &surfaces, int frame,
							bool normalAxes, ModelTransformPieceMap *map)
{
	if (!map)
		return false;
	ModelTransformPieceMap result;
	if (!surfaces.isEmpty())
	{
		result.wholeSurfaces = true;
		auto ordered = surfaces.values();
		std::sort(ordered.begin(), ordered.end());
		for (int index : std::as_const(ordered))
		{
			if (index < 0 || index >= mesh.surfaces.size())
				return false;
			ModelTransformPiece piece;
			if (!modelSurfaceSelectionPivot(mesh, {index}, frame, ModelTransformPivot::SelectionCentre, {}, &piece.pivot))
				return false;
			const auto &part = mesh.surfaces[index];
			if (normalAxes)
			{
				QSet<int> everything;
				for (int v = 0; v < part.vertexCount; ++v)
					everything.insert(v);
				piece.ownAxes = modelNormalTransformBasis(part, frame, everything, &piece.basis);
			}
			result.pieceOf.insert(index, result.pieces.size());
			result.pieces.append(piece);
		}
		*map = std::move(result);
		return true;
	}
	if (surface < 0 || surface >= mesh.surfaces.size() ||
		!modelTransformPieces(mesh.surfaces[surface], vertices, frame, &result.pieces))
		return false;
	for (int i = 0; i < result.pieces.size(); ++i)
	{
		auto &piece = result.pieces[i];
		if (normalAxes)
		{
			const QSet<int> members(piece.vertices.cbegin(), piece.vertices.cend());
			piece.ownAxes = modelNormalTransformBasis(mesh.surfaces[surface], frame, members, &piece.basis);
		}
		for (int v : std::as_const(piece.vertices))
			result.pieceOf.insert(v, i);
	}
	*map = std::move(result);
	return true;
}

bool modelPieceTransforms(const ModelSurface &surface, const ModelEdit &edit, const ModelTransform &transform, const QSet<int> &vertices,
						  int pivotFrame, QVector<ModelTransform> *transforms, QHash<int, int> *pieceOf, QString *error,
						  const ModelWorkControl &control)
{
	if (!transforms || !pieceOf)
		return fail(error, Text::tr("No transform pieces output was provided."));
	pieceOf->clear();
	if (edit.pivotMode != ModelTransformPivot::IndividualOrigins)
	{
		*transforms = {transform};
		return true;
	}
	QVector<ModelTransformPiece> pieces;
	if (!modelTransformPieces(surface, vertices, pivotFrame, &pieces, error, control))
		return false;
	// Normal axes come from the pose that places the axes, like Selection axes.
	const int axesFrame = edit.axesFrame >= 0 ? edit.axesFrame : pivotFrame;
	QVector<ModelTransform> result;
	for (int i = 0; i < pieces.size(); ++i)
	{
		auto &piece = pieces[i];
		// A piece without one facing direction (a closed shape) keeps the
		// selection's axes.
		if (edit.transformSpace == ModelTransformSpace::Normal)
		{
			const QSet<int> members(piece.vertices.cbegin(), piece.vertices.cend());
			piece.ownAxes = modelNormalTransformBasis(surface, axesFrame, members, &piece.basis);
		}
		result.append(modelPieceTransform(transform, piece));
		for (int v : std::as_const(piece.vertices))
			pieceOf->insert(v, i);
	}
	*transforms = std::move(result);
	return true;
}
} // namespace vibestudio
