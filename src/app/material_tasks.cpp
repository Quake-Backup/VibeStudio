#include "app/material_tasks.h"

#include <QMetaObject>
#include <QThread>

#include <condition_variable>
#include <mutex>

namespace vibestudio {

namespace {

// The stop flag of the task this worker thread is running.
thread_local const std::atomic_bool* t_currentStop = nullptr;

} // namespace

// What the GUI thread hands the lane's worker thread: one task at a time.
struct MaterialTaskLane::Worker {
	std::mutex mutex;
	std::condition_variable wake;
	Task task;
	std::shared_ptr<std::atomic_bool> stop;
	bool hasTask = false;
	bool quit = false;
};

MaterialTaskLane::MaterialTaskLane(QObject* parent)
	: QObject(parent)
{
}

MaterialTaskLane::~MaterialTaskLane()
{
	m_hasQueued = false;
	m_queued = {};
	if (m_stop) {
		m_stop->store(true);
	}
	if (m_thread) {
		{
			const std::lock_guard<std::mutex> lock(m_worker->mutex);
			m_worker->quit = true;
			m_worker->hasTask = false;
			m_worker->task = {};
		}
		m_worker->wake.notify_all();
		// The running task sees its stop flag and returns; its result, posted
		// to this lane, goes with it.
		m_thread->wait();
		delete m_thread;
		m_thread = nullptr;
	}
}

void MaterialTaskLane::submit(Task task, bool supersede)
{
	m_queued = std::move(task);
	m_hasQueued = true;
	if (supersede && m_stop) {
		m_stop->store(true);
	}
	startNext();
}

void MaterialTaskLane::cancel()
{
	m_hasQueued = false;
	m_queued = {};
	if (m_stop) {
		m_stop->store(true);
	}
}

bool MaterialTaskLane::busy() const
{
	return m_running;
}

bool MaterialTaskLane::pending() const
{
	return m_running || m_hasQueued;
}

const std::atomic_bool* MaterialTaskLane::currentStopFlag()
{
	return t_currentStop;
}

void MaterialTaskLane::startNext()
{
	if (m_running || !m_hasQueued) {
		return;
	}
	Task task = std::move(m_queued);
	m_queued = {};
	m_hasQueued = false;
	auto stop = std::make_shared<std::atomic_bool>(false);
	m_stop = stop;
	m_running = true;
	if (!m_thread) {
		// One worker for the lane's life, started with its first task.
		m_worker = std::make_shared<Worker>();
		const std::shared_ptr<Worker> worker = m_worker;
		MaterialTaskLane* lane = this;
		m_thread = QThread::create([worker, lane]() {
			for (;;) {
				Task next;
				std::shared_ptr<std::atomic_bool> flag;
				{
					std::unique_lock<std::mutex> lock(worker->mutex);
					worker->wake.wait(lock, [&worker]() { return worker->quit || worker->hasTask; });
					if (worker->quit) {
						return;
					}
					next = std::move(worker->task);
					worker->task = {};
					worker->hasTask = false;
					flag = worker->stop;
				}
				auto publish = std::make_shared<std::function<void()>>();
				t_currentStop = flag.get();
				*publish = next([flag]() { return flag->load(std::memory_order_relaxed); });
				t_currentStop = nullptr;
				next = {};
				// The lane outlives this thread (its destructor waits), and a
				// result posted to a lane being destroyed is dropped with it.
				QMetaObject::invokeMethod(
					lane, [lane, flag, publish]() { lane->finished(flag, publish); }, Qt::QueuedConnection);
			}
		});
		m_thread->setObjectName(QStringLiteral("MaterialTask"));
		m_thread->start(QThread::LowPriority);
	}
	{
		const std::lock_guard<std::mutex> lock(m_worker->mutex);
		m_worker->task = std::move(task);
		m_worker->stop = stop;
		m_worker->hasTask = true;
	}
	m_worker->wake.notify_one();
}

void MaterialTaskLane::finished(const std::shared_ptr<std::atomic_bool>& stop, const std::shared_ptr<std::function<void()>>& publish)
{
	m_running = false;
	if (!stop->load() && *publish) {
		(*publish)();
	}
	startNext();
}

} // namespace vibestudio
