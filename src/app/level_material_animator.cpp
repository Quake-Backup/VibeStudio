#include "app/level_material_animator.h"

#include "app/material_tasks.h"
#include "app/model_viewport.h"
#include "core/package_archive.h"

#include <QTimer>

namespace vibestudio {

namespace {

// Frames a second, the swatch side and how many materials play at once: a
// camera full of animated faces stays responsive.
constexpr int kFrameIntervalMs = 125;
constexpr int kSwatchSide = 64;
constexpr int kMaximumAnimated = 24;

} // namespace

LevelMaterialAnimator::LevelMaterialAnimator(ModelViewport* view, QObject* parent)
	: QObject(parent), m_view(view), m_lookup(std::make_shared<MaterialLookup>())
{
	m_timer = new QTimer(this);
	m_timer->setTimerType(Qt::PreciseTimer);
	m_timer->setInterval(kFrameIntervalMs);
	connect(m_timer, &QTimer::timeout, this, &LevelMaterialAnimator::tick);
	m_prepareLane = new MaterialTaskLane(this);
	m_frameLane = new MaterialTaskLane(this);
	m_clock.start();
}

LevelMaterialAnimator::~LevelMaterialAnimator()
{
	// Background work publishes into members; stop it before they go.
	delete m_prepareLane;
	m_prepareLane = nullptr;
	delete m_frameLane;
	m_frameLane = nullptr;
}

void LevelMaterialAnimator::setEnabled(bool enabled)
{
	if (enabled == m_enabled) {
		return;
	}
	m_enabled = enabled;
	if (m_enabled) {
		prepare();
	} else {
		stop();
	}
}

void LevelMaterialAnimator::setReducedMotion(bool reduced)
{
	if (reduced == m_reducedMotion) {
		return;
	}
	m_reducedMotion = reduced;
	if (m_reducedMotion) {
		stop();
	} else {
		prepare();
	}
}

void LevelMaterialAnimator::setPackage(std::shared_ptr<const PackageArchiveReader> reader, const QStringList& basePaths)
{
	m_reader = std::move(reader);
	m_basePaths = basePaths;
	m_lookupStale = true;
	prepare();
}

void LevelMaterialAnimator::setSurfaces(const ModelMesh& mesh, MaterialEngine engine, const QHash<int, QImage>& stillImages)
{
	m_surfaces.clear();
	for (const ModelSurface& surface : mesh.surfaces) {
		if (!surface.name.trimmed().isEmpty()) {
			m_surfaces[materialLookupKey(surface.name)].push_back(surface.index);
		}
	}
	m_engine = engine;
	m_stillImages = stillImages;
	m_animated.clear();
	prepare();
}

bool LevelMaterialAnimator::isPreparing() const
{
	return m_prepareLane && m_prepareLane->pending();
}

void LevelMaterialAnimator::prepare()
{
	++m_generation;
	m_timer->stop();
	m_frameLane->cancel();
	if (!m_enabled || m_reducedMotion || !m_reader || m_surfaces.isEmpty()) {
		m_prepareLane->cancel();
		return;
	}
	const std::shared_ptr<MaterialLookup> lookup = m_lookup;
	const std::shared_ptr<const PackageArchiveReader> reader = m_reader;
	const QStringList basePaths = m_basePaths;
	const bool reload = m_lookupStale || !lookup->isLoaded();
	const QStringList names = m_surfaces.keys();
	const MaterialEngine engine = m_engine;
	const quint64 generation = m_generation;
	QPointer<LevelMaterialAnimator> self(this);
	m_prepareLane->submit([lookup, reader, basePaths, reload, names, engine, generation, self](
							  const MaterialTaskLane::Cancelled& cancelled) -> std::function<void()> {
		if (reload) {
			MaterialLibraryOptions options;
			for (const QString& path : basePaths) {
				auto base = std::make_shared<PackageArchive>();
				QString error;
				if (base->load(path, &error)) {
					options.baseReaders.push_back(base);
				}
			}
			lookup->setOptions(options);
			lookup->setPackage(reader);
			lookup->reload([cancelled](int, int, const QString&) { return !cancelled(); });
		}
		if (cancelled()) {
			return {};
		}
		QHash<QString, MaterialLookupResult> animated;
		for (const QString& name : names) {
			const MaterialLookupResult result = lookup->find(name, engine);
			if (result.found && result.animated) {
				animated.insert(name, result);
				if (animated.size() >= kMaximumAnimated) {
					break;
				}
			}
		}
		return [self, animated, generation]() {
			if (!self || generation != self->m_generation) {
				return;
			}
			self->m_lookupStale = false;
			self->m_animated = animated;
			if (!animated.isEmpty()) {
				self->m_timer->start();
			}
		};
	});
}

void LevelMaterialAnimator::tick()
{
	if (!m_view || !m_view->isVisible() || m_animated.isEmpty()) {
		return;
	}
	// A frame still drawing: skip this one rather than queue behind it.
	if (m_frameLane->pending()) {
		return;
	}
	const std::shared_ptr<MaterialLookup> lookup = m_lookup;
	const QHash<QString, MaterialLookupResult> animated = m_animated;
	const double seconds = m_clock.elapsed() / 1000.0;
	const quint64 generation = m_generation;
	QPointer<LevelMaterialAnimator> self(this);
	m_frameLane->submit(
		[lookup, animated, seconds, generation, self](const MaterialTaskLane::Cancelled& cancelled) -> std::function<void()> {
			QHash<QString, QImage> frames;
			for (auto it = animated.cbegin(); it != animated.cend(); ++it) {
				if (cancelled()) {
					return {};
				}
				const QImage frame = lookup->swatch(it.value(), kSwatchSide, seconds);
				if (!frame.isNull()) {
					frames.insert(it.key(), frame);
				}
			}
			return [self, frames, generation]() {
				if (!self || generation != self->m_generation || !self->m_view) {
					return;
				}
				QHash<int, QImage> skins;
				for (auto it = frames.cbegin(); it != frames.cend(); ++it) {
					for (int surface : self->m_surfaces.value(it.key())) {
						skins.insert(surface, it.value());
					}
				}
				if (!skins.isEmpty()) {
					self->m_view->updateSurfaceSkinImages(skins);
					++self->m_frames;
				}
			};
		},
		false);
}

void LevelMaterialAnimator::restoreStillImages()
{
	if (!m_view || m_animated.isEmpty()) {
		return;
	}
	QHash<int, QImage> skins;
	for (auto it = m_animated.cbegin(); it != m_animated.cend(); ++it) {
		for (int surface : m_surfaces.value(it.key())) {
			const QImage still = m_stillImages.value(surface);
			if (!still.isNull()) {
				skins.insert(surface, still);
			}
		}
	}
	if (!skins.isEmpty()) {
		m_view->updateSurfaceSkinImages(skins);
	}
}

void LevelMaterialAnimator::stop()
{
	++m_generation;
	m_timer->stop();
	m_prepareLane->cancel();
	m_frameLane->cancel();
	restoreStillImages();
	m_animated.clear();
}

} // namespace vibestudio
