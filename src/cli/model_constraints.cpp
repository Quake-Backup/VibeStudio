// `model constraints`: a skeletal clip's persistent constraints and layers
// (core/model_animation_constraints.h).
#include "cli/model_tools.h"
#include "core/model_animation_constraints.h"
#include "core/model_animation_curves.h"
#include "core/model_document.h"
#include "core/model_file_io.h"

#include <QCoreApplication>
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
	Q_DECLARE_TR_FUNCTIONS(ModelConstraintsCli)
};
ModelToolsCliResult fail(int code, const QString &message)
{
	return {code, message, {}, {}};
}
bool integer(const QString &text, int *value)
{
	bool ok = false;
	*value = text.toInt(&ok);
	return ok && *value >= 0 && text == QString::number(*value);
}
bool triple(const QString &text, std::array<double, 3> *values)
{
	const auto parts = text.split(',');
	if (parts.size() != 3)
		return false;
	for (int i = 0; i < 3; ++i)
	{
		bool ok = false;
		(*values)[i] = parts[i].toDouble(&ok);
		if (!ok || !std::isfinite((*values)[i]))
			return false;
	}
	return true;
}
QJsonObject constraintJson(const ModelClipConstraint &c, int index)
{
	return {{"index", index},
			{"name", c.name},
			{"kind", modelClipConstraintKindId(c.kind)},
			{"joint", c.joint},
			{"rootJoint", c.rootJoint},
			{"target", c.target},
			{"point", QJsonArray{c.point[0], c.point[1], c.point[2]}},
			{"pole", QJsonArray{c.pole[0], c.pole[1], c.pole[2]}},
			{"usePole", c.usePole},
			{"axis", c.axis},
			{"first", c.first},
			{"last", c.last},
			{"weight", c.weight},
			{"weightKeys", modelWeightKeysText(c.weightKeys)},
			{"limits", c.limits},
			{"enabled", c.enabled}};
}
} // namespace

ModelToolsCliResult runModelConstraints(const QStringList &arguments)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite", "--no-pole", "--disabled"};
	const QSet<QString> options{"--input", "--output", "--operation", "--clip", "--index", "--kind", "--joint", "--root-joint", "--target",
								"--point", "--pole", "--axis", "--first", "--last", "--weight", "--name", "--layer", "--mode",
								"--settings-file", "--locale", "--catalog-root", "--weight-keys", "--limits"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	for (int i = 1; i < arguments.size(); ++i)
	{
		const auto argument = arguments[i];
		if (!argument.startsWith('-'))
		{
			positional.append(argument);
			continue;
		}
		const int equal = argument.indexOf('=');
		const auto key = equal < 0 ? argument : argument.left(equal);
		if (seen.contains(key))
			return fail(2, Text::tr("Repeated option: %1.").arg(key));
		seen.insert(key);
		if (flags.contains(key) && equal < 0)
			continue;
		if (!options.contains(key))
			return fail(2, Text::tr("Unknown or invalid option: %1.").arg(key));
		const auto value = equal < 0 ? arguments.value(++i) : argument.mid(equal + 1);
		if (value.isEmpty() || (value.startsWith("--")))
			return fail(2, Text::tr("Option %1 requires a value.").arg(key));
		values.insert(key, value);
	}
	if (positional.size() != (seen.contains("--input") ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "constraints")
		return fail(2, Text::tr("Provide one editable model source, positionally or with --input."));
	const auto input = seen.contains("--input") ? values.value("--input") : positional[2];
	const auto operation = values.value("--operation", "list");
	const QHash<QString, ModelClipStackAction> actions{{"add", ModelClipStackAction::AddConstraint},
														{"update", ModelClipStackAction::UpdateConstraint},
														{"remove", ModelClipStackAction::RemoveConstraint},
														{"add-layer", ModelClipStackAction::AddLayer},
														{"update-layer", ModelClipStackAction::UpdateLayer},
														{"remove-layer", ModelClipStackAction::RemoveLayer},
														{"bake", ModelClipStackAction::Bake}};
	const bool list = operation == "list";
	if (!list && !actions.contains(operation))
		return fail(2, Text::tr("Choose list, add, update, remove, add-layer, update-layer, remove-layer or bake."));
	const bool constraint = operation == "add" || operation == "update", layerEdit = operation == "add-layer" || operation == "update-layer";
	QSet<QString> required;
	if (!list)
		required = {"--clip", "--output"};
	if (operation == "add")
		required.unite({"--kind", "--joint"});
	if (operation == "add-layer")
		required.insert("--layer");
	if (operation.startsWith("update") || operation.startsWith("remove"))
		required.insert("--index");
	QSet<QString> allowed = required;
	allowed.unite({"--input", "--operation", "--settings-file", "--locale", "--catalog-root"});
	if (list)
		allowed.insert("--clip");
	if (constraint)
		allowed.unite({"--kind", "--joint", "--root-joint", "--target", "--point", "--pole", "--no-pole", "--axis", "--first", "--last", "--weight",
					   "--name", "--disabled", "--weight-keys", "--limits"});
	if (layerEdit)
		allowed.unite({"--layer", "--mode", "--weight", "--name", "--disabled", "--weight-keys"});
	for (const auto &key : required)
		if (!seen.contains(key))
			return fail(2, Text::tr("Operation %1 requires %2.").arg(operation, key));
	for (const auto &key : seen)
		if ((!flags.contains(key) && !allowed.contains(key)) || (list && (key == "--dry-run" || key == "--overwrite")) ||
			((key == "--no-pole" || key == "--disabled") && !allowed.contains(key)))
			return fail(2, Text::tr("Option %1 does not apply to %2.").arg(key, operation));
	if (!list && !values.value("--output").endsWith(".mesh.json", Qt::CaseInsensitive))
		return fail(2, Text::tr("Constraint edits require an output ending in .mesh.json."));
	ModelDocument document;
	QString error;
	if (!document.load(input, &error))
		return fail(1, error);
	const auto &skeleton = document.mesh().skeleton;
	ModelEdit edit;
	edit.kind = ModelEditKind::EditClipStack;
	auto &o = edit.animation.stack;
	if (seen.contains("--clip") && (!integer(values.value("--clip"), &o.clip) || o.clip >= skeleton.clips.size()))
		return fail(2, Text::tr("Option --clip requires an existing clip index."));
	if (seen.contains("--index") && !integer(values.value("--index"), &o.index))
		return fail(2, Text::tr("Option --index requires a nonnegative integer."));
	if (!list)
	{
		o.action = actions[operation];
		const auto &clip = skeleton.clips[o.clip];
		if (constraint)
		{
			auto &c = o.constraint;
			if (operation == "update")
			{
				if (o.index >= clip.constraints.size())
					return fail(4, Text::tr("Clip %1 has no constraint %2.").arg(clip.name).arg(o.index));
				c = clip.constraints[o.index];
			}
			if (seen.contains("--kind"))
			{
				const int kind = modelClipConstraintKindFromId(values.value("--kind"));
				if (kind < 0)
					return fail(2, Text::tr("Kind must be ik, aim, child-of, copy-transform or foot-lock."));
				c.kind = ModelClipConstraintKind(kind);
			}
			for (const auto &field : {qMakePair("--joint", &c.joint), qMakePair("--root-joint", &c.rootJoint), qMakePair("--target", &c.target),
									  qMakePair("--name", &c.name)})
				if (seen.contains(field.first))
					*field.second = values.value(field.first) == "-" ? QString() : values.value(field.first);
			if ((seen.contains("--point") && !triple(values.value("--point"), &c.point)) || (seen.contains("--pole") && !triple(values.value("--pole"), &c.pole)))
				return fail(2, Text::tr("Points and poles require three comma-separated numbers."));
			if (seen.contains("--no-pole"))
				c.usePole = false;
			if (seen.contains("--axis"))
			{
				const int axis = QStringList{"x", "y", "z", "-x", "-y", "-z"}.indexOf(values.value("--axis"));
				if (axis < 0)
					return fail(2, Text::tr("Axis must be x, y, z, -x, -y or -z."));
				c.axis = axis;
			}
			if (seen.contains("--first") && !integer(values.value("--first"), &c.first))
				return fail(2, Text::tr("Option --first requires a nonnegative integer."));
			if (seen.contains("--last") && values.value("--last") != "end" && !integer(values.value("--last"), &c.last))
				return fail(2, Text::tr("Option --last requires a nonnegative integer or end."));
			if (values.value("--last") == "end")
				c.last = -1;
			if (seen.contains("--disabled"))
				c.enabled = false;
			if (seen.contains("--limits"))
			{
				if (values.value("--limits") != "yes" && values.value("--limits") != "no")
					return fail(2, Text::tr("Option --limits requires yes or no."));
				c.limits = values.value("--limits") == "yes";
			}
		}
		if (layerEdit)
		{
			auto &l = o.layer;
			if (operation == "update-layer")
			{
				if (o.index >= clip.layers.size())
					return fail(4, Text::tr("Clip %1 has no layer %2.").arg(clip.name).arg(o.index));
				l = clip.layers[o.index];
			}
			if (seen.contains("--layer"))
			{
				if (modelPathsReferToSameFile(values.value("--layer"), values.value("--output")))
					return fail(1, Text::tr("The output cannot replace the layer input, including aliases of the same file."));
				QByteArray bytes;
				if (!readModelFile(values.value("--layer"), &bytes, &error))
					return fail(1, error);
				const auto json = QJsonDocument::fromJson(bytes);
				if (!modelClipCurvesFromJson(json.isArray() ? QJsonValue(json.array()) : json.object().value("curves"), int(clip.frames.size()), &l.curves, &error))
					return fail(2, error.isEmpty() ? Text::tr("A layer file holds joint curve groups, as model curves writes them.") : error);
			}
			if (seen.contains("--mode"))
			{
				const auto mode = values.value("--mode");
				if (mode != "additive" && mode != "override")
					return fail(2, Text::tr("Mode must be additive or override."));
				l.additive = mode == "additive";
			}
			if (seen.contains("--name"))
				l.name = values.value("--name");
			if (seen.contains("--disabled"))
				l.enabled = false;
		}
		if (seen.contains("--weight"))
		{
			bool ok = false;
			const double weight = values.value("--weight").toDouble(&ok);
			if (!ok || !std::isfinite(weight) || weight < 0 || weight > 1)
				return fail(2, Text::tr("Weight must be from 0 to 1."));
			(constraint ? o.constraint.weight : o.layer.weight) = weight;
		}
		if (seen.contains("--weight-keys"))
		{
			// "-" clears them; otherwise sample:value pairs.
			const auto text = values.value("--weight-keys") == "-" ? QString() : values.value("--weight-keys");
			if (!parseModelWeightKeys(text, int(clip.frames.size()), constraint ? &o.constraint.weightKeys : &o.layer.weightKeys, &error))
				return fail(2, error);
		}
		if (!document.edit(edit, &error))
			return fail(4, error);
		const auto target = inspectModelWriteTarget(values.value("--output"));
		if (!target.isValid())
			return fail(1, target.error);
		if (target.existed && !seen.contains("--overwrite"))
			return fail(1, Text::tr("The output exists; use --overwrite to replace it."));
		if (!document.save(values.value("--output"), seen.contains("--overwrite"), &error, {}, seen.contains("--dry-run")))
			return fail(1, error);
	}
	ModelToolsCliResult result;
	QJsonArray clips;
	const auto &after = document.mesh().skeleton;
	for (int i = 0; i < after.clips.size(); ++i)
	{
		if (seen.contains("--clip") && i != o.clip)
			continue;
		const auto &clip = after.clips[i];
		QJsonArray constraints, layers;
		for (int c = 0; c < clip.constraints.size(); ++c)
		{
			constraints.append(constraintJson(clip.constraints[c], c));
			result.lines << Text::tr("Clip %1 constraint %2: %3 on %4, samples %5 to %6, weight %7")
								.arg(clip.name)
								.arg(c)
								.arg(modelClipConstraintKindId(clip.constraints[c].kind), clip.constraints[c].joint)
								.arg(clip.constraints[c].first)
								.arg(clip.constraints[c].last < 0 ? int(clip.frames.size()) - 1 : clip.constraints[c].last)
								.arg(clip.constraints[c].weight);
		}
		for (int l = 0; l < clip.layers.size(); ++l)
		{
			const auto &layer = clip.layers[l];
			QStringList joints;
			for (const auto &group : layer.curves)
				joints << group.joint;
			layers.append(QJsonObject{{"index", l},
									  {"name", layer.name},
									  {"mode", layer.additive ? "additive" : "override"},
									  {"weight", layer.weight},
									  {"weightKeys", modelWeightKeysText(layer.weightKeys)},
									  {"enabled", layer.enabled},
									  {"joints", QJsonArray::fromStringList(joints)}});
			result.lines << Text::tr("Clip %1 layer %2 (%3): %4, weight %5")
								.arg(clip.name)
								.arg(l)
								.arg(layer.name, layer.additive ? QStringLiteral("additive") : QStringLiteral("override"))
								.arg(layer.weight);
		}
		clips.append(QJsonObject{{"index", i}, {"name", clip.name}, {"stack", modelClipHasStack(clip)}, {"constraints", constraints}, {"layers", layers}});
	}
	const bool dry = seen.contains("--dry-run");
	result.payload = {{"source", QFileInfo(input).absoluteFilePath()}, {"operation", operation}, {"clips", clips}, {"written", !list && !dry}, {"dryRun", dry}};
	if (!list)
	{
		result.payload.insert("outputPath", QFileInfo(values.value("--output")).absoluteFilePath());
		result.payload.insert("outputSource", modelSourceSaveJson(document));
		result.lines.prepend(dry ? Text::tr("Constraint edit validated; no files written.") : Text::tr("Saved the constraint edit to %1.").arg(values.value("--output")));
	}
	return result;
}
} // namespace vibestudio::cli
