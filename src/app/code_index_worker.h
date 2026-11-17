#pragma once

#include "core/advanced_studio.h"

#include <QObject>
#include <memory>
#include <optional>

class QThread;
class QTimer;

namespace slopstudio {

// One background scanner per shell, with a coalesced latest request. Reset
// invalidates results immediately; a retired scan never publishes into a new
// project. Callbacks and snapshot access belong to the owning GUI thread.
class CodeIndexWorker final : public QObject {
public:
	explicit CodeIndexWorker(QObject* parent = nullptr);
	~CodeIndexWorker() override;
	void start(CodeWorkspaceIndexRequest request);
	void cancel();
	void reset();
	bool busy() const { return m_thread || m_pending.has_value(); }
	QString requestedRoot() const { return m_root; }
	// A background scan is one the studio starts on its own (after an edit, or
	// when completion asks for names) rather than at the user's word. The owner
	// marks the next start() as one and reads background() in its callbacks to
	// show the scan quietly. backgroundRunning hears true when such a scan
	// starts and false however it ends (finished, cancelled, replaced, reset,
	// made ordinary, or the worker destroyed), so an indicator tied to it is
	// never left on.
	void setNextStartBackground(bool background) { m_nextStartBackground = background; }
	bool background() const { return m_background; }
	// Makes the scan in hand an ordinary one: its owner now shows it itself.
	void endBackground();
	std::function<void()> started;
	std::function<void(int files, int symbols)> progress;
	std::function<void(const CodeWorkspaceIndex&)> completed;
	std::function<void(bool running)> backgroundRunning;

private:
	struct Work;
	void startPending();
	void setBackgroundRunning(bool running);
	bool m_nextStartBackground = false;
	bool m_background = false;
	bool m_backgroundRunning = false;
	QThread* m_thread = nullptr;
	QTimer* m_progressTimer = nullptr;
	std::shared_ptr<Work> m_work;
	std::optional<CodeWorkspaceIndexRequest> m_pending;
	QString m_root;
	quint64 m_serial = 0;
};

} // namespace slopstudio
