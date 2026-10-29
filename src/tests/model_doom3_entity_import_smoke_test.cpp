// Doom 3 entityDef heads and attachments into an assembly recipe (ATT-SKEL-01
// follow-up): def_head on head_joint, def_attach on joint with origin and angles.
#include "core/model_assembly.h"
#include "core/model_doom3_entity_import.h"
#include "core/model_skeleton.h"
#include "core/package_archive.h"

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
namespace
{
int checks = 0;
bool expect(bool value, const char *message, const QString &detail = {})
{
	++checks;
	if (!value)
		std::cerr << "FAIL: " << message << ' ' << qPrintable(detail) << '\n';
	return value;
}
bool write(const QString &path, const QByteArray &data)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
bool closeTo(ModelVec3 a, ModelVec3 b, double tolerance = 1e-3)
{
	return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.z - b.z) <= tolerance;
}
// The body: a "Head" joint at (0,0,60), turned 45 degrees about Z (Doom 3
// rebuilds w from x, y and z).
const char kBody[] = R"MD5(MD5Version 10
commandline "body"
numJoints 2
numMeshes 1
joints {
	"origin"	-1 ( 0 0 0 ) ( 0 0 0 )
	"Head"	0 ( 0 0 60 ) ( 0 0 -0.3826834324 )
}
mesh {
	shader "models/test/body"
	numverts 3
	vert 0 ( 0 0 ) 0 1
	vert 1 ( 1 0 ) 1 1
	vert 2 ( 0 1 ) 2 1
	numtris 1
	tri 0 0 2 1
	numweights 3
	weight 0 0 1 ( 0 0 0 )
	weight 1 0 1 ( 10 0 0 )
	weight 2 0 1 ( 0 10 0 )
}
)MD5";
// A part at its own origin: (0,0,0), (4,0,0) and (0,4,0).
const char kPart[] = R"MD5(MD5Version 10
commandline "part"
numJoints 1
numMeshes 1
joints {
	"origin"	-1 ( 0 0 0 ) ( 0 0 0 )
}
mesh {
	shader "models/test/part"
	numverts 3
	vert 0 ( 0 0 ) 0 1
	vert 1 ( 1 0 ) 1 1
	vert 2 ( 0 1 ) 2 1
	numtris 1
	tri 0 0 2 1
	numweights 3
	weight 0 0 1 ( 0 0 0 )
	weight 1 0 1 ( 4 0 0 )
	weight 2 0 1 ( 0 4 0 )
}
)MD5";
const char kDefs[] = R"DEF(// Synthetic actor definitions.
model char_test {
	mesh models/md5/test/body.md5mesh
	offset ( 0 0 2 )
}
model head_test {
	mesh models/md5/test/head.md5mesh
}
entityDef actor_base {
	"model" "char_test"
	"head_joint" "head"
}
/* The actor inherits its model and head joint. */
entityDef char_test_actor {
	"inherit" "actor_base"
	"def_head" "head_test"
	"def_attach" "attach_light"
	"def_attach1" "attach_flare"
	"skin" "skins/test"
}
entityDef attach_light {
	"model" "models/md5/test/light.md5mesh"
	"joint" "Head"
	"origin" "2 0 0"
	"angles" "0 90 0"
}
entityDef attach_flare {
	"joint" "Head"
}
entityDef headless_actor {
	"model" "char_test"
	"def_head" "head_test"
}
entityDef wrong_joint_actor {
	"model" "char_test"
	"def_attach" "attach_elsewhere"
}
entityDef attach_elsewhere {
	"model" "models/md5/test/light.md5mesh"
	"joint" "Tail"
}
)DEF";
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("doom3-entity-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	bool ok = true;
	QString error;

	// The parser: entityDefs only, comments skipped, the last repeated key wins.
	QStringList warnings;
	const auto parsed = parseModelDoom3EntityDefs(QString::fromLatin1(kDefs) + "\nentityDef twice { \"a\" \"1\" \"a\" \"2\" \"empty\" \"\" }\n",
												  QStringLiteral("def/test.def"), &warnings);
	ok &= expect(parsed.size() == 8 && parsed[1].name == "char_test_actor" && parsed[1].value("def_attach1") == "attach_flare" &&
					 parsed[1].value("INHERIT") == "actor_base" && parsed[7].value("a") == "2" && !parsed[7].value("empty").isNull() &&
					 parsed[7].value("missing").isNull() && warnings.isEmpty(),
				 "entityDefs parse with case-insensitive keys, and model blocks and comments are skipped", warnings.join('\n'));

	ok &= expect(QDir().mkpath(path("pkg/def")) && QDir().mkpath(path("pkg/models/md5/test")) && write(path("pkg/def/test.def"), kDefs) &&
					 write(path("pkg/models/md5/test/body.md5mesh"), kBody) && write(path("pkg/models/md5/test/head.md5mesh"), kPart) &&
					 write(path("pkg/models/md5/test/light.md5mesh"), kPart),
				 "package written");
	auto archive = std::make_shared<PackageArchive>();
	ok &= expect(archive->load(path("pkg"), &error), "package opens", error);
	QVector<ModelDoom3EntityListing> entities;
	ok &= expect(listModelDoom3Entities(*archive, &entities, &error) && entities.size() == 3 && entities[0].name == "char_test_actor" &&
					 entities[0].head == "head_test" && entities[0].headJoint == "head" && entities[0].attachments == 2,
				 "actors with a head or attachments are listed, with inherited keys", error);

	ModelDoom3EntityImport imported;
	ok &= expect(importModelDoom3Entity(*archive, "char_test_actor", &imported, &error) && imported.assembly.parts.size() == 3,
				 "the actor imports with its body, head and one modelled attachment", error);
	if (!ok)
		return 1;
	const auto &parts = imported.assembly.parts;
	ok &= expect(parts[0].source == "models/md5/test/body.md5mesh" && parts[0].sourceKind == ModelAssemblySource::Package &&
					 parts[1].source == "models/md5/test/head.md5mesh" && parts[1].parent == parts[0].id && parts[1].tag == "joint:Head" &&
					 parts[2].tag == "joint:Head" && parts[2].id == "attach_light",
				 "parts come from the package on the joint's exact name");
	const auto notes = imported.notes.join(' ');
	ok &= expect(notes.contains("attach_flare") && notes.contains("offset") && notes.contains("Skin") && notes.contains("bind pose"),
				 "notes cover the left-out attachment, the model offset, skins and the bind pose", notes);

	// Sample the recipe: the head keeps the entity's axes at the joint, and
	// the attachment sits at the joint plus the model-space origin, turned by
	// its angles within the joint's axes.
	ModelAssemblyResolved resolved;
	ModelAssemblyPose pose;
	ok &= expect(resolveModelAssembly(imported.assembly, {temporary.path(), archive, {}}, &resolved, &error) &&
					 sampleModelAssembly(imported.assembly, resolved, 0, &pose, &error),
				 "the recipe resolves from the package and samples", error);
	const auto body = decodeModelMeshFromArchive(*archive, QStringLiteral("models/md5/test/body.md5mesh"));
	const auto bind = modelSkeletonBindPose(body.skeleton);
	const int head = modelJointIndex(body.skeleton, QStringLiteral("Head"));
	if (ok && head >= 0 && pose.surfaceParts.size() == 3)
	{
		const auto &m = bind[head].m;
		const ModelVec3 joint{m[3], m[7], m[11]};
		const auto at = [&](int part, int vertex) {
			for (int s = 0; s < pose.surfaceParts.size(); ++s)
				if (pose.surfaceParts[s] == part)
					return pose.mesh.surfaces[s].frames[0].positions.value(vertex);
			return ModelVec3{1e9f, 1e9f, 1e9f};
		};
		ok &= expect(std::abs(m[0] - std::cos(3.14159265358979 / 4)) < 1e-4 && std::abs(m[4]) > .5, "the head joint is turned");
		ok &= expect(closeTo(at(1, 1), {joint.x + 4, joint.y, joint.z}) && closeTo(at(1, 2), {joint.x, joint.y + 4, joint.z}),
					 "the head keeps the actor's axes at the joint, as SetupHead places it");
		// angles 0 90 0: yaw 90 turns +X to +Y within the joint, then the joint turns it.
		const ModelVec3 turned{float(m[1] * 4), float(m[5] * 4), float(m[9] * 4)}; // the joint's Y axis, times 4
		ok &= expect(closeTo(at(2, 0), {joint.x + 2, joint.y, joint.z}) &&
						 closeTo(at(2, 1), {joint.x + 2 + turned.x, joint.y + turned.y, joint.z + turned.z}),
					 "the attachment sits at joint + origin, turned by its angles within the joint, as Attach places it");
	}
	else
		ok &= expect(false, "the head joint and three parts are present");

	// Refusals: Doom 3 stops on a head without head_joint and a missing joint.
	ok &= expect(!importModelDoom3Entity(*archive, "headless_actor", &imported, &error) && error.contains("head_joint"),
				 "a head without head_joint is refused", error);
	ok &= expect(!importModelDoom3Entity(*archive, "wrong_joint_actor", &imported, &error) && error.contains("Tail"),
				 "an attachment on a missing joint is refused", error);
	ok &= expect(!importModelDoom3Entity(*archive, "attach_flare", &imported, &error) && error.contains("md5mesh"),
				 "an entity without an md5mesh body is refused", error);
	ok &= expect(!importModelDoom3Entity(*archive, "nobody", &imported, &error), "an unknown entity is refused");

	if (argc > 1)
	{
		QJsonObject output;
		const auto run = [&](QStringList args, int code = 0) {
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--settings-file", path("settings.ini")} + args + QStringList{"--json"});
			if (!process.waitForFinished(120000))
			{
				process.kill();
				process.waitForFinished();
				return false;
			}
			output = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
			if (process.exitCode() != code)
				std::cerr << qPrintable(QString::fromUtf8(process.readAllStandardError())) << '\n';
			return process.exitStatus() == QProcess::NormalExit && process.exitCode() == code;
		};
		ok &= expect(run({"model", "assembly", "--operation", "doom3-entities", "--package", "pkg"}) &&
						 output.value("entities").toArray().size() == 3,
					 "CLI lists the actors");
		ok &= expect(run({"model", "assembly", "--new", "--from-def", "char_test_actor", "--package", "pkg", "--output", "actor.assembly.json"}) &&
						 output.value("doom3Entity").toObject().value("parts").toArray().size() == 3 && QFileInfo::exists(path("actor.assembly.json")),
					 "CLI opens an actor as a new assembly");
		ModelAssembly saved;
		QFile file(path("actor.assembly.json"));
		ok &= expect(file.open(QIODevice::ReadOnly) && parseModelAssembly(file.readAll(), &saved, &error) && saved.parts.size() == 3 &&
						 saved.parts[1].tag == "joint:Head",
					 "the saved recipe keeps the joint attachments", error);
		ok &= expect(run({"model", "assembly", "--new", "--from-def", "char_test_actor", "--output", "x.assembly.json"}, 2) &&
						 run({"model", "assembly", "--new", "--from-def", "headless_actor", "--package", "pkg", "--output", "x.assembly.json"}, 4),
					 "CLI needs --package and refuses what the game refuses");
	}
	std::cout << checks << " Doom 3 entity import checks\n";
	return ok ? 0 : 1;
}
