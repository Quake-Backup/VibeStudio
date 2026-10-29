#pragma once

// The weight-paint stroke recipe: {"version": 1, "strokes": [...]}, each stroke
// with its surface, joint, mode, value, strength, locked joints, remainder
// joint and [vertex, opacity] samples. `model weight-paint --stroke-file`
// replays one, the Paint Skin Weights dialog's recovery sidecar stores its
// staged strokes in it (core/model_session_draft.h), and `model recover
// --strokes-output` writes one, so a crashed session can be replayed by
// either surface.

#include "core/model_weight_paint.h"

#include <QJsonObject>

namespace vibestudio
{

[[nodiscard]] QJsonObject modelWeightStrokeRecipeJson(const QVector<ModelWeightStroke> &strokes);
// Refuses unknown fields, wrong types, and more than modelWeightMaxStrokes
// strokes or modelWeightMaxSamples samples; `strokes` is left unchanged then.
bool parseModelWeightStrokeRecipe(const QJsonObject &root, QVector<ModelWeightStroke> *strokes, QString *error = nullptr);

} // namespace vibestudio
