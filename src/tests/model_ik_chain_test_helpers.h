#pragma once
#include "tests/model_rig_test_helpers.h"

namespace vibestudio::tests
{
// A straight chain of `joints` joints along +X, two units apart (j0 at the
// origin), plus a separate root-level "prop" joint that slides along Y over
// the clip's samples. Each chain joint carries two rigidly skinned vertices.
inline ModelMesh ikChainFixture(int joints, int samples = 3)
{
	auto mesh = rigFixture();
	mesh.skeleton.joints.clear();
	for (int j = 0; j < joints; ++j)
		mesh.skeleton.joints.append({QStringLiteral("j%1").arg(j), j - 1, modelJointMatrix({}, {float(2 * j), 0, 0})});
	mesh.skeleton.joints.append({QStringLiteral("prop"), -1, modelJointMatrix({}, {float(joints), 6, 0})});
	const auto bind = modelSkeletonBindPose(mesh.skeleton);
	ModelSkeletalClip clip;
	clip.name = QStringLiteral("reach");
	clip.framesPerSecond = 24;
	for (int f = 0; f < samples; ++f)
	{
		auto pose = bind;
		pose.last() = modelJointMatrix({}, {float(joints), float(6 - f % 3), 0});
		clip.frames.append(pose);
	}
	mesh.skeleton.clips = {clip};
	mesh.skeleton.tags.clear();
	auto &s = mesh.surfaces[0];
	s.vertexCount = 2 * joints;
	s.triangles.clear();
	s.texCoords.clear();
	QVector<ModelVec3> positions, normals;
	QVector<int> owners;
	for (int j = 0; j < joints; ++j)
	{
		positions << ModelVec3{float(2 * j), 0, 0} << ModelVec3{float(2 * j), 1, 0};
		normals << ModelVec3{0, 0, 1} << ModelVec3{0, 0, 1};
		owners << j << j;
		s.texCoords << ModelTexCoord{float(j) / joints, 0} << ModelTexCoord{float(j) / joints, 1};
		if (j + 1 < joints)
			s.triangles << ModelTriangle{2 * j, 2 * j + 2, 2 * j + 1} << ModelTriangle{2 * j + 2, 2 * j + 3, 2 * j + 1};
	}
	s.frames = {{positions, normals}};
	s.skinning = {};
	if (!modelSkinningFromBindPose(mesh.skeleton, positions, normals, owners, QVector<float>(owners.size(), 1), 1, &s.skinning) ||
		!bakeModelSkeleton(&mesh))
		return {};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline ModelEdit chainIkEdit(int joints, ModelVec3 target, ModelVec3 pole, int last = 2)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::SolveChainIk;
	edit.ik.clip = 0;
	edit.ik.first = 0;
	edit.ik.last = last;
	edit.ik.rootJoint = QStringLiteral("j0");
	edit.ik.endJoint = QStringLiteral("j%1").arg(joints - 1);
	edit.ik.target = target;
	edit.ik.pole = pole;
	edit.ik.iterations = 200;
	return edit;
}
} // namespace vibestudio::tests
