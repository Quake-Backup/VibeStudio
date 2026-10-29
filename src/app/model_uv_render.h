#pragma once

#include "core/model_document.h"

#include <QColor>
#include <QTransform>
#include <memory>

namespace vibestudio
{
enum class ModelUvFit
{
	None,
	All,
	Selection
};
struct ModelUvRenderRequest
{
	ModelSurface surface;
	ModelSelection selection;
	QImage texture;
	// Optional immutable cache produced for this exact triangle/UV/seam layout.
	std::shared_ptr<const ModelUvTopology> topology;
	QSize logicalSize;
	double pixelRatio = 1;
	QTransform camera;
	ModelUvFit fit = ModelUvFit::All;
	ModelTexCoord moveOffset;
	// While moving, selected UVs are drawn through this transform (a modal
	// scale or rotation preview) before moveOffset is added.
	QTransform moveTransform;
	bool moving = false, showVertices = false;
	QColor background, foreground, accent;
	// UV health overlay (core/model_uv_health.h): faces whose bits match
	// overlayMask are filled with overlayPattern above the texture, outlined,
	// and the 0–1 tile is drawn when showTile is set. One entry per face.
	QVector<quint8> faceOverlay;
	quint8 overlayMask = 0;
	Qt::BrushStyle overlayPattern = Qt::DiagCrossPattern;
	QColor overlayColour;
	bool showTile = false;
	// Other surfaces sharing the displayed material, drawn as dim dashed
	// wires beneath this surface's so overlaps on the shared skin show.
	QVector<ModelSurface> ghosts;
};
struct ModelUvRenderResult
{
	QImage image;
	QTransform camera;
	std::shared_ptr<const ModelUvTopology> topology;
	QRectF selectionBounds;
	bool hasSelection = false;
};
// Coordinate-only selection bounds; no frame positions or normals are read.
QRectF modelUvSelectionBounds(const ModelSurface &surface, const ModelSelection &selection, bool *available = nullptr);
// Runs on a value-only worker. Bounded to 4,194,304 pixels even for extreme aspect
// ratios. Selection is a clipped winding union; complete indexed wires share the
// antialiased 2D line painter (app/wire_lines), with seams/selection above ordinary geometry.
// Cancellation never publishes a partial image, camera or topology.
bool renderModelUv(const ModelUvRenderRequest &request, ModelUvRenderResult *result, QString *error, const ModelWorkControl &control = {});
} // namespace vibestudio
