#pragma once

// Painting a skin on the model in 3D (UVSKIN-09).
//
// A view looks at the model through a projection (the paint dialog's orbit,
// orthographic and framing the model, or the Mesh Editor viewport's own
// orthographic or perspective camera) and keeps which triangle is nearest at
// every pixel. A stroke is a list of brush positions in that view's pixels.
// Every texel of the surface's UV footprint is placed in 3D by its triangle's
// barycentric weights and projected into the view; texels whose projection
// lies under the brush and whose triangle is the visible one there (or lies
// within a depth tolerance of it, along shared edges) take the brush colour.
// Because the test is in 3D, a stroke across a UV seam paints both islands
// continuously, and an occluded back face is never touched. A mirror axis
// paints the texels whose mirrored position lies under the brush too. Painted
// texels then bleed a few texels past the footprint's edge so filtering does
// not pull in the background. Indexed (8-bit) skins take palette indices,
// never blended colours.
//
// A stamp brush takes its colour and coverage from an image centred on each
// brush position. Smudge drags the skin along the stroke: each texel under the
// brush takes the colour shown where the brush was one step earlier.
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

struct ModelEdit;
class PackageStagingModel;

struct ModelSkinPaintCamera {
	// Degrees about the model's Z axis and above its horizon.
	double yaw = 30.0;
	double pitch = 20.0;
	// 1 frames the model; larger zooms in.
	double zoom = 1.0;
	QSize size{512, 512};
};

// Model space to view pixels: a row-major 4x4 matrix whose rows give U, V, Z
// and W; the view point is (U/W, V/W) with y down, and Z/W is the depth,
// larger towards the viewer. An orthographic view has W = 1 and Z the distance
// along the eye; a perspective view has Z = 1 and W the distance in front of
// the camera, so depth is 1/distance and stays linear across the screen.
struct ModelSkinPaintProjection {
	QSize size;
	std::array<double, 16> matrix{};
	bool perspective = false;
	// Towards the viewer, for shading previews.
	ModelVec3 eye{0.0f, 0.0f, 1.0f};
	bool operator==(const ModelSkinPaintProjection& other) const
	{
		return size == other.size && matrix == other.matrix && perspective == other.perspective && eye.x == other.eye.x && eye.y == other.eye.y
			&& eye.z == other.eye.z;
	}
};

// The paint dialog's orbit view of `frame` as a projection.
bool modelSkinPaintOrbitProjection(const ModelMesh& mesh, int frame, const ModelSkinPaintCamera& camera, ModelSkinPaintProjection* projection,
	QString* error = nullptr);

struct ModelSkinPaintView {
	QSize size;
	ModelSkinPaintProjection projection;
	// Per pixel: the visible triangle (index into `triangles`) or -1, and its
	// depth.
	QVector<qint32> ids;
	QVector<float> depth;
	// Which surface and triangle each id is.
	QVector<QPair<int, int>> triangles;
	// How far behind the visible surface a texel may lie and still count as
	// visible (along shared edges), in model units.
	double tolerance = 0.0;
	// The view point of `p`; `depth` is Z/W and `w` the homogeneous W (not
	// positive behind a perspective camera).
	[[nodiscard]] QPointF project(const ModelVec3& p, double* depth = nullptr, double* w = nullptr) const;
	// Whether depth `a` lies no further from the viewer than depth `b` plus
	// the tolerance.
	[[nodiscard]] bool depthVisible(double a, double b) const;
};

bool buildModelSkinPaintView(const ModelMesh& mesh, int frame, const ModelSkinPaintCamera& camera, ModelSkinPaintView* view,
	QString* error = nullptr, const ModelWorkControl& control = {});
bool buildModelSkinPaintView(const ModelMesh& mesh, int frame, const ModelSkinPaintProjection& projection, ModelSkinPaintView* view,
	QString* error = nullptr, const ModelWorkControl& control = {});

enum class ModelSkinPaintMode {
	Mix,     // blend by the brush's coverage, as texture_paint's SourceOver
	Replace, // texels at least half covered take the colour outright
	Smudge,  // drag the skin along the stroke
};

struct ModelSkinBrush {
	double radius = 12.0; // view pixels
	QColor colour = QColor(200, 40, 40);
	// For indexed skins; -1 picks the palette entry nearest `colour`.
	int paletteIndex = -1;
	// Mix and Replace: coverage; Smudge: how much of the dragged colour
	// each step takes.
	double opacity = 1.0;
	// 0: soft to the rim; 1: hard edge.
	double hardness = 0.5;
	ModelSkinPaintMode mode = ModelSkinPaintMode::Mix;
	// -1 none; 0, 1 or 2 also paints across the model's X, Y or Z = 0 plane.
	// Smudge does not mirror.
	int mirrorAxis = -1;
	// Texels bled past the UV footprint's edge.
	int padding = 2;
	// Mix and Replace: when set, the colour and coverage come from this image,
	// scaled to the brush's diameter and centred on each brush position.
	QImage stamp;
};

struct ModelSkinPaintStroke {
	ModelSkinBrush brush;
	QVector<QPointF> samples;
	// Strokes painted through another view (the Mesh Editor viewport) carry
	// it; replays then use it instead of the strokes file's camera.
	bool hasProjection = false;
	ModelSkinPaintProjection projection;
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
// The embedded skin slot modelSurfaceIndexedSkin paints, or -1.
int modelSkinPaintMdlSlot(const ModelMesh& mesh, int surface);
// The edit that puts a painted indexed skin back into the model: it replaces
// the first member of the surface's MDL skin slot, as one undo step. False
// when the model has no MDL skin or the image does not match its size and
// palette.
bool modelSkinPaintMdlEdit(const ModelMesh& mesh, int surface, const QImage& skin, ModelEdit* edit, QString* error = nullptr);
// The bytes of a painted skin for an image path, chosen by its extension:
// png, tga, jpg or jpeg, or pcx (an indexed skin with its own palette).
bool encodeModelPaintedSkin(const QImage& skin, const QString& path, QByteArray* bytes, QString* error = nullptr);
// Stages encoded skin bytes at a package path (replacing the entry there) as
// one package operation; false, with the staging untouched, when the path is
// unsafe or the package cannot take it.
bool stageModelPaintedSkin(const QByteArray& bytes, const QString& virtualPath, PackageStagingModel* staging, QString* error = nullptr);

// A flat-shaded, point-sampled picture of the model from the view with
// `skins` on its surfaces (missing ones grey), for the paint dialog.
QImage renderModelSkinPaintPreview(const ModelMesh& mesh, int frame, const ModelSkinPaintView& view, const QHash<int, QImage>& skins,
	const QColor& background);

// Strokes files for model paint-skin: {"camera": {yaw, pitch, zoom, width,
// height}, "strokes": [{"colour": "#rrggbb", "index", "radius", "opacity",
// "hardness", "mode": "mix"|"replace"|"smudge", "mirror": "x"|"y"|"z",
// "padding", "stamp": "image path, relative to the strokes file" or
// "stampPng": "base64 PNG", "view": {"matrix": [16 numbers], "perspective",
// "width", "height", "eye": [x, y, z]}, "samples": [[x, y], ...]}].
// `baseDirectory` resolves relative stamp paths.
bool parseModelSkinPaintStrokes(const QJsonObject& object, ModelSkinPaintCamera* camera, QVector<ModelSkinPaintStroke>* strokes,
	QString* error = nullptr, const QString& baseDirectory = {});
QJsonObject modelSkinPaintStrokesJson(const ModelSkinPaintCamera& camera, const QVector<ModelSkinPaintStroke>& strokes);

} // namespace vibestudio
