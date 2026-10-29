// Edge bevel junctions and width clamping: a cube corner with three bevelled
// edges at one to three segments, every edge of a cube, clamping an over-wide
// bevel to the widest that fits (with a note), refusals and the CLI.
#include "tests/model_edge_bevel_test_helpers.h"
#include "tests/model_polygon_test_helpers.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
PolygonChecks expect;
}
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	const auto cube = bevelCube();
	for (int segments = 1; segments <= 3; ++segments)
	{
		auto mesh = cube;
		auto edit = bevelCubeEdit(mesh, 3, segments);
		edit.tool.bevelWidth = 4;
		ModelSelection result;
		expect(applyModelEdit(&mesh, edit, &result, &error), "three bevelled edges meeting at a cube corner apply", error);
		expect(consistentlyWound(mesh.surfaces[0]) && !result.faces.isEmpty(), "the corner patch closes the cube, consistently wound");
	}
	{
		auto mesh = cube;
		auto edit = bevelCubeEdit(mesh, 12, 2);
		edit.tool.bevelWidth = 4;
		expect(applyModelEdit(&mesh, edit, nullptr, &error) && consistentlyWound(mesh.surfaces[0]), "every cube edge bevels with corner patches",
			   error);
	}
	{
		// Clamping.
		auto mesh = cube;
		auto edit = bevelCubeEdit(mesh, 3, 2);
		edit.tool.bevelWidth = 1000;
		const auto before = QJsonDocument(editableModelJson(mesh)).toJson();
		expect(!applyModelEdit(&mesh, edit, nullptr, &error) && QJsonDocument(editableModelJson(mesh)).toJson() == before,
			   "an over-wide bevel is refused without clamping");
		edit.tool.bevelClamp = true;
		ModelEditReport report;
		expect(applyModelEdit(&mesh, edit, nullptr, &error, {}, &report) && consistentlyWound(mesh.surfaces[0]), "clamping fits the bevel", error);
		expect(report.notes.size() == 1 && report.notes.first().contains(QStringLiteral("clamped")), "the clamped width is reported");
		auto fits = cube;
		auto small = bevelCubeEdit(fits, 3, 2);
		small.tool.bevelWidth = 2;
		small.tool.bevelClamp = true;
		ModelEditReport quiet;
		auto plain = cube;
		auto unclamped = small;
		unclamped.tool.bevelClamp = false;
		expect(applyModelEdit(&fits, small, nullptr, &error, {}, &quiet) && applyModelEdit(&plain, unclamped, nullptr, &error) && quiet.notes.isEmpty() &&
				   QJsonDocument(editableModelJson(fits)).toJson() == QJsonDocument(editableModelJson(plain)).toJson(),
			   "a width that fits is used unchanged and says nothing");
		ModelEdit stray;
		stray.kind = ModelEditKind::BevelVertices;
		stray.selection.vertices = {0};
		stray.tool.bevelClamp = true;
		auto other = cube;
		expect(!applyModelEdit(&other, stray, nullptr, &error), "clamping applies only to edge bevels");
	}
	// CLI.
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	QTemporaryDir temporary(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("bevel-clamp-XXXXXX"));
	ModelDocument document;
	const auto input = temporary.filePath("cube.mesh.json"), output = temporary.filePath("bevelled.mesh.json");
	expect(temporary.isValid() && document.setMesh(cube, &error) && document.save(input, false, &error), "save CLI source", error);
	if (argc > 1)
	{
		const auto exe = QString::fromLocal8Bit(argv[1]);
		QStringList edges;
		for (const auto edge : bevelCubeEdit(cube, 3, 1).selection.edges)
			edges << QStringLiteral("%1:%2").arg(edge.first).arg(edge.second);
		QJsonObject json;
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "bevel-edges", "--edges", edges.join(QLatin1Char(',')), "--width", "1000", "--clamp",
							  "--output", output, "--json"},
							 0, &json) &&
				   json.value("notes").toArray().size() == 1,
			   "model tool bevel-edges --clamp reports the clamped width");
		expect(runPolygonCli(exe, temporary.path(),
							 {"model", "tool", input, "--tool", "bevel-edges", "--edges", edges.join(QLatin1Char(',')), "--width", "1000", "--overwrite",
							  "--output", output},
							 4),
			   "without --clamp the over-wide bevel is refused");
	}
	std::cout << expect.checks << " bevel junction and clamp checks; " << expect.failures << " failures\n";
	return expect.failures ? 1 : 0;
}
