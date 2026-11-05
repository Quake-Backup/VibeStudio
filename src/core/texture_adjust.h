#pragma once

#include "core/texture_paint.h"

#include <QColor>

namespace vibestudio {

// Whole-area raster operations for the texture editor. Every function reads an
// immutable source, writes ARGB32 pixels only inside `area`, and leaves alpha
// unchanged unless the operation says otherwise. Results are deterministic, so
// the GUI, the CLI recipe and tests share one definition.

struct TextureColorAdjustment {
	int brightness = 0;  // -255..255, added to each RGB channel
	int contrast = 0;    // -100..100 percent around mid-grey
	int hue = 0;         // -180..180 degrees
	int saturation = 0;  // -100..100 percent
	int lightness = 0;   // -100..100 percent towards black or white
	int gamma = 100;     // 10..1000, percent of 1.0; applied last
	int posterize = 0;   // 0 off, otherwise 2..64 levels per channel
	bool invert = false;
	bool grayscale = false;
};
bool validTextureColorAdjustment(const TextureColorAdjustment& adjustment);
[[nodiscard]] bool textureColorAdjustmentIsIdentity(const TextureColorAdjustment& adjustment);

enum class TextureFilterKind { Blur, Sharpen, Noise, Emboss, Edges };
QString textureFilterKindId(TextureFilterKind kind);
bool textureFilterKindFromId(const QString& id, TextureFilterKind* kind);

struct TextureFilterSettings {
	TextureFilterKind kind = TextureFilterKind::Blur;
	int radius = 1;       // 1..16, blur/sharpen neighbourhood
	int amount = 50;      // 1..255; sharpen/emboss strength percent, noise range
	quint32 seed = 1;     // noise sequence
	bool monochrome = true;
	// Sample across the canvas edges, so filtering a tiling texture adds no seam.
	bool wrap = false;
};
bool validTextureFilterSettings(const TextureFilterSettings& settings);

enum class TextureGradientShape { Linear, Radial, Reflected };
QString textureGradientShapeId(TextureGradientShape shape);
bool textureGradientShapeFromId(const QString& id, TextureGradientShape* shape);

struct TextureGradient {
	QPoint from, to;
	QRgb first = 0xff000000u, second = 0xffffffffu;
	TextureGradientShape shape = TextureGradientShape::Linear;
	// Replace writes the gradient; SourceOver blends it once per pixel.
	TexturePaintMode mode = TexturePaintMode::Replace;
	// Ordered 4 × 4 dither of the interpolated channels, for banding-free
	// gradients that are later quantized to a game palette.
	bool dither = false;
};

QImage adjustTextureColors(const QImage& source, QRect area, const TextureColorAdjustment& adjustment,
	QString* error, const TextureProgress& progress = {});
QImage filterTexturePixels(const QImage& source, QRect area, const TextureFilterSettings& settings,
	QString* error, const TextureProgress& progress = {});
QImage gradientTexturePixels(const QImage& source, QRect area, const TextureGradient& gradient,
	QString* error, const TextureProgress& progress = {});
// Replace every pixel within `tolerance` of `from` in each RGBA channel. Not
// connected, unlike fill; `keepAlpha` retains each matched pixel's alpha.
QImage replaceTextureColor(const QImage& source, QRect area, QRgb from, QRgb to, int tolerance, bool keepAlpha,
	QString* error, const TextureProgress& progress = {});
// Cross-fade each edge band of `blend` texels with the opposite edge, so the
// area tiles without a hard seam. `blend` is 1..half the area's shorter side.
QImage seamlessTexturePixels(const QImage& source, QRect area, int blend,
	QString* error, const TextureProgress& progress = {});

// Pixels outside a game palette after nearest-colour conversion would change
// them, counted on the visible composite (alpha ignored). -1 on cancellation.
qint64 textureOffPalettePixels(const QImage& image, const QVector<QRgb>& palette, const TextureProgress& progress = {});
// Distinct ARGB colours, capped at `limit` (returns limit + 1 when exceeded).
int textureDistinctColors(const QImage& image, int limit = 65536);

} // namespace vibestudio
