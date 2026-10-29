#include "app/model_constraints_dialog.h"
#include "app/studio_theme.h"
#include "core/model_animation_constraints.h"
#include "core/studio_settings.h"
#include "tests/model_pose_clipboard_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
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
							   "constraintAdd", "constraintsLayers", "layerWeight", "constraintWeightKeys", "constraintLimits", "layerName", "layerJoint",
							   "layerChannel", "layerKeySample", "layerKeyValue", "layerSetKey", "layerRemoveKey", "layerAdd"})
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

		// Keying a new layer in the dialog, with its weight keys.
		ModelConstraintsDialog layers(mesh, frame);
		layers.show();
		QCoreApplication::processEvents();
		auto *addLayer = layers.findChild<QPushButton *>("layerAdd");
		expect(addLayer && !addLayer->isEnabled(), "a layer needs keys before it can be added");
		expect(layers.setLayerKey("l_arm", 5, 2, 30), "key the arm's rotation in the layer draft");
		const auto &draft = layers.layerDraft();
		expect(draft.curves.size() == 1 && draft.curves[0].joint == "l_arm" && draft.curves[0].channels[5].size() == 2 &&
				   draft.curves[0].channels[5][1].value == 30 && draft.curves[0].channels[0].size() == 1 && draft.curves[0].channels[6][0].value == 1,
			   "an additive layer starts neutral and takes the key");
		expect(layers.setLayerKey("l_arm", 9, 0, 0) && layers.setLayerKey("l_arm", 9, 3, 1) && layers.layerDraft().weightKeys.size() == 2,
			   "the layer weight is keyed on its own channel");
		expect(!layers.setLayerKey("l_arm", 9, 1, 1.5) && layers.layerDraft().weightKeys.size() == 2, "a weight above 1 is refused");
		expect(layers.removeLayerKey("l_arm", 9, 3) && layers.layerDraft().weightKeys.size() == 1, "remove a weight key");
		expect(addLayer->isEnabled(), "a keyed layer can be added");
		layers.choose(ModelClipStackAction::AddLayer);
		auto layered = mesh;
		expect(layers.result() == QDialog::Accepted && layers.edit().animation.stack.action == ModelClipStackAction::AddLayer &&
				   layers.edit().animation.stack.layer.curves.size() == 1 && applyModelEdit(&layered, layers.edit(), nullptr, &error) &&
				   layered.skeleton.clips[0].layers.size() == 1,
			   "the dialog adds the keyed layer");
		ModelConstraintsDialog keyed(layered, frame);
		keyed.show();
		QCoreApplication::processEvents();
		keyed.findChild<QListWidget *>("constraintsLayers")->setCurrentRow(0);
		expect(keyed.layerDraft() == layered.skeleton.clips[0].layers[0] && keyed.setLayerKey("l_arm", 3, 1, 15), "a selected layer loads as the draft");
		keyed.choose(ModelClipStackAction::UpdateLayer);
		auto rekeyed = layered;
		expect(keyed.edit().animation.stack.index == 0 && applyModelEdit(&rekeyed, keyed.edit(), nullptr, &error) &&
				   rekeyed.skeleton.clips[0].layers[0].curves[0].channels[3].size() == 2,
			   "Update Layer applies the draft's keys");

		// Weight keys and limits on a constraint.
		ModelConstraintsDialog form(mesh, frame);
		form.show();
		QCoreApplication::processEvents();
		auto *formKind = form.findChild<QComboBox *>("constraintKind");
		formKind->setCurrentIndex(formKind->findData(int(ModelClipConstraintKind::Ik)));
		form.findChild<QComboBox *>("constraintJoint")->setCurrentIndex(form.findChild<QComboBox *>("constraintJoint")->findData("l_hand"));
		auto *limitsBox = form.findChild<QCheckBox *>("constraintLimits");
		expect(limitsBox && limitsBox->isEnabled(), "IK constraints offer joint limits");
		limitsBox->setChecked(true);
		form.findChild<QLineEdit *>("constraintWeightKeys")->setText("0:1, 3:0.5");
		const auto described = form.formConstraint();
		expect(described.limits && described.weightKeys.size() == 2 && described.weightKeys[1].value == .5, "the form carries weight keys and limits");
		form.findChild<QLineEdit *>("constraintWeightKeys")->setText("0:1, 3");
		form.choose(ModelClipStackAction::AddConstraint);
		expect(form.result() != QDialog::Accepted && form.isVisible(), "unreadable weight keys are explained instead of applied");
		formKind->setCurrentIndex(formKind->findData(int(ModelClipConstraintKind::CopyTransform)));
		expect(!limitsBox->isEnabled() && !form.formConstraint().limits, "constraints without a chain do not keep limits");
		form.reject();
	}
	std::cout << checks << " constraint UI checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
