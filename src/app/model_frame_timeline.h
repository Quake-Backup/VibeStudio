#pragma once

#include "core/model_mesh.h"

#include <QAbstractSlider>

#include <functional>

namespace vibestudio
{
// The Mesh Editor's frame timeline beside the transport's frame list: frame
// ticks, clip range bars (overlapping clips stack in lanes), Quake MDL native
// groups, the frame range the range tools use, and the current frame.
//
// It is a slider whose value is the current frame, so screen readers get its
// role, value and a per-frame description. Time runs left to right in every
// layout direction, as in the curve view. Click or drag to scrub; Shift-click
// or Shift-drag selects a range. Keys: Left and Right step one frame, Page Up
// and Page Down ten, Home and End jump to the ends; with Shift they extend
// the range from where it started.
class ModelFrameTimeline final : public QAbstractSlider
{
  public:
	explicit ModelFrameTimeline(QWidget *parent = nullptr);
	// Frame names, clips and MDL groups of `mesh`; keeps the current frame
	// and range where they still fit.
	void setModel(const ModelMesh &mesh);
	void setFrameRange(int first, int last);
	[[nodiscard]] int rangeFirst() const { return m_rangeFirst; }
	[[nodiscard]] int rangeLast() const { return m_rangeLast; }
	// Called when the user selects a range on the timeline.
	void setRangeCallback(std::function<void(int first, int last)> callback) { m_rangeChanged = std::move(callback); }
	void setHighContrast(bool enabled);
	// The frame under a widget x coordinate.
	[[nodiscard]] int frameAt(double x) const;
	// The widget x coordinate of a frame's centre.
	[[nodiscard]] double frameX(int frame) const;
	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;
	// The description announced for the current frame.
	[[nodiscard]] QString frameDescription(int frame) const;

  protected:
	void paintEvent(QPaintEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void sliderChange(SliderChange change) override;
	void changeEvent(QEvent *event) override;

  private:
	void selectRange(int first, int last, bool notify);
	void describe();
	int lanes() const;
	QStringList m_names;
	QVector<ModelAnimation> m_clips;
	QVector<int> m_clipLane;
	QVector<ModelMdlFrameGroup> m_groups;
	int m_rangeFirst = 0, m_rangeLast = 0, m_anchor = -1;
	bool m_dragging = false, m_extending = false, m_highContrast = false;
	std::function<void(int, int)> m_rangeChanged;
};
} // namespace vibestudio
