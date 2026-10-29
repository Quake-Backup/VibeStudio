#pragma once

#include "core/level_map.h"

#include <QMatrix4x4>

#include <array>

namespace vibestudio
{
// Where a placed model entity puts its model, read from the keys level preview
// draws it with: the entity origin, then `angles` ("pitch yaw roll": yaw about Z,
// then pitch about Y, then roll about X) or `angle` (yaw), then `modelscale_vec`
// or `modelscale`. Shared by level preview and model collision placement.
struct LevelModelInstanceTransform
{
	LevelMapVec3 origin{0, 0, 0, true};
	// Degrees. Without an `angles` key, yaw comes from `angle` and pitch/roll are zero.
	double pitch = 0, yaw = 0, roll = 0;
	bool hasAngles = false;
	std::array<double, 3> scale{1, 1, 1};
	// Which scale key was read: 0 none, 1 `modelscale`, 3 `modelscale_vec`.
	int scaleKeys = 0;
};

LevelModelInstanceTransform levelModelInstanceTransform(const LevelMapEntity &entity);
// The preview's single-precision matrix, built in the preview's operation order.
QMatrix4x4 levelModelInstanceMatrix(const LevelModelInstanceTransform &transform);
// The same transform in double precision, row by row (3x4, translation last):
// x' = m[0]x + m[1]y + m[2]z + m[3], and so on.
std::array<double, 12> levelModelInstanceAffine(const LevelModelInstanceTransform &transform);
LevelMapVec3 applyLevelModelInstanceAffine(const std::array<double, 12> &affine, double x, double y, double z);
// True for an entity that places a model through its `model` key (misc_model,
// misc_gamemodel and similar); false for brush-model references such as "*1".
bool levelEntityPlacesModel(const LevelMapEntity &entity);
} // namespace vibestudio
