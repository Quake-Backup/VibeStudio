#pragma once
#include "core/level_object_selection.h"
namespace slopstudio::detail {
bool selectLevelMapEntityQuery(LevelMapDocument* document, const LevelObjectSelectionRequest& request,
	LevelObjectSelectionReport* report, QString* error);
}
