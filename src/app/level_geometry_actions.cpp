// Geometry, patch, region and navigation tools of the Levels page, after
// the editors mappers know: vertex, edge and face editing in the 2D views
// (Radiant, TrenchBroom), Snap Vertices (TrenchBroom), hollow with touching
// walls and Make Room (NetRadiant Custom), Expand Selection to Whole
// Entities and Find Brush (Radiant), the patch matrix commands and Thicken
// (GtkRadiant, NetRadiant Custom), region drawing and Region Set Brush
// (Radiant), Go to Coordinates (Hammer) and replacing the selection with a
// prefab. Each edit is one undo step from the core, with a `map` command
// doing the same.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/map_viewport_projection.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_brush.h"
#include "core/level_patch_tools.h"
#include "core/level_prefab.h"
#include "core/level_selection_tools.h"
#include "core/package_staging.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QDir>
#include <QFileInfo>
#include <QSpinBox>
#include <QStatusBar>

#include <algorithm>
#include <cmath>

namespace vibestudio {

namespace {

struct PatchMatrixCommand {
	const char* id;
	LevelPatchMatrixOperation operation;
	const char* label;
	const char* tip;
};

const QList<PatchMatrixCommand>& patchMatrixCommands()
{
	static const QList<PatchMatrixCommand> commands {
		{"map.patchInsertColumns", LevelPatchMatrixOperation::InsertColumns, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Insert Patch Columns"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Split the first span of each row of the selected patches in two, adding two control columns without changing the surface.")},
		{"map.patchAddColumns", LevelPatchMatrixOperation::AddColumns, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Add Patch Columns"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Split the last span of each row of the selected patches in two, adding two control columns without changing the surface.")},
		{"map.patchInsertRows", LevelPatchMatrixOperation::InsertRows, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Insert Patch Rows"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Split the first span of each column of the selected patches in two, adding two control rows without changing the surface.")},
		{"map.patchAddRows", LevelPatchMatrixOperation::AddRows, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Add Patch Rows"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Split the last span of each column of the selected patches in two, adding two control rows without changing the surface.")},
		{"map.patchDeleteFirstColumns", LevelPatchMatrixOperation::DeleteFirstColumns, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Delete First Patch Columns"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Join the first two spans of each row into one through the point they shared, two control columns fewer.")},
		{"map.patchDeleteLastColumns", LevelPatchMatrixOperation::DeleteLastColumns, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Delete Last Patch Columns"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Join the last two spans of each row into one through the point they shared, two control columns fewer.")},
		{"map.patchDeleteFirstRows", LevelPatchMatrixOperation::DeleteFirstRows, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Delete First Patch Rows"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Join the first two spans of each column into one through the point they shared, two control rows fewer.")},
		{"map.patchDeleteLastRows", LevelPatchMatrixOperation::DeleteLastRows, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Delete Last Patch Rows"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Join the last two spans of each column into one through the point they shared, two control rows fewer.")},
		{"map.patchTranspose", LevelPatchMatrixOperation::Transpose, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Transpose Patch"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Swap the selected patches' rows and columns, as Radiant's Matrix Transpose does; they then face the other way.")},
		{"map.patchInvert", LevelPatchMatrixOperation::Invert, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Invert Patch Facing"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Turn the selected patches to face the other way.")},
		{"map.patchRedisperseColumns", LevelPatchMatrixOperation::RedisperseColumns, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Re-disperse Patch Columns"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Put each span's middle control point halfway between its ends along the rows, spacing the patch evenly, as Radiant's Re-disperse does.")},
		{"map.patchRedisperseRows", LevelPatchMatrixOperation::RedisperseRows, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Re-disperse Patch Rows"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Put each span's middle control point halfway between its ends down the columns, spacing the patch evenly.")},
		{"map.patchSmoothColumns", LevelPatchMatrixOperation::SmoothColumns, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Smooth Patch Columns"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Move each joint between spans along the rows halfway between its neighbours, so the spans meet without a crease, as NetRadiant Custom's Smooth does.")},
		{"map.patchSmoothRows", LevelPatchMatrixOperation::SmoothRows, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Smooth Patch Rows"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Move each joint between spans down the columns halfway between its neighbours, so the spans meet without a crease.")},
	};
	return commands;
}

QByteArray regionKey(const QString& path)
{
	// One remembered region per map file, keyed by its path.
	return QCryptographicHash::hash(QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toCaseFolded().toUtf8(), QCryptographicHash::Sha1).toHex();
}

} // namespace

QStringList ApplicationShell::levelPatchMatrixCommandIds()
{
	QStringList ids;
	for (const PatchMatrixCommand& command : patchMatrixCommands()) {
		ids << QString::fromLatin1(command.id);
	}
	return ids;
}

void ApplicationShell::registerLevelGeometryCommands()
{
	const auto add = [this](const QString& id, const QString& label, const QString& tip, const QString& icon, const QString& section,
						 std::function<void()> handler, bool checkable = false) {
		StudioCommandRegistration command;
		command.commandId = id;
		command.group = StudioCommandGroup::Edit;
		command.label = label;
		command.statusTip = tip;
		command.iconName = icon;
		command.menuSection = section;
		command.handler = [this, handler = std::move(handler)]() {
			setMode(StudioMode::Levels);
			handler();
		};
		m_commands->registerCommand(command);
		if (checkable) {
			if (QAction* action = m_commands->action(id)) {
				action->setCheckable(true);
			}
		}
	};
	// Vertex, edge and face editing in the 2D views. Each command turns its
	// mode on, or off when the views are in it already: the views, not the
	// action's check mark, which the page refresh resets on the way in.
	const QString components = tr("Vertex Editing");
	const auto mode = [this](int componentMode) {
		const int current = m_levelMapViewport ? static_cast<int>(m_levelMapViewport->componentMode()) : 0;
		setLevelComponentMode(current == componentMode ? 0 : componentMode);
	};
	add(QStringLiteral("map.vertexTool"), tr("Edit Vertices"),
		tr("Show the selected brushes' vertices and patches' control points as handles in the 2D views: pick and drag them, or nudge them with the "
		   "arrow keys, as Radiant's vertex mode and TrenchBroom's vertex tool do. Escape drops the picks, then leaves the mode."),
		QStringLiteral("vertex-tool"), components, [mode]() { mode(1); }, true);
	add(QStringLiteral("map.edgeTool"), tr("Edit Edges"),
		tr("Show the selected brushes' edges as handles in the 2D views and move them, as Radiant's edge mode does."), QStringLiteral("edge-tool"),
		components, [mode]() { mode(2); }, true);
	add(QStringLiteral("map.faceTool"), tr("Edit Faces"),
		tr("Show the selected brushes' faces as handles in the 2D views and move them, pushing or pulling a face to grow or shrink its brush, as "
		   "Radiant's face mode does."),
		QStringLiteral("face-tool"), components, [mode]() { mode(3); }, true);
	add(QStringLiteral("map.snapVertices"), tr("Snap Vertices to Grid"),
		tr("Round every vertex of the selected brushes onto the grid and rebuild them, as TrenchBroom's Snap Vertices does, to clean up brushes "
		   "left off the grid."),
		QStringLiteral("grid"), components, [this]() { snapLevelMapVerticesFromUi(); });
	const QString geometry = tr("Brush Tools");
	add(QStringLiteral("map.hollowTouching"), tr("Hollow Touching…"),
		tr("Hollow the selected brushes into walls that meet without overlapping, as NetRadiant Custom's wrapped hollow does."),
		QStringLiteral("cube"), geometry, [this]() { hollowLevelMapTouchingFromUi(); });
	add(QStringLiteral("map.makeRoom"), tr("Make Room"),
		tr("Build walls one grid step thick around the outside of each selected brush and remove the brush, so it becomes the room, as "
		   "NetRadiant Custom's Make Room does."),
		QStringLiteral("shape-room"), geometry, [this]() { makeLevelRoomFromUi(); });
	add(QStringLiteral("map.selectWholeEntities"), tr("Expand Selection to Whole Entities"),
		tr("Select the whole brush entity of every selected brush or patch, as Radiant's Expand Selection to Entities does."), QStringLiteral("entity"),
		tr("Select by Region"), [this]() { selectLevelWholeEntitiesFromUi(); });
	// Radiant's Patch menu.
	const QString patches = tr("Patch Matrix");
	for (const PatchMatrixCommand& command : patchMatrixCommands()) {
		const LevelPatchMatrixOperation operation = command.operation;
		add(QString::fromLatin1(command.id), QCoreApplication::translate("VibeStudioLevelGeometry", command.label),
			QCoreApplication::translate("VibeStudioLevelGeometry", command.tip), QStringLiteral("patch-cylinder"), patches,
			[this, operation]() { applyLevelPatchMatrixFromUi(operation); });
	}
	add(QStringLiteral("map.thickenPatch"), tr("Thicken Patch…"),
		tr("Give the selected patches a thickness: a copy behind each, facing away, and walls along its open edges, as NetRadiant Custom's "
		   "Thicken does."),
		QStringLiteral("patch-thicken"), tr("Patches"), [this]() { thickenLevelPatchesFromUi(); });
	// Regions.
	const QString regions = tr("Region");
	add(QStringLiteral("map.regionDraw"), tr("Draw Region"),
		tr("Drag a box in a 2D view to make it the region, through the whole map's depth, as Radiant's Region Set XY does. Escape leaves."),
		QStringLiteral("select-box"), regions, [this]() { setLevelRegionDrawMode(!(m_levelMapViewport && m_levelMapViewport->regionDrawMode())); }, true);
	add(QStringLiteral("map.regionSetBrush"), tr("Set Region to Brush"),
		tr("Make the selected brushes' bounds the region and remove the brushes, as Radiant's Region Set Brush does; Undo brings them back."),
		QStringLiteral("cube"), regions, [this]() { setLevelRegionFromBrushes(); });
	add(QStringLiteral("map.regionBounds"), tr("Region Bounds…"), tr("Type the region's lowest and highest corners."), QStringLiteral("resize"),
		regions, [this]() { editLevelRegionBounds(); });
	add(QStringLiteral("map.regionSelect"), tr("Select Everything in Region"),
		tr("Select every object the region keeps that is not hidden."), QStringLiteral("select-box"), regions, [this]() { selectLevelRegionObjects(); });
	// Navigation.
	const QString navigation = tr("Navigate");
	add(QStringLiteral("map.zoomToFit"), tr("Zoom to Fit"), tr("Frame the whole map in every 2D view."), QStringLiteral("frame"), navigation,
		[this]() { zoomLevelViewsToFit(); });
	add(QStringLiteral("map.centerOnSelection"), tr("Centre Views on Selection"),
		tr("Move every 2D view to the selection's centre without changing its zoom, as Radiant's Center does."), QStringLiteral("crosshair"), navigation,
		[this]() { centreLevelViewsOnSelection(); });
	add(QStringLiteral("map.goToCoordinates"), tr("Go to Coordinates…"),
		tr("Centre the 2D views on a point you type, and take the camera there, as Hammer's Go to Coordinates does."), QStringLiteral("crosshair"),
		navigation, [this]() { goToLevelCoordinatesFromUi(); });
	add(QStringLiteral("map.findObject"), tr("Find Brush by Number…"),
		tr("Select and frame the object compilers name as \"Entity 0, Brush 12\", as Radiant's Find Brush does."), QStringLiteral("search"), navigation,
		[this]() { findLevelObjectByNumberFromUi(); });
	add(QStringLiteral("map.replaceWithPrefab"), tr("Replace Selection with Prefab…"),
		tr("Put a prefab where the selection stands, its anchor at the selection's bottom centre, and remove the selection, as one undo step."),
		QStringLiteral("prefab"), tr("Prefabs"), [this]() { replaceLevelSelectionWithPrefabFromUi(); });
}

void ApplicationShell::refreshLevelGeometryCommands()
{
	if (!m_commands) {
		return;
	}
	const LevelMapDocument& document = m_levelMapDocument;
	const bool textMap = document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
	bool brushes = false;
	bool primitives = false;
	bool owned = false;
	for (const LevelMapSelectionRef& ref : document.selection) {
		brushes = brushes || ref.kind == LevelMapSelectionKind::QuakeBrush || ref.kind == LevelMapSelectionKind::Entity;
		primitives = primitives || ref.kind == LevelMapSelectionKind::QuakeBrush || ref.kind == LevelMapSelectionKind::QuakePatch;
		owned = owned || ref.kind == LevelMapSelectionKind::QuakeBrush || ref.kind == LevelMapSelectionKind::QuakePatch;
	}
	const bool patches = document.format == LevelMapFormat::Quake3Map && !levelMapSelectedPatchIds(document).isEmpty();
	for (const auto& id : {QStringLiteral("map.vertexTool"), QStringLiteral("map.edgeTool"), QStringLiteral("map.faceTool")}) {
		m_commands->setEnabled(id, textMap);
	}
	m_commands->setEnabled(QStringLiteral("map.snapVertices"), textMap && brushes);
	m_commands->setEnabled(QStringLiteral("map.hollowTouching"), textMap && levelMapSelectionClippable());
	m_commands->setEnabled(QStringLiteral("map.makeRoom"), textMap && levelMapSelectionClippable());
	m_commands->setEnabled(QStringLiteral("map.selectWholeEntities"), textMap && owned);
	for (const QString& id : levelPatchMatrixCommandIds()) {
		m_commands->setEnabled(id, patches);
	}
	m_commands->setEnabled(QStringLiteral("map.thickenPatch"), patches);
	m_commands->setEnabled(QStringLiteral("map.regionDraw"), textMap);
	m_commands->setEnabled(QStringLiteral("map.regionSetBrush"), textMap && brushes);
	m_commands->setEnabled(QStringLiteral("map.regionBounds"), textMap);
	m_commands->setEnabled(QStringLiteral("map.regionSelect"), levelRegionActive());
	m_commands->setEnabled(QStringLiteral("map.zoomToFit"), document.format != LevelMapFormat::Unknown);
	m_commands->setEnabled(QStringLiteral("map.centerOnSelection"), !document.selection.isEmpty());
	m_commands->setEnabled(QStringLiteral("map.goToCoordinates"), document.format != LevelMapFormat::Unknown);
	m_commands->setEnabled(QStringLiteral("map.findObject"), textMap);
	m_commands->setEnabled(QStringLiteral("map.replaceWithPrefab"), textMap && (primitives || brushes));
	// The mode commands show the views' mode, whichever way it was left.
	const int componentMode = m_levelMapViewport ? static_cast<int>(m_levelMapViewport->componentMode()) : 0;
	const QStringList modes {QStringLiteral("map.vertexTool"), QStringLiteral("map.edgeTool"), QStringLiteral("map.faceTool")};
	for (int index = 0; index < modes.size(); ++index) {
		if (QAction* action = m_commands->action(modes.at(index))) {
			const QSignalBlocker blocker(action);
			action->setChecked(componentMode == index + 1);
		}
	}
	if (QAction* action = m_commands->action(QStringLiteral("map.regionDraw"))) {
		const QSignalBlocker blocker(action);
		action->setChecked(m_levelMapViewport && m_levelMapViewport->regionDrawMode());
	}
}

// ---------------------------------------------------------------------------
// Vertex editing

void ApplicationShell::setLevelComponentMode(int mode)
{
	const bool textMap = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (mode != 0 && !textMap) {
		statusBar()->showMessage(tr("Open a Quake-family map to edit brush vertices and patch control points."));
		mode = 0;
	}
	if (mode != 0) {
		// The other plan tools give way, and the handles live in the 2D views.
		if (m_levelMapViewport && m_levelMapViewport->clipMode()) {
			setLevelMapClipMode(false);
		}
		setLevelShearMode(false);
		setLevelRegionDrawMode(false);
		if (!levelMap2DShowing()) {
			setLevelMap3D(false);
		}
	}
	for (MapViewport* view : std::as_const(m_levelPlanViews)) {
		const QSignalBlocker blocker(view);
		view->setComponentMode(static_cast<MapViewport::ComponentMode>(mode));
	}
	refreshLevelGeometryCommands();
	refreshLevelToolShelf();
	if (mode != 0) {
		if (m_levelMapViewport) {
			m_levelMapViewport->setFocus(Qt::ShortcutFocusReason);
		}
		const int handles = m_levelMapViewport ? m_levelMapViewport->componentHandleCount() : 0;
		statusBar()->showMessage(handles == 0
				? tr("Vertex editing on: select brushes or patches to show their handles. Escape leaves.")
				: tr("Vertex editing on: %n handle(s). Click or band them, then drag or use the arrow keys; Escape leaves.", nullptr, handles),
			6000);
	}
}

void ApplicationShell::moveLevelComponentsFromViewport(const QVector<LevelMapComponentSet>& sets, const LevelMapVec3& delta)
{
	LevelMapComponentReport report;
	QString error;
	// Dragging a vertex onto another merges them, as in TrenchBroom; Undo
	// brings a merged corner back.
	if (!moveLevelMapComponents(&m_levelMapDocument, sets, delta, true, &report, &error)) {
		statusBar()->showMessage(tr("Could not move the components: %1").arg(error), 6000);
		// The views find their picks where they were.
		for (MapViewport* view : std::as_const(m_levelPlanViews)) {
			view->updateDocument(view->displayDocument());
		}
		refreshLevelMapWorkbench();
		return;
	}
	if (report.brushes + report.patches == 0) {
		return;
	}
	recordActivity(tr("Level map components moved"), tr("%n object(s)", nullptr, report.brushes + report.patches), QStringLiteral("level-map"),
		OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	QString message = tr("Moved the picked components of %n object(s) by %1, %2, %3.", nullptr, report.brushes + report.patches)
						  .arg(delta.x, 0, 'g', 8)
						  .arg(delta.y, 0, 'g', 8)
						  .arg(delta.z, 0, 'g', 8);
	if (report.collapsedVertices > 0) {
		message += QLatin1Char(' ') + tr("%n vertex(es) merged; Undo brings them back.", nullptr, report.collapsedVertices);
	}
	statusBar()->showMessage(message, 5000);
}

void ApplicationShell::shareLevelComponentPicks(MapViewport* from)
{
	if (!from) {
		return;
	}
	const auto picks = from->pickedComponentPositions();
	for (MapViewport* view : std::as_const(m_levelPlanViews)) {
		if (view != from) {
			view->setPickedComponentPositions(picks);
		}
	}
}

void ApplicationShell::snapLevelMapVerticesFromUi()
{
	QSet<int> entities;
	QVector<int> brushes;
	for (const LevelMapSelectionRef& ref : std::as_const(m_levelMapDocument.selection)) {
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			brushes << ref.objectId;
		} else if (ref.kind == LevelMapSelectionKind::Entity) {
			entities.insert(ref.objectId);
		}
	}
	for (const LevelMapBrush& brush : std::as_const(m_levelMapDocument.brushes)) {
		if (entities.contains(brush.entityId) && !brushes.contains(brush.id)) {
			brushes << brush.id;
		}
	}
	const double grid = m_levelMapViewport ? std::max(1, m_levelMapViewport->gridSize()) : 8;
	int snapped = 0;
	QStringList skipped;
	QString error;
	if (!snapLevelMapBrushVertices(&m_levelMapDocument, brushes, grid, &snapped, &skipped, &error)) {
		statusBar()->showMessage(tr("Could not snap the vertices: %1").arg(error), 6000);
		return;
	}
	if (snapped == 0) {
		statusBar()->showMessage(skipped.isEmpty() ? tr("The selected brushes' vertices are already on grid %1.").arg(grid)
												   : tr("No brush changed.") + levelMapSkippedNote(skipped),
			5000);
		return;
	}
	recordActivity(tr("Brush vertices snapped to the grid"), tr("%n brush(es)", nullptr, snapped), QStringLiteral("level-map"),
		OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Snapped the vertices of %n brush(es) to grid %1.", nullptr, snapped).arg(grid) + levelMapSkippedNote(skipped), 5000);
}

void ApplicationShell::hollowLevelMapTouchingFromUi()
{
	if (!levelMapSelectionClippable()) {
		statusBar()->showMessage(tr("Select brushes, or a brush entity, to hollow."));
		return;
	}
	const int grid = m_levelMapViewport ? std::max(1, m_levelMapViewport->gridSize()) : 8;
	bool ok = false;
	const int thickness = QInputDialog::getInt(this, tr("Hollow Touching"), tr("Wall thickness (units)"), grid, 1, 4096, 1, &ok);
	if (!ok) {
		return;
	}
	int hollowed = 0;
	QString error;
	QStringList skipped;
	if (!hollowLevelMapSelection(&m_levelMapDocument, thickness, LevelMapHollowMode::Touch, &hollowed, &error, &skipped)) {
		statusBar()->showMessage(tr("Could not hollow: %1").arg(error));
		return;
	}
	recordActivity(tr("Level map brushes hollowed"), tr("%n wall(s)", nullptr, static_cast<int>(m_levelMapDocument.selection.size())),
		QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(
		tr("Hollowed %n brush(es) into walls %1 units thick that meet without overlapping.", nullptr, hollowed).arg(thickness) + levelMapSkippedNote(skipped));
}

void ApplicationShell::makeLevelRoomFromUi()
{
	if (!levelMapSelectionClippable()) {
		statusBar()->showMessage(tr("Select brushes, or a brush entity, to make rooms of."));
		return;
	}
	const int grid = m_levelMapViewport ? std::max(1, m_levelMapViewport->gridSize()) : 8;
	int hollowed = 0;
	QString error;
	QStringList skipped;
	if (!hollowLevelMapSelection(&m_levelMapDocument, grid, LevelMapHollowMode::Room, &hollowed, &error, &skipped)) {
		statusBar()->showMessage(tr("Could not make the room: %1").arg(error));
		return;
	}
	recordActivity(tr("Level map rooms made"), tr("%n brush(es)", nullptr, hollowed), QStringLiteral("level-map"), OperationState::Warning,
		tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Made %n room(s) with walls %1 units thick outside them.", nullptr, hollowed).arg(grid) + levelMapSkippedNote(skipped),
		5000);
}

void ApplicationShell::selectLevelWholeEntitiesFromUi()
{
	const QVector<LevelMapSelectionRef> whole = levelMapSelectionWholeEntities(m_levelMapDocument);
	if (whole == m_levelMapDocument.selection) {
		statusBar()->showMessage(tr("The selection holds no part of a brush entity."), 4000);
		return;
	}
	QString error;
	if (!setLevelMapSelection(&m_levelMapDocument, whole, &error)) {
		statusBar()->showMessage(error, 5000);
		return;
	}
	refreshLevelMapWorkbench();
	showLevelSelectionMessage(tr("Selected %n object(s), brush entities whole.", nullptr, static_cast<int>(whole.size())));
}

// ---------------------------------------------------------------------------
// Patches

void ApplicationShell::applyLevelPatchMatrixFromUi(LevelPatchMatrixOperation operation)
{
	const QVector<int> ids = levelMapSelectedPatchIds(m_levelMapDocument);
	if (ids.isEmpty()) {
		statusBar()->showMessage(tr("Select Quake III patches first."));
		return;
	}
	int changed = 0;
	QString error;
	if (!applyLevelMapPatchMatrixOperation(&m_levelMapDocument, ids, operation, &changed, &error)) {
		statusBar()->showMessage(tr("Could not change the patches: %1").arg(error), 6000);
		return;
	}
	if (changed == 0) {
		statusBar()->showMessage(tr("The patches are already that way; nothing changed."), 4000);
		return;
	}
	recordActivity(tr("Level patches changed"), levelPatchMatrixOperationId(operation), QStringLiteral("level-map"), OperationState::Warning,
		tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	QStringList grids;
	for (const LevelMapPatch& patch : std::as_const(m_levelMapDocument.patches)) {
		if (ids.contains(patch.id) && grids.size() < 4) {
			grids << tr("%1 × %2").arg(patch.width).arg(patch.height);
		}
	}
	statusBar()->showMessage(tr("Changed %n patch(es); now %1.", nullptr, changed).arg(grids.join(QStringLiteral(", "))), 5000);
}

void ApplicationShell::thickenLevelPatchesFromUi()
{
	const QVector<int> ids = levelMapSelectedPatchIds(m_levelMapDocument);
	if (ids.isEmpty()) {
		statusBar()->showMessage(tr("Select Quake III patches to thicken."));
		return;
	}
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("levelThickenDialog"));
	dialog.setWindowTitle(tr("Thicken Patch"));
	auto* form = new QFormLayout(&dialog);
	auto* thickness = new QDoubleSpinBox;
	thickness->setObjectName(QStringLiteral("levelThickenAmount"));
	thickness->setRange(-4096, 4096);
	thickness->setDecimals(2);
	thickness->setValue(m_levelMapViewport ? std::max(1, m_levelMapViewport->gridSize()) : 8);
	thickness->setAccessibleName(tr("Thickness in map units"));
	thickness->setToolTip(tr("How far behind the surface the copy goes; negative thickens in front of it."));
	auto* direction = new QComboBox;
	direction->setObjectName(QStringLiteral("levelThickenDirection"));
	direction->setAccessibleName(tr("Direction to thicken in"));
	direction->addItem(tr("Along the surface's normals"), -1);
	direction->addItem(tr("Along X"), 0);
	direction->addItem(tr("Along Y"), 1);
	direction->addItem(tr("Along Z"), 2);
	auto* seams = new QCheckBox(tr("Close the open edges with walls"));
	seams->setObjectName(QStringLiteral("levelThickenSeams"));
	seams->setChecked(true);
	form->addRow(tr("Thickness"), thickness);
	form->addRow(tr("Direction"), direction);
	form->addRow(seams);
	auto* note = new QLabel(tr("Each patch keeps its shape; the thickened copy faces away from it and joins its entity."));
	note->setWordWrap(true);
	form->addRow(note);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Thicken"));
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	LevelPatchThickenRequest request;
	request.thickness = thickness->value();
	request.axis = direction->currentData().toInt();
	request.seams = seams->isChecked();
	QVector<int> created;
	QString error;
	if (!thickenLevelMapPatches(&m_levelMapDocument, ids, request, &created, &error)) {
		statusBar()->showMessage(tr("Could not thicken: %1").arg(error), 6000);
		return;
	}
	recordActivity(tr("Level patches thickened"), tr("%n patch(es) made", nullptr, static_cast<int>(created.size())), QStringLiteral("level-map"),
		OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Thickened %n patch(es); the new pieces are selected.", nullptr, static_cast<int>(ids.size())), 5000);
}

// ---------------------------------------------------------------------------
// Regions

void ApplicationShell::setLevelRegionDrawMode(bool enabled)
{
	const bool textMap = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (enabled && !textMap) {
		statusBar()->showMessage(tr("Regions are for Quake-family maps."));
		enabled = false;
	}
	if (enabled) {
		setLevelComponentMode(0);
		if (!levelMap2DShowing()) {
			setLevelMap3D(false);
		}
	}
	for (MapViewport* view : std::as_const(m_levelPlanViews)) {
		const QSignalBlocker blocker(view);
		view->setRegionDrawMode(enabled);
	}
	refreshLevelGeometryCommands();
	if (enabled) {
		if (m_levelMapViewport) {
			m_levelMapViewport->setFocus(Qt::ShortcutFocusReason);
		}
		statusBar()->showMessage(tr("Draw Region on: drag a box in a 2D view to make it the region. Escape leaves."), 6000);
	}
}

void ApplicationShell::setLevelRegionFromDrawnBox(MapViewport* view, const QRectF& box)
{
	setLevelRegionDrawMode(false);
	if (!view || box.width() < 1 || box.height() < 1) {
		return;
	}
	const LevelMapStatistics statistics = levelMapStatistics(m_levelMapDocument);
	// Across the view as drawn, and through the whole map's depth.
	const double depthLow = std::min({statistics.mins.valid ? statistics.mins.x : -4096.0, statistics.mins.valid ? statistics.mins.y : -4096.0,
		statistics.mins.valid ? statistics.mins.z : -4096.0}) - 64;
	const double depthHigh = std::max({statistics.maxs.valid ? statistics.maxs.x : 4096.0, statistics.maxs.valid ? statistics.maxs.y : 4096.0,
		statistics.maxs.valid ? statistics.maxs.z : 4096.0}) + 64;
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	switch (view->projection()) {
	case MapViewportProjection::TopXY:
		mins = {box.left(), box.top(), statistics.mins.valid ? statistics.mins.z : depthLow, true};
		maxs = {box.right(), box.bottom(), statistics.maxs.valid ? statistics.maxs.z : depthHigh, true};
		break;
	case MapViewportProjection::FrontXZ:
		mins = {box.left(), statistics.mins.valid ? statistics.mins.y : depthLow, box.top(), true};
		maxs = {box.right(), statistics.maxs.valid ? statistics.maxs.y : depthHigh, box.bottom(), true};
		break;
	case MapViewportProjection::SideZY:
		mins = {statistics.mins.valid ? statistics.mins.x : depthLow, box.left(), box.top(), true};
		maxs = {statistics.maxs.valid ? statistics.maxs.x : depthHigh, box.right(), box.bottom(), true};
		break;
	}
	if (maxs.x - mins.x < 1 || maxs.y - mins.y < 1 || maxs.z - mins.z < 1) {
		maxs.x = std::max(maxs.x, mins.x + 1);
		maxs.y = std::max(maxs.y, mins.y + 1);
		maxs.z = std::max(maxs.z, mins.z + 1);
	}
	setLevelRegion(mins, maxs);
	showLevelSidebarPage(QStringLiteral("view"), QStringLiteral("view.region"));
	statusBar()->showMessage(tr("The region is set to the box you drew."), 4000);
}

void ApplicationShell::setLevelRegionFromBrushes()
{
	QVector<LevelMapSelectionRef> brushes;
	for (const LevelMapSelectionRef& ref : std::as_const(m_levelMapDocument.selection)) {
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			brushes << ref;
		}
	}
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	if (brushes.isEmpty() || !levelMapObjectsBounds(m_levelMapDocument, brushes, &mins, &maxs) || maxs.x - mins.x < 1 || maxs.y - mins.y < 1
		|| maxs.z - mins.z < 1) {
		statusBar()->showMessage(tr("Select a brush with some size on every axis to make the region of."));
		return;
	}
	QString error;
	if (!deleteLevelMapObjects(&m_levelMapDocument, brushes, &error)) {
		statusBar()->showMessage(tr("Could not take the brush away: %1").arg(error), 6000);
		return;
	}
	refreshLevelMapWorkbench();
	setLevelRegion(mins, maxs);
	showLevelSidebarPage(QStringLiteral("view"), QStringLiteral("view.region"));
	statusBar()->showMessage(tr("The region is set to the brush, which is gone; Undo brings it back and keeps the region."), 6000);
}

void ApplicationShell::editLevelRegionBounds()
{
	const bool textMap = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (!textMap) {
		statusBar()->showMessage(tr("Regions are for Quake-family maps."));
		return;
	}
	LevelMapVec3 mins = m_levelRegionMins;
	LevelMapVec3 maxs = m_levelRegionMaxs;
	if (!levelRegionActive() && !levelMapSelectionBounds(m_levelMapDocument, &mins, &maxs)) {
		const LevelMapStatistics statistics = levelMapStatistics(m_levelMapDocument);
		mins = statistics.mins.valid ? statistics.mins : LevelMapVec3 {-512, -512, -512, true};
		maxs = statistics.maxs.valid ? statistics.maxs : LevelMapVec3 {512, 512, 512, true};
	}
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("levelRegionBoundsDialog"));
	dialog.setWindowTitle(tr("Region Bounds"));
	auto* form = new QFormLayout(&dialog);
	QDoubleSpinBox* fields[6] {};
	const QStringList axes {tr("X"), tr("Y"), tr("Z")};
	for (int corner = 0; corner < 2; ++corner) {
		for (int axis = 0; axis < 3; ++axis) {
			auto* field = new QDoubleSpinBox;
			field->setObjectName(QStringLiteral("levelRegionBound%1%2").arg(corner == 0 ? QStringLiteral("Min") : QStringLiteral("Max")).arg(axis));
			field->setRange(-131072, 131072);
			field->setDecimals(2);
			const LevelMapVec3& from = corner == 0 ? mins : maxs;
			field->setValue(axis == 0 ? from.x : (axis == 1 ? from.y : from.z));
			const QString label = corner == 0 ? tr("Lowest %1").arg(axes.at(axis)) : tr("Highest %1").arg(axes.at(axis));
			field->setAccessibleName(label);
			form->addRow(label, field);
			fields[corner * 3 + axis] = field;
		}
	}
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Set Region"));
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	const LevelMapVec3 low {std::min(fields[0]->value(), fields[3]->value()), std::min(fields[1]->value(), fields[4]->value()),
		std::min(fields[2]->value(), fields[5]->value()), true};
	const LevelMapVec3 high {std::max(fields[0]->value(), fields[3]->value()), std::max(fields[1]->value(), fields[4]->value()),
		std::max(fields[2]->value(), fields[5]->value()), true};
	if (high.x - low.x < 1 || high.y - low.y < 1 || high.z - low.z < 1) {
		statusBar()->showMessage(tr("A region needs at least one unit of size on every axis."));
		return;
	}
	setLevelRegion(low, high);
	statusBar()->showMessage(tr("The region is set to the bounds you typed."), 4000);
}

void ApplicationShell::selectLevelRegionObjects()
{
	if (!levelRegionActive()) {
		statusBar()->showMessage(tr("Set a region first."));
		return;
	}
	int leftHidden = 0;
	const QVector<LevelMapSelectionRef> objects =
		visibleLevelMapObjects(levelMapRegionObjects(m_levelMapDocument, m_levelRegionMins, m_levelRegionMaxs), &leftHidden);
	// Worldspawn itself is never picked, as Select All leaves it.
	QVector<LevelMapSelectionRef> picked;
	for (const LevelMapSelectionRef& ref : objects) {
		bool world = false;
		if (ref.kind == LevelMapSelectionKind::Entity) {
			for (const LevelMapEntity& entity : std::as_const(m_levelMapDocument.entities)) {
				if (entity.id == ref.objectId) {
					world = entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0;
					break;
				}
			}
		}
		if (!world) {
			picked << ref;
		}
	}
	if (picked.isEmpty()) {
		statusBar()->showMessage(tr("Nothing in the region can be selected."), 4000);
		return;
	}
	QString error;
	if (!setLevelMapSelection(&m_levelMapDocument, picked, &error)) {
		statusBar()->showMessage(error, 5000);
		return;
	}
	refreshLevelMapWorkbench();
	showLevelSelectionMessage(leftHidden > 0 ? tr("Selected %n object(s) in the region; hidden ones were left out.", nullptr, static_cast<int>(picked.size()))
											 : tr("Selected %n object(s) in the region.", nullptr, static_cast<int>(picked.size())));
}

void ApplicationShell::rememberLevelRegion()
{
	if (m_levelMapDocument.sourcePath.isEmpty()) {
		return;
	}
	const QString key = QStringLiteral("levelRegion/") + QString::fromLatin1(regionKey(m_levelMapDocument.sourcePath));
	if (!levelRegionActive()) {
		m_settings.setShellLayoutState(key, {});
		return;
	}
	const QStringList values {QString::number(m_levelRegionMins.x, 'g', 17), QString::number(m_levelRegionMins.y, 'g', 17),
		QString::number(m_levelRegionMins.z, 'g', 17), QString::number(m_levelRegionMaxs.x, 'g', 17), QString::number(m_levelRegionMaxs.y, 'g', 17),
		QString::number(m_levelRegionMaxs.z, 'g', 17)};
	m_settings.setShellLayoutState(key, values.join(QLatin1Char(' ')).toUtf8());
}

bool ApplicationShell::rememberedLevelRegion(LevelMapVec3* mins, LevelMapVec3* maxs) const
{
	if (m_levelMapDocument.sourcePath.isEmpty()
		|| (m_levelMapDocument.format != LevelMapFormat::QuakeMap && m_levelMapDocument.format != LevelMapFormat::Quake3Map)) {
		return false;
	}
	const QByteArray stored =
		m_settings.shellLayoutState(QStringLiteral("levelRegion/") + QString::fromLatin1(regionKey(m_levelMapDocument.sourcePath)));
	const QStringList values = QString::fromUtf8(stored).split(QLatin1Char(' '), Qt::SkipEmptyParts);
	if (values.size() != 6) {
		return false;
	}
	double numbers[6] {};
	for (int i = 0; i < 6; ++i) {
		bool ok = false;
		numbers[i] = values.at(i).toDouble(&ok);
		if (!ok || !std::isfinite(numbers[i])) {
			return false;
		}
	}
	if (numbers[3] - numbers[0] < 1 || numbers[4] - numbers[1] < 1 || numbers[5] - numbers[2] < 1) {
		return false;
	}
	*mins = {numbers[0], numbers[1], numbers[2], true};
	*maxs = {numbers[3], numbers[4], numbers[5], true};
	return true;
}

// ---------------------------------------------------------------------------
// Navigation

void ApplicationShell::zoomLevelViewsToFit()
{
	if (m_levelMapDocument.format == LevelMapFormat::Unknown) {
		return;
	}
	for (MapViewport* view : std::as_const(m_levelPlanViews)) {
		if (view->hasDocument()) {
			view->zoomToFit();
		}
	}
	statusBar()->showMessage(tr("The 2D views frame the whole map."), 3000);
}

void ApplicationShell::centreLevelViewsOnSelection()
{
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	if (!levelMapSelectionBounds(m_levelMapDocument, &mins, &maxs)) {
		statusBar()->showMessage(tr("Select something to centre the views on."));
		return;
	}
	const LevelMapVec3 centre {(mins.x + maxs.x) / 2, (mins.y + maxs.y) / 2, (mins.z + maxs.z) / 2, true};
	for (MapViewport* view : std::as_const(m_levelPlanViews)) {
		if (!view->hasDocument()) {
			continue;
		}
		PlanViewState state = view->navigationState();
		state.center = mapViewportProjectPoint(view->projection(), centre);
		view->restoreNavigationState(state);
	}
	statusBar()->showMessage(tr("The 2D views are centred on the selection."), 3000);
}

void ApplicationShell::goToLevelCoordinatesFromUi()
{
	if (m_levelMapDocument.format == LevelMapFormat::Unknown) {
		return;
	}
	LevelMapVec3 start {0, 0, 0, true};
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	if (levelMapSelectionBounds(m_levelMapDocument, &mins, &maxs)) {
		start = {(mins.x + maxs.x) / 2, (mins.y + maxs.y) / 2, (mins.z + maxs.z) / 2, true};
	}
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("levelGoToDialog"));
	dialog.setWindowTitle(tr("Go to Coordinates"));
	auto* form = new QFormLayout(&dialog);
	auto* point = new QLineEdit(QStringLiteral("%1 %2 %3").arg(start.x, 0, 'g', 8).arg(start.y, 0, 'g', 8).arg(start.z, 0, 'g', 8));
	point->setObjectName(QStringLiteral("levelGoToPoint"));
	point->setAccessibleName(tr("Coordinates, x y z"));
	point->setToolTip(tr("Three numbers, such as 128 -64 32, separated by spaces or commas."));
	auto* camera = new QCheckBox(tr("Take the camera there too"));
	camera->setObjectName(QStringLiteral("levelGoToCamera"));
	camera->setChecked(levelMap3DShowing());
	form->addRow(tr("Point"), point);
	form->addRow(camera);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Go"));
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	// Three numbers, between spaces or commas.
	const QStringList parts = point->text().split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
	LevelMapVec3 target {0, 0, 0, true};
	bool parsed = parts.size() == 3;
	for (int axis = 0; parsed && axis < 3; ++axis) {
		bool ok = false;
		const double value = parts.at(axis).toDouble(&ok);
		parsed = ok && std::isfinite(value);
		(axis == 0 ? target.x : (axis == 1 ? target.y : target.z)) = value;
	}
	if (!parsed) {
		statusBar()->showMessage(tr("Type three numbers, x y z."), 5000);
		return;
	}
	for (MapViewport* view : std::as_const(m_levelPlanViews)) {
		if (!view->hasDocument()) {
			continue;
		}
		PlanViewState state = view->navigationState();
		state.center = mapViewportProjectPoint(view->projection(), target);
		view->restoreNavigationState(state);
	}
	if (camera->isChecked() && m_levelMap3D) {
		if (!levelMap3DShowing()) {
			setLevelMap3D(true);
		}
		ModelVec3 position;
		position.x = static_cast<float>(target.x);
		position.y = static_cast<float>(target.y);
		position.z = static_cast<float>(target.z);
		m_levelMap3D->setCameraView(position, m_levelMap3D->cameraYaw(), m_levelMap3D->cameraPitch());
	}
	statusBar()->showMessage(tr("The views are at %1, %2, %3.").arg(target.x, 0, 'g', 8).arg(target.y, 0, 'g', 8).arg(target.z, 0, 'g', 8), 4000);
}

void ApplicationShell::findLevelObjectByNumberFromUi()
{
	const bool textMap = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (!textMap) {
		statusBar()->showMessage(tr("Entity and brush numbers belong to Quake-family maps."));
		return;
	}
	int entity = 0;
	int primitive = 0;
	if (!m_levelMapDocument.selection.isEmpty()) {
		levelMapObjectNumber(m_levelMapDocument, m_levelMapDocument.selection.last(), &entity, &primitive);
		primitive = std::max(0, primitive);
	}
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("levelFindObjectDialog"));
	dialog.setWindowTitle(tr("Find Brush by Number"));
	auto* form = new QFormLayout(&dialog);
	auto* entityField = new QSpinBox;
	entityField->setObjectName(QStringLiteral("levelFindEntity"));
	entityField->setRange(0, 1 << 20);
	entityField->setValue(entity);
	entityField->setAccessibleName(tr("Entity number, worldspawn is 0"));
	auto* primitiveField = new QSpinBox;
	primitiveField->setObjectName(QStringLiteral("levelFindBrush"));
	primitiveField->setRange(-1, 1 << 20);
	primitiveField->setValue(primitive);
	primitiveField->setSpecialValueText(tr("The entity itself"));
	primitiveField->setAccessibleName(tr("Brush or patch number within the entity"));
	auto* message = new QLineEdit;
	message->setObjectName(QStringLiteral("levelFindMessage"));
	message->setPlaceholderText(tr("Or paste a compiler line, such as Entity 0, Brush 12"));
	message->setAccessibleName(tr("Compiler message to read the numbers from"));
	connect(message, &QLineEdit::textChanged, &dialog, [entityField, primitiveField](const QString& text) {
		int e = -1;
		int p = -1;
		if (parseLevelMapObjectNumber(text, &e, &p)) {
			entityField->setValue(e);
			primitiveField->setValue(p);
		}
	});
	form->addRow(tr("Entity"), entityField);
	form->addRow(tr("Brush"), primitiveField);
	form->addRow(message);
	auto* note = new QLabel(tr("Counted from 0 in the order the map file lists them, brushes and patches together, as compilers count them."));
	note->setWordWrap(true);
	form->addRow(note);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Find"));
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	LevelMapSelectionRef ref;
	QString error;
	if (!levelMapObjectAtNumber(m_levelMapDocument, entityField->value(), primitiveField->value(), &ref, &error)) {
		statusBar()->showMessage(error, 6000);
		return;
	}
	if (selectLevelMapObjectBySelector(levelMapSelectionRefId(ref))) {
		frameLevelMapSelection();
		statusBar()->showMessage(tr("Entity %1, brush %2 is %3.").arg(entityField->value()).arg(primitiveField->value()).arg(levelMapSelectionRefId(ref)),
			5000);
	}
}

// ---------------------------------------------------------------------------
// Prefabs

void ApplicationShell::replaceLevelSelectionWithPrefabFromUi()
{
	const bool textMap = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (!textMap || m_levelMapDocument.selection.isEmpty()) {
		statusBar()->showMessage(tr("Select the objects a prefab should take the place of."));
		return;
	}
	const QString start = m_lastLevelPrefabPath.isEmpty() ? m_settings.currentProjectPath() : m_lastLevelPrefabPath;
	const QString path = QFileDialog::getOpenFileName(this, tr("Replace Selection with Prefab"), start, tr("VibeStudio prefabs (*.vprefab)"));
	if (path.isEmpty()) {
		return;
	}
	m_lastLevelPrefabPath = path;
	replaceLevelSelectionWithPrefab(path);
}

void ApplicationShell::placeLevelPrefabFromPalette(const QString& payload, const QPointF& viewPoint)
{
	const bool textMap = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (!textMap || !m_levelMapViewport) {
		statusBar()->showMessage(tr("Open a Quake-family map to place prefabs."));
		return;
	}
	const bool entry = payload.startsWith(QStringLiteral("prefab-entry:"));
	const QString path = payload.mid(entry ? 13 : 12);
	LevelPrefab prefab;
	QString error;
	if (entry) {
		auto archive = m_packageStaging.isLoaded() ? std::make_shared<PackageStagingArchive>(m_packageStaging) : m_packageArchive.snapshotReader();
		if (!archive && m_packageArchive.isOpen()) {
			archive = std::make_shared<const PackageArchive>(m_packageArchive);
		}
		QByteArray bytes;
		if (!archive || !archive->readEntryBytes(path, &bytes, &error, levelPrefabByteLimit) || !parseLevelPrefab(bytes, &prefab, &error)) {
			statusBar()->showMessage(tr("Could not read the prefab: %1").arg(error.isEmpty() ? tr("open the package that holds it") : error), 6000);
			return;
		}
	} else if (!readLevelPrefab(path, &prefab, &error)) {
		statusBar()->showMessage(tr("Could not read the prefab: %1").arg(error), 6000);
		return;
	}
	// Its anchor goes where it was dropped, at the depth new brushes take there.
	LevelPrefabPlacement placement;
	placement.position = m_levelMapViewport->worldPositionAt(viewPoint, levelMapHiddenAxisValue());
	placement.textureLock = m_settings.levelTextureLock();
	LevelPrefabReport report;
	if (!insertLevelPrefab(&m_levelMapDocument, prefab, placement, &report, &error)) {
		statusBar()->showMessage(tr("Could not place the prefab: %1").arg(error), 6000);
		return;
	}
	recordActivity(tr("Prefab placed"), prefab.name, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	QString message = tr("Placed %1 at %2, %3, %4; Insert Prefab… turns one before placing it.")
						  .arg(prefab.name)
						  .arg(placement.position.x, 0, 'g', 8)
						  .arg(placement.position.y, 0, 'g', 8)
						  .arg(placement.position.z, 0, 'g', 8);
	if (!report.externalTargets.isEmpty()) {
		message += QLatin1Char(' ') + tr("%n link(s) name objects outside the prefab.", nullptr, static_cast<int>(report.externalTargets.size()));
	}
	statusBar()->showMessage(message, 6000);
}

bool ApplicationShell::replaceLevelSelectionWithPrefab(const QString& path)
{
	LevelPrefab prefab;
	QString error;
	if (!readLevelPrefab(path, &prefab, &error)) {
		statusBar()->showMessage(tr("Could not read the prefab: %1").arg(error), 6000);
		return false;
	}
	LevelPrefabPlacement placement;
	placement.textureLock = m_settings.levelTextureLock();
	LevelPrefabReport report;
	if (!replaceLevelMapSelectionWithPrefab(&m_levelMapDocument, prefab, placement, &report, &error)) {
		statusBar()->showMessage(tr("Could not replace the selection: %1").arg(error), 6000);
		return false;
	}
	recordActivity(tr("Selection replaced with a prefab"), prefab.name, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Replaced the selection with %1; Undo puts it back.").arg(prefab.name), 6000);
	return true;
}

} // namespace vibestudio
