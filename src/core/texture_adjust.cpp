#include "core/texture_adjust.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>

namespace vibestudio {
namespace {
bool fail(QString* error, const QString& message) { if (error) { *error = message; } return false; }
bool checkpoint(const TextureProgress& progress, qint64 done, qint64 total, QString* error)
{
	return !progress || progress(done, total) || fail(error, QCoreApplication::translate("VibeStudioTextureAdjust", "Texture operation cancelled."));
}
int clampChannel(int value) { return std::clamp(value, 0, 255); }
int clampChannel(double value) { return std::clamp(int(std::floor(value + 0.5)), 0, 255); }
int wrapped(int value, int size) { const int remainder = value % size; return remainder < 0 ? remainder + size : remainder; }

// The writable ARGB32 copy every operation starts from. Pixels outside `area`
// keep their exact values; an indexed source converts losslessly.
QImage workingCopy(const QImage& source, QRect area, QString* error)
{
	if (source.isNull() || area.isEmpty() || !source.rect().contains(area)) {
		fail(error, QCoreApplication::translate("VibeStudioTextureAdjust", "The edited area must lie inside the canvas."));
		return {};
	}
	QImage image = source.convertToFormat(QImage::Format_ARGB32);
	if (image.isNull()) { fail(error, QCoreApplication::translate("VibeStudioTextureAdjust", "Unable to allocate texture pixels.")); }
	return image;
}

QRgb sourceOver(QRgb source, QRgb destination)
{
	const int alpha = qAlpha(source), inverse = 255 - alpha;
	if (alpha == 0) { return destination; }
	if (alpha == 255 || qAlpha(destination) == 0) { return source; }
	const int denominator = alpha * 255 + qAlpha(destination) * inverse;
	const auto channel = [&](int foreground, int background) {
		return (foreground * alpha * 255 + background * qAlpha(destination) * inverse + denominator / 2) / denominator;
	};
	return qRgba(channel(qRed(source), qRed(destination)), channel(qGreen(source), qGreen(destination)),
		channel(qBlue(source), qBlue(destination)), (denominator + 127) / 255);
}

// Premultiplied interpolation keeps hidden colour under transparent texels from
// bleeding into blended neighbours.
QRgb mixPremultiplied(QRgb a, QRgb b, double t)
{
	const double alphaA = qAlpha(a) / 255.0, alphaB = qAlpha(b) / 255.0;
	const double alpha = alphaA * (1 - t) + alphaB * t;
	if (alpha <= 0) { return qRgba(0, 0, 0, 0); }
	const auto channel = [&](int x, int y) { return clampChannel((x * alphaA * (1 - t) + y * alphaB * t) / alpha); };
	return qRgba(channel(qRed(a), qRed(b)), channel(qGreen(a), qGreen(b)), channel(qBlue(a), qBlue(b)), clampChannel(alpha * 255));
}

int luma(QRgb color) { return (qRed(color) * 299 + qGreen(color) * 587 + qBlue(color) * 114 + 500) / 1000; }

// Premultiplied channels fit 16 bits (255 x 255), halving a 4-Mpixel pass.
using TexturePremultiplied = std::array<quint16, 4>;

// One horizontal or vertical box pass over premultiplied channels, sampling
// clamped or wrapped within the area.
QVector<TexturePremultiplied> boxPass(const QVector<TexturePremultiplied>& input, int width, int height, int radius, bool horizontal, bool wrap)
{
	QVector<TexturePremultiplied> output(input.size());
	const int lines = horizontal ? height : width, length = horizontal ? width : height, span = radius * 2 + 1;
	for (int line = 0; line < lines; ++line) {
		const auto at = [&](int position) {
			position = wrap ? wrapped(position, length) : std::clamp(position, 0, length - 1);
			return horizontal ? line * width + position : position * width + line;
		};
		std::array<int, 4> sum{};
		for (int offset = -radius; offset <= radius; ++offset) { for (int c = 0; c < 4; ++c) { sum[c] += input[at(offset)][c]; } }
		for (int position = 0; position < length; ++position) {
			auto& out = output[at(position)];
			for (int c = 0; c < 4; ++c) { out[c] = quint16((sum[c] + span / 2) / span); }
			const auto& leaving = input[at(position - radius)]; const auto& entering = input[at(position + radius + 1)];
			for (int c = 0; c < 4; ++c) { sum[c] += entering[c] - leaving[c]; }
		}
	}
	return output;
}
}

bool validTextureColorAdjustment(const TextureColorAdjustment& a)
{
	return a.brightness >= -255 && a.brightness <= 255 && a.contrast >= -100 && a.contrast <= 100 && a.hue >= -180 && a.hue <= 180 &&
		a.saturation >= -100 && a.saturation <= 100 && a.lightness >= -100 && a.lightness <= 100 && a.gamma >= 10 && a.gamma <= 1000 &&
		(a.posterize == 0 || (a.posterize >= 2 && a.posterize <= 64));
}

bool textureColorAdjustmentIsIdentity(const TextureColorAdjustment& a)
{
	return !a.brightness && !a.contrast && !a.hue && !a.saturation && !a.lightness && a.gamma == 100 && !a.posterize && !a.invert && !a.grayscale;
}

QString textureFilterKindId(TextureFilterKind kind)
{
	switch (kind) {
	case TextureFilterKind::Blur: return QStringLiteral("blur");
	case TextureFilterKind::Sharpen: return QStringLiteral("sharpen");
	case TextureFilterKind::Noise: return QStringLiteral("noise");
	case TextureFilterKind::Emboss: return QStringLiteral("emboss");
	case TextureFilterKind::Edges: return QStringLiteral("edges");
	}
	return {};
}

bool textureFilterKindFromId(const QString& id, TextureFilterKind* kind)
{
	for (auto value : {TextureFilterKind::Blur, TextureFilterKind::Sharpen, TextureFilterKind::Noise, TextureFilterKind::Emboss, TextureFilterKind::Edges}) {
		if (textureFilterKindId(value) == id) { if (kind) { *kind = value; } return true; }
	}
	return false;
}

bool validTextureFilterSettings(const TextureFilterSettings& s)
{
	return !textureFilterKindId(s.kind).isEmpty() && s.radius >= 1 && s.radius <= 16 && s.amount >= 1 && s.amount <= 255;
}

QString textureGradientShapeId(TextureGradientShape shape)
{
	switch (shape) {
	case TextureGradientShape::Linear: return QStringLiteral("linear");
	case TextureGradientShape::Radial: return QStringLiteral("radial");
	case TextureGradientShape::Reflected: return QStringLiteral("reflected");
	}
	return {};
}

bool textureGradientShapeFromId(const QString& id, TextureGradientShape* shape)
{
	for (auto value : {TextureGradientShape::Linear, TextureGradientShape::Radial, TextureGradientShape::Reflected}) {
		if (textureGradientShapeId(value) == id) { if (shape) { *shape = value; } return true; }
	}
	return false;
}

QImage adjustTextureColors(const QImage& source, QRect area, const TextureColorAdjustment& a, QString* error, const TextureProgress& progress)
{
	if (!validTextureColorAdjustment(a)) { fail(error, QCoreApplication::translate("VibeStudioTextureAdjust", "Colour adjustments are out of range.")); return {}; }
	QImage image = workingCopy(source, area, error);
	if (image.isNull()) { return {}; }
	// Order: invert, brightness, contrast, hue/saturation/lightness, greyscale,
	// posterize, gamma. Per-channel steps use lookup tables.
	std::array<int, 256> before{}, after{};
	for (int value = 0; value < 256; ++value) {
		int c = a.invert ? 255 - value : value;
		c = clampChannel(c + a.brightness);
		c = clampChannel((c - 128) * (100 + a.contrast) / 100.0 + 128);
		before[value] = c;
		int p = value;
		if (a.posterize) { const int steps = a.posterize - 1; p = clampChannel(std::floor(p * steps / 255.0 + 0.5) * 255.0 / steps); }
		if (a.gamma != 100) { p = clampChannel(255.0 * std::pow(p / 255.0, 100.0 / a.gamma)); }
		after[value] = p;
	}
	const bool hsl = a.hue || a.saturation || a.lightness;
	for (int y = area.top(); y <= area.bottom(); ++y) {
		if ((y - area.top()) % 32 == 0 && !checkpoint(progress, y - area.top(), area.height(), error)) { return {}; }
		auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (int x = area.left(); x <= area.right(); ++x) {
			const QRgb pixel = row[x];
			// Fully transparent texels keep their hidden colour.
			if (qAlpha(pixel) == 0) { continue; }
			int r = before[qRed(pixel)], g = before[qGreen(pixel)], b = before[qBlue(pixel)];
			if (hsl) {
				QColor color = QColor::fromRgb(r, g, b).toHsl();
				float h = 0, s = 0, l = 0; color.getHslF(&h, &s, &l);
				if (h >= 0) { h = std::fmod(h + a.hue / 360.0f + 1.0f, 1.0f); } else { h = 0; }
				s = std::clamp(s * (100 + a.saturation) / 100.0f, 0.0f, 1.0f);
				l = a.lightness > 0 ? l + (1 - l) * a.lightness / 100.0f : l + l * a.lightness / 100.0f;
				color.setHslF(h, s, std::clamp(l, 0.0f, 1.0f));
				const QColor rgb = color.toRgb(); r = rgb.red(); g = rgb.green(); b = rgb.blue();
			}
			if (a.grayscale) { r = g = b = luma(qRgb(r, g, b)); }
			row[x] = qRgba(after[r], after[g], after[b], qAlpha(pixel));
		}
	}
	return checkpoint(progress, area.height(), area.height(), error) ? image : QImage();
}

QImage filterTexturePixels(const QImage& source, QRect area, const TextureFilterSettings& s, QString* error, const TextureProgress& progress)
{
	if (!validTextureFilterSettings(s)) { fail(error, QCoreApplication::translate("VibeStudioTextureAdjust", "Choose a filter, a radius of 1–16 and an amount of 1–255.")); return {}; }
	QImage image = workingCopy(source, area, error);
	if (image.isNull()) { return {}; }
	const int width = area.width(), height = area.height();
	const QImage original = image;
	const auto sample = [&](int x, int y) {
		x = s.wrap ? wrapped(x, width) : std::clamp(x, 0, width - 1);
		y = s.wrap ? wrapped(y, height) : std::clamp(y, 0, height - 1);
		return original.pixel(area.left() + x, area.top() + y);
	};
	if (!checkpoint(progress, 0, height * 2, error)) { return {}; }
	if (s.kind == TextureFilterKind::Noise) {
		quint32 state = s.seed ? s.seed : 0x9e3779b9u;
		const auto next = [&state]() { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; };
		for (int y = 0; y < height; ++y) {
			if (y % 32 == 0 && !checkpoint(progress, y, height, error)) { return {}; }
			auto* row = reinterpret_cast<QRgb*>(image.scanLine(area.top() + y));
			for (int x = 0; x < width; ++x) {
				// The sequence advances for every texel, so the noise at a texel
				// does not depend on the alpha of the ones before it.
				const auto noise = [&]() { return int(next() % quint32(s.amount * 2 + 1)) - s.amount; };
				const int n = noise(), ng = s.monochrome ? n : noise(), nb = s.monochrome ? n : noise();
				QRgb& pixel = row[area.left() + x];
				if (qAlpha(pixel) == 0) { continue; }
				pixel = qRgba(clampChannel(qRed(pixel) + n), clampChannel(qGreen(pixel) + ng), clampChannel(qBlue(pixel) + nb), qAlpha(pixel));
			}
		}
		return checkpoint(progress, height, height, error) ? image : QImage();
	}
	if (s.kind == TextureFilterKind::Emboss || s.kind == TextureFilterKind::Edges) {
		for (int y = 0; y < height; ++y) {
			if (y % 32 == 0 && !checkpoint(progress, y, height, error)) { return {}; }
			auto* row = reinterpret_cast<QRgb*>(image.scanLine(area.top() + y));
			for (int x = 0; x < width; ++x) {
				QRgb& pixel = row[area.left() + x];
				if (qAlpha(pixel) == 0) { continue; }
				if (s.kind == TextureFilterKind::Emboss) {
					// Relief lit from the top left: the luma slope along the diagonal.
					const int slope = (luma(sample(x - 1, y - 1)) - luma(sample(x + 1, y + 1))) * s.amount / 100;
					pixel = qRgba(clampChannel(qRed(pixel) + slope), clampChannel(qGreen(pixel) + slope), clampChannel(qBlue(pixel) + slope), qAlpha(pixel));
				} else {
					const int gx = -luma(sample(x - 1, y - 1)) - 2 * luma(sample(x - 1, y)) - luma(sample(x - 1, y + 1)) +
						luma(sample(x + 1, y - 1)) + 2 * luma(sample(x + 1, y)) + luma(sample(x + 1, y + 1));
					const int gy = -luma(sample(x - 1, y - 1)) - 2 * luma(sample(x, y - 1)) - luma(sample(x + 1, y - 1)) +
						luma(sample(x - 1, y + 1)) + 2 * luma(sample(x, y + 1)) + luma(sample(x + 1, y + 1));
					const int value = clampChannel(std::sqrt(double(gx * gx + gy * gy)) * s.amount / 100.0);
					pixel = qRgba(value, value, value, qAlpha(pixel));
				}
			}
		}
		return checkpoint(progress, height, height, error) ? image : QImage();
	}
	// Blur and sharpen: one separable box pass in premultiplied space.
	QVector<TexturePremultiplied> channels(qsizetype(width) * height);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const QRgb pixel = original.pixel(area.left() + x, area.top() + y); const int alpha = qAlpha(pixel);
			channels[y * width + x] = {quint16(qRed(pixel) * alpha), quint16(qGreen(pixel) * alpha), quint16(qBlue(pixel) * alpha), quint16(alpha * 255)};
		}
	}
	if (!checkpoint(progress, height / 2, height * 2, error)) { return {}; }
	const auto blurred = boxPass(boxPass(channels, width, height, s.radius, true, s.wrap), width, height, s.radius, false, s.wrap);
	if (!checkpoint(progress, height, height * 2, error)) { return {}; }
	for (int y = 0; y < height; ++y) {
		if (y % 32 == 0 && !checkpoint(progress, height + y, height * 2, error)) { return {}; }
		auto* row = reinterpret_cast<QRgb*>(image.scanLine(area.top() + y));
		for (int x = 0; x < width; ++x) {
			QRgb& pixel = row[area.left() + x];
			const auto& blur = blurred[y * width + x];
			if (s.kind == TextureFilterKind::Blur) {
				const int alpha = clampChannel(blur[3] / 255.0);
				if (blur[3] <= 0) { pixel = qRgba(qRed(pixel), qGreen(pixel), qBlue(pixel), 0); continue; }
				pixel = qRgba(clampChannel(double(blur[0]) * 255 / blur[3]), clampChannel(double(blur[1]) * 255 / blur[3]), clampChannel(double(blur[2]) * 255 / blur[3]), alpha);
			} else {
				if (qAlpha(pixel) == 0) { continue; }
				const auto unsharp = [&](int value, int channel) {
					const double softened = blur[3] > 0 ? double(blur[channel]) * 255 / blur[3] : value;
					return clampChannel(value + (value - softened) * s.amount / 100.0);
				};
				pixel = qRgba(unsharp(qRed(pixel), 0), unsharp(qGreen(pixel), 1), unsharp(qBlue(pixel), 2), qAlpha(pixel));
			}
		}
	}
	return checkpoint(progress, height * 2, height * 2, error) ? image : QImage();
}

QImage gradientTexturePixels(const QImage& source, QRect area, const TextureGradient& g, QString* error, const TextureProgress& progress)
{
	if (g.mode == TexturePaintMode::Clone || textureGradientShapeId(g.shape).isEmpty()) { fail(error, QCoreApplication::translate("VibeStudioTextureAdjust", "Choose a linear, radial or reflected gradient that replaces or blends.")); return {}; }
	QImage image = workingCopy(source, area, error);
	if (image.isNull()) { return {}; }
	static constexpr int bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
	const double fx = g.from.x() + 0.5, fy = g.from.y() + 0.5;
	const double vx = g.to.x() - g.from.x(), vy = g.to.y() - g.from.y();
	const double length2 = vx * vx + vy * vy;
	for (int y = area.top(); y <= area.bottom(); ++y) {
		if ((y - area.top()) % 32 == 0 && !checkpoint(progress, y - area.top(), area.height(), error)) { return {}; }
		auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (int x = area.left(); x <= area.right(); ++x) {
			const double px = x + 0.5 - fx, py = y + 0.5 - fy;
			double t = 0;
			if (length2 > 0) {
				if (g.shape == TextureGradientShape::Radial) { t = std::sqrt((px * px + py * py) / length2); }
				else { t = (px * vx + py * vy) / length2; if (g.shape == TextureGradientShape::Reflected) { t = std::abs(t); } }
			}
			t = std::clamp(t, 0.0, 1.0);
			const double threshold = g.dither ? (bayer[y & 3][x & 3] + 0.5) / 16.0 : 0.5;
			const auto channel = [&](int first, int second) { return std::clamp(int(std::floor(first + (second - first) * t + threshold)), 0, 255); };
			const QRgb color = qRgba(channel(qRed(g.first), qRed(g.second)), channel(qGreen(g.first), qGreen(g.second)),
				channel(qBlue(g.first), qBlue(g.second)), channel(qAlpha(g.first), qAlpha(g.second)));
			row[x] = g.mode == TexturePaintMode::Replace ? color : sourceOver(color, row[x]);
		}
	}
	return checkpoint(progress, area.height(), area.height(), error) ? image : QImage();
}

QImage replaceTextureColor(const QImage& source, QRect area, QRgb from, QRgb to, int tolerance, bool keepAlpha, QString* error, const TextureProgress& progress)
{
	if (tolerance < 0 || tolerance > 255) { fail(error, QCoreApplication::translate("VibeStudioTextureAdjust", "Colour tolerance must be 0–255.")); return {}; }
	QImage image = workingCopy(source, area, error);
	if (image.isNull()) { return {}; }
	for (int y = area.top(); y <= area.bottom(); ++y) {
		if ((y - area.top()) % 32 == 0 && !checkpoint(progress, y - area.top(), area.height(), error)) { return {}; }
		auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (int x = area.left(); x <= area.right(); ++x) {
			const QRgb pixel = row[x];
			if (std::abs(qRed(pixel) - qRed(from)) > tolerance || std::abs(qGreen(pixel) - qGreen(from)) > tolerance ||
				std::abs(qBlue(pixel) - qBlue(from)) > tolerance || std::abs(qAlpha(pixel) - qAlpha(from)) > tolerance) { continue; }
			row[x] = keepAlpha ? (to & 0x00ffffffu) | (pixel & 0xff000000u) : to;
		}
	}
	return checkpoint(progress, area.height(), area.height(), error) ? image : QImage();
}

QImage seamlessTexturePixels(const QImage& source, QRect area, int blend, QString* error, const TextureProgress& progress)
{
	if (blend < 1 || blend > std::min(area.width(), area.height()) / 2) {
		fail(error, QCoreApplication::translate("VibeStudioTextureAdjust", "The seam blend width must be 1 to half the shorter side of the edited area."));
		return {};
	}
	QImage image = workingCopy(source, area, error);
	if (image.isNull()) { return {}; }
	// Each band texel at distance d from the seam mixes with its mirror partner
	// across the seam by 0.5 × (1 − d/blend). Both sides meet at the same
	// average, so the repeat is continuous. Columns first, then rows.
	for (int pass = 0; pass < 2; ++pass) {
		const bool columns = pass == 0;
		const int length = columns ? area.width() : area.height(), lines = columns ? area.height() : area.width();
		const QImage before = image;
		for (int line = 0; line < lines; ++line) {
			if (line % 32 == 0 && !checkpoint(progress, pass * lines + line, lines * 2, error)) { return {}; }
			for (int d = 0; d < blend; ++d) {
				const double weight = 0.5 * (1.0 - (d + 0.5) / blend);
				for (const int position : {d, length - 1 - d}) {
					const int partner = length - 1 - position;
					const QPoint here = columns ? QPoint(area.left() + position, area.top() + line) : QPoint(area.left() + line, area.top() + position);
					const QPoint there = columns ? QPoint(area.left() + partner, area.top() + line) : QPoint(area.left() + line, area.top() + partner);
					image.setPixel(here, mixPremultiplied(before.pixel(here), before.pixel(there), weight));
				}
			}
		}
	}
	return checkpoint(progress, 1, 1, error) ? image : QImage();
}

qint64 textureOffPalettePixels(const QImage& image, const QVector<QRgb>& palette, const TextureProgress& progress)
{
	QSet<QRgb> colors;
	for (const QRgb color : palette) { colors.insert(color & 0x00ffffffu); }
	qint64 count = 0;
	const QImage pixels = image.convertToFormat(QImage::Format_ARGB32);
	for (int y = 0; y < pixels.height(); ++y) {
		if (y % 64 == 0 && progress && !progress(y, pixels.height())) { return -1; }
		const auto* row = reinterpret_cast<const QRgb*>(pixels.constScanLine(y));
		for (int x = 0; x < pixels.width(); ++x) {
			if (qAlpha(row[x]) != 0 && !colors.contains(row[x] & 0x00ffffffu)) { ++count; }
		}
	}
	return count;
}

int textureDistinctColors(const QImage& image, int limit)
{
	QSet<QRgb> colors;
	const QImage pixels = image.convertToFormat(QImage::Format_ARGB32);
	for (int y = 0; y < pixels.height(); ++y) {
		const auto* row = reinterpret_cast<const QRgb*>(pixels.constScanLine(y));
		for (int x = 0; x < pixels.width(); ++x) {
			colors.insert(row[x]);
			if (colors.size() > limit) { return limit + 1; }
		}
	}
	return int(colors.size());
}

} // namespace vibestudio
