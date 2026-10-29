// Offscreen checks of NetRadiant Custom's Exact patch shapes and Square box
// in the real shell, under the NetRadiant Custom profile at 100% and at 200%
// text with the high-visibility theme, an expanding translator and
// right-to-left layout, and under TrenchBroom. Exact Patch Cylinder…, Exact
// Patch Cone… and Exact Patch Sphere… are labelled, in the palette, a menu
// and the Tools tab, and enabled on a Quake III map. Their Patch Density
// dialog offers upstream's odd columns from 7 and rows from 3 (a sphere's
// from 5), starts at 13x3 (13x7) and then at the last choice, and builds the
// shared service's patch, ring at angle 0 and squared when asked, in place of
// the selected brush in one undo step that undoes exactly; Cancel changes
// nothing and a locked brush is refused with a reason. The Shapes tab shows
// Rows and Square for the round patch shapes only and builds a squared patch
// cylinder of the chosen rows, and Add Patch… builds one too.

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
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>

#include <cmath>
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

// Triggers `action` and answers the modal dialog it opens with `handler`,
// from the dialog's own event loop as a person would. False when no dialog
// came up.
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

// Whether each odd control of every row and column lies halfway between its
// neighbours: the patch is squared.
bool isSquared(const LevelMapPatch& patch)
{
	const auto halfway = [](const LevelMapVec3& a, const LevelMapVec3& b, const LevelMapVec3& m) {
		return std::abs((a.x + b.x) / 2 - m.x) < 1e-3 && std::abs((a.y + b.y) / 2 - m.y) < 1e-3 && std::abs((a.z + b.z) / 2 - m.z) < 1e-3;
	};
	for (int row = 0; row < patch.height; ++row) {
		for (int column = 1; column + 1 < patch.width; column += 2) {
			const int at = row * patch.width + column;
			if (!halfway(patch.controlPoints.at(at - 1), patch.controlPoints.at(at + 1), patch.controlPoints.at(at))) {
				return false;
			}
		}
	}
	for (int column = 0; column < patch.width; ++column) {
		for (int row = 1; row + 1 < patch.height; row += 2) {
			const int at = row * patch.width + column;
			if (!halfway(patch.controlPoints.at(at - patch.width), patch.controlPoints.at(at + patch.width), patch.controlPoints.at(at))) {
				return false;
			}
		}
	}
	return true;
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
	const QString path = temp.filePath(QStringLiteral("density.map"));
	expect(tests::putMaterialFile(QDir(assets).filePath(QStringLiteral("textures/studio/grid.png")), tests::materialImage(128, 64))
			&& tests::putMaterialFile(path, fixture()),
		"fixture files");
	const QStringList exact {QStringLiteral("map.patchExactCylinder"), QStringLiteral("map.patchExactCone"), QStringLiteral("map.patchExactSphere")};
	for (const QString& id : exact) {
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(id, &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(), "palette entry", id);
	}

	struct Pass {
		const char* profile;
		int scale;
	};
	bool first = true;
	for (const Pass& pass : {Pass {"netradiant-custom", 100}, Pass {"netradiant-custom", 200}, Pass {"trenchbroom", 100}}) {
		phase = QStringLiteral("%1 %2%").arg(QLatin1String(pass.profile)).arg(pass.scale);
		std::cout << "Patch density UI " << qPrintable(phase) << std::endl;
		const bool full = QLatin1String(pass.profile) == QLatin1String("netradiant-custom") && pass.scale == 100;
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
			const MapViewportProjection projection = top->projection();
			const int viewAxis = projection == MapViewportProjection::FrontXZ ? 1 : (projection == MapViewportProjection::SideZY ? 0 : 2);

			phase = QStringLiteral("%1 %2%: commands").arg(QLatin1String(pass.profile)).arg(pass.scale);
			for (const QString& id : exact) {
				QAction* command = action(id);
				bool inMenu = false;
				for (auto* owner : command ? command->associatedObjects() : QList<QObject*> {}) {
					inMenu |= qobject_cast<QMenu*>(owner) != nullptr;
				}
				auto* tile = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-") + id);
				expect(command && command->text().contains(QChar(0x2026)) && !command->statusTip().isEmpty() && command->isEnabled() && inMenu && tile
						&& QAccessible::queryAccessibleInterface(tile),
					"the Exact command is labelled as asking, enabled, in a menu and on the Tools tab", id);
			}

			phase = QStringLiteral("%1 %2%: exact cylinder").arg(QLatin1String(pass.profile)).arg(pass.scale);
			// Exact Patch Cylinder on brush 0: 9 columns, 5 rows, squared.
			select({{LevelMapSelectionKind::QuakeBrush, 0}});
			const QByteArray start = bytes();
			const auto depth = shell.levelDocument().undoStack.size();
			LevelMapDocument expected = shell.levelDocument();
			LevelShapeRequest request;
			request.shape = QStringLiteral("patch-cylinder");
			request.axis = viewAxis;
			request.sides = 4;
			request.rows = 5;
			request.patchRingStart = LevelPatchRingStart::Exact;
			request.patchSquare = true;
			request.texture = QStringLiteral("studio/grid");
			request.patchImageSize = QSize(128, 64);
			levelMapPatchShapeBounds(expected, top->gridSize(), &request.mins, &request.maxs);
			QString problem;
			expect(replaceLevelMapSelectionWithPatchShape(&expected, request, nullptr, &problem), "the service replaces the brush", problem);
			bool offered = false;
			const bool cylinderAsked = answerDialog(action(QStringLiteral("map.patchExactCylinder")), &shell, [&](QDialog* dialog) {
				auto* columns = dialog->findChild<QComboBox*>(QStringLiteral("patchDensityColumns"));
				auto* rows = dialog->findChild<QComboBox*>(QStringLiteral("patchDensityRows"));
				auto* square = dialog->findChild<QCheckBox*>(QStringLiteral("patchDensitySquare"));
				auto* note = dialog->findChild<QLabel*>(QStringLiteral("patchDensityNote"));
				auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("patchDensityButtons"));
				// Upstream's ranges; the defaults the first time, then the last choice.
				offered = columns && rows && square && note && buttons && columns->count() == 13 && columns->itemData(0).toInt() == 7
					&& columns->itemData(12).toInt() == 31 && rows->count() == 15 && rows->itemData(0).toInt() == 3
					&& (first ? columns->currentData().toInt() == 13 && rows->currentData().toInt() == 3 && !square->isChecked()
							  : columns->currentData().toInt() == 9 && rows->currentData().toInt() == 5 && square->isChecked())
					&& !dialog->windowTitle().isEmpty() && !dialog->accessibleName().isEmpty() && !columns->accessibleName().isEmpty()
					&& !rows->accessibleName().isEmpty() && !square->accessibleName().isEmpty() && !square->toolTip().isEmpty() && !note->text().isEmpty()
					&& dialog->layoutDirection() == app.layoutDirection();
				if (!offered || !buttons) {
					dialog->reject();
					return;
				}
				columns->setCurrentIndex(columns->findData(9));
				rows->setCurrentIndex(rows->findData(5));
				square->setChecked(true);
				buttons->button(QDialogButtonBox::Ok)->click();
			});
			expect(cylinderAsked && offered, "the Patch Density dialog offers upstream's columns, rows and Square, labelled",
				cylinderAsked ? QString() : QStringLiteral("no dialog"));
			expect(until([&] { return shell.levelDocument().undoStack.size() == depth + 1; }, 5000) && serializeLevelMap(expected).bytes == bytes()
					&& shell.levelDocument().selection.size() == 1 && shell.levelDocument().selection.first().kind == LevelMapSelectionKind::QuakePatch
					&& shell.levelDocument().patches.last().width == 9 && shell.levelDocument().patches.last().height == 5
					&& isSquared(shell.levelDocument().patches.last()),
				"Exact Patch Cylinder replaces the brush with the service's squared 9x5 patch in one step", shell.statusBar()->currentMessage());
			undo->trigger();
			expect(until([&] { return bytes() == start; }, 5000) && shell.levelDocument().undoStack.size() == depth, "undo restores the brush exactly");

			phase = QStringLiteral("%1 %2%: cancel and lock").arg(QLatin1String(pass.profile)).arg(pass.scale);
			// Cancel changes nothing; the sphere's rows start at 5, at 13x7.
			bool sphereOffered = false;
			const bool sphereAsked = answerDialog(action(QStringLiteral("map.patchExactSphere")), &shell, [&](QDialog* dialog) {
				auto* columns = dialog->findChild<QComboBox*>(QStringLiteral("patchDensityColumns"));
				auto* rows = dialog->findChild<QComboBox*>(QStringLiteral("patchDensityRows"));
				sphereOffered = columns && rows && rows->count() == 14 && rows->itemData(0).toInt() == 5 && columns->currentData().toInt() == 13
					&& rows->currentData().toInt() == 7;
				dialog->reject();
			});
			expect(sphereAsked && sphereOffered && bytes() == start && shell.levelDocument().undoStack.size() == depth,
				"Exact Patch Sphere offers rows from 5, starts at 13x7, and Cancel changes nothing");
			// A brush in a locked layer is refused, and the status says why.
			// (TrenchBroom records selections as undo steps, so count after it.)
			select({{LevelMapSelectionKind::QuakeBrush, 1}});
			const auto lockedDepth = shell.levelDocument().undoStack.size();
			const bool coneAsked = answerDialog(action(QStringLiteral("map.patchExactCone")), &shell, [](QDialog* dialog) {
				if (auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("patchDensityButtons"))) {
					buttons->button(QDialogButtonBox::Ok)->click();
				} else {
					dialog->reject();
				}
			});
			expect(coneAsked && bytes() == start && shell.levelDocument().undoStack.size() == lockedDepth && !shell.statusBar()->currentMessage().isEmpty(),
				"a locked brush is not replaced", shell.statusBar()->currentMessage());
			select({});

			if (full) {
				phase = QStringLiteral("%1 %2%: shapes tab").arg(QLatin1String(pass.profile)).arg(pass.scale);
				expect(shell.showLevelSidebarTab(QStringLiteral("shapes")), "the Shapes tab shows");
				auto* cylinder = shell.findChild<QToolButton*>(QStringLiteral("levelShape-patch-cylinder"));
				auto* bevel = shell.findChild<QToolButton*>(QStringLiteral("levelShape-patch-bevel"));
				auto* sphere = shell.findChild<QToolButton*>(QStringLiteral("levelShape-patch-sphere"));
				auto* rows = shell.findChild<QSpinBox*>(QStringLiteral("levelShapePatchRows"));
				auto* square = shell.findChild<QCheckBox*>(QStringLiteral("levelShapePatchSquare"));
				auto* add = shell.findChild<QPushButton*>(QStringLiteral("levelShapeAdd"));
				if (expect(cylinder && bevel && sphere && rows && square && add, "the patch shape fields")) {
					cylinder->click();
					expect(rows->isVisibleTo(&shell) && square->isVisibleTo(&shell) && !square->accessibleName().isEmpty() && !square->isChecked(),
						"a patch cylinder shows Rows and Square, unticked");
					rows->setValue(5);
					square->setChecked(true);
					add->click();
					const auto& patches = shell.levelDocument().patches;
					expect(until([&] { return patches.size() == 2; }, 5000) && patches.last().width == 9 && patches.last().height == 5
							&& isSquared(patches.last()) && shell.levelDocument().undoStack.size() == depth + 1,
						"Add at View Centre makes a squared 9x5 patch cylinder in one step");
					undo->trigger();
					expect(until([&] { return bytes() == start; }, 5000), "undo takes the cylinder away");
					sphere->click();
					expect(!rows->isVisibleTo(&shell) && square->isVisibleTo(&shell), "a patch sphere shows Square but not Rows");
					bevel->click();
					expect(!rows->isVisibleTo(&shell) && !square->isVisibleTo(&shell), "a patch bevel keeps Radiant's rows, unsquared");
					square->setChecked(false);
					rows->setValue(3);
					shell.findChild<QToolButton*>(QStringLiteral("levelShape-box"))->click();
				}

				phase = QStringLiteral("%1 %2%: add patch").arg(QLatin1String(pass.profile)).arg(pass.scale);
				select({{LevelMapSelectionKind::QuakeBrush, 0}});
				bool built = false;
				const bool patchAsked = answerDialog(action(QStringLiteral("map.addPatch")), &shell, [&](QDialog* dialog) {
					auto* shape = dialog->findChild<QComboBox*>(QStringLiteral("patchShape"));
					auto* patchRows = dialog->findChild<QSpinBox*>(QStringLiteral("patchRows"));
					auto* patchSquare = dialog->findChild<QCheckBox*>(QStringLiteral("patchSquare"));
					auto* generate = dialog->findChild<QPushButton*>(QStringLiteral("patchGenerate"));
					auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("patchApply"));
					if (!shape || !patchRows || !patchSquare || !generate || !apply) {
						dialog->reject();
						return;
					}
					shape->setCurrentIndex(shape->findData(QStringLiteral("bevel")));
					const bool bevelFixed = !patchRows->isEnabled() && !patchSquare->isEnabled();
					shape->setCurrentIndex(shape->findData(QStringLiteral("cone")));
					built = bevelFixed && patchRows->isEnabled() && patchSquare->isEnabled() && !patchSquare->accessibleName().isEmpty();
					patchRows->setValue(7);
					patchSquare->setChecked(true);
					generate->click();
					apply->click();
				});
				expect(patchAsked && built, "Add Patch… offers Rows and Square for a cone, not a bevel");
				expect(until([&] { return shell.levelDocument().undoStack.size() == depth + 1; }, 5000) && shell.levelDocument().brushes.size() == 1
						&& shell.levelDocument().patches.size() == 2 && shell.levelDocument().patches.last().height == 7
						&& isSquared(shell.levelDocument().patches.last()),
					"the squared seven-row cone replaces the brush in one step", shell.statusBar()->currentMessage());
				undo->trigger();
				expect(until([&] { return bytes() == start; }, 5000), "undo restores the brush");
			}
		}
		first = false;
		if (pass.scale == 200) {
			app.removeTranslator(&expanded);
			app.setLayoutDirection(Qt::LeftToRight);
		}
	}

	if (!ok) {
		return 1;
	}
	std::cout << "patch density UI smoke test passed" << std::endl;
	return 0;
}
