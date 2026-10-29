// Quake III renderer tag interpolation in assemblies (ATT-TAG-02).
#include "core/model_assembly_animation.h"
#include "core/model_tags.h"
#include "tests/model_assembly_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <limits>
#include <random>

using namespace vibestudio;
namespace
{
int checks = 0;
bool expect(bool value, const char *message)
{
	++checks;
	if (!value)
		std::cerr << "FAIL: " << message << '\n';
	return value;
}
bool write(const QString &path, const QByteArray &data)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
// An independent statement of R_LerpTag (tr_model.c): plain double arithmetic
// over [origin, axis0, axis1, axis2], then a per-axis normalise that leaves a
// zero vector unchanged. Shares no code with core/model_pose.cpp.
struct Reference
{
	double origin[3], axis[3][3];
};
Reference lerpTag(const ModelTag &a, const ModelTag &b, double frac)
{
	Reference r{};
	const double back = 1.0 - frac;
	const float *ao = &a.origin.x, *bo = &b.origin.x;
	for (int i = 0; i < 3; ++i)
	{
		r.origin[i] = ao[i] * back + bo[i] * frac;
		for (int j = 0; j < 3; ++j)
			r.axis[j][i] = a.axis[j * 3 + i] * back + b.axis[j * 3 + i] * frac;
	}
	for (auto &v : r.axis)
	{
		const double length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
		if (length != 0)
			for (double &c : v)
				c /= length;
	}
	return r;
}
bool approx(double a, double b, double tolerance = 1e-6)
{
	return std::abs(a - b) <= tolerance * std::max(1.0, std::abs(b));
}
bool matches(const ModelTag &tag, const Reference &r)
{
	const float *o = &tag.origin.x;
	for (int i = 0; i < 3; ++i)
	{
		if (!approx(o[i], r.origin[i]))
			return false;
		for (int j = 0; j < 3; ++j)
			if (!approx(tag.axis[j * 3 + i], r.axis[j][i]))
				return false;
	}
	return true;
}
ModelTag rotationZ(double degrees, ModelVec3 origin = {})
{
	const double radians = degrees * 3.14159265358979323846 / 180;
	ModelTag tag;
	tag.name = QStringLiteral("tag_link");
	tag.origin = origin;
	const float c = float(std::cos(radians)), s = float(std::sin(radians));
	const float axis[9] = {c, s, 0, -s, c, 0, 0, 0, 1};
	std::copy(std::begin(axis), std::end(axis), tag.axis);
	return tag;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("assembly-tag-lerp-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	bool ok = true;
	QString error;

	// 1,000 random tag pairs and fractions against the independent formula.
	std::mt19937 random(20261028u);
	std::uniform_real_distribution<double> unit(-1, 1), fraction(0, 1);
	int agreed = 0;
	for (int n = 0; n < 1000; ++n)
	{
		ModelTag a, b, out;
		a.name = b.name = QStringLiteral("tag_weapon");
		for (int i = 0; i < 9; ++i)
		{
			a.axis[i] = float(unit(random));
			b.axis[i] = float(unit(random));
		}
		a.origin = {float(8 * unit(random)), float(8 * unit(random)), float(8 * unit(random))};
		b.origin = {float(8 * unit(random)), float(8 * unit(random)), float(8 * unit(random))};
		const double t = n == 0 ? 0 : n == 1 ? 1 : fraction(random);
		agreed += interpolateModelTagQuake3(a, b, t, &out) && matches(out, lerpTag(a, b, t)) ? 1 : 0;
	}
	ok &= expect(agreed == 1000, "Quake III blend agrees with the R_LerpTag formula for 1,000 random pairs");

	// Stored unit poses are exact endpoints.
	const auto start = rotationZ(0, {1, 2, 3}), quarter = rotationZ(90, {5, 6, 7});
	ModelTag out;
	ok &= expect(interpolateModelTagQuake3(start, quarter, 0, &out) && out.origin.x == 1 && out.axis[0] == 1 && out.axis[4] == 1 &&
					 interpolateModelTagQuake3(start, quarter, 1, &out) && out.origin.z == 7 && out.axis[0] == quarter.axis[0] &&
					 out.axis[1] == quarter.axis[1],
				 "stored poses are exact endpoints");
	// Halfway through a 90-degree turn the blend is normalised but skewed,
	// unlike the rigid shortest-arc pose.
	ModelTag arc;
	const double quarterTurn = std::atan(1.0); // 45 degrees in radians
	ok &= expect(interpolateModelTagQuake3(start, quarter, .5, &out) && interpolateModelTag(start, quarter, .5, &arc) &&
					 approx(out.axis[0], std::sqrt(.5)) && approx(out.axis[1], std::sqrt(.5)) && approx(arc.axis[0], std::cos(quarterTurn)),
				 "a 90-degree midpoint matches the per-axis blend");
	// A quarter of the way, the game's axis is (0.75, 0.25) normalised, not 22.5 degrees.
	ok &= expect(interpolateModelTagQuake3(start, quarter, .25, &out) && interpolateModelTag(start, quarter, .25, &arc) &&
					 approx(out.axis[0], .75 / std::sqrt(.625)) && approx(arc.axis[0], std::cos(quarterTurn / 2)) &&
					 !approx(out.axis[0], arc.axis[0], 1e-4),
				 "between frames the game blend differs from the shortest arc");
	// Antipodal and opposite-handedness pairs: the game allows them.
	auto half = start;
	const float halfTurn[9] = {-1, 0, 0, 0, -1, 0, 0, 0, 1};
	std::copy(std::begin(halfTurn), std::end(halfTurn), half.axis);
	ok &= expect(interpolateModelTagQuake3(start, half, .5, &out) && out.axis[0] == 0 && out.axis[1] == 0 && out.axis[3] == 0 &&
					 out.axis[4] == 0 && out.axis[8] == 1,
				 "an antipodal midpoint leaves zero-length axes, as VectorNormalize does");
	auto mirrored = start;
	mirrored.axis[8] = -1;
	ok &= expect(interpolateModelTagQuake3(start, mirrored, .25, &out) && !interpolateModelTag(start, mirrored, .25, &arc),
				 "opposite handedness blends in Quake III mode and is refused by shortest arc");
	auto renamed = quarter;
	renamed.name = QStringLiteral("tag_other");
	auto infinite = quarter;
	infinite.origin.x = std::numeric_limits<float>::infinity();
	ok &= expect(!interpolateModelTagQuake3(start, renamed, .5, &out) && !interpolateModelTagQuake3(start, infinite, .5, &out) &&
					 !interpolateModelTagQuake3(start, quarter, 1.5, &out),
				 "names must match and values must be finite");

	// Schema: version 4 stores the setting; older versions cannot carry it.
	const auto modelBytes = QJsonDocument(editableModelJson([] {
										auto mesh = tests::assemblyModel();
										const auto turned = rotationZ(90, {20, 0, 0});
										std::copy(std::begin(turned.axis), std::end(turned.axis), mesh.tags[1].axis);
										return mesh;
									}()))
								.toJson();
	ok &= expect(write(path("part.mesh.json"), modelBytes), "turning tag fixture written");
	auto recipe = tests::assemblyRecipe(path("part.mesh.json"));
	ModelAssembly parsed;
	ok &= expect(modelAssemblySchemaVersion(recipe) == 1 && !modelAssemblyJson(recipe).contains("tagInterpolation"),
				 "existing recipes keep their schema and stored shortest-arc behaviour");
	auto quake = recipe;
	quake.tagInterpolation = ModelTagInterpolation::Quake3Renderer;
	auto json = modelAssemblyJson(quake);
	ok &= expect(json.value("version").toInt() == 4 && json.value("tagInterpolation").toString() == "quake3" &&
					 parseModelAssembly(QJsonDocument(json).toJson(), &parsed, &error) &&
					 parsed.tagInterpolation == ModelTagInterpolation::Quake3Renderer &&
					 modelAssemblyFingerprint(parsed) == modelAssemblyFingerprint(quake),
				 "schema 4 round trips Quake III tag interpolation");
	auto older = json;
	older.insert("version", 3);
	auto unknown = json;
	unknown.insert("tagInterpolation", "slerp");
	parsed = recipe;
	ok &= expect(!parseModelAssembly(QJsonDocument(older).toJson(), &parsed, &error) &&
					 !parseModelAssembly(QJsonDocument(unknown).toJson(), &parsed, &error) &&
					 parsed.tagInterpolation == ModelTagInterpolation::ShortestArc,
				 "older schemas and unknown modes are refused without changing the output");

	// Composition: the child follows the engine basis between stored frames.
	ModelAssemblyResolved shortestInputs, quakeInputs;
	ok &= expect(resolveModelAssembly(recipe, {temporary.path(), {}, {}}, &shortestInputs, &error) &&
					 resolveModelAssembly(quake, {temporary.path(), {}, {}}, &quakeInputs, &error),
				 qPrintable(QStringLiteral("resolve both modes: ") + error));
	ModelAssemblyPose shortestPose, quakePose;
	// Root at 1 fps: at 0.25 s the parent tag is a quarter of the way to frame 1.
	ok &= expect(sampleModelAssembly(recipe, shortestInputs, .25, &shortestPose, &error) &&
					 sampleModelAssembly(quake, quakeInputs, .25, &quakePose, &error),
				 qPrintable(QStringLiteral("sample both modes: ") + error));
	if (ok)
	{
		const auto &mesh = shortestInputs.inputs[0].mesh;
		const auto expected = lerpTag(mesh.tags[0], mesh.tags[1], .25);
		// The child samples its own frame at 0.25 s * 2 fps = 0.5.
		const ModelVec3 local = interpolateModelPosition({4, 0, 0}, {4, 0, 1}, .5);
		const double x = expected.origin[0] + local.x * expected.axis[0][0] + local.y * expected.axis[1][0] + local.z * expected.axis[2][0];
		const double y = expected.origin[1] + local.x * expected.axis[0][1] + local.y * expected.axis[1][1] + local.z * expected.axis[2][1];
		const auto composed = quakePose.mesh.surfaces[1].frames[0].positions[1];
		ok &= expect(approx(composed.x, x, 1e-5) && approx(composed.y, y, 1e-5),
					 "the composed child vertex follows the engine basis analytically");
		const auto rigid = shortestPose.mesh.surfaces[1].frames[0].positions[1];
		ok &= expect(!approx(rigid.x, x, 1e-4) || !approx(rigid.y, y, 1e-4), "shortest arc remains a different, rigid pose");
		const auto normal = quakePose.mesh.surfaces[1].frames[0].normals[0];
		ok &= expect(approx(std::sqrt(double(normal.x) * normal.x + double(normal.y) * normal.y + double(normal.z) * normal.z), 1),
					 "composed normals stay unit length through a skewed basis");
		ok &= expect(quakePose.notes.join(' ').contains("Quake III renderer") && !shortestPose.notes.join(' ').contains("Quake III renderer"),
					 "notes name the active mode");
	}
	ModelAssemblyAnimation bake;
	ok &= expect(bakeModelAssemblyAnimation(quake, quakeInputs, {.25, 1, 1, QStringLiteral("q3")}, &bake, &error) &&
					 bake.mesh.surfaces[1].frames[0].positions[1].x == quakePose.mesh.surfaces[1].frames[0].positions[1].x,
				 "animation bakes use the assembly's tag interpolation");

	// Document: binding native animation selects the game blend in one undo step.
	ModelAssemblyDocument document;
	ok &= expect(document.setAssembly(recipe, temporary.path(), &error) && document.setTagInterpolation(ModelTagInterpolation::Quake3Renderer, &error) &&
					 document.assembly().tagInterpolation == ModelTagInterpolation::Quake3Renderer && document.undo() &&
					 document.assembly().tagInterpolation == ModelTagInterpolation::ShortestArc,
				 "tag interpolation is one undoable edit");
	ModelAssemblyQ3Animation binding;
	binding.lowerPart = QStringLiteral("root");
	binding.upperPart = QStringLiteral("child");
	ok &= expect(document.setQ3Animation(binding, &error) && document.assembly().tagInterpolation == ModelTagInterpolation::Quake3Renderer &&
					 document.undo() && !document.assembly().q3Animation &&
					 document.assembly().tagInterpolation == ModelTagInterpolation::ShortestArc,
				 "a new native animation binding selects the Quake III blend, and undo restores both");
	ok &= expect(document.save(path("source.assembly.json"), false, &error), "ordinary source saved");

	if (argc > 1)
	{
		QJsonObject output;
		const auto run = [&](QStringList args, int code = 0) {
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--settings-file", path("settings.ini")} + args + QStringList{"--json"});
			if (!process.waitForFinished(30000))
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
		ok &= expect(run({"model", "assembly", "source.assembly.json"}) && output.value("tagInterpolation").toString() == "shortest-arc",
					 "CLI inspection reports the stored mode");
		ok &= expect(run({"model", "assembly", "source.assembly.json", "--operation", "update", "--tag-interpolation", "quake3", "--time",
						  ".25", "--output", "quake.assembly.json"},
						 2),
					 "CLI update refuses --time");
		ok &= expect(run({"model", "assembly", "source.assembly.json", "--operation", "update", "--tag-interpolation", "quake3", "--output",
						  "quake.assembly.json"}) &&
						 output.value("tagInterpolation").toString() == "quake3" &&
						 output.value("assembly").toObject().value("version").toInt() == 4 &&
						 parseModelAssembly(read(path("quake.assembly.json")), &parsed, &error) &&
						 parsed.tagInterpolation == ModelTagInterpolation::Quake3Renderer,
					 "CLI update sets the mode without --part and writes schema 4");
		ok &= expect(run({"model", "assembly", "quake.assembly.json", "--time", ".25"}) &&
						 QJsonDocument(output.value("notes").toArray()).toJson().contains("Quake III renderer"),
					 "CLI inspection samples the stored mode");
		ok &= expect(run({"model", "assembly", "quake.assembly.json", "--operation", "update", "--tag-interpolation", "slerp", "--output",
						  "bad.assembly.json"},
						 2) &&
						 run({"model", "assembly", "quake.assembly.json", "--operation", "update", "--output", "bad.assembly.json"}, 2) &&
						 !QFileInfo::exists(path("bad.assembly.json")),
					 "CLI refuses unknown modes and an update with nothing to change");
		ok &= expect(run({"model", "assembly", "quake.assembly.json", "--operation", "update", "--part", "child", "--tag-interpolation",
						  "shortest-arc", "--scale", "2", "--output", "both.assembly.json"}) &&
						 output.value("tagInterpolation").toString() == "shortest-arc" &&
						 output.value("assembly").toObject().value("version").toInt() == 1,
					 "CLI combines a part update with the assembly-level mode");
	}
	std::cout << checks << " assembly tag interpolation checks\n";
	if (!ok)
		std::cerr << qPrintable(error) << '\n';
	return ok ? 0 : 1;
}
