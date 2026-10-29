#pragma once

#include "app/live_status_label.h"
#include "core/model_document.h"
#include <QDialog>
#include <QMap>
#include <QPointF>
#include <algorithm>
#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableView;
class QPainter;

namespace vibestudio
{
class ModelViewport;
class ModelWeightTable;
struct ModelVertexProjection;
class ModelWeightPaintDialog final : public QDialog
{
  public:
	ModelWeightPaintDialog(const ModelMesh &mesh, const ModelSelection &selection, const QString &joint, int frame,
						   QWidget *parent = nullptr);
	ModelEdit edit() const;
	const ModelMesh &stagedMesh() const
	{
		return m_document.mesh();
	}
	bool busy() const
	{
		return m_busy;
	}
	bool strokeActive() const
	{
		return bool(m_projection);
	}
	// Native gestures and offscreen QA share these semantic entry points.
	bool beginStroke(QPointF point);
	bool extendStroke(QPointF point);
	bool finishStroke(bool commit);
	bool paintSelected();
	void undoStroke();
	void redoStroke();
	void reject() override;
	// Recovery (app/model_session_drafts.cpp): the staged strokes, a callback
	// after they change, and strokes recovered after a crash, staged again
	// once the document is prepared.
	const QVector<ModelWeightStroke> &stagedStrokes() const
	{
		return m_strokes;
	}
	int surfaceIndex() const
	{
		return std::max(0, m_originalSelection.surface);
	}
	std::function<void()> stagedChanged;
	void restoreStrokes(QVector<ModelWeightStroke> strokes)
	{
		m_restore = std::move(strokes);
	}

  protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

  private:
	ModelDocument m_document;
	ModelSelection m_originalSelection;
	QVector<ModelWeightStroke> m_strokes, m_redoStrokes, m_restore;
	ModelWeightStroke m_pending;
	QMap<int, double> m_coverage;
	QSet<int> m_mask;
	std::shared_ptr<const ModelVertexProjection> m_projection;
	QPointF m_pointer, m_last;
	bool m_hover = false, m_busy = false, m_closeAfter = false, m_refreshing = false;
	std::function<void()> m_cancel;
	QWidget *m_content = nullptr;
	QComboBox *m_surface = nullptr, *m_joint = nullptr, *m_mode = nullptr, *m_falloff = nullptr, *m_remainder = nullptr, *m_frame = nullptr;
	QDoubleSpinBox *m_value = nullptr, *m_strength = nullptr;
	QSpinBox *m_radius = nullptr;
	QCheckBox *m_paint = nullptr, *m_through = nullptr, *m_selectionOnly = nullptr;
	QListWidget *m_locks = nullptr;
	QLabel *m_probe = nullptr;
	LiveStatusLabel *m_status = nullptr;
	QPushButton *m_apply = nullptr, *m_undo = nullptr, *m_redo = nullptr, *m_selected = nullptr;
	QTableView *m_table = nullptr;
	ModelWeightTable *m_tableModel = nullptr;
	ModelViewport *m_preview = nullptr;
	ModelWeightStroke settings() const;
	QSet<int> selectedVertices() const;
	bool stage(const ModelWeightStroke &stroke);
	void restageRecovered();
	void changed(bool mesh = false);
	void buttons();
	void paintOverlay(QPainter &painter);
	void probe(QPointF point);
};
} // namespace vibestudio
