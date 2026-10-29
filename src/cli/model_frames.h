#pragma once

#include "cli/model_tools.h"

namespace vibestudio::cli
{
// `model frames`: frame-range editing, frame naming after clips and the
// native clip check (core/model_frame_tools.h) through the shared document
// service, saved as a new editable source.
ModelToolsCliResult runModelFrames(const QStringList &arguments);
} // namespace vibestudio::cli
