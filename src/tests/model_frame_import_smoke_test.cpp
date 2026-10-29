#include "core/model_document.h"
#include "core/model_frame_import.h"
#include "core/model_obj.h"
#include "core/model_tags.h"
#include "tests/model_frame_tools_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRandomGenerator>
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
bool near(ModelVec3 a, ModelVec3 b, double tolerance = 1e-4)
{
	return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.z - b.z) <= tolerance;
}
bool write(const QString &path, const QByteArray &data)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
// Four poses of the target's plate, lifted 100 units in y.
ModelMesh sequenceMesh()
{
	auto mesh = tests::frameRangeFixture(4);
	for (auto &frame : mesh.surfaces[0].frames)
		for (auto &p : frame.positions)
			p.y += 100;
	updateEditableModelMetadata(&mesh);
	return mesh;
}
// Same pose at frame `frame` for every vertex of `target` frame `to`.
bool samePositions(const ModelMesh &target, int to, const ModelMesh &source, int frame, double tolerance = 1e-4)
{
	const auto &a = target.surfaces[0].frames[to].positions, &b = source.surfaces[0].frames[frame].positions;
	if (a.size() != b.size())
		return false;
	for (int v = 0; v < a.size(); ++v)
		if (!near(a[v], b[v], tolerance))
			return false;
	return true;
}
ModelEdit importEdit(const QVector<ModelMesh> &sources, const QStringList &names)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::ImportFrames;
	edit.frameImport.sources = sources;
	edit.frameImport.sourceNames = names;
	return edit;
}

void checkSequence(const QDir &dir)
{
	QString error;
	const auto target = tests::frameRangeFixture(2);
	const auto sequence = sequenceMesh();
	QStringList written;
	expect(exportModelFrameObjSequence(sequence, 0, 3, dir.filePath("seq/walk_##.obj"), false, false, {}, &written, &error) && written.size() == 4 &&
			   written[2].endsWith("walk_02.obj"),
		   QStringLiteral("export an OBJ sequence: ") + error);
	QStringList again;
	const bool refused = !exportModelFrameObjSequence(sequence, 0, 3, dir.filePath("seq/walk_##.obj"), false, false, {}, &again, &error) &&
						 error.contains("exists");
	expect(refused, QStringLiteral("an existing sequence is not overwritten without asking: ") + error);
	// Natural order: walk_10 after walk_9.
	expect(naturalSortedModelFramePaths({"a/walk_10.obj", "a/walk_9.obj", "a/walk_1.obj"}) == QStringList({"a/walk_1.obj", "a/walk_9.obj", "a/walk_10.obj"}),
		   QStringLiteral("frame files sort naturally"));
	QVector<ModelMesh> sources;
	QStringList names;
	expect(readModelFrameSources(naturalSortedModelFramePaths(written), {}, {}, &sources, &names, &error) && sources.size() == 4 &&
			   modelFrameSourceCount(sources) == 4,
		   QStringLiteral("read the sequence: ") + error);
	ModelDocument document;
	auto edit = importEdit(sources, names);
	edit.frameImport.clipName = QStringLiteral("lift");
	edit.frameImport.clipFps = 10;
	const bool imported = document.setMesh(target, &error) && document.edit(edit, &error);
	expect(imported, QStringLiteral("import the sequence: ") + error);
	auto after = document.mesh();
	bool exact = after.frames.size() == 6;
	for (int f = 0; exact && f < 4; ++f)
		exact = samePositions(after, 2 + f, sequence, f);
	expect(exact, QStringLiteral("imported frames reproduce the exported poses"));
	expect(after.animations.last().name == "lift" && after.animations.last().firstFrame == 2 && after.animations.last().frameCount == 4 &&
			   after.frames[2].name == "lift1" && after.frames[5].name == "lift4",
		   QStringLiteral("the new clip covers the frames, which are named after it"));
	expect(findModelTag(after, "tag_mount", 4) && after.collisionBoxes[0].framePoses.size() == 6, QStringLiteral("tags and collision follow"));
	expect(document.undo() && bytes(document.mesh()) == bytes(target), QStringLiteral("undo removes the imported frames in one step"));
	// Replace instead of insert.
	auto replace = importEdit(sources.mid(0, 2), names.mid(0, 2));
	replace.frameImport.replaceFirst = 0;
	expect(document.edit(replace, &error) && document.mesh().frames.size() == 2 && samePositions(document.mesh(), 1, sequence, 1),
		   QStringLiteral("replacing overwrites frames in place: ") + error);
	document.undo();
	// Reordered vertices match through face corners, not exactly.
	auto reordered = sequence;
	auto &surface = reordered.surfaces[0];
	const QVector<int> order{2, 0, 3, 1};
	auto texCoords = surface.texCoords;
	for (int i = 0; i < 4; ++i)
		texCoords[order[i]] = surface.texCoords[i];
	surface.texCoords = texCoords;
	for (auto &frame : surface.frames)
	{
		auto positions = frame.positions, normals = frame.normals;
		for (int i = 0; i < 4; ++i)
		{
			positions[order[i]] = frame.positions[i];
			normals[order[i]] = frame.normals[i];
		}
		frame.positions = positions;
		frame.normals = normals;
	}
	for (auto &t : surface.triangles)
		t = {order[t.a], order[t.b], order[t.c]};
	surface.topologyVertices.clear();
	auto corners = importEdit({reordered}, {"reordered"});
	expect(document.edit(corners, &error) && samePositions(document.mesh(), 3, sequence, 1), QStringLiteral("corner matching survives reordering: ") + error);
	document.undo();
	corners.frameImport.match = ModelFrameMatch::Exact;
	expect(!document.edit(corners, &error) && error.contains("triangle") && bytes(document.mesh()) == bytes(target),
		   QStringLiteral("exact matching refuses reordered vertices"));
	// Nearest matching by UV and reference pose.
	auto nearest = importEdit({target}, {"same"});
	nearest.frameImport.match = ModelFrameMatch::Nearest;
	expect(document.edit(nearest, &error) && samePositions(document.mesh(), 2, target, 0), QStringLiteral("nearest matching: ") + error);
	document.undo();
	// A different triangle list is refused with no change.
	auto broken = sequence;
	broken.surfaces[0].triangles.removeLast();
	expect(!document.edit(importEdit({broken}, {"broken"}), &error) && error.contains("triangles") && bytes(document.mesh()) == bytes(target),
		   QStringLiteral("a triangle count mismatch is refused: ") + error);
	// The frame limit.
	auto full = tests::frameRangeFixture(1022);
	full.collisionBoxes.clear();
	ModelDocument large;
	expect(large.setMesh(full, &error) && !large.edit(importEdit(sources, names), &error) && error.contains("1024"),
		   QStringLiteral("importing past 1024 frames is refused"));
	// Cancellation while reading.
	ModelWorkControl cancelled;
	cancelled.cancelled = [] { return true; };
	expect(!readModelFrameSources(written, {}, {}, &sources, &names, &error, cancelled), QStringLiteral("a cancelled read stops"));
	// The model's own source or output cannot be a frame source.
	expect(!readModelFrameSources(written, {}, written[1], &sources, &names, &error) && error.contains("own source"),
		   QStringLiteral("an alias of the protected file is refused"));
	// Tags come from sources by name.
	auto tagged = sequence;
	for (auto &tag : tagged.tags)
		tag.origin.x = 77;
	const auto md3 = exportEditableModel(tagged, "md3", 0, &error);
	ModelMesh decoded;
	expect(!md3.isEmpty() && importEditableModel("models/tagged.md3", md3, &decoded, &error), QStringLiteral("export a tagged MD3: ") + error);
	expect(document.edit(importEdit({decoded}, {"tagged.md3"}), &error) && document.mesh().frames.size() == 6 &&
			   findModelTag(document.mesh(), "tag_mount", 3)->origin.x == 77 && samePositions(document.mesh(), 5, sequence, 3, .02),
		   QStringLiteral("every MD3 frame imports with its tags by name: ") + error);
}

void checkObjOptions(const QDir &dir)
{
	// A Blender-style export: Y up, a material library, a loose edge and an
	// unused vertex.
	const auto obj = dir.filePath("crate.obj");
	write(obj, "# Blender\nmtllib crate.mtl\no Crate\nv 0 0 0\nv 1 0 0\nv 1 2 0\nv 0 2 0\nv 9 9 9\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
			   "usemtl Wood\ns off\nf 1/1 2/2 3/3 4/4\nl 1 3\n");
	write(dir.filePath("crate.mtl"), "newmtl Wood\nNs 250\nKa 1 1 1\nKd 0.8 0.8 0.8\nmap_Kd textures/wood.tga\nillum 2\n");
	QFile file(obj);
	if (!expect(file.open(QIODevice::ReadOnly), QStringLiteral("read the OBJ back")))
		return;
	const auto data = file.readAll();
	ModelImportOptions options;
	options.obj.up = ModelObjUpAxis::Y;
	options.obj.scale = 2;
	ModelImportReport report;
	ModelMesh mesh;
	QString error;
	expect(importEditableModel(QFileInfo(obj).absoluteFilePath(), data, &mesh, options, &report, &error), QStringLiteral("import a Blender OBJ: ") + error);
	if (!mesh.surfaces.isEmpty())
	{
		const auto &positions = mesh.surfaces[0].frames[0].positions;
		bool converted = false;
		for (const auto &p : positions)
			converted = converted || near(p, {2, 0, 4});
		expect(converted && mesh.surfaces[0].vertexCount == 4, QStringLiteral("Y up becomes Z up and the scale applies; the unused vertex goes"));
		expect(mesh.surfaces[0].skinPaths.value(0) == "textures/wood.tga", QStringLiteral("the material's diffuse map binds the surface"));
	}
	const auto notes = report.notes.join('\n');
	expect(notes.contains("line and point") && notes.contains("no face uses") && notes.contains("shading statements") && notes.contains("Z up"),
		   QStringLiteral("the report lists what was converted or dropped: ") + notes);
	options.obj.materials = ModelObjMaterials::Refuse;
	expect(!importEditableModel(QFileInfo(obj).absoluteFilePath(), data, &mesh, options, &report, &error) && error.contains("material libraries"),
		   QStringLiteral("a refusing import stops at mtllib"));
	options.obj = {};
	options.obj.refuseLooseData = true;
	expect(!importEditableModel(QFileInfo(obj).absoluteFilePath(), data, &mesh, options, &report, &error), QStringLiteral("loose data can be refused"));
	ModelImportOptions wrong;
	wrong.obj.scale = 3;
	expect(!importEditableModel("models/x.mdl", QByteArray("IDPO"), &mesh, wrong, &report, &error) && error.contains("OBJ"),
		   QStringLiteral("OBJ settings apply only to OBJ files"));
	// Without companions the library is noted, not read.
	QStringList plain;
	const auto packaged = decodeModelObj("models/crate.obj", data, ModelObjImportOptions{}, nullptr, &plain);
	expect(packaged.error.isEmpty() && plain.join(' ').contains("was not read") && packaged.surfaces[0].skinPaths.value(0) == "Wood",
		   QStringLiteral("a library without companions keeps material names: ") + packaged.error);
	// Malformed material libraries never break the import.
	auto *random = QRandomGenerator::global();
	for (int i = 0; i < 200; ++i)
	{
		QByteArray noise;
		const int size = int(random->bounded(400));
		for (int b = 0; b < size; ++b)
			noise.append(char(random->bounded(256)));
		if (i % 2)
			noise.prepend("newmtl Wood\nmap_Kd ");
		write(dir.filePath("crate.mtl"), noise);
		ModelMesh fuzzed;
		ModelImportReport fuzzReport;
		importEditableModel(QFileInfo(obj).absoluteFilePath(), data, &fuzzed, {}, &fuzzReport, &error);
	}
	expect(true, QStringLiteral("fuzzed material libraries"));
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
	const auto source = dir.filePath("cli/target.mesh.json");
	const auto sequence = dir.filePath("cli/sequence.mesh.json");
	QDir().mkpath(dir.filePath("cli"));
	ModelDocument document, frames;
	QString error;
	if (!expect(document.setMesh(tests::frameRangeFixture(2), &error) && document.save(source, false, &error) && frames.setMesh(sequenceMesh(), &error) &&
					frames.save(sequence, false, &error),
				QStringLiteral("save the CLI fixtures")))
		return;
	const QStringList common{"--json", "--settings-file", dir.filePath("settings.ini")};
	int code = -1;
	auto json = runCli(program, QStringList{"--cli", "model", "frames", sequence, "--operation", "export-obj", "--output", dir.filePath("cli/f_###.obj")} + common, &code);
	const auto files = json.value("files").toArray();
	expect(code == 0 && files.size() == 4 && json.value("written").toBool(), QStringLiteral("export-obj writes one OBJ per frame"));
	QStringList arguments{"--cli", "model", "frames", source, "--operation", "import", "--clip-name", "lift", "--output", dir.filePath("cli/out.mesh.json")};
	for (const auto &file : files)
		arguments << "--source" << file.toString();
	json = runCli(program, arguments + common, &code);
	expect(code == 0 && json.value("importedFrames").toInt() == 4 && json.value("frames").toInt() == 6, QStringLiteral("model frames import adds the sequence"));
	json = runCli(program, QStringList{"--cli", "model", "frames", source, "--operation", "import", "--source", source, "--output",
									   dir.filePath("cli/self.mesh.json")} + common,
				  &code);
	expect(code != 0, QStringLiteral("a model cannot import frames from itself"));
	json = runCli(program, QStringList{"--cli", "model", "import", dir.filePath("crate.obj"), "--obj-up", "y", "--obj-scale", "2", "--output",
									   dir.filePath("cli/crate.mesh.json")} + common,
				  &code);
	expect(code == 0 && !json.value("notes").toArray().isEmpty(), QStringLiteral("model import lists OBJ notes"));
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
	QTemporaryDir temporary(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("frame-import-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	const QDir dir(temporary.path());
	checkSequence(dir);
	checkObjOptions(dir);
	// The fuzz loop left a broken library; write a valid one back for the CLI.
	write(dir.filePath("crate.mtl"), "newmtl Wood\nmap_Kd textures/wood.tga\n");
	if (argc > 1)
		checkCli(QString::fromLocal8Bit(argv[1]), dir);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
