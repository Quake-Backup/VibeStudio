// Bridging with segments, open chains, UV strips and face regions: counts,
// interpolated poses, winding, seams, refusals and the CLI.
#include "core/model_boundary_bridge.h"
#include "tests/model_boundary_bridge_test_helpers.h"
#include "tests/model_polygon_test_helpers.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
PolygonChecks expect;
QByteArray bytes(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
// Two strips with a gap between x = 8 and x = 12: the first's right border has
// three vertices, the second's left border four.
ModelMesh twoStrips()
{
	QVector<ModelVec3> p;
	QVector<ModelTriangle> t;
	for (float y : {0.f, 8.f, 16.f})
		p << ModelVec3{0, y, 0} << ModelVec3{8, y, 0};
	t << ModelTriangle{0, 1, 3} << ModelTriangle{0, 3, 2} << ModelTriangle{2, 3, 5} << ModelTriangle{2, 5, 4};
	const int base = p.size();
	for (int k = 0; k < 4; ++k)
		p << ModelVec3{12, float(16.0 * k / 3), 0} << ModelVec3{20, float(16.0 * k / 3), 0};
	for (int k = 0; k < 3; ++k)
	{
		const int a = base + 2 * k;
		t << ModelTriangle{a, a + 1, a + 3} << ModelTriangle{a, a + 3, a + 2};
	}
	return polygonMesh(p, t);
}
ModelEdit bridgeEdit(QSet<ModelEdge> edges)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::BridgeBoundaryLoops;
	edit.selection.edges = std::move(edges);
	return edit;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	{
		// Open chains of three and four vertices.
		auto mesh = twoStrips();
		auto edit = bridgeEdit({modelEdge(1, 3), modelEdge(3, 5), modelEdge(6, 8), modelEdge(8, 10), modelEdge(10, 12)});
		edit.bridgeOpen = true;
		ModelSelection result;
		expect(applyModelEdit(&mesh, edit, &result, &error), "open chains bridge", error);
		const auto &surface = mesh.surfaces[0];
		expect(surface.triangles.size() == 10 + 5 && result.faces.size() == 5 && surface.vertexCount == 14,
			   "two and three side edges make five strip triangles from existing vertices");
		expect(consistentlyWound(surface, true), "the strip winds with both neighbours");
		auto loops = twoStrips();
		auto cycle = edit;
		cycle.selection.edges = {modelEdge(0, 1), modelEdge(1, 3), modelEdge(3, 5), modelEdge(5, 4), modelEdge(4, 2), modelEdge(2, 0),
								 modelEdge(6, 8), modelEdge(8, 10), modelEdge(10, 12)};
		expect(!applyModelEdit(&loops, cycle, nullptr, &error) && bytes(loops) == bytes(twoStrips()), "a closed loop is refused as an open chain");
		auto branched = twoStrips();
		auto twisted = edit;
		twisted.bridgeTwist = 1;
		expect(!applyModelEdit(&branched, twisted, nullptr, &error) && bytes(branched) == bytes(twoStrips()), "twist is refused for open chains atomically");
	}
	{
		// Segments: rings interpolated in every pose.
		auto mesh = bridgeDisks();
		auto edit = bridgeEdit({modelEdge(0, 1), modelEdge(4, 5)});
		edit.bridgeSegments = 2;
		ModelSelection result;
		const auto before = mesh;
		expect(applyModelEdit(&mesh, edit, &result, &error), "a two-segment bridge applies", error);
		const auto &surface = mesh.surfaces[0];
		expect(surface.triangles.size() == 4 + 8 * 5 && surface.vertexCount == 8 + 16 && result.faces.size() == 40,
			   "two rings: 16 new vertices and five triangles per side edge");
		bool thirds = true;
		for (int frame = 0; frame < 2; ++frame)
		{
			const auto &pose = surface.frames[frame].positions;
			const auto &old = before.surfaces[0].frames[frame].positions;
			for (int v = 8; v < surface.vertexCount; ++v)
			{
				bool between = false;
				for (int a = 0; a < 4 && !between; ++a)
					for (int b = 4; b < 8 && !between; ++b)
						for (double t : {1.0 / 3, 2.0 / 3})
							between = between || nearPoint(pose[v], {float(old[a].x + (old[b].x - old[a].x) * t), float(old[a].y + (old[b].y - old[a].y) * t),
																		float(old[a].z + (old[b].z - old[a].z) * t)}, 1e-3);
				thirds = thirds && between;
			}
		}
		expect(thirds, "every ring vertex sits a third or two thirds across a bridge edge in each pose");
		expect(consistentlyWound(surface), "the segmented bridge closes the prism consistently");
	}
	{
		// A UV strip: own vertices, area, seams.
		auto mesh = bridgeDisks();
		auto edit = bridgeEdit({modelEdge(0, 1), modelEdge(4, 5)});
		edit.bridgeTileSize = 4;
		ModelSelection result;
		expect(applyModelEdit(&mesh, edit, &result, &error), "a UV-strip bridge applies", error);
		const auto &surface = mesh.surfaces[0];
		bool area = !result.faces.isEmpty();
		for (int face : result.faces)
			area = area && uvArea(surface, face) > 1e-6;
		expect(area && surface.vertexCount == 8 + 10, "strip faces have UV area on ten new side vertices");
		expect(surface.uvSeams.size() >= 2 + 10, "both sides and the wrap column are marked as seams");
		expect(consistentlyWound(surface), "the strip's copies stay connected to the disks");
		auto bad = bridgeDisks();
		edit.bridgeSegments = 65;
		expect(!applyModelEdit(&bad, edit, nullptr, &error), "more than 64 segments are refused");
	}
	{
		// Bridge Faces between two cubes facing each other.
		auto mesh = polygonCube(4, {-10, 0, 0});
		auto other = polygonCube(4, {10, 0, 0});
		auto &surface = mesh.surfaces[0];
		const int offset = surface.vertexCount;
		for (int f = 0; f < surface.frames.size(); ++f)
		{
			surface.frames[f].positions += other.surfaces[0].frames[f].positions;
			surface.frames[f].normals += other.surfaces[0].frames[f].normals;
		}
		surface.texCoords += other.surfaces[0].texCoords;
		for (auto t : other.surfaces[0].triangles)
			surface.triangles << ModelTriangle{t.a + offset, t.b + offset, t.c + offset};
		surface.vertexCount = surface.texCoords.size();
		updateEditableModelMetadata(&mesh);
		// The first cube's +X side (faces 6, 7) and the second's -X side (faces 22, 23).
		ModelEdit edit;
		edit.kind = ModelEditKind::BridgeFaces;
		edit.selection.faces = {6, 7, 22, 23};
		edit.bridgeSegments = 1;
		ModelSelection result;
		expect(applyModelEdit(&mesh, edit, &result, &error), "bridging two face regions applies", error);
		const auto &joined = mesh.surfaces[0];
		expect(joined.triangles.size() == 24 - 4 + 8 * 3 && consistentlyWound(joined) && result.faces.size() == 24,
			   "the two cubes join into one closed, consistently wound shape");
		auto lonely = polygonCube(4);
		ModelEdit one;
		one.kind = ModelEditKind::BridgeFaces;
		one.selection.faces = {6, 7};
		expect(!applyModelEdit(&lonely, one, nullptr, &error) && bytes(lonely) == bytes(polygonCube(4)), "one region is refused atomically");
	}
	// CLI.
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	QTemporaryDir temporary(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("bridge-options-XXXXXX"));
	ModelDocument document;
	const auto input = temporary.filePath("strips.mesh.json"), disks = temporary.filePath("disks.mesh.json"),
			   output = temporary.filePath("bridged.mesh.json");
	ModelDocument second;
	expect(temporary.isValid() && document.setMesh(twoStrips(), &error) && document.save(input, false, &error) &&
			   second.setMesh(bridgeDisks(), &error) && second.save(disks, false, &error),
		   "save CLI sources", error);
	if (argc > 1)
	{
		const auto exe = QString::fromLocal8Bit(argv[1]);
		QJsonObject json;
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "edit", input, "--operation", "bridge-boundary-loops", "--edges", "1:3,3:5,6:8,8:10,10:12", "--bridge-open",
							  "--output", output, "--json"},
							 0),
			   "model edit bridges open chains");
		ModelDocument loaded;
		expect(loaded.load(output, &error) && loaded.mesh().surfaces[0].triangles.size() == 15, "the CLI chain bridge reloads", error);
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "edit", disks, "--operation", "bridge-boundary-loops", "--edges", "0:1,4:5", "--bridge-segments", "2",
							  "--tile-size", "4", "--overwrite", "--output", output},
							 0),
			   "model edit takes segments and a UV strip");
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "edit", disks, "--operation", "fill-boundary-loops", "--edges", "0:1", "--bridge-segments", "2", "--overwrite",
							  "--output", output},
							 2),
			   "segments are refused for other operations");
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "edit", disks, "--operation", "bridge-faces", "--edges", "0:1", "--overwrite", "--output", output}, 2),
			   "bridge-faces requires faces");
	}
	std::cout << expect.checks << " bridge option checks; " << expect.failures << " failures\n";
	return expect.failures ? 1 : 0;
}
