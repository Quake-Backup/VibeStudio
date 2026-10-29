#include "core/model_uv_pins.h"

#include "core/model_document.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <algorithm>
#include <cstring>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelUvPins)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
// A corner's identity for remapping: its reference-pose position and its UV,
// bit for bit.
QByteArray cornerKey(const ModelVec3 &position, const ModelTexCoord &uv)
{
	QByteArray key;
	key.reserve(int(sizeof(float) * 5));
	const float values[5]{position.x, position.y, position.z, uv.u, uv.v};
	key.append(reinterpret_cast<const char *>(values), int(sizeof(values)));
	return key;
}
bool samePosition(const ModelVec3 &a, const ModelVec3 &b) { return std::memcmp(&a, &b, sizeof(ModelVec3)) == 0; }
bool sameTriangles(const QVector<ModelTriangle> &a, const QVector<ModelTriangle> &b)
{
	if (a.size() != b.size())
		return false;
	if (a.constData() == b.constData())
		return true;
	return std::equal(a.cbegin(), a.cend(), b.cbegin(),
					  [](const ModelTriangle &x, const ModelTriangle &y) { return x.a == y.a && x.b == y.b && x.c == y.c; });
}
} // namespace

bool isModelUvPinEdit(ModelEditKind kind) { return kind == ModelEditKind::PinUv || kind == ModelEditKind::UnpinUv; }

bool applyModelUvPinEdit(ModelMesh *mesh, const ModelEdit &edit, QString *error)
{
	if (!mesh || !isModelUvPinEdit(edit.kind))
		return fail(error, Text::tr("Choose Pin UVs or Unpin UVs."));
	const auto &selection = edit.selection;
	if (selection.surface < 0 || selection.surface >= mesh->surfaces.size())
		return fail(error, Text::tr("Choose an existing surface."));
	auto &surface = mesh->surfaces[selection.surface];
	QSet<int> corners;
	for (int v : selection.vertices)
		corners.insert(v);
	for (const auto &edge : selection.edges)
	{
		corners.insert(edge.first);
		corners.insert(edge.second);
	}
	for (int face : selection.faces)
	{
		if (face < 0 || face >= surface.triangles.size())
			return fail(error, Text::tr("The selection names a face the surface does not have."));
		const auto t = surface.triangles[face];
		corners.insert(t.a);
		corners.insert(t.b);
		corners.insert(t.c);
	}
	if (corners.isEmpty())
		return fail(error, Text::tr("Select the UV corners, edges or faces to pin or unpin."));
	for (int v : corners)
		if (v < 0 || v >= surface.vertexCount)
			return fail(error, Text::tr("The selection names a vertex the surface does not have."));
	auto pins = surface.uvPins;
	if (edit.kind == ModelEditKind::PinUv)
		pins.unite(corners);
	else
		pins.subtract(corners);
	if (pins == surface.uvPins)
		return fail(error, edit.kind == ModelEditKind::PinUv ? Text::tr("Every selected UV corner is already pinned.")
															 : Text::tr("No selected UV corner is pinned."));
	surface.uvPins = std::move(pins);
	return true;
}

ModelUvPinState captureModelUvPins(const ModelMesh &mesh)
{
	ModelUvPinState state;
	for (int s = 0; s < mesh.surfaces.size(); ++s)
	{
		const auto &surface = mesh.surfaces[s];
		if (surface.uvPins.isEmpty())
			continue;
		// Implicitly shared: nothing is copied unless the edit detaches it.
		state.surfaces.append({s, surface.vertexCount, surface.triangles, surface.texCoords,
							   surface.frames.isEmpty() ? QVector<ModelVec3>{} : surface.frames.first().positions, surface.topologyVertices,
							   surface.uvPins});
	}
	return state;
}

void carryModelUvPins(const ModelUvPinState &before, ModelMesh *mesh)
{
	if (!mesh)
		return;
	for (const auto &old : before.surfaces)
	{
		if (old.index < 0 || old.index >= mesh->surfaces.size())
			continue;
		auto &surface = mesh->surfaces[old.index];
		const auto inRange = [&]
		{
			for (auto it = surface.uvPins.begin(); it != surface.uvPins.end();)
				it = *it < 0 || *it >= surface.vertexCount ? surface.uvPins.erase(it) : std::next(it);
		};
		// The edit kept the corners, or maintained the pins itself.
		if (surface.uvPins != old.pins || (surface.vertexCount == old.vertexCount && sameTriangles(surface.triangles, old.triangles)))
		{
			inRange();
			continue;
		}
		const auto positions = surface.frames.isEmpty() ? QVector<ModelVec3>{} : surface.frames.first().positions;
		// Corners kept their indices (splits were appended): every pinned
		// corner is still at its index with the same reference position.
		const bool prefix = surface.vertexCount >= old.vertexCount &&
							std::all_of(old.pins.cbegin(), old.pins.cend(),
										[&](int p)
										{
											return p < old.positions.size() && p < positions.size() && samePosition(old.positions[p], positions[p]) &&
												   (old.topologyVertices.isEmpty() || surface.topologyVertices.isEmpty() ||
													(p < surface.topologyVertices.size() && old.topologyVertices[p] == surface.topologyVertices[p]));
										});
		if (prefix)
		{
			inRange();
			continue;
		}
		// Renumbered: a pin stays on each corner with the same reference
		// position and UV.
		QSet<QByteArray> keys;
		for (int p : old.pins)
			if (p < old.positions.size() && p < old.texCoords.size())
				keys.insert(cornerKey(old.positions[p], old.texCoords[p]));
		QSet<int> pins;
		for (int v = 0; v < surface.vertexCount && v < positions.size() && v < surface.texCoords.size(); ++v)
			if (keys.contains(cornerKey(positions[v], surface.texCoords[v])))
				pins.insert(v);
		surface.uvPins = std::move(pins);
	}
}

QHash<int, ModelTexCoord> modelUvPinnedCorners(const ModelSurface &surface, const QSet<int> &faces)
{
	QHash<int, ModelTexCoord> pinned;
	if (surface.uvPins.isEmpty())
		return pinned;
	for (int face : faces)
	{
		if (face < 0 || face >= surface.triangles.size())
			continue;
		const auto t = surface.triangles[face];
		for (int v : {t.a, t.b, t.c})
			if (surface.uvPins.contains(v) && v < surface.texCoords.size())
				pinned.insert(v, surface.texCoords[v]);
	}
	return pinned;
}

QJsonValue modelUvPinsJson(const QSet<int> &pins)
{
	auto sorted = pins.values();
	std::sort(sorted.begin(), sorted.end());
	QJsonArray array;
	for (int v : sorted)
		array.append(v);
	return array;
}

bool parseModelUvPins(const QJsonValue &value, int vertexCount, QSet<int> *pins)
{
	if (!value.isArray() || value.toArray().size() > vertexCount)
		return false;
	QSet<int> result;
	int previous = -1;
	for (const auto &item : value.toArray())
	{
		const int v = item.toInt(-1);
		// Ascending and unique, inside the surface.
		if (item != QJsonValue(v) || v <= previous || v >= vertexCount)
			return false;
		result.insert(v);
		previous = v;
	}
	if (pins)
		*pins = std::move(result);
	return true;
}
} // namespace vibestudio
