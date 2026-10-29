// Offscreen checks of the camera's UV tool, NetRadiant Custom's G, under the
// NetRadiant Custom profile at 100% and at 200% text with the high-visibility
// theme, an expanding translator and right-to-left layout, and under
// TrenchBroom. The command is labelled, in the palette, the Surfaces menu,
// the Tools tab and on the Levels tool bar; G turns it on and off in the
// NetRadiant Custom profile only. A click on a face in the camera makes it
// the tool's surface. Dragging inside the face moves its texture, the ring
// turns it about the pivot (which keeps its texel), a repeat line stretches
// it, each one undo step that matches the shared service byte for byte and
// undoes exactly, with the camera showing the texture where the drag puts
// it; Escape during a drag leaves no trace; a classic face refuses a skew;
// the pivot snaps to a corner without editing the map; the camera's resize
// box gives way to the tool; a click on a face in a locked layer leaves the
// tool waiting with the reason. On a patch, a control point, a row and an
// Alt-drag skew change its texture coordinates, one undo step each, and a
// patch that becomes hidden leaves the tool waiting.

#include "app/application_shell.h"
#include "app/level_camera_uv_tool.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "app/studio_theme.h"
#include "core/level_patch.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_uv_tool.h"
#include "core/studio_semantics.h"
#include "tests/level_material_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QLineEdit>
#include <QFontMetricsF>
#include <QLineF>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QVBoxLayout>
#ifdef VIBESTUDIO_HAVE_QTEST
#include <QTest>
#endif

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;

namespace {

bool ok = true;
QString phase;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << qPrintable(phase) << ": " << label << ": " << qPrintable(detail) << std::endl;
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

void settle(int milliseconds = 50)
{
	QEventLoop loop;
	QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
	loop.exec();
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

bool finitePoint(QPointF p)
{
	return std::isfinite(p.x()) && std::isfinite(p.y());
}

// A brush and a standing patch with the same 128 by 64 material.
QByteArray fixture()
{
	LevelMapDocument created;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake3");
	create.starterRoom = false;
	QString error;
	createLevelMap(create, &created, &error);
	expect(addLevelMapBoxBrush(&created, {-64, -64, 0, true}, {64, 64, 128, true}, QStringLiteral("studio/grid"), nullptr, &error), "brush", error);
	// A second brush, left of the first, on a locked layer.
	QString layer;
	expect(addLevelMapBoxBrush(&created, {-192, -64, 0, true}, {-96, 64, 128, true}, QStringLiteral("studio/grid"), nullptr, &error)
			&& createLevelSceneNode(&created, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
			&& assignLevelSceneObjects(&created, layer, {levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush, created.brushes.last().id})}, &error)
			&& setLevelSceneLocked(&created, layer, true, &error),
		"locked brush", error);
	LevelPatchCreateRequest request;
	request.plane = QStringLiteral("xz");
	request.center = {224, -64, 64, true};
	request.size = {128, 128, 128, true};
	request.texture = QStringLiteral("studio/grid");
	LevelMapPatch patch;
	expect(createLevelPatch(request, &patch, &error) && addLevelMapPatch(&created, patch, nullptr, &error), "patch", error);
	return serializeLevelMap(created).bytes;
}

} // namespace

int main(int argc, char** argv)
{
	// Owned offscreen Qt widgets and semantic events only; no system input.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "temporary directory")) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	const QString assets = temp.filePath(QStringLiteral("assets"));
	const QString path = temp.filePath(QStringLiteral("uv.map"));
	expect(tests::putMaterialFile(QDir(assets).filePath(QStringLiteral("textures/studio/grid.png")), tests::materialImage(128, 64))
			&& tests::putMaterialFile(path, fixture()),
		"fixture files");
	{
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(QStringLiteral("map.uvTool"), &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(),
			"palette entry for the UV Tool");
	}
#ifdef VIBESTUDIO_HAVE_QTEST
	// A focus-layer key the user remapped to a printable key, as TrenchBroom's
	// Ctrl+3 for the Face inspector could be, still types into a field inside
	// its surface, and runs the command from elsewhere in the surface.
	phase = QStringLiteral("focus claims");
	{
		QWidget host;
		auto* layout = new QVBoxLayout(&host);
		auto* surface = new QWidget;
		auto* inside = new QVBoxLayout(surface);
		auto* field = new QLineEdit;
		auto* other = new QPushButton(QStringLiteral("Other"));
		inside->addWidget(field);
		inside->addWidget(other);
		layout->addWidget(surface);
		StudioCommandRegistry registry(&host);
		int fired = 0;
		StudioCommandRegistration command;
		command.commandId = QStringLiteral("test.focusClaim");
		command.label = QStringLiteral("Focus Claim");
		command.group = StudioCommandGroup::View;
		command.handler = [&fired] { ++fired; };
		registry.registerCommand(command);
		registry.setShortcutScopes(command.commandId, {surface});
		registry.setShortcutScopeClaims(command.commandId, true, QString::fromLatin1(StudioCommandRegistry::kShortcutFocusClaims));
		registry.setUserShortcuts({{command.commandId, {QStringLiteral("M")}}});
		host.show();
		host.activateWindow();
		until([&] { return QApplication::activeWindow() == &host; }, 5000);
		field->setFocus(Qt::OtherFocusReason);
		until([&] { return field->hasFocus(); }, 5000);
		QTest::keyClick(field, Qt::Key_M);
		expect(field->text() == QStringLiteral("m") && fired == 0, "a remapped printable key still types into a field inside the surface",
			field->text());
		other->setFocus(Qt::OtherFocusReason);
		until([&] { return other->hasFocus(); }, 5000);
		QTest::keyClick(other, Qt::Key_M);
		expect(fired == 1, "elsewhere inside the surface the key runs the command", QString::number(fired));
	}
#endif

	struct Pass {
		const char* profile;
		int scale;
	};
	for (const Pass& pass : {Pass {"netradiant-custom", 100}, Pass {"netradiant-custom", 200}, Pass {"trenchbroom", 100}}) {
		phase = QStringLiteral("%1 %2%").arg(QLatin1String(pass.profile)).arg(pass.scale);
		std::cout << "Camera UV tool UI " << qPrintable(phase) << std::endl;
		const bool nrc = QLatin1String(pass.profile) == QLatin1String("netradiant-custom");
		const bool full = nrc && pass.scale == 100;
		settings.setSelectedEditorProfileId(QString::fromLatin1(pass.profile));
		settings.setTextScalePercent(pass.scale);
		settings.setTheme(pass.scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
		settings.sync();
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, pass.scale));
		Expanded expanded;
		if (pass.scale == 200) {
			app.installTranslator(&expanded);
			app.setLayoutDirection(Qt::RightToLeft);
		}
		{
			ApplicationShell shell;
			shell.resize(pass.scale == 100 ? 1650 : 2400, pass.scale == 100 ? 1000 : 1500);
			shell.show();
			shell.openPathFromCommandLine(assets);
			shell.openPathFromCommandLine(path);
			const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
			action(QStringLiteral("shell.mode.levels"))->trigger();
			action(QStringLiteral("map.layout.four-views"))->trigger();
			auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
			if (!expect(camera && until([&] { return shell.levelDocument().sourcePath == path && !shell.levelDocument().patches.isEmpty(); }), "map ready")) {
				return 1;
			}
			const auto ready = [&] { return until([&] { return camera->isEnabled() && tests::settleModelViewport(*camera); }); };
			expect(ready() && until([&] { return camera->hasSkin(); }), "the camera draws the map with its material");
			QAction* tool = action(QStringLiteral("map.uvTool"));
			QAction* undo = action(QStringLiteral("map.undo"));
			if (!expect(tool && undo, "the UV Tool and Undo commands")) {
				return 1;
			}
			const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
			const auto uvTool = [&] { return shell.levelCameraUvTool(); };
			const auto send = [&](QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = {}) {
				QMouseEvent event(type, at, camera->mapToGlobal(at), button, buttons, modifiers);
				QCoreApplication::sendEvent(camera, &event);
			};
			const auto drag = [&](QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = {}) {
				send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, modifiers);
				send(QEvent::MouseMove, from + (to - from) * 0.5, Qt::NoButton, Qt::LeftButton, modifiers);
				send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, modifiers);
				send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, modifiers);
			};
			// The nearest point to `near` the tool hit-tests as `handle`.
			const auto find = [&](LevelUvToolHandle handle, QPointF near, double radius, Qt::KeyboardModifiers modifiers = {}) {
				QPointF best(std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN());
				double nearest = std::numeric_limits<double>::infinity();
				const LevelCameraUvTool* current = uvTool();
				if (!current || !finitePoint(near)) {
					return best;
				}
				for (double y = near.y() - radius; y <= near.y() + radius; y += 3) {
					for (double x = near.x() - radius; x <= near.x() + radius; x += 3) {
						const QPointF at(x, y);
						if (!QRectF(camera->rect()).adjusted(4, 4, -4, -4).contains(at) || current->hitAt(at, modifiers).handle != handle) {
							continue;
						}
						const double distance = QLineF(at, near).length();
						if (distance < nearest) {
							nearest = distance;
							best = at;
						}
					}
				}
				return best;
			};
			// Runs `edit`, checks one more undo step and the service's own
			// result for the tool's edit, then undoes it back to the exact bytes.
			const auto check = [&](const std::function<void()>& edit, const QString& label, const std::function<bool()>& also = {}) {
				const QByteArray start = bytes();
				const auto depth = shell.levelDocument().undoStack.size();
				const LevelMapDocument before = shell.levelDocument();
				const LevelUvLayout layout = uvTool() ? uvTool()->layout() : LevelUvLayout {};
				edit();
				settle();
				expect(bytes() != start && shell.levelDocument().undoStack.size() == depth + 1, "the drag is one undo step",
					label + QLatin1Char(' ') + shell.statusBar()->currentMessage());
				if (uvTool()) {
					LevelMapDocument expected = before;
					QString problem;
					expect(applyLevelUvToolEdit(&expected, layout, uvTool()->edit(), nullptr, &problem) && serializeLevelMap(expected).bytes == bytes(),
						"the drag matches the shared service byte for byte", label + QLatin1Char(' ') + problem);
				}
				if (also) {
					expect(also(), "and does what it should", label);
				}
				undo->trigger();
				settle();
				expect(bytes() == start && shell.levelDocument().undoStack.size() == depth, "undo restores the exact map", label);
				ready();
			};

			phase = QStringLiteral("%1 %2%: command").arg(QLatin1String(pass.profile)).arg(pass.scale);
			bool inMenu = false;
			for (auto* owner : tool->associatedObjects()) {
				inMenu |= qobject_cast<QMenu*>(owner) != nullptr;
			}
			auto* tile = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-map.uvTool"));
			auto* button = shell.findChild<QToolButton*>(QStringLiteral("levelToolButton-uv"));
			expect(!tool->text().isEmpty() && !tool->statusTip().isEmpty() && tool->isCheckable() && tool->isEnabled() && inMenu && tile
					&& QAccessible::queryAccessibleInterface(tile) && button && button->defaultAction() && button->defaultAction()->isVisible()
					&& !button->accessibleName().isEmpty()
					&& button->focusPolicy() != Qt::NoFocus,
				"the UV Tool is labelled, enabled, in a menu, on the Tools tab and on the tool bar");
			const QKeySequence g(Qt::Key_G);
			expect(nrc ? tool->shortcuts().contains(g) : !tool->shortcuts().contains(g), "NetRadiant Custom binds G", QKeySequence::listToString(tool->shortcuts()));

			phase = QStringLiteral("%1 %2%: on").arg(QLatin1String(pass.profile)).arg(pass.scale);
			camera->setCameraView({60, -460, 64}, 90, 0);
			ready();
			camera->setFocus(Qt::OtherFocusReason);
#ifdef VIBESTUDIO_HAVE_QTEST
			if (nrc) {
				shell.activateWindow();
				until([&] { return QApplication::activeWindow() == &shell; }, 5000);
				camera->setFocus(Qt::OtherFocusReason);
				QTest::keyClick(camera, Qt::Key_G);
			} else {
				tool->trigger();
			}
#else
			tool->trigger();
#endif
			expect(until([&] { return tool->isChecked() && uvTool() != nullptr; }, 5000) && button->isChecked(), "the tool turns on, with its button");
			expect(uvTool() && !uvTool()->hasSource() && camera->accessibleDescription().contains(QStringLiteral("UV Tool")),
				"with nothing picked it waits, and the camera says so", camera->accessibleDescription());
			// A click on the front face makes it the tool's surface.
			const auto& geometry = shell.levelDocument().brushes.first();
			const auto solved = solveBrushGeometry(geometry.faces);
			int front = -1;
			for (int i = 0; i < solved.faces.size(); ++i) {
				if (solved.faces[i].isValid() && solved.faces[i].plane.normalY < -0.9) {
					front = i;
				}
			}
			QPointF centre;
			camera->projectToView({0, -64, 64}, &centre);
			send(QEvent::MouseButtonPress, centre, Qt::LeftButton, Qt::LeftButton);
			send(QEvent::MouseButtonRelease, centre, Qt::LeftButton, Qt::NoButton);
			expect(until([&] { return uvTool() && uvTool()->hasSource(); }, 5000) && uvTool()->layout().target.kind == LevelMaterialKind::BrushFace
					&& uvTool()->layout().target.faceIndex == front,
				"a click on a face makes it the tool's surface", uvTool() ? uvTool()->describe() : QString());
			ready();
			if (!uvTool() || !uvTool()->hasSource()) {
				return 1;
			}
			// The UV tool replaces the camera's resize box, as NetRadiant Custom's does.
			const auto resizeHandleShown = [&] {
				const auto handles = camera->selectionResizeHandles();
				return std::any_of(handles.cbegin(), handles.cend(), [](const QPointF& point) { return finitePoint(point); });
			};
			expect(!shell.levelDocument().selection.isEmpty() && !resizeHandleShown(), "the selected brush shows no resize box while the UV tool is on");

			phase = QStringLiteral("%1 %2%: drags").arg(QLatin1String(pass.profile)).arg(pass.scale);
			// Inside the face, away from the lines: the texture follows the pointer.
			const QPointF inside = find(LevelUvToolHandle::Move, centre + QPointF(-30, 25), 60);
			const double pickRadius = std::max(6.0, QFontMetricsF(camera->font()).height() * 0.5);
			if (expect(finitePoint(inside), "a point inside the face moves the texture")) {
				bool previewed = false;
				check(
					[&] {
						send(QEvent::MouseButtonPress, inside, Qt::LeftButton, Qt::LeftButton);
						send(QEvent::MouseMove, inside + QPointF(12, 0), Qt::NoButton, Qt::LeftButton);
						send(QEvent::MouseMove, inside + QPointF(24, 6), Qt::NoButton, Qt::LeftButton);
						previewed = camera->texCoordPreviewActive();
						send(QEvent::MouseButtonRelease, inside + QPointF(24, 6), Qt::LeftButton, Qt::NoButton);
					},
					QStringLiteral("move"), [&] { return uvTool()->edit().handle == LevelUvToolHandle::Move; });
				expect(previewed, "the camera shows the texture where the drag puts it");
			}
			// The ring turns it about the pivot.
			QPointF pivot = uvTool()->texelScreen(uvTool()->pivotTexel());
			const QPointF ring = find(LevelUvToolHandle::Turn, pivot + QPointF(0, -60), 70);
			if (expect(finitePoint(pivot) && finitePoint(ring), "the ring is drawn about the pivot")) {
				const QPointF arm = ring - pivot;
				const LevelMapVec3 pivotWorld = uvTool()->pivotWorld();
				const QPointF pivotTexel = uvTool()->pivotTexel();
				check([&] { drag(ring, pivot + QPointF(-arm.y(), arm.x())); }, QStringLiteral("turn"), [&] {
					QPointF now;
					const bool kept = levelFaceTexelAt(shell.levelDocument().brushes.first().faces[front], uvTool()->layout().imageSize, pivotWorld, &now)
						&& QLineF(now, pivotTexel).length() < 0.01;
					return uvTool()->edit().handle == LevelUvToolHandle::Turn && std::abs(std::abs(uvTool()->edit().turn) - 90) < 3 && kept;
				});
			}
			// A repeat line stretches it.
			const QPointF line = find(LevelUvToolHandle::StretchS, centre, 120);
			if (expect(finitePoint(line), "a repeat line along V stretches along U")) {
				check([&] { drag(line, pivot + (line - pivot) * 0.6); }, QStringLiteral("stretch"), [&] {
					return uvTool()->edit().handle == LevelUvToolHandle::StretchS && uvTool()->edit().stretch.x() != 1 && uvTool()->edit().stretch.y() == 1;
				});
			}
			if (full) {
				// Escape during a drag leaves no trace.
				const QByteArray before = bytes();
				const auto depth = shell.levelDocument().undoStack.size();
				send(QEvent::MouseButtonPress, inside, Qt::LeftButton, Qt::LeftButton);
				send(QEvent::MouseMove, inside + QPointF(30, 0), Qt::NoButton, Qt::LeftButton);
				QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
				QCoreApplication::sendEvent(camera, &escape);
				send(QEvent::MouseButtonRelease, inside + QPointF(30, 0), Qt::LeftButton, Qt::NoButton);
				settle();
				expect(bytes() == before && shell.levelDocument().undoStack.size() == depth && !camera->texCoordPreviewActive(),
					"Escape during a drag leaves no trace");
				// A classic face takes no skew.
				const QPointF skewLine = find(LevelUvToolHandle::SkewS, centre, 120, Qt::AltModifier);
				if (expect(finitePoint(skewLine), "Alt and a line skews")) {
					drag(skewLine, skewLine + QPointF(30, 0), Qt::AltModifier);
					settle();
					expect(bytes() == before && shell.levelDocument().undoStack.size() == depth
							&& shell.statusBar()->currentMessage().contains(QStringLiteral("Valve 220")),
						"a classic face refuses a skew and says why", shell.statusBar()->currentMessage());
				}
				ready();
				// The pivot snaps to a corner and edits nothing.
				pivot = uvTool()->texelScreen(uvTool()->pivotTexel());
				const QPointF corner = uvTool()->texelScreen(uvTool()->layout().uv.value(1));
				drag(pivot, corner + QPointF(pickRadius * 0.4, -pickRadius * 0.4));
				settle();
				expect(QLineF(uvTool()->pivotTexel(), uvTool()->layout().uv.value(1)).length() < 1e-6 && bytes() == before
						&& shell.levelDocument().undoStack.size() == depth,
					"the pivot snaps to a corner without editing the map");
				// With nothing picked or selected the tool waits and says why.
				if (auto* top = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"))) {
					top->setSelectionSet({});
					top->selectionSetChanged({});
				}
				expect(until([&] { return !uvTool()->hasSource(); }, 5000) && !uvTool()->reason().isEmpty()
						&& camera->accessibleDescription().contains(uvTool()->reason()),
					"without a surface the tool waits and says why", uvTool()->reason());
				ready();

				phase = QStringLiteral("%1 %2%: refusals").arg(QLatin1String(pass.profile)).arg(pass.scale);
				// A click on a face of the brush in the locked layer picks it, and
				// the tool waits and says why instead of taking it.
				{
					QPointF onLocked;
					camera->projectToView({-144, -64, 64}, &onLocked);
					const QByteArray start = bytes();
					const auto depth = shell.levelDocument().undoStack.size();
					send(QEvent::MouseButtonPress, onLocked, Qt::LeftButton, Qt::LeftButton);
					send(QEvent::MouseButtonRelease, onLocked, Qt::LeftButton, Qt::NoButton);
					expect(until([&] { return !uvTool()->hasSource() && uvTool()->reason().contains(QStringLiteral("locked")); }, 5000)
							&& camera->accessibleDescription().contains(uvTool()->reason()),
						"a click on a locked brush's face leaves the tool waiting with the reason", uvTool()->reason());
					expect(uvTool()->hitAt(onLocked, Qt::NoModifier).handle == LevelUvToolHandle::None && bytes() == start
							&& shell.levelDocument().undoStack.size() == depth,
						"the locked face offers nothing to drag and is unchanged");
				}
				ready();

				phase = QStringLiteral("%1 %2%: patch").arg(QLatin1String(pass.profile)).arg(pass.scale);
				const int patchId = shell.levelDocument().patches.first().id;
				QPointF onPatch;
				camera->projectToView({224, -64, 64}, &onPatch);
				send(QEvent::MouseButtonPress, onPatch, Qt::LeftButton, Qt::LeftButton);
				send(QEvent::MouseButtonRelease, onPatch, Qt::LeftButton, Qt::NoButton);
				if (expect(until([&] { return uvTool()->hasSource() && uvTool()->layout().isPatch(); }, 5000), "a click on a patch makes it the tool's surface",
						uvTool()->describe())) {
					ready();
					const LevelUvLayout layout = uvTool()->layout();
					const int middle = layout.columns + 1;
					const QPointF point = find(LevelUvToolHandle::PatchPoint, uvTool()->texelScreen(layout.uv.value(middle)), 10);
					if (expect(finitePoint(point), "a control point is held")) {
						check([&] { drag(point, point + QPointF(20, 0)); }, QStringLiteral("patch point"), [&] {
							const LevelUvToolEdit& edit = uvTool()->edit();
							return edit.handle == LevelUvToolHandle::PatchPoint && edit.points == QVector<int> {middle} && edit.move != QPointF();
						});
					}
					const QPointF rowMiddle = (uvTool()->texelScreen(layout.uv.value(0)) + uvTool()->texelScreen(layout.uv.value(1))) / 2;
					const QPointF row = find(LevelUvToolHandle::PatchRow, rowMiddle, 12);
					if (expect(finitePoint(row), "a row is held")) {
						check([&] { drag(row, row + QPointF(0, 18)); }, QStringLiteral("patch row"), [&] {
							return uvTool()->edit().handle == LevelUvToolHandle::PatchRow && uvTool()->edit().points.size() == layout.columns;
						});
					}
					QPointF patchCentre;
					camera->projectToView({224, -64, 64}, &patchCentre);
					const QPointF skew = find(LevelUvToolHandle::SkewS, patchCentre, 140, Qt::AltModifier);
					if (expect(finitePoint(skew), "Alt and a line skews a patch")) {
						check([&] { drag(skew, skew + QPointF(24, 0), Qt::AltModifier); }, QStringLiteral("patch skew"),
							[&] { return uvTool()->edit().handle == LevelUvToolHandle::SkewS && uvTool()->edit().skew.x() != 0; });
					}
					expect(patchId == layout.target.objectId, "the patch is the one in the map");
					// A selected patch that becomes hidden leaves the tool waiting
					// with nothing to drag. (A picked face cannot be hidden: Hide
					// Selection waits for an object selection.)
					QAction* hide = action(QStringLiteral("map.hideSelection"));
					QAction* showAll = action(QStringLiteral("map.showAll"));
					if (expect(hide && hide->isEnabled() && showAll, "Hide Selection is available for the selected patch")) {
						hide->trigger();
						expect(until([&] { return !uvTool()->hasSource() && !uvTool()->reason().isEmpty(); }, 5000)
								&& uvTool()->hitAt(patchCentre, Qt::NoModifier).handle == LevelUvToolHandle::None,
							"a hidden patch leaves the tool waiting with nothing to drag", uvTool()->reason());
						ready();
						showAll->trigger();
						ready();
						expect(shell.levelDocument().hiddenObjects.isEmpty(), "Show All Hidden brings it back");
					}
				}
			}

			phase = QStringLiteral("%1 %2%: off").arg(QLatin1String(pass.profile)).arg(pass.scale);
			if (const QString grab = qEnvironmentVariable("VIBESTUDIO_CAMERA_UV_TOOL_GRAB"); !grab.isEmpty()) {
				camera->grab().save(QDir(grab).filePath(QStringLiteral("camera-uv-%1-%2.png").arg(QLatin1String(pass.profile)).arg(pass.scale)));
			}
#ifdef VIBESTUDIO_HAVE_QTEST
			if (nrc) {
				shell.activateWindow();
				until([&] { return QApplication::activeWindow() == &shell; }, 5000);
				camera->setFocus(Qt::OtherFocusReason);
				QTest::keyClick(camera, Qt::Key_G);
			} else {
				tool->trigger();
			}
#else
			tool->trigger();
#endif
			expect(until([&] { return !tool->isChecked() && uvTool() == nullptr; }, 5000) && !button->isChecked()
					&& !camera->accessibleDescription().contains(QStringLiteral("UV Tool")),
				"choosing it again returns to Select");
		}
		if (pass.scale == 200) {
			app.removeTranslator(&expanded);
			app.setLayoutDirection(Qt::LeftToRight);
		}
	}

	if (!ok) {
		return 1;
	}
	std::cout << "camera UV tool UI smoke test passed" << std::endl;
	return 0;
}
