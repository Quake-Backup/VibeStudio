#pragma once

// Transform Model: one affine transform for the whole model
// (ModelEditKind::TransformModel), for re-orienting, rescaling and re-origining
// imported models for idTech delivery (facing +X, Quake-unit scale, the origin
// at the feet or centre).
//
// The edit uses ModelEdit::translation, rotation (degrees, X then Y then Z)
// and scale about ModelEdit::pivot, as Apply Transform does, and moves every
// part of the model with the geometry:
// - every surface position and normal in every pose (a mirror reverses
//   winding on every surface, so faces keep facing outward);
// - attachment tags in every frame: origins move and axes turn, staying
//   orthonormal (a non-uniform scale keeps the forward and up directions; a
//   mirror recomputes the left axis so tags stay right-handed);
// - collision boxes and every collision pose (a rotated box under a
//   non-uniform scale would shear and is refused);
// - the skeleton: bind joints, every clip frame and bound, skeletal tag
//   offsets and the influences' bind offsets, so baking the skeleton again
//   gives the transformed frames. Skeletal models take rotation, positive
//   uniform scale and translation only, and rotating a model whose root
//   joints have animation curves is refused;
// - the Quake MDL eye position.
// Bounds and radii are recomputed afterwards. The edit spans every frame and
// is refused, changing nothing, when any part cannot follow.

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QString>

namespace vibestudio
{
struct ModelEdit;

bool applyModelWholeTransform(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control = {});

enum class ModelOriginTarget
{
	// The centre of the model's bounds in the chosen pose.
	BoundsCentre,
	// The centre of the bounds' base (lowest Z): the feet of a standing model.
	BoundsBottom,
	// An explicit point, such as the 3D cursor or the selection's centre.
	Point
};
// The translation that moves the target point to the model origin, for a
// TransformModel edit (Origin to Bounds Centre, Bottom, Cursor or Selection).
bool modelOriginOffset(const ModelMesh &mesh, int frame, ModelOriginTarget target, ModelVec3 point, ModelVec3 *offset,
					   QString *error = nullptr);
// The uniform scale that makes the model `height` units tall (along Z) in
// the chosen pose.
bool modelHeightScale(const ModelMesh &mesh, int frame, double height, double *factor, QString *error = nullptr);
} // namespace vibestudio
