// View > Camera Floor Up and Camera Floor Down in the Levels page, under the
// NetRadiant Custom and TrenchBroom profiles at 100% and at 200% with high
// contrast, right-to-left and expanded text: the commands, their menu entries
// and Tools tab buttons, Page Up and Page Down in NetRadiant Custom (claimed
// in the camera and the plan views, none in TrenchBroom), stepping through
// the floors and back under them, the status announcements, plan views that
// follow the camera, and that nothing changes the map or its undo history.
// Objects a view filter (Caulk) or a region hides are stepped past.
// A Doom map then checks the eye height and that, in a plan view, Raise Floor
// keeps Page Up while sectors are selected. Owned offscreen widgets and
// semantic Qt events only.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
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
#include <QMenu>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>

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

bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 45000) {
		QEventLoop loop;
		QTimer::singleShot(15, &loop, &QEventLoop::quit);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
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

QString box(std::array<double, 3> lo, std::array<double, 3> hi, const QString& texture = QStringLiteral("base/wall"))
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
			lines << QStringLiteral("%1 %2 %3 %4 0 0 0 0.5 0.5 0 0 0").arg(point(p), point(a), point(b), texture);
		}
	}
	lines << QStringLiteral("}");
	return lines.join(QLatin1Char('\n'));
}

// A key as Qt delivers it: the shortcut override first, then the press.
void pressKey(QWidget* target, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
	target->setFocus(Qt::OtherFocusReason);
	QKeyEvent override(QEvent::ShortcutOverride, key, modifiers);
	QApplication::sendEvent(target, &override);
	QKeyEvent press(QEvent::KeyPress, key, modifiers);
	QApplication::sendEvent(target, &press);
	QKeyEvent release(QEvent::KeyRelease, key, modifiers);
	QApplication::sendEvent(target, &release);
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
	// Over (128, 128): a slab with its top at 0 (stand at 32, underside -32),
	// a caulked platform with its top at 144 (176) and a ceiling slab at 272
	// (304).
	const QByteArray source = QStringLiteral("{\n\"classname\" \"worldspawn\"\n%1\n%2\n%3\n}\n")
								  .arg(box({0, 0, -16}, {256, 256, 0}), box({64, 64, 128}, {192, 192, 144}, QStringLiteral("common/caulk")),
									  box({0, 0, 256}, {256, 256, 272}))
								  .toUtf8();
	const QString path = temp.filePath(QStringLiteral("floors.map"));
	QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(), "fixture")) {
		return 1;
	}
	file.close();
	for (const char* profile : {"netradiant-custom", "trenchbroom"}) {
		for (int scale : {100, 200}) {
			const bool nrc = QLatin1String(profile) == QLatin1String("netradiant-custom");
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Level camera floor UI " << phase.toUtf8().constData() << std::endl;
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
				auto* up = action("map.cameraFloorUp");
				auto* down = action("map.cameraFloorDown");
				if (!expect(up && down, "floor commands exist")) {
					return 1;
				}
				if (!camera->isPerspective()) {
					auto controls = camera->cameraControls();
					controls.perspective = true;
					camera->setCameraControls(controls);
				}
				expect(until([&] { return up->isEnabled() && down->isEnabled(); }), "the first-person camera can step floors");
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
				expect(nrc ? up->shortcuts() == QList<QKeySequence> {QKeySequence(QStringLiteral("PgUp"))} : up->shortcuts().isEmpty(),
					"Page Up steps up only in NetRadiant Custom", up->shortcut().toString());
				expect(nrc ? down->shortcuts() == QList<QKeySequence> {QKeySequence(QStringLiteral("PgDown"))} : down->shortcuts().isEmpty(),
					"Page Down steps down only in NetRadiant Custom", down->shortcut().toString());
				const QByteArray bytes = serializeLevelMap(shell.levelDocument()).bytes;
				const auto revision = shell.levelDocument().revision;
				const auto history = shell.levelDocument().undoStack.size();
				const auto stepTo = [&](QAction* command, double height, const char* label) {
					shell.statusBar()->clearMessage();
					command->trigger();
					const ModelVec3 at = camera->cameraPosition();
					expect(nearly(at.x, 128) && nearly(at.y, 128) && nearly(at.z, height) && nearly(camera->cameraYaw(), 30)
							&& nearly(camera->cameraPitch(), -10) && !shell.statusBar()->currentMessage().isEmpty(),
						label, QStringLiteral("%1 %2 %3: %4").arg(at.x).arg(at.y).arg(at.z).arg(shell.statusBar()->currentMessage()));
				};
				camera->setCameraView({128, 128, 64}, 30, -10);
				stepTo(up, 176, "up to the platform");
				stepTo(up, 304, "up over the ceiling slab");
				stepTo(up, 304, "nothing higher: the camera stays and says so");
				stepTo(down, 176, "down to the platform");
				stepTo(down, 32, "down to the slab");
				stepTo(down, -32, "down under the slab");
				stepTo(up, 32, "back up onto the slab");

				// Plan views that follow the camera follow a floor step.
				auto* follow = action("map.followCamera");
				if (expect(follow, "Plan Views Follow Camera exists")) {
					if (!follow->isChecked()) {
						follow->trigger();
					}
					up->trigger();
					MapViewport* front = planFor(shell, MapViewportProjection::FrontXZ);
					expect(front && until([&] { return nearly(front->navigationState().center.x(), 128) && nearly(front->navigationState().center.y(), 176); }),
						"the front view centres on the camera's new height",
						front ? QStringLiteral("%1 %2").arg(front->navigationState().center.x()).arg(front->navigationState().center.y()) : QString());
					follow->trigger();
					down->trigger();
				}

				// The keys: claimed in the camera and the plan views in NRC,
				// left alone in TrenchBroom.
				MapViewport* top = planFor(shell, MapViewportProjection::TopXY);
				camera->setCameraView({128, 128, 64}, 30, -10);
				pressKey(camera, Qt::Key_PageUp);
				expect(nearly(camera->cameraPosition().z, nrc ? 176 : 64), "Page Up in the camera", QString::number(camera->cameraPosition().z));
				pressKey(camera, Qt::Key_PageDown);
				expect(nearly(camera->cameraPosition().z, nrc ? 32 : 64), "Page Down in the camera", QString::number(camera->cameraPosition().z));
				if (expect(top, "the top view")) {
					camera->setCameraView({128, 128, 64}, 30, -10);
					pressKey(top, Qt::Key_PageUp);
					expect(nearly(camera->cameraPosition().z, nrc ? 176 : 64), "Page Up in a plan view", QString::number(camera->cameraPosition().z));
				}
				// Steps while looking keep mouse look on.
				if (nrc) {
					camera->setCameraView({128, 128, 64}, 30, -10);
					camera->setLooking(true);
					pressKey(camera, Qt::Key_PageUp);
					expect(camera->isLooking() && nearly(camera->cameraPosition().z, 176), "Page Up during mouse look");
					camera->setLooking(false);
				}
				expect(shell.levelDocument().revision == revision && shell.levelDocument().undoStack.size() == history
						&& serializeLevelMap(shell.levelDocument()).bytes == bytes,
					"floor steps never change the map or its undo history");

				// What the views hide through a view filter or a region never
				// counts: the shell hands the service what MapViewport::isHidden
				// reports. After the check above, since a TrenchBroom selection
				// is an undo step.
				if (scale == 100 && expect(top, "the top view for filters and regions")) {
					const auto ready = [&] { return until([&] { return up->isEnabled() && down->isEnabled(); }); };
					auto* caulk = action("map.filterCaulk");
					if (expect(caulk && caulk->isEnabled() && !top->isHidden(LevelMapSelectionKind::QuakeBrush, 1), "the Caulk filter and a shown platform")) {
						caulk->trigger();
						expect(until([&] { return top->isHidden(LevelMapSelectionKind::QuakeBrush, 1); }), "the Caulk filter hides the caulked platform");
						expect(ready(), "the camera is ready after the filter");
						camera->setCameraView({128, 128, 32}, 30, -10);
						stepTo(up, 304, "a filtered platform is stepped past");
						stepTo(down, 32, "and back down past it");
						caulk->trigger();
						expect(until([&] { return !top->isHidden(LevelMapSelectionKind::QuakeBrush, 1); }), "the Caulk filter shows the platform again");
						expect(ready(), "the camera is ready after the filter is cleared");
						stepTo(up, 176, "the platform counts again");
					}
					auto* region = action("map.regionSetSelection");
					auto* clear = action("map.regionClear");
					const QVector<LevelMapSelectionRef> lower {{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakeBrush, 1}};
					top->setSelectionSet(lower);
					top->selectionSetChanged(lower);
					if (expect(region && clear && until([&] { return region->isEnabled(); }), "Set Region to Selection is available")) {
						region->trigger();
						expect(until([&] { return top->isHidden(LevelMapSelectionKind::QuakeBrush, 2); }), "the region leaves the ceiling slab out");
						action("map.selectNone")->trigger();
						expect(ready(), "the camera is ready after the region");
						camera->setCameraView({128, 128, 176}, 30, -10);
						stepTo(up, 176, "a slab outside the region is no floor: the camera stays and says so");
						action("map.regionClear")->trigger();
						expect(until([&] { return !top->isHidden(LevelMapSelectionKind::QuakeBrush, 2); }), "the region is cleared");
						expect(ready(), "the camera is ready after the region is cleared");
						stepTo(up, 304, "with the region cleared the ceiling slab counts again");
					}
					action("map.selectNone")->trigger();
				}
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}

	// A Doom map under NetRadiant Custom: eye height over the sector floor,
	// and Raise Floor keeping Page Up in a plan view while sectors are selected.
	phase = QStringLiteral("netradiant-custom Doom");
	std::cout << "Level camera floor UI " << phase.toUtf8().constData() << std::endl;
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
		auto* up = action("map.cameraFloorUp");
		auto* raise = action("map.raiseFloor");
		auto* top = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
		if (expect(camera && up && raise && top
					&& until([&] { return shell.levelDocument().format == LevelMapFormat::DoomWad && camera->isEnabled() && camera->hasMesh() && up->isEnabled(); }),
				"Doom map and camera ready")) {
			const LevelMapDocument& map = shell.levelDocument();
			double x = 0;
			double y = 0;
			for (const LevelMapDoomVertex& vertex : map.doomVertices) {
				x += vertex.x / map.doomVertices.size();
				y += vertex.y / map.doomVertices.size();
			}
			const double floor = map.doomSectors.first().floorHeight;
			const double ceiling = map.doomSectors.first().ceilingHeight;
			const double eye = std::max(floor, std::min(floor + 41.0, ceiling - 4.0));
			camera->setCameraView({float(x), float(y), float(floor - 50)}, 0, 0);
			up->trigger();
			expect(nearly(camera->cameraPosition().z, eye), "eye height over the sector floor", QString::number(camera->cameraPosition().z));
			const QVector<LevelMapSelectionRef> sector {{LevelMapSelectionKind::DoomSector, map.doomSectors.first().id}};
			top->setSelectionSet(sector);
			top->selectionSetChanged(sector);
			expect(until([&] { return raise->isEnabled(); }), "a selected sector enables Raise Floor");
			const auto history = shell.levelDocument().undoStack.size();
			pressKey(top, Qt::Key_PageUp);
			expect(nearly(shell.levelDocument().doomSectors.first().floorHeight, floor + 8) && shell.levelDocument().undoStack.size() == history + 1
					&& nearly(camera->cameraPosition().z, eye),
				"Raise Floor keeps Page Up in the plan view while it can act", shell.statusBar()->currentMessage());
			// The edit rebuilds the camera's picture; the floor keys wait for it.
			expect(until([&] { return camera->isEnabled() && camera->hasMesh() && up->isEnabled(); }), "the camera is ready again after the edit");
			pressKey(camera, Qt::Key_PageUp);
			expect(nearly(camera->cameraPosition().z, std::max(floor + 8, std::min(floor + 8 + 41.0, ceiling - 4.0))),
				"Page Up in the camera still steps to the raised floor's eye height",
				QStringLiteral("%1: %2").arg(camera->cameraPosition().z).arg(shell.statusBar()->currentMessage()));
		}
	}
	if (ok) {
		std::cout << "Level camera floor UI: commands, keys, floors, undersides, follow camera and Doom eye height passed." << std::endl;
	}
	return ok ? 0 : 1;
}
