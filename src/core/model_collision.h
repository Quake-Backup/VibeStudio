#pragma once

#include "core/level_model_instance.h"
#include "core/level_placement.h"
#include "core/model_mesh.h"
#include <QJsonArray>
#include <array>
#include <optional>

namespace vibestudio
{
enum class ModelEditKind;
struct ModelEdit;
struct ModelSelection;
struct ModelTransform;
inline constexpr int modelCollisionBoxLimit = 64;

void setModelCollisionFrames(ModelCollisionBox *box, QVector<ModelCollisionPose> poses);
// Samples a validated box into a static result. Endpoints are exact; intermediate centres and
// sizes are linear, orientations follow the same shortest arc as attachment tags.
bool sampleModelCollisionBox(const ModelCollisionBox &box, int frame, int nextFrame, double amount, ModelCollisionBox *result);

std::array<ModelVec3, 8> modelCollisionCorners(const ModelCollisionBox &box);
std::array<ModelVec3, 3> modelCollisionAxes(const ModelCollisionBox &box);
// Translation/rotation use the chosen basis; size scaling always uses the box's
// own local axes about the world-space pivot.
// Preserves an oriented box without introducing shear. Atomic on failure.
bool transformModelCollisionBox(const ModelCollisionBox &box, const ModelTransform &transform, ModelCollisionBox *result,
								QString *error = nullptr);
QStringList validateModelCollision(const ModelMesh &mesh, const ModelWorkControl &control = {});
// A static volume carried through an attachment basis (axis rows and origin,
// as ModelTag places a child) after a uniform scale. Boxes stay oriented boxes;
// a reflected basis is turned into the equivalent proper rotation (a box is
// symmetric; a hull's points are mirrored to match). Refuses a basis whose axes
// are not orthonormal within 1%; smaller drift is straightened.
bool composeModelCollisionVolume(const ModelCollisionBox &volume, const ModelTag &basis, double scale, ModelCollisionBox *result,
								 QString *error = nullptr);
const ModelCollisionBox *findModelCollisionBox(const ModelMesh &mesh, const QString &name);
bool validModelCollisionSelection(const ModelMesh &mesh, const ModelSelection &selection);
bool isModelCollisionEdit(ModelEditKind kind);
// Candidate-only edit; applyModelEdit validates and commits the whole state.
bool applyModelCollisionEdit(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error,
							 const ModelWorkControl &control = {});
QJsonArray modelCollisionJson(const ModelMesh &mesh, QString *error = nullptr, const ModelWorkControl &control = {});
bool parseModelCollision(const QJsonValue &value, ModelMesh *mesh, QString *error, const ModelWorkControl &control = {});

struct ModelCollisionExport
{
	QString target; // quake, quake2, quake3; always explicit
	// Quake III requires a project shader below textures/, without that prefix.
	// Its surfaceparms, not numeric map flags, determine compiler behavior.
	QString material;
	ModelVec3 origin;
	// Required when any box is animated. A map receives this stored pose only.
	int frame = -1;
	// A placed model instance's whole transform (levelModelInstanceAffine in
	// core/level_model_instance.h), row by row with the translation last. Every
	// corner goes through it, rotation and non-uniform scale included, instead
	// of `origin`, which must then be zero.
	std::optional<std::array<double, 12>> instance;
	// Level placement records where the brushes came from (see
	// core/model_collision_placement.h): the mesh source as the caller names it,
	// and the instance's model key when there is an instance.
	QString sourcePath;
	QString instanceModel;
};
struct ModelCollisionMap
{
	QByteArray bytes;
	QStringList notes;
	QString material;
	int brushCount = 0;
};
// One static worldspawn brush per box. Never changes render meshes or frames.
// With `groupProperties` the brushes are wrapped in one func_group entity
// carrying those keys instead (level placement records provenance that way).
bool exportModelCollisionMap(const ModelMesh &mesh, const ModelCollisionExport &request, ModelCollisionMap *result,
							 QString *error = nullptr, const ModelWorkControl &control = {},
							 const QVector<LevelMapProperty> &groupProperties = {});
// Shared placement/history service: one map undo step, scene locks, cancellation,
// target checks. Caller publishes the returned candidate after context checks.
// The brushes go into one func_group that records the source, collision
// revision, frame, target and placement transform, so they can be listed as
// current or stale and refreshed (core/model_collision_placement.h).
LevelPlacementResult prepareModelCollisionPlacement(const ModelMesh &mesh, const ModelCollisionExport &request, const LevelMapDocument &map,
													const ModelWorkControl &control = {});
QString modelCollisionOmissionNote();
} // namespace vibestudio
