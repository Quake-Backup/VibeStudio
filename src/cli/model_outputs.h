#pragma once

#include "cli/model_tools.h"

namespace vibestudio::cli
{
// `model outputs`: the receipts of a project's model outputs (core/model_outputs.h)
// with their status, refreshing stale exports and detail levels with the
// recorded settings, forgetting a receipt, or checking a player package's
// embedded receipt.
ModelToolsCliResult runModelOutputs(const QStringList &arguments);
} // namespace vibestudio::cli
