// Checks NetRadiant Custom's patch prefabs in the shared services and on the
// command line: the cylinder, square cylinder, classic cone, sphere, end cap
// and bevel land on the control points Radiant's Patch::ConstructPrefab lays
// out on every axis (rebuilt here from its index tables), and the straight
// cone and every other density on those of its Exact Cylinder, Cone and
// Sphere (rebuilt from their formulas), which start on the other side; new
// patches are
// mapped Natural, a plane Cap Texture, by the image's size at the default
// scale; the classic cone keeps full-width rings at any row count; a patch
// replaces the selected brushes and patches in one undo step, in the owner of
// the last one and the open group, leaving hidden objects and refusing
// locked ones; flat
// selections are widened by the grid; and `map add-patch` and `map add-shape`
// share all of it.

#include "core/level_document.h"
#include "core/level_map.h"
#include "core/level_patch.h"
#include "core/level_patch_tools.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_shapes.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace vibestudio;

namespace {

bool ok = true;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << label << ": " << qPrintable(detail) << std::endl;
	}
	return value;
}

bool near(double a, double b, double tolerance = 1e-9)
{
	return std::abs(a - b) <= tolerance;
}

bool samePoint(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

bool samePoints(const QVector<LevelMapVec3>& a, const QVector<LevelMapVec3>& b)
{
	if (a.size() != b.size()) {
		return false;
	}
	for (qsizetype i = 0; i < a.size(); ++i) {
		if (!samePoint(a.at(i), b.at(i))) {
			return false;
		}
	}
	return true;
}

QString pointText(const LevelMapVec3& p)
{
	return QStringLiteral("%1 %2 %3").arg(p.x).arg(p.y).arg(p.z);
}

double& component(LevelMapVec3& p, int index)
{
	return index == 0 ? p.x : (index == 1 ? p.y : p.z);
}

// NetRadiant Custom's prefab layout (radiant/patch.cpp Patch::ConstructPrefab
// at 449778b3), rebuilt from its index tables: vPos 0, 1 and 2 are the box's
// low corner, middle and high corner, and x, y and z permute with the axis.
QVector<LevelMapVec3> radiantPrefab(const QString& shape, int axis, const LevelMapVec3& low, const LevelMapVec3& high, int* width, int* height)
{
	const int x = axis == 2 ? 0 : (axis == 1 ? 2 : 1);
	const int y = axis == 2 ? 1 : (axis == 1 ? 0 : 2);
	const int z = axis;
	LevelMapVec3 middle {(low.x + high.x) / 2, (low.y + high.y) / 2, (low.z + high.z) / 2, true};
	LevelMapVec3 positions[3] = {low, middle, high};
	const auto at = [&](int ix, int iy, int iz) {
		LevelMapVec3 p {0, 0, 0, true};
		component(p, x) = component(positions[ix], x);
		component(p, y) = component(positions[iy], y);
		component(p, z) = component(positions[iz], z);
		return p;
	};
	const int cylinder[8][2] = {{0, 0}, {1, 0}, {2, 0}, {2, 1}, {2, 2}, {1, 2}, {0, 2}, {0, 1}};
	QVector<LevelMapVec3> points;
	if (shape == QStringLiteral("bevel") || shape == QStringLiteral("endcap")) {
		const int bevel[3][2] = {{0, 0}, {2, 0}, {2, 2}};
		const int endcap[5][2] = {{2, 0}, {2, 2}, {1, 2}, {0, 2}, {0, 0}};
		*width = shape == QStringLiteral("bevel") ? 3 : 5;
		*height = 3;
		for (int h = 0; h < 3; ++h) {
			for (int w = 0; w < *width; ++w) {
				const int* index = shape == QStringLiteral("bevel") ? bevel[w] : endcap[w];
				points << at(index[0], index[1], h);
			}
		}
		return points;
	}
	*width = 9;
	*height = shape == QStringLiteral("sphere") ? 5 : 3;
	points.resize(*width * *height);
	const int first = shape == QStringLiteral("square-cylinder") ? 0 : 1;
	const int firstRow = shape == QStringLiteral("sphere") ? 1 : 0;
	for (int h = 0; h < 3; ++h) {
		for (int w = 0; w < 8; ++w) {
			points[(firstRow + h) * 9 + first + w] = at(cylinder[w][0], cylinder[w][1], h);
		}
		if (first == 0) {
			points[(firstRow + h) * 9 + 8] = points[(firstRow + h) * 9];
		} else {
			points[(firstRow + h) * 9] = points[(firstRow + h) * 9 + 8];
		}
	}
	if (shape == QStringLiteral("classic-cone")) {
		for (int w = 0; w < 9; ++w) {
			points[2 * 9 + w] = at(1, 1, 2);
		}
	}
	if (shape == QStringLiteral("sphere")) {
		for (int w = 0; w < 9; ++w) {
			points[w] = at(1, 1, 0);
			points[4 * 9 + w] = at(1, 1, 2);
		}
	}
	return points;
}

// NetRadiant Custom's Exact Cylinder, Cone and Sphere (Patch::ConstructPrefab
// at 449778b3, EPatchPrefab::ExactCylinder, ExactCone and ExactSphere): column
// i at angle pi * i / n from the positive side of the first axis across, odd
// columns (and a sphere's odd rows) pushed out by 1 / cos of half a span.
QVector<LevelMapVec3> radiantExact(const QString& shape, int axis, const LevelMapVec3& low, const LevelMapVec3& high, int width, int height)
{
	const int x = axis == 2 ? 0 : (axis == 1 ? 2 : 1);
	const int y = axis == 2 ? 1 : (axis == 1 ? 0 : 2);
	const int z = axis;
	LevelMapVec3 lowCopy = low;
	LevelMapVec3 highCopy = high;
	LevelMapVec3 middle {(low.x + high.x) / 2, (low.y + high.y) / 2, (low.z + high.z) / 2, true};
	const double pi = 3.14159265358979323846;
	const int n = (width - 1) / 2;
	const int m = (height - 1) / 2;
	const double f = 1 / std::cos(pi / n);
	const double g = 1 / std::cos(pi / (2 * m));
	QVector<LevelMapVec3> points(width * height);
	for (int i = 0; i < width; ++i) {
		const double angle = pi * i / n;
		const double out = (i & 1) ? f : 1.0;
		for (int j = 0; j < height; ++j) {
			const double v = static_cast<double>(j) / (height - 1);
			double radius = 1.0;
			double along = component(lowCopy, z) + (component(highCopy, z) - component(lowCopy, z)) * v;
			if (shape == QStringLiteral("cone")) {
				radius = 1.0 - v;
			} else if (shape == QStringLiteral("sphere")) {
				const double angle2 = pi * j / (2 * m);
				const double outRow = (j & 1) ? g : 1.0;
				radius = std::sin(angle2) * outRow;
				along = component(middle, z) + (component(highCopy, z) - component(middle, z)) * -std::cos(angle2) * outRow;
			}
			LevelMapVec3 p {0, 0, 0, true};
			component(p, x) = component(middle, x) + (component(highCopy, x) - component(middle, x)) * radius * std::cos(angle) * out;
			component(p, y) = component(middle, y) + (component(highCopy, y) - component(middle, y)) * radius * std::sin(angle) * out;
			component(p, z) = along;
			points[j * width + i] = p;
		}
	}
	return points;
}

void checkExactLayout()
{
	const LevelMapVec3 low {-64, -32, 16, true};
	const LevelMapVec3 high {64, 96, 208, true};
	struct Case {
		const char* shape;
		int segments;
		int bands;
		int rows;
		LevelPatchRingStart start;
	};
	// The straight cone at Radiant's own density, other densities, and the
	// four-segment cylinder asked to start as the Exact one does.
	const Case cases[] = {{"cone", 4, 2, 3, LevelPatchRingStart::Auto}, {"cone", 6, 2, 5, LevelPatchRingStart::Auto},
		{"cylinder", 6, 2, 3, LevelPatchRingStart::Auto}, {"cylinder", 3, 2, 5, LevelPatchRingStart::Auto},
		{"cylinder", 4, 2, 3, LevelPatchRingStart::Exact}, {"sphere", 6, 3, 0, LevelPatchRingStart::Auto},
		{"sphere", 4, 3, 0, LevelPatchRingStart::Auto}, {"sphere", 4, 2, 0, LevelPatchRingStart::Exact}};
	for (const Case& entry : cases) {
		for (int axis = 0; axis < 3; ++axis) {
			LevelPatchCreateRequest request;
			request.shape = QString::fromLatin1(entry.shape);
			request.axis = axis;
			request.segments = entry.segments;
			request.bands = entry.bands;
			request.rows = entry.rows > 0 ? entry.rows : 3;
			request.ringStart = entry.start;
			request.center = {(low.x + high.x) / 2, (low.y + high.y) / 2, (low.z + high.z) / 2, true};
			request.size = {high.x - low.x, high.y - low.y, high.z - low.z, true};
			LevelMapPatch patch;
			QString error;
			const QString label = QStringLiteral("%1 %2x%3 axis %4").arg(QLatin1String(entry.shape)).arg(entry.segments).arg(entry.bands).arg(axis);
			if (!expect(createLevelPatch(request, &patch, &error), "exact shape builds", label + QLatin1Char(' ') + error)) {
				continue;
			}
			const QVector<LevelMapVec3> expected = radiantExact(request.shape, axis, low, high, patch.width, patch.height);
			QString detail;
			bool same = expected.size() == patch.controlPoints.size();
			for (int i = 0; same && i < expected.size(); ++i) {
				const LevelMapVec3& a = patch.controlPoints.at(i);
				const LevelMapVec3& b = expected.at(i);
				if (!near(a.x, b.x, 1e-6) || !near(a.y, b.y, 1e-6) || !near(a.z, b.z, 1e-6)) {
					same = false;
					detail = QStringLiteral("%1 point %2: %3 instead of %4").arg(label).arg(i).arg(pointText(a), pointText(b));
				}
			}
			expect(same, "the shape lands on NetRadiant Custom's Exact control points, starting at angle 0", detail.isEmpty() ? label : detail);
		}
	}
}

void checkRadiantLayout()
{
	const LevelMapVec3 low {-64, -32, 16, true};
	const LevelMapVec3 high {64, 96, 208, true};
	for (const QString& shape : {QStringLiteral("cylinder"), QStringLiteral("square-cylinder"), QStringLiteral("classic-cone"),
			 QStringLiteral("sphere"), QStringLiteral("endcap"), QStringLiteral("bevel")}) {
		for (int axis = 0; axis < 3; ++axis) {
			LevelShapeRequest request;
			request.shape = QStringLiteral("patch-") + shape;
			request.mins = low;
			request.maxs = high;
			request.axis = axis;
			request.sides = 4;
			request.bands = 2;
			request.texture = QStringLiteral("studio/curve");
			LevelPatchCreateRequest create;
			LevelMapPatch patch;
			QString error;
			if (!expect(levelShapePatchRequest(request, &create, &error) && createLevelPatch(create, &patch, &error), "prefab builds", shape + error)) {
				continue;
			}
			int width = 0, height = 0;
			const QVector<LevelMapVec3> expected = radiantPrefab(shape, axis, low, high, &width, &height);
			bool same = patch.width == width && patch.height == height && patch.controlPoints.size() == expected.size();
			QString detail;
			for (int i = 0; same && i < expected.size(); ++i) {
				if (!samePoint(patch.controlPoints.at(i), expected.at(i))) {
					same = false;
					detail = QStringLiteral("%1 axis %2 point %3: %4 instead of %5").arg(shape).arg(axis).arg(i).arg(pointText(patch.controlPoints.at(i)),
						pointText(expected.at(i)));
				}
			}
			expect(same, "the prefab lands on NetRadiant Custom's control points", detail.isEmpty() ? shape : detail);
		}
	}
}

void checkMapping()
{
	QString error;
	// Natural: a square cylinder's sides are 64 units a step and its rows 96
	// apart; a 128 by 64 image at 0.5 units a texel repeats every 64 by 32.
	LevelPatchCreateRequest request;
	request.shape = QStringLiteral("square-cylinder");
	request.size = {128, 128, 192, true};
	request.mapping = LevelPatchCreateMapping::Natural;
	request.imageSize = QSize(128, 64);
	LevelMapPatch square;
	if (expect(createLevelPatch(request, &square, &error), "natural square cylinder", error)) {
		bool natural = true;
		for (int row = 0; row < square.height; ++row) {
			for (int column = 0; column < square.width; ++column) {
				const int i = row * square.width + column;
				natural &= near(square.controlU.at(i), column) && near(square.controlV.at(i), -3.0 * row);
			}
		}
		expect(natural, "Natural spreads U along the columns and V down the rows by the image's size at half a unit a texel");
	}
	// Each mapping is the shared texture operation on the stretched patch.
	for (const QString& shape : levelPatchShapeIds()) {
		LevelPatchCreateRequest stretch;
		stretch.shape = shape;
		stretch.axis = 1;
		stretch.size = {96, 160, 128, true};
		LevelMapPatch flat;
		expect(createLevelPatch(stretch, &flat, &error), "stretched shape", shape + error);
		expect(near(flat.controlU.last(), 1) && near(flat.controlV.last(), 1), "Stretch spreads one repeat over the grid", shape);
		LevelPatchCreateRequest radiant = stretch;
		radiant.mapping = LevelPatchCreateMapping::Radiant;
		radiant.imageSize = QSize(256, 128);
		LevelMapPatch mapped;
		LevelMapPatch expected = flat;
		const bool plane = shape == QStringLiteral("plane");
		expect(createLevelPatch(radiant, &mapped, &error)
				&& (plane ? planarLevelPatchTexture(&expected, 128, 64, &error) : naturalLevelPatchTexture(&expected, 128, 64, &error))
				&& mapped.controlU == expected.controlU && mapped.controlV == expected.controlV,
			"Radiant maps a plane with Cap Texture and the rest Natural", shape + error);
		LevelPatchCreateRequest unknown = radiant;
		unknown.imageSize = {};
		LevelMapPatch guessed;
		LevelMapPatch sixtyFour = flat;
		expect(createLevelPatch(unknown, &guessed, &error)
				&& (plane ? planarLevelPatchTexture(&sixtyFour, 32, 32, &error) : naturalLevelPatchTexture(&sixtyFour, 32, 32, &error))
				&& guessed.controlU == sixtyFour.controlU && guessed.controlV == sixtyFour.controlV,
			"without an image the patch is mapped as 64 by 64 texels", shape);
		LevelPatchCreateRequest cap = stretch;
		cap.mapping = LevelPatchCreateMapping::Cap;
		cap.imageSize = QSize(64, 64);
		cap.textureScale = 1;
		LevelMapPatch capped;
		LevelMapPatch planar = flat;
		expect(createLevelPatch(cap, &capped, &error) && planarLevelPatchTexture(&planar, 64, 64, &error) && capped.controlU == planar.controlU,
			"Cap Texture follows the scale asked for", shape);
	}
	LevelPatchCreateRequest bad;
	bad.textureScale = 0;
	LevelMapPatch ignored;
	expect(!createLevelPatch(bad, &ignored, &error) && !error.isEmpty(), "a texture scale of zero is refused");
	LevelPatchCreateMapping parsed = LevelPatchCreateMapping::Stretch;
	expect(parseLevelPatchCreateMapping(QStringLiteral(" Natural "), &parsed) && parsed == LevelPatchCreateMapping::Natural
			&& levelPatchCreateMappingId(LevelPatchCreateMapping::Radiant) == QStringLiteral("radiant") && !parseLevelPatchCreateMapping(QStringLiteral("planar"), &parsed),
		"mapping ids parse and print");
}

void checkClassicCone()
{
	// Five rows: on-curve rows at 1 - v * v of the radius, middle controls at
	// 1 - a * b, and the last row the tip.
	LevelPatchCreateRequest request;
	request.shape = QStringLiteral("classic-cone");
	request.rows = 5;
	request.size = {128, 128, 128, true};
	LevelMapPatch cone;
	QString error;
	if (!expect(createLevelPatch(request, &cone, &error), "five-row classic cone", error)) {
		return;
	}
	const double expected[5] = {1.0, 1.0 - 0.0 * 0.5, 1.0 - 0.25, 1.0 - 0.5 * 1.0, 0.0};
	bool profile = cone.width == 9 && cone.height == 5;
	for (int row = 0; profile && row < 5; ++row) {
		// Column 0 sits on the ring's start, half a turn round: -X.
		const LevelMapVec3 p = cone.controlPoints.at(row * 9);
		profile &= near(p.x, -64 * expected[row]) && near(p.y, 0) && near(p.z, -64 + 32 * row);
	}
	expect(profile, "the classic cone stays full width and closes at its last row", pointText(cone.controlPoints.at(9)));
	LevelPatchCreateRequest straight = request;
	straight.shape = QStringLiteral("cone");
	LevelMapPatch exact;
	expect(createLevelPatch(straight, &exact, &error) && near(exact.controlPoints.at(2 * 9).x, 32),
		"the straight cone, NRC's Exact Cone, starts on the positive side with its middle row at half the radius");
}

LevelMapDocument fixture()
{
	LevelMapDocument document;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake3");
	create.starterRoom = false;
	QString error;
	expect(createLevelMap(create, &document, &error), "map", error);
	// Brush 0 and patch 0 in the world, brush 1 in a func_group, brush 2 on a
	// locked layer.
	expect(addLevelMapBoxBrush(&document, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("studio/box"), nullptr, &error), "brush 0", error);
	LevelPatchCreateRequest plane;
	plane.center = {128, 32, 32, true};
	plane.size = {64, 64, 64, true};
	plane.texture = QStringLiteral("textures/studio/flat");
	LevelMapPatch patch;
	expect(createLevelPatch(plane, &patch, &error) && addLevelMapPatch(&document, patch, nullptr, &error), "patch 0", error);
	expect(addLevelMapBoxBrush(&document, {256, 0, 0, true}, {320, 64, 64, true}, QStringLiteral("studio/box"), nullptr, &error)
			&& setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 1}}, &error), "brush 1", error);
	int group = -1;
	expect(tieLevelMapSelectionToEntity(&document, QStringLiteral("func_group"), {}, &group, &error), "func_group", error);
	expect(addLevelMapBoxBrush(&document, {512, 0, 0, true}, {576, 64, 64, true}, QStringLiteral("studio/box"), nullptr, &error), "brush 2", error);
	QString layer;
	expect(createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
			&& assignLevelSceneObjects(&document, layer, {QStringLiteral("brush:2")}, &error) && setLevelSceneLocked(&document, layer, true, &error),
		"locked layer", error);
	setLevelMapSelection(&document, {});
	return document;
}

const LevelMapBrush* brushNamed(const LevelMapDocument& document, int id)
{
	for (const LevelMapBrush& brush : document.brushes) {
		if (brush.id == id) {
			return &brush;
		}
	}
	return nullptr;
}

void checkReplace()
{
	LevelMapDocument document = fixture();
	QString error;
	const int func = brushNamed(document, 1) ? brushNamed(document, 1)->entityId : -1;
	// Brush 0 and patch 0: their bounds, widened nowhere, and one step.
	expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakePatch, 0}}, &error), "select", error);
	LevelShapeRequest request;
	request.shape = QStringLiteral("patch-cylinder");
	request.sides = 4;
	request.texture = QStringLiteral("studio/pipe");
	request.patchImageSize = QSize(64, 64);
	expect(levelMapPatchShapeBounds(document, 8, &request.mins, &request.maxs) && near(request.mins.x, 0) && near(request.maxs.x, 160)
			&& near(request.mins.z, 0) && near(request.maxs.z, 64),
		"the bounds are the selection's", pointText(request.mins) + QLatin1String(" / ") + pointText(request.maxs));
	const QByteArray before = serializeLevelMap(document).bytes;
	const auto depth = document.undoStack.size();
	int patchId = -1;
	if (expect(replaceLevelMapSelectionWithPatchShape(&document, request, &patchId, &error), "replace with a patch cylinder", error)) {
		const auto found = std::find_if(document.patches.cbegin(), document.patches.cend(), [patchId](const LevelMapPatch& each) { return each.id == patchId; });
		expect(document.undoStack.size() == depth + 1 && !brushNamed(document, 0) && found != document.patches.cend() && document.patches.size() == 1
				&& document.selection == QVector<LevelMapSelectionRef> {{LevelMapSelectionKind::QuakePatch, patchId}}
				&& found->width == 9 && found->textureName == QStringLiteral("studio/pipe") && !near(found->controlU.at(1), 0.125),
			"the brush and patch become one Natural-mapped patch, selected, in one undo step");
		expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == before && document.undoStack.size() == depth,
			"undo brings the brush and patch back exactly", error);
		expect(redoLevelMapEdit(&document, &error) && document.patches.size() == 1 && !brushNamed(document, 0), "redo replaces them again", error);
		expect(undoLevelMapEdit(&document, &error), "undo again", error);
	}
	// A brush in a brush entity: the patch joins that entity.
	expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 1}}, &error), "select the func_group brush", error);
	request.shape = QStringLiteral("patch-bevel");
	expect(levelMapPatchShapeBounds(document, 8, &request.mins, &request.maxs), "func_group bounds");
	if (expect(replaceLevelMapSelectionWithPatchShape(&document, request, &patchId, &error), "replace the func_group brush", error)) {
		const auto found = std::find_if(document.patches.cbegin(), document.patches.cend(), [patchId](const LevelMapPatch& each) { return each.id == patchId; });
		expect(found != document.patches.cend() && found->entityId == func && !brushNamed(document, 1),
			"the patch takes the brush's place in its brush entity", QString::number(found != document.patches.cend() ? found->entityId : -2));
		expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == before, "undo restores the func_group", error);
	}
	// A locked brush refuses, changing nothing.
	expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 2}}, &error), "select the locked brush", error);
	expect(levelMapPatchShapeBounds(document, 8, &request.mins, &request.maxs), "locked bounds");
	const auto lockedDepth = document.undoStack.size();
	expect(!replaceLevelMapSelectionWithPatchShape(&document, request, &patchId, &error) && !error.isEmpty() && serializeLevelMap(document).bytes == before
			&& document.undoStack.size() == lockedDepth && brushNamed(document, 2),
		"a locked brush is not replaced and the map is unchanged", error);
	// A hidden brush in the selection stays where it is.
	document.hiddenObjects = {QStringLiteral("brush:0")};
	expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakePatch, 0}}, &error), "select hidden", error);
	expect(levelMapPatchShapeBounds(document, 8, &request.mins, &request.maxs), "hidden bounds");
	QVector<LevelMapSelectionRef> taken;
	if (expect(replaceLevelMapSelectionWithPatchShape(&document, request, &patchId, &error, &taken), "replace beside a hidden brush", error)) {
		expect(brushNamed(document, 0) && document.patches.size() == 1 && document.patches.first().id == patchId
				&& taken == QVector<LevelMapSelectionRef> {{LevelMapSelectionKind::QuakePatch, 0}},
			"the hidden brush is left alone and not reported as replaced");
		expect(undoLevelMapEdit(&document, &error), "undo the hidden case", error);
	}
	document.hiddenObjects.clear();
	// Inside an open group the patch joins that group, in the same step.
	{
		QString group;
		expect(createLevelSceneNode(&document, LevelSceneNodeKind::Group, QStringLiteral("Room"), {}, &group, &error)
				&& assignLevelSceneObjects(&document, group, {QStringLiteral("brush:0")}, &error) && openLevelSceneGroup(&document, group, &error),
			"open group", error);
		const QByteArray grouped = serializeLevelMap(document).bytes;
		const auto groupDepth = document.undoStack.size();
		expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error), "select inside the group", error);
		expect(levelMapPatchShapeBounds(document, 8, &request.mins, &request.maxs), "group bounds");
		if (expect(replaceLevelMapSelectionWithPatchShape(&document, request, &patchId, &error), "replace inside the open group", error)) {
			QStringList members;
			for (const LevelSceneNode& node : std::as_const(document.scene.nodes)) {
				if (node.id == group) {
					members = node.objects;
				}
			}
			expect(document.undoStack.size() == groupDepth + 1 && !brushNamed(document, 0)
					&& members.contains(levelMapSelectionRefId({LevelMapSelectionKind::QuakePatch, patchId})),
				"the patch takes the brush's place in the open group in one step", members.join(QLatin1Char(',')));
			expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == grouped, "undo restores the group's brush", error);
		}
		closeLevelSceneGroup(&document, true, &error);
	}
	// A flat selection, the patch plane, is widened by the grid.
	expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakePatch, 0}}, &error), "select the plane", error);
	LevelMapVec3 low, high;
	expect(levelMapPatchShapeBounds(document, 16, &low, &high) && near(low.z, 32 - 16) && near(high.z, 32 + 16) && near(low.x, 96)
			&& near(high.x, 160),
		"a flat side is widened by the grid either side of its middle", pointText(low) + QLatin1String(" / ") + pointText(high));
	// Nothing selected: the patch is simply added, and no bounds come back.
	setLevelMapSelection(&document, {});
	expect(!levelMapPatchShapeBounds(document, 8, &low, &high), "no selection has no bounds");
	const auto added = document.patches.size();
	request.mins = {0, 0, 0, true};
	request.maxs = {64, 64, 64, true};
	expect(replaceLevelMapSelectionWithPatchShape(&document, request, &patchId, &error) && document.patches.size() == added + 1
			&& document.brushes.size() == 3,
		"with nothing selected the patch is added", error);
	LevelMapDocument quake;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake");
	create.starterRoom = false;
	createLevelMap(create, &quake, &error);
	expect(!replaceLevelMapSelectionWithPatchShape(&quake, request, &patchId, &error), "Quake maps take no patches");
}

QString writeMap(const QDir& root, const QString& name, const LevelMapDocument& document)
{
	const QString path = root.filePath(name);
	QFile file(path);
	expect(file.open(QIODevice::WriteOnly) && file.write(serializeLevelMap(document).bytes) > 0, "write fixture", path);
	return path;
}

void checkCli(const QString& binary)
{
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "temporary directory")) {
		return;
	}
	const QDir root(temp.path());
	const QString source = writeMap(root, QStringLiteral("source.map"), fixture());
	const auto run = [&](const QStringList& args, int expected, QJsonObject* json = nullptr) {
		QProcess process;
		process.start(binary, QStringList {QStringLiteral("--cli"), QStringLiteral("map")} + args);
		const bool finished = process.waitForFinished(60000);
		const QByteArray out = process.readAllStandardOutput();
		expect(finished && process.exitCode() == expected, "CLI exit code", args.join(QLatin1Char(' ')) + QLatin1Char(' ') + QString::fromUtf8(process.readAllStandardError()));
		if (json) {
			*json = QJsonDocument::fromJson(out).object();
		}
	};
	const auto load = [](const QString& path) {
		LevelMapDocument document;
		QString error;
		expect(loadLevelMap({path, {}, {}}, &document, &error), "reload CLI output", error);
		return document;
	};
	// map add-patch: the classic cone, mapped Natural by the image's size,
	// matches the core service.
	const QString cone = root.filePath(QStringLiteral("cone.map"));
	QJsonObject json;
	run({QStringLiteral("add-patch"), source, QStringLiteral("--shape"), QStringLiteral("classic-cone"), QStringLiteral("--axis"), QStringLiteral("y"),
			QStringLiteral("--size"), QStringLiteral("128,96,64"), QStringLiteral("--texture"), QStringLiteral("textures/studio/cone"),
			QStringLiteral("--texture-size"), QStringLiteral("128,64"), QStringLiteral("--output"), cone, QStringLiteral("--json")},
		0, &json);
	LevelPatchCreateRequest request;
	request.shape = QStringLiteral("classic-cone");
	request.axis = 1;
	request.size = {128, 96, 64, true};
	request.texture = QStringLiteral("textures/studio/cone");
	request.mapping = LevelPatchCreateMapping::Radiant;
	request.imageSize = QSize(128, 64);
	LevelMapPatch expected;
	QString error;
	createLevelPatch(request, &expected, &error);
	const LevelMapDocument coned = load(cone);
	// The map reader keeps patch texture coordinates to float precision.
	const auto sameNumbers = [](const QVector<double>& a, const QVector<double>& b) {
		bool same = a.size() == b.size();
		for (qsizetype i = 0; same && i < a.size(); ++i) {
			same = near(a.at(i), b.at(i), 1e-5);
		}
		return same;
	};
	const bool conePoints = !coned.patches.isEmpty() && samePoints(coned.patches.last().controlPoints, expected.controlPoints);
	const bool coneUv = !coned.patches.isEmpty() && sameNumbers(coned.patches.last().controlU, expected.controlU)
		&& sameNumbers(coned.patches.last().controlV, expected.controlV);
	expect(conePoints && coneUv && json.value(QStringLiteral("mapping")).toString() == QStringLiteral("radiant"),
		"map add-patch builds the classic cone and maps it as the service does",
		QStringLiteral("points %1 uv %2 mapping %3 expected %4").arg(conePoints).arg(coneUv).arg(json.value(QStringLiteral("mapping")).toString(), error));
	run({QStringLiteral("add-patch"), source, QStringLiteral("--shape"), QStringLiteral("cylinder"), QStringLiteral("--mapping"), QStringLiteral("stretch"),
			QStringLiteral("--texture"), QStringLiteral("textures/studio/cone"), QStringLiteral("--output"), cone, QStringLiteral("--overwrite"), QStringLiteral("--json")},
		0, &json);
	const LevelMapDocument stretched = load(cone);
	expect(!stretched.patches.isEmpty() && near(stretched.patches.last().controlU.last(), 1) && near(stretched.patches.last().controlV.last(), 1)
			&& json.value(QStringLiteral("mapping")).toString() == QStringLiteral("stretch"),
		"--mapping stretch keeps one repeat");
	run({QStringLiteral("add-patch"), source, QStringLiteral("--mapping"), QStringLiteral("planar"), QStringLiteral("--texture"), QStringLiteral("x"),
			QStringLiteral("--output"), cone, QStringLiteral("--overwrite")},
		2);
	run({QStringLiteral("add-patch"), source, QStringLiteral("--texture-size"), QStringLiteral("0,64"), QStringLiteral("--texture"), QStringLiteral("x"),
			QStringLiteral("--output"), cone, QStringLiteral("--overwrite")},
		2);
	// map add-shape: a patch cylinder replaces a brush and a patch, as the
	// service does, and reports what it replaced.
	const QString piped = root.filePath(QStringLiteral("piped.map"));
	run({QStringLiteral("add-shape"), source, QStringLiteral("--shape"), QStringLiteral("patch-cylinder"), QStringLiteral("--sides"), QStringLiteral("4"),
			QStringLiteral("--object"), QStringLiteral("brush:0"), QStringLiteral("--object"), QStringLiteral("patch:0"), QStringLiteral("--texture"),
			QStringLiteral("studio/pipe"), QStringLiteral("--texture-size"), QStringLiteral("64,64"), QStringLiteral("--output"), piped, QStringLiteral("--json")},
		0, &json);
	LevelMapDocument document = load(source);
	setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakePatch, 0}});
	LevelShapeRequest shape;
	shape.shape = QStringLiteral("patch-cylinder");
	shape.sides = 4;
	shape.texture = QStringLiteral("studio/pipe");
	shape.patchImageSize = QSize(64, 64);
	levelMapPatchShapeBounds(document, 8, &shape.mins, &shape.maxs);
	int patchId = -1;
	replaceLevelMapSelectionWithPatchShape(&document, shape, &patchId, &error);
	expect(load(piped).patches.size() == 1 && serializeLevelMap(load(piped)).bytes == serializeLevelMap(document).bytes
			&& json.value(QStringLiteral("replaced")).toArray().size() == 2,
		"map add-shape replaces a brush and a patch exactly as the service does", QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
	// The patch plane's control grid, and options that do not apply.
	const QString planePath = root.filePath(QStringLiteral("plane.map"));
	run({QStringLiteral("add-shape"), source, QStringLiteral("--shape"), QStringLiteral("patch-plane"), QStringLiteral("--columns"), QStringLiteral("5"),
			QStringLiteral("--rows"), QStringLiteral("7"), QStringLiteral("--mins"), QStringLiteral("0,0,0"), QStringLiteral("--maxs"), QStringLiteral("128,64,32"),
			QStringLiteral("--texture"), QStringLiteral("studio/flat"), QStringLiteral("--output"), planePath, QStringLiteral("--json")},
		0);
	const LevelMapDocument planed = load(planePath);
	expect(planed.patches.size() == 2 && planed.patches.last().width == 5 && planed.patches.last().height == 7 && near(planed.patches.last().mins.z, 32),
		"map add-shape builds a patch plane of the asked grid across the box's top");
	run({QStringLiteral("add-shape"), source, QStringLiteral("--shape"), QStringLiteral("cylinder"), QStringLiteral("--mapping"), QStringLiteral("natural"),
			QStringLiteral("--mins"), QStringLiteral("0,0,0"), QStringLiteral("--maxs"), QStringLiteral("64,64,64"), QStringLiteral("--texture"),
			QStringLiteral("studio/box"), QStringLiteral("--output"), planePath, QStringLiteral("--overwrite")},
		2);
	run({QStringLiteral("add-shape"), source, QStringLiteral("--shape"), QStringLiteral("patch-cylinder"), QStringLiteral("--columns"), QStringLiteral("5"),
			QStringLiteral("--mins"), QStringLiteral("0,0,0"), QStringLiteral("--maxs"), QStringLiteral("64,64,64"), QStringLiteral("--texture"),
			QStringLiteral("studio/box"), QStringLiteral("--output"), planePath, QStringLiteral("--overwrite")},
		2);
	run({QStringLiteral("add-shape"), source, QStringLiteral("--shape"), QStringLiteral("patch-sphere"), QStringLiteral("--object"), QStringLiteral("brush:99"),
			QStringLiteral("--texture"), QStringLiteral("studio/box"), QStringLiteral("--output"), planePath, QStringLiteral("--overwrite")},
		1);
	run({QStringLiteral("add-shape"), source, QStringLiteral("--shape"), QStringLiteral("stairs"), QStringLiteral("--object"), QStringLiteral("patch:0"),
			QStringLiteral("--texture"), QStringLiteral("studio/box"), QStringLiteral("--output"), planePath, QStringLiteral("--overwrite")},
		2);
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	checkRadiantLayout();
	checkExactLayout();
	checkMapping();
	checkClassicCone();
	checkReplace();
	if (argc > 1) {
		checkCli(QString::fromLocal8Bit(argv[1]));
	}
	if (!ok) {
		return 1;
	}
	std::cout << "level patch prefab smoke test passed" << std::endl;
	return 0;
}
