// TrenchBroom's Convert Selection to Patches. Workflow reference: TB
// ActionManager "Menu/Edit/Convert Selection to Patches" and
// MapWindow::convertSelectionToPatches at 4c756be1 (GPL-3.0-or-later).
// Original implementation; see docs/CREDITS.md.

#include "core/level_patch_convert.h"

#include "core/level_patch.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_texture_mapping.h"
#include "core/level_visibility.h"
#include "core/map_geometry.h"

#include <QCoreApplication>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <optional>

namespace vibestudio {
namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioLevelPatchConvert)
};

LevelMapVec3 add(const LevelMapVec3& a, const LevelMapVec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z, true}; }
LevelMapVec3 sub(const LevelMapVec3& a, const LevelMapVec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, true}; }
LevelMapVec3 scale(const LevelMapVec3& a, double s) { return {a.x * s, a.y * s, a.z * s, true}; }
double dot(const LevelMapVec3& a, const LevelMapVec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
LevelMapVec3 cross(const LevelMapVec3& a, const LevelMapVec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, true}; }
double length(const LevelMapVec3& a) { return std::sqrt(dot(a, a)); }

bool fail(QString* error, const QString& message)
{
	if (error) {
		*error = message;
	}
	return false;
}

bool gridDimension(int value) { return value >= 3 && value <= kLevelPatchMaxDimension && value % 2 == 1; }

LevelMapVec3 centroid(const QVector<LevelMapVec3>& points)
{
	LevelMapVec3 sum {0, 0, 0, true};
	for (const LevelMapVec3& point : points) {
		sum = add(sum, point);
	}
	return points.isEmpty() ? sum : scale(sum, 1.0 / static_cast<double>(points.size()));
}

// One face of a solved brush, as patches. `inside` is a point inside the
// brush, so each patch can face away from it.
bool facePatches(const LevelMapBrush& brush, const MapBrushGeometry& geometry, int faceIndex, const LevelMapVec3& inside,
	const LevelPatchConvertRequest& request, QHash<QString, QSize>* sizes, QStringList* unsized, QVector<LevelMapPatch>* out, QString* error)
{
	if (faceIndex < 0 || faceIndex >= brush.faces.size() || faceIndex >= geometry.faces.size()) {
		return fail(error, Text::tr("The brush has no such face."));
	}
	const LevelMapBrushFace& face = brush.faces[faceIndex];
	if (face.explicitPlane) {
		return fail(error, Text::tr("brushDef3 (idTech 4) faces are not converted; Quake III patches are patchDef2."));
	}
	// The face's corners, without repeats.
	QVector<LevelMapVec3> corners;
	for (const LevelMapVec3& point : geometry.faces[faceIndex].points) {
		if (corners.isEmpty() || length(sub(point, corners.last())) > 1e-6) {
			corners << point;
		}
	}
	while (corners.size() > 1 && length(sub(corners.first(), corners.last())) <= 1e-6) {
		corners.removeLast();
	}
	if (corners.size() < 3) {
		return fail(error, Text::tr("The face has no area."));
	}
	const LevelTextureProjection projection = levelTextureProjection(face);
	if (!projection.valid) {
		return fail(error, Text::tr("The face's texture mapping is not finite. Reset it first."));
	}
	// Patches count repeats; classic and Valve 220 faces count texels.
	QSize image(1, 1);
	if (!projection.normalizedCoordinates) {
		const QString key = face.textureName.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/')).toCaseFolded();
		if (!sizes->contains(key)) {
			QSize found = request.imageSize ? request.imageSize(face.textureName) : QSize();
			if (found.width() <= 0 || found.height() <= 0) {
				found = QSize(64, 64);
				if (!unsized->contains(face.textureName)) {
					*unsized << face.textureName;
				}
			}
			sizes->insert(key, found);
		}
		image = sizes->value(key);
	}
	// Newell's normal, turned to point out of the brush.
	LevelMapVec3 normal {0, 0, 0, true};
	for (qsizetype i = 0; i < corners.size(); ++i) {
		normal = add(normal, cross(corners[i], corners[(i + 1) % corners.size()]));
	}
	if (length(normal) < 1e-9) {
		return fail(error, Text::tr("The face has no area."));
	}
	if (dot(normal, sub(centroid(corners), inside)) < 0) {
		normal = scale(normal, -1);
	}
	// From the first corner: quadrilaterals, then a triangle if one is left.
	QVector<std::array<LevelMapVec3, 4>> pieces;
	const int n = static_cast<int>(corners.size());
	int next = 1;
	while (next + 2 <= n - 1) {
		pieces.push_back({corners[0], corners[next], corners[next + 1], corners[next + 2]});
		next += 2;
	}
	if (next + 1 == n - 1) {
		pieces.push_back({corners[0], corners[next], corners[next + 1], corners[next + 1]});
	}
	const QString material = levelPatchMaterialToken(face.textureName, false);
	for (auto corner : pieces) {
		// Columns run from the first corner to the second, rows from the
		// first to the fourth; their cross product is the side a patch shows.
		if (dot(cross(sub(corner[1], corner[0]), sub(corner[3], corner[0])), normal) < 0) {
			std::swap(corner[1], corner[3]);
		}
		LevelMapPatch patch;
		patch.entityId = brush.entityId;
		patch.textureName = material;
		patch.width = request.columns;
		patch.height = request.rows;
		patch.controlGridNormalized = true;
		patch.definitionDirty = true;
		for (int row = 0; row < patch.height; ++row) {
			const double v = static_cast<double>(row) / (patch.height - 1);
			for (int column = 0; column < patch.width; ++column) {
				const double u = static_cast<double>(column) / (patch.width - 1);
				// Bilinear across the corners: on a flat face every span's
				// middle control is its midpoint, so the patch is the face.
				const LevelMapVec3 point = add(add(scale(corner[0], (1 - u) * (1 - v)), scale(corner[1], u * (1 - v))),
					add(scale(corner[2], u * v), scale(corner[3], (1 - u) * v)));
				const QPointF at = projection.at(point);
				patch.controlPoints << point;
				patch.controlU << at.x() / image.width();
				patch.controlV << at.y() / image.height();
			}
		}
		refreshLevelPatchBounds(&patch);
		if (!validateLevelPatch(patch, error)) {
			return false;
		}
		*out << patch;
	}
	return true;
}

} // namespace

bool levelBrushFacePatches(const LevelMapBrush& brush, int faceIndex, const LevelPatchConvertRequest& request, QVector<LevelMapPatch>* patches,
	QString* error)
{
	if (error) {
		error->clear();
	}
	if (!patches) {
		return fail(error, Text::tr("Missing patch destination."));
	}
	if (!gridDimension(request.columns) || !gridDimension(request.rows)) {
		return fail(error, Text::tr("Patch columns and rows must be odd numbers from 3 to 31."));
	}
	const MapBrushGeometry geometry = solveBrushGeometry(brush.faces, brush.id, brush.entityId);
	if (!geometry.solved) {
		return fail(error, Text::tr("The brush's planes do not close a solid. Fix the brush first."));
	}
	QVector<LevelMapVec3> all;
	for (const MapFacePolygon& polygon : geometry.faces) {
		all += polygon.points;
	}
	QHash<QString, QSize> sizes;
	QStringList unsized;
	QVector<LevelMapPatch> made;
	if (!facePatches(brush, geometry, faceIndex, centroid(all), request, &sizes, &unsized, &made, error)) {
		return false;
	}
	*patches = made;
	return true;
}

bool convertLevelMapFacesToPatches(LevelMapDocument* document, const QVector<LevelSurfaceFace>& faces, const QVector<int>& wholeBrushes,
	const LevelPatchConvertRequest& request, LevelPatchConvertReport* report, QString* error)
{
	LevelPatchConvertReport staged;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
			return convertLevelMapFacesToPatches(candidate, faces, wholeBrushes, request, &staged, error);
		});
		guarded.has_value()) {
		if (*guarded && report) {
			*report = staged;
		}
		return *guarded;
	}
	if (error) {
		error->clear();
	}
	if (!document || document->format != LevelMapFormat::Quake3Map) {
		return fail(error, Text::tr("Brush faces can be converted to patches in Quake III maps only."));
	}
	if (!gridDimension(request.columns) || !gridDimension(request.rows)) {
		return fail(error, Text::tr("Patch columns and rows must be odd numbers from 3 to 31."));
	}
	if (faces.isEmpty() && wholeBrushes.isEmpty()) {
		return fail(error, Text::tr("Select brushes, or pick brush faces, to convert to patches."));
	}
	// Each brush's faces to convert; whole brushes take every face.
	QMap<int, QSet<int>> wanted;
	QSet<int> whole;
	for (const LevelSurfaceFace& face : faces) {
		wanted[face.brushId].insert(face.faceIndex);
	}
	for (const int brushId : wholeBrushes) {
		whole.insert(brushId);
		wanted[brushId];
	}
	const QSet<QString> hidden = levelMapHiddenObjects(*document);
	std::optional<LevelSceneSelectionContext> context;
	if (!document->openSceneGroups.isEmpty()) {
		context = levelSceneSelectionContext(*document);
	}
	LevelPatchConvertReport result;
	QHash<QString, QSize> sizes;
	QMap<int, QVector<LevelMapPatch>> byOwner;
	QVector<LevelMapSelectionRef> removed;
	int found = 0;
	for (const LevelMapBrush& brush : std::as_const(document->brushes)) {
		if (!wanted.contains(brush.id)) {
			continue;
		}
		++found;
		const QString self = levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush, brush.id});
		const QString owner = levelMapSelectionRefId({LevelMapSelectionKind::Entity, brush.entityId});
		if (hidden.contains(self) || hidden.contains(owner)) {
			result.skipped << Text::tr("Brush %1 is hidden.").arg(brush.id);
			continue;
		}
		if (context && !context->openGroup.isEmpty() && !context->allowedObjects.contains(self)) {
			result.skipped << Text::tr("Brush %1 is outside the open group.").arg(brush.id);
			continue;
		}
		const MapBrushGeometry geometry = solveBrushGeometry(brush.faces, brush.id, brush.entityId);
		if (!geometry.solved) {
			result.skipped << Text::tr("Brush %1's planes do not close a solid.").arg(brush.id);
			continue;
		}
		QVector<LevelMapVec3> all;
		for (const MapFacePolygon& polygon : geometry.faces) {
			all += polygon.points;
		}
		const LevelMapVec3 inside = centroid(all);
		QList<int> indexes = whole.contains(brush.id) ? QList<int>() : wanted.value(brush.id).values();
		if (whole.contains(brush.id)) {
			for (int i = 0; i < brush.faces.size(); ++i) {
				indexes << i;
			}
		}
		std::sort(indexes.begin(), indexes.end());
		bool complete = true;
		for (const int index : std::as_const(indexes)) {
			QVector<LevelMapPatch> made;
			QString problem;
			if (!facePatches(brush, geometry, index, inside, request, &sizes, &result.unsizedMaterials, &made, &problem)) {
				// A face clipped away by the others has no area and nothing to lose.
				const bool empty = index < geometry.faces.size() && geometry.faces[index].points.size() < 3;
				if (!empty) {
					complete = false;
					result.skipped << Text::tr("Brush %1, face %2: %3").arg(brush.id).arg(index + 1).arg(problem);
				}
				continue;
			}
			byOwner[brush.entityId] += made;
			++result.faces;
		}
		// A whole brush goes once every face it shows is a patch.
		if (whole.contains(brush.id) && complete && !request.keepBrushes) {
			removed << LevelMapSelectionRef {LevelMapSelectionKind::QuakeBrush, brush.id};
		}
	}
	if (found != wanted.size()) {
		return fail(error, Text::tr("A brush to convert no longer exists."));
	}
	if (byOwner.isEmpty()) {
		return fail(error, result.skipped.isEmpty() ? Text::tr("Nothing to convert.") : result.skipped.join(QLatin1Char('\n')));
	}
	LevelMapDocument working = *document;
	const quint64 revision = working.revision;
	for (auto owner = byOwner.cbegin(); owner != byOwner.cend(); ++owner) {
		for (qsizetype start = 0; start < owner.value().size(); start += 64) {
			QVector<int> added;
			if (!addLevelMapPatches(&working, owner.value().mid(start, 64), owner.key(), &added, error)) {
				return false;
			}
			result.patchIds += added;
		}
	}
	if (!removed.isEmpty()) {
		if (!deleteLevelMapObjects(&working, removed, error)) {
			return false;
		}
		result.brushesRemoved = static_cast<int>(removed.size());
	}
	const int steps = static_cast<int>(working.revision - revision);
	const int converted = result.faces;
	if (steps > 1
		&& !collapseLevelMapUndoSteps(&working, steps, Text::tr("Convert %n face(s) to patches", nullptr, converted),
			Text::tr("Restore the faces converted to patches"), error)) {
		return false;
	}
	QVector<LevelMapSelectionRef> selection;
	for (const int id : std::as_const(result.patchIds)) {
		selection << LevelMapSelectionRef {LevelMapSelectionKind::QuakePatch, id};
	}
	setLevelMapSelection(&working, selection);
	*document = std::move(working);
	if (report) {
		*report = result;
	}
	return true;
}

} // namespace vibestudio
