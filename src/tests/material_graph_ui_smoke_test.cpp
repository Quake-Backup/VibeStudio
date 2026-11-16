// The material node graph view and property panel as widgets: edits through
// real controls (choices, number steps, the wheel), focus kept across a panel
// rebuild, a legible fit, screen-reader names and actions, selection while
// panning, dragged positions following their nodes, the context menu, and
// right-to-left fields. A small harness stands in for the Materials
// workbench and, like it, applies each edit synchronously.
// No game data: every file is generated here.

#include "app/material_graph_view.h"
#include "app/material_property_panel.h"
#include "app/studio_theme.h"
#include "core/material_script.h"

#include <QAccessible>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QWheelEvent>

#include <functional>
#include <iostream>
#include <string>

using namespace vibestudio;

namespace {

int failures = 0;

bool expect(bool condition, const std::string& message, const QString& detail = QString())
{
	if (!condition) {
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty()) {
			std::cerr << "\n  " << detail.toStdString();
		}
		std::cerr << '\n';
		++failures;
	}
	return condition;
}

void settle(int ms = 0)
{
	QElapsedTimer timer;
	timer.start();
	do {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		if (ms > 0) {
			QThread::msleep(5);
		}
	} while (timer.elapsed() < ms);
}

const char* kShader = R"(// Graph fixture
textures/test/glow
{
	surfaceparm nolightmap
	{
		map textures/test/glow.tga
		tcMod scroll 0.5 0
		rgbGen wave sin 0.5 0.5 0 1
	}
	{
		map $lightmap
		blendFunc filter
		rgbGen const ( 1 0.5 0.25 )
	}
}
)";

const char* kMaterial = R"(textures/test/panel
{
	diffusemap textures/test/panel_d
	bumpmap addnormals( textures/test/panel_n, heightmap( textures/test/panel_h, 4 ) )
	{
		if parm7 == 0
		blend add
		map textures/test/panel_add
		red 0.5 + 0.5 * sinTable[ time ]
		green parm1 * 2
		blue ( time * 0.5 ) % 1
		scroll time * 0.1, time * -0.2
		scale 2 + parm4, 2 - parm5
		shear 0.1, sinTable[ time * 0.2 ]
		alphaTest 0.5 * parm6
	}
}
)";

// The workbench's part: apply each edit to the text, parse it again and show
// the new graph and properties, all before the signal returns.
struct Harness {
	QWidget window;
	MaterialGraphView* view = nullptr;
	MaterialPropertyPanel* panel = nullptr;
	QString text;
	MaterialTextKind kind = MaterialTextKind::Quake3Shader;
	MaterialEngine engine = MaterialEngine::Quake3;
	QString material;
	int applied = 0;
	QString lastError;

	Harness()
	{
		auto* layout = new QHBoxLayout(&window);
		view = new MaterialGraphView(&window);
		panel = new MaterialPropertyPanel(&window);
		layout->addWidget(view, 3);
		layout->addWidget(panel, 1);
		QObject::connect(view, &MaterialGraphView::selectionChanged, &window,
			[this](const QString& id) { panel->setNode(view->graph().node(id), false); });
		QObject::connect(view, &MaterialGraphView::editRequested, &window, [this](const MaterialGraphEdit& edit) { apply(edit); });
		QObject::connect(view, &MaterialGraphView::nodeActivated, &window, [this](const QString&) { panel->focusFirstField(); });
		QObject::connect(panel, &MaterialPropertyPanel::propertyEdited, &window,
			[this](const QString& node, const QString& property, const QString& value) {
				MaterialGraphEdit edit;
				edit.kind = MaterialGraphEditKind::SetProperty;
				edit.node = node;
				edit.property = property;
				edit.value = value;
				apply(edit);
			});
	}

	void load(const QString& source, MaterialTextKind textKind, MaterialEngine scriptEngine, const QString& name)
	{
		text = source;
		kind = textKind;
		engine = scriptEngine;
		material = name;
		show();
	}

	void show()
	{
		const MaterialScript script = parseMaterialScript(text, engine);
		const MaterialDefinition* definition = script.find(material);
		if (!definition) {
			view->clearGraph(QStringLiteral("missing"));
			return;
		}
		const MaterialGraph graph = buildMaterialGraph(*definition, kind);
		view->setNodeTemplates(materialGraphNodeTemplates(definition->engine, kind));
		view->setGraph(graph, material);
		panel->setNode(view->graph().node(view->selectedNode()), false);
	}

	bool apply(const MaterialGraphEdit& edit)
	{
		const MaterialGraphEditResult result = applyMaterialGraphEdit(text, kind, material, edit);
		if (!result.ok) {
			lastError = result.error;
			return false;
		}
		text = result.text;
		++applied;
		if (!result.renamedTo.isEmpty()) {
			view->renameLayoutKey(material, result.renamedTo);
			material = result.renamedTo;
		}
		show();
		if (!result.focusNode.isEmpty()) {
			view->selectNode(result.focusNode);
		}
		return true;
	}
};

QWidget* focusedWidget(QWidget* window)
{
	QWidget* focus = QApplication::focusWidget();
	return focus ? focus : window->focusWidget();
}

// A point on the graph's background, away from every node.
QPoint emptyPoint(MaterialGraphView* view)
{
	for (int y = 20; y < view->height() - 20; y += 10) {
		for (int x = 20; x < view->width() - 20; x += 10) {
			bool free = true;
			for (int index = 0; index < view->nodeCount() && free; ++index) {
				free = !view->nodeRect(index).adjusted(-12, -12, 12, 12).contains(QPoint(x, y));
			}
			if (free) {
				return {x, y};
			}
		}
	}
	return {-1, -1};
}

void sendKey(QWidget* widget, QEvent::Type type, int key, bool autoRepeat)
{
	QKeyEvent event(type, key, Qt::NoModifier, QString(), autoRepeat);
	QApplication::sendEvent(widget, &event);
}

void checkPanelEdits(Harness& harness)
{
	MaterialGraphView* view = harness.view;
	MaterialPropertyPanel* panel = harness.panel;
	harness.load(QString::fromUtf8(kShader), MaterialTextKind::Quake3Shader, MaterialEngine::Quake3, QStringLiteral("textures/test/glow"));
	settle();

	// Number stepping: a held key is one edit, sent when it is let go.
	view->selectNode(QStringLiteral("stage/0/rgbgen"));
	settle();
	auto* frequency = qobject_cast<QLineEdit*>(panel->editorFor(QStringLiteral("frequency")));
	if (expect(frequency != nullptr, "an rgbGen wave offers its frequency as a field")) {
		frequency->setFocus(Qt::OtherFocusReason);
		settle();
		const int before = harness.applied;
		for (int step = 0; step < 5; ++step) {
			sendKey(frequency, QEvent::KeyPress, Qt::Key_Up, step > 0);
		}
		settle();
		expect(harness.applied == before, "holding Up sends nothing while the key is held");
		sendKey(frequency, QEvent::KeyRelease, Qt::Key_Up, false);
		settle();
		expect(harness.applied == before + 1 && harness.text.contains(QStringLiteral("rgbGen wave sin 0.5 0.5 0 1.5")),
			"letting go of Up sends the five steps as one edit", harness.text);
		// Held until the delay runs out: still one edit.
		frequency = qobject_cast<QLineEdit*>(panel->editorFor(QStringLiteral("frequency")));
		if (frequency) {
			frequency->setFocus(Qt::OtherFocusReason);
			sendKey(frequency, QEvent::KeyPress, Qt::Key_Down, true);
			sendKey(frequency, QEvent::KeyPress, Qt::Key_Down, true);
			settle(MaterialPropertyPanel::kStepDelayMs + 400);
			expect(harness.applied == before + 2 && harness.text.contains(QStringLiteral("rgbGen wave sin 0.5 0.5 0 1.3")),
				"steps still held are sent after a short pause", harness.text);
		}
	}

	// A choice that changes the node's properties: the edit lands, the old
	// controls are not deleted under their own signal, and focus stays on
	// the same property.
	view->selectNode(QStringLiteral("stage/0/rgbgen"));
	settle();
	QPointer<QComboBox> source = qobject_cast<QComboBox*>(panel->editorFor(QStringLiteral("source")));
	if (expect(!source.isNull(), "the rgbGen source is a choice")) {
		source->setFocus(Qt::OtherFocusReason);
		settle();
		QTest::keyClick(source.data(), Qt::Key_Down);
		settle();
		expect(harness.text.contains(QStringLiteral("rgbGen entity")), "Down on the source choice changes rgbGen", harness.text);
		expect(panel->editorFor(QStringLiteral("frequency")) == nullptr, "the wave's fields are gone with the wave");
		QWidget* focus = focusedWidget(&harness.window);
		expect(focus && focus == panel->editorFor(QStringLiteral("source")) && focus != source.data(),
			"after the rebuild the new source choice has focus", focus ? focus->objectName() : QStringLiteral("nothing"));
		expect(source.isNull(), "the replaced choice is deleted once its signal is done");
	}

	// The wheel over a choice that has no focus scrolls, and edits nothing.
	view->selectNode(QStringLiteral("stage/1"));
	view->setFocus(Qt::OtherFocusReason);
	settle();
	auto* blend = qobject_cast<QComboBox*>(panel->editorFor(QStringLiteral("blend")));
	if (expect(blend != nullptr, "a stage offers its blend as a choice")) {
		const QString before = harness.text;
		const int index = blend->currentIndex();
		const QPointF centre = QPointF(blend->rect().center());
		QWheelEvent wheel(centre, blend->mapToGlobal(centre), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
		QApplication::sendEvent(blend, &wheel);
		settle();
		expect(harness.text == before && blend->currentIndex() == index, "the wheel over an unfocused choice changes nothing");
		// Labels are readable; the text gets the keyword.
		expect(blend->currentData().toString() == QStringLiteral("filter") && blend->currentText() != blend->currentData().toString(),
			"the blend choice shows a label and keeps the keyword as its value", blend->currentText());
	}

	// Right to left, values that are numbers or paths stay left to right.
	harness.window.setLayoutDirection(Qt::RightToLeft);
	view->selectNode(QStringLiteral("stage/1/rgbgen"));
	settle();
	QWidget* constant = panel->editorFor(QStringLiteral("value"));
	expect(constant && constant->layoutDirection() == Qt::LeftToRight, "right to left, a colour field reads left to right");
	harness.window.setLayoutDirection(Qt::LeftToRight);
	settle();
}

void checkViewBehaviour(Harness& harness)
{
	MaterialGraphView* view = harness.view;
	harness.load(QString::fromUtf8(kShader), MaterialTextKind::Quake3Shader, MaterialEngine::Quake3, QStringLiteral("textures/test/glow"));
	settle();

	// Names say which stage a node belongs to; descriptions say what can be
	// done with it; the nodes have press and menu actions.
	const int first = view->graph().indexOf(QStringLiteral("stage/0/tcgen"));
	const int second = view->graph().indexOf(QStringLiteral("stage/1/tcgen"));
	expect(first >= 0 && second >= 0 && view->nodeAccessibleName(first) != view->nodeAccessibleName(second),
		"two stages' coordinates have different accessible names", view->nodeAccessibleName(first));
	const int scroll = view->graph().indexOf(QStringLiteral("stage/0/tcmod/0"));
	const QString description = view->nodeAccessibleDescription(scroll);
	expect(description.contains(QStringLiteral("Delete")) && description.contains(QStringLiteral("Takes:")),
		"a removable node's description says Delete removes it and what feeds it", description);
	QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(view);
	QAccessibleInterface* child = accessible ? accessible->child(scroll) : nullptr;
	QAccessibleActionInterface* actions = child ? child->actionInterface() : nullptr;
	expect(actions && actions->actionNames().contains(QAccessibleActionInterface::pressAction())
			&& actions->actionNames().contains(QAccessibleActionInterface::showMenuAction()),
		"graph nodes have press and menu actions for screen readers");

	// Selection: a click on the background clears it, a pan keeps it.
	view->selectNode(QStringLiteral("stage/0"));
	settle();
	const QPoint empty = emptyPoint(view);
	if (expect(empty.x() >= 0, "the graph has some empty background")) {
		QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, empty);
		QTest::mouseMove(view, empty + QPoint(60, 30));
		QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, empty + QPoint(60, 30));
		settle();
		expect(view->selectedNode() == QStringLiteral("stage/0"), "panning the background keeps the selection", view->selectedNode());
		const QPoint again = emptyPoint(view);
		QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, again);
		settle();
		expect(view->selectedNode().isEmpty(), "a click on the background clears the selection", view->selectedNode());
	}

	// Delete selects what fed the removed node.
	view->setFocus(Qt::OtherFocusReason);
	view->selectNode(QStringLiteral("stage/0/tcmod/0"));
	QTest::keyClick(view, Qt::Key_Delete);
	settle();
	expect(!harness.text.contains(QStringLiteral("tcMod scroll")) && view->selectedNode() == QStringLiteral("stage/0/tcgen"),
		"Delete removes a tcMod and selects the coordinates it read", view->selectedNode());

	// Dragged positions follow their nodes when a stage moves.
	harness.load(QString::fromUtf8(kShader), MaterialTextKind::Quake3Shader, MaterialEngine::Quake3, QStringLiteral("textures/test/glow"));
	settle();
	view->selectNode(QStringLiteral("stage/1"));
	settle();
	const QRect box = view->nodeRect(view->selectedIndex());
	const QPoint grip(box.center().x(), box.top() + 8);
	QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, grip);
	QTest::mouseMove(view, grip + QPoint(0, 40));
	QTest::mouseMove(view, grip + QPoint(0, 80));
	QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, grip + QPoint(0, 80));
	settle();
	const QPointF dragged = view->nodeScenePosition(QStringLiteral("stage/1"));
	expect(view->hasDraggedNodes(), "dragging a node keeps its place");
	view->setFocus(Qt::OtherFocusReason);
	view->selectNode(QStringLiteral("stage/1"));
	QTest::keyClick(view, Qt::Key_Up, Qt::AltModifier);
	settle();
	expect(harness.text.indexOf(QStringLiteral("$lightmap")) < harness.text.indexOf(QStringLiteral("glow.tga")), "Alt+Up moves the stage earlier");
	expect(view->nodeScenePosition(QStringLiteral("stage/0")) == dragged && view->selectedNode() == QStringLiteral("stage/0"),
		"the dragged place and the selection follow the stage to its new id");
	view->resetLayout();
	expect(!view->hasDraggedNodes(), "Reset Layout forgets dragged places");

	// A legible fit: text is never shrunk below readable size to fit.
	harness.load(QString::fromUtf8(kMaterial), MaterialTextKind::Doom3Material, MaterialEngine::Doom3, QStringLiteral("textures/test/panel"));
	view->resize(700, 450);
	settle();
	view->fitToView();
	expect(view->zoom() * 12.0 >= 10.0, "a fit keeps node text at 10 pixels or more", QString::number(view->zoom()));
	expect(view->showsPartOfGraph(), "a graph too large to read whole is shown in part, and says so");
	view->showWholeGraph();
	expect(!view->showsPartOfGraph() && view->zoom() < 0.85, "Show Whole Graph fits everything");
	view->fitToView();
	const double zoom = view->zoom();
	applyStudioTheme(*qApp, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 150));
	settle();
	expect(std::abs(view->zoom() - zoom * 1.5) < 0.01, "a larger text size scales the graph's zoom with it", QString::number(view->zoom()));
	applyStudioTheme(*qApp, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	settle();

	// The context menu of a Doom 3 image offers its image functions.
	const int image = view->graph().indexOf(QStringLiteral("stage/0/image"));
	bool sawWrap = false;
	bool sawMenu = false;
	int attempts = 0;
	QTimer probe;
	probe.setInterval(50);
	QObject::connect(&probe, &QTimer::timeout, [&]() {
		++attempts;
		for (QWidget* widget : QApplication::topLevelWidgets()) {
			auto* menu = qobject_cast<QMenu*>(widget);
			if (!menu || !menu->isVisible()) {
				continue;
			}
			sawMenu = true;
			sawWrap = sawWrap || menu->findChild<QMenu*>(QStringLiteral("materialGraphWrapImage")) != nullptr;
			menu->close();
		}
		if (sawMenu || attempts > 100) {
			probe.stop();
		}
	});
	probe.start();
	view->showNodeMenu(image);
	settle(300);
	QElapsedTimer waited;
	waited.start();
	while (probe.isActive() && waited.elapsed() < 6000) {
		settle(50);
	}
	expect(sawMenu && sawWrap, "a Doom 3 image's menu offers Wrap In Image Function");
}

} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	Harness harness;
	harness.window.resize(1100, 600);
	harness.window.show();
	harness.window.activateWindow();
	const bool active = QTest::qWaitForWindowActive(&harness.window, 5000);
	if (!active) {
		std::cerr << "note: the test window did not become active\n";
	}
	checkPanelEdits(harness);
	checkViewBehaviour(harness);
	if (failures > 0) {
		std::cerr << failures << " material graph ui check(s) failed\n";
		return 1;
	}
	std::cout << "material graph ui passed\n";
	return 0;
}
