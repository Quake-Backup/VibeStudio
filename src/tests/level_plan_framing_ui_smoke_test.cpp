// Plan framing in the Levels page, under the NetRadiant Custom and
// TrenchBroom profiles at 100% and at 200% with high contrast, right-to-left
// and expanded text: a one-plan Top, Front, Side or Next 2D View keeps the
// zoom and centres on the selection, or on the camera when nothing is
// selected, and the plane already shown recentres; NetRadiant Custom's
// Ctrl+Tab in a plan view steps the plane ahead of the view's own Ctrl+Tab,
// which still extends the selection elsewhere; Zoom 100%; Frame Selection
// with NRC's Radiant framing (2D views only, two-thirds margin, the camera's
// surroundings when nothing is selected) and TrenchBroom's camera framing;
// Reset 2D Views from the top and front panes and its availability; and NRC's
// three-plan Top, Front and Side recentring every plan. The commands' menus,
// Tools tab buttons and palette entries, and that the map never changes. On a
// Doom map, Draw Sector's switch to Top keeps the zoom too. Read Aloud keeps
// Ctrl+Shift+U in every editor profile and every Levels view (TrenchBroom's
// Reset 2D Cameras key is left unbound), and a user who gives Reset 2D Views
// that key gets it only in a 2D view where it can act. Speech goes to the
// log engine. Owned offscreen widgets and semantic Qt events only.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_speech.h"
#include "app/studio_theme.h"
#include "core/editor_profiles.h"
#include "core/level_navigation.h"
#include "core/studio_semantics.h"
#include "tests/doom_preview_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QMenu>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#ifdef VIBESTUDIO_HAVE_QTEST
#include <QTest>
#endif

#include <algorithm>
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

bool near(double a, double b)
{
	return std::abs(a - b) <= 1.0e-6 * std::max(1.0, std::abs(b));
}

bool at(const MapViewport* view, MapViewportProjection projection, QPointF centre, double zoom)
{
	const PlanViewState state = view->navigationState();
	return view->projection() == projection && near(state.center.x(), centre.x()) && near(state.center.y(), centre.y()) && near(state.zoom, zoom);
}

QString describe(const MapViewport* view)
{
	const PlanViewState state = view->navigationState();
	return QStringLiteral("projection %1 centre %2,%3 zoom %4").arg(state.projection).arg(state.center.x()).arg(state.center.y()).arg(state.zoom);
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

} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
	// Read Aloud is pressed below: log what would be said, silently.
	qputenv("VIBESTUDIO_SPEECH_ENGINE", "log");
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
	// A floor, a pillar to select (centre 288, 160, 128) and a block far away,
	// so a fit of the whole map is far from any kept zoom.
	const std::array<double, 3> pillarLo {256, 128, 0};
	const std::array<double, 3> pillarHi {320, 192, 256};
	const QByteArray source = QStringLiteral("{\n\"classname\" \"worldspawn\"\n%1\n%2\n%3\n}\n")
								  .arg(box({-512, -512, -16}, {512, 512, 0}), box(pillarLo, pillarHi), box({2048, 2048, 0}, {2112, 2112, 64}))
								  .toUtf8();
	const QString path = temp.filePath(QStringLiteral("framing.map"));
	QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(), "fixture")) {
		return 1;
	}
	file.close();
	const QVector<LevelMapSelectionRef> pillar {{LevelMapSelectionKind::QuakeBrush, 1}};
	const QPointF pillarTop(288, 160);
	const QPointF pillarFront(288, 128);
	const QPointF pillarSide(160, 128);

	for (const char* profile : {"netradiant-custom", "trenchbroom"}) {
		for (int scale : {100, 200}) {
			const bool nrc = QLatin1String(profile) == QLatin1String("netradiant-custom");
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Level plan framing UI " << phase.toUtf8().constData() << std::endl;
			settings.setSelectedEditorProfileId(QString::fromLatin1(profile));
			settings.setTextScalePercent(scale);
			settings.setLevelViewLinks({});
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
				int readAloudCount = 0;
				QObject::connect(action("accessibility.readAloud"), &QAction::triggered, &shell, [&readAloudCount] { ++readAloudCount; });
				action("shell.mode.levels")->trigger();
				action("map.layout.camera-and-plan")->trigger();
				auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
				if (!expect(camera && until([&] { return shell.levelDocument().sourcePath == path && camera->isEnabled() && camera->hasMesh(); }),
						"map and camera ready")) {
					return 1;
				}
				// The one plan pane the layout shows.
				MapViewport* plan = nullptr;
				for (MapViewport* view : shell.findChildren<MapViewport*>()) {
					plan = !plan && !view->QWidget::isHidden() ? view : plan;
				}
				if (!expect(plan != nullptr, "one plan pane shows")) {
					return 1;
				}
				const QByteArray bytes = serializeLevelMap(shell.levelDocument()).bytes;
				const auto select = [&](MapViewport* view, const QVector<LevelMapSelectionRef>& objects) {
					view->setSelectionSet(objects);
					view->selectionSetChanged(objects);
				};
				const auto focus = [&](QWidget* pane) {
					shell.activateWindow();
					expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
					pane->setFocus(Qt::OtherFocusReason);
					return expect(until([&] { return pane->hasFocus(); }), "owned pane receives semantic focus");
				};

				// The commands: labelled, in a menu, on the Tools tab and in
				// command search; TrenchBroom's Ctrl+Shift+U, no keys in NRC.
				expect(plan->controls().radiantFraming == nrc, "Radiant framing follows the profile");
				for (const char* id : {"map.zoomActualSize", "map.resetPlanViews"}) {
					QAction* command = action(id);
					if (!expect(command != nullptr, "the command exists", QString::fromLatin1(id))) {
						return 1;
					}
					expect(!command->text().isEmpty() && !command->statusTip().isEmpty(), "command text and status tip", QString::fromLatin1(id));
					bool inMenu = false;
					for (QObject* owner : command->associatedObjects()) {
						inMenu = inMenu || qobject_cast<QMenu*>(owner) != nullptr;
					}
					expect(inMenu, "the command is in a menu", QString::fromLatin1(id));
					auto* button = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-") + QString::fromLatin1(id));
					expect(button && !button->accessibleName().isEmpty() && button->focusPolicy() != Qt::NoFocus && QAccessible::queryAccessibleInterface(button),
						"the Tools tab offers an accessible keyboard button", QString::fromLatin1(id));
					QString kebab;
					for (const QChar character : QString::fromLatin1(id)) {
						kebab += character.isUpper() ? QStringLiteral("-") + character.toLower() : QString(character);
					}
					CommandPaletteEntry entry;
					expect(commandPaletteEntryForCommandId(kebab, &entry) && !entry.label.isEmpty(), "the command is in command search", kebab);
				}
				expect(action("map.zoomActualSize")->shortcuts().isEmpty(), "Zoom 100% has no key, as in NetRadiant Custom");
				// TrenchBroom's Ctrl+Shift+U for Reset 2D Cameras is Read Aloud's
				// key, which accessibility keeps: the slot stays empty.
				expect(action("map.resetPlanViews")->shortcuts().isEmpty()
						&& action("accessibility.readAloud")->shortcuts().contains(QKeySequence(QStringLiteral("Ctrl+Shift+U"))),
					"Reset 2D Views has no key and Read Aloud keeps Ctrl+Shift+U", action("map.resetPlanViews")->shortcut().toString());

				// One plan view: a change of plane keeps the zoom and centres on
				// the selection.
				action("map.viewTop")->trigger();
				select(plan, pillar);
				expect(plan->restoreNavigationState({0, {-300, 50}, 0.8}), "a plan view far from the pillar");
				action("map.viewFront")->trigger();
				expect(at(plan, MapViewportProjection::FrontXZ, pillarFront, 0.8), "Front View keeps the zoom and centres on the selection", describe(plan));
				action("map.nextProjection")->trigger();
				expect(at(plan, MapViewportProjection::SideZY, pillarSide, 0.8), "Next 2D View keeps the zoom and centres on the selection", describe(plan));
#ifdef VIBESTUDIO_HAVE_QTEST
				// NRC's Ctrl+Tab in the plan view steps the plane ahead of the
				// view's own Ctrl+Tab, which extends the selection elsewhere.
				if (focus(plan)) {
					QTest::keyClick(plan, Qt::Key_Tab, Qt::ControlModifier);
					if (nrc) {
						expect(at(plan, MapViewportProjection::TopXY, pillarTop, 0.8) && plan->selectionSet() == pillar,
							"Ctrl+Tab steps the plane in NetRadiant Custom and leaves the selection", describe(plan));
					} else {
						expect(plan->projection() == MapViewportProjection::SideZY && plan->selectionSet().size() == 2,
							"Ctrl+Tab extends the selection where the profile leaves it to the view", describe(plan));
					}
				}
#endif
				// Nothing selected: the camera.
				action("map.selectNone")->trigger();
				camera->setCameraView({100, -200, 64}, 0, 0);
				action("map.viewFront")->trigger();
				const double kept = plan->zoom();
				expect(at(plan, MapViewportProjection::FrontXZ, {100, 64}, kept) && near(kept, 0.8),
					"with nothing selected the new plane centres on the camera", describe(plan));
				expect(plan->applyLinkedNavigation({1, {999, 999}, 0.8}), "pan away");
				action("map.viewFront")->trigger();
				expect(at(plan, MapViewportProjection::FrontXZ, {100, 64}, 0.8), "the plane already shown recentres on the camera", describe(plan));

				// Zoom 100%: one pixel per unit about the view's centre.
				shell.statusBar()->clearMessage();
				expect(action("map.zoomActualSize")->isEnabled(), "Zoom 100% is available");
				action("map.zoomActualSize")->trigger();
				expect(at(plan, MapViewportProjection::FrontXZ, {100, 64}, 1.0) && !shell.statusBar()->currentMessage().isEmpty(), "Zoom 100% is announced",
					describe(plan) + QStringLiteral(" / ") + shell.statusBar()->currentMessage());

				// Frame Selection: NRC frames the 2D view only, with a third to
				// spare; TrenchBroom also turns the camera to the selection.
				select(plan, pillar);
				const ModelVec3 before = camera->cameraPosition();
				shell.statusBar()->clearMessage();
				action("map.frameSelection")->trigger();
				const auto moved = [&] {
					const ModelVec3 now = camera->cameraPosition();
					return std::abs(now.x - before.x) + std::abs(now.y - before.y) + std::abs(now.z - before.z) > 1.0e-3;
				};
				if (nrc) {
					PlanViewState expected;
					expect(levelPlanFocusState(1, pillarLo, pillarHi, QSizeF(plan->width(), plan->height()), &expected), "the expected focus");
					expect(at(plan, MapViewportProjection::FrontXZ, expected.center, expected.zoom) && !moved() && !shell.statusBar()->currentMessage().isEmpty(),
						"Radiant framing fits the pillar in two-thirds of the view and leaves the camera", describe(plan));
					action("map.selectNone")->trigger();
					action("map.frameSelection")->trigger();
					const double span = std::clamp(std::min(plan->width(), plan->height()) / 384.0, 0.0025, 64.0);
					expect(at(plan, MapViewportProjection::FrontXZ, {100, 64}, span) && !moved(), "with nothing selected it frames the camera's surroundings",
						describe(plan));
				} else {
					expect(plan->projection() == MapViewportProjection::FrontXZ && near(plan->navigationState().center.x(), pillarFront.x())
							&& near(plan->navigationState().center.y(), pillarFront.y()) && moved(),
						"TrenchBroom's framing centres the plan and turns the camera to the selection", describe(plan));
					action("map.selectNone")->trigger();
				}
				expect(!action("map.resetPlanViews")->isEnabled(), "a one-plan layout has no other 2D view to reset");

				// Reset 2D Views in the four-pane layout, from the focused pane.
				action("map.layout.four-views")->trigger();
				plan = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
				auto* front = shell.findChild<MapViewport*>(QStringLiteral("mapViewport1"));
				auto* side = shell.findChild<MapViewport*>(QStringLiteral("mapViewport2"));
				if (!expect(plan && front && side && until([&] { return plan->isVisible() && front->isVisible() && side->isVisible(); }), "three plan panes")) {
					return 1;
				}
				const auto spread = [&] {
					plan->applyLinkedNavigation({0, {10, 20}, 2});
					front->applyLinkedNavigation({1, {-40, 30}, 5});
					side->applyLinkedNavigation({2, {70, -80}, 0.5});
				};
				select(plan, {});
				focus(plan);
				expect(until([&] { return plan->isActivePane() && action("map.resetPlanViews")->isEnabled(); }), "the top pane is focused and can reset the others");
				spread();
				shell.statusBar()->clearMessage();
				action("map.resetPlanViews")->trigger();
				expect(at(plan, MapViewportProjection::TopXY, {10, 20}, 2) && at(front, MapViewportProjection::FrontXZ, {10, 30}, 2)
						&& at(side, MapViewportProjection::SideZY, {20, -80}, 2) && !shell.statusBar()->currentMessage().isEmpty(),
					"Reset 2D Views gives X to front and Y to side, each keeping its Z, and the zoom",
					describe(front) + QStringLiteral(" / ") + describe(side));
				// From the front pane: top takes X and side takes Z, each keeping Y.
				spread();
				focus(front);
				expect(until([&] { return front->isActivePane(); }), "the front pane is focused");
				action("map.resetPlanViews")->trigger();
				expect(at(plan, MapViewportProjection::TopXY, {-40, 20}, 5) && at(side, MapViewportProjection::SideZY, {70, 30}, 5)
						&& at(front, MapViewportProjection::FrontXZ, {-40, 30}, 5),
					"Reset 2D Views from the front pane gives X and Z, keeping Y", describe(plan) + QStringLiteral(" / ") + describe(side));
#ifdef VIBESTUDIO_HAVE_QTEST
				// Where Reset 2D Views can act, Ctrl+Shift+U still reads aloud.
				spread();
				if (focus(plan)) {
					const int before = readAloudCount;
					QTest::keyClick(plan, Qt::Key_U, Qt::ControlModifier | Qt::ShiftModifier);
					expect(readAloudCount == before + 1 && at(front, MapViewportProjection::FrontXZ, {-40, 30}, 5)
							&& at(side, MapViewportProjection::SideZY, {70, -80}, 0.5),
						"Ctrl+Shift+U in a 2D view reads aloud and leaves the other views", describe(front) + QStringLiteral(" / ") + describe(side));
				}
#endif
				action("map.linkPlanCenters")->trigger();
				expect(action("map.resetPlanViews")->isEnabled(), "linked centres alone still allow a reset");
				action("map.linkPlanZoom")->trigger();
				expect(!action("map.resetPlanViews")->isEnabled(), "centres and zoom both linked leave nothing to reset");
				action("map.linkPlanCenters")->trigger();
				action("map.linkPlanZoom")->trigger();
				expect(action("map.resetPlanViews")->isEnabled() && StudioSettings().levelViewLinks() == LevelViewLinks {}, "links off again");

				// Three plans: Top, Front and Side focus their pane; NRC also
				// centres every plan on the selection, keeping each zoom.
				spread();
				select(plan, pillar);
				action("map.viewFront")->trigger();
				expect(front->isActivePane(), "Front View focuses the front pane");
				if (nrc) {
					expect(at(plan, MapViewportProjection::TopXY, pillarTop, 2) && at(front, MapViewportProjection::FrontXZ, pillarFront, 5)
							&& at(side, MapViewportProjection::SideZY, pillarSide, 0.5),
						"NRC's split layout centres every plan on the selection",
						describe(plan) + QStringLiteral(" / ") + describe(front) + QStringLiteral(" / ") + describe(side));
				} else {
					expect(at(plan, MapViewportProjection::TopXY, {10, 20}, 2) && at(side, MapViewportProjection::SideZY, {70, -80}, 0.5),
						"TrenchBroom's split layout only moves the focus", describe(plan) + QStringLiteral(" / ") + describe(side));
				}
				// Zoom 100% reaches every plan pane.
				action("map.zoomActualSize")->trigger();
				expect(near(plan->zoom(), 1) && near(front->zoom(), 1) && near(side->zoom(), 1), "Zoom 100% sets every plan pane");
				expect(serializeLevelMap(shell.levelDocument()).bytes == bytes, "plan framing never changes the map");
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}

	// A Doom map: Draw Sector switches the one plan view to Top, keeping its
	// zoom and centring on the selected sector, as any change of plane does.
	settings.setSelectedEditorProfileId(QStringLiteral("vibestudio-default"));
	settings.setTextScalePercent(100);
	settings.setTheme(StudioTheme::Dark);
	settings.setLevelViewLayoutPreference(QStringLiteral("camera-and-plan"));
	settings.sync();
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	phase = QStringLiteral("Doom Draw Sector");
	std::cout << "Level plan framing UI " << phase.toUtf8().constData() << std::endl;
	const QString wadPath = temp.filePath(QStringLiteral("room.wad"));
	if (expect(tests::doom::write(wadPath, tests::doom::fixture()), "Doom fixture")) {
		ApplicationShell shell;
		shell.resize(1700, 1050);
		shell.show();
		shell.openPathFromCommandLine(wadPath);
		const auto action = [&](const char* id) { return shell.findChild<QAction*>(QString::fromLatin1(id)); };
		action("shell.mode.levels")->trigger();
		action("map.layout.camera-and-plan")->trigger();
		MapViewport* plan = nullptr;
		if (expect(until([&] {
				plan = nullptr;
				for (MapViewport* view : shell.findChildren<MapViewport*>()) {
					plan = !plan && !view->QWidget::isHidden() && view->hasDocument() ? view : plan;
				}
				return shell.levelDocument().format == LevelMapFormat::DoomWad && plan != nullptr;
			}), "Doom map in one plan view")) {
			action("map.viewFront")->trigger();
			const QVector<LevelMapSelectionRef> sector {{LevelMapSelectionKind::DoomSector, shell.levelDocument().doomSectors.first().id}};
			plan->setSelectionSet(sector);
			plan->selectionSetChanged(sector);
			LevelMapVec3 low;
			LevelMapVec3 high;
			expect(levelMapSelectionBounds(shell.levelDocument(), &low, &high), "the sector's bounds");
			expect(plan->applyLinkedNavigation({1, {5000, 5000}, 0.8}), "a front view far from the sector");
			auto* draw = action("map.drawSector");
			if (expect(draw && draw->isEnabled(), "Draw Sector is available")) {
				draw->trigger();
				expect(at(plan, MapViewportProjection::TopXY, {(low.x + high.x) / 2, (low.y + high.y) / 2}, 0.8),
					"Draw Sector's switch to Top keeps the zoom and centres on the selected sector", describe(plan));
				if (draw->isCheckable() && draw->isChecked()) {
					draw->trigger();
				}
			}
		}
	}

	// Read Aloud keeps Ctrl+Shift+U in every editor profile and every Levels
	// view, including a 2D view where Reset 2D Views could act.
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	settings.setLevelViewLinks({});
	settings.sync();
	phase = QStringLiteral("Read Aloud key");
	std::cout << "Level plan framing UI " << phase.toUtf8().constData() << std::endl;
	const QKeySequence readAloudKey(QStringLiteral("Ctrl+Shift+U"));
	{
		ApplicationShell shell;
		shell.resize(1700, 1050);
		shell.show();
		shell.openPathFromCommandLine(path);
		const auto action = [&](const char* id) { return shell.findChild<QAction*>(QString::fromLatin1(id)); };
		action("shell.mode.levels")->trigger();
		auto* combo = shell.findChild<QComboBox*>(QStringLiteral("editorProfileCombo"));
		auto* readAloud = action("accessibility.readAloud");
		auto* reset = action("map.resetPlanViews");
		auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
		auto* top = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
		auto* front = shell.findChild<MapViewport*>(QStringLiteral("mapViewport1"));
		auto* side = shell.findChild<MapViewport*>(QStringLiteral("mapViewport2"));
		if (expect(combo && readAloud && reset && camera && top && front && side
					&& until([&] { return shell.levelDocument().sourcePath == path && camera->isEnabled() && camera->hasMesh(); }),
				"map, views and profile list ready")) {
			int readCount = 0;
			int resetCount = 0;
			QObject::connect(readAloud, &QAction::triggered, &shell, [&readCount] { ++readCount; });
			QObject::connect(reset, &QAction::triggered, &shell, [&resetCount] { ++resetCount; });
			const qsizetype spokenBefore = loggedSpeechForTesting().size();
			for (const EditorProfileDescriptor& profile : editorProfileDescriptors()) {
				const int index = combo->findData(profile.id);
				if (!expect(index >= 0, "the profile is listed", profile.id)) {
					continue;
				}
				combo->setCurrentIndex(index);
				action("map.layout.four-views")->trigger();
				expect(until([&] { return top->isVisible() && front->isVisible() && side->isVisible() && camera->isVisible() && reset->isEnabled(); }),
					"four views where Reset 2D Views can act", profile.id);
				expect(readAloud->shortcuts().contains(readAloudKey), "Read Aloud has Ctrl+Shift+U", profile.id);
				for (QAction* other : shell.findChildren<QAction*>()) {
					expect(other == readAloud || !other->shortcuts().contains(readAloudKey), "no other command takes Ctrl+Shift+U",
						profile.id + QLatin1Char(' ') + other->objectName());
				}
#ifdef VIBESTUDIO_HAVE_QTEST
				for (QWidget* view : std::initializer_list<QWidget*> {top, front, side, camera}) {
					shell.activateWindow();
					expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
					view->setFocus(Qt::OtherFocusReason);
					if (!expect(until([&] { return view->hasFocus(); }), "the view takes focus", profile.id + QLatin1Char(' ') + view->objectName())) {
						continue;
					}
					const int reads = readCount;
					const int resets = resetCount;
					QTest::keyClick(view, Qt::Key_U, Qt::ControlModifier | Qt::ShiftModifier);
					expect(readCount == reads + 1 && resetCount == resets, "Ctrl+Shift+U reaches Read Aloud",
						profile.id + QLatin1Char(' ') + view->objectName());
				}
#endif
			}
#ifdef VIBESTUDIO_HAVE_QTEST
			expect(loggedSpeechForTesting().size() > spokenBefore, "Read Aloud spoke (to the log)");
#endif
		}
	}

	// A user who gives Reset 2D Views Ctrl+Shift+U opts in: a 2D view where it
	// can act takes the key; everywhere else it still reads aloud.
	settings.setSelectedEditorProfileId(QStringLiteral("trenchbroom"));
	settings.setUserShortcuts({{QStringLiteral("map.resetPlanViews"), {QStringLiteral("Ctrl+Shift+U")}}});
	settings.sync();
	phase = QStringLiteral("Reset 2D Views opt-in");
	std::cout << "Level plan framing UI " << phase.toUtf8().constData() << std::endl;
	{
		ApplicationShell shell;
		shell.resize(1700, 1050);
		shell.show();
		shell.openPathFromCommandLine(path);
		const auto action = [&](const char* id) { return shell.findChild<QAction*>(QString::fromLatin1(id)); };
		action("shell.mode.levels")->trigger();
		action("map.layout.four-views")->trigger();
		auto* readAloud = action("accessibility.readAloud");
		auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
		auto* top = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
		auto* front = shell.findChild<MapViewport*>(QStringLiteral("mapViewport1"));
		if (expect(readAloud && camera && top && front
					&& until([&] { return shell.levelDocument().sourcePath == path && camera->isEnabled() && top->isVisible() && action("map.resetPlanViews")->isEnabled(); }),
				"opt-in shell ready")) {
			expect(action("map.resetPlanViews")->shortcuts().contains(readAloudKey) && readAloud->shortcuts().contains(readAloudKey),
				"the user's key and Read Aloud share Ctrl+Shift+U");
#ifdef VIBESTUDIO_HAVE_QTEST
			int readCount = 0;
			QObject::connect(readAloud, &QAction::triggered, &shell, [&readCount] { ++readCount; });
			top->applyLinkedNavigation({0, {10, 20}, 2});
			front->applyLinkedNavigation({1, {-40, 30}, 5});
			shell.activateWindow();
			expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
			top->setFocus(Qt::OtherFocusReason);
			if (expect(until([&] { return top->hasFocus() && top->isActivePane(); }), "top pane focus")) {
				QTest::keyClick(top, Qt::Key_U, Qt::ControlModifier | Qt::ShiftModifier);
				expect(at(front, MapViewportProjection::FrontXZ, {10, 30}, 2) && readCount == 0, "the opted-in key resets the other 2D views",
					describe(front));
			}
			camera->setFocus(Qt::OtherFocusReason);
			if (expect(until([&] { return camera->hasFocus(); }), "camera focus")) {
				QTest::keyClick(camera, Qt::Key_U, Qt::ControlModifier | Qt::ShiftModifier);
				expect(readCount == 1, "in the camera the key still reads aloud");
			}
#endif
		}
	}
	settings.setUserShortcuts({});
	settings.sync();
	if (ok) {
		std::cout << "Level plan framing UI: projection changes, Ctrl+Tab, Zoom 100%, Frame Selection, Reset 2D Views, the split layout, "
					 "Draw Sector's switch to Top and Read Aloud's key in every profile passed."
				  << std::endl;
	}
	return ok ? 0 : 1;
}
