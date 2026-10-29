#pragma once

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QSize>

namespace vibestudio
{
enum class ModelEditKind;
struct ModelEdit;
struct ModelSelection;

// Everyday UV operators (Weld, Stitch, Align, Snap to Texels) and the UV
// unwrap and packing choices, carried by ModelEdit::uvTools. Each operator
// is one candidate-only document edit (applyModelEdit), so undo, recovery,
// saves, Adjust Last Operation and the CLI share it.
enum class ModelUvAlignMode
{
	Minimum,
	Maximum,
	Centre,
	// Onto the line through the two selected UVs farthest apart along the
	// selection's principal direction; alignAxis is ignored.
	Straighten
};
enum class ModelUvSnapMode
{
	// (s + 0.5) / width, as the MDL and MD2 writers round UVs.
	Centres,
	Corners
};
enum class ModelUvUnwrapMethod
{
	// xatlas charting and packing (core/model_uv_atlas.h).
	Automatic,
	// Least squares conformal maps, one chart per seam-bounded island
	// (core/model_uv_unwrap.h).
	Conformal
};
struct ModelUvToolOptions
{
	// WeldUv: split corners weld when their UVs are within this distance (UV
	// units, 0 to 1) of the corner they join.
	double weldTolerance = 0;
	// AlignUv: 0 aligns U (a vertical line), 1 aligns V.
	int alignAxis = 0;
	ModelUvAlignMode alignMode = ModelUvAlignMode::Centre;
	// SnapUvTexels: an invalid size uses the MDL skin size for an MDL source,
	// else the MD2 skin size.
	QSize snapSize{};
	ModelUvSnapMode snapMode = ModelUvSnapMode::Centres;
	// PackUv, and Conformal unwrap's packing: also try islands turned a
	// quarter turn, keeping whichever fits densest. Never mirrors an island.
	bool packRotate = false;
	// UnwrapUv only.
	ModelUvUnwrapMethod unwrapMethod = ModelUvUnwrapMethod::Automatic;
	// Conformal unwrap: pack the new islands into the tile (default), or keep
	// each flattened island where it lands, scaled to its model area.
	bool unwrapPack = true;
	// PackUv: other whole surfaces packed into the same atlas as the selected
	// faces; each must share a material slot path with the active surface.
	QSet<int> packSurfaces{};
	bool operator==(const ModelUvToolOptions &) const = default;
};

// WeldUv, StitchUv, AlignUv and SnapUvTexels.
[[nodiscard]] bool isModelUvToolEdit(ModelEditKind kind);
// Applies one UV operator to the selected surface of `mesh`, updating
// `selection`. Weld joins split corners whose positions and normals match in
// every pose, with the same topology identity and skin influences, and whose
// UVs lie within the tolerance: the exact inverse of Detach UV Faces. Stitch
// moves the smaller of two islands rigidly (rotation and translation, never a
// reflection or scale) so the selected seam edges meet, then welds them.
// Align and Snap move the selected UV corners; faces sharing them follow.
// The mesh is unchanged on failure.
bool applyModelUvToolEdit(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error,
						  const ModelWorkControl &control = {});
// Refuses ModelEdit::uvTools settings on an operation that does not read them:
// the operators read their own fields, PackUv reads packRotate and
// packSurfaces, and UnwrapUv reads unwrapMethod, unwrapPack and (for a
// conformal unwrap that packs) packRotate.
bool validModelUvToolSettings(const ModelEdit &edit, QString *error = nullptr);
// The skin size Snap to Texels uses when ModelUvToolOptions::snapSize is not set.
[[nodiscard]] QSize modelUvSnapSize(const ModelMesh &mesh, QSize requested = {});
// The UV a coordinate snaps to on a `size` texel grid.
[[nodiscard]] ModelTexCoord modelUvSnapped(ModelTexCoord uv, QSize size, ModelUvSnapMode mode);
} // namespace vibestudio
