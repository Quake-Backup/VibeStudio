#pragma once

// Cancelling model commands from the console.
//
// Ctrl+C or Ctrl+Break (SetConsoleCtrlHandler on Windows) and SIGINT or SIGTERM
// (sigaction elsewhere) set one atomic flag instead of killing the process, so
// a long save, export or bake stops at its next checkpoint, removes its own
// staging files and publishes nothing. The command then exits with
// cliCancelledExitCode and, with --json, {"cancelled": true}. A second request
// ends the process at once, for a command that is not checking.
//
// --progress makes model commands print one JSON object per phase change and
// at most every 250 ms on stderr:
//   {"progress": {"phase": "serializing", "completed": 1048576, "total": 4194304}}

#include "core/model_work.h"

namespace vibestudio
{
// 128 + SIGINT, the code shells report for an interrupted command.
inline constexpr int cliCancelledExitCode = 130;

// Installs the handlers once for this process; later calls do nothing. False
// when the platform offers no handler (the default termination then applies).
bool installCliCancellation();
[[nodiscard]] bool cliCancellationRequested();
// Sets the flag as the handlers do (tests, and a future GUI-hosted CLI).
void requestCliCancellation();
// A ModelWorkControl that observes the flag and, with `progress`, reports
// phases as JSON lines on stderr.
[[nodiscard]] ModelWorkControl cliModelWorkControl(bool progress = false);
// "reading", "validating", "editing", "serializing", "writing", "committing".
[[nodiscard]] QString cliModelWorkPhaseId(ModelWorkPhase phase);
// For every model command: installs the handlers and makes the token (and,
// with `progress`, the phase reporter) the process default for model work
// started without a control (core/model_work_defaults.h). Later
// cliModelWorkControl calls report progress too when `progress` is set.
void installCliModelWorkDefaults(bool progress);
} // namespace vibestudio
