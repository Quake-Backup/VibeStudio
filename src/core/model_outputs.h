#pragma once

// Model outputs and the sources they were made from.
//
// An editable source (.mesh.json, .assembly.json) is delivered as game files:
// exports, staged package entries, detail-level sets, bakes and player
// packages. Each committed write leaves a receipt here: the source and every
// other input with their SHA-256s, the settings that made it (the recipe), and
// every output with its SHA-256. Comparing them later says which outputs are
// current, which are stale because an input changed, which were changed by
// someone else, and which are gone; an export or detail-level receipt can be
// re-run with its recorded settings, never over an output someone changed.
//
// Receipts are kept with the project, in the same manifest that holds its
// delivery targets (core/model_delivery.h), under the top-level key
// "modelOutputs" (ProjectManifest keeps unknown keys, so older studios leave it
// alone). Paths inside the project are stored relative to its root:
//   "modelOutputs": [{"kind": "export",
//     "source": {"path": "models/crate.mesh.json", "sha256": "..."},
//     "recipe": {"format": "md3", "frame": 0, "target": "ioquake3"},
//     "outputs": [{"path": "build/models/crate.md3", "sha256": "..."}],
//     "tool": "0.9.0", "deliveredUtc": "2026-10-29T10:00:00Z"}]
// A staged output names its package and entry instead of a path. A newer
// receipt for the same first output replaces the older one; at most
// modelOutputRecordLimit are kept, the oldest dropped first.
//
// Game folders never get receipts. A Quake III player package also embeds its
// own (vibestudio/player-bundle.json); readModelPackageReceipt reads it back.

#include "core/model_document.h"
#include "core/model_work.h"
#include "core/project_manifest.h"

#include <QDateTime>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

#include <functional>

namespace vibestudio
{
class PackageArchiveReader;

inline constexpr int modelOutputRecordLimit = 256;

enum class ModelOutputState
{
	// Every input and output still has its recorded bytes.
	Current,
	// An input (the source, a part, a skin) changed since: refresh the output.
	SourceChanged,
	// An output was changed by someone else; a refresh would not replace it.
	OutputChanged,
	// An output or the source is gone.
	Missing,
	// The receipt cannot be checked: exported from unsaved edits, or an
	// input or package that cannot be read.
	Unknown,
};

struct ModelOutputFile
{
	// A file on disk (absolute in memory), or a package entry.
	QString path;
	QString package;
	QString entry;
	QByteArray sha256;
	[[nodiscard]] QString display() const;
};

struct ModelOutputInput
{
	QString path;
	QByteArray sha256;
	// What the input is to the output: part, skin, icon, sound...
	QString role;
};

struct ModelOutputRecord
{
	// export, stage, lod-set, pose-bake, animation-bake, collision-map,
	// q3-animation, player-package, or another service's own kind.
	QString kind;
	// The editable source (absolute in memory). An empty SHA-256 means the
	// output was made from unsaved edits, so it cannot be judged against the
	// file.
	QString sourcePath;
	QByteArray sourceSha256;
	QVector<ModelOutputInput> inputs;
	// The settings that made the outputs, as the service that made them names
	// them; refresh reads "format", "frame", "clip", "md5FrameRate", "strict",
	// "target", "levels" and "ratio".
	QJsonObject recipe;
	QVector<ModelOutputFile> outputs;
	QString tool;
	QDateTime deliveredUtc;
	// The first output's identity: the key receipts are replaced by.
	[[nodiscard]] QString key() const;
};

struct ModelOutputStatus
{
	ModelOutputState state = ModelOutputState::Unknown;
	// What decided the state, in words: the changed input or output.
	QString detail;
};

// "current", "source-changed", "output-changed", "missing", "unknown".
[[nodiscard]] QString modelOutputStateId(ModelOutputState state);
// Translated, for status chips and the Models list.
[[nodiscard]] QString modelOutputStateText(ModelOutputState state);
// Stale states a refresh or a release should act on: SourceChanged, Missing.
[[nodiscard]] bool modelOutputStateIsStale(ModelOutputState state);

[[nodiscard]] QString modelOutputManifestKey();
[[nodiscard]] QJsonObject modelOutputRecordJson(const ModelOutputRecord &record, const QString &projectRoot);
bool parseModelOutputRecord(const QJsonObject &object, const QString &projectRoot, ModelOutputRecord *record, QString *error = nullptr);
// The project's receipts, oldest first; unreadable ones are left out with a
// note in `warnings`.
[[nodiscard]] QVector<ModelOutputRecord> projectModelOutputs(const ProjectManifest &manifest, QStringList *warnings = nullptr);
void setProjectModelOutputs(ProjectManifest *manifest, const QVector<ModelOutputRecord> &records);
// Adds `record`, replacing one with the same key, and bounds the list.
void addProjectModelOutput(ProjectManifest *manifest, const ModelOutputRecord &record);
// The shared hook every guarded export calls after a committed write: adds the
// receipt to the manifest of `projectRoot` and saves it. A project without a
// manifest gets one only when `createManifest` is set.
bool recordModelOutput(const QString &projectRoot, const ModelOutputRecord &record, QString *error = nullptr, bool createManifest = false);
// Removes the receipts whose key is `key` (an output path or package!entry).
bool forgetModelOutput(const QString &projectRoot, const QString &key, QString *error = nullptr);

// SHA-256 of a file, read in blocks up to modelFileByteLimit; empty when it
// cannot be read.
[[nodiscard]] QByteArray modelOutputFileSha256(const QString &path, const std::function<bool()> &cancelled = {});
// Checks every input and output of `record`. Package entries are read from
// `openPackage` when it is the receipt's package (the GUI's package view,
// staged edits included), otherwise from the package file.
[[nodiscard]] ModelOutputStatus modelOutputStatus(const ModelOutputRecord &record, const PackageArchiveReader *openPackage = nullptr,
												  const std::function<bool()> &cancelled = {});
// The receipts made from `sourcePath`.
[[nodiscard]] QVector<ModelOutputRecord> modelOutputsForSource(const QVector<ModelOutputRecord> &records, const QString &sourcePath);
// The editable source a receipt names for an output file or package entry
// (matched by entry path when `output` is a game path); empty when none.
[[nodiscard]] QString modelOutputSourceFor(const QVector<ModelOutputRecord> &records, const QString &output);

// A receipt for an export of `mesh`'s source written by exportEditableModel.
// `sourceSha256` is the SHA-256 of the saved source, or empty when the export
// came from unsaved edits.
[[nodiscard]] ModelOutputRecord modelExportOutputRecord(const QString &sourcePath, const QByteArray &sourceSha256, const QString &format,
														const ModelExportOptions &options, const ModelOutputFile &output,
														const QByteArray &outputBytes);

struct ModelOutputRefresh
{
	bool written = false;
	bool cancelled = false;
	// The receipt after the refresh (new hashes and time), to record.
	ModelOutputRecord record;
	// Outputs written, or that a dry run would write.
	QStringList outputs;
	QStringList notes;
	QString error;
};
// Re-runs an export or detail-level receipt from its source file with the
// recorded settings. Only file outputs are refreshed (a staged entry is staged
// again from the Mesh Editor). Every output must still have its recorded bytes
// or be missing: one changed by someone else is never replaced. A dry run
// prepares everything and writes nothing. Cancelling leaves every output as it
// was.
[[nodiscard]] ModelOutputRefresh refreshModelOutput(const ModelOutputRecord &record, bool dryRun,
													const ModelWorkControl &control = {});

// The receipt a Quake III player package embeds (vibestudio/player-bundle.json),
// as a record of kind player-package: each packaged file with its SHA-256,
// and the parts' source SHA-256s as inputs named by part id. `status` says
// whether the package's files still match it (OutputChanged when not); with an
// assembly, whether its parts' current sources match the receipt.
bool readModelPackageReceipt(const QString &packagePath, ModelOutputRecord *record, ModelOutputStatus *status, QString *error = nullptr,
							 const QString &assemblyPath = {}, const ModelWorkControl &control = {});

// Text for one receipt and its status, for the CLI and the details views.
[[nodiscard]] QJsonObject modelOutputStatusJson(const ModelOutputRecord &record, const ModelOutputStatus &status, const QString &projectRoot);
[[nodiscard]] QString modelOutputStatusLine(const ModelOutputRecord &record, const ModelOutputStatus &status);

} // namespace vibestudio
