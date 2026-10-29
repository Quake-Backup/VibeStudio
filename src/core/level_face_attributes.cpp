// The Face inspector's service: brush faces' texture values read and set in
// each face's own format, and the UV view's handles, which turn and stretch
// a texture about a pivot that keeps its place on it. Workflow reference: TB
// FaceAttribsEditor (offset, scale and angle fields, "multi" for mixed
// values), UVOffsetTool, UVOriginTool, UVRotateTool and UVScaleTool (a turn
// or stretch followed by an offset correction about the UV origin) at
// 4c756be1 (GPL-3.0-or-later). Original implementation; see docs/CREDITS.md.

#include "core/level_face_attributes.h"

#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_texture_mapping.h"
#include "core/level_visibility.h"

#include <QCoreApplication>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <optional>

namespace vibestudio {
namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioLevelFaceAttributes)
};

bool fail(QString* error, const QString& message)
{
	if (error) {
		*error = message;
	}
	return false;
}

QString materialKey(QString name)
{
	return name.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/')).toCaseFolded();
}

bool finitePoint(QPointF point)
{
	return std::isfinite(point.x()) && std::isfinite(point.y());
}

// Texels per stored unit: the image size for a brush-primitive face, which
// counts repeats, otherwise one.
bool texelUnit(const LevelMapBrushFace& face, QSize imageSize, QPointF* unit)
{
	if (!face.explicitTextureMatrix) {
		*unit = {1, 1};
		return true;
	}
	if (imageSize.width() <= 0 || imageSize.height() <= 0) {
		return false;
	}
	*unit = {static_cast<double>(imageSize.width()), static_cast<double>(imageSize.height())};
	return true;
}

// Moves the texture by `texels` in each format's own offset units.
void moveTexture(LevelMapBrushFace* face, QPointF texels, QPointF unit)
{
	if (face->explicitTextureMatrix) {
		face->textureMatrix[2] += texels.x() / unit.x();
		face->textureMatrix[5] += texels.y() / unit.y();
	} else if (face->explicitTextureAxes) {
		face->uOffset += texels.x();
		face->vOffset += texels.y();
	} else {
		face->shiftX += texels.x();
		face->shiftY += texels.y();
	}
}

double roundedOffset(double value)
{
	const double rounded = std::round(value * 10000.0) / 10000.0;
	return rounded == 0 ? 0.0 : rounded;
}

bool sameMappingFields(const LevelMapBrushFace& a, const LevelMapBrushFace& b)
{
	const auto same = [](const LevelMapVec3& x, const LevelMapVec3& y) { return x.valid == y.valid && x.x == y.x && x.y == y.y && x.z == y.z; };
	return a.shiftX == b.shiftX && a.shiftY == b.shiftY && a.scaleX == b.scaleX && a.scaleY == b.scaleY && a.rotation == b.rotation
		&& a.uOffset == b.uOffset && a.vOffset == b.vOffset && same(a.uAxis, b.uAxis) && same(a.vAxis, b.vAxis) && a.textureMatrix == b.textureMatrix;
}

bool sameValue(double a, double b)
{
	return std::abs(a - b) <= 1e-9 * std::max({1.0, std::abs(a), std::abs(b)});
}

} // namespace

bool levelFaceTexelAt(const LevelMapBrushFace& face, QSize imageSize, const LevelMapVec3& world, QPointF* texel)
{
	QPointF unit;
	const LevelTextureProjection projection = levelTextureProjection(face);
	if (!projection.valid || !texelUnit(face, imageSize, &unit) || !std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z)) {
		return false;
	}
	const QPointF at = projection.at(world);
	const QPointF result(at.x() * unit.x(), at.y() * unit.y());
	if (!finitePoint(result)) {
		return false;
	}
	if (texel) {
		*texel = result;
	}
	return true;
}

bool levelFaceWorldAtTexel(const QVector<LevelMapVec3>& points, const QVector<QPointF>& uv, QPointF texel, LevelMapVec3* world)
{
	if (points.size() < 3 || uv.size() != points.size() || !finitePoint(texel)) {
		return false;
	}
	// The two points that span the largest triangle with the first, so a
	// sliver of a face still solves well.
	int bestI = -1, bestJ = -1;
	double bestArea = 0;
	for (int i = 1; i < uv.size(); ++i) {
		for (int j = i + 1; j < uv.size(); ++j) {
			const QPointF a = uv[i] - uv[0], b = uv[j] - uv[0];
			const double area = std::abs(a.x() * b.y() - a.y() * b.x());
			if (area > bestArea) {
				bestArea = area;
				bestI = i;
				bestJ = j;
			}
		}
	}
	if (bestI < 0 || !std::isfinite(bestArea) || bestArea < 1e-12) {
		return false;
	}
	const QPointF a = uv[bestI] - uv[0], b = uv[bestJ] - uv[0], d = texel - uv[0];
	const double determinant = a.x() * b.y() - a.y() * b.x();
	const double s = (d.x() * b.y() - d.y() * b.x()) / determinant;
	const double t = (a.x() * d.y() - a.y() * d.x()) / determinant;
	const LevelMapVec3& p0 = points[0];
	const LevelMapVec3& pi = points[bestI];
	const LevelMapVec3& pj = points[bestJ];
	const LevelMapVec3 result{p0.x + s * (pi.x - p0.x) + t * (pj.x - p0.x), p0.y + s * (pi.y - p0.y) + t * (pj.y - p0.y),
		p0.z + s * (pi.z - p0.z) + t * (pj.z - p0.z), true};
	if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z)) {
		return false;
	}
	if (world) {
		*world = result;
	}
	return true;
}

bool applyLevelFaceAttributesEdit(const LevelMapBrushFace& face, const LevelFaceAttributesEdit& edit, QSize imageSize, LevelMapBrushFace* result,
	QString* error)
{
	if (error) {
		error->clear();
	}
	if (!result) {
		return fail(error, Text::tr("Missing brush face."));
	}
	const auto within = [](bool set, double value, double limit) { return !set || (std::isfinite(value) && std::abs(value) <= limit); };
	if (!within(edit.setShiftU, edit.shiftU, 1e6) || !within(edit.setShiftV, edit.shiftV, 1e6) || !within(edit.setScaleU, edit.scaleU, 1e6)
		|| !within(edit.setScaleV, edit.scaleV, 1e6) || !within(edit.setRotation, edit.rotation, 360000) || !finitePoint(edit.moveTexels)
		|| std::abs(edit.moveTexels.x()) > 1e6 || std::abs(edit.moveTexels.y()) > 1e6) {
		return fail(error, Text::tr("Texture values must be finite and in range: offsets within a million texels, scales within a million and "
									"rotations within 360,000 degrees."));
	}
	if ((edit.setScaleU && std::abs(edit.scaleU) < 1e-6) || (edit.setScaleV && std::abs(edit.scaleV) < 1e-6)) {
		return fail(error, Text::tr("A texture scale cannot be 0."));
	}
	QPointF unit;
	if (!texelUnit(face, imageSize, &unit)) {
		return fail(error, Text::tr("Material %1 needs a known image size. Open its package or provide an explicit texture size.").arg(face.textureName));
	}
	LevelMapBrushFace out = face;
	QPointF before;
	const bool pivot = edit.keepPivot && edit.changesValues();
	if (pivot && (!edit.pivot.valid || !levelFaceTexelAt(face, imageSize, edit.pivot, &before))) {
		return fail(error, Text::tr("The pivot must be a finite point and the face's mapping finite."));
	}
	if (edit.changesValues()) {
		LevelTextureResetRequest values;
		values.setShiftU = edit.setShiftU;
		values.setShiftV = edit.setShiftV;
		values.setScaleU = edit.setScaleU;
		values.setScaleV = edit.setScaleV;
		values.setRotation = edit.setRotation;
		values.shiftU = edit.shiftU;
		values.shiftV = edit.shiftV;
		values.scaleU = edit.scaleU;
		values.scaleV = edit.scaleV;
		values.rotation = edit.rotation;
		if (!assignLevelTextureValues(&out, values, imageSize, error)) {
			return false;
		}
	}
	if (pivot) {
		QPointF after;
		if (!levelFaceTexelAt(out, imageSize, edit.pivot, &after)) {
			return fail(error, Text::tr("The new texture values give the face a mapping that is not finite."));
		}
		moveTexture(&out, before - after, unit);
		if (!out.explicitTextureMatrix) {
			double& u = out.explicitTextureAxes ? out.uOffset : out.shiftX;
			double& v = out.explicitTextureAxes ? out.vOffset : out.shiftY;
			u = roundedOffset(u);
			v = roundedOffset(v);
		}
	}
	moveTexture(&out, edit.moveTexels, unit);
	const LevelTextureProjection projection = levelTextureProjection(out);
	if (!projection.valid || !std::isfinite(projection.offsetU) || !std::isfinite(projection.offsetV)) {
		return fail(error, Text::tr("The new texture values give the face a mapping that is not finite."));
	}
	if (!sameMappingFields(out, face)) {
		out.textureParametersDirty = true;
	}
	*result = out;
	return true;
}

LevelFaceAttributesSummary levelFaceAttributesSummary(const LevelMapDocument& document, const QVector<LevelSurfaceFace>& faces,
	const QHash<QString, QSize>& textureSizes)
{
	LevelFaceAttributesSummary summary;
	QHash<QString, QSize> sizes;
	for (auto it = textureSizes.cbegin(); it != textureSizes.cend(); ++it) {
		sizes.insert(materialKey(it.key()), it.value());
	}
	QHash<int, int> indexes;
	for (int i = 0; i < document.brushes.size(); ++i) {
		indexes.insert(document.brushes[i].id, i);
	}
	QSet<QPair<int, int>> seen;
	bool first = true;
	for (const LevelSurfaceFace& target : faces) {
		const int index = indexes.value(target.brushId, -1);
		if (index < 0 || target.faceIndex < 0 || target.faceIndex >= document.brushes[index].faces.size()
			|| seen.contains({target.brushId, target.faceIndex})) {
			continue;
		}
		seen.insert({target.brushId, target.faceIndex});
		const LevelMapBrushFace& face = document.brushes[index].faces[target.faceIndex];
		++summary.faces;
		++(face.explicitTextureMatrix ? summary.brushPrimitive : face.explicitTextureAxes ? summary.valve220 : summary.classic);
		if (summary.faces == 1) {
			summary.material = face.textureName;
		} else if (materialKey(face.textureName) != materialKey(summary.material)) {
			summary.materialMixed = true;
		}
		LevelTextureValues values;
		QString problem;
		if (!levelTextureValues(face, sizes.value(materialKey(face.textureName)), &values, &problem)) {
			++summary.unreadable;
			if (summary.problem.isEmpty()) {
				summary.problem = problem;
			}
			continue;
		}
		++summary.readable;
		if (first) {
			summary.values = values;
			first = false;
			continue;
		}
		summary.shiftUMixed = summary.shiftUMixed || !sameValue(values.shiftU, summary.values.shiftU);
		summary.shiftVMixed = summary.shiftVMixed || !sameValue(values.shiftV, summary.values.shiftV);
		summary.scaleUMixed = summary.scaleUMixed || !sameValue(values.scaleU, summary.values.scaleU);
		summary.scaleVMixed = summary.scaleVMixed || !sameValue(values.scaleV, summary.values.scaleV);
		summary.rotationMixed = summary.rotationMixed || !sameValue(values.rotation, summary.values.rotation);
	}
	return summary;
}

bool prepareLevelFaceAttributesEdit(const LevelMapDocument& document, const QVector<LevelSurfaceFace>& faces, const LevelFaceAttributesEdit& edit,
	const QHash<QString, QSize>& textureSizes, LevelSurfaceEditPlan* plan, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!plan) {
		return fail(error, Text::tr("Missing texture values plan."));
	}
	if (document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map) {
		return fail(error, Text::tr("Texture values belong to brush faces. Open a Quake, Quake II or Quake III map."));
	}
	if (!edit.changesSomething()) {
		return fail(error, Text::tr("Give an offset, scale, rotation or move before setting texture values."));
	}
	if (faces.isEmpty()) {
		return fail(error, Text::tr("Select brushes or pick faces to set their texture values."));
	}
	if (faces.size() > 262144) {
		return fail(error, Text::tr("Texture values change at most 262,144 faces at once."));
	}
	QHash<QString, QSize> sizes;
	for (auto it = textureSizes.cbegin(); it != textureSizes.cend(); ++it) {
		sizes.insert(materialKey(it.key()), it.value());
	}
	QHash<int, int> indexes;
	for (int i = 0; i < document.brushes.size(); ++i) {
		indexes.insert(document.brushes[i].id, i);
	}
	QMap<int, QSet<int>> targets;
	for (const LevelSurfaceFace& face : faces) {
		const int index = indexes.value(face.brushId, -1);
		if (index < 0 || face.faceIndex < 0 || face.faceIndex >= document.brushes[index].faces.size()) {
			return fail(error, Text::tr("Brush %1 has no face %2.").arg(face.brushId).arg(qint64(face.faceIndex) + 1));
		}
		targets[face.brushId].insert(face.faceIndex);
	}
	// Every face must be editable, or nothing changes.
	const QSet<QString> hidden = levelMapHiddenObjects(document);
	const QSet<QString> locked = levelSceneLockedObjects(document);
	std::optional<LevelSceneSelectionContext> context;
	if (!document.openSceneGroups.isEmpty()) {
		context = levelSceneSelectionContext(document);
	}
	for (auto it = targets.cbegin(); it != targets.cend(); ++it) {
		const LevelMapBrush& brush = document.brushes[indexes.value(it.key())];
		const QString self = levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush, brush.id});
		const QString owner = levelMapSelectionRefId({LevelMapSelectionKind::Entity, brush.entityId});
		if (hidden.contains(self) || hidden.contains(owner)) {
			return fail(error, Text::tr("Brush %1 is hidden. Show it before setting its texture values.").arg(brush.id));
		}
		if (locked.contains(self) || locked.contains(owner)) {
			return fail(error, Text::tr("Brush %1 is in a locked layer or group. Unlock it before setting its texture values.").arg(brush.id));
		}
		if (context && !context->openGroup.isEmpty() && !context->allowedObjects.contains(self)) {
			return fail(error, Text::tr("Brush %1 is outside the open group. Close the group before setting its texture values.").arg(brush.id));
		}
	}

	LevelSurfaceEditPlan result;
	result.m_sourcePath = document.sourcePath;
	result.m_sourceHash = document.sourceContentHash;
	result.m_revision = document.revision;
	result.m_format = document.format;
	result.m_attributes = true;
	for (auto it = targets.cbegin(); it != targets.cend(); ++it) {
		const LevelMapBrush& brush = document.brushes[indexes.value(it.key())];
		LevelMapBrush after = brush;
		int changed = 0;
		for (const int index : *it) {
			const LevelMapBrushFace& face = brush.faces[index];
			LevelMapBrushFace edited;
			QString problem;
			if (!applyLevelFaceAttributesEdit(face, edit, sizes.value(materialKey(face.textureName)), &edited, &problem)) {
				return fail(error, Text::tr("Brush %1, face %2: %3").arg(brush.id).arg(index + 1).arg(problem));
			}
			if (sameMappingFields(edited, face)) {
				continue;
			}
			after.faces[index] = edited;
			++changed;
		}
		if (changed == 0) {
			continue;
		}
		after.textureParametersDirty = true;
		result.m_before.append(brush);
		result.m_after.append(after);
		result.m_faceCount += changed;
	}
	result.m_ready = true;
	*plan = std::move(result);
	return true;
}

bool setLevelMapFaceAttributes(LevelMapDocument* document, const QVector<LevelSurfaceFace>& faces, const LevelFaceAttributesEdit& edit,
	const QHash<QString, QSize>& textureSizes, LevelFaceAttributesReport* report, QString* error)
{
	if (!document) {
		return fail(error, Text::tr("Missing map."));
	}
	LevelSurfaceEditPlan plan;
	if (!prepareLevelFaceAttributesEdit(*document, faces, edit, textureSizes, &plan, error) || !commitLevelSurfaceEdit(document, plan, error)) {
		return false;
	}
	if (report) {
		QSet<QPair<int, int>> unique;
		for (const LevelSurfaceFace& face : faces) {
			unique.insert({face.brushId, face.faceIndex});
		}
		report->faces = static_cast<int>(unique.size());
		report->changedFaces = plan.faceCount();
		report->brushes = plan.brushCount();
	}
	return true;
}

} // namespace vibestudio
