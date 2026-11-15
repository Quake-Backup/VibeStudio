#pragma once

// The start-up screen: the first thing the user sees, up within the first
// moments of the process and gone once the studio window has painted. It
// names each stage of start-up as the shell reports it (reportStartupStage in
// app/studio_progress), so a slow start reads as work in progress rather than
// as a window that has not come. It follows the theme, text scale, layout
// direction and reduced-motion preference the user chose, and is left out of
// scripted runs (--self-test, --ui-snapshot, --no-splash) and headless
// platforms.

#include "core/operation_state.h"

#include <QFont>
#include <QPixmap>
#include <QString>
#include <QWidget>

class QVariantAnimation;

namespace vibestudio {

class BusyIndicator;

class StudioSplash final : public QWidget {
	Q_OBJECT

public:
	explicit StudioSplash(QWidget* parent = nullptr);
	~StudioSplash() override;

	// Whether a start-up screen makes sense here: a real window system, not
	// the offscreen or minimal platform.
	[[nodiscard]] static bool platformShowsSplash();

	// How many stages start-up reports in all, for the progress line.
	void setExpectedStages(int count);
	[[nodiscard]] int expectedStages() const;
	// Shows a stage and paints at once, so it is on screen before the work it
	// names begins, even when that work holds the interface thread.
	void setStage(const QString& label, int completed);
	[[nodiscard]] QString stageLabel() const;
	[[nodiscard]] int completedStages() const;
	// 0 to 1 across the expected stages.
	[[nodiscard]] qreal progressFraction() const;
	// Hears every reportStartupStage() from now on.
	void listenForStartupStages();

	void setReducedMotion(bool reducedMotion);
	[[nodiscard]] bool reducedMotion() const;
	// The version and status lines under the wordmark; set from the manifest
	// by default.
	void setVersionText(const QString& text);
	[[nodiscard]] QString versionText() const;

	// Shows the splash centred on the primary screen and paints it.
	void present();
	// Fades out once `window` has been shown on screen, then deletes itself.
	// Until then the splash keeps reporting stages.
	void finish(QWidget* window);
	[[nodiscard]] bool isFinishing() const;
	[[nodiscard]] BusyIndicator* indicator() const;

	static constexpr int kFadeDurationMs = 180;

protected:
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void showEvent(QShowEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void changeEvent(QEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	struct Metrics {
		int padding = 28;
		int mark = 72;
		int gap = 20;
		int track = 4;
	};

	void fitToContent();
	void placeIndicator();
	void beginFade();
	void refreshAccessibleText();
	[[nodiscard]] Metrics metrics() const;
	[[nodiscard]] QRect contentRect() const;
	[[nodiscard]] QFont wordmarkFont() const;

	QPixmap m_mark;
	QString m_wordmarkFamily;
	QString m_versionText;
	QString m_stageLabel;
	int m_expectedStages = 1;
	int m_completedStages = 0;
	int m_shadowMargin = 0;
	bool m_reducedMotion = false;
	bool m_finishing = false;
	bool m_announced = false;
	QWidget* m_window = nullptr;
	QObject* m_watchedWindow = nullptr;
	BusyIndicator* m_indicator = nullptr;
	QVariantAnimation* m_fade = nullptr;
};

} // namespace vibestudio
