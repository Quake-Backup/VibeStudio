// Extrude Faces: region, along normals with even thickness, individual
// prisms, side-wall UV strips and seams, every pose, refusals, capacity and
// the CLI.
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
ModelEdit extrudeEdit(const QSet<int> &faces, ModelExtrudeMode mode, double distance)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::ExtrudeFaces;
	edit.selection.faces = faces;
	edit.tool.extrudeMode = mode;
	edit.tool.offset = distance;
	return edit;
}
// A square facing +Z on top of a square facing +X: a 90-degree corner along x = 8.
ModelMesh corner(int poses = 2)
{
	QVector<ModelVec3> p;
	QVector<ModelTriangle> t;
	appendSquare(&p, &t, {4, 4, 0}, {1, 0, 0}, {0, 1, 0});
	appendSquare(&p, &t, {8, 4, -4}, {0, 1, 0}, {0, 0, 1});
	return polygonMesh(p, t, poses);
}
QSet<int> pieceGroups(const ModelSurface &surface, const QSet<int> &faces)
{
	ModelGeometricTopology topology;
	buildModelGeometricTopology(surface, &topology);
	QSet<int> groups;
	for (int face : faces)
	{
		const auto &t = surface.triangles[face];
		groups << topology.group(t.a) << topology.group(t.b) << topology.group(t.c);
	}
	return groups;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	{
		// Individual faces: every triangle becomes its own prism.
		auto mesh = polygonGrid(2, 1);
		ModelSelection result;
		expect(applyModelEdit(&mesh, extrudeEdit(allFaces(mesh.surfaces[0]), ModelExtrudeMode::Individual, 2), &result, &error),
			   "individual extrusion applies", error);
		const auto &surface = mesh.surfaces[0];
		expect(surface.triangles.size() == 4 + 4 * 6 && result.faces.size() == 4, "four caps, each with three two-triangle walls");
		expect(pieceGroups(surface, result.faces).size() == 12, "the caps share no vertices: separate prisms with their own identities");
		bool lifted = true;
		for (int face : result.faces)
		{
			const auto &t = surface.triangles[face];
			for (int v : {t.a, t.b, t.c})
				lifted = lifted && std::abs(surface.frames[0].positions[v].z - 2) < 1e-4f && std::abs(surface.frames[1].positions[v].z - 10) < 1e-4f;
		}
		expect(lifted && consistentlyWound(surface, true), "each cap rises along its own normal in every pose, consistently wound");
	}
	{
		// Along normals with even thickness keeps a 90-degree corner's faces parallel.
		auto mesh = corner();
		ModelSelection result;
		expect(applyModelEdit(&mesh, extrudeEdit(allFaces(mesh.surfaces[0]), ModelExtrudeMode::AlongNormals, 2), &result, &error),
			   "along-normal extrusion applies", error);
		const auto &surface = mesh.surfaces[0];
		bool top = true, side = true, cornerMoved = false;
		for (int face : result.faces)
		{
			const auto &t = surface.triangles[face];
			for (int v : {t.a, t.b, t.c})
			{
				const auto q = surface.frames[0].positions[v];
				if (face == *std::min_element(result.faces.cbegin(), result.faces.cend()))
					top = top && std::abs(q.z - 2) < 1e-4f;
				cornerMoved = cornerMoved || nearPoint(q, {10, 0, 2});
			}
		}
		for (int face : result.faces)
		{
			const auto &t = surface.triangles[face];
			const auto a = surface.frames[0].positions[t.a], b = surface.frames[0].positions[t.b], c = surface.frames[0].positions[t.c];
			const bool onTop = std::abs(a.z - 2) < 1e-4f && std::abs(b.z - 2) < 1e-4f && std::abs(c.z - 2) < 1e-4f;
			const bool onSide = std::abs(a.x - 10) < 1e-4f && std::abs(b.x - 10) < 1e-4f && std::abs(c.x - 10) < 1e-4f;
			side = side && (onTop || onSide);
		}
		expect(top && side && cornerMoved, "even thickness moves each face the whole distance: the corner reaches (10, 0, 2)");
		expect(surface.triangles.size() == 4 + 2 * 6 && consistentlyWound(surface, true), "six border walls join the L-shaped cap to its base");
		auto uneven = corner();
		auto edit = extrudeEdit(allFaces(uneven.surfaces[0]), ModelExtrudeMode::AlongNormals, 2);
		edit.tool.evenThickness = false;
		expect(applyModelEdit(&uneven, edit, &result, &error), "uneven along-normal extrusion applies", error);
		bool shorter = false;
		for (const auto &q : uneven.surfaces[0].frames[0].positions)
			shorter = shorter || nearPoint(q, {float(8 + std::sqrt(2.0)), 0, float(std::sqrt(2.0))}, 1e-3);
		expect(shorter, "without even thickness the corner moves the distance along its averaged normal");
	}
	{
		// Region with UV walls: strips with area, one repeat per tile, seams marked.
		QVector<ModelVec3> p;
		QVector<ModelTriangle> t;
		appendSquare(&p, &t, {4, 4, 0}, {1, 0, 0}, {0, 1, 0});
		auto mesh = polygonMesh(p, t, 3);
		auto edit = extrudeEdit({0, 1}, ModelExtrudeMode::Region, 4);
		edit.tool.extrudeTileSize = 8;
		expect(applyModelEdit(&mesh, edit, nullptr, &error), "region extrusion with UV walls applies", error);
		const auto &surface = mesh.surfaces[0];
		bool area = surface.triangles.size() == 10;
		float widest = 0, tallest = 0;
		for (int face = 2; face < surface.triangles.size(); ++face)
		{
			area = area && uvArea(surface, face) > 1e-6;
			for (int v : {surface.triangles[face].a, surface.triangles[face].b, surface.triangles[face].c})
			{
				widest = std::max(widest, surface.texCoords[v].u);
				tallest = std::max(tallest, surface.texCoords[v].v);
			}
		}
		expect(area && std::abs(widest - 4) < 1e-4f && std::abs(tallest - .5f) < 1e-4f,
			   "each wall has UV area; the strip spans the 32-unit loop and 4-unit height at 8 units per repeat");
		expect(surface.uvSeams.size() >= 10, "the strip's rows and its closing column are marked as seams");
		expect(std::abs(surface.frames[2].positions[surface.triangles[0].a].z - 20) < 1e-4f, "the third pose rises with its own base");
		ModelGeometricTopology topology;
		expect(buildModelGeometricTopology(surface, &topology) && consistentlyWound(surface, true), "the walls stay connected to the cap");
		auto plain = polygonMesh(p, t, 3);
		edit.tool.extrudeTileSize = 0;
		expect(applyModelEdit(&plain, edit, nullptr, &error) && plain.surfaces[0].triangles.size() == 10 && uvArea(plain.surfaces[0], 5) < 1e-9,
			   "a zero tile size copies the border UVs onto the walls", error);
	}
	{
		// Closed shapes, refusals and capacity.
		auto cube = polygonCube();
		const auto before = QJsonDocument(editableModelJson(cube)).toJson();
		expect(!applyModelEdit(&cube, extrudeEdit(allFaces(cube.surfaces[0]), ModelExtrudeMode::Region, 2), nullptr, &error) &&
				   QJsonDocument(editableModelJson(cube)).toJson() == before,
			   "a closed region has no direction to extrude in and is refused atomically");
		expect(applyModelEdit(&cube, extrudeEdit(allFaces(cube.surfaces[0]), ModelExtrudeMode::AlongNormals, 1), nullptr, &error) &&
				   cube.surfaces[0].triangles.size() == 12 && consistentlyWound(cube.surfaces[0]),
			   "a closed shape extruded along its normals grows without walls", error);
		auto square = polygonGrid(1, 1);
		expect(!applyModelEdit(&square, extrudeEdit({0, 1}, ModelExtrudeMode::Region, 0), nullptr, &error), "a zero distance is refused");
		auto big = polygonGrid(100, 100, 1, 1);
		const auto bigBefore = QJsonDocument(editableModelJson(big)).toJson();
		expect(!applyModelEdit(&big, extrudeEdit(allFaces(big.surfaces[0]), ModelExtrudeMode::Individual, 1), nullptr, &error) &&
				   error.contains(QStringLiteral("limit")) && QJsonDocument(editableModelJson(big)).toJson() == bigBefore,
			   "an extrusion over the triangle limit is refused before allocating", error);
		ModelEdit stray;
		stray.kind = ModelEditKind::Spin;
		stray.selection.faces = {0};
		stray.tool.extrudeTileSize = 4;
		expect(!applyModelEdit(&square, stray, nullptr, &error), "extrusion settings apply only to Extrude Faces");
	}
	// CLI.
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (!root.isEmpty())
		QDir().mkpath(root);
	QTemporaryDir temporary(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("extrude-XXXXXX"));
	ModelDocument document;
	const auto input = temporary.filePath("grid.mesh.json"), output = temporary.filePath("extruded.mesh.json");
	expect(temporary.isValid() && document.setMesh(polygonGrid(2, 1), &error) && document.save(input, false, &error), "save CLI source", error);
	if (argc > 1)
	{
		const auto exe = QString::fromLocal8Bit(argv[1]);
		QJsonObject json;
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "extrude", "--faces", "all", "--mode", "individual", "--distance", "2",
							  "--tile-size", "8", "--output", output, "--json"},
							 0, &json) &&
				   json.value("surface").toObject().value("triangles").toInt() == 28 &&
				   json.value("selection").toObject().value("faces").toArray().size() == 4,
			   "model tool extrude --mode individual writes four prisms and selects their caps");
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "extrude", "--faces", "all", "--mode", "normals", "--distance", "1", "--no-even",
							  "--overwrite", "--output", output},
							 0),
			   "model tool extrude --mode normals applies");
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "extrude", "--faces", "all", "--mode", "sideways", "--distance", "1", "--overwrite",
							  "--output", output},
							 2),
			   "an unknown extrude mode is a usage error");
		expect(runPolygonCli(exe, temporary.path(), {"model", "tool", input, "--tool", "extrude", "--faces", "all", "--overwrite", "--output", output}, 2),
			   "extrude requires --distance");
	}
	std::cout << expect.checks << " extrude checks; " << expect.failures << " failures\n";
	return expect.failures ? 1 : 0;
}
