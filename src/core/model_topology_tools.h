#pragma once

// Polygon modelling tools for idTech models, part A: connecting vertices and
// edges, deleting and dissolving vertices and edges, limited dissolve,
// recalculating outside, marking sharp edges, placing vertices at explicit
// positions and welding onto a target vertex.
//
// Each tool is a candidate-only operation dispatched by applyModelMeshToolEdit
// (core/model_mesh_tools.h): applyModelEdit validates and commits the whole
// document as one undo step. Tools reason about geometric topology
// (core/model_geometric_topology.h), so UV and hard-normal seam copies act as
// one vertex, and every animation pose follows the same topology. Tool names
// and behaviour follow Blender's documented edit-mode operators
// (https://docs.blender.org/manual/en/latest/modeling/meshes/editing/index.html)
// and 3ds Max's Editable Poly Connect and Remove; no code from either is used.

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QString>
#include <QVector>

#include <array>

namespace vibestudio
{
struct ModelEdit;
struct ModelSelection;
struct ModelEditReport;

// Delete (ModelEditKind::DeleteGeometry): what a vertex or edge selection removes.
enum class ModelDeleteMode
{
	// Every face touching a selected vertex (Blender's Delete Vertices).
	Vertices,
	// Every face using a selected edge (Delete Edges).
	Edges,
	// Faces whose three corners are selected (Delete Faces).
	Faces
};

struct ModelTopologyToolOptions
{
	ModelDeleteMode deleteMode = ModelDeleteMode::Faces;
	// Dissolve Edges: also dissolve vertices left between exactly two
	// remaining edges along the dissolved chains (Blender's Dissolve Vertices).
	bool dissolveVertices = true;
	// Limited Dissolve: the largest angle in degrees between neighbouring face
	// normals that still merge (0-180), and between border edges that still
	// count as one straight edge.
	double dissolveAngle = 5;
	// Recalculate Outside: point the faces inward instead.
	bool inside = false;
	// Set Vertex Positions: explicit positions for these vertices in the edited
	// pose(s). Every geometric copy of a listed vertex takes the position.
	QVector<int> vertices;
	QVector<std::array<double, 3>> positions;
	// Merge at Target (ModelMergeMode::Target): the vertex whose position, UV
	// and normal the merged vertices take.
	int targetVertex = -1;
	// Proportional transform: weld moved vertices that land within this
	// distance of another vertex in the reference pose (0 = off).
	double autoMergeDistance = 0;
	// Topology tools that only check local collapse: refuse, rather than
	// report, faces that newly intersect other faces (core/model_intersections.h).
	bool refuseIntersections = false;
	bool operator==(const ModelTopologyToolOptions &) const = default;
};

// Candidate-only entry points; applyModelMeshToolEdit dispatches them.
bool connectModelVertices(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error,
						  const ModelWorkControl &control, ModelEditReport *report);
bool connectModelEdges(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error,
					   const ModelWorkControl &control, ModelEditReport *report);
bool deleteModelGeometry(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error,
						 const ModelWorkControl &control, ModelEditReport *report);
bool dissolveModelEdges(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error,
						const ModelWorkControl &control, ModelEditReport *report);
bool limitedDissolveModel(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error,
						  const ModelWorkControl &control, ModelEditReport *report);
bool recalculateModelOutside(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error,
							 const ModelWorkControl &control);
bool markModelSharpEdges(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error,
						 const ModelWorkControl &control);
bool setModelVertexPositions(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error,
							 const ModelWorkControl &control);
} // namespace vibestudio
