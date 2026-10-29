#include "core/model_uv_ops.h"

#include "core/model_document.h"
#include "core/model_uv.h"
#include "core/model_uv_pins.h"
#include "core/model_vertex_identity.h"
#include "core/model_vertex_origins.h"

#include <QCoreApplication>
#include <QHash>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>
#include <numeric>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelUvOps)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
QSet<int> componentVertices(const ModelSurface &surface, const ModelSelection &selection)
{
	auto vertices = selection.vertices;
	for (const auto &edge : selection.edges)
	{
		vertices.insert(edge.first);
		vertices.insert(edge.second);
	}
	for (int face : selection.faces)
	{
		if (face < 0 || face >= surface.triangles.size())
			continue;
		const auto t = surface.triangles[face];
		vertices.insert(t.a);
		vertices.insert(t.b);
		vertices.insert(t.c);
	}
	return vertices;
}
void appendBytes(QByteArray *key, const void *data, int size) { key->append(static_cast<const char *>(data), size); }
// Every pose's position (and normal), plus the topology identity: two corners
// with equal keys are the same point of the model with different UVs.
QByteArray geometryKey(const ModelSurface &surface, int vertex, bool normals)
{
	QByteArray key;
	key.reserve(int(surface.frames.size()) * (normals ? 24 : 12) + 4);
	for (const auto &frame : surface.frames)
	{
		appendBytes(&key, &frame.positions[vertex], sizeof(ModelVec3));
		if (normals)
			appendBytes(&key, &frame.normals[vertex], sizeof(ModelVec3));
	}
	if (!surface.topologyVertices.isEmpty())
		appendBytes(&key, &surface.topologyVertices[vertex], sizeof(int));
	return key;
}
bool weldable(const ModelSurface &surface, int a, int b)
{
	return geometryKey(surface, a, true) == geometryKey(surface, b, true) && sameModelSkinWeights(surface, a, b);
}
quint64 faceKey(ModelTriangle t)
{
	std::array<quint64, 3> v{quint64(t.a), quint64(t.b), quint64(t.c)};
	std::sort(v.begin(), v.end());
	return (v[0] << 40) | (v[1] << 20) | v[2];
}
int duplicateFaces(const QVector<ModelTriangle> &triangles)
{
	QSet<quint64> seen;
	int duplicates = 0;
	for (const auto &t : triangles)
	{
		const auto key = faceKey(t);
		duplicates += seen.contains(key);
		seen.insert(key);
	}
	return duplicates;
}
// Points each corner at mapping[corner] and removes the vertices nothing uses
// any longer (those mapped elsewhere), keeping every other vertex in order.
bool weldMapped(ModelSurface *surface, const QVector<int> &mapping, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	auto triangles = surface->triangles;
	for (auto &t : triangles)
	{
		if (!work.step())
			return false;
		t = {mapping[t.a], mapping[t.b], mapping[t.c]};
		if (t.a == t.b || t.b == t.c || t.a == t.c)
			return fail(error, Text::tr("Welding these UVs would collapse a face. Select fewer corners."));
	}
	if (duplicateFaces(triangles) > duplicateFaces(surface->triangles))
		return fail(error, Text::tr("Welding these UVs would make two faces identical. Select fewer corners."));
	QVector<int> kept, newIndex(surface->vertexCount, -1);
	for (int v = 0; v < surface->vertexCount; ++v)
		if (mapping[v] == v)
		{
			newIndex[v] = int(kept.size());
			kept.append(v);
		}
	ModelSurface result = *surface;
	result.texCoords.clear();
	result.topologyVertices.clear();
	result.skinning = {};
	for (auto &frame : result.frames)
	{
		frame.positions.clear();
		frame.normals.clear();
	}
	for (int old : kept)
	{
		if (!work.step())
			return false;
		result.texCoords.append(surface->texCoords[old]);
		if (!surface->topologyVertices.isEmpty())
			result.topologyVertices.append(surface->topologyVertices[old]);
		if (!surface->skinning.isEmpty())
		{
			result.skinning.first.append(int(result.skinning.influences.size()));
			result.skinning.count.append(surface->skinning.count[old]);
			for (int n = 0; n < surface->skinning.count[old]; ++n)
				result.skinning.influences.append(surface->skinning.influences[surface->skinning.first[old] + n]);
		}
		for (int f = 0; f < surface->frames.size(); ++f)
		{
			result.frames[f].positions.append(surface->frames[f].positions[old]);
			result.frames[f].normals.append(surface->frames[f].normals[old]);
		}
	}
	for (auto &t : triangles)
		t = {newIndex[t.a], newIndex[t.b], newIndex[t.c]};
	QSet<ModelEdge> edges;
	for (const auto &t : triangles)
		for (auto edge : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
			edges.insert(edge);
	result.uvSeams.clear();
	for (auto seam : surface->uvSeams)
	{
		const auto edge = modelEdge(newIndex[mapping[seam.first]], newIndex[mapping[seam.second]]);
		if (edge.first != edge.second && edges.contains(edge))
			result.uvSeams.insert(edge);
	}
	result.triangles = std::move(triangles);
	result.vertexCount = int(result.texCoords.size());
	// A joined corner is pinned when any corner it joined was.
	result.uvPins.clear();
	for (int pin : surface->uvPins)
		if (pin >= 0 && pin < surface->vertexCount && newIndex[mapping[pin]] >= 0)
			result.uvPins.insert(newIndex[mapping[pin]]);
	compactModelVertexIdentities(&result);
	rebuildModelVertexOrigins(&result, surface->editOrigins, kept);
	if (selection)
	{
		QSet<int> vertices;
		for (int v : selection->vertices)
			vertices.insert(newIndex[mapping[v]]);
		QSet<ModelEdge> selectedEdges;
		for (auto edge : selection->edges)
		{
			const auto mapped = modelEdge(newIndex[mapping[edge.first]], newIndex[mapping[edge.second]]);
			if (mapped.first != mapped.second && edges.contains(mapped))
				selectedEdges.insert(mapped);
		}
		selection->vertices = std::move(vertices);
		selection->edges = std::move(selectedEdges);
	}
	if (!work.check())
		return false;
	*surface = std::move(result);
	return true;
}
bool weld(ModelSurface *surface, const QSet<int> &seeds, double tolerance, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	QHash<QByteArray, QVector<int>> buckets;
	QVector<QByteArray> keys(surface->vertexCount);
	for (int v = 0; v < surface->vertexCount; ++v)
	{
		if (!work.step())
			return false;
		keys[v] = geometryKey(*surface, v, true);
		buckets[keys[v]].append(v);
	}
	QSet<QByteArray> seeded;
	for (int v : seeds)
		seeded.insert(keys[v]);
	QVector<int> mapping(surface->vertexCount);
	std::iota(mapping.begin(), mapping.end(), 0);
	QHash<int, QVector<int>> members;
	for (int v = 0; v < surface->vertexCount; ++v)
	{
		if (!work.step())
			return false;
		const auto &bucket = buckets[keys[v]];
		if (bucket.first() != v || bucket.size() < 2 || !seeded.contains(keys[v]))
			continue;
		QVector<int> anchors;
		for (int member : bucket)
		{
			const auto uv = surface->texCoords[member];
			int anchor = -1;
			for (int candidate : anchors)
			{
				const auto at = surface->texCoords[candidate];
				if (std::hypot(double(uv.u) - at.u, double(uv.v) - at.v) <= tolerance && sameModelSkinWeights(*surface, candidate, member))
				{
					anchor = candidate;
					break;
				}
			}
			if (anchor < 0)
				anchors.append(member);
			else
			{
				mapping[member] = anchor;
				members[anchor].append(member);
			}
		}
	}
	if (members.isEmpty())
		return fail(error, Text::tr("No selected UV corner has a split copy within the weld distance whose position and normal match in "
									"every pose."));
	// A joined corner takes the mean UV of everything it joined.
	for (auto it = members.cbegin(); it != members.cend(); ++it)
	{
		double u = surface->texCoords[it.key()].u, v = surface->texCoords[it.key()].v;
		for (int member : it.value())
		{
			u += surface->texCoords[member].u;
			v += surface->texCoords[member].v;
		}
		const double count = double(it.value().size() + 1);
		surface->texCoords[it.key()] = {float(u / count), float(v / count)};
	}
	return weldMapped(surface, mapping, selection, error, work);
}
double islandWinding(const ModelSurface &surface, const ModelUvIsland &island)
{
	double sum = 0;
	for (int face : island.faces)
	{
		const auto t = surface.triangles[face];
		const auto a = surface.texCoords[t.a], b = surface.texCoords[t.b], c = surface.texCoords[t.c];
		sum += (double(b.u) - a.u) * (double(c.v) - a.v) - (double(b.v) - a.v) * (double(c.u) - a.u);
	}
	return sum;
}
int cornerSlot(ModelTriangle t, int vertex) { return t.a == vertex ? 0 : t.b == vertex ? 1 : t.c == vertex ? 2 : -1; }
int cornerAt(ModelTriangle t, int slot) { return slot == 0 ? t.a : slot == 1 ? t.b : t.c; }
bool stitch(const ModelMesh &mesh, ModelSurface *surface, ModelSelection *selection, QString *error, const ModelWorkControl &control,
			ModelWorkProgress &work)
{
	if (selection->edges.isEmpty() || !selection->faces.isEmpty() || !selection->vertices.isEmpty())
		return fail(error, Text::tr("Select the seam edges to stitch, in edge selection."));
	ModelUvTopology topology;
	if (!buildModelUvTopology(*surface, &topology, error, control))
		return false;
	QHash<ModelEdge, QVector<int>> edgeFaces;
	for (const auto &edge : topology.edges)
		edgeFaces.insert(edge.vertices, edge.faces);
	QHash<QByteArray, QVector<int>> points;
	QVector<QByteArray> keys(surface->vertexCount);
	for (int v = 0; v < surface->vertexCount; ++v)
	{
		if (!work.step())
			return false;
		keys[v] = geometryKey(*surface, v, false);
		points[keys[v]].append(v);
	}
	// Corners are kept as (face, slot): faces keep their indices while
	// islands move, though a moved corner may be split into a new vertex.
	struct Corner
	{
		int face = -1, slot = 0;
	};
	struct Correspondence
	{
		// side[0] lies on island[0]; corner k of one side meets corner k of
		// the other.
		std::array<std::array<Corner, 2>, 2> side;
		std::array<int, 2> island{};
	};
	QVector<Correspondence> found;
	QSet<QPair<ModelEdge, ModelEdge>> seen;
	auto edges = selection->edges.values();
	std::sort(edges.begin(), edges.end());
	for (const auto &edge : edges)
	{
		if (!work.step())
			return false;
		const auto faces = edgeFaces.value(edge);
		if (faces.size() != 1)
			continue;
		bool matched = false;
		for (int c : points.value(keys[edge.first]))
		{
			for (int d : points.value(keys[edge.second]))
			{
				if ((c == edge.first && d == edge.second) || c == d)
					continue;
				const auto other = edgeFaces.value(modelEdge(c, d));
				if (other.size() != 1 || topology.faceIsland[other.first()] == topology.faceIsland[faces.first()])
					continue;
				// Both sides selected name one seam once.
				const auto counterpart = modelEdge(c, d);
				const auto key = edge < counterpart ? qMakePair(edge, counterpart) : qMakePair(counterpart, edge);
				matched = true;
				if (seen.contains(key))
					break;
				seen.insert(key);
				const auto f = surface->triangles[faces.first()], g = surface->triangles[other.first()];
				Correspondence match;
				// Keep the corner order: edge.first meets c and edge.second meets d.
				match.side[0] = {Corner{faces.first(), cornerSlot(f, edge.first)}, Corner{faces.first(), cornerSlot(f, edge.second)}};
				match.side[1] = {Corner{other.first(), cornerSlot(g, c)}, Corner{other.first(), cornerSlot(g, d)}};
				match.island = {topology.faceIsland[faces.first()], topology.faceIsland[other.first()]};
				found.append(match);
				break;
			}
			if (matched)
				break;
		}
	}
	if (found.isEmpty())
		return fail(error, Text::tr("No selected edge has a split copy on another island. Select boundary edges of a UV seam whose two sides "
									"lie on different islands."));
	// Islands the seams connect, as a graph. In each connected group the
	// largest island stays put; the others are placed breadth first, each
	// fitted to every island already placed that it shares a seam with.
	const int islands = int(topology.islands.size());
	QVector<QVector<int>> neighbours(islands);
	for (const auto &match : found)
	{
		neighbours[match.island[0]].append(match.island[1]);
		neighbours[match.island[1]].append(match.island[0]);
	}
	for (auto &list : neighbours)
	{
		std::sort(list.begin(), list.end());
		list.erase(std::unique(list.begin(), list.end()), list.end());
	}
	QVector<int> order, position(islands, -1), group(islands, -1);
	QVector<int> roots;
	for (int start = 0; start < islands; ++start)
	{
		if (group[start] >= 0 || neighbours[start].isEmpty())
			continue;
		// The group's members, then its largest island as the root.
		QVector<int> members{start};
		group[start] = start;
		for (int i = 0; i < members.size(); ++i)
			for (int next : neighbours[members[i]])
				if (group[next] < 0)
				{
					group[next] = start;
					members.append(next);
				}
		int root = members.first();
		for (int island : members)
			if (topology.islands[island].faces.size() > topology.islands[root].faces.size() ||
				(topology.islands[island].faces.size() == topology.islands[root].faces.size() && island < root))
				root = island;
		roots.append(root);
		QVector<int> queue{root};
		position[root] = int(order.size());
		order.append(root);
		for (int i = 0; i < queue.size(); ++i)
			for (int next : neighbours[queue[i]])
				if (position[next] < 0)
				{
					position[next] = int(order.size());
					order.append(next);
					queue.append(next);
				}
	}
	for (int island : order)
		if ((islandWinding(*surface, topology.islands[island]) < 0) != (islandWinding(*surface, topology.islands[group[island]]) < 0))
			return fail(error, Text::tr("One of these islands is mirrored. Mirror it back (a UV scale of -1) before stitching."));
	const auto uvAt = [](const ModelSurface &s, const Corner &corner) { return s.texCoords[cornerAt(s.triangles[corner.face], corner.slot)]; };
	// Rigid placements in original UVs: placed(x) = R x + t.
	struct Placement
	{
		double cosine = 1, sine = 0, tu = 0, tv = 0;
	};
	QVector<Placement> placement(islands);
	for (int island : order)
	{
		if (!work.step())
			return false;
		if (std::find(roots.cbegin(), roots.cend(), island) != roots.cend())
			continue;
		// Every corner pair with an island placed before this one.
		QVector<QPair<ModelTexCoord, ModelTexCoord>> pairs;
		for (const auto &match : found)
			for (int s = 0; s < 2; ++s)
				if (match.island[s] == island && position[match.island[1 - s]] < position[island])
				{
					const auto &placed = placement[match.island[1 - s]];
					for (int k = 0; k < 2; ++k)
					{
						const auto from = uvAt(*surface, match.side[s][k]), to = uvAt(*surface, match.side[1 - s][k]);
						pairs.append({from, {float(placed.cosine * to.u - placed.sine * to.v + placed.tu),
											 float(placed.sine * to.u + placed.cosine * to.v + placed.tv)}});
					}
				}
		// The two-dimensional orthogonal Procrustes solution: rotation and
		// translation only.
		double pu = 0, pv = 0, qu = 0, qv = 0;
		for (const auto &pair : pairs)
		{
			pu += pair.first.u;
			pv += pair.first.v;
			qu += pair.second.u;
			qv += pair.second.v;
		}
		const double n = double(std::max<qsizetype>(1, pairs.size()));
		pu /= n, pv /= n, qu /= n, qv /= n;
		double dot = 0, cross = 0;
		for (const auto &pair : pairs)
		{
			const double x = pair.first.u - pu, y = pair.first.v - pv, s = pair.second.u - qu, t = pair.second.v - qv;
			dot += x * s + y * t;
			cross += x * t - y * s;
		}
		const double angle = (dot == 0 && cross == 0) ? 0 : std::atan2(cross, dot);
		auto &fit = placement[island];
		fit.cosine = std::cos(angle);
		fit.sine = std::sin(angle);
		fit.tu = qu - (fit.cosine * pu - fit.sine * pv);
		fit.tv = qv - (fit.sine * pu + fit.cosine * pv);
	}
	qint64 others = 0;
	for (const auto &other : mesh.surfaces)
		others += other.vertexCount;
	others -= surface->vertexCount;
	const int vertexLimit =
		int(std::min<qint64>(modelDocumentMaxVertices, modelDocumentMaxFrameVertices / std::max<qint64>(1, mesh.frames.size())) - others);
	if (vertexLimit < surface->vertexCount)
		return fail(error, Text::tr("Stitching would exceed the vertex or animation storage limit."));
	ModelSurface moved = *surface;
	for (int island : order)
	{
		if (std::find(roots.cbegin(), roots.cend(), island) != roots.cend())
			continue;
		const auto &fit = placement[island];
		// transformModelUvIslands turns about the island's bounds centre c:
		// R(x - c) + c + offset = R x + t, so offset = t + R c - c.
		const auto &shape = topology.islands[island];
		const double cu = (double(shape.mins.u) + shape.maxs.u) / 2, cv = (double(shape.mins.v) + shape.maxs.v) / 2;
		ModelUvIslandTransform transform;
		transform.rotation = std::atan2(fit.sine, fit.cosine) * 180 / std::numbers::pi;
		transform.offset = {float(fit.tu + (fit.cosine * cu - fit.sine * cv) - cu), float(fit.tv + (fit.sine * cu + fit.cosine * cv) - cv)};
		transform.vertexLimit = vertexLimit;
		const QSet<int> faces(shape.faces.cbegin(), shape.faces.cend());
		ModelSurface next;
		if (!transformModelUvIslands(moved, faces, transform, &next, error, control))
			return false;
		moved = std::move(next);
	}
	// Later islands' corners meet the earlier ones exactly, in placement
	// order so a corner copies a UV that is already final; corners that
	// match in every pose then weld.
	std::sort(found.begin(), found.end(),
			  [&](const Correspondence &a, const Correspondence &b)
			  {
				  return std::max(position[a.island[0]], position[a.island[1]]) < std::max(position[b.island[0]], position[b.island[1]]);
			  });
	QVector<int> mapping(moved.vertexCount);
	std::iota(mapping.begin(), mapping.end(), 0);
	QSet<ModelEdge> stitched;
	bool welded = false;
	for (const auto &match : found)
	{
		if (!work.step())
			return false;
		const int later = position[match.island[0]] > position[match.island[1]] ? 0 : 1;
		const auto &from = match.side[later], &to = match.side[1 - later];
		for (int k = 0; k < 2; ++k)
		{
			const int corner = cornerAt(moved.triangles[from[k].face], from[k].slot);
			const int target = cornerAt(moved.triangles[to[k].face], to[k].slot);
			moved.texCoords[corner] = moved.texCoords[target];
			if (corner != target && weldable(moved, corner, target))
			{
				mapping[corner] = target;
				welded = true;
			}
		}
		// The stitched edges no longer separate their islands.
		const auto kept = modelEdge(cornerAt(moved.triangles[to[0].face], to[0].slot), cornerAt(moved.triangles[to[1].face], to[1].slot));
		moved.uvSeams.remove(kept);
		moved.uvSeams.remove(modelEdge(cornerAt(moved.triangles[from[0].face], from[0].slot), cornerAt(moved.triangles[from[1].face], from[1].slot)));
		stitched.insert(kept);
	}
	// A corner welds to the corner its target welded to.
	for (int v = 0; v < mapping.size(); ++v)
	{
		int root = v;
		while (mapping[root] != root)
			root = mapping[root];
		mapping[v] = root;
	}
	ModelSelection result = *selection;
	result.edges = stitched;
	if (welded && !weldMapped(&moved, mapping, &result, error, work))
		return false;
	if (!work.check())
		return false;
	*surface = std::move(moved);
	*selection = std::move(result);
	return true;
}
bool align(ModelSurface *surface, const QSet<int> &vertices, const ModelUvToolOptions &options, QString *error, ModelWorkProgress &work)
{
	if (vertices.size() < 2)
		return fail(error, Text::tr("Select at least two UV corners to align."));
	auto chosen = vertices.values();
	std::sort(chosen.begin(), chosen.end());
	if (options.alignMode == ModelUvAlignMode::Straighten)
	{
		// Principal direction of the selected UVs, then the line through the
		// two extreme corners along it.
		double mu = 0, mv = 0;
		for (int v : chosen)
		{
			mu += surface->texCoords[v].u;
			mv += surface->texCoords[v].v;
		}
		mu /= chosen.size();
		mv /= chosen.size();
		double xx = 0, xy = 0, yy = 0;
		for (int v : chosen)
		{
			const double x = surface->texCoords[v].u - mu, y = surface->texCoords[v].v - mv;
			xx += x * x;
			xy += x * y;
			yy += y * y;
		}
		const double theta = .5 * std::atan2(2 * xy, xx - yy);
		const double du = std::cos(theta), dv = std::sin(theta);
		int low = chosen.first(), high = chosen.first();
		double lowAt = 1e300, highAt = -1e300;
		for (int v : chosen)
		{
			const double at = (surface->texCoords[v].u - mu) * du + (surface->texCoords[v].v - mv) * dv;
			if (at < lowAt)
			{
				lowAt = at;
				low = v;
			}
			if (at > highAt)
			{
				highAt = at;
				high = v;
			}
		}
		const auto p = surface->texCoords[low], q = surface->texCoords[high];
		const double lu = double(q.u) - p.u, lv = double(q.v) - p.v, length = lu * lu + lv * lv;
		if (!(length > 0))
			return fail(error, Text::tr("The selected UV corners coincide; there is no line to straighten them onto."));
		for (int v : chosen)
		{
			if (!work.step())
				return false;
			const auto uv = surface->texCoords[v];
			const double s = ((uv.u - p.u) * lu + (uv.v - p.v) * lv) / length;
			surface->texCoords[v] = {float(p.u + s * lu), float(p.v + s * lv)};
		}
		surface->texCoords[low] = p;
		surface->texCoords[high] = q;
		return true;
	}
	double lo = 1e300, hi = -1e300;
	for (int v : chosen)
	{
		const double value = options.alignAxis == 0 ? surface->texCoords[v].u : surface->texCoords[v].v;
		lo = std::min(lo, value);
		hi = std::max(hi, value);
	}
	const double target = options.alignMode == ModelUvAlignMode::Minimum	 ? lo
						  : options.alignMode == ModelUvAlignMode::Maximum ? hi
																		   : (lo + hi) / 2;
	for (int v : chosen)
	{
		if (!work.step())
			return false;
		(options.alignAxis == 0 ? surface->texCoords[v].u : surface->texCoords[v].v) = float(target);
	}
	return true;
}
} // namespace

bool isModelUvToolEdit(ModelEditKind kind)
{
	return kind == ModelEditKind::WeldUv || kind == ModelEditKind::StitchUv || kind == ModelEditKind::AlignUv ||
		   kind == ModelEditKind::SnapUvTexels || isModelUvPinEdit(kind);
}

bool validModelUvToolSettings(const ModelEdit &edit, QString *error)
{
	const auto &tools = edit.uvTools;
	if (tools == ModelUvToolOptions{} || isModelUvToolEdit(edit.kind))
		return true;
	ModelUvToolOptions atlas;
	atlas.packRotate = tools.packRotate;
	atlas.packSurfaces = tools.packSurfaces;
	atlas.unwrapMethod = tools.unwrapMethod;
	atlas.unwrapPack = tools.unwrapPack;
	const bool conformal = tools.unwrapMethod == ModelUvUnwrapMethod::Conformal;
	const bool valid = tools == atlas &&
					   ((edit.kind == ModelEditKind::PackUv && !conformal && tools.unwrapPack) ||
						(edit.kind == ModelEditKind::UnwrapUv && tools.packSurfaces.isEmpty() && (conformal || tools.unwrapPack) &&
						 (!tools.packRotate || (conformal && tools.unwrapPack))));
	return valid || fail(error, Text::tr("These UV settings do not apply to this operation. Rotation packing applies to Pack Islands and "
										 "a packed conformal unwrap; the unwrap method to Unwrap; other surfaces to Pack Islands."));
}

QSize modelUvSnapSize(const ModelMesh &mesh, QSize requested)
{
	if (requested.isValid() && !requested.isEmpty())
		return requested;
	const auto size = mesh.mdl.enabled ? mesh.mdl.skinSize : mesh.md2SkinSize;
	return size.isValid() && !size.isEmpty() ? size : QSize(256, 256);
}

ModelTexCoord modelUvSnapped(ModelTexCoord uv, QSize size, ModelUvSnapMode mode)
{
	const auto axis = [mode](float value, int length)
	{
		const double scaled = double(value) * length;
		if (mode == ModelUvSnapMode::Corners)
			return float(std::round(scaled) / length);
		// The writers' rounding, clamped inside the tile as they clamp it.
		auto texel = std::lround(scaled - .5);
		if (value >= 0 && value <= 1)
			texel = std::clamp(texel, 0L, long(length - 1));
		return float((double(texel) + .5) / length);
	};
	return {axis(uv.u, size.width()), axis(uv.v, size.height())};
}

bool applyModelUvToolEdit(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	const auto &options = edit.uvTools;
	if (!mesh || !selection || selection->surface < 0 || selection->surface >= mesh->surfaces.size())
		return fail(error, Text::tr("Select a model surface first."));
	if (isModelUvPinEdit(edit.kind))
	{
		if (options != ModelUvToolOptions{})
			return fail(error, Text::tr("Pin UVs and Unpin UVs take no UV settings."));
		auto pinned = edit;
		pinned.selection = *selection;
		return applyModelUvPinEdit(mesh, pinned, error) && work.check();
	}
	if (options.packRotate || options.unwrapMethod != ModelUvUnwrapMethod::Automatic || !options.unwrapPack || !options.packSurfaces.isEmpty())
		return fail(error, Text::tr("Packing and unwrap settings apply only to Unwrap and Pack."));
	if (!std::isfinite(options.weldTolerance) || options.weldTolerance < 0 || options.weldTolerance > 1 || options.alignAxis < 0 ||
		options.alignAxis > 1 || (options.snapSize.isValid() && (options.snapSize.width() < 1 || options.snapSize.height() < 1 ||
																 options.snapSize.width() > 65536 || options.snapSize.height() > 65536)))
		return fail(error, Text::tr("Choose a weld distance from 0 to 1, the U or V axis, and a texel grid from 1 to 65,536 pixels."));
	auto &surface = mesh->surfaces[selection->surface];
	const auto vertices = componentVertices(surface, *selection);
	switch (edit.kind)
	{
	case ModelEditKind::WeldUv:
		if (vertices.isEmpty())
			return fail(error, Text::tr("Select the UV corners, edges or faces to weld."));
		return weld(&surface, vertices, options.weldTolerance, selection, error, work);
	case ModelEditKind::StitchUv:
		return stitch(*mesh, &surface, selection, error, control, work);
	case ModelEditKind::AlignUv:
		return align(&surface, vertices, options, error, work) && work.check();
	case ModelEditKind::SnapUvTexels:
	{
		if (vertices.isEmpty())
			return fail(error, Text::tr("Select the UV corners, edges or faces to snap."));
		const auto size = modelUvSnapSize(*mesh, options.snapSize);
		for (int v : vertices)
		{
			if (!work.step())
				return false;
			surface.texCoords[v] = modelUvSnapped(surface.texCoords[v], size, options.snapMode);
		}
		return work.check();
	}
	default:
		break;
	}
	return fail(error, Text::tr("This is not a UV tool operation."));
}
} // namespace vibestudio
