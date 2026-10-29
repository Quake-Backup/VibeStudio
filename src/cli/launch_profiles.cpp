#include "cli/launch_profiles.h"

#include "core/build_pipeline.h"
#include "core/game_installation.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QSet>

#include <algorithm>

namespace vibestudio::cli {
namespace {
struct Text {
	Q_DECLARE_TR_FUNCTIONS(LaunchProfilesCli)
};

QString engineLabel(const QString& engine)
{
	return engine.isEmpty() || engine == QLatin1String("unknown") ? Text::tr("any game") : engine;
}

} // namespace

LaunchProfilesCliResult runLaunchProfiles(const QStringList& arguments)
{
	LaunchProfilesCliResult result;
	const auto fail = [&](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QSet<QString> flags {"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--clear"};
	const QSet<QString> options {"--name", "--arguments", "--engine", "--installation", "--settings-file", "--locale", "--catalog-root"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const QString arg = arguments[i];
		if (!arg.startsWith(QStringLiteral("--"))) {
			positional << arg;
			continue;
		}
		const auto equal = arg.indexOf(QLatin1Char('='));
		const QString name = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(name)) {
			return fail(2, Text::tr("Repeated option: %1.").arg(name));
		}
		seen.insert(name);
		if (flags.contains(name) && equal < 0) {
			continue;
		}
		if (!options.contains(name)) {
			return fail(2, Text::tr("Unexpected option: %1.").arg(arg));
		}
		QString value;
		if (equal >= 0) {
			value = arg.mid(equal + 1);
		} else if (i + 1 < arguments.size()) {
			// Engine arguments may start with - or +; only another --option
			// is not a value.
			if (!arguments[i + 1].startsWith(QStringLiteral("--"))) {
				value = arguments[++i];
			}
		}
		if (value.trimmed().isEmpty()) {
			return fail(2, Text::tr("Missing value for %1.").arg(name));
		}
		values.insert(name, value);
	}
	const QString action = positional.value(1);
	const bool dryRun = seen.contains(QStringLiteral("--dry-run"));
	// The global options (--settings-file and the rest) go with any action.
	const QStringList ownOptions {QStringLiteral("--name"), QStringLiteral("--arguments"), QStringLiteral("--engine"), QStringLiteral("--installation")};
	bool anyOwnOption = false;
	for (const QString& option : ownOptions) {
		anyOwnOption = anyOwnOption || values.contains(option);
	}
	StudioSettings settings;
	QVector<GameLaunchProfile> mine = settings.userGameLaunchProfiles();

	if (action == QLatin1String("profiles")) {
		if (positional.size() != 2 || anyOwnOption || seen.contains(QStringLiteral("--dry-run")) || seen.contains(QStringLiteral("--clear"))) {
			return fail(2, Text::tr("Expected launch profiles with no other arguments."));
		}
		QJsonArray builtIn;
		QJsonArray own;
		result.lines << Text::tr("Built-in launch profiles");
		for (const GameLaunchProfile& profile : gameLaunchProfiles()) {
			builtIn.push_back(gameLaunchProfileJson(profile));
			result.lines << QStringLiteral("- %1: %2 (%3): %4").arg(profile.id, profile.displayName, engineLabel(profile.engineFamily),
				profile.argumentTemplates.isEmpty() ? Text::tr("no arguments") : profile.argumentTemplates.join(QLatin1Char(' ')));
		}
		result.lines << Text::tr("Your launch profiles");
		for (const GameLaunchProfile& profile : std::as_const(mine)) {
			own.push_back(gameLaunchProfileJson(profile));
			result.lines << QStringLiteral("- %1: %2 (%3): %4").arg(profile.id, profile.displayName, engineLabel(profile.engineFamily),
				profile.argumentLine.isEmpty() ? Text::tr("no arguments") : profile.argumentLine);
		}
		if (mine.isEmpty()) {
			result.lines << Text::tr("(none; add one with launch profile-set)");
		}
		result.lines << Text::tr("Tokens: %1").arg(QStringLiteral("{") + gameLaunchTokens().join(QStringLiteral("} {")) + QStringLiteral("}"));
		result.payload.insert(QStringLiteral("builtIn"), builtIn);
		result.payload.insert(QStringLiteral("user"), own);
		QJsonArray tokens;
		for (const QString& token : gameLaunchTokens()) {
			tokens.push_back(token);
		}
		result.payload.insert(QStringLiteral("tokens"), tokens);
		return result;
	}

	if (action == QLatin1String("profile-set")) {
		if (positional.size() != 3 || !values.contains(QStringLiteral("--name")) || !values.contains(QStringLiteral("--arguments"))
			|| values.contains(QStringLiteral("--installation")) || seen.contains(QStringLiteral("--clear"))) {
			return fail(2, Text::tr("Expected launch profile-set <id> --name NAME --arguments \"ARGUMENTS\", with --engine and --dry-run if wanted."));
		}
		GameLaunchProfile profile;
		profile.userDefined = true;
		profile.id = positional.value(2).trimmed();
		profile.displayName = values.value(QStringLiteral("--name")).trimmed();
		profile.argumentLine = values.value(QStringLiteral("--arguments")).trimmed();
		profile.requiresMap = profile.argumentLine.contains(QStringLiteral("{map}")) || profile.argumentLine.contains(QStringLiteral("{warp}"));
		const QString engine = values.value(QStringLiteral("--engine")).trimmed();
		if (!engine.isEmpty() && engine.compare(QStringLiteral("any"), Qt::CaseInsensitive) != 0) {
			const GameEngineFamily family = gameEngineFamilyFromId(engine);
			if (family == GameEngineFamily::Unknown) {
				return fail(2, Text::tr("--engine takes idTech1, idTech2, idTech3 or any."));
			}
			profile.engineFamily = family == GameEngineFamily::IdTech1 ? QStringLiteral("idTech1")
				: (family == GameEngineFamily::IdTech2 ? QStringLiteral("idTech2") : QStringLiteral("idTech3"));
		}
		if (const QString problem = userGameLaunchProfileProblem(profile); !problem.isEmpty()) {
			return fail(4, problem);
		}
		bool replaced = false;
		for (GameLaunchProfile& existing : mine) {
			if (existing.id == profile.id) {
				existing = profile;
				replaced = true;
			}
		}
		for (const GameLaunchProfile& existing : std::as_const(mine)) {
			if (existing.id != profile.id && existing.displayName.compare(profile.displayName, Qt::CaseInsensitive) == 0) {
				return fail(4, Text::tr("Another launch profile is called %1; give each a name of its own.").arg(profile.displayName));
			}
		}
		if (!replaced) {
			mine.push_back(profile);
		}
		if (!dryRun) {
			settings.setUserGameLaunchProfiles(mine);
			settings.sync();
		}
		result.payload.insert(QStringLiteral("profile"), gameLaunchProfileJson(profile));
		result.payload.insert(QStringLiteral("replaced"), replaced);
		result.payload.insert(QStringLiteral("dryRun"), dryRun);
		result.payload.insert(QStringLiteral("saved"), !dryRun);
		result.lines << (replaced ? Text::tr("Launch profile %1 replaced: %2").arg(profile.id, profile.argumentLine)
								  : Text::tr("Launch profile %1 added: %2").arg(profile.id, profile.argumentLine));
		if (dryRun) {
			result.lines << Text::tr("Dry run: nothing was saved.");
		}
		return result;
	}

	if (action == QLatin1String("profile-remove")) {
		if (positional.size() != 3 || anyOwnOption || seen.contains(QStringLiteral("--clear"))) {
			return fail(2, Text::tr("Expected launch profile-remove <id>, with --dry-run if wanted."));
		}
		const QString id = positional.value(2).trimmed();
		const qsizetype before = mine.size();
		mine.erase(std::remove_if(mine.begin(), mine.end(), [&id](const GameLaunchProfile& profile) { return profile.id == id; }), mine.end());
		if (mine.size() == before) {
			return fail(3, gameLaunchProfileIds().contains(id) ? Text::tr("%1 is a built-in launch profile and cannot be removed.").arg(id)
															   : Text::tr("You have no launch profile %1.").arg(id));
		}
		if (!dryRun) {
			settings.setUserGameLaunchProfiles(mine);
			settings.sync();
		}
		result.payload.insert(QStringLiteral("removed"), id);
		result.payload.insert(QStringLiteral("dryRun"), dryRun);
		result.lines << Text::tr("Launch profile %1 removed.").arg(id);
		if (dryRun) {
			result.lines << Text::tr("Dry run: nothing was saved.");
		}
		return result;
	}

	if (action == QLatin1String("extra-args")) {
		if (positional.size() != 2 || values.contains(QStringLiteral("--name")) || values.contains(QStringLiteral("--engine"))
			|| (values.contains(QStringLiteral("--arguments")) && seen.contains(QStringLiteral("--clear")))) {
			return fail(2, Text::tr("Expected launch extra-args, with --installation ID and --arguments \"ARGUMENTS\" or --clear to change them."));
		}
		const QVector<GameInstallationProfile> installations = settings.gameInstallations();
		if (installations.isEmpty()) {
			return fail(3, Text::tr("No game installation profiles are saved. Add one with install add first."));
		}
		const QString requested = values.contains(QStringLiteral("--installation")) ? values.value(QStringLiteral("--installation")) : settings.selectedGameInstallationId();
		GameInstallationProfile installation = installations.first();
		bool found = requested.trimmed().isEmpty();
		for (const GameInstallationProfile& profile : installations) {
			if (sameGameInstallationId(profile.id, requested)) {
				installation = profile;
				found = true;
				break;
			}
		}
		if (!found) {
			return fail(3, Text::tr("Installation profile not found: %1").arg(requested));
		}
		const bool change = values.contains(QStringLiteral("--arguments")) || seen.contains(QStringLiteral("--clear"));
		if (!change && dryRun) {
			return fail(2, Text::tr("--dry-run goes with --arguments or --clear."));
		}
		const QString arguments = seen.contains(QStringLiteral("--clear")) ? QString() : (change ? values.value(QStringLiteral("--arguments")).trimmed() : settings.launchExtraArguments(installation.id));
		if (arguments.count(QLatin1Char('"')) % 2 != 0) {
			return fail(4, Text::tr("The arguments have an unclosed quote."));
		}
		if (change && !dryRun) {
			settings.setLaunchExtraArguments(installation.id, arguments);
			settings.sync();
		}
		QJsonArray split;
		for (const QString& word : splitGameLaunchArguments(arguments)) {
			split.push_back(word);
		}
		result.payload.insert(QStringLiteral("installationId"), installation.id);
		result.payload.insert(QStringLiteral("extraArguments"), arguments);
		result.payload.insert(QStringLiteral("arguments"), split);
		result.payload.insert(QStringLiteral("changed"), change);
		result.payload.insert(QStringLiteral("dryRun"), dryRun);
		result.lines << Text::tr("Installation: %1").arg(installation.id);
		result.lines << Text::tr("Extra arguments: %1").arg(arguments.isEmpty() ? Text::tr("none") : arguments);
		if (change && dryRun) {
			result.lines << Text::tr("Dry run: nothing was saved.");
		}
		return result;
	}
	return fail(2, Text::tr("Unknown launch command: %1.").arg(action));
}

} // namespace vibestudio::cli
