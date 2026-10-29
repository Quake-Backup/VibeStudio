#pragma once

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QJsonObject>
#include <QSize>

namespace vibestudio
{
// UV health: what a skin layout gets wrong before an export or the game shows
// it. Read-only; nothing here edits a model.

// Per-face problem bits. A face can carry several.
enum ModelUvHealthFlag : quint8
{
	// The face stretches or squashes texels beyond ModelUvHealthOptions::
	// stretchLimit, against its island's average density.
	ModelUvHealthStretched = 1,
	// The face shares UV area with another face of the same winding.
	ModelUvHealthOverlapping = 2,
	// The face winds against the rest of its island: a fold.
	ModelUvHealthFlipped = 4,
	// A corner lies outside the 0–1 tile that MDL and MD2 exports require.
	ModelUvHealthOutsideTile = 8,
	ModelUvHealthCollapsedUv = 16,
	ModelUvHealthCollapsedGeometry = 32,
	// The face shares UV area only with faces of a mirrored island: stacked
	// mirror halves, often deliberate on symmetrical models.
	ModelUvHealthMirrorStacked = 64,
};

struct ModelUvHealthOptions
{
	// The original skin size: stretch is measured in texels and the texel
	// error is for this size.
	QSize textureSize{256, 256};
	int referenceFrame = 0;
	// A face whose stretch exceeds this is flagged; 1 is an exact isometry
	// up to the island's own scale.
	double stretchLimit = 1.5;
	// Bounding-box overlaps the overlap sweep may test before it stops.
	quint64 overlapComparisons = 8000000;
	// Overlapping pairs listed in the report; the counts are always complete
	// unless the comparison limit is reached.
	int listedPairs = 1000;
	bool operator==(const ModelUvHealthOptions &) const = default;
};

struct ModelUvHealthIsland
{
	int island = -1;
	QVector<int> faces;
	// Most of the island winds backwards: a deliberately mirrored island,
	// whose faces are not counted as flipped.
	bool mirrored = false;
	int flipped = 0;
	// Stretch is max(σ1/ρ, ρ/σ2) for the singular values σ of the map from the
	// reference pose to texels and the island's density ρ. Angle distortion is
	// σ1/σ2 and the area ratio σ1σ2/ρ². Means are weighted by model area.
	double minStretch = 1, maxStretch = 1, meanStretch = 1;
	double maxAngleDistortion = 1, minAreaRatio = 1, maxAreaRatio = 1;
	double density = 0;
};

// A native format's stored vertices for this model, as its writer would build
// them. `available` is false with `note` saying why (for example, MDL needs one
// surface, MDL settings and UVs inside the tile).
struct ModelUvHealthNative
{
	QString format;
	bool available = false;
	int stored = 0;
	// MDL: stored plus seam copies; MD2: distinct position and texel pairs.
	int render = 0;
	// The format's default delivery-target limit (0: none).
	int limit = 0;
	int renderLimit = 0;
	QString note;
};

struct ModelUvHealthReport
{
	int surface = 0;
	QSize textureSize;
	int referenceFrame = 0;
	double stretchLimit = 1.5;
	int faceCount = 0;
	// One entry per face: ModelUvHealthFlag bits, and stretch (1 for a
	// collapsed face).
	QVector<quint8> faceFlags;
	QVector<double> faceStretch;
	// Ascending face indices per problem.
	QVector<int> stretched, overlapping, flipped, outside, collapsedUv, collapsedGeometry, mirrorStacked;
	// Vertices used by faces whose UV lies outside the 0–1 tile.
	QVector<int> outsideVertices;
	// Overlapping face pairs (lower first), at most listedPairs of them.
	QVector<QPair<int, int>> overlapPairs;
	int overlapPairCount = 0, mirrorStackedPairCount = 0;
	bool overlapLimitReached = false;
	QVector<ModelUvHealthIsland> islands;
	int mirroredIslands = 0;
	double maxStretch = 1;
	// The largest distance, in texels of textureSize, from a used corner inside
	// the tile to the texel centre MDL and MD2 exports round it to.
	double maxTexelError = 0;
	// Set by inspectModelUvHealth only.
	QVector<ModelUvHealthNative> native;
	// GLQuake-style MDL loading flood-fills the colour connected to the first
	// skin texel (Mod_FloodFillSkin). Checked for the first embedded skin when
	// its top-left index is neither 0 nor 255: the region's size and how many
	// texels used by UVs it covers.
	bool floodFillChecked = false;
	int floodFillIndex = -1;
	int floodFillTexels = 0, floodFillUsedTexels = 0;
	[[nodiscard]] bool floodFillHazard() const { return floodFillUsedTexels > 0; }
	[[nodiscard]] QVector<int> faces(quint8 flag) const;
};

// Per-face health of one surface. `surface` needs referenceFrame positions,
// not every pose. Fails (output unchanged) on invalid input or cancellation.
bool inspectModelUvSurfaceHealth(const ModelSurface &surface, const ModelUvHealthOptions &options, ModelUvHealthReport *report,
								 QString *error = nullptr, const ModelWorkControl &control = {});
// inspectModelUvSurfaceHealth for one surface of a validated editable model,
// plus native vertex estimates and the MDL flood-fill check.
bool inspectModelUvHealth(const ModelMesh &mesh, int surface, const ModelUvHealthOptions &options, ModelUvHealthReport *report,
						  QString *error = nullptr, const ModelWorkControl &control = {});
// The skin size UV health measures against by default: the MDL skin size for
// an MDL source, else the MD2 skin size.
[[nodiscard]] QSize modelUvHealthDefaultTextureSize(const ModelMesh &mesh);
[[nodiscard]] QJsonObject modelUvHealthJson(const ModelUvHealthReport &report);
// "overlap", "flipped", "outside", "collapsed", "stretch" or "flood-fill":
// whether the report has that problem (stretch: any face over the limit).
[[nodiscard]] bool modelUvHealthHas(const ModelUvHealthReport &report, const QString &problem);
} // namespace vibestudio
