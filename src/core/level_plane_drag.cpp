#include "core/level_plane_drag.h"
#include "core/level_repeat_p.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_visibility.h"
#include "core/map_geometry.h"

#include <QCoreApplication>
#include <QHash>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

// Workflow reference: NetRadiant Custom radiant/selection_mtor_drag.cpp
// DragManipulator and radiant/brush.h BrushInstance::selectPlanes at 449778b3
// (GPL-2.0-or-later). Original implementation; see docs/CREDITS.md.
namespace vibestudio {
namespace {
struct Text {
	Q_DECLARE_TR_FUNCTIONS(LevelPlaneDrag)
};

constexpr int kMaximumBrushes = 256;

double dot(const LevelMapVec3& a, const LevelMapVec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
LevelMapVec3 add(const LevelMapVec3& a, const LevelMapVec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z, true}; }
LevelMapVec3 sub(const LevelMapVec3& a, const LevelMapVec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, true}; }
LevelMapVec3 scaled(const LevelMapVec3& a, double s) { return {a.x * s, a.y * s, a.z * s, true}; }
LevelMapVec3 cross(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, true};
}
double length(const LevelMapVec3& a) { return std::sqrt(dot(a, a)); }
bool finite(const LevelMapVec3& p) { return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
LevelMapVec3 normalised(const LevelMapVec3& a)
{
	const double l = length(a);
	return l > 1e-12 ? scaled(a, 1 / l) : LevelMapVec3 {};
}
double coordinate(const LevelMapVec3& p, int axis) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; }
void setCoordinate(LevelMapVec3* p, int axis, double value)
{
	if (axis == 0) { p->x = value; } else if (axis == 1) { p->y = value; } else { p->z = value; }
}
bool quakeFamily(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
}
LevelMapVec3 normalOf(const MapPlane& plane) { return {plane.normalX, plane.normalY, plane.normalZ, true}; }
bool samePlane(const MapPlane& a, const MapPlane& b)
{
	return std::abs(a.normalX - b.normalX) < 1e-6 && std::abs(a.normalY - b.normalY) < 1e-6 && std::abs(a.normalZ - b.normalZ) < 1e-6
		&& std::abs(a.distance - b.distance) < 1e-3;
}
bool flippedPlane(const MapPlane& a, const MapPlane& b)
{
	return std::abs(a.normalX + b.normalX) < 1e-6 && std::abs(a.normalY + b.normalY) < 1e-6 && std::abs(a.normalZ + b.normalZ) < 1e-6
		&& std::abs(a.distance + b.distance) < 1e-3;
}
LevelMapVec3 centroid(const QVector<LevelMapVec3>& points)
{
	LevelMapVec3 c {0, 0, 0, true};
	for (const auto& p : points) { c = add(c, p); }
	return points.isEmpty() ? c : scaled(c, 1.0 / static_cast<double>(points.size()));
}

struct Target {
	LevelMapBrush brush;
	LevelBrushTopology topology;
};

QVector<Target> targets(const LevelMapDocument& document, const QVector<LevelMapSelectionRef>& selection)
{
	QVector<Target> result;
	const QVector<int> ids = levelPlaneDragBrushIds(document, selection);
	const QSet<int> wanted(ids.cbegin(), ids.cend());
	for (const auto& brush : document.brushes) {
		if (!wanted.contains(brush.id)) { continue; }
		Target target;
		target.brush = brush;
		if (!levelBrushTopology(brush, &target.topology)) { continue; }
		result << target;
	}
	return result;
}

// The point on the press ray at the view depth of `point`.
LevelMapVec3 rayPointAtDepth(const LevelPlaneDragProbe& probe, const LevelMapVec3& point)
{
	const double along = dot(probe.direction, probe.forward);
	if (std::abs(along) < 1e-9) { return probe.origin; }
	return add(probe.origin, scaled(probe.direction, dot(sub(point, probe.origin), probe.forward) / along));
}

void addFace(QVector<LevelMapComponentSet>* sets, int brushId, int face)
{
	for (auto& set : *sets) {
		if (set.object.objectId == brushId) {
			if (!set.components.contains(face)) { set.components << face; }
			return;
		}
	}
	LevelMapComponentSet set;
	set.object = {LevelMapSelectionKind::QuakeBrush, brushId};
	set.kind = LevelBrushComponent::Face;
	set.components = {face};
	*sets << set;
}

// NetRadiant Custom's best plane: the face the ray meets first, or else the
// face behind the edge nearest the ray.
struct BestPlane {
	bool valid = false;
	MapPlane plane;
	LevelMapVec3 anchor;
};

bool insidePolygon(const QVector<LevelMapVec3>& points, const LevelMapVec3& normal, const LevelMapVec3& p)
{
	// Inside a convex polygon the point is on the same side of every edge,
	// whichever way the polygon winds.
	bool before = false, after = false;
	for (qsizetype i = 0; i < points.size(); ++i) {
		const auto& a = points[i];
		const auto& b = points[(i + 1) % points.size()];
		const double side = dot(cross(sub(b, a), sub(p, a)), normal);
		before |= side < -1e-6;
		after |= side > 1e-6;
	}
	return points.size() >= 3 && !(before && after);
}

BestPlane bestPlane(const QVector<Target>& brushes, const LevelPlaneDragProbe& probe)
{
	BestPlane best;
	double nearest = std::numeric_limits<double>::infinity();
	for (const auto& target : brushes) {
		for (const auto& face : target.topology.geometry.faces) {
			const LevelMapVec3 n = normalOf(face.plane);
			const double across = dot(n, probe.direction);
			// A 2D view cannot drag a plane along its own line of sight, so
			// it looks past faces it sees flat.
			if (std::abs(across) < 1e-9 || (!probe.camera && std::abs(dot(n, probe.forward)) > 0.999)) { continue; }
			const double t = (face.plane.distance - dot(n, probe.origin)) / across;
			if ((probe.camera && t <= 0) || t >= nearest) { continue; }
			const LevelMapVec3 hit = add(probe.origin, scaled(probe.direction, t));
			if (!insidePolygon(face.points, n, hit)) { continue; }
			nearest = t;
			best = {true, face.plane, hit};
		}
	}
	if (best.valid) { return best; }
	double closest = std::numeric_limits<double>::infinity();
	for (const auto& target : brushes) {
		const auto& topology = target.topology;
		QHash<quint64, QVector<int>> adjacent;
		for (int f = 0; f < topology.faces.size(); ++f) {
			const auto& ids = topology.faces[f];
			for (qsizetype i = 0; i < ids.size(); ++i) {
				const int a = ids[i], b = ids[(i + 1) % ids.size()];
				adjacent[(quint64(std::min(a, b)) << 32) | quint64(std::max(a, b))] << f;
			}
		}
		for (auto it = adjacent.cbegin(); it != adjacent.cend(); ++it) {
			if (it.value().size() != 2) { continue; }
			const LevelMapVec3 a = topology.vertices[int(it.key() >> 32)];
			const LevelMapVec3 b = topology.vertices[int(it.key() & 0xffffffffu)];
			// The closest points of the ray's line and the edge.
			const LevelMapVec3 u = probe.direction, v = sub(b, a), w = sub(probe.origin, a);
			const double uu = dot(u, u), uv = dot(u, v), vv = dot(v, v), uw = dot(u, w), vw = dot(v, w);
			const double denominator = uu * vv - uv * uv;
			double s = 0, t = 0;
			if (vv < 1e-9) { continue; }
			if (std::abs(denominator) < 1e-12) {
				t = std::clamp(vw / vv, 0.0, 1.0);
				s = (dot(u, sub(add(a, scaled(v, t)), probe.origin))) / uu;
			} else {
				t = std::clamp((uu * vw - uv * uw) / denominator, 0.0, 1.0);
				s = dot(u, sub(add(a, scaled(v, t)), probe.origin)) / uu;
			}
			const LevelMapVec3 onEdge = add(a, scaled(v, t));
			const LevelMapVec3 onRay = add(probe.origin, scaled(u, s));
			double distance = length(sub(onEdge, onRay));
			if (probe.camera) {
				if (s <= 1) { continue; }
				distance /= s;
			} else {
				// An edge seen end-on has no length on the screen to aim at.
				const LevelMapVec3 across = sub(v, scaled(probe.forward, dot(v, probe.forward)));
				if (dot(across, across) <= 1e-3) { continue; }
			}
			if (distance >= closest - 1e-9) { continue; }
			const auto& faces = topology.geometry.faces;
			const MapPlane& first = faces[it.value()[0]].plane;
			const MapPlane& second = faces[it.value()[1]].plane;
			MapPlane chosen;
			bool found = false;
			if (probe.camera) {
				// The face behind the silhouette: the one the viewer is behind.
				for (const MapPlane* plane : {&first, &second}) {
					if (dot(normalOf(*plane), probe.origin) - plane->distance <= 0) { chosen = *plane; found = true; break; }
				}
			} else {
				chosen = std::abs(dot(normalOf(first), probe.forward)) < std::abs(dot(normalOf(second), probe.forward)) ? first : second;
				found = true;
			}
			if (!found) { continue; }
			closest = distance;
			best = {true, chosen, onEdge};
		}
	}
	return best;
}

QString noSelectionReason(const LevelMapDocument& document)
{
	if (!quakeFamily(document)) { return Text::tr("Plane drags work on brushes in Quake-family maps."); }
	return Text::tr("Select brushes, or brush entities, to drag their planes.");
}

LevelMapVec3 roundedVector(const LevelMapVec3& v)
{
	// Keeps -0 out of reports and requests.
	return {v.x == 0 ? 0 : v.x, v.y == 0 ? 0 : v.y, v.z == 0 ? 0 : v.z, true};
}

bool appendEdges(const LevelMapBrush& brush, QVector<QPair<LevelMapVec3, LevelMapVec3>>* edges)
{
	LevelBrushTopology topology;
	if (!levelBrushTopology(brush, &topology)) { return false; }
	for (const auto& edge : topology.edges) { edges->append({topology.vertices[edge[0]], topology.vertices[edge[1]]}); }
	return true;
}
} // namespace

QString levelPlaneDragKindId(LevelPlaneDragKind kind)
{
	switch (kind) {
	case LevelPlaneDragKind::Plane: return QStringLiteral("plane");
	case LevelPlaneDragKind::Vertices: return QStringLiteral("vertices");
	case LevelPlaneDragKind::Extrude: return QStringLiteral("extrude");
	case LevelPlaneDragKind::FacingPlanes: break;
	}
	return QStringLiteral("facing-planes");
}

bool levelPlaneDragKindFromId(const QString& id, LevelPlaneDragKind* kind)
{
	for (const auto candidate : {LevelPlaneDragKind::FacingPlanes, LevelPlaneDragKind::Plane, LevelPlaneDragKind::Vertices, LevelPlaneDragKind::Extrude}) {
		if (id.trimmed().compare(levelPlaneDragKindId(candidate), Qt::CaseInsensitive) == 0) {
			if (kind) { *kind = candidate; }
			return true;
		}
	}
	return false;
}

QVector<int> levelPlaneDragBrushIds(const LevelMapDocument& document, const QVector<LevelMapSelectionRef>& selection)
{
	QVector<int> ids;
	if (!quakeFamily(document)) { return ids; }
	QSet<int> brushes, entities;
	for (const auto& ref : selection) {
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) { brushes.insert(ref.objectId); }
		else if (ref.kind == LevelMapSelectionKind::Entity) { entities.insert(ref.objectId); }
	}
	const QSet<QString> hidden = levelMapHiddenObjects(document);
	for (const auto& brush : document.brushes) {
		if (!brushes.contains(brush.id) && !entities.contains(brush.entityId)) { continue; }
		if (hidden.contains(levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush, brush.id}))) { continue; }
		ids << brush.id;
		if (ids.size() >= kMaximumBrushes) { break; }
	}
	return ids;
}

LevelPlaneDragPick pickLevelPlaneDrag(const LevelMapDocument& document, const QVector<LevelMapSelectionRef>& selection,
	LevelPlaneDragKind kind, const LevelPlaneDragProbe& requested)
{
	LevelPlaneDragPick pick;
	pick.kind = kind;
	LevelPlaneDragProbe probe = requested;
	probe.direction = normalised(probe.direction);
	probe.forward = finite(probe.forward) ? normalised(probe.forward) : probe.direction;
	if (!finite(probe.origin) || !finite(probe.direction) || !finite(probe.forward)) {
		pick.reason = Text::tr("The press has no position or direction to pick planes from.");
		return pick;
	}
	const QVector<Target> brushes = targets(document, selection);
	if (brushes.isEmpty()) {
		pick.reason = noSelectionReason(document);
		return pick;
	}
	QSet<int> involved;
	if (kind == LevelPlaneDragKind::FacingPlanes) {
		pick.anchor = probe.origin;
		QVector<MapPlane> picked;
		for (const auto& target : brushes) {
			const auto& faces = target.topology.geometry.faces;
			QVector<int> best;
			double bestDot = 1;
			for (int f = 0; f < faces.size(); ++f) {
				const auto& face = faces[f];
				const LevelMapVec3 n = normalOf(face.plane);
				const LevelMapVec3 q = rayPointAtDepth(probe, centroid(face.points));
				// The press must lie in front of the plane, by more than a hair so
				// faces seen almost flat are not taken.
				if (dot(n, q) - face.plane.distance < 0.005) { continue; }
				const double seen = std::abs(dot(n, probe.forward));
				const double difference = bestDot - seen;
				if (difference > 0.03) {
					bestDot = seen;
					best = {f};
				} else if (std::abs(difference) <= 0.03) {
					best << f;
				}
			}
			if (probe.camera && best.size() > 1) { best.resize(1); }
			for (const int f : std::as_const(best)) {
				addFace(&pick.sets, target.brush.id, f);
				picked << faces[f].plane;
				involved.insert(target.brush.id);
			}
		}
		// The planes back to back with a picked one move with it, so touching
		// brushes stay touching.
		for (const auto& target : brushes) {
			const auto& faces = target.topology.geometry.faces;
			for (int f = 0; f < faces.size(); ++f) {
				for (const auto& plane : std::as_const(picked)) {
					if (flippedPlane(faces[f].plane, plane)) {
						addFace(&pick.sets, target.brush.id, f);
						involved.insert(target.brush.id);
						break;
					}
				}
			}
		}
		pick.planes = static_cast<int>(picked.size());
		if (pick.sets.isEmpty()) {
			pick.reason = Text::tr("No selected brush has a plane facing the press point. Press beside the selection, outside its brushes.");
		}
	} else {
		const BestPlane best = bestPlane(brushes, probe);
		if (!best.valid) {
			pick.reason = Text::tr("No plane of the selected brushes lies under the pointer.");
			return pick;
		}
		pick.normal = normalOf(best.plane);
		pick.anchor = best.anchor;
		pick.planes = 1;
		if (kind == LevelPlaneDragKind::Vertices) {
			// The selected brushes' vertices under the pointer first, every one
			// this view stacks there; otherwise the corners of the faces on the
			// plane under it.
			double nearest = probe.tolerance;
			const auto across = [&probe](const LevelMapVec3& p) {
				const LevelMapVec3 offset = sub(p, probe.origin);
				return length(sub(offset, scaled(probe.direction, dot(offset, probe.direction))));
			};
			if (!probe.camera && probe.tolerance > 0) {
				for (const auto& target : brushes) {
					for (const auto& vertex : target.topology.vertices) { nearest = std::min(nearest, across(vertex)); }
				}
			}
			if (!probe.camera && nearest < probe.tolerance) {
				for (const auto& target : brushes) {
					LevelMapComponentSet set {{LevelMapSelectionKind::QuakeBrush, target.brush.id}, LevelBrushComponent::Vertex, {}};
					for (int v = 0; v < target.topology.vertices.size(); ++v) {
						if (across(target.topology.vertices[v]) <= nearest + 1e-3) {
							set.components << v;
							pick.corners << target.topology.vertices[v];
						}
					}
					if (!set.components.isEmpty()) { pick.sets << set; involved.insert(target.brush.id); }
				}
				pick.planes = 0;
			} else {
				for (const auto& target : brushes) {
					LevelMapComponentSet set {{LevelMapSelectionKind::QuakeBrush, target.brush.id}, LevelBrushComponent::Vertex, {}};
					const auto& faces = target.topology.geometry.faces;
					for (int f = 0; f < faces.size(); ++f) {
						if (!samePlane(faces[f].plane, best.plane) && !flippedPlane(faces[f].plane, best.plane)) { continue; }
						for (const int v : target.topology.faces[f]) {
							if (!set.components.contains(v)) { set.components << v; pick.corners << target.topology.vertices[v]; }
						}
					}
					if (!set.components.isEmpty()) { pick.sets << set; involved.insert(target.brush.id); }
				}
			}
		} else {
			for (const auto& target : brushes) {
				const auto& faces = target.topology.geometry.faces;
				for (int f = 0; f < faces.size(); ++f) {
					const bool same = samePlane(faces[f].plane, best.plane);
					// The camera's Alt drag takes back-to-back planes too; an
					// extrusion pulls only faces turned the same way.
					if (!same && !(kind == LevelPlaneDragKind::Plane && flippedPlane(faces[f].plane, best.plane))) { continue; }
					addFace(&pick.sets, target.brush.id, f);
					involved.insert(target.brush.id);
					for (const auto& corner : faces[f].points) { pick.corners << corner; }
				}
			}
		}
		if (pick.sets.isEmpty()) {
			pick.reason = Text::tr("No plane of the selected brushes lies under the pointer.");
		}
	}
	for (const auto& target : brushes) {
		if (involved.contains(target.brush.id)) { pick.brushes << target.brush; }
	}
	return pick;
}

bool moveLevelBrushPlanes(const LevelMapBrush& brush, const QVector<int>& faces, const LevelMapVec3& translation, double grid,
	LevelMapBrush* moved, LevelMapVec3* applied, QString* error)
{
	if (error) { error->clear(); }
	if (applied) { *applied = {0, 0, 0, true}; }
	if (!moved || !finite(translation) || !std::isfinite(grid) || grid < 0 || faces.isEmpty()) {
		if (error) { *error = Text::tr("A plane drag needs faces and a finite translation."); }
		return false;
	}
	for (const int face : faces) {
		if (face < 0 || face >= brush.faces.size()) {
			if (error) { *error = Text::tr("A dragged face is missing from brush %1.").arg(brush.id); }
			return false;
		}
	}
	LevelBrushTopology before;
	if (!levelBrushTopology(brush, &before, error)) { return false; }
	const auto attempt = [&](const LevelMapVec3& t, LevelMapBrush* out) {
		LevelMapBrush copy = brush;
		for (const int face : faces) {
			auto& target = copy.faces[face];
			for (auto* p : {&target.p0, &target.p1, &target.p2}) { *p = add(*p, t); }
			if (target.explicitPlane) { target.planeDistance -= dot(target.planeNormal, t); }
		}
		LevelBrushTopology topology;
		if (!levelBrushTopology(copy, &topology)) { return false; }
		copy.mins = topology.geometry.mins;
		copy.maxs = topology.geometry.maxs;
		copy.boundsSolved = true;
		copy.faceCount = static_cast<int>(copy.faces.size());
		copy.geometryDirty = true;
		*out = std::move(copy);
		return true;
	};
	if (attempt(translation, moved)) {
		if (applied) { *applied = roundedVector(translation); }
		return true;
	}
	// NetRadiant Custom's search: each axis in turn takes the furthest step
	// that still leaves a whole brush, the axes before it kept.
	LevelMapVec3 kept {0, 0, 0, true};
	LevelMapBrush candidate;
	for (int axis = 0; axis < 3; ++axis) {
		const double wanted = coordinate(translation, axis);
		if (wanted == 0) { continue; }
		const double step = grid > 0 ? grid : std::abs(wanted) / 64;
		int high = static_cast<int>(std::floor(std::abs(wanted) / step + 0.5)), low = 0;
		while (high > low) {
			const int middle = low + (high - low + 1) / 2;
			LevelMapVec3 trial = kept;
			setCoordinate(&trial, axis, std::copysign(middle * step, wanted));
			if (attempt(trial, &candidate)) { low = middle; } else { high = middle - 1; }
		}
		setCoordinate(&kept, axis, low == 0 ? 0.0 : std::copysign(low * step, wanted));
	}
	if (!attempt(kept, moved)) { *moved = brush; kept = {0, 0, 0, true}; }
	if (applied) { *applied = roundedVector(kept); }
	return true;
}

bool previewLevelPlaneDrag(const QVector<LevelMapBrush>& brushes, const LevelPlaneDragRequest& request,
	QVector<QPair<LevelMapVec3, LevelMapVec3>>* edges, LevelPlaneDragReport* report, QString* error)
{
	if (error) { error->clear(); }
	LevelPlaneDragReport made;
	QVector<QPair<LevelMapVec3, LevelMapVec3>> lines;
	for (const auto& set : request.sets) {
		const auto found = std::find_if(brushes.cbegin(), brushes.cend(), [&](const LevelMapBrush& brush) { return brush.id == set.object.objectId; });
		if (found == brushes.cend()) {
			if (error) { *error = Text::tr("A dragged brush is missing."); }
			return false;
		}
		QString problem;
		switch (request.kind) {
		case LevelPlaneDragKind::FacingPlanes:
		case LevelPlaneDragKind::Plane: {
			LevelMapBrush moved;
			LevelMapVec3 applied;
			if (!moveLevelBrushPlanes(*found, set.components, request.translation, request.grid, &moved, &applied, &problem)) {
				if (error) { *error = problem; }
				return false;
			}
			if (applied.x != request.translation.x || applied.y != request.translation.y || applied.z != request.translation.z) { ++made.clampedBrushes; }
			made.planes += static_cast<int>(set.components.size());
			appendEdges(moved, &lines);
			break;
		}
		case LevelPlaneDragKind::Vertices: {
			LevelMapBrush moved = *found;
			LevelBrushEditReport edit;
			if (!moveLevelBrushComponents(&moved, LevelBrushComponent::Vertex, set.components, request.translation, 0, true, &edit, &problem)) {
				if (error) { *error = Text::tr("Brush %1: %2").arg(found->id).arg(problem); }
				return false;
			}
			made.vertices += static_cast<int>(set.components.size());
			made.collapsedVertices += edit.collapsedVertices;
			appendEdges(moved, &lines);
			break;
		}
		case LevelPlaneDragKind::Extrude: {
			if (set.components.size() != 1) {
				if (error) { *error = Text::tr("An extrusion takes one face per brush."); }
				return false;
			}
			LevelBrushExtrusion extrusion;
			LevelBrushTopology topology;
			if (!levelBrushTopology(*found, &topology, &problem)) {
				if (error) { *error = problem; }
				return false;
			}
			const int face = set.components[0];
			const double along = face >= 0 && face < topology.geometry.faces.size() && finite(request.normal)
				? dot(normalOf(topology.geometry.faces[face].plane), request.normal) : 1.0;
			if (!prepareLevelBrushExtrusion(*found, face, request.distance * (along < 0 ? -1 : 1), true, false, &extrusion, &problem)) {
				if (error) { *error = Text::tr("Brush %1: %2").arg(found->id).arg(problem); }
				return false;
			}
			made.planes += 1;
			made.createdBrushes += std::max(0, static_cast<int>(extrusion.brushes.size()) - 1);
			for (const auto& piece : std::as_const(extrusion.brushes)) { appendEdges(piece, &lines); }
			break;
		}
		}
		++made.brushes;
		made.brushIds << found->id;
	}
	if (edges) { *edges = std::move(lines); }
	if (report) { *report = std::move(made); }
	return true;
}

bool applyLevelPlaneDrag(LevelMapDocument* document, const LevelPlaneDragRequest& request, LevelPlaneDragReport* report, QString* error)
{
	if (report) { *report = {}; }
	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) {
		if (error) { *error = message; }
		return false;
	};
	if (!document || !quakeFamily(*document)) { return fail(Text::tr("Plane drags work on brushes in Quake-family maps.")); }
	if (request.sets.isEmpty() || request.sets.size() > kMaximumBrushes) {
		return fail(Text::tr("Pick the planes of 1 to 256 brushes to drag."));
	}
	if (!finite(request.translation) || !std::isfinite(request.distance) || std::abs(request.distance) > 32768 || !std::isfinite(request.grid)
		|| request.grid < 0 || request.grid > 32768) {
		return fail(Text::tr("A plane drag needs a finite translation, distance and grid."));
	}
	// The sets must name what the selection moves, all of it editable here.
	QSet<int> allowed;
	for (const int id : levelPlaneDragBrushIds(*document, document->selection)) { allowed.insert(id); }
	const QSet<QString> hidden = levelMapHiddenObjects(*document);
	const QSet<QString> locked = levelSceneLockedObjects(*document);
	const LevelSceneSelectionContext context = levelSceneSelectionContext(*document);
	QSet<int> seen;
	QMap<int, const LevelMapBrush*> sources;
	for (const auto& set : request.sets) {
		const QString id = levelMapSelectionRefId(set.object);
		if (set.object.kind != LevelMapSelectionKind::QuakeBrush || set.components.isEmpty() || seen.contains(set.object.objectId)
			|| set.kind != (request.kind == LevelPlaneDragKind::Vertices ? LevelBrushComponent::Vertex : LevelBrushComponent::Face)) {
			return fail(request.kind == LevelPlaneDragKind::Vertices ? Text::tr("Each dragged brush needs its own set of vertices.")
																	 : Text::tr("Each dragged brush needs its own set of faces."));
		}
		seen.insert(set.object.objectId);
		const auto found = std::find_if(document->brushes.cbegin(), document->brushes.cend(),
			[&](const LevelMapBrush& brush) { return brush.id == set.object.objectId; });
		if (found == document->brushes.cend()) { return fail(Text::tr("Brush %1 no longer exists.").arg(set.object.objectId)); }
		if (hidden.contains(id)) { return fail(Text::tr("Brush %1 is hidden. Show it before dragging its planes.").arg(found->id)); }
		if (!allowed.contains(found->id)) {
			return fail(Text::tr("Brush %1 is not selected. Plane drags move the selected brushes.").arg(found->id));
		}
		const QString entity = levelMapSelectionRefId({LevelMapSelectionKind::Entity, found->entityId});
		if (locked.contains(id)) {
			return fail(Text::tr("Brush %1 is locked. Unlock its layer or group first.").arg(found->id));
		}
		if (!context.openGroup.isEmpty() && !context.allowedObjects.contains(id) && !context.allowedObjects.contains(entity)) {
			return fail(Text::tr("Brush %1 lies outside the open group.").arg(found->id));
		}
		sources.insert(found->id, &*found);
	}
	LevelPlaneDragReport made;
	switch (request.kind) {
	case LevelPlaneDragKind::FacingPlanes:
	case LevelPlaneDragKind::Plane: {
		QMap<int, LevelMapBrush> moved;
		for (const auto& set : request.sets) {
			LevelMapBrush result;
			LevelMapVec3 applied;
			QString problem;
			if (!moveLevelBrushPlanes(*sources.value(set.object.objectId), set.components, request.translation, request.grid, &result, &applied, &problem)) {
				return fail(Text::tr("Brush %1: %2").arg(set.object.objectId).arg(problem));
			}
			if (applied.x != request.translation.x || applied.y != request.translation.y || applied.z != request.translation.z) { ++made.clampedBrushes; }
			if (applied.x == 0 && applied.y == 0 && applied.z == 0) { continue; }
			moved.insert(set.object.objectId, result);
			made.planes += static_cast<int>(set.components.size());
			made.brushIds << set.object.objectId;
		}
		if (moved.isEmpty()) {
			return fail(Text::tr("The planes cannot move that way without a face of their brush vanishing."));
		}
		made.brushes = static_cast<int>(moved.size());
		const QString description = Text::tr("Drag %n brush plane(s)", nullptr, made.planes);
		if (!replaceLevelMapComponentGeometry(document, moved, {}, description, error)) { return false; }
		break;
	}
	case LevelPlaneDragKind::Vertices: {
		if (request.translation.x == 0 && request.translation.y == 0 && request.translation.z == 0) {
			return fail(Text::tr("The vertices did not move."));
		}
		LevelComponentTransformRequest move;
		move.delta = request.translation;
		move.allowCollapse = true;
		LevelMapComponentReport components;
		if (!transformLevelMapComponents(document, request.sets, move, &components, error)) { return false; }
		made.brushes = components.brushes;
		made.collapsedVertices = components.collapsedVertices;
		for (const auto& set : request.sets) {
			made.vertices += static_cast<int>(set.components.size());
			made.brushIds << set.object.objectId;
		}
		break;
	}
	case LevelPlaneDragKind::Extrude: {
		if (request.distance == 0) { return fail(Text::tr("The faces did not move.")); }
		// extrudeLevelMapFaces() measures along the first face's normal; the
		// request measures along the picked plane's.
		LevelBrushTopology topology;
		const auto& first = request.sets.first();
		double sign = 1;
		if (finite(request.normal) && levelBrushTopology(*sources.value(first.object.objectId), &topology) && first.components.size() == 1
			&& first.components[0] >= 0 && first.components[0] < topology.geometry.faces.size()) {
			sign = dot(normalOf(topology.geometry.faces[first.components[0]].plane), request.normal) < 0 ? -1 : 1;
		}
		LevelMapExtrusionReport extrusion;
		if (!extrudeLevelMapFaces(document, request.sets, request.distance * sign, true, request.textureLock, &extrusion, error)) { return false; }
		made.brushes = extrusion.changedBrushes;
		made.createdBrushes = extrusion.createdBrushes;
		made.planes = static_cast<int>(request.sets.size());
		made.brushIds = extrusion.brushIds;
		break;
	}
	}
	if (report) { *report = std::move(made); }
	return true;
}

LevelCloneChain levelCloneChain(const LevelMapVec3& mins, const LevelMapVec3& maxs, const LevelMapVec3& travel, double grid, int maximum)
{
	LevelCloneChain chain;
	if (!finite(mins) || !finite(maxs) || !finite(travel) || !std::isfinite(grid) || maximum <= 0) { return chain; }
	int axis = 0;
	for (int candidate = 1; candidate < 3; ++candidate) {
		if (std::abs(coordinate(travel, candidate)) > std::abs(coordinate(travel, axis))) { axis = candidate; }
	}
	const double along = coordinate(travel, axis);
	if (along == 0) { return chain; }
	double size = coordinate(maxs, axis) - coordinate(mins, axis);
	if (!(size > 1e-9)) { size = grid > 0 ? grid : 1; }
	const int copies = std::min(maximum, static_cast<int>(std::floor(std::abs(along) / size + 0.5)));
	if (copies <= 0) { return chain; }
	chain.axis = axis;
	chain.copies = copies;
	setCoordinate(&chain.step, axis, std::copysign(size, along));
	return chain;
}

void recordLevelCloneChainRepeat(LevelMapDocument* document, const LevelMapVec3& step, int copies)
{
	if (!document || copies < 2 || !finite(step)) { return; }
	LevelRepeatAction action;
	action.kind = LevelRepeatAction::Kind::Duplicate;
	action.matrix[3] = step.x;
	action.matrix[7] = step.y;
	action.matrix[11] = step.z;
	// The array selects its copies copy by copy; keep the last one, so a
	// repeat continues the chain instead of copying the whole group.
	const QVector<LevelMapSelectionRef> selection = document->selection;
	if (!selection.isEmpty() && selection.size() % copies == 0) {
		const qsizetype each = selection.size() / copies;
		setLevelMapSelection(document, selection.mid(selection.size() - each));
	}
	const quint64 before = document->repeat.revision;
	detail::LevelRepeatCaptureScope scope(document);
	scope.commit(action);
	// The first step went through the ordinary capture (which clears the old
	// commands once the selection changed); the rest follow it. Duplicates
	// leave the repeatable transforms as they are.
	if (document->repeat.revision == before) { return; }
	for (int copy = 1; copy < copies; ++copy) { document->repeat.actions << action; }
}

} // namespace vibestudio
