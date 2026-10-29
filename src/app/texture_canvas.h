#pragma once

#include "core/texture_document.h"

#include <QWidget>
#include <functional>

namespace vibestudio {

// Input stays inside this focused widget. Automation calls the document API.
class TextureCanvas final : public QWidget {
	Q_OBJECT
public:
	// New tools are appended so the tool combo's indices stay stable.
	enum class Tool { Pencil, Eraser, Fill, Pick, Select, Brush, Line, Rectangle, Ellipse, Gradient, Clone };
	explicit TextureCanvas(TextureDocument* document, QWidget* parent = nullptr);
	std::function<void()> changed;
	// The colour is the primary paint colour, or the secondary one for a
	// right-button (or secondary keyboard) gesture.
	std::function<void(QPoint, QColor)> fillRequested;
	std::function<void(TextureShape, QPoint, QPoint, const TextureBrush&, bool, QColor)> shapeRequested;
	std::function<void(QPoint, QPoint, QColor, QColor)> gradientRequested;
	std::function<void(QColor)> colorPicked;
	std::function<void(QColor)> secondaryColorPicked;
	std::function<void(QRect)> selectionChanged;
	std::function<void(const QString&)> statusChanged;
	std::function<void(const QString&)> operationFailed;
	std::function<void(double)> zoomChanged;
	std::function<void(QPoint)> cloneSourceChanged;
	void setTool(Tool tool);
	[[nodiscard]] Tool tool() const { return m_tool; }
	void setColor(QColor color) { m_color = color; }
	void setSecondaryColor(QColor color) { m_secondary = color; }
	void setBrushWidth(int width) { m_brushWidth = width; update(); }
	void setBrushShape(TextureBrushShape shape) { finishStroke(); m_brushShape = shape; }
	void setPaintMode(TexturePaintMode mode) { finishStroke(); m_paintMode = mode; }
	void setWrappedPainting(bool wrap) { finishStroke(); m_wrap = wrap; }
	void setFilledShapes(bool filled) { finishStroke(); m_filledShapes = filled; }
	void setMirror(bool horizontal, bool vertical) { finishStroke(); m_mirrorX = horizontal; m_mirrorY = vertical; update(); }
	void setPressureEnabled(bool enabled) { m_pressure = enabled; }
	void setTiled(bool tiled) { finishStroke(); m_tiled = tiled; update(); }
	void setGrid(bool grid) { m_grid = grid; update(); }
	void setHighContrast(bool highContrast) { m_highContrast = highContrast; update(); }
	// A translucent frame drawn over the canvas, such as the previous sprite frame.
	void setOnionSkin(const QImage& image) { m_onion = image; update(); }
	void setZoom(double zoom);
	void setZoomAt(double zoom, QPointF anchor);
	[[nodiscard]] QPointF imagePositionAt(QPointF point) const;
	void fit();
	void refresh();
	void finishStroke();
	[[nodiscard]] double zoom() const { return m_zoom; }
	[[nodiscard]] QRect selection() const { return m_selection; }
	[[nodiscard]] QPoint cursorPixel() const { return m_pixel; }
	void setCursorPixel(QPoint pixel);
	void setSelection(QRect rectangle);
	// Clone copies from the source. Aligned keeps the first stroke's offset
	// for later strokes; otherwise each stroke starts again at the source.
	void setCloneSource(QPoint pixel);
	void clearCloneSource();
	void setCloneAligned(bool aligned) { m_cloneAligned = aligned; m_cloneOffsetSet = false; }
	[[nodiscard]] bool hasCloneSource() const { return m_hasCloneSource; }
	[[nodiscard]] QPoint cloneSource() const { return m_cloneSource; }
	void applyAtCursor(bool secondary = false);
	// Shared gesture commands used by this widget's input and direct command tests.
	void beginGesture(QPoint point, bool secondary = false);
	void continueGesture(QPoint point);
	// Pressure 0..1 scales the brush width of the current stroke (tablet input).
	void continueGestureWithPressure(QPoint point, double pressure);
	void endGesture();
	void cancelGesture();
	[[nodiscard]] bool hasPendingShape() const { return m_drawing; }
	// The colour the latest stroke painted with: primary or secondary.
	[[nodiscard]] QColor lastPaintColor() const { return m_lastPaint; }
	QSize minimumSizeHint() const override { return {240, 240}; }
	QSize sizeHint() const override { return {650, 600}; }

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseMoveEvent(QMouseEvent*) override;
	void mouseReleaseEvent(QMouseEvent*) override;
	void tabletEvent(QTabletEvent*) override;
	void wheelEvent(QWheelEvent*) override;
	void keyPressEvent(QKeyEvent*) override;
	void focusOutEvent(QFocusEvent*) override;
	void leaveEvent(QEvent*) override;

private:
	QRectF imageRect() const;
	QPoint pixelAt(QPointF point) const;
	void reportPixel();
	[[nodiscard]] TextureBrush currentBrush() const;
	[[nodiscard]] bool shapeTool() const;
	[[nodiscard]] bool strokeTool() const;
	[[nodiscard]] QColor gestureColor() const { return m_secondaryGesture ? m_secondary : m_color; }
	[[nodiscard]] QPoint boundedPoint(QPoint point) const;
	void submitShape();
	TextureDocument* m_document;
	Tool m_tool = Tool::Pencil;
	QColor m_color = Qt::white, m_secondary = Qt::black, m_lastPaint;
	int m_brushWidth = 1;
	TextureBrushShape m_brushShape = TextureBrushShape::Round;
	TexturePaintMode m_paintMode = TexturePaintMode::SourceOver;
	bool m_wrap = false, m_filledShapes = false, m_drawing = false;
	bool m_mirrorX = false, m_mirrorY = false, m_pressure = true, m_secondaryGesture = false;
	bool m_hasCloneSource = false, m_cloneAligned = true, m_cloneOffsetSet = false, m_hover = false;
	double m_zoom = 4;
	bool m_grid = true, m_tiled = false, m_highContrast = false, m_stroke = false, m_selecting = false, m_panning = false;
	QPoint m_pixel{0, 0}, m_selectionStart;
	QPoint m_shapeStart, m_shapeEnd, m_cloneSource, m_cloneOffset;
	QRect m_selection;
	QPointF m_offset, m_panStart;
	QImage m_onion;
};

} // namespace vibestudio
