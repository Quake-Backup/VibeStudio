// The Levels Rotate tool in the shell (app/level_object_gizmo_actions.cpp,
// app/level_component_gizmo.cpp, map_viewport_object_gizmo.cpp,
// model_viewport_object_gizmo.cpp): the tool bar button, command, Tools tile,
// palette entry and the NetRadiant Custom and TrenchBroom R; X, Y and Z rings
// (a face-on ring left out), the outer view ring, the free turn and the shared
// pivot in the plan views and the camera; a view-ring drag with real mouse
// events that previews a wire and equals the core service byte for byte in
// one undo step; Escape; Shift's 15-degree snap; a placed pivot; a free turn
// in the camera; TrenchBroom's profile without the free turn, where a click
// inside the rings selects; refusal of locked selections. Also the Translate package's
// follow-ups: Clip does not hand back the object tool, focus loss clears the
// drag cursor, and pointer targets grow at 200%. NetRadiant Custom,
// TrenchBroom and VibeStudio profiles at 100%, and at 200% with high
// contrast, expanded translations and right-to-left layout.
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
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFocusEvent>
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
#include <numbers>

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
const LevelComponentGizmoHandle* handle(const QVector<LevelComponentGizmoHandle>& all, int id)
{
	for (const auto& candidate : all) {
		if (candidate.id == id) { return &candidate; }
	}
	return nullptr;
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
	const auto path = temp.filePath("rotate.map");
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(serializeLevelMap(created).bytes) <= 0) { return 1; }
	file.close();

	for (const auto* profile : {"netradiant-custom", "trenchbroom", "vibestudio-default"}) {
		const bool keyed = QLatin1String(profile) != QLatin1String("vibestudio-default");
		// TrenchBroom's rotate handle has no free turn inside its rings.
		const bool freeTurn = QLatin1String(profile) != QLatin1String("trenchbroom");
		for (int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Rotate tool UI " << phase.toUtf8().constData() << std::endl;
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
				auto* rotate = action(QStringLiteral("map.rotateTool"));
				if (!expect(panes.size() == 3 && camera && grid && snap && undo && rotate, "panes, camera, grid, snap and the command")) { return 1; }
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
				// What a drag asks for, taken before release: afterwards an
				// automatic pivot follows the turned selection's new centre.
				const auto turnOf = [](LevelComponentGizmo* gizmo) {
					LevelObjectRotateRequest turn;
					turn.axis = gizmo->rotationAxis();
					turn.degrees = gizmo->request().amount;
					turn.pivot = gizmo->pivot();
					return turn;
				};
				// The core's turn of brushes 0 and 1 for that request.
				const auto expected = [&](const LevelObjectRotateRequest& turn, bool textureLock) {
					LevelMapDocument reference;
					QString problem;
					expect(loadLevelMap({path, {}, {}}, &reference, &problem), "reference map", problem);
					setLevelMapSelection(&reference, {brush(0), brush(1)});
					LevelObjectRotateRequest request = turn;
					request.textures = {textureLock, false};
					LevelObjectRotateReport report;
					expect(rotateLevelMapObjects(&reference, request, &report, &problem), "core reference", problem);
					return serializeLevelMap(reference).bytes;
				};

				// Registration: command, menus, Tools tile, palette, tool bar
				// button and the profiles' R.
				bool menu = false;
				for (auto* owner : rotate->associatedObjects()) { menu |= qobject_cast<QMenu*>(owner) != nullptr; }
				CommandPaletteEntry entry;
				expect(rotate->isCheckable() && rotate->isEnabled() && !rotate->text().isEmpty() && !rotate->statusTip().isEmpty() && menu
						&& commandPaletteEntryForCommandId(QStringLiteral("map.rotateTool"), &entry) && !entry.label.isEmpty(),
					"Rotate Tool command, menu and palette");
				auto* row = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-map.rotateTool"));
				expect(row && row->focusPolicy() != Qt::NoFocus && !row->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(row),
					"accessible Tools tile");
				auto* button = shell.findChild<QToolButton*>(QStringLiteral("levelToolButton-rotate"));
				expect(button && action(QStringLiteral("levelTool-rotate"))->isVisible() && !button->accessibleName().isEmpty()
						&& !button->accessibleDescription().isEmpty() && button->focusPolicy() != Qt::NoFocus,
					"accessible tool bar button");
				expect(keyed ? rotate->shortcuts() == QList<QKeySequence> {QKeySequence(Qt::Key_R)} : rotate->shortcuts().isEmpty(),
					"NetRadiant Custom and TrenchBroom bind R; the default leaves it to the tool bar", QKeySequence::listToString(rotate->shortcuts()));

				// Choosing the tool with nothing selected shows no manipulator.
				pick(top, {});
				rotate->trigger();
				auto* gizmo = top->objectGizmo();
				expect(rotate->isChecked() && gizmo->tool() == LevelComponentTool::Rotate && !gizmo->available() && action(QStringLiteral("levelTool-rotate"))->isChecked()
						&& !action(QStringLiteral("map.translateTool"))->isChecked(),
					"tool on, nothing to turn");

				// Two brushes: rings, the outer ring, the sphere and the pivot.
				pick(top, {brush(0), brush(1)});
				if (!expect(until([&] { return gizmo->available() && camera->objectGizmo()->available(); }), "manipulator available")) { return 1; }
				const auto depth = shell.levelDocument().undoStack.size();
				expect(at(gizmo->pivot(), 48, 16, 16) && at(camera->objectGizmo()->pivot(), 48, 16, 16), "the pivot is the selection's centre", text(gizmo->pivot()));
				expect(!top->hasResizeHandles(), "the Rotate tool replaces the resize handles");
				auto topFrame = top->componentGizmoFrame();
				QSet<int> ids;
				for (const auto& candidate : gizmo->handles(topFrame)) { ids.insert(candidate.id); }
				expect(ids == (freeTurn ? QSet<int> {0, 1, 3, 4, 5} : QSet<int> {0, 1, 3, 4}),
					"Top shows the X and Y rings edge-on, the outer ring, the pivot and (with a free turn) the sphere; the face-on Z ring is left out");
				expect(gizmo->freeTurn() == freeTurn, "the profile decides the free turn");
				expect(top->accessibleDescription().contains(gizmo->summary()) && !gizmo->summary().isEmpty(), "the view describes the tool");

				// Pointer targets grow with the text scale (the Translate
				// package's 200% follow-up): 9 px from the pivot grabs it only
				// at 200%.
				const auto all = gizmo->handles(topFrame);
				const auto* centreHandle = handle(all, 4);
				const auto* ring = handle(all, 3);
				if (expect(centreHandle && ring, "pivot and outer ring handles")) {
					const QPointF centre = centreHandle->label;
					expect((gizmo->hitTest(centre + QPointF(0, 9), topFrame, top->font()) == 4) == (scale == 200), "the pivot's grab radius scales with the text");
					// A real drag on the outer ring a quarter turn round: a
					// preview first, then one undo step equal to the core.
					// From 30 degrees above the right, clear of the rings' labels,
					// a quarter turn round the outer ring.
					const double radius = std::sqrt(QPointF::dotProduct(ring->points.first() - centre, ring->points.first() - centre));
					const double first = -std::numbers::pi / 6;
					const auto around = [&](double angle) { return centre + QPointF(std::cos(angle), std::sin(angle)) * radius; };
					const QPointF start = around(first);
					expect(gizmo->hitTest(start, topFrame, top->font()) == 3, "the outer ring under the pointer");
					mouse(top, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
					for (int step = 1; step <= 6; ++step) {
						mouse(top, QEvent::MouseMove, around(first - std::numbers::pi / 2 * step / 6), Qt::NoButton, Qt::LeftButton);
					}
					expect(gizmo->active() && std::abs(gizmo->request().amount) == 90 && gizmo->objectPreviewEdgeCount() == 48 && bytes() == baseline
							&& top->testAttribute(Qt::WA_SetCursor),
						"the drag previews a quarter turn of both brushes' wires without editing",
						QStringLiteral("%1° %2 edges").arg(gizmo->request().amount).arg(gizmo->objectPreviewEdgeCount()));
					expect(gizmo->summary().contains(QStringLiteral("90")), "the status gives the angle", gizmo->summary());
					const auto quarter = turnOf(gizmo);
					mouse(top, QEvent::MouseButtonRelease, around(first - std::numbers::pi / 2), Qt::LeftButton, Qt::NoButton);
					expect(until([&] { return bytes() != baseline; }), "release applies the turn");
					expect(bytes() == expected(quarter, true) && shell.levelDocument().undoStack.size() == depth + 1 && !top->testAttribute(Qt::WA_SetCursor),
						"the drag equals the core service in one undo step");
					LevelMapVec3 mins, maxs;
					expect(levelMapSelectionBounds(shell.levelDocument(), &mins, &maxs) && at(mins, 32, -32, 0) && at(maxs, 64, 64, 32),
						"a quarter turn about the centre", text(mins) + QStringLiteral(" / ") + text(maxs));
					undo->trigger();
					expect(bytes() == baseline, "undo restores the map exactly");
				}

				// Escape throws a ring drag away; focus loss does too and takes
				// the drag cursor with it.
				topFrame = top->componentGizmoFrame();
				const auto ringHandles = gizmo->handles(topFrame);
				const auto* xRing = handle(ringHandles, 0);
				const auto* yRing = handle(ringHandles, 1);
				const QPointF xPoint = xRing ? xRing->label : QPointF(std::nan(""), std::nan(""));
				const QPointF yPoint = yRing ? yRing->label : QPointF(std::nan(""), std::nan(""));
				if (expect(finitePoint(xPoint) && top->beginObjectTransform(xPoint), "X ring drag begins")) {
					gizmo->updateDrag(xPoint + QPointF(0, -60));
					expect(gizmo->request().amount != 0, "the edge-on X ring turns with a straight drag", QString::number(gizmo->request().amount));
					top->setFocus(Qt::OtherFocusReason);
					QTest::keyClick(top, Qt::Key_Escape);
					expect(!gizmo->active() && bytes() == baseline && shell.levelDocument().undoStack.size() == depth, "Escape cancels without an edit");
				}
				if (expect(finitePoint(yPoint) && gizmo->hitTest(yPoint, topFrame, top->font()) == 1, "Y ring under its label")) {
					mouse(top, QEvent::MouseButtonPress, yPoint, Qt::LeftButton, Qt::LeftButton);
					if (expect(gizmo->active() && top->testAttribute(Qt::WA_SetCursor), "a pressed ring sets the drag cursor")) {
						QFocusEvent out(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
						QApplication::sendEvent(top, &out);
						expect(!gizmo->active() && !top->testAttribute(Qt::WA_SetCursor) && bytes() == baseline, "focus loss cancels and clears the cursor");
					}
				}

				// Shift snaps the free angle of the Front view's outer ring to
				// 15 degrees; with Snap off it turns freely.
				snap->setChecked(false);
				auto* frontGizmo = front->objectGizmo();
				const auto frontFrame = front->componentGizmoFrame();
				const auto frontHandles = frontGizmo->handles(frontFrame);
				const auto* frontRing = handle(frontHandles, 3);
				const auto* frontCentre = handle(frontHandles, 4);
				if (expect(frontRing && frontCentre && until([&] { return frontGizmo->available(); }), "Front outer ring")) {
					const QPointF centre = frontCentre->label;
					const double radius = std::sqrt(QPointF::dotProduct(frontRing->points.first() - centre, frontRing->points.first() - centre));
					const double first = -std::numbers::pi / 6;
					if (expect(front->beginObjectTransform(centre + QPointF(std::cos(first), std::sin(first)) * radius), "Front ring drag begins")) {
						frontGizmo->updateDrag(centre + QPointF(std::cos(first - 0.7), std::sin(first - 0.7)) * radius, Qt::NoModifier);
						const double free = frontGizmo->request().amount;
						frontGizmo->updateModifiers(Qt::ShiftModifier);
						const double snapped = frontGizmo->request().amount;
						expect(std::abs(std::remainder(free, 15.0)) > 1e-3 && std::abs(std::remainder(snapped, 15.0)) < 1e-9 && std::abs(snapped) == 45
								&& at(frontGizmo->rotationAxis(), 0, -1, 0),
							"free with Snap off, 15 degrees with Shift, about the Front view's line of sight", QStringLiteral("%1 / %2").arg(free).arg(snapped));
						frontGizmo->finish(false);
					}
				}
				snap->setChecked(true);

				// A placed pivot: drag it, then turn about it; nothing is edited
				// until the turn.
				topFrame = top->componentGizmoFrame();
				const auto pivotHandles = gizmo->handles(topFrame);
				const auto* pivotHandle = handle(pivotHandles, 4);
				if (expect(pivotHandle && top->beginObjectTransform(pivotHandle->label), "pivot drag begins")) {
					gizmo->updateDrag(pivotHandle->label + QPointF(-200, 0));
					gizmo->finish(true);
					expect(until([&] { return gizmo->pivot().x != 48; }) && camera->objectGizmo()->pivot().x == gizmo->pivot().x && gizmo->pivot().y == 16
							&& bytes() == baseline && action(QStringLiteral("map.resetObjectPivot"))->isEnabled(),
						"the placed pivot is shared and edits nothing", text(gizmo->pivot()));
					const auto placed = gizmo->pivot();
					topFrame = top->componentGizmoFrame();
					const auto turnHandles = gizmo->handles(topFrame);
					const auto* outer = handle(turnHandles, 3);
					const auto* centreAt = handle(turnHandles, 4);
					if (expect(outer && centreAt, "outer ring about the placed pivot")) {
						const double radius = std::sqrt(QPointF::dotProduct(outer->points.first() - centreAt->label, outer->points.first() - centreAt->label));
						const double first = -std::numbers::pi / 6;
						const auto around = [&](double angle) { return centreAt->label + QPointF(std::cos(angle), std::sin(angle)) * radius; };
						if (expect(top->beginObjectTransform(around(first)), "turn about the placed pivot begins")) {
							gizmo->updateDrag(around(first + std::numbers::pi / 2));
							gizmo->updateDrag(around(first + std::numbers::pi));
							expect(std::abs(gizmo->request().amount) == 180, "a half turn", QString::number(gizmo->request().amount));
							const auto half = turnOf(gizmo);
							gizmo->finish(true);
							expect(until([&] { return bytes() != baseline; }) && bytes() == expected(half, true) && at(gizmo->pivot(), placed.x, placed.y, placed.z),
								"a half turn about the placed pivot equals the core, and the pivot stays", text(gizmo->pivot()));
							undo->trigger();
							expect(bytes() == baseline, "the half turn undoes");
						}
					}
					action(QStringLiteral("map.resetObjectPivot"))->trigger();
					expect(at(gizmo->pivot(), 48, 16, 16), "reset to the centre");
				}

				// Inside the rings, off the rings and the pivot: NetRadiant
				// Custom's free turn, or under TrenchBroom's profile the view's
				// own click, which selects what is under the pointer.
				pick(top, {brush(1)});
				if (expect(until([&] { return gizmo->available() && at(gizmo->pivot(), 80, 16, 16); }), "manipulator on brush 1")) {
					const auto insideFrame = top->componentGizmoFrame();
					const auto insideHandles = gizmo->handles(insideFrame);
					const auto* outerRing = handle(insideHandles, 3);
					const auto* pivotPoint = handle(insideHandles, 4);
					if (expect(outerRing && pivotPoint, "outer ring and pivot")) {
						const double radius = std::sqrt(QPointF::dotProduct(outerRing->points.first() - pivotPoint->label, outerRing->points.first() - pivotPoint->label)) / 1.15;
						const QPointF inside = pivotPoint->label + QPointF(-0.5, -0.5) * radius;
						expect(gizmo->hitTest(inside, insideFrame, top->font()) == (freeTurn ? 5 : -1), "inside the rings is the free turn only where there is one");
						if (!freeTurn) {
							// The same click under Select, for comparison.
							rotate->trigger();
							pick(top, {brush(1)});
							mouse(top, QEvent::MouseButtonPress, inside, Qt::LeftButton, Qt::LeftButton);
							mouse(top, QEvent::MouseButtonRelease, inside, Qt::LeftButton, Qt::NoButton);
							const auto underSelect = shell.levelDocument().selection;
							rotate->trigger();
							pick(top, {brush(1)});
							expect(until([&] { return gizmo->available() && rotate->isChecked(); }), "Rotate again on brush 1");
							mouse(top, QEvent::MouseButtonPress, inside, Qt::LeftButton, Qt::LeftButton);
							expect(!gizmo->active(), "a press inside the rings does not turn");
							mouse(top, QEvent::MouseButtonRelease, inside, Qt::LeftButton, Qt::NoButton);
							const auto selection = shell.levelDocument().selection;
							expect(selection == underSelect && rotate->isChecked(), "the click selects as it does under Select, and the tool stays",
								QStringLiteral("%1 selected, %2 under Select").arg(selection.size()).arg(underSelect.size()));
							expect(bytes() == baseline, "the click edits nothing");
						}
					}
				}
				pick(top, {brush(0), brush(1)});
				expect(until([&] { return at(gizmo->pivot(), 48, 16, 16); }), "both brushes again");

				// The camera: a free turn inside the rings, with Texture Lock off
				// so any axis applies without Valve 220 conversion.
				camera->setCameraView({-160, -200, 140}, 50, -25);
				if (freeTurn && expect(until([&] { return camera->isEnabled(); }) && tests::settleModelViewport(*camera), "camera ready")) {
					auto* cameraGizmo = camera->objectGizmo();
					const auto cameraFrame = camera->componentGizmoFrame(cameraGizmo->pivot());
					const auto cameraHandles = cameraGizmo->handles(cameraFrame);
					const auto* cameraCentre = handle(cameraHandles, 4);
					QPointF inside(std::nan(""), std::nan(""));
					for (int dx = -60; dx <= 60 && cameraCentre && !finitePoint(inside); dx += 6) {
						for (int dy = -60; dy <= 60 && !finitePoint(inside); dy += 6) {
							const QPointF candidate = cameraCentre->label + QPointF(dx, dy);
							if (cameraGizmo->hitTest(candidate, cameraFrame, camera->font()) == 5) { inside = candidate; }
						}
					}
					auto* lock = action(QStringLiteral("map.textureLock"));
					if (lock && lock->isChecked()) { lock->trigger(); }
					if (expect(finitePoint(inside) && camera->beginObjectTransform(inside), "camera free turn begins")) {
						cameraGizmo->updateDrag(inside + QPointF(40, -25));
						const auto axis = cameraGizmo->rotationAxis();
						expect(axis.valid && cameraGizmo->request().amount > 0 && camera->isMovingSelection() && cameraGizmo->objectPreviewEdgeCount() == 48,
							"the camera previews a free turn", text(axis) + QStringLiteral(" %1°").arg(cameraGizmo->request().amount));
						const auto free = turnOf(cameraGizmo);
						cameraGizmo->finish(true);
						const bool applied = until([&] { return bytes() != baseline; });
						expect(applied && bytes() == expected(free, false) && shell.levelDocument().undoStack.size() == depth + 1,
							"the free turn equals the core in one undo step",
							QStringLiteral("applied %1, depth %2/%3, axis %4, %5°, status %6").arg(applied).arg(shell.levelDocument().undoStack.size()).arg(depth + 1)
								.arg(text(cameraGizmo->rotationAxis())).arg(cameraGizmo->request().amount).arg(shell.statusBar()->currentMessage()));
						undo->trigger();
						expect(bytes() == baseline, "the free turn undoes");
					}
					if (lock && !lock->isChecked()) { lock->trigger(); }
				}

				// Clip replaces the Rotate tool, and it does not come back when
				// Clip ends (the Translate package's follow-up).
				if (auto* clip = action(QStringLiteral("map.clipTool")); expect(clip != nullptr, "Clip Tool command")) {
					clip->trigger();
					expect(top->clipMode() && !rotate->isChecked() && gizmo->tool() == LevelComponentTool::Move, "Clip replaces the Rotate tool");
					clip->trigger();
					expect(!top->clipMode() && !rotate->isChecked() && action(QStringLiteral("levelTool-select"))->isChecked(), "Select follows Clip");
				}

				// A locked object in the selection: no manipulator.
				rotate->trigger();
				pick(top, {brush(1), brush(2)});
				expect(until([&] { return !gizmo->available(); }) && !camera->objectGizmo()->available() && rotate->isChecked(), "a locked object hides the manipulator");
				rotate->trigger();
				expect(!rotate->isChecked() && gizmo->tool() != LevelComponentTool::Rotate && action(QStringLiteral("levelTool-select"))->isChecked(),
					"choosing it again returns to Select");
				if (keyed) {
					// R toggles the tool from a plan view.
					shell.activateWindow();
					top->setFocus(Qt::OtherFocusReason);
					expect(until([&] { return top->hasFocus(); }), "plan view focused");
					QTest::keyClick(top, Qt::Key_R);
					expect(rotate->isChecked(), "R turns the Rotate tool on");
					QTest::keyClick(top, Qt::Key_R);
					expect(!rotate->isChecked(), "R again returns to Select");
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
