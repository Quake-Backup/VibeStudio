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
// (applyModelUvMapping). Read-only on the model; the caller commits the image.
enum class ModelSkinTransferFilter
{
	Nearest,
	Bilinear
};
struct ModelSkinTransferOptions
{
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
	// Faces only one of the surfaces has, and faces whose old or new UVs are
	// collapsed, which cannot carry pixels.
	QVector<int> missingFaces, collapsedFaces;
	bool indexed = false;
};
// Paints `outputSize` texels for `after`'s UVs from `source` read through
// `before`'s UVs. An indexed source (Format_Indexed8) gives an indexed result
// with its colour table: nearest sampling copies indices exactly, bilinear
// blends colours and maps each texel to the nearest palette entry. Output is
// unchanged on failure or cancellation.
bool transferModelSkin(const ModelSurface &before, const ModelSurface &after, const QImage &source, QSize outputSize,
					   const ModelSkinTransferOptions &options, QImage *result, ModelSkinTransferReport *report = nullptr,
					   QString *error = nullptr, const ModelWorkControl &control = {});
[[nodiscard]] QJsonObject modelSkinTransferJson(const ModelSkinTransferReport &report);
} // namespace vibestudio
