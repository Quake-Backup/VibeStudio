// View > Camera Display in the Levels page: Far Clip in Camera with Shorter
// and Longer Draw Distance (NetRadiant Custom's cubic clip), the View tab's
// box and Draw distance list kept in step, palette entries, NRC's Ctrl+\,
// Ctrl+[ and Ctrl+] (none in TrenchBroom), the announcements, and the saved
// choice each next shell starts with (the key editor far-clip reads). Runs
// under both profiles at 100% and at 200% with high contrast, right-to-left
// and expanded text. The commands never change the map or its undo history.
// With a renderer (each one that starts here), the camera's pixels and picks
// show a box beyond the draw distance gone, faces and wireframe alike, and
// back at a longer one; the nearer box untouched; fewer triangles counted as
// drawn; the fog ending at the clip; and the clip in the orthographic orbit
// camera. Show Grid and 3D Wireframe are registered checkable. Owned
// offscreen widgets and semantic Qt events only.

#include "app/application_shell.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_camera_render.h"
#include "core/render_device.h"
#include "core/studio_semantics.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#ifdef VIBESTUDIO_HAVE_QTEST
#include <QTest>
#endif

#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <tuple>
#include <utility>
#include <vector>

using namespace vibestudio;

namespace {

bool ok = true;
QString phase;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << phase.toUtf8().constData() << ": " << label << ": " << detail.toUtf8().constData() << std::endl;
	}
	return value;
}

bool until(const std::function<bool()>& ready, int timeoutMs = 45000)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < timeoutMs) {
		QEventLoop loop;
		QTimer::singleShot(15, &loop, &QEventLoop::quit);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}

class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};

QString box(std::array<double, 3> lo, std::array<double, 3> hi)
{
	QStringList lines {QStringLiteral("{")};
	const auto point = [](const std::array<double, 3>& v) {
		return QStringLiteral("( %1 %2 %3 )").arg(v[0], 0, 'g', 10).arg(v[1], 0, 'g', 10).arg(v[2], 0, 'g', 10);
	};
	for (int axis = 0; axis < 3; ++axis) {
		for (const bool positive : {false, true}) {
			std::array<double, 3> p = lo;
			p[size_t(axis)] = positive ? hi[size_t(axis)] : lo[size_t(axis)];
			auto a = p;
			auto b = p;
			int first = (axis + 1) % 3;
			int second = (axis + 2) % 3;
			if (positive) {
				std::swap(first, second);
			}
			a[size_t(first)] += 1;
			b[size_t(second)] += 1;
			lines << QStringLiteral("%1 %2 %3 base/wall 0 0 0 0.5 0.5 0 0 0").arg(point(p), point(a), point(b));
		}
	}
	lines << QStringLiteral("}");
	return lines.join(QLatin1Char('\n'));
}

QString describe(const LevelCameraFarClipState& state)
{
	return QStringLiteral("on=%1 step=%2").arg(int(state.enabled)).arg(state.step);
}

int differences(const QImage& a, const QImage& b, const QRect& area)
{
	if (a.size() != b.size()) {
		return -1;
	}
	int count = 0;
	const QRect clipped = area.intersected(a.rect());
	for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
		for (int x = clipped.left(); x <= clipped.right(); ++x) {
			count += a.pixel(x, y) != b.pixel(x, y) ? 1 : 0;
		}
	}
	return count;
}

// Pixels of `area` that are not `colour`.
int other(const QImage& image, const QRect& area, QRgb colour)
{
	int count = 0;
	const QRect clipped = area.intersected(image.rect());
	for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
		for (int x = clipped.left(); x <= clipped.right(); ++x) {
			count += image.pixel(x, y) != colour ? 1 : 0;
		}
	}
	return count;
}

int distance(QRgb a, QRgb b)
{
	return std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) + std::abs(qBlue(a) - qBlue(b));
}

QString rgb(QRgb value)
{
	return QStringLiteral("%1,%2,%3").arg(qRed(value)).arg(qGreen(value)).arg(qBlue(value));
}

// The triangles the camera counts as drawn (English status lines).
int drawnTriangles(const ModelViewport& camera)
{
	static const QRegularExpression pattern(QStringLiteral("\\((\\d+) drawn"));
	for (const QString& line : camera.statusLines()) {
		const auto match = pattern.match(line);
		if (match.hasMatch()) {
			return match.captured(1).toInt();
		}
	}
	return -1;
}

} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	prepareRenderBackends();
	RenderDeviceInfo failure;
	const bool renderer = activeRenderDevice(&failure) != nullptr;
	if (!renderer) {
		std::cout << "No 3D renderer starts here; the camera's pixel checks are skipped. " << failure.error.toUtf8().constData() << std::endl;
	}
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	const QString settingsPath = temp.filePath(QStringLiteral("settings.ini"));
	StudioSettings::setOverrideFilePath(settingsPath);
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	// A near box to the right of the camera's view and a far one, 1500 to
	// 1564 units ahead, to the left (camwindow's default draw distance is
	// 1448 units).
	const std::array<double, 3> nearLo {0, -128, 0}, nearHi {64, -64, 64};
	const std::array<double, 3> farLo {1500, 64, -128}, farHi {1564, 576, 256};
	const QByteArray source = QStringLiteral("{\n\"classname\" \"worldspawn\"\n%1\n%2\n}\n").arg(box(nearLo, nearHi), box(farLo, farHi)).toUtf8();
	const QString path = temp.filePath(QStringLiteral("far-clip.map"));
	QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(), "fixture")) {
		return 1;
	}
	file.close();

	// What each shell leaves for the next one to start with.
	const auto state = [](bool enabled, int step) {
		LevelCameraFarClipState result;
		result.enabled = enabled;
		result.step = step;
		return result;
	};
	const std::array<LevelCameraFarClipState, 4> leaves {state(true, 12), state(false, 20), state(true, 1), state(false, kLevelCameraFarClipDefaultStep)};
	LevelCameraFarClipState expectedStart;
	int run = 0;
	for (const char* profile : {"netradiant-custom", "trenchbroom"}) {
		const bool nrc = QLatin1String(profile) == QLatin1String("netradiant-custom");
		for (int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Level camera far clip UI " << phase.toUtf8().constData() << std::endl;
			settings.setSelectedEditorProfileId(QString::fromLatin1(profile));
			settings.setTextScalePercent(scale);
			const auto theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark;
			settings.setTheme(theme);
			settings.sync();
			applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
			Expanded expanded;
			if (scale == 200) {
				app.installTranslator(&expanded);
				app.setLayoutDirection(Qt::RightToLeft);
			}
			{
				ApplicationShell shell;
				shell.resize(scale == 100 ? 1700 : 2600, scale == 100 ? 1050 : 1800);
				shell.show();
				shell.openPathFromCommandLine(path);
				const auto action = [&](const char* id) { return shell.findChild<QAction*>(QString::fromLatin1(id)); };
				action("shell.mode.levels")->trigger();
				action("map.layout.four-views")->trigger();
				auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
				if (!expect(camera && until([&] { return shell.levelDocument().sourcePath == path && camera->isEnabled() && camera->hasMesh(); }),
						"map and camera ready")) {
					return 1;
				}
				const char* ids[] = {"map.previewFarClip", "map.shorterDrawDistance", "map.longerDrawDistance"};
				bool present = true;
				for (const char* id : ids) {
					present = expect(action(id) != nullptr, "the command exists", QString::fromLatin1(id)) && present;
				}
				auto* viewFarClip = shell.findChild<QCheckBox*>(QStringLiteral("levelViewFarClip"));
				auto* row = shell.findChild<QWidget*>(QStringLiteral("levelViewFarClipRange"));
				auto* list = shell.findChild<QComboBox*>(QStringLiteral("levelViewFarClipDistance"));
				if (!expect(present && viewFarClip && row && list, "the View tab box and the Draw distance list exist")) {
					return 1;
				}
				const auto rowShown = [&] { return !(row->testAttribute(Qt::WA_WState_ExplicitShowHide) && row->testAttribute(Qt::WA_WState_Hidden)); };
				const auto showing = [&](const LevelCameraFarClipState& wanted) {
					return camera->farClip() == levelCameraFarClipPlane(wanted) && action("map.previewFarClip")->isChecked() == wanted.enabled
						&& viewFarClip->isChecked() == wanted.enabled && rowShown() == wanted.enabled && list->currentData().toInt() == wanted.step;
				};
				const auto actual = [&] {
					return QStringLiteral("camera=%1 check=%2 box=%3 row=%4 list=%5")
						.arg(camera->farClip())
						.arg(int(action("map.previewFarClip")->isChecked()))
						.arg(int(viewFarClip->isChecked()))
						.arg(int(rowShown()))
						.arg(list->currentData().toInt());
				};
				const auto saved = [&] {
					return StudioSettings(settingsPath, StudioSettings::AccessMode::ReadOnly).shellLayoutState(levelCameraFarClipSettingsKey());
				};
				expect(showing(expectedStart), "the saved far clip comes back", describe(expectedStart) + QStringLiteral(" / ") + actual());

				// Labelled View > Camera Display items with palette entries; NRC's
				// keys in its profile, none in TrenchBroom's, which has no far clip.
				const std::array<QKeySequence, 3> keys {QKeySequence(QStringLiteral("Ctrl+\\")), QKeySequence(QStringLiteral("Ctrl+[")),
					QKeySequence(QStringLiteral("Ctrl+]"))};
				for (size_t index = 0; index < 3; ++index) {
					QAction* command = action(ids[index]);
					expect(command->isCheckable() == (index == 0) && !command->text().isEmpty() && !command->statusTip().isEmpty() && command->isEnabled(),
						"a command with text, checkable only for the toggle", QString::fromLatin1(ids[index]));
					QMenu* menu = nullptr;
					for (QObject* owner : command->associatedObjects()) {
						if (auto* found = qobject_cast<QMenu*>(owner)) {
							menu = found;
						}
					}
					expect(menu && menu->title().contains(QStringLiteral("Camera Display")), "the command is in View > Camera Display",
						menu ? menu->title() : QString::fromLatin1(ids[index]));
					QString kebab;
					for (const QChar character : QString::fromLatin1(ids[index])) {
						kebab += character.isUpper() ? QStringLiteral("-") + character.toLower() : QString(character);
					}
					CommandPaletteEntry entry;
					expect(commandPaletteEntryForCommandId(kebab, &entry) && !entry.label.isEmpty(), "the command is in command search", kebab);
					const QList<QKeySequence> wanted = nrc ? QList<QKeySequence> {keys[index]} : QList<QKeySequence> {};
					expect(command->shortcuts() == wanted, "the profile's key", QString::fromLatin1(ids[index]) + QStringLiteral(": ")
							+ QKeySequence::listToString(command->shortcuts()));
				}
				// Show Grid and 3D Wireframe are checkable commands.
				expect(action("map.toggleGrid")->isCheckable() && action("map.previewWireframe")->isCheckable(), "Show Grid and 3D Wireframe are checkable");
				for (QWidget* widget : std::initializer_list<QWidget*> {viewFarClip, list}) {
					QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(widget);
					const bool combo = widget == list;
					expect(accessible && accessible->role() == (combo ? QAccessible::ComboBox : QAccessible::CheckBox)
							&& !accessible->text(QAccessible::Name).isEmpty() && widget->focusPolicy() != Qt::NoFocus && !widget->toolTip().isEmpty(),
						"a name, a role, a tooltip and keyboard focus", widget->objectName());
				}
				int buddies = 0;
				for (QLabel* label : row->findChildren<QLabel*>()) {
					buddies += label->buddy() == list ? 1 : 0;
				}
				expect(buddies == 1 && list->count() == kLevelCameraFarClipMaximumStep, "the Draw distance list is labelled and has every step",
					QString::number(list->count()));
				expect(viewFarClip->text() == QString(action("map.previewFarClip")->text()).remove(QLatin1Char('&')), "the View tab box names the command",
					viewFarClip->text());

				const QByteArray bytes = serializeLevelMap(shell.levelDocument()).bytes;
				const auto untouched = [&](const std::function<void()>& command, const QString& name) {
					const auto revision = shell.levelDocument().revision;
					const auto history = shell.levelDocument().undoStack.size();
					command();
					expect(shell.levelDocument().revision == revision && shell.levelDocument().undoStack.size() == history,
						"a display command adds no edit or undo step", name);
				};
				const LevelCameraShadingState shading;
				LevelCameraFarClipState current = expectedStart;
				const auto check = [&](const char* label, const QString& message) {
					expect(showing(current) && saved() == levelCameraFarClipText(current), label,
						describe(current) + QStringLiteral(" / ") + actual() + QStringLiteral(" / saved ") + QString::fromLatin1(saved()));
					if (!message.isNull()) {
						expect(shell.statusBar()->currentMessage() == message, "the change is announced", shell.statusBar()->currentMessage());
					}
				};
				// Off at the default step, through the commands themselves.
				if (current.enabled) {
					untouched([&] { action("map.previewFarClip")->trigger(); }, QStringLiteral("toggle off"));
					current.enabled = false;
					check("the toggle turns the clip off", levelCameraFarClipSummary(current, shading));
				}
				untouched([&] { list->setCurrentIndex(list->findData(kLevelCameraFarClipDefaultStep)); }, QStringLiteral("list"));
				current.step = kLevelCameraFarClipDefaultStep;
				expect(showing(current), "the list sets the step", actual());

				untouched([&] { action("map.previewFarClip")->trigger(); }, QStringLiteral("toggle on"));
				current.enabled = true;
				check("Far Clip in Camera turns the clip on at 1448 units", levelCameraFarClipSummary(current, shading));
				expect(camera->farClip() == 1448.0, "the camera's far plane", QString::number(camera->farClip()));
				LevelCameraFarClipState before = current;
				untouched([&] { action("map.shorterDrawDistance")->trigger(); }, QStringLiteral("shorter"));
				current.step = 13;
				check("Shorter Draw Distance steps in", levelCameraFarClipStepAnnouncement(before, current));
				before = current;
				untouched([&] { action("map.longerDrawDistance")->trigger(); }, QStringLiteral("longer"));
				untouched([&] { action("map.longerDrawDistance")->trigger(); }, QStringLiteral("longer again"));
				current.step = 15;
				check("Longer Draw Distance steps out", levelCameraFarClipStepAnnouncement(levelCameraWithFarClipStep(before, 14), current));
				expect(camera->farClip() == 2048.0, "the camera follows", QString::number(camera->farClip()));
				before = current;
				untouched([&] { list->setCurrentIndex(list->findData(kLevelCameraFarClipMinimumStep)); }, QStringLiteral("list shortest"));
				current.step = kLevelCameraFarClipMinimumStep;
				check("the list picks the shortest distance", levelCameraFarClipStepAnnouncement(before, current));
				untouched([&] { action("map.shorterDrawDistance")->trigger(); }, QStringLiteral("shorter at the end"));
				check("the shortest distance holds", levelCameraFarClipStepAnnouncement(current, current));
				list->setCurrentIndex(list->findData(kLevelCameraFarClipMaximumStep));
				current.step = kLevelCameraFarClipMaximumStep;
				untouched([&] { action("map.longerDrawDistance")->trigger(); }, QStringLiteral("longer at the end"));
				check("the longest distance holds", levelCameraFarClipStepAnnouncement(current, current));

				// The View tab box is the command; with the clip off the steps
				// still move the distance, which waits for the clip.
				untouched([&] { viewFarClip->setChecked(false); }, QStringLiteral("View tab off"));
				current.enabled = false;
				check("the View tab box turns the clip off and hides the list", levelCameraFarClipSummary(current, shading));
				before = current;
				untouched([&] { action("map.shorterDrawDistance")->trigger(); }, QStringLiteral("shorter while off"));
				current.step = kLevelCameraFarClipMaximumStep - 1;
				check("a step while off moves the distance only", levelCameraFarClipStepAnnouncement(before, current));
				expect(camera->farClip() == 0.0, "the camera still draws everything", QString::number(camera->farClip()));

				// A saved choice, available with the camera hidden too.
				action("map.layout.single-2d")->trigger();
				expect(until([&] { return !camera->isVisible(); }), "the camera is hidden");
				for (const char* id : ids) {
					expect(action(id)->isEnabled(), "a saved choice stays available", QString::fromLatin1(id));
				}
				untouched([&] { action("map.previewFarClip")->trigger(); }, QStringLiteral("toggle while hidden"));
				current.enabled = true;
				action("map.layout.four-views")->trigger();
				expect(until([&] { return camera->isVisible() && camera->isEnabled(); }), "the camera comes back");
				check("and draws with the clip turned on while hidden", QString());

#ifdef VIBESTUDIO_HAVE_QTEST
				if (nrc) {
					// NetRadiant Custom's keys, pressed in the camera.
					shell.activateWindow();
					expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
					camera->setFocus(Qt::OtherFocusReason);
					if (expect(until([&] { return camera->hasFocus(); }), "camera focus")) {
						QTest::keyClick(camera, Qt::Key_BracketLeft, Qt::ControlModifier);
						current.step -= 1;
						check("Ctrl+[ shortens the draw distance", QString());
						QTest::keyClick(camera, Qt::Key_BracketRight, Qt::ControlModifier);
						current.step += 1;
						check("Ctrl+] lengthens it", QString());
						QTest::keyClick(camera, Qt::Key_Backslash, Qt::ControlModifier);
						current.enabled = false;
						check("Ctrl+\\ turns the clip off", QString());
						QTest::keyClick(camera, Qt::Key_Backslash, Qt::ControlModifier);
						current.enabled = true;
						check("and on again", QString());
					}
				}
#endif

				// The camera's pixels and picks, on every renderer that starts here.
				if (renderer && run == 0) {
					action("map.hideCameraTextures")->trigger();
					if (action("map.previewEdges")->isChecked()) {
						action("map.previewEdges")->trigger();
					}
					// No ground grid to sample through.
					if (action("map.toggleGrid")->isChecked()) {
						action("map.toggleGrid")->trigger();
					}
					const auto setClip = [&](bool enabled, int step) {
						list->setCurrentIndex(list->findData(step));
						if (action("map.previewFarClip")->isChecked() != enabled) {
							action("map.previewFarClip")->trigger();
						}
						current = state(enabled, step);
						expect(showing(current), "the clip for the pixels", describe(current) + QStringLiteral(" / ") + actual());
					};
					const auto view = [&] { camera->setCameraView({-100, 0, 32}, 0, 0); };
					view();
					const auto projected = [&](const std::array<double, 3>& lo, const std::array<double, 3>& hi) {
						double left = 1.0e9, top = 1.0e9, right = -1.0e9, bottom = -1.0e9;
						for (int corner = 0; corner < 8; ++corner) {
							const ModelVec3 point {float((corner & 1) ? hi[0] : lo[0]), float((corner & 2) ? hi[1] : lo[1]), float((corner & 4) ? hi[2] : lo[2])};
							QPointF screen;
							if (camera->projectToView(point, &screen)) {
								left = std::min(left, screen.x());
								right = std::max(right, screen.x());
								top = std::min(top, screen.y());
								bottom = std::max(bottom, screen.y());
							}
						}
						if (right < left) {
							return QRect();
						}
						return QRectF(QPointF(left, top), QPointF(right, bottom)).toAlignedRect().adjusted(3, 3, -3, -3).intersected(camera->rect().adjusted(2, 2, -2, -2));
					};
					const QRect nearArea = projected(nearLo, nearHi);
					const QRect farArea = projected(farLo, farHi);
					const QPoint nearCentre = nearArea.center();
					const QPoint farCentre = farArea.center();
					expect(nearArea.width() > 8 && farArea.width() > 8 && !nearArea.intersects(farArea), "both boxes are in view, apart",
						QStringLiteral("near %1,%2 %3x%4 far %5,%6 %7x%8")
							.arg(nearArea.x())
							.arg(nearArea.y())
							.arg(nearArea.width())
							.arg(nearArea.height())
							.arg(farArea.x())
							.arg(farArea.y())
							.arg(farArea.width())
							.arg(farArea.height()));
					std::vector<std::pair<RenderBackendChoice, const char*>> backends;
					for (const auto& [backend, choice, name] : {std::tuple {RenderBackend::Vulkan, RenderBackendChoice::Vulkan, "Vulkan"},
							 std::tuple {RenderBackend::OpenGL, RenderBackendChoice::OpenGL, "OpenGL"}}) {
						if (renderDevice(backend)->isAvailable()) {
							backends.push_back({choice, name});
						}
					}
					expect(!backends.empty(), "a renderer is available by name");
					for (const auto& [choice, name] : backends) {
						const QString backendName = QString::fromLatin1(name);
						setRenderBackendChoiceOverride(choice);
						const auto picture = [&] {
							expect(tests::settleModelViewport(*camera), "the camera finishes drawing", backendName);
							expect(tests::settleModelViewport(*camera), "the camera finishes drawing again", backendName);
							QImage image(camera->size(), QImage::Format_ARGB32_Premultiplied);
							image.fill(Qt::transparent);
							camera->render(&image);
							return image;
						};
						// Faces: the far box goes beyond 1448 units, picks and counts
						// with it, and comes back at 2048; the near box never changes.
						setClip(false, kLevelCameraFarClipDefaultStep);
						const QImage all = picture();
						const bool farPicked = camera->hitAt(farCentre).valid;
						const int drawnAll = drawnTriangles(*camera);
						setClip(true, kLevelCameraFarClipDefaultStep);
						const QImage clipped = picture();
						const bool farPickedClipped = camera->hitAt(farCentre).valid;
						const bool nearPickedClipped = camera->hitAt(nearCentre).valid;
						const int drawnClipped = drawnTriangles(*camera);
						const QRgb background = clipped.pixel(farCentre);
						expect(differences(all, clipped, farArea) > farArea.width() * farArea.height() / 2 && other(clipped, farArea, background) == 0,
							"the far box is not drawn beyond the draw distance",
							QStringLiteral("%1: %2 changed, %3 not background").arg(backendName).arg(differences(all, clipped, farArea)).arg(other(clipped, farArea, background)));
						expect(differences(all, clipped, nearArea) == 0, "the near box is untouched",
							QStringLiteral("%1: %2").arg(backendName).arg(differences(all, clipped, nearArea)));
						expect(farPicked && !farPickedClipped && nearPickedClipped, "a pick finds the far box only while it is drawn", backendName);
						expect(drawnAll > 0 && drawnClipped > 0 && drawnClipped < drawnAll, "fewer triangles are counted as drawn",
							QStringLiteral("%1: %2 -> %3").arg(backendName).arg(drawnAll).arg(drawnClipped));
						action("map.longerDrawDistance")->trigger();
						current.step += 1;
						const QImage longer = picture();
						expect(differences(all, longer, farArea) == 0 && camera->hitAt(farCentre).valid, "at 2048 units the far box is back",
							QStringLiteral("%1: %2").arg(backendName).arg(differences(all, longer, farArea)));

						// 3D Wireframe: the far box's edges go too.
						action("map.hideCameraFaces")->trigger();
						setClip(false, kLevelCameraFarClipDefaultStep);
						const QImage wire = picture();
						setClip(true, kLevelCameraFarClipDefaultStep);
						const QImage wireClipped = picture();
						const QRect farEdges = farArea.adjusted(-4, -4, 4, 4);
						expect(other(wire, farEdges, background) > 0 && other(wireClipped, farEdges, background) == 0,
							"wireframe edges beyond the draw distance are not drawn",
							QStringLiteral("%1: %2 -> %3").arg(backendName).arg(other(wire, farEdges, background)).arg(other(wireClipped, farEdges, background)));
						action("map.hideCameraTextures")->trigger();

						// Fog from 0 to 20000 units barely touches the near box; with
						// the clip at 256 units it ends there and thickens.
						auto* fogStart = shell.findChild<QSpinBox*>(QStringLiteral("levelViewFogStart"));
						auto* fogEnd = shell.findChild<QSpinBox*>(QStringLiteral("levelViewFogEnd"));
						if (expect(fogStart && fogEnd, "the fog distances exist", backendName)) {
							setClip(false, 9);
							const QImage clear = picture();
							action("map.previewFog")->trigger();
							fogEnd->setValue(20000);
							fogStart->setValue(0);
							const QImage thin = picture();
							setClip(true, 9);
							const QImage thick = picture();
							expect(shell.statusBar()->currentMessage().contains(levelCameraFarClipDistanceText(9)), "the fog's end at the clip is announced",
								shell.statusBar()->currentMessage());
							action("map.previewFog")->trigger();
							fogStart->setValue(kLevelCameraFogDefaultStart);
							fogEnd->setValue(kLevelCameraFogDefaultEnd);
							const int unfogged = distance(clear.pixel(nearCentre), background);
							expect(distance(thin.pixel(nearCentre), clear.pixel(nearCentre)) <= 6, "a long fog barely changes the near box",
								QStringLiteral("%1: %2 -> %3").arg(backendName, rgb(clear.pixel(nearCentre)), rgb(thin.pixel(nearCentre))));
							expect(distance(thick.pixel(nearCentre), background) * 10 <= unfogged * 7, "with the clip on the fog ends at it, so it thickens",
								QStringLiteral("%1: %2 -> %3 (background %4)")
									.arg(backendName, rgb(clear.pixel(nearCentre)), rgb(thick.pixel(nearCentre)), rgb(background)));
						}

						// The orthographic orbit camera clips that depth behind the
						// front of the map.
						const CameraViewControls profileControls = camera->cameraControls();
						camera->setCameraControls(CameraViewControls {});
						expect(!camera->isPerspective(), "an orthographic camera", backendName);
						setClip(false, kLevelCameraFarClipMinimumStep);
						const QImage orbit = picture();
						setClip(true, kLevelCameraFarClipMinimumStep);
						const QImage orbitClipped = picture();
						expect(differences(orbit, orbitClipped, camera->rect()) > 0, "the clip shows in the orthographic camera too",
							QStringLiteral("%1: %2").arg(backendName).arg(differences(orbit, orbitClipped, camera->rect())));
						camera->setCameraControls(profileControls);
						view();
						std::cout << name << " camera far clip: " << differences(all, clipped, farArea) << " far-box pixels cleared, " << drawnAll << " -> "
								  << drawnClipped << " triangles drawn" << std::endl;
					}
					setRenderBackendChoiceOverride(RenderBackendChoice::Automatic);
					action("map.showCameraTextures")->trigger();
					if (!action("map.toggleGrid")->isChecked()) {
						action("map.toggleGrid")->trigger();
					}
				}

				expect(serializeLevelMap(shell.levelDocument()).bytes == bytes, "the far clip never changes the map");

				// Leave a choice for the next shell to bring back.
				const LevelCameraFarClipState leave = leaves[size_t(run)];
				list->setCurrentIndex(list->findData(leave.step));
				viewFarClip->setChecked(leave.enabled);
				current = leave;
				check("the choice to leave", QString());
				expectedStart = leave;
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
			++run;
		}
	}
	if (ok) {
		std::cout << "Level camera far clip UI: commands, View tab, keys, saved choice and pixels passed." << std::endl;
	}
	return ok ? 0 : 1;
}
