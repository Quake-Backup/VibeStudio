#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QString>
#include <functional>

namespace vibestudio {

// Return false to cancel. Called between bounded chunks on the executing thread.
using TextureProgress = std::function<bool(qint64 completed, qint64 total)>;

enum class TextureBrushShape { Square, Round };
// Clone copies the gesture-start pixels at cloneOffset instead of a color.
enum class TexturePaintMode { Replace, SourceOver, Clone };
enum class TextureShape { Line, Rectangle, Ellipse };

struct TextureBrush {
	int width = 1;
	TextureBrushShape shape = TextureBrushShape::Square;
	TexturePaintMode mode = TexturePaintMode::Replace;
	bool wrap = false;
	// Mirror strokes and shapes across the canvas centre lines.
	bool mirrorX = false, mirrorY = false;
	// Clone samples source(x + dx, y + dy); wrapped brushes also wrap samples.
	QPoint cloneOffset;
};

QString textureBrushShapeId(TextureBrushShape shape);
QString texturePaintModeId(TexturePaintMode mode);
QString textureMirrorId(const TextureBrush& brush);
bool textureMirrorFromId(const QString& id, TextureBrush* brush);
bool textureBrushShapeFromId(const QString& id, TextureBrushShape* shape);
// Accepts the color modes only; Clone is chosen by the clone tool or recipe.
bool texturePaintModeFromId(const QString& id, TexturePaintMode* mode);
bool validTextureBrush(const TextureBrush& brush);
// Wrapped input may span the three-by-three repeat, preserving seam crossings.
bool validTexturePaintPoint(QPoint point, QSize size, bool wrap);
QRect texturePixelBounds(QPoint first, QPoint last);

// Internal raster services. Destination is ARGB32; source is the immutable
// gesture-start layer. Blend-over is evaluated once per covered pixel against
// that source, so overlapping stamps never multiply a gesture's opacity.
bool paintTextureSegment(QImage* pixels, const QImage& source, QPoint from, QPoint to,
	QRgb color, const TextureBrush& brush, QRect clip, const TextureProgress& progress = {});
bool paintTextureShape(QImage* pixels, const QImage& source, TextureShape shape, QPoint from, QPoint to,
	QRgb color, const TextureBrush& brush, bool filled, QRect clip, const TextureProgress& progress = {});

} // namespace vibestudio
