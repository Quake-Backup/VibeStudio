#pragma once

#include "core/model_collision.h"
#include "tests/model_uv_test_helpers.h"

#include <algorithm>

namespace vibestudio::tests
{
// A one-surface vertex animation with `count` distinct poses: frames walk1..3
// then run1.., one attachment tag and one animated collision box per frame,
// and clips walk (0-2), run (3-) and an overlapping all (every frame).
inline ModelMesh frameRangeFixture(int count = 6)
{
	auto mesh = uvSquare();
	const auto info = mesh.frames[0];
	const auto pose = mesh.surfaces[0].frames[0];
	mesh.frames.clear();
	mesh.surfaces[0].frames.clear();
	mesh.tags.clear();
	ModelCollisionBox box{QStringLiteral("body"), {}, {8, 8, 8}, {}};
	QVector<ModelCollisionPose> poses;
	for (int f = 0; f < count; ++f)
	{
		auto frame = info;
		frame.name = f < 3 ? QStringLiteral("walk%1").arg(f + 1) : QStringLiteral("run%1").arg(f - 2);
		frame.origin = {0, 0, float(f)};
		mesh.frames << frame;
		auto geometry = pose;
		for (auto &point : geometry.positions)
		{
			point.z = float(f * 4);
			point.x += float(f) * .5f;
		}
		mesh.surfaces[0].frames << geometry;
		ModelTag tag;
		tag.name = QStringLiteral("tag_mount");
		tag.frameIndex = f;
		tag.origin = {1, 2, float(f * 4)};
		mesh.tags << tag;
		poses << ModelCollisionPose{{float(f), 0, 0}, {8, 8, 8}, {}};
	}
	setModelCollisionFrames(&box, poses);
	mesh.collisionBoxes = {box};
	mesh.animations = {{QStringLiteral("walk"), 0, std::min(3, count)}};
	if (count > 3)
		mesh.animations.append({QStringLiteral("run"), 3, count - 3});
	mesh.animations.append({QStringLiteral("all"), 0, count});
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace vibestudio::tests
