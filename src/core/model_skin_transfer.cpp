#include "core/model_skin_transfer.h"

#include "core/model_uv_raster.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelSkinTransfer)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
int wrapIndex(int value, int length)
{
	const int r = value % length;
	return r < 0 ? r + length : r;
}
int texel(double coordinate, int length, bool wrap)
{
	const int index = int(std::floor(coordinate));
	return wrap ? wrapIndex(index, length) : std::clamp(index, 0, length - 1);
}
double uvArea(ModelTexCoord a, ModelTexCoord b, ModelTexCoord c)
{
	return (double(b.u) - a.u) * (double(c.v) - a.v) - (double(b.v) - a.v) * (double(c.u) - a.u);
}
// Nearest points on the old surface, for faces matched by projection: a
// bounding volume hierarchy over the old faces in the reference pose, and the
// closest point on a triangle by its Voronoi regions (Ericson, Real-Time
// Collision Detection, 2005, section 5.1.5).
struct Point
{
	double x = 0, y = 0, z = 0;
};
Point operator-(Point a, Point b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Point operator+(Point a, Point b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Point operator*(Point a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(Point a, Point b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
struct Nearest
{
	int face = -1;
	double distance2 = std::numeric_limits<double>::max();
	// Barycentric weights of the nearest point on `face`.
	double w0 = 1, w1 = 0, w2 = 0;
};
void closestOnTriangle(Point p, Point a, Point b, Point c, double *w0, double *w1, double *w2)
{
	const auto set = [&](double u, double v, double w)
	{
		*w0 = u;
		*w1 = v;
		*w2 = w;
	};
	const Point ab = b - a, ac = c - a, ap = p - a;
	const double d1 = dot(ab, ap), d2 = dot(ac, ap);
	if (d1 <= 0 && d2 <= 0)
		return set(1, 0, 0);
	const Point bp = p - b;
	const double d3 = dot(ab, bp), d4 = dot(ac, bp);
	if (d3 >= 0 && d4 <= d3)
		return set(0, 1, 0);
	const double vc = d1 * d4 - d3 * d2;
	if (vc <= 0 && d1 >= 0 && d3 <= 0)
	{
		const double v = d1 / (d1 - d3);
		return set(1 - v, v, 0);
	}
	const Point cp = p - c;
	const double d5 = dot(ab, cp), d6 = dot(ac, cp);
	if (d6 >= 0 && d5 <= d6)
		return set(0, 0, 1);
	const double vb = d5 * d2 - d1 * d6;
	if (vb <= 0 && d2 >= 0 && d6 <= 0)
	{
		const double w = d2 / (d2 - d6);
		return set(1 - w, 0, w);
	}
	const double va = d3 * d6 - d5 * d4;
	if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
	{
		const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		return set(0, 1 - w, w);
	}
	const double denominator = va + vb + vc;
	if (!(std::abs(denominator) > 0))
		return set(1, 0, 0);
	const double v = vb / denominator, w = vc / denominator;
	set(1 - v - w, v, w);
}
class FaceTree
{
  public:
	FaceTree(const QVector<std::array<Point, 3>> &corners, const QVector<int> &faces) : m_corners(corners)
	{
		m_items = faces;
		if (!m_items.isEmpty())
			build(0, int(m_items.size()));
	}
	[[nodiscard]] bool isEmpty() const { return m_items.isEmpty(); }
	Nearest nearest(Point p) const
	{
		Nearest best;
		if (m_nodes.isEmpty())
			return best;
		QVector<int> stack{0};
		while (!stack.isEmpty())
		{
			const auto &node = m_nodes[stack.takeLast()];
			if (boxDistance2(node, p) > best.distance2)
				continue;
			if (node.count > 0)
			{
				for (int i = node.first; i < node.first + node.count; ++i)
				{
					const int face = m_items[i];
					const auto &t = m_corners[face];
					double w0 = 1, w1 = 0, w2 = 0;
					closestOnTriangle(p, t[0], t[1], t[2], &w0, &w1, &w2);
					const Point q = t[0] * w0 + t[1] * w1 + t[2] * w2;
					const double d = dot(q - p, q - p);
					// Ties go to the lower face index, so results never depend
					// on traversal order.
					if (d < best.distance2 || (d == best.distance2 && face < best.face))
						best = {face, d, w0, w1, w2};
				}
				continue;
			}
			// Nearer child last, so it is searched first.
			const auto &left = m_nodes[node.left], &right = m_nodes[node.right];
			if (boxDistance2(left, p) < boxDistance2(right, p))
			{
				stack.append(node.right);
				stack.append(node.left);
			}
			else
			{
				stack.append(node.left);
				stack.append(node.right);
			}
		}
		return best;
	}

  private:
	struct Node
	{
		Point mins, maxs;
		int left = -1, right = -1, first = 0, count = 0;
	};
	static double boxDistance2(const Node &node, Point p)
	{
		const auto axis = [](double value, double low, double high) { return value < low ? low - value : value > high ? value - high : 0.0; };
		const double dx = axis(p.x, node.mins.x, node.maxs.x), dy = axis(p.y, node.mins.y, node.maxs.y), dz = axis(p.z, node.mins.z, node.maxs.z);
		return dx * dx + dy * dy + dz * dz;
	}
	int build(int first, int count)
	{
		Node node;
		const double huge = std::numeric_limits<double>::max();
		node.mins = {huge, huge, huge};
		node.maxs = {-huge, -huge, -huge};
		for (int i = first; i < first + count; ++i)
			for (const auto &q : m_corners[m_items[i]])
			{
				node.mins = {std::min(node.mins.x, q.x), std::min(node.mins.y, q.y), std::min(node.mins.z, q.z)};
				node.maxs = {std::max(node.maxs.x, q.x), std::max(node.maxs.y, q.y), std::max(node.maxs.z, q.z)};
			}
		const int index = int(m_nodes.size());
		m_nodes.append(node);
		if (count <= 4)
		{
			m_nodes[index].first = first;
			m_nodes[index].count = count;
			return index;
		}
		// Split at the median centroid along the longest axis.
		const Point extent = node.maxs - node.mins;
		const int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : extent.y >= extent.z ? 1 : 2;
		const auto centre = [&](int face)
		{
			const auto &t = m_corners[face];
			const Point sum = t[0] + t[1] + t[2];
			return axis == 0 ? sum.x : axis == 1 ? sum.y : sum.z;
		};
		const int middle = first + count / 2;
		std::nth_element(m_items.begin() + first, m_items.begin() + middle, m_items.begin() + first + count,
						 [&](int a, int b) { return centre(a) < centre(b) || (centre(a) == centre(b) && a < b); });
		const int left = build(first, middle - first);
		const int right = build(middle, first + count - middle);
		m_nodes[index].left = left;
		m_nodes[index].right = right;
		return index;
	}
	const QVector<std::array<Point, 3>> &m_corners;
	QVector<int> m_items;
	QVector<Node> m_nodes;
};
QVector<std::array<Point, 3>> faceCorners(const ModelSurface &surface, int frame)
{
	QVector<std::array<Point, 3>> corners(surface.triangles.size());
	if (frame < 0 || frame >= surface.frames.size())
		return corners;
	const auto &positions = surface.frames[frame].positions;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		const auto t = surface.triangles[face];
		const auto point = [&](int v) { return v >= 0 && v < positions.size() ? Point{positions[v].x, positions[v].y, positions[v].z} : Point{}; };
		corners[face] = {point(t.a), point(t.b), point(t.c)};
	}
	return corners;
}
int nearestIndex(const QVector<QRgb> &table, QRgb colour)
{
	int best = 0;
	qint64 distance = std::numeric_limits<qint64>::max();
	for (int i = 0; i < table.size(); ++i)
	{
		const qint64 dr = qRed(table[i]) - qRed(colour), dg = qGreen(table[i]) - qGreen(colour), db = qBlue(table[i]) - qBlue(colour);
		const qint64 d = dr * dr + dg * dg + db * db;
		if (d < distance)
		{
			distance = d;
			best = i;
		}
	}
	return best;
}
} // namespace

bool transferModelSkin(const ModelSurface &before, const ModelSurface &after, const QImage &source, QSize outputSize,
					   const ModelSkinTransferOptions &options, QImage *result, ModelSkinTransferReport *report, QString *error,
					   const ModelWorkControl &control)
{
	if (error)
		error->clear();
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	if (!result || source.isNull() || source.width() > 16384 || source.height() > 16384 || outputSize.width() < 1 ||
		outputSize.height() < 1 || outputSize.width() > 4096 || outputSize.height() > 4096 || options.padding < 0 || options.padding > 64 ||
		before.texCoords.size() != before.vertexCount || after.texCoords.size() != after.vertexCount || options.referenceFrame < 0)
		return fail(error, Text::tr("Choose a skin image, an output size from 1 to 4,096 pixels per side and padding from 0 to 64 texels."));
	const int frame = options.referenceFrame;
	if (options.mapping != ModelSkinTransferMapping::Faces && (frame >= before.frames.size() || frame >= after.frames.size()))
		return fail(error, Text::tr("Both surfaces need the reference pose to find faces by projection."));
	const bool indexed = source.format() == QImage::Format_Indexed8;
	const QImage colours = indexed && options.filter == ModelSkinTransferFilter::Nearest ? QImage() : source.convertToFormat(QImage::Format_ARGB32);
	const QVector<QRgb> table = indexed ? source.colorTable() : QVector<QRgb>{};
	if (indexed && table.isEmpty())
		return fail(error, Text::tr("The indexed skin has no palette."));
	const int width = outputSize.width(), height = outputSize.height(), sw = source.width(), sh = source.height();
	QVector<QRgb> pixels(width * height, options.background.rgba());
	QVector<int> indices(indexed ? width * height : 0, std::clamp(options.backgroundIndex, 0, int(table.size()) - 1));
	QVector<bool> covered(width * height, false);
	ModelSkinTransferReport summary;
	summary.size = outputSize;
	summary.indexed = indexed;
	const int shared = int(std::min(before.triangles.size(), after.triangles.size()));
	const bool byFaces = options.mapping == ModelSkinTransferMapping::Faces;
	if (byFaces)
		for (int face = shared; face < std::max(before.triangles.size(), after.triangles.size()); ++face)
			summary.missingFaces.append(face);
	// Faces that keep their index and their corners in the reference pose
	// are matched by index; the rest are projected.
	const auto oldCorners = byFaces ? QVector<std::array<Point, 3>>{} : faceCorners(before, frame);
	const auto newCorners = byFaces ? QVector<std::array<Point, 3>>{} : faceCorners(after, frame);
	const auto sameCorners = [&](int face)
	{
		if (byFaces)
			return face < shared;
		if (options.mapping == ModelSkinTransferMapping::Projection || face >= shared)
			return false;
		for (int k = 0; k < 3; ++k)
		{
			const Point d = oldCorners[face][k] - newCorners[face][k];
			if (dot(d, d) > 1e-12)
				return false;
		}
		return true;
	};
	// Old faces whose UVs can carry pixels, for projection.
	QVector<int> carriers;
	if (!byFaces)
		for (int face = 0; face < before.triangles.size(); ++face)
		{
			const auto o = before.triangles[face];
			if (std::abs(uvArea(before.texCoords[o.a], before.texCoords[o.b], before.texCoords[o.c])) >= 1e-14)
				carriers.append(face);
		}
	const FaceTree tree(oldCorners, carriers);
	const auto sample = [&](double u, double v, int *index) -> QRgb
	{
		const double x = u * sw, y = v * sh;
		if (options.filter == ModelSkinTransferFilter::Nearest)
		{
			const int px = texel(x, sw, options.wrap), py = texel(y, sh, options.wrap);
			if (indexed)
			{
				*index = source.pixelIndex(px, py);
				return table.value(*index);
			}
			return colours.pixel(px, py);
		}
		// Bilinear between the four texel centres around the point.
		const double fx = x - .5, fy = y - .5;
		const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
		const double tx = fx - x0, ty = fy - y0;
		double channel[4]{};
		for (int j = 0; j < 2; ++j)
			for (int i = 0; i < 2; ++i)
			{
				const int px = options.wrap ? wrapIndex(x0 + i, sw) : std::clamp(x0 + i, 0, sw - 1);
				const int py = options.wrap ? wrapIndex(y0 + j, sh) : std::clamp(y0 + j, 0, sh - 1);
				const QRgb c = colours.pixel(px, py);
				const double w = (i ? tx : 1 - tx) * (j ? ty : 1 - ty);
				channel[0] += qRed(c) * w;
				channel[1] += qGreen(c) * w;
				channel[2] += qBlue(c) * w;
				channel[3] += qAlpha(c) * w;
			}
		const QRgb blended = qRgba(int(std::lround(channel[0])), int(std::lround(channel[1])), int(std::lround(channel[2])),
								   int(std::lround(channel[3])));
		if (indexed)
			*index = nearestIndex(table, blended);
		return blended;
	};
	const int faces = byFaces ? shared : int(after.triangles.size());
	for (int face = 0; face < faces; ++face)
	{
		if (!work.step())
			return false;
		const bool byIndex = sameCorners(face);
		const auto t = after.triangles[face];
		const auto o = byIndex ? before.triangles[face] : ModelTriangle{};
		const ModelTexCoord n[3]{after.texCoords[t.a], after.texCoords[t.b], after.texCoords[t.c]};
		ModelTexCoord p[3]{};
		if (byIndex)
		{
			p[0] = before.texCoords[o.a];
			p[1] = before.texCoords[o.b];
			p[2] = before.texCoords[o.c];
		}
		if (std::abs(uvArea(n[0], n[1], n[2])) < 1e-14 || (byIndex && std::abs(uvArea(p[0], p[1], p[2])) < 1e-14))
		{
			summary.collapsedFaces.append(face);
			continue;
		}
		if (!byIndex && tree.isEmpty())
		{
			summary.missingFaces.append(face);
			continue;
		}
		if (!byIndex)
			summary.projectedFaces.append(face);
		++summary.faces;
		bool stopped = false;
		rasterizeModelUvTriangle(modelUvPixel(n[0], width, height), modelUvPixel(n[1], width, height), modelUvPixel(n[2], width, height),
								 width, height,
								 [&](int x, int y, double w0, double w1, double w2)
								 {
									 if (!work.step())
									 {
										 stopped = true;
										 return false;
									 }
									 double u = w0 * p[0].u + w1 * p[1].u + w2 * p[2].u;
									 double v = w0 * p[0].v + w1 * p[1].v + w2 * p[2].v;
									 if (!byIndex)
									 {
										 // The texel's point on the new face, then the
										 // nearest point of the old surface.
										 const auto &c = newCorners[face];
										 const auto hit = tree.nearest(c[0] * w0 + c[1] * w1 + c[2] * w2);
										 const auto g = before.triangles[hit.face];
										 const auto a = before.texCoords[g.a], b = before.texCoords[g.b], d = before.texCoords[g.c];
										 u = hit.w0 * a.u + hit.w1 * b.u + hit.w2 * d.u;
										 v = hit.w0 * a.v + hit.w1 * b.v + hit.w2 * d.v;
										 summary.maxProjectionDistance = std::max(summary.maxProjectionDistance, std::sqrt(hit.distance2));
									 }
									 int index = 0;
									 const auto colour = sample(u, v, &index);
									 const int at = y * width + x;
									 pixels[at] = colour;
									 if (indexed)
										 indices[at] = index;
									 summary.texels += !covered[at];
									 covered[at] = true;
									 return true;
								 });
		if (stopped)
			return false;
	}
	// Padding: each ring takes the colour of its first covered neighbour,
	// left, right, above, then below.
	for (int ring = 0; ring < options.padding; ++ring)
	{
		auto next = covered;
		for (int y = 0; y < height; ++y)
		{
			if (!work.step())
				return false;
			for (int x = 0; x < width; ++x)
			{
				const int at = y * width + x;
				if (covered[at])
					continue;
				const std::array<std::array<int, 2>, 4> around{{{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}}};
				for (const auto &n : around)
				{
					if (n[0] < 0 || n[1] < 0 || n[0] >= width || n[1] >= height || !covered[n[1] * width + n[0]])
						continue;
					const int from = n[1] * width + n[0];
					pixels[at] = pixels[from];
					if (indexed)
						indices[at] = indices[from];
					next[at] = true;
					++summary.paddedTexels;
					break;
				}
			}
		}
		covered = std::move(next);
	}
	QImage image;
	if (indexed)
	{
		image = QImage(width, height, QImage::Format_Indexed8);
		image.setColorTable(table);
		for (int y = 0; y < height; ++y)
		{
			auto *row = image.scanLine(y);
			for (int x = 0; x < width; ++x)
				row[x] = uchar(indices[y * width + x]);
		}
	}
	else
	{
		image = QImage(width, height, QImage::Format_ARGB32);
		for (int y = 0; y < height; ++y)
			std::copy_n(pixels.constData() + y * width, width, reinterpret_cast<QRgb *>(image.scanLine(y)));
	}
	if (image.isNull())
		return fail(error, Text::tr("Unable to allocate the transferred skin. Choose a smaller size."));
	if (!work.check())
		return false;
	*result = std::move(image);
	if (report)
		*report = std::move(summary);
	return true;
}

QJsonObject modelSkinTransferJson(const ModelSkinTransferReport &report)
{
	QJsonArray missing, collapsed, projected;
	for (int face : report.missingFaces)
		missing.append(face);
	for (int face : report.collapsedFaces)
		collapsed.append(face);
	for (int face : report.projectedFaces)
		projected.append(face);
	return {{"width", report.size.width()},
			{"height", report.size.height()},
			{"faces", report.faces},
			{"texels", report.texels},
			{"paddedTexels", report.paddedTexels},
			{"indexed", report.indexed},
			{"missingFaces", missing},
			{"collapsedFaces", collapsed},
			{"projectedFaces", projected},
			{"maxProjectionDistance", report.maxProjectionDistance}};
}
} // namespace vibestudio
