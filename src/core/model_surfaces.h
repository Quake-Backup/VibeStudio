#pragma once
#include "core/model_mesh.h"

namespace slopstudio
{
enum class ModelEditKind;
struct ModelEdit;
struct ModelSelection;
bool isModelSurfaceEdit(ModelEditKind kind);
// Candidate-only operation; applyModelEdit validates and atomically commits the
// complete document. No welding, attribute recomputation or implicit cleanup.
bool applyModelSurfaceEdit(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error,
						   const ModelWorkControl &control = {});
} // namespace slopstudio
