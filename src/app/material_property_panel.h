#pragma once

// The properties of the selected graph node, as ordinary controls: numbers,
// choices, switches, image paths and Doom 3 expressions. Each committed
// change becomes one graph edit.
//
// Edits are sent after the control's own signal has returned: applying one
// rebuilds the graph and may give the node a different set of properties,
// and the controls are then replaced (later, never while one of them is
// still handling input). The property that had keyboard focus keeps it in
// the new controls. Number steps (Up and Down, spin box arrows) are sent
// once the stepping stops, so a held key is one edit.

#include "core/material_graph.h"

#include <QHash>
#include <QPointer>
#include <QWidget>

class QFormLayout;
class QLabel;
class QLineEdit;
class QScrollArea;
class QTimer;

namespace slopstudio {

class MaterialPropertyPanel final : public QWidget {
	Q_OBJECT

public:
	explicit MaterialPropertyPanel(QWidget* parent = nullptr);

	// Shows a node's properties; null shows the empty state. A node with the
	// same id and the same set of properties keeps its controls (and the
	// focused one keeps what the user is typing).
	void setNode(const MaterialGraphNode* node, bool readOnly);
	[[nodiscard]] QString nodeId() const { return m_hasNode ? m_node.id : QString(); }
	// Image paths the image fields complete from.
	void setImageChoices(const QStringList& images);
	// Moves keyboard focus to the first editable control.
	void focusFirstField();
	[[nodiscard]] QWidget* editorFor(const QString& propertyId) const;
	// Sends a number step that is still waiting for the stepping to stop.
	void flushPendingStep();
	// How long number stepping waits after the last step before it is sent.
	static constexpr int kStepDelayMs = 350;

Q_SIGNALS:
	void propertyEdited(const QString& nodeId, const QString& propertyId, const QString& value);
	void showImageRequested(const QString& reference);

protected:
	void changeEvent(QEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void rebuild();
	void refreshValues();
	QWidget* createEditor(const MaterialGraphProperty& property);
	// Sends an edit from `editor` once the current event is handled; nothing
	// when the editor belongs to controls already replaced, or the value is
	// the node's.
	void commit(QWidget* editor, const QString& propertyId, const QString& value);
	void commitNumber(QLineEdit* field, const QString& propertyId);
	void scheduleStep(const QString& propertyId);
	[[nodiscard]] QString focusedProperty() const;
	[[nodiscard]] QString signature(const MaterialGraphNode& node) const;
	[[nodiscard]] QString valueText(const MaterialGraphProperty& property, const QString& value) const;

	QLabel* m_title = nullptr;
	QLabel* m_subtitle = nullptr;
	QLabel* m_empty = nullptr;
	QScrollArea* m_scroll = nullptr;
	QWidget* m_formHost = nullptr;
	QFormLayout* m_form = nullptr;
	QTimer* m_stepTimer = nullptr;
	QString m_stepProperty;
	QString m_pendingCommit;
	MaterialGraphNode m_node;
	bool m_hasNode = false;
	bool m_readOnly = false;
	QStringList m_images;
	QHash<QString, QPointer<QWidget>> m_editors;
	QHash<QString, QPointer<QLabel>> m_problems;
};

} // namespace slopstudio
