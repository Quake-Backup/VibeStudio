#pragma once

#include "core/level_map.h"

#include <QSize>
#include <QStringList>

namespace vibestudio {

// Authorable quadratic grids have odd dimensions, matching q3map2's 31-point
// axis limit (external/compilers/vibemap3/tools/quake3/q3map2/q3map2.h).
inline constexpr int kLevelPatchMaxDimension = 31;

// How a new patch's texture coordinates are laid out. Radiant is NetRadiant
// Custom's choice on creation (Patch::ConstructPrefab at 449778b3): a plane
// takes Cap Texture and every other shape Natural. Stretch spreads one
// repeat over the whole grid (column 0 to 1 across, row 0 to 1 down).
enum class LevelPatchCreateMapping { Stretch, Natural, Cap, Radiant };
// "stretch", "natural", "cap" and "radiant", in that order.
[[nodiscard]] QStringList levelPatchCreateMappingIds();
[[nodiscard]] QString levelPatchCreateMappingId(LevelPatchCreateMapping mapping);
bool parseLevelPatchCreateMapping(const QString& id, LevelPatchCreateMapping* mapping);

// The shapes of GtkRadiant's and NetRadiant's Patch menu: plane, cylinder,
// square-cylinder, cone, classic-cone, sphere, bevel and endcap. Each fills
// the box of `center` and `size`; rows run along `axis`, columns around or
// across it. Round rings run anticlockwise and start where NetRadiant
// Custom's do (see LevelPatchRingStart).
//
// Where a round shape's ring starts. Auto follows NetRadiant Custom's Patch
// menu (radiant/patch.cpp Patch::ConstructPrefab at 449778b3): its fixed
// prefabs, the four-segment cylinder, the four-segment two-band sphere and
// the classic cone, start half a turn round, on the negative side of the
// first axis across; every other density, and the straight cone, can only be
// made by its Exact Cylinder, Sphere and Cone, which start at angle 0, on the
// positive side. Exact always starts at angle 0.
enum class LevelPatchRingStart { Auto, Exact };
// "auto" and "exact", in that order.
[[nodiscard]] QStringList levelPatchRingStartIds();
[[nodiscard]] QString levelPatchRingStartId(LevelPatchRingStart start);
bool parseLevelPatchRingStart(const QString& id, LevelPatchRingStart* start);

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
	LevelPatchRingStart ringStart = LevelPatchRingStart::Auto;
	// The texture layout. Natural and Cap need the material image's size in
	// texels; an empty size counts as 64 by 64, the size of NetRadiant
	// Custom's missing-texture image. `textureScale` is map units per texel,
	// Radiant's default texture scale (0.5 for Quake III).
	LevelPatchCreateMapping mapping = LevelPatchCreateMapping::Stretch;
	QSize imageSize;
	double textureScale = 0.5;
	// NetRadiant Custom's Square box on its Exact shapes: once mapped, every
	// span's middle control moves halfway between its ends along the columns
	// and then the rows (Radiant's Re-disperse), so the curves become flat
	// facets. The texture coordinates keep the round shape's mapping, as
	// upstream re-disperses after mapping.
	bool square = false;
};

// The shape ids createLevelPatch() takes, in menu order. "cone" tapers
// straight to its tip, as NetRadiant Custom's Exact Cone does; "classic-cone"
// is Radiant's Cone, whose rings stay at full width until the last, which
// closes to the tip, so its sides bulge.
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
