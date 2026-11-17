#pragma once

#include "core/idtech_image.h"
#include "core/package_archive.h"
#include "core/project_text_search.h"

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace slopstudio {

enum class AssetPreviewKind {
	Unknown,
	Image,
	Model,
	Audio,
	Text,
	Binary,
};

// Decoded PCM envelope used by the waveform view and by CLI audio reports.
struct AssetAudioPeaks {
	bool valid = false;
	int channels = 0;
	int sampleRate = 0;
	int bitsPerSample = 0;
	qint64 durationMs = 0;
	qint64 frameCount = 0;
	// Interleaved min/max pairs in [-1, 1], `bucketCount` pairs per channel,
	// channel-major: channel 0 pairs first, then channel 1, and so on.
	QVector<float> peaks;
	int bucketCount = 0;
	QString error;
};

// One finding of the local text check: an unbalanced brace, an unterminated
// string, an unknown shader directive and so on.
struct AssetTextDiagnostic {
	// The 1-based line the finding names; 0 when it is about the text as a whole.
	int line = 0;
	// "warning" or "info": a stable id, not for display.
	QString severity;
	// The finding in words, translated ("unterminated quoted string").
	QString message;
	// As lists show it, with its line ("line 12: unterminated quoted string").
	QString text;
};

// Who the local text check speaks to.
enum class AssetTextDiagnosticScope {
	// A package preview or a CLI report of a read-only sample: every finding,
	// with the hints that only suit a sample (long lines, compiler-style
	// ERROR/WARNING markers in the text).
	Preview,
	// A document being edited: real findings only, each of which is underlined
	// in the editor and counted as a problem.
	Editor,
};

struct AssetAnalysis {
	AssetPreviewKind kind = AssetPreviewKind::Unknown;
	QString kindId;
	QString title;
	QString summary;
	QString body;
	QStringList detailLines;
	QStringList rawLines;
	QString error;

	QString imageFormat;
	QSize imageSize;
	int imageDepth = 0;
	int imageColorCount = 0;
	bool imageHasAlpha = false;
	bool imagePaletteAware = false;
	QStringList imagePaletteLines;
	// Real decoded pixels, empty when only header metadata could be recovered.
	QImage imagePixels;
	QString imageFormatId;
	bool imageIdTechFormat = false;
	int imageMipLevelCount = 0;
	int imageFrameCount = 0;
	int imageLeftOffset = 0;
	int imageTopOffset = 0;
	QString imagePaletteId;
	bool imagePaletteGenerated = false;
	QString imagePaletteSourceVirtualPath;
	QString imageTextureName;
	QString imageAnimationNextName;
	quint32 imageSurfaceFlags = 0;
	quint32 imageContentFlags = 0;
	qint32 imageSurfaceValue = 0;

	QString modelFormat;
	QString modelFamily;
	int modelFrameCount = 0;
	int modelSkinCount = 0;
	int modelSurfaceCount = 0;
	int modelTagCount = 0;
	int modelVertexCount = 0;
	int modelTriangleCount = 0;
	// True when the sampled byte range did not cover the whole model, so the
	// aggregated vertex/triangle/surface counts are partial.
	bool modelCountsPartial = false;
	QStringList modelSkinPaths;
	QStringList modelAnimationNames;
	QStringList modelViewportLines;
	QStringList modelMaterialLines;

	QString audioFormat;
	QString audioCodec;
	int audioChannels = 0;
	int audioSampleRate = 0;
	int audioBitsPerSample = 0;
	qint64 audioBitrateBitsPerSecond = 0;
	qint64 audioDurationMs = 0;
	qint64 audioFrameCount = 0;
	bool audioQtPlaybackCandidate = false;
	bool audioWavExportSupported = false;
	// True when WAV export needs a sample-format conversion rather than a copy.
	bool audioWavExportNeedsConversion = false;
	QStringList audioWaveformLines;
	AssetAudioPeaks audioPeaks;

	QString textLanguageId;
	QString textLanguageName;
	QString textSyntaxEngine;
	QString textSaveState;
	QStringList textHighlightLines;
	QStringList textDiagnosticLines;
	// The findings behind textDiagnosticLines, each with its line and severity.
	// Empty when the text is clean; textDiagnosticLines then holds one line
	// saying so.
	QVector<AssetTextDiagnostic> textDiagnostics;
};

struct AssetImageConversionRequest {
	QStringList virtualPaths;
	QString outputDirectory;
	QString outputFormat = QStringLiteral("png");
	QRect cropRect;
	QSize resizeSize;
	// "", "none" and "rgb" keep truecolor output; "grayscale" converts to 8-bit
	// grey; "indexed" and any id from idTechPaletteIds() quantize onto an
	// idTech palette.
	QString paletteMode;
	// Palette used for decoding paletted sources and for "indexed" output. When
	// empty a palette is picked from the source format and resolved against the
	// package.
	QString paletteId;
	bool dryRun = false;
	bool overwriteExisting = false;
};

struct AssetImageConversionEntryResult {
	QString virtualPath;
	QString outputPath;
	QString outputFormat;
	QSize beforeSize;
	QSize afterSize;
	qint64 inputBytes = 0;
	qint64 outputBytes = 0;
	bool dryRun = false;
	bool written = false;
	QString message;
	QString error;
	QStringList previewLines;
	QString sourceFormatId;
	QString paletteId;
	bool paletteFromPackage = false;
	bool paletteGenerated = false;
	QString paletteSourceVirtualPath;
};

struct AssetImageConversionReport {
	QString sourcePath;
	QString outputDirectory;
	int requestedCount = 0;
	int processedCount = 0;
	int writtenCount = 0;
	int errorCount = 0;
	qint64 totalInputBytes = 0;
	qint64 totalOutputBytes = 0;
	bool dryRun = false;
	QVector<AssetImageConversionEntryResult> entries;
	QStringList warnings;

	[[nodiscard]] bool succeeded() const;
};

struct AssetAudioExportReport {
	QString sourcePath;
	QString virtualPath;
	QString outputPath;
	qint64 bytes = 0;
	qint64 sourceBytes = 0;
	bool dryRun = false;
	bool written = false;
	bool converted = false;
	QString sourceFormat;
	QString conversionMode;
	QString message;
	QString error;
	QStringList detailLines;
	bool cancelled = false;

	[[nodiscard]] bool succeeded() const;
};

QString assetPreviewKindId(AssetPreviewKind kind);
AssetPreviewKind assetPreviewKindForPath(const QString& virtualPath);
// Like assetPreviewKindForPath, and also names what a WAD's extensionless
// lumps are: WAD2/WAD3 lumps and a Doom WAD's flats, sprites, and patches
// (from their namespace type hints) and its well-known global graphics are
// images; DS* and DP* lumps are sounds, as are bare or .lmp entries under a
// PK3's sounds/ directory.
AssetPreviewKind assetPreviewKindForEntry(const QString& virtualPath, const QString& typeHint = {});
// The path an image decoder should be given for a package entry. A WAD flat
// keeps its lump name but is decoded as if it sat under flats/, because its
// namespace marker, not its bytes, is what says it is a flat.
QString assetDetectionPath(const QString& virtualPath, const QString& typeHint);

// Something a media player can open for an audio entry: original WAV bytes,
// a Doom DMX sound widened to PCM16 WAV, and original compressed streams
// passed through for the host's codecs. `fileName` carries the suffix a
// player sniffs the format from.
struct AssetAudioPlaybackSource {
	QByteArray bytes;
	QString fileName;
	QString format;
	QString error;

	[[nodiscard]] bool playable() const;
};
AssetAudioPlaybackSource assetAudioPlaybackSource(const QString& virtualPath, const QByteArray& bytes, qint64 maxBytes = 128ll * 1024 * 1024);

// `palette` is used when the payload turns out to be a paletted idTech image.
// When it is null a generated, license-clean palette is chosen from the
// detected format.
// A negative total retains the historical unknown-size behavior.
AssetAnalysis analyzeAssetBytes(const QString& virtualPath, const QByteArray& bytes, qint64 totalBytes = -1, const IdTechPalette* palette = nullptr);
// Analyze a bounded sample with a known unsigned archive size. A prefix remains
// partial even when the declared total exceeds the signed file-offset range.
AssetAnalysis analyzeAssetSample(const QString& virtualPath, const QByteArray& bytes, quint64 totalBytes, const IdTechPalette* palette = nullptr);

// The local text check on its own, for text already in hand; the language
// comes from the path's extension, as in analyzeAssetBytes(). An empty list
// means nothing was found. The check stops after 64 findings: what can only be
// said of the whole text (a block or comment never closed) is then left out,
// and in the editor scope a last "info" entry with no line says it stopped.
QVector<AssetTextDiagnostic> assetTextDiagnostics(const QString& virtualPath, const QString& text,
	AssetTextDiagnosticScope scope = AssetTextDiagnosticScope::Editor);

// Extracts a min/max envelope from a RIFF/WAVE payload. Supports 8-bit
// unsigned, 16/24/32-bit signed PCM and 32-bit float, including
// WAVE_FORMAT_EXTENSIBLE.
AssetAudioPeaks extractWavePeaks(const QByteArray& bytes, int bucketCount = 512);

// ASCII envelope rendering used by the CLI and by compact preview panes.
QStringList assetWaveformLines(const AssetAudioPeaks& peaks, int buckets = 32, int width = 12);

AssetImageConversionReport convertPackageImages(const PackageArchive& archive, const AssetImageConversionRequest& request);
QString assetImageConversionReportText(const AssetImageConversionReport& report);

AssetAudioExportReport exportPackageAudioToWav(const PackageArchive& archive, const QString& virtualPath, const QString& outputPath, bool dryRun = false, bool overwriteExisting = false, const std::function<bool()>& cancelled = {});
// Exact immutable reader occurrence, including repeated WAD sound names.
AssetAudioExportReport exportPackageAudioToWavAt(const PackageArchive& archive, qsizetype entryIndex, const QString& outputPath, bool dryRun = false, bool overwriteExisting = false, const std::function<bool()>& cancelled = {});
QString assetAudioExportReportText(const AssetAudioExportReport& report);


} // namespace slopstudio
