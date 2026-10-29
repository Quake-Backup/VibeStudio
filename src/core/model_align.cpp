#include "core/model_align.h"

#include "core/model_mesh_tools_p.h"

#include <algorithm>
#include <cmath>

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
	if (int(options.target) < 0 || int(options.target) > int(ModelAlignTarget::Grid))
		return fail(error, Text::tr("Choose an alignment target: a value, the centroid, the minimum, the maximum or the grid."));
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
	// Faces already collapsed before the edit are not this edit's doing.
	QSet<int> touched, collapsedBefore;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!work.step())
			return false;
		const auto &t = surface.triangles[face];
		if (vertices.contains(t.a) || vertices.contains(t.b) || vertices.contains(t.c))
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
			if (!(options.axes & (1 << axis)))
				continue;
			if (options.target == ModelAlignTarget::Value)
			{
				target[axis] = options.value[axis];
				continue;
			}
			double sum = 0, low = INFINITY, high = -INFINITY;
			for (int group : std::as_const(groups))
			{
				const P3 p = position(surface, frame, group);
				const double c = axis == 0 ? p.x : axis == 1 ? p.y : p.z;
				sum += c;
				low = std::min(low, c);
				high = std::max(high, c);
			}
			target[axis] = options.target == ModelAlignTarget::Centroid ? sum / groups.size()
						   : options.target == ModelAlignTarget::Minimum ? low
																		  : high;
		}
		for (int group : std::as_const(groups))
		{
			if (!work.step())
				return false;
			ModelVec3 p = surface.frames[frame].positions[group];
			float *coordinates[3]{&p.x, &p.y, &p.z};
			for (int axis = 0; axis < 3; ++axis)
			{
				if (!(options.axes & (1 << axis)))
					continue;
				const double value = options.target == ModelAlignTarget::Grid ? std::round(double(*coordinates[axis]) / options.grid) * options.grid
																			   : target[axis];
				*coordinates[axis] = float(value);
			}
			const auto before = surface.frames[frame].positions[group];
			if (before.x != p.x || before.y != p.y || before.z != p.z)
				changed = true;
			// Seam copies share the position, so UV and hard-normal seams stay closed.
			for (int copy : topology.copies[group])
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
	QSet<int> around = vertices;
	for (int face : std::as_const(touched))
	{
		const auto &t = surface.triangles[face];
		around << t.a << t.b << t.c;
	}
	return refreshNormals(&surface, around, work, edit.frame);
}
} // namespace vibestudio
