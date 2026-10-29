// NetRadiant Custom's drag-tool gestures and the Build tool in the shell
// (app/level_plane_drag_actions.cpp, map_viewport_plane_drag.cpp,
// model_viewport_plane_drag.cpp): the Build Tool command, tool bar button,
// Tools tile, palette entry and NetRadiant Custom's B; the profile's gesture
// table; in NetRadiant Custom, a real QE4 drag beside the selection that moves
// the brush plane facing the press, an Alt vertex drag and a Ctrl+Alt
// extrusion in the Top view and an Alt plane drag in the camera, each equal
// to the core service in one undo step; in every profile, the Build tool's
// face drag, its Shift chain recorded for Repeat Last Commands, its click
// that selects, Escape, refusal of locked selections and the return to
// Select. NetRadiant Custom, TrenchBroom and VibeStudio profiles at 100%, and
// at 200% with high contrast, expanded translations and right-to-left layout.
#include "app/application_shell.h"
#include "app/level_plane_drag_controller.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_document.h"
#include "core/level_plane_drag.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/studio_semantics.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>

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
bool until(const std::function<bool()>& ready, int milliseconds = 30000)
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
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, '~'));
	}
};
QString text(const LevelMapVec3& v) { return QStringLiteral("%1 %2 %3").arg(v.x).arg(v.y).arg(v.z); }
LevelMapSelectionRef brush(int id) { return {LevelMapSelectionKind::QuakeBrush, id}; }
void mouse(QWidget* widget, QEvent::Type type, const QPointF& point, Qt::MouseButton button, Qt::MouseButtons buttons, Qt::KeyboardModifiers keys = Qt::NoModifier)
{
	QMouseEvent event(type, point, widget->mapToGlobal(point), button, buttons, keys);
	QApplication::sendEvent(widget, &event);
}
// A left drag from `from` to `to` in steps, with `keys` held throughout.
void drag(QWidget* widget, const QPointF& from, const QPointF& to, Qt::KeyboardModifiers keys = Qt::NoModifier)
{
	mouse(widget, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, keys);
	for (int step = 1; step <= 4; ++step) {
		mouse(widget, QEvent::MouseMove, from + (to - from) * (step / 4.0), Qt::NoButton, Qt::LeftButton, keys);
	}
	mouse(widget, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, keys);
}
bool solvedBox(const LevelMapDocument& map, int id, LevelMapVec3* mins, LevelMapVec3* maxs)
{
	for (const auto& b : map.brushes) {
		LevelBrushTopology topology;
		if (b.id == id && levelBrushTopology(b, &topology)) {
			*mins = topology.geometry.mins;
			*maxs = topology.geometry.maxs;
			return true;
		}
	}
	return false;
}
} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) { return 1; }
	// Brushes 0 (0..32 cubed) and 1 (64..96) are free; brush 2 sits on a locked layer.
	LevelMapDocument created;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake");
	create.starterRoom = false;
	QString error;
	QString layer;
	if (!expect(createLevelMap(create, &created, &error)
			&& addLevelMapBoxBrush(&created, {0, 0, 0, true}, {32, 32, 32, true}, "studio/checker", nullptr, &error)
			&& addLevelMapBoxBrush(&created, {64, 0, 0, true}, {96, 32, 32, true}, "studio/checker", nullptr, &error)
			&& addLevelMapBoxBrush(&created, {128, 0, 0, true}, {160, 32, 32, true}, "studio/checker", nullptr, &error)
			&& createLevelSceneNode(&created, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
			&& assignLevelSceneObjects(&created, layer, {QStringLiteral("brush:2")}, &error) && setLevelSceneLocked(&created, layer, true, &error),
			"fixture", error)) {
		return 1;
	}
	const auto path = temp.filePath("drag.map");
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(serializeLevelMap(created).bytes) <= 0) { return 1; }
	file.close();

	for (const auto* profile : {"netradiant-custom", "trenchbroom", "vibestudio-default"}) {
		const bool nrc = QLatin1String(profile) == QLatin1String("netradiant-custom");
		for (int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Plane drag UI " << phase.toUtf8().constData() << std::endl;
			// Fresh settings for every combination.
			StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings-%1-%2.ini").arg(QLatin1String(profile)).arg(scale)));
			StudioSettings settings;
			settings.setRestoreSession(false);
			settings.setReducedMotion(true);
			settings.setLevelViewLayoutPreference("four-views");
			settings.setSelectedEditorProfileId(profile);
			settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
			settings.setLevelTextureLock(true);
			settings.sync();
			applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, scale));
			Expanded expanded;
			if (scale == 200) {
				app.installTranslator(&expanded);
				app.setLayoutDirection(Qt::RightToLeft);
			}
			{
				ApplicationShell shell;
				shell.resize(1600, 1000);
				shell.show();
				shell.openPathFromCommandLine(path);
				const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
				action(QStringLiteral("shell.mode.levels"))->trigger();
				action(QStringLiteral("map.layout.four-views"))->trigger();
				if (!expect(until([&] { return shell.levelDocument().brushes.size() == 3 && shell.findChildren<MapViewport*>().size() >= 3; }), "map ready")) {
					return 1;
				}
				QHash<int, MapViewport*> panes;
				for (auto* pane : shell.findChildren<MapViewport*>()) {
					if (!panes.contains(static_cast<int>(pane->projection()))) { panes.insert(static_cast<int>(pane->projection()), pane); }
				}
				auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
				auto* grid = shell.findChild<QComboBox*>("levelMapGrid");
				auto* snap = shell.findChild<QCheckBox*>("levelMapSnap");
				auto* undo = action(QStringLiteral("map.undo"));
				auto* build = action(QStringLiteral("map.buildTool"));
				if (!expect(panes.size() == 3 && camera && grid && snap && undo && build, "panes, camera, grid, snap and the command")) { return 1; }
				grid->setCurrentIndex(grid->findData(16));
				snap->setChecked(true);
				auto* top = panes.value(0);
				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				const auto pick = [&](MapViewport* pane, const QVector<LevelMapSelectionRef>& refs) {
					pane->setSelectionSet(refs);
					pane->selectionSetChanged(refs);
				};
				const auto reference = [&](const QVector<LevelMapSelectionRef>& selection) {
					LevelMapDocument expected;
					QString problem;
					expect(loadLevelMap({path, {}, {}}, &expected, &problem), "reference map", problem);
					setLevelMapSelection(&expected, selection);
					return expected;
				};
				std::optional<LevelPlaneDragRequest> committed;
				std::optional<QPair<LevelMapVec3, int>> chained;
				for (auto* controller : {top->planeDrag(), camera->planeDrag()}) {
					QObject::connect(controller, &LevelPlaneDragController::commitRequested, &shell, [&](const LevelPlaneDragRequest& request) { committed = request; });
					QObject::connect(controller, &LevelPlaneDragController::chainRequested, &shell,
						[&](const LevelMapVec3& step, int copies) { chained = QPair<LevelMapVec3, int> {step, copies}; });
				}
				const auto baseline = bytes();
				const auto depth = shell.levelDocument().undoStack.size();
				const auto plane = [&](double x, double y) { return top->viewPointFor(x, y); };

				// Registration: command, menu, Tools tile, palette, tool bar
				// button and the profile's key.
				bool menu = false;
				for (auto* owner : build->associatedObjects()) { menu |= qobject_cast<QMenu*>(owner) != nullptr; }
				CommandPaletteEntry entry;
				expect(build->isCheckable() && build->isEnabled() && !build->text().isEmpty() && !build->statusTip().isEmpty() && menu
						&& commandPaletteEntryForCommandId(QStringLiteral("map.buildTool"), &entry) && !entry.label.isEmpty(),
					"Build Tool command, menu and palette");
				auto* row = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-map.buildTool"));
				expect(row && row->focusPolicy() != Qt::NoFocus && !row->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(row),
					"accessible Tools tile");
				auto* button = shell.findChild<QToolButton*>(QStringLiteral("levelToolButton-build"));
				expect(button && action(QStringLiteral("levelTool-build"))->isVisible() && !button->accessibleName().isEmpty()
						&& !button->accessibleDescription().isEmpty() && button->focusPolicy() != Qt::NoFocus,
					"accessible tool bar button");
				expect(nrc ? build->shortcuts() == QList<QKeySequence> {QKeySequence(Qt::Key_B)} : build->shortcuts().isEmpty(),
					"NetRadiant Custom binds B; the others leave it to the tool bar", QKeySequence::listToString(build->shortcuts()));
				// The gesture table follows the profile.
				expect(nrc == (top->controls().emptyDragWithSelection == PlanEmptyDrag::DragPlanes)
						&& nrc == (top->controls().vertexDragModifiers == Qt::AltModifier)
						&& nrc == (top->controls().extrudeDragModifiers == (Qt::ControlModifier | Qt::AltModifier)),
					"the 2D gestures are NetRadiant Custom's there");

				if (nrc) {
					// The QE4 drag beside brush 0 moves its +X plane, by real events.
					pick(top, {brush(0)});
					committed.reset();
					drag(top, plane(48, 16), plane(80, 16));
					expect(until([&] { return bytes() != baseline; }) && committed && committed->kind == LevelPlaneDragKind::FacingPlanes
							&& committed->translation.x == 32 && committed->translation.y == 0,
						"a drag beside the selection moves the plane facing it", committed ? text(committed->translation) : QString());
					if (committed) {
						auto expected = reference({brush(0)});
						LevelPlaneDragReport report;
						expect(applyLevelPlaneDrag(&expected, *committed, &report, &error) && bytes() == serializeLevelMap(expected).bytes
								&& shell.levelDocument().undoStack.size() == depth + 1,
							"the drag equals the core service in one undo step", error);
						LevelMapVec3 mins, maxs;
						expect(solvedBox(shell.levelDocument(), 0, &mins, &maxs) && maxs.x == 64 && mins.x == 0, "the side follows the pointer", text(maxs));
					}
					undo->trigger();
					expect(bytes() == baseline, "undo restores the map exactly");

					// Alt drags the vertices under the pointer.
					committed.reset();
					drag(top, plane(32, 32), plane(48, 32), Qt::AltModifier);
					expect(until([&] { return bytes() != baseline; }) && committed && committed->kind == LevelPlaneDragKind::Vertices
							&& committed->sets.size() == 1 && committed->sets[0].components.size() == 2,
						"Alt drags the stacked corner", committed ? text(committed->translation) : QString());
					if (committed) {
						auto expected = reference({brush(0)});
						LevelPlaneDragReport report;
						expect(applyLevelPlaneDrag(&expected, *committed, &report, &error) && bytes() == serializeLevelMap(expected).bytes, "the vertex drag equals the core",
							error);
					}
					undo->trigger();
					expect(bytes() == baseline, "the vertex drag undoes");

					// Ctrl+Alt extrudes the side behind the nearest edge.
					committed.reset();
					drag(top, plane(40, 16), plane(72, 16), Qt::ControlModifier | Qt::AltModifier);
					expect(until([&] { return shell.levelDocument().brushes.size() == 4; }) && committed && committed->kind == LevelPlaneDragKind::Extrude
							&& committed->distance == 32,
						"Ctrl+Alt extrudes a new brush", committed ? QString::number(committed->distance) : QString());
					if (committed) {
						auto expected = reference({brush(0)});
						LevelPlaneDragReport report;
						expect(applyLevelPlaneDrag(&expected, *committed, &report, &error) && bytes() == serializeLevelMap(expected).bytes
								&& shell.levelDocument().undoStack.size() == depth + 1,
							"the extrusion equals the core in one undo step", error);
					}
					undo->trigger();
					expect(bytes() == baseline, "the extrusion undoes");

					// Escape throws a drag away.
					mouse(top, QEvent::MouseButtonPress, plane(48, 16), Qt::LeftButton, Qt::LeftButton);
					mouse(top, QEvent::MouseMove, plane(64, 16), Qt::NoButton, Qt::LeftButton);
					mouse(top, QEvent::MouseMove, plane(80, 16), Qt::NoButton, Qt::LeftButton);
					expect(top->planeDrag()->active() && top->accessibleDescription().contains(top->planeDrag()->summary()), "the drag describes itself");
					top->setFocus(Qt::OtherFocusReason);
					QTest::keyClick(top, Qt::Key_Escape);
					mouse(top, QEvent::MouseButtonRelease, plane(80, 16), Qt::LeftButton, Qt::NoButton);
					expect(!top->planeDrag()->active() && bytes() == baseline, "Escape cancels without an edit");

					// The camera's Alt drag of the plane under the pointer.
					camera->setCameraView({16, -120, 140}, 90, -38);
					if (expect(until([&] { return camera->isEnabled(); }) && tests::settleModelViewport(*camera), "camera ready")) {
						const QPointF onTop = camera->componentGizmoFrame({16, 16, 32, true}).project({16, 16, 32, true});
						expect(std::isfinite(onTop.x()) && QRectF(camera->rect()).contains(onTop), "brush 0's top is in view",
							QStringLiteral("%1,%2").arg(onTop.x()).arg(onTop.y()));
						committed.reset();
						drag(camera, onTop, onTop + QPointF(0, -60), Qt::AltModifier);
						expect(until([&] { return bytes() != baseline; }) && committed && committed->kind == LevelPlaneDragKind::Plane
								&& committed->translation.z > 0 && committed->translation.x == 0 && committed->translation.y == 0,
							"Alt in the camera lifts the top plane", committed ? text(committed->translation) : QStringLiteral("no request"));
						if (committed) {
							auto expected = reference({brush(0)});
							LevelPlaneDragReport report;
							expect(applyLevelPlaneDrag(&expected, *committed, &report, &error) && bytes() == serializeLevelMap(expected).bytes,
								"the camera drag equals the core", error);
							undo->trigger();
						}
						expect(bytes() == baseline, "the camera drag undoes");
					}

					// A locked brush in the selection: nothing moves.
					pick(top, {brush(1), brush(2)});
					drag(top, plane(176, 16), plane(208, 16));
					expect(bytes() == baseline, "a locked selection does not drag");
				}

				// The Build tool: a face drag extrudes, Shift lays a chain.
				pick(top, {brush(0)});
				build->trigger();
				const auto handles = camera->selectionResizeHandles();
				expect(build->isChecked() && action(QStringLiteral("levelTool-build"))->isChecked() && top->buildMode() && camera->buildMode()
						&& std::none_of(handles.cbegin(), handles.cend(), [](const QPointF& p) { return std::isfinite(p.x()); }),
					"Build tool on in every view, the camera's resize box gone");
				committed.reset();
				drag(top, plane(40, 16), plane(72, 16));
				expect(until([&] { return shell.levelDocument().brushes.size() == 4; }) && committed && committed->kind == LevelPlaneDragKind::Extrude,
					"a Build drag beside a face extrudes it");
				if (committed) {
					auto expected = reference({brush(0)});
					LevelPlaneDragReport report;
					expect(applyLevelPlaneDrag(&expected, *committed, &report, &error) && bytes() == serializeLevelMap(expected).bytes, "the Build extrusion equals the core",
						error);
				}
				undo->trigger();
				expect(until([&] { return bytes() == baseline; }), "the Build extrusion undoes");
				pick(top, {brush(0)});
				chained.reset();
				// TrenchBroom records selection changes in the history too.
				const auto chainDepth = shell.levelDocument().undoStack.size();
				drag(top, plane(16, 16), plane(96, 16), Qt::ShiftModifier);
				expect(until([&] { return shell.levelDocument().brushes.size() == 6; }) && chained && chained->second == 3 && chained->first.x == 32
						&& shell.levelDocument().undoStack.size() == chainDepth + 1,
					"Shift and a drag lay three copies in one undo step", chained ? text(chained->first) : QString());
				if (chained) {
					auto expected = reference({brush(0)});
					expect(arrayLevelMapSelection(&expected, chained->first, chained->second, {true, false}, &error) && bytes() == serializeLevelMap(expected).bytes,
						"the chain equals the array placement", error);
				}
				expect(shell.levelDocument().repeat.actions.size() == 3, "the chain is recorded for Repeat");
				// Only the last copy stays selected, so a repeat carries the
				// chain on from it, one length at a time, nothing stacked.
				const auto chainStarts = [&] {
					QVector<double> starts;
					for (const auto& b : shell.levelDocument().brushes) {
						LevelMapVec3 mins, maxs;
						if (b.id > 2 && solvedBox(shell.levelDocument(), b.id, &mins, &maxs)) { starts << mins.x; }
					}
					std::sort(starts.begin(), starts.end());
					return starts;
				};
				const auto& chainSelection = shell.levelDocument().selection;
				LevelMapVec3 lastMins, lastMaxs;
				expect(chainSelection.size() == 1 && solvedBox(shell.levelDocument(), chainSelection[0].objectId, &lastMins, &lastMaxs) && lastMins.x == 96
						&& chainStarts() == QVector<double> {32, 64, 96},
					"the copies sit one length apart and only the last stays selected");
				action(QStringLiteral("map.repeat"))->trigger();
				expect(until([&] { return shell.levelDocument().brushes.size() == 9; }) && chainStarts() == QVector<double> {32, 64, 96, 128, 160, 192},
					"Repeat Last Commands carries the chain on without stacking", QString::number(shell.levelDocument().brushes.size()));
				undo->trigger();
				undo->trigger();
				expect(until([&] { return bytes() == baseline; }), "the chain undoes");
				// A click selects; Escape cancels a drag.
				pick(top, {brush(0)});
				mouse(top, QEvent::MouseButtonPress, plane(80, 16), Qt::LeftButton, Qt::LeftButton);
				mouse(top, QEvent::MouseButtonRelease, plane(80, 16), Qt::LeftButton, Qt::NoButton);
				expect(until([&] { return shell.levelDocument().selection == QVector<LevelMapSelectionRef> {brush(1)}; }) && bytes() == baseline,
					"a Build click selects what is under it");
				pick(top, {brush(0)});
				mouse(top, QEvent::MouseButtonPress, plane(40, 16), Qt::LeftButton, Qt::LeftButton);
				mouse(top, QEvent::MouseMove, plane(56, 16), Qt::NoButton, Qt::LeftButton);
				mouse(top, QEvent::MouseMove, plane(72, 16), Qt::NoButton, Qt::LeftButton);
				top->setFocus(Qt::OtherFocusReason);
				QTest::keyClick(top, Qt::Key_Escape);
				mouse(top, QEvent::MouseButtonRelease, plane(72, 16), Qt::LeftButton, Qt::NoButton);
				expect(!top->planeDrag()->active() && bytes() == baseline, "Escape cancels a Build drag");
				// A locked brush: refused with its reason, nothing moves.
				pick(top, {brush(2)});
				drag(top, plane(168, 16), plane(200, 16));
				expect(bytes() == baseline && !top->planeDrag()->lastError().isEmpty(), "a locked brush is refused", top->planeDrag()->lastError());
				// Choosing it again returns to Select.
				build->trigger();
				expect(!build->isChecked() && !top->buildMode() && action(QStringLiteral("levelTool-select"))->isChecked(), "choosing it again returns to Select");
				if (nrc) {
					shell.activateWindow();
					top->setFocus(Qt::OtherFocusReason);
					expect(until([&] { return top->hasFocus(); }), "plan view focused");
					QTest::keyClick(top, Qt::Key_B);
					expect(build->isChecked(), "B turns the Build tool on");
					QTest::keyClick(top, Qt::Key_B);
					expect(!build->isChecked(), "B again returns to Select");
				}
				expect(bytes() == baseline, "nothing left behind");
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}
	return ok ? 0 : 1;
}
