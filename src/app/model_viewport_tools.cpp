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

void ModelViewport::setAnimationOverlayPainter(std::function<void(QPainter &)> painter)
{
	m_animationOverlayPainter = std::move(painter);
	update();
}

void ModelViewport::setEditPreviewWeights(QVector<double> weights)
{
	if (weights == m_editPreviewWeights)
		return;
	m_editPreviewWeights = std::move(weights);
	if (m_editMoveActive)
		update();
}

double ModelViewport::editPreviewWeight() const
{
	if (m_editPreviewWeights.isEmpty())
		return 1;
	const double here = m_editPreviewWeights.value(m_frame, 0);
	// Playback between frames blends the two frames' weights, as it blends
	// their positions.
	return m_frameBlend > 0 ? here + (m_editPreviewWeights.value(m_blendFrame, 0) - here) * m_frameBlend : here;
}

ModelTransform ModelViewport::previewEditTransform() const
{
	if (m_editPreviewWeights.isEmpty() || !m_editTag.isEmpty() || !m_editJoint.isEmpty())
		return m_editTransform;
	// The same weighting as the frame range transform (core/model_pose_range.cpp).
	const double w = editPreviewWeight();
	auto result = m_editTransform;
	const auto &t = m_editTransform;
	result.translation = {float(t.translation.x * w), float(t.translation.y * w), float(t.translation.z * w)};
	result.rotation = {float(t.rotation.x * w), float(t.rotation.y * w), float(t.rotation.z * w)};
	result.scale = {float(1 + (t.scale.x - 1) * w), float(1 + (t.scale.y - 1) * w), float(1 + (t.scale.z - 1) * w)};
	return result;
}
} // namespace vibestudio
