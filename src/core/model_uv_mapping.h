#pragma once
#include "core/model_uv_atlas.h"

namespace vibestudio
{
// Internal shared atlas output admission. Original face/corner identities are
// retained; chart cuts copy every pose and remap authored seam marks.
bool validateModelUvMapping(const QVector<ModelTexCoord> &uv, const QVector<uint32_t> &indices, QString *error,
							const ModelWorkControl &control);
bool applyModelUvMapping(const ModelSurface &source, const QVector<int> &faces, const QVector<int> &sourceVertices,
						 const QVector<ModelTexCoord> &uv, const QVector<uint32_t> &indices, int vertexLimit, ModelSurface *result,
						 QString *error, const ModelWorkControl &control);
// The sweep validateModelUvMapping refuses with, reporting instead of failing:
// pairs of triangles (positions in `indices` / 3, lower first) whose UVs share
// interior area. Shared edges and vertices do not count. Triangles marked in
// `skip` (for example collapsed ones) are left out. Stops after `maxPairs`
// pairs (0: no cap) or `maxComparisons` bounding-box overlaps, setting
// `limitReached` for the latter. False only on cancellation.
struct ModelUvOverlapScan
{
	QVector<QPair<int, int>> pairs;
	bool limitReached = false;
	quint64 comparisons = 0;
};
bool findModelUvOverlaps(const QVector<ModelTexCoord> &uv, const QVector<uint32_t> &indices, const QVector<bool> &skip, int maxPairs,
						 quint64 maxComparisons, ModelUvOverlapScan *scan, QString *error, const ModelWorkControl &control);
} // namespace vibestudio
