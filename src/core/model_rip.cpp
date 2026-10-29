#include "core/model_rip.h"
#include "core/model_mesh_tools_p.h"

#include <algorithm>
#include <cmath>
#include <limits>

// Behavioural reference: Blender 4.5 Rip Vertices / Rip Vertices and Fill
// manuals, reviewed 2026-10-21 (CC-BY-SA-4.0). Original implementation; no
// upstream code or prose. VibeStudio cancels the entire candidate on Escape.
// https://docs.blender.org/manual/en/4.5/modeling/meshes/editing/vertex/rip_vertices.html
namespace vibestudio
{
namespace
{
using namespace model_tools;
struct RipText
{
	Q_DECLARE_TR_FUNCTIONS(ModelRip)
};
template <typename T> QVector<T> ordered(const QSet<T> &values)
{
	auto result = values.values().toVector();
	std::sort(result.begin(), result.end());
	return result;
}
int other(ModelEdge edge, int vertex) { return edge.first == vertex ? edge.second : edge.first; }
int winding(const ModelGeometricTopology &topology, ModelTriangle t, ModelEdge edge)
{
	for (auto pair : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
		if (topology.geometricEdge(pair.first, pair.second) == edge)
			return topology.group(pair.first) == edge.first ? 1 : -1;
	return 0;
}
} // namespace

bool planModelRip(const ModelSurface &surface, const ModelSelection &selection, const ModelMeshToolOptions &options, ModelRipPlan *result,
				  QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	const auto refuse = [&](const QString &text) { return model_tools::fail(error, text); };
	if (!work.check())
		return false;
	if (!result || options.referenceFrame < 0 || options.referenceFrame >= surface.frames.size() || options.ripFace < 0 ||
		options.ripFace >= surface.triangles.size() || !selection.faces.isEmpty() ||
		(selection.vertices.isEmpty() && selection.edges.isEmpty()))
		return refuse(
			RipText::tr("Rip needs selected vertices or edges and an incident side face. Separate faces to detach a face region."));
	ModelQuadTopology quads;
	if (!buildModelQuadTopology(surface, options.referenceFrame, options.maxAngle, &quads, error, control, selection.edges))
		return false;
	ModelRipPlan plan;
	plan.geometry = std::move(quads.geometry);
	plan.sideFace = options.ripFace;
	plan.vertexSelection = selection.edges.isEmpty();
	const auto &g = plan.geometry;
	for (int v : ordered(selection.vertices))
	{
		if (!work.step())
			return false;
		if (g.group(v) < 0)
			return refuse(RipText::tr("Choose existing rip vertices."));
		plan.groups.insert(g.group(v));
	}
	for (auto edge : ordered(selection.edges))
	{
		if (!work.step())
			return false;
		const auto key = g.geometricEdge(edge.first, edge.second);
		if (edge.first == edge.second || key.first < 0 || key.second < 0 || g.edgeFaces.value(key).size() != 2)
			return refuse(RipText::tr("Selected rip edges must lie between two faces."));
		plan.selectedEdges.insert(key);
		plan.groups << key.first << key.second;
	}
	QHash<int, QVector<ModelEdge>> incident;
	QSet<ModelEdge> allEdges;
	for (auto i = g.edgeFaces.cbegin(); i != g.edgeFaces.cend(); ++i)
		allEdges.insert(i.key());
	for (auto edge : ordered(allEdges))
	{
		if (!work.step())
			return false;
		for (int v : {edge.first, edge.second})
			if (plan.groups.contains(v))
				incident[v].append(edge);
		if (selection.edges.isEmpty() && plan.groups.contains(edge.first) && plan.groups.contains(edge.second) &&
			!quads.diagonals.contains(edge))
			plan.selectedEdges.insert(edge);
	}
	QHash<int, QVector<int>> selectedNeighbours;
	for (auto edge : ordered(plan.selectedEdges))
	{
		if (g.edgeFaces.value(edge).size() != 2)
			return refuse(RipText::tr("Selected rip edges must lie between two faces."));
		selectedNeighbours[edge.first].append(edge.second);
		selectedNeighbours[edge.second].append(edge.first);
	}
	const auto groups = ordered(plan.groups);
	QSet<int> connected;
	QVector<int> pending{groups.first()};
	while (!pending.isEmpty())
	{
		if (!work.step())
			return false;
		const int v = pending.takeLast();
		if (connected.contains(v))
			continue;
		connected.insert(v);
		for (int next : selectedNeighbours.value(v))
			if (!connected.contains(next))
				pending.append(next);
	}
	if (connected != plan.groups)
		return refuse(RipText::tr("Rip one connected vertex or edge chain at a time."));
	QSet<int> touched;
	for (int v : groups)
	{
		if (!work.step())
			return false;
		for (int f : g.groupFaces[v])
			touched.insert(f);
	}
	if (!touched.contains(options.ripFace))
		return refuse(RipText::tr("The rip side face must touch the selected vertices or edges."));
	const auto seed = surface.triangles[options.ripFace];
	const auto seedCentre = (position(surface, options.referenceFrame, seed.a) + position(surface, options.referenceFrame, seed.b) +
							 position(surface, options.referenceFrame, seed.c)) *
							(1.0 / 3);
	plan.cuts = plan.selectedEdges;
	for (int v : groups)
	{
		if (!work.step())
			return false;
		bool boundary = false;
		QVector<ModelEdge> candidates;
		for (auto edge : incident.value(v))
		{
			if (!work.step())
				return false;
			const auto faces = g.edgeFaces.value(edge);
			if (faces.size() > 2 ||
				(faces.size() == 2 && winding(g, surface.triangles[faces[0]], edge) == winding(g, surface.triangles[faces[1]], edge)))
				return refuse(RipText::tr("Repair nonmanifold or inconsistently wound rip neighbourhoods first."));
			boundary |= faces.size() == 1;
			if (faces.size() == 2 && !plan.cuts.contains(edge) && !quads.diagonals.contains(edge))
				candidates.append(edge);
		}
		// Junctions of three or more selected edges need no extension: their
		// sectors become separate fans below.
		const int degree = selectedNeighbours.value(v).size();
		const int needed = degree == 0 ? (boundary ? 1 : 2) : degree == 1 && !boundary ? 1 : 0;
		auto direction =
			degree == 1
				? unit(position(surface, options.referenceFrame, selectedNeighbours[v][0]) - position(surface, options.referenceFrame, v))
				: P3{};
		const auto hint = unit(seedCentre - position(surface, options.referenceFrame, v));
		for (int n = 0; n < needed; ++n)
		{
			int best = -1;
			double bestScore = std::numeric_limits<double>::infinity();
			for (int i = 0; i < candidates.size(); ++i)
			{
				if (!work.step())
					return false;
				const auto vector =
					unit(position(surface, options.referenceFrame, other(candidates[i], v)) - position(surface, options.referenceFrame, v));
				const double score = length(direction) > 0 ? dot(direction, vector) : std::abs(dot(hint, vector));
				if (score < bestScore - 1e-12)
				{
					bestScore = score;
					best = i;
				}
			}
			if (best < 0)
				return refuse(RipText::tr("The rip cannot continue across this endpoint. Choose another chain or side face."));
			const auto edge = candidates.takeAt(best);
			plan.cuts.insert(edge);
			direction = unit(position(surface, options.referenceFrame, other(edge, v)) - position(surface, options.referenceFrame, v));
		}
	}
	QHash<int, QVector<int>> adjacent;
	for (auto edge : ordered(allEdges))
	{
		if (!work.step())
			return false;
		const auto faces = g.edgeFaces.value(edge);
		if (faces.size() == 2 && !plan.cuts.contains(edge) && (plan.groups.contains(edge.first) || plan.groups.contains(edge.second)))
		{
			adjacent[faces[0]].append(faces[1]);
			adjacent[faces[1]].append(faces[0]);
		}
	}
	pending = {options.ripFace};
	while (!pending.isEmpty())
	{
		if (!work.step())
			return false;
		const int face = pending.takeLast();
		if (plan.movingFaces.contains(face))
			continue;
		plan.movingFaces.insert(face);
		for (int next : adjacent.value(face))
			if (!plan.movingFaces.contains(next))
				pending.append(next);
	}
	if (options.ripOpposite)
		plan.movingFaces = touched - plan.movingFaces;
	bool branched = false;
	for (int v : groups)
		branched = branched || selectedNeighbours.value(v).size() > 2;
	QSet<ModelEdge> actual;
	if (!branched)
	{
		// Each side at every moved vertex must remain one connected fan. A global
		// side flood alone cannot detect a bow-tie vertex.
		for (int v : groups)
			for (bool moving : {false, true})
			{
				QSet<int> side;
				for (int f : g.groupFaces[v])
					if (plan.movingFaces.contains(f) == moving)
						side.insert(f);
				if (side.isEmpty())
					return refuse(RipText::tr("The chosen rip side does not divide every selected vertex."));
				QHash<int, QVector<int>> fan;
				for (auto edge : incident[v])
				{
					if (!work.step())
						return false;
					const auto faces = g.edgeFaces.value(edge);
					if (faces.size() == 2 && side.contains(faces[0]) && side.contains(faces[1]))
					{
						fan[faces[0]].append(faces[1]);
						fan[faces[1]].append(faces[0]);
					}
				}
				QSet<int> visited;
				pending = {ordered(side).first()};
				while (!pending.isEmpty())
				{
					if (!work.step())
						return false;
					const int f = pending.takeLast();
					if (visited.contains(f))
						continue;
					visited.insert(f);
					for (int next : fan.value(f))
						if (!visited.contains(next))
							pending.append(next);
				}
				if (visited != side)
					return refuse(
						RipText::tr("The rip would leave a disconnected vertex fan. Repair the neighbourhood or choose another side."));
			}
		for (auto edge : ordered(plan.cuts))
		{
			if (!work.step())
				return false;
			const auto faces = g.edgeFaces.value(edge);
			if (faces.size() == 2 && plan.movingFaces.contains(faces[0]) != plan.movingFaces.contains(faces[1]))
				actual.insert(edge);
		}
	}
	else
	{
		// Branched rips: around every selected vertex, the faces between cut
		// edges form fans (sectors); each fan gets its own copy of the vertex.
		// The first unmoved fan keeps the original. A vertex whose faces are
		// not one fan before cutting (a bow tie) is refused.
		if (plan.movingFaces.isEmpty())
			return refuse(RipText::tr("The chosen rip side does not divide every selected vertex."));
		for (int v : groups)
		{
			QHash<int, QVector<int>> link, whole;
			for (auto edge : incident.value(v))
			{
				if (!work.step())
					return false;
				const auto faces = g.edgeFaces.value(edge);
				if (faces.size() != 2)
					continue;
				whole[faces[0]].append(faces[1]);
				whole[faces[1]].append(faces[0]);
				if (!plan.cuts.contains(edge))
				{
					link[faces[0]].append(faces[1]);
					link[faces[1]].append(faces[0]);
				}
			}
			const auto flood = [&](const QHash<int, QVector<int>> &graph, int start, QSet<int> *visited)
			{
				pending = {start};
				while (!pending.isEmpty())
				{
					const int f = pending.takeLast();
					if (visited->contains(f))
						continue;
					visited->insert(f);
					for (int next : graph.value(f))
						if (!visited->contains(next))
							pending.append(next);
				}
			};
			const auto around = g.groupFaces[v];
			QSet<int> all;
			flood(whole, around.first(), &all);
			if (all.size() != around.size())
				return refuse(
					RipText::tr("The rip would leave a disconnected vertex fan. Repair the neighbourhood or choose another side."));
			QVector<QSet<int>> fans;
			QSet<int> assigned;
			for (int f : around)
			{
				if (assigned.contains(f))
					continue;
				QSet<int> fan;
				flood(link, f, &fan);
				assigned += fan;
				fans.append(fan);
			}
			if (fans.size() < 2)
				return refuse(RipText::tr("The chosen rip side does not divide every selected vertex."));
			int base = 0;
			for (int i = fans.size() - 1; i >= 0; --i)
				if (!plan.movingFaces.contains(*fans[i].cbegin()))
					base = i;
			int next = 1;
			for (int i = 0; i < fans.size(); ++i)
			{
				const int id = i == base ? 0 : next++;
				for (int f : std::as_const(fans[i]))
					plan.fans.insert(qMakePair(v, f), id);
			}
		}
		for (auto edge : ordered(plan.cuts))
		{
			if (!work.step())
				return false;
			const auto faces = g.edgeFaces.value(edge);
			if (faces.size() != 2)
				continue;
			bool opens = true;
			for (int v : {edge.first, edge.second})
				if (plan.groups.contains(v))
					opens = opens && plan.fans.value(qMakePair(v, faces[0])) != plan.fans.value(qMakePair(v, faces[1]));
			if (opens)
				actual.insert(edge);
		}
	}
	if (actual.isEmpty() || !(plan.selectedEdges - actual).isEmpty())
		return refuse(RipText::tr("The chosen side does not open every selected rip edge."));
	plan.cuts = std::move(actual);
	if (!work.check())
		return false;
	*result = std::move(plan);
	return true;
}
} // namespace vibestudio
