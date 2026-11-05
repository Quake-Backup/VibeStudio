// Offscreen checks of TrenchBroom's Convert Selection to Patches and
// NetRadiant Custom's Deform… in the real shell, under the NetRadiant Custom
// profile at 100% and at 200% text with the high-visibility theme, an
// expanding translator and right-to-left layout, and under TrenchBroom. Both
// commands are labelled, in the palette, a menu and the Tools tab, and enable
// with what they act on. Convert replaces the selected box with the shared
// service's six patches in one undo step that undoes exactly, selects them and
// turns on Edit Vertices; a locked brush is refused with a reason.
// TrenchBroom's Ctrl+P converts from a view, but never from a field that
// types in Levels, where Go to File keeps it, as it does with nothing to
// convert; the shortcut registry's typing guard behind that is checked on its
// own first. NetRadiant Custom keeps Ctrl+P for Go to File and binds Cap
// Patch to Shift+C. Deform's dialog offers
// upstream's maximum (64) and Z, a labelled seed, and makes the service's
// result for that seed in one undo step; it remembers the last maximum and
// axis, and Cancel changes nothing.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "app/studio_theme.h"
#include "core/level_patch.h"
#include "core/level_patch_convert.h"
#include "core/level_patch_tools.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/studio_semantics.h"
#include "tests/level_material_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QVBoxLayout>
#ifdef VIBESTUDIO_HAVE_QTEST
#include <QTest>
#endif

#include <functional>
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

class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};

// Brush 0 (a 128 cube), brush 1 on a locked layer and patch 0, a 5x3 plane,
// all with a 128 by 64 material.
QByteArray fixture()
{
	LevelMapDocument created;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake3");
	create.starterRoom = false;
	QString error;
	createLevelMap(create, &created, &error);
	expect(addLevelMapBoxBrush(&created, {-64, -64, 0, true}, {64, 64, 128, true}, QStringLiteral("studio/grid"), nullptr, &error), "brush", error);
	QString layer;
	expect(addLevelMapBoxBrush(&created, {256, -64, 0, true}, {320, 64, 128, true}, QStringLiteral("studio/grid"), nullptr, &error)
			&& createLevelSceneNode(&created, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
			&& assignLevelSceneObjects(&created, layer, {QStringLiteral("brush:1")}, &error) && setLevelSceneLocked(&created, layer, true, &error),
		"locked brush", error);
	LevelPatchCreateRequest plane;
	plane.columns = 5;
	plane.center = {-256, 0, 64, true};
	plane.size = {128, 128, 128, true};
	plane.texture = QStringLiteral("textures/studio/grid");
	LevelMapPatch patch;
	expect(createLevelPatch(plane, &patch, &error) && addLevelMapPatch(&created, patch, nullptr, &error), "patch", error);
	return serializeLevelMap(created).bytes;
}

// Triggers `action` and answers the modal dialog it opens with `handler`,
// from the dialog's own event loop. False when no dialog came up.
bool answerDialog(QAction* action, QObject* context, const std::function<void(QDialog*)>& handler)
{
	bool answered = false;
	std::function<void(QDialog*)> onModal = [&](QDialog* dialog) {
		answered = true;
		handler(dialog);
	};
	QTimer drive;
	drive.setInterval(30);
	QObject::connect(&drive, &QTimer::timeout, context, [&] {
		auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
		if (dialog && onModal) {
			auto run = std::move(onModal);
			onModal = nullptr;
			run(dialog);
		}
	});
	QTimer watchdog;
	watchdog.setSingleShot(true);
	QObject::connect(&watchdog, &QTimer::timeout, context, [] {
		if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
			dialog->reject();
		}
	});
	drive.start();
	watchdog.start(60000);
	action->trigger();
	until([&] { return !onModal && QApplication::activeModalWidget() == nullptr; }, 30000);
	drive.stop();
	watchdog.stop();
	return answered;
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
	const QString path = temp.filePath(QStringLiteral("convert.map"));
	expect(tests::putMaterialFile(QDir(assets).filePath(QStringLiteral("textures/studio/grid.png")), tests::materialImage(128, 64))
			&& tests::putMaterialFile(path, fixture()),
		"fixture files");
	const QStringList ids {QStringLiteral("map.convertToPatches"), QStringLiteral("map.deformPatch")};
	for (const QString& id : ids) {
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(id, &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(), "palette entry", id);
	}

#ifdef VIBESTUDIO_HAVE_QTEST
	// The registry's typing guard: in a field that types inside a guarded
	// surface no editing command takes a key, even one the field ignores
	// (Ctrl+P, Ctrl+D, Return); the window-wide owner of a shared key still
	// runs, and so do claims of other groups (Ctrl+3). Elsewhere in the
	// surface the editing commands run as before.
	phase = QStringLiteral("typing guard");
	{
		QWidget host;
		auto* layout = new QVBoxLayout(&host);
		auto* surface = new QWidget;
		auto* inside = new QVBoxLayout(surface);
		auto* field = new QLineEdit;
		auto* other = new QPushButton(QStringLiteral("Other"));
		auto* outside = new QPushButton(QStringLiteral("Outside"));
		inside->addWidget(field);
		inside->addWidget(other);
		layout->addWidget(surface);
		layout->addWidget(outside);
		StudioCommandRegistry registry(&host);
		QHash<QString, int> fired;
		const auto add = [&](const char* id, StudioCommandGroup group) {
			StudioCommandRegistration command;
			command.commandId = QString::fromLatin1(id);
			command.label = command.commandId;
			command.group = group;
			command.handler = [&fired, id] { ++fired[QString::fromLatin1(id)]; };
			registry.registerCommand(command);
		};
		add("test.goTo", StudioCommandGroup::File);
		add("test.convert", StudioCommandGroup::Edit);
		add("test.show", StudioCommandGroup::View);
		add("test.duplicate", StudioCommandGroup::Edit);
		add("test.commit", StudioCommandGroup::Edit);
		const QString focusLayer = QString::fromLatin1(StudioCommandRegistry::kShortcutFocusClaims);
		for (const char* id : {"test.convert", "test.show", "test.duplicate", "test.commit"}) {
			registry.setShortcutScopes(QString::fromLatin1(id), {surface});
		}
		registry.setShortcutScopeClaims(QStringLiteral("test.convert"), true, focusLayer);
		registry.setShortcutScopeClaims(QStringLiteral("test.show"), true, focusLayer);
		registry.setUserShortcuts({{QStringLiteral("test.goTo"), {QStringLiteral("Ctrl+P")}}, {QStringLiteral("test.convert"), {QStringLiteral("Ctrl+P")}},
			{QStringLiteral("test.show"), {QStringLiteral("Ctrl+3")}}, {QStringLiteral("test.duplicate"), {QStringLiteral("Ctrl+D")}},
			{QStringLiteral("test.commit"), {QStringLiteral("Return")}}});
		registry.setShortcutTypingGuards({surface});
		// A window-wide action needs a widget, as the shell's menus give it.
		host.addAction(registry.action(QStringLiteral("test.goTo")));
		int returned = 0;
		QObject::connect(field, &QLineEdit::returnPressed, &host, [&returned] { ++returned; });
		host.show();
		host.activateWindow();
		until([&] { return QApplication::activeWindow() == &host; }, 5000);
		const auto press = [&](QWidget* target, Qt::Key key, Qt::KeyboardModifiers modifiers) {
			target->setFocus(Qt::OtherFocusReason);
			until([&] { return target->hasFocus(); }, 5000);
			QTest::keyClick(target, key, modifiers);
		};
		const auto counts = [&] {
			return QStringLiteral("goTo %1, convert %2, show %3, duplicate %4, commit %5, returned %6")
				.arg(fired.value(QStringLiteral("test.goTo")))
				.arg(fired.value(QStringLiteral("test.convert")))
				.arg(fired.value(QStringLiteral("test.show")))
				.arg(fired.value(QStringLiteral("test.duplicate")))
				.arg(fired.value(QStringLiteral("test.commit")))
				.arg(returned);
		};
		press(field, Qt::Key_P, Qt::ControlModifier);
		expect(fired.value(QStringLiteral("test.goTo")) == 1 && fired.value(QStringLiteral("test.convert")) == 0,
			"in a field an editing claim leaves Ctrl+P to its window-wide owner", counts());
		press(field, Qt::Key_D, Qt::ControlModifier);
		press(field, Qt::Key_Return, Qt::NoModifier);
		expect(fired.value(QStringLiteral("test.duplicate")) == 0 && fired.value(QStringLiteral("test.commit")) == 0 && returned == 1,
			"in a field scoped editing keys do not run, and Return reaches the field", counts());
		press(field, Qt::Key_3, Qt::ControlModifier);
		expect(fired.value(QStringLiteral("test.show")) == 1, "a claim of another group still runs from the field", counts());
		press(other, Qt::Key_P, Qt::ControlModifier);
		press(other, Qt::Key_D, Qt::ControlModifier);
		press(other, Qt::Key_Return, Qt::NoModifier);
		expect(fired.value(QStringLiteral("test.convert")) == 1 && fired.value(QStringLiteral("test.goTo")) == 1 && fired.value(QStringLiteral("test.duplicate")) == 1
				&& fired.value(QStringLiteral("test.commit")) == 1,
			"elsewhere in the surface the editing keys run", counts());
		press(outside, Qt::Key_P, Qt::ControlModifier);
		expect(fired.value(QStringLiteral("test.goTo")) == 2 && fired.value(QStringLiteral("test.convert")) == 1, "outside the surface Ctrl+P is the window-wide command's",
			counts());
	}
#endif

	struct Pass {
		const char* profile;
		int scale;
	};
	bool first = true;
	for (const Pass& pass : {Pass {"netradiant-custom", 100}, Pass {"netradiant-custom", 200}, Pass {"trenchbroom", 100}}) {
		phase = QStringLiteral("%1 %2%").arg(QLatin1String(pass.profile)).arg(pass.scale);
		std::cout << "Patch convert and deform UI " << qPrintable(phase) << std::endl;
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
			shell.show();
			shell.openPathFromCommandLine(assets);
			shell.openPathFromCommandLine(path);
			const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
			action(QStringLiteral("shell.mode.levels"))->trigger();
			action(QStringLiteral("map.layout.four-views"))->trigger();
			auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
			auto* top = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
			if (!expect(camera && top && until([&] { return shell.levelDocument().sourcePath == path && shell.levelDocument().patches.size() == 1; }),
					"map ready")) {
				return 1;
			}
			expect(until([&] { return camera->isEnabled() && tests::settleModelViewport(*camera) && camera->hasSkin(); }), "the camera loads the material");
			QAction* undo = action(QStringLiteral("map.undo"));
			const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
			const auto select = [&](const QVector<LevelMapSelectionRef>& refs) {
				top->setSelectionSet(refs);
				top->selectionSetChanged(top->selectionSet());
			};

			phase = QStringLiteral("%1 %2%: commands").arg(QLatin1String(pass.profile)).arg(pass.scale);
			select({});
			for (const QString& id : ids) {
				QAction* command = action(id);
				bool inMenu = false;
				for (auto* owner : command ? command->associatedObjects() : QList<QObject*> {}) {
					inMenu |= qobject_cast<QMenu*>(owner) != nullptr;
				}
				auto* tile = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-") + id);
				expect(command && !command->text().isEmpty() && !command->statusTip().isEmpty() && !command->isEnabled() && inMenu && tile
						&& QAccessible::queryAccessibleInterface(tile),
					"the command is labelled, disabled with nothing to act on, in a menu and on the Tools tab", id);
			}
			expect(action(QStringLiteral("map.deformPatch"))->text().contains(QChar(0x2026)), "Deform Patch… asks first");
			// The keys: TrenchBroom's Ctrl+P, NetRadiant Custom's Shift+C.
			const QKeySequence ctrlP(QStringLiteral("Ctrl+P"));
			expect(trenchBroom == action(QStringLiteral("map.convertToPatches"))->shortcuts().contains(ctrlP), "TrenchBroom binds Ctrl+P to Convert");
			expect(!trenchBroom == action(QStringLiteral("map.capPatch"))->shortcuts().contains(QKeySequence(QStringLiteral("Shift+C"))),
				"NetRadiant Custom binds Cap Patch to Shift+C");

			phase = QStringLiteral("%1 %2%: convert").arg(QLatin1String(pass.profile)).arg(pass.scale);
			select({{LevelMapSelectionKind::QuakeBrush, 0}});
			expect(until([&] { return action(QStringLiteral("map.convertToPatches"))->isEnabled(); }, 5000), "Convert enables for a selected brush");
			const QByteArray start = bytes();
			const auto depth = shell.levelDocument().undoStack.size();
			LevelMapDocument expected = shell.levelDocument();
			LevelPatchConvertRequest request;
			request.imageSize = [](const QString&) { return QSize(128, 64); };
			QString problem;
			expect(convertLevelMapFacesToPatches(&expected, {}, {0}, request, nullptr, &problem), "the service converts the box", problem);
			action(QStringLiteral("map.convertToPatches"))->trigger();
			expect(until([&] { return shell.levelDocument().undoStack.size() == depth + 1; }, 5000) && bytes() == serializeLevelMap(expected).bytes
					&& shell.levelDocument().selection.size() == 6 && action(QStringLiteral("map.vertexTool"))->isChecked(),
				"Convert replaces the box with the service's six patches, selected, and turns on Edit Vertices", shell.statusBar()->currentMessage());
			action(QStringLiteral("map.vertexTool"))->trigger();
			undo->trigger();
			expect(until([&] { return bytes() == start; }, 5000) && shell.levelDocument().undoStack.size() == depth, "undo restores the box exactly");
#ifdef VIBESTUDIO_HAVE_QTEST
			if (trenchBroom) {
				// Ctrl+P in a view converts; Go to File keeps it elsewhere.
				expect(action(QStringLiteral("shell.goToFile")) && action(QStringLiteral("shell.goToFile"))->shortcuts().contains(ctrlP),
					"Go to File keeps Ctrl+P");
				auto* quickOpen = shell.findChild<QDialog*>(QStringLiteral("quickOpen"));
				const auto goToFileOpens = [&](QWidget* target, const char* label) {
					shell.activateWindow();
					until([&] { return QApplication::activeWindow() == &shell; }, 5000);
					target->setFocus(Qt::OtherFocusReason);
					until([&] { return target->hasFocus(); }, 5000);
					const QByteArray before = bytes();
					const auto keyDepth = shell.levelDocument().undoStack.size();
					QTest::keyClick(target, Qt::Key_P, Qt::ControlModifier);
					expect(quickOpen && until([&] { return quickOpen->isVisible(); }, 5000) && bytes() == before
							&& shell.levelDocument().undoStack.size() == keyDepth,
						label, shell.statusBar()->currentMessage());
					if (quickOpen && quickOpen->isVisible()) {
						quickOpen->reject();
						until([&] { return !quickOpen->isVisible(); }, 5000);
					}
				};
				// With nothing to convert, the 2D view's Ctrl+P is Go to File.
				select({});
				goToFileOpens(top, "with nothing to convert, Ctrl+P in a view opens Go to File");
				// A field that types in Levels never converts: Ctrl+P there is Go to File.
				auto* mapPath = shell.findChild<QLineEdit*>(QStringLiteral("levelMapPath"));
				select({{LevelMapSelectionKind::QuakeBrush, 0}});
				expect(mapPath && until([&] { return action(QStringLiteral("map.convertToPatches"))->isEnabled(); }, 5000), "the map path field");
				if (mapPath) {
					goToFileOpens(mapPath, "Ctrl+P in a Levels text field opens Go to File and converts nothing");
				}
				select({{LevelMapSelectionKind::QuakeBrush, 0}});
				shell.activateWindow();
				until([&] { return QApplication::activeWindow() == &shell; }, 5000);
				top->setFocus(Qt::OtherFocusReason);
				expect(until([&] { return top->hasFocus() && action(QStringLiteral("map.convertToPatches"))->isEnabled(); }, 5000), "the 2D view takes focus",
					QStringLiteral("focus %1, enabled %2, vertex %3, selected %4, focus widget %5")
						.arg(top->hasFocus())
						.arg(action(QStringLiteral("map.convertToPatches"))->isEnabled())
						.arg(action(QStringLiteral("map.vertexTool"))->isChecked())
						.arg(shell.levelDocument().selection.size())
						.arg(QApplication::focusWidget() ? QApplication::focusWidget()->objectName() : QStringLiteral("none")));
				// TrenchBroom's selection changes are undo steps of their own.
				const auto keyDepth = shell.levelDocument().undoStack.size();
				QTest::keyClick(top, Qt::Key_P, Qt::ControlModifier);
				const bool converted = until([&] { return shell.levelDocument().undoStack.size() == keyDepth + 1; }, 5000);
				expect(converted && shell.levelDocument().patches.size() == 7, "TrenchBroom's Ctrl+P converts from a view", shell.statusBar()->currentMessage());
				if (action(QStringLiteral("map.vertexTool"))->isChecked()) {
					action(QStringLiteral("map.vertexTool"))->trigger();
				}
				if (converted) {
					undo->trigger();
				}
				expect(until([&] { return bytes() == start; }, 5000), "undo again");
			}
#endif
			// A locked brush is refused with a reason.
			select({{LevelMapSelectionKind::QuakeBrush, 1}});
			until([&] { return action(QStringLiteral("map.convertToPatches"))->isEnabled(); }, 5000);
			const auto lockedDepth = shell.levelDocument().undoStack.size();
			action(QStringLiteral("map.convertToPatches"))->trigger();
			expect(bytes() == start && shell.levelDocument().undoStack.size() == lockedDepth && !shell.statusBar()->currentMessage().isEmpty(),
				"a locked brush is refused and the map is unchanged", shell.statusBar()->currentMessage());
			if (action(QStringLiteral("map.vertexTool"))->isChecked()) {
				action(QStringLiteral("map.vertexTool"))->trigger();
			}

			phase = QStringLiteral("%1 %2%: deform").arg(QLatin1String(pass.profile)).arg(pass.scale);
			int patchId = shell.levelDocument().patches.first().id;
			select({{LevelMapSelectionKind::QuakePatch, patchId}});
			QAction* deform = action(QStringLiteral("map.deformPatch"));
			expect(until([&] { return deform->isEnabled(); }, 5000) && !action(QStringLiteral("map.convertToPatches"))->isEnabled(),
				"Deform enables for a patch and Convert does not");
			const auto deformDepth = shell.levelDocument().undoStack.size();
			bool offered = false;
			bool cancelled = answerDialog(deform, &shell, [&](QDialog* dialog) {
				auto* maximum = dialog->findChild<QSpinBox*>(QStringLiteral("levelDeformMaximum"));
				auto* axis = dialog->findChild<QComboBox*>(QStringLiteral("levelDeformAxis"));
				auto* seed = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("levelDeformSeed"));
				auto* reseed = dialog->findChild<QPushButton*>(QStringLiteral("levelDeformNewSeed"));
				auto* note = dialog->findChild<QLabel*>(QStringLiteral("levelDeformNote"));
				offered = maximum && axis && seed && reseed && note && maximum->minimum() == -9999 && maximum->maximum() == 9999
					&& (first ? maximum->value() == 64 && axis->currentIndex() == 2 : maximum->value() == 16 && axis->currentIndex() == 0)
					&& axis->count() == 3 && seed->decimals() == 0 && seed->minimum() == 0.0 && seed->maximum() == 4294967295.0
					&& !maximum->accessibleName().isEmpty() && !axis->accessibleName().isEmpty() && !seed->accessibleName().isEmpty()
					&& !reseed->accessibleName().isEmpty() && !note->text().isEmpty() && !dialog->windowTitle().isEmpty()
					&& dialog->layoutDirection() == app.layoutDirection();
				dialog->reject();
			});
			expect(cancelled && offered && bytes() == start, "the dialog offers upstream's maximum and axis, labelled, and Cancel changes nothing");
			LevelPatchDeformRequest deformRequest;
			deformRequest.maximum = 16;
			deformRequest.axis = 0;
			deformRequest.seed = 4000000000u;
			LevelMapDocument deformed = shell.levelDocument();
			expect(deformLevelMapPatches(&deformed, {patchId}, deformRequest, nullptr, nullptr, &problem), "the service deforms", problem);
			const bool asked = answerDialog(deform, &shell, [&](QDialog* dialog) {
				auto* maximum = dialog->findChild<QSpinBox*>(QStringLiteral("levelDeformMaximum"));
				auto* axis = dialog->findChild<QComboBox*>(QStringLiteral("levelDeformAxis"));
				auto* seed = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("levelDeformSeed"));
				auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("levelDeformButtons"));
				if (!maximum || !axis || !seed || !buttons) {
					dialog->reject();
					return;
				}
				maximum->setValue(16);
				axis->setCurrentIndex(0);
				// Above QSpinBox's 2147483647, as map deform-patch can draw.
				seed->setValue(4000000000.0);
				buttons->button(QDialogButtonBox::Ok)->click();
			});
			expect(asked && until([&] { return shell.levelDocument().undoStack.size() == deformDepth + 1; }, 5000)
					&& bytes() == serializeLevelMap(deformed).bytes,
				"Deform makes the service's result for the seed in one step", shell.statusBar()->currentMessage());
			undo->trigger();
			expect(until([&] { return bytes() == start; }, 5000), "undo restores the patch exactly");
			first = false;
			select({});
		}
		if (pass.scale == 200) {
			app.removeTranslator(&expanded);
			app.setLayoutDirection(Qt::LeftToRight);
		}
	}
	std::cout << (ok ? "patch convert and deform UI checks passed" : "patch convert and deform UI checks FAILED") << std::endl;
	return ok ? 0 : 1;
}
