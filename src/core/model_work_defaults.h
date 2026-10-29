#pragma once

// A process-wide fallback for model work started without a control (ROB-10).
// The command line installs its console cancellation token and --progress
// reporter here before running a model command, so every core operation that
// command reaches with an empty ModelWorkControl (a document load, edit or
// save, an assembly bake, a skinning update) stops at its next
// modelWorkCheckpoint on Ctrl+C and reports its phases. A control passed
// explicitly keeps its own callbacks; a field it leaves empty uses the
// fallback's. The studio never sets it: its workers pass their own controls.

#include "core/model_work.h"

namespace vibestudio
{
// Set once, before the work starts; not synchronised with running work.
void setModelWorkDefaults(ModelWorkControl control);
[[nodiscard]] const ModelWorkControl &modelWorkDefaults();
} // namespace vibestudio
