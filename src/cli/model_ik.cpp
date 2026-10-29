#include "cli/model_tools.h"
#include "core/model_document.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>

namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelIkCli)
};
ModelToolsCliResult fail(int code, const QString &message)
{
	ModelToolsCliResult r;
	r.exitCode = code;
	r.error = message;
	return r;
}
bool integer(const QString &s, int *n)
{
	bool ok = false;
	*n = s.toInt(&ok);
	return ok && *n >= 0 && s == QString::number(*n);
}
bool vector(const QString &s, ModelVec3 *result)
{
	const auto parts = s.split(',');
	if (parts.size() != 3)
		return false;
	float values[3];
	for (int i = 0; i < 3; ++i)
	{
		bool ok = false;
		const double n = parts[i].toDouble(&ok);
		if (!ok || !std::isfinite(n) || std::abs(n) > 1000000)
			return false;
		values[i] = float(n);
	}
	*result = {values[0], values[1], values[2]};
	return true;
}
} // namespace
ModelToolsCliResult runModelIk(const QStringList &args)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite", "--nearest-reachable", "--follow-end",
							  "--limits"};
	const QSet<QString> options{"--input",	"--output", "--clip",		   "--first",  "--last",		"--end-joint",
								"--target", "--pole",	"--settings-file", "--locale", "--catalog-root", "--root-joint", "--solver",
								"--iterations", "--tolerance", "--target-joint", "--target-offset", "--targets", "--target-keys", "--pole-keys"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	for (int i = 1; i < args.size(); ++i)
	{
		const auto arg = args[i];
		if (!arg.startsWith('-'))
		{
			positional.append(arg);
			continue;
		}
		const int equal = arg.indexOf('=');
		const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key))
			return fail(2, Text::tr("Repeated option: %1.").arg(key));
		seen.insert(key);
		if (flags.contains(key) && equal < 0)
			continue;
		if (!options.contains(key))
			return fail(2, Text::tr("Unknown or invalid option: %1.").arg(key));
		const auto value = equal < 0 ? args.value(++i) : arg.mid(equal + 1);
		if (value.isEmpty() || value.startsWith("--"))
			return fail(2, Text::tr("Option %1 requires a value.").arg(key));
		values[key] = value;
	}
	const bool input = seen.contains("--input");
	if (positional.size() != (input ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "ik")
		return fail(2, Text::tr("Provide one editable model source, positionally or with --input."));
	if (!values.value("--output").endsWith(".mesh.json", Qt::CaseInsensitive))
		return fail(2, Text::tr("Inverse kinematics requires --output ending in .mesh.json."));
	ModelEdit edit;
	auto &o = edit.ik;
	// Chain IK (--root-joint, --solver fabrik|ccd) or the analytic two-bone solve.
	const auto solver = values.value("--solver", seen.contains("--root-joint") ? "fabrik" : "two-bone");
	if (solver != "two-bone" && solver != "fabrik" && solver != "ccd")
		return fail(2, Text::tr("Solver must be two-bone, fabrik or ccd."));
	const bool chained = solver != "two-bone";
	edit.kind = chained ? ModelEditKind::SolveChainIk : ModelEditKind::SolveTwoBoneIk;
	if (chained != seen.contains("--root-joint"))
		return fail(2, Text::tr("Chain solvers (fabrik, ccd) need --root-joint; the two-bone solve takes none."));
	for (const auto &key : {"--iterations", "--tolerance", "--limits"})
		if (!chained && seen.contains(key))
			return fail(2, Text::tr("Option %1 applies only to chain solvers.").arg(key));
	if (!integer(values.value("--clip"), &o.clip) || !integer(values.value("--first"), &o.first) ||
		!integer(values.value("--last"), &o.last) || values.value("--end-joint").isEmpty())
		return fail(2, Text::tr("Provide --clip, --first and --last as nonnegative integers, and --end-joint as an exact joint name."));
	o.endJoint = values["--end-joint"];
	o.rootJoint = values.value("--root-joint");
	o.solver = solver == "ccd" ? ModelIkSolver::Ccd : ModelIkSolver::Fabrik;
	const int sources = int(seen.contains("--target")) + int(seen.contains("--target-joint")) + int(seen.contains("--targets")) +
						int(seen.contains("--target-keys"));
	if (sources != 1)
		return fail(2, Text::tr("Give exactly one target: --target x,y,z, --target-joint name (with optional --target-offset x,y,z), --targets "
								"file.json or --target-keys \"sample:x,y,z; ...\"."));
	if (seen.contains("--pole") && seen.contains("--pole-keys"))
		return fail(2, Text::tr("Give either --pole or --pole-keys."));
	QString keyProblem;
	if ((seen.contains("--target-keys") && !parseModelIkKeys(values.value("--target-keys"), &o.targetKeys, &keyProblem)) ||
		(seen.contains("--pole-keys") && !parseModelIkKeys(values.value("--pole-keys"), &o.poleKeys, &keyProblem)))
		return fail(2, keyProblem);
	if ((seen.contains("--target") && !vector(values.value("--target"), &o.target)) ||
		(seen.contains("--target-offset") && !vector(values.value("--target-offset"), &o.targetOffset)) ||
		(seen.contains("--pole") && !vector(values.value("--pole"), &o.pole)))
		return fail(2, Text::tr("Provide --target, --target-offset and --pole as x,y,z: finite model-space coordinates within one million units."));
	if (seen.contains("--target-offset") && !seen.contains("--target-joint"))
		return fail(2, Text::tr("A target offset needs --target-joint."));
	if (!chained && !seen.contains("--pole") && !seen.contains("--pole-keys"))
		return fail(2, Text::tr("The two-bone solve needs --pole x,y,z or --pole-keys."));
	o.targetJoint = values.value("--target-joint");
	if (seen.contains("--targets"))
	{
		if (modelPathsReferToSameFile(values["--targets"], values.value("--output")))
			return fail(1, Text::tr("The output cannot replace the targets input, including aliases of the same file."));
		QByteArray bytes;
		QString problem;
		if (!readModelFile(values["--targets"], &bytes, &problem))
			return fail(1, problem);
		const auto points = QJsonDocument::fromJson(bytes).array();
		for (const auto &point : points)
		{
			const auto xyz = point.toArray();
			if (xyz.size() != 3 || !xyz[0].isDouble() || !xyz[1].isDouble() || !xyz[2].isDouble())
				return fail(2, Text::tr("A targets file is a JSON array of [x, y, z] points, one per sample from --first to --last."));
			o.targets.append({float(xyz[0].toDouble()), float(xyz[1].toDouble()), float(xyz[2].toDouble())});
		}
		if (o.targets.isEmpty())
			return fail(2, Text::tr("A targets file is a JSON array of [x, y, z] points, one per sample from --first to --last."));
	}
	if (chained)
	{
		o.usePole = seen.contains("--pole") || seen.contains("--pole-keys");
		o.limits = seen.contains("--limits");
		if (seen.contains("--iterations") && (!integer(values["--iterations"], &o.iterations) || o.iterations < 1 || o.iterations > 1000))
			return fail(2, Text::tr("Iterations must be a whole number from 1 to 1000."));
		if (seen.contains("--tolerance"))
		{
			bool ok = false;
			o.tolerance = values["--tolerance"].toDouble(&ok);
			if (!ok || !std::isfinite(o.tolerance) || o.tolerance <= 0 || o.tolerance > 1000000)
				return fail(2, Text::tr("Tolerance must be a positive number of model units."));
		}
	}
	o.nearestReachable = seen.contains("--nearest-reachable");
	o.keepEndOrientation = !seen.contains("--follow-end");
	const auto source = input ? values["--input"] : positional[2];
	ModelDocument document;
	QString error;
	if (!document.load(source, &error))
		return fail(1, error);
	if (!document.edit(edit, &error))
		return fail(4, error);
	const auto target = inspectModelWriteTarget(values["--output"]);
	if (!target.isValid())
		return fail(1, target.error);
	if (target.existed && !seen.contains("--overwrite"))
		return fail(1, Text::tr("The output already exists; use --overwrite to replace it."));
	const bool dry = seen.contains("--dry-run");
	if (!document.save(values["--output"], seen.contains("--overwrite"), &error, {}, dry))
		return fail(1, error);
	QJsonArray samples;
	int reached = 0;
	QString limitProblem;
	bool withinLimits = true;
	for (const auto &sample : modelIkSamples(document.mesh(), o, chained))
	{
		reached += sample.reached;
		samples.append(QJsonObject{{"sample", sample.frame},
								   {"end", QJsonArray{sample.end.x, sample.end.y, sample.end.z}},
								   {"target", QJsonArray{sample.target.x, sample.target.y, sample.target.z}},
								   {"error", sample.error},
								   {"tolerance", sample.tolerance},
								   {"reached", sample.reached}});
		if (withinLimits)
			withinLimits = modelPoseWithinJointLimits(document.mesh().skeleton, document.mesh().skeleton.clips[o.clip].frames[sample.frame], 1e-3,
													  &limitProblem);
	}
	ModelToolsCliResult result;
	result.payload = {{"source", QFileInfo(source).absoluteFilePath()},
					  {"output", QFileInfo(values["--output"]).absoluteFilePath()},
					  {"clip", o.clip},
					  {"endJoint", o.endJoint},
					  {"first", o.first},
					  {"last", o.last},
					  {"nearestReachable", o.nearestReachable},
					  {"keepEndOrientation", o.keepEndOrientation},
					  {"solver", solver},
					  {"rootJoint", o.rootJoint},
					  {"targetJoint", o.targetJoint},
					  {"targetKeys", modelIkKeysText(o.targetKeys)},
					  {"poleKeys", modelIkKeysText(o.poleKeys)},
					  {"limits", o.limits},
					  {"withinLimits", withinLimits},
					  {"samples", samples},
					  {"dryRun", dry},
					  {"written", !dry}};
	result.lines << Text::tr("Inverse kinematics reviewed. Samples reaching the target: %1 / %2.").arg(reached).arg(samples.size());
	if (!withinLimits)
		result.lines << limitProblem;
	result.payload.insert("outputSource", modelSourceSaveJson(document));
	if (const auto packed = modelSourceSaveNotice(document); !packed.isEmpty())
		result.lines << packed;
	return result;
}
} // namespace vibestudio::cli
