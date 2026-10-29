#include "cli/model_tools.h"
#include "core/model_document.h"
#include "core/model_uv_health.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <cmath>

namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelUvHealthCli)
};
ModelToolsCliResult fail(int code, const QString &message)
{
	ModelToolsCliResult result;
	result.exitCode = code;
	result.error = message;
	return result;
}
bool integer(const QString &text, int *value)
{
	bool ok = false;
	*value = text.toInt(&ok);
	return ok && *value >= 0 && text == QString::number(*value);
}
} // namespace

ModelToolsCliResult runModelUvHealth(const QStringList &args)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--health"};
	const QSet<QString> options{"--input",		   "--surface", "--reference-frame", "--texture-size", "--stretch-limit",
								"--fail-on",	   "--settings-file", "--locale",	  "--catalog-root"};
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
	if (positional.size() != (input ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "uv")
		return fail(2, Text::tr("Provide one model source, positionally or with --input."));
	ModelUvHealthOptions health;
	int surface = 0;
	if ((seen.contains("--surface") && !integer(values["--surface"], &surface)) ||
		(seen.contains("--reference-frame") && !integer(values["--reference-frame"], &health.referenceFrame)))
		return fail(2, Text::tr("Surface and reference pose must be nonnegative integer indices."));
	bool explicitSize = false;
	if (seen.contains("--texture-size"))
	{
		const auto dimensions = values["--texture-size"].split('x');
		int width = 0, height = 0;
		if (dimensions.size() < 1 || dimensions.size() > 2 || !integer(dimensions[0], &width) ||
			(dimensions.size() == 2 && !integer(dimensions[1], &height)) || width < 1 || width > 65536 ||
			(dimensions.size() == 2 && (height < 1 || height > 65536)))
			return fail(2, Text::tr("Provide --texture-size N or WIDTHxHEIGHT from 1 to 65,536 pixels."));
		health.textureSize = {width, dimensions.size() == 1 ? width : height};
		explicitSize = true;
	}
	double stretchLimit = 0;
	if (seen.contains("--stretch-limit"))
	{
		bool ok = false;
		stretchLimit = values["--stretch-limit"].toDouble(&ok);
		if (!ok || !std::isfinite(stretchLimit) || stretchLimit < 1 || stretchLimit > 1000000)
			return fail(2, Text::tr("The stretch limit must be a number from 1 to 1,000,000."));
	}
	QStringList failOn;
	if (seen.contains("--fail-on"))
	{
		const QSet<QString> known{"overlap", "flipped", "outside", "collapsed", "stretch", "flood-fill"};
		for (auto token : values["--fail-on"].split(','))
		{
			token = token.trimmed();
			const int mark = std::max(token.indexOf('>'), token.indexOf('='));
			if (mark > 0 && token.left(mark) == "stretch")
			{
				bool ok = false;
				const double limit = token.mid(mark + 1).toDouble(&ok);
				if (!ok || !std::isfinite(limit) || limit < 1 || limit > 1000000 || (stretchLimit > 0 && stretchLimit != limit))
					return fail(2, Text::tr("Write stretch>N with N from 1 to 1,000,000, matching any --stretch-limit."));
				stretchLimit = limit;
				token = "stretch";
			}
			if (!known.contains(token) || failOn.contains(token))
				return fail(2, Text::tr("--fail-on takes a comma-separated list of overlap, flipped, outside, collapsed, stretch>N and "
										"flood-fill, each once."));
			failOn.append(token);
		}
	}
	if (stretchLimit > 0)
		health.stretchLimit = stretchLimit;
	const auto source = input ? values["--input"] : positional[2];
	QFile file(source);
	if (!file.open(QIODevice::ReadOnly) || file.size() > modelDocumentMaxSourceBytes)
		return fail(1, Text::tr("The model cannot be read or exceeds 64 MiB."));
	const auto bytes = file.readAll();
	ModelMesh mesh;
	QString error;
	if (!importEditableModel(QFileInfo(source).absoluteFilePath(), bytes, &mesh, &error))
		return fail(4, error);
	if (surface >= mesh.surfaces.size())
		return fail(2, Text::tr("Choose an existing model surface."));
	if (!explicitSize)
		health.textureSize = modelUvHealthDefaultTextureSize(mesh);
	ModelUvHealthReport report;
	if (!inspectModelUvHealth(mesh, surface, health, &report, &error))
		return fail(4, error);
	ModelToolsCliResult result;
	result.payload = modelUvHealthJson(report);
	result.payload.insert("source", QFileInfo(source).absoluteFilePath());
	QStringList failed;
	for (const auto &problem : failOn)
		if (modelUvHealthHas(report, problem))
			failed.append(problem);
	result.payload.insert("failOn", QJsonArray::fromStringList(failOn));
	result.payload.insert("failed", QJsonArray::fromStringList(failed));
	const int overlapping = int(report.overlapping.size()), flipped = int(report.flipped.size()), outside = int(report.outside.size()),
			  stretched = int(report.stretched.size()), stacked = int(report.mirrorStacked.size());
	const int collapsed = int(report.collapsedUv.size() + report.collapsedGeometry.size());
	result.lines << Text::tr("Surface %1 (%2): UV health against %3 × %4 texels, pose %5.")
						.arg(surface)
						.arg(mesh.surfaces[surface].name)
						.arg(report.textureSize.width())
						.arg(report.textureSize.height())
						.arg(report.referenceFrame);
	result.lines << Text::tr("Overlapping faces: %1 (%2 pairs%3). Faces on stacked mirror halves: %4.")
						.arg(overlapping)
						.arg(report.overlapPairCount)
						.arg(report.overlapLimitReached ? Text::tr("; the check reached its work limit") : QString())
						.arg(stacked);
	result.lines << Text::tr("Flipped faces: %1. Mirrored islands: %2. Faces outside the 0–1 tile: %3. Collapsed faces: %4.")
						.arg(flipped)
						.arg(report.mirroredIslands)
						.arg(outside)
						.arg(collapsed);
	result.lines << Text::tr("Stretched faces: %1 over %2; largest stretch %3. Largest texel-centre error: %4 texels.")
						.arg(stretched)
						.arg(report.stretchLimit, 0, 'g', 6)
						.arg(report.maxStretch, 0, 'g', 6)
						.arg(report.maxTexelError, 0, 'g', 6);
	for (const auto &native : report.native)
	{
		if (native.available)
			result.lines << Text::tr("%1: %2 stored vertices (limit %3), %4 drawn.")
								.arg(native.format.toUpper())
								.arg(native.stored)
								.arg(native.limit)
								.arg(native.render);
		else
			result.lines << Text::tr("%1: not estimated. %2").arg(native.format.toUpper(), native.note);
	}
	if (report.floodFillChecked)
		result.lines << (report.floodFillHazard()
							 ? Text::tr("Flood fill: the first texel's colour (index %1) reaches %2 texels the UVs use. GLQuake-style "
										"renderers repaint them.")
								   .arg(report.floodFillIndex)
								   .arg(report.floodFillUsedTexels)
							 : Text::tr("Flood fill: no used texel is in the first texel's colour region."));
	if (!failed.isEmpty())
	{
		result.exitCode = 4;
		result.error = Text::tr("UV health failed: %1.").arg(failed.join(QStringLiteral(", ")));
	}
	return result;
}
} // namespace vibestudio::cli
