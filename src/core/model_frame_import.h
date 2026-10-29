#pragma once

// Vertex-animation frames brought in from other files
// (ModelEditKind::ImportFrames): an OBJ sequence exported per frame from
// Blender or another modeller (one frame per file, in natural file-name
// order), or the frames of an MD2, MD3 or MDL of the same mesh. Each source
// must have the target's topology; its poses become new frames, or replace
// existing ones, in one undo step. Everything indexed by frame follows
// (core/model_frame_tools.h).
//
// Matching, per target surface (by name, else by index):
// - Exact: identical vertex and triangle lists.
// - Corners (the default): the same triangles in the same order; each target
//   vertex takes the position of the source vertices at its triangle
//   corners, which must agree. This survives exporters that split vertices
//   differently in each frame (per-frame normal lists in OBJ).
// - Nearest: each target vertex takes the source vertex with the same UV
//   whose position in the source's first frame lies within `tolerance` of
//   the target's reference frame.

#include "core/model_frame_tools.h"
#include "core/model_obj.h"

#include <QStringList>

namespace vibestudio
{
enum class ModelFrameMatch
{
	Exact,
	Corners,
	Nearest,
};

// ModelEdit::frameImport. Defaults are required by every other edit.
struct ModelFrameImportOptions
{
	// Decoded sources in order; each contributes every frame it holds.
	QVector<ModelMesh> sources;
	// One display name per source (its file name), for messages and frame
	// names of single-frame sources.
	QStringList sourceNames;
	ModelFrameMatch match = ModelFrameMatch::Corners;
	// Nearest: the largest position distance, in model units.
	double tolerance = 0.001;
	// Nearest: the target frame compared with each source's first frame.
	int referenceFrame = 0;
	// The new frames follow this frame; -1 appends after the last frame.
	int insertAfter = -1;
	// Zero or more: replace frames from this one instead of inserting.
	int replaceFirst = -1;
	// Optional clip over the new frames, and its rate (zero: unspecified).
	QString clipName;
	double clipFps = 0;
	[[nodiscard]] bool isDefault() const;
};

// The suffixes frame sources may have (obj, md2, md3, mdl, mesh.json).
[[nodiscard]] QStringList modelFrameSourceSuffixes();
// Sorts paths the way people number frames: walk_2.obj before walk_10.obj.
[[nodiscard]] QStringList naturalSortedModelFramePaths(const QStringList &paths);
// Reads and decodes frame sources from disk, each within the editable
// source limits, refusing a source that is the same file as `protectedPath`
// (the document's own source or the output). OBJ sources use `obj`.
bool readModelFrameSources(const QStringList &paths, const ModelObjImportOptions &obj, const QString &protectedPath,
						   QVector<ModelMesh> *sources, QStringList *names, QString *error = nullptr, const ModelWorkControl &control = {});
// The number of frames the sources hold together.
[[nodiscard]] int modelFrameSourceCount(const QVector<ModelMesh> &sources);
// Candidate-only implementation; applyModelEdit validates and adopts.
bool applyModelFrameImportEdit(ModelMesh *candidate, const ModelEdit &edit, QString *error = nullptr, const ModelWorkControl &control = {});

// Writes frames first..last as one OBJ per frame. `pattern` names the files:
// its last run of '#' becomes the zero-padded frame number (walk_###.obj),
// or "_<number>" is added before ".obj". Each file goes through the guarded
// one-frame export (core/model_frame_export.h), which refuses writing over
// `protectedFiles`. Every destination is reviewed first, so nothing is
// written when one file exists and `overwrite` is false; a dry run writes
// nothing.
bool exportModelFrameObjSequence(const ModelMesh &mesh, int first, int last, const QString &pattern, bool overwrite, bool dryRun,
								 const QStringList &protectedFiles, QStringList *written, QString *error = nullptr,
								 const ModelWorkControl &control = {});
} // namespace vibestudio
