#include "core/level_patch_cap.h"
#include "core/level_patch_tools.h"
#include "core/level_scene_locks.h"
#include "core/level_visibility.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const char *message)
{
	if (error) {
		*error = QCoreApplication::translate("LevelPatchCap", message);
	}
	return false;
}
LevelMapVec3 add(const LevelMapVec3 &a, const LevelMapVec3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z, true}; }
LevelMapVec3 scale(const LevelMapVec3 &a, double s) { return {a.x * s, a.y * s, a.z * s, true}; }
LevelMapVec3 sub(const LevelMapVec3 &a, const LevelMapVec3 &b) { return add(a, scale(b, -1)); }
double dot(const LevelMapVec3 &a, const LevelMapVec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
LevelMapVec3 cross(const LevelMapVec3 &a, const LevelMapVec3 &b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, true};
}
double length(const LevelMapVec3 &a) { return std::hypot(a.x, a.y, a.z); }

bool buildRadial(const LevelMapPatch &source, LevelPatchBoundary edge, const LevelPatchCapRequest &request, LevelPatchCapResult *out,
				 QString *error)
{
	const auto indices = levelPatchBoundaryIndices(source, edge);
	if (indices.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "Choose a valid patch boundary."));
	}
	QVector<LevelMapVec3> points;
	for (int i : indices) {
		points << source.controlPoints[i];
	}
	const bool closed = length(sub(points.first(), points.last())) <= 1e-7;
	LevelMapVec3 center{0, 0, 0, true};
	if (closed) {
		for (qsizetype i = 0; i < points.size() - 1; ++i) {
			center = add(center, points[i]);
		}
		center = scale(center, 1.0 / (points.size() - 1));
	} else {
		center = scale(add(points.first(), points.last()), 0.5);
	}
	if (request.customCenter) {
		if (!closed && length(sub(request.center, center)) > 1e-7) {
			return fail(error,
						QT_TRANSLATE_NOOP("LevelPatchCap",
										  "An open boundary closes along its endpoint chord; its center must be the chord midpoint."));
		}
		center = request.center;
	}
	if (!center.valid || !std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) || std::abs(center.x) > 1048576 ||
		std::abs(center.y) > 1048576 || std::abs(center.z) > 1048576) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "The cap center must contain finite coordinates within ±1048576."));
	}
	// Sum cross products around the translated polygon for a stable plane normal.
	LevelMapVec3 normal{0, 0, 0, true};
	for (qsizetype i = 0; i < points.size(); ++i) {
		normal = add(normal, cross(sub(points[i], center), sub(points[(i + 1) % points.size()], center)));
	}
	const double area = length(normal);
	if (area < 1e-8) {
		return fail(error,
					QT_TRANSLATE_NOOP("LevelPatchCap", "The boundary is collapsed, straight or self-crossing and cannot form a cap."));
	}
	normal = scale(normal, 1.0 / area);
	double radius = 0;
	for (const auto &p : points) {
		radius = std::max(radius, length(sub(p, center)));
	}
	for (const auto &p : points) {
		const auto v = sub(p, center);
		if (std::abs(dot(v, normal)) > std::max(1e-7, radius * 1e-10)) {
			return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap",
												 "The boundary and center must share one plane. Flatten the boundary before capping it."));
		}
		if (length(v) < 1e-7) {
			return fail(error,
						QT_TRANSLATE_NOOP("LevelPatchCap",
										  "A boundary control lies at the cap center. Choose another center or reshape the boundary."));
		}
	}
	// A positive angular sweep of at most one turn makes each radial ray unique.
	// For a quadratic arc, positive cross(p0,p1), cross(p1,p2) and cross(p0,p2)
	// imply cross(B(t),B'(t)) >= 0 throughout the span, not just at samples.
	double sweep = 0;
	for (qsizetype i = 1; i < points.size(); ++i) {
		const auto a = sub(points[i - 1], center), b = sub(points[i], center);
		const double angle = std::atan2(dot(normal, cross(a, b)), dot(a, b));
		if (angle < -1e-10 || angle >= std::numbers::pi - 1e-10) {
			return fail(error, QT_TRANSLATE_NOOP(
								   "LevelPatchCap",
								   "The boundary folds around the cap center. Reshape it or choose a center inside its visible region."));
		}
		sweep += std::max(0.0, angle);
	}
	for (qsizetype i = 0; i + 2 < points.size(); i += 2) {
		const auto a = sub(points[i], center), b = sub(points[i + 2], center);
		if (dot(normal, cross(a, b)) < -1e-10 * length(a) * length(b) ||
			(length(cross(a, b)) < 1e-10 * length(a) * length(b) && dot(a, b) > 0)) {
			return fail(error,
						QT_TRANSLATE_NOOP(
							"LevelPatchCap",
							"Each curved span must cover a positive angle of at most half a turn. Subdivide the boundary before capping."));
		}
	}
	const double expected = closed ? 2 * std::numbers::pi : std::numbers::pi;
	if (std::abs(sweep - expected) > 1e-7) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "The boundary overlaps itself or cannot close as a single radial cap."));
	}
	const bool reversed = (edge == LevelPatchBoundary::LastRow || edge == LevelPatchBoundary::FirstColumn) != request.invert;
	const auto uAxis = scale(sub(points.first(), center), 1.0 / length(sub(points.first(), center))), vAxis = cross(normal, uAxis);
	double centerU = 0, centerV = 0;
	for (int index : indices) {
		centerU += source.controlU[index] / indices.size();
		centerV += source.controlV[index] / indices.size();
	}
	LevelMapPatch cap;
	cap.entityId = source.entityId;
	cap.width = static_cast<int>(indices.size());
	cap.height = 3;
	cap.textureName = request.texture.isEmpty() ? source.textureName : levelPatchMaterialToken(request.texture, source.fixedSubdivisions);
	cap.fixedSubdivisions = source.fixedSubdivisions;
	cap.subdivisionsX =
		source.fixedSubdivisions
			? ((edge == LevelPatchBoundary::FirstRow || edge == LevelPatchBoundary::LastRow) ? source.subdivisionsX : source.subdivisionsY)
			: 0;
	cap.subdivisionsY = source.fixedSubdivisions ? 1 : 0;
	cap.headerTail = source.headerTail;
	cap.controlGridNormalized = cap.definitionDirty = true;
	for (int row = 0; row < 3; ++row) {
		const double radial = row * 0.5;
		for (int column = 0; column < cap.width; ++column) {
			const int i = reversed ? cap.width - 1 - column : column;
			const auto p = row == 2 ? points[i] : add(center, scale(sub(points[i], center), radial));
			cap.controlPoints << p;
			if (request.uv == LevelPatchCapUv::Planar) {
				cap.controlU << 0.5 + dot(sub(p, center), uAxis) / request.unitsPerTile;
				cap.controlV << 0.5 + dot(sub(p, center), vAxis) / request.unitsPerTile;
			} else {
				cap.controlU << centerU + (source.controlU[indices[i]] - centerU) * radial;
				cap.controlV << centerV + (source.controlV[indices[i]] - centerV) * radial;
			}
		}
	}
	refreshLevelPatchBounds(&cap);
	if (!validateLevelPatch(cap, error)) {
		return false;
	}
	out->caps << cap;
	out->sources << source.id;
	out->boundaries << edge;
	out->centers << center;
	out->closed << closed;
	out->reversed << reversed;
	return true;
}

// One control of a seam: a position and its texture coordinates, so every
// control the seam derives from the boundary carries the boundary's mapping
// the way it carries its position.
struct SeamPoint {
	LevelMapVec3 p{0, 0, 0, true};
	double u = 0, v = 0;
};
SeamPoint seamMix(const SeamPoint &a, double wa, const SeamPoint &b, double wb, const SeamPoint &c = {}, double wc = 0)
{
	return {add(add(scale(a.p, wa), scale(b.p, wb)), scale(c.p, wc)), a.u * wa + b.u * wb + c.u * wc, a.v * wa + b.v * wb + c.v * wc};
}

// NetRadiant Custom's Cap Selection seams: the control layouts of its
// Patch::ConstructSeam, from the boundary's controls `p` taken forwards (the
// first row) or backwards (the last row), as its Patch::MakeCap takes them.
// Workflow reference: NRC radiant/patch.cpp Patch::MakeCap and
// Patch::ConstructSeam, radiant/patchmanip.cpp Patch_makeCaps at 449778b3
// (GPL-2.0-or-later). Original implementation; see docs/CREDITS.md.
bool buildSeam(const LevelMapPatch &source, LevelPatchBoundary edge, const LevelPatchCapRequest &request, LevelPatchCapResult *out,
			   QString *error)
{
	const auto indices = levelPatchBoundaryIndices(source, edge);
	if (indices.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "Choose a valid patch boundary."));
	}
	const auto widths = levelPatchCapStyleWidths(request.style);
	const int width = static_cast<int>(indices.size());
	if (!widths.contains(width)) {
		if (error) {
			const QString style = levelPatchCapStyleLabel(request.style);
			*error = widths.size() == 1
						 ? QCoreApplication::translate("LevelPatchCap", "%1 caps need a boundary of %2 control points; this one has %3.")
							   .arg(style)
							   .arg(widths.first())
							   .arg(width)
						 : QCoreApplication::translate("LevelPatchCap", "%1 caps need a boundary of %2 or %3 control points; this one has %4.")
							   .arg(style)
							   .arg(widths.first())
							   .arg(widths.last())
							   .arg(width);
		}
		return false;
	}
	// The first row and last column forwards, as NetRadiant Custom takes its
	// first row; the last row and first column backwards, so each cap faces
	// out of the patch as Radial caps do. Invert takes them the other way,
	// which turns every seam layout round.
	const bool reversed = (edge == LevelPatchBoundary::LastRow || edge == LevelPatchBoundary::FirstColumn) != request.invert;
	QVector<SeamPoint> p;
	for (int i = 0; i < width; ++i) {
		const int index = indices[reversed ? width - 1 - i : i];
		p.append({source.controlPoints[index], source.controlU[index], source.controlV[index]});
	}
	double extent = 0;
	for (const auto &point : std::as_const(p)) {
		extent = std::max(extent, length(sub(point.p, p.first().p)));
	}
	if (extent < 1e-7) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "The boundary is collapsed to one point and cannot form a cap."));
	}
	LevelMapPatch cap;
	QVector<SeamPoint> grid;
	SeamPoint closesOn;
	switch (request.style) {
	case LevelPatchCapStyle::Bevel: {
		// The corner opposite the arc's middle control, where a quarter
		// round's centre lies; the arc runs down the last column.
		const SeamPoint corner = seamMix(p[2], 1, p[0], 1, p[1], -1);
		cap.width = cap.height = 3;
		grid = {corner, corner, p[2], corner, corner, p[1], corner, corner, p[0]};
		closesOn = corner;
		break;
	}
	case LevelPatchCapStyle::InvertedBevel:
		// The arc down the first column, filled out to its middle control.
		cap.width = cap.height = 3;
		grid = {p[0], p[1], p[1], p[1], p[1], p[1], p[2], p[1], p[1]};
		closesOn = p[1];
		break;
	case LevelPatchCapStyle::Endcap: {
		// The chord across the first row, each half of the arc down a side,
		// meeting at its middle control.
		const SeamPoint chord = seamMix(p[0], 0.5, p[4], 0.5);
		cap.width = cap.height = 3;
		grid = {p[0], chord, p[4], p[1], p[2], p[3], p[2], p[2], p[2]};
		closesOn = chord;
		break;
	}
	case LevelPatchCapStyle::InvertedEndcap:
		// The arc across the first row, out to the line through its corner
		// controls.
		cap.width = 5;
		cap.height = 3;
		grid = {p[4], p[3], p[2], p[1], p[0], p[3], p[3], p[2], p[1], p[1], p[3], p[3], p[2], p[1], p[1]};
		closesOn = p[2];
		break;
	case LevelPatchCapStyle::Cylinder:
		// Each half of the ring down a side, joined by straight chords:
		// the middle column halfway across each row, as Re-disperse puts it.
		cap.width = 3;
		cap.height = 5;
		for (int row = 0; row < 5; ++row) {
			grid << p[row] << seamMix(p[row], 0.5, p[8 - row], 0.5) << p[8 - row];
		}
		closesOn = seamMix(p[0], 0.5, p[4], 0.5);
		break;
	case LevelPatchCapStyle::Radial:
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "Choose a cap style."));
	}
	cap.entityId = source.entityId;
	cap.textureName = request.texture.isEmpty() ? source.textureName : levelPatchMaterialToken(request.texture, source.fixedSubdivisions);
	cap.fixedSubdivisions = source.fixedSubdivisions;
	const int along =
		(edge == LevelPatchBoundary::FirstRow || edge == LevelPatchBoundary::LastRow) ? source.subdivisionsX : source.subdivisionsY;
	cap.subdivisionsX = source.fixedSubdivisions ? along : 0;
	cap.subdivisionsY = source.fixedSubdivisions ? along : 0;
	cap.headerTail = source.headerTail;
	cap.controlGridNormalized = cap.definitionDirty = true;
	for (const auto &point : std::as_const(grid)) {
		cap.controlPoints << point.p;
		cap.controlU << point.u;
		cap.controlV << point.v;
	}
	refreshLevelPatchBounds(&cap);
	if (!validateLevelPatch(cap, error)) {
		return false;
	}
	// Radiant maps every new seam with Cap Texture.
	if (request.uv == LevelPatchCapUv::Planar && !planarLevelPatchTexture(&cap, request.unitsPerTile, request.unitsPerTile, error)) {
		return false;
	}
	const bool closed = length(sub(p.first().p, p.last().p)) <= 1e-7;
	out->caps << cap;
	out->sources << source.id;
	out->boundaries << edge;
	out->centers << closesOn.p;
	out->closed << closed;
	out->reversed << reversed;
	return true;
}
} // namespace

QStringList levelPatchCapStyleIds()
{
	return {QStringLiteral("radial"),		   QStringLiteral("bevel"),			  QStringLiteral("endcap"),
			QStringLiteral("inverted-bevel"), QStringLiteral("inverted-endcap"), QStringLiteral("cylinder")};
}

QString levelPatchCapStyleId(LevelPatchCapStyle style) { return levelPatchCapStyleIds().value(static_cast<int>(style)); }

QString levelPatchCapStyleLabel(LevelPatchCapStyle style)
{
	static const char *const labels[] = {QT_TRANSLATE_NOOP("LevelPatchCap", "Radial"),
										 QT_TRANSLATE_NOOP("LevelPatchCap", "Bevel"),
										 QT_TRANSLATE_NOOP("LevelPatchCap", "Endcap"),
										 QT_TRANSLATE_NOOP("LevelPatchCap", "Inverted Bevel"),
										 QT_TRANSLATE_NOOP("LevelPatchCap", "Inverted Endcap"),
										 QT_TRANSLATE_NOOP("LevelPatchCap", "Cylinder")};
	const int index = static_cast<int>(style);
	return index >= 0 && index < 6 ? QCoreApplication::translate("LevelPatchCap", labels[index]) : QString();
}

bool parseLevelPatchCapStyle(const QString &id, LevelPatchCapStyle *style)
{
	const qsizetype index = levelPatchCapStyleIds().indexOf(id.trimmed().toLower());
	if (index < 0 || !style) {
		return false;
	}
	*style = static_cast<LevelPatchCapStyle>(index);
	return true;
}

QVector<int> levelPatchCapStyleWidths(LevelPatchCapStyle style)
{
	// NetRadiant Custom's Patch_makeCaps width checks.
	switch (style) {
	case LevelPatchCapStyle::Bevel:
	case LevelPatchCapStyle::InvertedBevel:
		return {3, 5};
	case LevelPatchCapStyle::Endcap:
	case LevelPatchCapStyle::InvertedEndcap:
		return {5};
	case LevelPatchCapStyle::Cylinder:
		return {9};
	case LevelPatchCapStyle::Radial:
		break;
	}
	return {};
}

bool prepareLevelPatchCaps(const LevelMapPatch &source, const LevelPatchCapRequest &request, LevelPatchCapResult *result, QString *error,
						   const std::function<bool()> &cancelled)
{
	if (error) {
		error->clear();
	}
	if (!result || !validateLevelPatch(source, error)) {
		return false;
	}
	if (request.boundaries.isEmpty() || request.boundaries.size() > 4 || (request.customCenter && request.boundaries.size() != 1) ||
		!std::isfinite(request.unitsPerTile) || request.unitsPerTile <= 0 ||
		(request.uv != LevelPatchCapUv::Planar && request.uv != LevelPatchCapUv::Boundary)) {
		return fail(error,
					QT_TRANSLATE_NOOP(
						"LevelPatchCap",
						"Choose one to four distinct boundaries, a positive texture scale, and at most one boundary for a custom center."));
	}
	const bool radial = request.style == LevelPatchCapStyle::Radial;
	if (static_cast<int>(request.style) < 0 || static_cast<int>(request.style) > static_cast<int>(LevelPatchCapStyle::Cylinder)) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "Choose a cap style."));
	}
	if (!radial && request.customCenter) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "Only radial caps take a custom center; the other styles close where their layout does."));
	}
	LevelPatchCapResult prepared;
	prepared.style = request.style;
	QSet<int> seen;
	for (auto edge : request.boundaries) {
		if (cancelled && cancelled()) {
			return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "Patch cap preparation cancelled."));
		}
		if (seen.contains(static_cast<int>(edge))) {
			return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "A cap boundary was selected more than once."));
		}
		seen.insert(static_cast<int>(edge));
		if (!(radial ? buildRadial(source, edge, request, &prepared, error) : buildSeam(source, edge, request, &prepared, error))) {
			if (error) {
				*error = QCoreApplication::translate("LevelPatchCap", "Cannot cap %1: %2").arg(levelPatchBoundaryId(edge), *error);
			}
			return false;
		}
	}
	if (request.uv == LevelPatchCapUv::Boundary && radial) {
		prepared.warnings << QCoreApplication::translate(
			"LevelPatchCap",
			"Boundary UVs are retained; seams and distortion converge at the cap center. Planar mapping avoids this for flat caps.");
	} else if (request.uv == LevelPatchCapUv::Boundary) {
		prepared.warnings << QCoreApplication::translate(
			"LevelPatchCap", "Boundary UVs are carried to the seam's other control points, so the texture can stretch where the seam folds.");
	}
	if (request.invert) {
		prepared.warnings << QCoreApplication::translate(
			"LevelPatchCap", "Cap facing is inverted from the source boundary. Review the visible side before applying.");
	}
	prepared.warnings << QCoreApplication::translate("LevelPatchCap",
													 "Caps are separate patch surfaces and do not create a solid brush. Compiler collision "
													 "and tessellation follow the chosen material and engine.");
	*result = std::move(prepared);
	return true;
}

bool capLevelMapPatch(LevelMapDocument *document, int patchId, const LevelPatchCapRequest &request, LevelPatchCapResult *result,
					  QString *error)
{
	if (!document || document->format != LevelMapFormat::Quake3Map) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "Open a Quake III-family map to add patch caps."));
	}
	const auto source = std::find_if(document->patches.cbegin(), document->patches.cend(), [&](const auto &p) { return p.id == patchId; });
	if (source == document->patches.cend()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "The source patch no longer exists."));
	}
	LevelPatchCapResult prepared;
	if (!prepareLevelPatchCaps(*source, request, &prepared, error)) {
		return false;
	}
	QVector<int> ids;
	if (!addLevelMapPatches(document, prepared.caps, source->entityId, &ids, error)) {
		return false;
	}
	for (int i = 0; i < ids.size(); ++i) {
		prepared.caps[i].id = ids[i];
	}
	if (result) {
		*result = std::move(prepared);
	}
	return true;
}

// Workflow reference: NRC radiant/patchmanip.cpp Scene_PatchDoCap_Selected
// and Patch_makeCaps at 449778b3 (GPL-2.0-or-later): every visible selected
// patch, its caps beside it and selected. Original implementation; see
// docs/CREDITS.md.
bool capLevelMapPatches(LevelMapDocument *document, const QVector<int> &patchIds, const LevelPatchCapRequest &request,
						LevelPatchCapResult *result, QString *error)
{
	LevelPatchCapResult staged;
	if (const auto guarded = guardLevelSceneEdit(
			document, error, [&](LevelMapDocument *candidate) { return capLevelMapPatches(candidate, patchIds, request, &staged, error); });
		guarded.has_value()) {
		if (*guarded && result) {
			*result = staged;
		}
		return *guarded;
	}
	if (error) {
		error->clear();
	}
	if (!document || document->format != LevelMapFormat::Quake3Map) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "Open a Quake III-family map to add patch caps."));
	}
	if (patchIds.isEmpty() || patchIds.size() > 256) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "Choose between 1 and 256 patches to cap."));
	}
	const QSet<int> wanted(patchIds.cbegin(), patchIds.cend());
	if (wanted.size() != patchIds.size()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "A patch was chosen more than once."));
	}
	const QSet<QString> hidden = levelMapHiddenObjects(*document);
	LevelPatchCapResult prepared;
	prepared.style = request.style;
	// Caps go in their source's entity: each owner's caps, by index.
	QMap<int, QVector<int>> byOwner;
	int found = 0;
	for (const auto &patch : std::as_const(document->patches)) {
		if (!wanted.contains(patch.id)) {
			continue;
		}
		++found;
		if (hidden.contains(levelMapSelectionRefId({LevelMapSelectionKind::QuakePatch, patch.id}))) {
			prepared.skipped << QCoreApplication::translate("LevelPatchCap", "Patch %1 is hidden.").arg(patch.id);
			continue;
		}
		LevelPatchCapResult one;
		QString problem;
		if (!prepareLevelPatchCaps(patch, request, &one, &problem)) {
			prepared.skipped << QCoreApplication::translate("LevelPatchCap", "Patch %1: %2").arg(patch.id).arg(problem);
			continue;
		}
		for (qsizetype i = 0; i < one.caps.size(); ++i) {
			byOwner[patch.entityId] << static_cast<int>(prepared.caps.size() + i);
		}
		prepared.caps += one.caps;
		prepared.sources += one.sources;
		prepared.boundaries += one.boundaries;
		prepared.centers += one.centers;
		prepared.closed += one.closed;
		prepared.reversed += one.reversed;
		if (prepared.warnings.isEmpty()) {
			prepared.warnings = one.warnings;
		}
	}
	if (found != wanted.size()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "The source patch no longer exists."));
	}
	if (prepared.caps.isEmpty()) {
		if (error) {
			*error = prepared.skipped.isEmpty() ? QCoreApplication::translate("LevelPatchCap", "Nothing to cap.")
												: prepared.skipped.join(QLatin1Char('\n'));
		}
		return false;
	}
	LevelMapDocument working = *document;
	QVector<int> ids(prepared.caps.size(), -1);
	int steps = 0;
	for (auto owner = byOwner.cbegin(); owner != byOwner.cend(); ++owner) {
		for (qsizetype start = 0; start < owner.value().size(); start += 64) {
			const QVector<int> chunk = owner.value().mid(start, 64);
			QVector<LevelMapPatch> caps;
			for (int index : chunk) {
				caps << prepared.caps[index];
			}
			QVector<int> added;
			if (!addLevelMapPatches(&working, caps, owner.key(), &added, error)) {
				return false;
			}
			for (qsizetype i = 0; i < chunk.size() && i < added.size(); ++i) {
				ids[chunk[i]] = added[i];
			}
			++steps;
		}
	}
	const int capped = static_cast<int>(QSet<int>(prepared.sources.cbegin(), prepared.sources.cend()).size());
	if (steps > 1 && !collapseLevelMapUndoSteps(&working, steps, QCoreApplication::translate("LevelPatchCap", "Cap %n patch(es)", nullptr, capped),
												QCoreApplication::translate("LevelPatchCap", "Remove the patch caps"), error)) {
		return false;
	}
	QVector<LevelMapSelectionRef> selection;
	for (int i = 0; i < ids.size(); ++i) {
		prepared.caps[i].id = ids[i];
		selection << LevelMapSelectionRef{LevelMapSelectionKind::QuakePatch, ids[i]};
	}
	setLevelMapSelection(&working, selection);
	*document = std::move(working);
	if (result) {
		*result = std::move(prepared);
	}
	return true;
}

QJsonObject levelPatchCapReportJson(const LevelPatchCapResult &result)
{
	QJsonArray caps, warnings, skipped;
	for (qsizetype i = 0; i < result.caps.size(); ++i) {
		const auto &cap = result.caps[i];
		const auto center = result.centers[i];
		caps << QJsonObject{{QStringLiteral("id"), cap.id},
							{QStringLiteral("source"), result.sources.value(i, -1)},
							{QStringLiteral("entity"), cap.entityId},
							{QStringLiteral("boundary"), levelPatchBoundaryId(result.boundaries[i])},
							{QStringLiteral("closed"), result.closed[i]},
							{QStringLiteral("reversed"), result.reversed[i]},
							{QStringLiteral("grid"), QJsonArray{cap.width, cap.height}},
							{QStringLiteral("center"), QJsonArray{center.x, center.y, center.z}},
							{QStringLiteral("material"), cap.textureName}};
	}
	for (const auto &warning : result.warnings) {
		warnings << warning;
	}
	for (const auto &reason : result.skipped) {
		skipped << reason;
	}
	return {{QStringLiteral("style"), levelPatchCapStyleId(result.style)},
			{QStringLiteral("caps"), caps},
			{QStringLiteral("skipped"), skipped},
			{QStringLiteral("warnings"), warnings}};
}
} // namespace vibestudio
