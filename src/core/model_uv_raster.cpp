#include "core/model_uv_raster.h"

#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
double orient(QPointF a, QPointF b, QPointF p)
{
	return (b.x() - a.x()) * (p.y() - a.y()) - (b.y() - a.y()) * (p.x() - a.x());
}
// With positive-area corner order, an edge lying exactly through a texel
// centre belongs to the triangle on its "owning" side. Two triangles sharing
// the edge see it in opposite directions, so exactly one owns it.
bool owns(QPointF from, QPointF to)
{
	const double dx = to.x() - from.x(), dy = to.y() - from.y();
	return dy > 0 || (dy == 0 && dx < 0);
}
} // namespace

bool rasterizeModelUvTriangle(QPointF a, QPointF b, QPointF c, int width, int height, const ModelUvTexelVisitor &visit)
{
	if (width < 1 || height < 1)
		return true;
	for (const auto p : {a, b, c})
		if (!std::isfinite(p.x()) || !std::isfinite(p.y()))
			return true;
	double area = orient(a, b, c);
	bool swapped = false;
	if (area < 0)
	{
		std::swap(b, c);
		area = -area;
		swapped = true;
	}
	if (!(area > 1e-18))
		return true;
	const double minX = std::min({a.x(), b.x(), c.x()}), maxX = std::max({a.x(), b.x(), c.x()});
	const double minY = std::min({a.y(), b.y(), c.y()}), maxY = std::max({a.y(), b.y(), c.y()});
	const int x0 = int(std::max(0.0, std::ceil(std::max(-1e9, minX) - .5))), x1 = int(std::min(double(width - 1), std::floor(std::min(1e9, maxX) - .5)));
	const int y0 = int(std::max(0.0, std::ceil(std::max(-1e9, minY) - .5))), y1 = int(std::min(double(height - 1), std::floor(std::min(1e9, maxY) - .5)));
	const bool ownA = owns(b, c), ownB = owns(c, a), ownC = owns(a, b);
	for (int y = y0; y <= y1; ++y)
	{
		for (int x = x0; x <= x1; ++x)
		{
			const QPointF p(x + .5, y + .5);
			const double ea = orient(b, c, p), eb = orient(c, a, p), ec = orient(a, b, p);
			if (ea < 0 || eb < 0 || ec < 0 || (ea == 0 && !ownA) || (eb == 0 && !ownB) || (ec == 0 && !ownC))
				continue;
			const double wa = ea / area, wb = eb / area, wc = ec / area;
			if (!(swapped ? visit(x, y, wa, wc, wb) : visit(x, y, wa, wb, wc)))
				return false;
		}
	}
	return true;
}
} // namespace vibestudio
