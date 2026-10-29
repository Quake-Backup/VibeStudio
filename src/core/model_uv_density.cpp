#include "core/model_uv_density.h"
#include "core/model_document.h"
#include "core/model_geometry_helpers.h"
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
	Q_DECLARE_TR_FUNCTIONS(ModelUvDensity)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
bool validSize(QSize size) { return size.width() >= 1 && size.height() >= 1 && size.width() <= 65536 && size.height() <= 65536; }
} // namespace

bool inspectModelUvDensity(const ModelSurface &surface, const QSet<int> &faces, QSize textureSize, int referenceFrame,
						   ModelUvDensityReport *report, QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	if (!report || !validSize(textureSize) || referenceFrame < 0 || referenceFrame >= surface.frames.size() ||
		surface.frames[referenceFrame].positions.size() != surface.vertexCount)
		return fail(error, Text::tr("Choose an existing reference pose and texture dimensions from 1 to 65,536 pixels."));
	ModelUvTopology topology;
	if (!buildModelUvTopology(surface, &topology, error, control))
		return false;
	QSet<int> chosen;
	for (int face : faces)
	{
		if (!work.step())
			return false;
		if (face < 0 || face >= topology.faceIsland.size())
			return fail(error, Text::tr("A selected UV face no longer exists."));
		chosen.insert(topology.faceIsland[face]);
	}
	ModelUvDensityReport result;
	result.textureSize = textureSize;
	result.referenceFrame = referenceFrame;
	const auto &positions = surface.frames[referenceFrame].positions;
	const double pixels = double(textureSize.width()) * textureSize.height();
	using namespace model_geometry;
	for (int island = 0; island < topology.islands.size(); ++island)
	{
		if (!work.step())
			return false;
		if (!faces.isEmpty() && !chosen.contains(island))
			continue;
		ModelUvDensityIsland row;
		row.island = island;
		row.faces = topology.islands[island].faces;
		row.minimum = std::numeric_limits<double>::infinity();
		for (int face : row.faces)
		{
			if (!work.step())
				return false;
			result.faces.insert(face);
			const auto t = surface.triangles[face];
			const auto a = point(positions[t.a]), b = point(positions[t.b]), c = point(positions[t.c]);
			const auto u = surface.texCoords[t.a], v = surface.texCoords[t.b], w = surface.texCoords[t.c];
			const double area = .5 * length(cross(b - a, c - a));
			const double x = double(v.u) - u.u, y = double(v.v) - u.v, z = double(w.u) - u.u, h = double(w.v) - u.v;
			const double signedUv = x * h - y * z;
			const double uvArea = .5 * std::abs(signedUv) * pixels;
			if (!std::isfinite(area) || !std::isfinite(uvArea))
				return fail(error, Text::tr("UV density requires finite geometry and UV coordinates."));
			const bool collapsed = collapsedTriangle(positions[t.a], positions[t.b], positions[t.c]);
			const bool collapsedUv = std::abs(signedUv) <= 1e-12 * std::max({x * x + y * y, z * z + h * h, 1e-24});
			row.collapsedGeometry += collapsed;
			row.collapsedUvs += collapsedUv;
			row.mirroredFaces += signedUv < 0;
			row.modelArea += area;
			row.pixelArea += uvArea;
			if (!collapsed)
			{
				const double density = std::sqrt(uvArea / area);
				row.minimum = std::min(row.minimum, density);
				row.maximum = std::max(row.maximum, density);
			}
		}
		if (!std::isfinite(row.minimum))
			row.minimum = 0;
		row.density = row.modelArea > 0 ? std::sqrt(row.pixelArea / row.modelArea) : 0;
		result.modelArea += row.modelArea;
		result.pixelArea += row.pixelArea;
		result.islands.append(row);
	}
	if (result.islands.isEmpty())
		return fail(error, Text::tr("This surface has no UV islands to measure."));
	result.density = result.modelArea > 0 ? std::sqrt(result.pixelArea / result.modelArea) : 0;
	if (!work.check())
		return false;
	*report = std::move(result);
	return true;
}

QJsonObject modelUvDensityJson(const ModelUvDensityReport &report)
{
	QJsonArray islands;
	for (const auto &row : report.islands)
	{
		QJsonArray faces;
		for (int face : row.faces)
			faces.append(face);
		islands.append(QJsonObject{{"island", row.island},
								   {"faces", faces},
								   {"modelArea", row.modelArea},
								   {"pixelArea", row.pixelArea},
								   {"pixelsPerUnit", row.density},
								   {"minimumFaceDensity", row.minimum},
								   {"maximumFaceDensity", row.maximum},
								   {"collapsedGeometry", row.collapsedGeometry},
								   {"collapsedUvs", row.collapsedUvs},
								   {"mirroredFaces", row.mirroredFaces},
								   {"scalable", row.scalable()}});
	}
	return {{"textureWidth", report.textureSize.width()},
			{"textureHeight", report.textureSize.height()},
			{"referenceFrame", report.referenceFrame},
			{"modelArea", report.modelArea},
			{"pixelArea", report.pixelArea},
			{"pixelsPerUnit", report.density},
			{"islands", islands}};
}

bool applyModelUvDensity(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	const auto &o = edit.uvDensity;
	if (!mesh || !selection || selection->surface < 0 || selection->surface >= mesh->surfaces.size() || edit.frame != -1 ||
		!std::isfinite(o.target) || o.target < .000001 || o.target > 1000000)
		return fail(error, Text::tr("UV density spans every pose and needs a target from 0.000001 to 1,000,000 pixels per model unit."));
	// The active surface's selected islands, then every island of each other
	// listed surface, each measured against its own skin size.
	struct Part
	{
		int surface;
		QSet<int> faces;
		QSize size;
		ModelUvDensityReport before;
	};
	QVector<Part> parts;
	{
		const auto &source = mesh->surfaces[selection->surface];
		ModelUvTopology topology;
		QSet<int> faces;
		if (!buildModelUvTopology(source, &topology, error, control) ||
			!expandModelUvIslands(source, topology, selection->faces, selection->vertices, selection->edges, &faces, error, control))
			return false;
		if (faces.isEmpty())
			return fail(error, Text::tr("Select mesh components or complete UV islands before setting density."));
		parts.append({selection->surface, faces, o.textureSize, {}});
	}
	auto others = o.surfaces.values();
	std::sort(others.begin(), others.end());
	for (auto it = o.surfaceSizes.cbegin(); it != o.surfaceSizes.cend(); ++it)
		if (!o.surfaces.contains(it.key()) || !validSize(it.value()))
			return fail(error, Text::tr("Give texture sizes only for the other surfaces being matched, from 1 to 65,536 pixels."));
	for (int s : others)
	{
		if (s < 0 || s >= mesh->surfaces.size() || s == selection->surface)
			return fail(error, Text::tr("Match density across existing surfaces other than the active one."));
		QSet<int> all;
		for (int face = 0; face < mesh->surfaces[s].triangles.size(); ++face)
			all.insert(face);
		parts.append({s, all, o.surfaceSizes.value(s, o.textureSize), {}});
	}
	double modelArea = 0, pixelArea = 0;
	for (auto &part : parts)
	{
		if (!inspectModelUvDensity(mesh->surfaces[part.surface], part.faces, part.size, o.referenceFrame, &part.before, error, control))
			return false;
		modelArea += part.before.modelArea;
		pixelArea += part.before.pixelArea;
	}
	// Group mode keeps relative density across every surface: one factor from
	// the summed areas, each surface's texels counted on its own skin.
	const double combined = modelArea > 0 ? std::sqrt(pixelArea / modelArea) : 0;
	auto candidate = *mesh;
	for (const auto &part : parts)
	{
		const auto &source = candidate.surfaces[part.surface];
		const auto &before = part.before;
		ModelUvIslandTransform transform;
		QSet<int> changedFaces;
		transform.referenceFrame = o.referenceFrame;
		qint64 other = 0;
		for (int s = 0; s < candidate.surfaces.size(); ++s)
			if (s != part.surface)
				other += candidate.surfaces[s].vertexCount;
		transform.vertexLimit = int(
			std::min<qint64>(modelDocumentMaxVertices, modelDocumentMaxFrameVertices / std::max<qsizetype>(1, candidate.frames.size())) - other);
		for (const auto &row : before.islands)
		{
			if (!work.step())
				return false;
			if (!row.scalable())
				return fail(error, Text::tr("Island %1 of surface %2 has collapsed geometry or UV faces. Repair or unwrap it before setting "
											"density.")
									   .arg(row.island)
									   .arg(part.surface));
			const double factor = o.target / (o.individual ? row.density : combined);
			if (std::abs(factor - 1) <= 1e-12)
				continue;
			transform.islandScales.insert(row.island, factor);
			for (int face : row.faces)
			{
				if (!work.step())
					return false;
				changedFaces.insert(face);
			}
		}
		if (changedFaces.isEmpty())
			continue;
		if (transform.vertexLimit < source.vertexCount)
			return fail(error, Text::tr("Splitting UV islands would exceed the vertex or animation storage limit."));
		ModelSurface result;
		if (!transformModelUvIslands(source, changedFaces, transform, &result, error, control))
			return false;
		ModelUvDensityReport after;
		if (!inspectModelUvDensity(result, part.faces, part.size, o.referenceFrame, &after, error, control))
			return false;
		// Float UV storage can lose very small changes far outside the base tile.
		// Verify the stored result instead of claiming an unachievable target.
		for (int i = 0; i < after.islands.size(); ++i)
		{
			if (!work.step())
				return false;
			const double expected = o.individual ? o.target : before.islands[i].density * o.target / combined;
			if (!after.islands[i].scalable() || std::abs(after.islands[i].density - expected) > expected * .0001)
				return fail(
					error,
					Text::tr(
						"Stored UV precision cannot represent this density here. Move islands nearer the origin or choose a larger target."));
		}
		candidate.surfaces[part.surface] = std::move(result);
	}
	if (!work.check())
		return false;
	*mesh = std::move(candidate);
	*selection = {selection->surface, {}, parts.first().faces};
	return true;
}
} // namespace vibestudio
