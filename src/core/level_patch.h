#pragma once

#include "core/level_map.h"

namespace vibestudio {

// Authorable quadratic grids have odd dimensions, matching q3map2's 31-point
// axis limit (external/compilers/vibemap3/tools/quake3/q3map2/q3map2.h).
inline constexpr int kLevelPatchMaxDimension = 31;

// The shapes of GtkRadiant's and NetRadiant's Patch menu: plane, cylinder,
// square-cylinder, cone, sphere, bevel and endcap. Each fills the box of
// `center` and `size`; rows run along `axis`, columns around or across it.
struct LevelPatchCreateRequest {
	QString shape = QStringLiteral("plane");
	QString plane = QStringLiteral("xy");    // plane preset only: xy, xz, yz
	int columns = 3;
	// Rows along the axis of everything but a sphere, whose rows come from
	// `bands`.
	int rows = 3;
	LevelMapVec3 center{0, 0, 0, true};
	LevelMapVec3 size{128, 128, 128, true};
	QString texture = QStringLiteral("textures/common/caulk");
	// The axis a cylinder, cone or sphere stands on, and the one a bevel or
	// end cap runs along: 0 x, 1 y, 2 z.
	int axis = 2;
	// Quadratic spans around a cylinder, cone or sphere, 3 to 15. Each span
	// is a circular arc's quadratic, its middle control on the tangents at its
	// ends, as NetRadiant Custom's exact cylinders are; 4 is Radiant's
	// nine-column cylinder.
	int segments = 4;
	// Spans from pole to pole of a sphere, 2 to 15.
	int bands = 2;
};

// The shape ids createLevelPatch() takes, in menu order.
QStringList levelPatchShapeIds();
bool validateLevelPatch(const LevelMapPatch& patch, QString* error = nullptr);
// q3map2 prefixes patchDef2 shader tokens with "textures/" itself. patchDef3
// retains its material path. Normalize external material choices once on
// assignment; LevelMapPatch::textureName stores the map token thereafter.
QString levelPatchMaterialToken(const QString& material, bool patchDef3);
bool createLevelPatch(const LevelPatchCreateRequest& request, LevelMapPatch* patch, QString* error = nullptr);
// Canonical width-major .map definition. Keeps header extension fields and
// comments from sourceLines when an authored patch replaces its old block.
QStringList levelPatchDefinition(const LevelMapPatch& patch);
void refreshLevelPatchBounds(LevelMapPatch* patch);
// Each operation is transactional; invalid input leaves the patch untouched.
bool moveLevelPatchPoints(LevelMapPatch* patch, const QVector<int>& points, const LevelMapVec3& delta, double grid = 0.0,
                          QString* error = nullptr);
// De Casteljau splitting preserves the quadratic surface and its UV mapping.
bool subdivideLevelPatch(LevelMapPatch* patch, bool columns, QString* error = nullptr);
bool invertLevelPatch(LevelMapPatch* patch, QString* error = nullptr);

// The document owns persistence, revisions, selection and undo. A replacement
// keeps its id and entity owner; caller-supplied source bindings are ignored.
bool addLevelMapPatch(LevelMapDocument* document, const LevelMapPatch& patch, int* patchId = nullptr, QString* error = nullptr);
// Insert into an explicit existing owner atomically; validate all source grids
// and their serialized definitions before changing the document or selection.
bool addLevelMapPatches(LevelMapDocument* document, const QVector<LevelMapPatch>& patches, int entityId,
	QVector<int>* patchIds = nullptr, QString* error = nullptr);
bool replaceLevelMapPatch(LevelMapDocument* document, int patchId, const LevelMapPatch& patch, QString* error = nullptr);
// Validate every replacement first, then commit all changed patches in one undo
// command. Keys bind the destination; supplied ownership/source metadata is ignored.
bool replaceLevelMapPatches(LevelMapDocument* document, const QMap<int, LevelMapPatch>& patches, QString* error = nullptr);

} // namespace vibestudio
