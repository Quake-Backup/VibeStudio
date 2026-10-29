#pragma once

// UV > Paint Skin… (UVSKIN-09): paint a surface's skin directly on the model
// (core/model_skin_paint.h). Strokes paint every visible texel under the
// brush in 3D, across UV seams, with an optional mirror axis, a stamp image,
// or a smudge that drags the skin along; Escape cancels the stroke in
// progress, Undo and Redo step through strokes, Save Skin… writes the image
// and Apply to Model hands it back (an MDL skin member as one undo step, or
// an image staged in the open package). Paint in View paints through the Mesh
// Editor's own viewport and camera. Indexed skins are painted with palette
// indices.

#include "core/model_skin_paint.h"

#include <QDialog>
#include <QPointF>

#include <functional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace vibestudio
{

class ModelSkinPaintCanvas;

class ModelSkinPaintDialog final : public QDialog
{
  public:
	ModelSkinPaintDialog(const ModelMesh &mesh, int surface, int frame, const QImage &skin, QWidget *parent = nullptr);

	// Semantic entry points the canvas, the viewport and tests share; points
	// are in the canvas's view pixels, or in `view`'s for a view stroke.
	bool beginStroke(QPointF point);
	bool beginViewStroke(QPointF point, const ModelSkinPaintView &view);
	bool extendStroke(QPointF point);
	bool finishStroke(bool commit);
	[[nodiscard]] bool strokeActive() const { return m_active; }
	void undo();
	void redo();
	bool saveTo(const QString &path);
	[[nodiscard]] const QImage &skin() const { return m_skin; }
	[[nodiscard]] const ModelSkinPaintView &view() const { return m_view; }
	[[nodiscard]] int surface() const { return m_surface; }
	[[nodiscard]] int frame() const { return m_frame; }
	[[nodiscard]] const ModelMesh &mesh() const { return m_mesh; }
	// The model to paint, when the editor's document changed (the skin and
	// strokes stay).
	void updateModel(const ModelMesh &mesh);
	// The strokes so far, as model paint-skin replays them.
	[[nodiscard]] QJsonObject strokesJson() const;
	[[nodiscard]] QImage preview() const;
	void setStamp(const QImage &stamp);
	void setMode(ModelSkinPaintMode mode);

	// Apply to Model: what it does (shown on the button's tooltip), and the
	// editor's handler; without one the button stays off and says why.
	void setApplyTarget(const QString &description, std::function<bool(const QImage &, QString *)> apply);
	bool applyToModel();
	// Paint in View: the editor installs or removes the viewport hook.
	std::function<void(bool)> paintInView;
	void setPaintInView(bool on);
	[[nodiscard]] bool paintingInView() const;
	// Called with the skin after every change, so the editor can show it.
	std::function<void(const QImage &)> skinChanged;

  protected:
	void keyPressEvent(QKeyEvent *event) override;

  private:
	friend class ModelSkinPaintCanvas;
	void rebuildView();
	void repaintStroke();
	void refresh();
	void announce(const QString &text);
	ModelSkinBrush brush() const;
	[[nodiscard]] const ModelSkinPaintView &strokeView() const { return m_viewStroke ? m_externalView : m_view; }

	ModelMesh m_mesh;
	int m_surface = 0;
	int m_frame = 0;
	QImage m_skin;
	QImage m_before;
	QImage m_stamp;
	QVector<QImage> m_undo, m_redo;
	QVector<ModelSkinPaintStroke> m_strokes, m_redoStrokes;
	ModelSkinPaintStroke m_pending;
	bool m_active = false;
	bool m_viewStroke = false;
	ModelSkinPaintCamera m_camera;
	ModelSkinPaintView m_view;
	ModelSkinPaintView m_externalView;
	QColor m_colour = QColor(200, 40, 40);
	std::function<bool(const QImage &, QString *)> m_apply;
	ModelSkinPaintCanvas *m_canvas = nullptr;
	QPushButton *m_colourButton = nullptr, *m_undoButton = nullptr, *m_redoButton = nullptr, *m_save = nullptr;
	QPushButton *m_stampButton = nullptr, *m_clearStamp = nullptr, *m_applyButton = nullptr, *m_inView = nullptr;
	QSpinBox *m_radius = nullptr, *m_index = nullptr;
	QDoubleSpinBox *m_opacity = nullptr, *m_hardness = nullptr, *m_yaw = nullptr, *m_pitch = nullptr, *m_zoom = nullptr;
	QComboBox *m_mode = nullptr, *m_mirror = nullptr;
	QLabel *m_status = nullptr;
};

} // namespace vibestudio
