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
	const QSet<QString> valued{"--strokes", "--surface", "--frame", "--texture", "--size", "--output", "--settings-file", "--locale", "--catalog-root"};
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
	if (!document.isObject() || !parseModelSkinPaintStrokes(document.object(), &camera, &strokes, &error))
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
	for (const QString &source : {input, values.value("--strokes"), values.value("--texture")})
		if (!source.isEmpty() && QFileInfo(source).absoluteFilePath().compare(target.absoluteFilePath(), Qt::CaseInsensitive) == 0)
			return fail(2, Text::tr("Write the painted skin to another file; the inputs are never overwritten."));
	if (target.exists() && !seen.contains("--overwrite"))
		return fail(1, Text::tr("%1 exists; add --overwrite to replace it.").arg(QDir::toNativeSeparators(output)));
	ModelSkinPaintView view;
	if (!buildModelSkinPaintView(mesh, frame, camera, &view, &error))
		return fail(2, error);
	int texels = 0;
	QRect dirty;
	for (const ModelSkinPaintStroke &stroke : std::as_const(strokes))
	{
		ModelSkinPaintResult painted;
		if (!paintModelSkinStroke(mesh, surface, frame, view, stroke, &skin, &painted, &error))
			return fail(4, error);
		texels += painted.texels;
		dirty = dirty.united(painted.dirty);
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
	result.lines << Text::tr("Painted %1 texel(s) with %2 stroke(s) into %3.").arg(texels).arg(strokes.size()).arg(QDir::toNativeSeparators(output));
	return result;
}
} // namespace vibestudio::cli
