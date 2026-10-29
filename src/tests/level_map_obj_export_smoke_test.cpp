// Export Map as OBJ and Export Selection as OBJ (core/level_map_obj_export)
// on classic, Valve 220 and brush-primitive maps and Quake III patches, the
// map export-obj CLI, and the .bsp conversion fixes of Import Map: the game
// read from the BSP header, the exit code of a silent VibeMap3 failure and the
// preview's byte count.
#include "core/compiler_runner.h"
#include "core/level_document.h"
#include "core/level_map_obj_export.h"
#include "core/level_map_transfer.h"
#include "core/level_materials.h"
#include "core/level_patch.h"
#include "core/level_scene.h"
#include "core/level_texture_mapping.h"
#include "core/level_visibility.h"
#include "core/map_geometry.h"
#include "core/studio_settings.h"
#include "tests/level_placement_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>

#include <array>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool pass, const char* label, const QString& detail = {})
{
	if (!pass) {
		std::cerr << "FAIL: " << label << ": " << qPrintable(detail) << '\n';
		ok = false;
	}
	return pass;
}

using Vec3 = std::array<double, 3>;

// Just enough of OBJ to check what the export writes.
struct Obj {
	QVector<Vec3> v, vn;
	QVector<std::array<double, 2>> vt;
	struct Face {
		QString object;
		QString material;
		QVector<std::array<int, 3>> corners;
	};
	QVector<Face> faces;
	QStringList objects, groups, usemtl;
	QString mtllib;
};

Obj parseObj(const QByteArray& bytes)
{
	Obj obj;
	QString object, material;
	const auto lines = QString::fromUtf8(bytes).split(QLatin1Char('\n'));
	for (const auto& line : lines) {
		const auto parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
		if (parts.isEmpty()) {
			continue;
		}
		const auto& kind = parts.first();
		if (kind == QLatin1String("v") && parts.size() == 4) {
			obj.v.push_back({parts[1].toDouble(), parts[2].toDouble(), parts[3].toDouble()});
		} else if (kind == QLatin1String("vn") && parts.size() == 4) {
			obj.vn.push_back({parts[1].toDouble(), parts[2].toDouble(), parts[3].toDouble()});
		} else if (kind == QLatin1String("vt") && parts.size() == 3) {
			obj.vt.push_back({parts[1].toDouble(), parts[2].toDouble()});
		} else if (kind == QLatin1String("o")) {
			object = parts.value(1);
			obj.objects << object;
		} else if (kind == QLatin1String("g")) {
			object = parts.value(1);
			obj.groups << object;
		} else if (kind == QLatin1String("usemtl")) {
			material = parts.value(1);
			obj.usemtl << material;
		} else if (kind == QLatin1String("mtllib")) {
			obj.mtllib = parts.value(1);
		} else if (kind == QLatin1String("f")) {
			Obj::Face face{object, material, {}};
			for (int i = 1; i < parts.size(); ++i) {
				const auto refs = parts[i].split(QLatin1Char('/'));
				face.corners.push_back({refs.value(0).toInt(), refs.value(1).toInt(), refs.value(2).toInt()});
			}
			obj.faces << face;
		}
	}
	return obj;
}

LevelMapDocument fixture(const QString& kind)
{
	LevelMapDocument document;
	QString error;
	expect(loadLevelMapBytes({QStringLiteral("fixture-%1.map").arg(kind), {}, QStringLiteral("idtech3")}, tests::placementFixture(kind), &document, &error),
		"fixture loads", kind + ' ' + error);
	return document;
}

LevelMapObjFiles build(const LevelMapDocument& document, const LevelMapObjExportOptions& options, const QString& objPath,
	LevelMapObjExportReport* report = nullptr, QString* error = nullptr)
{
	LevelMapDocument exported;
	LevelMapObjFiles files;
	LevelMapObjExportReport local;
	auto& counts = report ? *report : local;
	QString problem;
	if (!prepareLevelMapObjExport(document, options, &exported, &counts, &problem) || !buildLevelMapObj(exported, objPath, options, &files, &counts, &problem)) {
		if (error) {
			*error = problem;
		}
		return {};
	}
	return files;
}

Vec3 toMap(const Vec3& value, bool zUp)
{
	return zUp ? value : Vec3{value[0], -value[2], value[1]};
}

// Every corner's UV is the face projection at that point (over the texture
// size for classic and Valve 220), and every face is counter-clockwise seen
// from outside the box.
void checkBox(const LevelMapDocument& document, const Obj& obj, const QSize& size, bool zUp, const char* label)
{
	const auto& brush = document.brushes.first();
	bool uvs = true, facing = true, outward = true;
	QString detail;
	for (const auto& face : obj.faces) {
		Vec3 newell{0, 0, 0}, centroid{0, 0, 0};
		for (int i = 0; i < face.corners.size(); ++i) {
			const auto& a = obj.v.value(face.corners[i][0] - 1);
			const auto& b = obj.v.value(face.corners[(i + 1) % face.corners.size()][0] - 1);
			newell[0] += (a[1] - b[1]) * (a[2] + b[2]);
			newell[1] += (a[2] - b[2]) * (a[0] + b[0]);
			newell[2] += (a[0] - b[0]) * (a[1] + b[1]);
			for (int axis = 0; axis < 3; ++axis) {
				centroid[axis] += a[axis] / static_cast<double>(face.corners.size());
			}
		}
		const auto normal = obj.vn.value(face.corners.first()[2] - 1);
		facing &= newell[0] * normal[0] + newell[1] * normal[1] + newell[2] * normal[2] > 0.0;
		// The box spans (-29, 19, -5) to (99, 83, 59) in map space.
		const auto centre = toMap(centroid, zUp);
		const auto mapNormal = toMap(normal, zUp);
		outward &= (centre[0] - 35) * mapNormal[0] + (centre[1] - 51) * mapNormal[1] + (centre[2] - 27) * mapNormal[2] > 0.0;
		for (const auto& corner : face.corners) {
			const auto p = toMap(obj.v.value(corner[0] - 1), zUp);
			const LevelMapVec3 point{p[0], p[1], p[2], true};
			const LevelMapBrushFace* source = nullptr;
			for (const auto& candidate : brush.faces) {
				const auto plane = planeFromPoints(candidate.p0, candidate.p1, candidate.p2);
				const double along = plane.normalX * mapNormal[0] + plane.normalY * mapNormal[1] + plane.normalZ * mapNormal[2];
				const double distance = plane.normalX * p[0] + plane.normalY * p[1] + plane.normalZ * p[2] - plane.distance;
				if (std::abs(along) > 0.999 && std::abs(distance) < 0.01) {
					source = &candidate;
				}
			}
			if (!source) {
				uvs = false;
				detail = QStringLiteral("no face for a corner");
				continue;
			}
			const auto projection = levelTextureProjection(*source);
			auto expected = projection.at(point);
			if (!projection.normalizedCoordinates) {
				expected = QPointF(expected.x() / size.width(), expected.y() / size.height());
			}
			const auto st = obj.vt.value(corner[1] - 1);
			if (std::abs(expected.x() - st[0]) > 2e-4 || std::abs(expected.y() - (1.0 - st[1])) > 2e-4) {
				uvs = false;
				detail = QStringLiteral("expected %1 %2, wrote %3 %4").arg(expected.x()).arg(expected.y()).arg(st[0]).arg(1.0 - st[1]);
			}
		}
	}
	expect(uvs, label, QStringLiteral("UVs: ") + detail);
	expect(facing && outward, label, QStringLiteral("faces wind counter-clockwise from outside"));
}

int worldspawnId(const LevelMapDocument& document)
{
	for (const auto& entity : document.entities) {
		if (entity.className == QLatin1String("worldspawn")) {
			return entity.id;
		}
	}
	return -1;
}

void dialectTests(const QString& folder)
{
	const auto objPath = QDir(folder).filePath(QStringLiteral("box.obj"));
	for (const auto& kind : {QStringLiteral("classic"), QStringLiteral("valve220"), QStringLiteral("brushDef")}) {
		const auto document = fixture(kind);
		if (document.brushes.isEmpty()) {
			continue;
		}
		auto options = levelMapObjDefaultOptions();
		options.materials.insert(QStringLiteral("studio/checker"), {QStringLiteral("textures/studio/checker.tga"), QSize(64, 32), QColor(255, 0, 0)});
		LevelMapObjExportReport report;
		QString error;
		const auto files = build(document, options, objPath, &report, &error);
		const auto obj = parseObj(files.obj);
		const auto label = QStringLiteral("%1: ").arg(kind);
		expect(!files.obj.isEmpty() && error.isEmpty(), "box exports", label + error);
		expect(obj.objects == QStringList{QStringLiteral("entity%1_brush%2").arg(document.brushes.first().entityId).arg(document.brushes.first().id)}
				&& obj.faces.size() == 6 && obj.v.size() == 8 && obj.vn.size() == 6 && obj.mtllib == QStringLiteral("box.mtl")
				&& obj.usemtl == QStringList{QStringLiteral("studio/checker")},
			"one welded quad object with its material", label + QString::fromUtf8(files.obj.left(400)));
		bool quads = true;
		for (const auto& face : obj.faces) {
			quads &= face.corners.size() == 4;
		}
		expect(quads && report.polygons == 6 && report.triangles == 12 && report.brushes == 1 && report.materials == 1 && report.unsizedMaterials.isEmpty(),
			"faces are whole polygons", label);
		checkBox(document, obj, QSize(64, 32), false, "Y-up box");
		expect(files.mtl.contains("newmtl studio/checker\nKd 1 0 0\nmap_Kd textures/studio/checker.tga\n"), "MTL names the image", QString::fromUtf8(files.mtl));
		// Y up: the map's Z runs along OBJ Y, its Y along -Z.
		double minY = 1e9, maxY = -1e9, minZ = 1e9, maxZ = -1e9;
		for (const auto& v : obj.v) {
			minY = std::min(minY, v[1]);
			maxY = std::max(maxY, v[1]);
			minZ = std::min(minZ, v[2]);
			maxZ = std::max(maxZ, v[2]);
		}
		expect(minY == -5 && maxY == 59 && minZ == -83 && maxZ == -19, "Y-up axes", label);

		options.keepZUp = true;
		options.triangulate = true;
		options.weld = false;
		const auto zUp = parseObj(build(document, options, objPath).obj);
		bool triangles = true;
		for (const auto& face : zUp.faces) {
			triangles &= face.corners.size() == 3;
		}
		expect(triangles && zUp.faces.size() == 12 && zUp.v.size() == 24, "triangles without welding", label);
		checkBox(document, zUp, QSize(64, 32), true, "Z-up box");
	}

	// Unknown sizes fall back to 64 x 64 and are reported.
	const auto classic = fixture(QStringLiteral("classic"));
	LevelMapObjExportReport report;
	const auto unsized = parseObj(build(classic, levelMapObjDefaultOptions(), objPath, &report).obj);
	checkBox(classic, unsized, QSize(64, 64), false, "unsized material assumes 64 x 64");
	expect(report.unsizedMaterials == QStringList{QStringLiteral("studio/checker")} && report.unresolvedMaterials == QStringList{QStringLiteral("studio/checker")}
		&& report.warnings.size() == 2, "unsized and unresolved materials reported", report.warnings.join('\n'));
	const auto brushDef = fixture(QStringLiteral("brushDef"));
	build(brushDef, levelMapObjDefaultOptions(), objPath, &report);
	expect(report.unsizedMaterials.isEmpty(), "brush primitives need no size");
}

// A Quake III map: a metal box, a caulk box, a func_door box and a patch.
LevelMapDocument quake3Map()
{
	LevelMapDocument document;
	LevelMapCreateRequest request;
	request.game = QStringLiteral("quake3");
	request.starterRoom = false;
	QString error;
	expect(createLevelMap(request, &document, &error), "quake3 map", error);
	expect(addLevelMapBoxBrush(&document, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("base_wall/metal"), nullptr, &error)
		&& addLevelMapBoxBrush(&document, {128, 0, 0, true}, {192, 64, 64, true}, QStringLiteral("common/caulk"), nullptr, &error)
		&& addLevelMapBoxBrush(&document, {256, 0, 0, true}, {320, 64, 64, true}, QStringLiteral("base_wall/a_very_long_material_name_indeed"), nullptr, &error),
		"boxes", error);
	expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, document.brushes.last().id}}, &error)
		&& tieLevelMapSelectionToEntity(&document, QStringLiteral("func_door"), {}, nullptr, &error), "door", error);
	LevelPatchCreateRequest shape;
	shape.center = {32, 32, 128, true};
	shape.size = {64, 64, 16, true};
	shape.texture = QStringLiteral("textures/base_floor/patch");
	LevelMapPatch patch;
	expect(createLevelPatch(shape, &patch, &error) && addLevelMapPatch(&document, patch, nullptr, &error), "patch", error);
	clearLevelMapSelection(&document);
	return document;
}

void optionTests(const QString& folder)
{
	const auto objPath = QDir(folder).filePath(QStringLiteral("q3.obj"));
	auto document = quake3Map();
	if (!expect(document.brushes.size() == 3 && document.patches.size() == 1, "quake3 fixture objects")) {
		return;
	}
	QString error;
	LevelMapObjExportReport report;
	auto options = levelMapObjDefaultOptions();
	auto obj = parseObj(build(document, options, objPath, &report, &error).obj);
	const int door = document.brushes.last().entityId;
	const auto patchObject = QStringLiteral("entity%1_patch%2").arg(document.patches.first().entityId).arg(document.patches.first().id);
	expect(report.brushes == 2 && report.patches == 1 && report.ignoredFaces == 6 && obj.objects.size() == 3 && obj.objects.contains(patchObject)
			&& obj.objects.contains(QStringLiteral("entity%1_brush%2").arg(door).arg(document.brushes.last().id)) && !obj.usemtl.contains(QStringLiteral("common/caulk")),
		"caulk left out, door and patch kept", error + ' ' + obj.objects.join(','));
	bool patchUvs = false;
	for (const auto& face : obj.faces) {
		if (face.object == patchObject) {
			patchUvs = true;
			for (const auto& corner : face.corners) {
				const auto st = obj.vt.value(corner[1] - 1);
				patchUvs &= std::isfinite(st[0]) && std::isfinite(st[1]);
			}
		}
	}
	expect(patchUvs, "patch quads written with UVs");

	options.ignoredMaterials.clear();
	build(document, options, objPath, &report);
	expect(report.brushes == 3 && report.ignoredFaces == 0, "nothing ignored on request");
	expect(levelMapObjMaterialIgnored(QStringLiteral("textures/common/caulk"), {QStringLiteral("caulk")})
			&& levelMapObjMaterialIgnored(QStringLiteral("e1u1/CLIP"), {QStringLiteral("clip")})
			&& levelMapObjMaterialIgnored(QStringLiteral("textures/common/caulk"), {QStringLiteral("common/caulk")})
			&& !levelMapObjMaterialIgnored(QStringLiteral("common/caulkx"), {QStringLiteral("caulk")})
			&& !levelMapObjMaterialIgnored(QStringLiteral("xcommon/caulk"), {QStringLiteral("common/caulk")})
			&& !levelMapObjMaterialIgnored(QStringLiteral("*water1"), levelMapObjDefaultIgnoredMaterials()),
		"ignore list matches the last part or the end of the path");

	// Grouping by material, into one mesh, and g records.
	options = levelMapObjDefaultOptions();
	options.grouping = LevelMapObjGrouping::Material;
	obj = parseObj(build(document, options, objPath, &report).obj);
	expect(obj.objects.size() == 3 && obj.objects.first().endsWith(QStringLiteral("base_floor/patch"))
			&& obj.objects.mid(1) == QStringList({QStringLiteral("base_wall/a_very_long_material_name_indeed"), QStringLiteral("base_wall/metal")}),
		"one object for each material, by name", obj.objects.join(','));
	options.grouping = LevelMapObjGrouping::Single;
	options.objects = false;
	obj = parseObj(build(document, options, objPath, &report).obj);
	expect(obj.objects.isEmpty() && obj.groups.size() == 1 && obj.usemtl.size() == 3, "one group with three materials", obj.groups.join(','));
	options.shortMaterialNames = true;
	obj = parseObj(build(document, options, objPath, &report).obj);
	bool shortNames = !obj.usemtl.isEmpty();
	for (const auto& name : obj.usemtl) {
		shortNames &= name.size() <= 20;
	}
	expect(shortNames && obj.usemtl.contains(QStringLiteral("aterial_name_indeed")) == false && obj.usemtl.contains(QStringLiteral("material_name_indeed")),
		"short material names keep the last 20 characters", obj.usemtl.join(','));
	options = levelMapObjDefaultOptions();
	options.materialLibrary = false;
	LevelMapObjFiles files = build(document, options, objPath);
	obj = parseObj(files.obj);
	expect(files.mtl.isEmpty() && obj.mtllib.isEmpty() && obj.usemtl.isEmpty(), "no material library on request");

	// The selection: a brush, or an entity with its brushes.
	options = levelMapObjDefaultOptions();
	options.selectionOnly = true;
	expect(build(document, options, objPath, &report, &error).obj.isEmpty() && !error.isEmpty(), "an empty selection is refused");
	auto selected = document;
	expect(setLevelMapSelection(&selected, {{LevelMapSelectionKind::Entity, door}}, &error), "select door", error);
	obj = parseObj(build(selected, options, objPath, &report, &error).obj);
	expect(report.brushes == 1 && report.patches == 0 && obj.objects.size() == 1 && obj.objects.first().startsWith(QStringLiteral("entity%1_").arg(door)),
		"a selected entity brings its brushes", error);
	expect(setLevelMapSelection(&selected, {{LevelMapSelectionKind::QuakeBrush, document.brushes.at(1).id}}, &error), "select caulk", error);
	expect(build(selected, options, objPath, &report, &error).obj.isEmpty() && error.contains(QStringLiteral("ignored")), "an all-caulk selection is refused", error);

	// Hidden objects and layers marked Omit from export are left out.
	auto hidden = document;
	expect(setLevelMapSelection(&hidden, {{LevelMapSelectionKind::QuakeBrush, document.brushes.first().id}}, &error)
		&& changeLevelMapVisibility(&hidden, {LevelVisibilityOperation::HideSelected}, nullptr, &error), "hide box", error);
	options = levelMapObjDefaultOptions();
	build(hidden, options, objPath, &report);
	expect(report.hiddenObjects == 1 && report.brushes == 1 && report.patches == 1, "hidden box left out");
	auto layered = document;
	QString layer;
	expect(createLevelSceneNode(&layered, LevelSceneNodeKind::Layer, QStringLiteral("Notes"), {}, &layer, &error)
		&& assignLevelSceneObjects(&layered, layer, {QStringLiteral("patch:%1").arg(document.patches.first().id)}, &error)
		&& setLevelSceneOmitFromExport(&layered, layer, true, &error), "omitted layer", error);
	build(layered, options, objPath, &report);
	expect(report.patches == 0 && report.omittedLayers == QStringList{QStringLiteral("Notes")} && report.omittedObjects == 1, "omitted layer left out");
	options.includeOmittedLayers = true;
	build(layered, options, objPath, &report);
	expect(report.patches == 1 && report.omittedLayers.isEmpty(), "omitted layer kept on request");
	options.includeOmittedLayers = false;
	options.omitLayers = {QStringLiteral("nowhere")};
	expect(build(layered, options, objPath, &report, &error).obj.isEmpty() && !error.isEmpty(), "unknown layer refused", error);
	options = levelMapObjDefaultOptions();
	options.selectionOnly = true;
	expect(setLevelMapSelection(&layered, {{LevelMapSelectionKind::QuakePatch, document.patches.first().id}}, &error), "select patch", error);
	build(layered, options, objPath, &report);
	expect(report.patches == 1, "a selection keeps objects of omitted layers");

	// Doom maps, cancellation and the triangle limit.
	LevelMapDocument doom;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("doom");
	expect(createLevelMap(create, &doom, &error), "doom map", error);
	options = levelMapObjDefaultOptions();
	expect(build(doom, options, objPath, &report, &error).obj.isEmpty() && error.contains(QStringLiteral("Doom")), "Doom maps refused", error);
	LevelMapDocument exported;
	LevelMapObjFiles cancelled;
	expect(prepareLevelMapObjExport(document, options, &exported, &report, &error)
			&& !buildLevelMapObj(exported, objPath, options, &cancelled, &report, &error, [] { return true; }) && report.cancelled,
		"cancellation", error);
	options.triangleLimit = 4;
	expect(build(document, options, objPath, &report, &error).obj.isEmpty() && error.contains(QStringLiteral("triangles")), "triangle limit", error);
}

void writeTests(const QString& folder)
{
	auto document = quake3Map();
	if (document.patches.isEmpty()) {
		return;
	}
	QString error;
	const auto mapPath = QDir(folder).filePath(QStringLiteral("q3.map"));
	LevelDocumentSaveRequest save;
	save.path = mapPath;
	save.keepBackup = false;
	expect(writeLevelDocument(document, save).succeeded(), "q3 map saved");
	expect(loadLevelMap({mapPath, {}, {}}, &document, &error), "q3 map loads", error);
	const auto before = serializeLevelMap(document).bytes;
	const auto revision = document.revision;
	const auto objPath = QDir(folder).filePath(QStringLiteral("out/q3.obj"));
	const auto mtlPath = QDir(folder).filePath(QStringLiteral("out/q3.mtl"));
	LevelMapObjExportRequest request;
	request.save.path = objPath;
	request.save.dryRun = true;
	request.options = levelMapObjDefaultOptions();
	auto report = exportLevelMapObj(document, request);
	expect(report.succeeded() && !report.written && !QFileInfo::exists(objPath) && !QFileInfo::exists(mtlPath) && report.objBytes > 0 && report.mtlBytes > 0,
		"dry run writes nothing", report.errors.join('\n'));
	request.save.dryRun = false;
	report = exportLevelMapObj(document, request);
	expect(report.succeeded() && report.written && QFileInfo::exists(objPath) && QFileInfo::exists(mtlPath) && report.mtlPath == QFileInfo(mtlPath).absoluteFilePath(),
		"OBJ and MTL written", report.errors.join('\n'));
	expect(serializeLevelMap(document).bytes == before && document.revision == revision, "the document is unchanged");
	report = exportLevelMapObj(document, request);
	expect(!report.succeeded() && report.errors.join(' ').contains(QStringLiteral("exists")), "existing files need overwrite", report.errors.join('\n'));
	request.save.overwrite = true;
	report = exportLevelMapObj(document, request);
	expect(report.succeeded() && !report.objBackup.isEmpty() && !report.mtlBackup.isEmpty(), "replaced files are backed up", report.errors.join('\n'));
	request.save.path = QDir(folder).filePath(QStringLiteral("out/q3.txt"));
	expect(!exportLevelMapObj(document, request).succeeded(), "a name without .obj is refused");
	// A folder named like the export is refused in the export's own words.
	QDir().mkpath(QDir(folder).filePath(QStringLiteral("out/folder.obj")));
	request.save.path = QDir(folder).filePath(QStringLiteral("out/folder.obj"));
	report = exportLevelMapObj(document, request);
	expect(!report.succeeded() && report.errors.join(' ').contains(QStringLiteral("folder or a symbolic link")) && !report.errors.join(' ').contains(QStringLiteral("map output")),
		"a folder target is refused as an export", report.errors.join('\n'));
	// Spaces in the OBJ's name: mtllib names a library without them.
	const auto spaced = QDir(folder).filePath(QStringLiteral("out/my room.obj"));
	expect(levelMapObjMaterialLibraryPath(spaced) == QFileInfo(QDir(folder).filePath(QStringLiteral("out/my_room.mtl"))).absoluteFilePath(), "library name without spaces");
	request.save.path = spaced;
	report = exportLevelMapObj(document, request);
	QFile spacedObj(spaced);
	expect(report.succeeded() && QFileInfo::exists(QDir(folder).filePath(QStringLiteral("out/my_room.mtl"))) && spacedObj.open(QIODevice::ReadOnly)
			&& spacedObj.readAll().contains("mtllib my_room.mtl\n"),
		"a spaced OBJ name writes a spaceless mtllib", report.errors.join('\n'));
	request.save.path = objPath;
	request.options.materialPaths = LevelMapObjMaterialPaths::Export;
	request.options.materialRoot = folder;
	request.options.materials.insert(QStringLiteral("base_wall/metal"), {QStringLiteral("textures/base_wall/metal.jpg"), QSize(128, 128), {}});
	report = exportLevelMapObj(document, request);
	QFile mtl(mtlPath);
	expect(report.succeeded() && mtl.open(QIODevice::ReadOnly) && mtl.readAll().contains("map_Kd ../textures/base_wall/metal.jpg\n"),
		"export-relative material paths", report.errors.join('\n'));
	const auto json = levelMapObjExportReportJson(report);
	expect(json[QStringLiteral("written")].toBool() && json[QStringLiteral("brushes")].toInt() == 2 && json[QStringLiteral("patches")].toInt() == 1,
		"report JSON");

	// Material sources from resolved preview assets.
	LevelPreviewAssets assets;
	LevelPreviewMaterial material;
	material.key = QStringLiteral("base_wall/metal");
	material.imagePath = QStringLiteral("textures/base_wall/metal.tga");
	material.sourceSize = QSize(256, 128);
	material.image = QImage(4, 4, QImage::Format_ARGB32_Premultiplied);
	material.image.fill(QColor(0, 0, 255));
	assets.materials << material;
	const auto sources = levelMapObjMaterials(assets);
	const auto source = sources.value(QStringLiteral("base_wall/metal"));
	expect(source.imagePath == material.imagePath && source.size == QSize(256, 128) && source.colour == QColor(0, 0, 255), "materials from preview assets");
}

QString writeBsp(const QString& folder, const QString& name, const QByteArray& ident, qint32 version)
{
	const auto path = QDir(folder).filePath(name);
	QFile file(path);
	QByteArray bytes = ident;
	bytes.resize(4, '\0');
	char number[4];
	qToLittleEndian<qint32>(version, number);
	bytes.append(number, 4);
	bytes.append(QByteArray(64, '\0'));
	expect(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "bsp fixture");
	return path;
}

void conversionTests(const QString& folder)
{
	QString error;
	const auto q3 = writeBsp(folder, QStringLiteral("q3.bsp"), "IBSP", 46);
	const auto wolf = writeBsp(folder, QStringLiteral("wolf.bsp"), "IBSP", 47);
	const auto raven = writeBsp(folder, QStringLiteral("raven.bsp"), "RBSP", 1);
	const auto quake2 = writeBsp(folder, QStringLiteral("q2.bsp"), "IBSP", 38);
	const auto quake = writeBsp(folder, QStringLiteral("q1.bsp"), QByteArray("\x1d\0\0\0", 4), 0);
	const auto unknown = writeBsp(folder, QStringLiteral("odd.bsp"), "ABCD", 3);
	expect(levelMapBspConversionGame(q3, {}, &error) == QStringLiteral("quake3") && levelMapBspConversionGame(q3, QStringLiteral("xonotic")) == QStringLiteral("xonotic")
			&& levelMapBspConversionGame(q3, QStringLiteral("et")) == QStringLiteral("quake3"),
		"IBSP 46 converts for Quake III or the project's IBSP 46 game", error);
	expect(levelMapBspConversionGame(wolf, QStringLiteral("quake3")) == QStringLiteral("wolf") && levelMapBspConversionGame(wolf, QStringLiteral("et")) == QStringLiteral("et")
			&& levelMapBspConversionGame(wolf, QStringLiteral("quakelive")) == QStringLiteral("quakelive"),
		"IBSP 47 converts for RTCW, ET or Quake Live");
	expect(levelMapBspConversionGame(raven, {}) == QStringLiteral("ja") && levelMapBspConversionGame(raven, QStringLiteral("jk2")) == QStringLiteral("jk2"),
		"RBSP converts for Jedi Academy or Jedi Outcast");
	expect(levelMapBspConversionGame(quake2, QStringLiteral("quake2"), &error).isEmpty() && error.contains(QStringLiteral("Quake II")), "Quake II BSPs refused", error);
	expect(levelMapBspConversionGame(quake, QStringLiteral("quake"), &error).isEmpty() && error.contains(QStringLiteral("Quake BSP")), "Quake BSPs refused", error);
	expect(levelMapBspConversionGame(unknown, {}, &error).isEmpty() && error.contains(QStringLiteral("VibeMap3")), "unknown headers refused", error);

	LevelMapBspConversionContext context;
	context.gameKey = QStringLiteral("quake3");
	context.basePath = folder;
	context.gameDirectory = QStringLiteral("baseq3");
	CompilerCommandRequest request;
	expect(prepareLevelMapImportConversion(q3, QDir(folder).filePath(QStringLiteral("q3_converted.map")), QStringLiteral("brushDef"), context, &request, &error)
			&& request.extraArguments.mid(0, 2) == QStringList({QStringLiteral("-format"), QStringLiteral("map_bp")})
			&& request.extraArguments.mid(4) == QStringList({QStringLiteral("-game"), QStringLiteral("quake3"), QStringLiteral("-fs_basepath"),
												 QDir::cleanPath(QFileInfo(folder).absoluteFilePath()), QStringLiteral("-fs_game"), QStringLiteral("baseq3")}),
		"conversion passes the game and folders", request.extraArguments.join(' ') + ' ' + error);
	expect(!prepareLevelMapImportConversion(quake2, QStringLiteral("x.map"), {}, context, &request, &error), "Quake II conversion refused before running");

	CompilerRunResult result;
	result.started = true;
	result.exitCode = static_cast<int>(0xC0000139u);
	expect(levelMapImportConversionProblem(result, QStringLiteral("missing.map")).contains(QStringLiteral("0xC0000139")), "a silent failure names its exit code",
		levelMapImportConversionProblem(result, QStringLiteral("missing.map")));
	result.exitCode = 1;
	result.stdoutText = QStringLiteral("--- Convert ---\n********** ERROR **********\nERROR: Unknown BSP format\n");
	const auto printed = levelMapImportConversionProblem(result, QStringLiteral("missing.map"));
	expect(printed.contains(QStringLiteral("code 1")) && printed.contains(QStringLiteral("Unknown BSP format")), "printed errors are kept", printed);
	result.exitCode = 0;
	result.stdoutText.clear();
	expect(levelMapImportConversionProblem(result, QStringLiteral("missing.map")).contains(QStringLiteral("no map")), "a missing output is named");
	result.started = false;
	result.error = QStringLiteral("not found");
	expect(levelMapImportConversionProblem(result, QStringLiteral("missing.map")) == QStringLiteral("not found"), "runner errors pass through");
	const auto converted = QDir(folder).filePath(QStringLiteral("done.map"));
	QFile done(converted);
	expect(done.open(QIODevice::WriteOnly) && done.write("{\n}\n") > 0, "converted fixture");
	done.close();
	result = {};
	result.started = true;
	result.exitCode = 0;
	expect(levelMapImportConversionProblem(result, converted).isEmpty(), "a finished conversion has no problem");

	// The preview counts UTF-8 bytes without copying, or takes the worker's size.
	LevelMapDocument destination, imported;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	expect(createLevelMap(create, &destination, &error) && createLevelMap(create, &imported, &error)
		&& setLevelMapEntityProperty(&imported, worldspawnId(imported), QStringLiteral("message"), QString::fromUtf8("Caf\xc3\xa9 \xe2\x9c\x93 \xf0\x9d\x84\x9e"), &error),
		"preview fixtures", error);
	const auto text = serializeLevelMap(imported).bytes;
	expect(loadLevelMapBytes({QStringLiteral("imported.map"), {}, QStringLiteral("idtech3")}, text, &imported, &error), "imported reloads", error);
	expect(previewLevelMapImport(destination, imported).bytes == imported.originalText.toUtf8().size(), "UTF-8 bytes counted");
	expect(previewLevelMapImport(destination, imported, {}, 1234).bytes == 1234, "measured size used");
}

void cliTests(const QString& executable, const QString& folder)
{
	const auto settingsPath = QDir(folder).filePath(QStringLiteral("settings.ini"));
	const auto run = [&](const QStringList& args, int code = 0) {
		QProcess process;
		process.setWorkingDirectory(folder);
		process.start(executable, QStringList{"--cli", "--json", "--settings-file", settingsPath} + args);
		const bool done = process.waitForStarted() && process.waitForFinished(120000);
		const auto out = process.readAllStandardOutput(), err = process.readAllStandardError();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code, "CLI status", args.join(' ') + '\n' + QString::fromUtf8(out + err));
		if (!done) {
			process.kill();
			process.waitForFinished();
		}
		return QJsonDocument::fromJson(out).object();
	};
	// A folder package with the image of a Quake III map's classic faces.
	const auto package = QDir(folder).filePath(QStringLiteral("pkg"));
	QDir().mkpath(QDir(package).filePath(QStringLiteral("textures/studio")));
	QImage image(128, 64, QImage::Format_RGB32);
	image.fill(QColor(200, 100, 50));
	expect(image.save(QDir(package).filePath(QStringLiteral("textures/studio/checker.png"))), "package image");
	QString error;
	LevelMapDocument document;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake3");
	create.starterRoom = false;
	LevelPatchCreateRequest shape;
	shape.center = {32, 32, 128, true};
	shape.size = {64, 64, 16, true};
	shape.texture = QStringLiteral("textures/studio/checker");
	LevelMapPatch patch;
	expect(createLevelMap(create, &document, &error)
			&& addLevelMapBoxBrush(&document, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("studio/checker"), nullptr, &error)
			&& createLevelPatch(shape, &patch, &error) && addLevelMapPatch(&document, patch, nullptr, &error),
		"CLI fixture", error);
	const auto mapPath = QDir(folder).filePath(QStringLiteral("cli-q3.map"));
	LevelDocumentSaveRequest save;
	save.path = mapPath;
	save.keepBackup = false;
	expect(writeLevelDocument(document, save).succeeded(), "CLI fixture saved");
	const auto obj = QDir(folder).filePath(QStringLiteral("cli/out.obj")), mtl = QDir(folder).filePath(QStringLiteral("cli/out.mtl"));
	auto report = run({"map", "export-obj", mapPath, "--output", obj, "--dry-run"});
	expect(report["dryRun"] == true && report["written"] == false && !QFileInfo::exists(obj) && report["brushes"] == 1 && report["patches"] == 1
			&& report["unsizedMaterials"].toArray().size() == 1,
		"CLI dry run", QString::fromUtf8(QJsonDocument(report).toJson()));
	QDir().mkpath(QFileInfo(obj).absolutePath());
	report = run({"map", "export-obj", mapPath, "--output", obj, "--package", package, "--material-paths", "export"});
	QFile written(mtl);
	const auto mtlText = written.open(QIODevice::ReadOnly) ? written.readAll() : QByteArray();
	expect(report["written"] == true && QFileInfo::exists(obj) && mtlText.contains("map_Kd ../pkg/textures/studio/checker.png")
			&& mtlText.contains("Kd 0.7843 0.3922 0.1961") && report["unsizedMaterials"].toArray().isEmpty() && report["unresolvedMaterials"].toArray().isEmpty(),
		"CLI export with a package", QString::fromUtf8(mtlText) + QString::fromUtf8(QJsonDocument(report).toJson()));
	QFile objFile(obj);
	run({"map", "export-obj", mapPath, "--output", obj}, 4);
	report = run({"map", "export-obj", mapPath, "--output", obj, "--overwrite", "--object", QStringLiteral("brush:%1").arg(document.brushes.first().id), "--group",
		"material", "--triangulate", "--no-mtl", "--groups"});
	const auto selection = parseObj(objFile.open(QIODevice::ReadOnly) ? objFile.readAll() : QByteArray());
	expect(report["written"] == true && selection.groups == QStringList{QStringLiteral("studio/checker")} && selection.mtllib.isEmpty() && selection.faces.size() == 12
			&& report["materialLibrary"].toString().isEmpty(),
		"CLI selection by material in triangles without a library");
	run({"map", "export-obj", mapPath}, 2);
	run({"map", "export-obj", mapPath, "--output", obj, "--group", "sideways"}, 2);
	run({"map", "export-obj", mapPath, "--output", obj, "--material-root", folder}, 2);
	run({"map", "export-obj", mapPath, "--output", obj, "--object", "brush:0", "--include-omitted"}, 2);
	run({"map", "export-obj", mapPath, "--output", QDir(folder).filePath(QStringLiteral("cli/out.txt")), "--overwrite"}, 4);
	run({"map", "export-obj", QDir(folder).filePath(QStringLiteral("missing.map")), "--output", obj}, 3);
	// --engine idTech3 reads a classic-format box with no Quake III evidence
	// as Quake III, so its textures/ image resolves from the package.
	{
		const auto classic = QDir(folder).filePath(QStringLiteral("cli-classic.map"));
		QFile file(classic);
		expect(file.open(QIODevice::WriteOnly) && file.write(tests::placementFixture(QStringLiteral("classic"))) > 0, "classic box written");
		file.close();
		const auto classicObj = QDir(folder).filePath(QStringLiteral("cli/classic.obj"));
		report = run({"map", "export-obj", classic, "--output", classicObj, "--package", package, "--engine", "idTech3", "--dry-run"});
		expect(report["unsizedMaterials"].toArray().isEmpty() && report["brushes"] == 1, "--engine idTech3 resolves the classic map's image",
			QString::fromUtf8(QJsonDocument(report).toJson()));
		report = run({"map", "export-obj", classic, "--output", classicObj, "--package", package, "--dry-run"});
		expect(report["unsizedMaterials"].toArray().size() == 1, "without --engine the classic map reads as Quake or Quake II",
			QString::fromUtf8(QJsonDocument(report).toJson()));
	}

	// A .bsp import whose VibeMap3 (this test, standing in) exits silently.
	StudioSettings::setOverrideFilePath(settingsPath);
	{
		StudioSettings settings;
		settings.upsertCompilerToolPathOverride({QStringLiteral("vibemap3"), QCoreApplication::applicationFilePath()});
		settings.sync();
	}
	const auto destination = QDir(folder).filePath(QStringLiteral("cli-dest.map"));
	QFile dest(destination);
	expect(dest.open(QIODevice::WriteOnly) && dest.write(tests::placementFixture(QStringLiteral("brushDef"))) > 0, "import destination");
	dest.close();
	const auto bsp = writeBsp(folder, QStringLiteral("cli.bsp"), "IBSP", 47);
	report = run({"map", "import", destination, "--from", bsp, "--output", QDir(folder).filePath(QStringLiteral("cli-imported.map")), "--game", "et"}, 4);
	const auto commandLine = report["conversion"].toObject()["commandLine"].toString();
	expect(report["message"].toString().contains(QStringLiteral("code 3")) && commandLine.contains(QStringLiteral("-game et")),
		"CLI names VibeMap3's exit code and passes the game", report["message"].toString() + '\n' + commandLine);
	report = run({"map", "import", destination, "--from", writeBsp(folder, QStringLiteral("cli-q2.bsp"), "IBSP", 38), "--output",
					 QDir(folder).filePath(QStringLiteral("cli-q2.map"))}, 4);
	expect(report["message"].toString().contains(QStringLiteral("Quake II")) && !report.contains(QStringLiteral("conversion")), "CLI refuses a Quake II BSP before running");
}
} // namespace

int main(int argc, char** argv)
{
	// Standing in for VibeMap3 in cliTests: exit at once, printing nothing.
	if (argc >= 2 && argv[1][0] == '-') {
		return 3;
	}
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "temporary folder")) {
		return 1;
	}
	dialectTests(temp.path());
	optionTests(temp.path());
	writeTests(temp.path());
	conversionTests(temp.path());
	if (argc >= 2) {
		cliTests(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath(), temp.path());
	}
	std::cout << (ok ? "Map OBJ export smoke test passed." : "Map OBJ export smoke test failed.") << std::endl;
	return ok ? 0 : 1;
}
