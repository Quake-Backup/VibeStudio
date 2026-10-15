#pragma once

#include <QScrollArea>

class QDialog;
class QTabWidget;

namespace vibestudio {

// Hosts an authoring document in the studio while retaining its save, recovery
// and asynchronous close guards. The document owns the decision to close.
class StudioEditorPage final : public QScrollArea {
public:
	StudioEditorPage(QDialog* editor, QTabWidget* tabs);
	~StudioEditorPage() override;
	void focusEditor();
	void closeEditor();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void refreshTitle();
	QDialog* m_editor;
	QTabWidget* m_tabs;
};

} // namespace vibestudio
