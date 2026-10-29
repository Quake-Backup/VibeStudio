#pragma once

#include "core/model_mesh.h"

#include <QPointF>
#include <functional>

namespace vibestudio
{
// Texel-centre rasterisation of UV triangles, shared by UV health (texels a
// layout uses), UV layout export and skin transfer.
//
// Visits every texel (x, y) of a width x height grid whose centre (x + 0.5,
// y + 0.5) lies inside the triangle with pixel-space corners a, b, c. A
// consistent fill rule visits a texel centre on an edge shared by two
// triangles exactly once. w0, w1 and w2 are the centre's barycentric weights
// for a, b and c. Collapsed triangles visit nothing. `visit` returns false to
// stop; rasterizeModelUvTriangle then returns false.
using ModelUvTexelVisitor = std::function<bool(int x, int y, double w0, double w1, double w2)>;
bool rasterizeModelUvTriangle(QPointF a, QPointF b, QPointF c, int width, int height, const ModelUvTexelVisitor &visit);
// A UV coordinate in pixel space of a width x height image (u right, v down).
[[nodiscard]] inline QPointF modelUvPixel(ModelTexCoord uv, int width, int height)
{
	return {double(uv.u) * width, double(uv.v) * height};
}
} // namespace vibestudio
