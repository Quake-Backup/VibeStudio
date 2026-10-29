// The materials module's links to the rest of a game: Quake III dialects
// (which keywords each game loads, which script wins), RoQ videos for
// videoMap, base packages beneath a package (and Doom IWAD + PWAD), the maps
// that use a material (.map, BSPs, Doom and UDMF maps), whole-definition
// edits and rename through the graph, the face lookup the Levels page uses,
// and Doom 3's heat haze programs. Every file is generated here; no game
// data.

#include "core/material_graph.h"
#include "core/material_images.h"
#include "core/material_library.h"
#include "core/material_lookup.h"
#include "core/material_render.h"
#include "core/material_script.h"
#include "core/material_usage.h"
#include "core/material_video.h"
#include "core/package_archive.h"
#include "tests/render_test_support.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtEndian>

#include <cmath>
#include <iostream>
#include <tuple>
#include <utility>

using namespace vibestudio;

namespace {

int failures = 0;

bool expect(bool condition, const std::string& message, const QString& detail = QString())
{
	if (!condition) {
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty()) {
			std::cerr << "\n  " << detail.toUtf8().constData();
		}
		std::cerr << '\n';
		++failures;
	}
	return condition;
}

void writeFile(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(bytes);
	}
}

QByteArray tga(QRgb color, int size = 8)
{
	QByteArray bytes(18, '\0');
	bytes[2] = 2;
	qToLittleEndian<quint16>(static_cast<quint16>(size), bytes.data() + 12);
	qToLittleEndian<quint16>(static_cast<quint16>(size), bytes.data() + 14);
	bytes[16] = 32;
	bytes[17] = 0x28;
	for (int pixel = 0; pixel < size * size; ++pixel) {
		bytes.append(static_cast<char>(qBlue(color)));
		bytes.append(static_cast<char>(qGreen(color)));
		bytes.append(static_cast<char>(qRed(color)));
		bytes.append(static_cast<char>(qAlpha(color)));
	}
	return bytes;
}

void put16(QByteArray* bytes, quint16 value)
{
	char data[2];
	qToLittleEndian<quint16>(value, data);
	bytes->append(data, 2);
}

void put32(QByteArray* bytes, quint32 value)
{
	char data[4];
	qToLittleEndian<quint32>(value, data);
	bytes->append(data, 4);
}

QByteArray fixedName(const QByteArray& name, int size)
{
	QByteArray bytes = name.left(size);
	bytes.resize(size, '\0');
	return bytes;
}

// --- RoQ -------------------------------------------------------------------------

QByteArray roqChunk(quint16 id, quint16 argument, const QByteArray& data)
{
	QByteArray chunk;
	put16(&chunk, id);
	put32(&chunk, static_cast<quint32>(data.size()));
	put16(&chunk, argument);
	return chunk + data;
}

// A 16 x 16 video: a bright frame, a dark one, then a frame whose first 8 x 8
// block splits into 4 x 4 cells (the top left bright) while the rest keeps the
// dark frame.
QByteArray roqVideo()
{
	QByteArray bytes;
	put16(&bytes, 0x1084);
	put32(&bytes, 0xFFFFFFFFu);
	put16(&bytes, 30);
	QByteArray info;
	put16(&info, 16);
	put16(&info, 16);
	put16(&info, 8);
	put16(&info, 4);
	bytes += roqChunk(0x1001, 0, info);
	QByteArray codebook;
	codebook += QByteArray("\xC8\xC8\xC8\xC8\x80\x80", 6); // 2x2 cell 0: Y 200
	codebook += QByteArray("\x32\x32\x32\x32\x80\x80", 6); // 2x2 cell 1: Y 50
	codebook += QByteArray("\x00\x00\x00\x00", 4);         // 4x4 cell 0: all bright
	codebook += QByteArray("\x01\x01\x01\x01", 4);         // 4x4 cell 1: all dark
	bytes += roqChunk(0x1002, (2 << 8) | 2, codebook);
	// Four blocks coded 2 (one 4x4 cell each): flags 10 10 10 10 00 00 00 00.
	bytes += roqChunk(0x1011, 0, QByteArray("\x00\xAA\x00\x00\x00\x00", 6));
	bytes += roqChunk(0x1011, 0, QByteArray("\x00\xAA\x01\x01\x01\x01", 6));
	// Block 0 split (3), its quarters split into 2x2 cells (3 3 3 3), the
	// other blocks kept (0 0 0): flags 11 11 11 11 11 00 00 00.
	QByteArray split("\xC0\xFF", 2);
	split += QByteArray("\x00\x00\x00\x00", 4);
	split += QByteArray("\x01\x01\x01\x01", 4);
	split += QByteArray("\x01\x01\x01\x01", 4);
	split += QByteArray("\x01\x01\x01\x01", 4);
	bytes += roqChunk(0x1011, 0, split);
	return bytes;
}

void checkVideo()
{
	QString error;
	const std::shared_ptr<MaterialVideo> video = MaterialVideo::open(roqVideo(), QStringLiteral("video/test.roq"), QStringLiteral("video/test.roq"), &error);
	if (!expect(video != nullptr, "a RoQ file opens", error)) {
		return;
	}
	expect(video->width() == 16 && video->height() == 16 && video->frameCount() == 3, "the RoQ size and frame count are read",
		video->description());
	expect(std::abs(video->framesPerSecond() - 30.0) < 1.0e-9 && std::abs(video->durationSeconds() - 0.1) < 1.0e-6,
		"the header's frame rate sets the duration");
	const MaterialTexturePtr first = video->frameAt(0.0);
	const MaterialTexturePtr second = video->frameAt(1.0 / 30.0 + 0.001);
	const MaterialTexturePtr third = video->frameAt(2.0 / 30.0 + 0.001);
	expect(first && first->usable() && qRed(first->pixels.at(5 * 16 + 5)) == 200 && first->status == QStringLiteral("video"),
		"vector frames paint 4x4 codebook cells scaled to 8x8 blocks");
	expect(second && qRed(second->pixels.at(12 * 16 + 12)) == 50, "the next frame replaces the cells");
	expect(third && qRed(third->pixels.at(1 * 16 + 1)) == 200 && qRed(third->pixels.at(5 * 16 + 5)) == 50 && qRed(third->pixels.at(12 * 16 + 12)) == 50,
		"split blocks take 2x2 cells and skipped blocks keep the previous frame");
	const MaterialTexturePtr looped = video->frameAt(3.0 / 30.0 + 0.001, true);
	const MaterialTexturePtr held = video->frameAt(10.0, false);
	expect(looped && qRed(looped->pixels.at(5 * 16 + 5)) == 200, "a looping video starts again");
	expect(held && qRed(held->pixels.at(1 * 16 + 1)) == 200 && qRed(held->pixels.at(5 * 16 + 5)) == 50, "a video that does not loop holds its last frame");
	expect(!MaterialVideo::open(QByteArray("not a video"), QString(), QStringLiteral("x.roq"), &error) && !error.isEmpty(), "other bytes are refused");
	expect(materialVideoCandidates(QStringLiteral("intro")).contains(QStringLiteral("video/intro.roq")),
		"a bare videoMap name is looked for under video/ with .roq");
}

// --- Dialects ----------------------------------------------------------------------

void checkDialects()
{
	expect(detectMaterialDialect(QStringLiteral("a\n{\n\t{\n\t\tmap a.tga\n\t\tglow\n\t}\n}\n")) == MaterialDialect::JediKnight,
		"glow marks a Jedi Knight script");
	expect(detectMaterialDialect(QStringLiteral("a\n{\n\tsunshader sun\n}\n")) == MaterialDialect::Wolfenstein, "sunshader marks a Wolfenstein script");
	expect(detectMaterialDialect(QStringLiteral("textures/glow\n{\n\t{\n\t\tmap textures/glow.tga\n\t}\n}\n")) == MaterialDialect::Quake3,
		"a shader named glow is not the glow keyword");
	MaterialDialect parsed = MaterialDialect::Automatic;
	expect(materialDialectFromId(QStringLiteral("jka"), &parsed) && parsed == MaterialDialect::JediKnight && materialDialectId(parsed) == QStringLiteral("jediknight"),
		"dialect ids and aliases round-trip");
	expect(!materialDialectLastScriptWins(MaterialDialect::Quake3Original) && materialDialectLastScriptWins(MaterialDialect::Quake3),
		"Quake III 1.32 keeps the first script's definition, ioquake3 the last");

	const QString jedi = QStringLiteral(
		"textures/jk/grass\n{\n\tmaterial shortgrass\n\t{\n\t\tmap textures/jk/grass.tga\n\t}\n\t{\n\t\tmap textures/jk/blade.tga\n\t\tsurfaceSprites vertical 16 32 64 500\n\t\tssWind 1.0\n\t}\n}\n"
		"textures/jk/sign\n{\n\t{\n\t\toneshotanimMap 4 textures/jk/s1.tga textures/jk/s2.tga\n\t\tglow\n\t}\n}\n");
	const MaterialScript script = parseMaterialScript(jedi, MaterialEngine::Quake3);
	const MaterialDefinition* grass = script.find(QStringLiteral("textures/jk/grass"));
	const MaterialDefinition* sign = script.find(QStringLiteral("textures/jk/sign"));
	expect(script.dialect == MaterialDialect::JediKnight && grass && grass->engineRejection.isEmpty() && grass->surfaceType == QStringLiteral("shortgrass"),
		"Jedi Knight keywords load as Jedi Knight's and material sets the surface type");
	expect(grass && grass->stages.size() == 2 && grass->stages.at(1).surfaceSprites, "surfaceSprites marks its stage");
	expect(sign && sign->stages.size() == 1 && sign->stages.first().animationOnce && sign->stages.first().glow
			&& sign->stages.first().imageKind == MaterialImageKind::Animation,
		"oneshotanimMap plays once and glow is kept");
	const MaterialScript arena = parseMaterialScript(jedi, MaterialEngine::Quake3, QString(), MaterialDialect::Quake3);
	const MaterialDefinition* arenaSign = arena.find(QStringLiteral("textures/jk/sign"));
	expect(arenaSign && arenaSign->engineRejection.contains(QStringLiteral("Jedi Knight")), "Quake III Arena drops the Jedi Knight shader");

	const QString et = QStringLiteral("textures/et/water\n{\n\tdistanceCull 128 512 0.5\n\t{\n\t\tmap textures/et/water.tga\n\t\ttcMod swap\n\t}\n\t{\n\t\tlightmap textures/et/water_lm.tga\n\t}\n}\n");
	const MaterialScript etScript = parseMaterialScript(et, MaterialEngine::Quake3);
	const MaterialDefinition* water = etScript.find(QStringLiteral("textures/et/water"));
	expect(etScript.dialect == MaterialDialect::EnemyTerritory && water && water->engineRejection.isEmpty() && water->stages.size() == 2,
		"distanceCull and the lightmap stage load in Enemy Territory");
	expect(water && water->stages.at(0).tcMods.size() == 1 && water->stages.at(0).tcMods.first().kind == MaterialTexModKind::Swap,
		"tcMod swap swaps the coordinates in Enemy Territory");
	expect(water && water->stages.at(1).clamp && water->stages.at(1).tcGen.source == MaterialTexCoordSource::Lightmap
			&& water->stages.at(1).imagePath == QStringLiteral("textures/et/water_lm.tga"),
		"the lightmap stage draws a clamped image with lightmap coordinates");
	const MaterialScript rtcw = parseMaterialScript(et, MaterialEngine::Quake3, QString(), MaterialDialect::Wolfenstein);
	const MaterialDefinition* rtcwWater = rtcw.find(QStringLiteral("textures/et/water"));
	expect(rtcwWater && rtcwWater->engineRejection.contains(QStringLiteral("Enemy Territory")) && rtcwWater->stages.at(0).tcMods.isEmpty(),
		"Wolfenstein drops distanceCull and ignores tcMod swap");

	const QString wolf = QStringLiteral("textures/w/a\n{\n\tnomipmap\n\t{\n\t\tmap16 textures/w/a16.tga\n\t\tmap32 textures/w/a.tga\n\t\tfog on\n\t}\n}\n");
	const MaterialScript wolfScript = parseMaterialScript(wolf, MaterialEngine::Quake3);
	const MaterialDefinition* wolfA = wolfScript.find(QStringLiteral("textures/w/a"));
	expect(wolfScript.dialect == MaterialDialect::Wolfenstein && wolfA && wolfA->engineRejection.isEmpty() && wolfA->noMipMaps
			&& wolfA->stages.first().imagePath == QStringLiteral("textures/w/a.tga"),
		"Wolfenstein draws map32 on a 32-bit display and skips map16");
	const QStringList keywords = materialStageKeywords(MaterialEngine::Quake3, MaterialDialect::JediKnight);
	expect(keywords.contains(QStringLiteral("glow")) && !materialStageKeywords(MaterialEngine::Quake3, MaterialDialect::Quake3).contains(QStringLiteral("glow")),
		"completion offers each game's own keywords");
}

void checkScriptOrder(const QDir& root)
{
	const QString folder = root.filePath(QStringLiteral("order"));
	writeFile(folder + QStringLiteral("/scripts/a.shader"), "textures/x/same\n{\n\t{\n\t\tmap textures/x/a.tga\n\t}\n}\n");
	writeFile(folder + QStringLiteral("/scripts/b.shader"), "textures/x/same\n{\n\t{\n\t\tmap textures/x/b.tga\n\t}\n}\n");
	auto archive = std::make_shared<PackageArchive>();
	QString error;
	if (!expect(archive->load(folder, &error), "the script-order folder opens", error)) {
		return;
	}
	for (const auto& [dialect, expected] : {std::pair {MaterialDialect::Quake3, QStringLiteral("scripts/b.shader")},
			 std::pair {MaterialDialect::Quake3Original, QStringLiteral("scripts/a.shader")}}) {
		MaterialLibraryOptions options;
		options.dialect = dialect;
		options.includeImplicit = false;
		const MaterialLibrary library = scanMaterialLibrary(std::shared_ptr<const PackageArchiveReader>(archive), options);
		const int entry = library.indexOf(QStringLiteral("textures/x/same"));
		expect(library.dialect == dialect && entry >= 0 && library.entries.at(entry).sourcePath == expected,
			"the dialect decides which of two scripts defining a name wins", entry >= 0 ? library.entries.at(entry).sourcePath : QString());
	}
}

// --- Base packages ---------------------------------------------------------------

QByteArray doomWad(const QByteArray& magic, const QVector<std::pair<QByteArray, QByteArray>>& lumps)
{
	QByteArray body;
	QByteArray directory;
	int offset = 12;
	for (const auto& [name, data] : lumps) {
		put32(&directory, static_cast<quint32>(data.isEmpty() ? 0 : offset));
		put32(&directory, static_cast<quint32>(data.size()));
		directory += fixedName(name, 8);
		body += data;
		offset += static_cast<int>(data.size());
	}
	QByteArray header = magic;
	put32(&header, static_cast<quint32>(lumps.size()));
	put32(&header, static_cast<quint32>(offset));
	return header + body + directory;
}

void checkBasePackages(const QDir& root)
{
	const QString base = root.filePath(QStringLiteral("base"));
	const QString mod = root.filePath(QStringLiteral("mod"));
	writeFile(base + QStringLiteral("/scripts/base.shader"),
		"textures/base/a\n{\n\tqer_editorimage textures/base/a.tga\n\t{\n\t\tmap textures/base/a.tga\n\t}\n}\n"
		"textures/base/shared\n{\n\t{\n\t\tmap textures/base/a.tga\n\t}\n}\n");
	writeFile(base + QStringLiteral("/textures/base/a.tga"), tga(qRgb(10, 200, 10)));
	writeFile(mod + QStringLiteral("/scripts/mod.shader"),
		"textures/base/shared\n{\n\t{\n\t\tmap textures/base/a.tga\n\t\tblendFunc add\n\t}\n}\n"
		"textures/mod/b\n{\n\t{\n\t\tmap textures/base/a.tga\n\t}\n}\n");
	auto baseArchive = std::make_shared<PackageArchive>();
	auto modArchive = std::make_shared<PackageArchive>();
	QString error;
	if (!expect(baseArchive->load(base, &error) && modArchive->load(mod, &error), "the base and mod folders open", error)) {
		return;
	}
	MaterialLibraryOptions options;
	options.baseReaders = {baseArchive};
	const MaterialLibrary library = scanMaterialLibrary(std::shared_ptr<const PackageArchiveReader>(modArchive), options);
	const int baseA = library.indexOf(QStringLiteral("textures/base/a"));
	const int shared = library.indexOf(QStringLiteral("textures/base/shared"));
	const int modB = library.indexOf(QStringLiteral("textures/mod/b"));
	expect(library.baseCount == 1 && baseA >= 0 && library.entries.at(baseA).base, "a base package's material is listed and marked base");
	expect(shared >= 0 && !library.entries.at(shared).base && library.entries.at(shared).sourcePath == QStringLiteral("scripts/mod.shader"),
		"the package's definition wins over the base's (scripts/mod.shader sorts after scripts/base.shader)");
	expect(modB >= 0 && !library.entries.at(modB).base, "the package's own materials are not base");
	int sharedCount = 0;
	for (const MaterialLibraryEntry& entry : library.entries) {
		sharedCount += materialLookupKey(entry.name) == QStringLiteral("textures/base/shared") ? 1 : 0;
	}
	expect(sharedCount == 2, "both definitions of a shadowed name are listed");
	// Images come from the base package.
	MaterialImageSource source;
	source.archive = library.reader;
	const MaterialDefinition definition = library.definition(modB);
	const MaterialImageSet images = resolveMaterialImages(definition, source);
	const MaterialTexturePtr image = images.find(QStringLiteral("textures/base/a.tga"));
	expect(image && image->usable(), "an image only the base package has is found");
	options.listBaseMaterials = false;
	const MaterialLibrary own = scanMaterialLibrary(std::shared_ptr<const PackageArchiveReader>(modArchive), options);
	expect(own.indexOf(QStringLiteral("textures/base/a")) < 0 && own.indexOf(QStringLiteral("textures/mod/b")) >= 0,
		"base materials can be left out of the listing");

	// The face lookup the Levels page uses.
	MaterialLookup lookup(options);
	lookup.setPackage(modArchive);
	MaterialLibraryOptions listed = options;
	listed.listBaseMaterials = true;
	lookup.setOptions(listed);
	lookup.setPackage(modArchive);
	expect(!lookup.isLoaded(), "a lookup reads nothing until reloaded");
	lookup.reload();
	const MaterialLookupResult found = lookup.find(QStringLiteral("base/a"), MaterialEngine::Quake3);
	expect(lookup.isLoaded() && found.found && found.definition.name == QStringLiteral("textures/base/a") && found.displayImage == QStringLiteral("textures/base/a.tga"),
		"a Quake III map's face name finds its shader without the textures/ folder");
	const MaterialLookupResult implicit = lookup.find(QStringLiteral("nothing/here"), MaterialEngine::Quake3);
	expect(!implicit.found && implicit.definition.kind == QStringLiteral("implicit") && implicit.definition.stages.size() == 2,
		"a name no script defines is the engine's implicit shader");

	// Doom: a PWAD over its IWAD.
	QByteArray palette;
	for (int index = 0; index < 256; ++index) {
		palette.append(static_cast<char>(index));
		palette.append(static_cast<char>(index));
		palette.append(static_cast<char>(index));
	}
	const QString iwadPath = root.filePath(QStringLiteral("doom/iwad.wad"));
	const QString pwadPath = root.filePath(QStringLiteral("doom/pwad.wad"));
	writeFile(iwadPath, doomWad("IWAD", {{"PLAYPAL", palette}, {"F_START", {}}, {"NUKAGE1", QByteArray(4096, '\x40')}, {"F_END", {}}}));
	writeFile(pwadPath, doomWad("PWAD", {{"F_START", {}}, {"MYFLAT", QByteArray(4096, '\x60')}, {"F_END", {}}}));
	auto iwad = std::make_shared<PackageArchive>();
	auto pwad = std::make_shared<PackageArchive>();
	if (!expect(iwad->load(iwadPath, &error) && pwad->load(pwadPath, &error), "the WADs open", error)) {
		return;
	}
	MaterialLibraryOptions doomOptions;
	doomOptions.baseReaders = {iwad};
	const MaterialLibrary doom = scanMaterialLibrary(std::shared_ptr<const PackageArchiveReader>(pwad), doomOptions);
	const int iwadFlat = doom.indexOf(QStringLiteral("NUKAGE1"));
	const int pwadFlat = doom.indexOf(QStringLiteral("MYFLAT"));
	expect(iwadFlat >= 0 && doom.entries.at(iwadFlat).base && pwadFlat >= 0 && !doom.entries.at(pwadFlat).base,
		"a PWAD's flats join its IWAD's, the IWAD's marked base");
}

// --- Map usage -------------------------------------------------------------------

QByteArray quake3Bsp()
{
	QByteArray header("IBSP", 4);
	put32(&header, 46);
	const int headerSize = 8 + 17 * 8;
	QByteArray shaders = fixedName("textures/base/a", 64) + QByteArray(8, '\0') + fixedName("textures/mod/b", 64) + QByteArray(8, '\0');
	QByteArray surfaces;
	for (int shader : {0, 0, 1}) {
		QByteArray surface(104, '\0');
		qToLittleEndian<qint32>(shader, surface.data());
		surfaces += surface;
	}
	for (int lump = 0; lump < 17; ++lump) {
		const int offset = lump == 1 ? headerSize : lump == 13 ? headerSize + static_cast<int>(shaders.size()) : headerSize;
		const int length = lump == 1 ? static_cast<int>(shaders.size()) : lump == 13 ? static_cast<int>(surfaces.size()) : 0;
		put32(&header, static_cast<quint32>(offset));
		put32(&header, static_cast<quint32>(length));
	}
	return header + shaders + surfaces;
}

QByteArray quake2Bsp()
{
	QByteArray header("IBSP", 4);
	put32(&header, 38);
	const int headerSize = 8 + 19 * 8;
	QByteArray texinfo;
	for (const QByteArray& name : {QByteArray("e1u1/floor"), QByteArray("e1u1/wall")}) {
		QByteArray info(76, '\0');
		info.replace(40, 32, fixedName(name, 32));
		texinfo += info;
	}
	QByteArray faces;
	for (int info : {0, 1, 1}) {
		QByteArray face(20, '\0');
		qToLittleEndian<qint16>(static_cast<qint16>(info), face.data() + 10);
		faces += face;
	}
	for (int lump = 0; lump < 19; ++lump) {
		const int offset = lump == 5 ? headerSize : lump == 6 ? headerSize + static_cast<int>(texinfo.size()) : headerSize;
		const int length = lump == 5 ? static_cast<int>(texinfo.size()) : lump == 6 ? static_cast<int>(faces.size()) : 0;
		put32(&header, static_cast<quint32>(offset));
		put32(&header, static_cast<quint32>(length));
	}
	return header + texinfo + faces;
}

QByteArray quakeBsp()
{
	QByteArray header;
	put32(&header, 29);
	const int headerSize = 4 + 15 * 8;
	QByteArray miptex;
	put32(&miptex, 2);
	put32(&miptex, 12);
	put32(&miptex, 52);
	for (const QByteArray& name : {QByteArray("+0slime"), QByteArray("sky4")}) {
		miptex += fixedName(name, 16) + QByteArray(24, '\0');
	}
	QByteArray texinfo;
	for (int texture : {0, 1}) {
		QByteArray info(40, '\0');
		qToLittleEndian<qint32>(texture, info.data() + 32);
		texinfo += info;
	}
	QByteArray faces;
	for (int info : {0, 1, 1}) {
		QByteArray face(20, '\0');
		qToLittleEndian<qint16>(static_cast<qint16>(info), face.data() + 10);
		faces += face;
	}
	const int miptexAt = headerSize;
	const int texinfoAt = miptexAt + static_cast<int>(miptex.size());
	const int facesAt = texinfoAt + static_cast<int>(texinfo.size());
	for (int lump = 0; lump < 15; ++lump) {
		const int offset = lump == 2 ? miptexAt : lump == 6 ? texinfoAt : lump == 7 ? facesAt : headerSize;
		const int length = lump == 2 ? static_cast<int>(miptex.size()) : lump == 6 ? static_cast<int>(texinfo.size()) : lump == 7 ? static_cast<int>(faces.size()) : 0;
		put32(&header, static_cast<quint32>(offset));
		put32(&header, static_cast<quint32>(length));
	}
	return header + miptex + texinfo + faces;
}

QByteArray doomMaps()
{
	QByteArray sides;
	for (const auto& [upper, lower, middle] : {std::tuple {QByteArray("STARTAN3"), QByteArray("-"), QByteArray("BIGDOOR1")},
			 std::tuple {QByteArray("-"), QByteArray("-"), QByteArray("STARTAN3")}}) {
		QByteArray side(4, '\0');
		side += fixedName(upper, 8) + fixedName(lower, 8) + fixedName(middle, 8);
		put16(&side, 0);
		sides += side;
	}
	QByteArray sector(4, '\0');
	sector += fixedName("FLOOR4_8", 8) + fixedName("CEIL3_5", 8);
	sector += QByteArray(6, '\0');
	const QByteArray textmap = "namespace = \"zdoom\";\nsidedef\n{\ntexturemiddle = \"STARTAN3\";\n}\nsector\n{\ntexturefloor = \"FLOOR4_8\";\ntextureceiling = \"F_SKY1\";\n}\n";
	return doomWad("PWAD", {{"E1M1", {}}, {"THINGS", QByteArray(10, '\0')}, {"LINEDEFS", QByteArray(14, '\0')}, {"SIDEDEFS", sides},
							   {"VERTEXES", QByteArray(4, '\0')}, {"SECTORS", sector}, {"MAP01", {}}, {"TEXTMAP", textmap}, {"ENDMAP", {}}});
}

int usesIn(const QVector<MaterialMapUse>& uses, const QString& map)
{
	int total = 0;
	for (const MaterialMapUse& use : uses) {
		if (use.mapPath == map || use.mapName == map) {
			total += use.count;
		}
	}
	return total;
}

void checkUsage(const QDir& root)
{
	const QString folder = root.filePath(QStringLiteral("usage"));
	writeFile(folder + QStringLiteral("/maps/q3.map"),
		"// entity 0\n{\n\"classname\" \"worldspawn\"\n// brush 0\n{\nbrushDef\n{\n"
		"( 0 0 64 ) ( 64 0 64 ) ( 0 64 64 ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) base/a 0 0 0\n"
		"( 0 0 0 ) ( 0 64 0 ) ( 64 0 0 ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) mod/b 0 0 0\n}\n}\n"
		"// brush 1\n{\n( 0 0 0 ) ( 1 0 0 ) ( 0 1 0 ) base/a 0 0 0 0.5 0.5 0 0 0\n}\n"
		"{\npatchDef2\n{\nbase/a\n( 3 3 0 0 0 )\n(\n( ( 0 0 0 0 0 ) ( 0 0 0 0 0 ) ( 0 0 0 0 0 ) )\n)\n}\n}\n}\n");
	writeFile(folder + QStringLiteral("/maps/d3.map"),
		"Version 2\n// entity 0\n{\n\"classname\" \"worldspawn\"\n// primitive 0\n{\nbrushDef3\n{\n"
		"( 0 0 1 -64 ) ( ( 0.03125 0 0 ) ( 0 0.03125 0 ) ) \"textures/base/a\" 0 0 0\n}\n}\n}\n");
	writeFile(folder + QStringLiteral("/maps/q3.bsp"), quake3Bsp());
	writeFile(folder + QStringLiteral("/maps/q2.bsp"), quake2Bsp());
	writeFile(folder + QStringLiteral("/maps/q1.bsp"), quakeBsp());
	writeFile(folder + QStringLiteral("/maps/doom.wad"), doomMaps());
	auto archive = std::make_shared<PackageArchive>();
	QString error;
	if (!expect(archive->load(folder, &error), "the usage folder opens", error)) {
		return;
	}
	const MaterialUsageIndex usage = scanMaterialUsage(*archive);
	expect(usage.complete && usage.maps.size() >= 6, "every map is read", usage.maps.join(QStringLiteral(", ")) + usage.warnings.join(QStringLiteral("; ")));
	const QVector<MaterialMapUse> baseA = usage.usesOf(QStringLiteral("textures/base/a"));
	expect(usesIn(baseA, QStringLiteral("maps/q3.map")) == 3, "a Quake III .map's brushDef, classic and patch faces count, without textures/",
		QString::number(usesIn(baseA, QStringLiteral("maps/q3.map"))));
	expect(usesIn(baseA, QStringLiteral("maps/d3.map")) == 1, "a Doom 3 brushDef3 face counts by its full name");
	expect(usesIn(baseA, QStringLiteral("maps/q3.bsp")) == 2, "a Quake III BSP's surfaces count by shader");
	const QVector<MaterialMapUse> q3Lines = usage.usesOf(QStringLiteral("textures/base/a"));
	bool lines = false;
	for (const MaterialMapUse& use : q3Lines) {
		lines = lines || (use.mapPath == QStringLiteral("maps/q3.map") && !use.lines.isEmpty() && use.lines.first() > 0);
	}
	expect(lines, ".map uses keep their line numbers");
	expect(usesIn(usage.usesOf(QStringLiteral("e1u1/wall")), QStringLiteral("maps/q2.bsp")) == 2, "a Quake II BSP's faces count through texinfo");
	expect(usesIn(usage.usesOf(QStringLiteral("sky4")), QStringLiteral("maps/q1.bsp")) == 2, "a Quake BSP's faces count through texinfo and miptex");
	const QVector<MaterialMapUse> startan = usage.usesOf(QStringLiteral("STARTAN3"));
	expect(usesIn(startan, QStringLiteral("E1M1")) == 2 && usesIn(startan, QStringLiteral("MAP01")) == 1,
		"Doom sidedefs and UDMF fields count, map by map");
	expect(usage.useCount(QStringLiteral("FLOOR4_8")) == 2 && usage.useCount(QStringLiteral("unused/material")) == 0, "sector flats count too");

	// The open WAD's own maps.
	auto wad = std::make_shared<PackageArchive>();
	if (expect(wad->load(folder + QStringLiteral("/maps/doom.wad"), &error), "the Doom map WAD opens", error)) {
		const MaterialUsageIndex own = scanMaterialUsage(*wad);
		expect(own.maps.contains(QStringLiteral("E1M1")) && own.useCount(QStringLiteral("BIGDOOR1")) == 1, "an open WAD's maps are read in directory order");
	}
}

// --- Edits -------------------------------------------------------------------------

void checkWholeDefinitionEdits()
{
	const QString text = QStringLiteral("textures/x/a\n{\n\t// keep\n\t{\n\t\tmap textures/x/a.tga\n\t}\n}\n\ntextures/x/c\n{\n}\n");
	const MaterialScript script = parseMaterialScript(text, MaterialEngine::Quake3, QStringLiteral("scripts/x.shader"));
	MaterialEdit duplicate;
	duplicate.kind = MaterialEditKind::DuplicateDefinition;
	duplicate.material = QStringLiteral("textures/x/a");
	duplicate.text = QStringLiteral("textures/x/b");
	const MaterialEditResult copied = applyMaterialEdit(script, duplicate);
	expect(copied.ok && copied.script.materials.size() == 3 && copied.script.materials.at(1).name == QStringLiteral("textures/x/b")
			&& copied.text.count(QStringLiteral("// keep")) == 2,
		"a duplicate is the definition's own text, renamed, after it", copied.error);
	MaterialEdit clash;
	clash.kind = MaterialEditKind::RenameDefinition;
	clash.material = QStringLiteral("textures/x/a");
	clash.text = QStringLiteral("textures/x/c");
	expect(!applyMaterialEdit(script, clash).ok, "renaming onto a name the script defines is refused");
	MaterialEditKind kind = MaterialEditKind::SetDirective;
	expect(materialEditKindFromId(QStringLiteral("duplicate"), &kind) && kind == MaterialEditKind::DuplicateDefinition, "duplicate is a JSON edit op");

	// Through the graph: the material node's name.
	MaterialGraphEdit rename;
	rename.kind = MaterialGraphEditKind::SetProperty;
	rename.node = QStringLiteral("material");
	rename.property = QStringLiteral("name");
	rename.value = QStringLiteral("textures/x/renamed");
	const MaterialGraphEditResult renamed = applyMaterialGraphEdit(text, MaterialTextKind::Quake3Shader, QStringLiteral("textures/x/a"), rename);
	expect(renamed.ok && renamed.renamedTo == QStringLiteral("textures/x/renamed") && renamed.text.startsWith(QStringLiteral("textures/x/renamed\n{")),
		"setting the material node's name renames the definition", renamed.error);
	const MaterialGraph graph = buildMaterialGraph(script.materials.first(), MaterialTextKind::Quake3Shader);
	const MaterialGraphNode* output = graph.node(QStringLiteral("material"));
	expect(output && output->property(QStringLiteral("name")) && output->property(QStringLiteral("name"))->value == QStringLiteral("textures/x/a"),
		"the material node shows its name as a property");
}

// --- Doom 3 heat haze ----------------------------------------------------------------

void checkHeatHaze(const QDir& root)
{
	const QString folder = root.filePath(QStringLiteral("d3"));
	writeFile(folder + QStringLiteral("/materials/haze.mtr"),
		"textures/sfx/haze\n{\n\ttranslucent\n\t{\n\t\tprogram heatHaze.vfp\n\t\tvertexParm 0 time * 0.1, time * 0.5\n\t\tvertexParm 1 1.5\n"
		"\t\tfragmentMap 0 _currentRender\n\t\tfragmentMap 1 textures/sfx/haze_n\n\t}\n}\n");
	QImage normals(16, 16, QImage::Format_ARGB32);
	for (int y = 0; y < 16; ++y) {
		for (int x = 0; x < 16; ++x) {
			normals.setPixel(x, y, qRgb(x * 16, y * 16, 255));
		}
	}
	QByteArray bytes(18, '\0');
	bytes[2] = 2;
	qToLittleEndian<quint16>(16, bytes.data() + 12);
	qToLittleEndian<quint16>(16, bytes.data() + 14);
	bytes[16] = 32;
	bytes[17] = 0x28;
	for (int y = 0; y < 16; ++y) {
		for (int x = 0; x < 16; ++x) {
			const QRgb pixel = normals.pixel(x, y);
			bytes.append(static_cast<char>(qBlue(pixel)));
			bytes.append(static_cast<char>(qGreen(pixel)));
			bytes.append(static_cast<char>(qRed(pixel)));
			bytes.append('\xFF');
		}
	}
	writeFile(folder + QStringLiteral("/textures/sfx/haze_n.tga"), bytes);
	auto archive = std::make_shared<PackageArchive>();
	QString error;
	if (!expect(archive->load(folder, &error), "the Doom 3 folder opens", error)) {
		return;
	}
	const MaterialLibrary library = scanMaterialLibrary(std::shared_ptr<const PackageArchiveReader>(archive));
	const MaterialDefinition haze = library.definition(library.indexOf(QStringLiteral("textures/sfx/haze")));
	expect(haze.stages.size() == 1 && haze.stages.first().vertexParms.size() == 2 && haze.stages.first().vertexParms.at(0)[0] >= 0
			&& haze.stages.first().vertexParms.at(0)[1] >= 0 && haze.stages.first().fragmentMaps.size() == 2,
		"vertexParm and fragmentMap values are kept on the stage");
	expect(haze.imageReferences().contains(QStringLiteral("textures/sfx/haze_n")) && !haze.imageReferences().contains(QStringLiteral("_currentRender")),
		"a fragment map is an image the material reads; _currentRender is not");
	MaterialImageSource source;
	source.archive = library.reader;
	const MaterialImageSet images = resolveMaterialImages(haze, source);
	const MaterialTexturePtr normalMap = images.find(QStringLiteral("textures/sfx/haze_n"));
	expect(normalMap && normalMap->usable(), "the heat haze normal map is read");
	if (vibestudio::test_support::exitCodeWithoutRenderer("material-integration-smoke") < 0) {
		MaterialRenderOptions options;
		options.size = QSize(64, 64);
		options.time = 0.5;
		const MaterialRenderResult result = renderMaterial(haze, images, library.tables, options);
		expect(!result.image.isNull() && result.stagesDrawn >= 1 && result.notes.join(QLatin1Char(' ')).contains(QStringLiteral("heatHaze")),
			"the heat haze stage is drawn as an approximation and says so", result.notes.join(QStringLiteral(" | ")) + result.error);
	}

	// bumpyEnvironment: a cube map reflected about a normal map.
	const QStringList faces {QStringLiteral("_px"), QStringLiteral("_nx"), QStringLiteral("_py"), QStringLiteral("_ny"), QStringLiteral("_pz"),
		QStringLiteral("_nz")};
	for (int face = 0; face < faces.size(); ++face) {
		QByteArray side(18, '\0');
		side[2] = 2;
		qToLittleEndian<quint16>(4, side.data() + 12);
		qToLittleEndian<quint16>(4, side.data() + 14);
		side[16] = 32;
		side[17] = 0x28;
		for (int pixel = 0; pixel < 16; ++pixel) {
			side.append(static_cast<char>(face * 40));
			side.append(static_cast<char>(200 - face * 30));
			side.append(static_cast<char>(60 + face * 30));
			side.append('\xFF');
		}
		writeFile(folder + QStringLiteral("/env/shiny") + faces.at(face) + QStringLiteral(".tga"), side);
	}
	writeFile(folder + QStringLiteral("/materials/glass.mtr"),
		"textures/glass/shiny\n{\n\ttranslucent\n\t{\n\t\tprogram bumpyEnvironment.vfp\n\t\tfragmentMap 0 cubeMap env/shiny\n"
		"\t\tfragmentMap 1 textures/sfx/haze_n\n\t}\n}\n");
	auto glassArchive = std::make_shared<PackageArchive>();
	if (!expect(glassArchive->load(folder, &error), "the Doom 3 folder opens again", error)) {
		return;
	}
	const MaterialLibrary glassLibrary = scanMaterialLibrary(std::shared_ptr<const PackageArchiveReader>(glassArchive));
	const MaterialDefinition glass = glassLibrary.definition(glassLibrary.indexOf(QStringLiteral("textures/glass/shiny")));
	expect(glass.stages.size() == 1 && glass.stages.first().fragmentMapCubes.value(0) == 1 && glass.stages.first().fragmentMapCubes.value(1) == 0,
		"fragmentMap remembers which maps are cube maps");
	MaterialImageSource glassSource;
	glassSource.archive = glassLibrary.reader;
	const MaterialImageSet glassImages = resolveMaterialImages(glass, glassSource);
	const MaterialCubeTexturePtr environment = glassImages.findCube(QStringLiteral("env/shiny"));
	expect(environment && environment->isValid(), "a fragment cube map is read as a cube");
	if (vibestudio::test_support::exitCodeWithoutRenderer("material-integration-smoke") < 0) {
		MaterialRenderOptions options;
		options.size = QSize(64, 64);
		const MaterialRenderResult result = renderMaterial(glass, glassImages, glassLibrary.tables, options);
		expect(!result.image.isNull() && result.stagesDrawn >= 1 && result.notes.join(QLatin1Char(' ')).contains(QStringLiteral("bumpyEnvironment")),
			"the bumpy environment stage is drawn as an approximation and says so", result.notes.join(QStringLiteral(" | ")) + result.error);
	}
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	const QDir root(temp.path());
	checkVideo();
	checkDialects();
	checkScriptOrder(root);
	checkBasePackages(root);
	checkUsage(root);
	checkWholeDefinitionEdits();
	checkHeatHaze(root);
	if (failures > 0) {
		std::cerr << failures << " material integration check(s) failed\n";
		return 1;
	}
	std::cout << "material integration smoke passed\n";
	return 0;
}
