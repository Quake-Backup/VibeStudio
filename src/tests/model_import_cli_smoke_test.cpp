// model import from package entries (archive, folder and portable draft) and
// --palette, as the Mesh Editor imports a package model. Every fixture is
// synthetic: the MDL and MD5 layouts are assembled from the public formats.
#include "core/model_document.h"
#include "core/package_archive.h"
#include "core/package_draft.h"
#include "core/package_staging.h"
#include "tests/model_mdl_test_helpers.h"
#include "tests/model_output_test_helpers.h"
#include "tests/package_subset_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}

// A three-joint chain with two meshes, and a two-frame clip for it.
const char kChainMesh[] = R"MD5(MD5Version 10
commandline "import fixture"
numJoints 3
numMeshes 1

joints {
	"origin"	-1 ( 0 0 0 ) ( 0 0 0 )
	"hip"	0 ( 0 0 10 ) ( 0 0 0 )
	"knee"	1 ( 0 0 20 ) ( 0 0 -0.7071067812 )
}

mesh {
	shader "models/test/body"
	numverts 4
	vert 0 ( 0 0 ) 0 1
	vert 1 ( 1 0 ) 1 1
	vert 2 ( 0 1 ) 2 2
	vert 3 ( 1 1 ) 4 1
	numtris 2
	tri 0 0 1 2
	tri 1 1 3 2
	numweights 5
	weight 0 1 1 ( 1 0 0 )
	weight 1 1 1 ( 0 1 0 )
	weight 2 1 0.25 ( 2 0 0 )
	weight 3 2 0.75 ( 1 0 0 )
	weight 4 2 1 ( 0 0 2 )
}
)MD5";

const char kWalkAnim[] = R"MD5(MD5Version 10
commandline "walk fixture"
numFrames 2
numJoints 3
frameRate 30
numAnimatedComponents 4

hierarchy {
	"origin"	-1 0 0
	"hip"	0 56 0
	"knee"	1 1 3
}

bounds {
	( -1 -1 0 ) ( 1 1 22 )
	( -1 -12 0 ) ( 1 1 11 )
}

baseframe {
	( 0 0 0 ) ( 0 0 0 )
	( 0 0 10 ) ( 0 0 0 )
	( 0 0 10 ) ( 0 0 -0.7071067812 )
}

frame 0 {
	 0 0 0 0
}

frame 1 {
	 -0.7071067812 0 0 0
}
)MD5";

bool hasClip(const ModelMesh &mesh, const QString &name)
{
	for (const auto &clip : mesh.skeleton.clips)
	{
		if (clip.name == name)
		{
			return true;
		}
	}
	return false;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	if (argc < 2)
	{
		return EXIT_FAILURE;
	}
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("model-import-cli-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	const auto path = [&](const QString &name) { return QDir(temporary.path()).filePath(name); };
	bool ok = true;
	QJsonObject output;
	int lastExit = -1;
	const auto run = [&](const QStringList &args, int code = 0)
	{
		QProcess process;
		process.setWorkingDirectory(temporary.path());
		process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--settings-file", path("settings.ini")} + args + QStringList{"--json"});
		if (!process.waitForFinished(60000))
		{
			process.kill();
			process.waitForFinished();
			return false;
		}
		const auto bytes = process.readAllStandardOutput();
		output = QJsonDocument::fromJson(bytes).object();
		lastExit = process.exitCode();
		if (lastExit != code || output.isEmpty())
		{
			std::cerr << qPrintable(args.join(' ')) << " exit=" << lastExit << '\n'
					  << bytes.constData() << process.readAllStandardError().constData() << '\n';
			return false;
		}
		return process.exitStatus() == QProcess::NormalExit;
	};

	const auto mdl = tests::groupedMdlFixture().bytes;
	const QVector<subset_test::Lump> files{{"progs/fixture.mdl", mdl},
										   {"models/md5/test/chain.md5mesh", QByteArray(kChainMesh)},
										   {"models/md5/test/walk.md5anim", QByteArray(kWalkAnim)}};
	ok &= expect(subset_test::pak(path("pak0.pak"), files), "write synthetic PAK");
	const auto pakBefore = tests::modelTestReadFile(path("pak0.pak"));

	// Archive entry: the source records the entry and reloads as a document.
	ok &= expect(run({"model", "import", path("pak0.pak"), "progs/fixture.mdl", "--output", path("fixture.mesh.json")}) &&
					 output.value("written").toBool() && output.value("sourcePath").toString() == "progs/fixture.mdl" &&
					 output.value("input").toString().endsWith("!progs/fixture.mdl"),
				 "imports an MDL entry from a PAK and reports package!entry");
	ModelDocument document;
	QString error;
	ok &= expect(document.load(path("fixture.mesh.json"), &error) && document.mesh().mdl.enabled &&
					 document.mesh().embeddedSkins.size() == 2 && document.mesh().sourcePath == "progs/fixture.mdl",
				 "the imported source reloads with its MDL skins and records the entry path");

	// --palette changes the decoded skin pixels (no palette in the package).
	ok &= expect(run({"model", "import", "--package", path("pak0.pak"), "--entry", "progs/fixture.mdl", "--palette", "doom", "--output",
					  path("doom.mesh.json")}),
				 "imports the same entry with --package, --entry and --palette");
	ModelDocument doom;
	ok &= expect(doom.load(path("doom.mesh.json"), &error) && !doom.mesh().embeddedSkins.isEmpty() &&
					 !document.mesh().embeddedSkins.isEmpty() &&
					 doom.mesh().embeddedSkins.first().image != document.mesh().embeddedSkins.first().image &&
					 doom.mesh().mdl.palette != document.mesh().mdl.palette,
				 "--palette changes the embedded skin pixels and the stored palette");
	ok &= expect(run({"model", "import", path("fixture.mdl.missing"), "--palette", "nonsense", "--output", path("bad.mesh.json")}, 2) &&
					 !QFileInfo::exists(path("bad.mesh.json")),
				 "an unknown palette family is a usage error");

	// md5mesh with its md5anim inside the archive keeps the clip.
	ok &= expect(run({"model", "import", path("pak0.pak"), "models/md5/test/chain.md5mesh", "--output", path("chain.mesh.json")}),
				 "imports an md5mesh entry");
	ModelDocument chain;
	ok &= expect(chain.load(path("chain.mesh.json"), &error) && hasClip(chain.mesh(), "walk"),
				 "the in-archive md5anim companion is kept as a clip");
	QDir().mkpath(path("lonely"));
	ok &= expect(subset_test::put(path("lonely/chain.md5mesh"), QByteArray(kChainMesh)) &&
					 run({"model", "import", path("lonely/chain.md5mesh"), "--output", path("lonely.mesh.json")}) &&
					 chain.load(path("lonely.mesh.json"), &error) && !hasClip(chain.mesh(), "walk"),
				 "a loose md5mesh without its companion has no walk clip");

	// Folder package and portable draft.
	const auto folder = path("folder");
	QDir().mkpath(folder + "/progs");
	ok &= expect(subset_test::put(folder + "/progs/fixture.mdl", mdl), "write folder package");
	ok &= expect(run({"model", "import", folder, "progs/fixture.mdl", "--output", path("from-folder.mesh.json")}) &&
					 output.value("sourcePath").toString() == "progs/fixture.mdl",
				 "imports an entry from a folder package");
	ok &= expect(run({"model", "import", folder, "progs/fixture.mdl", "--output", folder + "/progs/inside.mesh.json"}, 1) &&
					 !QFileInfo::exists(folder + "/progs/inside.mesh.json"),
				 "refuses writing the source inside its folder package");
	ok &= expect(tests::modelTestHardLink(folder + "/progs/fixture.mdl", path("folder-alias.mesh.json")) &&
					 run({"model", "import", folder, "progs/fixture.mdl", "--output", path("folder-alias.mesh.json"), "--overwrite"}, 1) &&
					 tests::modelTestReadFile(folder + "/progs/fixture.mdl") == mdl,
				 "refuses a hard-link alias of a folder package input");
	ok &= expect(tests::modelTestHardLink(path("pak0.pak"), path("pak-alias.mesh.json")) &&
					 run({"model", "import", path("pak0.pak"), "progs/fixture.mdl", "--output", path("pak-alias.mesh.json"), "--overwrite"}, 1) &&
					 tests::modelTestReadFile(path("pak0.pak")) == pakBefore,
				 "refuses a hard-link alias of the archive");
	PackageArchive archive;
	PackageStagingModel staging;
	ok &= expect(archive.load(path("pak0.pak"), &error) && staging.loadBaseArchive(archive, &error) &&
					 PackageDraft::save(path("draft.vibepackage"), &staging, false, &error),
				 "save a portable draft over the PAK");
	ok &= expect(run({"model", "import", path("draft.vibepackage"), "progs/fixture.mdl", "--output", path("from-draft.mesh.json")}),
				 "imports an entry from a portable draft");

	// Dry run and usage errors write nothing.
	ok &= expect(run({"model", "import", path("pak0.pak"), "progs/fixture.mdl", "--output", path("dry.mesh.json"), "--dry-run"}) &&
					 output.value("dryRun").toBool() && !output.value("written").toBool() && !QFileInfo::exists(path("dry.mesh.json")),
				 "a dry run writes nothing");
	ok &= expect(run({"model", "import", "--package", path("pak0.pak"), "--output", path("usage.mesh.json")}, 2) &&
					 run({"model", "import", path("pak0.pak"), "progs/fixture.mdl", "--input", path("pak0.pak"), "--output", path("usage.mesh.json")}, 2) &&
					 run({"model", "import", path("pak0.pak"), "progs/fixture.mdl", "--output", path("usage.obj")}, 2) &&
					 !QFileInfo::exists(path("usage.mesh.json")),
				 "package import usage errors exit 2 without writing");
	ok &= expect(run({"model", "import", path("pak0.pak"), "progs/missing.mdl", "--output", path("missing.mesh.json")}, 4) &&
					 !QFileInfo::exists(path("missing.mesh.json")),
				 "a missing entry is refused without writing");
	ok &= expect(tests::modelTestReadFile(path("pak0.pak")) == pakBefore, "the package is unchanged");
	if (!ok)
	{
		std::cerr << qPrintable(error) << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
