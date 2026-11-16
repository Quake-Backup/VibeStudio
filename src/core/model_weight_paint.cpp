#include "core/model_weight_paint.h"
#include "core/model_document.h"
#include "core/model_skeleton.h"
#include "core/model_skinning_work.h"
#include <QCoreApplication>
#include <QHash>
#include <QMap>
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace slopstudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelWeightPaint)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
bool near(ModelVec3 a, ModelVec3 b)
{
	return std::abs(double(a.x) - b.x) <= .0001 && std::abs(double(a.y) - b.y) <= .0001 && std::abs(double(a.z) - b.z) <= .0001;
}
bool normalised(const ModelSurface &surface, int v)
{
	double sum = 0;
	for (int i = surface.skinning.first[v]; i < surface.skinning.first[v] + surface.skinning.count[v]; ++i)
	{
		const double w = surface.skinning.influences[i].weight;
		if (!std::isfinite(w) || w < 0)
			return false;
		sum += w;
	}
	return std::abs(sum - 1) <= .00001;
}
// What the strokes of one edit share about a surface. Triangles do not change
// while painting, so the adjacency is built once, on the first Smooth stroke.
struct PaintSurface
{
	// Sorted distinct neighbours of vertex v: adjacent[start[v]] ..
	// adjacent[start[v + 1] - 1].
	QVector<int> start, adjacent;
	bool adjacency = false;
	// Influences stored vertex by vertex in vertex order without gaps, the
	// layout a painted binding is written in.
	bool packed = false;
	// Some vertex has fewer than one or more than 64 influences.
	bool badCounts = false;
};
bool buildAdjacency(const ModelSurface &surface, PaintSurface *state, ModelWorkProgress &work)
{
	const int n = surface.vertexCount;
	QVector<int> offsets(n + 1, 0);
	const auto edges = [](const ModelTriangle &t) { return std::array<std::pair<int, int>, 3>{{{t.a, t.b}, {t.b, t.c}, {t.c, t.a}}}; };
	for (const auto &t : surface.triangles)
	{
		if (!work.step())
			return false;
		for (const auto &[a, b] : edges(t))
			if (a != b)
			{
				++offsets[a + 1];
				++offsets[b + 1];
			}
	}
	for (int v = 0; v < n; ++v)
		offsets[v + 1] += offsets[v];
	QVector<int> fill(offsets.cbegin(), offsets.cend() - 1), adjacent(offsets[n]);
	for (const auto &t : surface.triangles)
		for (const auto &[a, b] : edges(t))
			if (a != b)
			{
				adjacent[fill[a]++] = b;
				adjacent[fill[b]++] = a;
			}
	// Sorted for reproducible floating-point sums across processes; compacted
	// in place, since each vertex's distinct neighbours never outrun its slots.
	state->start.resize(n + 1);
	int out = 0;
	for (int v = 0; v < n; ++v)
	{
		state->start[v] = out;
		const auto from = adjacent.begin() + offsets[v], to = adjacent.begin() + offsets[v + 1];
		std::sort(from, to);
		for (auto it = from, last = std::unique(from, to); it != last; ++it)
			adjacent[out++] = *it;
	}
	state->start[n] = out;
	adjacent.resize(out);
	state->adjacent = std::move(adjacent);
	state->adjacency = true;
	return true;
}
PaintSurface paintSurface(const ModelSurface &surface)
{
	PaintSurface state;
	const auto &skin = surface.skinning;
	const int n = surface.vertexCount;
	state.packed = skin.first.size() == n && skin.count.size() == n;
	int next = 0;
	for (int v = 0; v < n && v < skin.count.size(); ++v)
	{
		state.badCounts |= skin.count[v] < 1 || skin.count[v] > 64;
		state.packed = state.packed && skin.first[v] == next;
		next += skin.count[v];
	}
	state.packed = state.packed && next == skin.influences.size();
	return state;
}
bool stroke(ModelMesh *mesh, const ModelWeightStroke &s, int bind, PaintSurface &state, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	auto &surface = mesh->surfaces[s.surface];
	const int joint = modelJointIndex(mesh->skeleton, s.joint, Qt::CaseSensitive);
	const int remainder = modelJointIndex(mesh->skeleton, s.remainderJoint, Qt::CaseSensitive);
	QSet<int> locks;
	for (const auto &name : s.lockedJoints)
		locks.insert(modelJointIndex(mesh->skeleton, name, Qt::CaseSensitive));
	if (s.mode == ModelWeightPaintMode::Smooth && !state.adjacency && !buildAdjacency(surface, &state, work))
		return false;
	QMap<int, double> coverage;
	for (const auto &sample : s.samples)
		if (sample.opacity > 0)
			coverage.insert(sample.vertex, sample.opacity);
	const auto tooMany = [&] { return fail(error, Text::tr("Painting would exceed 64 influences at a vertex. Reduce its weights explicitly first.")); };
	// Vertices the stroke does not cover keep their influences; only a binding
	// that already has a vertex without 1-64 influences needs them checked, in
	// vertex order with the covered ones.
	int previous = -1;
	const auto checkUncovered = [&](int to)
	{
		for (int u = previous + 1; state.badCounts && u < to; ++u)
		{
			if (!work.step())
				return false;
			if (surface.skinning.count[u] < 1 || surface.skinning.count[u] > 64)
				return tooMany();
		}
		return true;
	};
	// The covered vertices' new influences, in `painted`.
	struct Replacement
	{
		int vertex = 0, offset = 0, count = 0;
	};
	QVector<ModelJointInfluence> painted;
	QVector<Replacement> replaced;
	bool changed = false;
	for (auto covered = coverage.cbegin(); covered != coverage.cend(); ++covered)
	{
		const int v = covered.key();
		if (!checkUncovered(v))
			return false;
		previous = v;
		if (!work.step())
			return false;
		const int first = surface.skinning.first[v], end = first + surface.skinning.count[v];
		const int offset = int(painted.size());
		{
			if (!normalised(surface, v))
				return fail(error, Text::tr("Vertex %1 has negative or unnormalised weights. Repair its binding before painting.").arg(v));
			double locked = 0, others = 0, oldWeight = 0;
			bool normalOffsets = false;
			for (int i = first; i < end; ++i)
			{
				const auto &w = surface.skinning.influences[i];
				if (w.weight > 0 &&
					!near(modelJointTransformPoint(mesh->skeleton.joints[w.joint].bind, w.offset), surface.frames[bind].positions[v]))
					return fail(error, Text::tr("Vertex %1 has distinct imported bind offsets. Rebind it before painting.").arg(v));
				if (locks.contains(w.joint))
					locked += w.weight;
				else if (w.joint == joint)
					oldWeight += w.weight;
				else
					others += w.weight;
				if (w.weight > 0 && (w.normalOffset.x != 0 || w.normalOffset.y != 0 || w.normalOffset.z != 0))
					normalOffsets = true;
			}
			// The normalisation tolerance: three locked thirds stored as floats
			// sum just above one.
			if (locked > 1.00001)
				return fail(error, Text::tr("Locked weights exceed one at vertex %1.").arg(v));
			const double available = std::max(0., 1 - locked), alpha = s.strength * covered.value();
			double wanted = s.value;
			if (s.mode == ModelWeightPaintMode::Add)
				wanted = oldWeight + s.value;
			else if (s.mode == ModelWeightPaintMode::Subtract)
				wanted = oldWeight - s.value;
			else if (s.mode == ModelWeightPaintMode::Smooth)
			{
				wanted = 0;
				const int from = state.start[v], to = state.start[v + 1];
				for (int i = from; i < to; ++i)
				{
					if (!work.step())
						return false;
					const int other = state.adjacent[i];
					if (!normalised(surface, other))
						return fail(error, Text::tr("Smoothing touches an invalid neighbouring binding. Repair it first."));
					wanted += modelVertexJointWeight(surface, other, joint);
				}
				wanted = from == to ? oldWeight : wanted / (to - from);
			}
			// Clamp after blending: strength scales the requested add/subtract.
			const double weight = std::clamp(oldWeight + alpha * (wanted - oldWeight), 0., available);
			// A representable no-op must not renormalise float round-off or add
			// history. Explicit binding repair owns that operation.
			if (float(weight) == float(oldWeight))
				continue;
			changed = true;
			const double rest = available - weight;
			if (rest > 1e-12 && others <= 0 && remainder < 0)
				return fail(error, Text::tr("Reducing the sole unlocked influence at vertex %1 needs an explicit remainder joint.").arg(v));
			for (int i = first; i < end; ++i)
			{
				auto w = surface.skinning.influences[i];
				if (locks.contains(w.joint))
					painted.append(w);
				else if (w.weight > 0)
				{
					w.weight = float(w.weight * (w.joint == joint ? weight / oldWeight : others > 0 ? rest / others : 0));
					if (w.weight > 0)
						painted.append(w);
				}
			}
			const auto add = [&](int j, double amount)
			{
				bool invertible = false;
				const auto inverse = modelJointInverse(mesh->skeleton.joints[j].bind, &invertible);
				if (!invertible || !modelJointMatrixIsFinite(inverse))
					return fail(error, Text::tr("A painted joint has a singular bind transform."));
				painted.append({j, float(amount), modelJointTransformPoint(inverse, surface.frames[bind].positions[v]),
								normalOffsets ? modelJointTransformVector(inverse, surface.frames[bind].normals[v]) : ModelVec3{}});
				return true;
			};
			if (oldWeight <= 0 && weight > 0 && !add(joint, weight))
				return false;
			if (others <= 0 && rest > 1e-12 && !add(remainder, rest))
				return false;
		}
		const int count = int(painted.size()) - offset;
		if (count < 1 || count > 64)
			return tooMany();
		replaced.append({v, offset, count});
	}
	if (!checkUncovered(surface.vertexCount))
		return false;
	if (!changed)
		return work.check();
	auto &skin = surface.skinning;
	const bool sameLayout = state.packed && std::all_of(replaced.cbegin(), replaced.cend(), [&](const Replacement &r) { return r.count == skin.count[r.vertex]; });
	if (sameLayout)
		for (const auto &r : replaced)
			std::copy(painted.cbegin() + r.offset, painted.cbegin() + r.offset + r.count, skin.influences.begin() + skin.first[r.vertex]);
	else
	{
		ModelSurfaceSkinning output;
		output.first.reserve(surface.vertexCount);
		output.count.reserve(surface.vertexCount);
		output.influences.reserve(skin.influences.size() + painted.size());
		int next = 0;
		for (int v = 0; v < surface.vertexCount; ++v)
		{
			output.first.append(int(output.influences.size()));
			const bool repainted = next < replaced.size() && replaced[next].vertex == v;
			const int from = repainted ? replaced[next].offset : skin.first[v], count = repainted ? replaced[next].count : skin.count[v];
			const auto &source = repainted ? painted : skin.influences;
			for (int i = from; i < from + count; ++i)
				output.influences.append(source[i]);
			output.count.append(count);
			next += repainted ? 1 : 0;
		}
		skin = std::move(output);
		state.packed = true;
	}
	return work.check();
}
} // namespace
double modelVertexJointWeight(const ModelSurface &surface, int vertex, int joint)
{
	if (vertex < 0 || vertex >= surface.vertexCount || surface.skinning.isEmpty())
		return 0;
	double weight = 0;
	for (int i = surface.skinning.first[vertex]; i < surface.skinning.first[vertex] + surface.skinning.count[vertex]; ++i)
		if (surface.skinning.influences[i].joint == joint)
			weight += surface.skinning.influences[i].weight;
	return weight;
}
bool applyModelWeightPaint(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!mesh || !work.check())
		return false;
	ModelSelection context;
	context.surface = edit.selection.surface;
	if (edit.kind != ModelEditKind::PaintSkinWeights || edit.frame != -1 || edit.selection != context || edit.weightStrokes.isEmpty() ||
		edit.weightStrokes.size() > modelWeightMaxStrokes)
		return fail(error, Text::tr("Weight painting needs 1–1024 strokes with explicit vertex targets and no competing selection."));
	QSet<int> surfaces;
	qint64 samples = 0;
	for (const auto &s : edit.weightStrokes)
	{
		if (!work.check())
			return false;
		if (s.surface < 0 || s.surface >= mesh->surfaces.size() || mesh->surfaces[s.surface].skinning.isEmpty() || int(s.mode) < 0 ||
			int(s.mode) > int(ModelWeightPaintMode::Smooth) || !std::isfinite(s.value) || s.value < 0 || s.value > 1 ||
			!std::isfinite(s.strength) || s.strength <= 0 || s.strength > 1 || (s.mode == ModelWeightPaintMode::Smooth && s.value != 1) ||
			s.samples.isEmpty())
			return fail(error,
						Text::tr("Choose a bound surface, a brush mode, a value from zero to one and a positive strength up to one."));
		const int joint = modelJointIndex(mesh->skeleton, s.joint, Qt::CaseSensitive);
		QSet<QString> locks;
		for (const auto &name : s.lockedJoints)
		{
			if (name == s.joint || locks.contains(name) || modelJointIndex(mesh->skeleton, name, Qt::CaseSensitive) < 0)
				return fail(error, Text::tr("Locked joints must be distinct existing names and cannot include the painted joint."));
			locks.insert(name);
		}
		if (joint < 0 || (!s.remainderJoint.isEmpty() && (s.remainderJoint == s.joint || locks.contains(s.remainderJoint) ||
														  modelJointIndex(mesh->skeleton, s.remainderJoint, Qt::CaseSensitive) < 0)))
			return fail(error, Text::tr("Choose an existing painted joint and, if needed, a different unlocked remainder joint."));
		QSet<int> seen;
		bool covered = false;
		for (const auto &sample : s.samples)
		{
			if (!work.step())
				return false;
			if (++samples > modelWeightMaxSamples || sample.vertex < 0 || sample.vertex >= mesh->surfaces[s.surface].vertexCount ||
				seen.contains(sample.vertex) || !std::isfinite(sample.opacity) || sample.opacity < 0 || sample.opacity > 1)
				return fail(error, Text::tr("Stroke samples need distinct existing vertices, opacity from zero to one and at most "
											"1,048,576 samples in total."));
			seen.insert(sample.vertex);
			covered |= sample.opacity > 0;
		}
		if (!covered)
			return fail(error, Text::tr("The brush stroke has no covered vertices."));
		surfaces.insert(s.surface);
	}
	ModelSkinningWork skin;
	if (!skin.prepare(*mesh, error))
		return false;
	auto ordered = surfaces.values();
	std::sort(ordered.begin(), ordered.end());
	QMap<int, ModelSurfaceSkinning> original;
	QHash<int, PaintSurface> states;
	for (int s : ordered)
	{
		if (!skin.validateSurface(*mesh, s, error, control))
			return false;
		original.insert(s, mesh->surfaces[s].skinning);
		states.insert(s, paintSurface(mesh->surfaces[s]));
	}
	for (const auto &s : edit.weightStrokes)
		if (!stroke(mesh, s, skin.bindFrame, states[s.surface], error, control))
			return false;
	bool changed = false;
	for (int s : ordered)
	{
		if (original[s].influences.constData() == mesh->surfaces[s].skinning.influences.constData())
			continue;
		changed = true;
		if (!skin.bakeSurface(mesh, s, error, control))
			return false;
	}
	if (changed)
		for (auto &clip : mesh->skeleton.clips)
		{
			clip.frameMins.clear();
			clip.frameMaxs.clear();
		}
	return work.check();
}
} // namespace slopstudio
