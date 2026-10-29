#include "cli/model_game_skin.h"
#include "core/material_library.h"
#include "core/material_script.h"
#include "core/model_document.h"
#include "core/model_game_preview.h"
#include "core/package_archive.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>

namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelGameSkinCli)
};
ModelGameSkinCliResult fail(int code, const QString &message)
{
	return {code, message, {}, {}};
}
QString imageHash(const QImage &image)
{
	if (image.isNull())
		return {};
	const QImage normalized = image.convertToFormat(QImage::Format_ARGB32);
	QCryptographicHash hash(QCryptographicHash::Sha256);
	for (int y = 0; y < normalized.height(); ++y)
		hash.addData(QByteArrayView(reinterpret_cast<const char *>(normalized.constScanLine(y)), qsizetype(normalized.width()) * 4));
	return QString::fromLatin1(hash.result().toHex());
}
} // namespace

ModelGameSkinCliResult runModelGameSkin(const QStringList &arguments)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--no-flood-fill", "--overwrite"};
	const QSet<QString> valued{"--package", "--surface", "--time", "--quake-renderer", "--top", "--bottom", "--output", "--glow-output",
							   "--settings-file", "--locale", "--catalog-root"};
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
	if (positional.size() != 3 || positional[0] != "model" || positional[1] != "game-skin" || !values.contains("--output"))
		return fail(2, Text::tr("Use model game-skin <model> --output <skin.png>."));
	ModelGamePreviewOptions options;
	bool ok = true;
	if (values.contains("--time"))
	{
		options.time = values.value("--time").toDouble(&ok);
		if (!ok || !std::isfinite(options.time) || options.time < 0 || options.time > 86400)
			return fail(2, Text::tr("--time takes seconds from 0 to 86400."));
	}
	const QString renderer = values.value("--quake-renderer", "modern");
	if (renderer == "glquake")
		options.quakeRenderer = MaterialQuakeRenderer::GLQuake;
	else if (renderer == "software")
		options.quakeRenderer = MaterialQuakeRenderer::Software;
	else if (renderer != "modern")
		return fail(2, Text::tr("--quake-renderer takes glquake, modern or software."));
	for (const auto &[key, target] : {std::pair<QString, int *>{"--top", &options.topColor}, std::pair<QString, int *>{"--bottom", &options.bottomColor}})
	{
		if (!values.contains(key))
			continue;
		*target = values.value(key).toInt(&ok);
		if (!ok || *target < 0 || *target > 13)
			return fail(2, Text::tr("%1 takes a player colour row from 0 to 13.").arg(key));
	}
	options.floodFill = !seen.contains("--no-flood-fill");
	int surface = 0;
	if (values.contains("--surface"))
	{
		surface = values.value("--surface").toInt(&ok);
		if (!ok || surface < 0)
			return fail(2, Text::tr("--surface takes a zero-based surface index."));
	}
	// Read the model: an editable source or any native format.
	const QString input = positional[2];
	QFile file(input);
	if (!file.open(QIODevice::ReadOnly) || file.size() > modelDocumentMaxSourceBytes)
		return fail(3, Text::tr("The model cannot be read or exceeds 64 MiB: %1").arg(QDir::toNativeSeparators(input)));
	const QByteArray bytes = file.readAll();
	ModelMesh mesh;
	if (input.endsWith(".mesh.json", Qt::CaseInsensitive))
	{
		QString error;
		if (!parseEditableModel(bytes, &mesh, &error))
			return fail(4, error);
	}
	else
	{
		mesh = decodeModelMesh(QFileInfo(input).absoluteFilePath(), bytes, nullptr, {}, modelCompanionsFromFileSystem(QFileInfo(input).absoluteFilePath()));
		if (!mesh.error.isEmpty())
			return fail(4, mesh.error);
	}
	if (surface >= mesh.surfaces.size())
		return fail(2, Text::tr("The model has %1 surface(s); --surface %2 does not exist.").arg(mesh.surfaces.size()).arg(surface));
	// Quake III shaders come from --package, read only.
	MaterialDefinition definition;
	MaterialImageSet images;
	bool haveShader = false;
	if (values.contains("--package"))
	{
		auto archive = std::make_shared<PackageArchive>();
		QString error;
		if (!archive->load(values.value("--package"), &error))
			return fail(3, error);
		QString name = mesh.surfaces[surface].skinPaths.value(0);
		const int dot = int(name.lastIndexOf('.'));
		if (dot > int(name.lastIndexOf('/')))
			name.truncate(dot);
		if (!name.isEmpty())
		{
			MaterialLibraryOptions libraryOptions;
			libraryOptions.engines = {MaterialEngine::Quake3};
			const MaterialLibrary library = scanMaterialLibrary(*archive, libraryOptions);
			const int entry = library.indexOf(name, MaterialEngine::Quake3);
			definition = entry >= 0 ? library.definition(entry) : implicitQuake3Material(name, MaterialSurfaceContext::Model);
			MaterialImageSource source;
			source.archive = archive;
			images = resolveMaterialImages(definition, source);
			haveShader = true;
		}
	}
	// Outputs are new files unless --overwrite; never the model or package.
	const QStringList outputs{values.value("--output"), values.value("--glow-output")};
	for (const QString &output : outputs)
	{
		if (output.isEmpty())
			continue;
		const QFileInfo target(output);
		if (target.absoluteFilePath().compare(QFileInfo(input).absoluteFilePath(), Qt::CaseInsensitive) == 0 ||
			(values.contains("--package") && target.absoluteFilePath().compare(QFileInfo(values.value("--package")).absoluteFilePath(), Qt::CaseInsensitive) == 0))
			return fail(2, Text::tr("Write the preview to another file; the model and the package are never overwritten."));
		if (target.exists() && !seen.contains("--overwrite"))
			return fail(1, Text::tr("%1 exists; add --overwrite to replace it.").arg(QDir::toNativeSeparators(output)));
	}
	ModelGameSkin skin;
	QString error;
	QImage fallback;
	if (const auto *embedded = modelEmbeddedSkinForSurface(mesh, surface))
		fallback = embedded->image;
	if (!composeModelGameSkin(mesh, surface, haveShader ? &definition : nullptr, haveShader ? &images : nullptr, fallback, options, &skin, &error))
		return fail(4, error.isEmpty() ? Text::tr("Surface %1 has nothing the Game preview can draw; give --package for its shader.").arg(surface) : error);
	if (!skin.base.convertToFormat(QImage::Format_ARGB32).save(values.value("--output"), "PNG"))
		return fail(1, Text::tr("The preview could not be written to %1.").arg(QDir::toNativeSeparators(values.value("--output"))));
	if (!values.value("--glow-output").isEmpty())
	{
		QImage glow = skin.glow;
		if (glow.isNull())
		{
			glow = QImage(skin.base.size(), QImage::Format_ARGB32);
			glow.fill(qRgba(0, 0, 0, 255));
		}
		if (!glow.convertToFormat(QImage::Format_ARGB32).save(values.value("--glow-output"), "PNG"))
			return fail(1, Text::tr("The glow layer could not be written to %1.").arg(QDir::toNativeSeparators(values.value("--glow-output"))));
	}
	ModelGameSkinCliResult result;
	result.payload = QJsonObject{{"surface", surface},
		{"time", options.time},
		{"quakeRenderer", renderer},
		{"width", skin.base.width()},
		{"height", skin.base.height()},
		{"frame", skin.frame},
		{"stagesDrawn", skin.stagesDrawn},
		{"nearest", skin.nearest},
		{"glow", !skin.glow.isNull()},
		{"baseSha256", imageHash(skin.base)},
		{"glowSha256", imageHash(skin.glow)},
		{"shader", haveShader ? definition.name : QString()},
		{"output", QFileInfo(values.value("--output")).absoluteFilePath()},
		{"notes", QJsonArray::fromStringList(skin.notes)}};
	result.lines << Text::tr("Wrote the Game preview of surface %1 (%2 x %3) to %4.")
						.arg(surface)
						.arg(skin.base.width())
						.arg(skin.base.height())
						.arg(QDir::toNativeSeparators(values.value("--output")));
	result.lines += skin.notes;
	return result;
}
} // namespace vibestudio::cli
