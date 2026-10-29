#include "app/model_animation_curves_dialog.h"
#include "app/studio_theme.h"
#include "core/model_rotation_tracks.h"
#include "core/studio_settings.h"
#include "tests/model_pose_clipboard_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAbstractItemModel>
#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableView>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <numbers>

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
bool accessibleControl(QWidget *w)
{
	return w && QAccessible::queryAccessibleInterface(w) && (w->focusPolicy() & Qt::TabFocus) && !w->accessibleName().isEmpty() &&
		   !w->accessibleDescription().isEmpty();
}
std::array<double, 4> aboutZ(double degrees)
{
	const double half = degrees * std::numbers::pi / 360;
	return {0, 0, std::sin(half), std::cos(half)};
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir tmp(QDir(root).filePath("rotation-tracks-ui-XXXXXX"));
	if (!tmp.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(tmp.path()).filePath("settings.ini"));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("quaternion rotation keys UI"); unavailable >= 0)
		return unavailable;
	QString error;
	auto keyed = symmetricRigFixture();
	ModelEdit capture;
	capture.kind = ModelEditKind::EditAnimationCurves;
	capture.curves.clip = 0;
	capture.curves.joint = "l_arm";
	if (!expect(applyModelEdit(&keyed, capture, nullptr, &error), "capture the arm's curves"))
		return 1;
	const int first = keyed.skeleton.bakedClipForFrame.indexOf(0);
	for (int scenario = 0; scenario < 2; ++scenario)
	{
		app.setLayoutDirection(scenario ? Qt::RightToLeft : Qt::LeftToRight);
		applyStudioTheme(app, studioThemeTokens(scenario ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scenario ? 200 : 100));
		ModelAnimationCurvesDialog dialog(keyed, "l_arm", first + 1);
		dialog.resize(1040, 860);
		dialog.show();
		QCoreApplication::processEvents();
		auto *mode = dialog.findChild<QComboBox *>("curvesRotationMode");
		auto *everySample = dialog.findChild<QCheckBox *>("curvesConvertEverySample");
		auto *channel = dialog.findChild<QComboBox *>("curvesChannel");
		auto *table = dialog.findChild<QTableView *>("curvesKeys");
		if (!expect(mode && everySample && channel && table && accessibleControl(mode) && accessibleControl(everySample) && mode->currentIndex() == 0,
					"the curves dialog offers Euler or quaternion rotation keys"))
			return 1;
		channel->setCurrentIndex(5);
		mode->setCurrentIndex(1);
		expect(table->model()->rowCount() == 4 && table->model()->data(table->model()->index(2, 0)).toInt() == 2,
			   "converting stages quaternion keys shown in the angle view");
		expect(!dialog.findChild<QPushButton *>("curvesCopyKeys")->isEnabled() && !dialog.findChild<QComboBox *>("curvesTangent")->isEnabled(),
			   "quaternion rotation keys have no tangents or key copies");
		dialog.findChild<QSpinBox *>("curvesKeyFrame")->setValue(1);
		dialog.findChild<QDoubleSpinBox *>("curvesKeyValue")->setValue(30);
		dialog.findChild<QPushButton *>("curvesSetKey")->click();
		dialog.review();
		const auto group = dialog.edit().curves.group;
		expect(dialog.ready() && group.rotation.size() == 4 && modelQuatAngleDegrees(group.rotation[1].value, aboutZ(30)) < 1e-3,
			   "a key set in the angle view becomes a quaternion key");
		ModelMesh candidate = keyed;
		expect(applyModelEdit(&candidate, dialog.edit(), nullptr, &error) && !candidate.skeleton.clips[0].curves[0].rotation.isEmpty(),
			   "the reviewed quaternion keys apply");
		channel->setCurrentIndex(0);
		expect(dialog.findChild<QPushButton *>("curvesCopyKeys")->isEnabled(), "translation channels still copy");
		everySample->setChecked(true);
		mode->setCurrentIndex(0);
		dialog.review();
		expect(dialog.ready() && dialog.edit().curves.group.rotation.isEmpty() && dialog.edit().curves.group.channels[5].size() == 4,
			   "converting back stages Euler channels with a key at every sample");
		dialog.reject();
	}
	std::cout << checks << " quaternion rotation UI checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
