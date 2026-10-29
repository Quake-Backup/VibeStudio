#include "app/model_animation_curves_dialog.h"
#include "app/model_curve_keys_dialog.h"
#include "app/model_editor_dialog.h"
#include "app/model_editor_pose.h"
#include "app/model_editor_tools.h"
#include "app/model_skeletal_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/model_editor_controls.h"
#include "core/studio_settings.h"
#include "tests/model_pose_clipboard_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QMimeData>
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
bool near(ModelVec3 a, ModelVec3 b)
{
	return std::abs(a.x - b.x) <= 1e-4 && std::abs(a.y - b.y) <= 1e-4 && std::abs(a.z - b.z) <= 1e-4;
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temp(QDir(root).filePath("pose-clipboard-ui-XXXXXX"));
	if (!temp.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("pose clipboard UI"); unavailable >= 0)
		return unavailable;
	const auto source = symmetricRigFixture();
	QString error;
	for (int scenario = 0; scenario < 2; ++scenario)
	{
		app.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		applyStudioTheme(app, studioThemeTokens(scenario == 0 ? StudioTheme::Dark : StudioTheme::HighContrastDark, UiDensity::Standard,
												scenario ? 200 : 100));
		ModelEditorDialog editor;
		editor.setAttribute(Qt::WA_DeleteOnClose, false);
		editor.resize(1400, 900);
		editor.show();
		app.processEvents();
		expect(editor.setMesh(source, &error), "open the symmetric rig");
		auto *tools = editor.tools();
		auto *view = editor.findChild<ModelViewport *>("meshPreview");
		if (!expect(tools && view, "editor exposes tools and viewport"))
			return 1;
		for (const auto &id : {"meshCopyPose", "meshPastePose", "meshPastePoseMirrored", "meshMirrorPose"})
		{
			ModelEditorCommand command;
			expect(tools->action(id) && tools->searchableActions().contains(tools->action(id)) && modelEditorCommandForId(id, &command) &&
					   command.category == QStringLiteral("animation"),
				   "pose clipboard commands are registered and searchable");
			auto *button = editor.findChild<QPushButton *>(QString::fromLatin1(id) + QStringLiteral("Button"));
			expect(button && !button->accessibleName().isEmpty() && !button->accessibleDescription().isEmpty(),
				   "Skeleton page buttons carry accessible names and descriptions");
		}
		const auto &mesh = editor.document().mesh();
		const int first = mesh.skeleton.bakedClipForFrame.indexOf(0);
		view->setFrame(first + 3);
		expect(settleModelViewport(*view), "viewport settles on the last walk sample");
		QGuiApplication::clipboard()->clear();
		tools->action("meshCopyPose")->trigger();
		const auto *data = QGuiApplication::clipboard()->mimeData();
		expect(data && data->hasFormat(QString::fromLatin1(modelPoseClipboardMimeType)) && !modelPoseClipboardFromSystem().isEmpty(),
			   "Copy Pose puts the pose MIME data on the clipboard");
		view->setFrame(first + 1);
		expect(settleModelViewport(*view), "viewport settles on the second sample");
		tools->action("meshPastePose")->trigger();
		const auto &pasted = editor.document().mesh();
		expect(editor.document().canUndo() && near(modelJointTranslation(pasted.skeleton.clips[0].frames[1][3]),
												   modelJointTranslation(source.skeleton.clips[0].frames[3][3])),
			   "Paste Pose writes the copied pose at the shown sample as one undo step");
		editor.findChild<QAction *>("undoMesh")->trigger();
		expect(!editor.document().canUndo(), "undo the paste");
		tools->action("meshMirrorPose")->trigger();
		const auto left = modelJointTranslation(source.skeleton.clips[0].frames[1][3]);
		expect(near(modelJointTranslation(editor.document().mesh().skeleton.clips[0].frames[1][5]), {left.x, -left.y, left.z}),
			   "Mirror Pose reflects the shown pose onto the partner joints");
		editor.findChild<QAction *>("undoMesh")->trigger();
		expect(!editor.document().canUndo(), "undo the mirror");

		// The Edit Skeletal Animation dialog copies with Ctrl+C and pastes as an operation.
		ModelSkeletalDialog dialog(editor.document().mesh(), editor.document().selection(), QString(), first + 2, &editor);
		dialog.show();
		app.processEvents();
		auto *copy = dialog.findChild<QPushButton *>("copySkeletalPose");
		auto *operation = dialog.findChild<QComboBox *>("skeletalOperation");
		expect(copy && copy->shortcut() == QKeySequence(QKeySequence::Copy) && !copy->accessibleDescription().isEmpty(),
			   "the dialog's copy button takes Ctrl+C");
		QGuiApplication::clipboard()->clear();
		copy->click();
		ModelPoseClipboard clipboard;
		expect(parseModelPoseClipboard(modelPoseClipboardFromSystem(), &clipboard) && clipboard.samples == 1 && clipboard.first == 2,
			   "the dialog copies its pose range");
		operation->setCurrentIndex(operation->findData(int(ModelEditKind::PastePose)));
		auto *mirror = dialog.findChild<QCheckBox *>("skeletalPasteMirror");
		expect(mirror && mirror->isVisibleTo(&dialog) && dialog.findChild<QCheckBox *>("skeletalPasteReplaceKeys")->isVisibleTo(&dialog),
			   "paste shows its mirror and key options");
		mirror->setChecked(true);
		dialog.findChild<QSpinBox *>("skeletalFirst")->setValue(0);
		const auto edit = dialog.edit();
		expect(edit.kind == ModelEditKind::PastePose && edit.animation.paste.mirrorAxis == 1 && edit.animation.paste.first == 0 &&
				   editor.applyEdit(edit, &error),
			   "the dialog pastes the copy mirrored");
		dialog.close();

		// Animation Keys copies the selected keys with Ctrl+C.
		auto keyed = source;
		ModelEdit capture;
		capture.kind = ModelEditKind::EditAnimationCurves;
		capture.curves.clip = 0;
		capture.curves.joint = "l_arm";
		expect(applyModelEdit(&keyed, capture, nullptr, &error), "capture keys for the key dialog");
		ModelCurveKeysDialog keys(keyed, "l_arm", first + 1, &editor);
		keys.show();
		app.processEvents();
		auto *copyKeys = keys.findChild<QPushButton *>("curveKeysCopy");
		if (auto *all = keys.findChild<QPushButton *>("curveKeysAll"))
			all->click();
		QGuiApplication::clipboard()->clear();
		expect(copyKeys && copyKeys->shortcut() == QKeySequence(QKeySequence::Copy), "Copy Keys takes Ctrl+C");
		if (copyKeys)
			copyKeys->click();
		expect(parseModelPoseClipboard(modelPoseClipboardFromSystem(), &clipboard) && clipboard.kind == ModelPoseClipboardKind::Keys &&
				   clipboard.joints == QStringList{"l_arm"},
			   "Animation Keys copies the selected keys");
		keys.reject();

		// Animation Curves: Copy Keys and Paste Keys on the draft.
		ModelAnimationCurvesDialog curves(keyed, "l_arm", first + 1, &editor);
		curves.show();
		app.processEvents();
		auto *copyCurve = curves.findChild<QPushButton *>("curvesCopyKeys");
		auto *pasteCurve = curves.findChild<QPushButton *>("curvesPasteKeys");
		auto *channel = curves.findChild<QComboBox *>("curvesChannel");
		auto *keySample = curves.findChild<QSpinBox *>("curvesKeyFrame");
		expect(copyCurve && pasteCurve && channel && keySample && copyCurve->shortcut() == QKeySequence(QKeySequence::Copy) &&
				   pasteCurve->shortcut() == QKeySequence(QKeySequence::Paste) && !copyCurve->accessibleDescription().isEmpty() &&
				   !pasteCurve->accessibleDescription().isEmpty(),
			   "Animation Curves copies with Ctrl+C and pastes with Ctrl+V");
		channel->setCurrentIndex(5);
		QGuiApplication::clipboard()->clear();
		curves.copyKeys();
		expect(parseModelPoseClipboard(modelPoseClipboardFromSystem(), &clipboard) && clipboard.kind == ModelPoseClipboardKind::Keys &&
				   clipboard.joints == QStringList{"l_arm"} && clipboard.channels == QVector<int>{5} && clipboard.samples == 4,
			   "Copy Keys copies the shown channel over the clip");
		channel->setCurrentIndex(3);
		keySample->setValue(0);
		curves.pasteKeys();
		curves.review();
		const auto pastedKeys = curves.edit().curves.group.channels;
		bool same = curves.ready() && pastedKeys[3].size() == pastedKeys[5].size();
		for (int i = 0; same && i < pastedKeys[3].size(); ++i)
			same = pastedKeys[3][i].frame == pastedKeys[5][i].frame && std::abs(pastedKeys[3][i].value - pastedKeys[5][i].value) < 1e-6;
		expect(same, "one copied channel pastes onto the shown channel");
		modelPoseClipboardToSystem(copyPoseBytes(keyed, 0, 3, 3, {"l_arm"}));
		keySample->setValue(1);
		curves.pasteKeys();
		curves.review();
		const auto posed = curves.edit().curves.group.channels[5];
		bool atOne = false;
		for (const auto &k : posed)
			atOne = atOne || (k.frame == 1 && std::abs(k.value - 60) < 1e-3);
		expect(curves.ready() && atOne, "a copied pose keys all nine channels at the key sample");
		QGuiApplication::clipboard()->clear();
		curves.pasteKeys();
		expect(curves.findChild<QLabel *>("curvesStatus")->text().contains(QStringLiteral("clipboard")), "an empty clipboard is reported");
		curves.reject();

		// Edit Skeletal Animation: a copy from another rig lands through the joint map.
		auto other = source;
		for (auto &joint : other.skeleton.joints)
			joint.name = joint.name.toUpper();
		modelPoseClipboardToSystem(copyPoseBytes(other, 0, 2, 2));
		ModelSkeletalDialog mapped(editor.document().mesh(), editor.document().selection(), QString(), first, &editor);
		mapped.show();
		app.processEvents();
		auto *mappedOperation = mapped.findChild<QComboBox *>("skeletalOperation");
		mappedOperation->setCurrentIndex(mappedOperation->findData(int(ModelEditKind::PastePose)));
		auto *map = mapped.findChild<QTableWidget *>("skeletalPasteJointMap");
		auto *applyMapped = mapped.findChild<QPushButton *>("applySkeletalEdit");
		expect(map && map->isVisibleTo(&mapped) && map->rowCount() == 6 && !map->accessibleDescription().isEmpty(),
			   "paste lists every copied joint in the joint map");
		const auto mapEdit = mapped.edit();
		expect(mapEdit.animation.paste.joints.size() == 6 && mapEdit.animation.paste.joints.value("L_ARM") == "l_arm" && applyMapped->isEnabled(),
			   "copied joints match this rig's joints whatever their case");
		ModelMesh mappedResult = editor.document().mesh();
		expect(applyModelEdit(&mappedResult, mapEdit, nullptr, &error) &&
				   near(modelJointTranslation(mappedResult.skeleton.clips[0].frames[0][3]), modelJointTranslation(source.skeleton.clips[0].frames[2][3])),
			   "the mapped paste lands on this rig's joints");
		auto *lArm = qobject_cast<QComboBox *>(map->cellWidget(2, 1));
		lArm->setCurrentIndex(0);
		expect(!applyMapped->isEnabled(), "an unmapped copied joint blocks Apply");
		lArm->setCurrentIndex(lArm->findData(QStringLiteral("l_arm")));
		expect(applyMapped->isEnabled(), "mapping it again allows Apply");
		mapped.close();
		expect(editor.setMesh(source, &error), "restore the clean fixture before closing");
		editor.close();
		app.processEvents();
	}
	std::cout << checks << " pose clipboard UI checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
