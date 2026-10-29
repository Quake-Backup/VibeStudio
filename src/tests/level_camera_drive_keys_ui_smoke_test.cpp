// Camera drive keys from the 2D views and the camera speed in the Levels page:
// NetRadiant Custom at 100% and at 200% with high contrast, right-to-left and
// expanded text, GtkRadiant 1.6.0 and TrenchBroom. Increase and Decrease
// Camera Speed with their menu entries, Tools tab buttons and NetRadiant
// Custom's Shift+keypad keys, the saved speed and its readout above the views;
// A and D strafing the camera from a 2D view in 32-unit steps, bound up, down
// and pitch keys with NetRadiant Custom's pitch stop, GtkRadiant's held keys
// gliding until release or focus loss, a command keeping a key it can act on
// (a user's key, Draw Sector's D on a Doom map), and that nothing changes the
// map or its undo history. Owned offscreen widgets and semantic Qt events
// only.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_camera_keys.h"
#include "tests/doom_preview_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#ifdef VIBESTUDIO_HAVE_QTEST
#include <QTest>
#endif

#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <utility>

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

bool until(const std::function<bool()>& ready, int milliseconds = 45000)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < milliseconds) {
		QEventLoop loop;
		QTimer::singleShot(15, &loop, &QEventLoop::quit);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}

void settle(int milliseconds)
{
	QEventLoop loop;
	QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
	loop.exec(QEventLoop::ExcludeUserInputEvents);
}

bool nearly(double a, double b)
{
	return std::abs(a - b) <= 1.0e-3;
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
			p[axis] = positive ? hi[axis] : lo[axis];
			auto a = p;
			auto b = p;
			int first = (axis + 1) % 3;
			int second = (axis + 2) % 3;
			if (positive) {
				std::swap(first, second);
			}
			a[first] += 1;
			b[second] += 1;
			lines << QStringLiteral("%1 %2 %3 base/wall 0 0 0 0.5 0.5 0 0 0").arg(point(p), point(a), point(b));
		}
	}
	lines << QStringLiteral("}");
	return lines.join(QLatin1Char('\n'));
}

// A key as Qt delivers it to a focused view: the shortcut override, then the
// press and, unless held, the release. Returns whether the view claimed it.
bool pressKey(QWidget* target, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier, bool release = true, bool autoRepeat = false)
{
	target->setFocus(Qt::OtherFocusReason);
	QKeyEvent override(QEvent::ShortcutOverride, key, modifiers, QString(), autoRepeat);
	override.ignore();
	QApplication::sendEvent(target, &override);
	QKeyEvent press(QEvent::KeyPress, key, modifiers, QString(), autoRepeat);
	QApplication::sendEvent(target, &press);
	if (release) {
		QKeyEvent up(QEvent::KeyRelease, key, modifiers);
		QApplication::sendEvent(target, &up);
	}
	return override.isAccepted();
}

void releaseKey(QWidget* target, int key)
{
	QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier);
	QApplication::sendEvent(target, &up);
}

MapViewport* planFor(ApplicationShell& shell, MapViewportProjection projection)
{
	for (MapViewport* view : shell.findChildren<MapViewport*>()) {
		if (view->projection() == projection && !view->QWidget::isHidden()) {
			return view;
		}
	}
	return nullptr;
}

QString point(const ModelVec3& at)
{
	return QStringLiteral("%1 %2 %3").arg(at.x).arg(at.y).arg(at.z);
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
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	const QByteArray source = QStringLiteral("{\n\"classname\" \"worldspawn\"\n%1\n}\n").arg(box({0, 0, -16}, {256, 256, 0})).toUtf8();
	const QString path = temp.filePath(QStringLiteral("slab.map"));
	QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(), "fixture")) {
		return 1;
	}
	file.close();
	struct Pass {
		const char* profile;
		int scale;
	};
	for (const Pass& pass : {Pass {"netradiant-custom", 100}, Pass {"netradiant-custom", 200}, Pass {"gtkradiant-1-6", 100}, Pass {"trenchbroom", 100}}) {
		const QString profile = QString::fromLatin1(pass.profile);
		const bool nrc = profile == QLatin1String("netradiant-custom");
		const bool gtk = profile == QLatin1String("gtkradiant-1-6");
		const int scale = pass.scale;
		phase = QStringLiteral("%1 %2%").arg(profile).arg(scale);
		std::cout << "Level camera drive keys UI " << phase.toUtf8().constData() << std::endl;
		settings.setSelectedEditorProfileId(profile);
		settings.setTextScalePercent(scale);
		const auto theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark;
		settings.setTheme(theme);
		// NetRadiant Custom at 100% binds vertical and pitch drive keys; at 200%
		// it starts from a saved speed and gives A to a command.
		QJsonObject gestures;
		if (nrc && scale == 100) {
			gestures = {{"camera.driveKeys.up", "J"}, {"camera.driveKeys.down", "N"}, {"camera.driveKeys.pitchUp", "O"}, {"camera.driveKeys.pitchDown", "L"}};
		} else if (nrc) {
			gestures = {{"camera.moveSpeed", "1000"}};
		}
		QString error;
		expect(settings.setEditorGestureOverrides(profile, gestures, &error), "gesture preferences", error);
		settings.setUserShortcuts(nrc && scale == 200 ? QHash<QString, QStringList> {{QStringLiteral("map.selectAll"), {QStringLiteral("A")}}}
													  : QHash<QString, QStringList> {});
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
			expect(camera->isPerspective(), "a first-person camera");
			auto* up = action("map.cameraSpeedUp");
			auto* down = action("map.cameraSpeedDown");
			auto* readout = shell.findChild<QLabel*>(QStringLiteral("levelCameraSpeed"));
			if (!expect(up && down && readout, "the speed commands and readout exist")) {
				return 1;
			}
			for (QAction* command : {up, down}) {
				expect(!command->text().isEmpty() && !command->statusTip().isEmpty(), "command text and status tip", command->objectName());
				bool inMenu = false;
				for (QObject* owner : command->associatedObjects()) {
					inMenu = inMenu || qobject_cast<QMenu*>(owner) != nullptr;
				}
				expect(inMenu, "the command is in a menu", command->objectName());
				auto* button = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-") + command->objectName());
				expect(button && !button->accessibleName().isEmpty() && button->focusPolicy() != Qt::NoFocus && QAccessible::queryAccessibleInterface(button),
					"the Tools tab offers an accessible keyboard button", command->objectName());
			}
			expect(nrc ? up->shortcuts() == QList<QKeySequence> {QKeySequence(Qt::ShiftModifier | Qt::KeypadModifier | Qt::Key_Plus)} : up->shortcuts().isEmpty(),
				"Shift+keypad + increases the speed only in NetRadiant Custom", up->shortcut().toString());
			expect(nrc ? down->shortcuts() == QList<QKeySequence> {QKeySequence(Qt::ShiftModifier | Qt::KeypadModifier | Qt::Key_Minus)} : down->shortcuts().isEmpty(),
				"Shift+keypad - decreases the speed only in NetRadiant Custom", down->shortcut().toString());
			const auto readoutShows = [&](int units) {
				return readout->isVisible() && readout->text().contains(QLocale().toString(units)) && !readout->accessibleName().isEmpty()
					&& !readout->accessibleDescription().isEmpty() && readout->toolTip() == readout->accessibleDescription()
					&& readout->width() >= readout->minimumSizeHint().width();
			};
			const QByteArray bytes = serializeLevelMap(shell.levelDocument()).bytes;
			const auto revision = shell.levelDocument().revision;
			const auto history = shell.levelDocument().undoStack.size();
			const auto savedSpeed = [&] {
				return StudioSettings().editorGestureOverrides(profile).value(QStringLiteral("camera.moveSpeed")).toString();
			};

			// The speed: the profile's, stepped, saved and shown.
			const int start = nrc ? (scale == 100 ? 500 : 1000) : 0;
			const double automatic = camera->cameraMoveSpeed();
			expect(start == 0 || nearly(automatic, start), "the camera starts at the profile's or saved speed", QString::number(automatic));
			expect(until([&] { return readoutShows(static_cast<int>(std::lround(automatic))); }), "the readout shows the speed", readout->text());
			expect(until([&] { return up->isEnabled() && down->isEnabled(); }), "both speed commands can act");
			shell.statusBar()->clearMessage();
			up->trigger();
			const int faster = levelCameraSteppedMoveSpeed(start, 1, automatic);
			expect(nearly(camera->cameraMoveSpeed(), faster) && savedSpeed() == QString::number(faster) && !shell.statusBar()->currentMessage().isEmpty()
					&& until([&] { return readoutShows(faster); }, 5000),
				"Increase Camera Speed steps up, saves the speed and says so", QStringLiteral("%1 %2 %3").arg(camera->cameraMoveSpeed()).arg(savedSpeed(), readout->text()));
			if (nrc && scale == 100) {
				down->trigger();
				expect(nearly(camera->cameraMoveSpeed(), 500) && savedSpeed().isEmpty(), "back to the profile's 500 leaves no override", savedSpeed());
				down->trigger();
				down->trigger();
				expect(nearly(camera->cameraMoveSpeed(), 50) && !down->isEnabled() && up->isEnabled() && savedSpeed() == QStringLiteral("50"),
					"down to 50, where Decrease Camera Speed greys out", QString::number(camera->cameraMoveSpeed()));
				for (int step = 0; step < 40 && up->isEnabled(); ++step) {
					up->trigger();
				}
				expect(nearly(camera->cameraMoveSpeed(), 9999) && !up->isEnabled() && down->isEnabled() && until([&] { return readoutShows(9999); }, 5000),
					"up to 9999, where Increase Camera Speed greys out",
					QStringLiteral("%1 %2 %3 %4 %5").arg(camera->cameraMoveSpeed()).arg(up->isEnabled()).arg(readout->text()).arg(readout->width()).arg(readout->minimumSizeHint().width()));
				down->trigger();
				expect(nearly(camera->cameraMoveSpeed(), 9750), "and back down a step", QString::number(camera->cameraMoveSpeed()));
			}
			if (!nrc) {
				down->trigger();
				expect(nearly(camera->cameraMoveSpeed(), levelCameraSteppedMoveSpeed(faster, -1, 0)) && savedSpeed() == QString::number(levelCameraSteppedMoveSpeed(faster, -1, 0)),
					"Decrease Camera Speed steps back down", QString::number(camera->cameraMoveSpeed()));
			}
			expect(StudioSettings().effectiveLevelEditorControls(profile).camera.moveSpeed == static_cast<int>(std::lround(camera->cameraMoveSpeed())),
				"the saved speed is what the profile now uses");

			// The drive keys from a 2D view, in an active window so that focus
			// moves as it does for a user.
			MapViewport* top = planFor(shell, MapViewportProjection::TopXY);
			if (!expect(top, "the top view")) {
				return 1;
			}
			shell.activateWindow();
			expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
			top->setFocus(Qt::OtherFocusReason);
			expect(until([&] { return top->hasFocus(); }), "top view focus");
			const auto place = [&] { camera->setCameraView({128, 128, 64}, 0, 0); };
			const auto at = [&](double x, double y, double z) {
				const ModelVec3 position = camera->cameraPosition();
				return nearly(position.x, x) && nearly(position.y, y) && nearly(position.z, z);
			};
			if (nrc) {
				// A and D strafe in NetRadiant Custom's 32-unit steps; at yaw 0
				// the camera's right is -Y.
				place();
				if (scale == 100) {
					expect(pressKey(top, Qt::Key_A) && at(128, 160, 64) && nearly(camera->cameraYaw(), 0), "A strafes the camera left from the top view", point(camera->cameraPosition()));
					pressKey(top, Qt::Key_A, Qt::NoModifier, false, true);
					expect(at(128, 192, 64), "a repeat steps again", point(camera->cameraPosition()));
					pressKey(top, Qt::Key_D);
					pressKey(top, Qt::Key_D, Qt::KeypadModifier);
					expect(at(128, 128, 64), "D strafes back, the keypad flag aside", point(camera->cameraPosition()));
					expect(pressKey(top, Qt::Key_J) && at(128, 128, 96), "a bound up key lifts the camera", point(camera->cameraPosition()));
					pressKey(top, Qt::Key_N);
					expect(at(128, 128, 64), "a bound down key lowers it", point(camera->cameraPosition()));
					for (int step = 0; step < 5; ++step) {
						pressKey(top, Qt::Key_O);
					}
					expect(nearly(camera->cameraPitch(), 89), "look-up steps stop at NetRadiant Custom's vertical, 1 degree short", QString::number(camera->cameraPitch()));
					place();
					for (int step = 0; step < 5; ++step) {
						pressKey(top, Qt::Key_L);
					}
					expect(nearly(camera->cameraPitch(), -89), "look-down steps stop there too", QString::number(camera->cameraPitch()));
					place();
					expect(!pressKey(top, Qt::Key_A, Qt::ShiftModifier) && at(128, 128, 64), "Shift+A is not a drive key");
					expect(!pressKey(top, Qt::Key_W) && at(128, 128, 64), "the fly keys stay with the camera");
				} else {
					// The user gave A to Select All: it keeps the key in the 2D
					// views, while D still strafes.
					expect(!pressKey(top, Qt::Key_A) && at(128, 128, 64), "a command the user gave A keeps it", point(camera->cameraPosition()));
					expect(pressKey(top, Qt::Key_D) && at(128, 96, 64), "D still strafes", point(camera->cameraPosition()));
				}
			} else if (gtk) {
				// Held keys glide until they are let go or the view loses focus.
				place();
				pressKey(top, Qt::Key_Comma, Qt::NoModifier, false);
				expect(until([&] { return camera->cameraPosition().y > 140; }, 5000), "a held comma glides left", point(camera->cameraPosition()));
				releaseKey(top, Qt::Key_Comma);
				settle(40);
				const ModelVec3 stopped = camera->cameraPosition();
				settle(120);
				expect(nearly(camera->cameraPosition().y, stopped.y), "letting go stops it", point(camera->cameraPosition()));
				pressKey(top, Qt::Key_D, Qt::NoModifier, false);
				expect(until([&] { return camera->cameraPosition().z > 80; }, 5000), "a held D rises", point(camera->cameraPosition()));
				camera->setFocus(Qt::OtherFocusReason);
				expect(until([&] { return camera->hasFocus(); }), "the camera takes focus");
				settle(40);
				const ModelVec3 unfocused = camera->cameraPosition();
				settle(120);
				expect(nearly(camera->cameraPosition().z, unfocused.z), "losing focus stops it", point(camera->cameraPosition()));
				releaseKey(top, Qt::Key_D);
			} else {
				place();
				expect(!pressKey(top, Qt::Key_A) && !pressKey(top, Qt::Key_D) && at(128, 128, 64), "TrenchBroom's camera keys stay in the camera");
			}
			expect(shell.levelDocument().revision == revision && shell.levelDocument().undoStack.size() == history
					&& serializeLevelMap(shell.levelDocument()).bytes == bytes,
				"speed steps and drive keys never change the map or its undo history");

#ifdef VIBESTUDIO_HAVE_QTEST
			if (nrc) {
				// Real key presses through the shortcut map.
				shell.activateWindow();
				expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
				top->setFocus(Qt::OtherFocusReason);
				if (expect(until([&] { return top->hasFocus(); }), "top view focus")) {
					const double before = camera->cameraMoveSpeed();
					QTest::keyClick(top, Qt::Key_Plus, Qt::ShiftModifier | Qt::KeypadModifier);
					expect(nearly(camera->cameraMoveSpeed(), levelCameraSteppedMoveSpeed(static_cast<int>(before), 1, 0)), "Shift+keypad + speeds the camera up",
						QString::number(camera->cameraMoveSpeed()));
					QTest::keyClick(top, Qt::Key_Minus, Qt::ShiftModifier | Qt::KeypadModifier);
					expect(nearly(camera->cameraMoveSpeed(), before), "Shift+keypad - slows it down again", QString::number(camera->cameraMoveSpeed()));
					place();
					QTest::keyClick(top, Qt::Key_D);
					expect(at(128, 96, 64), "a real D press strafes the camera", point(camera->cameraPosition()));
					if (scale == 200) {
						action("map.selectNone")->trigger();
						top->setFocus(Qt::OtherFocusReason);
						const auto selected = shell.levelDocument().selection.size();
						QTest::keyClick(top, Qt::Key_A);
						auto* selectAll = action("map.selectAll");
						expect(at(128, 96, 64) && shell.levelDocument().selection.size() > selected, "a real A press runs the user's Select All instead",
							QStringLiteral("%1; %2 -> %3; keys %4, enabled %5").arg(point(camera->cameraPosition())).arg(selected).arg(shell.levelDocument().selection.size())
								.arg(selectAll ? selectAll->shortcut().toString() : QString(), selectAll && selectAll->isEnabled() ? QStringLiteral("yes") : QStringLiteral("no")));
						action("map.selectNone")->trigger();
					}
				}
			}
#endif
		}
		if (scale == 200) {
			app.removeTranslator(&expanded);
			app.setLayoutDirection(Qt::LeftToRight);
		}
		settings.setEditorGestureOverrides(profile, {}, &error);
		settings.setUserShortcuts({});
		settings.sync();
	}

#ifdef VIBESTUDIO_HAVE_QTEST
	// A Doom map under NetRadiant Custom: Draw Sector keeps D in a 2D view,
	// where it can act, while A still strafes.
	phase = QStringLiteral("netradiant-custom Doom");
	std::cout << "Level camera drive keys UI " << phase.toUtf8().constData() << std::endl;
	settings.setSelectedEditorProfileId(QStringLiteral("netradiant-custom"));
	settings.setTextScalePercent(100);
	settings.setTheme(StudioTheme::Dark);
	settings.sync();
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	const QString wadPath = temp.filePath(QStringLiteral("room.wad"));
	if (expect(tests::doom::write(wadPath, tests::doom::fixture()), "Doom fixture")) {
		ApplicationShell shell;
		shell.resize(1700, 1050);
		shell.show();
		shell.openPathFromCommandLine(wadPath);
		const auto action = [&](const char* id) { return shell.findChild<QAction*>(QString::fromLatin1(id)); };
		action("shell.mode.levels")->trigger();
		action("map.layout.four-views")->trigger();
		auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
		auto* drawSector = action("map.drawSector");
		auto* top = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
		if (expect(camera && drawSector && top
					&& until([&] { return shell.levelDocument().format == LevelMapFormat::DoomWad && camera->isEnabled() && camera->hasMesh() && drawSector->isEnabled(); }),
				"Doom map and camera ready")) {
			shell.activateWindow();
			expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
			top->setFocus(Qt::OtherFocusReason);
			if (expect(until([&] { return top->hasFocus(); }), "top view focus")) {
				camera->setCameraView({0, 0, 64}, 0, 0);
				const ModelVec3 before = camera->cameraPosition();
				QTest::keyClick(top, Qt::Key_D);
				expect(drawSector->isChecked() && nearly(camera->cameraPosition().y, before.y), "Draw Sector keeps D on a Doom map", point(camera->cameraPosition()));
				if (drawSector->isChecked()) {
					drawSector->trigger();
				}
				top->setFocus(Qt::OtherFocusReason);
				QTest::keyClick(top, Qt::Key_A);
				expect(nearly(camera->cameraPosition().y, before.y + 32), "A still strafes the camera", point(camera->cameraPosition()));
			}
		}
	}
#endif
	if (ok) {
		std::cout << "Level camera drive keys UI: speed commands, readout, keys from the 2D views, held keys and command precedence passed." << std::endl;
	}
	return ok ? 0 : 1;
}
