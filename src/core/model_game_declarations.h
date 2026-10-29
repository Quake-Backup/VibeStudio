#pragma once

// Game-side declarations generated from an editable model, so game code and
// assets stay in step with the source's frames and clips:
// - Doom 3 / Quake 4 `model` declarations (def/*.def): the mesh, one `anim`
//   per skeletal clip and each clip's events as `frame N command` lines, and
//   the whole model set (md5mesh, every clip as an md5anim, the .def);
// - QuakeC `$frame` macros (with `$cd`, `$base`, `$skin`), grouped by clip;
// - a Quake II frame header (`#define FRAME_name index`, as id's m_*.h files);
// - a GZDoom MODELDEF block mapping sprite frames to model frames;
// - GZDoom VOXELDEF entries replacing sprites with a KVX voxel model.
//
// Formats follow the released id Software sources (Doom 3 idDeclModelDef and
// idAnim::AddFrameCommand in neo/game/anim/Anim.cpp; the QuakeC frame macros of
// the Quake progs sources; the Quake II game's m_*.h headers), the ZDoom
// wiki's MODELDEF and VOXELDEF references and GZDoom's VOXELDEF parser
// (R_InitVoxels, src/r_data/voxeldef.cpp); see docs/CREDITS.md. Text output
// only: writing files goes through the guarded model writers, and staging a
// model set goes through the package staging service.

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <array>

namespace vibestudio
{
class PackageStagingModel;
struct ModelDoom3DefOptions
{
	// The declaration's name, such as monster_imp.
	QString name;
	// Game-relative paths with '/': the md5mesh, and the folder each clip's
	// <clip>.md5anim goes in (empty: the mesh's folder).
	QString meshPath;
	QString animDirectory;
	QString inherit, skin;
	bool hasOffset = false;
	std::array<double, 3> offset{};
	// Named in the generated comment, such as imp.mesh.json.
	QString sourceName;
};
struct ModelQuakeCFrameOptions
{
	// `$cd` folder (empty leaves it out), `$base` and `$skin` names.
	QString modelDirectory;
	QString base = QStringLiteral("base");
	QString skin = QStringLiteral("skin");
	QString sourceName;
};
struct ModelQuake2FrameOptions
{
	// Names the model in the header comment.
	QString modelDirectory;
	QString prefix = QStringLiteral("FRAME_");
	double scale = 1;
	QString sourceName;
};
struct ModelGzdoomModeldefOptions
{
	// The actor class the block applies to.
	QString actor;
	// `Path` (folder of the model in the package), `Model 0` and `Skin 0` file names.
	QString path;
	QString model;
	QString skin;
	std::array<double, 3> scale{1, 1, 1};
	std::array<double, 3> offset{};
	double angleOffset = 0;
	// Three characters; each clip takes sprite names of these plus one more
	// character, 29 frames (A-Z, [, \, ]) per name.
	QString sprite;
	QString sourceName;
};
struct ModelGzdoomVoxeldefOptions
{
	// The sprites the voxel replaces: four characters (every frame of the
	// sprite) or five (one frame, A-Z, [, \ or ]), one entry each.
	QStringList sprites;
	// The KVX file's path in the package, such as voxels/troo.kvx.
	QString path;
	double scale = 1;
	// Degrees added to the voxel's facing (GZDoom adds its own 90).
	double angleOffset = 0;
	// GZDoom's spin speeds for placed and dropped items, whole numbers; zero
	// does not spin, a negative value spins the other way.
	int placedSpin = 0, droppedSpin = 0;
	// Draw with the voxel's own palette remapped to the game's.
	bool overridePalette = false;
	QString sourceName;
};
struct ModelGameFile
{
	// Game-relative path with '/'.
	QString path;
	QByteArray bytes;
	// "mesh", "anim" or "declaration".
	QString role;
};

// "doom3", "quakec", "quake2", "gzdoom" (MODELDEF) and "gzdoom-voxeldef".
[[nodiscard]] QStringList modelGameDeclarationTargets();
// A game-relative path idTech engines accept: '/'-separated ASCII letters,
// digits, underscores, dots and hyphens, no "..", not absolute.
[[nodiscard]] bool validModelGamePath(const QString &path);
// Empty with `error` when the model or options cannot be declared.
[[nodiscard]] QString modelDoom3ModelDefText(const ModelMesh &mesh, const ModelDoom3DefOptions &options, QString *error = nullptr);
// The md5mesh, every skeletal clip as an md5anim, and the .def (def/<name>.def),
// in that order. Refuses a model whose clips cannot all be written.
bool modelDoom3ModelSet(const ModelMesh &mesh, const ModelDoom3DefOptions &options, QVector<ModelGameFile> *files, QString *error = nullptr,
						const ModelWorkControl &control = {});
[[nodiscard]] QString modelQuakeCFrameMacrosText(const ModelMesh &mesh, const ModelQuakeCFrameOptions &options, QString *error = nullptr);
[[nodiscard]] QString modelQuake2FrameHeaderText(const ModelMesh &mesh, const ModelQuake2FrameOptions &options, QString *error = nullptr);
[[nodiscard]] QString modelGzdoomModeldefText(const ModelMesh &mesh, const ModelGzdoomModeldefOptions &options, QString *error = nullptr);
// The entries name a .kvx file in the package (GZDoom loads voxels only as
// KVX); refuses an animated model, since voxels are drawn unanimated.
[[nodiscard]] QString modelGzdoomVoxeldefText(const ModelMesh &mesh, const ModelGzdoomVoxeldefOptions &options, QString *error = nullptr);
// Stages a model set (modelDoom3ModelSet) into the open folder or ZIP-family
// package as one package operation, so one undo removes it all; nothing is
// staged when any file is refused. Files already at those paths are replaced
// only with `replaceExisting`.
bool stageModelGameFiles(const QVector<ModelGameFile> &files, PackageStagingModel *staging, bool replaceExisting, QString *error = nullptr);
} // namespace vibestudio
