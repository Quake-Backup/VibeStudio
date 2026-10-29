#pragma once

#include "core/level_patch.h"

#include <QCoreApplication>
#include <QDialog>
#include <QPointF>
#include <QSize>
#include <QWidget>
#include <functional>

class QAction;
class QComboBox;
class QLabel;
class QTableWidget;

namespace vibestudio {
class ModelViewport;

class PatchControlView final : public QWidget {
	Q_OBJECT
  public:
	explicit PatchControlView(QWidget* parent = nullptr);
	void setPatch(const LevelMapPatch& patch);
	void setSelectedPoints(const QVector<int>& selected);
	QVector<int> selectedPoints() const
	{
		return m_selected;
	}
	void setPlane(const QString& plane);
	void setGrid(double grid)
	{
		m_grid = grid;
		update();
	}
	void framePatch();
	QPointF pointPosition(int index) const;
	int pointAt(const QPointF& position) const;
	QSize minimumSizeHint() const override
	{
		return {260, 220};
	}
  Q_SIGNALS:
	void selectionChanged(const QVector<int>& selected);
	void moveRequested(const QVector<int>& selected, const vibestudio::LevelMapVec3& delta);

  protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseMoveEvent(QMouseEvent*) override;
	void mouseReleaseEvent(QMouseEvent*) override;
	void keyPressEvent(QKeyEvent*) override;
	void wheelEvent(QWheelEvent*) override;

  private:
	QPointF project(const LevelMapVec3& p) const;
	LevelMapVec3 delta(const QPointF& p) const;
	QPointF toView(const QPointF& p) const;
	LevelMapPatch m_patch;
	QVector<int> m_selected;
	QString m_plane = QStringLiteral("xy");
	QPointF m_center;
	double m_scale = 0, m_grid = 8;
	bool m_dragging = false, m_panning = false;
	QPointF m_press, m_last, m_previewDelta;
};

// What Add Patch… starts from: the box its shapes fill (the selection's
// bounds, as NetRadiant Custom fits its Simple Patch Mesh, or a box at the
// view's centre), the axis, and whether the patch can replace the selection.
struct PatchEditorCreateOptions {
	LevelMapVec3 mins {0, 0, 0, false};
	LevelMapVec3 maxs {0, 0, 0, false};
	int axis = 2;
	bool canReplace = false;
	// The material image's size for Natural and Cap mapping; empty when unknown.
	std::function<QSize(const QString&)> imageSize;
};

class PatchEditorDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(PatchEditorDialog)
  public:
	explicit PatchEditorDialog(const LevelMapPatch& patch, bool creating, double grid, QWidget* parent = nullptr,
		const PatchEditorCreateOptions& create = {});
	const LevelMapPatch& patch() const
	{
		return m_patch;
	}
	// Add Patch…: whether the patch replaces the selected brushes and patches.
	bool replaceSelection() const
	{
		return m_replaceSelection;
	}
	bool applyPatch(const LevelMapPatch& patch, QString* error = nullptr);
	void selectPoints(const QVector<int>& points);
	bool moveSelected(const LevelMapVec3& delta, double grid = 0);
	void undo();
	void redo();

  protected:
	void showEvent(QShowEvent* event) override;

  private:
	QString materialToken() const;
	void refresh();
	void syncSelection(const QVector<int>& points);
	void setError(const QString& message);
	LevelMapPatch m_patch;
	QVector<LevelMapPatch> m_undo, m_redo;
	PatchControlView* m_controls = nullptr;
	ModelViewport* m_preview = nullptr;
	QTableWidget* m_points = nullptr;
	QComboBox* m_texture = nullptr;
	QLabel* m_status = nullptr;
	QAction* m_undoAction = nullptr;
	QAction* m_redoAction = nullptr;
	bool m_refreshing = false;
	bool m_firstShow = true;
	bool m_replaceSelection = false;
};
} // namespace vibestudio
