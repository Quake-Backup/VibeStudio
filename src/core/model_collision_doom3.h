#pragma once

#include "core/model_collision.h"

namespace vibestudio
{
struct ModelExportReport;

// Doom 3 collision output. Doom 3 builds a model's collision from its render
// surfaces; when any surface's material has the `collision` keyword (such as
// textures/common/collision), only those surfaces collide (CollisionModel_load.cpp,
// idCollisionModelManagerLocal::LoadRenderModel). A moveable's `clipmodel`
// spawnArg names a model whose single convex volume becomes its trace model,
// within MAX_TRACEMODEL_* (idlib/geometry/TraceModel.h).
struct ModelCollisionSurfaceExport
{
	QString material = QStringLiteral("textures/common/collision");
	// Static volumes ignore it; animated volumes are sampled at this stored
	// frame (required when any volume is animated). ASE writes this frame.
	int frame = 0;
};
// One closed surface per volume, named collision_<volume>, triangles wound so
// cross(b - a, c - a) points outwards, flat normals, the material as the only
// slot. Every mesh frame holds the volume's pose at that frame.
bool modelCollisionSurfaces(const ModelMesh &mesh, const ModelCollisionSurfaceExport &request, QVector<ModelSurface> *surfaces,
							QString *error = nullptr, const ModelWorkControl &control = {});
// The render surfaces plus the collision surfaces as one ASE scene. Render
// surfaces are written exactly as a plain ASE export writes them.
QByteArray exportModelAseWithCollision(const ModelMesh &mesh, const ModelCollisionSurfaceExport &request, QString *error = nullptr,
									   const ModelWorkControl &control = {}, ModelExportReport *report = nullptr);
struct ModelDoom3ClipModel
{
	QByteArray bytes; // an ASE holding the volume alone
	int faces = 0, edges = 0, vertices = 0;
	// For an axis-aligned static box: the equivalent "mins" and "maxs" keys.
	bool hasBounds = false;
	ModelVec3 mins, maxs;
	QStringList notes;
};
// One volume (box or hull) as a Doom 3 clip model. Refuses a volume over the
// trace model budget (16 polygons, 32 edges, 32 vertices).
bool exportModelDoom3ClipModel(const ModelMesh &mesh, const QString &volume, const ModelCollisionSurfaceExport &request,
							   ModelDoom3ClipModel *result, QString *error = nullptr, const ModelWorkControl &control = {});
} // namespace vibestudio
