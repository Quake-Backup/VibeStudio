#include "core/model_bake.h"

#include "core/model_bvh.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace vibestudio {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Vec {
	double x = 0, y = 0, z = 0;
};
Vec vec(const ModelVec3& v)
{
	return {v.x, v.y, v.z};
}
ModelVec3 model(const Vec& v)
{
	return {float(v.x), float(v.y), float(v.z)};
}
Vec add(Vec a, Vec b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vec sub(Vec a, Vec b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vec mul(Vec a, double s)
{
	return {a.x * s, a.y * s, a.z * s};
}
double dot(Vec a, Vec b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec cross(Vec a, Vec b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec normalized(Vec a, Vec fallback = {0, 0, 1})
{
	const double length = std::sqrt(dot(a, a));
	return length > 1e-12 && std::isfinite(length) ? mul(a, 1.0 / length) : fallback;
}

// SplitMix64 (Steele, Lea and Flood, "Fast Splittable Pseudorandom Number
// Generators", OOPSLA 2014) over the seed, texel and sample, so every texel's
// sequence is fixed whatever order texels are visited in.
double random01(quint32 seed, int x, int y, int sample)
{
	quint64 z = (quint64(seed) << 32) ^ (quint64(quint32(x)) * 0x9E3779B97F4A7C15ull) ^ (quint64(quint32(y)) << 20) ^ quint64(quint32(sample)) * 0xBF58476D1CE4E5B9ull;
	z += 0x9E3779B97F4A7C15ull;
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
	z ^= z >> 31;
	return double(z >> 11) * (1.0 / 9007199254740992.0);
}

struct Texel {
	bool covered = false;
	Vec position, normal, tangent, bitangent;
};

double radiusOf(const ModelMesh& mesh, int frame)
{
	Vec low{1e30, 1e30, 1e30}, high{-1e30, -1e30, -1e30};
	bool any = false;
	for (const ModelSurface& surface : mesh.surfaces) {
		if (frame < 0 || frame >= surface.frames.size()) {
			continue;
		}
		for (const ModelVec3& p : surface.frames.at(frame).positions) {
			low = {std::min(low.x, double(p.x)), std::min(low.y, double(p.y)), std::min(low.z, double(p.z))};
			high = {std::max(high.x, double(p.x)), std::max(high.y, double(p.y)), std::max(high.z, double(p.z))};
			any = true;
		}
	}
	return any ? std::max(1e-3, 0.5 * std::sqrt(dot(sub(high, low), sub(high, low)))) : 1.0;
}

QVector<ModelBvhTriangle> trianglesOf(const ModelMesh& mesh, int frame)
{
	QVector<ModelBvhTriangle> triangles;
	for (int s = 0; s < mesh.surfaces.size(); ++s) {
		const ModelSurface& surface = mesh.surfaces.at(s);
		if (frame < 0 || frame >= surface.frames.size()) {
			continue;
		}
		const auto& positions = surface.frames.at(frame).positions;
		for (int t = 0; t < surface.triangles.size(); ++t) {
			const ModelTriangle& tri = surface.triangles.at(t);
			if (tri.a < 0 || tri.b < 0 || tri.c < 0 || tri.a >= positions.size() || tri.b >= positions.size() || tri.c >= positions.size()) {
				continue;
			}
			triangles.append(ModelBvhTriangle{{positions.at(tri.a), positions.at(tri.b), positions.at(tri.c)}, s, t});
		}
	}
	return triangles;
}

QRgb encodeNormal(Vec n)
{
	const auto channel = [](double value) { return std::clamp(int(std::lround((value * 0.5 + 0.5) * 255.0)), 0, 255); };
	return qRgba(channel(n.x), channel(n.y), channel(n.z), 255);
}

QRgb gray(double value)
{
	const int v = std::clamp(int(std::lround(value * 255.0)), 0, 255);
	return qRgba(v, v, v, 255);
}

} // namespace

QString modelBakeTypeId(ModelBakeType type)
{
	switch (type) {
	case ModelBakeType::AmbientOcclusion:
		return QStringLiteral("ao");
	case ModelBakeType::ObjectNormal:
		return QStringLiteral("object-normal");
	case ModelBakeType::TangentNormal:
		return QStringLiteral("normal");
	case ModelBakeType::Lighting:
		break;
	}
	return QStringLiteral("lighting");
}

bool modelBakeTypeFromId(const QString& id, ModelBakeType* type)
{
	for (const auto candidate : {ModelBakeType::AmbientOcclusion, ModelBakeType::ObjectNormal, ModelBakeType::TangentNormal, ModelBakeType::Lighting}) {
		if (modelBakeTypeId(candidate) == id) {
			if (type) {
				*type = candidate;
			}
			return true;
		}
	}
	return false;
}

QString modelBakeTypeDisplayName(ModelBakeType type)
{
	switch (type) {
	case ModelBakeType::AmbientOcclusion:
		return QCoreApplication::translate("VibeStudioModelBake", "Ambient occlusion");
	case ModelBakeType::ObjectNormal:
		return QCoreApplication::translate("VibeStudioModelBake", "Object-space normals");
	case ModelBakeType::TangentNormal:
		return QCoreApplication::translate("VibeStudioModelBake", "Tangent-space normals (Doom 3)");
	case ModelBakeType::Lighting:
		break;
	}
	return QCoreApplication::translate("VibeStudioModelBake", "Lighting");
}

qint64 modelBakeMemoryEstimate(const ModelMesh& target, const ModelMesh* highPoly, const ModelBakeOptions& options)
{
	qint64 triangles = target.triangleCount;
	if (highPoly) {
		triangles += highPoly->triangleCount;
	}
	const qint64 texels = qint64(std::max(0, options.size.width())) * std::max(0, options.size.height());
	return texels * qint64(sizeof(Texel) + 8) + triangles * qint64(sizeof(ModelBvhTriangle) + 48);
}

bool bakeModelMaps(const ModelMesh& target, const QVector<int>& surfaces, int frame, const ModelMesh* highPoly, const ModelBakeOptions& options,
	ModelBakeResult* result, QString* error, const ModelWorkControl& control)
{
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	const int width = options.size.width(), height = options.size.height();
	if (!result || width < 8 || height < 8 || width > 4096 || height > 4096 || options.samples < 1 || options.samples > 1024 || options.padding < 0
		|| options.padding > 64 || !std::isfinite(options.maxDistance) || options.maxDistance < 0 || !std::isfinite(options.cageDistance)
		|| options.cageDistance < 0 || !std::isfinite(options.ambient) || options.ambient < 0 || options.ambient > 1) {
		return fail(QCoreApplication::translate("VibeStudioModelBake",
			"Bake sizes are 8 to 4096 texels, 1 to 1024 samples, 0 to 64 texels of padding, and distances and ambient must be positive."));
	}
	if (frame < 0 || frame >= target.frames.size()) {
		return fail(QCoreApplication::translate("VibeStudioModelBake", "Frame %1 does not exist.").arg(frame));
	}
	QVector<int> chosen = surfaces;
	if (chosen.isEmpty()) {
		for (int s = 0; s < target.surfaces.size(); ++s) {
			chosen << s;
		}
	}
	for (const int s : std::as_const(chosen)) {
		if (s < 0 || s >= target.surfaces.size()) {
			return fail(QCoreApplication::translate("VibeStudioModelBake", "Surface %1 does not exist.").arg(s));
		}
	}
	if (highPoly && (!highPoly->geometryAvailable || highPoly->frames.isEmpty())) {
		return fail(QCoreApplication::translate("VibeStudioModelBake", "The high-poly model has no geometry."));
	}
	const qint64 estimate = modelBakeMemoryEstimate(target, highPoly, options);
	if (estimate > options.memoryBudget) {
		return fail(QCoreApplication::translate("VibeStudioModelBake", "The bake would use %1 MiB, over its %2 MiB budget; bake a smaller size.")
			.arg(estimate / (1024 * 1024)).arg(options.memoryBudget / (1024 * 1024)));
	}
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	const double radius = radiusOf(target, frame);
	const double maxDistance = options.maxDistance > 0 ? options.maxDistance : radius * 0.25;
	const double cage = options.cageDistance > 0 ? options.cageDistance : radius * 0.05;
	const double epsilon = radius * 1e-4 + 1e-5;
	const bool needsOccluders = options.type == ModelBakeType::AmbientOcclusion || options.type == ModelBakeType::Lighting;
	ModelBvh occluders, high;
	QString bvhError;
	if (needsOccluders && !occluders.build(trianglesOf(highPoly && options.type == ModelBakeType::AmbientOcclusion ? *highPoly : target,
								 highPoly && options.type == ModelBakeType::AmbientOcclusion ? 0 : frame), &bvhError, control)) {
		return fail(bvhError);
	}
	const bool projecting = highPoly && (options.type == ModelBakeType::ObjectNormal || options.type == ModelBakeType::TangentNormal);
	if (projecting && !high.build(trianglesOf(*highPoly, 0), &bvhError, control)) {
		return fail(bvhError);
	}

	// Rasterise the chosen surfaces' UV triangles at texel centres.
	QVector<Texel> texels(qsizetype(width) * height);
	int overlaps = 0, outside = 0;
	for (const int s : std::as_const(chosen)) {
		const ModelSurface& surface = target.surfaces.at(s);
		if (frame >= surface.frames.size()) {
			continue;
		}
		const auto& positions = surface.frames.at(frame).positions;
		const auto& normals = surface.frames.at(frame).normals;
		for (const ModelTriangle& tri : surface.triangles) {
			if (!work.step()) {
				return false;
			}
			const int corner[3] = {tri.a, tri.b, tri.c};
			bool valid = true;
			for (int c : corner) {
				valid = valid && c >= 0 && c < positions.size() && c < surface.texCoords.size() && std::isfinite(surface.texCoords.at(c).u)
					&& std::isfinite(surface.texCoords.at(c).v);
			}
			if (!valid) {
				continue;
			}
			double px[3], py[3];
			Vec p[3], n[3];
			for (int i = 0; i < 3; ++i) {
				px[i] = double(surface.texCoords.at(corner[i]).u) * width;
				py[i] = double(surface.texCoords.at(corner[i]).v) * height;
				p[i] = vec(positions.at(corner[i]));
				n[i] = corner[i] < normals.size() ? vec(normals.at(corner[i])) : Vec{};
			}
			const Vec face = normalized(cross(sub(p[1], p[0]), sub(p[2], p[0])));
			for (Vec& normal : n) {
				normal = normalized(normal, face);
			}
			// Doom 3's per-triangle tangents from the s and t gradients.
			const double s1 = (px[1] - px[0]) / width, t1 = (py[1] - py[0]) / height;
			const double s2 = (px[2] - px[0]) / width, t2 = (py[2] - py[0]) / height;
			const double area = s1 * t2 - s2 * t1;
			Vec tangent{1, 0, 0}, bitangent{0, 1, 0};
			if (std::abs(area) > 1e-20) {
				const double inverse = 1.0 / area;
				tangent = mul(sub(mul(sub(p[1], p[0]), t2), mul(sub(p[2], p[0]), t1)), inverse);
				bitangent = mul(sub(mul(sub(p[2], p[0]), s1), mul(sub(p[1], p[0]), s2)), inverse);
			}
			const double uvArea = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);
			if (std::abs(uvArea) < 1e-12) {
				continue;
			}
			const int x0 = std::max(0, int(std::floor(std::min({px[0], px[1], px[2]}) - 0.5)));
			const int x1 = std::min(width - 1, int(std::ceil(std::max({px[0], px[1], px[2]}) - 0.5)));
			const int y0 = std::max(0, int(std::floor(std::min({py[0], py[1], py[2]}) - 0.5)));
			const int y1 = std::min(height - 1, int(std::ceil(std::max({py[0], py[1], py[2]}) - 0.5)));
			if (std::min({px[0], px[1], px[2]}) < 0 || std::max({px[0], px[1], px[2]}) > width || std::min({py[0], py[1], py[2]}) < 0
				|| std::max({py[0], py[1], py[2]}) > height) {
				++outside;
			}
			for (int y = y0; y <= y1; ++y) {
				for (int x = x0; x <= x1; ++x) {
					const double cx = x + 0.5, cy = y + 0.5;
					const double w0 = ((px[1] - cx) * (py[2] - cy) - (px[2] - cx) * (py[1] - cy)) / uvArea;
					const double w1 = ((px[2] - cx) * (py[0] - cy) - (px[0] - cx) * (py[2] - cy)) / uvArea;
					const double w2 = 1.0 - w0 - w1;
					if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) {
						continue;
					}
					Texel& texel = texels[qsizetype(y) * width + x];
					if (texel.covered) {
						++overlaps;
						continue;
					}
					texel.covered = true;
					texel.position = add(add(mul(p[0], w0), mul(p[1], w1)), mul(p[2], w2));
					texel.normal = normalized(add(add(mul(n[0], w0), mul(n[1], w1)), mul(n[2], w2)), face);
					const Vec t = normalized(sub(tangent, mul(texel.normal, dot(texel.normal, tangent))), Vec{1, 0, 0});
					const Vec b = normalized(sub(sub(bitangent, mul(texel.normal, dot(texel.normal, bitangent))), mul(t, dot(t, bitangent))), cross(texel.normal, t));
					texel.tangent = t;
					texel.bitangent = b;
				}
			}
		}
	}
	int covered = 0;
	for (const Texel& texel : std::as_const(texels)) {
		covered += texel.covered ? 1 : 0;
	}
	if (covered == 0) {
		return fail(QCoreApplication::translate("VibeStudioModelBake", "The chosen surfaces cover no texel of the 0-1 UV square at this size."));
	}

	QImage image(width, height, QImage::Format_ARGB32);
	const QRgb background = options.type == ModelBakeType::TangentNormal ? qRgba(128, 128, 255, 255)
		: options.type == ModelBakeType::ObjectNormal                   ? qRgba(128, 128, 128, 255)
		: options.type == ModelBakeType::AmbientOcclusion               ? qRgba(255, 255, 255, 255)
																		: gray(options.ambient);
	image.fill(background);
	const Vec light = normalized(vec(options.lightDirection));
	int misses = 0;
	for (int y = 0; y < height; ++y) {
		auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (int x = 0; x < width; ++x) {
			const Texel& texel = texels.at(qsizetype(y) * width + x);
			if (!texel.covered) {
				continue;
			}
			if (!work.step()) {
				return false;
			}
			const Vec origin = add(texel.position, mul(texel.normal, epsilon));
			switch (options.type) {
			case ModelBakeType::AmbientOcclusion: {
				// Cosine-weighted directions around the normal.
				const Vec helper = std::abs(texel.normal.z) < 0.9 ? Vec{0, 0, 1} : Vec{1, 0, 0};
				const Vec u = normalized(cross(helper, texel.normal));
				const Vec v = cross(texel.normal, u);
				int hits = 0;
				for (int sample = 0; sample < options.samples; ++sample) {
					const double r1 = random01(options.seed, x, y, sample * 2), r2 = random01(options.seed, x, y, sample * 2 + 1);
					const double phi = 2.0 * kPi * r1, r = std::sqrt(r2);
					const Vec direction = normalized(add(add(mul(u, r * std::cos(phi)), mul(v, r * std::sin(phi))), mul(texel.normal, std::sqrt(std::max(0.0, 1.0 - r2)))));
					if (occluders.occluded(model(origin), model(direction), 0.0, maxDistance)) {
						++hits;
					}
				}
				line[x] = gray(1.0 - double(hits) / options.samples);
				break;
			}
			case ModelBakeType::ObjectNormal:
			case ModelBakeType::TangentNormal: {
				Vec normal = texel.normal;
				if (projecting) {
					const Vec start = add(texel.position, mul(texel.normal, cage));
					const ModelBvhHit hit = high.nearest(model(start), model(mul(texel.normal, -1.0)), 0.0, cage * 2.0);
					if (hit.hit) {
						const ModelBvhTriangle& triangle = high.triangles().at(hit.index);
						const ModelSurface& surface = highPoly->surfaces.at(triangle.surface);
						const ModelTriangle& source = surface.triangles.at(triangle.triangle);
						const auto& highNormals = surface.frames.first().normals;
						const Vec face = normalized(cross(sub(vec(triangle.corners[1]), vec(triangle.corners[0])), sub(vec(triangle.corners[2]), vec(triangle.corners[0]))));
						if (highNormals.size() == surface.frames.first().positions.size() && source.a < highNormals.size() && source.b < highNormals.size()
							&& source.c < highNormals.size()) {
							normal = normalized(add(add(mul(vec(highNormals.at(source.a)), 1.0 - hit.u - hit.v), mul(vec(highNormals.at(source.b)), hit.u)),
													mul(vec(highNormals.at(source.c)), hit.v)),
								face);
						} else {
							normal = face;
						}
					} else {
						++misses;
					}
				}
				line[x] = options.type == ModelBakeType::ObjectNormal
					? encodeNormal(normal)
					: encodeNormal(normalized(Vec{dot(normal, texel.tangent), dot(normal, texel.bitangent), dot(normal, texel.normal)}));
				break;
			}
			case ModelBakeType::Lighting: {
				const double lambert = std::max(0.0, dot(texel.normal, light));
				const bool shadowed = lambert > 0.0 && occluders.occluded(model(origin), model(light), 0.0, radius * 4.0);
				line[x] = gray(options.ambient + (1.0 - options.ambient) * (shadowed ? 0.0 : lambert));
				break;
			}
			}
		}
	}

	// Padding: each pass gives every empty texel next to a filled one the
	// average of its filled neighbours.
	QVector<bool> filled(texels.size());
	for (qsizetype i = 0; i < texels.size(); ++i) {
		filled[i] = texels.at(i).covered;
	}
	for (int pass = 0; pass < options.padding; ++pass) {
		if (!work.check()) {
			return false;
		}
		QVector<bool> next = filled;
		const QImage before = image;
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				if (filled.at(qsizetype(y) * width + x)) {
					continue;
				}
				int r = 0, g = 0, b = 0, count = 0;
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						const int nx = x + dx, ny = y + dy;
						if ((dx || dy) && nx >= 0 && ny >= 0 && nx < width && ny < height && filled.at(qsizetype(ny) * width + nx)) {
							const QRgb c = before.pixel(nx, ny);
							r += qRed(c);
							g += qGreen(c);
							b += qBlue(c);
							++count;
						}
					}
				}
				if (count > 0) {
					image.setPixel(x, y, qRgba(r / count, g / count, b / count, 255));
					next[qsizetype(y) * width + x] = true;
				}
			}
		}
		filled = std::move(next);
	}
	if (!work.check()) {
		return false;
	}
	ModelBakeResult baked;
	baked.image = image;
	baked.texels = covered;
	baked.misses = misses;
	baked.maxDistance = maxDistance;
	baked.cageDistance = cage;
	if (overlaps > 0) {
		baked.notes << QCoreApplication::translate("VibeStudioModelBake", "%1 texel(s) are covered by more than one triangle; the first one baked wins.").arg(overlaps);
	}
	if (outside > 0) {
		baked.notes << QCoreApplication::translate("VibeStudioModelBake", "%1 triangle(s) reach outside the 0-1 UV square; only the part inside is baked.").arg(outside);
	}
	if (misses > 0) {
		baked.notes << QCoreApplication::translate("VibeStudioModelBake", "%1 texel(s) found no high-poly surface within the cage; they keep the low-poly normal.").arg(misses);
	}
	*result = std::move(baked);
	return true;
}

QJsonObject modelBakeResultJson(const ModelBakeResult& result, const ModelBakeOptions& options)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	const QImage image = result.image.convertToFormat(QImage::Format_ARGB32);
	for (int y = 0; y < image.height(); ++y) {
		hash.addData(QByteArrayView(reinterpret_cast<const char*>(image.constScanLine(y)), qsizetype(image.width()) * 4));
	}
	return QJsonObject{{QStringLiteral("type"), modelBakeTypeId(options.type)}, {QStringLiteral("width"), image.width()},
		{QStringLiteral("height"), image.height()}, {QStringLiteral("samples"), options.samples}, {QStringLiteral("seed"), double(options.seed)},
		{QStringLiteral("texels"), result.texels}, {QStringLiteral("misses"), result.misses},
		{QStringLiteral("maxDistance"), std::round(result.maxDistance * 1e6) / 1e6}, {QStringLiteral("cageDistance"), std::round(result.cageDistance * 1e6) / 1e6},
		{QStringLiteral("sha256"), QString::fromLatin1(hash.result().toHex())}, {QStringLiteral("notes"), QJsonArray::fromStringList(result.notes)}};
}

} // namespace vibestudio
