// Intersect: two interpenetrating boxes cut along their intersection, then
// united or subtracted into closed shapes; coplanar overlaps refused; three
// poses; the CLI.
#include "core/model_mesh_tools.h"
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
// Box A spans -4..4 on every axis; box B is 6 units across, centred at
// (5, 0.5, 0.5), so it passes through A's +X side without sharing a plane.
ModelMesh boxes(int poses = 3, float bHalf = 3, ModelVec3 bCentre = {5, .5f, .5f})
{
	auto a = polygonCube(4, {}, poses), b = polygonCube(bHalf, bCentre, poses);
	auto &surface = a.surfaces[0];
	const int offset = surface.vertexCount;
	for (int f = 0; f < surface.frames.size(); ++f)
	{
		surface.frames[f].positions += b.surfaces[0].frames[f].positions;
		surface.frames[f].normals += b.surfaces[0].frames[f].normals;
	}
	surface.texCoords += b.surfaces[0].texCoords;
	for (auto t : b.surfaces[0].triangles)
		surface.triangles << ModelTriangle{t.a + offset, t.b + offset, t.c + offset};
	surface.vertexCount = surface.texCoords.size();
	updateEditableModelMetadata(&a);
	return a;
}
ModelEdit intersectEdit(ModelIntersectMode mode)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::IntersectCut;
	for (int f = 0; f < 12; ++f)
		edit.selection.faces.insert(f);
	edit.tool.intersectMode = mode;
	return edit;
}
int nonManifoldEdges(const ModelSurface &surface)
{
	ModelGeometricTopology topology;
	buildModelGeometricTopology(surface, &topology);
	int count = 0;
	for (const auto &faces : topology.edgeFaces)
		count += faces.size() != 2;
	return count;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	{
		auto mesh = boxes();
		ModelSelection result;
		expect(applyModelEdit(&mesh, intersectEdit(ModelIntersectMode::Cut), &result, &error), "cutting along the intersection applies", error);
		const auto &surface = mesh.surfaces[0];
		expect(surface.triangles.size() > 24 && result.edges.size() >= 8, "both boxes are cut and the intersection edges are selected");
		// The cut points sit on A's +X plane, joined between the boxes.
		int onPlane = 0;
		for (const auto &p : surface.frames[0].positions)
			onPlane += std::abs(p.x - 4) < 1e-4f && std::abs(p.y - .5f) <= 3.0001f && std::abs(p.z - .5f) <= 3.0001f;
		expect(onPlane >= 8, "the intersection loop lies where B's sides cross A's +X face");
		bool poses = true;
		for (int f = 1; f < 3; ++f)
		{
			for (int v = 0; v < surface.vertexCount; ++v)
				poses = poses && nearPoint(surface.frames[f].positions[v], {surface.frames[0].positions[v].x, surface.frames[0].positions[v].y,
																			  surface.frames[0].positions[v].z + 8 * f}, 1e-3);
		}
		expect(poses, "cut points follow every pose");
	}
	{
		auto mesh = boxes();
		ModelSelection result;
		expect(applyModelEdit(&mesh, intersectEdit(ModelIntersectMode::Union), &result, &error), "a union applies", error);
		const auto &surface = mesh.surfaces[0];
		expect(nonManifoldEdges(surface) == 0 && consistentlyWound(surface), "the union is one closed, consistently wound shell");
		bool hidden = false;
		for (const auto &t : surface.triangles)
		{
			const auto c = (model_geometry::point(surface.frames[0].positions[t.a]) + model_geometry::point(surface.frames[0].positions[t.b]) +
							model_geometry::point(surface.frames[0].positions[t.c])) *
						   (1.0 / 3);
			hidden = hidden || (c.x > -3.9 && c.x < 3.9 && c.y > -3.9 && c.y < 3.9 && c.z > -3.9 && c.z < 3.9);
		}
		expect(!hidden, "no face is left inside box A");
	}
	{
		auto mesh = boxes();
		expect(applyModelEdit(&mesh, intersectEdit(ModelIntersectMode::Difference), nullptr, &error) && nonManifoldEdges(mesh.surfaces[0]) == 0 &&
				   consistentlyWound(mesh.surfaces[0]),
			   "a difference leaves a closed shell with a pocket", error);
		bool beyond = false;
		for (const auto &p : mesh.surfaces[0].frames[0].positions)
			beyond = beyond || p.x > 4.0001f;
		expect(!beyond, "nothing of B outside A remains");
	}
	{
		// Refusals.
		auto coplanar = boxes(1, 4, {4, 0, 0});
		const auto before = bytes(coplanar);
		expect(!applyModelEdit(&coplanar, intersectEdit(ModelIntersectMode::Cut), nullptr, &error) && bytes(coplanar) == before,
			   "boxes sharing face planes are refused atomically");
		auto apart = boxes(1, 2, {20, 0, 0});
		expect(!applyModelEdit(&apart, intersectEdit(ModelIntersectMode::Cut), nullptr, &error) && error.contains(QStringLiteral("do not pass")),
			   "boxes that do not meet are refused");
		auto open = boxes(1);
		open.surfaces[0].triangles.removeAt(13);
		updateEditableModelMetadata(&open);
		expect(!applyModelEdit(&open, intersectEdit(ModelIntersectMode::Union), nullptr, &error), "a union needs closed parts");
	}
	// CLI.
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (!root.isEmpty())
		QDir().mkpath(root);
	QTemporaryDir temporary(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("intersect-XXXXXX"));
	ModelDocument document;
	const auto input = temporary.filePath("boxes.mesh.json"), output = temporary.filePath("united.mesh.json");
	expect(temporary.isValid() && document.setMesh(boxes(), &error) && document.save(input, false, &error), "save CLI source", error);
	if (argc > 1)
	{
		const auto exe = QString::fromLocal8Bit(argv[1]);
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "intersect", "--faces", "0,1,2,3,4,5,6,7,8,9,10,11", "--mode", "union", "--output",
							  output, "--json"},
							 0),
			   "model tool intersect --mode union");
		ModelDocument loaded;
		expect(loaded.load(output, &error) && nonManifoldEdges(loaded.mesh().surfaces[0]) == 0, "the CLI union reloads closed", error);
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "intersect", "--faces", "0", "--mode", "xor", "--overwrite", "--output", output}, 2),
			   "an unknown mode is a usage error");
	}
	std::cout << expect.checks << " intersect checks; " << expect.failures << " failures\n";
	return expect.failures ? 1 : 0;
}
