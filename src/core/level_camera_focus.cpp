// Levelling and focusing the level camera (core/level_camera_focus.h).
//
// Workflow reference: NRC camwindow.cpp GlobalCamera_ResetAngles,
// Camera_getFocusPos and GlobalCamera_FocusOnSelected at 449778b3
// (GPL-2.0-or-later); Q3Radiant MainFrm.cpp OnViewCenter at dbe4ddb1
// (GPL-2.0-or-later). Original Qt implementation; see docs/CREDITS.md.

#include "core/level_camera_focus.h"
#include "core/level_visibility.h"
#include "core/map_geometry.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace vibestudio {
namespace {

constexpr double kStepDegrees = 22.5;
constexpr double kCoordinateLimit = 1.0e7;

bool finiteCoordinate(double value)
{
	return std::isfinite(value) && std::abs(value) <= kCoordinateLimit;
}

LevelCameraFocus failed(const QString& error)
{
	LevelCameraFocus focus;
	focus.error = error;
	return focus;
}

} // namespace

double levelCameraLevelledYaw(double yawDegrees)
{
	if (!std::isfinite(yawDegrees)) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double heading = std::fmod(yawDegrees, 360.0);
	if (heading < 0.0) {
		heading += 360.0;
	}
	double levelled = std::round(heading / kStepDegrees) * kStepDegrees;
	if (levelled >= 360.0) {
		levelled -= 360.0;
	}
	return levelled;
}

double levelCameraFocusOffsetScale(int offsetSteps)
{
	const int steps = std::clamp(offsetSteps, -kLevelCameraFocusOffsetLimit, kLevelCameraFocusOffsetLimit);
	return std::exp2(-static_cast<double>(steps) / 8.0);
}

LevelCameraFocus levelCameraFocusPosition(const LevelCameraFocusRequest& request)
{
	const LevelMapVec3& low = request.mins;
	const LevelMapVec3& high = request.maxs;
	if (!finiteCoordinate(low.x) || !finiteCoordinate(low.y) || !finiteCoordinate(low.z) || !finiteCoordinate(high.x)
		|| !finiteCoordinate(high.y) || !finiteCoordinate(high.z) || low.x > high.x || low.y > high.y || low.z > high.z) {
		return failed(QCoreApplication::translate("LevelCameraFocus", "The box to focus on has no valid size or position."));
	}
	if (!std::isfinite(request.yawDegrees) || !std::isfinite(request.pitchDegrees) || std::abs(request.pitchDegrees) > 90.0) {
		return failed(QCoreApplication::translate("LevelCameraFocus", "The camera heading must be finite and its pitch between -90 and 90 degrees."));
	}
	if (!std::isfinite(request.fieldOfViewDegrees) || request.fieldOfViewDegrees < 15.0 || request.fieldOfViewDegrees > 150.0) {
		return failed(QCoreApplication::translate("LevelCameraFocus", "The field of view must be between 15 and 150 degrees."));
	}
	if (!std::isfinite(request.aspect) || request.aspect < 0.01 || request.aspect > 100.0) {
		return failed(QCoreApplication::translate("LevelCameraFocus", "The view's width to height ratio must be between 0.01 and 100."));
	}
	// The camera's own axes: forward along the view, right level with the
	// ground (Quake-family cameras never roll), up completing the basis.
	const double yaw = request.yawDegrees * std::numbers::pi / 180.0;
	const double pitch = request.pitchDegrees * std::numbers::pi / 180.0;
	const std::array<double, 3> forward {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
	const std::array<double, 3> right {std::sin(yaw), -std::cos(yaw), 0.0};
	const std::array<double, 3> up {-std::cos(yaw) * std::sin(pitch), -std::sin(yaw) * std::sin(pitch), std::cos(pitch)};
	const double tanVertical = std::tan(request.fieldOfViewDegrees * std::numbers::pi / 360.0);
	const double tanHorizontal = tanVertical * request.aspect;
	const std::array<double, 3> centre {(low.x + high.x) * 0.5, (low.y + high.y) * 0.5, (low.z + high.z) * 0.5};
	const std::array<double, 3> half {(high.x - low.x) * 0.5, (high.y - low.y) * 0.5, (high.z - low.z) * 0.5};
	// Standing `d` back from the centre along the view, a corner `q` from the
	// centre is `d + q.forward` deep, and inside the view while its sideways
	// and upward offsets stay within that depth times the half-angle tangents.
	// The fit is the smallest `d` that holds for all eight corners.
	double fit = kLevelCameraFocusMinimumDistance;
	for (int corner = 0; corner < 8; ++corner) {
		const std::array<double, 3> offset {(corner & 1) ? half[0] : -half[0], (corner & 2) ? half[1] : -half[1], (corner & 4) ? half[2] : -half[2]};
		const auto dot = [&offset](const std::array<double, 3>& axis) { return offset[0] * axis[0] + offset[1] * axis[1] + offset[2] * axis[2]; };
		const double along = dot(forward);
		fit = std::max({fit, std::abs(dot(right)) / tanHorizontal - along, std::abs(dot(up)) / tanVertical - along,
			kLevelCameraFocusNearClearance - along});
	}
	if (!std::isfinite(fit)) {
		return failed(QCoreApplication::translate("LevelCameraFocus", "The box to focus on has no valid size or position."));
	}
	LevelCameraFocus focus;
	focus.valid = true;
	focus.fitDistance = fit;
	focus.distance = fit * levelCameraFocusOffsetScale(request.offsetSteps);
	focus.centre = {centre[0], centre[1], centre[2], true};
	focus.position = {centre[0] - forward[0] * focus.distance, centre[1] - forward[1] * focus.distance, centre[2] - forward[2] * focus.distance, true};
	return focus;
}

bool levelCameraFocusObjectsBounds(const LevelMapDocument& document, const QVector<LevelMapSelectionRef>& objects,
	LevelMapVec3* mins, LevelMapVec3* maxs)
{
	const QSet<QString> hidden = levelMapHiddenObjects(document);
	QVector<LevelMapSelectionRef> measured;
	measured.reserve(objects.size());
	for (const LevelMapSelectionRef& ref : objects) {
		if (levelMapObjectExists(document, ref) && !hidden.contains(levelMapSelectionRefId(ref))) {
			measured.push_back(ref);
		}
	}
	LevelMapVec3 low;
	LevelMapVec3 high;
	if (measured.isEmpty() || !levelMapObjectsBounds(document, measured, &low, &high)) {
		return false;
	}
	low.valid = high.valid = true;
	if (mins) {
		*mins = low;
	}
	if (maxs) {
		*maxs = high;
	}
	return true;
}

QString levelCameraFloorDirectionId(LevelCameraFloorDirection direction)
{
	return direction == LevelCameraFloorDirection::Up ? QStringLiteral("up") : QStringLiteral("down");
}

bool levelCameraFloorDirectionFromId(const QString& id, LevelCameraFloorDirection* direction)
{
	const QString key = id.trimmed().toLower();
	if (key != QLatin1String("up") && key != QLatin1String("down")) {
		return false;
	}
	if (direction) {
		*direction = key == QLatin1String("up") ? LevelCameraFloorDirection::Up : LevelCameraFloorDirection::Down;
	}
	return true;
}

QString levelCameraFloorSourceId(LevelCameraFloorSource source)
{
	switch (source) {
	case LevelCameraFloorSource::StandingHeight:
		return QStringLiteral("standing-height");
	case LevelCameraFloorSource::Underside:
		return QStringLiteral("underside");
	case LevelCameraFloorSource::SectorEyeHeight:
		return QStringLiteral("sector-eye-height");
	case LevelCameraFloorSource::None:
		break;
	}
	return QStringLiteral("none");
}

// Workflow reference: NRC camwindow.cpp FloorHeightWalker and
// CamWnd::Cam_ChangeFloor at 449778b3 (GPL-2.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.
LevelCameraFloorStep levelCameraFloorStep(const LevelMapDocument& document, const LevelCameraFloorRequest& request)
{
	LevelCameraFloorStep step;
	const LevelMapVec3& camera = request.position;
	if (!finiteCoordinate(camera.x) || !finiteCoordinate(camera.y) || !finiteCoordinate(camera.z)) {
		step.error = QCoreApplication::translate("LevelCameraFocus", "The camera position must be finite.");
		return step;
	}
	step.valid = true;
	step.position = {camera.x, camera.y, camera.z, true};
	QSet<QString> excluded = levelMapHiddenObjects(document);
	excluded.unite(request.excludedObjects);
	QSet<QString> selected;
	for (const LevelMapSelectionRef& ref : document.selection) {
		selected.insert(levelMapSelectionRefId(ref));
	}
	const double current = camera.z;
	// The nearest standing heights above and below, and the lowest underside.
	struct Candidate {
		bool found = false;
		double height = 0.0;
		LevelMapSelectionRef object;
	};
	Candidate up;
	Candidate down;
	Candidate bottom;
	const auto offer = [&](double stand, const LevelMapSelectionRef& object) {
		if (stand > current + kLevelCameraFloorEpsilon && (!up.found || stand < up.height)) {
			up = {true, stand, object};
		}
		if (stand < current - kLevelCameraFloorEpsilon && (!down.found || stand > down.height)) {
			down = {true, stand, object};
		}
	};
	const auto under = [&camera](double lowX, double lowY, double highX, double highY) {
		return camera.x > lowX && camera.x < highX && camera.y > lowY && camera.y < highY;
	};
	const bool doom = document.format == LevelMapFormat::DoomWad;
	if (doom) {
		QHash<int, const LevelMapDoomSector*> sectors;
		for (const LevelMapDoomSector& sector : document.doomSectors) {
			sectors.insert(sector.id, &sector);
		}
		const QPointF at(camera.x, camera.y);
		for (const DoomSectorOutline& outline : buildDoomSectorOutlines(document)) {
			const LevelMapSelectionRef ref {LevelMapSelectionKind::DoomSector, outline.sectorId};
			const QString id = levelMapSelectionRefId(ref);
			const LevelMapDoomSector* sector = sectors.value(outline.sectorId, nullptr);
			if (!sector || excluded.contains(id)) {
				continue;
			}
			bool inside = false;
			if (outline.bounds.contains(at)) {
				for (const QPolygonF& loop : outline.loops) {
					if (loop.containsPoint(at, Qt::OddEvenFill)) {
						inside = !inside;
					}
				}
			}
			if (!inside && !selected.contains(id)) {
				continue;
			}
			// The player's eye stands 41 units above the floor, but never
			// closer than 4 units to the ceiling (P_CalcHeight), nor under the
			// floor in a closed sector.
			const double floorHeight = sector->floorHeight;
			const double eye = std::max(floorHeight, std::min(floorHeight + kLevelCameraDoomEyeHeight, sector->ceilingHeight - kLevelCameraDoomCeilingGap));
			++step.candidates;
			offer(eye, ref);
		}
	} else {
		QSet<int> owners;
		QSet<int> selectedEntities;
		for (const LevelMapBrush& brush : document.brushes) {
			owners.insert(brush.entityId);
		}
		for (const LevelMapPatch& patch : document.patches) {
			owners.insert(patch.entityId);
		}
		for (const LevelMapSelectionRef& ref : document.selection) {
			if (ref.kind == LevelMapSelectionKind::Entity) {
				selectedEntities.insert(ref.objectId);
			}
		}
		const auto measure = [&](const LevelMapSelectionRef& ref, int entityId, const LevelMapVec3& low, const LevelMapVec3& high) {
			if (!finiteCoordinate(low.x) || !finiteCoordinate(low.y) || !finiteCoordinate(low.z) || !finiteCoordinate(high.x)
				|| !finiteCoordinate(high.y) || !finiteCoordinate(high.z) || low.z > high.z) {
				return;
			}
			const QString id = levelMapSelectionRefId(ref);
			if (excluded.contains(id)) {
				return;
			}
			if (!under(low.x, low.y, high.x, high.y) && !selected.contains(id) && !selectedEntities.contains(entityId)) {
				return;
			}
			++step.candidates;
			offer(high.z + kLevelCameraFloorStandHeight, ref);
			const double underside = low.z - kLevelCameraFloorUndersideClearance;
			if (!bottom.found || underside < bottom.height) {
				bottom = {true, underside, ref};
			}
		};
		for (const LevelMapBrush& brush : document.brushes) {
			if (brush.boundsSolved) {
				measure({LevelMapSelectionKind::QuakeBrush, brush.id}, brush.entityId, brush.mins, brush.maxs);
			}
		}
		for (const LevelMapPatch& patch : document.patches) {
			if (patch.mins.valid && patch.maxs.valid) {
				measure({LevelMapSelectionKind::QuakePatch, patch.id}, patch.entityId, patch.mins, patch.maxs);
			}
		}
		for (const LevelMapEntity& entity : document.entities) {
			if (owners.contains(entity.id) || !entity.origin.valid || entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0) {
				continue;
			}
			const ResizeBox box = request.entityBounds.value(entity.className.toCaseFolded(), ResizeBox {{-8.0, -8.0, -8.0}, {8.0, 8.0, 8.0}});
			const LevelMapVec3 low {entity.origin.x + box.mins[0], entity.origin.y + box.mins[1], entity.origin.z + box.mins[2], true};
			const LevelMapVec3 high {entity.origin.x + box.maxs[0], entity.origin.y + box.maxs[1], entity.origin.z + box.maxs[2], true};
			if (low.x <= high.x && low.y <= high.y) {
				measure({LevelMapSelectionKind::Entity, entity.id}, entity.id, low, high);
			}
		}
	}
	Candidate chosen;
	if (request.direction == LevelCameraFloorDirection::Up) {
		// Below everything, the camera first rises to the lowest underside.
		if (bottom.found && bottom.height > current) {
			chosen = bottom;
			step.source = LevelCameraFloorSource::Underside;
		} else if (up.found) {
			chosen = up;
			step.source = doom ? LevelCameraFloorSource::SectorEyeHeight : LevelCameraFloorSource::StandingHeight;
		}
	} else if (down.found) {
		chosen = down;
		step.source = doom ? LevelCameraFloorSource::SectorEyeHeight : LevelCameraFloorSource::StandingHeight;
	} else if (bottom.found && bottom.height < current) {
		chosen = bottom;
		step.source = LevelCameraFloorSource::Underside;
	}
	if (!chosen.found || chosen.height == current) {
		step.source = LevelCameraFloorSource::None;
		return step;
	}
	step.moved = true;
	step.object = chosen.object;
	step.position.z = chosen.height;
	return step;
}

} // namespace vibestudio
