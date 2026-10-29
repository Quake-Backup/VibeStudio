#pragma once

// Bulk model audit (FMT-11): decode every model in a package, a folder or a
// game installation and record, per entry, whether it decodes, how long it
// takes, its decode issues by severity (core/model_decode_issues.h), whether
// it can be imported for editing, and optionally whether the same format
// written back from it decodes to the same geometry within the format's
// quantisation. A summary per format and a stable JSON make runs comparable.
//
// The audit only reads: packages and folders are opened read-only, nothing
// is written into the source, and written-back bytes stay in memory.

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace vibestudio {

struct ModelAuditOptions {
	// Format ids (model formats) to audit; empty audits every model format.
	QStringList formats;
	// Write each editable model back in its own format (MDL, MD2, MD3, MD5,
	// IQM, ASE, OBJ) and compare the decoded geometry.
	bool roundTrip = false;
	// Packages found inside a folder are audited too, up to this many.
	int maxPackages = 256;
	// Entries past this many are counted as skipped.
	int maxEntries = 20000;
	// Entries larger than this are skipped without being read.
	qint64 maxEntryBytes = 64LL * 1024LL * 1024LL;
	// Called with the entries done and the total known so far; the label is
	// the entry being read. On the worker thread.
	std::function<void(int done, int total, const QString& label)> progress;
};

struct ModelAuditRoundTrip {
	bool attempted = false;
	// The export id written (the model's own format).
	QString format;
	bool written = false;
	bool decoded = false;
	QString error;
	qint64 bytes = 0;
	int triangleDelta = 0;
	int frameDelta = 0;
	// The largest distance between a triangle corner before and after, over
	// every frame, in game units; -1 when the counts differ.
	double maxPositionError = -1;
	// What the format can hold exactly: half a quantisation step per axis.
	double tolerance = 0;
	[[nodiscard]] bool withinTolerance() const { return decoded && triangleDelta == 0 && frameDelta == 0 && maxPositionError >= 0 && maxPositionError <= tolerance; }
};

struct ModelAuditEntry {
	// The package (or folder) and the entry's path inside it.
	QString container;
	QString path;
	QString format;
	qint64 sizeBytes = 0;
	bool skipped = false;
	QString skipReason;
	bool decoded = false;
	QString error;
	double decodeMilliseconds = 0;
	int surfaces = 0;
	int frames = 0;
	int vertices = 0;
	int triangles = 0;
	int joints = 0;
	int clips = 0;
	QVector<ModelDecodeIssue> issues;
	// clean, advisories, accept-omissions, damaged, or failed.
	QString verdict;
	// Whether importEditableModel would accept it, with omissions accepted
	// when that is all that stands in the way, and why not.
	bool editable = false;
	bool needsAcceptance = false;
	QString editableError;
	ModelAuditRoundTrip roundTrip;
};

struct ModelAuditFormatSummary {
	QString format;
	int entries = 0;
	int skipped = 0;
	int decoded = 0;
	int failed = 0;
	int clean = 0;
	int advisories = 0;
	int needsAcceptance = 0;
	int damaged = 0;
	int editable = 0;
	int roundTrips = 0;
	int roundTripsWithin = 0;
	double maxPositionError = 0;
	double decodeMilliseconds = 0;
};

struct ModelAuditReport {
	QString source;
	QStringList containers;
	QVector<ModelAuditEntry> entries;
	QVector<ModelAuditFormatSummary> formats;
	bool cancelled = false;
	int skippedEntries = 0;
	// Containers that could not be opened, with the reason.
	QStringList problems;
};

// Audits the model entries of `path`: a package (PAK, PK3, PK4, WAD, ZIP and
// the other formats PackageArchive opens), a folder of loose files, or a game
// installation folder (its loose files and every package inside it). False
// with an error when nothing could be opened; a cancelled audit returns true
// with report->cancelled set and the entries done so far.
bool auditModels(const QString& path, const ModelAuditOptions& options, ModelAuditReport* report, QString* error = nullptr,
	const ModelWorkControl& control = {});

// The model formats whose suffixes the audit looks for.
[[nodiscard]] QStringList modelAuditSuffixes();

// Stable JSON: entries sorted by container then path, formats by id, numbers
// in the C locale, timings left out unless `timings` is set so two runs over
// the same files compare equal.
[[nodiscard]] QJsonObject modelAuditJson(const ModelAuditReport& report, bool timings = false);
// One line per format, then the entries that are not clean, translated.
[[nodiscard]] QStringList modelAuditLines(const ModelAuditReport& report);

} // namespace vibestudio
