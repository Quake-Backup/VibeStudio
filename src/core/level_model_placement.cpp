#include "core/level_model_placement.h"

#include "core/package_archive.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QLocale>

#include <cmath>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
// Numbers in map keys use the C locale and drop trailing zeros.
QString number(double value)
{
	return QString::number(value, 'g', 10);
}
} // namespace

QString chooseLevelModelEntityClass(const EntityDefinitionCatalogue *definitions)
{
	// misc_model is the class VibeMap3 (like q3map2) bakes models from, and the
	// one the Quake-family definitions most often declare.
	if (definitions)
	{
		for (const QString &candidate : {QStringLiteral("misc_model"), QStringLiteral("misc_gamemodel")})
		{
			if (definitions->classForName(candidate))
				return candidate;
		}
	}
	return QStringLiteral("misc_model");
}

QStringList levelModelPlacementSuffixes(LevelMapFormat format)
{
	switch (format)
	{
	case LevelMapFormat::Quake3Map:
		// What q3map2's picomodel loader (and so VibeMap3) reads.
		return {QStringLiteral("md3"), QStringLiteral("ase"), QStringLiteral("mdc"), QStringLiteral("md2"),
				QStringLiteral("obj"), QStringLiteral("lwo"), QStringLiteral("fm")};
	case LevelMapFormat::QuakeMap:
		// The original engines' own formats; source ports read more.
		return {QStringLiteral("mdl"), QStringLiteral("md2")};
	case LevelMapFormat::DoomWad:
	case LevelMapFormat::Unknown:
		break;
	}
	return {};
}

bool checkLevelModelPlacement(const LevelMapDocument &document, const LevelModelPlacementRequest &request, LevelModelPlacementResult *result,
							  QString *error)
{
	LevelModelPlacementResult checked;
	if (document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map)
		return fail(error, document.format == LevelMapFormat::DoomWad
							   ? QCoreApplication::translate("VibeStudioLevelModelPlacement",
															 "Doom maps have no model entities; things take their sprites from the game.")
							   : QCoreApplication::translate("VibeStudioLevelModelPlacement", "Open a Quake-family map to place a model."));
	const auto normalized = normalizePackageVirtualPath(request.virtualPath, false);
	if (!normalized.isSafe() || normalized.normalizedPath.isEmpty() || normalized.normalizedPath.toUtf8().size() >= 64)
		return fail(error, QCoreApplication::translate("VibeStudioLevelModelPlacement", "Choose a safe model package path shorter than 64 bytes."));
	const QString suffix = QFileInfo(normalized.normalizedPath).suffix().toLower();
	const auto loaded = levelModelPlacementSuffixes(document.format);
	if (document.format == LevelMapFormat::Quake3Map && !loaded.contains(suffix))
		return fail(error, QCoreApplication::translate("VibeStudioLevelModelPlacement",
													   "A Quake III map bakes MD3, ASE, MDC, MD2, OBJ, LWO and FM models; %1 is not one.")
							   .arg(normalized.normalizedPath));
	if (document.format == LevelMapFormat::QuakeMap && !loaded.contains(suffix))
		checked.warnings << QCoreApplication::translate("VibeStudioLevelModelPlacement",
														"Quake and Quake II load MDL and MD2 models; %1 loads only in source ports that read .%2 files.")
								.arg(normalized.normalizedPath, suffix);
	const auto finite = [](const LevelMapVec3 &v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
	if (!finite(request.origin) || !finite(request.angles) || !std::isfinite(request.modelscale) || request.modelscale <= 0)
		return fail(error, QCoreApplication::translate("VibeStudioLevelModelPlacement",
													   "Model placement needs finite coordinates and angles, and a scale above zero."));
	checked.entityClass = request.entityClass.trimmed().isEmpty() ? chooseLevelModelEntityClass(request.definitions) : request.entityClass.trimmed();
	if (checked.entityClass.contains(QLatin1Char('"')) || checked.entityClass.contains(QLatin1Char(' ')) || checked.entityClass.contains(QLatin1Char('\n')))
		return fail(error, QCoreApplication::translate("VibeStudioLevelModelPlacement", "An entity class is one word without quotes."));
	// VibeMap3 bakes misc_model whatever the game's definitions say.
	const bool baked = document.format == LevelMapFormat::Quake3Map && checked.entityClass == QStringLiteral("misc_model");
	if (!baked && request.definitions && !request.definitions->isEmpty() && !request.definitions->classForName(checked.entityClass))
		checked.warnings << QCoreApplication::translate("VibeStudioLevelModelPlacement",
														"No loaded definition declares %1, so make sure your game or mod has it.")
								.arg(checked.entityClass);
	if (result)
		*result = std::move(checked);
	return true;
}

bool placeLevelModelEntity(LevelMapDocument *document, const LevelModelPlacementRequest &request, LevelModelPlacementResult *result,
						   QString *error)
{
	if (!document)
		return fail(error, QCoreApplication::translate("VibeStudioLevelModelPlacement", "Open a Quake-family map to place a model."));
	LevelModelPlacementResult placed;
	if (!checkLevelModelPlacement(*document, request, &placed, error))
		return false;
	QVector<LevelMapProperty> properties{{QStringLiteral("model"), normalizePackageVirtualPath(request.virtualPath, false).normalizedPath, 0}};
	const auto &a = request.angles;
	if (a.x != 0 || a.z != 0)
		properties.append({QStringLiteral("angles"), QStringLiteral("%1 %2 %3").arg(number(a.x), number(a.y), number(a.z)), 0});
	else if (a.y != 0)
		properties.append({QStringLiteral("angle"), number(a.y), 0});
	if (request.modelscale != 1.0)
		properties.append({QStringLiteral("modelscale"), number(request.modelscale), 0});
	LevelMapVec3 origin = request.origin;
	origin.valid = true;
	if (!addLevelMapEntity(document, placed.entityClass, origin, properties, &placed.entityId, error))
		return false;
	if (result)
		*result = std::move(placed);
	return true;
}

QString levelEntityModelPath(const LevelMapEntity &entity)
{
	for (const auto &property : entity.properties)
	{
		if (property.key.compare(QStringLiteral("model"), Qt::CaseInsensitive) == 0)
		{
			const QString value = property.value.trimmed();
			// "*1" names the entity's own brushes, not a file.
			return value.startsWith(QLatin1Char('*')) || QFileInfo(value).suffix().isEmpty() ? QString() : value;
		}
	}
	return {};
}

} // namespace vibestudio
