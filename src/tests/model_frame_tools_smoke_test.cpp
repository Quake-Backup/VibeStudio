#include "core/model_document.h"
#include "core/model_frame_tools.h"
#include "core/model_skeleton.h"
#include "core/model_tags.h"
#include "core/model_vertex_origins.h"
#include "tests/model_frame_tools_test_helpers.h"
#include "tests/model_mdl_test_helpers.h"
#include "tests/model_skin_provenance_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
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
bool same(ModelVec3 a, ModelVec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
// Frame `r` of `after` is frame `o` of `before`: poses, name, origin, tag and collision pose.
bool copied(const ModelMesh &before, int o, const ModelMesh &after, int r)
{
	if (before.frames[o].name != after.frames[r].name || !same(before.frames[o].origin, after.frames[r].origin))
		return false;
	for (int s = 0; s < before.surfaces.size(); ++s)
		for (int v = 0; v < before.surfaces[s].vertexCount; ++v)
			if (!same(before.surfaces[s].frames[o].positions[v], after.surfaces[s].frames[r].positions[v]) ||
				!same(before.surfaces[s].frames[o].normals[v], after.surfaces[s].frames[r].normals[v]))
				return false;
	for (const auto &tag : before.tags)
	{
		if (tag.frameIndex != o)
			continue;
		const auto *other = findModelTag(after, tag.name, r);
		if (!other || !same(tag.origin, other->origin))
			return false;
	}
	for (int b = 0; b < before.collisionBoxes.size(); ++b)
		if (!before.collisionBoxes[b].framePoses.isEmpty() &&
			!same(before.collisionBoxes[b].framePoses[o].centre, after.collisionBoxes[b].framePoses[r].centre))
			return false;
	return true;
}
bool follows(const ModelMesh &before, const ModelMesh &after, const QVector<int> &order)
{
	if (after.frames.size() != order.size() || after.tags.size() != order.size() * (before.tags.size() / before.frames.size()))
		return false;
	for (int r = 0; r < order.size(); ++r)
		if (!copied(before, order[r], after, r) || after.frames[r].index != r)
			return false;
	return true;
}
bool clipIs(const ModelMesh &mesh, const QString &name, int first, int count)
{
	for (const auto &clip : mesh.animations)
		if (clip.name == name)
			return clip.firstFrame == first && clip.frameCount == count;
	return false;
}
ModelEdit rangeEdit(ModelEditKind kind, int first, int last, int destination = -1, bool pingPong = false)
{
	ModelEdit edit;
	edit.kind = kind;
	edit.frameRange.first = first;
	edit.frameRange.last = last;
	edit.frameRange.destination = destination;
	edit.frameRange.pingPong = pingPong;
	return edit;
}
// Applies one edit through the document: the result follows `order`, and
// undo and redo are one step each.
ModelMesh runRange(const ModelMesh &before, const ModelEdit &edit, const QVector<int> &order, const QString &name)
{
	ModelDocument document;
	QString error;
	if (!expect(document.setMesh(before, &error) && document.edit(edit, &error), name + QStringLiteral(" applies: ") + error))
		return before;
	const auto after = document.mesh();
	expect(follows(before, after, order), name + QStringLiteral(" moves every frame-indexed record"));
	expect(validateEditableModel(after).isEmpty(), name + QStringLiteral(" leaves a valid source"));
	expect(document.undo() && bytes(document.mesh()) == bytes(before) && !document.canUndo(), name + QStringLiteral(" undoes in one step"));
	expect(document.redo() && bytes(document.mesh()) == bytes(after), name + QStringLiteral(" redoes"));
	return after;
}

void checkRangeOperations()
{
	const auto mesh = tests::frameRangeFixture();
	auto after = runRange(mesh, rangeEdit(ModelEditKind::MoveFrames, 1, 2, 3), {0, 3, 4, 1, 2, 5}, QStringLiteral("move"));
	expect(clipIs(after, "walk", 0, 1) && clipIs(after, "run", 1, 5) && clipIs(after, "all", 0, 6),
		   QStringLiteral("a move shrinks the clip it leaves, grows the clip it lands inside and keeps a clip holding both"));
	after = runRange(mesh, rangeEdit(ModelEditKind::MoveFrames, 3, 5, 0), {3, 4, 5, 0, 1, 2}, QStringLiteral("move a whole clip"));
	expect(clipIs(after, "run", 0, 3) && clipIs(after, "walk", 3, 3) && clipIs(after, "all", 0, 6),
		   QStringLiteral("a whole clip moves with its frames"));
	after = runRange(mesh, rangeEdit(ModelEditKind::MoveFrames, 1, 1, 2), {0, 2, 1, 3, 4, 5}, QStringLiteral("move within a clip"));
	expect(clipIs(after, "walk", 0, 3), QStringLiteral("moving a frame within its clip keeps the clip"));
	after = runRange(mesh, rangeEdit(ModelEditKind::DuplicateFrameRange, 1, 2), {0, 1, 2, 1, 2, 3, 4, 5}, QStringLiteral("duplicate"));
	expect(clipIs(after, "walk", 0, 5) && clipIs(after, "run", 5, 3) && clipIs(after, "all", 0, 8),
		   QStringLiteral("duplicating grows the clip holding the range's end and moves later clips"));
	after = runRange(mesh, rangeEdit(ModelEditKind::DeleteFrameRange, 2, 3), {0, 1, 4, 5}, QStringLiteral("delete"));
	expect(clipIs(after, "walk", 0, 2) && clipIs(after, "run", 2, 2) && clipIs(after, "all", 0, 4), QStringLiteral("deleting shrinks clips"));
	after = runRange(mesh, rangeEdit(ModelEditKind::DeleteFrameRange, 3, 5), {0, 1, 2}, QStringLiteral("delete a clip's frames"));
	expect(after.animations.size() == 2 && clipIs(after, "walk", 0, 3), QStringLiteral("a clip left without frames is removed"));
	after = runRange(mesh, rangeEdit(ModelEditKind::ReverseFrameRange, 0, 5), {5, 4, 3, 2, 1, 0}, QStringLiteral("reverse"));
	expect(after.animations.size() == 3 && clipIs(after, "walk", 0, 3), QStringLiteral("reversing keeps clip ranges"));
	after = runRange(mesh, rangeEdit(ModelEditKind::ReverseFrameRange, 0, 3, -1, true), {0, 1, 2, 3, 2, 1, 4, 5},
					 QStringLiteral("ping-pong"));
	expect(clipIs(after, "walk", 0, 3) && clipIs(after, "run", 3, 5), QStringLiteral("ping-pong frames join the clip holding the range's end"));

	ModelDocument document;
	QString error;
	expect(document.setMesh(mesh, &error), QStringLiteral("open the range fixture"));
	for (const auto &[edit, text] : {std::pair{rangeEdit(ModelEditKind::MoveFrames, 1, 2, 1), "already at that position"},
									 std::pair{rangeEdit(ModelEditKind::MoveFrames, 1, 2, 5), "destination from 0 to 4"},
									 std::pair{rangeEdit(ModelEditKind::DeleteFrameRange, 0, 5), "at least one frame"},
									 std::pair{rangeEdit(ModelEditKind::ReverseFrameRange, 2, 2), "at least two frames"},
									 std::pair{rangeEdit(ModelEditKind::ReverseFrameRange, 1, 2, -1, true), "at least three frames"},
									 std::pair{rangeEdit(ModelEditKind::DuplicateFrameRange, 4, 2), "first frame no later"},
									 std::pair{rangeEdit(ModelEditKind::DuplicateFrameRange, 0, 6), "inside the model"}})
		expect(!document.edit(edit, &error) && error.contains(QString::fromLatin1(text)) && !document.canUndo(),
			   QStringLiteral("refused: %1 (%2)").arg(QString::fromLatin1(text), error));
	auto stray = rangeEdit(ModelEditKind::DuplicateFrameRange, 0, 0);
	stray.frameRange.pingPong = true;
	expect(!document.edit(stray, &error) && error.contains("do not apply"), QStringLiteral("ping-pong applies only to reverse"));
	ModelEdit other;
	other.kind = ModelEditKind::RenameFrame;
	other.frame = 0;
	other.text = "x";
	other.frameRange.first = 0;
	expect(!document.edit(other, &error) && error.contains("Frame range settings"), QStringLiteral("range settings refuse other edits"));
	// The capacity limit is enforced before anything changes.
	auto full = tests::frameRangeFixture(modelDocumentMaxFrames - 1);
	full.collisionBoxes.clear();
	expect(document.setMesh(full, &error), QStringLiteral("open a nearly full model"));
	expect(!document.edit(rangeEdit(ModelEditKind::DuplicateFrameRange, 0, 1), &error) && error.contains("1024"),
		   QStringLiteral("duplicating past 1024 frames is refused"));
	expect(document.edit(rangeEdit(ModelEditKind::DuplicateFrameRange, 0, 0), &error) && document.mesh().frames.size() == modelDocumentMaxFrames,
		   QStringLiteral("duplicating up to 1024 frames works"));
	// Cancellation leaves the document unchanged.
	document = {};
	expect(document.setMesh(mesh, &error), QStringLiteral("reopen the range fixture"));
	ModelWorkControl cancelled;
	cancelled.cancelled = [] { return true; };
	expect(!document.edit(rangeEdit(ModelEditKind::ReverseFrameRange, 0, 5), &error, cancelled) && bytes(document.mesh()) == bytes(mesh),
		   QStringLiteral("a cancelled range edit changes nothing"));
}

void checkMdlGroups()
{
	ModelMesh mesh;
	QString error;
	const auto fixture = tests::groupedMdlFixture();
	if (!expect(importEditableModel("models/grouped.mdl", fixture.bytes, &mesh, &error) && mesh.mdl.enabled && mesh.frames.size() == 3,
				QStringLiteral("decode the grouped MDL fixture: ") + error))
		return;
	const auto near = [](float a, double b) { return std::abs(double(a) - b) < 1e-6; };
	ModelDocument document;
	expect(document.setMesh(mesh, &error), QStringLiteral("open the MDL"));
	expect(document.edit(rangeEdit(ModelEditKind::ReverseFrameRange, 0, 1), &error), QStringLiteral("reverse a timed group: ") + error);
	auto groups = document.mesh().mdl.frameGroups;
	expect(groups.size() == 2 && groups[0].intervals.size() == 2 && near(groups[0].intervals[0], .2) && near(groups[0].intervals[1], .3) &&
			   groups[1].firstFrame == 2 && groups[1].intervals.isEmpty(),
		   QStringLiteral("a reversed group keeps its members' own durations in the new order"));
	document.undo();
	expect(document.edit(rangeEdit(ModelEditKind::MoveFrames, 2, 2, 0), &error), QStringLiteral("move the single frame first: ") + error);
	groups = document.mesh().mdl.frameGroups;
	expect(groups.size() == 2 && groups[0].intervals.isEmpty() && groups[1].firstFrame == 1 && near(groups[1].intervals[1], .3),
		   QStringLiteral("the timed group moves whole"));
	document.undo();
	expect(document.edit(rangeEdit(ModelEditKind::DeleteFrameRange, 0, 0), &error), QStringLiteral("delete a group member: ") + error);
	groups = document.mesh().mdl.frameGroups;
	expect(groups.size() == 2 && groups[0].intervals.size() == 1 && near(groups[0].intervals[0], .2),
		   QStringLiteral("a deleted member takes its duration with it"));
	document.undo();
	expect(document.edit(rangeEdit(ModelEditKind::DuplicateFrameRange, 0, 1), &error), QStringLiteral("duplicate a group: ") + error);
	groups = document.mesh().mdl.frameGroups;
	expect(groups.size() == 2 && groups[0].intervals.size() == 4 && near(groups[0].intervals[3], .6),
		   QStringLiteral("copies next to their group join it with their durations"));
	ModelExportReport report;
	const auto exported = exportEditableModel(document.mesh(), "mdl", 0, &error, {}, &report);
	ModelMesh decoded;
	expect(!exported.isEmpty() && importEditableModel("models/out.mdl", exported, &decoded, &error) && decoded.mdl.frameGroups.size() == 2 &&
			   decoded.mdl.frameGroups[0].intervals.size() == 4,
		   QStringLiteral("the edited groups export and decode: ") + error);
}

void checkSkeletal()
{
	auto mesh = tests::skinFixture();
	const int bind = modelBindPoseFrame(mesh);
	auto info = mesh.frames[bind];
	info.name = QStringLiteral("edit");
	mesh.frames.append(info);
	for (auto &surface : mesh.surfaces)
		surface.frames.append(surface.frames[bind]);
	mesh.skeleton.bakedClipForFrame.append(-2);
	updateEditableModelMetadata(&mesh);
	const auto map = mesh.skeleton.bakedClipForFrame;
	if (!expect(map == QVector<int>({-1, 0, 0, -2}), QStringLiteral("the skeletal fixture holds a bind frame, a two-frame clip and an independent frame")))
		return;
	QString error;
	for (const auto &[edit, text] : {std::pair{rangeEdit(ModelEditKind::ReverseFrameRange, 1, 2), "skeletal clip wave"},
									 std::pair{rangeEdit(ModelEditKind::DeleteFrameRange, 0, 0), "bind pose"},
									 std::pair{rangeEdit(ModelEditKind::DuplicateFrameRange, 2, 3), "skeletal clip wave"},
									 std::pair{rangeEdit(ModelEditKind::MoveFrames, 3, 3, 2), "skeletal clip wave"}})
	{
		ModelDocument document;
		expect(document.setMesh(mesh, &error), QStringLiteral("open the skinned fixture"));
		const auto before = tests::skinBytes(document.mesh());
		expect(!document.edit(edit, &error) && error.contains(QString::fromLatin1(text)) && tests::skinBytes(document.mesh()) == before,
			   QStringLiteral("a range edit that moves skeletal frames is refused: ") + error);
		expect(modelFrameRangeSkeletalRefusal(mesh, edit) == error, QStringLiteral("the editor shows the same refusal"));
	}
	ModelDocument document;
	ModelEditReport report;
	expect(document.setMesh(mesh, &error) && document.edit(rangeEdit(ModelEditKind::DuplicateFrameRange, 3, 3), &error, {}, &report),
		   QStringLiteral("independent frames copy freely: ") + error);
	expect(document.mesh().skeleton.bakedClipForFrame == QVector<int>({-1, 0, 0, -2, -2}), QStringLiteral("clip frames keep their mapping"));
	for (auto it = report.skin.cbegin(); it != report.skin.cend(); ++it)
		expect(it.value().interpolated == 0 && it.value().rebound == 0 && it.value().bound == 0,
			   QStringLiteral("frame edits keep every vertex's influences"));
	// Once every surface is unbound, the frames are ordinary vertex animation.
	auto unbound = mesh;
	for (auto &surface : unbound.surfaces)
		surface.skinning = {};
	document = {};
	expect(document.setMesh(unbound, &error) && document.edit(rangeEdit(ModelEditKind::ReverseFrameRange, 1, 2), &error),
		   QStringLiteral("an unbound model reverses clip frames: ") + error);
	expect(document.mesh().skeleton.bakedClipForFrame == QVector<int>({-1, -2, -2, -2}),
		   QStringLiteral("a clip whose frames no longer play in order becomes independent"));
}

void checkNaming()
{
	expect(modelClipFrameStem("walk", 5) == "walk" && modelClipFrameName("walk", 1) == "walk1", QStringLiteral("plain stems"));
	expect(modelClipFrameStem("attack2", 9) == "attack2" && modelClipFrameName("attack2", 1) == "attack2_1" &&
			   modelFrameNameStem("attack2_1") == "attack2",
		   QStringLiteral("a stem ending in a digit keeps a separator before the number"));
	expect(modelClipFrameStem(" Run Fast! ", 3) == "Run_Fast" && modelClipFrameStem(QString::fromUtf8("\xc3\xa9t\xc3\xa9"), 3) == "t",
		   QStringLiteral("other characters become separators or go"));
	expect(modelClipFrameStem("***", 1) == "frame", QStringLiteral("an unusable name falls back to frame"));
	// Baked skeletal clips keep one stem however many digits the numbers need.
	for (const auto &clip : {QStringLiteral("a very long clip name"), QStringLiteral("attack_2"), QStringLiteral("x1234567890123456")})
	{
		QSet<QString> stems;
		bool fits = true;
		for (int frame = 0; frame < 120; ++frame)
		{
			const auto name = modelBakedFrameName(clip, frame, 120);
			fits = fits && name.size() <= 15;
			stems.insert(modelFrameNameStem(name));
		}
		expect(fits && stems.size() == 1, QStringLiteral("baked frames of %1 share one stem within 15 characters").arg(clip));
		QVector<ModelFrameInfo> frames;
		for (int frame = 0; frame < 120; ++frame)
			frames.append({frame, modelBakedFrameName(clip, frame, 120)});
		const auto rebuilt = modelAnimationsFromFrameNames(frames);
		expect(rebuilt.size() == 1 && rebuilt[0].frameCount == 120, QStringLiteral("readers rebuild baked clip %1 whole").arg(clip));
	}
	expect(modelBakedFrameName("walk2", 0, 12) == "walk2_0", QStringLiteral("a digit-ending clip name does not merge into its numbers"));

	auto mesh = tests::frameRangeFixture();
	mesh.animations = {{QStringLiteral("Walk Cycle"), 0, 3}, {QStringLiteral("run2"), 3, 3}};
	for (int f = 0; f < mesh.frames.size(); ++f)
		mesh.frames[f].name = QStringLiteral("pose%1").arg(f);
	auto checks = modelNativeClipRoundTrip(mesh);
	expect(checks.size() == 2 && !checks[0].survives && checks[0].rebuiltName == "pose" && checks[0].rebuiltLast == 5,
		   QStringLiteral("the check says what the frame names give instead"));
	expect(modelNativeClipSummary(checks).contains("0 of 2"), QStringLiteral("the summary counts surviving clips"));
	ModelDocument document;
	QString error;
	ModelEdit name;
	name.kind = ModelEditKind::RenameFramesFromClips;
	expect(document.setMesh(mesh, &error) && document.edit(name, &error), QStringLiteral("name frames from clips: ") + error);
	const auto &named = document.mesh();
	expect(named.frames[0].name == "Walk_Cycle1" && named.frames[2].name == "Walk_Cycle3" && named.frames[3].name == "run2_1" &&
			   named.animations[0].name == "Walk_Cycle" && named.animations[1].name == "run2",
		   QStringLiteral("frames take their clip's stem and number; clips take the stem"));
	checks = modelNativeClipRoundTrip(named);
	expect(checks[0].survives && checks[1].survives, QStringLiteral("named clips come back from frame names"));
	for (const auto *format : {"md3"})
	{
		ModelExportReport report;
		const auto exported = exportEditableModel(named, QString::fromLatin1(format), 0, &error, {}, &report);
		ModelMesh decoded;
		const bool read = !exported.isEmpty() && importEditableModel(QStringLiteral("models/named.") + format, exported, &decoded, &error);
		expect(read && decoded.animations.size() == 2 && decoded.animations[0].name == "Walk_Cycle" && decoded.animations[0].firstFrame == 0 &&
				   decoded.animations[0].frameCount == 3 && decoded.animations[1].name == "run2" && decoded.animations[1].frameCount == 3,
			   QStringLiteral("%1 export and decode rebuild the authored clips: %2").arg(QString::fromLatin1(format), error));
		expect(report.omissions.join(' ').indexOf("animation clip table") < 0,
			   QStringLiteral("%1 export no longer lists the clips as lost").arg(QString::fromLatin1(format)));
	}
	// Start numbers and one clip at a time; clip names kept on request.
	document = {};
	name.frameRange.clip = 1;
	name.frameRange.startNumber = 0;
	name.frameRange.renameClips = false;
	expect(document.setMesh(mesh, &error) && document.edit(name, &error), QStringLiteral("name one clip from zero: ") + error);
	expect(document.mesh().frames[3].name == "run2_0" && document.mesh().frames[0].name == "pose0" && document.mesh().animations[1].name == "run2",
		   QStringLiteral("one clip is named from its start number"));
	// Overlapping clips and neighbours that share the stem are refused.
	document = {};
	auto overlapping = tests::frameRangeFixture();
	ModelEdit all;
	all.kind = ModelEditKind::RenameFramesFromClips;
	expect(document.setMesh(overlapping, &error) && !document.edit(all, &error) && error.contains("share frames"),
		   QStringLiteral("overlapping clips are refused: ") + error);
	auto neighbours = tests::frameRangeFixture();
	neighbours.animations = {{QStringLiteral("walk"), 0, 2}};
	neighbours.frames[2].name = QStringLiteral("walk9");
	document = {};
	expect(document.setMesh(neighbours, &error) && !document.edit(all, &error) && error.contains("still not come back"),
		   QStringLiteral("a neighbour sharing the stem is refused: ") + error);
	// In-between frames inside a clip take its stem by default.
	auto walk = tests::frameRangeFixture();
	walk.animations = {{QStringLiteral("walk"), 0, 3}, {QStringLiteral("run"), 3, 3}};
	ModelEdit inbetween;
	inbetween.kind = ModelEditKind::InsertInbetweens;
	inbetween.frame = 0;
	inbetween.inbetweenCount = 2;
	document = {};
	expect(document.setMesh(walk, &error) && document.edit(inbetween, &error), QStringLiteral("insert in-betweens: ") + error);
	expect(document.mesh().frames[1].name == "walk_001" && document.mesh().frames[2].name == "walk_002",
		   QStringLiteral("in-betweens take the endpoints' stem"));
	checks = modelNativeClipRoundTrip(document.mesh());
	expect(checks.size() == 2 && checks[0].survives && checks[0].last == 4 && checks[1].survives,
		   QStringLiteral("in-betweens inside a clip keep it whole for native files"));
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
	const auto settings = dir.filePath("settings.ini");
	const auto source = dir.filePath("frames.mesh.json");
	const auto mesh = tests::frameRangeFixture();
	ModelDocument document;
	QString error;
	if (!expect(document.setMesh(mesh, &error) && document.save(source, false, &error), QStringLiteral("save the CLI fixture: ") + error))
		return;
	int code = -1;
	const QStringList common{"--json", "--settings-file", settings};
	auto json = runCli(program, QStringList{"--cli", "model", "frames", source} + common, &code);
	const auto clips = json.value("clips").toArray();
	expect(code == 0 && json.value("frames").toInt() == 6 && clips.size() == 3 && clips[0].toObject().value("survivesNativeNames").toBool() &&
			   !clips[2].toObject().value("survivesNativeNames").toBool() && clips[2].toObject().value("rebuiltName").toString() == "walk",
		   QStringLiteral("model frames lists the native clip check"));
	const auto output = dir.filePath("moved.mesh.json");
	json = runCli(program, QStringList{"--cli", "model", "frames", source, "--operation", "move", "--first", "1", "--last", "2", "--to", "3",
									   "--output", output} + common,
				  &code);
	ModelDocument written, expected;
	expect(code == 0 && json.value("written").toBool() && written.load(output, &error) && expected.setMesh(mesh, &error) &&
			   expected.edit(rangeEdit(ModelEditKind::MoveFrames, 1, 2, 3), &error) && bytes(written.mesh()) == bytes(expected.mesh()),
		   QStringLiteral("model frames move writes what the document service makes: ") + error);
	const auto named = dir.filePath("named.mesh.json");
	json = runCli(program, QStringList{"--cli", "model", "frames", source, "--operation", "name-from-clips", "--clip", "1", "--start", "0",
									   "--keep-clip-names", "--output", named} + common,
				  &code);
	expect(code == 0 && json.value("frameNames").toArray()[3].toString() == "run0", QStringLiteral("model frames names one clip's frames"));
	json = runCli(program, QStringList{"--cli", "model", "frames", source, "--operation", "ping-pong", "--first", "0", "--last", "3", "--output",
									   dir.filePath("pong.mesh.json"), "--dry-run"} + common,
				  &code);
	expect(code == 0 && json.value("frames").toInt() == 8 && !json.value("written").toBool() && !QFile::exists(dir.filePath("pong.mesh.json")),
		   QStringLiteral("a dry run reports without writing"));
	runCli(program, QStringList{"--cli", "model", "frames", source, "--operation", "move", "--first", "1", "--last", "2", "--output", output,
								"--overwrite"} + common,
		   &code);
	expect(code == 2, QStringLiteral("move without --to is a usage error"));
	runCli(program, QStringList{"--cli", "model", "frames", source, "--operation", "reverse", "--first", "2", "--last", "2", "--output",
								dir.filePath("bad.mesh.json")} + common,
		   &code);
	expect(code == 4 && !QFile::exists(dir.filePath("bad.mesh.json")), QStringLiteral("a refused edit writes nothing"));
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
	QTemporaryDir temporary(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("frame-tools-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	checkRangeOperations();
	checkMdlGroups();
	checkSkeletal();
	checkNaming();
	if (argc > 1)
		checkCli(QString::fromLocal8Bit(argv[1]), QDir(temporary.path()));
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
