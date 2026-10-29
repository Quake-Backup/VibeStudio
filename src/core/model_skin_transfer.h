#pragma once

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QImage>
#include <QJsonObject>

namespace vibestudio
{
// Skin transfer (rebake): repaints a skin for changed UVs by sampling the old
// skin through each face's old UVs, so unwrapping, packing or moving islands
// keeps the painted art. Faces are matched by index, which every UV edit keeps
// (applyModelUvMapping); faces an edit reshaped, added or renumbered are
// found by projection onto the old surface instead. Read-only on the model;
// the caller commits the image.
enum class ModelSkinTransferFilter
{
	Nearest,
	Bilinear
};
// How each new face finds where it was painted on the old surface.
enum class ModelSkinTransferMapping
{
	// By face index where the face still has the same corners in the
	// reference pose, by projection otherwise (topology edits).
	Automatic,
	// By face index only; faces only one surface has are reported missing.
	Faces,
	// Every texel by projection: the nearest point of the old surface (in
	// the reference pose) to the texel's point on the new surface.
	Projection
};
struct ModelSkinTransferOptions
{
	ModelSkinTransferMapping mapping = ModelSkinTransferMapping::Automatic;
	// The pose both surfaces are compared and projected in.
	int referenceFrame = 0;
	ModelSkinTransferFilter filter = ModelSkinTransferFilter::Nearest;
	// Texels around each island repeated outward, for mipmaps and filtering.
	int padding = 2;
	// Old UVs outside 0–1 sample the skin as a repeating tile; otherwise the
	// edge texels repeat.
	bool wrap = true;
	// Texels no face covers: transparent for colour skins, this index for
	// indexed (MDL) skins.
	QColor background = QColor(0, 0, 0, 0);
	int backgroundIndex = 0;
	bool operator==(const ModelSkinTransferOptions &) const = default;
};
struct ModelSkinTransferReport
{
	QSize size;
	int faces = 0, texels = 0, paddedTexels = 0;
	// Faces only one of the surfaces has (face mapping), and faces whose old
	// or new UVs are collapsed, which cannot carry pixels.
	QVector<int> missingFaces, collapsedFaces;
	// New faces painted by projection, and the farthest any of their texels
	// lay from the old surface (model units, reference pose).
	QVector<int> projectedFaces;
	double maxProjectionDistance = 0;
	bool indexed = false;
};
// Paints `outputSize` texels for `after`'s UVs from `source` read through
// `before`'s UVs, matching faces by `options.mapping`. An indexed source (Format_Indexed8) gives an indexed result
// with its colour table: nearest sampling copies indices exactly, bilinear
// blends colours and maps each texel to the nearest palette entry. Output is
// unchanged on failure or cancellation.
bool transferModelSkin(const ModelSurface &before, const ModelSurface &after, const QImage &source, QSize outputSize,
					   const ModelSkinTransferOptions &options, QImage *result, ModelSkinTransferReport *report = nullptr,
					   QString *error = nullptr, const ModelWorkControl &control = {});
[[nodiscard]] QJsonObject modelSkinTransferJson(const ModelSkinTransferReport &report);
} // namespace vibestudio
