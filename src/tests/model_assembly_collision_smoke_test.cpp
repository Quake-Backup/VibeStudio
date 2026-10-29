// Part collision carried into assembly pose and animation bakes (ATT-ASM-COLL-01).
#include "core/model_assembly_animation.h"
#include "core/model_collision.h"
#include "core/model_collision_hull.h"
#include "core/model_document.h"
#include "tests/model_assembly_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
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
// The same eight corners, in any order.
bool sameCorners(const ModelCollisionBox &box, const QVector<ModelVec3> &expected)
{
	const auto corners = modelCollisionCorners(box);
	for (const auto &e : expected)
		if (std::none_of(corners.cbegin(), corners.cend(), [&](ModelVec3 c) { return closeTo(c, e); }))
			return false;
	return true;
}
ModelVec3 rotateZ(ModelVec3 v, double degrees)
{
	const double r = degrees * 3.14159265358979323846 / 180, c = std::cos(r), s = std::sin(r);
	return {float(c * v.x - s * v.y), float(s * v.x + c * v.y), v.z};
}
bool sameShape(const QVector<ModelVec3> &a, const QVector<ModelVec3> &b)
{
	return std::equal(a.cbegin(), a.cend(), b.cbegin(), b.cend(),
					  [](ModelVec3 x, ModelVec3 y) { return x.x == y.x && x.y == y.y && x.z == y.z; });
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("assembly-collision-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	bool ok = true;
	QString error;

	// Each part carries one box; the child turns 90 degrees and doubles.
	auto model = tests::assemblyModel();
	ModelCollisionBox box;
	box.name = QStringLiteral("box");
	box.centre = {1, 2, 3};
	box.size = {4, 6, 8};
	box.rotation = {0, 0, 30};
	model.collisionBoxes = {box};
	ok &= expect(write(path("part.mesh.json"), serializeEditableModel(model, &error)), "part with collision written", error);
	auto recipe = tests::assemblyRecipe(path("part.mesh.json"));
	recipe.parts[1].rotation = {0, 0, 90};
	recipe.parts[1].scale = 2;
	ModelAssemblyResolved resolved;
	ok &= expect(resolveModelAssembly(recipe, {temporary.path(), {}, {}}, &resolved, &error), "resolve", error);
	ModelAssemblyPose plain, carried;
	ok &= expect(sampleModelAssembly(recipe, resolved, 0, &plain, &error) && sampleModelAssembly(recipe, resolved, 0, &carried, &error, {}, true),
				 "sample with and without collision", error);
	if (!ok)
		return 1;
	ok &= expect(plain.mesh.collisionBoxes.isEmpty() && plain.notes.join(' ').contains("Input collision boxes remain"),
				 "the default omits collision and says so");
	ok &= expect(QJsonDocument(editableModelJson(plain.mesh)).toJson() ==
					 [&] {
						 auto mesh = carried.mesh;
						 mesh.collisionBoxes.clear();
						 return QJsonDocument(editableModelJson(mesh)).toJson();
					 }(),
				 "carrying collision leaves the geometry byte for byte the same");
	ok &= expect(carried.mesh.collisionBoxes.size() == 2 && carried.mesh.collisionBoxes[0].name == "root_box" &&
					 carried.mesh.collisionBoxes[1].name == "child_box" && carried.notes.join(' ').contains("part collision"),
				 "each part's volume is named part_volume and noted");
	// Root: identity. Child: tag at (10,0,0), then Rz(90) and scale 2.
	ok &= expect(closeTo(carried.mesh.collisionBoxes[0].centre, box.centre), "the root volume stays in place");
	const auto &child = carried.mesh.collisionBoxes[1];
	ok &= expect(closeTo(child.centre, {6, 2, 6}) && closeTo(child.size, {8, 12, 16}) && std::abs(child.rotation.z - 120) < 1e-3,
				 "the child volume follows the tag, local turn and scale analytically");
	QVector<ModelVec3> expected;
	for (const auto corner : modelCollisionCorners(box))
	{
		const auto turned = rotateZ({corner.x * 2, corner.y * 2, corner.z * 2}, 90);
		expected << ModelVec3{turned.x + 10, turned.y, turned.z};
	}
	ok &= expect(sameCorners(child, expected), "the child's corners are the analytic image of the part's corners");

	// A reflected tag: the box keeps a proper rotation and the mirrored corners.
	auto reflectedRecipe = recipe;
	reflectedRecipe.parts[1].rotation = {};
	reflectedRecipe.parts[1].scale = 1;
	auto reflected = resolved;
	reflected.recipeSha256 = modelAssemblyFingerprint(reflectedRecipe);
	reflected.inputs[0].mesh.tags[0].axis[0] = -1;
	ModelAssemblyPose mirrored;
	ok &= expect(sampleModelAssembly(reflectedRecipe, reflected, 0, &mirrored, &error, {}, true), "sample through a reflected tag", error);
	expected.clear();
	for (const auto corner : modelCollisionCorners(box))
		expected << ModelVec3{10 - corner.x, corner.y, corner.z};
	ok &= expect(mirrored.mesh.collisionBoxes.size() == 2 && sameCorners(mirrored.mesh.collisionBoxes[1], expected),
				 "a reflected attachment gives the mirrored box as a proper rotation");

	// Two levels: a grandchild on the child's tag.
	auto chain = recipe;
	auto grandchild = chain.parts[1];
	grandchild.id = QStringLiteral("tip");
	grandchild.parent = QStringLiteral("child");
	grandchild.rotation = {};
	grandchild.scale = 1;
	chain.parts << grandchild;
	ModelAssemblyResolved chainResolved;
	ModelAssemblyPose chained;
	ok &= expect(resolveModelAssembly(chain, {temporary.path(), {}, {}}, &chainResolved, &error) &&
					 sampleModelAssembly(chain, chainResolved, 0, &chained, &error, {}, true) && chained.mesh.collisionBoxes.size() == 3,
				 "a two-level chain carries every part's volume", error);
	if (chained.mesh.collisionBoxes.size() == 3)
	{
		// The child's tag_link is at its local (10,0,0): turned 90 and doubled, that is (0,20,0) from (10,0,0).
		const auto turned = rotateZ({2, 4, 6}, 90); // the grandchild inherits scale 2 and the 90-degree turn
		ok &= expect(closeTo(chained.mesh.collisionBoxes[2].centre, {10 + 0 + turned.x, 20 + turned.y, turned.z}) &&
						 closeTo(chained.mesh.collisionBoxes[2].size, {8, 12, 16}),
					 "the grandchild composes both attachment levels");
	}

	// Hulls follow too.
	auto hullModel = model;
	ModelCollisionBox hull;
	ok &= expect(makeModelCollisionHull(QStringLiteral("hull"), {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}, {0, 0, 4}}, &hull, &error), "hull", error);
	hullModel.collisionBoxes = {hull};
	auto hullResolved = resolved;
	for (auto &input : hullResolved.inputs)
		input.mesh.collisionBoxes = {hull};
	ModelAssemblyPose hullPose;
	ok &= expect(sampleModelAssembly(recipe, hullResolved, 0, &hullPose, &error, {}, true) &&
					 isModelCollisionHull(hullPose.mesh.collisionBoxes.value(1)),
				 "hulls are carried with their points", error);

	// A part's hull with a shape per frame bakes each sample's own shape.
	ModelCollisionBox shapeOne;
	ok &= expect(makeModelCollisionHull(QStringLiteral("hull"), {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}, {4, 4, 4}}, &shapeOne, &error), "second shape", error);
	auto shaped = hull;
	setModelCollisionFrames(&shaped, {ModelCollisionPose{hull.centre, hull.size, hull.rotation, hull.hull},
									  ModelCollisionPose{shapeOne.centre, shapeOne.size, shapeOne.rotation, shapeOne.hull}});
	auto shapedResolved = resolved;
	for (auto &input : shapedResolved.inputs)
		input.mesh.collisionBoxes = {shaped};
	ModelAssemblyAnimation shapedBake;
	ModelAssemblyAnimationOptions shapedOptions{0, 3, 2, QStringLiteral("swing")};
	shapedOptions.includeCollision = true;
	ok &= expect(bakeModelAssemblyAnimation(recipe, shapedResolved, shapedOptions, &shapedBake, &error) &&
					 shapedBake.mesh.collisionBoxes.size() == 2 && shapedBake.mesh.collisionBoxes[0].framePoses.size() == 3,
				 "an assembly whose part hulls change shape bakes", error);
	if (shapedBake.mesh.collisionBoxes.size() == 2 && shapedBake.mesh.collisionBoxes[0].framePoses.size() == 3)
	{
		bool agrees = validateEditableModel(shapedBake.mesh).isEmpty();
		QVector<QVector<ModelVec3>> shapes;
		for (int i = 0; i < 3; ++i)
		{
			ModelAssemblyPose sample;
			ModelCollisionBox baked;
			agrees &= sampleModelAssembly(recipe, shapedResolved, i / 2.0, &sample, &error, {}, true) &&
					  sampleModelCollisionBox(shapedBake.mesh.collisionBoxes[0], i, i, 0, &baked) &&
					  sameShape(baked.hull, sample.mesh.collisionBoxes[0].hull);
			shapes << baked.hull;
		}
		ok &= expect(agrees && !sameShape(shapes[0], shapes[2]), "each baked pose keeps the shape sampled at that time");
	}

	// The 64-volume limit.
	auto crowded = resolved;
	for (auto &input : crowded.inputs)
	{
		input.mesh.collisionBoxes.clear();
		for (int i = 0; i < 40; ++i)
		{
			auto copy = box;
			copy.name = QStringLiteral("b%1").arg(i);
			input.mesh.collisionBoxes << copy;
		}
	}
	ModelAssemblyPose overflow;
	ok &= expect(!sampleModelAssembly(recipe, crowded, 0, &overflow, &error, {}, true) && error.contains("64") &&
					 sampleModelAssembly(recipe, crowded, 0, &overflow, &error),
				 "more than 64 volumes in all are refused, and only when collision is carried");

	// Animation bake: one pose per sample, schema 7 round trip.
	ModelAssemblyAnimationOptions options{0, 3, 2, QStringLiteral("swing")};
	ModelAssemblyAnimation omitted, tracked;
	ok &= expect(bakeModelAssemblyAnimation(recipe, resolved, options, &omitted, &error) && omitted.mesh.collisionBoxes.isEmpty(),
				 "the default animation bake carries no collision", error);
	options.includeCollision = true;
	ok &= expect(bakeModelAssemblyAnimation(recipe, resolved, options, &tracked, &error) && tracked.mesh.collisionBoxes.size() == 2 &&
					 tracked.mesh.collisionBoxes[1].framePoses.size() == 3,
				 "the animated bake gives each volume a track with one pose per sample", error);
	if (tracked.mesh.collisionBoxes.size() == 2 && tracked.mesh.collisionBoxes[1].framePoses.size() == 3)
	{
		bool agrees = true;
		for (int i = 0; i < 3; ++i)
		{
			ModelAssemblyPose sample;
			agrees &= sampleModelAssembly(recipe, resolved, i / 2.0, &sample, &error, {}, true) &&
					  closeTo(sample.mesh.collisionBoxes[1].centre, tracked.mesh.collisionBoxes[1].framePoses[i].centre, 1e-6);
		}
		ok &= expect(agrees, "each track pose equals the pose sampled at that time");
		ok &= expect(QJsonDocument(editableModelJson(omitted.mesh)).toJson() ==
						 [&] {
							 auto mesh = tracked.mesh;
							 mesh.collisionBoxes.clear();
							 return QJsonDocument(editableModelJson(mesh)).toJson();
						 }(),
					 "the animated geometry is unchanged by carrying collision");
		ModelMesh parsed;
		const auto json = editableModelJson(tracked.mesh);
		ok &= expect(json.value("version").toInt() >= 7 && parseEditableModel(QJsonDocument(json).toJson(), &parsed, &error) &&
						 parsed.collisionBoxes.size() == 2 && parsed.collisionBoxes[1].framePoses.size() == 3,
					 "animated collision tracks round trip in the editable source", error);
	}

	if (argc > 1)
	{
		ModelAssemblyDocument document;
		ok &= expect(document.setAssembly(recipe, temporary.path(), &error) && document.save(path("source.assembly.json"), false, &error),
					 "assembly source saved", error);
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
		ModelMesh written;
		ok &= expect(run({"model", "assembly", "source.assembly.json", "--operation", "bake", "--collision", "include", "--output",
						  "pose.mesh.json"}) &&
						 output.value("collisionVolumes").toInt() == 2 && parseEditableModel([&] {
							 QFile file(path("pose.mesh.json"));
							 return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
						 }(),
																	   &written, &error) &&
						 written.collisionBoxes.size() == 2,
					 "CLI pose bake carries collision into the editable source");
		ok &= expect(run({"model", "assembly", "source.assembly.json", "--operation", "bake-animation", "--frames", "3", "--sample-fps", "2",
						  "--collision", "include", "--output", "anim.mesh.json"}) &&
						 output.value("collisionVolumes").toInt() == 2,
					 "CLI animation bake carries collision");
		ok &= expect(run({"model", "assembly", "source.assembly.json", "--operation", "bake", "--collision", "maybe", "--output", "x.mesh.json"}, 2) &&
						 run({"model", "assembly", "source.assembly.json", "--collision", "include"}, 2),
					 "CLI refuses unknown --collision values and inspection with it");
		ok &= expect(run({"model", "assembly", "source.assembly.json", "--operation", "bake", "--output", "plain.mesh.json"}) &&
						 output.value("collisionVolumes").toInt() == 0,
					 "CLI omits collision by default");
	}
	std::cout << checks << " assembly collision checks\n";
	return ok ? 0 : 1;
}
