#include "app/model_ik_dialog.h"
#include "app/model_rig_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_ik_chain_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
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
	QTemporaryDir tmp(QDir(root).filePath("ik-chain-ui-XXXXXX"));
	if (!tmp.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(tmp.path()).filePath("settings.ini"));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("chain inverse kinematics UI"); unavailable >= 0)
		return unavailable;
	const auto mesh = ikChainFixture(5);
	QString error;
	for (int scenario = 0; scenario < 2; ++scenario)
	{
		app.setLayoutDirection(scenario ? Qt::RightToLeft : Qt::LeftToRight);
		applyStudioTheme(app, studioThemeTokens(scenario ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scenario ? 200 : 100));
		ModelIkDialog dialog(mesh, QStringLiteral("j4"), 1);
		dialog.resize(1000, 860);
		dialog.show();
		QCoreApplication::processEvents();
		auto *solver = dialog.findChild<QComboBox *>("ikSolver");
		auto *rootJoint = dialog.findChild<QComboBox *>("ikRootJoint");
		auto *source = dialog.findChild<QComboBox *>("ikTargetSource");
		auto *cursor = dialog.findChild<QPushButton *>("ikTargetCursor");
		if (!expect(solver && rootJoint && source && cursor && dialog.findChild<QSpinBox *>("ikIterations") &&
						dialog.findChild<QCheckBox *>("ikLimits") && dialog.findChild<QCheckBox *>("ikUsePole"),
					"chain controls exist"))
			return 1;
		for (const char *id : {"ikSolver", "ikRootJoint", "ikTargetSource", "ikTargetJoint", "ikIterations", "ikLimits", "ikUsePole",
							   "ikTargetCursor", "ikPoleCursor"})
		{
			auto *w = dialog.findChild<QWidget *>(id);
			expect(w && QAccessible::queryAccessibleInterface(w) && (w->focusPolicy() & Qt::TabFocus) && !w->accessibleName().isEmpty() &&
					   !w->accessibleDescription().isEmpty(),
				   "chain control has an accessible name, description and keyboard focus");
		}
		expect(!rootJoint->isVisibleTo(&dialog) && !cursor->isEnabled(), "two-bone mode hides the root joint; the cursor needs a point");
		solver->setCurrentIndex(1);
		expect(rootJoint->isVisibleTo(&dialog) && rootJoint->findText("j0") >= 0 && rootJoint->findText("j3") < 0,
			   "the root joint lists ancestors two or more bones up");
		rootJoint->setCurrentIndex(rootJoint->findText("j0"));
		expect(dialog.findChild<QLabel *>("ikChain")->text().contains("j1") && dialog.findChild<QLabel *>("ikChain")->text().contains("j0"),
			   "the resolved chain names every joint");
		dialog.setCursorPoint({3, 4, 0});
		expect(cursor->isEnabled(), "the cursor buttons follow the editor's 3D cursor");
		cursor->click();
		expect(dialog.findChild<QDoubleSpinBox *>("ikTarget0")->value() == 3 && dialog.findChild<QDoubleSpinBox *>("ikTarget1")->value() == 4,
			   "Target at 3D Cursor fills the target");
		dialog.findChild<QSpinBox *>("ikIterations")->setValue(200);
		dialog.findChild<QSpinBox *>("ikLast")->setValue(2);
		dialog.review();
		const auto edit = dialog.edit();
		expect(dialog.ready() && edit.kind == ModelEditKind::SolveChainIk && edit.ik.rootJoint == "j0" && edit.ik.solver == ModelIkSolver::Fabrik,
			   "review prepares a chain solve");
		ModelMesh candidate = mesh;
		expect(applyModelEdit(&candidate, edit, nullptr, &error), "the reviewed chain edit applies");
		// The preview's drag handle: the shared move gizmo on the target, then the pole.
		auto *handle = dialog.findChild<QComboBox *>("ikHandle");
		auto *tabs = dialog.findChild<QTabWidget *>("ikTabs");
		auto *preview = dialog.preview();
		expect(handle && !handle->accessibleName().isEmpty() && !handle->accessibleDescription().isEmpty() && handle->currentIndex() == 1,
			   "the preview offers a target drag handle");
		tabs->setCurrentIndex(1);
		QCoreApplication::processEvents();
		expect(settleModelViewport(*preview), "the IK preview settles");
		const auto dragAlongX = [&](const char *label)
		{
			const auto points = preview->transformGizmoPoints();
			const QPointF tip = points[0], centre = points[3];
			const auto along = tip - centre;
			const double length = std::hypot(along.x(), along.y());
			if (!std::isfinite(length) || length < 4)
				return expect(false, label);
			const auto step = along * (40.0 / length);
			const bool begun = preview->beginEditTransform(tip);
			preview->updateEditTransform(tip + step * .5);
			preview->updateEditTransform(tip + step);
			preview->finishEditTransform(true);
			return expect(begun, label);
		};
		const double before = dialog.findChild<QDoubleSpinBox *>("ikTarget0")->value();
		dragAlongX("the target handle starts a drag on its X arrow");
		const double after = dialog.findChild<QDoubleSpinBox *>("ikTarget0")->value();
		expect(after > before + .01 && std::abs(dialog.findChild<QDoubleSpinBox *>("ikTarget1")->value() - 4) < 1e-6 && !dialog.ready(),
			   "dropping the target handle moves the target along X and needs a new review");
		expect(std::abs(dialog.targetPoint().x - after) < 1e-4, "the handle and the target fields agree");
		dialog.setHandle(2);
		const double pole = dialog.findChild<QDoubleSpinBox *>("ikPole0")->value();
		dragAlongX("the pole handle starts a drag on its X arrow");
		expect(dialog.findChild<QDoubleSpinBox *>("ikPole0")->value() > pole + .01 &&
				   std::abs(dialog.findChild<QDoubleSpinBox *>("ikTarget0")->value() - after) < 1e-9,
			   "dropping the pole handle moves only the pole");
		dialog.setHandle(0);
		expect(preview->transformGizmoAt(preview->transformGizmoPoints()[3]) < 0, "no handle hides the gizmo");
		tabs->setCurrentIndex(0);
		source->setCurrentIndex(1);
		expect(dialog.findChild<QComboBox *>("ikTargetJoint")->isVisibleTo(&dialog), "following a joint shows the joint choice");
		dialog.reject();

		ModelRigDialog rig(mesh, {}, QStringLiteral("j1"));
		rig.show();
		QCoreApplication::processEvents();
		auto *operation = rig.findChild<QComboBox *>("rigOperation");
		operation->setCurrentIndex(operation->findData(int(ModelEditKind::SetJointLimits)));
		auto *enabled = rig.findChild<QComboBox *>("rigLimitsEnabled");
		auto *maxZ = rig.findChild<QDoubleSpinBox *>("rigLimitMax2");
		expect(enabled && maxZ && enabled->isVisibleTo(&rig) && !maxZ->accessibleName().isEmpty(), "Edit Skeleton offers joint limits");
		maxZ->setValue(30);
		rig.findChild<QDoubleSpinBox *>("rigLimitMin2")->setValue(-30);
		const auto limits = rig.edit();
		expect(limits.kind == ModelEditKind::SetJointLimits && limits.animation.limits.joint == "j1" && limits.animation.limits.limits.enabled &&
				   limits.animation.limits.limits.maximum[2] == 30 && limits.rig.joint.isEmpty(),
			   "the dialog builds a joint limits edit");
		candidate = mesh;
		expect(applyModelEdit(&candidate, limits, nullptr, &error) && candidate.skeleton.joints[1].limits.minimum[2] == -30, "joint limits apply");
		auto *mode = rig.findChild<QComboBox *>("rigLimitMode");
		auto *twist = rig.findChild<QComboBox *>("rigTwistAxis");
		expect(mode && twist && mode->isVisibleTo(&rig) && !mode->accessibleDescription().isEmpty() && !twist->isEnabled(),
			   "Edit Skeleton offers swing-twist limits");
		mode->setCurrentIndex(1);
		expect(twist->isEnabled(), "swing and twist enables the twist axis");
		twist->setCurrentIndex(0);
		const auto swing = rig.edit();
		candidate = mesh;
		expect(swing.animation.limits.limits.swingTwist && swing.animation.limits.limits.twistAxis == 0 &&
				   applyModelEdit(&candidate, swing, nullptr, &error) && candidate.skeleton.joints[1].limits.swingTwist,
			   "the dialog builds swing-twist limits");
		rig.reject();
	}
	std::cout << checks << " chain IK UI checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
