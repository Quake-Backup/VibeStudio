// The Levels Scale tool in the shell (app/level_object_gizmo_actions.cpp,
// app/level_component_gizmo.cpp, map_viewport_object_gizmo.cpp,
// model_viewport_object_gizmo.cpp, model_viewport_resize.cpp, map_viewport.cpp):
// the tool bar button, command, Tools tile, palette entry and TrenchBroom's T;
// X, Y and Z handles, the view square and the shared pivot in the plan views;
// an axis drag with real mouse events that previews a wire and equals the core
// service byte for byte in one undo step; Shift's equal axes, Ctrl's grid size
// and Escape; the Front square with Shift; the box handles kept beside the
// manipulator, with Alt resizing about the centre and Shift keeping
// proportions in a plan view; TrenchBroom's edge and corner handles in the
// camera with Shift and Alt; Rotate or Scale Tool for objects and components;
// refusal of locked selections. NetRadiant Custom, TrenchBroom and VibeStudio
// profiles at 100%, and at 200% with high contrast, expanded translations and
// right-to-left layout.
#include "app/application_shell.h"
#include "app/level_component_gizmo.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/box_resize.h"
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
#include <QFont>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
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
bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }
bool at(const LevelMapVec3& v, double x, double y, double z, double tolerance = 1e-6)
{
	return v.valid && near(v.x, x, tolerance) && near(v.y, y, tolerance) && near(v.z, z, tolerance);
}
LevelMapSelectionRef brush(int id) { return {LevelMapSelectionKind::QuakeBrush, id}; }
const LevelComponentGizmoHandle* handle(const QVector<LevelComponentGizmoHandle>& all, int id)
{
	for (const auto& candidate : all) {
		if (candidate.id == id) { return &candidate; }
	}
	return nullptr;
}
bool finitePoint(const QPointF& p) { return std::isfinite(p.x()) && std::isfinite(p.y()); }
double length(const QPointF& p) { return std::sqrt(QPointF::dotProduct(p, p)); }
void mouse(QWidget* widget, QEvent::Type type, const QPointF& point, Qt::MouseButton button, Qt::MouseButtons buttons,
	Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
	QMouseEvent event(type, point, widget->mapToGlobal(point), button, buttons, modifiers);
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
	// Brushes 0 and 1 are free; brush 2 sits on a locked layer. Brushes 0
	// and 1 together span 0..96 by 0..32 by 0..32, centred on 48, 16, 16.
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
	const auto path = temp.filePath("scale.map");
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(serializeLevelMap(created).bytes) <= 0) { return 1; }
	file.close();

	for (const auto* profile : {"netradiant-custom", "trenchbroom", "vibestudio-default"}) {
		const bool trenchBroom = QLatin1String(profile) == QLatin1String("trenchbroom");
		for (int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Scale tool UI " << phase.toUtf8().constData() << std::endl;
			settings.setSelectedEditorProfileId(profile);
			settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
			settings.setLevelTextureScaleLock(true);
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
				auto* scaleTool = action(QStringLiteral("map.scaleTool"));
				auto* rotateOrScale = action(QStringLiteral("map.rotateOrScaleTool"));
				auto* rotate = action(QStringLiteral("map.rotateTool"));
				if (!expect(panes.size() == 3 && camera && grid && snap && undo && scaleTool && rotateOrScale && rotate, "panes, camera, grid, snap and the commands")) {
					return 1;
				}
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
				// The core's scale of brushes 0 and 1 for these factors.
				const auto expected = [&](const LevelMapVec3& factors, const LevelMapVec3& pivot) {
					LevelMapDocument reference;
					QString problem;
					expect(loadLevelMap({path, {}, {}}, &reference, &problem), "reference map", problem);
					setLevelMapSelection(&reference, {brush(0), brush(1)});
					LevelObjectScaleRequest request;
					request.factors = {factors.x, factors.y, factors.z};
					request.pivot = pivot;
					request.textures = {true, false};
					LevelObjectScaleReport report;
					expect(scaleLevelMapObjects(&reference, request, &report, &problem), "core reference", problem);
					return serializeLevelMap(reference).bytes;
				};
				const auto bounds = [&] {
					LevelMapVec3 mins, maxs;
					levelMapSelectionBounds(shell.levelDocument(), &mins, &maxs);
					return std::pair {mins, maxs};
				};

				// Registration: commands, menus, Tools tiles, palette, tool bar
				// button and TrenchBroom's T.
				bool menu = false;
				for (auto* owner : scaleTool->associatedObjects()) { menu |= qobject_cast<QMenu*>(owner) != nullptr; }
				CommandPaletteEntry entry, toggleEntry;
				expect(scaleTool->isCheckable() && scaleTool->isEnabled() && !scaleTool->text().isEmpty() && !scaleTool->statusTip().isEmpty() && menu
						&& commandPaletteEntryForCommandId(QStringLiteral("map.scaleTool"), &entry) && !entry.label.isEmpty()
						&& commandPaletteEntryForCommandId(QStringLiteral("map.rotateOrScaleTool"), &toggleEntry) && !toggleEntry.label.isEmpty()
						&& !rotateOrScale->isCheckable() && rotateOrScale->isEnabled(),
					"Scale Tool and Rotate or Scale Tool commands, menu and palette");
				for (const char* id : {"map.scaleTool", "map.rotateOrScaleTool"}) {
					auto* row = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-%1").arg(QLatin1String(id)));
					expect(row && row->focusPolicy() != Qt::NoFocus && !row->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(row),
						"accessible Tools tile", QLatin1String(id));
				}
				auto* button = shell.findChild<QToolButton*>(QStringLiteral("levelToolButton-scale"));
				expect(button && action(QStringLiteral("levelTool-scale"))->isVisible() && !button->accessibleName().isEmpty()
						&& !button->accessibleDescription().isEmpty() && button->focusPolicy() != Qt::NoFocus,
					"accessible tool bar button");
				expect(trenchBroom ? scaleTool->shortcuts() == QList<QKeySequence> {QKeySequence(Qt::Key_T)} : scaleTool->shortcuts().isEmpty(),
					"TrenchBroom binds T; the others leave Scale to the tool bar", QKeySequence::listToString(scaleTool->shortcuts()));
				expect(rotateOrScale->shortcuts().isEmpty(), "Rotate or Scale Tool has no default key", QKeySequence::listToString(rotateOrScale->shortcuts()));

				// Choosing the tool with nothing selected shows no manipulator.
				pick(top, {});
				scaleTool->trigger();
				auto* gizmo = top->objectGizmo();
				expect(scaleTool->isChecked() && gizmo->tool() == LevelComponentTool::Scale && !gizmo->available() && action(QStringLiteral("levelTool-scale"))->isChecked()
						&& !rotate->isChecked() && !action(QStringLiteral("map.translateTool"))->isChecked(),
					"tool on, nothing to scale");

				// Two brushes: X and Y handles, the square and the pivot in Top,
				// and the box's resize handles kept beside them.
				pick(top, {brush(0), brush(1)});
				if (!expect(until([&] { return gizmo->available() && camera->objectGizmo()->available(); }), "manipulator available")) { return 1; }
				const auto depth = shell.levelDocument().undoStack.size();
				expect(at(gizmo->pivot(), 48, 16, 16) && at(camera->objectGizmo()->pivot(), 48, 16, 16), "the pivot is the selection's centre", text(gizmo->pivot()));
				expect(top->hasResizeHandles(), "the Scale tool keeps the box's resize handles");
				auto topFrame = top->componentGizmoFrame();
				QSet<int> ids;
				for (const auto& candidate : gizmo->handles(topFrame)) { ids.insert(candidate.id); }
				expect(ids == QSet<int> {0, 1, 3, 4}, "Top shows X and Y, the square and the pivot; Z points along the view");
				expect(top->accessibleDescription().contains(gizmo->summary()) && gizmo->summary().contains(QStringLiteral("Scale")), "the view describes the tool");

				// A real drag of the X handle 48 units outward: a preview, then
				// one undo step equal to the core.
				const auto all = gizmo->handles(topFrame);
				const auto* xHandle = handle(all, 0);
				if (expect(xHandle && xHandle->points.size() == 2, "X handle")) {
					const QPointF centre = xHandle->points[0], tip = xHandle->points[1];
					const double zoom = length(tip - centre) / topFrame.radius;
					// Along the handle, clear of the square and of the box's own
					// resize handles, which outrank it.
					QPointF start = centre + (tip - centre) * 0.6;
					for (const double share : {0.6, 0.5, 0.7, 0.8, 0.9}) {
						const QPointF candidate = centre + (tip - centre) * share;
						bool clear = true;
						for (const int edges : {int(MapViewport::ResizeMaxHorizontal), int(MapViewport::ResizeMinHorizontal), int(MapViewport::ResizeMaxVertical),
								 int(MapViewport::ResizeMinVertical), MapViewport::ResizeMaxHorizontal | MapViewport::ResizeMaxVertical,
								 MapViewport::ResizeMaxHorizontal | MapViewport::ResizeMinVertical}) {
							clear = clear && length(top->resizeHandlePosition(edges) - candidate) > 12;
						}
						if (clear && length(candidate - centre) > 14 * scale / 100.0) { start = candidate; break; }
					}
					expect(gizmo->hitTest(start, topFrame, top->font()) == 0, "the X handle under the pointer");
					mouse(top, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
					for (int step = 1; step <= 6; ++step) {
						mouse(top, QEvent::MouseMove, start + QPointF(48 * zoom * step / 6, 0), Qt::NoButton, Qt::LeftButton);
					}
					const auto factors = gizmo->scaleFactors();
					expect(gizmo->active() && at(factors, 2, 1, 1) && gizmo->objectPreviewEdgeCount() == 48 && bytes() == baseline && top->testAttribute(Qt::WA_SetCursor),
						"the drag previews both brushes twice as wide without editing", text(factors) + QStringLiteral(" %1 edges").arg(gizmo->objectPreviewEdgeCount()));
					expect(gizmo->summary().contains(QStringLiteral("192")), "the status gives the new size", gizmo->summary());
					mouse(top, QEvent::MouseButtonRelease, start + QPointF(48 * zoom, 0), Qt::LeftButton, Qt::NoButton);
					expect(until([&] { return bytes() != baseline; }), "release applies the scale");
					expect(bytes() == expected(factors, {48, 16, 16, true}) && shell.levelDocument().undoStack.size() == depth + 1 && !top->testAttribute(Qt::WA_SetCursor),
						"the drag equals the core service in one undo step");
					const auto [mins, maxs] = bounds();
					expect(at(mins, -48, 0, 0) && at(maxs, 144, 32, 32), "twice as wide about the centre", text(mins) + QStringLiteral(" / ") + text(maxs));
					undo->trigger();
					expect(bytes() == baseline, "undo restores the map exactly");

					// Shift scales every axis alike; Ctrl lands the size on the
					// grid; Escape throws the drag away.
					// The applied scale and its undo refresh the views first.
					expect(until([&] { return top->isEnabled() && gizmo->available() && !gizmo->active(); }), "views ready after undo");
					topFrame = top->componentGizmoFrame();
					// The view may have moved with the edit: aim at the handle again.
					const auto againHandles = gizmo->handles(topFrame);
					if (const auto* again = handle(againHandles, 0); expect(again && again->points.size() == 2, "X handle again")) {
						start = again->points[0] + (again->points[1] - again->points[0]) * (length(start - centre) / length(tip - centre));
					}
					if (expect(top->beginObjectTransform(start), "X drag begins")) {
						gizmo->updateDrag(start + QPointF(20 * zoom, 0));
						const auto plain = gizmo->scaleFactors();
						gizmo->updateModifiers(Qt::ShiftModifier);
						const auto equal = gizmo->scaleFactors();
						expect(plain.x > 1 && near(plain.y, 1) && near(equal.x, plain.x) && near(equal.y, plain.x) && near(equal.z, plain.x),
							"Shift scales the axes equally", text(plain) + QStringLiteral(" / ") + text(equal));
						gizmo->finish(false);
						snap->setChecked(false);
						if (expect(top->beginObjectTransform(start), "free X drag begins")) {
							gizmo->updateDrag(start + QPointF(13 * zoom, 0));
							gizmo->updateModifiers(Qt::ControlModifier);
							const double size = 96 * gizmo->scaleFactors().x;
							expect(near(std::remainder(size, 16.0), 0) && size > 96, "Ctrl lands the size on the grid", QString::number(size));
							top->setFocus(Qt::OtherFocusReason);
							QTest::keyClick(top, Qt::Key_Escape);
							expect(!gizmo->active() && bytes() == baseline && shell.levelDocument().undoStack.size() == depth, "Escape cancels without an edit");
						}
						snap->setChecked(true);
					}
				}

				// The Front square: X and Z scale; Shift takes the larger change
				// for both and leaves Y, the line of sight.
				auto* frontGizmo = front->objectGizmo();
				if (expect(until([&] { return frontGizmo->available(); }), "Front manipulator")) {
					const auto frontFrame = front->componentGizmoFrame();
					const auto frontHandles = frontGizmo->handles(frontFrame);
					const auto* frontX = handle(frontHandles, 0);
					const auto* frontCentre = handle(frontHandles, 4);
					if (expect(frontX && frontCentre, "Front handles")) {
						const double zoom = length(frontX->points[1] - frontX->points[0]) / frontFrame.radius;
						const QPointF square = frontCentre->label + QPointF(9 * scale / 100.0, 0);
						expect(frontGizmo->hitTest(square, frontFrame, front->font()) == 3, "the square under the pointer");
						if (expect(front->beginObjectTransform(square), "square drag begins")) {
							frontGizmo->updateDrag(square + QPointF(48 * zoom, -32 * zoom), Qt::NoModifier);
							const auto free = frontGizmo->scaleFactors();
							frontGizmo->updateModifiers(Qt::ShiftModifier);
							const auto equal = frontGizmo->scaleFactors();
							expect(at(free, 2, 1, 3) && at(equal, 3, 1, 3), "the square scales X and Z; Shift takes the larger for both",
								text(free) + QStringLiteral(" / ") + text(equal));
							frontGizmo->finish(false);
						}
					}
				}

				// The box's handles keep working beside the manipulator: Alt
				// resizes about the centre, Shift keeps the other axis in
				// proportion.
				const QPointF right = top->resizeHandlePosition(MapViewport::ResizeMaxHorizontal);
				const double topZoom = top->zoom();
				mouse(top, QEvent::MouseButtonPress, right, Qt::LeftButton, Qt::LeftButton);
				expect(!gizmo->active(), "a resize handle outranks the manipulator");
				mouse(top, QEvent::MouseMove, right + QPointF(16 * topZoom, 0), Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
				mouse(top, QEvent::MouseButtonRelease, right + QPointF(16 * topZoom, 0), Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
				if (expect(until([&] { return bytes() != baseline; }), "Alt resize applies")) {
					const auto [mins, maxs] = bounds();
					expect(at(mins, -16, 0, 0) && at(maxs, 112, 32, 32) && shell.levelDocument().undoStack.size() == depth + 1, "Alt resizes about the centre",
						text(mins) + QStringLiteral(" / ") + text(maxs));
					undo->trigger();
				}
				mouse(top, QEvent::MouseButtonPress, right, Qt::LeftButton, Qt::LeftButton);
				mouse(top, QEvent::MouseMove, right + QPointF(32 * topZoom, 0), Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
				mouse(top, QEvent::MouseButtonRelease, right + QPointF(32 * topZoom, 0), Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
				if (expect(until([&] { return bytes() != baseline; }), "Shift resize applies")) {
					const auto [mins, maxs] = bounds();
					expect(at(mins, 0, 16 - 64.0 / 3, 0, 1e-4) && at(maxs, 128, 16 + 64.0 / 3, 32, 1e-4), "Shift keeps Y in proportion about the centre; Z stays",
						text(mins) + QStringLiteral(" / ") + text(maxs));
					undo->trigger();
				}
				expect(bytes() == baseline, "the resizes undo");

				// The camera: TrenchBroom's edge and corner handles while Scale
				// is on; a corner keeps proportions, Alt grows it about the
				// centre, and release resizes the selection in one undo step.
				// Close enough that the box's 26 handles stand apart.
				camera->setCameraView({7, -36, 51}, 52, -28);
				if (expect(until([&] { return camera->isEnabled(); }) && tests::settleModelViewport(*camera), "camera ready")) {
					const auto cameraHandles = camera->selectionResizeHandles();
					int corner = -1, shown = 0;
					for (int id = kBoxResizeFaceHandles; id < kBoxResizeHandles; ++id) {
						if (!finitePoint(cameraHandles[static_cast<size_t>(id)])) { continue; }
						++shown;
						if (id >= 18 && corner < 0) { corner = id; }
					}
					int faces = 0;
					for (int id = 0; id < kBoxResizeFaceHandles; ++id) { faces += finitePoint(cameraHandles[static_cast<size_t>(id)]) ? 1 : 0; }
					expect(camera->selectionResizeBoxHandles() && shown > 0 && corner >= 0, "edge and corner handles show with the Scale tool",
						QStringLiteral("%1 shown, %2 faces, all %3, enabled %4, size %5x%6").arg(shown).arg(faces).arg(camera->selectionResizeBoxHandles())
							.arg(camera->isEnabled()).arg(camera->width()).arg(camera->height()));
					QPointF middle;
					if (corner >= 0 && expect(camera->projectToView({48, 16, 16}, &middle), "centre on screen")) {
						const QPointF point = cameraHandles[static_cast<size_t>(corner)];
						const QPointF outward = (point - middle) / std::max(1.0, length(point - middle));
						if (expect(camera->selectionResizeHandleAt(point) == corner && camera->beginSelectionResize(point), "corner resize begins")) {
							camera->updateSelectionResize(point + outward * 80);
							const ResizeBox source {{0, 0, 0}, {96, 32, 32}};
							const ResizeBox moved = camera->selectionResizeBox();
							const double rx = (moved.maxs[0] - moved.mins[0]) / 96, ry = (moved.maxs[1] - moved.mins[1]) / 32, rz = (moved.maxs[2] - moved.mins[2]) / 32;
							expect(moved != source && near(rx, ry, 1e-9) && near(ry, rz, 1e-9), "a corner keeps the box's proportions",
								QStringLiteral("%1 %2 %3").arg(rx).arg(ry).arg(rz));
							camera->updateSelectionResizeModifiers(Qt::AltModifier);
							const ResizeBox centred = camera->selectionResizeBox();
							expect(near(centred.mins[0] + centred.maxs[0], 96, 1e-9) && near(centred.mins[2] + centred.maxs[2], 32, 1e-9), "Alt grows the corner about the centre");
							camera->finishSelectionResize(true);
							if (expect(until([&] { return bytes() != baseline; }), "the camera resize applies")) {
								const auto [mins, maxs] = bounds();
								expect(near(mins.x, centred.mins[0], 1e-6) && near(maxs.z, centred.maxs[2], 1e-6) && shell.levelDocument().undoStack.size() == depth + 1,
									"the selection takes the previewed box in one undo step", text(mins) + QStringLiteral(" / ") + text(maxs));
								undo->trigger();
								expect(bytes() == baseline, "the camera resize undoes");
							}
						}
					}
				}

				// Rotate or Scale Tool: Scale to Rotate, back to Scale, and
				// Rotate from Select; in a component mode, the component tools.
				rotateOrScale->trigger();
				expect(rotate->isChecked() && !scaleTool->isChecked() && gizmo->tool() == LevelComponentTool::Rotate, "Rotate or Scale goes from Scale to Rotate");
				rotateOrScale->trigger();
				expect(scaleTool->isChecked() && gizmo->tool() == LevelComponentTool::Scale, "and from Rotate to Scale");
				scaleTool->trigger();
				expect(!scaleTool->isChecked() && action(QStringLiteral("levelTool-select"))->isChecked() && !camera->selectionResizeBoxHandles(),
					"choosing Scale again returns to Select, with the camera's face handles only");
				rotateOrScale->trigger();
				expect(rotate->isChecked(), "Rotate or Scale goes from Select to Rotate");
				rotate->trigger();
				if (auto* vertex = action(QStringLiteral("map.vertexTool")); expect(vertex != nullptr, "Edit Vertices command")) {
					vertex->trigger();
					rotateOrScale->trigger();
					expect(action(QStringLiteral("map.componentRotateTool"))->isChecked() && !rotate->isChecked(), "in a component mode it picks Rotate Components");
					rotateOrScale->trigger();
					expect(action(QStringLiteral("map.componentScaleTool"))->isChecked(), "then Scale Components");
					action(QStringLiteral("levelTool-select"))->trigger();
				}

				// A locked object in the selection: no manipulator.
				scaleTool->trigger();
				pick(top, {brush(1), brush(2)});
				expect(until([&] { return !gizmo->available(); }) && !camera->objectGizmo()->available() && scaleTool->isChecked(), "a locked object hides the manipulator");
				scaleTool->trigger();
				expect(!scaleTool->isChecked() && gizmo->tool() != LevelComponentTool::Scale && action(QStringLiteral("levelTool-select"))->isChecked(),
					"choosing it again returns to Select");
				if (trenchBroom) {
					// T toggles the tool from a plan view.
					shell.activateWindow();
					top->setFocus(Qt::OtherFocusReason);
					expect(until([&] { return top->hasFocus(); }), "plan view focused");
					QTest::keyClick(top, Qt::Key_T);
					expect(scaleTool->isChecked(), "T turns the Scale tool on");
					QTest::keyClick(top, Qt::Key_T);
					expect(!scaleTool->isChecked(), "T again returns to Select");
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
