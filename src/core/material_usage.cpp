#include "core/material_usage.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QtEndian>

#include <algorithm>

namespace vibestudio {
namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioMaterials)
};

constexpr qint64 kMapReadLimit = 64LL * 1024 * 1024;
constexpr int kLineSample = 8;

QString suffixOf(const QString& path)
{
	const QString name = packageVirtualPathFileName(path);
	const int dot = name.lastIndexOf(QLatin1Char('.'));
	return dot < 0 ? QString() : name.mid(dot + 1).toLower();
}

// The uses of one map, gathered by name before they join the index.
class MapUses {
public:
	MapUses(QString mapPath, QString mapName, MaterialMapKind kind)
		: m_path(std::move(mapPath)), m_name(std::move(mapName)), m_kind(kind)
	{
	}

	void add(const QString& texture, int count = 1, int line = 0)
	{
		const QString name = texture.trimmed();
		if (name.isEmpty() || name == QStringLiteral("-") || count <= 0) {
			return;
		}
		const QString key = materialLookupKey(name);
		auto found = m_uses.find(key);
		if (found == m_uses.end()) {
			MaterialMapUse use;
			use.mapPath = m_path;
			use.mapName = m_name;
			use.kind = m_kind;
			use.textureName = name;
			found = m_uses.insert(key, use);
			m_order << key;
		}
		found->count += count;
		if (line > 0 && found->lines.size() < kLineSample) {
			found->lines << line;
		}
	}

	void commit(MaterialUsageIndex* index) const
	{
		index->maps << (m_name.isEmpty() ? m_path : (m_path.isEmpty() ? m_name : m_path + QLatin1Char(':') + m_name));
		for (const QString& key : m_order) {
			index->uses[key].push_back(m_uses.value(key));
		}
	}

private:
	QString m_path;
	QString m_name;
	MaterialMapKind m_kind;
	QHash<QString, MaterialMapUse> m_uses;
	QStringList m_order;
};

// --- .map text ---------------------------------------------------------------

struct MapToken {
	QString text;
	int line = 0;
	bool quoted = false;
};

QVector<MapToken> tokenizeMap(const QString& text)
{
	QVector<MapToken> tokens;
	int line = 1;
	const qsizetype size = text.size();
	qsizetype at = 0;
	while (at < size) {
		const QChar c = text.at(at);
		if (c == QLatin1Char('\n')) {
			++line;
			++at;
			continue;
		}
		if (c.isSpace()) {
			++at;
			continue;
		}
		if (c == QLatin1Char('/') && at + 1 < size && text.at(at + 1) == QLatin1Char('/')) {
			while (at < size && text.at(at) != QLatin1Char('\n')) {
				++at;
			}
			continue;
		}
		if (c == QLatin1Char('"')) {
			const qsizetype end = text.indexOf(QLatin1Char('"'), at + 1);
			const qsizetype stop = end < 0 ? size : end;
			MapToken token {text.mid(at + 1, stop - at - 1), line, true};
			line += static_cast<int>(token.text.count(QLatin1Char('\n')));
			tokens.push_back(token);
			at = stop + 1;
			continue;
		}
		if (c == QLatin1Char('(') || c == QLatin1Char(')') || c == QLatin1Char('{') || c == QLatin1Char('}') || c == QLatin1Char('[')
			|| c == QLatin1Char(']')) {
			tokens.push_back({QString(c), line, false});
			++at;
			continue;
		}
		const qsizetype start = at;
		while (at < size) {
			const QChar d = text.at(at);
			if (d.isSpace() || d == QLatin1Char('(') || d == QLatin1Char(')') || d == QLatin1Char('{') || d == QLatin1Char('}')
				|| d == QLatin1Char('"') || d == QLatin1Char('[') || d == QLatin1Char(']')) {
				break;
			}
			++at;
		}
		tokens.push_back({text.mid(start, at - start), line, false});
	}
	return tokens;
}

bool isPunctuation(const MapToken& token, char c)
{
	return !token.quoted && token.text.size() == 1 && token.text.at(0) == QLatin1Char(c);
}

// A face names its texture right after the last of its parenthesised groups
// (plane points, a brushDef texture matrix); a patch names it first thing
// inside its braces. Entity keys sit one level up and are never read.
void scanMapText(const QString& text, MapUses* uses)
{
	const QVector<MapToken> tokens = tokenizeMap(text);
	int depth = 0;
	int paren = 0;
	bool patchPending = false;
	bool patchName = false;
	for (qsizetype index = 0; index < tokens.size(); ++index) {
		const MapToken& token = tokens.at(index);
		if (isPunctuation(token, '{')) {
			++depth;
			if (patchPending) {
				patchName = true;
				patchPending = false;
			}
			continue;
		}
		if (isPunctuation(token, '}')) {
			depth = std::max(0, depth - 1);
			paren = 0;
			continue;
		}
		if (patchName) {
			patchName = false;
			uses->add(token.text, 1, token.line);
			continue;
		}
		if (depth < 2) {
			continue;
		}
		if (!token.quoted && token.text.startsWith(QStringLiteral("patchDef"), Qt::CaseInsensitive)) {
			patchPending = true;
			continue;
		}
		if (isPunctuation(token, '(')) {
			++paren;
			continue;
		}
		if (isPunctuation(token, ')')) {
			paren = std::max(0, paren - 1);
			if (paren == 0 && index + 1 < tokens.size()) {
				const MapToken& next = tokens.at(index + 1);
				const bool punctuation = isPunctuation(next, '(') || isPunctuation(next, '{') || isPunctuation(next, '}') || isPunctuation(next, '[');
				bool number = false;
				next.text.toDouble(&number);
				if (!punctuation && !(number && !next.quoted)) {
					uses->add(next.text, 1, next.line);
					++index;
				}
			}
		}
	}
}

// --- Compiled maps -------------------------------------------------------------

struct Lump {
	qint64 offset = 0;
	qint64 length = 0;
};

bool readLumps(const QByteArray& bytes, int first, int count, QVector<Lump>* lumps)
{
	if (bytes.size() < first + count * 8) {
		return false;
	}
	for (int index = 0; index < count; ++index) {
		Lump lump;
		lump.offset = qFromLittleEndian<qint32>(bytes.constData() + first + index * 8);
		lump.length = qFromLittleEndian<qint32>(bytes.constData() + first + index * 8 + 4);
		if (lump.offset < 0 || lump.length < 0 || lump.offset + lump.length > bytes.size()) {
			return false;
		}
		lumps->push_back(lump);
	}
	return true;
}

QString fixedString(const QByteArray& bytes, qint64 offset, int length)
{
	if (offset < 0 || offset + length > bytes.size()) {
		return {};
	}
	const QByteArray raw = bytes.mid(offset, length);
	const qsizetype end = raw.indexOf('\0');
	return QString::fromLatin1(end < 0 ? raw : raw.left(end)).trimmed();
}

// Quake and Half-Life (bspfile.h): faces point at texinfo, texinfo at a
// miptex whose name the textures lump holds. BSP2 widens the face fields.
bool scanQuakeBsp(const QByteArray& bytes, MapUses* uses, QString* error)
{
	const qint32 version = qFromLittleEndian<qint32>(bytes.constData());
	const bool bsp2 = bytes.startsWith("BSP2") || bytes.startsWith("2PSB");
	if (version != 29 && version != 30 && !bsp2) {
		*error = Text::tr("not a Quake, Half-Life or BSP2 map");
		return false;
	}
	QVector<Lump> lumps;
	if (!readLumps(bytes, 4, 15, &lumps)) {
		*error = Text::tr("its lump directory is damaged");
		return false;
	}
	const Lump& textures = lumps.at(2);
	const Lump& texinfo = lumps.at(6);
	const Lump& faces = lumps.at(7);
	QStringList names;
	if (textures.length >= 4) {
		const qint32 count = qFromLittleEndian<qint32>(bytes.constData() + textures.offset);
		for (qint32 index = 0; index >= 0 && index < count && 4 + 4LL * (index + 1) <= textures.length; ++index) {
			const qint32 offset = qFromLittleEndian<qint32>(bytes.constData() + textures.offset + 4 + 4 * index);
			names << (offset < 0 ? QString() : fixedString(bytes, textures.offset + offset, 16));
		}
	}
	QVector<int> texinfoMiptex;
	for (qint64 at = 0; at + 40 <= texinfo.length; at += 40) {
		texinfoMiptex << qFromLittleEndian<qint32>(bytes.constData() + texinfo.offset + at + 32);
	}
	QHash<int, int> counts;
	const int faceSize = bsp2 ? 28 : 20;
	const int texinfoAt = bsp2 ? 16 : 10;
	for (qint64 at = 0; at + faceSize <= faces.length; at += faceSize) {
		const char* face = bytes.constData() + faces.offset + at;
		const int info = bsp2 ? qFromLittleEndian<qint32>(face + texinfoAt) : qFromLittleEndian<qint16>(face + texinfoAt);
		if (info >= 0 && info < texinfoMiptex.size()) {
			++counts[texinfoMiptex.at(info)];
		}
	}
	for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
		uses->add(names.value(it.key()), it.value());
	}
	return true;
}

// Quake II (qfiles.h, IBSP 38): faces point at texinfo, which names the WAL.
bool scanQuake2Bsp(const QByteArray& bytes, MapUses* uses, QString* error)
{
	QVector<Lump> lumps;
	if (!readLumps(bytes, 8, 19, &lumps)) {
		*error = Text::tr("its lump directory is damaged");
		return false;
	}
	const Lump& texinfo = lumps.at(5);
	const Lump& faces = lumps.at(6);
	QStringList names;
	for (qint64 at = 0; at + 76 <= texinfo.length; at += 76) {
		names << fixedString(bytes, texinfo.offset + at + 40, 32);
	}
	QHash<int, int> counts;
	for (qint64 at = 0; at + 20 <= faces.length; at += 20) {
		const int info = qFromLittleEndian<qint16>(bytes.constData() + faces.offset + at + 10);
		if (info >= 0 && info < names.size()) {
			++counts[info];
		}
	}
	QHash<QString, int> byName;
	for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
		byName[names.at(it.key())] += it.value();
	}
	for (auto it = byName.cbegin(); it != byName.cend(); ++it) {
		uses->add(it.key(), it.value());
	}
	return true;
}

// Quake III (IBSP 46), Wolfenstein and Enemy Territory (IBSP 47), Jedi
// Knight (RBSP 1): surfaces point at the shaders lump.
bool scanQuake3Bsp(const QByteArray& bytes, bool raven, MapUses* uses, QString* error)
{
	QVector<Lump> lumps;
	if (!readLumps(bytes, 8, raven ? 18 : 17, &lumps)) {
		*error = Text::tr("its lump directory is damaged");
		return false;
	}
	const Lump& shaders = lumps.at(1);
	const Lump& surfaces = lumps.at(13);
	QStringList names;
	for (qint64 at = 0; at + 72 <= shaders.length; at += 72) {
		names << fixedString(bytes, shaders.offset + at, 64);
	}
	const int surfaceSize = raven ? 148 : 104;
	QHash<int, int> counts;
	for (qint64 at = 0; at + surfaceSize <= surfaces.length; at += surfaceSize) {
		const int shader = qFromLittleEndian<qint32>(bytes.constData() + surfaces.offset + at);
		if (shader >= 0 && shader < names.size()) {
			++counts[shader];
		}
	}
	for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
		uses->add(names.at(it.key()), it.value());
	}
	return true;
}

bool scanBsp(const QByteArray& bytes, MapUses* uses, QString* error)
{
	if (bytes.size() < 8) {
		*error = Text::tr("the file is too short");
		return false;
	}
	const qint32 version = qFromLittleEndian<qint32>(bytes.constData() + 4);
	if (bytes.startsWith("IBSP") && version == 38) {
		return scanQuake2Bsp(bytes, uses, error);
	}
	if (bytes.startsWith("IBSP") && (version == 46 || version == 47)) {
		return scanQuake3Bsp(bytes, false, uses, error);
	}
	if (bytes.startsWith("RBSP") && version == 1) {
		return scanQuake3Bsp(bytes, true, uses, error);
	}
	return scanQuakeBsp(bytes, uses, error);
}

// --- Doom ----------------------------------------------------------------------

struct DoomLump {
	QString name;
	std::function<bool(QByteArray*)> read;
};

bool isMapDataLump(const QString& name)
{
	static const QSet<QString> names {QStringLiteral("THINGS"), QStringLiteral("LINEDEFS"), QStringLiteral("SIDEDEFS"),
		QStringLiteral("VERTEXES"), QStringLiteral("SEGS"), QStringLiteral("SSECTORS"), QStringLiteral("NODES"), QStringLiteral("SECTORS"),
		QStringLiteral("REJECT"), QStringLiteral("BLOCKMAP"), QStringLiteral("BEHAVIOR"), QStringLiteral("SCRIPTS"), QStringLiteral("TEXTMAP"),
		QStringLiteral("ZNODES"), QStringLiteral("DIALOGUE"), QStringLiteral("ENDMAP")};
	return names.contains(name);
}

// Map lumps follow a marker lump (E1M1, MAP01 or any name): SIDEDEFS name
// three textures per side (vanilla and Hexen alike, 30 bytes), SECTORS two
// flats (26 bytes); UDMF writes them as fields in TEXTMAP.
void scanDoomLumps(const QVector<DoomLump>& lumps, const QString& wadPath, MaterialUsageIndex* index, const MaterialUsageProgress& progress)
{
	static const QRegularExpression udmf(QStringLiteral("\\b(texturetop|texturebottom|texturemiddle|texturefloor|textureceiling)\\s*=\\s*\"([^\"]*)\""),
		QRegularExpression::CaseInsensitiveOption);
	for (qsizetype at = 0; at < lumps.size(); ++at) {
		const QString& marker = lumps.at(at).name;
		if (at + 1 >= lumps.size() || isMapDataLump(marker)
			|| (lumps.at(at + 1).name != QStringLiteral("THINGS") && lumps.at(at + 1).name != QStringLiteral("TEXTMAP"))) {
			continue;
		}
		if (progress && !progress(static_cast<int>(at), static_cast<int>(lumps.size()), marker)) {
			index->cancelled = true;
			index->complete = false;
			return;
		}
		MapUses uses(wadPath, marker, MaterialMapKind::DoomMap);
		qsizetype data = at + 1;
		for (; data < lumps.size() && isMapDataLump(lumps.at(data).name); ++data) {
			const QString& name = lumps.at(data).name;
			if (name != QStringLiteral("SIDEDEFS") && name != QStringLiteral("SECTORS") && name != QStringLiteral("TEXTMAP")) {
				continue;
			}
			QByteArray bytes;
			if (!lumps.at(data).read(&bytes)) {
				index->warnings << Text::tr("%1 of %2 could not be read.").arg(name, marker);
				index->complete = false;
				continue;
			}
			if (name == QStringLiteral("SIDEDEFS")) {
				for (qsizetype side = 0; side + 30 <= bytes.size(); side += 30) {
					uses.add(fixedString(bytes, side + 4, 8).toUpper());
					uses.add(fixedString(bytes, side + 12, 8).toUpper());
					uses.add(fixedString(bytes, side + 20, 8).toUpper());
				}
			} else if (name == QStringLiteral("SECTORS")) {
				for (qsizetype sector = 0; sector + 26 <= bytes.size(); sector += 26) {
					uses.add(fixedString(bytes, sector + 4, 8).toUpper());
					uses.add(fixedString(bytes, sector + 12, 8).toUpper());
				}
			} else {
				const QString text = QString::fromUtf8(bytes);
				for (auto match = udmf.globalMatch(text); match.hasNext();) {
					const QRegularExpressionMatch found = match.next();
					uses.add(found.captured(2), 1, static_cast<int>(text.left(found.capturedStart()).count(QLatin1Char('\n'))) + 1);
				}
			}
		}
		uses.commit(index);
		at = data - 1;
	}
}

// A Doom WAD held as bytes (maps/*.wad in a ZDoom PK3).
void scanWadBytes(const QByteArray& bytes, const QString& wadPath, MaterialUsageIndex* index)
{
	if (bytes.size() < 12 || (!bytes.startsWith("IWAD") && !bytes.startsWith("PWAD"))) {
		return;
	}
	const qint32 count = qFromLittleEndian<qint32>(bytes.constData() + 4);
	const qint32 directory = qFromLittleEndian<qint32>(bytes.constData() + 8);
	if (count < 0 || directory < 12 || directory + 16LL * count > bytes.size()) {
		index->warnings << Text::tr("%1 has a damaged lump directory.").arg(wadPath);
		index->complete = false;
		return;
	}
	QVector<DoomLump> lumps;
	for (qint32 lump = 0; lump < count; ++lump) {
		const char* entry = bytes.constData() + directory + 16 * lump;
		const qint32 offset = qFromLittleEndian<qint32>(entry);
		const qint32 size = qFromLittleEndian<qint32>(entry + 4);
		lumps.push_back({fixedString(bytes, directory + 16LL * lump + 8, 8).toUpper(), [bytes, offset, size](QByteArray* out) {
							 if (offset < 0 || size < 0 || offset + static_cast<qint64>(size) > bytes.size()) {
								 return false;
							 }
							 *out = bytes.mid(offset, size);
							 return true;
						 }});
	}
	scanDoomLumps(lumps, wadPath, index, {});
}

} // namespace

QString materialMapKindId(MaterialMapKind kind)
{
	switch (kind) {
	case MaterialMapKind::MapSource:
		return QStringLiteral("map");
	case MaterialMapKind::Bsp:
		return QStringLiteral("bsp");
	case MaterialMapKind::DoomMap:
		return QStringLiteral("doom");
	}
	return QStringLiteral("map");
}

QVector<MaterialMapUse> MaterialUsageIndex::usesOf(const QString& materialName) const
{
	const QString key = materialLookupKey(materialName);
	QStringList keys {key};
	const QString prefix = QStringLiteral("textures/");
	if (key.startsWith(prefix)) {
		keys << key.mid(prefix.size());
	} else {
		keys << prefix + key;
	}
	QVector<MaterialMapUse> result;
	for (const QString& each : std::as_const(keys)) {
		result += uses.value(each);
	}
	std::stable_sort(result.begin(), result.end(), [](const MaterialMapUse& a, const MaterialMapUse& b) {
		const int path = a.mapPath.compare(b.mapPath, Qt::CaseInsensitive);
		return path != 0 ? path < 0 : a.mapName.compare(b.mapName, Qt::CaseInsensitive) < 0;
	});
	return result;
}

int MaterialUsageIndex::useCount(const QString& materialName) const
{
	int total = 0;
	for (const MaterialMapUse& use : usesOf(materialName)) {
		total += use.count;
	}
	return total;
}

void addMaterialUsageFromBytes(const QByteArray& bytes, const QString& mapPath, MaterialUsageIndex* index)
{
	const QString suffix = suffixOf(mapPath);
	if (suffix == QStringLiteral("wad")) {
		scanWadBytes(bytes, mapPath, index);
		return;
	}
	if (suffix == QStringLiteral("bsp")) {
		MapUses uses(mapPath, QString(), MaterialMapKind::Bsp);
		QString error;
		if (!scanBsp(bytes, &uses, &error)) {
			index->warnings << Text::tr("%1 was not read: %2.").arg(mapPath, error);
			index->complete = false;
			return;
		}
		uses.commit(index);
		return;
	}
	MapUses uses(mapPath, QString(), MaterialMapKind::MapSource);
	scanMapText(QString::fromUtf8(bytes), &uses);
	uses.commit(index);
}

MaterialUsageIndex scanMaterialUsage(const PackageArchiveReader& reader, const MaterialUsageProgress& progress)
{
	MaterialUsageIndex index;
	QVector<PackageEntry> entries = reader.entries();
	if (reader.format() == PackageArchiveFormat::Wad) {
		// The open WAD's own maps, in directory order.
		QVector<qsizetype> order(entries.size());
		for (qsizetype at = 0; at < entries.size(); ++at) {
			order[at] = at;
		}
		std::stable_sort(order.begin(), order.end(), [&](qsizetype a, qsizetype b) { return entries.at(a).sourceOrdinal < entries.at(b).sourceOrdinal; });
		QVector<DoomLump> lumps;
		for (qsizetype at : std::as_const(order)) {
			const PackageEntry& entry = entries.at(at);
			if (entry.kind != PackageEntryKind::File) {
				continue;
			}
			lumps.push_back({packageVirtualPathFileName(entry.virtualPath).section(QLatin1Char('.'), 0, 0).toUpper(), [&reader, at](QByteArray* out) {
								 QString error;
								 return reader.readEntryAt(at, out, &error, kMapReadLimit);
							 }});
		}
		scanDoomLumps(lumps, QString(), &index, progress);
		return index;
	}
	QVector<qsizetype> maps;
	for (qsizetype at = 0; at < entries.size(); ++at) {
		const PackageEntry& entry = entries.at(at);
		const QString suffix = suffixOf(entry.virtualPath);
		if (entry.kind == PackageEntryKind::File && (suffix == QStringLiteral("map") || suffix == QStringLiteral("bsp") || suffix == QStringLiteral("wad"))) {
			maps << at;
		}
	}
	std::sort(maps.begin(), maps.end(), [&](qsizetype a, qsizetype b) { return entries.at(a).virtualPath.compare(entries.at(b).virtualPath, Qt::CaseInsensitive) < 0; });
	const QString phase = Text::tr("Reading maps");
	for (qsizetype position = 0; position < maps.size(); ++position) {
		if (progress && !progress(static_cast<int>(position), static_cast<int>(maps.size()), phase)) {
			index.cancelled = true;
			index.complete = false;
			return index;
		}
		const PackageEntry& entry = entries.at(maps.at(position));
		if (entry.sizeBytes > static_cast<quint64>(kMapReadLimit)) {
			index.warnings << Text::tr("%1 is skipped: it is larger than 64 MiB.").arg(entry.virtualPath);
			index.complete = false;
			continue;
		}
		QByteArray bytes;
		QString error;
		if (!reader.readEntryAt(maps.at(position), &bytes, &error, kMapReadLimit)) {
			index.warnings << Text::tr("%1 could not be read: %2").arg(entry.virtualPath, error);
			index.complete = false;
			continue;
		}
		addMaterialUsageFromBytes(bytes, entry.virtualPath, &index);
	}
	if (progress) {
		progress(static_cast<int>(maps.size()), static_cast<int>(maps.size()), phase);
	}
	return index;
}

QJsonObject materialMapUseJson(const MaterialMapUse& use)
{
	QJsonObject object;
	object.insert(QStringLiteral("map"), use.mapPath);
	if (!use.mapName.isEmpty()) {
		object.insert(QStringLiteral("mapName"), use.mapName);
	}
	object.insert(QStringLiteral("kind"), materialMapKindId(use.kind));
	object.insert(QStringLiteral("texture"), use.textureName);
	object.insert(QStringLiteral("count"), use.count);
	if (!use.lines.isEmpty()) {
		QJsonArray lines;
		for (int line : use.lines) {
			lines.append(line);
		}
		object.insert(QStringLiteral("lines"), lines);
	}
	return object;
}

QJsonObject materialUsageJson(const MaterialUsageIndex& index, const QString& materialName)
{
	QJsonObject object;
	object.insert(QStringLiteral("maps"), QJsonArray::fromStringList(index.maps));
	object.insert(QStringLiteral("complete"), index.complete);
	if (!index.warnings.isEmpty()) {
		object.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(index.warnings));
	}
	if (!materialName.isEmpty()) {
		object.insert(QStringLiteral("material"), materialName);
		QJsonArray uses;
		int total = 0;
		for (const MaterialMapUse& use : index.usesOf(materialName)) {
			uses.append(materialMapUseJson(use));
			total += use.count;
		}
		object.insert(QStringLiteral("uses"), uses);
		object.insert(QStringLiteral("total"), total);
		return object;
	}
	QJsonObject materials;
	for (auto it = index.uses.cbegin(); it != index.uses.cend(); ++it) {
		int total = 0;
		for (const MaterialMapUse& use : it.value()) {
			total += use.count;
		}
		materials.insert(it.value().isEmpty() ? it.key() : it.value().first().textureName, total);
	}
	object.insert(QStringLiteral("materials"), materials);
	return object;
}

} // namespace vibestudio
