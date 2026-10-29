#pragma once

// The Mesh Editor's import review (FMT-02): when only decode omissions keep a
// native model from importing for editing, the user sees every decode issue
// by severity and chooses to accept the omissions or cancel.

#include "core/model_mesh.h"

#include <QString>
#include <QVector>

class QWidget;

namespace vibestudio
{

// The suffixes the Mesh Editor can open or import, as "*.mesh.json *.obj ..."
// patterns: the editable sources and every model format that decodes
// geometry (core/model_mesh.h modelFormatCapabilities), without the
// animation-only formats.
QString modelEditorOpenPatterns();

// Shows the issues of `fileName`, grouped by severity, in a modal dialog.
// True when the user accepts the omissions; false when they cancel.
bool reviewModelImportIssues(QWidget *parent, const QString &fileName, const QVector<ModelDecodeIssue> &issues);

} // namespace vibestudio
