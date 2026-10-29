#include "cli/model_tools.h"
#include "core/model_animation_constraints.h"
#include "core/model_curve_key_tools.h"
#include "core/model_rotation_tracks.h"
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
using Q = std::array<double, 4>;
Q aboutZ(double degrees)
{
	const double half = degrees * std::numbers::pi / 360;
	return {0, 0, std::sin(half), std::cos(half)};
}
ModelPoseQuatKey quatKey(int frame, const Q &value, ModelCurveInterpolation mode = ModelCurveInterpolation::Linear)
{
	ModelPoseQuatKey key;
	key.frame = frame;
	key.value = value;
	key.interpolation = mode;
	return key;
}
ModelPoseCurveKey key(int frame, double value)
{
	ModelPoseCurveKey k;
	k.frame = frame;
	k.value = value;
	return k;
}
// A group with constant translation and scale and the given rotation channels.
ModelPoseCurves eulerGroup(const QString &joint, const QVector<ModelPoseCurveKey> &rx, const QVector<ModelPoseCurveKey> &ry,
						   const QVector<ModelPoseCurveKey> &rz)
{
	ModelPoseCurves group;
	group.joint = joint;
	for (int c = 0; c < 9; ++c)
		group.channels[c] = {key(0, c >= 6 ? 1.0 : 0.0)};
	group.channels[3] = rx;
	group.channels[4] = ry;
	group.channels[5] = rz;
	return group;
}
Q rotationAt(const ModelPoseCurves &group, double frame)
{
	const auto r = modelJointRotation(evaluateModelPoseCurves(group, frame));
	return {r.x, r.y, r.z, r.w};
}
QJsonObject runJson(const QStringList &args, int *code)
{
	const auto result = cli::runModelCurves(args);
	*code = result.exitCode;
	return result.exitCode == 0 ? result.payload : QJsonObject{};
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir tmp(QDir(root).filePath("rotation-tracks-XXXXXX"));
	if (!tmp.isValid())
		return 1;
	QString error;

	// Euler and quaternion conversions agree with the curves' X-then-Y-then-Z order.
	const std::array<double, 3> angles{10, 20, 30};
	const auto turned = modelQuatFromEulerDegrees(angles);
	const auto back = modelEulerDegreesFromQuat(turned);
	expect(std::abs(back[0] - 10) < 1e-3 && std::abs(back[1] - 20) < 1e-3 && std::abs(back[2] - 30) < 1e-3, "Euler degrees round-trip through a quaternion");
	const auto group0 = eulerGroup("j", {key(0, 10)}, {key(0, 20)}, {key(0, 30)});
	expect(modelQuatAngleDegrees(rotationAt(group0, 0), turned) < 1e-3, "the quaternion matches the curves' Euler rotation");
	const std::array<double, 3> previous{370, 20, 30};
	const auto unwrapped = modelEulerDegreesFromQuat(turned, &previous);
	expect(std::abs(unwrapped[0] - 370) < 1e-3, "the angles nearest a previous key keep a track continuous");

	// Sampling: slerp along the shorter arc, steps, squad.
	QVector<ModelPoseQuatKey> track{quatKey(0, aboutZ(0)), quatKey(10, aboutZ(90))};
	expect(validateModelQuatTrack(track, 11, &error), "a two-key track is valid");
	expect(modelQuatAngleDegrees(sampleModelQuatTrack(track, 5), aboutZ(45)) < 1e-6 &&
			   modelQuatAngleDegrees(sampleModelQuatTrack(track, 10), aboutZ(90)) < 1e-9 &&
			   modelQuatAngleDegrees(sampleModelQuatTrack(track, 20), aboutZ(90)) < 1e-9,
		   "linear keys slerp and hold the end key");
	track[0].interpolation = ModelCurveInterpolation::Step;
	expect(modelQuatAngleDegrees(sampleModelQuatTrack(track, 9.5), aboutZ(0)) < 1e-9, "step holds the key");
	track[0].interpolation = ModelCurveInterpolation::StepNext;
	expect(modelQuatAngleDegrees(sampleModelQuatTrack(track, .5), aboutZ(90)) < 1e-9 &&
			   modelQuatAngleDegrees(sampleModelQuatTrack(track, 0), aboutZ(0)) < 1e-9,
		   "hold-next takes the next key, except at its own key time");
	track = {quatKey(0, aboutZ(0)), quatKey(10, aboutZ(270))};
	expect(modelQuatAngleDegrees(sampleModelQuatTrack(track, 5), aboutZ(-45)) < 1e-6, "slerp takes the shorter arc");
	track = {quatKey(0, aboutZ(0), ModelCurveInterpolation::Cubic), quatKey(10, aboutZ(60), ModelCurveInterpolation::Cubic),
			 quatKey(20, aboutZ(90), ModelCurveInterpolation::Cubic)};
	bool planar = true, monotonic = true;
	double last = -1;
	for (int f = 0; f <= 20; ++f)
	{
		const auto q = sampleModelQuatTrack(track, f);
		planar = planar && std::abs(q[0]) < 1e-9 && std::abs(q[1]) < 1e-9;
		const double angle = modelQuatAngleDegrees(q, aboutZ(0));
		monotonic = monotonic && angle >= last - 1e-9;
		last = angle;
	}
	expect(planar && monotonic && modelQuatAngleDegrees(sampleModelQuatTrack(track, 10), aboutZ(60)) < 1e-9,
		   "squad stays on the rotation's plane, passes through its keys and keeps turning one way");
	auto bad = track;
	bad[1].value = {0, 0, 0, 2};
	expect(!validateModelQuatTrack(bad, 21) && !validateModelQuatTrack(track, 15), "non-unit keys and keys past the clip are refused");

	// Converting between Euler channels and quaternion keys.
	auto zOnly = eulerGroup("l_arm", {key(0, 0)}, {key(0, 0)}, {key(0, 0), key(3, 90)});
	auto converted = zOnly;
	double change = -1;
	expect(convertModelPoseCurvesRotation(&converted, true, ModelRotationKeyTimes::Keys, 4, &change, &error) && converted.rotation.size() == 2 &&
			   converted.channels[5] == QVector<ModelPoseCurveKey>{key(0, 0)} && change < 1e-3,
		   "a single-axis turn converts to two slerp keys without changing any sample");
	expect(!convertModelPoseCurvesRotation(&converted, true, ModelRotationKeyTimes::Keys, 4, nullptr, &error), "converting twice is refused");
	auto twoAxes = eulerGroup("l_arm", {key(0, 0), key(3, 90)}, {key(0, 0)}, {key(0, 0), key(3, 90)});
	auto sparse = twoAxes, dense = twoAxes;
	double sparseChange = 0, denseChange = -1;
	expect(convertModelPoseCurvesRotation(&sparse, true, ModelRotationKeyTimes::Keys, 4, &sparseChange, &error) && sparseChange > .1 &&
			   convertModelPoseCurvesRotation(&dense, true, ModelRotationKeyTimes::EverySample, 4, &denseChange, &error) && dense.rotation.size() == 4 &&
			   denseChange < 1e-3,
		   "two-axis Euler motion converted at its keys is reported; every sample keeps it exactly");
	auto euler = dense;
	expect(convertModelPoseCurvesRotation(&euler, false, ModelRotationKeyTimes::Keys, 4, &change, &error) && euler.rotation.isEmpty() &&
			   euler.channels[3].size() == 4 && change < 1e-3,
		   "quaternion keys convert back to Euler keys at their samples");
	bool samples = true;
	for (int f = 0; f < 4; ++f)
		samples = samples && modelQuatAngleDegrees(rotationAt(euler, f), rotationAt(twoAxes, f)) < 1e-3;
	expect(samples, "the round trip keeps every sample's rotation");
	auto edited = converted;
	expect(setModelQuatKeyAxis(&edited, 1, 2, 45, ModelCurveInterpolation::Linear, 4, &error) && edited.rotation.size() == 3 &&
			   std::abs(modelEulerDegreesFromQuat(sampleModelQuatTrack(edited.rotation, 1))[2] - 45) < 1e-3,
		   "setting one axis of the angle view adds a quaternion key");
	expect(removeModelQuatKey(&edited, 1, &error) && edited.rotation.size() == 2 && !removeModelQuatKey(&edited, 1, &error),
		   "remove a quaternion key once");
	auto single = edited;
	expect(removeModelQuatKey(&single, 3, &error) && !removeModelQuatKey(&single, 0, &error), "a track keeps at least one key");
	auto placeholder = converted;
	placeholder.channels[4] = {key(0, 5)};
	expect(!validateModelCurveGroup(placeholder, 4, &error), "rotation channels of a quaternion joint keep their zero placeholders");

	// The document edit, saving and source version 15.
	const auto mesh = symmetricRigFixture();
	ModelEdit capture;
	capture.kind = ModelEditKind::EditAnimationCurves;
	capture.curves.clip = 0;
	capture.curves.joint = "l_arm";
	ModelDocument document;
	expect(document.setMesh(mesh, &error) && document.edit(capture, &error), "capture the arm's curves");
	auto group = document.mesh().skeleton.clips[0].curves[0];
	const auto original = document.mesh().skeleton.clips[0].frames;
	expect(convertModelPoseCurvesRotation(&group, true, ModelRotationKeyTimes::Keys, 4, &change, &error) && change < 1e-3,
		   "the captured arm converts to quaternion keys");
	ModelEdit replace = capture;
	replace.curves.action = ModelCurveAction::Replace;
	replace.curves.group = group;
	expect(document.edit(replace, &error), "replace the arm's curves with quaternion keys");
	bool kept = true;
	for (int f = 0; f < 4; ++f)
		kept = kept && modelJointMatrixDistance(document.mesh().skeleton.clips[0].frames[f][2], original[f][2]) < 1e-4;
	expect(kept, "the samples stay as they were");
	const auto saved = QDir(tmp.path()).filePath("quaternion.mesh.json");
	ModelDocument reopened;
	expect(document.save(saved, false, &error) && reopened.load(saved, &error) &&
			   reopened.mesh().skeleton.clips[0].curves[0].rotation == document.mesh().skeleton.clips[0].curves[0].rotation,
		   "quaternion keys survive save and reload");
	QFile file(saved);
	QByteArray bytes;
	if (file.open(QIODevice::ReadOnly))
		bytes = file.readAll();
	file.close();
	expect(QJsonDocument::fromJson(bytes).object().value("version").toInt() == 15 && bytes.contains("\"rotation\""),
		   "quaternion keys write source version 15 with an optional rotation key");

	// Edits that do not handle quaternion keys yet refuse them clearly.
	auto setRz = capture;
	setRz.curves.action = ModelCurveAction::SetKey;
	setRz.curves.channel = 5;
	setRz.curves.frame = 1;
	setRz.curves.key = key(1, 10);
	expect(!document.edit(setRz, &error) && error.contains("quaternion"), "Euler rotation key edits refuse a quaternion joint");
	auto setTx = setRz;
	setTx.curves.channel = 0;
	setTx.curves.key = key(1, .5);
	expect(document.edit(setTx, &error), "translation keys still edit");
	ModelEdit retime;
	retime.kind = ModelEditKind::EditCurveKeys;
	retime.curveKeys.action = ModelCurveKeyAction::Remove;
	retime.curveKeys.clip = 0;
	retime.curveKeys.selection = {{"l_arm", 5, {0}}};
	expect(!document.edit(retime, &error) && error.contains("quaternion"), "key tools refuse quaternion rotation channels");
	ModelPoseCopyOptions copyKeys;
	copyKeys.clip = 0;
	copyKeys.joints = {"l_arm"};
	copyKeys.keys = true;
	ModelPoseClipboard clipboard;
	expect(!copyModelPose(document.mesh(), copyKeys, &clipboard, &error), "key copies refuse quaternion rotation channels");
	copyKeys.channels = {0, 1, 2};
	expect(copyModelPose(document.mesh(), copyKeys, &clipboard, &error), "translation keys of a quaternion joint copy");
	// A pasted pose sets a quaternion key.
	ModelDocument pasted;
	expect(pasted.setMesh(document.mesh(), &error) && pasted.edit(pastePoseEdit(copyPoseBytes(mesh, 0, 3, 3, {"l_arm"}), 0, 1), &error) == false,
		   "pasting over existing keys is refused without replace");
	auto replacing = pastePoseEdit(copyPoseBytes(mesh, 0, 3, 3, {"l_arm"}), 0, 1);
	replacing.animation.paste.replaceKeys = true;
	expect(pasted.edit(replacing, &error), "a pose pastes onto a quaternion joint");
	const auto &pastedGroup = pasted.mesh().skeleton.clips[0].curves[0];
	expect(!pastedGroup.rotation.isEmpty() &&
			   modelQuatAngleDegrees(sampleModelQuatTrack(pastedGroup.rotation, 1), aboutZ(60)) < 1e-3,
		   "the pasted pose becomes the rotation key at that sample");
	// Layers keep Euler channels.
	auto layered = document.mesh();
	ModelEdit layer;
	layer.kind = ModelEditKind::EditClipStack;
	layer.animation.stack.action = ModelClipStackAction::AddLayer;
	layer.animation.stack.clip = 0;
	layer.animation.stack.layer.name = "turn";
	layer.animation.stack.layer.curves = {group};
	expect(!applyModelEdit(&layered, layer, nullptr, &error), "layers refuse quaternion rotation keys");

	// CLI parity.
	const auto source = QDir(tmp.path()).filePath("keyed.mesh.json"), out = QDir(tmp.path()).filePath("out.mesh.json");
	ModelDocument keyed;
	expect(keyed.setMesh(mesh, &error) && keyed.edit(capture, &error) && keyed.save(source, false, &error), "save a keyed source");
	int code = 0;
	auto result = runJson({"vibestudio", "--cli", "model", "curves", source, "--clip", "0", "--joint", "l_arm", "--operation", "convert-rotation", "--to",
						   "quaternion", "--output", out, "--json"},
						  &code);
	expect(code == 0 && result["curves"].toArray()[0].toObject()["rotation"].toArray().size() == 4 && result["rotationChange"].toDouble() < 1e-3,
		   "CLI converts a joint's rotation to quaternion keys");
	result = runJson({"vibestudio", "--cli", "model", "curves", out, "--clip", "0", "--joint", "l_arm", "--operation", "set-rotation-key", "--frame", "2",
					  "--rotate", "0,0,45", "--interpolation", "cubic", "--output", out, "--overwrite", "--json"},
					 &code);
	const auto keys = result["curves"].toArray()[0].toObject()["rotation"].toArray();
	expect(code == 0 && keys.size() == 4 && keys[2].toArray()[5].toString() == "cubic" &&
			   std::abs(keys[2].toArray()[3].toDouble() - std::sin(45 * std::numbers::pi / 360)) < 1e-6,
		   "CLI sets a quaternion key from degrees");
	expect(runJson({"vibestudio", "--cli", "model", "curves", out, "--clip", "0", "--joint", "l_arm", "--operation", "set-rotation-key", "--frame", "2",
					"--quat", "0,0,0,0", "--output", out, "--overwrite"},
				   &code)
				   .isEmpty() &&
			   code == 2,
		   "CLI refuses a zero quaternion");
	result = runJson({"vibestudio", "--cli", "model", "curves", out, "--clip", "0", "--joint", "l_arm", "--operation", "remove-rotation-key", "--frame", "1",
					  "--output", out, "--overwrite", "--json"},
					 &code);
	expect(code == 0 && result["curves"].toArray()[0].toObject()["rotation"].toArray().size() == 3, "CLI removes a quaternion key");
	result = runJson({"vibestudio", "--cli", "model", "curves", out, "--clip", "0", "--joint", "l_arm", "--operation", "convert-rotation", "--to", "euler",
					  "--every-sample", "--output", out, "--overwrite", "--json"},
					 &code);
	expect(code == 0 && !result["curves"].toArray()[0].toObject().contains("rotation") && result["rotationChange"].toDouble() < 1e-3,
		   "CLI converts back to Euler channels with a key at every sample");
	expect(runJson({"vibestudio", "--cli", "model", "curves", out, "--clip", "0", "--joint", "l_arm", "--operation", "convert-rotation", "--to", "sideways",
					"--output", out, "--overwrite"},
				   &code)
				   .isEmpty() &&
			   code == 2,
		   "CLI refuses an unknown rotation form");
	if (argc > 1)
	{
		QProcess process;
		process.start(QString::fromLocal8Bit(argv[1]), {"--cli", "model", "curves", source, "--clip", "0", "--joint", "l_arm", "--operation",
														"convert-rotation", "--to", "quaternion", "--output", out, "--overwrite", "--dry-run", "--json"});
		expect(process.waitForFinished(60000) && process.exitCode() == 0 &&
				   QJsonDocument::fromJson(process.readAllStandardOutput()).object()["operation"].toString() == "convert-rotation",
			   "application dispatches rotation conversion");
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
