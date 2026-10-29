#pragma once

#include "core/model_mdl.h"
#include "core/texture_export.h"

#include <QLocale>

namespace vibestudio
{
// Skin edit round trip helpers: an embedded MDL skin member as an image with
// its exact palette indices, any edited image mapped back to the model's
// palette as a member, and the MD2 skin-size check.

// The member as an indexed image whose colour table is the model's palette.
// Null with an error for an unknown skin or member.
[[nodiscard]] QImage modelMdlSkinMemberImage(const ModelMesh &mesh, int skin, int member, QString *error = nullptr);
// The member encoded as PCX ("pcx") or indexed PNG ("png"), copying its
// palette indices exactly. Read back with decodeModelMdlSkin, the indices and
// palette are identical.
bool exportModelMdlSkinMember(const ModelMesh &mesh, int skin, int member, const QString &format, QByteArray *bytes,
							  QString *error = nullptr, const ModelWorkControl &control = {});
// Any image of the MDL skin size as a member: an indexed image whose colour
// table is the model's palette keeps its indices; any other image maps each
// pixel to the nearest palette colour (alpha ignored). The fullbright range
// (indices 224-255) is left out under Exclude, used only for exact colour
// matches under Preserve, and treated like any entry under Allow. Use it with
// ModelEditKind::ReplaceMdlSkinMember.
bool modelMdlSkinInputFromImage(const ModelMesh &mesh, const QImage &image, TextureFullbrightMode fullbright, ModelMdlSkinInput *input,
								QString *error = nullptr, const ModelWorkControl &control = {});
// One sentence when an MD2 skin image's size differs from the model's MD2
// skin size (the Quake II software renderer indexes skins by that size);
// empty when they match.
[[nodiscard]] QString modelMd2SkinSizeMismatch(const ModelMesh &mesh, QSize image, const QLocale &locale = QLocale::c());
} // namespace vibestudio
