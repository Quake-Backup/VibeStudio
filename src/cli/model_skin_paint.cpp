#include "cli/model_skin_paint.h"
#include "core/model_document.h"
#include "core/model_skin_paint.h"
#include "core/texture_export.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelSkinPaintCli)
};
ModelSkinPaintCliResult fail(int code, const QString &message)
{
	return {code, message, {}, {}};
}
QString imageHash(const QImage &image)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	const QImage normalized = image.convertToFormat(QImage::Format_ARGB32);
	for (int y = 0; y < normalized.height(); ++y)
		hash.addData(QByteArrayView(reinterpret_cast<const char *>(normalized.constScanLine(y)), qsizetype(normalized.width()) * 4));
	return QString::fromLatin1(hash.result().toHex());
}
} // namespace

ModelSkinPaintCliResult runModelSkinPaint(const QStringList &arguments)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--overwrite"};
	const QSet<QString> valued{"--strokes", "--surface", "--frame", "--texture", "--size", "--output", "--model-output", "--settings-file", "--locale",
		"--catalog-root"};
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
	if (positional.size() != 3 || positional[0] != "model" || positional[1] != "paint-skin" || !values.contains("--strokes") || !values.contains("--output"))
		return fail(2, Text::tr("Use model paint-skin <model> --strokes <strokes.json> --output <skin.png|skin.tga>."));
	bool ok = true;
	const int surface = values.value("--surface", "0").toInt(&ok);
	if (!ok || surface < 0)
		return fail(2, Text::tr("--surface takes a zero-based surface index."));
	const int frame = values.value("--frame", "0").toInt(&ok);
	if (!ok || frame < 0)
		return fail(2, Text::tr("--frame takes a zero-based frame index."));
	// The model: an editable source or any native format.
	const QString input = positional[2];
	QFile file(input);
	if (!file.open(QIODevice::ReadOnly) || file.size() > modelDocumentMaxSourceBytes)
		return fail(3, Text::tr("The model cannot be read or exceeds 64 MiB: %1").arg(QDir::toNativeSeparators(input)));
	const QByteArray bytes = file.readAll();
	ModelMesh mesh;
	QString error;
	if (input.endsWith(".mesh.json", Qt::CaseInsensitive))
	{
		if (!parseEditableModel(bytes, &mesh, &error))
			return fail(4, error);
	}
	else
	{
		mesh = decodeModelMesh(QFileInfo(input).absoluteFilePath(), bytes, nullptr, {}, modelCompanionsFromFileSystem(QFileInfo(input).absoluteFilePath()));
		if (!mesh.error.isEmpty())
			return fail(4, mesh.error);
	}
	if (surface >= mesh.surfaces.size() || frame >= mesh.frames.size())
		return fail(2, Text::tr("The model has no surface %1 or frame %2.").arg(surface).arg(frame));
	QFile strokesFile(values.value("--strokes"));
	if (!strokesFile.open(QIODevice::ReadOnly) || strokesFile.size() > 64 * 1024 * 1024)
		return fail(3, Text::tr("The strokes file cannot be read: %1").arg(QDir::toNativeSeparators(values.value("--strokes"))));
	const QJsonDocument document = QJsonDocument::fromJson(strokesFile.readAll());
	ModelSkinPaintCamera camera;
	QVector<ModelSkinPaintStroke> strokes;
	if (!document.isObject() ||
		!parseModelSkinPaintStrokes(document.object(), &camera, &strokes, &error, QFileInfo(values.value("--strokes")).absolutePath()))
		return fail(2, error.isEmpty() ? Text::tr("The strokes file is not a JSON object.") : error);
	// The skin: --texture, else the model's own indexed skin, else blank.
	QImage skin;
	if (values.contains("--texture"))
	{
		if (!skin.load(values.value("--texture")))
			return fail(3, Text::tr("The texture cannot be read: %1").arg(QDir::toNativeSeparators(values.value("--texture"))));
	}
	else if (!(skin = modelSurfaceIndexedSkin(mesh, surface)).isNull())
	{
		// The model's own indexed skin: painting writes palette indices.
	}
	else
	{
		QSize size(256, 256);
		if (values.contains("--size"))
		{
			const auto parts = values.value("--size").toLower().split('x');
			bool w = false, h = false;
			size = parts.size() == 2 ? QSize(parts[0].toInt(&w), parts[1].toInt(&h)) : QSize();
			if (!w || !h || size.width() < 1 || size.height() < 1 || size.width() > 4096 || size.height() > 4096)
				return fail(2, Text::tr("--size takes WIDTHxHEIGHT up to 4096x4096."));
		}
		skin = QImage(size, QImage::Format_ARGB32);
		skin.fill(Qt::white);
	}
	const QString output = values.value("--output");
	const QFileInfo target(output);
	const QString modelOutput = values.value("--model-output");
	for (const QString &written : {output, modelOutput})
	{
		if (written.isEmpty())
			continue;
		for (const QString &source : {input, values.value("--strokes"), values.value("--texture")})
			if (!source.isEmpty() && QFileInfo(source).absoluteFilePath().compare(QFileInfo(written).absoluteFilePath(), Qt::CaseInsensitive) == 0)
				return fail(2, Text::tr("Write the painted skin and model to other files; the inputs are never overwritten."));
		if (QFileInfo::exists(written) && !seen.contains("--overwrite"))
			return fail(1, Text::tr("%1 exists; add --overwrite to replace it.").arg(QDir::toNativeSeparators(written)));
	}
	if (!modelOutput.isEmpty() && !modelOutput.endsWith(".mesh.json", Qt::CaseInsensitive))
		return fail(2, Text::tr("--model-output writes an editable source (.mesh.json)."));
	if (!modelOutput.isEmpty() && modelSkinPaintMdlSlot(mesh, surface) < 0)
		return fail(2, Text::tr("--model-output puts the skin back into a model with an MDL skin; this surface has none."));
	// The orbit view of the strokes file, and the view each stroke painted
	// through the Mesh Editor's viewport carries.
	ModelSkinPaintView orbit;
	if (!buildModelSkinPaintView(mesh, frame, camera, &orbit, &error))
		return fail(2, error);
	QVector<QPair<ModelSkinPaintProjection, ModelSkinPaintView>> views;
	int texels = 0;
	QRect dirty;
	for (const ModelSkinPaintStroke &stroke : std::as_const(strokes))
	{
		const ModelSkinPaintView *view = &orbit;
		if (stroke.hasProjection)
		{
			view = nullptr;
			for (const auto &known : std::as_const(views))
				if (known.first == stroke.projection)
					view = &known.second;
			if (!view)
			{
				ModelSkinPaintView built;
				if (!buildModelSkinPaintView(mesh, frame, stroke.projection, &built, &error))
					return fail(2, error);
				views.append({stroke.projection, built});
				view = &views.last().second;
			}
		}
		ModelSkinPaintResult painted;
		if (!paintModelSkinStroke(mesh, surface, frame, *view, stroke, &skin, &painted, &error))
			return fail(4, error);
		texels += painted.texels;
		dirty = dirty.united(painted.dirty);
	}
	// Back into the model: the MDL skin member replaced as one edit.
	if (!modelOutput.isEmpty())
	{
		ModelMesh editable = mesh;
		if (!input.endsWith(".mesh.json", Qt::CaseInsensitive) &&
			!importEditableModel(QFileInfo(input).absoluteFilePath(), bytes, &editable, &error))
			return fail(4, error);
		ModelEdit edit;
		if (!modelSkinPaintMdlEdit(editable, surface, skin, &edit, &error) || !applyModelEdit(&editable, edit, nullptr, &error))
			return fail(4, error);
		const QByteArray source = serializeEditableModel(editable, &error);
		QSaveFile savedModel(modelOutput);
		if (source.isEmpty() || !savedModel.open(QIODevice::WriteOnly) || savedModel.write(source) != source.size() || !savedModel.commit())
			return fail(1, error.isEmpty() ? Text::tr("The model could not be written to %1.").arg(QDir::toNativeSeparators(modelOutput)) : error);
	}
	QByteArray encoded;
	if (target.suffix().compare("tga", Qt::CaseInsensitive) == 0)
	{
		TextureExportOptions options;
		options.format = TextureExportFormat::Targa;
		const TextureExportResult result = encodeTextureExport(skin.convertToFormat(QImage::Format_ARGB32), options);
		if (!result.succeeded)
			return fail(1, result.error);
		encoded = result.bytes;
	}
	QSaveFile saved(output);
	if (!saved.open(QIODevice::WriteOnly) || !(encoded.isEmpty() ? skin.save(&saved, "PNG") : saved.write(encoded) == encoded.size()) || !saved.commit())
		return fail(1, Text::tr("The painted skin could not be written to %1.").arg(QDir::toNativeSeparators(output)));
	ModelSkinPaintCliResult result;
	result.payload = QJsonObject{{"surface", surface},
		{"frame", frame},
		{"strokes", int(strokes.size())},
		{"texels", texels},
		{"indexed", skin.format() == QImage::Format_Indexed8},
		{"width", skin.width()},
		{"height", skin.height()},
		{"dirty", QJsonArray{dirty.x(), dirty.y(), dirty.width(), dirty.height()}},
		{"sha256", imageHash(skin)},
		{"output", target.absoluteFilePath()}};
	if (!modelOutput.isEmpty())
	{
		result.payload.insert("modelOutput", QFileInfo(modelOutput).absoluteFilePath());
		result.payload.insert("skinSlot", modelSkinPaintMdlSlot(mesh, surface));
	}
	result.lines << Text::tr("Painted %n texel(s) into %1 (strokes: %2).", nullptr, texels).arg(QDir::toNativeSeparators(output)).arg(strokes.size());
	if (!modelOutput.isEmpty())
		result.lines << Text::tr("Wrote the model with the painted skin to %1.").arg(QDir::toNativeSeparators(modelOutput));
	return result;
}
} // namespace vibestudio::cli
