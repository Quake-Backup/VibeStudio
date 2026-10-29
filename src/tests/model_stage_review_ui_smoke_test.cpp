// Stage in Package's review (SI-2): the review dialog lists every file with
// its role, size and what it replaces, the materials and the notes, and keeps
// Stage disabled while files would be replaced without that being chosen. The
// Mesh Editor stages an md5mesh with its clips and an MD3 with its .skin
// files as one reviewed step; cancelling the review stages nothing.
#include "app/model_editor_dialog.h"
#include "app/model_stage_review_dialog.h"
#include "app/studio_theme.h"
#include "core/model_stage.h"
#include "core/studio_settings.h"
#include "tests/model_rig_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int failures = 0;
bool expect(bool value, const char *label)
{
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << label << '\n';
	}
	return value;
}
QStringList paths(const QVector<ModelStagedFile> &files)
{
	QStringList result;
	for (const auto &file : files)
		result << file.path;
	return result;
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("stage-review-XXXXXX")));
	if (!temporary.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("stage review"); unavailable >= 0)
		return unavailable;
	QString error;

	// The dialog, in both directions and high contrast at 200 %.
	ModelStageReview review;
	review.packagePath = QStringLiteral("C:/mods/mymod.pk3");
	review.entries = {{QStringLiteral("models/props/crate.md3"), QStringLiteral("model"), 4096, ModelStageEntryState::ReplacesPackageFile},
					  {QStringLiteral("models/props/crate_1.md3"), QStringLiteral("detail-level"), 2048, ModelStageEntryState::New},
					  {QStringLiteral("models/props/crate_default.skin"), QStringLiteral("skin"), 40, ModelStageEntryState::ReplacesStagedFile}};
	review.notes = {QStringLiteral("Normals were quantised.")};
	review.dependenciesReviewed = true;
	LevelDependency found;
	found.reference = QStringLiteral("textures/props/crate");
	found.status = LevelDependencyStatus::Resolved;
	LevelDependency missing;
	missing.reference = QStringLiteral("textures/props/lid");
	missing.status = LevelDependencyStatus::Missing;
	review.dependencies.dependencies = {found, missing};
	review.dependencies.missingCount = 1;
	expect(review.conflicts() == 2 && review.totalBytes() == 6184, "the review counts replaced files and bytes");
	for (int scenario = 0; scenario < 2; ++scenario)
	{
		app.setLayoutDirection(scenario ? Qt::RightToLeft : Qt::LeftToRight);
		applyStudioTheme(app, studioThemeTokens(scenario ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scenario ? 200 : 100));
		ModelStageReviewDialog dialog(review, false, false);
		dialog.show();
		app.processEvents();
		auto *files = dialog.findChild<QTableWidget *>(QStringLiteral("modelStageReviewFiles"));
		auto *materials = dialog.findChild<QTableWidget *>(QStringLiteral("modelStageReviewMaterials"));
		auto *replace = dialog.findChild<QCheckBox *>(QStringLiteral("modelStageReviewReplace"));
		auto *stage = dialog.findChild<QPushButton *>(QStringLiteral("modelStageReviewStage"));
		auto *blocked = dialog.findChild<QLabel *>(QStringLiteral("modelStageReviewBlocked"));
		if (!expect(files && materials && replace && stage && blocked, "the review shows files, materials, replace and Stage"))
			return 1;
		expect(files->rowCount() == 3 && files->item(0, 3)->text() == modelStageEntryStateText(ModelStageEntryState::ReplacesPackageFile) &&
				   files->item(1, 3)->text() == modelStageEntryStateText(ModelStageEntryState::New),
			   "each file says whether it is new or replaces one");
		expect(materials->rowCount() == 2 && materials->item(1, 1)->text() == levelDependencyStatusName(LevelDependencyStatus::Missing),
			   "a missing material is named in words");
		expect(!stage->isEnabled() && blocked->isVisible() && !blocked->text().isEmpty(),
			   "Stage waits while files would be replaced without that being chosen");
		replace->setChecked(true);
		expect(stage->isEnabled() && dialog.replaceExisting(), "choosing Replace enables Stage");
		for (auto *control : QList<QWidget *>{files, materials, replace, stage})
			expect(!control->accessibleName().isEmpty() && (control->focusPolicy() & Qt::TabFocus) &&
					   QAccessible::queryAccessibleInterface(control),
				   "review controls are named and keyboard reachable");
		expect(dialog.layoutDirection() == app.layoutDirection(), "the review follows the layout direction");
		dialog.reject();
	}
	app.setLayoutDirection(Qt::LeftToRight);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));

	// The Mesh Editor stages one reviewed set.
	auto mesh = rigFixture();
	ModelSkinVariant red;
	red.name = QStringLiteral("red");
	red.bindings = {{QStringLiteral("body"), QStringLiteral("models/red.tga")}};
	mesh.skinVariants = {red};
	ModelEditorDialog editor;
	expect(editor.setMesh(mesh, &error), "open the rig with a skin variant");
	editor.context = [] { return ModelDesignContext{QStringLiteral("C:/mods/mymod.pk3"), QString(), true, false, false, {}}; };
	QVector<ModelStagedFile> staged;
	bool stagedReplace = false;
	int singles = 0;
	editor.handoff = [&](const QByteArray &, const QString &, bool, const LevelMapVec3 &, bool replace, QString *)
	{
		++singles;
		stagedReplace = replace;
		return true;
	};
	editor.handoffFiles = [&](const QVector<ModelStagedFile> &files, bool, const LevelMapVec3 &, bool replace, QString *)
	{
		staged = files;
		stagedReplace = replace;
		return true;
	};
	ModelStageReview seen;
	bool conflicting = false;
	editor.reviewStage = [&](const ModelStageSet &set, const ModelMesh &)
	{
		auto result = reviewModelStageSet(set, nullptr);
		if (conflicting)
			result.entries.first().state = ModelStageEntryState::ReplacesPackageFile;
		return result;
	};
	int answer = 1;
	editor.stageReviewChoice = [&](const ModelStageReview &shown)
	{
		seen = shown;
		return answer;
	};
	editor.show();
	editor.refreshContext();
	auto *path = editor.findChild<QLineEdit *>(QStringLiteral("meshVirtualPath"));
	auto *clips = editor.findChild<QCheckBox *>(QStringLiteral("meshStageClips"));
	auto *skins = editor.findChild<QCheckBox *>(QStringLiteral("meshStageSkins"));
	auto *stage = editor.findChild<QAction *>(QStringLiteral("stageMesh"));
	if (!expect(path && clips && skins && stage, "the Export page offers clip and skin staging"))
		return 1;
	path->setText(QStringLiteral("models/rig/rig.md5mesh"));
	expect(clips->isEnabled() && clips->isChecked() && !skins->isEnabled(), "clips apply to an md5mesh, skins to an MD3");
	stage->trigger();
	expect(paths(staged) == QStringList{QStringLiteral("models/rig/rig.md5mesh"), QStringLiteral("models/rig/lift.md5anim")} && !stagedReplace,
		   "an md5mesh stages with its clip as one step");
	expect(seen.entries.size() == 2 && seen.entries.last().role == QStringLiteral("animation"), "the review lists the clip as an animation");

	staged.clear();
	path->setText(QStringLiteral("models/players/rig/lower.md3"));
	expect(skins->isEnabled() && !clips->isEnabled(), "skins apply to an MD3");
	skins->setChecked(true);
	stage->trigger();
	expect(paths(staged) == QStringList{QStringLiteral("models/players/rig/lower.md3"), QStringLiteral("models/players/rig/lower_default.skin"),
										QStringLiteral("models/players/rig/lower_red.skin")},
		   "an MD3 stages with its default and variant skins");
	const auto redSkin = staged.value(2).bytes;
	expect(redSkin.contains("models/red.tga") && staged.value(1).bytes.contains("models/body.tga"), "each skin binds its variant's materials");

	// Cancelling, and replacing, are chosen in the review.
	staged.clear();
	answer = 0;
	stage->trigger();
	expect(staged.isEmpty() && singles == 0, "a cancelled review stages nothing");
	conflicting = true;
	answer = 1;
	stage->trigger();
	expect(staged.isEmpty(), "replacing files needs that choice");
	answer = 2;
	stage->trigger();
	expect(staged.size() == 3 && stagedReplace, "choosing Replace stages over the existing files");

	// Stage and Place refuses when the map changed while the model was prepared
	// (SI-3): readPackageFiles runs during preparation, so a map edit there
	// stands for one made meanwhile in the Levels workbench.
	quint64 mapRevision = 7;
	editor.context = [&]
	{
		ModelDesignContext context{QStringLiteral("C:/mods/mymod.pk3"), QStringLiteral("C:/mods/maps/arena.map"), true, true, false,
								   {QStringLiteral("md3")}};
		context.mapRevision = mapRevision;
		return context;
	};
	bool placed = false;
	editor.handoff = [&](const QByteArray &, const QString &, bool place, const LevelMapVec3 &, bool, QString *)
	{
		placed = place;
		++singles;
		return true;
	};
	editor.readPackageFiles = [&](const QStringList &)
	{
		++mapRevision;
		return QHash<QString, QByteArray>{};
	};
	conflicting = false;
	skins->setChecked(false);
	answer = 1;
	editor.refreshContext();
	auto *placeAction = editor.findChild<QAction *>(QStringLiteral("placeMesh"));
	const int before = singles;
	expect(placeAction && placeAction->isEnabled(), "Stage and Place is offered for an MD3 with a Quake III map");
	if (placeAction)
		placeAction->trigger();
	expect(singles == before && !placed, "a map changed while preparing refuses Stage and Place and stages nothing");
	editor.readPackageFiles = {};
	if (placeAction)
		placeAction->trigger();
	expect(singles == before + 1 && placed, "an unchanged map stages and places");

	// Without the shell's review the editor stages directly, as before.
	editor.reviewStage = {};
	answer = 0;
	const int direct = singles;
	stage->trigger();
	expect(singles == direct + 1, "an editor without a package plan stages without a review");
	StudioSettings::setOverrideFilePath(QString());
	std::cout << failures << " failures\n";
	return failures ? 1 : 0;
}
