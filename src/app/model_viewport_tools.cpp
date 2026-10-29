#include "app/model_viewport.h"

#include <cmath>

// Small public helpers for the Mesh Editor's modal tools and selection
// overlays. They reuse the viewport's own camera state and projection.
namespace vibestudio
{
ModelPickRay ModelViewport::viewRay(const QPointF &point)
{
	ensureProjection();
	return editRay(point);
}

bool ModelViewport::projectToView(const ModelVec3 &point, QPointF *screen)
{
	if (!m_hasMesh || !screen)
		return false;
	ensureProjection();
	double depth = 0;
	const QPointF projected = projectPoint(m_camera, point, &depth);
	if (!std::isfinite(projected.x()) || !std::isfinite(projected.y()))
		return false;
	*screen = projected;
	return true;
}

ModelVec3 ModelViewport::viewForward()
{
	ensureProjection();
	if (m_perspective)
		return viewDirection();
	return {-m_camera.eye.x, -m_camera.eye.y, -m_camera.eye.z};
}

void ModelViewport::viewAxes(ModelVec3 *right, ModelVec3 *up)
{
	ensureProjection();
	if (right)
		*right = m_camera.right;
	if (up)
		*up = m_camera.up;
}

void ModelViewport::setOverlayPainter(std::function<void(QPainter &)> painter)
{
	m_overlayPainter = std::move(painter);
	update();
}

bool ModelViewport::projectSegmentToView(const ModelVec3 &a, const ModelVec3 &b, QPointF *screenA, QPointF *screenB)
{
	if (!m_hasMesh || !screenA || !screenB)
		return false;
	ensureProjection();
	if (!projectSegment(a, b, screenA, screenB))
		return false;
	return std::isfinite(screenA->x()) && std::isfinite(screenA->y()) && std::isfinite(screenB->x()) && std::isfinite(screenB->y());
}

void ModelViewport::setTexCoordPreview(const QVector<int> &triangles, const std::function<QPointF(const ModelVec3 &)> &texCoordAt)
{
	// Back to the mesh's own coordinates first, so previews never compound.
	for (auto it = m_texCoordBackup.cbegin(); it != m_texCoordBackup.cend(); ++it)
	{
		const int surface = static_cast<int>(it.key() >> 32), vertex = static_cast<int>(it.key() & 0xffffffff);
		if (surface < m_mesh.surfaces.size() && vertex < m_mesh.surfaces[surface].texCoords.size())
			m_mesh.surfaces[surface].texCoords[vertex] = it.value();
	}
	const bool had = !m_texCoordBackup.isEmpty();
	m_texCoordBackup.clear();
	if (m_texCoordTouchedMesh != m_meshRevision)
	{
		m_texCoordTouched.clear();
		m_texCoordTouchedMesh = m_meshRevision;
	}
	if (texCoordAt)
	{
		for (const int index : triangles)
		{
			if (index < 0 || index >= m_meshTriangles.size())
				continue;
			const MeshTriangle &triangle = m_meshTriangles[index];
			if (triangle.surface < 0 || triangle.surface >= m_mesh.surfaces.size())
				continue;
			// A level mesh gives every polygon its own corners, so these
			// triangles are the only ones whose coordinates change.
			m_texCoordTouched.insert(index);
			ModelSurface &surface = m_mesh.surfaces[triangle.surface];
			if (surface.frames.isEmpty())
				continue;
			for (const int vertex : {triangle.a, triangle.b, triangle.c})
			{
				const qint64 key = (qint64(triangle.surface) << 32) | qint64(vertex);
				if (vertex < 0 || vertex >= surface.texCoords.size() || vertex >= surface.frames.first().positions.size() || m_texCoordBackup.contains(key))
					continue;
				const QPointF uv = texCoordAt(surface.frames.first().positions[vertex]);
				if (!std::isfinite(uv.x()) || !std::isfinite(uv.y()))
					continue;
				m_texCoordBackup.insert(key, surface.texCoords[vertex]);
				surface.texCoords[vertex] = {static_cast<float>(uv.x()), static_cast<float>(uv.y())};
			}
		}
	}
	if (!had && m_texCoordBackup.isEmpty())
		return;
	// Only the coordinates changed: the render worker rewrites the touched
	// triangles' uvs in its last upload, and nothing else about the view is
	// reset.
	++m_texCoordRevision;
	invalidateRaster(false);
	update();
}

void ModelViewport::clearTexCoordPreview()
{
	setTexCoordPreview({}, {});
}
} // namespace vibestudio
