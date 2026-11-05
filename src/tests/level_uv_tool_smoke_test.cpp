// Core and command-line checks of the camera UV tool's service
// (core/level_uv_tool.h), NetRadiant Custom's UV tool: the plane frame that
// places texels on a face exactly and on a patch by least squares; the repeat
// lines and their limit; the snapping of a pivot, a move, a stretch, a skew
// and a turn; a face's move, turn and stretch as its own texture values (a
// classic face stays classic, the pivot keeps its texel and the U direction
// turns as far as asked), and a skew that a classic face takes only by
// becoming a Valve 220 face; a patch's whole-texture edits and its row,
// column and point moves; each edit one exact undo step with redo, nothing
// for an edit that changes nothing, and refusals of a stale read, a hidden
// brush, a brush outside the open group and a locked layer. `map edit-uv
// --stretch/--skew-u/--rows` writes what the service writes.

#include "core/level_document.h"
#include "core/level_patch.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_uv_tool.h"
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
#include <numbers>

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

bool nearVec(const LevelMapVec3& a, const LevelMapVec3& b, double tolerance = 1e-6)
{
	return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

QString text(QPointF p)
{
	return QStringLiteral("%1, %2").arg(p.x(), 0, 'g', 12).arg(p.y(), 0, 'g', 12);
}

QString texts(const QVector<QPointF>& points)
{
	QStringList list;
	for (const QPointF& p : points) {
		list << text(p);
	}
	return list.join(QStringLiteral("; "));
}

// Degrees anticlockwise about `axis` from `a` to `b`.
double angleAbout(const LevelMapVec3& a, const LevelMapVec3& b, const LevelMapVec3& axis)
{
	const auto dot = [](const LevelMapVec3& x, const LevelMapVec3& y) { return x.x * y.x + x.y * y.y + x.z * y.z; };
	const LevelMapVec3 c {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, true};
	return std::atan2(dot(c, axis), dot(a, b)) * 180.0 / std::numbers::pi;
}

bool load(const QByteArray& bytes, const QString& engine, LevelMapDocument* document)
{
	QString error;
	return expect(loadLevelMapBytes({QStringLiteral("uv-tool.map"), {}, engine}, bytes, document, &error), "load fixture", error);
}

QString engineFor(const QString& dialect)
{
	return dialect == QStringLiteral("classic") ? QStringLiteral("idtech2") : QStringLiteral("idtech3");
}

const QSize kImage(64, 64);

LevelUvLayout faceLayout(const LevelMapDocument& document, int face = 0, QSize size = kImage)
{
	LevelUvLayout layout;
	QString error;
	expect(readLevelUvLayout(document, {LevelMaterialKind::BrushFace, document.brushes.first().id, face}, size, &layout, &error), "read the face",
		error);
	return layout;
}

QByteArray patchFixture(int* patchId)
{
	LevelMapDocument created;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake3");
	create.starterRoom = false;
	QString error;
	createLevelMap(create, &created, &error);
	LevelPatchCreateRequest request;
	request.shape = QStringLiteral("plane");
	request.texture = QStringLiteral("studio/grid");
	request.size = {256, 128, 128, true};
	LevelMapPatch plane;
	expect(createLevelPatch(request, &plane, &error) && addLevelMapPatch(&created, plane, patchId, &error), "patch fixture", error);
	return serializeLevelMap(created).bytes;
}

// Applies `edit` as one undo step, undoes it exactly and redoes it.
bool roundTrip(LevelMapDocument* document, const LevelUvLayout& layout, const LevelUvToolEdit& edit, LevelUvToolResult* result, const char* label)
{
	const QByteArray before = serializeLevelMap(*document).bytes;
	const auto depth = document->undoStack.size();
	QString error;
	if (!expect(applyLevelUvToolEdit(document, layout, edit, result, &error), label, error)) {
		return false;
	}
	const QByteArray after = serializeLevelMap(*document).bytes;
	expect(document->undoStack.size() == depth + 1 && after != before, "one undo step", QString::fromLatin1(label));
	expect(undoLevelMapEdit(document, &error) && serializeLevelMap(*document).bytes == before, "undo puts it back exactly", QString::fromLatin1(label));
	expect(redoLevelMapEdit(document, &error) && serializeLevelMap(*document).bytes == after, "redo applies it again", QString::fromLatin1(label));
	return true;
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

	phase = QStringLiteral("frame");
	{
		LevelMapDocument document;
		load(tests::surfaceFixture(QStringLiteral("classic")), QStringLiteral("idtech2"), &document);
		for (int face = 0; face < document.brushes.first().faces.size(); ++face) {
			const LevelUvLayout layout = faceLayout(document, face);
			const LevelUvToolFrame frame = levelUvToolFrame(layout.points, layout.uv);
			bool exact = frame.valid;
			for (qsizetype i = 0; i < layout.points.size(); ++i) {
				exact &= nearVec(frame.worldAt(layout.uv[i]), layout.points[i], 1e-6) && nearPoint(frame.texelAt(layout.points[i]), layout.uv[i], 1e-6);
			}
			expect(exact, "a face's frame places its texels exactly, both ways", QString::number(face));
			// A ray along the normal meets the plane at the point it starts above.
			const LevelMapVec3 at = layout.points.first();
			const LevelMapVec3 above {at.x + frame.normal.x * 50, at.y + frame.normal.y * 50, at.z + frame.normal.z * 50, true};
			LevelMapVec3 hit;
			expect(frame.hit(above, {-frame.normal.x, -frame.normal.y, -frame.normal.z, true}, &hit) && nearVec(hit, at, 1e-6)
					&& !frame.hit(above, frame.normal, &hit),
				"a ray meets the plane in front only", QString::number(face));
			// A quarter turn in texels is a quarter turn on the plane.
			expect(near(std::abs(frame.turnDegrees(layout.uv[0], layout.uv[0] + QPointF(16, 0), layout.uv[0] + QPointF(0, 16))), 90, 1e-6),
				"turn angles measure on the plane");
		}
		expect(!levelUvToolFrame({{0, 0, 0, true}, {1, 0, 0, true}, {2, 0, 0, true}}, {{0, 0}, {1, 0}, {2, 0}}).valid, "collinear texels have no frame");
	}

	phase = QStringLiteral("grid and snapping");
	{
		const QVector<QPointF> uv {{10, 10}, {50, 10}, {50, 40}, {10, 40}};
		const LevelUvToolGrid grid = levelUvToolGrid(uv, QSizeF(64, 32), QPointF(10, 40));
		expect(grid.stepS == 64 && grid.stepT == 32 && grid.s == QVector<double>({0, 64}) && grid.t == QVector<double>({0, 32, 64}),
			"repeat lines cover the surface on whole repeats");
		const LevelUvToolGrid many = levelUvToolGrid({{0, 0}, {10000, 10}, {0, 10}}, QSizeF(1, 1), QPointF(), 40);
		expect(many.s.size() <= 41 && many.stepS > 1 && std::fmod(many.stepS, 1.0) == 0 && many.maxS >= 10000, "a dense grid keeps to its limit",
			QString::number(many.s.size()));
		LevelUvToolSnap snap;
		snap.tolerance = {4, 4};
		expect(levelUvToolSnapPivot(uv, grid, {3, 61}, snap) == QPointF(0, 64), "the pivot snaps to the lines it is near");
		expect(levelUvToolSnapPivot(uv, grid, {48, 20}, snap) == QPointF(50, 20), "the pivot snaps to a corner's coordinate");
		expect(levelUvToolSnapPivot(uv, grid, {30, 20}, snap) == QPointF(30, 20), "a pivot away from every line stays");
		snap.hard = true;
		expect(levelUvToolSnapPivot(uv, grid, {30, 20}, snap) == QPointF(10, 10), "Ctrl snaps the pivot to the nearest line or corner");
		snap.hard = false;
		// The texture follows the pointer; a corner lands on a line when close.
		expect(levelUvToolMove(uv, grid, QPointF(), {0, 0}, {-52, 0}, snap, false, {}) == QPointF(54, 0), "a move snaps a corner onto a repeat line",
			text(levelUvToolMove(uv, grid, QPointF(), {0, 0}, {-52, 0}, snap, false, {})));
		expect(levelUvToolMove(uv, grid, QPointF(), {0, 0}, {-30, -3}, {}, false, {}) == QPointF(30, 3), "without tolerance a move is the pointer's");
		expect(levelUvToolMove(uv, grid, QPointF(), {0, 0}, {-30, -3}, {}, true, {}) == QPointF(30, 0), "Shift keeps the axis the pointer moved further");
		expect(levelUvToolPointMove({{10, 10}}, grid, QPointF(40, 40), {0, 0}, {29, 0}, snap, false, {}) == QPointF(30, 0),
			"a control point snaps onto the pivot's coordinate");
		// A stretch brings the dragged line under the pointer.
		expect(near(levelUvToolStretch({0, 32}, 0, 64, 32, 0, false), 2), "a stretch brings the line to the pointer");
		expect(near(levelUvToolStretch({0, 32}, 0, 64, 30, 4, false), 2), "a corner meets the line when close");
		expect(near(levelUvToolStretch({0, 32}, 0, 64, 30, 0, false), 64.0 / 30), "a stretch away from every corner stays");
		expect(near(levelUvToolStretch({}, 0, 64, -5, 0, false), 64), "past the pivot the texture is as small as it goes");
		// A skew lines the texture up with an edge when close.
		const QVector<QPair<QPointF, QPointF>> edges {{{0, 0}, {32, 32}}};
		const double skew = levelUvToolSkew(LevelUvToolHandle::SkewS, {}, edges, grid, {0, 0}, {10, 32}, {40, 32}, snap);
		expect(near(skew, -1), "a skew snaps along an edge", QString::number(skew));
		snap.tolerance = {0, 0};
		expect(near(levelUvToolSkew(LevelUvToolHandle::SkewS, {}, edges, grid, {0, 0}, {10, 32}, {40, 32}, snap), -30.0 / 32), "an unsnapped skew");
		expect(near(levelUvToolSkew(LevelUvToolHandle::SkewT, {}, {}, grid, {0, 0}, {32, 10}, {32, 26}, snap), -0.5), "a skew along V");
	}

	phase = QStringLiteral("turn snapping");
	{
		LevelMapDocument document;
		load(tests::surfaceFixture(QStringLiteral("valve220")), QStringLiteral("idtech3"), &document);
		// An unrotated face: its texture axes run along its edges.
		const LevelUvLayout layout = faceLayout(document);
		const LevelUvToolFrame frame = levelUvToolFrame(layout.points, layout.uv);
		const auto edges = levelUvToolEdges(layout);
		expect(edges.size() == layout.uv.size(), "a face's edges are its outline");
		expect(near(levelUvToolSnapTurn(frame, edges, 1.5, 0, 2, false), 0, 1e-9), "a turn near an edge lines up with it");
		expect(near(levelUvToolSnapTurn(frame, edges, 10, 0, 2, false), 10, 1e-9), "a turn away from every edge stays");
		expect(near(levelUvToolSnapTurn(frame, edges, 50, 0, 2, true), 90, 1e-9), "Ctrl lines it up with the nearest edge");
		expect(near(levelUvToolSnapTurn(frame, edges, 22, 15, 2, false), 15, 1e-9), "Shift turns in steps");
	}

	const QStringList dialects {QStringLiteral("classic"), QStringLiteral("valve220"), QStringLiteral("brushDef")};
	for (const QString& dialect : dialects) {
		phase = QStringLiteral("face %1").arg(dialect);
		LevelMapDocument document;
		load(tests::surfaceFixture(dialect), engineFor(dialect), &document);
		const LevelUvLayout layout = faceLayout(document);
		const LevelUvToolFrame frame = levelUvToolFrame(layout.points, layout.uv);
		const QPointF pivot = layout.uv[0];
		const LevelMapVec3 pivotWorld = frame.worldAt(pivot);
		const auto texelOf = [&](const LevelMapBrushFace& face, const LevelMapVec3& world) {
			QPointF texel;
			levelFaceTexelAt(face, kImage, world, &texel);
			return texel;
		};
		LevelUvToolResult result;
		// Move.
		LevelUvToolEdit move;
		move.handle = LevelUvToolHandle::Move;
		move.move = {8, 4};
		if (roundTrip(&document, layout, move, &result, "move")) {
			bool moved = result.values && result.uv.size() == layout.uv.size();
			for (qsizetype i = 0; moved && i < layout.uv.size(); ++i) {
				moved &= nearPoint(result.uv[i], layout.uv[i] + QPointF(8, 4), 1e-6);
			}
			expect(moved, "every point gains the move", texts(result.uv));
			expect(undoLevelMapEdit(&document, &error), "back to the start", error);
		}
		// Turn: the U direction turns as far as asked, about the pivot.
		for (const double degrees : {90.0, -35.0}) {
			LevelUvToolEdit turn;
			turn.handle = LevelUvToolHandle::Turn;
			turn.pivot = pivot;
			turn.turn = degrees;
			if (roundTrip(&document, layout, turn, &result, "turn")) {
				const LevelUvToolFrame after = levelUvToolFrame(layout.points, result.uv);
				const LevelMapBrushFace face = document.brushes.first().faces.first();
				expect(result.values && result.valuesEdit.setRotation && near(angleAbout(frame.alongS, after.alongS, frame.normal), degrees, 0.01),
					"the texture turns as far as asked", QString::number(angleAbout(frame.alongS, after.alongS, frame.normal)));
				expect(nearPoint(texelOf(face, pivotWorld), pivot, 1e-3), "the pivot keeps its texel", text(texelOf(face, pivotWorld)));
				expect(face.explicitTextureAxes == layout.face.explicitTextureAxes && face.explicitTextureMatrix == layout.face.explicitTextureMatrix,
					"the face keeps its format");
				expect(undoLevelMapEdit(&document, &error), "back to the start", error);
			}
		}
		// Stretch: twice the texels along U from the pivot, V unchanged.
		LevelUvToolEdit stretch;
		stretch.handle = LevelUvToolHandle::StretchS;
		stretch.pivot = pivot;
		stretch.stretch = {2, 1};
		if (roundTrip(&document, layout, stretch, &result, "stretch")) {
			bool doubled = result.uv.size() == layout.uv.size();
			for (qsizetype i = 0; doubled && i < layout.uv.size(); ++i) {
				doubled &= near(result.uv[i].x() - pivot.x(), 2 * (layout.uv[i].x() - pivot.x()), 1e-3) && near(result.uv[i].y(), layout.uv[i].y(), 1e-3);
			}
			expect(doubled && result.values && result.valuesEdit.setScaleU && !result.valuesEdit.setScaleV, "a stretch doubles U about the pivot",
				texts(result.uv));
			expect(undoLevelMapEdit(&document, &error), "back to the start", error);
		}
		// Skew: a classic face needs Valve 220 to take it.
		LevelUvToolEdit skew;
		skew.handle = LevelUvToolHandle::SkewS;
		skew.pivot = pivot;
		skew.skew = {0.5, 0};
		if (dialect == QStringLiteral("classic")) {
			const QByteArray before = serializeLevelMap(document).bytes;
			expect(!applyLevelUvToolEdit(&document, layout, skew, nullptr, &error) && error.contains(QStringLiteral("Valve 220"))
					&& serializeLevelMap(document).bytes == before,
				"a classic face refuses a skew", error);
			skew.allowValve220 = true;
		}
		if (roundTrip(&document, layout, skew, &result, "skew")) {
			bool skewed = result.uv.size() == layout.uv.size();
			for (qsizetype i = 0; skewed && i < layout.uv.size(); ++i) {
				skewed &= nearPoint(result.uv[i], {layout.uv[i].x() + 0.5 * (layout.uv[i].y() - pivot.y()), layout.uv[i].y()}, 1e-3);
			}
			expect(skewed && !result.values, "a skew shears U along V about the pivot", texts(result.uv));
			expect(result.fit == (dialect == QStringLiteral("classic") ? LevelUvFit::Converted : LevelUvFit::Exact), "a skew's fit");
			expect(undoLevelMapEdit(&document, &error), "back to the start", error);
		}
		// Nothing to do adds no step; a read gone stale is refused.
		{
			const auto depth = document.undoStack.size();
			LevelUvToolEdit none;
			none.handle = LevelUvToolHandle::Move;
			expect(applyLevelUvToolEdit(&document, layout, none, &result, &error) && document.undoStack.size() == depth && !result.changed(),
				"an edit that changes nothing adds no step", error);
			LevelMapDocument changed = document;
			expect(applyLevelUvToolEdit(&changed, layout, move, nullptr, &error), "a first move", error);
			const QByteArray before = serializeLevelMap(changed).bytes;
			expect(!applyLevelUvToolEdit(&changed, layout, stretch, nullptr, &error) && error.contains(QStringLiteral("changed"))
					&& serializeLevelMap(changed).bytes == before,
				"a stale read is refused", error);
		}
	}

	phase = QStringLiteral("brush primitive without an image size");
	{
		LevelMapDocument document;
		load(tests::surfaceFixture(QStringLiteral("brushDef")), QStringLiteral("idtech3"), &document);
		LevelUvLayout layout;
		expect(!readLevelUvLayout(document, {LevelMaterialKind::BrushFace, document.brushes.first().id, 0}, {}, &layout, &error),
			"a brush-primitive face needs its image size", error);
	}

	phase = QStringLiteral("patch");
	{
		int created = -1;
		LevelMapDocument document;
		load(patchFixture(&created), QStringLiteral("idtech3"), &document);
		if (expect(document.patches.size() == 1, "patch reloaded")) {
			const LevelMaterialTarget target {LevelMaterialKind::Patch, document.patches.first().id, 0};
			LevelUvLayout layout;
			expect(readLevelUvLayout(document, target, QSize(128, 64), &layout, &error), "read the patch", error);
			const LevelUvToolFrame frame = levelUvToolFrame(layout.points, layout.uv);
			expect(frame.valid, "a patch's fitted frame");
			const auto edges = levelUvToolEdges(layout);
			expect(edges.size() == qsizetype(layout.rows) * (layout.columns - 1) + qsizetype(layout.columns) * (layout.rows - 1),
				"a patch's edges are its rows and columns");
			const QVector<int> row = levelUvToolPatchPoints(layout, LevelUvToolHandle::PatchRow, 1);
			const QVector<int> column = levelUvToolPatchPoints(layout, LevelUvToolHandle::PatchColumn, 2);
			expect(row.size() == layout.columns && row.first() == layout.columns && column.size() == layout.rows && column.first() == 2,
				"rows and columns name their control points");
			LevelUvToolResult result;
			// A row's coordinates move; every other point keeps its numbers.
			LevelUvToolEdit rowMove;
			rowMove.handle = LevelUvToolHandle::PatchRow;
			rowMove.points = row;
			rowMove.move = {8, -4};
			if (roundTrip(&document, layout, rowMove, &result, "row move")) {
				bool only = true;
				for (qsizetype i = 0; i < layout.uv.size(); ++i) {
					only &= nearPoint(result.uv[i], layout.uv[i] + (row.contains(static_cast<int>(i)) ? QPointF(8, -4) : QPointF()), 1e-9);
				}
				expect(only, "only the row's coordinates move", texts(result.uv));
				expect(undoLevelMapEdit(&document, &error), "back to the start", error);
			}
			// The whole texture turns about the pivot, as far as asked.
			LevelUvToolEdit turn;
			turn.handle = LevelUvToolHandle::Turn;
			turn.pivot = layout.centre();
			turn.turn = 90;
			if (roundTrip(&document, layout, turn, &result, "patch turn")) {
				const LevelUvToolFrame after = levelUvToolFrame(layout.points, result.uv);
				expect(near(angleAbout(frame.alongS, after.alongS, frame.normal), 90, 1e-6), "a patch's texture turns as far as asked",
					QString::number(angleAbout(frame.alongS, after.alongS, frame.normal)));
				expect(nearVec(result.frame.worldAt(result.uv[0]), frame.worldAt(layout.uv[0]), 1e-6), "the points stay where they were on the plane");
				expect(undoLevelMapEdit(&document, &error), "back to the start", error);
			}
			// A column stretches about the pivot.
			LevelUvToolEdit stretch;
			stretch.handle = LevelUvToolHandle::StretchBoth;
			stretch.pivot = {0, 0};
			stretch.stretch = {2, 0.5};
			stretch.points = column;
			if (roundTrip(&document, layout, stretch, &result, "column stretch")) {
				bool scaled = true;
				for (qsizetype i = 0; i < layout.uv.size(); ++i) {
					const QPointF wanted = column.contains(static_cast<int>(i)) ? QPointF(layout.uv[i].x() * 2, layout.uv[i].y() * 0.5) : layout.uv[i];
					scaled &= nearPoint(result.uv[i], wanted, 1e-9);
				}
				expect(scaled, "a stretch of chosen points scales them about the pivot", texts(result.uv));
				expect(undoLevelMapEdit(&document, &error), "back to the start", error);
			}
			LevelUvToolEdit bad;
			bad.handle = LevelUvToolHandle::PatchPoint;
			bad.points = {static_cast<int>(layout.uv.size())};
			bad.move = {1, 0};
			expect(!previewLevelUvToolEdit(layout, bad, &result, &error), "a control point the patch lacks is refused", error);
		}
	}

	phase = QStringLiteral("hidden, open groups and locks");
	{
		LevelMapDocument document;
		load(tests::surfaceFixture(QStringLiteral("valve220")), QStringLiteral("idtech3"), &document);
		int second = -1;
		expect(addLevelMapBoxBrush(&document, {256, 0, 0, true}, {320, 64, 64, true}, QStringLiteral("studio/grid"), &second, &error), "a second brush", error);
		const int first = document.brushes.first().id;
		const LevelUvLayout layout = faceLayout(document);
		LevelUvLayout secondLayout;
		expect(readLevelUvLayout(document, {LevelMaterialKind::BrushFace, second, 0}, kImage, &secondLayout, &error), "read the second face", error);
		LevelUvToolEdit edit;
		edit.handle = LevelUvToolHandle::Move;
		edit.move = {4, 0};
		const auto refused = [&](const LevelUvLayout& target, const char* label, const char* reason) {
			const QByteArray before = serializeLevelMap(document).bytes;
			const auto depth = document.undoStack.size();
			QString problem;
			expect(!applyLevelUvToolEdit(&document, target, edit, nullptr, &problem) && problem.contains(QString::fromLatin1(reason))
					&& serializeLevelMap(document).bytes == before && document.undoStack.size() == depth,
				label, problem);
		};
		setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, first}});
		LevelVisibilityRequest hide;
		expect(changeLevelMapVisibility(&document, hide, nullptr, &error), "hide the first brush", error);
		refused(layout, "a hidden face is refused", "hidden");
		LevelVisibilityRequest show;
		show.operation = LevelVisibilityOperation::ShowAll;
		expect(changeLevelMapVisibility(&document, show, nullptr, &error), "show all", error);
		QString group;
		expect(createLevelSceneNode(&document, LevelSceneNodeKind::Group, QStringLiteral("Room"), {}, &group, &error)
				&& assignLevelSceneObjects(&document, group, {QStringLiteral("brush:%1").arg(first)}, &error) && openLevelSceneGroup(&document, group, &error),
			"open a group holding the first brush", error);
		refused(secondLayout, "a face outside the open group is refused", "outside the open group");
		expect(closeLevelSceneGroup(&document, true, &error), "close the group", error);
		QString layer;
		expect(createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
				&& assignLevelSceneObjects(&document, layer, {QStringLiteral("brush:%1").arg(second)}, &error) && setLevelSceneLocked(&document, layer, true, &error),
			"lock the second brush", error);
		refused(secondLayout, "a locked face is refused", "locked");
	}

	if (argc > 1) {
		phase = QStringLiteral("cli");
		const QString binary = QString::fromLocal8Bit(argv[1]);
		const QString settings = QDir(temp.path()).filePath(QStringLiteral("settings.ini"));
		const auto run = [&](const QStringList& arguments, int* exitCode) {
			QProcess process;
			process.start(binary, QStringList {QStringLiteral("--settings-file"), settings, QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("edit-uv")}
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
		const QString classic = out("classic.map");
		tests::putMaterialFile(classic, tests::surfaceFixture(QStringLiteral("classic")));
		const QByteArray original = read(classic);
		int code = -1;
		// A stretch writes what the service writes; the face stays classic.
		LevelMapDocument expected;
		load(original, QStringLiteral("idtech2"), &expected);
		const LevelUvLayout layout = faceLayout(expected, 0, {});
		LevelUvToolEdit stretch;
		stretch.handle = LevelUvToolHandle::StretchBoth;
		stretch.pivot = {0, 0};
		stretch.stretch = {2, 1};
		expect(applyLevelUvToolEdit(&expected, layout, stretch, nullptr, &error), "reference stretch", error);
		const QStringList face {classic, QStringLiteral("--engine"), QStringLiteral("idtech2"), QStringLiteral("--target"), QStringLiteral("face:0:1")};
		auto json = run(face + QStringList {QStringLiteral("--stretch"), QStringLiteral("2,1"), QStringLiteral("--pivot"), QStringLiteral("0,0"),
								   QStringLiteral("--output"), out("stretched.map")},
			&code);
		LevelMapDocument written;
		load(read(out("stretched.map")), QStringLiteral("idtech2"), &written);
		expect(code == 0 && json.value(QStringLiteral("operation")).toString() == QStringLiteral("stretch")
				&& json.value(QStringLiteral("fit")).toString() == QStringLiteral("exact") && serializeLevelMap(written).bytes == serializeLevelMap(expected).bytes
				&& !written.brushes.first().faces.first().explicitTextureAxes && read(classic) == original,
			"--stretch matches the shared service and keeps the face classic", compact(json));
		// A classic face's skew needs --allow-valve220.
		const QStringList skew = face + QStringList {QStringLiteral("--skew-u"), QStringLiteral("0.5"), QStringLiteral("--output"), out("skewed.map")};
		json = run(skew, &code);
		expect(code == 4 && !QFile::exists(out("skewed.map")), "a classic face's skew is refused without --allow-valve220", compact(json));
		json = run(skew + QStringList {QStringLiteral("--allow-valve220")}, &code);
		expect(code == 0 && json.value(QStringLiteral("fit")).toString() == QStringLiteral("converted") && QFile::exists(out("skewed.map")),
			"--allow-valve220 lets it take the skew", compact(json));
		// A patch's row moves.
		int created = -1;
		const QString curves = out("curves.map");
		tests::putMaterialFile(curves, patchFixture(&created));
		LevelMapDocument patchDocument;
		load(read(curves), QStringLiteral("idtech3"), &patchDocument);
		const int columns = patchDocument.patches.isEmpty() ? 0 : patchDocument.patches.first().width;
		const QString patchTarget = QStringLiteral("patch:%1").arg(patchDocument.patches.isEmpty() ? 0 : patchDocument.patches.first().id);
		json = run({curves, QStringLiteral("--engine"), QStringLiteral("idtech3"), QStringLiteral("--target"), patchTarget, QStringLiteral("--rows"),
					   QStringLiteral("2"), QStringLiteral("--offset"), QStringLiteral("8,0"), QStringLiteral("--texture-size"), QStringLiteral("128,64"),
					   QStringLiteral("--output"), out("curves-row.map")},
			&code);
		bool rowOnly = code == 0;
		const QJsonArray after = json.value(QStringLiteral("points")).toArray();
		LevelUvLayout before;
		readLevelUvLayout(patchDocument, {LevelMaterialKind::Patch, patchDocument.patches.first().id, 0}, QSize(128, 64), &before, &error);
		for (qsizetype i = 0; rowOnly && i < after.size() && i < before.uv.size(); ++i) {
			const QJsonArray texel = after[i].toObject().value(QStringLiteral("texel")).toArray();
			const QPointF wanted = before.uv[i] + (i / columns == 1 ? QPointF(8, 0) : QPointF());
			rowOnly &= nearPoint({texel.at(0).toDouble(), texel.at(1).toDouble()}, wanted, 1e-9);
		}
		expect(rowOnly && after.size() == before.uv.size(), "--rows moves only that row's coordinates", compact(json));
		// Usage errors write nothing.
		for (const QStringList& wrong : {face + QStringList {QStringLiteral("--offset"), QStringLiteral("1,0"), QStringLiteral("--pivot"), QStringLiteral("0,0"),
											   QStringLiteral("--output"), out("wrong.map")},
				 face + QStringList {QStringLiteral("--rows"), QStringLiteral("1"), QStringLiteral("--offset"), QStringLiteral("1,0"), QStringLiteral("--output"),
					 out("wrong.map")},
				 face + QStringList {QStringLiteral("--stretch"), QStringLiteral("0,1"), QStringLiteral("--output"), out("wrong.map")},
				 face + QStringList {QStringLiteral("--stretch"), QStringLiteral("2,1"), QStringLiteral("--skew-u"), QStringLiteral("1"), QStringLiteral("--output"),
					 out("wrong.map")}}) {
			json = run(wrong, &code);
			expect(code == 2 && !QFile::exists(out("wrong.map")), "a usage error writes nothing", wrong.join(QLatin1Char(' ')));
		}
	}

	if (!ok) {
		return 1;
	}
	std::cout << "level UV tool smoke test passed" << std::endl;
	return 0;
}
