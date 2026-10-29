#include "core/model_uv_health.h"

#include "core/model_geometry_helpers.h"
#include "core/model_mdl.h"
#include "core/model_targets.h"
#include "core/model_uv.h"
#include "core/model_uv_mapping.h"
#include "core/model_uv_raster.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelUvHealth)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
bool inTile(ModelTexCoord uv) { return uv.u >= 0 && uv.u <= 1 && uv.v >= 0 && uv.v <= 1; }
// Singular values of the map from a face's reference-pose plane to texels.
bool singularValues(const ModelSurface &surface, int face, int frame, QSize size, double *large, double *small)
{
	using namespace model_geometry;
	const auto t = surface.triangles[face];
	const auto &positions = surface.frames[frame].positions;
	const auto p0 = point(positions[t.a]), p1 = point(positions[t.b]), p2 = point(positions[t.c]);
	const auto e1 = p1 - p0, e2 = p2 - p0;
	const double l1 = length(e1);
	const auto n = cross(e1, e2);
	const double ln = length(n);
	if (!(l1 > 0) || !(ln > 0))
		return false;
	const auto x = e1 * (1 / l1), y = cross(n, e1) * (1 / (ln * l1));
	// Local 2D corners: (0,0), (l1,0), (px,py).
	const double px = dot(e2, x), py = dot(e2, y);
	const auto uv0 = surface.texCoords[t.a], uv1 = surface.texCoords[t.b], uv2 = surface.texCoords[t.c];
	const double w = size.width(), h = size.height();
	const double q1x = (double(uv1.u) - uv0.u) * w, q1y = (double(uv1.v) - uv0.v) * h;
	const double q2x = (double(uv2.u) - uv0.u) * w, q2y = (double(uv2.v) - uv0.v) * h;
	// J = Q * P^-1 with P = [[l1, px], [0, py]].
	const double inv00 = 1 / l1, inv01 = -px / (l1 * py), inv11 = 1 / py;
	const double a = q1x * inv00, b = q1x * inv01 + q2x * inv11;
	const double c = q1y * inv00, d = q1y * inv01 + q2y * inv11;
	const double e = (a * a + b * b + c * c + d * d) / 2, det = std::abs(a * d - b * c);
	const double root = std::sqrt(std::max(0.0, e * e - det * det));
	*large = std::sqrt(e + root);
	*small = std::sqrt(std::max(0.0, e - root));
	return std::isfinite(*large) && std::isfinite(*small);
}
QJsonArray indices(const QVector<int> &values)
{
	QJsonArray result;
	for (int value : values)
		result.append(value);
	return result;
}
} // namespace

QVector<int> ModelUvHealthReport::faces(quint8 flag) const
{
	QVector<int> result;
	for (int face = 0; face < faceFlags.size(); ++face)
		if (faceFlags[face] & flag)
			result.append(face);
	return result;
}

bool inspectModelUvSurfaceHealth(const ModelSurface &surface, const ModelUvHealthOptions &options, ModelUvHealthReport *report,
								 QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	if (!work.check())
		return false;
	const auto size = options.textureSize;
	if (!report || size.width() < 1 || size.height() < 1 || size.width() > 65536 || size.height() > 65536 || options.referenceFrame < 0 ||
		options.referenceFrame >= surface.frames.size() || surface.frames[options.referenceFrame].positions.size() != surface.vertexCount ||
		!std::isfinite(options.stretchLimit) || options.stretchLimit < 1 || options.stretchLimit > 1000000 || options.listedPairs < 0 ||
		options.overlapComparisons < 1)
		return fail(error, Text::tr("Choose an existing reference pose, texture dimensions from 1 to 65,536 pixels and a stretch limit "
									"of at least 1."));
	ModelUvTopology topology;
	if (!buildModelUvTopology(surface, &topology, error, control))
		return false;
	using namespace model_geometry;
	const int faces = int(surface.triangles.size());
	ModelUvHealthReport result;
	result.textureSize = size;
	result.referenceFrame = options.referenceFrame;
	result.stretchLimit = options.stretchLimit;
	result.faceCount = faces;
	result.faceFlags.fill(0, faces);
	result.faceStretch.fill(1, faces);
	const auto &positions = surface.frames[options.referenceFrame].positions;
	const double pixels = double(size.width()) * size.height();
	QVector<double> signedArea(faces), modelArea(faces), large(faces), small(faces);
	QVector<bool> skip(faces);
	for (int face = 0; face < faces; ++face)
	{
		if (!work.step())
			return false;
		const auto t = surface.triangles[face];
		const auto u = surface.texCoords[t.a], v = surface.texCoords[t.b], w = surface.texCoords[t.c];
		const double x = double(v.u) - u.u, y = double(v.v) - u.v, z = double(w.u) - u.u, h = double(w.v) - u.v;
		const double area = x * h - y * z;
		signedArea[face] = .5 * area * pixels;
		const bool collapsedUv = std::abs(area) <= 1e-12 * std::max({x * x + y * y, z * z + h * h, 1e-24});
		const bool collapsedGeometry = collapsedTriangle(positions[t.a], positions[t.b], positions[t.c]);
		const auto a3 = point(positions[t.a]), b3 = point(positions[t.b]), c3 = point(positions[t.c]);
		modelArea[face] = .5 * length(cross(b3 - a3, c3 - a3));
		if (!std::isfinite(modelArea[face]) || !std::isfinite(signedArea[face]))
			return fail(error, Text::tr("UV health requires finite geometry and UV coordinates."));
		auto &flags = result.faceFlags[face];
		if (collapsedUv)
			flags |= ModelUvHealthCollapsedUv;
		if (collapsedGeometry)
			flags |= ModelUvHealthCollapsedGeometry;
		skip[face] = collapsedUv;
		if (!collapsedUv && !collapsedGeometry && !singularValues(surface, face, options.referenceFrame, size, &large[face], &small[face]))
			flags |= ModelUvHealthCollapsedGeometry;
		for (int vertex : {t.a, t.b, t.c})
			if (!inTile(surface.texCoords[vertex]))
				flags |= ModelUvHealthOutsideTile;
	}
	// Islands: majority winding, density and stretch.
	for (int index = 0; index < topology.islands.size(); ++index)
	{
		if (!work.step())
			return false;
		ModelUvHealthIsland island;
		island.island = index;
		island.faces = topology.islands[index].faces;
		double winding = 0, area3 = 0, areaUv = 0;
		for (int face : island.faces)
		{
			if (result.faceFlags[face] & (ModelUvHealthCollapsedUv | ModelUvHealthCollapsedGeometry))
				continue;
			winding += signedArea[face];
			area3 += modelArea[face];
			areaUv += std::abs(signedArea[face]);
		}
		island.mirrored = winding < 0;
		island.density = area3 > 0 ? std::sqrt(areaUv / area3) : 0;
		const double rho = island.density;
		double weighted = 0, weights = 0;
		island.minStretch = std::numeric_limits<double>::infinity();
		island.maxStretch = 1;
		bool measured = false;
		for (int face : island.faces)
		{
			if (!work.step())
				return false;
			auto &flags = result.faceFlags[face];
			if (flags & (ModelUvHealthCollapsedUv | ModelUvHealthCollapsedGeometry))
				continue;
			if ((signedArea[face] < 0) != island.mirrored)
			{
				flags |= ModelUvHealthFlipped;
				++island.flipped;
			}
			if (!(rho > 0) || !(small[face] > 0))
				continue;
			const double s1 = large[face] / rho, s2 = small[face] / rho;
			const double stretch = std::max(s1, 1 / s2);
			result.faceStretch[face] = stretch;
			measured = true;
			island.minStretch = std::min(island.minStretch, stretch);
			island.maxStretch = std::max(island.maxStretch, stretch);
			island.maxAngleDistortion = std::max(island.maxAngleDistortion, large[face] / small[face]);
			island.minAreaRatio = std::min(island.minAreaRatio, s1 * s2);
			island.maxAreaRatio = std::max(island.maxAreaRatio, s1 * s2);
			weighted += stretch * modelArea[face];
			weights += modelArea[face];
			if (stretch > options.stretchLimit)
				flags |= ModelUvHealthStretched;
		}
		if (!measured)
			island.minStretch = 1;
		island.meanStretch = weights > 0 ? weighted / weights : 1;
		result.maxStretch = std::max(result.maxStretch, island.maxStretch);
		result.mirroredIslands += island.mirrored;
		result.islands.append(island);
	}
	// Overlaps across the whole surface, with the packer's own sweep.
	QVector<uint32_t> corners;
	corners.reserve(faces * 3);
	for (const auto &t : surface.triangles)
		corners << uint32_t(t.a) << uint32_t(t.b) << uint32_t(t.c);
	ModelUvOverlapScan scan;
	if (!findModelUvOverlaps(surface.texCoords, corners, skip, 0, options.overlapComparisons, &scan, error, control))
		return false;
	result.overlapLimitReached = scan.limitReached;
	const auto mirroredFace = [&](int face) { return result.islands[topology.faceIsland[face]].mirrored; };
	for (const auto &pair : scan.pairs)
	{
		if (!work.step())
			return false;
		const int a = pair.first, b = pair.second;
		const bool stacked = topology.faceIsland[a] != topology.faceIsland[b] && mirroredFace(a) != mirroredFace(b) &&
							 !(result.faceFlags[a] & ModelUvHealthFlipped) && !(result.faceFlags[b] & ModelUvHealthFlipped);
		if (stacked)
		{
			++result.mirrorStackedPairCount;
			result.faceFlags[a] |= ModelUvHealthMirrorStacked;
			result.faceFlags[b] |= ModelUvHealthMirrorStacked;
			continue;
		}
		++result.overlapPairCount;
		result.faceFlags[a] |= ModelUvHealthOverlapping;
		result.faceFlags[b] |= ModelUvHealthOverlapping;
		if (result.overlapPairs.size() < options.listedPairs)
			result.overlapPairs.append(pair);
	}
	// Texel-centre rounding of every used corner inside the tile, as the MDL and
	// MD2 writers round it.
	QVector<bool> used(surface.vertexCount);
	for (const auto &t : surface.triangles)
		used[t.a] = used[t.b] = used[t.c] = true;
	const int width = size.width(), height = size.height();
	for (int vertex = 0; vertex < surface.vertexCount; ++vertex)
	{
		if (!work.step())
			return false;
		if (!used[vertex])
			continue;
		const auto uv = surface.texCoords[vertex];
		if (!inTile(uv))
		{
			result.outsideVertices.append(vertex);
			continue;
		}
		const int s = std::clamp(int(std::lround(double(uv.u) * width - .5)), 0, width - 1);
		const int t = std::clamp(int(std::lround(double(uv.v) * height - .5)), 0, height - 1);
		result.maxTexelError = std::max({result.maxTexelError, std::abs(s + .5 - double(uv.u) * width), std::abs(t + .5 - double(uv.v) * height)});
	}
	for (int face = 0; face < faces; ++face)
	{
		const auto flags = result.faceFlags[face];
		if (flags & ModelUvHealthStretched)
			result.stretched.append(face);
		if (flags & ModelUvHealthOverlapping)
			result.overlapping.append(face);
		if (flags & ModelUvHealthFlipped)
			result.flipped.append(face);
		if (flags & ModelUvHealthOutsideTile)
			result.outside.append(face);
		if (flags & ModelUvHealthCollapsedUv)
			result.collapsedUv.append(face);
		if (flags & ModelUvHealthCollapsedGeometry)
			result.collapsedGeometry.append(face);
		if (flags & ModelUvHealthMirrorStacked)
			result.mirrorStacked.append(face);
	}
	if (!work.check())
		return false;
	*report = std::move(result);
	return true;
}

QSize modelUvHealthDefaultTextureSize(const ModelMesh &mesh)
{
	const auto size = mesh.mdl.enabled ? mesh.mdl.skinSize : mesh.md2SkinSize;
	return size.isValid() && !size.isEmpty() ? size : QSize(256, 256);
}

bool inspectModelUvHealth(const ModelMesh &mesh, int surface, const ModelUvHealthOptions &options, ModelUvHealthReport *report,
						  QString *error, const ModelWorkControl &control)
{
	if (surface < 0 || surface >= mesh.surfaces.size())
		return fail(error, Text::tr("Choose an existing model surface."));
	ModelUvHealthReport result;
	if (!inspectModelUvSurfaceHealth(mesh.surfaces[surface], options, &result, error, control))
		return false;
	result.surface = surface;
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	const auto &source = mesh.surfaces[surface];
	const bool allInside = std::all_of(source.texCoords.cbegin(), source.texCoords.cend(), inTile);
	const auto limits = [](const QString &format)
	{
		ModelTargetProfile profile;
		findModelTargetProfile(format, {}, &profile);
		return profile.limits;
	};
	ModelUvHealthNative mdl;
	mdl.format = QStringLiteral("mdl");
	mdl.limit = limits(mdl.format).vertices;
	mdl.renderLimit = limits(mdl.format).renderVertices;
	if (!mesh.mdl.enabled)
		mdl.note = Text::tr("The source has no MDL settings.");
	else if (mesh.surfaces.size() != 1)
		mdl.note = Text::tr("MDL holds one surface; join the surfaces first.");
	else if (!allInside)
		mdl.note = Text::tr("MDL needs every UV inside the 0–1 tile.");
	else
	{
		QString problem;
		mdl.available = modelMdlVertexCounts(mesh, &mdl.stored, &mdl.render, &problem, control);
		if (!mdl.available)
		{
			if (!work.check())
				return false;
			mdl.note = problem;
		}
	}
	result.native.append(mdl);
	ModelUvHealthNative md2;
	md2.format = QStringLiteral("md2");
	md2.limit = limits(md2.format).vertices;
	md2.renderLimit = limits(md2.format).renderVertices;
	if (mesh.surfaces.size() != 1)
		md2.note = Text::tr("MD2 holds one surface; join the surfaces first.");
	else if (!allInside)
		md2.note = Text::tr("MD2 needs every UV inside the 0–1 tile.");
	else
	{
		QVector<int> positionIndex, representative;
		if (!modelMd2PositionIndex(mesh, &positionIndex, &representative, error, control))
			return false;
		const int width = std::max(1, mesh.md2SkinSize.width()), height = std::max(1, mesh.md2SkinSize.height());
		QSet<qint64> drawn;
		for (const auto &face : source.triangles)
			for (int v : {face.a, face.b, face.c})
			{
				const auto uv = source.texCoords[v];
				const qint64 s = std::clamp(int(std::lround(double(uv.u) * width - .5)), 0, width - 1);
				const qint64 t = std::clamp(int(std::lround(double(uv.v) * height - .5)), 0, height - 1);
				drawn.insert((qint64(positionIndex[v]) << 32) | (s << 16) | t);
			}
		md2.available = true;
		md2.stored = int(representative.size());
		md2.render = int(drawn.size());
	}
	result.native.append(md2);
	ModelUvHealthNative md3;
	md3.format = QStringLiteral("md3");
	md3.limit = limits(md3.format).vertices;
	md3.available = true;
	md3.stored = md3.render = source.vertexCount;
	result.native.append(md3);
	// GLQuake-style flood fill of the first skin's top-left colour region. The
	// behaviour follows Mod_FloodFillSkin in id Software's WinQuake/gl_model.c
	// (GPL-2.0-or-later, see docs/CREDITS.md); this check is original and only
	// measures the region, it never changes skin pixels.
	if (surface == 0 && mesh.mdl.enabled && mesh.surfaces.size() == 1 && !mesh.embeddedSkins.isEmpty() &&
		!mesh.embeddedSkins.first().indexedFrames.isEmpty())
	{
		const int width = mesh.mdl.skinSize.width(), height = mesh.mdl.skinSize.height();
		const auto &pixels = mesh.embeddedSkins.first().indexedFrames.first();
		if (width > 0 && height > 0 && pixels.size() == qint64(width) * height)
		{
			result.floodFillChecked = true;
			result.floodFillIndex = quint8(pixels[0]);
			if (result.floodFillIndex != 0 && result.floodFillIndex != 255)
			{
				QVector<bool> region(width * height), usedTexel(width * height);
				QVector<int> queue{0};
				region[0] = true;
				for (int head = 0; head < queue.size(); ++head)
				{
					if (!work.step())
						return false;
					const int at = queue[head], x = at % width, y = at / width;
					const int next[4][2]{{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
					for (const auto &n : next)
					{
						if (n[0] < 0 || n[1] < 0 || n[0] >= width || n[1] >= height)
							continue;
						const int index = n[1] * width + n[0];
						if (!region[index] && quint8(pixels[index]) == result.floodFillIndex)
						{
							region[index] = true;
							queue.append(index);
						}
					}
				}
				result.floodFillTexels = int(queue.size());
				// Texels a face samples: centres inside it and its corner texels.
				for (const auto &t : source.triangles)
				{
					if (!work.step())
						return false;
					for (int v : {t.a, t.b, t.c})
					{
						const auto uv = source.texCoords[v];
						const int s = std::clamp(int(std::floor(double(uv.u) * width)), 0, width - 1);
						const int r = std::clamp(int(std::floor(double(uv.v) * height)), 0, height - 1);
						usedTexel[r * width + s] = true;
					}
					rasterizeModelUvTriangle(modelUvPixel(source.texCoords[t.a], width, height),
											 modelUvPixel(source.texCoords[t.b], width, height),
											 modelUvPixel(source.texCoords[t.c], width, height), width, height,
											 [&](int x, int y, double, double, double)
											 {
												 usedTexel[y * width + x] = true;
												 return true;
											 });
				}
				for (int index : queue)
					result.floodFillUsedTexels += usedTexel[index];
			}
		}
	}
	if (!work.check())
		return false;
	*report = std::move(result);
	return true;
}

QJsonObject modelUvHealthJson(const ModelUvHealthReport &report)
{
	QJsonArray pairs, islands, native;
	for (const auto &pair : report.overlapPairs)
		pairs.append(QJsonArray{pair.first, pair.second});
	for (const auto &island : report.islands)
		islands.append(QJsonObject{{"island", island.island},
								   {"faces", indices(island.faces)},
								   {"mirrored", island.mirrored},
								   {"flippedFaces", island.flipped},
								   {"density", island.density},
								   {"minStretch", island.minStretch},
								   {"maxStretch", island.maxStretch},
								   {"meanStretch", island.meanStretch},
								   {"maxAngleDistortion", island.maxAngleDistortion},
								   {"minAreaRatio", island.minAreaRatio},
								   {"maxAreaRatio", island.maxAreaRatio}});
	for (const auto &entry : report.native)
	{
		QJsonObject row{{"format", entry.format}, {"available", entry.available}, {"limit", entry.limit}};
		if (entry.available)
		{
			row.insert("stored", entry.stored);
			row.insert("render", entry.render);
			if (entry.renderLimit > 0)
				row.insert("renderLimit", entry.renderLimit);
			row.insert("withinLimit", (entry.limit <= 0 || entry.stored <= entry.limit) &&
										  (entry.renderLimit <= 0 || entry.render <= entry.renderLimit));
		}
		else
			row.insert("note", entry.note);
		native.append(row);
	}
	QJsonObject result{{"surface", report.surface},
					   {"textureSize", QJsonArray{report.textureSize.width(), report.textureSize.height()}},
					   {"referenceFrame", report.referenceFrame},
					   {"stretchLimit", report.stretchLimit},
					   {"faces", report.faceCount},
					   {"stretchedFaces", indices(report.stretched)},
					   {"overlappingFaces", indices(report.overlapping)},
					   {"flippedFaces", indices(report.flipped)},
					   {"outsideTileFaces", indices(report.outside)},
					   {"outsideTileVertices", indices(report.outsideVertices)},
					   {"collapsedUvFaces", indices(report.collapsedUv)},
					   {"collapsedGeometryFaces", indices(report.collapsedGeometry)},
					   {"mirrorStackedFaces", indices(report.mirrorStacked)},
					   {"overlapPairs", pairs},
					   {"overlapPairCount", report.overlapPairCount},
					   {"mirrorStackedPairCount", report.mirrorStackedPairCount},
					   {"overlapLimitReached", report.overlapLimitReached},
					   {"mirroredIslands", report.mirroredIslands},
					   {"maxStretch", report.maxStretch},
					   {"maxTexelError", report.maxTexelError},
					   {"islands", islands}};
	if (!report.native.isEmpty())
		result.insert("native", native);
	if (report.floodFillChecked)
		result.insert("floodFill", QJsonObject{{"firstTexelIndex", report.floodFillIndex},
											   {"regionTexels", report.floodFillTexels},
											   {"usedTexels", report.floodFillUsedTexels},
											   {"hazard", report.floodFillHazard()}});
	return result;
}

bool modelUvHealthHas(const ModelUvHealthReport &report, const QString &problem)
{
	if (problem == QLatin1String("overlap"))
		return report.overlapPairCount > 0 || report.overlapLimitReached;
	if (problem == QLatin1String("flipped"))
		return !report.flipped.isEmpty();
	if (problem == QLatin1String("outside"))
		return !report.outside.isEmpty();
	if (problem == QLatin1String("collapsed"))
		return !report.collapsedUv.isEmpty() || !report.collapsedGeometry.isEmpty();
	if (problem == QLatin1String("stretch"))
		return !report.stretched.isEmpty();
	if (problem == QLatin1String("flood-fill"))
		return report.floodFillHazard();
	return false;
}
} // namespace vibestudio
