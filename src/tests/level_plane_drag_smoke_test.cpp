// NetRadiant Custom's drag-tool gestures as core services and their CLI route
// (core/level_plane_drag.h, map drag-planes): the QE4 pick of the planes
// facing a press beside the selection (with planes back to back on other
// selected brushes and both planes of a corner), a sloped face moving along
// its own normal, the per-axis search that stops a drag before a face
// vanishes, the camera's one plane under the pointer, the Alt vertex pick in
// a 2D view and its fallback to the corners of the plane under the pointer,
// Ctrl+Alt extrusion out into a new brush and in to split a slab off; one
// undo step each; refusal of hidden, locked, unselected and out-of-group
// brushes and of Doom maps without touching the document; the Build tool's
// copy chain and its Repeat Last Commands record; and the CLI writing exactly
// what the core writes, with usage errors and refusals that write nothing.
#include "core/level_document.h"
#include "core/level_map.h"
#include "core/level_plane_drag.h"
#include "core/level_primitive.h"
#include "core/level_repeat.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <iostream>

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
bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }
bool at(const LevelMapVec3& v, double x, double y, double z, double tolerance = 1e-6)
{
	return v.valid && near(v.x, x, tolerance) && near(v.y, y, tolerance) && near(v.z, z, tolerance);
}
QString text(const LevelMapVec3& v) { return QStringLiteral("%1 %2 %3").arg(v.x).arg(v.y).arg(v.z); }
LevelMapSelectionRef brush(int id) { return {LevelMapSelectionKind::QuakeBrush, id}; }
const LevelMapBrush* find(const LevelMapDocument& map, int id)
{
	for (const auto& b : map.brushes) {
		if (b.id == id) { return &b; }
	}
	return nullptr;
}
// The solved box of brush `id`.
bool box(const LevelMapDocument& map, int id, LevelMapVec3* mins, LevelMapVec3* maxs)
{
	const LevelMapBrush* b = find(map, id);
	LevelBrushTopology topology;
	if (!b || !levelBrushTopology(*b, &topology)) { return false; }
	*mins = topology.geometry.mins;
	*maxs = topology.geometry.maxs;
	return true;
}
bool boxIs(const LevelMapDocument& map, int id, const LevelMapVec3& low, const LevelMapVec3& high, QString* detail)
{
	LevelMapVec3 mins, maxs;
	const bool found = box(map, id, &mins, &maxs);
	*detail = found ? text(mins) + QStringLiteral(" .. ") + text(maxs) : QStringLiteral("no brush %1").arg(id);
	return found && at(mins, low.x, low.y, low.z) && at(maxs, high.x, high.y, high.z);
}
// The face of brush `id` whose outward normal is (x, y, z).
int face(const LevelMapDocument& map, int id, double x, double y, double z)
{
	const LevelMapBrush* b = find(map, id);
	LevelBrushTopology topology;
	if (!b || !levelBrushTopology(*b, &topology)) { return -1; }
	for (int f = 0; f < topology.geometry.faces.size(); ++f) {
		const auto& plane = topology.geometry.faces[f].plane;
		if (near(plane.normalX, x, 1e-4) && near(plane.normalY, y, 1e-4) && near(plane.normalZ, z, 1e-4)) { return f; }
	}
	return -1;
}
bool picked(const LevelPlaneDragPick& pick, int id, const QVector<int>& faces)
{
	for (const auto& set : pick.sets) {
		if (set.object.objectId != id) { continue; }
		auto mine = set.components, wanted = faces;
		std::sort(mine.begin(), mine.end());
		std::sort(wanted.begin(), wanted.end());
		return mine == wanted;
	}
	return faces.isEmpty();
}
LevelPlaneDragProbe top(double x, double y)
{
	LevelPlaneDragProbe probe;
	probe.origin = {x, y, 0, true};
	probe.direction = {0, 0, -1, true};
	probe.forward = probe.direction;
	probe.tolerance = 2;
	return probe;
}

// Brushes 0 (0..32 x 0..16 x 0..16) and 1 (32..64 x 0..16 x 0..16), touching
// at x = 32; a wedge, brush 2, over 0..32 x 32..64 whose top slopes from
// z = 32 at x = 0 down to x = 32; an info_player_start. A Quake map.
struct Fixture {
	LevelMapDocument map;
	int wedge = -1, entity = -1;
};
Fixture fixture()
{
	Fixture result;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake");
	create.starterRoom = false;
	QString error;
	QVector<int> hulls;
	expect(createLevelMap(create, &result.map, &error) && addLevelMapBoxBrush(&result.map, {0, 0, 0, true}, {32, 16, 16, true}, "studio/checker", nullptr, &error)
			&& addLevelMapBoxBrush(&result.map, {32, 0, 0, true}, {64, 16, 16, true}, "studio/checker", nullptr, &error)
			&& addLevelMapBrushHulls(&result.map,
				{{{0, 32, 0, true}, {32, 32, 0, true}, {0, 64, 0, true}, {32, 64, 0, true}, {0, 32, 32, true}, {0, 64, 32, true}}}, "studio/checker",
				QStringLiteral("wedge"), QStringLiteral("wedge"), &hulls, &error)
			&& addLevelMapEntity(&result.map, QStringLiteral("info_player_start"), {-40, 8, 24, true}, {}, &result.entity, &error),
		"fixture", error);
	result.wedge = hulls.value(0, -1);
	setLevelMapSelection(&result.map, {});
	result.map.undoStack.clear();
	return result;
}

void kinds()
{
	for (const auto kind : {LevelPlaneDragKind::FacingPlanes, LevelPlaneDragKind::Plane, LevelPlaneDragKind::Vertices, LevelPlaneDragKind::Extrude}) {
		LevelPlaneDragKind back = LevelPlaneDragKind::Plane;
		expect(levelPlaneDragKindFromId(levelPlaneDragKindId(kind), &back) && back == kind, "kind ids round-trip", levelPlaneDragKindId(kind));
	}
	expect(!levelPlaneDragKindFromId(QStringLiteral("sideways"), nullptr), "unknown kind refused");

	// The Build tool's chain: half a box length rounds up, the sign follows
	// the drag, a flat box steps by the grid.
	const LevelMapVec3 low {0, 0, 0, true}, high {32, 16, 16, true};
	auto chain = levelCloneChain(low, high, {40, 5, 0, true}, 16);
	expect(chain.axis == 0 && chain.copies == 1 && at(chain.step, 32, 0, 0), "one box length makes one copy", text(chain.step));
	chain = levelCloneChain(low, high, {-80, 5, 0, true}, 16);
	expect(chain.axis == 0 && chain.copies == 3 && at(chain.step, -32, 0, 0), "two and a half lengths make three copies the other way", text(chain.step));
	chain = levelCloneChain(low, high, {0, 7, 0, true}, 16);
	expect(chain.copies == 0, "less than half a length makes none");
	chain = levelCloneChain({8, 8, 8, true}, {8, 8, 8, true}, {0, 0, 40, true}, 16);
	expect(chain.axis == 2 && chain.copies == 3 && at(chain.step, 0, 0, 16), "a point steps by the grid", text(chain.step));
	chain = levelCloneChain(low, high, {4000, 0, 0, true}, 16, 64);
	expect(chain.copies == 64, "at most the maximum");
}

void facingPlanes()
{
	QString error, detail;
	// Beside the right of brush 0 in the Top view: its +X plane.
	auto data = fixture();
	setLevelMapSelection(&data.map, {brush(0)});
	const int plusX = face(data.map, 0, 1, 0, 0), plusY = face(data.map, 0, 0, 1, 0), minusX = face(data.map, 0, -1, 0, 0);
	auto pick = pickLevelPlaneDrag(data.map, data.map.selection, LevelPlaneDragKind::FacingPlanes, top(48, 8));
	expect(pick.valid() && pick.planes == 1 && picked(pick, 0, {plusX}) && pick.brushes.size() == 1, "the plane facing the press", pick.reason);
	const auto before = serializeLevelMap(data.map).bytes;
	LevelPlaneDragRequest request;
	request.sets = pick.sets;
	request.translation = {16, 8, 0, true};
	request.grid = 16;
	QVector<QPair<LevelMapVec3, LevelMapVec3>> edges;
	LevelPlaneDragReport preview;
	expect(previewLevelPlaneDrag(pick.brushes, request, &edges, &preview, &error) && edges.size() == 12 && preview.planes == 1 && preview.clampedBrushes == 0
			&& serializeLevelMap(data.map).bytes == before,
		"the preview edits nothing", error);
	LevelPlaneDragReport report;
	expect(applyLevelPlaneDrag(&data.map, request, &report, &error) && report.brushes == 1 && report.planes == 1 && data.map.undoStack.size() == 1,
		"the drag is one undo step", error);
	expect(boxIs(data.map, 0, {0, 0, 0, true}, {48, 16, 16, true}, &detail), "only the plane's normal share of the drag counts", detail);
	expect(undoLevelMapEdit(&data.map, &error) && serializeLevelMap(data.map).bytes == before, "undo restores the map exactly", error);

	// A corner press takes both planes facing it.
	pick = pickLevelPlaneDrag(data.map, data.map.selection, LevelPlaneDragKind::FacingPlanes, top(48, 32));
	expect(picked(pick, 0, {plusX, plusY}) && pick.planes == 2, "a corner takes both planes");
	request.sets = pick.sets;
	request.translation = {16, 16, 0, true};
	expect(applyLevelPlaneDrag(&data.map, request, &report, &error) && boxIs(data.map, 0, {0, 0, 0, true}, {48, 32, 16, true}, &detail), "both sides follow",
		error + detail);
	undoLevelMapEdit(&data.map, &error);

	// With both brushes selected, brush 1's plane back to back with brush
	// 0's +X plane goes too, so they stay touching.
	setLevelMapSelection(&data.map, {brush(0), brush(1)});
	pick = pickLevelPlaneDrag(data.map, data.map.selection, LevelPlaneDragKind::FacingPlanes, top(40, 8));
	const int oneMinusX = face(data.map, 1, -1, 0, 0);
	expect(picked(pick, 0, {plusX}) && picked(pick, 1, {oneMinusX}) && pick.planes == 1, "the plane back to back moves too");
	request.sets = pick.sets;
	request.translation = {16, 0, 0, true};
	expect(applyLevelPlaneDrag(&data.map, request, &report, &error) && report.brushes == 2 && data.map.undoStack.size() == 1
			&& boxIs(data.map, 0, {0, 0, 0, true}, {48, 16, 16, true}, &detail) && boxIs(data.map, 1, {48, 0, 0, true}, {64, 16, 16, true}, &detail),
		"the shared wall moves as one", error + detail);
	undoLevelMapEdit(&data.map, &error);

	// A sloped face moves along its own normal: the wedge grows, not stretches.
	setLevelMapSelection(&data.map, {brush(data.wedge)});
	pick = pickLevelPlaneDrag(data.map, data.map.selection, LevelPlaneDragKind::FacingPlanes, top(40, 48));
	const int slope = face(data.map, data.wedge, std::sqrt(0.5), 0, std::sqrt(0.5));
	expect(slope >= 0 && picked(pick, data.wedge, {slope}), "the sloped plane faces the press", pick.reason);
	request.sets = pick.sets;
	request.translation = {16, 0, 0, true};
	expect(applyLevelPlaneDrag(&data.map, request, &report, &error) && boxIs(data.map, data.wedge, {0, 32, 0, true}, {48, 64, 48, true}, &detail),
		"the slope moves out along its normal", error + detail);
	undoLevelMapEdit(&data.map, &error);

	// Pulled through the opposite side, the plane stops a grid step short.
	setLevelMapSelection(&data.map, {brush(0)});
	pick = pickLevelPlaneDrag(data.map, data.map.selection, LevelPlaneDragKind::FacingPlanes, top(-16, 8));
	expect(picked(pick, 0, {minusX}), "the -X plane faces a press on the left");
	request.sets = pick.sets;
	request.translation = {48, 0, 0, true};
	expect(previewLevelPlaneDrag(pick.brushes, request, nullptr, &preview, &error) && preview.clampedBrushes == 1, "the preview says it stops short", error);
	expect(applyLevelPlaneDrag(&data.map, request, &report, &error) && report.clampedBrushes == 1
			&& boxIs(data.map, 0, {16, 0, 0, true}, {32, 16, 16, true}, &detail),
		"the drag stops before the brush turns inside out", error + detail);
	undoLevelMapEdit(&data.map, &error);
	LevelMapBrush moved;
	LevelMapVec3 applied;
	expect(moveLevelBrushPlanes(*find(data.map, 0), {minusX}, {64, 0, 0, true}, 0, &moved, &applied, &error) && applied.x > 0 && applied.x < 32,
		"without a grid the search takes 64 steps", text(applied));

	// Nothing faces a press inside the brush.
	pick = pickLevelPlaneDrag(data.map, data.map.selection, LevelPlaneDragKind::FacingPlanes, top(16, 8));
	expect(!pick.valid() && !pick.reason.isEmpty(), "inside, no plane faces the press", pick.reason);
	// A point entity has no planes.
	setLevelMapSelection(&data.map, {{LevelMapSelectionKind::Entity, data.entity}});
	pick = pickLevelPlaneDrag(data.map, data.map.selection, LevelPlaneDragKind::FacingPlanes, top(0, 8));
	expect(!pick.valid() && !pick.reason.isEmpty(), "a point entity has no planes", pick.reason);
}

void planeAndVertices()
{
	QString error, detail;
	auto data = fixture();
	// The camera, looking down on brush 0's top: that plane, and brush 1's top
	// on the same plane.
	setLevelMapSelection(&data.map, {brush(0), brush(1)});
	LevelPlaneDragProbe camera;
	camera.origin = {16, 8, 100, true};
	camera.direction = {0, 0, -1, true};
	camera.forward = {0, 0, -1, true};
	camera.camera = true;
	auto pick = pickLevelPlaneDrag(data.map, data.map.selection, LevelPlaneDragKind::Plane, camera);
	expect(pick.valid() && at(pick.normal, 0, 0, 1) && at(pick.anchor, 16, 8, 16) && picked(pick, 0, {face(data.map, 0, 0, 0, 1)})
			&& picked(pick, 1, {face(data.map, 1, 0, 0, 1)}),
		"the camera's plane under the pointer", text(pick.anchor));
	LevelPlaneDragRequest request;
	request.kind = LevelPlaneDragKind::Plane;
	request.sets = pick.sets;
	request.translation = {0, 0, 8, true};
	LevelPlaneDragReport report;
	expect(applyLevelPlaneDrag(&data.map, request, &report, &error) && boxIs(data.map, 0, {0, 0, 0, true}, {32, 16, 24, true}, &detail)
			&& boxIs(data.map, 1, {32, 0, 0, true}, {64, 16, 24, true}, &detail) && data.map.undoStack.size() == 1,
		"both tops rise in one step", error + detail);
	undoLevelMapEdit(&data.map, &error);
	// Behind the camera nothing is hit.
	camera.direction = {0, 0, 1, true};
	expect(!pickLevelPlaneDrag(data.map, data.map.selection, LevelPlaneDragKind::Plane, camera).valid(), "nothing behind the camera");

	// Alt in the Top view: the vertices under the pointer, every one stacked
	// there; else the corners of the plane under the pointer.
	setLevelMapSelection(&data.map, {brush(0)});
	pick = pickLevelPlaneDrag(data.map, data.map.selection, LevelPlaneDragKind::Vertices, top(32.5, 16.5));
	expect(pick.valid() && pick.sets.size() == 1 && pick.sets[0].kind == LevelBrushComponent::Vertex && pick.sets[0].components.size() == 2 && pick.planes == 0,
		"the two stacked vertices under the pointer", pick.reason);
	request.kind = LevelPlaneDragKind::Vertices;
	request.sets = pick.sets;
	request.translation = {16, 0, 0, true};
	expect(applyLevelPlaneDrag(&data.map, request, &report, &error) && report.vertices == 2 && boxIs(data.map, 0, {0, 0, 0, true}, {48, 16, 16, true}, &detail)
			&& data.map.undoStack.size() == 1,
		"the corner moves", error + detail);
	undoLevelMapEdit(&data.map, &error);
	pick = pickLevelPlaneDrag(data.map, data.map.selection, LevelPlaneDragKind::Vertices, top(40, 8));
	expect(pick.valid() && pick.sets.size() == 1 && pick.sets[0].components.size() == 4 && at(pick.normal, 1, 0, 0),
		"away from any vertex, the corners of the plane behind the nearest edge", pick.reason);
	request.sets = pick.sets;
	expect(applyLevelPlaneDrag(&data.map, request, &report, &error) && boxIs(data.map, 0, {0, 0, 0, true}, {48, 16, 16, true}, &detail), "the face's corners move",
		error + detail);
	undoLevelMapEdit(&data.map, &error);
}

void extrusion()
{
	QString error, detail;
	auto data = fixture();
	setLevelMapSelection(&data.map, {brush(0)});
	const auto before = serializeLevelMap(data.map).bytes;
	// Beside the +X side in the Top view, the side seen edge-on.
	auto pick = pickLevelPlaneDrag(data.map, data.map.selection, LevelPlaneDragKind::Extrude, top(40, 8));
	expect(pick.valid() && at(pick.normal, 1, 0, 0) && picked(pick, 0, {face(data.map, 0, 1, 0, 0)}) && !pick.corners.isEmpty(), "the plane behind the nearest edge",
		pick.reason);
	LevelPlaneDragRequest request;
	request.kind = LevelPlaneDragKind::Extrude;
	request.sets = pick.sets;
	request.normal = pick.normal;
	request.distance = 16;
	const int count = static_cast<int>(data.map.brushes.size());
	LevelPlaneDragReport report;
	expect(applyLevelPlaneDrag(&data.map, request, &report, &error) && report.createdBrushes == 1 && data.map.brushes.size() == count + 1
			&& data.map.undoStack.size() == 1 && boxIs(data.map, 0, {0, 0, 0, true}, {32, 16, 16, true}, &detail),
		"a pull makes a new brush and keeps the old", error + detail);
	const int created = data.map.brushes.last().id;
	expect(boxIs(data.map, created, {32, 0, 0, true}, {48, 16, 16, true}, &detail), "the new brush spans the pull", detail);
	expect(undoLevelMapEdit(&data.map, &error) && serializeLevelMap(data.map).bytes == before, "extrusion undoes exactly", error);
	request.distance = -8;
	expect(applyLevelPlaneDrag(&data.map, request, &report, &error) && report.createdBrushes == 1 && boxIs(data.map, 0, {0, 0, 0, true}, {24, 16, 16, true}, &detail),
		"a push splits a slab off", error + detail);
	expect(boxIs(data.map, data.map.brushes.last().id, {24, 0, 0, true}, {32, 16, 16, true}, &detail), "the slab is the pushed part", detail);
	undoLevelMapEdit(&data.map, &error);
	// The Top view cannot extrude the face it looks at: the pick passes it by.
	pick = pickLevelPlaneDrag(data.map, data.map.selection, LevelPlaneDragKind::Extrude, top(16, 8));
	expect(!pick.valid() || !at(pick.normal, 0, 0, 1), "a face seen flat is not taken", text(pick.normal));
}

void refusals()
{
	QString error;
	auto data = fixture();
	setLevelMapSelection(&data.map, {brush(0)});
	LevelPlaneDragRequest request;
	request.sets = {{brush(1), LevelBrushComponent::Face, {face(data.map, 1, 1, 0, 0)}}};
	request.translation = {16, 0, 0, true};
	auto bytes = serializeLevelMap(data.map).bytes;
	LevelPlaneDragReport report;
	expect(!applyLevelPlaneDrag(&data.map, request, &report, &error) && !error.isEmpty() && serializeLevelMap(data.map).bytes == bytes
			&& data.map.undoStack.isEmpty(),
		"an unselected brush is refused untouched", error);
	setLevelMapSelection(&data.map, {brush(0), brush(1)});
	data.map.hiddenObjects = {QStringLiteral("brush:1")};
	expect(levelPlaneDragBrushIds(data.map, data.map.selection) == QVector<int> {0}, "hidden brushes are not dragged");
	bytes = serializeLevelMap(data.map).bytes;
	expect(!applyLevelPlaneDrag(&data.map, request, &report, &error) && serializeLevelMap(data.map).bytes == bytes, "a hidden brush is refused untouched", error);
	data = fixture();
	QString layer;
	expect(createLevelSceneNode(&data.map, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
			&& assignLevelSceneObjects(&data.map, layer, {QStringLiteral("brush:1")}, &error) && setLevelSceneLocked(&data.map, layer, true, &error),
		"lock fixture", error);
	data.map.selection = {brush(0), brush(1)};
	request.sets = {{brush(0), LevelBrushComponent::Face, {face(data.map, 0, 1, 0, 0)}}, {brush(1), LevelBrushComponent::Face, {face(data.map, 1, -1, 0, 0)}}};
	bytes = serializeLevelMap(data.map).bytes;
	const auto depth = data.map.undoStack.size();
	expect(!applyLevelPlaneDrag(&data.map, request, &report, &error) && serializeLevelMap(data.map).bytes == bytes && data.map.undoStack.size() == depth,
		"a locked brush refuses the whole drag", error);
	data = fixture();
	QString group;
	expect(createLevelSceneNode(&data.map, LevelSceneNodeKind::Group, QStringLiteral("Room"), {}, &group, &error)
			&& assignLevelSceneObjects(&data.map, group, {QStringLiteral("brush:0")}, &error) && openLevelSceneGroup(&data.map, group, &error),
		"group fixture", error);
	data.map.selection = {brush(0), brush(1)};
	expect(!applyLevelPlaneDrag(&data.map, request, &report, &error), "a brush outside the open group is refused", error);
	data.map.selection = {brush(0)};
	request.sets = {{brush(0), LevelBrushComponent::Face, {face(data.map, 0, 1, 0, 0)}}};
	expect(applyLevelPlaneDrag(&data.map, request, &report, &error) && report.brushes == 1, "a brush inside the open group drags", error);
	LevelMapDocument doom;
	LevelMapCreateRequest doomRequest;
	doomRequest.game = QStringLiteral("doom");
	expect(createLevelMap(doomRequest, &doom, &error), "Doom fixture", error);
	expect(!applyLevelPlaneDrag(&doom, request, &report, &error) && !pickLevelPlaneDrag(doom, doom.selection, LevelPlaneDragKind::FacingPlanes, top(0, 0)).valid(),
		"Doom maps are refused");
}

void chainRepeat()
{
	QString error;
	auto data = fixture();
	setLevelMapSelection(&data.map, {brush(0)});
	const int count = static_cast<int>(data.map.brushes.size());
	expect(arrayLevelMapSelection(&data.map, {32, 0, 0, true}, 3, {false, false}, &error) && data.map.brushes.size() == count + 3, "the chain's array", error);
	expect(data.map.repeat.actions.isEmpty() || data.map.repeat.clearOnNextAction, "an array alone records nothing to repeat");
	recordLevelCloneChainRepeat(&data.map, {32, 0, 0, true}, 3);
	expect(data.map.repeat.actions.size() == 3 && !data.map.repeat.clearOnNextAction
			&& data.map.repeat.actions[0].kind == LevelRepeatAction::Kind::Duplicate && near(data.map.repeat.actions[2].matrix[3], 32),
		"the chain is recorded as three duplicate steps");
	// Only the last copy stays selected, so Repeat carries the chain on from
	// it: three more, each one length further, none on top of another.
	expect(data.map.selection == QVector<LevelMapSelectionRef> {brush(data.map.brushes.last().id)}, "only the last copy stays selected");
	expect(repeatLevelMapCommands(&data.map, {false, false}, &error) && data.map.brushes.size() == count + 6, "repeat lays three more", error);
	QVector<double> starts;
	for (const auto& b : data.map.brushes) {
		LevelMapVec3 mins, maxs;
		if (b.id != data.wedge && box(data.map, b.id, &mins, &maxs) && near(mins.y, 0) && b.id != 1) { starts << mins.x; }
	}
	std::sort(starts.begin(), starts.end());
	expect(starts == QVector<double> {0, 32, 64, 96, 128, 160, 192}, "the chain runs on one length at a time without stacking",
		QStringLiteral("%1").arg(starts.size()));
	// One copy records itself as a duplicate does; this adds nothing.
	const auto actions = data.map.repeat.actions;
	recordLevelCloneChainRepeat(&data.map, {32, 0, 0, true}, 1);
	expect(data.map.repeat.actions == actions, "a single copy is not recorded twice");
}

void cli(const QString& executable)
{
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "CLI temporary folder")) { return; }
	auto data = fixture();
	const QString path = temp.filePath("drag.map");
	QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(serializeLevelMap(data.map).bytes) > 0, "CLI fixture written")) { return; }
	file.close();
	const auto run = [&](const QStringList& args, int code = 0) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(executable, QStringList {"--cli", "--json", "--settings-file", temp.filePath("settings.ini"), "map", "drag-planes"} + args);
		const bool done = process.waitForStarted() && process.waitForFinished(60000);
		const auto out = process.readAllStandardOutput(), err = process.readAllStandardError();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code, "CLI status", QString::fromUtf8(out + err) + args.join(' '));
		if (!done) {
			process.kill();
			process.waitForFinished();
		}
		return QJsonDocument::fromJson(out).object();
	};
	const auto load = [&](const QString& output) {
		LevelMapDocument result;
		QString problem;
		expect(loadLevelMap({output, {}, {}}, &result, &problem), "CLI output loads", problem);
		return result;
	};
	LevelMapDocument source;
	QString error;
	expect(loadLevelMap({path, {}, {}}, &source, &error), "CLI source loads", error);

	// The QE4 drag beside brush 0, with brush 1 back to back.
	auto report = run({path, "--object", "brush:0", "--object", "brush:1", "--from", "40,8,0", "--view", "top", "--delta", "16,8,0", "--grid", "16", "--output",
		temp.filePath("planes.map")});
	auto expected = source;
	setLevelMapSelection(&expected, {brush(0), brush(1)});
	auto pick = pickLevelPlaneDrag(expected, expected.selection, LevelPlaneDragKind::FacingPlanes, top(40, 8));
	LevelPlaneDragRequest request;
	request.sets = pick.sets;
	request.translation = {16, 8, 0, true};
	request.grid = 16;
	LevelPlaneDragReport core;
	expect(applyLevelPlaneDrag(&expected, request, &core, &error), "core reference", error);
	expect(report["command"].toString() == QStringLiteral("map drag-planes") && report["mode"].toString() == QStringLiteral("facing-planes")
			&& report["planes"].toInt() == 1 && report["brushes"].toInt() == 2 && report["picked"].toArray().size() == 2
			&& report["textureLockPolicy"].toString() == QStringLiteral("world"),
		"CLI report matches the core", QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));
	expect(serializeLevelMap(load(temp.filePath("planes.map"))).bytes == serializeLevelMap(expected).bytes, "CLI plane drag writes what the core writes");

	// Extrusion from a camera ray down onto brush 0's top.
	report = run({path, "--object", "brush:0", "--mode", "extrude", "--from", "16,8,100", "--direction", "0,0,-1", "--camera", "--distance", "16",
		"--texture-lock", "off", "--output", temp.filePath("extrude.map")});
	expected = source;
	setLevelMapSelection(&expected, {brush(0)});
	LevelPlaneDragProbe camera;
	camera.origin = {16, 8, 100, true};
	camera.direction = camera.forward = {0, 0, -1, true};
	camera.camera = true;
	pick = pickLevelPlaneDrag(expected, expected.selection, LevelPlaneDragKind::Extrude, camera);
	request = {};
	request.kind = LevelPlaneDragKind::Extrude;
	request.sets = pick.sets;
	request.normal = pick.normal;
	request.distance = 16;
	expect(applyLevelPlaneDrag(&expected, request, &core, &error), "core extrusion reference", error);
	expect(report["createdBrushes"].toInt() == 1 && report["distance"].toDouble() == 16 && report["mode"].toString() == QStringLiteral("extrude"),
		"CLI extrusion report", QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));
	expect(serializeLevelMap(load(temp.filePath("extrude.map"))).bytes == serializeLevelMap(expected).bytes, "CLI extrusion writes what the core writes");

	// Vertices and a plane by distance.
	report = run({path, "--object", "brush:0", "--mode", "vertices", "--from", "32,16,0", "--delta", "16,0,0", "--output", temp.filePath("vertices.map")});
	expect(report["vertices"].toInt() == 2, "CLI vertex drag", QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));
	report = run({path, "--object", "brush:0", "--mode", "plane", "--from", "16,8,100", "--direction", "0,0,-1", "--camera", "--distance", "8", "--output",
		temp.filePath("plane.map")});
	QString detail;
	expect(boxIs(load(temp.filePath("plane.map")), 0, {0, 0, 0, true}, {32, 16, 24, true}, &detail), "CLI plane by distance", detail);

	// Usage errors exit 2 and refusals 4, writing nothing.
	for (const QStringList& bad : {QStringList {"--object", "brush:0", "--delta", "16,0,0"}, QStringList {"--object", "brush:0", "--from", "0,0,0", "--mode", "sideways", "--delta", "1,0,0"},
			 QStringList {"--object", "brush:0", "--from", "0,0,0", "--delta", "1,0,0", "--distance", "4"},
			 QStringList {"--object", "brush:0", "--from", "0,0,0", "--view", "sideways", "--delta", "1,0,0"},
			 QStringList {"--object", "brush:0", "--from", "0,0,0", "--mode", "extrude", "--delta", "1,0,0"},
			 QStringList {"--from", "48,8,0", "--delta", "16,0,0"}}) {
		run(QStringList {path} + bad + QStringList {"--output", temp.filePath("bad.map")}, 2);
	}
	run({path, "--object", "brush:0", "--from", "16,8,0", "--delta", "16,0,0", "--output", temp.filePath("bad.map")}, 4);
	expect(!QFileInfo::exists(temp.filePath("bad.map")), "usage errors and refusals write nothing");
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	kinds();
	facingPlanes();
	planeAndVertices();
	extrusion();
	refusals();
	chainRepeat();
	if (argc == 2) { cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath()); }
	return ok ? 0 : 1;
}
