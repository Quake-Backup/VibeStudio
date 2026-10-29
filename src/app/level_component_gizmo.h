#pragma once

#include "core/level_brush.h"
#include "core/model_transform.h"
#include <QObject>
#include <QPolygonF>
#include <QFont>
#include <array>
#include <functional>

class QPainter;
namespace vibestudio {
// The Levels manipulator's tools. Move, Rotate, Scale, Shear and Pivot act on
// picked components. Translate is NetRadiant Custom's object Translate tool
// (core/level_object_manipulator.h): X, Y and Z arrows (handles 0 to 2), a
// square that moves in the view plane (3) and the shared pivot point (4).
// An object manipulator is the same controller with setObjectContext(). With
// it, Rotate is NetRadiant Custom's object Rotate tool: X, Y and Z rings (0 to
// 2, a ring seen face-on left out), an outer ring that turns about the line of
// sight (3), the pivot point (4) and a free turn inside the rings (5, left
// out with setFreeTurn(false), as TrenchBroom has none). Scale is NetRadiant
// Custom's object Scale tool: X, Y and Z handles (0 to 2), a square that
// scales across the view (3) and the pivot point (4). Transform is NetRadiant
// Custom's object Transform tool, a manipulator on the selection's box: the
// pivot point (4), a free move inside the box (6), side lines that skew
// (10 to 21), corner points that turn (30 to 41) and a scale from just
// outside a side or a corner (50 and up); see transformKindOf().
enum class LevelComponentTool { Move, Rotate, Scale, Shear, Pivot, Translate, Transform };
// What an object Transform drag does, by the handle it began on.
enum class LevelObjectTransformKind { None, Pivot, Move, Skew, Rotate, Scale };
struct LevelComponentGizmoFrame {
	std::function<QPointF(const LevelMapVec3&)> project;
	std::function<ModelPickRay(const QPointF&)> ray;
	std::function<bool(const LevelMapVec3&)> visible;
	ModelVec3 viewDirection;
	double radius = 64;
	bool perspective = false, worldAxes = false;
	int rotationAxis = -1; // Optional single ring for a tool in an orthographic plan.
	// The UI's text scale (1 at 100%): the object tools' pointer targets and
	// markers grow with it.
	double handleScale = 1;
};
struct LevelComponentGizmoHandle {
	int id = -1, axis = 0;
	QPolygonF points;
	QPointF label;
	QString text;
};

// Shared, non-document interaction state for the plan and camera. The owner
// supplies projection/rays and a checked asynchronous geometry response.
// Only the latest serial can publish; release waits for that response.
class LevelComponentGizmo final : public QObject {
	Q_OBJECT
public:
	explicit LevelComponentGizmo(QObject* parent = nullptr);
	void setContext(LevelComponentTool tool, const LevelMapVec3& pivot, bool available, double grid,
		double angleStep = 15, double scaleStep = .125, double shearStep = .125);
	// Whole objects: summaries speak of the selection, a dragged pivot lands
	// on the bounds' minimum, centre or maximum or the grid, and Ctrl lands the
	// translated bounds on `boundsGrid`. `customPivot` marks a placed pivot.
	void setObjectContext(const LevelMapVec3& mins, const LevelMapVec3& maxs, double boundsGrid, bool customPivot);
	[[nodiscard]] bool objects() const { return m_objects; }
	// An object manipulator with a tool on (Select sets Move, which shows none).
	[[nodiscard]] bool objectToolOn() const { return m_objects && m_tool != LevelComponentTool::Move; }
	// The object tool's name for status lines: "Translate", "Rotate", "Scale"
	// or "Transform".
	[[nodiscard]] QString toolName() const;
	// The kind of an object Transform handle id; None for another id.
	[[nodiscard]] static LevelObjectTransformKind transformKindOf(int handle);
	// The active object Transform drag's kind; None otherwise.
	[[nodiscard]] LevelObjectTransformKind transformKind() const;
	// An object Transform skew as a side shear (core/level_object_manipulator.h
	// LevelObjectShearRequest): the side that slides and how far. False unless
	// the last Transform drag skewed. It stays readable while the drag asks
	// for its commit.
	bool objectSkew(int* axis, int* side, LevelMapVec3* offset) const;
	[[nodiscard]] bool available() const { return m_available; }
	[[nodiscard]] int activeHandle() const { return m_active ? m_handle : -1; }
	[[nodiscard]] bool movingPivot() const { return m_active && (m_tool == LevelComponentTool::Pivot || objectPivotHandle()); }
	// The translation an active Translate drag of the selection previews, all
	// zero otherwise (also while the pivot is dragged).
	[[nodiscard]] LevelMapVec3 translation() const;
	// The unit axis of the object Rotate drag's turn, request().amount degrees
	// right-handed about it through pivot(). It stays readable when the drag
	// asks for its commit; invalid before any turn.
	[[nodiscard]] LevelMapVec3 rotationAxis() const { return m_rotationAxis; }
	// What an object Rotate drag previews as a dashed wire, turned live: the
	// owner hands the selection's edges over when the drag begins
	// (core/level_object_manipulator.h levelObjectManipulatorEdges()). The
	// drag's end forgets them.
	void setObjectPreviewEdges(const QVector<QPair<LevelMapVec3,LevelMapVec3>>& edges);
	[[nodiscard]] qsizetype objectPreviewEdgeCount() const { return m_objectEdges.size(); }
	// The object Rotate tool's free turn inside the rings (NetRadiant
	// Custom's); off, a press there falls through to the view's own select
	// and move, as under TrenchBroom's rotate handle.
	void setFreeTurn(bool on);
	[[nodiscard]] bool freeTurn() const { return m_freeTurn; }
	// The object Scale or Transform scale drag's factors, request().scale;
	// 1, 1, 1 otherwise.
	[[nodiscard]] LevelMapVec3 scaleFactors() const;
	[[nodiscard]] LevelComponentTool tool() const { return m_tool; }
	// The pivot. An object Transform drag turns, skews and scales about its
	// own pivot (the side or corner opposite its handle while no pivot is
	// placed), which this reports until the drag has finished.
	[[nodiscard]] LevelMapVec3 pivot() const { return m_transformPivot.valid ? m_transformPivot : m_pivot; }
	[[nodiscard]] bool active() const { return m_active; }
	[[nodiscard]] bool pending() const { return m_pending; }
	[[nodiscard]] bool releasePending() const { return m_releasePending; }
	[[nodiscard]] quint64 serial() const { return m_serial; }
	[[nodiscard]] const LevelComponentTransformRequest& request() const { return m_request; }
	[[nodiscard]] QString summary() const;
	[[nodiscard]] QVector<LevelComponentGizmoHandle> handles(const LevelComponentGizmoFrame& frame) const;
	[[nodiscard]] int hitTest(const QPointF& point, const LevelComponentGizmoFrame& frame, const QFont& font = {}) const;
	bool begin(const QPointF& point, const LevelComponentGizmoFrame& frame, const QFont& font = {});
	// Translate reads Shift (keep the main axis of the square or pivot drag)
	// and Ctrl (land the bounds on the grid); object Rotate reads Shift (snap
	// the angle to 15 degrees); object Scale reads Shift (scale the axes
	// equally) and Ctrl (land the size on the grid); the component tools
	// ignore them.
	bool updateDrag(const QPointF& point, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
	// A Shift or Ctrl change mid-drag, applied at the last pointer position.
	bool updateModifiers(Qt::KeyboardModifiers modifiers);
	void finish(bool commit);
	void setPreview(quint64 serial, const QVector<QPair<LevelMapVec3,LevelMapVec3>>& edges, const QString& error);
	[[nodiscard]] LevelMapVec3 transformed(const LevelMapVec3& point) const;
	void paint(QPainter& painter, const LevelComponentGizmoFrame& frame, bool highContrast) const;
Q_SIGNALS:
	void activeChanged(bool active);
	void previewRequested(quint64 serial);
	void commitRequested(quint64 serial);
	void pivotChanged(const vibestudio::LevelMapVec3& pivot);
	// A click on Translate's pivot point that did not drag: NetRadiant
	// Custom switches between the placed pivot and the automatic one.
	void pivotToggleRequested();
	void changed();
private:
	void refuse(const QString& error);
	void paintTranslate(QPainter& painter, const LevelComponentGizmoFrame& frame, bool highContrast) const;
	void paintObjectRotate(QPainter& painter, const LevelComponentGizmoFrame& frame, bool highContrast) const;
	void paintObjectScale(QPainter& painter, const LevelComponentGizmoFrame& frame, bool highContrast) const;
	void paintObjectTransform(QPainter& painter, const LevelComponentGizmoFrame& frame, bool highContrast) const;
	// The object Transform tool's handles, its hit test and the start of its drags.
	[[nodiscard]] QVector<LevelComponentGizmoHandle> transformHandles(const LevelComponentGizmoFrame& frame) const;
	[[nodiscard]] int transformHitTest(const QPointF& point, const LevelComponentGizmoFrame& frame) const;
	bool beginTransform(const QPointF& point, const LevelComponentGizmoFrame& frame, int handle);
	// The selection where an object Rotate or Scale drag would take it.
	void paintObjectWire(QPainter& painter, const LevelComponentGizmoFrame& frame, bool highContrast) const;
	// The shared pivot point of an object tool (handle 4).
	[[nodiscard]] bool objectPivotHandle() const
	{
		return m_objects && m_handle == 4
			&& (m_tool == LevelComponentTool::Translate || m_tool == LevelComponentTool::Rotate || m_tool == LevelComponentTool::Scale
				|| m_tool == LevelComponentTool::Transform);
	}
	bool m_objects = false, m_customPivot = false, m_freeTurn = true;
	// The object Scale square's view axis, which its Shift leaves alone.
	int m_scaleViewAxis = -1;
	LevelMapVec3 m_rotationAxis, m_screenAxis, m_screenPrevious, m_sphereStart;
	double m_screenAngle = 0;
	QVector<QPair<LevelMapVec3,LevelMapVec3>> m_objectEdges;
	LevelMapVec3 m_mins, m_maxs;
	double m_boundsGrid = 0;
	// The object Transform drag: its pivot, the box sides it moves (-1, 0 or
	// +1 per axis), and the move's vertical plane (Alt in the camera).
	LevelMapVec3 m_transformPivot;
	std::array<int,3> m_transformSides {0,0,0};
	ModelMoveDrag m_moveVertical;
	bool m_moveVerticalValid = false, m_finishing = false;
	Qt::KeyboardModifiers m_modifiers = Qt::NoModifier;
	QPointF m_last;
	LevelComponentTool m_tool = LevelComponentTool::Move;
	LevelMapVec3 m_pivot;
	bool m_available = false, m_active = false, m_pending = false, m_releasePending = false, m_valid = true, m_travelled = false, m_linearRotation = false;
	double m_grid = 0, m_angleStep = 15, m_scaleStep = .125, m_shearStep = .125;
	int m_handle = -1;
	quint64 m_serial = 0;
	LevelComponentGizmoFrame m_frame;
	LevelComponentTransformRequest m_request;
	ModelMoveDrag m_move;
	ModelRotateDrag m_rotate;
	QPointF m_press, m_tangent;
	QString m_error;
	QVector<QPair<LevelMapVec3,LevelMapVec3>> m_edges;
};
} // namespace vibestudio
