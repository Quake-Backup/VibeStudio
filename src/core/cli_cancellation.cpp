#include "core/cli_cancellation.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif

namespace vibestudio
{
namespace
{
std::atomic_int g_requests{0};
std::atomic_bool g_installed{false};

#ifdef Q_OS_WIN
BOOL WINAPI consoleHandler(DWORD type)
{
	switch (type)
	{
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
		// The second request does not wait for a checkpoint.
		if (g_requests.fetch_add(1) > 0)
			ExitProcess(cliCancelledExitCode);
		return TRUE;
	default:
		return FALSE;
	}
}
#else
void signalHandler(int)
{
	if (g_requests.fetch_add(1) > 0)
		_exit(cliCancelledExitCode);
}
#endif
} // namespace

bool installCliCancellation()
{
	if (g_installed.exchange(true))
		return true;
#ifdef Q_OS_WIN
	return SetConsoleCtrlHandler(consoleHandler, TRUE) != 0;
#else
	struct sigaction action = {};
	action.sa_handler = signalHandler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	return sigaction(SIGINT, &action, nullptr) == 0 && sigaction(SIGTERM, &action, nullptr) == 0;
#endif
}

bool cliCancellationRequested()
{
	return g_requests.load() > 0;
}

void requestCliCancellation()
{
	g_requests.fetch_add(1);
}

QString cliModelWorkPhaseId(ModelWorkPhase phase)
{
	switch (phase)
	{
	case ModelWorkPhase::Reading:
		return QStringLiteral("reading");
	case ModelWorkPhase::Validating:
		return QStringLiteral("validating");
	case ModelWorkPhase::Editing:
		return QStringLiteral("editing");
	case ModelWorkPhase::Serializing:
		return QStringLiteral("serializing");
	case ModelWorkPhase::Writing:
		return QStringLiteral("writing");
	case ModelWorkPhase::Committing:
		return QStringLiteral("committing");
	}
	return {};
}

ModelWorkControl cliModelWorkControl(bool progress)
{
	ModelWorkControl control;
	control.cancelled = [] { return cliCancellationRequested(); };
	// A test hook: cancel at the Nth progress report, as a console request
	// arriving at that moment would, without depending on timing or a console.
	const qint64 cancelAt = qEnvironmentVariableIntValue("VIBESTUDIO_CLI_CANCEL_AT_PROGRESS");
	if (cancelAt <= 0 && !progress)
		return control;
	struct Counter
	{
		qint64 reports = 0;
		QElapsedTimer clock;
		int phase = -1;
	};
	auto counter = std::make_shared<Counter>();
	counter->clock.start();
	control.progress = [counter, cancelAt, progress](ModelWorkPhase phase, qint64 completed, qint64 total)
	{
		if (cancelAt > 0 && ++counter->reports >= cancelAt)
			requestCliCancellation();
		// A new phase always prints; within one, at most every 250 ms.
		if (!progress || (int(phase) == counter->phase && counter->clock.elapsed() < 250))
			return;
		counter->phase = int(phase);
		counter->clock.restart();
		const QJsonObject line{{QStringLiteral("progress"), QJsonObject{{QStringLiteral("phase"), cliModelWorkPhaseId(phase)},
																		{QStringLiteral("completed"), completed},
																		{QStringLiteral("total"), total}}}};
		const auto bytes = QJsonDocument(line).toJson(QJsonDocument::Compact) + '\n';
		std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stderr);
		std::fflush(stderr);
	};
	return control;
}
} // namespace vibestudio
