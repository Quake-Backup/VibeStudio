#pragma once

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QJsonValue>

namespace slopstudio
{
// Internal bounded structural decoder for the import-repair quarantine. The
// result is NOT an editable document: indices, normals and metadata still need
// validation. Normal source loading always uses parseEditableModel instead.
bool decodeEditableModelSource(const QByteArray &bytes, ModelMesh *mesh, QString *error, const ModelWorkControl &control);
// parseEditableModel (core/model_document.h) that also returns one top-level
// member the model itself does not read, such as a recovery copy's selection,
// from the same parse of the source. `memberValue` is undefined when the
// member is absent and is only meaningful when the function returns true.
bool parseEditableModelWithMember(const QByteArray &bytes, const QString &member, ModelMesh *mesh, QJsonValue *memberValue, QString *error,
								  const ModelWorkControl &control);
} // namespace slopstudio
