#pragma once
// "Override in Package": copying a base game material into the package above
// it, so the game uses the package's copy. Shared by the Materials page's
// Override in Package action and `material override`.
//
// A Quake III shader or Doom 3 material is copied as text into an override
// script the game reads in the right order: Quake III (as ioquake3, RTCW, ET
// and Jedi Knight run it) lets the alphabetically last script win, so the copy
// goes into scripts/zz_slopstudio_overrides.shader; Quake III 1.32 and Doom 3
// keep the first definition, so it goes into scripts/00_slopstudio_overrides
// .shader or materials/00_slopstudio_overrides.mtr. A Quake II texture is a
// WAL file: the package gets its own copy at the same path.
#include "core/material_library.h"
#include "core/material_model.h"

#include <QByteArray>
#include <QString>

namespace slopstudio {

struct MaterialOverride {
	QString name;
	MaterialEngine engine = MaterialEngine::Unknown;
	// Package-relative path the copy goes to: the override script, or the
	// Quake II WAL's own path.
	QString virtualPath;
	// Shaders and Doom 3 materials: the definition's text, name through
	// closing brace.
	QString definitionText;
	// Quake II: the WAL's bytes, copied whole.
	QByteArray fileBytes;

	[[nodiscard]] bool copiesFile() const { return engine == MaterialEngine::Quake2; }
};

// The override script for an engine and dialect (see above); empty for
// engines without material scripts.
QString materialOverrideScriptPath(MaterialEngine engine, MaterialDialect dialect);

// What overriding `entry` writes. Fails with a reason when the entry is not a
// base material (MaterialLibraryEntry::base), or is one without text or a
// file to copy (Doom textures, Quake WAD textures, implicit shaders).
bool materialOverrideForEntry(const MaterialLibrary& library, int entry, MaterialDialect dialect, MaterialOverride* out, QString* error);

// The override script's text with the definition added: `existing` (empty
// for a new script) with the definition appended. Fails when the script
// already defines the name or the result does not parse.
bool materialOverrideScriptText(const QString& existing, const MaterialOverride& copy, MaterialDialect dialect, QString* text, QString* error);

} // namespace slopstudio
