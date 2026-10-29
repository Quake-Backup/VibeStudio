#include "cli/model_tools.h"
#include "core/idtech_image.h"
#include "core/model_document.h"
#include "core/model_file_io.h"
#include "core/model_mdl.h"
#include "core/model_skin_transfer.h"
#include "core/model_uv_health.h"
#include "core/model_uv_layout.h"
#include "core/texture_export.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <cstring>

// `model uv-layout` (skin templates) and `model skin-transfer` (rebaking a
// skin onto changed UVs), both writing images through the guarded model writer.
namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelUvLayoutCli)
};
ModelToolsCliResult fail(int code, const QString &message)
{
	ModelToolsCliResult result;
	result.exitCode = code;
	result.error = message;
	return result;
}
struct Arguments
{
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	QString problem;
};
Arguments parse(const QStringList &args, const QSet<QString> &flags, const QSet<QString> &options)
{
	Arguments parsed;
	for (int i = 1; i < args.size(); ++i)
	{
		const auto arg = args[i];
		if (!arg.startsWith('-'))
		{
			parsed.positional.append(arg);
			continue;
		}
		const int equal = int(arg.indexOf('='));
		const auto key = equal < 0 ? arg : arg.left(equal);
		if (parsed.seen.contains(key))
		{
			parsed.problem = Text::tr("Repeated option: %1.").arg(key);
			return parsed;
		}
		parsed.seen.insert(key);
		if (flags.contains(key) && equal < 0)
			continue;
		if (!options.contains(key))
		{
			parsed.problem = Text::tr("Unknown or invalid option: %1.").arg(key);
			return parsed;
		}
		const auto value = equal < 0 ? args.value(++i) : arg.mid(equal + 1);
		if (value.isEmpty() || value.startsWith(QStringLiteral("--")))
		{
			parsed.problem = Text::tr("Option %1 requires a value.").arg(key);
			return parsed;
		}
		parsed.values[key] = value;
	}
	return parsed;
}
bool size(const QString &text, QSize *result, int maximum)
{
	const auto match = QRegularExpression(QStringLiteral("\\A([0-9]+)(?:x([0-9]+))?\\z")).match(text);
	const int width = match.captured(1).toInt(), height = match.captured(2).isEmpty() ? width : match.captured(2).toInt();
	if (!match.hasMatch() || width < 1 || height < 1 || width > maximum || height > maximum)
		return false;
	*result = {width, height};
	return true;
}
bool loadMesh(const QString &path, ModelMesh *mesh, QString *error)
{
	QByteArray bytes;
	return readModelFile(path, &bytes, error) && importEditableModel(QFileInfo(path).absoluteFilePath(), bytes, mesh, error);
}
IdTechPaletteResolution paletteFromTable(const QVector<QRgb> &table)
{
	IdTechPaletteResolution resolution;
	resolution.palette.id = QStringLiteral("skin");
	resolution.palette.colors = table;
	resolution.palette.colors.resize(256);
	resolution.requestedPaletteId = QStringLiteral("skin");
	return resolution;
}
// Encodes by suffix: png, tga or pcx. Indexed images keep their indices.
bool encode(const QImage &image, const QString &path, const IdTechPaletteResolution &palette, QColor matte, QByteArray *bytes,
			QJsonObject *report, QString *error)
{
	const auto suffix = QFileInfo(path).suffix().toLower();
	TextureExportOptions options;
	const bool indexed = image.format() == QImage::Format_Indexed8;
	if (suffix == QStringLiteral("png"))
		options.format = indexed ? TextureExportFormat::IndexedPng : TextureExportFormat::Png;
	else if (suffix == QStringLiteral("tga"))
		options.format = TextureExportFormat::Targa;
	else if (suffix == QStringLiteral("pcx"))
	{
		options.format = TextureExportFormat::Pcx;
		options.alpha = TextureExportAlpha::Matte;
		options.matte = matte;
	}
	else
	{
		*error = Text::tr("Write a .png, .tga or .pcx image.");
		return false;
	}
	options.allowGeneratedPalette = true;
	options.fullbright = TextureFullbrightMode::Allow;
	const auto result = encodeTextureExport(image, options, palette);
	if (!result.succeeded)
	{
		*error = result.error;
		return false;
	}
	*bytes = result.bytes;
	*report = textureExportReportJson(options, palette, result);
	return true;
}
bool write(const QString &source, const QString &output, bool overwrite, bool dry, const QByteArray &bytes, QString *error, int *code)
{
	*code = 1;
	if (modelPathsReferToSameFile(source, output))
	{
		*code = 2;
		*error = Text::tr("Choose an output different from the source, including aliases of the same file.");
		return false;
	}
	const auto target = inspectModelWriteTarget(output);
	if (!target.isValid())
	{
		*error = target.error;
		return false;
	}
	if (target.existed && !overwrite)
	{
		*error = Text::tr("The output already exists; use --overwrite to replace it.");
		return false;
	}
	return dry ? validateModelWriteTarget(target, error) : writeModelFile(target, bytes, error);
}
} // namespace

ModelToolsCliResult runModelUvLayout(const QStringList &args)
{
	const auto parsed = parse(args, {"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite", "--no-seams", "--antialias", "--wrap",
									 "--no-tints"},
							  {"--input", "--surface", "--surfaces", "--size", "--output", "--fill", "--line-width", "--palette",
							   "--background", "--settings-file", "--locale", "--catalog-root"});
	if (!parsed.problem.isEmpty())
		return fail(2, parsed.problem);
	const auto &seen = parsed.seen;
	const auto &values = parsed.values;
	const bool input = seen.contains("--input");
	if (parsed.positional.size() != (input ? 2 : 3) || parsed.positional.value(1) != "uv-layout" || !values.contains("--output"))
		return fail(2, Text::tr("Provide one model source, positionally or with --input, and --output with a .png, .tga or .pcx path."));
	const auto source = input ? values["--input"] : parsed.positional[2];
	ModelUvLayoutOptions options;
	options.seams = !seen.contains("--no-seams");
	options.antialias = seen.contains("--antialias");
	options.wrap = seen.contains("--wrap");
	options.islandTints = !seen.contains("--no-tints");
	bool ok = true;
	if (values.contains("--fill"))
	{
		options.fillOpacity = values["--fill"].toDouble(&ok);
		if (!ok || !std::isfinite(options.fillOpacity) || options.fillOpacity < 0 || options.fillOpacity > 1)
			return fail(2, Text::tr("--fill takes an island fill opacity from 0 to 1."));
	}
	if (values.contains("--line-width"))
	{
		options.lineWidth = values["--line-width"].toDouble(&ok);
		if (!ok || !std::isfinite(options.lineWidth) || options.lineWidth <= 0 || options.lineWidth > 64)
			return fail(2, Text::tr("--line-width takes a width above 0 and up to 64 pixels."));
	}
	const auto background = values.value("--background", QStringLiteral("transparent"));
	if (background != QStringLiteral("transparent") && background != QStringLiteral("white") && background != QStringLiteral("black"))
		return fail(2, Text::tr("--background takes transparent, white or black."));
	options.background = background == QStringLiteral("white") ? QColor(Qt::white)
						 : background == QStringLiteral("black") ? QColor(Qt::black)
															  : QColor(0, 0, 0, 0);
	if (background == QStringLiteral("black"))
		options.edge = Qt::white;
	ModelMesh mesh;
	QString error;
	if (!loadMesh(source, &mesh, &error))
		return fail(1, error);
	QSize layoutSize = modelUvHealthDefaultTextureSize(mesh);
	if (values.contains("--size") && !size(values["--size"], &layoutSize, modelUvLayoutMaxSize))
		return fail(2, Text::tr("--size takes N or WIDTHxHEIGHT pixels from 1 to %1.").arg(modelUvLayoutMaxSize));
	int active = 0;
	if (values.contains("--surface"))
	{
		active = values["--surface"].toInt(&ok);
		if (!ok || active < 0 || active >= mesh.surfaces.size())
			return fail(2, Text::tr("--surface takes an existing zero-based surface index."));
	}
	QVector<int> surfaces{active};
	if (values.contains("--surfaces"))
	{
		const auto scope = values["--surfaces"];
		surfaces.clear();
		if (scope == QStringLiteral("all"))
			for (int s = 0; s < mesh.surfaces.size(); ++s)
				surfaces.append(s);
		else if (scope == QStringLiteral("material"))
			surfaces = modelUvLayoutMaterialSurfaces(mesh, active);
		else
			for (const auto &part : scope.split(','))
			{
				const int s = part.toInt(&ok);
				if (!ok || s < 0 || s >= mesh.surfaces.size() || surfaces.contains(s))
					return fail(2, Text::tr("--surfaces takes all, material (surfaces sharing --surface's material) or unique surface indices."));
				surfaces.append(s);
			}
	}
	QImage image;
	ModelUvLayoutReport layout;
	if (!renderModelUvLayout(mesh, surfaces, layoutSize, options, &image, &layout, &error))
		return fail(4, error);
	IdTechPaletteResolution palette;
	palette.requestedPaletteId = values.value("--palette", QStringLiteral("quake"));
	palette.palette = generatedIdTechPalette(palette.requestedPaletteId);
	if (!palette.palette.isValid())
		return fail(2, Text::tr("--palette names a built-in palette such as quake or quake2."));
	QByteArray bytes;
	QJsonObject encoded;
	const QColor matte = background == QStringLiteral("black") ? QColor(Qt::black) : QColor(Qt::white);
	if (!encode(image, values["--output"], palette, matte, &bytes, &encoded, &error))
		return fail(2, error);
	const bool dry = seen.contains("--dry-run");
	int code = 1;
	if (!write(source, values["--output"], seen.contains("--overwrite"), dry, bytes, &error, &code))
		return fail(code, error);
	ModelToolsCliResult result;
	result.payload = {{"source", QFileInfo(source).absoluteFilePath()},
					  {"output", QFileInfo(values["--output"]).absoluteFilePath()},
					  {"layout", modelUvLayoutJson(layout)},
					  {"encoding", encoded},
					  {"bytes", qint64(bytes.size())},
					  {"dryRun", dry},
					  {"written", !dry}};
	result.lines << Text::tr("UV layout of %1 surfaces, %2 faces and %3 islands on %4 × %5 pixels%6 %7.")
						.arg(layout.surfaces)
						.arg(layout.faces)
						.arg(layout.islands)
						.arg(layoutSize.width())
						.arg(layoutSize.height())
						.arg(dry ? Text::tr("; dry run, would write") : Text::tr("; wrote"))
						.arg(QFileInfo(values["--output"]).absoluteFilePath());
	if (layout.outsideFaces)
		result.lines << Text::tr("%1 faces reach outside the 0–1 tile%2.")
							.arg(layout.outsideFaces)
							.arg(options.wrap ? Text::tr(" and are drawn wrapped into it") : Text::tr("; --wrap draws them inside it"));
	return result;
}

ModelToolsCliResult runModelSkinTransfer(const QStringList &args)
{
	const auto parsed = parse(args, {"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite", "--no-wrap"},
							  {"--from", "--to", "--surface", "--texture", "--size", "--output", "--filter", "--padding", "--settings-file",
							   "--locale", "--catalog-root"});
	if (!parsed.problem.isEmpty())
		return fail(2, parsed.problem);
	const auto &seen = parsed.seen;
	const auto &values = parsed.values;
	if (parsed.positional.size() != 2 || parsed.positional.value(1) != "skin-transfer" || !values.contains("--from") || !values.contains("--to") ||
		!values.contains("--texture") || !values.contains("--output"))
		return fail(2, Text::tr("Provide --from (the source before the UV edit), --to (after), --texture (the old skin) and --output (.png, "
								".tga or .pcx)."));
	ModelSkinTransferOptions options;
	options.wrap = !seen.contains("--no-wrap");
	const auto filter = values.value("--filter", QStringLiteral("nearest"));
	if (filter != QStringLiteral("nearest") && filter != QStringLiteral("bilinear"))
		return fail(2, Text::tr("--filter takes nearest or bilinear."));
	options.filter = filter == QStringLiteral("bilinear") ? ModelSkinTransferFilter::Bilinear : ModelSkinTransferFilter::Nearest;
	bool ok = true;
	if (values.contains("--padding"))
	{
		options.padding = values["--padding"].toInt(&ok);
		if (!ok || options.padding < 0 || options.padding > 64)
			return fail(2, Text::tr("--padding takes 0 to 64 texels."));
	}
	int surface = 0;
	if (values.contains("--surface"))
	{
		surface = values["--surface"].toInt(&ok);
		if (!ok || surface < 0)
			return fail(2, Text::tr("--surface takes an existing zero-based surface index."));
	}
	ModelMesh before, after;
	QString error;
	if (!loadMesh(values["--from"], &before, &error) || !loadMesh(values["--to"], &after, &error))
		return fail(1, error);
	if (surface >= before.surfaces.size() || surface >= after.surfaces.size())
		return fail(2, Text::tr("--surface takes an existing zero-based surface index."));
	// Indexed formats keep their indices; everything else is colour.
	QByteArray textureBytes;
	if (!readModelFile(values["--texture"], &textureBytes, &error))
		return fail(1, error);
	QImage texture;
	const auto suffix = QFileInfo(values["--texture"]).suffix().toLower();
	if (suffix == QStringLiteral("pcx") || suffix == QStringLiteral("lmp") || suffix == QStringLiteral("wal"))
	{
		ModelMdlSkinInput skin;
		if (!decodeModelMdlSkin(values["--texture"], textureBytes, generatedIdTechPalette(QStringLiteral("quake")), &skin, &error))
			return fail(4, error);
		texture = QImage(skin.size, QImage::Format_Indexed8);
		IdTechPalette palette;
		parseIdTechPaletteBytes(skin.palette, QStringLiteral("quake"), &palette);
		QVector<QRgb> table(palette.colors.cbegin(), palette.colors.cend());
		table.resize(256);
		texture.setColorTable(table);
		for (int y = 0; y < skin.size.height(); ++y)
			std::memcpy(texture.scanLine(y), skin.pixels.constData() + qint64(y) * skin.size.width(), size_t(skin.size.width()));
	}
	else
		texture = QImage::fromData(textureBytes);
	if (texture.isNull())
		return fail(4, Text::tr("The skin image could not be read."));
	QSize outputSize = texture.size();
	if (values.contains("--size") && !size(values["--size"], &outputSize, 4096))
		return fail(2, Text::tr("--size takes N or WIDTHxHEIGHT pixels from 1 to 4,096."));
	QImage result;
	ModelSkinTransferReport report;
	if (!transferModelSkin(before.surfaces[surface], after.surfaces[surface], texture, outputSize, options, &result, &report, &error))
		return fail(4, error);
	QByteArray bytes;
	QJsonObject encoded;
	const auto palette = result.format() == QImage::Format_Indexed8 ? paletteFromTable(result.colorTable())
																	: IdTechPaletteResolution{generatedIdTechPalette(QStringLiteral("quake"))};
	if (!encode(result, values["--output"], palette, Qt::black, &bytes, &encoded, &error))
		return fail(2, error);
	const bool dry = seen.contains("--dry-run");
	int code = 1;
	for (const auto &input : {values["--from"], values["--to"], values["--texture"]})
		if (modelPathsReferToSameFile(input, values["--output"]))
			return fail(2, Text::tr("Choose an output different from every input, including aliases of the same file."));
	if (!write(values["--texture"], values["--output"], seen.contains("--overwrite"), dry, bytes, &error, &code))
		return fail(code, error);
	ModelToolsCliResult output;
	output.payload = {{"from", QFileInfo(values["--from"]).absoluteFilePath()},
					  {"to", QFileInfo(values["--to"]).absoluteFilePath()},
					  {"surface", surface},
					  {"output", QFileInfo(values["--output"]).absoluteFilePath()},
					  {"transfer", modelSkinTransferJson(report)},
					  {"encoding", encoded},
					  {"dryRun", dry},
					  {"written", !dry}};
	output.lines << Text::tr("Transferred %1 faces onto %2 × %3 texels (%4 painted, %5 padding)%6 %7.")
						.arg(report.faces)
						.arg(outputSize.width())
						.arg(outputSize.height())
						.arg(report.texels)
						.arg(report.paddedTexels)
						.arg(dry ? Text::tr("; dry run, would write") : Text::tr("; wrote"))
						.arg(QFileInfo(values["--output"]).absoluteFilePath());
	if (!report.missingFaces.isEmpty() || !report.collapsedFaces.isEmpty())
		output.lines << Text::tr("Faces without a match: %1; faces with collapsed UVs: %2.")
							.arg(report.missingFaces.size())
							.arg(report.collapsedFaces.size());
	return output;
}
} // namespace vibestudio::cli
