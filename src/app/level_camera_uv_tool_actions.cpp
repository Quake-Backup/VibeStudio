// The camera's UV tool in the shell: NetRadiant Custom's UV tool (G) on the
// Levels tool bar. It works on the UV Editor's target (one picked brush face,
// or one selected patch), previews each drag live in the camera and commits
// it as one undo step through core/level_uv_tool.h, which `map edit-uv`'s
// stretch and skew options share.
// Workflow reference: NRC radiant/tools.cpp MouseUV (G, and G again returns
// to the default tool) and radiant/selection_mtor_uv.cpp UVManipulator at
// 449778b3 (GPL-2.0-or-later). Original Qt implementation; see
// docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/level_camera_uv_tool.h"
#include "app/level_preview_worker.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/editor_profiles.h"
#include "core/level_scene.h"
#include "core/level_texture_mapping.h"
#include "core/level_uv_tool.h"
#include "core/map_geometry.h"

#include <QAction>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QStatusBar>

#include <cmath>
#include <cstring>
#include <limits>

namespace vibestudio {
namespace {

bool quakeFamily(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
}

QString materialKey(QString name)
{
	return name.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/')).toCaseFolded();
}

// A camera vertex's exact position, to find the patch point it came from.
QByteArray positionKey(const ModelVec3& p)
{
	QByteArray key(12, Qt::Uninitialized);
	std::memcpy(key.data(), &p.x, 4);
	std::memcpy(key.data() + 4, &p.y, 4);
	std::memcpy(key.data() + 8, &p.z, 4);
	return key;
}

} // namespace

void ApplicationShell::registerLevelCameraUvToolCommands()
{
	StudioCommandRegistration command;
	command.commandId = QStringLiteral("map.uvTool");
	command.label = tr("UV Tool");
	command.group = StudioCommandGroup::Edit;
	command.menuSection = tr("Manipulators");
	command.iconName = QStringLiteral("uv");
	command.checkable = true;
	command.statusTip = tr("Move, turn, stretch and skew the picked face's or the selected patch's texture on the surface in the camera, about a "
						   "pivot you can place, and move a patch's control points in texture space; each drag is one undo step. Choose it again "
						   "to return to Select.");
	command.handler = [this] {
		setMode(StudioMode::Levels);
		chooseLevelTool(m_levelUvToolOn ? QStringLiteral("select") : QStringLiteral("uv"));
	};
	m_commands->registerCommand(command);
	m_commands->setEnabled(command.commandId, false);
}

const LevelCameraUvTool* ApplicationShell::levelCameraUvTool() const
{
	return m_levelUvToolOn ? m_levelUvTool.get() : nullptr;
}

bool ApplicationShell::levelCameraUvToolActive() const
{
	return m_levelUvToolOn && quakeFamily(m_levelMapDocument);
}

QString ApplicationShell::levelCameraControlsHelp() const
{
	const QString base = levelControlsAccessibleText(QStringLiteral("camera"), tr("3D view"));
	if (!m_levelUvToolOn || !m_levelUvTool) {
		return base;
	}
	return tr("%1 UV Tool: drag the surface to move its texture, the ring to turn it, a repeat line to stretch it, Alt and a line to skew it, "
			  "a patch's control point, row or column to move its texture coordinates, and the pivot to place it; Shift and Ctrl snap, and "
			  "Escape puts a drag back. %2")
		.arg(base, m_levelUvTool->describe());
}

void ApplicationShell::setLevelCameraUvTool(bool enabled)
{
	if (enabled && (!quakeFamily(m_levelMapDocument) || !m_levelMap3D)) {
		statusBar()->showMessage(tr("The UV tool works on brush faces and patches. Open a Quake, Quake II or Quake III map."), 6000);
		enabled = false;
	}
	if (m_levelUvTool && !enabled) {
		m_levelUvTool->cancelDrag();
		m_levelUvTool->setHover({});
	}
	if (m_levelMap3D && !enabled) {
		m_levelMap3D->clearTexCoordPreview();
	}
	const bool changed = enabled != m_levelUvToolOn;
	m_levelUvToolOn = enabled;
	if (enabled) {
		if (!levelMap3DShowing()) {
			setLevelMap3D(true);
		}
		if (!m_levelUvTool) {
			m_levelUvTool = std::make_shared<LevelCameraUvTool>(m_levelMap3D);
		}
	}
	refreshLevelCameraUvTool();
	// The UV tool replaces the camera's resize box while it is on.
	refreshLevelCameraResize();
	if (m_levelMap3D) {
		m_levelMap3D->setControlsHelp(levelCameraControlsHelp());
		m_levelMap3D->update();
	}
	if (enabled && changed) {
		statusBar()->showMessage(m_levelUvTool->hasSource()
				? tr("UV Tool: drag the surface to move the texture, the ring to turn it and a repeat line to stretch it; Alt and a line skews.")
				: m_levelUvTool->describe(),
			8000);
	}
}

void ApplicationShell::refreshLevelCameraUvTool()
{
	if (!m_commands) {
		return;
	}
	const bool open = quakeFamily(m_levelMapDocument) && m_levelMap3D;
	if (!open && m_levelUvToolOn) {
		// The map closed or is not a brush map: the tool turns off.
		m_levelUvToolOn = false;
		if (m_levelUvTool) {
			m_levelUvTool->cancelDrag();
		}
		if (m_levelMap3D) {
			m_levelMap3D->clearTexCoordPreview();
			m_levelMap3D->setControlsHelp(levelCameraControlsHelp());
		}
	}
	m_commands->setEnabled(QStringLiteral("map.uvTool"), open);
	if (QAction* action = m_commands->action(QStringLiteral("map.uvTool"))) {
		const QSignalBlocker blocker(action);
		action->setChecked(m_levelUvToolOn);
	}
	if (!m_levelUvToolOn || !m_levelUvTool) {
		return;
	}
	// Shift turns in fixed 15° steps, as NRC's UVManipulator snaps its ring
	// to pi/12 whatever the Surface Inspector's rotate increment is.
	m_levelUvTool->setAngleStep(15);
	m_levelUvTool->setAllowValve220(m_settings.levelAllowValve220());
	if (m_levelUvTool->dragging()) {
		// A drag holds the surface it began on; a map that changed under it
		// ends the drag with nothing applied.
		if (m_levelMapDocument.revision == m_levelUvToolRevision && m_levelMapLoadSerial == m_levelUvToolLoad) {
			return;
		}
		m_levelUvTool->cancelDrag();
		m_levelMap3D->clearTexCoordPreview();
	}
	// The UV Editor's target: one picked face, or else one selected patch.
	LevelMaterialTarget target;
	QString reason;
	const QVector<LevelSurfaceFace> faces = levelSurfaceFaceTargets();
	if (faces.size() == 1) {
		target = {LevelMaterialKind::BrushFace, faces.first().brushId, faces.first().faceIndex};
	} else if (faces.isEmpty() && m_levelMapDocument.selection.size() == 1
		&& m_levelMapDocument.selection.first().kind == LevelMapSelectionKind::QuakePatch) {
		target = {LevelMaterialKind::Patch, m_levelMapDocument.selection.first().objectId, 0};
	} else {
		reason = faces.size() > 1 ? tr("pick just one brush face for the UV tool.") : tr("click a brush face or a patch in the camera to edit its texture.");
	}
	if (reason.isEmpty()) {
		reason = levelUvTargetProblem(m_levelMapDocument, target);
	}
	if (!reason.isEmpty()) {
		m_levelUvTool->clearSource(reason);
	} else {
		QString material;
		if (target.kind == LevelMaterialKind::BrushFace) {
			for (const LevelMapBrush& brush : std::as_const(m_levelMapDocument.brushes)) {
				if (brush.id == target.objectId) {
					material = brush.faces.value(target.faceIndex).textureName;
				}
			}
		} else {
			for (const LevelMapPatch& patch : std::as_const(m_levelMapDocument.patches)) {
				if (patch.id == target.objectId) {
					material = patch.textureName;
				}
			}
		}
		// The image size the Levels preview read.
		QSize size;
		const QString key = materialKey(material);
		for (const LevelPreviewMaterial& candidate : std::as_const(m_levelPreviewAssets.materials)) {
			if (candidate.key == key && candidate.ready()) {
				size = candidate.sourceSize;
			}
		}
		// While the camera reloads its materials after an edit, the surface
		// keeps the size it had, so the pivot and the last drag stay.
		if (!size.isValid() && m_levelUvTool->hasSource() && m_levelUvTool->layout().target == target
			&& materialKey(m_levelUvTool->layout().material) == key) {
			size = m_levelUvTool->layout().imageSize;
		}
		LevelUvLayout layout;
		QString error;
		if (readLevelUvLayout(m_levelMapDocument, target, size, &layout, &error)) {
			m_levelUvTool->setSource(layout);
		} else if (size.isValid()) {
			m_levelUvTool->clearSource(error);
		} else if (m_levelPreviewWorker && m_levelPreviewWorker->busy()) {
			m_levelUvTool->clearSource(tr("%1 Wait for the camera to load the material's image.").arg(error));
		} else {
			// The camera finished without an image for this material (missing,
			// unreadable or not in the open packages), so none will arrive.
			m_levelUvTool->clearSource(tr("%1 The camera has no image for %2, so its size is unknown. Use the UV Editor with a texture size, "
										  "or map edit-uv --texture-size.")
					.arg(error, material.trimmed().isEmpty() ? tr("this material") : material.trimmed()));
		}
	}
	if (m_levelMap3D) {
		m_levelMap3D->setControlsHelp(levelCameraControlsHelp());
		m_levelMap3D->update();
	}
}

void ApplicationShell::paintLevelCameraUvTool(QPainter& painter) const
{
	if (m_levelUvToolOn && m_levelUvTool && m_levelMap3D && !m_levelMap3D->isLooking()) {
		m_levelUvTool->paint(painter);
	}
}

bool ApplicationShell::pickLevelCameraUvToolSurface(int triangle)
{
	if (triangle < 0 || triangle >= m_levelMap3DOwners.size()) {
		return false;
	}
	const LevelMapSelectionRef owner = m_levelMap3DOwners.at(triangle);
	const int face = m_levelMap3DFaces.value(triangle, -1);
	const bool brushFace = owner.kind == LevelMapSelectionKind::QuakeBrush && face >= 0;
	if (!brushFace && owner.kind != LevelMapSelectionKind::QuakePatch) {
		return false;
	}
	const LevelMaterialTarget clicked = brushFace ? LevelMaterialTarget {LevelMaterialKind::BrushFace, owner.objectId, face}
												  : LevelMaterialTarget {LevelMaterialKind::Patch, owner.objectId, 0};
	if (m_levelUvTool->hasSource() && m_levelUvTool->layout().target == clicked) {
		return false;
	}
	const auto context = levelSceneSelectionContext(m_levelMapDocument);
	const QString object = levelMapSelectionRefId(owner);
	if (context.closedGroups.contains(object)) {
		// A closed group picks as a whole, as the camera always picks it.
		return false;
	}
	if (!context.allowedObjects.contains(object)) {
		statusBar()->showMessage(tr("%1 is outside the open group. Close the group to edit its texture.").arg(object), 6000);
		return true;
	}
	// As a face pick in the camera: the brush is selected with that face.
	QString error;
	if (brushFace) {
		m_levelPickedFaces = {LevelSurfaceFace {owner.objectId, face}};
		if (!setLevelMapSelection(&m_levelMapDocument, {owner}, &error)) {
			m_levelPickedFaces.clear();
			statusBar()->showMessage(error, 6000);
			return true;
		}
		m_levelPickedSelection = m_levelMapDocument.selection;
	} else {
		m_levelPickedFaces.clear();
		if (!setLevelMapUserSelection({owner}, &error)) {
			statusBar()->showMessage(error, 6000);
			return true;
		}
	}
	refreshLevelMapWorkbench();
	if (m_levelUvTool) {
		statusBar()->showMessage(m_levelUvTool->describe(), 6000);
	}
	return true;
}

void ApplicationShell::previewLevelCameraUvToolDrag()
{
	const LevelCameraUvTool& tool = *m_levelUvTool;
	statusBar()->showMessage(tool.dragSummary());
	if (tool.dragHit().handle == LevelUvToolHandle::Pivot || !tool.previewReady() || m_levelUvToolTriangles.isEmpty()) {
		m_levelMap3D->clearTexCoordPreview();
		m_levelMap3D->update();
		return;
	}
	// The camera shows the texture where the drag puts it.
	const LevelUvLayout& layout = tool.layout();
	const LevelUvToolResult& preview = tool.preview();
	if (layout.isPatch()) {
		const QVector<QVector<LevelMapVec3>> grid = tessellatePatchMesh(preview.patch, 3);
		const QVector<QVector<QPointF>> uv = tessellatePatchTexCoords(preview.patch, 3);
		QHash<QByteArray, QPointF> lookup;
		for (qsizetype row = 0; row < grid.size() && row < uv.size(); ++row) {
			for (qsizetype column = 0; column < grid[row].size() && column < uv[row].size(); ++column) {
				const LevelMapVec3& p = grid[row][column];
				lookup.insert(positionKey({static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)}), uv[row][column]);
			}
		}
		m_levelMap3D->setTexCoordPreview(m_levelUvToolTriangles, [lookup](const ModelVec3& position) {
			return lookup.value(positionKey(position), QPointF(std::numeric_limits<double>::quiet_NaN(), 0));
		});
	} else {
		const LevelTextureProjection projection = levelTextureProjection(preview.face);
		const QSize size = layout.imageSize;
		if (!projection.valid || (!projection.normalizedCoordinates && !size.isValid())) {
			m_levelMap3D->clearTexCoordPreview();
		} else {
			m_levelMap3D->setTexCoordPreview(m_levelUvToolTriangles, [projection, size](const ModelVec3& position) {
				const QPointF at = projection.at({position.x, position.y, position.z, true});
				return projection.normalizedCoordinates ? at : QPointF(at.x() / size.width(), at.y() / size.height());
			});
		}
	}
	m_levelMap3D->update();
}

void ApplicationShell::commitLevelCameraUvToolDrag()
{
	LevelCameraUvTool& tool = *m_levelUvTool;
	const LevelUvToolHandle handle = tool.dragHit().handle;
	const LevelUvToolEdit edit = tool.edit();
	const LevelUvLayout layout = tool.layout();
	const bool moved = tool.finishDrag();
	const auto putBack = [this](const QString& message) {
		m_levelMap3D->clearTexCoordPreview();
		m_levelMap3D->update();
		if (!message.isEmpty()) {
			statusBar()->showMessage(message, 8000);
		}
	};
	if (!moved) {
		putBack({});
		return;
	}
	if (handle == LevelUvToolHandle::Pivot) {
		const QPointF pivot = tool.pivotTexel();
		putBack(tr("The pivot is at %1, %2 texels; turns, stretches and skews keep it in place.")
					.arg(QLocale().toString(pivot.x(), 'f', 2), QLocale().toString(pivot.y(), 'f', 2)));
		refreshLevelCameraUvTool();
		return;
	}
	if (!edit.changesSomething()) {
		putBack({});
		return;
	}
	if (m_levelMapDocument.revision != m_levelUvToolRevision || m_levelMapLoadSerial != m_levelUvToolLoad) {
		putBack(tr("The map changed during the drag, so the UV tool left the texture as it was."));
		refreshLevelCameraUvTool();
		return;
	}
	if (levelSurfaceEditsPending() || levelSurfaceGeometryBusy()) {
		putBack(tr("Finish or cancel the pending level edit before changing the texture."));
		return;
	}
	const quint64 revision = m_levelMapDocument.revision;
	LevelSurfaceEditPlan plan;
	LevelUvToolResult result;
	QString error;
	if (!prepareLevelUvToolEdit(m_levelMapDocument, layout, edit, &plan, &result, &error) || !commitLevelSurfaceEdit(&m_levelMapDocument, plan, &error)) {
		putBack(tr("The UV tool could not change the texture: %1").arg(error));
		refreshLevelCameraUvTool();
		return;
	}
	if (revision == m_levelMapDocument.revision) {
		putBack(tr("The texture already reads that way; no undo step was added."));
		refreshLevelCameraUvTool();
		return;
	}
	recordActivity(tr("Texture changed with the UV tool"), levelMaterialTargetId(layout.target), QStringLiteral("level-map"), OperationState::Warning,
		tr("Unsaved map edit"));
	// The camera keeps the preview until it draws the edited map.
	refreshLevelMapWorkbench();
	QString message = result.fit == LevelUvFit::Converted
		? tr("Changed the texture of %1 in one undo step; the classic face became a Valve 220 face to take the skew.").arg(levelMaterialTargetId(layout.target))
		: tr("Changed the texture of %1 in one undo step.").arg(levelMaterialTargetId(layout.target));
	statusBar()->showMessage(message, 6000);
}

bool ApplicationShell::handleLevelCameraUvToolEvent(QEvent* event)
{
	if (!m_levelUvToolOn || !m_levelUvTool || !m_levelMap3D) {
		return false;
	}
	LevelCameraUvTool& tool = *m_levelUvTool;
	switch (event->type()) {
	case QEvent::MouseButtonPress: {
		const auto* mouse = static_cast<QMouseEvent*>(event);
		if (mouse->button() != Qt::LeftButton || tool.dragging() || m_levelMap3D->isLooking()) {
			return false;
		}
		// Alt skews; other modifiers keep the camera's own gestures.
		const Qt::KeyboardModifiers modifiers = mouse->modifiers() & ~Qt::KeypadModifier;
		if (modifiers != Qt::NoModifier && modifiers != Qt::AltModifier) {
			return false;
		}
		const LevelCameraUvTool::Hit hit = tool.hitAt(mouse->position(), modifiers);
		if (hit.handle != LevelUvToolHandle::None) {
			if (hit.handle != LevelUvToolHandle::Pivot && (levelSurfaceEditsPending() || levelSurfaceGeometryBusy())) {
				statusBar()->showMessage(tr("Finish or cancel the pending level edit before changing the texture."), 6000);
				return true;
			}
			if (!tool.beginDrag(hit, mouse->position())) {
				return false;
			}
			m_levelUvToolRevision = m_levelMapDocument.revision;
			m_levelUvToolLoad = m_levelMapLoadSerial;
			m_levelUvToolTriangles.clear();
			for (int i = 0; i < m_levelPreviewMaterialTargets.size(); ++i) {
				if (m_levelPreviewMaterialTargets[i] == tool.layout().target) {
					m_levelUvToolTriangles.append(i);
				}
			}
			m_levelMap3D->setFocus(Qt::MouseFocusReason);
			statusBar()->showMessage(tool.hitHelp(hit));
			m_levelMap3D->update();
			return true;
		}
		if (modifiers != Qt::NoModifier) {
			return false;
		}
		// A click on another brush face or patch makes it the tool's surface.
		CameraSurfacePoint surface;
		int triangle = -1;
		return m_levelMap3D->surfacePointAt(mouse->position(), &surface, &triangle) && pickLevelCameraUvToolSurface(triangle);
	}
	case QEvent::MouseMove: {
		const auto* mouse = static_cast<QMouseEvent*>(event);
		if (tool.dragging()) {
			if (tool.updateDrag(mouse->position(), mouse->modifiers())) {
				previewLevelCameraUvToolDrag();
			}
			return true;
		}
		if (mouse->buttons() == Qt::NoButton) {
			const LevelCameraUvTool::Hit hit = tool.hitAt(mouse->position(), mouse->modifiers() & ~Qt::KeypadModifier);
			if (!(hit == tool.hover())) {
				tool.setHover(hit);
				m_levelMap3D->update();
				if (hit.handle != LevelUvToolHandle::None) {
					statusBar()->showMessage(tool.hitHelp(hit), 4000);
				}
			}
		}
		return false;
	}
	case QEvent::MouseButtonRelease: {
		const auto* mouse = static_cast<QMouseEvent*>(event);
		if (!tool.dragging()) {
			return false;
		}
		if (mouse->button() == Qt::LeftButton) {
			commitLevelCameraUvToolDrag();
		}
		return true;
	}
	case QEvent::Leave:
		if (!tool.dragging() && tool.hover().handle != LevelUvToolHandle::None) {
			tool.setHover({});
			m_levelMap3D->update();
		}
		return false;
	case QEvent::ShortcutOverride:
	case QEvent::KeyPress: {
		auto* key = static_cast<QKeyEvent*>(event);
		if (!tool.dragging() || key->key() != Qt::Key_Escape) {
			return false;
		}
		event->accept();
		if (event->type() == QEvent::KeyPress) {
			// Escape puts the texture back and leaves no step.
			tool.cancelDrag();
			m_levelMap3D->clearTexCoordPreview();
			m_levelMap3D->update();
			statusBar()->showMessage(tr("The UV tool put the texture back."), 4000);
		}
		return true;
	}
	default:
		break;
	}
	return false;
}

} // namespace vibestudio
