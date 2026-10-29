#include "core/model_animation_constraints.h"
#include "core/model_document.h"

#include <QCoreApplication>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelAnimationConstraints)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
} // namespace

QString modelClipConstraintKindId(ModelClipConstraintKind kind)
{
	switch (kind)
	{
	case ModelClipConstraintKind::Ik:
		return QStringLiteral("ik");
	case ModelClipConstraintKind::Aim:
		return QStringLiteral("aim");
	case ModelClipConstraintKind::ChildOf:
		return QStringLiteral("child-of");
	case ModelClipConstraintKind::CopyTransform:
		return QStringLiteral("copy-transform");
	case ModelClipConstraintKind::FootLock:
		return QStringLiteral("foot-lock");
	}
	return {};
}
int modelClipConstraintKindFromId(const QString &id)
{
	for (int kind = 0; kind <= int(ModelClipConstraintKind::FootLock); ++kind)
		if (modelClipConstraintKindId(ModelClipConstraintKind(kind)) == id)
			return kind;
	return -1;
}
bool modelClipHasStack(const ModelSkeletalClip &clip)
{
	return !clip.constraints.isEmpty() || !clip.layers.isEmpty() || !clip.base.isEmpty();
}
QVector<bool> modelClipConstraintAffectedJoints(const ModelSkeleton &skeleton, const ModelClipConstraint &)
{
	return QVector<bool>(skeleton.joints.size(), false);
}
bool validModelClipStack(const ModelSkeleton &, const ModelSkeletalClip &clip, QString *error)
{
	return !modelClipHasStack(clip) || fail(error, Text::tr("Clip constraints and layers are not available yet."));
}
bool evaluateModelClipStack(const ModelSkeleton &, const ModelSkeletalClip &, const QVector<QVector<ModelJointMatrix>> &base,
							QVector<QVector<ModelJointMatrix>> *frames, QString *, const ModelWorkControl &)
{
	*frames = base;
	return true;
}
bool validateModelClipStacks(const ModelMesh &mesh, QString *error, const ModelWorkControl &)
{
	for (const auto &clip : mesh.skeleton.clips)
		if (!validModelClipStack(mesh.skeleton, clip, error))
			return false;
	return true;
}
bool applyModelClipStack(ModelMesh *, const ModelEdit &, QString *error, const ModelWorkControl &)
{
	return fail(error, Text::tr("Clip constraints and layers are not available yet."));
}
bool rebaseModelClipStack(const ModelSkeleton &, ModelSkeletalClip *clip, const QVector<QVector<ModelJointMatrix>> &samples, QString *,
						  const ModelWorkControl &)
{
	clip->frames = samples;
	return true;
}
void writeModelClipStackJson(const ModelSkeletalClip &, bool, QJsonObject *)
{
}
bool readModelClipStackJson(const QJsonObject &entry, int, bool, ModelSkeletalClip *)
{
	return !entry.contains(QStringLiteral("constraints")) && !entry.contains(QStringLiteral("layers")) && !entry.contains(QStringLiteral("base"));
}
} // namespace vibestudio
