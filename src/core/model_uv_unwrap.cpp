#include "core/model_uv_unwrap.h"

#include "core/model_geometry_helpers.h"
#include "core/model_uv.h"
#include "core/model_uv_mapping.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QMap>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

// Least Squares Conformal Maps follow the published method: Bruno Lévy,
// Sylvain Petitjean, Nicolas Ray and Jérome Maillot, "Least Squares Conformal
// Maps for Automatic Texture Atlas Generation", ACM SIGGRAPH 2002 (ACM
// Transactions on Graphics 21(3), 362-371). Each triangle contributes the two
// Cauchy-Riemann residuals of its linear map in a local orthonormal frame,
// weighted by the inverse square root of twice its area; two pinned corners
// remove the similarity freedom. The solver is an original conjugate-gradient
// least-squares (CGLS) iteration with Jacobi column scaling. No code is taken
// from another implementation.

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelUvUnwrap)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
struct Sets
{
	QVector<int> parent;
	explicit Sets(int count) : parent(count) { std::iota(parent.begin(), parent.end(), 0); }
	int root(int index)
	{
		while (parent[index] != index)
		{
			parent[index] = parent[parent[index]];
			index = parent[index];
		}
		return index;
	}
	void join(int a, int b)
	{
		a = root(a);
		b = root(b);
		parent[std::max(a, b)] = std::min(a, b);
	}
};
std::array<int, 3> corners(ModelTriangle t) { return {t.a, t.b, t.c}; }
QString faceList(const QVector<int> &faces)
{
	QStringList parts;
	for (int i = 0; i < faces.size() && i < 12; ++i)
		parts << QString::number(faces[i]);
	if (faces.size() > 12)
		parts << QStringLiteral("…");
	return parts.join(QStringLiteral(", "));
}
// Sparse rows for min ||A x - b||: two per triangle, at most six entries each.
struct Rows
{
	QVector<int> start{0}, column;
	QVector<double> value, rhs;
	void add(const QVector<QPair<int, double>> &entries, double target)
	{
		for (const auto &[c, v] : entries)
		{
			column.append(c);
			value.append(v);
		}
		start.append(int(column.size()));
		rhs.append(target);
	}
	[[nodiscard]] int count() const { return int(rhs.size()); }
};
// CGLS with Jacobi column scaling. False on cancellation.
bool solve(const Rows &rows, int unknowns, int maxIterations, double tolerance, QVector<double> *x, int *iterations, ModelWorkProgress &work)
{
	QVector<double> scale(unknowns, 0);
	for (int i = 0; i < rows.column.size(); ++i)
		scale[rows.column[i]] += rows.value[i] * rows.value[i];
	for (auto &s : scale)
		s = s > 0 ? 1 / std::sqrt(s) : 1;
	const auto multiply = [&](const QVector<double> &y, QVector<double> &out)
	{
		out.fill(0, rows.count());
		for (int r = 0; r < rows.count(); ++r)
		{
			double sum = 0;
			for (int i = rows.start[r]; i < rows.start[r + 1]; ++i)
				sum += rows.value[i] * scale[rows.column[i]] * y[rows.column[i]];
			out[r] = sum;
		}
	};
	const auto transposed = [&](const QVector<double> &r, QVector<double> &out)
	{
		out.fill(0, unknowns);
		for (int row = 0; row < rows.count(); ++row)
			for (int i = rows.start[row]; i < rows.start[row + 1]; ++i)
				out[rows.column[i]] += rows.value[i] * scale[rows.column[i]] * r[row];
	};
	QVector<double> y(unknowns, 0), r = rows.rhs, s, p, q;
	transposed(r, s);
	p = s;
	double gamma = 0;
	for (double v : s)
		gamma += v * v;
	const double initial = std::sqrt(gamma);
	int k = 0;
	for (; k < maxIterations && gamma > 0 && std::sqrt(gamma) > tolerance * initial; ++k)
	{
		if (!work.step())
			return false;
		multiply(p, q);
		double qq = 0;
		for (double v : q)
			qq += v * v;
		if (!(qq > 0))
			break;
		const double alpha = gamma / qq;
		for (int i = 0; i < unknowns; ++i)
			y[i] += alpha * p[i];
		for (int i = 0; i < rows.count(); ++i)
			r[i] -= alpha * q[i];
		transposed(r, s);
		double next = 0;
		for (double v : s)
			next += v * v;
		const double beta = next / gamma;
		for (int i = 0; i < unknowns; ++i)
			p[i] = s[i] + beta * p[i];
		gamma = next;
	}
	x->resize(unknowns);
	for (int i = 0; i < unknowns; ++i)
		(*x)[i] = y[i] * scale[i];
	*iterations = k;
	return true;
}
struct Chart
{
	QVector<int> faces;			  // local face indices
	QVector<int> vertices;		  // fan vertex ids
	QVector<int> sourceVertices;  // fan id -> surface vertex, parallel to vertices
};
} // namespace

bool unwrapModelUvConformal(const ModelSurface &source, const QSet<int> &faces, const QHash<int, ModelTexCoord> &pins,
							const ModelUvUnwrapOptions &options, ModelSurface *result, ModelUvUnwrapReport *report, QString *error,
							const ModelWorkControl &control)
{
	if (error)
		error->clear();
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	if (!result || faces.isEmpty() || options.frame < 0 || options.frame >= source.frames.size() || options.maxIterations < 1 ||
		options.maxIterations > 1000000 || !std::isfinite(options.tolerance) || options.tolerance <= 0 || options.tolerance >= 1)
		return fail(error, Text::tr("Select faces and a valid reference pose to unwrap."));
	for (int face : faces)
		if (face < 0 || face >= source.triangles.size())
			return fail(error, Text::tr("A selected face no longer exists."));
	for (auto it = pins.cbegin(); it != pins.cend(); ++it)
		if (it.key() < 0 || it.key() >= source.vertexCount || !std::isfinite(it.value().u) || !std::isfinite(it.value().v))
			return fail(error, Text::tr("A pinned UV corner no longer exists or has no finite UV."));
	ModelUvTopology topology;
	if (!buildModelUvTopology(source, &topology, error, control))
		return false;
	auto sorted = faces.values();
	std::sort(sorted.begin(), sorted.end());
	QVector<int> local(source.triangles.size(), -1);
	for (int i = 0; i < sorted.size(); ++i)
		local[sorted[i]] = i;
	// Corner fans and charts join across unmarked edges shared by two
	// selected faces, as packing does; marks and splits cut both.
	const int count = int(sorted.size());
	Sets fans(count * 3), charts(count);
	for (const auto &edge : topology.edges)
	{
		if (!work.step())
			return false;
		if (edge.seam || edge.faces.size() != 2)
			continue;
		const int a = local[edge.faces[0]], b = local[edge.faces[1]];
		if (a < 0 || b < 0)
			continue;
		charts.join(a, b);
		const auto ca = corners(source.triangles[edge.faces[0]]), cb = corners(source.triangles[edge.faces[1]]);
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				if (ca[i] == cb[j] && (ca[i] == edge.vertices.first || ca[i] == edge.vertices.second))
					fans.join(a * 3 + i, b * 3 + j);
	}
	QVector<int> fanOf(count * 3);
	QVector<int> fanSource;
	QMap<int, int> fanIds;
	for (int f = 0; f < count; ++f)
	{
		const auto c = corners(source.triangles[sorted[f]]);
		for (int i = 0; i < 3; ++i)
		{
			const int root = fans.root(f * 3 + i);
			auto found = fanIds.constFind(root);
			if (found == fanIds.cend())
			{
				found = fanIds.insert(root, int(fanSource.size()));
				fanSource.append(c[i]);
			}
			fanOf[f * 3 + i] = found.value();
		}
	}
	QMap<int, Chart> chartOf;
	for (int f = 0; f < count; ++f)
		chartOf[charts.root(f)].faces.append(f);
	const auto &positions = source.frames[options.frame].positions;
	using namespace model_geometry;
	QVector<double> uvU(fanSource.size(), 0), uvV(fanSource.size(), 0);
	ModelUvUnwrapReport summary;
	// Unpinned islands are laid in a row at one shared density.
	struct Placed
	{
		QVector<int> fans;
		double minU, minV, maxU, maxV;
	};
	QVector<Placed> row;
	for (auto it = chartOf.begin(); it != chartOf.end(); ++it)
	{
		auto &chart = it.value();
		QVector<int> sourceFaces;
		for (int f : chart.faces)
			sourceFaces.append(sorted[f]);
		// Fan ids in this chart, and boundary edges between them.
		QMap<int, int> unknown;
		QMap<QPair<int, int>, int> edgeUses;
		for (int f : chart.faces)
		{
			for (int i = 0; i < 3; ++i)
			{
				const int a = fanOf[f * 3 + i], b = fanOf[f * 3 + (i + 1) % 3];
				unknown.insert(a, 0);
				edgeUses[{std::min(a, b), std::max(a, b)}]++;
			}
		}
		QVector<int> boundary;
		for (auto e = edgeUses.cbegin(); e != edgeUses.cend(); ++e)
			if (e.value() == 1)
			{
				boundary.append(e.key().first);
				boundary.append(e.key().second);
			}
		if (boundary.isEmpty())
			return fail(error, Text::tr("A selected island is closed, so it cannot be flattened without a cut. Mark a seam on it and "
										"unwrap again. Faces: %1.")
								   .arg(faceList(sourceFaces)));
		std::sort(boundary.begin(), boundary.end());
		boundary.erase(std::unique(boundary.begin(), boundary.end()), boundary.end());
		// Pins: the user's, else the two boundary corners farthest apart.
		QVector<int> pinned;
		for (auto u = unknown.cbegin(); u != unknown.cend(); ++u)
			if (pins.contains(fanSource[u.key()]))
				pinned.append(u.key());
		// One pin alone cannot fix an island's turn and scale; it is ignored.
		const bool userPinned = pinned.size() >= 2;
		if (!userPinned)
			pinned.clear();
		QVector<int> anchors = pinned;
		if (!userPinned)
		{
			const auto point3 = [&](int fan) { return point(positions[fanSource[fan]]); };
			const auto farthest = [&](int from)
			{
				int best = from;
				double distance = -1;
				for (int fan : boundary)
				{
					const double d = length(point3(fan) - point3(from));
					if (d > distance)
					{
						distance = d;
						best = fan;
					}
				}
				return best;
			};
			const int first = farthest(boundary.first());
			const int second = farthest(first);
			if (first == second || !(length(point3(first) - point3(second)) > 0))
				return fail(error, Text::tr("A selected island has no extent to flatten. Faces: %1.").arg(faceList(sourceFaces)));
			anchors = {first, second};
		}
		QHash<int, std::array<double, 2>> anchorUv;
		if (userPinned)
		{
			for (int fan : anchors)
				anchorUv.insert(fan, {double(pins.value(fanSource[fan]).u), double(pins.value(fanSource[fan]).v)});
		}
		else
		{
			const double d = length(point(positions[fanSource[anchors[1]]]) - point(positions[fanSource[anchors[0]]]));
			anchorUv.insert(anchors[0], {0, 0});
			anchorUv.insert(anchors[1], {d, 0});
		}
		int next = 0;
		for (auto u = unknown.begin(); u != unknown.end(); ++u)
			u.value() = anchorUv.contains(u.key()) ? -1 : next++;
		// Two Cauchy-Riemann rows per triangle in a local orthonormal frame.
		Rows rows;
		QVector<std::array<double, 6>> frames;
		for (int f : chart.faces)
		{
			if (!work.step())
				return false;
			const auto t = source.triangles[sorted[f]];
			const auto p0 = point(positions[t.a]), p1 = point(positions[t.b]), p2 = point(positions[t.c]);
			const auto e1 = p1 - p0, e2 = p2 - p0;
			const double l1 = length(e1);
			const auto n = cross(e1, e2);
			const double ln = length(n);
			if (!(l1 > 0) || !(ln > 0))
				return fail(error, Text::tr("A selected face has no area in the reference pose. Repair it before unwrapping. Face: %1.")
									   .arg(sorted[f]));
			const auto xAxis = e1 * (1 / l1), yAxis = cross(n, e1) * (1 / (ln * l1));
			const std::array<double, 6> xy{0, 0, l1, 0, dot(e2, xAxis), dot(e2, yAxis)};
			frames.append(xy);
			const double weight = 1 / std::sqrt(l1 * xy[5]);
			QVector<QPair<int, double>> real, imaginary;
			double realTarget = 0, imaginaryTarget = 0;
			for (int j = 0; j < 3; ++j)
			{
				const int j1 = (j + 1) % 3, j2 = (j + 2) % 3;
				const double d = (xy[j1 * 2 + 1] - xy[j2 * 2 + 1]) * weight, e = (xy[j2 * 2] - xy[j1 * 2]) * weight;
				const int fan = fanOf[f * 3 + j];
				const int index = unknown.value(fan);
				if (index < 0)
				{
					const auto fixed = anchorUv.value(fan);
					realTarget -= d * fixed[0] - e * fixed[1];
					imaginaryTarget -= e * fixed[0] + d * fixed[1];
					continue;
				}
				real.append({index * 2, d});
				real.append({index * 2 + 1, -e});
				imaginary.append({index * 2, e});
				imaginary.append({index * 2 + 1, d});
			}
			rows.add(real, realTarget);
			rows.add(imaginary, imaginaryTarget);
		}
		QVector<double> solution;
		int iterations = 0;
		if (next > 0 && !solve(rows, next * 2, options.maxIterations, options.tolerance, &solution, &iterations, work))
			return false;
		for (auto u = unknown.cbegin(); u != unknown.cend(); ++u)
		{
			if (u.value() < 0)
			{
				uvU[u.key()] = anchorUv.value(u.key())[0];
				uvV[u.key()] = anchorUv.value(u.key())[1];
			}
			else
			{
				uvU[u.key()] = solution[u.value() * 2];
				uvV[u.key()] = solution[u.value() * 2 + 1];
			}
		}
		// Folds: every face must keep the frame's (positive) winding.
		double area3 = 0, areaUv = 0;
		QVector<int> folded;
		for (int i = 0; i < chart.faces.size(); ++i)
		{
			const int f = chart.faces[i];
			const int a = fanOf[f * 3], b = fanOf[f * 3 + 1], c = fanOf[f * 3 + 2];
			const double signedArea = (uvU[b] - uvU[a]) * (uvV[c] - uvV[a]) - (uvV[b] - uvV[a]) * (uvU[c] - uvU[a]);
			if (!std::isfinite(signedArea) || signedArea <= 0)
				folded.append(sorted[f]);
			areaUv += std::abs(signedArea) / 2;
			area3 += frames[i][2] * frames[i][5] / 2;
		}
		if (!folded.isEmpty())
			return fail(error, Text::tr("An island folds over itself when flattened. Mark a seam to open it and unwrap again. Faces: %1.")
								   .arg(faceList(folded)));
		// Uniform density: scale an island the pins do not fix to its model area.
		const double factor = userPinned || !(areaUv > 0) ? 1 : std::sqrt(area3 / areaUv);
		if (!userPinned)
		{
			for (auto u = unknown.cbegin(); u != unknown.cend(); ++u)
			{
				uvU[u.key()] *= factor;
				uvV[u.key()] *= factor;
			}
		}
		// Overlaps inside the island.
		{
			QVector<ModelTexCoord> uv;
			QVector<uint32_t> indices;
			QHash<int, int> slot;
			for (int f : chart.faces)
				for (int i = 0; i < 3; ++i)
				{
					const int fan = fanOf[f * 3 + i];
					if (!slot.contains(fan))
					{
						slot.insert(fan, int(uv.size()));
						uv.append({float(uvU[fan]), float(uvV[fan])});
					}
					indices.append(uint32_t(slot.value(fan)));
				}
			ModelUvOverlapScan scan;
			if (!findModelUvOverlaps(uv, indices, {}, 1, 8000000, &scan, error, control))
				return false;
			if (!scan.pairs.isEmpty() || scan.limitReached)
			{
				const QVector<int> pair{sorted[chart.faces[scan.pairs.isEmpty() ? 0 : scan.pairs.first().first]],
										sorted[chart.faces[scan.pairs.isEmpty() ? 0 : scan.pairs.first().second]]};
				return fail(error, Text::tr("An island overlaps itself when flattened. Mark a seam where it wraps and unwrap again. "
											"Faces: %1.")
									   .arg(faceList(pair)));
			}
		}
		// Distortion per face against the island's density.
		ModelUvUnwrapIsland island;
		island.faces = sourceFaces;
		island.pinned = userPinned;
		island.iterations = iterations;
		const double rho2 = area3 > 0 ? (areaUv * factor * factor) / area3 : 1;
		double weighted = 0, weights = 0;
		island.minAreaRatio = 1e300;
		island.maxAreaRatio = 0;
		for (int i = 0; i < chart.faces.size(); ++i)
		{
			const int f = chart.faces[i];
			const auto &xy = frames[i];
			const int a = fanOf[f * 3], b = fanOf[f * 3 + 1], c = fanOf[f * 3 + 2];
			const double q1u = uvU[b] - uvU[a], q1v = uvV[b] - uvV[a], q2u = uvU[c] - uvU[a], q2v = uvV[c] - uvV[a];
			const double inv00 = 1 / xy[2], inv01 = -xy[4] / (xy[2] * xy[5]), inv11 = 1 / xy[5];
			const double ja = q1u * inv00, jb = q1u * inv01 + q2u * inv11, jc = q1v * inv00, jd = q1v * inv01 + q2v * inv11;
			const double e = (ja * ja + jb * jb + jc * jc + jd * jd) / 2, det = std::abs(ja * jd - jb * jc);
			const double root = std::sqrt(std::max(0.0, e * e - det * det));
			const double s1 = std::sqrt(e + root), s2 = std::sqrt(std::max(0.0, e - root));
			const double angle = s2 > 0 ? s1 / s2 : 1e9, area = rho2 > 0 ? s1 * s2 / rho2 : 1;
			const double faceArea = xy[2] * xy[5] / 2;
			island.maxAngleDistortion = std::max(island.maxAngleDistortion, angle);
			island.minAreaRatio = std::min(island.minAreaRatio, area);
			island.maxAreaRatio = std::max(island.maxAreaRatio, area);
			weighted += angle * faceArea;
			weights += faceArea;
		}
		island.meanAngleDistortion = weights > 0 ? weighted / weights : 1;
		summary.maxAngleDistortion = std::max(summary.maxAngleDistortion, island.maxAngleDistortion);
		summary.minAreaRatio = std::min(summary.minAreaRatio, island.minAreaRatio);
		summary.maxAreaRatio = std::max(summary.maxAreaRatio, island.maxAreaRatio);
		summary.islands.append(island);
		if (!userPinned)
		{
			Placed placed{unknown.keys(), 1e300, 1e300, -1e300, -1e300};
			for (int fan : placed.fans)
			{
				placed.minU = std::min(placed.minU, uvU[fan]);
				placed.maxU = std::max(placed.maxU, uvU[fan]);
				placed.minV = std::min(placed.minV, uvV[fan]);
				placed.maxV = std::max(placed.maxV, uvV[fan]);
			}
			row.append(placed);
		}
	}
	// Unpinned islands sit side by side along U at one density, scaled down
	// together when they would leave the tile.
	if (!row.isEmpty())
	{
		double width = 0, height = 0;
		for (const auto &placed : row)
		{
			width += placed.maxU - placed.minU;
			height = std::max(height, placed.maxV - placed.minV);
		}
		const double gap = width * .02;
		width += gap * (row.size() + 1);
		height += gap * 2;
		// Square texels on the atlas's aspect: U spans `aspect` times V.
		const int atlasHeight = options.atlas.height == 0 ? options.atlas.resolution : options.atlas.height;
		const double aspect = double(options.atlas.resolution) / std::max(1, atlasHeight);
		const double fit = 1 / std::max({width / aspect, height, 1e-12});
		double cursor = gap;
		for (const auto &placed : row)
		{
			for (int fan : placed.fans)
			{
				uvU[fan] = (uvU[fan] - placed.minU + cursor) * fit / aspect;
				uvV[fan] = (uvV[fan] - placed.minV + gap) * fit;
			}
			cursor += placed.maxU - placed.minU + gap;
		}
	}
	QVector<ModelTexCoord> uv;
	for (int fan = 0; fan < fanSource.size(); ++fan)
	{
		if (!std::isfinite(uvU[fan]) || !std::isfinite(uvV[fan]) || std::abs(uvU[fan]) > 1000000 || std::abs(uvV[fan]) > 1000000)
			return fail(error, Text::tr("The flattened UVs exceed the supported coordinate range. Check the pinned UVs."));
		uv.append({float(uvU[fan]), float(uvV[fan])});
	}
	QVector<uint32_t> indices;
	for (int f = 0; f < count; ++f)
		for (int i = 0; i < 3; ++i)
			indices.append(uint32_t(fanOf[f * 3 + i]));
	ModelSurface mapped;
	if (!applyModelUvMapping(source, sorted, fanSource, uv, indices, options.atlas.vertexLimit, &mapped, error, control))
		return false;
	if (options.pack)
	{
		// Packing keeps each island's shape, orientation and relative density.
		// Islands held by two or more pins stay where their pins put them, as
		// Blender's unwrap leaves pinned islands out of its packing; only the
		// others are packed.
		QSet<int> loose;
		for (const auto &island : summary.islands)
			if (!island.pinned)
				for (int face : island.faces)
					loose.insert(face);
		if (!loose.isEmpty())
		{
			ModelSurface packed;
			if (!atlasModelUv(mapped, loose, options.frame, false, options.atlas, &packed, error, control))
				return false;
			mapped = std::move(packed);
		}
	}
	if (!work.check())
		return false;
	*result = std::move(mapped);
	if (report)
		*report = std::move(summary);
	return true;
}

QJsonObject modelUvUnwrapJson(const ModelUvUnwrapReport &report)
{
	QJsonArray islands;
	for (const auto &island : report.islands)
	{
		QJsonArray faces;
		for (int face : island.faces)
			faces.append(face);
		islands.append(QJsonObject{{"faces", faces},
								   {"pinned", island.pinned},
								   {"iterations", island.iterations},
								   {"meanAngleDistortion", island.meanAngleDistortion},
								   {"maxAngleDistortion", island.maxAngleDistortion},
								   {"minAreaRatio", island.minAreaRatio},
								   {"maxAreaRatio", island.maxAreaRatio}});
	}
	return {{"method", "conformal"},
			{"islands", islands},
			{"maxAngleDistortion", report.maxAngleDistortion},
			{"minAreaRatio", report.minAreaRatio},
			{"maxAreaRatio", report.maxAreaRatio}};
}
} // namespace vibestudio
