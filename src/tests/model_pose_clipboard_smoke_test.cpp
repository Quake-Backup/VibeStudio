#include "cli/model_tools.h"
#include "tests/model_pose_clipboard_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
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
ModelVec3 origin(const ModelMesh &mesh, int clip, int frame, int joint)
{
	return modelJointTranslation(mesh.skeleton.clips[clip].frames[frame][joint]);
}
bool samePoses(const QVector<ModelJointMatrix> &a, const QVector<ModelJointMatrix> &b)
{
	if (a.size() != b.size())
		return false;
	for (int i = 0; i < a.size(); ++i)
		if (modelJointMatrixDistance(a[i], b[i]) != 0)
			return false;
	return true;
}
ModelEdit addClip(const QString &name, int frames)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::AddSkeletalClip;
	edit.skeletal.name = name;
	edit.skeletal.frameCount = frames;
	return edit;
}
QJsonObject runJson(const QStringList &arguments, int *code)
{
	const auto result = cli::runModelPose(arguments);
	*code = result.exitCode;
	return result.payload;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir tmp(QDir(root).filePath("pose-clipboard-XXXXXX"));
	if (!tmp.isValid())
		return 1;
	QString error;
	const auto mesh = symmetricRigFixture();
	if (!expect(mesh.isValid() && mesh.skeleton.clips.size() == 1, "symmetric rig fixture builds"))
		return 1;

	// Clipboard payload.
	ModelPoseCopyOptions copy;
	copy.clip = 0;
	copy.first = 1;
	copy.last = 2;
	ModelPoseClipboard clipboard;
	expect(copyModelPose(mesh, copy, &clipboard, &error) && clipboard.samples == 2 && clipboard.joints.size() == 6 &&
			   clipboard.local.size() == 2 && clipboard.world.size() == 2,
		   "copy captures every joint over the sample range");
	const auto bytes = modelPoseClipboardBytes(clipboard);
	ModelPoseClipboard parsed;
	expect(parseModelPoseClipboard(bytes, &parsed, &error) && modelPoseClipboardBytes(parsed) == bytes,
		   "clipboard JSON round-trips byte for byte");
	expect(modelPoseClipboardFingerprint(parsed) == modelRigFingerprint(mesh.skeleton), "clipboard identifies its rig");
	auto broken = bytes;
	broken.replace("\"version\":1", "\"version\":9");
	expect(!parseModelPoseClipboard(broken, &parsed, &error) && !parseModelPoseClipboard("{}", &parsed, &error) &&
			   !parseModelPoseClipboard(bytes.left(bytes.size() / 2), &parsed, &error),
		   "unsupported or truncated clipboards are refused");
	copy.joints = {"r_arm", "spine"};
	expect(copyModelPose(mesh, copy, &clipboard, &error) && clipboard.joints == QStringList({"spine", "r_arm"}),
		   "a joint subset is copied in rig order");
	copy.joints = {"nope"};
	expect(!copyModelPose(mesh, copy, &clipboard, &error), "unknown joints are refused");

	// Same-document round trip into a new clip.
	ModelDocument document;
	expect(document.setMesh(mesh, &error) && document.edit(addClip("dest", 4), &error), "destination clip added");
	const auto beforePaste = rigBytes(document.mesh());
	const auto whole = copyPoseBytes(mesh, 0, 1, 2);
	expect(document.edit(pastePoseEdit(whole, 1, 1), &error), "paste a two-sample pose range");
	const auto pasted = document.mesh();
	expect(samePoses(pasted.skeleton.clips[1].frames[1], mesh.skeleton.clips[0].frames[1]) &&
			   samePoses(pasted.skeleton.clips[1].frames[2], mesh.skeleton.clips[0].frames[2]),
		   "a whole-pose round trip on the same rig is exact");
	expect(samePoses(pasted.skeleton.clips[1].frames[0], modelSkeletonBindPose(mesh.skeleton)) &&
			   samePoses(pasted.skeleton.clips[1].frames[3], modelSkeletonBindPose(mesh.skeleton)),
		   "samples outside the pasted range keep their poses");
	const int destFirst = pasted.skeleton.bakedClipForFrame.indexOf(1), srcFirst = pasted.skeleton.bakedClipForFrame.indexOf(0);
	expect(near(pasted.surfaces[0].frames[destFirst + 2].positions[3], mesh.surfaces[0].frames[srcFirst + 2].positions[3]),
		   "baked skin follows the pasted pose");
	expect(document.undo() && rigBytes(document.mesh()) == beforePaste && document.redo() && rigBytes(document.mesh()) == rigBytes(pasted),
		   "paste is one undo and redo step");
	auto overflow = pastePoseEdit(whole, 1, 3);
	expect(!document.edit(overflow, &error) && rigBytes(document.mesh()) == rigBytes(pasted), "a paste past the clip end is refused atomically");
	auto mixed = pastePoseEdit(whole, 1, 0);
	mixed.skeletal.name = "unrelated";
	expect(!document.edit(mixed, &error), "unrelated edit settings are refused");
	ModelEdit stray;
	stray.kind = ModelEditKind::Transform;
	stray.animation.paste.clip = 0;
	expect(!document.edit(stray, &error), "paste settings on another edit are refused");

	// A subset pastes as parent-local poses; descendants follow.
	const auto arm = copyPoseBytes(mesh, 0, 3, 3, {"l_arm"});
	expect(document.edit(pastePoseEdit(arm, 1, 0), &error), "paste one joint's pose");
	expect(near(origin(document.mesh(), 1, 0, 3), origin(mesh, 0, 3, 3)), "the child follows its pasted parent");

	// Another document with the same rig, then a renamed rig.
	ModelDocument other;
	expect(other.setMesh(mesh, &error) && other.edit(pastePoseEdit(copyPoseBytes(pasted, 1, 1, 1), 0, 3), &error) &&
			   samePoses(other.mesh().skeleton.clips[0].frames[3], mesh.skeleton.clips[0].frames[1]),
		   "a copy pastes into another document with an identical rig");
	ModelEdit rename;
	rename.kind = ModelEditKind::RenameJoint;
	rename.rig.joint = "l_hand";
	rename.rig.name = "l_palm";
	ModelDocument renamed;
	expect(renamed.setMesh(mesh, &error) && renamed.edit(rename, &error), "rename a joint in another rig");
	const auto renamedBytes = rigBytes(renamed.mesh());
	expect(!renamed.edit(pastePoseEdit(whole, 0, 0), &error) && rigBytes(renamed.mesh()) == renamedBytes,
		   "a copy from a different rig is refused without a joint map");
	auto mapped = pastePoseEdit(whole, 0, 0);
	mapped.animation.paste.joints = {{"l_hand", "l_palm"}};
	expect(renamed.edit(mapped, &error) && samePoses(renamed.mesh().skeleton.clips[0].frames[0], mesh.skeleton.clips[0].frames[1]),
		   "a joint map pastes across renamed rigs");
	auto badMap = mapped;
	badMap.animation.paste.joints = {{"l_hand", "missing"}};
	expect(!renamed.edit(badMap, &error), "a map naming missing joints is refused");

	// Mirroring a half cycle onto the other side.
	QMap<QString, QString> pairs;
	expect(detectModelPoseMirrorAxis(mesh, &pairs) == 1 && pairs.value("l_arm") == "r_arm" && pairs.value("r_hand") == "l_hand",
		   "left/right pairs and the Y mirror axis are detected");
	ModelDocument mirrored;
	auto mirror = pastePoseEdit(copyPoseBytes(mesh, 0, 0, 3), 0, 0);
	mirror.animation.paste.mirrorAxis = 1;
	mirror.animation.paste.mirrorJoints = pairs;
	expect(mirrored.setMesh(mesh, &error) && mirrored.edit(mirror, &error), "paste a range mirrored in place");
	bool reflected = true;
	for (int f = 0; f < 4; ++f)
	{
		const auto left = origin(mesh, 0, f, 3), right = origin(mesh, 0, f, 5);
		reflected = reflected && near(origin(mirrored.mesh(), 0, f, 5), {left.x, -left.y, left.z}) &&
					near(origin(mirrored.mesh(), 0, f, 3), {right.x, -right.y, right.z});
	}
	expect(reflected, "mirrored joints land on the reflected partner positions");
	auto unpaired = mirror;
	unpaired.animation.paste.mirrorJoints.clear();
	expect(!mirrored.edit(unpaired, &error), "an off-plane joint without a partner is refused");
	auto keysMirror = mirror;
	keysMirror.animation.paste.clipboard = copyPoseBytes(mesh, 0, 0, 3, {"l_arm"}, true);
	expect(!mirrored.edit(keysMirror, &error), "mirroring keys is refused");

	// Keys: collisions are refused unless replacing.
	ModelEdit capture;
	capture.kind = ModelEditKind::EditAnimationCurves;
	capture.curves.clip = 0;
	capture.curves.joint = "l_arm";
	ModelDocument keyed;
	expect(keyed.setMesh(mesh, &error) && keyed.edit(capture, &error) && keyed.mesh().skeleton.clips[0].curves.size() == 1, "capture curves");
	const auto keyedBytes = rigBytes(keyed.mesh());
	auto collide = pastePoseEdit(copyPoseBytes(mesh, 0, 3, 3, {"l_arm"}), 0, 1);
	expect(!keyed.edit(collide, &error) && rigBytes(keyed.mesh()) == keyedBytes, "a pose paste onto existing keys is refused");
	collide.animation.paste.replaceKeys = true;
	expect(keyed.edit(collide, &error) && near(origin(keyed.mesh(), 0, 1, 3), origin(mesh, 0, 3, 3)),
		   "replacing keys sets the pasted pose as keys");
	expect(keyed.mesh().skeleton.clips[0].curves.size() == 1 && keyed.mesh().skeleton.clips[0].curves[0].channels[5][1].value > 50,
		   "the rotation key holds the pasted angle");
	ModelDocument keys;
	expect(keys.setMesh(document.mesh(), &error), "reset key destination");
	const auto keyCopy = copyPoseBytes(keyed.mesh(), 0, 0, 3, {"l_arm"}, true);
	expect(!keyCopy.isEmpty() && keys.edit(pastePoseEdit(keyCopy, 1, 0), &error), "paste keys into an unkeyed joint");
	expect(keys.mesh().skeleton.clips[1].curves.size() == 1 && near(origin(keys.mesh(), 1, 1, 3), origin(keyed.mesh(), 0, 1, 3)),
		   "pasted keys reproduce the copied motion");
	const auto keysBytes = rigBytes(keys.mesh());
	expect(!keys.edit(pastePoseEdit(keyCopy, 1, 0), &error) && rigBytes(keys.mesh()) == keysBytes, "pasting onto authored keys is refused");
	auto replace = pastePoseEdit(keyCopy, 1, 0);
	replace.animation.paste.replaceKeys = true;
	expect(keys.edit(replace, &error), "replace keys inside the range");

	// CLI.
	const auto source = QDir(tmp.path()).filePath("source.mesh.json");
	ModelDocument saved;
	expect(saved.setMesh(mesh, &error) && saved.edit(addClip("dest", 4), &error) && saved.save(source, false, &error), "save CLI source");
	const auto poseFile = QDir(tmp.path()).filePath("pose.json"), out = QDir(tmp.path()).filePath("out.mesh.json");
	int code = 0;
	auto payload = runJson({"vibestudio", "--cli", "model", "pose", source, "--operation", "copy-out", "--clip", "0", "--first", "1", "--last", "2", "--output", poseFile}, &code);
	expect(code == 0 && QFile::exists(poseFile) && payload["samples"].toInt() == 2 && payload["kind"].toString() == "pose", "CLI copy-out writes a pose file");
	expect(runJson({"vibestudio", "--cli", "model", "pose", source, "--operation", "copy-out", "--clip", "0", "--first", "1", "--output", poseFile}, &code).isEmpty() && code == 1,
		   "CLI copy-out keeps overwrites explicit");
	payload = runJson({"vibestudio", "--cli", "model", "pose", source, "--operation", "paste", "--clipboard", poseFile, "--clip", "1", "--first", "1", "--output", out}, &code);
	ModelDocument reopened;
	expect(code == 0 && reopened.load(out, &error) && samePoses(reopened.mesh().skeleton.clips[1].frames[2], mesh.skeleton.clips[0].frames[2]),
		   "CLI paste applies the copy");
	payload = runJson({"vibestudio", "--cli", "model", "pose", source, "--operation", "mirror", "--clip", "0", "--first", "0", "--last", "3", "--output", out, "--overwrite"}, &code);
	expect(code == 0 && payload["mirror"].toString() == "y" && payload["mirrorJoints"].toObject().size() == 4 && reopened.load(out, &error) &&
			   near(origin(reopened.mesh(), 0, 2, 5), {origin(mesh, 0, 2, 3).x, -origin(mesh, 0, 2, 3).y, origin(mesh, 0, 2, 3).z}),
		   "CLI mirror detects the axis and pairs");
	expect(runJson({"vibestudio", "--cli", "model", "pose", source, "--operation", "paste", "--clipboard", poseFile, "--clip", "1", "--first", "3", "--output", out, "--overwrite"}, &code).isEmpty() && code == 4,
		   "CLI paste refuses a range past the clip end");
	expect(runJson({"vibestudio", "--cli", "model", "pose", source, "--operation", "paste", "--clipboard", poseFile, "--clip", "1", "--first", "0", "--output", source, "--overwrite"}, &code).isEmpty() && code == 1,
		   "CLI paste refuses to overwrite its input");
	expect(runJson({"vibestudio", "--cli", "model", "pose", source, "--operation", "copy-out", "--clip", "0", "--first", "0", "--replace-keys", "--output", poseFile, "--overwrite"}, &code).isEmpty() && code == 2,
		   "CLI refuses inapplicable flags");
	// The shared key splice: the pasted span replaces the keys inside it.
	{
		const auto key = [](int frame, double value)
		{
			ModelPoseCurveKey k;
			k.frame = frame;
			k.value = value;
			return k;
		};
		const QVector<ModelPoseCurveKey> destination{key(0, 0), key(5, 5), key(10, 10)}, copied{key(0, 100), key(2, 102)};
		QVector<ModelPoseCurveKey> merged;
		QString spliceError;
		expect(spliceModelCurveKeys(destination, copied, 2, 4, 11, &merged, &spliceError) && merged.size() == 4 && merged[0].frame == 0 &&
				   merged[1].frame == 4 && merged[1].value == 100 && merged[2].frame == 6 && merged[2].value == 102 && merged[3].frame == 10,
			   "the splice replaces the keys inside the pasted span and keeps the rest");
		expect(spliceModelCurveKeys(destination, copied, 1, 4, 11, &merged) && merged.size() == 4 && merged[2].frame == 5 &&
				   std::abs(merged[2].value - 101) < 1e-9,
			   "a shorter span crops the copied curve with its exact boundary value");
		expect(!spliceModelCurveKeys(destination, copied, 2, 9, 11, &merged, &spliceError) && !spliceError.isEmpty(),
			   "a span past the clip end is refused");
	}
	if (argc > 1)
	{
		QProcess process;
		process.start(QString::fromLocal8Bit(argv[1]), {"--cli", "model", "pose", source, "--operation", "paste", "--clipboard", poseFile, "--clip", "1", "--first", "0", "--output", out, "--overwrite", "--dry-run", "--json"});
		expect(process.waitForFinished(30000) && process.exitCode() == 0 &&
				   QJsonDocument::fromJson(process.readAllStandardOutput()).object()["dryRun"].toBool(),
			   "application dispatches pose paste");
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
