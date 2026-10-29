#include "app/model_ik_dialog.h"
#include "app/model_rig_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_ik_chain_test_helpers.h"
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
		rig.reject();
	}
	std::cout << checks << " chain IK UI checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
