#include "core/model_skin_paint.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {

namespace {

constexpr double kPi = 3.14159265358979323846;

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

int nearestPaletteIndex(const QImage& image, const QColor& colour)
{
	const QVector<QRgb> table = image.colorTable();
	int best = 0;
	long long bestDistance = std::numeric_limits<long long>::max();
	for (int i = 0; i < table.size(); ++i) {
		const long long dr = qRed(table.at(i)) - colour.red(), dg = qGreen(table.at(i)) - colour.green(), db = qBlue(table.at(i)) - colour.blue();
		const long long distance = dr * dr + dg * dg + db * db;
		if (distance < bestDistance) {
			bestDistance = distance;
			best = i;
		}
	}
	return best;
}

} // namespace

QPointF ModelSkinPaintView::project(const ModelVec3& p, double* depthOut) const
{
	const double x = matrix[0] * p.x + matrix[1] * p.y + matrix[2] * p.z + matrix[3];
	const double y = matrix[4] * p.x + matrix[5] * p.y + matrix[6] * p.z + matrix[7];
	if (depthOut) {
		*depthOut = matrix[8] * p.x + matrix[9] * p.y + matrix[10] * p.z + matrix[11];
	}
	return {x, y};
}

bool buildModelSkinPaintView(const ModelMesh& mesh, int frame, const ModelSkinPaintCamera& camera, ModelSkinPaintView* view, QString* error,
	const ModelWorkControl& control)
{
	if (!view || camera.size.width() < 16 || camera.size.height() < 16 || camera.size.width() > 4096 || camera.size.height() > 4096
		|| !std::isfinite(camera.yaw) || !std::isfinite(camera.pitch) || !std::isfinite(camera.zoom) || camera.zoom <= 0.0) {
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "The paint view needs a size of 16 to 4096 pixels and a positive zoom."));
	}
	if (frame < 0 || frame >= mesh.frames.size()) {
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "Frame %1 does not exist.").arg(frame));
	}
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
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
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "The model has no geometry in frame %1.").arg(frame));
	}
	const Vec centre{(low.x + high.x) / 2, (low.y + high.y) / 2, (low.z + high.z) / 2};
	const Vec half{(high.x - low.x) / 2, (high.y - low.y) / 2, (high.z - low.z) / 2};
	const double radius = std::max(1e-3, std::sqrt(dot(half, half)));
	const double yaw = camera.yaw * kPi / 180.0, pitch = camera.pitch * kPi / 180.0;
	const Vec eye{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
	const Vec right = normalized(cross({0, 0, 1}, eye), {0, 1, 0});
	const Vec up = cross(eye, right);
	const int width = camera.size.width(), height = camera.size.height();
	const double scale = std::min(width, height) * 0.45 * camera.zoom / radius;
	ModelSkinPaintView result;
	result.size = camera.size;
	result.matrix = {scale * right.x, scale * right.y, scale * right.z, width / 2.0 - scale * dot(right, centre),
		-scale * up.x, -scale * up.y, -scale * up.z, height / 2.0 + scale * dot(up, centre),
		eye.x, eye.y, eye.z, -dot(eye, centre)};
	result.tolerance = radius * 0.01;
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
			double z[3];
			const QPointF p[3] = {result.project(positions.at(tri.a), &z[0]), result.project(positions.at(tri.b), &z[1]), result.project(positions.at(tri.c), &z[2])};
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
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	const bool indexed = skin->format() == QImage::Format_Indexed8;
	QImage target = indexed ? *skin : skin->convertToFormat(QImage::Format_ARGB32);
	if (indexed && target.colorCount() == 0) {
		return fail(error, QCoreApplication::translate("VibeStudioModelSkinPaint", "The indexed skin has no palette."));
	}
	const int width = target.width(), height = target.height();
	const ModelSurface& surface = mesh.surfaces.at(surfaceIndex);
	const auto& positions = surface.frames.at(frame).positions;
	QHash<QPair<int, int>, int> idOf;
	for (int id = 0; id < view.triangles.size(); ++id) {
		idOf.insert(view.triangles.at(id), id);
	}
	// The stroke's reach in view pixels, for a quick rejection.
	double reachLow[2] = {1e30, 1e30}, reachHigh[2] = {-1e30, -1e30};
	for (const QPointF& sample : stroke.samples) {
		reachLow[0] = std::min(reachLow[0], sample.x() - brush.radius);
		reachLow[1] = std::min(reachLow[1], sample.y() - brush.radius);
		reachHigh[0] = std::max(reachHigh[0], sample.x() + brush.radius);
		reachHigh[1] = std::max(reachHigh[1], sample.y() + brush.radius);
	}
	const auto visible = [&](const ModelVec3& p, int id, QPointF* screen) {
		double depth = 0;
		*screen = view.project(p, &depth);
		const int x = int(std::floor(screen->x())), y = int(std::floor(screen->y()));
		if (x < 0 || y < 0 || x >= view.size.width() || y >= view.size.height()) {
			return false;
		}
		const qsizetype at = qsizetype(y) * view.size.width() + x;
		const int shown = view.ids.at(at);
		return shown >= 0 && (shown == id || depth >= double(view.depth.at(at)) - view.tolerance);
	};
	const auto coverageAt = [&](const QPointF& screen) {
		if (screen.x() < reachLow[0] || screen.x() > reachHigh[0] || screen.y() < reachLow[1] || screen.y() > reachHigh[1]) {
			return 0.0;
		}
		double best = 0.0;
		for (const QPointF& sample : stroke.samples) {
			best = std::max(best, brushWeight(brush, std::hypot(sample.x() - screen.x(), sample.y() - screen.y())));
		}
		return best;
	};
	QVector<float> coverage(qsizetype(width) * height, 0.0f);
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
				footprint[at] = true;
				const ModelVec3& a = positions.at(tri.a);
				const ModelVec3& b = positions.at(tri.b);
				const ModelVec3& c = positions.at(tri.c);
				const ModelVec3 p{float(w0 * a.x + w1 * b.x + w2 * c.x), float(w0 * a.y + w1 * b.y + w2 * c.y), float(w0 * a.z + w1 * b.z + w2 * c.z)};
				QPointF screen;
				double weight = visible(p, id, &screen) ? coverageAt(screen) : 0.0;
				if (brush.mirrorAxis >= 0) {
					ModelVec3 mirrored = p;
					(brush.mirrorAxis == 0 ? mirrored.x : brush.mirrorAxis == 1 ? mirrored.y : mirrored.z) *= -1.0f;
					if (visible(mirrored, -2, &screen)) {
						weight = std::max(weight, coverageAt(screen));
					}
				}
				coverage[at] = std::max(coverage.at(at), float(weight));
			}
		}
	}
	const int index = indexed ? (brush.paletteIndex >= 0 && brush.paletteIndex < target.colorCount() ? brush.paletteIndex : nearestPaletteIndex(target, brush.colour)) : -1;
	QVector<bool> painted(coverage.size(), false);
	QRect dirty;
	int texels = 0;
	const QRgb colour = brush.colour.rgb();
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const qsizetype at = qsizetype(y) * width + x;
			const double amount = coverage.at(at);
			if (amount <= 0.0) {
				continue;
			}
			if (indexed || brush.mode == ModelSkinPaintMode::Replace) {
				if (amount < 0.5) {
					continue;
				}
				if (indexed) {
					target.setPixel(x, y, uint(index));
				} else {
					target.setPixel(x, y, qRgba(qRed(colour), qGreen(colour), qBlue(colour), 255));
				}
			} else {
				const QRgb old = target.pixel(x, y);
				const auto mix = [amount](int from, int to) { return int(std::lround(from + (to - from) * amount)); };
				target.setPixel(x, y, qRgba(mix(qRed(old), qRed(colour)), mix(qGreen(old), qGreen(colour)), mix(qBlue(old), qBlue(colour)), mix(qAlpha(old), 255)));
			}
			painted[at] = true;
			dirty = dirty.united(QRect(x, y, 1, 1));
			++texels;
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

QImage renderModelSkinPaintPreview(const ModelMesh& mesh, int frame, const ModelSkinPaintView& view, const QHash<int, QImage>& skins, const QColor& background)
{
	QImage image(view.size, QImage::Format_ARGB32);
	image.fill(background);
	if (view.ids.size() != qsizetype(view.size.width()) * view.size.height()) {
		return image;
	}
	const Vec eye = normalized({view.matrix[8], view.matrix[9], view.matrix[10]}, {0, 0, 1});
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
			const QPointF a = view.project(positions.at(tri.a)), b = view.project(positions.at(tri.b)), c = view.project(positions.at(tri.c));
			double w0 = 0, w1 = 0, w2 = 0;
			barycentric(a, b, c, x + 0.5, y + 0.5, &w0, &w1, &w2);
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

bool parseModelSkinPaintStrokes(const QJsonObject& object, ModelSkinPaintCamera* camera, QVector<ModelSkinPaintStroke>* strokes, QString* error)
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
		if (mode != QStringLiteral("mix") && mode != QStringLiteral("replace")) {
			return malformed(QStringLiteral("mode"));
		}
		brush.mode = mode == QStringLiteral("replace") ? ModelSkinPaintMode::Replace : ModelSkinPaintMode::Mix;
		const QString mirror = entry.value(QStringLiteral("mirror")).toString();
		brush.mirrorAxis = mirror == QStringLiteral("x") ? 0 : mirror == QStringLiteral("y") ? 1 : mirror == QStringLiteral("z") ? 2 : -1;
		if (!mirror.isEmpty() && brush.mirrorAxis < 0) {
			return malformed(QStringLiteral("mirror"));
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
		QJsonObject entry{{QStringLiteral("colour"), stroke.brush.colour.name()}, {QStringLiteral("radius"), stroke.brush.radius},
			{QStringLiteral("opacity"), stroke.brush.opacity}, {QStringLiteral("hardness"), stroke.brush.hardness},
			{QStringLiteral("padding"), stroke.brush.padding},
			{QStringLiteral("mode"), stroke.brush.mode == ModelSkinPaintMode::Replace ? QStringLiteral("replace") : QStringLiteral("mix")},
			{QStringLiteral("samples"), samples}};
		if (stroke.brush.paletteIndex >= 0) {
			entry.insert(QStringLiteral("index"), stroke.brush.paletteIndex);
		}
		if (stroke.brush.mirrorAxis >= 0) {
			entry.insert(QStringLiteral("mirror"), QString(QLatin1Char("xyz"[stroke.brush.mirrorAxis])));
		}
		list.append(entry);
	}
	return QJsonObject{{QStringLiteral("camera"), QJsonObject{{QStringLiteral("yaw"), camera.yaw}, {QStringLiteral("pitch"), camera.pitch},
										  {QStringLiteral("zoom"), camera.zoom}, {QStringLiteral("width"), camera.size.width()},
										  {QStringLiteral("height"), camera.size.height()}}},
		{QStringLiteral("strokes"), list}};
}

} // namespace vibestudio
