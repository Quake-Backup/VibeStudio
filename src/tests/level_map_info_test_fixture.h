#pragma once

// A Quake map for the Map Info and Map tab inspector tests: worldspawn and
// three brush entities (func_group, func_door and a func_detail far outside
// the game's ±4096 bounds), two lights, a player start, a misc_model, an
// info_null far outside and a box of shells. By NetRadiant Custom's rule four
// entities reach the game (worldspawn, the door, the start, the shells) and
// four are group entities, two of them in the game.

#include "core/level_document.h"
#include "core/level_map.h"

#include <QStringList>

namespace vibestudio::tests {

inline QString mapInfoBox(const LevelMapVec3& mins, const LevelMapVec3& maxs)
{
	LevelMapDocument box;
	LevelMapCreateRequest request;
	request.game = QStringLiteral("quake");
	request.starterRoom = false;
	createLevelMap(request, &box);
	addLevelMapBoxBrush(&box, mins, maxs, QStringLiteral("base/wall"));
	const auto number = [](double value) { return QString::number(value, 'g', 15); };
	const auto point = [&number](const LevelMapVec3& p) { return QStringLiteral("( %1 %2 %3 )").arg(number(p.x), number(p.y), number(p.z)); };
	QStringList lines {QStringLiteral("{")};
	if (!box.brushes.isEmpty()) {
		for (const LevelMapBrushFace& face : box.brushes.constFirst().faces) {
			lines << QStringLiteral("%1 %2 %3 base/wall 0 0 0 1 1").arg(point(face.p0), point(face.p1), point(face.p2));
		}
	}
	lines << QStringLiteral("}");
	return lines.join(QLatin1Char('\n'));
}

inline QByteArray mapInfoFixture()
{
	const auto entity = [](const QString& className, const QStringList& keys, const QString& brush = QString()) {
		QStringList lines {QStringLiteral("{"), QStringLiteral("\"classname\" \"%1\"").arg(className)};
		lines += keys;
		if (!brush.isEmpty()) {
			lines << brush;
		}
		lines << QStringLiteral("}");
		return lines.join(QLatin1Char('\n'));
	};
	QStringList blocks;
	blocks << entity(QStringLiteral("worldspawn"), {QStringLiteral("\"message\" \"Map Info\"")}, mapInfoBox({0, 0, 0, true}, {256, 256, 16, true}));
	blocks << entity(QStringLiteral("func_group"), {}, mapInfoBox({0, 0, 16, true}, {64, 64, 32, true}));
	blocks << entity(QStringLiteral("func_door"), {QStringLiteral("\"targetname\" \"door1\"")}, mapInfoBox({64, 0, 16, true}, {128, 16, 128, true}));
	blocks << entity(QStringLiteral("func_detail"), {}, mapInfoBox({5000, 0, 0, true}, {5064, 64, 64, true}));
	blocks << entity(QStringLiteral("light"), {QStringLiteral("\"origin\" \"32 32 64\"")});
	blocks << entity(QStringLiteral("light"), {QStringLiteral("\"origin\" \"96 32 64\"")});
	blocks << entity(QStringLiteral("info_player_start"), {QStringLiteral("\"origin\" \"32 32 40\"")});
	blocks << entity(QStringLiteral("misc_model"), {QStringLiteral("\"origin\" \"128 128 16\"")});
	blocks << entity(QStringLiteral("info_null"), {QStringLiteral("\"origin\" \"100000 0 0\"")});
	blocks << entity(QStringLiteral("item_shells"), {QStringLiteral("\"origin\" \"160 160 24\"")});
	return (blocks.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
}

} // namespace vibestudio::tests
