#pragma once

// Classified decode problems (FMT-02).
//
// Decoders report problems as translated warnings on the model and its
// surfaces. Many are advisories about how a game treats the file, or data
// the decoder leaves out on purpose, rather than damage. This module records
// each warning's severity next to it so editable import can refuse only what
// matters:
// - Damage (dropped triangles, a truncated surface chain) is never editable.
// - Omissions (an extra LightWave layer, an animation that does not match
//   the skeleton, a texture file that could not be read) need the user's
//   acceptance: ModelImportOptions::acceptOmissions, `model import
//   --accept-omissions`, or the Mesh Editor's import review.
// - Advisories never block editing.
// A warning no decoder classified counts as damage, so a new decoder message
// stays safe until someone classifies it.
//
// An editable source keeps the advisories and accepted omissions it was
// imported with in ModelMesh::issues (the "importIssues" block of a
// .mesh.json, version modelSourceImportIssuesVersion) and carries no warnings.

#include "core/model_mesh.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

// "advisory", "omission" or "damage".
QString modelDecodeIssueSeverityId(ModelDecodeIssueSeverity severity);
bool modelDecodeIssueSeverityFromId(const QString& id, ModelDecodeIssueSeverity* severity);
// "Advisory", "Omission" or "Damage", translated.
QString modelDecodeIssueSeverityName(ModelDecodeIssueSeverity severity);

// Decoders call these instead of appending to `warnings` directly: the text
// goes into the model's (or the surface's) warnings as before and the issue
// into mesh->issues. A surface issue takes the index the surface will have
// once appended when surface->index is unset.
void noteModelDecodeIssue(ModelMesh* mesh, ModelDecodeIssueSeverity severity, const QString& id, const QString& text);
void noteModelSurfaceIssue(ModelMesh* mesh, ModelSurface* surface, ModelDecodeIssueSeverity severity, const QString& id, const QString& text);
// Changes the severity of the issue noted last with `text`; false when none is.
bool reclassifyModelDecodeIssue(ModelMesh* mesh, const QString& text, ModelDecodeIssueSeverity severity);

// Every issue of `mesh`: its recorded issues, then each warning (model, then
// surfaces in order) that no recorded issue accounts for, as unclassified
// damage. Recorded issues whose text no warning carries (an editable
// source's import record) are kept as they are.
QVector<ModelDecodeIssue> modelDecodeIssues(const ModelMesh& mesh);
// Replaces mesh->issues with modelDecodeIssues(*mesh). decodeModelMesh calls it.
void classifyModelDecodeIssues(ModelMesh* mesh);
// The texts that keep `mesh` from being editable: damage, and omissions not
// accepted. Each line names its severity and, for a surface, the surface.
QStringList modelDecodeBlockingIssues(const ModelMesh& mesh);
// True when something blocks editing and every blocking issue is an omission,
// so accepting omissions would let the import go ahead.
bool modelDecodeNeedsOmissionAcceptance(const ModelMesh& mesh);
// Marks every omission accepted.
void acceptModelDecodeOmissions(ModelMesh* mesh);
// Turns a decoded model's warnings into its import record once nothing
// blocks editing: classifies them, keeps the advisories and accepted
// omissions in mesh->issues, and clears the model's and surfaces' warnings.
// A model with damage or unaccepted omissions, or a source that carries no
// warnings, is left unchanged.
void adoptModelDecodeIssues(ModelMesh* mesh);

struct ModelDecodeIssueCounts {
	int advisories = 0;
	int omissions = 0;
	int damage = 0;
	int accepted = 0;
};
ModelDecodeIssueCounts countModelDecodeIssues(const QVector<ModelDecodeIssue>& issues);

// [{id, severity, surface, text, accepted}] in the C locale.
QJsonArray modelDecodeIssuesJson(const QVector<ModelDecodeIssue>& issues);
// The editable source version that introduced the importIssues block.
inline constexpr int modelSourceImportIssuesVersion = 18;

// The importIssues block of an editable source; false with an error when it
// is malformed (unknown severity, damage, a surface out of range).
bool parseModelDecodeIssues(const QJsonValue& value, int surfaceCount, QVector<ModelDecodeIssue>* issues, QString* error = nullptr);
// "Advisory: text", "Omission (accepted), surface 2: text", one per issue,
// translated; `surfaceNames` names surfaces when given.
QStringList modelDecodeIssueLines(const QVector<ModelDecodeIssue>& issues, const QStringList& surfaceNames = {});
// "2 advisory notes, 1 accepted omission", translated, with numbers in the
// application locale; empty when there are no issues.
QString modelDecodeIssueSummary(const QVector<ModelDecodeIssue>& issues);

} // namespace vibestudio
