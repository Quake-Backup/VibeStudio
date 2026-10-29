#include "core/extra_image.h"
#include "core/package_archive.h"
#include "core/package_staging.h"
#include "core/texture_doom_walls.h"
#include "core/texture_export.h"
#include "core/texture_formats.h"
#include "core/texture_project.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;

namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << "FAIL: " << message << '\n'; }
	return value;
}
bool put(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
qint32 at32(const QByteArray& bytes, qsizetype offset) { return qFromLittleEndian<qint32>(bytes.constData() + offset); }
void append32(QByteArray& bytes, quint32 value) { const auto offset = bytes.size(); bytes.resize(offset + 4); qToLittleEndian(value, bytes.data() + offset); }
using Lump = QPair<QString, QByteArray>;
QByteArray wad(const QByteArray& magic, const QVector<Lump>& lumps)
{
	QByteArray bytes = magic; append32(bytes, quint32(lumps.size())); append32(bytes, 0);
	QByteArray directory;
	for (const auto& lump : lumps) {
		append32(directory, quint32(bytes.size())); append32(directory, quint32(lump.second.size()));
		directory.append(lump.first.toLatin1().leftJustified(8, '\0')); bytes += lump.second;
	}
	qToLittleEndian(quint32(bytes.size()), bytes.data() + 8); bytes += directory; return bytes;
}
QStringList names(const PackageArchiveReader& archive) { QStringList result; for (const auto& entry : archive.entries()) { result << entry.virtualPath; } return result; }
QByteArray entry(const PackageArchiveReader& archive, const QString& name) { QByteArray bytes; QString error; archive.readEntryBytes(name, &bytes, &error); return bytes; }

// Two frames: a red square and a green one, on transparency.
TextureDocument spriteDocument()
{
	TextureDocument document; document.create({16, 8}, Qt::transparent);
	document.paintStroke({QPoint(2, 2)}, QColor(255, 0, 0), TextureBrush{3});
	document.addLayer(QStringLiteral("Frame 2")); document.paintStroke({QPoint(10, 4)}, QColor(0, 255, 0), TextureBrush{3});
	return document;
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temporary; if (!temporary.isValid()) { return EXIT_FAILURE; }
	const QDir root(temporary.path());
	bool ok = true; QString error;
	IdTechPaletteResolution quake; quake.palette = generatedIdTechPalette(QStringLiteral("quake")); quake.palette.generated = false;
	IdTechPaletteResolution quake2; quake2.palette = generatedIdTechPalette(QStringLiteral("quake2")); quake2.palette.generated = false;

	// Every new profile is listed with a unique id and round-trips its options.
	{
		QStringList ids;
		for (const auto& profile : textureExportProfiles()) { ids << profile.id; }
		for (const char* id : {"dds-mipmaps", "sin-swl", "halflife-miptex", "halflife-wad3", "quake-spr", "halflife-spr", "quake2-sp2"}) {
			ok &= expect(ids.count(QString::fromLatin1(id)) == 1, id);
		}
		TextureExportOptions options; options.format = TextureExportFormat::HalfLifeSprite; options.spriteType = 3; options.spriteTexture = 3;
		options.spriteRandomSync = true; options.centeredOrigin = false; options.imagePalette = false; options.doomSprite = true; options.wallTexture = QStringLiteral("WALL1");
		TextureExportOptions parsed;
		ok &= expect(textureExportOptionsFromJson(textureExportOptionsJson(options), &parsed) && textureExportOptionsJson(parsed) == textureExportOptionsJson(options), "new export options round-trip");
		auto json = textureExportOptionsJson(options); json.insert(QStringLiteral("spriteType"), QStringLiteral("sideways"));
		ok &= expect(!textureExportOptionsFromJson(json, &parsed), "unknown sprite orientations are refused");
		json = textureExportOptionsJson(options); json.remove(QStringLiteral("spriteType")); json.remove(QStringLiteral("wallTexture"));
		ok &= expect(textureExportOptionsFromJson(json, &parsed) && parsed.spriteType == 2 && parsed.wallTexture.isEmpty(), "older settings without the new fields take their defaults");
	}

	// Quake sprite: each visible layer is a frame, centred, index 255 transparent.
	{
		auto document = spriteDocument();
		TextureExportOptions options; options.format = TextureExportFormat::QuakeSprite; options.alpha = TextureExportAlpha::Threshold;
		const auto result = encodeTextureDocumentExport(document, options, quake);
		ok &= expect(result.succeeded && result.frames.size() == 2 && result.bytes.startsWith("IDSP") && at32(result.bytes, 4) == 1, "Quake sprite encodes two frames");
		ok &= expect(at32(result.bytes, 16) == 16 && at32(result.bytes, 20) == 8 && at32(result.bytes, 24) == 2 && at32(result.bytes, 8) == 2, "header records size, frame count and the parallel orientation");
		ok &= expect(at32(result.bytes, 40) == -8 && at32(result.bytes, 44) == 4, "centred origin: left edge -w/2, top edge h/2");
		const auto decoded = decodeIdTechImage(QStringLiteral("sprites/test.spr"), result.bytes, quake.palette);
		ok &= expect(decoded.decoded && decoded.frames.size() == 2 && decoded.frames[1].image.size() == QSize(16, 8), "the existing decoder reads every frame");
		ok &= expect(decoded.frames.size() == 2 && qAlpha(decoded.frames[0].image.pixel(0, 0)) == 0 && qAlpha(decoded.frames[0].image.pixel(2, 2)) == 255 &&
			qAlpha(decoded.frames[1].image.pixel(2, 2)) == 0 && qGreen(decoded.frames[1].image.pixel(10, 4)) > 128, "transparency and colours survive in each frame");
		TextureExportOptions strict = options; strict.alpha = TextureExportAlpha::Strict;
		TextureDocument soft; soft.create({8, 8}, QColor(255, 0, 0, 100));
		ok &= expect(!encodeTextureDocumentExport(soft, strict, quake).succeeded, "partial alpha is refused unless thresholded");
		document.selectLayer(1); document.setLayerProperties(QStringLiteral("Frame 2"), false, false, 100, TextureBlendMode::Normal);
		ok &= expect(encodeTextureDocumentExport(document, options, quake).frames.size() == 1, "hidden layers are not frames");
		options.centeredOrigin = false; options.leftOffset = 3; options.topOffset = 7;
		const auto offset = encodeTextureDocumentExport(document, options, quake);
		ok &= expect(at32(offset.bytes, 40) == -3 && at32(offset.bytes, 44) == 7, "explicit origin offsets are measured from the top-left corner");
		IdTechImageDecodeResult reread = decodeIdTechImage(QStringLiteral("sprites/test.spr"), offset.bytes, quake.palette);
		const auto imported = textureExportOptionsForImage(reread);
		ok &= expect(imported.format == TextureExportFormat::QuakeSprite && !imported.centeredOrigin && imported.leftOffset == 3 && imported.topOffset == 7, "imported sprites keep their profile and origin");
	}

	// Half-Life sprite: one embedded palette, alpha-test and index-alpha.
	{
		auto document = spriteDocument();
		TextureExportOptions options; options.format = TextureExportFormat::HalfLifeSprite; options.spriteTexture = 3; options.alpha = TextureExportAlpha::Threshold;
		const auto result = encodeTextureDocumentExport(document, options, {});
		ok &= expect(result.succeeded && at32(result.bytes, 4) == 2 && at32(result.bytes, 12) == 3 && result.embeddedPalette.size() == 256, "Half-Life sprite needs no game palette and stores its own");
		const auto decoded = decodeIdTechImage(QStringLiteral("sprites/test.spr"), result.bytes, IdTechPalette{});
		ok &= expect(decoded.decoded && decoded.frames.size() == 2 && decoded.frames[0].image.pixel(2, 2) == qRgb(255, 0, 0) && qAlpha(decoded.frames[0].image.pixel(0, 0)) == 0, "exact colours from the built palette, with alpha-test transparency");
		options.spriteTexture = 0;
		ok &= expect(!encodeTextureDocumentExport(document, options, {}).succeeded, "a normal (opaque) Half-Life sprite refuses transparency without a matte");
		options.spriteTexture = 2; options.alpha = TextureExportAlpha::Strict;
		TextureDocument glow; glow.create({4, 4}, QColor(255, 128, 0, 0)); glow.paintStroke({QPoint(1, 1)}, QColor(255, 128, 0, 200), TextureBrush{1});
		const auto indexAlpha = encodeTextureDocumentExport(glow, options, {});
		const auto glowDecoded = decodeIdTechImage(QStringLiteral("sprites/glow.spr"), indexAlpha.bytes, IdTechPalette{});
		ok &= expect(indexAlpha.succeeded && glowDecoded.decoded && qAlpha(glowDecoded.frames[0].image.pixel(1, 1)) == 200 && qAlpha(glowDecoded.frames[0].image.pixel(0, 0)) == 0, "index-alpha keeps full alpha coverage");
	}

	// Quake II sprite: an SP2 naming PCX frames, written beside it.
	{
		auto document = spriteDocument();
		TextureExportOptions options; options.format = TextureExportFormat::Quake2Sprite; options.alpha = TextureExportAlpha::Threshold; options.name = QStringLiteral("sprites/flame");
		const auto result = encodeTextureDocumentExport(document, options, quake2);
		ok &= expect(result.succeeded && result.bytes.startsWith("IDS2") && at32(result.bytes, 8) == 2 && result.companions.size() == 2, "SP2 names one PCX per frame");
		ok &= expect(result.companions.size() == 2 && result.companions[1].path == QStringLiteral("sprites/flame_1.pcx") && result.bytes.mid(28, 19) == QByteArray("sprites/flame_0.pcx"), "frame names follow the native name");
		ok &= expect(at32(result.bytes, 20) == 8 && at32(result.bytes, 24) == 4, "Quake II origins count from the left and bottom edges");
		const auto frame = decodeIdTechImage(QStringLiteral("sprites/flame_0.pcx"), result.companions.value(0).bytes, quake2.palette);
		ok &= expect(frame.decoded && frame.image.size() == QSize(16, 8), "the PCX frames decode");
		const QString output = root.filePath(QStringLiteral("flame.sp2"));
		ok &= expect(saveTextureFramesExport(textureExportFrames(document, options), options, quake2, output, false, false, nullptr, &error) &&
			QFileInfo::exists(root.filePath(QStringLiteral("flame_0.pcx"))) && QFileInfo::exists(root.filePath(QStringLiteral("flame_1.pcx"))), "file export writes the frames beside the SP2");
		ok &= expect(!saveTextureFramesExport(textureExportFrames(document, options), options, quake2, output, false, false, nullptr, &error), "existing outputs need overwrite");
		options.name = QStringLiteral("../escape");
		ok &= expect(!encodeTextureDocumentExport(document, options, quake2).succeeded, "frame names must be safe package paths");
		PackageArchive folder; PackageStagingModel staging; QDir(root.filePath(QStringLiteral("mod"))).mkpath(QStringLiteral("."));
		options.name = QStringLiteral("sprites/flame");
		ok &= expect(folder.load(root.filePath(QStringLiteral("mod")), &error) && staging.loadBaseArchive(folder, &error) &&
			stageTextureExport(result, options, QStringLiteral("sprites/flame.sp2"), &staging, false, &error), "SP2 stages with its frames");
		const PackageStagingArchive planned(staging);
		ok &= expect(names(planned).contains(QStringLiteral("sprites/flame_0.pcx")) && names(planned).contains(QStringLiteral("sprites/flame.sp2")), "frames and sprite are in the plan");
		ok &= expect(staging.undo() && names(PackageStagingArchive(staging)).isEmpty(), "sprite and frames undo as one step");
	}

	// Half-Life WAD3: per-texture palette, '{' transparency, staging into WAD3.
	{
		TextureDocument document; document.create({32, 16}, QColor(200, 40, 40)); document.paintStroke({QPoint(4, 4)}, Qt::transparent, TextureBrush{4});
		TextureExportOptions options; options.format = TextureExportFormat::HalfLifeWad3; options.name = QStringLiteral("{fence"); options.alpha = TextureExportAlpha::Threshold;
		const auto result = encodeTextureDocumentExport(document, options, {});
		ok &= expect(result.succeeded && result.bytes.startsWith("WAD3") && result.mipLevels.size() == 4 && result.embeddedPalette.value(255) == qRgb(0, 0, 255), "WAD3 texture with reserved blue index 255");
		const QString path = root.filePath(QStringLiteral("halflife.wad")); put(path, result.bytes);
		PackageArchive archive;
		ok &= expect(archive.load(path, &error) && archive.wadMagic() == QStringLiteral("WAD3") && names(archive) == QStringList{QStringLiteral("{fence")}, "the WAD3 archive loads with one lump");
		IdTechPaletteResolution resolution;
		const auto decoded = decodeIdTechImageFromArchive(archive, QStringLiteral("{fence"), QStringLiteral("quake"), &resolution);
		ok &= expect(decoded.decoded && decoded.paletteId == QStringLiteral("wad3-embedded") && decoded.image.pixel(20, 10) == qRgb(200, 40, 40) && qAlpha(decoded.image.pixel(4, 4)) == 0, "the decoder reads the embedded palette and transparency");
		ok &= expect(textureExportOptionsForImage(decoded).format == TextureExportFormat::HalfLifeMiptex, "editing a WAD3 texture selects the Half-Life miptexture profile");
		options.name = QStringLiteral("fence");
		ok &= expect(!encodeTextureDocumentExport(document, options, {}).succeeded, "only '{' names store transparency");
		options.alpha = TextureExportAlpha::Matte; options.format = TextureExportFormat::HalfLifeMiptex; options.name = QStringLiteral("brick");
		const auto lump = encodeTextureDocumentExport(document, options, {});
		PackageStagingModel staging;
		ok &= expect(lump.succeeded && staging.loadBaseArchive(archive, &error) && stageTextureExport(lump, options, QStringLiteral("brick"), &staging, false, &error), "a Half-Life miptexture stages into a WAD3");
		const PackageStagingArchive planned(staging);
		ok &= expect(names(planned).contains(QStringLiteral("brick")) && decodeIdTechImageFromArchive(planned, QStringLiteral("brick"), QStringLiteral("quake")).decoded, "the staged lump decodes from the plan");
		options.imagePalette = false;
		ok &= expect(!encodeTextureDocumentExport(document, options, {}).succeeded && encodeTextureDocumentExport(document, options, quake).succeeded, "the selected-palette option needs a resolved palette");
	}

	// SWL and mipmapped DDS.
	{
		TextureDocument document; document.create({32, 32}, QColor(30, 90, 160)); document.paintStroke({QPoint(5, 5)}, QColor(250, 250, 0), TextureBrush{6});
		TextureExportOptions options; options.format = TextureExportFormat::SinSwl; options.name = QStringLiteral("textures/sin/panel"); options.animationNext = QStringLiteral("textures/sin/panel2");
		options.surfaceFlags = 0x10; options.contentFlags = 1; options.surfaceValue = 42;
		const auto swl = encodeTextureDocumentExport(document, options, {});
		IdTechImageDecodeContext context;
		const auto decoded = decodeExtraImage(IdTechImageFormat::SinSwl, swl.bytes, context);
		ok &= expect(swl.succeeded && decoded.decoded && decoded.mipLevels.size() == 4 && decoded.textureName == options.name && decoded.image.pixel(20, 20) == qRgb(30, 90, 160), "SWL decodes with its name, palette and mips");
		ok &= expect(at32(swl.bytes, 1180) == 0x10 && at32(swl.bytes, 1184) == 1 && qFromLittleEndian<qint16>(swl.bytes.constData() + 1188) == 42 && swl.bytes.mid(1116, 19) == QByteArray("textures/sin/panel2"), "SWL stores animation, flags and value");
		options.surfaceValue = 40000;
		ok &= expect(!encodeTextureDocumentExport(document, options, {}).succeeded, "SWL values are 16-bit");
		options = {}; options.format = TextureExportFormat::DdsMipmapped;
		const auto dds = encodeTextureDocumentExport(document, options, {});
		ok &= expect(dds.succeeded && at32(dds.bytes, 28) == 6 && dds.mipLevels.size() == 6 && dds.mipLevels.last().size() == QSize(1, 1), "DDS writes a full mip chain");
		qint64 expected = 128; for (const auto& mip : dds.mipLevels) { expected += qint64(mip.width()) * mip.height() * 4; }
		ok &= expect(dds.bytes.size() == expected && decodeDdsImage(dds.bytes, context, &error) == document.image().convertToFormat(decodeDdsImage(dds.bytes, context, &error).format()), "the base level decodes unchanged");
		const auto ddsImport = decodeExtraImage(IdTechImageFormat::Dds, dds.bytes, context);
		ok &= expect(ddsImport.decoded && ddsImport.mipLevels.size() == 6 && ddsImport.mipLevels[2].size() == QSize(8, 8) &&
			ddsImport.mipLevels[2].convertToFormat(QImage::Format_ARGB32) == dds.mipLevels[2] && ddsImport.warnings.isEmpty(), "DDS import keeps every stored mip level");
		ok &= expect(encodeRawTexturePalette(quake.palette.colors).size() == 768 && uchar(encodeRawTexturePalette(quake.palette.colors).at(5)) == qBlue(quake.palette.colorAt(1)), "raw palettes are 256 RGB triplets");
	}

	// Doom wall textures: PNAMES/TEXTURE1 round trips and staging.
	{
		const QStringList patches{QStringLiteral("WALL00_1"), QStringLiteral("DOOR2_4")};
		QVector<DoomWallTexture> textures{{QStringLiteral("STARTAN3"), {128, 128}, false, {{0, 0, QStringLiteral("WALL00_1")}, {64, 0, QStringLiteral("DOOR2_4")}}}};
		for (bool strife : {false, true}) {
			const auto lump = encodeDoomTextureLump(textures, patches, strife);
			QStringList names; QVector<DoomWallTexture> parsed; bool detected = !strife;
			ok &= expect(parseDoomPatchNames(encodeDoomPatchNames(patches), &names) && names == patches, "PNAMES round-trips");
			ok &= expect(parseDoomTextureLump(lump, patches, &parsed, &detected) && detected == strife && parsed.size() == 1 && parsed[0].patches.size() == 2 &&
				parsed[0].patches[1].originX == 64 && parsed[0].patches[1].patch == QStringLiteral("DOOR2_4") && parsed[0].size == QSize(128, 128), strife ? "Strife TEXTURE1 round-trips" : "Doom TEXTURE1 round-trips");
		}
		QStringList none; QVector<DoomWallTexture> parsed;
		ok &= expect(!parseDoomTextureLump(encodeDoomTextureLump(textures, patches, false), none, &parsed, nullptr), "definitions naming missing patches are refused");
		const QString source = root.filePath(QStringLiteral("walls.wad"));
		put(source, wad("PWAD", {{"PNAMES", encodeDoomPatchNames(patches)}, {"TEXTURE1", encodeDoomTextureLump(textures, patches, false)}}));
		PackageArchive archive; PackageStagingModel staging;
		ok &= expect(archive.load(source, &error) && staging.loadBaseArchive(archive, &error), "load a PWAD with wall definitions");
		IdTechPaletteResolution doom; doom.palette = generatedIdTechPalette(QStringLiteral("doom")); doom.palette.generated = false;
		TextureDocument patch; patch.create({64, 72}, QColor(90, 60, 30));
		TextureExportOptions options; options.format = TextureExportFormat::DoomPatch; options.wallTexture = QStringLiteral("NEWWALL");
		const auto encoded = encodeTextureDocumentExport(patch, options, doom);
		ok &= expect(encoded.succeeded && stageTextureExport(encoded, options, QStringLiteral("NEWPATCH"), &staging, false, &error), "a patch stages with its wall definition");
		if (!error.isEmpty()) { std::cerr << error.toStdString() << '\n'; }
		const PackageStagingArchive planned(staging);
		QStringList stagedNames; QVector<DoomWallTexture> staged; bool strife = true;
		ok &= expect(parseDoomPatchNames(entry(planned, QStringLiteral("PNAMES")), &stagedNames) && stagedNames == patches + QStringList{QStringLiteral("NEWPATCH")}, "PNAMES gains the patch, keeping existing order");
		ok &= expect(parseDoomTextureLump(entry(planned, QStringLiteral("TEXTURE1")), stagedNames, &staged, &strife) && !strife && staged.size() == 2 && staged[0].name == QStringLiteral("STARTAN3") &&
			staged[1].name == QStringLiteral("NEWWALL") && staged[1].size == QSize(64, 72) && staged[1].patches.size() == 1 && staged[1].patches[0].patch == QStringLiteral("NEWPATCH"), "TEXTURE1 keeps old definitions and adds the new one");
		ok &= expect(staging.undo() && !names(PackageStagingArchive(staging)).contains(QStringLiteral("NEWPATCH")) && parseDoomPatchNames(entry(PackageStagingArchive(staging), QStringLiteral("PNAMES")), &stagedNames) && stagedNames == patches,
			"the patch and both definition lumps undo together");
		staging.redo();
		ok &= expect(!stageDoomWallTexture(&staging, QStringLiteral("NEWPATCH"), QStringLiteral("NEWWALL"), {64, 72}, false, nullptr, &error), "an existing definition needs replacement");
		DoomWallStageReport report;
		ok &= expect(stageDoomWallTexture(&staging, QStringLiteral("NEWPATCH"), QStringLiteral("NEWWALL"), {32, 72}, true, &report, &error) && report.replacedDefinition && !report.addedPatchName, "replacement redefines in place");
		const QString empty = root.filePath(QStringLiteral("empty.wad")); put(empty, wad("PWAD", {}));
		PackageArchive emptyArchive; PackageStagingModel fresh;
		ok &= expect(emptyArchive.load(empty, &error) && fresh.loadBaseArchive(emptyArchive, &error) &&
			stageDoomWallTexture(&fresh, QStringLiteral("NEWPATCH"), QStringLiteral("NEWWALL"), {64, 72}, false, &report, &error) && report.createdPatchNames && report.createdTextureLump && !report.warnings.isEmpty(),
			"a WAD without definitions gains both lumps, with the vanilla warning");
		options.doomSprite = true; options.wallTexture.clear();
		ok &= expect(stageTextureExport(encoded, options, QStringLiteral("TROOA1"), &fresh, false, &error) && names(PackageStagingArchive(fresh)).contains(QStringLiteral("S_START")), "Doom sprite patches stage into S_START/S_END");
	}

	// The CLI shares the exporters: sprite frames from a layered project.
	if (argc > 1) {
		const QString binary = QString::fromLocal8Bit(argv[1]);
		const auto cli = [&](const QStringList& arguments, int expected, QJsonObject* out = nullptr) {
			QProcess process; process.start(binary, QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"), root.filePath(QStringLiteral("settings.ini")), QStringLiteral("--json")} + arguments);
			if (!process.waitForFinished(60000)) { process.kill(); process.waitForFinished(); return false; }
			const auto result = QJsonDocument::fromJson(process.readAllStandardOutput());
			if (process.exitCode() != expected) { std::cerr << result.toJson().constData() << process.readAllStandardError().constData(); }
			if (out) { *out = result.object(); }
			return process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && result.isObject();
		};
		const QString project = root.filePath(QStringLiteral("sprite.vtexture"));
		ok &= expect(put(project, encodeTextureProject(spriteDocument())), "write a layered sprite project");
		const QString cliDir = root.filePath(QStringLiteral("cli")); QDir().mkpath(cliDir);
		const QString options = root.filePath(QStringLiteral("sp2.json"));
		put(options, R"({"name": "sprites/cli", "alpha": "threshold"})");
		QJsonObject report;
		ok &= expect(cli({QStringLiteral("texture"), QStringLiteral("export"), project, QStringLiteral("--profile"), QStringLiteral("quake2-sp2"), QStringLiteral("--export-options"), options,
			QStringLiteral("--allow-generated-palette"), QStringLiteral("--output"), QDir(cliDir).filePath(QStringLiteral("cli.sp2"))}, 0, &report), "CLI exports an SP2");
		ok &= expect(QFileInfo::exists(QDir(cliDir).filePath(QStringLiteral("cli_0.pcx"))) && QFileInfo::exists(QDir(cliDir).filePath(QStringLiteral("cli_1.pcx"))) &&
			report.value(QStringLiteral("export")).toObject().value(QStringLiteral("companions")).toArray().size() == 2, "CLI writes and reports the PCX frames");
		const QString recipe = root.filePath(QStringLiteral("adjust.json"));
		put(recipe, R"([{"op": "adjust", "invert": true}, {"op": "filter", "kind": "blur", "wrap": true}, {"op": "seamless", "width": 2}])");
		ok &= expect(cli({QStringLiteral("texture"), QStringLiteral("edit"), project, QStringLiteral("--operations"), recipe, QStringLiteral("--output"), root.filePath(QStringLiteral("adjusted.vtexture"))}, 0), "CLI recipes run the new operations");
		ok &= expect(cli({QStringLiteral("texture"), QStringLiteral("inspect"), project}, 0, &report) && report.value(QStringLiteral("frames")).toInt() == 2 && report.contains(QStringLiteral("distinctColors")), "inspect reports frames and colours");
		const QString walls = root.filePath(QStringLiteral("walls.wad"));
		const QString draft = root.filePath(QStringLiteral("walls.vibepackage"));
		TextureDocument patchDocument; patchDocument.create({64, 64}, QColor(40, 40, 40));
		const QString patchProject = root.filePath(QStringLiteral("patch.vtexture")); put(patchProject, encodeTextureProject(patchDocument));
		ok &= expect(cli({QStringLiteral("texture"), QStringLiteral("stage"), patchProject, QStringLiteral("--profile"), QStringLiteral("doom-patch"), QStringLiteral("--allow-generated-palette"),
			QStringLiteral("--target-package"), walls, QStringLiteral("--target-entry"), QStringLiteral("CLIPATCH"), QStringLiteral("--wall-texture"), QStringLiteral("cliwall"), QStringLiteral("--output"), draft, QStringLiteral("--dry-run")}, 0, &report), "CLI stages a wall texture");
		ok &= expect(report.value(QStringLiteral("export")).toObject().value(QStringLiteral("options")).toObject().value(QStringLiteral("wallTexture")).toString() == QStringLiteral("CLIWALL"), "the wall texture option reaches the exporter");
		const QString paletteOut = root.filePath(QStringLiteral("cli-palette.lmp"));
		const QStringList palette{QStringLiteral("texture"), QStringLiteral("palette"), QStringLiteral("--palette"), QStringLiteral("quake"), QStringLiteral("--output"), paletteOut};
		ok &= expect(cli(palette + QStringList{QStringLiteral("--dry-run")}, 0, &report) && !QFileInfo::exists(paletteOut) && !report.value(QStringLiteral("written")).toBool(), "palette output dry runs write nothing");
		ok &= expect(cli(palette, 0, &report) && QFileInfo(paletteOut).size() == 768 && report.value(QStringLiteral("written")).toBool(), "texture palette writes a raw palette");
		ok &= expect(cli(palette, 4), "palette output refuses implicit replacement");
	}

	std::cout << (ok ? "texture formats smoke passed" : "texture formats smoke failed") << '\n';
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
