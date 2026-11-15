// Visible work: the shared indicators in app/studio_progress and the start-up
// screen in app/studio_splash. Runs under the offscreen platform.

#include "app/studio_progress.h"
#include "app/studio_splash.h"
#include "app/studio_theme.h"
#include "app/ui_primitives.h"
#include "core/studio_settings.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include <algorithm>
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

int countColour(const QImage& image, const QColor& colour, int left = 0, int right = -1)
{
	const QRgb wanted = colour.rgb();
	const int end = right < 0 ? image.width() : right;
	int count = 0;
	for (int y = 0; y < image.height(); ++y) {
		for (int x = left; x < end; ++x) {
			if ((image.pixel(x, y) | 0xff000000) == (wanted | 0xff000000)) {
				++count;
			}
		}
	}
	return count;
}

bool differsFromCorner(const QImage& image)
{
	const QRgb corner = image.pixel(0, 0);
	for (int y = 0; y < image.height(); ++y) {
		for (int x = 0; x < image.width(); ++x) {
			if (image.pixel(x, y) != corner) {
				return true;
			}
		}
	}
	return false;
}

void save(const QImage& image, const QString& name)
{
	const QString root = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
	if (!root.isEmpty()) {
		QDir().mkpath(root);
		image.save(QDir(root).filePath(name + QStringLiteral(".png")));
	}
}

bool checkStartupStages()
{
	bool ok = true;
	resetStartupStages();
	QStringList labels;
	QVector<int> counts;
	setStartupStageListener([&](const QString& label, int completed) {
		labels << label;
		counts << completed;
	});
	reportStartupStage(QStringLiteral("First"));
	reportStartupStage(QStringLiteral("Second"));
	ok &= expect(labels == QStringList{QStringLiteral("First"), QStringLiteral("Second")}, "stage listener hears each label in order");
	ok &= expect(counts == QVector<int>{1, 2}, "stage listener hears the running count");
	ok &= expect(startupStagesReported() == 2 && lastStartupStage() == QStringLiteral("Second"), "stage count and last stage are kept");
	setStartupStageListener({});
	reportStartupStage(QStringLiteral("Third"));
	ok &= expect(labels.size() == 2 && startupStagesReported() == 3, "a removed listener hears nothing, the count still grows");
	resetStartupStages();
	ok &= expect(startupStagesReported() == 0 && lastStartupStage().isEmpty(), "reset clears the stages");
	return ok;
}

bool checkWorkTracker()
{
	bool ok = true;
	WorkTracker& tracker = studioWorkTracker();
	tracker.clear();
	int changes = 0;
	const auto connection = QObject::connect(&tracker, &WorkTracker::changed, [&changes]() { ++changes; });
	ok &= expect(tracker.activeCount() == 0 && tracker.summaryText() == QStringLiteral("Nothing is running."), "an idle tracker says so");
	const int first = tracker.begin(QStringLiteral("Loading package"));
	ok &= expect(first != 0 && tracker.activeCount() == 1 && tracker.summaryText() == QStringLiteral("Loading package"), "one piece of work is named in the summary");
	const int second = tracker.begin(QStringLiteral("Indexing code"), OperationProgress{25, 100});
	ok &= expect(tracker.activeCount() == 2 && tracker.activeLabels() == QStringList{QStringLiteral("Loading package"), QStringLiteral("Indexing code")}, "labels list in order");
	ok &= expect(tracker.summaryText() == QStringLiteral("2 tasks are running."), QStringLiteral("two tasks are counted: ") + tracker.summaryText());
	ok &= expect(tracker.progress().total == 100 && tracker.progress().current == 25, "measurable work alone feeds the aggregate progress");
	tracker.setProgress(first, OperationProgress{10, 20});
	ok &= expect(tracker.progress().total == 120 && tracker.progress().current == 35, "progress adds up across tracked work");
	const int before = changes;
	tracker.update(second, QStringLiteral("Indexing code"), OperationProgress{25, 100});
	ok &= expect(changes == before, "an unchanged update emits nothing");
	tracker.end(first);
	tracker.end(first);
	tracker.end(9999);
	ok &= expect(tracker.activeCount() == 1 && !tracker.contains(first) && tracker.contains(second), "ending work removes it once; unknown tokens are ignored");
	tracker.end(second);
	ok &= expect(tracker.activeCount() == 0 && changes >= 5, QStringLiteral("every change is signalled: %1").arg(changes));
	QObject::disconnect(connection);

	// A loading pane counts while it is busy, and stops counting when it is
	// done or gone.
	{
		LoadingPane pane;
		pane.setTitle(QStringLiteral("Package Scan"));
		ok &= expect(tracker.activeCount() == 0, "an idle pane is not counted");
		pane.setState(OperationState::Loading);
		ok &= expect(tracker.activeCount() == 1 && tracker.activeLabels() == QStringList{QStringLiteral("Package Scan")}, "a loading pane is counted under its title");
		pane.setProgress(OperationProgress{3, 9});
		ok &= expect(tracker.progress().total == 9 && tracker.progress().current == 3, "a pane's progress reaches the tracker");
		pane.setState(OperationState::Running);
		ok &= expect(tracker.activeCount() == 1, "a pane that keeps busy is counted once");
		pane.setState(OperationState::Completed);
		ok &= expect(tracker.activeCount() == 0, "a finished pane is no longer counted");
		pane.setState(OperationState::Queued);
		ok &= expect(tracker.activeCount() == 1, "a queued pane counts again");
	}
	ok &= expect(tracker.activeCount() == 0, "a destroyed busy pane leaves the tracker");
	return ok;
}

bool checkBusyIndicator(QApplication& app)
{
	bool ok = true;
	const StudioThemeTokens& tokens = currentStudioTheme();
	QWidget window;
	window.resize(120, 120);
	BusyIndicator indicator(&window);
	indicator.setDiameter(48);
	indicator.move(36, 36);
	indicator.resize(48, 48);
	window.show();
	app.processEvents();
	ok &= expect(indicator.isActive() && indicator.isTurning(), "a shown indeterminate indicator turns");
	ok &= expect(indicator.sizeHint() == QSize(48, 48), "the size hint is the diameter");
	const QImage frame = indicator.grab().toImage();
	save(frame, QStringLiteral("busy-indicator"));
	ok &= expect(differsFromCorner(frame), "the ring paints");
	ok &= expect(countColour(frame, tokens.colors.selection) > 20, "the arc is drawn in the selection colour");
	const qreal angle = indicator.angle();
	pump(BusyIndicator::kTickIntervalMs * 3);
	ok &= expect(indicator.angle() != angle, "the arc moves on as time passes");
	indicator.setReducedMotion(true);
	ok &= expect(!indicator.isTurning() && indicator.angle() == 0, "reduced motion stops the ring");
	const QImage still = indicator.grab().toImage();
	pump(BusyIndicator::kTickIntervalMs * 3);
	ok &= expect(indicator.grab().toImage() == still, "a still ring paints the same frame over time");
	ok &= expect(differsFromCorner(still), "a still ring is still visible");
	indicator.setReducedMotion(false);
	indicator.setProgress(OperationProgress{50, 100});
	ok &= expect(!indicator.isTurning(), "a known total stops the turning");
	const QImage half = indicator.grab().toImage();
	save(half, QStringLiteral("busy-indicator-50"));
	ok &= expect(differsFromCorner(half) && half != still, "the filled ring paints differently from the turning one");
	ok &= expect(indicator.accessibleDescription().contains(QStringLiteral("50")), "the accessible description carries the percentage");
	indicator.setActive(false);
	ok &= expect(indicator.sizeHint() == QSize(0, 0) && !indicator.isTurning() && indicator.accessibleName().isEmpty(), "an inactive indicator takes no room and says nothing");
	const QPixmap icon = BusyIndicator::framePixmap(14, 2.0, 90.0);
	ok &= expect(icon.width() == 28 && qFuzzyCompare(icon.devicePixelRatio(), 2.0) && differsFromCorner(icon.toImage()), "a frame pixmap paints at the device pixel ratio");
	return ok;
}

bool checkBusyOverlay(QApplication& app)
{
	bool ok = true;
	WorkTracker& tracker = studioWorkTracker();
	tracker.clear();
	QWidget host;
	host.resize(420, 300);
	auto* content = new QPushButton(QStringLiteral("Content"), &host);
	content->setGeometry(20, 20, 120, 30);
	host.show();
	app.processEvents();
	auto* overlay = new BusyOverlay(&host);
	ok &= expect(!overlay->isVisible() && !overlay->isActive(), "an overlay starts hidden");
	overlay->begin(QStringLiteral("Restoring your session"), QStringLiteral("Opening the package that was open."));
	app.processEvents();
	ok &= expect(overlay->isActive() && !overlay->isVisible(), "begin() waits before showing, so brief work never flashes");
	ok &= expect(tracker.activeCount() == 1 && tracker.activeLabels().first() == QStringLiteral("Restoring your session"), "active work counts from begin(), shown or not");
	pump(overlay->appearanceDelay() + 150);
	ok &= expect(overlay->isVisible(), "the overlay shows after its delay");
	ok &= expect(overlay->geometry() == host.rect(), "the overlay covers the host");
	QWidget* hit = host.childAt(QPoint(30, 30));
	ok &= expect(hit && (hit == overlay || overlay->isAncestorOf(hit)), "the overlay takes the pointer where the content was");
	const QImage frame = overlay->grab().toImage();
	save(frame, QStringLiteral("busy-overlay"));
	ok &= expect(differsFromCorner(frame), "the overlay paints its veil and card");
	ok &= expect(overlay->indicator()->isTurning(), "the overlay's ring turns");
	overlay->setProgress(OperationProgress{2, 5});
	ok &= expect(!overlay->indicator()->isTurning() && tracker.progress().total == 5 && tracker.progress().current == 2, "overlay progress fills the ring and the tracker");
	auto* progressLabel = overlay->findChild<QLabel*>(QStringLiteral("busyOverlayProgress"));
	ok &= expect(progressLabel && progressLabel->isVisible() && progressLabel->text().contains(QStringLiteral("2")), "the count of steps is shown");
	int cancels = 0;
	QObject::connect(overlay, &BusyOverlay::cancelRequested, [&cancels]() { ++cancels; });
	QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
	QApplication::sendEvent(overlay, &escape);
	ok &= expect(cancels == 0, "Esc does nothing while the work cannot be cancelled");
	overlay->setCancellable(true);
	app.processEvents();
	auto* cancel = overlay->findChild<QPushButton*>(QStringLiteral("busyOverlayCancel"));
	ok &= expect(cancel && cancel->isVisible(), "a cancellable overlay shows Cancel");
	QApplication::sendEvent(overlay, &escape);
	cancel->click();
	ok &= expect(cancels == 2, "Esc and the button both ask to cancel");
	overlay->setReducedMotion(true);
	overlay->setProgress({});
	ok &= expect(!overlay->indicator()->isTurning(), "reduced motion keeps the overlay's ring still");
	host.resize(500, 360);
	app.processEvents();
	ok &= expect(overlay->geometry() == host.rect(), "the overlay follows the host's size");
	overlay->finish();
	ok &= expect(!overlay->isVisible() && !overlay->isActive() && tracker.activeCount() == 0, "finish() hides the overlay and ends its work");
	overlay->beginNow(QStringLiteral("Opening the map"));
	ok &= expect(overlay->isVisible() && overlay->title() == QStringLiteral("Opening the map") && tracker.activeCount() == 1, "beginNow() shows at once");
	delete overlay;
	ok &= expect(tracker.activeCount() == 0, "a destroyed overlay leaves the tracker");
	return ok;
}

bool checkResponsivenessMonitor()
{
	bool ok = true;
	UiResponsivenessMonitor monitor;
	monitor.setInterval(50);
	monitor.setStallThreshold(200);
	QVector<qint64> stalls;
	QObject::connect(&monitor, &UiResponsivenessMonitor::stallDetected, [&stalls](qint64 ms) { stalls << ms; });
	monitor.start();
	ok &= expect(monitor.isRunning(), "the monitor runs once started");
	pump(300);
	ok &= expect(stalls.isEmpty(), "a responsive loop reports no stall");
	ok &= expect(monitor.summaryText() == QStringLiteral("The interface has not paused this session."), "the summary says no pause");
	QThread::msleep(500);
	pump(200);
	ok &= expect(stalls.size() == 1 && stalls.first() >= 400 && stalls.first() < 2000,
		QStringLiteral("a held thread is reported once with its length: %1").arg(stalls.isEmpty() ? -1 : stalls.first()));
	ok &= expect(monitor.stallCount() == 1 && monitor.longestStallMs() >= 400 && monitor.totalStallMs() == monitor.longestStallMs(), "the counts record the stall");
	ok &= expect(monitor.summaryText().contains(QStringLiteral("1 interface pause")), QStringLiteral("the summary counts the pause: ") + monitor.summaryText());
	monitor.stop();
	ok &= expect(!monitor.isRunning(), "the monitor stops");
	QThread::msleep(300);
	pump(100);
	ok &= expect(stalls.size() == 1, "a stopped monitor reports nothing");
	return ok;
}

bool checkSplash(QApplication& app)
{
	bool ok = true;
	const StudioThemeTokens& tokens = currentStudioTheme();
	resetStartupStages();
	{
		QPointer<StudioSplash> splash = new StudioSplash;
		splash->setExpectedStages(26);
		splash->listenForStartupStages();
		splash->present();
		app.processEvents();
		ok &= expect(splash->isVisible(), "the splash shows");
		ok &= expect(splash->versionText().contains(QStringLiteral("Pre-alpha")), "the splash states the pre-alpha status");
		reportStartupStage(QStringLiteral("Building the Levels page"));
		ok &= expect(splash->stageLabel() == QStringLiteral("Building the Levels page") && splash->completedStages() == 1, "the splash hears reported stages");
		splash->setStage(QStringLiteral("Halfway"), 13);
		ok &= expect(qFuzzyCompare(splash->progressFraction(), 0.5), "progress is the share of expected stages");
		ok &= expect(splash->accessibleDescription().contains(QStringLiteral("Halfway")), "the stage is in the accessible description");
		ok &= expect(splash->indicator() && splash->indicator()->isTurning(), "the splash's ring turns");
		const QImage frame = splash->grab().toImage();
		save(frame, QStringLiteral("splash-ltr"));
		ok &= expect(differsFromCorner(frame), "the splash paints");
		const int middle = frame.width() / 2;
		const int leftFill = countColour(frame, tokens.colors.selection, 0, middle);
		const int rightFill = countColour(frame, tokens.colors.selection, middle, frame.width());
		ok &= expect(leftFill > 0 && rightFill == 0, QStringLiteral("half the progress track fills from the left: %1 left, %2 right").arg(leftFill).arg(rightFill));
		splash->setReducedMotion(true);
		ok &= expect(!splash->indicator()->isTurning(), "reduced motion stills the splash's ring");
		splash->finish(nullptr);
		ok &= expect(splash->isFinishing() && splash->completedStages() == 26, "finishing completes the progress");
		pump(StudioSplash::kFadeDurationMs + 200);
		ok &= expect(splash.isNull(), "the splash deletes itself once finished");
		ok &= expect(startupStagesReported() == 1, "the splash stops listening once it finishes");
		reportStartupStage(QStringLiteral("After"));
	}
	// Right to left: the track fills from the right, and the mark sits there.
	app.setLayoutDirection(Qt::RightToLeft);
	{
		StudioSplash splash;
		splash.setExpectedStages(4);
		splash.setStage(QStringLiteral("نصف"), 2);
		splash.show();
		app.processEvents();
		const QImage frame = splash.grab().toImage();
		save(frame, QStringLiteral("splash-rtl"));
		const int middle = frame.width() / 2;
		const int leftFill = countColour(frame, tokens.colors.selection, 0, middle);
		const int rightFill = countColour(frame, tokens.colors.selection, middle, frame.width());
		ok &= expect(rightFill > 0 && leftFill == 0, QStringLiteral("right to left, the track fills from the right: %1 left, %2 right").arg(leftFill).arg(rightFill));
	}
	app.setLayoutDirection(Qt::LeftToRight);
	// Every theme and a large text scale paint without complaint.
	for (const StudioTheme theme : {StudioTheme::Light, StudioTheme::HighContrastDark, StudioTheme::HighContrastLight}) {
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, theme == StudioTheme::HighContrastLight ? 200 : 100));
		StudioSplash splash;
		splash.setExpectedStages(5);
		splash.setStage(QStringLiteral("Applying your preferences"), 4);
		splash.show();
		app.processEvents();
		const QImage frame = splash.grab().toImage();
		save(frame, QStringLiteral("splash-") + themeId(theme));
		ok &= expect(differsFromCorner(frame), QStringLiteral("the splash paints in theme %1").arg(themeId(theme)));
		ok &= expect(splash.width() >= 440 && splash.height() > 100, "the splash has a usable size");
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	return ok;
}

} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	bool ok = true;

	const QVector<UiPrimitiveDescriptor> primitives = uiPrimitiveDescriptors();
	for (const QString& id : {QStringLiteral("busy-indicator"), QStringLiteral("busy-overlay"), QStringLiteral("start-up-screen")}) {
		ok &= expect(std::any_of(primitives.cbegin(), primitives.cend(), [&id](const UiPrimitiveDescriptor& primitive) { return primitive.id == id; }),
			QStringLiteral("missing primitive descriptor %1").arg(id));
	}
	ok &= expect(directionIsolated(QStringLiteral("0 ms")) == QString(QChar(0x2068)) + QStringLiteral("0 ms") + QChar(0x2069) && directionIsolated(QString()).isEmpty(),
		"direction isolates wrap text and leave nothing empty");
	ok &= expect(!StudioSplash::platformShowsSplash(), "the offscreen platform shows no splash to anyone");

	ok &= checkStartupStages();
	ok &= checkWorkTracker();
	ok &= checkBusyIndicator(app);
	ok &= checkBusyOverlay(app);
	ok &= checkResponsivenessMonitor();
	ok &= checkSplash(app);

	if (!ok) {
		std::cerr << "studio progress smoke test failed\n";
		return 1;
	}
	std::cout << "studio progress smoke test passed\n";
	return 0;
}
