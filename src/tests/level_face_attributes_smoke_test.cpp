// Core and command-line checks of the Face section's service
// (core/level_face_attributes.h), TrenchBroom's Face inspector: each face
// format's offset, scale and rotation read as Radiant's Surface Inspector
// reads them (a brush-primitive face from its matrix, which needs the image
// size) and written back unchanged; a summary marking values that differ
// between faces; absolute values set on every face in its own format as one
// exact undo step, with redo; a pivot that keeps its texel through a new
// rotation or scale, so a stretch doubles distances along U from it and keeps
// V; a texture move by texels; an unchanged edit that adds no undo step; and
// refusals of a zero scale, a missing image size, hidden faces, faces outside
// the open group and a locked layer, changing nothing. map face-attributes
// reads the values and writes what the shared service writes.

#include "core/level_document.h"
#include "core/level_face_attributes.h"
#include "core/level_materials.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_uv_edit.h"
#include "core/level_visibility.h"
#include "tests/level_material_test_helpers.h"
#include "tests/level_surface_test_helpers.h"

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
QString phase;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << qPrintable(phase) << ": " << label << ": " << qPrintable(detail) << std::endl;
	}
	return value;
}

bool near(double a, double b, double tolerance = 1e-6)
{
	return std::abs(a - b) <= tolerance;
}

bool nearPoint(QPointF a, QPointF b, double tolerance = 1e-6)
{
	return near(a.x(), b.x(), tolerance) && near(a.y(), b.y(), tolerance);
}

QString describe(const LevelTextureValues& v)
{
	return QStringLiteral("%1 %2 %3 %4 %5").arg(v.shiftU).arg(v.shiftV).arg(v.scaleU).arg(v.scaleV).arg(v.rotation);
}

bool load(const QByteArray& bytes, const QString& engine, LevelMapDocument* document)
{
	QString error;
	return expect(loadLevelMapBytes({QStringLiteral("attributes.map"), {}, engine}, bytes, document, &error), "load fixture", error);
}

QVector<LevelSurfaceFace> brushFaces(const LevelMapDocument& document, int brushIndex = 0)
{
	QVector<LevelSurfaceFace> faces;
	const LevelMapBrush& brush = document.brushes[brushIndex];
	for (int i = 0; i < brush.faces.size(); ++i) {
		faces.append({brush.id, i});
	}
	return faces;
}

QString engineFor(const QString& dialect)
{
	return dialect == QStringLiteral("classic") ? QStringLiteral("idtech2") : QStringLiteral("idtech3");
}

const QSize kImage(64, 64);
// Keys in any case or slash direction find the material.
QHash<QString, QSize> sizes()
{
	return {{QStringLiteral("STUDIO\\Grid"), kImage}};
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "temporary directory")) {
		return 1;
	}
	QString error;
	const QStringList dialects {QStringLiteral("classic"), QStringLiteral("valve220"), QStringLiteral("brushDef")};

	phase = QStringLiteral("values in each format");
	{
		LevelMapDocument classic, valve, primitive;
		load(tests::surfaceFixture(QStringLiteral("classic")), QStringLiteral("idtech2"), &classic);
		load(tests::surfaceFixture(QStringLiteral("valve220")), QStringLiteral("idtech3"), &valve);
		load(tests::surfaceFixture(QStringLiteral("brushDef")), QStringLiteral("idtech3"), &primitive);
		LevelTextureValues values;
		expect(levelTextureValues(classic.brushes.first().faces.first(), {}, &values) && values.shiftU == 7 && values.shiftV == 9 && values.scaleU == -0.5
				&& values.scaleV == 2 && values.rotation == 17,
			"a classic face's own numbers", describe(values));
		expect(levelTextureValues(valve.brushes.first().faces.first(), {}, &values) && values.shiftU == 7 && values.shiftV == 9 && values.scaleU == -0.5
				&& values.scaleV == 2 && values.rotation == 0,
			"a Valve 220 face's offsets, scales and rotation", describe(values));
		const LevelMapBrushFace& matrix = primitive.brushes.first().faces.first();
		expect(!levelTextureValues(matrix, {}, &values, &error) && error.contains(QStringLiteral("image size")), "a brush-primitive face needs its image size",
			error);
		expect(levelTextureValues(matrix, kImage, &values, &error) && near(values.shiftU, 0.125 * -64) && near(values.shiftV, -0.75 * 64) && values.scaleU < 0
				&& values.scaleV > 0 && std::isfinite(values.rotation),
			"a brush-primitive face's fake shift, scale and turn", describe(values) + error);
		// Written back, the values change nothing.
		LevelTextureResetRequest all;
		all.setShiftU = all.setShiftV = all.setScaleU = all.setScaleV = all.setRotation = true;
		all.shiftU = values.shiftU;
		all.shiftV = values.shiftV;
		all.scaleU = values.scaleU;
		all.scaleV = values.scaleV;
		all.rotation = values.rotation;
		LevelMapBrushFace written = matrix;
		bool same = assignLevelTextureValues(&written, all, kImage, &error);
		for (int i = 0; i < 6 && same; ++i) {
			same = near(written.textureMatrix[i], matrix.textureMatrix[i], 1e-12);
		}
		expect(same, "reading and writing a matrix's values leaves it as it was", error);
	}

	phase = QStringLiteral("summary");
	{
		LevelMapDocument document;
		load(tests::surfaceFixture(QStringLiteral("classic")), QStringLiteral("idtech2"), &document);
		const auto faces = brushFaces(document);
		LevelFaceAttributesSummary summary = levelFaceAttributesSummary(document, faces);
		expect(summary.faces == 6 && summary.readable == 6 && summary.classic == 6 && !summary.shiftUMixed && !summary.scaleVMixed && !summary.rotationMixed
				&& summary.values.rotation == 17 && summary.material == QStringLiteral("studio/grid") && !summary.materialMixed,
			"six classic faces share their values");
		LevelFaceAttributesEdit edit;
		edit.setShiftU = true;
		edit.shiftU = 3;
		expect(setLevelMapFaceAttributes(&document, {faces.first()}, edit, {}, nullptr, &error), "one face's offset", error);
		summary = levelFaceAttributesSummary(document, faces + QVector<LevelSurfaceFace> {faces.first(), {999, 0}});
		expect(summary.faces == 6 && summary.shiftUMixed && !summary.shiftVMixed && !summary.scaleUMixed && !summary.rotationMixed && summary.values.shiftU == 3,
			"a differing offset is mixed; repeated and missing faces are skipped");
		LevelMapDocument primitive;
		load(tests::surfaceFixture(QStringLiteral("brushDef")), QStringLiteral("idtech3"), &primitive);
		summary = levelFaceAttributesSummary(primitive, brushFaces(primitive));
		expect(summary.readable == 0 && summary.unreadable == 6 && summary.brushPrimitive == 6 && !summary.problem.isEmpty(),
			"brush-primitive faces without an image size are unreadable", summary.problem);
		summary = levelFaceAttributesSummary(primitive, brushFaces(primitive), sizes());
		expect(summary.readable == 6 && summary.unreadable == 0, "with the image size they read, whatever the key's case and slashes");
	}

	for (const QString& dialect : dialects) {
		phase = QStringLiteral("set values: %1").arg(dialect);
		LevelMapDocument document;
		load(tests::surfaceFixture(dialect), engineFor(dialect), &document);
		const auto faces = brushFaces(document);
		QVector<LevelTextureValues> before;
		for (const auto& face : document.brushes.first().faces) {
			LevelTextureValues values;
			levelTextureValues(face, kImage, &values);
			before.append(values);
		}
		const QByteArray original = serializeLevelMap(document).bytes;
		const auto depth = document.undoStack.size();
		LevelFaceAttributesEdit edit;
		edit.setScaleU = true;
		edit.scaleU = 0.25;
		edit.setRotation = true;
		edit.rotation = 30;
		LevelFaceAttributesReport report;
		expect(setLevelMapFaceAttributes(&document, faces, edit, sizes(), &report, &error) && report.changedFaces == 6 && report.brushes == 1,
			"scale and rotation on six faces", error);
		bool set = true;
		QString detail;
		LevelMapDocument unchanged;
		load(tests::surfaceFixture(dialect), engineFor(dialect), &unchanged);
		for (int i = 0; i < faces.size(); ++i) {
			LevelTextureValues values;
			const LevelMapBrushFace& face = document.brushes.first().faces[i];
			const bool read = levelTextureValues(face, kImage, &values);
			if (dialect == QStringLiteral("brushDef")) {
				// Radiant's reading ties a matrix's U scale sign to its turn
				// (TexMatToFakeTexCoords), so a turn past 90 degrees can read
				// back as the same texture with both scales mirrored: the
				// matrix is what Reset Textures writes, offsets kept.
				LevelTextureResetRequest request;
				request.setScaleU = request.setRotation = true;
				request.scaleU = 0.25;
				request.rotation = 30;
				LevelMapBrushFace wanted = unchanged.brushes.first().faces[i];
				set &= assignLevelTextureValues(&wanted, request, kImage) && wanted.textureMatrix == face.textureMatrix && read
					&& near(std::abs(values.scaleU), 0.25) && near(values.shiftU, before[i].shiftU) && near(values.shiftV, before[i].shiftV);
			} else {
				set &= read && near(values.scaleU, 0.25) && near(values.rotation, 30, 1e-6) && near(values.scaleV, before[i].scaleV)
					&& near(values.shiftU, before[i].shiftU) && near(values.shiftV, before[i].shiftV);
			}
			if (!set && detail.isEmpty()) {
				detail = describe(values) + QStringLiteral(" from ") + describe(before[i]);
			}
		}
		expect(set, "each face takes the new values and keeps the others", detail);
		expect(document.undoStack.size() == depth + 1 && document.undoStack.last().description.contains(QStringLiteral("texture values")),
			"one undo step named for the values", document.undoStack.isEmpty() ? QString() : document.undoStack.last().description);
		const QByteArray edited = serializeLevelMap(document).bytes;
		expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == original, "undo restores the exact map", error);
		expect(redoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == edited, "redo makes it again", error);

		phase = QStringLiteral("pivot: %1").arg(dialect);
		LevelUvLayout layout;
		expect(readLevelUvLayout(document, {LevelMaterialKind::BrushFace, document.brushes.first().id, 0}, kImage, &layout, &error), "the face's layout", error);
		if (layout.points.size() >= 3) {
			const LevelMapVec3 pivot = layout.points[1];
			LevelMapVec3 world;
			expect(levelFaceWorldAtTexel(layout.points, layout.uv, layout.uv[2], &world) && near(world.x, layout.points[2].x, 1e-6)
					&& near(world.y, layout.points[2].y, 1e-6) && near(world.z, layout.points[2].z, 1e-6),
				"a point's texel leads back to the point");
			expect(!levelFaceWorldAtTexel(layout.points.mid(0, 2), layout.uv.mid(0, 2), layout.uv[0], &world), "two points span no plane");
			const LevelMapBrushFace face = document.brushes.first().faces.first();
			LevelTextureValues values;
			levelTextureValues(face, kImage, &values);
			QPointF start;
			levelFaceTexelAt(face, kImage, pivot, &start);
			// A turn about the pivot.
			LevelFaceAttributesEdit turn;
			turn.setRotation = true;
			turn.rotation = values.rotation + 40;
			turn.keepPivot = true;
			turn.pivot = pivot;
			LevelMapBrushFace turned;
			QPointF after;
			expect(applyLevelFaceAttributesEdit(face, turn, kImage, &turned, &error) && levelFaceTexelAt(turned, kImage, pivot, &after)
					&& nearPoint(after, start, 1e-3),
				"a turn keeps the pivot's texel",
				QStringLiteral("%1,%2 -> %3,%4 %5").arg(start.x()).arg(start.y()).arg(after.x()).arg(after.y()).arg(error));
			// A stretch along U about the pivot doubles distances along U.
			LevelFaceAttributesEdit stretch;
			stretch.setScaleU = true;
			stretch.scaleU = values.scaleU / 2;
			stretch.keepPivot = true;
			stretch.pivot = pivot;
			LevelMapBrushFace stretched;
			bool doubled = applyLevelFaceAttributesEdit(face, stretch, kImage, &stretched, &error);
			for (const LevelMapVec3& point : std::as_const(layout.points)) {
				QPointF was, now;
				doubled &= levelFaceTexelAt(face, kImage, point, &was) && levelFaceTexelAt(stretched, kImage, point, &now)
					&& near(now.x() - start.x(), 2 * (was.x() - start.x()), 1e-3) && near(now.y(), was.y(), 1e-3);
			}
			expect(doubled, "a stretch doubles distances along U from the pivot and keeps V", error);
			// A move adds texels to every point.
			LevelFaceAttributesEdit move;
			move.moveTexels = {8, -4};
			LevelMapBrushFace moved;
			bool shifted = applyLevelFaceAttributesEdit(face, move, kImage, &moved, &error);
			for (const LevelMapVec3& point : std::as_const(layout.points)) {
				QPointF was, now;
				shifted &= levelFaceTexelAt(face, kImage, point, &was) && levelFaceTexelAt(moved, kImage, point, &now) && nearPoint(now, was + QPointF(8, -4), 1e-6);
			}
			expect(shifted, "a move adds its texels to every point", error);
			// Through the map: one undo step.
			const auto steps = document.undoStack.size();
			expect(setLevelMapFaceAttributes(&document, {{document.brushes.first().id, 0}}, turn, sizes(), nullptr, &error)
					&& document.undoStack.size() == steps + 1,
				"a pivot turn is one undo step", error);
		}
	}

	phase = QStringLiteral("unchanged and refused edits");
	{
		LevelMapDocument document;
		load(tests::surfaceFixture(QStringLiteral("classic")), QStringLiteral("idtech2"), &document);
		const auto faces = brushFaces(document);
		const QByteArray original = serializeLevelMap(document).bytes;
		const auto revision = document.revision;
		const auto depth = document.undoStack.size();
		LevelFaceAttributesEdit same;
		same.setRotation = true;
		same.rotation = 17;
		LevelFaceAttributesReport report;
		expect(setLevelMapFaceAttributes(&document, faces, same, {}, &report, &error) && report.changedFaces == 0 && document.revision == revision
				&& document.undoStack.size() == depth && serializeLevelMap(document).bytes == original,
			"values the faces already have add no undo step", error);
		const auto refuse = [&](const LevelMapDocument& map, const QVector<LevelSurfaceFace>& targets, const LevelFaceAttributesEdit& edit,
								const QHash<QString, QSize>& textureSizes, const char* label, const char* reason) {
			LevelMapDocument copy = map;
			const QByteArray bytes = serializeLevelMap(copy).bytes;
			const auto steps = copy.undoStack.size();
			QString problem;
			expect(!setLevelMapFaceAttributes(&copy, targets, edit, textureSizes, nullptr, &problem) && problem.contains(QString::fromLatin1(reason))
					&& serializeLevelMap(copy).bytes == bytes && copy.undoStack.size() == steps,
				label, problem);
		};
		LevelFaceAttributesEdit zero;
		zero.setScaleV = true;
		zero.scaleV = 0;
		refuse(document, faces, zero, {}, "a zero scale is refused", "cannot be 0");
		refuse(document, faces, {}, {}, "an empty edit is refused", "Give an offset");
		refuse(document, {{document.brushes.first().id, 9}}, same, {}, "a missing face is refused", "has no face");
		LevelMapDocument primitive;
		load(tests::surfaceFixture(QStringLiteral("brushDef")), QStringLiteral("idtech3"), &primitive);
		LevelFaceAttributesEdit scale;
		scale.setScaleU = true;
		scale.scaleU = 0.5;
		refuse(primitive, brushFaces(primitive), scale, {}, "a brush-primitive face without its image size is refused", "image size");
	}

	phase = QStringLiteral("hidden, open groups and locks");
	{
		LevelMapDocument document;
		load(tests::surfaceFixture(QStringLiteral("classic")), QStringLiteral("idtech2"), &document);
		int second = -1;
		expect(addLevelMapBoxBrush(&document, {256, 0, 0, true}, {320, 64, 64, true}, QStringLiteral("studio/grid"), &second, &error), "a second brush", error);
		const int first = document.brushes.first().id;
		LevelFaceAttributesEdit edit;
		edit.setShiftV = true;
		edit.shiftV = 32;
		const auto check = [&](const QVector<LevelSurfaceFace>& targets, const char* label, const char* reason) {
			const QByteArray before = serializeLevelMap(document).bytes;
			const auto depth = document.undoStack.size();
			QString problem;
			expect(!setLevelMapFaceAttributes(&document, targets, edit, {}, nullptr, &problem) && problem.contains(QString::fromLatin1(reason))
					&& serializeLevelMap(document).bytes == before && document.undoStack.size() == depth,
				label, problem);
		};
		setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, first}});
		LevelVisibilityRequest hide;
		expect(changeLevelMapVisibility(&document, hide, nullptr, &error), "hide the first brush", error);
		check({{first, 0}}, "a hidden face is refused", "hidden");
		LevelVisibilityRequest show;
		show.operation = LevelVisibilityOperation::ShowAll;
		expect(changeLevelMapVisibility(&document, show, nullptr, &error), "show all", error);
		QString group;
		expect(createLevelSceneNode(&document, LevelSceneNodeKind::Group, QStringLiteral("Room"), {}, &group, &error)
				&& assignLevelSceneObjects(&document, group, {QStringLiteral("brush:%1").arg(first)}, &error) && openLevelSceneGroup(&document, group, &error),
			"open a group holding the first brush", error);
		check({{second, 0}}, "a face outside the open group is refused", "outside the open group");
		expect(setLevelMapFaceAttributes(&document, {{first, 0}}, edit, {}, nullptr, &error), "a face inside the open group changes", error);
		expect(closeLevelSceneGroup(&document, true, &error), "close the group", error);
		QString layer;
		expect(createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
				&& assignLevelSceneObjects(&document, layer, {QStringLiteral("brush:%1").arg(second)}, &error) && setLevelSceneLocked(&document, layer, true, &error),
			"lock the second brush", error);
		check({{first, 1}, {second, 0}}, "a locked face refuses the whole edit", "locked");
	}

	if (argc > 1) {
		phase = QStringLiteral("cli");
		const QString binary = QString::fromLocal8Bit(argv[1]);
		const QString settings = QDir(temp.path()).filePath(QStringLiteral("settings.ini"));
		const auto run = [&](const QStringList& arguments, int* exitCode) {
			QProcess process;
			process.start(binary, QStringList {QStringLiteral("--settings-file"), settings, QStringLiteral("--cli"), QStringLiteral("map"),
									  QStringLiteral("face-attributes")}
					+ arguments + QStringList {QStringLiteral("--json")});
			const bool ended = process.waitForFinished(60000);
			*exitCode = ended && process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
			return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
		};
		const auto out = [&](const char* name) { return QDir(temp.path()).filePath(QString::fromLatin1(name)); };
		const auto read = [](const QString& path) {
			QFile file(path);
			return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
		};
		const auto compact = [](const QJsonObject& json) { return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)); };
		const QString classic = out("classic.map"), primitive = out("primitive.map");
		tests::putMaterialFile(classic, tests::surfaceFixture(QStringLiteral("classic")));
		tests::putMaterialFile(primitive, tests::surfaceFixture(QStringLiteral("brushDef")));
		const QByteArray original = read(classic);
		int code = -1;
		// Read only.
		auto json = run({classic, QStringLiteral("--engine"), QStringLiteral("idtech2"), QStringLiteral("--face"), QStringLiteral("0:1")}, &code);
		const auto row = json.value(QStringLiteral("faces")).toArray().at(0).toObject();
		const auto values = row.value(QStringLiteral("values")).toObject();
		expect(code == 0 && !json.value(QStringLiteral("mapWritten")).toBool() && row.value(QStringLiteral("face")).toString() == QStringLiteral("0:1")
				&& row.value(QStringLiteral("format")).toString() == QStringLiteral("classic") && values.value(QStringLiteral("offsetU")).toDouble() == 7
				&& values.value(QStringLiteral("scaleU")).toDouble() == -0.5 && values.value(QStringLiteral("rotation")).toDouble() == 17
				&& json.value(QStringLiteral("summary")).toObject().value(QStringLiteral("rotation")).toDouble() == 17,
			"a face's values", compact(json));
		json = run({primitive, QStringLiteral("--engine"), QStringLiteral("idtech3"), QStringLiteral("--object"), QStringLiteral("brush:0")}, &code);
		expect(code == 0 && json.value(QStringLiteral("summary")).toObject().value(QStringLiteral("unreadable")).toInt() == 6
				&& json.value(QStringLiteral("faces")).toArray().at(0).toObject().value(QStringLiteral("values")).isNull(),
			"brush-primitive values need the image size", compact(json));
		// A change writes what the service writes.
		LevelMapDocument expected;
		load(original, QStringLiteral("idtech2"), &expected);
		LevelFaceAttributesEdit edit;
		edit.setScaleU = edit.setScaleV = true;
		edit.scaleU = edit.scaleV = 0.25;
		expect(setLevelMapFaceAttributes(&expected, brushFaces(expected), edit, {}, nullptr, &error), "reference edit", error);
		const QStringList change {classic, QStringLiteral("--engine"), QStringLiteral("idtech2"), QStringLiteral("--object"), QStringLiteral("brush:0"),
			QStringLiteral("--scale-u"), QStringLiteral("0.25"), QStringLiteral("--scale-v"), QStringLiteral("0.25")};
		json = run(change + QStringList {QStringLiteral("--output"), out("dry.map"), QStringLiteral("--dry-run")}, &code);
		expect(code == 0 && !QFile::exists(out("dry.map")) && read(classic) == original && json.value(QStringLiteral("changedFaces")).toInt() == 6,
			"a dry run writes nothing", compact(json));
		json = run(change + QStringList {QStringLiteral("--output"), out("fine.map")}, &code);
		LevelMapDocument written;
		load(read(out("fine.map")), QStringLiteral("idtech2"), &written);
		expect(code == 0 && json.value(QStringLiteral("changedFaces")).toInt() == 6
				&& json.value(QStringLiteral("set")).toObject().value(QStringLiteral("scaleU")).toDouble() == 0.25
				&& serializeLevelMap(written).bytes == serializeLevelMap(expected).bytes && read(classic) == original,
			"the change matches the shared service", compact(json));
		// A turn about a pivot, with a move, on one face.
		LevelMapDocument pivoted;
		load(original, QStringLiteral("idtech2"), &pivoted);
		LevelFaceAttributesEdit turn;
		turn.setRotation = true;
		turn.rotation = 30;
		turn.keepPivot = true;
		turn.pivot = {0, 64, 32, true};
		turn.moveTexels = {2, -3};
		expect(setLevelMapFaceAttributes(&pivoted, {{0, 1}}, turn, {}, nullptr, &error), "reference turn", error);
		json = run({classic, QStringLiteral("--engine"), QStringLiteral("idtech2"), QStringLiteral("--face"), QStringLiteral("0:2"), QStringLiteral("--rotation"),
					   QStringLiteral("30"), QStringLiteral("--pivot"), QStringLiteral("0,64,32"), QStringLiteral("--move"), QStringLiteral("2,-3"),
					   QStringLiteral("--output"), out("turned.map")},
			&code);
		LevelMapDocument turned;
		load(read(out("turned.map")), QStringLiteral("idtech2"), &turned);
		expect(code == 0 && json.value(QStringLiteral("changedFaces")).toInt() == 1 && json.value(QStringLiteral("pivot")).toArray().size() == 3
				&& serializeLevelMap(turned).bytes == serializeLevelMap(pivoted).bytes,
			"--rotation with --pivot and --move matches the service", compact(json));
		// The image size from the command line.
		json = run({primitive, QStringLiteral("--engine"), QStringLiteral("idtech3"), QStringLiteral("--object"), QStringLiteral("brush:0"),
					   QStringLiteral("--scale-u"), QStringLiteral("0.5"), QStringLiteral("--output"), out("bp.map")},
			&code);
		expect(code == 4 && !QFile::exists(out("bp.map")), "brush-primitive faces refuse without an image size", compact(json));
		json = run({primitive, QStringLiteral("--engine"), QStringLiteral("idtech3"), QStringLiteral("--object"), QStringLiteral("brush:0"),
					   QStringLiteral("--scale-u"), QStringLiteral("0.5"), QStringLiteral("--texture-size"), QStringLiteral("64,64"), QStringLiteral("--output"),
					   out("bp.map")},
			&code);
		expect(code == 0 && json.value(QStringLiteral("changedFaces")).toInt() == 6 && QFile::exists(out("bp.map")), "and take --texture-size", compact(json));
		// Usage errors.
		json = run({classic, QStringLiteral("--face"), QStringLiteral("0:1"), QStringLiteral("--pivot"), QStringLiteral("0,0,0"), QStringLiteral("--move"),
					   QStringLiteral("1,1"), QStringLiteral("--output"), out("bad.map")},
			&code);
		expect(code == 2 && !QFile::exists(out("bad.map")), "--pivot needs a new scale or rotation");
		json = run({classic, QStringLiteral("--face"), QStringLiteral("0:1"), QStringLiteral("--rotation"), QStringLiteral("30")}, &code);
		expect(code == 2, "a change needs --output");
		json = run({classic, QStringLiteral("--face"), QStringLiteral("0:1"), QStringLiteral("--scale-u"), QStringLiteral("0"), QStringLiteral("--output"),
					   out("zero.map")},
			&code);
		expect(code == 4 && !QFile::exists(out("zero.map")), "a zero scale is refused");
	}

	std::cout << (ok ? "Face attributes checks passed" : "Face attributes checks failed") << std::endl;
	return ok ? 0 : 1;
}
