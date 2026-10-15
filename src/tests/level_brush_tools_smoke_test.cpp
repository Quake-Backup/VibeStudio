// The brush and selection tools of this round in the core: hollow's
// touching and Make Room modes, Snap Vertices, component moves across
// brushes and patches as one edit, Expand Selection to Whole Entities,
// Radiant's entity and brush numbers, and replacing the selection with a
// prefab. Each edit is checked against exact bytes after undo and against a
// reload.

#include "core/level_brush.h"
#include "core/level_document.h"
#include "core/level_patch.h"
#include "core/level_prefab.h"
#include "core/level_selection_tools.h"
#include "core/map_geometry.h"

#include <QCoreApplication>

#include <cmath>
#include <iostream>

using namespace vibestudio;

namespace {

int failures = 0;

bool expect(bool condition, const char* message, const QString& detail = {})
{
	if (!condition) {
		++failures;
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty()) {
			std::cerr << " (" << detail.toStdString() << ")";
		}
		std::cerr << "\n";
	}
	return condition;
}

bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }

QByteArray bytesOf(const LevelMapDocument& document) { return serializeLevelMap(document).bytes; }

LevelMapDocument reloaded(const LevelMapDocument& document)
{
	LevelMapDocument loaded;
	QString error;
	loadLevelMapBytes({QStringLiteral("brush-tools.map"), {}, document.format == LevelMapFormat::Quake3Map ? QStringLiteral("idtech3") : QString()},
		bytesOf(document), &loaded, &error);
	return loaded;
}

LevelMapDocument mapWith(const QString& game, const QVector<std::pair<LevelMapVec3, LevelMapVec3>>& boxes)
{
	LevelMapDocument document;
	LevelMapCreateRequest create;
	create.game = game;
	create.starterRoom = false;
	QString error;
	createLevelMap(create, &document, &error);
	for (const auto& [mins, maxs] : boxes) {
		addLevelMapBoxBrush(&document, mins, maxs, QStringLiteral("studio/wall"), nullptr, &error);
	}
	return reloaded(document);
}

double volume(const LevelMapBrush& brush)
{
	const MapBrushGeometry geometry = solveBrushGeometry(brush.faces, brush.id, brush.entityId);
	double total = 0;
	for (const auto& face : geometry.faces) {
		if (!face.isValid()) {
			continue;
		}
		double ax = 0, ay = 0, az = 0;
		for (int i = 0; i < face.points.size(); ++i) {
			const auto& a = face.points.at(i);
			const auto& b = face.points.at((i + 1) % face.points.size());
			ax += a.y * b.z - a.z * b.y;
			ay += a.z * b.x - a.x * b.z;
			az += a.x * b.y - a.y * b.x;
		}
		const auto& p = face.points.first();
		total += (p.x * ax + p.y * ay + p.z * az) / 6.0;
	}
	return std::abs(total);
}

void checkHollowModes()
{
	QString error;
	const double outer = 128.0 * 128.0 * 128.0, inner = 96.0 * 96.0 * 96.0, grown = 160.0 * 160.0 * 160.0;
	for (const QString& game : {QStringLiteral("quake"), QStringLiteral("quake3")}) {
		for (const LevelMapHollowMode mode : {LevelMapHollowMode::Overlap, LevelMapHollowMode::Touch, LevelMapHollowMode::Room}) {
			LevelMapDocument document = mapWith(game, {{{-64, -64, -64, true}, {64, 64, 64, true}}});
			const QByteArray original = bytesOf(document);
			setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, document.brushes.first().id}});
			int hollowed = 0;
			const QString label = game + QLatin1Char(' ') + levelMapHollowModeId(mode);
			if (!expect(hollowLevelMapSelection(&document, 16, mode, &hollowed, &error) && hollowed == 1, "hollow in each mode", label + QLatin1Char(' ') + error)) {
				continue;
			}
			expect(document.brushes.size() == 6 && document.undoStack.size() == 1, "six walls in one undo step", label);
			double total = 0;
			bool outside = true;
			for (const auto& wall : document.brushes) {
				total += volume(wall);
				// Make Room walls lie outside the original brush.
				const bool overlapsInside = wall.mins.x < 64 - 1e-6 && wall.maxs.x > -64 + 1e-6 && wall.mins.y < 64 - 1e-6 && wall.maxs.y > -64 + 1e-6
					&& wall.mins.z < 64 - 1e-6 && wall.maxs.z > -64 + 1e-6;
				outside &= !overlapsInside;
			}
			if (mode == LevelMapHollowMode::Touch) {
				expect(near(total, outer - inner, 1e-3), "touching walls fill the shell exactly once", QString::number(total));
			} else if (mode == LevelMapHollowMode::Overlap) {
				expect(total > outer - inner + 1, "overlapping walls overlap at the edges");
			} else {
				expect(outside, "Make Room builds its walls outside the brush");
				expect(total > grown - outer + 1, "Make Room's walls fill the corners, overlapping there");
				double low = 1e9, high = -1e9;
				for (const auto& wall : document.brushes) {
					low = std::min(low, wall.mins.x);
					high = std::max(high, wall.maxs.x);
				}
				expect(near(low, -80) && near(high, 80), "Make Room's shell is the brush grown by the thickness");
			}
			const LevelMapDocument back = reloaded(document);
			expect(back.brushes.size() == 6, "the walls survive a reload", label);
			undoLevelMapEdit(&document, &error);
			expect(bytesOf(document) == original, "undo brings the brush back exactly", label);
		}
	}
	// Make Room needs no room inside the brush; the inward modes do.
	LevelMapDocument thin = mapWith(QStringLiteral("quake"), {{{0, 0, 0, true}, {64, 64, 8, true}}});
	setLevelMapSelection(&thin, {{LevelMapSelectionKind::QuakeBrush, thin.brushes.first().id}});
	LevelMapDocument copy = thin;
	expect(!hollowLevelMapSelection(&copy, 16, LevelMapHollowMode::Touch, nullptr, &error), "a thin brush cannot hollow inwards");
	expect(hollowLevelMapSelection(&thin, 16, LevelMapHollowMode::Room, nullptr, &error), "a thin brush can make a room", error);
	LevelMapHollowMode parsed = LevelMapHollowMode::Overlap;
	expect(parseLevelMapHollowMode(QStringLiteral("ROOM"), &parsed) && parsed == LevelMapHollowMode::Room && !parseLevelMapHollowMode(QStringLiteral("x"), &parsed),
		"hollow modes parse");
}

void checkSnapVertices()
{
	QString error;
	LevelMapDocument document = mapWith(QStringLiteral("quake"), {{{0.25, 0, 0, true}, {64.5, 64, 63.75, true}}, {{128, 0, 0, true}, {192, 64, 64, true}},
														  {{256, 0, 0, true}, {258, 2, 2, true}}});
	const QByteArray original = bytesOf(document);
	int snapped = 0;
	QStringList skipped;
	QVector<int> ids;
	for (const auto& brush : document.brushes) {
		ids << brush.id;
	}
	expect(snapLevelMapBrushVertices(&document, ids, 8, &snapped, &skipped, &error), "snap vertices", error);
	expect(snapped == 1, "only the brush off the grid changes", QString::number(snapped));
	expect(skipped.size() == 1 && skipped.first().contains(QString::number(ids.at(2))), "a brush the grid would collapse is left with its reason",
		skipped.join(QLatin1Char('|')));
	expect(document.undoStack.size() == 1, "snapping is one undo step");
	const auto& brush = document.brushes.first();
	expect(near(brush.mins.x, 0) && near(brush.maxs.x, 64) && near(brush.maxs.z, 64), "the vertices land on the grid");
	expect(reloaded(document).brushes.size() == 3, "snapped brushes reload");
	undoLevelMapEdit(&document, &error);
	expect(bytesOf(document) == original, "undo restores the brushes exactly");
	LevelMapDocument clean = mapWith(QStringLiteral("quake"), {{{0, 0, 0, true}, {64, 64, 64, true}}});
	expect(snapLevelMapBrushVertices(&clean, {clean.brushes.first().id}, 8, &snapped, &skipped, &error) && snapped == 0 && clean.undoStack.isEmpty(),
		"a brush already on the grid adds no undo step", error);
}

void checkComponentMoves()
{
	QString error;
	LevelMapDocument document = mapWith(QStringLiteral("quake3"), {{{0, 0, 0, true}, {64, 64, 64, true}}});
	LevelPatchCreateRequest request;
	request.center = {200, 0, 0, true};
	LevelMapPatch patch;
	createLevelPatch(request, &patch, &error);
	addLevelMapPatch(&document, patch, nullptr, &error);
	document = reloaded(document);
	const QByteArray original = bytesOf(document);
	LevelBrushTopology topology;
	expect(levelBrushTopology(document.brushes.first(), &topology, &error), "box topology", error);
	LevelMapComponentSet top;
	top.object = {LevelMapSelectionKind::QuakeBrush, document.brushes.first().id};
	for (int i = 0; i < topology.vertices.size(); ++i) {
		if (near(topology.vertices.at(i).z, 64)) {
			top.components << i;
		}
	}
	LevelMapComponentSet middle;
	middle.object = {LevelMapSelectionKind::QuakePatch, document.patches.first().id};
	middle.components = {4};
	LevelMapComponentReport report;
	expect(top.components.size() == 4, "a box has four top vertices");
	expect(moveLevelMapComponents(&document, {top, middle}, {0, 0, 16, true}, false, &report, &error), "move a brush's and a patch's components", error);
	expect(report.brushes == 1 && report.patches == 1 && document.undoStack.size() == 1, "both objects change in one undo step");
	expect(near(document.brushes.first().maxs.z, 80), "the brush grows to the moved vertices");
	expect(near(document.patches.first().controlPoints.at(4).z, 16), "the patch's control point moves");
	const LevelMapDocument back = reloaded(document);
	expect(back.brushes.size() == 1 && near(back.brushes.first().maxs.z, 80), "the moved components survive a reload");
	undoLevelMapEdit(&document, &error);
	expect(bytesOf(document) == original, "undo puts every component back exactly");
	// Pulling two corners onto two others would merge vertices.
	LevelMapComponentSet edge;
	edge.object = top.object;
	for (int i = 0; i < topology.vertices.size(); ++i) {
		if (near(topology.vertices.at(i).z, 64) && near(topology.vertices.at(i).x, 64)) {
			edge.components << i;
		}
	}
	expect(!moveLevelMapComponents(&document, {edge}, {-64, 0, 0, true}, false, &report, &error), "a collapsing move needs permission");
	expect(bytesOf(document) == original, "a refused move leaves the map alone");
	expect(moveLevelMapComponents(&document, {edge}, {-64, 0, 0, true}, true, &report, &error) && report.collapsedVertices > 0,
		"a collapsing move with permission makes a wedge", error);
}

void checkSelectionTools()
{
	QString error;
	LevelMapDocument document = mapWith(QStringLiteral("quake"), {{{0, 0, 0, true}, {64, 64, 64, true}}, {{128, 0, 0, true}, {192, 64, 64, true}},
														  {{256, 0, 0, true}, {320, 64, 64, true}}});
	setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, document.brushes.at(2).id}});
	int entity = -1;
	expect(tieLevelMapSelectionToEntity(&document, QStringLiteral("func_door"), {}, &entity, &error), "make a door", error);
	document = reloaded(document);
	const int door = document.brushes.last().entityId;
	setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, document.brushes.first().id},
										{LevelMapSelectionKind::QuakeBrush, document.brushes.last().id}});
	const auto whole = levelMapSelectionWholeEntities(document);
	expect(whole.size() == 2 && whole.at(0).kind == LevelMapSelectionKind::QuakeBrush && whole.at(1).kind == LevelMapSelectionKind::Entity
			&& whole.at(1).objectId == door,
		"expanding takes a brush entity's brush to its entity and keeps world brushes");
	LevelMapSelectionRef ref;
	expect(levelMapObjectAtNumber(document, 0, 1, &ref, &error) && ref.kind == LevelMapSelectionKind::QuakeBrush
			&& ref.objectId == document.brushes.at(1).id,
		"entity 0, brush 1 is the second world brush", error);
	expect(levelMapObjectAtNumber(document, 1, 0, &ref, &error) && ref.objectId == document.brushes.last().id, "entity 1, brush 0 is the door's brush",
		error);
	expect(levelMapObjectAtNumber(document, 1, -1, &ref, &error) && ref.kind == LevelMapSelectionKind::Entity && ref.objectId == door,
		"an entity number alone names the entity", error);
	expect(!levelMapObjectAtNumber(document, 0, 9, &ref, &error) && !error.isEmpty(), "a brush number past the end is refused");
	int primitive = -1;
	expect(levelMapObjectNumber(document, {LevelMapSelectionKind::QuakeBrush, document.brushes.at(1).id}, &entity, &primitive) && entity == 0
			&& primitive == 1,
		"numbers round-trip");
	expect(parseLevelMapObjectNumber(QStringLiteral("WARNING: Entity 0, Brush 12: duplicate plane"), &entity, &primitive) && entity == 0 && primitive == 12,
		"q3map2 messages give their numbers");
	expect(parseLevelMapObjectNumber(QStringLiteral("entity 3 brush 7 has no faces"), &entity, &primitive) && entity == 3 && primitive == 7,
		"lower-case messages give their numbers");
	expect(parseLevelMapObjectNumber(QStringLiteral("Entity 5 (func_door) has no brushes"), &entity, &primitive) && entity == 5 && primitive == -1,
		"an entity on its own gives its number");
	expect(!parseLevelMapObjectNumber(QStringLiteral("leaked at 0 0 0"), &entity, &primitive), "lines without numbers give none");
}

} // namespace

void checkPrefabReplace()
{
	QString error;
	LevelMapDocument piece = mapWith(QStringLiteral("quake"), {{{-16, -16, 0, true}, {16, 16, 32, true}}});
	setLevelMapSelection(&piece, {{LevelMapSelectionKind::QuakeBrush, piece.brushes.first().id}});
	LevelPrefab prefab;
	LevelPrefabCreateRequest request;
	request.name = QStringLiteral("Crate");
	expect(createLevelPrefab(piece, request, &prefab, nullptr, &error), "capture a one-brush prefab", error);
	LevelMapDocument document = mapWith(QStringLiteral("quake"), {{{0, 0, 0, true}, {64, 64, 64, true}}, {{128, 0, 0, true}, {192, 64, 16, true}}});
	const QByteArray original = bytesOf(document);
	LevelPrefabReport report;
	LevelMapDocument unselected = document;
	expect(!replaceLevelMapSelectionWithPrefab(&unselected, prefab, {}, &report, &error) && !error.isEmpty() && bytesOf(unselected) == original,
		"with nothing selected nothing is replaced");
	setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, document.brushes.at(1).id}});
	if (!expect(replaceLevelMapSelectionWithPrefab(&document, prefab, {}, &report, &error), "replace a brush with the prefab", error)) {
		return;
	}
	expect(document.brushes.size() == 2 && document.undoStack.size() == 1 && report.inserted.size() == 1 && document.selection == report.inserted,
		"the replacement is one undo step that selects the prefab");
	const LevelMapBrush* placed = nullptr;
	bool replacedGone = true;
	for (const auto& brush : document.brushes) {
		// New brushes may take a freed id; the replaced one is known by its bounds.
		replacedGone &= !(near(brush.mins.x, 128) && near(brush.maxs.x, 192));
		if (!report.inserted.isEmpty() && brush.id == report.inserted.first().objectId) {
			placed = &brush;
		}
	}
	expect(replacedGone && placed && near((placed->mins.x + placed->maxs.x) / 2, 160) && near((placed->mins.y + placed->maxs.y) / 2, 32)
			&& near(placed->mins.z, 0) && near(placed->maxs.z, 32),
		"the prefab's anchor lands on the replaced brush's bottom centre",
		placed ? QStringLiteral("%1 %2 %3 / %4 %5 %6").arg(placed->mins.x).arg(placed->mins.y).arg(placed->mins.z).arg(placed->maxs.x).arg(placed->maxs.y).arg(placed->maxs.z)
			   : QStringLiteral("none"));
	expect(reloaded(document).brushes.size() == 2, "the replacement survives a reload");
	undoLevelMapEdit(&document, &error);
	expect(bytesOf(document) == original, "undo puts the replaced brush back exactly");
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	checkHollowModes();
	checkSnapVertices();
	checkComponentMoves();
	checkSelectionTools();
	checkPrefabReplace();
	if (failures > 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "level brush tools smoke test passed\n";
	return 0;
}
