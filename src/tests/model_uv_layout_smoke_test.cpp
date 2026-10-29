#include "cli/model_tools.h"
#include "core/idtech_image.h"
#include "core/model_document.h"
#include "core/model_mdl.h"
#include "core/model_uv_layout.h"
#include "core/texture_export.h"
#include "tests/model_uv_health_test_helpers.h"
#include "tests/model_uv_islands_test_helpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
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
QByteArray hash(const QImage &image)
{
	QCryptographicHash sum(QCryptographicHash::Sha256);
	for (int y = 0; y < image.height(); ++y)
		sum.addData(QByteArrayView(reinterpret_cast<const char *>(image.constScanLine(y)), image.bytesPerLine()));
	return sum.result().toHex();
}
// One triangle with corners on texel centres of an 8 x 8 skin: (0,0), (6,0)
// and (0,6) in texels.
ModelMesh triangle()
{
	auto mesh = strip(1);
	auto &s = mesh.surfaces[0];
	s.triangles = {{0, 1, 2}};
	s.texCoords = {{.5f / 8, .5f / 8}, {6.5f / 8, .5f / 8}, {.5f / 8, 6.5f / 8}, {0, 0}};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	Q_UNUSED(argc);
	Q_UNUSED(argv);
	QString error;
	const auto mesh = triangle();
	ModelUvLayoutOptions options;
	QImage image;
	ModelUvLayoutReport report;
	expect(renderModelUvLayout(mesh, {0}, {8, 8}, options, &image, &report, &error) && image.size() == QSize(8, 8) && report.faces == 1 &&
			   report.islands == 1,
		   "draw a one-triangle layout", error);
	const QColor seam = options.seam;
	expect(image.pixelColor(3, 0) == seam && image.pixelColor(0, 3) == seam && image.pixelColor(2, 2).alpha() == 0 &&
			   image.pixelColor(7, 7).alpha() == 0,
		   "boundary lines land on the texels the UVs name; the inside stays clear");
	options.seams = false;
	expect(renderModelUvLayout(mesh, {0}, {8, 8}, options, &image, nullptr, &error) && image.pixelColor(3, 0) == options.edge,
		   "without boundary highlighting, edges use the edge colour");
	options.fillOpacity = 1;
	options.islandTints = false;
	options.fill = Qt::red;
	expect(renderModelUvLayout(mesh, {0}, {8, 8}, options, &image, nullptr, &error) && image.pixelColor(2, 2) == QColor(Qt::red) &&
			   image.pixelColor(6, 6).alpha() == 0,
		   "island fill covers the island only");
	QImage again;
	renderModelUvLayout(mesh, {0}, {8, 8}, options, &again, nullptr, &error);
	expect(hash(image) == hash(again), "layouts are deterministic");
	const auto quad = strip(2);
	QImage wide, square;
	expect(renderModelUvLayout(quad, {0}, {64, 16}, {}, &wide, nullptr, &error) &&
			   renderModelUvLayout(quad, {0}, {32, 32}, {}, &square, nullptr, &error) && hash(wide) != hash(square),
		   "a rectangular layout scales U and V separately");
	std::cout << "layout hashes: triangle " << hash(image).left(16).constData() << ", quad " << hash(wide).left(16).constData() << '\n';
	// Faces outside the tile wrap only when asked.
	auto outside = mesh;
	for (auto &uv : outside.surfaces[0].texCoords)
		uv.u += 1;
	ModelUvLayoutOptions wrap;
	expect(renderModelUvLayout(outside, {0}, {8, 8}, wrap, &image, &report, &error) && report.outsideFaces == 1 &&
			   image.pixelColor(3, 0).alpha() == 0,
		   "faces beyond the tile are cut off by default");
	wrap.wrap = true;
	expect(renderModelUvLayout(outside, {0}, {8, 8}, wrap, &image, &report, &error) && report.wrappedFaces == 1 &&
			   image.pixelColor(3, 0) == wrap.seam,
		   "wrap draws them inside the tile");
	// Limits, invalid input and cancellation keep the caller's image.
	const auto kept = hash(image);
	for (const auto size : {QSize(4097, 8), QSize(0, 8)})
		expect(!renderModelUvLayout(mesh, {0}, size, {}, &image, nullptr, &error) && hash(image) == kept, "sizes outside 1–4096 are refused");
	expect(!renderModelUvLayout(mesh, {3}, {8, 8}, {}, &image, nullptr, &error) && hash(image) == kept, "unknown surfaces are refused");
	ModelWorkControl stop;
	stop.cancelled = [] { return true; };
	expect(!renderModelUvLayout(mesh, {0}, {8, 8}, {}, &image, nullptr, &error, stop) && hash(image) == kept, "cancellation keeps the image");
	// PCX keeps the layout as palette indices.
	ModelUvLayoutOptions solid;
	solid.background = Qt::white;
	expect(renderModelUvLayout(mesh, {0}, {8, 8}, solid, &image, nullptr, &error), "draw on white");
	TextureExportOptions pcx;
	pcx.format = TextureExportFormat::Pcx;
	pcx.allowGeneratedPalette = true;
	pcx.fullbright = TextureFullbrightMode::Allow;
	IdTechPaletteResolution palette;
	palette.palette = generatedIdTechPalette(QStringLiteral("quake"));
	const auto encoded = encodeTextureExport(image, pcx, palette);
	ModelMdlSkinInput decoded;
	expect(encoded.succeeded && decodeModelMdlSkin("layout.pcx", encoded.bytes, palette.palette, &decoded, &error) &&
			   decoded.size == QSize(8, 8) && quint8(decoded.pixels[0 * 8 + 3]) != quint8(decoded.pixels[2 * 8 + 2]),
		   "PCX layouts store lines and paper as palette indices", error.isEmpty() ? encoded.error : error);
	// The material scope finds surfaces sharing a slot path.
	auto shared = uvIslandsFixture();
	expect(modelUvLayoutMaterialSurfaces(shared, 0) == QVector<int>{0, 1}, "surfaces sharing a material draw together");
	// CLI.
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	QTemporaryDir temp(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("uv-layout-XXXXXX"));
	const auto input = temp.filePath("strip.mesh.json"), output = temp.filePath("strip_uv.png");
	QFile file(input);
	expect(file.open(QIODevice::WriteOnly) && file.write(serializeEditableModel(quad, &error)) > 0, "write CLI source");
	file.close();
	const QStringList base{"vibestudio", "--cli", "model", "uv-layout", input, "--size", "64x16", "--output", output};
	auto dry = cli::runModelUvLayout(base + QStringList{"--dry-run"});
	expect(dry.exitCode == 0 && !QFile::exists(output) && dry.payload["layout"].toObject()["faces"].toInt() == 4, "CLI dry run", dry.error);
	auto written = cli::runModelUvLayout(base);
	expect(written.exitCode == 0 && QImage(output) == wide.convertToFormat(QImage(output).format()), "CLI writes the same layout", written.error);
	expect(cli::runModelUvLayout(base).exitCode == 1 && cli::runModelUvLayout(base + QStringList{"--overwrite"}).exitCode == 0,
		   "existing outputs need --overwrite");
	auto alias = cli::runModelUvLayout({"vibestudio", "--cli", "model", "uv-layout", input, "--output", input, "--overwrite"});
	expect(alias.exitCode != 0, "the source is protected");
	expect(cli::runModelUvLayout(base + QStringList{"--size", "5000"}).exitCode == 2, "oversized layouts are refused");
	auto pcxRun = cli::runModelUvLayout({"vibestudio", "--cli", "model", "uv-layout", input, "--output", temp.filePath("strip_uv.pcx"),
										 "--background", "white", "--fill", "0.25"});
	expect(pcxRun.exitCode == 0 && QFile::exists(temp.filePath("strip_uv.pcx")), "CLI PCX layout", pcxRun.error);
	std::cout << checks << " UV layout checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
