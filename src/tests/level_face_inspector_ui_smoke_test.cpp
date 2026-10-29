// Offscreen checks of the Surfaces tab's Face section and Show Face
// Inspector, TrenchBroom's Face inspector, under the TrenchBroom profile at
// 100% and at 200% text with the high-visibility theme, an expanding
// translator and right-to-left layout, and under NetRadiant Custom. The
// command is labelled, in the palette, a menu and the Tools tab; it shows the
// Surfaces tab at its Face section and never hides it. TrenchBroom's Ctrl+3
// shows it from anywhere in Levels while Ctrl+3 still switches to Models
// elsewhere; NetRadiant Custom keeps Ctrl+3 for its prisms. The fields show
// the targeted faces' offset, scale and rotation, "Mixed" where they differ,
// wait with nothing targeted or a hidden brush, and set every targeted face
// as the shared service does, one undo step each. One picked face shows in
// the view: dragging its U stretch knob halves its U scale about the pivot,
// its turn knob turns it about the pivot, a drag inside moves it, arrow keys
// move and ] turns it, each one undo step, while Ctrl+Z still undoes the
// map; the pivot snaps to the face's points without editing the map.

#include "app/application_shell.h"
#include "app/level_surface_tools.h"
#include "app/level_uv_view.h"
#include "app/map_viewport.h"
#include "app/studio_actions.h"
#include "app/studio_sidebar.h"
#include "app/studio_theme.h"
#include "core/level_face_attributes.h"
#include "core/studio_semantics.h"
#include "tests/level_material_test_helpers.h"
#include "tests/level_surface_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFont>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#ifdef VIBESTUDIO_HAVE_QTEST
#include <QTest>
#endif

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

LevelTextureValues valuesOf(const LevelMapDocument& document, int face)
{
	LevelTextureValues values;
	levelTextureValues(document.brushes.first().faces[face], {}, &values);
	return values;
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
	QString error;
	// Classic Quake II faces: offset 7, 9, scale -0.5, 2 and rotation 17.
	const QString path = temp.filePath(QStringLiteral("faces.map"));
	expect(tests::putMaterialFile(path, tests::surfaceFixture(QStringLiteral("classic"))), "fixture");
	{
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(QStringLiteral("map.showFaceInspector"), &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(),
			"palette entry for Show Face Inspector");
	}

	struct Pass {
		const char* profile;
		int scale;
	};
	for (const Pass& pass : {Pass {"trenchbroom", 100}, Pass {"trenchbroom", 200}, Pass {"netradiant-custom", 100}}) {
		phase = QStringLiteral("%1 %2%").arg(QLatin1String(pass.profile)).arg(pass.scale);
		std::cout << "Face inspector UI " << qPrintable(phase) << std::endl;
		const bool trenchBroom = QLatin1String(pass.profile) == QLatin1String("trenchbroom");
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
			QComboBox* engine = nullptr;
			for (auto* combo : shell.findChildren<QComboBox*>()) {
				if (combo->findData(QStringLiteral("idtech3")) >= 0 && combo->findData(QStringLiteral("idtech1")) >= 0) {
					engine = combo;
				}
			}
			if (engine) {
				engine->setCurrentIndex(engine->findData(QStringLiteral("idtech2")));
			}
			shell.show();
			shell.openPathFromCommandLine(path);
			if (!expect(until([&] { return shell.levelDocument().sourcePath == path && !shell.levelDocument().brushes.isEmpty(); }), "map ready")) {
				return 1;
			}
			const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
			action(QStringLiteral("shell.mode.levels"))->trigger();
			if (!expect(until([&] { return shell.findChildren<MapViewport*>().size() >= 3; }), "plan views")) {
				return 1;
			}
			MapViewport* top = nullptr;
			for (auto* pane : shell.findChildren<MapViewport*>()) {
				if (pane->projection() == MapViewportProjection::TopXY) {
					top = pane;
				}
			}
			auto* tools = static_cast<LevelSurfaceTools*>(shell.findChild<QScrollArea*>(QStringLiteral("levelSurfaceTools")));
			StudioSidebar* sidebar = nullptr;
			for (auto* candidate : shell.findChildren<StudioSidebar*>()) {
				if (candidate->indexOfPage(QStringLiteral("surfaces")) >= 0) {
					sidebar = candidate;
				}
			}
			auto* stack = shell.findChild<QStackedWidget*>(QStringLiteral("modeStack"));
			QAction* inspector = action(QStringLiteral("map.showFaceInspector"));
			QAction* undo = action(QStringLiteral("map.undo"));
			if (!expect(top && tools && sidebar && stack && inspector && undo, "top view, Surfaces tab, mode stack and commands")) {
				return 1;
			}
			const auto mode = [&] { return static_cast<StudioMode>(stack->currentIndex()); };
			auto* view = tools->findChild<LevelUvView*>(QStringLiteral("surfaceFaceView"));
			QDoubleSpinBox* fields[5] = {};
			const char* const names[5] = {"surfaceFaceOffsetU", "surfaceFaceOffsetV", "surfaceFaceScaleU", "surfaceFaceScaleV", "surfaceFaceRotation"};
			bool found = view != nullptr;
			for (int i = 0; i < 5; ++i) {
				fields[i] = tools->findChild<QDoubleSpinBox*>(QString::fromLatin1(names[i]));
				found &= fields[i] != nullptr;
			}
			auto* summary = tools->findChild<QLabel*>(QStringLiteral("surfaceFaceSummary"));
			if (!expect(found && summary, "the Face section's view, fields and summary")) {
				return 1;
			}
			const int brushId = shell.levelDocument().brushes.first().id;
			QVector<LevelSurfaceFace> faces;
			for (int i = 0; i < shell.levelDocument().brushes.first().faces.size(); ++i) {
				faces.append({brushId, i});
			}
			const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
			const auto select = [&](bool brush) {
				const QVector<LevelMapSelectionRef> refs = brush ? QVector<LevelMapSelectionRef> {{LevelMapSelectionKind::QuakeBrush, brushId}}
																 : QVector<LevelMapSelectionRef> {};
				top->setSelectionSet(refs);
				top->selectionSetChanged(refs);
				settle();
			};
			const auto reference = [&](const QVector<LevelSurfaceFace>& targets, const LevelFaceAttributesEdit& edit) {
				LevelMapDocument expected = shell.levelDocument();
				QString problem;
				expect(setLevelMapFaceAttributes(&expected, targets, edit, {}, nullptr, &problem), "reference edit", problem);
				return serializeLevelMap(expected).bytes;
			};
			// Runs `edit`, checks one more undo step (and the map `wanted` when
			// given), then undoes it back to the exact bytes.
			const auto check = [&](const QByteArray& wanted, const std::function<void()>& edit, const QString& label,
								   const std::function<bool()>& also = {}) {
				const QByteArray start = bytes();
				const auto depth = shell.levelDocument().undoStack.size();
				edit();
				settle();
				expect((wanted.isEmpty() || bytes() == wanted) && bytes() != start && shell.levelDocument().undoStack.size() == depth + 1,
					"the edit is one undo step", label + QLatin1Char(' ') + shell.statusBar()->currentMessage());
				if (also) {
					expect(also(), "and does what it should", label);
				}
				undo->trigger();
				settle();
				expect(bytes() == start && shell.levelDocument().undoStack.size() == depth, "undo restores the exact map", label);
			};

			phase = QStringLiteral("%1 %2%: command").arg(QLatin1String(pass.profile)).arg(pass.scale);
			bool inMenu = false;
			for (auto* owner : inspector->associatedObjects()) {
				inMenu |= qobject_cast<QMenu*>(owner) != nullptr;
			}
			auto* tile = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-map.showFaceInspector"));
			expect(!inspector->text().isEmpty() && !inspector->statusTip().isEmpty() && inMenu && tile && QAccessible::queryAccessibleInterface(tile),
				"Show Face Inspector is labelled, in a menu and on the Tools tab");
			const QKeySequence ctrl3(QStringLiteral("Ctrl+3"));
			expect(trenchBroom ? inspector->shortcuts().contains(ctrl3) : !inspector->shortcuts().contains(ctrl3), "TrenchBroom binds Ctrl+3",
				QKeySequence::listToString(inspector->shortcuts()));
			expect(action(QStringLiteral("shell.mode.models"))->shortcuts().contains(ctrl3), "Models keeps Ctrl+3");
			// Shows the tab at the Face section, and never hides it.
			shell.showLevelSidebarTab(QStringLiteral("inspector"));
			settle();
			inspector->trigger();
			settle();
			SidebarPage* page = sidebar->page(QStringLiteral("surfaces"));
			SidebarSection* section = page ? page->section(QStringLiteral("surfaces.face")) : nullptr;
			expect(section && section->isExpanded() && sidebar->currentPageId() == QStringLiteral("surfaces") && !sidebar->isFolded() && view->isVisible(),
				"Show Face Inspector shows the Surfaces tab at its open Face section", sidebar->currentPageId());
			if (section) {
				// A folded section opens again.
				section->setExpanded(false);
				inspector->trigger();
				settle();
				expect(section->isExpanded() && sidebar->currentPageId() == QStringLiteral("surfaces") && view->isVisible(),
					"pressing it again keeps the tab and opens the section");
			}
			action(QStringLiteral("shell.mode.workspace"))->trigger();
			settle();
			inspector->trigger();
			expect(until([&] { return mode() == StudioMode::Levels && sidebar->currentPageId() == QStringLiteral("surfaces"); }, 5000),
				"from another mode it opens Levels at the Face section");

			phase = QStringLiteral("%1 %2%: fields").arg(QLatin1String(pass.profile)).arg(pass.scale);
			select(false);
			expect(!fields[0]->isEnabled() && !view->hasSource() && !summary->text().isEmpty(), "with nothing targeted the fields wait", summary->text());
			select(true);
			const double shown[5] = {7, 9, -0.5, 2, 17};
			bool values = true;
			for (int i = 0; i < 5; ++i) {
				values &= fields[i]->isEnabled() && fields[i]->value() == shown[i] && !fields[i]->accessibleName().isEmpty()
					&& fields[i]->focusPolicy() != Qt::NoFocus && fields[i]->layoutDirection() == Qt::LeftToRight;
			}
			expect(values && summary->text().contains(QLatin1Char('6')) && !view->hasSource(), "six faces' shared values; no single face to view",
				summary->text());
			{
				LevelFaceAttributesEdit edit;
				edit.setShiftU = true;
				edit.shiftU = 16;
				check(reference(faces, edit), [&] { fields[0]->setValue(16); }, QStringLiteral("offset U on six faces"));
				expect(until([&] { return fields[0]->value() == 7; }, 5000), "the field follows undo", QString::number(fields[0]->value()));
				edit = {};
				edit.setRotation = true;
				edit.rotation = 45;
				check(reference(faces, edit), [&] { fields[4]->setValue(45); }, QStringLiteral("rotation on six faces"));
				edit = {};
				edit.setScaleV = true;
				edit.scaleV = 0.25;
				check(reference(faces, edit), [&] { fields[3]->setValue(0.25); }, QStringLiteral("scale V on six faces"));
			}
			// A value one face lacks shows as Mixed.
			{
				LevelFaceAttributesEdit edit;
				edit.setShiftV = true;
				edit.shiftV = 40;
				LevelSurfaceEditPlan plan;
				expect(prepareLevelFaceAttributesEdit(shell.levelDocument(), {faces.first()}, edit, {}, &plan, &error) && shell.applyLevelSurfaceEdit(plan, &error),
					"one face's offset V", error);
				// The section follows the edit; the selection stays, since a
				// TrenchBroom profile would record a reselection as an undo step.
				expect(until([&] { return fields[1]->value() == fields[1]->minimum(); }, 5000) && fields[1]->text() == fields[1]->specialValueText()
						&& fields[0]->value() == 7,
					"a differing offset shows as Mixed", fields[1]->text());
				// "Mixed" is a marker, not a number to step from.
				const auto steps = shell.levelDocument().undoStack.size();
				fields[1]->stepBy(1);
				fields[1]->stepBy(-1);
				settle();
				expect(fields[1]->value() == fields[1]->minimum() && shell.levelDocument().undoStack.size() == steps,
					"stepping a Mixed field changes nothing", fields[1]->text());
				const auto depth = shell.levelDocument().undoStack.size();
				undo->trigger();
				settle();
				expect(shell.levelDocument().undoStack.size() + 1 == depth && fields[1]->value() == 9, "undo takes the one face's offset back");
			}
			// Hidden brushes are not targets.
			action(QStringLiteral("map.hideSelection"))->trigger();
			expect(until([&] { return !fields[0]->isEnabled(); }, 5000), "a hidden brush is not a target");
			action(QStringLiteral("map.showAll"))->trigger();
			settle();

			phase = QStringLiteral("%1 %2%: view").arg(QLatin1String(pass.profile)).arg(pass.scale);
			select(true);
			if (top->componentMode() != MapViewport::ComponentMode::Faces) {
				action(QStringLiteral("map.faceTool"))->trigger();
			}
			expect(top->setPickedComponents({{{LevelMapSelectionKind::QuakeBrush, brushId}, LevelBrushComponent::Face, {0}}}), "pick the first face");
			top->componentPicksChanged(top->pickedComponentCount());
			inspector->trigger();
			expect(until([&] { return view->hasSource(); }, 5000) && view->layout().target.faceIndex == 0 && view->handlesEnabled(),
				"one picked face shows in the view", view->summary());
			const QPointF handles[5] = {view->handlePosition(LevelUvView::Handle::Pivot), view->handlePosition(LevelUvView::Handle::Turn),
				view->handlePosition(LevelUvView::Handle::StretchU), view->handlePosition(LevelUvView::Handle::StretchV),
				view->handlePosition(LevelUvView::Handle::StretchBoth)};
			bool visible = true;
			for (const QPointF& handle : handles) {
				visible &= std::isfinite(handle.x()) && std::isfinite(handle.y());
			}
			QAccessibleInterface* face = QAccessible::queryAccessibleInterface(view);
			expect(visible && face && !view->accessibleName().isEmpty() && view->accessibleDescription().contains(QStringLiteral("Pivot"))
					&& view->focusPolicy() != Qt::NoFocus && view->height() >= 160,
				"the handles are drawn, and the view is named and describes the pivot", view->accessibleDescription());
			if (const QString grab = qEnvironmentVariable("VIBESTUDIO_FACE_INSPECTOR_GRAB"); !grab.isEmpty()) {
				tools->grab().save(QDir(grab).filePath(QStringLiteral("face-%1-%2.png").arg(QLatin1String(pass.profile)).arg(pass.scale)));
			}
#ifdef VIBESTUDIO_HAVE_QTEST
			const LevelMapVec3 pivot = view->pivotWorld();
			QPointF start;
			levelFaceTexelAt(shell.levelDocument().brushes.first().faces.first(), {}, pivot, &start);
			const auto keptPivot = [&] {
				QPointF now;
				return levelFaceTexelAt(shell.levelDocument().brushes.first().faces.first(), {}, pivot, &now) && std::abs(now.x() - start.x()) < 0.01
					&& std::abs(now.y() - start.y()) < 0.01;
			};
			const auto drag = [&](QPointF from, QPointF to) {
				QTest::mousePress(view, Qt::LeftButton, {}, from.toPoint());
				QTest::mouseMove(view, ((from + to) / 2).toPoint());
				QTest::mouseMove(view, to.toPoint());
				QTest::mouseRelease(view, Qt::LeftButton, {}, to.toPoint());
				settle();
			};
			// Stretch U to twice the distance from the pivot: half the scale.
			const LevelTextureValues before = valuesOf(shell.levelDocument(), 0);
			check({}, [&] { drag(handles[2], handles[0] + (handles[2] - handles[0]) * 2); }, QStringLiteral("stretch U"), [&] {
				const LevelTextureValues after = valuesOf(shell.levelDocument(), 0);
				return std::abs(after.scaleU - before.scaleU / 2) < 0.01 && after.scaleV == before.scaleV && keptPivot();
			});
			// A quarter turn of the knob about the pivot.
			const QPointF arm = handles[1] - handles[0];
			check({}, [&] { drag(handles[1], handles[0] + QPointF(-arm.y(), arm.x())); }, QStringLiteral("turn"), [&] {
				const LevelTextureValues after = valuesOf(shell.levelDocument(), 0);
				return std::abs(after.rotation - before.rotation) > 1 && after.scaleU == before.scaleU && keptPivot();
			});
			// A drag inside the face moves the texture.
			QPolygonF outline;
			for (const QPointF& texel : view->result().uv) {
				outline << view->texelToScreen(texel);
			}
			const QPointF inside = outline.boundingRect().center();
			check({}, [&] { drag(inside, inside + QPointF(24, 0)); }, QStringLiteral("move"), [&] {
				const LevelTextureValues after = valuesOf(shell.levelDocument(), 0);
				return after.shiftU != before.shiftU && after.shiftV == before.shiftV && after.rotation == before.rotation;
			});
			// Keys: an arrow moves one texel, ] turns, and Ctrl+Z is the map's undo.
			shell.activateWindow();
			expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
			view->setFocus(Qt::OtherFocusReason);
			expect(until([&] { return view->hasFocus(); }), "the view takes focus");
			{
				const QByteArray original = bytes();
				const auto depth = shell.levelDocument().undoStack.size();
				QTest::keyClick(view, Qt::Key_Right);
				settle();
				expect(std::abs(valuesOf(shell.levelDocument(), 0).shiftU - (before.shiftU + 1)) < 1e-9 && shell.levelDocument().undoStack.size() == depth + 1,
					"Right moves the texture one texel in one undo step");
				// The move took the pivot's texel along; the turn keeps it there.
				levelFaceTexelAt(shell.levelDocument().brushes.first().faces.first(), {}, pivot, &start);
				view->setFocus(Qt::OtherFocusReason);
				QTest::keyClick(view, Qt::Key_BracketRight);
				settle();
				expect(valuesOf(shell.levelDocument(), 0).rotation != before.rotation && shell.levelDocument().undoStack.size() == depth + 2 && keptPivot(),
					"] turns it about the pivot in one undo step");
				view->setFocus(Qt::OtherFocusReason);
				QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier);
				QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier);
				expect(until([&] { return bytes() == original; }, 5000), "Ctrl+Z in the view undoes the map");
			}
			// The pivot snaps to a point and edits nothing.
			{
				const QByteArray original = bytes();
				const QPointF corner = view->texelToScreen(view->result().uv.value(2));
				drag(view->handlePosition(LevelUvView::Handle::Pivot), corner + QPointF(3, -3));
				const LevelMapVec3 moved = view->pivotWorld();
				const LevelMapVec3 wanted = view->layout().points.value(2);
				expect(std::abs(moved.x - wanted.x) < 1e-6 && std::abs(moved.y - wanted.y) < 1e-6 && std::abs(moved.z - wanted.z) < 1e-6 && bytes() == original,
					"the pivot snaps to the face's point without editing the map");
			}
			// TrenchBroom's Ctrl+3 shows the Face section from a view; elsewhere
			// Ctrl+3 switches to Models.
			if (trenchBroom) {
				shell.showLevelSidebarTab(QStringLiteral("inspector"));
				top->setFocus(Qt::OtherFocusReason);
				expect(until([&] { return top->hasFocus(); }), "the 2D view takes focus");
				QTest::keyClick(top, Qt::Key_3, Qt::ControlModifier);
				expect(until([&] { return sidebar->currentPageId() == QStringLiteral("surfaces"); }, 5000) && mode() == StudioMode::Levels,
					"Ctrl+3 in a view shows the Face section and stays in Levels", sidebar->currentPageId());
				action(QStringLiteral("shell.mode.workspace"))->trigger();
				settle();
				QWidget* elsewhere = QApplication::focusWidget() ? QApplication::focusWidget() : static_cast<QWidget*>(&shell);
				QTest::keyClick(elsewhere, Qt::Key_3, Qt::ControlModifier);
				expect(until([&] { return mode() == StudioMode::Models; }, 5000), "outside Levels Ctrl+3 still opens Models");
				action(QStringLiteral("shell.mode.levels"))->trigger();
				settle();
			}
#endif
			// Every edit was undone, so closing asks nothing; should a check have
			// failed and left one, the question is answered so the test ends.
			expect(shell.levelDocument().editState != QStringLiteral("modified"), "every edit was undone",
				QString::number(shell.levelDocument().undoStack.size()));
			QTimer discard;
			discard.setInterval(50);
			QObject::connect(&discard, &QTimer::timeout, &shell, [] {
				if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
					for (QAbstractButton* button : box->buttons()) {
						if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
							button->click();
						}
					}
				}
			});
			discard.start();
			shell.close();
			discard.stop();
		}
		if (pass.scale == 200) {
			app.removeTranslator(&expanded);
			app.setLayoutDirection(Qt::LeftToRight);
		}
	}
	QEventLoop drain;
	QTimer::singleShot(200, &drain, &QEventLoop::quit);
	drain.exec();
	std::cout << (ok ? "Face inspector UI checks passed" : "Face inspector UI checks failed") << std::endl;
	return ok ? 0 : 1;
}
