#pragma once

// Texture map baking (UVSKIN-06): ambient occlusion, object-space and
// tangent-space normals (from the model itself or projected from a high-poly
// model, as Doom 3's renderbump does), and directional lighting, rasterised
// into a surface's UV layout.
//
// - Texels are the centres of the image's pixels inside each triangle's UV
//   footprint (wrapped UVs outside 0-1 are left out), with the frame's
//   position and interpolated normal.
// - Ambient occlusion casts cosine-weighted hemisphere rays (a seeded,
//   per-texel random sequence, so the result does not depend on threading or
//   order) against every triangle of the model, or of the high-poly model
//   when given; white is unoccluded.
// - Normals from a high-poly model cast a ray from the texel pushed out along
//   its normal by the cage distance back along the normal, and take the
//   nearest high-poly hit's interpolated normal. Without a high-poly model
//   the low-poly normal is baked.
// - Tangent space follows Doom 3's R_DeriveTangents (neo/renderer/
//   tr_trisurf.cpp): per-triangle tangents along the texture's s and t
//   gradients, made orthogonal to the normal. Red is +s, green +t (down the
//   image, as Doom 3 reads its bump maps) and blue the normal.
// - Lighting is ambient plus a directional light's Lambert term, shadowed by
//   the model.
// - Padding then bleeds covered texels into empty neighbours so filtering
//   and mip-mapping do not pull in the background at seams.
//
// Original VibeStudio code; the BVH is core/model_bvh.h.

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QImage>
#include <QJsonObject>
#include <QSize>
#include <QStringList>
#include <QVector>

namespace vibestudio {

enum class ModelBakeType {
	AmbientOcclusion,
	ObjectNormal,
	TangentNormal,
	Lighting,
};

QString modelBakeTypeId(ModelBakeType type); // "ao", "object-normal", "normal", "lighting"
bool modelBakeTypeFromId(const QString& id, ModelBakeType* type);
QString modelBakeTypeDisplayName(ModelBakeType type);

struct ModelBakeOptions {
	ModelBakeType type = ModelBakeType::AmbientOcclusion;
	QSize size{256, 256};
	// Ambient occlusion rays per texel (1-1024).
	int samples = 64;
	// How far occluders count, in model units; 0 uses a quarter of the
	// model's bounding radius.
	double maxDistance = 0.0;
	// How far outside the low-poly surface high-poly rays start; 0 uses 5%
	// of the bounding radius.
	double cageDistance = 0.0;
	// Texels to bleed past island edges.
	int padding = 4;
	quint32 seed = 1;
	// Lighting: direction towards the light (normalised) and ambient level.
	ModelVec3 lightDirection{0.4f, -0.35f, 0.85f};
	double ambient = 0.25;
	// The work refuses to start past this many bytes.
	qint64 memoryBudget = 768LL * 1024LL * 1024LL;
};

struct ModelBakeResult {
	QImage image;
	int texels = 0;
	// High-poly rays that found nothing (their texels keep the low-poly
	// normal).
	int misses = 0;
	double maxDistance = 0.0;
	double cageDistance = 0.0;
	QStringList notes;
};

// Bakes `surfaces` (empty: every surface) of `target` at `frame` into one
// image over the shared UV space. `highPoly`, when given, is a decoded or
// editable model in the same space (its first frame is used). False with an
// error on cancellation, an empty UV footprint or a refused budget; the
// result is untouched then.
bool bakeModelMaps(const ModelMesh& target, const QVector<int>& surfaces, int frame, const ModelMesh* highPoly, const ModelBakeOptions& options,
	ModelBakeResult* result, QString* error = nullptr, const ModelWorkControl& control = {});

// Bytes a bake would use, for the budget check and the dialog.
qint64 modelBakeMemoryEstimate(const ModelMesh& target, const ModelMesh* highPoly, const ModelBakeOptions& options);

// {type, width, height, texels, misses, maxDistance, cageDistance, sha256, notes}
QJsonObject modelBakeResultJson(const ModelBakeResult& result, const ModelBakeOptions& options);

// Hands a baked occlusion or lighting map to a skin (UVSKIN-06): every texel's
// colour times lerp(1, map, strength), the map scaled to the skin. An indexed
// skin takes the nearest palette entry below `fullbrightStart` (texels at or
// above it keep their index, so fullbrights stay lit); -1 uses the whole
// palette. Normal maps are refused: they are not a shade.
bool multiplyModelBakeIntoSkin(const QImage& skin, const QImage& map, ModelBakeType type, double strength, int fullbrightStart, QImage* result,
	QString* error = nullptr);
// The package path a baked map is staged at beside a skin image: Doom 3's
// "<name>_local.tga" for tangent-space normals, "<name>_ao.tga",
// "<name>_objnormal.tga" and "<name>_light.tga" for the others.
QString modelBakeCompanionPath(const QString& imagePath, ModelBakeType type);

} // namespace vibestudio
