#pragma once

#include "core/level_uv_tool.h"

#include <QCoreApplication>
#include <QPointF>
#include <QString>
#include <QVector>

class QPainter;

namespace vibestudio {

class ModelViewport;

// The Levels camera's UV tool, NetRadiant Custom's G: one picked brush face
// or one selected patch with its texture's repeat lines laid over it in the
// camera, a pivot with a ring about it, and, for a patch, its control points
// and their rows and columns in texture space. It hit-tests, drags and draws;
// the shell commits each finished drag as one undo step through
// core/level_uv_tool.h.
//
// Mouse: drag the surface to move the texture, the ring to turn it, a repeat
// line to stretch it along U or V (where two lines cross, both), Alt and a
// line to skew it, the pivot to place it, and a patch's control point, row or
// column to move its texture coordinates. Shift keeps one direction, turns in
// steps or keeps the proportions; Ctrl always snaps (to a line, a point or an
// edge); otherwise snapping takes hold within a few pixels.
// Workflow reference: NRC radiant/selection_mtor_uv.cpp UVManipulator at
// 449778b3 (GPL-2.0-or-later). Original Qt implementation; see
// docs/CREDITS.md.
class LevelCameraUvTool final {
	Q_DECLARE_TR_FUNCTIONS(LevelCameraUvTool)
  public:
	struct Hit {
		LevelUvToolHandle handle = LevelUvToolHandle::None;
		// A patch control point, row or column.
		int index = -1;
		// The repeat lines a stretch or skew holds: s and t values.
		double lineS = 0, lineT = 0;
		[[nodiscard]] bool operator==(const Hit& other) const = default;
	};
	explicit LevelCameraUvTool(ModelViewport* camera);

	// Starts from `layout`. The pivot stays where it was on the same face or
	// patch; elsewhere it starts at the surface's lowest texel corner.
	void setSource(const LevelUvLayout& layout);
	void clearSource(const QString& reason = {});
	[[nodiscard]] bool hasSource() const { return m_hasSource; }
	[[nodiscard]] const LevelUvLayout& layout() const { return m_layout; }
	[[nodiscard]] QString reason() const { return m_reason; }
	[[nodiscard]] const LevelUvToolFrame& frame() const { return m_frame; }
	[[nodiscard]] const LevelUvToolGrid& grid() const { return m_grid; }
	[[nodiscard]] LevelMapVec3 pivotWorld() const { return m_pivot; }
	[[nodiscard]] QPointF pivotTexel() const;
	bool setPivotTexel(QPointF texel);
	void resetPivot();
	// Shift's turn step in degrees, and whether a classic face may become a
	// Valve 220 face to take a skew.
	void setAngleStep(double degrees) { m_angleStep = degrees; }
	void setAllowValve220(bool allow) { m_allowValve220 = allow; }

	// What a press at `screen` (widget coordinates) would take.
	[[nodiscard]] Hit hitAt(QPointF screen, Qt::KeyboardModifiers modifiers) const;
	// Where world points and texels land in the camera.
	bool screenPoint(const LevelMapVec3& world, QPointF* screen) const;
	[[nodiscard]] QPointF texelScreen(QPointF texel) const;
	// The texel under `screen`, on the surface's plane.
	bool texelUnder(QPointF screen, QPointF* texel) const;

	bool beginDrag(const Hit& hit, QPointF screen);
	// Follows the pointer; false while it has not left the press.
	bool updateDrag(QPointF screen, Qt::KeyboardModifiers modifiers);
	void cancelDrag();
	// Ends the drag; true when it moved, so its edit (or pivot) stands.
	bool finishDrag();
	[[nodiscard]] bool dragging() const { return m_drag.handle != LevelUvToolHandle::None; }
	[[nodiscard]] bool dragMoved() const { return m_moved; }
	[[nodiscard]] const Hit& dragHit() const { return m_drag; }
	[[nodiscard]] const LevelUvToolEdit& edit() const { return m_edit; }
	[[nodiscard]] bool previewReady() const { return m_previewReady; }
	[[nodiscard]] const LevelUvToolResult& preview() const { return m_preview; }
	[[nodiscard]] QString previewError() const { return m_previewError; }

	void setHover(const Hit& hit) { m_hover = hit; }
	[[nodiscard]] const Hit& hover() const { return m_hover; }
	// The drag's values, and what a drag of a hit would do, for the status bar.
	[[nodiscard]] QString dragSummary() const;
	[[nodiscard]] QString hitHelp(const Hit& hit) const;
	// The surface, pivot and handles in plain text, for the camera's
	// accessible description.
	[[nodiscard]] QString describe() const;

	void paint(QPainter& painter) const;

  private:
	void rebuild();
	[[nodiscard]] double pickRadius() const;
	[[nodiscard]] double ringRadiusWorld(const LevelUvToolFrame& frame, const LevelMapVec3& centre) const;
	[[nodiscard]] QVector<QPointF> ringScreen(const LevelUvToolFrame& frame, const LevelMapVec3& centre) const;
	[[nodiscard]] LevelUvToolSnap snapAt(QPointF texel, bool hard) const;
	bool segmentScreen(const LevelMapVec3& a, const LevelMapVec3& b, QPointF* screenA, QPointF* screenB) const;

	ModelViewport* m_camera = nullptr;
	LevelUvLayout m_layout;
	bool m_hasSource = false;
	QString m_reason;
	LevelUvToolFrame m_frame;
	LevelUvToolGrid m_grid;
	QVector<QPair<QPointF, QPointF>> m_edges;
	LevelMapVec3 m_pivot;
	double m_angleStep = 15;
	bool m_allowValve220 = false;
	Hit m_hover, m_drag;
	QPointF m_press, m_startTexel;
	bool m_moved = false;
	LevelUvToolEdit m_edit;
	LevelUvToolResult m_preview;
	bool m_previewReady = false;
	QString m_previewError;
};

} // namespace vibestudio
