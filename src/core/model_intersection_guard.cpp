// Intersections introduced by topology tools that only check local collapse
// (bevel, rip, spin, inset, extrude edges, solidify, knife, loop cut,
// shrink/fatten, connect, dissolve). Faces are matched by their corner
// positions in every pose, so renumbering does not hide or invent findings;
// the shared inspection (core/model_intersections.h) then scans only the faces
// the edit made or moved, against every face of the edited surface and of the
// other surfaces, once, after the edit.
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

bool introducedModelIntersections(const ModelMesh &before, const ModelMesh &after, int surfaceIndex, QVector<QPair<int, int>> *introduced,
								  QVector<std::array<int, 3>> *crossings, bool *limited, QString *error, const ModelWorkControl &control)
{
	introduced->clear();
	crossings->clear();
	*limited = false;
	// Faces the edit made or moved: no face of the surface had the same
	// corners in every pose before. Only pairs with one of them can be new, as
	// two faces that were there before cross now exactly when they did then.
	const auto &was = before.surfaces[surfaceIndex], &is = after.surfaces[surfaceIndex];
	QSet<QByteArray> existing;
	existing.reserve(was.triangles.size());
	for (const auto &t : was.triangles)
		existing.insert(faceKey(was, t));
	ModelIntersectionOptions options;
	options.surface = surfaceIndex;
	for (int face = 0; face < is.triangles.size(); ++face)
		if (!existing.contains(faceKey(is, is.triangles[face])))
			options.faces.insert(face);
	if (options.faces.isEmpty())
		return true;
	ModelIntersectionReport now;
	QString failure;
	// A scan past its work limits reports nothing rather than a partial list.
	if (!inspectModelIntersections(after, options, &now, &failure, control))
	{
		if (control.cancelled && control.cancelled())
			return fail(error, failure);
		*limited = true;
		return true;
	}
	for (const auto &finding : std::as_const(now.findings))
	{
		if (finding.firstSurface == surfaceIndex && finding.secondSurface == surfaceIndex)
		{
			introduced->append(qMakePair(std::min(finding.firstFace, finding.secondFace), std::max(finding.firstFace, finding.secondFace)));
			continue;
		}
		// Another surface's face against one the edit made or moved.
		const bool first = finding.firstSurface == surfaceIndex;
		crossings->append(std::array<int, 3>{first ? finding.firstFace : finding.secondFace, first ? finding.secondSurface : finding.firstSurface,
						   first ? finding.secondFace : finding.firstFace});
	}
	// Each pair once, though it may cross in several poses.
	std::sort(introduced->begin(), introduced->end());
	introduced->erase(std::unique(introduced->begin(), introduced->end()), introduced->end());
	std::sort(crossings->begin(), crossings->end());
	crossings->erase(std::unique(crossings->begin(), crossings->end()), crossings->end());
	return true;
}
} // namespace vibestudio::model_tools
