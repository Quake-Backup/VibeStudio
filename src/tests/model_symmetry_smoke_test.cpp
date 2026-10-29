// Live symmetric modelling and Mirror Faces: extrude, loop cut, inset and
// dissolve on one side mirrored to the other in every pose, plane vertices,
// refusals, selection on both sides, mirrored winding, gizmo previews that
// match the committed weighted transform, and the CLI.
#include "core/model_mesh_tools.h"
#include "core/model_symmetry.h"
#include "core/model_transform_pieces.h"
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
// A 4x2 grid of 8-unit quads from x = -16 to 16: symmetric across X = 0.
ModelMesh halves(int poses = 3)
{
	QVector<ModelVec3> p;
	QVector<ModelTriangle> t;
	for (int j = 0; j <= 2; ++j)
	{
		for (int i = 0; i <= 4; ++i)
			p << ModelVec3{float(-16 + 8 * i), float(8 * j), 0};
	}
	const auto at = [](int i, int j) { return j * 5 + i; };
	for (int j = 0; j < 2; ++j)
	{
		for (int i = 0; i < 4; ++i)
			t << ModelTriangle{at(i, j), at(i + 1, j), at(i + 1, j + 1)} << ModelTriangle{at(i, j), at(i + 1, j + 1), at(i, j + 1)};
	}
	return polygonMesh(p, t, poses);
}
// Faces of the quad in column i, row j.
QSet<int> quad(int i, int j)
{
	return {2 * (j * 4 + i), 2 * (j * 4 + i) + 1};
}
bool mirrored(const ModelSurface &surface, int frame)
{
	// Every vertex has a counterpart across X = 0 in this pose.
	for (const auto &p : surface.frames[frame].positions)
	{
		bool found = false;
		for (const auto &q : surface.frames[frame].positions)
			found = found || nearPoint(q, {-p.x, p.y, p.z}, 1e-4);
		if (!found)
			return false;
	}
	return true;
}
int onLine(const ModelSurface &surface, float x)
{
	int count = 0;
	for (const auto &p : surface.frames[0].positions)
		count += std::abs(p.x - x) < 1e-4f;
	return count;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	{
		// Extrude the outer +X quad; the -X quad follows.
		auto mesh = halves();
		ModelEdit edit;
		edit.kind = ModelEditKind::ExtrudeFaces;
		edit.selection.faces = quad(3, 0);
		edit.tool.offset = 4;
		edit.tool.symmetryAxes = 1;
		ModelSelection result;
		expect(applyModelEdit(&mesh, edit, &result, &error), "a symmetric extrusion applies", error);
		const auto &surface = mesh.surfaces[0];
		expect(surface.triangles.size() == 32, "both sides gain a cap and four walls");
		expect(mirrored(surface, 0) && mirrored(surface, 2), "the result mirrors across X in every pose");
		bool raised = false;
		for (const auto &p : surface.frames[2].positions)
			raised = raised || nearPoint(p, {-16, 0, 20});
		expect(raised, "the mirrored cap rises in the third pose too");
		expect(result.faces.size() == 4 && consistentlyWound(surface, true), "both caps stay selected and the surface stays consistently wound");
		expect(onLine(surface, 0) == 3, "vertices on the mirror plane stay on it, shared by both halves");
	}
	{
		// Loop cut across the outer +X column; the cut appears at x = -12 too.
		auto mesh = halves();
		ModelEdit edit;
		edit.kind = ModelEditKind::LoopCut;
		edit.selection.edges = {modelEdge(3, 4)};
		edit.tool.symmetryAxes = 1;
		ModelSelection result;
		expect(applyModelEdit(&mesh, edit, &result, &error), "a symmetric loop cut applies", error);
		expect(onLine(mesh.surfaces[0], 12) == 3 && onLine(mesh.surfaces[0], -12) == 3 && mirrored(mesh.surfaces[0], 1),
			   "the cut is mirrored", error);
		expect(result.edges.size() >= 4, "the new edges on both sides stay selected");
	}
	{
		// Inset and dissolve on +X.
		auto mesh = halves();
		ModelEdit inset;
		inset.kind = ModelEditKind::InsetFaces;
		inset.selection.faces = quad(2, 1);
		inset.tool.insetThickness = 2;
		inset.tool.symmetryAxes = 1;
		expect(applyModelEdit(&mesh, inset, nullptr, &error) && mirrored(mesh.surfaces[0], 0) && mesh.surfaces[0].triangles.size() == 16 + 2 * 8,
			   "a symmetric inset applies to both sides", error);
		auto flat = halves();
		ModelEdit dissolve;
		dissolve.kind = ModelEditKind::DissolveFaces;
		dissolve.selection.faces = quad(2, 0) + quad(3, 0);
		dissolve.tool.symmetryAxes = 1;
		expect(applyModelEdit(&flat, dissolve, nullptr, &error) && mirrored(flat.surfaces[0], 0) && consistentlyWound(flat.surfaces[0], true),
			   "a symmetric dissolve retriangulates both sides alike", error);
	}
	{
		// Refusals and a selection that is already symmetric.
		auto mesh = halves();
		const auto before = bytes(mesh);
		ModelEdit edit;
		edit.kind = ModelEditKind::ExtrudeFaces;
		edit.selection.faces = quad(3, 0) + quad(1, 1);
		edit.tool.offset = 4;
		edit.tool.symmetryAxes = 1;
		expect(!applyModelEdit(&mesh, edit, nullptr, &error) && error.contains(QStringLiteral("both sides")) && bytes(mesh) == before,
			   "a selection on both sides that is not symmetric is refused atomically");
		edit.selection.faces = quad(3, 0) + quad(0, 0);
		expect(applyModelEdit(&mesh, edit, nullptr, &error) && mesh.surfaces[0].triangles.size() == 32, "a symmetric selection edits both sides directly",
			   error);
		auto scoped = halves();
		ModelEdit smooth;
		smooth.kind = ModelEditKind::ShrinkFatten;
		smooth.selection.faces = quad(3, 0);
		smooth.tool.offset = 1;
		smooth.tool.symmetryAxes = 1;
		smooth.frame = 1;
		expect(!applyModelEdit(&scoped, smooth, nullptr, &error), "symmetric editing needs the all-frames scope");
		ModelEdit decimate;
		decimate.kind = ModelEditKind::Decimate;
		decimate.selection.faces = quad(3, 0);
		decimate.tool.symmetryAxes = 1;
		expect(!applyModelEdit(&scoped, decimate, nullptr, &error) && !modelSymmetrySupported(ModelEditKind::Decimate),
			   "tools without symmetry refuse the setting");
	}
	{
		// Mirror Faces: a detached square reflected across the plane x = 0.
		QVector<ModelVec3> p;
		QVector<ModelTriangle> t;
		appendSquare(&p, &t, {20, 0, 0}, {1, 0, 0}, {0, 1, 0});
		appendSquare(&p, &t, {40, 0, 0}, {1, 0, 0}, {0, 1, 0});
		auto mesh = polygonMesh(p, t);
		ModelEdit edit;
		edit.kind = ModelEditKind::MirrorFaces;
		edit.selection.faces = {0, 1};
		edit.tool.axis = 0;
		expect(applyModelEdit(&mesh, edit, nullptr, &error), "mirroring a detached square applies", error);
		const auto &surface = mesh.surfaces[0];
		expect(nearPoint(surface.frames[0].positions[0], {-16, -4, 0}) && nearPoint(surface.frames[1].positions[0], {-16, -4, 8}) &&
				   nearPoint(surface.frames[0].positions[4], {36, -4, 0}),
			   "the selected square moves across the plane in every pose; the other stays");
		expect(consistentlyWound(surface, true) && surface.frames[0].normals[0].z > 0.99f, "the mirrored faces keep facing out");
		edit.tool.point = {30, 0, 0};
		edit.selection.faces = {2, 3};
		expect(applyModelEdit(&mesh, edit, nullptr, &error) && nearPoint(mesh.surfaces[0].frames[0].positions[4], {24, -4, 0}),
			   "the plane can pass through the 3D cursor", error);
		auto grid = halves(2);
		ModelEdit attached = edit;
		attached.selection.faces = quad(3, 0);
		const auto before = bytes(grid);
		expect(!applyModelEdit(&grid, attached, nullptr, &error) && bytes(grid) == before, "faces attached to unselected faces are refused");
	}
	{
		// Gizmo previews: the weighted preview map reproduces the committed
		// Weighted Transform for symmetry, proportional editing, both, and
		// symmetry with Individual Origins, in every pose.
		const auto grid = halves(2);
		struct Case
		{
			int mirror;
			double radius;
			bool individual;
			QSet<int> vertices;
		};
		const QVector<Case> cases{{1, 0, false, {3, 4}}, {0, 12, false, {4}}, {1, 12, false, {8, 9}}, {1, 0, true, {3, 9, 14}}};
		for (const auto &c : cases)
		{
			ModelEdit edit;
			edit.kind = ModelEditKind::WeightedTransform;
			edit.selection.vertices = c.vertices;
			edit.translation = {2, 3, 1};
			edit.rotation = {0, 0, 15};
			edit.scale = {1.25f, 1.25f, 1.25f};
			edit.pivotMode = c.individual ? ModelTransformPivot::IndividualOrigins : ModelTransformPivot::SelectionCentre;
			edit.tool.mirrorAxes = c.mirror;
			edit.tool.proportionalRadius = c.radius;
			auto committed = grid;
			expect(applyModelEdit(&committed, edit, nullptr, &error), "the weighted transform applies", error);
			ModelTransformPieceMap map;
			expect(modelWeightedTransformPieceMap(grid, 0, c.vertices, 0, c.individual, false, edit.tool, &map) && !map.weights.isEmpty(),
				   "the preview map builds");
			ModelTransform transform;
			transform.translation = edit.translation;
			transform.rotation = edit.rotation;
			transform.scale = edit.scale;
			expect(modelTransformPivot(grid.surfaces[0].frames[0].positions, c.vertices, ModelTransformPivot::SelectionCentre, {}, &transform.pivot),
				   "the preview pivot resolves");
			bool same = true, counterpartMoved = c.mirror == 0;
			for (int frame = 0; frame < 2; ++frame)
			{
				for (int v = 0; v < grid.surfaces[0].vertexCount; ++v)
				{
					const auto p = grid.surfaces[0].frames[frame].positions[v];
					const auto preview = c.vertices.contains(v) || map.moves(v) ? map.apply(transform, 0, v, p) : p;
					same = same && nearPoint(preview, committed.surfaces[0].frames[frame].positions[v], 1e-4);
					counterpartMoved = counterpartMoved || (map.weights.value(v).mirror == 1 && !nearPoint(preview, p, 1e-3));
				}
			}
			expect(same && counterpartMoved, "the gizmo preview equals the committed result, counterparts included");
		}
		// Vertices on the plane stay on it.
		ModelEdit edit;
		edit.kind = ModelEditKind::WeightedTransform;
		edit.selection.vertices = {2, 3};
		edit.translation = {3, 0, 0};
		edit.pivotMode = ModelTransformPivot::SelectionCentre;
		edit.tool.mirrorAxes = 1;
		auto mesh = grid;
		expect(applyModelEdit(&mesh, edit, nullptr, &error) && std::abs(mesh.surfaces[0].frames[0].positions[2].x) < 1e-5f &&
				   std::abs(mesh.surfaces[0].frames[0].positions[1].x + 11) < 1e-4f,
			   "a plane vertex stays on X = 0 while the counterpart of its neighbour moves the other way", error);
	}
	// CLI.
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (!root.isEmpty())
		QDir().mkpath(root);
	QTemporaryDir temporary(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("symmetry-XXXXXX"));
	ModelDocument document;
	const auto input = temporary.filePath("halves.mesh.json"), output = temporary.filePath("mirrored.mesh.json");
	expect(temporary.isValid() && document.setMesh(halves(), &error) && document.save(input, false, &error), "save CLI source", error);
	if (argc > 1)
	{
		const auto exe = QString::fromLocal8Bit(argv[1]);
		QJsonObject json;
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "extrude", "--faces", "6,7", "--distance", "4", "--symmetry", "x", "--output", output,
							  "--json"},
							 0, &json) &&
				   json.value("surface").toObject().value("triangles").toInt() == 32,
			   "model tool extrude --symmetry x mirrors the extrusion");
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "decimate", "--faces", "all", "--ratio", "0.5", "--symmetry", "x", "--overwrite",
							  "--output", output},
							 2),
			   "tools without symmetry refuse --symmetry");
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "extrude", "--faces", "6,7", "--distance", "4", "--symmetry", "xx", "--overwrite",
							  "--output", output},
							 2),
			   "a repeated symmetry axis is a usage error");
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "mirror", "--faces", "all", "--axis", "y", "--point", "0,8,0", "--overwrite", "--output",
							  output},
							 0),
			   "model tool mirror reflects the whole grid across y = 8");
	}
	std::cout << expect.checks << " symmetry checks; " << expect.failures << " failures\n";
	return expect.failures ? 1 : 0;
}
