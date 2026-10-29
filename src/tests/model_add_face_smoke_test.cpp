// Add Face: new corners in every pose, winding from a neighbour or the facing
// direction, refusals, capacity and the CLI.
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
ModelEdit addFace(const QSet<int> &vertices, const QVector<std::array<double, 3>> &points)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::AddFace;
	edit.selection.vertices = vertices;
	edit.tool.facePointCount = int(points.size());
	for (int i = 0; i < points.size(); ++i)
		edit.tool.facePoints[i] = points[i];
	return edit;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	{
		// Three new points beside a square, in a three-pose model.
		QVector<ModelVec3> p;
		QVector<ModelTriangle> t;
		appendSquare(&p, &t, {0, 0, 0}, {1, 0, 0}, {0, 1, 0});
		auto mesh = polygonMesh(p, t, 3);
		ModelSelection result;
		expect(applyModelEdit(&mesh, addFace({}, {{10, 0, 0}, {20, 0, 0}, {10, 10, 0}}), &result, &error), "a face from three new points applies",
			   error);
		const auto &surface = mesh.surfaces[0];
		expect(surface.vertexCount == 7 && surface.triangles.size() == 3 && result.faces == QSet<int>{2}, "three new vertices and one selected face");
		bool everyPose = true;
		for (const auto &frame : surface.frames)
			everyPose = everyPose && nearPoint(frame.positions[4], {10, 0, 0}) && nearPoint(frame.positions[6], {10, 10, 0});
		expect(everyPose, "new corners sit at the same place in every pose");
		const auto &face = surface.triangles[2];
		const auto a = surface.frames[0].positions[face.a], b = surface.frames[0].positions[face.b], c = surface.frames[0].positions[face.c];
		expect((double(b.x) - a.x) * (double(c.y) - a.y) - (double(b.y) - a.y) * (double(c.x) - a.x) > 0, "an isolated face turns towards +Z by default");
		expect(surface.frames[0].normals[4].z > .99f && uvArea(surface, 2) > 0, "new corners get the face normal and planar UVs");
	}
	{
		// A border edge plus one point follows its neighbour's winding.
		QVector<ModelVec3> p;
		QVector<ModelTriangle> t;
		appendSquare(&p, &t, {4, 4, 0}, {1, 0, 0}, {0, 1, 0});
		auto mesh = polygonMesh(p, t);
		auto edit = addFace({1, 2}, {{12, 4, 0}});
		edit.tool.normal = {0, 0, -1}; // the neighbour wins over the facing
		expect(applyModelEdit(&mesh, edit, nullptr, &error), "an edge and a point make a face", error);
		expect(consistentlyWound(mesh.surfaces[0], true) && mesh.surfaces[0].triangles.size() == 3, "the face is wound with its neighbour");
		auto again = mesh;
		const auto before = bytes(again);
		expect(!applyModelEdit(&again, addFace({1, 2, 4}, {}), nullptr, &error) && bytes(again) == before, "a face that already exists is refused");
		auto twoSided = mesh;
		expect(!applyModelEdit(&twoSided, addFace({0, 1}, {{4, -8, 0}}), nullptr, &error) || consistentlyWound(twoSided.surfaces[0], true),
			   "a face never makes winding inconsistent");
	}
	{
		// Refusals.
		auto mesh = polygonGrid(1, 1);
		const auto before = bytes(mesh);
		expect(!applyModelEdit(&mesh, addFace({}, {{0, 0, 5}, {1, 1, 5}, {2, 2, 5}}), nullptr, &error) && bytes(mesh) == before,
			   "collinear corners are refused");
		expect(!applyModelEdit(&mesh, addFace({}, {{0, 0, 5}, {0, 0, 5}, {2, 0, 5}}), nullptr, &error), "coincident corners are refused");
		expect(!applyModelEdit(&mesh, addFace({0}, {{0, 0, 0}, {0, 8, 5}}), nullptr, &error), "a new point on a selected corner is refused");
		expect(!applyModelEdit(&mesh, addFace({0, 1}, {}), nullptr, &error), "two corners are not enough");
		ModelEdit stray;
		stray.kind = ModelEditKind::PokeFaces;
		stray.selection.faces = {0};
		stray.tool.facePointCount = 1;
		expect(!applyModelEdit(&mesh, stray, nullptr, &error), "face points apply only to Add Face");
		// Capacity: a surface at the vertex limit cannot take new corners.
		auto full = polygonGrid(1, 1, 8, 1);
		auto &surface = full.surfaces[0];
		while (surface.vertexCount < modelDocumentMaxVertices - 1)
		{
			surface.texCoords.append({0, 0});
			surface.frames[0].positions.append({0, 0, 0});
			surface.frames[0].normals.append({0, 0, 1});
			++surface.vertexCount;
		}
		updateEditableModelMetadata(&full);
		expect(!applyModelEdit(&full, addFace({}, {{0, 0, 5}, {1, 0, 5}, {0, 1, 5}}), nullptr, &error) && error.contains(QStringLiteral("limit")),
			   "new corners respect the vertex limit", error);
	}
	// CLI.
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	QTemporaryDir temporary(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("add-face-XXXXXX"));
	ModelDocument document;
	const auto input = temporary.filePath("square.mesh.json"), output = temporary.filePath("added.mesh.json");
	expect(temporary.isValid() && document.setMesh(polygonGrid(1, 1), &error) && document.save(input, false, &error), "save CLI source", error);
	if (argc > 1)
	{
		const auto exe = QString::fromLocal8Bit(argv[1]);
		QJsonObject json;
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "add-face", "--points", "20,0,0;30,0,0;20,10,0", "--output", output, "--json"}, 0,
							 &json) &&
				   json.value("surface").toObject().value("vertices").toInt() == 7 &&
				   json.value("selection").toObject().value("faces").toArray().size() == 1,
			   "model tool add-face from three points");
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "add-face", "--vertices", "1,3", "--point", "16,0,0", "--overwrite", "--output", output},
							 0),
			   "model tool add-face from an edge and a point");
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "add-face", "--points", "0,0,0;1,1,1", "--overwrite", "--output", output}, 4),
			   "two corners are refused");
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "add-face", "--points", "0,0;1,1,1;2,2,2", "--overwrite", "--output", output}, 2),
			   "a malformed point is a usage error");
	}
	std::cout << expect.checks << " add face checks; " << expect.failures << " failures\n";
	return expect.failures ? 1 : 0;
}
