#pragma once

#include "core/model_mesh.h"

namespace vibestudio
{
struct ModelUvAtlasOptions
{
	int resolution = 512;
	int padding = 4;
	int vertexLimit = 65536;
	// Callers may lower this ceiling for a constrained worker or a test.
	qsizetype memoryLimit = 256 * 1024 * 1024;
	// Width is resolution; zero height preserves the existing square API.
	int height = 0;
	// Packing only: also try every island turned a quarter turn so its long
	// side lies along U, then along V, keeping the densest fit. Islands are
	// never mirrored; a result that would mirror one is refused.
	bool allowRotation = false;
};
struct ModelUvAtlasReport
{
	int charts = 0;
	qsizetype peakBytes = 0;
};

// Unwrap selected faces from one reference pose, or repack existing UV islands.
// All coordinates share one atlas. Unwrap preserves chart shape in pixel space;
// Pack preserves orientation and relative UV scale for the given texture size.
// Unselected corners, materials and all animation geometry are retained.
// Inputs must come from a validated editable model. Output is atomic on failure.
bool atlasModelUv(const ModelSurface &source, const QSet<int> &faces, int frame, bool unwrap, const ModelUvAtlasOptions &options,
				  ModelSurface *result, QString *error = nullptr, const ModelWorkControl &control = {},
				  ModelUvAtlasReport *report = nullptr);
// Whether two surfaces share a nonempty material slot path (case-insensitive,
// slash-normalised), the rule Pack Around Unselected uses for shared skins.
[[nodiscard]] bool modelUvSurfacesShareMaterial(const ModelSurface &a, const ModelSurface &b);
// Packs the selected faces of `surface` and every face of `others` (surfaces
// sharing its material) into one atlas, keeping each island's shape,
// orientation and relative scale, so no island overlaps another across the
// union. Corners split per surface as atlasModelUv splits them; every pose
// and the other surfaces' data survive. Output is atomic on failure.
bool atlasModelUvSurfaces(const ModelMesh &mesh, int surface, const QSet<int> &faces, const QSet<int> &others, int frame,
						  const ModelUvAtlasOptions &options, ModelMesh *result, QString *error = nullptr, const ModelWorkControl &control = {},
						  ModelUvAtlasReport *report = nullptr);
} // namespace vibestudio
