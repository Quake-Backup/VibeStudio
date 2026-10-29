#pragma once

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QHash>
#include <QJsonValue>

namespace vibestudio
{
enum class ModelEditKind;
struct ModelEdit;
struct ModelSelection;

// Pinned UV corners (ModelSurface::uvPins). A conformal unwrap keeps every
// pinned corner where it is and flattens the rest of its island around it,
// as Blender's pins do. Pins are saved with the editable source (version 17)
// and follow their corners through edits:
// - UV edits, packing and unwraps keep corner indices; a corner an unwrap or
//   a packer splits passes its pin to the copy (applyModelUvMapping).
// - Compaction, Weld UVs and Stitch UVs remap pins with the corners.
// - Any other edit that renumbers corners keeps a pin on each corner whose
//   reference-pose position and UV are unchanged; a corner the edit removed
//   or rebuilt loses its pin.

// PinUv or UnpinUv.
[[nodiscard]] bool isModelUvPinEdit(ModelEditKind kind);
// Pins or unpins the selected corners of the selected surface: selected
// vertices, the ends of selected edges and the corners of selected faces.
// Refuses a selection that would change nothing. The mesh is unchanged on
// failure.
bool applyModelUvPinEdit(ModelMesh *mesh, const ModelEdit &edit, QString *error = nullptr);

// The pins of every surface before an edit, kept cheaply (implicitly shared
// copies) so carryModelUvPins can follow them through it.
struct ModelUvPinState
{
	struct Surface
	{
		int index = -1;
		int vertexCount = 0;
		QVector<ModelTriangle> triangles;
		QVector<ModelTexCoord> texCoords;
		QVector<ModelVec3> positions;
		QVector<int> topologyVertices;
		QSet<int> pins;
	};
	QVector<Surface> surfaces;
	[[nodiscard]] bool isEmpty() const { return surfaces.isEmpty(); }
};
[[nodiscard]] ModelUvPinState captureModelUvPins(const ModelMesh &mesh);
// After an edit: keeps, remaps or drops each surface's pins as described
// above, and removes pins outside the surface.
void carryModelUvPins(const ModelUvPinState &before, ModelMesh *mesh);
// Pins of `surface` that fall in the given faces, as corner -> UV, the form
// unwrapModelUvConformal reads.
[[nodiscard]] QHash<int, ModelTexCoord> modelUvPinnedCorners(const ModelSurface &surface, const QSet<int> &faces);

// Editable source form: ascending corner indices.
[[nodiscard]] QJsonValue modelUvPinsJson(const QSet<int> &pins);
bool parseModelUvPins(const QJsonValue &value, int vertexCount, QSet<int> *pins);
} // namespace vibestudio
