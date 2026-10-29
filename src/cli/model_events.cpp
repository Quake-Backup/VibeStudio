// `model events` (animation events) and `model declarations` (game-side
// declarations: Doom 3 .def and model sets, QuakeC $frame macros, Quake II
// frame headers, GZDoom MODELDEF).
#include "cli/model_tools.h"
#include "core/model_animation_events.h"
#include "core/model_document.h"
#include "core/model_file_io.h"
#include "core/model_game_declarations.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <cmath>
#include <iostream>

namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelEventsCli)
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
// Strict option parsing shared by both commands: positional "model <command>
// <source>" or --input, each option once.
bool parse(const QStringList &arguments, const QString &command, const QSet<QString> &flags, const QSet<QString> &options, QSet<QString> *seen,
		   QHash<QString, QString> *values, QString *input, ModelToolsCliResult *failure)
{
	QStringList positional;
	for (int i = 1; i < arguments.size(); ++i)
	{
		const auto argument = arguments[i];
		if (!argument.startsWith('-') || argument == "-")
		{
			positional.append(argument);
			continue;
		}
		const int equal = argument.indexOf('=');
		const auto key = equal < 0 ? argument : argument.left(equal);
		if (seen->contains(key))
		{
			*failure = fail(2, Text::tr("Repeated option: %1.").arg(key));
			return false;
		}
		seen->insert(key);
		if (flags.contains(key) && equal < 0)
			continue;
		if (!options.contains(key))
		{
			*failure = fail(2, Text::tr("Unknown or invalid option: %1.").arg(key));
			return false;
		}
		const auto value = equal < 0 ? arguments.value(++i) : argument.mid(equal + 1);
		if (value.isEmpty() || (value.startsWith("--")))
		{
			*failure = fail(2, Text::tr("Option %1 requires a value.").arg(key));
			return false;
		}
		values->insert(key, value);
	}
	if (positional.size() != (seen->contains("--input") ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != command)
	{
		*failure = fail(2, Text::tr("Provide one editable model source, positionally or with --input."));
		return false;
	}
	*input = seen->contains("--input") ? values->value("--input") : positional[2];
	return true;
}
QJsonArray eventsJson(const QVector<ModelAnimationEvent> &events)
{
	QJsonArray result;
	for (int i = 0; i < events.size(); ++i)
		result.append(QJsonObject{{"index", i},
								  {"frame", events[i].frame},
								  {"command", events[i].command},
								  {"args", QJsonArray::fromStringList(events[i].args)},
								  {"line", modelAnimationEventCommandLine(events[i])}});
	return result;
}
QString baseName(const QString &path)
{
	auto name = QFileInfo(path).fileName();
	for (const auto &suffix : {QStringLiteral(".mesh.json"), QStringLiteral(".json")})
		if (name.endsWith(suffix, Qt::CaseInsensitive))
		{
			name.chop(suffix.size());
			break;
		}
	static const QRegularExpression invalid(QStringLiteral("[^A-Za-z0-9_]"));
	name.replace(invalid, QStringLiteral("_"));
	if (name.isEmpty() || name[0].isDigit())
		name.prepend(QLatin1Char('_'));
	return name.toLower();
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
} // namespace

ModelToolsCliResult runModelEvents(const QStringList &arguments)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite"};
	const QSet<QString> options{"--input", "--output", "--operation", "--clip", "--animation", "--frame", "--command", "--index", "--events",
								"--settings-file", "--locale", "--catalog-root"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QString input;
	ModelToolsCliResult failure;
	if (!parse(arguments, "events", flags, options, &seen, &values, &input, &failure))
		return failure;
	const auto operation = values.value("--operation", "list");
	const QHash<QString, ModelAnimationEventAction> actions{{"add", ModelAnimationEventAction::Add},
															 {"remove", ModelAnimationEventAction::Remove},
															 {"move", ModelAnimationEventAction::Move},
															 {"replace", ModelAnimationEventAction::Replace}};
	const bool list = operation == "list";
	if (!list && !actions.contains(operation))
		return fail(2, Text::tr("Choose list, add, remove, move or replace."));
	QSet<QString> required;
	if (!list)
		required = {"--output"};
	if (operation == "add")
		required.unite({"--frame", "--command"});
	if (operation == "remove")
		required.insert("--index");
	if (operation == "move")
		required.unite({"--index", "--frame"});
	if (operation == "replace")
		required.insert("--events");
	QSet<QString> allowed = required;
	allowed.unite({"--input", "--operation", "--clip", "--animation", "--settings-file", "--locale", "--catalog-root"});
	for (const auto &key : required)
		if (!seen.contains(key))
			return fail(2, Text::tr("Operation %1 requires %2.").arg(operation, key));
	for (const auto &key : seen)
		if ((!flags.contains(key) && !allowed.contains(key)) || (list && (key == "--dry-run" || key == "--overwrite")))
			return fail(2, Text::tr("Option %1 does not apply to %2.").arg(key, operation));
	if (!list && seen.contains("--clip") == seen.contains("--animation"))
		return fail(2, Text::tr("Choose a skeletal clip with --clip or a vertex animation with --animation."));
	if (!list && !values.value("--output").endsWith(".mesh.json", Qt::CaseInsensitive))
		return fail(2, Text::tr("Event edits require an output ending in .mesh.json."));
	ModelEdit edit;
	edit.kind = ModelEditKind::EditAnimationEvents;
	auto &o = edit.animation.events;
	for (const auto &field : {qMakePair("--clip", &o.clip), qMakePair("--animation", &o.animation), qMakePair("--index", &o.index)})
		if (seen.contains(field.first) && !integer(values.value(field.first), field.second))
			return fail(2, Text::tr("Option %1 requires a nonnegative integer.").arg(field.first));
	int frame = 0;
	if (seen.contains("--frame") && !integer(values.value("--frame"), &frame))
		return fail(2, Text::tr("Option --frame requires a nonnegative integer."));
	QString error;
	if (operation == "add" && !parseModelAnimationEventCommandLine(values.value("--command"), frame, &o.event, &error))
		return fail(2, error);
	if (operation == "move")
		o.frame = frame;
	ModelDocument document;
	if (!document.load(input, &error))
		return fail(1, error);
	if (operation == "replace")
	{
		if (modelPathsReferToSameFile(values.value("--events"), values.value("--output")))
			return fail(1, Text::tr("The output cannot replace the events input, including aliases of the same file."));
		QByteArray bytes;
		if (!readModelFile(values.value("--events"), &bytes, &error))
			return fail(1, error);
		const int frames = o.clip >= 0 ? int(document.mesh().skeleton.clips.value(o.clip).frames.size())
										: document.mesh().animations.value(o.animation).frameCount;
		const auto json = QJsonDocument::fromJson(bytes);
		if (!json.isArray() || !modelAnimationEventsFromJson(json.array(), frames, &o.events, &error))
			return fail(2, error.isEmpty() ? Text::tr("An events file is a JSON array of {frame, command, args} objects.") : error);
	}
	if (!list)
	{
		o.action = actions[operation];
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
	const auto &mesh = document.mesh();
	ModelToolsCliResult result;
	QJsonArray clips, animations;
	for (int i = 0; i < mesh.skeleton.clips.size(); ++i)
	{
		const auto &clip = mesh.skeleton.clips[i];
		if ((seen.contains("--clip") && i != o.clip) || seen.contains("--animation"))
			continue;
		clips.append(QJsonObject{{"index", i}, {"name", clip.name}, {"frames", int(clip.frames.size())}, {"events", eventsJson(clip.events)}});
		result.lines << Text::tr("Clip %1 (%2): %n event(s)", nullptr, int(clip.events.size())).arg(i).arg(clip.name);
		for (const auto &event : clip.events)
			result.lines << Text::tr("  frame %1: %2").arg(event.frame).arg(modelAnimationEventCommandLine(event));
	}
	for (int i = 0; i < mesh.animations.size(); ++i)
	{
		const auto &animation = mesh.animations[i];
		if ((seen.contains("--animation") && i != o.animation) || seen.contains("--clip") || !mesh.skeleton.isEmpty())
			continue;
		animations.append(QJsonObject{{"index", i}, {"name", animation.name}, {"frames", animation.frameCount}, {"events", eventsJson(animation.events)}});
		result.lines << Text::tr("Animation %1 (%2): %n event(s)", nullptr, int(animation.events.size())).arg(i).arg(animation.name);
		for (const auto &event : animation.events)
			result.lines << Text::tr("  frame %1: %2").arg(event.frame).arg(modelAnimationEventCommandLine(event));
	}
	const bool dry = seen.contains("--dry-run");
	result.payload = {{"source", QFileInfo(input).absoluteFilePath()},
					  {"operation", operation},
					  {"clips", clips},
					  {"animations", animations},
					  {"written", !list && !dry},
					  {"dryRun", dry}};
	if (!list)
	{
		result.payload.insert("outputPath", QFileInfo(values.value("--output")).absoluteFilePath());
		result.payload.insert("outputSource", modelSourceSaveJson(document));
		result.lines.prepend(dry ? Text::tr("Event edit validated; no files written.") : Text::tr("Saved the event edit to %1.").arg(values.value("--output")));
	}
	return result;
}

ModelToolsCliResult runModelDeclarations(const QStringList &arguments)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite"};
	const QSet<QString> options{"--input",	   "--target",	  "--output",  "--set",	  "--name",			 "--mesh-path", "--anim-dir",
								"--skin",	   "--inherit",	  "--offset",  "--cd",	  "--base",			 "--prefix",	"--scale",
								"--actor",	   "--path",	  "--model",   "--sprite", "--angle-offset", "--settings-file", "--locale",
								"--catalog-root"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QString input;
	ModelToolsCliResult failure;
	if (!parse(arguments, "declarations", flags, options, &seen, &values, &input, &failure))
		return failure;
	const auto target = values.value("--target");
	if (!modelGameDeclarationTargets().contains(target))
		return fail(2, Text::tr("Choose --target doom3, quakec, quake2 or gzdoom."));
	const QHash<QString, QSet<QString>> specific{
		{"doom3", {"--set", "--name", "--mesh-path", "--anim-dir", "--skin", "--inherit", "--offset"}},
		{"quakec", {"--cd", "--base", "--skin"}},
		{"quake2", {"--cd", "--prefix", "--scale"}},
		{"gzdoom", {"--actor", "--path", "--model", "--skin", "--scale", "--offset", "--sprite", "--angle-offset"}}};
	for (const auto &key : seen)
		if (!flags.contains(key) && !QSet<QString>{"--input", "--target", "--output", "--settings-file", "--locale", "--catalog-root"}.contains(key) &&
			!specific.value(target).contains(key))
			return fail(2, Text::tr("Option %1 does not apply to --target %2.").arg(key, target));
	if (seen.contains("--set") && seen.contains("--output"))
		return fail(2, Text::tr("Write either one declaration with --output or the whole model set with --set."));
	const auto output = values.value("--output", "-");
	const bool dry = seen.contains("--dry-run"), toStdout = !seen.contains("--set") && output == "-";
	if (toStdout && (dry || seen.contains("--overwrite")))
		return fail(2, Text::tr("--dry-run and --overwrite apply to written files."));
	ModelDocument document;
	QString error;
	if (!document.load(input, &error))
		return fail(1, error);
	const auto &mesh = document.mesh();
	const auto name = values.value("--name", baseName(input));
	const auto sourceName = QFileInfo(input).fileName();
	QString text;
	QVector<ModelGameFile> files;
	if (target == "doom3")
	{
		ModelDoom3DefOptions o;
		o.name = name;
		o.meshPath = values.value("--mesh-path", QStringLiteral("models/md5/%1/%1.md5mesh").arg(name));
		o.animDirectory = values.value("--anim-dir");
		o.skin = values.value("--skin");
		o.inherit = values.value("--inherit");
		o.sourceName = sourceName;
		if (seen.contains("--offset") && !(o.hasOffset = triple(values.value("--offset"), &o.offset)))
			return fail(2, Text::tr("Offset requires three comma-separated numbers."));
		if (seen.contains("--set"))
		{
			if (!modelDoom3ModelSet(mesh, o, &files, &error))
				return fail(4, error);
			text = QString::fromUtf8(files.last().bytes);
		}
		else
			text = modelDoom3ModelDefText(mesh, o, &error);
	}
	else if (target == "quakec")
	{
		ModelQuakeCFrameOptions o;
		o.modelDirectory = values.value("--cd");
		o.base = values.value("--base", o.base);
		o.skin = values.value("--skin", o.skin);
		o.sourceName = sourceName;
		text = modelQuakeCFrameMacrosText(mesh, o, &error);
	}
	else if (target == "quake2")
	{
		ModelQuake2FrameOptions o;
		o.modelDirectory = values.value("--cd");
		o.prefix = values.value("--prefix", o.prefix);
		o.sourceName = sourceName;
		if (seen.contains("--scale"))
		{
			bool ok = false;
			o.scale = values.value("--scale").toDouble(&ok);
			if (!ok)
				return fail(2, Text::tr("Scale must be a positive number."));
		}
		text = modelQuake2FrameHeaderText(mesh, o, &error);
	}
	else
	{
		ModelGzdoomModeldefOptions o;
		o.actor = values.value("--actor", name);
		o.path = values.value("--path", QStringLiteral("models/%1").arg(name));
		o.model = values.value("--model", QStringLiteral("%1.md3").arg(name));
		o.skin = values.value("--skin");
		auto sprite = name.toUpper();
		static const QRegularExpression invalid(QStringLiteral("[^A-Z0-9]"));
		sprite.remove(invalid);
		o.sprite = values.value("--sprite", (sprite + QStringLiteral("XXX")).left(3));
		o.sourceName = sourceName;
		if ((seen.contains("--scale") && !triple(values.value("--scale"), &o.scale)) ||
			(seen.contains("--offset") && !triple(values.value("--offset"), &o.offset)))
			return fail(2, Text::tr("Scale and offset require three comma-separated numbers."));
		if (seen.contains("--angle-offset"))
		{
			bool ok = false;
			o.angleOffset = values.value("--angle-offset").toDouble(&ok);
			if (!ok)
				return fail(2, Text::tr("The angle offset must be a number."));
		}
		text = modelGzdoomModeldefText(mesh, o, &error);
	}
	if (text.isEmpty())
		return fail(4, error);
	if (files.isEmpty() && !toStdout)
		files.append({output, text.toUtf8(), QStringLiteral("declaration")});
	// Every destination is checked before anything is written.
	const auto root = values.value("--set");
	QVector<ModelWriteTarget> targets;
	QJsonArray planned;
	for (const auto &file : files)
	{
		const auto path = root.isEmpty() ? file.path : QDir(root).filePath(file.path);
		if (modelPathsReferToSameFile(path, input))
			return fail(1, Text::tr("The output cannot replace the input, including aliases of the same file."));
		auto write = inspectModelWriteTarget(path);
		if (!write.isValid())
			return fail(1, write.error);
		if (write.existed && !seen.contains("--overwrite"))
			return fail(1, Text::tr("%1 exists; use --overwrite to replace it.").arg(QDir::toNativeSeparators(path)));
		targets.append(write);
		planned.append(QJsonObject{{"path", QFileInfo(path).absoluteFilePath()}, {"role", file.role}, {"bytes", int(file.bytes.size())}});
	}
	if (!root.isEmpty() && !dry && !QDir().mkpath(root))
		return fail(1, Text::tr("Cannot create the folder %1.").arg(root));
	for (int i = 0; i < files.size() && !dry; ++i)
	{
		QDir().mkpath(QFileInfo(targets[i].path).absolutePath());
		auto write = inspectModelWriteTarget(targets[i].path);
		if (!write.isValid() || !writeModelFile(write, files[i].bytes, &error))
			return fail(1, write.isValid() ? error : write.error);
	}
	ModelToolsCliResult result;
	if (toStdout)
		result.lines << text;
	else
		result.lines << (dry ? Text::tr("Validated %n file(s); nothing written.", nullptr, int(files.size()))
							 : Text::tr("Wrote %n file(s).", nullptr, int(files.size())));
	for (const auto &file : planned)
		if (!toStdout)
			result.lines << QDir::toNativeSeparators(file.toObject()["path"].toString());
	result.payload = {{"source", QFileInfo(input).absoluteFilePath()},
					  {"target", target},
					  {"text", text},
					  {"files", planned},
					  {"written", !toStdout && !dry},
					  {"dryRun", dry}};
	return result;
}
} // namespace vibestudio::cli
