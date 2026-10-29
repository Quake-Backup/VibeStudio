// Branched rips: T and Y junctions, a closed loop with one branch, filled
// gaps, the opposite side, zero-distance splits that persist, every pose, a
// skinned surface, a refused bow-tie vertex and the CLI.
#include "core/model_mesh_tools.h"
#include "tests/model_polygon_test_helpers.h"
#include "tests/model_rip_test_helpers.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
PolygonChecks expect;
// Indexed copies at (x, y) in the first pose that faces use, and their identities.
int copiesAt(const ModelSurface &surface, ModelVec3 at, QSet<int> *identities = nullptr)
{
	QSet<int> used;
	for (const auto &t : surface.triangles)
		used << t.a << t.b << t.c;
	int count = 0;
	for (int v : std::as_const(used))
	{
		if (!nearPoint(surface.frames[0].positions[v], at, 1e-4))
			continue;
		++count;
		if (identities)
			identities->insert(surface.topologyVertices.value(v, -1));
	}
	return count;
}
int boundaryEdges(const ModelSurface &surface)
{
	ModelGeometricTopology topology;
	buildModelGeometricTopology(surface, &topology);
	int count = 0;
	for (const auto &faces : topology.edgeFaces)
		count += faces.size() == 1;
	return count;
}
ModelEdge edgeAt(const ModelMesh &mesh, float ax, float ay, float bx, float by)
{
	return modelEdge(ripVertex(mesh, ax, ay), ripVertex(mesh, bx, by));
}
// The face touching (x, y) whose centre lies towards (dx, dy) from it.
int sideFace(const ModelMesh &mesh, float x, float y, float dx, float dy)
{
	const auto &surface = mesh.surfaces[0];
	const int centre = ripVertex(mesh, x, y);
	int best = -1;
	double bestScore = -1e9;
	for (int f = 0; f < surface.triangles.size(); ++f)
	{
		const auto t = surface.triangles[f];
		if (t.a != centre && t.b != centre && t.c != centre)
			continue;
		const auto &p = surface.frames[0].positions;
		const double cx = (p[t.a].x + p[t.b].x + p[t.c].x) / 3 - x, cy = (p[t.a].y + p[t.b].y + p[t.c].y) / 3 - y;
		const double score = (cx * dx + cy * dy) / std::max(1e-9, std::hypot(cx, cy));
		if (score > bestScore)
		{
			bestScore = score;
			best = f;
		}
	}
	return best;
}
// A T: the horizontal chain (-16,0)-(0,0)-(16,0) and a branch down to (0,-16),
// on a 5x5 grid (-32..32), with the side face above the junction.
ModelEdit tEdit(const ModelMesh &mesh, ModelVec3 offset = {0, 4, 0}, bool fill = false)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::Rip;
	edit.selection.edges = {edgeAt(mesh, -16, 0, 0, 0), edgeAt(mesh, 0, 0, 16, 0), edgeAt(mesh, 0, 0, 0, -16)};
	edit.translation = offset;
	edit.tool.ripFill = fill;
	edit.tool.ripFace = sideFace(mesh, 0, 0, 0, 1);
	return edit;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	const auto grid = ripGrid(5);
	expect(validateEditableModel(grid).isEmpty() && grid.surfaces[0].triangles.size() == 32, "valid two-pose 5x5 grid");
	const int baseBoundary = boundaryEdges(grid.surfaces[0]);
	{
		// T junction: the upper side moves; the branch splits its two lower
		// sectors in place.
		auto mesh = grid;
		ModelSelection result;
		ModelEditReport report;
		expect(applyModelEdit(&mesh, tEdit(grid), &result, &error, {}, &report), "a T-shaped rip applies", error);
		const auto &surface = mesh.surfaces[0];
		QSet<int> junction;
		expect(copiesAt(surface, {0, 0, 0}, &junction) == 2 && copiesAt(surface, {0, 4, 0}, &junction) == 1 && junction.size() == 3,
			   "the junction becomes three vertices with their own identities: one moved, one per lower sector");
		QSet<int> branch;
		expect(copiesAt(surface, {0, -16, 0}, &branch) == 2 && branch.size() == 2 && copiesAt(surface, {16, 4, 0}) == 1 &&
				   copiesAt(surface, {16, 0, 0}) == 1,
			   "the branch splits without moving; chain ends move with the upper side");
		const int boundary = boundaryEdges(surface);
		expect(boundary == baseBoundary + 12, "every selected edge and its extension opens",
			   QStringLiteral("%1 boundary edges").arg(boundary));
		expect(result.edges.size() == 2 && validateEditableModel(mesh).isEmpty() && consistentlyWound(surface, true),
			   "the moved chain edges stay selected; the result is valid and consistently wound");
		bool raised = false;
		for (int v = 0; v < surface.vertexCount; ++v)
			raised = raised || (nearPoint(surface.frames[0].positions[v], {0, 4, 0}) && nearPoint(surface.frames[1].positions[v], {0, 4, 0}));
		expect(raised && report.sourceFaces.value(0).size() == surface.triangles.size(), "the moved junction copy moves in every pose");
	}
	{
		// Fill: strips along the moved chain and its extensions; nothing across
		// the unmoved branch.
		auto mesh = grid;
		expect(applyModelEdit(&mesh, tEdit(grid, {0, 4, 0}, true), nullptr, &error) && mesh.surfaces[0].triangles.size() == 32 + 6 &&
				   validateEditableModel(mesh).isEmpty() && consistentlyWound(mesh.surfaces[0], true),
			   "a filled T adds two strips and two extension triangles", error);
		// The opposite side: both lower sectors move as separate copies.
		auto opposite = grid;
		auto edit = tEdit(grid, {0, -4, 0});
		edit.tool.ripOpposite = true;
		ModelSelection result;
		expect(applyModelEdit(&opposite, edit, &result, &error) && validateEditableModel(opposite).isEmpty() &&
				   copiesAt(opposite.surfaces[0], {0, -4, 0}) == 2 && copiesAt(opposite.surfaces[0], {0, 0, 0}) == 1,
			   "the opposite side moves both lower sectors as separate copies", error);
		// Fill strips have their own corners; only the chain against the upper
		// side is filled.
		auto filled = grid;
		edit.tool.ripFill = true;
		expect(applyModelEdit(&filled, edit, nullptr, &error) && filled.surfaces[0].triangles.size() == 32 + 6 &&
				   copiesAt(filled.surfaces[0], {0, -4, 0}) == 2 + 2,
			   "filling the opposite side adds strips only against the upper side", error);
	}
	{
		// Y junction through an explicitly selected diagonal.
		auto mesh = grid;
		ModelEdit edit;
		edit.kind = ModelEditKind::Rip;
		edit.selection.edges = {edgeAt(grid, 0, 0, 16, 16), edgeAt(grid, 0, 0, -16, 0), edgeAt(grid, 0, 0, 0, -16)};
		edit.translation = {-3, -3, 0};
		edit.tool.ripFace = sideFace(grid, 0, 0, -1, -1);
		QSet<int> junction;
		expect(applyModelEdit(&mesh, edit, nullptr, &error) && validateEditableModel(mesh).isEmpty() &&
				   copiesAt(mesh.surfaces[0], {-3, -3, 0}, &junction) == 1 && copiesAt(mesh.surfaces[0], {0, 0, 0}, &junction) == 2 &&
				   junction.size() == 3,
			   "a Y-shaped rip splits the junction into three sectors", error);
	}
	{
		// A closed loop with one branch to the border: the inside rises; the
		// outside splits at the branch, though it is connected around the loop.
		auto mesh = grid;
		ModelEdit edit;
		edit.kind = ModelEditKind::Rip;
		const float loop[][2]{{-16, -16}, {0, -16}, {16, -16}, {16, 0}, {16, 16}, {0, 16}, {-16, 16}, {-16, 0}};
		for (int i = 0; i < 8; ++i)
			edit.selection.edges.insert(edgeAt(grid, loop[i][0], loop[i][1], loop[(i + 1) % 8][0], loop[(i + 1) % 8][1]));
		edit.selection.edges.insert(edgeAt(grid, 16, 0, 32, 0));
		edit.translation = {0, 0, 4};
		edit.tool.ripFace = sideFace(grid, 16, 0, -1, 0);
		auto filled = mesh;
		expect(applyModelEdit(&mesh, edit, nullptr, &error) && validateEditableModel(mesh).isEmpty(), "a loop with a branch rips", error);
		QSet<int> junction;
		expect(copiesAt(mesh.surfaces[0], {16, 0, 0}, &junction) == 2 && copiesAt(mesh.surfaces[0], {16, 0, 4}, &junction) == 1 &&
				   junction.size() == 3 && copiesAt(mesh.surfaces[0], {32, 0, 0}) == 2,
			   "the branch junction has a raised inside copy and one copy each side of the branch");
		edit.tool.ripFill = true;
		expect(applyModelEdit(&filled, edit, nullptr, &error) && filled.surfaces[0].triangles.size() == 32 + 16 &&
				   consistentlyWound(filled.surfaces[0], true),
			   "filling the loop adds a strip of two triangles per loop edge", error);
	}
	{
		// Zero distance: a persistent split with no movement.
		auto mesh = grid;
		expect(applyModelEdit(&mesh, tEdit(grid, {}), nullptr, &error) && mesh.surfaces[0].vertexCount > grid.surfaces[0].vertexCount &&
				   boundaryEdges(mesh.surfaces[0]) > baseBoundary,
			   "a zero-distance T rip splits in place", error);
		ModelMesh restored;
		const auto json = QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
		expect(parseEditableModel(json, &restored, &error) && boundaryEdges(restored.surfaces[0]) == boundaryEdges(mesh.surfaces[0]),
			   "the split survives saving and loading", error);
		auto filled = grid;
		const auto before = ripBytes(filled);
		expect(!applyModelEdit(&filled, tEdit(grid, {}, true), nullptr, &error) && ripBytes(filled) == before,
			   "Rip and Fill still needs movement");
	}
	{
		// A skinned surface: the moved copies keep their joints and weights.
		const auto rig = ripRig();
		expect(rig.isValid(), "skinned 3x3 fixture");
		auto mesh = rig;
		ModelEdit edit;
		edit.kind = ModelEditKind::Rip;
		// Baked positions are not exact, so components come from the unbaked grid.
		const auto small = ripGrid();
		edit.selection.edges = {edgeAt(small, -16, 0, 0, 0), edgeAt(small, 0, 0, 16, 0), edgeAt(small, 0, 0, 0, -16)};
		edit.translation = {0, 3, 0};
		edit.tool.ripFace = sideFace(small, 0, 0, 0, 1);
		expect(applyModelEdit(&mesh, edit, nullptr, &error) && validateEditableModel(mesh).isEmpty(), "a branched rip on a skinned surface", error);
		auto baked = mesh;
		bool same = bakeModelSkeleton(&baked, {}, &error);
		const auto &s = mesh.surfaces[0];
		for (int v = 0; same && v < s.vertexCount; ++v)
			same = nearPoint(s.frames[0].positions[v], baked.surfaces[0].frames[0].positions[v], 1e-3) && s.skinning.count[v] == 2;
		expect(same, "rebaking reproduces the ripped positions from the preserved weights", error);
	}
	{
		// A bow-tie vertex on the branched chain is refused unchanged.
		auto mesh = grid;
		auto &surface = mesh.surfaces[0];
		// Remove the cells north-west (2,3) and south-east (3,2) of (16,16).
		for (int f : {29, 28, 23, 22})
			surface.triangles.removeAt(f);
		updateEditableModelMetadata(&mesh);
		ModelEdit edit;
		edit.kind = ModelEditKind::Rip;
		edit.selection.edges = {edgeAt(mesh, 0, 0, 16, 16), edgeAt(mesh, 16, 16, 32, 32), edgeAt(mesh, 0, 0, -16, 0), edgeAt(mesh, 0, 0, 0, -16)};
		edit.translation = {0, 2, 0};
		edit.tool.ripFace = sideFace(mesh, 0, 0, -1, 1);
		const auto before = ripBytes(mesh);
		expect(!applyModelEdit(&mesh, edit, nullptr, &error) && ripBytes(mesh) == before && error.contains(QStringLiteral("fan")),
			   "a bow-tie vertex in a branched rip is refused", error);
	}
	// CLI.
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (!root.isEmpty())
		QDir().mkpath(root);
	QTemporaryDir temporary(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("rip-branch-XXXXXX"));
	ModelDocument document;
	const auto input = temporary.filePath("grid.mesh.json"), output = temporary.filePath("ripped.mesh.json");
	expect(temporary.isValid() && document.setMesh(grid, &error) && document.save(input, false, &error), "save CLI source", error);
	if (argc > 1)
	{
		const auto edit = tEdit(grid, {0, 4, 0}, true);
		QStringList edges;
		for (const auto edge : edit.selection.edges)
			edges << QStringLiteral("%1:%2").arg(edge.first).arg(edge.second);
		ModelDocument expected;
		expect(expected.setMesh(grid, &error) && expected.edit(edit, &error), "prepare the CLI result through the document service", error);
		QJsonObject json;
		expect(runPolygonCli(QString::fromLocal8Bit(argv[1]), temporary.path(),
							 {"model", "tool", input, "--tool", "rip", "--edges", edges.join(QLatin1Char(',')), "--side-face",
							  QString::number(edit.tool.ripFace), "--offset", "0,4,0", "--fill", "--output", output, "--json"},
							 0, &json),
			   "model tool rip takes a branched edge selection");
		ModelDocument loaded;
		expect(loaded.load(output, &error) && ripBytes(loaded.mesh()) == ripBytes(expected.mesh()), "the CLI writes the same branched rip", error);
	}
	std::cout << expect.checks << " branched rip checks; " << expect.failures << " failures\n";
	return expect.failures ? 1 : 0;
}
