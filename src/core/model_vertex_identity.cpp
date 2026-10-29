#include "core/model_vertex_identity.h"
#include "core/model_geometric_topology.h"
#include <QCoreApplication>
#include <QHash>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelVertexIdentity)
};
} // namespace
bool validateModelVertexIdentities(const ModelSurface &surface, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	if (!work.check())
		return false;
	if (surface.topologyVertices.isEmpty() && !surface.sharpEdges.isEmpty())
	{
		if (error)
			*error = Text::tr("Surface %1 has sharp edges without vertex connectivity identities.").arg(surface.name);
		return false;
	}
	for (const auto &edge : surface.sharpEdges)
		if (edge.first < 0 || edge.second <= edge.first || edge.second >= surface.vertexCount)
		{
			if (error)
				*error = Text::tr("Surface %1 has an invalid sharp edge.").arg(surface.name);
			return false;
		}
	if (surface.topologyVertices.isEmpty())
		return true;
	if (surface.topologyVertices.size() != surface.vertexCount)
	{
		if (error)
			*error = Text::tr("Surface %1 has incomplete vertex connectivity identities.").arg(surface.name);
		return false;
	}
	for (int identity : surface.topologyVertices)
	{
		if (!work.step())
			return false;
		if (identity < 0 || identity >= surface.vertexCount)
		{
			if (error)
				*error = Text::tr("Surface %1 has an invalid vertex connectivity identity.").arg(surface.name);
			return false;
		}
	}
	return work.check();
}
bool materialiseModelVertexIdentities(ModelSurface *surface, QString *error, const ModelWorkControl &control)
{
	if (!surface)
		return false;
	if (!surface->topologyVertices.isEmpty())
		return validateModelVertexIdentities(*surface, error, control);
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(*surface, &topology, error, control))
		return false;
	surface->topologyVertices = std::move(topology.vertexGroup);
	compactModelVertexIdentities(surface);
	return true;
}
void compactModelVertexIdentities(ModelSurface *surface)
{
	if (!surface || surface->topologyVertices.isEmpty())
		return;
	QHash<int, int> labels;
	for (int &identity : surface->topologyVertices)
	{
		const auto found = labels.constFind(identity);
		if (found != labels.cend())
			identity = found.value();
		else
		{
			const int label = labels.size();
			labels.insert(identity, label);
			identity = label;
		}
	}
	// Sharp edges name identities; they follow the relabelling, and an edge
	// whose vertices are gone goes with them.
	if (!surface->sharpEdges.isEmpty())
	{
		QSet<QPair<int, int>> sharp;
		for (const auto &edge : std::as_const(surface->sharpEdges))
			if (labels.contains(edge.first) && labels.contains(edge.second))
			{
				const int a = labels.value(edge.first), b = labels.value(edge.second);
				if (a != b)
					sharp.insert({std::min(a, b), std::max(a, b)});
			}
		surface->sharpEdges = std::move(sharp);
	}
}
void detachModelVertexIdentity(ModelSurface *surface, int vertex, QHash<int, int> *copies)
{
	if (surface->topologyVertices.isEmpty())
		return;
	const int old = surface->topologyVertices[vertex];
	if (!copies->contains(old))
		copies->insert(old, vertex);
	surface->topologyVertices[vertex] = copies->value(old);
}
} // namespace vibestudio
