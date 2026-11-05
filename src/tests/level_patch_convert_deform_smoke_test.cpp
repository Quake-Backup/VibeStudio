// Core and command-line checks of TrenchBroom's Convert Selection to Patches
// and NetRadiant Custom's Deform on Quake III maps. Converting a box replaces
// it with six flat 3x3 patches that lie on its faces, face out of it and keep
// each face's texture mapping at every control point (by the classic Quake
// formula, worked out here on its own), as one undo step that undoes and
// redoes exactly; a picked face keeps its brush; an octagon becomes three
// patches covering its area; brush-primitive faces keep their repeats
// whatever the image size; grids of other densities, hidden brushes and
// locked brushes behave. Deform draws the same offsets from the same seed on
// every platform (the first draw of seed 5489 is 52 of 64), moves only the
// chosen axis, keeps texture coordinates, skips hidden patches and undoes
// exactly. map convert-patches and map deform-patch write what the services
// make and refuse bad options with exit code 2.

#include "core/level_document.h"
#include "core/level_map.h"
#include "core/level_patch.h"
#include "core/level_patch_convert.h"
#include "core/level_patch_tools.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_shapes.h"
#include "core/level_texture_mapping.h"
#include "core/level_visibility.h"
#include "core/map_geometry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

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

bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }

LevelMapVec3 sub(const LevelMapVec3& a, const LevelMapVec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, true}; }
LevelMapVec3 cross(const LevelMapVec3& a, const LevelMapVec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, true}; }
double dot(const LevelMapVec3& a, const LevelMapVec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

LevelMapDocument quake3Map()
{
	LevelMapDocument document;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake3");
	create.starterRoom = false;
	QString error;
	expect(createLevelMap(create, &document, &error), "create a Quake III map", error);
	return document;
}

const LevelMapPatch* patchById(const LevelMapDocument& document, int id)
{
	for (const LevelMapPatch& patch : document.patches) {
		if (patch.id == id) {
			return &patch;
		}
	}
	return nullptr;
}

// QuakeEd's texture axes for an axial face, by its normal's largest part.
void classicAxes(const LevelMapVec3& normal, LevelMapVec3* u, LevelMapVec3* v)
{
	const double ax = std::abs(normal.x), ay = std::abs(normal.y), az = std::abs(normal.z);
	if (az >= ax && az >= ay) {
		*u = {1, 0, 0, true};
		*v = {0, -1, 0, true};
	} else if (ax >= ay) {
		*u = {0, 1, 0, true};
		*v = {0, 0, -1, true};
	} else {
		*u = {1, 0, 0, true};
		*v = {0, 0, -1, true};
	}
}

void checkConvertBox()
{
	LevelMapDocument document = quake3Map();
	QString error;
	int brushId = -1;
	expect(addLevelMapBoxBrush(&document, {-64, -32, 0, true}, {64, 32, 96, true}, QStringLiteral("studio/grid"), &brushId, &error), "box", error);
	const LevelMapBrush box = document.brushes.first();
	const QByteArray before = serializeLevelMap(document).bytes;
	const auto depth = document.undoStack.size();
	LevelPatchConvertRequest request;
	request.imageSize = [](const QString& material) { return material.contains(QStringLiteral("grid")) ? QSize(128, 64) : QSize(); };
	LevelPatchConvertReport report;
	expect(convertLevelMapFacesToPatches(&document, {}, {brushId}, request, &report, &error) && report.patchIds.size() == 6 && report.faces == 6
			&& report.brushesRemoved == 1 && document.brushes.isEmpty() && document.patches.size() == 6 && document.undoStack.size() == depth + 1
			&& document.selection.size() == 6 && report.unsizedMaterials.isEmpty(),
		"a box becomes six patches in one step, replacing it and selected", error);
	const LevelMapVec3 centre {0, 0, 48, true};
	for (const int id : std::as_const(report.patchIds)) {
		const LevelMapPatch* patch = patchById(document, id);
		if (!expect(patch && patch->width == 3 && patch->height == 3 && patch->textureName == QStringLiteral("studio/grid"), "a 3x3 patch in the face's material")) {
			continue;
		}
		// Flat, on a face of the box, facing out: columns crossed with rows.
		const LevelMapVec3 a = patch->controlPoints[0], b = patch->controlPoints[2], d = patch->controlPoints[6];
		const LevelMapVec3 normal = cross(sub(b, a), sub(d, a));
		const LevelMapVec3 middle = patch->controlPoints[4];
		expect(dot(normal, sub(middle, centre)) > 0, "the patch faces out of the brush");
		bool flat = true;
		for (const LevelMapVec3& point : patch->controlPoints) {
			flat = flat && near(dot(normal, sub(point, a)), 0, 1e-6);
		}
		expect(flat && near(middle.x, (a.x + patch->controlPoints[8].x) / 2) && near(middle.y, (a.y + patch->controlPoints[8].y) / 2)
				&& near(middle.z, (a.z + patch->controlPoints[8].z) / 2),
			"the patch is flat with its middle control halfway");
		// The face it came from: the one whose plane holds the patch.
		const LevelMapBrushFace* face = nullptr;
		for (const LevelMapBrushFace& candidate : box.faces) {
			const MapPlane plane = planeFromPoints(candidate.p0, candidate.p1, candidate.p2);
			if (std::abs(planeDistanceToPoint(plane, a)) < 1e-6 && std::abs(planeDistanceToPoint(plane, middle)) < 1e-6
				&& std::abs(planeDistanceToPoint(plane, patch->controlPoints[8])) < 1e-6) {
				face = &candidate;
			}
		}
		if (!expect(face && (face->explicitTextureAxes || face->explicitTextureMatrix || face->rotation == 0), "the patch lies on a face of the box")) {
			continue;
		}
		LevelMapVec3 u;
		LevelMapVec3 v;
		double shiftU = face->shiftX;
		double shiftV = face->shiftY;
		if (face->explicitTextureAxes) {
			// Valve 220: the face's own axes and offsets.
			u = face->uAxis;
			v = face->vAxis;
			shiftU = face->uOffset;
			shiftV = face->vOffset;
		} else {
			const double length = std::sqrt(dot(normal, normal));
			classicAxes({normal.x / length, normal.y / length, normal.z / length, true}, &u, &v);
		}
		const double scaleX = face->scaleX == 0 ? 1 : face->scaleX, scaleY = face->scaleY == 0 ? 1 : face->scaleY;
		const LevelTextureProjection projection = levelTextureProjection(*face);
		bool mapped = true;
		for (qsizetype i = 0; i < patch->controlPoints.size(); ++i) {
			if (face->explicitTextureMatrix) {
				// Brush primitives count repeats already.
				const QPointF at = projection.at(patch->controlPoints[i]);
				mapped = mapped && near(patch->controlU[i], at.x()) && near(patch->controlV[i], at.y());
				continue;
			}
			const double texelU = dot(patch->controlPoints[i], u) / scaleX + shiftU;
			const double texelV = dot(patch->controlPoints[i], v) / scaleY + shiftV;
			mapped = mapped && near(patch->controlU[i], texelU / 128) && near(patch->controlV[i], texelV / 64);
		}
		expect(mapped, "each control keeps the face's mapping, in repeats of the 128 by 64 image");
	}
	const QByteArray after = serializeLevelMap(document).bytes;
	expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == before && redoLevelMapEdit(&document, &error)
			&& serializeLevelMap(document).bytes == after,
		"undo restores the box exactly and redo converts again", error);
	LevelMapDocument reloaded;
	expect(loadLevelMapBytes({QStringLiteral("converted.map"), {}, {}}, after, &reloaded, &error) && reloaded.patches.size() == 6, "the patches save and load",
		error);

	// One picked face: its brush stays. Keep Brushes keeps a whole brush.
	document = quake3Map();
	expect(addLevelMapBoxBrush(&document, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("studio/rock"), &brushId, &error), "second box", error);
	expect(convertLevelMapFacesToPatches(&document, {{brushId, 2}}, {}, request, &report, &error) && report.patchIds.size() == 1 && document.brushes.size() == 1
			&& report.brushesRemoved == 0 && report.unsizedMaterials == QStringList {QStringLiteral("studio/rock")},
		"a picked face becomes one patch beside its brush, an unsized material noted", error);
	request.keepBrushes = true;
	request.columns = 5;
	request.rows = 7;
	expect(convertLevelMapFacesToPatches(&document, {}, {brushId}, request, &report, &error) && report.patchIds.size() == 6 && document.brushes.size() == 1
			&& patchById(document, report.patchIds.first())->width == 5 && patchById(document, report.patchIds.first())->height == 7,
		"Keep Brushes and a 5x7 grid", error);
	request = {};
	request.columns = 4;
	expect(!convertLevelMapFacesToPatches(&document, {}, {brushId}, request, &report, &error), "an even grid is refused");
	request = {};
	expect(!convertLevelMapFacesToPatches(&document, {}, {}, request, &report, &error), "nothing to convert is refused");
	expect(!convertLevelMapFacesToPatches(&document, {}, {99}, request, &report, &error), "a missing brush is refused");

	// Hidden brushes are left; locked ones refuse the edit.
	document = quake3Map();
	int hiddenId = -1;
	expect(addLevelMapBoxBrush(&document, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("studio/grid"), &brushId, &error)
			&& addLevelMapBoxBrush(&document, {128, 0, 0, true}, {192, 64, 64, true}, QStringLiteral("studio/grid"), &hiddenId, &error),
		"two boxes", error);
	LevelVisibilityRequest hide;
	expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, hiddenId}}, &error) && changeLevelMapVisibility(&document, hide, nullptr, &error),
		"hide one", error);
	expect(convertLevelMapFacesToPatches(&document, {}, {brushId, hiddenId}, request, &report, &error) && report.patchIds.size() == 6
			&& report.skipped.size() == 1 && document.brushes.size() == 1 && document.brushes.first().id == hiddenId,
		"a hidden brush is left as it was, with its reason", error);
	expect(!convertLevelMapFacesToPatches(&document, {}, {hiddenId}, request, &report, &error) && error.contains(QString::number(hiddenId)),
		"only hidden brushes is a refusal", error);
	document = quake3Map();
	QString layer;
	expect(addLevelMapBoxBrush(&document, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("studio/grid"), &brushId, &error)
			&& createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
			&& assignLevelSceneObjects(&document, layer, {QStringLiteral("brush:%1").arg(brushId)}, &error) && setLevelSceneLocked(&document, layer, true, &error),
		"locked box", error);
	const QByteArray locked = serializeLevelMap(document).bytes;
	expect(!convertLevelMapFacesToPatches(&document, {}, {brushId}, request, &report, &error) && serializeLevelMap(document).bytes == locked,
		"a locked brush refuses the edit and changes nothing");
	// A Quake map has no patches.
	LevelMapDocument quake;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake");
	create.starterRoom = false;
	expect(createLevelMap(create, &quake, &error) && addLevelMapBoxBrush(&quake, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("rock"), &brushId, &error)
			&& !convertLevelMapFacesToPatches(&quake, {}, {brushId}, request, &report, &error),
		"a Quake map is refused");
}

void checkPolygonsAndPrimitives()
{
	// An eight-sided prism's ends become three patches each, covering them.
	LevelMapDocument document = quake3Map();
	QString error;
	LevelShapeRequest shape;
	shape.shape = QStringLiteral("cylinder");
	shape.sides = 8;
	shape.mins = {-64, -64, 0, true};
	shape.maxs = {64, 64, 32, true};
	shape.texture = QStringLiteral("studio/grid");
	QVector<int> ids;
	expect(addLevelMapShape(&document, shape, &ids, &error) && ids.size() == 1, "octagonal prism", error);
	const LevelMapBrush brush = document.brushes.first();
	const MapBrushGeometry geometry = solveBrushGeometry(brush.faces, brush.id, brush.entityId);
	for (int face = 0; face < brush.faces.size(); ++face) {
		const auto& corners = geometry.faces[face].points;
		if (corners.size() != 8) {
			continue;
		}
		LevelPatchConvertRequest request;
		QVector<LevelMapPatch> patches;
		expect(levelBrushFacePatches(brush, face, request, &patches, &error) && patches.size() == 3, "an octagon becomes three patches", error);
		// Their bilinear areas add up to the octagon's.
		const auto area = [](const QVector<LevelMapVec3>& polygon) {
			LevelMapVec3 sum {0, 0, 0, true};
			for (qsizetype i = 0; i < polygon.size(); ++i) {
				const LevelMapVec3 c = cross(polygon[i], polygon[(i + 1) % polygon.size()]);
				sum = {sum.x + c.x, sum.y + c.y, sum.z + c.z, true};
			}
			return std::sqrt(dot(sum, sum)) / 2;
		};
		double covered = 0;
		for (const LevelMapPatch& patch : patches) {
			covered += area({patch.controlPoints[0], patch.controlPoints[2], patch.controlPoints[8], patch.controlPoints[6]});
		}
		expect(near(covered, area(corners), 1e-6), "the patches cover the face exactly", QStringLiteral("%1 of %2").arg(covered).arg(area(corners)));
	}
	// A triangle is one patch with its last edge closed to a point.
	LevelShapeRequest wedgeShape = shape;
	wedgeShape.shape = QStringLiteral("wedge");
	expect(addLevelMapShape(&document, wedgeShape, &ids, &error), "wedge", error);
	const LevelMapBrush wedgeBrush = document.brushes.last();
	const MapBrushGeometry wedgeGeometry = solveBrushGeometry(wedgeBrush.faces, wedgeBrush.id, wedgeBrush.entityId);
	bool triangle = false;
	for (int face = 0; face < wedgeBrush.faces.size(); ++face) {
		if (wedgeGeometry.faces[face].points.size() == 3) {
			QVector<LevelMapPatch> patches;
			const auto same = [](const LevelMapVec3& a, const LevelMapVec3& b) { return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z); };
			// The last row or, turned to face out, the last column.
			triangle = levelBrushFacePatches(wedgeBrush, face, {}, &patches, &error) && patches.size() == 1
				&& (same(patches.first().controlPoints[6], patches.first().controlPoints[8])
					|| same(patches.first().controlPoints[2], patches.first().controlPoints[8]));
		}
	}
	expect(triangle, "a triangle becomes one patch with a collapsed edge", error);
	// Brush primitives count repeats: the image size changes nothing.
	LevelMapBrush primitive = brush;
	for (LevelMapBrushFace& face : primitive.faces) {
		face.explicitTextureMatrix = true;
		face.textureMatrix = {0.0078125, 0, 0.25, 0, 0.015625, 0.5};
	}
	LevelPatchConvertRequest small;
	small.imageSize = [](const QString&) { return QSize(16, 16); };
	LevelPatchConvertRequest large;
	large.imageSize = [](const QString&) { return QSize(1024, 512); };
	QVector<LevelMapPatch> a;
	QVector<LevelMapPatch> b;
	expect(levelBrushFacePatches(primitive, 0, small, &a, &error) && levelBrushFacePatches(primitive, 0, large, &b, &error) && !a.isEmpty()
			&& a.first().controlU == b.first().controlU && a.first().controlV == b.first().controlV,
		"brush-primitive faces keep their repeats", error);
	if (!a.isEmpty()) {
		const LevelTextureProjection projection = levelTextureProjection(primitive.faces.first());
		const QPointF at = projection.at(a.first().controlPoints[4]);
		expect(projection.normalizedCoordinates && near(a.first().controlU[4], at.x()) && near(a.first().controlV[4], at.y()), "repeats as the face reads them");
	}
}

void checkDeform()
{
	// mt19937's first output for its default seed is 3499211612, fixed by the
	// C++ standard: 64 * 3499211612 / 4294967295 truncates to 52.
	expect(levelPatchDeformOffsets(5489, 64, 1) == QVector<int> {52}, "the first draw of seed 5489 is 52");
	const QVector<int> draws = levelPatchDeformOffsets(7, 16, 200);
	const QVector<int> negative = levelPatchDeformOffsets(7, -16, 200);
	bool ranged = draws.size() == 200 && draws == levelPatchDeformOffsets(7, 16, 200) && draws != levelPatchDeformOffsets(8, 16, 200);
	for (int i = 0; i < draws.size(); ++i) {
		ranged = ranged && draws[i] >= 0 && draws[i] <= 16 && negative[i] == -draws[i];
	}
	expect(ranged, "a seed repeats its draws, from 0 to the maximum, mirrored for a negative one");

	LevelMapDocument document = quake3Map();
	QString error;
	LevelPatchCreateRequest create;
	create.shape = QStringLiteral("plane");
	create.columns = 5;
	create.rows = 3;
	create.texture = QStringLiteral("studio/grid");
	LevelMapPatch plane;
	int first = -1;
	int second = -1;
	expect(createLevelPatch(create, &plane, &error) && addLevelMapPatch(&document, plane, &first, &error), "first patch", error);
	create.center = {256, 0, 0, true};
	expect(createLevelPatch(create, &plane, &error) && addLevelMapPatch(&document, plane, &second, &error), "second patch", error);
	const LevelMapDocument start = document;
	const QByteArray before = serializeLevelMap(document).bytes;
	const auto depth = document.undoStack.size();
	LevelPatchDeformRequest request;
	request.maximum = 24;
	request.axis = 2;
	request.seed = 11;
	int changed = 0;
	int skipped = 0;
	expect(deformLevelMapPatches(&document, {first, second}, request, &changed, &skipped, &error) && changed == 2 && skipped == 0
			&& document.undoStack.size() == depth + 1,
		"Deform moves both patches in one step", error);
	const QVector<int> offsets = levelPatchDeformOffsets(11, 24, 30);
	int next = 0;
	bool moved = true;
	for (const int id : {first, second}) {
		const LevelMapPatch* was = patchById(start, id);
		const LevelMapPatch* now = patchById(document, id);
		for (qsizetype i = 0; was && now && i < was->controlPoints.size(); ++i) {
			const int offset = offsets.value(next++);
			moved = moved && near(now->controlPoints[i].x, was->controlPoints[i].x) && near(now->controlPoints[i].y, was->controlPoints[i].y)
				&& near(now->controlPoints[i].z, was->controlPoints[i].z + offset) && now->controlU[i] == was->controlU[i] && now->controlV[i] == was->controlV[i];
		}
	}
	expect(moved && next == 30, "each control point moves by its own draw along Z only, in document order, its texture coordinates kept");
	expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == before, "undo restores the patches exactly", error);
	LevelVisibilityRequest hide;
	expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakePatch, second}}, &error) && changeLevelMapVisibility(&document, hide, nullptr, &error),
		"hide the second", error);
	expect(deformLevelMapPatches(&document, {first, second}, request, &changed, &skipped, &error) && changed == 1 && skipped == 1,
		"a hidden patch is left as it was", error);
	request.axis = 3;
	expect(!deformLevelMapPatches(&document, {first}, request, &changed, &skipped, &error), "a fourth axis is refused");
	request.axis = 0;
	request.maximum = 10000;
	expect(!deformLevelMapPatches(&document, {first}, request, &changed, &skipped, &error), "a maximum past 9999 is refused");
	request.maximum = 8;
	expect(!deformLevelMapPatches(&document, {99}, request, &changed, &skipped, &error), "a missing patch is refused");
}

void checkCli(const QString& binary)
{
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "temporary directory")) {
		return;
	}
	const QDir root(temp.path());
	LevelMapDocument document = quake3Map();
	QString error;
	int brushId = -1;
	expect(addLevelMapBoxBrush(&document, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("studio/grid"), &brushId, &error), "box", error);
	LevelPatchCreateRequest create;
	create.shape = QStringLiteral("cylinder");
	create.center = {256, 0, 32, true};
	create.texture = QStringLiteral("studio/pipe");
	LevelMapPatch tube;
	int tubeId = -1;
	expect(createLevelPatch(create, &tube, &error) && addLevelMapPatch(&document, tube, &tubeId, &error), "tube", error);
	const QString source = root.filePath(QStringLiteral("source.map"));
	{
		QFile file(source);
		const QByteArray bytes = serializeLevelMap(document).bytes;
		expect(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write the source map");
	}
	const auto run = [&](const QStringList& arguments, int code, QJsonObject* json = nullptr) {
		QProcess process;
		process.start(binary, QStringList {QStringLiteral("--cli"), QStringLiteral("--settings-file"), root.filePath(QStringLiteral("settings.ini")),
								  QStringLiteral("map")} + arguments);
		const bool done = process.waitForFinished(60000);
		const QByteArray output = process.readAllStandardOutput();
		if (json) {
			*json = QJsonDocument::fromJson(output).object();
		}
		return expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code, "CLI exit code",
			arguments.join(QLatin1Char(' ')) + QLatin1Char('\n') + QString::fromUtf8(output + process.readAllStandardError()));
	};
	const auto load = [](const QString& path) {
		LevelMapDocument loaded;
		QString problem;
		expect(loadLevelMap({path, {}, {}}, &loaded, &problem), "reload CLI output", problem);
		return loaded;
	};
	// convert-patches writes what the service makes.
	const QString converted = root.filePath(QStringLiteral("converted.map"));
	QJsonObject json;
	run({QStringLiteral("convert-patches"), source, QStringLiteral("--object"), QStringLiteral("brush:%1").arg(brushId), QStringLiteral("--texture-size"),
			QStringLiteral("128,64"), QStringLiteral("--output"), converted, QStringLiteral("--json")},
		0, &json);
	LevelMapDocument expected = load(source);
	LevelPatchConvertRequest request;
	request.imageSize = [](const QString&) { return QSize(128, 64); };
	expect(convertLevelMapFacesToPatches(&expected, {}, {brushId}, request, nullptr, &error), "the service converts", error);
	expect(serializeLevelMap(load(converted)).bytes == serializeLevelMap(expected).bytes && json.value(QStringLiteral("patches")).toArray().size() == 6
			&& json.value(QStringLiteral("brushesRemoved")).toInt() == 1 && json.value(QStringLiteral("faces")).toInt() == 6,
		"map convert-patches writes the service's patches", QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
	run({QStringLiteral("convert-patches"), source, QStringLiteral("--face"), QStringLiteral("face:%1:1").arg(brushId), QStringLiteral("--columns"),
			QStringLiteral("5"), QStringLiteral("--output"), converted, QStringLiteral("--overwrite"), QStringLiteral("--json")},
		0, &json);
	const LevelMapDocument picked = load(converted);
	expect(picked.brushes.size() == 1 && picked.patches.size() == 2 && picked.patches.last().width == 5 && json.value(QStringLiteral("unsizedMaterials")).toArray().size() == 1,
		"a --face converts beside its brush, an unsized material reported", QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
	for (const QStringList& bad : {QStringList {QStringLiteral("--object"), QStringLiteral("patch:0")}, QStringList {QStringLiteral("--columns"), QStringLiteral("4"), QStringLiteral("--object"), QStringLiteral("brush:0")},
			 QStringList {}, QStringList {QStringLiteral("--object"), QStringLiteral("brush:0"), QStringLiteral("--texture-size"), QStringLiteral("64,64"), QStringLiteral("--package"), root.path()},
			 QStringList {QStringLiteral("--face"), QStringLiteral("brush:0")}}) {
		run(QStringList {QStringLiteral("convert-patches"), source} + bad + QStringList {QStringLiteral("--output"), converted, QStringLiteral("--overwrite")}, 2);
	}
	run({QStringLiteral("convert-patches"), source, QStringLiteral("--object"), QStringLiteral("brush:9"), QStringLiteral("--output"), converted,
			QStringLiteral("--overwrite")},
		4);
	// deform-patch with a seed repeats the service's result.
	const QString deformed = root.filePath(QStringLiteral("deformed.map"));
	run({QStringLiteral("deform-patch"), source, QStringLiteral("--patch"), QString::number(tubeId), QStringLiteral("--max"), QStringLiteral("12"),
			QStringLiteral("--axis"), QStringLiteral("x"), QStringLiteral("--seed"), QStringLiteral("99"), QStringLiteral("--output"), deformed,
			QStringLiteral("--json")},
		0, &json);
	expected = load(source);
	LevelPatchDeformRequest deform;
	deform.maximum = 12;
	deform.axis = 0;
	deform.seed = 99;
	expect(deformLevelMapPatches(&expected, {tubeId}, deform, nullptr, nullptr, &error), "the service deforms", error);
	expect(serializeLevelMap(load(deformed)).bytes == serializeLevelMap(expected).bytes && json.value(QStringLiteral("seed")).toInteger() == 99
			&& json.value(QStringLiteral("axis")).toString() == QStringLiteral("x") && json.value(QStringLiteral("changed")).toInt() == 1,
		"map deform-patch writes the service's result for the seed", QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
	run({QStringLiteral("deform-patch"), source, QStringLiteral("--object"), QStringLiteral("patch:%1").arg(tubeId), QStringLiteral("--output"), deformed,
			QStringLiteral("--overwrite"), QStringLiteral("--json")},
		0, &json);
	expect(json.contains(QStringLiteral("seed")) && json.value(QStringLiteral("max")).toInt() == 64 && json.value(QStringLiteral("axis")).toString() == QStringLiteral("z"),
		"without --seed a drawn seed is reported, with upstream's 64 along Z");
	for (const QStringList& bad : {QStringList {}, QStringList {QStringLiteral("--patch"), QStringLiteral("0"), QStringLiteral("--axis"), QStringLiteral("w")},
			 QStringList {QStringLiteral("--patch"), QStringLiteral("0"), QStringLiteral("--max"), QStringLiteral("10000")},
			 QStringList {QStringLiteral("--patch"), QStringLiteral("0"), QStringLiteral("--patch"), QStringLiteral("0")},
			 QStringList {QStringLiteral("--object"), QStringLiteral("brush:0")}, QStringList {QStringLiteral("--patch"), QStringLiteral("0"), QStringLiteral("--seed"), QStringLiteral("-1")}}) {
		run(QStringList {QStringLiteral("deform-patch"), source} + bad + QStringList {QStringLiteral("--output"), deformed, QStringLiteral("--overwrite")}, 2);
	}
	run({QStringLiteral("deform-patch"), source, QStringLiteral("--patch"), QStringLiteral("7"), QStringLiteral("--output"), deformed, QStringLiteral("--overwrite")}, 4);
	run({QStringLiteral("deform-patch"), source, QStringLiteral("--patch"), QString::number(tubeId)}, 2);
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	checkConvertBox();
	checkPolygonsAndPrimitives();
	checkDeform();
	if (argc > 1) {
		checkCli(QString::fromLocal8Bit(argv[1]));
	}
	std::cout << (ok ? "level patch convert and deform checks passed" : "level patch convert and deform checks FAILED") << std::endl;
	return ok ? 0 : 1;
}
