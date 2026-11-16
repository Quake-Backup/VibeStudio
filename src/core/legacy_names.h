#pragma once

#include <QString>

class QSettings;

namespace slopstudio {

// SlopStudio was called VibeStudio until 2026-11-15. These helpers let the
// studio keep reading what VibeStudio wrote: format and schema ids, the
// .vibepackage and .vibeworkspace extensions, the .vibestudio project folder,
// the per-user settings store, compiler and profile ids and VIBESTUDIO_*
// environment variables. New files always use the current names.

// The studio's former name, as VibeStudio wrote it ("VibeStudio").
QString legacyStudioName();

// `value` with every ASCII case-variant of the former name's "vibe" mapped
// letter by letter to "slop", so "vibestudio-workspace" becomes
// "slopstudio-workspace", ".vibepackage" becomes ".sloppackage" and
// "vibemap2-bsp" becomes "slopmap2-bsp". Use it on ids, never on paths.
QString withCurrentStudioName(const QString& value);

// What VibeStudio wrote in place of the current id `current`: the inverse of
// withCurrentStudioName(), so "slopstudio.modelSources" gives
// "vibestudio.modelSources". For looking up keys and file names.
QString legacyStudioId(const QString& current);

// True when `value` is `current`, or what VibeStudio wrote in its place.
bool isCurrentOrLegacyStudioName(const QString& value, const QString& current, Qt::CaseSensitivity sensitivity = Qt::CaseSensitive);

// True when `path` ends in `currentSuffix` (for example ".sloppackage") or in
// the suffix VibeStudio used for it (".vibepackage"), ignoring case.
bool hasCurrentOrLegacySuffix(const QString& path, const QString& currentSuffix);

// A portable package draft folder: *.sloppackage, or a VibeStudio *.vibepackage.
bool isPackageDraftPath(const QString& path);

// A workspace document: *.slopworkspace, or a VibeStudio *.vibeworkspace.
bool isWorkspaceDocumentPath(const QString& path);

// SLOPSTUDIO_<name> when it is set, else the VibeStudio-era VIBESTUDIO_<name>.
QString studioEnvironmentVariable(const char* name, const QString& defaultValue = {});
int studioEnvironmentVariableIntValue(const char* name);
bool studioEnvironmentVariableIsSet(const char* name);

// The project folder VibeStudio kept its manifest and caches in.
QString legacyProjectDirectoryName();

// When a project root holds only VibeStudio's .vibestudio folder, renames it
// to .slopstudio once so every existing manifest, build and backup carries
// over. Leaves both alone when .slopstudio already exists. Returns false only
// when a move was needed and failed; `error` then says why.
bool migrateLegacyProjectDirectory(const QString& projectRootPath, QString* error = nullptr);

// Copies every key of `legacy` into `current` when `current` is empty and
// `legacy` is not. Returns the number of keys copied.
int copyLegacySettings(const QSettings& legacy, QSettings& current);

// Copies VibeStudio's native per-user settings store into SlopStudio's once,
// on the first run after the rename. Call after the application's
// organisation and name are set, and only for the native store (never for a
// --settings-file override). Returns the number of keys copied.
int migrateLegacySettingsStore();

// `currentPath` with its last path component named after the current
// application ("SlopStudio") renamed to VibeStudio's; empty when it has none.
QString legacyDataDirectory(const QString& currentPath);

// Renames VibeStudio's per-user data, local data, configuration and cache
// folders to SlopStudio's when only the old ones exist, so recovery copies,
// logs and caches carry over. Same calling rules as
// migrateLegacySettingsStore(). Returns the number of folders moved.
int migrateLegacyDataDirectories();

} // namespace slopstudio
