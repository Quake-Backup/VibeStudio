// The Levels Transform tool and the Shear Tool in the shell
// (app/level_object_gizmo_actions.cpp, app/level_component_gizmo.cpp,
// map_viewport_object_gizmo.cpp, model_viewport_resize.cpp,
// level_camera_resize_actions.cpp, level_editing_actions.cpp): the tool bar
// button, commands, Tools tiles, palette entries and keys (NetRadiant Custom's
// Q for Select or Transform, TrenchBroom's G for the Shear Tool); the box's
// side lines, corners and pivot in a plan view; a side's skew with real mouse
// events that previews a wire and equals the core side shear byte for byte in
// one undo step; a corner's turn about the opposite corner, a scale from just
// outside a side and a move from inside, each equal to its core service; a
// placed pivot; Escape; the manipulator in the camera with Alt lifting a move;
// the camera's shear handles with a top face slid within its plane and Alt
// sliding a side face up; one tool at a time; and refusal of locked
// selections. NetRadiant Custom, TrenchBroom and VibeStudio profiles at 100%,
// and at 200% with high contrast, expanded translations and right-to-left
// layout.
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
	const auto path = temp.filePath("transform.map");
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(serializeLevelMap(created).bytes) <= 0) { return 1; }
	file.close();

	// The NetRadiant Custom, TrenchBroom and VibeStudio profiles at 100% and
	// 200%; GtkRadiant, QuArK and Hammer at 100%, as the gestures do not
	// depend on the profile beyond its keys.
	for (const auto* profile : {"netradiant-custom", "trenchbroom", "vibestudio-default", "gtkradiant-1-6", "quark", "hammer"}) {
		const bool radiant = QLatin1String(profile) == QLatin1String("netradiant-custom");
		const bool trenchBroom = QLatin1String(profile) == QLatin1String("trenchbroom");
		const bool both = radiant || trenchBroom || QLatin1String(profile) == QLatin1String("vibestudio-default");
		for (int scale : {100, 200}) {
			if (scale == 200 && !both) { continue; }
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			// VIBESTUDIO_TEST_ONLY="trenchbroom 200%" runs one combination.
			if (qEnvironmentVariableIsSet("VIBESTUDIO_TEST_ONLY") && phase != qEnvironmentVariable("VIBESTUDIO_TEST_ONLY")) { continue; }
			std::cout << "Transform tool UI " << phase.toUtf8().constData() << std::endl;
			// Fresh settings for each combination, so no window layout saved by
			// the one before decides the views' sizes.
			StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings-%1-%2.ini").arg(QLatin1String(profile)).arg(scale)));
			StudioSettings settings;
			settings.setRestoreSession(false);
			settings.setReducedMotion(true);
			settings.setLevelViewLayoutPreference("four-views");
			// Plain geometry so each drag can be compared with its core service.
			settings.setLevelTextureLock(false);
			settings.setLevelTextureScaleLock(false);
			settings.setSelectedEditorProfileId(profile);
			settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
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
				auto* transform = action(QStringLiteral("map.transformTool"));
				auto* toggle = action(QStringLiteral("map.selectOrTransformTool"));
				auto* shearTool = action(QStringLiteral("map.shearTool"));
				auto* rotate = action(QStringLiteral("map.rotateTool"));
				if (!expect(panes.size() == 3 && camera && grid && snap && undo && transform && toggle && shearTool && rotate, "panes, camera, grid, snap and the commands")) {
					return 1;
				}
				grid->setCurrentIndex(grid->findData(16));
				snap->setChecked(true);
				auto* top = panes.value(0);
				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				const auto pick = [&](MapViewport* pane, const QVector<LevelMapSelectionRef>& refs) {
					pane->setSelectionSet(refs);
					pane->selectionSetChanged(refs);
				};
				const auto baseline = bytes();
				// The core's edit of brushes 0 and 1.
				const auto reference = [&](const std::function<bool(LevelMapDocument*, QString*)>& edit) {
					LevelMapDocument document;
					QString problem;
					expect(loadLevelMap({path, {}, {}}, &document, &problem), "reference map", problem);
					setLevelMapSelection(&document, {brush(0), brush(1)});
					expect(edit(&document, &problem), "core reference", problem);
					return serializeLevelMap(document).bytes;
				};

				// Registration: commands, menus, Tools tiles, palette, tool bar
				// button and the profiles' keys.
				bool menu = false;
				for (auto* owner : transform->associatedObjects()) { menu |= qobject_cast<QMenu*>(owner) != nullptr; }
				CommandPaletteEntry entry, toggleEntry, shearEntry;
				expect(transform->isCheckable() && transform->isEnabled() && !transform->text().isEmpty() && !transform->statusTip().isEmpty() && menu
						&& commandPaletteEntryForCommandId(QStringLiteral("map.transformTool"), &entry) && !entry.label.isEmpty()
						&& commandPaletteEntryForCommandId(QStringLiteral("map.selectOrTransformTool"), &toggleEntry) && !toggleEntry.label.isEmpty()
						&& commandPaletteEntryForCommandId(QStringLiteral("map.shearTool"), &shearEntry) && !shearEntry.label.isEmpty()
						&& !toggle->isCheckable() && toggle->isEnabled(),
					"Transform Tool, Select or Transform Tool and Shear Tool commands, menu and palette");
				for (const char* id : {"map.transformTool", "map.selectOrTransformTool", "map.shearTool"}) {
					auto* row = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-%1").arg(QLatin1String(id)));
					expect(row && row->focusPolicy() != Qt::NoFocus && !row->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(row),
						"accessible Tools tile", QLatin1String(id));
				}
				auto* button = shell.findChild<QToolButton*>(QStringLiteral("levelToolButton-transform"));
				expect(button && action(QStringLiteral("levelTool-transform"))->isVisible() && !button->accessibleName().isEmpty()
						&& !button->accessibleDescription().isEmpty() && button->focusPolicy() != Qt::NoFocus,
					"accessible tool bar button");
				expect(radiant ? toggle->shortcuts() == QList<QKeySequence> {QKeySequence(Qt::Key_Q)} : toggle->shortcuts().isEmpty(),
					"NetRadiant Custom binds Q to Select or Transform", QKeySequence::listToString(toggle->shortcuts()));
				expect(transform->shortcuts().isEmpty(), "Transform Tool has no default key", QKeySequence::listToString(transform->shortcuts()));
				expect(trenchBroom ? shearTool->shortcuts() == QList<QKeySequence> {QKeySequence(Qt::Key_G)} : shearTool->shortcuts().isEmpty(),
					"TrenchBroom binds G to the Shear Tool", QKeySequence::listToString(shearTool->shortcuts()));

				// Choosing the tool with nothing selected shows no manipulator.
				pick(top, {});
				transform->trigger();
				auto* gizmo = top->objectGizmo();
				expect(transform->isChecked() && gizmo->tool() == LevelComponentTool::Transform && !gizmo->available()
						&& action(QStringLiteral("levelTool-transform"))->isChecked() && !rotate->isChecked(),
					"tool on, nothing to transform");

				// Two brushes: the box's four side lines, four corners and the
				// pivot in Top; the resize handles give way.
				pick(top, {brush(0), brush(1)});
				if (!expect(until([&] { return gizmo->available() && camera->objectGizmo()->available(); }), "manipulator available")) { return 1; }
				// The box fills the Top view, whatever the profile framed, so its
				// inside stays clear of the side lines at 200%.
				top->zoomToSelection();
				for (int step = 0; step < 4; ++step) { top->zoomIn(); }
				const auto depth = shell.levelDocument().undoStack.size();
				expect(!top->hasResizeHandles(), "the Transform tool replaces the resize handles");
				auto frame = top->componentGizmoFrame();
				QSet<int> ids;
				for (const auto& candidate : gizmo->handles(frame)) { ids.insert(candidate.id); }
				expect(ids == QSet<int> {4, 10, 11, 16, 17, 38, 39, 40, 41}, "Top shows the outline's four sides, its corners and the pivot",
					QStringLiteral("%1").arg(ids.size()));
				expect(top->accessibleDescription().contains(gizmo->summary()) && gizmo->summary().contains(QStringLiteral("Transform")), "the view describes the tool");
				const auto worldToScreen = [&](double x, double y) { return frame.project({x, y, 16, true}); };
				const double zoom = length(worldToScreen(16, 0) - worldToScreen(0, 0)) / 16;

				// A real drag of the top side (Y 32) 32 units along X: a wire
				// preview, then one undo step equal to the core's side shear.
				{
					const QPointF start = worldToScreen(28, 32);
					expect(gizmo->hitTest(start, frame, top->font()) == 11, "the top side under the pointer",
						QString::number(gizmo->hitTest(start, frame, top->font())));
					mouse(top, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
					const QPointF end = worldToScreen(60, 32);
					for (int step = 1; step <= 6; ++step) {
						mouse(top, QEvent::MouseMove, start + (end - start) * step / 6, Qt::NoButton, Qt::LeftButton);
					}
					int axis = -1, side = 0;
					LevelMapVec3 offset;
					expect(gizmo->active() && gizmo->transformKind() == LevelObjectTransformKind::Skew && gizmo->objectSkew(&axis, &side, &offset)
							&& axis == 1 && side == 1 && at(offset, 32, 0, 0) && at(gizmo->pivot(), 48, 0, 16) && gizmo->objectPreviewEdgeCount() == 48
							&& bytes() == baseline && top->testAttribute(Qt::WA_SetCursor),
						"the drag previews the slant about the bottom side without editing",
						text(offset) + QStringLiteral(" about ") + text(gizmo->pivot()) + QStringLiteral(", %1 edges").arg(gizmo->objectPreviewEdgeCount()));
					expect(gizmo->summary().contains(QStringLiteral("Skew")), "the status describes the skew", gizmo->summary());
					mouse(top, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
					expect(until([&] { return bytes() != baseline; }), "release applies the skew");
					const auto expected = reference([](LevelMapDocument* document, QString* problem) {
						LevelObjectShearRequest request;
						request.axis = 1;
						request.side = 1;
						request.offset = {32, 0, 0, true};
						request.pivot = {48, 0, 16, true};
						request.textures = {false, false};
						return shearLevelMapObjects(document, request, nullptr, problem);
					});
					expect(bytes() == expected && shell.levelDocument().undoStack.size() == depth + 1 && !top->testAttribute(Qt::WA_SetCursor),
						"the drag equals the core side shear in one undo step");
					undo->trigger();
					expect(bytes() == baseline, "undo restores the map exactly");
				}

				// The views refresh after the edit and its undo.
				expect(until([&] { return top->isEnabled() && gizmo->available() && !gizmo->active(); }), "views ready after undo");
				frame = top->componentGizmoFrame();

				// A corner turns about the opposite corner: 15 degrees about Z.
				{
					const QPointF corner = worldToScreen(96, 32);
					expect(gizmo->hitTest(corner, frame, top->font()) == 41, "the top right corner under the pointer");
					if (expect(top->beginObjectTransform(corner), "corner turn begins")) {
						const double radians = 15 * 3.14159265358979323846 / 180;
						const QPointF turned = worldToScreen(96 * std::cos(radians) - 32 * std::sin(radians), 96 * std::sin(radians) + 32 * std::cos(radians));
						gizmo->updateDrag(corner + (turned - corner) * 0.5);
						gizmo->updateDrag(turned);
						expect(gizmo->transformKind() == LevelObjectTransformKind::Rotate && near(gizmo->request().amount, 15) && at(gizmo->pivot(), 0, 0, 16)
								&& at(gizmo->rotationAxis(), 0, 0, 1),
							"15 degrees about Z through the opposite corner",
							QString::number(gizmo->request().amount) + QStringLiteral(" about ") + text(gizmo->pivot()));
						gizmo->finish(true);
						expect(until([&] { return bytes() != baseline; }), "the turn applies");
						const auto expected = reference([](LevelMapDocument* document, QString* problem) {
							LevelObjectRotateRequest request;
							request.axis = {0, 0, 1, true};
							request.degrees = 15;
							request.pivot = {0, 0, 16, true};
							request.textures = {false, false};
							return rotateLevelMapObjects(document, request, nullptr, problem);
						});
						expect(bytes() == expected && shell.levelDocument().undoStack.size() == depth + 1, "the turn equals the core's in one undo step");
						undo->trigger();
					}
				}
				expect(until([&] { return gizmo->available() && !gizmo->active(); }) && bytes() == baseline, "the turn undoes");
				frame = top->componentGizmoFrame();

				// Just outside the right side scales X about the left side.
				{
					const QPointF outside = worldToScreen(96, 16) + QPointF(12 * scale / 100.0, 0);
					expect(gizmo->hitTest(outside, frame, top->font()) == 52, "outside the right side scales X",
						QString::number(gizmo->hitTest(outside, frame, top->font())));
					expect(gizmo->hitTest(worldToScreen(96, 16) + QPointF(200 * scale / 100.0, 0), frame, top->font()) < 0, "far outside, the view selects");
					if (expect(top->beginObjectTransform(outside), "scale begins")) {
						gizmo->updateDrag(outside + QPointF(32 * zoom, 0));
						const auto factors = gizmo->scaleFactors();
						expect(gizmo->transformKind() == LevelObjectTransformKind::Scale && near(factors.x, 4.0 / 3) && near(factors.y, 1) && near(factors.z, 1)
								&& at(gizmo->pivot(), 0, 16, 16) && gizmo->objectPreviewEdgeCount() == 48,
							"32 units on a 96-unit box about the left side", text(factors) + QStringLiteral(" about ") + text(gizmo->pivot()));
						gizmo->finish(true);
						expect(until([&] { return bytes() != baseline; }), "the scale applies");
						const auto expected = reference([](LevelMapDocument* document, QString* problem) {
							LevelObjectScaleRequest request;
							request.factors = {4.0 / 3, 1, 1};
							request.pivot = {0, 16, 16, true};
							return scaleLevelMapObjects(document, request, nullptr, problem);
						});
						expect(bytes() == expected && shell.levelDocument().undoStack.size() == depth + 1, "the scale equals the core's in one undo step");
						undo->trigger();
					}
				}
				expect(until([&] { return gizmo->available() && !gizmo->active(); }) && bytes() == baseline, "the scale undoes");
				frame = top->componentGizmoFrame();

				// Inside the box moves it; Escape throws a drag away.
				{
					// A point inside, clear of the lines, corners and pivot at this zoom.
					QPointF inside = worldToScreen(44, 12);
					for (const double y : {12.0, 20.0, 8.0, 24.0}) {
						for (const double x : {24.0, 72.0, 16.0, 80.0, 32.0, 64.0}) {
							if (gizmo->hitTest(worldToScreen(x, y), frame, top->font()) == 6 && gizmo->hitTest(inside, frame, top->font()) != 6) {
								inside = worldToScreen(x, y);
							}
						}
					}
					expect(gizmo->hitTest(inside, frame, top->font()) == 6, "inside the box moves",
						QStringLiteral("%1, zoom %2, %3x%4").arg(gizmo->hitTest(inside, frame, top->font())).arg(top->zoom()).arg(top->width()).arg(top->height()));
					if (expect(top->beginObjectTransform(inside), "move begins")) {
						gizmo->updateDrag(inside + QPointF(16 * zoom, 0));
						expect(gizmo->transformKind() == LevelObjectTransformKind::Move && at(gizmo->translation(), 16, 0, 0), "the move previews 16 along X",
							text(gizmo->translation()));
						top->setFocus(Qt::OtherFocusReason);
						QTest::keyClick(top, Qt::Key_Escape);
						expect(!gizmo->active() && bytes() == baseline && shell.levelDocument().undoStack.size() == depth, "Escape cancels without an edit");
					}
					if (expect(top->beginObjectTransform(inside), "move begins again")) {
						gizmo->updateDrag(inside + QPointF(16 * zoom, 0));
						gizmo->finish(true);
						expect(until([&] { return bytes() != baseline; }), "the move applies");
						const auto expected = reference([](LevelMapDocument* document, QString* problem) {
							LevelObjectTranslateRequest request;
							request.delta = {16, 0, 0, true};
							request.textures = {false, false};
							return translateLevelMapObjects(document, request, nullptr, problem);
						});
						expect(bytes() == expected && shell.levelDocument().undoStack.size() == depth + 1, "the move equals the core's in one undo step");
						undo->trigger();
					}
				}
				expect(until([&] { return gizmo->available() && !gizmo->active(); }) && bytes() == baseline, "the move undoes");
				frame = top->componentGizmoFrame();

				// A placed pivot: the top side's skew keeps the centre line instead.
				{
					const auto shown = gizmo->handles(frame);
					const auto* pivot = handle(shown, 4);
					if (expect(pivot != nullptr, "pivot point")) {
						const int aimed = gizmo->hitTest(pivot->label, frame, top->font());
						expect(aimed == 4, "the pivot point under the pointer", QStringLiteral("%1, active %2").arg(aimed).arg(gizmo->active()));
						mouse(top, QEvent::MouseButtonPress, pivot->label, Qt::LeftButton, Qt::LeftButton);
						mouse(top, QEvent::MouseButtonRelease, pivot->label, Qt::LeftButton, Qt::NoButton);
						expect(until([&] { return action(QStringLiteral("map.resetObjectPivot"))->isEnabled(); }), "a click places the pivot");
						frame = top->componentGizmoFrame();
						const QPointF start = worldToScreen(28, 32);
						if (expect(top->beginObjectTransform(start), "skew about the placed pivot begins")) {
							gizmo->updateDrag(worldToScreen(44, 32));
							int axis = -1, side = 0;
							LevelMapVec3 offset;
							expect(gizmo->objectSkew(&axis, &side, &offset) && at(offset, 16, 0, 0) && at(gizmo->pivot(), 48, 16, 16)
									&& near(gizmo->request().amount, 1),
								"16 units over the 16 to the pivot's line", text(offset) + QStringLiteral(" about ") + text(gizmo->pivot()));
							gizmo->finish(false);
						}
						action(QStringLiteral("map.resetObjectPivot"))->trigger();
						frame = top->componentGizmoFrame();
					}
				}

				// A box small on screen keeps only the pivot, the move inside and
				// the scale outside; a zone round its centre still moves it.
				{
					int steps = 0;
					while (steps < 40 && length(top->componentGizmoFrame().project({0, 32, 16, true}) - top->componentGizmoFrame().project({0, 0, 16, true})) >= 12) {
						top->zoomOut();
						++steps;
					}
					const auto small = top->componentGizmoFrame();
					int handles = 0;
					for (const auto& candidate : gizmo->handles(small)) { handles += candidate.id == 4 ? 0 : 1; }
					const QPointF middle = small.project({48, 16, 16, true});
					const QPointF beside = middle + QPointF(4, 0);
					expect(handles == 0 && gizmo->hitTest(beside, small, top->font()) == 6, "a box small on screen keeps a move zone and hides its lines and corners",
						QStringLiteral("%1 handles, hit %2 after %3 steps").arg(handles).arg(gizmo->hitTest(beside, small, top->font())).arg(steps));
					top->zoomToSelection();
					for (int step = 0; step < 4; ++step) { top->zoomIn(); }
					frame = top->componentGizmoFrame();
				}

				// The camera: the same manipulator on the box, Alt lifting a move.
				camera->setCameraView({7, -36, 51}, 52, -28);
				if (expect(until([&] { return camera->isEnabled(); }) && tests::settleModelViewport(*camera), "camera ready")) {
					auto* lens = camera->objectGizmo();
					const auto cameraFrame = camera->componentGizmoFrame(lens->pivot());
					int skews = 0, corners = 0;
					for (const auto& candidate : lens->handles(cameraFrame)) {
						skews += LevelComponentGizmo::transformKindOf(candidate.id) == LevelObjectTransformKind::Skew ? 1 : 0;
						corners += LevelComponentGizmo::transformKindOf(candidate.id) == LevelObjectTransformKind::Rotate ? 1 : 0;
					}
					const QPointF probe = cameraFrame.project ? cameraFrame.project({0, 0, 0, true}) : QPointF();
					expect(lens->tool() == LevelComponentTool::Transform && skews == 12 && corners == 12, "the camera shows every side line and corner",
						QStringLiteral("%1 lines, %2 corners, tool %3, available %4, active %5, radius %6, perspective %7, corner at %8,%9, enabled %10")
							.arg(skews).arg(corners).arg(int(lens->tool())).arg(lens->available()).arg(lens->active()).arg(cameraFrame.radius)
							.arg(cameraFrame.perspective).arg(probe.x()).arg(probe.y()).arg(camera->isEnabled()));
					QPointF centre;
					expect(camera->projectToView({48, 16, 16}, &centre), "box centre on screen");
					// A point inside the box, clear of its lines and corners: the
					// one nearest the centre.
					QPointF inside(std::numeric_limits<double>::quiet_NaN(), 0);
					for (int y = 0; y < camera->height(); y += 4) {
						for (int x = 0; x < camera->width(); x += 4) {
							const QPointF candidate(x, y);
							if ((!finitePoint(inside) || length(candidate - centre) < length(inside - centre))
								&& lens->hitTest(candidate, cameraFrame, camera->font()) == 6) {
								inside = candidate;
							}
						}
					}
					// The zone round the box's centre always moves, at any text scale:
					// the nearest move lies well inside the box as the camera shows it.
					QPointF least(1e9, 1e9), most(-1e9, -1e9);
					for (int corner = 0; corner < 8; ++corner) {
						const QPointF screen = cameraFrame.project({corner & 1 ? 96.0 : 0.0, corner & 2 ? 32.0 : 0.0, corner & 4 ? 32.0 : 0.0, true});
						least = {std::min(least.x(), screen.x()), std::min(least.y(), screen.y())};
						most = {std::max(most.x(), screen.x()), std::max(most.y(), screen.y())};
					}
					const double smaller = std::min(most.x() - least.x(), most.y() - least.y());
					expect(finitePoint(inside) && length(inside - centre) <= 0.35 * smaller,
						"a move zone near the box's centre in the camera",
						QStringLiteral("%1 px from the centre of a box %2 px across at text scale %3").arg(length(inside - centre)).arg(smaller).arg(cameraFrame.handleScale));
					if (expect(finitePoint(inside) && camera->beginObjectTransform(inside), "a camera move begins inside the box")) {
						lens->updateDrag(inside + QPointF(0, -40), Qt::AltModifier);
						const auto lifted = lens->translation();
						expect(lens->transformKind() == LevelObjectTransformKind::Move && near(lifted.x, 0) && near(lifted.y, 0) && lifted.z > 0
								&& near(std::remainder(lifted.z, 16), 0),
							"Alt lifts the move on the grid", text(lifted));
						// The camera's manipulator commits through the same move.
						lens->finish(true);
						if (expect(until([&] { return bytes() != baseline; }), "the camera move applies")) {
							const auto expected = reference([lifted](LevelMapDocument* document, QString* problem) {
								LevelObjectTranslateRequest request;
								request.delta = lifted;
								request.textures = {false, false};
								return translateLevelMapObjects(document, request, nullptr, problem);
							});
							expect(bytes() == expected && shell.levelDocument().undoStack.size() == depth + 1, "the camera move equals the core's in one undo step");
							undo->trigger();
							expect(bytes() == baseline, "the camera move undoes");
						}
					}
				}

				// The Shear Tool ends the Transform tool, and its camera handles
				// slide the top within its plane and a side up with Alt.
				shearTool->trigger();
				expect(shearTool->isChecked() && top->shearMode() && !transform->isChecked() && action(QStringLiteral("levelTool-select"))->isChecked()
						&& camera->selectionShearMode() && !camera->selectionResizeBoxHandles(),
					"the Shear Tool replaces the Transform tool and gives the camera shear handles");
				if (expect(until([&] { return camera->isEnabled(); }) && tests::settleModelViewport(*camera), "camera ready for shear")) {
					const auto handles = camera->selectionResizeHandles();
					int topFace = -1, sideFace = -1;
					for (int id = 0; id < kBoxResizeHandles; ++id) {
						if (!finitePoint(handles[static_cast<size_t>(id)])) { continue; }
						expect(id < kBoxResizeFaceHandles, "only the faces shear");
						const auto sides = boxResizeHandleSides(id);
						if (sides[2] > 0) { topFace = id; }
						if (sides[0] != 0 && sideFace < 0) { sideFace = id; }
					}
					if (expect(topFace >= 0 && camera->beginSelectionResize(handles[static_cast<size_t>(topFace)]), "the top face's shear begins")) {
						const QPointF from = handles[static_cast<size_t>(topFace)];
						camera->updateSelectionResize(from + QPointF(30, 12));
						const auto offset = camera->selectionShearOffset();
						expect(camera->selectionShearPreviewEdgeCount() == 48 && near(offset[2], 0) && (offset[0] != 0 || offset[1] != 0)
								&& near(std::remainder(offset[0], 16), 0) && near(std::remainder(offset[1], 16), 0) && bytes() == baseline,
							"the top slides within its plane on the grid, previewed",
							QStringLiteral("%1 %2 %3, %4 edges").arg(offset[0]).arg(offset[1]).arg(offset[2]).arg(camera->selectionShearPreviewEdgeCount()));
						camera->finishSelectionResize(true);
						if (expect(until([&] { return bytes() != baseline; }), "the camera shear applies")) {
							const auto expected = reference([offset](LevelMapDocument* document, QString* problem) {
								LevelObjectShearRequest request;
								request.axis = 2;
								request.side = 1;
								request.offset = {offset[0], offset[1], offset[2], true};
								request.textures = {false, false};
								return shearLevelMapObjects(document, request, nullptr, problem);
							});
							expect(bytes() == expected && shell.levelDocument().undoStack.size() == depth + 1, "the camera shear equals the core in one undo step");
							undo->trigger();
							expect(bytes() == baseline, "the camera shear undoes");
						}
					}
					expect(until([&] { return camera->isEnabled() && camera->selectionShearMode(); }) && tests::settleModelViewport(*camera), "camera ready again");
					const auto again = camera->selectionResizeHandles();
					if (expect(sideFace >= 0 && finitePoint(again[static_cast<size_t>(sideFace)]) && camera->beginSelectionResize(again[static_cast<size_t>(sideFace)]),
							"a side face's shear begins")) {
						// Sideways first, then Alt keeps that slide and goes on up from there.
						const QPointF from = again[static_cast<size_t>(sideFace)];
						QPointF along = from + QPointF(40, 0);
						camera->updateSelectionResize(along);
						auto sideways = camera->selectionShearOffset();
						if (near(sideways[0], 0) && near(sideways[1], 0)) {
							along = from + QPointF(0, 40);
							camera->updateSelectionResize(along);
							sideways = camera->selectionShearOffset();
						}
						camera->updateSelectionResize(along, Qt::AltModifier);
						const auto switched = camera->selectionShearOffset();
						camera->updateSelectionResize(along + QPointF(0, -100), Qt::AltModifier);
						const auto offset = camera->selectionShearOffset();
						expect(near(switched[0], sideways[0]) && near(switched[1], sideways[1]) && near(switched[2], 0), "Alt keeps the slide so far",
							QStringLiteral("%1 %2 %3, at %4,%5 of %6x%7, %8").arg(switched[0]).arg(switched[1]).arg(switched[2]).arg(along.x()).arg(along.y())
								.arg(camera->width()).arg(camera->height()).arg(camera->accessibleDescription().right(160)));
						expect(near(sideways[2], 0) && (sideways[0] != 0 || sideways[1] != 0) && near(offset[0], sideways[0]) && near(offset[1], sideways[1])
								&& offset[2] != 0 && camera->accessibleDescription().contains(QStringLiteral("Shear")),
							"a side face slides sideways, and Alt keeps that and slides it up",
							QStringLiteral("%1 %2 %3 then %4 %5 %6, at %7,%8 of %9x%10").arg(sideways[0]).arg(sideways[1]).arg(sideways[2]).arg(offset[0]).arg(offset[1])
								.arg(offset[2]).arg(along.x()).arg(along.y()).arg(camera->width()).arg(camera->height()));
						camera->finishSelectionResize(false);
						expect(bytes() == baseline, "a cancelled camera shear changes nothing");
					}
				}
				transform->trigger();
				expect(transform->isChecked() && !shearTool->isChecked() && !top->shearMode() && !camera->selectionShearMode(),
					"the Transform tool ends the Shear Tool");

				// Select or Transform: Select from Transform, Transform from Select,
				// and Select from another tool; Move and Shear Components in a
				// component mode.
				toggle->trigger();
				expect(!transform->isChecked() && action(QStringLiteral("levelTool-select"))->isChecked(), "Select or Transform goes to Select");
				toggle->trigger();
				expect(transform->isChecked() && action(QStringLiteral("levelTool-transform"))->isChecked(), "and from Select to Transform");
				rotate->trigger();
				toggle->trigger();
				expect(!rotate->isChecked() && action(QStringLiteral("levelTool-select"))->isChecked(), "and from Rotate to Select");
				if (auto* vertex = action(QStringLiteral("map.vertexTool")); expect(vertex != nullptr, "Edit Vertices command")) {
					vertex->trigger();
					toggle->trigger();
					expect(action(QStringLiteral("map.componentShearTool"))->isChecked(), "in a component mode it picks Shear Components");
					toggle->trigger();
					expect(action(QStringLiteral("map.componentMoveTool"))->isChecked(), "then Move Components");
					action(QStringLiteral("levelTool-select"))->trigger();
				}

				// A locked object in the selection: no manipulator.
				transform->trigger();
				pick(top, {brush(1), brush(2)});
				expect(until([&] { return !gizmo->available(); }) && !camera->objectGizmo()->available() && transform->isChecked(), "a locked object hides the manipulator");
				transform->trigger();
				expect(!transform->isChecked() && gizmo->tool() != LevelComponentTool::Transform && action(QStringLiteral("levelTool-select"))->isChecked(),
					"choosing it again returns to Select");
				if (radiant || trenchBroom) {
					shell.activateWindow();
					top->setFocus(Qt::OtherFocusReason);
					expect(until([&] { return top->hasFocus(); }), "plan view focused");
					if (radiant) {
						// Q switches between Select and Transform from a plan view.
						QTest::keyClick(top, Qt::Key_Q);
						expect(transform->isChecked() && action(QStringLiteral("levelTool-transform"))->isChecked(), "Q turns the Transform tool on");
						QTest::keyClick(top, Qt::Key_Q);
						expect(!transform->isChecked() && action(QStringLiteral("levelTool-select"))->isChecked(), "Q again returns to Select");
					} else {
						// G turns TrenchBroom's Shear Tool on and off.
						QTest::keyClick(top, Qt::Key_G);
						expect(shearTool->isChecked() && top->shearMode(), "G turns the Shear Tool on");
						QTest::keyClick(top, Qt::Key_G);
						expect(!shearTool->isChecked() && !top->shearMode(), "G again turns it off");
					}
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
