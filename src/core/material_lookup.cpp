#include "core/material_lookup.h"

#include "core/material_script.h"

#include <QMutexLocker>

namespace vibestudio {

QString materialDisplayImage(const MaterialDefinition& definition)
{
	const QString image = materialDeclaredDisplayImage(definition);
	return image.isEmpty() ? definition.name : image;
}

QString materialDeclaredDisplayImage(const MaterialDefinition& definition)
{
	if (!definition.editorImage.isEmpty()) {
		return definition.editorImage;
	}
	const auto stageImage = [&definition](const MaterialStage& stage) {
		if (stage.imageKind == MaterialImageKind::File && !stage.imagePath.isEmpty()) {
			return stage.imagePath;
		}
		if (stage.imageKind == MaterialImageKind::Animation && !stage.animationFrames.isEmpty()) {
			return stage.animationFrames.first();
		}
		if (stage.imageProgram >= 0 && stage.imageProgram < definition.imagePrograms.size()) {
			const QStringList leaves = materialImageProgramImages(definition.imagePrograms, stage.imageProgram);
			return leaves.isEmpty() ? QString() : leaves.first();
		}
		return QString();
	};
	for (const MaterialStage& stage : definition.stages) {
		if (stage.role == MaterialStageRole::Diffuse) {
			const QString image = stageImage(stage);
			if (!image.isEmpty()) {
				return image;
			}
		}
	}
	for (const MaterialStage& stage : definition.stages) {
		const QString image = stageImage(stage);
		if (!image.isEmpty() && !image.startsWith(QLatin1Char('_')) && !image.startsWith(QLatin1Char('$'))) {
			return image;
		}
	}
	return QString();
}

MaterialLookup::MaterialLookup(MaterialLibraryOptions options)
	: m_options(std::move(options)), m_cache(std::make_shared<MaterialImageCache>(64LL * 1024 * 1024))
{
}

void MaterialLookup::setPackage(std::shared_ptr<const PackageArchiveReader> reader)
{
	QMutexLocker locker(&m_mutex);
	m_reader = std::move(reader);
	m_library.reset();
	m_cache->clear();
}

void MaterialLookup::setOptions(const MaterialLibraryOptions& options)
{
	QMutexLocker locker(&m_mutex);
	m_options = options;
	m_library.reset();
	m_cache->clear();
}

void MaterialLookup::reload(const MaterialLibraryProgress& progress)
{
	std::shared_ptr<const PackageArchiveReader> reader;
	MaterialLibraryOptions options;
	{
		QMutexLocker locker(&m_mutex);
		reader = m_reader;
		options = m_options;
	}
	std::shared_ptr<const MaterialLibrary> library;
	if (reader) {
		library = std::make_shared<const MaterialLibrary>(scanMaterialLibrary(reader, options, progress));
	}
	QMutexLocker locker(&m_mutex);
	// A package changed meanwhile keeps the newer state.
	if (reader == m_reader) {
		m_library = library;
		m_cache->clear();
	}
}

void MaterialLookup::invalidate()
{
	QMutexLocker locker(&m_mutex);
	m_library.reset();
	m_cache->clear();
}

bool MaterialLookup::isLoaded() const
{
	QMutexLocker locker(&m_mutex);
	return static_cast<bool>(m_library);
}

std::shared_ptr<const MaterialLibrary> MaterialLookup::library() const
{
	QMutexLocker locker(&m_mutex);
	return m_library;
}

MaterialLookupResult MaterialLookup::find(const QString& faceTexture, MaterialEngine engine) const
{
	MaterialLookupResult result;
	const QString name = faceTexture.trimmed();
	if (name.isEmpty()) {
		return result;
	}
	const std::shared_ptr<const MaterialLibrary> library = this->library();
	int entry = library ? library->indexOf(name, engine) : -1;
	// Quake III maps name shaders without their textures/ folder.
	const QString prefixed = QStringLiteral("textures/") + name;
	if (entry < 0 && library && !name.startsWith(QStringLiteral("textures/"), Qt::CaseInsensitive)) {
		entry = library->indexOf(prefixed, engine);
	}
	if (entry >= 0) {
		result.found = true;
		result.entry = entry;
		result.definition = library->definition(entry);
	} else if (engine == MaterialEngine::Doom3) {
		result.definition = implicitDoom3Material(name);
	} else if (engine == MaterialEngine::Quake3 || engine == MaterialEngine::Unknown) {
		// R_FindShader's default for a lightmapped world surface.
		result.definition = implicitQuake3Material(name.startsWith(QStringLiteral("textures/"), Qt::CaseInsensitive) ? name : prefixed);
	}
	result.displayImage = materialDisplayImage(result.definition);
	result.animated = result.definition.isAnimated();
	return result;
}

QImage MaterialLookup::swatch(const MaterialLookupResult& result, int side, double seconds) const
{
	if (result.definition.name.isEmpty() || side <= 0) {
		return {};
	}
	const std::shared_ptr<const MaterialLibrary> library = this->library();
	MaterialImageSource source;
	source.archive = library ? library->reader : std::shared_ptr<const PackageArchiveReader>();
	source.doomCatalog = library ? library->doomCatalog : nullptr;
	source.revision = library ? library->sourcePath : QString();
	source.cache = m_cache.get();
	const MaterialImageSet images = resolveMaterialImages(result.definition, source);
	const MaterialTableSet tables = library ? library->tables : MaterialTableSet();
	return renderMaterialSwatch(result.definition, images, tables, side, seconds).image;
}

} // namespace vibestudio
