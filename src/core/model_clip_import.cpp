#include "core/model_clip_import.h"
#include "core/model_document.h"

#include <QCoreApplication>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelClipImport)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
} // namespace

bool modelClipImportFromSource(const ModelMesh &, int, const QString &, ModelClipImportOptions *, QString *error)
{
	return fail(error, Text::tr("Importing clips is not available yet."));
}
bool planModelClipImport(const ModelMesh &, const ModelClipImportOptions &, ModelClipImportPlan *, QString *error)
{
	return fail(error, Text::tr("Importing clips is not available yet."));
}
bool applyModelClipImport(ModelMesh *, const ModelEdit &, QString *error, const ModelWorkControl &)
{
	return fail(error, Text::tr("Importing clips is not available yet."));
}
} // namespace vibestudio
