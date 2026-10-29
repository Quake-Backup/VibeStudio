// Stage and Place from the Mesh Editor in the studio (SI-3): the model is
// placed through the shared placement service with the entity definitions the
// Levels workbench loaded, so a mod that declares only misc_gamemodel gets
// that class, after the review. (A map edited while the model is prepared is
// refused; model_stage_review_ui_smoke_test covers that.)
#include "app/application_shell.h"
#include "app/model_editor_dialog.h"
#include "app/studio_theme.h"
#include "core/level_model_placement.h"
#include "core/model_stage.h"
#include "tests/level_material_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>
#include <iostream>

using namespace vibestudio;
namespace
{
int failures = 0;
bool expect(bool value, const char *label, const QString &detail = {})
{
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << label << (detail.isEmpty() ? "" : ": ") << qPrintable(detail) << '\n';
	}
	return value;
}
bool until(const std::function<bool()> &ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 60000)
	{
		QEventLoop loop;
		QTimer::singleShot(20, &loop, &QEventLoop::quit);
		loop.exec();
	}
	return ready();
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temp(QDir(root).filePath(QStringLiteral("stage-place-XXXXXX")));
	if (!temp.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("stage and place"); unavailable >= 0)
		return unavailable;
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	QString error;
	LevelMapDocument fixture;
	expect(tests::createMaterialFixture(temp.path(), &fixture, &error), "map and package fixture", error);
	const auto mapPath = temp.filePath(QStringLiteral("arena.map"));
	expect(tests::putMaterialFile(mapPath, serializeLevelMap(fixture).bytes), "write the map");
	// A mod whose definitions declare only misc_gamemodel for models.
	const auto definitions = temp.filePath(QStringLiteral("defs/mod.def"));
	expect(tests::putMaterialFile(definitions, "/*QUAKED misc_gamemodel (1 0.5 0.25) (-16 -16 -16) (16 16 16)\n"
											   "A model placed by the game.\n*/\n"
											   "/*QUAKED info_player_start (1 0 0) (-16 -16 -24) (16 16 32)\n*/\n"),
		   "write the definitions");
	{
		ApplicationShell shell;
		shell.resize(1600, 1000);
		shell.show();
		shell.openPathFromCommandLine(mapPath);
		shell.openPathFromCommandLine(temp.filePath(QStringLiteral("assets")));
		shell.findChild<QAction *>(QStringLiteral("shell.mode.levels"))->trigger();
		QLineEdit *definitionPath = nullptr;
		for (auto *edit : shell.findChildren<QLineEdit *>())
			if (edit->accessibleName() == QStringLiteral("Entity definition path"))
				definitionPath = edit;
		if (!expect(definitionPath != nullptr, "the Levels workbench offers an entity definition path"))
			return EXIT_FAILURE;
		definitionPath->setText(definitions);
		QMetaObject::invokeMethod(definitionPath, "returnPressed");
		expect(until([&] { return shell.levelDocument().entities.size() == fixture.entities.size(); }), "the map is open");
		const int entitiesBefore = int(shell.levelDocument().entities.size());

		ModelDesign design;
		ModelDesignPart part;
		part.material = QStringLiteral("textures/studio/shader");
		design.parts = {part};
		const auto meshPath = temp.filePath(QStringLiteral("crate.mesh.json"));
		ModelDocument source;
		expect(source.setMesh(buildModelDesignMesh(design), &error) && source.save(meshPath, true, &error), "save an editable source", error);
		shell.openPathFromCommandLine(meshPath);
		QCoreApplication::processEvents();
		auto *editor = static_cast<ModelEditorDialog *>(shell.findChild<QDialog *>(QStringLiteral("modelEditorDialog")));
		if (!expect(editor != nullptr, "the Mesh Editor opens"))
			return EXIT_FAILURE;
		int reviews = 0;
		editor->stageReviewChoice = [&](const ModelStageReview &review)
		{
			++reviews;
			expect(review.dependenciesReviewed && !review.entries.isEmpty(), "the review covers the package and its materials");
			return 1;
		};
		editor->findChild<QLineEdit *>(QStringLiteral("meshVirtualPath"))->setText(QStringLiteral("models/studio/crate.md3"));
		auto *place = editor->findChild<QAction *>(QStringLiteral("placeMesh"));
		expect(place && until([&]
							  {
								  editor->refreshContext();
								  return place->isEnabled();
							  }),
			   "Stage and Place is available with a Quake III map and a folder package");

		if (place)
			place->trigger();
		expect(reviews == 1, "Stage and Place shows the review first");
		const auto &map = shell.levelDocument();
		expect(int(map.entities.size()) == entitiesBefore + 1, "one entity is placed");
		const auto &added = map.entities.last();
		expect(added.className == QStringLiteral("misc_gamemodel") && levelEntityModelPath(added) == QStringLiteral("models/studio/crate.md3"),
			   "the placed entity uses the class the loaded definitions declare", added.className);
		editor->stageReviewChoice = {};
	}
	StudioSettings::setOverrideFilePath(QString());
	std::cout << failures << " failures\n";
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
