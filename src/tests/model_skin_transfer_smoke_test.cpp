#include "cli/model_tools.h"
#include "core/model_document.h"
#include "core/model_skin_transfer.h"
#include "tests/model_uv_health_test_helpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImageWriter>
#include <QTemporaryDir>
#include <cmath>
#include <functional>
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
// An 8 x 8 skin with a distinct colour per texel.
QImage checker()
{
	QImage image(8, 8, QImage::Format_ARGB32);
	for (int y = 0; y < 8; ++y)
		for (int x = 0; x < 8; ++x)
			image.setPixel(x, y, qRgb(x * 31, y * 31, (x + y) % 2 ? 200 : 40));
	return image;
}
// One quad island covering the skin's top-left 4 x 4 texels.
ModelSurface island(std::function<ModelTexCoord(ModelTexCoord)> map = {})
{
	auto mesh = strip(1);
	auto surface = mesh.surfaces[0];
	for (auto &uv : surface.texCoords)
	{
		uv = {uv.u * .5f, uv.v * .5f};
		if (map)
			uv = map(uv);
	}
	return surface;
}
// Every texel of the transferred island equals the source texel the
// inverse map names.
bool matches(const QImage &result, const QImage &source, const ModelSurface &after, std::function<QPointF(QPointF)> inverse)
{
	for (int y = 0; y < result.height(); ++y)
		for (int x = 0; x < result.width(); ++x)
		{
			const QPointF uv((x + .5) / result.width(), (y + .5) / result.height());
			// Only texels inside the new island are defined.
			double minU = 2, maxU = -1, minV = 2, maxV = -1;
			for (const auto c : after.texCoords)
			{
				minU = std::min(minU, double(c.u));
				maxU = std::max(maxU, double(c.u));
				minV = std::min(minV, double(c.v));
				maxV = std::max(maxV, double(c.v));
			}
			if (uv.x() < minU || uv.x() > maxU || uv.y() < minV || uv.y() > maxV)
				continue;
			const auto old = inverse(uv);
			const int sx = int(std::floor(old.x() * source.width())), sy = int(std::floor(old.y() * source.height()));
			if (result.pixel(x, y) != source.pixel(sx, sy))
				return false;
		}
	return true;
}
QByteArray hash(const QImage &image)
{
	QCryptographicHash sum(QCryptographicHash::Sha256);
	for (int y = 0; y < image.height(); ++y)
		sum.addData(QByteArrayView(reinterpret_cast<const char *>(image.constScanLine(y)), image.bytesPerLine()));
	return sum.result().toHex();
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	Q_UNUSED(argc);
	Q_UNUSED(argv);
	QString error;
	const auto source = checker();
	const auto before = island();
	ModelSkinTransferOptions options;
	options.padding = 0;
	QImage result;
	ModelSkinTransferReport report;
	struct Case
	{
		const char *name;
		std::function<ModelTexCoord(ModelTexCoord)> forward;
		std::function<QPointF(QPointF)> inverse;
	};
	const QVector<Case> cases{
		{"translation", [](ModelTexCoord p) { return ModelTexCoord{p.u + .5f, p.v + .25f}; },
		 [](QPointF p) { return QPointF(p.x() - .5, p.y() - .25); }},
		{"quarter turn", [](ModelTexCoord p) { return ModelTexCoord{.25f - (p.v - .25f) + .5f, .25f + (p.u - .25f) + .5f}; },
		 [](QPointF p) { return QPointF(.25 + (p.y() - .75), .25 - (p.x() - .75)); }},
		{"uniform scale", [](ModelTexCoord p) { return ModelTexCoord{p.u * 2, p.v * 2}; }, [](QPointF p) { return p / 2; }},
		{"mirror", [](ModelTexCoord p) { return ModelTexCoord{1 - p.u, p.v}; }, [](QPointF p) { return QPointF(1 - p.x(), p.y()); }},
	};
	for (const auto &c : cases)
	{
		const auto after = island(c.forward);
		expect(transferModelSkin(before, after, source, {8, 8}, options, &result, &report, &error) && report.faces == 2 &&
				   matches(result, source, after, c.inverse),
			   c.name, error);
	}
	// Bilinear sampling at texel centres of a whole-texel move is exact.
	options.filter = ModelSkinTransferFilter::Bilinear;
	const auto moved = island(cases[0].forward);
	expect(transferModelSkin(before, moved, source, {8, 8}, options, &result, &report, &error) && matches(result, source, moved, cases[0].inverse),
		   "bilinear stays exact on texel centres", error);
	options.filter = ModelSkinTransferFilter::Nearest;
	// Padding repeats island edges outward.
	options.padding = 1;
	expect(transferModelSkin(before, moved, source, {8, 8}, options, &result, &report, &error) && report.texels == 16 &&
			   report.paddedTexels == 12 && result.pixel(3, 2) == result.pixel(4, 2) && qAlpha(result.pixel(0, 0)) == 0,
		   "one ring of edge-adjacent padding around a 4 x 4 island", QStringLiteral("%1 padded").arg(report.paddedTexels));
	options.padding = 0;
	// Indexed skins keep their indices under nearest sampling.
	QImage indexed(8, 8, QImage::Format_Indexed8);
	QVector<QRgb> table;
	for (int i = 0; i < 256; ++i)
		table.append(qRgb(i, 255 - i, (i * 7) % 256));
	indexed.setColorTable(table);
	for (int y = 0; y < 8; ++y)
		for (int x = 0; x < 8; ++x)
			indexed.setPixel(x, y, uint(y * 8 + x + 100));
	expect(transferModelSkin(before, moved, indexed, {8, 8}, options, &result, &report, &error) && result.format() == QImage::Format_Indexed8 &&
			   result.colorTable() == table && result.pixelIndex(4, 2) == 100 && result.pixelIndex(7, 5) == 3 * 8 + 3 + 100 && report.indexed,
		   "indexed skins keep exact palette indices", error);
	// Faces only one side has are reported.
	auto extra = moved;
	extra.triangles.append(extra.triangles.first());
	expect(transferModelSkin(before, extra, source, {8, 8}, options, &result, &report, &error) && report.missingFaces == QVector<int>{2},
		   "faces missing from one side are reported");
	// Determinism, invalid input and cancellation.
	QImage first, second;
	transferModelSkin(before, moved, source, {16, 16}, options, &first, nullptr, &error);
	transferModelSkin(before, moved, source, {16, 16}, options, &second, nullptr, &error);
	expect(hash(first) == hash(second), "transfers are deterministic");
	const auto kept = hash(first);
	expect(!transferModelSkin(before, moved, QImage(), {8, 8}, options, &first, nullptr, &error) && hash(first) == kept, "a missing skin is refused");
	ModelWorkControl stop;
	stop.cancelled = [] { return true; };
	expect(!transferModelSkin(before, moved, source, {8, 8}, options, &first, nullptr, &error, stop) && hash(first) == kept,
		   "cancellation keeps the caller's image");
	// CLI.
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	QTemporaryDir temp(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("skin-transfer-XXXXXX"));
	auto meshBefore = strip(1), meshAfter = strip(1);
	meshBefore.surfaces[0] = before;
	meshAfter.surfaces[0] = moved;
	const auto from = temp.filePath("before.mesh.json"), to = temp.filePath("after.mesh.json"), texture = temp.filePath("skin.png"),
			   output = temp.filePath("rebaked.png");
	for (const auto &[path, mesh] : {std::pair{from, meshBefore}, std::pair{to, meshAfter}})
	{
		QFile file(path);
		expect(file.open(QIODevice::WriteOnly) && file.write(serializeEditableModel(mesh, &error)) > 0, "write CLI source");
	}
	expect(QImageWriter(texture, "png").write(source), "write the CLI skin");
	const QStringList base{"vibestudio", "--cli", "model", "skin-transfer", "--from", from, "--to", to, "--texture", texture, "--output", output,
						   "--padding", "0"};
	auto dry = cli::runModelSkinTransfer(base + QStringList{"--dry-run"});
	expect(dry.exitCode == 0 && !QFile::exists(output) && dry.payload["transfer"].toObject()["faces"].toInt() == 2, "CLI dry run", dry.error);
	auto run = cli::runModelSkinTransfer(base);
	transferModelSkin(before, moved, source, {8, 8}, options, &result, nullptr, &error);
	expect(run.exitCode == 0 && QImage(output).convertToFormat(QImage::Format_ARGB32) == result, "CLI writes the same skin", run.error);
	auto protect = cli::runModelSkinTransfer({"vibestudio", "--cli", "model", "skin-transfer", "--from", from, "--to", to, "--texture", texture,
											  "--output", texture, "--overwrite"});
	expect(protect.exitCode != 0, "the source skin is protected");
	expect(cli::runModelSkinTransfer(base + QStringList{"--filter", "cubic"}).exitCode == 2, "unknown filters are refused");
	std::cout << checks << " skin transfer checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
