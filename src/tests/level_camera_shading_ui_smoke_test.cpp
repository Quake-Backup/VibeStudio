// View > Camera Display in the Levels page: Shade Faces in Camera, Fog in
// Camera with its start and end distances, and Grid on Camera Faces, as saved
// commands with View tab boxes kept in step, palette entries and no keys in
// the TrenchBroom and NetRadiant Custom profiles (neither binds them). Show
// Grid is checkable and hides the camera's ground and face grids; the face
// grid follows the grid size. Runs under both profiles at 100% and at 200%
// with high contrast, right-to-left and expanded text; each shell starts with
// the choices the previous one left, so they are shown to be saved. The
// commands never change the map or its undo history. With a renderer (each
// one that starts here), the camera's pixels show unshaded faces at one
// colour, the face grid only while Show Grid is on and changing with the grid
// size, and fog pulling faces towards the background past its start and not
// before. Owned offscreen widgets and semantic Qt events only.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_camera_render.h"
#include "core/level_editor_controls.h"
#include "core/render_device.h"
#include "core/studio_semantics.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QSpinBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>

#include <array>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <tuple>
#include <utility>
#include <vector>

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

bool until(const std::function<bool()>& ready, int timeoutMs = 45000)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < timeoutMs) {
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

QString box(std::array<double, 3> lo, std::array<double, 3> hi)
{
	QStringList lines {QStringLiteral("{")};
	const auto point = [](const std::array<double, 3>& v) {
		return QStringLiteral("( %1 %2 %3 )").arg(v[0], 0, 'g', 10).arg(v[1], 0, 'g', 10).arg(v[2], 0, 'g', 10);
	};
	for (int axis = 0; axis < 3; ++axis) {
		for (const bool positive : {false, true}) {
			std::array<double, 3> p = lo;
			p[size_t(axis)] = positive ? hi[size_t(axis)] : lo[size_t(axis)];
			auto a = p;
			auto b = p;
			int first = (axis + 1) % 3;
			int second = (axis + 2) % 3;
			if (positive) {
				std::swap(first, second);
			}
			a[size_t(first)] += 1;
			b[size_t(second)] += 1;
			lines << QStringLiteral("%1 %2 %3 base/wall 0 0 0 0.5 0.5 0 0 0").arg(point(p), point(a), point(b));
		}
	}
	lines << QStringLiteral("}");
	return lines.join(QLatin1Char('\n'));
}

QString describe(const LevelCameraShadingState& state)
{
	return QStringLiteral("shade=%1 fog=%2 range=%3..%4 grid=%5")
		.arg(int(state.shadeFaces))
		.arg(int(state.fog))
		.arg(state.fogStart)
		.arg(state.fogEnd)
		.arg(int(state.faceGrid));
}

int differences(const QImage& a, const QImage& b, const QRect& area)
{
	if (a.size() != b.size()) {
		return -1;
	}
	int count = 0;
	const QRect clipped = area.intersected(a.rect());
	for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
		for (int x = clipped.left(); x <= clipped.right(); ++x) {
			count += a.pixel(x, y) != b.pixel(x, y) ? 1 : 0;
		}
	}
	return count;
}

// Summed channel distance between two colours.
int distance(QRgb a, QRgb b)
{
	return std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) + std::abs(qBlue(a) - qBlue(b));
}

QString rgb(QRgb value)
{
	return QStringLiteral("%1,%2,%3").arg(qRed(value)).arg(qGreen(value)).arg(qBlue(value));
}

} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	prepareRenderBackends();
	RenderDeviceInfo failure;
	const bool renderer = activeRenderDevice(&failure) != nullptr;
	if (!renderer) {
		std::cout << "No 3D renderer starts here; the camera's pixel checks are skipped. " << failure.error.toUtf8().constData() << std::endl;
	}
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	const QByteArray source = QStringLiteral("{\n\"classname\" \"worldspawn\"\n%1\n%2\n}\n")
								  .arg(box({0, 0, 0}, {64, 64, 64}), box({256, -32, 0}, {320, 32, 128}))
								  .toUtf8();
	const QString path = temp.filePath(QStringLiteral("shading.map"));
	QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(), "fixture")) {
		return 1;
	}
	file.close();

	// What each shell leaves behind for the next one to start with.
	const auto state = [](bool shade, bool fog, int start, int end, bool grid) {
		LevelCameraShadingState result;
		result.shadeFaces = shade;
		result.fog = fog;
		result.fogStart = start;
		result.fogEnd = end;
		result.faceGrid = grid;
		return result;
	};
	const std::array<LevelCameraShadingState, 4> leaves {
		state(false, true, 640, 2048, true),
		state(true, false, 640, 2048, true),
		state(false, true, 1024, 1040, false),
		state(true, false, kLevelCameraFogDefaultStart, kLevelCameraFogDefaultEnd, false),
	};
	LevelCameraShadingState expectedStart;
	int run = 0;
	for (const char* profile : {"netradiant-custom", "trenchbroom"}) {
		for (int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Level camera shading UI " << phase.toUtf8().constData() << std::endl;
			settings.setSelectedEditorProfileId(QString::fromLatin1(profile));
			settings.setTextScalePercent(scale);
			const auto theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark;
			settings.setTheme(theme);
			settings.sync();
			applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
			Expanded expanded;
			if (scale == 200) {
				app.installTranslator(&expanded);
				app.setLayoutDirection(Qt::RightToLeft);
			}
			{
				ApplicationShell shell;
				shell.resize(scale == 100 ? 1700 : 2600, scale == 100 ? 1050 : 1800);
				shell.show();
				shell.openPathFromCommandLine(path);
				const auto action = [&](const char* id) { return shell.findChild<QAction*>(QString::fromLatin1(id)); };
				action("shell.mode.levels")->trigger();
				action("map.layout.four-views")->trigger();
				auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
				if (!expect(camera && until([&] { return shell.levelDocument().sourcePath == path && camera->isEnabled() && camera->hasMesh(); }),
						"map and camera ready")) {
					return 1;
				}
				const char* ids[] = {"map.previewShading", "map.previewFog", "map.previewFaceGrid"};
				bool present = action("map.toggleGrid") && action("map.previewTextures");
				for (const char* id : ids) {
					present = expect(action(id) != nullptr, "the command exists", QString::fromLatin1(id)) && present;
				}
				auto* viewShading = shell.findChild<QCheckBox*>(QStringLiteral("levelViewShading"));
				auto* viewFog = shell.findChild<QCheckBox*>(QStringLiteral("levelViewFog"));
				auto* viewFaceGrid = shell.findChild<QCheckBox*>(QStringLiteral("levelViewFaceGrid"));
				auto* fogRow = shell.findChild<QWidget*>(QStringLiteral("levelViewFogRange"));
				auto* fogStart = shell.findChild<QSpinBox*>(QStringLiteral("levelViewFogStart"));
				auto* fogEnd = shell.findChild<QSpinBox*>(QStringLiteral("levelViewFogEnd"));
				auto* showGridItem = shell.findChild<QAction*>(QStringLiteral("levelMapShowGrid"));
				if (!expect(present && viewShading && viewFog && viewFaceGrid && fogRow && fogStart && fogEnd && showGridItem,
						"View tab boxes, the fog distances and the Show menu's Grid exist")) {
					return 1;
				}
				const auto gridShown = [&] { return showGridItem->isChecked(); };
				// The View tab may never have been shown: an explicit hide is what
				// keeps the fog distances out of sight.
				const auto rowShown = [&] { return !(fogRow->testAttribute(Qt::WA_WState_ExplicitShowHide) && fogRow->testAttribute(Qt::WA_WState_Hidden)); };
				const auto showing = [&](const LevelCameraShadingState& wanted) {
					const double spacing = levelCameraFaceGridSpacing(wanted, gridShown(), camera->faceGrid() > 0 ? camera->faceGrid() : 16.0);
					return camera->faceShading() == wanted.shadeFaces && camera->distanceFog() == wanted.fog
						&& (camera->faceGrid() > 0) == (spacing > 0) && camera->showGrid() == gridShown()
						&& action("map.previewShading")->isChecked() == wanted.shadeFaces && action("map.previewFog")->isChecked() == wanted.fog
						&& action("map.previewFaceGrid")->isChecked() == wanted.faceGrid && viewShading->isChecked() == wanted.shadeFaces
						&& viewFog->isChecked() == wanted.fog && viewFaceGrid->isChecked() == wanted.faceGrid && rowShown() == wanted.fog
						&& fogStart->value() == wanted.fogStart && fogEnd->value() == wanted.fogEnd;
				};
				const auto actual = [&] {
					return QStringLiteral("camera shade=%1 fog=%2 grid=%3 ground=%4 checks=%5%6%7 boxes=%8%9%10 row=%11 range=%12..%13")
						.arg(int(camera->faceShading()))
						.arg(int(camera->distanceFog()))
						.arg(camera->faceGrid())
						.arg(int(camera->showGrid()))
						.arg(int(action("map.previewShading")->isChecked()))
						.arg(int(action("map.previewFog")->isChecked()))
						.arg(int(action("map.previewFaceGrid")->isChecked()))
						.arg(int(viewShading->isChecked()))
						.arg(int(viewFog->isChecked()))
						.arg(int(viewFaceGrid->isChecked()))
						.arg(int(rowShown()))
						.arg(fogStart->value())
						.arg(fogEnd->value());
				};
				expect(showing(expectedStart), "the saved shading, fog and face grid come back", describe(expectedStart) + QStringLiteral(" / ") + actual());

				// Every command is a labelled View > Camera Display item with a
				// palette entry and, as in TrenchBroom and NetRadiant Custom, no key.
				for (const char* id : ids) {
					QAction* command = action(id);
					expect(command->isCheckable() && !command->text().isEmpty() && !command->statusTip().isEmpty(), "a checkable command with text",
						QString::fromLatin1(id));
					QMenu* menu = nullptr;
					for (QObject* owner : command->associatedObjects()) {
						if (auto* found = qobject_cast<QMenu*>(owner)) {
							menu = found;
						}
					}
					expect(menu && menu->title().contains(QStringLiteral("Camera Display")), "the command is in View > Camera Display",
						menu ? menu->title() : QString::fromLatin1(id));
					QString kebab;
					for (const QChar character : QString::fromLatin1(id)) {
						kebab += character.isUpper() ? QStringLiteral("-") + character.toLower() : QString(character);
					}
					CommandPaletteEntry entry;
					expect(commandPaletteEntryForCommandId(kebab, &entry) && !entry.label.isEmpty(), "the command is in command search", kebab);
					expect(command->shortcuts().isEmpty(), "no profile key", QString::fromLatin1(id));
				}
				for (QWidget* widget : std::initializer_list<QWidget*> {viewShading, viewFog, viewFaceGrid, fogStart, fogEnd}) {
					QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(widget);
					const bool spin = qobject_cast<QSpinBox*>(widget) != nullptr;
					expect(accessible && accessible->role() == (spin ? QAccessible::SpinBox : QAccessible::CheckBox)
							&& !accessible->text(QAccessible::Name).isEmpty() && widget->focusPolicy() != Qt::NoFocus && !widget->toolTip().isEmpty(),
						"a name, a role, a tooltip and keyboard focus", widget->objectName());
				}
				int buddies = 0;
				for (QLabel* label : fogRow->findChildren<QLabel*>()) {
					buddies += label->buddy() == fogStart || label->buddy() == fogEnd ? 1 : 0;
				}
				expect(buddies == 2, "the fog distances are labelled", QString::number(buddies));
				expect(viewFog->text() == QString(action("map.previewFog")->text()).remove(QLatin1Char('&')), "the View tab box names the command",
					viewFog->text());
				expect(action("map.toggleGrid")->isCheckable() && action("map.toggleGrid")->isChecked() == gridShown(),
					"Show Grid is checked while the grid shows");

				const QByteArray bytes = serializeLevelMap(shell.levelDocument()).bytes;
				const auto untouched = [&](const std::function<void()>& command, const QString& name) {
					const auto revision = shell.levelDocument().revision;
					const auto history = shell.levelDocument().undoStack.size();
					command();
					expect(shell.levelDocument().revision == revision && shell.levelDocument().undoStack.size() == history,
						"a display command adds no edit or undo step", name);
				};

				// Back to the defaults through the commands themselves.
				LevelCameraShadingState current = expectedStart;
				const auto toggle = [&](const char* id, bool LevelCameraShadingState::*field) {
					untouched([&] { action(id)->trigger(); }, QString::fromLatin1(id));
					current.*field = !(current.*field);
					expect(showing(current), "the command toggles its choice", QString::fromLatin1(id) + QStringLiteral(": ") + describe(current)
							+ QStringLiteral(" / ") + actual());
				};
				if (!current.shadeFaces) {
					toggle("map.previewShading", &LevelCameraShadingState::shadeFaces);
				}
				if (current.fog) {
					toggle("map.previewFog", &LevelCameraShadingState::fog);
				}
				if (current.faceGrid) {
					toggle("map.previewFaceGrid", &LevelCameraShadingState::faceGrid);
				}
				expect(camera->faceShading() && !camera->distanceFog() && camera->faceGrid() == 0.0, "shaded, unfogged, no face grid", actual());

				// Each command says what the camera now draws.
				toggle("map.previewShading", &LevelCameraShadingState::shadeFaces);
				expect(shell.statusBar()->currentMessage() == levelCameraShadingSummary(current), "Shade Faces is announced",
					shell.statusBar()->currentMessage());
				toggle("map.previewFog", &LevelCameraShadingState::fog);
				expect(shell.statusBar()->currentMessage() == levelCameraFogSummary(current), "Fog is announced", shell.statusBar()->currentMessage());
				toggle("map.previewFaceGrid", &LevelCameraShadingState::faceGrid);
				expect(shell.statusBar()->currentMessage() == levelCameraFaceGridSummary(current, true, camera->faceGrid()), "the face grid is announced",
					shell.statusBar()->currentMessage());

				// The face grid uses the grid size, and Show Grid hides it with the
				// ground grid; Show Grid's check mark follows the Show menu.
				untouched([&] { action(qPrintable(levelEditorGridCommandId(32)))->trigger(); }, QStringLiteral("Grid 32"));
				expect(camera->faceGrid() == 32.0, "the face grid follows the grid size", QString::number(camera->faceGrid()));
				untouched([&] { action(qPrintable(levelEditorGridCommandId(8)))->trigger(); }, QStringLiteral("Grid 8"));
				expect(camera->faceGrid() == 8.0, "and again", QString::number(camera->faceGrid()));
				untouched([&] { action("map.toggleGrid")->trigger(); }, QStringLiteral("Show Grid"));
				expect(!gridShown() && !action("map.toggleGrid")->isChecked() && !camera->showGrid() && camera->faceGrid() == 0.0,
					"Show Grid off hides the camera's ground and face grids", actual());
				untouched([&] { showGridItem->trigger(); }, QStringLiteral("Show menu Grid"));
				expect(gridShown() && action("map.toggleGrid")->isChecked() && camera->showGrid() && camera->faceGrid() == 8.0,
					"the Show menu's Grid brings them back and checks Show Grid", actual());
				untouched([&] { action(qPrintable(levelEditorGridCommandId(16)))->trigger(); }, QStringLiteral("Grid 16"));

				// The fog distances: shown with the fog, kept apart by the minimum
				// gap, the edited end pulling the start in.
				expect(rowShown(), "the fog distances show while the fog is on");
				untouched([&] { fogStart->setValue(3000); }, QStringLiteral("fog start"));
				current = levelCameraWithFogRange(current, 3000, current.fogEnd);
				expect(showing(current), "the fog start", describe(current) + QStringLiteral(" / ") + actual());
				untouched([&] { fogEnd->setValue(1000); }, QStringLiteral("fog end"));
				current = levelCameraWithFogRange(current, current.fogStart, 1000, true);
				expect(current.fogStart == 1000 - kLevelCameraFogMinimumGap && showing(current), "an end before the start pulls the start in",
					describe(current) + QStringLiteral(" / ") + actual());
				expect(shell.statusBar()->currentMessage() == levelCameraFogSummary(current), "the new distances are announced",
					shell.statusBar()->currentMessage());

				// The View tab boxes are the commands.
				untouched([&] { viewFog->setChecked(false); }, QStringLiteral("View tab Fog"));
				current.fog = false;
				expect(showing(current), "the View tab box turns the fog off and hides its distances", actual());
				untouched([&] { viewShading->setChecked(false); }, QStringLiteral("View tab Shade Faces"));
				current.shadeFaces = false;
				expect(showing(current), "the View tab box turns shading off", actual());
				untouched([&] { viewFaceGrid->setChecked(false); }, QStringLiteral("View tab Grid on Faces"));
				current.faceGrid = false;
				expect(showing(current), "the View tab box turns the face grid off", actual());
				untouched([&] { viewShading->setChecked(true); }, QStringLiteral("View tab Shade Faces on"));
				current.shadeFaces = true;
				expect(showing(current), "and back on", actual());

				// They are saved choices, available with the camera hidden too.
				action("map.layout.single-2d")->trigger();
				expect(until([&] { return !camera->isVisible(); }), "the camera is hidden");
				for (const char* id : ids) {
					expect(action(id)->isEnabled(), "a saved choice stays available", QString::fromLatin1(id));
				}
				toggle("map.previewFaceGrid", &LevelCameraShadingState::faceGrid);
				toggle("map.previewFaceGrid", &LevelCameraShadingState::faceGrid);
				action("map.layout.four-views")->trigger();
				expect(until([&] { return camera->isVisible() && camera->isEnabled(); }) && showing(current), "the camera comes back as it was", actual());

				// The camera's pixels, on every renderer that starts here.
				if (renderer && run == 0) {
					action("map.hideCameraTextures")->trigger();
					if (action("map.previewEdges")->isChecked()) {
						action("map.previewEdges")->trigger();
					}
					// Looking along +X+Y at the first box's vertical edge: its -X face
					// on the left, its -Y face on the right, both about 85 units away, so
					// they fill the middle of even a small camera pane.
					camera->setCameraView({-60, -60, 32}, 45, 0);
					const QPoint centre = camera->rect().center();
					const QPoint left = centre - QPoint(18, 0);
					const QPoint right = centre + QPoint(18, 0);
					const QRect faces(centre - QPoint(26, 20), QSize(53, 41));
					const QRgb background = qRgb(20, 22, 27);
					std::vector<std::pair<RenderBackendChoice, const char*>> backends;
					for (const auto& [backend, choice, name] : {std::tuple {RenderBackend::Vulkan, RenderBackendChoice::Vulkan, "Vulkan"},
							 std::tuple {RenderBackend::OpenGL, RenderBackendChoice::OpenGL, "OpenGL"}}) {
						if (renderDevice(backend)->isAvailable()) {
							backends.push_back({choice, name});
						}
					}
					expect(!backends.empty(), "a renderer is available by name");
					for (const auto& [choice, name] : backends) {
						setRenderBackendChoiceOverride(choice);
						const auto picture = [&] {
							expect(tests::settleModelViewport(*camera), "the camera finishes drawing", QString::fromLatin1(name));
							expect(tests::settleModelViewport(*camera), "the camera finishes drawing again", QString::fromLatin1(name));
							QImage image(camera->size(), QImage::Format_ARGB32_Premultiplied);
							image.fill(Qt::transparent);
							camera->render(&image);
							return image;
						};
						const QImage shaded = picture();
						expect(shaded.pixel(left) != shaded.pixel(right), "shading sets the two faces apart",
							QStringLiteral("%1: %2 / %3").arg(QLatin1String(name), rgb(shaded.pixel(left)), rgb(shaded.pixel(right))));
						action("map.previewShading")->trigger();
						const QImage flat = picture();
						expect(flat.pixel(left) == flat.pixel(right) && flat.pixel(left) != shaded.pixel(left),
							"unshaded, both faces show their surface's one colour",
							QStringLiteral("%1: %2 / %3").arg(QLatin1String(name), rgb(flat.pixel(left)), rgb(flat.pixel(right))));

						// The face grid at 16 units, then 8; none with Show Grid off.
						action("map.previewFaceGrid")->trigger();
						const QImage grid16 = picture();
						action(qPrintable(levelEditorGridCommandId(8)))->trigger();
						const QImage grid8 = picture();
						action("map.toggleGrid")->trigger();
						const QImage gridHidden = picture();
						action("map.toggleGrid")->trigger();
						action(qPrintable(levelEditorGridCommandId(16)))->trigger();
						action("map.previewFaceGrid")->trigger();
						expect(differences(grid16, flat, faces) > 0, "the face grid draws lines on the faces",
							QStringLiteral("%1: %2").arg(QLatin1String(name)).arg(differences(grid16, flat, faces)));
						expect(differences(grid8, grid16, faces) > 0, "a finer grid draws other lines",
							QStringLiteral("%1: %2").arg(QLatin1String(name)).arg(differences(grid8, grid16, faces)));
						expect(differences(gridHidden, flat, faces) == 0, "Show Grid off leaves the faces plain",
							QStringLiteral("%1: %2").arg(QLatin1String(name)).arg(differences(gridHidden, flat, faces)));

						// Fog thick from 32 units: both faces pulled most of the way to
						// the background, still alike; with the fog starting beyond
						// them, untouched.
						action("map.previewFog")->trigger();
						fogStart->setValue(0);
						fogEnd->setValue(32);
						const QImage fogged = picture();
						fogEnd->setValue(20000);
						fogStart->setValue(10000);
						const QImage beyond = picture();
						action("map.previewFog")->trigger();
						const int clear = distance(flat.pixel(left), background);
						const int thick = distance(fogged.pixel(left), background);
						expect(thick * 10 <= clear * 3 + 30 && fogged.pixel(left) == fogged.pixel(right), "fog pulls the faces towards the background",
							QStringLiteral("%1: %2 -> %3").arg(QLatin1String(name), rgb(flat.pixel(left)), rgb(fogged.pixel(left))));
						expect(differences(beyond, flat, faces) == 0, "no fog before its start",
							QStringLiteral("%1: %2").arg(QLatin1String(name)).arg(differences(beyond, flat, faces)));
						action("map.previewShading")->trigger();
						const QImage restored = picture();
						expect(differences(restored, shaded, faces) == 0, "shading back on restores the picture",
							QStringLiteral("%1: %2").arg(QLatin1String(name)).arg(differences(restored, shaded, faces)));

						// The orthographic orbit camera (the VibeStudio Default
						// profile's) counts fog from the front of the map.
						const CameraViewControls profileControls = camera->cameraControls();
						camera->setCameraControls(CameraViewControls {});
						expect(!camera->isPerspective(), "an orthographic camera", QString::fromLatin1(name));
						const QImage orbit = picture();
						action("map.previewFog")->trigger();
						fogStart->setValue(0);
						fogEnd->setValue(kLevelCameraFogMinimumGap);
						const QImage orbitFogged = picture();
						fogEnd->setValue(20000);
						fogStart->setValue(10000);
						action("map.previewFog")->trigger();
						camera->setCameraControls(profileControls);
						camera->setCameraView({-60, -60, 32}, 45, 0);
						expect(differences(orbitFogged, orbit, camera->rect()) > 0, "fog shows in the orthographic camera too",
							QStringLiteral("%1: %2").arg(QLatin1String(name)).arg(differences(orbitFogged, orbit, camera->rect())));
						std::cout << name << " camera pixels: shaded " << rgb(shaded.pixel(left)).toUtf8().constData() << " / "
								  << rgb(shaded.pixel(right)).toUtf8().constData() << ", unshaded " << rgb(flat.pixel(left)).toUtf8().constData()
								  << ", fogged " << rgb(fogged.pixel(left)).toUtf8().constData() << ", grid pixels " << differences(grid16, flat, faces)
								  << std::endl;
					}
					setRenderBackendChoiceOverride(RenderBackendChoice::Automatic);
					current = levelCameraWithFogRange(current, 10000, 20000);
					action("map.showCameraTextures")->trigger();
					expect(showing(current), "the pixel checks leave the choices as they were", describe(current) + QStringLiteral(" / ") + actual());
				}

				expect(serializeLevelMap(shell.levelDocument()).bytes == bytes, "camera shading never changes the map");

				// Leave choices for the next shell to bring back.
				const LevelCameraShadingState leave = leaves[size_t(run)];
				if (!viewFog->isChecked()) {
					viewFog->setChecked(true);
				}
				fogEnd->setValue(kLevelCameraFogMaximumDistance);
				fogStart->setValue(leave.fogStart);
				fogEnd->setValue(leave.fogEnd);
				viewFog->setChecked(leave.fog);
				viewShading->setChecked(leave.shadeFaces);
				viewFaceGrid->setChecked(leave.faceGrid);
				expect(showing(leave), "the choices to leave", describe(leave) + QStringLiteral(" / ") + actual());
				expectedStart = leave;
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
			++run;
		}
	}
	if (ok) {
		std::cout << "Level camera shading UI: saved shading, fog and face grid, Show Grid, View tab and pixels passed." << std::endl;
	}
	return ok ? 0 : 1;
}
