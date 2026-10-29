#include "core/model_skin_transfer.h"

#include "core/model_uv_raster.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelSkinTransfer)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
int wrapIndex(int value, int length)
{
	const int r = value % length;
	return r < 0 ? r + length : r;
}
int texel(double coordinate, int length, bool wrap)
{
	const int index = int(std::floor(coordinate));
	return wrap ? wrapIndex(index, length) : std::clamp(index, 0, length - 1);
}
double uvArea(ModelTexCoord a, ModelTexCoord b, ModelTexCoord c)
{
	return (double(b.u) - a.u) * (double(c.v) - a.v) - (double(b.v) - a.v) * (double(c.u) - a.u);
}
int nearestIndex(const QVector<QRgb> &table, QRgb colour)
{
	int best = 0;
	qint64 distance = std::numeric_limits<qint64>::max();
	for (int i = 0; i < table.size(); ++i)
	{
		const qint64 dr = qRed(table[i]) - qRed(colour), dg = qGreen(table[i]) - qGreen(colour), db = qBlue(table[i]) - qBlue(colour);
		const qint64 d = dr * dr + dg * dg + db * db;
		if (d < distance)
		{
			distance = d;
			best = i;
		}
	}
	return best;
}
} // namespace

bool transferModelSkin(const ModelSurface &before, const ModelSurface &after, const QImage &source, QSize outputSize,
					   const ModelSkinTransferOptions &options, QImage *result, ModelSkinTransferReport *report, QString *error,
					   const ModelWorkControl &control)
{
	if (error)
		error->clear();
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	if (!result || source.isNull() || source.width() > 16384 || source.height() > 16384 || outputSize.width() < 1 ||
		outputSize.height() < 1 || outputSize.width() > 4096 || outputSize.height() > 4096 || options.padding < 0 || options.padding > 64 ||
		before.texCoords.size() != before.vertexCount || after.texCoords.size() != after.vertexCount)
		return fail(error, Text::tr("Choose a skin image, an output size from 1 to 4,096 pixels per side and padding from 0 to 64 texels."));
	const bool indexed = source.format() == QImage::Format_Indexed8;
	const QImage colours = indexed && options.filter == ModelSkinTransferFilter::Nearest ? QImage() : source.convertToFormat(QImage::Format_ARGB32);
	const QVector<QRgb> table = indexed ? source.colorTable() : QVector<QRgb>{};
	if (indexed && table.isEmpty())
		return fail(error, Text::tr("The indexed skin has no palette."));
	const int width = outputSize.width(), height = outputSize.height(), sw = source.width(), sh = source.height();
	QVector<QRgb> pixels(width * height, options.background.rgba());
	QVector<int> indices(indexed ? width * height : 0, std::clamp(options.backgroundIndex, 0, int(table.size()) - 1));
	QVector<bool> covered(width * height, false);
	ModelSkinTransferReport summary;
	summary.size = outputSize;
	summary.indexed = indexed;
	const int shared = int(std::min(before.triangles.size(), after.triangles.size()));
	for (int face = shared; face < std::max(before.triangles.size(), after.triangles.size()); ++face)
		summary.missingFaces.append(face);
	const auto sample = [&](double u, double v, int *index) -> QRgb
	{
		const double x = u * sw, y = v * sh;
		if (options.filter == ModelSkinTransferFilter::Nearest)
		{
			const int px = texel(x, sw, options.wrap), py = texel(y, sh, options.wrap);
			if (indexed)
			{
				*index = source.pixelIndex(px, py);
				return table.value(*index);
			}
			return colours.pixel(px, py);
		}
		// Bilinear between the four texel centres around the point.
		const double fx = x - .5, fy = y - .5;
		const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
		const double tx = fx - x0, ty = fy - y0;
		double channel[4]{};
		for (int j = 0; j < 2; ++j)
			for (int i = 0; i < 2; ++i)
			{
				const int px = options.wrap ? wrapIndex(x0 + i, sw) : std::clamp(x0 + i, 0, sw - 1);
				const int py = options.wrap ? wrapIndex(y0 + j, sh) : std::clamp(y0 + j, 0, sh - 1);
				const QRgb c = colours.pixel(px, py);
				const double w = (i ? tx : 1 - tx) * (j ? ty : 1 - ty);
				channel[0] += qRed(c) * w;
				channel[1] += qGreen(c) * w;
				channel[2] += qBlue(c) * w;
				channel[3] += qAlpha(c) * w;
			}
		const QRgb blended = qRgba(int(std::lround(channel[0])), int(std::lround(channel[1])), int(std::lround(channel[2])),
								   int(std::lround(channel[3])));
		if (indexed)
			*index = nearestIndex(table, blended);
		return blended;
	};
	for (int face = 0; face < shared; ++face)
	{
		if (!work.step())
			return false;
		const auto t = after.triangles[face], o = before.triangles[face];
		const ModelTexCoord n[3]{after.texCoords[t.a], after.texCoords[t.b], after.texCoords[t.c]};
		const ModelTexCoord p[3]{before.texCoords[o.a], before.texCoords[o.b], before.texCoords[o.c]};
		if (std::abs(uvArea(n[0], n[1], n[2])) < 1e-14 || std::abs(uvArea(p[0], p[1], p[2])) < 1e-14)
		{
			summary.collapsedFaces.append(face);
			continue;
		}
		++summary.faces;
		bool stopped = false;
		rasterizeModelUvTriangle(modelUvPixel(n[0], width, height), modelUvPixel(n[1], width, height), modelUvPixel(n[2], width, height),
								 width, height,
								 [&](int x, int y, double w0, double w1, double w2)
								 {
									 if (!work.step())
									 {
										 stopped = true;
										 return false;
									 }
									 const double u = w0 * p[0].u + w1 * p[1].u + w2 * p[2].u;
									 const double v = w0 * p[0].v + w1 * p[1].v + w2 * p[2].v;
									 int index = 0;
									 const auto colour = sample(u, v, &index);
									 const int at = y * width + x;
									 pixels[at] = colour;
									 if (indexed)
										 indices[at] = index;
									 summary.texels += !covered[at];
									 covered[at] = true;
									 return true;
								 });
		if (stopped)
			return false;
	}
	// Padding: each ring takes the colour of its first covered neighbour,
	// left, right, above, then below.
	for (int ring = 0; ring < options.padding; ++ring)
	{
		auto next = covered;
		for (int y = 0; y < height; ++y)
		{
			if (!work.step())
				return false;
			for (int x = 0; x < width; ++x)
			{
				const int at = y * width + x;
				if (covered[at])
					continue;
				const std::array<std::array<int, 2>, 4> around{{{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}}};
				for (const auto &n : around)
				{
					if (n[0] < 0 || n[1] < 0 || n[0] >= width || n[1] >= height || !covered[n[1] * width + n[0]])
						continue;
					const int from = n[1] * width + n[0];
					pixels[at] = pixels[from];
					if (indexed)
						indices[at] = indices[from];
					next[at] = true;
					++summary.paddedTexels;
					break;
				}
			}
		}
		covered = std::move(next);
	}
	QImage image;
	if (indexed)
	{
		image = QImage(width, height, QImage::Format_Indexed8);
		image.setColorTable(table);
		for (int y = 0; y < height; ++y)
		{
			auto *row = image.scanLine(y);
			for (int x = 0; x < width; ++x)
				row[x] = uchar(indices[y * width + x]);
		}
	}
	else
	{
		image = QImage(width, height, QImage::Format_ARGB32);
		for (int y = 0; y < height; ++y)
			std::copy_n(pixels.constData() + y * width, width, reinterpret_cast<QRgb *>(image.scanLine(y)));
	}
	if (image.isNull())
		return fail(error, Text::tr("Unable to allocate the transferred skin. Choose a smaller size."));
	if (!work.check())
		return false;
	*result = std::move(image);
	if (report)
		*report = std::move(summary);
	return true;
}

QJsonObject modelSkinTransferJson(const ModelSkinTransferReport &report)
{
	QJsonArray missing, collapsed;
	for (int face : report.missingFaces)
		missing.append(face);
	for (int face : report.collapsedFaces)
		collapsed.append(face);
	return {{"width", report.size.width()},	 {"height", report.size.height()}, {"faces", report.faces},
			{"texels", report.texels},		 {"paddedTexels", report.paddedTexels}, {"indexed", report.indexed},
			{"missingFaces", missing},		 {"collapsedFaces", collapsed}};
}
} // namespace vibestudio
