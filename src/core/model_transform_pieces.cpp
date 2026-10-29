#include "core/model_transform_pieces.h"

#include "core/model_document.h"
#include "core/model_geometric_topology.h"
#include "core/model_geometry_helpers.h"
#include "core/model_mesh_tools.h"
#include "core/model_surface_selection.h"

#include <QCoreApplication>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <numeric>
#include <queue>

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

ModelVec3 ModelTransformPieceMap::apply(const ModelTransform &transform, int surface, int vertex, const ModelVec3 &point) const
{
	const auto found = wholeSurfaces ? weights.cend() : weights.constFind(vertex);
	if (found == weights.cend())
		return transformModelPoint(point, transformFor(transform, surface, vertex));
	const int piece = found->piece;
	return modelWeightedTransformPoint(point, piece >= 0 && piece < pieces.size() ? modelPieceTransform(transform, pieces[piece]) : transform,
									   *found);
}

ModelVec3 ModelTransformPieceMap::applyNormal(const ModelTransform &transform, int surface, int vertex, const ModelVec3 &normal) const
{
	const auto found = wholeSurfaces ? weights.cend() : weights.constFind(vertex);
	if (found == weights.cend())
		return transformModelNormal(normal, transformFor(transform, surface, vertex));
	const int piece = found->piece;
	return modelWeightedTransformNormal(normal, piece >= 0 && piece < pieces.size() ? modelPieceTransform(transform, pieces[piece]) : transform,
										*found);
}

namespace
{
P3 reflected(P3 p, int axes)
{
	if (axes & 1)
		p.x = -p.x;
	if (axes & 2)
		p.y = -p.y;
	if (axes & 4)
		p.z = -p.z;
	return p;
}
ModelTransform weighted(const ModelTransform &transform, double weight)
{
	ModelTransform t = transform;
	t.translation = {float(transform.translation.x * weight), float(transform.translation.y * weight), float(transform.translation.z * weight)};
	t.rotation = {float(transform.rotation.x * weight), float(transform.rotation.y * weight), float(transform.rotation.z * weight)};
	t.scale = {float(1 + (transform.scale.x - 1) * weight), float(1 + (transform.scale.y - 1) * weight), float(1 + (transform.scale.z - 1) * weight)};
	return t;
}
ModelVec3 vec(P3 p)
{
	return {float(p.x), float(p.y), float(p.z)};
}
P3 at(const ModelSurface &surface, int frame, int vertex)
{
	return point(surface.frames[frame].positions[vertex]);
}
} // namespace

ModelVec3 modelWeightedTransformPoint(const ModelVec3 &position, const ModelTransform &transform, const ModelTransformWeight &weight)
{
	const auto t = weighted(transform, weight.weight);
	const P3 p = point(position);
	if (weight.mirror != 0)
		return vec(reflected(point(transformModelPoint(vec(reflected(p, weight.mirror)), t)), weight.mirror));
	P3 result = point(transformModelPoint(position, t));
	if (weight.plane != 0)
	{
		// Vertices on an active mirror plane stay on it: average both reflections.
		const P3 mirror = reflected(point(transformModelPoint(vec(reflected(p, weight.plane)), t)), weight.plane);
		result = (result + mirror) * 0.5;
	}
	return vec(result);
}

ModelVec3 modelWeightedTransformNormal(const ModelVec3 &normal, const ModelTransform &transform, const ModelTransformWeight &weight)
{
	const auto t = weighted(transform, weight.weight);
	if (weight.mirror != 0)
		return vec(reflected(point(transformModelNormal(vec(reflected(point(normal), weight.mirror)), t)), weight.mirror));
	return transformModelNormal(normal, t);
}

bool modelTransformWeights(const ModelSurface &surface, const ModelGeometricTopology &topology, const QSet<int> &vertices, int reference,
						   const ModelMeshToolOptions &options, const QHash<int, int> &pieceOf, QHash<int, ModelTransformWeight> *result,
						   ModelWorkProgress &work)
{
	if (!result || reference < 0 || reference >= surface.frames.size())
		return false;
	result->clear();
	QHash<int, int> groupPiece;
	// Weight 1 for the selection; the proportional falloff for the rest.
	QSet<int> selectedGroups;
	for (int v : vertices)
	{
		selectedGroups.insert(topology.group(v));
		groupPiece.insert(topology.group(v), pieceOf.value(v));
	}
	QHash<int, double> weights;
	for (int group : selectedGroups)
		weights.insert(group, 1.0);
	const double radius = options.proportionalRadius;
	if (radius > 0)
	{
		QHash<int, double> distance;
		if (options.connectedOnly)
		{
			// Geodesic distance along edges from any selected vertex.
			QHash<int, QVector<int>> adjacency;
			for (auto it = topology.edgeFaces.cbegin(); it != topology.edgeFaces.cend(); ++it)
			{
				adjacency[it.key().first].append(it.key().second);
				adjacency[it.key().second].append(it.key().first);
			}
			using Entry = std::pair<double, int>;
			std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
			for (int group : selectedGroups)
			{
				distance.insert(group, 0);
				queue.push({0, group});
			}
			while (!queue.empty())
			{
				if (!work.step())
					return false;
				const auto [d, group] = queue.top();
				queue.pop();
				if (d > distance.value(group, INFINITY) || d >= radius)
					continue;
				for (int other : adjacency.value(group))
				{
					const double next = d + length(at(surface, reference, other) - at(surface, reference, group));
					if (next < distance.value(other, INFINITY) && next < radius)
					{
						distance.insert(other, next);
						if (!selectedGroups.contains(other))
							groupPiece.insert(other, groupPiece.value(group));
						queue.push({next, other});
					}
				}
			}
		}
		else
		{
			// Ascending groups, so equal distances fall to the lowest source.
			auto ordered = selectedGroups.values();
			std::sort(ordered.begin(), ordered.end());
			QVector<P3> sources;
			for (int group : std::as_const(ordered))
				sources.append(at(surface, reference, group));
			for (int v = 0; v < surface.vertexCount; ++v)
			{
				if (!work.step())
					return false;
				if (topology.group(v) != v || selectedGroups.contains(v))
					continue;
				const P3 p = at(surface, reference, v);
				double nearest = INFINITY;
				int source = -1;
				for (int i = 0; i < sources.size(); ++i)
				{
					const double d = length(p - sources[i]);
					if (d < nearest)
					{
						nearest = d;
						source = i;
					}
				}
				if (nearest < radius)
				{
					distance.insert(v, nearest);
					groupPiece.insert(v, groupPiece.value(ordered[source]));
				}
			}
		}
		for (auto it = distance.cbegin(); it != distance.cend(); ++it)
		{
			if (!selectedGroups.contains(it.key()))
			{
				const double w = modelFalloffWeight(options.falloff, it.value() / radius);
				if (w > 0)
					weights.insert(it.key(), w);
			}
		}
	}
	// Mirror counterparts take the reflected transform with the same weight.
	QHash<int, std::pair<int, double>> mirrored; // group -> (axes, weight)
	if (options.mirrorAxes != 0)
	{
		double scale = 0;
		for (const auto &p : surface.frames[reference].positions)
			scale = std::max({scale, double(std::abs(p.x)), double(std::abs(p.y)), double(std::abs(p.z))});
		const double tolerance = std::max(options.mergeThreshold, scale * 1e-6);
		// Grid cells twice the tolerance wide; a match lies in a neighbouring cell.
		const double cell = std::max(tolerance * 2, 1e-9);
		const auto cellOf = [&](P3 p)
		{ return std::array<qint64, 3>{qint64(std::floor(p.x / cell)), qint64(std::floor(p.y / cell)), qint64(std::floor(p.z / cell))}; };
		std::map<std::array<qint64, 3>, QVector<int>> grid;
		for (int v = 0; v < surface.vertexCount; ++v)
		{
			if (topology.group(v) == v)
				grid[cellOf(at(surface, reference, v))].append(v);
		}
		const auto weightedGroups = weights;
		for (auto it = weightedGroups.cbegin(); it != weightedGroups.cend(); ++it)
		{
			for (int axes = 1; axes <= 7; ++axes)
			{
				if ((axes & ~options.mirrorAxes) != 0)
					continue;
				const P3 target = reflected(at(surface, reference, it.key()), axes);
				int match = -1;
				double best = tolerance;
				const auto centre = cellOf(target);
				for (qint64 dx = -1; dx <= 1; ++dx)
				{
					for (qint64 dy = -1; dy <= 1; ++dy)
					{
						for (qint64 dz = -1; dz <= 1; ++dz)
						{
							const auto found = grid.find({centre[0] + dx, centre[1] + dy, centre[2] + dz});
							if (found == grid.end())
								continue;
							for (int candidate : found->second)
							{
								const double d = length(at(surface, reference, candidate) - target);
								if (d < best || (d == best && match >= 0 && candidate < match) || (d == best && match < 0))
								{
									best = d;
									match = candidate;
								}
							}
						}
					}
				}
				if (match < 0 || weights.contains(match) || mirrored.contains(match))
					continue;
				if (match == it.key())
					continue;
				mirrored.insert(match, {axes, it.value()});
				groupPiece.insert(match, groupPiece.value(it.key()));
			}
		}
	}
	const double planeTolerance = std::max(options.mergeThreshold, 1e-6);
	for (auto it = weights.cbegin(); it != weights.cend(); ++it)
	{
		ModelTransformWeight weight;
		weight.weight = it.value();
		weight.piece = groupPiece.value(it.key());
		const P3 p = at(surface, reference, it.key());
		if ((options.mirrorAxes & 1) && std::abs(p.x) <= planeTolerance)
			weight.plane |= 1;
		if ((options.mirrorAxes & 2) && std::abs(p.y) <= planeTolerance)
			weight.plane |= 2;
		if ((options.mirrorAxes & 4) && std::abs(p.z) <= planeTolerance)
			weight.plane |= 4;
		result->insert(it.key(), weight);
	}
	for (auto it = mirrored.cbegin(); it != mirrored.cend(); ++it)
	{
		ModelTransformWeight weight;
		weight.weight = it.value().second;
		weight.mirror = it.value().first;
		weight.piece = groupPiece.value(it.key());
		result->insert(it.key(), weight);
	}
	return true;
}

bool modelWeightedTransformPieceMap(const ModelMesh &mesh, int surface, const QSet<int> &vertices, int frame, bool individualOrigins,
									bool normalAxes, const ModelMeshToolOptions &options, ModelTransformPieceMap *map)
{
	if (!map || surface < 0 || surface >= mesh.surfaces.size())
		return false;
	const auto &part = mesh.surfaces[surface];
	if (frame < 0 || frame >= part.frames.size())
		return false;
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(part, &topology))
		return false;
	// Seam copies move with their group, as in the committed transform.
	QSet<int> expanded;
	for (int v : vertices)
	{
		const int group = topology.group(v);
		if (group < 0)
			return false;
		for (int copy : topology.copies[group])
			expanded.insert(copy);
	}
	if (expanded.isEmpty())
		return false;
	ModelTransformPieceMap result;
	if (individualOrigins && !modelTransformPieceMap(mesh, surface, expanded, {}, frame, normalAxes, &result))
		return false;
	const ModelWorkControl control;
	ModelWorkProgress work(control, ModelWorkPhase::Editing);
	QHash<int, ModelTransformWeight> groups;
	if (!modelTransformWeights(part, topology, expanded, frame, options, result.pieceOf, &groups, work))
		return false;
	for (auto it = groups.cbegin(); it != groups.cend(); ++it)
	{
		for (int copy : topology.copies[it.key()])
			result.weights.insert(copy, it.value());
	}
	*map = std::move(result);
	return true;
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
