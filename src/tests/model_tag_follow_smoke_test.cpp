#include "core/model_assembly.h"
#include "core/model_document.h"
#include "core/model_tag_follow.h"
#include "core/model_tags.h"
#include "tests/model_frame_tools_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <array>
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
using Mat = std::array<double, 9>;
// Rotation about z by 10 f degrees, then about x by 5 f degrees.
Mat rotationOf(int frame)
{
	const double a = frame * 10 * 3.14159265358979 / 180, b = frame * 5 * 3.14159265358979 / 180;
	const Mat z{std::cos(a), -std::sin(a), 0, std::sin(a), std::cos(a), 0, 0, 0, 1};
	const Mat x{1, 0, 0, 0, std::cos(b), -std::sin(b), 0, std::sin(b), std::cos(b)};
	Mat r{};
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			for (int k = 0; k < 3; ++k)
				r[i * 3 + j] += x[i * 3 + k] * z[k * 3 + j];
	return r;
}
ModelVec3 moved(int frame, ModelVec3 p)
{
	const auto r = rotationOf(frame);
	return {float(r[0] * p.x + r[1] * p.y + r[2] * p.z + frame), float(r[3] * p.x + r[4] * p.y + r[5] * p.z + 2 * frame),
			float(r[6] * p.x + r[7] * p.y + r[8] * p.z + 3 * frame)};
}
bool near(ModelVec3 a, ModelVec3 b, double tolerance = 1e-4)
{
	return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.z - b.z) <= tolerance;
}
// A plate moving rigidly through six frames, with tag_mount at (3, 4, 5) in
// frame 0 and a meaningless pose elsewhere.
ModelMesh plateFixture(bool reflected = false)
{
	auto mesh = tests::frameRangeFixture(6);
	mesh.collisionBoxes.clear();
	const auto base = mesh.surfaces[0].frames[0].positions;
	for (int f = 0; f < 6; ++f)
	{
		auto &frame = mesh.surfaces[0].frames[f];
		for (int v = 0; v < base.size(); ++v)
			frame.positions[v] = moved(f, {base[v].x, base[v].y, 0});
		const auto n = moved(f, {0, 0, 1});
		const auto o = moved(f, {0, 0, 0});
		frame.normals.fill({n.x - o.x, n.y - o.y, n.z - o.z}, base.size());
	}
	for (auto &tag : mesh.tags)
	{
		tag.origin = tag.frameIndex == 0 ? ModelVec3{3, 4, 5} : ModelVec3{0, 0, 0};
		std::fill(std::begin(tag.axis), std::end(tag.axis), 0.0f);
		tag.axis[0] = tag.axis[4] = 1;
		tag.axis[8] = reflected ? -1 : 1;
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}
ModelEdit followEdit(const QString &tag, ModelTagFollowMode mode, int first = -1, int last = -1)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::FollowTagComponents;
	edit.selection.surface = 0;
	edit.selection.vertices = {0, 1, 2, 3};
	edit.tagFollow.tag = tag;
	edit.tagFollow.mode = mode;
	edit.tagFollow.first = first;
	edit.tagFollow.last = last;
	return edit;
}
bool followsRigidly(const ModelMesh &mesh, const QString &name, int frame, ModelVec3 origin, double handedness)
{
	const auto *tag = findModelTag(mesh, name, frame);
	if (!tag || !near(tag->origin, moved(frame, origin)))
		return false;
	const auto r = rotationOf(frame);
	for (int row = 0; row < 3; ++row)
		for (int c = 0; c < 3; ++c)
		{
			const double expected = r[c * 3 + row] * (row == 2 ? handedness : 1);
			if (std::abs(tag->axis[row * 3 + c] - expected) > 1e-4)
				return false;
		}
	return true;
}

void checkCore()
{
	QString error;
	const auto mesh = plateFixture();
	ModelDocument document;
	expect(document.setMesh(mesh, &error) && document.edit(followEdit("tag_mount", ModelTagFollowMode::Rigid), &error),
		   QStringLiteral("rigid follow: ") + error);
	bool rigid = true;
	for (int f = 0; f < 6; ++f)
		rigid = rigid && followsRigidly(document.mesh(), "tag_mount", f, {3, 4, 5}, 1);
	expect(rigid, QStringLiteral("the tag follows the plate's rigid motion at its frame-0 offset"));
	ModelTagFollowResult result;
	expect(solveModelTagFollow(mesh, followEdit("tag_mount", ModelTagFollowMode::Rigid).selection, followEdit("tag_mount", ModelTagFollowMode::Rigid).tagFollow,
							   &result, &error) &&
			   result.poses.size() == 6 && result.maxResidual < 1e-4 && result.points == 4 && !result.created,
		   QStringLiteral("rigid motion fits with no residual"));
	expect(modelTagFollowSummary(followEdit("tag_mount", ModelTagFollowMode::Rigid).tagFollow, result).contains("6 frames"),
		   QStringLiteral("the summary counts frames"));
	expect(document.undo() && bytes(document.mesh()) == bytes(mesh), QStringLiteral("undo restores every tag in one step"));
	// Surface mode reproduces rigid motion of a plate.
	expect(document.edit(followEdit("tag_mount", ModelTagFollowMode::Surface), &error) == false, QStringLiteral("surface mode needs faces"));
	auto surface = followEdit("tag_mount", ModelTagFollowMode::Surface);
	surface.selection.vertices.clear();
	surface.selection.faces = {0, 1};
	expect(document.edit(surface, &error) && followsRigidly(document.mesh(), "tag_mount", 4, {3, 4, 5}, 1),
		   QStringLiteral("surface following matches the plate's rotation: ") + error);
	document.undo();
	// Centroid mode keeps the axes.
	expect(document.edit(followEdit("tag_mount", ModelTagFollowMode::Centroid), &error), QStringLiteral("centroid follow: ") + error);
	const auto *centroid = findModelTag(document.mesh(), "tag_mount", 3);
	const auto c0 = moved(0, {5, 5, 0}), c3 = moved(3, {5, 5, 0});
	expect(centroid && near(centroid->origin, {3 - c0.x + c3.x, 4 - c0.y + c3.y, 5 - c0.z + c3.z}) && centroid->axis[0] == 1 && centroid->axis[4] == 1,
		   QStringLiteral("centroid following moves the origin only"));
	document.undo();
	// A frame range leaves other frames alone; a new tag is created.
	expect(document.edit(followEdit("tag_weapon", ModelTagFollowMode::Rigid, 2, 3), &error), QStringLiteral("new tag over a range: ") + error);
	const auto *start = findModelTag(document.mesh(), "tag_weapon", 0), *fitted = findModelTag(document.mesh(), "tag_weapon", 3);
	const auto centre0 = moved(0, {5, 5, 0});
	expect(start && fitted && near(start->origin, centre0) && followsRigidly(document.mesh(), "tag_weapon", 3, {5, 5, 0}, 1) &&
			   findModelTag(document.mesh(), "tag_mount", 3)->origin.x == 0,
		   QStringLiteral("a new tag starts at the selection's centre and follows it inside the range"));
	document.undo();
	// Reflected bases keep their handedness.
	ModelDocument reflected;
	expect(reflected.setMesh(plateFixture(true), &error) && reflected.edit(followEdit("tag_mount", ModelTagFollowMode::Rigid), &error) &&
			   followsRigidly(reflected.mesh(), "tag_mount", 5, {3, 4, 5}, -1),
		   QStringLiteral("a reflected tag stays reflected: ") + error);
	// Refusals change nothing.
	const auto before = bytes(document.mesh());
	auto line = followEdit("tag_mount", ModelTagFollowMode::Rigid);
	line.selection.vertices = {0, 1};
	expect(!document.edit(line, &error) && error.contains("not in a line") && bytes(document.mesh()) == before,
		   QStringLiteral("two points are refused for rigid following"));
	auto badName = followEdit(QStringLiteral(" tag"), ModelTagFollowMode::Rigid);
	expect(!document.edit(badName, &error) && bytes(document.mesh()) == before, QStringLiteral("an invalid name is refused"));
	ModelWorkControl cancelled;
	cancelled.cancelled = [] { return true; };
	expect(!document.edit(followEdit("tag_mount", ModelTagFollowMode::Rigid), &error, cancelled) && bytes(document.mesh()) == before,
		   QStringLiteral("a cancelled fit changes nothing"));
	// The fitted track survives an MD3 round trip.
	expect(document.edit(followEdit("tag_mount", ModelTagFollowMode::Rigid), &error), QStringLiteral("fit again"));
	const auto exported = exportEditableModel(document.mesh(), "md3", 0, &error);
	ModelMesh decoded;
	expect(!exported.isEmpty() && importEditableModel("models/plate.md3", exported, &decoded, &error) &&
			   followsRigidly(decoded, "tag_mount", 4, {3, 4, 5}, 1),
		   QStringLiteral("MD3 keeps the fitted tag track: ") + error);
}

// Anchors keep what Follow Selection fitted, so the tag can be fitted again.
void checkAnchors(const QDir &dir)
{
	QString error;
	const auto mesh = plateFixture();
	ModelDocument document;
	expect(document.setMesh(mesh, &error) && document.edit(followEdit("tag_mount", ModelTagFollowMode::Rigid), &error),
		   QStringLiteral("fit an anchored tag: ") + error);
	const auto anchors = document.mesh().tagAnchors;
	expect(anchors.size() == 1 && anchors[0].tag == "tag_mount" && anchors[0].vertices == QVector<int>({0, 1, 2, 3}) && anchors[0].vertexCount == 4 &&
			   anchors[0].surfaceName == mesh.surfaces[0].name && modelTagAnchorProblem(document.mesh(), anchors[0]).isEmpty(),
		   QStringLiteral("Follow Selection keeps an anchor"));
	// Move the plate in frames 2-3; the tag stays until it is refitted.
	ModelEdit lift;
	lift.kind = ModelEditKind::TransformFrameRange;
	lift.selection.surface = 0;
	lift.selection.vertices = {0, 1, 2, 3};
	lift.poseRange.first = 2;
	lift.poseRange.last = 3;
	lift.translation = {0, 0, 6};
	expect(document.edit(lift, &error), QStringLiteral("move the plate in two frames: ") + error);
	const auto before = moved(2, {3, 4, 5});
	expect(near(findModelTag(document.mesh(), "tag_mount", 2)->origin, before), QStringLiteral("a geometry edit leaves the baked tag alone"));
	ModelEdit refit;
	refit.kind = ModelEditKind::RefitAnchoredTags;
	expect(document.edit(refit, &error), QStringLiteral("refit anchored tags: ") + error);
	expect(near(findModelTag(document.mesh(), "tag_mount", 2)->origin, {before.x, before.y, before.z + 6}) &&
			   near(findModelTag(document.mesh(), "tag_mount", 4)->origin, moved(4, {3, 4, 5})),
		   QStringLiteral("refitting moves the tag with its components again"));
	expect(document.undo() && near(findModelTag(document.mesh(), "tag_mount", 2)->origin, before), QStringLiteral("a refit undoes in one step"));
	document.redo();
	// Anchors are saved in the editable source (version 19).
	const auto path = dir.filePath("anchored.mesh.json");
	ModelDocument reloaded;
	QFile saved(path);
	expect(document.save(path, false, &error) && reloaded.load(path, &error) && reloaded.mesh().tagAnchors == document.mesh().tagAnchors &&
			   saved.open(QIODevice::ReadOnly) && QJsonDocument::fromJson(saved.readAll()).object().value("version").toInt() == modelSourceTagAnchorVersion,
		   QStringLiteral("anchors survive a save and reload: ") + error);
	// Renaming and deleting the tag keep the anchor in step.
	ModelEdit rename;
	rename.kind = ModelEditKind::RenameTag;
	rename.selection.tag = "tag_mount";
	rename.text = "tag_hand";
	expect(document.edit(rename, &error) && document.mesh().tagAnchors.value(0).tag == "tag_hand", QStringLiteral("a renamed tag keeps its anchor"));
	// A topology change makes the anchor stale; refitting says so.
	ModelEdit cut;
	cut.kind = ModelEditKind::DeleteFaces;
	cut.selection.surface = 0;
	cut.selection.faces = {1};
	expect(document.edit(cut, &error), QStringLiteral("delete a face: ") + error);
	const auto stale = modelTagAnchorProblem(document.mesh(), document.mesh().tagAnchors.value(0));
	const bool refused = !document.edit(refit, &error) && error.contains("Follow Selection again");
	expect(!stale.isEmpty() && refused, QStringLiteral("a stale anchor is refused with a reason: ") + error);
	document.undo();
	ModelEdit remove;
	remove.kind = ModelEditKind::DeleteTag;
	remove.selection.tag = "tag_hand";
	expect(document.edit(remove, &error) && document.mesh().tagAnchors.isEmpty(), QStringLiteral("deleting the tag removes its anchor"));
	// Following without an anchor keeps none.
	auto unanchored = followEdit("tag_mount", ModelTagFollowMode::Rigid);
	unanchored.tagFollow.keepAnchor = false;
	ModelDocument plain;
	expect(plain.setMesh(mesh, &error) && plain.edit(unanchored, &error) && plain.mesh().tagAnchors.isEmpty() &&
			   !plain.edit(refit, &error) && error.contains("Follow Selection"),
		   QStringLiteral("a fit without an anchor cannot be refitted"));
}

// A part attached at the fitted tag follows the moving plate.
void checkAssembly(const QDir &dir)
{
	QString error;
	ModelDocument root, child;
	const auto rootPath = dir.filePath("assembly_plate.mesh.json"), childPath = dir.filePath("child.mesh.json");
	ModelMesh part = plateFixture();
	part.tags.clear();
	part.tagAnchors.clear();
	expect(root.setMesh(plateFixture(), &error) && root.edit(followEdit("tag_mount", ModelTagFollowMode::Rigid), &error) &&
			   root.save(rootPath, false, &error) && child.setMesh(part, &error) && child.save(childPath, false, &error),
		   QStringLiteral("save the assembly parts: ") + error);
	ModelAssembly assembly;
	ModelAssemblyPart base;
	base.id = "plate";
	base.source = "assembly_plate.mesh.json";
	base.framesPerSecond = 1;
	base.interpolate = false;
	ModelAssemblyPart attached = base;
	attached.id = "weapon";
	attached.source = "child.mesh.json";
	attached.parent = "plate";
	attached.tag = "tag_mount";
	assembly.parts = {base, attached};
	ModelAssemblyContext context;
	context.directory = dir.path();
	ModelAssemblyResolved resolved;
	if (!expect(resolveModelAssembly(assembly, context, &resolved, &error), QStringLiteral("resolve the assembly: ") + error))
		return;
	bool follows = true;
	for (int frame = 0; frame < 6; ++frame)
	{
		ModelAssemblyPose pose;
		follows = follows && sampleModelAssembly(assembly, resolved, frame, &pose, &error) && pose.partTransforms.size() == 2 &&
				  near(pose.partTransforms[1].origin, moved(frame, {3, 4, 5}), 1e-3);
	}
	expect(follows, QStringLiteral("the attached part follows the fitted tag in every frame: ") + error);
}

void checkCli(const QString &program, const QDir &dir)
{
	const auto source = dir.filePath("plate.mesh.json");
	const auto mesh = plateFixture();
	ModelDocument document;
	QString error;
	if (!expect(document.setMesh(mesh, &error) && document.save(source, false, &error), QStringLiteral("save the CLI fixture")))
		return;
	const auto output = dir.filePath("fitted.mesh.json");
	QProcess process;
	process.start(program, {"--cli", "model", "frames", source, "--operation", "follow-tag", "--tag", "tag_mount", "--surface", "0",
							"--vertices", "0,1,2,3", "--output", output, "--json", "--settings-file", dir.filePath("settings.ini")});
	const bool finished = process.waitForStarted(10000) && process.waitForFinished(120000);
	const auto json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
	ModelDocument written, expected;
	expect(finished && process.exitCode() == 0 && json.value("tagFollow").toObject().value("maxResidual").toDouble() < 1e-4 &&
			   written.load(output, &error) && expected.setMesh(mesh, &error) &&
			   expected.edit(followEdit("tag_mount", ModelTagFollowMode::Rigid), &error) && bytes(written.mesh()) == bytes(expected.mesh()),
		   QStringLiteral("model frames follow-tag writes the shared fit: ") + error);
	QProcess refit;
	refit.start(program, {"--cli", "model", "frames", output, "--operation", "refit-tags", "--output", dir.filePath("refit.mesh.json"), "--json",
						  "--settings-file", dir.filePath("settings.ini")});
	const bool done = refit.waitForStarted(10000) && refit.waitForFinished(120000);
	const auto refitJson = QJsonDocument::fromJson(refit.readAllStandardOutput()).object();
	expect(done && refit.exitCode() == 0 && refitJson.value("tagAnchors").toArray().size() == 1, QStringLiteral("model frames refit-tags refits anchors"));
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
	QTemporaryDir temporary(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("tag-follow-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	checkCore();
	checkAnchors(QDir(temporary.path()));
	checkAssembly(QDir(temporary.path()));
	if (argc > 1)
		checkCli(QString::fromLocal8Bit(argv[1]), QDir(temporary.path()));
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
