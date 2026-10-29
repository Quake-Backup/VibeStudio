#include "core/model_skin_roundtrip.h"

#include "core/model_skin_handoff.h"
#include "core/texture_document.h"
#include "core/texture_export.h"
#include "core/texture_project.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QRegularExpression>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelSkinRoundTrip)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
TextureProgress progress(const ModelWorkControl &control, ModelWorkPhase phase)
{
	return [control, phase](qint64 done, qint64 total) { return modelWorkCheckpoint(control, phase, done, total); };
}
QString safeName(QString text)
{
	text.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]+")), QStringLiteral("_"));
	return text.isEmpty() ? QStringLiteral("model") : text.left(64);
}
} // namespace

QJsonObject modelSkinRoundTripLinkJson(const ModelSkinRoundTripLink &link)
{
	return {{QStringLiteral("model"), link.model},
			{QStringLiteral("surface"), link.surface},
			{QStringLiteral("mdlSkin"), link.mdlSkin},
			{QStringLiteral("mdlMember"), link.mdlMember},
			{QStringLiteral("imagePath"), link.imagePath},
			{QStringLiteral("width"), link.size.width()},
			{QStringLiteral("height"), link.size.height()},
			{QStringLiteral("indexed"), link.indexed}};
}

bool modelSkinRoundTripLinkFromJson(const QJsonObject &object, ModelSkinRoundTripLink *link)
{
	ModelSkinRoundTripLink parsed;
	const auto integer = [&](const char *key, int *value)
	{
		const auto field = object.value(QLatin1String(key));
		*value = field.toInt(-2);
		return field.isDouble() && field == QJsonValue(*value);
	};
	int width = 0, height = 0;
	if (!object.value(QStringLiteral("model")).isString() || !object.value(QStringLiteral("imagePath")).isString() ||
		!object.value(QStringLiteral("indexed")).isBool() || !integer("surface", &parsed.surface) || !integer("mdlSkin", &parsed.mdlSkin) ||
		!integer("mdlMember", &parsed.mdlMember) || !integer("width", &width) || !integer("height", &height) || parsed.surface < 0 ||
		parsed.mdlSkin < -1 || parsed.mdlMember < -1 || width < 1 || height < 1)
		return false;
	parsed.model = object.value(QStringLiteral("model")).toString();
	parsed.imagePath = object.value(QStringLiteral("imagePath")).toString();
	parsed.indexed = object.value(QStringLiteral("indexed")).toBool();
	parsed.size = {width, height};
	if (link)
		*link = std::move(parsed);
	return true;
}

QString modelSkinRoundTripFileName(const ModelSkinRoundTripLink &link)
{
	const auto model = safeName(QFileInfo(link.model).completeBaseName());
	if (link.isMdlMember())
		return QStringLiteral("%1-skin%2-%3.vtexture").arg(model).arg(link.mdlSkin).arg(link.mdlMember);
	return QStringLiteral("%1-%2-%3.vtexture").arg(model).arg(link.surface).arg(safeName(QFileInfo(link.imagePath).completeBaseName()));
}

bool writeModelSkinRoundTrip(const QString &path, const QImage &skin, const QImage &guide, const ModelSkinRoundTripLink &link,
							 QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	ModelWorkProgress work(control, ModelWorkPhase::Serializing, error);
	if (!work.check())
		return false;
	if (skin.isNull())
		return fail(error, Text::tr("There is no skin image to edit."));
	const bool indexed = skin.format() == QImage::Format_Indexed8;
	const QImage pixels = indexed ? skin : skin.convertToFormat(QImage::Format_ARGB32);
	QImage overlay = guide.isNull() ? QImage() : guide.convertToFormat(QImage::Format_ARGB32);
	if (overlay.size() != pixels.size())
	{
		overlay = QImage(pixels.size(), QImage::Format_ARGB32);
		if (!overlay.isNull())
			overlay.fill(Qt::transparent);
	}
	// The skin is the active, editable layer; the guide sits above it at half
	// opacity and locked, and Textures can hide it from the Layers panel.
	TextureLayer base{1, Text::tr("Skin"), pixels, true, false, 100, TextureBlendMode::Normal};
	TextureLayer layout{2, Text::tr("UV guide"), overlay, true, true, 50, TextureBlendMode::Normal};
	TextureDocument document;
	if (!document.restoreLayers(pixels.size(), {base, layout}, 0, error) || !work.check())
		return false;
	auto object = modelSkinRoundTripLinkJson(link);
	object.insert(QStringLiteral("guideLayer"), layout.id);
	QJsonObject metadata{{QStringLiteral("modelSkin"), object}};
	TextureExportOptions options;
	const auto suffix = QFileInfo(link.imagePath).suffix().toLower();
	if (indexed)
	{
		// The skin's own palette, so Textures paints and exports with it.
		IdTechPaletteResolution palette;
		palette.requestedPaletteId = QStringLiteral("skin");
		palette.palette.id = QStringLiteral("skin");
		palette.palette.colors = skin.colorTable();
		palette.palette.colors.resize(256);
		metadata.insert(QStringLiteral("palette"), texturePaletteMetadata(palette));
		options.format = TextureExportFormat::Pcx;
		options.alpha = TextureExportAlpha::Matte;
	}
	else
		options.format = suffix == QStringLiteral("tga") ? TextureExportFormat::Targa
						 : suffix == QStringLiteral("pcx") ? TextureExportFormat::Pcx
														   : TextureExportFormat::Png;
	metadata.insert(QStringLiteral("export"), textureExportOptionsJson(options));
	if (!link.imagePath.isEmpty())
		metadata.insert(QStringLiteral("packageTexturePath"), link.imagePath);
	TextureProjectSaveRequest request;
	request.path = path;
	request.overwrite = true;
	request.keepBackup = true;
	const auto prepared = prepareTextureProjectSave(document, request, metadata, progress(control, ModelWorkPhase::Serializing));
	if (!prepared.report.succeeded)
		return fail(error, prepared.report.error.isEmpty() ? Text::tr("The skin project could not be prepared.") : prepared.report.error);
	if (!work.check())
		return false;
	const auto report = publishTextureProjectSave(prepared);
	if (!report.succeeded)
		return fail(error, report.error.isEmpty() ? Text::tr("The skin project could not be written.") : report.error);
	return true;
}

bool readModelSkinRoundTrip(const QString &path, QImage *skin, ModelSkinRoundTripLink *link, QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	ModelWorkProgress work(control, ModelWorkPhase::Reading, error);
	if (!work.check())
		return false;
	TextureDocument document;
	QJsonObject metadata;
	QString problem;
	if (!readTextureProject(path, &document, nullptr, &metadata, &problem, progress(control, ModelWorkPhase::Reading)))
		return fail(error, problem.isEmpty() ? Text::tr("%1 could not be read as a texture project.").arg(path) : problem);
	const auto object = metadata.value(QStringLiteral("modelSkin")).toObject();
	ModelSkinRoundTripLink parsed;
	if (!modelSkinRoundTripLinkFromJson(object, &parsed))
		return fail(error, Text::tr("%1 was not written by Edit Skin.").arg(path));
	const int guide = object.value(QStringLiteral("guideLayer")).toInt(-1);
	auto layers = document.layers();
	QVector<int> shown;
	for (int i = 0; i < layers.size(); ++i)
		if (layers[i].id != guide && layers[i].visible && layers[i].opacity > 0)
			shown.append(i);
	if (shown.isEmpty())
		return fail(error, Text::tr("Every layer except the UV guide is hidden in %1. Show the skin layer and save again.").arg(path));
	QImage result;
	const auto &only = layers[shown.first()];
	if (shown.size() == 1 && only.pixels.format() == QImage::Format_Indexed8 && only.opacity == 100 && only.blend == TextureBlendMode::Normal)
		result = only.pixels.copy();
	else
	{
		for (auto &layer : layers)
			if (layer.id == guide)
				layer.visible = false;
		if (!document.restoreLayers(document.size(), layers, 0, &problem))
			return fail(error, problem);
		result = document.image().convertToFormat(QImage::Format_ARGB32);
	}
	if (result.isNull())
		return fail(error, Text::tr("The skin could not be composed from its layers."));
	// A painted external indexed skin (an MD2 PCX, say) goes back to its own
	// palette, nearest colour per texel; MDL members keep their indices in
	// modelSkinRoundTripMdlInput instead.
	IdTechPaletteResolution palette;
	if (parsed.indexed && !parsed.isMdlMember() && result.format() != QImage::Format_Indexed8 && metadata.contains(QStringLiteral("palette")) &&
		texturePaletteFromMetadata(metadata.value(QStringLiteral("palette")).toObject(), &palette) && !palette.palette.colors.isEmpty())
	{
		const QVector<QRgb> table(palette.palette.colors.cbegin(), palette.palette.colors.cend());
		result = result.convertToFormat(QImage::Format_Indexed8, table, Qt::ThresholdDither | Qt::AvoidDither);
	}
	if (!work.check())
		return false;
	if (skin)
		*skin = std::move(result);
	if (link)
		*link = std::move(parsed);
	return true;
}

bool modelSkinRoundTripMdlInput(const ModelMesh &mesh, int skin, int member, const QImage &image, ModelMdlSkinInput *input, QString *error,
								const ModelWorkControl &control)
{
	if (error)
		error->clear();
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	const auto original = modelMdlSkinMemberImage(mesh, skin, member, error);
	if (original.isNull())
		return false;
	if (image.size() != original.size())
		return fail(error, Text::tr("The edited skin is %1 × %2 texels, but skin %3 is %4 × %5. Keep the canvas size in Textures.")
							   .arg(image.width())
							   .arg(image.height())
							   .arg(skin)
							   .arg(original.width())
							   .arg(original.height()));
	const auto table = original.colorTable();
	if (image.format() == QImage::Format_Indexed8 && image.colorTable() == table)
		return modelMdlSkinInputFromImage(mesh, image, TextureFullbrightMode::Preserve, input, error, control);
	const QImage colours = image.convertToFormat(QImage::Format_ARGB32);
	ModelMdlSkinInput mapped;
	if (!modelMdlSkinInputFromImage(mesh, colours, TextureFullbrightMode::Preserve, &mapped, error, control))
		return false;
	// Unchanged texels keep their index, even where the palette repeats a
	// colour under another index.
	const int width = original.width();
	for (int y = 0; y < original.height(); ++y)
	{
		if (!work.step())
			return false;
		const uchar *indices = original.constScanLine(y);
		const auto *row = reinterpret_cast<const QRgb *>(colours.constScanLine(y));
		for (int x = 0; x < width; ++x)
			if (indices[x] < table.size() && qRgb(qRed(row[x]), qGreen(row[x]), qBlue(row[x])) == qRgb(qRed(table[indices[x]]), qGreen(table[indices[x]]), qBlue(table[indices[x]])))
				mapped.pixels[y * width + x] = char(indices[x]);
	}
	if (input)
		*input = std::move(mapped);
	return work.check();
}
} // namespace vibestudio
