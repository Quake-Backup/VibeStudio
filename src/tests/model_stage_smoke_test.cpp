// Staging a model as the set of files the game loads with it
// (core/model_stage.h): an MD3 with detail levels, an md5mesh with its clips'
// md5anims, IQM and ASE; path and header refusals; conflicts block unless
// replacing; the material review reports a missing image until the package
// has it; one bad entry stages nothing; cancellation prepares nothing. With
// the studio as argv[1], `model stage` writes a new package (or only reports
// with --dry-run) and protects its input package.
#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_mesh.h"
#include "core/model_stage.h"
#include "core/model_skin_variants.h"
#include "core/package_archive.h"
#include "core/package_staging.h"
#include "tests/package_subset_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
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
bool writeBytes(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
const char kMesh[] = R"MD5(MD5Version 10
commandline "stage fixture"
numJoints 2
numMeshes 1

joints {
	"origin"	-1 ( 0 0 0 ) ( 0 0 0 )
	"hip"	0 ( 0 0 10 ) ( 0 0 0 )
}

mesh {
	shader "models/test/body"
	numverts 3
	vert 0 ( 0 0 ) 0 1
	vert 1 ( 1 0 ) 1 1
	vert 2 ( 0 1 ) 2 1
	numtris 1
	tri 0 0 1 2
	numweights 3
	weight 0 1 1 ( 1 0 0 )
	weight 1 1 1 ( 0 1 0 )
	weight 2 0 1 ( 0 0 2 )
}
)MD5";
const char kAnim[] = R"MD5(MD5Version 10
commandline "walk"
numFrames 2
numJoints 2
frameRate 24
numAnimatedComponents 1

hierarchy {
	"origin"	-1 0 0
	"hip"	0 4 0
}

bounds {
	( -1 -1 0 ) ( 1 1 12 )
	( -1 -1 0 ) ( 1 1 12 )
}

baseframe {
	( 0 0 0 ) ( 0 0 0 )
	( 0 0 10 ) ( 0 0 0 )
}

frame 0 {
	 10
}

frame 1 {
	 11
}
)MD5";
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("model-stage-XXXXXX")));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	const auto path = [&](const QString &name) { return QDir(temporary.path()).filePath(name); };
	bool ok = true;
	QString error;
	ModelDesign design;
	ModelDesignPart part;
	part.material = QStringLiteral("textures/test/crate");
	design.parts << part;
	const auto prop = buildModelDesignMesh(design);

	// MD3 with one generated detail level: two entries.
	ModelStageSet set;
	ModelStageRequest request;
	request.virtualPath = QStringLiteral("models/props/crate.md3");
	request.lodLevels = 1;
	ok &= expect(prepareModelStageSet(prop, request, nullptr, &set, &error) && set.entries.size() == 2 &&
					 set.entries.at(0).virtualPath == QStringLiteral("models/props/crate.md3") && set.entries.at(0).role == QStringLiteral("model") &&
					 set.entries.at(1).virtualPath == QStringLiteral("models/props/crate_1.md3") &&
					 set.entries.at(1).role == QStringLiteral("detail-level"),
				 "an MD3 stages with its detail level");
	// MD2 slots name PCX skins; an MDL needs an indexed skin, which a design has not.
	ModelDesign pcxDesign = design;
	pcxDesign.parts[0].material = QStringLiteral("models/props/crate.pcx");
	const auto pcxProp = buildModelDesignMesh(pcxDesign);
	for (const auto &suffix : {QStringLiteral("iqm"), QStringLiteral("ase"), QStringLiteral("md2")})
	{
		ModelStageSet single;
		request.virtualPath = QStringLiteral("models/props/crate.") + suffix;
		request.lodLevels = 0;
		const bool staged = prepareModelStageSet(suffix == QStringLiteral("md2") ? pcxProp : prop, request, nullptr, &single, &error) &&
							single.entries.size() == 1 && checkModelStageEntry(single.entries.first().virtualPath, single.entries.first().bytes, &error);
		if (!staged)
			std::cerr << qPrintable(suffix) << ": " << qPrintable(error) << '\n';
		ok &= expect(staged, "IQM, ASE and MD2 stage as one checked file");
	}
	ModelStageSet refused;
	request.virtualPath = QStringLiteral("progs/crate.mdl");
	ok &= expect(!prepareModelStageSet(prop, request, nullptr, &refused, &error) && refused.entries.isEmpty() && !error.isEmpty(),
				 "an MDL its target refuses stages nothing and says why");

	// md5mesh with every clip's md5anim beside it.
	QDir().mkpath(path(QStringLiteral("md5")));
	ok &= expect(writeBytes(path(QStringLiteral("md5/body.md5mesh")), kMesh) && writeBytes(path(QStringLiteral("md5/walk.md5anim")), kAnim),
				 "write the MD5 fixture");
	ModelMesh skeletal;
	QFile meshFile(path(QStringLiteral("md5/body.md5mesh")));
	ok &= expect(meshFile.open(QIODevice::ReadOnly) &&
					 importEditableModel(path(QStringLiteral("md5/body.md5mesh")), meshFile.readAll(), &skeletal, &error) &&
					 !skeletal.skeleton.clips.isEmpty(),
				 "import the skeletal fixture with its clip");
	ModelStageSet md5;
	request = {};
	request.virtualPath = QStringLiteral("models/md5/body.md5mesh");
	ok &= expect(prepareModelStageSet(skeletal, request, nullptr, &md5, &error) && md5.entries.size() == 1 + skeletal.skeleton.clips.size() &&
					 md5.entries.last().role == QStringLiteral("animation") && md5.entries.last().virtualPath.startsWith(QStringLiteral("models/md5/")) &&
					 md5.entries.last().virtualPath.endsWith(QStringLiteral(".md5anim")),
				 "an md5mesh stages with an md5anim per clip beside it");

	// Refusals.
	ok &= expect(!checkModelStageEntry(QStringLiteral("../escape.md3"), set.entries.first().bytes, &error) &&
					 !checkModelStageEntry(QStringLiteral("models/") + QString(60, QLatin1Char('x')) + QStringLiteral(".md3"), set.entries.first().bytes, &error) &&
					 !checkModelStageEntry(QStringLiteral("models/props/crate.md2"), set.entries.first().bytes, &error) &&
					 !checkModelStageEntry(QStringLiteral("models/props/crate.obj"), "v 0 0 0\n", &error),
				 "unsafe, long, mismatched and unsupported entries are refused");

	// A folder package: material review, conflicts and atomicity.
	const QDir folder(path(QStringLiteral("package")));
	QDir().mkpath(folder.filePath(QStringLiteral("models/props")));
	ok &= expect(writeBytes(folder.filePath(QStringLiteral("models/props/crate.md3")), set.entries.first().bytes), "write an existing entry");
	PackageArchive archive;
	PackageStagingModel staging;
	ok &= expect(archive.load(folder.path(), &error) && staging.loadBaseArchive(archive, &error), "open the folder package");
	request = {};
	request.virtualPath = QStringLiteral("models/props/crate.md3");
	request.lodLevels = 1;
	ModelStageSet reviewed;
	ok &= expect(prepareModelStageSet(prop, request, &archive, &reviewed, &error) && reviewed.dependencies.missingCount == 1,
				 "a missing material image is reported");
	request.requireMaterials = true;
	ok &= expect(!prepareModelStageSet(prop, request, &archive, &reviewed, &error), "a required missing material refuses the set");
	QDir().mkpath(folder.filePath(QStringLiteral("textures/test")));
	QImage image(4, 4, QImage::Format_RGB32);
	image.fill(Qt::gray);
	PackageArchive withImage;
	ok &= expect(image.save(folder.filePath(QStringLiteral("textures/test/crate.jpg"))) && withImage.load(folder.path(), &error) &&
					 prepareModelStageSet(prop, request, &withImage, &reviewed, &error) && reviewed.dependencies.missingCount == 0,
				 "the material is satisfied once the package has its image");
	// The review before staging: the model replaces a package file, its detail level is new.
	const auto planReview = reviewModelStageSet(reviewed, &staging);
	ok &= expect(planReview.entries.size() == 2 && planReview.entries.first().state == ModelStageEntryState::ReplacesPackageFile &&
					 planReview.entries.last().state == ModelStageEntryState::New && planReview.conflicts() == 1 &&
					 modelStageEntryStateId(ModelStageEntryState::ReplacesStagedFile) == QStringLiteral("replaces-staged-file"),
				 "the review says which entries replace package files");
	// Quake III skins beside an MD3: the default and one per variant.
	auto skinned = prop;
	ModelSkinVariant red;
	red.name = QStringLiteral("red");
	red.bindings = {{skinned.surfaces.first().name, QStringLiteral("textures/test/red")}};
	skinned.skinVariants = {red};
	ModelStageRequest skinRequest;
	skinRequest.virtualPath = QStringLiteral("models/props/crate.md3");
	skinRequest.lodLevels = 0;
	skinRequest.skins = true;
	ModelStageSet withSkins;
	ok &= expect(prepareModelStageSet(skinned, skinRequest, nullptr, &withSkins, &error) && withSkins.entries.size() == 3 &&
					 withSkins.entries.at(1).virtualPath == QStringLiteral("models/props/crate_default.skin") &&
					 withSkins.entries.at(2).virtualPath == QStringLiteral("models/props/crate_red.skin") &&
					 withSkins.entries.at(2).role == QStringLiteral("skin") && withSkins.entries.at(2).bytes.contains("textures/test/red"),
				 "skins stage beside the MD3 as name_default.skin and name_variant.skin");
	ok &= expect(!checkModelStageEntry(QStringLiteral("models/props/crate_default.skin"), QByteArray("no records")) &&
					 checkModelStageEntry(QStringLiteral("models/props/crate_default.skin"), QByteArray("body,textures/test/crate\n")),
				 "a skin entry must hold surface,shader records");
	const auto operations = staging.operations().size();
	ok &= expect(!stageModelSet(reviewed, &staging, false, &error) && staging.operations().size() == operations,
				 "a conflicting entry blocks the whole set without replacing");
	ok &= expect(stageModelSet(reviewed, &staging, true, &error) && staging.summary().canSave, "replacing stages the whole set");
	auto broken = reviewed;
	broken.entries.last().bytes = "not a model";
	PackageStagingModel untouched;
	ok &= expect(untouched.loadBaseArchive(archive, &error), "open a second plan");
	const auto untouchedOperations = untouched.operations().size();
	ok &= expect(!stageModelSet(broken, &untouched, true, &error) && untouched.operations().size() == untouchedOperations,
				 "one bad entry stages nothing");

	// Cancellation prepares nothing.
	ModelWorkControl cancelled;
	cancelled.cancelled = [] { return true; };
	ModelStageSet none;
	ok &= expect(!prepareModelStageSet(prop, request, nullptr, &none, &error, cancelled) && none.entries.isEmpty(), "a cancelled set prepares nothing");

	// `model stage`.
	if (argc > 1)
	{
		ModelDocument source;
		const auto sourcePath = path(QStringLiteral("crate.mesh.json"));
		ok &= expect(source.setMesh(prop, &error) && source.save(sourcePath, false, &error), "save the source");
		ok &= expect(subset_test::zip(path(QStringLiteral("base.pk3")), {{"textures/test/crate.tga", QByteArray()}}), "write a base package");
		QJsonObject output;
		const auto run = [&](const QStringList &args, int code)
		{
			QProcess process;
			process.start(QString::fromLocal8Bit(argv[1]), QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"),
																	   path(QStringLiteral("settings.ini"))} +
															   args + QStringList{QStringLiteral("--json")});
			if (!process.waitForFinished(60000))
				return false;
			output = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
			if (process.exitCode() != code)
				std::cerr << qPrintable(args.join(' ')) << " exit=" << process.exitCode() << ' ' << process.readAllStandardError().constData() << '\n';
			return process.exitCode() == code;
		};
		const QStringList stage{QStringLiteral("model"), QStringLiteral("stage"), sourcePath, QStringLiteral("--package"), path(QStringLiteral("base.pk3")),
								QStringLiteral("--as"), QStringLiteral("models/props/crate.md3"), QStringLiteral("--lod"), QStringLiteral("1")};
		ok &= expect(run(stage + QStringList{QStringLiteral("--output"), path(QStringLiteral("out.pk3")), QStringLiteral("--dry-run")}, 0) &&
						 !QFileInfo::exists(path(QStringLiteral("out.pk3"))) &&
						 output.value(QStringLiteral("set")).toObject().value(QStringLiteral("entries")).toArray().size() == 2,
					 "a dry run reports the set and writes nothing");
		ok &= expect(run(stage + QStringList{QStringLiteral("--output"), path(QStringLiteral("out.pk3"))}, 0), "model stage writes a new package");
		PackageArchive written;
		ok &= expect(written.load(path(QStringLiteral("out.pk3")), &error) &&
						 decodeModelMeshFromArchive(written, QStringLiteral("models/props/crate_1.md3")).geometryAvailable &&
						 decodeModelMeshFromArchive(written, QStringLiteral("models/props/crate.md3")).geometryAvailable,
					 "the written package holds the model and its detail level");
		ok &= expect(run(stage + QStringList{QStringLiteral("--output"), path(QStringLiteral("out.pk3"))}, 1), "an existing output needs --overwrite");
		ok &= expect(run(stage + QStringList{QStringLiteral("--output"), path(QStringLiteral("base.pk3")), QStringLiteral("--overwrite")}, 1),
					 "the input package is never the output");
		ok &= expect(run({QStringLiteral("model"), QStringLiteral("stage"), sourcePath, QStringLiteral("--package"), path(QStringLiteral("base.pk3")),
						  QStringLiteral("--as"), QStringLiteral("models/props/crate.obj"), QStringLiteral("--output"), path(QStringLiteral("x.pk3"))},
						 2) &&
						 !QFileInfo::exists(path(QStringLiteral("x.pk3"))),
					 "an unsupported format is a usage error");
	}
	if (!ok)
		std::cerr << qPrintable(error) << '\n';
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
