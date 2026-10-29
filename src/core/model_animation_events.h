#pragma once

// Animation events: game-side frame commands on clip frames (ModelAnimationEvent
// in core/model_mesh.h). Skeletal models keep them on their skeletal clips;
// vertex-animated models on their animations. Doom 3 model declarations carry
// them as `frame N command args` lines inside `anim` blocks (frame numbers are
// one-based there, zero-based here). See core/model_game_declarations.h.

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QJsonArray>
#include <QJsonValue>

namespace vibestudio
{
struct ModelEdit;

// At most this many events per clip.
inline constexpr int modelAnimationMaxEventsPerClip = 4096;

// A command is one token of ASCII letters, digits, underscores, dots or
// hyphens, starting with a letter or underscore (Doom 3 frame commands such as
// sound_body, melee, call, footstep). Arguments are tokens without quotes,
// braces, control characters or line breaks; Doom 3 quotes paths when needed.
bool validModelAnimationEvent(const ModelAnimationEvent &event, int frameCount, QString *error = nullptr);
bool validModelAnimationEvents(const QVector<ModelAnimationEvent> &events, int frameCount, QString *error = nullptr);
// Sorted by frame, keeping the order of events on one frame.
[[nodiscard]] QVector<ModelAnimationEvent> sortedModelAnimationEvents(QVector<ModelAnimationEvent> events);
// "command arg arg" (the command line of a Doom 3 frame command).
[[nodiscard]] QString modelAnimationEventCommandLine(const ModelAnimationEvent &event);
// Parses "command arg arg" into an event at `frame`.
bool parseModelAnimationEventCommandLine(const QString &text, int frame, ModelAnimationEvent *event, QString *error = nullptr);
[[nodiscard]] QJsonArray modelAnimationEventsJson(const QVector<ModelAnimationEvent> &events);
bool modelAnimationEventsFromJson(const QJsonValue &value, int frameCount, QVector<ModelAnimationEvent> *events, QString *error = nullptr);

// Every clip's and animation's events are valid and lie inside it; vertex
// animations of a skeletal model carry none.
bool validateModelAnimationEvents(const ModelMesh &mesh, QString *error = nullptr);
// After a frame edit (duplicate, delete, insert in-betweens, range changes),
// moves the vertex animations' events with their frames, or refuses an edit
// that would drop or move an event it cannot follow. `before` is the model
// before the edit, `after` the edited candidate.
bool remapModelAnimationEvents(const ModelMesh &before, ModelMesh *after, const ModelEdit &edit, QString *error = nullptr);
// Candidate-only EditAnimationEvents.
bool applyModelAnimationEvents(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control = {});
} // namespace vibestudio
