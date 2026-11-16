#pragma once

// What the game draws on a level face: the material a face's texture name
// finds, looked up the way the map's game looks it up, from one material
// library per package (with its base packages beneath it). The Levels page
// and other surfaces ask here instead of parsing scripts themselves, so a
// face shows what the Materials page shows.

#include "core/material_library.h"
#include "core/material_render.h"

#include <QImage>
#include <QMutex>
#include <QString>

#include <memory>

namespace slopstudio {

// The image editors show for a material: qer_editorimage, else a Doom 3
// diffuse map, else the first stage image, else (classic textures and
// implicit materials) the name itself.
QString materialDisplayImage(const MaterialDefinition& definition);
// The same, empty when the definition names no image of its own (where
// materialDisplayImage falls back to the name). The Levels preview takes a
// shader's still image from here.
QString materialDeclaredDisplayImage(const MaterialDefinition& definition);

struct MaterialLookupResult {
	// True when a script, decl or classic texture defines the name; false
	// leaves the engine's implicit material in `definition`.
	bool found = false;
	// The library entry, -1 for an implicit material.
	int entry = -1;
	MaterialDefinition definition;
	QString displayImage;
	bool animated = false;
};

class MaterialLookup {
public:
	explicit MaterialLookup(MaterialLibraryOptions options = {});
	MaterialLookup(const MaterialLookup&) = delete;
	MaterialLookup& operator=(const MaterialLookup&) = delete;

	// The package to read (null clears it). Nothing is read until reload().
	void setPackage(std::shared_ptr<const PackageArchiveReader> reader);
	void setOptions(const MaterialLibraryOptions& options);
	// Reads the package again: after staged edits, a script save or a change
	// of base packages. Safe to call from a worker; finds made meanwhile see
	// the previous library.
	void reload(const MaterialLibraryProgress& progress = {});
	// Drops what was read, so isLoaded() is false until the next reload().
	void invalidate();
	[[nodiscard]] bool isLoaded() const;
	[[nodiscard]] std::shared_ptr<const MaterialLibrary> library() const;

	// A face's texture name as the map writes it: Quake III maps leave out
	// textures/, Doom names are lump names, Doom 3 names are whole. `engine`
	// narrows the search (Unknown takes any); `kind` ("doom-wall" for a
	// sidedef, "doom-flat" for a sector plane) picks between a Doom wall and
	// flat of the same name. Quake III and Doom 3 drop a written extension,
	// as R_FindShader and the decl manager do.
	[[nodiscard]] MaterialLookupResult find(const QString& faceTexture, MaterialEngine engine = MaterialEngine::Unknown,
		const QString& kind = QString()) const;
	// The material drawn face-on at `seconds` (renderMaterialSwatch), for a
	// texture that animates in a level view. Null without a 3D renderer.
	[[nodiscard]] QImage swatch(const MaterialLookupResult& result, int side, double seconds) const;

private:
	mutable QMutex m_mutex;
	MaterialLibraryOptions m_options;
	std::shared_ptr<const PackageArchiveReader> m_reader;
	std::shared_ptr<const MaterialLibrary> m_library;
	std::shared_ptr<MaterialImageCache> m_cache;
	// Counts library changes; part of the image cache key, so a swatch still
	// drawing from a replaced library cannot leave its images for the next.
	quint64 m_generation = 0;
};

} // namespace slopstudio
