#include "core/model_animation_curves.h"
#include "core/model_document.h"
#include "core/model_frame_tools.h"
#include "core/model_pose.h"
#include "core/model_skeleton.h"
#include "core/model_tags.h"
#include "tests/model_frame_tools_test_helpers.h"
#include "tests/model_mdl_test_helpers.h"
#include "tests/model_skin_provenance_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <cstdlib>
#include <iostream>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

using namespace vibestudio;
namespace
{
bool ok = true;
bool expect(bool condition, const QString &message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << qPrintable(message) << '\n';
		ok = false;
	}
	return condition;
}
QByteArray bytes(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
bool near(double a, double b, double tolerance = 1e-4) { return std::abs(a - b) <= tolerance; }

// Frames walk1..3, run1..; positions and tags move linearly in z, the tag
// turns 3 degrees per frame about z, and the collision box slides in x.
ModelMesh motionFixture(int count)
{
	auto mesh = tests::frameRangeFixture(count);
	for (auto &tag : mesh.tags)
	{
		const double angle = tag.frameIndex * 3.0 * 3.14159265358979 / 180;
		tag.axis[0] = float(std::cos(angle));
		tag.axis[1] = float(std::sin(angle));
		tag.axis[3] = float(-std::sin(angle));
		tag.axis[4] = float(std::cos(angle));
	}
	for (auto &frame : mesh.surfaces[0].frames)
		for (auto &p : frame.positions)
			p.x = float(int(p.x)); // keep x independent of the frame
	mesh.animations[1].framesPerSecond = 30;
	updateEditableModelMetadata(&mesh);
	return mesh;
}
double tagAngle(const ModelMesh &mesh, int frame)
{
	const auto *tag = findModelTag(mesh, "tag_mount", frame);
	return tag ? std::atan2(tag->axis[1], tag->axis[0]) * 180 / 3.14159265358979 : -1000;
}
ModelEdit resampleEdit(int clip, double fps, int frames, ModelResampleMethod method = ModelResampleMethod::Linear, bool cyclic = false)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::ResampleAnimation;
	edit.resample.clip = clip;
	edit.resample.targetFps = fps;
	edit.resample.targetFrames = frames;
	edit.resample.method = method;
	edit.resample.cyclic = cyclic;
	return edit;
}

void checkVertexResample()
{
	const auto mesh = motionFixture(31);
	QString error;
	ModelDocument document;
	expect(document.setMesh(mesh, &error), QStringLiteral("open the motion fixture: ") + error);
	// 30 to 10 FPS: every third stored frame, exactly.
	expect(document.edit(resampleEdit(1, 10, 0), &error), QStringLiteral("resample 30 to 10 FPS: ") + error);
	auto after = document.mesh();
	bool exact = after.frames.size() == 13;
	for (int j = 0; exact && j < 10; ++j)
		exact = after.surfaces[0].frames[3 + j].positions[0].z == float(4 * (3 + 3 * j)) && near(tagAngle(after, 3 + j), 3.0 * (3 + 3 * j));
	expect(exact, QStringLiteral("30 to 10 FPS keeps every third pose exactly"));
	expect(after.animations[1].firstFrame == 3 && after.animations[1].frameCount == 10 && after.animations[1].framesPerSecond == 10 &&
			   after.animations[0].frameCount == 3 && after.animations[2].frameCount == 13,
		   QStringLiteral("the clip takes its new range and rate; a clip holding it shrinks with it"));
	expect(after.frames[3].name == "run1" && after.frames[12].name == "run10", QStringLiteral("resampled frames are renamed after the clip"));
	expect(modelNativeClipRoundTrip(after)[1].survives || after.animations[1].framesPerSecond > 0,
		   QStringLiteral("resampled names keep the clip's stem"));
	expect(document.undo() && bytes(document.mesh()) == bytes(mesh), QStringLiteral("undo restores the clip in one step"));
	// A fractional step blends positions, tags and collision.
	expect(document.edit(resampleEdit(1, 0, 19), &error), QStringLiteral("resample to 19 frames: ") + error);
	after = document.mesh();
	bool blended = after.frames.size() == 22;
	for (int j = 0; blended && j < 19; ++j)
	{
		const double u = 3 + 1.5 * j;
		blended = near(after.surfaces[0].frames[3 + j].positions[0].z, 4 * u) && near(tagAngle(after, 3 + j), 3 * u, 1e-3) &&
				  near(after.collisionBoxes[0].framePoses[3 + j].centre.x, u) && near(findModelTag(after, "tag_mount", 3 + j)->origin.z, 4 * u);
	}
	expect(blended, QStringLiteral("linear samples blend positions, slerp tags and interpolate collision"));
	expect(near(after.animations[1].framesPerSecond, 30.0 * 18 / 27), QStringLiteral("a frame count keeps the clip's duration"));
	document.undo();
	expect(document.edit(resampleEdit(1, 0, 19, ModelResampleMethod::Nearest), &error), QStringLiteral("nearest resample: ") + error);
	after = document.mesh();
	bool nearest = true;
	for (int j = 0; nearest && j < 19; ++j)
		nearest = after.surfaces[0].frames[3 + j].positions[0].z == float(4 * (3 + std::lround(1.5 * j)));
	expect(nearest, QStringLiteral("nearest samples copy stored poses"));
	document.undo();
	// A looping clip samples the step back to its first frame.
	expect(document.edit(resampleEdit(0, 0, 6, ModelResampleMethod::Linear, true), &error), QStringLiteral("cyclic resample: ") + error);
	after = document.mesh();
	expect(after.animations[0].frameCount == 6 && near(after.surfaces[0].frames[5].positions[0].z, 4) &&
			   near(after.surfaces[0].frames[1].positions[0].z, 2),
		   QStringLiteral("the last cyclic sample blends the last frame back toward the first"));
	document.undo();
	// Refusals change nothing.
	const auto before = bytes(document.mesh());
	ModelEdit bad = resampleEdit(1, 10, 5);
	expect(!document.edit(bad, &error) && error.contains("either") && bytes(document.mesh()) == before, QStringLiteral("rate and count conflict"));
	expect(!document.edit(resampleEdit(0, 10, 0), &error) && error.contains("FPS") && bytes(document.mesh()) == before,
		   QStringLiteral("a clip without FPS needs a source rate"));
	const bool limited = !document.edit(resampleEdit(0, 0, 1024), &error) && error.contains("1024") && bytes(document.mesh()) == before;
	expect(limited, QStringLiteral("the frame limit is enforced: ") + error);
	ModelWorkControl cancelled;
	cancelled.cancelled = [] { return true; };
	expect(!document.edit(resampleEdit(1, 10, 0), &error, cancelled) && bytes(document.mesh()) == before,
		   QStringLiteral("a cancelled resample changes nothing"));
	auto overlapping = mesh;
	overlapping.animations.append({QStringLiteral("half"), 2, 4});
	ModelDocument other;
	expect(other.setMesh(overlapping, &error) && !other.edit(resampleEdit(1, 10, 0), &error) && error.contains("covers part"),
		   QStringLiteral("a partly overlapping clip is refused: ") + error);
}

void checkLoops()
{
	const auto mesh = motionFixture(6);
	QString error;
	const auto close = [](ModelLoopClose mode, int blend)
	{
		ModelEdit edit;
		edit.kind = ModelEditKind::CloseAnimationLoop;
		edit.resample.clip = 1;
		edit.resample.loopClose = mode;
		edit.resample.blendFrames = blend;
		return edit;
	};
	ModelDocument document;
	expect(document.setMesh(mesh, &error) && document.edit(close(ModelLoopClose::ReplaceLast, 0), &error), QStringLiteral("replace the last frame: ") + error);
	auto after = document.mesh();
	expect(after.surfaces[0].frames[5].positions[0].z == mesh.surfaces[0].frames[3].positions[0].z && after.frames[5].name == "run3" &&
			   after.frames.size() == 6,
		   QStringLiteral("the last frame takes the first pose and keeps its name"));
	document.undo();
	expect(document.edit(close(ModelLoopClose::AppendFirst, 0), &error), QStringLiteral("append the first pose: ") + error);
	after = document.mesh();
	expect(after.frames.size() == 7 && after.animations[1].frameCount == 4 && after.frames[6].name == "run4" &&
			   after.surfaces[0].frames[6].positions[0].z == mesh.surfaces[0].frames[3].positions[0].z,
		   QStringLiteral("an appended first pose extends the clip"));
	document.undo();
	expect(document.edit(close(ModelLoopClose::CrossFade, 2), &error), QStringLiteral("cross-fade: ") + error);
	after = document.mesh();
	const double first = 12, fourth = 16, fifth = 20;
	expect(near(after.surfaces[0].frames[4].positions[0].z, fourth + (first - fourth) / 3) &&
			   near(after.surfaces[0].frames[5].positions[0].z, fifth + (first - fifth) * 2 / 3) && after.frames.size() == 6,
		   QStringLiteral("cross-faded frames ease toward the first pose"));
	document.undo();
	expect(!document.edit(close(ModelLoopClose::CrossFade, 3), &error) && error.contains("1 to 2"), QStringLiteral("too many blend frames"));
}

void checkMdl()
{
	ModelMesh mesh;
	QString error;
	if (!expect(importEditableModel("models/grouped.mdl", tests::groupedMdlFixture().bytes, &mesh, &error), QStringLiteral("decode MDL: ") + error))
		return;
	int walk = -1;
	for (int i = 0; i < mesh.animations.size(); ++i)
		if (mesh.animations[i].name == "walk")
			walk = i;
	ModelDocument document;
	expect(walk >= 0 && document.setMesh(mesh, &error) && document.edit(resampleEdit(walk, 0, 4), &error),
		   QStringLiteral("resample the timed group's clip: ") + error);
	const auto groups = document.mesh().mdl.frameGroups;
	expect(groups.size() == 2 && groups[0].intervals.size() == 4 && near(groups[0].intervals[0], .075) && near(groups[0].intervals[3], .3) &&
			   groups[1].firstFrame == 4,
		   QStringLiteral("a resampled group keeps its total time, split evenly"));
	const auto exported = exportEditableModel(document.mesh(), "mdl", 0, &error);
	expect(!exported.isEmpty(), QStringLiteral("the resampled MDL exports: ") + error);
}

void checkSkeletal()
{
	auto mesh = tests::skinFixture();
	QString error;
	const auto parents = modelJointParents(mesh.skeleton);
	const auto original = mesh.skeleton.clips[0];
	ModelEdit edit;
	edit.kind = ModelEditKind::ResampleSkeletalClip;
	edit.resample.clip = 0;
	edit.resample.targetFrames = 3;
	ModelDocument document;
	expect(document.setMesh(mesh, &error) && document.edit(edit, &error), QStringLiteral("resample a skeletal clip: ") + error);
	const auto &clip = document.mesh().skeleton.clips[0];
	bool matches = clip.frames.size() == 3 && modelJointMatrixDistance(clip.frames[0][1], original.frames[0][1]) < 1e-5 &&
				   modelJointMatrixDistance(clip.frames[2][1], original.frames[1][1]) < 1e-5;
	const auto a = modelJointsToLocalSpace(parents, original.frames[0]), b = modelJointsToLocalSpace(parents, original.frames[1]);
	const auto middle = modelJointsToLocalSpace(parents, clip.frames[1]);
	for (int joint = 0; matches && joint < a.size(); ++joint)
	{
		const auto expected = modelJointMatrix(modelQuatSlerp(modelJointRotation(a[joint]), modelJointRotation(b[joint]), .5f),
											   interpolateModelPosition(modelJointTranslation(a[joint]), modelJointTranslation(b[joint]), .5),
											   interpolateModelPosition(modelJointScale(a[joint]), modelJointScale(b[joint]), .5));
		matches = modelJointMatrixDistance(middle[joint], expected) < 1e-4;
	}
	expect(matches, QStringLiteral("the middle sample is the parent-space blend of the two poses"));
	expect(clip.framesPerSecond == 20 && document.mesh().skeleton.bakedClipForFrame.count(0) == 3,
		   QStringLiteral("the clip keeps its duration and its frames are re-baked"));
	ModelExportOptions options;
	options.clip = 0;
	ModelExportReport report;
	expect(!exportEditableModel(document.mesh(), "md5anim", options, &error, {}, &report).isEmpty() && report.frameRate == 20 &&
			   report.clipFrames == 3,
		   QStringLiteral("the resampled clip exports md5anim with a whole frame rate: ") + error);
	expect(document.undo() && document.mesh().skeleton.clips[0].frames.size() == 2, QStringLiteral("undo restores the skeletal clip"));
	// Curves are rescaled and evaluated at the new sample times.
	ModelPoseCurves curves;
	if (expect(captureModelPoseCurves(mesh, 0, "arm.L", &curves, &error), QStringLiteral("capture curves: ") + error))
	{
		auto curved = mesh;
		curved.skeleton.clips[0].curves = {curves};
		ModelDocument withCurves;
		edit.resample.targetFrames = 4;
		expect(withCurves.setMesh(curved, &error) && withCurves.edit(edit, &error), QStringLiteral("resample a clip with curves: ") + error);
		const auto &result = withCurves.mesh().skeleton.clips[0];
		const int arm = modelJointIndex(withCurves.mesh().skeleton, "arm.L", Qt::CaseSensitive);
		const auto local = modelJointsToLocalSpace(parents, result.frames[1]);
		expect(result.curves.size() == 1 && result.curves[0].channels[0].last().frame == 3 &&
				   modelJointMatrixDistance(local[arm], evaluateModelPoseCurves(curves, 1.0 / 3)) < 1e-4,
			   QStringLiteral("keyed joints follow their curves at the new sample times"));
		edit.resample.cyclic = true;
		ModelDocument cyclic;
		expect(cyclic.setMesh(curved, &error) && !cyclic.edit(edit, &error) && error.contains("Bake curves"),
			   QStringLiteral("a looping clip with curves needs Bake curves"));
		edit.resample.bakeCurves = true;
		expect(cyclic.edit(edit, &error) && cyclic.mesh().skeleton.clips[0].curves.isEmpty(), QStringLiteral("baking removes the curves: ") + error);
	}
	// Closing a loop on a clip with curves edits the curve keys too.
	ModelPoseCurves armCurves;
	if (expect(captureModelPoseCurves(mesh, 0, "arm.L", &armCurves, &error), QStringLiteral("capture curves for the loop: ") + error))
	{
		auto curved = mesh;
		curved.skeleton.clips[0].curves = {armCurves};
		const int arm = modelJointIndex(curved.skeleton, "arm.L", Qt::CaseSensitive);
		for (const auto mode : {ModelLoopClose::ReplaceLast, ModelLoopClose::AppendFirst})
		{
			ModelEdit close;
			close.kind = ModelEditKind::CloseSkeletalLoop;
			close.resample.clip = 0;
			close.resample.loopClose = mode;
			ModelDocument keyed;
			const bool closed = keyed.setMesh(curved, &error) && keyed.edit(close, &error);
			const auto &result = keyed.mesh().skeleton.clips[0];
			const int last = int(result.frames.size()) - 1;
			const auto firstLocal = modelJointsToLocalSpace(parents, result.frames[0]), lastLocal = modelJointsToLocalSpace(parents, result.frames[last]);
			expect(closed && result.curves.size() == 1 && result.curves[0].channels[0].last().frame == last &&
					   modelJointMatrixDistance(firstLocal[arm], lastLocal[arm]) < 1e-4 &&
					   modelJointMatrixDistance(evaluateModelPoseCurves(result.curves[0], last), lastLocal[arm]) < 1e-4,
				   QStringLiteral("closing a loop keeps the curves and keys the first pose at the end: ") + error);
		}
	}
	ModelEdit loop;
	loop.kind = ModelEditKind::CloseSkeletalLoop;
	loop.resample.clip = 0;
	loop.resample.loopClose = ModelLoopClose::AppendFirst;
	ModelDocument looped;
	expect(looped.setMesh(mesh, &error) && looped.edit(loop, &error) && looped.mesh().skeleton.clips[0].frames.size() == 3 &&
			   modelJointMatrixDistance(looped.mesh().skeleton.clips[0].frames[2][1], original.frames[0][1]) < 1e-6,
		   QStringLiteral("closing a skeletal loop appends the first pose: ") + error);
}

QJsonObject runCli(const QString &program, const QStringList &arguments, int *exitCode)
{
	QProcess process;
	process.start(program, arguments);
	const bool finished = process.waitForStarted(10000) && process.waitForFinished(120000);
	*exitCode = finished ? process.exitCode() : -1;
	return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
}

void checkCli(const QString &program, const QDir &dir)
{
	const auto source = dir.filePath("motion.mesh.json");
	const auto mesh = motionFixture(31);
	ModelDocument document;
	QString error;
	if (!expect(document.setMesh(mesh, &error) && document.save(source, false, &error), QStringLiteral("save the CLI fixture: ") + error))
		return;
	const QStringList common{"--json", "--settings-file", dir.filePath("settings.ini")};
	int code = -1;
	const auto output = dir.filePath("10fps.mesh.json");
	auto json = runCli(program, QStringList{"--cli", "model", "frames", source, "--operation", "resample", "--clip", "1", "--fps", "10", "--output", output} + common, &code);
	ModelDocument written, expected;
	expect(code == 0 && written.load(output, &error) && expected.setMesh(mesh, &error) && expected.edit(resampleEdit(1, 10, 0), &error) &&
			   bytes(written.mesh()) == bytes(expected.mesh()) && json.value("frames").toInt() == 13,
		   QStringLiteral("model frames resample writes what the document service makes: ") + error);
	runCli(program, QStringList{"--cli", "model", "frames", source, "--operation", "resample", "--clip", "1", "--fps", "10", "--frames", "4",
								"--output", dir.filePath("bad.mesh.json")} + common,
		   &code);
	expect(code == 2, QStringLiteral("rate and count together are a usage error"));
	json = runCli(program, QStringList{"--cli", "model", "frames", source, "--operation", "close-loop", "--clip", "1", "--mode", "crossfade",
									   "--blend-frames", "3", "--output", dir.filePath("loop.mesh.json"), "--dry-run"} + common,
				  &code);
	expect(code == 0 && !json.value("written").toBool(), QStringLiteral("close-loop dry run"));
}
} // namespace

int main(int argc, char **argv)
{
#if defined(_MSC_VER) && defined(_DEBUG)
	for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
	{
		_CrtSetReportMode(type, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
	}
#endif
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	QTemporaryDir temporary(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("frame-resample-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	checkVertexResample();
	checkLoops();
	checkMdl();
	checkSkeletal();
	if (argc > 1)
		checkCli(QString::fromLocal8Bit(argv[1]), QDir(temporary.path()));
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
