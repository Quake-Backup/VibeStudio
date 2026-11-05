// The Surfaces tab's Face section: TrenchBroom's Face inspector. The one
// picked face over its material image with move, turn and stretch handles
// about a pivot, and the offset, scale and rotation of the picked faces, or
// of the Surfaces target's brush faces, each change one undo step through
// core/level_face_attributes.h, the service `map face-attributes` uses.
// Show Face Inspector opens the tab on that section and never hides it, as
// TrenchBroom's View > Switch to Face Inspector does (Ctrl+3 in its profile,
// anywhere in Levels).
//
// Workflow reference: TB FaceAttribsEditor, UVView and ActionManager
// "Menu/View/Switch to Face Inspector" at 4c756be1 (GPL-3.0-or-later).
// Original Qt implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/level_surface_tools.h"
#include "app/level_uv_view.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "app/studio_sidebar.h"
#include "core/level_face_attributes.h"
#include "core/level_uv_edit.h"

#include <QApplication>
#include <QStatusBar>

namespace vibestudio {
namespace {

QString materialKey(QString name)
{
	return name.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/')).toCaseFolded();
}

} // namespace

void ApplicationShell::registerLevelFaceInspectorCommands()
{
	StudioCommandRegistration command;
	command.commandId = QStringLiteral("map.showFaceInspector");
	command.label = tr("Show Face Inspector");
	command.group = StudioCommandGroup::View;
	command.menuSection = tr("Level Sidebars");
	command.iconName = QStringLiteral("uv");
	command.statusTip = tr("Show the Surfaces tab at its Face section: the picked face over its texture with move, turn and stretch handles, and "
						   "the offset, scale and rotation of the targeted faces.");
	command.handler = [this] { showLevelFaceInspector(); };
	m_commands->registerCommand(command);
}

void ApplicationShell::showLevelFaceInspector()
{
	StudioSidebar* sidebar = levelSidebarHolding(QStringLiteral("surfaces"));
	if (!sidebar || !m_levelSurfaceTools) {
		return;
	}
	// Show Surfaces can still go back to the tab this one replaced.
	if (sidebar->isFolded() || sidebar->currentPageId() != QLatin1String("surfaces")) {
		m_levelSurfacesReturnTab = sidebar->isFolded() ? QString() : sidebar->currentPageId();
	}
	// From a view's key the view keeps the keyboard; from a menu or command
	// search focus moves into the face's texture space.
	QWidget* focused = QApplication::focusWidget();
	const bool fromView = qobject_cast<MapViewport*>(focused) || qobject_cast<ModelViewport*>(focused);
	setMode(StudioMode::Levels);
	showLevelSidebarPage(QStringLiteral("surfaces"), QStringLiteral("surfaces.face"));
	refreshLevelFaceInspector();
	LevelUvView* view = m_levelSurfaceTools->faceView();
	m_levelSurfaceTools->ensureWidgetVisible(view, 0, 0);
	if (!fromView) {
		view->setFocus(Qt::ShortcutFocusReason);
	}
	refreshLevelSurfacesToggle();
}

void ApplicationShell::refreshLevelFaceInspector()
{
	// The camera's UV tool follows the same picks and preview images.
	refreshLevelCameraUvTool();
	if (!m_levelSurfaceTools || !m_commands) {
		return;
	}
	const bool quake = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	QVector<LevelSurfaceFace> faces;
	QVector<int> patches;
	if (quake) {
		levelSurfaceQueueTargets(true, &faces, &patches);
	}
	const QHash<QString, QSize> sizes = levelPreviewTextureSizes(m_levelPreviewAssets);
	LevelFaceAttributesSummary summary;
	if (faces.size() <= 16384) {
		summary = levelFaceAttributesSummary(m_levelMapDocument, faces, sizes);
	}
	const bool editable = quake && summary.faces > 0 && faces.size() <= 16384 && !levelSurfaceEditsPending() && !levelSurfaceGeometryBusy();
	m_levelSurfaceTools->setFaceAttributes(summary, static_cast<int>(patches.size()), editable);

	// The view: the one targeted face, over the image the Levels preview read.
	if (!quake) {
		m_levelSurfaceTools->setFaceLayout(nullptr, {}, tr("The Face section shows brush faces of Quake, Quake II and Quake III maps."));
		return;
	}
	if (faces.size() != 1) {
		const int count = static_cast<int>(faces.size());
		m_levelSurfaceTools->setFaceLayout(nullptr, {}, count == 0 ? tr("Pick one brush face to see it over its texture.")
			: tr("%n faces are targeted. Pick one to see it over its texture; the fields set them all.", nullptr, count));
		return;
	}
	const LevelMaterialTarget target {LevelMaterialKind::BrushFace, faces.first().brushId, faces.first().faceIndex};
	if (const QString problem = levelUvTargetProblem(m_levelMapDocument, target); !problem.isEmpty()) {
		m_levelSurfaceTools->setFaceLayout(nullptr, {}, problem);
		return;
	}
	QString material;
	for (const LevelMapBrush& brush : std::as_const(m_levelMapDocument.brushes)) {
		if (brush.id == target.objectId) {
			material = brush.faces.value(target.faceIndex).textureName;
		}
	}
	QImage image;
	QSize size;
	const QString key = materialKey(material);
	for (const LevelPreviewMaterial& candidate : std::as_const(m_levelPreviewAssets.materials)) {
		if (candidate.key == key && candidate.ready()) {
			image = candidate.image;
			size = candidate.sourceSize;
		}
	}
	LevelUvLayout layout;
	QString error;
	if (!readLevelUvLayout(m_levelMapDocument, target, size, &layout, &error)) {
		m_levelSurfaceTools->setFaceLayout(nullptr, {}, error);
		return;
	}
	m_levelSurfaceTools->setFaceLayout(&layout, image, {});
	m_levelSurfaceTools->faceView()->setEnabled(editable);
}

bool ApplicationShell::applyLevelFaceAttributesFromUi(const LevelFaceAttributesEdit& edit, bool fromView, QString* error)
{
	if (error) {
		error->clear();
	}
	const auto fail = [this, error](const QString& message) {
		if (error) {
			*error = message;
		}
		// Puts back any field or handle the change moved.
		refreshLevelFaceInspector();
		return false;
	};
	if (!m_levelSurfaceTools) {
		return fail(tr("Surface tools are unavailable."));
	}
	if (levelSurfaceEditsPending() || levelSurfaceGeometryBusy()
		|| (m_levelMap3D && (m_levelMap3D->surfaceStrokeActive() || m_levelMap3D->isDrawingBrush()))) {
		return fail(tr("Finish or cancel the pending level edit before changing texture values."));
	}
	QVector<LevelSurfaceFace> faces;
	QVector<int> patches;
	if (fromView) {
		// The handles change the face the view shows, and only that one.
		const LevelUvView* view = m_levelSurfaceTools->faceView();
		if (!view->hasSource() || view->layout().isPatch()) {
			return fail(tr("Pick one brush face to change it in the Face section."));
		}
		faces = {LevelSurfaceFace {view->layout().target.objectId, view->layout().target.faceIndex}};
	} else {
		levelSurfaceQueueTargets(true, &faces, &patches);
	}
	if (faces.isEmpty()) {
		return fail(tr("Select brushes or pick faces to set their texture values."));
	}
	if (faces.size() > 16384) {
		return fail(tr("The Face section changes at most 16,384 brush faces at once."));
	}
	LevelFaceAttributesReport report;
	QString problem;
	if (!setLevelMapFaceAttributes(&m_levelMapDocument, faces, edit, levelPreviewTextureSizes(m_levelPreviewAssets), &report, &problem)) {
		return fail(problem);
	}
	QString message;
	if (report.changedFaces > 0) {
		recordActivity(tr("Texture values changed"), tr("%n brush face(s)", nullptr, report.changedFaces), QStringLiteral("level-map"),
			OperationState::Warning, tr("Unsaved map edit"));
		refreshLevelMapWorkbench();
		message = tr("Set the texture values of %n brush face(s). The change is one undo step.", nullptr, report.changedFaces);
	} else {
		refreshLevelFaceInspector();
		message = tr("The faces already have these texture values; no undo step was added.");
	}
	m_levelSurfaceTools->setStatus(message, false);
	statusBar()->showMessage(message, 6000);
	return true;
}

} // namespace vibestudio
