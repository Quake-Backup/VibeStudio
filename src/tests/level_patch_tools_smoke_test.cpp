// Radiant's Patch menu in the core: the new patch shapes, the matrix
// commands, Thicken, patch texture coordinates through the surface service,
// and patch shapes in the Shapes service. Every document edit is checked
// against exact bytes after undo, and the results against a reload.

#include "core/level_document.h"
#include "core/level_patch.h"
#include "core/level_patch_tools.h"
#include "core/level_shapes.h"
#include "core/level_surface.h"
#include "core/level_texture_mapping.h"
#include "core/map_geometry.h"

#include "tests/level_surface_test_helpers.h"

#include <QCoreApplication>

#include <cmath>
#include <iostream>

using namespace vibestudio;

namespace {

int failures = 0;

bool expect(bool condition, const char* message, const QString& detail = {})
{
	if (!condition) {
		++failures;
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty()) {
			std::cerr << " (" << detail.toStdString() << ")";
		}
		std::cerr << "\n";
	}
	return condition;
}

bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) <= tolerance; }

bool samePoint(const LevelMapVec3& a, const LevelMapVec3& b, double tolerance = 1e-9)
{
	return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

LevelMapVec3 quadratic(const LevelMapVec3& a, const LevelMapVec3& b, const LevelMapVec3& c, double t)
{
	const double s = 1 - t;
	return {a.x * s * s + 2 * b.x * s * t + c.x * t * t, a.y * s * s + 2 * b.y * s * t + c.y * t * t, a.z * s * s + 2 * b.z * s * t + c.z * t * t,
		true};
}

double quadratic(double a, double b, double c, double t)
{
	const double s = 1 - t;
	return a * s * s + 2 * b * s * t + c * t * t;
}

LevelMapVec3 at(const LevelMapPatch& patch, int row, int column) { return patch.controlPoints.at(row * patch.width + column); }

LevelMapVec3 sub(const LevelMapVec3& a, const LevelMapVec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, true}; }
LevelMapVec3 cross(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, true};
}
double dot(const LevelMapVec3& a, const LevelMapVec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// The side the preview draws: the quad winding of map_preview_mesh, across
// the columns then down the rows.
LevelMapVec3 front(const LevelMapPatch& patch, int row, int column)
{
	const int c0 = std::max(0, column - 1), c1 = std::min(patch.width - 1, column + 1);
	const int r0 = std::max(0, row - 1), r1 = std::min(patch.height - 1, row + 1);
	return cross(sub(at(patch, row, c1), at(patch, row, c0)), sub(at(patch, r1, column), at(patch, r0, column)));
}

LevelMapDocument quake3Map()
{
	LevelMapDocument document;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake3");
	create.starterRoom = false;
	QString error;
	createLevelMap(create, &document, &error);
	return document;
}

LevelMapDocument reloaded(const LevelMapDocument& document, const QString& hint = QStringLiteral("idtech3"))
{
	LevelMapDocument loaded;
	QString error;
	loadLevelMapBytes({QStringLiteral("patch-tools.map"), {}, hint}, serializeLevelMap(document).bytes, &loaded, &error);
	return loaded;
}

QByteArray bytesOf(const LevelMapDocument& document) { return serializeLevelMap(document).bytes; }

// A map holding the patch, read back so it owns its source lines.
LevelMapDocument withPatches(const QVector<LevelMapPatch>& patches)
{
	LevelMapDocument document = quake3Map();
	QString error;
	for (const auto& patch : patches) {
		addLevelMapPatch(&document, patch, nullptr, &error);
	}
	return reloaded(document);
}

void checkShapes()
{
	QString error;
	// The default cylinder keeps the nine-column ring it always had.
	LevelPatchCreateRequest legacy;
	legacy.shape = QStringLiteral("cylinder");
	legacy.center = {16, -8, 32, true};
	legacy.size = {64, 128, 96, true};
	LevelMapPatch cylinder;
	expect(createLevelPatch(legacy, &cylinder, &error), "default cylinder", error);
	const double ringX[9] {1, 1, 0, -1, -1, -1, 0, 1, 1};
	const double ringY[9] {0, 1, 1, 1, 0, -1, -1, -1, 0};
	bool same = cylinder.width == 9 && cylinder.height == 3;
	for (int row = 0; same && row < 3; ++row) {
		for (int column = 0; column < 9; ++column) {
			const LevelMapVec3 p = at(cylinder, row, column);
			same &= p.x == 16 + ringX[column] * 64 * 0.5 && p.y == -8 + ringY[column] * 128 * 0.5 && p.z == 32 + (row / 2.0 - 0.5) * 96;
		}
	}
	expect(same, "the four-segment cylinder lands on the same control points as before");

	for (const QString& shape : levelPatchShapeIds()) {
		for (int axis = 0; axis < 3; ++axis) {
			LevelPatchCreateRequest request;
			request.shape = shape;
			request.axis = axis;
			request.segments = 6;
			request.bands = 3;
			request.size = {64, 96, 128, true};
			LevelMapPatch patch;
			if (!expect(createLevelPatch(request, &patch, &error), "every patch shape builds on every axis", shape + QLatin1Char(' ') + error)) {
				continue;
			}
			expect(validateLevelPatch(patch, &error), "patch shapes validate", shape + QLatin1Char(' ') + error);
			// Control points stay inside the box; the on-curve ones of round
			// shapes reach its sides.
			bool inside = true;
			for (const auto& p : patch.controlPoints) {
				inside &= std::abs(p.x) <= 32 + 1e-9 && std::abs(p.y) <= 48 + 1e-9 && std::abs(p.z) <= 64 + 1e-9;
			}
			if (shape != QStringLiteral("cylinder") && shape != QStringLiteral("cone") && shape != QStringLiteral("sphere")) {
				expect(inside, "straight patch shapes keep their control points in the box", shape);
			}
			const bool closed = shape == QStringLiteral("cylinder") || shape == QStringLiteral("square-cylinder")
				|| shape == QStringLiteral("cone") || shape == QStringLiteral("sphere");
			if (closed) {
				// Each closed shape faces outwards, the way Radiant's cylinder does.
				const int row = patch.height / 2 - (shape == QStringLiteral("cone") ? 1 : 0);
				const int column = 2;
				const LevelMapVec3 p = at(patch, std::max(0, row), column);
				LevelMapVec3 radial = p;
				(axis == 0 ? radial.x : (axis == 1 ? radial.y : radial.z)) = 0;
				expect(dot(front(patch, std::max(0, row), column), radial) > 0, "closed patch shapes face outwards", shape + QString::number(axis));
				for (int r = 0; r < patch.height; ++r) {
					expect(samePoint(at(patch, r, 0), at(patch, r, patch.width - 1)), "closed shapes close their rows", shape);
				}
			}
			if (shape == QStringLiteral("cylinder") || shape == QStringLiteral("cone")) {
				expect(patch.width == 13, "segments set the columns round a cylinder or cone", shape);
				// On-curve points lie on the ellipse.
				for (int column = 0; column < patch.width; column += 2) {
					const LevelMapVec3 p = at(patch, 0, column);
					const double a = axis == 2 ? p.x / 32 : (axis == 1 ? p.z / 64 : p.y / 48);
					const double b = axis == 2 ? p.y / 48 : (axis == 1 ? p.x / 32 : p.z / 64);
					expect(near(a * a + b * b, 1.0, 1e-9), "round patch ends lie on the circle", shape);
				}
			}
			if (shape == QStringLiteral("sphere")) {
				expect(patch.height == 7 && patch.width == 13, "bands set a sphere's rows");
				for (int column = 1; column < patch.width; ++column) {
					expect(samePoint(at(patch, 0, column), at(patch, 0, 0)) && samePoint(at(patch, 6, column), at(patch, 6, 0)),
						"a sphere's first and last rows close to its poles");
				}
			}
		}
	}
	LevelPatchCreateRequest bad;
	bad.shape = QStringLiteral("cylinder");
	bad.segments = 16;
	LevelMapPatch ignored;
	expect(!createLevelPatch(bad, &ignored, &error), "more than 15 segments is refused");
	bad.shape = QStringLiteral("torus");
	bad.segments = 4;
	expect(!createLevelPatch(bad, &ignored, &error), "unknown shapes are refused");
}

void checkMatrix()
{
	QString error;
	LevelPatchCreateRequest request;
	request.shape = QStringLiteral("plane");
	request.columns = 5;
	request.rows = 3;
	LevelMapPatch plane;
	createLevelPatch(request, &plane, &error);
	// Bend it so splitting has something to keep.
	plane.controlPoints[1].z = 40;
	plane.controlPoints[7].z = -24;
	plane.controlU[1] = 0.7;

	for (const bool atEnd : {false, true}) {
		LevelMapPatch split = plane;
		const auto operation = atEnd ? LevelPatchMatrixOperation::AddColumns : LevelPatchMatrixOperation::InsertColumns;
		expect(applyLevelPatchMatrixOperation(&split, operation, &error), "insert columns", error);
		expect(split.width == plane.width + 2 && split.height == plane.height, "inserting adds two columns");
		const int first = atEnd ? plane.width - 3 : 0;
		bool exact = true;
		for (int row = 0; row < plane.height; ++row) {
			for (const double t : {0.0, 0.1, 0.25, 0.4, 0.5, 0.6, 0.9, 1.0}) {
				const LevelMapVec3 before = quadratic(at(plane, row, first), at(plane, row, first + 1), at(plane, row, first + 2), t);
				const int half = t <= 0.5 ? first : first + 2;
				const double local = t <= 0.5 ? t * 2 : t * 2 - 1;
				const LevelMapVec3 after = quadratic(at(split, row, half), at(split, row, half + 1), at(split, row, half + 2), local);
				exact &= samePoint(before, after, 1e-9);
				const auto u = [&](const LevelMapPatch& p, int r, int c) { return p.controlU.at(r * p.width + c); };
				exact &= near(quadratic(u(plane, row, first), u(plane, row, first + 1), u(plane, row, first + 2), t),
					quadratic(u(split, row, half), u(split, row, half + 1), u(split, row, half + 2), local));
			}
		}
		expect(exact, "inserting splits a span exactly, positions and texture coordinates");
		LevelMapPatch back = split;
		expect(applyLevelPatchMatrixOperation(&back, atEnd ? LevelPatchMatrixOperation::DeleteLastColumns : LevelPatchMatrixOperation::DeleteFirstColumns,
				   &error),
			"delete columns", error);
		bool restored = back.width == plane.width;
		for (int i = 0; restored && i < plane.controlPoints.size(); ++i) {
			restored &= samePoint(back.controlPoints[i], plane.controlPoints[i], 1e-9) && near(back.controlU[i], plane.controlU[i]);
		}
		expect(restored, "deleting the inserted columns gives the patch back");
	}
	LevelMapPatch rows = plane;
	expect(applyLevelPatchMatrixOperation(&rows, LevelPatchMatrixOperation::InsertRows, &error) && rows.height == 5 && rows.width == 5,
		"insert rows adds two rows", error);
	expect(applyLevelPatchMatrixOperation(&rows, LevelPatchMatrixOperation::DeleteLastRows, &error) && rows.height == 3, "delete rows", error);
	LevelMapPatch narrow = plane;
	expect(!applyLevelPatchMatrixOperation(&narrow, LevelPatchMatrixOperation::DeleteFirstRows, &error), "three rows are the fewest");
	LevelMapPatch wide = plane;
	wide.width = 31;
	wide.controlPoints.resize(31 * 3);
	wide.controlU.resize(31 * 3);
	wide.controlV.resize(31 * 3);
	for (auto& p : wide.controlPoints) {
		p.valid = true;
	}
	expect(!applyLevelPatchMatrixOperation(&wide, LevelPatchMatrixOperation::AddColumns, &error), "31 columns are the most");

	LevelMapPatch turned = plane;
	expect(applyLevelPatchMatrixOperation(&turned, LevelPatchMatrixOperation::Transpose, &error) && turned.width == 3 && turned.height == 5,
		"transpose swaps the grid", error);
	expect(samePoint(at(turned, 3, 1), at(plane, 1, 3)), "transpose moves each point across the diagonal");
	expect(dot(front(turned, 2, 1), front(plane, 1, 2)) < 0, "a transposed patch faces the other way");
	applyLevelPatchMatrixOperation(&turned, LevelPatchMatrixOperation::Transpose, &error);
	expect(turned.width == plane.width && samePoint(at(turned, 1, 3), at(plane, 1, 3)), "transposing twice gives the patch back");

	LevelMapPatch even = plane;
	expect(applyLevelPatchMatrixOperation(&even, LevelPatchMatrixOperation::RedisperseColumns, &error), "redisperse", error);
	expect(samePoint(at(even, 0, 1), {(at(plane, 0, 0).x + at(plane, 0, 2).x) / 2, (at(plane, 0, 0).y + at(plane, 0, 2).y) / 2,
										 (at(plane, 0, 0).z + at(plane, 0, 2).z) / 2, true}),
		"re-dispersing puts middles halfway");
	expect(near(even.controlU[1], plane.controlU[1]), "re-dispersing leaves texture coordinates");

	LevelPatchCreateRequest tube;
	tube.shape = QStringLiteral("cylinder");
	LevelMapPatch cylinder;
	createLevelPatch(tube, &cylinder, &error);
	cylinder.controlPoints[2].x += 10; // a crease at the second joint of the first row
	LevelMapPatch smooth = cylinder;
	expect(applyLevelPatchMatrixOperation(&smooth, LevelPatchMatrixOperation::SmoothColumns, &error), "smooth", error);
	const LevelMapVec3 joint = at(smooth, 0, 2);
	const LevelMapVec3 left = at(smooth, 0, 1), right = at(smooth, 0, 3);
	expect(samePoint(joint, {(left.x + right.x) / 2, (left.y + right.y) / 2, (left.z + right.z) / 2, true}), "smoothing puts joints between their controls");
	expect(samePoint(at(smooth, 1, 0), at(smooth, 1, 8)), "smoothing keeps a closed row closed across its seam");

	// The document operation: one undo step for every patch, exact undo.
	LevelMapDocument document = withPatches({plane, cylinder});
	const QByteArray original = bytesOf(document);
	int changed = 0;
	expect(applyLevelMapPatchMatrixOperation(&document, {0, 1}, LevelPatchMatrixOperation::InsertColumns, &changed, &error) && changed == 2,
		"matrix operation on two patches", error);
	expect(document.undoStack.size() == 1, "the matrix operation is one undo step");
	const LevelMapDocument back = reloaded(document);
	expect(back.patches.size() == 2 && back.patches[0].width == 7 && back.patches[1].width == 11, "inserted columns survive a reload");
	undoLevelMapEdit(&document, &error);
	expect(bytesOf(document) == original, "undo restores the exact bytes");
	expect(!applyLevelMapPatchMatrixOperation(&document, {0, 1}, LevelPatchMatrixOperation::DeleteFirstRows, &changed, &error),
		"one refused patch refuses the whole operation");
	expect(bytesOf(document) == original, "a refused operation leaves the map alone");
	LevelMapDocument quake;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake");
	createLevelMap(create, &quake, &error);
	expect(!applyLevelMapPatchMatrixOperation(&quake, {0}, LevelPatchMatrixOperation::Transpose, &changed, &error), "Quake maps have no patches");
}

void checkThicken()
{
	QString error;
	LevelPatchCreateRequest request;
	request.shape = QStringLiteral("plane");
	request.center = {0, 0, 0, true};
	LevelMapPatch plane;
	createLevelPatch(request, &plane, &error);
	// A plane in XY faces +Z.
	expect(front(plane, 1, 1).z > 0, "a plane in XY faces up");
	QVector<LevelMapPatch> pieces;
	LevelPatchThickenRequest thicken;
	thicken.thickness = 16;
	expect(thickenLevelPatch(plane, thicken, &pieces, &error), "thicken a plane", error);
	expect(pieces.size() == 5, "an open plane gains a back and four walls", QString::number(pieces.size()));
	if (pieces.size() == 5) {
		const LevelMapPatch& back = pieces.first();
		bool behind = true;
		for (const auto& p : back.controlPoints) {
			behind &= near(p.z, -16);
		}
		expect(behind, "the thickened copy lies behind the surface");
		expect(front(back, 1, 1).z < 0, "the thickened copy faces away");
		for (int wall = 1; wall < 5; ++wall) {
			const LevelMapPatch& seam = pieces[wall];
			const LevelMapVec3 normal = front(seam, 1, seam.width / 2);
			const LevelMapVec3 middle = at(seam, 1, seam.width / 2);
			expect(seam.height == 3 && dot(normal, middle) > 0, "each wall faces out of the slab");
		}
	}
	LevelPatchThickenRequest along;
	along.thickness = -8;
	along.axis = 2;
	along.seams = false;
	expect(thickenLevelPatch(plane, along, &pieces, &error) && pieces.size() == 1 && near(pieces.first().controlPoints.first().z, -8),
		"thicken along an axis", error);
	LevelPatchCreateRequest tube;
	tube.shape = QStringLiteral("cylinder");
	LevelMapPatch cylinder;
	createLevelPatch(tube, &cylinder, &error);
	expect(thickenLevelPatch(cylinder, thicken, &pieces, &error) && pieces.size() == 3, "a cylinder needs walls only at its rims", error);
	if (!pieces.isEmpty()) {
		// Thickened behind an outward face: the copy lies inside.
		const LevelMapVec3 p = pieces.first().controlPoints.at(4);
		expect(std::hypot(p.x, p.y) < 64 - 1, "a cylinder thickens inwards");
	}
	LevelPatchCreateRequest ball;
	ball.shape = QStringLiteral("sphere");
	LevelMapPatch sphere;
	createLevelPatch(ball, &sphere, &error);
	expect(thickenLevelPatch(sphere, thicken, &pieces, &error) && pieces.size() == 1, "a sphere thickens without walls", error);
	if (!pieces.isEmpty()) {
		const LevelMapVec3 pole = pieces.first().controlPoints.first();
		expect(near(pole.x, 0, 1e-6) && near(pole.y, 0, 1e-6) && pole.z > -64 + 15, "a sphere's pole moves along its axis");
	}
	// Unevenly spaced columns still move the poles along the axis.
	LevelMapPatch uneven = sphere;
	applyLevelPatchMatrixOperation(&uneven, LevelPatchMatrixOperation::InsertColumns, &error);
	expect(thickenLevelPatch(uneven, thicken, &pieces, &error) && pieces.size() == 1, "an unevenly split sphere thickens without walls", error);
	if (!pieces.isEmpty()) {
		const LevelMapVec3 pole = pieces.first().controlPoints.last();
		expect(near(pole.x, 0, 1e-6) && near(pole.y, 0, 1e-6) && near(pole.z, 64 - 16, 1e-6), "the top pole moves straight down");
	}
	LevelPatchThickenRequest none;
	none.thickness = 0;
	expect(!thickenLevelPatch(plane, none, &pieces, &error), "zero thickness is refused");

	LevelMapDocument document = withPatches({plane, cylinder});
	const QByteArray original = bytesOf(document);
	QVector<int> created;
	expect(thickenLevelMapPatches(&document, {0, 1}, thicken, &created, &error) && created.size() == 8, "thicken patches in a map", error);
	expect(document.undoStack.size() == 1 && document.selection.size() == 8, "thickening is one step and selects what it made");
	expect(reloaded(document).patches.size() == 10, "thickened patches survive a reload");
	undoLevelMapEdit(&document, &error);
	expect(bytesOf(document) == original, "undo removes every thickened piece");
}

void checkPatchTextures()
{
	QString error;
	LevelPatchCreateRequest request;
	request.shape = QStringLiteral("plane");
	request.size = {256, 128, 128, true};
	request.columns = 5;
	LevelMapPatch plane;
	createLevelPatch(request, &plane, &error);
	LevelMapPatch natural = plane;
	expect(naturalLevelPatchTexture(&natural, 64, 32, &error), "natural texture", error);
	expect(near(natural.controlU[4], 4.0) && near(natural.controlU[0], 0.0), "natural U follows the distance across");
	expect(near(natural.controlV[2 * 5], -4.0), "natural V runs down the rows, as Radiant's does");
	LevelMapPatch planar = plane;
	expect(planarLevelPatchTexture(&planar, 64, 64, &error), "planar texture", error);
	const auto [s, t] = levelTexturePrimitiveBasis(levelPatchAverageNormal(plane));
	expect(near(planar.controlU[3], dot(plane.controlPoints[3], s) / 64), "planar texture projects along the patch's basis");

	// Through the surface service, alongside brush faces.
	LevelMapDocument document = withPatches({plane});
	const QByteArray original = bytesOf(document);
	LevelSurfaceEditPlan plan;
	LevelSurfaceRequest fit;
	fit.operation = LevelSurfaceOperation::Fit;
	fit.x = 2;
	fit.y = 0;
	expect(prepareLevelSurfaceEdits(document, {}, {0}, {fit}, {}, &plan, &error) && plan.patchCount() == 1, "fit a patch without its size", error);
	expect(commitLevelSurfaceEdit(&document, plan, &error), "commit the patch fit", error);
	const LevelMapPatch fitted = document.patches.first();
	expect(near(fitted.controlU[4], 2.0) && near(fitted.controlV[10], plane.controlV[10]), "fitting one axis keeps the other");
	expect(document.undoStack.size() == 1, "a patch surface edit is one undo step");
	expect(near(reloaded(document).patches.first().controlU[4], 2.0), "the fitted coordinates survive a reload");
	undoLevelMapEdit(&document, &error);
	expect(bytesOf(document) == original, "undo restores the patch's coordinates exactly");

	LevelSurfaceRequest shift;
	shift.operation = LevelSurfaceOperation::Shift;
	shift.x = 32;
	shift.y = 0;
	expect(!prepareLevelSurfaceEdits(document, {}, {0}, {shift}, {}, &plan, &error), "a patch shift needs the image size");
	const QHash<QString, QSize> sizes {{plane.textureName, QSize(64, 64)}};
	expect(prepareLevelSurfaceEdits(document, {}, {0}, {shift}, sizes, &plan, &error), "shift a patch with its size", error);
	commitLevelSurfaceEdit(&document, plan, &error);
	expect(near(document.patches.first().controlU[0], 0.5), "a patch shift counts texels");
	LevelSurfaceRequest flip;
	flip.operation = LevelSurfaceOperation::Scale;
	flip.x = -1;
	flip.y = 1;
	expect(prepareLevelSurfaceEdits(document, {}, {0}, {flip}, {}, &plan, &error) && commitLevelSurfaceEdit(&document, plan, &error), "flip a patch", error);
	expect(near(document.patches.first().controlU[0], 1.5) && near(document.patches.first().controlU[4], 0.5), "flipping mirrors about the middle");
	LevelSurfaceRequest turn;
	turn.operation = LevelSurfaceOperation::Rotate;
	turn.degrees = 90;
	expect(prepareLevelSurfaceEdits(document, {}, {0}, {turn}, sizes, &plan, &error) && commitLevelSurfaceEdit(&document, plan, &error), "rotate a patch",
		error);
	LevelSurfaceRequest reset;
	reset.operation = LevelSurfaceOperation::Natural;
	reset.x = reset.y = 0.5;
	expect(prepareLevelSurfaceEdits(document, {}, {0}, {reset}, sizes, &plan, &error) && commitLevelSurfaceEdit(&document, plan, &error),
		"reset a patch to natural", error);
	LevelMapPatch expected = plane;
	naturalLevelPatchTexture(&expected, 32, 32, &error);
	expect(near(document.patches.first().controlU[4], expected.controlU[4]) && near(document.patches.first().controlV[10], expected.controlV[10]),
		"Natural on a patch matches Radiant's natural spread");
}

void checkFaceResets()
{
	QString error;
	for (const QString& dialect : {QStringLiteral("classic"), QStringLiteral("valve220"), QStringLiteral("brushDef"), QStringLiteral("brushDef3")}) {
		LevelMapDocument document;
		loadLevelMapBytes({QStringLiteral("reset.map"), {}, dialect.startsWith(QStringLiteral("brushDef")) ? QStringLiteral("idtech3") : QString()},
			tests::surfaceFixture(dialect), &document, &error);
		const QByteArray original = bytesOf(document);
		const QHash<QString, QSize> sizes {{QStringLiteral("studio/grid"), QSize(128, 64)}};
		LevelSurfaceEditPlan plan;
		LevelSurfaceRequest natural;
		natural.operation = LevelSurfaceOperation::Natural;
		natural.x = natural.y = 0.5;
		const auto faces = levelMapSelectedSurfaces([&] {
			LevelMapDocument selected = document;
			setLevelMapSelection(&selected, {{LevelMapSelectionKind::QuakeBrush, document.brushes.first().id}});
			return selected;
		}());
		expect(prepareLevelSurfaceEdits(document, faces, {natural}, sizes, &plan, &error), "natural on every dialect", dialect + QLatin1Char(' ') + error);
		expect(commitLevelSurfaceEdit(&document, plan, &error), "commit natural", error);
		for (const auto& face : document.brushes.first().faces) {
			const auto projection = levelTextureProjection(face);
			const auto expected = levelNaturalTextureProjection(face, 0.5, 0.5, QSize(128, 64));
			expect(projection.valid && samePoint(projection.u, expected.u, 1e-6) && samePoint(projection.v, expected.v, 1e-6)
					&& near(projection.offsetU, 0, 1e-6) && near(projection.offsetV, 0, 1e-6),
				"natural gives the axial projection with no offset", dialect);
			if (!face.explicitTextureMatrix && !face.explicitTextureAxes) {
				expect(face.scaleX == 0.5 && face.rotation == 0 && face.shiftX == 0, "classic natural writes plain numbers");
			}
		}
		const auto back = reloaded(document, dialect.startsWith(QStringLiteral("brushDef")) ? QStringLiteral("idtech3") : QString());
		expect(back.brushes.size() == document.brushes.size(), "natural faces reload", dialect);
		undoLevelMapEdit(&document, &error);
		expect(bytesOf(document) == original, "undo restores the faces exactly", dialect);
		LevelSurfaceRequest fitU;
		fitU.operation = LevelSurfaceOperation::Fit;
		fitU.x = 1;
		fitU.y = 0;
		expect(prepareLevelSurfaceEdits(document, faces, {fitU}, sizes, &plan, &error), "fit one axis", error);
	}
	// Align to Face on slopes: a Valve 220 brush turned 30 degrees about Y
	// takes its axes into each face's plane.
	LevelMapDocument valve;
	expect(loadLevelMapBytes({QStringLiteral("slope.map"), {}, {}}, tests::surfaceFixture(QStringLiteral("valve220")), &valve, &error)
			&& valve.brushes.size() == 1,
		"load the Valve brush", error);
	if (!valve.brushes.isEmpty()) {
		setLevelMapSelection(&valve, {{LevelMapSelectionKind::QuakeBrush, valve.brushes.first().id}});
		LevelMapRotationRequest turn;
		turn.axis = 1;
		turn.degrees = 30;
		expect(rotateLevelMapSelection(&valve, turn, &error), "turn the brush onto slopes", error);
		LevelSurfaceEditPlan plan;
		LevelSurfaceRequest aligned;
		aligned.operation = LevelSurfaceOperation::FaceAligned;
		aligned.x = aligned.y = 1;
		expect(prepareLevelSurfaceEdits(valve, levelMapSelectedSurfaces(valve), {aligned}, {}, &plan, &error)
				&& commitLevelSurfaceEdit(&valve, plan, &error),
			"align sloped textures to their faces", error);
		for (const auto& face : valve.brushes.first().faces) {
			const auto plane = planeFromPoints(face.p0, face.p1, face.p2);
			const LevelMapVec3 normal {plane.normalX, plane.normalY, plane.normalZ, true};
			expect(near(dot(face.uAxis, normal), 0, 1e-6) && near(dot(face.vAxis, normal), 0, 1e-6), "face-aligned axes lie in the face");
			expect(near(std::sqrt(dot(face.uAxis, face.uAxis)), 1, 1e-6) && face.scaleX == 1, "face-aligned axes keep a texel per unit");
		}
	}
}

void checkShapesService()
{
	QString error;
	LevelMapDocument document = quake3Map();
	addLevelMapBoxBrush(&document, {0, 0, 0, true}, {64, 128, 32, true}, QStringLiteral("studio/a"), nullptr, &error);
	document = reloaded(document);
	const QByteArray original = bytesOf(document);
	expect(isLevelShape(QStringLiteral("patch-cylinder")) && levelShapeIsPatch(QStringLiteral("patch-bevel")) && !levelShapeIsPatch(QStringLiteral("box")),
		"patch shapes are shapes");
	LevelShapeRequest shape;
	shape.shape = QStringLiteral("patch-cylinder");
	shape.mins = {-64, -64, 0, true};
	shape.maxs = {64, 64, 128, true};
	shape.sides = 6;
	shape.texture = QStringLiteral("studio/pipe");
	QVector<int> added;
	expect(addLevelMapShape(&document, shape, &added, &error) && added.size() == 1, "add a patch shape", error);
	expect(document.patches.size() == 1 && document.patches.first().width == 13 && document.selection.size() == 1
			&& document.selection.first().kind == LevelMapSelectionKind::QuakePatch,
		"the patch shape is added and selected");
	undoLevelMapEdit(&document, &error);
	shape.shape = QStringLiteral("patch-bevel");
	expect(replaceLevelMapBrushesWithShape(&document, {document.brushes.first().id}, shape, &added, &error), "replace a brush with a patch", error);
	expect(document.brushes.isEmpty() && document.patches.size() == 1 && document.undoStack.size() == 1,
		"the brush becomes a patch in one step");
	if (!document.patches.isEmpty()) {
		expect(near(document.patches.first().mins.x, 0) && near(document.patches.first().maxs.y, 128), "the patch fills the brush's bounds");
	}
	expect(document.selection.size() == 1 && document.selection.first().kind == LevelMapSelectionKind::QuakePatch, "the new patch is selected");
	undoLevelMapEdit(&document, &error);
	expect(bytesOf(document) == original, "undo brings the brush back exactly");
	LevelShapeRequest plane;
	plane.shape = QStringLiteral("patch-plane");
	plane.axis = 2;
	plane.mins = {0, 0, 0, true};
	plane.maxs = {64, 64, 32, true};
	expect(addLevelMapShape(&document, plane, &added, &error) && near(document.patches.last().controlPoints.first().z, 32),
		"a patch plane lies across the box's top", error);
	LevelMapDocument quake;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake");
	create.starterRoom = false;
	createLevelMap(create, &quake, &error);
	expect(!addLevelMapShape(&quake, shape, &added, &error), "Quake maps refuse patch shapes");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	checkShapes();
	checkMatrix();
	checkThicken();
	checkPatchTextures();
	checkFaceResets();
	checkShapesService();
	if (failures > 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "level patch tools smoke test passed\n";
	return 0;
}
