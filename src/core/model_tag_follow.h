#pragma once

// Attachment tags that follow mesh components across frames
// (ModelEditKind::FollowTagComponents). For vertex-animated models (MD2-derived
// frames, OBJ sequences, hand-animated MD3) a tag such as tag_weapon or
// tag_head is fitted to the selected vertices, edges or faces in every frame
// of a range, keeping the offset it has from them in a reference frame. The
// results are ordinary per-frame tags (native MD3), not a live constraint:
// later geometry edits do not move them until the tag is fitted again.
//
// Modes:
// - Rigid: the best-fit rotation and translation of the selected vertices
//   from the reference frame to each frame (Horn's closed-form quaternion
//   solution to the absolute orientation problem; B. K. P. Horn, "Closed-form
//   solution of absolute orientation using unit quaternions", JOSA A 4(4),
//   1987). The rotation is always proper, so a reflected tag basis keeps its
//   handedness. The per-frame RMS residual says where the selection does not
//   move rigidly.
// - Centroid: the origin follows the selection's centroid; the axes stay.
// - Surface: the rotation comes from the selected faces' area-weighted
//   normal and the direction of the first selected face's first edge in the
//   face plane.
//
// Rigid and surface modes need at least three non-collinear selected points;
// a frame whose fit is degenerate refuses the whole edit.

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QJsonArray>
#include <QLocale>

namespace vibestudio
{
struct ModelEdit;
struct ModelSelection;

enum class ModelTagFollowMode
{
	Rigid,
	Centroid,
	Surface,
};

// ModelEdit::tagFollow. Defaults are required by every other edit.
struct ModelTagFollowOptions
{
	// An existing tag, or a new name: a new tag starts at the selection's
	// centroid in the reference frame with model-aligned axes, and frames
	// outside the range keep that pose.
	QString tag;
	ModelTagFollowMode mode = ModelTagFollowMode::Rigid;
	// The frame whose tag pose sets the tag's offset from the selection.
	int referenceFrame = 0;
	// The inclusive frames to fit; -1 and -1 fit every frame.
	int first = -1;
	int last = -1;
	// Keep an anchor (ModelMesh::tagAnchors) so Refit Anchored Tags can fit
	// the tag again after geometry edits.
	bool keepAnchor = true;
	bool operator==(const ModelTagFollowOptions &) const = default;
};

// The editable source version that first stores tag anchors.
inline constexpr int modelSourceTagAnchorVersion = 15;

struct ModelTagFollowResult
{
	// One fitted pose per frame of the range, in frame order.
	QVector<ModelTag> poses;
	// Per fitted frame, the RMS distance between the selected points and the
	// reference points carried by the fit (zero for rigid motion).
	QVector<double> residuals;
	double maxResidual = 0;
	int worstFrame = -1;
	bool created = false;
	// The tag's pose in the reference frame before fitting: the existing
	// tag, or for a new tag the selection's centroid with model axes.
	ModelTag start;
	int points = 0;
};

// Fits the tag; false with a translated reason, changing nothing.
bool solveModelTagFollow(const ModelMesh &mesh, const ModelSelection &selection, const ModelTagFollowOptions &options,
						 ModelTagFollowResult *result, QString *error = nullptr, const ModelWorkControl &control = {});
// "tag_weapon follows 12 points across 40 frames; largest residual 0.003 in frame 17."
[[nodiscard]] QString modelTagFollowSummary(const ModelTagFollowOptions &options, const ModelTagFollowResult &result,
											const QLocale &locale = QLocale::c());
// Why an anchor can no longer be refitted (its tag, surface, components or
// frames changed since it was fitted); empty when it can.
[[nodiscard]] QString modelTagAnchorProblem(const ModelMesh &mesh, const ModelTagAnchor &anchor);
// Refit one anchor: the fit Follow Selection would make with its stored
// components and settings.
bool solveModelTagAnchor(const ModelMesh &mesh, const ModelTagAnchor &anchor, ModelTagFollowResult *result, QString *error = nullptr,
						 const ModelWorkControl &control = {});
QJsonArray modelTagAnchorsJson(const QVector<ModelTagAnchor> &anchors);
bool parseModelTagAnchors(const QJsonValue &value, QVector<ModelTagAnchor> *anchors, QString *error = nullptr);
// Candidate-only implementation of FollowTagComponents and
// RefitAnchoredTags (tagFollow.tag names one anchored tag, or every one when
// empty); applyModelEdit validates and adopts.
bool applyModelTagFollowEdit(ModelMesh *candidate, const ModelEdit &edit, QString *error = nullptr, const ModelWorkControl &control = {});
} // namespace vibestudio
