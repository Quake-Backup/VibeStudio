#include "cli/model_tools.h"
#include "core/model_document.h"
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
	Q_DECLARE_TR_FUNCTIONS(ModelUvDensityCli)
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
ModelToolsCliResult runModelUvDensity(const QStringList &args)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite"};
	const QSet<QString> options{"--input",	"--output", "--surface",	   "--reference-frame", "--texture-size", "--faces",
								"--target", "--mode",	"--settings-file", "--locale",			"--catalog-root", "--surfaces",
								"--texture-size-for"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional, sizesFor;
	for (int i = 1; i < args.size(); ++i)
	{
		const auto arg = args[i];
		if (!arg.startsWith('-'))
		{
			positional.append(arg);
			continue;
		}
		const int equal = int(arg.indexOf('='));
		const auto key = equal < 0 ? arg : arg.left(equal);
		// --texture-size-for N=WIDTHxHEIGHT repeats, once per surface.
		if (key == "--texture-size-for")
		{
			const auto value = equal < 0 ? args.value(++i) : arg.mid(equal + 1);
			if (value.isEmpty() || value.startsWith("--"))
				return fail(2, Text::tr("Option %1 requires a value.").arg(key));
			sizesFor.append(value);
			seen.insert(key);
			continue;
		}
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
	const bool input = seen.contains("--input"), adjusting = seen.contains("--target");
	if (positional.size() != (input ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "uv-density")
		return fail(2, Text::tr("Provide one editable model source, positionally or with --input."));
	ModelEdit edit;
	edit.kind = ModelEditKind::SetUvDensity;
	auto &o = edit.uvDensity;
	const auto dimensions = values.value("--texture-size").split('x');
	int width = 0, height = 0;
	if (dimensions.size() < 1 || dimensions.size() > 2 || !integer(dimensions[0], &width) ||
		(dimensions.size() == 2 && !integer(dimensions[1], &height)) || width < 1 || width > 65536 ||
		(dimensions.size() == 2 && (height < 1 || height > 65536)))
		return fail(2, Text::tr("Provide --texture-size N or WIDTHxHEIGHT using original image dimensions from 1 to 65,536 pixels."));
	o.textureSize = {width, dimensions.size() == 1 ? width : height};
	if ((seen.contains("--surface") && !integer(values["--surface"], &edit.selection.surface)) ||
		(seen.contains("--reference-frame") && !integer(values["--reference-frame"], &o.referenceFrame)))
		return fail(2, Text::tr("Surface and reference pose must be nonnegative integer indices."));
	// Per-surface original skin sizes: N=WIDTHxHEIGHT.
	QHash<int, QSize> sizes;
	for (const auto &entry : sizesFor)
	{
		const auto parts = entry.split('=');
		int index = -1, w = 0, h = 0;
		const auto wh = parts.value(1).split('x');
		if (parts.size() != 2 || !integer(parts[0], &index) || wh.size() != 2 || !integer(wh[0], &w) || !integer(wh[1], &h) || w < 1 ||
			h < 1 || w > 65536 || h > 65536 || sizes.contains(index))
			return fail(2, Text::tr("Write --texture-size-for SURFACE=WIDTHxHEIGHT once per surface, with sizes from 1 to 65,536 pixels."));
		sizes.insert(index, {w, h});
	}
	if (adjusting)
	{
		bool ok = false;
		o.target = values["--target"].toDouble(&ok);
		if (!ok || !std::isfinite(o.target) || o.target < .000001 || o.target > 1000000)
			return fail(2, Text::tr("Target density must be from 0.000001 to 1,000,000 pixels per model unit."));
		const auto mode = values.value("--mode", "individual");
		if (mode != "individual" && mode != "group")
			return fail(2, Text::tr("Density mode must be individual or group."));
		o.individual = mode == "individual";
		if (!values.value("--output").endsWith(".mesh.json", Qt::CaseInsensitive))
			return fail(2, Text::tr("Setting UV density requires --output ending in .mesh.json."));
	}
	else if (seen.contains("--mode") || seen.contains("--output") || seen.contains("--dry-run") || seen.contains("--overwrite"))
		return fail(2, Text::tr("Mode, output, dry-run and overwrite require --target. Omit them to inspect density."));
	const auto source = input ? values["--input"] : positional[2];
	if (adjusting && modelPathsReferToSameFile(source, values["--output"]))
		return fail(2, Text::tr("Choose an output different from the source, including aliases of the same file."));
	ModelDocument document;
	QString error;
	if (!document.load(source, &error))
		return fail(1, error);
	if (edit.selection.surface >= document.mesh().surfaces.size())
		return fail(4, Text::tr("Choose an existing model surface."));
	// --surfaces: every surface measured or matched together; the first
	// listed (or --surface) holds the --faces selection.
	QVector<int> chosen;
	if (seen.contains("--surfaces"))
	{
		const auto scope = values["--surfaces"];
		if (scope == "all")
			for (int s = 0; s < document.mesh().surfaces.size(); ++s)
				chosen.append(s);
		else
			for (const auto &part : scope.split(','))
			{
				int s = -1;
				if (!integer(part, &s) || s >= document.mesh().surfaces.size() || chosen.contains(s))
					return fail(2, Text::tr("--surfaces takes all or unique existing surface indices."));
				chosen.append(s);
			}
		if (!seen.contains("--surface"))
			edit.selection.surface = chosen.first();
		else if (!chosen.contains(edit.selection.surface))
			return fail(2, Text::tr("--surface must be one of --surfaces."));
	}
	for (auto it = sizes.cbegin(); it != sizes.cend(); ++it)
		if (it.key() >= document.mesh().surfaces.size() || (!chosen.isEmpty() && !chosen.contains(it.key())) ||
			(chosen.isEmpty() && it.key() != edit.selection.surface))
			return fail(2, Text::tr("--texture-size-for names surfaces being measured."));
	if (sizes.contains(edit.selection.surface))
		o.textureSize = sizes.value(edit.selection.surface);
	for (int s : chosen)
		if (s != edit.selection.surface)
		{
			o.surfaces.insert(s);
			if (sizes.contains(s))
				o.surfaceSizes.insert(s, sizes.value(s));
		}
	const auto &surface = document.mesh().surfaces[edit.selection.surface];
	const auto faceText = values.value("--faces", "all");
	if (faceText == "all")
		for (int f = 0; f < surface.triangles.size(); ++f)
			edit.selection.faces.insert(f);
	else
		for (const auto &part : faceText.split(','))
		{
			int face = -1;
			if (!integer(part, &face) || face >= surface.triangles.size() || edit.selection.faces.contains(face))
				return fail(2, Text::tr("Use --faces all or unique existing face indices; each expands to its complete UV island."));
			edit.selection.faces.insert(face);
		}
	ModelUvDensityReport before, after;
	if (!inspectModelUvDensity(surface, edit.selection.faces, o.textureSize, o.referenceFrame, &before, &error))
		return fail(4, error);
	// The other surfaces, whole, each on its own skin size.
	QVector<QPair<int, ModelUvDensityReport>> others;
	double modelArea = before.modelArea, pixelArea = before.pixelArea;
	for (int s : chosen)
	{
		if (s == edit.selection.surface)
			continue;
		ModelUvDensityReport report;
		if (!inspectModelUvDensity(document.mesh().surfaces[s], {}, o.surfaceSizes.value(s, o.textureSize), o.referenceFrame, &report, &error))
			return fail(4, error);
		modelArea += report.modelArea;
		pixelArea += report.pixelArea;
		others.append({s, report});
	}
	ModelToolsCliResult result;
	result.payload = {{"source", QFileInfo(source).absoluteFilePath()},
					  {"surface", edit.selection.surface},
					  {"before", modelUvDensityJson(before)},
					  {"written", false},
					  {"dryRun", seen.contains("--dry-run")}};
	if (adjusting)
	{
		const auto target = inspectModelWriteTarget(values["--output"]);
		if (!target.isValid())
			return fail(1, target.error);
		if (target.existed && !seen.contains("--overwrite"))
			return fail(1, Text::tr("The output already exists; use --overwrite to replace it."));
		if (!document.edit(edit, &error) || !inspectModelUvDensity(document.mesh().surfaces[edit.selection.surface], before.faces,
																   o.textureSize, o.referenceFrame, &after, &error))
			return fail(4, error);
		const bool dry = seen.contains("--dry-run");
		if (!document.save(values["--output"], seen.contains("--overwrite"), &error, {}, dry))
			return fail(1, error);
		result.payload.insert("after", modelUvDensityJson(after));
		QJsonArray matched;
		for (const auto &[s, report] : others)
		{
			ModelUvDensityReport changed;
			if (!inspectModelUvDensity(document.mesh().surfaces[s], {}, o.surfaceSizes.value(s, o.textureSize), o.referenceFrame, &changed,
									   &error))
				return fail(4, error);
			matched.append(QJsonObject{{"surface", s}, {"before", modelUvDensityJson(report)}, {"after", modelUvDensityJson(changed)}});
		}
		if (!others.isEmpty())
			result.payload.insert("otherSurfaces", matched);
		result.payload.insert("output", QFileInfo(values["--output"]).absoluteFilePath());
		result.payload.insert("target", o.target);
		result.payload.insert("mode", o.individual ? "individual" : "group");
		result.payload.insert("written", !dry);
	}
	if (!others.isEmpty())
	{
		QJsonArray measured;
		for (const auto &[s, report] : others)
			measured.append(QJsonObject{{"surface", s}, {"before", modelUvDensityJson(report)}});
		result.payload.insert("surfaces", measured);
		result.payload.insert("combinedPixelsPerUnit", modelArea > 0 ? std::sqrt(pixelArea / modelArea) : 0);
		for (const auto &[s, report] : others)
			result.lines << Text::tr("Surface %1: %2 pixels per model unit across %3 UV islands on %4 × %5 pixels.")
								.arg(s)
								.arg(report.density, 0, 'g', 9)
								.arg(report.islands.size())
								.arg(report.textureSize.width())
								.arg(report.textureSize.height());
		result.lines << Text::tr("Combined: %1 pixels per model unit.").arg(modelArea > 0 ? std::sqrt(pixelArea / modelArea) : 0, 0, 'g', 9);
	}
	result.lines << Text::tr("Surface %1: %2 pixels per model unit across %3 UV islands, using %4 × %5 pixels and pose %6.")
						.arg(edit.selection.surface)
						.arg(before.density, 0, 'g', 9)
						.arg(before.islands.size())
						.arg(width)
						.arg(o.textureSize.height())
						.arg(o.referenceFrame);
	for (int i = 0; i < before.islands.size(); ++i)
	{
		const auto &row = before.islands[i];
		result.lines
			<< Text::tr("Island %1: %2 faces; density %3; face range %4–%5; collapsed geometry %6, collapsed UVs %7, mirrored faces %8.")
				   .arg(row.island)
				   .arg(row.faces.size())
				   .arg(row.density, 0, 'g', 9)
				   .arg(row.minimum, 0, 'g', 9)
				   .arg(row.maximum, 0, 'g', 9)
				   .arg(row.collapsedGeometry)
				   .arg(row.collapsedUvs)
				   .arg(row.mirroredFaces);
		if (adjusting)
			result.lines << Text::tr("Result density: %1 pixels per model unit.").arg(after.islands[i].density, 0, 'g', 9);
	}
	return result;
}
} // namespace vibestudio::cli
