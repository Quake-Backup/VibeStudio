#pragma once

// UV > Paint Skin… (UVSKIN-09): paint a surface's skin directly on the model
// (core/model_skin_paint.h). Strokes paint every visible texel under the
// brush in 3D, across UV seams, with an optional mirror axis; Escape cancels
// the stroke in progress, Undo and Redo step through strokes, and Save Skin…
// writes the image. Indexed MDL skins are painted with palette indices.

#include "core/model_skin_paint.h"

#include <QDialog>
#include <QPointF>

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

	// Semantic entry points the canvas and tests share; points are in the
	// canvas's view pixels.
	bool beginStroke(QPointF point);
	bool extendStroke(QPointF point);
	bool finishStroke(bool commit);
	[[nodiscard]] bool strokeActive() const { return m_active; }
	void undo();
	void redo();
	bool saveTo(const QString &path);
	[[nodiscard]] const QImage &skin() const { return m_skin; }
	[[nodiscard]] const ModelSkinPaintView &view() const { return m_view; }
	// The strokes so far, as model paint-skin replays them.
	[[nodiscard]] QJsonObject strokesJson() const;
	[[nodiscard]] QImage preview() const;

  protected:
	void keyPressEvent(QKeyEvent *event) override;

  private:
	friend class ModelSkinPaintCanvas;
	void rebuildView();
	void repaintStroke();
	void refresh();
	ModelSkinBrush brush() const;

	ModelMesh m_mesh;
	int m_surface = 0;
	int m_frame = 0;
	QImage m_skin;
	QImage m_before;
	QVector<QImage> m_undo, m_redo;
	QVector<ModelSkinPaintStroke> m_strokes, m_redoStrokes;
	ModelSkinPaintStroke m_pending;
	bool m_active = false;
	ModelSkinPaintCamera m_camera;
	ModelSkinPaintView m_view;
	QColor m_colour = QColor(200, 40, 40);
	ModelSkinPaintCanvas *m_canvas = nullptr;
	QPushButton *m_colourButton = nullptr, *m_undoButton = nullptr, *m_redoButton = nullptr, *m_save = nullptr;
	QSpinBox *m_radius = nullptr, *m_index = nullptr;
	QDoubleSpinBox *m_opacity = nullptr, *m_hardness = nullptr, *m_yaw = nullptr, *m_pitch = nullptr, *m_zoom = nullptr;
	QComboBox *m_mode = nullptr, *m_mirror = nullptr;
	QLabel *m_status = nullptr;
};

} // namespace vibestudio
