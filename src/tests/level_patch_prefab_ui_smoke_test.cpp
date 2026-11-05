// Offscreen checks of NetRadiant Custom's patch prefabs in the real shell,
// under the NetRadiant Custom profile at 100% and at 200% text with the
// high-visibility theme, an expanding translator and right-to-left layout,
// and under TrenchBroom. The six prefab commands are labelled, in the
// palette, a menu and the Tools tab, and enabled on a Quake III map. Patch
// Cylinder replaces the selected brush with a 9x3 patch in its bounds, mapped
// Natural by the material's loaded image, in one undo step that matches the
// shared service byte for byte and undoes exactly; a brush in a locked layer
// is refused with a reason. The Shapes tab starts patch shapes at Radiant's
// 9x3 and 9x5 densities with their own fields, offers the classic cone and a
// patch plane's columns and rows, and replaces selected patches too. Add
// Patch… (Shift+P in the NetRadiant Custom profile only) offers every shape,
// fits the selection's bounds and replaces it in one step.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "app/studio_theme.h"
#include "core/level_patch.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_shapes.h"
#include "core/studio_semantics.h"
#include "tests/level_material_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFont>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
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

// Brush 0 (a 128 cube), brush 1 on a locked layer and patch 0, a plane, all
// with a 128 by 64 material.
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
	plane.center = {-256, 0, 64, true};
	plane.size = {128, 128, 128, true};
	plane.texture = QStringLiteral("textures/studio/grid");
	LevelMapPatch patch;
	expect(createLevelPatch(plane, &patch, &error) && addLevelMapPatch(&created, patch, nullptr, &error), "patch", error);
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
	const QString path = temp.filePath(QStringLiteral("prefabs.map"));
	expect(tests::putMaterialFile(QDir(assets).filePath(QStringLiteral("textures/studio/grid.png")), tests::materialImage(128, 64))
			&& tests::putMaterialFile(path, fixture()),
		"fixture files");
	const QStringList prefabs {QStringLiteral("map.patchCylinder"), QStringLiteral("map.patchSquareCylinder"), QStringLiteral("map.patchEndCap"),
		QStringLiteral("map.patchBevel"), QStringLiteral("map.patchClassicCone"), QStringLiteral("map.patchSphere")};
	for (const QString& id : prefabs) {
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(id, &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(), "palette entry", id);
	}

	struct Pass {
		const char* profile;
		int scale;
	};
	for (const Pass& pass : {Pass {"netradiant-custom", 100}, Pass {"netradiant-custom", 200}, Pass {"trenchbroom", 100}}) {
		phase = QStringLiteral("%1 %2%").arg(QLatin1String(pass.profile)).arg(pass.scale);
		std::cout << "Patch prefab UI " << qPrintable(phase) << std::endl;
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
			auto* top = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
			if (!expect(camera && top && until([&] { return shell.levelDocument().sourcePath == path && shell.levelDocument().patches.size() == 1; }),
					"map ready")) {
				return 1;
			}
			// The camera's loaded image gives the Natural mapping its size.
			expect(until([&] { return camera->isEnabled() && tests::settleModelViewport(*camera) && camera->hasSkin(); }), "the camera loads the material");
			QAction* undo = action(QStringLiteral("map.undo"));
			const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
			const auto select = [&](const QVector<LevelMapSelectionRef>& refs) {
				top->setSelectionSet(refs);
				top->selectionSetChanged(top->selectionSet());
			};

			phase = QStringLiteral("%1 %2%: commands").arg(QLatin1String(pass.profile)).arg(pass.scale);
			for (const QString& id : prefabs) {
				QAction* command = action(id);
				bool inMenu = false;
				for (auto* owner : command ? command->associatedObjects() : QList<QObject*> {}) {
					inMenu |= qobject_cast<QMenu*>(owner) != nullptr;
				}
				auto* tile = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-") + id);
				expect(command && !command->text().isEmpty() && !command->statusTip().isEmpty() && command->isEnabled() && inMenu && tile
						&& QAccessible::queryAccessibleInterface(tile),
					"the prefab is labelled, enabled, in a menu and on the Tools tab", id);
			}
			QAction* addPatch = action(QStringLiteral("map.addPatch"));
			const QKeySequence shiftP(Qt::SHIFT | Qt::Key_P);
			expect(addPatch && (nrc ? addPatch->shortcuts().contains(shiftP) : !addPatch->shortcuts().contains(shiftP)),
				"NetRadiant Custom binds Shift+P to Add Patch", addPatch ? QKeySequence::listToString(addPatch->shortcuts()) : QString());

			phase = QStringLiteral("%1 %2%: cylinder").arg(QLatin1String(pass.profile)).arg(pass.scale);
			// Patch Cylinder on brush 0: the service's patch, one undo step.
			select({{LevelMapSelectionKind::QuakeBrush, 0}});
			const QByteArray start = bytes();
			const auto depth = shell.levelDocument().undoStack.size();
			LevelMapDocument expected = shell.levelDocument();
			LevelShapeRequest request;
			request.shape = QStringLiteral("patch-cylinder");
			const MapViewportProjection projection = top->projection();
			request.axis = projection == MapViewportProjection::FrontXZ ? 1 : (projection == MapViewportProjection::SideZY ? 0 : 2);
			request.sides = 4;
			request.bands = 2;
			request.texture = QStringLiteral("studio/grid");
			request.patchImageSize = QSize(128, 64);
			levelMapPatchShapeBounds(expected, top->gridSize(), &request.mins, &request.maxs);
			QString problem;
			expect(replaceLevelMapSelectionWithPatchShape(&expected, request, nullptr, &problem), "the service replaces the brush", problem);
			action(QStringLiteral("map.patchCylinder"))->trigger();
			expect(until([&] { return shell.levelDocument().undoStack.size() == depth + 1; }, 5000)
					&& serializeLevelMap(expected).bytes == bytes() && shell.levelDocument().selection.size() == 1
					&& shell.levelDocument().selection.first().kind == LevelMapSelectionKind::QuakePatch,
				"Patch Cylinder replaces the brush with the service's Natural-mapped patch in one step", shell.statusBar()->currentMessage());
			undo->trigger();
			expect(until([&] { return bytes() == start; }, 5000) && shell.levelDocument().undoStack.size() == depth, "undo restores the brush exactly");

			// A brush in a locked layer is refused, and the status says why.
			// (TrenchBroom records selections as undo steps, so count after it.)
			select({{LevelMapSelectionKind::QuakeBrush, 1}});
			const auto lockedDepth = shell.levelDocument().undoStack.size();
			action(QStringLiteral("map.patchSphere"))->trigger();
			expect(bytes() == start && shell.levelDocument().undoStack.size() == lockedDepth && !shell.statusBar()->currentMessage().isEmpty(),
				"a locked brush is not replaced", shell.statusBar()->currentMessage());
			select({});

			if (full) {
				phase = QStringLiteral("%1 %2%: shapes tab").arg(QLatin1String(pass.profile)).arg(pass.scale);
				expect(shell.showLevelSidebarTab(QStringLiteral("shapes")), "the Shapes tab shows");
				auto* sphere = shell.findChild<QToolButton*>(QStringLiteral("levelShape-patch-sphere"));
				auto* classic = shell.findChild<QToolButton*>(QStringLiteral("levelShape-patch-classic-cone"));
				auto* plane = shell.findChild<QToolButton*>(QStringLiteral("levelShape-patch-plane"));
				auto* segments = shell.findChild<QSpinBox*>(QStringLiteral("levelShapePatchSegments"));
				auto* bands = shell.findChild<QSpinBox*>(QStringLiteral("levelShapePatchBands"));
				auto* columns = shell.findChild<QSpinBox*>(QStringLiteral("levelShapePatchColumns"));
				auto* rows = shell.findChild<QSpinBox*>(QStringLiteral("levelShapePatchRows"));
				auto* sides = shell.findChild<QSpinBox*>(QStringLiteral("levelShapeSides"));
				auto* add = shell.findChild<QPushButton*>(QStringLiteral("levelShapeAdd"));
				auto* replace = shell.findChild<QPushButton*>(QStringLiteral("levelShapeReplace"));
				if (expect(sphere && classic && plane && segments && bands && columns && rows && sides && add && replace, "the patch shape fields")) {
					sphere->click();
					expect(segments->isVisibleTo(&shell) && bands->isVisibleTo(&shell) && !sides->isVisibleTo(&shell) && segments->value() == 4
							&& bands->value() == 2 && !segments->accessibleName().isEmpty() && !bands->accessibleName().isEmpty(),
						"a patch sphere starts at Radiant's four segments and two bands, in its own fields");
					add->click();
					expect(until([&] { return shell.levelDocument().patches.size() == 2; }, 5000) && shell.levelDocument().patches.last().width == 9
							&& shell.levelDocument().patches.last().height == 5 && shell.levelDocument().undoStack.size() == depth + 1,
						"Add at View Centre makes Radiant's 9x5 sphere in one step");
					undo->trigger();
					expect(until([&] { return bytes() == start; }, 5000), "undo takes the sphere away");
					plane->click();
					expect(columns->isVisibleTo(&shell) && rows->isVisibleTo(&shell) && !segments->isVisibleTo(&shell), "a patch plane shows its columns and rows");
					columns->setValue(5);
					rows->setValue(7);
					add->click();
					expect(until([&] { return shell.levelDocument().patches.size() == 2; }, 5000) && shell.levelDocument().patches.last().width == 5
							&& shell.levelDocument().patches.last().height == 7,
						"the patch plane takes the chosen grid");
					undo->trigger();
					until([&] { return bytes() == start; }, 5000);
					// The classic cone replaces the selected patch in one step.
					classic->click();
					select({{LevelMapSelectionKind::QuakePatch, 0}});
					expect(replace->isEnabled(), "a patch shape can replace a selected patch");
					replace->click();
					const auto& patches = shell.levelDocument().patches;
					expect(until([&] { return shell.levelDocument().undoStack.size() == depth + 1; }, 5000) && patches.size() == 1 && patches.first().id != 0
							&& patches.first().width == 9,
						"the classic cone takes the patch's place in one step");
					undo->trigger();
					expect(until([&] { return bytes() == start; }, 5000), "undo brings the patch back");
					shell.findChild<QToolButton*>(QStringLiteral("levelShape-box"))->click();
				}

				phase = QStringLiteral("%1 %2%: add patch").arg(QLatin1String(pass.profile)).arg(pass.scale);
				select({{LevelMapSelectionKind::QuakeBrush, 0}});
				// The dialog is answered from its own event loop, as a person would.
				bool answered = false;
				std::function<void(QDialog*)> onModal = [&](QDialog* dialog) {
					auto* shape = dialog->findChild<QComboBox*>(QStringLiteral("patchShape"));
					auto* replaceBox = dialog->findChild<QCheckBox*>(QStringLiteral("patchReplaceSelection"));
					auto* sizeX = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("patchSizeX"));
					auto* generate = dialog->findChild<QPushButton*>(QStringLiteral("patchGenerate"));
					auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("patchApply"));
					answered = shape && shape->count() == 8 && replaceBox && replaceBox->isChecked() && sizeX && sizeX->value() == 128 && generate && apply;
					if (answered) {
						shape->setCurrentIndex(shape->findData(QStringLiteral("sphere")));
						generate->click();
						apply->click();
					} else {
						dialog->reject();
					}
				};
				QTimer drive;
				drive.setInterval(30);
				QObject::connect(&drive, &QTimer::timeout, &shell, [&] {
					auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
					if (dialog && onModal) {
						auto handler = std::move(onModal);
						onModal = nullptr;
						handler(dialog);
					}
				});
				QTimer watchdog;
				watchdog.setSingleShot(true);
				QObject::connect(&watchdog, &QTimer::timeout, &shell, [] {
					if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
						dialog->reject();
					}
				});
				drive.start();
				watchdog.start(60000);
#ifdef VIBESTUDIO_HAVE_QTEST
				shell.activateWindow();
				until([&] { return QApplication::activeWindow() == &shell; }, 5000);
				top->setFocus(Qt::OtherFocusReason);
				QTest::keyClick(top, Qt::Key_P, Qt::ShiftModifier);
#else
				addPatch->trigger();
#endif
				until([&] { return !onModal && QApplication::activeModalWidget() == nullptr; }, 30000);
				drive.stop();
				watchdog.stop();
				expect(answered, "Add Patch… offers every shape, fits the selection and offers to replace it");
				expect(until([&] { return shell.levelDocument().undoStack.size() == depth + 1; }, 5000) && shell.levelDocument().brushes.size() == 1
						&& shell.levelDocument().patches.size() == 2 && shell.levelDocument().patches.last().height == 5,
					"the sphere replaces the brush in one step", shell.statusBar()->currentMessage());
				undo->trigger();
				expect(until([&] { return bytes() == start; }, 5000), "undo restores the brush");
			}
		}
		if (pass.scale == 200) {
			app.removeTranslator(&expanded);
			app.setLayoutDirection(Qt::LeftToRight);
		}
	}

	if (!ok) {
		return 1;
	}
	std::cout << "patch prefab UI smoke test passed" << std::endl;
	return 0;
}
