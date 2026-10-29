#include "core/texture_formats.h"

#include <QHash>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

// Reimplemented layouts, not copied code. References, reviewed 2026-10-28:
// - IDSP version 1: id Software Quake, WinQuake/spritegn.h (GPL-2.0-or-later)
//   and the Quake Specifications sprite chapter.
// - IDSP version 2: Valve halflife SDK utils/sprgen/sprgen.c
//   (Half-Life SDK licence; layout only).
// - IDS2: id Software Quake-2 qcommon/qfiles.h dsprite_t / dsprframe_t
//   (GPL-2.0-or-later) and ref_gl/gl_rmain.c R_DrawSpriteModel for origins.
// - WAD3: the Valve Developer Community "WAD" page.
// - SWL: Ritual SiN SDK qfiles.h sinmiptex_t, as read by PakFu swl_image.cpp.
// - DDS: Microsoft "DDS_HEADER structure" and "DDS_PIXELFORMAT" documentation.
// See docs/CREDITS.md, "Texture export formats".

namespace vibestudio {
namespace {
void put16(QByteArray& bytes, qsizetype at, quint16 value) { qToLittleEndian(value, bytes.data() + at); }
void put32(QByteArray& bytes, qsizetype at, quint32 value) { qToLittleEndian(value, bytes.data() + at); }
void putFloat(QByteArray& bytes, qsizetype at, float value) { quint32 bits = 0; std::memcpy(&bits, &value, sizeof bits); put32(bytes, at, bits); }
void putName(QByteArray& bytes, qsizetype at, const QString& name, int limit)
{
	const auto encoded = name.toLatin1().left(limit - 1);
	std::memcpy(bytes.data() + at, encoded.constData(), size_t(encoded.size()));
}
void appendRows(QByteArray& bytes, const QImage& indexed)
{
	for (int y = 0; y < indexed.height(); ++y) { bytes.append(reinterpret_cast<const char*>(indexed.constScanLine(y)), indexed.width()); }
}
int luma(QRgb color) { return qRed(color) * 299 + qGreen(color) * 587 + qBlue(color) * 114; }
bool report(const TextureProgress& progress, qint64 done, qint64 total) { return !progress || progress(done, total); }
}

QVector<QRgb> buildTexturePalette(const QVector<QImage>& images, int count, const TextureProgress& progress)
{
	count = std::clamp(count, 1, 256);
	qint64 rows = 0;
	for (const auto& image : images) { rows += image.height(); }
	rows = std::max<qint64>(rows, 1);
	struct Entry { int r = 0, g = 0, b = 0; qint64 weight = 0; };
	QHash<QRgb, qint64> exact; bool reduced = false;
	QHash<int, std::array<qint64, 4>> buckets;
	const auto bucket = [&](QRgb rgb, qint64 weight) {
		auto& sum = buckets[((qRed(rgb) >> 3) << 10) | ((qGreen(rgb) >> 3) << 5) | (qBlue(rgb) >> 3)];
		sum[0] += qint64(qRed(rgb)) * weight; sum[1] += qint64(qGreen(rgb)) * weight; sum[2] += qint64(qBlue(rgb)) * weight; sum[3] += weight;
	};
	qint64 scanned = 0;
	for (const auto& source : images) {
		const QImage image = source.convertToFormat(QImage::Format_ARGB32);
		for (int y = 0; y < image.height(); ++y, ++scanned) {
			if (scanned % 64 == 0 && !report(progress, scanned, rows * 2)) { return {}; }
			const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
			for (int x = 0; x < image.width(); ++x) {
				if (qAlpha(row[x]) == 0) { continue; }
				const QRgb rgb = row[x] & 0x00ffffffu;
				if (reduced) { bucket(rgb, 1); continue; }
				++exact[rgb];
				if (exact.size() > 65536) {
					// Too many colours to cut exactly: continue on 15-bit buckets that
					// keep the true mean colour of their members.
					for (auto it = exact.cbegin(); it != exact.cend(); ++it) { bucket(it.key(), it.value()); }
					exact.clear(); reduced = true;
				}
			}
		}
	}
	QVector<Entry> entries;
	if (!reduced) {
		QVector<QRgb> keys = exact.keys(); std::sort(keys.begin(), keys.end());
		for (const QRgb key : keys) { entries.push_back({qRed(key), qGreen(key), qBlue(key), exact.value(key)}); }
	} else {
		QVector<int> keys = buckets.keys(); std::sort(keys.begin(), keys.end());
		for (const int key : keys) {
			const auto& sum = buckets.value(key);
			entries.push_back({int((sum[0] + sum[3] / 2) / sum[3]), int((sum[1] + sum[3] / 2) / sum[3]), int((sum[2] + sum[3] / 2) / sum[3]), sum[3]});
		}
	}
	if (entries.isEmpty()) { return {qRgb(0, 0, 0)}; }
	struct Box { int begin = 0, end = 0; };
	QVector<Box> boxes{{0, int(entries.size())}};
	const auto range = [&](const Box& box, int channel) {
		int low = 255, high = 0;
		for (int i = box.begin; i < box.end; ++i) {
			const int value = channel == 0 ? entries[i].r : channel == 1 ? entries[i].g : entries[i].b;
			low = std::min(low, value); high = std::max(high, value);
		}
		return high - low;
	};
	while (boxes.size() < count) {
		if (!report(progress, rows + boxes.size() * rows / count, rows * 2)) { return {}; }
		int chosen = -1, axis = 0, widest = 0;
		for (int index = 0; index < boxes.size(); ++index) {
			if (boxes[index].end - boxes[index].begin < 2) { continue; }
			for (int channel = 0; channel < 3; ++channel) {
				const int extent = range(boxes[index], channel);
				if (extent > widest) { widest = extent; chosen = index; axis = channel; }
			}
		}
		if (chosen < 0) { break; }
		const Box box = boxes[chosen];
		const auto key = [axis](const Entry& e) { return axis == 0 ? std::array<int, 3>{e.r, e.g, e.b} : axis == 1 ? std::array<int, 3>{e.g, e.r, e.b} : std::array<int, 3>{e.b, e.r, e.g}; };
		std::sort(entries.begin() + box.begin, entries.begin() + box.end, [&](const Entry& a, const Entry& b) { return key(a) < key(b); });
		qint64 total = 0;
		for (int i = box.begin; i < box.end; ++i) { total += entries[i].weight; }
		qint64 running = 0; int split = box.begin + 1;
		for (int i = box.begin; i < box.end - 1; ++i) { running += entries[i].weight; split = i + 1; if (running * 2 >= total) { break; } }
		boxes[chosen] = {box.begin, split}; boxes.push_back({split, box.end});
	}
	QVector<QRgb> palette;
	for (const auto& box : boxes) {
		qint64 r = 0, g = 0, b = 0, weight = 0;
		for (int i = box.begin; i < box.end; ++i) { r += entries[i].r * entries[i].weight; g += entries[i].g * entries[i].weight; b += entries[i].b * entries[i].weight; weight += entries[i].weight; }
		palette.push_back(qRgb(int((r + weight / 2) / weight), int((g + weight / 2) / weight), int((b + weight / 2) / weight)));
	}
	std::sort(palette.begin(), palette.end(), [](QRgb a, QRgb b) { return luma(a) != luma(b) ? luma(a) < luma(b) : a < b; });
	palette.erase(std::unique(palette.begin(), palette.end()), palette.end());
	return report(progress, 1, 1) ? palette : QVector<QRgb>();
}

QByteArray encodeRawTexturePalette(const QVector<QRgb>& colors)
{
	QByteArray bytes; bytes.reserve(768);
	for (int index = 0; index < 256; ++index) { const QRgb color = colors.value(index); bytes += char(qRed(color)); bytes += char(qGreen(color)); bytes += char(qBlue(color)); }
	return bytes;
}

QByteArray encodeIdspSprite(const TextureSpriteEncoding& sprite)
{
	const bool halfLife = sprite.version == 2;
	int width = 0, height = 0;
	for (const auto& frame : sprite.frames) { width = std::max(width, frame.width()); height = std::max(height, frame.height()); }
	QByteArray bytes(halfLife ? 40 : 36, '\0');
	std::memcpy(bytes.data(), "IDSP", 4); put32(bytes, 4, quint32(sprite.version)); put32(bytes, 8, quint32(sprite.type));
	const qsizetype at = halfLife ? 16 : 12;
	if (halfLife) { put32(bytes, 12, quint32(sprite.texFormat)); }
	// sprgen: the radius of the largest frame's half extents.
	putFloat(bytes, at, float(std::sqrt(double((width >> 1) * (width >> 1) + (height >> 1) * (height >> 1)))));
	put32(bytes, at + 4, quint32(width)); put32(bytes, at + 8, quint32(height));
	put32(bytes, at + 12, quint32(sprite.frames.size())); putFloat(bytes, at + 16, 0.0f); put32(bytes, at + 20, quint32(sprite.syncType));
	if (halfLife) {
		QByteArray count(2, '\0'); put16(count, 0, 256); bytes += count;
		for (int index = 0; index < 256; ++index) {
			const QRgb color = sprite.palette.value(index); bytes += char(qRed(color)); bytes += char(qGreen(color)); bytes += char(qBlue(color));
		}
	}
	for (qsizetype index = 0; index < sprite.frames.size(); ++index) {
		const auto& frame = sprite.frames[index]; const QPoint origin = sprite.origins.value(index);
		QByteArray header(20, '\0'); // SPR_SINGLE, then dspriteframe_t
		put32(header, 4, quint32(origin.x())); put32(header, 8, quint32(origin.y()));
		put32(header, 12, quint32(frame.width())); put32(header, 16, quint32(frame.height()));
		bytes += header; appendRows(bytes, frame);
	}
	return bytes;
}

QByteArray encodeSp2Sprite(const QVector<TextureSp2Frame>& frames)
{
	QByteArray bytes(12, '\0');
	std::memcpy(bytes.data(), "IDS2", 4); put32(bytes, 4, 2); put32(bytes, 8, quint32(frames.size()));
	for (const auto& frame : frames) {
		QByteArray entry(80, '\0');
		put32(entry, 0, quint32(frame.size.width())); put32(entry, 4, quint32(frame.size.height()));
		put32(entry, 8, quint32(frame.origin.x())); put32(entry, 12, quint32(frame.origin.y()));
		putName(entry, 16, frame.name, 64); bytes += entry;
	}
	return bytes;
}

QByteArray encodeWad3Miptex(const QVector<QImage>& mips, const QString& name, const QVector<QRgb>& palette)
{
	QByteArray bytes(40, '\0'); putName(bytes, 0, name, 16);
	put32(bytes, 16, quint32(mips.value(0).width())); put32(bytes, 20, quint32(mips.value(0).height()));
	for (int level = 0; level < 4 && level < mips.size(); ++level) { put32(bytes, 24 + level * 4, quint32(bytes.size())); appendRows(bytes, mips[level]); }
	QByteArray count(2, '\0'); put16(count, 0, 256); bytes += count;
	for (int index = 0; index < 256; ++index) { const QRgb color = palette.value(index); bytes += char(qRed(color)); bytes += char(qGreen(color)); bytes += char(qBlue(color)); }
	bytes += QByteArray(2, '\0'); // pads the lump to a four-byte boundary
	return bytes;
}

QByteArray encodeWad3Archive(const QByteArray& lump, const QString& name)
{
	QByteArray wad(12, '\0'); std::memcpy(wad.data(), "WAD3", 4); put32(wad, 4, 1); put32(wad, 8, quint32(12 + lump.size())); wad += lump;
	QByteArray directory(32, '\0'); put32(directory, 0, 12); put32(directory, 4, quint32(lump.size())); put32(directory, 8, quint32(lump.size()));
	directory[12] = char(0x43); putName(directory, 16, name, 16);
	return wad + directory;
}

QByteArray encodeSwl(const QVector<QImage>& mips, const QVector<QRgb>& palette, const TextureSwlFields& fields)
{
	// sinmiptex_t: name[64], width, height, palette[256][4], palcrc + pad,
	// offsets[4], animname[64], flags, contents, value, direct, then animation,
	// lighting and physics floats to byte 1236. VibeStudio writes zero for the
	// palette checksum and for the properties it does not author.
	QByteArray bytes(1236, '\0'); putName(bytes, 0, fields.name, 64);
	put32(bytes, 64, quint32(mips.value(0).width())); put32(bytes, 68, quint32(mips.value(0).height()));
	for (int index = 0; index < 256; ++index) {
		const QRgb color = palette.value(index);
		bytes[72 + index * 4] = char(qRed(color)); bytes[73 + index * 4] = char(qGreen(color)); bytes[74 + index * 4] = char(qBlue(color));
	}
	putName(bytes, 1116, fields.animationNext, 64); put32(bytes, 1180, fields.flags); put32(bytes, 1184, fields.contents);
	put16(bytes, 1188, quint16(fields.value));
	for (int level = 0; level < 4 && level < mips.size(); ++level) { put32(bytes, 1100 + level * 4, quint32(bytes.size())); appendRows(bytes, mips[level]); }
	return bytes;
}

QVector<QImage> textureRgbaMipChain(const QImage& source, const TextureProgress& progress)
{
	QVector<QImage> mips{source.convertToFormat(QImage::Format_ARGB32)};
	while (mips.last().width() > 1 || mips.last().height() > 1) {
		const QImage& previous = mips.last();
		QImage next(std::max(1, previous.width() / 2), std::max(1, previous.height() / 2), QImage::Format_ARGB32);
		if (next.isNull()) { return {}; }
		for (int y = 0; y < next.height(); ++y) {
			if (y % 64 == 0 && !report(progress, mips.size(), 16)) { return {}; }
			auto* row = reinterpret_cast<QRgb*>(next.scanLine(y));
			for (int x = 0; x < next.width(); ++x) {
				qint64 sum[4]{}; int samples = 0;
				for (int dy = 0; dy < 2; ++dy) {
					for (int dx = 0; dx < 2; ++dx) {
						const int sx = std::min(x * 2 + dx, previous.width() - 1), sy = std::min(y * 2 + dy, previous.height() - 1);
						const QRgb pixel = previous.pixel(sx, sy); const int alpha = qAlpha(pixel);
						sum[0] += qRed(pixel) * alpha; sum[1] += qGreen(pixel) * alpha; sum[2] += qBlue(pixel) * alpha; sum[3] += alpha; ++samples;
					}
				}
				const auto channel = [&](qint64 value) { return sum[3] ? int((value + sum[3] / 2) / sum[3]) : 0; };
				row[x] = qRgba(channel(sum[0]), channel(sum[1]), channel(sum[2]), int((sum[3] + samples / 2) / samples));
			}
		}
		mips << next;
	}
	return mips;
}

QByteArray encodeDdsMipmapped(const QVector<QImage>& mips, const TextureProgress& progress)
{
	if (mips.isEmpty()) { return {}; }
	const QImage& base = mips.first();
	QByteArray bytes(128, '\0');
	std::memcpy(bytes.data(), "DDS ", 4); put32(bytes, 4, 124);
	// CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT | MIPMAPCOUNT
	put32(bytes, 8, 0x2100f); put32(bytes, 12, quint32(base.height())); put32(bytes, 16, quint32(base.width()));
	put32(bytes, 20, quint32(base.width() * 4)); put32(bytes, 28, quint32(mips.size()));
	put32(bytes, 76, 32); put32(bytes, 80, 0x41); put32(bytes, 88, 32);
	put32(bytes, 92, 0x00ff0000); put32(bytes, 96, 0x0000ff00); put32(bytes, 100, 0x000000ff); put32(bytes, 104, 0xff000000);
	put32(bytes, 108, 0x401008); // TEXTURE | MIPMAP | COMPLEX
	for (qsizetype level = 0; level < mips.size(); ++level) {
		if (!report(progress, level, mips.size())) { return {}; }
		const QImage image = mips[level].convertToFormat(QImage::Format_ARGB32);
		for (int y = 0; y < image.height(); ++y) {
			const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
			for (int x = 0; x < image.width(); ++x) { bytes += char(qBlue(row[x])); bytes += char(qGreen(row[x])); bytes += char(qRed(row[x])); bytes += char(qAlpha(row[x])); }
		}
	}
	return bytes;
}

} // namespace vibestudio
