// Skeletal joints as assembly attachment points (ATT-SKEL-01).
#include "core/model_assembly_animation.h"
#include "core/model_skeleton.h"
#include "tests/model_assembly_test_helpers.h"

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
bool closeTo(ModelVec3 a, ModelVec3 b, double tolerance = 1e-4)
{
	return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.z - b.z) <= tolerance;
}
ModelJointMatrix matrix(std::initializer_list<float> values)
{
	ModelJointMatrix result;
	std::copy(values.begin(), values.end(), result.m);
	return result;
}
// A body whose "hand" joint is at (0,0,10) in the bind pose and in the clip's
// first frame, then turned 90 degrees about Z at (5,0,10).
ModelMesh skeletalBody(QString *error)
{
	auto mesh = tests::assemblyModel();
	mesh.tags.clear();
	mesh.frames.resize(1);
	for (auto &surface : mesh.surfaces)
		surface.frames.resize(1);
	ModelSkeleton skeleton;
	skeleton.sourceFormat = QStringLiteral("md5");
	const auto identity = matrix({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0});
	const auto raised = matrix({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 10});
	const auto turned = matrix({0, -1, 0, 5, 1, 0, 0, 0, 0, 0, 1, 10});
	skeleton.joints = {{QStringLiteral("origin"), -1, identity, 0}, {QStringLiteral("hand"), 0, raised, 0}};
	ModelSkeletalClip wave;
	wave.name = QStringLiteral("wave");
	wave.frames = {{identity, raised}, {identity, turned}};
	skeleton.clips = {wave};
	mesh.skeleton = skeleton;
	updateEditableModelMetadata(&mesh);
	if (!bakeModelSkeleton(&mesh, {}, error))
		return {};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("assembly-joint-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	bool ok = true;
	QString error;
	const auto body = skeletalBody(&error);
	ok &= expect(body.frames.size() == 3 && body.skeleton.bakedClipForFrame == QVector<int>{-1, 0, 0},
				 "the skeletal fixture bakes the bind pose and two clip frames", error);
	ok &= expect(write(path("body.mesh.json"), serializeEditableModel(body, &error)) &&
					 write(path("gun.mesh.json"), serializeEditableModel(tests::assemblyModel(), &error)),
				 "fixtures written", error);
	if (!ok)
		return 1;
	ModelAssembly recipe;
	recipe.name = QStringLiteral("Armed");
	ModelAssemblyPart bodyPart;
	bodyPart.id = QStringLiteral("body");
	bodyPart.source = path("body.mesh.json");
	bodyPart.framesPerSecond = 1;
	ModelAssemblyPart gun;
	gun.id = QStringLiteral("gun");
	gun.source = path("gun.mesh.json");
	gun.parent = bodyPart.id;
	gun.tag = QStringLiteral("joint:hand");
	gun.framesPerSecond = 0;
	recipe.parts = {bodyPart, gun};

	ModelAssemblyResolved resolved;
	ok &= expect(resolveModelAssembly(recipe, {temporary.path(), {}, {}}, &resolved, &error), "a joint attachment resolves", error);
	if (!ok)
		return 1;
	const auto vertexAt = [&](double seconds, ModelVec3 *out) {
		ModelAssemblyPose pose;
		if (!sampleModelAssembly(recipe, resolved, seconds, &pose, &error))
			return false;
		*out = pose.mesh.surfaces[1].frames[0].positions[1];
		return true;
	};
	ModelVec3 at;
	ok &= expect(vertexAt(0, &at) && closeTo(at, {4, 0, 10}), "the bind pose places the gun at the hand joint");
	ok &= expect(vertexAt(2, &at) && closeTo(at, {5, 4, 10}), "a clip frame turns and moves the gun with the joint");
	const double half = 4 * std::sqrt(.5);
	ok &= expect(vertexAt(1.5, &at) && closeTo(at, {float(2.5 + half), float(half), 10}, 1e-3),
				 "between clip frames the joint attachment blends like a tag");

	// Missing joints follow the repairable missing-tag path.
	auto missing = recipe;
	missing.parts[1].tag = QStringLiteral("joint:foot");
	ModelAssemblyResolved unresolved;
	ok &= expect(!resolveModelAssembly(missing, {temporary.path(), {}, {}}, &unresolved, &error) && error.contains("foot"),
				 "a missing joint is reported by name");
	auto flat = recipe;
	flat.parts[0].source = path("gun.mesh.json");
	ok &= expect(!resolveModelAssembly(flat, {temporary.path(), {}, {}}, &unresolved, &error), "a non-skeletal parent has no joints");

	// Schema 4 carries joint attachments; older schemas refuse them.
	const auto json = modelAssemblyJson(recipe);
	ModelAssembly parsed;
	ok &= expect(json.value("version").toInt() == 4 && parseModelAssembly(QJsonDocument(json).toJson(), &parsed, &error) &&
					 parsed.parts[1].tag == "joint:hand",
				 "schema 4 round trips a joint attachment", error);
	auto older = json;
	older.insert("version", 3);
	ok &= expect(!parseModelAssembly(QJsonDocument(older).toJson(), &parsed, &error), "schema 3 refuses a joint attachment");

	// Bakes work unchanged on frames.
	ModelAssemblyAnimation baked;
	ok &= expect(bakeModelAssemblyAnimation(recipe, resolved, {0, 3, 1, QStringLiteral("armed")}, &baked, &error) &&
					 closeTo(baked.mesh.surfaces[1].frames[2].positions[1], {5, 4, 10}),
				 "an animation bake follows the joint", error);

	if (argc > 1)
	{
		QJsonObject output;
		const auto run = [&](QStringList args, int code = 0) {
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--settings-file", path("settings.ini")} + args + QStringList{"--json"});
			if (!process.waitForFinished(60000))
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
		ok &= expect(run({"model", "assembly", "--new", "--part", "body", "--model", "body.mesh.json", "--fps", "1", "--output",
						  "armed.assembly.json"}) &&
						 run({"model", "assembly", "armed.assembly.json", "--operation", "add", "--part", "gun", "--model", "gun.mesh.json",
							  "--parent", "body", "--tag", "joint:hand", "--fps", "0", "--output", "armed.assembly.json"}) &&
						 output.value("assembly").toObject().value("version").toInt() == 4,
					 "CLI attaches a part to a joint");
		ok &= expect(run({"model", "assembly", "armed.assembly.json", "--time", "2"}) && output.value("vertices").toInt() == 6,
					 "CLI samples a joint attachment");
		ok &= expect(run({"model", "assembly", "armed.assembly.json", "--operation", "update", "--part", "gun", "--tag", "joint:foot",
						  "--output", "broken.assembly.json"},
						 4),
					 "CLI refuses a missing joint");
	}
	std::cout << checks << " assembly joint checks\n";
	return ok ? 0 : 1;
}
