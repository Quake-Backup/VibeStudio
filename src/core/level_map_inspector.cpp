#include "core/level_map_inspector.h"

#include "core/game_installation.h"
#include "core/level_scene.h"
#include "core/level_scene_layers.h"
#include "core/level_scene_locks.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <cmath>

namespace vibestudio {

namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioLevelMapInspector)
};

bool textMap(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
}

const LevelMapEntity* worldspawn(const LevelMapDocument& document)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0) {
			return &entity;
		}
	}
	return nullptr;
}

bool worldValue(const LevelMapDocument& document, const char* key, QString* value)
{
	if (const LevelMapEntity* world = worldspawn(document)) {
		for (const LevelMapProperty& property : world->properties) {
			if (property.key.compare(QLatin1String(key), Qt::CaseInsensitive) == 0) {
				*value = property.value;
				return true;
			}
		}
	}
	return false;
}

// Numbers separated by spaces or commas; false when any does not read.
bool readNumbers(const QString& text, QVector<double>* numbers)
{
	static const QRegularExpression separators(QStringLiteral("[\\s,]+"));
	numbers->clear();
	for (const QString& part : text.split(separators, Qt::SkipEmptyParts)) {
		bool ok = false;
		const double value = part.toDouble(&ok);
		if (!ok || !std::isfinite(value)) {
			return false;
		}
		numbers->push_back(value);
	}
	return true;
}

QString numberText(double value)
{
	if (std::abs(value) < 1e15 && value == std::floor(value)) {
		return QString::number(static_cast<qint64>(value));
	}
	return QString::number(value, 'g', 12);
}

bool below(const LevelMapVec3& mins, const LevelMapVec3& maxs)
{
	return mins.x < maxs.x && mins.y < maxs.y && mins.z < maxs.z;
}

QString cornersText(const LevelMapSoftBounds& bounds)
{
	return QStringLiteral("%1 %2 %3 – %4 %5 %6")
		.arg(numberText(bounds.mins.x), numberText(bounds.mins.y), numberText(bounds.mins.z), numberText(bounds.maxs.x), numberText(bounds.maxs.y),
			numberText(bounds.maxs.z));
}

// Names the history step after the setting rather than the key, when the
// edit made one.
void nameStep(LevelMapDocument* document, quint64 revision, const char* description, const char* undoDescription)
{
	if (document->revision != revision && !document->undoStack.isEmpty()) {
		document->undoStack.last().description = QCoreApplication::translate("VibeStudioLevelMapInspector", description);
		document->undoStack.last().undoDescription = QCoreApplication::translate("VibeStudioLevelMapInspector", undoDescription);
	}
}

// Writes or takes away one worldspawn key as one undo step; nothing when the
// key already says `value` (or is already absent for an empty one).
bool writeWorldKey(LevelMapDocument* document, const char* key, const QString& value, bool remove, QString* error)
{
	const LevelMapEntity* world = worldspawn(*document);
	if (!world) {
		if (error) {
			*error = Text::tr("The map has no worldspawn entity to hold this setting.");
		}
		return false;
	}
	const int worldId = world->id;
	QString current;
	const bool present = worldValue(*document, key, &current);
	if (remove ? !present : (present && current == value)) {
		return true;
	}
	return editLevelMapWorldSettings(document, [&](LevelMapDocument* edited) {
		return remove ? removeLevelMapEntitiesProperty(edited, {worldId}, QString::fromLatin1(key), error)
					  : setLevelMapEntitiesProperty(edited, {worldId}, QString::fromLatin1(key), {value}, error);
	});
}

QJsonArray vectorJson(const LevelMapVec3& vector)
{
	return QJsonArray {vector.x, vector.y, vector.z};
}

} // namespace

bool editLevelMapWorldSettings(LevelMapDocument* document, const std::function<bool(LevelMapDocument*)>& edit)
{
	if (!document || !edit) {
		return false;
	}
	// The open-group boundary is set aside for this one edit and put back;
	// nothing here opens or closes a group, and the undo step records no
	// change of context.
	const QStringList openGroups = document->openSceneGroups;
	document->openSceneGroups.clear();
	const bool edited = edit(document);
	document->openSceneGroups = openGroups;
	return edited;
}

LevelMapSoftBounds levelMapGameSoftBounds(const LevelMapDocument& document)
{
	LevelMapSoftBounds bounds;
	bounds.mode = LevelMapSoftBoundsMode::Game;
	if (!textMap(document)) {
		return bounds;
	}
	// Quake III: q3map2's MIN_WORLD_COORD and MAX_WORLD_COORD
	// (external/compilers/vibemap3/tools/quake3/common/maxworld.h). Quake
	// and Quake II send coordinates as shorts of 1/8 unit.
	const double extent = document.format == LevelMapFormat::Quake3Map ? 65536.0 : 4096.0;
	bounds.valid = true;
	bounds.mins = {-extent, -extent, -extent, true};
	bounds.maxs = {extent, extent, extent, true};
	return bounds;
}

LevelMapSoftBounds levelMapSoftBounds(const LevelMapDocument& document)
{
	QString value;
	if (!textMap(document) || !worldValue(document, kLevelMapSoftBoundsKey, &value)) {
		return levelMapGameSoftBounds(document);
	}
	LevelMapSoftBounds bounds;
	bounds.value = value;
	if (value.trimmed().compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0) {
		bounds.mode = LevelMapSoftBoundsMode::Disabled;
		return bounds;
	}
	bounds.mode = LevelMapSoftBoundsMode::Custom;
	bounds.valid = parseLevelMapSoftBounds(value, &bounds.mins, &bounds.maxs);
	bounds.malformed = !bounds.valid;
	if (!bounds.valid) {
		bounds.mins = {};
		bounds.maxs = {};
	}
	return bounds;
}

QString levelMapSoftBoundsModeId(LevelMapSoftBoundsMode mode)
{
	switch (mode) {
	case LevelMapSoftBoundsMode::Custom:
		return QStringLiteral("custom");
	case LevelMapSoftBoundsMode::Disabled:
		return QStringLiteral("none");
	case LevelMapSoftBoundsMode::Game:
		break;
	}
	return QStringLiteral("game");
}

bool levelMapSoftBoundsModeFromId(const QString& id, LevelMapSoftBoundsMode* mode)
{
	const QString key = id.trimmed().toLower();
	if (key == QLatin1String("game")) {
		*mode = LevelMapSoftBoundsMode::Game;
	} else if (key == QLatin1String("none") || key == QLatin1String("disabled") || key == QLatin1String("off")) {
		*mode = LevelMapSoftBoundsMode::Disabled;
	} else if (key == QLatin1String("custom")) {
		*mode = LevelMapSoftBoundsMode::Custom;
	} else {
		return false;
	}
	return true;
}

bool parseLevelMapSoftBounds(const QString& text, LevelMapVec3* mins, LevelMapVec3* maxs)
{
	QVector<double> numbers;
	if (!readNumbers(text, &numbers) || numbers.size() != 6) {
		return false;
	}
	const LevelMapVec3 low {numbers[0], numbers[1], numbers[2], true};
	const LevelMapVec3 high {numbers[3], numbers[4], numbers[5], true};
	if (!below(low, high)) {
		return false;
	}
	*mins = low;
	*maxs = high;
	return true;
}

bool parseLevelMapSoftBoundsCorner(const QString& text, LevelMapVec3* corner)
{
	QVector<double> numbers;
	if (!readNumbers(text, &numbers) || (numbers.size() != 1 && numbers.size() != 3)) {
		return false;
	}
	*corner = numbers.size() == 1 ? LevelMapVec3 {numbers[0], numbers[0], numbers[0], true} : LevelMapVec3 {numbers[0], numbers[1], numbers[2], true};
	return true;
}

QString levelMapSoftBoundsCornerText(const LevelMapVec3& corner)
{
	if (corner.x == corner.y && corner.y == corner.z) {
		return numberText(corner.x);
	}
	return QStringLiteral("%1 %2 %3").arg(numberText(corner.x), numberText(corner.y), numberText(corner.z));
}

QString levelMapSoftBoundsValue(const LevelMapVec3& mins, const LevelMapVec3& maxs)
{
	return QStringLiteral("%1 %2 %3 %4 %5 %6")
		.arg(numberText(mins.x), numberText(mins.y), numberText(mins.z), numberText(maxs.x), numberText(maxs.y), numberText(maxs.z));
}

bool setLevelMapSoftBounds(LevelMapDocument* document, const LevelMapSoftBoundsChange& change, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document || !textMap(*document)) {
		if (error) {
			*error = Text::tr("Soft bounds belong to Quake-family maps; Doom maps have none.");
		}
		return false;
	}
	QString value;
	if (change.mode == LevelMapSoftBoundsMode::Disabled) {
		value = QStringLiteral("none");
	} else if (change.mode == LevelMapSoftBoundsMode::Custom) {
		if (!change.mins.valid || !change.maxs.valid || !below(change.mins, change.maxs)) {
			if (error) {
				*error = Text::tr("Each minimum must be below its maximum.");
			}
			return false;
		}
		value = levelMapSoftBoundsValue(change.mins, change.maxs);
	}
	const auto revision = document->revision;
	if (!writeWorldKey(document, kLevelMapSoftBoundsKey, value, change.mode == LevelMapSoftBoundsMode::Game, error)) {
		return false;
	}
	nameStep(document, revision, QT_TRANSLATE_NOOP("VibeStudioLevelMapInspector", "Set soft map bounds"),
		QT_TRANSLATE_NOOP("VibeStudioLevelMapInspector", "Restore soft map bounds"));
	return true;
}

QStringList levelMapObjectsOutsideBounds(const LevelMapDocument& document, const LevelMapVec3& mins, const LevelMapVec3& maxs)
{
	QStringList outside;
	if (!textMap(document) || !mins.valid || !maxs.valid) {
		return outside;
	}
	const auto beyond = [&](const LevelMapVec3& low, const LevelMapVec3& high) {
		return low.valid && high.valid
			&& (low.x < mins.x || low.y < mins.y || low.z < mins.z || high.x > maxs.x || high.y > maxs.y || high.z > maxs.z);
	};
	// Entities that hold brushes or patches are measured by them.
	QSet<int> owners;
	for (const LevelMapBrush& brush : document.brushes) {
		owners.insert(brush.entityId);
	}
	for (const LevelMapPatch& patch : document.patches) {
		owners.insert(patch.entityId);
	}
	for (const LevelMapEntity& entity : document.entities) {
		if (!owners.contains(entity.id) && entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) != 0
			&& beyond(entity.origin, entity.origin)) {
			outside << QStringLiteral("entity:%1").arg(entity.id);
		}
	}
	for (const LevelMapBrush& brush : document.brushes) {
		if (brush.boundsSolved && beyond(brush.mins, brush.maxs)) {
			outside << QStringLiteral("brush:%1").arg(brush.id);
		}
	}
	for (const LevelMapPatch& patch : document.patches) {
		if (beyond(patch.mins, patch.maxs)) {
			outside << QStringLiteral("patch:%1").arg(patch.id);
		}
	}
	return outside;
}

QStringList levelMapEnabledMods(const LevelMapDocument& document)
{
	QString value;
	QStringList mods;
	if (textMap(document) && worldValue(document, kLevelMapModsKey, &value)) {
		for (const QString& part : value.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
			if (const QString name = part.trimmed(); !name.isEmpty()) {
				mods << name;
			}
		}
	}
	return mods;
}

QString levelMapModNameProblem(const QString& name)
{
	const QString trimmed = name.trimmed();
	if (trimmed.isEmpty()) {
		return Text::tr("Give the mod's folder name.");
	}
	if (trimmed == QLatin1String(".") || trimmed == QLatin1String("..")) {
		return Text::tr("A mod is a folder beside the game's own folder, not . or ..");
	}
	for (const QChar character : trimmed) {
		if (character.category() == QChar::Other_Control || character == QLatin1Char(';') || character == QLatin1Char('"') || character == QLatin1Char('/')
			|| character == QLatin1Char('\\') || character == QLatin1Char(':')) {
			return Text::tr("A mod's folder name cannot hold ; \" / \\ : or control characters.");
		}
	}
	return QString();
}

bool setLevelMapEnabledMods(LevelMapDocument* document, const QStringList& mods, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document || !textMap(*document)) {
		if (error) {
			*error = Text::tr("Mods belong to Quake-family maps; Doom maps have none.");
		}
		return false;
	}
	QStringList names;
	QSet<QString> seen;
	for (const QString& mod : mods) {
		if (const QString problem = levelMapModNameProblem(mod); !problem.isEmpty()) {
			if (error) {
				*error = problem;
			}
			return false;
		}
		const QString name = mod.trimmed();
		if (seen.contains(name.toCaseFolded())) {
			if (error) {
				*error = Text::tr("%1 is listed twice.").arg(name);
			}
			return false;
		}
		seen.insert(name.toCaseFolded());
		names << name;
	}
	const auto revision = document->revision;
	if (!writeWorldKey(document, kLevelMapModsKey, names.join(QLatin1Char(';')), names.isEmpty(), error)) {
		return false;
	}
	nameStep(document, revision, QT_TRANSLATE_NOOP("VibeStudioLevelMapInspector", "Set enabled mods"),
		QT_TRANSLATE_NOOP("VibeStudioLevelMapInspector", "Restore enabled mods"));
	return true;
}

QStringList levelMapAvailableMods(const QString& gameRoot, const QStringList& excluded)
{
	QStringList mods;
	if (gameRoot.trimmed().isEmpty() || !QFileInfo(gameRoot).isDir()) {
		return mods;
	}
	QSet<QString> skip;
	for (const QString& folder : excluded) {
		skip.insert(folder.trimmed().toCaseFolded());
	}
	for (const QString& folder : QDir(gameRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
		if (!folder.startsWith(QLatin1Char('.')) && !skip.contains(folder.toCaseFolded()) && levelMapModNameProblem(folder).isEmpty()) {
			mods << folder;
		}
	}
	return mods;
}

QStringList levelMapBaseGameFolders()
{
	QStringList folders;
	for (const GameDefinition& definition : knownGameDefinitions()) {
		if (!definition.baseGameDirectory.isEmpty() && !folders.contains(definition.baseGameDirectory, Qt::CaseInsensitive)) {
			folders << definition.baseGameDirectory;
		}
	}
	return folders;
}

QVector<LevelMapLayerSummary> levelMapLayerSummaries(const LevelMapDocument& document)
{
	QVector<LevelMapLayerSummary> layers;
	if (document.format == LevelMapFormat::Unknown) {
		return layers;
	}
	LevelMapLayerSummary implicit;
	implicit.name = Text::tr("Default");
	layers << implicit;
	QHash<QString, int> index {{QString(), 0}};
	const QSet<QString> locked = levelSceneLockedNodes(document.scene);
	for (const LevelSceneNode& node : document.scene.nodes) {
		if (node.kind != LevelSceneNodeKind::Layer) {
			continue;
		}
		LevelMapLayerSummary layer;
		layer.id = node.id;
		layer.name = node.name;
		layer.visible = node.visible;
		layer.locked = locked.contains(node.id);
		layer.omitFromExport = levelSceneOmittedFromExport(node);
		index.insert(node.id, static_cast<int>(layers.size()));
		layers << layer;
	}
	const QHash<QString, QString> objectLayers = levelSceneObjectLayers(document, document.scene);
	for (auto it = objectLayers.cbegin(); it != objectLayers.cend(); ++it) {
		++layers[index.value(it.value(), 0)].objects;
	}
	const QHash<QString, QString> nodeLayers = levelSceneNodeLayers(document.scene);
	for (const LevelSceneNode& node : document.scene.nodes) {
		if (node.kind == LevelSceneNodeKind::Group) {
			++layers[index.value(nodeLayers.value(node.id), 0)].groups;
		}
	}
	layers[index.value(nodeLayers.value(document.activeSceneNode), 0)].creation = true;
	return layers;
}

QJsonObject levelMapSoftBoundsJson(const LevelMapDocument& document)
{
	const LevelMapSoftBounds bounds = levelMapSoftBounds(document);
	const LevelMapSoftBounds game = levelMapGameSoftBounds(document);
	QJsonObject object {
		{QStringLiteral("applies"), textMap(document)},
		{QStringLiteral("mode"), levelMapSoftBoundsModeId(bounds.mode)},
		{QStringLiteral("key"), QString::fromLatin1(kLevelMapSoftBoundsKey)},
		{QStringLiteral("value"), bounds.value},
		{QStringLiteral("valid"), bounds.valid},
		{QStringLiteral("malformed"), bounds.malformed},
	};
	if (bounds.valid) {
		object.insert(QStringLiteral("mins"), vectorJson(bounds.mins));
		object.insert(QStringLiteral("maxs"), vectorJson(bounds.maxs));
		const QStringList outside = levelMapObjectsOutsideBounds(document, bounds.mins, bounds.maxs);
		object.insert(QStringLiteral("outsideCount"), static_cast<int>(outside.size()));
		object.insert(QStringLiteral("outsideObjects"), QJsonArray::fromStringList(outside.mid(0, 256)));
	}
	QJsonObject gameDefault {{QStringLiteral("valid"), game.valid}};
	if (game.valid) {
		gameDefault.insert(QStringLiteral("mins"), vectorJson(game.mins));
		gameDefault.insert(QStringLiteral("maxs"), vectorJson(game.maxs));
	}
	object.insert(QStringLiteral("gameDefault"), gameDefault);
	return object;
}

QJsonArray levelMapLayerSummariesJson(const QVector<LevelMapLayerSummary>& layers)
{
	QJsonArray array;
	for (const LevelMapLayerSummary& layer : layers) {
		array.append(QJsonObject {
			{QStringLiteral("id"), layer.id},
			{QStringLiteral("name"), layer.name},
			{QStringLiteral("objects"), layer.objects},
			{QStringLiteral("groups"), layer.groups},
			{QStringLiteral("visible"), layer.visible},
			{QStringLiteral("locked"), layer.locked},
			{QStringLiteral("omitFromExport"), layer.omitFromExport},
			{QStringLiteral("creation"), layer.creation},
		});
	}
	return array;
}

QJsonObject levelMapInspectorJson(const LevelMapDocument& document)
{
	return QJsonObject {
		{QStringLiteral("softBounds"), levelMapSoftBoundsJson(document)},
		{QStringLiteral("mods"), QJsonArray::fromStringList(levelMapEnabledMods(document))},
		{QStringLiteral("modsKey"), QString::fromLatin1(kLevelMapModsKey)},
		{QStringLiteral("layers"), levelMapLayerSummariesJson(levelMapLayerSummaries(document))},
	};
}

QStringList levelMapInspectorLines(const LevelMapDocument& document)
{
	QStringList lines;
	if (textMap(document)) {
		const LevelMapSoftBounds bounds = levelMapSoftBounds(document);
		if (bounds.mode == LevelMapSoftBoundsMode::Disabled) {
			lines << Text::tr("Soft bounds: none");
		} else if (bounds.malformed) {
			lines << Text::tr("Soft bounds: “%1” is not six numbers with each minimum below its maximum, so none are in force").arg(bounds.value);
		} else if (bounds.mode == LevelMapSoftBoundsMode::Custom) {
			lines << Text::tr("Soft bounds: the map's own, %1").arg(cornersText(bounds));
		} else {
			lines << Text::tr("Soft bounds: the game's, %1").arg(cornersText(bounds));
		}
		if (bounds.valid) {
			const int outside = static_cast<int>(levelMapObjectsOutsideBounds(document, bounds.mins, bounds.maxs).size());
			lines << Text::tr("Objects outside the soft bounds: %1").arg(outside);
		}
		const QStringList mods = levelMapEnabledMods(document);
		lines << (mods.isEmpty() ? Text::tr("Mods: none") : Text::tr("Mods: %1").arg(mods.join(QStringLiteral(", "))));
	} else {
		lines << Text::tr("Soft bounds and mods: Quake-family maps only");
	}
	for (const LevelMapLayerSummary& layer : levelMapLayerSummaries(document)) {
		QStringList states;
		if (!layer.visible) {
			states << Text::tr("hidden");
		}
		if (layer.locked) {
			states << Text::tr("locked");
		}
		if (layer.omitFromExport) {
			states << Text::tr("omitted from export");
		}
		if (layer.creation) {
			states << Text::tr("new objects go here");
		}
		const int objects = layer.objects;
		const int groups = layer.groups;
		QString line = Text::tr("Layer %1: %2, %3").arg(layer.name, Text::tr("%n object(s)", nullptr, objects), Text::tr("%n group(s)", nullptr, groups));
		if (!states.isEmpty()) {
			line += QStringLiteral("; ") + states.join(QStringLiteral(", "));
		}
		lines << line;
	}
	return lines;
}

} // namespace vibestudio
