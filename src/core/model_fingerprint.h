#pragma once

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QHash>

namespace slopstudio
{
// Opaque content identity for a validated editable model. This is an internal
// revision key, not a file checksum or a substitute for source serialization.
QByteArray modelStateFingerprint(const ModelMesh &mesh, QString *error = nullptr, const ModelWorkControl &control = {});
// The identity as builds computed it before the bulk arrays were hashed block
// by block: one stream over every array. Only for matching work an earlier
// build staged, such as a recovered session draft; it hashes the whole model
// on every call.
QByteArray modelStateLegacyFingerprint(const ModelMesh &mesh, QString *error = nullptr, const ModelWorkControl &control = {});
// The fingerprint and, from the same pass, the exact size of the mesh as a
// packed editable source (core/model_source_encoding.h): the metadata the
// fingerprint serialises anyway, plus the packed geometry arrays, whose size
// follows from their counts. Both are empty or -1 on cancellation or error.
struct ModelStateSummary
{
	QByteArray fingerprint;
	qint64 packedSourceBytes = -1;
};
// The digests of one summarised model's bulk arrays: triangles, texture
// coordinates and each pose's positions and normals. The arrays are implicitly
// shared and an entry holds its array, so an array of a later model with the
// same data pointer and length is the same array with the same content. A
// summary given the blocks of the state an edit started from hashes only the
// arrays the edit replaced: one pose for a frame-local move, instead of every
// pose of the model.
struct ModelStateBlocks
{
	template <typename T> struct Block
	{
		QVector<T> array;
		QByteArray digest;
	};
	QHash<const void *, Block<ModelVec3>> vectors;
	QHash<const void *, Block<ModelTriangle>> triangles;
	QHash<const void *, Block<ModelTexCoord>> texCoords;
	[[nodiscard]] bool isEmpty() const { return vectors.isEmpty() && triangles.isEmpty() && texCoords.isEmpty(); }
	void clear()
	{
		vectors.clear();
		triangles.clear();
		texCoords.clear();
	}
};
// `blocks`, when given, supplies the digests of arrays already hashed and
// receives exactly this mesh's blocks on success; it is left alone on
// cancellation or error. The fingerprint depends on content only, never on
// what was reused.
ModelStateSummary summarizeModelState(const ModelMesh &mesh, QString *error = nullptr, const ModelWorkControl &control = {},
									  ModelStateBlocks *blocks = nullptr);
} // namespace slopstudio
