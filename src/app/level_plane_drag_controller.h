#pragma once
#include "app/level_component_gizmo.h"
#include "core/level_plane_drag.h"
#include "core/model_transform.h"

#include <QObject>
#include <functional>
#include <QPair>
#include <QString>
#include <QVector>

class QPainter;
namespace vibestudio {

// The interactive side of core/level_plane_drag.h, shared by the 2D views and
// the camera: NetRadiant Custom's drag-tool gestures (the QE4 plane drag beside
// the selection, Alt plane and vertex drags, Ctrl+Alt extrusion) and the Build
// tool's face drags and copy chains. A press picks; each move previews the
// result synchronously from the brushes the pick copied, and release asks for
// the edit through commitRequested() or chainRequested(), which the shell
// applies as one undo step. The controller never edits a document.
//
// Workflow reference: NetRadiant Custom radiant/selection_mtor_drag.cpp
// DragManipulator and radiant/tools.cpp MouseBuild at 449778b3
// (GPL-2.0-or-later). Original Qt implementation; see docs/CREDITS.md.
class LevelPlaneDragController final : public QObject {
	Q_OBJECT
public:
	explicit LevelPlaneDragController(QObject* parent = nullptr) : QObject(parent) {}

	// The shell's check of the selection, asked at each press: empty when
	// its brushes can be dragged here, otherwise why not. Then the snapping
	// grid (0 while Snap is off) and Texture Lock for extrusions.
	void setContext(std::function<QString()> availability, double grid, bool textureLock);

	// Picks for `kind` under `point` and, when something is picked, starts the
	// drag (nothing moves until the pointer does). False leaves the controller
	// idle with lastError() saying why. `tolerancePixels` is how near the
	// pointer a vertex counts as under it.
	bool begin(LevelPlaneDragKind kind, const QPointF& point, const LevelComponentGizmoFrame& frame, const LevelMapDocument& document,
		const QVector<LevelMapSelectionRef>& selection, double tolerancePixels = 6);
	// The Build tool's chain of copies of a selection whose box is
	// `mins`..`maxs`, laid along the drag (core/level_plane_drag.h
	// levelCloneChain()).
	bool beginChain(const QPointF& point, const LevelComponentGizmoFrame& frame, const LevelMapVec3& mins, const LevelMapVec3& maxs);
	bool updateDrag(const QPointF& point);
	// Commit asks for the edit when the drag changed something and its
	// preview held; otherwise, and on cancel, nothing happens.
	void finish(bool commit);

	[[nodiscard]] bool active() const { return m_active; }
	[[nodiscard]] bool chain() const { return m_chain; }
	// The pointer has moved the drag off its start.
	[[nodiscard]] bool moved() const;
	[[nodiscard]] LevelPlaneDragKind kind() const { return m_pick.kind; }
	[[nodiscard]] const LevelPlaneDragPick& pick() const { return m_pick; }
	[[nodiscard]] LevelPlaneDragRequest request() const;
	[[nodiscard]] const LevelCloneChain& cloneChain() const { return m_cloneChain; }
	[[nodiscard]] const QVector<QPair<LevelMapVec3, LevelMapVec3>>& previewEdges() const { return m_edges; }
	[[nodiscard]] const LevelPlaneDragReport& previewReport() const { return m_report; }
	[[nodiscard]] QString lastError() const { return m_error; }
	// What the drag is doing, for the status line and screen readers.
	[[nodiscard]] QString summary() const;
	// The picked planes, heavy, and the dashed result of the drag.
	void paint(QPainter& painter, const LevelComponentGizmoFrame& frame, bool highContrast) const;

Q_SIGNALS:
	void activeChanged(bool active);
	void commitRequested(const vibestudio::LevelPlaneDragRequest& request);
	void chainRequested(const vibestudio::LevelMapVec3& step, int copies);
	void changed();

private:
	void reset();
	void refreshPreview();

	std::function<QString()> m_availability;
	bool m_textureLock = false;
	double m_grid = 0;
	bool m_active = false, m_chain = false, m_axisDrag = false;
	LevelComponentGizmoFrame m_frame;
	ModelMoveDrag m_drag;
	QPointF m_press;
	LevelPlaneDragPick m_pick;
	LevelMapVec3 m_translation {0, 0, 0, true};
	double m_distance = 0;
	LevelMapVec3 m_mins, m_maxs;
	LevelCloneChain m_cloneChain;
	QVector<QPair<LevelMapVec3, LevelMapVec3>> m_picked, m_edges;
	LevelPlaneDragReport m_report;
	QString m_error;
};

} // namespace vibestudio
