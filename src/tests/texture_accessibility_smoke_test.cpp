#include "app/asset_views.h"
#include "app/studio_theme.h"
#include "app/texture_canvas.h"
#include "app/texture_editor_dialog.h"
#include "core/studio_settings.h"

#include <QAbstractButton>
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QVBoxLayout>

#include <iostream>

using namespace slopstudio;

namespace {
bool expect(bool value, const QString& message)
{
	if (!value) { std::cerr << message.toStdString() << '\n'; }
	return value;
}

bool settled(TextureEditorDialog& editor)
{
	QEventLoop loop; QTimer poll, timeout; timeout.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { if (!editor.isBusy()) { loop.quit(); } });
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start(5); timeout.start(15000); if (editor.isBusy()) { loop.exec(); }
	return !editor.isBusy();
}

class ExpandedTranslator final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (!QByteArray(context).startsWith("SlopStudioTexture")) { return {}; }
		return QStringLiteral("[%1 expanded]").arg(QString::fromUtf8(source));
	}
};

// The rules by which the editors give keyboard focus back after a job that
// disabled the focused control, on a plain window so each case is exact.
bool workFocusContract(QApplication& app)
{
	bool ok = true;
	QWidget window; auto* layout = new QVBoxLayout(&window);
	auto* first = new QPushButton(QStringLiteral("first")); auto* second = new QPushButton(QStringLiteral("second"));
	auto* third = new QPushButton(QStringLiteral("third")); auto* cancel = new QPushButton(QStringLiteral("cancel"));
	for (auto* button : {first, second, third, cancel}) { layout->addWidget(button); }
	cancel->hide(); window.show(); app.processEvents();
	WorkFocus keeper(&window);
	// What an editor does around one job: disable, return to the event loop, and
	// later enable again.
	const auto start = [&]() { keeper.begin(); first->setEnabled(false); cancel->show(); app.processEvents(); };
	const auto finish = [&]() { keeper.ended(cancel); first->setEnabled(true); cancel->hide(); };

	first->setFocus(); start();
	ok &= expect(window.focusWidget() != first, QStringLiteral("disabling the focused control moves the focus on"));
	finish(); keeper.restore();
	ok &= expect(window.focusWidget() == first, QStringLiteral("the focus returns to the control that had it before the job"));

	first->setFocus(); start(); third->setFocus(); finish(); keeper.restore();
	ok &= expect(window.focusWidget() == third, QStringLiteral("a control the user went to during the job keeps the focus"));

	first->setFocus(); start(); cancel->setFocus(); finish(); keeper.restore();
	ok &= expect(window.focusWidget() == first, QStringLiteral("focus left on the job's own Cancel button returns to the earlier control"));

	first->setFocus(); start(); finish();
	start(); // a completion callback starts the next job of the chain
	finish(); keeper.restore();
	ok &= expect(window.focusWidget() == first, QStringLiteral("a chain of jobs returns the focus of its first job"));

	first->setFocus(); start(); keeper.ended(cancel); cancel->hide(); keeper.restore();
	first->setEnabled(true); keeper.restore();
	ok &= expect(window.focusWidget() != first && !keeper.waiting(), QStringLiteral("a control still disabled when the job ends is not given the focus later"));

	first->setFocus(); start(); keeper.ended(cancel); cancel->hide(); keeper.restore(true);
	ok &= expect(keeper.waiting() && window.focusWidget() != first, QStringLiteral("the focus can be held for a control that is enabled a moment later"));
	app.processEvents(); first->setEnabled(true); keeper.restore();
	ok &= expect(window.focusWidget() == first && !keeper.waiting(), QStringLiteral("the held focus returns when its control is enabled"));

	first->setFocus(); start(); keeper.ended(cancel); cancel->hide(); keeper.restore(true);
	app.processEvents(); third->setFocus(); first->setEnabled(true); keeper.restore();
	ok &= expect(window.focusWidget() == third && !keeper.waiting(), QStringLiteral("a control the user went to while the focus was held keeps it"));

	second->setFocus(); start(); finish(); keeper.restore();
	ok &= expect(window.focusWidget() == second, QStringLiteral("focus on a control the job never disabled stays where it is"));
	return ok;
}
}

int main(int argc, char** argv)
{
	// Read native accessibility/focus metadata and invoke canvas commands only.
	// Do not synthesize input, change the live clipboard or capture the desktop.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	QTemporaryDir temporary;
	if (!temporary.isValid()) { return EXIT_FAILURE; }
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("profile.ini")));
	bool ok = workFocusContract(app);
	int pass = 0;
	for (const auto theme : {StudioTheme::Dark, StudioTheme::HighContrastDark, StudioTheme::HighContrastLight}) {
		const bool enlarged = pass != 0;
		ExpandedTranslator expanded;
		if (enlarged) { app.installTranslator(&expanded); }
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, enlarged ? 200 : 100));
		{
			TextureEditorDialog editor;
			editor.resize(enlarged ? QSize(1450, 1000) : QSize(1180, 800));
			editor.setLayoutDirection(enlarged ? Qt::RightToLeft : Qt::LeftToRight);
			QImage source(16, 16, QImage::Format_ARGB32); source.fill(QColor(30, 80, 140, 160));
			ok &= expect(editor.setImage(source, QStringLiteral("fixture.png")), QStringLiteral("load accessibility fixture"));
			editor.show(); app.processEvents();
			auto* sections = editor.findChild<QComboBox*>(QStringLiteral("texturePropertySection"));
			auto* canvas = editor.findChild<TextureCanvas*>(QStringLiteral("textureCanvas"));
			if (!sections || !canvas) { return EXIT_FAILURE; }
			QSet<QWidget*> focusChain;
			auto* next = editor.nextInFocusChain();
			while (next != &editor && !focusChain.contains(next)) { focusChain.insert(next); next = next->nextInFocusChain(); }
			ok &= expect(next == &editor, QStringLiteral("focus chain returns to the editor without trapping navigation"));
			QSet<QWidget*> inspected;
			for (int index = 0; index < sections->count(); ++index) {
				sections->setCurrentIndex(index); app.processEvents();
				// Opening Recovery starts a real asynchronous inventory. Audit the
				// settled controls before invoking authoring commands on the canvas.
				if (!expect(settled(editor), QStringLiteral("inspector operation completes: ") + sections->currentText())) { return EXIT_FAILURE; }
				for (auto* field : editor.findChildren<QWidget*>()) {
					const bool control = qobject_cast<QAbstractButton*>(field) || qobject_cast<QComboBox*>(field)
						|| qobject_cast<QSpinBox*>(field) || qobject_cast<QLineEdit*>(field) || qobject_cast<QListWidget*>(field)
						|| qobject_cast<PaletteSwatchView*>(field) || field == canvas;
					if (!control || !field->isVisibleTo(&editor) || inspected.contains(field) || field->objectName() == QStringLiteral("qt_spinbox_lineedit")) { continue; }
					inspected.insert(field);
					auto* accessible = QAccessible::queryAccessibleInterface(field);
					const QString name = accessible ? accessible->text(QAccessible::Name) : QString{};
					const auto diagnostic = QStringLiteral("section %1, %2 (%3)").arg(sections->currentText(), QString::fromLatin1(field->metaObject()->className()), name);
					ok &= expect(accessible && !name.isEmpty(), QStringLiteral("missing accessible name: ") + diagnostic);
					ok &= expect((field->focusPolicy() & Qt::TabFocus) && focusChain.contains(field), QStringLiteral("control cannot receive Tab focus: ") + diagnostic);
					ok &= expect(accessible && (!field->isEnabled() || accessible->state().focusable), QStringLiteral("enabled control lacks accessible focus state: ") + diagnostic);
				}
				for (auto* scroll : editor.findChildren<QScrollArea*>()) {
					if (scroll->isVisibleTo(&editor)) { ok &= expect(scroll->horizontalScrollBar()->maximum() == 0, QStringLiteral("inspector clips horizontally: ") + sections->currentText()); }
				}
			}
			ok &= expect(inspected.size() >= 75, QStringLiteral("audit includes every inspector section and both toolbars"));
			auto* accessible = QAccessible::queryAccessibleInterface(canvas);
			ok &= expect(accessible && accessible->role() == QAccessible::Graphic, QStringLiteral("canvas exposes the graphic accessibility role"));
			canvas->setCursorPixel({4, 5});
			const auto readout = accessible->text(QAccessible::Description);
			ok &= expect(readout.contains(QStringLiteral("4, 5")) && readout.contains(QStringLiteral("#a01e508c")), QStringLiteral("spoken readout contains the current pixel and RGBA"));
			canvas->setTool(TextureCanvas::Tool::Line); canvas->applyAtCursor(); canvas->setCursorPixel({9, 10});
			ok &= expect(canvas->hasPendingShape() && accessible->text(QAccessible::Description).contains(QStringLiteral("9, 10")), QStringLiteral("spoken readout follows the pending endpoint"));
			canvas->cancelGesture();
			ok &= expect(!canvas->hasPendingShape() && accessible->text(QAccessible::Description).contains(QStringLiteral("#a01e508c")) && !editor.hasUnsavedChanges(), QStringLiteral("cancelling a shape restores the pixel readout without editing"));
			auto* palette = editor.findChild<PaletteSwatchView*>();
			auto* paletteAccessible = QAccessible::queryAccessibleInterface(palette);
			ok &= expect(paletteAccessible && paletteAccessible->role() == QAccessible::ColorChooser, QStringLiteral("palette exposes the color chooser role"));
			palette->setSelectedIndex(7);
			ok &= expect(paletteAccessible->text(QAccessible::Description).contains(QStringLiteral("selected index 7")), QStringLiteral("palette selection updates its spoken index and color"));
			ImagePreviewView preview; preview.setImage(source, QStringLiteral("RGBA fixture"));
			auto* previewAccessible = QAccessible::queryAccessibleInterface(&preview);
			ok &= expect(previewAccessible && previewAccessible->role() == QAccessible::Graphic && !previewAccessible->text(QAccessible::Description).isEmpty(), QStringLiteral("shared image previews expose a graphic role and image summary"));
			for (const auto* id : {"newTexture", "openTexture", "saveTexture", "saveTextureAs", "undoTexture", "redoTexture"}) {
				auto* action = editor.findChild<QAction*>(QString::fromLatin1(id));
				ok &= expect(action && !action->shortcut().isEmpty() && action->shortcutContext() == Qt::WidgetWithChildrenShortcut && editor.actions().contains(action), QStringLiteral("document shortcut is scoped to its editor: ") + QString::fromLatin1(id));
			}
			if (pass == 0) {
				// A job disables the canvas and the inspector, which moves the keyboard
				// focus on. It must come back, or the canvas keys and Ctrl+Z do nothing
				// until something is clicked.
				canvas->setFocus();
				editor.applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("flip")}, {QStringLiteral("axis"), QStringLiteral("horizontal")}}});
				ok &= expect(editor.isBusy() && editor.focusWidget() != canvas, QStringLiteral("a running job disables the canvas and moves the focus on"));
				ok &= expect(settled(editor) && canvas->isEnabled() && editor.focusWidget() == canvas, QStringLiteral("the canvas has the keyboard focus again after a job"));
				sections->setCurrentIndex(1); app.processEvents();
				auto* flip = editor.findChild<QPushButton*>(QStringLiteral("flipTextureHorizontal"));
				if (!flip) { return EXIT_FAILURE; }
				flip->setFocus(); flip->click();
				ok &= expect(editor.isBusy() && editor.focusWidget() != flip, QStringLiteral("a running job disables the inspector and moves the focus on"));
				ok &= expect(settled(editor) && editor.focusWidget() == flip, QStringLiteral("an inspector button has the keyboard focus again after its job"));
				canvas->setFocus();
				editor.saveProjectToPath(QDir(temporary.path()).filePath(QStringLiteral("focus.vtexture")), false);
				ok &= expect(settled(editor) && !editor.hasUnsavedChanges() && editor.focusWidget() == canvas, QStringLiteral("the focus survives both jobs of a project save (prepare, then write)"));
			}
			std::cout << "theme=" << pass << " inspected_controls=" << inspected.size() << " focus_chain_widgets=" << focusChain.size() << '\n';
		}
		if (enlarged) { app.removeTranslator(&expanded); }
		++pass;
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
