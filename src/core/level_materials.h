#pragma once

#include "core/map_preview_mesh.h"
#include "core/material_model.h"
#include "core/package_archive.h"

#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QSize>
#include <functional>

namespace vibestudio
{

// Images for editing, not a simulation of an engine's shader stages or lighting.
// Resolution uses the same package paths, shader catalogue and image/model
// decoders as dependency review and asset editing; a Quake III shader's image
// is the one the Materials page shows for it (core/material_lookup.h). Run on
// a worker with an immutable reader.
struct LevelPreviewInput {
	qsizetype entryIndex = -1; // Identity inside this immutable package snapshot.
	qint64 sourceOrdinal = -1;
	QString path, namespaceId, layer, role;
	quint64 sizeBytes = 0;
};

struct LevelPreviewMaterial {
	QString key; // map token, doomPreviewMaterialKey(), or levelModelSurfaceMaterialKey()
	QString name;
	QString imagePath;
	QString shaderPath;
	QString sourceLayer;
	// image, composite, editor-image, stage-image, embedded, builtin, missing, ambiguous,
	// unreadable, unsupported, budget, cancelled
	QString status;
	QString note;
	QStringList warnings;
	QSize sourceSize; // original texel dimensions, before preview downsampling
	QImage image;
	QVector<LevelPreviewInput> inputs; // Exact Doom flat/patch/definition/palette occurrences.
	[[nodiscard]] bool ready() const { return !image.isNull(); }
};

// The Quake III game Levels reads shaders for when a caller does not say:
// the Materials page's choice in the studio, Automatic elsewhere (the CLI).
void setLevelPreviewDefaultDialect(MaterialDialect dialect);
[[nodiscard]] MaterialDialect levelPreviewDefaultDialect();

struct LevelPreviewAssetOptions {
	QString paletteId;
	int materialLimit = 256;
	int modelLimit = 32;
	qint64 entryByteLimit = 16 * 1024 * 1024;
	qint64 totalReadByteLimit = 64 * 1024 * 1024;
	qint64 imageByteLimit = 64 * 1024 * 1024;
	int previewDimension = 512;
	// The Quake III game shaders are read for, as the Materials page reads
	// them; Automatic works it out from the package's scripts together.
	MaterialDialect materialDialect = levelPreviewDefaultDialect();
};

struct LevelPreviewAssets {
	QString sourcePath;
	QVector<LevelPreviewMaterial> materials;
	QHash<QString, ModelMesh> models; // selected static pose, keyed by levelModelAppearance().cacheKey
	QJsonArray modelAppearances; // Effective materials, omitted surfaces and exact skin inputs.
	QStringList warnings;
	int requestedMaterials = 0;
	int requestedModels = 0;
	int unavailableModels = 0;
	qint64 readBytes = 0;
	qint64 imageBytes = 0;
	bool complete = true;
	bool cancelled = false;
	[[nodiscard]] int readyCount() const;
	[[nodiscard]] int problemCount() const;
};

using LevelPreviewAssetProgress = std::function<bool(int completed, int total)>;
// `archive` may be a package over its game's base packages
// (MaterialLayeredReader): base images, models and palettes then resolve
// beneath the package's, and a shader the package declares hides the game's.
LevelPreviewAssets resolveLevelPreviewAssets(const LevelMapDocument &document, const PackageArchiveReader &archive,
											 const LevelPreviewAssetOptions &options = {}, LevelPreviewAssetProgress progress = {});
// Authored meshes use the same bounded image/shader resolver without loading
// model geometry from the archive. Keys identify surface array positions.
QString modelPreviewSurfaceMaterialKey(int surface);
LevelPreviewAssets resolveModelPreviewAssets(const ModelMesh &mesh, const PackageArchiveReader &archive,
											const LevelPreviewAssetOptions &options = {}, LevelPreviewAssetProgress progress = {});
QHash<QString, QSize> levelPreviewTextureSizes(const LevelPreviewAssets &assets);
QHash<int, QImage> levelPreviewSurfaceImages(const ModelMesh &mesh, const LevelPreviewAssets &assets);
QJsonObject levelPreviewAssetsJson(const LevelPreviewAssets &assets);
QString levelPreviewAssetsText(const LevelPreviewAssets &assets);

} // namespace vibestudio
