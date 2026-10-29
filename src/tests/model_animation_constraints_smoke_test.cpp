#include "cli/model_tools.h"
#include "core/model_animation_constraints.h"
#include "tests/model_pose_clipboard_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool ok, const char *label)
{
	++checks;
	if (!ok)
	{
		++failures;
		std::cerr << "FAIL: " << label << '\n';
	}
	return ok;
}
bool near(ModelVec3 a, ModelVec3 b, double tolerance = 1e-4)
{
	return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.z - b.z) <= tolerance;
}
bool sameFrames(const QVector<QVector<ModelJointMatrix>> &a, const QVector<QVector<ModelJointMatrix>> &b, double tolerance = 0)
{
	if (a.size() != b.size())
		return false;
	for (int f = 0; f < a.size(); ++f)
		for (int j = 0; j < a[f].size(); ++j)
			if (modelJointMatrixDistance(a[f][j], b[f][j]) > tolerance)
				return false;
	return true;
}
ModelVec3 at(const ModelMesh &mesh, int frame, const char *joint)
{
	return modelJointTranslation(mesh.skeleton.clips[0].frames[frame][modelJointIndex(mesh.skeleton, joint, Qt::CaseSensitive)]);
}
ModelEdit stackEdit(ModelClipStackAction action)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::EditClipStack;
	edit.animation.stack.action = action;
	edit.animation.stack.clip = 0;
	return edit;
}
ModelEdit addConstraint(ModelClipConstraintKind kind, const QString &joint, const QString &target = {})
{
	auto edit = stackEdit(ModelClipStackAction::AddConstraint);
	edit.animation.stack.constraint.kind = kind;
	edit.animation.stack.constraint.joint = joint;
	edit.animation.stack.constraint.target = target;
	return edit;
}
ModelClipLayer turnLayer(double degrees, double weight)
{
	ModelClipLayer layer;
	layer.name = "turn";
	layer.weight = weight;
	ModelPoseCurves group;
	group.joint = "l_arm";
	const double values[9] = {0, 0, 0, 0, 0, degrees, 1, 1, 1};
	for (int c = 0; c < 9; ++c)
	{
		ModelPoseCurveKey key;
		key.value = values[c];
		group.channels[c] = {key};
	}
	layer.curves = {group};
	return layer;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir tmp(QDir(root).filePath("constraints-XXXXXX"));
	if (!tmp.isValid())
		return 1;
	QString error;
	const auto mesh = symmetricRigFixture();

	// A copy-transform constraint keeps its base and evaluates into frames.
	ModelDocument document;
	expect(document.setMesh(mesh, &error) && document.edit(addConstraint(ModelClipConstraintKind::CopyTransform, "r_hand", "l_hand"), &error),
		   "add a copy-transform constraint");
	const auto &clip = document.mesh().skeleton.clips[0];
	bool copied = true;
	for (int f = 0; f < 4; ++f)
		copied = copied && near(at(document.mesh(), f, "r_hand"), at(document.mesh(), f, "l_hand"));
	expect(copied && sameFrames(clip.base, mesh.skeleton.clips[0].frames), "the constraint moves the joint and keeps the base samples");
	expect(document.undo() && !modelClipHasStack(document.mesh().skeleton.clips[0]) && document.redo(), "adding a constraint is one undo step");

	// Determinism and order.
	ModelMesh twice = mesh;
	expect(applyModelEdit(&twice, addConstraint(ModelClipConstraintKind::CopyTransform, "r_hand", "l_hand"), nullptr, &error) &&
			   sameFrames(twice.skeleton.clips[0].frames, document.mesh().skeleton.clips[0].frames),
		   "evaluating the same stack twice gives identical samples");
	auto childOf = addConstraint(ModelClipConstraintKind::ChildOf, "r_hand", "spine");
	ModelMesh ordered = document.mesh(), reversed = mesh;
	expect(applyModelEdit(&ordered, childOf, nullptr, &error) && applyModelEdit(&reversed, childOf, nullptr, &error) &&
			   applyModelEdit(&reversed, addConstraint(ModelClipConstraintKind::CopyTransform, "r_hand", "l_hand"), nullptr, &error),
		   "stack two constraints in both orders");
	expect(!sameFrames(ordered.skeleton.clips[0].frames, reversed.skeleton.clips[0].frames, 1e-3),
		   "constraints evaluate in their stack order");

	// Cycles.
	auto cycle = addConstraint(ModelClipConstraintKind::ChildOf, "l_arm", "l_hand");
	expect(!document.edit(cycle, &error) && error.contains("l_hand"), "a constraint that follows a joint it moves is refused");

	// Editing a base curve evaluates the constraint again.
	ModelEdit capture;
	capture.kind = ModelEditKind::EditAnimationCurves;
	capture.curves.clip = 0;
	capture.curves.joint = "l_arm";
	expect(document.edit(capture, &error), "capture curves on the base");
	auto key = capture;
	key.curves.action = ModelCurveAction::SetKey;
	key.curves.channel = 5;
	key.curves.frame = 1;
	key.curves.key.frame = 1;
	key.curves.key.value = 75;
	expect(document.edit(key, &error), "set a base key");
	const auto &edited = document.mesh();
	expect(near(at(edited, 1, "r_hand"), at(edited, 1, "l_hand")) && !near(at(edited, 1, "l_hand"), at(mesh, 1, "l_hand"), 1e-2),
		   "editing a base curve re-evaluates the constraint");

	// Direct sample edits wait for a bake; joint renames follow.
	ModelEdit transform;
	transform.kind = ModelEditKind::TransformSkeletalPose;
	transform.skeletal.clip = 0;
	transform.skeletal.joint = "spine";
	transform.skeletal.rotation = {0, 0, 10};
	expect(!document.edit(transform, &error) && error.contains("bake"), "direct pose edits on a stacked clip are refused");
	ModelEdit addJoint;
	addJoint.kind = ModelEditKind::AddJoint;
	addJoint.rig.name = "extra";
	addJoint.rig.parent = "spine";
	expect(!document.edit(addJoint, &error), "hierarchy edits wait for a bake");
	ModelEdit rename;
	rename.kind = ModelEditKind::RenameJoint;
	rename.rig.joint = "l_hand";
	rename.rig.name = "l_palm";
	expect(document.edit(rename, &error) && document.mesh().skeleton.clips[0].constraints[0].target == "l_palm" && document.undo(),
		   "renaming a joint renames constraint references");

	// Source version 15 round trip.
	const auto saved = QDir(tmp.path()).filePath("stack.mesh.json");
	ModelDocument reopened;
	expect(document.save(saved, false, &error) && reopened.load(saved, &error) &&
			   reopened.mesh().skeleton.clips[0].constraints == document.mesh().skeleton.clips[0].constraints &&
			   sameFrames(reopened.mesh().skeleton.clips[0].base, document.mesh().skeleton.clips[0].base, 1e-6),
		   "constraints and base samples survive save and reload");
	QFile file(saved);
	QByteArray bytes;
	if (file.open(QIODevice::ReadOnly))
		bytes = file.readAll();
	file.close();
	auto older = bytes;
	older.replace("\"version\":15", "\"version\":14");
	ModelMesh parsed;
	expect(bytes.contains("\"version\":15") && !parseEditableModel(older, &parsed, &error), "clip stacks need source version 15");

	// Exports read the evaluated samples.
	ModelExportOptions exportOptions;
	exportOptions.clip = 0;
	const auto md5anim = exportEditableModel(document.mesh(), "md5anim", exportOptions, &error);
	const auto animPath = QDir(tmp.path()).filePath("walk.md5anim");
	QFile anim(animPath);
	expect(!md5anim.isEmpty() && anim.open(QIODevice::WriteOnly) && anim.write(md5anim) == md5anim.size(), "export the stacked clip");
	anim.close();
	const auto decoded = decodeModelMeshFile(animPath);
	expect(decoded.skeleton.clips.size() == 1 && sameFrames(decoded.skeleton.clips[0].frames, document.mesh().skeleton.clips[0].frames, 2e-4),
		   "an exported md5anim equals the stack's evaluation");

	// Bake keeps the samples.
	const auto beforeBake = document.mesh().skeleton.clips[0].frames;
	expect(document.edit(stackEdit(ModelClipStackAction::Bake), &error) && !modelClipHasStack(document.mesh().skeleton.clips[0]) &&
			   document.mesh().skeleton.clips[0].base.isEmpty() && sameFrames(document.mesh().skeleton.clips[0].frames, beforeBake) &&
			   document.mesh().skeleton.clips[0].curves.size() == 1,
		   "bake keeps the evaluated samples and the curves the stack left alone");
	expect(document.edit(transform, &error), "direct pose edits work again after a bake");

	// Layers at weight 0 and 1.
	auto layered = mesh;
	auto layer = stackEdit(ModelClipStackAction::AddLayer);
	layer.animation.stack.layer = turnLayer(30, 0);
	expect(applyModelEdit(&layered, layer, nullptr, &error) && sameFrames(layered.skeleton.clips[0].frames, mesh.skeleton.clips[0].frames, 1e-6),
		   "a layer at weight 0 leaves the samples");
	auto full = stackEdit(ModelClipStackAction::UpdateLayer);
	full.animation.stack.index = 0;
	full.animation.stack.layer = turnLayer(30, 1);
	expect(applyModelEdit(&layered, full, nullptr, &error), "raise the layer to weight 1");
	const auto turned = modelJointsToLocalSpace(modelJointParents(layered.skeleton), layered.skeleton.clips[0].frames[2]);
	const auto plain = modelJointsToLocalSpace(modelJointParents(mesh.skeleton), mesh.skeleton.clips[0].frames[2]);
	const auto expected = modelQuatMultiply(modelJointRotation(plain[2]), poseZRotation(30));
	const auto actual = modelJointRotation(turned[2]);
	expect(std::abs(std::abs(expected.x * actual.x + expected.y * actual.y + expected.z * actual.z + expected.w * actual.w) - 1) < 1e-5,
		   "an additive layer at weight 1 adds its full rotation");
	auto removeLayer = stackEdit(ModelClipStackAction::RemoveLayer);
	removeLayer.animation.stack.index = 0;
	expect(applyModelEdit(&layered, removeLayer, nullptr, &error) && !modelClipHasStack(layered.skeleton.clips[0]) &&
			   sameFrames(layered.skeleton.clips[0].frames, mesh.skeleton.clips[0].frames, 1e-6),
		   "removing the last layer returns the clip to its base");

	// IK, foot lock and weights.
	auto ik = addConstraint(ModelClipConstraintKind::Ik, "l_hand");
	ik.animation.stack.constraint.point = {2, 5, 6};
	ik.animation.stack.constraint.pole = {0, 4, 16};
	ModelMesh reached = mesh;
	bool atTarget = applyModelEdit(&reached, ik, nullptr, &error);
	for (int f = 0; atTarget && f < 4; ++f)
		atTarget = near(at(reached, f, "l_hand"), {2, 5, 6}, 1e-3);
	expect(atTarget, "an IK constraint holds the hand on its target");
	auto lock = addConstraint(ModelClipConstraintKind::FootLock, "l_hand");
	lock.animation.stack.constraint.first = 1;
	ModelMesh locked = mesh;
	bool planted = applyModelEdit(&locked, lock, nullptr, &error);
	for (int f = 1; planted && f < 4; ++f)
		planted = near(at(locked, f, "l_hand"), at(mesh, 1, "l_hand"), 1e-3);
	expect(planted && near(at(locked, 0, "l_hand"), at(mesh, 0, "l_hand")), "a foot lock holds the joint from its first sample");
	auto half = addConstraint(ModelClipConstraintKind::CopyTransform, "r_hand", "l_hand");
	half.animation.stack.constraint.weight = .5;
	ModelMesh blended = mesh;
	expect(applyModelEdit(&blended, half, nullptr, &error) && !near(at(blended, 3, "r_hand"), at(blended, 3, "l_hand"), 1e-2) &&
			   !near(at(blended, 3, "r_hand"), at(mesh, 3, "r_hand"), 1e-2),
		   "a half-weight constraint blends between poses");
	auto aim = addConstraint(ModelClipConstraintKind::Aim, "r_arm");
	aim.animation.stack.constraint.point = {10, -2, 8};
	aim.animation.stack.constraint.axis = 1;
	aim.animation.stack.constraint.usePole = false;
	ModelMesh aimed = mesh;
	expect(applyModelEdit(&aimed, aim, nullptr, &error), "add an aim constraint");
	const auto arm = at(aimed, 0, "r_arm"), hand = at(aimed, 0, "r_hand");
	const double dx = hand.x - arm.x, dy = hand.y - arm.y, dz = hand.z - arm.z, length = std::sqrt(dx * dx + dy * dy + dz * dz);
	// r_arm's +Y axis points at the hand's offset (0, -4, 0) in bind, so +Y aimed at +X rotates the hand to -X... check direction to target instead.
	const auto rotation = modelJointRotation(aimed.skeleton.clips[0].frames[0][4]);
	const auto aimAxis = modelQuatRotate(rotation, {0, 1, 0});
	expect(length > 3.9 && near(aimAxis, {1, 0, 0}, 1e-3), "an aim constraint points the joint's axis at the target");

	// CLI.
	const auto source = QDir(tmp.path()).filePath("rig.mesh.json"), out = QDir(tmp.path()).filePath("out.mesh.json");
	ModelDocument cliSource;
	expect(cliSource.setMesh(mesh, &error) && cliSource.save(source, false, &error), "save the CLI source");
	auto result = cli::runModelConstraints({"vibestudio", "--cli", "model", "constraints", source, "--operation", "add", "--clip", "0", "--kind",
											"copy-transform", "--joint", "r_hand", "--target", "l_hand", "--weight", "1", "--output", out, "--json"});
	expect(result.exitCode == 0 && result.payload["clips"].toArray()[0].toObject()["constraints"].toArray().size() == 1, "CLI adds a constraint");
	result = cli::runModelConstraints({"vibestudio", "--cli", "model", "constraints", out, "--json"});
	expect(result.exitCode == 0 && result.payload["clips"].toArray()[0].toObject()["stack"].toBool(), "CLI lists the stack");
	expect(cli::runModelConstraints({"vibestudio", "--cli", "model", "constraints", source, "--operation", "add", "--clip", "0", "--kind", "aim",
									 "--joint", "l_arm", "--target", "l_hand", "--output", out, "--overwrite"})
				   .exitCode == 4,
		   "CLI refuses a cycle");
	const auto layerFile = QDir(tmp.path()).filePath("layer.json");
	QFile layerOut(layerFile);
	expect(layerOut.open(QIODevice::WriteOnly) && layerOut.write(QJsonDocument(modelClipCurvesJson(turnLayer(30, 1).curves)).toJson()) > 0, "write a layer file");
	layerOut.close();
	const auto layered2 = QDir(tmp.path()).filePath("layered.mesh.json");
	expect(cli::runModelConstraints({"vibestudio", "--cli", "model", "constraints", out, "--operation", "add-layer", "--clip", "0", "--layer", layerFile,
									 "--mode", "additive", "--weight", "0.5", "--name", "turn", "--output", layered2})
					   .exitCode == 0,
		   "CLI adds a layer");
	const auto baked = QDir(tmp.path()).filePath("baked.mesh.json");
	expect(cli::runModelConstraints({"vibestudio", "--cli", "model", "constraints", layered2, "--operation", "bake", "--clip", "0", "--output", baked}).exitCode == 0,
		   "CLI bakes the stack");
	if (argc > 1)
	{
		QProcess process;
		process.start(QString::fromLocal8Bit(argv[1]), {"--cli", "model", "constraints", out, "--json"});
		expect(process.waitForFinished(60000) && process.exitCode() == 0, "application dispatches model constraints");
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
