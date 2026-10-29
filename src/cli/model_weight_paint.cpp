#include "cli/model_tools.h"
#include "core/model_document.h"
#include "core/model_weight_stroke_recipe.h"
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
	Q_DECLARE_TR_FUNCTIONS(ModelWeightPaintCli)
};
ModelToolsCliResult fail(int code, const QString &message)
{
	ModelToolsCliResult r;
	r.exitCode = code;
	r.error = message;
	return r;
}
bool integer(const QString &value, int *out)
{
	bool ok = false;
	*out = value.toInt(&ok);
	return ok && *out >= 0 && value == QString::number(*out);
}
const QHash<QString, ModelWeightPaintMode> modes{{"replace", ModelWeightPaintMode::Replace},
												 {"add", ModelWeightPaintMode::Add},
												 {"subtract", ModelWeightPaintMode::Subtract},
												 {"smooth", ModelWeightPaintMode::Smooth}};
bool recipe(const QString &path, QVector<ModelWeightStroke> *strokes, QString *error)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > modelDocumentMaxSourceBytes)
	{
		*error = Text::tr("Cannot read a stroke file of at most 64 MiB: %1.").arg(path);
		return false;
	}
	const auto bytes = file.read(modelDocumentMaxSourceBytes + 1);
	QJsonParseError parse;
	const auto doc = QJsonDocument::fromJson(bytes, &parse);
	if (bytes.size() > modelDocumentMaxSourceBytes || parse.error != QJsonParseError::NoError || !doc.isObject())
	{
		*error = Text::tr("A stroke file needs version 1 and a strokes array.");
		return false;
	}
	// The recipe format is shared with recovery sidecars (core/model_weight_stroke_recipe.h).
	return parseModelWeightStrokeRecipe(doc.object(), strokes, error);
}
} // namespace
ModelToolsCliResult runModelWeightPaint(const QStringList &arguments)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite"};
	const QSet<QString> options{"--input",			 "--output",	  "--surface",		 "--joint",	  "--mode",
								"--value",			 "--strength",	  "--vertices",		 "--opacity", "--locked-joints",
								"--remainder-joint", "--stroke-file", "--settings-file", "--locale",  "--catalog-root"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	for (int i = 1; i < arguments.size(); ++i)
	{
		const auto arg = arguments[i];
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
		const auto value = equal < 0 ? arguments.value(++i) : arg.mid(equal + 1);
		if (value.isEmpty() || value.startsWith("--"))
			return fail(2, Text::tr("Option %1 requires a value.").arg(key));
		values[key] = value;
	}
	const bool input = seen.contains("--input"), replay = seen.contains("--stroke-file");
	if (positional.size() != (input ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "weight-paint")
		return fail(2, Text::tr("Provide one editable model source, positionally or with --input."));
	if (!values.value("--output").endsWith(".mesh.json", Qt::CaseInsensitive))
		return fail(2, Text::tr("Weight painting requires --output ending in .mesh.json."));
	if (replay)
		for (const auto &key :
			 {"--surface", "--joint", "--mode", "--value", "--strength", "--vertices", "--opacity", "--locked-joints", "--remainder-joint"})
			if (seen.contains(key))
				return fail(2, Text::tr("A stroke file cannot be combined with brush options."));
	ModelEdit edit;
	edit.kind = ModelEditKind::PaintSkinWeights;
	QString error;
	if (replay && !recipe(values["--stroke-file"], &edit.weightStrokes, &error))
		return fail(2, error);
	if (replay && modelPathsReferToSameFile(values["--output"], values["--stroke-file"]))
		return fail(1, Text::tr("The output cannot replace the stroke input, including aliases of the same file."));
	ModelWeightStroke s;
	double opacity = 1;
	if (!replay)
	{
		if (!integer(values.value("--surface"), &s.surface) || !seen.contains("--joint") || !seen.contains("--vertices"))
			return fail(2, Text::tr("Provide --surface, --joint and explicit --vertices all or a comma-separated list."));
		const auto mode = values.value("--mode", "replace");
		if (!modes.contains(mode))
			return fail(2, Text::tr("Brush mode must be replace, add, subtract or smooth."));
		s.mode = modes[mode];
		s.joint = values["--joint"];
		s.remainderJoint = values["--remainder-joint"];
		if (seen.contains("--locked-joints"))
			s.lockedJoints = values["--locked-joints"].split(',');
		if (s.mode == ModelWeightPaintMode::Smooth && seen.contains("--value"))
			return fail(2, Text::tr("Smooth does not accept --value."));
		for (const auto &key : {QStringLiteral("--value"), QStringLiteral("--strength"), QStringLiteral("--opacity")})
			if (seen.contains(key))
			{
				bool ok = false;
				const auto number = values[key].toDouble(&ok);
				if (!ok || !std::isfinite(number) || number < 0 || number > 1)
					return fail(2, Text::tr("Brush numbers must be from zero to one."));
				(key == "--value" ? s.value : key == "--strength" ? s.strength : opacity) = number;
			}
	}
	const auto source = input ? values["--input"] : positional[2];
	ModelDocument document;
	if (!document.load(source, &error))
		return fail(1, error);
	if (!replay)
	{
		if (s.surface >= document.mesh().surfaces.size())
			return fail(4, Text::tr("The surface does not exist."));
		if (values["--vertices"] == "all")
			for (int v = 0; v < document.mesh().surfaces[s.surface].vertexCount; ++v)
				s.samples.append({v, opacity});
		else
			for (const auto &value : values["--vertices"].split(','))
			{
				int vertex = -1;
				if (!integer(value, &vertex))
					return fail(2, Text::tr("Vertices must be nonnegative integers or all."));
				s.samples.append({vertex, opacity});
			}
		edit.weightStrokes = {s};
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
	QSet<int> surfaces;
	qint64 count = 0;
	for (const auto &stroke : edit.weightStrokes)
	{
		surfaces.insert(stroke.surface);
		count += stroke.samples.size();
	}
	ModelToolsCliResult result;
	result.payload = {{"source", QFileInfo(source).absoluteFilePath()},
					  {"output", QFileInfo(values["--output"]).absoluteFilePath()},
					  {"strokes", int(edit.weightStrokes.size())},
					  {"samples", count},
					  {"surfaces", int(surfaces.size())},
					  {"dryRun", dry},
					  {"written", !dry}};
	result.lines
		<< Text::tr("Reviewed strokes: %1. Samples: %2. Surfaces: %3.").arg(edit.weightStrokes.size()).arg(count).arg(surfaces.size());
	result.payload.insert("outputSource", modelSourceSaveJson(document));
	if (const auto packed = modelSourceSaveNotice(document); !packed.isEmpty())
		result.lines << packed;
	return result;
}
} // namespace vibestudio::cli
