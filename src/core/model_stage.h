#pragma once

// A model delivered into a package as the set of files the game loads with it,
// staged as one package step: Quake III detail levels beside an MD3, the clip
// md5anims beside an md5mesh, or a single MDL, MD2, IQM or ASE. The set is
// prepared from an editable model, each file is checked (a safe package path,
// shorter than 64 bytes for the Quake-family formats, and the format's own
// header), and the materials it uses are reviewed against the package view
// (core/level_dependencies.h). Staging applies every entry to a copy of the
// package plan and adopts it only when all of them stage, so one conflict
// review and one undo step cover the whole set. The Mesh Editor's Stage in
// Package and `model stage` share it.

#include "core/level_dependencies.h"
#include "core/model_document.h"
#include "core/package_staging.h"

#include <QStringList>
#include <QVector>

namespace vibestudio
{

struct ModelStageRequest
{
	// The model's package path; its suffix names the format (mdl, md2, md3,
	// md5mesh, md5anim, iqm, ase).
	QString virtualPath;
	ModelExportOptions options;
	// MD3 detail levels: -1 the source's stored recipe, 0 none, 1-3 generated
	// with `lodRatio`.
	int lodLevels = -1;
	double lodRatio = 0.5;
	// md5mesh: the clips written beside it as md5anim. Empty with `allClips`
	// false writes none.
	bool allClips = true;
	QVector<int> clips;
	// Where a stored recipe's hand-made levels are read from.
	QString baseDirectory;
	// Refuse the set when a material it uses is missing from the package.
	bool requireMaterials = false;
};

struct ModelStageEntry
{
	QString virtualPath;
	QByteArray bytes;
	// model, detail-level, animation.
	QString role;
};

struct ModelStageSet
{
	QVector<ModelStageEntry> entries;
	// What each export left out or approximated, and what was not staged.
	QStringList notes;
	// The materials and skins the model uses, resolved against the package view.
	LevelDependencyReport dependencies;
	[[nodiscard]] qint64 totalBytes() const;
};

// The package suffixes stageModelSet accepts.
[[nodiscard]] QStringList modelStageSuffixes();
// A safe package path for the format, and bytes that start like it.
bool checkModelStageEntry(const QString &virtualPath, const QByteArray &bytes, QString *error = nullptr);
// Prepares every file of the set and reviews its materials against `view`
// (null skips the review). Refuses, preparing nothing, a path or export that
// fails, and missing materials when the request requires them.
bool prepareModelStageSet(const ModelMesh &mesh, const ModelStageRequest &request, const PackageArchiveReader *view, ModelStageSet *set,
						  QString *error = nullptr, const ModelWorkControl &control = {});
// Stages every entry as one package operation group; on any failure the plan
// is unchanged.
bool stageModelSet(const ModelStageSet &set, PackageStagingModel *staging, bool replaceExisting, QString *error = nullptr);
// JSON for CLI output: entries (path, role, bytes, sha256), notes and the
// dependency review.
[[nodiscard]] QJsonObject modelStageSetJson(const ModelStageSet &set);

} // namespace vibestudio
