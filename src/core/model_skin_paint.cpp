#include "core/model_skin_paint.h"

#include "core/model_document.h"
#include "core/package_archive.h"
#include "core/package_staging.h"
#include "core/texture_export.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImageWriter>
#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxStampDimension = 1024;

struct Vec {
	double x = 0, y = 0, z = 0;
};
double dot(Vec a, Vec b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec cross(Vec a, Vec b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec normalized(Vec a, Vec fallback)
{
	const double length = std::sqrt(dot(a, a));
	return length > 1e-12 ? Vec{a.x / length, a.y / length, a.z / length} : fallback;
}

bool fail(QString* error, const QString& message)
{
	if (error) {
		*error = message;
	}
	return false;
}

// Barycentric weights of (x, y) in a screen triangle, or false outside it.
bool barycentric(const QPointF& a, const QPointF& b, const QPointF& c, double x, double y, double* w0, double* w1, double* w2)
{
	const double area = (b.x() - a.x()) * (c.y() - a.y()) - (c.x() - a.x()) * (b.y() - a.y());
	if (std::abs(area) < 1e-12) {
		return false;
	}
	*w0 = ((b.x() - x) * (c.y() - y) - (c.x() - x) * (b.y() - y)) / area;
	*w1 = ((c.x() - x) * (a.y() - y) - (a.x() - x) * (c.y() - y)) / area;
	*w2 = 1.0 - *w0 - *w1;
	return *w0 >= -1e-9 && *w1 >= -1e-9 && *w2 >= -1e-9;
}

double brushWeight(const ModelSkinBrush& brush, double distance)
{
	if (distance >= brush.radius) {
		return 0.0;
	}
	const double t = distance / brush.radius;
	const double hard = std::clamp(brush.hardness, 0.0, 1.0);
	const double weight = t <= hard || hard >= 1.0 ? 1.0 : 1.0 - (t - hard) / (1.0 - hard);
	return std::clamp(weight * brush.opacity, 0.0, 1.0);
}

int nearestPaletteIndex(const QVector<QRgb>& table, QRgb colour)
{
	int best = 0;
	long long bestDistance = std::numeric_limits<long long>::max();
	for (int i = 0; i < table.size(); ++i) {
		const long long dr = qRed(table.at(i)) - qRed(colour), dg = qGreen(table.at(i)) - qGreen(colour), db = qBlue(table.at(i)) - qBlue(colour);
		const long long distance = dr * dr + dg * dg + db * db;
		if (distance < bestDistance) {
			bestDistance = distance;
			best = i;
		}
	}
	return best;
}

struct Bounds {
	Vec centre;
	double radius = 0.0;
};

bool frameBounds(const ModelMesh& mesh, int frame, Bounds* bounds)
{
	Vec low{1e30, 1e30, 1e30}, high{-1e30, -1e30, -1e30};
	for (const ModelSurface& surface : mesh.surfaces) {
		if (frame >= surface.frames.size()) {
			continue;
		}
		for (const ModelVec3& p : surface.frames.at(frame).positions) {
			low = {std::min(low.x, double(p.x)), std::min(low.y, double(p.y)), std::min(low.z, double(p.z))};
			high = {std::max(high.x, double(p.x)), std::max(high.y, double(p.y)), std::max(high.z, double(p.z))};
		}
	}
	if (low.x > high.x) {
		return false;
	}
	const Vec half{(high.x - low.x) / 2, (high.y - low.y) / 2, (high.z - low.z) / 2};
	bounds->centre = {(low.x + high.x) / 2, (low.y + high.y) / 2, (low.z + high.z) / 2};
	bounds->radius = std::max(1e-3, std::sqrt(dot(half, half)));
	return true;
}

bool validSize(const QSize& size)
{
	return size.width() >= 16 && size.height() >= 16 && size.width() <= 4096 && size.height() <= 4096;
}

// The stamp's colour at an offset from the brush centre, transparent outside.
QRgb stampAt(const QImage& stamp, double radius, const QPointF& offset)
{
	const double u = offset.x() / (2.0 * radius) + 0.5, v = offset.y() / (2.0 * radius) + 0.5;
	if (u < 0.0 || v < 0.0 || u >= 1.0 || v >= 1.0) {
		return 0;
	}
	const int x = std::clamp(int(u * stamp.width()), 0, stamp.width() - 1), y = std::clamp(int(v * stamp.height()), 0, stamp.height() - 1);
	return reinterpret_cast<const QRgb*>(stamp.constScanLine(y))[x];
}

QJsonArray matrixJson(const std::array<double, 16>& matrix)
{
	QJsonArray list;
	for (const double value : matrix) {
		list.append(value);
	}
	return list;
}

} // namespace

QPointF ModelSkinPaintView::project(const ModelVec3& p, double* depthOut, double* wOut) const
{
	const auto& m = projection.matrix;
	const double u = m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3];
	const double v = m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7];
	const double z = m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11];
	const double w = m[12] * p.x + m[13] * p.y + m[14] * p.z + m[15];
	if (wOut) {
		*wOut = w;
	}
	if (std::abs(w) < 1e-12) {
		if (depthOut) {
			*depthOut = -std::numeric_limits<double>::max();
		}
		return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
	}
	if (depthOut) {
		*depthOut = z / w;
	}
	return {u / w, v / w};
}

bool ModelSkinPaintView::depthVisible(double a, double b) const
{
	if (!projection.perspective) {
		return a >= b - tolerance;
	}
	// Perspective depth is 1/distance: compare the distances themselves.
	return a > 0.0 && b > 0.0 && 1.0 / a <= 1.0 / b + tolerance;
}

bool modelSkinPaintOrbitProjection(const ModelMesh& mesh, int frame, const ModelSkinPaintCamera& camera, ModelSkinPaintProjection* projection, QString* error)
{
	if (!projection || !validSize(camera.size) || !std::isfinite(camera.yaw) || !std::isfinite(camera.pitch) || !std::isfinite(camera.zoom)
		|| camera.zoom <= 0.0) {
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "The paint view needs a size of 16 to 4096 pixels and a positive zoom."));
	}
	if (frame < 0 || frame >= mesh.frames.size()) {
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "Frame %1 does not exist.").arg(frame));
	}
	Bounds bounds;
	if (!frameBounds(mesh, frame, &bounds)) {
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "The model has no geometry in frame %1.").arg(frame));
	}
	const Vec centre = bounds.centre;
	const double yaw = camera.yaw * kPi / 180.0, pitch = camera.pitch * kPi / 180.0;
	const Vec eye{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
	const Vec right = normalized(cross({0, 0, 1}, eye), {0, 1, 0});
	const Vec up = cross(eye, right);
	const int width = camera.size.width(), height = camera.size.height();
	const double scale = std::min(width, height) * 0.45 * camera.zoom / bounds.radius;
	ModelSkinPaintProjection result;
	result.size = camera.size;
	result.perspective = false;
	result.eye = {float(eye.x), float(eye.y), float(eye.z)};
	result.matrix = {scale * right.x, scale * right.y, scale * right.z, width / 2.0 - scale * dot(right, centre),
		-scale * up.x, -scale * up.y, -scale * up.z, height / 2.0 + scale * dot(up, centre),
		eye.x, eye.y, eye.z, -dot(eye, centre),
		0.0, 0.0, 0.0, 1.0};
	*projection = result;
	return true;
}

bool buildModelSkinPaintView(const ModelMesh& mesh, int frame, const ModelSkinPaintCamera& camera, ModelSkinPaintView* view, QString* error,
	const ModelWorkControl& control)
{
	ModelSkinPaintProjection projection;
	return modelSkinPaintOrbitProjection(mesh, frame, camera, &projection, error) && buildModelSkinPaintView(mesh, frame, projection, view, error, control);
}

bool buildModelSkinPaintView(const ModelMesh& mesh, int frame, const ModelSkinPaintProjection& projection, ModelSkinPaintView* view, QString* error,
	const ModelWorkControl& control)
{
	bool finite = true;
	for (const double value : projection.matrix) {
		finite = finite && std::isfinite(value);
	}
	if (!view || !validSize(projection.size) || !finite) {
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "The paint view needs a size of 16 to 4096 pixels and a finite projection."));
	}
	if (frame < 0 || frame >= mesh.frames.size()) {
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "Frame %1 does not exist.").arg(frame));
	}
	Bounds bounds;
	if (!frameBounds(mesh, frame, &bounds)) {
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "The model has no geometry in frame %1.").arg(frame));
	}
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	const int width = projection.size.width(), height = projection.size.height();
	ModelSkinPaintView result;
	result.size = projection.size;
	result.projection = projection;
	result.tolerance = bounds.radius * 0.01;
	result.ids = QVector<qint32>(qsizetype(width) * height, -1);
	result.depth = QVector<float>(qsizetype(width) * height, -std::numeric_limits<float>::max());
	for (int s = 0; s < mesh.surfaces.size(); ++s) {
		const ModelSurface& surface = mesh.surfaces.at(s);
		if (frame >= surface.frames.size()) {
			continue;
		}
		const auto& positions = surface.frames.at(frame).positions;
		for (int t = 0; t < surface.triangles.size(); ++t) {
			if (!work.step()) {
				return false;
			}
			const ModelTriangle& tri = surface.triangles.at(t);
			if (tri.a < 0 || tri.b < 0 || tri.c < 0 || tri.a >= positions.size() || tri.b >= positions.size() || tri.c >= positions.size()) {
				continue;
			}
			const int id = int(result.triangles.size());
			result.triangles.append({s, t});
			double z[3], w[3];
			const QPointF p[3] = {result.project(positions.at(tri.a), &z[0], &w[0]), result.project(positions.at(tri.b), &z[1], &w[1]),
				result.project(positions.at(tri.c), &z[2], &w[2])};
			// A triangle reaching behind a perspective camera is left out.
			if (w[0] <= 1e-6 || w[1] <= 1e-6 || w[2] <= 1e-6) {
				continue;
			}
			const int x0 = std::max(0, int(std::floor(std::min({p[0].x(), p[1].x(), p[2].x()}))));
			const int x1 = std::min(width - 1, int(std::ceil(std::max({p[0].x(), p[1].x(), p[2].x()}))));
			const int y0 = std::max(0, int(std::floor(std::min({p[0].y(), p[1].y(), p[2].y()}))));
			const int y1 = std::min(height - 1, int(std::ceil(std::max({p[0].y(), p[1].y(), p[2].y()}))));
			for (int y = y0; y <= y1; ++y) {
				for (int x = x0; x <= x1; ++x) {
					double w0 = 0, w1 = 0, w2 = 0;
					if (!barycentric(p[0], p[1], p[2], x + 0.5, y + 0.5, &w0, &w1, &w2)) {
						continue;
					}
					const float depth = float(w0 * z[0] + w1 * z[1] + w2 * z[2]);
					const qsizetype at = qsizetype(y) * width + x;
					if (depth > result.depth.at(at)) {
						result.depth[at] = depth;
						result.ids[at] = id;
					}
				}
			}
		}
	}
	if (!work.check()) {
		return false;
	}
	*view = std::move(result);
	return true;
}

bool paintModelSkinStroke(const ModelMesh& mesh, int surfaceIndex, int frame, const ModelSkinPaintView& view, const ModelSkinPaintStroke& stroke,
	QImage* skin, ModelSkinPaintResult* result, QString* error, const ModelWorkControl& control)
{
	if (!skin || skin->isNull()) {
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "Painting needs a skin image."));
	}
	if (surfaceIndex < 0 || surfaceIndex >= mesh.surfaces.size() || frame < 0 || frame >= mesh.surfaces.at(surfaceIndex).frames.size()) {
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "Surface %1 has no frame %2 to paint.").arg(surfaceIndex).arg(frame));
	}
	const ModelSkinBrush& brush = stroke.brush;
	if (!std::isfinite(brush.radius) || brush.radius <= 0.0 || brush.radius > 4096.0 || !std::isfinite(brush.opacity) || brush.opacity < 0.0
		|| brush.opacity > 1.0 || brush.mirrorAxis < -1 || brush.mirrorAxis > 2 || brush.padding < 0 || brush.padding > 16
		|| view.ids.size() != qsizetype(view.size.width()) * view.size.height()) {
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint",
			"A brush needs a radius of up to 4096 pixels, an opacity of 0 to 1, a mirror axis of none, X, Y or Z and up to 16 texels of padding."));
	}
	if (!brush.stamp.isNull() && (brush.stamp.width() > kMaxStampDimension || brush.stamp.height() > kMaxStampDimension)) {
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "A stamp image may be up to %1 pixels on a side.").arg(kMaxStampDimension));
	}
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	const bool indexed = skin->format() == QImage::Format_Indexed8;
	QImage target = indexed ? *skin : skin->convertToFormat(QImage::Format_ARGB32);
	if (indexed && target.colorCount() == 0) {
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "The indexed skin has no palette."));
	}
	const QVector<QRgb> table = indexed ? target.colorTable() : QVector<QRgb>();
	const int width = target.width(), height = target.height();
	const ModelSurface& surface = mesh.surfaces.at(surfaceIndex);
	const auto& positions = surface.frames.at(frame).positions;
	QHash<QPair<int, int>, int> idOf;
	for (int id = 0; id < view.triangles.size(); ++id) {
		idOf.insert(view.triangles.at(id), id);
	}
	const int viewWidth = view.size.width(), viewHeight = view.size.height();
	const auto visible = [&](const ModelVec3& p, int id, QPointF* screen) {
		double depth = 0, w = 0;
		*screen = view.project(p, &depth, &w);
		if (w <= 1e-6 || !std::isfinite(screen->x()) || !std::isfinite(screen->y())) {
			return false;
		}
		const int x = int(std::floor(screen->x())), y = int(std::floor(screen->y()));
		if (x < 0 || y < 0 || x >= viewWidth || y >= viewHeight) {
			return false;
		}
		const qsizetype at = qsizetype(y) * viewWidth + x;
		const int shown = view.ids.at(at);
		return shown >= 0 && (shown == id || view.depthVisible(depth, double(view.depth.at(at))));
	};
	// Where each texel of the footprint shows in the view, and its mirror.
	struct Hit {
		qsizetype at = 0;
		int x = 0, y = 0;
		bool shown = false, mirrorShown = false;
		QPointF screen, mirrorScreen;
	};
	QVector<Hit> hits;
	QVector<bool> footprint(qsizetype(width) * height, false);
	for (int t = 0; t < surface.triangles.size(); ++t) {
		if (!work.step()) {
			return false;
		}
		const ModelTriangle& tri = surface.triangles.at(t);
		const int corner[3] = {tri.a, tri.b, tri.c};
		bool valid = true;
		for (const int c : corner) {
			valid = valid && c >= 0 && c < positions.size() && c < surface.texCoords.size();
		}
		if (!valid) {
			continue;
		}
		const QPointF uv[3] = {{surface.texCoords.at(tri.a).u * double(width), surface.texCoords.at(tri.a).v * double(height)},
			{surface.texCoords.at(tri.b).u * double(width), surface.texCoords.at(tri.b).v * double(height)},
			{surface.texCoords.at(tri.c).u * double(width), surface.texCoords.at(tri.c).v * double(height)}};
		const int id = idOf.value({surfaceIndex, t}, -1);
		const int x0 = std::max(0, int(std::floor(std::min({uv[0].x(), uv[1].x(), uv[2].x()}) - 0.5)));
		const int x1 = std::min(width - 1, int(std::ceil(std::max({uv[0].x(), uv[1].x(), uv[2].x()}) - 0.5)));
		const int y0 = std::max(0, int(std::floor(std::min({uv[0].y(), uv[1].y(), uv[2].y()}) - 0.5)));
		const int y1 = std::min(height - 1, int(std::ceil(std::max({uv[0].y(), uv[1].y(), uv[2].y()}) - 0.5)));
		for (int y = y0; y <= y1; ++y) {
			for (int x = x0; x <= x1; ++x) {
				double w0 = 0, w1 = 0, w2 = 0;
				if (!barycentric(uv[0], uv[1], uv[2], x + 0.5, y + 0.5, &w0, &w1, &w2)) {
					continue;
				}
				const qsizetype at = qsizetype(y) * width + x;
				if (footprint.at(at)) {
					continue;
				}
				footprint[at] = true;
				const ModelVec3& a = positions.at(tri.a);
				const ModelVec3& b = positions.at(tri.b);
				const ModelVec3& c = positions.at(tri.c);
				const ModelVec3 p{float(w0 * a.x + w1 * b.x + w2 * c.x), float(w0 * a.y + w1 * b.y + w2 * c.y), float(w0 * a.z + w1 * b.z + w2 * c.z)};
				Hit hit;
				hit.at = at;
				hit.x = x;
				hit.y = y;
				hit.shown = visible(p, id, &hit.screen);
				if (brush.mirrorAxis >= 0 && brush.mode != ModelSkinPaintMode::Smudge) {
					ModelVec3 mirrored = p;
					(brush.mirrorAxis == 0 ? mirrored.x : brush.mirrorAxis == 1 ? mirrored.y : mirrored.z) *= -1.0f;
					hit.mirrorShown = visible(mirrored, -2, &hit.mirrorScreen);
				}
				if (hit.shown || hit.mirrorShown) {
					hits.append(hit);
				}
			}
		}
	}
	QVector<bool> painted(footprint.size(), false);
	QRect dirty;
	int texels = 0;
	const auto mark = [&](const Hit& hit) {
		if (!painted.at(hit.at)) {
			painted[hit.at] = true;
			dirty = dirty.united(QRect(hit.x, hit.y, 1, 1));
			++texels;
		}
	};
	const auto mixRgb = [](QRgb from, QRgb to, double amount) {
		const auto mix = [amount](int a, int b) { return int(std::lround(a + (b - a) * amount)); };
		return qRgba(mix(qRed(from), qRed(to)), mix(qGreen(from), qGreen(to)), mix(qBlue(from), qBlue(to)), mix(qAlpha(from), qAlpha(to)));
	};
	if (brush.mode == ModelSkinPaintMode::Smudge) {
		// The texel of this surface shown at a view point, from `source`.
		const auto texelAt = [&](const QPointF& point, int* tx, int* ty) {
			const int px = int(std::floor(point.x())), py = int(std::floor(point.y()));
			if (px < 0 || py < 0 || px >= viewWidth || py >= viewHeight) {
				return false;
			}
			const int id = view.ids.at(qsizetype(py) * viewWidth + px);
			if (id < 0 || view.triangles.at(id).first != surfaceIndex) {
				return false;
			}
			const ModelTriangle& tri = surface.triangles.at(view.triangles.at(id).second);
			if (tri.a >= surface.texCoords.size() || tri.b >= surface.texCoords.size() || tri.c >= surface.texCoords.size()) {
				return false;
			}
			double wa = 1, wb = 1, wc = 1;
			const QPointF a = view.project(positions.at(tri.a), nullptr, &wa), b = view.project(positions.at(tri.b), nullptr, &wb),
						  c = view.project(positions.at(tri.c), nullptr, &wc);
			double w0 = 0, w1 = 0, w2 = 0;
			barycentric(a, b, c, point.x(), point.y(), &w0, &w1, &w2);
			if (view.projection.perspective && wa > 0 && wb > 0 && wc > 0) {
				// Perspective-correct: weights over W, renormalised.
				w0 /= wa;
				w1 /= wb;
				w2 /= wc;
				const double sum = w0 + w1 + w2;
				if (std::abs(sum) < 1e-12) {
					return false;
				}
				w0 /= sum;
				w1 /= sum;
				w2 /= sum;
			}
			const double u = w0 * surface.texCoords.at(tri.a).u + w1 * surface.texCoords.at(tri.b).u + w2 * surface.texCoords.at(tri.c).u;
			const double v = w0 * surface.texCoords.at(tri.a).v + w1 * surface.texCoords.at(tri.b).v + w2 * surface.texCoords.at(tri.c).v;
			*tx = std::clamp(int(std::floor(u * width)), 0, width - 1);
			*ty = std::clamp(int(std::floor(v * height)), 0, height - 1);
			return true;
		};
		for (int i = 1; i < stroke.samples.size(); ++i) {
			if (!work.check()) {
				return false;
			}
			const QPointF sample = stroke.samples.at(i), delta = sample - stroke.samples.at(i - 1);
			if (delta.isNull()) {
				continue;
			}
			const QImage source = target;
			for (const Hit& hit : std::as_const(hits)) {
				if (!hit.shown) {
					continue;
				}
				const double weight = brushWeight(brush, std::hypot(sample.x() - hit.screen.x(), sample.y() - hit.screen.y()));
				int tx = 0, ty = 0;
				if (weight <= 0.0 || !texelAt(hit.screen - delta, &tx, &ty)) {
					continue;
				}
				if (indexed) {
					if (weight >= 0.5 && target.pixelIndex(hit.x, hit.y) != source.pixelIndex(tx, ty)) {
						target.setPixel(hit.x, hit.y, uint(source.pixelIndex(tx, ty)));
						mark(hit);
					}
				} else {
					const QRgb before = target.pixel(hit.x, hit.y);
					const QRgb after = mixRgb(before, source.pixel(tx, ty), weight);
					if (after != before) {
						target.setPixel(hit.x, hit.y, after);
						mark(hit);
					}
				}
			}
		}
	} else {
		const QImage stamp = brush.stamp.isNull() ? QImage() : brush.stamp.convertToFormat(QImage::Format_ARGB32);
		const QRgb flat = qRgba(brush.colour.red(), brush.colour.green(), brush.colour.blue(), 255);
		double reachLow[2] = {1e30, 1e30}, reachHigh[2] = {-1e30, -1e30};
		for (const QPointF& sample : stroke.samples) {
			reachLow[0] = std::min(reachLow[0], sample.x() - brush.radius);
			reachLow[1] = std::min(reachLow[1], sample.y() - brush.radius);
			reachHigh[0] = std::max(reachHigh[0], sample.x() + brush.radius);
			reachHigh[1] = std::max(reachHigh[1], sample.y() + brush.radius);
		}
		// The strongest brush position over a view point: its coverage and
		// colour (the stamp's, when there is one).
		const auto coverageAt = [&](const QPointF& screen, QRgb* colour) {
			if (screen.x() < reachLow[0] || screen.x() > reachHigh[0] || screen.y() < reachLow[1] || screen.y() > reachHigh[1]) {
				return 0.0;
			}
			double best = 0.0;
			for (const QPointF& sample : stroke.samples) {
				double weight = brushWeight(brush, std::hypot(sample.x() - screen.x(), sample.y() - screen.y()));
				QRgb texel = flat;
				if (weight > 0.0 && !stamp.isNull()) {
					texel = stampAt(stamp, brush.radius, screen - sample);
					weight *= qAlpha(texel) / 255.0;
					texel = qRgba(qRed(texel), qGreen(texel), qBlue(texel), 255);
				}
				if (weight > best) {
					best = weight;
					*colour = texel;
				}
			}
			return best;
		};
		const int brushIndex = indexed ? (brush.paletteIndex >= 0 && brush.paletteIndex < table.size() ? brush.paletteIndex : nearestPaletteIndex(table, flat)) : -1;
		QHash<QRgb, int> nearest;
		for (const Hit& hit : std::as_const(hits)) {
			if (!work.step()) {
				return false;
			}
			QRgb colour = flat, mirrorColour = flat;
			double amount = hit.shown ? coverageAt(hit.screen, &colour) : 0.0;
			const double mirrored = hit.mirrorShown ? coverageAt(hit.mirrorScreen, &mirrorColour) : 0.0;
			if (mirrored > amount) {
				amount = mirrored;
				colour = mirrorColour;
			}
			if (amount <= 0.0) {
				continue;
			}
			if (indexed || brush.mode == ModelSkinPaintMode::Replace) {
				if (amount < 0.5) {
					continue;
				}
				if (indexed) {
					int index = brushIndex;
					if (!stamp.isNull()) {
						const auto known = nearest.constFind(colour);
						index = known != nearest.cend() ? known.value() : nearest.insert(colour, nearestPaletteIndex(table, colour)).value();
					}
					target.setPixel(hit.x, hit.y, uint(index));
				} else {
					target.setPixel(hit.x, hit.y, colour);
				}
			} else {
				target.setPixel(hit.x, hit.y, mixRgb(target.pixel(hit.x, hit.y), colour, amount));
			}
			mark(hit);
		}
	}
	// Bleed painted texels past the footprint's edge.
	for (int pass = 0; pass < brush.padding && texels > 0; ++pass) {
		if (!work.check()) {
			return false;
		}
		QVector<bool> next = painted;
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				const qsizetype at = qsizetype(y) * width + x;
				if (footprint.at(at) || painted.at(at)) {
					continue;
				}
				for (int d = 0; d < 8; ++d) {
					static const int dx[8] = {-1, 1, 0, 0, -1, 1, -1, 1}, dy[8] = {0, 0, -1, 1, -1, -1, 1, 1};
					const int nx = x + dx[d], ny = y + dy[d];
					if (nx < 0 || ny < 0 || nx >= width || ny >= height || !painted.at(qsizetype(ny) * width + nx)) {
						continue;
					}
					target.setPixel(x, y, indexed ? target.pixelIndex(nx, ny) : target.pixel(nx, ny));
					next[at] = true;
					dirty = dirty.united(QRect(x, y, 1, 1));
					break;
				}
			}
		}
		painted = std::move(next);
	}
	if (!work.check()) {
		return false;
	}
	*skin = target;
	if (result) {
		result->dirty = dirty;
		result->texels = texels;
	}
	return true;
}

QImage modelSurfaceIndexedSkin(const ModelMesh& mesh, int surface)
{
	const ModelEmbeddedSkin* embedded = modelEmbeddedSkinForSurface(mesh, surface);
	const QSize size = mesh.mdl.skinSize;
	if (!embedded || embedded->indexedFrames.isEmpty() || mesh.mdl.palette.size() < 768 || size.isEmpty()
		|| embedded->indexedFrames.first().size() != qsizetype(size.width()) * size.height()) {
		return {};
	}
	QImage skin(size, QImage::Format_Indexed8);
	QVector<QRgb> table;
	for (int i = 0; i < 256; ++i) {
		table << qRgb(quint8(mesh.mdl.palette[i * 3]), quint8(mesh.mdl.palette[i * 3 + 1]), quint8(mesh.mdl.palette[i * 3 + 2]));
	}
	skin.setColorTable(table);
	const QByteArray& pixels = embedded->indexedFrames.first();
	for (int y = 0; y < size.height(); ++y) {
		std::copy_n(pixels.constData() + qsizetype(y) * size.width(), size.width(), reinterpret_cast<char*>(skin.scanLine(y)));
	}
	return skin;
}

int modelSkinPaintMdlSlot(const ModelMesh& mesh, int surface)
{
	if (!mesh.mdl.enabled || modelSurfaceIndexedSkin(mesh, surface).isNull()) {
		return -1;
	}
	const ModelEmbeddedSkin* embedded = modelEmbeddedSkinForSurface(mesh, surface);
	return embedded ? int(embedded - mesh.embeddedSkins.constData()) : -1;
}

bool modelSkinPaintMdlEdit(const ModelMesh& mesh, int surface, const QImage& skin, ModelEdit* edit, QString* error)
{
	const int slot = modelSkinPaintMdlSlot(mesh, surface);
	if (!edit || slot < 0) {
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "The surface has no MDL skin to replace; save the painted skin instead."));
	}
	const QImage own = modelSurfaceIndexedSkin(mesh, surface);
	if (skin.format() != QImage::Format_Indexed8 || skin.size() != mesh.mdl.skinSize || skin.colorTable() != own.colorTable()) {
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint",
			"The painted skin no longer matches the model's skin size and palette, so it cannot replace it."));
	}
	ModelEdit result;
	result.kind = ModelEditKind::ReplaceMdlSkinMember;
	result.mdlSkinSlot = slot;
	result.mdlSkinMember = 0;
	result.mdlSkin.size = skin.size();
	result.mdlSkin.palette = mesh.mdl.palette;
	result.mdlSkin.paletteGenerated = mesh.mdl.paletteGenerated;
	result.mdlSkin.pixels.resize(qsizetype(skin.width()) * skin.height());
	for (int y = 0; y < skin.height(); ++y) {
		std::copy_n(reinterpret_cast<const char*>(skin.constScanLine(y)), skin.width(), result.mdlSkin.pixels.data() + qsizetype(y) * skin.width());
	}
	*edit = result;
	return true;
}

bool encodeModelPaintedSkin(const QImage& skin, const QString& path, QByteArray* bytes, QString* error)
{
	if (!bytes || skin.isNull()) {
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "There is no painted skin to write."));
	}
	const QString suffix = QFileInfo(path).suffix().toLower();
	if (suffix == QStringLiteral("png") || suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg")) {
		QBuffer buffer(bytes);
		buffer.open(QIODevice::WriteOnly);
		QImageWriter writer(&buffer, suffix == QStringLiteral("png") ? QByteArrayLiteral("PNG") : QByteArrayLiteral("JPG"));
		if (suffix != QStringLiteral("png")) {
			writer.setQuality(95);
		}
		const QImage image = suffix == QStringLiteral("png") ? skin : skin.convertToFormat(QImage::Format_RGB32);
		if (!writer.write(image)) {
			return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "The painted skin could not be encoded as %1: %2").arg(suffix, writer.errorString()));
		}
		return true;
	}
	TextureExportOptions options;
	IdTechPaletteResolution palette;
	if (suffix == QStringLiteral("tga")) {
		options.format = TextureExportFormat::Targa;
	} else if (suffix == QStringLiteral("pcx")) {
		if (skin.format() != QImage::Format_Indexed8) {
			return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "A PCX skin is written from an indexed skin; this one holds colours."));
		}
		options.format = TextureExportFormat::Pcx;
		palette.palette.id = QStringLiteral("painted");
		palette.palette.displayName = palette.palette.id;
		palette.palette.colors = skin.colorTable();
		while (palette.palette.colors.size() < 256) {
			palette.palette.colors.append(qRgb(0, 0, 0));
		}
	} else {
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "Painted skins are written as PNG, TGA, JPEG or PCX, not .%1.").arg(suffix));
	}
	const TextureExportResult encoded = encodeTextureExport(options.format == TextureExportFormat::Pcx ? skin : skin.convertToFormat(QImage::Format_ARGB32), options, palette);
	if (!encoded.succeeded) {
		return fail(error, encoded.error);
	}
	*bytes = encoded.bytes;
	return true;
}

bool stageModelPaintedSkin(const QByteArray& bytes, const QString& virtualPath, PackageStagingModel* staging, QString* error)
{
	if (!staging || !staging->isLoaded() || staging->sourceFormat() == PackageArchiveFormat::Wad) {
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "Open a folder, PAK, ZIP or PK3 package before staging a painted skin."));
	}
	const auto normalized = normalizePackageVirtualPath(virtualPath, false);
	if (bytes.isEmpty() || !normalized.isSafe() || normalized.normalizedPath.isEmpty()) {
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "The painted skin needs a safe package path."));
	}
	auto staged = *staging;
	if (!staged.addBytes(bytes, normalized.normalizedPath, error, PackageStageConflictResolution::ReplaceExisting)) {
		return false;
	}
	*staging = std::move(staged);
	return true;
}

QImage renderModelSkinPaintPreview(const ModelMesh& mesh, int frame, const ModelSkinPaintView& view, const QHash<int, QImage>& skins, const QColor& background)
{
	QImage image(view.size, QImage::Format_ARGB32);
	image.fill(background);
	if (view.ids.size() != qsizetype(view.size.width()) * view.size.height()) {
		return image;
	}
	const Vec eye = normalized({view.projection.eye.x, view.projection.eye.y, view.projection.eye.z}, {0, 0, 1});
	QHash<int, QImage> converted;
	for (auto it = skins.cbegin(); it != skins.cend(); ++it) {
		converted.insert(it.key(), it.value().convertToFormat(QImage::Format_ARGB32));
	}
	for (int y = 0; y < view.size.height(); ++y) {
		auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (int x = 0; x < view.size.width(); ++x) {
			const int id = view.ids.at(qsizetype(y) * view.size.width() + x);
			if (id < 0) {
				continue;
			}
			const auto [s, t] = view.triangles.at(id);
			const ModelSurface& surface = mesh.surfaces.at(s);
			const ModelTriangle& tri = surface.triangles.at(t);
			const auto& positions = surface.frames.at(frame).positions;
			double wa = 1, wb = 1, wc = 1;
			const QPointF a = view.project(positions.at(tri.a), nullptr, &wa), b = view.project(positions.at(tri.b), nullptr, &wb),
						  c = view.project(positions.at(tri.c), nullptr, &wc);
			double w0 = 0, w1 = 0, w2 = 0;
			barycentric(a, b, c, x + 0.5, y + 0.5, &w0, &w1, &w2);
			if (view.projection.perspective && wa > 0 && wb > 0 && wc > 0) {
				w0 /= wa;
				w1 /= wb;
				w2 /= wc;
				const double sum = w0 + w1 + w2;
				if (std::abs(sum) > 1e-12) {
					w0 /= sum;
					w1 /= sum;
					w2 /= sum;
				}
			}
			const Vec n = normalized(cross(Vec{double(positions.at(tri.b).x) - positions.at(tri.a).x, double(positions.at(tri.b).y) - positions.at(tri.a).y,
											   double(positions.at(tri.b).z) - positions.at(tri.a).z},
										 Vec{double(positions.at(tri.c).x) - positions.at(tri.a).x, double(positions.at(tri.c).y) - positions.at(tri.a).y,
											 double(positions.at(tri.c).z) - positions.at(tri.a).z}),
				{0, 0, 1});
			const double shade = 0.45 + 0.55 * std::abs(dot(n, eye));
			QRgb texel = qRgb(170, 170, 170);
			const auto found = converted.constFind(s);
			if (found != converted.cend() && !found->isNull() && tri.a < surface.texCoords.size() && tri.b < surface.texCoords.size() && tri.c < surface.texCoords.size()) {
				const double u = w0 * surface.texCoords.at(tri.a).u + w1 * surface.texCoords.at(tri.b).u + w2 * surface.texCoords.at(tri.c).u;
				const double v = w0 * surface.texCoords.at(tri.a).v + w1 * surface.texCoords.at(tri.b).v + w2 * surface.texCoords.at(tri.c).v;
				const int tx = ((int(std::floor(u * found->width())) % found->width()) + found->width()) % found->width();
				const int ty = ((int(std::floor(v * found->height())) % found->height()) + found->height()) % found->height();
				texel = found->pixel(tx, ty);
			}
			line[x] = qRgb(int(qRed(texel) * shade), int(qGreen(texel) * shade), int(qBlue(texel) * shade));
		}
	}
	return image;
}

bool parseModelSkinPaintStrokes(const QJsonObject& object, ModelSkinPaintCamera* camera, QVector<ModelSkinPaintStroke>* strokes, QString* error,
	const QString& baseDirectory)
{
	const auto malformed = [error](const QString& detail) {
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "The strokes file is malformed: %1").arg(detail));
	};
	const QJsonObject view = object.value(QStringLiteral("camera")).toObject();
	ModelSkinPaintCamera parsedCamera;
	parsedCamera.yaw = view.value(QStringLiteral("yaw")).toDouble(parsedCamera.yaw);
	parsedCamera.pitch = view.value(QStringLiteral("pitch")).toDouble(parsedCamera.pitch);
	parsedCamera.zoom = view.value(QStringLiteral("zoom")).toDouble(parsedCamera.zoom);
	parsedCamera.size = QSize(view.value(QStringLiteral("width")).toInt(512), view.value(QStringLiteral("height")).toInt(512));
	const QJsonArray list = object.value(QStringLiteral("strokes")).toArray();
	if (!object.value(QStringLiteral("strokes")).isArray() || list.size() > 10000) {
		return malformed(QStringLiteral("strokes"));
	}
	QVector<ModelSkinPaintStroke> parsed;
	for (const QJsonValue& value : list) {
		const QJsonObject entry = value.toObject();
		ModelSkinPaintStroke stroke;
		ModelSkinBrush& brush = stroke.brush;
		if (entry.contains(QStringLiteral("colour"))) {
			brush.colour = QColor(entry.value(QStringLiteral("colour")).toString());
			if (!brush.colour.isValid()) {
				return malformed(QStringLiteral("colour"));
			}
		}
		brush.paletteIndex = entry.value(QStringLiteral("index")).toInt(-1);
		brush.radius = entry.value(QStringLiteral("radius")).toDouble(brush.radius);
		brush.opacity = entry.value(QStringLiteral("opacity")).toDouble(brush.opacity);
		brush.hardness = entry.value(QStringLiteral("hardness")).toDouble(brush.hardness);
		brush.padding = entry.value(QStringLiteral("padding")).toInt(brush.padding);
		const QString mode = entry.value(QStringLiteral("mode")).toString(QStringLiteral("mix"));
		if (mode != QStringLiteral("mix") && mode != QStringLiteral("replace") && mode != QStringLiteral("smudge")) {
			return malformed(QStringLiteral("mode"));
		}
		brush.mode = mode == QStringLiteral("replace") ? ModelSkinPaintMode::Replace
			: mode == QStringLiteral("smudge")		 ? ModelSkinPaintMode::Smudge
													   : ModelSkinPaintMode::Mix;
		const QString mirror = entry.value(QStringLiteral("mirror")).toString();
		brush.mirrorAxis = mirror == QStringLiteral("x") ? 0 : mirror == QStringLiteral("y") ? 1 : mirror == QStringLiteral("z") ? 2 : -1;
		if (!mirror.isEmpty() && brush.mirrorAxis < 0) {
			return malformed(QStringLiteral("mirror"));
		}
		if (entry.contains(QStringLiteral("stampPng"))) {
			brush.stamp = QImage::fromData(QByteArray::fromBase64(entry.value(QStringLiteral("stampPng")).toString().toLatin1()), "PNG");
			if (brush.stamp.isNull()) {
				return malformed(QStringLiteral("stampPng"));
			}
		} else if (entry.contains(QStringLiteral("stamp"))) {
			const QString path = entry.value(QStringLiteral("stamp")).toString();
			const QString resolved = QFileInfo(path).isAbsolute() || baseDirectory.isEmpty() ? path : QDir(baseDirectory).filePath(path);
			if (path.isEmpty() || !brush.stamp.load(resolved)) {
				return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "The stamp image cannot be read: %1").arg(QDir::toNativeSeparators(resolved)));
			}
		}
		if (!brush.stamp.isNull() && (brush.stamp.width() > kMaxStampDimension || brush.stamp.height() > kMaxStampDimension)) {
			return malformed(QStringLiteral("stamp"));
		}
		if (entry.contains(QStringLiteral("view"))) {
			const QJsonObject projection = entry.value(QStringLiteral("view")).toObject();
			const QJsonArray matrix = projection.value(QStringLiteral("matrix")).toArray();
			const QJsonArray eye = projection.value(QStringLiteral("eye")).toArray();
			if (matrix.size() != 16) {
				return malformed(QStringLiteral("view"));
			}
			for (int i = 0; i < 16; ++i) {
				if (!matrix.at(i).isDouble()) {
					return malformed(QStringLiteral("view"));
				}
				stroke.projection.matrix[size_t(i)] = matrix.at(i).toDouble();
			}
			stroke.projection.size = QSize(projection.value(QStringLiteral("width")).toInt(), projection.value(QStringLiteral("height")).toInt());
			stroke.projection.perspective = projection.value(QStringLiteral("perspective")).toBool();
			if (eye.size() == 3) {
				stroke.projection.eye = {float(eye.at(0).toDouble()), float(eye.at(1).toDouble()), float(eye.at(2).toDouble())};
			}
			if (!validSize(stroke.projection.size)) {
				return malformed(QStringLiteral("view"));
			}
			stroke.hasProjection = true;
		}
		const QJsonArray samples = entry.value(QStringLiteral("samples")).toArray();
		if (samples.isEmpty() || samples.size() > 100000) {
			return malformed(QStringLiteral("samples"));
		}
		for (const QJsonValue& sample : samples) {
			const QJsonArray point = sample.toArray();
			if (point.size() != 2 || !point.at(0).isDouble() || !point.at(1).isDouble()) {
				return malformed(QStringLiteral("samples"));
			}
			stroke.samples.append(QPointF(point.at(0).toDouble(), point.at(1).toDouble()));
		}
		parsed.append(stroke);
	}
	if (camera) {
		*camera = parsedCamera;
	}
	if (strokes) {
		*strokes = std::move(parsed);
	}
	return true;
}

QJsonObject modelSkinPaintStrokesJson(const ModelSkinPaintCamera& camera, const QVector<ModelSkinPaintStroke>& strokes)
{
	QJsonArray list;
	for (const ModelSkinPaintStroke& stroke : strokes) {
		QJsonArray samples;
		for (const QPointF& point : stroke.samples) {
			samples.append(QJsonArray{point.x(), point.y()});
		}
		const ModelSkinPaintMode mode = stroke.brush.mode;
		QJsonObject entry{{QStringLiteral("colour"), stroke.brush.colour.name()}, {QStringLiteral("radius"), stroke.brush.radius},
			{QStringLiteral("opacity"), stroke.brush.opacity}, {QStringLiteral("hardness"), stroke.brush.hardness},
			{QStringLiteral("padding"), stroke.brush.padding},
			{QStringLiteral("mode"), mode == ModelSkinPaintMode::Replace ? QStringLiteral("replace")
					: mode == ModelSkinPaintMode::Smudge					? QStringLiteral("smudge")
																			: QStringLiteral("mix")},
			{QStringLiteral("samples"), samples}};
		if (stroke.brush.paletteIndex >= 0) {
			entry.insert(QStringLiteral("index"), stroke.brush.paletteIndex);
		}
		if (stroke.brush.mirrorAxis >= 0) {
			entry.insert(QStringLiteral("mirror"), QString(QLatin1Char("xyz"[stroke.brush.mirrorAxis])));
		}
		if (!stroke.brush.stamp.isNull()) {
			QByteArray png;
			QBuffer buffer(&png);
			buffer.open(QIODevice::WriteOnly);
			stroke.brush.stamp.save(&buffer, "PNG");
			entry.insert(QStringLiteral("stampPng"), QString::fromLatin1(png.toBase64()));
		}
		if (stroke.hasProjection) {
			const ModelVec3& eye = stroke.projection.eye;
			entry.insert(QStringLiteral("view"), QJsonObject{{QStringLiteral("matrix"), matrixJson(stroke.projection.matrix)},
													 {QStringLiteral("perspective"), stroke.projection.perspective},
													 {QStringLiteral("width"), stroke.projection.size.width()},
													 {QStringLiteral("height"), stroke.projection.size.height()},
													 {QStringLiteral("eye"), QJsonArray{eye.x, eye.y, eye.z}}});
		}
		list.append(entry);
	}
	return QJsonObject{{QStringLiteral("camera"), QJsonObject{{QStringLiteral("yaw"), camera.yaw}, {QStringLiteral("pitch"), camera.pitch},
										  {QStringLiteral("zoom"), camera.zoom}, {QStringLiteral("width"), camera.size.width()},
										  {QStringLiteral("height"), camera.size.height()}}},
		{QStringLiteral("strokes"), list}};
}

} // namespace vibestudio
