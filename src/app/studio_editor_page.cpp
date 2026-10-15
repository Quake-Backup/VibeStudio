#include "app/studio_editor_page.h"

#include <QAction>
#include <QDialog>
#include <QKeyEvent>
#include <QTabWidget>

namespace vibestudio {

StudioEditorPage::StudioEditorPage(QDialog* editor, QTabWidget* tabs)
	: QScrollArea(tabs), m_editor(editor), m_tabs(tabs)
{
	setObjectName(editor->objectName() + QStringLiteral("Page"));
	setAccessibleName(editor->accessibleName());
	setFrameShape(QFrame::NoFrame);
	setWidgetResizable(true);
	// Setting a parent alone retains QDialog's top-level window flag.
	editor->setParent(this, Qt::Widget);
	editor->setProperty("studioEmbeddedEditor", true);
	editor->setFocusPolicy(Qt::StrongFocus);
	setWidget(editor);
	tabs->addTab(this, QString());
	editor->installEventFilter(this);
	refreshTitle();
	connect(editor, &QObject::destroyed, this, [this] {
		m_editor = nullptr;
		m_tabs->removeTab(m_tabs->indexOf(this));
		deleteLater();
	});
	auto* close = new QAction(editor);
	close->setObjectName(QStringLiteral("closeStudioEditor"));
	close->setShortcut(QKeySequence::Close);
	close->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	editor->addAction(close);
	connect(close, &QAction::triggered, this, &StudioEditorPage::closeEditor);
}

StudioEditorPage::~StudioEditorPage()
{
	if (m_editor) { disconnect(m_editor, nullptr, this, nullptr); }
}

void StudioEditorPage::refreshTitle()
{
	const auto title = QString(m_editor->windowTitle()).replace(QStringLiteral("[*]"),
		m_editor->isWindowModified() ? QStringLiteral("*") : QString());
	const int index = m_tabs->indexOf(this);
	m_tabs->setTabText(index, title);
	m_tabs->setTabToolTip(index, title);
}

void StudioEditorPage::focusEditor()
{
	if (!m_editor) { return; }
	QWidget* focus = m_editor->focusWidget();
	if (!focus || !focus->isVisibleTo(m_editor) || !focus->isEnabled()) { focus = m_editor; }
	focus->setFocus(Qt::OtherFocusReason);
}

void StudioEditorPage::closeEditor()
{
	if (m_editor) {
		m_tabs->setCurrentWidget(this);
		m_editor->close();
	}
}

bool StudioEditorPage::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == m_editor) {
		if (event->type() == QEvent::WindowTitleChange || event->type() == QEvent::ModifiedChange) { refreshTitle(); }
		// Unhandled keys reaching the document must not invoke QDialog's
		// Escape-to-reject or default-button behaviour. Child controls and
		// viewports still handle their own cancellation and activation first.
		if (event->type() == QEvent::KeyPress) {
			const int key = static_cast<QKeyEvent*>(event)->key();
			if (key == Qt::Key_Escape || key == Qt::Key_Return || key == Qt::Key_Enter) { return true; }
		}
	}
	return QScrollArea::eventFilter(watched, event);
}

} // namespace vibestudio
