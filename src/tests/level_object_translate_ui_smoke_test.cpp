// The Levels Translate tool in the shell (app/level_object_gizmo_actions.cpp,
// map_viewport_object_gizmo.cpp, model_viewport_object_gizmo.cpp): the tool
// bar button, command, Tools tile, palette entry and NetRadiant Custom's W;
// X, Y and Z arrows, the view-plane square and the shared pivot in the plan
// views and the camera; a drag that equals the core service byte for byte
// in one undo step; Escape; a placed pivot shared by every view; refusal of
// locked selections. NetRadiant Custom, TrenchBroom and VibeStudio profiles at
// 100%, and at 200% with high contrast, expanded translations and
// right-to-left layout. Owned offscreen widgets, Qt Test events and the
// views' semantic manipulator calls.
#include "app/application_shell.h"
#include "app/level_component_gizmo.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_document.h"
#include "core/level_object_manipulator.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/studio_semantics.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
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

#include <cmath>
#include <iostream>

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
bool at(const LevelMapVec3& v, double x, double y, double z) { return v.valid && std::abs(v.x - x) < 1e-6 && std::abs(v.y - y) < 1e-6 && std::abs(v.z - z) < 1e-6; }
LevelMapSelectionRef brush(int id) { return {LevelMapSelectionKind::QuakeBrush, id}; }
QPointF handlePoint(LevelComponentGizmo* gizmo, const LevelComponentGizmoFrame& frame, int id, bool label = true)
{
	for (const auto& handle : gizmo->handles(frame)) {
		if (handle.id == id) {
			// Arrows: their label; the square: a corner inside it; the pivot: its centre.
			return id <= 2 ? (label ? handle.label : (handle.points[0] + handle.points[1]) * 0.5) : id == 3 ? handle.label + QPointF(9, 9) : handle.label;
		}
	}
	return {std::nan(""), std::nan("")};
}
bool finitePoint(const QPointF& p) { return std::isfinite(p.x()) && std::isfinite(p.y()); }
void mouse(QWidget* widget, QEvent::Type type, const QPointF& point, Qt::MouseButton button, Qt::MouseButtons buttons)
{
	QMouseEvent event(type, point, widget->mapToGlobal(point), button, buttons, Qt::NoModifier);
	QApplication::sendEvent(widget, &event);
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
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference("four-views");
	// Brushes 0 and 1 are free; brush 2 sits on a locked layer.
	LevelMapDocument created;
	LevelMapCreateRequest create;
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
	const auto path = temp.filePath("translate.map");
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(serializeLevelMap(created).bytes) <= 0) { return 1; }
	file.close();

	for (const auto* profile : {"netradiant-custom", "trenchbroom", "vibestudio-default"}) {
		const bool nrc = QLatin1String(profile) == QLatin1String("netradiant-custom");
		for (int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Translate tool UI " << phase.toUtf8().constData() << std::endl;
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
				auto* translate = action(QStringLiteral("map.translateTool"));
				if (!expect(panes.size() == 3 && camera && grid && snap && undo && translate, "panes, camera, grid, snap and the command")) { return 1; }
				grid->setCurrentIndex(grid->findData(16));
				snap->setChecked(true);
				auto* top = panes.value(0);
				auto* front = panes.value(1);
				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				const auto pick = [&](MapViewport* pane, const QVector<LevelMapSelectionRef>& refs) {
					pane->setSelectionSet(refs);
					pane->selectionSetChanged(refs);
				};
				const auto baseline = bytes();

				// Registration: command, menus, Tools tile, palette, tool bar
				// button and the profile's key.
				bool menu = false;
				for (auto* owner : translate->associatedObjects()) { menu |= qobject_cast<QMenu*>(owner) != nullptr; }
				CommandPaletteEntry entry;
				expect(translate->isCheckable() && translate->isEnabled() && !translate->text().isEmpty() && !translate->statusTip().isEmpty() && menu
						&& commandPaletteEntryForCommandId(QStringLiteral("map.translateTool"), &entry) && !entry.label.isEmpty(),
					"Translate Tool command, menu and palette");
				for (const char* id : {"map.translateTool", "map.setObjectPivot", "map.resetObjectPivot"}) {
					auto* row = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-") + QLatin1String(id));
					expect(row && row->focusPolicy() != Qt::NoFocus && !row->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(row)
							&& commandPaletteEntryForCommandId(QLatin1String(id)),
						"accessible Tools tile and palette entry", QLatin1String(id));
				}
				auto* button = shell.findChild<QToolButton*>(QStringLiteral("levelToolButton-translate"));
				expect(button && action(QStringLiteral("levelTool-translate"))->isVisible() && !button->accessibleName().isEmpty()
						&& !button->accessibleDescription().isEmpty() && button->focusPolicy() != Qt::NoFocus,
					"accessible tool bar button");
				expect(nrc ? translate->shortcuts() == QList<QKeySequence> {QKeySequence(Qt::Key_W)} : translate->shortcuts().isEmpty(),
					"NetRadiant Custom binds W; the others leave it to the tool bar", QKeySequence::listToString(translate->shortcuts()));

				// Choosing the tool with nothing selected shows no manipulator.
				pick(top, {});
				translate->trigger();
				auto* gizmo = top->objectGizmo();
				expect(translate->isChecked() && gizmo->tool() == LevelComponentTool::Translate && !gizmo->available()
						&& action(QStringLiteral("levelTool-translate"))->isChecked(),
					"tool on, nothing to manipulate");

				// Two brushes: arrows, square and pivot in the plans and the camera.
				pick(top, {brush(0), brush(1)});
				if (!expect(until([&] { return gizmo->available() && camera->objectGizmo()->available(); }), "manipulator available")) { return 1; }
				// TrenchBroom records selection changes in the history too.
				const auto depth = shell.levelDocument().undoStack.size();
				expect(at(gizmo->pivot(), 48, 16, 16) && at(front->objectGizmo()->pivot(), 48, 16, 16) && at(camera->objectGizmo()->pivot(), 48, 16, 16),
					"the pivot is the selection's centre in every view", text(gizmo->pivot()));
				expect(!top->hasResizeHandles(), "the Translate tool replaces the resize handles");
				const auto topFrame = top->componentGizmoFrame();
				QSet<int> ids;
				for (const auto& handle : gizmo->handles(topFrame)) { ids.insert(handle.id); }
				expect(ids == QSet<int> {0, 1, 3, 4}, "the Top view shows X, Y, the square and the pivot; Z points at the viewer");
				expect(top->accessibleDescription().contains(gizmo->summary()) && !gizmo->summary().isEmpty(), "the view describes the tool");

				// A real drag on the X arrow: preview first, then one undo step
				// that equals the core service.
				const QPointF xHandle = handlePoint(gizmo, topFrame, 0);
				if (expect(finitePoint(xHandle) && gizmo->hitTest(xHandle, topFrame, top->font()) == 0, "X arrow under its label")) {
					mouse(top, QEvent::MouseButtonPress, xHandle, Qt::LeftButton, Qt::LeftButton);
					mouse(top, QEvent::MouseMove, xHandle + QPointF(30, 0), Qt::NoButton, Qt::LeftButton);
					mouse(top, QEvent::MouseMove, xHandle + QPointF(70, 6), Qt::NoButton, Qt::LeftButton);
					const LevelMapVec3 preview = gizmo->translation();
					expect(gizmo->active() && preview.x > 0 && preview.y == 0 && preview.z == 0 && std::fmod(preview.x, 16) == 0 && bytes() == baseline,
						"the drag previews an X move on the grid without editing", text(preview));
					mouse(top, QEvent::MouseButtonRelease, xHandle + QPointF(70, 6), Qt::LeftButton, Qt::NoButton);
					expect(until([&] { return bytes() != baseline; }), "release applies the move");
					LevelMapDocument expected;
					expect(loadLevelMap({path, {}, {}}, &expected, &error), "reference map", error);
					setLevelMapSelection(&expected, {brush(0), brush(1)});
					LevelObjectTranslateRequest request;
					request.delta = gizmo->request().delta;
					request.textures = {true, false};
					LevelObjectTranslateReport report;
					expect(translateLevelMapObjects(&expected, request, &report, &error), "core reference", error);
					expect(bytes() == serializeLevelMap(expected).bytes && shell.levelDocument().undoStack.size() == depth + 1, "the drag equals the core service in one undo step");
					expect(at(gizmo->pivot(), 48 + request.delta.x, 16, 16), "the manipulator follows the moved selection", text(gizmo->pivot()));
					undo->trigger();
					expect(bytes() == baseline, "undo restores the map exactly");
				}

				// Escape throws a drag away.
				const QPointF yHandle = handlePoint(gizmo, top->componentGizmoFrame(), 1);
				if (expect(finitePoint(yHandle) && top->beginObjectTransform(yHandle), "Y arrow drag begins")) {
					gizmo->updateDrag(yHandle + QPointF(0, -80));
					expect(gizmo->translation().y > 0 && gizmo->translation().x == 0, "Y moves up the Top view", text(gizmo->translation()));
					top->setFocus(Qt::OtherFocusReason);
					QTest::keyClick(top, Qt::Key_Escape);
					expect(!gizmo->active() && bytes() == baseline && shell.levelDocument().undoStack.size() == depth, "Escape cancels without an edit");
				}

				// The square moves in the view plane; Shift keeps the main axis.
				const auto frontFrame = front->componentGizmoFrame();
				const QPointF square = handlePoint(front->objectGizmo(), frontFrame, 3);
				if (expect(finitePoint(square) && front->objectGizmo()->hitTest(square, frontFrame, front->font()) == 3 && front->beginObjectTransform(square),
						"Front square drag begins")) {
					front->objectGizmo()->updateDrag(square + QPointF(160, -100), Qt::NoModifier);
					const auto free = front->objectGizmo()->translation();
					front->objectGizmo()->updateModifiers(Qt::ShiftModifier);
					const auto axis = front->objectGizmo()->translation();
					expect(free.x > 0 && free.z > 0 && free.y == 0 && axis.x == free.x && axis.z == 0, "free X/Z move, then Shift keeps X",
						text(free) + QStringLiteral(" / ") + text(axis));
					front->objectGizmo()->finish(true);
					expect(until([&] { return bytes() != baseline; }) && shell.levelDocument().undoStack.size() == depth + 1, "square drag applies once");
					undo->trigger();
					expect(bytes() == baseline, "square drag undoes");
				}

				// The pivot: drag it (snapping to the bounds or grid), share it,
				// click it to follow the centre again.
				const QPointF pivotPoint = handlePoint(gizmo, top->componentGizmoFrame(), 4);
				if (expect(finitePoint(pivotPoint) && gizmo->hitTest(pivotPoint, top->componentGizmoFrame(), top->font()) == 4 && top->beginObjectTransform(pivotPoint),
						"pivot drag begins")) {
					gizmo->updateDrag(pivotPoint + QPointF(200, 0));
					gizmo->finish(true);
					expect(until([&] { return gizmo->pivot().x != 48; }) && camera->objectGizmo()->pivot().x == gizmo->pivot().x
							&& std::fmod(gizmo->pivot().x, 16) == 0 && gizmo->pivot().y == 16 && bytes() == baseline,
						"the placed pivot is shared and edits nothing", text(gizmo->pivot()));
					expect(action(QStringLiteral("map.resetObjectPivot"))->isEnabled(), "Reset Object Pivot is offered");
					const double placed = gizmo->pivot().x;
					// A move carries a placed pivot with the selection.
					const QPointF arrow = handlePoint(gizmo, top->componentGizmoFrame(), 0);
					if (expect(top->beginObjectTransform(arrow), "move with a placed pivot")) {
						gizmo->updateDrag(arrow + QPointF(60, 0));
						const double moved = gizmo->translation().x;
						gizmo->finish(true);
						expect(until([&] { return bytes() != baseline; }) && std::abs(gizmo->pivot().x - (placed + moved)) < 1e-6, "the placed pivot travels",
							text(gizmo->pivot()));
						undo->trigger();
					}
					action(QStringLiteral("map.resetObjectPivot"))->trigger();
					expect(at(gizmo->pivot(), 48, 16, 16) && !action(QStringLiteral("map.resetObjectPivot"))->isEnabled(), "reset to the centre");
					// A click on the pivot places it where it is; another click frees it.
					const QPointF click = handlePoint(gizmo, top->componentGizmoFrame(), 4);
					expect(top->beginObjectTransform(click), "pivot click");
					gizmo->finish(true);
					expect(action(QStringLiteral("map.resetObjectPivot"))->isEnabled(), "a click places the pivot");
					expect(top->beginObjectTransform(handlePoint(gizmo, top->componentGizmoFrame(), 4)), "second pivot click");
					gizmo->finish(true);
					expect(!action(QStringLiteral("map.resetObjectPivot"))->isEnabled(), "a second click follows the centre again");
				}

				// Set Object Pivot types the point; it is accessible and fits.
				{
					QTimer drive;
					drive.setInterval(10);
					QObject::connect(&drive, &QTimer::timeout, &shell, [&] {
						auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
						if (!dialog || dialog->objectName() != QStringLiteral("levelObjectPivotDialog")) { return; }
						drive.stop();
						for (auto* field : dialog->findChildren<QDoubleSpinBox*>()) {
							auto* accessible = QAccessible::queryAccessibleInterface(field);
							expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && field->layoutDirection() == Qt::LeftToRight, "accessible pivot field",
								field->objectName());
						}
						dialog->findChild<QCheckBox*>(QStringLiteral("levelObjectPivotPlaced"))->setChecked(true);
						dialog->findChild<QDoubleSpinBox*>(QStringLiteral("levelObjectPivotX"))->setValue(8);
						dialog->findChild<QDoubleSpinBox*>(QStringLiteral("levelObjectPivotY"))->setValue(-8);
						auto* accept = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
						expect(accept->isVisible() && dialog->rect().contains(accept->mapTo(dialog, accept->rect().center())), "OK visible");
						accept->click();
					});
					QTimer watchdog;
					watchdog.setSingleShot(true);
					QObject::connect(&watchdog, &QTimer::timeout, &shell, [] {
						expect(false, "pivot dialog watchdog");
						if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) { modal->reject(); }
					});
					drive.start();
					watchdog.start(15000);
					action(QStringLiteral("map.setObjectPivot"))->trigger();
					drive.stop();
					watchdog.stop();
					expect(at(gizmo->pivot(), 8, -8, 16) && at(camera->objectGizmo()->pivot(), 8, -8, 16) && bytes() == baseline, "typed pivot shared",
						text(gizmo->pivot()));
					action(QStringLiteral("map.resetObjectPivot"))->trigger();
				}

				// The camera's Z arrow lifts the selection.
				camera->setCameraView({-160, -200, 140}, 50, -25);
				if (expect(until([&] { return camera->isEnabled(); }) && tests::settleModelViewport(*camera), "camera ready")) {
					auto* cameraGizmo = camera->objectGizmo();
					const auto cameraFrame = camera->componentGizmoFrame(cameraGizmo->pivot());
					const QPointF zHandle = handlePoint(cameraGizmo, cameraFrame, 2, false);
					if (expect(finitePoint(zHandle) && camera->beginObjectTransform(zHandle), "camera Z arrow drag begins",
							QStringLiteral("%1,%2 hit %3").arg(zHandle.x()).arg(zHandle.y()).arg(cameraGizmo->hitTest(zHandle, cameraFrame, camera->font())))) {
						cameraGizmo->updateDrag(zHandle + QPointF(0, -90));
						const auto lift = cameraGizmo->translation();
						expect(lift.z > 0 && lift.x == 0 && lift.y == 0 && camera->isMovingSelection(), "the camera previews a lift", text(lift));
						cameraGizmo->finish(true);
						expect(until([&] { return bytes() != baseline; }) && shell.levelDocument().undoStack.size() == depth + 1, "camera drag applies once");
						undo->trigger();
						expect(bytes() == baseline, "camera drag undoes");
					}
				}

				// A locked object in the selection: no manipulator, nothing moves.
				pick(top, {brush(1), brush(2)});
				expect(until([&] { return !gizmo->available(); }) && !camera->objectGizmo()->available(), "a locked object hides the manipulator");
				translate->trigger();
				expect(!translate->isChecked() && gizmo->tool() != LevelComponentTool::Translate && action(QStringLiteral("levelTool-select"))->isChecked(),
					"choosing it again returns to Select");
				if (nrc) {
					// W toggles the tool from a plan view.
					shell.activateWindow();
					top->setFocus(Qt::OtherFocusReason);
					expect(until([&] { return top->hasFocus(); }), "plan view focused");
					QTest::keyClick(top, Qt::Key_W);
					expect(translate->isChecked(), "W turns the Translate tool on");
					QTest::keyClick(top, Qt::Key_W);
					expect(!translate->isChecked(), "W again returns to Select");
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
