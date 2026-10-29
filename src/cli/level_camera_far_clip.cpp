// editor far-clip: the Levels camera's saved far clip from the command line.
//
// Workflow reference: NRC camwindow.cpp Camera_ToggleFarClip, Camera_CubeIn,
// Camera_CubeOut and the CubicClipping and CubicScale preferences at 449778b3
// (GPL-2.0-or-later). Original Qt implementation; see docs/CREDITS.md.

#include "cli/level_camera_far_clip.h"

#include "core/level_camera_render.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QSet>

#include <cmath>

namespace vibestudio::cli {
namespace {
struct Text {
	Q_DECLARE_TR_FUNCTIONS(LevelCameraFarClipCli)
};
} // namespace

LevelCameraFarClipCliResult runLevelCameraFarClip(const QStringList& arguments)
{
	const auto fail = [](const QString& message) { return LevelCameraFarClipCliResult {2, message, {}, {}}; };
	const QSet<QString> flags {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose"),
		QStringLiteral("--dry-run"), QStringLiteral("--shorter"), QStringLiteral("--longer")};
	const QSet<QString> valued {QStringLiteral("--clip"), QStringLiteral("--step"), QStringLiteral("--distance"), QStringLiteral("--settings-file"),
		QStringLiteral("--locale"), QStringLiteral("--catalog-root")};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	// arguments[0] is the program, as for the other editor commands.
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const QString& argument = arguments.at(i);
		if (!argument.startsWith(QLatin1Char('-'))) {
			positional << argument;
			continue;
		}
		const qsizetype equal = argument.indexOf(QLatin1Char('='));
		const QString key = equal < 0 ? argument : argument.left(equal);
		if (seen.contains(key)) {
			return fail(Text::tr("%1 was given more than once.").arg(key));
		}
		seen.insert(key);
		if (flags.contains(key) && equal < 0) {
			continue;
		}
		if (!valued.contains(key)) {
			return fail(Text::tr("editor far-clip does not take %1.").arg(key));
		}
		QString value;
		if (equal >= 0) {
			value = argument.mid(equal + 1);
		} else if (i + 1 < arguments.size() && !arguments.at(i + 1).startsWith(QStringLiteral("--"))) {
			value = arguments.at(++i);
		}
		if (value.trimmed().isEmpty()) {
			return fail(Text::tr("%1 needs a value.").arg(key));
		}
		values.insert(key, value.trimmed());
	}
	const bool shorter = seen.contains(QStringLiteral("--shorter"));
	const bool longer = seen.contains(QStringLiteral("--longer"));
	const int distanceChoices = int(values.contains(QStringLiteral("--step"))) + int(values.contains(QStringLiteral("--distance"))) + int(shorter)
		+ int(longer);
	const bool dryRun = seen.contains(QStringLiteral("--dry-run"));
	const bool writing = values.contains(QStringLiteral("--clip")) || distanceChoices > 0;
	if (positional.size() != 2 || distanceChoices > 1 || (dryRun && !writing)) {
		return fail(Text::tr("Use editor far-clip to see the camera's far clip, --clip on|off to turn it on or off, and one of --step 1-23, --distance UNITS, --shorter or --longer to set its draw distance. --dry-run previews a change."));
	}

	const StudioSettings reader(StudioSettings::AccessMode::ReadOnly);
	LevelCameraFarClipState state;
	const QByteArray saved = reader.shellLayoutState(levelCameraFarClipSettingsKey());
	// Nothing saved, or text the studio could not read, is the default.
	if (!levelCameraFarClipFromText(saved, &state)) {
		state = {};
	}
	const LevelCameraFarClipState before = state;
	if (values.contains(QStringLiteral("--clip"))) {
		const QString choice = values.value(QStringLiteral("--clip"));
		if (choice != QLatin1String("on") && choice != QLatin1String("off")) {
			return fail(Text::tr("--clip takes on or off."));
		}
		state.enabled = choice == QLatin1String("on");
	}
	if (values.contains(QStringLiteral("--step"))) {
		bool numeric = false;
		const int step = values.value(QStringLiteral("--step")).toInt(&numeric);
		if (!numeric || step < kLevelCameraFarClipMinimumStep || step > kLevelCameraFarClipMaximumStep) {
			return fail(Text::tr("--step takes a whole number from %1 to %2.").arg(kLevelCameraFarClipMinimumStep).arg(kLevelCameraFarClipMaximumStep));
		}
		state = levelCameraWithFarClipStep(state, step);
	} else if (values.contains(QStringLiteral("--distance"))) {
		bool numeric = false;
		const double distance = values.value(QStringLiteral("--distance")).toDouble(&numeric);
		if (!numeric || !std::isfinite(distance) || distance <= 0.0) {
			return fail(Text::tr("--distance takes a number of units greater than 0."));
		}
		state = levelCameraWithFarClipStep(state, levelCameraFarClipStepForDistance(distance));
	} else if (shorter || longer) {
		state = levelCameraWithFarClipStep(state, state.step + (longer ? 1 : -1));
	}

	if (writing && !dryRun) {
		StudioSettings writer;
		if (writer.isReadOnly()) {
			return {1, Text::tr("The settings are read-only, so the far clip was not saved."), {}, {}};
		}
		writer.setShellLayoutState(levelCameraFarClipSettingsKey(), levelCameraFarClipText(state));
		writer.sync();
		if (writer.status() != QSettings::NoError) {
			return {1, Text::tr("Unable to save the far clip."), {}, {}};
		}
	}

	QJsonArray distances;
	for (int step = kLevelCameraFarClipMinimumStep; step <= kLevelCameraFarClipMaximumStep; ++step) {
		distances.append(levelCameraFarClipDistance(step));
	}
	const QJsonObject farClip {
		{QStringLiteral("enabled"), state.enabled},
		{QStringLiteral("step"), state.step},
		{QStringLiteral("distance"), levelCameraFarClipDistance(state.step)},
		{QStringLiteral("minimumStep"), kLevelCameraFarClipMinimumStep},
		{QStringLiteral("maximumStep"), kLevelCameraFarClipMaximumStep},
		{QStringLiteral("defaultStep"), kLevelCameraFarClipDefaultStep},
		{QStringLiteral("distances"), distances},
	};
	LevelCameraFarClipCliResult result;
	result.payload = {
		{QStringLiteral("farClip"), farClip},
		{QStringLiteral("settingsKey"), levelCameraFarClipSettingsKey()},
		{QStringLiteral("changed"), state != before},
		{QStringLiteral("applied"), writing && !dryRun},
		{QStringLiteral("dryRun"), dryRun},
	};
	result.lines << (state.enabled ? Text::tr("Far clip: on") : Text::tr("Far clip: off"));
	result.lines << Text::tr("Draw distance: %1 units (step %2 of %3)")
						.arg(levelCameraFarClipDistance(state.step))
						.arg(state.step)
						.arg(kLevelCameraFarClipMaximumStep);
	if (dryRun) {
		result.lines << Text::tr("Dry run: nothing was saved.");
	}
	return result;
}

} // namespace vibestudio::cli
