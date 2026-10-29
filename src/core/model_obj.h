#pragma once

#include "core/model_mesh.h"

namespace vibestudio
{
// The axis an OBJ's "up" points along. Blender, Maya and most modellers
// export Y up and -Z forward; idTech models are Z up.
enum class ModelObjUpAxis
{
	Z,
	Y,
};
// What happens to `mtllib` material libraries.
enum class ModelObjMaterials
{
	// Read the library through the decode's companions (the files beside a
	// loose OBJ); a material with a diffuse map (map_Kd) binds that image
	// beside the OBJ, others keep their name. Without companions, ignore.
	Resolve,
	// Keep `usemtl` names as material paths and note the library.
	Ignore,
	// Refuse the file.
	Refuse,
};
struct ModelObjImportOptions
{
	ModelObjUpAxis up = ModelObjUpAxis::Z;
	// Applied after the axis conversion: 0.000001 to 1,000,000.
	double scale = 1;
	ModelObjMaterials materials = ModelObjMaterials::Resolve;
	// Line (l) and point (p) records and vertices no face uses are dropped
	// with a note, or refused when this is set.
	bool refuseLooseData = false;
	bool operator==(const ModelObjImportOptions &) const = default;
};

// Bounded polygonal OBJ intake. Free-form geometry, external calls and
// unrepresentable attributes fail with a line diagnostic. usemtl names a
// package-relative material unless a resolved library binds an image.
// Everything converted, ignored or dropped is listed in `notes`.
ModelMesh decodeModelObj(const QString &path, const QByteArray &bytes, const ModelObjImportOptions &options,
						 const ModelCompanionSource *companions, QStringList *notes, const ModelWorkControl &control = {});
// The defaults, without companions or notes.
ModelMesh decodeModelObj(const QString &path, const QByteArray &bytes, const ModelWorkControl &control = {});
// Exact, verified streaming from an immutable reader. Ambiguous names fail.
ModelMesh decodeModelObjFromArchive(const PackageArchiveReader &archive, const QString &path, const ModelWorkControl &control = {});
} // namespace vibestudio
