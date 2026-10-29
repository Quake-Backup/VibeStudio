#pragma once

// Plays animated materials on the Levels camera: a face whose material
// changes over time (Quake III scrolling, waves, animMap and videos; Doom 3
// expressions; Quake and Quake II texture animations) shows a frame the
// material renderer draws for this moment, several times a second, while the
// camera shows. The materials are looked up the way the Materials page reads
// them (core/material_lookup.h), with the game's base packages beneath the
// package. Only those faces' images change, so the selection and any drag
// in progress are untouched; turning it off puts the still images back.

#include "core/material_lookup.h"
#include "core/model_mesh.h"

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QStringList>

#include <memory>

class QTimer;

namespace vibestudio {

class MaterialTaskLane;
class ModelViewport;

class LevelMaterialAnimator final : public QObject {
public:
	explicit LevelMaterialAnimator(ModelViewport* view, QObject* parent = nullptr);
	~LevelMaterialAnimator() override;

	void setEnabled(bool enabled);
	[[nodiscard]] bool isEnabled() const { return m_enabled; }
	// Reduced motion keeps every face still.
	void setReducedMotion(bool reduced);
	// The package the map's materials come from and the packages beneath it
	// (file paths, lowest first), read for the Materials page's Quake III
	// game. Read in the background when next needed.
	void setPackage(std::shared_ptr<const PackageArchiveReader> reader, const QStringList& basePaths,
		MaterialDialect dialect = MaterialDialect::Automatic);
	[[nodiscard]] MaterialDialect dialect() const { return m_dialect; }
	// The camera's mesh now shown, its surfaces named by material, and each
	// surface's still image.
	void setSurfaces(const ModelMesh& mesh, MaterialEngine engine, const QHash<int, QImage>& stillImages);
	// Stops and puts the still images back.
	void stop();

	[[nodiscard]] QStringList animatedMaterials() const { return m_animated.keys(); }
	[[nodiscard]] int framesShown() const { return m_frames; }
	// Reading the package or working out which materials animate.
	[[nodiscard]] bool isPreparing() const;

private:
	void prepare();
	void tick();
	void restoreStillImages();

	QPointer<ModelViewport> m_view;
	QTimer* m_timer = nullptr;
	MaterialTaskLane* m_prepareLane = nullptr;
	MaterialTaskLane* m_frameLane = nullptr;
	std::shared_ptr<MaterialLookup> m_lookup;
	std::shared_ptr<const PackageArchiveReader> m_reader;
	QStringList m_basePaths;
	MaterialDialect m_dialect = MaterialDialect::Automatic;
	bool m_lookupStale = true;
	MaterialEngine m_engine = MaterialEngine::Unknown;
	// Material names (lookup keys) to the surfaces that show them.
	QHash<QString, QVector<int>> m_surfaces;
	QHash<int, QImage> m_stillImages;
	QHash<QString, MaterialLookupResult> m_animated;
	QElapsedTimer m_clock;
	bool m_enabled = false;
	bool m_reducedMotion = false;
	quint64 m_generation = 0;
	int m_frames = 0;
};

} // namespace vibestudio
