#pragma once

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QColor>
#include <QImage>
#include <QJsonObject>

namespace vibestudio
{
// UV layout (skin template) images to paint a skin over, in the studio's
// texture editor or any other. Drawn with QPainter on a QImage, on the
// caller's worker; nothing here edits the model.
inline constexpr int modelUvLayoutMaxSize = 4096;

struct ModelUvLayoutOptions
{
	QColor background = QColor(0, 0, 0, 0);
	QColor edge = QColor(0, 0, 0);
	// Island boundaries: marked seams and edges where UVs split.
	QColor seam = QColor(255, 0, 255);
	bool seams = true;
	double lineWidth = 1;
	// Off by default: crisp single-texel lines for pixel skins.
	bool antialias = false;
	// Island fill opacity (0 draws no fill), with a distinct hue per island
	// when islandTints is set, else `fill`.
	double fillOpacity = 0;
	bool islandTints = true;
	QColor fill = QColor(255, 255, 255);
	// Faces outside the 0–1 tile are drawn moved into it by whole tiles;
	// otherwise they are clipped at the image edge.
	bool wrap = false;
	// Optional image drawn first, scaled to the layout size (the material).
	QImage underlay;
};
struct ModelUvLayoutReport
{
	QSize size;
	int surfaces = 0, faces = 0, islands = 0, outsideFaces = 0, wrappedFaces = 0;
};

// Draws the UVs of `surfaces` (indices into mesh.surfaces, at least one) on a
// size.width() x size.height() image, each side 1 to modelUvLayoutMaxSize.
// Output is unchanged on failure or cancellation.
bool renderModelUvLayout(const ModelMesh &mesh, const QVector<int> &surfaces, QSize size, const ModelUvLayoutOptions &options,
						 QImage *image, ModelUvLayoutReport *report = nullptr, QString *error = nullptr, const ModelWorkControl &control = {});
// The surfaces sharing a material slot with `surface`, itself first.
[[nodiscard]] QVector<int> modelUvLayoutMaterialSurfaces(const ModelMesh &mesh, int surface);
[[nodiscard]] QJsonObject modelUvLayoutJson(const ModelUvLayoutReport &report);
} // namespace vibestudio
