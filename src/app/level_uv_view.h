#pragma once

#include "core/level_face_attributes.h"
#include "core/level_uv_edit.h"

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <functional>

class QMenu;

namespace vibestudio {

// The UV Editor's texture-space view: one brush face's winding or one
// patch's control grid drawn over its material image, tiled as the map
// repeats it. A face's points move its whole texture: dragging a point moves
// the texture so the point follows the pointer, and the point dragged last
// becomes the anchor, about which dragging any other point turns the
// texture, as NetRadiant Custom's TexTool does. A patch's points move one by
// one, or as a selection. Every change is checked by the shared service
// (core/level_uv_edit.h) and drawn as the map will read it.
//
// Mouse: left-drag a point (Shift snaps a turn to the turn step), left-drag
// inside a face to move it, left-drag across a patch to select points
// (Ctrl adds), right- or middle-drag to pan, the wheel to zoom; right-click
// for a menu. Keys: arrows nudge by the snap step (Shift eight times as far),
// Ctrl+arrows pan, [ and ] turn by the turn step ({ and } by one degree), +,
// - and Insert and Delete zoom, Home or F frames, N and Shift+N step through
// the points, A sets or clears the anchor at the current point, Ctrl+A
// selects every patch point, Ctrl+Z and Ctrl+Shift+Z undo and redo, Escape
// cancels a drag.
//
// With handles on (the Surfaces tab's Face section), a face shows a pivot, a
// turn knob on a ring about it and stretch knobs along U, along V and along
// both; dragging inside the face or one of its points moves the texture.
// Each finished drag, nudge or turn goes out once through `handleEdited` as
// values for the face (core/level_face_attributes.h), which the inspector
// commits as one map undo step before giving the view the face again; the
// view keeps no edit history of its own then.
class LevelUvView final : public QWidget {
	Q_OBJECT
  public:
	enum class Handle { None, Pivot, Turn, StretchU, StretchV, StretchBoth };
	explicit LevelUvView(QWidget* parent = nullptr);
	~LevelUvView() override;
	// Starts again from `layout`, with no edit. `image` may be null: a
	// checker then shows the repeats. `keepView` keeps the zoom, pan and
	// pivot when the layout is the same face or patch again, as after an
	// edit the view itself made.
	void setSource(const LevelUvLayout& layout, const QImage& image, bool keepView = false);
	// Shows `message` in place of a surface.
	void clearSource(const QString& message = {});
	// The Face section's handles; faces only.
	void setHandlesEnabled(bool enabled);
	[[nodiscard]] bool handlesEnabled() const { return m_handles; }
	// The point turns and stretches keep in place, on the face's plane, and
	// where it reads on the texture now.
	[[nodiscard]] LevelMapVec3 pivotWorld() const { return m_pivot; }
	[[nodiscard]] QPointF pivotTexel() const;
	bool setPivotTexel(QPointF texel);
	// Where a handle is drawn, in widget coordinates; not finite when hidden.
	[[nodiscard]] QPointF handlePosition(Handle handle) const;
	// Handles: turns the face's texture by about `clockwiseDegrees` as the
	// view draws it, through its rotation value, about the pivot.
	bool turnTexture(double clockwiseDegrees);
	// Handles: called with each finished change, to commit to the map.
	std::function<void(const LevelFaceAttributesEdit&)> handleEdited;
	void setImage(const QImage& image);
	[[nodiscard]] bool hasSource() const { return m_hasSource; }
	[[nodiscard]] const LevelUvLayout& layout() const { return m_layout; }
	[[nodiscard]] const LevelUvEdit& edit() const { return m_edit; }
	[[nodiscard]] const LevelUvResult& result() const { return m_result; }
	// The last refused change, or empty.
	[[nodiscard]] QString error() const { return m_error; }
	// Whether the edit changes the surface.
	[[nodiscard]] bool changed() const { return m_result.fit != LevelUvFit::Unchanged; }
	void setAllowValve220(bool allow);
	// Texels a drag snaps to and a nudge moves; 0 turns snapping off (a nudge
	// then moves one texel).
	void setSnapStep(double texels);
	[[nodiscard]] double snapStep() const { return m_snap; }
	void setAngleStep(double degrees);
	[[nodiscard]] double angleStep() const { return m_angleStep; }

	[[nodiscard]] int anchor() const { return m_anchor; }
	// -1 clears the anchor.
	bool setAnchor(int point);
	[[nodiscard]] int currentPoint() const { return m_current; }
	void setCurrentPoint(int point);
	[[nodiscard]] QVector<int> selectedPoints() const { return m_selected; }
	void setSelectedPoints(const QVector<int>& points);

	// The edits the keys, buttons and menu make, each one undo step here.
	// A face moves or turns as a whole; a patch moves or turns its selected
	// points, or every point with none selected. A turn goes about the anchor,
	// or else the centre of what turns.
	bool nudge(QPointF texels);
	bool rotate(double clockwiseDegrees);
	// A face: its whole texture moves so `point` reads `texel`. A patch: the
	// point takes `texel`.
	bool movePointTo(int point, QPointF texel);
	bool resetEdit();
	bool undo();
	bool redo();
	[[nodiscard]] bool canUndo() const { return !m_undo.isEmpty(); }
	[[nodiscard]] bool canRedo() const { return !m_redo.isEmpty(); }

	void frameAll();
	// About `screenPoint`, or the middle of the view when it is not finite.
	void zoomBy(double factor, QPointF screenPoint);
	void zoomBy(double factor);
	void panBy(QPointF screenDelta);
	[[nodiscard]] double zoom() const { return m_zoom; }
	[[nodiscard]] QPointF texelToScreen(QPointF texel) const;
	[[nodiscard]] QPointF screenToTexel(QPointF screen) const;
	// The point under `screen`, nearest first, or -1.
	[[nodiscard]] int pointAt(QPointF screen, double tolerance = 8) const;
	[[nodiscard]] bool dragging() const { return m_drag != Drag::None; }
	// Plain text for the accessible description and the dialog's status.
	[[nodiscard]] QString summary() const;
	[[nodiscard]] QSize minimumSizeHint() const override;
	[[nodiscard]] QSize sizeHint() const override;
	// The menu a right-click opens, for the view's own actions.
	[[nodiscard]] QMenu* contextMenu() const { return m_menu; }

  Q_SIGNALS:
	// The edit or its result changed, including live during a drag.
	void editChanged();
	// The anchor, current point, selection or undo history changed.
	void stateChanged();

  protected:
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void focusOutEvent(QFocusEvent* event) override;
	void changeEvent(QEvent* event) override;
	bool event(QEvent* event) override;

  private:
	enum class Drag { None, Point, Grab, Band, Pan, Handle };
	bool setWorkingEdit(const LevelUvEdit& edit);
	// Handles: shows the face as `edit` would leave it, or the source again
	// for an empty edit.
	bool previewHandleEdit(const LevelFaceAttributesEdit& edit);
	// Handles: reports `edit` and shows the source again unless the commit
	// replaced it.
	void reportHandleEdit(const LevelFaceAttributesEdit& edit);
	[[nodiscard]] Handle handleAt(QPointF screen) const;
	[[nodiscard]] LevelFaceAttributesEdit handleDragEdit(QPointF screen, Qt::KeyboardModifiers modifiers) const;
	// Degrees the face's outline turns, as drawn, per degree of rotation value.
	[[nodiscard]] double turnRate(const LevelTextureValues& values) const;
	[[nodiscard]] bool faceValues(LevelTextureValues* values) const;
	[[nodiscard]] double ringRadius() const;
	void resetPivot();
	void record(const LevelUvEdit& before);
	void finishDrag(bool commit);
	void announce();
	void buildMenu();
	QVector<int> turningPoints() const;
	QPointF pivotFor(const QVector<int>& points) const;
	QRectF contentBounds() const;
	QSizeF tile() const;

	LevelUvLayout m_layout;
	bool m_hasSource = false;
	QImage m_image, m_checker;
	LevelUvEdit m_edit;
	LevelUvResult m_result;
	QString m_error;
	QVector<LevelUvEdit> m_undo, m_redo;
	int m_anchor = -1, m_current = -1;
	QVector<int> m_selected;
	double m_snap = 1, m_angleStep = 15;
	bool m_allowValve220 = false;
	double m_zoom = 1;
	QPointF m_centre;
	// The user zoomed or panned: resizes keep the camera instead of framing.
	bool m_userCamera = false;
	Drag m_drag = Drag::None;
	int m_dragPoint = -1;
	bool m_turning = false, m_moved = false, m_bandAdd = false;
	Qt::MouseButton m_button = Qt::NoButton;
	QPointF m_press, m_last;
	LevelUvEdit m_dragStartEdit;
	QVector<QPointF> m_dragStartUv;
	QMenu* m_menu = nullptr;
	QString m_message;
	// Handles.
	bool m_handles = false;
	LevelMapVec3 m_pivot;
	Handle m_handle = Handle::None;
	LevelFaceAttributesEdit m_handleEdit;
	LevelTextureValues m_dragValues;
	QPointF m_dragPivot, m_dragKnob;
	double m_dragRate = 1;
	quint64 m_sourceSerial = 0;
};

} // namespace vibestudio
