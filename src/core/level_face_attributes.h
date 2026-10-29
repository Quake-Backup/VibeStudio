#pragma once

#include "core/level_surface.h"

#include <QHash>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QVector>

namespace vibestudio {

// The Face inspector's service: the texture values of brush faces as each
// face's own format holds them (levelTextureValues() in core/level_surface.h),
// set from the inspector's offset, scale and rotation fields, and changed by
// its UV view's move, turn and stretch handles about a pivot that keeps its
// place on the texture, as TrenchBroom's Face inspector does.
// Workflow reference: TB FaceAttribsEditor, UVOffsetTool, UVOriginTool,
// UVRotateTool and UVScaleTool at 4c756be1 (GPL-3.0-or-later). Original
// implementation; see docs/CREDITS.md.

struct LevelFaceAttributesEdit {
	// Absolute values for the ticked fields, in levelTextureValues() terms:
	// texels, map units per texel (never 0) and degrees.
	bool setShiftU = false, setShiftV = false, setScaleU = false, setScaleV = false, setRotation = false;
	double shiftU = 0, shiftV = 0, scaleU = 1, scaleV = 1, rotation = 0;
	// After the values change, the offsets shift so `pivot` reads the texel it
	// read before: a turn or a stretch happens about that point, as about
	// TrenchBroom's UV origin. Any point works; the inspector uses one on the
	// face. Classic and Valve 220 offsets the correction writes are rounded to
	// 1/10000 texel, as TrenchBroom rounds them.
	bool keepPivot = false;
	LevelMapVec3 pivot;
	// Then the whole texture moves: every point's texel coordinates grow by
	// these texels, as dragging the face over the image moves it.
	QPointF moveTexels;
	[[nodiscard]] bool changesValues() const { return setShiftU || setShiftV || setScaleU || setScaleV || setRotation; }
	[[nodiscard]] bool changesSomething() const { return changesValues() || moveTexels.x() != 0 || moveTexels.y() != 0; }
};

// The face after `edit`: the values, then the pivot correction, then the
// move. A brush-primitive face needs `imageSize`. Refuses a zero or
// out-of-range value (shifts and moves within a million texels, scales within
// a million, rotations within 360,000 degrees) and a result that is not finite.
bool applyLevelFaceAttributesEdit(const LevelMapBrushFace& face, const LevelFaceAttributesEdit& edit, QSize imageSize, LevelMapBrushFace* result,
	QString* error = nullptr);

// Where `world` lands on the face's texture, in texels (a brush-primitive face
// counts repeats, so it needs `imageSize`).
bool levelFaceTexelAt(const LevelMapBrushFace& face, QSize imageSize, const LevelMapVec3& world, QPointF* texel);
// The point of a face's plane at `texel`, from its winding `points` and their
// texel coordinates `uv`, as readLevelUvLayout() gives them. False when the
// winding maps onto a line.
bool levelFaceWorldAtTexel(const QVector<LevelMapVec3>& points, const QVector<QPointF>& uv, QPointF texel, LevelMapVec3* world);

// The values the Face inspector's fields show for the targeted faces.
struct LevelFaceAttributesSummary {
	int faces = 0;
	// Faces whose values were read; the rest are brush-primitive faces with no
	// known image size or a collapsed matrix (`problem` says why).
	int readable = 0;
	int unreadable = 0;
	QString problem;
	// The first readable face's values, and which differ on another face.
	LevelTextureValues values;
	bool shiftUMixed = false, shiftVMixed = false, scaleUMixed = false, scaleVMixed = false, rotationMixed = false;
	QString material;
	bool materialMixed = false;
	int classic = 0, valve220 = 0, brushPrimitive = 0;
};
// `textureSizes` are keyed by material name in any case or slash direction.
LevelFaceAttributesSummary levelFaceAttributesSummary(const LevelMapDocument& document, const QVector<LevelSurfaceFace>& faces,
	const QHash<QString, QSize>& textureSizes = {});

// Prepares the edit on every face as one plan that commitLevelSurfaceEdit()
// makes one undo step ("Set texture values"). Quake-family maps only; at most
// 262,144 faces. Every face must exist and be visible, unlocked and inside
// the open group, and every face must take the edit, or nothing changes. An
// edit that leaves every face as it was gives a ready plan that commits
// nothing.
bool prepareLevelFaceAttributesEdit(const LevelMapDocument& document, const QVector<LevelSurfaceFace>& faces, const LevelFaceAttributesEdit& edit,
	const QHash<QString, QSize>& textureSizes, LevelSurfaceEditPlan* plan, QString* error = nullptr);

struct LevelFaceAttributesReport {
	int faces = 0;
	int changedFaces = 0;
	int brushes = 0;
};
// Prepares and commits the edit: one undo step, or none when nothing changes.
bool setLevelMapFaceAttributes(LevelMapDocument* document, const QVector<LevelSurfaceFace>& faces, const LevelFaceAttributesEdit& edit,
	const QHash<QString, QSize>& textureSizes, LevelFaceAttributesReport* report = nullptr, QString* error = nullptr);

} // namespace vibestudio
