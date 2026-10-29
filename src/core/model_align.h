#pragma once

// Align Vertices: give selected vertices one coordinate on chosen axes, or
// round them to a grid, as one shape edit (ModelEditKind::AlignVertices).
//
// MilkShape 3D's Flatten X/Y/Z, Blender's Scale to zero on an axis about the
// median and Snap Selection to Grid, and setting the centre line of a
// symmetric model exactly on X = 0 all become this edit. Every geometric copy
// of a selected vertex (UV and hard-normal seam copies) moves with it, so seams
// stay closed; the edit honours the frame scope (ModelEdit::frame, -1 for every
// pose) and refreshes normals around the moved vertices. An edit that would
// collapse a face in some pose is refused, leaving the model unchanged.

#include "core/model_work.h"

#include <QString>

#include <array>

namespace vibestudio
{
struct ModelMesh;
struct ModelEdit;
struct ModelSelection;

enum class ModelAlignTarget
{
	// Each chosen axis takes ModelAlignOptions::value.
	Value,
	// The mean of the selected vertices in each pose (each geometric vertex once).
	Centroid,
	// The smallest or largest coordinate of the selection in each pose.
	Minimum,
	Maximum,
	// Each coordinate rounds to the nearest multiple of ModelAlignOptions::grid.
	Grid,
	// The whole selection moves, keeping its shape, so its centroid takes
	// ModelAlignOptions::value on each chosen axis (Blender's Median fields).
	Median
};

struct ModelAlignOptions
{
	// The axes to set: bit mask X = 1, Y = 2, Z = 4. Zero leaves the edit unset.
	int axes = 0;
	ModelAlignTarget target = ModelAlignTarget::Value;
	// Value: the coordinate for each axis, in model units.
	std::array<double, 3> value{0, 0, 0};
	// Grid: the step (0.000001 to 1,000,000 units).
	double grid = 0;
	bool operator==(const ModelAlignOptions &) const = default;
};

// Candidate-only edit dispatched by applyModelMeshToolEdit for AlignVertices.
bool alignModelVertices(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error,
						const ModelWorkControl &control = {});
// "x", "yz", "xyz": the axes of a mask, for messages and CLI JSON.
QString modelAlignAxesText(int axes);
} // namespace vibestudio
