// The Levels object manipulators in the shell: the Translate, Rotate, Scale
// and Transform tools' commands, their shared pivot, and publishing a
// manipulator drag through the ordinary move, rotation, scale and shear
// services with Texture Lock, the Valve 220 policy and Texture Scale Lock
// (core/level_object_manipulator.h).
//
// Workflow reference: NetRadiant Custom radiant/tools.cpp MouseTranslate (W),
// MouseRotate (R), MouseScale, MouseRotateOrScale (ToggleRotateScaleModes),
// MouseTransform and MouseDragOrTransform (ToggleDragSkewModes, Q), and
// radiant/selection.cpp TransformOriginManipulator at 449778b3
// (GPL-2.0-or-later); TrenchBroom "Menu/Edit/Tools/Rotate Tool" (R),
// "Menu/Edit/Tools/Scale Tool" (T) and "Menu/Edit/Tools/Shear Tool" (G) at
// 4c756be1 (GPL-3.0-or-later). Original Qt implementation; see docs/CREDITS.md.
#include "app/application_shell.h"
#include "app/level_component_gizmo.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_object_manipulator.h"
#include "core/level_placement.h"

#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace vibestudio {
namespace {
bool quakeFamily(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
}
} // namespace

void ApplicationShell::registerLevelObjectGizmoCommands()
{
	const auto add = [this](const QString& id, const QString& label, const QString& tip, const QString& icon, bool checkable,
						 std::function<void()> handler) {
		StudioCommandRegistration command;
		command.commandId = id;
		command.group = StudioCommandGroup::Edit;
		command.label = label;
		command.statusTip = tip;
		command.iconName = icon;
		command.checkable = checkable;
		command.menuSection = tr("Manipulators");
		command.handler = [this, handler = std::move(handler)]() {
			setMode(StudioMode::Levels);
			handler();
		};
		m_commands->registerCommand(command);
	};
	add(QStringLiteral("map.translateTool"), tr("Translate Tool"),
		tr("Move the selection with X, Y and Z arrows, a square that moves across the view, and a pivot you can place. Choose it again to "
		   "return to Select."),
		QStringLiteral("move"), true, [this]() {
			chooseLevelTool(m_levelObjectTool == static_cast<int>(LevelObjectTool::Translate) ? QStringLiteral("select") : QStringLiteral("translate"));
		});
	add(QStringLiteral("map.rotateTool"), tr("Rotate Tool"),
		tr("Turn the selection with X, Y and Z rings and an outer ring that turns it in the view (and, where your profile offers it, a free "
		   "turn inside the rings), about a pivot you can place. Choose it again to return to Select."),
		QStringLiteral("rotate"), true, [this]() {
			chooseLevelTool(m_levelObjectTool == static_cast<int>(LevelObjectTool::Rotate) ? QStringLiteral("select") : QStringLiteral("rotate"));
		});
	add(QStringLiteral("map.scaleTool"), tr("Scale Tool"),
		tr("Scale the selection with X, Y and Z handles and a square that scales it across the view, about a pivot you can place, or by its "
		   "box's faces, edges and corners. Choose it again to return to Select."),
		QStringLiteral("resize"), true, [this]() {
			chooseLevelTool(m_levelObjectTool == static_cast<int>(LevelObjectTool::Scale) ? QStringLiteral("select") : QStringLiteral("scale"));
		});
	add(QStringLiteral("map.transformTool"), tr("Transform Tool"),
		tr("Skew the selection by the sides of its box, turn it by the corners, move it from inside and scale it from just outside a side "
		   "or corner, about the opposite side or corner or a pivot you can place. Choose it again to return to Select."),
		QStringLiteral("transform"), true, [this]() {
			chooseLevelTool(m_levelObjectTool == static_cast<int>(LevelObjectTool::Transform) ? QStringLiteral("select") : QStringLiteral("transform"));
		});
	add(QStringLiteral("map.selectOrTransformTool"), tr("Select or Transform Tool"),
		tr("Switch to the Transform tool from Select, and back to Select from any other tool. In a vertex, edge or face mode it switches "
		   "between Move Components and Shear Components."),
		QStringLiteral("transform"), false, [this]() { toggleLevelSelectOrTransformTool(); });
	add(QStringLiteral("map.rotateOrScaleTool"), tr("Rotate or Scale Tool"),
		tr("Switch to the Scale tool from the Rotate tool, and to the Rotate tool otherwise. In a component mode it switches between Rotate "
		   "Components and Scale Components."),
		QStringLiteral("rotate"), false, [this]() { toggleLevelRotateOrScaleTool(); });
	add(QStringLiteral("map.resetObjectPivot"), tr("Reset Object Pivot"),
		tr("Let the manipulator pivot follow the centre of the selection again."), QStringLiteral("move"), false, [this]() {
			m_levelObjectPivot = {};
			refreshLevelObjectGizmos();
			refreshLevelObjectGizmoCommands();
			statusBar()->showMessage(tr("The manipulator pivot follows the centre of the selection."), 5000);
		});
	add(QStringLiteral("map.setObjectPivot"), tr("Set Object Pivot…"),
		tr("Type the point the object manipulators move and turn the selection about."), QStringLiteral("move"), false,
		[this]() { editLevelObjectPivotFromUi(); });
}

void ApplicationShell::refreshLevelObjectGizmoCommands()
{
	if (!m_commands) {
		return;
	}
	const bool open = quakeFamily(m_levelMapDocument) && m_levelMapViewport && m_levelMap3D;
	for (const auto& [id, tool] : {std::pair {QStringLiteral("map.translateTool"), LevelObjectTool::Translate},
			 std::pair {QStringLiteral("map.rotateTool"), LevelObjectTool::Rotate}, std::pair {QStringLiteral("map.scaleTool"), LevelObjectTool::Scale},
			 std::pair {QStringLiteral("map.transformTool"), LevelObjectTool::Transform}}) {
		m_commands->setEnabled(id, open);
		if (QAction* action = m_commands->action(id)) {
			const QSignalBlocker blocker(action);
			action->setChecked(open && m_levelObjectTool == static_cast<int>(tool));
		}
	}
	m_commands->setEnabled(QStringLiteral("map.rotateOrScaleTool"), open);
	m_commands->setEnabled(QStringLiteral("map.selectOrTransformTool"), open);
	m_commands->setEnabled(QStringLiteral("map.resetObjectPivot"), open && m_levelObjectPivot.valid);
	m_commands->setEnabled(QStringLiteral("map.setObjectPivot"), open && !m_levelMapDocument.selection.isEmpty());
}

bool ApplicationShell::levelObjectToolActive() const
{
	return m_levelObjectTool != static_cast<int>(LevelObjectTool::Select) && quakeFamily(m_levelMapDocument);
}

void ApplicationShell::setLevelObjectTool(int tool)
{
	const int next = tool == static_cast<int>(LevelObjectTool::Translate) || tool == static_cast<int>(LevelObjectTool::Rotate)
			|| tool == static_cast<int>(LevelObjectTool::Scale) || tool == static_cast<int>(LevelObjectTool::Transform)
		? tool : static_cast<int>(LevelObjectTool::Select);
	// One tool at a time: an object tool ends the Shear Tool, as TrenchBroom's
	// tool box does.
	if (next != static_cast<int>(LevelObjectTool::Select) && m_levelMapViewport && m_levelMapViewport->shearMode()) {
		setLevelShearMode(false);
	}
	for (auto* view : std::as_const(m_levelPlanViews)) { view->objectGizmo()->finish(false); }
	if (m_levelMap3D) { m_levelMap3D->objectGizmo()->finish(false); }
	const bool changed = next != m_levelObjectTool;
	m_levelObjectTool = next;
	refreshLevelObjectGizmos();
	refreshLevelCameraResize();
	refreshLevelObjectGizmoCommands();
	if (!changed || next == static_cast<int>(LevelObjectTool::Select)) {
		return;
	}
	const LevelObjectManipulatorTarget target = levelObjectManipulatorTarget(m_levelMapDocument);
	if (next == static_cast<int>(LevelObjectTool::Rotate)) {
		statusBar()->showMessage(!target.available ? tr("Rotate tool: %1").arg(target.reason)
				: m_levelControls.objectRotateFreeTurn
				? tr("Rotate tool: drag X, Y or Z to turn about that axis, the outer ring to turn in the view, inside the rings to turn freely, "
					 "or the pivot point to place the pivot.")
				: tr("Rotate tool: drag X, Y or Z to turn about that axis, the outer ring to turn in the view, or the pivot point to place the pivot."),
			6000);
		return;
	}
	if (next == static_cast<int>(LevelObjectTool::Transform)) {
		statusBar()->showMessage(target.available
				? tr("Transform tool: drag a side to skew, a corner to turn, inside the box to move, just outside a side or corner to scale, or "
					 "the pivot point to place the pivot.")
				: tr("Transform tool: %1").arg(target.reason),
			6000);
		return;
	}
	if (next == static_cast<int>(LevelObjectTool::Scale)) {
		statusBar()->showMessage(target.available
				? tr("Scale tool: drag X, Y or Z to scale along that axis, the square to scale across the view, a face, edge or corner of the box "
					 "to stretch it, or the pivot point to place the pivot.")
				: tr("Scale tool: %1").arg(target.reason),
			6000);
		return;
	}
	statusBar()->showMessage(target.available
			? tr("Translate tool: drag X, Y or Z along an axis, the square across the view, or the pivot point to place the pivot.")
			: tr("Translate tool: %1").arg(target.reason),
		6000);
}

void ApplicationShell::refreshLevelObjectGizmos()
{
	if (!m_levelMapViewport || !m_levelMap3D) {
		return;
	}
	if (m_levelObjectPivotLoad != m_levelMapLoadSerial) {
		m_levelObjectPivotLoad = m_levelMapLoadSerial;
		m_levelObjectPivot = {};
	}
	const bool translate = m_levelObjectTool == static_cast<int>(LevelObjectTool::Translate);
	const bool rotate = m_levelObjectTool == static_cast<int>(LevelObjectTool::Rotate);
	const bool scale = m_levelObjectTool == static_cast<int>(LevelObjectTool::Scale);
	const bool transform = m_levelObjectTool == static_cast<int>(LevelObjectTool::Transform);
	const bool on = (translate || rotate || scale || transform) && quakeFamily(m_levelMapDocument);
	// Measured only while a manipulator is on: Select costs nothing here.
	const LevelObjectManipulatorTarget target = on ? levelObjectManipulatorTarget(m_levelMapDocument) : LevelObjectManipulatorTarget {};
	const bool available = on && target.available && !levelComponentsActive() && !levelSweepToolActive();
	const LevelMapVec3 pivot = available ? levelObjectManipulatorPivot(target, m_levelObjectPivot) : LevelMapVec3 {};
	const LevelComponentTool tool = !on ? LevelComponentTool::Move
		: rotate	? LevelComponentTool::Rotate
		: scale		? LevelComponentTool::Scale
		: transform ? LevelComponentTool::Transform
					: LevelComponentTool::Translate;
	// Ctrl lands the bounds on the working grid even while snapping is off.
	const double boundsGrid = levelMapGridUnits();
	// The rings turn by the manipulator rotation step while snapping is on
	// (Component Manipulator Options); Shift snaps to 15 degrees either way.
	const double angleStep = m_settings.levelComponentTransformSteps()[0];
	for (auto* view : std::as_const(m_levelPlanViews)) {
		view->objectGizmo()->setFreeTurn(m_levelControls.objectRotateFreeTurn);
		view->objectGizmo()->setObjectContext(target.mins, target.maxs, boundsGrid, m_levelObjectPivot.valid);
		view->objectGizmo()->setContext(tool, pivot, available, view->snapToGrid() ? view->gridSize() : 0, view->snapToGrid() ? angleStep : 0);
	}
	const bool snap = m_levelMapViewport->snapToGrid();
	m_levelMap3D->objectGizmo()->setFreeTurn(m_levelControls.objectRotateFreeTurn);
	m_levelMap3D->objectGizmo()->setObjectContext(target.mins, target.maxs, boundsGrid, m_levelObjectPivot.valid);
	m_levelMap3D->objectGizmo()->setContext(tool, pivot, available, snap ? m_levelMapViewport->gridSize() : 0, snap ? angleStep : 0);
}

void ApplicationShell::connectLevelObjectGizmo(LevelComponentGizmo* gizmo, MapViewport* plan)
{
	connect(gizmo, &LevelComponentGizmo::activeChanged, this, [this, gizmo, plan](bool active) {
		if (active && plan) {
			activateLevelPlanViewport(plan);
		}
		// A turn, scale or skew previews the selection's wire, measured once as
		// it begins; a move shows the selection itself.
		const auto kind = gizmo->transformKind();
		const bool wire = gizmo->tool() == LevelComponentTool::Rotate || gizmo->tool() == LevelComponentTool::Scale
			|| kind == LevelObjectTransformKind::Skew || kind == LevelObjectTransformKind::Rotate || kind == LevelObjectTransformKind::Scale;
		if (active && gizmo->objects() && wire && gizmo->activeHandle() != 4) {
			gizmo->setObjectPreviewEdges(levelObjectManipulatorEdges(m_levelMapDocument, m_levelMapDocument.selection));
		}
	});
	connect(gizmo, &LevelComponentGizmo::commitRequested, this, [this, gizmo, plan](quint64) {
		// The Transform tool's drags go through the service of what they do.
		const auto kind = gizmo->transformKind();
		if (kind == LevelObjectTransformKind::Skew) {
			int axis = 2, side = 1;
			LevelMapVec3 offset;
			if (gizmo->objectSkew(&axis, &side, &offset)) {
				shearLevelObjectsFromView(axis, side, offset, gizmo->pivot(), plan);
			}
		} else if (kind == LevelObjectTransformKind::Rotate || gizmo->tool() == LevelComponentTool::Rotate) {
			rotateLevelObjectsFromGizmo(gizmo, plan);
		} else if (kind == LevelObjectTransformKind::Scale || gizmo->tool() == LevelComponentTool::Scale) {
			scaleLevelObjectsFromGizmo(gizmo, plan);
		} else {
			translateLevelObjectsFromGizmo(gizmo, plan);
		}
	});
	connect(gizmo, &LevelComponentGizmo::pivotChanged, this, [this](const LevelMapVec3& pivot) {
		m_levelObjectPivot = pivot;
		refreshLevelObjectGizmos();
		refreshLevelObjectGizmoCommands();
		statusBar()->showMessage(tr("Pivot placed at %1, %2, %3. Click the pivot point, or use Reset Object Pivot, to follow the selection's centre again.")
			.arg(pivot.x).arg(pivot.y).arg(pivot.z), 6000);
	});
	// NetRadiant Custom: a click on the pivot point switches between the
	// placed pivot and the automatic one, keeping a placed one where it is.
	connect(gizmo, &LevelComponentGizmo::pivotToggleRequested, this, [this, gizmo]() {
		if (m_levelObjectPivot.valid) {
			m_levelObjectPivot = {};
			statusBar()->showMessage(tr("The manipulator pivot follows the centre of the selection."), 5000);
		} else {
			m_levelObjectPivot = gizmo->pivot();
			statusBar()->showMessage(tr("Pivot placed at %1, %2, %3. It stays there while the selection changes.")
				.arg(m_levelObjectPivot.x).arg(m_levelObjectPivot.y).arg(m_levelObjectPivot.z), 6000);
		}
		refreshLevelObjectGizmos();
		refreshLevelObjectGizmoCommands();
	});
}

void ApplicationShell::translateLevelObjectsFromGizmo(LevelComponentGizmo* gizmo, MapViewport* plan)
{
	if (plan) {
		activateLevelPlanViewport(plan);
	}
	const LevelMapVec3 delta = gizmo->request().delta;
	const LevelObjectManipulatorTarget target = levelObjectManipulatorTarget(m_levelMapDocument);
	if (!target.available || !delta.valid || (delta.x == 0 && delta.y == 0 && delta.z == 0)) {
		if (!target.available) {
			statusBar()->showMessage(tr("Selection unchanged: %1").arg(target.reason), 6000);
		}
		refreshLevelMapViewport();
		return;
	}
	// The manipulator already snapped the move, to the grid or by the bounds,
	// so it is applied exactly, through the move every drag and nudge uses.
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::Move;
	request.offset = delta;
	request.grid = 0;
	request.snapMoveDelta = false;
	request.textures = {m_settings.levelTextureLock(), m_settings.levelAllowValve220()};
	QString error;
	if (!runLevelPlacementFromUi(request, &error)) {
		if (!error.isEmpty()) {
			statusBar()->showMessage(tr("Selection unchanged: %1").arg(error), 6000);
		}
		refreshLevelMapViewport();
		return;
	}
	// A placed pivot travels with what it belongs to, as NetRadiant Custom's does.
	if (m_levelObjectPivot.valid) {
		m_levelObjectPivot = {m_levelObjectPivot.x + delta.x, m_levelObjectPivot.y + delta.y, m_levelObjectPivot.z + delta.z, true};
	}
	recordActivity(tr("Level map selection translated"), tr("%1, %2, %3").arg(delta.x).arg(delta.y).arg(delta.z), QStringLiteral("level-map"),
		OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	refreshLevelObjectGizmos();
	const int moved = static_cast<int>(target.objects.size());
	statusBar()->showMessage(tr("Translated %n object(s) by %1, %2, %3. Undo restores them.", nullptr, moved)
		.arg(delta.x).arg(delta.y).arg(delta.z), 5000);
}

void ApplicationShell::rotateLevelObjectsFromGizmo(LevelComponentGizmo* gizmo, MapViewport* plan)
{
	if (plan) {
		activateLevelPlanViewport(plan);
	}
	// The manipulator already snapped the angle; the core service turns it
	// into the numeric rotation Rotate Selection… applies, so Texture Lock,
	// the Valve 220 policy, locks, linked groups, Repeat and undo behave alike.
	LevelObjectRotateRequest request;
	request.axis = gizmo->rotationAxis();
	request.degrees = gizmo->request().amount;
	request.pivot = gizmo->pivot();
	request.textures = {m_settings.levelTextureLock(), m_settings.levelAllowValve220()};
	LevelMapRotationRequest rotation;
	LevelObjectRotateReport report;
	QString error;
	if (!levelObjectRotation(levelObjectManipulatorTarget(m_levelMapDocument), request, &rotation, &report, &error)) {
		statusBar()->showMessage(tr("Selection unchanged: %1").arg(error), 6000);
		refreshLevelMapViewport();
		return;
	}
	LevelPlacementRequest work;
	work.operation = LevelPlacementOperation::Rotate;
	work.rotation = rotation;
	if (!runLevelPlacementFromUi(work, &error)) {
		if (!error.isEmpty()) {
			statusBar()->showMessage(tr("Selection unchanged: %1").arg(error), 6000);
		}
		refreshLevelMapViewport();
		return;
	}
	const QString turn = rotation.euler
		? tr("%1°, %2° and %3° about X, Y and Z").arg(rotation.eulerDegrees[0], 0, 'g', 6).arg(rotation.eulerDegrees[1], 0, 'g', 6).arg(rotation.eulerDegrees[2], 0, 'g', 6)
		: tr("%1° about %2").arg(rotation.degrees, 0, 'g', 6).arg(QLatin1Char("XYZ"[std::clamp(rotation.axis, 0, 2)]));
	recordActivity(tr("Level map selection rotated"), turn, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	refreshLevelObjectGizmos();
	statusBar()->showMessage(tr("Rotated %n object(s) by %1. Undo restores them.", nullptr, report.objects).arg(turn), 5000);
}

void ApplicationShell::scaleLevelObjectsFromGizmo(LevelComponentGizmo* gizmo, MapViewport* plan)
{
	if (plan) {
		activateLevelPlanViewport(plan);
	}
	// The manipulator already snapped the factors; Scale Selection's service
	// applies them about the drag's pivot with Texture Scale Lock, so locks,
	// linked groups, Repeat and undo behave alike.
	const LevelMapVec3 factors = gizmo->request().scale;
	LevelObjectScaleRequest request;
	request.factors = {factors.x, factors.y, factors.z};
	request.pivot = gizmo->pivot();
	request.textures = {m_settings.levelTextureScaleLock(), m_settings.levelAllowValve220()};
	LevelMapScaleRequest scale;
	LevelObjectScaleReport report;
	QString error;
	if (!levelObjectScale(levelObjectManipulatorTarget(m_levelMapDocument), request, &scale, &report, &error)) {
		statusBar()->showMessage(tr("Selection unchanged: %1").arg(error), 6000);
		refreshLevelMapViewport();
		return;
	}
	LevelPlacementRequest work;
	work.operation = LevelPlacementOperation::Scale;
	work.scale = scale;
	if (!runLevelPlacementFromUi(work, &error)) {
		if (!error.isEmpty()) {
			statusBar()->showMessage(tr("Selection unchanged: %1").arg(error), 6000);
		}
		refreshLevelMapViewport();
		return;
	}
	const QString by = tr("%1 × %2 × %3").arg(scale.factors[0], 0, 'g', 6).arg(scale.factors[1], 0, 'g', 6).arg(scale.factors[2], 0, 'g', 6);
	recordActivity(tr("Level map selection scaled"), by, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	refreshLevelObjectGizmos();
	statusBar()->showMessage(tr("Scaled %n object(s) by %1. Undo restores them.", nullptr, report.objects).arg(by), 5000);
}

void ApplicationShell::toggleLevelRotateOrScaleTool()
{
	// NetRadiant Custom's MouseRotateOrScale: Scale from Rotate, Rotate from
	// anything else; in a component mode, the component manipulators.
	if (levelComponentsActive()) {
		setLevelComponentTool(m_levelComponentTool == 1 ? 2 : 1);
		return;
	}
	chooseLevelTool(m_levelObjectTool == static_cast<int>(LevelObjectTool::Rotate) ? QStringLiteral("scale") : QStringLiteral("rotate"));
}

void ApplicationShell::toggleLevelSelectOrTransformTool()
{
	// NetRadiant Custom's MouseDragOrTransform: Transform from its Drag tool,
	// Drag from anything else. Its component modes drag components too, so
	// there the command alternates Move and Shear Components.
	if (levelComponentsActive()) {
		setLevelComponentTool(m_levelComponentTool == 3 ? 0 : 3);
		return;
	}
	chooseLevelTool(currentLevelTool() == QLatin1String("select") ? QStringLiteral("transform") : QStringLiteral("select"));
}

void ApplicationShell::shearLevelObjectsFromView(int axis, int side, const LevelMapVec3& offset, const LevelMapVec3& pivot, MapViewport* plan)
{
	if (plan) {
		activateLevelPlanViewport(plan);
	}
	LevelObjectShearRequest request;
	request.axis = axis;
	request.side = side;
	request.offset = offset;
	request.pivot = pivot;
	request.textures = {m_settings.levelTextureLock(), m_settings.levelAllowValve220()};
	LevelObjectShearReport report;
	QString error;
	bool unlocked = false;
	if (!shearLevelMapObjects(&m_levelMapDocument, request, &report, &error)) {
		// Classic face formats cannot slant a texture with its face; the tool
		// shears the geometry anyway rather than refuse the drag.
		QString retry;
		LevelObjectShearRequest plain = request;
		plain.textures = {false, false};
		if (!request.textures.enabled || !shearLevelMapObjects(&m_levelMapDocument, plain, &report, &retry)) {
			statusBar()->showMessage(tr("Selection unchanged: %1").arg(error), 6000);
			refreshLevelMapViewport();
			return;
		}
		unlocked = true;
	}
	const QString slide = tr("%1, %2, %3").arg(offset.x, 0, 'g', 8).arg(offset.y, 0, 'g', 8).arg(offset.z, 0, 'g', 8);
	recordActivity(tr("Level map selection sheared"), slide, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	refreshLevelObjectGizmos();
	refreshLevelCameraResize();
	statusBar()->showMessage(unlocked
			? tr("Sheared %n object(s): the side slid %1, without texture lock, as this map's face format cannot slant textures with their faces.",
				  nullptr, report.objects).arg(slide)
			: tr("Sheared %n object(s): the side slid %1. Undo restores them.", nullptr, report.objects).arg(slide),
		5000);
}

void ApplicationShell::editLevelObjectPivotFromUi()
{
	if (!quakeFamily(m_levelMapDocument) || m_levelMapDocument.selection.isEmpty()) {
		statusBar()->showMessage(tr("Select map objects before setting the manipulator pivot."), 5000);
		return;
	}
	const auto loadSerial = m_levelMapLoadSerial;
	const LevelObjectManipulatorTarget target = levelObjectManipulatorTarget(m_levelMapDocument);
	const LevelMapVec3 pivot = levelObjectManipulatorPivot(target, m_levelObjectPivot);
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("levelObjectPivotDialog"));
	dialog.setWindowTitle(tr("Object Pivot"));
	dialog.setAccessibleName(dialog.windowTitle());
	auto* layout = new QVBoxLayout(&dialog);
	auto* form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	layout->addLayout(form);
	auto* placed = new QCheckBox(tr("Place the pivot at a typed point"));
	placed->setObjectName(QStringLiteral("levelObjectPivotPlaced"));
	placed->setAccessibleName(placed->text());
	placed->setChecked(m_levelObjectPivot.valid);
	form->addRow(placed);
	const auto number = [form, placed](const QString& label, const QString& name, double value) {
		auto* field = new QDoubleSpinBox;
		field->setObjectName(name);
		field->setRange(-131072, 131072);
		field->setDecimals(4);
		field->setValue(std::isfinite(value) ? value : 0.0);
		field->setAccessibleName(label);
		// Coordinates read left to right in every layout.
		field->setLayoutDirection(Qt::LeftToRight);
		field->setEnabled(placed->isChecked());
		QObject::connect(placed, &QCheckBox::toggled, field, &QWidget::setEnabled);
		form->addRow(label, field);
		return field;
	};
	auto* x = number(tr("Pivot X"), QStringLiteral("levelObjectPivotX"), pivot.x);
	auto* y = number(tr("Pivot Y"), QStringLiteral("levelObjectPivotY"), pivot.y);
	auto* z = number(tr("Pivot Z"), QStringLiteral("levelObjectPivotZ"), pivot.z);
	auto* note = new QLabel(tr("The pivot is shared by the plan views and the camera. Unchecked, it follows the centre of the selection. "
		"Placing it does not edit the map."));
	note->setWordWrap(true);
	form->addRow(note);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted || loadSerial != m_levelMapLoadSerial) {
		return;
	}
	m_levelObjectPivot = placed->isChecked() ? LevelMapVec3 {x->value(), y->value(), z->value(), true} : LevelMapVec3 {};
	refreshLevelObjectGizmos();
	refreshLevelObjectGizmoCommands();
	statusBar()->showMessage(m_levelObjectPivot.valid
			? tr("Pivot placed at %1, %2, %3.").arg(m_levelObjectPivot.x).arg(m_levelObjectPivot.y).arg(m_levelObjectPivot.z)
			: tr("The manipulator pivot follows the centre of the selection."),
		5000);
}

} // namespace vibestudio
