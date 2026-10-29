#include "core/model_mesh_tools_p.h"

#include <QMap>
#include <algorithm>
#include <cmath>

// Extrude Faces: Region, Along Normals and Individual Faces, following the
// documented behaviour of Blender's Extrude Region, Extrude Region (Shrink/
// Fatten) and Extrude Individual Faces operators and 3ds Max's Extrude
// Polygons Group, Local Normal and By Polygon types (Blender manual 4.0,
// modeling/meshes/editing/face/extrude_faces*.html; reviewed 2026-10-28). No
// code is used. Side-wall UV strips follow the strip layout of Bevel Edges.
namespace vibestudio::model_tools
{
namespace
{
struct ExtrudeText
{
	Q_DECLARE_TR_FUNCTIONS(ModelExtrude)
};
QVector<int> sortedFaces(const QSet<int> &values)
{
	auto result = values.values().toVector();
	std::sort(result.begin(), result.end());
	return result;
}
// A region border edge in its face's winding.
struct Border
{
	int a = -1, b = -1, face = -1;
};
} // namespace

bool extrudeFaces(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control,
				  ModelEditReport *report)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	const auto &options = edit.tool;
	const double distance = options.offset, tile = options.extrudeTileSize;
	if (!std::isfinite(distance) || distance == 0 || std::abs(distance) > 1000000)
		return fail(error, ExtrudeText::tr("Extrude needs a nonzero distance within ±1,000,000 units."));
	if (!std::isfinite(tile) || tile < 0 || tile > 1000000 || int(options.extrudeMode) < 0 ||
		int(options.extrudeMode) > int(ModelExtrudeMode::Individual))
		return fail(error, ExtrudeText::tr("Choose region, along normals or individual extrusion, and zero or a positive UV tile size."));
	const auto faces = sortedFaces(selection->faces);
	if (faces.isEmpty())
		return fail(error, ExtrudeText::tr("Select the faces to extrude."));
	auto &surface = mesh->surfaces[selection->surface];
	for (int face : faces)
	{
		if (face < 0 || face >= surface.triangles.size())
			return fail(error, ExtrudeText::tr("A selected face no longer exists."));
	}
	if (!materialiseModelVertexIdentities(&surface, error, control))
		return false;
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error, control))
		return false;
	const bool individual = options.extrudeMode == ModelExtrudeMode::Individual;
	QVector<QVector<int>> regions;
	if (individual)
	{
		for (int face : faces)
			regions.append(QVector<int>{face});
	}
	else
		regions.append(faces);
	// Borders of each region: geometric edges used by one of its faces.
	QVector<QVector<Border>> borders;
	qint64 addedVertices = 0, addedTriangles = 0;
	for (const auto &region : std::as_const(regions))
	{
		QMap<ModelEdge, QPair<int, Border>> uses;
		QHash<ModelEdge, int> balance;
		for (int face : region)
		{
			if (!work.step())
				return false;
			const auto &t = surface.triangles[face];
			for (const auto &[a, b] : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
			{
				const auto key = topology.geometricEdge(a, b);
				auto &use = uses[key];
				++use.first;
				use.second = {a, b, face};
				balance[key] += topology.group(a) == key.first ? 1 : -1;
			}
		}
		QVector<Border> border;
		for (auto i = uses.cbegin(); i != uses.cend(); ++i)
		{
			if (i->first > 2 || (i->first == 2 && balance.value(i.key()) != 0))
				return fail(error, ExtrudeText::tr("Extrusion needs consistently wound faces with at most two faces per edge."));
			if (i->first == 1)
				border.append(i->second);
		}
		borders.append(border);
		QSet<int> corners;
		for (int face : region)
		{
			const auto &t = surface.triangles[face];
			corners << t.a << t.b << t.c;
		}
		addedVertices += corners.size() + (tile > 0 ? 4 * qint64(border.size()) : 0);
		addedTriangles += 2 * qint64(border.size());
	}
	if (!reserveCapacity(*mesh, selection->surface, addedVertices, addedTriangles, error))
		return false;
	const int frames = int(surface.frames.size());
	const QSet<int> chosen(faces.cbegin(), faces.cend());
	QVector<ModelTriangle> kept, added;
	QVector<int> keptSources, addedSources;
	QVector<int> caps;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!chosen.contains(face))
		{
			kept.append(surface.triangles[face]);
			keptSources.append(face);
		}
	}
	QSet<int> touched, candidates;
	const auto seams = surface.uvSeams;
	for (int r = 0; r < regions.size(); ++r)
	{
		const auto &region = regions[r];
		QSet<int> cornerSet;
		for (int face : region)
		{
			const auto &t = surface.triangles[face];
			cornerSet << t.a << t.b << t.c;
		}
		auto corners = cornerSet.values();
		std::sort(corners.begin(), corners.end());
		QSet<int> groups;
		for (int v : std::as_const(corners))
			groups.insert(topology.group(v));
		// Each group's offset in each pose.
		QHash<int, QVector<P3>> shift;
		for (int frame = 0; frame < frames; ++frame)
		{
			if (!work.step())
				return false;
			P3 regionNormal{0, 0, 0};
			for (int face : region)
				regionNormal = regionNormal + faceCross(surface, frame, surface.triangles[face]);
			if (options.extrudeMode != ModelExtrudeMode::AlongNormals && length(regionNormal) <= 1e-12)
				return fail(error, ExtrudeText::tr("The faces to extrude have no overall direction in pose %1. Extrude them along their normals or individually.")
									   .arg(frame));
			regionNormal = unit(regionNormal);
			for (int group : std::as_const(groups))
			{
				P3 move = regionNormal * distance;
				if (options.extrudeMode == ModelExtrudeMode::AlongNormals)
				{
					// The region's own vertex normal, scaled so every face moves the
					// whole distance along its own normal when even thickness is on.
					P3 sum{0, 0, 0};
					QVector<P3> cornersAt;
					for (int face : region)
					{
						const P3 c = matchingCornerNormal(surface, frame, surface.triangles[face],
														  [&](int v) { return topology.group(v) == group; });
						if (length(c) > 0)
						{
							sum = sum + c;
							cornersAt.append(c);
						}
					}
					if (length(sum) <= 1e-12)
						return fail(error, ExtrudeText::tr("A vertex to extrude has no normal in pose %1.").arg(frame));
					const P3 normal = unit(sum);
					double factor = 1;
					if (options.evenThickness)
					{
						double weight = 0, cosine = 0;
						for (const auto &c : std::as_const(cornersAt))
						{
							weight += length(c);
							cosine += length(c) * dot(unit(c), normal);
						}
						if (weight > 0)
							factor = 1 / std::max(cosine / weight, 0.25);
					}
					move = normal * (distance * factor);
				}
				shift[group].append(move);
			}
		}
		// The cap: copies of the region's corners, moved, carrying the faces.
		QHash<int, int> copyOf, identities;
		for (int v : std::as_const(corners))
		{
			if (!work.step())
				return false;
			const int copy = appendCopy(&surface, v);
			detachModelVertexIdentity(&surface, copy, &identities);
			const auto &moves = shift.value(topology.group(v));
			for (int frame = 0; frame < frames; ++frame)
				surface.frames[frame].positions[copy] = vec(position(surface, frame, v) + moves[frame]);
			copyOf.insert(v, copy);
			touched << v << copy;
			candidates.insert(v);
		}
		for (int face : region)
		{
			const auto &t = surface.triangles[face];
			caps.append(added.size());
			added.append({copyOf.value(t.a), copyOf.value(t.b), copyOf.value(t.c)});
			addedSources.append(face);
		}
		for (const auto edge : seams)
		{
			if (copyOf.contains(edge.first) && copyOf.contains(edge.second))
				surface.uvSeams.insert(modelEdge(copyOf.value(edge.first), copyOf.value(edge.second)));
		}
		const auto &border = borders[r];
		if (tile <= 0)
		{
			// Walls share the border and cap vertices and their UVs.
			for (const auto &edge : border)
			{
				added.append({edge.a, edge.b, copyOf.value(edge.b)});
				added.append({edge.a, copyOf.value(edge.b), copyOf.value(edge.a)});
				addedSources << edge.face << edge.face;
			}
			continue;
		}
		// UV walls: border loops become strips with their own UVs, one texture
		// repeat per `tile` units around the loop and up the wall.
		QHash<int, int> outgoing, incoming;
		for (int i = 0; i < border.size(); ++i)
		{
			++outgoing[topology.group(border[i].a)];
			++incoming[topology.group(border[i].b)];
		}
		bool loops = true;
		for (const auto &edge : border)
			loops = loops && outgoing.value(topology.group(edge.a)) == 1 && incoming.value(topology.group(edge.a)) == 1;
		QVector<QPair<QVector<Border>, bool>> chains;
		if (loops)
		{
			QHash<int, int> next;
			for (int i = 0; i < border.size(); ++i)
				next.insert(topology.group(border[i].a), i);
			QSet<int> used;
			for (int start = 0; start < border.size(); ++start)
			{
				if (used.contains(start))
					continue;
				QVector<Border> chain;
				for (int i = start; i >= 0 && !used.contains(i); i = next.value(topology.group(border[i].b), -1))
				{
					used.insert(i);
					chain.append(border[i]);
				}
				chains.append(qMakePair(chain, true));
			}
		}
		else
		{
			// A region that touches itself at a corner: one strip per edge.
			for (const auto &edge : border)
				chains.append(qMakePair(QVector<Border>{edge}, false));
		}
		for (const auto &[chain, closed] : std::as_const(chains))
		{
			const int count = int(chain.size());
			QVector<int> bottom, top;
			double along = 0;
			for (int column = 0; column <= count; ++column)
			{
				if (!work.step())
					return false;
				const int source = column < count ? chain[column].a : chain[count - 1].b;
				if (column > 0)
					along += length(position(surface, options.referenceFrame, chain[column - 1].b) -
									position(surface, options.referenceFrame, chain[column - 1].a));
				const int low = appendCopy(&surface, source), high = appendCopy(&surface, copyOf.value(source));
				const double height = length(position(surface, options.referenceFrame, high) - position(surface, options.referenceFrame, low));
				surface.texCoords[low] = {float(along / tile), 0};
				surface.texCoords[high] = {float(along / tile), float(height / tile)};
				for (auto &frame : surface.frames)
					frame.normals[low] = frame.normals[high] = {0, 0, 0};
				bottom.append(low);
				top.append(high);
				touched << low << high;
			}
			for (int i = 0; i < count; ++i)
			{
				added.append({bottom[i], bottom[i + 1], top[i + 1]});
				added.append({bottom[i], top[i + 1], top[i]});
				addedSources << chain[i].face << chain[i].face;
				surface.uvSeams.insert(modelEdge(bottom[i], bottom[i + 1]));
				surface.uvSeams.insert(modelEdge(top[i], top[i + 1]));
			}
			surface.uvSeams.insert(modelEdge(bottom.first(), top.first()));
			surface.uvSeams.insert(modelEdge(bottom.last(), top.last()));
			Q_UNUSED(closed);
		}
	}
	for (const auto &t : std::as_const(added))
	{
		if (!work.step())
			return false;
		if (collapsedInAnyFrame(surface, t))
			return fail(error, ExtrudeText::tr("The extrusion collapses a face in a pose. Change the distance or the selection."));
	}
	surface.triangles = kept + added;
	auto sources = keptSources + addedSources;
	selection->faces.clear();
	selection->vertices.clear();
	selection->edges.clear();
	for (int cap : std::as_const(caps))
		selection->faces.insert(int(kept.size()) + cap);
	if (!refreshNormals(&surface, touched, work))
		return false;
	// The region's inner corners are no longer used by any face.
	if (!compactUnused(&surface, candidates, selection, work))
		return false;
	pruneSeams(&surface);
	if (report)
		report->sourceFaces.insert(selection->surface, sources);
	return work.check();
}
} // namespace vibestudio::model_tools
