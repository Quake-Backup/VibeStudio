#include "cli/model_tools.h"
#include "core/model_animation_events.h"
#include "core/model_game_declarations.h"
#include "core/model_md5.h"
#include "core/package_archive.h"
#include "core/package_staging.h"
#include "tests/model_animation_test_helpers.h"
#include "tests/model_pose_clipboard_test_helpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
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
bool same(const QString &actual, const QString &expected, const char *label)
{
	if (actual == expected)
		return expect(true, label);
	std::cerr << "--- expected\n" << qPrintable(expected) << "--- actual\n" << qPrintable(actual) << "---\n";
	return expect(false, label);
}
ModelEdit eventEdit(ModelAnimationEventAction action, int clip, int animation)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::EditAnimationEvents;
	edit.animation.events.action = action;
	edit.animation.events.clip = clip;
	edit.animation.events.animation = animation;
	return edit;
}
ModelAnimationEvent event(int frame, const QString &command, const QStringList &args = {})
{
	ModelAnimationEvent result;
	result.frame = frame;
	result.command = command;
	result.args = args;
	return result;
}
// Six frames, stand1-stand3 and walk1-walk3, in two animations.
ModelMesh vertexClipFixture()
{
	auto mesh = animationFixture();
	mesh.animations.clear();
	for (int i = 0; i < 3; ++i)
	{
		ModelEdit duplicate;
		duplicate.kind = ModelEditKind::DuplicateFrame;
		duplicate.frame = 2;
		if (!applyModelEdit(&mesh, duplicate, nullptr, nullptr))
			return {};
	}
	const QStringList names{"stand1", "stand2", "stand3", "walk1", "walk2", "walk3"};
	for (int f = 0; f < names.size(); ++f)
	{
		ModelEdit rename;
		rename.kind = ModelEditKind::RenameFrame;
		rename.frame = f;
		rename.text = names[f];
		if (!applyModelEdit(&mesh, rename, nullptr, nullptr))
			return {};
	}
	mesh.animations = {{"stand", 0, 3, 10}, {"walk", 3, 3, 10}};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
QStringList tokens(const QString &text)
{
	QStringList result;
	for (auto line : text.split('\n'))
	{
		const int comment = line.indexOf("//");
		if (comment >= 0)
			line = line.left(comment);
		static const QRegularExpression token(QStringLiteral("\"[^\"]*\"|[{}]|[^\\s{}\"]+"));
		for (auto it = token.globalMatch(line); it.hasNext();)
			result << it.next().captured();
	}
	return result;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir tmp(QDir(root).filePath("declarations-XXXXXX"));
	if (!tmp.isValid())
		return 1;
	QString error;

	// Skeletal clip events.
	auto rig = symmetricRigFixture();
	ModelEdit idle;
	idle.kind = ModelEditKind::AddSkeletalClip;
	idle.skeletal.name = "idle";
	idle.skeletal.frameCount = 2;
	idle.skeletal.rate = 12;
	if (!expect(applyModelEdit(&rig, idle, nullptr, &error), "add an idle clip"))
		return 1;
	ModelDocument document;
	expect(document.setMesh(rig, &error), "open the rig");
	auto add = eventEdit(ModelAnimationEventAction::Add, 0, -1);
	add.animation.events.event = event(3, "melee", {"impLeftClaw"});
	expect(document.edit(add, &error), "add an event");
	add.animation.events.event = event(1, "sound_body", {"snd_footstep"});
	expect(document.edit(add, &error) && document.mesh().skeleton.clips[0].events.size() == 2 &&
			   document.mesh().skeleton.clips[0].events[0].frame == 1,
		   "events stay sorted by frame");
	expect(document.undo() && document.mesh().skeleton.clips[0].events.size() == 1 && document.redo(), "an event edit is one undo step");
	auto bad = eventEdit(ModelAnimationEventAction::Add, 0, -1);
	bad.animation.events.event = event(4, "sound_body", {"snd_footstep"});
	expect(!document.edit(bad, &error), "an event past the clip is refused");
	bad.animation.events.event = event(0, "two words");
	expect(!document.edit(bad, &error), "a malformed command is refused");
	bad = eventEdit(ModelAnimationEventAction::Add, -1, 0);
	bad.animation.events.event = event(0, "footstep");
	expect(!document.edit(bad, &error), "a model with joints keeps events on skeletal clips");
	auto move = eventEdit(ModelAnimationEventAction::Move, 0, -1);
	move.animation.events.index = 1;
	move.animation.events.frame = 2;
	expect(document.edit(move, &error) && document.mesh().skeleton.clips[0].events[1].frame == 2 && document.undo(), "move an event");
	ModelEdit shrink;
	shrink.kind = ModelEditKind::ResizeSkeletalClip;
	shrink.skeletal.clip = 0;
	shrink.skeletal.frameCount = 2;
	expect(!document.edit(shrink, &error) && error.contains("melee"), "shrinking a clip below its events is refused");
	const auto evented = QDir(tmp.path()).filePath("evented.mesh.json");
	ModelDocument reopened;
	expect(document.save(evented, false, &error) && reopened.load(evented, &error) &&
			   reopened.mesh().skeleton.clips[0].events == document.mesh().skeleton.clips[0].events,
		   "events survive save and reload");
	QFile file(evented);
	QByteArray bytes;
	if (file.open(QIODevice::ReadOnly))
		bytes = file.readAll();
	file.close();
	auto older = bytes;
	older.replace("\"version\":15", "\"version\":14");
	ModelMesh parsed;
	expect(bytes.contains("\"version\":15") && !parseEditableModel(older, &parsed, &error), "events need source version 15");

	// Vertex animation events follow frame edits.
	auto vertex = vertexClipFixture();
	if (!expect(vertex.isValid() && vertex.frames.size() == 6, "vertex fixture builds"))
		return 1;
	ModelDocument frames;
	expect(frames.setMesh(vertex, &error), "open the vertex model");
	auto step = eventEdit(ModelAnimationEventAction::Add, -1, 1);
	step.animation.events.event = event(1, "footstep");
	expect(frames.edit(step, &error), "add a walk event");
	ModelEdit duplicate;
	duplicate.kind = ModelEditKind::DuplicateFrame;
	duplicate.frame = 3;
	expect(frames.edit(duplicate, &error) && frames.mesh().animations[1].frameCount == 4 && frames.mesh().animations[1].events[0].frame == 2,
		   "duplicating an earlier frame of the animation moves its event");
	ModelEdit remove;
	remove.kind = ModelEditKind::DeleteFrame;
	remove.frame = 5;
	expect(!frames.edit(remove, &error) && error.contains("footstep"), "deleting an evented frame is refused");
	remove.frame = 0;
	expect(frames.edit(remove, &error) && frames.mesh().animations[1].firstFrame == 2 && frames.mesh().animations[1].events[0].frame == 2,
		   "deleting a frame before the animation keeps the event on its frame");
	ModelEdit inbetween;
	inbetween.kind = ModelEditKind::InsertInbetweens;
	inbetween.frame = 2;
	inbetween.inbetweenCount = 2;
	expect(frames.edit(inbetween, &error) && frames.mesh().frames[frames.mesh().animations[1].firstFrame + frames.mesh().animations[1].events[0].frame].name == "walk2",
		   "in-betweens keep the event on its frame");
	ModelEdit addJoint;
	addJoint.kind = ModelEditKind::AddJoint;
	addJoint.rig.name = "root";
	expect(!frames.edit(addJoint, &error), "adding joints to a model with vertex events is refused until they move to clips");

	// Doom 3 model declaration: golden text, parse back, and the model set.
	ModelDoom3DefOptions doom3;
	doom3.name = "monster_test";
	doom3.meshPath = "models/md5/test/test.md5mesh";
	doom3.sourceName = "test.mesh.json";
	const auto &mesh = document.mesh();
	const auto def = modelDoom3ModelDefText(mesh, doom3, &error);
	same(def,
		 "// Generated by VibeStudio from test.mesh.json. Edit the source and generate it again.\n\nmodel monster_test {\n"
		 "\tmesh\tmodels/md5/test/test.md5mesh\n\n"
		 "\tanim\twalk\tmodels/md5/test/walk.md5anim {\n\t\tframe 2\tsound_body snd_footstep\n\t\tframe 4\tmelee impLeftClaw\n\t}\n"
		 "\tanim\tidle\tmodels/md5/test/idle.md5anim\n}\n",
		 "the Doom 3 declaration matches its golden text");
	QStringList warnings;
	const auto decls = parseDoom3ModelDecls(def, &warnings);
	expect(warnings.isEmpty() && decls.size() == 1 && decls[0].name == "monster_test" && decls[0].anims.size() == 2 &&
			   decls[0].anims[0].second == "models/md5/test/walk.md5anim" && decls[0].animEvents[0] == mesh.skeleton.clips[0].events &&
			   decls[0].animEvents[1].isEmpty(),
		   "the declaration parses back with its frame commands");
	auto badPath = doom3;
	badPath.meshPath = "../outside.md5mesh";
	expect(modelDoom3ModelDefText(mesh, badPath, &error).isEmpty(), "paths outside the game folder are refused");
	QVector<ModelGameFile> set, again;
	expect(modelDoom3ModelSet(mesh, doom3, &set, &error) && set.size() == 4 && set[0].path == doom3.meshPath &&
			   set[1].path == "models/md5/test/walk.md5anim" && set[3].path == "def/monster_test.def",
		   "the model set holds the mesh, every clip and the declaration");
	expect(modelDoom3ModelSet(mesh, doom3, &again, &error) && again.size() == set.size(), "the set is generated again");
	bool stable = true;
	for (int i = 0; i < set.size() && i < again.size(); ++i)
		stable = stable && QCryptographicHash::hash(set[i].bytes, QCryptographicHash::Sha256) == QCryptographicHash::hash(again[i].bytes, QCryptographicHash::Sha256);
	expect(stable, "the model set's bytes are stable");
	const auto game = QDir(tmp.path()).filePath("base");
	for (const auto &item : set)
	{
		QDir().mkpath(QFileInfo(QDir(game).filePath(item.path)).absolutePath());
		QFile out(QDir(game).filePath(item.path));
		expect(out.open(QIODevice::WriteOnly) && out.write(item.bytes) == item.bytes.size(), "write the set");
	}
	const auto decoded = decodeModelMeshFile(QDir(game).filePath(doom3.meshPath));
	int walk = -1, idleClip = -1;
	for (int i = 0; i < decoded.skeleton.clips.size(); ++i)
	{
		if (decoded.skeleton.clips[i].name == "walk")
			walk = i;
		if (decoded.skeleton.clips[i].name == "idle")
			idleClip = i;
	}
	expect(decoded.error.isEmpty() && decoded.skeleton.clips.size() == 2 && walk >= 0 && idleClip >= 0 &&
			   decoded.skeleton.clips[walk].frames.size() == 4 && decoded.skeleton.clips[idleClip].frames.size() == 2,
		   "the md5mesh decodes with every clip under its declared name");
	expect(walk >= 0 && decoded.skeleton.clips[walk].events == mesh.skeleton.clips[0].events, "the declaration's frame commands come back as events");

	// QuakeC, Quake II and GZDoom.
	auto quake = vertexClipFixture();
	quake.animations[0].events = {event(1, "sound", {"misc/step.wav"})};
	ModelQuakeCFrameOptions qc;
	qc.modelDirectory = "progs/test";
	qc.sourceName = "test.mesh.json";
	same(modelQuakeCFrameMacrosText(quake, qc, &error),
		 "/*\n==============================================================================\n\nGenerated by VibeStudio from test.mesh.json. Edit "
		 "the source and generate it again.\n\n==============================================================================\n*/\n\n"
		 "$cd progs/test\n$base base\n$skin skin\n\n// stand: frames 0-2\n$frame stand1 stand2 stand3\n// event on stand2: sound misc/step.wav\n\n"
		 "// walk: frames 3-5\n$frame walk1 walk2 walk3\n",
		 "QuakeC frame macros match their golden text");
	auto invalid = quake;
	invalid.frames[4].name = "walk-2";
	expect(modelQuakeCFrameMacrosText(invalid, qc, &error).isEmpty() && error.contains("walk-2"), "frame names that are not QuakeC names are refused");
	invalid.frames[4].name = "walk1";
	expect(modelQuakeCFrameMacrosText(invalid, qc, &error).isEmpty(), "duplicate frame names are refused");
	ModelQuake2FrameOptions q2;
	q2.modelDirectory = "models/monsters/test";
	q2.sourceName = "test.mesh.json";
	same(modelQuake2FrameHeaderText(quake, q2, &error),
		 "// models/monsters/test\n\n// Generated by VibeStudio from test.mesh.json. Edit the source and generate it again.\n// 6 frames\n\n"
		 "// stand: frames 0-2\n#define FRAME_stand1            0\n#define FRAME_stand2            1\n#define FRAME_stand3            2\n\n"
		 "// walk: frames 3-5\n#define FRAME_walk1             3\n#define FRAME_walk2             4\n#define FRAME_walk3             5\n\n"
		 "#define MODEL_SCALE\t\t1.000000\n",
		 "the Quake II frame header matches its golden text");
	ModelGzdoomModeldefOptions gz;
	gz.actor = "TestModel";
	gz.path = "models/test";
	gz.model = "test.md3";
	gz.sprite = "TST";
	gz.sourceName = "test.mesh.json";
	const auto modeldef = modelGzdoomModeldefText(quake, gz, &error);
	same(modeldef,
		 "// Generated by VibeStudio from test.mesh.json. Edit the source and generate it again.\n// Sprite frames for the actor's states, by "
		 "animation:\n// stand: TST0 A-C\n// walk: TST1 A-C\nModel TestModel\n{\n\tPath \"models/test\"\n\tModel 0 \"test.md3\"\n\tScale 1 1 1\n\n"
		 "\tFrameIndex TST0 A 0 0\n\tFrameIndex TST0 B 0 1\n\tFrameIndex TST0 C 0 2\n\tFrameIndex TST1 A 0 3\n\tFrameIndex TST1 B 0 4\n"
		 "\tFrameIndex TST1 C 0 5\n}\n",
		 "the GZDoom MODELDEF matches its golden text");
	const auto words = tokens(modeldef);
	bool structured = words.size() > 4 && words[0] == "Model" && words[1] == "TestModel" && words[2] == "{" && words.last() == "}";
	int indexes = 0;
	static const QRegularExpression sprite(QStringLiteral("^[A-Z0-9]{4}$")), letter(QStringLiteral("^[A-Z\\[\\\\\\]]$"));
	for (int i = 0; structured && i < words.size(); ++i)
		if (words[i] == "FrameIndex")
		{
			bool ok = false;
			const int frame = words.value(i + 4).toInt(&ok);
			structured = sprite.match(words.value(i + 1)).hasMatch() && letter.match(words.value(i + 2)).hasMatch() && words.value(i + 3) == "0" &&
						 ok && frame >= 0 && frame < quake.frames.size();
			++indexes;
		}
	expect(structured && indexes == 6, "an independent tokenizer reads the MODELDEF block");
	gz.sprite = "toolong";
	expect(modelGzdoomModeldefText(quake, gz, &error).isEmpty(), "invalid sprite names are refused");

	// GZDoom VOXELDEF for an unanimated model.
	ModelDesign design;
	design.parts.append(ModelDesignPart{});
	auto still = buildModelDesignMesh(design);
	updateEditableModelMetadata(&still);
	ModelGzdoomVoxeldefOptions voxel;
	voxel.sprites = {"CEYE", "POSSA"};
	voxel.path = "voxels/ceye.kvx";
	voxel.scale = .5;
	voxel.angleOffset = 90;
	voxel.placedSpin = voxel.droppedSpin = 10;
	voxel.overridePalette = true;
	voxel.sourceName = "ceye.kvx";
	same(modelGzdoomVoxeldefText(still, voxel, &error),
		 "// Generated by VibeStudio from ceye.kvx. Edit the source and generate it again.\nCEYE = \"voxels/ceye.kvx\"\n{\n\tScale = 0.5\n"
		 "\tAngleOffset = 90\n\tSpin = 10\n\tOverridePalette\n}\nPOSSA = \"voxels/ceye.kvx\"\n{\n\tScale = 0.5\n\tAngleOffset = 90\n\tSpin = 10\n"
		 "\tOverridePalette\n}\n",
		 "the GZDoom VOXELDEF matches its golden text");
	ModelGzdoomVoxeldefOptions plain;
	plain.sprites = {"CEYE"};
	plain.path = "voxels/ceye.kvx";
	same(modelGzdoomVoxeldefText(still, plain, &error),
		 "// Generated by VibeStudio. Edit the source and generate it again.\nCEYE = \"voxels/ceye.kvx\"\n", "a VOXELDEF entry without options has no block");
	plain.scale = 2;
	plain.placedSpin = 5;
	expect(modelGzdoomVoxeldefText(still, plain, &error).contains("\tScale = 2.0\n\tPlacedSpin = 5\n}") &&
			   !modelGzdoomVoxeldefText(still, plain, &error).contains("DroppedSpin"),
		   "the scale keeps a decimal point and separate spins are written apart");
	auto badVoxel = voxel;
	badVoxel.sprites = {"CE"};
	expect(modelGzdoomVoxeldefText(still, badVoxel, &error).isEmpty(), "invalid VOXELDEF sprite names are refused");
	badVoxel = voxel;
	badVoxel.path = "voxels/ceye.md3";
	expect(modelGzdoomVoxeldefText(still, badVoxel, &error).isEmpty(), "a VOXELDEF path must name a .kvx file");
	expect(modelGzdoomVoxeldefText(quake, voxel, &error).isEmpty() && !error.isEmpty(), "an animated model cannot be declared as a voxel");
	expect(modelGameDeclarationTargets().contains("gzdoom-voxeldef"), "VOXELDEF is a declaration target");

	// Staging the Doom 3 model set into a package as one operation.
	const auto packageRoot = QDir(tmp.path()).filePath("package");
	QDir().mkpath(packageRoot);
	{
		QFile marker(QDir(packageRoot).filePath("readme.txt"));
		expect(marker.open(QIODevice::WriteOnly) && marker.write("package\n") > 0, "write a package fixture");
	}
	PackageArchive archive;
	PackageStagingModel staging;
	expect(archive.load(packageRoot, &error) && staging.loadBaseArchive(archive, &error), "load the package");
	const auto operations = staging.operations().size();
	if (!expect(stageModelGameFiles(set, &staging, false, &error) && staging.operations().size() == operations + set.size() &&
					staging.undoLabel().contains("Doom 3"),
				"the model set stages as one labelled package step"))
		std::cerr << qPrintable(error) << ' ' << staging.operations().size() << ' ' << operations << '\n';
	expect(!stageModelGameFiles(set, &staging, false, &error) && staging.operations().size() == operations + set.size(),
		   "staging over files already in the package is refused");
	{
		auto undone = staging;
		expect(undone.undo() && undone.operations().size() == operations, "one undo removes the whole staged set");
	}
	expect(stageModelGameFiles(set, &staging, true, &error), "replacing staged files is allowed on request");
	auto wrong = set;
	wrong.last().path = "def/monster_test.txt";
	const auto before = staging.operations().size();
	expect(!stageModelGameFiles(wrong, &staging, true, &error) && staging.operations().size() == before, "a file the set cannot hold stages nothing");
	PackageStagingModel empty;
	expect(!stageModelGameFiles(set, &empty, false, &error), "staging needs an open package");

	// CLI.
	const auto source = QDir(tmp.path()).filePath("test.mesh.json"), out = QDir(tmp.path()).filePath("out.mesh.json");
	expect(document.save(source, false, &error), "save the CLI source");
	auto result = cli::runModelEvents({"vibestudio", "--cli", "model", "events", source, "--operation", "add", "--clip", "1", "--frame", "1", "--command",
									   "sound_voice snd_idle", "--output", out, "--json"});
	if (!expect(result.exitCode == 0 && result.payload["clips"].toArray().size() == 1, "CLI adds an event"))
		std::cerr << result.exitCode << ' ' << qPrintable(result.error) << ' ' << result.payload["clips"].toArray().size() << '\n';
	result = cli::runModelEvents({"vibestudio", "--cli", "model", "events", out, "--clip", "1", "--json"});
	expect(result.exitCode == 0 && result.payload["clips"].toArray().size() == 1 &&
			   result.payload["clips"].toArray()[0].toObject()["events"].toArray()[0].toObject()["command"].toString() == "sound_voice",
		   "CLI lists a clip's events");
	expect(cli::runModelEvents({"vibestudio", "--cli", "model", "events", source, "--operation", "add", "--clip", "1", "--frame", "9", "--command",
								"footstep", "--output", out, "--overwrite"})
				   .exitCode == 4,
		   "CLI refuses an event past the clip");
	result = cli::runModelDeclarations({"vibestudio", "--cli", "model", "declarations", source, "--target", "doom3", "--name", "monster_test",
										"--mesh-path", "models/md5/test/test.md5mesh"});
	expect(result.exitCode == 0 && result.payload["text"].toString() == def,
		   "CLI prints the declaration");
	const auto setRoot = QDir(tmp.path()).filePath("set");
	result = cli::runModelDeclarations({"vibestudio", "--cli", "model", "declarations", source, "--target", "doom3", "--name", "monster_test",
										"--mesh-path", "models/md5/test/test.md5mesh", "--set", setRoot, "--dry-run"});
	expect(result.exitCode == 0 && result.payload["files"].toArray().size() == 4 && !QDir(setRoot).exists(), "CLI dry-run lists the set without writing");
	result = cli::runModelDeclarations({"vibestudio", "--cli", "model", "declarations", source, "--target", "doom3", "--name", "monster_test",
										"--mesh-path", "models/md5/test/test.md5mesh", "--set", setRoot});
	expect(result.exitCode == 0 && QFile::exists(QDir(setRoot).filePath("def/monster_test.def")) &&
			   QFile::exists(QDir(setRoot).filePath("models/md5/test/idle.md5anim")),
		   "CLI writes the whole set");
	QFile::remove(QDir(setRoot).filePath("def/monster_test.def"));
	expect(cli::runModelDeclarations({"vibestudio", "--cli", "model", "declarations", source, "--target", "doom3", "--name", "monster_test",
									  "--mesh-path", "models/md5/test/test.md5mesh", "--set", setRoot})
					   .exitCode == 1 &&
			   !QFile::exists(QDir(setRoot).filePath("def/monster_test.def")),
		   "CLI checks every destination before writing any");
	const auto qcPath = QDir(tmp.path()).filePath("frames.qc");
	ModelDocument quakeDocument;
	const auto quakeSource = QDir(tmp.path()).filePath("quake.mesh.json");
	expect(quakeDocument.setMesh(quake, &error) && quakeDocument.save(quakeSource, false, &error), "save the vertex source");
	expect(cli::runModelDeclarations({"vibestudio", "--cli", "model", "declarations", quakeSource, "--target", "quakec", "--cd", "progs/test",
									  "--output", qcPath})
					   .exitCode == 0 &&
			   QFile::exists(qcPath),
		   "CLI writes QuakeC macros to a file");
	expect(cli::runModelDeclarations({"vibestudio", "--cli", "model", "declarations", quakeSource, "--target", "quakec", "--sprite", "ABC"}).exitCode == 2,
		   "CLI refuses options of another target");
	const auto stillSource = QDir(tmp.path()).filePath("ceye.mesh.json");
	ModelDocument stillDocument;
	expect(stillDocument.setMesh(still, &error) && stillDocument.save(stillSource, false, &error), "save the unanimated source");
	result = cli::runModelDeclarations({"vibestudio", "--cli", "model", "declarations", stillSource, "--target", "gzdoom-voxeldef", "--sprites",
										"CEYE,POSSA", "--angle-offset", "90", "--spin", "10", "--override-palette", "--json"});
	expect(result.exitCode == 0 && result.payload["text"].toString().contains("POSSA = \"voxels/ceye.kvx\"") &&
			   result.payload["text"].toString().contains("\tOverridePalette"),
		   "CLI prints VOXELDEF entries");
	expect(cli::runModelDeclarations({"vibestudio", "--cli", "model", "declarations", stillSource, "--target", "gzdoom-voxeldef", "--spin", "10",
									  "--placed-spin", "5"})
				   .exitCode == 2,
		   "CLI takes one spin or the separate spins");
	expect(cli::runModelDeclarations({"vibestudio", "--cli", "model", "declarations", quakeSource, "--target", "gzdoom-voxeldef"}).exitCode == 4,
		   "CLI refuses VOXELDEF for an animated model");
	if (argc > 1)
	{
		QProcess process;
		process.start(QString::fromLocal8Bit(argv[1]), {"--cli", "model", "declarations", quakeSource, "--target", "quake2", "--json"});
		expect(process.waitForFinished(60000) && process.exitCode() == 0 &&
				   QJsonDocument::fromJson(process.readAllStandardOutput()).object()["text"].toString().contains("#define FRAME_walk3"),
			   "application dispatches model declarations");
		process.start(QString::fromLocal8Bit(argv[1]), {"--cli", "model", "events", source, "--json"});
		expect(process.waitForFinished(60000) && process.exitCode() == 0, "application dispatches model events");
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
