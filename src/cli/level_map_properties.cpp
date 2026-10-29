#include "cli/level_map_properties.h"

#include "core/game_installation.h"
#include "core/level_map.h"
#include "core/level_map_inspector.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QSet>

namespace vibestudio::cli {
namespace {
struct Text {
	Q_DECLARE_TR_FUNCTIONS(LevelMapPropertiesCli)
};
} // namespace

LevelMapPropertiesCliResult runLevelMapProperties(const QStringList& arguments)
{
	LevelMapPropertiesCliResult result;
	const auto fail = [&](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QSet<QString> flags {"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite", "--no-mods"};
	const QSet<QString> options {"--soft-bounds", "--mods", "--enable-mod", "--disable-mod", "--game-root", "--game", "--output", "--engine", "--map-name",
		"--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> repeatable {"--enable-mod", "--disable-mod"};
	QSet<QString> seen;
	QHash<QString, QStringList> values;
	QStringList positional;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const QString arg = arguments[i];
		if (!arg.startsWith(QStringLiteral("--"))) {
			positional << arg;
			continue;
		}
		const auto equal = arg.indexOf(QLatin1Char('='));
		const QString name = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(name) && !repeatable.contains(name)) {
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
		} else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith(QStringLiteral("--"))) {
			value = arguments[++i];
		}
		if (value.trimmed().isEmpty()) {
			return fail(2, Text::tr("Missing value for %1.").arg(name));
		}
		values[name] << value;
	}
	if (positional.size() != 3) {
		return fail(2, Text::tr("Expected map properties <map>, with --soft-bounds, --mods, --no-mods, --enable-mod or --disable-mod and --output to change them."));
	}
	const bool changeBounds = seen.contains(QStringLiteral("--soft-bounds"));
	const bool changeMods = seen.contains(QStringLiteral("--mods")) || seen.contains(QStringLiteral("--no-mods")) || seen.contains(QStringLiteral("--enable-mod"))
		|| seen.contains(QStringLiteral("--disable-mod"));
	const bool change = changeBounds || changeMods;
	if (seen.contains(QStringLiteral("--mods")) && seen.contains(QStringLiteral("--no-mods"))) {
		return fail(2, Text::tr("Use --mods or --no-mods, not both."));
	}
	if (change && values.value(QStringLiteral("--output")).isEmpty()) {
		return fail(2, Text::tr("Changing map properties requires --output; --dry-run checks without writing."));
	}
	if (!change && (seen.contains(QStringLiteral("--output")) || seen.contains(QStringLiteral("--dry-run")) || seen.contains(QStringLiteral("--overwrite")))) {
		return fail(2, Text::tr("--output, --dry-run and --overwrite go with a change: --soft-bounds or a mods option."));
	}
	if (seen.contains(QStringLiteral("--game")) && !seen.contains(QStringLiteral("--game-root"))) {
		return fail(2, Text::tr("--game names the game whose folder --game-root leaves out of the mods it lists."));
	}
	LevelMapSoftBoundsChange bounds;
	if (changeBounds) {
		const QString value = values.value(QStringLiteral("--soft-bounds")).first();
		if (!levelMapSoftBoundsModeFromId(value, &bounds.mode) || bounds.mode == LevelMapSoftBoundsMode::Custom) {
			bounds.mode = LevelMapSoftBoundsMode::Custom;
			if (!parseLevelMapSoftBounds(value, &bounds.mins, &bounds.maxs)) {
				return fail(2, Text::tr("--soft-bounds takes game, none, or six numbers \"minX minY minZ maxX maxY maxZ\" with each minimum below its maximum."));
			}
		}
	}

	const QString source = QFileInfo(positional[2]).absoluteFilePath();
	if (!QFileInfo(source).isFile()) {
		return fail(3, Text::tr("The map does not exist: %1").arg(source));
	}
	QString error;
	LevelMapDocument document;
	if (!loadLevelMap({source, values.value(QStringLiteral("--map-name")).value(0), values.value(QStringLiteral("--engine")).value(0)}, &document, &error)) {
		return fail(4, error);
	}
	QStringList changed;
	if (changeBounds) {
		const auto revision = document.revision;
		if (!setLevelMapSoftBounds(&document, bounds, &error)) {
			return fail(4, error);
		}
		if (document.revision != revision) {
			changed << QStringLiteral("softBounds");
		}
	}
	if (changeMods) {
		QStringList mods = levelMapEnabledMods(document);
		if (seen.contains(QStringLiteral("--no-mods"))) {
			mods.clear();
		} else if (seen.contains(QStringLiteral("--mods"))) {
			mods = values.value(QStringLiteral("--mods")).first().split(QLatin1Char(';'), Qt::SkipEmptyParts);
			for (QString& mod : mods) {
				mod = mod.trimmed();
			}
		}
		for (const QString& mod : values.value(QStringLiteral("--enable-mod"))) {
			if (!mods.contains(mod.trimmed(), Qt::CaseInsensitive)) {
				mods << mod.trimmed();
			}
		}
		for (const QString& mod : values.value(QStringLiteral("--disable-mod"))) {
			qsizetype index = -1;
			for (qsizetype at = 0; at < mods.size() && index < 0; ++at) {
				index = mods.at(at).compare(mod.trimmed(), Qt::CaseInsensitive) == 0 ? at : -1;
			}
			if (index < 0) {
				return fail(4, Text::tr("%1 is not an enabled mod of this map.").arg(mod.trimmed()));
			}
			mods.removeAt(index);
		}
		const auto revision = document.revision;
		if (!setLevelMapEnabledMods(&document, mods, &error)) {
			return fail(4, error);
		}
		if (document.revision != revision) {
			changed << QStringLiteral("mods");
		}
	}

	const QJsonObject properties = levelMapInspectorJson(document);
	for (auto it = properties.constBegin(); it != properties.constEnd(); ++it) {
		result.payload.insert(it.key(), it.value());
	}
	result.payload.insert(QStringLiteral("map"), source);
	result.lines = levelMapInspectorLines(document);
	if (seen.contains(QStringLiteral("--game-root"))) {
		QStringList excluded = levelMapBaseGameFolders();
		if (seen.contains(QStringLiteral("--game"))) {
			const QString key = values.value(QStringLiteral("--game")).first();
			if (gameDefinitionForKey(key).gameKey != normalizedGameKey(key)) {
				return fail(2, Text::tr("Unknown game: %1.").arg(key));
			}
			excluded = QStringList {gameDefinitionForKey(key).baseGameDirectory};
		}
		const QString root = QFileInfo(values.value(QStringLiteral("--game-root")).first()).absoluteFilePath();
		if (!QFileInfo(root).isDir()) {
			return fail(3, Text::tr("The game folder does not exist: %1").arg(root));
		}
		const QStringList available = levelMapAvailableMods(root, excluded);
		result.payload.insert(QStringLiteral("gameRoot"), root);
		result.payload.insert(QStringLiteral("availableMods"), QJsonArray::fromStringList(available));
		result.lines << (available.isEmpty() ? Text::tr("Available mods: none") : Text::tr("Available mods: %1").arg(available.join(QStringLiteral(", "))));
	}
	result.payload.insert(QStringLiteral("changed"), QJsonArray::fromStringList(changed));
	result.payload.insert(QStringLiteral("mapWritten"), false);
	if (!change) {
		return result;
	}
	const auto saved = saveLevelMapAs(document, values.value(QStringLiteral("--output")).first(), seen.contains(QStringLiteral("--dry-run")),
		seen.contains(QStringLiteral("--overwrite")));
	if (!saved.succeeded()) {
		return fail(4, saved.errors.join(QLatin1Char('\n')));
	}
	result.payload.insert(QStringLiteral("mapWritten"), saved.written);
	result.payload.insert(QStringLiteral("output"), saved.outputPath);
	result.payload.insert(QStringLiteral("dryRun"), saved.dryRun);
	const int steps = static_cast<int>(changed.size());
	result.lines.prepend(steps == 0 ? Text::tr("The map already had these properties; nothing changed.")
									: Text::tr("Changed the map properties in %n undo step(s).", nullptr, steps));
	result.lines << levelMapSaveReportText(saved);
	return result;
}

} // namespace vibestudio::cli
