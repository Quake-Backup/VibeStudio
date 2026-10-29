#pragma once

// Placing a model in a level, shared by the Levels page Models tab, the Mesh
// Editor's and Design Prop's Stage and Place, and `map place-model`.
//
// A Quake III map names models for VibeMap3 (q3map2's picomodel loader) to bake
// into the BSP, as misc_model: MD3, MDC, MD2, ASE, OBJ, LWO and FM load. A Quake
// or Quake II map places a runtime entity whose class the game or mod defines;
// MDL and MD2 load in the original engines, and other formats only in source
// ports, which the placement says. Doom maps have no model entities.
//
// The entity class comes from the request, else the loaded definitions
// (misc_model, then misc_gamemodel), else misc_model. Quake maps warn when no
// loaded definition declares it. "angles" (pitch yaw roll, or "angle" for a
// yaw alone) and "modelscale" are written only when they differ from the
// defaults.

#include "core/entity_definitions.h"
#include "core/level_map.h"

#include <QStringList>

namespace vibestudio
{

struct LevelModelPlacementRequest
{
	QString virtualPath;
	LevelMapVec3 origin;
	// Pitch, yaw and roll in degrees.
	LevelMapVec3 angles;
	double modelscale = 1.0;
	// Empty chooses from the definitions.
	QString entityClass;
	const EntityDefinitionCatalogue *definitions = nullptr;
};

struct LevelModelPlacementResult
{
	int entityId = -1;
	QString entityClass;
	QStringList warnings;
};

// The class a model is placed as for these definitions.
[[nodiscard]] QString chooseLevelModelEntityClass(const EntityDefinitionCatalogue *definitions);
// The model file suffixes a map of this format places, the engine's own first.
[[nodiscard]] QStringList levelModelPlacementSuffixes(LevelMapFormat format);
// Checks a placement without changing the map: the map format, a safe package
// path shorter than 64 bytes with a suffix the map loads, finite values and a
// positive scale. Fills the class and warnings.
bool checkLevelModelPlacement(const LevelMapDocument &document, const LevelModelPlacementRequest &request, LevelModelPlacementResult *result,
							  QString *error = nullptr);
// Adds the entity as one undo step and reports its id.
bool placeLevelModelEntity(LevelMapDocument *document, const LevelModelPlacementRequest &request, LevelModelPlacementResult *result,
						   QString *error = nullptr);
// The model path an entity names ("model" key), or empty: the reverse step from
// a placed entity to the Models page and its editable source. Brush models
// ("*1") are not files.
[[nodiscard]] QString levelEntityModelPath(const LevelMapEntity &entity);

} // namespace vibestudio
