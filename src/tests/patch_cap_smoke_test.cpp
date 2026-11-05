#include "tests/patch_cap_test_helpers.h"
#include "core/level_visibility.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointF>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *label, const QString &error = {})
{
	if (!value) {
		std::cerr << label << ": " << error.toStdString() << '\n';
	}
	return value;
}
bool near(double a, double b) { return std::abs(a - b) < 1e-8; }
QByteArray read(const QString &path)
{
	QFile f(path);
	return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{};
}
// NetRadiant Custom's Cap Selection seams, against its Patch::ConstructSeam
// layouts written out here on their own: each entry of a style's grid names
// the boundary control it copies (0 to 8), or a combination of them.
struct Expected {
	int width = 0, height = 0;
	QVector<LevelMapVec3> points;
};
LevelMapVec3 mixPoints(const LevelMapVec3 &a, double wa, const LevelMapVec3 &b, double wb, const LevelMapVec3 &c = {}, double wc = 0)
{
	return {a.x * wa + b.x * wb + c.x * wc, a.y * wa + b.y * wb + c.y * wc, a.z * wa + b.z * wb + c.z * wc, true};
}
Expected radiantSeam(LevelPatchCapStyle style, const QVector<LevelMapVec3> &p)
{
	Expected e;
	switch (style) {
	case LevelPatchCapStyle::Bevel: {
		const auto c = mixPoints(p[2], 1, p[0], 1, p[1], -1);
		e = {3, 3, {c, c, p[2], c, c, p[1], c, c, p[0]}};
		break;
	}
	case LevelPatchCapStyle::InvertedBevel:
		e = {3, 3, {p[0], p[1], p[1], p[1], p[1], p[1], p[2], p[1], p[1]}};
		break;
	case LevelPatchCapStyle::Endcap: {
		const auto m = mixPoints(p[0], 0.5, p[4], 0.5);
		e = {3, 3, {p[0], m, p[4], p[1], p[2], p[3], p[2], p[2], p[2]}};
		break;
	}
	case LevelPatchCapStyle::InvertedEndcap:
		e = {5, 3, {p[4], p[3], p[2], p[1], p[0], p[3], p[3], p[2], p[1], p[1], p[3], p[3], p[2], p[1], p[1]}};
		break;
	case LevelPatchCapStyle::Cylinder:
		e.width = 3;
		e.height = 5;
		for (int i = 0; i < 5; ++i) {
			e.points << p[i] << mixPoints(p[i], 0.5, p[8 - i], 0.5) << p[8 - i];
		}
		break;
	case LevelPatchCapStyle::Radial:
		break;
	}
	return e;
}
// The surface's facing at (u, v): columns crossed with rows.
double facingZ(const LevelMapPatch &patch, double u, double v)
{
	const auto a = tests::samplePatch(patch, u, v), b = tests::samplePatch(patch, u + 1e-4, v), c = tests::samplePatch(patch, u, v + 1e-4);
	return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}
bool checkStyles()
{
	bool ok = true;
	QString error;
	LevelPatchCapStyle parsed = LevelPatchCapStyle::Radial;
	ok &= expect(levelPatchCapStyleIds() == QStringList{QStringLiteral("radial"), QStringLiteral("bevel"), QStringLiteral("endcap"),
														 QStringLiteral("inverted-bevel"), QStringLiteral("inverted-endcap"), QStringLiteral("cylinder")} &&
					 parseLevelPatchCapStyle(QStringLiteral(" Inverted-Endcap "), &parsed) && parsed == LevelPatchCapStyle::InvertedEndcap &&
					 !parseLevelPatchCapStyle(QStringLiteral("wedge"), &parsed) && parsed == LevelPatchCapStyle::InvertedEndcap &&
					 !levelPatchCapStyleLabel(LevelPatchCapStyle::Cylinder).isEmpty() &&
					 levelPatchCapStyleWidths(LevelPatchCapStyle::Bevel) == QVector<int>{3, 5} &&
					 levelPatchCapStyleWidths(LevelPatchCapStyle::Cylinder) == QVector<int>{9} &&
					 levelPatchCapStyleWidths(LevelPatchCapStyle::Radial).isEmpty(),
				 "cap style ids, labels and NetRadiant Custom's widths");
	// Radiant's prefabs along Z: the cylinder's ring, the end cap's half
	// round and the bevel's quarter round, nine, five and three wide.
	struct Case {
		const char *shape;
		LevelPatchCapStyle style;
		// Facing of the first row's cap along Z: outward (down) for the
		// plain styles, the other way for the inverted ones, as upstream.
		bool down;
	};
	const Case cases[] = {{"cylinder", LevelPatchCapStyle::Cylinder, true},	 {"endcap", LevelPatchCapStyle::Endcap, true},
						  {"endcap", LevelPatchCapStyle::InvertedEndcap, false}, {"bevel", LevelPatchCapStyle::Bevel, true},
						  {"bevel", LevelPatchCapStyle::InvertedBevel, false},	 {"endcap", LevelPatchCapStyle::Bevel, true}};
	for (const Case &entry : cases) {
		LevelPatchCreateRequest create;
		create.shape = QString::fromLatin1(entry.shape);
		create.texture = QStringLiteral("studio/grid");
		LevelMapPatch source;
		ok &= expect(createLevelPatch(create, &source, &error), "prefab", error);
		source.id = 4;
		source.entityId = 0;
		for (bool invert : {false, true}) {
			for (auto uv : {LevelPatchCapUv::Planar, LevelPatchCapUv::Boundary}) {
				LevelPatchCapRequest request;
				request.style = entry.style;
				request.invert = invert;
				request.uv = uv;
				LevelPatchCapResult result;
				const QString label = QStringLiteral("%1 on %2").arg(levelPatchCapStyleId(entry.style), create.shape);
				if (!expect(prepareLevelPatchCaps(source, request, &result, &error) && result.caps.size() == 2 && result.style == entry.style,
							"seam caps both rows", label + QLatin1Char(' ') + error)) {
					ok = false;
					continue;
				}
				for (int edge = 0; edge < 2; ++edge) {
					// Upstream's Patch::MakeCap: the first row forwards, the last
					// backwards; Invert takes each the other way.
					QVector<LevelMapVec3> p;
					QVector<QPointF> uvs;
					const int row = edge == 0 ? 0 : source.height - 1;
					for (int i = 0; i < source.width; ++i) {
						const int column = (edge == 1) != invert ? source.width - 1 - i : i;
						p << source.controlPoints[row * source.width + column];
						uvs << QPointF(source.controlU[row * source.width + column], source.controlV[row * source.width + column]);
					}
					const auto expected = radiantSeam(entry.style, p);
					const auto &cap = result.caps[edge];
					bool same = cap.width == expected.width && cap.height == expected.height &&
								cap.controlPoints.size() == expected.points.size() && cap.textureName == source.textureName &&
								result.sources[edge] == source.id && result.reversed[edge] == ((edge == 1) != invert);
					for (qsizetype i = 0; same && i < expected.points.size(); ++i) {
						same = near(cap.controlPoints[i].x, expected.points[i].x) && near(cap.controlPoints[i].y, expected.points[i].y) &&
							   near(cap.controlPoints[i].z, expected.points[i].z);
					}
					ok &= expect(same, "the seam matches upstream's control layout", label);
					// Each boundary control keeps its texture coordinates with
					// Boundary mapping.
					if (uv == LevelPatchCapUv::Boundary) {
						bool kept = true;
						for (qsizetype i = 0; i < cap.controlPoints.size(); ++i) {
							for (qsizetype j = 0; j < p.size(); ++j) {
								if (near(cap.controlPoints[i].x, p[j].x) && near(cap.controlPoints[i].y, p[j].y) &&
									near(cap.controlPoints[i].z, p[j].z) &&
									!(near(cap.controlU[i], uvs[j].x()) && near(cap.controlV[i], uvs[j].y()))) {
									// A ring's first and last controls share a place
									// but not a texture coordinate.
									kept = kept && (j == 0 || j == p.size() - 1) &&
										   near(p.first().x, p.last().x) && near(p.first().y, p.last().y);
								}
							}
						}
						ok &= expect(kept, "boundary mapping keeps the boundary's coordinates", label);
					}
					const double z = facingZ(cap, 0.4, 0.45);
					const bool wantDown = (entry.down != invert) == (edge == 0);
					ok &= expect(std::abs(z) > 1e-9 && (z < 0) == wantDown, "the seam faces as upstream's does", label + QString::number(z));
				}
			}
		}
	}
	// NetRadiant Custom's width checks, with the reason.
	const auto cylinder = tests::capSource();
	LevelPatchCapRequest request;
	LevelPatchCapResult result;
	for (auto style : {LevelPatchCapStyle::Bevel, LevelPatchCapStyle::Endcap, LevelPatchCapStyle::InvertedEndcap}) {
		request.style = style;
		ok &= expect(!prepareLevelPatchCaps(cylinder, request, &result, &error) && error.contains(QStringLiteral("9")),
					 "a nine-wide boundary is refused for the bevel and end caps", error);
	}
	request.style = LevelPatchCapStyle::Cylinder;
	request.customCenter = true;
	request.boundaries = {LevelPatchBoundary::FirstRow};
	ok &= expect(!prepareLevelPatchCaps(cylinder, request, &result, &error), "only radial caps take a custom centre");
	// Columns: a transposed cylinder faces in, and its column seams face the
	// way its radial caps do.
	request = {};
	request.style = LevelPatchCapStyle::Cylinder;
	request.boundaries = {LevelPatchBoundary::FirstColumn, LevelPatchBoundary::LastColumn};
	LevelPatchCapRequest radial = request;
	radial.style = LevelPatchCapStyle::Radial;
	LevelPatchCapResult radialResult;
	ok &= expect(prepareLevelPatchCaps(tests::capSource(true), request, &result, &error) && result.caps.size() == 2 &&
					 prepareLevelPatchCaps(tests::capSource(true), radial, &radialResult, &error) &&
					 facingZ(result.caps[0], 0.4, 0.45) > 0 && facingZ(result.caps[1], 0.4, 0.45) < 0 &&
					 facingZ(radialResult.caps[0], 0.4, 0.45) > 0 && facingZ(radialResult.caps[1], 0.4, 0.45) < 0,
				 "column seams face as the radial caps do", error);
	// Cap Selection: every patch at once, wrong widths skipped, one undo step,
	// the caps selected in their own entities, hidden patches left alone.
	LevelMapDocument map;
	ok &= expect(tests::createCapMap(&map, &error), "map", error);
	LevelPatchCreateRequest endcap;
	endcap.shape = QStringLiteral("endcap");
	endcap.center = {256, 0, 0, true};
	endcap.texture = QStringLiteral("studio/arch");
	LevelMapPatch arch;
	int archId = -1;
	ok &= expect(createLevelPatch(endcap, &arch, &error) && addLevelMapPatch(&map, arch, &archId, &error), "second patch", error);
	const auto before = serializeLevelMap(map).bytes;
	const auto depth = map.undoStack.size();
	request = {};
	request.style = LevelPatchCapStyle::Endcap;
	request.texture = QStringLiteral("textures/studio/cap");
	ok &= expect(capLevelMapPatches(&map, {0, archId}, request, &result, &error) && result.caps.size() == 2 && result.skipped.size() == 1 &&
					 result.skipped.first().contains(QStringLiteral("0")) && map.undoStack.size() == depth + 1 &&
					 map.patches.size() == 4 && map.selection.size() == 2 && result.caps[0].textureName == QStringLiteral("studio/cap"),
				 "Cap Selection caps what it can in one step and lists the rest", error);
	const auto json = levelPatchCapReportJson(result);
	ok &= expect(json.value(QStringLiteral("style")).toString() == QStringLiteral("endcap") &&
					 json.value(QStringLiteral("skipped")).toArray().size() == 1 &&
					 json.value(QStringLiteral("caps")).toArray().first().toObject().value(QStringLiteral("source")).toInt() == archId,
				 "the report names the style, sources and skipped patches");
	ok &= expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == before, "undo removes every cap", error);
	ok &= expect(!capLevelMapPatches(&map, {0}, request, &result, &error) && map.undoStack.size() == depth && !error.isEmpty(),
				 "nothing capped is a refusal that changes nothing");
	ok &= expect(!capLevelMapPatches(&map, {0, 0}, request, &result, &error), "a patch named twice is refused");
	ok &= expect(!capLevelMapPatches(&map, {99}, request, &result, &error), "a missing patch is refused");
	LevelVisibilityRequest hide;
	ok &= expect(setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakePatch, archId}}, &error) &&
					 changeLevelMapVisibility(&map, hide, nullptr, &error),
				 "hide the arch", error);
	ok &= expect(!capLevelMapPatches(&map, {archId}, request, &result, &error) && error.contains(QStringLiteral("hidden")),
				 "a hidden patch is not capped", error);
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return EXIT_FAILURE;
	}
	bool ok = checkStyles();
	QString error;
	for (bool open : {false, true}) {
		for (int edge = 0; edge < 4; ++edge) {
			for (bool invert : {false, true}) {
				for (auto uv : {LevelPatchCapUv::Planar, LevelPatchCapUv::Boundary}) {
					const auto p = tests::capSource(edge >= 2, open);
					const auto original = levelPatchDefinition(p);
					LevelPatchCapRequest request;
					request.boundaries = {static_cast<LevelPatchBoundary>(edge)};
					request.invert = invert;
					request.uv = uv;
					LevelPatchCapResult result;
					if (!expect(prepareLevelPatchCaps(p, request, &result, &error), "closed and open caps on every boundary", error)) {
						ok = false;
						continue;
					}
					const auto &cap = result.caps.first();
					const auto center = result.centers.first();
					ok &= expect(levelPatchDefinition(p) == original && result.closed.first() != open && cap.height == 3,
								 "source retained and correct closure type");
					for (int i = 0; i <= 40; ++i) {
						const double u = i / 40.0, t = result.reversed.first() ? 1 - u : u;
						const auto boundary = tests::samplePatch(p, edge < 2 ? t : edge - 2, edge < 2 ? edge : t);
						for (int j = 0; j <= 10; ++j) {
							const double radial = j / 10.0;
							const auto point = tests::samplePatch(cap, u, radial);
							ok &= expect(near(point[0], center.x + (boundary[0] - center.x) * radial) &&
											 near(point[1], center.y + (boundary[1] - center.y) * radial) &&
											 near(point[2], center.z + (boundary[2] - center.z) * radial),
										 "independent evaluator verifies entire radial surface and exact seam");
							if (uv == LevelPatchCapUv::Boundary && j == 10) {
								ok &= expect(near(point[3], boundary[3]) && near(point[4], boundary[4]),
											 "outer curve retains full UV function");
							}
						}
					}
					const auto a = tests::samplePatch(cap, 0.1, 0.7), b = tests::samplePatch(cap, 0.1001, 0.7),
							   c = tests::samplePatch(cap, 0.1, 0.7001);
					const double normalZ = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
					const bool positive = ((edge == 1 || edge == 2) != invert) != open;
					ok &= expect((normalZ > 0) == positive, "caps face consistently with source orientation");
				}
			}
		}
	}
	auto source = tests::capSource();
	{
		auto rotated = source;
		const double a = 0.7, b = 0.4;
		for (auto &p : rotated.controlPoints) {
			const double x = p.x, y = p.y * std::cos(a) - p.z * std::sin(a), z = p.y * std::sin(a) + p.z * std::cos(a);
			p = {x * std::cos(b) - y * std::sin(b), x * std::sin(b) + y * std::cos(b), z, true};
		}
		LevelPatchCapResult tilted;
		ok &= expect(prepareLevelPatchCaps(rotated, {}, &tilted, &error), "caps on arbitrarily oriented planes", error);
		if (!tilted.caps.isEmpty()) {
			const auto p = tests::samplePatch(tilted.caps.first(), 0.1, 0.7), q = tests::samplePatch(tilted.caps.first(), 0.1001, 0.7),
					   r = tests::samplePatch(tilted.caps.first(), 0.1, 0.7001);
			const double nx = (q[1] - p[1]) * (r[2] - p[2]) - (q[2] - p[2]) * (r[1] - p[1]);
			const double ny = (q[2] - p[2]) * (r[0] - p[0]) - (q[0] - p[0]) * (r[2] - p[2]);
			const double nz = (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]);
			ok &=
				expect(nx * std::sin(b) * std::sin(a) - ny * std::cos(b) * std::sin(a) + nz * std::cos(a) < 0, "rotated cap faces outward");
		}
		auto maximum = source;
		maximum.width = 31;
		maximum.controlPoints.clear();
		maximum.controlU.clear();
		maximum.controlV.clear();
		for (int row = 0; row < 3; ++row) {
			for (int column = 0; column < 31; ++column) {
				const double theta = column * 2 * std::acos(-1.0) / 30;
				maximum.controlPoints << LevelMapVec3{64 * std::cos(theta), 64 * std::sin(theta), (row - 1) * 64.0, true};
				maximum.controlU << column / 30.0;
				maximum.controlV << row * 0.5;
			}
		}
		ok &= expect(prepareLevelPatchCaps(maximum, {}, &tilted, &error) && tilted.caps.first().width == 31,
					 "maximum supported boundary grid", error);
	}
	LevelPatchCapRequest request;
	LevelPatchCapResult result;
	for (int spans : {4, 8, 12}) {
		auto refined = source;
		ok &= refineLevelPatch(&refined, true, spans, &error);
		ok &= expect(prepareLevelPatchCaps(refined, request, &result, &error) && result.caps.first().width == spans * 2 + 1,
					 "caps follow exact refined boundary grids", error);
	}
	request.boundaries = {LevelPatchBoundary::FirstRow};
	request.customCenter = true;
	request.center = {8, 4, -64, true};
	ok &= expect(prepareLevelPatchCaps(source, request, &result, &error), "off-center star-shaped cap", error);
	request.center = {1000, 0, -64, true};
	ok &= expect(!prepareLevelPatchCaps(source, request, &result, &error), "outside center refused");
	request.center = {0, 0, 0, true};
	ok &= expect(!prepareLevelPatchCaps(source, request, &result, &error), "off-plane center refused");
	request = {};
	source.fixedSubdivisions = true;
	source.subdivisionsX = 7;
	source.subdivisionsY = 3;
	source.headerTail = {1, 2, 3};
	ok &= expect(prepareLevelPatchCaps(source, request, &result, &error) && result.caps.first().subdivisionsX == 7 &&
					 result.caps.first().subdivisionsY == 1 && result.caps.first().headerTail == source.headerTail,
				 "fixed seam tessellation and header extensions retained", error);
	source = tests::capSource();
	const auto stable = levelPatchCapReportJson(result);
	auto invalid = source;
	invalid.controlPoints[1].z += 1;
	ok &= expect(!prepareLevelPatchCaps(invalid, request, &result, &error) && levelPatchCapReportJson(result) == stable,
				 "nonplanar input leaves result intact");
	invalid = source;
	std::swap(invalid.controlPoints[1], invalid.controlPoints[5]);
	ok &= expect(!prepareLevelPatchCaps(invalid, request, &result, &error), "folded control directions refused");
	invalid = source;
	for (int i = 0; i < 9; ++i) {
		invalid.controlPoints[i] = invalid.controlPoints[0];
	}
	ok &= expect(!prepareLevelPatchCaps(invalid, request, &result, &error), "collapsed end refused");
	ok &= expect(!prepareLevelPatchCaps(source, request, &result, &error, [] { return true; }), "cancelled draft refused");
	request.unitsPerTile = std::numeric_limits<double>::infinity();
	ok &= expect(!prepareLevelPatchCaps(source, request, &result, &error), "nonfinite UV scale refused");
	request = {};
	request.boundaries = {LevelPatchBoundary::FirstRow, LevelPatchBoundary::FirstRow};
	ok &= expect(!prepareLevelPatchCaps(source, request, &result, &error), "duplicate boundary refused");
	request = {};
	LevelMapDocument map;
	ok &= expect(tests::createCapMap(&map, &error), "create document", error);
	const auto input = serializeLevelMap(map).bytes;
	const auto depth = map.undoStack.size();
	const auto revision = map.revision;
	ok &= expect(capLevelMapPatch(&map, 0, request, &result, &error) && map.patches.size() == 3 && map.undoStack.size() == depth + 1 &&
					 map.revision == revision + 1,
				 "both caps commit in one revision and undo", error);
	const auto after = serializeLevelMap(map).bytes;
	ok &= expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == input && redoLevelMapEdit(&map, &error) &&
					 serializeLevelMap(map).bytes == after,
				 "exact source undo and redo", error);
	LevelMapDocument loaded;
	ok &= expect(loadLevelMapBytes({QStringLiteral("caps.map"), {}, {}}, after, &loaded, &error) && loaded.patches.size() == 3,
				 "serialized cap round trip", error);
	// All cap surfaces inherit the source entity, including non-world owners.
	QByteArray entity = "{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"func_static\"\n\"custom\" \"retain\"\n// original curve\n";
	entity += levelPatchDefinition(source).join('\n').toUtf8() + "\n}\n";
	ok &= expect(loadLevelMapBytes({QStringLiteral("entity.map"), {}, {}}, entity, &map, &error) &&
					 capLevelMapPatch(&map, 0, request, &result, &error),
				 "loaded entity caps", error);
	ok &= expect(map.patches.size() == 3 && map.patches[1].entityId == 1 && map.patches[2].entityId == 1 &&
					 serializeLevelMap(map).bytes.contains("// original curve") &&
					 serializeLevelMap(map).bytes.contains("\"custom\" \"retain\""),
				 "owner and unrelated source text retained");
	const auto beforeBad = serializeLevelMap(map).bytes;
	const auto badDepth = map.undoStack.size();
	invalid = source;
	invalid.controlV.clear();
	QVector<int> ids{999};
	ok &= expect(!addLevelMapPatches(&map, {source, invalid}, 1, &ids, &error) && ids == QVector<int>{999} &&
					 serializeLevelMap(map).bytes == beforeBad && map.undoStack.size() == badDepth,
				 "invalid second insertion changes nothing");
	QByteArray shared = entity;
	shared.chop(4);
	shared += "}}\n";
	ok &= expect(loadLevelMapBytes({QStringLiteral("shared.map"), {}, {}}, shared, &map, &error) &&
					 !capLevelMapPatch(&map, 0, request, &result, &error),
				 "shared owner closing line refused", error);
	if (argc > 1) {
		const auto in = QDir(temp.path()).filePath(QStringLiteral("source.map")),
				   out = QDir(temp.path()).filePath(QStringLiteral("capped.map"));
		QFile f(in);
		ok &= f.open(QIODevice::WriteOnly) && f.write(input) == input.size();
		f.close();
		const auto run = [&](const QStringList &args, int code, QJsonObject *json = nullptr) {
			QProcess p;
			p.start(QString::fromLocal8Bit(argv[1]),
					QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"),
								QDir(temp.path()).filePath(QStringLiteral("settings.ini")), QStringLiteral("map"),
								QStringLiteral("cap-patch"), QStringLiteral("--json")} +
						args + QStringList{in});
			const bool done = p.waitForFinished(30000);
			const auto bytes = p.readAllStandardOutput();
			if (json) {
				*json = QJsonDocument::fromJson(bytes).object();
			}
			return expect(done && p.exitStatus() == QProcess::NormalExit && p.exitCode() == code, "CLI cap result",
						  QString::fromUtf8(bytes + p.readAllStandardError()));
		};
		const QStringList args{
			QStringLiteral("--patch"),	  QStringLiteral("0"),		  QStringLiteral("--boundary"), QStringLiteral("first-row"),
			QStringLiteral("--boundary"), QStringLiteral("last-row"), QStringLiteral("--output"),	out};
		QJsonObject json;
		ok &= run(args + QStringList{QStringLiteral("--dry-run")}, 0, &json);
		ok &=
			expect(!QFile::exists(out) && json.value(QStringLiteral("cap")).toObject().value(QStringLiteral("caps")).toArray().size() == 2,
				   "dry run reports both caps without writes");
		ok &= run(args + QStringList{QStringLiteral("--invert"), QStringLiteral("--dry-run")}, 0);
		ok &= run(args, 0);
		const auto saved = read(out);
		ok &= run(args, 4);
		ok &= expect(read(in) == input && read(out) == saved, "source and existing output protected");
		ok &= run(args + QStringList{QStringLiteral("--uv"), QStringLiteral("bad")}, 2);
		ok &= run(args + QStringList{QStringLiteral("--boundary")}, 2);
		// Repeated --patch caps several; a missing one refuses the edit.
		ok &= run(args + QStringList{QStringLiteral("--patch"), QStringLiteral("1")}, 4);
		ok &= run(args + QStringList{QStringLiteral("--patch"), QStringLiteral("0")}, 2);
		ok &= run(args + QStringList{QStringLiteral("--style"), QStringLiteral("wedge"), QStringLiteral("--dry-run")}, 2);
		ok &= run(args + QStringList{QStringLiteral("--style"), QStringLiteral("bevel"), QStringLiteral("--dry-run")}, 4);
		ok &= run(args + QStringList{QStringLiteral("--style"), QStringLiteral("cylinder"), QStringLiteral("--center"), QStringLiteral("0,0,-64"),
									 QStringLiteral("--dry-run")},
				  4);
		ok &= run({QStringLiteral("--patch"), QStringLiteral("0"), QStringLiteral("--style"), QStringLiteral("cylinder"), QStringLiteral("--texture"),
				   QStringLiteral("studio/cap"), QStringLiteral("--output"), out, QStringLiteral("--overwrite"), QStringLiteral("--dry-run")},
				  0, &json);
		const auto caps = json.value(QStringLiteral("cap")).toObject();
		ok &= expect(caps.value(QStringLiteral("style")).toString() == QStringLiteral("cylinder") &&
						 caps.value(QStringLiteral("caps")).toArray().size() == 2 &&
						 caps.value(QStringLiteral("caps")).toArray().first().toObject().value(QStringLiteral("grid")).toArray() == QJsonArray{3, 5},
					 "map cap-patch --style cylinder caps both rows without --boundary");
		ok &= run(args + QStringList{QStringLiteral("--center"), QStringLiteral("nan,0,0")}, 2);
		ok &= run(args + QStringList{QStringLiteral("--boundary"), QStringLiteral("first-row")}, 4);
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
