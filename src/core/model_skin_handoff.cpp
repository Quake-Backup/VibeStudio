#include "core/model_skin_handoff.h"

#include <QBuffer>
#include <QHash>
#include <QCoreApplication>
#include <QImageWriter>

#include <limits>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelSkinHandoff)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
IdTechPaletteResolution resolution(const ModelMesh &mesh)
{
	IdTechPaletteResolution result;
	result.palette = modelMdlPreviewPalette(mesh);
	result.requestedPaletteId = QStringLiteral("quake");
	return result;
}
} // namespace

QImage modelMdlSkinMemberImage(const ModelMesh &mesh, int skin, int member, QString *error)
{
	if (!mesh.mdl.enabled || skin < 0 || skin >= mesh.embeddedSkins.size() || member < 0 ||
		member >= mesh.embeddedSkins[skin].indexedFrames.size())
	{
		fail(error, Text::tr("Choose an existing embedded MDL skin and member."));
		return {};
	}
	const auto size = mesh.mdl.skinSize;
	const auto &pixels = mesh.embeddedSkins[skin].indexedFrames[member];
	if (pixels.size() != qint64(size.width()) * size.height())
	{
		fail(error, Text::tr("The skin member does not match the MDL skin size."));
		return {};
	}
	QImage image(size, QImage::Format_Indexed8);
	const auto palette = modelMdlPreviewPalette(mesh);
	QVector<QRgb> table(palette.colors.cbegin(), palette.colors.cend());
	table.resize(256);
	image.setColorTable(table);
	for (int y = 0; y < size.height(); ++y)
		std::memcpy(image.scanLine(y), pixels.constData() + qint64(y) * size.width(), size_t(size.width()));
	return image;
}

bool exportModelMdlSkinMember(const ModelMesh &mesh, int skin, int member, const QString &format, QByteArray *bytes, QString *error,
							  const ModelWorkControl &control)
{
	const auto image = modelMdlSkinMemberImage(mesh, skin, member, error);
	if (image.isNull())
		return false;
	if (!bytes || (format != QLatin1String("pcx") && format != QLatin1String("png")))
		return fail(error, Text::tr("Export MDL skin members as pcx or png."));
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Serializing, 0, 1, error))
		return false;
	if (format == QLatin1String("png"))
	{
		// Qt writes an indexed image as a palette PNG with its indices.
		QBuffer buffer;
		buffer.open(QIODevice::WriteOnly);
		QImageWriter writer(&buffer, "png");
		if (!writer.write(image))
			return fail(error, Text::tr("The skin member could not be encoded: %1.").arg(writer.errorString()));
		*bytes = buffer.data();
		return true;
	}
	TextureExportOptions options;
	options.format = TextureExportFormat::Pcx;
	options.allowGeneratedPalette = true;
	options.fullbright = TextureFullbrightMode::Allow;
	const auto result = encodeTextureExport(image, options, resolution(mesh),
											[&](qint64 done, qint64 total) { return modelWorkCheckpoint(control, ModelWorkPhase::Serializing, done, total); });
	if (!result.succeeded)
		return fail(error, result.error);
	*bytes = result.bytes;
	return true;
}

bool modelMdlSkinInputFromImage(const ModelMesh &mesh, const QImage &image, TextureFullbrightMode fullbright, ModelMdlSkinInput *input,
								QString *error, const ModelWorkControl &control)
{
	if (!mesh.mdl.enabled || !input || image.isNull())
		return fail(error, Text::tr("Choose an image for an MDL source."));
	if (image.size() != mesh.mdl.skinSize)
		return fail(error, Text::tr("The image is %1 × %2 pixels; this MDL's skins are %3 × %4. Resize it to match.")
							   .arg(image.width())
							   .arg(image.height())
							   .arg(mesh.mdl.skinSize.width())
							   .arg(mesh.mdl.skinSize.height()));
	const auto palette = modelMdlPreviewPalette(mesh);
	ModelMdlSkinInput result;
	result.size = image.size();
	result.palette = modelMdlPaletteBytes(palette);
	result.paletteGenerated = palette.generated;
	QVector<QRgb> table(palette.colors.cbegin(), palette.colors.cend());
	table.resize(256);
	if (image.format() == QImage::Format_Indexed8 && image.colorTable() == table)
	{
		// The model's own palette: indices pass through untouched.
		for (int y = 0; y < image.height(); ++y)
			result.pixels.append(reinterpret_cast<const char *>(image.constScanLine(y)), image.width());
		*input = std::move(result);
		return true;
	}
	// Nearest palette colour (lowest index on ties). The fullbright range,
	// 224-255, takes only exact colour matches under Preserve and nothing
	// under Exclude; Allow treats it like any other entry.
	const int ordinary = fullbright == TextureFullbrightMode::Allow ? 256 : 224;
	const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
	QHash<QRgb, int> cache;
	const auto nearest = [&](QRgb colour)
	{
		const auto found = cache.constFind(colour);
		if (found != cache.cend())
			return found.value();
		int best = 0;
		qint64 distance = std::numeric_limits<qint64>::max();
		for (int i = 0; i < 256; ++i)
		{
			const QRgb entry = table[i] & 0x00ffffffu;
			if (i >= ordinary && !(fullbright == TextureFullbrightMode::Preserve && entry == colour))
				continue;
			const qint64 dr = qRed(entry) - qRed(colour), dg = qGreen(entry) - qGreen(colour), db = qBlue(entry) - qBlue(colour);
			const qint64 d = dr * dr + dg * dg + db * db;
			if (d < distance)
			{
				distance = d;
				best = i;
			}
		}
		cache.insert(colour, best);
		return best;
	};
	for (int y = 0; y < rgb.height(); ++y)
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Editing, y, rgb.height(), error))
			return false;
		const auto *row = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
		for (int x = 0; x < rgb.width(); ++x)
			result.pixels.append(char(nearest(row[x] & 0x00ffffffu)));
	}
	*input = std::move(result);
	return true;
}

QString modelMd2SkinSizeMismatch(const ModelMesh &mesh, QSize image, const QLocale &locale)
{
	if (!image.isValid() || image.isEmpty() || image == mesh.md2SkinSize)
		return {};
	return Text::tr("The skin image is %1 × %2 pixels but the MD2 skin size is %3 × %4; the Quake II software renderer reads skins at the "
					"MD2 size. Use the image size for MD2, or resize the image.")
		.arg(locale.toString(image.width()), locale.toString(image.height()), locale.toString(mesh.md2SkinSize.width()),
			 locale.toString(mesh.md2SkinSize.height()));
}
} // namespace vibestudio
