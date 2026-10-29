// Align Vertices (core/model_align.h). With the editor's Symmetry axes
// (ModelMeshToolOptions::mirrorAxes) a selected vertex on an active mirror
// plane keeps its coordinate across it, and an unselected mirror counterpart
// of a selected vertex takes the reflected result on the aligned axes.
#include "core/model_align.h"

#include "core/model_mesh_tools_p.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace vibestudio
{
using namespace model_tools;

QString modelAlignAxesText(int axes)
{
	QString text;
	for (int axis = 0; axis < 3; ++axis)
	{
		if (axes & (1 << axis))
			text += QLatin1Char("xyz"[axis]);
	}
	return text;
}

bool alignModelVertices(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	const auto &options = edit.tool.align;
	if (options.axes < 1 || options.axes > 7)
		return fail(error, Text::tr("Align needs at least one of the X, Y and Z axes."));
	if (int(options.target) < 0 || int(options.target) > int(ModelAlignTarget::Median))
		return fail(error, Text::tr("Choose an alignment target: a value, the centroid, the minimum, the maximum, the grid or the median."));
	if (options.target == ModelAlignTarget::Grid && !(std::isfinite(options.grid) && options.grid >= 0.000001 && options.grid <= 1000000))
		return fail(error, Text::tr("Snapping to the grid needs a step from 0.000001 to 1,000,000 units."));
	if (options.target != ModelAlignTarget::Grid && options.grid != 0)
		return fail(error, Text::tr("A grid step applies only to snapping to the grid."));
	for (double value : options.value)
	{
		if (!std::isfinite(value) || std::abs(value) > 1000000)
			return fail(error, Text::tr("Tool coordinates must be finite and within ±1,000,000 units."));
	}
	auto &surface = candidate->surfaces[selection->surface];
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error, control))
		return false;
	const auto vertices = expandToCopies(topology, selectedVertexSet(surface, *selection));
	if (vertices.isEmpty())
		return fail(error, Text::tr("Select the vertices, edges or faces to align."));
	QVector<int> groups;
	for (int vertex : vertices)
	{
		if (topology.group(vertex) == vertex)
			groups.append(vertex);
	}
	std::sort(groups.begin(), groups.end());
	const int reference = edit.frame >= 0 ? edit.frame : std::clamp(edit.tool.referenceFrame, 0, int(surface.frames.size()) - 1);
	const auto coordinate = [](const P3 &p, int axis) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; };
	const auto centroid = [&](int frame, int axis)
	{
		double sum = 0;
		for (int group : std::as_const(groups))
			sum += coordinate(position(surface, frame, group), axis);
		return sum / groups.size();
	};
	// Median: the whole selection moves by one offset, taken in the reference
	// pose, so its centroid lands on the value and its shape is kept.
	double medianOffset[3]{0, 0, 0};
	if (options.target == ModelAlignTarget::Median)
		for (int axis = 0; axis < 3; ++axis)
			if (options.axes & (1 << axis))
				medianOffset[axis] = options.value[axis] - centroid(reference, axis);
	// Symmetry (edit.tool.mirrorAxes): a selected vertex on an active mirror
	// plane keeps its coordinate across that plane, and an unselected mirror
	// counterpart takes the reflected result on the aligned axes.
	const int mirror = edit.tool.mirrorAxes;
	QHash<int, int> planeAxes;					  // selected group -> planes it lies on
	QHash<int, std::pair<int, int>> counterparts; // counterpart group -> (selected group, reflected axes)
	if (mirror != 0)
	{
		double scale = 0;
		for (const auto &p : surface.frames[reference].positions)
			scale = std::max({scale, double(std::abs(p.x)), double(std::abs(p.y)), double(std::abs(p.z))});
		const double tolerance = std::max(edit.tool.mergeThreshold, scale * 1e-6);
		const QSet<int> chosen(groups.cbegin(), groups.cend());
		// Unselected groups in cells twice the tolerance wide.
		const double cell = std::max(tolerance * 2, 1e-9);
		const auto cellOf = [&](P3 p)
		{ return std::array<qint64, 3>{qint64(std::floor(p.x / cell)), qint64(std::floor(p.y / cell)), qint64(std::floor(p.z / cell))}; };
		std::map<std::array<qint64, 3>, QVector<int>> grid;
		for (int v = 0; v < surface.vertexCount; ++v)
			if (topology.group(v) == v && !chosen.contains(v))
				grid[cellOf(position(surface, reference, v))].append(v);
		for (int group : std::as_const(groups))
		{
			if (!work.step())
				return false;
			const P3 p = position(surface, reference, group);
			int plane = 0;
			for (int axis = 0; axis < 3; ++axis)
				if ((mirror & (1 << axis)) && std::abs(coordinate(p, axis)) <= tolerance)
					plane |= 1 << axis;
			if (plane != 0)
				planeAxes.insert(group, plane);
			for (int axes = 1; axes <= 7; ++axes)
			{
				if ((axes & ~mirror) != 0 || (axes & plane) != 0)
					continue;
				const P3 target{axes & 1 ? -p.x : p.x, axes & 2 ? -p.y : p.y, axes & 4 ? -p.z : p.z};
				int match = -1;
				double best = tolerance;
				const auto centre = cellOf(target);
				for (qint64 dx = -1; dx <= 1; ++dx)
					for (qint64 dy = -1; dy <= 1; ++dy)
						for (qint64 dz = -1; dz <= 1; ++dz)
						{
							const auto found = grid.find({centre[0] + dx, centre[1] + dy, centre[2] + dz});
							if (found == grid.end())
								continue;
							for (int candidate : found->second)
							{
								const double d = length(position(surface, reference, candidate) - target);
								if (d < best || (d == best && (match < 0 || candidate < match)))
								{
									best = d;
									match = candidate;
								}
							}
						}
				if (match >= 0 && !counterparts.contains(match))
					counterparts.insert(match, {group, axes});
			}
		}
	}
	QSet<int> moving = vertices;
	for (auto it = counterparts.cbegin(); it != counterparts.cend(); ++it)
		for (int copy : topology.copies[it.key()])
			moving.insert(copy);
	// Faces already collapsed before the edit are not this edit's doing.
	QSet<int> touched, collapsedBefore;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!work.step())
			return false;
		const auto &t = surface.triangles[face];
		if (moving.contains(t.a) || moving.contains(t.b) || moving.contains(t.c))
		{
			touched.insert(face);
			if (collapsedInAnyFrame(surface, t))
				collapsedBefore.insert(face);
		}
	}
	bool changed = false;
	for (int frame = 0; frame < surface.frames.size(); ++frame)
	{
		if (edit.frame >= 0 && frame != edit.frame)
			continue;
		// Each pose aligns to its own centroid or bounds, so an animated
		// selection is flattened where it is in that pose.
		double target[3]{0, 0, 0};
		for (int axis = 0; axis < 3; ++axis)
		{
			if (!(options.axes & (1 << axis)) || options.target == ModelAlignTarget::Grid || options.target == ModelAlignTarget::Median)
				continue;
			if (options.target == ModelAlignTarget::Value)
			{
				target[axis] = options.value[axis];
				continue;
			}
			double low = INFINITY, high = -INFINITY;
			for (int group : std::as_const(groups))
			{
				const double c = coordinate(position(surface, frame, group), axis);
				low = std::min(low, c);
				high = std::max(high, c);
			}
			target[axis] = options.target == ModelAlignTarget::Centroid ? centroid(frame, axis)
						   : options.target == ModelAlignTarget::Minimum ? low
																		  : high;
		}
		QHash<int, ModelVec3> results;
		for (int group : std::as_const(groups))
		{
			if (!work.step())
				return false;
			ModelVec3 p = surface.frames[frame].positions[group];
			float *coordinates[3]{&p.x, &p.y, &p.z};
			const int plane = planeAxes.value(group);
			for (int axis = 0; axis < 3; ++axis)
			{
				if (!(options.axes & (1 << axis)) || (plane & (1 << axis)))
					continue;
				const double value = options.target == ModelAlignTarget::Grid	   ? std::round(double(*coordinates[axis]) / options.grid) * options.grid
									 : options.target == ModelAlignTarget::Median ? double(*coordinates[axis]) + medianOffset[axis]
																				  : target[axis];
				*coordinates[axis] = float(value);
			}
			results.insert(group, p);
		}
		for (auto it = counterparts.cbegin(); it != counterparts.cend(); ++it)
		{
			const auto source = results.value(it.value().first);
			const int axes = it.value().second;
			ModelVec3 p = surface.frames[frame].positions[it.key()];
			const float reflected[3]{axes & 1 ? -source.x : source.x, axes & 2 ? -source.y : source.y, axes & 4 ? -source.z : source.z};
			float *coordinates[3]{&p.x, &p.y, &p.z};
			for (int axis = 0; axis < 3; ++axis)
				if (options.axes & (1 << axis))
					*coordinates[axis] = reflected[axis];
			results.insert(it.key(), p);
		}
		for (auto it = results.cbegin(); it != results.cend(); ++it)
		{
			const auto before = surface.frames[frame].positions[it.key()];
			const auto p = it.value();
			if (before.x != p.x || before.y != p.y || before.z != p.z)
				changed = true;
			// Seam copies share the position, so UV and hard-normal seams stay closed.
			for (int copy : topology.copies[it.key()])
				surface.frames[frame].positions[copy] = p;
		}
	}
	if (!changed)
		return fail(error, Text::tr("The selection is already aligned. Nothing moved."));
	int collapsed = 0;
	for (int face : std::as_const(touched))
	{
		if (!collapsedBefore.contains(face) && collapsedInAnyFrame(surface, surface.triangles[face]))
			++collapsed;
	}
	if (collapsed > 0)
		return fail(error, Text::tr("Aligning would collapse %n face(s) to a line or point. Select fewer vertices or another axis.", nullptr, collapsed));
	QSet<int> around = moving;
	for (int face : std::as_const(touched))
	{
		const auto &t = surface.triangles[face];
		around << t.a << t.b << t.c;
	}
	return refreshNormals(&surface, around, work, edit.frame);
}
} // namespace vibestudio
