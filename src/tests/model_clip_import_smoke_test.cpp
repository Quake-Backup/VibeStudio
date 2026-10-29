#include "cli/model_tools.h"
#include "core/model_clip_import.h"
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
bool samePoses(const QVector<QVector<ModelJointMatrix>> &a, const QVector<QVector<ModelJointMatrix>> &b, double tolerance)
{
	if (a.size() != b.size())
		return false;
	for (int f = 0; f < a.size(); ++f)
	{
		if (a[f].size() != b[f].size())
			return false;
		for (int j = 0; j < a[f].size(); ++j)
			if (modelJointMatrixDistance(a[f][j], b[f][j]) > tolerance)
				return false;
	}
	return true;
}
bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
ModelEdit importEdit(const ModelMesh &source, const QString &name, const QString &path = {})
{
	ModelEdit edit;
	edit.kind = ModelEditKind::ImportSkeletalClip;
	edit.animation.import.name = name;
	modelClipImportFromSource(source, 0, path, &edit.animation.import);
	return edit;
}
ModelJointMatrix local(const ModelMesh &mesh, int clip, int frame, int joint)
{
	const auto &pose = mesh.skeleton.clips[clip].frames[frame];
	const int parent = mesh.skeleton.joints[joint].parent;
	return parent < 0 ? pose[joint] : modelJointMultiply(modelJointInverse(pose[parent]), pose[joint]);
}
double length(const ModelMesh &mesh, int clip, int frame, int joint)
{
	const auto &pose = mesh.skeleton.clips[clip].frames[frame];
	const auto a = modelJointTranslation(pose[joint]), b = modelJointTranslation(pose[mesh.skeleton.joints[joint].parent]);
	return std::sqrt(double(a.x - b.x) * (a.x - b.x) + double(a.y - b.y) * (a.y - b.y) + double(a.z - b.z) * (a.z - b.z));
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir tmp(QDir(root).filePath("clip-import-XXXXXX"));
	if (!tmp.isValid())
		return 1;
	QString error;
	const auto mesh = symmetricRigFixture();
	const auto &walk = mesh.skeleton.clips[0].frames;

	// A same-rig md5anim reproduces the source poses.
	ModelExportOptions exportOptions;
	exportOptions.clip = 0;
	const auto md5anim = exportEditableModel(mesh, "md5anim", exportOptions, &error);
	const auto animPath = QDir(tmp.path()).filePath("walk.md5anim");
	expect(!md5anim.isEmpty() && write(animPath, md5anim), "export the walk as an md5anim");
	const auto decoded = decodeModelMeshFile(animPath);
	expect(decoded.error.isEmpty() && decoded.skeleton.clips.size() == 1, "decode the md5anim");
	ModelDocument document;
	auto edit = importEdit(decoded, "walk_again", animPath);
	ModelClipImportPlan plan;
	expect(planModelClipImport(mesh, edit.animation.import, &plan, &error) && plan.samples == 4 && plan.unusedSourceJoints.isEmpty(),
		   "the plan maps every joint");
	expect(document.setMesh(mesh, &error) && document.edit(edit, &error), "import the md5anim clip");
	const auto &imported = document.mesh().skeleton.clips.last();
	expect(imported.name == "walk_again" && imported.framesPerSecond == 12 && imported.sourcePath.endsWith("walk.md5anim") &&
			   samePoses(imported.frames, walk, 2e-4),
		   "a same-rig md5anim import reproduces the source poses");
	expect(document.mesh().skeleton.bakedClipForFrame.contains(1), "the imported clip joins the working set");
	expect(document.undo() && document.mesh().skeleton.clips.size() == 1 && document.redo() && document.mesh().skeleton.clips.size() == 2,
		   "the import is one undo step");
	const auto saved = QDir(tmp.path()).filePath("imported.mesh.json");
	ModelDocument reopened;
	expect(document.save(saved, false, &error) && reopened.load(saved, &error) &&
			   samePoses(reopened.mesh().skeleton.clips.last().frames, imported.frames, 1e-6),
		   "the imported clip survives save and reload");
	expect(!document.edit(edit, &error), "a clip name in use is refused");

	// Another editable source with the same skeleton imports exactly.
	auto exact = importEdit(mesh, "copy");
	expect(document.setMesh(mesh, &error) && document.edit(exact, &error) &&
			   samePoses(document.mesh().skeleton.clips.last().frames, walk, 1e-5),
		   "an identical rig imports exactly");

	// Renamed joints need a map; missing joints need fill.
	ModelEdit rename;
	rename.kind = ModelEditKind::RenameJoint;
	rename.rig.joint = "l_hand";
	rename.rig.name = "l_palm";
	ModelDocument renamed;
	expect(renamed.setMesh(mesh, &error) && renamed.edit(rename, &error), "rename a joint of the destination");
	expect(!renamed.edit(importEdit(mesh, "walk2"), &error) && error.contains("l_palm"), "a joint without a source is refused");
	auto mapped = importEdit(mesh, "walk2");
	mapped.animation.import.joints = {{"l_hand", "l_palm"}};
	expect(renamed.edit(mapped, &error) && samePoses(renamed.mesh().skeleton.clips.last().frames, walk, 1e-5),
		   "renamed joints import through a map");
	ModelEdit addJoint;
	addJoint.kind = ModelEditKind::AddJoint;
	addJoint.rig.name = "extra";
	addJoint.rig.parent = "spine";
	addJoint.rig.origin = {0, 0, 12};
	ModelDocument extra;
	expect(extra.setMesh(mesh, &error) && extra.edit(addJoint, &error), "add a joint the source lacks");
	expect(!extra.edit(importEdit(mesh, "walk2"), &error), "a missing joint refuses the import");
	auto filled = importEdit(mesh, "walk2");
	filled.animation.import.fillFromBind = true;
	expect(extra.edit(filled, &error), "fill from bind imports the clip");
	const int extraJoint = modelJointIndex(extra.mesh().skeleton, "extra", Qt::CaseSensitive);
	expect(modelJointMatrixDistance(local(extra.mesh(), 1, 2, extraJoint),
									modelJointMultiply(modelJointInverse(extra.mesh().skeleton.joints[1].bind), extra.mesh().skeleton.joints[extraJoint].bind)) < 1e-5,
		   "the unmatched joint keeps its bind pose");

	// Retargeting onto a rig with longer bones.
	const auto tall = symmetricRigFixture(4, 1.5f);
	auto retarget = importEdit(mesh, "walk_tall");
	ModelDocument tallDocument;
	expect(tallDocument.setMesh(tall, &error), "open the taller rig");
	auto unscaled = retarget;
	unscaled.animation.import.rootScale = 2;
	expect(!tallDocument.edit(unscaled, &error), "root scale needs retargeting");
	retarget.animation.import.retarget = true;
	expect(tallDocument.edit(retarget, &error), "retarget the walk onto the taller rig");
	const auto &result = tallDocument.mesh();
	const int clip = int(result.skeleton.clips.size()) - 1;
	bool lengths = true, rotations = true;
	for (int f = 0; f < 4; ++f)
		for (int j = 1; j < result.skeleton.joints.size(); ++j)
		{
			const auto bindTranslation = modelJointTranslation(modelJointMultiply(modelJointInverse(tall.skeleton.joints[result.skeleton.joints[j].parent].bind),
																				   tall.skeleton.joints[j].bind));
			const double bindLength = std::sqrt(double(bindTranslation.x) * bindTranslation.x + double(bindTranslation.y) * bindTranslation.y +
												double(bindTranslation.z) * bindTranslation.z);
			lengths = lengths && std::abs(length(result, clip, f, j) - bindLength) < 1e-4;
			const auto a = modelJointRotation(local(result, clip, f, j)), b = modelJointRotation(local(mesh, 0, f, j));
			rotations = rotations && std::abs(std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) - 1) < 1e-5;
		}
	expect(lengths, "retargeted poses keep the destination's bone lengths");
	expect(rotations, "retargeted poses match the source's local rotations");

	// Hierarchy mismatches need retargeting.
	ModelEdit reparent;
	reparent.kind = ModelEditKind::ReparentJoint;
	reparent.rig.joint = "r_arm";
	reparent.rig.parent = "root";
	ModelDocument moved;
	expect(moved.setMesh(mesh, &error) && moved.edit(reparent, &error), "reparent a destination joint");
	expect(!moved.edit(importEdit(mesh, "walk2"), &error), "an exact import between different hierarchies is refused");
	auto across = importEdit(mesh, "walk2");
	across.animation.import.retarget = true;
	expect(moved.edit(across, &error), "retargeting imports between different hierarchies");

	// The source clip's curves and events come along.
	auto keyed = mesh;
	ModelEdit capture;
	capture.kind = ModelEditKind::EditAnimationCurves;
	capture.curves.clip = 0;
	capture.curves.joint = "l_arm";
	ModelEdit step;
	step.kind = ModelEditKind::EditAnimationEvents;
	step.animation.events.clip = 0;
	step.animation.events.event = {2, "sound_body", {"snd_step"}};
	expect(applyModelEdit(&keyed, capture, nullptr, &error) && applyModelEdit(&keyed, step, nullptr, &error) &&
			   keyed.skeleton.clips[0].curves.size() == 1 && keyed.skeleton.clips[0].events.size() == 1,
		   "key the source's arm and add a footstep event");
	auto withCurves = importEdit(keyed, "keyed");
	expect(withCurves.animation.import.curves.size() == 1 && withCurves.animation.import.events.size() == 1,
		   "the source's curves and events fill the import");
	expect(planModelClipImport(mesh, withCurves.animation.import, &plan, &error) && plan.curves == 1 && plan.events == 1 && !plan.capturedCurves,
		   "the plan counts the curves and events it keeps");
	ModelDocument keyedDocument;
	expect(keyedDocument.setMesh(mesh, &error) && keyedDocument.edit(withCurves, &error), "import a keyed clip");
	const auto &keyedClip = keyedDocument.mesh().skeleton.clips.last();
	expect(keyedClip.curves.size() == 1 && keyedClip.curves[0] == keyed.skeleton.clips[0].curves[0] && keyedClip.events == keyed.skeleton.clips[0].events,
		   "an exact import keeps the source's keys and events");
	ModelDocument renamedKeys;
	auto renamedArm = rename;
	renamedArm.rig.joint = "l_arm";
	renamedArm.rig.name = "l_limb";
	auto mappedKeys = importEdit(keyed, "keyed");
	mappedKeys.animation.import.joints = {{"l_arm", "l_limb"}};
	expect(renamedKeys.setMesh(mesh, &error) && renamedKeys.edit(renamedArm, &error) && renamedKeys.edit(mappedKeys, &error) &&
			   renamedKeys.mesh().skeleton.clips.last().curves.size() == 1 && renamedKeys.mesh().skeleton.clips.last().curves[0].joint == "l_limb",
		   "curves follow the joint map");
	auto samplesOnly = importEdit(keyed, "plain");
	samplesOnly.animation.import.curves.clear();
	samplesOnly.animation.import.events.clear();
	expect(keyedDocument.edit(samplesOnly, &error) && keyedDocument.mesh().skeleton.clips.last().curves.isEmpty() &&
			   keyedDocument.mesh().skeleton.clips.last().events.isEmpty(),
		   "cleared curves and events import the samples alone");
	auto lateEvent = importEdit(keyed, "late");
	lateEvent.animation.import.events = {{9, "sound_body", {"snd_step"}}};
	expect(!planModelClipImport(mesh, lateEvent.animation.import, &plan, &error) && !keyedDocument.edit(lateEvent, &error),
		   "events past the clip's samples are refused");
	auto keyedRetarget = importEdit(keyed, "keyed_tall");
	keyedRetarget.animation.import.retarget = true;
	ModelDocument keyedTall;
	expect(planModelClipImport(tall, keyedRetarget.animation.import, &plan, &error) && plan.capturedCurves && plan.curves == 1 &&
			   keyedTall.setMesh(tall, &error) && keyedTall.edit(keyedRetarget, &error),
		   "a retargeted import captures curves for the keyed joints");
	const auto &tallClip = keyedTall.mesh().skeleton.clips.last();
	expect(tallClip.curves.size() == 1 && tallClip.curves[0].joint == "l_arm" && tallClip.curves[0].channels[5].size() == 4 && tallClip.events.size() == 1,
		   "captured curves hold one key per sample and the events are kept");

	// CLI.
	const auto source = QDir(tmp.path()).filePath("rig.mesh.json"), out = QDir(tmp.path()).filePath("out.mesh.json");
	ModelDocument cliSource;
	expect(cliSource.setMesh(mesh, &error) && cliSource.save(source, false, &error), "save the CLI source");
	auto cli = cli::runModelPose({"vibestudio", "--cli", "model", "pose", source, "--operation", "import", "--source", animPath, "--name", "walk2",
								  "--output", out, "--json"});
	expect(cli.exitCode == 0 && cli.payload["name"].toString() == "walk2" && cli.payload["samples"].toInt() == 4 &&
			   cli.payload["inWorkingSet"].toBool(),
		   "CLI imports an md5anim clip");
	expect(cli::runModelPose({"vibestudio", "--cli", "model", "pose", source, "--operation", "import", "--source", saved, "--name", "again",
							  "--output", saved, "--overwrite"})
				   .exitCode == 1,
		   "CLI refuses to overwrite the source it reads");
	expect(cli::runModelPose({"vibestudio", "--cli", "model", "pose", source, "--operation", "import", "--source", animPath, "--root-scale", "2",
							  "--output", out, "--overwrite"})
				   .exitCode == 4,
		   "CLI refuses root scale without retargeting");
	cli = cli::runModelPose({"vibestudio", "--cli", "model", "pose", source, "--operation", "import", "--source", saved, "--source-clip",
							 "walk_again", "--retarget", "--name", "again", "--output", out, "--overwrite", "--json"});
	expect(cli.exitCode == 0 && cli.payload["retarget"].toBool() && cli.payload["sourceClip"].toString() == "walk_again",
		   "CLI retargets a named clip of another editable source");
	const auto keyedPath = QDir(tmp.path()).filePath("keyed.mesh.json");
	ModelDocument keyedSave;
	expect(keyedSave.setMesh(keyed, &error) && keyedSave.save(keyedPath, false, &error), "save the keyed source");
	cli = cli::runModelPose({"vibestudio", "--cli", "model", "pose", source, "--operation", "import", "--source", keyedPath, "--name", "keyed",
							 "--output", out, "--overwrite", "--json"});
	expect(cli.exitCode == 0 && cli.payload["curveJoints"].toInt() == 1 && cli.payload["events"].toInt() == 1 && !cli.payload["curvesCaptured"].toBool(),
		   "CLI imports the source's curves and events");
	cli = cli::runModelPose({"vibestudio", "--cli", "model", "pose", source, "--operation", "import", "--source", keyedPath, "--name", "keyed",
							 "--no-curves", "--no-events", "--output", out, "--overwrite", "--json"});
	expect(cli.exitCode == 0 && cli.payload["curveJoints"].toInt() == 0 && cli.payload["events"].toInt() == 0, "CLI imports samples alone on request");
	expect(cli::runModelPose({"vibestudio", "--cli", "model", "pose", source, "--operation", "copy-out", "--clip", "0", "--first", "0", "--no-events",
							  "--output", out, "--overwrite"})
				   .exitCode == 2,
		   "CLI refuses --no-events outside import");
	if (argc > 1)
	{
		QProcess process;
		process.start(QString::fromLocal8Bit(argv[1]), {"--cli", "model", "pose", source, "--operation", "import", "--source", animPath, "--name",
														"walk3", "--output", out, "--overwrite", "--dry-run", "--json"});
		expect(process.waitForFinished(60000) && process.exitCode() == 0 &&
				   QJsonDocument::fromJson(process.readAllStandardOutput()).object()["operation"].toString() == "import",
			   "application dispatches clip import");
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
