#pragma once

// Which programs a build may start without asking.
//
// A project folder, or a saved compiler run beside a map, can come from
// someone else. Re-run Last Compile replays the program a saved run names, and
// a project's compiler paths and build pipelines can point a stage at a
// program inside the project. Such a program starts only when it is one of the
// compilers VibeStudio knows from the user's own configuration (Toolchain
// paths, `compiler set-path`, search paths typed on the command line, the
// studio's compiler folders and PATH) for the right game, or when the user has
// trusted it for that project before. Otherwise the command line is shown and
// the user decides; the default is not to run it. Trust is kept per user in
// StudioSettings, never in the project, and it names the program's SHA-256, so
// a replaced program is asked about again.

#include "core/compiler_registry.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct BuildPipelineResult;
struct CompilerCommandManifest;
struct CompilerCommandPlan;

// A compiler executable VibeStudio resolves from the user's configuration.
struct KnownCompilerProgram {
	QString toolId;
	QString engineFamily;
	QString path;
	// Named by the user (a Toolchain path, `compiler set-path`, or a search
	// path typed on the command line) rather than found by discovery. A
	// discovered program inside the folder a build trusts by scope (the
	// project or the map's folder) does not count as known.
	bool configured = false;
};

// One program a build is about to start.
struct BuildProgramUse {
	QString program;
	QString commandLine;
	QString workingDirectory;
	// The stage or profile it runs for, as shown to the user.
	QString label;
	// The engine family of its compiler profile; empty when unknown, which
	// accepts a known compiler of any family.
	QString engineFamily;
};

enum class BuildProgramTrust {
	// A compiler from the user's configuration or discovery, for that game.
	Known,
	// Trusted by the user for this project (or map folder) before.
	Trusted,
	// Ask before running it.
	Untrusted,
	// No such file: nothing will start, so there is nothing to ask.
	Missing,
};

struct BuildProgramTrustRecord {
	// The project root, or the map's folder without a project; cleaned.
	QString scope;
	// The program's cleaned absolute path.
	QString program;
	// SHA-256 of the program when it was trusted, in hex.
	QString sha256;
	QDateTime trustedUtc;
};

struct BuildProgramReview {
	QString scope;
	QVector<BuildProgramUse> uses;
	QVector<BuildProgramTrust> trust;

	[[nodiscard]] bool needsConfirmation() const;
	// Each untrusted program once, in the order the build would start them.
	[[nodiscard]] QStringList untrustedPrograms() const;
};

// The user's own compiler configuration, without anything from a project:
// `userOverrides` from StudioSettings, `userSearchPaths` typed by the user,
// the studio's own compiler folders (from the current folder) and PATH.
CompilerRegistryOptions userCompilerRegistryOptions(const QVector<CompilerToolPathOverride>& userOverrides, const QStringList& userSearchPaths = {});
QVector<KnownCompilerProgram> knownCompilerPrograms(const CompilerRegistryOptions& userOptions);

// The cleaned absolute folder a trust record belongs to.
QString buildProgramTrustScope(const QString& folder);
// SHA-256 of a program file in hex; empty when it cannot be read.
QString buildProgramFingerprint(const QString& program);

BuildProgramUse compilerManifestProgramUse(const CompilerCommandManifest& manifest);
BuildProgramUse compilerPlanProgramUse(const CompilerCommandPlan& plan);
// Every stage that would run (not skipped) of a planned pipeline.
QVector<BuildProgramUse> buildPipelineProgramUses(const BuildPipelineResult& plan);

BuildProgramReview reviewBuildPrograms(const QVector<BuildProgramUse>& uses, const QString& scope, const QVector<KnownCompilerProgram>& known,
	const QVector<BuildProgramTrustRecord>& trusted);
// `records` with the review's untrusted programs trusted for its scope: one
// record per program and scope, with the program's current fingerprint.
QVector<BuildProgramTrustRecord> withTrustedPrograms(const BuildProgramReview& review, QVector<BuildProgramTrustRecord> records);

QString buildProgramTrustId(BuildProgramTrust trust);
// The untrusted programs with their command lines and folders, for a
// confirmation or a refusal.
QString buildProgramReviewText(const BuildProgramReview& review);
QJsonObject buildProgramReviewJson(const BuildProgramReview& review);

} // namespace vibestudio
