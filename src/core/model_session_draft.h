#pragma once

// Staged sessions beside model recovery copies (ROB-9). Paint Skin Weights and
// Animation Curves stage their work in memory and commit it as one document
// edit, so a crash used to lose a long painting or curve session. While one of
// them is open the Mesh Editor keeps a recovery copy of the model it started
// from (core/model_recovery.h) and writes the staged work beside it as a
// sidecar, <recovery id>.<kind>.vsmeshsession, together with the document
// fingerprint the work was staged on.
//
// A sidecar is a one-line JSON header (kind, owner, base fingerprint, counts,
// payload SHA-256) followed by the payload: the weight-paint stroke recipe
// (core/model_weight_stroke_recipe.h) or one clip's joint curves
// (core/model_animation_curves.h). Lists read the header only. Restoring the
// recovery copy offers the staged work for review again; a sidecar whose base
// fingerprint differs from the restored model is refused, as its strokes or
// keys were made on another version. Sidecars are removed with their copy
// (removeModelRecovery) and, once their copy is gone, by the retention policy
// (pruneModelRecoveries).

#include "core/model_weight_paint.h"

#include <QDateTime>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

namespace vibestudio
{

enum class ModelSessionDraftKind
{
	WeightPaint,
	AnimationCurves
};

struct ModelSessionDraft
{
	ModelSessionDraftKind kind = ModelSessionDraftKind::WeightPaint;
	// The recovery copy the work belongs to (a UUID without braces).
	QString ownerId;
	// ModelDocument::revisionFingerprint() of the model the work was staged on.
	QByteArray baseFingerprint;
	QString title;
	qint64 ownerProcessId = 0;
	QDateTime writtenUtc;
	// Paint Skin Weights: the staged strokes in order, on `surface`.
	int surface = 0;
	QVector<ModelWeightStroke> strokes;
	// Animation Curves: the staged curves of one joint in clip `clip`, which
	// has `frameCount` frames; `bake` stages removing them in favour of the
	// sampled poses.
	int clip = -1;
	int frameCount = 0;
	bool bake = false;
	ModelPoseCurves curves;
	// Filled by reading: the file, and the header's counts (also when only
	// the header was read).
	QString path;
	qint64 strokeCount = 0, sampleCount = 0, keyCount = 0;
	QString joint;
	QString error;
	[[nodiscard]] bool isValid() const { return !ownerId.isEmpty() && error.isEmpty(); }
};

[[nodiscard]] QString modelSessionDraftKindId(ModelSessionDraftKind kind);
[[nodiscard]] QString modelSessionDraftPath(const QString &directory, const QString &ownerId, ModelSessionDraftKind kind);
// Writes atomically, replacing the previous sidecar of the same owner and
// kind. Refuses an invalid owner, a folder that is a link, and a sidecar over
// the 64 MiB source limit.
bool writeModelSessionDraft(const QString &directory, const ModelSessionDraft &draft, QString *error = nullptr);
// Headers only, newest first, at most 256: every sidecar in `directory`, or
// those of `ownerId`. Unreadable headers are listed with `error` set.
[[nodiscard]] QVector<ModelSessionDraft> inspectModelSessionDrafts(const QString &directory, const QString &ownerId = {});
// The whole sidecar, its payload hash verified.
bool readModelSessionDraft(const QString &path, ModelSessionDraft *draft, QString *error = nullptr);
// Removes the sidecars of `ownerId` (one kind, or every kind when `kind` is null).
bool removeModelSessionDrafts(const QString &directory, const QString &ownerId, QString *error = nullptr,
							  const ModelSessionDraftKind *kind = nullptr);
// "12 staged weight-paint strokes (4,096 samples)" or "Staged curves for joint
// arm in clip 2 (18 keys)", translated.
[[nodiscard]] QString modelSessionDraftSummary(const ModelSessionDraft &draft);
// The header for CLI JSON: kind, owner, base fingerprint, counts, path.
[[nodiscard]] QJsonObject modelSessionDraftJson(const ModelSessionDraft &draft);

} // namespace vibestudio
