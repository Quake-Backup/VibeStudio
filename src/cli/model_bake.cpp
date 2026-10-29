#include "cli/model_bake.h"
#include "core/model_bake.h"
#include "core/model_document.h"
#include "core/model_skin_paint.h"
#include "core/texture_export.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

#include <cmath>

namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelBakeCli)
};
ModelBakeCliResult fail(int code, const QString &message)
{
	return {code, message, {}, {}};
}
// An editable source, or any native model with its companions.
bool loadModel(const QString &path, ModelMesh *mesh, QString *error)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > modelDocumentMaxSourceBytes)
	{
		*error = Text::tr("The model cannot be read or exceeds 64 MiB: %1").arg(QDir::toNativeSeparators(path));
		return false;
	}
	const QByteArray bytes = file.readAll();
	if (path.endsWith(".mesh.json", Qt::CaseInsensitive))
		return parseEditableModel(bytes, mesh, error);
	*mesh = decodeModelMesh(QFileInfo(path).absoluteFilePath(), bytes, nullptr, {}, modelCompanionsFromFileSystem(QFileInfo(path).absoluteFilePath()));
	if (!mesh->error.isEmpty())
	{
		*error = mesh->error;
		return false;
	}
	return true;
}
} // namespace

ModelBakeCliResult runModelBake(const QStringList &arguments)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite"};
	const QSet<QString> valued{"--type", "--surface", "--frame", "--size", "--samples", "--distance", "--cage", "--padding", "--seed", "--ambient",
							   "--high", "--output", "--model-output", "--strength", "--settings-file", "--locale", "--catalog-root"};
	QStringList positional;
	QHash<QString, QString> values;
	QSet<QString> seen;
	for (int i = 1; i < arguments.size(); ++i)
	{
		const auto arg = arguments[i];
		if (!arg.startsWith('-'))
		{
			positional << arg;
			continue;
		}
		const int equal = int(arg.indexOf('='));
		const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key))
			return fail(2, Text::tr("Repeated option: %1.").arg(key));
		seen.insert(key);
		if (flags.contains(key) && equal < 0)
			continue;
		if (!valued.contains(key))
			return fail(2, Text::tr("Unexpected option: %1.").arg(arg));
		const auto value = equal >= 0 ? arg.mid(equal + 1) : i + 1 < arguments.size() && !arguments[i + 1].startsWith("--") ? arguments[++i] : QString();
		if (value.trimmed().isEmpty())
			return fail(2, Text::tr("Missing value for %1.").arg(key));
		values.insert(key, value);
	}
	const bool dryRun = seen.contains("--dry-run");
	if (positional.size() != 3 || positional[0] != "model" || positional[1] != "bake" ||
		(!dryRun && !values.contains("--output") && !values.contains("--model-output")))
		return fail(2, Text::tr("Use model bake <model> --type ao|object-normal|normal|lighting --output <map.png|map.tga>."));
	ModelBakeOptions options;
	if (!modelBakeTypeFromId(values.value("--type", "ao"), &options.type))
		return fail(2, Text::tr("--type takes ao, object-normal, normal or lighting."));
	bool ok = true;
	const auto integer = [&](const char *key, int low, int high, int *target) {
		if (!values.contains(key))
			return true;
		*target = values.value(key).toInt(&ok);
		return ok && *target >= low && *target <= high;
	};
	const auto real = [&](const char *key, double low, double high, double *target) {
		if (!values.contains(key))
			return true;
		*target = values.value(key).toDouble(&ok);
		return ok && std::isfinite(*target) && *target >= low && *target <= high;
	};
	if (values.contains("--size"))
	{
		const auto parts = values.value("--size").toLower().split('x');
		bool w = false, h = false;
		options.size = parts.size() == 2 ? QSize(parts[0].toInt(&w), parts[1].toInt(&h)) : QSize();
		if (!w || !h || options.size.width() < 8 || options.size.height() < 8 || options.size.width() > 4096 || options.size.height() > 4096)
			return fail(2, Text::tr("--size takes WIDTHxHEIGHT from 8x8 to 4096x4096."));
	}
	int frame = 0, seed = int(options.seed);
	if (!integer("--samples", 1, 1024, &options.samples) || !integer("--padding", 0, 64, &options.padding) || !integer("--frame", 0, 1 << 20, &frame) ||
		!integer("--seed", 0, 0x7fffffff, &seed) || !real("--distance", 0, 1e7, &options.maxDistance) || !real("--cage", 0, 1e7, &options.cageDistance) ||
		!real("--ambient", 0, 1, &options.ambient))
		return fail(2, Text::tr("--samples takes 1-1024, --padding 0-64, --frame and --seed whole numbers, --distance and --cage positive distances, --ambient 0-1."));
	options.seed = quint32(seed);
	QVector<int> surfaces;
	if (values.contains("--surface"))
	{
		for (const QString &part : values.value("--surface").split(',', Qt::SkipEmptyParts))
		{
			const int surface = part.toInt(&ok);
			if (!ok || surface < 0)
				return fail(2, Text::tr("--surface takes zero-based surface indexes separated by commas."));
			surfaces << surface;
		}
	}
	const QString input = positional[2];
	ModelMesh mesh, high;
	QString error;
	if (!loadModel(input, &mesh, &error))
		return fail(3, error);
	if (values.contains("--high") && !loadModel(values.value("--high"), &high, &error))
		return fail(3, error);
	const QString output = values.value("--output");
	if (!output.isEmpty())
	{
		const QFileInfo target(output);
		const QString suffix = target.suffix().toLower();
		if (suffix != "png" && suffix != "tga")
			return fail(2, Text::tr("--output writes a .png or .tga image."));
		for (const QString &source : {input, values.value("--high")})
			if (!source.isEmpty() && QFileInfo(source).absoluteFilePath().compare(target.absoluteFilePath(), Qt::CaseInsensitive) == 0)
				return fail(2, Text::tr("Write the map to another file; the models are never overwritten."));
		if (target.exists() && !seen.contains("--overwrite"))
			return fail(1, Text::tr("%1 exists; add --overwrite to replace it.").arg(QDir::toNativeSeparators(output)));
	}
	// --model-output shades the surface's MDL skin with the map, as the Mesh
	// Editor's Apply to Model does, and writes the editable source.
	const QString modelOutput = values.value("--model-output");
	double strength = 1.0;
	if (!real("--strength", 0, 1, &strength))
		return fail(2, Text::tr("--strength takes 0 to 1."));
	const int shadedSurface = surfaces.isEmpty() ? 0 : surfaces.first();
	if (!modelOutput.isEmpty())
	{
		if (!modelOutput.endsWith(".mesh.json", Qt::CaseInsensitive))
			return fail(2, Text::tr("--model-output writes an editable source (.mesh.json)."));
		if (options.type != ModelBakeType::AmbientOcclusion && options.type != ModelBakeType::Lighting)
			return fail(2, Text::tr("--model-output shades the skin with an ao or lighting map."));
		if (modelSkinPaintMdlSlot(mesh, shadedSurface) < 0)
			return fail(2, Text::tr("--model-output shades a model's MDL skin; surface %1 has none.").arg(shadedSurface));
		if (QFileInfo(modelOutput).absoluteFilePath().compare(QFileInfo(input).absoluteFilePath(), Qt::CaseInsensitive) == 0)
			return fail(2, Text::tr("Write the map and model to other files; the models are never overwritten."));
		if (QFileInfo::exists(modelOutput) && !seen.contains("--overwrite"))
			return fail(1, Text::tr("%1 exists; add --overwrite to replace it.").arg(QDir::toNativeSeparators(modelOutput)));
	}
	ModelBakeResult baked;
	if (!bakeModelMaps(mesh, surfaces, frame, values.contains("--high") ? &high : nullptr, options, &baked, &error))
		return fail(4, error);
	if (!dryRun && !modelOutput.isEmpty())
	{
		ModelMesh editable = mesh;
		if (!input.endsWith(".mesh.json", Qt::CaseInsensitive))
		{
			QFile source(input);
			if (!source.open(QIODevice::ReadOnly) || !importEditableModel(QFileInfo(input).absoluteFilePath(), source.readAll(), &editable, &error))
				return fail(4, error);
		}
		QImage shaded;
		ModelEdit edit;
		if (!multiplyModelBakeIntoSkin(modelSurfaceIndexedSkin(editable, shadedSurface), baked.image, options.type, strength, 224, &shaded, &error) ||
			!modelSkinPaintMdlEdit(editable, shadedSurface, shaded, &edit, &error) || !applyModelEdit(&editable, edit, nullptr, &error))
			return fail(4, error);
		const QByteArray source = serializeEditableModel(editable, &error);
		QSaveFile written(modelOutput);
		if (source.isEmpty() || !written.open(QIODevice::WriteOnly) || written.write(source) != source.size() || !written.commit())
			return fail(1, error.isEmpty() ? Text::tr("The model could not be written to %1.").arg(QDir::toNativeSeparators(modelOutput)) : error);
	}
	if (!dryRun && !output.isEmpty())
	{
		QByteArray bytes;
		if (QFileInfo(output).suffix().compare("tga", Qt::CaseInsensitive) == 0)
		{
			TextureExportOptions exportOptions;
			exportOptions.format = TextureExportFormat::Targa;
			const TextureExportResult encoded = encodeTextureExport(baked.image, exportOptions);
			if (!encoded.succeeded)
				return fail(1, encoded.error);
			bytes = encoded.bytes;
		}
		QSaveFile file(output);
		const bool written = file.open(QIODevice::WriteOnly) &&
							 (bytes.isEmpty() ? baked.image.save(&file, "PNG") : file.write(bytes) == bytes.size()) && file.commit();
		if (!written)
			return fail(1, Text::tr("The map could not be written to %1.").arg(QDir::toNativeSeparators(output)));
	}
	ModelBakeCliResult result;
	result.payload = modelBakeResultJson(baked, options);
	result.payload.insert("dryRun", dryRun);
	result.payload.insert("written", !dryRun && !output.isEmpty());
	result.payload.insert("output", output.isEmpty() ? QString() : QFileInfo(output).absoluteFilePath());
	result.payload.insert("high", values.contains("--high"));
	if (!modelOutput.isEmpty())
	{
		result.payload.insert("modelOutput", dryRun ? QString() : QFileInfo(modelOutput).absoluteFilePath());
		result.payload.insert("strength", strength);
	}
	if (dryRun)
		result.lines << Text::tr("Would bake %1 (%2 x %3, %n texel(s)).", nullptr, baked.texels)
							.arg(modelBakeTypeDisplayName(options.type))
							.arg(baked.image.width())
							.arg(baked.image.height());
	else
		result.lines << Text::tr("Baked %1 (%2 x %3, %n texel(s)) to %4.", nullptr, baked.texels)
							.arg(modelBakeTypeDisplayName(options.type))
							.arg(baked.image.width())
							.arg(baked.image.height())
							.arg(QDir::toNativeSeparators(output));
	if (!dryRun && !modelOutput.isEmpty())
		result.lines << Text::tr("Wrote the model with its MDL skin shaded by the map to %1.").arg(QDir::toNativeSeparators(modelOutput));
	result.lines += baked.notes;
	return result;
}
} // namespace vibestudio::cli
