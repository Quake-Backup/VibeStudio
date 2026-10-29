#pragma once

#include "core/texture_paint.h"

#include <QByteArray>
#include <QImage>
#include <QStringList>
#include <QVector>

namespace vibestudio {

// Byte-level writers for the texture formats added after the original export
// profiles: IDSP sprites (Quake and Half-Life), Quake II SP2, Half-Life WAD3
// miptextures, SiN SWL and mipmapped DDS. Inputs are already quantized and
// validated by core/texture_export; these functions only lay out bytes.

// Median-cut palette of at most `count` (1..256) colours from the texels with
// non-zero alpha, deterministic for a given image. Fewer distinct colours are
// returned exactly, sorted by luma. Empty when cancelled.
QVector<QRgb> buildTexturePalette(const QVector<QImage>& images, int count, const TextureProgress& progress = {});
// 256 RGB triplets (768 bytes), the layout of Quake's gfx/palette.lmp.
QByteArray encodeRawTexturePalette(const QVector<QRgb>& colors);

struct TextureSpriteEncoding {
	int version = 1;      // 1 Quake, 2 Half-Life
	int type = 2;         // orientation, 0..4 (parallel-upright .. parallel-oriented)
	int texFormat = 0;    // Half-Life: 0 normal, 1 additive, 2 index-alpha, 3 alpha-test
	int syncType = 0;     // 0 synchronised, 1 random
	QVector<QRgb> palette;  // Half-Life only: 256 RGB entries
	QVector<QImage> frames; // Indexed8 frames
	// dspriteframe_t origin: left edge relative to the origin (negative to the
	// left) and top edge relative to the origin (positive upwards).
	QVector<QPoint> origins;
};
QByteArray encodeIdspSprite(const TextureSpriteEncoding& sprite);

struct TextureSp2Frame {
	QSize size;
	// dsprframe_t origin: from the left edge and from the bottom edge.
	QPoint origin;
	QString name; // package path of the frame image, at most 63 bytes
};
QByteArray encodeSp2Sprite(const QVector<TextureSp2Frame>& frames);

// One Half-Life miptexture lump: header, four mips, then the palette that
// WAD3 stores with every texture.
QByteArray encodeWad3Miptex(const QVector<QImage>& mips, const QString& name, const QVector<QRgb>& palette);
// A new WAD3 holding one miptexture lump of type 0x43.
QByteArray encodeWad3Archive(const QByteArray& lump, const QString& name);

struct TextureSwlFields {
	QString name, animationNext;
	quint32 flags = 0, contents = 0;
	qint16 value = 0;
};
QByteArray encodeSwl(const QVector<QImage>& mips, const QVector<QRgb>& palette, const TextureSwlFields& fields);

// Area-averaged (premultiplied) RGBA mip chain down to 1 × 1, level 0 first.
QVector<QImage> textureRgbaMipChain(const QImage& image, const TextureProgress& progress = {});
QByteArray encodeDdsMipmapped(const QVector<QImage>& mips, const TextureProgress& progress = {});

} // namespace vibestudio
