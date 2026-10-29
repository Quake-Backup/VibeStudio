// Intersect: two interpenetrating boxes cut along their intersection, then
// united, subtracted or reduced to their shared volume; the same against
// another surface; large crossing grids; coplanar overlaps refused; three
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
	{
		// Shared volume: only what lies inside both boxes remains, closed.
		auto mesh = boxes();
		expect(applyModelEdit(&mesh, intersectEdit(ModelIntersectMode::Intersection), nullptr, &error) && nonManifoldEdges(mesh.surfaces[0]) == 0 &&
				   consistentlyWound(mesh.surfaces[0]),
			   "an intersection leaves one closed shell", error);
		bool within = !mesh.surfaces[0].triangles.isEmpty();
		for (const auto &t : mesh.surfaces[0].triangles)
		{
			for (int v : {t.a, t.b, t.c})
			{
				const auto p = mesh.surfaces[0].frames[0].positions[v];
				within = within && p.x > 1.9999f && p.x < 4.0001f && std::abs(p.y - .5f) < 3.0001f && std::abs(p.z - .5f) < 3.0001f;
			}
		}
		expect(within, "every remaining corner lies in both boxes");
	}
	{
		// Another surface as the cutter: box A on surface 0, box B on surface 1.
		const auto separate = [](int poses = 3)
		{
			auto mesh = polygonCube(4, {}, poses);
			auto other = polygonCube(3, {5, .5f, .5f}, poses);
			auto surface = other.surfaces[0];
			surface.name = QStringLiteral("cutter");
			mesh.surfaces << surface;
			updateEditableModelMetadata(&mesh);
			return mesh;
		};
		const auto centroid = [](const ModelSurface &surface, int face)
		{
			const auto &p = surface.frames[0].positions;
			const auto &t = surface.triangles[face];
			return ModelVec3{(p[t.a].x + p[t.b].x + p[t.c].x) / 3, (p[t.a].y + p[t.b].y + p[t.c].y) / 3, (p[t.a].z + p[t.b].z + p[t.c].z) / 3};
		};
		const auto insideA = [](ModelVec3 c) { return std::abs(c.x) < 3.999f && std::abs(c.y) < 3.999f && std::abs(c.z) < 3.999f; };
		const auto insideB = [](ModelVec3 c)
		{ return std::abs(c.x - 5) < 2.999f && std::abs(c.y - .5f) < 2.999f && std::abs(c.z - .5f) < 2.999f; };
		auto edit = intersectEdit(ModelIntersectMode::Cut);
		edit.tool.intersectSurface = 1;
		auto mesh = separate();
		ModelSelection result;
		expect(applyModelEdit(&mesh, edit, &result, &error) && mesh.surfaces[0].triangles.size() > 12 && mesh.surfaces[1].triangles.size() > 12 &&
				   result.surface == 0 && result.edges.size() >= 8,
			   "cutting against another surface cuts both surfaces and selects the new edges", error);
		bool poses = true;
		for (int s = 0; s < 2; ++s)
			for (int v = 0; v < mesh.surfaces[s].vertexCount; ++v)
				poses = poses && nearPoint(mesh.surfaces[s].frames[2].positions[v],
										   {mesh.surfaces[s].frames[0].positions[v].x, mesh.surfaces[s].frames[0].positions[v].y,
											mesh.surfaces[s].frames[0].positions[v].z + 16},
										   1e-3);
		expect(poses, "both surfaces' cut points follow every pose");
		for (const auto mode : {ModelIntersectMode::Union, ModelIntersectMode::Difference, ModelIntersectMode::Intersection})
		{
			auto parts = separate();
			auto boolean = intersectEdit(mode);
			boolean.tool.intersectSurface = 1;
			if (!expect(applyModelEdit(&parts, boolean, nullptr, &error), "a boolean against another surface applies", error))
				continue;
			bool right = !parts.surfaces[0].triangles.isEmpty() && !parts.surfaces[1].triangles.isEmpty();
			for (int f = 0; f < parts.surfaces[0].triangles.size(); ++f)
			{
				const bool in = insideB(centroid(parts.surfaces[0], f));
				right = right && (mode == ModelIntersectMode::Intersection ? in : !in);
			}
			for (int f = 0; f < parts.surfaces[1].triangles.size(); ++f)
			{
				const bool in = insideA(centroid(parts.surfaces[1], f));
				right = right && (mode == ModelIntersectMode::Union ? !in : in);
			}
			expect(right && consistentlyWound(parts.surfaces[0], true) && consistentlyWound(parts.surfaces[1], true),
				   mode == ModelIntersectMode::Union		? "union across surfaces drops each surface's hidden faces"
				   : mode == ModelIntersectMode::Difference ? "difference across surfaces keeps the cutter's inside as the pocket"
															: "intersection across surfaces keeps each surface's part inside the other");
		}
		auto missing = separate(1);
		auto stray = intersectEdit(ModelIntersectMode::Cut);
		stray.tool.intersectSurface = 7;
		const auto before = bytes(missing);
		expect(!applyModelEdit(&missing, stray, nullptr, &error) && bytes(missing) == before, "a missing cutting surface is refused");
		ModelEdit other;
		other.kind = ModelEditKind::Knife;
		other.selection.faces = {0};
		other.tool.intersectSurface = 1;
		expect(!applyModelEdit(&missing, other, nullptr, &error), "the cutting surface applies only to Intersect");
	}
	{
		// Many faces: two crossing 48x48 grids on separate surfaces pair through
		// the face grid rather than face by face.
		auto mesh = polygonGrid(48, 48, 1, 1);
		auto wall = polygonGrid(48, 48, 1, 1);
		for (auto &p : wall.surfaces[0].frames[0].positions)
			p = {20.25f, p.x - 4.f, p.y - 24.5f};
		auto surface = wall.surfaces[0];
		surface.name = QStringLiteral("wall");
		mesh.surfaces << surface;
		updateEditableModelMetadata(&mesh);
		ModelEdit edit;
		edit.kind = ModelEditKind::IntersectCut;
		edit.selection.faces = allFaces(mesh.surfaces[0]);
		edit.tool.intersectSurface = 1;
		ModelSelection result;
		expect(applyModelEdit(&mesh, edit, &result, &error) && result.edges.size() >= 44 && mesh.surfaces[1].triangles.size() > 48 * 48 * 2,
			   "large crossing grids are cut along their whole crossing", error);
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
		// Against another surface, keeping the shared volume.
		auto parts = polygonCube(4, {}, 3);
		auto cutter = polygonCube(3, {5, .5f, .5f}, 3).surfaces[0];
		cutter.name = QStringLiteral("cutter");
		parts.surfaces << cutter;
		updateEditableModelMetadata(&parts);
		const auto twoSurfaces = temporary.filePath("two.mesh.json");
		ModelDocument two;
		expect(two.setMesh(parts, &error) && two.save(twoSurfaces, false, &error), "save the two-surface CLI source", error);
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", twoSurfaces, "--tool", "intersect", "--faces", "all", "--with-surface", "1", "--mode", "intersection",
							  "--overwrite", "--output", output},
							 0) &&
				   loaded.load(output, &error) && loaded.mesh().surfaces.size() == 2 && loaded.mesh().surfaces[1].triangles.size() > 0,
			   "model tool intersect --with-surface 1 --mode intersection", error);
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", twoSurfaces, "--tool", "intersect", "--faces", "all", "--with-surface", "x", "--overwrite", "--output", output},
							 2),
			   "a non-numeric cutting surface is a usage error");
	}
	std::cout << expect.checks << " intersect checks; " << expect.failures << " failures\n";
	return expect.failures ? 1 : 0;
}
