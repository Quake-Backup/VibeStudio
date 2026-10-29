#include "app/model_clip_import_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_pose_clipboard_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool ok, const char *name)
{
	++checks;
	if (!ok)
	{
		++failures;
		std::cerr << "FAIL: " << name << '\n';
	}
	return ok;
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir tmp(QDir(root).filePath("clip-import-ui-XXXXXX"));
	if (!tmp.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(tmp.path()).filePath("settings.ini"));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("clip import UI"); unavailable >= 0)
		return unavailable;
	QString error;
	auto source = symmetricRigFixture();
	const auto tall = symmetricRigFixture(4, 1.5f);
	// The source keys its arm and carries a footstep event.
	ModelEdit capture;
	capture.kind = ModelEditKind::EditAnimationCurves;
	capture.curves.clip = 0;
	capture.curves.joint = "l_arm";
	ModelEdit step;
	step.kind = ModelEditKind::EditAnimationEvents;
	step.animation.events.clip = 0;
	step.animation.events.event = {2, "sound_body", {"snd_step"}};
	if (!expect(applyModelEdit(&source, capture, nullptr, &error) && applyModelEdit(&source, step, nullptr, &error), "key the source"))
		return 1;
	const auto sourcePath = QDir(tmp.path()).filePath("walk.mesh.json");
	ModelDocument saved;
	if (!expect(saved.setMesh(source, &error) && saved.save(sourcePath, false, &error), "save the animation source"))
		return 1;
	for (int scenario = 0; scenario < 2; ++scenario)
	{
		app.setLayoutDirection(scenario ? Qt::RightToLeft : Qt::LeftToRight);
		applyStudioTheme(app, studioThemeTokens(scenario ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scenario ? 200 : 100));
		ModelClipImportDialog dialog(tall);
		dialog.resize(900, 760);
		dialog.show();
		QCoreApplication::processEvents();
		auto *apply = dialog.findChild<QPushButton *>("clipImportApply");
		expect(apply && !apply->isEnabled() && !dialog.ready(), "nothing can be imported before a source is chosen");
		for (const char *id : {"clipImportChoose", "clipImportClip", "clipImportName", "clipImportFps", "clipImportMode", "clipImportRootScale",
							   "clipImportFill", "clipImportMapping"})
		{
			auto *w = dialog.findChild<QWidget *>(id);
			expect(w && QAccessible::queryAccessibleInterface(w) && (w->focusPolicy() & Qt::TabFocus) && !w->accessibleName().isEmpty() &&
					   !w->accessibleDescription().isEmpty(),
				   "import control has an accessible name, description and keyboard focus");
		}
		expect(dialog.loadSource(sourcePath), "read the source's clips");
		auto *mapping = dialog.findChild<QTableWidget *>("clipImportMapping");
		expect(mapping && mapping->rowCount() == 6 && dialog.findChild<QLineEdit *>("clipImportName")->text() == "walk" &&
				   dialog.findChild<QDoubleSpinBox *>("clipImportFps")->value() == 12,
			   "the mapping lists every joint and the clip's name and rate are filled in");
		// The name walk is taken in the destination: rename it.
		expect(!dialog.ready(), "a clip name already in use cannot be applied");
		dialog.findChild<QLineEdit *>("clipImportName")->setText("walk_tall");
		dialog.findChild<QComboBox *>("clipImportMode")->setCurrentIndex(1);
		expect(dialog.ready() && apply->isEnabled(), "a retargeted import is ready to apply");
		const auto edit = dialog.edit();
		ModelMesh candidate = tall;
		expect(edit.kind == ModelEditKind::ImportSkeletalClip && edit.animation.import.retarget && applyModelEdit(&candidate, edit, nullptr, &error) &&
				   candidate.skeleton.clips.last().name == "walk_tall",
			   "the dialog's edit imports the clip");
		// The source's curves and events, and the side-by-side preview.
		auto *curves = dialog.findChild<QCheckBox *>("clipImportCurves");
		auto *events = dialog.findChild<QCheckBox *>("clipImportEvents");
		auto *preview = dialog.findChild<QPushButton *>("clipImportPreview");
		expect(curves && events && preview && curves->isChecked() && events->isChecked() && preview->isEnabled() &&
				   !curves->accessibleDescription().isEmpty() && !preview->accessibleDescription().isEmpty(),
			   "curves, events and the preview are offered");
		expect(edit.animation.import.events.size() == 1 && edit.animation.import.curves.size() == 1, "the edit carries the source's events and curves");
		events->setChecked(false);
		expect(dialog.edit().animation.import.events.isEmpty() && dialog.ready(), "clearing events leaves them out");
		events->setChecked(true);
		expect(dialog.updatePreview() && dialog.previewed(), "the preview imports into a copy");
		auto *sourceView = dialog.findChild<ModelViewport *>("clipImportSourceView");
		auto *resultView = dialog.findChild<ModelViewport *>("clipImportResultView");
		auto *sample = dialog.findChild<QSpinBox *>("clipImportSample");
		expect(sourceView && resultView && sample && sample->maximum() == 3 && !sourceView->accessibleName().isEmpty() &&
				   !resultView->accessibleName().isEmpty() && resultView->frameCount() > 0,
			   "the source and the result show side by side");
		sample->setValue(2);
		QCoreApplication::processEvents();
		ModelJointMatrix sourceHand, resultHand;
		expect(sourceView->jointPose("l_arm", &sourceHand) && resultView->jointPose("l_arm", &resultHand) &&
				   [&]
				   {
					   const auto a = modelJointRotation(sourceHand), b = modelJointRotation(resultHand);
					   return std::abs(std::abs(double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z + double(a.w) * b.w) - 1) < 1e-4;
				   }(),
			   "both previews show the same sample of the arm's swing");
		dialog.findChild<QDoubleSpinBox *>("clipImportFps")->setValue(30);
		expect(!dialog.previewed(), "a changed setting marks the result preview stale");
		auto *handRow = qobject_cast<QComboBox *>(mapping->cellWidget(3, 1));
		expect(handRow && !handRow->accessibleName().isEmpty() && handRow->currentText() == "l_hand", "joints with the same name match by default");
		handRow->setCurrentIndex(handRow->findText("r_hand"));
		expect(!dialog.ready(), "remapping a joint leaves its old joint without a source");
		dialog.reject();
	}
	std::cout << checks << " clip import UI checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
