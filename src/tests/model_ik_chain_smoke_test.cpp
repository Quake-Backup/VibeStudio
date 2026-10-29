#include "cli/model_tools.h"
#include "core/model_ik.h"
#include "tests/model_ik_chain_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <numbers>

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
double distance(ModelVec3 a, ModelVec3 b)
{
	return std::sqrt(double(a.x - b.x) * (a.x - b.x) + double(a.y - b.y) * (a.y - b.y) + double(a.z - b.z) * (a.z - b.z));
}
ModelVec3 at(const ModelMesh &mesh, int frame, int joint)
{
	return modelJointTranslation(mesh.skeleton.clips[0].frames[frame][joint]);
}
// Which side of the root-to-target line a joint bends to in sample 0.
double side(const ModelMesh &mesh, int joint, ModelVec3 target)
{
	const auto p = at(mesh, 0, joint);
	return double(target.x) * p.y - double(target.y) * p.x;
}
// Every bone of the chain keeps its two-unit length in every sample.
bool lengthsKept(const ModelMesh &mesh, int joints, double tolerance = 1e-4)
{
	for (int f = 0; f < mesh.skeleton.clips[0].frames.size(); ++f)
		for (int j = 1; j < joints; ++j)
			if (std::abs(distance(at(mesh, f, j), at(mesh, f, j - 1)) - 2) > tolerance)
				return false;
	return true;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir tmp(QDir(root).filePath("ik-chain-XXXXXX"));
	if (!tmp.isValid())
		return 1;
	QString error;
	for (int joints : {3, 5, 10})
	{
		const auto mesh = ikChainFixture(joints);
		if (!expect(mesh.isValid(), "chain fixture builds"))
			return 1;
		const double total = 2.0 * (joints - 1);
		const ModelVec3 target{float(total * .55), float(total * .35), 0}, pole{float(total * .3), float(total), 0};
		for (auto solver : {ModelIkSolver::Fabrik, ModelIkSolver::Ccd})
		{
			auto edit = chainIkEdit(joints, target, pole);
			edit.ik.solver = solver;
			edit.ik.iterations = 500;
			edit.ik.usePole = solver == ModelIkSolver::Fabrik;
			ModelDocument document;
			if (!expect(document.setMesh(mesh, &error) && document.edit(edit, &error), "chain solves a reachable target"))
			{
				std::cerr << qPrintable(error) << '\n';
				continue;
			}
			const auto solved = document.mesh();
			bool reached = true;
			for (int f = 0; f < 3; ++f)
				reached = reached && distance(at(solved, f, joints - 1), target) < 1e-3 && distance(at(solved, f, 0), {0, 0, 0}) < 1e-6;
			expect(reached, "the end reaches the target and the root stays in place");
			expect(lengthsKept(solved, joints), "bone lengths are kept");
			const auto samples = modelIkSamples(solved, edit.ik, true);
			expect(samples.size() == 3 && samples[0].reached && distance(samples[0].target, target) < 1e-6, "diagnostics report reached samples");
			expect(document.canUndo() && document.undo() && !document.canUndo(), "the solve is one undo step");
			if (solver == ModelIkSolver::Fabrik)
			{
				expect(side(solved, joints / 2, target) > 0, "the FABRIK bend follows the pole");
				ModelDocument again;
				expect(again.setMesh(mesh, &error) && again.edit(edit, &error) && rigBytes(again.mesh()) == rigBytes(solved),
					   "pole handling is deterministic");
				auto flipped = edit;
				flipped.ik.pole.y = -flipped.ik.pole.y;
				expect(again.setMesh(mesh, &error) && again.edit(flipped, &error) && side(again.mesh(), joints / 2, target) < 0,
					   "the opposite pole bends the other way");
			}
		}
		auto far = chainIkEdit(joints, {float(total * 1.5), 0, 0}, {0, 1, 0});
		ModelDocument document;
		expect(document.setMesh(mesh, &error) && !document.edit(far, &error) && !document.canUndo(), "an unreachable target is refused");
		far.ik.nearestReachable = true;
		expect(document.edit(far, &error) && std::abs(distance(at(document.mesh(), 0, joints - 1), {0, 0, 0}) - total) < 1e-3,
			   "nearest reachable stretches the chain towards the target");
	}

	// Joint limits.
	const int joints = 5;
	auto mesh = ikChainFixture(joints);
	ModelEdit limit;
	limit.kind = ModelEditKind::SetJointLimits;
	limit.animation.limits.joint = QStringLiteral("j1");
	limit.animation.limits.limits.enabled = true;
	limit.animation.limits.limits.minimum = {-5, -5, -10};
	limit.animation.limits.limits.maximum = {5, 5, 10};
	ModelDocument limited;
	expect(limited.setMesh(mesh, &error) && limited.edit(limit, &error) && limited.mesh().skeleton.joints[1].limits.enabled,
		   "set joint limits");
	for (const auto *name : {"j0", "j2", "j3"})
	{
		auto other = limit;
		other.animation.limits.joint = QString::fromLatin1(name);
		expect(limited.edit(other, &error), "limit every turning joint of the chain");
	}
	auto bad = limit;
	bad.animation.limits.limits.minimum[2] = 30;
	expect(!limited.edit(bad, &error), "a minimum above the maximum is refused");
	auto constrained = chainIkEdit(joints, {3, 4, 0}, {2, 8, 0});
	constrained.ik.limits = true;
	constrained.ik.nearestReachable = true;
	constrained.ik.iterations = 100;
	expect(limited.edit(constrained, &error), "chain IK solves with joint limits");
	bool within = true;
	for (const auto &pose : limited.mesh().skeleton.clips[0].frames)
		within = within && modelPoseWithinJointLimits(limited.mesh().skeleton, pose, 1e-3, &error);
	expect(within, "joint limits are never exceeded");
	expect(lengthsKept(limited.mesh(), joints), "limited solves keep bone lengths");
	const auto limits = modelJointLimitAngles(limited.mesh().skeleton, limited.mesh().skeleton.clips[0].frames[0], 1);
	expect(std::abs(limits[2]) <= 10 + 1e-3, "the limited joint's turn stays inside its range");
	auto strict = constrained;
	strict.ik.nearestReachable = false;
	ModelDocument strictDocument;
	expect(strictDocument.setMesh(limited.mesh(), &error) && !strictDocument.edit(strict, &error) && error.contains(QStringLiteral("limits")),
		   "a target out of reach within the limits is refused unless nearest reachable is chosen");

	// The limits persist in source version 15; version 14 sources cannot carry them.
	const auto saved = QDir(tmp.path()).filePath("limited.mesh.json");
	ModelDocument reopened;
	expect(limited.save(saved, false, &error) && reopened.load(saved, &error) &&
			   reopened.mesh().skeleton.joints[1].limits == limited.mesh().skeleton.joints[1].limits,
		   "joint limits survive save and reload");
	QFile file(saved);
	QByteArray bytes;
	if (file.open(QIODevice::ReadOnly))
		bytes = file.readAll();
	file.close();
	expect(QJsonDocument::fromJson(bytes).object().value("version").toInt() == 15, "limits write source version 15");
	auto older = bytes;
	older.replace("\"version\":15", "\"version\":14");
	ModelMesh parsed;
	expect(!parseEditableModel(older, &parsed, &error), "a version 14 source cannot carry joint limits");

	// Swing-twist limits: the twist about the bone's own axis and the swing
	// inside the ellipse of the other two axes' bounds.
	{
		ModelJointLimits swing;
		swing.enabled = true;
		swing.swingTwist = true;
		swing.twistAxis = 0;
		swing.minimum = {-30, -10, -20};
		swing.maximum = {30, 10, 20};
		ModelJointLimits read;
		expect(validModelJointLimits(swing, &error) && modelJointLimitsFromJson(modelJointLimitsJson(swing), &read) && read == swing,
			   "swing-twist limits round-trip through JSON");
		auto off = swing;
		off.minimum[1] = 5;
		auto axis = swing;
		axis.twistAxis = 3;
		auto euler = swing;
		euler.swingTwist = false;
		euler.twistAxis = 2;
		expect(!validModelJointLimits(off) && !validModelJointLimits(axis) && !validModelJointLimits(euler),
			   "swing bounds must include zero and the twist axis must be X, Y or Z");
		const auto turn = [](int which, double degrees)
		{
			const double half = degrees * std::numbers::pi / 360;
			ModelQuat q{0, 0, 0, float(std::cos(half))};
			(which == 0 ? q.x : which == 1 ? q.y : q.z) = float(std::sin(half));
			return q;
		};
		auto skeleton = ikChainFixture(3).skeleton;
		skeleton.joints[1].limits = swing;
		auto pose = modelSkeletonBindPose(skeleton);
		// Twisted 50 degrees about the bone (X), then swung 40 degrees about Z.
		pose[1] = modelJointMatrix(modelQuatMultiply(turn(2, 40), turn(0, 50)), modelJointTranslation(pose[1]));
		pose[2] = modelJointMultiply(pose[1], modelJointMatrix({}, {2, 0, 0}));
		const auto angles = modelJointLimitAngles(skeleton, pose, 1);
		expect(std::abs(angles[0] - 50) < .01 && std::abs(angles[1]) < .01 && std::abs(angles[2] - 40) < .01,
			   "swing-twist angles split the twist from the swing");
		QString problem;
		expect(!modelPoseWithinJointLimits(skeleton, pose, 1e-3, &problem) && !problem.isEmpty(), "the twisted, swung pose is outside the limits");
		pose[1] = modelJointMatrix(modelQuatMultiply(turn(2, 10), turn(0, 25)), modelJointTranslation(pose[1]));
		expect(modelPoseWithinJointLimits(skeleton, pose, 1e-3), "a pose inside the swing ellipse and twist range is within the limits");

		// A chain solve keeps swing-twist joints inside their limits.
		ModelDocument swung;
		expect(swung.setMesh(mesh, &error), "open the chain for swing-twist limits");
		for (const auto *name : {"j0", "j1", "j2", "j3"})
		{
			auto set = limit;
			set.animation.limits.joint = QString::fromLatin1(name);
			set.animation.limits.limits = swing;
			set.animation.limits.limits.minimum = {-5, -10, -25};
			set.animation.limits.limits.maximum = {5, 10, 25};
			expect(swung.edit(set, &error), "set swing-twist limits on a chain joint");
		}
		expect(swung.edit(constrained, &error), "chain IK solves with swing-twist limits");
		bool inside = true;
		for (const auto &solved : swung.mesh().skeleton.clips[0].frames)
			inside = inside && modelPoseWithinJointLimits(swung.mesh().skeleton, solved, 1e-3, &error);
		expect(inside && lengthsKept(swung.mesh(), joints), "swing-twist limits are never exceeded and bone lengths are kept");
		const auto savedSwing = QDir(tmp.path()).filePath("swing.mesh.json");
		ModelDocument reread;
		expect(swung.save(savedSwing, false, &error) && reread.load(savedSwing, &error) &&
				   reread.mesh().skeleton.joints[1].limits == swung.mesh().skeleton.joints[1].limits &&
				   reread.mesh().skeleton.joints[1].limits.swingTwist,
			   "swing-twist limits survive save and reload");
	}

	// Curve protection still applies.
	ModelEdit capture;
	capture.kind = ModelEditKind::EditAnimationCurves;
	capture.curves.clip = 0;
	capture.curves.joint = QStringLiteral("j2");
	ModelDocument keyed;
	expect(keyed.setMesh(mesh, &error) && keyed.edit(capture, &error), "capture curves on a chain joint");
	expect(!keyed.edit(chainIkEdit(joints, {3, 4, 0}, {2, 8, 0}), &error) && error.contains(QStringLiteral("curves")),
		   "a solve that would stale stored curves is refused");

	// A followed joint moves the target sample by sample.
	auto follow = chainIkEdit(joints, {}, {2, 8, 0});
	follow.ik.targetJoint = QStringLiteral("prop");
	follow.ik.targetOffset = {0, -2, 0};
	ModelDocument followed;
	expect(followed.setMesh(mesh, &error) && followed.edit(follow, &error), "solve towards a followed joint");
	bool tracks = true;
	for (int f = 0; f < 3; ++f)
		tracks = tracks && distance(at(followed.mesh(), f, joints - 1), {float(joints), float(4 - f % 3), 0}) < 1e-3;
	expect(tracks, "follow-joint targets track a moving joint");
	auto inChain = follow;
	inChain.ik.targetJoint = QStringLiteral("j3");
	expect(!followed.edit(inChain, &error), "following a joint the chain moves is refused");
	auto twoBone = follow;
	twoBone.kind = ModelEditKind::SolveTwoBoneIk;
	twoBone.ik.rootJoint.clear();
	twoBone.ik.iterations = 32;
	twoBone.ik.endJoint = QStringLiteral("j2");
	twoBone.ik.targetOffset = {-2, -4, 0};
	expect(followed.setMesh(mesh, &error) && followed.edit(twoBone, &error), "two-bone IK follows a joint too");
	auto chainOnly = twoBone;
	chainOnly.ik.limits = true;
	expect(!followed.edit(chainOnly, &error), "chain settings are refused for the two-bone solve");
	auto perSample = chainIkEdit(joints, {}, {2, 8, 0});
	perSample.ik.targets = {{3, 4, 0}, {4, 3, 0}, {5, 2, 0}};
	expect(followed.setMesh(mesh, &error) && followed.edit(perSample, &error) && distance(at(followed.mesh(), 2, joints - 1), {5, 2, 0}) < 1e-3,
		   "per-sample targets place each sample");
	perSample.ik.targets.removeLast();
	expect(!followed.edit(perSample, &error), "per-sample targets must cover the range");

	// A dense chain over 1,023 samples.
	const auto dense = ikChainFixture(10, 1023);
	auto denseEdit = chainIkEdit(10, {8, 9, 0}, {4, 18, 0}, 1022);
	ModelDocument denseDocument;
	QElapsedTimer timer;
	timer.start();
	expect(dense.isValid() && denseDocument.setMesh(dense, &error) && denseDocument.edit(denseEdit, &error), "solve a ten-joint chain over 1,023 samples");
	std::cout << "dense chain solve: " << timer.elapsed() << " ms\n";
	expect(timer.elapsed() < 120000, "the dense solve finishes in reasonable time");

	// CLI.
	const auto source = QDir(tmp.path()).filePath("chain.mesh.json"), out = QDir(tmp.path()).filePath("out.mesh.json");
	ModelDocument chainSource;
	expect(chainSource.setMesh(mesh, &error) && chainSource.save(source, false, &error), "save CLI source");
	auto result = cli::runModelIk({"vibestudio", "--cli", "model", "ik", source, "--clip", "0", "--first", "0", "--last", "2", "--root-joint", "j0",
								   "--end-joint", "j4", "--target", "3,4,0", "--pole", "2,8,0", "--solver", "fabrik", "--iterations", "200", "--output", out, "--json"});
	expect(result.exitCode == 0 && result.payload["solver"].toString() == "fabrik" && result.payload["samples"].toArray().size() == 3 &&
			   result.payload["samples"].toArray()[0].toObject()["reached"].toBool(),
		   "CLI solves a chain");
	result = cli::runModelIk({"vibestudio", "--cli", "model", "ik", source, "--clip", "0", "--first", "0", "--last", "2", "--root-joint", "j0",
							  "--end-joint", "j4", "--target-joint", "prop", "--solver", "ccd", "--iterations", "500", "--output", out, "--overwrite"});
	expect(result.exitCode == 0, "CLI follows a joint with CCD");
	expect(cli::runModelIk({"vibestudio", "--cli", "model", "ik", source, "--clip", "0", "--first", "0", "--last", "2", "--end-joint", "j4",
							"--target", "3,4,0", "--pole", "2,8,0", "--iterations", "4", "--output", out, "--overwrite"})
				   .exitCode == 2,
		   "CLI refuses chain options for the two-bone solve");
	expect(cli::runModelIk({"vibestudio", "--cli", "model", "ik", source, "--clip", "0", "--first", "0", "--last", "2", "--root-joint", "j0",
							"--end-joint", "j4", "--target", "3,4,0", "--target-joint", "prop", "--output", out, "--overwrite"})
				   .exitCode == 2,
		   "CLI needs exactly one target source");
	const auto limitedOut = QDir(tmp.path()).filePath("limits.mesh.json");
	auto rig = cli::runModelRig({"vibestudio", "--cli", "model", "rig", source, "--operation", "limits", "--joint", "j1", "--min", "-5,-5,-20",
								 "--max", "5,5,20", "--output", limitedOut, "--json"});
	expect(rig.exitCode == 0 && rig.payload["joints"].toArray()[1].toObject().contains("limits"), "CLI sets joint limits");
	expect(cli::runModelRig({"vibestudio", "--cli", "model", "rig", source, "--operation", "limits", "--joint", "j1", "--min", "-5,-5,-20",
							 "--output", limitedOut, "--overwrite"})
				   .exitCode == 2,
		   "CLI limits need both bounds");
	const auto swingOut = QDir(tmp.path()).filePath("swing-cli.mesh.json");
	rig = cli::runModelRig({"vibestudio", "--cli", "model", "rig", source, "--operation", "limits", "--joint", "j1", "--min", "-30,-10,-20",
							"--max", "30,10,20", "--swing-twist", "x", "--output", swingOut, "--json"});
	expect(rig.exitCode == 0 &&
			   rig.payload["joints"].toArray()[1].toObject()["limits"].toObject()["mode"].toString() == QStringLiteral("swing-twist") &&
			   rig.payload["joints"].toArray()[1].toObject()["limits"].toObject()["twistAxis"].toInt() == 0,
		   "CLI sets swing-twist limits");
	expect(cli::runModelRig({"vibestudio", "--cli", "model", "rig", source, "--operation", "limits", "--joint", "j1", "--min", "-30,-10,-20",
							 "--max", "30,10,20", "--swing-twist", "w", "--output", swingOut, "--overwrite"})
				   .exitCode == 2,
		   "CLI swing-twist needs an axis of x, y or z");
	expect(cli::runModelRig({"vibestudio", "--cli", "model", "rig", source, "--operation", "limits", "--joint", "j1", "--min", "5,5,5", "--max",
							 "30,10,20", "--swing-twist", "x", "--output", swingOut, "--overwrite"})
				   .exitCode != 0,
		   "CLI refuses swing bounds that exclude the bind pose");
	result = cli::runModelIk({"vibestudio", "--cli", "model", "ik", limitedOut, "--clip", "0", "--first", "0", "--last", "2", "--root-joint", "j0",
							  "--end-joint", "j4", "--target", "3,4,0", "--pole", "2,8,0", "--limits", "--nearest-reachable", "--output", out,
							  "--overwrite", "--json"});
	expect(result.exitCode == 0 && result.payload["withinLimits"].toBool() && result.payload["limits"].toBool(), "CLI honours joint limits");
	if (argc > 1)
	{
		QProcess process;
		process.start(QString::fromLocal8Bit(argv[1]), {"--cli", "model", "ik", source, "--clip", "0", "--first", "0", "--last", "2", "--root-joint",
														"j0", "--end-joint", "j4", "--target", "3,4,0", "--output", out, "--overwrite", "--dry-run", "--json"});
		expect(process.waitForFinished(60000) && process.exitCode() == 0 &&
				   QJsonDocument::fromJson(process.readAllStandardOutput()).object()["rootJoint"].toString() == "j0",
			   "application dispatches chain IK");
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
