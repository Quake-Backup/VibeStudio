#pragma once

#include "core/compiler_known_issues.h"
#include "core/compiler_profiles.h"

#include <functional>

namespace slopstudio {

struct CompilerRunRequest {
	CompilerCommandRequest command;
	QString manifestPath;
	bool dryRun = false;
	bool registerOutputs = false;
	// How long the process may run before it is stopped. Zero or less means no
	// limit: the run ends when the tool exits or the caller cancels it.
	int timeoutMs = 5 * 60 * 1000;
};

struct CompilerManifestRerunRequest {
	QString manifestPath;
	bool dryRun = false;
	bool registerOutputs = true;
	// As CompilerRunRequest::timeoutMs: zero or less means no limit.
	int timeoutMs = 5 * 60 * 1000;
};

struct CompilerRunCallbacks {
	std::function<bool()> cancellationRequested;
	std::function<void(const CompilerTaskLogEntry&)> logEntry;
};

struct CompilerRunResult {
	CompilerCommandPlan plan;
	CompilerCommandManifest manifest;
	OperationState state = OperationState::Idle;
	bool started = false;
	bool timedOut = false;
	bool cancelled = false;
	int exitCode = -1;
	qint64 durationMs = -1;
	QString stdoutText;
	QString stderrText;
	QVector<CompilerDiagnostic> diagnostics;
	QStringList registeredOutputPaths;
	QString manifestPath;
	QString error;
	// True when a qbsp-family run leaked: a leak point file appeared, or the tool reported a reached occupant.
	bool leakDetected = false;
	QString leakOccupantClassname;
	QString leakPointText;
	QString leakPointFilePath;
};

CompilerRunResult runCompilerCommand(const CompilerRunRequest& request, const CompilerRunCallbacks& callbacks = {});
CompilerRunResult rerunCompilerCommandManifest(const CompilerCommandManifest& manifest, const CompilerRunCallbacks& callbacks = {}, const QString& manifestPath = QString());
CompilerRunResult rerunCompilerCommandManifest(const CompilerCommandManifest& manifest, const CompilerManifestRerunRequest& request, const CompilerRunCallbacks& callbacks = {});
QString compilerRunResultText(const CompilerRunResult& result);

// The diagnostics a run reads from one channel of a tool's output, parsed line
// by line as a live run parses them: carriage-return progress, colour escapes,
// fatal-error banners and source locations.
QVector<CompilerDiagnostic> compilerOutputDiagnostics(const QString& output, const QString& channel = QStringLiteral("stdout"));
// The upstream issues a run notes beside its diagnostics. The catalogue is
// matched against the diagnostic lines only: the rest of a healthy run's
// output (lump tables, face counts, palette notices) uses its keywords too.
QVector<CompilerKnownIssueMatch> compilerKnownIssuesForDiagnostics(const QVector<CompilerDiagnostic>& diagnostics, const QString& toolId = QString(),
	const QString& profileId = QString());

// Saved compiler runs. Run Profile in Levels and on the Build page, and
// `compiler run`, keep each run's manifest in `<root>/.slopstudio/compiler-runs`,
// where root is the open project or, without one, the map's folder.
QString compilerRunManifestDirectory(const QString& rootPath);
// A new, time-stamped manifest path in that folder for one profile's run.
QString newCompilerRunManifestPath(const QString& rootPath, const QString& profileId);

struct CompilerRunManifestMatch {
	QString path;
	CompilerCommandManifest manifest;
};

// How many of a folder's newest saved runs are read when looking for a map's
// newest one; Re-run Last Compile and its enablement use the same number.
constexpr int kCompilerRunManifestScanLimit = 128;

// The newest saved run of `inputPath` (any input when it is empty) in the
// compiler-runs folders of `rootPaths`, newest file first; at most
// `scanLimit` files are read per folder. False when there is none.
bool findNewestCompilerRunManifest(const QStringList& rootPaths, const QString& inputPath, CompilerRunManifestMatch* out = nullptr,
	int scanLimit = kCompilerRunManifestScanLimit);

} // namespace slopstudio
