#include "app/model_constraints_dialog.h"
#include "app/studio_theme.h"
#include "core/model_animation_constraints.h"
#include "core/studio_settings.h"
#include "tests/model_pose_clipboard_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAccessible>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
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
	QTemporaryDir tmp(QDir(root).filePath("constraints-ui-XXXXXX"));
	if (!tmp.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(tmp.path()).filePath("settings.ini"));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("constraints UI"); unavailable >= 0)
		return unavailable;
	QString error;
	const auto mesh = symmetricRigFixture();
	const int frame = mesh.skeleton.bakedClipForFrame.indexOf(0);
	for (int scenario = 0; scenario < 2; ++scenario)
	{
		app.setLayoutDirection(scenario ? Qt::RightToLeft : Qt::LeftToRight);
		applyStudioTheme(app, studioThemeTokens(scenario ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scenario ? 200 : 100));
		ModelConstraintsDialog dialog(mesh, frame);
		dialog.show();
		QCoreApplication::processEvents();
		for (const char *id : {"constraintsClip", "constraintsList", "constraintKind", "constraintJoint", "constraintRoot", "constraintTarget",
							   "constraintPoint0", "constraintPole2", "constraintAxis", "constraintFirst", "constraintLast", "constraintWeight",
							   "constraintAdd", "constraintsLayers", "layerWeight"})
		{
			auto *w = dialog.findChild<QWidget *>(id);
			expect(w && QAccessible::queryAccessibleInterface(w) && (w->focusPolicy() & Qt::TabFocus) && !w->accessibleName().isEmpty() &&
					   !w->accessibleDescription().isEmpty(),
				   "constraint control has an accessible name, description and keyboard focus");
		}
		expect(!dialog.findChild<QPushButton *>("constraintsBake")->isEnabled() && !dialog.findChild<QPushButton *>("constraintUpdate")->isEnabled(),
			   "bake and update wait for a stack and a selection");
		auto *kind = dialog.findChild<QComboBox *>("constraintKind");
		auto *joint = dialog.findChild<QComboBox *>("constraintJoint");
		auto *target = dialog.findChild<QComboBox *>("constraintTarget");
		kind->setCurrentIndex(kind->findData(int(ModelClipConstraintKind::ChildOf)));
		joint->setCurrentIndex(joint->findData("l_arm"));
		target->setCurrentIndex(target->findData("l_hand"));
		dialog.choose(ModelClipStackAction::AddConstraint);
		expect(dialog.result() != QDialog::Accepted && dialog.isVisible(), "a cycle is explained in the dialog instead of applied");
		kind->setCurrentIndex(kind->findData(int(ModelClipConstraintKind::CopyTransform)));
		joint->setCurrentIndex(joint->findData("r_hand"));
		dialog.choose(ModelClipStackAction::AddConstraint);
		const auto edit = dialog.edit();
		auto candidate = mesh;
		expect(dialog.result() == QDialog::Accepted && edit.kind == ModelEditKind::EditClipStack &&
				   edit.animation.stack.constraint.kind == ModelClipConstraintKind::CopyTransform && applyModelEdit(&candidate, edit, nullptr, &error) &&
				   modelClipHasStack(candidate.skeleton.clips[0]),
			   "the dialog adds a constraint");

		ModelConstraintsDialog stacked(candidate, frame);
		stacked.show();
		QCoreApplication::processEvents();
		auto *list = stacked.findChild<QListWidget *>("constraintsList");
		expect(list && list->count() == 1, "the stack is listed");
		list->setCurrentRow(0);
		expect(stacked.findChild<QComboBox *>("constraintJoint")->currentData().toString() == "r_hand" &&
				   stacked.findChild<QPushButton *>("constraintUpdate")->isEnabled() && stacked.findChild<QPushButton *>("constraintsBake")->isEnabled(),
			   "selecting a constraint loads it for editing");
		stacked.findChild<QDoubleSpinBox *>("constraintWeight")->setValue(.25);
		stacked.choose(ModelClipStackAction::UpdateConstraint);
		auto updated = candidate;
		expect(stacked.edit().animation.stack.index == 0 && stacked.edit().animation.stack.constraint.weight == .25 &&
				   applyModelEdit(&updated, stacked.edit(), nullptr, &error),
			   "the dialog updates the selected constraint");
		ModelConstraintsDialog bake(updated, frame);
		bake.choose(ModelClipStackAction::Bake);
		auto baked = updated;
		expect(applyModelEdit(&baked, bake.edit(), nullptr, &error) && !modelClipHasStack(baked.skeleton.clips[0]), "the dialog bakes the stack");
	}
	std::cout << checks << " constraint UI checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
