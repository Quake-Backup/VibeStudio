// Intersections introduced by topology tools that only check local collapse
// (bevel, rip, spin, inset, extrude edges, solidify, knife, loop cut,
// shrink/fatten, connect, dissolve). The shared inspection
// (core/model_intersections.h) scans the edited surface before and after the
// edit; a crossing counts as introduced unless both faces existed, unmoved,
// before the edit and already crossed then. Faces are matched by their corner
// positions in every pose, so renumbering does not hide or invent findings.
#include "core/model_mesh_tools_p.h"

#include "core/model_intersections.h"

#include <algorithm>
#include <array>

namespace vibestudio::model_tools
{
namespace
{
// A face's corner positions in every pose, in a canonical corner order.
QByteArray faceKey(const ModelSurface &surface, const ModelTriangle &t)
{
	std::array<int, 3> corners{t.a, t.b, t.c};
	std::array<QByteArray, 3> parts;
	for (int i = 0; i < 3; ++i)
		for (const auto &frame : surface.frames)
		{
			const auto p = frame.positions[corners[i]];
			parts[i].append(reinterpret_cast<const char *>(&p), sizeof(p));
		}
	std::sort(parts.begin(), parts.end());
	return parts[0] + parts[1] + parts[2];
}
} // namespace

bool isModelIntersectionGuardedEdit(ModelEditKind kind)
{
	switch (kind)
	{
	case ModelEditKind::BevelVertices:
	case ModelEditKind::BevelEdges:
	case ModelEditKind::Rip:
	case ModelEditKind::Spin:
	case ModelEditKind::InsetFaces:
	case ModelEditKind::ExtrudeEdges:
	case ModelEditKind::Solidify:
	case ModelEditKind::Knife:
	case ModelEditKind::LoopCut:
	case ModelEditKind::ShrinkFatten:
	case ModelEditKind::ConnectVertices:
	case ModelEditKind::ConnectEdges:
	case ModelEditKind::DissolveEdges:
	case ModelEditKind::LimitedDissolve:
		return true;
	default:
		return false;
	}
}

bool introducedModelIntersections(const ModelMesh &before, const ModelMesh &after, int surfaceIndex, QVector<QPair<int, int>> *introduced, bool *limited,
								  QString *error, const ModelWorkControl &control)
{
	introduced->clear();
	*limited = false;
	ModelIntersectionOptions options;
	options.surface = surfaceIndex;
	ModelIntersectionReport old, now;
	QString failure;
	// A scan past its work limits reports nothing rather than a partial list.
	if (!inspectModelIntersections(after, options, &now, &failure, control))
	{
		if (control.cancelled && control.cancelled())
			return fail(error, failure);
		*limited = true;
		return true;
	}
	if (now.findings.isEmpty())
		return true;
	if (!inspectModelIntersections(before, options, &old, &failure, control))
	{
		if (control.cancelled && control.cancelled())
			return fail(error, failure);
		*limited = true;
		return true;
	}
	const auto &was = before.surfaces[surfaceIndex], &is = after.surfaces[surfaceIndex];
	QSet<QPair<QByteArray, QByteArray>> crossed;
	for (const auto &finding : std::as_const(old.findings))
	{
		if (finding.firstSurface != surfaceIndex || finding.secondSurface != surfaceIndex)
			continue;
		auto a = faceKey(was, was.triangles[finding.firstFace]), b = faceKey(was, was.triangles[finding.secondFace]);
		if (b < a)
			std::swap(a, b);
		crossed.insert({a, b});
	}
	QSet<QPair<int, int>> seen;
	for (const auto &finding : std::as_const(now.findings))
	{
		// Findings against other surfaces are reported by Health; this guard
		// watches the edited surface itself.
		if (finding.firstSurface != surfaceIndex || finding.secondSurface != surfaceIndex)
			continue;
		const QPair<int, int> pair{std::min(finding.firstFace, finding.secondFace), std::max(finding.firstFace, finding.secondFace)};
		if (seen.contains(pair))
			continue;
		auto a = faceKey(is, is.triangles[pair.first]), b = faceKey(is, is.triangles[pair.second]);
		if (b < a)
			std::swap(a, b);
		if (crossed.contains({a, b}))
			continue;
		seen.insert(pair);
		introduced->append(pair);
	}
	std::sort(introduced->begin(), introduced->end());
	return true;
}
} // namespace vibestudio::model_tools
