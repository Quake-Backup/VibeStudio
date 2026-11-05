// The Levels camera's far clip (core/level_camera_render.h): NetRadiant
// Custom's 23 cubic-clip distances and their clamps, the nearest step for a
// distance, the far plane only while the clip is on, the reversed depth
// mapping both GPU backends clip with, the fog ending at the clip while both
// are on, the saved text, the translated announcements and palette entries,
// the Radiant profiles' keys, cursor placement that takes nothing past the
// clip, and editor far-clip on the command line (run with the vibestudio
// executable as the argument).

#include "core/editor_profiles.h"
#include "core/level_camera_render.h"
#include "core/level_editor_controls.h"
#include "core/level_map.h"
#include "core/level_paste.h"
#include "core/studio_semantics.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <tuple>
#include <utility>

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

QString describe(const LevelCameraShadingState& state)
{
	return QStringLiteral("fog=%1 range=%2..%3").arg(int(state.fog)).arg(state.fogStart).arg(state.fogEnd);
}

void distances()
{
	const LevelCameraFarClipState defaults;
	expect(!defaults.enabled && defaults.step == 14 && levelCameraFarClipDistance(defaults.step) == 1448, "off at NetRadiant Custom's step 14, 1448 units",
		QString::number(levelCameraFarClipDistance(defaults.step)));
	const std::pair<int, int> known[] = {{1, 16}, {2, 23}, {9, 256}, {13, 1024}, {14, 1448}, {15, 2048}, {19, 8192}, {22, 23170}, {23, 32768}};
	for (const auto& [step, units] : known) {
		expect(levelCameraFarClipDistance(step) == units, "the distance of a step", QStringLiteral("%1 -> %2").arg(step).arg(levelCameraFarClipDistance(step)));
	}
	for (int step = kLevelCameraFarClipMinimumStep + 1; step <= kLevelCameraFarClipMaximumStep; ++step) {
		const double ratio = double(levelCameraFarClipDistance(step)) / levelCameraFarClipDistance(step - 1);
		expect(ratio > 1.38 && ratio < 1.45, "each step is about the square root of two farther", QString::number(ratio));
	}
	expect(levelCameraFarClipDistance(-5) == 16 && levelCameraFarClipDistance(99) == 32768, "out-of-range steps clamp");
	LevelCameraFarClipState state;
	expect(levelCameraFarClipPlane(state) == 0.0, "no far plane while the clip is off");
	state.enabled = true;
	expect(levelCameraFarClipPlane(state) == 1448.0, "the step's distance while it is on");
	state = levelCameraWithFarClipStep(state, 0);
	expect(state.enabled && state.step == 1, "the shortest step holds", QString::number(state.step));
	state = levelCameraWithFarClipStep(state, 40);
	expect(state.enabled && state.step == 23, "the longest step holds", QString::number(state.step));
	state.enabled = false;
	state = levelCameraWithFarClipStep(state, 5);
	expect(!state.enabled && state.step == 5, "stepping keeps the on/off choice");
	const std::pair<double, int> nearest[] = {{2048, 15}, {1500, 14}, {1800, 15}, {0.5, 1}, {16, 1}, {1.0e9, 23}, {40000, 23}, {100, 6}};
	for (const auto& [units, step] : nearest) {
		expect(levelCameraFarClipStepForDistance(units) == step, "the nearest step for a distance",
			QStringLiteral("%1 -> %2").arg(units).arg(levelCameraFarClipStepForDistance(units)));
	}
	// 1024 and 1448 are 424 apart; 1236 is 212 from 1024 and 212 from 1448.
	expect(levelCameraFarClipStepForDistance(1236) == 13, "a tie takes the shorter step", QString::number(levelCameraFarClipStepForDistance(1236)));
}

void depth()
{
	const LevelCameraDepthMapping infinite = levelCameraPerspectiveDepth(1.0, 0.0);
	expect(infinite.scale == 0.0 && infinite.offset == 1.0, "no far plane keeps the infinite mapping");
	const LevelCameraDepthMapping bad = levelCameraPerspectiveDepth(1.0, 0.5);
	expect(bad.scale == 0.0 && bad.offset == 1.0, "a far plane before the near plane is ignored");
	const LevelCameraDepthMapping clip = levelCameraPerspectiveDepth(1.0, 1000.0);
	const auto ratio = [&](double distance) { return (clip.scale * distance + clip.offset) / distance; };
	expect(std::abs(ratio(1.0) - 1.0) < 1.0e-12, "z / w is 1 at the near plane", QString::number(ratio(1.0)));
	expect(std::abs(ratio(1000.0)) < 1.0e-12, "and 0 at the far plane", QString::number(ratio(1000.0)));
	expect(ratio(1001.0) < 0.0 && ratio(5000.0) < 0.0, "and negative beyond it, where the backends clip");
	expect(ratio(10.0) > ratio(100.0) && ratio(100.0) > ratio(999.0) && ratio(999.0) > 0.0, "nearer is greater, as the depth test wants");
}

void fog()
{
	LevelCameraShadingState shading;
	LevelCameraFarClipState clip;
	clip.enabled = true;
	expect(levelCameraFogWithinFarClip(shading, clip) == shading, "no fog, nothing to change");
	shading.fog = true;
	LevelCameraShadingState drawn = levelCameraFogWithinFarClip(shading, clip);
	expect(drawn.fog && drawn.fogStart == 512 && drawn.fogEnd == 1448, "the fog ends at the clip distance", describe(drawn));
	clip.enabled = false;
	expect(levelCameraFogWithinFarClip(shading, clip) == shading, "with the clip off the saved range is drawn");
	clip.enabled = true;
	shading = levelCameraWithFogRange(shading, 2000, 3000);
	drawn = levelCameraFogWithinFarClip(shading, clip);
	expect(drawn.fogStart == 1448 - kLevelCameraFogMinimumGap && drawn.fogEnd == 1448, "a start beyond the clip comes in, keeping the gap", describe(drawn));
	shading = levelCameraWithFogRange(shading, 100, 1000);
	expect(levelCameraFogWithinFarClip(shading, clip) == shading, "a fog that ends before the clip is untouched");
	clip = levelCameraWithFarClipStep(clip, 1);
	drawn = levelCameraFogWithinFarClip(shading, clip);
	expect(drawn.fogStart == 0 && drawn.fogEnd == 16, "the shortest clip still fits the fog's gap", describe(drawn));
	expect(levelCameraFogAmount(drawn, 16.0) > 0.79, "faces at the clip take the fullest fog", QString::number(levelCameraFogAmount(drawn, 16.0)));
}

void text()
{
	expect(levelCameraFarClipSettingsKey() == QStringLiteral("levelPreviewFarClip"), "the settings key");
	LevelCameraFarClipState state;
	state.enabled = true;
	state.step = 9;
	expect(levelCameraFarClipText(state) == QByteArrayLiteral("on,9"), "saved text", QString::fromLatin1(levelCameraFarClipText(state)));
	LevelCameraFarClipState read;
	expect(levelCameraFarClipFromText(levelCameraFarClipText(state), &read) && read == state, "it reads back");
	expect(levelCameraFarClipFromText(QByteArrayLiteral(" off , 99 "), &read) && !read.enabled && read.step == 23, "reading clamps the step");
	for (const QByteArray& bad : {QByteArrayLiteral(""), QByteArrayLiteral("on"), QByteArrayLiteral("yes,3"), QByteArrayLiteral("on,x"),
			 QByteArrayLiteral("on,3,4")}) {
		LevelCameraFarClipState untouched;
		untouched.step = 7;
		expect(!levelCameraFarClipFromText(bad, &untouched) && untouched.step == 7, "unreadable text is refused", QString::fromLatin1(bad));
	}

	LevelCameraShadingState shading;
	LevelCameraFarClipState off;
	expect(!levelCameraFarClipSummary(off, shading).isEmpty() && !levelCameraFarClipSummary(off, shading).contains(QStringLiteral("1448")),
		"the off summary", levelCameraFarClipSummary(off, shading));
	LevelCameraFarClipState on;
	on.enabled = true;
	const QString summary = levelCameraFarClipSummary(on, shading);
	expect(summary.contains(levelCameraFarClipDistanceText(14)) && summary.contains(QStringLiteral("14")) && summary.contains(QStringLiteral("23")),
		"the on summary names the distance and step", summary);
	shading.fog = true;
	expect(levelCameraFarClipSummary(on, shading).size() > summary.size(), "and says when it ends the fog", levelCameraFarClipSummary(on, shading));
	expect(levelCameraFarClipDistanceText(23).contains(QStringLiteral("32768")), "distances are written without group separators",
		levelCameraFarClipDistanceText(23));
	const LevelCameraFarClipState shortest = levelCameraWithFarClipStep(on, 1);
	const LevelCameraFarClipState longest = levelCameraWithFarClipStep(on, 23);
	const QString atShortest = levelCameraFarClipStepAnnouncement(shortest, shortest);
	const QString atLongest = levelCameraFarClipStepAnnouncement(longest, longest);
	const QString stepped = levelCameraFarClipStepAnnouncement(on, levelCameraWithFarClipStep(on, 15));
	const QString steppedOff = levelCameraFarClipStepAnnouncement(off, levelCameraWithFarClipStep(off, 15));
	expect(!atShortest.isEmpty() && atShortest != atLongest && atShortest.contains(QStringLiteral("16")) && atLongest.contains(QStringLiteral("32768")),
		"the ends of the range are announced", atShortest + QStringLiteral(" / ") + atLongest);
	expect(stepped.contains(QStringLiteral("2048")) && steppedOff.contains(QStringLiteral("2048")) && steppedOff != stepped,
		"a step is announced, noting when the clip is off", stepped + QStringLiteral(" / ") + steppedOff);
}

void commands()
{
	const std::pair<const char*, const char*> entries[] = {{"map.preview-far-clip", "Far Clip in Camera"},
		{"map.shorter-draw-distance", "Shorter Draw Distance"}, {"map.longer-draw-distance", "Longer Draw Distance"}};
	for (const auto& [id, label] : entries) {
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(QString::fromLatin1(id), &entry) && entry.label == QString::fromLatin1(label)
				&& entry.summary.contains(QStringLiteral("cubic clip")),
			"a palette entry that cubic clip finds", QString::fromLatin1(id));
	}
	// NetRadiant Custom's keys; NetRadiant binds the same ones. TrenchBroom
	// has no far clip, so its profile leaves the commands without keys.
	const std::pair<const char*, const char*> keys[] = {
		{"map.previewFarClip", "Ctrl+\\"}, {"map.shorterDrawDistance", "Ctrl+["}, {"map.longerDrawDistance", "Ctrl+]"}};
	for (const char* profile : {"netradiant-custom", "netradiant"}) {
		const LevelEditorControls controls = levelEditorControlsForProfile(QString::fromLatin1(profile));
		for (const auto& [id, key] : keys) {
			QStringList bound;
			for (const LevelEditorKeyBinding& binding : controls.keys) {
				if (binding.commandId == QLatin1String(id)) {
					bound = binding.keys;
				}
			}
			expect(bound == QStringList {QString::fromLatin1(key)}, "the profile's key", QStringLiteral("%1 %2: %3").arg(QLatin1String(profile), QLatin1String(id), bound.join(QLatin1Char(' '))));
		}
	}
	// The older Radiants bind them too: GtkRadiant 1.4 to 1.6 the same way
	// round as NetRadiant Custom, Q3Radiant and DoomEdit with the steps the
	// other way round (Ctrl+[ longer), and QeRadiant only the steps, as its
	// manual gives Ctrl+\ to Q3Radiant alone.
	const auto profileKeys = [](const char* profile, const char* command) {
		for (const LevelEditorKeyBinding& binding : levelEditorControlsForProfile(QString::fromLatin1(profile)).keys) {
			if (binding.commandId == QLatin1String(command)) {
				return binding.keys;
			}
		}
		return QStringList();
	};
	for (const char* profile : {"gtkradiant-1-6", "gtkradiant-1-5", "gtkradiant-1-4"}) {
		for (const auto& [id, key] : keys) {
			expect(profileKeys(profile, id) == QStringList {QString::fromLatin1(key)}, "GtkRadiant's far clip key",
				QStringLiteral("%1 %2").arg(QLatin1String(profile), QLatin1String(id)));
		}
	}
	for (const char* profile : {"q3radiant", "doomedit", "qeradiant"}) {
		const bool toggles = QLatin1String(profile) != QLatin1String("qeradiant");
		expect(profileKeys(profile, "map.shorterDrawDistance") == QStringList {QStringLiteral("Ctrl+]")}
				&& profileKeys(profile, "map.longerDrawDistance") == QStringList {QStringLiteral("Ctrl+[")}
				&& profileKeys(profile, "map.previewFarClip") == (toggles ? QStringList {QStringLiteral("Ctrl+\\")} : QStringList()),
			"Q3Radiant's far clip keys", QString::fromLatin1(profile));
	}
	const LevelEditorControls trenchBroom = levelEditorControlsForProfile(QStringLiteral("trenchbroom"));
	for (const LevelEditorKeyBinding& binding : trenchBroom.keys) {
		for (const auto& [id, key] : keys) {
			Q_UNUSED(key);
			expect(binding.commandId != QLatin1String(id) || binding.keys.isEmpty(), "TrenchBroom binds no far clip key", QString::fromLatin1(id));
		}
	}
}

QString box(std::array<double, 3> lo, std::array<double, 3> hi)
{
	QStringList lines {QStringLiteral("{")};
	const auto point = [](const std::array<double, 3>& v) {
		return QStringLiteral("( %1 %2 %3 )").arg(v[0], 0, 'g', 10).arg(v[1], 0, 'g', 10).arg(v[2], 0, 'g', 10);
	};
	for (int axis = 0; axis < 3; ++axis) {
		for (const bool positive : {false, true}) {
			std::array<double, 3> p = lo;
			p[axis] = positive ? hi[axis] : lo[axis];
			auto a = p;
			auto b = p;
			int first = (axis + 1) % 3;
			int second = (axis + 2) % 3;
			if (positive) {
				std::swap(first, second);
			}
			a[first] += 1;
			b[second] += 1;
			lines << QStringLiteral("%1 %2 %3 base/wall 0 0 0 0.5 0.5 0 0 0").arg(point(p), point(a), point(b));
		}
	}
	lines << QStringLiteral("}");
	return lines.join(QLatin1Char('\n'));
}

// Placing or pasting at the camera pointer takes no brush past the far clip,
// as the camera's own picks do not, and a miss stays this side of it.
void placement()
{
	LevelMapDocument map;
	LevelMapDocument clipboard;
	QString error;
	const QByteArray far = QStringLiteral("{\n\"classname\" \"worldspawn\"\n%1\n}\n").arg(box({2000, -64, -64}, {2064, 64, 64})).toUtf8();
	const QByteArray near = QStringLiteral("{\n\"classname\" \"worldspawn\"\n%1\n%2\n}\n")
								.arg(box({512, -64, -64}, {576, 64, 64}), box({2000, -64, -64}, {2064, 64, 64}))
								.toUtf8();
	const QByteArray entity = QByteArrayLiteral("{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"info_null\"\n\"origin\" \"0 0 0\"\n}\n");
	if (!expect(loadLevelMapBytes({QStringLiteral("far.map"), {}, QStringLiteral("idtech3")}, far, &map, &error)
				&& loadLevelMapBytes({QStringLiteral("clip.map"), {}, QStringLiteral("idtech3")}, entity, &clipboard, &error),
			"placement fixtures", error)) {
		return;
	}
	int entityId = -1;
	for (const LevelMapEntity& candidate : clipboard.entities) {
		if (candidate.className == QLatin1String("info_null")) {
			entityId = candidate.id;
		}
	}
	LevelPastePlacement ray;
	ray.mode = LevelPasteMode::ViewRay;
	ray.rayOrigin = {0, 0, 0, true};
	ray.direction = {1, 0, 0, true};
	ray.viewDirection = {1, 0, 0, true};
	// The pasted box's far side along the ray: x + 8 for the 16-unit cube.
	const auto reach = [&](const LevelMapDocument& destination, const LevelPastePlacement& placement) {
		LevelMapVec3 offset;
		QString failure;
		if (!levelPasteOffset(destination, clipboard, {{LevelMapSelectionKind::Entity, entityId}}, placement, &offset, &failure)) {
			expect(false, "cursor placement resolves", failure);
			return std::numeric_limits<double>::quiet_NaN();
		}
		return offset.x + 8.0;
	};
	expect(std::abs(reach(map, ray) - 2000.0) < 1.0e-6, "without a far clip the far brush takes the placement");
	LevelPastePlacement clipped = ray;
	clipped.farClipPoint = {static_cast<double>(levelCameraFarClipDistance(14)), 0, 0, true};
	clipped.farClipNormal = {1, 0, 0, true};
	expect(std::abs(reach(map, clipped) - 256.0) < 1.0e-6, "past the far clip the brush is missed and the miss plane is used");
	clipped.farClipPoint = {100, 0, 0, true};
	expect(std::abs(reach(map, clipped) - 100.0) < 1.0e-6, "a far clip nearer than the miss plane pulls it in");
	LevelMapDocument nearMap;
	expect(loadLevelMapBytes({QStringLiteral("near.map"), {}, QStringLiteral("idtech3")}, near, &nearMap, &error), "near fixture", error);
	clipped.farClipPoint = {static_cast<double>(levelCameraFarClipDistance(14)), 0, 0, true};
	expect(std::abs(reach(nearMap, clipped) - 512.0) < 1.0e-6, "a brush this side of the far clip still takes the placement");
}

void cli(const QString& executable, const QTemporaryDir& temp)
{
	const QString settingsPath = temp.filePath(QStringLiteral("far-clip-settings.ini"));
	const auto run = [&](const QStringList& args, int code = 0, bool json = true) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		QStringList command {QStringLiteral("--cli")};
		if (json) {
			command << QStringLiteral("--json");
		}
		command << QStringLiteral("--settings-file") << settingsPath << QStringLiteral("editor") << QStringLiteral("far-clip");
		process.start(executable, command + args);
		const bool done = process.waitForStarted() && process.waitForFinished(30000);
		const QByteArray out = process.readAllStandardOutput();
		const QByteArray err = process.readAllStandardError();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code, "CLI exit status",
			args.join(QLatin1Char(' ')) + QLatin1String(" -> ") + QString::fromUtf8(out + err));
		if (!done) {
			process.kill();
			process.waitForFinished();
		}
		return std::make_pair(QJsonDocument::fromJson(out).object(), QString::fromUtf8(out + err));
	};
	const auto saved = [&] { return StudioSettings(settingsPath, StudioSettings::AccessMode::ReadOnly).shellLayoutState(levelCameraFarClipSettingsKey()); };
	const auto clip = [](const QJsonObject& report) { return report.value(QStringLiteral("farClip")).toObject(); };

	auto [report, output] = run({});
	expect(clip(report).value(QStringLiteral("enabled")) == false && clip(report).value(QStringLiteral("step")) == 14
			&& clip(report).value(QStringLiteral("distance")) == 1448 && clip(report).value(QStringLiteral("distances")).toArray().size() == 23
			&& report.value(QStringLiteral("applied")) == false && report.value(QStringLiteral("settingsKey")) == levelCameraFarClipSettingsKey(),
		"a fresh store reports the default", output);
	std::tie(report, output) = run({QStringLiteral("--clip"), QStringLiteral("on"), QStringLiteral("--distance"), QStringLiteral("2048")});
	expect(clip(report).value(QStringLiteral("enabled")) == true && clip(report).value(QStringLiteral("step")) == 15
			&& report.value(QStringLiteral("applied")) == true && report.value(QStringLiteral("changed")) == true && saved() == QByteArrayLiteral("on,15"),
		"--clip on --distance 2048 saves step 15", output + QStringLiteral(" saved ") + QString::fromLatin1(saved()));
	std::tie(report, output) = run({QStringLiteral("--shorter"), QStringLiteral("--dry-run")});
	expect(clip(report).value(QStringLiteral("step")) == 14 && report.value(QStringLiteral("dryRun")) == true
			&& report.value(QStringLiteral("applied")) == false && saved() == QByteArrayLiteral("on,15"),
		"a dry run previews a step without saving it", output);
	std::tie(report, output) = run({QStringLiteral("--longer")});
	expect(clip(report).value(QStringLiteral("step")) == 16 && saved() == QByteArrayLiteral("on,16"), "--longer saves the next step", output);
	std::tie(report, output) = run({QStringLiteral("--step=23")});
	std::tie(report, output) = run({QStringLiteral("--longer")});
	expect(clip(report).value(QStringLiteral("step")) == 23 && report.value(QStringLiteral("changed")) == false, "the longest step holds", output);
	std::tie(report, output) = run({QStringLiteral("--clip=off"), QStringLiteral("--step"), QStringLiteral("3")}, 0, false);
	expect(output.contains(QStringLiteral("Far clip: off")) && output.contains(QStringLiteral("32 units")) && saved() == QByteArrayLiteral("off,3"),
		"text output names the state and distance", output);
	for (const QStringList& bad : QVector<QStringList> {{QStringLiteral("--step"), QStringLiteral("0")}, {QStringLiteral("--step"), QStringLiteral("24")},
			 {QStringLiteral("--step"), QStringLiteral("x")}, {QStringLiteral("--distance"), QStringLiteral("-5")},
			 {QStringLiteral("--clip"), QStringLiteral("maybe")}, {QStringLiteral("--step"), QStringLiteral("5"), QStringLiteral("--shorter")},
			 {QStringLiteral("--shorter"), QStringLiteral("--longer")}, {QStringLiteral("--dry-run")}, {QStringLiteral("--clip")},
			 {QStringLiteral("--bogus")}, {QStringLiteral("extra")}, {QStringLiteral("--clip"), QStringLiteral("on"), QStringLiteral("--clip"), QStringLiteral("off")}}) {
		run(bad, 2);
	}
	expect(saved() == QByteArrayLiteral("off,3"), "refused calls save nothing", QString::fromLatin1(saved()));
	// What the studio saved but the CLI cannot read reads as the default.
	{
		StudioSettings writer(settingsPath);
		writer.setShellLayoutState(levelCameraFarClipSettingsKey(), QByteArrayLiteral("garbage"));
		writer.sync();
	}
	std::tie(report, output) = run({});
	expect(clip(report).value(QStringLiteral("enabled")) == false && clip(report).value(QStringLiteral("step")) == 14, "unreadable text reads as the default",
		output);
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	distances();
	depth();
	fog();
	text();
	commands();
	placement();
	if (argc == 2) {
		cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath(), temp);
	}
	if (ok) {
		std::cout << "Level camera far clip: distances, depth mapping, fog, saved text, announcements, palette, keys, placement past the clip" << (argc == 2 ? " and CLI" : "")
				  << " passed." << std::endl;
	}
	return ok ? 0 : 1;
}
