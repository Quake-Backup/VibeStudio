#pragma once

#include <QByteArray>
#include <QSize>
#include <QStringList>
#include <QVector>

namespace vibestudio {

class PackageStagingModel;

// Doom wall textures are compositions of patches: PNAMES lists patch lump
// names and TEXTURE1/TEXTURE2 define each texture's size and patch placements.
// Layouts from Chocolate Doom 3.1.0 src/doom/r_data.c (GPL-2.0-or-later) and
// the Doom Wiki "TEXTURE1 and TEXTURE2" page, including Strife's shorter
// records. Reimplemented; no code copied. See docs/CREDITS.md.

struct DoomWallPatch {
	int originX = 0, originY = 0;
	QString patch; // PNAMES entry, upper case
};

struct DoomWallTexture {
	QString name; // at most 8 characters, upper case
	QSize size;
	bool masked = false;
	QVector<DoomWallPatch> patches;
};

bool parseDoomPatchNames(const QByteArray& bytes, QStringList* names, QString* error = nullptr);
QByteArray encodeDoomPatchNames(const QStringList& names);
// `strife` reports (or, for encoding, selects) Strife's record layout.
bool parseDoomTextureLump(const QByteArray& bytes, const QStringList& patchNames, QVector<DoomWallTexture>* textures, bool* strife, QString* error = nullptr);
QByteArray encodeDoomTextureLump(const QVector<DoomWallTexture>& textures, const QStringList& patchNames, bool strife);

struct DoomWallStageReport {
	bool createdPatchNames = false, createdTextureLump = false, replacedDefinition = false, addedPatchName = false;
	QStringList warnings;
};

// Define `textureName` as `patchName` at 0, 0 in a staged IWAD/PWAD: adds the
// patch to PNAMES when missing and the texture to TEXTURE1 (replacing an
// existing definition only with `replaceExisting`), creating either lump when
// the WAD has none. One package undo step; nothing changes on failure.
bool stageDoomWallTexture(PackageStagingModel* staging, const QString& patchName, const QString& textureName, QSize size,
	bool replaceExisting, DoomWallStageReport* report = nullptr, QString* error = nullptr);

} // namespace vibestudio
