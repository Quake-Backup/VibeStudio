#include "core/model_animation_authoring.h"
#include "core/model_document.h"

#include <QCoreApplication>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelAnimationAuthoring)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
} // namespace

bool isModelAnimationAuthoringEdit(ModelEditKind kind)
{
	return kind == ModelEditKind::PastePose;
}

bool validModelAnimationAuthoringOptions(const ModelEdit &edit, QString *error)
{
	const auto &o = edit.animation;
	if (edit.kind != ModelEditKind::PastePose && !(o.paste == ModelPosePasteOptions{}))
		return fail(error, Text::tr("Pose paste settings apply only to Paste Pose."));
	return true;
}

bool applyModelAnimationAuthoringEdit(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	switch (edit.kind)
	{
	case ModelEditKind::PastePose:
		return applyModelPosePaste(candidate, edit, error, control);
	default:
		return fail(error, Text::tr("This is not an animation authoring operation."));
	}
}
} // namespace vibestudio
