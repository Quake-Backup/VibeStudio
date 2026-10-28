#pragma once
#include "tests/model_rig_test_helpers.h"
#include <cmath>
#include <numbers>

namespace vibestudio::tests
{
// A left/right symmetric rig across Y = 0: root, spine, and l_/r_ arms with
// hands. Clip "walk" swings the left arm about Z by 20 degrees per sample.
inline ModelQuat poseZRotation(double degrees)
{
	const double half = degrees * std::numbers::pi / 360.0;
	return {0, 0, float(std::sin(half)), float(std::cos(half))};
}
inline ModelMesh symmetricRigFixture(int samples = 4)
{
	auto mesh = rigFixture();
	mesh.skeleton.joints = {{"root", -1, modelJointIdentity()},
							{"spine", 0, modelJointMatrix({}, {0, 0, 4})},
							{"l_arm", 1, modelJointMatrix({}, {0, 2, 8})},
							{"l_hand", 2, modelJointMatrix({}, {0, 6, 8})},
							{"r_arm", 1, modelJointMatrix({}, {0, -2, 8})},
							{"r_hand", 4, modelJointMatrix({}, {0, -6, 8})}};
	mesh.skeleton.tags = {{"tag_l_hand", 3, modelJointIdentity()}};
	const auto bind = modelSkeletonBindPose(mesh.skeleton);
	ModelSkeletalClip clip;
	clip.name = "walk";
	clip.framesPerSecond = 12;
	for (int f = 0; f < samples; ++f)
	{
		auto pose = bind;
		pose[2] = modelJointMatrix(poseZRotation(20.0 * f), {0, 2, 8});
		pose[3] = modelJointMultiply(pose[2], modelJointMatrix({}, {0, 4, 0}));
		clip.frames.append(pose);
	}
	mesh.skeleton.clips = {clip};
	auto &s = mesh.surfaces[0];
	s.vertexCount = 6;
	s.triangles = {{0, 1, 2}, {0, 2, 3}, {0, 4, 5}, {0, 5, 1}};
	s.texCoords = {{0, 0}, {.25f, 0}, {.5f, 0}, {.75f, 0}, {.25f, 1}, {.5f, 1}};
	s.frames = {{{{0, 0, 0}, {0, 0, 4}, {0, 2, 8}, {0, 6, 8}, {0, -2, 8}, {0, -6, 8}}, QVector<ModelVec3>(6, {1, 0, 0})}};
	if (!modelSkinningFromBindPose(mesh.skeleton, s.frames[0].positions, s.frames[0].normals, {0, 1, 2, 3, 4, 5}, {1, 1, 1, 1, 1, 1}, 1,
								   &s.skinning) ||
		!bakeModelSkeleton(&mesh))
		return {};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline ModelEdit pastePoseEdit(const QByteArray &clipboard, int clip, int first)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::PastePose;
	edit.animation.paste.clipboard = clipboard;
	edit.animation.paste.clip = clip;
	edit.animation.paste.first = first;
	return edit;
}
inline QByteArray copyPoseBytes(const ModelMesh &mesh, int clip, int first, int last, const QStringList &joints = {}, bool keys = false)
{
	ModelPoseCopyOptions options;
	options.clip = clip;
	options.first = first;
	options.last = last;
	options.joints = joints;
	options.keys = keys;
	ModelPoseClipboard clipboard;
	return copyModelPose(mesh, options, &clipboard) ? modelPoseClipboardBytes(clipboard) : QByteArray();
}
} // namespace vibestudio::tests
