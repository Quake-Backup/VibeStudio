// The shell's part of visible work: it reports exactly the start-up stages it
// declares, the status bar's Activity button turns while anything is busy,
// and a held interface thread is recorded. Runs under the offscreen platform.

#include "app/application_shell.h"
#include "app/studio_progress.h"
#include "app/studio_theme.h"
#include "app/ui_primitives.h"
#include "core/studio_settings.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFrame>
#include <QListWidget>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>

#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool condition, const QString& message)
{
	if (!condition) {
		std::cerr << message.toStdString() << '\n';
	}
	return condition;
}

void pump(int milliseconds)
{
	QElapsedTimer timer;
	timer.start();
	while (timer.elapsed() < milliseconds) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QThread::msleep(2);
	}
}

} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
	// Offscreen runs keep pauses to the log unless asked; this one asks.
	qputenv("VIBESTUDIO_RECORD_UI_PAUSES", "1");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QTemporaryDir settingsRoot;
	StudioSettings::setOverrideFilePath(settingsRoot.filePath(QStringLiteral("settings.ini")));
	QApplication app(argc, argv);
	QStandardPaths::setTestModeEnabled(true);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	bool ok = true;

	resetStartupStages();
	QStringList labels;
	QVector<int> counts;
	setStartupStageListener([&](const QString& label, int completed) {
		labels << label;
		counts << completed;
	});
	ApplicationShell shell;
	setStartupStageListener({});
	ok &= expect(labels.size() == ApplicationShell::startupStageCount(),
		QStringLiteral("the shell reports the %1 stages it declares, not %2").arg(ApplicationShell::startupStageCount()).arg(labels.size()));
	ok &= expect(QSet<QString>(labels.cbegin(), labels.cend()).size() == labels.size(), "every stage has its own label");
	for (int index = 0; index < labels.size(); ++index) {
		ok &= expect(!labels.at(index).trimmed().isEmpty(), "a stage label is empty");
		ok &= expect(counts.at(index) == index + 1, "stage counts run in order");
	}
	ok &= expect(labels.first().startsWith(QStringLiteral("Preparing")) && labels.last().startsWith(QStringLiteral("Checking for unsaved")),
		"the stages start with the commands and end with recovery");

	shell.show();
	app.processEvents();
	auto* activityToggle = shell.findChild<QToolButton*>(QString(), Qt::FindChildrenRecursively);
	for (QToolButton* button : shell.findChildren<QToolButton*>()) {
		if (button->text().startsWith(QStringLiteral("Activity"))) {
			activityToggle = button;
			break;
		}
	}
	ok &= expect(activityToggle && activityToggle->text().startsWith(QStringLiteral("Activity")), "the Activity button is in the status bar");
	WorkTracker& tracker = studioWorkTracker();
	const int settled = tracker.activeCount();
	const QString restingToolTip = activityToggle ? activityToggle->toolTip() : QString();
	auto* ring = shell.findChild<BusyIndicator*>(QStringLiteral("statusWorkIndicator"));
	ok &= expect(ring != nullptr, "the status bar has its work ring");
	ok &= expect(ring && !ring->isVisible() == (settled == 0), "the ring shows only while work runs");
	LoadingPane* pane = nullptr;
	for (QFrame* frame : shell.findChildren<QFrame*>(QStringLiteral("loadingPane"))) {
		if (auto* candidate = dynamic_cast<LoadingPane*>(frame)) {
			pane = candidate;
			break;
		}
	}
	ok &= expect(pane != nullptr, "the shell has loading panes");
	if (pane && activityToggle) {
		pane->setTitle(QStringLiteral("Package Scan"));
		pane->setProgress({});
		pane->setState(OperationState::Loading);
		app.processEvents();
		ok &= expect(tracker.activeCount() == settled + 1, "a busy pane in the shell counts as running work");
		ok &= expect(activityToggle->toolTip().contains(QStringLiteral("Running now")) && activityToggle->toolTip().contains(QStringLiteral("Package Scan")),
			QStringLiteral("the Activity button names the running work: ") + activityToggle->toolTip());
		ok &= expect(ring && ring->isVisible() && ring->isTurning(), "the ring beside the Activity button shows and turns while work runs");
		// The ring turns a step every tick; give it a few, and a little slack.
		const qreal angle = ring ? ring->angle() : 0;
		bool turned = false;
		for (int attempt = 0; attempt < 12 && !turned; ++attempt) {
			pump(BusyIndicator::kTickIntervalMs);
			turned = ring && ring->angle() != angle;
		}
		ok &= expect(turned, "the ring keeps turning");
		// A known total fills the ring instead, and it stands still.
		pane->setProgress(OperationProgress{3, 10});
		app.processEvents();
		ok &= expect(tracker.progress().total == 10 && ring && !ring->isTurning() && ring->progress().total == 10, "measurable work fills the ring and keeps it still");
		ok &= expect(activityToggle->toolTip().contains(QStringLiteral("Package Scan")), "the tooltip keeps naming the work while the ring shows");
		pane->setState(OperationState::Completed);
		app.processEvents();
		ok &= expect(tracker.activeCount() == settled, "the finished pane is no longer counted");
		ok &= expect(activityToggle->toolTip() == restingToolTip, "the Activity button's tooltip returns once work ends");
		ok &= expect(ring && ring->isVisible() == (settled > 0), "the ring goes once work ends");
	}

	// A held interface thread is recorded in the Activity Center.
	auto* activityTasks = shell.findChild<QListWidget*>(QStringLiteral("activityTasks"));
	ok &= expect(activityTasks != nullptr, "the Activity Center list exists");
	const auto pauseRecords = [activityTasks]() {
		int count = 0;
		for (int index = 0; activityTasks && index < activityTasks->count(); ++index) {
			if (activityTasks->item(index)->text().contains(QStringLiteral("Studio paused"))) {
				++count;
			}
		}
		return count;
	};
	// A loaded machine may already have held the window long enough to be
	// recorded (the first paint of a debug build, say); what matters is that
	// a pause the test makes itself is recorded once it is over. The record
	// is rate limited to one per half minute, so none is forced beforehand.
	pump(300);
	const int before = pauseRecords();
	ok &= expect(before <= 1, QStringLiteral("at most one pause is recorded before the test holds the window: %1").arg(before));
	if (before == 0) {
		QThread::msleep(2200);
		pump(300);
		ok &= expect(pauseRecords() == 1, "a pause of two seconds is recorded in the Activity Center");
	} else {
		std::cerr << "note: a pause was recorded during start-up on this machine; the deliberate pause is skipped (rate limit)\n";
	}

	if (!ok) {
		std::cerr << "startup progress smoke test failed\n";
		return 1;
	}
	std::cout << "startup progress smoke test passed\n";
	return 0;
}
