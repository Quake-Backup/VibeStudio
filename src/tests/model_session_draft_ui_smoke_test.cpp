// Staged sessions survive a crash (ROB-9, core/model_session_draft.h): while
// Paint Skin Weights or Animation Curves is open the Mesh Editor keeps a
// recovery copy of the model and writes the staged strokes or curves beside
// it. A "crash" is simulated by copying the recovery folder while the dialog
// is open. Restoring that copy in a new editor offers the work again, the
// re-reviewed strokes are identical and apply as one edit, work staged on
// another version is refused, and `model recover --strokes-output` replays
// through `model weight-paint --stroke-file` to the same weights.
#include "app/model_animation_curves_dialog.h"
#include "app/model_editor_dialog.h"
#include "app/model_weight_paint_dialog.h"
#include "app/studio_theme.h"
#include "core/model_recovery.h"
#include "core/model_session_draft.h"
#include "core/studio_settings.h"
#include "tests/model_rig_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <cmath>
#include <functional>
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
bool waitFor(const std::function<bool()> &condition, int milliseconds = 30000)
{
	QElapsedTimer clock;
	clock.start();
	while (!condition() && clock.elapsed() < milliseconds)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QThread::msleep(2);
	}
	return condition();
}
template <class Dialog> Dialog *topLevel()
{
	for (auto *widget : QApplication::topLevelWidgets())
		if (auto *dialog = dynamic_cast<Dialog *>(widget); dialog && dialog->isVisible())
			return dialog;
	return nullptr;
}
QDialog *chooser()
{
	for (auto *widget : QApplication::topLevelWidgets())
		if (widget->objectName() == QStringLiteral("modelRecoveryDialog") && widget->isVisible())
			return qobject_cast<QDialog *>(widget);
	return nullptr;
}
// Restores the newest copy through Model > Recover… once the chooser lists it.
void restoreNewest(ModelEditorDialog &editor)
{
	bool clicked = false;
	QTimer driver;
	QObject::connect(&driver, &QTimer::timeout, &editor,
					 [&]
					 {
						 auto *dialog = chooser();
						 if (!dialog || clicked)
							 return;
						 auto *list = dialog->findChild<QListWidget *>(QStringLiteral("modelRecoveryList"));
						 auto *restore = dialog->findChild<QPushButton *>(QStringLiteral("restoreModelRecovery"));
						 if (!list || !restore || list->count() == 0 || !list->isEnabled())
							 return;
						 list->setCurrentRow(0);
						 if (!restore->isEnabled())
							 return;
						 clicked = true;
						 restore->click();
					 });
	driver.start(10);
	editor.findChild<QAction *>(QStringLiteral("recoverMesh"))->trigger();
	driver.stop();
}
bool copyFolder(const QString &from, const QString &to)
{
	QDir().mkpath(to);
	bool ok = true;
	for (const auto &name : QDir(from).entryList(QDir::Files))
		ok &= QFile::copy(QDir(from).filePath(name), QDir(to).filePath(name));
	return ok;
}
void selectRows(QTableView *table, const QSet<int> &rows)
{
	table->selectionModel()->clearSelection();
	for (int row : rows)
		table->selectionModel()->select(table->model()->index(row, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
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
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("session-drafts-XXXXXX")));
	if (!temporary.isValid())
		return 1;
	const auto folder = [&](const QString &name) { return QDir(temporary.path()).filePath(name); };
	StudioSettings::setOverrideFilePath(QDir(folder(QStringLiteral("live"))).filePath(QStringLiteral("settings.ini")));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("staged session recovery"); unavailable >= 0)
		return unavailable;
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	const auto mesh = rigFixture();
	const auto liveDirectory = modelRecoveryDirectory();
	QString error;

	// 1. Painting keeps a copy of the model and the staged strokes beside it.
	QVector<ModelWeightStroke> painted;
	QByteArray base;
	{
		ModelEditorDialog editor;
		expect(editor.setMesh(mesh, &error), "open the rig");
		editor.showSidebarPage(QStringLiteral("skeleton"));
		editor.show();
		base = editor.document().revisionFingerprint();
		QTimer::singleShot(0, &editor,
						   [&]
						   {
							   auto *dialog = topLevel<ModelWeightPaintDialog>();
							   if (!expect(dialog != nullptr, "Paint Skin Weights opens"))
								   return;
							   expect(waitFor([&] { return !dialog->busy(); }), "the painting document is prepared");
							   expect(!editor.sessionDraftPath().isEmpty() || inspectModelSessionDrafts(liveDirectory).isEmpty(),
									  "no strokes, no sidecar");
							   auto *table = dialog->findChild<QTableView *>(QStringLiteral("weightPaintTable"));
							   dialog->findChild<QComboBox *>(QStringLiteral("weightPaintJoint"))->setCurrentIndex(1);
							   dialog->findChild<QDoubleSpinBox *>(QStringLiteral("weightPaintStrength"))->setValue(1);
							   dialog->findChild<QDoubleSpinBox *>(QStringLiteral("weightPaintValue"))->setValue(.6);
							   selectRows(table, {0});
							   expect(dialog->paintSelected(), "stage the first stroke");
							   dialog->findChild<QDoubleSpinBox *>(QStringLiteral("weightPaintValue"))->setValue(.3);
							   selectRows(table, {2});
							   expect(dialog->paintSelected(), "stage the second stroke");
							   painted = dialog->stagedStrokes();
							   const bool kept = waitFor(
								   [&]
								   {
									   const auto drafts = inspectModelSessionDrafts(liveDirectory);
									   return !editor.recoveryBusy() && QFileInfo::exists(editor.recoveryPath()) && drafts.size() == 1 &&
											  drafts.first().strokeCount == painted.size();
								   });
							   const auto drafts = inspectModelSessionDrafts(liveDirectory);
							   if (!expect(kept, "the copy and a sidecar with both strokes are kept while painting"))
								   std::cerr << "busy " << editor.recoveryBusy() << " copy " << QFileInfo::exists(editor.recoveryPath()) << " sidecars "
											 << drafts.size() << " strokes " << (drafts.isEmpty() ? -1 : drafts.first().strokeCount) << " painted "
											 << painted.size() << " path " << qPrintable(editor.sessionDraftPath()) << '\n';
							   expect(drafts.size() == 1 && drafts.first().baseFingerprint == base &&
										  drafts.first().ownerId == QFileInfo(editor.recoveryPath()).completeBaseName(),
									  "the sidecar names the copy and the model it was staged on");
							   // The crash: what is on disk now is what a restart finds.
							   expect(copyFolder(liveDirectory, folder(QStringLiteral("crash/model-recovery"))) &&
										  copyFolder(liveDirectory, folder(QStringLiteral("pristine"))),
									  "keep the crash state");
							   dialog->reject();
						   });
		editor.findChild<QPushButton *>(QStringLiteral("paintMeshSkinWeights"))->click();
		expect(painted.size() == 2, "two strokes were staged");
		expect(waitFor([&] { return !editor.recoveryBusy() && inspectModelSessionDrafts(liveDirectory).isEmpty(); }),
			   "closing Paint Skin Weights removes its sidecar");
		expect(waitFor([&] { return !editor.recoveryBusy() && listModelRecoveries(liveDirectory).records.isEmpty(); }),
			   "an unmodified model keeps no copy once the session is closed");

		// Animation Curves keeps its staged draft the same way.
		ModelPoseCurves captured;
		QTimer::singleShot(0, &editor,
						   [&]
						   {
							   auto *dialog = topLevel<ModelAnimationCurvesDialog>();
							   if (!expect(dialog != nullptr, "Animation Curves opens"))
								   return;
							   dialog->findChild<QComboBox *>(QStringLiteral("curvesJoint"))->setCurrentText(QStringLiteral("tip"));
							   dialog->capture();
							   int clip = -1;
							   bool bake = false;
							   expect(dialog->stagedDraft(&clip, &captured, &bake) && clip == 0 && !bake, "capture stages curves");
							   const bool kept = waitFor(
								   [&]
								   {
									   const auto drafts = inspectModelSessionDrafts(liveDirectory);
									   return !editor.recoveryBusy() && drafts.size() == 1 &&
											  drafts.first().kind == ModelSessionDraftKind::AnimationCurves && drafts.first().keyCount > 0;
								   });
							   expect(kept, "the staged curves are kept beside the copy");
							   expect(copyFolder(liveDirectory, folder(QStringLiteral("curves/model-recovery"))), "keep the curve crash state");
							   dialog->reject();
						   });
		editor.findChild<QPushButton *>(QStringLiteral("editMeshAnimationCurves"))->click();
		expect(waitFor([&] { return !editor.recoveryBusy() && inspectModelSessionDrafts(liveDirectory).isEmpty(); }),
			   "closing Animation Curves removes its sidecar");

		// 2. After the crash: restoring offers the strokes again, identical.
		StudioSettings::setOverrideFilePath(QDir(folder(QStringLiteral("crash"))).filePath(QStringLiteral("settings.ini")));
		const auto crashDirectory = modelRecoveryDirectory();
		const auto crashCopy = listModelRecoveries(crashDirectory).records.value(0).path;
		expect(!crashCopy.isEmpty() && inspectModelSessionDrafts(crashDirectory).size() == 1, "the crash left a copy and a sidecar");
		ModelEditorDialog restored;
		restored.show();
		QString offered;
		restored.sessionDraftPrompt = [&](const QString &summary)
		{
			offered = summary;
			return 1;
		};
		QVector<ModelWeightStroke> reviewed;
		QTimer review;
		QObject::connect(&review, &QTimer::timeout, &restored,
						 [&]
						 {
							 auto *dialog = topLevel<ModelWeightPaintDialog>();
							 if (!dialog || dialog->busy() || dialog->stagedStrokes().isEmpty())
								 return;
							 review.stop();
							 reviewed = dialog->stagedStrokes();
							 expect(waitFor([&] { return !restored.sessionDraftPath().isEmpty(); }),
									"the review keeps its own sidecar with the restored copy");
							 dialog->findChild<QPushButton *>(QStringLiteral("weightPaintApply"))->click();
						 });
		review.start(20);
		restoreNewest(restored);
		review.stop();
		expect(!offered.isEmpty(), "restoring the copy offers the staged strokes");
		expect(reviewed == painted, "the re-reviewed strokes are identical");
		ModelDocument expected;
		ModelEdit edit;
		edit.kind = ModelEditKind::PaintSkinWeights;
		edit.selection.surface = 0;
		edit.weightStrokes = painted;
		expect(expected.setMesh(mesh, &error) && expected.edit(edit, &error), "apply the strokes directly");
		expect(restored.document().revisionFingerprint() == expected.revisionFingerprint(),
			   "applying the reviewed strokes gives the painted weights");
		expect(waitFor([&] { return !restored.recoveryBusy() && inspectModelSessionDrafts(crashDirectory).isEmpty(); }),
			   "the recovered sidecar is consumed once reviewed");

		// 3. Work staged on another version is refused.
		StudioSettings::setOverrideFilePath(QDir(folder(QStringLiteral("stale"))).filePath(QStringLiteral("settings.ini")));
		const auto staleDirectory = modelRecoveryDirectory();
		expect(copyFolder(folder(QStringLiteral("pristine")), staleDirectory), "copy the crash state");
		const auto staleHeader = inspectModelSessionDrafts(staleDirectory).value(0);
		ModelSessionDraft stale;
		expect(readModelSessionDraft(staleHeader.path, &stale, &error), "read the sidecar");
		stale.baseFingerprint = QByteArray(32, '\x05');
		expect(writeModelSessionDraft(staleDirectory, stale, &error), "rewrite it as staged on another version");
		ModelEditorDialog refused;
		refused.show();
		bool asked = false;
		refused.sessionDraftPrompt = [&](const QString &)
		{
			asked = true;
			return 1;
		};
		restoreNewest(refused);
		expect(!asked && refused.document().isModified() && topLevel<ModelWeightPaintDialog>() == nullptr,
			   "strokes staged on another version are not offered");

		// 4. Curves come back into Animation Curves.
		StudioSettings::setOverrideFilePath(QDir(folder(QStringLiteral("curves"))).filePath(QStringLiteral("settings.ini")));
		ModelEditorDialog curves;
		curves.show();
		curves.sessionDraftPrompt = [](const QString &) { return 1; };
		ModelPoseCurves restoredCurves;
		QTimer curveReview;
		QObject::connect(&curveReview, &QTimer::timeout, &curves,
						 [&]
						 {
							 auto *dialog = topLevel<ModelAnimationCurvesDialog>();
							 int clip = -1;
							 bool bake = false;
							 if (!dialog || !dialog->stagedDraft(&clip, &restoredCurves, &bake))
								 return;
							 curveReview.stop();
							 dialog->reject();
						 });
		curveReview.start(20);
		restoreNewest(curves);
		curveReview.stop();
		expect(restoredCurves == captured && !restoredCurves.channels[0].isEmpty(), "the staged curves come back for review");

		// 5. The CLI replays the same strokes.
		if (argc > 1)
		{
			const auto run = [&](const QStringList &arguments, QJsonObject *json)
			{
				QProcess process;
				process.start(QString::fromLocal8Bit(argv[1]), QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"),
																		   QDir(folder(QStringLiteral("crash"))).filePath(QStringLiteral("cli.ini"))} +
																   arguments + QStringList{QStringLiteral("--json")});
				if (!process.waitForFinished(120000))
					return -1;
				*json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
				return process.exitCode();
			};
			QJsonObject json;
			const auto cliDirectory = folder(QStringLiteral("cli/model-recovery"));
			expect(copyFolder(folder(QStringLiteral("pristine")), cliDirectory), "copy the crash state for the CLI");
			expect(run({QStringLiteral("model"), QStringLiteral("recoveries"), QStringLiteral("--directory"), cliDirectory}, &json) == 0 &&
					   json.value(QStringLiteral("records")).toArray().at(0).toObject().value(QStringLiteral("sessions")).toArray().size() == 1,
				   "model recoveries lists the staged session");
			const auto copyPath = listModelRecoveries(cliDirectory).records.value(0).path;
			const auto recovered = folder(QStringLiteral("cli/recovered.mesh.json"));
			const auto strokes = folder(QStringLiteral("cli/strokes.json"));
			const auto replayed = folder(QStringLiteral("cli/replayed.mesh.json"));
			expect(run({QStringLiteral("model"), QStringLiteral("recover"), copyPath, QStringLiteral("--output"), recovered,
						QStringLiteral("--strokes-output"), strokes},
					   &json) == 0 &&
					   QFileInfo::exists(strokes),
				   "model recover writes the staged strokes");
			expect(run({QStringLiteral("model"), QStringLiteral("weight-paint"), recovered, QStringLiteral("--stroke-file"), strokes,
						QStringLiteral("--output"), replayed},
					   &json) == 0,
				   "model weight-paint replays them");
			ModelDocument replay;
			expect(replay.load(replayed, &error) && replay.revisionFingerprint() == expected.revisionFingerprint(),
				   "the CLI replay gives the same weights as the review");
			expect(run({QStringLiteral("model"), QStringLiteral("recover"), copyPath, QStringLiteral("--output"),
						folder(QStringLiteral("cli/again.mesh.json")), QStringLiteral("--curves-output"), folder(QStringLiteral("cli/c.json"))},
					   &json) == 3,
				   "asking for curves the copy does not keep is refused");
		}
	}
	StudioSettings::setOverrideFilePath(QString());
	if (failures && !error.isEmpty())
		std::cerr << qPrintable(error) << '\n';
	std::cout << failures << " failures\n";
	return failures ? 1 : 0;
}
