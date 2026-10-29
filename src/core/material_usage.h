#pragma once

// Which maps in a package use which materials: the reverse of the Levels
// texture list, for the Materials page's Used In Maps tab and `material
// usage`. Names are read the way each format stores them:
// - .map text of every idTech 2-4 editor: Quake, Quake II and Half-Life
//   brush faces (Valve 220 too), Quake III brushDef and patchDef2/3, Doom 3
//   brushDef3 (Quake III maps leave out the textures/ folder);
// - compiled maps: Quake and Half-Life BSP (miptex through texinfo and
//   faces, BSP2 too), Quake II IBSP 38 (texinfo), Quake III-family IBSP
//   46/47 and RBSP (shaders through surfaces);
// - Doom WAD maps: SIDEDEFS upper, lower and middle textures and SECTORS
//   floors and ceilings, and UDMF TEXTMAP.
// Layouts follow the GPL engine sources credited in docs/CREDITS.md
// ("Materials, Shaders And Textures").

#include "core/material_model.h"
#include "core/package_archive.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace vibestudio {

enum class MaterialMapKind {
	MapSource, // .map text
	Bsp,       // a compiled map
	DoomMap,   // a map inside a Doom WAD
};

QString materialMapKindId(MaterialMapKind kind);

// One map's use of one material.
struct MaterialMapUse {
	// The map's package path; for a Doom WAD map the WAD's own path (empty
	// for the open WAD itself) and the map's marker in mapName.
	QString mapPath;
	QString mapName;
	MaterialMapKind kind = MaterialMapKind::MapSource;
	// The name as the map writes it.
	QString textureName;
	// Faces, patches, sidedef slots or sector planes using it.
	int count = 0;
	// Source lines of the first few uses (.map text only).
	QVector<int> lines;
};

struct MaterialUsageIndex {
	// Material lookup key (materialLookupKey) to its uses, map by map.
	QHash<QString, QVector<MaterialMapUse>> uses;
	// Every map read: "maps/dm1.bsp", "E1M1".
	QStringList maps;
	QStringList warnings;
	bool complete = true;
	bool cancelled = false;

	// The uses of a material by its name, matching how maps name it: with or
	// without Quake III's textures/ folder, ignoring case.
	[[nodiscard]] QVector<MaterialMapUse> usesOf(const QString& materialName) const;
	// Total uses across maps.
	[[nodiscard]] int useCount(const QString& materialName) const;
};

using MaterialUsageProgress = std::function<bool(int done, int total, const QString& phase)>;

// Reads every map in the package. Maps larger than 64 MiB are skipped with
// a warning.
MaterialUsageIndex scanMaterialUsage(const PackageArchiveReader& reader, const MaterialUsageProgress& progress = {});

// The same for one map's bytes, by its path's extension (.map, .bsp) or as a
// WAD; `mapPath` names it in the result.
void addMaterialUsageFromBytes(const QByteArray& bytes, const QString& mapPath, MaterialUsageIndex* index);

QJsonObject materialMapUseJson(const MaterialMapUse& use);
QJsonObject materialUsageJson(const MaterialUsageIndex& index, const QString& materialName = QString());

} // namespace vibestudio
