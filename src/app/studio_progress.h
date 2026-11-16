#pragma once

// Visible work: the studio's promise that the user always sees that something
// is happening (docs/UX_DESIGN.md, "Visible Work"). This module holds the
// shared parts that keep that promise:
//
// - reportStartupStage(): the stages the studio passes through while it
//   starts, which the start-up screen (app/studio_splash) shows one by one.
// - WorkTracker: one count of everything busy in the window, fed by every
//   LoadingPane and BusyOverlay and by the shell's own tasks, which the
//   status bar's activity indicator reads.
// - BusyIndicator: a ring that turns while work runs and fills as a
//   measurable task progresses; still under reduced motion.
// - BusyOverlay: a translucent cover for a surface whose content is being
//   prepared, with a title, detail, progress and an optional Cancel.
// - UiResponsivenessMonitor: notices when the interface thread stopped
//   answering, so a pause is recorded instead of passing in silence.

#include "core/operation_state.h"

#include <QColor>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>

class QLabel;
class QPushButton;
class QTimer;

namespace slopstudio {

class ElidedLabel;

// ---- Start-up stages -------------------------------------------------------

// Hears each stage the studio reports while it starts: the stage's label and
// how many stages have been reported so far, this one included. There is one
// listener; an empty function removes it.
using StartupStageListener = std::function<void(const QString& label, int completed)>;
void setStartupStageListener(StartupStageListener listener);
// Names the work the studio is about to do ("Building the Levels page"). The
// shell calls it between the steps of its construction; main() calls it for
// the steps around the shell. Nothing happens without a listener.
void reportStartupStage(const QString& label);
[[nodiscard]] int startupStagesReported();
[[nodiscard]] QString lastStartupStage();
void resetStartupStages();

// ---- Work tracker ------------------------------------------------------------

// Everything busy in the window, counted in one place. A LoadingPane that
// enters a busy state and a BusyOverlay that shows register themselves; the
// shell registers its activity tasks. Callers on the interface thread only.
class WorkTracker final : public QObject {
	Q_OBJECT

public:
	struct Item {
		int token = 0;
		QString label;
		OperationProgress progress;
	};

	// Starts tracking a piece of work and returns its token (never 0).
	int begin(const QString& label, OperationProgress progress = {});
	// Changes the label or progress of tracked work; unknown tokens are ignored.
	void update(int token, const QString& label, OperationProgress progress = {});
	void setProgress(int token, OperationProgress progress);
	// Stops tracking; unknown or already ended tokens are ignored.
	void end(int token);
	[[nodiscard]] bool contains(int token) const;

	[[nodiscard]] int activeCount() const;
	[[nodiscard]] QStringList activeLabels() const;
	[[nodiscard]] QVector<Item> items() const;
	// The progress of all tracked work with a known total, added together;
	// a zero total when nothing measurable is running.
	[[nodiscard]] OperationProgress progress() const;
	// One line for the status bar: the label of a single piece of work, or
	// how many are running.
	[[nodiscard]] QString summaryText() const;

	void clear();

signals:
	void changed();

private:
	QVector<Item> m_items;
	int m_nextToken = 1;
};

// The window's tracker. Tests that want a clean count call clear().
[[nodiscard]] WorkTracker& studioWorkTracker();

// ---- Busy indicator ---------------------------------------------------------

class BusyIndicator final : public QWidget {
	Q_OBJECT

public:
	explicit BusyIndicator(QWidget* parent = nullptr);

	// Turns the ring (unless reduced motion or a known total). An inactive
	// indicator paints nothing and takes no room in a layout that asks it.
	void setActive(bool active);
	[[nodiscard]] bool isActive() const;
	// A known total fills the ring clockwise from the top instead of turning
	// it, with the percentage in the middle when the ring is large enough.
	void setProgress(OperationProgress progress);
	[[nodiscard]] OperationProgress progress() const;
	void setReducedMotion(bool reducedMotion);
	[[nodiscard]] bool reducedMotion() const;
	// Logical pixels across; the default follows the font.
	void setDiameter(int diameter);
	[[nodiscard]] int diameter() const;
	// Whether the ring is turning right now: active, shown, indeterminate,
	// and motion allowed.
	[[nodiscard]] bool isTurning() const;
	[[nodiscard]] qreal angle() const;

	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;

	// One frame of the ring, for places that show an icon rather than a
	// widget (the status bar's Activity button). `angle` is where the turning
	// arc starts, in degrees clockwise from the top; a known total in
	// `progress` draws the filled ring instead. `arc` and `track` default to
	// the theme's selection and border colours.
	[[nodiscard]] static QPixmap framePixmap(int diameter, qreal devicePixelRatio, qreal angle, OperationProgress progress = {},
		const QColor& arc = QColor(), const QColor& track = QColor());
	// How far the arc turns between frames, and how often.
	static constexpr qreal kDegreesPerTick = 30.0;
	static constexpr int kTickIntervalMs = 80;

protected:
	void paintEvent(QPaintEvent* event) override;
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;
	void timerEvent(QTimerEvent* event) override;
	void changeEvent(QEvent* event) override;

private:
	void refreshTimer();
	void refreshAccessibleText();

	OperationProgress m_progress;
	int m_diameter = 0;
	int m_timerId = 0;
	qreal m_angle = 0;
	bool m_active = true;
	bool m_reducedMotion = false;
};

// ---- Busy overlay -------------------------------------------------------------

// Covers `host` while its content is being prepared, so the surface is never
// blank or half-filled without saying why. The overlay takes the pointer and
// keyboard events that would reach the host, keeps itself the size of the
// host, and registers with the work tracker while it shows. It appears after
// a short delay by default, so brief work never flashes; beginNow() shows and
// paints it at once, for work that will hold the interface thread.
class BusyOverlay final : public QWidget {
	Q_OBJECT

public:
	explicit BusyOverlay(QWidget* host);
	~BusyOverlay() override;

	void begin(const QString& title, const QString& detail = QString());
	void beginNow(const QString& title, const QString& detail = QString());
	void setTitle(const QString& title);
	void setDetail(const QString& detail);
	void setProgress(OperationProgress progress);
	// Shows a Cancel button; pressing it, or Esc, emits cancelRequested().
	void setCancellable(bool cancellable);
	void setReducedMotion(bool reducedMotion);
	// How long begin() waits before the overlay shows (default 250 ms).
	void setAppearanceDelay(int milliseconds);
	void finish();

	[[nodiscard]] bool isActive() const;
	[[nodiscard]] QString title() const;
	[[nodiscard]] QString detail() const;
	[[nodiscard]] OperationProgress progress() const;
	[[nodiscard]] bool isCancellable() const;
	[[nodiscard]] int appearanceDelay() const;
	[[nodiscard]] BusyIndicator* indicator() const;

signals:
	void cancelRequested();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;
	void changeEvent(QEvent* event) override;

private:
	void present();
	void fitHost();
	void refreshText();
	void refreshTracker();

	QWidget* m_host = nullptr;
	QWidget* m_card = nullptr;
	BusyIndicator* m_indicator = nullptr;
	QLabel* m_titleLabel = nullptr;
	QLabel* m_detailLabel = nullptr;
	QLabel* m_progressLabel = nullptr;
	QPushButton* m_cancelButton = nullptr;
	QTimer* m_appearanceTimer = nullptr;
	QString m_title;
	QString m_detail;
	OperationProgress m_progress;
	int m_appearanceDelay = 250;
	int m_trackerToken = 0;
	bool m_active = false;
	bool m_cancellable = false;
};

// ---- Responsiveness monitor -------------------------------------------------

// Watches the interface thread from inside it: a timer that should fire
// every `interval` measures, when it finally runs, how long it was kept
// waiting. A gap longer than the threshold is a stall the user saw as a
// frozen window, reported once it is over through stallDetected(). It cannot
// report while the stall lasts (nothing can run then); it makes sure the
// pause is recorded afterwards rather than passing in silence.
class UiResponsivenessMonitor final : public QObject {
	Q_OBJECT

public:
	explicit UiResponsivenessMonitor(QObject* parent = nullptr);

	// The shortest gap that counts as a stall (default 500 ms).
	void setStallThreshold(int milliseconds);
	[[nodiscard]] int stallThreshold() const;
	// How often the heartbeat runs (default 100 ms).
	void setInterval(int milliseconds);
	[[nodiscard]] int interval() const;

	void start();
	void stop();
	[[nodiscard]] bool isRunning() const;

	[[nodiscard]] int stallCount() const;
	[[nodiscard]] qint64 longestStallMs() const;
	[[nodiscard]] qint64 totalStallMs() const;
	// One line for diagnostics: "No pauses", or the count and the longest.
	[[nodiscard]] QString summaryText() const;

signals:
	void stallDetected(qint64 milliseconds);

protected:
	void timerEvent(QTimerEvent* event) override;

private:
	int m_threshold = 500;
	int m_interval = 100;
	int m_timerId = 0;
	qint64 m_lastBeatMs = 0;
	int m_stallCount = 0;
	qint64 m_longestStallMs = 0;
	qint64 m_totalStallMs = 0;
};

// ---- Helpers -------------------------------------------------------------------

// A text direction isolate around `text`, so a number or an untranslated
// word keeps its own order inside a right-to-left line.
[[nodiscard]] QString directionIsolated(const QString& text);

// Paints the interface right now, for the moment before work that will hold
// the interface thread: a shown widget's pending paint is flushed to the
// screen without returning to the event loop. Hidden widgets are left alone.
void paintNow(QWidget* widget);

} // namespace slopstudio
