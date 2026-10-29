#include "core/model_mesh_tools_p.h"

#include <QMap>
#include <algorithm>
#include <array>
#include <cmath>

// Intersect: cut faces along the lines where the selection passes through
// the other faces of its surface (or of another surface), as Blender's
// Intersect (Knife) and Intersect (Boolean) do (manual 4.0,
// modeling/meshes/editing/face/
// intersect_knife.html and intersect_boolean.html, reviewed 2026-10-28).
// The triangle intersection follows Möller's interval method ("A Fast
// Triangle-Triangle Intersection Test", Journal of Graphics Tools 2(2),
// 1997) and inside tests use ray parity with the Möller-Trumbore ray-triangle
// test (1997). No code is used.
namespace vibestudio::model_tools
{
namespace
{
struct IntersectText
{
	Q_DECLARE_TR_FUNCTIONS(ModelIntersectCut)
};
using Triangle = std::array<P3, 3>;
enum class Contact
{
	None,
	Segment,
	Coplanar
};
// The points where the triangle meets the plane (signed distances d), at
// most two after merging repeats.
QVector<P3> planePoints(const Triangle &t, const double d[3], double epsilon)
{
	QVector<P3> points;
	const auto add = [&](P3 p)
	{
		for (const auto &q : std::as_const(points))
		{
			if (length(q - p) <= epsilon)
				return;
		}
		points << p;
	};
	for (int i = 0; i < 3; ++i)
	{
		if (std::abs(d[i]) <= epsilon)
			add(t[i]);
	}
	for (int i = 0; i < 3; ++i)
	{
		const int j = (i + 1) % 3;
		if ((d[i] > epsilon && d[j] < -epsilon) || (d[i] < -epsilon && d[j] > epsilon))
			add(t[i] + (t[j] - t[i]) * (d[i] / (d[i] - d[j])));
	}
	return points;
}
Contact intersect(const Triangle &a, const Triangle &b, double epsilon, P3 *from, P3 *to)
{
	const P3 na = cross(a[1] - a[0], a[2] - a[0]), nb = cross(b[1] - b[0], b[2] - b[0]);
	const double la = length(na), lb = length(nb);
	if (la <= 1e-20 || lb <= 1e-20)
		return Contact::None;
	double da[3], db[3];
	for (int i = 0; i < 3; ++i)
	{
		da[i] = dot(nb, a[i] - b[0]) / lb;
		db[i] = dot(na, b[i] - a[0]) / la;
	}
	const auto allAbove = [&](const double d[3]) { return d[0] > epsilon && d[1] > epsilon && d[2] > epsilon; };
	const auto allBelow = [&](const double d[3]) { return d[0] < -epsilon && d[1] < -epsilon && d[2] < -epsilon; };
	if (allAbove(da) || allBelow(da) || allAbove(db) || allBelow(db))
		return Contact::None;
	if (std::abs(da[0]) <= epsilon && std::abs(da[1]) <= epsilon && std::abs(da[2]) <= epsilon)
		return Contact::Coplanar;
	const auto pa = planePoints(a, da, epsilon), pb = planePoints(b, db, epsilon);
	if (pa.size() < 2 || pb.size() < 2)
		return Contact::None;
	const P3 direction = unit(cross(na, nb));
	const auto along = [&](P3 p) { return dot(p, direction); };
	const auto ordered = [&](const QVector<P3> &points, P3 *low, P3 *high)
	{
		*low = *high = points[0];
		for (const auto &p : points)
		{
			if (along(p) < along(*low))
				*low = p;
			if (along(p) > along(*high))
				*high = p;
		}
	};
	P3 a0, a1, b0, b1;
	ordered(pa, &a0, &a1);
	ordered(pb, &b0, &b1);
	const double start = std::max(along(a0), along(b0)), end = std::min(along(a1), along(b1));
	if (end - start <= epsilon)
		return Contact::None;
	// Points on the first triangle's segment at those positions along the line.
	const double span = along(a1) - along(a0);
	if (span <= 1e-20)
		return Contact::None;
	*from = a0 + (a1 - a0) * ((start - along(a0)) / span);
	*to = a0 + (a1 - a0) * ((end - along(a0)) / span);
	return Contact::Segment;
}
// Weights of B and C for a point on the triangle.
std::array<double, 2> barycentric(const Triangle &t, P3 p)
{
	const P3 v0 = t[1] - t[0], v1 = t[2] - t[0], v2 = p - t[0];
	const double d00 = dot(v0, v0), d01 = dot(v0, v1), d11 = dot(v1, v1), d20 = dot(v2, v0), d21 = dot(v2, v1);
	const double denominator = d00 * d11 - d01 * d01;
	if (std::abs(denominator) <= 1e-30)
		return {0, 0};
	double b = (d11 * d20 - d01 * d21) / denominator, c = (d00 * d21 - d01 * d20) / denominator;
	b = std::clamp(b, 0.0, 1.0);
	c = std::clamp(c, 0.0, 1.0);
	if (b + c > 1)
	{
		const double sum = b + c;
		b /= sum;
		c /= sum;
	}
	return {b, c};
}
// Möller-Trumbore: whether the ray from origin along direction crosses the triangle.
bool crosses(P3 origin, P3 direction, const Triangle &t)
{
	const P3 e1 = t[1] - t[0], e2 = t[2] - t[0], h = cross(direction, e2);
	const double a = dot(e1, h);
	if (std::abs(a) <= 1e-14)
		return false;
	const double f = 1 / a;
	const P3 s = origin - t[0];
	const double u = f * dot(s, h);
	if (u < 0 || u > 1)
		return false;
	const P3 q = cross(s, e1);
	const double v = f * dot(direction, q);
	if (v < 0 || u + v > 1)
		return false;
	return f * dot(e2, q) > 1e-9;
}
Triangle triangleAt(const ModelSurface &surface, int frame, const ModelTriangle &t)
{
	return {position(surface, frame, t.a), position(surface, frame, t.b), position(surface, frame, t.c)};
}
// Faces of the pieces (joined through shared geometric vertices) holding the seeds.
QSet<int> piecesOf(const ModelSurface &surface, const ModelGeometricTopology &topology, const QSet<int> &seeds)
{
	QSet<int> faces, groups;
	QVector<int> stack(seeds.cbegin(), seeds.cend());
	while (!stack.isEmpty())
	{
		const int face = stack.takeLast();
		if (faces.contains(face))
			continue;
		faces.insert(face);
		const auto &t = surface.triangles[face];
		for (int v : {t.a, t.b, t.c})
		{
			const int group = topology.group(v);
			if (groups.contains(group))
				continue;
			groups.insert(group);
			for (int other : topology.groupFaces.value(group))
				stack << other;
		}
	}
	return faces;
}
// Every geometric edge of these faces is used by exactly two of them.
bool closed(const ModelSurface &surface, const ModelGeometricTopology &topology, const QSet<int> &faces)
{
	QHash<ModelEdge, int> uses;
	for (int face : faces)
	{
		const auto &t = surface.triangles[face];
		for (const auto edge : {topology.geometricEdge(t.a, t.b), topology.geometricEdge(t.b, t.c), topology.geometricEdge(t.c, t.a)})
			++uses[edge];
	}
	return !uses.isEmpty() && std::all_of(uses.cbegin(), uses.cend(), [](int count) { return count == 2; });
}
// A uniform grid of face bounds, so a face meets only the faces near it
// instead of every face of the other part.
class FaceGrid
{
  public:
	FaceGrid(const QVector<std::array<double, 6>> &boxes, const QVector<int> &faces, double minimumCell)
	{
		double extent = 0;
		for (int f : faces)
			extent += std::max({boxes[f][3] - boxes[f][0], boxes[f][4] - boxes[f][1], boxes[f][5] - boxes[f][2]});
		m_cell = std::max(faces.isEmpty() ? 1.0 : extent / faces.size(), minimumCell);
		for (int f : faces)
		{
			const auto low = cellOf(boxes[f][0], boxes[f][1], boxes[f][2]), high = cellOf(boxes[f][3], boxes[f][4], boxes[f][5]);
			// A face spanning very many cells is tested against everything.
			const qint64 span = (high[0] - low[0] + 1) * (high[1] - low[1] + 1) * (high[2] - low[2] + 1);
			if (span > 512)
			{
				m_large << f;
				continue;
			}
			for (qint64 x = low[0]; x <= high[0]; ++x)
				for (qint64 y = low[1]; y <= high[1]; ++y)
					for (qint64 z = low[2]; z <= high[2]; ++z)
						m_cells[key(x, y, z)] << f;
		}
	}
	// Faces whose cells overlap the box, ascending and without repeats.
	QVector<int> near(const std::array<double, 6> &box) const
	{
		QVector<int> result = m_large;
		const auto low = cellOf(box[0], box[1], box[2]), high = cellOf(box[3], box[4], box[5]);
		const qint64 span = (high[0] - low[0] + 1) * (high[1] - low[1] + 1) * (high[2] - low[2] + 1);
		if (span > 4096)
		{
			for (const auto &list : m_cells)
				result += list;
		}
		else
		{
			for (qint64 x = low[0]; x <= high[0]; ++x)
				for (qint64 y = low[1]; y <= high[1]; ++y)
					for (qint64 z = low[2]; z <= high[2]; ++z)
					{
						const auto found = m_cells.constFind(key(x, y, z));
						if (found != m_cells.cend())
							result += found.value();
					}
		}
		std::sort(result.begin(), result.end());
		result.erase(std::unique(result.begin(), result.end()), result.end());
		return result;
	}

  private:
	std::array<qint64, 3> cellOf(double x, double y, double z) const
	{
		return {qint64(std::floor(x / m_cell)), qint64(std::floor(y / m_cell)), qint64(std::floor(z / m_cell))};
	}
	static quint64 key(qint64 x, qint64 y, qint64 z)
	{
		return (quint64(x & 0x1fffff) << 42) | (quint64(y & 0x1fffff) << 21) | quint64(z & 0x1fffff);
	}
	double m_cell = 1;
	QHash<quint64, QVector<int>> m_cells;
	QVector<int> m_large;
};
// Ray parity against one closed part: for each ray direction, the part's
// faces projected onto the plane across the ray and binned in a 2D grid, so
// a ray meets only the faces whose projection covers its start.
class RayParity
{
  public:
	static constexpr int rays = 5;
	RayParity(const ModelSurface &surface, int frame, const QVector<int> &faces) : m_surface(surface), m_frame(frame)
	{
		// Five skewed rays vote, so a ray that grazes an edge or a vertex of
		// the other part (and counts it twice or not at all) cannot decide alone.
		const P3 directions[rays]{unit(P3{0.5773, 0.5774, 0.5775}), unit(P3{-0.31, 0.82, 0.48}), unit(P3{0.71, -0.29, 0.64}),
								  unit(P3{-0.43, -0.56, 0.71}), unit(P3{0.12, 0.33, -0.94})};
		for (int r = 0; r < rays; ++r)
		{
			auto &axis = m_axes[r];
			axis.direction = directions[r];
			const P3 helper = std::abs(axis.direction.x) < 0.9 ? P3{1, 0, 0} : P3{0, 1, 0};
			axis.u = unit(cross(axis.direction, helper));
			axis.v = cross(axis.direction, axis.u);
			QVector<std::array<double, 4>> boxes;
			double extent = 0;
			for (int f : faces)
			{
				const auto t = triangleAt(surface, frame, surface.triangles[f]);
				std::array<double, 4> box{1e300, 1e300, -1e300, -1e300};
				for (const auto &p : t)
				{
					const double u = dot(p, axis.u), v = dot(p, axis.v);
					box = {std::min(box[0], u), std::min(box[1], v), std::max(box[2], u), std::max(box[3], v)};
				}
				boxes << box;
				extent += std::max(box[2] - box[0], box[3] - box[1]);
			}
			axis.cell = std::max(faces.isEmpty() ? 1.0 : extent / faces.size(), 1e-6);
			for (int i = 0; i < faces.size(); ++i)
			{
				const auto &box = boxes[i];
				const qint64 x0 = qint64(std::floor(box[0] / axis.cell)), y0 = qint64(std::floor(box[1] / axis.cell));
				const qint64 x1 = qint64(std::floor(box[2] / axis.cell)), y1 = qint64(std::floor(box[3] / axis.cell));
				if ((x1 - x0 + 1) * (y1 - y0 + 1) > 1024)
				{
					axis.large << faces[i];
					continue;
				}
				for (qint64 x = x0; x <= x1; ++x)
					for (qint64 y = y0; y <= y1; ++y)
						axis.cells[key(x, y)] << faces[i];
			}
		}
	}
	// Whether the point lies inside the part: a majority of rays cross it an odd number of times.
	bool inside(P3 point) const
	{
		int votes = 0;
		for (const auto &axis : m_axes)
		{
			const qint64 x = qint64(std::floor(dot(point, axis.u) / axis.cell)), y = qint64(std::floor(dot(point, axis.v) / axis.cell));
			int count = 0;
			for (int f : axis.cells.value(key(x, y)))
				count += crosses(point, axis.direction, triangleAt(m_surface, m_frame, m_surface.triangles[f]));
			for (int f : axis.large)
				count += crosses(point, axis.direction, triangleAt(m_surface, m_frame, m_surface.triangles[f]));
			votes += count % 2;
		}
		return votes >= 3;
	}

  private:
	struct Axis
	{
		P3 direction, u, v;
		double cell = 1;
		QHash<quint64, QVector<int>> cells;
		QVector<int> large;
	};
	static quint64 key(qint64 x, qint64 y) { return (quint64(x & 0xffffffff) << 32) | quint64(y & 0xffffffff); }
	const ModelSurface &m_surface;
	int m_frame;
	std::array<Axis, rays> m_axes;
};
QVector<std::array<double, 6>> faceBoxes(const ModelSurface &surface, int frame)
{
	QVector<std::array<double, 6>> boxes(surface.triangles.size());
	for (int f = 0; f < surface.triangles.size(); ++f)
	{
		const auto t = triangleAt(surface, frame, surface.triangles[f]);
		auto &box = boxes[f];
		box = {t[0].x, t[0].y, t[0].z, t[0].x, t[0].y, t[0].z};
		for (const auto &p : t)
		{
			box[0] = std::min(box[0], p.x), box[1] = std::min(box[1], p.y), box[2] = std::min(box[2], p.z);
			box[3] = std::max(box[3], p.x), box[4] = std::max(box[4], p.y), box[5] = std::max(box[5], p.z);
		}
	}
	return boxes;
}
} // namespace

bool intersectCut(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control,
				  ModelEditReport *report)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	const auto mode = edit.tool.intersectMode;
	if (int(mode) < 0 || int(mode) > int(ModelIntersectMode::Intersection))
		return fail(error, IntersectText::tr("Choose cut, union, difference or intersection."));
	if (selection->faces.isEmpty())
		return fail(error, IntersectText::tr("Select the faces that pass through the rest of the surface."));
	const int s = selection->surface;
	const int o = edit.tool.intersectSurface < 0 ? s : edit.tool.intersectSurface;
	if (o >= mesh->surfaces.size())
		return fail(error, IntersectText::tr("Choose an existing surface to intersect with."));
	// Across surfaces, the other surface's faces cut the selection and are cut
	// in turn; the two surfaces keep their own vertices.
	const bool across = o != s;
	const auto before = mesh->surfaces[s];
	const auto otherBefore = mesh->surfaces[o];
	const int frame = edit.tool.referenceFrame;
	if (frame < 0 || frame >= before.frames.size() || frame >= otherBefore.frames.size())
		return fail(error, IntersectText::tr("Choose a pose that both parts have."));
	if (mode != ModelIntersectMode::Cut && (!before.skinning.isEmpty() || !otherBefore.skinning.isEmpty()))
		return fail(error, IntersectText::tr("Union, difference and intersection need unskinned surfaces. Cut the skinned surface, then delete the "
											 "hidden faces."));
	ModelGeometricTopology topology, otherTopology;
	if (!buildModelGeometricTopology(before, &topology, error, control) ||
		(across && !buildModelGeometricTopology(otherBefore, &otherTopology, error, control)))
		return false;
	const auto &partTopology = across ? otherTopology : topology;
	double scale = 1;
	for (const auto *part : {&before, &otherBefore})
		for (const auto &p : part->frames[frame].positions)
			scale = std::max({scale, double(std::abs(p.x)), double(std::abs(p.y)), double(std::abs(p.z))});
	const double epsilon = scale * 1e-7;
	// Bounds of every face in the reference pose, for a quick rejection.
	const auto boxes = faceBoxes(before, frame);
	const auto otherBoxes = across ? faceBoxes(otherBefore, frame) : boxes;
	const auto overlap = [&](int a, int b)
	{
		for (int axis = 0; axis < 3; ++axis)
		{
			if (boxes[a][axis] > otherBoxes[b][axis + 3] + epsilon || otherBoxes[b][axis] > boxes[a][axis + 3] + epsilon)
				return false;
		}
		return true;
	};
	auto selected = selection->faces.values();
	std::sort(selected.begin(), selected.end());
	const QSet<int> selectedSet(selected.cbegin(), selected.cend());
	for (int a : std::as_const(selected))
	{
		if (a < 0 || a >= before.triangles.size())
			return fail(error, IntersectText::tr("A selected face no longer exists."));
	}
	QVector<int> pool;
	for (int b = 0; b < otherBefore.triangles.size(); ++b)
	{
		if (across || !selectedSet.contains(b))
			pool << b;
	}
	const FaceGrid grid(otherBoxes, pool, epsilon * 1000);
	// Same surface: one list of cuts in pair order. Across: one per surface.
	QVector<ModelKnifeCut> cuts, otherCuts;
	QSet<int> partners;
	for (int a : std::as_const(selected))
	{
		const auto &ta = before.triangles[a];
		const int groups[3]{topology.group(ta.a), topology.group(ta.b), topology.group(ta.c)};
		std::array<double, 6> box = boxes[a];
		for (int axis = 0; axis < 3; ++axis)
		{
			box[axis] -= epsilon;
			box[axis + 3] += epsilon;
		}
		for (int b : grid.near(box))
		{
			if (!work.step())
				return false;
			if (!overlap(a, b))
				continue;
			const auto &tb = otherBefore.triangles[b];
			if (!across)
			{
				bool touching = false;
				for (int v : {tb.a, tb.b, tb.c})
					touching = touching || topology.group(v) == groups[0] || topology.group(v) == groups[1] || topology.group(v) == groups[2];
				if (touching)
					continue;
			}
			const auto first = triangleAt(before, frame, ta), second = triangleAt(otherBefore, frame, tb);
			P3 from, to;
			const auto contact = intersect(first, second, epsilon, &from, &to);
			if (contact == Contact::Coplanar)
			{
				// Coplanar faces that only touch are fine; overlapping ones cannot be cut.
				const auto within = [&](const Triangle &outer, P3 p)
				{
					const auto w = barycentric(outer, p);
					return length(outer[0] + (outer[1] - outer[0]) * w[0] + (outer[2] - outer[0]) * w[1] - p) <= epsilon;
				};
				const P3 centreA = (first[0] + first[1] + first[2]) * (1.0 / 3), centreB = (second[0] + second[1] + second[2]) * (1.0 / 3);
				if (within(first, centreB) || within(second, centreA))
					return fail(error, IntersectText::tr("Faces %1 and %2 overlap in one plane. Move one part slightly before intersecting.").arg(a).arg(b));
				continue;
			}
			if (contact != Contact::Segment || length(to - from) <= epsilon)
				continue;
			cuts << ModelKnifeCut{a, barycentric(first, from), barycentric(first, to)};
			(across ? otherCuts : cuts) << ModelKnifeCut{b, barycentric(second, from), barycentric(second, to)};
			partners.insert(b);
		}
	}
	if (cuts.isEmpty())
		return fail(error, across ? IntersectText::tr("The selected faces do not pass through any faces of surface %1.").arg(o)
								  : IntersectText::tr("The selected faces do not pass through any other faces of the surface."));
	const auto partPieces = piecesOf(otherBefore, partTopology, partners);
	if (mode != ModelIntersectMode::Cut && (!closed(before, topology, selectedSet) || !closed(otherBefore, partTopology, partPieces)))
		return fail(error, IntersectText::tr("Union, difference and intersection need closed parts: select a whole closed piece that passes "
											 "through another closed piece."));
	// Cut both parts with the knife's apply stage.
	const int firstNew = before.vertexCount;
	ModelSelection knifeSelection = *selection;
	ModelEditReport knifeReport, otherReport;
	if (!applyModelKnifeCuts(mesh, edit, &knifeSelection, cuts, error, control, &knifeReport))
		return false;
	ModelSelection otherSelection;
	otherSelection.surface = o;
	if (across && !applyModelKnifeCuts(mesh, edit, &otherSelection, otherCuts, error, control, &otherReport))
		return false;
	auto &surface = mesh->surfaces[s];
	auto &otherSurface = mesh->surfaces[o];
	const auto sources = knifeReport.sourceFaces.value(s);
	const auto otherSources = across ? otherReport.sourceFaces.value(o) : sources;
	if (sources.size() != surface.triangles.size() || otherSources.size() != otherSurface.triangles.size())
		return fail(error, IntersectText::tr("The cut lost track of its faces."));
	// Join each cut point on the other part to the matching cut point on the
	// selection: one position and identity in every pose where they agree.
	// Separate surfaces keep their own vertices.
	if (!across)
	{
		QSet<int> fromSelection, fromOthers;
		for (int face = 0; face < surface.triangles.size(); ++face)
		{
			const auto &t = surface.triangles[face];
			auto &side = selectedSet.contains(sources[face]) ? fromSelection : fromOthers;
			for (int v : {t.a, t.b, t.c})
			{
				if (v >= firstNew)
					side.insert(v);
			}
		}
		int separate = 0;
		if (surface.skinning.isEmpty() && !surface.topologyVertices.isEmpty())
		{
			for (int v : std::as_const(fromOthers))
			{
				if (!work.step())
					return false;
				int match = -1;
				double best = epsilon * 100;
				for (int w : std::as_const(fromSelection))
				{
					const double d = length(position(surface, frame, v) - position(surface, frame, w));
					if (d <= best)
					{
						best = d;
						match = w;
					}
				}
				bool everyPose = match >= 0;
				for (int f = 0; f < surface.frames.size() && everyPose; ++f)
					everyPose = length(position(surface, f, v) - position(surface, f, match)) <= epsilon * 100;
				if (!everyPose)
				{
					++separate;
					continue;
				}
				for (auto &pose : surface.frames)
					pose.positions[v] = pose.positions[match];
				surface.topologyVertices[v] = surface.topologyVertices[match];
			}
		}
		else
			separate = int(fromOthers.size());
		if (separate > 0 && report)
			report->notes << IntersectText::tr("%n cut point(s) on the other part stay separate from the selection's, because the parts move apart in "
											   "another pose or follow the skeleton.",
											   nullptr, separate);
	}
	if (mode == ModelIntersectMode::Cut)
	{
		*selection = knifeSelection;
		if (report)
		{
			report->sourceFaces.insert(s, sources);
			if (across)
				report->sourceFaces.insert(o, otherSources);
		}
		return work.check();
	}
	// Union, difference and intersection: classify every face of both parts
	// by ray parity against the other part in the reference pose.
	QVector<int> partA, partB;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (selectedSet.contains(sources[face]))
			partA << face;
		else if (!across && partPieces.contains(sources[face]))
			partB << face;
	}
	if (across)
	{
		for (int face = 0; face < otherSurface.triangles.size(); ++face)
		{
			if (partPieces.contains(otherSources[face]))
				partB << face;
		}
	}
	const auto centre = [&](const ModelSurface &part, int face)
	{
		const auto t = triangleAt(part, frame, part.triangles[face]);
		return (t[0] + t[1] + t[2]) * (1.0 / 3);
	};
	const RayParity insideA(surface, frame, partA), insideB(otherSurface, frame, partB);
	QVector<bool> drop(surface.triangles.size(), false), flip(surface.triangles.size(), false);
	QVector<bool> otherDrop(across ? otherSurface.triangles.size() : 0, false), otherFlip(across ? otherSurface.triangles.size() : 0, false);
	auto &dropB = across ? otherDrop : drop;
	auto &flipB = across ? otherFlip : flip;
	for (int face : std::as_const(partA))
	{
		if (!work.step())
			return false;
		const bool within = insideB.inside(centre(surface, face));
		drop[face] = mode == ModelIntersectMode::Intersection ? !within : within;
	}
	for (int face : std::as_const(partB))
	{
		if (!work.step())
			return false;
		const bool within = insideA.inside(centre(otherSurface, face));
		if (mode == ModelIntersectMode::Union)
			dropB[face] = within;
		else if (mode == ModelIntersectMode::Intersection)
			dropB[face] = !within;
		else
		{
			dropB[face] = !within;
			flipB[face] = within;
		}
	}
	// Rebuild one surface without its dropped faces, flipped ones reversed.
	const auto rebuild = [&](ModelSurface &part, const QVector<bool> &dropped, const QVector<bool> &flipped, const QVector<int> &partSources,
							 ModelSelection *partSelection, bool selectionFaces, QVector<int> *keptSources)
	{
		QVector<ModelTriangle> kept;
		QSet<int> result, touched, candidates;
		for (int face = 0; face < part.triangles.size(); ++face)
		{
			auto t = part.triangles[face];
			candidates << t.a << t.b << t.c;
			if (dropped[face])
				continue;
			if (flipped[face])
			{
				std::swap(t.b, t.c);
				touched << t.a << t.b << t.c;
			}
			if (selectionFaces && selectedSet.contains(partSources[face]))
				result.insert(kept.size());
			kept << t;
			*keptSources << partSources[face];
		}
		part.triangles = kept;
		if (!refreshNormals(&part, touched, work))
			return false;
		partSelection->vertices.clear();
		partSelection->edges.clear();
		partSelection->faces = result;
		if (!compactUnused(&part, candidates, partSelection, work))
			return false;
		pruneSeams(&part);
		return true;
	};
	QVector<int> keptSources, otherKept;
	if (!rebuild(surface, drop, flip, sources, selection, true, &keptSources) ||
		(across && !rebuild(otherSurface, otherDrop, otherFlip, otherSources, &otherSelection, false, &otherKept)))
		return false;
	if (surface.triangles.isEmpty() || otherSurface.triangles.isEmpty())
		return fail(error, IntersectText::tr("Nothing would remain of a part. Check that the parts overlap as intended."));
	if (report)
	{
		report->sourceFaces.insert(s, keptSources);
		if (across)
			report->sourceFaces.insert(o, otherKept);
	}
	return work.check();
}
} // namespace vibestudio::model_tools
