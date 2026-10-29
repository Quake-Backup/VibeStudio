#include "core/model_boundary_bridge.h"
#include "core/model_boundary_helpers.h"
#include "core/model_mesh_tools_p.h"

#include <QCoreApplication>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

// Segments, open-chain bridging, UV strips and face-region bridging follow
// the documented options of Blender's Bridge Edge Loops (number of cuts,
// open loops, bridging two face selections; manual 4.0,
// modeling/meshes/editing/edge/bridge_edge_loops.html, reviewed 2026-10-28).
// The implementation is original.
namespace vibestudio
{
namespace
{
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
double distanceSquared(ModelVec3 a, ModelVec3 b)
{
	const double x = double(a.x) - b.x, y = double(a.y) - b.y, z = double(a.z) - b.z;
	return x * x + y * y + z * z;
}
bool hasArea(const ModelFrameGeometry &pose, ModelTriangle triangle)
{
	const auto a = pose.positions[triangle.a], b = pose.positions[triangle.b], c = pose.positions[triangle.c];
	const double ux = double(b.x) - a.x, uy = double(b.y) - a.y, uz = double(b.z) - a.z,
		vx = double(c.x) - a.x, vy = double(c.y) - a.y, vz = double(c.z) - a.z;
	return std::hypot(uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx) >= 1e-10;
}
// A path through the lattice of cross edges (i, j) from (0, 0) to (n, m):
// each step consumes one side edge ('a' on the first side, 'b' on the second).
struct Strip
{
	QVector<char> steps;
	double cost = std::numeric_limits<double>::infinity();
};

// The sides list their lattice vertices: a closed loop repeats its first
// vertex at the end, so lattice point i always names a[i].
Strip triangulate(const QVector<int> &a, const QVector<int> &b, bool closed, const ModelFrameGeometry &pose,
	const QSet<ModelEdge> &existing, bool startWithA, ModelBoundaryWork &work)
{
	const int n = a.size() - 1, m = b.size() - 1, width = m + 1;
	QVector<double> costs((n + 1) * width, std::numeric_limits<double>::infinity());
	QVector<char> steps(costs.size(), 0);
	const auto at = [width](int i, int j) { return i * width + j; };
	// Each lattice point is one edge across the gap. A step consumes exactly
	// one boundary edge and adds its triangle. For closed loops the two seam
	// orientations keep the initial cross edge unique until closure; allowing
	// an early full lap would reuse cross edges and create nonmanifold fans.
	for (int i = 0; i <= n; ++i)
	{
		for (int j = 0; j <= m; ++j)
		{
			if (!work.step())
				return {};
			if (closed && ((i == n && j == 0) || (i == 0 && j == m) ||
				(startWithA && ((i == 0 && j > 0) || (j == m && i < n))) ||
				(!startWithA && ((j == 0 && i > 0) || (i == n && j < m)))))
				continue;
			const int ai = a[i], bj = b[j], cell = at(i, j);
			if (existing.contains(modelEdge(ai, bj)))
				continue;
			if (i == 0 && j == 0)
			{
				costs[cell] = 0;
				continue;
			}
			const double cost = distanceSquared(pose.positions[ai], pose.positions[bj]);
			if (i > 0 && std::isfinite(costs[at(i - 1, j)]) && hasArea(pose, {a[i - 1], ai, bj}))
			{
				costs[cell] = costs[at(i - 1, j)] + cost;
				steps[cell] = 'a';
			}
			if (j > 0 && std::isfinite(costs[at(i, j - 1)]) && costs[at(i, j - 1)] + cost < costs[cell] &&
				hasArea(pose, {ai, bj, b[j - 1]}))
			{
				costs[cell] = costs[at(i, j - 1)] + cost;
				steps[cell] = 'b';
			}
		}
	}
	Strip result;
	result.cost = costs[at(n, m)];
	if (!std::isfinite(result.cost))
		return result;
	int i = n, j = m;
	while (i || j)
	{
		if (!work.step())
			return {};
		result.steps << steps[at(i, j)];
		if (steps[at(i, j)] == 'a')
			--i;
		else
			--j;
	}
	std::reverse(result.steps.begin(), result.steps.end());
	return result;
}

// Two open boundary chains from exactly the selected edges, each running
// opposite to its faces (as boundary loops do), ordered by first vertex.
bool findOpenChains(const ModelSurface &source, const QSet<ModelEdge> &selected, QVector<QVector<int>> *chains, QSet<ModelEdge> *existing,
	QVector<ModelEdge> *edges, ModelBoundaryWork &work)
{
	QHash<ModelEdge, int> uses;
	QHash<ModelEdge, QPair<int, int>> directions;
	for (const auto &t : source.triangles)
	{
		if (!work.step())
			return false;
		for (const auto &[from, to] : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
		{
			const auto key = modelEdge(from, to);
			++uses[key];
			directions.insert(key, {from, to});
			existing->insert(key);
		}
	}
	auto seeds = selected.values();
	std::sort(seeds.begin(), seeds.end());
	QHash<int, int> next, previous;
	for (const auto seed : std::as_const(seeds))
	{
		if (!work.step())
			return false;
		if (uses.value(seed) != 1)
			return fail(work.error, QCoreApplication::translate("ModelBoundaryBridge", "Edge %1:%2 is not a boundary edge. Select edges used by exactly one face.")
										.arg(seed.first)
										.arg(seed.second));
		const auto [from, to] = directions.value(seed);
		if (next.contains(to) || previous.contains(from))
			return fail(work.error, QCoreApplication::translate("ModelBoundaryBridge", "The selected edges branch at vertex %1. Select two unbranched chains.")
										.arg(next.contains(to) ? to : from));
		next.insert(to, from);
		previous.insert(from, to);
	}
	QVector<int> starts;
	for (auto i = next.cbegin(); i != next.cend(); ++i)
	{
		if (!previous.contains(i.key()))
			starts << i.key();
	}
	std::sort(starts.begin(), starts.end());
	QSet<int> visited;
	QVector<QVector<int>> found;
	for (int start : std::as_const(starts))
	{
		QVector<int> chain{start};
		visited.insert(start);
		for (int v = start; next.contains(v);)
		{
			if (!work.step())
				return false;
			v = next.value(v);
			chain << v;
			visited.insert(v);
		}
		found << chain;
	}
	if (visited.size() != next.size() + found.size())
		return fail(work.error, QCoreApplication::translate("ModelBoundaryBridge",
			"The selected edges close a loop. Bridge closed loops without open chains, or select two chains with ends."));
	if (found.size() != 2)
		return fail(work.error, QCoreApplication::translate("ModelBoundaryBridge", "Select exactly two open chains of boundary edges to bridge."));
	*chains = found;
	*edges = seeds.toVector();
	return true;
}

// The bridge's new vertices and faces for one strip, on a copy of the source.
bool buildStrip(const ModelSurface &source, const QVector<int> &a, const QVector<int> &b, bool closed, const Strip &strip,
	const ModelBoundaryBridgeOptions &options, ModelSurface *out, QVector<ModelTriangle> *faces, ModelBoundaryWork &work)
{
	using namespace model_tools;
	ModelSurface surface = source;
	const int k = options.segments;
	const auto &pose = source.frames[options.referenceFrame];
	QVector<int> rowA = a, rowB = b;
	if (options.tileSize > 0)
	{
		// The strip's own copies of both sides, with UVs along and across it.
		const auto lengths = [&](const QVector<int> &side)
		{
			QVector<double> along{0};
			for (int i = 1; i < side.size(); ++i)
				along << along.last() + std::sqrt(distanceSquared(pose.positions[side[i - 1]], pose.positions[side[i]]));
			return along;
		};
		const auto alongA = lengths(a), alongB = lengths(b);
		const double average = (alongA.last() + alongB.last()) / 2;
		double across = 0;
		int crossings = 0;
		for (int i = 0, j = 0, step = 0;; ++step)
		{
			across += std::sqrt(distanceSquared(pose.positions[a[i]], pose.positions[b[j]]));
			++crossings;
			if (step >= strip.steps.size())
				break;
			strip.steps[step] == 'a' ? ++i : ++j;
		}
		const double height = across / crossings / options.tileSize;
		for (int i = 0; i < a.size(); ++i)
		{
			rowA[i] = appendCopy(&surface, a[i]);
			surface.texCoords[rowA[i]] = {float(alongA[i] / std::max(alongA.last(), 1e-12) * average / options.tileSize), 0};
		}
		for (int j = 0; j < b.size(); ++j)
		{
			rowB[j] = appendCopy(&surface, b[j]);
			surface.texCoords[rowB[j]] = {float(alongB[j] / std::max(alongB.last(), 1e-12) * average / options.tileSize), float(height)};
		}
	}
	QHash<ModelEdge, QVector<int>> columns;
	const auto column = [&](int i, int j) -> QVector<int>
	{
		const ModelEdge key{rowA[i], rowB[j]};
		if (!columns.contains(key))
		{
			QVector<int> levels{rowA[i]};
			for (int s = 1; s <= k; ++s)
				levels << appendLerp(&surface, rowA[i], rowB[j], double(s) / (k + 1));
			levels << rowB[j];
			columns.insert(key, levels);
		}
		return columns.value(key);
	};
	QVector<ModelTriangle> added;
	int i = 0, j = 0;
	const auto first = column(0, 0);
	for (const char step : strip.steps)
	{
		if (!work.step())
			return false;
		if (step == 'a')
		{
			const auto low = column(i, j), high = column(i + 1, j);
			for (int s = 0; s < k; ++s)
				added << ModelTriangle{low[s], high[s], high[s + 1]} << ModelTriangle{low[s], high[s + 1], low[s + 1]};
			added << ModelTriangle{low[k], high[k], rowB[j]};
			++i;
		}
		else
		{
			const auto low = column(i, j), high = column(i, j + 1);
			added << ModelTriangle{rowA[i], high[1], low[1]};
			for (int s = 1; s <= k; ++s)
				added << ModelTriangle{high[s], high[s + 1], low[s + 1]} << ModelTriangle{high[s], low[s + 1], low[s]};
			++j;
		}
	}
	if (options.tileSize > 0)
	{
		// The strip is its own chart: seams along both sides and, for a loop,
		// down the column where its UVs wrap, whose two copies share identities.
		for (int x = 1; x < rowA.size(); ++x)
			surface.uvSeams.insert(modelEdge(rowA[x - 1], rowA[x]));
		for (int y = 1; y < rowB.size(); ++y)
			surface.uvSeams.insert(modelEdge(rowB[y - 1], rowB[y]));
		if (closed)
		{
			const auto last = column(a.size() - 1, b.size() - 1);
			for (int s = 1; s <= k + 1; ++s)
			{
				surface.uvSeams.insert(modelEdge(first[s - 1], first[s]));
				surface.uvSeams.insert(modelEdge(last[s - 1], last[s]));
			}
			if (!surface.topologyVertices.isEmpty())
			{
				for (int s = 1; s <= k; ++s)
					surface.topologyVertices[last[s]] = surface.topologyVertices[first[s]];
			}
		}
	}
	*out = std::move(surface);
	*faces = std::move(added);
	return work.progress.check();
}
} // namespace

bool bridgeModelBoundaryLoops(const ModelSurface &source, const QSet<ModelEdge> &selected, int referenceFrame, int twist,
	int triangleLimit, ModelSurface *result, ModelBoundaryBridgeReport *report, QString *error, const ModelWorkControl &control)
{
	ModelBoundaryBridgeOptions options;
	options.referenceFrame = referenceFrame;
	options.twist = twist;
	return bridgeModelBoundaries(source, selected, options, triangleLimit, std::numeric_limits<int>::max(), result, report, error, control);
}

bool bridgeModelBoundaries(const ModelSurface &source, const QSet<ModelEdge> &selected, const ModelBoundaryBridgeOptions &options,
	int triangleLimit, int vertexLimit, ModelSurface *result, ModelBoundaryBridgeReport *report, QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	ModelBoundaryWork work{{control, ModelWorkPhase::Editing, error}, error};
	if (!work.progress.check())
		return false;
	const int referenceFrame = options.referenceFrame, twist = options.twist;
	if (!result || referenceFrame < 0 || referenceFrame >= source.frames.size() || twist < -1023 || twist > 1023 ||
		triangleLimit < source.triangles.size())
		return fail(error, QCoreApplication::translate("ModelBoundaryBridge",
			"Choose an existing reference frame, a bridge twist from -1023 to 1023 and enough triangle capacity."));
	if (options.segments < 0 || options.segments > 64 || !std::isfinite(options.tileSize) || options.tileSize < 0 ||
		(options.tileSize > 0 && options.tileSize < 0.001) || options.tileSize > 1000000 || (options.openChains && twist != 0))
		return fail(error, QCoreApplication::translate("ModelBoundaryBridge",
			"Use 0–64 bridge segments and zero or 0.001–1,000,000 units per UV repeat; twist applies to closed loops only."));
	QVector<int> a, b;
	QSet<ModelEdge> existing;
	QVector<ModelEdge> boundaryEdges;
	const bool closed = !options.openChains;
	const auto &pose = source.frames[referenceFrame];
	if (closed)
	{
		ModelBoundaryLoops boundary;
		if (!findModelBoundaryLoops(source, selected, &boundary, work))
			return false;
		if (boundary.loops.size() != 2)
			return fail(error, QCoreApplication::translate("ModelBoundaryBridge",
				"Select boundary edges from exactly two closed loops on one surface. Join surfaces first when needed."));
		a = boundary.loops[0];
		b = boundary.loops[1];
		existing = std::move(boundary.existing);
		boundaryEdges = std::move(boundary.edges);
	}
	else
	{
		QVector<QVector<int>> chains;
		if (!findOpenChains(source, selected, &chains, &existing, &boundaryEdges, work))
			return false;
		a = chains[0];
		b = chains[1];
	}
	const QSet<int> firstVertices(a.cbegin(), a.cend());
	for (int vertex : b)
		if (firstVertices.contains(vertex))
			return fail(error, QCoreApplication::translate("ModelBoundaryBridge", "Bridge boundaries must not share vertices."));
	if (closed)
	{
		// Opposite strip sides follow opposite directions. Preserve the canonical
		// first vertex while reversing the second loop, then find a stable seam.
		std::reverse(b.begin() + 1, b.end());
		double closest = std::numeric_limits<double>::infinity();
		int startA = 0, startB = 0;
		for (int i = 0; i < a.size(); ++i)
		{
			for (int j = 0; j < b.size(); ++j)
			{
				if (!work.step())
					return false;
				const double distance = distanceSquared(pose.positions[a[i]], pose.positions[b[j]]);
				if (!existing.contains(modelEdge(a[i], b[j])) && distance < closest)
				{
					closest = distance;
					startA = i;
					startB = j;
				}
			}
		}
		const int secondStart = (startB + twist % b.size() + b.size()) % b.size();
		std::rotate(a.begin(), a.begin() + startA, a.end());
		std::rotate(b.begin(), b.begin() + secondStart, b.end());
		a << a.first();
		b << b.first();
	}
	else
		std::reverse(b.begin(), b.end());
	const qint64 sideEdges = a.size() - 1 + b.size() - 1;
	const qint64 addedTriangles = sideEdges * (2 * qint64(options.segments) + 1);
	const qint64 addedVertices = (options.tileSize > 0 ? qint64(a.size() + b.size()) : 0) + qint64(options.segments) * (sideEdges + 1);
	if (qint64(source.triangles.size()) + addedTriangles > triangleLimit)
		return fail(error, QCoreApplication::translate("ModelBoundaryBridge", "Bridging these loops would exceed the document's triangle limit."));
	if (qint64(source.vertexCount) + addedVertices > vertexLimit)
		return fail(error, QCoreApplication::translate("ModelBoundaryBridge", "Bridging these boundaries would exceed the document's vertex or animation storage limit."));
	QVector<Strip> strips;
	for (int orientation = 0; orientation < (closed ? 2 : 1); ++orientation)
	{
		strips << triangulate(a, b, closed, pose, existing, orientation == 0, work);
		if (work.stopped)
			return false;
	}
	std::stable_sort(strips.begin(), strips.end(), [](const Strip &x, const Strip &y) { return x.cost < y.cost; });
	ModelSurface accepted;
	QVector<ModelTriangle> faces;
	bool found = false;
	for (const auto &strip : std::as_const(strips))
	{
		if (!std::isfinite(strip.cost))
			continue;
		ModelSurface candidate;
		QVector<ModelTriangle> added;
		if (!buildStrip(source, a, b, closed, strip, options, &candidate, &added, work))
			return false;
		if (validateModelBoundaryFaces(candidate, added, work))
		{
			accepted = std::move(candidate);
			faces = std::move(added);
			found = true;
			break;
		}
		if (work.stopped || !work.progress.check())
			return false;
	}
	if (!found)
	{
		if (error && !error->isEmpty())
			return false;
		return fail(error, QCoreApplication::translate("ModelBoundaryBridge",
			"These boundaries cannot form a valid bridge with this alignment. Adjust the twist, reference pose or boundary geometry."));
	}
	ModelBoundaryBridgeReport receipt;
	receipt.firstAnchor = a[0];
	receipt.secondAnchor = b[0];
	receipt.boundaryEdges = std::move(boundaryEdges);
	for (auto face : std::as_const(faces))
	{
		if (!work.step())
			return false;
		receipt.faces << int(accepted.triangles.size());
		accepted.triangles << face;
	}
	if (!work.progress.check())
		return false;
	*result = std::move(accepted);
	if (report)
		*report = std::move(receipt);
	if (error)
		error->clear();
	return true;
}

bool bridgeModelFaceRegions(const ModelSurface &source, const QSet<int> &faces, const ModelBoundaryBridgeOptions &options, int triangleLimit,
	int vertexLimit, ModelSurface *result, ModelBoundaryBridgeReport *report, QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	ModelBoundaryWork work{{control, ModelWorkPhase::Editing, error}, error};
	if (!result || faces.isEmpty() || options.openChains)
		return fail(error, QCoreApplication::translate("ModelBoundaryBridge", "Select two face regions to bridge."));
	for (int face : faces)
	{
		if (face < 0 || face >= source.triangles.size())
			return fail(error, QCoreApplication::translate("ModelBoundaryBridge", "A selected face no longer exists."));
	}
	// Regions: selected faces joined through shared edges.
	QHash<ModelEdge, QVector<int>> edgeFaces;
	for (int face = 0; face < source.triangles.size(); ++face)
	{
		if (!work.step())
			return false;
		const auto &t = source.triangles[face];
		for (const auto edge : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
			edgeFaces[edge] << face;
	}
	auto ordered = faces.values();
	std::sort(ordered.begin(), ordered.end());
	QHash<int, int> region;
	int regions = 0;
	for (int seed : std::as_const(ordered))
	{
		if (region.contains(seed))
			continue;
		QVector<int> stack{seed};
		region.insert(seed, regions);
		while (!stack.isEmpty())
		{
			if (!work.step())
				return false;
			const int face = stack.takeLast();
			const auto &t = source.triangles[face];
			for (const auto edge : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
			{
				for (int other : edgeFaces.value(edge))
				{
					if (faces.contains(other) && !region.contains(other))
					{
						region.insert(other, regions);
						stack << other;
					}
				}
			}
		}
		++regions;
	}
	if (regions != 2)
		return fail(error, QCoreApplication::translate("ModelBoundaryBridge", "Select exactly two separate face regions to bridge."));
	// Each region's border must touch other faces all round, so removing it
	// leaves one hole.
	QSet<ModelEdge> seeds;
	for (auto i = edgeFaces.cbegin(); i != edgeFaces.cend(); ++i)
	{
		int inside = 0, outside = 0;
		for (int face : i.value())
			faces.contains(face) ? ++inside : ++outside;
		if (inside == 1)
		{
			if (outside != 1)
				return fail(error, QCoreApplication::translate("ModelBoundaryBridge",
					"Each face region must be surrounded by other faces so that removing it leaves one hole."));
			seeds.insert(i.key());
		}
	}
	ModelSurface opened = source;
	opened.triangles.clear();
	for (int face = 0; face < source.triangles.size(); ++face)
	{
		if (!faces.contains(face))
			opened.triangles << source.triangles[face];
	}
	model_tools::pruneSeams(&opened);
	ModelSurface bridged;
	ModelBoundaryBridgeReport receipt;
	if (!bridgeModelBoundaries(opened, seeds, options, triangleLimit, vertexLimit, &bridged, &receipt, error, control))
		return false;
	*result = std::move(bridged);
	if (report)
		*report = std::move(receipt);
	return true;
}
} // namespace vibestudio
