#include "core/model_stage.h"

#include "core/model_lod.h"
#include "core/model_skeletal_export.h"
#include "core/package_archive.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>

#include <algorithm>

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
bool quakeFamily(const QString &suffix)
{
	return suffix == QStringLiteral("mdl") || suffix == QStringLiteral("md2") || suffix == QStringLiteral("md3");
}
} // namespace

qint64 ModelStageSet::totalBytes() const
{
	qint64 total = 0;
	for (const auto &entry : entries)
		total += entry.bytes.size();
	return total;
}

QStringList modelStageSuffixes()
{
	return {QStringLiteral("md3"), QStringLiteral("mdl"), QStringLiteral("md2"), QStringLiteral("md5mesh"), QStringLiteral("md5anim"),
			QStringLiteral("iqm"), QStringLiteral("ase")};
}

bool checkModelStageEntry(const QString &virtualPath, const QByteArray &bytes, QString *error)
{
	const auto normalized = normalizePackageVirtualPath(virtualPath, false);
	const QString suffix = QFileInfo(normalized.normalizedPath).suffix().toLower();
	if (!normalized.isSafe() || normalized.normalizedPath.isEmpty() || !modelStageSuffixes().contains(suffix))
		return fail(error, QCoreApplication::translate("VibeStudioModelStage", "Choose a safe MD3, MDL, MD2, MD5, IQM or ASE package path: %1")
							   .arg(virtualPath));
	// The Quake engines keep model paths in 64-byte fields (MAX_QPATH).
	if (quakeFamily(suffix) && normalized.normalizedPath.toUtf8().size() >= 64)
		return fail(error, QCoreApplication::translate("VibeStudioModelStage", "%1 is 64 bytes or longer; the game cannot load it.")
							   .arg(normalized.normalizedPath));
	if (bytes.size() > modelDocumentMaxSourceBytes)
		return fail(error, QCoreApplication::translate("VibeStudioModelStage", "%1 is larger than 64 MiB.").arg(normalized.normalizedPath));
	// Each format's own header (IDPO, IDP2, IDP3, MD5Version 10, INTERQUAKEMODEL, *3DSMAX_ASCIIEXPORT).
	const QByteArray head = bytes.left(64).trimmed();
	const bool matches = suffix == QStringLiteral("mdl")	   ? bytes.startsWith("IDPO") && bytes.size() >= 68
						 : suffix == QStringLiteral("md2")	   ? bytes.startsWith("IDP2") && bytes.size() >= 68
						 : suffix == QStringLiteral("md3")	   ? bytes.startsWith("IDP3") && bytes.size() >= 108
						 : suffix == QStringLiteral("iqm")	   ? bytes.startsWith(QByteArrayLiteral("INTERQUAKEMODEL\0"))
						 : suffix == QStringLiteral("ase")	   ? head.startsWith("*3DSMAX_ASCIIEXPORT")
															   : head.startsWith("MD5Version 10");
	if (!matches)
		return fail(error, QCoreApplication::translate("VibeStudioModelStage", "%1 does not hold a %2 model.")
							   .arg(normalized.normalizedPath, suffix.toUpper()));
	return true;
}

bool prepareModelStageSet(const ModelMesh &mesh, const ModelStageRequest &request, const PackageArchiveReader *view, ModelStageSet *set,
						  QString *error, const ModelWorkControl &control)
{
	if (!set)
		return fail(error, QCoreApplication::translate("VibeStudioModelStage", "Staging a model needs somewhere to put the set."));
	ModelStageSet result;
	const auto normalized = normalizePackageVirtualPath(request.virtualPath, false);
	const QString base = normalized.normalizedPath;
	const QString suffix = QFileInfo(base).suffix().toLower();
	if (!normalized.isSafe() || !modelStageSuffixes().contains(suffix))
		return fail(error, QCoreApplication::translate("VibeStudioModelStage", "Choose a safe MD3, MDL, MD2, MD5, IQM or ASE package path: %1")
							   .arg(request.virtualPath));
	const auto add = [&](const QString &path, const QByteArray &bytes, const QString &role)
	{
		if (!checkModelStageEntry(path, bytes, error))
			return false;
		result.entries.append({normalizePackageVirtualPath(path, false).normalizedPath, bytes, role});
		return true;
	};
	if (suffix == QStringLiteral("md3") && request.lodLevels != 0)
	{
		// The model and its detail levels, from the stored recipe or generated.
		const auto recipe = request.lodLevels < 0 ? mesh.detailLevels : modelLodGeneratedRecipe(request.lodLevels, request.lodRatio);
		if (!recipe.isEmpty())
		{
			ModelLodSet levels;
			if (!prepareModelLodFiles(mesh, recipe, request.baseDirectory, base, request.options.target, request.options.strict, &levels, error,
									  control))
				return false;
			for (const auto &file : std::as_const(levels.files))
			{
				if (!add(file.path, file.bytes, file.level == 0 ? QStringLiteral("model") : QStringLiteral("detail-level")))
					return false;
				result.notes += file.notes;
			}
		}
	}
	if (result.entries.isEmpty())
	{
		ModelExportReport report;
		const auto bytes = exportEditableModel(mesh, suffix, request.options, error, control, &report);
		if (bytes.isEmpty() || !add(base, bytes, suffix == QStringLiteral("md5anim") ? QStringLiteral("animation") : QStringLiteral("model")))
			return false;
		result.notes += report.notes;
	}
	if (suffix == QStringLiteral("md5mesh") && !mesh.skeleton.clips.isEmpty())
	{
		// Each clip's md5anim beside the mesh, as Doom 3 and Quake 4 .def files name them.
		QVector<int> clips = request.clips;
		if (request.allClips && clips.isEmpty())
		{
			for (int clip = 0; clip < mesh.skeleton.clips.size(); ++clip)
				clips << clip;
		}
		const QString folder = QFileInfo(base).path();
		for (const int clip : std::as_const(clips))
		{
			if (clip < 0 || clip >= mesh.skeleton.clips.size())
				return fail(error, QCoreApplication::translate("VibeStudioModelStage", "There is no clip %1 to stage.").arg(clip));
			auto options = request.options;
			options.clip = clip;
			ModelExportReport report;
			const auto bytes = exportEditableModel(mesh, QStringLiteral("md5anim"), options, error, control, &report);
			const QString name = modelMd5ClipFileName(mesh.skeleton, clip);
			if (bytes.isEmpty() || !add(folder == QStringLiteral(".") ? name : folder + QLatin1Char('/') + name, bytes, QStringLiteral("animation")))
				return false;
			result.notes += report.notes;
		}
	}
	// Two entries at one path would stage over each other.
	QStringList paths;
	for (const auto &entry : std::as_const(result.entries))
		paths << entry.virtualPath.toLower();
	paths.removeDuplicates();
	if (paths.size() != result.entries.size())
		return fail(error, QCoreApplication::translate("VibeStudioModelStage", "Two files of the set share a package path."));
	if (view && view->isOpen())
	{
		result.dependencies = inspectModelMaterialDependencies(mesh, *view);
		if (result.dependencies.cancelled || !modelWorkCheckpoint(control, ModelWorkPhase::Validating, 1, 1, error))
			return fail(error, error && !error->isEmpty() ? *error : QCoreApplication::translate("VibeStudioModelStage", "Staging was cancelled."));
		if (request.requireMaterials && result.dependencies.missingCount > 0)
			return fail(error, QCoreApplication::translate("VibeStudioModelStage", "%n material(s) the model uses are missing from the package.", nullptr,
														   result.dependencies.missingCount));
		if (result.dependencies.missingCount > 0)
			result.notes << QCoreApplication::translate("VibeStudioModelStage",
														 "%n material(s) the model uses are missing from the package; the game draws them as missing textures.",
														 nullptr, result.dependencies.missingCount);
	}
	result.notes.removeDuplicates();
	*set = std::move(result);
	return true;
}

bool stageModelSet(const ModelStageSet &set, PackageStagingModel *staging, bool replaceExisting, QString *error)
{
	if (set.entries.isEmpty())
		return fail(error, QCoreApplication::translate("VibeStudioModelStage", "Prepare the model files before staging them."));
	if (!staging || !staging->isLoaded() || staging->sourceFormat() == PackageArchiveFormat::Wad)
		return fail(error, QCoreApplication::translate("VibeStudioModelStage", "Open a folder, PAK, ZIP, or PK3 before staging a model."));
	auto staged = *staging;
	if (!staged.beginOperationGroup(QCoreApplication::translate("VibeStudioModelStage", "Stage model set"), error))
		return false;
	for (const auto &entry : set.entries)
	{
		if (!checkModelStageEntry(entry.virtualPath, entry.bytes, error) ||
			!staged.addBytes(entry.bytes, entry.virtualPath, error,
							 replaceExisting ? PackageStageConflictResolution::ReplaceExisting : PackageStageConflictResolution::Block))
		{
			staged.endOperationGroup(false);
			return false;
		}
	}
	if (!staged.endOperationGroup(true, error))
		return false;
	if (!staged.summary().canSave)
		return fail(error, QCoreApplication::translate("VibeStudioModelStage", "Resolve package staging conflicts before staging the model."));
	*staging = std::move(staged);
	return true;
}

QJsonObject modelStageSetJson(const ModelStageSet &set)
{
	QJsonArray entries;
	for (const auto &entry : set.entries)
	{
		entries.append(QJsonObject{{QStringLiteral("path"), entry.virtualPath},
								   {QStringLiteral("role"), entry.role},
								   {QStringLiteral("bytes"), qint64(entry.bytes.size())},
								   {QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(entry.bytes, QCryptographicHash::Sha256).toHex())}});
	}
	return {{QStringLiteral("entries"), entries},
			{QStringLiteral("totalBytes"), set.totalBytes()},
			{QStringLiteral("notes"), QJsonArray::fromStringList(set.notes)},
			{QStringLiteral("dependencies"), levelDependencyReportJson(set.dependencies)}};
}

} // namespace vibestudio
