// Image lookup, damaged files, caches, the library scan and map usage.
// No game data: every file is generated here.

#include "core/material_classic.h"
#include "core/material_images.h"
#include "core/material_library.h"
#include "core/material_lookup.h"
#include "core/material_script.h"
#include "core/material_usage.h"
#include "core/material_video.h"
#include "core/package_archive.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QtEndian>

#include <atomic>
#include <iostream>
#include <memory>
#include <string>
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

QByteArray tga(QRgb color, int size = 8)
{
	QByteArray bytes(18, '\0');
	bytes[2] = 2;
	qToLittleEndian<quint16>(static_cast<quint16>(size), bytes.data() + 12);
	qToLittleEndian<quint16>(static_cast<quint16>(size), bytes.data() + 14);
	bytes[16] = 32;
	bytes[17] = 0x28;
	QByteArray pixel;
	pixel.append(static_cast<char>(qBlue(color)));
	pixel.append(static_cast<char>(qGreen(color)));
	pixel.append(static_cast<char>(qRed(color)));
	pixel.append(static_cast<char>(qAlpha(color)));
	bytes += pixel.repeated(size * size);
	return bytes;
}

QByteArray png(QRgb color, int size = 8)
{
	QImage image(size, size, QImage::Format_ARGB32);
	image.fill(color);
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	return bytes;
}

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

// A Doom picture: one post per column, every pixel `index`.
QByteArray doomPatch(int width, int height, int index)
{
	QByteArray bytes;
	put16(&bytes, static_cast<quint16>(width));
	put16(&bytes, static_cast<quint16>(height));
	put16(&bytes, 0);
	put16(&bytes, 0);
	const int columnsAt = 8 + 4 * width;
	const int columnSize = 4 + height + 1;
	for (int x = 0; x < width; ++x) {
		put32(&bytes, static_cast<quint32>(columnsAt + x * columnSize));
	}
	for (int x = 0; x < width; ++x) {
		bytes.append('\0');
		bytes.append(static_cast<char>(height));
		bytes.append('\0');
		bytes += QByteArray(height, static_cast<char>(index));
		bytes.append('\0');
		bytes.append('\xFF');
	}
	return bytes;
}

QByteArray pnamesLump(const QList<QByteArray>& names)
{
	QByteArray bytes;
	put32(&bytes, static_cast<quint32>(names.size()));
	for (const QByteArray& name : names) {
		bytes += fixedName(name, 8);
	}
	return bytes;
}

struct WallDef {
	QByteArray name;
	int width = 64;
	int height = 64;
	QVector<int> patches; // PNAMES indexes, each placed at 0,0
};

QByteArray texture1Lump(const QVector<WallDef>& walls)
{
	QByteArray header;
	put32(&header, static_cast<quint32>(walls.size()));
	QByteArray records;
	const int base = 4 + 4 * static_cast<int>(walls.size());
	for (const WallDef& wall : walls) {
		put32(&header, static_cast<quint32>(base + records.size()));
		QByteArray record = fixedName(wall.name, 8);
		put32(&record, 0);
		put16(&record, static_cast<quint16>(wall.width));
		put16(&record, static_cast<quint16>(wall.height));
		put32(&record, 0);
		put16(&record, static_cast<quint16>(wall.patches.size()));
		for (int patch : wall.patches) {
			put16(&record, 0);
			put16(&record, 0);
			put16(&record, static_cast<quint16>(patch));
			put16(&record, 1);
			put16(&record, 0);
		}
		records += record;
	}
	return header + records;
}

QByteArray boomAnimated(const QVector<std::pair<QByteArray, QByteArray>>& textureRanges)
{
	QByteArray bytes;
	for (const auto& [first, last] : textureRanges) {
		bytes.append('\x01');
		bytes += fixedName(last, 9);
		bytes += fixedName(first, 9);
		put32(&bytes, 8);
	}
	bytes.append('\xFF');
	return bytes;
}

// An in-memory package that counts how often its entry list is read.
class MemoryReader final : public PackageArchiveReader {
public:
	explicit MemoryReader(PackageArchiveFormat format = PackageArchiveFormat::Folder)
		: m_format(format)
	{
	}

	void add(const QString& path, const QByteArray& bytes, const QString& typeHint = QString())
	{
		PackageEntry entry;
		entry.virtualPath = path;
		entry.sizeBytes = static_cast<quint64>(bytes.size());
		entry.typeHint = typeHint;
		entry.sourceOrdinal = m_entries.size();
		m_entries.push_back(entry);
		m_data.push_back(bytes);
	}

	[[nodiscard]] PackageArchiveFormat format() const override { return m_format; }
	[[nodiscard]] QString sourcePath() const override { return QStringLiteral("memory"); }
	[[nodiscard]] bool isOpen() const override { return true; }
	[[nodiscard]] QVector<PackageEntry> entries() const override
	{
		++m_entryReads;
		return m_entries;
	}
	bool readEntryBytes(const QString& virtualPath, QByteArray* out, QString* error, qint64 maxBytes = -1) const override
	{
		for (qsizetype index = m_entries.size() - 1; index >= 0; --index) {
			if (m_entries.at(index).virtualPath.compare(virtualPath, Qt::CaseInsensitive) == 0) {
				return readEntryAt(index, out, error, maxBytes);
			}
		}
		if (error) {
			*error = QStringLiteral("missing");
		}
		return false;
	}
	bool readEntryAt(qsizetype index, QByteArray* out, QString* error, qint64 maxBytes = -1) const override
	{
		if (index < 0 || index >= m_data.size()) {
			if (error) {
				*error = QStringLiteral("no entry");
			}
			return false;
		}
		*out = maxBytes >= 0 ? m_data.at(index).left(maxBytes) : m_data.at(index);
		return true;
	}

	[[nodiscard]] int entryReads() const { return m_entryReads.load(); }

private:
	PackageArchiveFormat m_format;
	QVector<PackageEntry> m_entries;
	QVector<QByteArray> m_data;
	mutable std::atomic<int> m_entryReads {0};
};

MaterialDefinition quake3Shader(const QString& text, const QString& name, MaterialDialect dialect = MaterialDialect::Quake3)
{
	const MaterialScript script = parseMaterialScript(text, MaterialEngine::Quake3, QStringLiteral("scripts/test.shader"), dialect);
	const MaterialDefinition* definition = script.find(name);
	return definition ? *definition : MaterialDefinition();
}

// --- Quake III image rules (F1, C12) -------------------------------------------

void checkExtensionRules(const QDir& root)
{
	using Rule = MaterialImageExtensionRule;
	expect(materialImageExtensionRule(MaterialDialect::Automatic) == Rule::Ioquake3 && materialImageExtensionRule(MaterialDialect::Quake3) == Rule::Ioquake3
			&& materialImageExtensionRule(MaterialDialect::Quake3Original) == Rule::Quake3Original
			&& materialImageExtensionRule(MaterialDialect::EnemyTerritory) == Rule::Quake3Original
			&& materialImageExtensionRule(MaterialDialect::JediKnight) == Rule::JediKnight,
		"each dialect maps to its game's image rule");
	const QStringList original = materialImageCandidates(MaterialEngine::Quake3, QStringLiteral("textures/a/b.tga"), Rule::Quake3Original);
	expect(original == QStringList {QStringLiteral("textures/a/b.tga"), QStringLiteral("textures/a/b.jpg")}, "Quake III 1.32 retries .tga as .jpg only",
		original.join(QLatin1Char(' ')));
	expect(materialImageCandidates(MaterialEngine::Quake3, QStringLiteral("textures/a/b.png"), Rule::Quake3Original).isEmpty()
			&& materialImageCandidates(MaterialEngine::Quake3, QStringLiteral("textures/a/b"), Rule::Quake3Original).isEmpty(),
		"Quake III 1.32 loads no .png and no name without an extension");
	expect(materialImageCandidates(MaterialEngine::Quake3, QStringLiteral("textures/a/b.pcx"), Rule::Quake3Original)
			== QStringList {QStringLiteral("textures/a/b.pcx")},
		"Quake III 1.32 loads .pcx as written");
	const QStringList ioq3 = materialImageCandidates(MaterialEngine::Quake3, QStringLiteral("textures/a/b.tga"));
	expect(ioq3.first() == QStringLiteral("textures/a/b.tga") && ioq3.contains(QStringLiteral("textures/a/b.png"))
			&& ioq3.contains(QStringLiteral("textures/a/b.bmp")),
		"ioquake3 tries the written name, then every loader's extension", ioq3.join(QLatin1Char(' ')));
	const QStringList jedi = materialImageCandidates(MaterialEngine::Quake3, QStringLiteral("textures/a/b.tga"), Rule::JediKnight);
	expect(jedi.first() == QStringLiteral("textures/a/b.tga") && jedi.contains(QStringLiteral("textures/a/b.png"))
			&& !jedi.contains(QStringLiteral("textures/a/b.pcx")),
		"Jedi Knight tries jpg, tga and png only", jedi.join(QLatin1Char(' ')));

	const QString folder = root.filePath(QStringLiteral("q3rules"));
	writeFile(folder + QStringLiteral("/textures/a/b.png"), png(qRgb(200, 20, 20)));
	auto archive = std::make_shared<PackageArchive>();
	QString error;
	if (!expect(archive->load(folder, &error), "the Quake III rules folder opens", error)) {
		return;
	}
	const QString text = QStringLiteral("textures/a/s\n{\n\t{\n\t\tmap textures/a/b.tga\n\t}\n}\n"
										"textures/a/bare\n{\n\t{\n\t\tmap textures/a/b\n\t}\n}\n");
	MaterialImageSource source;
	source.archive = archive;
	const MaterialImageSet ioq3Images = resolveMaterialImages(quake3Shader(text, QStringLiteral("textures/a/s")), source);
	const MaterialTexturePtr found = ioq3Images.find(QStringLiteral("textures/a/b.tga"));
	expect(found && found->usable() && ioq3Images.complete, "ioquake3 finds a .png image a stage names as .tga");
	source.extensionRule = Rule::Quake3Original;
	const MaterialImageSet originalImages = resolveMaterialImages(quake3Shader(text, QStringLiteral("textures/a/s"), MaterialDialect::Quake3Original), source);
	const MaterialTexturePtr lost = originalImages.find(QStringLiteral("textures/a/b.tga"));
	expect(lost && !lost->usable() && !originalImages.complete && originalImages.missing().contains(QStringLiteral("textures/a/b.tga")),
		"Quake III 1.32 does not find the .png, so the shader is incomplete");
	const MaterialImageSet bareImages = resolveMaterialImages(quake3Shader(text, QStringLiteral("textures/a/bare"), MaterialDialect::Quake3Original), source);
	const MaterialTexturePtr bare = bareImages.find(QStringLiteral("textures/a/b"));
	expect(bare && !bare->usable() && !bare->note.isEmpty() && !bare->note.trimmed().endsWith(QLatin1Char(':')),
		"a name the game reads no file for says why instead of an empty list", bare ? bare->note : QString());
}

// --- Image cache (C11) -----------------------------------------------------------

MaterialTexturePtr blankTexture(int width, int height)
{
	auto texture = std::make_shared<MaterialTexture>();
	texture->width = width;
	texture->height = height;
	texture->pixels.resize(static_cast<qsizetype>(width) * height);
	texture->status = QStringLiteral("image");
	return texture;
}

void checkImageCache()
{
	MaterialImageCache cache(16LL * 1024 * 1024);
	cache.insert(QStringLiteral("a"), blankTexture(1536, 1024)); // 6 MiB
	cache.insert(QStringLiteral("b"), blankTexture(1536, 1024)); // 6 MiB
	cache.find(QStringLiteral("b"));
	// Replacing "a" (the oldest) with 11 MiB evicts "b"; "a"'s old bytes are
	// subtracted once.
	cache.insert(QStringLiteral("a"), blankTexture(2816, 1024));
	expect(cache.bytes() == 11LL * 1024 * 1024 && cache.find(QStringLiteral("a")) && !cache.find(QStringLiteral("b")),
		"replacing a cached image keeps the byte count equal to what is resident", QString::number(cache.bytes()));
}

// --- Palette search (C1) --------------------------------------------------------

void checkPaletteSearch()
{
	auto reader = std::make_shared<MemoryReader>();
	for (int index = 0; index < 20000; ++index) {
		reader->add(QStringLiteral("textures/filler/f%1.jpg").arg(index), QByteArray());
	}
	reader->add(QStringLiteral("textures/x/a.tga"), tga(qRgb(0, 200, 0)));
	const MaterialDefinition shader = quake3Shader(QStringLiteral("textures/x/s\n{\n\t{\n\t\tmap textures/x/a.tga\n\t}\n}\n"), QStringLiteral("textures/x/s"));
	MaterialImageSource source;
	source.archive = reader;
	const int before = reader->entryReads();
	QElapsedTimer timer;
	timer.start();
	bool ok = true;
	for (int index = 0; index < 200; ++index) {
		const MaterialImageSet images = resolveMaterialImages(shader, source);
		ok = ok && images.complete && images.paletteGenerated;
	}
	const int reads = reader->entryReads() - before;
	expect(ok && reads <= 2, "Quake III images never search the package for a game palette, and its index is built once",
		QStringLiteral("%1 entry list reads, %2 ms").arg(reads).arg(timer.elapsed()));

	// Quake: the palette lives only in the base layer, and is found once.
	auto base = std::make_shared<MemoryReader>();
	QByteArray palette;
	for (int index = 0; index < 256; ++index) {
		palette.append(static_cast<char>(index)).append(static_cast<char>(255 - index)).append(static_cast<char>(index / 2));
	}
	base->add(QStringLiteral("gfx/palette.lmp"), palette);
	auto mod = std::make_shared<MemoryReader>();
	mod->add(QStringLiteral("textures/#lava1.tga"), tga(qRgb(250, 80, 0)));
	auto layered = std::make_shared<MaterialLayeredReader>(mod, QVector<std::shared_ptr<const PackageArchiveReader>> {base});
	MaterialImageSource quakeSource;
	quakeSource.archive = layered;
	const MaterialDefinition lava = quakeMaterialDefinition(QStringLiteral("*lava1"), {QStringLiteral("*lava1")});
	const int layeredBefore = base->entryReads();
	bool real = true;
	for (int index = 0; index < 20; ++index) {
		const MaterialImageSet images = resolveMaterialImages(lava, quakeSource);
		real = real && !images.paletteGenerated;
		if (index == 0) {
			const MaterialTexturePtr texture = images.find(QStringLiteral("*lava1"));
			expect(texture && texture->usable(), "a Quake liquid finds its loose replacement under # (DarkPlaces, FTE)");
		}
	}
	expect(real, "a palette only the base layer has is the game's, not a stand-in");
	expect(base->entryReads() - layeredBefore <= 2, "the palette search runs once per package snapshot",
		QString::number(base->entryReads() - layeredBefore));
}

// --- Doom catalogs (C2, C5, C10, C17) --------------------------------------------

void checkDoomScale()
{
	// 3000 walls and 100 Boom ANIMATED ranges of three.
	QVector<WallDef> walls;
	for (int index = 0; index < 3000; ++index) {
		walls.push_back({QStringLiteral("T%1").arg(index, 4, 10, QLatin1Char('0')).toLatin1(), 64, 64, {}});
	}
	QVector<std::pair<QByteArray, QByteArray>> ranges;
	for (int range = 0; range < 100; ++range) {
		ranges.push_back({walls.at(range * 3).name, walls.at(range * 3 + 2).name});
	}
	MemoryReader reader(PackageArchiveFormat::Wad);
	reader.add(QStringLiteral("TEXTURE1"), texture1Lump(walls), QStringLiteral("wad-lump"));
	reader.add(QStringLiteral("ANIMATED"), boomAnimated(ranges), QStringLiteral("wad-lump"));
	QElapsedTimer timer;
	timer.start();
	int phases = 0;
	const MaterialLibrary library = scanMaterialLibrary(reader, {}, [&](int, int, const QString&) {
		++phases;
		return true;
	});
	const qint64 elapsed = timer.elapsed();
	expect(library.entries.size() == 3000 && elapsed < 30000, "3000 walls with 100 ranges are read quickly",
		QStringLiteral("%1 entries in %2 ms").arg(library.entries.size()).arg(elapsed));
	expect(phases > 10, "reading Doom textures reports progress as it goes", QString::number(phases));
	const int t3 = library.indexOf(QStringLiteral("T0003"), MaterialEngine::Doom);
	const int t4 = library.indexOf(QStringLiteral("T0004"), MaterialEngine::Doom);
	const MaterialDefinition first = library.definition(t3);
	const MaterialDefinition second = library.definition(t4);
	expect(first.classic.frames.size() == 3 && first.classic.frames.at(0).name == QStringLiteral("T0003")
			&& second.classic.frames.size() == 3 && second.classic.frames.at(0).name == QStringLiteral("T0004")
			&& second.classic.frames.at(2).name == QStringLiteral("T0003"),
		"range members rotate by their absolute texture number");
	const int t2999 = library.indexOf(QStringLiteral("T2999"), MaterialEngine::Doom);
	expect(t2999 >= 0 && library.definition(t2999).classic.frames.isEmpty(), "a wall outside every range does not animate");
	bool cancelled = false;
	const MaterialLibrary stopped = scanMaterialLibrary(reader, {}, [&](int, int, const QString& phase) {
		if (phase.contains(QStringLiteral("Doom"))) {
			cancelled = true;
			return false;
		}
		return true;
	});
	expect(cancelled && stopped.cancelled, "reading Doom textures can be cancelled");
}

void checkDoomNamespaces(const QDir& root)
{
	// ZDoom PK3 folders: flats/ and textures/.
	const QString folder = root.filePath(QStringLiteral("zdoom"));
	writeFile(folder + QStringLiteral("/flats/F1.png"), png(qRgb(20, 200, 20), 64));
	writeFile(folder + QStringLiteral("/textures/W1.png"), png(qRgb(200, 20, 20), 32));
	auto archive = std::make_shared<PackageArchive>();
	QString error;
	if (expect(archive->load(folder, &error), "the ZDoom folder opens", error)) {
		const MaterialLibrary library = scanMaterialLibrary(std::shared_ptr<const PackageArchiveReader>(archive));
		MaterialImageSource source;
		source.archive = library.reader;
		source.doomCatalog = library.doomCatalog;
		const int wall = library.indexOfKind(QStringLiteral("W1"), MaterialEngine::Doom, QStringLiteral("doom-wall"));
		const int flat = library.indexOfKind(QStringLiteral("F1"), MaterialEngine::Doom, QStringLiteral("doom-flat"));
		if (expect(wall >= 0 && flat >= 0, "a PK3's textures/ wall and flats/ flat are listed")) {
			const MaterialTexturePtr wallImage = resolveMaterialImages(library.definition(wall), source).find(QStringLiteral("W1"));
			const MaterialTexturePtr flatImage = resolveMaterialImages(library.definition(flat), source).find(QStringLiteral("F1"));
			expect(wallImage && wallImage->usable() && wallImage->width == 32, "a textures/ wall resolves", wallImage ? wallImage->note : QString());
			expect(flatImage && flatImage->usable(), "a flats/ flat resolves", flatImage ? flatImage->note : QString());
		}
	}

	// A wall and a flat of the same name, and a wall whose patch is missing.
	QByteArray flatPixels(4096, static_cast<char>(40));
	const QByteArray wad = doomWad("PWAD", {{"PNAMES", pnamesLump({"PATCHA", "GONE"})},
		{"TEXTURE1", texture1Lump({{"SAME", 16, 16, {0}}, {"BROKEN", 16, 16, {0, 1}}, {"BADINDEX", 16, 16, {7}}})}, {"P_START", {}},
		{"PATCHA", doomPatch(16, 16, 90)}, {"P_END", {}}, {"F_START", {}}, {"SAME", flatPixels}, {"F_END", {}}});
	const QString wadPath = root.filePath(QStringLiteral("same.wad"));
	writeFile(wadPath, wad);
	auto doom = std::make_shared<PackageArchive>();
	if (!expect(doom->load(wadPath, &error), "the Doom WAD opens", error)) {
		return;
	}
	const MaterialLibrary library = scanMaterialLibrary(std::shared_ptr<const PackageArchiveReader>(doom));
	const int wall = library.indexOfKind(QStringLiteral("SAME"), MaterialEngine::Doom, QStringLiteral("doom-wall"));
	const int flat = library.indexOfKind(QStringLiteral("SAME"), MaterialEngine::Doom, QStringLiteral("doom-flat"));
	expect(wall >= 0 && flat >= 0 && wall != flat && !library.entries.at(wall).shadowed && !library.entries.at(flat).shadowed,
		"a Doom wall and flat of one name are both listed and neither shadows the other");
	const int broken = library.indexOf(QStringLiteral("BROKEN"), MaterialEngine::Doom);
	const int badIndex = library.indexOf(QStringLiteral("BADINDEX"), MaterialEngine::Doom);
	if (expect(broken >= 0 && badIndex >= 0, "the broken walls are listed")) {
		const MaterialDefinition definition = library.definition(broken);
		bool diagnosed = false;
		for (const MaterialDiagnostic& diagnostic : definition.diagnostics) {
			diagnosed = diagnosed || (diagnostic.code == QStringLiteral("missing-patch") && diagnostic.message.contains(QStringLiteral("GONE")));
		}
		expect(diagnosed, "a patch no lump holds is diagnosed");
		bool indexError = false;
		for (const MaterialDiagnostic& diagnostic : library.definition(badIndex).diagnostics) {
			indexError = indexError || (diagnostic.code == QStringLiteral("missing-patch") && diagnostic.severity == MaterialDiagnosticSeverity::Error);
		}
		expect(indexError, "a patch number PNAMES does not list is an error");
		MaterialImageSource source;
		source.archive = library.reader;
		source.doomCatalog = library.doomCatalog;
		const MaterialImageSet images = resolveMaterialImages(definition, source);
		const MaterialTexturePtr composite = images.find(QStringLiteral("BROKEN"));
		expect(composite && composite->usable() && !images.complete && images.missing().contains(QStringLiteral("GONE")),
			"a composite with a missing patch still draws, and the patch is listed as missing", images.missing().join(QStringLiteral(", ")));
	}

	// A composite placing one patch many times decodes it once.
	QVector<int> many(500, 0);
	const QByteArray repeated = doomWad("PWAD", {{"PNAMES", pnamesLump({"PATCHA"})}, {"TEXTURE1", texture1Lump({{"MANY", 64, 64, many}})},
		{"P_START", {}}, {"PATCHA", doomPatch(16, 16, 90)}, {"P_END", {}}});
	QString wadError;
	const QString manyPath = root.filePath(QStringLiteral("many.wad"));
	writeFile(manyPath, repeated);
	auto manyWad = std::make_shared<PackageArchive>();
	if (expect(manyWad->load(manyPath, &wadError), "the repeated-patch WAD opens", wadError)) {
		const MaterialLibrary manyLibrary = scanMaterialLibrary(std::shared_ptr<const PackageArchiveReader>(manyWad));
		MaterialImageSource source;
		source.archive = manyLibrary.reader;
		source.doomCatalog = manyLibrary.doomCatalog;
		QElapsedTimer timer;
		timer.start();
		const MaterialImageSet images = resolveMaterialImages(manyLibrary.definition(manyLibrary.indexOf(QStringLiteral("MANY"))), source);
		const MaterialTexturePtr composite = images.find(QStringLiteral("MANY"));
		expect(composite && composite->usable() && composite->hasIndices() && composite->indices.at(0) == 90 && timer.elapsed() < 10000,
			"a patch placed 500 times composes quickly with its palette indices", QString::number(timer.elapsed()));
		const MaterialImageSet again = resolveMaterialImages(manyLibrary.definition(manyLibrary.indexOf(QStringLiteral("MANY"))), source);
		expect(again.doomColormap == images.doomColormap && again.doomColormapGenerated, "a generated colormap is the same each time");
	}
}

// --- Quake II sidecars (C14) ------------------------------------------------------

void checkWalSidecar(const QDir& root)
{
	const QString folder = root.filePath(QStringLiteral("q2"));
	QByteArray wal(100 + 16 * 16 + 8 * 8 + 4 * 4 + 2 * 2, '\0');
	Quake2WalInfo info;
	info.size = QSize(16, 16);
	QString error;
	rewriteQuake2WalHeader(&wal, info, &error);
	qToLittleEndian<quint32>(16, wal.data() + 32);
	qToLittleEndian<quint32>(16, wal.data() + 36);
	writeFile(folder + QStringLiteral("/textures/e1u1/blank.wal"), wal);
	writeFile(folder + QStringLiteral("/textures/e1u1/blank.wal_json"), "{\"value\": 5}");
	auto archive = std::make_shared<PackageArchive>();
	if (!expect(archive->load(folder, &error), "the Quake II folder opens", error)) {
		return;
	}
	const MaterialLibrary library = scanMaterialLibrary(std::shared_ptr<const PackageArchiveReader>(archive));
	int count = 0;
	for (const MaterialLibraryEntry& entry : library.entries) {
		count += materialLookupKey(entry.name) == QStringLiteral("e1u1/blank") ? 1 : 0;
	}
	expect(count == 1, "a WAL with a blank header name and a .wal_json is listed once", QString::number(count));
}

// --- Doom 3 budget (C7) -----------------------------------------------------------

void checkDoom3Budget(const QDir& root)
{
	const QString folder = root.filePath(QStringLiteral("d3budget"));
	writeFile(folder + QStringLiteral("/textures/x/h.tga"), tga(qRgb(128, 128, 128), 512));
	QString text = QStringLiteral("textures/x/p\n{\n");
	for (int scale = 1; scale <= 4; ++scale) {
		text += QStringLiteral("\t{\n\t\tblend add\n\t\tmap heightmap(textures/x/h, %1)\n\t}\n").arg(scale);
	}
	text += QStringLiteral("}\n");
	writeFile(folder + QStringLiteral("/materials/x.mtr"), text.toUtf8());
	auto archive = std::make_shared<PackageArchive>();
	QString error;
	if (!expect(archive->load(folder, &error), "the Doom 3 folder opens", error)) {
		return;
	}
	const MaterialLibrary library = scanMaterialLibrary(std::shared_ptr<const PackageArchiveReader>(archive));
	const int entry = library.indexOf(QStringLiteral("textures/x/p"), MaterialEngine::Doom3);
	if (!expect(entry >= 0, "the Doom 3 material is listed")) {
		return;
	}
	const MaterialDefinition definition = library.definition(entry);
	MaterialImageCache cache;
	MaterialImageSource source;
	source.archive = archive;
	source.revision = QStringLiteral("d3budget");
	source.cache = &cache;
	source.byteBudget = 4LL * 512 * 512 * 5 + 64 * 1024;
	const MaterialImageSet first = resolveMaterialImages(definition, source);
	int programs = 0;
	for (auto it = first.textures.cbegin(); it != first.textures.cend(); ++it) {
		programs += it.value() && it.value()->status == QStringLiteral("program") && it.value()->usable() ? 1 : 0;
	}
	expect(first.complete && programs == 4 && first.warnings.isEmpty(), "four program results fit a budget of four images: the leaf is not charged",
		first.warnings.join(QStringLiteral("; ")) + QStringLiteral(" programs ") + QString::number(programs));
	const MaterialImageSet second = resolveMaterialImages(definition, source);
	expect(second.complete == first.complete && second.warnings == first.warnings, "a cached second resolve gives the same result");

	// Tables are checked across scripts: one script's table serves another's
	// material; a table nobody defines is reported.
	const QString tables = root.filePath(QStringLiteral("d3tables"));
	writeFile(tables + QStringLiteral("/materials/a_tables.mtr"), "table mytable { { 0, 1 } }\n");
	writeFile(tables + QStringLiteral("/materials/b.mtr"),
		"textures/x/uses\n{\n\t{\n\t\tblend add\n\t\tmap _white\n\t\trgb mytable[ time ]\n\t}\n}\n"
		"textures/x/lost\n{\n\t{\n\t\tblend add\n\t\tmap _white\n\t\trgb nosuchtable[ time ]\n\t}\n}\n");
	auto tableArchive = std::make_shared<PackageArchive>();
	if (expect(tableArchive->load(tables, &error), "the Doom 3 tables folder opens", error)) {
		const MaterialLibrary tableLibrary = scanMaterialLibrary(std::shared_ptr<const PackageArchiveReader>(tableArchive));
		const auto hasMissingTable = [&](const QString& name) {
			const int index = tableLibrary.indexOf(name, MaterialEngine::Doom3);
			if (index < 0) {
				return false;
			}
			for (const MaterialDiagnostic& diagnostic : tableLibrary.definition(index).diagnostics) {
				if (diagnostic.code == QStringLiteral("missing-table")) {
					return true;
				}
			}
			return false;
		};
		expect(tableLibrary.indexOf(QStringLiteral("textures/x/uses"), MaterialEngine::Doom3) >= 0 && !hasMissingTable(QStringLiteral("textures/x/uses"))
				&& hasMissingTable(QStringLiteral("textures/x/lost")),
			"the library checks table lookups against every script's tables");
	}
}

// --- Videos (C6, C15) ---------------------------------------------------------------

QByteArray roqChunk(quint16 id, quint16 argument, const QByteArray& data)
{
	QByteArray chunk;
	put16(&chunk, id);
	put32(&chunk, static_cast<quint32>(data.size()));
	put16(&chunk, argument);
	return chunk + data;
}

QByteArray roqHeader(int width, int height)
{
	QByteArray bytes;
	put16(&bytes, 0x1084);
	put32(&bytes, 0xFFFFFFFFu);
	put16(&bytes, 30);
	QByteArray info;
	put16(&info, static_cast<quint16>(width));
	put16(&info, static_cast<quint16>(height));
	put16(&info, 8);
	put16(&info, 4);
	return bytes + roqChunk(0x1001, 0, info);
}

void checkVideo()
{
	// Bright, dark, all kept, all kept: kept blocks alternate between the
	// two buffers, as id's decoder and FFmpeg's show them.
	QByteArray bytes = roqHeader(16, 16);
	QByteArray codebook;
	codebook += QByteArray("\xC8\xC8\xC8\xC8\x80\x80", 6);
	codebook += QByteArray("\x32\x32\x32\x32\x80\x80", 6);
	codebook += QByteArray("\x00\x00\x00\x00", 4);
	codebook += QByteArray("\x01\x01\x01\x01", 4);
	bytes += roqChunk(0x1002, (2 << 8) | 2, codebook);
	bytes += roqChunk(0x1011, 0, QByteArray("\x00\xAA\x00\x00\x00\x00", 6));
	bytes += roqChunk(0x1011, 0, QByteArray("\x00\xAA\x01\x01\x01\x01", 6));
	bytes += roqChunk(0x1011, 0, QByteArray("\x00\x00", 2));
	bytes += roqChunk(0x1011, 0, QByteArray("\x00\x00", 2));
	// Frame 5: every block moved from the previous frame with no offset
	// (motion byte 0x88 and mean 0 give dx = dy = 0).
	bytes += roqChunk(0x1011, 0, QByteArray("\x00\x55\x88\x88\x88\x88", 6));
	QString error;
	const std::shared_ptr<MaterialVideo> video = MaterialVideo::open(bytes, QStringLiteral("v.roq"), QStringLiteral("v.roq"), &error);
	if (!expect(video && video->frameCount() == 5, "the double-buffer test video opens", error)) {
		return;
	}
	const auto red = [&](int frame) {
		const MaterialTexturePtr texture = video->frame(frame);
		return texture ? qRed(texture->pixels.at(5 * 16 + 5)) : -1;
	};
	expect(red(0) == 200 && red(1) == 50 && red(2) == 200 && red(3) == 50, "kept blocks show the frame two back",
		QStringLiteral("%1 %2 %3 %4").arg(red(0)).arg(red(1)).arg(red(2)).arg(red(3)));
	expect(red(4) == 50, "motion copies from the previous frame", QString::number(red(4)));
	expect(red(1) == 50 && red(0) == 200, "seeking back decodes the same frames again");

	expect(!MaterialVideo::open(roqHeader(8192, 8192) + roqChunk(0x1011, 0, QByteArray(2, '\0')), QString(), QStringLiteral("big.roq"), &error),
		"a RoQ larger than 2048 pixels a side is refused");
	QByteArray many = roqHeader(16, 16);
	const QByteArray empty = roqChunk(0x1011, 0, QByteArray());
	many += empty.repeated(200000);
	QElapsedTimer timer;
	timer.start();
	const std::shared_ptr<MaterialVideo> capped = MaterialVideo::open(many, QString(), QStringLiteral("many.roq"), &error);
	expect(capped && capped->frameCount() == 100000 && timer.elapsed() < 10000, "the frame index is capped",
		capped ? QString::number(capped->frameCount()) : error);
	QByteArray truncated = roqHeader(16, 16) + roqChunk(0x1011, 0, QByteArray("\x00\xAA\x00\x00\x00\x00", 6));
	truncated += roqChunk(0x1002, 0, QByteArray(40, '\x10')).left(20);
	const std::shared_ptr<MaterialVideo> cut = MaterialVideo::open(truncated, QString(), QStringLiteral("cut.roq"), &error);
	expect(cut && cut->frameCount() == 1 && cut->frame(0) && cut->frame(0)->usable(), "a truncated last chunk ends the video");
}

// --- Map usage (C3, C4) ---------------------------------------------------------------

QByteArray doomSidedefs(const QByteArray& middle, int count)
{
	QByteArray sides;
	for (int index = 0; index < count; ++index) {
		QByteArray side(4, '\0');
		side += fixedName("-", 8) + fixedName("-", 8) + fixedName(middle, 8);
		put16(&side, 0);
		sides += side;
	}
	return sides;
}

QVector<std::pair<QByteArray, QByteArray>> doomMap(const QByteArray& marker, const QByteArray& texture)
{
	return {{marker, {}}, {"THINGS", QByteArray(10, '\0')}, {"LINEDEFS", QByteArray(14, '\0')}, {"SIDEDEFS", doomSidedefs(texture, 2)},
		{"VERTEXES", QByteArray(4, '\0')}, {"SECTORS", QByteArray(26, '\0')}};
}

void checkUsage(const QDir& root)
{
	// UDMF line numbers, and a large TEXTMAP in one pass.
	const QByteArray small = "namespace = \"zdoom\";\nsidedef\n{\ntexturemiddle = \"AAA\";\n}\n\nsidedef\n{\ntexturetop = \"BBB\";\n}\n";
	MaterialUsageIndex smallIndex;
	addMaterialUsageFromBytes(doomWad("PWAD", {{"MAP01", {}}, {"TEXTMAP", small}, {"ENDMAP", {}}}), QStringLiteral("maps/u.wad"), &smallIndex);
	const QVector<MaterialMapUse> aaa = smallIndex.usesOf(QStringLiteral("AAA"));
	const QVector<MaterialMapUse> bbb = smallIndex.usesOf(QStringLiteral("BBB"));
	expect(aaa.size() == 1 && aaa.first().lines == QVector<int> {4} && bbb.size() == 1 && bbb.first().lines == QVector<int> {9},
		"UDMF uses keep their line numbers");
	QByteArray big = "namespace = \"zdoom\";\n";
	const QByteArray sidedef = "sidedef\n{\ntexturemiddle = \"WALL\";\n}\n";
	big += sidedef.repeated(200000);
	MaterialUsageIndex bigIndex;
	QElapsedTimer timer;
	timer.start();
	addMaterialUsageFromBytes(doomWad("PWAD", {{"MAP01", {}}, {"TEXTMAP", big}, {"ENDMAP", {}}}), QStringLiteral("maps/big.wad"), &bigIndex);
	expect(bigIndex.useCount(QStringLiteral("WALL")) == 200000 && timer.elapsed() < 30000, "a TEXTMAP with 200k sidedefs is read in one pass",
		QString::number(timer.elapsed()));

	// A PWAD over its IWAD: the package's maps by default, the base's on
	// request, and a replaced map only once.
	QVector<std::pair<QByteArray, QByteArray>> iwadLumps = doomMap("E1M1", "IWADTEX");
	iwadLumps += doomMap("MAP02", "BASETEX");
	QVector<std::pair<QByteArray, QByteArray>> pwadLumps = doomMap("MAP01", "PWADTEX");
	pwadLumps += doomMap("E1M1", "NEWTEX");
	const QString iwadPath = root.filePath(QStringLiteral("usage/iwad.wad"));
	const QString pwadPath = root.filePath(QStringLiteral("usage/pwad.wad"));
	writeFile(iwadPath, doomWad("IWAD", iwadLumps));
	writeFile(pwadPath, doomWad("PWAD", pwadLumps));
	auto iwad = std::make_shared<PackageArchive>();
	auto pwad = std::make_shared<PackageArchive>();
	QString error;
	if (!expect(iwad->load(iwadPath, &error) && pwad->load(pwadPath, &error), "the usage WADs open", error)) {
		return;
	}
	const MaterialLayeredReader layered(pwad, {iwad});
	const MaterialUsageIndex own = scanMaterialUsage(layered);
	expect(own.useCount(QStringLiteral("PWADTEX")) == 2 && own.useCount(QStringLiteral("NEWTEX")) == 2 && own.useCount(QStringLiteral("IWADTEX")) == 0
			&& own.useCount(QStringLiteral("BASETEX")) == 0 && own.maps.size() == 2,
		"a PWAD over its IWAD reads only the PWAD's maps, in its own order", own.maps.join(QStringLiteral(", ")));
	const MaterialUsageIndex all = scanMaterialUsage(layered, {}, MaterialUsageScope::WithBase);
	const QVector<MaterialMapUse> baseUses = all.usesOf(QStringLiteral("BASETEX"));
	expect(all.useCount(QStringLiteral("PWADTEX")) == 2 && all.useCount(QStringLiteral("IWADTEX")) == 0 && baseUses.size() == 1 && baseUses.first().base
			&& baseUses.first().mapName == QStringLiteral("MAP02") && baseUses.first().mapPath == QStringLiteral("iwad.wad"),
		"with the base, the IWAD's maps are read on their own and marked base; the replaced E1M1 is left out", all.maps.join(QStringLiteral(", ")));

	// A folder mod over a folder base: the base's maps only on request.
	const QString base = root.filePath(QStringLiteral("usage/base"));
	const QString mod = root.filePath(QStringLiteral("usage/mod"));
	const QByteArray map = "{\n\"classname\" \"worldspawn\"\n{\n( 0 0 0 ) ( 1 0 0 ) ( 0 1 0 ) %1 0 0 0 0.5 0.5 0 0 0\n}\n}\n";
	writeFile(base + QStringLiteral("/maps/b.map"), QByteArray(map).replace("%1", "base/x"));
	writeFile(base + QStringLiteral("/maps/same.map"), QByteArray(map).replace("%1", "base/hidden"));
	writeFile(mod + QStringLiteral("/maps/m.map"), QByteArray(map).replace("%1", "mod/y"));
	writeFile(mod + QStringLiteral("/maps/same.map"), QByteArray(map).replace("%1", "mod/z"));
	auto baseFolder = std::make_shared<PackageArchive>();
	auto modFolder = std::make_shared<PackageArchive>();
	if (expect(baseFolder->load(base, &error) && modFolder->load(mod, &error), "the usage folders open", error)) {
		const MaterialLayeredReader folders(modFolder, {baseFolder});
		const MaterialUsageIndex modOnly = scanMaterialUsage(folders);
		expect(modOnly.useCount(QStringLiteral("mod/y")) == 1 && modOnly.useCount(QStringLiteral("base/x")) == 0 && modOnly.maps.size() == 2,
			"a mod's usage reads its own maps only", modOnly.maps.join(QStringLiteral(", ")));
		const MaterialUsageIndex withBase = scanMaterialUsage(folders, {}, MaterialUsageScope::WithBase);
		expect(withBase.useCount(QStringLiteral("base/x")) == 1 && withBase.useCount(QStringLiteral("base/hidden")) == 0
				&& withBase.useCount(QStringLiteral("mod/z")) == 1,
			"with the base, the base's maps join, except those the mod replaces");
	}
}

// --- Damaged classic data (C19) ---------------------------------------------------------

void checkMalformedClassic()
{
	QVector<int> indices;
	QSize size;
	QString error;
	expect(!decodeDoomPatchIndices(QByteArray(6, '\0'), &indices, &size, nullptr, &error) && !error.isEmpty(), "a patch shorter than its header is refused");
	QByteArray header;
	put16(&header, 4);
	put16(&header, 4);
	put16(&header, 0);
	put16(&header, 0);
	error.clear();
	expect(!decodeDoomPatchIndices(header, &indices, &size, nullptr, &error) && !error.isEmpty(), "a patch without its column table is refused");
	QByteArray outside = header;
	for (int column = 0; column < 4; ++column) {
		put32(&outside, 5000);
	}
	error.clear();
	expect(!decodeDoomPatchIndices(outside, &indices, &size, nullptr, &error) && !error.isEmpty(), "a column offset past the lump is refused");
	QByteArray cut = doomPatch(4, 4, 7);
	cut.chop(8);
	error.clear();
	expect(!decodeDoomPatchIndices(cut, &indices, &size, nullptr, &error) && !error.isEmpty(), "a post running past the lump is refused");
	// A DeePsea tall patch: a second post whose topdelta is not above the
	// first is relative to it.
	QByteArray tall;
	put16(&tall, 1);
	put16(&tall, 300);
	put16(&tall, 0);
	put16(&tall, 0);
	put32(&tall, 12);
	tall += QByteArray("\xFE\x01\x00\x05\x00", 5); // rows 254
	tall += QByteArray("\x10\x01\x00\x06\x00", 5); // 16 <= 254: row 270
	tall.append('\xFF');
	expect(decodeDoomPatchIndices(tall, &indices, &size) && indices.at(254) == 5 && indices.at(270) == 6, "tall patches use relative topdeltas");

	MemoryReader reader(PackageArchiveFormat::Wad);
	QByteArray negative;
	put32(&negative, 0xFFFFFFFFu);
	reader.add(QStringLiteral("TEXTURE1"), negative, QStringLiteral("wad-lump"));
	QByteArray badOffset;
	put32(&badOffset, 1);
	put32(&badOffset, 900);
	reader.add(QStringLiteral("TEXTURE2"), badOffset, QStringLiteral("wad-lump"));
	QByteArray pnames;
	put32(&pnames, 100);
	reader.add(QStringLiteral("PNAMES"), pnames, QStringLiteral("wad-lump"));
	const DoomMaterialCatalog catalog = readDoomMaterialCatalog(reader);
	expect(catalog.textures.isEmpty() && catalog.patchNames.isEmpty() && catalog.warnings.size() == 3,
		"negative counts, bad texture offsets and a short PNAMES are warnings, not garbage", catalog.warnings.join(QStringLiteral("; ")));
	QVector<WallDef> truncatedWalls {{"CUT", 16, 16, {0, 0, 0}}};
	QByteArray truncated = texture1Lump(truncatedWalls);
	truncated.chop(12);
	MemoryReader cutReader(PackageArchiveFormat::Wad);
	cutReader.add(QStringLiteral("TEXTURE1"), truncated, QStringLiteral("wad-lump"));
	const DoomMaterialCatalog cutCatalog = readDoomMaterialCatalog(cutReader);
	expect(cutCatalog.textures.isEmpty() && cutCatalog.warnings.size() == 1, "truncated patch records are a warning");

	QVector<DoomAnimationRange> ranges;
	QByteArray unterminated = boomAnimated({{"A1", "A3"}});
	unterminated.chop(1);
	error.clear();
	expect(parseBoomAnimatedLump(unterminated, &ranges, &error) && ranges.size() == 1 && !error.isEmpty(),
		"ANIMATED without its terminator keeps its records and says so");
	error.clear();
	expect(!parseBoomAnimatedLump(unterminated.left(10), &ranges, &error) && !error.isEmpty(), "ANIMATED ending inside a record is refused");
	QVector<DoomSwitchPair> switches;
	error.clear();
	expect(!parseBoomSwitchesLump(QByteArray(12, 'A'), &switches, &error) && switches.isEmpty() && !error.isEmpty(), "a truncated SWITCHES lump is refused");
}

// --- Face lookup (C18) ----------------------------------------------------------------

void checkLookup(const QDir& root)
{
	const QString folder = root.filePath(QStringLiteral("lookup"));
	writeFile(folder + QStringLiteral("/scripts/a.shader"), "textures/base/a\n{\n\t{\n\t\tmap textures/base/a.tga\n\t}\n}\n");
	writeFile(folder + QStringLiteral("/textures/base/a.tga"), tga(qRgb(10, 10, 200)));
	auto archive = std::make_shared<PackageArchive>();
	QString error;
	if (!expect(archive->load(folder, &error), "the lookup folder opens", error)) {
		return;
	}
	MaterialLookup lookup;
	lookup.setPackage(archive);
	lookup.reload();
	const MaterialLookupResult written = lookup.find(QStringLiteral("base/a.tga"), MaterialEngine::Quake3);
	const MaterialLookupResult full = lookup.find(QStringLiteral("textures/base/a.tga"), MaterialEngine::Quake3);
	expect(written.found && written.definition.name == QStringLiteral("textures/base/a") && full.found,
		"a face name written with an extension finds its shader, as R_FindShader strips it");
	const std::shared_ptr<const MaterialLibrary> library = lookup.library();
	expect(library && library->entriesUsingImage(QStringLiteral("textures/base/a")).size() == 1
			&& library->entriesUsingImage(QStringLiteral("TEXTURES/BASE/A.TGA")).size() == 1,
		"entriesUsingImage matches with or without the extension, ignoring case");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "a temporary folder is made")) {
		return 1;
	}
	const QDir root(temp.path());
	checkExtensionRules(root);
	checkImageCache();
	checkPaletteSearch();
	checkDoomScale();
	checkDoomNamespaces(root);
	checkWalSidecar(root);
	checkDoom3Budget(root);
	checkVideo();
	checkUsage(root);
	checkMalformedClassic();
	checkLookup(root);
	if (failures > 0) {
		std::cerr << failures << " material asset rules check(s) failed\n";
		return 1;
	}
	std::cout << "material asset rules passed\n";
	return 0;
}
