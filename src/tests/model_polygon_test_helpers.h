#pragma once

// Fixtures and checks shared by the polygon modelling tool tests (individual
// origins, arrays, extrude variants, bridging, symmetry, face creation, rips,
// bevels and intersection cuts). Every fixture is built in code.

#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_geometric_topology.h"
#include "core/model_geometry_helpers.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#include <cmath>
#include <functional>
#include <iostream>

namespace vibestudio::tests
{
struct PolygonChecks
{
	int checks = 0, failures = 0;
	bool operator()(bool ok, const char *message, const QString &error = {})
	{
		++checks;
		if (!ok)
		{
			++failures;
			std::cerr << "FAIL: " << message;
			if (!error.isEmpty())
				std::cerr << ": " << error.toUtf8().constData();
			std::cerr << '\n';
		}
		return ok;
	}
};
inline bool nearPoint(ModelVec3 a, ModelVec3 b, double tolerance = 1e-4)
{
	return model_geometry::length(model_geometry::point(a) - model_geometry::point(b)) <= tolerance;
}
inline bool samePoints(const QVector<ModelVec3> &a, const QVector<ModelVec3> &b, double tolerance = 1e-5)
{
	if (a.size() != b.size())
		return false;
	for (int i = 0; i < a.size(); ++i)
	{
		if (!nearPoint(a[i], b[i], tolerance))
			return false;
	}
	return true;
}
inline bool samePoses(const ModelSurface &a, const ModelSurface &b, double tolerance = 1e-5)
{
	if (a.frames.size() != b.frames.size())
		return false;
	for (int f = 0; f < a.frames.size(); ++f)
	{
		if (!samePoints(a.frames[f].positions, b.frames[f].positions, tolerance))
			return false;
	}
	return true;
}
// A mesh with one surface from these positions and triangles. Every vertex
// faces +Z in its first pose; each later pose moves the first by `poseOffset`
// times the pose index. UVs are planar from X and Y unless given.
inline ModelMesh polygonMesh(const QVector<ModelVec3> &positions, const QVector<ModelTriangle> &triangles, int poses = 2,
							 ModelVec3 poseOffset = {0, 0, 8}, const QVector<ModelTexCoord> &uvs = {})
{
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	auto mesh = buildModelDesignMesh(design);
	while (mesh.frames.size() < poses)
	{
		mesh.frames << mesh.frames[0];
		mesh.frames.last().name = QStringLiteral("pose%1").arg(mesh.frames.size());
	}
	mesh.frames.resize(poses);
	auto &surface = mesh.surfaces[0];
	surface.triangles = triangles;
	surface.vertexCount = int(positions.size());
	surface.texCoords.clear();
	if (uvs.isEmpty())
	{
		for (const auto &p : positions)
			surface.texCoords.append({p.x / 64.0f, p.y / 64.0f});
	}
	else
		surface.texCoords = uvs;
	surface.uvSeams.clear();
	surface.topologyVertices.clear();
	ModelFrameGeometry frame;
	frame.positions = positions;
	frame.normals.fill({0, 0, 1}, positions.size());
	surface.frames.clear();
	for (int pose = 0; pose < poses; ++pose)
	{
		auto moved = frame;
		for (auto &p : moved.positions)
			p = {p.x + poseOffset.x * pose, p.y + poseOffset.y * pose, p.z + poseOffset.z * pose};
		surface.frames.append(moved);
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}
// Appends a square of side `size` in the plane through `centre` spanned by
// `u` and `v` (two counter-clockwise triangles about u x v).
inline void appendSquare(QVector<ModelVec3> *positions, QVector<ModelTriangle> *triangles, ModelVec3 centre, ModelVec3 u, ModelVec3 v,
						 float size = 8)
{
	const int base = int(positions->size());
	const float h = size / 2;
	for (const auto &[a, b] : {std::pair{-h, -h}, std::pair{h, -h}, std::pair{h, h}, std::pair{-h, h}})
		positions->append({centre.x + u.x * a + v.x * b, centre.y + u.y * a + v.y * b, centre.z + u.z * a + v.z * b});
	triangles->append({base, base + 1, base + 2});
	triangles->append({base, base + 2, base + 3});
}
// A flat grid of quads (two triangles each) in the XY plane.
inline ModelMesh polygonGrid(int nx, int ny, float size = 8, int poses = 2)
{
	QVector<ModelVec3> positions;
	QVector<ModelTriangle> triangles;
	for (int j = 0; j <= ny; ++j)
	{
		for (int i = 0; i <= nx; ++i)
			positions.append({i * size, j * size, 0});
	}
	const auto at = [&](int i, int j) { return j * (nx + 1) + i; };
	for (int j = 0; j < ny; ++j)
	{
		for (int i = 0; i < nx; ++i)
		{
			triangles.append({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
			triangles.append({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
		}
	}
	return polygonMesh(positions, triangles, poses);
}
// A closed cube of half-size h with eight shared corners and outward faces.
inline ModelMesh polygonCube(float h = 8, ModelVec3 centre = {}, int poses = 2)
{
	QVector<ModelVec3> p{{-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h}, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}};
	for (auto &q : p)
		q = {q.x + centre.x, q.y + centre.y, q.z + centre.z};
	const QVector<ModelTriangle> t{{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
								   {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}};
	auto mesh = polygonMesh(p, t, poses);
	for (auto &frame : mesh.surfaces[0].frames)
	{
		for (int v = 0; v < 8; ++v)
		{
			const auto q = p[v];
			const float l = std::sqrt(3.0f) * h;
			frame.normals[v] = {(q.x - centre.x) / l, (q.y - centre.y) / l, (q.z - centre.z) / l};
		}
	}
	return mesh;
}
inline QSet<int> allFaces(const ModelSurface &surface)
{
	QSet<int> faces;
	for (int face = 0; face < surface.triangles.size(); ++face)
		faces.insert(face);
	return faces;
}
inline ModelVec3 boundsCentre(const ModelSurface &surface, int frame, const QVector<int> &vertices)
{
	double low[3]{INFINITY, INFINITY, INFINITY}, high[3]{-INFINITY, -INFINITY, -INFINITY};
	for (int v : vertices)
	{
		const auto p = surface.frames[frame].positions[v];
		const double values[3]{p.x, p.y, p.z};
		for (int axis = 0; axis < 3; ++axis)
		{
			low[axis] = std::min(low[axis], values[axis]);
			high[axis] = std::max(high[axis], values[axis]);
		}
	}
	return {float((low[0] + high[0]) / 2), float((low[1] + high[1]) / 2), float((low[2] + high[2]) / 2)};
}
// Every geometric edge has two faces with opposite windings (a closed,
// consistently wound surface); open edges are allowed when `open` is set.
inline bool consistentlyWound(const ModelSurface &surface, bool open = false)
{
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology))
		return false;
	QHash<ModelEdge, int> balance;
	for (const auto &t : surface.triangles)
	{
		for (const auto &edge : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
		{
			const int a = topology.group(edge.first), b = topology.group(edge.second);
			balance[modelEdge(a, b)] += a < b ? 1 : -1;
		}
	}
	for (auto i = topology.edgeFaces.cbegin(); i != topology.edgeFaces.cend(); ++i)
	{
		if (i->size() > 2 || (i->size() == 1 && !open) || (i->size() == 2 && balance[i.key()] != 0))
			return false;
	}
	return true;
}
inline double uvArea(const ModelSurface &surface, int face)
{
	const auto &t = surface.triangles[face];
	const auto a = surface.texCoords[t.a], b = surface.texCoords[t.b], c = surface.texCoords[t.c];
	return std::abs((double(b.u) - a.u) * (double(c.v) - a.v) - (double(b.v) - a.v) * (double(c.u) - a.u)) / 2;
}
// Runs the actual executable's CLI; JSON output lands in `json` when given.
inline bool runPolygonCli(const QString &executable, const QString &directory, const QStringList &arguments, int expected,
						  QJsonObject *json = nullptr)
{
	QProcess process;
	process.setWorkingDirectory(directory);
	process.start(executable, QStringList{"--cli", "--settings-file", QDir(directory).filePath("settings.ini")} + arguments);
	if (!process.waitForFinished(60000))
	{
		process.kill();
		process.waitForFinished();
		return false;
	}
	const auto out = process.readAllStandardOutput();
	if (process.exitCode() != expected)
		std::cerr << out.constData() << process.readAllStandardError().constData() << '\n';
	if (json)
		*json = QJsonDocument::fromJson(out).object();
	return process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected;
}
} // namespace vibestudio::tests
