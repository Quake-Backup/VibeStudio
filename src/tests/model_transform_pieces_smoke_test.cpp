// Individual Origins pivots and Normal transform axes: pieces, pivots, normal
// bases, the document Transform and Weighted Transform, whole surfaces, frame
// scope and the CLI.
#include "core/model_transform_axes.h"
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
// Three separate squares in the XY plane, centred at x = 0, 20 and 40.
ModelMesh threeSquares()
{
	QVector<ModelVec3> p;
	QVector<ModelTriangle> t;
	for (float x : {0.f, 20.f, 40.f})
		appendSquare(&p, &t, {x, 0, 0}, {1, 0, 0}, {0, 1, 0});
	return polygonMesh(p, t);
}
// A square facing +Z at the origin and one facing -Y at x = 20.
ModelMesh twoFacings()
{
	QVector<ModelVec3> p;
	QVector<ModelTriangle> t;
	appendSquare(&p, &t, {0, 0, 0}, {1, 0, 0}, {0, 1, 0});
	appendSquare(&p, &t, {20, 0, 0}, {1, 0, 0}, {0, 0, 1});
	return polygonMesh(p, t);
}
ModelEdit transformEdit(const QSet<int> &faces)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::Transform;
	edit.selection.faces = faces;
	edit.pivotMode = ModelTransformPivot::IndividualOrigins;
	return edit;
}
QVector<int> range(int first, int count)
{
	QVector<int> values;
	for (int i = 0; i < count; ++i)
		values.append(first + i);
	return values;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	{
		const auto mesh = threeSquares();
		const auto &surface = mesh.surfaces[0];
		QSet<int> vertices;
		for (int v = 0; v < surface.vertexCount; ++v)
			vertices.insert(v);
		QVector<ModelTransformPiece> pieces;
		expect(modelTransformPieces(surface, vertices, 0, &pieces, &error) && pieces.size() == 3 && pieces[0].vertices == range(0, 4) &&
				   nearPoint(pieces[1].pivot, {20, 0, 0}) && nearPoint(pieces[2].pivot, {40, 0, 0}),
			   "three separate squares are three pieces with their own centres", error);
		expect(modelTransformPieces(surface, vertices, 1, &pieces, &error) && nearPoint(pieces[0].pivot, {0, 0, 8}),
			   "pivots come from the pivot pose");
		expect(!modelTransformPieces(surface, {}, 0, &pieces, &error) && !error.isEmpty(), "an empty selection has no pieces");
		expect(modelTransformPieces(surface, {0, 2, 5}, 0, &pieces, &error) && pieces.size() == 2 && pieces[0].vertices == QVector<int>({0, 2}),
			   "selected vertices joined by a triangle edge form one piece; isolated ones their own");
	}
	{
		// Scale three squares by a half about their own centres.
		auto mesh = threeSquares();
		auto edit = transformEdit(allFaces(mesh.surfaces[0]));
		edit.scale = {.5f, .5f, .5f};
		const auto before = mesh;
		expect(applyModelEdit(&mesh, edit, nullptr, &error), "individual-origin scale applies", error);
		const auto &surface = mesh.surfaces[0];
		bool centres = true, halved = true, poses = true;
		for (int piece = 0; piece < 3; ++piece)
		{
			const auto vertices = range(piece * 4, 4);
			centres = centres && nearPoint(boundsCentre(surface, 0, vertices), {piece * 20.f, 0, 0});
			const auto a = surface.frames[0].positions[vertices[0]], c = surface.frames[0].positions[vertices[2]];
			halved = halved && std::abs((c.x - a.x) - 4) < 1e-4f;
			// The second pose scales about the same fixed pivot from the pivot pose.
			for (int v : vertices)
			{
				const auto p = before.surfaces[0].frames[1].positions[v];
				const ModelVec3 pivot{piece * 20.f, 0, 0};
				poses = poses && nearPoint(surface.frames[1].positions[v],
										   {pivot.x + (p.x - pivot.x) / 2, pivot.y + (p.y - pivot.y) / 2, pivot.z + (p.z - pivot.z) / 2});
			}
		}
		expect(centres && halved, "each square halves in place, keeping its centre");
		expect(poses, "every pose uses the fixed pivots of the pivot pose");
	}
	{
		// Rotate two "bolts" a quarter turn in place.
		auto mesh = threeSquares();
		auto edit = transformEdit({0, 1, 4, 5});
		edit.rotation = {0, 0, 90};
		expect(applyModelEdit(&mesh, edit, nullptr, &error), "individual-origin rotation applies", error);
		const auto &positions = mesh.surfaces[0].frames[0].positions;
		expect(nearPoint(positions[0], {4, -4, 0}) && nearPoint(positions[8], {44, -4, 0}) && nearPoint(positions[4], {16, -4, 0}),
			   "each selected square turns about its own centre and the unselected one stays");
	}
	{
		// Current-frame scope: pivots from that frame, other poses unchanged.
		auto mesh = threeSquares();
		const auto before = mesh;
		auto edit = transformEdit(allFaces(mesh.surfaces[0]));
		edit.frame = 1;
		edit.scale = {2, 2, 2};
		expect(applyModelEdit(&mesh, edit, nullptr, &error), "current-frame individual scale applies", error);
		const auto &surface = mesh.surfaces[0];
		expect(samePoints(surface.frames[0].positions, before.surfaces[0].frames[0].positions) &&
				   nearPoint(boundsCentre(surface, 1, range(4, 4)), {20, 0, 8}) && nearPoint(surface.frames[1].positions[4], {12, -8, 8}),
			   "a frame-local edit pivots each piece in its own pose and leaves the others");
	}
	{
		// Normal axes for a tilted square: Z is its normal, X its longest border edge.
		QVector<ModelVec3> p;
		QVector<ModelTriangle> t;
		const float c = std::sqrt(.5f);
		appendSquare(&p, &t, {0, 0, 0}, {1, 0, 0}, {0, c, c}, 8);
		p[2].x += 4; // stretch the +X side so one border edge is the longest
		auto mesh = polygonMesh(p, t);
		ModelEdit probe;
		probe.kind = ModelEditKind::Transform;
		probe.selection.faces = {0, 1};
		probe.transformSpace = ModelTransformSpace::Normal;
		ModelTransformBasis basis;
		expect(resolveModelTransformAxes(mesh, probe, &basis, &error) && !basis.world, "normal axes resolve for a tilted face set", error);
		expect(nearPoint(basis.axes[2], {0, -c, c}, 1e-3) && std::abs(basis.axes[0].x) > .9f,
			   "Z is the averaged face normal and X follows the longest border edge");
		probe.selection = {};
		probe.selection.vertices = {0};
		expect(resolveModelTransformAxes(mesh, probe, &basis, &error) && nearPoint(basis.axes[2], {0, 0, 1}) && nearPoint(basis.axes[0], {1, 0, 0}),
			   "a lone vertex uses its normal and the world axis least aligned with it");
		auto cube = polygonCube();
		probe.selection = {};
		probe.selection.faces = allFaces(cube.surfaces[0]);
		expect(resolveModelTransformAxes(cube, probe, &basis, &error) && basis.world, "a closed shape has no facing direction and keeps world axes");
		probe.axesFrame = 1;
		expect(resolveModelTransformAxes(cube, probe, &basis, &error), "normal axes accept a reference pose", error);
		probe.transformSpace = ModelTransformSpace::Custom;
		expect(!resolveModelTransformAxes(cube, probe, &basis, &error), "a reference pose still needs selection or normal axes");
	}
	{
		// Individual origins with normal axes: each piece moves along its own normal.
		auto mesh = twoFacings();
		auto edit = transformEdit(allFaces(mesh.surfaces[0]));
		edit.transformSpace = ModelTransformSpace::Normal;
		edit.axesFrame = 0;
		edit.translation = {0, 0, 2};
		const auto before = mesh;
		expect(applyModelEdit(&mesh, edit, nullptr, &error), "individual normal move applies", error);
		const auto &now = mesh.surfaces[0].frames[0].positions;
		const auto &was = before.surfaces[0].frames[0].positions;
		expect(nearPoint(now[0], {was[0].x, was[0].y, was[0].z + 2}) && nearPoint(now[4], {was[4].x, was[4].y - 2, was[4].z}),
			   "the +Z square rises and the -Y square moves towards -Y");
	}
	{
		// Weighted transform: the same pieces, and falloff follows the nearest piece.
		auto mesh = threeSquares();
		auto plain = mesh;
		auto edit = transformEdit(allFaces(mesh.surfaces[0]));
		edit.scale = {.5f, .5f, .5f};
		auto weighted = edit;
		weighted.kind = ModelEditKind::WeightedTransform;
		expect(applyModelEdit(&plain, edit, nullptr, &error) && applyModelEdit(&mesh, weighted, nullptr, &error) &&
				   samePoses(mesh.surfaces[0], plain.surfaces[0]),
			   "a weighted transform without falloff matches the plain transform", error);
		mesh = threeSquares();
		weighted.selection.faces = {0, 1};
		weighted.tool.proportionalRadius = 12.5;
		weighted.tool.falloff = ModelFalloff::Constant;
		expect(applyModelEdit(&mesh, weighted, nullptr, &error), "individual-origin proportional scale applies", error);
		// Square 1's corner (16,-4) is within 12 of square 0's corner (4,-4) and
		// scales by a half towards square 0's centre.
		expect(nearPoint(mesh.surfaces[0].frames[0].positions[4], {8, -2, 0}), "a falloff vertex follows its nearest piece's pivot");
	}
	{
		// Whole surfaces: each surface about its own centre.
		auto mesh = threeSquares();
		ModelEdit separate;
		separate.kind = ModelEditKind::SeparateFaces;
		separate.selection.faces = {4, 5};
		separate.text = QStringLiteral("bolt");
		expect(applyModelEdit(&mesh, separate, nullptr, &error) && mesh.surfaces.size() == 2, "separate one square into its own surface", error);
		ModelEdit edit;
		edit.kind = ModelEditKind::Transform;
		edit.selection.surface = 0;
		edit.selection.surfaces = {0, 1};
		edit.pivotMode = ModelTransformPivot::IndividualOrigins;
		edit.scale = {.5f, .5f, .5f};
		const auto first = boundsCentre(mesh.surfaces[0], 0, range(0, mesh.surfaces[0].vertexCount));
		const auto second = boundsCentre(mesh.surfaces[1], 0, range(0, mesh.surfaces[1].vertexCount));
		expect(applyModelEdit(&mesh, edit, nullptr, &error) &&
				   nearPoint(boundsCentre(mesh.surfaces[0], 0, range(0, mesh.surfaces[0].vertexCount)), first) &&
				   nearPoint(boundsCentre(mesh.surfaces[1], 0, range(0, mesh.surfaces[1].vertexCount)), second),
			   "whole surfaces scale about their own centres", error);
	}
	{
		// The preview map matches the document transform.
		const auto mesh = twoFacings();
		ModelTransformPieceMap map;
		QSet<int> vertices;
		for (int v = 0; v < 8; ++v)
			vertices.insert(v);
		expect(modelTransformPieceMap(mesh, 0, vertices, {}, 0, true, &map) && map.pieces.size() == 2 && map.pieces[1].ownAxes,
			   "the preview map holds each piece and its own normal axes");
		ModelTransform transform;
		transform.translation = {0, 0, 2};
		transform.basis.world = false;
		const auto moved = transformModelPoint(mesh.surfaces[0].frames[0].positions[4], map.transformFor(transform, 0, 4));
		expect(nearPoint(moved, {16, -2, -4}), "the preview moves a piece along its own normal");
	}
	// CLI: model tool and model edit.
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (!root.isEmpty())
		QDir().mkpath(root);
	QTemporaryDir temporary(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("pieces-XXXXXX"));
	ModelDocument document;
	const auto input = temporary.filePath("squares.mesh.json"), output = temporary.filePath("scaled.mesh.json");
	expect(temporary.isValid() && document.setMesh(threeSquares(), &error) && document.save(input, false, &error), "save CLI source", error);
	if (argc > 1)
	{
		const auto exe = QString::fromLocal8Bit(argv[1]);
		QJsonObject json;
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "transform", "--faces", "all", "--scale", "0.5,0.5,0.5", "--pivot-mode",
							  "individual", "--output", output, "--json"},
							 0, &json),
			   "model tool transform accepts --pivot-mode individual");
		ModelDocument loaded;
		expect(loaded.load(output, &error) && nearPoint(boundsCentre(loaded.mesh().surfaces[0], 0, range(4, 4)), {20, 0, 0}) &&
				   nearPoint(loaded.mesh().surfaces[0].frames[0].positions[4], {18, -2, 0}),
			   "the CLI result scales each square in place", error);
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "transform", "--faces", "all", "--offset", "0,0,2", "--pivot-mode", "individual",
							  "--transform-space", "normal", "--overwrite", "--output", output, "--json"},
							 0),
			   "model tool transform accepts --transform-space normal");
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "edit", input, "--operation", "transform", "--faces", "all", "--scale", "0.5,0.5,0.5", "--pivot-mode",
							  "individual", "--transform-space", "normal", "--axes-frame", "1", "--overwrite", "--output", output, "--json"},
							 0),
			   "model edit transform accepts individual origins and normal axes");
		expect(loaded.load(output, &error) && nearPoint(boundsCentre(loaded.mesh().surfaces[0], 0, range(8, 4)), {40, 0, 0}),
			   "model edit scales each square in place", error);
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "transform", "--faces", "all", "--scale", "2,2,2", "--pivot-mode", "each",
							  "--overwrite", "--output", output},
							 2),
			   "an unknown pivot mode is a usage error");
	}
	std::cout << expect.checks << " individual origin checks; " << expect.failures << " failures\n";
	return expect.failures ? 1 : 0;
}
