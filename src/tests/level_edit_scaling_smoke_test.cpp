// The level document's edit paths that were reworked to scale with the size of
// the edit rather than the size of the map (core/level_map.cpp,
// core/level_visibility.cpp): lookups by id when records are out of id order,
// selection changes that refuse without touching the map, shared-line refusals
// that read the same for a few surfaces and for many, objects woven back into
// their places by undo, session visibility followed through structural edits,
// the texture list left alone by entity edits, face points rewritten in place
// without disturbing the rest of the line, texture-policy moves refused alike
// for one brush and for many, object counts that agree with the named lists,
// and 8-bit map text kept byte for byte. Synthetic geometry only.
#include "core/level_document.h"
#include "core/level_map.h"
#include "core/level_map_inspector.h"
#include "core/level_scene.h"
#include "core/level_visibility.h"
#include "tests/level_geometry_test_helpers.h"
#include <QCoreApplication>
#include <algorithm>
#include <iostream>

using namespace slopstudio;
namespace {
bool ok = true;
bool expect(bool result, const char* message, const QString& detail = {})
{
	if (!result) { ok = false; std::cerr << "FAIL: " << message << ": " << qPrintable(detail) << std::endl; }
	return result;
}
LevelMapSelectionRef brush(int id) { return {LevelMapSelectionKind::QuakeBrush, id}; }
LevelMapSelectionRef entity(int id) { return {LevelMapSelectionKind::Entity, id}; }
LevelMapDocument fixture(int count)
{
	LevelMapDocument map; QString error;
	expect(tests::createGeometryFixture(count, &map, &error), "fixture", error);
	return map;
}
// The fixture as a map opened from a file: source lines, spans and all.
LevelMapDocument loaded(const QByteArray& bytes)
{
	LevelMapDocument map; QString error;
	LevelMapLoadRequest request; request.path = QStringLiteral("scaling.map"); request.engineHint = QStringLiteral("quake3");
	expect(loadLevelMapBytes(request, bytes, &map, &error), "load", error);
	return map;
}
LevelMapDocument loaded(int count) { return loaded(serializeLevelMap(fixture(count)).bytes); }
QVector<int> brushIds(const LevelMapDocument& map)
{
	QVector<int> ids;
	for (const auto& record : map.brushes) { ids.push_back(record.id); }
	return ids;
}
const LevelMapBrush* brushOf(const LevelMapDocument& map, int id)
{
	const auto found = std::find_if(map.brushes.cbegin(), map.brushes.cend(), [id](const LevelMapBrush& record) { return record.id == id; });
	return found == map.brushes.cend() ? nullptr : &*found;
}

// Records out of id order take the fallback search: every edit by id must
// still reach the right record.
void unorderedIds()
{
	auto ordered = fixture(40);
	auto shuffled = ordered;
	std::reverse(shuffled.brushes.begin(), shuffled.brushes.end());
	std::swap(shuffled.brushes[3], shuffled.brushes[17]);
	QString error;
	for (auto* map : {&ordered, &shuffled}) {
		expect(setLevelMapSelection(map, {brush(5), brush(31), brush(12)}, &error), "select by id", error);
		expect(moveLevelMapSelection(map, 8, 0, 0, &error), "move by id", error);
		expect(applyLevelMapTexture(map, QStringLiteral("studio/other"), nullptr, &error), "texture by id", error);
	}
	for (int id = 0; id < 40; ++id) {
		const auto* left = brushOf(ordered, id);
		const auto* right = brushOf(shuffled, id);
		if (!expect(left && right, "brush present", QString::number(id))) { continue; }
		const bool moved = id == 5 || id == 31 || id == 12;
		expect(left->mins.x == right->mins.x && left->selected == right->selected && left->selected == moved
			&& left->faces.first().textureName == right->faces.first().textureName
			&& (left->faces.first().textureName == QStringLiteral("studio/other")) == moved,
			"same edit whatever the record order", QString::number(id));
	}
	expect(deleteLevelMapObjects(&shuffled, {brush(31), brush(0)}, &error) && !brushOf(shuffled, 31) && !brushOf(shuffled, 0)
		&& shuffled.brushes.size() == 38, "delete by id", error);
	const auto afterDelete = brushIds(shuffled);
	expect(undoLevelMapEdit(&shuffled, &error) && shuffled.brushes.size() == 40 && brushOf(shuffled, 31) && brushOf(shuffled, 0),
		"undo delete restores unordered records", error);
	expect(redoLevelMapEdit(&shuffled, &error) && brushIds(shuffled) == afterDelete, "redo delete", error);
	expect(!levelMapObjectExists(shuffled, brush(31)) && levelMapObjectExists(shuffled, brush(30))
		&& !levelMapObjectExists(shuffled, brush(400)), "existence by id");
}

void selectionHistory()
{
	auto map = fixture(12); QString error;
	expect(changeLevelMapSelection(&map, {brush(1), brush(2)}, &error) && map.undoStack.size() == 1 && map.selection.size() == 2
		&& brushOf(map, 1)->selected && brushOf(map, 2)->selected && !brushOf(map, 3)->selected, "selection is one undo step", error);
	const auto stamp = map.historyRevision;
	expect(changeLevelMapSelection(&map, {brush(1), brush(2)}, &error) && map.undoStack.size() == 1 && map.historyRevision == stamp,
		"an unchanged selection adds no history", error);
	const auto before = map;
	expect(!changeLevelMapSelection(&map, {brush(3), brush(99)}, &error) && !error.isEmpty(), "a missing object is refused");
	expect(map.selection == before.selection && map.undoStack.size() == before.undoStack.size() && map.historyRevision == before.historyRevision
		&& map.revision == before.revision && brushOf(map, 1)->selected && !brushOf(map, 3)->selected, "a refused selection changes nothing");
	expect(changeLevelMapSelection(&map, {brush(4), brush(4), brush(3)}, &error) && map.selection == QVector<LevelMapSelectionRef>{brush(4), brush(3)}
		&& map.selectedObjectId == 3, "duplicates collapse and the last entry is primary", error);
	expect(undoLevelMapEdit(&map, &error) && map.selection == before.selection && brushOf(map, 1)->selected && !brushOf(map, 4)->selected,
		"undo restores the selection and its flags", error);
}

// Two brushes with a face each on one source line: retexturing either is
// refused, and the refusal names the same pair whether a few surfaces are
// checked against the map or many against the gathered line owners.
void sharedLines()
{
	const auto text = QString::fromUtf8(serializeLevelMap(fixture(4)).bytes);
	QStringList lines = text.split(QLatin1Char('\n'));
	// Join the closing brace of the first brush, the opening brace of the
	// second and their neighbouring faces into one line.
	int firstClose = -1;
	int depth = 0;
	for (int index = 0; index < lines.size(); ++index) {
		const auto trimmed = lines.at(index).trimmed();
		if (trimmed == QLatin1String("{")) { ++depth; }
		else if (trimmed == QLatin1String("}")) { if (depth == 2) { firstClose = index; break; } --depth; }
	}
	if (!expect(firstClose > 1 && firstClose + 2 < lines.size() && lines.at(firstClose + 1).trimmed() == QLatin1String("{"), "fixture layout", text.left(400))) { return; }
	const QString joined = lines.at(firstClose - 1) + QStringLiteral(" } { ") + lines.at(firstClose + 2);
	lines[firstClose - 1] = joined;
	lines.remove(firstClose, 3);
	auto map = loaded(lines.join(QLatin1Char('\n')).toUtf8());
	if (!expect(map.brushes.size() == 4, "compact brushes parse", QString::number(map.brushes.size()))) { return; }
	const int first = map.brushes.at(0).id;
	const int second = map.brushes.at(1).id;
	const auto refusal = [&](const QVector<LevelMapSelectionRef>& selection) {
		auto candidate = map; QString error;
		setLevelMapSelection(&candidate, selection);
		const bool applied = replaceLevelMapTexture(&candidate, QStringLiteral("studio/cache"), QStringLiteral("studio/other"), true, nullptr, &error);
		expect(!applied && serializeLevelMap(candidate).bytes == serializeLevelMap(map).bytes, "a shared line refuses the whole edit", error);
		return error;
	};
	const QString few = refusal({brush(first)});
	const QString many = refusal({brush(first), brush(map.brushes.at(2).id), brush(map.brushes.at(3).id)});
	expect(!few.isEmpty() && few == many, "the refusal reads the same for few and many surfaces", few + QStringLiteral(" / ") + many);
	expect(few.contains(QString::number(first)) && few.contains(QString::number(second)), "the refusal names both brushes", few);
	const QString other = refusal({brush(second), brush(map.brushes.at(2).id), brush(map.brushes.at(3).id)});
	expect(!other.isEmpty() && other != few, "the other brush of the pair is refused in its own name", other);
	// Brushes with lines of their own still take the edit, many at a time.
	QString error; int replaced = 0;
	setLevelMapSelection(&map, {brush(map.brushes.at(2).id), brush(map.brushes.at(3).id)});
	expect(replaceLevelMapTexture(&map, QStringLiteral("studio/cache"), QStringLiteral("studio/other"), true, &replaced, &error) && replaced == 12,
		"unshared brushes are retextured", error);
	expect(moveLevelMapSelection(&map, 16, 0, 0, &error), "unshared brushes move", error);
	setLevelMapSelection(&map, {brush(first), brush(map.brushes.at(2).id)});
	QString single;
	expect(!moveLevelMapSelection(&map, 16, 0, 0, &single) && !single.isEmpty(), "a brush sharing a line does not move");
	auto bulk = loaded(48);
	QVector<LevelMapSelectionRef> everything;
	for (const auto& record : bulk.brushes) { everything.push_back(brush(record.id)); }
	setLevelMapSelection(&bulk, everything);
	expect(moveLevelMapSelection(&bulk, 16, 0, 0, &error) && bulk.undoStack.size() == 1, "many brushes move as one step", error);
}

// Deleted objects go back where they were, a few by insertion and many woven
// in one pass; both leave the records exactly as they stood.
void restoreOrder()
{
	for (const int step : {7, 3}) {
		auto map = loaded(60); QString error;
		for (int index = 0; index < 5; ++index) {
			expect(addLevelMapEntity(&map, QStringLiteral("light"), {double(index * 16), -64, 32, true}, {}, nullptr, &error), "add entity", error);
		}
		const auto before = map;
		QVector<LevelMapSelectionRef> doomed;
		for (int index = 0; index < map.brushes.size(); index += step) { doomed.push_back(brush(map.brushes.at(index).id)); }
		doomed.push_back(entity(map.entities.at(2).id));
		doomed.push_back(entity(map.entities.at(4).id));
		expect(deleteLevelMapObjects(&map, doomed, &error) && map.brushes.size() == before.brushes.size() - (doomed.size() - 2)
			&& map.entities.size() == before.entities.size() - 2, "delete interleaved objects", error);
		expect(map.textureReferences.size() == map.brushes.size() * 6, "deleted brushes leave the texture list");
		expect(undoLevelMapEdit(&map, &error), "undo delete", error);
		bool same = brushIds(map) == brushIds(before) && map.entities.size() == before.entities.size() && map.textureReferences == before.textureReferences;
		for (int index = 0; same && index < map.entities.size(); ++index) { same = map.entities.at(index).id == before.entities.at(index).id; }
		for (int index = 0; same && index < map.brushes.size(); ++index) {
			same = map.brushes.at(index).startLine == before.brushes.at(index).startLine && map.brushes.at(index).mins.x == before.brushes.at(index).mins.x;
		}
		expect(same, "undo puts every object back in its place", QString::number(step));
		expect(serializeLevelMap(map).bytes == serializeLevelMap(before).bytes, "the restored map saves as it did", QString::number(step));
		expect(redoLevelMapEdit(&map, &error) && undoLevelMapEdit(&map, &error) && brushIds(map) == brushIds(before), "redo and undo again", error);
	}
}

void textureList()
{
	auto map = loaded(20); QString error;
	const auto references = map.textureReferences;
	int light = -1;
	expect(addLevelMapEntity(&map, QStringLiteral("light"), {0, -64, 32, true}, {}, &light, &error) && map.textureReferences == references,
		"an added entity leaves the texture list", error);
	expect(setLevelMapSelection(&map, {entity(light)}) && deleteLevelMapSelection(&map, &error) && map.textureReferences == references,
		"a deleted entity leaves the texture list", error);
	expect(undoLevelMapEdit(&map, &error) && undoLevelMapEdit(&map, &error) && map.textureReferences == references, "and so do their undos", error);
	expect(addLevelMapBoxBrush(&map, {0, -256, 0, true}, {32, -224, 32, true}, QStringLiteral("studio/new"), nullptr, &error)
		&& map.textureReferences.size() == references.size() + 6 && map.textureReferences.count(QStringLiteral("studio/new")) == 6
		&& levelMapTextureNames(map).contains(QStringLiteral("studio/new")), "a new brush joins the texture list", error);
	expect(undoLevelMapEdit(&map, &error) && map.textureReferences == references, "and leaves it on undo", error);
	expect(redoLevelMapEdit(&map, &error) && map.textureReferences.size() == references.size() + 6, "and returns on redo", error);
}

// Temporarily hidden objects stay hidden through edits around them, go with
// their object when it is deleted, and come back with it.
void visibility()
{
	auto map = loaded(12); QString error;
	const int hiddenId = map.brushes.at(4).id;
	const QString hidden = QStringLiteral("brush:%1").arg(hiddenId);
	setLevelMapSelection(&map, {brush(hiddenId)});
	expect(changeLevelMapVisibility(&map, {LevelVisibilityOperation::HideSelected, true}, nullptr, &error) && map.hiddenObjects == QSet<QString>{hidden},
		"hide one brush", error);
	expect(levelMapVisibilityTargets(map).contains(hidden) && !levelMapVisibilityTargets(map).contains(QStringLiteral("brush:04")),
		"targets are canonical names");
	expect(addLevelMapEntity(&map, QStringLiteral("light"), {0, -64, 32, true}, {}, nullptr, &error) && map.hiddenObjects == QSet<QString>{hidden},
		"adding an entity keeps the hidden brush", error);
	expect(undoLevelMapEdit(&map, &error) && map.hiddenObjects == QSet<QString>{hidden} && redoLevelMapEdit(&map, &error)
		&& map.hiddenObjects == QSet<QString>{hidden}, "and so do its undo and redo", error);
	expect(deleteLevelMapObjects(&map, {brush(map.brushes.at(1).id), brush(map.brushes.at(8).id)}, &error) && map.hiddenObjects == QSet<QString>{hidden},
		"deleting other brushes keeps it", error);
	expect(undoLevelMapEdit(&map, &error) && map.hiddenObjects == QSet<QString>{hidden}, "undo of that deletion keeps it", error);
	expect(deleteLevelMapObjects(&map, {brush(hiddenId)}, &error) && map.hiddenObjects.isEmpty(), "a deleted object leaves the hidden set", error);
	expect(undoLevelMapEdit(&map, &error) && map.hiddenObjects == QSet<QString>{hidden}, "and returns hidden when restored", error);
	expect(redoLevelMapEdit(&map, &error) && map.hiddenObjects.isEmpty() && undoLevelMapEdit(&map, &error)
		&& map.hiddenObjects == QSet<QString>{hidden}, "through redo and undo again", error);
}

// A moved face rewrites its three points and nothing else on its line.
void facePoints()
{
	const auto text = QString::fromUtf8(serializeLevelMap(fixture(2)).bytes);
	QStringList lines = text.split(QLatin1Char('\n'));
	int face = -1;
	for (int index = 0; index < lines.size(); ++index) {
		if (lines.at(index).trimmed().startsWith(QLatin1Char('('))) { face = index; break; }
	}
	if (!expect(face >= 0 && lines.at(face).count(QStringLiteral(") (")) >= 2, "fixture face line", text.left(300))) { return; }
	const QString canonical = lines.at(face);
	// Odd but legal spacing: no gap between the first two groups, tabs inside
	// the first, and a comment after the numbers.
	QString spaced = canonical;
	spaced.replace(spaced.indexOf(QStringLiteral(") (")), 3, QStringLiteral(")("));
	spaced.replace(spaced.indexOf(QStringLiteral("( ")), 2, QStringLiteral("(\t"));
	spaced += QStringLiteral(" // keep ( 1 2 3 ) me");
	QStringList edited = lines;
	edited[face] = spaced;
	const auto moveAndSave = [&](const QStringList& source) {
		auto map = loaded(source.join(QLatin1Char('\n')).toUtf8()); QString error;
		expect(map.brushes.size() == 2 && setLevelMapSelection(&map, {brush(map.brushes.first().id)})
			&& moveLevelMapSelection(&map, 16, -8, 4, &error), "move the first brush", error);
		return QString::fromUtf8(serializeLevelMap(map).bytes).split(QLatin1Char('\n'));
	};
	const auto plain = moveAndSave(lines);
	const auto odd = moveAndSave(edited);
	if (!expect(plain.size() == lines.size() && odd.size() == lines.size(), "a move keeps the line count")) { return; }
	expect(plain.at(face) != canonical && plain.at(face).count(QStringLiteral(") (")) >= 2, "the moved face is rewritten", plain.at(face));
	QString expected = plain.at(face);
	expected.replace(expected.indexOf(QStringLiteral(") (")), 3, QStringLiteral(")("));
	expected += QStringLiteral(" // keep ( 1 2 3 ) me");
	expect(odd.at(face) == expected, "only the three point groups change", odd.at(face) + QStringLiteral(" / ") + expected);
	for (int index = 0; index < lines.size(); ++index) {
		if (index != face && plain.at(index) != odd.at(index)) { expect(false, "other lines agree", QString::number(index)); break; }
	}
	// What was written reads back as the moved brush.
	const auto reread = loaded(odd.join(QLatin1Char('\n')).toUtf8());
	const auto original = loaded(lines.join(QLatin1Char('\n')).toUtf8());
	expect(reread.brushes.size() == 2 && reread.brushes.first().mins.x == original.brushes.first().mins.x + 16
		&& reread.brushes.first().mins.y == original.brushes.first().mins.y - 8 && reread.brushes.first().mins.z == original.brushes.first().mins.z + 4
		&& reread.brushes.at(1).mins.x == original.brushes.at(1).mins.x, "the saved move reads back");
	// Fractional and exponent spellings are found as numbers too.
	QStringList scientific = lines;
	QString exponent = canonical;
	const int open = exponent.indexOf(QLatin1Char('('));
	const int close = exponent.indexOf(QLatin1Char(')'));
	const QStringList numbers = exponent.mid(open + 1, close - open - 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
	if (expect(numbers.size() == 3, "three numbers", exponent)) {
		exponent.replace(open, close - open + 1, QStringLiteral("(%1.0   %2e0\t%3)").arg(numbers.at(0), numbers.at(1), numbers.at(2)));
		scientific[face] = exponent;
		const auto saved = moveAndSave(scientific);
		expect(saved.size() == lines.size() && saved.at(face) == plain.at(face), "decimal and exponent spellings are rewritten", saved.value(face));
	}
}

// A .map in an older editor's 8-bit code page keeps every byte through an
// edit and a save; a UTF-8 map stays UTF-8.
void eightBitText()
{
	const QByteArray plain = serializeLevelMap(fixture(3)).bytes;
	const QByteArray key("\"classname\" \"worldspawn\"\n");
	const auto at = plain.indexOf(key);
	if (!expect(at >= 0, "fixture worldspawn")) { return; }
	// "Caf\xE9 \xA9 1997" in Windows-1252 / Latin-1: not valid UTF-8.
	QByteArray legacy = plain;
	legacy.insert(at + key.size(), QByteArray("\"message\" \"Caf\xE9 \xA9 1997\"\n"));
	QByteArray modern = plain;
	modern.insert(at + key.size(), QByteArray("\"message\" \"Caf\xC3\xA9 \xC2\xA9 1997\"\n"));
	QString error;

	auto old = loaded(legacy);
	expect(old.sourceTextEightBit && old.issues.isEmpty(), "8-bit text is recognised");
	const auto message = [](const LevelMapDocument& map) {
		for (const auto& property : map.entities.first().properties) {
			if (property.key == QLatin1String("message")) { return property.value; }
		}
		return QString();
	};
	expect(message(old) == QString::fromLatin1("Caf\xE9 \xA9 1997"), "each byte reads as its character", message(old));
	expect(serializeLevelMap(old).bytes == legacy, "an unedited 8-bit map saves byte for byte");
	expect(setLevelMapSelection(&old, {brush(old.brushes.at(1).id)}) && moveLevelMapSelection(&old, 16, 0, 0, &error), "edit beside the text", error);
	const auto saved = serializeLevelMap(old);
	expect(saved.succeeded() && saved.warnings.isEmpty() && saved.bytes.contains(QByteArray("\"Caf\xE9 \xA9 1997\""))
		&& !saved.bytes.contains(QByteArray("\xEF\xBF\xBD")) && !saved.bytes.contains(QByteArray("\xC3\xA9")), "an edited 8-bit map keeps the author's bytes");
	const auto reread = loaded(saved.bytes);
	expect(reread.sourceTextEightBit && message(reread) == message(old), "and reads back the same");
	// A character no 8-bit page holds turns the save to UTF-8, and says so.
	expect(setLevelMapEntityProperty(&old, old.entities.first().id, QStringLiteral("author"), QString::fromUtf8("\xC5\x81ukasz \xE2\x98\x85"), &error), "add wider text", error);
	const auto widened = serializeLevelMap(old);
	expect(widened.succeeded() && widened.warnings.size() == 1 && widened.bytes.isValidUtf8()
		&& widened.bytes.contains(QByteArray("Caf\xC3\xA9 \xC2\xA9 1997")) && widened.bytes.contains(QByteArray("\xC5\x81ukasz")),
		"wider characters save as UTF-8 with a warning", widened.warnings.join(QLatin1Char(' ')));
	const auto widenedAgain = loaded(widened.bytes);
	expect(!widenedAgain.sourceTextEightBit && message(widenedAgain) == message(old), "which reopens as UTF-8 with the same text");

	auto utf8 = loaded(modern);
	expect(!utf8.sourceTextEightBit && message(utf8) == QString::fromUtf8("Caf\xC3\xA9 \xC2\xA9 1997"), "UTF-8 text is read as UTF-8", message(utf8));
	expect(serializeLevelMap(utf8).bytes == modern && serializeLevelMap(utf8).warnings.isEmpty(), "and saved as UTF-8");
	auto ascii = loaded(plain);
	expect(!ascii.sourceTextEightBit && serializeLevelMap(ascii).bytes == plain, "plain text is unchanged");
	// A UTF-8 mark keeps the UTF-8 reading even when later bytes are not UTF-8.
	const auto marked = loaded(QByteArray("\xEF\xBB\xBF") + legacy);
	expect(!marked.sourceTextEightBit && marked.brushes.size() == 3, "a marked file is read as UTF-8");
}

// A move under a texture policy (what a nudge or a drag makes) checks a few
// brushes against the map and many against gathered lines; both refuse a
// brush that shares a source line, in the same words.
void texturePolicyMoves()
{
	const auto text = QString::fromUtf8(serializeLevelMap(fixture(16)).bytes);
	QStringList lines = text.split(QLatin1Char('\n'));
	int firstClose = -1;
	int depth = 0;
	for (int index = 0; index < lines.size(); ++index) {
		const auto trimmed = lines.at(index).trimmed();
		if (trimmed == QLatin1String("{")) { ++depth; }
		else if (trimmed == QLatin1String("}")) { if (depth == 2) { firstClose = index; break; } --depth; }
	}
	if (!expect(firstClose > 1 && firstClose + 2 < lines.size(), "fixture layout")) { return; }
	lines[firstClose - 1] = lines.at(firstClose - 1) + QStringLiteral(" } { ") + lines.at(firstClose + 2);
	lines.remove(firstClose, 3);
	const auto map = loaded(lines.join(QLatin1Char('\n')).toUtf8());
	if (!expect(map.brushes.size() == 16, "compact brushes parse")) { return; }
	const LevelMapTextureLockOptions lock {true, false};
	const auto move = [&](const QVector<LevelMapSelectionRef>& selection, QString* error) {
		auto candidate = map;
		setLevelMapSelection(&candidate, selection);
		const bool moved = moveLevelMapSelection(&candidate, 16, 0, 0, lock, error);
		if (!moved) { expect(serializeLevelMap(candidate).bytes == serializeLevelMap(map).bytes, "a refused move changes nothing"); }
		return moved;
	};
	QVector<LevelMapSelectionRef> many {brush(map.brushes.at(0).id)};
	QVector<LevelMapSelectionRef> free;
	for (int index = 2; index < map.brushes.size(); ++index) { many.push_back(brush(map.brushes.at(index).id)); free.push_back(brush(map.brushes.at(index).id)); }
	QString few, bulk, error;
	expect(!move({brush(map.brushes.at(0).id)}, &few) && !few.isEmpty(), "one brush sharing a line does not move");
	expect(!move(many, &bulk) && bulk == few, "nor among many, for the same reason", few + QStringLiteral(" / ") + bulk);
	expect(move({brush(map.brushes.at(5).id)}, &error), "one free brush moves", error);
	expect(move(free, &error), "many free brushes move", error);
}

// Counts the Levels page shows after every edit agree with the named lists
// they stand in for.
void sceneCounts()
{
	auto map = loaded(12); QString error;
	expect(addLevelMapEntity(&map, QStringLiteral("light"), {0, -64, 32, true}, {}, nullptr, &error), "add entity", error);
	expect(levelSceneObjectCount(map) == levelSceneObjects(map).size() && levelSceneObjectCount(map) == 13, "scene objects are counted as they are named");
	auto layers = levelMapLayerSummaries(map);
	expect(layers.size() == 1 && layers.first().objects == 13 && layers.first().creation, "every object is in Default without layers");
	QString layer;
	expect(createLevelSceneNode(&map, LevelSceneNodeKind::Layer, QStringLiteral("Detail"), {}, &layer, &error)
		&& assignLevelSceneObjects(&map, layer, {QStringLiteral("brush:%1").arg(map.brushes.at(2).id), QStringLiteral("brush:%1").arg(map.brushes.at(3).id)}, &error),
		"make a layer", error);
	layers = levelMapLayerSummaries(map);
	expect(layers.size() == 2 && layers.at(0).objects == 11 && layers.at(1).objects == 2 && levelSceneMembers(map, {}, false).size() == 11,
		"layers split the count");
	LevelMapDocument doom; LevelMapCreateRequest request; request.game = QStringLiteral("doom");
	expect(createLevelMap(request, &doom, &error) && levelSceneObjectCount(doom) == levelSceneObjects(doom).size() && levelSceneObjectCount(doom) > 0,
		"Doom objects are counted as they are named", error);
}

// Whether the selection can be deleted is asked about ten times after every
// selection change, so each selected entity is found by id rather than by a
// pass over the map's entities: worldspawn holds the answer back wherever it
// stands in the selection, in id order and out of it.
void deletableSelection()
{
	auto ordered = fixture(6); QString error;
	int first = -1, second = -1;
	expect(addLevelMapEntity(&ordered, QStringLiteral("light"), {0, -64, 32, true}, {}, &first, &error)
		&& addLevelMapEntity(&ordered, QStringLiteral("light"), {32, -64, 32, true}, {}, &second, &error), "add entities", error);
	int world = -1;
	for (const auto& record : std::as_const(ordered.entities)) {
		if (record.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0) { world = record.id; }
	}
	if (!expect(world >= 0 && first >= 0 && second >= 0 && ordered.entities.size() == 3, "fixture entities")) { return; }
	auto shuffled = ordered;
	std::reverse(shuffled.entities.begin(), shuffled.entities.end());
	const int box = ordered.brushes.at(2).id;
	for (auto* map : {&ordered, &shuffled}) {
		const QString order = map == &ordered ? QStringLiteral("id order") : QStringLiteral("out of id order");
		const auto deletable = [&](const QVector<LevelMapSelectionRef>& selection) {
			auto probe = *map;
			return expect(setLevelMapSelection(&probe, selection, &error), "select", error) && levelMapSelectionIsDeletable(probe);
		};
		expect(deletable({entity(first), brush(box), entity(second)}), "entities and a brush can be deleted", order);
		expect(!deletable({entity(world)}), "worldspawn alone cannot", order);
		expect(!deletable({entity(first), entity(world), brush(box)}), "nor among other objects", order);
		expect(!deletable({brush(box), entity(second), entity(world)}), "nor as the primary", order);
		expect(deletable({brush(box)}), "a brush alone can", order);
	}
	auto empty = ordered;
	setLevelMapSelection(&empty, {});
	expect(empty.selection.isEmpty() && !levelMapSelectionIsDeletable(empty), "nothing selected is nothing to delete");
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	texturePolicyMoves();
	sceneCounts();
	eightBitText();
	unorderedIds();
	selectionHistory();
	sharedLines();
	restoreOrder();
	textureList();
	visibility();
	facePoints();
	deletableSelection();
	if (ok) { std::cout << "level edit scaling checks passed" << std::endl; }
	return ok ? 0 : 1;
}
