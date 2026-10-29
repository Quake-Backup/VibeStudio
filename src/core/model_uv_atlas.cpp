#include "core/model_uv_atlas.h"
#include "core/model_uv_mapping.h"
#include "core/model_uv.h"
#include "core/model_uv_atlas_memory.h"
#include "core/package_archive.h"

#include <QCoreApplication>
#include <QMap>
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <new>
#include <numeric>
#include <xatlas.h>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const QString &message)
{
	if (error)
	{
		*error = message;
	}
	return false;
}
std::array<int, 3> corners(ModelTriangle t)
{
	return {t.a, t.b, t.c};
}
struct Sets
{
	QVector<int> parent;
	explicit Sets(int count) : parent(count)
	{
		std::iota(parent.begin(), parent.end(), 0);
	}
	int root(int index)
	{
		while (parent[index] != index)
		{
			parent[index] = parent[parent[index]];
			index = parent[index];
		}
		return index;
	}
	void join(int a, int b)
	{
		a = root(a);
		b = root(b);
		parent[std::max(a, b)] = std::min(a, b);
	}
};
struct Input
{
	QVector<int> faces, sourceVertices;
	QVector<uint32_t> indices, materials;
	QVector<ModelVec3> positions, normals;
	QVector<ModelTexCoord> uv;
};

bool prepare(const ModelSurface &source, const QSet<int> &selected, int frame, bool unwrap, int width, int height, Input *input,
			 QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	ModelUvTopology topology;
	if (!buildModelUvTopology(source, &topology, error, control))
	{
		return false;
	}
	input->faces = selected.values();
	std::sort(input->faces.begin(), input->faces.end());
	QVector<int> local(source.triangles.size(), -1);
	for (int i = 0; i < input->faces.size(); ++i)
	{
		local[input->faces[i]] = i;
	}
	Sets fans(input->faces.size() * 3), charts(input->faces.size());
	for (const auto &edge : topology.edges)
	{
		if (!work.step())
		{
			return false;
		}
		if (edge.seam || edge.faces.size() != 2)
		{
			continue;
		}
		const int a = local[edge.faces[0]], b = local[edge.faces[1]];
		if (a < 0 || b < 0)
		{
			continue;
		}
		charts.join(a, b);
		const auto ca = corners(source.triangles[edge.faces[0]]), cb = corners(source.triangles[edge.faces[1]]);
		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				if (ca[i] == cb[j] && (ca[i] == edge.vertices.first || ca[i] == edge.vertices.second))
				{
					fans.join(a * 3 + i, b * 3 + j);
				}
			}
		}
	}
	QMap<int, int> roots;
	for (int face = 0; face < input->faces.size(); ++face)
	{
		if (!work.step())
		{
			return false;
		}
		input->materials.append(charts.root(face));
		const auto c = corners(source.triangles[input->faces[face]]);
		for (int i = 0; i < 3; ++i)
		{
			const int root = fans.root(face * 3 + i);
			if (!roots.contains(root))
			{
				roots.insert(root, input->sourceVertices.size());
				input->sourceVertices.append(c[i]);
			}
			input->indices.append(roots[root]);
		}
	}
	// An isolated interior seam can reconnect around both endpoint fans. Give
	// one incident face its own edge corners; automatic unwrap may add cuts.
	if (unwrap)
	{
		for (const auto &edge : topology.edges)
		{
			if (!work.step())
			{
				return false;
			}
			if (!edge.seam || edge.faces.size() != 2 || local[edge.faces[0]] < 0 || local[edge.faces[1]] < 0)
			{
				continue;
			}
			const int a = local[edge.faces[0]], b = local[edge.faces[1]];
			const auto ca = corners(source.triangles[edge.faces[0]]), cb = corners(source.triangles[edge.faces[1]]);
			for (int i = 0; i < 3; ++i)
			{
				if (ca[i] != edge.vertices.first && ca[i] != edge.vertices.second)
				{
					continue;
				}
				for (int j = 0; j < 3; ++j)
				{
					if (ca[i] == cb[j] && input->indices[a * 3 + i] == input->indices[b * 3 + j])
					{
						input->indices[b * 3 + j] = input->sourceVertices.size();
						input->sourceVertices.append(cb[j]);
					}
				}
			}
		}
	}
	// Normalize library input, not document geometry. A stable magnitude avoids
	// xatlas's absolute area tolerance rejecting otherwise usable small meshes.
	ModelVec3 lo{1e6f, 1e6f, 1e6f}, hi{-1e6f, -1e6f, -1e6f};
	ModelTexCoord uvLo{1e6f, 1e6f}, uvHi{-1e6f, -1e6f};
	for (int v : input->sourceVertices)
	{
		if (!work.step())
		{
			return false;
		}
		const auto p = source.frames[frame].positions[v];
		lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
		hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
		const auto uv = source.texCoords[v];
		uvLo = {std::min(uvLo.u, uv.u), std::min(uvLo.v, uv.v)};
		uvHi = {std::max(uvHi.u, uv.u), std::max(uvHi.v, uv.v)};
	}
	// Existing UVs describe this texture's normalized axes. Pack in pixel space
	// so rectangular textures retain chart shape and relative density.
	const double uAspect = double(width) / height;
	const double extent = unwrap ? std::max({double(hi.x) - lo.x, double(hi.y) - lo.y, double(hi.z) - lo.z})
								 : std::max((double(uvHi.u) - uvLo.u) * uAspect, double(uvHi.v) - uvLo.v);
	if (extent <= 0 || !std::isfinite(extent))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas",
													   "Selected faces have no usable area. Repair geometry or project UVs first."));
	}
	const double scale = 1024.0 / extent;
	for (int v : input->sourceVertices)
	{
		if (!work.step())
		{
			return false;
		}
		const auto p = source.frames[frame].positions[v];
		input->positions.append(
			{float((double(p.x) - lo.x) * scale), float((double(p.y) - lo.y) * scale), float((double(p.z) - lo.z) * scale)});
		input->normals.append(source.frames[frame].normals[v]);
		const auto uv = source.texCoords[v];
		input->uv.append({float((double(uv.u) - uvLo.u) * uAspect * scale), float((double(uv.v) - uvLo.v) * scale)});
	}
	return work.check();
}

struct Progress
{
	const ModelWorkControl &control;
	QString *error;
	bool stopped = false;
	static bool callback(xatlas::ProgressCategory category, int percent, void *context)
	{
		auto &self = *static_cast<Progress *>(context);
		self.stopped =
			self.stopped || !modelWorkCheckpoint(self.control, ModelWorkPhase::Editing, int(category) * 100 + percent, 400, self.error);
		return !self.stopped;
	}
};
struct AtlasOwner
{
	xatlas::Atlas *value = xatlas::Create();
	~AtlasOwner()
	{
		// An allocator exception may leave the upstream graph incomplete. The
		// enclosing allocation region releases all its blocks in that case.
		if (std::uncaught_exceptions() == 0)
		{
			xatlas::Destroy(value);
		}
	}
};
struct UvTriangle
{
	std::array<ModelTexCoord, 3> p;
	double minX, maxX, minY, maxY;
	int index = 0;
};
bool overlaps(const UvTriangle &a, const UvTriangle &b)
{
	for (const auto *triangle : {&a, &b})
	{
		for (int i = 0; i < 3; ++i)
		{
			const auto p = triangle->p[i], q = triangle->p[(i + 1) % 3];
			const double x = double(q.v) - p.v, y = double(p.u) - q.u;
			double loA = 1e20, hiA = -1e20, loB = 1e20, hiB = -1e20;
			for (int j = 0; j < 3; ++j)
			{
				const double pa = x * a.p[j].u + y * a.p[j].v, pb = x * b.p[j].u + y * b.p[j].v;
				loA = std::min(loA, pa);
				hiA = std::max(hiA, pa);
				loB = std::min(loB, pb);
				hiB = std::max(hiB, pb);
			}
			if (std::min(hiA, hiB) - std::max(loA, loB) <= 1e-10 * std::hypot(x, y))
			{
				return false;
			}
		}
	}
	return true;
}
bool validateAtlas(const QVector<ModelTexCoord> &uv, const QVector<uint32_t> &indices, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	for (int i = 0; i < indices.size(); i += 3)
	{
		if (!work.step())
		{
			return false;
		}
		const auto a = uv[indices[i]], b = uv[indices[i + 1]], c = uv[indices[i + 2]];
		const double area = (double(b.u) - a.u) * (double(c.v) - a.v) - (double(b.v) - a.v) * (double(c.u) - a.u);
		if (std::abs(area) < 1e-14)
		{
			return fail(error, QCoreApplication::translate(
								   "VibeStudioModelUvAtlas",
								   "A selected face has collapsed UVs. Repair geometry, unwrap again, or increase atlas resolution."));
		}
	}
	ModelUvOverlapScan scan;
	if (!findModelUvOverlaps(uv, indices, {}, 1, 8000000, &scan, error, control))
	{
		return false;
	}
	if (scan.limitReached)
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas",
													   "UV overlap validation reached its work limit. Process fewer faces at a time."));
	}
	if (!scan.pairs.isEmpty())
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas",
													   "Selected UV faces overlap inside an island. Add seams and unwrap before packing."));
	}
	return work.check();
}
} // namespace

bool findModelUvOverlaps(const QVector<ModelTexCoord> &uv, const QVector<uint32_t> &indices, const QVector<bool> &skip, int maxPairs,
						 quint64 maxComparisons, ModelUvOverlapScan *scan, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	QVector<UvTriangle> triangles;
	triangles.reserve(indices.size() / 3);
	for (int i = 0; i + 2 < indices.size(); i += 3)
	{
		if (!work.step())
		{
			return false;
		}
		if (i / 3 < skip.size() && skip[i / 3])
		{
			continue;
		}
		UvTriangle t;
		for (int j = 0; j < 3; ++j)
		{
			t.p[j] = uv[indices[i + j]];
		}
		const auto a = t.p[0], b = t.p[1], c = t.p[2];
		t.minX = std::min({a.u, b.u, c.u});
		t.maxX = std::max({a.u, b.u, c.u});
		t.minY = std::min({a.v, b.v, c.v});
		t.maxY = std::max({a.v, b.v, c.v});
		t.index = i / 3;
		triangles.append(t);
	}
	std::stable_sort(triangles.begin(), triangles.end(), [](const auto &a, const auto &b) { return a.minX < b.minX; });
	ModelUvOverlapScan result;
	for (int i = 0; i < triangles.size(); ++i)
	{
		const auto &a = triangles[i];
		for (int j = i + 1; j < triangles.size() && triangles[j].minX < a.maxX; ++j)
		{
			if (!work.step())
			{
				return false;
			}
			if (++result.comparisons > maxComparisons)
			{
				result.limitReached = true;
				*scan = std::move(result);
				return work.check();
			}
			const auto &b = triangles[j];
			if (a.maxY <= b.minY || b.maxY <= a.minY)
			{
				continue;
			}
			if (overlaps(a, b))
			{
				result.pairs.append({std::min(a.index, b.index), std::max(a.index, b.index)});
				if (maxPairs > 0 && result.pairs.size() >= maxPairs)
				{
					*scan = std::move(result);
					return work.check();
				}
			}
		}
	}
	std::sort(result.pairs.begin(), result.pairs.end());
	if (!work.check())
	{
		return false;
	}
	*scan = std::move(result);
	return true;
}

bool validateModelUvMapping(const QVector<ModelTexCoord> &uv, const QVector<uint32_t> &indices, QString *error,
							const ModelWorkControl &control)
{
	return validateAtlas(uv, indices, error, control);
}

namespace
{
struct Layout
{
	QVector<int> faces, sourceVertices;
	QVector<ModelTexCoord> uv;
	QVector<uint32_t> indices;
	int charts = 0;
	qsizetype peakBytes = 0;
	float density = 0;
};
QString materialKey(const QString &path)
{
	const auto normalized = normalizePackageVirtualPath(path, false);
	return normalized.isSafe() ? normalized.normalizedPath.toCaseFolded() : QString{};
}
double signedArea(const QVector<ModelTexCoord> &uv, const QVector<uint32_t> &indices, int face)
{
	const auto a = uv[indices[face * 3]], b = uv[indices[face * 3 + 1]], c = uv[indices[face * 3 + 2]];
	return (double(b.u) - a.u) * (double(c.v) - a.v) - (double(b.v) - a.v) * (double(c.u) - a.u);
}
// One xatlas charting and packing run. `uvInput` holds the pixel-space UVs
// packing keeps (ignored when unwrapping).
bool runAtlas(const Input &input, const QVector<ModelTexCoord> &uvInput, bool unwrap, const ModelUvAtlasOptions &options, int height,
			  Layout *layout, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	model_atlas_internal::AllocationRegion region(size_t(options.memoryLimit), control);
	try
	{
		// Uses xatlas's reviewed MIT/BSD implementation; see external/modelling/xatlas/VIBESTUDIO.md.
		Progress progress{control, error};
		AtlasOwner atlas;
		xatlas::SetProgressCallback(atlas.value, Progress::callback, &progress);
		xatlas::AddMeshError added;
		if (unwrap)
		{
			xatlas::MeshDecl decl;
			decl.vertexPositionData = input.positions.constData();
			decl.vertexPositionStride = sizeof(ModelVec3);
			decl.vertexNormalData = input.normals.constData();
			decl.vertexNormalStride = sizeof(ModelVec3);
			decl.vertexCount = input.sourceVertices.size();
			decl.indexData = input.indices.constData();
			decl.indexCount = input.indices.size();
			decl.indexFormat = xatlas::IndexFormat::UInt32;
			decl.faceMaterialData = input.materials.constData();
			added = xatlas::AddMesh(atlas.value, decl, 1);
		}
		else
		{
			xatlas::UvMeshDecl decl;
			decl.vertexUvData = uvInput.constData();
			decl.vertexStride = sizeof(ModelTexCoord);
			decl.vertexCount = uvInput.size();
			decl.indexData = input.indices.constData();
			decl.indexCount = input.indices.size();
			decl.indexFormat = xatlas::IndexFormat::UInt32;
			decl.faceMaterialData = input.materials.constData();
			added = xatlas::AddUvMesh(atlas.value, decl);
		}
		if (progress.stopped || !work.check())
		{
			return false;
		}
		if (added != xatlas::AddMeshError::Success)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas", "The atlas input could not be prepared: %1.")
								   .arg(QString::fromLatin1(xatlas::StringForEnum(added))));
		}
		xatlas::ChartOptions charts;
		charts.maxIterations = 2;
		xatlas::ComputeCharts(atlas.value, charts);
		if (progress.stopped || !work.check())
		{
			return false;
		}
		xatlas::PackOptions pack;
		pack.resolution = options.resolution - 2 * options.padding;
		pack.resolutionHeight = height - 2 * options.padding;
		pack.padding = options.padding;
		// Existing islands retain orientation, shape and relative scale. No
		// lightmap-style per-axis rounding or reflected quarter-turn placement:
		// xatlas's rotateCharts transposes charts, which mirrors them, so
		// quarter turns are made before packing instead (atlasLayout).
		pack.rotateCharts = false;
		pack.rotateChartsToAxis = unwrap;
		xatlas::PackCharts(atlas.value, pack);
		if (progress.stopped || !work.check())
		{
			return false;
		}
		const uint32_t maximum = std::max(atlas.value->width, atlas.value->height);
		if (maximum == 0 || atlas.value->chartCount == 0)
		{
			return fail(error,
						QCoreApplication::translate("VibeStudioModelUvAtlas",
													"No usable UV charts were produced. Repair degenerate faces or project UVs first."));
		}
		pack.texelsPerUnit =
			atlas.value->texelsPerUnit *
			std::min({1.0f, float(pack.resolution - 1) / atlas.value->width, float(pack.resolutionHeight - 1) / atlas.value->height}) *
			0.9f;
		const bool rectangular = options.resolution != height;
		if (rectangular && atlas.value->meshCount == 1 && atlas.value->meshes)
		{
			// Bound uniform density by each chart, rather than shrinking an entire
			// provisional square arrangement to the short axis. This lets charts
			// use the long axis without per-chart resizing or aspect distortion.
			QMap<int, std::array<float, 4>> bounds;
			const auto &provisional = atlas.value->meshes[0];
			for (uint32_t v = 0; v < provisional.vertexCount; ++v)
			{
				if (!work.step())
				{
					return false;
				}
				const auto &vertex = provisional.vertexArray[v];
				if (vertex.chartIndex < 0)
				{
					continue;
				}
				if (!bounds.contains(vertex.chartIndex))
				{
					bounds.insert(vertex.chartIndex, {vertex.uv[0], vertex.uv[0], vertex.uv[1], vertex.uv[1]});
				}
				auto &box = bounds[vertex.chartIndex];
				box = {std::min(box[0], vertex.uv[0]), std::max(box[1], vertex.uv[0]), std::min(box[2], vertex.uv[1]),
					   std::max(box[3], vertex.uv[1])};
			}
			float ratio = std::numeric_limits<float>::max();
			for (const auto &box : bounds)
			{
				if (box[1] > box[0])
				{
					ratio = std::min(ratio, (pack.resolution - 2.f) / (box[1] - box[0]));
				}
				if (box[3] > box[2])
				{
					ratio = std::min(ratio, (pack.resolutionHeight - 2.f) / (box[3] - box[2]));
				}
			}
			if (std::isfinite(ratio) && ratio > 0 && !bounds.isEmpty())
				pack.texelsPerUnit = atlas.value->texelsPerUnit * ratio * 0.99f;
		}
		float failedDensity = 0;
		for (int attempt = 0; attempt < 12; ++attempt)
		{
			xatlas::PackCharts(atlas.value, pack);
			if (progress.stopped || !work.check())
			{
				return false;
			}
			if (atlas.value->atlasCount == 1)
			{
				break;
			}
			failedDensity = pack.texelsPerUnit;
			pack.texelsPerUnit *= 0.7f;
		}
		if ((rectangular || options.allowRotation) && atlas.value->atlasCount == 1 && failedDensity > pack.texelsPerUnit)
		{
			float fitted = pack.texelsPerUnit;
			// Four bounded refinements recover useful texel density after finding
			// a fit. No exhaustive optimum is claimed by this heuristic packer.
			for (int attempt = 0; attempt < 4; ++attempt)
			{
				pack.texelsPerUnit = (fitted + failedDensity) * 0.5f;
				xatlas::PackCharts(atlas.value, pack);
				if (progress.stopped || !work.check())
				{
					return false;
				}
				if (atlas.value->atlasCount == 1)
				{
					fitted = pack.texelsPerUnit;
				}
				else
				{
					failedDensity = pack.texelsPerUnit;
				}
			}
			pack.texelsPerUnit = fitted;
			xatlas::PackCharts(atlas.value, pack);
			if (progress.stopped || !work.check())
			{
				return false;
			}
		}
		if (atlas.value->atlasCount != 1 || atlas.value->meshCount != 1 || !atlas.value->meshes)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas",
														   "The charts do not fit one atlas. Increase its size or reduce padding."));
		}
		const auto &output = atlas.value->meshes[0];
		if (output.indexCount != uint32_t(input.indices.size()))
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas",
														   "Atlas generation changed the face count. No changes were applied."));
		}
		Layout result;
		for (uint32_t v = 0; v < output.vertexCount; ++v)
		{
			if (!work.step())
			{
				return false;
			}
			const auto &vertex = output.vertexArray[v];
			const ModelTexCoord point{(vertex.uv[0] + options.padding) / options.resolution, (vertex.uv[1] + options.padding) / height};
			if (vertex.atlasIndex != 0 || vertex.chartIndex < 0 || vertex.xref >= uint32_t(input.sourceVertices.size()) ||
				!std::isfinite(point.u) || !std::isfinite(point.v) || point.u < 0 || point.v < 0 || point.u > 1 || point.v > 1)
			{
				return fail(error, QCoreApplication::translate(
									   "VibeStudioModelUvAtlas",
									   "A selected face could not be mapped into the atlas. Repair degenerate geometry or UVs first."));
			}
			result.uv.append(point);
			result.sourceVertices.append(input.sourceVertices[vertex.xref]);
		}
		for (uint32_t i = 0; i < output.indexCount; ++i)
		{
			if (!work.step())
			{
				return false;
			}
			const auto index = output.indexArray[i];
			if (index >= uint32_t(result.uv.size()) ||
				input.sourceVertices[output.vertexArray[index].xref] != input.sourceVertices[input.indices[i]])
			{
				return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas",
															   "Atlas generation changed a face corner. No changes were applied."));
			}
			result.indices.append(index);
		}
		result.faces = input.faces;
		result.charts = int(atlas.value->chartCount);
		result.peakBytes = qsizetype(region.peakBytes());
		result.density = pack.texelsPerUnit;
		*layout = std::move(result);
		return true;
	}
	catch (const model_atlas_internal::Cancelled &)
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas", "UV atlas operation cancelled. No changes were applied."));
	}
	catch (const std::bad_alloc &)
	{
		return fail(
			error, QCoreApplication::translate("VibeStudioModelUvAtlas",
											   "UV atlas generation exceeded its memory budget. Reduce the face selection or atlas size."));
	}
}
// The pixel-space input with every chart whose bounds are taller than wide
// (alongU) or wider than tall turned a quarter turn about its own centre.
// A rotation, never a reflection: (u, v) -> (cu - (v - cv), cv + (u - cu)).
QVector<ModelTexCoord> turnedCharts(const Input &input, bool alongU, bool *changed)
{
	QMap<uint32_t, std::array<double, 4>> bounds;
	for (int i = 0; i < input.indices.size(); ++i)
	{
		const auto chart = input.materials[i / 3];
		const auto uv = input.uv[input.indices[i]];
		if (!bounds.contains(chart))
			bounds.insert(chart, {uv.u, uv.u, uv.v, uv.v});
		auto &box = bounds[chart];
		box = {std::min(box[0], double(uv.u)), std::max(box[1], double(uv.u)), std::min(box[2], double(uv.v)), std::max(box[3], double(uv.v))};
	}
	auto result = input.uv;
	QVector<bool> done(result.size());
	*changed = false;
	for (int i = 0; i < input.indices.size(); ++i)
	{
		const auto vertex = input.indices[i];
		if (done[vertex])
			continue;
		done[vertex] = true;
		const auto &box = bounds[input.materials[i / 3]];
		const double width = box[1] - box[0], height = box[3] - box[2];
		if (alongU ? height <= width : width <= height)
			continue;
		const double cu = (box[0] + box[1]) / 2, cv = (box[2] + box[3]) / 2;
		const auto uv = input.uv[vertex];
		// Keep the turned chart's lower bound where it was, so coordinates stay
		// positive for the packer.
		result[vertex] = {float(box[0] + height / 2 - (uv.v - cv)), float(box[2] + width / 2 + (uv.u - cu))};
		*changed = true;
	}
	return result;
}
bool atlasLayout(const ModelSurface &source, const QSet<int> &faces, int frame, bool unwrap, const ModelUvAtlasOptions &options, Layout *layout,
				 QString *error, const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
	{
		return false;
	}
	const int height = options.height == 0 ? options.resolution : options.height;
	if (faces.isEmpty() || frame < 0 || frame >= source.frames.size() || options.resolution < 32 || options.resolution > 4096 || height < 32 ||
		height > 4096 || options.padding < 0 || options.padding > 64 || options.padding * 8 >= std::min(options.resolution, height) ||
		options.memoryLimit < 65536 || options.memoryLimit > 256 * 1024 * 1024 || options.vertexLimit < 1 || options.vertexLimit > 65536)
	{
		return fail(error,
					QCoreApplication::translate("VibeStudioModelUvAtlas",
												"Select faces and a valid reference frame. Atlas width and height must be 32–4096 pixels; "
												"padding must be 0–64 and less than one eighth of the smaller dimension."));
	}
	for (int face : faces)
	{
		if (face < 0 || face >= source.triangles.size())
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas", "A selected atlas face no longer exists."));
		}
	}
	Input input;
	if (!prepare(source, faces, frame, unwrap, options.resolution, height, &input, error, control))
	{
		return false;
	}
	// Quarter turns (packing only): also try every chart turned so its long
	// side lies along U, then along V, and keep the densest fit.
	QVector<QVector<ModelTexCoord>> candidates{input.uv};
	if (!unwrap && options.allowRotation)
	{
		for (bool alongU : {true, false})
		{
			bool changed = false;
			auto turned = turnedCharts(input, alongU, &changed);
			if (changed)
				candidates.append(std::move(turned));
		}
	}
	Layout best;
	bool found = false;
	QString firstError;
	for (int candidate = 0; candidate < candidates.size(); ++candidate)
	{
		Layout attempt;
		QString problem;
		if (!runAtlas(input, candidates[candidate], unwrap, options, height, &attempt, &problem, control))
		{
			if (!work.check())
				return false;
			if (candidate == 0)
				firstError = problem;
			continue;
		}
		if (!found || attempt.density > best.density)
		{
			best = std::move(attempt);
			found = true;
		}
	}
	if (!found)
		return fail(error, firstError);
	if (!validateAtlas(best.uv, best.indices, error, control))
	{
		return false;
	}
	if (!unwrap)
	{
		// Packing keeps every chart's winding: a mirrored chart would flip
		// the painted skin, so a reflected result is refused.
		for (int face = 0; face < best.faces.size(); ++face)
		{
			if (!work.step())
				return false;
			if ((signedArea(input.uv, input.indices, face) < 0) != (signedArea(best.uv, best.indices, face) < 0))
				return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas",
															   "Packing would mirror a UV island. No changes were applied."));
		}
	}
	*layout = std::move(best);
	return true;
}
} // namespace

bool atlasModelUv(const ModelSurface &source, const QSet<int> &faces, int frame, bool unwrap, const ModelUvAtlasOptions &options,
				  ModelSurface *result, QString *error, const ModelWorkControl &control, ModelUvAtlasReport *report)
{
	if (!result)
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas", "A selected atlas face no longer exists."));
	}
	Layout layout;
	if (!atlasLayout(source, faces, frame, unwrap, options, &layout, error, control))
		return false;
	if (!applyModelUvMapping(source, layout.faces, layout.sourceVertices, layout.uv, layout.indices, options.vertexLimit, result, error, control))
		return false;
	if (report)
	{
		*report = {layout.charts, layout.peakBytes};
	}
	return true;
}

bool modelUvSurfacesShareMaterial(const ModelSurface &a, const ModelSurface &b)
{
	QSet<QString> materials;
	for (const auto &path : a.skinPaths)
		if (!path.isEmpty())
			materials.insert(materialKey(path));
	return std::any_of(b.skinPaths.cbegin(), b.skinPaths.cend(),
					   [&](const QString &path) { return !path.isEmpty() && materials.contains(materialKey(path)); });
}

bool atlasModelUvSurfaces(const ModelMesh &mesh, int surface, const QSet<int> &faces, const QSet<int> &others, int frame,
						  const ModelUvAtlasOptions &options, ModelMesh *result, QString *error, const ModelWorkControl &control,
						  ModelUvAtlasReport *report)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!result || surface < 0 || surface >= mesh.surfaces.size() || frame < 0 || frame >= mesh.frames.size())
		return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas", "Choose an existing surface and reference frame."));
	auto sorted = others.values();
	std::sort(sorted.begin(), sorted.end());
	for (int s : sorted)
		if (s < 0 || s >= mesh.surfaces.size() || s == surface || !modelUvSurfacesShareMaterial(mesh.surfaces[surface], mesh.surfaces[s]))
			return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas",
														   "Pack only other surfaces that share a material slot with the active surface."));
	// One combined surface: the active surface's selected faces and every
	// face of the others, each part keeping its own vertex range.
	QVector<int> parts{surface};
	parts += sorted;
	ModelSurface combined;
	combined.frames.resize(1);
	QVector<int> vertexOffset, faceOffset;
	QSet<int> combinedFaces;
	for (int part : parts)
	{
		const auto &s = mesh.surfaces[part];
		vertexOffset.append(combined.vertexCount);
		faceOffset.append(int(combined.triangles.size()));
		if (s.frames.size() <= frame)
			return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas", "Choose an existing surface and reference frame."));
		combined.texCoords += s.texCoords;
		combined.frames[0].positions += s.frames[frame].positions;
		combined.frames[0].normals += s.frames[frame].normals;
		for (const auto &seam : s.uvSeams)
			combined.uvSeams.insert(modelEdge(seam.first + combined.vertexCount, seam.second + combined.vertexCount));
		for (int f = 0; f < s.triangles.size(); ++f)
		{
			if (!work.step())
				return false;
			const auto t = s.triangles[f];
			if (part != surface || faces.contains(f))
				combinedFaces.insert(int(combined.triangles.size()));
			combined.triangles.append({t.a + combined.vertexCount, t.b + combined.vertexCount, t.c + combined.vertexCount});
		}
		combined.vertexCount += s.vertexCount;
	}
	Layout layout;
	if (!atlasLayout(combined, combinedFaces, 0, false, options, &layout, error, control))
		return false;
	const auto partOfVertex = [&](int vertex)
	{
		return int(std::upper_bound(vertexOffset.cbegin(), vertexOffset.cend(), vertex) - vertexOffset.cbegin()) - 1;
	};
	auto candidate = mesh;
	for (int p = 0; p < parts.size(); ++p)
	{
		QVector<int> localFaces, localSources, outputIndex(layout.uv.size(), -1);
		QVector<ModelTexCoord> localUv;
		QVector<uint32_t> localIndices;
		for (int i = 0; i < layout.faces.size(); ++i)
		{
			if (!work.step())
				return false;
			const int face = layout.faces[i];
			const int next = p + 1 < parts.size() ? faceOffset[p + 1] : int(combined.triangles.size());
			if (face < faceOffset[p] || face >= next)
				continue;
			localFaces.append(face - faceOffset[p]);
			for (int corner = 0; corner < 3; ++corner)
			{
				const auto out = layout.indices[i * 3 + corner];
				const int source = layout.sourceVertices[out];
				if (partOfVertex(source) != p)
					return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas",
																   "Atlas generation changed a face corner. No changes were applied."));
				if (outputIndex[out] < 0)
				{
					outputIndex[out] = int(localUv.size());
					localUv.append(layout.uv[out]);
					localSources.append(source - vertexOffset[p]);
				}
				localIndices.append(uint32_t(outputIndex[out]));
			}
		}
		ModelSurface mapped;
		if (!applyModelUvMapping(mesh.surfaces[parts[p]], localFaces, localSources, localUv, localIndices, options.vertexLimit, &mapped, error,
								 control))
			return false;
		candidate.surfaces[parts[p]] = std::move(mapped);
	}
	if (!work.check())
		return false;
	if (report)
		*report = {layout.charts, layout.peakBytes};
	*result = std::move(candidate);
	return true;
}
} // namespace vibestudio
