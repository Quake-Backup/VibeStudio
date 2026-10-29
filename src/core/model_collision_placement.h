#pragma once

#include "core/model_collision.h"

namespace vibestudio
{
// Collision placed in a level keeps a record of where it came from. Each
// placement is one func_group entity holding the brushes; its keys name the
// mesh source, a fingerprint of the collision volumes, the frame, the target
// and material, and the placement transform. func_group is merged into the
// world by ericw-tools/VibeMap2, Quake II qbsp3 and q3map2/VibeMap3, as
// TrenchBroom's groups are, so the record changes nothing in the compiled map.
inline constexpr auto modelCollisionRecordSource = "_vs_collision_source";
inline constexpr auto modelCollisionRecordRevision = "_vs_collision_revision";
inline constexpr auto modelCollisionRecordFrame = "_vs_collision_frame";
inline constexpr auto modelCollisionRecordTarget = "_vs_collision_target";
inline constexpr auto modelCollisionRecordMaterial = "_vs_collision_material";
inline constexpr auto modelCollisionRecordTransform = "_vs_collision_transform";
inline constexpr auto modelCollisionRecordModel = "_vs_collision_model";

// SHA-256 of the collision volumes as the source stores them. Mesh edits that
// leave the volumes alone keep it; any volume edit changes it.
QByteArray modelCollisionFingerprint(const ModelMesh &mesh);

enum class ModelCollisionRecordStatus
{
	Current,	 // the source matches and its volumes are unchanged
	Stale,		 // the source matches but its volumes changed since placement
	OtherSource, // placed from another (or an unrecorded) mesh source
	Invalid		 // keys are missing or malformed; refresh is refused
};
struct ModelCollisionRecord
{
	int entityId = -1;
	QString source;			// as recorded, relative to the map's folder when it could be
	QString resolvedSource; // absolute, or empty when nothing was recorded
	QByteArray revision;
	int frame = -1;
	QString target, material, model;
	std::array<double, 12> transform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
	int brushCount = 0;
	ModelCollisionRecordStatus status = ModelCollisionRecordStatus::Invalid;
	QString problem;
};
// Keys written on a new placement's func_group.
QVector<LevelMapProperty> modelCollisionRecordProperties(const ModelMesh &mesh, const ModelCollisionExport &request,
														 const LevelMapDocument &map);
// Every collision record in the map. With a mesh, status compares the record
// against that mesh and `meshSource` (its file); without one, records are
// Stale or OtherSource only by their keys' validity (Current is never claimed).
QVector<ModelCollisionRecord> levelCollisionRecords(const LevelMapDocument &map, const ModelMesh *mesh = nullptr,
													const QString &meshSource = {});
QString modelCollisionRecordStatusId(ModelCollisionRecordStatus status);
// The selected model instance of an open level: the primary selection when it
// is an entity that places a model. Fills the request's instance and model key.
bool modelCollisionInstanceFromSelection(const LevelMapDocument &map, ModelCollisionExport *request, int entityId = -1,
										 QString *error = nullptr);
// Replaces one record's brushes with the mesh's current volumes, using the
// recorded target, material, frame and transform, as one level undo step. With
// `instanceEntityId`, the brushes follow that model entity's current transform
// instead (for an instance that moved, turned or was rescaled). The record
// must name `meshSource` (or have none recorded, when `allowUnrecordedSource`).
// Scene locks are honoured: a locked group or layer refuses the refresh. The
// new func_group goes into the scene node the old one was in.
LevelPlacementResult prepareModelCollisionRefresh(const ModelMesh &mesh, const QString &meshSource, const LevelMapDocument &map,
												  int entityId, int instanceEntityId = -1, bool allowUnrecordedSource = false,
												  const ModelWorkControl &control = {});
} // namespace vibestudio
