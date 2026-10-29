#include "core/level_object_manipulator.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_visibility.h"
#include "core/map_geometry.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

// Workflow reference: NetRadiant Custom radiant/tools.cpp MouseTranslate,
// radiant/selection_mtable_translate.h TranslateAxis, TranslateFree and
// aabb_snap_translation, and radiant/selection.cpp TransformOriginTranslate and
// RadiantSelectionSystem::setCustomTransformOrigin at 449778b3
// (GPL-2.0-or-later). Original Qt implementation; see docs/CREDITS.md.
namespace vibestudio {
namespace {
struct Text {
	Q_DECLARE_TR_FUNCTIONS(LevelObjectManipulator)
};

double coordinate(const LevelMapVec3& p, int axis) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; }
void setCoordinate(LevelMapVec3* p, int axis, double value)
{
	if (axis == 0) { p->x = value; } else if (axis == 1) { p->y = value; } else { p->z = value; }
}
bool finite(const LevelMapVec3& p) { return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
bool quakeFamily(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
}
} // namespace

QString levelObjectToolId(LevelObjectTool tool)
{
	switch (tool) {
	case LevelObjectTool::Translate: return QStringLiteral("translate");
	case LevelObjectTool::Rotate: return QStringLiteral("rotate");
	case LevelObjectTool::Scale: return QStringLiteral("scale");
	case LevelObjectTool::Transform: return QStringLiteral("transform");
	case LevelObjectTool::Select: break;
	}
	return QStringLiteral("select");
}

LevelObjectTool levelObjectToolFromId(const QString& id)
{
	for (const auto tool : {LevelObjectTool::Translate, LevelObjectTool::Rotate, LevelObjectTool::Scale, LevelObjectTool::Transform}) {
		if (id.compare(levelObjectToolId(tool), Qt::CaseInsensitive) == 0) { return tool; }
	}
	return LevelObjectTool::Select;
}

LevelObjectManipulatorTarget levelObjectManipulatorTarget(const LevelMapDocument& document)
{
	LevelObjectManipulatorTarget target;
	if (!quakeFamily(document)) {
		target.reason = Text::tr("The object manipulators work on Quake-family maps.");
		return target;
	}
	if (document.selection.isEmpty()) {
		target.reason = Text::tr("Select map objects to manipulate them.");
		return target;
	}
	target.objects = document.selection;
	// A selected entity moves its own brushes and patches, so they count too.
	QSet<int> entities;
	for (const auto& ref : document.selection) {
		if (ref.kind == LevelMapSelectionKind::Entity) { entities.insert(ref.objectId); }
	}
	QSet<QString> moved;
	for (const auto& ref : document.selection) { moved.insert(levelMapSelectionRefId(ref)); }
	for (const auto& brush : document.brushes) {
		if (entities.contains(brush.entityId)) { moved.insert(levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush, brush.id})); }
	}
	for (const auto& patch : document.patches) {
		if (entities.contains(patch.entityId)) { moved.insert(levelMapSelectionRefId({LevelMapSelectionKind::QuakePatch, patch.id})); }
	}
	const QSet<QString> hidden = levelMapHiddenObjects(document);
	const QSet<QString> locked = levelSceneLockedObjects(document);
	const LevelSceneSelectionContext context = levelSceneSelectionContext(document);
	for (const QString& object : std::as_const(moved)) {
		if (hidden.contains(object)) { ++target.hidden; }
		if (locked.contains(object)) { ++target.locked; }
	}
	if (!context.openGroup.isEmpty()) {
		for (const auto& ref : document.selection) {
			if (!context.allowedObjects.contains(levelMapSelectionRefId(ref))) { ++target.outsideGroup; }
		}
	}
	const bool bounded = levelMapSelectionBounds(document, &target.mins, &target.maxs) && finite(target.mins) && finite(target.maxs);
	if (bounded) {
		target.centre = {(target.mins.x + target.maxs.x) / 2, (target.mins.y + target.maxs.y) / 2, (target.mins.z + target.maxs.z) / 2, true};
	}
	if (target.hidden > 0) {
		target.reason = Text::tr("%n selected object(s) are hidden. Show them, or leave them out of the selection.", nullptr, target.hidden);
	} else if (target.locked > 0) {
		target.reason = Text::tr("%n selected object(s) are locked. Unlock their layer or group first.", nullptr, target.locked);
	} else if (target.outsideGroup > 0) {
		target.reason = Text::tr("%n selected object(s) lie outside the open group.", nullptr, target.outsideGroup);
	} else if (!bounded) {
		target.reason = Text::tr("The selection has no position to move.");
	} else {
		target.available = true;
	}
	return target;
}

LevelMapVec3 levelObjectManipulatorPivot(const LevelObjectManipulatorTarget& target, const LevelMapVec3& custom)
{
	if (finite(custom)) { return custom; }
	return target.centre;
}

LevelMapVec3 snapLevelObjectPivot(const LevelMapVec3& wanted, const LevelMapVec3& start, int axes, const LevelMapVec3& mins,
	const LevelMapVec3& maxs, double grid)
{
	if (!finite(wanted) || !finite(start) || !std::isfinite(grid)) { return start; }
	LevelMapVec3 result = start;
	const bool bounded = finite(mins) && finite(maxs);
	for (int axis = 0; axis < 3; ++axis) {
		if ((axes & (1 << axis)) == 0) { continue; }
		const double value = coordinate(wanted, axis);
		// Centre first, then the far faces, then the grid: the first of equally
		// near candidates wins.
		std::array<double, 4> candidates {};
		int count = 0;
		if (bounded) {
			const double low = coordinate(mins, axis), high = coordinate(maxs, axis);
			candidates[count++] = (low + high) / 2;
			candidates[count++] = high;
			candidates[count++] = low;
		}
		if (grid > 0) { candidates[count++] = snapLevelMapCoordinate(value, grid); }
		double best = value, nearest = std::numeric_limits<double>::infinity();
		for (int i = 0; i < count; ++i) {
			const double distance = std::abs(candidates[static_cast<size_t>(i)] - value);
			if (distance < nearest) { nearest = distance; best = candidates[static_cast<size_t>(i)]; }
		}
		setCoordinate(&result, axis, best);
	}
	return result;
}

LevelMapVec3 snapLevelObjectTranslation(const LevelMapVec3& delta, const LevelMapVec3& mins, const LevelMapVec3& maxs, double grid,
	LevelObjectTranslateSnap snap)
{
	if (!finite(delta) || !std::isfinite(grid) || grid <= 0 || snap == LevelObjectTranslateSnap::None) { return delta; }
	if (snap == LevelObjectTranslateSnap::Grid) { return snapLevelMapDelta(delta, grid); }
	if (!finite(mins) || !finite(maxs)) { return delta; }
	LevelMapVec3 result = delta;
	for (int axis = 0; axis < 3; ++axis) {
		const double move = coordinate(delta, axis);
		if (std::abs(move) <= 1e-2) { continue; }
		const double high = coordinate(maxs, axis) + move, low = coordinate(mins, axis) + move;
		const double highSnap = snapLevelMapCoordinate(high, grid), lowSnap = snapLevelMapCoordinate(low, grid);
		// The nearer face lands on its grid line; the minimum on a tie.
		setCoordinate(&result, axis,
			std::abs(high - highSnap) < std::abs(low - lowSnap) ? highSnap - coordinate(maxs, axis) : lowSnap - coordinate(mins, axis));
	}
	return result;
}

LevelMapVec3 levelObjectDominantAxis(const LevelMapVec3& delta)
{
	if (!finite(delta)) { return delta; }
	int largest = 0;
	for (int axis = 1; axis < 3; ++axis) {
		if (std::abs(coordinate(delta, axis)) > std::abs(coordinate(delta, largest))) { largest = axis; }
	}
	LevelMapVec3 result {0, 0, 0, true};
	setCoordinate(&result, largest, coordinate(delta, largest));
	return result;
}

bool translateLevelMapObjects(LevelMapDocument* document, const LevelObjectTranslateRequest& request,
	LevelObjectTranslateReport* report, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) {
		if (error) { *error = message; }
		return false;
	};
	if (!document) { return fail(Text::tr("Missing map document.")); }
	if (!std::isfinite(request.grid) || request.grid < 0 || request.grid > 32768) {
		return fail(Text::tr("The grid must be from 0 (no snapping) to 32768 units."));
	}
	const LevelObjectManipulatorTarget target = levelObjectManipulatorTarget(*document);
	if (!target.available) { return fail(target.reason); }
	if (request.pivot.valid && !finite(request.pivot)) { return fail(Text::tr("The pivot must be three finite numbers.")); }
	const LevelMapVec3 pivot = levelObjectManipulatorPivot(target, request.pivot);
	LevelMapVec3 delta = request.delta;
	if (request.destination.valid) {
		if (!finite(request.destination)) { return fail(Text::tr("The destination must be three finite numbers.")); }
		delta = {request.destination.x - pivot.x, request.destination.y - pivot.y, request.destination.z - pivot.z, true};
	}
	if (!finite(delta)) { return fail(Text::tr("The translation must be three finite numbers.")); }
	delta = snapLevelObjectTranslation(delta, target.mins, target.maxs, request.grid, request.snap);
	if (delta.x == 0 && delta.y == 0 && delta.z == 0) {
		return fail(Text::tr("The translation is zero after snapping, so nothing moved."));
	}
	// moveLevelMapSelection() runs the scene guard and records one undo step;
	// a candidate keeps the live document untouched on refusal.
	LevelMapDocument candidate = *document;
	QString problem;
	if (!moveLevelMapSelection(&candidate, delta.x, delta.y, delta.z, request.textures, &problem)) { return fail(problem); }
	LevelObjectTranslateReport result;
	result.delta = delta;
	result.pivotBefore = pivot;
	result.pivotAfter = {pivot.x + delta.x, pivot.y + delta.y, pivot.z + delta.z, true};
	result.objects = static_cast<int>(target.objects.size());
	if (!levelMapSelectionBounds(candidate, &result.mins, &result.maxs)) { result.mins = result.maxs = {}; }
	*document = std::move(candidate);
	if (report) { *report = result; }
	return true;
}

// Workflow reference for the Rotate tool: NetRadiant Custom
// radiant/selection_mtable_rotate.h RotateAxis and RotateFree,
// radiant/selection_mtor_rotate.cpp RotateManipulatorImpl and libs/math
// matrix4_rotation_for_euler_xyz at 449778b3 (GPL-2.0-or-later), and
// TrenchBroom ui/RotateTool and RotateHandle at 4c756be1 (GPL-3.0-or-later).
// Original Qt implementation; see docs/CREDITS.md.
double snapLevelObjectAngle(double degrees, double step)
{
	if (!std::isfinite(degrees) || !std::isfinite(step) || step <= 0) { return degrees; }
	return std::round(degrees / step) * step;
}

std::array<LevelMapVec3, 3> levelObjectRotationColumns(const LevelMapVec3& axis, double degrees)
{
	std::array<LevelMapVec3, 3> columns {{{1, 0, 0, true}, {0, 1, 0, true}, {0, 0, 1, true}}};
	const double length = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
	if (!finite(axis) || !std::isfinite(degrees) || !std::isfinite(length) || length <= 1e-12) { return columns; }
	const double nx = axis.x / length, ny = axis.y / length, nz = axis.z / length;
	const double radians = std::remainder(degrees, 360.0) * 3.14159265358979323846 / 180.0;
	const double c = std::cos(radians), s = std::sin(radians), t = 1 - c;
	// Rodrigues: v cos + (n x v) sin + n (n . v)(1 - cos), for each basis vector.
	columns[0] = {c + nx * nx * t, nz * s + ny * nx * t, -ny * s + nz * nx * t, true};
	columns[1] = {-nz * s + nx * ny * t, c + ny * ny * t, nx * s + nz * ny * t, true};
	columns[2] = {ny * s + nx * nz * t, -nx * s + ny * nz * t, c + nz * nz * t, true};
	return columns;
}

std::array<double, 3> levelObjectEulerXyz(const std::array<LevelMapVec3, 3>& columns)
{
	// R = Rz(c) Ry(b) Rx(a); R[row][column] is coordinate `row` of column `column`.
	const double r00 = columns[0].x, r10 = columns[0].y, r20 = columns[0].z;
	const double r11 = columns[1].y, r21 = columns[1].z, r12 = columns[2].y, r22 = columns[2].z;
	constexpr double toDegrees = 180.0 / 3.14159265358979323846;
	const double b = std::asin(std::clamp(-r20, -1.0, 1.0));
	if (std::abs(r20) < 1 - 1e-12) {
		return {std::atan2(r21, r22) * toDegrees, b * toDegrees, std::atan2(r10, r00) * toDegrees};
	}
	// Gimbal lock: the X and Z turns share an axis; Z takes none of it.
	return {std::atan2(-r12, r11) * toDegrees, b * toDegrees, 0.0};
}

LevelMapVec3 levelObjectSpherePoint(const LevelMapVec3& origin, const LevelMapVec3& direction, const LevelMapVec3& centre, double radius)
{
	const double length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
	if (!finite(origin) || !finite(direction) || !finite(centre) || !std::isfinite(radius) || radius <= 0 || !std::isfinite(length) || length <= 1e-12) {
		return {};
	}
	const LevelMapVec3 d {direction.x / length, direction.y / length, direction.z / length, true};
	const double along = (centre.x - origin.x) * d.x + (centre.y - origin.y) * d.y + (centre.z - origin.z) * d.z;
	const LevelMapVec3 closest {origin.x + d.x * along - centre.x, origin.y + d.y * along - centre.y, origin.z + d.z * along - centre.z, true};
	const double distance = closest.x * closest.x + closest.y * closest.y + closest.z * closest.z;
	if (distance <= radius * radius) {
		// The near side: the line runs away from the viewer.
		const double back = std::sqrt(radius * radius - distance);
		return {(closest.x - d.x * back) / radius, (closest.y - d.y * back) / radius, (closest.z - d.z * back) / radius, true};
	}
	const double edge = std::sqrt(distance);
	return {closest.x / edge, closest.y / edge, closest.z / edge, true};
}

bool levelObjectArcball(const LevelMapVec3& from, const LevelMapVec3& to, LevelMapVec3* axis, double* degrees)
{
	if (!finite(from) || !finite(to) || !axis || !degrees) { return false; }
	const LevelMapVec3 cross {from.y * to.z - from.z * to.y, from.z * to.x - from.x * to.z, from.x * to.y - from.y * to.x, true};
	const double sine = std::sqrt(cross.x * cross.x + cross.y * cross.y + cross.z * cross.z);
	const double cosine = from.x * to.x + from.y * to.y + from.z * to.z;
	if (!std::isfinite(sine) || sine <= 1e-12) { return false; }
	*axis = {cross.x / sine, cross.y / sine, cross.z / sine, true};
	*degrees = std::atan2(sine, cosine) * 180.0 / 3.14159265358979323846;
	return true;
}

bool levelObjectRotation(const LevelObjectManipulatorTarget& target, const LevelObjectRotateRequest& request, LevelMapRotationRequest* rotation,
	LevelObjectRotateReport* report, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) {
		if (error) { *error = message; }
		return false;
	};
	if (!rotation) { return fail(Text::tr("Missing rotation request.")); }
	if (!target.available) { return fail(target.reason); }
	const double length = std::sqrt(request.axis.x * request.axis.x + request.axis.y * request.axis.y + request.axis.z * request.axis.z);
	if (!finite(request.axis) || !std::isfinite(length) || length <= 1e-9) {
		return fail(Text::tr("The rotation axis must be three finite numbers that are not all zero."));
	}
	if (!std::isfinite(request.angleStep) || request.angleStep < 0 || request.angleStep > 360) {
		return fail(Text::tr("The angle step must be from 0 (no snapping) to 360 degrees."));
	}
	if (!std::isfinite(request.degrees) || std::abs(request.degrees) > 360000) {
		return fail(Text::tr("The angle must be a finite number between -360000 and 360000 degrees."));
	}
	const double degrees = snapLevelObjectAngle(request.degrees, request.angleStep);
	if (std::abs(std::remainder(degrees, 360.0)) < 1e-9) {
		return fail(Text::tr("The turn is a whole number of turns after snapping, so nothing turned."));
	}
	if (request.pivot.valid && !finite(request.pivot)) { return fail(Text::tr("The pivot must be three finite numbers.")); }
	const LevelMapVec3 axis {request.axis.x / length, request.axis.y / length, request.axis.z / length, true};
	LevelObjectRotateReport result;
	result.axis = axis;
	result.degrees = degrees;
	result.pivot = levelObjectManipulatorPivot(target, request.pivot);
	result.objects = static_cast<int>(target.objects.size());
	LevelMapRotationRequest numeric;
	numeric.pivot = result.pivot;
	numeric.textureLock = request.textures.enabled;
	numeric.allowValve220 = request.textures.allowValve220;
	for (int candidate = 0; candidate < 3 && result.worldAxis < 0; ++candidate) {
		bool along = true;
		for (int other = 0; other < 3; ++other) {
			if (other != candidate && std::abs(coordinate(axis, other)) > 1e-9) { along = false; }
		}
		if (along) { result.worldAxis = candidate; }
	}
	if (result.worldAxis >= 0) {
		// One plain turn: exactly what Rotate Selection… does about that axis.
		numeric.axis = result.worldAxis;
		numeric.degrees = coordinate(axis, result.worldAxis) < 0 ? -degrees : degrees;
	} else {
		numeric.euler = true;
		result.eulerXyz = levelObjectEulerXyz(levelObjectRotationColumns(axis, degrees));
		numeric.eulerDegrees = result.eulerXyz;
	}
	*rotation = numeric;
	if (report) { *report = result; }
	return true;
}

bool rotateLevelMapObjects(LevelMapDocument* document, const LevelObjectRotateRequest& request, LevelObjectRotateReport* report, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) {
		if (error) { *error = message; }
		return false;
	};
	if (!document) { return fail(Text::tr("Missing map document.")); }
	LevelMapRotationRequest rotation;
	LevelObjectRotateReport result;
	QString problem;
	if (!levelObjectRotation(levelObjectManipulatorTarget(*document), request, &rotation, &result, &problem)) { return fail(problem); }
	// rotateLevelMapSelection() runs the scene guard and records one undo
	// step; a candidate keeps the live document untouched on refusal.
	LevelMapDocument candidate = *document;
	if (!rotateLevelMapSelection(&candidate, rotation, &problem)) { return fail(problem); }
	if (!levelMapSelectionBounds(candidate, &result.mins, &result.maxs)) { result.mins = result.maxs = {}; }
	*document = std::move(candidate);
	if (report) { *report = result; }
	return true;
}

// Workflow reference for the Scale tool: NetRadiant Custom
// radiant/selection_mtable_scale.h ScaleAxis and ScaleFree and
// radiant/selection_mtor_scale.cpp ScaleManipulatorImpl at 449778b3
// (GPL-2.0-or-later). Original Qt implementation; see docs/CREDITS.md.
std::array<double, 3> levelObjectScaleFactors(const LevelObjectScaleDrag& drag)
{
	std::array<double, 3> factors {1, 1, 1};
	if (drag.handle < 0 || drag.handle > 3 || !finite(drag.delta) || !finite(drag.mins) || !finite(drag.maxs) || !finite(drag.pivot)
		|| !std::isfinite(drag.grid) || !std::isfinite(drag.sizeGrid)) {
		return factors;
	}
	const double grid = drag.grid > 0 ? drag.grid : 0, sizeGrid = drag.sizeGrid > 0 ? drag.sizeGrid : 0;
	const auto factor = [&](int axis) {
		const double low = coordinate(drag.mins, axis), high = coordinate(drag.maxs, axis), pivot = coordinate(drag.pivot, axis);
		// The farther side from the pivot follows the pointer; a selection
		// with (almost) no extent here keeps its size.
		const double extent = std::max(high - pivot, pivot - low), size = high - low;
		double travel = coordinate(drag.delta, axis);
		if (grid > 0) { travel = snapLevelMapCoordinate(travel, grid); }
		if (extent <= 0.0625 || size <= 0 || travel == 0) { return 1.0; }
		double value = (extent + travel) / extent;
		// Ctrl: the scaled size lands on the size grid.
		if (sizeGrid > 0) { value = snapLevelMapCoordinate(value * size, sizeGrid) / size; }
		const double least = std::min(sizeGrid > 0 ? sizeGrid : grid > 0 ? grid : 1.0, size) / size;
		return std::isfinite(value) ? std::max(value, least) : 1.0;
	};
	if (drag.handle < 3) {
		factors[static_cast<size_t>(drag.handle)] = factor(drag.handle);
		if (drag.equal) { factors.fill(factors[static_cast<size_t>(drag.handle)]); }
		return factors;
	}
	for (int axis = 0; axis < 3; ++axis) { factors[static_cast<size_t>(axis)] = factor(axis); }
	if (drag.equal) {
		// The view axes scale alike, by whichever changes most.
		double best = 1;
		for (int axis = 0; axis < 3; ++axis) {
			const double value = factors[static_cast<size_t>(axis)];
			if (axis != drag.viewAxis && std::abs(std::log(value)) > std::abs(std::log(best))) { best = value; }
		}
		for (int axis = 0; axis < 3; ++axis) {
			if (axis != drag.viewAxis) { factors[static_cast<size_t>(axis)] = best; }
		}
	}
	return factors;
}

bool levelObjectScale(const LevelObjectManipulatorTarget& target, const LevelObjectScaleRequest& request, LevelMapScaleRequest* scale,
	LevelObjectScaleReport* report, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) {
		if (error) { *error = message; }
		return false;
	};
	if (!scale) { return fail(Text::tr("Missing scale request.")); }
	if (!target.available) { return fail(target.reason); }
	bool unchanged = true;
	for (const double value : request.factors) {
		if (!std::isfinite(value) || value <= 0 || value > 1024) {
			return fail(Text::tr("Scale factors must be above zero and at most 1024. Flip mirrors the selection."));
		}
		unchanged = unchanged && value == 1;
	}
	if (unchanged) {
		// Point entities alone have an origin but no size: say so rather than
		// leave the handle's refusal unexplained.
		const bool sized = finite(target.mins) && finite(target.maxs)
			&& (target.maxs.x - target.mins.x > 0.0625 || target.maxs.y - target.mins.y > 0.0625 || target.maxs.z - target.mins.z > 0.0625);
		return fail(sized ? Text::tr("The scale is 1 along every axis, so nothing changed.")
						  : Text::tr("The selection has no size to scale, such as point entities alone, so nothing changed. Move it instead."));
	}
	if (request.pivot.valid && !finite(request.pivot)) { return fail(Text::tr("The pivot must be three finite numbers.")); }
	LevelObjectScaleReport result;
	result.factors = request.factors;
	result.pivot = levelObjectManipulatorPivot(target, request.pivot);
	result.objects = static_cast<int>(target.objects.size());
	LevelMapScaleRequest numeric;
	numeric.factors = request.factors;
	numeric.pivot = result.pivot;
	numeric.textures = request.textures;
	*scale = numeric;
	if (report) { *report = result; }
	return true;
}

bool scaleLevelMapObjects(LevelMapDocument* document, const LevelObjectScaleRequest& request, LevelObjectScaleReport* report, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) {
		if (error) { *error = message; }
		return false;
	};
	if (!document) { return fail(Text::tr("Missing map document.")); }
	LevelMapScaleRequest scale;
	LevelObjectScaleReport result;
	QString problem;
	if (!levelObjectScale(levelObjectManipulatorTarget(*document), request, &scale, &result, &problem)) { return fail(problem); }
	// scaleLevelMapSelection() runs the scene guard and records one undo
	// step; a candidate keeps the live document untouched on refusal.
	LevelMapDocument candidate = *document;
	if (!scaleLevelMapSelection(&candidate, scale, &problem)) { return fail(problem); }
	if (!levelMapSelectionBounds(candidate, &result.mins, &result.maxs)) { result.mins = result.maxs = {}; }
	*document = std::move(candidate);
	if (report) { *report = result; }
	return true;
}

// Workflow reference for the Transform tool and the side shear: NetRadiant
// Custom radiant/selection_mtor_skew.cpp SkewManipulatorImpl, radiant/
// selection_mtable_skew.h SkewAxis and radiant/tools.cpp MouseTransform and
// MouseDragOrTransform at 449778b3 (GPL-2.0-or-later); TrenchBroom
// ui/ShearTool and ui/ShearToolController and vm shear_bbox_matrix at
// 4c756be1 (GPL-3.0-or-later). Original Qt implementation; see docs/CREDITS.md.
LevelMapVec3 levelObjectTransformPivot(const LevelMapVec3& mins, const LevelMapVec3& maxs, const std::array<int, 3>& sides)
{
	if (!finite(mins) || !finite(maxs)) { return {}; }
	LevelMapVec3 pivot {(mins.x + maxs.x) / 2, (mins.y + maxs.y) / 2, (mins.z + maxs.z) / 2, true};
	for (int axis = 0; axis < 3; ++axis) {
		const int side = sides[static_cast<size_t>(axis)];
		if (side > 0) { setCoordinate(&pivot, axis, coordinate(mins, axis)); }
		else if (side < 0) { setCoordinate(&pivot, axis, coordinate(maxs, axis)); }
	}
	return pivot;
}

std::array<double, 3> levelObjectSideScaleFactors(const LevelObjectSideScaleDrag& drag)
{
	std::array<double, 3> factors {1, 1, 1};
	if (!finite(drag.delta) || !finite(drag.mins) || !finite(drag.maxs) || !finite(drag.pivot) || !std::isfinite(drag.grid)
		|| !std::isfinite(drag.sizeGrid)) {
		return factors;
	}
	const double grid = drag.grid > 0 ? drag.grid : 0, sizeGrid = drag.sizeGrid > 0 ? drag.sizeGrid : 0;
	for (int axis = 0; axis < 3; ++axis) {
		const int side = drag.sides[static_cast<size_t>(axis)];
		if (side == 0) { continue; }
		const double low = coordinate(drag.mins, axis), high = coordinate(drag.maxs, axis), size = high - low;
		// The dragged side's distance from the pivot, signed, follows the pointer.
		const double reach = (side > 0 ? high : low) - coordinate(drag.pivot, axis);
		double travel = coordinate(drag.delta, axis);
		if (grid > 0) { travel = snapLevelMapCoordinate(travel, grid); }
		if (std::abs(reach) <= 0.0625 || size <= 0 || travel == 0) { continue; }
		double value = (reach + travel) / reach;
		if (sizeGrid > 0) { value = snapLevelMapCoordinate(value * size, sizeGrid) / size; }
		const double least = std::min(sizeGrid > 0 ? sizeGrid : grid > 0 ? grid : 1.0, size) / size;
		factors[static_cast<size_t>(axis)] = std::isfinite(value) ? std::max(value, least) : 1.0;
	}
	if (drag.equal) {
		// Proportions kept: every axis takes the factor that changes most.
		double best = 1;
		for (const double value : factors) {
			if (std::abs(std::log(value)) > std::abs(std::log(best))) { best = value; }
		}
		factors.fill(best);
	}
	return factors;
}

bool levelObjectShear(const LevelObjectManipulatorTarget& target, const LevelObjectShearRequest& request, LevelObjectShearReport* report,
	QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) {
		if (error) { *error = message; }
		return false;
	};
	if (!target.available) { return fail(target.reason); }
	if (request.axis < 0 || request.axis > 2 || (request.side != -1 && request.side != 1)) {
		return fail(Text::tr("Name the side that slides: x, y or z with + or -."));
	}
	if (!finite(request.offset)) { return fail(Text::tr("The slide must be three finite numbers.")); }
	if (request.pivot.valid && !finite(request.pivot)) { return fail(Text::tr("The pivot must be three finite numbers.")); }
	const int axis = request.axis;
	const double side = request.side > 0 ? coordinate(target.maxs, axis) : coordinate(target.mins, axis);
	const double anchor = request.pivot.valid ? coordinate(request.pivot, axis)
		: request.side > 0 ? coordinate(target.mins, axis) : coordinate(target.maxs, axis);
	const double reach = side - anchor;
	LevelObjectShearReport result;
	result.anchor = anchor;
	result.objects = static_cast<int>(target.objects.size());
	bool moved = false;
	for (int other = 0; other < 3; ++other) {
		if (other == axis) { continue; }
		const double slide = coordinate(request.offset, other);
		if (slide == 0) { continue; }
		moved = true;
		if (std::abs(reach) < 1e-6) {
			return fail(Text::tr("The side that slides lies on the plane that stays put, so it cannot slant the selection."));
		}
		const double factor = slide / reach;
		if (!std::isfinite(factor) || std::abs(factor) > 16) {
			return fail(Text::tr("The slide is more than 16 units for each unit across the box. Slant it in smaller steps."));
		}
		result.factors[static_cast<size_t>(other)] = factor;
		// Row `other`: other += factor * (axis - anchor).
		result.matrix[static_cast<size_t>(other * 4 + axis)] = factor;
		result.matrix[static_cast<size_t>(other * 4 + 3)] = -factor * anchor;
	}
	if (!moved) { return fail(Text::tr("The side did not slide, so nothing changed.")); }
	if (report) { *report = result; }
	return true;
}

bool shearLevelMapObjects(LevelMapDocument* document, const LevelObjectShearRequest& request, LevelObjectShearReport* report, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) {
		if (error) { *error = message; }
		return false;
	};
	if (!document) { return fail(Text::tr("Missing map document.")); }
	LevelObjectShearReport result;
	QString problem;
	if (!levelObjectShear(levelObjectManipulatorTarget(*document), request, &result, &problem)) { return fail(problem); }
	// The axis that slides, when only one does.
	int slanted = 0, slid = -1;
	for (int other = 0; other < 3; ++other) {
		if (result.factors[static_cast<size_t>(other)] != 0) { ++slanted; slid = other; }
	}
	// Both paths run the scene guard and record one undo step; a candidate
	// keeps the live document untouched on refusal.
	LevelMapDocument candidate = *document;
	const bool done = slanted == 1
		? shearLevelMapSelectionAbout(&candidate, slid, request.axis, result.factors[static_cast<size_t>(slid)], result.anchor, request.textures, &problem)
		: transformLevelMapSelectionAffine(&candidate, result.matrix, request.textures, &problem);
	if (!done) { return fail(problem); }
	if (!levelMapSelectionBounds(candidate, &result.mins, &result.maxs)) { result.mins = result.maxs = {}; }
	*document = std::move(candidate);
	if (report) { *report = result; }
	return true;
}

QVector<QPair<LevelMapVec3, LevelMapVec3>> levelObjectManipulatorEdges(const LevelMapDocument& document,
	const QVector<LevelMapSelectionRef>& objects, int limit, bool* limited)
{
	QVector<QPair<LevelMapVec3, LevelMapVec3>> edges;
	if (limited) { *limited = false; }
	QSet<int> brushes, patches, entities, owners;
	for (const auto& ref : objects) {
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) { brushes.insert(ref.objectId); }
		else if (ref.kind == LevelMapSelectionKind::QuakePatch) { patches.insert(ref.objectId); }
		else if (ref.kind == LevelMapSelectionKind::Entity) { entities.insert(ref.objectId); }
	}
	bool over = limit < 12;
	const auto add = [&edges, &over, limit](const LevelMapVec3& a, const LevelMapVec3& b) {
		if (edges.size() >= limit) { over = true; return; }
		edges.append({a, b});
	};
	// The twelve edges of a box, each corner to its neighbours.
	const auto boxEdges = [](const LevelMapVec3& low, const LevelMapVec3& high) {
		QVector<QPair<LevelMapVec3, LevelMapVec3>> result;
		const auto corner = [&low, &high](int bits) {
			return LevelMapVec3 {(bits & 1) ? high.x : low.x, (bits & 2) ? high.y : low.y, (bits & 4) ? high.z : low.z, true};
		};
		for (int bits = 0; bits < 8; ++bits) {
			for (int bit : {1, 2, 4}) {
				if ((bits & bit) == 0) { result.append({corner(bits), corner(bits | bit)}); }
			}
		}
		return result;
	};
	const auto box = [&add, &boxEdges](const LevelMapVec3& low, const LevelMapVec3& high) {
		for (const auto& edge : boxEdges(low, high)) { add(edge.first, edge.second); }
	};
	for (const auto& brush : document.brushes) {
		if (over) { break; }
		owners.insert(brush.entityId);
		if (!brushes.contains(brush.id) && !entities.contains(brush.entityId)) { continue; }
		const MapBrushGeometry geometry = solveBrushGeometry(brush.faces, brush.id, brush.entityId);
		for (const auto& polygon : geometry.faces) {
			for (qsizetype i = 0; i < polygon.points.size() && !over; ++i) { add(polygon.points[i], polygon.points[(i + 1) % polygon.points.size()]); }
		}
	}
	for (const auto& patch : document.patches) {
		if (over) { break; }
		owners.insert(patch.entityId);
		if (!patches.contains(patch.id) && !entities.contains(patch.entityId)) { continue; }
		if (patch.width <= 0 || patch.height <= 0 || patch.controlPoints.size() != patch.width * patch.height) { continue; }
		for (int row = 0; row < patch.height; ++row) {
			for (int column = 0; column < patch.width; ++column) {
				const int index = row * patch.width + column;
				if (column + 1 < patch.width) { add(patch.controlPoints[index], patch.controlPoints[index + 1]); }
				if (row + 1 < patch.height) { add(patch.controlPoints[index], patch.controlPoints[index + patch.width]); }
			}
		}
	}
	for (const auto& entity : document.entities) {
		if (over) { break; }
		if (!entities.contains(entity.id) || owners.contains(entity.id) || !finite(entity.origin)) { continue; }
		const auto& o = entity.origin;
		box({o.x - 8, o.y - 8, o.z - 8, true}, {o.x + 8, o.y + 8, o.z + 8, true});
	}
	if (over) {
		// Too many to draw while dragging: the box of all of them stands in.
		edges.clear();
		LevelMapVec3 low, high;
		if (levelMapObjectsBounds(document, objects, &low, &high)) { edges = boxEdges(low, high); }
		if (limited) { *limited = true; }
	}
	return edges;
}
} // namespace vibestudio
