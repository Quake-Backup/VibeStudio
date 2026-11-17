#pragma once

#include <QPointer>
#include <QWidget>

namespace slopstudio {

// Keeps keyboard focus across background work that disables part of a window.
//
// Qt moves the focus on when the focused control is disabled and does not bring
// it back when the control is enabled again, so after a job the keyboard user
// is left on whichever control happened to be next: Space no longer reaches the
// waveform, Ctrl+Z no longer reaches the canvas. An editor that disables its
// controls while a worker runs calls begin() before it disables anything,
// ended() as each job finishes (before it enables or hides anything) and
// restore() once its controls are enabled again. A chain of jobs (prepare, then
// publish) holds the focus of its first job until the chain ends.
//
// The work is not modal, so the user may meanwhile have gone to a control that
// stayed enabled. That choice wins: focus is given back only while it still
// rests where disabling left it, or on a control that exists only for the job.
class WorkFocus final {
public:
	explicit WorkFocus(QWidget* window) : m_window(window) {}

	// A job starts. Call before disabling anything.
	void begin()
	{
		// Between ended() and restore() a completion callback is running: a job
		// started there continues the chain and keeps the first job's focus.
		if (m_phase != Phase::Ending) {
			m_focus = m_window->focusWidget();
			m_moved = false;
		}
		m_phase = Phase::Working;
		settleLater();
	}

	// A job finished. Call before enabling or hiding anything. `transient` is a
	// control that exists only while a job runs (Cancel): focus left there is
	// given back like focus the user never moved.
	void ended(const QWidget* transient = nullptr)
	{
		if (m_phase == Phase::Working) {
			noteMoved(transient);
			m_phase = Phase::Ending;
		}
	}

	// The chain has ended and the controls are enabled again. `mayWait` keeps the
	// focus held when its control is still disabled for a moment the caller will
	// see the end of (playback loading); call restore() again at that end.
	void restore(bool mayWait = false)
	{
		if (m_phase != Phase::Ending && m_phase != Phase::Waiting) {
			return;
		}
		if (m_phase == Phase::Waiting) {
			noteMoved(nullptr);
		}
		const QPointer<QWidget> focus = m_focus;
		if (!focus || m_moved || !focus->isVisibleTo(m_window)) {
			clear();
			return;
		}
		if (!focus->isEnabled()) {
			if (!mayWait) {
				clear();
			} else if (m_phase != Phase::Waiting) {
				m_phase = Phase::Waiting;
				settleLater();
			}
			return;
		}
		clear();
		if (focus != m_window->focusWidget()) {
			focus->setFocus(Qt::OtherFocusReason);
		}
	}

	// True while restore(true) is holding the focus for a disabled control.
	[[nodiscard]] bool waiting() const { return m_phase == Phase::Waiting; }

private:
	enum class Phase { Idle, Working, Ending, Waiting };

	// Controls are disabled in the caller's own code, some of it after the job
	// has started, and each one disabled can move the focus again. Where it came
	// to rest is known once that code has returned to the event loop, which is
	// also before any input from the user can arrive.
	void settleLater()
	{
		m_settled = false;
		const quint64 serial = ++m_serial;
		QMetaObject::invokeMethod(m_window, [this, serial]() {
			if (serial == m_serial) {
				m_parked = m_window->focusWidget();
				m_settled = true;
			}
		}, Qt::QueuedConnection);
	}

	void noteMoved(const QWidget* transient)
	{
		const QWidget* now = m_window->focusWidget();
		if (m_settled && now && now != m_parked && now != m_focus && now != transient) {
			m_moved = true;
		}
	}

	void clear()
	{
		m_phase = Phase::Idle;
		m_focus = nullptr;
		m_parked = nullptr;
		m_moved = false;
		m_settled = false;
		++m_serial;
	}

	QWidget* m_window = nullptr;
	QPointer<QWidget> m_focus;  // the control to give the focus back to
	QPointer<QWidget> m_parked; // where disabling left the focus
	Phase m_phase = Phase::Idle;
	bool m_moved = false;
	bool m_settled = false;
	quint64 m_serial = 0;
};

} // namespace slopstudio
