#pragma once

// Animation authoring edits added in the 2026-10 modeller round: pose and key
// paste (core/model_pose_clipboard.h). Each is a ModelEditKind handled by
// applyModelEdit through the skeletal edit path, so undo, recovery, saves and
// the CLI treat them like every other skeletal edit. Their settings travel in
// ModelEdit::animation.

#include "core/model_pose_clipboard.h"

namespace vibestudio
{
struct ModelEdit;
enum class ModelEditKind;

struct ModelAnimationAuthoringOptions
{
	ModelPosePasteOptions paste{};
	bool operator==(const ModelAnimationAuthoringOptions &) const = default;
};

[[nodiscard]] bool isModelAnimationAuthoringEdit(ModelEditKind kind);
// Refuses settings given to an edit they do not apply to.
bool validModelAnimationAuthoringOptions(const ModelEdit &edit, QString *error);
// Candidate-only dispatch for isModelAnimationAuthoringEdit kinds.
bool applyModelAnimationAuthoringEdit(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control = {});
} // namespace vibestudio
