// Camera drive keys from the 2D views and the saved camera speed, without the
// shell: the NetRadiant Custom speed list and its steps, which drive keys each
// of the 24 profiles passes on from a 2D view and which command keys overlap
// them, that no routed key or speed key takes an accessibility or other
// window-wide key, the editor gestures fields, the controls reference rows,
// the overlap warnings and `editor gestures` / `editor controls` from the
// command line.

#include "core/editor_profiles.h"
#include "core/level_camera_keys.h"
#include "core/level_gestures.h"
#include "core/studio_semantics.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;

namespace {

bool ok = true;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << label << ": " << detail.toUtf8().constData() << std::endl;
	}
	return value;
}

QString commandKey(QString id)
{
	return id.toLower().remove(QLatin1Char('-')).remove(QLatin1Char('_'));
}

QString keyText(const QString& key)
{
	return QKeySequence::fromString(key, QKeySequence::PortableText).toString(QKeySequence::PortableText).toLower();
}

// Every key a profile gives a command that reaches the Levels page: the
// window-wide keys, the Levels ones and the profile's own, keyed by command.
QHash<QString, QStringList> levelKeys(const EditorProfileDescriptor& profile, QHash<QString, QString>* contexts = nullptr)
{
	QHash<QString, QStringList> keys;
	for (const ShortcutDescriptor& descriptor : shortcutDescriptors()) {
		if (descriptor.surfaceScoped && descriptor.context != QLatin1String("map") && descriptor.context != QLatin1String("map-view")) {
			continue;
		}
		QStringList list;
		if (!descriptor.defaultSequence.isEmpty()) {
			list << descriptor.defaultSequence;
		}
		list += descriptor.alternateSequences;
		keys.insert(commandKey(descriptor.commandId), list);
		if (contexts) {
			contexts->insert(commandKey(descriptor.commandId), descriptor.surfaceScoped ? descriptor.context : QStringLiteral("window"));
		}
	}
	for (const EditorProfileBinding& binding : editorProfileImplementedBindings(profile)) {
		keys.insert(commandKey(binding.commandId), editorProfileBindingKeys(binding));
		if (contexts && !contexts->contains(commandKey(binding.commandId))) {
			contexts->insert(commandKey(binding.commandId), QStringLiteral("map"));
		}
	}
	return keys;
}

QStringList routedKeys(const LevelEditorControls& controls)
{
	return planRoutedCameraDriveKeys(controls).all();
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	const QString settingsPath = temp.filePath(QStringLiteral("settings.ini"));
	StudioSettings::setOverrideFilePath(settingsPath);

	// NetRadiant Custom's CamMoveSpeed list: 50, then 250 to 9750 by 250, then 9999.
	const QVector<int>& speeds = levelCameraMoveSpeeds();
	expect(speeds.size() == 41 && speeds.first() == 50 && speeds.at(1) == 250 && speeds.contains(500) && speeds.contains(9750)
			&& speeds.last() == 9999 && std::is_sorted(speeds.cbegin(), speeds.cend()),
		"the speed list", QString::number(speeds.size()));
	expect(levelCameraSteppedMoveSpeed(500, 1, 0) == 750 && levelCameraSteppedMoveSpeed(500, -1, 0) == 250
			&& levelCameraSteppedMoveSpeed(250, -1, 0) == 50 && levelCameraSteppedMoveSpeed(50, -1, 0) == 50
			&& levelCameraSteppedMoveSpeed(50, 1, 0) == 250 && levelCameraSteppedMoveSpeed(9750, 1, 0) == 9999
			&& levelCameraSteppedMoveSpeed(9999, 1, 0) == 9999 && levelCameraSteppedMoveSpeed(9999, -1, 0) == 9750
			&& levelCameraSteppedMoveSpeed(500, 0, 0) == 500,
		"steps through the list and stops at each end");
	expect(levelCameraSteppedMoveSpeed(0, 1, 384) == 500 && levelCameraSteppedMoveSpeed(0, -1, 384) == 250
			&& levelCameraSteppedMoveSpeed(0, -1, 192) == 50 && levelCameraSteppedMoveSpeed(0, 1, 1600) == 1750
			&& levelCameraSteppedMoveSpeed(0, 1, 500) == 750 && levelCameraSteppedMoveSpeed(0, -1, 50) == 0
			&& levelCameraSteppedMoveSpeed(0, 1, std::numeric_limits<double>::quiet_NaN()) == 750
			&& levelCameraSteppedMoveSpeed(300, 1, 0) == 500 && levelCameraSteppedMoveSpeed(300, -1, 0) == 250,
		"the map-scaled speed and an unlisted speed step to the nearest listed speed");

	// The profiles: NetRadiant Custom's audited defaults, the Radiant family's
	// routing, and nothing new elsewhere.
	const LevelEditorControls nrc = netRadiantCustomLevelControls();
	expect(nrc.camera.discreteDriveKeys && nrc.camera.drivePitchLimitDegrees == 90.0 && nrc.camera.moveSpeed == 500 && nrc.plan.cameraDriveKeys,
		"NetRadiant Custom drives in discrete steps to 90 degrees at 500 units per second, from the 2D views too");
	const LevelEditorControls netRadiant = familiarLevelControls(QStringLiteral("netradiant"));
	expect(!netRadiant.camera.discreteDriveKeys && netRadiant.camera.drivePitchLimitDegrees == 85.0 && netRadiant.camera.moveSpeed == 0
			&& netRadiant.plan.cameraDriveKeys,
		"NetRadiant keeps its unaudited continuous drive and map-scaled speed");
	const auto keysOf = [](const LevelEditorControls& controls, const char* id) {
		for (const LevelEditorKeyBinding& binding : controls.keys) {
			if (binding.commandId == QLatin1String(id)) {
				return binding.keys;
			}
		}
		return QStringList();
	};
	expect(keysOf(nrc, "map.cameraSpeedUp") == QStringList {QStringLiteral("Shift+Num++")}
			&& keysOf(nrc, "map.cameraSpeedDown") == QStringList {QStringLiteral("Shift+Num+-")}
			&& keysOf(netRadiant, "map.cameraSpeedUp").isEmpty() && keysOf(trenchBroomLevelControls(), "map.cameraSpeedUp").isEmpty(),
		"Shift+keypad + and - step the speed in NetRadiant Custom only");
	expect(QKeySequence::fromString(QStringLiteral("Shift+Num++"), QKeySequence::PortableText)
			== QKeySequence(Qt::ShiftModifier | Qt::KeypadModifier | Qt::Key_Plus),
		"the speed key is the keypad's plus");
	expect(routedKeys(nrc) == QStringList {QStringLiteral("A"), QStringLiteral("D")}, "NetRadiant Custom's 2D views pass on A and D",
		routedKeys(nrc).join(QLatin1Char(',')));
	expect(routedKeys(gtkRadiantLevelControls()) == QStringList {QStringLiteral(","), QStringLiteral("."), QStringLiteral("D"), QStringLiteral("C")},
		"GtkRadiant 1.6's 2D views pass on comma, period, D and C", routedKeys(gtkRadiantLevelControls()).join(QLatin1Char(' ')));
	const LevelEditorControls q3 = familiarLevelControls(QStringLiteral("q3radiant"));
	expect(routedKeys(q3) == QStringList {QStringLiteral(","), QStringLiteral("."), QStringLiteral("D"), QStringLiteral("C"), QStringLiteral("A"), QStringLiteral("Z")},
		"Q3Radiant's 2D views pass on its look keys too", routedKeys(q3).join(QLatin1Char(' ')));
	expect(routedKeys(trenchBroomLevelControls()).isEmpty() && routedKeys(vibeStudioLevelControls()).isEmpty()
			&& routedKeys(familiarLevelControls(QStringLiteral("quark"))).isEmpty() && routedKeys(familiarLevelControls(QStringLiteral("bsp"))).isEmpty(),
		"profiles whose camera keys need the camera's focus pass nothing on");
	CameraKeyMotion motion;
	expect(levelPlanCameraDriveKey(nrc, Qt::Key_A, Qt::NoModifier, &motion) && motion.right == -1 && motion.forward == 0
			&& levelPlanCameraDriveKey(nrc, Qt::Key_D, Qt::KeypadModifier, &motion) && motion.right == 1,
		"A strafes left and D right, the keypad flag aside");
	expect(!levelPlanCameraDriveKey(nrc, Qt::Key_A, Qt::ShiftModifier) && !levelPlanCameraDriveKey(nrc, Qt::Key_A, Qt::AltModifier)
			&& !levelPlanCameraDriveKey(nrc, Qt::Key_Up, Qt::NoModifier) && !levelPlanCameraDriveKey(nrc, Qt::Key_W, Qt::NoModifier)
			&& !levelPlanCameraDriveKey(nrc, Qt::Key_S, Qt::NoModifier),
		"modified keys, the 2D view's own arrows and the fly keys stay where they were");
	LevelEditorControls custom;
	QJsonObject normalized;
	QString error;
	expect(applyLevelGestureOverrides(nrc, {{"camera.driveKeys.up", "J"}, {"camera.driveKeys.down", "N"}, {"camera.driveKeys.pitchUp", "O"},
				{"camera.driveKeys.pitchDown", "L"}, {"camera.driveKeys.right", "F"}}, &custom, &normalized, &error),
		"NetRadiant Custom takes vertical and pitch drive keys", error);
	expect(levelPlanCameraDriveKey(custom, Qt::Key_J, Qt::NoModifier, &motion) && motion.up == 1
			&& levelPlanCameraDriveKey(custom, Qt::Key_N, Qt::NoModifier, &motion) && motion.up == -1
			&& levelPlanCameraDriveKey(custom, Qt::Key_O, Qt::NoModifier, &motion) && motion.pitch == 1
			&& levelPlanCameraDriveKey(custom, Qt::Key_L, Qt::NoModifier, &motion) && motion.pitch == -1,
		"bound up, down and pitch keys work from the 2D views");
	expect(!levelPlanCameraDriveKey(custom, Qt::Key_F, Qt::NoModifier) && planViewKeepsKey(custom.plan, Qt::Key_F, Qt::NoModifier),
		"F stays the 2D view's framing key even as a drive key");
	LevelEditorControls off;
	expect(applyLevelGestureOverrides(nrc, {{"plan.cameraDriveKeys", "off"}}, &off, nullptr, &error) && routedKeys(off).isEmpty()
			&& !levelPlanCameraDriveKey(off, Qt::Key_A, Qt::NoModifier),
		"turning the 2D view's camera drive keys off", error);
	LevelEditorControls sledgeOn;
	expect(applyLevelGestureOverrides(familiarLevelControls(QStringLiteral("sledge")), {{"plan.cameraDriveKeys", "on"}}, &sledgeOn, nullptr, &error)
			&& routedKeys(sledgeOn).isEmpty(),
		"Sledge's arrow-only drive keys stay with the 2D view's arrows when turned on", error);
	expect(!applyLevelGestureOverrides(vibeStudioLevelControls(), {{"plan.cameraDriveKeys", "on"}}, nullptr, nullptr, &error)
			&& !applyLevelGestureOverrides(vibeStudioLevelControls(), {{"camera.moveSpeed", "500"}}, nullptr, nullptr, &error),
		"the orbiting camera has no drive keys or speed to set");

	// Across all 24 profiles: what a routed key overlaps, and that it never
	// takes a window-wide key, accessibility's above all, nor the speed keys.
	const QVector<EditorProfileDescriptor> profiles = editorProfileDescriptors();
	expect(profiles.size() == 24, "24 profiles", QString::number(profiles.size()));
	QSet<QString> windowWide;
	for (const ShortcutDescriptor& descriptor : shortcutDescriptors()) {
		if (!descriptor.surfaceScoped) {
			for (const QString& key : QStringList {descriptor.defaultSequence} + descriptor.alternateSequences) {
				if (!key.isEmpty()) {
					windowWide.insert(keyText(key));
				}
			}
		}
	}
	int routing = 0;
	for (const EditorProfileDescriptor& profile : profiles) {
		const LevelEditorControls& controls = profile.controls;
		QHash<QString, QString> contexts;
		const QHash<QString, QStringList> keys = levelKeys(profile, &contexts);
		const QStringList routed = routedKeys(controls);
		routing += routed.isEmpty() ? 0 : 1;
		expect(routed.isEmpty() == !controls.plan.cameraDriveKeys || controls.camera.driveKeys.isEmpty(),
			"routing follows the 2D view's choice", profile.id);
		for (const QString& key : routed) {
			const QString text = keyText(key);
			expect(!windowWide.contains(text), "a routed key never takes a window-wide key", profile.id + QLatin1Char(' ') + key);
			for (auto it = keys.cbegin(); it != keys.cend(); ++it) {
				for (const QString& bound : it.value()) {
					if (keyText(bound) != text) {
						continue;
					}
					// Draw Sector's D acts only on a Doom map, where it keeps the key.
					expect(it.key() == commandKey(QStringLiteral("map.drawSector")), "the only routed-key overlap is Draw Sector's D",
						profile.id + QLatin1Char(' ') + key + QLatin1Char(' ') + it.key());
				}
			}
		}
		for (const QString& speedKey : {QStringLiteral("Shift+Num++"), QStringLiteral("Shift+Num+-")}) {
			QStringList users;
			for (auto it = keys.cbegin(); it != keys.cend(); ++it) {
				for (const QString& bound : it.value()) {
					if (keyText(bound) == keyText(speedKey)) {
						users << it.key();
					}
				}
			}
			const QString owner = commandKey(speedKey.endsWith(QLatin1Char('+')) ? QStringLiteral("map.cameraSpeedUp") : QStringLiteral("map.cameraSpeedDown"));
			expect(users.isEmpty() || users == QStringList {owner}, "the speed keys belong to the speed commands alone", profile.id + QLatin1Char(' ') + users.join(QLatin1Char(',')));
			expect(!windowWide.contains(keyText(speedKey)), "the speed keys are no window-wide key", speedKey);
		}
		QString conflict;
		expect(!editorProfileHasShortcutConflict(profile, &conflict), "no profile conflict", profile.id + QLatin1Char(' ') + conflict);
		expect(levelEditorControlProblems(controls).isEmpty(), "valid controls", profile.id + QLatin1Char(' ') + levelEditorControlProblems(controls).join(QLatin1Char(' ')));
		expect(levelCameraShortcutWarnings(profile, controls).isEmpty(), "built-in overlaps are no warnings", profile.id + QLatin1Char(' ')
			+ levelCameraShortcutWarnings(profile, controls).join(QLatin1Char(' ')));
	}
	expect(routing == 9, "nine Radiant-family profiles pass drive keys on", QString::number(routing));

	// The gestures fields.
	QHash<QString, LevelGestureField> fields;
	for (const LevelGestureField& field : levelGestureFields()) {
		fields.insert(field.id, field);
	}
	const LevelGestureField speedField = fields.value(QStringLiteral("camera.moveSpeed"));
	expect(speedField.kind == LevelGestureFieldKind::Choice && speedField.section == QLatin1String("camera") && speedField.options.size() == 42
			&& speedField.options.first().id == QLatin1String("auto") && speedField.options.at(3).id == QLatin1String("500")
			&& speedField.options.last().id == QLatin1String("9999") && !speedField.label.isEmpty(),
		"camera.moveSpeed offers the map-scaled speed and every listed speed");
	expect(fields.value(QStringLiteral("plan.cameraDriveKeys")).section == QLatin1String("plan"), "plan.cameraDriveKeys is a 2D view field");
	expect(levelGestureValues(nrc).value(QStringLiteral("camera.moveSpeed")) == QStringLiteral("500")
			&& levelGestureValues(trenchBroomLevelControls()).value(QStringLiteral("camera.moveSpeed")) == QStringLiteral("auto")
			&& levelGestureValues(nrc).value(QStringLiteral("plan.cameraDriveKeys")) == QStringLiteral("on")
			&& levelGestureValues(trenchBroomLevelControls()).value(QStringLiteral("plan.cameraDriveKeys")) == QStringLiteral("off"),
		"the profiles' values");
	LevelEditorControls faster;
	expect(applyLevelGestureOverrides(nrc, {{"camera.moveSpeed", "750"}}, &faster, &normalized, &error) && faster.camera.moveSpeed == 750
			&& normalized == QJsonObject {{"camera.moveSpeed", "750"}},
		"a listed speed applies", error);
	expect(applyLevelGestureOverrides(nrc, {{"camera.moveSpeed", "500"}}, &faster, &normalized, &error) && normalized.isEmpty(),
		"the profile's own speed leaves no override", error);
	expect(applyLevelGestureOverrides(trenchBroomLevelControls(), {{"camera.moveSpeed", "50"}}, &faster, nullptr, &error) && faster.camera.moveSpeed == 50,
		"TrenchBroom takes a saved speed", error);
	expect(!applyLevelGestureOverrides(nrc, {{"camera.moveSpeed", "600"}}, nullptr, nullptr, &error)
			&& !applyLevelGestureOverrides(nrc, {{"camera.moveSpeed", "-1"}}, nullptr, nullptr, &error),
		"unlisted speeds are refused");
	LevelEditorControls broken = nrc;
	broken.camera.moveSpeed = 600;
	broken.camera.drivePitchLimitDegrees = 0.0;
	expect(levelEditorControlProblems(broken).size() == 2, "an unlisted speed and a zero pitch stop are problems", levelEditorControlProblems(broken).join(QLatin1Char(' ')));

	// The controls reference.
	const auto rowText = [](const LevelEditorControls& controls, const QString& section, const QString& action) {
		for (const LevelEditorControlRow& row : levelEditorControlRows(controls)) {
			if (row.section == section && row.action == action) {
				return row.gesture;
			}
		}
		return QString();
	};
	expect(rowText(nrc, QStringLiteral("plan"), QStringLiteral("Camera drive keys")).contains(QStringLiteral("A left"))
			&& rowText(nrc, QStringLiteral("plan"), QStringLiteral("Camera drive keys")).contains(QStringLiteral("D right"))
			&& rowText(trenchBroomLevelControls(), QStringLiteral("plan"), QStringLiteral("Camera drive keys")).isEmpty(),
		"the 2D view lists the drive keys it passes on", rowText(nrc, QStringLiteral("plan"), QStringLiteral("Camera drive keys")));
	expect(rowText(nrc, QStringLiteral("camera"), QStringLiteral("Camera speed")).contains(QStringLiteral("500"))
			&& rowText(trenchBroomLevelControls(), QStringLiteral("camera"), QStringLiteral("Camera speed")).contains(QStringLiteral("map"))
			&& rowText(vibeStudioLevelControls(), QStringLiteral("camera"), QStringLiteral("Camera speed")).isEmpty(),
		"the camera lists its speed", rowText(nrc, QStringLiteral("camera"), QStringLiteral("Camera speed")));

	// Overlap warnings: a key a command takes from a routed drive key.
	EditorProfileDescriptor nrcProfile;
	editorProfileForId(QStringLiteral("netradiant-custom"), &nrcProfile);
	const QStringList userA = levelCameraShortcutWarnings(nrcProfile, nrcProfile.controls, {{QStringLiteral("map.selectAll"), {QStringLiteral("A")}}});
	// The camera's own claim on A, while it has focus, is reported as before.
	expect(userA.size() == 2 && userA.first().contains(QStringLiteral("2D view")) && userA.last().contains(QStringLiteral("camera has focus")),
		"a command the user gives A keeps it in the 2D views", userA.join(QLatin1Char(' ')));
	LevelEditorControls kDrive;
	expect(applyLevelGestureOverrides(nrcProfile.controls, {{"camera.driveKeys.pitchUp", "K"}}, &kDrive, nullptr, &error), "K looks up", error);
	const QStringList kWarnings = levelCameraShortcutWarnings(nrcProfile, kDrive);
	bool entityColour = false;
	for (const QString& warning : kWarnings) {
		entityColour = entityColour || (warning.contains(QStringLiteral("2D view")) && warning.contains(QStringLiteral("K")));
	}
	expect(entityColour, "a drive key on Set Entity Colour's K is reported", kWarnings.join(QLatin1Char(' ')));

	// The command line: editor gestures and editor controls.
	if (argc > 1) {
		const QString binary = QString::fromLocal8Bit(argv[1]);
		const auto cli = [&](const QStringList& arguments, int expected) {
			QProcess process;
			process.setWorkingDirectory(temp.path());
			process.start(binary, QStringList {QStringLiteral("--cli"), QStringLiteral("--settings-file"), settingsPath, QStringLiteral("editor")} + arguments
					+ QStringList {QStringLiteral("--json")});
			const bool finished = process.waitForFinished(30000);
			const QByteArray out = process.readAllStandardOutput();
			expect(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected, "CLI exit",
				arguments.join(QLatin1Char(' ')) + QLatin1Char(' ') + QString::fromUtf8(out + process.readAllStandardError()));
			return QJsonDocument::fromJson(out).object();
		};
		QJsonObject result = cli({QStringLiteral("gestures"), QStringLiteral("netradiant-custom"), QStringLiteral("--set"), QStringLiteral("camera.moveSpeed=750")}, 0);
		expect(result.value(QStringLiteral("values")).toObject().value(QStringLiteral("camera.moveSpeed")) == QStringLiteral("750")
				&& result.value(QStringLiteral("overrides")).toObject().value(QStringLiteral("camera.moveSpeed")) == QStringLiteral("750"),
			"editor gestures saves a speed");
		bool described = false;
		for (const QJsonValue& value : result.value(QStringLiteral("fields")).toArray()) {
			const QJsonObject field = value.toObject();
			if (field.value(QStringLiteral("id")) == QStringLiteral("camera.moveSpeed")) {
				described = field.value(QStringLiteral("type")) == QStringLiteral("choice") && field.value(QStringLiteral("choices")).toArray().size() == 42
					&& field.value(QStringLiteral("default")) == QStringLiteral("500") && field.value(QStringLiteral("available")).toBool();
			}
		}
		expect(described, "editor gestures describes the speed field");
		expect(StudioSettings().effectiveLevelEditorControls(QStringLiteral("netradiant-custom")).camera.moveSpeed == 750, "the saved speed is effective");
		cli({QStringLiteral("gestures"), QStringLiteral("netradiant-custom"), QStringLiteral("--set"), QStringLiteral("camera.moveSpeed=600")}, 4);
		expect(StudioSettings().effectiveLevelEditorControls(QStringLiteral("netradiant-custom")).camera.moveSpeed == 750, "a refused speed changes nothing");
		const QJsonObject controls = cli({QStringLiteral("controls"), QStringLiteral("netradiant-custom")}, 0);
		expect(controls.value(QStringLiteral("gestureOverrides")).toObject().value(QStringLiteral("camera.moveSpeed")) == QStringLiteral("750"),
			"editor controls reports the saved speed");
		result = cli({QStringLiteral("gestures"), QStringLiteral("netradiant-custom"), QStringLiteral("--set"), QStringLiteral("plan.cameraDriveKeys=off"),
						 QStringLiteral("--set"), QStringLiteral("camera.moveSpeed=default")}, 0);
		expect(result.value(QStringLiteral("overrides")).toObject() == QJsonObject {{"plan.cameraDriveKeys", "off"}}, "the speed returns to the profile's");
		cli({QStringLiteral("gestures"), QStringLiteral("netradiant-custom"), QStringLiteral("--reset")}, 0);
		result = cli({QStringLiteral("gestures"), QStringLiteral("trenchbroom"), QStringLiteral("--set"), QStringLiteral("camera.moveSpeed=1000"),
						 QStringLiteral("--dry-run")}, 0);
		expect(result.value(QStringLiteral("values")).toObject().value(QStringLiteral("camera.moveSpeed")) == QStringLiteral("1000")
				&& StudioSettings().editorGestureOverrides(QStringLiteral("trenchbroom")).isEmpty(),
			"a dry run previews without saving");
		cli({QStringLiteral("gestures"), QStringLiteral("vibestudio-default"), QStringLiteral("--set"), QStringLiteral("camera.moveSpeed=500")}, 4);
	}
	if (ok) {
		std::cout << "Level camera drive keys: speeds, routing across 24 profiles, gestures, rows, warnings and CLI passed." << std::endl;
	}
	return ok ? 0 : 1;
}
