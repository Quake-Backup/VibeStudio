// Doom 3 collision: collision surfaces in ASE exports and trace-model clip
// models (ATT-COLL-02).
#include "core/model_collision_doom3.h"
#include "core/model_collision_hull.h"
#include "core/model_document.h"
#include "core/package_archive.h"
#include "tests/model_assembly_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <map>

using namespace vibestudio;
namespace
{
int checks = 0;
bool expect(bool value, const char *message, const QString &detail = {})
{
	++checks;
	if (!value)
		std::cerr << "FAIL: " << message << ' ' << qPrintable(detail) << '\n';
	return value;
}
bool write(const QString &path, const QByteArray &data)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
const ModelSurface *surfaceNamed(const ModelMesh &mesh, const QString &name)
{
	for (const auto &surface : mesh.surfaces)
		if (surface.name == name)
			return &surface;
	return nullptr;
}
// Every edge, by rounded position, is used exactly twice and in opposite
// directions, and every triangle faces away from the volume's centre.
bool closedOutward(const ModelSurface &surface, ModelVec3 centre)
{
	const auto key = [](ModelVec3 p) {
		return std::array<long long, 3>{std::llround(p.x * 1000.0), std::llround(p.y * 1000.0), std::llround(p.z * 1000.0)};
	};
	std::map<std::pair<std::array<long long, 3>, std::array<long long, 3>>, int> directed;
	const auto &positions = surface.frames[0].positions;
	for (const auto &t : surface.triangles)
	{
		const ModelVec3 a = positions[t.a], b = positions[t.b], c = positions[t.c];
		const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
		const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
		const double mx = (a.x + b.x + c.x) / 3 - centre.x, my = (a.y + b.y + c.y) / 3 - centre.y, mz = (a.z + b.z + c.z) / 3 - centre.z;
		if (nx * mx + ny * my + nz * mz <= 0)
			return false;
		for (const auto &[from, to] : {std::pair{a, b}, std::pair{b, c}, std::pair{c, a}})
			++directed[{key(from), key(to)}];
	}
	for (const auto &[edge, count] : directed)
	{
		const auto reverse = directed.find({edge.second, edge.first});
		if (count != 1 || reverse == directed.end() || reverse->second != 1)
			return false;
	}
	return true;
}
// Reads a .cm file as Doom 3's CollisionModel_files.cpp grammar lays it out
// and checks the model is closed, convex-wound and consistent: every edge used
// once each way, polygons counter-clockwise about their outward normal, planes
// and bounds matching their vertices.
struct CmSummary
{
	bool valid = false;
	QString name, material;
	int vertices = 0, edges = 0, polygons = 0;
};
CmSummary readCm(const QByteArray &bytes)
{
	CmSummary summary;
	QString text = QString::fromUtf8(bytes);
	static const QRegularExpression comment(QStringLiteral("/\\*.*?\\*/"));
	text.remove(comment);
	const auto tokens = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
	int at = 0;
	const auto next = [&] { return at < tokens.size() ? tokens[at++] : QString(); };
	const auto expectToken = [&](const char *token) { return next() == QLatin1String(token); };
	const auto number = [&] { return next().toDouble(); };
	const auto readVector = [&](ModelVec3 *v) {
		if (!expectToken("("))
			return false;
		v->x = float(number());
		v->y = float(number());
		v->z = float(number());
		return expectToken(")");
	};
	if (!expectToken("CM") || !expectToken("\"1.00\"") || !expectToken("0") || !expectToken("collisionModel"))
		return summary;
	summary.name = next();
	QVector<ModelVec3> vertices;
	QVector<QPair<int, int>> edges;
	if (!expectToken("{") || !expectToken("vertices") || !expectToken("{"))
		return summary;
	for (int n = next().toInt(); n > 0; --n)
	{
		ModelVec3 v;
		if (!readVector(&v))
			return summary;
		vertices << v;
	}
	if (!expectToken("}") || !expectToken("edges") || !expectToken("{"))
		return summary;
	QVector<int> users;
	for (int n = next().toInt(); n > 0; --n)
	{
		if (!expectToken("("))
			return summary;
		const int a = next().toInt(), b = next().toInt();
		if (!expectToken(")") || next() != "0")
			return summary;
		users << next().toInt();
		edges << QPair<int, int>{a, b};
	}
	if (edges.isEmpty() || edges[0] != QPair<int, int>{0, 0} || !expectToken("}") || !expectToken("nodes") || !expectToken("{") ||
		!expectToken("(") || !expectToken("-1") || !expectToken("0") || !expectToken(")") || !expectToken("}") || !expectToken("polygons") ||
		!expectToken("{"))
		return summary;
	QVector<int> forward(edges.size()), backward(edges.size());
	while (at < tokens.size() && tokens[at] != "}")
	{
		const int count = next().toInt();
		if (count < 3 || !expectToken("("))
			return summary;
		QVector<int> ring;
		for (int i = 0; i < count; ++i)
		{
			const int e = next().toInt();
			if (e == 0 || std::abs(e) >= edges.size())
				return summary;
			++(e > 0 ? forward : backward)[std::abs(e)];
			ring << (e > 0 ? edges[e].first : edges[-e].second);
			const int end = e > 0 ? edges[e].second : edges[-e].first;
			ring << end;
		}
		ModelVec3 normal, lo, hi;
		if (!expectToken(")") || !readVector(&normal))
			return summary;
		const double dist = number();
		if (!readVector(&lo) || !readVector(&hi))
			return summary;
		summary.material = next();
		// Consecutive edges join, and the ring closes.
		for (int i = 0; i < count; ++i)
			if (ring[2 * i + 1] != ring[(2 * i + 2) % (2 * count)])
				return summary;
		const auto p = [&](int i) { return vertices[ring[2 * i]]; };
		const ModelVec3 a = p(0), b = p(1), c = p(2);
		const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
		if ((uy * vz - uz * vy) * normal.x + (uz * vx - ux * vz) * normal.y + (ux * vy - uy * vx) * normal.z <= 0)
			return summary;
		for (int i = 0; i < count; ++i)
		{
			const auto q = p(i);
			if (std::abs(double(normal.x) * q.x + double(normal.y) * q.y + double(normal.z) * q.z - dist) > 1e-3 || q.x < lo.x - 1e-4 ||
				q.y < lo.y - 1e-4 || q.z < lo.z - 1e-4 || q.x > hi.x + 1e-4 || q.y > hi.y + 1e-4 || q.z > hi.z + 1e-4)
				return summary;
		}
		++summary.polygons;
	}
	if (!expectToken("}") || !expectToken("}") || at != tokens.size())
		return summary;
	for (int e = 1; e < edges.size(); ++e)
		if (forward[e] != 1 || backward[e] != 1 || users[e] != 2)
			return summary;
	summary.vertices = int(vertices.size());
	summary.edges = int(edges.size()) - 1;
	summary.valid = true;
	return summary;
}
ModelMesh fixture()
{
	auto mesh = tests::assemblyModel();
	ModelCollisionBox body;
	body.name = QStringLiteral("body");
	body.centre = {2, 3, 4};
	body.size = {10, 8, 6};
	body.rotation = {0, 0, 30};
	ModelCollisionBox upright = body;
	upright.name = QStringLiteral("upright");
	upright.rotation = {};
	ModelCollisionBox shell;
	makeModelCollisionHull(QStringLiteral("shell"), {{10, 0, 0}, {-10, 0, 0}, {0, 12, 0}, {0, -12, 0}, {0, 0, 8}, {0, 0, -8}}, &shell);
	mesh.collisionBoxes = {body, upright, shell};
	return mesh;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("collision-doom3-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	bool ok = true;
	QString error;
	const auto mesh = fixture();
	ok &= expect(validateEditableModel(mesh).isEmpty(), "fixture is a valid editable model");

	// Collision surfaces: closed, outward, one per volume.
	QVector<ModelSurface> surfaces;
	ok &= expect(modelCollisionSurfaces(mesh, {}, &surfaces, &error) && surfaces.size() == 3 && surfaces[0].name == "collision_body" &&
					 surfaces[0].triangles.size() == 12 && surfaces[2].triangles.size() == 8 &&
					 surfaces[0].skinPaths == QStringList{"textures/common/collision"},
				 "one collision surface per box and hull, with the collision material", error);
	if (!ok)
		return 1;
	ok &= expect(closedOutward(surfaces[0], mesh.collisionBoxes[0].centre) && closedOutward(surfaces[2], mesh.collisionBoxes[2].centre),
				 "collision surfaces are closed and wound outwards");

	// ASE: render surfaces as a plain export writes them, plus collision.
	ModelExportReport plainReport, report;
	auto plainMesh = mesh;
	const auto plain = exportEditableModel(plainMesh, QStringLiteral("ase"), 0, &error, {}, &plainReport);
	const auto withCollision = exportModelAseWithCollision(mesh, {}, &error, {}, &report);
	ok &= expect(!plain.isEmpty() && !withCollision.isEmpty(), "both ASE exports succeed", error);
	const auto block = [](const QByteArray &bytes) {
		const auto first = bytes.indexOf("*GEOMOBJECT");
		const auto second = bytes.indexOf("*GEOMOBJECT", first + 1);
		return first < 0 ? QByteArray() : bytes.mid(first, second < 0 ? -1 : second - first);
	};
	ok &= expect(!block(plain).isEmpty() && withCollision.contains(block(plain)), "the render surface is written byte for byte as before");
	ok &= expect(plainReport.notes.join(' ').contains("Collision boxes remain") && !report.notes.join(' ').contains("Collision boxes remain") &&
					 report.notes.join(' ').contains("collision keyword"),
				 "notes: the plain export reports the omission; the collision export explains Doom 3's rule");
	const auto decoded = decodeModelMesh(QStringLiteral("collision.ase"), withCollision);
	const auto *render = surfaceNamed(decoded, "triangle");
	const auto *body = surfaceNamed(decoded, "collision_body");
	const auto *shell = surfaceNamed(decoded, "collision_shell");
	ok &= expect(decoded.surfaces.size() == 4 && render && body && shell && body->triangles.size() == 12 && shell->triangles.size() == 8 &&
					 body->skinPaths.value(0).contains("textures/common/collision"),
				 "the decoded ASE holds the render surface and the collision surfaces with their material");
	if (body && shell)
		ok &= expect(closedOutward(*body, mesh.collisionBoxes[0].centre) && closedOutward(*shell, mesh.collisionBoxes[2].centre),
					 "decoded collision surfaces stay closed and outward");
	auto clash = mesh;
	clash.surfaces[0].name = QStringLiteral("collision_body");
	ok &= expect(exportModelAseWithCollision(clash, {}, &error).isEmpty(), "a render surface named like a collision surface is refused");
	ok &= expect(exportModelAseWithCollision(mesh, {QStringLiteral("textures/../bad"), 0}, &error).isEmpty() &&
					 exportModelAseWithCollision(mesh, {QStringLiteral("textures/common/collision"), 5}, &error).isEmpty(),
				 "unsafe materials and missing frames are refused");

	// Clip models: trace-model budget.
	ModelDoom3ClipModel clip;
	ok &= expect(exportModelDoom3ClipModel(mesh, "body", {}, &clip, &error) && clip.faces == 6 && clip.edges == 12 && clip.vertices == 8 &&
					 !clip.hasBounds && decodeModelMesh(QStringLiteral("clip.ase"), clip.bytes).surfaces.size() == 1,
				 "a box becomes a one-volume clip model", error);
	ok &= expect(exportModelDoom3ClipModel(mesh, "upright", {}, &clip, &error) && clip.hasBounds && clip.mins.x == -3 && clip.maxs.z == 7,
				 "an axis-aligned box also reports mins and maxs", error);
	ok &= expect(exportModelDoom3ClipModel(mesh, "shell", {}, &clip, &error) && clip.faces == 8 && clip.vertices == 6,
				 "an in-budget hull becomes a clip model", error);
	auto dense = mesh;
	QVector<ModelVec3> ball;
	for (int i = 0; i < 40; ++i)
	{
		const double y = 1 - 2 * (i + .5) / 40, r = std::sqrt(1 - y * y), angle = 2.39996322972865332 * i;
		ball << ModelVec3{float(10 * r * std::cos(angle)), float(10 * y), float(10 * r * std::sin(angle))};
	}
	ModelCollisionBox round;
	ok &= expect(makeModelCollisionHull(QStringLiteral("round"), ball, &round, &error), "a dense hull", error);
	dense.collisionBoxes << round;
	ok &= expect(!exportModelDoom3ClipModel(dense, "round", {}, &clip, &error) && error.contains("16") &&
					 !exportModelDoom3ClipModel(mesh, "missing", {}, &clip, &error),
				 "an over-budget hull and a missing volume are refused");

	// Collision model files (.cm): the format Doom 3 reads beside a static model.
	ModelDoom3CollisionModelFile cm;
	ok &= expect(exportModelDoom3CollisionModelFile(mesh, "models/props/prop.ase", {}, &cm, &error) && cm.polygons == 20 && cm.edges == 36 &&
					 cm.vertices == 22 && cm.filePath == "models/props/prop.cm",
				 "every volume's faces become polygons of one collision model", error);
	const auto summary = readCm(cm.bytes);
	ok &= expect(summary.valid && summary.name == "\"models/props/prop.ase\"" && summary.material == "\"textures/common/collision\"" &&
					 summary.polygons == 20 && summary.edges == 36 && summary.vertices == 22,
				 "the .cm file is closed, wound outwards and consistent", QString::fromUtf8(cm.bytes.left(400)));
	ok &= expect(!exportModelDoom3CollisionModelFile(mesh, "models/props/prop.md5mesh", {}, &cm, &error) &&
					 !exportModelDoom3CollisionModelFile(mesh, "../prop.ase", {}, &cm, &error) &&
					 !exportModelDoom3CollisionModelFile(mesh, "models/props/prop.ase", {QStringLiteral("textures/common/collision"), 9}, &cm,
														  &error),
				 "model paths other than static .ase/.lwo/.ma, unsafe paths and missing frames are refused");

	// The project's material scripts: the collision keyword decides whether
	// Doom 3 treats the surfaces as collision.
	ok &= expect(QDir().mkpath(path("package/materials")) &&
					 write(path("package/materials/props.mtr"), "textures/props/clip_good\n{\n\tnoshadows\n\tcollision\n}\n"
																 "textures/props/clip_bad\n{\n\tnoshadows\n\tnonsolid\n}\n"),
				 "material script written");
	PackageArchive package;
	ok &= expect(package.load(path("package"), &error), "folder package opens", error);
	const auto good = checkModelDoom3CollisionMaterial(package, "textures/props/clip_good");
	const auto bad = checkModelDoom3CollisionMaterial(package, "textures/props/clip_bad");
	const auto missing = checkModelDoom3CollisionMaterial(package, "textures/props/clip_none");
	ok &= expect(good.state == ModelDoom3CollisionMaterialState::Defined && good.note.isEmpty() && good.sourcePath.contains("props.mtr"),
				 "a material with the collision keyword passes", good.note);
	ok &= expect(bad.state == ModelDoom3CollisionMaterialState::NoCollisionKey && bad.note.contains("collision keyword") &&
					 modelDoom3CollisionMaterialStateId(bad.state) == "no-collision-keyword",
				 "a material without the collision keyword is reported", bad.note);
	ok &= expect(missing.state == ModelDoom3CollisionMaterialState::Undefined && !missing.note.isEmpty(), "an undefined material is reported");
	ModelWorkControl cancelled;
	cancelled.cancelled = [] { return true; };
	ok &= expect(checkModelDoom3CollisionMaterial(package, "textures/props/clip_bad", cancelled).state ==
					 ModelDoom3CollisionMaterialState::Unchecked,
				 "a cancelled scan reports nothing");

	if (argc > 1)
	{
		ok &= expect(write(path("prop.mesh.json"), serializeEditableModel(mesh, &error)), "CLI fixture written", error);
		QJsonObject output;
		const auto run = [&](QStringList args, int code = 0) {
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--settings-file", path("settings.ini")} + args + QStringList{"--json"});
			if (!process.waitForFinished(60000))
			{
				process.kill();
				process.waitForFinished();
				return false;
			}
			output = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
			if (process.exitCode() != code)
				std::cerr << qPrintable(QString::fromUtf8(process.readAllStandardError())) << '\n';
			return process.exitStatus() == QProcess::NormalExit && process.exitCode() == code;
		};
		const QStringList ase{"model", "collision", "prop.mesh.json", "--operation", "export-ase", "--target", "doom3", "--output", "prop.ase"};
		ok &= expect(run(ase + QStringList{"--dry-run"}) && !QFileInfo::exists(path("prop.ase")), "CLI dry run writes nothing");
		ok &= expect(run(ase) && output.value("collisionSurfaces").toInt() == 3 && read(path("prop.ase")) == withCollision,
					 "CLI writes the same ASE as the shared service");
		ok &= expect(run(ase, 4) && run(ase + QStringList{"--overwrite"}), "CLI protects an existing output unless --overwrite");
		ok &= expect(run({"model", "collision", "prop.mesh.json", "--operation", "export-ase", "--target", "quake3", "--output", "x.ase"}, 2) &&
						 run({"model", "collision", "prop.mesh.json", "--operation", "export-ase", "--target", "doom3", "--output", "x.map"}, 2),
					 "CLI requires --target doom3 and an .ase output");
		ok &= expect(run({"model", "collision", "prop.mesh.json", "--operation", "export-clipmodel", "--target", "doom3", "--box", "upright",
						  "--output", "clip.ase"}) &&
						 output.value("clipModel").toObject().value("faces").toInt() == 6 &&
						 output.value("clipModel").toObject().value("mins").toString() == "-3 -1 1",
					 "CLI writes a clip model and reports equivalent bounds");
		ok &= expect(run({"model", "collision", "prop.mesh.json", "--operation", "export-clipmodel", "--target", "doom3", "--output", "clip2.ase"}, 2),
					 "CLI export-clipmodel requires --box");
		// A fresh output each time: the check reports, the export still writes.
		const QStringList checked{"model", "collision", "prop.mesh.json", "--operation", "export-ase", "--target", "doom3", "--output", "checked.ase"};
		ok &= expect(run(checked + QStringList{"--material", "textures/props/clip_bad", "--package", "package"}) &&
						 output.value("materialCheck").toObject().value("state").toString() == "no-collision-keyword" &&
						 output.value("notes").toArray().size() > 1 && QFileInfo::exists(path("checked.ase")),
					 "CLI reports a collision material without the keyword and still writes the export");
		ok &= expect(run(checked + QStringList{"--material", "textures/props/clip_good", "--package", "package", "--overwrite", "--dry-run"}) &&
						 output.value("materialCheck").toObject().value("state").toString() == "defined" &&
						 run({"model", "collision", "prop.mesh.json", "--operation", "export-clipmodel", "--target", "doom3", "--box", "upright",
							  "--package", "package", "--output", "clip3.ase"},
							 2),
					 "CLI checks a defined material; --package applies to export-ase only");
		ok &= expect(run({"model", "collision", "prop.mesh.json", "--operation", "export-cm", "--target", "doom3", "--model-path",
						  "models/props/prop.ase", "--output", "prop.cm"}) &&
						 output.value("collisionModel").toObject().value("polygons").toInt() == 20 &&
						 output.value("collisionModel").toObject().value("gamePath").toString() == "models/props/prop.cm" &&
						 read(path("prop.cm")) == cm.bytes,
					 "CLI writes the same .cm file as the shared service");
		ok &= expect(run({"model", "collision", "prop.mesh.json", "--operation", "export-cm", "--target", "doom3", "--output", "x.cm"}, 2) &&
						 run({"model", "collision", "prop.mesh.json", "--operation", "export-cm", "--target", "doom3", "--model-path",
							  "models/props/prop.ase", "--output", "x.ase"},
							 2),
					 "CLI export-cm requires --model-path and a .cm output");
	}
	std::cout << checks << " Doom 3 collision checks\n";
	return ok ? 0 : 1;
}
