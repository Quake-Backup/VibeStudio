// Skin painting through the Mesh Editor viewport (UVSKIN-09): the stroke hook
// and the camera as a core/model_skin_paint.h projection.

#include "app/model_viewport.h"

#include "core/model_skin_paint.h"

namespace vibestudio {

void ModelViewport::setSkinPaintHandler(std::function<bool(ModelViewportStrokePhase, const QPointF&)> handler)
{
	if (m_skinPainting && m_skinPaintHandler) {
		m_skinPaintHandler(ModelViewportStrokePhase::Cancel, commandPosition());
	}
	m_skinPainting = false;
	m_skinPaintHandler = std::move(handler);
	setCursor(m_skinPaintHandler ? Qt::CrossCursor : Qt::ArrowCursor);
}

bool ModelViewport::skinPaintProjection(ModelSkinPaintProjection* projection)
{
	if (!projection || !m_hasMesh || width() < 16 || height() < 16) {
		return false;
	}
	ensureProjection();
	const Camera& camera = m_camera;
	ModelSkinPaintProjection result;
	result.size = size();
	const auto dot3 = [](const ModelVec3& a, double x, double y, double z) { return double(a.x) * x + double(a.y) * y + double(a.z) * z; };
	if (!m_perspective) {
		// projectPoint's orthographic mapping: scale about the model centre,
		// screen Y down, depth along the eye (towards the viewer).
		const double s = camera.scale;
		const double cx = m_center.x, cy = m_center.y, cz = m_center.z;
		result.perspective = false;
		result.eye = camera.eye;
		result.matrix = {s * camera.right.x, s * camera.right.y, s * camera.right.z, camera.origin.x() - s * dot3(camera.right, cx, cy, cz),
			-s * camera.up.x, -s * camera.up.y, -s * camera.up.z, camera.origin.y() + s * dot3(camera.up, cx, cy, cz),
			double(camera.eye.x), double(camera.eye.y), double(camera.eye.z), -dot3(camera.eye, cx, cy, cz),
			0.0, 0.0, 0.0, 1.0};
	} else {
		// toView/fromView: x and y over the distance along `forward`, scaled by
		// the focal length; depth is 1/distance (Z = 1, W = distance).
		const double f = camera.focal, ox = camera.origin.x(), oy = camera.origin.y();
		const double px = camera.position.x, py = camera.position.y, pz = camera.position.z;
		const ModelVec3& r = camera.right;
		const ModelVec3& u = camera.up;
		const ModelVec3& d = camera.forward;
		result.perspective = true;
		result.eye = {-d.x, -d.y, -d.z};
		result.matrix = {ox * d.x + f * r.x, ox * d.y + f * r.y, ox * d.z + f * r.z, -(ox * dot3(d, px, py, pz) + f * dot3(r, px, py, pz)),
			oy * d.x - f * u.x, oy * d.y - f * u.y, oy * d.z - f * u.z, -(oy * dot3(d, px, py, pz) - f * dot3(u, px, py, pz)),
			0.0, 0.0, 0.0, 1.0,
			double(d.x), double(d.y), double(d.z), -dot3(d, px, py, pz)};
	}
	*projection = result;
	return true;
}

} // namespace vibestudio
