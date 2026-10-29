#pragma once

// Live symmetric modelling and Mirror Selection.
//
// With ModelMeshToolOptions::symmetryAxes set, a topology or shape tool runs
// on its selection and then the part of the surface it edited is made
// symmetric across each chosen model plane through the origin (Symmetrize
// from the selection's side), as one edit and one undo step. The selection
// must lie on one side of each plane (vertices on the plane count for both),
// or be mirror-symmetric itself, in which case that plane needs no
// mirroring. Only the edited connected part of the surface is mirrored:
// other pieces keep any deliberate asymmetry. The resulting selection is the
// tool's result on both sides.
//
// Behaviour follows Blender's mesh symmetry options for edit-mode tools
// (https://docs.blender.org/manual/en/latest/modeling/meshes/tools/tool_settings.html#mesh-symmetry);
// no Blender code is used.

#include "core/model_work.h"

#include <QString>

#include <functional>

namespace vibestudio
{
enum class ModelEditKind;
struct ModelEdit;
struct ModelEditReport;
struct ModelMesh;
struct ModelSelection;

// Tools that accept symmetryAxes.
[[nodiscard]] bool modelSymmetrySupported(ModelEditKind kind);

using ModelEditApplier =
	std::function<bool(ModelMesh *, const ModelEdit &, ModelSelection *, QString *, const ModelWorkControl &, ModelEditReport *)>;
// Runs `edit` without symmetry through `apply`, then Symmetrize on the
// edited part for each axis, on a copy; the mesh changes only when both
// succeed. `apply` is the document's unreconciled edit service.
bool applySymmetricModelEdit(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *resultingSelection, QString *error,
							 const ModelWorkControl &control, ModelEditReport *report, const ModelEditApplier &apply);
} // namespace vibestudio
