#pragma once

// Painting a skin on the model in 3D (UVSKIN-09).
//
// A view looks at the model from an orbit (orthographic, framing the model)
// and keeps which triangle is nearest at every pixel. A stroke is a list of
// brush positions in that view's pixels. Every texel of the surface's UV
// footprint is placed in 3D by its triangle's barycentric weights and
// projected into the view; texels whose projection lies under the brush and
// whose triangle is the visible one there (or lies within a depth tolerance of
// it, along shared edges) take the brush colour. Because the test is in 3D, a
// stroke across a UV seam paints both islands continuously, and an occluded
// back face is never touched. A mirror axis paints the texels whose mirrored
// position lies under the brush too. Painted texels then bleed a few texels
// past the footprint's edge so filtering does not pull in the background.
// Indexed (8-bit) skins take palette indices, never blended colours.
//
// Original VibeStudio code.

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QVector>

#include <array>

namespace vibestudio {

struct ModelSkinPaintCamera {
	// Degrees about the model's Z axis and above its horizon.
	double yaw = 30.0;
	double pitch = 20.0;
	// 1 frames the model; larger zooms in.
	double zoom = 1.0;
	QSize size{512, 512};
};

struct ModelSkinPaintView {
	QSize size;
	// Model space to view: x and y in pixels (y down), z towards the viewer.
	std::array<double, 12> matrix{};
	// Per pixel: the visible triangle (index into `triangles`) or -1, and its
	// depth.
	QVector<qint32> ids;
	QVector<float> depth;
	// Which surface and triangle each id is.
	QVector<QPair<int, int>> triangles;
	// Depth tolerance for texels along shared edges, in view units.
	double tolerance = 0.0;
	[[nodiscard]] QPointF project(const ModelVec3& p, double* depth = nullptr) const;
};

bool buildModelSkinPaintView(const ModelMesh& mesh, int frame, const ModelSkinPaintCamera& camera, ModelSkinPaintView* view,
	QString* error = nullptr, const ModelWorkControl& control = {});

enum class ModelSkinPaintMode {
	Mix,     // blend by the brush's coverage, as texture_paint's SourceOver
	Replace, // texels at least half covered take the colour outright
};

struct ModelSkinBrush {
	double radius = 12.0; // view pixels
	QColor colour = QColor(200, 40, 40);
	// For indexed skins; -1 picks the palette entry nearest `colour`.
	int paletteIndex = -1;
	double opacity = 1.0;
	// 0: soft to the rim; 1: hard edge.
	double hardness = 0.5;
	ModelSkinPaintMode mode = ModelSkinPaintMode::Mix;
	// -1 none; 0, 1 or 2 also paints across the model's X, Y or Z = 0 plane.
	int mirrorAxis = -1;
	// Texels bled past the UV footprint's edge.
	int padding = 2;
};

struct ModelSkinPaintStroke {
	ModelSkinBrush brush;
	QVector<QPointF> samples;
};

struct ModelSkinPaintResult {
	QRect dirty;
	int texels = 0;
};

// Paints one stroke of `surface` at `frame` into `skin` (ARGB32 or Indexed8,
// in the surface's UV space). False with an error on cancellation or invalid
// input; the skin is untouched then.
bool paintModelSkinStroke(const ModelMesh& mesh, int surface, int frame, const ModelSkinPaintView& view, const ModelSkinPaintStroke& stroke,
	QImage* skin, ModelSkinPaintResult* result = nullptr, QString* error = nullptr, const ModelWorkControl& control = {});

// The surface's own MDL skin as an Indexed8 image with the model's palette,
// so painting writes palette indices; null when the model has none.
QImage modelSurfaceIndexedSkin(const ModelMesh& mesh, int surface);

// A flat-shaded, point-sampled picture of the model from the view with
// `skins` on its surfaces (missing ones grey), for the paint dialog.
QImage renderModelSkinPaintPreview(const ModelMesh& mesh, int frame, const ModelSkinPaintView& view, const QHash<int, QImage>& skins,
	const QColor& background);

// Strokes files for model paint-skin: {"camera": {yaw, pitch, zoom, width,
// height}, "strokes": [{"colour": "#rrggbb", "index", "radius", "opacity",
// "hardness", "mode": "mix"|"replace", "mirror": "x"|"y"|"z", "padding",
// "samples": [[x, y], ...]}]}.
bool parseModelSkinPaintStrokes(const QJsonObject& object, ModelSkinPaintCamera* camera, QVector<ModelSkinPaintStroke>* strokes,
	QString* error = nullptr);
QJsonObject modelSkinPaintStrokesJson(const ModelSkinPaintCamera& camera, const QVector<ModelSkinPaintStroke>& strokes);

} // namespace vibestudio
