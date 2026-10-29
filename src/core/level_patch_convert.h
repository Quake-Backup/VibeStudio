#pragma once

// TrenchBroom's Convert Selection to Patches for Quake III maps: each brush
// face becomes flat patches covering it exactly, in the face's material
// with its texture mapping. Workflow reference: TB ActionManager "Menu/Edit/
// Convert Selection to Patches" and MapWindow::convertSelectionToPatches at
// 4c756be1 (GPL-3.0-or-later). Original implementation; see docs/CREDITS.md.

#include "core/level_map.h"
#include "core/level_surface.h"

#include <QHash>
#include <QSize>
#include <QStringList>
#include <QVector>

#include <functional>

namespace vibestudio {

struct LevelPatchConvertRequest {
	// Each patch's control grid: odd, 3 to 31. TrenchBroom makes 3 by 3.
	int columns = 3;
	int rows = 3;
	// Whole brushes are replaced by their faces' patches unless this is set.
	// Single faces never remove their brush.
	bool keepBrushes = false;
	// A material's image size in texels, which classic and Valve 220 faces
	// need to turn their texel coordinates into the repeats patches count.
	// An empty or missing answer counts as 64 by 64, NetRadiant Custom's
	// missing-texture size. Brush-primitive faces count repeats already.
	std::function<QSize(const QString& material)> imageSize;
};

struct LevelPatchConvertReport {
	QVector<int> patchIds;
	int faces = 0;
	int brushesRemoved = 0;
	// Faces and brushes left as they were, with the reason.
	QStringList skipped;
	// Materials mapped as 64 by 64 because no image size was found.
	QStringList unsizedMaterials;
};

// The patches that cover one face of a solved brush: a three- or four-sided
// face is one patch (a triangle's last edge collapsed to a point), and a
// larger one is split from its first corner into quadrilaterals and at most
// one triangle. Each faces out of the brush, in the face's material, with
// texture coordinates that reproduce the face's mapping at every point.
bool levelBrushFacePatches(const LevelMapBrush& brush, int faceIndex, const LevelPatchConvertRequest& request, QVector<LevelMapPatch>* patches,
	QString* error = nullptr);

// Converts `faces` (their brushes stay) and every face of `wholeBrushes`
// (removed unless request.keepBrushes) as one undo step, putting each patch
// in its brush's entity and selecting the new patches. Hidden brushes, brushes
// outside the open group and brushDef3 (idTech 4) faces are left as they were,
// with their reasons in the report; a locked brush refuses the whole edit.
bool convertLevelMapFacesToPatches(LevelMapDocument* document, const QVector<LevelSurfaceFace>& faces, const QVector<int>& wholeBrushes,
	const LevelPatchConvertRequest& request, LevelPatchConvertReport* report = nullptr, QString* error = nullptr);

} // namespace vibestudio
