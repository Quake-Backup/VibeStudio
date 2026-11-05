#include "core/texture_export.h"
#include "core/extra_image.h"
#include "core/texture_formats.h"
#include "core/texture_doom_walls.h"

#include "core/package_staging.h"
#include "core/texture_output.h"
#include "core/deflate.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

namespace vibestudio {
namespace {
bool fail(QString* error, const QString& text) { if (error) { *error = text; } return false; }
bool checkpoint(const TextureProgress& progress, qint64 done, qint64 total, QString* error)
{
	return !progress || progress(done, total) || fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Texture export cancelled."));
}
bool isQuake(TextureExportFormat format) { return format == TextureExportFormat::QuakeMiptex || format == TextureExportFormat::QuakeWad2; }
bool isHalfLifeMip(TextureExportFormat format) { return format == TextureExportFormat::HalfLifeMiptex || format == TextureExportFormat::HalfLifeWad3; }
bool isSprite(TextureExportFormat format) { return format == TextureExportFormat::QuakeSprite || format == TextureExportFormat::HalfLifeSprite || format == TextureExportFormat::Quake2Sprite; }
bool isMipmapped(TextureExportFormat format) { return isQuake(format) || format == TextureExportFormat::Quake2Wal || isHalfLifeMip(format) || format == TextureExportFormat::SinSwl; }
bool isIndexed(TextureExportFormat format)
{
	return format != TextureExportFormat::Png && format != TextureExportFormat::Targa && format != TextureExportFormat::Dds &&
		format != TextureExportFormat::Ftx && format != TextureExportFormat::DdsMipmapped;
}
// Formats that store their own palette with the pixels.
bool embedsPalette(TextureExportFormat format) { return isHalfLifeMip(format) || format == TextureExportFormat::HalfLifeSprite || format == TextureExportFormat::SinSwl; }
bool usesSelectedPalette(const TextureExportOptions& options) { return isIndexed(options.format) && !(embedsPalette(options.format) && options.imagePalette); }
constexpr int kSpriteIndexAlpha = 2, kSpriteAlphaTest = 3;
// Index 255 marks transparent texels: sprites, SWL, and Half-Life textures
// named with a leading '{'. WAL reserves it without storing transparency.
bool transparentIndex(const TextureExportOptions& options)
{
	if (options.format == TextureExportFormat::HalfLifeSprite) { return options.spriteTexture == kSpriteAlphaTest; }
	return options.format == TextureExportFormat::QuakeSprite || options.format == TextureExportFormat::Quake2Sprite ||
		options.format == TextureExportFormat::SinSwl || (isHalfLifeMip(options.format) && options.name.startsWith(QLatin1Char('{')));
}
bool reservesIndex255(const TextureExportOptions& options) { return options.format == TextureExportFormat::Quake2Wal || transparentIndex(options); }
enum class AlphaKind { Opaque, Binary, Full };
AlphaKind alphaKind(const TextureExportOptions& options)
{
	switch (options.format) {
	case TextureExportFormat::Png: case TextureExportFormat::IndexedPng: case TextureExportFormat::Targa:
	case TextureExportFormat::Dds: case TextureExportFormat::Ftx: case TextureExportFormat::DdsMipmapped: return AlphaKind::Full;
	case TextureExportFormat::DoomPatch: return AlphaKind::Binary;
	case TextureExportFormat::HalfLifeSprite: return options.spriteTexture == kSpriteIndexAlpha ? AlphaKind::Full : (transparentIndex(options) ? AlphaKind::Binary : AlphaKind::Opaque);
	default: return transparentIndex(options) ? AlphaKind::Binary : AlphaKind::Opaque;
	}
}
const QStringList& spriteTypeIds()
{
	static const QStringList ids{QStringLiteral("parallel-upright"), QStringLiteral("facing-upright"), QStringLiteral("parallel"), QStringLiteral("oriented"), QStringLiteral("parallel-oriented")};
	return ids;
}
const QStringList& spriteTextureIds()
{
	static const QStringList ids{QStringLiteral("normal"), QStringLiteral("additive"), QStringLiteral("index-alpha"), QStringLiteral("alpha-test")};
	return ids;
}
QString alphaId(TextureExportAlpha mode)
{
	switch (mode) { case TextureExportAlpha::Strict: return QStringLiteral("strict"); case TextureExportAlpha::Matte: return QStringLiteral("matte"); case TextureExportAlpha::Threshold: return QStringLiteral("threshold"); }
	return {};
}
QString fullbrightId(TextureFullbrightMode mode)
{
	switch (mode) { case TextureFullbrightMode::Preserve: return QStringLiteral("preserve"); case TextureFullbrightMode::Exclude: return QStringLiteral("exclude"); case TextureFullbrightMode::Allow: return QStringLiteral("allow"); }
	return {};
}
QString mipFilterId(TextureMipmapFilter mode)
{
	switch (mode) { case TextureMipmapFilter::Box: return QStringLiteral("box"); case TextureMipmapFilter::Nearest: return QStringLiteral("nearest"); }
	return {};
}
void put16(QByteArray& bytes, qsizetype at, quint16 value) { qToLittleEndian(value, bytes.data() + at); }
void put32(QByteArray& bytes, qsizetype at, quint32 value) { qToLittleEndian(value, bytes.data() + at); }
void putName(QByteArray& bytes, qsizetype at, const QString& name) { const auto encoded = name.toLatin1(); std::memcpy(bytes.data() + at, encoded.constData(), size_t(encoded.size())); }
QByteArray paletteRgb(const IdTechPalette& palette)
{
	QByteArray bytes; bytes.reserve(768);
	for (int i = 0; i < 256; ++i) { const QRgb color = palette.colorAt(i); bytes += char(qRed(color)); bytes += char(qGreen(color)); bytes += char(qBlue(color)); }
	return bytes;
}
bool validName(const QString& name, int limit, bool path, bool optional = false)
{
	if (name.isEmpty()) { return optional; }
	if (name.size() > limit || name == QStringLiteral(".") || name == QStringLiteral("..")) { return false; }
	for (const auto character : name) {
		const ushort c = character.unicode();
		const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
		if (!alnum && !QStringLiteral("_+-!~{}*.").contains(character) && !(path && c == '/')) { return false; }
	}
	if (path) { const auto normalized = normalizePackageVirtualPath(name, false); return normalized.isSafe() && normalized.normalizedPath == name; }
	return true;
}
bool validate(const QImage& image, const TextureExportOptions& options, const IdTechPaletteResolution& palette, QString* error)
{
	if (image.isNull()) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Cannot export an empty texture.")); }
	if (!validTextureSize(image.size(), error)) { return false; }
	if (image.format() == QImage::Format_Indexed8) {
		TextureDocument validated;
		if (!validated.reset(image, error)) { return false; }
	}
	if (textureExportFormatId(options.format).isEmpty() || alphaId(options.alpha).isEmpty() || fullbrightId(options.fullbright).isEmpty() || mipFilterId(options.mipFilter).isEmpty() ||
		!options.matte.isValid() || options.matte.alpha() != 255 || options.alphaThreshold < 1 || options.alphaThreshold > 255 ||
		options.leftOffset < -32768 || options.leftOffset > 32767 || options.topOffset < -32768 || options.topOffset > 32767) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Invalid export profile, alpha mode, matte, threshold, mipmap filter, or patch offsets."));
	}
	if (options.spriteType < 0 || options.spriteType >= spriteTypeIds().size() || options.spriteTexture < 0 || options.spriteTexture >= spriteTextureIds().size()) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Invalid sprite orientation or Half-Life sprite texture format."));
	}
	if (usesSelectedPalette(options)) {
		if (!palette.palette.isValid()) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "This export profile needs a resolved 256-color palette.")); }
		if (palette.palette.generated && !options.allowGeneratedPalette) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "The selected palette is a generated stand-in. Resolve a game palette or explicitly allow generated colors.")); }
	}
	if (isMipmapped(options.format) && (image.width() % 16 || image.height() % 16)) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Mipmapped game textures require width and height divisible by 16.")); }
	if (isQuake(options.format)) {
		if (!validName(options.name, 15, false)) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Quake texture names need 1–15 ASCII letters, digits, or texture-name punctuation, without folders.")); }
		if (!options.extendedLimits && qint64(image.width()) * image.height() * 85 / 64 + 40 > 2 * 1024 * 1024) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "This texture exceeds the original Quake compiler's entire 2 MiB texture lump. Enable source-port limits for a compatible compiler and engine.")); }
		if (options.name.startsWith(QStringLiteral("sky")) && !options.extendedLimits && image.size() != QSize(256, 128)) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Classic Quake sky textures require a 256 × 128 canvas.")); }
	}
	if (options.format == TextureExportFormat::Quake2Wal) {
		if (!validName(options.name, 31, true) || !validName(options.animationNext, 31, true, true)) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "WAL texture and animation names must be safe relative ASCII names of at most 31 characters.")); }
		if (!options.extendedLimits && qint64(image.width()) * image.height() > 512 * 256) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "The original Quake II renderer accepts at most 131,072 indexed pixels. Enable source-port limits only for a compatible target.")); }
	}
	if (isHalfLifeMip(options.format)) {
		if (!validName(options.name, 15, false)) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Half-Life texture names need 1–15 ASCII letters, digits, or texture-name punctuation, without folders.")); }
	}
	if (options.format == TextureExportFormat::SinSwl) {
		if (!validName(options.name, 63, true) || !validName(options.animationNext, 63, true, true)) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "SWL texture and animation names must be safe relative ASCII names of at most 63 characters.")); }
		if (options.surfaceValue < -32768 || options.surfaceValue > 32767) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "SWL stores the surface value in 16 bits: choose -32768 to 32767.")); }
	}
	if (options.format == TextureExportFormat::Quake2Sprite && !validName(options.name, 55, true)) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Quake II sprite frames are named from the native name: a safe relative path of at most 55 characters, such as sprites/flame."));
	}
	if (!options.wallTexture.isEmpty() && (!validName(options.wallTexture, 8, false) || options.wallTexture.contains(QLatin1Char('.')))) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "A Doom wall texture name needs 1–8 lump-name characters."));
	}
	if (options.format == TextureExportFormat::DoomFlat && image.size() != QSize(64, 64)) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Doom flats require exactly 64 × 64 pixels.")); }
	if (options.format == TextureExportFormat::DoomPatch && !options.extendedLimits && image.height() > 255) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Classic Doom patches are limited to 255 rows. Enable source-port limits to write tall patch posts.")); }
	const bool alphaSupported = alphaKind(options) != AlphaKind::Opaque;
	if (options.alpha == TextureExportAlpha::Threshold && !alphaSupported) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "This opaque profile cannot use alpha thresholding. Choose a matte or supply an opaque image.")); }
	return true;
}

QImage prepareAlpha(const QImage& source, const TextureExportOptions& options, qint64* changed, QString* error, const TextureProgress& progress)
{
	QImage image = source.convertToFormat(QImage::Format_ARGB32);
	if (image.isNull()) { fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Unable to allocate export pixels.")); return {}; }
	const bool opaque = alphaKind(options) == AlphaKind::Opaque, binary = alphaKind(options) == AlphaKind::Binary;
	for (int y = 0; y < image.height(); ++y) {
		if (!checkpoint(progress, y, image.height(), error)) { return {}; }
		auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (int x = 0; x < image.width(); ++x) {
			const int alpha = qAlpha(row[x]);
			if (options.alpha == TextureExportAlpha::Strict) {
				if ((opaque && alpha != 255) || (binary && alpha != 0 && alpha != 255)) {
					fail(error, QCoreApplication::translate("VibeStudioTextureExport", "The image contains alpha that this profile cannot store. Choose a matte, or a binary threshold for a profile with transparency.")); return {};
				}
			} else if (options.alpha == TextureExportAlpha::Matte) {
				const auto blend = [alpha](int value, int background) { return (value * alpha + background * (255 - alpha) + 127) / 255; };
				row[x] = qRgb(blend(qRed(row[x]), options.matte.red()), blend(qGreen(row[x]), options.matte.green()), blend(qBlue(row[x]), options.matte.blue()));
			} else { row[x] = (row[x] & 0x00ffffffu) | (alpha >= options.alphaThreshold ? 0xff000000u : 0u); }
			if (qAlpha(row[x]) != alpha) { ++*changed; }
		}
	}
	return checkpoint(progress, 1, 1, error) ? image : QImage();
}

QImage quantize(const QImage& source, const QImage& prepared, const TextureExportOptions& options, const IdTechPalette& palette, bool* preserved, QString* error, const TextureProgress& progress)
{
	IdTechPalette effective = palette;
	const bool indexedPng = options.format == TextureExportFormat::IndexedPng;
	const int fullbright = isQuake(options.format) ? 224 : 256;
	const int limit = reservesIndex255(options) ? 255 : 256;
	for (int i = 0; i < 256; ++i) { effective.colors[i] = indexedPng ? palette.colorAt(i) : (palette.colorAt(i) | 0xff000000u); }
	if (!indexedPng) { effective.transparentIndex = -1; }
	const auto storedTable = effective.colors;
	const bool sourceIndexed = source.format() == QImage::Format_Indexed8 && source.colorCount() > 0 && source.colorCount() <= 256;
	bool matching[256]{};
	for (int index = 0; sourceIndexed && index < source.colorCount(); ++index) {
		matching[index] = index < limit && (options.fullbright != TextureFullbrightMode::Exclude || index < fullbright) &&
			(indexedPng ? source.color(index) == storedTable[index] : (source.color(index) & 0x00ffffffu) == (storedTable[index] & 0x00ffffffu));
	}
	const auto unchanged = [&](int index, QRgb preparedPixel) {
		return matching[index] && (indexedPng ? preparedPixel == storedTable[index] : (preparedPixel & 0x00ffffffu) == (storedTable[index] & 0x00ffffffu));
	};
	bool exact = sourceIndexed;
	if (exact) {
		for (int y = 0; y < source.height() && exact; ++y) {
			if (!checkpoint(progress, y, source.height() * 4, error)) { return {}; }
			const auto* row = source.constScanLine(y); const auto* rgba = reinterpret_cast<const QRgb*>(prepared.constScanLine(y));
			for (int x = 0; x < source.width(); ++x) { if (!unchanged(row[x], rgba[x])) { exact = false; break; } }
		}
	}
	if (exact) { QImage image = source; image.setColorTable(storedTable); *preserved = true; return checkpoint(progress, 1, 1, error) ? image : QImage(); }
	// Duplicate disallowed colors at the first entry. The common quantizer's
	// stable lowest-index tie break then excludes them without a second algorithm.
	const int candidates = options.fullbright == TextureFullbrightMode::Allow ? limit : std::min(limit, fullbright);
	for (int index = candidates; index < 256; ++index) { effective.colors[index] = effective.colors[0]; }
	QImage image = quantizeToIdTechPalette(prepared, effective, options.dither, [&](int done, int total) { return checkpoint(progress, total + done * 2, total * 4, error); });
	if (image.isNull()) { if (error && error->isEmpty()) { *error = QCoreApplication::translate("VibeStudioTextureExport", "Unable to quantize export pixels."); } return {}; }
	image.setColorTable(storedTable);
	for (int y = 0; y < image.height(); ++y) {
		if (!checkpoint(progress, image.height() * 3 + y, image.height() * 4, error)) { return {}; }
		const auto* rgba = reinterpret_cast<const QRgb*>(prepared.constScanLine(y)); auto* indices = image.scanLine(y);
		const auto* original = sourceIndexed ? source.constScanLine(y) : nullptr;
		for (int x = 0; x < image.width(); ++x) {
			if (original && unchanged(original[x], rgba[x])) { indices[x] = original[x]; }
			if (indexedPng && qAlpha(storedTable[indices[x]]) != qAlpha(rgba[x])) { fail(error, QCoreApplication::translate("VibeStudioTextureExport", "The selected palette cannot preserve this image's alpha. Choose a compatible palette, a matte, or RGBA PNG.")); return {}; }
		}
	}
	return checkpoint(progress, 1, 1, error) ? image : QImage();
}

// `limit` excludes reserved indices from averaged colours. With a transparent
// index, a texel is transparent when at least two of its four samples are.
QImage boxMip(const QImage& source, bool quake, TextureFullbrightMode mode, int limit, int transparent, QString* error, const TextureProgress& progress)
{
	QImage image(source.size() / 2, QImage::Format_Indexed8); image.setColorTable(source.colorTable());
	if (image.isNull()) { fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Unable to allocate mipmap pixels.")); return {}; }
	QHash<quint32, uchar> cache;
	for (int y = 0; y < image.height(); ++y) {
		if (!checkpoint(progress, y, image.height(), error)) { return {}; }
		auto* output = image.scanLine(y);
		for (int x = 0; x < image.width(); ++x) {
			const int indices[]{source.pixelIndex(x * 2, y * 2), source.pixelIndex(x * 2 + 1, y * 2), source.pixelIndex(x * 2, y * 2 + 1), source.pixelIndex(x * 2 + 1, y * 2 + 1)};
			if (std::all_of(std::begin(indices) + 1, std::end(indices), [&](int value) { return value == indices[0]; })) { output[x] = uchar(indices[0]); continue; }
			if (transparent >= 0 && std::count(std::begin(indices), std::end(indices), transparent) >= 2) { output[x] = uchar(transparent); continue; }
			const bool bright = quake && mode != TextureFullbrightMode::Exclude && std::any_of(std::begin(indices), std::end(indices), [](int value) { return value >= 224; });
			int red = 0, green = 0, blue = 0, count = 0;
			for (int index : indices) {
				if ((bright && index < 224) || index == transparent) { continue; }
				const QRgb c = source.color(index); red += qRed(c); green += qGreen(c); blue += qBlue(c); ++count;
			}
			red = (red + count / 2) / count; green = (green + count / 2) / count; blue = (blue + count / 2) / count;
			const quint32 key = quint32(qRgb(red, green, blue) & 0x00ffffffu) | (bright ? 0x01000000u : 0u);
			const auto found = cache.constFind(key); if (found != cache.constEnd()) { output[x] = found.value(); continue; }
			const int begin = bright ? 224 : 0, end = quake ? (bright ? 256 : 224) : limit;
			int best = begin, distance = std::numeric_limits<int>::max();
			for (int index = begin; index < end; ++index) {
				const QRgb c = source.color(index); const int dr = red - qRed(c), dg = green - qGreen(c), db = blue - qBlue(c);
				const int candidate = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
				if (candidate < distance) { best = index; distance = candidate; if (distance == 0) { break; } }
			}
			output[x] = uchar(best); if (cache.size() < 65536) { cache.insert(key, uchar(best)); }
		}
	}
	return checkpoint(progress, 1, 1, error) ? image : QImage();
}

QByteArray packedRows(const QImage& image)
{
	QByteArray bytes; bytes.resize(qsizetype(image.width()) * image.height());
	for (int y = 0; y < image.height(); ++y) { std::memcpy(bytes.data() + y * image.width(), image.constScanLine(y), size_t(image.width())); }
	return bytes;
}

// Original PNG 3 sections 5, 9–11 implementation (W3C, 2025-06-24), without
// specification sample code. Explicit color type 3 retains grayscale palettes.
QByteArray encodeIndexedPng(const QImage& image, QString* error, const TextureProgress& progress)
{
	QByteArray rows; rows.reserve((image.width() + 1) * image.height());
	for (int y = 0; y < image.height(); ++y) {
		if (!checkpoint(progress, y, image.height() * 2, error)) { return {}; }
		rows += '\0'; rows.append(reinterpret_cast<const char*>(image.constScanLine(y)), image.width());
	}
	if (!checkpoint(progress, 1, 2, error)) { return {}; }
	const auto compressed = qCompress(rows, 6);
	if (compressed.size() <= 4) { fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Unable to compress indexed PNG pixels.")); return {}; }
	QByteArray bytes = QByteArray::fromHex("89504e470d0a1a0a");
	const auto chunk = [&](const QByteArray& type, const QByteArray& payload) {
		QByteArray size(4, '\0'); qToBigEndian(quint32(payload.size()), size.data()); bytes += size; bytes += type; bytes += payload;
		qToBigEndian(crc32Bytes(payload, crc32Bytes(type)), size.data()); bytes += size;
	};
	QByteArray header(13, '\0'); qToBigEndian(quint32(image.width()), header.data()); qToBigEndian(quint32(image.height()), header.data() + 4);
	header[8] = 8; header[9] = 3; chunk(QByteArrayLiteral("IHDR"), header);
	QByteArray colors, alpha; bool transparent = false;
	for (QRgb color : image.colorTable()) { colors += char(qRed(color)); colors += char(qGreen(color)); colors += char(qBlue(color)); alpha += char(qAlpha(color)); transparent |= qAlpha(color) != 255; }
	chunk(QByteArrayLiteral("PLTE"), colors); if (transparent) { chunk(QByteArrayLiteral("tRNS"), alpha); }
	chunk(QByteArrayLiteral("IDAT"), compressed.mid(4)); chunk(QByteArrayLiteral("IEND"), {});
	return checkpoint(progress, 1, 1, error) ? bytes : QByteArray();
}

// Reimplemented layouts, not copied engine code. GPL-2.0-or-later references:
// id-Software/Quake WinQuake/bspfile.h and wad.h; Quake-2 qcommon/qfiles.h;
// Quake-III-Arena code/renderer/tr_image.c. Reviewed 2026-10-04; docs/CREDITS.md.
QByteArray encodeMip(const QVector<QImage>& mips, const TextureExportOptions& options)
{
	const bool wal = options.format == TextureExportFormat::Quake2Wal;
	QByteArray bytes(wal ? 100 : 40, '\0'); putName(bytes, 0, options.name);
	put32(bytes, wal ? 32 : 16, quint32(mips[0].width())); put32(bytes, wal ? 36 : 20, quint32(mips[0].height()));
	if (wal) { putName(bytes, 56, options.animationNext); put32(bytes, 88, options.surfaceFlags); put32(bytes, 92, options.contentFlags); put32(bytes, 96, quint32(options.surfaceValue)); }
	for (int level = 0; level < 4; ++level) { put32(bytes, (wal ? 40 : 24) + level * 4, quint32(bytes.size())); bytes += packedRows(mips[level]); }
	if (options.format != TextureExportFormat::QuakeWad2) { return bytes; }
	QByteArray wad(12, '\0'); std::memcpy(wad.data(), "WAD2", 4); put32(wad, 4, 1); put32(wad, 8, quint32(12 + bytes.size())); wad += bytes;
	QByteArray directory(32, '\0'); put32(directory, 0, 12); put32(directory, 4, quint32(bytes.size())); put32(directory, 8, quint32(bytes.size())); directory[12] = char(0x44); putName(directory, 16, options.name);
	return wad + directory;
}
QByteArray encodeTarga(const QImage& image, QString* error, const TextureProgress& progress)
{
	QByteArray bytes(18, '\0'); bytes[2] = 2; put16(bytes, 12, quint16(image.width())); put16(bytes, 14, quint16(image.height())); bytes[16] = 32; bytes[17] = 8;
	bytes.reserve(18 + image.width() * image.height() * 4 + 26);
	// Bottom-left origin works with original Quake II/III readers, which ignore
	// or reject top-origin images. Descriptor 8 explicitly declares alpha.
	for (int row = 0; row < image.height(); ++row) {
		if (!checkpoint(progress, row, image.height(), error)) { return {}; }
		const auto* pixels = reinterpret_cast<const QRgb*>(image.constScanLine(image.height() - 1 - row));
		for (int x = 0; x < image.width(); ++x) { bytes += char(qBlue(pixels[x])); bytes += char(qGreen(pixels[x])); bytes += char(qRed(pixels[x])); bytes += char(qAlpha(pixels[x])); }
	}
	bytes += QByteArray(8, '\0'); bytes += QByteArray("TRUEVISION-XFILE.\0", 18);
	return checkpoint(progress, 1, 1, error) ? bytes : QByteArray();
}
QByteArray encodePcx(const QImage& image, const IdTechPalette& palette, QString* error, const TextureProgress& progress)
{
	const int stride = (image.width() + 1) & ~1;
	QByteArray bytes(128, '\0'); bytes[0] = 10; bytes[1] = 5; bytes[2] = 1; bytes[3] = 8;
	put16(bytes, 8, quint16(image.width() - 1)); put16(bytes, 10, quint16(image.height() - 1)); put16(bytes, 12, 72); put16(bytes, 14, 72); bytes[65] = 1; put16(bytes, 66, quint16(stride)); put16(bytes, 68, 1);
	for (int y = 0; y < image.height(); ++y) {
		if (!checkpoint(progress, y, image.height(), error)) { return {}; }
		const auto* row = image.constScanLine(y);
		for (int x = 0; x < stride;) {
			const uchar value = x < image.width() ? row[x] : 0; int run = 1;
			while (run < 63 && x + run < stride && (x + run < image.width() ? row[x + run] : 0) == value) { ++run; }
			if (run > 1 || value >= 0xc0) { bytes += char(0xc0 | run); } bytes += char(value); x += run;
		}
	}
	bytes += char(12); bytes += paletteRgb(palette);
	return checkpoint(progress, 1, 1, error) ? bytes : QByteArray();
}

QByteArray encodePatch(const QImage& indexed, const QImage& alpha, const TextureExportOptions& options, QString* error, const TextureProgress& progress)
{
	// Doom's layout: Chocolate Doom 3.1.0 src/v_patch.h (GPL-2.0-or-later).
	// Relative top deltas: GZDoom src/common/textures/formats/patchtexture.cpp
	// (BSD-3-Clause), reviewed 2026-10-04. Layout/behavior only; no code copied.
	// Zero-length posts advance the origin without drawing any pixel.
	QByteArray bytes(8 + indexed.width() * 4, '\0'); put16(bytes, 0, quint16(indexed.width())); put16(bytes, 2, quint16(indexed.height()));
	put16(bytes, 4, quint16(options.leftOffset)); put16(bytes, 6, quint16(options.topOffset));
	for (int x = 0; x < indexed.width(); ++x) {
		if (!checkpoint(progress, x, indexed.width(), error)) { return {}; }
		put32(bytes, 8 + x * 4, quint32(bytes.size())); int previous = -1;
		const auto emptyPost = [&](int delta) { bytes += char(delta); bytes += QByteArray(3, '\0'); };
		for (int y = 0; y < indexed.height();) {
			if (qAlpha(alpha.pixel(x, y)) == 0) { ++y; continue; }
			int length = 1;
			while (length < 255 && y + length < indexed.height() && qAlpha(alpha.pixel(x, y + length)) != 0) { ++length; }
			int delta = y;
			if (y >= 255) {
				if (previous < 254) { emptyPost(254); previous = 254; }
				while (y - previous > 254) { emptyPost(254); previous += 254; }
				delta = y - previous;
			}
			bytes += char(delta); bytes += char(length); bytes += char(0);
			for (int row = 0; row < length; ++row) { bytes += char(indexed.pixelIndex(x, y + row)); }
			bytes += char(0); previous = y; y += length;
		}
		bytes += char(255);
	}
	return checkpoint(progress, 1, 1, error) ? bytes : QByteArray();
}
} // namespace

QVector<TextureExportProfile> textureExportProfiles()
{
	return {
		{TextureExportFormat::Png, QStringLiteral("png"), QCoreApplication::translate("VibeStudioTextureExport", "PNG · RGBA"), QStringLiteral("png"), QCoreApplication::translate("VibeStudioTextureExport", "Lossless color and alpha. Requires a PNG-capable target engine."), false, false},
		{TextureExportFormat::IndexedPng, QStringLiteral("png-indexed"), QCoreApplication::translate("VibeStudioTextureExport", "PNG · game palette"), QStringLiteral("png"), QCoreApplication::translate("VibeStudioTextureExport", "Indexed PNG with an embedded palette. Alpha must be representable by that palette."), true, false},
		{TextureExportFormat::Targa, QStringLiteral("tga"), QCoreApplication::translate("VibeStudioTextureExport", "Targa · RGBA"), QStringLiteral("tga"), QCoreApplication::translate("VibeStudioTextureExport", "32-bit BGRA, bottom-left origin, explicit alpha; suitable for Quake III image references."), false, false},
		{TextureExportFormat::Dds, QStringLiteral("dds"), QStringLiteral("DDS · BGRA8"), QStringLiteral("dds"), QCoreApplication::translate("VibeStudioTextureExport", "Uncompressed 32-bit color and alpha, one surface without mipmaps. Requires a DDS-capable target."), false, false},
		{TextureExportFormat::Ftx, QStringLiteral("ftx"), QStringLiteral("FTX · RGBA8"), QStringLiteral("ftx"), QCoreApplication::translate("VibeStudioTextureExport", "Lossless 32-bit color and alpha for FTX-capable tools and engines."), false, false},
		{TextureExportFormat::Pcx, QStringLiteral("pcx"), QCoreApplication::translate("VibeStudioTextureExport", "PCX · opaque palette"), QStringLiteral("pcx"), QCoreApplication::translate("VibeStudioTextureExport", "8-bit RLE with an embedded palette and even row stride. Original Quake II has additional size and row-stride limits."), true, false},
		{TextureExportFormat::QuakeMiptex, QStringLiteral("quake-miptex"), QCoreApplication::translate("VibeStudioTextureExport", "Quake · miptexture"), QStringLiteral("mip"), QCoreApplication::translate("VibeStudioTextureExport", "Four opaque indexed mip levels; dimensions divisible by 16. The game supplies the palette."), true, true},
		{TextureExportFormat::QuakeWad2, QStringLiteral("quake-wad2"), QCoreApplication::translate("VibeStudioTextureExport", "Quake · WAD2 texture"), QStringLiteral("wad"), QCoreApplication::translate("VibeStudioTextureExport", "One miptexture in a WAD2 archive, with lump type 0x44 and four mip levels."), true, true},
		{TextureExportFormat::Quake2Wal, QStringLiteral("quake2-wal"), QCoreApplication::translate("VibeStudioTextureExport", "Quake II · WAL"), QStringLiteral("wal"), QCoreApplication::translate("VibeStudioTextureExport", "Four opaque mips with animation and surface metadata. Index 255 is reserved; classic output is limited to 131,072 pixels."), true, true},
		{TextureExportFormat::DoomFlat, QStringLiteral("doom-flat"), QCoreApplication::translate("VibeStudioTextureExport", "Doom · flat"), QStringLiteral("lmp"), QCoreApplication::translate("VibeStudioTextureExport", "Raw 64 × 64 opaque indexed pixels. Place between flat namespace markers in a Doom WAD."), true, false},
		{TextureExportFormat::DoomPatch, QStringLiteral("doom-patch"), QCoreApplication::translate("VibeStudioTextureExport", "Doom · patch"), QStringLiteral("lmp"), QCoreApplication::translate("VibeStudioTextureExport", "Column posts with binary transparency and signed offsets. Tall patches require compatible source ports."), true, false},
		{TextureExportFormat::DdsMipmapped, QStringLiteral("dds-mipmaps"), QCoreApplication::translate("VibeStudioTextureExport", "DDS · BGRA8 with mipmaps"), QStringLiteral("dds"), QCoreApplication::translate("VibeStudioTextureExport", "Uncompressed 32-bit color and alpha with an area-averaged mip chain down to 1 × 1. Requires a DDS-capable target."), false, false},
		{TextureExportFormat::SinSwl, QStringLiteral("sin-swl"), QCoreApplication::translate("VibeStudioTextureExport", "SiN · SWL"), QStringLiteral("swl"), QCoreApplication::translate("VibeStudioTextureExport", "Four indexed mips with their own palette, animation name and surface flags. Index 255 is transparent."), true, true},
		{TextureExportFormat::HalfLifeMiptex, QStringLiteral("halflife-miptex"), QCoreApplication::translate("VibeStudioTextureExport", "Half-Life · miptexture"), QStringLiteral("mip"), QCoreApplication::translate("VibeStudioTextureExport", "Four indexed mips with their own palette; dimensions divisible by 16. A name starting with { makes index 255 transparent."), true, true},
		{TextureExportFormat::HalfLifeWad3, QStringLiteral("halflife-wad3"), QCoreApplication::translate("VibeStudioTextureExport", "Half-Life · WAD3 texture"), QStringLiteral("wad"), QCoreApplication::translate("VibeStudioTextureExport", "One miptexture and its palette in a new WAD3 archive, lump type 0x43."), true, true},
		{TextureExportFormat::QuakeSprite, QStringLiteral("quake-spr"), QCoreApplication::translate("VibeStudioTextureExport", "Quake · sprite"), QStringLiteral("spr"), QCoreApplication::translate("VibeStudioTextureExport", "Each visible layer is one frame, indexed with the game palette. Index 255 is transparent."), true, false, true},
		{TextureExportFormat::HalfLifeSprite, QStringLiteral("halflife-spr"), QCoreApplication::translate("VibeStudioTextureExport", "Half-Life · sprite"), QStringLiteral("spr"), QCoreApplication::translate("VibeStudioTextureExport", "Each visible layer is one frame, sharing one embedded palette and a Half-Life texture format."), true, false, true},
		{TextureExportFormat::Quake2Sprite, QStringLiteral("quake2-sp2"), QCoreApplication::translate("VibeStudioTextureExport", "Quake II · sprite"), QStringLiteral("sp2"), QCoreApplication::translate("VibeStudioTextureExport", "An SP2 naming one PCX per visible layer; the frames are written or staged beside it. Index 255 is transparent."), true, false, true}
	};
}
QString textureExportFormatId(TextureExportFormat format) { for (const auto& profile : textureExportProfiles()) { if (profile.format == format) { return profile.id; } } return {}; }
QString textureExportSuffix(TextureExportFormat format) { for (const auto& profile : textureExportProfiles()) { if (profile.format == format) { return profile.suffix; } } return {}; }
bool textureExportFormatFromId(const QString& id, TextureExportFormat* format) { for (const auto& profile : textureExportProfiles()) { if (profile.id == id) { if (format) { *format = profile.format; } return true; } } return false; }

QJsonObject textureExportOptionsJson(const TextureExportOptions& options)
{
	return {{QStringLiteral("version"), 1}, {QStringLiteral("profile"), textureExportFormatId(options.format)}, {QStringLiteral("alpha"), alphaId(options.alpha)},
		{QStringLiteral("matte"), options.matte.name(QColor::HexArgb)}, {QStringLiteral("alphaThreshold"), options.alphaThreshold}, {QStringLiteral("dither"), options.dither},
		{QStringLiteral("allowGeneratedPalette"), options.allowGeneratedPalette}, {QStringLiteral("extendedLimits"), options.extendedLimits}, {QStringLiteral("mipFilter"), mipFilterId(options.mipFilter)},
		{QStringLiteral("fullbright"), fullbrightId(options.fullbright)}, {QStringLiteral("name"), options.name}, {QStringLiteral("animationNext"), options.animationNext},
		{QStringLiteral("surfaceFlags"), double(options.surfaceFlags)}, {QStringLiteral("contentFlags"), double(options.contentFlags)}, {QStringLiteral("surfaceValue"), options.surfaceValue},
		{QStringLiteral("leftOffset"), options.leftOffset}, {QStringLiteral("topOffset"), options.topOffset},
		{QStringLiteral("spriteType"), spriteTypeIds().value(options.spriteType)}, {QStringLiteral("spriteTexture"), spriteTextureIds().value(options.spriteTexture)},
		{QStringLiteral("spriteRandomSync"), options.spriteRandomSync}, {QStringLiteral("centeredOrigin"), options.centeredOrigin},
		{QStringLiteral("embeddedPalette"), options.imagePalette ? QStringLiteral("image") : QStringLiteral("selected")}, {QStringLiteral("doomSprite"), options.doomSprite}, {QStringLiteral("wallTexture"), options.wallTexture}};
}
TextureExportOptions textureExportOptionsForImage(const IdTechImageDecodeResult& decoded)
{
	TextureExportOptions options;
	textureExportFormatFromId(decoded.formatId == QStringLiteral("targa") ? QStringLiteral("tga") : decoded.formatId, &options.format);
	if (!decoded.textureName.isEmpty()) { options.name = decoded.textureName; }
	options.animationNext = decoded.animationNextName; options.surfaceFlags = decoded.surfaceFlags;
	options.contentFlags = decoded.contentFlags; options.surfaceValue = decoded.surfaceValue;
	options.leftOffset = decoded.leftOffset; options.topOffset = decoded.topOffset;
	// Sprites keep their first frame's origin, converted to top-left offsets.
	const auto frameSize = decoded.frames.isEmpty() ? QSize(decoded.width, decoded.height) : decoded.frames.first().image.size();
	if (decoded.formatId == QStringLiteral("quake-spr") || decoded.formatId == QStringLiteral("halflife-spr")) {
		options.format = decoded.formatId == QStringLiteral("quake-spr") ? TextureExportFormat::QuakeSprite : TextureExportFormat::HalfLifeSprite;
		options.leftOffset = -decoded.leftOffset; options.topOffset = decoded.topOffset; options.centeredOrigin = false;
		if (options.format == TextureExportFormat::HalfLifeSprite && decoded.hasTransparency) { options.spriteTexture = kSpriteAlphaTest; }
	} else if (decoded.formatId == QStringLiteral("quake2-sp2")) {
		options.format = TextureExportFormat::Quake2Sprite;
		options.leftOffset = decoded.leftOffset; options.topOffset = frameSize.height() - decoded.topOffset; options.centeredOrigin = false;
		QString base = decoded.frames.isEmpty() ? QString() : decoded.frames.first().sourceName;
		if (base.endsWith(QStringLiteral(".pcx"), Qt::CaseInsensitive)) { base.chop(4); }
		while (!base.isEmpty() && base.back().isDigit()) { base.chop(1); }
		if (base.endsWith(QLatin1Char('_'))) { base.chop(1); }
		if (!base.isEmpty()) { options.name = base; }
	} else if (decoded.formatId == QStringLiteral("quake-miptex") && decoded.paletteId == QStringLiteral("wad3-embedded")) {
		options.format = TextureExportFormat::HalfLifeMiptex;
	}
	return options;
}
bool textureExportOptionsFromJson(const QJsonObject& object, TextureExportOptions* options, QString* error)
{
	TextureExportOptions parsed;
	const auto defaults = textureExportOptionsJson(parsed);
	for (auto it = object.begin(); it != object.end(); ++it) { if (!defaults.contains(it.key())) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Unknown texture export option: %1").arg(it.key())); } }
	const auto field = [&](const char* key) { const auto name = QLatin1String(key); return object.value(name).isUndefined() ? defaults.value(name) : object.value(name); };
	const auto number = [&](const char* key, qint64 low, qint64 high, auto* output) {
		const auto value = field(key); const double n = value.toDouble(std::numeric_limits<double>::quiet_NaN());
		if (!value.isDouble() || !std::isfinite(n) || std::floor(n) != n || n < double(low) || n > double(high)) { return false; }
		*output = static_cast<std::remove_reference_t<decltype(*output)>>(n); return true;
	};
	const auto boolean = [&](const char* key, bool* output) { const auto value = field(key); if (!value.isBool()) { return false; } *output = value.toBool(); return true; };
	const auto textField = [&](const char* key, QString* output, int maximum) { const auto value = field(key); if (!value.isString() || value.toString().size() > maximum || value.toString().contains(QChar::Null)) { return false; } *output = value.toString(); return true; };
	int version = 0; QString alpha, mip, fullbright, matte, spriteType, spriteTexture, embeddedPalette;
	bool valid = number("version", 1, 1, &version) && textureExportFormatFromId(field("profile").toString(), &parsed.format) &&
		textField("alpha", &alpha, 16) && textField("mipFilter", &mip, 16) && textField("fullbright", &fullbright, 16) && textField("matte", &matte, 16) &&
		number("alphaThreshold", 1, 255, &parsed.alphaThreshold) && boolean("dither", &parsed.dither) && boolean("allowGeneratedPalette", &parsed.allowGeneratedPalette) && boolean("extendedLimits", &parsed.extendedLimits) &&
		textField("name", &parsed.name, 128) && textField("animationNext", &parsed.animationNext, 128) && number("surfaceFlags", 0, 0xffffffffll, &parsed.surfaceFlags) &&
		number("contentFlags", 0, 0xffffffffll, &parsed.contentFlags) && number("surfaceValue", -2147483648ll, 2147483647ll, &parsed.surfaceValue) &&
		number("leftOffset", -32768, 32767, &parsed.leftOffset) && number("topOffset", -32768, 32767, &parsed.topOffset) &&
		textField("spriteType", &spriteType, 32) && textField("spriteTexture", &spriteTexture, 32) && textField("embeddedPalette", &embeddedPalette, 16) &&
		boolean("spriteRandomSync", &parsed.spriteRandomSync) && boolean("centeredOrigin", &parsed.centeredOrigin) && boolean("doomSprite", &parsed.doomSprite) &&
		textField("wallTexture", &parsed.wallTexture, 8);
	parsed.spriteType = int(spriteTypeIds().indexOf(spriteType)); parsed.spriteTexture = int(spriteTextureIds().indexOf(spriteTexture));
	valid = valid && parsed.spriteType >= 0 && parsed.spriteTexture >= 0 && (embeddedPalette == QStringLiteral("image") || embeddedPalette == QStringLiteral("selected"));
	parsed.imagePalette = embeddedPalette == QStringLiteral("image");
	parsed.matte = QColor(matte);
	valid = valid && parsed.matte.isValid() && parsed.matte.alpha() == 255;
	if (alpha == QStringLiteral("strict")) { parsed.alpha = TextureExportAlpha::Strict; } else if (alpha == QStringLiteral("matte")) { parsed.alpha = TextureExportAlpha::Matte; } else if (alpha == QStringLiteral("threshold")) { parsed.alpha = TextureExportAlpha::Threshold; } else { valid = false; }
	if (mip == QStringLiteral("box")) { parsed.mipFilter = TextureMipmapFilter::Box; } else if (mip == QStringLiteral("nearest")) { parsed.mipFilter = TextureMipmapFilter::Nearest; } else { valid = false; }
	if (fullbright == QStringLiteral("preserve")) { parsed.fullbright = TextureFullbrightMode::Preserve; } else if (fullbright == QStringLiteral("exclude")) { parsed.fullbright = TextureFullbrightMode::Exclude; } else if (fullbright == QStringLiteral("allow")) { parsed.fullbright = TextureFullbrightMode::Allow; } else { valid = false; }
	if (!options || !valid) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Invalid or unsupported texture export options.")); }
	*options = std::move(parsed); return true;
}

namespace {
// Preview of a binary-alpha encoding: palette colours with the prepared alpha.
QImage binaryPreview(const QImage& indexed, const QImage& prepared)
{
	QImage preview = indexed.convertToFormat(QImage::Format_ARGB32);
	for (int y = 0; y < prepared.height(); ++y) {
		auto* row = reinterpret_cast<QRgb*>(preview.scanLine(y));
		for (int x = 0; x < prepared.width(); ++x) { row[x] = (row[x] & 0x00ffffffu) | (prepared.pixel(x, y) & 0xff000000u); }
	}
	return preview;
}

// Transparent texels take index 255; quantization never assigns it otherwise.
QImage withTransparentIndex(QImage indexed, const QImage& prepared)
{
	for (int y = 0; y < prepared.height(); ++y) {
		auto* row = indexed.scanLine(y);
		for (int x = 0; x < prepared.width(); ++x) { if (qAlpha(prepared.pixel(x, y)) == 0) { row[x] = 255; } }
	}
	return indexed;
}

// The palette stored with an embedded-palette format: median cut over the
// prepared frames, or the selected game palette when requested.
IdTechPalette exportPalette(const QVector<QImage>& prepared, const TextureExportOptions& options, const IdTechPalette& selected, QString* error, const TextureProgress& progress)
{
	if (usesSelectedPalette(options)) { return selected; }
	const bool reserve = reservesIndex255(options);
	auto colors = buildTexturePalette(prepared, reserve ? 255 : 256, progress);
	if (colors.isEmpty()) { fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Texture export cancelled.")); return {}; }
	colors.resize(256, qRgb(0, 0, 0));
	// Half-Life tools conventionally store pure blue in the transparent entry.
	if (reserve) { colors[255] = qRgb(0, 0, 255); }
	IdTechPalette palette;
	palette.id = QStringLiteral("image"); palette.colors = colors; palette.transparentIndex = reserve ? 255 : -1;
	palette.displayName = QCoreApplication::translate("VibeStudioTextureExport", "Palette built from the image");
	palette.sourceDescription = palette.displayName;
	return palette;
}

qint64 colorChanges(const QImage& image, const QImage& preview, bool skipTransparent)
{
	qint64 changed = 0;
	for (int y = 0; y < image.height(); ++y) {
		for (int x = 0; x < image.width(); ++x) {
			if (skipTransparent && qAlpha(preview.pixel(x, y)) == 0) { continue; }
			if ((image.pixel(x, y) & 0x00ffffffu) != (preview.pixel(x, y) & 0x00ffffffu)) { ++changed; }
		}
	}
	return changed;
}

TextureExportResult encodeSprite(const QVector<QImage>& frames, const TextureExportOptions& options, const IdTechPaletteResolution& palette, const TextureProgress& progress)
{
	TextureExportResult result;
	const qint64 steps = qint64(frames.size()) * 3 + 2;
	const auto part = [&](qint64 step) { return [&, step](qint64 done, qint64 total) { return !progress || progress(step * 1000 + done * 1000 / std::max(qint64(1), total), steps * 1000); }; };
	QVector<QImage> prepared;
	for (qsizetype index = 0; index < frames.size(); ++index) {
		QImage frame = prepareAlpha(frames[index], options, &result.alphaChangedPixels, &result.error, part(index));
		if (frame.isNull()) { return result; }
		prepared << frame;
	}
	const bool halfLife = options.format == TextureExportFormat::HalfLifeSprite;
	const bool indexAlpha = halfLife && options.spriteTexture == kSpriteIndexAlpha;
	IdTechPalette stored;
	if (indexAlpha) {
		// sprgen.c writes a grey ramp; the engine draws coverage (the index) in
		// the last entry's colour, so that entry is the frames' mean colour.
		qint64 sum[4]{};
		for (const auto& frame : prepared) {
			for (int y = 0; y < frame.height(); ++y) {
				for (int x = 0; x < frame.width(); ++x) { const QRgb p = frame.pixel(x, y); sum[0] += qRed(p) * qAlpha(p); sum[1] += qGreen(p) * qAlpha(p); sum[2] += qBlue(p) * qAlpha(p); sum[3] += qAlpha(p); }
			}
		}
		for (int index = 0; index < 255; ++index) { stored.colors << qRgb(index, index, index); }
		stored.colors << (sum[3] ? qRgb(int(sum[0] / sum[3]), int(sum[1] / sum[3]), int(sum[2] / sum[3])) : qRgb(255, 255, 255));
	} else {
		stored = exportPalette(prepared, options, palette.palette, &result.error, part(frames.size()));
		if (!stored.isValid()) { if (result.error.isEmpty()) { result.error = QCoreApplication::translate("VibeStudioTextureExport", "This export profile needs a resolved 256-color palette."); } return result; }
	}
	QVector<QImage> indexed;
	for (qsizetype index = 0; index < frames.size(); ++index) {
		QImage frame, preview;
		if (indexAlpha) {
			frame = QImage(prepared[index].size(), QImage::Format_Indexed8); preview = QImage(prepared[index].size(), QImage::Format_ARGB32);
			if (frame.isNull() || preview.isNull()) { result.error = QCoreApplication::translate("VibeStudioTextureExport", "Unable to allocate sprite frame pixels."); return result; }
			frame.setColorTable(stored.colors);
			for (int y = 0; y < frame.height(); ++y) {
				auto* out = frame.scanLine(y); auto* view = reinterpret_cast<QRgb*>(preview.scanLine(y));
				for (int x = 0; x < frame.width(); ++x) { const int alpha = qAlpha(prepared[index].pixel(x, y)); out[x] = uchar(alpha); view[x] = (stored.colors.last() & 0x00ffffffu) | (quint32(alpha) << 24); }
			}
		} else {
			bool preserved = false;
			frame = quantize(frames[index], prepared[index], options, stored, &preserved, &result.error, part(frames.size() + 1 + index));
			if (frame.isNull()) { return result; }
			result.preservedIndices = (index == 0 || result.preservedIndices) && preserved;
			if (transparentIndex(options)) { frame = withTransparentIndex(frame, prepared[index]); }
			preview = alphaKind(options) == AlphaKind::Binary ? binaryPreview(frame, prepared[index]) : frame.convertToFormat(QImage::Format_ARGB32);
			result.colorChangedPixels += colorChanges(frames[index], preview, true);
		}
		indexed << frame; result.frames << preview;
	}
	result.preview = result.frames.first();
	if (usesSelectedPalette(options) && palette.palette.generated) { result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "Generated palette used: colors will not match an original game palette."); }
	if (options.format == TextureExportFormat::Quake2Sprite) {
		if (frames.size() > 32) {
			if (!options.extendedLimits) { result.error = QCoreApplication::translate("VibeStudioTextureExport", "Quake II loads at most 32 sprite frames. Hide layers, or enable source-port limits for a compatible engine."); return result; }
			result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "More than 32 frames: the original Quake II refuses this sprite.");
		}
		QVector<TextureSp2Frame> entries;
		for (qsizetype index = 0; index < indexed.size(); ++index) {
			const QSize size = indexed[index].size();
			const QPoint offset = options.centeredOrigin ? QPoint(size.width() / 2, size.height() / 2) : QPoint(options.leftOffset, options.topOffset);
			const QString name = QStringLiteral("%1_%2.pcx").arg(options.name).arg(index);
			const QByteArray pcx = encodePcx(indexed[index], stored, &result.error, part(frames.size() * 2 + 1 + index));
			if (pcx.isEmpty()) { return result; }
			result.companions.push_back({name, pcx});
			entries.push_back({size, QPoint(offset.x(), size.height() - offset.y()), name});
		}
		result.bytes = encodeSp2Sprite(entries);
		result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "The SP2 names %n PCX frame(s), written or staged beside it.", nullptr, int(entries.size()));
	} else {
		TextureSpriteEncoding sprite;
		sprite.version = halfLife ? 2 : 1; sprite.type = options.spriteType; sprite.texFormat = options.spriteTexture;
		sprite.syncType = options.spriteRandomSync ? 1 : 0; sprite.frames = indexed;
		if (halfLife) { sprite.palette = stored.colors; result.embeddedPalette = stored.colors; }
		for (const auto& frame : indexed) {
			const QPoint offset = options.centeredOrigin ? QPoint(frame.width() / 2, frame.height() / 2) : QPoint(options.leftOffset, options.topOffset);
			sprite.origins << QPoint(-offset.x(), offset.y());
		}
		result.bytes = encodeIdspSprite(sprite);
		if (!halfLife) { result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "Quake draws palette index 255 as transparent and stores no other alpha."); }
		else if (options.imagePalette && !indexAlpha) { result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "One palette was built from all frames' colors."); }
	}
	if (result.bytes.isEmpty() || result.bytes.size() > textureOutputByteLimit) { if (result.error.isEmpty()) { result.error = QCoreApplication::translate("VibeStudioTextureExport", "The encoded texture is empty or exceeds 64 MiB."); } return result; }
	if (!checkpoint(progress, steps * 1000, steps * 1000, &result.error)) { result.bytes.clear(); return result; }
	result.succeeded = true; return result;
}
} // namespace

TextureExportResult encodeTextureExport(const QImage& image, const TextureExportOptions& options, const IdTechPaletteResolution& palette, const TextureProgress& progress)
{
	return encodeTextureFrames({image}, options, palette, progress);
}

QVector<QImage> textureExportFrames(const TextureDocument& document, const TextureExportOptions& options)
{
	return isSprite(options.format) ? document.layerFrames() : QVector<QImage>{document.image()};
}

TextureExportResult encodeTextureDocumentExport(const TextureDocument& document, const TextureExportOptions& options, const IdTechPaletteResolution& palette, const TextureProgress& progress)
{
	return encodeTextureFrames(textureExportFrames(document, options), options, palette, progress);
}

TextureExportResult encodeTextureFrames(const QVector<QImage>& frames, const TextureExportOptions& options, const IdTechPaletteResolution& palette, const TextureProgress& progress)
{
	TextureExportResult result;
	if (frames.isEmpty()) { result.error = QCoreApplication::translate("VibeStudioTextureExport", "A sprite needs at least one visible layer."); return result; }
	if (frames.size() > TextureDocument::MaximumLayers) { result.error = QCoreApplication::translate("VibeStudioTextureExport", "A sprite export supports at most 32 frames."); return result; }
	for (const auto& frame : frames) { if (!validate(frame, options, palette, &result.error)) { return result; } }
	if (isSprite(options.format)) { return encodeSprite(frames, options, palette, progress); }
	const QImage& image = frames.first();
	const auto part = [&](int step, int count = 1) { return [&, step, count](qint64 done, qint64 total) { return !progress || progress(step * 1000 + done * count * 1000 / std::max(qint64(1), total), 8000); }; };
	QImage pixels = prepareAlpha(image, options, &result.alphaChangedPixels, &result.error, part(0));
	if (pixels.isNull()) { return result; }
	QImage indexed; IdTechPalette stored = palette.palette;
	if (isIndexed(options.format)) {
		// Palette building checks cancellation without advancing the reported
		// fraction, so quantization's progress that follows stays monotonic.
		stored = exportPalette({pixels}, options, palette.palette, &result.error, [&](qint64, qint64) { return part(1)(0, 1); });
		if (!stored.isValid()) { if (result.error.isEmpty()) { result.error = QCoreApplication::translate("VibeStudioTextureExport", "This export profile needs a resolved 256-color palette."); } return result; }
		indexed = quantize(image, pixels, options, stored, &result.preservedIndices, &result.error, part(1, 2));
		if (indexed.isNull()) { return result; }
		if (transparentIndex(options)) { indexed = withTransparentIndex(indexed, pixels); }
		result.preview = alphaKind(options) == AlphaKind::Binary ? binaryPreview(indexed, pixels) : indexed;
		if (usesSelectedPalette(options) && palette.palette.generated) { result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "Generated palette used: colors will not match an original game palette."); }
		if (embedsPalette(options.format)) {
			result.embeddedPalette = stored.colors;
			if (options.imagePalette) { result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "The stored palette was built from the image's colors."); }
		}
	} else { result.preview = pixels; }
	if (!checkpoint(part(3), 0, 1, &result.error)) { return result; }
	result.colorChangedPixels = colorChanges(image, result.preview, transparentIndex(options));
	if (isMipmapped(options.format)) {
		result.mipLevels << indexed;
		const int limit = reservesIndex255(options) ? 255 : 256, transparent = transparentIndex(options) ? 255 : -1;
		for (int level = 1; level < 4; ++level) {
			const auto report = [&](qint64 done, qint64 total) { return part(4, 2)((level - 1) * 1000 + done * 1000 / std::max(qint64(1), total), 3000); };
			const auto previous = result.mipLevels.last();
			QImage mip = options.mipFilter == TextureMipmapFilter::Nearest ? resizeTexturePixels(previous, previous.size() / 2, false, &result.error, report) : boxMip(previous, isQuake(options.format), options.fullbright, limit, transparent, &result.error, report);
			if (mip.isNull()) { return result; } result.mipLevels << mip;
		}
		if (isHalfLifeMip(options.format)) {
			const auto lump = encodeWad3Miptex(result.mipLevels, options.name, stored.colors);
			result.bytes = options.format == TextureExportFormat::HalfLifeWad3 ? encodeWad3Archive(lump, options.name) : lump;
			if (image.width() > 512 || image.height() > 512) { result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "Textures over 512 pixels per side may exceed GoldSrc engine and compiler limits."); }
		} else if (options.format == TextureExportFormat::SinSwl) {
			result.bytes = encodeSwl(result.mipLevels, stored.colors, {options.name, options.animationNext, options.surfaceFlags, options.contentFlags, qint16(options.surfaceValue)});
			result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "SiN's palette checksum and lighting, animation and physics properties are written as zero.");
		} else { result.bytes = encodeMip(result.mipLevels, options); }
	} else if (options.format == TextureExportFormat::DdsMipmapped) {
		result.mipLevels = textureRgbaMipChain(pixels, part(4, 2));
		if (result.mipLevels.isEmpty()) { result.error = QCoreApplication::translate("VibeStudioTextureExport", "Texture export cancelled."); return result; }
		result.bytes = encodeDdsMipmapped(result.mipLevels, part(6));
	} else if (options.format == TextureExportFormat::IndexedPng) { result.bytes = encodeIndexedPng(indexed, &result.error, part(6)); }
	else if (options.format == TextureExportFormat::Png) {
		result.bytes = encodeTextureOutputPng(result.preview, &result.error, [&] { return !checkpoint(part(6), 0, 1, &result.error); });
		if (result.bytes.isEmpty()) { return result; }
	} else if (options.format == TextureExportFormat::Targa) { result.bytes = encodeTarga(pixels, &result.error, part(6)); }
	else if (options.format == TextureExportFormat::Dds || options.format == TextureExportFormat::Ftx) {
		result.bytes = encodeExtraImage(pixels, options.format == TextureExportFormat::Dds, &result.error, [&] { return !checkpoint(part(6), 0, 1, &result.error); });
	}
	else if (options.format == TextureExportFormat::Pcx) {
		result.bytes = encodePcx(indexed, stored, &result.error, part(6));
		if (image.width() % 2 || image.width() > 640 || image.height() > 480 || qint64(image.width()) * image.height() > 512 * 256) { result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "Valid PCX, but outside original Quake II's even-width, 640 × 480 and 131,072-pixel loading limits."); }
		result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "PCX stores no alpha. Quake II uses its global palette and interprets index 255 as transparent.");
	} else if (options.format == TextureExportFormat::DoomFlat) { result.bytes = packedRows(indexed); }
	else if (options.format == TextureExportFormat::DoomPatch) {
		result.bytes = encodePatch(indexed, pixels, options, &result.error, part(6));
		if (image.height() > 255) { result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "Tall patch posts require a compatible source port."); }
		if (image.width() > 2048 || image.height() > 2048 || std::abs(options.leftOffset) >= 4096 || std::abs(options.topOffset) >= 4096) { result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "These patch dimensions or offsets exceed GZDoom's 2048-pixel and ±4095-offset recognition limits. Confirm support in the target port."); }
		if (result.bytes.size() > 65535) { result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "Large patches can exceed original Doom wall-texture column offsets; review the target engine and texture composition."); }
	}
	if (result.bytes.isEmpty() || result.bytes.size() > textureOutputByteLimit) { if (result.error.isEmpty()) { result.error = QCoreApplication::translate("VibeStudioTextureExport", "The encoded texture is empty or exceeds 64 MiB."); } return result; }
	if (!checkpoint(progress, 8000, 8000, &result.error)) { result.bytes.clear(); return result; }
	if (options.extendedLimits && (isMipmapped(options.format) || options.format == TextureExportFormat::DoomPatch)) { result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "Source-port limits enabled; validate the chosen engine and compiler before packaging."); }
	result.succeeded = true; return result;
}

QString textureExportCompanionPath(const QString& outputPath, const TextureExportCompanion& companion)
{
	return QFileInfo(outputPath).absoluteDir().filePath(QFileInfo(companion.path).fileName());
}

bool saveTextureExport(const QImage& image, const TextureExportOptions& options, const IdTechPaletteResolution& palette, const QString& path,
	bool overwrite, bool dryRun, TextureExportResult* result, QString* error, const TextureProgress& progress)
{
	return saveTextureFramesExport({image}, options, palette, path, overwrite, dryRun, result, error, progress);
}

bool saveTextureFramesExport(const QVector<QImage>& frames, const TextureExportOptions& options, const IdTechPaletteResolution& palette, const QString& path,
	bool overwrite, bool dryRun, TextureExportResult* result, QString* error, const TextureProgress& progress)
{
	if (error) { error->clear(); }
	if (QFileInfo(path).suffix().compare(textureExportSuffix(options.format), Qt::CaseInsensitive) != 0) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "The export filename extension must match the selected profile.")); }
	TextureOutputTarget target; if (!inspectTextureOutputTarget(path, overwrite, &target, error, [&] { return progress && !progress(0, 1); })) { return false; }
	auto encoded = encodeTextureFrames(frames, options, palette, progress);
	const bool ready = encoded.succeeded;
	if (!ready) { if (error) { *error = encoded.error; } if (result) { *result = std::move(encoded); } return false; }
	// Frames go first, so a published sprite never names a missing file.
	QVector<TextureOutputTarget> companions;
	for (const auto& companion : encoded.companions) {
		TextureOutputTarget companionTarget;
		if (!inspectTextureOutputTarget(textureExportCompanionPath(path, companion), overwrite, &companionTarget, error)) { if (result) { *result = std::move(encoded); } return false; }
		companions << companionTarget;
	}
	for (qsizetype index = 0; index < companions.size(); ++index) {
		if (!writeTextureOutput(companions[index], encoded.companions[index].bytes, dryRun, error)) { if (result) { *result = std::move(encoded); } return false; }
	}
	const bool written = writeTextureOutput(target, encoded.bytes, dryRun, error);
	if (result) { *result = std::move(encoded); } return written;
}

QJsonObject textureExportReportJson(const TextureExportOptions& options, const IdTechPaletteResolution& palette, const TextureExportResult& result)
{
	QJsonArray mips;
	for (const auto& mip : result.mipLevels) { mips.append(QJsonObject{{QStringLiteral("width"), mip.width()}, {QStringLiteral("height"), mip.height()}}); }
	QJsonObject object{{QStringLiteral("options"), textureExportOptionsJson(options)}, {QStringLiteral("succeeded"), result.succeeded}, {QStringLiteral("error"), result.error},
		{QStringLiteral("width"), result.preview.width()}, {QStringLiteral("height"), result.preview.height()}, {QStringLiteral("bytes"), double(result.bytes.size())},
		{QStringLiteral("colorChangedPixels"), double(result.colorChangedPixels)}, {QStringLiteral("alphaChangedPixels"), double(result.alphaChangedPixels)},
		{QStringLiteral("preservedIndices"), result.preservedIndices}, {QStringLiteral("mipLevels"), mips}, {QStringLiteral("warnings"), QJsonArray::fromStringList(result.warnings)},
		{QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(result.bytes, QCryptographicHash::Sha256).toHex())}};
	object.insert(QStringLiteral("frames"), result.frames.size());
	QJsonArray companions;
	for (const auto& companion : result.companions) {
		companions.append(QJsonObject{{QStringLiteral("path"), companion.path}, {QStringLiteral("bytes"), double(companion.bytes.size())},
			{QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(companion.bytes, QCryptographicHash::Sha256).toHex())}});
	}
	object.insert(QStringLiteral("companions"), companions);
	if (!result.embeddedPalette.isEmpty()) {
		IdTechPalette stored; stored.colors = result.embeddedPalette;
		object.insert(QStringLiteral("embeddedPalette"), QJsonObject{{QStringLiteral("source"), options.imagePalette ? QStringLiteral("image") : QStringLiteral("selected")},
			{QStringLiteral("rgbSha256"), QString::fromLatin1(QCryptographicHash::hash(paletteRgb(stored), QCryptographicHash::Sha256).toHex())}});
	}
	if (usesSelectedPalette(options)) { object.insert(QStringLiteral("palette"), QJsonObject{{QStringLiteral("id"), palette.palette.id}, {QStringLiteral("generated"), palette.palette.generated},
		{QStringLiteral("source"), palette.palette.sourceDescription}, {QStringLiteral("sourcePackage"), palette.sourcePackagePath}, {QStringLiteral("sourceEntry"), palette.sourceVirtualPath},
		{QStringLiteral("rgbSha256"), QString::fromLatin1(QCryptographicHash::hash(paletteRgb(palette.palette), QCryptographicHash::Sha256).toHex())}}); }
	return object;
}

bool textureExportSupportsQuake3Map(TextureExportFormat format)
{
	return format == TextureExportFormat::Png || format == TextureExportFormat::IndexedPng || format == TextureExportFormat::Targa;
}

bool stageTextureExport(const TextureExportResult& result, const TextureExportOptions& options, const QString& path,
	PackageStagingModel* staging, bool replaceExisting, QString* error)
{
	if (error) { error->clear(); }
	if (!staging || !staging->isLoaded() || !result.succeeded || result.bytes.isEmpty() || result.bytes.size() > 64 * 1024 * 1024
		|| result.preview.isNull() || !validTextureSize(result.preview.size(), error)) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Open a package and validate a bounded texture export before staging."));
	}
	if (staging->sourceFormat() == PackageArchiveFormat::Wad) {
		const auto magic = staging->sourceWadMagic();
		if ((magic == QStringLiteral("WAD2") && options.format == TextureExportFormat::QuakeMiptex) || (magic == QStringLiteral("WAD3") && options.format == TextureExportFormat::HalfLifeMiptex)) {
			if (path != options.name) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "The WAD lump name must match the miptexture export name.")); }
			return staging->addWadBytes(result.bytes, path, {}, magic == QStringLiteral("WAD3") ? 0x43 : 0x44, replaceExisting, error);
		}
		if ((magic == QStringLiteral("PWAD") || magic == QStringLiteral("IWAD"))
			&& (options.format == TextureExportFormat::DoomFlat || options.format == TextureExportFormat::DoomPatch)) {
			QString namespaceId = options.format == TextureExportFormat::DoomFlat ? QStringLiteral("flat") : (options.doomSprite ? QStringLiteral("sprite") : QStringLiteral("patch"));
			if (replaceExisting && options.format == TextureExportFormat::DoomPatch && !options.doomSprite) {
				// Menus and title pictures are valid unnamespaced patches. Retain
				// their position after checking the existing native payload; a
				// palette, map lump, or arbitrary global blob is not a patch target.
				const PackageStagingArchive planned(*staging);
				for (const auto& entry : planned.entries()) {
					if (entry.virtualPath.compare(path, Qt::CaseInsensitive) != 0 || entry.typeHint != QStringLiteral("wad-lump")) { continue; }
					QByteArray existing;
					if (planned.readEntryBytes(path, &existing, error, 64 * 1024 * 1024) && detectIdTechImageFormat(path, existing) == IdTechImageFormat::DoomPatch) { namespaceId = QStringLiteral("global"); }
					break;
				}
			}
			if (options.format != TextureExportFormat::DoomPatch || options.doomSprite || options.wallTexture.isEmpty()) {
				return staging->addWadBytes(result.bytes, path, namespaceId, 0, replaceExisting, error);
			}
			// The patch and its wall definition stage as one package undo step.
			auto next = *staging;
			if (!next.beginOperationGroup(QCoreApplication::translate("VibeStudioTextureExport", "Stage wall texture %1").arg(options.wallTexture.toUpper()), error) ||
				!next.addWadBytes(result.bytes, path, namespaceId, 0, replaceExisting, error) ||
				!stageDoomWallTexture(&next, path, options.wallTexture, result.preview.size(), replaceExisting, nullptr, error) || !next.endOperationGroup(true, error)) { return false; }
			*staging = std::move(next); return true;
		}
		return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Choose Quake miptexture for WAD2, Half-Life miptexture for WAD3, or Doom flat/patch for IWAD/PWAD."));
	}
	const auto normalized = normalizePackageVirtualPath(path, false);
	if (!normalized.isSafe() || QFileInfo(normalized.normalizedPath).suffix().compare(textureExportSuffix(options.format), Qt::CaseInsensitive) != 0) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Choose a safe package-relative path with the selected export profile's extension."));
	}
	auto next = *staging;
	const auto resolution = replaceExisting ? PackageStageConflictResolution::ReplaceExisting : PackageStageConflictResolution::Block;
	// A sprite and the frames it names stage as one package undo step.
	const bool grouped = !result.companions.isEmpty();
	if (grouped && !next.beginOperationGroup(QCoreApplication::translate("VibeStudioTextureExport", "Stage sprite %1").arg(normalized.normalizedPath), error)) { return false; }
	for (const auto& companion : result.companions) {
		const auto frame = normalizePackageVirtualPath(companion.path, false);
		if (!frame.isSafe() || !next.addBytes(companion.bytes, frame.normalizedPath, error, resolution)) {
			return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "A sprite frame path is unsafe or conflicts with the package: %1").arg(companion.path));
		}
	}
	if (!next.addBytes(result.bytes, normalized.normalizedPath, error, resolution) || (grouped && !next.endOperationGroup(true, error))
		|| !next.summary().canSave) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Resolve package conflicts or enable replacement before staging this texture.")); }
	*staging = std::move(next); return true;
}

} // namespace vibestudio
