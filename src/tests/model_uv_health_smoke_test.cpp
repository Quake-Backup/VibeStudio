#include "cli/model_tools.h"
#include "core/model_document.h"
#include "core/model_mdl.h"
#include "core/model_uv_health.h"
#include "core/model_uv_raster.h"
#include "tests/model_mdl_test_helpers.h"
#include "tests/model_uv_health_test_helpers.h"
#include "core/idtech_image.h"

#include <algorithm>
#include <array>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>

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
bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }
QVector<int> list(std::initializer_list<int> values) { return QVector<int>(values); }
QByteArray json(const ModelUvHealthReport &report) { return QJsonDocument(modelUvHealthJson(report)).toJson(QJsonDocument::Compact); }
bool writeFile(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	ModelUvHealthReport report;
	ModelUvHealthOptions options;

	// Texel-centre rasteriser: shared edges visit each centre once.
	{
		QVector<int> hits(16);
		for (const auto &triangle : {std::array<QPointF, 3>{QPointF(0, 0), QPointF(4, 0), QPointF(4, 4)},
									 std::array<QPointF, 3>{QPointF(0, 0), QPointF(4, 4), QPointF(0, 4)}})
			rasterizeModelUvTriangle(triangle[0], triangle[1], triangle[2], 4, 4,
									 [&](int x, int y, double a, double b, double c)
									 {
										 expect(near(a + b + c, 1), "barycentric weights sum to one");
										 ++hits[y * 4 + x];
										 return true;
									 });
		expect(std::all_of(hits.cbegin(), hits.cend(), [](int hit) { return hit == 1; }), "a split square covers every texel centre once");
		int reversed = 0;
		rasterizeModelUvTriangle(QPointF(0, 0), QPointF(0, 4), QPointF(4, 4), 4, 4, [&](int, int, double, double, double) { return ++reversed; });
		expect(reversed > 0, "clockwise triangles rasterise too");
	}

	// A quad stretched 2:1 in U has known singular values.
	{
		auto mesh = strip(1);
		auto &s = mesh.surfaces[0];
		for (auto &uv : s.texCoords)
			uv.v *= .5f;
		options.textureSize = {20, 20};
		options.stretchLimit = 1.4;
		expect(inspectModelUvSurfaceHealth(s, options, &report, &error), "measure a stretched quad", error);
		expect(report.islands.size() == 1 && near(report.islands[0].density, std::sqrt(2.)) && near(report.faceStretch[0], std::sqrt(2.)) &&
				   near(report.faceStretch[1], std::sqrt(2.)) && near(report.islands[0].maxAngleDistortion, 2) &&
				   near(report.islands[0].maxAreaRatio, 1) && near(report.maxStretch, std::sqrt(2.)),
			   "stretch is max(σ1/ρ, ρ/σ2) with σ = 2 and 1 texels per unit");
		expect(report.stretched == list({0, 1}), "faces over the stretch limit are listed");
		options.stretchLimit = 1.5;
		expect(inspectModelUvSurfaceHealth(s, options, &report, &error) && report.stretched.isEmpty(), "a higher limit flags nothing");
		options.textureSize = {10, 20};
		expect(inspectModelUvSurfaceHealth(s, options, &report, &error) && near(report.maxStretch, 1),
			   "a matching rectangular skin is an isometry");
		expect(report.flipped.isEmpty() && report.overlapping.isEmpty() && report.outside.isEmpty() && report.collapsedUv.isEmpty(),
			   "a clean quad has no other problems");
	}

	// A folded strip: the last quad folds back over its neighbour.
	options = {};
	options.textureSize = {30, 10};
	{
		auto mesh = strip(3);
		auto &s = mesh.surfaces[0];
		s.texCoords[3].u = s.texCoords[7].u = .5f;
		expect(inspectModelUvSurfaceHealth(s, options, &report, &error), "measure a folded strip", error);
		expect(report.flipped == list({4, 5}) && report.islands.size() == 1 && report.islands[0].flipped == 2 && !report.islands[0].mirrored,
			   "faces winding against the island majority are flipped", QString::fromUtf8(json(report)));
		expect(report.overlapping == list({2, 3, 4, 5}) && report.overlapPairCount == 4 && report.mirrorStacked.isEmpty(),
			   "the fold overlaps its neighbour's faces", QString::fromUtf8(json(report)));
		expect(report.stretched == list({4, 5}) && near(report.faceStretch[4], 2 * std::sqrt(5. / 6)),
			   "the folded quad is squashed in U against the island's density");
		ModelUvHealthReport again;
		expect(inspectModelUvSurfaceHealth(s, options, &again, &error) && json(again) == json(report), "health is deterministic");
		ModelWorkControl stop;
		stop.cancelled = [] { return true; };
		const auto kept = json(report);
		expect(!inspectModelUvSurfaceHealth(s, options, &report, &error, stop) && json(report) == kept, "cancellation keeps the report");
	}

	// Mirrored islands are not flipped; stacked mirror halves are told apart.
	{
		const auto apart = twoIslands(true, false);
		expect(inspectModelUvSurfaceHealth(apart.surfaces[0], options, &report, &error), "measure mirrored islands", error);
		expect(report.mirroredIslands == 1 && report.flipped.isEmpty() && report.overlapping.isEmpty() && report.mirrorStacked.isEmpty(),
			   "a mirrored island is reported as mirrored, not flipped", QString::fromUtf8(json(report)));
		const auto stacked = twoIslands(true, true);
		expect(inspectModelUvSurfaceHealth(stacked.surfaces[0], options, &report, &error) && report.overlapping.isEmpty() &&
				   report.mirrorStacked == list({0, 1, 2, 3}) && report.mirrorStackedPairCount > 0,
			   "mirror halves sharing UV space are stacked, not overlapping", QString::fromUtf8(json(report)));
		const auto duplicate = twoIslands(false, true);
		expect(inspectModelUvSurfaceHealth(duplicate.surfaces[0], options, &report, &error) && report.overlapping == list({0, 1, 2, 3}) &&
				   report.mirrorStacked.isEmpty() && report.mirroredIslands == 0,
			   "same-winding copies sharing UV space overlap");
		expect(modelUvHealthHas(report, "overlap") && !modelUvHealthHas(report, "flipped"), "problem queries follow the report");
	}

	// Out-of-tile corners and collapsed UVs.
	{
		auto mesh = strip(2);
		auto &s = mesh.surfaces[0];
		s.texCoords[2].u = 1.25f;
		expect(inspectModelUvSurfaceHealth(s, options, &report, &error), "measure out-of-tile UVs", error);
		expect(report.outsideVertices == list({2}) && report.outside == list({2}), "corners outside 0–1 name their vertices and faces",
			   QString::fromUtf8(json(report)));
		auto collapsed = strip(2);
		auto &c = collapsed.surfaces[0];
		c.texCoords[5] = {1, 0};
		c.texCoords[2] = {1, 0};
		expect(inspectModelUvSurfaceHealth(c, options, &report, &error) && report.collapsedUv == list({2}),
			   "a face with coincident UVs is collapsed", QString::fromUtf8(json(report)));
		expect(!(report.faceFlags[2] & (ModelUvHealthFlipped | ModelUvHealthStretched | ModelUvHealthOverlapping)),
			   "collapsed faces are not measured for stretch, flips or overlaps");
	}

	// Texel-centre error and invalid input.
	{
		auto mesh = strip(1);
		auto &s = mesh.surfaces[0];
		options.textureSize = {16, 16};
		for (auto &uv : s.texCoords)
			uv = {std::clamp(uv.u, 1 / 32.f, 31 / 32.f), std::clamp(uv.v, 1 / 32.f, 31 / 32.f)};
		expect(inspectModelUvSurfaceHealth(s, options, &report, &error) && report.maxTexelError == 0, "texel centres have no error");
		s.texCoords[0] = {.25f, 1 / 32.f};
		expect(inspectModelUvSurfaceHealth(s, options, &report, &error) && near(report.maxTexelError, .5),
			   "a corner on a texel edge is half a texel from either centre");
		const auto kept = json(report);
		for (int kind = 0; kind < 3; ++kind)
		{
			auto bad = options;
			if (kind == 0)
				bad.textureSize = {0, 4};
			if (kind == 1)
				bad.referenceFrame = 9;
			if (kind == 2)
				bad.stretchLimit = .5;
			expect(!inspectModelUvSurfaceHealth(s, bad, &report, &error) && json(report) == kept && !error.isEmpty(),
				   "invalid options are refused without changing the report");
		}
	}

	// Native estimates match real exports.
	{
		auto md2 = strip(2);
		md2.md2SkinSize = {64, 32};
		expect(inspectModelUvHealth(md2, 0, options, &report, &error) && report.native.size() == 3, "estimate native vertices", error);
		ModelExportReport exported;
		const auto bytes = exportEditableModel(md2, "md2", 0, &error, {}, &exported);
		expect(!bytes.isEmpty() && report.native[1].available && report.native[1].stored == exported.storedVertices &&
				   report.native[2].stored == md2.surfaces[0].vertexCount && !report.native[0].available,
			   "MD2 positions match the writer; MD3 stores every UV corner; MDL needs MDL settings", error);
		auto palette = generatedIdTechPalette("quake");
		const auto fixture = groupedMdlFixture();
		ModelMesh editable;
		expect(importEditableModel("fixture.mdl", fixture.bytes, &editable, &error, &palette), "import an MDL fixture", error);
		expect(inspectModelUvHealth(editable, 0, {}, &report, &error) && report.native[0].available, "estimate MDL vertices", error);
		const auto mdlBytes = exportEditableModel(editable, "mdl", 0, &error, {}, &exported);
		expect(!mdlBytes.isEmpty() && report.native[0].stored == exported.storedVertices, "MDL stored vertices match the writer", error);
		expect(report.floodFillChecked && report.floodFillIndex == 0 && !report.floodFillHazard(),
			   "a first texel of index 0 is never flood-filled");
		// A left-half region of index 5 under UVs that cover the whole skin.
		const int width = editable.mdl.skinSize.width(), height = editable.mdl.skinSize.height();
		auto &pixels = editable.embeddedSkins[0].indexedFrames[0];
		for (int y = 0; y < height; ++y)
			for (int x = 0; x < width; ++x)
				pixels[y * width + x] = char(x < width / 2 ? 5 : 9);
		expect(inspectModelUvHealth(editable, 0, {}, &report, &error) && report.floodFillIndex == 5 &&
				   report.floodFillTexels == width * height / 2 && report.floodFillUsedTexels > 0 && report.floodFillHazard(),
			   "a used texel in the first texel's colour region is a flood-fill hazard", QString::fromUtf8(json(report)));
		auto &surface = editable.surfaces[0];
		for (auto &uv : surface.texCoords)
			uv.u = .75f + uv.u / 4;
		expect(inspectModelUvHealth(editable, 0, {}, &report, &error) && report.floodFillTexels == width * height / 2 &&
				   report.floodFillUsedTexels == 0 && !report.floodFillHazard(),
			   "UVs kept out of the region are safe", QString::fromUtf8(json(report)));
	}

	// CLI: the shared service, --fail-on exit codes and application dispatch.
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	QTemporaryDir temp(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("uv-health-XXXXXX"));
	expect(temp.isValid(), "temporary directory");
	auto folded = strip(3);
	folded.surfaces[0].texCoords[3].u = folded.surfaces[0].texCoords[7].u = .5f;
	const auto input = temp.filePath("folded.mesh.json");
	expect(writeFile(input, serializeEditableModel(folded, &error)), "write CLI source", error);
	const QStringList base{"vibestudio", "--cli", "model", "uv", input, "--health", "--texture-size", "30x10"};
	auto inspected = cli::runModelUvHealth(base);
	expect(inspected.exitCode == 0 && inspected.payload["overlappingFaces"].toArray().size() == 4 &&
			   inspected.payload["flippedFaces"].toArray().size() == 2 && inspected.payload.contains("native"),
		   "CLI reports the shared health", inspected.error);
	auto failing = cli::runModelUvHealth(base + QStringList{"--fail-on", "overlap,flipped"});
	expect(failing.exitCode == 4 && failing.payload["failed"].toArray().size() == 2, "--fail-on exits 4 with the report", failing.error);
	auto passing = cli::runModelUvHealth(base + QStringList{"--fail-on", "outside,collapsed,flood-fill"});
	expect(passing.exitCode == 0, "--fail-on passes when none of its problems are present", passing.error);
	auto stretchFail = cli::runModelUvHealth(base + QStringList{"--fail-on", "stretch>1.5"});
	auto stretchPass = cli::runModelUvHealth(base + QStringList{"--fail-on", "stretch>3"});
	expect(stretchFail.exitCode == 4 && stretchPass.exitCode == 0 && stretchPass.payload["stretchLimit"].toDouble() == 3,
		   "stretch>N sets the limit it fails on");
	for (const auto &bad : QList<QStringList>{{"--fail-on", "overlap,overlap"},
											  {"--fail-on", "stretch>0.5"},
											  {"--fail-on", "nonsense"},
											  {"--texture-size", "0x4"},
											  {"--surface", "3"},
											  {"--stretch-limit", "2", "--fail-on", "stretch>3"}})
	{
		auto args = base;
		if (bad.first() == "--texture-size")
			args = QStringList{"vibestudio", "--cli", "model", "uv", input, "--health"};
		expect(cli::runModelUvHealth(args + bad).exitCode == 2, "CLI rejects malformed options");
	}
	if (argc > 1)
	{
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(QString::fromLocal8Bit(argv[1]),
					  base.mid(1) + QStringList{"--fail-on", "overlap", "--json", "--settings-file", temp.filePath("settings.ini")});
		const bool finished = process.waitForFinished(60000);
		const auto output = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
		expect(finished && process.exitCode() == 4 && output["overlapPairCount"].toInt() == 4 && output["failed"].toArray().size() == 1,
			   "application dispatch prints the report and exits 4", QString::fromUtf8(process.readAllStandardError()));
		process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "model", "uv", input, "--json", "--settings-file", temp.filePath("settings.ini")});
		expect(process.waitForFinished(60000) && process.exitCode() == 0 &&
				   QJsonDocument::fromJson(process.readAllStandardOutput()).object().contains("islands"),
			   "model uv without --health keeps the topology report");
	}
	std::cout << checks << " UV health checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
