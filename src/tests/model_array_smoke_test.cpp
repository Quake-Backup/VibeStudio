// Linear arrays and screws: copies, offsets, rotation, merged ends, every
// pose, identities, capacity, the spin axial offset and the CLI.
#include "core/model_mesh_tools.h"
#include "tests/model_polygon_test_helpers.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <iostream>
#include <numbers>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
PolygonChecks expect;
// One 8-unit square in the XY plane from (0,0) to (8,8).
ModelMesh square()
{
	QVector<ModelVec3> p;
	QVector<ModelTriangle> t;
	appendSquare(&p, &t, {4, 4, 0}, {1, 0, 0}, {0, 1, 0});
	return polygonMesh(p, t);
}
ModelEdit arrayEdit(int count)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::Array;
	edit.selection.faces = {0, 1};
	edit.tool.arrayCount = count;
	edit.tool.point = {4, 4, 0};
	return edit;
}
int groups(const ModelSurface &surface)
{
	ModelGeometricTopology topology;
	buildModelGeometricTopology(surface, &topology);
	int count = 0;
	for (int v = 0; v < surface.vertexCount; ++v)
		count += topology.group(v) == v && !topology.groupFaces[v].isEmpty();
	return count;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	{
		// Four copies along the relative offset (one selection width each).
		auto mesh = square();
		auto edit = arrayEdit(4);
		ModelSelection selection;
		expect(applyModelEdit(&mesh, edit, &selection, &error), "a four-copy array applies", error);
		const auto &surface = mesh.surfaces[0];
		expect(surface.triangles.size() == 8 && surface.vertexCount == 16 && selection.faces.size() == 8,
			   "four copies of a two-triangle square, all selected");
		expect(nearPoint(surface.frames[0].positions[12], {24, 0, 0}) && nearPoint(surface.frames[1].positions[12], {24, 0, 8}),
			   "the last copy sits three widths along X in every pose");
		expect(groups(surface) == 16, "unmerged copies stay separate pieces with their own identities");
		expect(consistentlyWound(surface, true), "copies keep the selection's winding");
	}
	{
		// Merged ends join the copies into one manifold strip.
		auto mesh = square();
		auto edit = arrayEdit(4);
		edit.tool.arrayMerge = .01;
		expect(applyModelEdit(&mesh, edit, nullptr, &error), "a merged array applies", error);
		const auto &surface = mesh.surfaces[0];
		ModelGeometricTopology topology;
		expect(buildModelGeometricTopology(surface, &topology), "merged strip topology builds");
		int interior = 0, border = 0;
		for (const auto &faces : topology.edgeFaces)
		{
			interior += faces.size() == 2;
			border += faces.size() == 1;
		}
		expect(groups(surface) == 10 && border == 10 && consistentlyWound(surface, true),
			   "merged ends share vertices: a 4x1 strip has 10 corners and 10 border edges");
		expect(interior == 7, "three joins and four diagonals are interior edges");
		expect(surface.vertexCount == 16, "merged vertices keep their own UVs as seam copies");
	}
	{
		// Constant offset with a rotation: links of a chain turn a quarter each.
		auto mesh = square();
		auto edit = arrayEdit(3);
		edit.tool.arrayRelative = {0, 0, 0};
		edit.tool.arrayOffset = {10, 0, 0};
		edit.tool.arrayRotation = {90, 0, 0};
		expect(applyModelEdit(&mesh, edit, nullptr, &error), "a rotated array applies", error);
		const auto &p = mesh.surfaces[0].frames[0].positions;
		// Corner (0,0,0) turns about X through the centre (4,4,0): (0,4,-4), then moves by 10.
		expect(nearPoint(p[4], {10, 4, -4}) && nearPoint(p[8], {20, 8, 0}), "each copy turns about the centre and moves by the offset");
		expect(nearPoint(mesh.surfaces[0].frames[0].normals[4], {0, -1, 0}, 1e-3), "copied normals turn with their faces");
	}
	{
		// Scale and refusals.
		auto mesh = square();
		auto edit = arrayEdit(2);
		edit.tool.arrayRelative = {0, 0, 0};
		const auto before = QJsonDocument(editableModelJson(mesh)).toJson();
		expect(!applyModelEdit(&mesh, edit, nullptr, &error) && !error.isEmpty() && QJsonDocument(editableModelJson(mesh)).toJson() == before,
			   "copies that would sit on the selection are refused atomically");
		edit.tool.arrayScale = .5;
		expect(applyModelEdit(&mesh, edit, nullptr, &error) && nearPoint(mesh.surfaces[0].frames[0].positions[4], {2, 2, 0}),
			   "a scaled copy shrinks about the centre", error);
		edit = arrayEdit(300);
		expect(!applyModelEdit(&mesh, edit, nullptr, &error), "more than 256 copies are refused");
		auto grid = polygonGrid(20, 20, 2, 2);
		ModelEdit big;
		big.kind = ModelEditKind::Array;
		big.selection.faces = allFaces(grid.surfaces[0]);
		big.tool.arrayCount = 256;
		const auto gridBefore = QJsonDocument(editableModelJson(grid)).toJson();
		expect(!applyModelEdit(&grid, big, nullptr, &error) && error.contains(QStringLiteral("limit")) &&
				   QJsonDocument(editableModelJson(grid)).toJson() == gridBefore,
			   "an array over the vertex limit is refused before allocating", error);
		ModelEdit stray;
		stray.kind = ModelEditKind::Spin;
		stray.selection.faces = {0};
		stray.tool.arrayCount = 4;
		expect(!applyModelEdit(&mesh, stray, nullptr, &error), "array settings apply only to the array tool");
	}
	{
		// Screw: a full turn that climbs 32 units over 8 steps.
		QVector<ModelVec3> p;
		QVector<ModelTriangle> t;
		appendSquare(&p, &t, {20, 0, 4}, {1, 0, 0}, {0, 0, 1});
		auto mesh = polygonMesh(p, t);
		ModelEdit screw;
		screw.kind = ModelEditKind::Spin;
		screw.selection.faces = {0, 1};
		screw.tool.spinAngle = 360;
		screw.tool.spinSteps = 8;
		screw.tool.spinAxialOffset = 32;
		ModelEditReport report;
		expect(applyModelEdit(&mesh, screw, nullptr, &error, {}, &report), "a screw applies", error);
		const auto &surface = mesh.surfaces[0];
		const double c = std::sqrt(.5);
		bool helix = false, cap = false, lifted = false;
		for (int v = 0; v < surface.vertexCount; ++v)
		{
			const auto q = surface.frames[0].positions[v];
			helix = helix || nearPoint(q, {float(16 * c), float(16 * c), 4}, 1e-3);
			cap = cap || nearPoint(q, {16, 0, 32}, 1e-3);
			lifted = lifted || nearPoint(surface.frames[1].positions[v], {float(16 * c), float(16 * c), 12}, 1e-3);
		}
		expect(helix && cap && lifted, "each step turns 45 degrees and climbs 4 units in every pose; the end cap climbs the whole offset");
		expect(consistentlyWound(surface, true), "the screw band and its end cap are consistently wound");
		screw.tool.spinAxialOffset = 2000000;
		auto other = polygonMesh(p, t);
		expect(!applyModelEdit(&other, screw, nullptr, &error), "an out-of-range axial offset is refused");
	}
	// CLI.
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (!root.isEmpty())
		QDir().mkpath(root);
	QTemporaryDir temporary(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("array-XXXXXX"));
	ModelDocument document;
	const auto input = temporary.filePath("square.mesh.json"), output = temporary.filePath("array.mesh.json");
	expect(temporary.isValid() && document.setMesh(square(), &error) && document.save(input, false, &error), "save CLI source", error);
	if (argc > 1)
	{
		const auto exe = QString::fromLocal8Bit(argv[1]);
		QJsonObject json;
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "array", "--faces", "all", "--count", "4", "--merge", "0.01", "--output", output,
							  "--json"},
							 0, &json) &&
				   json.value("surface").toObject().value("triangles").toInt() == 8,
			   "model tool array writes four merged copies");
		ModelDocument loaded;
		expect(loaded.load(output, &error) && groups(loaded.mesh().surfaces[0]) == 10, "the CLI array reloads as one strip", error);
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "array", "--faces", "all", "--count", "3", "--relative", "0,0,0", "--offset",
							  "0,0,10", "--rotate", "0,0,45", "--scale", "0.5", "--overwrite", "--output", output},
							 0),
			   "model tool array takes offset, rotation and scale");
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "array", "--faces", "all", "--count", "1", "--overwrite", "--output", output}, 2),
			   "a count below two is a usage error");
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "spin", "--faces", "all", "--angle", "360", "--axis", "x", "--centre", "0,-8,0", "--axial-offset",
							  "16", "--overwrite", "--output", output},
							 0),
			   "model tool spin takes --axial-offset for a screw");
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "array", "--faces", "all", "--axial-offset", "16", "--overwrite", "--output", output},
							 2),
			   "array refuses spin options");
	}
	std::cout << expect.checks << " array and screw checks; " << expect.failures << " failures\n";
	return expect.failures ? 1 : 0;
}
