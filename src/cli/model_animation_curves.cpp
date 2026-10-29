#include "cli/model_tools.h"
#include "core/model_document.h"
#include "core/model_rotation_tracks.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <algorithm>
#include <array>
#include <cmath>
namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelAnimationCurvesCli)
};
ModelToolsCliResult fail(int code, const QString &error)
{
	ModelToolsCliResult r;
	r.exitCode = code;
	r.error = error;
	return r;
}
bool integer(const QString &s, int *n)
{
	bool ok = false;
	*n = s.toInt(&ok);
	return ok && *n >= 0 && s == QString::number(*n);
}
bool number(const QString &s, double *n)
{
	bool ok = false;
	*n = s.toDouble(&ok);
	return ok && std::isfinite(*n) && std::abs(*n) <= 1000000;
}
} // namespace
ModelToolsCliResult runModelCurves(const QStringList &args)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite", "--every-sample"};
	const QSet<QString> options{"--input",	   "--output",	  "--operation",	 "--clip",			"--joint",		 "--channel",
								"--frame",	   "--new-frame", "--value",		 "--interpolation", "--tangent",	 "--in-slope",
								"--out-slope", "--curves",	  "--settings-file", "--locale",		"--catalog-root", "--to",
								"--rotate",	   "--quat"};
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
	if (positional.size() != (input ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "curves")
		return fail(2, Text::tr("Provide one editable model source, positionally or with --input."));
	const QStringList actions{"capture", "replace", "set-key", "remove-key", "move-key", "bake"};
	// Quaternion rotation operations (core/model_rotation_tracks.h) replace
	// the joint's curve group, as the curves dialog does.
	const QStringList rotationActions{"convert-rotation", "set-rotation-key", "remove-rotation-key"};
	const auto action = values.value("--operation", "inspect");
	const bool inspect = action == "inspect";
	const bool rotationAction = rotationActions.contains(action);
	const int actionIndex = actions.indexOf(action);
	if (!inspect && actionIndex < 0 && !rotationAction)
		return fail(2, Text::tr("Operation must be inspect, capture, replace, set-key, remove-key, move-key, bake, convert-rotation, "
								"set-rotation-key or remove-rotation-key."));
	ModelEdit edit;
	edit.kind = ModelEditKind::EditAnimationCurves;
	auto &o = edit.curves;
	if (!integer(values.value("--clip"), &o.clip))
		return fail(2, Text::tr("Provide --clip as a nonnegative integer."));
	o.joint = values.value("--joint");
	QSet<QString> allowed{"--cli",	"--json",  "--quiet",		  "--verbose", "--operation",	"--input",
						  "--clip", "--joint", "--settings-file", "--locale",  "--catalog-root"};
	if (!inspect)
	{
		allowed.unite({"--output", "--dry-run", "--overwrite"});
		o.action = rotationAction ? ModelCurveAction::Replace : ModelCurveAction(actionIndex);
		if (o.joint.isEmpty() || !values.value("--output").endsWith(".mesh.json", Qt::CaseInsensitive))
			return fail(2, Text::tr("Curve edits require --joint and --output ending in .mesh.json."));
		if (action == "replace")
			allowed.insert("--curves");
		if (action == "convert-rotation")
			allowed.unite({"--to", "--every-sample"});
		if (action == "set-rotation-key")
			allowed.unite({"--frame", "--rotate", "--quat", "--interpolation"});
		if (action == "remove-rotation-key")
			allowed.insert("--frame");
		if (action.endsWith("-key") && !rotationAction)
		{
			allowed.unite({"--channel", "--frame"});
			o.channel = modelCurveChannelIndex(values.value("--channel"));
			if (o.channel < 0 || !integer(values.value("--frame"), &o.frame))
				return fail(2, Text::tr("Key edits require --channel tx/ty/tz/rx/ry/rz/sx/sy/sz and a nonnegative --frame."));
		}
		if (action == "set-key")
			allowed.unite({"--value", "--interpolation", "--tangent", "--in-slope", "--out-slope"});
		if (action == "move-key")
		{
			allowed.insert("--new-frame");
			if (!integer(values.value("--new-frame"), &o.newFrame))
				return fail(2, Text::tr("Moving a key requires a nonnegative --new-frame."));
		}
	}
	for (const auto &key : seen)
		if (!allowed.contains(key))
			return fail(2, Text::tr("Option %1 does not apply to %2.").arg(key, action));
	const auto source = input ? values["--input"] : positional[2];
	ModelDocument document;
	QString error;
	if (!document.load(source, &error))
		return fail(1, error);
	if (o.clip >= document.mesh().skeleton.clips.size())
		return fail(4, Text::tr("The selected skeletal clip does not exist."));
	const auto &clip = document.mesh().skeleton.clips[o.clip];
	if (action == "set-key")
	{
		for (const auto &group : clip.curves)
			if (group.joint == o.joint)
				for (const auto &key : group.channels[o.channel])
					if (key.frame == o.frame)
						o.key = key;
		o.key.frame = o.frame;
		if (!number(values.value("--value"), &o.key.value))
			return fail(2, Text::tr("Setting a key requires a finite --value within one million units."));
		if (seen.contains("--interpolation"))
		{
			const int mode = QStringList{"step", "linear", "cubic", "step-next"}.indexOf(values["--interpolation"]);
			if (mode < 0)
				return fail(2, Text::tr("Interpolation must be step, linear, cubic or step-next."));
			o.key.interpolation = ModelCurveInterpolation(mode);
		}
		if (seen.contains("--tangent"))
		{
			const int mode = QStringList{"auto", "clamped", "free", "flat"}.indexOf(values["--tangent"]);
			if (mode < 0)
				return fail(2, Text::tr("Tangent mode must be auto, clamped, free or flat."));
			o.key.tangent = ModelCurveTangent(mode);
		}
		for (const auto &name : QStringList{"--in-slope", "--out-slope"})
			if (seen.contains(name))
			{
				double slope = 0;
				if (o.key.tangent != ModelCurveTangent::Free || !number(values[name], &slope))
					return fail(2, Text::tr("Explicit slopes require free tangents and finite values within one million units."));
				if (name == "--in-slope")
					o.key.inSlope = slope;
				else
					o.key.outSlope = slope;
			}
	}
	double rotationChange = 0;
	if (rotationAction)
	{
		const auto existing =
			std::find_if(clip.curves.cbegin(), clip.curves.cend(), [&](const ModelPoseCurves &group) { return group.joint == o.joint; });
		if (existing == clip.curves.cend())
			return fail(4, Text::tr("The selected joint has no stored curves. Capture them first."));
		o.group = *existing;
		const int frames = int(clip.frames.size());
		if (action == "convert-rotation")
		{
			const auto to = values.value("--to");
			if (to != "quaternion" && to != "euler")
				return fail(2, Text::tr("Converting rotation requires --to quaternion or --to euler."));
			if (!convertModelPoseCurvesRotation(&o.group, to == "quaternion",
												seen.contains("--every-sample") ? ModelRotationKeyTimes::EverySample : ModelRotationKeyTimes::Keys,
												frames, &rotationChange, &error))
				return fail(4, error);
		}
		else
		{
			int frame = 0;
			if (!integer(values.value("--frame"), &frame))
				return fail(2, Text::tr("Rotation key edits require a nonnegative --frame."));
			if (action == "remove-rotation-key")
			{
				if (!removeModelQuatKey(&o.group, frame, &error))
					return fail(4, error);
			}
			else
			{
				if (seen.contains("--rotate") == seen.contains("--quat"))
					return fail(2, Text::tr("Give the key's rotation as --rotate x,y,z (degrees) or --quat x,y,z,w."));
				ModelPoseQuatKey key;
				key.frame = frame;
				for (const auto &old : o.group.rotation)
					if (old.frame == frame)
						key.interpolation = old.interpolation;
				const auto parts = values.value(seen.contains("--rotate") ? "--rotate" : "--quat").split(',');
				const int count = seen.contains("--rotate") ? 3 : 4;
				std::array<double, 4> numbers{};
				bool ok = parts.size() == count;
				for (int i = 0; ok && i < count; ++i)
					ok = number(parts[i], &numbers[i]);
				if (!ok || (count == 4 && numbers[0] * numbers[0] + numbers[1] * numbers[1] + numbers[2] * numbers[2] + numbers[3] * numbers[3] < 1e-12))
					return fail(2, Text::tr("Give --rotate as three angles in degrees, or --quat as four numbers of a nonzero quaternion."));
				key.value = count == 3 ? modelQuatFromEulerDegrees({numbers[0], numbers[1], numbers[2]}) : numbers;
				if (seen.contains("--interpolation"))
				{
					const int mode = QStringList{"step", "linear", "cubic", "step-next"}.indexOf(values["--interpolation"]);
					if (mode < 0)
						return fail(2, Text::tr("Interpolation must be step, linear, cubic or step-next."));
					key.interpolation = ModelCurveInterpolation(mode);
				}
				if (!setModelQuatKey(&o.group, key, frames, &error))
					return fail(4, error);
			}
		}
	}
	if (action == "replace")
	{
		if (modelPathsReferToSameFile(values["--output"], values["--curves"]))
			return fail(1, Text::tr("The output cannot replace the curve input, including aliases of the same file."));
		QFile file(values.value("--curves"));
		if (!file.open(QIODevice::ReadOnly))
			return fail(1, Text::tr("Cannot read the replacement curve file."));
		const auto bytes = file.read(modelDocumentMaxSourceBytes + 1);
		QJsonParseError parse;
		const auto json = QJsonDocument::fromJson(bytes, &parse);
		if (bytes.size() > modelDocumentMaxSourceBytes || parse.error != QJsonParseError::NoError || !json.isObject())
			return fail(2, Text::tr("Replacement curves must be one bounded JSON joint-curve object."));
		if (!modelPoseCurvesFromJson(json.object(), int(clip.frames.size()), &o.group, &error))
			return fail(4, error);
	}
	if (inspect)
	{
		QVector<ModelPoseCurves> groups;
		for (const auto &group : clip.curves)
			if (o.joint.isEmpty() || group.joint == o.joint)
				groups.append(group);
		if (!o.joint.isEmpty() && groups.isEmpty())
			return fail(4, Text::tr("The selected joint has no stored curves."));
		ModelToolsCliResult result;
		result.payload = {{"clip", o.clip}, {"name", clip.name}, {"samples", clip.frames.size()}, {"curves", modelClipCurvesJson(groups)}};
		result.lines << Text::tr("Clip %1 · joints with curves: %2.").arg(clip.name).arg(groups.size());
		return result;
	}
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
	ModelToolsCliResult result;
	result.payload = {{"source", QFileInfo(source).absoluteFilePath()},
					  {"output", QFileInfo(values["--output"]).absoluteFilePath()},
					  {"operation", action},
					  {"clip", o.clip},
					  {"joint", o.joint},
					  {"dryRun", dry},
					  {"written", !dry},
					  {"curves", modelClipCurvesJson(document.mesh().skeleton.clips[o.clip].curves)}};
	result.lines << Text::tr("Animation curve operation %1 validated for joint %2.").arg(action, o.joint);
	if (action == "convert-rotation")
	{
		result.payload.insert("rotationChange", rotationChange);
		result.lines << Text::tr("Largest rotation change at any sample: %1 degrees.").arg(rotationChange, 0, 'g', 6);
	}
	result.payload.insert("outputSource", modelSourceSaveJson(document));
	if (const auto packed = modelSourceSaveNotice(document); !packed.isEmpty())
		result.lines << packed;
	return result;
}
} // namespace vibestudio::cli
