#pragma once
#include "core/model_mesh.h"
#include "core/model_work.h"
#include <QJsonValue>

namespace vibestudio
{
struct ModelEdit;
// SolveTwoBoneIk always uses the analytic two-bone solve. SolveChainIk solves a
// chain from rootJoint to endJoint with FABRIK (forward and backward reaching,
// Aristidou and Lasenby 2011) or cyclic coordinate descent.
enum class ModelIkSolver
{
	Fabrik,
	Ccd
};
struct ModelIkOptions
{
	int clip = -1, first = 0, last = 0;
	// SolveTwoBoneIk: the end joint's parent and grandparent define the chain.
	// SolveChainIk: the chain runs from rootJoint, an ancestor of endJoint at
	// least two bones up, which stays in place.
	QString endJoint;
	ModelVec3 target, pole;
	bool nearestReachable = false, keepEndOrientation = true;
	QString rootJoint;
	ModelIkSolver solver = ModelIkSolver::Fabrik;
	// Chain solves: iteration cap (1-1000) and end-target distance accepted as
	// reached; zero picks a tolerance from the chain's length.
	int iterations = 32;
	double tolerance = 0;
	// Chain solves: false keeps each sample's own bend instead of turning the
	// chain towards the pole (CCD never uses the pole).
	bool usePole = true;
	// Chain solves: clamp every rotated joint to its stored ModelJointLimits.
	bool limits = false;
	// Target source. With targetJoint, each sample's target is that joint's
	// model-space pose applied to targetOffset (a point in the joint's space),
	// so the end follows a moving joint or prop. With `targets` (one point per
	// sample, first to last), each sample has its own fixed target. Otherwise
	// every sample uses `target`.
	QString targetJoint;
	ModelVec3 targetOffset;
	QVector<ModelVec3> targets;
	bool operator==(const ModelIkOptions &other) const;
};
struct ModelIkSample
{
	int frame = 0;
	ModelVec3 end;
	double error = 0, tolerance = 0;
	bool reached = false;
	// The target this sample aimed at.
	ModelVec3 target;
};
// Read-only diagnostics from a solved mesh. Empty for invalid clip/chain/range.
// `chain` selects the SolveChainIk reading of the options.
QVector<ModelIkSample> modelIkSamples(const ModelMesh &mesh, const ModelIkOptions &options, bool chain = false);
// Candidate-only: use applyModelEdit/ModelDocument for transactional publication.
// Handles SolveTwoBoneIk and SolveChainIk.
bool applyModelIk(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control = {});

// Joint limits (ModelJoint::limits).
// The joint's parent-local rotation relative to its bind's, as X-then-Y-then-Z
// Euler degrees in the given pose.
[[nodiscard]] std::array<double, 3> modelJointLimitAngles(const ModelSkeleton &skeleton, const QVector<ModelJointMatrix> &pose, int joint);
// True when every limited joint of every clip sample lies within its limits
// (with `tolerance` degrees); `problem` names the first that does not.
bool modelPoseWithinJointLimits(const ModelSkeleton &skeleton, const QVector<ModelJointMatrix> &pose, double tolerance = 1e-3,
								QString *problem = nullptr);
bool validModelJointLimits(const ModelJointLimits &limits, QString *error = nullptr);
[[nodiscard]] QJsonValue modelJointLimitsJson(const ModelJointLimits &limits);
bool modelJointLimitsFromJson(const QJsonValue &value, ModelJointLimits *limits);
} // namespace vibestudio
