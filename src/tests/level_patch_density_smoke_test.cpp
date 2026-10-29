// Checks NetRadiant Custom's Exact patch densities and Square box in the
// shared services and on the command line: an Exact Cylinder, Cone and
// Sphere at any columns and rows land on the control points of upstream's
// formulas (rebuilt here) on every axis, starting at angle 0; Square
// re-disperses the columns and then the rows of the mapped patch, moving only
// control points, so the texture coordinates are the round shape's Natural
// ones, as upstream re-disperses after mapping; the shape route passes rows,
// Square and the ring start only to the shapes that read them; a squared
// Exact patch replaces the selection in one undo step that undoes exactly;
// and `map add-patch` and `map add-shape` share all of it, with --rows,
// --square and --ring-start, and refuse them where they do not apply.

#include "core/level_document.h"
#include "core/level_map.h"
#include "core/level_patch.h"
#include "core/level_patch_tools.h"
#include "core/level_shapes.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
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

bool near(double a, double b, double tolerance = 1e-6)
{
	return std::abs(a - b) <= tolerance;
}

QString pointText(const LevelMapVec3& p)
{
	return QStringLiteral("%1 %2 %3").arg(p.x).arg(p.y).arg(p.z);
}

double& component(LevelMapVec3& p, int index)
{
	return index == 0 ? p.x : (index == 1 ? p.y : p.z);
}

LevelMapVec3 middle(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return {(a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2, true};
}

// Compares control points, saying where they first differ.
bool samePoints(const QVector<LevelMapVec3>& a, const QVector<LevelMapVec3>& b, QString* detail, double tolerance = 1e-6)
{
	if (a.size() != b.size()) {
		*detail = QStringLiteral("%1 points instead of %2").arg(a.size()).arg(b.size());
		return false;
	}
	for (qsizetype i = 0; i < a.size(); ++i) {
		if (!near(a.at(i).x, b.at(i).x, tolerance) || !near(a.at(i).y, b.at(i).y, tolerance) || !near(a.at(i).z, b.at(i).z, tolerance)) {
			*detail = QStringLiteral("point %1: %2 instead of %3").arg(i).arg(pointText(a.at(i)), pointText(b.at(i)));
			return false;
		}
	}
	return true;
}

// NetRadiant Custom's Exact Cylinder, Cone and Sphere (radiant/patch.cpp
// Patch::ConstructPrefab at 449778b3): column i at angle pi * i / n from the
// positive side of the first axis across, odd columns (and a sphere's odd
// rows) pushed out by 1 / cos of half a span; rows evenly along the axis, a
// sphere's from the bottom pole.
QVector<LevelMapVec3> radiantExact(const QString& shape, int axis, const LevelMapVec3& low, const LevelMapVec3& high, int width, int height)
{
	const int x = axis == 2 ? 0 : (axis == 1 ? 2 : 1);
	const int y = axis == 2 ? 1 : (axis == 1 ? 0 : 2);
	const int z = axis;
	LevelMapVec3 lowCopy = low;
	LevelMapVec3 highCopy = high;
	LevelMapVec3 centre = middle(low, high);
	const double pi = 3.14159265358979323846;
	const int n = (width - 1) / 2;
	const int m = (height - 1) / 2;
	const double f = 1 / std::cos(pi / n);
	const double g = m > 0 ? 1 / std::cos(pi / (2 * m)) : 1.0;
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
				along = component(centre, z) + (component(highCopy, z) - component(centre, z)) * -std::cos(angle2) * outRow;
			}
			LevelMapVec3 p {0, 0, 0, true};
			component(p, x) = component(centre, x) + (component(highCopy, x) - component(centre, x)) * radius * std::cos(angle) * out;
			component(p, y) = component(centre, y) + (component(highCopy, y) - component(centre, y)) * radius * std::sin(angle) * out;
			component(p, z) = along;
			points[j * width + i] = p;
		}
	}
	return points;
}

// Radiant's Patch::Redisperse, columns and then rows, as
// Scene_PatchConstructPrefab applies the Square box: each odd control goes
// halfway between its even neighbours.
QVector<LevelMapVec3> squared(QVector<LevelMapVec3> points, int width, int height)
{
	for (int row = 0; row < height; ++row) {
		for (int column = 1; column + 1 < width; column += 2) {
			points[row * width + column] = middle(points.at(row * width + column - 1), points.at(row * width + column + 1));
		}
	}
	for (int column = 0; column < width; ++column) {
		for (int row = 1; row + 1 < height; row += 2) {
			points[row * width + column] = middle(points.at((row - 1) * width + column), points.at((row + 1) * width + column));
		}
	}
	return points;
}

void checkRingStartIds()
{
	LevelPatchRingStart start = LevelPatchRingStart::Auto;
	expect(levelPatchRingStartIds() == QStringList {QStringLiteral("auto"), QStringLiteral("exact")}
			&& levelPatchRingStartId(LevelPatchRingStart::Exact) == QStringLiteral("exact")
			&& parseLevelPatchRingStart(QStringLiteral(" EXACT "), &start) && start == LevelPatchRingStart::Exact
			&& parseLevelPatchRingStart(QStringLiteral("auto"), &start) && start == LevelPatchRingStart::Auto
			&& !parseLevelPatchRingStart(QStringLiteral("sideways"), &start) && start == LevelPatchRingStart::Auto,
		"ring start ids parse and print");
	// Auto starts half a turn round only for NetRadiant Custom's fixed 9x3
	// Cylinder; a nine-column cylinder of five rows is one only its Exact
	// Cylinder makes, which starts at angle 0.
	for (const int rows : {3, 5}) {
		LevelPatchCreateRequest automatic;
		automatic.shape = QStringLiteral("cylinder");
		automatic.rows = rows;
		LevelPatchCreateRequest exact = automatic;
		exact.ringStart = LevelPatchRingStart::Exact;
		LevelMapPatch autoPatch;
		LevelMapPatch exactPatch;
		const bool built = createLevelPatch(automatic, &autoPatch) && createLevelPatch(exact, &exactPatch);
		const double startX = built ? autoPatch.controlPoints.first().x : 0;
		expect(built && (rows == 3 ? startX < 0 : startX > 0) && (rows == 3) != (startX == exactPatch.controlPoints.first().x),
			"Auto starts the 9x3 cylinder half a turn round and the 9x5 one at angle 0", QString::number(startX));
	}
}

// Exact shapes at NetRadiant Custom's density choices, through the shape
// route the Exact commands use, plain and squared, on every axis.
void checkExactDensities()
{
	const LevelMapVec3 low {-64, -32, 16, true};
	const LevelMapVec3 high {64, 96, 208, true};
	struct Case {
		const char* shape;
		int columns;
		int rows;
	};
	// Upstream's defaults (13x3, 13x7), its smallest (7 wide, a sphere 5
	// high), the nine-column ring that otherwise starts as the prefab does,
	// and its largest.
	const Case cases[] = {{"cylinder", 13, 3}, {"cylinder", 7, 9}, {"cylinder", 9, 3}, {"cylinder", 31, 31}, {"cone", 13, 3}, {"cone", 9, 7},
		{"cone", 7, 31}, {"sphere", 13, 7}, {"sphere", 9, 5}, {"sphere", 7, 5}, {"sphere", 31, 31}};
	for (const Case& entry : cases) {
		const QString shape = QString::fromLatin1(entry.shape);
		for (int axis = 0; axis < 3; ++axis) {
			for (const bool square : {false, true}) {
				LevelShapeRequest request;
				request.shape = QStringLiteral("patch-") + shape;
				request.mins = low;
				request.maxs = high;
				request.axis = axis;
				request.sides = (entry.columns - 1) / 2;
				if (shape == QStringLiteral("sphere")) {
					request.bands = (entry.rows - 1) / 2;
				} else {
					request.rows = entry.rows;
				}
				request.texture = QStringLiteral("studio/curve");
				request.patchImageSize = QSize(128, 64);
				request.patchRingStart = LevelPatchRingStart::Exact;
				request.patchSquare = square;
				const QString label =
					QStringLiteral("%1 %2x%3 axis %4%5").arg(shape).arg(entry.columns).arg(entry.rows).arg(axis).arg(square ? QStringLiteral(" square") : QString());
				LevelPatchCreateRequest create;
				LevelMapPatch patch;
				QString error;
				if (!expect(levelShapePatchRequest(request, &create, &error) && createLevelPatch(create, &patch, &error), "exact shape builds",
						label + QLatin1Char(' ') + error)) {
					continue;
				}
				expect(patch.width == entry.columns && patch.height == entry.rows && create.square == square
						&& create.ringStart == LevelPatchRingStart::Exact,
					"the shape takes the chosen columns and rows", QStringLiteral("%1: %2x%3").arg(label).arg(patch.width).arg(patch.height));
				QVector<LevelMapVec3> expected = radiantExact(shape, axis, low, high, entry.columns, entry.rows);
				if (square) {
					expected = squared(expected, entry.columns, entry.rows);
				}
				QString detail;
				expect(samePoints(patch.controlPoints, expected, &detail), square ? "Square re-disperses NetRadiant Custom's Exact control points"
																					: "the shape lands on NetRadiant Custom's Exact control points",
					label + QLatin1String(": ") + detail);
				if (!square) {
					continue;
				}
				// The squared patch keeps the round one's Natural mapping, and is
				// the round one re-dispersed by the shared matrix operations.
				LevelShapeRequest roundRequest = request;
				roundRequest.patchSquare = false;
				LevelPatchCreateRequest roundCreate;
				LevelMapPatch round;
				levelShapePatchRequest(roundRequest, &roundCreate, &error);
				createLevelPatch(roundCreate, &round, &error);
				LevelMapPatch natural = round;
				naturalLevelPatchTexture(&natural, 64, 32, &error);
				expect(patch.controlU == round.controlU && patch.controlV == round.controlV && round.controlU == natural.controlU
						&& round.controlV == natural.controlV,
					"Square keeps the round shape's Natural texture coordinates", label);
				LevelMapPatch redispersed = round;
				expect(applyLevelPatchMatrixOperation(&redispersed, LevelPatchMatrixOperation::RedisperseColumns, &error)
						&& applyLevelPatchMatrixOperation(&redispersed, LevelPatchMatrixOperation::RedisperseRows, &error)
						&& samePoints(patch.controlPoints, redispersed.controlPoints, &detail, 1e-12),
					"Square is Re-disperse Columns then Rows", label + QLatin1String(": ") + detail);
			}
		}
	}
	// A squared cylinder's columns are straight: every middle control on the
	// chord between its ends, so the ring is a polygon of its spans.
	LevelPatchCreateRequest request;
	request.shape = QStringLiteral("cylinder");
	request.segments = 6;
	request.square = true;
	LevelMapPatch hexagon;
	QString error;
	if (expect(createLevelPatch(request, &hexagon, &error), "squared hexagon", error)) {
		bool straight = true;
		for (int column = 1; column < hexagon.width; column += 2) {
			const LevelMapVec3 between = middle(hexagon.controlPoints.at(column - 1), hexagon.controlPoints.at(column + 1));
			straight &= near(hexagon.controlPoints.at(column).x, between.x) && near(hexagon.controlPoints.at(column).y, between.y);
		}
		const double radius = std::hypot(hexagon.controlPoints.at(2).x, hexagon.controlPoints.at(2).y);
		expect(straight && near(radius, 64), "a squared six-segment cylinder is a hexagonal tube with its corners on the circle",
			QString::number(radius));
	}
}

// The shape route hands rows, Square and the ring start only to the shapes
// that read them; every other shape keeps Radiant's.
void checkShapeParameters()
{
	const QStringList round {QStringLiteral("patch-cylinder"), QStringLiteral("patch-cone"), QStringLiteral("patch-classic-cone")};
	for (const QString& shape : levelShapeIds()) {
		if (!levelShapeIsPatch(shape)) {
			expect(!levelShapeParameters(shape).contains(QStringLiteral("square")) && !levelShapeParameters(shape).contains(QStringLiteral("ringStart")),
				"brush shapes take no Square or ring start", shape);
			continue;
		}
		LevelShapeRequest request;
		request.shape = shape;
		request.mins = {0, 0, 0, true};
		request.maxs = {128, 128, 128, true};
		request.rows = 7;
		request.columns = 5;
		request.patchSquare = true;
		request.patchRingStart = LevelPatchRingStart::Exact;
		request.texture = QStringLiteral("studio/curve");
		LevelPatchCreateRequest create;
		QString error;
		if (!expect(levelShapePatchRequest(request, &create, &error), "patch request", shape + error)) {
			continue;
		}
		const QStringList reads = levelShapeParameters(shape);
		const bool roundShape = round.contains(shape) || shape == QStringLiteral("patch-sphere");
		expect(reads.contains(QStringLiteral("square")) == roundShape && reads.contains(QStringLiteral("ringStart")) == roundShape
				&& create.square == roundShape && (create.ringStart == LevelPatchRingStart::Exact) == roundShape,
			"Square and the ring start belong to the round patch shapes", shape);
		const bool rows = round.contains(shape) || shape == QStringLiteral("patch-plane");
		expect(reads.contains(QStringLiteral("rows")) == rows && create.rows == (rows ? 7 : 3), "rows belong to the plane, cylinder and cones", shape);
		expect(create.columns == (shape == QStringLiteral("patch-plane") ? 5 : 3), "columns belong to the plane", shape);
	}
	// A square cylinder, bevel or end cap keeps Radiant's three rows, unsquared.
	LevelShapeRequest bevel;
	bevel.shape = QStringLiteral("patch-bevel");
	bevel.rows = 9;
	bevel.patchSquare = true;
	bevel.texture = QStringLiteral("studio/curve");
	LevelPatchCreateRequest create;
	LevelMapPatch patch;
	QString error;
	expect(levelShapePatchRequest(bevel, &create, &error) && createLevelPatch(create, &patch, &error) && patch.width == 3 && patch.height == 3
			&& !near(patch.controlPoints.at(1).x, (patch.controlPoints.at(0).x + patch.controlPoints.at(2).x) / 2, 1e-3),
		"the bevel stays Radiant's round 3x3", error);
	// The classic cone takes rows too and still closes to its tip.
	LevelShapeRequest classic;
	classic.shape = QStringLiteral("patch-classic-cone");
	classic.rows = 7;
	classic.texture = QStringLiteral("studio/curve");
	expect(levelShapePatchRequest(classic, &create, &error) && createLevelPatch(create, &patch, &error) && patch.height == 7
			&& near(patch.controlPoints.at(6 * patch.width).x, patch.controlPoints.at(6 * patch.width + 2).x),
		"the classic cone takes seven rows and closes to its tip", error);
}

LevelMapDocument fixture()
{
	LevelMapDocument document;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake3");
	create.starterRoom = false;
	QString error;
	expect(createLevelMap(create, &document, &error), "map", error);
	expect(addLevelMapBoxBrush(&document, {0, 0, 0, true}, {64, 64, 128, true}, QStringLiteral("studio/box"), nullptr, &error), "brush 0", error);
	setLevelMapSelection(&document, {});
	return document;
}

// A squared Exact sphere replaces the selected brush in one undo step.
void checkReplace()
{
	LevelMapDocument document = fixture();
	const QByteArray before = serializeLevelMap(document).bytes;
	QString error;
	expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error), "select brush 0", error);
	const auto depth = document.undoStack.size();
	LevelShapeRequest request;
	request.shape = QStringLiteral("patch-sphere");
	request.axis = 2;
	request.sides = 6;
	request.bands = 3;
	request.patchRingStart = LevelPatchRingStart::Exact;
	request.patchSquare = true;
	request.texture = QStringLiteral("studio/orb");
	expect(levelMapPatchShapeBounds(document, 8, &request.mins, &request.maxs), "bounds");
	int patchId = -1;
	QVector<LevelMapSelectionRef> taken;
	if (expect(replaceLevelMapSelectionWithPatchShape(&document, request, &patchId, &error, &taken), "replace with a squared sphere", error)) {
		QString detail;
		expect(document.undoStack.size() == depth + 1 && document.brushes.isEmpty() && document.patches.size() == 1 && document.patches.first().width == 13
				&& document.patches.first().height == 7 && taken.size() == 1
				&& samePoints(document.patches.first().controlPoints,
					squared(radiantExact(QStringLiteral("sphere"), 2, {0, 0, 0, true}, {64, 64, 128, true}, 13, 7), 13, 7), &detail),
			"the squared 13x7 sphere takes the brush's place in one step", detail);
		expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == before, "undo restores the brush exactly", error);
	}
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
		expect(finished && process.exitCode() == expected, "CLI exit code",
			args.join(QLatin1Char(' ')) + QLatin1Char(' ') + QString::fromUtf8(out) + QString::fromUtf8(process.readAllStandardError()));
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
	// map add-patch: an Exact 13x5 cylinder, squared, as the service builds
	// it. --mapping before the map path is an option, not the path.
	const QString tube = root.filePath(QStringLiteral("tube.map"));
	QJsonObject json;
	run({QStringLiteral("add-patch"), QStringLiteral("--mapping"), QStringLiteral("natural"), source, QStringLiteral("--shape"), QStringLiteral("cylinder"),
			QStringLiteral("--segments"), QStringLiteral("6"), QStringLiteral("--rows"), QStringLiteral("5"), QStringLiteral("--ring-start"),
			QStringLiteral("exact"), QStringLiteral("--square"), QStringLiteral("--size"), QStringLiteral("128,96,64"), QStringLiteral("--texture"),
			QStringLiteral("textures/studio/pipe"), QStringLiteral("--texture-size"), QStringLiteral("128,64"), QStringLiteral("--output"), tube,
			QStringLiteral("--json")},
		0, &json);
	LevelPatchCreateRequest request;
	request.shape = QStringLiteral("cylinder");
	request.segments = 6;
	request.rows = 5;
	request.ringStart = LevelPatchRingStart::Exact;
	request.square = true;
	request.size = {128, 96, 64, true};
	request.texture = QStringLiteral("textures/studio/pipe");
	request.mapping = LevelPatchCreateMapping::Natural;
	request.imageSize = QSize(128, 64);
	LevelMapPatch expected;
	QString error;
	createLevelPatch(request, &expected, &error);
	const LevelMapDocument tubed = load(tube);
	QString detail;
	// The map reader keeps control points and texture coordinates to float
	// precision.
	bool uv = !tubed.patches.isEmpty() && tubed.patches.last().controlU.size() == expected.controlU.size();
	for (qsizetype i = 0; uv && i < expected.controlU.size(); ++i) {
		uv = near(tubed.patches.last().controlU.at(i), expected.controlU.at(i), 1e-4) && near(tubed.patches.last().controlV.at(i), expected.controlV.at(i), 1e-4);
	}
	expect(!tubed.patches.isEmpty() && samePoints(tubed.patches.last().controlPoints, expected.controlPoints, &detail, 1e-3) && uv
			&& json.value(QStringLiteral("ringStart")).toString() == QStringLiteral("exact") && json.value(QStringLiteral("square")).toBool()
			&& json.value(QStringLiteral("columns")).toInt() == 13 && json.value(QStringLiteral("rows")).toInt() == 5
			&& json.value(QStringLiteral("mapping")).toString() == QStringLiteral("natural"),
		"map add-patch builds the squared Exact cylinder as the service does", detail + QLatin1Char(' ') + QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
	run({QStringLiteral("add-patch"), source, QStringLiteral("--ring-start"), QStringLiteral("sideways"), QStringLiteral("--texture"), QStringLiteral("x"),
			QStringLiteral("--output"), tube, QStringLiteral("--overwrite")},
		2);
	// As map add-shape does, add-patch takes Square and the ring start only
	// for the round shapes.
	for (const QStringList& extra : {QStringList {QStringLiteral("--shape"), QStringLiteral("bevel"), QStringLiteral("--square")},
			 QStringList {QStringLiteral("--ring-start"), QStringLiteral("exact")},
			 QStringList {QStringLiteral("--shape"), QStringLiteral("square-cylinder"), QStringLiteral("--square")}}) {
		run(QStringList {QStringLiteral("add-patch"), source} + extra
				+ QStringList {QStringLiteral("--texture"), QStringLiteral("x"), QStringLiteral("--output"), tube, QStringLiteral("--overwrite")},
			2);
	}
	// map add-shape: a squared Exact 13x7 sphere replaces brush 0, exactly as
	// the service does, and a patch cone takes rows.
	const QString orb = root.filePath(QStringLiteral("orb.map"));
	run({QStringLiteral("add-shape"), source, QStringLiteral("--shape"), QStringLiteral("patch-sphere"), QStringLiteral("--sides"), QStringLiteral("6"),
			QStringLiteral("--bands"), QStringLiteral("3"), QStringLiteral("--ring-start"), QStringLiteral("exact"), QStringLiteral("--square"),
			QStringLiteral("--object"), QStringLiteral("brush:0"), QStringLiteral("--texture"), QStringLiteral("studio/orb"), QStringLiteral("--texture-size"),
			QStringLiteral("64,64"), QStringLiteral("--output"), orb, QStringLiteral("--json")},
		0, &json);
	LevelMapDocument document = load(source);
	setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 0}});
	LevelShapeRequest shape;
	shape.shape = QStringLiteral("patch-sphere");
	shape.sides = 6;
	shape.bands = 3;
	shape.patchRingStart = LevelPatchRingStart::Exact;
	shape.patchSquare = true;
	shape.texture = QStringLiteral("studio/orb");
	shape.patchImageSize = QSize(64, 64);
	levelMapPatchShapeBounds(document, 8, &shape.mins, &shape.maxs);
	replaceLevelMapSelectionWithPatchShape(&document, shape, nullptr, &error);
	expect(load(orb).patches.size() == 1 && serializeLevelMap(load(orb)).bytes == serializeLevelMap(document).bytes
			&& json.value(QStringLiteral("ringStart")).toString() == QStringLiteral("exact") && json.value(QStringLiteral("square")).toBool(),
		"map add-shape replaces the brush with the squared Exact sphere exactly as the service does",
		QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
	const QString cone = root.filePath(QStringLiteral("cone.map"));
	run({QStringLiteral("add-shape"), source, QStringLiteral("--shape"), QStringLiteral("patch-cone"), QStringLiteral("--sides"), QStringLiteral("5"),
			QStringLiteral("--rows"), QStringLiteral("7"), QStringLiteral("--mins"), QStringLiteral("0,0,0"), QStringLiteral("--maxs"),
			QStringLiteral("128,128,64"), QStringLiteral("--texture"), QStringLiteral("studio/cone"), QStringLiteral("--output"), cone, QStringLiteral("--json")},
		0, &json);
	const LevelMapDocument coned = load(cone);
	expect(coned.patches.size() == 1 && coned.patches.first().width == 11 && coned.patches.first().height == 7
			&& json.value(QStringLiteral("ringStart")).toString() == QStringLiteral("auto") && !json.value(QStringLiteral("square")).toBool(true),
		"map add-shape builds an 11x7 patch cone");
	// Square, the ring start and rows only where the shape reads them.
	for (const QStringList& extra : {QStringList {QStringLiteral("--shape"), QStringLiteral("patch-bevel"), QStringLiteral("--square")},
			 QStringList {QStringLiteral("--shape"), QStringLiteral("patch-plane"), QStringLiteral("--ring-start"), QStringLiteral("exact")},
			 QStringList {QStringLiteral("--shape"), QStringLiteral("patch-sphere"), QStringLiteral("--rows"), QStringLiteral("5")},
			 QStringList {QStringLiteral("--shape"), QStringLiteral("cylinder"), QStringLiteral("--square")},
			 QStringList {QStringLiteral("--shape"), QStringLiteral("patch-cylinder"), QStringLiteral("--ring-start"), QStringLiteral("sideways")}}) {
		run(QStringList {QStringLiteral("add-shape"), source} + extra
				+ QStringList {QStringLiteral("--mins"), QStringLiteral("0,0,0"), QStringLiteral("--maxs"), QStringLiteral("64,64,64"), QStringLiteral("--texture"),
					QStringLiteral("studio/box"), QStringLiteral("--output"), cone, QStringLiteral("--overwrite")},
			2);
	}
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	checkRingStartIds();
	checkExactDensities();
	checkShapeParameters();
	checkReplace();
	if (argc > 1) {
		checkCli(QString::fromLocal8Bit(argv[1]));
	}
	if (!ok) {
		return 1;
	}
	std::cout << "level patch density smoke test passed" << std::endl;
	return 0;
}
