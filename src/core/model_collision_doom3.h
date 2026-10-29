#pragma once

#include "core/model_collision.h"

namespace vibestudio
{
class PackageArchiveReader;
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
// slot. Every mesh frame holds the volume's pose at that frame, in the shape it
// has at `frame`: one surface keeps one topology, so a hull with a shape per pose
// is exported in the exported frame's shape.
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

// A Doom 3 collision model file (.cm) for a static model. Doom 3's
// idCollisionModelManagerLocal::LoadModel (cm/CollisionModel_load.cpp) first
// reads the model's path with its extension changed to .cm, with CRC 0 so the
// file's CRC is not compared, and converts the render surfaces only when there
// is none. The file follows cm/CollisionModel_files.cpp: `CM "1.00"`, a CRC,
// then one collisionModel named exactly as the game loads the model (such as
// models/props/crate.ase). Every volume's faces become polygons wound
// counter-clockwise seen from outside, edge 0 is the reserved dummy, every
// edge has two users and none is internal, and one leaf node holds them all.
// Contents come from the material, as for any polygon.
struct ModelDoom3CollisionModelFile
{
	QByteArray bytes;
	QString modelPath; // the collisionModel name: the model's game path
	QString filePath;  // where Doom 3 looks for it: modelPath with .cm
	int polygons = 0, edges = 0, vertices = 0;
	QStringList notes;
};
// modelPath is the static model's game path, ending .ase, .lwo or .ma (the
// render formats LoadRenderModel converts). Animated volumes take request.frame.
bool exportModelDoom3CollisionModelFile(const ModelMesh &mesh, const QString &modelPath, const ModelCollisionSurfaceExport &request,
										ModelDoom3CollisionModelFile *result, QString *error = nullptr,
										const ModelWorkControl &control = {});

// Whether a package's Doom 3 material scripts give the collision material the
// `collision` keyword (an infoParm in Material.cpp, which sets SURF_COLLISION
// for LoadRenderModel). Without it Doom 3 draws the collision surfaces and
// collides with every surface, so exports report it rather than refuse.
enum class ModelDoom3CollisionMaterialState
{
	Unchecked,	   // no package, or the scan was cancelled
	Defined,	   // defined with the collision keyword
	Undefined,	   // no material script in the package defines it
	NoCollisionKey // defined without the collision keyword
};
struct ModelDoom3CollisionMaterialCheck
{
	ModelDoom3CollisionMaterialState state = ModelDoom3CollisionMaterialState::Unchecked;
	QString material;
	QString sourcePath; // the defining script, when there is one
	QString note;		// empty unless the material needs attention
};
ModelDoom3CollisionMaterialCheck checkModelDoom3CollisionMaterial(const PackageArchiveReader &reader, const QString &material,
																	const ModelWorkControl &control = {});
// "unchecked", "defined", "undefined" or "no-collision-keyword".
QString modelDoom3CollisionMaterialStateId(ModelDoom3CollisionMaterialState state);
} // namespace vibestudio
