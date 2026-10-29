#pragma once

#include "core/model_mdl.h"
#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QImage>
#include <QJsonObject>

namespace vibestudio
{
// Edit Skin… round trip through the Textures workbench. The Mesh Editor
// writes the skin as a texture project (.vtexture, core/texture_project.h)
// in a folder the project owns, with the skin on an editable layer and the
// UV layout on a locked "UV guide" layer above it, and opens that file in
// Textures. When Textures saves the project, the Mesh Editor reads the skin
// back without the guide layer and offers it to the model.

// What a round-trip file belongs to, stored in the project's metadata.
struct ModelSkinRoundTripLink
{
	QString model;
	int surface = 0;
	// An embedded MDL skin member, or -1 for a skin image in the package.
	int mdlSkin = -1, mdlMember = -1;
	// The package path of an external skin image.
	QString imagePath;
	QSize size;
	bool indexed = false;
	[[nodiscard]] bool isMdlMember() const { return mdlSkin >= 0 && mdlMember >= 0; }
	bool operator==(const ModelSkinRoundTripLink &) const = default;
};
// "<model>-skin<N>-<M>.vtexture" or "<model>-<surface>-<image>.vtexture",
// file-system safe and stable for one skin.
[[nodiscard]] QString modelSkinRoundTripFileName(const ModelSkinRoundTripLink &link);
// Writes the project: `skin` (an indexed image keeps its indices and colour
// table) under a guide layer drawn from `guide` (transparent where empty),
// shown at half opacity and locked. Export settings default to PCX for an
// indexed skin, else to the image path's format. Replaces an existing file,
// keeping its previous version as the project writer's backup.
bool writeModelSkinRoundTrip(const QString &path, const QImage &skin, const QImage &guide, const ModelSkinRoundTripLink &link,
							 QString *error = nullptr, const ModelWorkControl &control = {});
// Reads a project written by writeModelSkinRoundTrip (and saved by Textures
// since) as the skin with the guide layer left out. When the only other
// visible layer is indexed, its indices come back exactly (Format_Indexed8);
// otherwise the visible layers are composited (Format_ARGB32).
bool readModelSkinRoundTrip(const QString &path, QImage *skin, ModelSkinRoundTripLink *link, QString *error = nullptr,
							const ModelWorkControl &control = {});
// An edited image as the MDL member it came from: texels whose colour still
// matches the member's palette entry keep their index, so colours the
// palette repeats survive; changed texels map to the nearest palette colour
// (core/model_skin_handoff.h, fullbright entries only for exact matches).
bool modelSkinRoundTripMdlInput(const ModelMesh &mesh, int skin, int member, const QImage &image, ModelMdlSkinInput *input,
								QString *error = nullptr, const ModelWorkControl &control = {});
[[nodiscard]] QJsonObject modelSkinRoundTripLinkJson(const ModelSkinRoundTripLink &link);
bool modelSkinRoundTripLinkFromJson(const QJsonObject &object, ModelSkinRoundTripLink *link);
} // namespace vibestudio
