#include "core/idtech_image.h"
#include "core/model_document.h"
#include "core/model_mdl.h"
#include "tests/model_mdl_test_helpers.h"
#include "tests/model_uv_islands_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *message, const QString &detail = {})
{
	++checks;
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << message << ": " << qPrintable(detail) << '\n';
	}
	return value;
}
// An original octahedron MDL (public IDPO layout): tips at +-127/16 units on
// each axis, one 64 x 32 skin of index 0, one frame, every face front.
QByteArray octahedronMdl(int skinWidth = 64)
{
	QByteArray bytes("IDPO");
	mdlInteger(bytes, 6);
	for (float value : {0.0625f, 0.0625f, 0.0625f, -8.f, -8.f, -8.f, 8.f, 0.f, 0.f, 4.f})
		mdlNumber(bytes, value);
	for (qint32 value : {1, skinWidth, 32, 6, 8, 2, 0, 0})
		mdlInteger(bytes, value);
	mdlNumber(bytes, 8.f);
	mdlInteger(bytes, 0);
	bytes.append(QByteArray(skinWidth * 32, char(0)));
	for (int v = 0; v < 6; ++v)
	{
		mdlInteger(bytes, 0);
		mdlInteger(bytes, 4 + v * 8);
		mdlInteger(bytes, 2 + v * 4);
	}
	// +X 0, -X 1, +Y 2, -Y 3, +Z 4, -Z 5; outward winding.
	const int faces[8][3]{{0, 2, 4}, {0, 4, 3}, {0, 3, 5}, {0, 5, 2}, {1, 4, 2}, {1, 3, 4}, {1, 5, 3}, {1, 2, 5}};
	for (const auto &face : faces)
	{
		mdlInteger(bytes, 1);
		for (int v : face)
			mdlInteger(bytes, v);
	}
	const unsigned char packed[6][3]{{255, 128, 128}, {1, 128, 128}, {128, 255, 128}, {128, 1, 128}, {128, 128, 255}, {128, 128, 1}};
	const ModelVec3 directions[6]{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	for (int frame = 0; frame < 2; ++frame)
	{
		mdlInteger(bytes, 0);
		bytes += QByteArray::fromHex("01010100ffffff00");
		QByteArray name(16, '\0');
		name.replace(0, 5, frame ? "pose2" : "pose1");
		bytes += name;
		for (int v = 0; v < 6; ++v)
		{
			const auto &p = packed[v];
			bytes.append(char(p[0]));
			bytes.append(char(p[1]));
			bytes.append(char(frame ? std::min(255, p[2] + 8) : p[2]));
			bytes.append(char(modelAliasNormalIndex(directions[v])));
		}
	}
	return bytes;
}
ModelEdit frontBack(const ModelMesh &mesh, int axis)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::ProjectUvMapping;
	for (int f = 0; f < mesh.surfaces[0].triangles.size(); ++f)
		edit.selection.faces.insert(f);
	edit.tool.uvProjection = ModelUvProjection::QuakeFrontBack;
	edit.tool.axis = axis;
	return edit;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	auto palette = generatedIdTechPalette("quake");
	ModelMesh mesh;
	expect(importEditableModel("octahedron.mdl", octahedronMdl(), &mesh, &error, &palette) && mesh.mdl.enabled &&
			   mesh.mdl.skinSize == QSize(64, 32) && mesh.surfaces[0].vertexCount == 6,
		   "import the octahedron fixture", error);
	const auto original = mesh;
	expect(applyModelEdit(&mesh, frontBack(mesh, 0), nullptr, &error), "lay out the front and back halves", error);
	const auto &s = mesh.surfaces[0];
	expect(s.vertexCount == 10 && uvSamePoses(original.surfaces[0], s), "silhouette corners split into front and back copies; poses survive");
	int pairs = 0;
	for (int a = 0; a < s.vertexCount; ++a)
		for (int b = 0; b < s.vertexCount; ++b)
			if (a != b && s.frames[0].positions[a].x == s.frames[0].positions[b].x && s.frames[0].positions[a].y == s.frames[0].positions[b].y &&
				s.frames[0].positions[a].z == s.frames[0].positions[b].z && s.texCoords[b].v == s.texCoords[a].v &&
				std::lround(double(s.texCoords[b].u) * 64 - .5) == std::lround(double(s.texCoords[a].u) * 64 - .5) + 32)
				++pairs;
	expect(pairs == 4, "each silhouette back corner sits exactly half the skin to the right of its front corner");
	bool centred = true, halves = true;
	for (int f = 0; f < s.triangles.size(); ++f)
	{
		const auto t = s.triangles[f];
		const bool back = f >= 4;
		for (int v : {t.a, t.b, t.c})
		{
			const double x = double(s.texCoords[v].u) * 64 - .5, y = double(s.texCoords[v].v) * 32 - .5;
			centred = centred && std::abs(x - std::round(x)) < 1e-4 && std::abs(y - std::round(y)) < 1e-4;
			halves = halves && (back ? s.texCoords[v].u > .5f : s.texCoords[v].u < .5f);
		}
	}
	expect(centred && halves, "front faces fill the left half and back faces the right, on texel centres");
	int stored = 0, render = 0;
	expect(modelMdlVertexCounts(mesh, &stored, &render, &error) && stored == 6 && render == 10, "the MDL stores 6 vertices, 4 on the seam",
		   QStringLiteral("%1 %2").arg(stored).arg(render));
	ModelExportReport report;
	const auto bytes = exportEditableModel(mesh, "mdl", 0, &error, {}, &report);
	expect(!bytes.isEmpty() && report.storedVertices == 6 && report.storedVertices < s.vertexCount && report.maxUvError == 0,
		   "export pairs the silhouette as onseam vertices", error);
	ModelMesh reloaded;
	expect(importEditableModel("roundtrip.mdl", bytes, &reloaded, &error, &palette), "re-import the export", error);
	bool same = reloaded.surfaces[0].triangles.size() == s.triangles.size();
	for (int f = 0; same && f < s.triangles.size(); ++f)
	{
		const auto a = s.triangles[f], b = reloaded.surfaces[0].triangles[f];
		const int x[]{a.a, a.b, a.c}, y[]{b.a, b.b, b.c};
		for (int c = 0; c < 3; ++c)
			same = same && std::abs(s.texCoords[x[c]].u - reloaded.surfaces[0].texCoords[y[c]].u) <= .5f / 64 &&
				   std::abs(s.texCoords[x[c]].v - reloaded.surfaces[0].texCoords[y[c]].v) <= .5f / 32;
	}
	expect(same, "re-imported UVs equal the authored ones within half a texel");
	// Quake loads skin widths in multiples of 4.
	auto odd = uvSquare();
	odd.tags.clear();
	odd.md2SkinSize = {30, 16};
	const auto before = QJsonDocument(editableModelJson(odd)).toJson();
	expect(!applyModelEdit(&odd, frontBack(odd, 0), nullptr, &error) && error.contains(QStringLiteral("4")) &&
			   QJsonDocument(editableModelJson(odd)).toJson() == before,
		   "a skin width that is not a multiple of 4 is refused", error);
	// CLI: model tool reports the predicted native counts.
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	QTemporaryDir temp(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("uv-frontback-XXXXXX"));
	if (argc > 1 && temp.isValid())
	{
		const auto input = temp.filePath("octahedron.mesh.json");
		QFile file(input);
		expect(file.open(QIODevice::WriteOnly) && file.write(serializeEditableModel(original, &error)) > 0, "write CLI source");
		file.close();
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(QString::fromLocal8Bit(argv[1]), {"--cli", "model", "tool", input, "--tool", "uv-quake-frontback", "--axis", "x", "--faces",
														  "all", "--output", temp.filePath("out.mesh.json"), "--dry-run", "--json",
														  "--settings-file", temp.filePath("s.ini")});
		const bool finished = process.waitForFinished(60000);
		const auto json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
		expect(finished && process.exitCode() == 0 && json["mdlLayout"].toObject()["stored"].toInt() == 6 &&
				   json["mdlLayout"].toObject()["onseam"].toInt() == 4,
			   "CLI reports the predicted MDL vertices", QString::fromUtf8(process.readAllStandardError()));
	}
	std::cout << checks << " front/back layout checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
