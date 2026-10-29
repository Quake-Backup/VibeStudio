#pragma once

// Importing skeletal clips from other files into an editable rig, exactly or
// retargeted between rigs (ModelEditKind::ImportSkeletalClip).
//
// The source is decoded with decodeModelMesh and its companions (md5anim, IQM,
// MDS, MDR, MDX, GLA, or another .mesh.json), under the decoder's own input
// protection, and one of its clips becomes a pose copy
// (core/model_pose_clipboard.h) carried by the edit, so the edit is
// self-contained for undo, recovery and the CLI.
//
// Exact mode recomposes the source's parent-local poses through the
// destination hierarchy: joint names (through an optional map) and parents
// must match. Retarget mode transfers each mapped joint's rotation relative
// to its bind, R_t = Rbind_t * Rbind_s^-1 * R_s in parent-local space, keeps
// the destination's bone lengths (translations from its bind) and scales the
// mapped root's motion by a height ratio or an explicit factor.

#include "core/model_animation_authoring.h"
#include "core/model_mesh.h"
#include "core/model_work.h"

namespace vibestudio
{
struct ModelEdit;

// Fills options.clip (and, when empty, name, framesPerSecond, loops and
// sourcePath) from clip `clip` of a decoded source model.
bool modelClipImportFromSource(const ModelMesh &source, int clip, const QString &sourcePath, ModelClipImportOptions *options,
							   QString *error = nullptr);
struct ModelClipImportJoint
{
	QString destination;
	// Empty when the destination joint keeps its bind pose.
	QString source;
	bool translation = false;
};
struct ModelClipImportPlan
{
	QVector<ModelClipImportJoint> joints;
	double rootScale = 1;
	int samples = 0;
	QStringList unusedSourceJoints;
	// Curve groups the new clip receives (exact: the source's keys; retarget:
	// captured, one key per sample) and the events it keeps.
	int curves = 0, events = 0;
	bool capturedCurves = false;
};
// What an import would do, without changing anything; false with the reason
// the import would be refused.
bool planModelClipImport(const ModelMesh &mesh, const ModelClipImportOptions &options, ModelClipImportPlan *plan, QString *error = nullptr);
// Candidate-only: use applyModelEdit/ModelDocument for transactional publication.
bool applyModelClipImport(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control = {});
} // namespace vibestudio
