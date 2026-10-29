#pragma once

// What a mesh recovery copy means now, and how many are kept.
//
// A recovery copy records the source it was taken from and that source's
// SHA-256. After a crash the source may have been saved again, replaced by
// another tool or deleted; restoring is safe to save back over it only when it
// still holds the bytes the copy started from. Copies from repeated crashes
// would also accumulate without limit; the retention policy removes the oldest
// beyond a count and a size, and never a copy whose studio is still running.

#include "core/model_recovery.h"

#include <QStringList>

#include <functional>

namespace vibestudio
{

enum class ModelRecoverySourceState
{
	// The copy was taken from a document never saved to a source.
	Unsaved,
	// The source still holds the bytes the copy started from.
	Unchanged,
	// The source was saved or replaced after the copy started.
	Changed,
	// The source is gone.
	Missing,
	// The source exists but could not be read (or is over the source limit).
	Unreadable,
};

// Stable identifiers for JSON: unsaved, unchanged, changed, missing, unreadable.
[[nodiscard]] QString modelRecoverySourceStateId(ModelRecoverySourceState state);
// Translated words for the recovery chooser's status chip.
[[nodiscard]] QString modelRecoverySourceStateText(ModelRecoverySourceState state);

// Streams the recorded source (at most modelDocumentMaxSourceBytes) and
// compares its SHA-256 with the record's. Run it off the GUI thread.
[[nodiscard]] ModelRecoverySourceState modelRecoverySourceState(const ModelRecoveryRecord &record,
																const std::function<bool()> &cancelled = {});

// SHA-256 of a file, streamed in blocks; empty when it cannot be read, is not
// a regular file, exceeds `limit`, or the work is cancelled.
[[nodiscard]] QByteArray modelRecoveryFileSha256(const QString &path, qint64 limit, const std::function<bool()> &cancelled = {});

struct ModelRecoveryRetention
{
	// Keep at most this many copies, newest first...
	int maxCopies = 32;
	// ...and at most this many bytes of them.
	qint64 maxBytes = 2LL * 1024 * 1024 * 1024;
};

struct ModelRecoveryPruneReport
{
	// Recovery ids removed (or that a dry run would remove), oldest first.
	QStringList removed;
	int kept = 0;
	// Over the policy but kept because the studio that wrote them is running.
	int keptLive = 0;
	// Staged-session sidecars whose copy is gone and whose studio is not
	// running (removed, or that a dry run would remove).
	int removedSessions = 0;
	QString error;
};

// Removes the oldest copies beyond the policy. A copy written by a studio that
// `ownerRunning` says is still running (this one included) is never removed;
// an unreadable header is removed only when it is also beyond the policy.
// `dryRun` only reports.
ModelRecoveryPruneReport pruneModelRecoveries(const QString &directory, const ModelRecoveryRetention &policy,
											  const std::function<bool(qint64)> &ownerRunning,
											  const std::function<bool()> &cancelled = {}, bool dryRun = false);

} // namespace vibestudio
