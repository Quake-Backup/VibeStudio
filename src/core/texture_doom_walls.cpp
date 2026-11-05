#include "core/texture_doom_walls.h"

#include "core/package_staging.h"

#include <QCoreApplication>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <cstring>

namespace vibestudio {
namespace {
bool fail(QString* error, const QString& message) { if (error) { *error = message; } return false; }
qint32 read32(const QByteArray& bytes, qsizetype at) { return qFromLittleEndian<qint32>(bytes.constData() + at); }
qint16 read16(const QByteArray& bytes, qsizetype at) { return qFromLittleEndian<qint16>(bytes.constData() + at); }
void put32(QByteArray& bytes, qsizetype at, qint32 value) { qToLittleEndian(value, bytes.data() + at); }
void put16(QByteArray& bytes, qsizetype at, qint16 value) { qToLittleEndian(value, bytes.data() + at); }
QString lumpName(const QByteArray& bytes, qsizetype at)
{
	QByteArray name = bytes.mid(at, 8);
	const auto end = name.indexOf('\0'); if (end >= 0) { name.truncate(end); }
	return QString::fromLatin1(name).toUpper();
}
void putLumpName(QByteArray& bytes, qsizetype at, const QString& name)
{
	const auto encoded = name.toUpper().toLatin1().left(8);
	std::memcpy(bytes.data() + at, encoded.constData(), size_t(encoded.size()));
}
bool validLumpName(const QString& name)
{
	if (name.isEmpty() || name.size() > 8) { return false; }
	return std::all_of(name.cbegin(), name.cend(), [](QChar c) {
		const ushort u = c.toUpper().unicode();
		return (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || QStringLiteral("[]-_\\^").contains(QChar(u));
	});
}
// Record sizes: Doom name[8] masked width height columndirectory patchcount,
// patches originx originy patch stepdir colormap; Strife drops the two
// obsolete fields of each.
constexpr int kDoomHeader = 22, kDoomPatch = 10, kStrifeHeader = 18, kStrifePatch = 6;

bool parseLayout(const QByteArray& bytes, const QStringList& patchNames, bool strife, QVector<DoomWallTexture>* textures)
{
	if (bytes.size() < 4) { return false; }
	const qint32 count = read32(bytes, 0);
	if (count < 0 || count > 65536 || 4 + qint64(count) * 4 > bytes.size()) { return false; }
	const int header = strife ? kStrifeHeader : kDoomHeader, patchSize = strife ? kStrifePatch : kDoomPatch;
	QVector<DoomWallTexture> parsed;
	// Records written by id's and later tools tile the lump after the offset
	// table. Requiring that rejects the other layout, whose record sizes
	// cannot tile the same bytes.
	QVector<QPair<qint64, qint64>> spans;
	for (qint32 index = 0; index < count; ++index) {
		const qint32 at = read32(bytes, 4 + index * 4);
		if (at < 4 + count * 4 || qint64(at) + header > bytes.size()) { return false; }
		DoomWallTexture texture; texture.name = lumpName(bytes, at); texture.masked = read32(bytes, at + 8) != 0;
		texture.size = QSize(read16(bytes, at + 12), read16(bytes, at + 14));
		const int patches = read16(bytes, at + (strife ? 16 : 20));
		if (texture.size.width() <= 0 || texture.size.height() <= 0 || patches < 0 || qint64(at) + header + qint64(patches) * patchSize > bytes.size()) { return false; }
		spans.push_back({at, qint64(at) + header + qint64(patches) * patchSize});
		for (int patch = 0; patch < patches; ++patch) {
			const qsizetype p = at + header + qsizetype(patch) * patchSize;
			const int number = read16(bytes, p + 4);
			if (number < 0 || number >= patchNames.size()) { return false; }
			texture.patches.push_back({read16(bytes, p), read16(bytes, p + 2), patchNames[number]});
		}
		parsed.push_back(texture);
	}
	std::sort(spans.begin(), spans.end());
	qint64 next = 4 + qint64(count) * 4;
	for (const auto& span : spans) {
		if (span.first != next) { return false; }
		next = span.second;
	}
	if (next != bytes.size()) { return false; }
	*textures = std::move(parsed);
	return true;
}
}

bool parseDoomPatchNames(const QByteArray& bytes, QStringList* names, QString* error)
{
	if (bytes.size() < 4) { return fail(error, QCoreApplication::translate("VibeStudioDoomWalls", "PNAMES is truncated.")); }
	const qint32 count = read32(bytes, 0);
	if (count < 0 || count > 65536 || 4 + qint64(count) * 8 > bytes.size()) { return fail(error, QCoreApplication::translate("VibeStudioDoomWalls", "PNAMES has an implausible patch count.")); }
	QStringList parsed;
	for (qint32 index = 0; index < count; ++index) { parsed << lumpName(bytes, 4 + index * 8); }
	*names = parsed;
	return true;
}

QByteArray encodeDoomPatchNames(const QStringList& names)
{
	QByteArray bytes(4 + names.size() * 8, '\0'); put32(bytes, 0, qint32(names.size()));
	for (qsizetype index = 0; index < names.size(); ++index) { putLumpName(bytes, 4 + index * 8, names[index]); }
	return bytes;
}

bool parseDoomTextureLump(const QByteArray& bytes, const QStringList& patchNames, QVector<DoomWallTexture>* textures, bool* strife, QString* error)
{
	// A Doom record read as Strife (or the reverse) runs into the next record's
	// bytes, so only the right layout parses every texture within the lump.
	QVector<DoomWallTexture> doom, strifeTextures;
	const bool doomFits = parseLayout(bytes, patchNames, false, &doom);
	const bool strifeFits = !doomFits && parseLayout(bytes, patchNames, true, &strifeTextures);
	if (!doomFits && !strifeFits) { return fail(error, QCoreApplication::translate("VibeStudioDoomWalls", "The texture definition lump is malformed or names patches missing from PNAMES.")); }
	*textures = doomFits ? doom : strifeTextures;
	if (strife) { *strife = strifeFits; }
	return true;
}

QByteArray encodeDoomTextureLump(const QVector<DoomWallTexture>& textures, const QStringList& patchNames, bool strife)
{
	const int header = strife ? kStrifeHeader : kDoomHeader, patchSize = strife ? kStrifePatch : kDoomPatch;
	QByteArray bytes(4 + textures.size() * 4, '\0'); put32(bytes, 0, qint32(textures.size()));
	for (qsizetype index = 0; index < textures.size(); ++index) {
		const auto& texture = textures[index];
		put32(bytes, 4 + index * 4, qint32(bytes.size()));
		QByteArray record(header + texture.patches.size() * patchSize, '\0');
		putLumpName(record, 0, texture.name); put32(record, 8, texture.masked ? 1 : 0);
		put16(record, 12, qint16(texture.size.width())); put16(record, 14, qint16(texture.size.height()));
		put16(record, strife ? 16 : 20, qint16(texture.patches.size()));
		for (qsizetype patch = 0; patch < texture.patches.size(); ++patch) {
			const qsizetype p = header + patch * patchSize; const auto& placement = texture.patches[patch];
			put16(record, p, qint16(placement.originX)); put16(record, p + 2, qint16(placement.originY));
			put16(record, p + 4, qint16(patchNames.indexOf(placement.patch.toUpper())));
			if (!strife) { put16(record, p + 6, 1); } // stepdir; colormap stays 0
		}
		bytes += record;
	}
	return bytes;
}

bool stageDoomWallTexture(PackageStagingModel* staging, const QString& patchName, const QString& textureName, QSize size,
	bool replaceExisting, DoomWallStageReport* report, QString* error)
{
	DoomWallStageReport local;
	auto* out = report ? report : &local; *out = {};
	if (!staging || !staging->isLoaded() || (staging->sourceWadMagic() != QStringLiteral("IWAD") && staging->sourceWadMagic() != QStringLiteral("PWAD"))) {
		return fail(error, QCoreApplication::translate("VibeStudioDoomWalls", "Open a Doom IWAD or PWAD before defining a wall texture."));
	}
	if (!validLumpName(patchName) || !validLumpName(textureName)) { return fail(error, QCoreApplication::translate("VibeStudioDoomWalls", "Patch and wall texture names need 1–8 lump-name characters.")); }
	if (size.width() < 1 || size.height() < 1 || size.width() > 32767 || size.height() > 32767) { return fail(error, QCoreApplication::translate("VibeStudioDoomWalls", "Wall texture dimensions are out of range.")); }
	const QString patch = patchName.toUpper(), texture = textureName.toUpper();
	const PackageStagingArchive planned(*staging);
	QByteArray pnamesBytes, textureBytes; bool hasPnames = false, hasTexture = false;
	for (const auto& entry : planned.entries()) {
		const QString name = entry.virtualPath.toUpper();
		if (name == QStringLiteral("PNAMES") && !hasPnames) { hasPnames = planned.readEntryBytes(entry.virtualPath, &pnamesBytes, error, 16 * 1024 * 1024); if (!hasPnames) { return false; } }
		if (name == QStringLiteral("TEXTURE1") && !hasTexture) { hasTexture = planned.readEntryBytes(entry.virtualPath, &textureBytes, error, 16 * 1024 * 1024); if (!hasTexture) { return false; } }
	}
	QStringList names; QVector<DoomWallTexture> textures; bool strife = false;
	if (hasPnames && !parseDoomPatchNames(pnamesBytes, &names, error)) { return false; }
	if (hasTexture && !parseDoomTextureLump(textureBytes, names, &textures, &strife, error)) { return false; }
	if (!names.contains(patch)) {
		if (names.size() >= 32767) { return fail(error, QCoreApplication::translate("VibeStudioDoomWalls", "PNAMES has no room for another patch.")); }
		names << patch; out->addedPatchName = true;
	}
	DoomWallTexture definition; definition.name = texture; definition.size = size; definition.patches = {{0, 0, patch}};
	const auto existing = std::find_if(textures.begin(), textures.end(), [&](const DoomWallTexture& t) { return t.name == texture; });
	if (existing != textures.end()) {
		if (!replaceExisting) { return fail(error, QCoreApplication::translate("VibeStudioDoomWalls", "TEXTURE1 already defines %1. Enable replacement to redefine it.").arg(texture)); }
		*existing = definition; out->replacedDefinition = true;
	} else { textures.push_back(definition); }
	out->createdPatchNames = !hasPnames; out->createdTextureLump = !hasTexture;
	if (!hasTexture) {
		out->warnings << QCoreApplication::translate("VibeStudioDoomWalls", "This WAD had no TEXTURE1. Source ports such as ZDoom merge it with the game's textures; original Doom replaces the whole list, so copy the game's definitions in for vanilla play.");
	}
	auto next = *staging;
	if (!next.beginOperationGroup(QCoreApplication::translate("VibeStudioDoomWalls", "Define wall texture %1").arg(texture), error)) { return false; }
	const auto put = [&](const QByteArray& bytes, const QString& name, bool exists) {
		return exists ? next.addWadBytes(bytes, name, QStringLiteral("global"), 0, true, error) : next.addBytes(bytes, name, error);
	};
	if (!put(encodeDoomPatchNames(names), QStringLiteral("PNAMES"), hasPnames) || !put(encodeDoomTextureLump(textures, names, strife), QStringLiteral("TEXTURE1"), hasTexture)) { return false; }
	if (!next.endOperationGroup(true, error) || !next.summary().canSave) { return fail(error, QCoreApplication::translate("VibeStudioDoomWalls", "Defining the wall texture would create package conflicts.")); }
	*staging = std::move(next);
	return true;
}

} // namespace vibestudio
