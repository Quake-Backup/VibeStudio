#pragma once

#include "core/texture_document.h"

#include <QJsonObject>

namespace vibestudio {

// New formats are appended so stored ordinals and profile order stay stable.
enum class TextureExportFormat { Png, IndexedPng, Targa, Dds, Ftx, Pcx, QuakeMiptex, QuakeWad2, Quake2Wal, DoomFlat, DoomPatch,
	DdsMipmapped, SinSwl, HalfLifeMiptex, HalfLifeWad3, QuakeSprite, HalfLifeSprite, Quake2Sprite };
enum class TextureExportAlpha { Strict, Matte, Threshold };
enum class TextureMipmapFilter { Box, Nearest };
enum class TextureFullbrightMode { Preserve, Exclude, Allow };

struct TextureExportProfile {
	TextureExportFormat format;
	QString id, name, suffix, description;
	bool indexed = false;
	bool mipmapped = false;
	// Sprite profiles encode each visible layer as one frame.
	bool frames = false;
};
QVector<TextureExportProfile> textureExportProfiles();
QString textureExportFormatId(TextureExportFormat format);
bool textureExportFormatFromId(const QString& id, TextureExportFormat* format);
QString textureExportSuffix(TextureExportFormat format);

struct TextureExportOptions {
	TextureExportFormat format = TextureExportFormat::Png;
	TextureExportAlpha alpha = TextureExportAlpha::Strict;
	QColor matte = Qt::black;
	int alphaThreshold = 128;
	bool dither = false;
	bool allowGeneratedPalette = false;
	// Allows documented source-port limits, including tall Doom patch posts.
	bool extendedLimits = false;
	TextureMipmapFilter mipFilter = TextureMipmapFilter::Box;
	TextureFullbrightMode fullbright = TextureFullbrightMode::Preserve;
	QString name = QStringLiteral("texture"), animationNext;
	quint32 surfaceFlags = 0, contentFlags = 0;
	qint32 surfaceValue = 0;
	// Doom patch offsets, and the sprite origin measured from the frame's
	// top-left corner when centeredOrigin is off.
	int leftOffset = 0, topOffset = 0;
	// Sprites: orientation 0..4 (parallel-upright, facing-upright, parallel,
	// oriented, parallel-oriented), Half-Life texture format 0..3 (normal,
	// additive, index-alpha, alpha-test) and random frame synchronisation.
	int spriteType = 2, spriteTexture = 0;
	bool spriteRandomSync = false, centeredOrigin = true;
	// WAD3, Half-Life sprites and SWL store their own palette: built from the
	// image (median cut) unless the selected game palette is requested.
	bool imagePalette = true;
	// Doom patches staged into a WAD's sprite namespace (S_START/S_END).
	bool doomSprite = false;
	// When staging a Doom patch, also define this TEXTURE1 wall texture as the
	// patch at 0, 0 (PNAMES is updated to match). Empty: patch only.
	QString wallTexture;
};

QJsonObject textureExportOptionsJson(const TextureExportOptions& options);
bool textureExportOptionsFromJson(const QJsonObject& object, TextureExportOptions* options, QString* error = nullptr);
// Import supported native metadata; mip levels are regenerated from edited pixels.
TextureExportOptions textureExportOptionsForImage(const IdTechImageDecodeResult& decoded);

struct TextureExportCompanion {
	QString path; // package path, e.g. a Quake II sprite frame's PCX
	QByteArray bytes;
};

struct TextureExportResult {
	bool succeeded = false;
	QByteArray bytes;
	QImage preview;
	QVector<QImage> mipLevels;
	// Sprite frames as encoded, with transparency, and files the main output
	// names (Quake II SP2 frames). Companions are written or staged with it.
	QVector<QImage> frames;
	QVector<TextureExportCompanion> companions;
	// The palette actually stored, for formats that embed their own.
	QVector<QRgb> embeddedPalette;
	QStringList warnings;
	QString error;
	qint64 colorChangedPixels = 0, alphaChangedPixels = 0;
	bool preservedIndices = false;
};

// Encodes a composite snapshot. Never edits layers or changes saved state.
TextureExportResult encodeTextureExport(const QImage& image, const TextureExportOptions& options,
	const IdTechPaletteResolution& palette = {}, const TextureProgress& progress = {});
// Sprite profiles encode `frames` in order; the others encode the first image.
TextureExportResult encodeTextureFrames(const QVector<QImage>& frames, const TextureExportOptions& options,
	const IdTechPaletteResolution& palette = {}, const TextureProgress& progress = {});
// The document's frames for this profile: visible layers for sprites,
// otherwise the visible composite.
QVector<QImage> textureExportFrames(const TextureDocument& document, const TextureExportOptions& options);
TextureExportResult encodeTextureDocumentExport(const TextureDocument& document, const TextureExportOptions& options,
	const IdTechPaletteResolution& palette = {}, const TextureProgress& progress = {});
bool saveTextureExport(const QImage& image, const TextureExportOptions& options, const IdTechPaletteResolution& palette,
	const QString& path, bool overwrite, bool dryRun, TextureExportResult* result = nullptr, QString* error = nullptr, const TextureProgress& progress = {});
bool saveTextureFramesExport(const QVector<QImage>& frames, const TextureExportOptions& options, const IdTechPaletteResolution& palette,
	const QString& path, bool overwrite, bool dryRun, TextureExportResult* result = nullptr, QString* error = nullptr, const TextureProgress& progress = {});
// Where a companion is written beside a file export: the output's folder
// plus the companion's file name.
QString textureExportCompanionPath(const QString& outputPath, const TextureExportCompanion& companion);
QJsonObject textureExportReportJson(const TextureExportOptions& options, const IdTechPaletteResolution& palette, const TextureExportResult& result);

// Stages a successful encoder result without writing a temporary asset. WAD2
// and WAD3 accept their miptexture lumps; Doom WADs place flats/patches in
// their namespaces (sprites in S_START/S_END); SP2 frames stage beside it.
bool stageTextureExport(const TextureExportResult& result, const TextureExportOptions& options, const QString& path,
	PackageStagingModel* staging, bool replaceExisting, QString* error = nullptr);
bool textureExportSupportsQuake3Map(TextureExportFormat format);

} // namespace vibestudio
