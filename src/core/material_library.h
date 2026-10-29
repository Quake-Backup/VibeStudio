#pragma once

// Every material a package or folder holds, across engines: Quake III shader
// scripts and the images they leave to the default shader, Doom 3 material
// and table decls, Doom wall textures and flats with their animations and
// switches, Quake WAD2/WAD3 textures, and Quake II WAL textures with their
// ericw-tools .wal_json sidecars.
//
// Which definition the game uses when a name is defined twice follows each
// engine: ioquake3 concatenates scripts in reverse listing order, so the
// alphabetically last script wins (and the first definition inside one
// file); Doom 3's decl manager keeps the first definition it parses, reading
// files in sorted order. The others are kept and marked as shadowed.

#include "core/material_classic.h"
#include "core/material_eval.h"
#include "core/material_model.h"
#include "core/material_script.h"
#include "core/package_archive.h"
#include "core/studio_query.h"

#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace vibestudio {

struct MaterialLibraryEntry {
	QString name;
	MaterialEngine engine = MaterialEngine::Unknown;
	// The definition's kind ("shader", "material", "implicit", "doom-wall",
	// "doom-flat", "quake-miptex", "quake2-wal", "guide").
	QString kind;
	QString sourcePath;
	QString sourceLayer;
	int line = 0;
	// Script entries: indexes into MaterialLibrary::scripts and the
	// script's materials. -1 for entries that carry their own definition.
	int scriptIndex = -1;
	int materialIndex = -1;
	// A cheap image for list thumbnails before the full render.
	QString thumbnailImage;
	bool animated = false;
	bool sky = false;
	bool light = false;
	bool fog = false;
	bool translucent = false;
	bool rejected = false;
	int stageCount = 0;
	int errors = 0;
	int warnings = 0;
	QStringList images;
	// Another definition of the same name wins in game.
	bool shadowed = false;
	QString shadowedBy;
	// Read from a base package beneath the open one (a game installation's
	// pak0.pk3, the IWAD): the game has it, the package does not.
	bool base = false;
};

struct MaterialLibraryOptions {
	// Images under textures/ that no script defines become implicit
	// Quake III or Doom 3 materials.
	bool includeImplicit = true;
	bool includeClassic = true;
	int maximumScripts = 4096;
	int maximumEntries = 200000;
	qint64 maximumScriptBytes = 32LL * 1024 * 1024;
	qint64 maximumTotalBytes = 512LL * 1024 * 1024;
	// Engines to scan; empty scans all.
	QVector<MaterialEngine> engines;
	// The Quake III game the scripts are read for; Automatic works it out
	// from every script together.
	MaterialDialect dialect = MaterialDialect::Automatic;
	// Packages the game mounts beneath the open one, lowest first: a game
	// installation's base packages or a PWAD's IWAD. Their materials join
	// the library marked as base; the package's own shadow them, and Doom
	// textures, flats and patches merge as the game merges WADs.
	QVector<std::shared_ptr<const PackageArchiveReader>> baseReaders;
	// Base materials are listed (otherwise their images, tables and Doom
	// lumps are still used, but only the package's materials are listed).
	bool listBaseMaterials = true;
};

// A package over the packages beneath it, read as the game reads them:
// entries() lists the lowest layer first, so Doom's last-lump-wins search
// finds the package's lump, and a path in a higher layer hides the same path
// below it. Base entries carry layerId "base:<n>" (n counts from the lowest).
class MaterialLayeredReader final : public PackageArchiveReader {
public:
	MaterialLayeredReader(std::shared_ptr<const PackageArchiveReader> package, QVector<std::shared_ptr<const PackageArchiveReader>> base);

	[[nodiscard]] PackageArchiveFormat format() const override;
	[[nodiscard]] QString sourcePath() const override;
	[[nodiscard]] QString wadMagic() const override;
	[[nodiscard]] bool isOpen() const override;
	[[nodiscard]] QVector<PackageEntry> entries() const override { return m_entries; }
	bool readEntryBytes(const QString& virtualPath, QByteArray* out, QString* error, qint64 maxBytes = -1) const override;
	bool readEntryAt(qsizetype index, QByteArray* out, QString* error, qint64 maxBytes = -1) const override;

	[[nodiscard]] const std::shared_ptr<const PackageArchiveReader>& package() const { return m_package; }
	[[nodiscard]] int baseCount() const { return static_cast<int>(m_base.size()); }
	// True when the entry at `index` comes from a base layer.
	[[nodiscard]] bool isBaseEntry(qsizetype index) const;

private:
	struct Source {
		int layer = 0; // 0..baseCount()-1 base, baseCount() the package
		qsizetype index = 0;
	};
	[[nodiscard]] const PackageArchiveReader* layer(int index) const;

	std::shared_ptr<const PackageArchiveReader> m_package;
	QVector<std::shared_ptr<const PackageArchiveReader>> m_base;
	QVector<PackageEntry> m_entries;
	QVector<Source> m_sources;
};

using MaterialLibraryProgress = std::function<bool(int done, int total, const QString& phase)>;

class MaterialLibrary {
public:
	QString sourcePath;
	QVector<MaterialScript> scripts;
	QVector<MaterialLibraryEntry> entries;
	MaterialTableSet tables;
	std::shared_ptr<const DoomMaterialCatalog> doomCatalog;
	QStringList warnings;
	bool complete = true;
	bool cancelled = false;
	// The Quake III game the scripts were read for (never Automatic).
	MaterialDialect dialect = MaterialDialect::Quake3;
	// What images are read from: the package, or the package over its base
	// packages (MaterialLayeredReader). Null for a library of loose scripts.
	std::shared_ptr<const PackageArchiveReader> reader;
	// How many base packages are beneath the package.
	int baseCount = 0;

	// The entry the game would use for a name, preferring an engine.
	[[nodiscard]] int indexOf(const QString& name, MaterialEngine engine = MaterialEngine::Unknown) const;
	[[nodiscard]] MaterialDefinition definition(int entry) const;
	[[nodiscard]] const MaterialScript* script(int entry) const;
	[[nodiscard]] QVector<MaterialEngine> engines() const;
	[[nodiscard]] int count(MaterialEngine engine) const;
	// Entries whose images include `image` (case-insensitive, extension
	// optional).
	[[nodiscard]] QVector<int> entriesUsingImage(const QString& image) const;
	// Replaces one script (after an edit) and rebuilds its entries.
	void replaceScript(int scriptIndex, const MaterialScript& script);
	// Adds a loose script; returns its index.
	int addScript(const MaterialScript& script);
	// Adds a script read from outside the scan as the game would see it at
	// `virtualPath` (see materialScriptVirtualPath): a script already at that
	// path is replaced, so definitions keep the engine's lookup order.
	// Returns the script's index.
	int adoptScript(MaterialScript script, const QString& virtualPath);

	void rebuildEntries();

	// Entries carrying their own definition: classic textures (scriptIndex
	// -1) and implicit shaders for images no script names (scriptIndex -2).
	QVector<MaterialDefinition> ownDefinitions;
	QVector<MaterialDefinition> implicitDefinitions;
	// Candidate images for implicit materials, and their engine.
	QStringList implicitImages;
	MaterialEngine implicitEngine = MaterialEngine::Unknown;
	// Lower-case paths (and Doom lump names) the game reads from a base
	// package, and whether entries from there are listed.
	QSet<QString> basePaths;
	bool listBase = true;

private:
	void resolveShadowing();
};

MaterialLibrary scanMaterialLibrary(const PackageArchiveReader& reader, const MaterialLibraryOptions& options = {},
	const MaterialLibraryProgress& progress = {});
// The same, keeping the reader (over options.baseReaders when there are
// any) as the library's image source.
MaterialLibrary scanMaterialLibrary(std::shared_ptr<const PackageArchiveReader> reader, const MaterialLibraryOptions& options = {},
	const MaterialLibraryProgress& progress = {});

// Where the game reads a script file: its path inside `packageRoot` when it
// lies in that folder, otherwise scripts/<file> (materials/<file> for Doom 3),
// the folder the engine scans.
QString materialScriptVirtualPath(const QString& filePath, const QString& packageRoot, MaterialEngine engine);

// The engine a package most likely belongs to, from its contents.
MaterialEngine detectMaterialEngine(const PackageArchiveReader& reader);

// What the filter reads: name, engine, kind, source, stages, images,
// animated, sky, light, fog, translucent, rejected, errors, warnings,
// shadowed, and each surfaceparm and flag as its own key.
StudioQueryProperties materialLibraryEntryQueryProperties(const MaterialLibraryEntry& entry, const MaterialDefinition& definition);

QJsonObject materialLibraryEntryJson(const MaterialLibraryEntry& entry);
QJsonObject materialLibraryJson(const MaterialLibrary& library, bool includeEntries = true);
QStringList materialLibrarySummaryLines(const MaterialLibrary& library);

} // namespace vibestudio
