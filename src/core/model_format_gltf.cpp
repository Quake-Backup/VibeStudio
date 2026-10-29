// glTF 2.0 import (FMT-05): .gltf JSON with embedded (data: URI) or external
// .bin buffers, and binary .glb.
//
// Layouts and rules follow the Khronos glTF 2.0 specification
// (https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html, version 2.0.1,
// read 2026-10-28): the GLB header and chunks, buffers, buffer views,
// accessors (component types, normalisation, byte strides, sparse storage),
// meshes and primitives (triangle lists, strips and fans; morph targets),
// the node hierarchy (matrix or translation/rotation/scale), skins (joints
// and inverse bind matrices, the skinned mesh node's own transform ignored),
// animations (LINEAR, STEP and CUBICSPLINE samplers; the cubic Hermite form
// of Appendix C) and materials' base colour textures. These are format
// facts; no code from any glTF library is used.
//
// Conventions:
// - glTF is right-handed and Y-up with +Z forward and metres. Positions,
//   normals and joint transforms become Z-up game space as Blender's glTF
//   importer maps them, game (x, y, z) = glTF (x, -z, y), one metre to one
//   game unit, so a model authored in Blender keeps the numbers it had there.
//   Use the Mesh Editor's transform to scale it.
// - Front faces are counter-clockwise, as in the editable mesh; a node whose
//   world matrix mirrors (negative determinant) has its faces reversed.
// - Texture coordinates have v = 0 at the top of the image, like MD3 STs, so
//   they are kept as stored.
// - Skinned meshes become one skeleton of every joint node (parents first);
//   each animation that moves a joint becomes a skeletal clip sampled at a
//   whole frame rate (24, 25, 30 or 60 frames per second when every key time
//   lands on a frame of that rate, otherwise 30), then baked.
// - Without a skin, node animations and morph target weights are sampled the
//   same way into vertex-animated frames: the rest pose first, then each
//   animation as a named clip.

#include "core/model_decode_issues.h"
#include "core/model_document.h"
#include "core/model_formats_p.h"
#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace vibestudio::model_formats {

namespace {

constexpr int kMaxNodes = 65536;
constexpr int kMaxAccessors = 65536;
constexpr int kMaxBuffers = 256;
constexpr int kMaxAnimations = 1024;
constexpr qint64 kMaxAccessorValues = 32LL * 1024LL * 1024LL;
constexpr int kMaxVertexFrames = 1024;
constexpr qint64 kMaxJointPoses = 8LL * 1024LL * 1024LL;
constexpr int kMaxPrimitiveSurfaces = 2048;

// glTF (x, y, z) -> game (x, -z, y), a proper rotation, so its inverse is its
// transpose.
const ModelJointMatrix& axisMatrix()
{
	static const ModelJointMatrix matrix{{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}};
	return matrix;
}
const ModelJointMatrix& axisInverse()
{
	static const ModelJointMatrix matrix{{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f}};
	return matrix;
}
ModelVec3 toGame(const ModelVec3& v)
{
	return ModelVec3{v.x, -v.z, v.y};
}
ModelJointMatrix toGame(const ModelJointMatrix& m)
{
	return modelJointMultiply(axisMatrix(), modelJointMultiply(m, axisInverse()));
}

double determinant3(const ModelJointMatrix& m)
{
	return double(m.m[0]) * (double(m.m[5]) * m.m[10] - double(m.m[6]) * m.m[9])
		- double(m.m[1]) * (double(m.m[4]) * m.m[10] - double(m.m[6]) * m.m[8])
		+ double(m.m[2]) * (double(m.m[4]) * m.m[9] - double(m.m[5]) * m.m[8]);
}

struct Trs {
	ModelVec3 translation{0.0f, 0.0f, 0.0f};
	ModelQuat rotation{};
	ModelVec3 scale{1.0f, 1.0f, 1.0f};
};

struct Node {
	QString name;
	int parent = -1;
	QVector<int> children;
	bool hasMatrix = false;
	ModelJointMatrix matrix;
	Trs trs;
	int mesh = -1;
	int skin = -1;
	QVector<float> weights;
};

struct Accessor {
	int count = 0;
	int components = 0;
	QVector<float> values;
	[[nodiscard]] float at(int element, int component) const { return values.at(element * components + component); }
};

enum class Interpolation { Linear, Step, Cubic };

struct Sampler {
	QVector<float> times;
	Accessor output;
	Interpolation interpolation = Interpolation::Linear;
};

struct Channel {
	int sampler = -1;
	int node = -1;
	// 0 translation, 1 rotation, 2 scale, 3 weights.
	int path = 0;
};

struct Animation {
	QString name;
	QVector<Sampler> samplers;
	QVector<Channel> channels;
	float start = 0.0f;
	float end = 0.0f;
};

// One drawn primitive: a node instance of one mesh primitive.
struct Instance {
	int node = -1;
	int mesh = -1;
	int skin = -1;
	ModelSurface surface;
	// Mesh-space (glTF) attributes, before any node or axis transform.
	QVector<ModelVec3> positions;
	QVector<ModelVec3> normals;
	QVector<QVector<ModelVec3>> morphPositions;
	QVector<QVector<ModelVec3>> morphNormals;
	QVector<int> joints;
	QVector<float> weights;
	int perVertex = 0;
};

class GltfDecoder {
public:
	GltfDecoder(const QString& path, const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control, Companions& companions)
		: m_path(path)
		, m_bytes(bytes)
		, m_mesh(mesh)
		, m_control(control)
		, m_companions(companions)
		, m_work(control, ModelWorkPhase::Validating, &mesh->error)
	{
	}

	void decode()
	{
		if (!readContainer() || !readBuffers() || !readNodes() || !readInstances() || !readAnimations()) {
			return;
		}
		if (m_instances.isEmpty()) {
			fail(QCoreApplication::translate("VibeStudioModelMesh", "The glTF scene draws no triangles."));
			return;
		}
		readMaterials();
		const bool skinned = std::any_of(m_instances.cbegin(), m_instances.cend(), [](const Instance& instance) { return instance.skin >= 0; });
		if (skinned ? !buildSkeletal() : !buildVertexAnimated()) {
			return;
		}
		m_mesh->geometryAvailable = !m_mesh->surfaces.isEmpty();
	}

private:
	bool fail(const QString& message)
	{
		if (m_mesh->error.isEmpty()) {
			m_mesh->error = message;
		}
		return false;
	}

	// --- Container -----------------------------------------------------------

	bool readContainer()
	{
		QByteArray json;
		if (m_bytes.startsWith("glTF")) {
			// GLB: magic, version, length, then JSON and optional BIN chunks.
			if (m_bytes.size() < 20) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "The GLB header is truncated."));
			}
			const quint32 version = readU32(m_bytes, 4);
			const quint32 length = readU32(m_bytes, 8);
			if (version != 2) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "Unsupported GLB version %1; only glTF 2.0 is read.").arg(version));
			}
			if (qint64(length) > qint64(m_bytes.size()) || length < 20) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "The GLB declares a length past the end of the file."));
			}
			qint64 offset = 12;
			int chunk = 0;
			while (offset + 8 <= qint64(length)) {
				if (!m_work.step()) { return false; }
				const quint32 chunkLength = readU32(m_bytes, offset);
				const quint32 type = readU32(m_bytes, offset + 4);
				if (!rangeOk(m_bytes, offset + 8, chunkLength) || offset + 8 + qint64(chunkLength) > qint64(length)) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "A GLB chunk runs past the end of the file."));
				}
				const QByteArray data = m_bytes.mid(qsizetype(offset + 8), qsizetype(chunkLength));
				if (chunk == 0) {
					if (type != 0x4E4F534Au) { // "JSON"
						return fail(QCoreApplication::translate("VibeStudioModelMesh", "The GLB does not start with a JSON chunk."));
					}
					json = data;
				} else if (chunk == 1 && type == 0x004E4942u) { // "BIN\0"
					m_binChunk = data;
					m_hasBinChunk = true;
				}
				++chunk;
				offset += 8 + qint64(chunkLength);
			}
			if (chunk == 0) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "The GLB holds no JSON chunk."));
			}
		} else {
			json = m_bytes;
		}
		if (json.size() > kMaxTextBytes) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The glTF JSON is larger than 64 MiB."));
		}
		QJsonParseError parseError;
		const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
		if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The glTF JSON cannot be read: %1").arg(parseError.errorString()));
		}
		m_root = document.object();
		const QJsonObject asset = m_root.value(QStringLiteral("asset")).toObject();
		const QString version = asset.value(QStringLiteral("version")).toString();
		const QString minVersion = asset.value(QStringLiteral("minVersion")).toString();
		if (!version.startsWith(QStringLiteral("2.")) || (!minVersion.isEmpty() && minVersion != QStringLiteral("2.0"))) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "Unsupported glTF version \"%1\"; only glTF 2.0 is read.").arg(version));
		}
		m_mesh->version = 2;
		// Required extensions change how the data must be read; refuse the
		// ones this reader does not implement. KHR_mesh_quantization only
		// widens the accessor types, which are all read anyway.
		for (const QJsonValue& extension : m_root.value(QStringLiteral("extensionsRequired")).toArray()) {
			const QString name = extension.toString();
			if (name != QStringLiteral("KHR_mesh_quantization") && name != QStringLiteral("KHR_materials_emissive_strength")) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "The glTF requires the extension %1, which is not supported; export it without that extension.").arg(name));
			}
		}
		QStringList used;
		for (const QJsonValue& extension : m_root.value(QStringLiteral("extensionsUsed")).toArray()) {
			used << extension.toString();
		}
		if (!used.isEmpty()) {
			m_mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Extensions used: %1").arg(used.join(QStringLiteral(", ")));
		}
		const QString generator = asset.value(QStringLiteral("generator")).toString().left(200);
		if (!generator.isEmpty()) {
			m_mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Generator: %1").arg(generator);
		}
		return true;
	}

	bool readBuffers()
	{
		const QJsonArray buffers = m_root.value(QStringLiteral("buffers")).toArray();
		if (buffers.size() > kMaxBuffers) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The glTF declares more buffers than can be read."));
		}
		for (int index = 0; index < buffers.size(); ++index) {
			if (!m_work.step()) { return false; }
			const QJsonObject buffer = buffers.at(index).toObject();
			const qint64 length = qint64(buffer.value(QStringLiteral("byteLength")).toDouble(-1));
			if (length < 0 || length > modelDocumentMaxSourceBytes * 4LL) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF buffer %1 declares an unsupported length.").arg(index));
			}
			QByteArray data;
			const QString uri = buffer.value(QStringLiteral("uri")).toString();
			if (uri.isEmpty()) {
				if (index != 0 || !m_hasBinChunk) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF buffer %1 has no data: no URI and no GLB binary chunk.").arg(index));
				}
				data = m_binChunk;
			} else if (uri.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive)) {
				const int comma = uri.indexOf(QLatin1Char(','));
				if (comma < 0 || !uri.left(comma).endsWith(QStringLiteral(";base64"), Qt::CaseInsensitive)) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF buffer %1 has a data URI that is not base64.").arg(index));
				}
				const auto decoded = QByteArray::fromBase64Encoding(uri.mid(comma + 1).toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
				if (!decoded) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF buffer %1 has a data URI that is not valid base64.").arg(index));
				}
				data = *decoded;
			} else {
				// A file beside the model: decoded percent-escapes, never a
				// path outside the model's folder tree.
				const QString relative = QUrl::fromPercentEncoding(uri.toUtf8());
				if (relative.contains(QStringLiteral("..")) || relative.startsWith(QLatin1Char('/')) || relative.contains(QLatin1Char(':'))) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF buffer %1 names %2, outside the model's folder.").arg(index).arg(relative));
				}
				QString problem;
				if (!m_companions.canRead() || !m_companions.read(joinPath(pathDirectory(m_path), relative), &data, &problem)) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF buffer %1 (%2) could not be read: %3")
						.arg(index).arg(relative, problem.isEmpty() ? QCoreApplication::translate("VibeStudioModelMesh", "companion files cannot be read here") : problem));
				}
			}
			if (qint64(data.size()) < length) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF buffer %1 holds %2 bytes but declares %3.").arg(index).arg(data.size()).arg(length));
			}
			data.truncate(qsizetype(length));
			m_buffers.append(data);
		}
		return true;
	}

	// --- Accessors ------------------------------------------------------------

	static int componentCount(const QString& type)
	{
		static const QHash<QString, int> counts{{QStringLiteral("SCALAR"), 1}, {QStringLiteral("VEC2"), 2}, {QStringLiteral("VEC3"), 3},
			{QStringLiteral("VEC4"), 4}, {QStringLiteral("MAT2"), 4}, {QStringLiteral("MAT3"), 9}, {QStringLiteral("MAT4"), 16}};
		return counts.value(type, 0);
	}

	static int componentBytes(int componentType)
	{
		switch (componentType) {
		case 5120:
		case 5121:
			return 1;
		case 5122:
		case 5123:
			return 2;
		case 5125:
		case 5126:
			return 4;
		default:
			return 0;
		}
	}

	static float component(const QByteArray& data, qint64 offset, int componentType, bool normalized)
	{
		switch (componentType) {
		case 5120: {
			const float value = float(readI8(data, offset));
			return normalized ? std::max(value / 127.0f, -1.0f) : value;
		}
		case 5121: {
			const float value = float(readU8(data, offset));
			return normalized ? value / 255.0f : value;
		}
		case 5122: {
			const float value = float(readI16(data, offset));
			return normalized ? std::max(value / 32767.0f, -1.0f) : value;
		}
		case 5123: {
			const float value = float(readU16(data, offset));
			return normalized ? value / 65535.0f : value;
		}
		case 5125:
			return float(readU32(data, offset));
		default:
			return readF32(data, offset);
		}
	}

	// The bytes of a buffer view, checked against its buffer.
	bool viewBytes(int index, QByteArray* data, int* stride)
	{
		const QJsonArray views = m_root.value(QStringLiteral("bufferViews")).toArray();
		if (index < 0 || index >= views.size()) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "A glTF accessor names buffer view %1, which does not exist.").arg(index));
		}
		const QJsonObject view = views.at(index).toObject();
		const int buffer = view.value(QStringLiteral("buffer")).toInt(-1);
		const qint64 offset = qint64(view.value(QStringLiteral("byteOffset")).toDouble(0));
		const qint64 length = qint64(view.value(QStringLiteral("byteLength")).toDouble(-1));
		*stride = view.value(QStringLiteral("byteStride")).toInt(0);
		if (buffer < 0 || buffer >= m_buffers.size() || length < 0 || offset < 0 || !rangeOk(m_buffers.at(buffer), offset, length) || *stride < 0 || *stride > 252) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF buffer view %1 lies outside its buffer.").arg(index));
		}
		*data = m_buffers.at(buffer).mid(qsizetype(offset), qsizetype(length));
		return true;
	}

	// Reads an accessor into floats, sparse values applied; `expected` lists the
	// component counts the caller accepts (empty accepts any).
	bool readAccessor(int index, std::initializer_list<int> expected, Accessor* out)
	{
		const QJsonArray accessors = m_root.value(QStringLiteral("accessors")).toArray();
		if (index < 0 || index >= accessors.size() || accessors.size() > kMaxAccessors) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF accessor %1 does not exist.").arg(index));
		}
		const QJsonObject accessor = accessors.at(index).toObject();
		const int componentType = accessor.value(QStringLiteral("componentType")).toInt();
		const int components = componentCount(accessor.value(QStringLiteral("type")).toString());
		const int size = componentBytes(componentType);
		const qint64 count = qint64(accessor.value(QStringLiteral("count")).toDouble(-1));
		const bool normalized = accessor.value(QStringLiteral("normalized")).toBool(false);
		const qint64 accessorOffset = qint64(accessor.value(QStringLiteral("byteOffset")).toDouble(0));
		if (size == 0 || components == 0 || count < 1 || count * components > kMaxAccessorValues || accessorOffset < 0
			|| (normalized && (componentType == 5125 || componentType == 5126))) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF accessor %1 has an unsupported type, count or normalisation.").arg(index));
		}
		if (expected.size() > 0 && std::find(expected.begin(), expected.end(), components) == expected.end()) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF accessor %1 has the wrong number of components for its use.").arg(index));
		}
		out->count = int(count);
		out->components = components;
		out->values = QVector<float>(int(count * components), 0.0f);
		const qint64 element = qint64(size) * components;
		if (accessor.contains(QStringLiteral("bufferView"))) {
			QByteArray data;
			int stride = 0;
			if (!viewBytes(accessor.value(QStringLiteral("bufferView")).toInt(-1), &data, &stride)) {
				return false;
			}
			const qint64 step = stride > 0 ? stride : element;
			if (step < element || !rangeOk(data, accessorOffset, (count - 1) * step + element)) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF accessor %1 runs past the end of its buffer view.").arg(index));
			}
			for (qint64 item = 0; item < count; ++item) {
				if (!m_work.step()) { return false; }
				const qint64 base = accessorOffset + item * step;
				for (int c = 0; c < components; ++c) {
					out->values[int(item * components + c)] = component(data, base + qint64(c) * size, componentType, normalized);
				}
			}
		}
		if (accessor.contains(QStringLiteral("sparse"))) {
			const QJsonObject sparse = accessor.value(QStringLiteral("sparse")).toObject();
			const qint64 sparseCount = qint64(sparse.value(QStringLiteral("count")).toDouble(-1));
			const QJsonObject indices = sparse.value(QStringLiteral("indices")).toObject();
			const QJsonObject values = sparse.value(QStringLiteral("values")).toObject();
			const int indexType = indices.value(QStringLiteral("componentType")).toInt();
			const int indexSize = indexType == 5121 ? 1 : indexType == 5123 ? 2 : indexType == 5125 ? 4 : 0;
			QByteArray indexData, valueData;
			int unusedStride = 0;
			if (sparseCount < 1 || sparseCount > count || indexSize == 0
				|| !viewBytes(indices.value(QStringLiteral("bufferView")).toInt(-1), &indexData, &unusedStride)
				|| !viewBytes(values.value(QStringLiteral("bufferView")).toInt(-1), &valueData, &unusedStride)) {
				return m_mesh->error.isEmpty() ? fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF accessor %1 has malformed sparse storage.").arg(index)) : false;
			}
			const qint64 indexOffset = qint64(indices.value(QStringLiteral("byteOffset")).toDouble(0));
			const qint64 valueOffset = qint64(values.value(QStringLiteral("byteOffset")).toDouble(0));
			if (!rangeOk(indexData, indexOffset, sparseCount * indexSize) || !rangeOk(valueData, valueOffset, sparseCount * element)) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF accessor %1 has sparse storage past the end of its buffer views.").arg(index));
			}
			for (qint64 item = 0; item < sparseCount; ++item) {
				if (!m_work.step()) { return false; }
				const qint64 target = qint64(component(indexData, indexOffset + item * indexSize, indexType, false));
				if (target < 0 || target >= count) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF accessor %1 has a sparse index outside the accessor.").arg(index));
				}
				for (int c = 0; c < components; ++c) {
					out->values[int(target * components + c)] = component(valueData, valueOffset + item * element + qint64(c) * size, componentType, normalized);
				}
			}
		}
		return true;
	}

	// --- Nodes ----------------------------------------------------------------

	bool readNodes()
	{
		const QJsonArray nodes = m_root.value(QStringLiteral("nodes")).toArray();
		if (nodes.size() > kMaxNodes) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The glTF holds more nodes than can be read."));
		}
		const int meshCount = int(m_root.value(QStringLiteral("meshes")).toArray().size());
		const int skinCount = int(m_root.value(QStringLiteral("skins")).toArray().size());
		m_nodes.resize(nodes.size());
		for (int index = 0; index < nodes.size(); ++index) {
			if (!m_work.step()) { return false; }
			const QJsonObject object = nodes.at(index).toObject();
			Node& node = m_nodes[index];
			node.name = object.value(QStringLiteral("name")).toString().left(64);
			node.mesh = object.contains(QStringLiteral("mesh")) ? object.value(QStringLiteral("mesh")).toInt(-2) : -1;
			node.skin = object.contains(QStringLiteral("skin")) ? object.value(QStringLiteral("skin")).toInt(-2) : -1;
			if (node.mesh < -1 || node.mesh >= meshCount || node.skin < -1 || node.skin >= skinCount) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF node %1 names a mesh or skin that does not exist.").arg(index));
			}
			if (object.contains(QStringLiteral("matrix"))) {
				const QJsonArray values = object.value(QStringLiteral("matrix")).toArray();
				if (values.size() != 16) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF node %1 has a matrix without 16 values.").arg(index));
				}
				node.hasMatrix = true;
				// Column-major 4x4 into a row-major 3x4.
				for (int row = 0; row < 3; ++row) {
					for (int column = 0; column < 4; ++column) {
						node.matrix.m[row * 4 + column] = float(values.at(column * 4 + row).toDouble());
					}
				}
			} else {
				const QJsonArray t = object.value(QStringLiteral("translation")).toArray();
				const QJsonArray r = object.value(QStringLiteral("rotation")).toArray();
				const QJsonArray s = object.value(QStringLiteral("scale")).toArray();
				if (t.size() == 3) {
					node.trs.translation = ModelVec3{float(t.at(0).toDouble()), float(t.at(1).toDouble()), float(t.at(2).toDouble())};
				}
				if (r.size() == 4) {
					node.trs.rotation = ModelQuat{float(r.at(0).toDouble()), float(r.at(1).toDouble()), float(r.at(2).toDouble()), float(r.at(3).toDouble(1.0))};
				}
				if (s.size() == 3) {
					node.trs.scale = ModelVec3{float(s.at(0).toDouble(1.0)), float(s.at(1).toDouble(1.0)), float(s.at(2).toDouble(1.0))};
				}
			}
			for (const QJsonValue& weight : object.value(QStringLiteral("weights")).toArray()) {
				node.weights.append(float(weight.toDouble()));
			}
			for (const QJsonValue& child : object.value(QStringLiteral("children")).toArray()) {
				const int childIndex = child.toInt(-1);
				if (childIndex < 0 || childIndex >= nodes.size() || childIndex == index) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF node %1 names a child that does not exist.").arg(index));
				}
				node.children.append(childIndex);
			}
		}
		for (int index = 0; index < m_nodes.size(); ++index) {
			for (const int child : std::as_const(m_nodes[index].children)) {
				if (m_nodes[child].parent >= 0) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF node %1 has more than one parent.").arg(child));
				}
				m_nodes[child].parent = index;
			}
		}
		// Parents first, and no cycles: every node must be reached from a root.
		QVector<int> stack;
		for (int index = m_nodes.size() - 1; index >= 0; --index) {
			if (m_nodes.at(index).parent < 0) {
				stack.append(index);
			}
		}
		while (!stack.isEmpty()) {
			if (!m_work.step()) { return false; }
			const int node = stack.takeLast();
			m_order.append(node);
			const QVector<int>& children = m_nodes.at(node).children;
			for (auto child = children.crbegin(); child != children.crend(); ++child) {
				stack.append(*child);
			}
		}
		if (m_order.size() != m_nodes.size()) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The glTF node hierarchy has a cycle."));
		}
		// The scene to draw: `scene`, else the first, else every root.
		const QJsonArray scenes = m_root.value(QStringLiteral("scenes")).toArray();
		const int sceneIndex = m_root.value(QStringLiteral("scene")).toInt(scenes.isEmpty() ? -1 : 0);
		m_drawn = QVector<bool>(m_nodes.size(), scenes.isEmpty());
		if (!scenes.isEmpty()) {
			if (sceneIndex < 0 || sceneIndex >= scenes.size()) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "The glTF names a default scene that does not exist."));
			}
			QVector<int> roots;
			for (const QJsonValue& value : scenes.at(sceneIndex).toObject().value(QStringLiteral("nodes")).toArray()) {
				const int root = value.toInt(-1);
				if (root < 0 || root >= m_nodes.size()) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "The glTF scene names a node that does not exist."));
				}
				roots.append(root);
			}
			while (!roots.isEmpty()) {
				const int node = roots.takeLast();
				if (m_drawn.at(node)) {
					continue;
				}
				m_drawn[node] = true;
				roots += m_nodes.at(node).children;
			}
		}
		return true;
	}

	ModelJointMatrix localMatrix(const Node& node, const Trs* override = nullptr) const
	{
		if (node.hasMatrix && !override) {
			return node.matrix;
		}
		const Trs& trs = override ? *override : node.trs;
		return modelJointMatrix(trs.rotation, trs.translation, trs.scale);
	}

	// World matrices (glTF space) of every node, from local transforms.
	QVector<ModelJointMatrix> worldMatrices(const QVector<ModelJointMatrix>& local) const
	{
		QVector<ModelJointMatrix> world(m_nodes.size());
		for (const int node : m_order) {
			const int parent = m_nodes.at(node).parent;
			world[node] = parent >= 0 ? modelJointMultiply(world.at(parent), local.at(node)) : local.at(node);
		}
		return world;
	}

	QVector<ModelJointMatrix> restLocals() const
	{
		QVector<ModelJointMatrix> local(m_nodes.size());
		for (int node = 0; node < m_nodes.size(); ++node) {
			local[node] = localMatrix(m_nodes.at(node));
		}
		return local;
	}

	// --- Meshes ---------------------------------------------------------------

	bool readInstances()
	{
		const QJsonArray meshes = m_root.value(QStringLiteral("meshes")).toArray();
		QSet<QString> names;
		int omittedModes = 0;
		for (const int nodeIndex : std::as_const(m_order)) {
			if (!m_work.step()) { return false; }
			const Node& node = m_nodes.at(nodeIndex);
			if (node.mesh < 0 || !m_drawn.at(nodeIndex)) {
				continue;
			}
			const QJsonObject mesh = meshes.at(node.mesh).toObject();
			const QJsonArray primitives = mesh.value(QStringLiteral("primitives")).toArray();
			QString baseName = node.name.isEmpty() ? mesh.value(QStringLiteral("name")).toString().left(64) : node.name;
			if (baseName.isEmpty()) {
				baseName = QStringLiteral("mesh%1").arg(node.mesh);
			}
			QVector<float> defaultWeights = node.weights;
			if (defaultWeights.isEmpty()) {
				for (const QJsonValue& weight : mesh.value(QStringLiteral("weights")).toArray()) {
					defaultWeights.append(float(weight.toDouble()));
				}
			}
			for (int p = 0; p < primitives.size(); ++p) {
				if (!m_work.step()) { return false; }
				if (m_instances.size() >= kMaxPrimitiveSurfaces) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "The glTF draws more primitives than can be read."));
				}
				const QJsonObject primitive = primitives.at(p).toObject();
				const int mode = primitive.value(QStringLiteral("mode")).toInt(4);
				if (mode < 4 || mode > 6) {
					++omittedModes;
					continue;
				}
				Instance instance;
				instance.node = nodeIndex;
				instance.mesh = node.mesh;
				instance.skin = node.skin;
				QString name = primitives.size() > 1 ? QStringLiteral("%1_%2").arg(baseName).arg(p) : baseName;
				for (int suffix = 1; names.contains(name.toLower()); ++suffix) {
					name = QStringLiteral("%1_%2").arg(baseName.left(56)).arg(suffix);
				}
				names.insert(name.toLower());
				instance.surface.name = name;
				if (!readPrimitive(primitive, mode, defaultWeights, &instance)) {
					return false;
				}
				const int material = primitive.value(QStringLiteral("material")).toInt(-1);
				m_materialOfInstance.append(material);
				m_instances.append(std::move(instance));
			}
		}
		if (omittedModes > 0) {
			noteModelDecodeIssue(m_mesh, ModelDecodeIssueSeverity::Omission, QStringLiteral("gltf-primitive-mode"),
				QCoreApplication::translate("VibeStudioModelMesh", "%n point or line primitive(s) were left out; only triangles are read.", nullptr, omittedModes));
		}
		return true;
	}

	bool readPrimitive(const QJsonObject& primitive, int mode, const QVector<float>& defaultWeights, Instance* instance)
	{
		const QJsonObject attributes = primitive.value(QStringLiteral("attributes")).toObject();
		if (!attributes.contains(QStringLiteral("POSITION"))) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF primitive %1 has no positions.").arg(instance->surface.name));
		}
		Accessor positions;
		if (!readAccessor(attributes.value(QStringLiteral("POSITION")).toInt(-1), {3}, &positions)) {
			return false;
		}
		if (positions.count > kMaxSurfaceVertices) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF primitive %1 has more vertices than can be decoded.").arg(instance->surface.name));
		}
		const int count = positions.count;
		instance->positions.resize(count);
		for (int v = 0; v < count; ++v) {
			instance->positions[v] = ModelVec3{positions.at(v, 0), positions.at(v, 1), positions.at(v, 2)};
			if (!isFinite(instance->positions.at(v))) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF primitive %1 has a position that is not a finite number.").arg(instance->surface.name));
			}
		}
		const auto vectorAttribute = [&](const QString& key, QVector<ModelVec3>* out) {
			Accessor accessor;
			if (!readAccessor(attributes.value(key).toInt(-1), {3}, &accessor)) {
				return false;
			}
			if (accessor.count != count) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF primitive %1 has attributes of different lengths.").arg(instance->surface.name));
			}
			out->resize(count);
			for (int v = 0; v < count; ++v) {
				ModelVec3 value{accessor.at(v, 0), accessor.at(v, 1), accessor.at(v, 2)};
				(*out)[v] = isFinite(value) ? value : ModelVec3{};
			}
			return true;
		};
		if (attributes.contains(QStringLiteral("NORMAL")) && !vectorAttribute(QStringLiteral("NORMAL"), &instance->normals)) {
			return false;
		}
		instance->surface.texCoords.resize(count);
		if (attributes.contains(QStringLiteral("TEXCOORD_0"))) {
			Accessor uv;
			if (!readAccessor(attributes.value(QStringLiteral("TEXCOORD_0")).toInt(-1), {2}, &uv)) {
				return false;
			}
			if (uv.count != count) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF primitive %1 has attributes of different lengths.").arg(instance->surface.name));
			}
			for (int v = 0; v < count; ++v) {
				const float u = uv.at(v, 0), t = uv.at(v, 1);
				instance->surface.texCoords[v] = ModelTexCoord{std::isfinite(u) ? u : 0.0f, std::isfinite(t) ? t : 0.0f};
			}
		} else {
			m_missingUvs = true;
		}
		// Skin influences: up to eight per vertex (JOINTS_0/1, WEIGHTS_0/1).
		if (instance->skin >= 0 && attributes.contains(QStringLiteral("JOINTS_0")) && attributes.contains(QStringLiteral("WEIGHTS_0"))) {
			const int sets = attributes.contains(QStringLiteral("JOINTS_1")) && attributes.contains(QStringLiteral("WEIGHTS_1")) ? 2 : 1;
			instance->perVertex = sets * 4;
			instance->joints = QVector<int>(count * instance->perVertex, 0);
			instance->weights = QVector<float>(count * instance->perVertex, 0.0f);
			for (int set = 0; set < sets; ++set) {
				Accessor joints, weights;
				if (!readAccessor(attributes.value(QStringLiteral("JOINTS_%1").arg(set)).toInt(-1), {4}, &joints)
					|| !readAccessor(attributes.value(QStringLiteral("WEIGHTS_%1").arg(set)).toInt(-1), {4}, &weights)) {
					return false;
				}
				if (joints.count != count || weights.count != count) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF primitive %1 has attributes of different lengths.").arg(instance->surface.name));
				}
				for (int v = 0; v < count; ++v) {
					for (int slot = 0; slot < 4; ++slot) {
						const int at = v * instance->perVertex + set * 4 + slot;
						const float joint = joints.at(v, slot);
						const float weight = weights.at(v, slot);
						instance->joints[at] = joint >= 0.0f && joint < 65536.0f ? int(joint) : -1;
						instance->weights[at] = std::isfinite(weight) && weight > 0.0f ? weight : 0.0f;
					}
				}
			}
		} else if (instance->skin >= 0) {
			noteModelDecodeIssue(m_mesh, ModelDecodeIssueSeverity::Advisory, QStringLiteral("gltf-unskinned-primitive"),
				QCoreApplication::translate("VibeStudioModelMesh", "glTF primitive %1 belongs to a skinned node but has no joints or weights; it stays in its rest pose.").arg(instance->surface.name));
			instance->skin = -1;
		}
		// Morph targets: position and normal deltas, mixed by weight.
		const QJsonArray targets = primitive.value(QStringLiteral("targets")).toArray();
		for (int target = 0; target < targets.size(); ++target) {
			if (!m_work.step()) { return false; }
			const QJsonObject object = targets.at(target).toObject();
			QVector<ModelVec3> deltaPositions(count), deltaNormals;
			if (object.contains(QStringLiteral("POSITION"))) {
				Accessor accessor;
				if (!readAccessor(object.value(QStringLiteral("POSITION")).toInt(-1), {3}, &accessor)) {
					return false;
				}
				if (accessor.count != count) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF primitive %1 has a morph target of a different length.").arg(instance->surface.name));
				}
				for (int v = 0; v < count; ++v) {
					const ModelVec3 delta{accessor.at(v, 0), accessor.at(v, 1), accessor.at(v, 2)};
					deltaPositions[v] = isFinite(delta) ? delta : ModelVec3{};
				}
			}
			if (object.contains(QStringLiteral("NORMAL"))) {
				Accessor accessor;
				if (!readAccessor(object.value(QStringLiteral("NORMAL")).toInt(-1), {3}, &accessor) || accessor.count != count) {
					return m_mesh->error.isEmpty() ? fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF primitive %1 has a morph target of a different length.").arg(instance->surface.name)) : false;
				}
				deltaNormals.resize(count);
				for (int v = 0; v < count; ++v) {
					const ModelVec3 delta{accessor.at(v, 0), accessor.at(v, 1), accessor.at(v, 2)};
					deltaNormals[v] = isFinite(delta) ? delta : ModelVec3{};
				}
			}
			instance->morphPositions.append(deltaPositions);
			instance->morphNormals.append(deltaNormals);
		}
		instance->surface.vertexCount = count;
		m_defaultWeights.append(defaultWeights);
		// Triangles: a list, a strip or a fan.
		QVector<int> indices;
		if (primitive.contains(QStringLiteral("indices"))) {
			Accessor accessor;
			if (!readAccessor(primitive.value(QStringLiteral("indices")).toInt(-1), {1}, &accessor)) {
				return false;
			}
			indices.resize(accessor.count);
			for (int i = 0; i < accessor.count; ++i) {
				indices[i] = int(accessor.values.at(i));
			}
		} else {
			indices.resize(count);
			std::iota(indices.begin(), indices.end(), 0);
		}
		int skipped = 0;
		const auto add = [&](int a, int b, int c) {
			if (a < 0 || b < 0 || c < 0 || a >= count || b >= count || c >= count) {
				++skipped;
				return;
			}
			if (a == b || b == c || a == c) {
				return; // degenerate strip connectors draw nothing
			}
			instance->surface.triangles.append(ModelTriangle{a, b, c});
		};
		if (mode == 4) {
			for (int i = 0; i + 2 < indices.size(); i += 3) {
				add(indices.at(i), indices.at(i + 1), indices.at(i + 2));
			}
		} else if (mode == 5) {
			for (int i = 0; i + 2 < indices.size(); ++i) {
				if (i % 2 == 0) {
					add(indices.at(i), indices.at(i + 1), indices.at(i + 2));
				} else {
					add(indices.at(i + 1), indices.at(i), indices.at(i + 2));
				}
			}
		} else {
			for (int i = 1; i + 1 < indices.size(); ++i) {
				add(indices.at(0), indices.at(i), indices.at(i + 1));
			}
		}
		if (instance->surface.triangles.size() > kMaxSurfaceTriangles) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF primitive %1 has more triangles than can be decoded.").arg(instance->surface.name));
		}
		if (skipped > 0) {
			instance->surface.warnings << QCoreApplication::translate("VibeStudioModelMesh", "%n triangle(s) referenced out-of-range indices and were dropped.", nullptr, skipped);
		}
		return true;
	}

	void readMaterials()
	{
		const QJsonArray materials = m_root.value(QStringLiteral("materials")).toArray();
		const QJsonArray textures = m_root.value(QStringLiteral("textures")).toArray();
		const QJsonArray images = m_root.value(QStringLiteral("images")).toArray();
		int embedded = 0;
		for (int i = 0; i < m_instances.size(); ++i) {
			const int material = m_materialOfInstance.at(i);
			if (material < 0 || material >= materials.size()) {
				continue;
			}
			const QJsonObject object = materials.at(material).toObject();
			QString skin;
			const int texture = object.value(QStringLiteral("pbrMetallicRoughness")).toObject().value(QStringLiteral("baseColorTexture")).toObject()
				.value(QStringLiteral("index")).toInt(-1);
			if (texture >= 0 && texture < textures.size()) {
				const int image = textures.at(texture).toObject().value(QStringLiteral("source")).toInt(-1);
				if (image >= 0 && image < images.size()) {
					const QJsonObject imageObject = images.at(image).toObject();
					const QString uri = imageObject.value(QStringLiteral("uri")).toString();
					if (!uri.isEmpty() && !uri.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive)) {
						skin = QUrl::fromPercentEncoding(uri.toUtf8());
					} else {
						++embedded;
						skin = imageObject.value(QStringLiteral("name")).toString();
					}
				}
			}
			if (skin.isEmpty()) {
				skin = object.value(QStringLiteral("name")).toString();
			}
			skin = skin.left(256);
			if (!skin.isEmpty()) {
				m_instances[i].surface.skinPaths.append(skin);
				if (!m_mesh->skinPaths.contains(skin)) {
					m_mesh->skinPaths.append(skin);
				}
			}
		}
		m_mesh->skinCount = m_mesh->skinPaths.size();
		if (embedded > 0) {
			noteModelDecodeIssue(m_mesh, ModelDecodeIssueSeverity::Advisory, QStringLiteral("gltf-embedded-image"),
				QCoreApplication::translate("VibeStudioModelMesh", "%n material(s) use images stored inside the glTF; their names are kept as materials, but the images are not extracted.", nullptr, embedded));
		}
		if (m_missingUvs) {
			noteModelDecodeIssue(m_mesh, ModelDecodeIssueSeverity::Advisory, QStringLiteral("gltf-no-uvs"),
				QCoreApplication::translate("VibeStudioModelMesh", "Some glTF primitives have no texture coordinates; they are set to zero."));
		}
	}

	// --- Animations -----------------------------------------------------------

	bool readAnimations()
	{
		const QJsonArray animations = m_root.value(QStringLiteral("animations")).toArray();
		if (animations.size() > kMaxAnimations) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The glTF holds more animations than can be read."));
		}
		for (int index = 0; index < animations.size(); ++index) {
			if (!m_work.step()) { return false; }
			const QJsonObject object = animations.at(index).toObject();
			Animation animation;
			animation.name = object.value(QStringLiteral("name")).toString().left(48);
			if (animation.name.isEmpty()) {
				animation.name = QStringLiteral("animation%1").arg(index);
			}
			const QJsonArray samplers = object.value(QStringLiteral("samplers")).toArray();
			bool first = true;
			for (const QJsonValue& value : samplers) {
				const QJsonObject sampler = value.toObject();
				Sampler parsed;
				Accessor input;
				if (!readAccessor(sampler.value(QStringLiteral("input")).toInt(-1), {1}, &input)
					|| !readAccessor(sampler.value(QStringLiteral("output")).toInt(-1), {}, &parsed.output)) {
					return false;
				}
				parsed.times = input.values;
				for (int key = 0; key < parsed.times.size(); ++key) {
					if (!std::isfinite(parsed.times.at(key)) || parsed.times.at(key) < 0.0f || (key > 0 && parsed.times.at(key) < parsed.times.at(key - 1))) {
						return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF animation \"%1\" has key times that are not increasing.").arg(animation.name));
					}
				}
				const QString interpolation = sampler.value(QStringLiteral("interpolation")).toString(QStringLiteral("LINEAR"));
				parsed.interpolation = interpolation == QStringLiteral("STEP") ? Interpolation::Step
					: interpolation == QStringLiteral("CUBICSPLINE") ? Interpolation::Cubic : Interpolation::Linear;
				animation.start = first ? parsed.times.first() : std::min(animation.start, parsed.times.first());
				animation.end = first ? parsed.times.last() : std::max(animation.end, parsed.times.last());
				first = false;
				animation.samplers.append(parsed);
			}
			for (const QJsonValue& value : object.value(QStringLiteral("channels")).toArray()) {
				const QJsonObject channel = value.toObject();
				const QJsonObject target = channel.value(QStringLiteral("target")).toObject();
				Channel parsed;
				parsed.sampler = channel.value(QStringLiteral("sampler")).toInt(-1);
				parsed.node = target.value(QStringLiteral("node")).toInt(-1);
				const QString path = target.value(QStringLiteral("path")).toString();
				parsed.path = path == QStringLiteral("translation") ? 0 : path == QStringLiteral("rotation") ? 1 : path == QStringLiteral("scale") ? 2
					: path == QStringLiteral("weights") ? 3 : -1;
				if (parsed.path < 0 || parsed.node < 0) {
					continue; // a pointer extension or an unknown path: nothing to drive here
				}
				if (parsed.sampler < 0 || parsed.sampler >= animation.samplers.size() || parsed.node >= m_nodes.size()) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF animation \"%1\" has a channel that names a missing sampler or node.").arg(animation.name));
				}
				const Sampler& sampler = animation.samplers.at(parsed.sampler);
				const int width = parsed.path == 1 ? 4 : parsed.path == 3 ? 0 : 3;
				const int keys = int(sampler.times.size()) * (sampler.interpolation == Interpolation::Cubic ? 3 : 1);
				if (width > 0 && (sampler.output.components != width || sampler.output.count != keys)) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF animation \"%1\" has output values that do not match its key times.").arg(animation.name));
				}
				if (width == 0 && (sampler.output.components != 1 || keys == 0 || sampler.output.count % keys != 0)) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF animation \"%1\" has morph weights that do not match its key times.").arg(animation.name));
				}
				animation.channels.append(parsed);
			}
			if (!animation.channels.isEmpty() && animation.end >= animation.start) {
				m_animations.append(animation);
			}
		}
		return true;
	}

	// The value of a sampler at `time`, `width` floats per key, as glTF's
	// interpolation rules give it (Appendix C for CUBICSPLINE).
	static QVector<float> sample(const Sampler& sampler, float time, int width, bool rotation)
	{
		const QVector<float>& times = sampler.times;
		const bool cubic = sampler.interpolation == Interpolation::Cubic;
		const auto value = [&](int key, int part) {
			QVector<float> out(width);
			const int element = cubic ? key * 3 + part : key;
			for (int c = 0; c < width; ++c) {
				out[c] = sampler.output.values.at(element * width + c);
			}
			return out;
		};
		if (times.size() == 1 || time <= times.first()) {
			return value(0, 1);
		}
		if (time >= times.last()) {
			return value(int(times.size()) - 1, 1);
		}
		const int next = int(std::upper_bound(times.cbegin(), times.cend(), time) - times.cbegin());
		const int key = next - 1;
		const float span = times.at(next) - times.at(key);
		const float s = span > 0.0f ? (time - times.at(key)) / span : 0.0f;
		if (sampler.interpolation == Interpolation::Step) {
			return value(key, 1);
		}
		QVector<float> out(width);
		if (cubic) {
			const QVector<float> v0 = value(key, 1), b0 = value(key, 2), a1 = value(next, 0), v1 = value(next, 1);
			const float s2 = s * s, s3 = s2 * s;
			for (int c = 0; c < width; ++c) {
				out[c] = (2 * s3 - 3 * s2 + 1) * v0.at(c) + (s3 - 2 * s2 + s) * span * b0.at(c) + (-2 * s3 + 3 * s2) * v1.at(c) + (s3 - s2) * span * a1.at(c);
			}
		} else if (rotation) {
			const QVector<float> a = value(key, 1), b = value(next, 1);
			const ModelQuat q = modelQuatSlerp(ModelQuat{a.at(0), a.at(1), a.at(2), a.at(3)}, ModelQuat{b.at(0), b.at(1), b.at(2), b.at(3)}, s);
			return {q.x, q.y, q.z, q.w};
		} else {
			const QVector<float> a = value(key, 1), b = value(next, 1);
			for (int c = 0; c < width; ++c) {
				out[c] = a.at(c) + (b.at(c) - a.at(c)) * s;
			}
		}
		if (rotation) {
			const ModelQuat q = modelQuatNormalized(ModelQuat{out.at(0), out.at(1), out.at(2), out.at(3)});
			return {q.x, q.y, q.z, q.w};
		}
		return out;
	}

	// The whole frame rate whose frames every key time lands on, else 30.
	static int frameRate(const Animation& animation)
	{
		for (const int rate : {24, 25, 30, 60}) {
			bool aligned = true;
			for (const Sampler& sampler : animation.samplers) {
				for (const float time : sampler.times) {
					const double frames = double(time - animation.start) * rate;
					if (std::abs(frames - std::round(frames)) > 1e-3) {
						aligned = false;
						break;
					}
				}
				if (!aligned) {
					break;
				}
			}
			if (aligned) {
				return rate;
			}
		}
		return 30;
	}

	// Local matrices and morph weights at `time` of `animation`.
	void pose(const Animation& animation, float time, QVector<ModelJointMatrix>* locals, QHash<int, QVector<float>>* weights) const
	{
		QVector<Trs> trs(m_nodes.size());
		QVector<bool> moved(m_nodes.size(), false);
		for (int node = 0; node < m_nodes.size(); ++node) {
			trs[node] = m_nodes.at(node).trs;
			if (m_nodes.at(node).hasMatrix) {
				// A matrix node is never a channel target (the specification
				// forbids it); decompose it so it can still be posed.
				const ModelJointMatrix& m = m_nodes.at(node).matrix;
				trs[node].translation = modelJointTranslation(m);
				trs[node].rotation = modelJointRotation(m);
				trs[node].scale = modelJointScale(m);
			}
		}
		for (const Channel& channel : animation.channels) {
			const Sampler& sampler = animation.samplers.at(channel.sampler);
			if (channel.path == 3) {
				const int keys = int(sampler.times.size()) * (sampler.interpolation == Interpolation::Cubic ? 3 : 1);
				const int targets = sampler.output.count / keys;
				Sampler scaled = sampler;
				scaled.output.components = targets;
				(*weights)[channel.node] = sample(scaled, time, targets, false);
				continue;
			}
			const QVector<float> v = sample(sampler, time, channel.path == 1 ? 4 : 3, channel.path == 1);
			moved[channel.node] = true;
			if (channel.path == 0) {
				trs[channel.node].translation = ModelVec3{v.at(0), v.at(1), v.at(2)};
			} else if (channel.path == 1) {
				trs[channel.node].rotation = ModelQuat{v.at(0), v.at(1), v.at(2), v.at(3)};
			} else {
				trs[channel.node].scale = ModelVec3{v.at(0), v.at(1), v.at(2)};
			}
		}
		locals->resize(m_nodes.size());
		for (int node = 0; node < m_nodes.size(); ++node) {
			(*locals)[node] = moved.at(node) ? localMatrix(m_nodes.at(node), &trs.at(node)) : localMatrix(m_nodes.at(node));
		}
	}

	// --- Skeletal models --------------------------------------------------------

	bool buildSkeletal()
	{
		const QJsonArray skins = m_root.value(QStringLiteral("skins")).toArray();
		// Every joint of every skin a drawn node uses, ordered parents first.
		QSet<int> used, jointNodes;
		for (const Instance& instance : std::as_const(m_instances)) {
			if (instance.skin >= 0) {
				used.insert(instance.skin);
			}
		}
		QHash<int, ModelJointMatrix> inverseBind;
		QVector<QVector<int>> skinJoints(skins.size());
		bool conflictingBinds = false;
		for (const int skin : std::as_const(used)) {
			const QJsonObject object = skins.at(skin).toObject();
			for (const QJsonValue& value : object.value(QStringLiteral("joints")).toArray()) {
				const int node = value.toInt(-1);
				if (node < 0 || node >= m_nodes.size()) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF skin %1 names a joint node that does not exist.").arg(skin));
				}
				skinJoints[skin].append(node);
				jointNodes.insert(node);
			}
			if (skinJoints.at(skin).isEmpty() || skinJoints.at(skin).size() > kMaxJoints) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF skin %1 has no joints or more than can be read.").arg(skin));
			}
			Accessor matrices;
			if (object.contains(QStringLiteral("inverseBindMatrices"))) {
				if (!readAccessor(object.value(QStringLiteral("inverseBindMatrices")).toInt(-1), {16}, &matrices)) {
					return false;
				}
				if (matrices.count < skinJoints.at(skin).size()) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF skin %1 has fewer inverse bind matrices than joints.").arg(skin));
				}
			}
			for (int j = 0; j < skinJoints.at(skin).size(); ++j) {
				ModelJointMatrix m;
				if (matrices.count > 0) {
					for (int row = 0; row < 3; ++row) {
						for (int column = 0; column < 4; ++column) {
							m.m[row * 4 + column] = matrices.at(j, column * 4 + row);
						}
					}
				}
				const int node = skinJoints.at(skin).at(j);
				if (inverseBind.contains(node)) {
					conflictingBinds = conflictingBinds || modelJointMatrixDistance(inverseBind.value(node), m) > 1e-4;
				} else {
					inverseBind.insert(node, m);
				}
			}
		}
		if (conflictingBinds) {
			noteModelDecodeIssue(m_mesh, ModelDecodeIssueSeverity::Advisory, QStringLiteral("gltf-skin-binds"),
				QCoreApplication::translate("VibeStudioModelMesh", "Skins that share a joint give it different bind poses; the first skin's bind pose is used."));
		}
		ModelSkeleton& skeleton = m_mesh->skeleton;
		skeleton.sourceFormat = QStringLiteral("gltf");
		QHash<int, int> jointOfNode;
		QSet<QString> jointNames;
		bool renamed = false;
		for (const int node : std::as_const(m_order)) {
			if (!jointNodes.contains(node)) {
				continue;
			}
			ModelJoint joint;
			QString name = m_nodes.at(node).name.isEmpty() ? QStringLiteral("joint%1").arg(node) : m_nodes.at(node).name;
			for (int suffix = 1; jointNames.contains(name.toLower()); ++suffix) {
				name = QStringLiteral("%1_%2").arg(m_nodes.at(node).name.left(56)).arg(suffix);
				renamed = true;
			}
			jointNames.insert(name.toLower());
			joint.name = name;
			for (int ancestor = m_nodes.at(node).parent; ancestor >= 0; ancestor = m_nodes.at(ancestor).parent) {
				if (jointOfNode.contains(ancestor)) {
					joint.parent = jointOfNode.value(ancestor);
					break;
				}
			}
			bool inverted = false;
			joint.bind = toGame(modelJointInverse(inverseBind.value(node), &inverted));
			if (!inverted || !modelJointMatrixIsFinite(joint.bind)) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF joint \"%1\" has an inverse bind matrix that cannot be inverted.").arg(name));
			}
			jointOfNode.insert(node, int(skeleton.joints.size()));
			skeleton.joints.append(joint);
		}
		if (renamed) {
			noteModelDecodeIssue(m_mesh, ModelDecodeIssueSeverity::Advisory, QStringLiteral("gltf-duplicate-joints"),
				QCoreApplication::translate("VibeStudioModelMesh", "Some glTF joints share a name; the repeats were given a numbered suffix."));
		}
		m_mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Joints: %1").arg(skeleton.joints.size());

		const QVector<ModelJointMatrix> restWorld = worldMatrices(restLocals());
		int morphOmitted = 0;
		for (int i = 0; i < m_instances.size(); ++i) {
			if (!m_work.step()) { return false; }
			Instance& instance = m_instances[i];
			ModelSurface surface = instance.surface;
			surface.index = int(m_mesh->surfaces.size());
			const QVector<ModelVec3> base = morphed(instance, m_defaultWeights.at(i), false);
			const QVector<ModelVec3> baseNormals = morphed(instance, m_defaultWeights.at(i), true);
			morphOmitted += instance.morphPositions.isEmpty() ? 0 : 1;
			ModelFrameGeometry geometry;
			geometry.positions.resize(base.size());
			QVector<ModelVec3> normals(baseNormals.size());
			if (instance.skin >= 0) {
				// Skinned: mesh space is the bind space; the node's own
				// transform is ignored, as the specification requires.
				for (int v = 0; v < base.size(); ++v) {
					geometry.positions[v] = toGame(base.at(v));
				}
				for (int v = 0; v < baseNormals.size(); ++v) {
					normals[v] = toGame(baseNormals.at(v));
				}
				const QVector<int>& local = skinJoints.at(instance.skin);
				QVector<int> joints(instance.joints.size(), 0);
				QVector<float> weights = instance.weights;
				int unweighted = 0;
				for (int v = 0; v < base.size(); ++v) {
					float total = 0.0f;
					for (int slot = 0; slot < instance.perVertex; ++slot) {
						const int at = v * instance.perVertex + slot;
						const int joint = instance.joints.at(at);
						if (weights.at(at) > 0.0f && (joint < 0 || joint >= local.size())) {
							return fail(QCoreApplication::translate("VibeStudioModelMesh", "glTF primitive %1 names a joint its skin does not have.").arg(surface.name));
						}
						joints[at] = joint >= 0 && joint < local.size() ? jointOfNode.value(local.at(joint)) : 0;
						total += weights.at(at);
					}
					if (total <= 0.0f) {
						++unweighted;
						joints[v * instance.perVertex] = jointOfNode.value(local.first());
						weights[v * instance.perVertex] = 1.0f;
						continue;
					}
					for (int slot = 0; slot < instance.perVertex; ++slot) {
						weights[v * instance.perVertex + slot] /= total;
					}
				}
				if (unweighted > 0) {
					noteModelDecodeIssue(m_mesh, ModelDecodeIssueSeverity::Advisory, QStringLiteral("gltf-unweighted-vertices"),
						QCoreApplication::translate("VibeStudioModelMesh", "%n vertex(es) of glTF primitive %1 carry no weight; they follow its skin's first joint.", nullptr, unweighted)
							.arg(surface.name));
				}
				QString problem;
				if (!modelSkinningFromBindPose(skeleton, geometry.positions, normals, joints, weights, instance.perVertex, &surface.skinning, &problem)) {
					return fail(problem);
				}
			} else {
				const ModelJointMatrix world = toGame(restWorld.at(instance.node));
				for (int v = 0; v < base.size(); ++v) {
					geometry.positions[v] = modelJointTransformPoint(world, toGame(base.at(v)));
				}
				for (int v = 0; v < baseNormals.size(); ++v) {
					normals[v] = modelJointTransformNormal(world, toGame(baseNormals.at(v)));
				}
				if (determinant3(world) < 0.0) {
					flip(&surface);
				}
				// A static mesh under a joint follows that joint rigidly.
				int jointAncestor = -1;
				for (int ancestor = m_nodes.at(instance.node).parent; ancestor >= 0 && jointAncestor < 0; ancestor = m_nodes.at(ancestor).parent) {
					jointAncestor = jointOfNode.value(ancestor, -1);
				}
				if (jointAncestor >= 0) {
					// In the bind pose the mesh sits where the joint's bind puts it.
					const int node = jointOfNode.key(jointAncestor);
					bool inverted = false;
					const ModelJointMatrix toBind = modelJointMultiply(skeleton.joints.at(jointAncestor).bind,
						modelJointInverse(toGame(restWorld.at(node)), &inverted));
					QVector<ModelVec3> bindPositions = geometry.positions;
					QVector<ModelVec3> bindNormals = normals;
					for (auto& p : bindPositions) {
						p = modelJointTransformPoint(toBind, p);
					}
					for (auto& n : bindNormals) {
						n = modelJointTransformNormal(toBind, n);
					}
					geometry.positions = bindPositions;
					normals = bindNormals;
					QString problem;
					if (inverted && !modelSkinningFromBindPose(skeleton, geometry.positions, normals, QVector<int>(base.size(), jointAncestor),
						QVector<float>(base.size(), 1.0f), 1, &surface.skinning, &problem)) {
						return fail(problem);
					}
				}
			}
			geometry.normals = normals.isEmpty() ? modelFaceNormals(surface, geometry.positions) : normals;
			fillZeroNormals(surface, &geometry);
			surface.frames.append(geometry);
			m_mesh->surfaces.append(surface);
		}
		if (morphOmitted > 0) {
			noteModelDecodeIssue(m_mesh, ModelDecodeIssueSeverity::Omission, QStringLiteral("gltf-skinned-morphs"),
				QCoreApplication::translate("VibeStudioModelMesh", "%n skinned glTF primitive(s) have morph targets; their default weights shape the bind pose, and morph animation is left out.", nullptr, morphOmitted));
		}

		// Clips: animations that move a joint.
		qint64 poses = 0;
		int omitted = 0, untouched = 0;
		QSet<QString> clipNames;
		for (const Animation& animation : std::as_const(m_animations)) {
			if (!m_work.step()) { return false; }
			const bool movesJoint = std::any_of(animation.channels.cbegin(), animation.channels.cend(),
				[&](const Channel& channel) { return channel.path != 3 && jointNodes.contains(channel.node); });
			if (!movesJoint) {
				++untouched;
				continue;
			}
			const int rate = frameRate(animation);
			const qint64 frameCount = qint64(std::floor(double(animation.end - animation.start) * rate + 1e-3)) + 1;
			if (frameCount > kMaxFrames || poses + frameCount * skeleton.joints.size() > kMaxJointPoses) {
				++omitted;
				continue;
			}
			poses += frameCount * skeleton.joints.size();
			ModelSkeletalClip clip;
			clip.name = animation.name;
			for (int suffix = 1; clipNames.contains(clip.name.toLower()); ++suffix) {
				clip.name = QStringLiteral("%1_%2").arg(animation.name.left(40)).arg(suffix);
			}
			clipNames.insert(clip.name.toLower());
			clip.framesPerSecond = rate;
			clip.frames.reserve(int(frameCount));
			for (qint64 frame = 0; frame < frameCount; ++frame) {
				if (!m_work.step()) { return false; }
				QVector<ModelJointMatrix> locals;
				QHash<int, QVector<float>> unusedWeights;
				pose(animation, animation.start + float(double(frame) / rate), &locals, &unusedWeights);
				const QVector<ModelJointMatrix> world = worldMatrices(locals);
				QVector<ModelJointMatrix> matrices(skeleton.joints.size());
				for (auto it = jointOfNode.cbegin(); it != jointOfNode.cend(); ++it) {
					matrices[it.value()] = toGame(world.at(it.key()));
				}
				clip.frames.append(matrices);
			}
			skeleton.clips.append(clip);
		}
		if (omitted > 0) {
			noteModelDecodeIssue(m_mesh, ModelDecodeIssueSeverity::Omission, QStringLiteral("gltf-animations-capped"),
				QCoreApplication::translate("VibeStudioModelMesh", "%n glTF animation(s) were left out to keep the clips within %1 frames and %2 joint poses.", nullptr, omitted)
					.arg(kMaxFrames).arg(kMaxJointPoses));
		}
		if (untouched > 0) {
			noteModelDecodeIssue(m_mesh, ModelDecodeIssueSeverity::Omission, QStringLiteral("gltf-animations-unskinned"),
				QCoreApplication::translate("VibeStudioModelMesh", "%n glTF animation(s) move no joint (only nodes or morph weights) and were left out of the skeletal model.", nullptr, untouched));
		}
		m_mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Animations read: %1").arg(skeleton.clips.size());
		QString problem;
		if (!bakeModelSkeleton(m_mesh, ModelSkeletonBakeOptions{}, &problem, m_control)) {
			return fail(problem.isEmpty() ? QCoreApplication::translate("VibeStudioModelMesh", "The glTF skeleton could not be baked into frames.") : problem);
		}
		return true;
	}

	// --- Vertex-animated models -----------------------------------------------

	QVector<ModelVec3> morphed(const Instance& instance, const QVector<float>& weights, bool normals) const
	{
		QVector<ModelVec3> out = normals ? instance.normals : instance.positions;
		const auto& deltas = normals ? instance.morphNormals : instance.morphPositions;
		for (int target = 0; target < deltas.size() && target < weights.size(); ++target) {
			const float weight = weights.at(target);
			if (weight == 0.0f || deltas.at(target).size() != out.size()) {
				continue;
			}
			for (int v = 0; v < out.size(); ++v) {
				out[v].x += weight * deltas.at(target).at(v).x;
				out[v].y += weight * deltas.at(target).at(v).y;
				out[v].z += weight * deltas.at(target).at(v).z;
			}
		}
		return out;
	}

	static void flip(ModelSurface* surface)
	{
		for (ModelTriangle& triangle : surface->triangles) {
			std::swap(triangle.b, triangle.c);
		}
	}

	static void fillZeroNormals(const ModelSurface& surface, ModelFrameGeometry* geometry)
	{
		const QVector<ModelVec3> faces = modelFaceNormals(surface, geometry->positions);
		for (int v = 0; v < geometry->normals.size() && v < faces.size(); ++v) {
			const ModelVec3& n = geometry->normals.at(v);
			if (n.x == 0.0f && n.y == 0.0f && n.z == 0.0f) {
				geometry->normals[v] = faces.at(v);
			}
		}
	}

	// Each instance's geometry for one set of node matrices and weights.
	void placeFrame(const QVector<ModelJointMatrix>& world, const QHash<int, QVector<float>>& animatedWeights, QVector<ModelFrameGeometry>* frames) const
	{
		frames->resize(m_instances.size());
		for (int i = 0; i < m_instances.size(); ++i) {
			const Instance& instance = m_instances.at(i);
			const QVector<float> weights = animatedWeights.value(instance.node, m_defaultWeights.at(i));
			const QVector<ModelVec3> base = morphed(instance, weights, false);
			const QVector<ModelVec3> baseNormals = morphed(instance, weights, true);
			const ModelJointMatrix matrix = toGame(world.at(instance.node));
			ModelFrameGeometry& geometry = (*frames)[i];
			geometry.positions.resize(base.size());
			for (int v = 0; v < base.size(); ++v) {
				geometry.positions[v] = modelJointTransformPoint(matrix, toGame(base.at(v)));
			}
			geometry.normals.resize(baseNormals.size());
			for (int v = 0; v < baseNormals.size(); ++v) {
				geometry.normals[v] = modelJointTransformNormal(matrix, toGame(baseNormals.at(v)));
			}
		}
	}

	ModelFrameInfo frameInfo(int index, const QString& name, const QVector<ModelFrameGeometry>& geometry) const
	{
		ModelFrameInfo info;
		info.index = index;
		info.name = name;
		bool any = false;
		for (const ModelFrameGeometry& surface : geometry) {
			for (const ModelVec3& p : surface.positions) {
				if (!any) {
					info.mins = info.maxs = p;
					any = true;
					continue;
				}
				info.mins = ModelVec3{std::min(info.mins.x, p.x), std::min(info.mins.y, p.y), std::min(info.mins.z, p.z)};
				info.maxs = ModelVec3{std::max(info.maxs.x, p.x), std::max(info.maxs.y, p.y), std::max(info.maxs.z, p.z)};
			}
		}
		info.origin = ModelVec3{(info.mins.x + info.maxs.x) * 0.5f, (info.mins.y + info.maxs.y) * 0.5f, (info.mins.z + info.maxs.z) * 0.5f};
		const ModelVec3 half{info.maxs.x - info.origin.x, info.maxs.y - info.origin.y, info.maxs.z - info.origin.z};
		info.radius = std::sqrt(half.x * half.x + half.y * half.y + half.z * half.z);
		return info;
	}

	bool buildVertexAnimated()
	{
		const QVector<ModelJointMatrix> restWorld = worldMatrices(restLocals());
		qint64 vertices = 0;
		for (int i = 0; i < m_instances.size(); ++i) {
			ModelSurface surface = m_instances.at(i).surface;
			surface.index = i;
			if (determinant3(toGame(restWorld.at(m_instances.at(i).node))) < 0.0) {
				flip(&surface);
			}
			vertices += surface.vertexCount;
			m_mesh->surfaces.append(surface);
		}
		const auto append = [&](const QString& name, const QVector<ModelFrameGeometry>& geometry) {
			for (int i = 0; i < m_mesh->surfaces.size(); ++i) {
				ModelFrameGeometry frame = geometry.at(i);
				if (frame.normals.isEmpty()) {
					frame.normals = modelFaceNormals(m_mesh->surfaces.at(i), frame.positions);
				}
				fillZeroNormals(m_mesh->surfaces.at(i), &frame);
				m_mesh->surfaces[i].frames.append(frame);
			}
			m_mesh->frames.append(frameInfo(int(m_mesh->frames.size()), name, geometry));
		};
		QVector<ModelFrameGeometry> rest;
		placeFrame(restWorld, {}, &rest);
		append(m_animations.isEmpty() ? QStringLiteral("frame0") : QStringLiteral("rest"), rest);
		int omitted = 0;
		QSet<QString> clipNames;
		for (const Animation& animation : std::as_const(m_animations)) {
			if (!m_work.step()) { return false; }
			const int rate = frameRate(animation);
			const qint64 frameCount = qint64(std::floor(double(animation.end - animation.start) * rate + 1e-3)) + 1;
			if (m_mesh->frames.size() + frameCount > kMaxVertexFrames || vertices * (qint64(m_mesh->frames.size()) + frameCount) > kMaxVertexSlots) {
				++omitted;
				continue;
			}
			ModelAnimation clip;
			clip.name = animation.name;
			for (int suffix = 1; clipNames.contains(clip.name.toLower()); ++suffix) {
				clip.name = QStringLiteral("%1_%2").arg(animation.name.left(40)).arg(suffix);
			}
			clipNames.insert(clip.name.toLower());
			clip.firstFrame = int(m_mesh->frames.size());
			clip.frameCount = int(frameCount);
			clip.framesPerSecond = rate;
			for (qint64 frame = 0; frame < frameCount; ++frame) {
				if (!m_work.step()) { return false; }
				QVector<ModelJointMatrix> locals;
				QHash<int, QVector<float>> weights;
				pose(animation, animation.start + float(double(frame) / rate), &locals, &weights);
				QVector<ModelFrameGeometry> geometry;
				placeFrame(worldMatrices(locals), weights, &geometry);
				append(modelBakedFrameName(clip.name, int(frame)), geometry);
			}
			m_mesh->animations.append(clip);
		}
		if (omitted > 0) {
			noteModelDecodeIssue(m_mesh, ModelDecodeIssueSeverity::Omission, QStringLiteral("gltf-animations-capped"),
				QCoreApplication::translate("VibeStudioModelMesh", "%n glTF animation(s) were left out to keep the model within %1 frames.", nullptr, omitted).arg(kMaxVertexFrames));
		}
		m_mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Animations read: %1").arg(m_mesh->animations.size());
		return true;
	}

	QString m_path;
	const QByteArray& m_bytes;
	ModelMesh* m_mesh = nullptr;
	const ModelWorkControl& m_control;
	Companions& m_companions;
	ModelWorkProgress m_work;
	QJsonObject m_root;
	QByteArray m_binChunk;
	bool m_hasBinChunk = false;
	bool m_missingUvs = false;
	QVector<QByteArray> m_buffers;
	QVector<Node> m_nodes;
	QVector<int> m_order;
	QVector<bool> m_drawn;
	QVector<Instance> m_instances;
	QVector<int> m_materialOfInstance;
	QVector<QVector<float>> m_defaultWeights;
	QVector<Animation> m_animations;
};

} // namespace

void decodeGltf(const QString& path, const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control, Companions& companions)
{
	GltfDecoder decoder(path, bytes, mesh, control, companions);
	decoder.decode();
}

} // namespace vibestudio::model_formats
