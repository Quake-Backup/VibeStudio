#include "core/model_document.h"
#include "core/model_uv.h"
#include "core/model_uv_mapping.h"
#include "core/model_uv_unwrap.h"
#include "tests/model_uv_islands_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <numbers>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *message, const QString &detail = {})
{
	++checks;
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << message << ": " << qPrintable(detail) << '\n';
	}
	return value;
}
QByteArray source(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
template <typename T> QByteArray raw(const QVector<T> &values)
{
	return QByteArray(reinterpret_cast<const char *>(values.constData()), qsizetype(values.size() * sizeof(T)));
}
// One surface with two poses (the second lifted by 8 in Z) from positions,
// normals and triangles; UVs start as a planar XY projection.
ModelMesh shape(const QVector<ModelVec3> &points, const QVector<ModelVec3> &normals, const QVector<ModelTriangle> &triangles)
{
	auto mesh = uvSquare();
	mesh.tags.clear();
	auto &s = mesh.surfaces[0];
	s.triangles = triangles;
	s.texCoords.clear();
	for (const auto &p : points)
		s.texCoords.append({p.x / 64, p.y / 64});
	for (int f = 0; f < s.frames.size(); ++f)
	{
		s.frames[f].positions = points;
		for (auto &p : s.frames[f].positions)
			p.z += f * 8.f;
		s.frames[f].normals = normals;
	}
	s.vertexCount = int(points.size());
	s.uvSeams.clear();
	s.topologyVertices.clear();
	updateEditableModelMetadata(&mesh);
	return mesh;
}
ModelVec3 unit(ModelVec3 p)
{
	const double l = std::sqrt(double(p.x) * p.x + double(p.y) * p.y + double(p.z) * p.z);
	return l > 0 ? ModelVec3{float(p.x / l), float(p.y / l), float(p.z / l)} : ModelVec3{0, 0, 1};
}
// An open cylinder of `segments` quads around Z, radius 10 and height 20.
ModelMesh cylinder(int segments)
{
	QVector<ModelVec3> points, normals;
	QVector<ModelTriangle> triangles;
	for (int ring = 0; ring < 2; ++ring)
		for (int i = 0; i < segments; ++i)
		{
			const double angle = 2 * std::numbers::pi * i / segments;
			points.append({float(10 * std::cos(angle)), float(10 * std::sin(angle)), float(ring * 20)});
			normals.append({float(std::cos(angle)), float(std::sin(angle)), 0});
		}
	for (int i = 0; i < segments; ++i)
	{
		const int b0 = i, b1 = (i + 1) % segments, t0 = segments + i, t1 = segments + (i + 1) % segments;
		triangles.append({b0, b1, t1});
		triangles.append({b0, t1, t0});
	}
	return shape(points, normals, triangles);
}
// A sphere of `rings` latitude bands; `upper` bands only (from the top pole)
// gives an open cap.
ModelMesh sphere(int rings, int segments, int upper)
{
	QVector<ModelVec3> points{{0, 0, 10}}, normals{{0, 0, 1}};
	QVector<ModelTriangle> triangles;
	for (int ring = 1; ring < rings; ++ring)
	{
		const double polar = std::numbers::pi * ring / rings;
		for (int i = 0; i < segments; ++i)
		{
			const double angle = 2 * std::numbers::pi * i / segments;
			const ModelVec3 p{float(10 * std::sin(polar) * std::cos(angle)), float(10 * std::sin(polar) * std::sin(angle)), float(10 * std::cos(polar))};
			points.append(p);
			normals.append(unit(p));
		}
	}
	const int bottom = int(points.size());
	points.append({0, 0, -10});
	normals.append({0, 0, -1});
	const auto at = [&](int ring, int i) { return 1 + (ring - 1) * segments + (i % segments); };
	for (int i = 0; i < segments; ++i)
		triangles.append({0, at(1, i), at(1, i + 1)});
	for (int ring = 1; ring + 1 < rings && ring < upper; ++ring)
		for (int i = 0; i < segments; ++i)
		{
			triangles.append({at(ring, i), at(ring + 1, i), at(ring + 1, i + 1)});
			triangles.append({at(ring, i), at(ring + 1, i + 1), at(ring, i + 1)});
		}
	if (upper >= rings)
		for (int i = 0; i < segments; ++i)
			triangles.append({at(rings - 1, i), bottom, at(rings - 1, i + 1)});
	auto mesh = shape(points, normals, triangles);
	if (upper < rings)
	{
		// Drop the unused lower vertices so the cap is a valid source.
		auto &s = mesh.surfaces[0];
		const int keep = at(upper, segments - 1) + 1;
		s.texCoords.resize(keep);
		for (auto &frame : s.frames)
		{
			frame.positions.resize(keep);
			frame.normals.resize(keep);
		}
		s.vertexCount = keep;
		updateEditableModelMetadata(&mesh);
	}
	return mesh;
}
ModelMesh grid(int cells)
{
	QVector<ModelVec3> points, normals;
	QVector<ModelTriangle> triangles;
	for (int y = 0; y <= cells; ++y)
		for (int x = 0; x <= cells; ++x)
		{
			points.append({float(x * 10), float(y * 10), 0});
			normals.append({0, 0, 1});
		}
	for (int y = 0; y < cells; ++y)
		for (int x = 0; x < cells; ++x)
		{
			const int a = y * (cells + 1) + x, b = a + 1, c = a + cells + 2, d = c - 1;
			triangles.append({a, b, c});
			triangles.append({a, c, d});
		}
	return shape(points, normals, triangles);
}
QSet<int> allFaces(const ModelMesh &mesh)
{
	QSet<int> faces;
	for (int f = 0; f < mesh.surfaces[0].triangles.size(); ++f)
		faces.insert(f);
	return faces;
}
bool positiveWinding(const ModelSurface &s)
{
	for (const auto &t : s.triangles)
	{
		const auto a = s.texCoords[t.a], b = s.texCoords[t.b], c = s.texCoords[t.c];
		if ((double(b.u) - a.u) * (double(c.v) - a.v) - (double(b.v) - a.v) * (double(c.u) - a.u) <= 0)
			return false;
	}
	return true;
}
int islands(const ModelSurface &s)
{
	ModelUvTopology topology;
	return buildModelUvTopology(s, &topology) ? int(topology.islands.size()) : -1;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	ModelUvUnwrapOptions options;
	options.pack = false;
	ModelUvUnwrapReport report;

	// An open cylinder cut by one seam edge unrolls into one isometric strip.
	{
		auto mesh = cylinder(16);
		mesh.surfaces[0].uvSeams = {modelEdge(0, 16)};
		ModelSurface result;
		expect(unwrapModelUvConformal(mesh.surfaces[0], allFaces(mesh), {}, options, &result, &report, &error), "unwrap a seamed cylinder", error);
		expect(islands(result) == 1 && report.islands.size() == 1 && positiveWinding(result), "one island, no flipped faces");
		expect(report.maxAngleDistortion < 1.001 && report.maxAreaRatio < 1.001 && report.minAreaRatio > .999,
			   "a developable cylinder flattens without distortion",
			   QStringLiteral("%1 %2 %3").arg(report.maxAngleDistortion).arg(report.minAreaRatio).arg(report.maxAreaRatio));
		expect(uvSamePoses(mesh.surfaces[0], result) && result.uvSeams.size() >= 1, "every pose and normal survives; seam marks follow");
		ModelSurface again;
		expect(unwrapModelUvConformal(mesh.surfaces[0], allFaces(mesh), {}, options, &again, nullptr, &error) &&
				   raw(again.texCoords) == raw(result.texCoords) && raw(again.triangles) == raw(result.triangles),
			   "unwrapping is deterministic");
		// Through the document, packed, with automatic charts for comparison.
		auto packed = mesh;
		ModelEdit edit;
		edit.kind = ModelEditKind::UnwrapUv;
		edit.selection.faces = allFaces(mesh);
		edit.uvTools.unwrapMethod = ModelUvUnwrapMethod::Conformal;
		expect(applyModelEdit(&packed, edit, nullptr, &error) && islands(packed.surfaces[0]) == 1, "document conformal unwrap and pack", error);
		QVector<uint32_t> indices;
		for (const auto &t : packed.surfaces[0].triangles)
			indices << uint32_t(t.a) << uint32_t(t.b) << uint32_t(t.c);
		expect(validateModelUvMapping(packed.surfaces[0].texCoords, indices, &error, {}), "the packed island does not overlap itself", error);
		auto automatic = mesh;
		edit.uvTools = {};
		expect(applyModelEdit(&automatic, edit, nullptr, &error), "automatic unwrap of the same cylinder", error);
		std::cout << "cylinder charts: conformal 1, automatic " << islands(automatic.surfaces[0]) << '\n';
		auto twice = mesh;
		edit.uvTools.unwrapMethod = ModelUvUnwrapMethod::Conformal;
		expect(applyModelEdit(&twice, edit, nullptr, &error) && source(twice) == source(packed), "two runs give identical sources");
	}

	// A planar grid is an isometry; pins keep their UVs exactly.
	{
		auto mesh = grid(4);
		ModelSurface result;
		expect(unwrapModelUvConformal(mesh.surfaces[0], allFaces(mesh), {}, options, &result, &report, &error) &&
				   report.maxAngleDistortion < 1.0001 && std::abs(report.maxAreaRatio - 1) < 1e-4 && std::abs(report.minAreaRatio - 1) < 1e-4,
			   "a flat grid flattens isometrically", error);
		QHash<int, ModelTexCoord> pins{{0, {.1f, .1f}}, {4, {.9f, .1f}}};
		expect(unwrapModelUvConformal(mesh.surfaces[0], allFaces(mesh), pins, options, &result, &report, &error) && report.islands[0].pinned,
			   "unwrap with two pins", error);
		bool exact = true;
		for (const auto &t : result.triangles)
			for (int corner : {t.a, t.b, t.c})
			{
				const int original = corner < 25 ? corner : -1;
				if (original == 0)
					exact = exact && result.texCoords[corner].u == .1f && result.texCoords[corner].v == .1f;
				if (original == 4)
					exact = exact && result.texCoords[corner].u == .9f && result.texCoords[corner].v == .1f;
			}
		expect(exact && std::abs(result.texCoords[2].u - .5f) < 1e-4 && std::abs(result.texCoords[2].v - .1f) < 1e-4,
			   "pinned corners keep exact UVs and the rest follow conformally");
		// Through the document, the selected vertices are the pins.
		auto pinned = mesh;
		pinned.surfaces[0].texCoords[0] = {.1f, .1f};
		pinned.surfaces[0].texCoords[4] = {.9f, .1f};
		ModelEdit edit;
		edit.kind = ModelEditKind::UnwrapUv;
		edit.selection.faces = allFaces(mesh);
		edit.selection.vertices = {0, 4};
		edit.uvTools.unwrapMethod = ModelUvUnwrapMethod::Conformal;
		edit.uvTools.unwrapPack = false;
		expect(applyModelEdit(&pinned, edit, nullptr, &error) && pinned.surfaces[0].texCoords[0].u == .1f &&
				   pinned.surfaces[0].texCoords[4].u == .9f,
			   "document pins from the selected vertices", error);
		auto automaticPins = mesh;
		edit.uvTools.unwrapMethod = ModelUvUnwrapMethod::Automatic;
		edit.uvTools.unwrapPack = true;
		expect(!applyModelEdit(&automaticPins, edit, nullptr, &error), "automatic unwrap refuses vertex pins");
	}

	// An open hemisphere flattens without folds; a closed sphere is refused.
	{
		const auto cap = sphere(8, 16, 4);
		ModelSurface result;
		expect(unwrapModelUvConformal(cap.surfaces[0], allFaces(cap), {}, options, &result, &report, &error) && positiveWinding(result) &&
				   islands(result) == 1,
			   "a hemisphere with its rim flattens without folds", error);
		std::cout << "hemisphere: max angle distortion " << report.maxAngleDistortion << ", area ratios " << report.minAreaRatio << " to "
				  << report.maxAreaRatio << '\n';
		const auto ball = sphere(8, 16, 99);
		const auto before = source(ball);
		ModelSurface untouched;
		expect(!unwrapModelUvConformal(ball.surfaces[0], allFaces(ball), {}, options, &untouched, nullptr, &error) &&
				   error.contains(QStringLiteral("seam")) && untouched.triangles.isEmpty(),
			   "a closed sphere without seams is refused with a seam hint", error);
		auto document = ball;
		ModelEdit edit;
		edit.kind = ModelEditKind::UnwrapUv;
		edit.selection.faces = allFaces(ball);
		edit.uvTools.unwrapMethod = ModelUvUnwrapMethod::Conformal;
		expect(!applyModelEdit(&document, edit, nullptr, &error) && source(document) == before, "a refused unwrap changes nothing");
		ModelWorkControl stop;
		stop.cancelled = [] { return true; };
		auto cancelled = cap;
		const auto kept = source(cancelled);
		edit.selection.faces = allFaces(cap);
		expect(!applyModelEdit(&cancelled, edit, nullptr, &error, stop) && source(cancelled) == kept, "cancellation is atomic");
	}

	// CLI: a dry run reports the islands and their distortion.
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	QTemporaryDir temp(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("uv-unwrap-XXXXXX"));
	if (argc > 1 && temp.isValid())
	{
		auto mesh = cylinder(16);
		mesh.surfaces[0].uvSeams = {modelEdge(0, 16)};
		const auto input = temp.filePath("cylinder.mesh.json");
		QFile file(input);
		expect(file.open(QIODevice::WriteOnly) && file.write(serializeEditableModel(mesh, &error)) > 0, "write CLI source");
		file.close();
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(QString::fromLocal8Bit(argv[1]),
					  {"--cli", "model", "edit", input, "--operation", "uv-unwrap", "--faces", "all", "--uv-method", "conformal", "--output",
					   temp.filePath("out.mesh.json"), "--dry-run", "--json", "--settings-file", temp.filePath("s.ini")});
		const bool finished = process.waitForFinished(60000);
		const auto json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
		expect(finished && process.exitCode() == 0 && json["unwrap"].toObject()["islands"].toArray().size() == 1 &&
				   json["unwrap"].toObject()["maxAngleDistortion"].toDouble() < 1.001 && !QFile::exists(temp.filePath("out.mesh.json")),
			   "CLI conformal dry run reports one island", QString::fromUtf8(process.readAllStandardError()));
	}
	std::cout << checks << " conformal unwrap checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
