#pragma once

// The node editor for a material: core/material_graph.h laid out as boxes
// and wires. Images and coordinate, colour and geometry nodes feed stages,
// which feed the material's output in draw order. Every edit made here is a
// MaterialGraphEdit the workbench turns into text edits, so the text and the
// graph never disagree.

#include "core/material_graph.h"

#include <QAccessible>
#include <QHash>
#include <QImage>
#include <QPointF>
#include <QWidget>

namespace vibestudio {

class MaterialGraphView final : public QWidget {
	Q_OBJECT

public:
	explicit MaterialGraphView(QWidget* parent = nullptr);
	~MaterialGraphView() override;

	// Shows a graph. Nodes the user dragged keep their place while
	// `layoutKey` (the material's name) stays the same; after an edit made
	// here, they follow their nodes to new ids (a stage moved earlier keeps
	// its dragged place).
	void setGraph(const MaterialGraph& graph, const QString& layoutKey);
	void clearGraph(const QString& message);
	[[nodiscard]] const MaterialGraph& graph() const { return m_graph; }
	// A renamed material keeps its layout: call before showing the graph
	// under the new name.
	void renameLayoutKey(const QString& from, const QString& to);
	// Small pictures for image nodes, by image reference.
	void setThumbnails(const QHash<QString, QImage>& thumbnails);
	// What the Add Node menu offers.
	void setNodeTemplates(const QVector<MaterialGraphNodeTemplate>& templates);
	void setReadOnly(bool readOnly);
	// Offers Edit Image in Texture Editor on image nodes.
	void setImageEditing(bool available) { m_imageEditing = available; }
	[[nodiscard]] bool isReadOnly() const { return m_readOnly; }

	[[nodiscard]] QString selectedNode() const;
	// Selects a node by id. An id the graph no longer has selects its
	// nearest existing parent ("stage/0/tcmod/3" -> "stage/0"), else the
	// output; an empty id clears the selection.
	void selectNode(const QString& nodeId);
	void selectIndex(int index);
	[[nodiscard]] int selectedIndex() const { return m_selected; }
	// Fits the graph at a zoom its text stays legible at; a graph too large
	// for that is centred on the selected node (or the output) instead.
	void fitToView();
	// Fits the whole graph however small its text becomes.
	void showWholeGraph();
	// Forgets where nodes were dragged and fits the automatic layout.
	void resetLayout();
	void zoomBy(double factor);
	[[nodiscard]] double zoom() const { return m_zoom; }
	// True after a fit that could not show the whole graph legibly.
	[[nodiscard]] bool showsPartOfGraph() const { return m_partialFit; }
	[[nodiscard]] bool hasDraggedNodes() const { return !m_moved.isEmpty(); }
	[[nodiscard]] QPointF nodeScenePosition(const QString& nodeId) const;
	// Tells screen readers what happened, politely.
	void announce(const QString& message);

	// For screen readers and tests.
	[[nodiscard]] int nodeCount() const { return static_cast<int>(m_graph.nodes.size()); }
	[[nodiscard]] QRect nodeRect(int index) const;
	[[nodiscard]] QString nodeAccessibleName(int index) const;
	[[nodiscard]] QString nodeAccessibleDescription(int index) const;
	QAccessibleInterface* accessibleChild(int index) const;
	[[nodiscard]] int accessibleChildIndex(const QAccessibleInterface* child) const;
	// The node's accessible actions: press edits its properties, the menu
	// action opens its context menu.
	void activateNode(int index);
	void showNodeMenu(int index);

Q_SIGNALS:
	void selectionChanged(const QString& nodeId);
	void editRequested(const MaterialGraphEdit& edit);
	// Enter on a node: the workbench moves focus to its properties.
	void nodeActivated(const QString& nodeId);
	void showImageRequested(const QString& reference);
	void showTextRequested(int line);
	// Edit Image in Texture Editor on an image node.
	void editImageRequested(const QString& reference);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;
	void focusInEvent(QFocusEvent* event) override;
	void focusOutEvent(QFocusEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void changeEvent(QEvent* event) override;
	bool event(QEvent* event) override;

private:
	// What a node shows, elided to its box once per graph rather than on
	// every paint.
	struct NodeText {
		QString title;
		QString subtitle;
		QStringList inputs;
		QStringList outputs;
	};

	[[nodiscard]] int indexOf(const QString& nodeId) const { return m_indexById.value(nodeId, -1); }
	[[nodiscard]] QSizeF nodeSize(int index) const;
	[[nodiscard]] QPointF nodePosition(int index) const;
	[[nodiscard]] QRectF sceneRect(int index) const;
	[[nodiscard]] QPointF portPoint(int index, int port, bool output) const;
	[[nodiscard]] int portIndex(int node, const QString& portId, bool output) const;
	[[nodiscard]] QPointF toWidget(const QPointF& scene) const;
	[[nodiscard]] QPointF toScene(const QPointF& widget) const;
	[[nodiscard]] int nodeAt(const QPointF& widget) const;
	[[nodiscard]] QRectF contentsRect() const;
	[[nodiscard]] bool rightToLeft() const { return layoutDirection() == Qt::RightToLeft; }
	void ensureVisible(int index);
	void moveSelection(int key);
	void showMenu(const QPoint& globalPosition, int index);
	void requestEdit(MaterialGraphEditKind kind, const QString& nodeId, const QString& value = QString(), int step = 0);
	void sendEdit(const MaterialGraphEdit& edit, const QString& summary);
	void notifySelection();
	void resetAccessibleChildren();
	void rebuildIndex();
	void ensureTexts() const;
	void updateNode(int index);
	void setZoomAround(double zoom, const QPointF& anchor);

	MaterialGraph m_graph;
	QHash<QString, int> m_indexById;
	// Per node, the indexes of the links into and out of it.
	QVector<QVector<int>> m_linksIn;
	QVector<QVector<int>> m_linksOut;
	mutable QVector<NodeText> m_texts;
	mutable bool m_textsValid = false;
	QString m_layoutKey;
	// Where the user dragged nodes, by node id, for the current layout key.
	QHash<QString, QPointF> m_moved;
	// Id moves of the edit being sent, applied by the graph it produces.
	QVector<MaterialGraphIdMove> m_pendingMoves;
	int m_graphGeneration = 0;
	QHash<QString, QImage> m_thumbnails;
	QVector<MaterialGraphNodeTemplate> m_templates;
	QString m_message;
	bool m_readOnly = false;
	bool m_imageEditing = false;
	int m_selected = -1;
	int m_hover = -1;
	QPointF m_pan;
	double m_zoom = 1.0;
	bool m_fitPending = true;
	bool m_partialFit = false;
	// The text scale the zoom was last fitted for.
	double m_fitTextZoom = 1.0;

	enum class Drag {
		None,
		Pan,
		Node,
	};
	Drag m_drag = Drag::None;
	QPointF m_dragOrigin;
	QPointF m_dragStart;
	bool m_dragMoved = false;
	// A left press on the background: it clears the selection on release
	// unless it became a pan.
	bool m_backgroundPress = false;

	mutable QHash<int, QAccessible::Id> m_accessibleChildren;
};

} // namespace vibestudio
