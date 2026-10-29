#include "cli/model_tools.h"
#include "core/idtech_image.h"
#include "core/model_document.h"
#include "core/model_mdl.h"
#include "core/model_uv_mapping.h"
#include "core/model_uv_density.h"
#include "core/model_uv_ops.h"
#include "tests/model_uv_islands_test_helpers.h"
#include "tests/model_mdl_test_helpers.h"
#include "tests/model_uv_health_test_helpers.h"

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
ModelEdit operation(ModelEditKind kind, ModelSelection selection)
{
	ModelEdit edit;
	edit.kind = kind;
	edit.selection = selection;
	return edit;
}
double uvArea(const ModelSurface &surface, int face)
{
	const auto t = surface.triangles[face];
	const auto a = surface.texCoords[t.a], b = surface.texCoords[t.b], c = surface.texCoords[t.c];
	return ((double(b.u) - a.u) * (double(c.v) - a.v) - (double(b.v) - a.v) * (double(c.u) - a.u)) / 2;
}
double distance(ModelTexCoord a, ModelTexCoord b) { return std::hypot(double(a.u) - b.u, double(a.v) - b.v); }
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QString error;

	// Weld is the exact inverse of Detach.
	{
		const auto original = strip(2);
		auto mesh = original;
		ModelSelection selection;
		expect(applyModelEdit(&mesh, operation(ModelEditKind::DetachUv, {0, {}, {2, 3}}), &selection, &error) &&
				   mesh.surfaces[0].vertexCount == 8,
			   "detach splits the shared column", error);
		auto weld = operation(ModelEditKind::WeldUv, {0, {}, {2, 3}});
		expect(applyModelEdit(&mesh, weld, &selection, &error) && source(mesh) == source(original),
			   "weld restores the source byte for byte", error);
		// A mark on a detached edge comes back on its original edge only.
		auto seamed = original;
		seamed.surfaces[0].uvSeams = {modelEdge(1, 4)};
		auto split = seamed;
		expect(applyModelEdit(&split, operation(ModelEditKind::DetachUv, {0, {}, {2, 3}}), nullptr, &error) &&
				   applyModelEdit(&split, operation(ModelEditKind::WeldUv, {0, {}, {2, 3}}), nullptr, &error) && source(split) == source(seamed),
			   "weld maps copied seam marks back", error);
		// Corners whose normal or pose differ never weld.
		for (int change = 0; change < 2; ++change)
		{
			auto different = original;
			expect(applyModelEdit(&different, operation(ModelEditKind::DetachUv, {0, {}, {2, 3}}), nullptr, &error), "detach for refusal");
			auto &s = different.surfaces[0];
			for (const int copy : {s.triangles[2].a, s.triangles[3].c})
			{
				if (change == 0)
					s.frames[1].normals[copy] = {0, 1, 0};
				else
					s.frames[1].positions[copy].z += 1;
			}
			const auto before = source(different);
			expect(!applyModelEdit(&different, weld, nullptr, &error) && source(different) == before && !error.isEmpty(),
				   "weld refuses corners whose normals or poses differ");
		}
		// Within a tolerance, the joined corner takes the mean UV.
		auto moved = original;
		expect(applyModelEdit(&moved, operation(ModelEditKind::DetachUv, {0, {}, {2, 3}}), nullptr, &error), "detach for tolerance");
		for (const int copy : {moved.surfaces[0].triangles[2].a, moved.surfaces[0].triangles[3].c})
			moved.surfaces[0].texCoords[copy].u += .01f;
		auto strict = moved;
		expect(!applyModelEdit(&strict, weld, nullptr, &error), "exact weld refuses moved corners");
		weld.uvTools.weldTolerance = .02;
		expect(applyModelEdit(&moved, weld, nullptr, &error) && moved.surfaces[0].vertexCount == 6 &&
				   std::abs(moved.surfaces[0].texCoords[1].u - (.5f + .005f)) < 1e-6f,
			   "a tolerance weld joins at the mean UV", error);
		ModelWorkControl stop;
		stop.cancelled = [] { return true; };
		auto cancelled = original;
		applyModelEdit(&cancelled, operation(ModelEditKind::DetachUv, {0, {}, {2, 3}}), nullptr, &error);
		const auto kept = source(cancelled);
		expect(!applyModelEdit(&cancelled, operation(ModelEditKind::WeldUv, {0, {}, {2, 3}}), nullptr, &error, stop) &&
				   source(cancelled) == kept,
			   "cancelled weld changes nothing");
	}

	// Stitch moves the smaller island rigidly so the seam meets, then welds.
	{
		const auto original = strip(2);
		auto mesh = original;
		expect(applyModelEdit(&mesh, operation(ModelEditKind::DetachUv, {0, {}, {2, 3}}), nullptr, &error), "detach for stitch");
		auto turn = operation(ModelEditKind::TransformUv, {0, {}, {2, 3}});
		turn.uvRotation = 90;
		turn.uvOffset = {.2f, .3f};
		turn.uvPivotMode = ModelUvPivot::SelectionCentre;
		expect(applyModelEdit(&mesh, turn, nullptr, &error), "turn the detached island away", error);
		auto &s = mesh.surfaces[0];
		const int a = s.triangles[3].a, b = s.triangles[3].c; // copies of vertices 1 and 4
		QVector<double> before;
		const QVector<int> island{s.triangles[2].a, s.triangles[2].b, s.triangles[2].c, s.triangles[3].c};
		for (int i = 0; i < island.size(); ++i)
			for (int j = i + 1; j < island.size(); ++j)
				before.append(distance(s.texCoords[island[i]], s.texCoords[island[j]]));
		ModelSelection selection;
		auto stitch = operation(ModelEditKind::StitchUv, {0, {}, {}, {modelEdge(a, b)}});
		expect(applyModelEdit(&mesh, stitch, &selection, &error), "stitch the seam", error);
		const auto &t = mesh.surfaces[0];
		expect(t.vertexCount == original.surfaces[0].vertexCount && t.triangles[2].a == 1 && t.triangles[3].c == 4,
			   "the stitched corners weld back to the fixed island");
		QVector<double> after;
		const QVector<int> moved{t.triangles[2].a, t.triangles[2].b, t.triangles[2].c, t.triangles[3].c};
		for (int i = 0; i < moved.size(); ++i)
			for (int j = i + 1; j < moved.size(); ++j)
				after.append(distance(t.texCoords[moved[i]], t.texCoords[moved[j]]));
		bool rigid = before.size() == after.size();
		for (int i = 0; rigid && i < before.size(); ++i)
			rigid = std::abs(before[i] - after[i]) < 2e-5;
		expect(rigid && distance(t.texCoords[2], original.surfaces[0].texCoords[2]) < 2e-5 && uvArea(t, 2) > 0 && uvArea(t, 3) > 0,
			   "the moved island keeps its shape and winding");
		expect(selection.edges == QSet<ModelEdge>{modelEdge(1, 4)}, "the stitched edge stays selected");
		auto flipped = original;
		applyModelEdit(&flipped, operation(ModelEditKind::DetachUv, {0, {}, {2, 3}}), nullptr, &error);
		auto mirror = operation(ModelEditKind::TransformUv, {0, {}, {2, 3}});
		mirror.uvScale = {-1, 1};
		mirror.uvPivotMode = ModelUvPivot::SelectionCentre;
		applyModelEdit(&flipped, mirror, nullptr, &error);
		const int fa = flipped.surfaces[0].triangles[3].a, fb = flipped.surfaces[0].triangles[3].c;
		const auto kept = source(flipped);
		expect(!applyModelEdit(&flipped, operation(ModelEditKind::StitchUv, {0, {}, {}, {modelEdge(fa, fb)}}), nullptr, &error) &&
				   source(flipped) == kept,
			   "a mirrored island is refused, not reflected");
		auto joined = original;
		expect(!applyModelEdit(&joined, operation(ModelEditKind::StitchUv, {0, {}, {}, {modelEdge(1, 4)}}), nullptr, &error),
			   "an edge with no split copy is refused");
	}

	// Align and straighten.
	{
		auto mesh = strip(2);
		auto &s = mesh.surfaces[0];
		s.texCoords[1].u = .45f;
		auto align = operation(ModelEditKind::AlignUv, {0, {0, 1, 2}});
		align.uvTools.alignMode = ModelUvAlignMode::Minimum;
		align.uvTools.alignAxis = 1;
		s.texCoords[2].v = .1f;
		expect(applyModelEdit(&mesh, align, nullptr, &error) && mesh.surfaces[0].texCoords[2].v == 0 && mesh.surfaces[0].texCoords[0].v == 0,
			   "align V to the minimum", error);
		align.uvTools.alignAxis = 0;
		align.uvTools.alignMode = ModelUvAlignMode::Centre;
		align.selection.vertices = {1, 4};
		expect(applyModelEdit(&mesh, align, nullptr, &error) && mesh.surfaces[0].texCoords[1].u == mesh.surfaces[0].texCoords[4].u,
			   "align U to the centre gives equal coordinates", error);
		auto bent = strip(2);
		bent.surfaces[0].texCoords[1] = {.5f, .2f};
		auto straighten = operation(ModelEditKind::AlignUv, {0, {0, 1, 2}});
		straighten.uvTools.alignMode = ModelUvAlignMode::Straighten;
		expect(applyModelEdit(&bent, straighten, nullptr, &error), "straighten", error);
		const auto p = bent.surfaces[0].texCoords[0], q = bent.surfaces[0].texCoords[1], r = bent.surfaces[0].texCoords[2];
		expect(std::abs((double(q.u) - p.u) * (double(r.v) - p.v) - (double(q.v) - p.v) * (double(r.u) - p.u)) < 1e-6 && p.u == 0 && r.u == 1,
			   "straightened corners lie on the line between the end corners");
		expect(!applyModelEdit(&bent, operation(ModelEditKind::AlignUv, {0, {0}}), nullptr, &error), "align needs two corners");
	}

	// Snap to texel centres makes native exports exact.
	{
		auto mesh = strip(2);
		mesh.md2SkinSize = {64, 32};
		for (auto &uv : mesh.surfaces[0].texCoords)
			uv = {uv.u * .9f + .037f, uv.v * .8f + .061f};
		ModelExportReport report;
		expect(!exportEditableModel(mesh, "md2", 0, &error, {}, &report).isEmpty() && report.maxUvError > 0, "unsnapped MD2 has UV error", error);
		auto snap = operation(ModelEditKind::SnapUvTexels, {0, {}, {0, 1, 2, 3}});
		expect(applyModelEdit(&mesh, snap, nullptr, &error), "snap to the MD2 skin", error);
		expect(!exportEditableModel(mesh, "md2", 0, &error, {}, &report).isEmpty() && report.maxUvError == 0, "snapped MD2 exports exactly", error);
		const auto snapped = source(mesh);
		expect(applyModelEdit(&mesh, snap, nullptr, &error) && source(mesh) == snapped, "snapping is idempotent", error);
		snap.uvTools.snapMode = ModelUvSnapMode::Corners;
		snap.uvTools.snapSize = {16, 16};
		expect(applyModelEdit(&mesh, snap, nullptr, &error) && std::abs(mesh.surfaces[0].texCoords[0].u * 16 - std::round(mesh.surfaces[0].texCoords[0].u * 16)) < 1e-5,
			   "corner snapping uses an explicit grid", error);
		auto palette = generatedIdTechPalette("quake");
		ModelMesh mdl;
		expect(importEditableModel("fixture.mdl", groupedMdlFixture().bytes, &mdl, &error, &palette), "import an MDL", error);
		for (auto &uv : mdl.surfaces[0].texCoords)
			uv.u = std::clamp(uv.u + .02f, 0.f, 1.f);
		expect(!exportEditableModel(mdl, "mdl", 0, &error, {}, &report).isEmpty() && report.maxUvError > 0, "unsnapped MDL has UV error", error);
		auto mdlSnap = operation(ModelEditKind::SnapUvTexels, {0, {}, {0, 1}});
		expect(applyModelEdit(&mdl, mdlSnap, nullptr, &error) && !exportEditableModel(mdl, "mdl", 0, &error, {}, &report).isEmpty() &&
				   report.maxUvError == 0,
			   "snapped MDL exports exactly on its skin size", error);
		auto bad = snap;
		bad.uvTools.snapSize = {0, 4};
		expect(!applyModelEdit(&mesh, bad, nullptr, &error), "an empty explicit grid is refused");
	}

	// Quarter-turn packing fits tall islands into a wide atlas, never mirrored.
	{
		ModelMesh mesh = strip(1);
		auto &s = mesh.surfaces[0];
		const auto base = s;
		for (auto &frame : s.frames)
			for (auto &p : frame.positions)
				p.y *= 4;
		// 16 x 64 texels on a 256 x 64 skin: tall islands in a wide atlas.
		s.texCoords = {{0, 0}, {.0625f, 0}, {0, 1}, {.0625f, 1}};
		const auto one = s;
		for (int v = 0; v < one.vertexCount; ++v)
		{
			s.texCoords.append({one.texCoords[v].u + .5f, one.texCoords[v].v});
			for (int f = 0; f < s.frames.size(); ++f)
			{
				auto p = one.frames[f].positions[v];
				p.x += 30;
				s.frames[f].positions.append(p);
				s.frames[f].normals.append(one.frames[f].normals[v]);
			}
		}
		for (const auto &t : one.triangles)
			s.triangles.append({t.a + 4, t.b + 4, t.c + 4});
		s.vertexCount = 8;
		updateEditableModelMetadata(&mesh);
		auto pack = operation(ModelEditKind::PackUv, {0, {}, {0, 1, 2, 3}});
		pack.uvAtlasResolution = 256;
		pack.uvAtlasHeight = 64;
		pack.uvAtlasPadding = 2;
		auto upright = mesh, turned = mesh;
		expect(applyModelEdit(&upright, pack, nullptr, &error), "pack without turning", error);
		pack.uvTools.packRotate = true;
		expect(applyModelEdit(&turned, pack, nullptr, &error), "pack with quarter turns", error);
		double plain = 0, rotated = 0;
		bool positive = true;
		for (int face = 0; face < 4; ++face)
		{
			plain += uvArea(upright.surfaces[0], face);
			rotated += uvArea(turned.surfaces[0], face);
			positive = positive && uvArea(turned.surfaces[0], face) > 0;
		}
		expect(rotated > plain * 2 && positive, "turned islands pack denser and keep their winding",
			   QStringLiteral("%1 vs %2").arg(rotated).arg(plain));
		QVector<uint32_t> indices;
		for (const auto &t : turned.surfaces[0].triangles)
			indices << uint32_t(t.a) << uint32_t(t.b) << uint32_t(t.c);
		expect(validateModelUvMapping(turned.surfaces[0].texCoords, indices, &error, {}), "turned islands do not overlap", error);
		auto again = mesh;
		expect(applyModelEdit(&again, pack, nullptr, &error) && source(again) == source(turned), "rotation packing is deterministic");
		auto misplaced = operation(ModelEditKind::PackUvAround, {0, {}, {0, 1}});
		misplaced.uvTools.packRotate = true;
		expect(!applyModelEdit(&again, misplaced, nullptr, &error), "rotation is refused where it does not apply");
		Q_UNUSED(base);
	}

	// Shared-material joint packing and multi-surface density.
	{
		auto mesh = uvIslandsFixture();
		auto third = mesh.surfaces[1];
		third.name = QStringLiteral("elsewhere");
		third.skinPaths = {QStringLiteral("textures/uv/other")};
		mesh.surfaces.append(third);
		updateEditableModelMetadata(&mesh);
		QSet<int> all;
		for (int f = 0; f < mesh.surfaces[0].triangles.size(); ++f)
			all.insert(f);
		auto pack = operation(ModelEditKind::PackUv, {0, {}, all});
		pack.uvTools.packSurfaces = {1};
		auto packed = mesh;
		expect(applyModelEdit(&packed, pack, nullptr, &error), "pack two surfaces sharing a material", error);
		QVector<ModelTexCoord> uv;
		QVector<uint32_t> indices;
		for (int s : {0, 1})
		{
			const auto &surface = packed.surfaces[s];
			const auto offset = uint32_t(uv.size());
			uv += surface.texCoords;
			for (const auto &t : surface.triangles)
				indices << offset + uint32_t(t.a) << offset + uint32_t(t.b) << offset + uint32_t(t.c);
		}
		expect(validateModelUvMapping(uv, indices, &error, {}), "no island overlaps another across the shared skin", error);
		expect(source(ModelMesh{}) == source(ModelMesh{}) &&
				   QJsonDocument(editableModelJson(packed)).object()["surfaces"].toArray()[2] ==
					   QJsonDocument(editableModelJson(mesh)).object()["surfaces"].toArray()[2],
			   "a surface with another material is untouched");
		pack.uvTools.packSurfaces = {2};
		expect(!applyModelEdit(&packed, pack, nullptr, &error), "surfaces with another material are refused");
		// Density across surfaces with different skins.
		auto density = strip(1);
		density.surfaces.append(density.surfaces[0]);
		density.surfaces[1].name = QStringLiteral("second");
		for (auto &frame : density.surfaces[1].frames)
			for (auto &p : frame.positions)
				p.x += 20;
		updateEditableModelMetadata(&density);
		ModelEdit match;
		match.kind = ModelEditKind::SetUvDensity;
		match.selection = {0, {}, {0, 1}};
		match.uvDensity.textureSize = {256, 256};
		match.uvDensity.target = 8;
		match.uvDensity.surfaces = {1};
		match.uvDensity.surfaceSizes = {{1, QSize(512, 512)}};
		auto matched = density;
		ModelUvDensityReport first, second;
		expect(applyModelEdit(&matched, match, nullptr, &error) &&
				   inspectModelUvDensity(matched.surfaces[0], {}, {256, 256}, 0, &first, &error) &&
				   inspectModelUvDensity(matched.surfaces[1], {}, {512, 512}, 0, &second, &error) &&
				   std::abs(first.density - 8) < 8e-4 && std::abs(second.density - 8) < 8e-4,
			   "two surfaces on 256 and 512 skins both reach the target", error);
		match.uvDensity.individual = false;
		auto grouped = density;
		ModelUvDensityReport a0, b0, a1, b1;
		inspectModelUvDensity(density.surfaces[0], {}, {256, 256}, 0, &a0, &error);
		inspectModelUvDensity(density.surfaces[1], {}, {512, 512}, 0, &b0, &error);
		expect(applyModelEdit(&grouped, match, nullptr, &error) && inspectModelUvDensity(grouped.surfaces[0], {}, {256, 256}, 0, &a1, &error) &&
				   inspectModelUvDensity(grouped.surfaces[1], {}, {512, 512}, 0, &b1, &error) &&
				   std::abs(a1.density / b1.density - a0.density / b0.density) < 1e-4,
			   "group mode keeps the surfaces' relative density", error);
		ModelDocument document;
		expect(document.setMesh(density, &error) && document.edit(match, &error) && document.undoSteps() == 1 && document.undo() &&
				   source(document.mesh()) == source(density),
			   "a multi-surface density match is one undo step", error);
		auto collapsed = density;
		collapsed.surfaces[1].texCoords[2] = collapsed.surfaces[1].texCoords[0];
		collapsed.surfaces[1].texCoords[3] = collapsed.surfaces[1].texCoords[1];
		const auto kept = source(collapsed);
		expect(!applyModelEdit(&collapsed, match, nullptr, &error) && source(collapsed) == kept && error.contains(QStringLiteral("1")),
			   "a collapsed island on another surface refuses the whole edit", error);
	}

	// CLI: the shared document edits.
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	QTemporaryDir temp(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("uv-ops-XXXXXX"));
	auto detached = strip(2);
	applyModelEdit(&detached, operation(ModelEditKind::DetachUv, {0, {}, {2, 3}}), nullptr, &error);
	const auto input = temp.filePath("detached.mesh.json"), output = temp.filePath("welded.mesh.json");
	QFile file(input);
	expect(file.open(QIODevice::WriteOnly) && file.write(serializeEditableModel(detached, &error)) > 0, "write CLI source");
	file.close();
	if (argc > 1)
	{
		const auto run = [&](const QStringList &arguments, QJsonObject *json)
		{
			QProcess process;
			process.setWorkingDirectory(temp.path());
			process.start(QString::fromLocal8Bit(argv[1]),
						  QStringList{"--cli", "model", "edit", input} + arguments + QStringList{"--json", "--settings-file", temp.filePath("s.ini")});
			if (!process.waitForFinished(60000))
				return -1;
			*json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
			return process.exitCode();
		};
		QJsonObject json;
		expect(run({"--operation", "uv-weld", "--faces", "2,3", "--output", output, "--dry-run"}, &json) == 0 && !QFile::exists(output),
			   "CLI weld dry run");
		expect(run({"--operation", "uv-weld", "--faces", "2,3", "--output", output}, &json) == 0, "CLI weld");
		ModelDocument loaded;
		expect(loaded.load(output, &error) && source(loaded.mesh()) == source(strip(2)), "CLI weld matches the document edit", error);
		expect(run({"--operation", "uv-align", "--vertices", "0,1", "--uv-align", "v-max", "--output", output, "--overwrite", "--dry-run"}, &json) == 0,
			   "CLI align dry run");
		expect(run({"--operation", "uv-snap-texels", "--faces", "all", "--uv-snap-size", "64x32", "--output", output, "--overwrite", "--dry-run"}, &json) == 0,
			   "CLI snap dry run");
		expect(run({"--operation", "uv-weld", "--faces", "2,3", "--uv-align", "u-min", "--output", output, "--overwrite"}, &json) == 2,
			   "CLI refuses options of another operation");
	}
	std::cout << checks << " UV operator checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
