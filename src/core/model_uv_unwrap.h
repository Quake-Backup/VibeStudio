#pragma once

#include "core/model_uv_atlas.h"

#include <QHash>
#include <QJsonObject>

namespace vibestudio
{
// Seam-bounded conformal unwrap: Least Squares Conformal Maps (Lévy, Petitjean,
// Ray and Maillot, SIGGRAPH 2002), solved per island with a Jacobi-scaled
// conjugate-gradient least-squares iteration in double precision. Original
// implementation from the published method; see docs/CREDITS.md.
struct ModelUvUnwrapOptions
{
	// Reference pose the islands are flattened from.
	int frame = 0;
	// Pack the flattened islands into one tile with `atlas` (pack mode, so
	// shape, orientation and relative density stay), or leave them in a row
	// at one shared density inside the tile.
	bool pack = true;
	ModelUvAtlasOptions atlas;
	// Conjugate-gradient iterations per island and the relative residual
	// at which an island counts as solved.
	int maxIterations = 20000;
	double tolerance = 1e-11;
};
struct ModelUvUnwrapIsland
{
	QVector<int> faces;
	// Pinned islands keep their pinned UVs exactly (before packing).
	bool pinned = false;
	int iterations = 0;
	// Per face, angle distortion is σ1/σ2 and the area ratio σ1σ2/ρ² for the
	// singular values σ of the flattening and the island's density ρ; 1 is
	// perfect. Means are weighted by model area.
	double meanAngleDistortion = 1, maxAngleDistortion = 1, minAreaRatio = 1, maxAreaRatio = 1;
};
struct ModelUvUnwrapReport
{
	QVector<ModelUvUnwrapIsland> islands;
	double maxAngleDistortion = 1, minAreaRatio = 1, maxAreaRatio = 1;
};
// Flattens every island the selected faces form, one chart per island:
// marked seams, split UV corners and mesh boundaries bound islands, and no
// other cut is ever added. `pins` (surface vertex -> UV) fix those corners in
// islands with two or more pins; islands with fewer are pinned at their two
// boundary corners farthest apart and scaled to their model area. A closed
// island, or one that would fold or overlap itself, is refused with its faces
// named so a seam can be added. Corner splits copy every pose, normal and skin
// influence (applyModelUvMapping). Output and report are unchanged on failure.
bool unwrapModelUvConformal(const ModelSurface &source, const QSet<int> &faces, const QHash<int, ModelTexCoord> &pins,
							const ModelUvUnwrapOptions &options, ModelSurface *result, ModelUvUnwrapReport *report = nullptr,
							QString *error = nullptr, const ModelWorkControl &control = {});
[[nodiscard]] QJsonObject modelUvUnwrapJson(const ModelUvUnwrapReport &report);
} // namespace vibestudio
