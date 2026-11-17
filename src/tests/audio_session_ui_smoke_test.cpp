#include "app/application_shell.h"
#include "app/audio_automation_editor.h"
#include "app/audio_session_dialog.h"
#include "app/audio_session_timeline.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/fake_audio_playback.h"
#include "tests/fake_audio_stream.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QJsonDocument>
#include <QLabel>
#include <QPaintEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QTreeWidget>
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace slopstudio;
namespace
{
bool expect(bool value, const char *name)
{
	if (!value) {
		std::cerr << name << '\n';
	}
	return value;
}
// The rectangle of every paint event a widget receives.
class PaintLog final : public QObject {
  public:
	QVector<QRect> rects;
	bool eventFilter(QObject *, QEvent *event) override
	{
		if (event->type() == QEvent::Paint) {
			rects.append(static_cast<QPaintEvent *>(event)->rect());
		}
		return false;
	}
};
// For counting the description announcements of one widget.
QObject *watchedDescription = nullptr;
int descriptionEvents = 0;
bool finish(AudioSessionDialog &dialog)
{
	QElapsedTimer timer;
	timer.start();
	while (dialog.isBusy() && timer.elapsed() < 30000) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(1);
	}
	QCoreApplication::processEvents();
	return expect(!dialog.isBusy(), "session worker finishes while processing GUI events");
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
class Expanded final : public QTranslator {
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		if (!QByteArray(context).contains("AudioSession")) {
			return {};
		}
		const auto text = QString::fromUtf8(source);
		return text + QStringLiteral(" · ") + text;
	}
};
} // namespace
int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication application(argc, argv);
#ifdef Q_OS_WIN
	application.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("SLOPSTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root)) {
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("session-ui-XXXXXX"));
	if (!temporary.isValid()) {
		return EXIT_FAILURE;
	}
	const auto file = [&](const char *name) { return QDir(temporary.path()).filePath(QLatin1String(name)); };
	StudioSettings::setOverrideFilePath(file("settings.ini"));
	AudioProject source;
	source.sourceName = QString::fromUtf8("Original voice — 声音.wav");
	source.clip = {1, 48000, QVector<float>(48000)};
	source.endFrame = 48000;
	for (int i = 0; i < source.clip.samples.size(); ++i) {
		source.clip.samples[i] = float(0.25 * std::cos(2 * 3.141592653589793 * 440 * i / 48000));
	}
	const auto original = encodeAudioProject(source);
	bool ok = true;
	auto fake = std::make_shared<FakeAudioStreamState>();
	AudioSessionDialog dialog(nullptr, fakeAudioStreamFactory(fake));
	dialog.setAttribute(Qt::WA_DeleteOnClose, false);
	dialog.show();
	ok &=
	    expect(dialog.importSource(source) && dialog.isBusy() && finish(dialog) && dialog.session().tracks.size() == 1,
	           "asynchronous snapshot import creates editable track");
	if (!ok)
		return EXIT_FAILURE;
	const auto track = dialog.session().tracks[0].id, clip = dialog.session().tracks[0].regions[0].id;
	ok &= expect(encodeAudioProject(source) == original, "GUI import preserves source samples and metadata");
	ok &= expect(dialog.saveSession(file("session.vssession")) && finish(dialog) && !dialog.hasChanges(),
	             "native session save clears dirty revision");
	{
		ApplicationShell shell(nullptr, std::make_unique<FakeAudioPlaybackBackend>());
		shell.openPathFromCommandLine(file("session.vssession"));
		auto *opened = shell.findChild<AudioSessionDialog *>();
		ok &= expect(opened && finish(*opened) && opened->sessionPath() == file("session.vssession") &&
		                 opened->session().tracks.size() == 1,
		             "normal shell open routing recognizes native multitrack sessions");
	}
	AudioSessionEdit edit;
	edit.operation = "track";
	edit.trackId = track;
	edit.name = "Renamed";
	edit.gainDb = -6;
	edit.pan = -1;
	ok &= expect(dialog.applyEdit(edit, "Mix change") && dialog.hasChanges(),
	             "mixer edit is undoable and marks document dirty");
	dialog.undo();
	ok &= expect(!dialog.hasChanges() && dialog.session().tracks[0].gainDb == 0, "undo restores saved revision");
	dialog.redo();
	ok &= expect(dialog.hasChanges() && dialog.session().tracks[0].gainDb == -6,
	             "redo restores independent mixer controls");
	const auto beforeInvalid = audioSessionSummary(dialog.session());
	dialog.selectRegion(track, {});
	QTimer::singleShot(0, &dialog, [&] {
		auto *curves = dialog.findChild<QDialog *>("trackAutomationDialog");
		if (!curves) {
			ok = false;
			return;
		}
		auto *tabs = curves->findChild<QTabWidget *>();
		auto *gain = tabs->widget(0)->findChild<AudioAutomationEditor *>();
		auto *pan = tabs->widget(1)->findChild<AudioAutomationEditor *>();
		gain->setPoints({{0, -6, AudioAutomationCurve::Smooth}, {100, 0}});
		pan->findChild<QPushButton *>("automationAdd")->click();
		ok &= expect(pan->points().size() == 1 && pan->points()[0].value == -1,
		             "first pan point starts at the current static balance");
		curves->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
	});
	dialog.findChild<QAction *>("sessionAutomation")->trigger();
	ok &= expect(dialog.session().tracks[0].gainAutomation.size() == 2 &&
	                 dialog.session().tracks[0].panAutomation.size() == 1,
	             "actual automation action adopts both lane drafts");
	dialog.undo();
	ok &= expect(audioSessionSummary(dialog.session()) == beforeInvalid,
	             "automation dialog is one undoable session edit");
	dialog.redo();
	ok &= expect(dialog.session().tracks[0].gainAutomation[0].curve == AudioAutomationCurve::Smooth,
	             "redo restores authored curve kinds");
	dialog.undo();
	edit.pan = 2;
	ok &= expect(!dialog.applyEdit(edit, "Invalid") && audioSessionSummary(dialog.session()) == beforeInvalid,
	             "invalid GUI edit keeps complete session state");
	edit = {};
	edit.operation = "region";
	edit.trackId = track;
	edit.regionId = clip;
	edit.name = "Far clip";
	edit.position = 10000000000LL;
	edit.length = 48000;
	ok &= expect(dialog.applyEdit(edit, "Move clip"), "GUI retains exact 64-bit timeline positions");
	dialog.setSelection(edit.position, edit.position + 4);
	int coordinated = 0;
	dialog.beforePlayback = [&] { ++coordinated; };
	auto *playback = dialog.findChild<AudioSessionPlayback *>();
	const auto waitState = [&](AudioSessionPlaybackSnapshot::State state) {
		QElapsedTimer timer;
		timer.start();
		while (playback->snapshot().state != state && timer.elapsed() < 10000) {
			QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
			QThread::msleep(1);
		}
		return playback->snapshot().state == state;
	};
	// Play is disabled while playback prepares, which moves the keyboard focus
	// off its button. It must come back, or Space on Play cannot pause.
	QToolButton *playButton = nullptr;
	for (auto *button : dialog.findChildren<QToolButton *>()) {
		if (button->defaultAction() == dialog.findChild<QAction *>("sessionPlay")) {
			playButton = button;
		}
	}
	ok &= expect(playButton != nullptr, "Play Range has a tool button");
	if (!playButton)
		return EXIT_FAILURE;
	playButton->setFocus();
	dialog.prepareMix(true);
	ok &= expect(playback->snapshot().state != AudioSessionPlaybackSnapshot::State::Preparing ||
	                 dialog.focusWidget() != playButton,
	             "preparing playback disables Play and moves the focus on");
	ok &= expect(waitState(AudioSessionPlaybackSnapshot::State::Playing) &&
	                 fake->locked([](auto &v) { return v.opens == 1; }) && coordinated == 1,
	             "far timeline audition streams bounded blocks and coordinates other surfaces");
	ok &= expect(dialog.focusWidget() == playButton, "Play has the keyboard focus again once playback has prepared");
	dialog.prepareMix(true);
	ok &= expect(waitState(AudioSessionPlaybackSnapshot::State::Paused), "streamed session can pause");
	dialog.prepareMix(true);
	ok &= expect(waitState(AudioSessionPlaybackSnapshot::State::Playing) &&
	                 fake->locked([](auto &v) { return v.opens == 1 && v.resumes == 1; }),
	             "streamed session resumes without re-preparation");
	dialog.findChild<QDoubleSpinBox *>("sessionCursor")->setValue(double(edit.position + 1));
	ok &= expect(waitState(AudioSessionPlaybackSnapshot::State::Playing), "numeric cursor can seek session playback");
	dialog.stopPlayback();
	StudioSettings().setReducedMotion(true);
	dialog.prepareMix(true);
	ok &= expect(dialog.findChild<QProgressBar *>()->maximum() == 1,
	             "reduced motion uses static playback preparation progress");
	dialog.stopPlayback();
	ok &= expect(waitState(AudioSessionPlaybackSnapshot::State::Stopped),
	             "other surfaces cancel pending streaming preparation");
	StudioSettings().setReducedMotion(false);
	QByteArray handoff;
	QObject::connect(&dialog, &AudioSessionDialog::mixReady, &dialog, [&](const QByteArray &wav, const QString &path) {
		handoff = wav;
		ok &= expect(path == dialog.sessionPath(), "handoff retains source session protection");
	});
	// Session work disables the timeline while it runs, which moves the keyboard
	// focus on; it must come back when the work ends.
	auto *focusTimeline = dialog.findChild<AudioSessionTimeline *>();
	focusTimeline->setFocus();
	dialog.prepareMix(false);
	ok &= expect(dialog.isBusy() && dialog.focusWidget() != focusTimeline,
	             "session work disables the timeline and moves the focus on");
	ok &= expect(finish(dialog) && !handoff.isEmpty(), "mixdown reaches existing editor handoff");
	ok &= expect(dialog.focusWidget() == focusTimeline, "the timeline has the keyboard focus again after session work");
	{
		// Playback moves the cursor some twenty times a second. That repaints the
		// cursor's strip only, queries the waveform for that strip only, and
		// leaves the spoken description until playback settles.
		const auto &farClip = dialog.session().tracks[0].regions[0];
		AudioSessionTimeline strip;
		strip.setSession(dialog.session(), {{farClip.sourceId, AudioWaveformData::build(source.clip)}});
		strip.setVisibleRange(farClip.position - 4000, farClip.position + farClip.length + 4000);
		strip.resize(900, strip.sizeHint().height());
		strip.show();
		QCoreApplication::processEvents();
		PaintLog painted;
		strip.installEventFilter(&painted);
		const auto whole = [&] {
			QImage image(strip.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			strip.render(&image);
			return image;
		};
		strip.setCursor(farClip.position + 6000);
		QCoreApplication::processEvents();
		painted.rects.clear();
		const QString described = strip.accessibleDescription();
		ok &= expect(described.contains(QString::number(farClip.position + 6000)), "a cursor the user moves is described at once");
		watchedDescription = &strip;
		descriptionEvents = 0;
		const auto previousHandler = QAccessible::installUpdateHandler([](QAccessibleEvent *event) {
			if (event->type() == QAccessible::DescriptionChanged && event->object() == watchedDescription) {
				++descriptionEvents;
			}
		});
		strip.trackCursor(farClip.position + 30000);
		QCoreApplication::processEvents();
		int widest = 0;
		for (const QRect &rect : painted.rects) {
			widest = std::max(widest, rect.width());
		}
		ok &= expect(!painted.rects.isEmpty() && widest < strip.width(),
		             "a cursor moved by playback repaints its strips, not the whole timeline");
		ok &= expect(strip.accessibleDescription() == described && descriptionEvents == 0,
		             "a cursor moved by playback leaves the spoken description alone");
		strip.settleCursor();
		ok &= expect(strip.accessibleDescription().contains(QString::number(farClip.position + 30000)) && descriptionEvents == 1,
		             "the description catches up, with one announcement, when playback settles");
		strip.setCursor(farClip.position + 31000);
		strip.settleCursor();
		ok &= expect(descriptionEvents == 2, "each described cursor move is announced once");
		QAccessible::installUpdateHandler(previousHandler);
		watchedDescription = nullptr;
		// A strip repainted on its own shows exactly what a full repaint shows there.
		const QImage full = whole();
		// The row through the middle of the clip's waveform, where lines alternate with the fill.
		const int waveformRow = int(strip.regionRect(0, farClip).center().y()) + strip.fontMetrics().height() / 2;
		bool samePixels = true, waveformSeen = false;
		for (const int x : {150, 301, 452, 617, 783, 880}) {
			const QRect part(x, 0, 7, strip.height());
			QImage image(strip.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			strip.render(&image, part.topLeft(), QRegion(part));
			samePixels &= image.copy(part) == full.copy(part);
			waveformSeen |= QSet<QRgb>{full.pixel(x, waveformRow), full.pixel(x + 1, waveformRow), full.pixel(x + 2, waveformRow)}.size() > 1;
		}
		ok &= expect(samePixels, "a timeline strip repainted alone matches the same strip of a full repaint");
		ok &= expect(waveformSeen, "the compared strips cross a clip's waveform");
	}
	const auto decoded = decodeAudioClip("mix.wav", handoff);
	ok &= expect(decoded.succeeded() && decoded.clip.frameCount() == 4,
	             "handoff preserves requested range and float samples");
	ok &= expect(dialog.exportMix(file("mix.wav")) && finish(dialog) && read(file("mix.wav")) == handoff,
	             "GUI export and editor handoff share identical float32 mix bytes");
	const auto beforeCancel = encodeAudioSession(dialog.session());
	dialog.prepareMix(false);
	dialog.cancelWork();
	ok &= expect(finish(dialog) && encodeAudioSession(dialog.session()) == beforeCancel,
	             "preparation cancellation leaves session intact");
	ok &= expect(dialog.saveSession(file("session.vssession"), true) && finish(dialog) && !dialog.hasChanges(),
	             "save current revision after timeline edit");
	AudioSessionDialog reopened(nullptr, fakeAudioStreamFactory());
	reopened.setAttribute(Qt::WA_DeleteOnClose, false);
	ok &= expect(reopened.openSession(file("session.vssession")) && finish(reopened) &&
	                 audioSessionSummary(reopened.session()) == audioSessionSummary(dialog.session()),
	             "GUI reopen restores media arrangement and mixer metadata");
	AudioSessionDialog branches(nullptr, fakeAudioStreamFactory());
	branches.setAttribute(Qt::WA_DeleteOnClose, false);
	for (int i = 0; i < 10; ++i) {
		ok &= expect(branches.importSource(source) && finish(branches), "import onto a new undo branch");
		ok &= expect(branches.retainedWaveformBytes() < source.clip.samples.size() * 8,
		             "abandoned import branches release source and waveform storage");
		branches.undo();
		branches.redo();
		ok &= expect(branches.session().sources.size() == 1, "redo retains its media snapshot");
		branches.undo();
	}
	auto *timeline = dialog.findChild<AudioSessionTimeline *>();
	ok &= expect(timeline && timeline->focusPolicy() == Qt::StrongFocus, "timeline accepts keyboard focus");
	if (timeline) {
		timeline->setVisibleRange(edit.position, edit.position + 48000);
		const auto rectangle = timeline->regionRect(0, dialog.session().tracks[0].regions[0]);
		QString selectedTrack;
		ok &= expect(timeline->hitRegion(rectangle.center(), &selectedTrack) == clip && selectedTrack == track,
		             "timeline hit geometry identifies exact track and clip");
		ok &= expect(std::abs(timeline->frameAt(rectangle.center().x()) - (edit.position + 24000)) <= 1,
		             "timeline coordinates retain exact far frame positions");
		const auto *accessible = QAccessible::queryAccessibleInterface(timeline);
		ok &= expect(accessible && accessible->role() == QAccessible::Graphic &&
		                 !accessible->text(QAccessible::Description).isEmpty(),
		             "custom timeline exposes graphic role and current frame description");
	}
	const QString renderRoot = qEnvironmentVariable("SLOPSTUDIO_AUDIO_SESSION_RENDER_DIR");
	if (!renderRoot.isEmpty()) {
		ok &= expect(QDir().mkpath(renderRoot), "create designated render output");
	}
	struct LayoutCase {
		StudioTheme theme;
		int scale;
		bool rtl;
		const char *name;
	};
	for (const auto &config : {LayoutCase{StudioTheme::Dark, 100, false, "dark"},
	                           LayoutCase{StudioTheme::HighContrastLight, 125, false, "contrast-light"},
	                           LayoutCase{StudioTheme::HighContrastDark, 200, true, "expanded-rtl"}}) {
		Expanded expanded;
		if (config.rtl) {
			application.installTranslator(&expanded);
		}
		applyStudioTheme(application, studioThemeTokens(config.theme, UiDensity::Comfortable, config.scale));
		AudioSessionDialog view(nullptr, fakeAudioStreamFactory());
		view.setAttribute(Qt::WA_DeleteOnClose, false);
		view.setLayoutDirection(config.rtl ? Qt::RightToLeft : Qt::LeftToRight);
		view.resize(1100, 780);
		view.show();
		ok &= expect(view.importSource(source) && finish(view), "layout fixture imports actual audio");
		if (!ok || view.session().tracks.isEmpty())
			return EXIT_FAILURE;
		AudioProject rightOnly = source;
		rightOnly.sourceName = QStringLiteral("Right channel texture.wav");
		rightOnly.clip.channels = 2;
		rightOnly.clip.samples.fill(0, 96000);
		for (int frame = 0; frame < 48000; ++frame)
			rightOnly.clip.samples[frame * 2 + 1] = source.clip.samples[frame];
		ok &= expect(view.importSource(rightOnly) && finish(view), "stereo timeline includes right-only media");
		AudioSessionEdit duplicate;
		duplicate.operation = "duplicate";
		duplicate.trackId = view.session().tracks[0].id;
		duplicate.regionId = view.session().tracks[0].regions[0].id;
		duplicate.position = 60000;
		ok &= expect(view.applyEdit(duplicate, "Duplicate"), "layout fixture contains separate editable clips");
		view.findChild<AudioSessionTimeline *>()->setVisibleRange(0, 120000);
		QCoreApplication::processEvents();
		for (auto *box : view.findChildren<QDoubleSpinBox *>()) {
			ok &= expect(!box->accessibleName().isEmpty() && box->focusPolicy() != Qt::NoFocus,
			             "frame controls expose names and focus policy");
		}
		if (!renderRoot.isEmpty()) {
			QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			view.render(&image);
			ok &= expect(image.save(QDir(renderRoot).filePath(QString::fromLatin1(config.name) + ".png")),
			             "render Qt widget directly without OS capture");
		}
		for (const auto &name : {"sessionOutputDevice", "sessionOutputBuffer"}) {
			auto *box = view.findChild<QComboBox *>(name);
			ok &= expect(box && !box->accessibleName().isEmpty() && box->focusPolicy() != Qt::NoFocus,
			             "stream output controls expose named keyboard alternatives");
		}
		if (!renderRoot.isEmpty()) {
			const auto renderControl = [&](QWidget *control, const QString &prefix) {
				for (auto *parent = control->parentWidget(); parent; parent = parent->parentWidget()) {
					if (auto *area = qobject_cast<QScrollArea *>(parent))
						area->ensureWidgetVisible(control, 12, 12);
				}
				QCoreApplication::processEvents();
				QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
				image.fill(Qt::transparent);
				view.render(&image);
				return image.save(QDir(renderRoot).filePath(prefix + QString::fromLatin1(config.name) + ".png"));
			};
			ok &= expect(renderControl(view.findChild<QComboBox *>("sessionOutputBuffer"), "devices-"),
			             "render reachable output controls at each scale");
			view.prepareMix(true);
			auto *stream = view.findChild<AudioSessionPlayback *>();
			QElapsedTimer ready;
			ready.start();
			while (stream->snapshot().state == AudioSessionPlaybackSnapshot::State::Preparing &&
			       ready.elapsed() < 10000) {
				QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
				QThread::msleep(1);
			}
			ok &= expect(stream->snapshot().state == AudioSessionPlaybackSnapshot::State::Playing &&
			                 renderControl(view.findChild<QLabel *>("sessionTransportStatus"), "playing-"),
			             "render live stream meters and status using a fake output");
			view.stopPlayback();
		}
		view.resize(650, 400);
		QCoreApplication::processEvents();
		bool canScroll = false;
		for (auto *scroll : view.findChildren<QScrollArea *>()) {
			canScroll |= scroll->verticalScrollBar()->maximum() > 0;
		}
		ok &= expect(canScroll, "small window retains controls through scrolling");
		if (config.rtl) {
			application.removeTranslator(&expanded);
		}
	}
	std::cout << (ok ? "Audio session UI verification passed\n" : "Audio session UI verification failed\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
