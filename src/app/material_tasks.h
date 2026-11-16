#pragma once

// Background work for the Materials page: library scans, image decoding and
// preview frames run off the GUI thread, newest request first.

#include <QObject>

#include <atomic>
#include <functional>
#include <memory>

class QThread;

namespace slopstudio {

// Runs one task at a time off the GUI thread and keeps one queued task. A
// task returns the function that publishes its result; that function runs on
// the GUI thread unless the task was superseded or cancelled meanwhile, so a
// stale result never shows. Each lane keeps one worker thread for its whole
// life, so a playing preview's frames do not start a thread each.
class MaterialTaskLane final : public QObject {
public:
	using Cancelled = std::function<bool()>;
	using Task = std::function<std::function<void()>(const Cancelled& cancelled)>;

	explicit MaterialTaskLane(QObject* parent = nullptr);
	~MaterialTaskLane() override;

	// `supersede` stops the running task and drops its result: right when the
	// inputs changed. Without it the running task finishes and publishes and
	// only the queued task is replaced: right for animation frames, which
	// should keep arriving while newer ones are asked for.
	void submit(Task task, bool supersede = true);
	void cancel();
	[[nodiscard]] bool busy() const;
	// True while a task runs or waits.
	[[nodiscard]] bool pending() const;

	// The stop flag of the task running on the calling thread, or null off a
	// lane's worker: set when the task is superseded or cancelled, for work
	// that takes an atomic rather than the Cancelled function.
	[[nodiscard]] static const std::atomic_bool* currentStopFlag();

private:
	struct Worker;

	void startNext();
	void finished(const std::shared_ptr<std::atomic_bool>& stop, const std::shared_ptr<std::function<void()>>& publish);

	Task m_queued;
	bool m_hasQueued = false;
	bool m_running = false;
	std::shared_ptr<std::atomic_bool> m_stop;
	std::shared_ptr<Worker> m_worker;
	QThread* m_thread = nullptr;
};

} // namespace slopstudio
