#pragma once

// Selection and lookup helpers the editors share: Radiant's Expand
// Selection to Whole Entities, and the "entity N, brush M" numbering Radiant's
// Find Brush and the Quake compilers' messages use.

#include "core/level_map.h"

namespace vibestudio {

// The selection with each brush and patch of a brush entity replaced by that
// entity, as Radiant's Expand Selection to Whole Entities does. World brushes
// and patches, point entities and Doom objects stay as they are.
[[nodiscard]] QVector<LevelMapSelectionRef> levelMapSelectionWholeEntities(const LevelMapDocument& document);

// Entity `entity` in file order (worldspawn is 0) and its primitive
// `primitive`, counting brushes and patches together in the order they are
// written, as Radiant's Find Brush and q3map2's and ericw-tools' "Entity 0,
// Brush 12" messages count them. A primitive of -1 names the entity itself.
bool levelMapObjectAtNumber(const LevelMapDocument& document, int entity, int primitive, LevelMapSelectionRef* ref,
	QString* error = nullptr);
// The numbers of an object, the reverse of levelMapObjectAtNumber(); an
// entity's own primitive number is -1.
bool levelMapObjectNumber(const LevelMapDocument& document, const LevelMapSelectionRef& ref, int* entity, int* primitive);
// "Entity 3, Brush 12" or "entity 3 brush 12" in a compiler line, as the two
// numbers; false when the line names none.
bool parseLevelMapObjectNumber(const QString& text, int* entity, int* primitive);

} // namespace vibestudio
