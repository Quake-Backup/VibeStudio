#pragma once

#include "core/level_patch_stitch.h"
#include <QJsonObject>

namespace vibestudio
{

// Radial is VibeStudio's own cap: a planar radial fan from the boundary to a
// centre, closing a loop as a disc and an open arc on its chord. The others
// are NetRadiant Custom's Cap Selection types (radiant/patch.cpp
// Patch::ConstructSeam at 449778b3), seams built from the boundary's
// controls: Bevel fills a three-point arc to the corner opposite its middle
// control, Inverted Bevel fills it out to that middle control, Endcap closes
// a five-point half round on its chord, Inverted Endcap fills the corners
// beside it out to the line through its middle controls, and Cylinder closes
// a nine-point ring with straight chords across it.
enum class LevelPatchCapStyle { Radial, Bevel, Endcap, InvertedBevel, InvertedEndcap, Cylinder };
// "radial", "bevel", "endcap", "inverted-bevel", "inverted-endcap" and
// "cylinder", in that order.
[[nodiscard]] QStringList levelPatchCapStyleIds();
[[nodiscard]] QString levelPatchCapStyleId(LevelPatchCapStyle style);
// The translated name, as the Cap Patch dialog lists it.
[[nodiscard]] QString levelPatchCapStyleLabel(LevelPatchCapStyle style);
bool parseLevelPatchCapStyle(const QString &id, LevelPatchCapStyle *style);
// The boundary control counts a style takes, as NetRadiant Custom checks
// them: any for Radial, 3 or 5 for the bevels, 5 for the end caps and 9 for
// Cylinder. Empty means any.
[[nodiscard]] QVector<int> levelPatchCapStyleWidths(LevelPatchCapStyle style);

// Planar: Radial projects across the cap from its centre at map units per
// tile; the other styles project as Radiant's Cap Texture does, along the
// cap's facing, at map units per tile. Boundary keeps the boundary's texture
// coordinates and carries them to every other control point as its position
// is carried (Radial towards their mean at the centre).
enum class LevelPatchCapUv { Planar, Boundary };

struct LevelPatchCapRequest {
	LevelPatchCapStyle style = LevelPatchCapStyle::Radial;
	QVector<LevelPatchBoundary> boundaries{LevelPatchBoundary::FirstRow, LevelPatchBoundary::LastRow};
	QString texture; // Empty retains the source material.
	LevelPatchCapUv uv = LevelPatchCapUv::Planar;
	double unitsPerTile = 128;
	bool customCenter = false; // Radial with one boundary only; open arcs always close on their chord.
	LevelMapVec3 center{0, 0, 0, true};
	bool invert = false;
};

struct LevelPatchCapResult {
	LevelPatchCapStyle style = LevelPatchCapStyle::Radial;
	QVector<LevelMapPatch> caps;
	// Per cap: its source patch, boundary, centre (for the seam styles the
	// point the seam closes on), whether the boundary closes on itself and
	// whether its controls were taken in reverse.
	QVector<int> sources;
	QVector<LevelPatchBoundary> boundaries;
	QVector<LevelMapVec3> centers;
	QVector<bool> closed;
	QVector<bool> reversed;
	QStringList warnings;
	// Patches left uncapped by capLevelMapPatches, with the reason.
	QStringList skipped;
};

// Original constructions. Radial is a radial quadratic fan: the outer row
// retains the exact boundary curve, and planarity and monotone polar control
// directions certify that it cannot fold or cover itself. The seam styles
// follow NetRadiant Custom's control layouts and width checks. No source
// patch is modified.
bool prepareLevelPatchCaps(const LevelMapPatch &source, const LevelPatchCapRequest &request, LevelPatchCapResult *result,
						   QString *error = nullptr, const std::function<bool()> &cancelled = {});
bool capLevelMapPatch(LevelMapDocument *document, int patchId, const LevelPatchCapRequest &request, LevelPatchCapResult *result = nullptr,
					  QString *error = nullptr);
// NetRadiant Custom's Cap Selection: caps each patch, in its own entity, as
// one undo step, and selects the caps. Hidden patches, and patches the style
// cannot cap (a boundary of the wrong width, say), are left as they were,
// their reasons in result->skipped; nothing changes when none can be capped.
bool capLevelMapPatches(LevelMapDocument *document, const QVector<int> &patchIds, const LevelPatchCapRequest &request,
						LevelPatchCapResult *result = nullptr, QString *error = nullptr);
QJsonObject levelPatchCapReportJson(const LevelPatchCapResult &result);

} // namespace vibestudio
