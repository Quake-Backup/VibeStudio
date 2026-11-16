// The Materials workbench as a widget: scanning a package, choosing
// materials, the live preview, node and property edits that rewrite the
// text, text edits that rebuild the graph, undo, keyboard navigation and
// screen-reader children in the graph, saving loose scripts, and staging
// scripts, Doom lumps and Quake II WAL headers through the host hooks;
// rename, duplicate and delete, Save All, the Maps tab, problems of the whole
// package, base packages and Override in Package, export, the find bar and
// the view state kept between sessions; texts kept per package with their
// undo history, Discard, the filter pause, Engine View state, busy states,
// swatch density, keyboard paths and the task lanes. Every package is
// generated here; no game data.

#include "app/code_editor.h"
#include "app/material_graph_view.h"
#include "app/material_library_model.h"
#include "app/material_preview_view.h"
#include "app/material_property_panel.h"
#include "app/material_tasks.h"
#include "app/material_workbench.h"
#include "app/studio_progress.h"
#include "app/studio_theme.h"
#include "core/material_usage.h"
#include "core/package_archive.h"
#include "tests/render_test_support.h"

#include <QAbstractItemModel>
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QSortFilterProxyModel>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QtEndian>

#include <algorithm>
#include <functional>
#include <iostream>

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

// Generous: the first frame waits for the 3D renderer to start, and a
// loaded machine slows every frame. A scan over a base package has taken
// over a minute beside other sessions' builds, and a step that times out
// runs against the previous library, so later checks fail with it.
bool waitFor(const std::function<bool()>& condition, int timeoutMs = 180000)
{
	QElapsedTimer timer;
	timer.start();
	while (!condition()) {
		if (timer.elapsed() > timeoutMs) {
			return false;
		}
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QThread::msleep(5);
	}
	return true;
}

void writeFile(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(bytes);
	}
}

QByteArray readFile(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray tga(QRgb a, QRgb b, int size = 32)
{
	QByteArray bytes(18, '\0');
	bytes[2] = 2;
	qToLittleEndian<quint16>(static_cast<quint16>(size), bytes.data() + 12);
	qToLittleEndian<quint16>(static_cast<quint16>(size), bytes.data() + 14);
	bytes[16] = 32;
	bytes[17] = 0x28;
	for (int y = 0; y < size; ++y) {
		for (int x = 0; x < size; ++x) {
			const QRgb pixel = ((x / 8 + y / 8) % 2) == 0 ? a : b;
			bytes.append(static_cast<char>(qBlue(pixel)));
			bytes.append(static_cast<char>(qGreen(pixel)));
			bytes.append(static_cast<char>(qRed(pixel)));
			bytes.append(static_cast<char>(qAlpha(pixel)));
		}
	}
	return bytes;
}

struct Lump {
	QByteArray name;
	QByteArray data;
};

QByteArray doomWad(const QVector<Lump>& lumps)
{
	QByteArray body;
	QByteArray directory;
	int offset = 12;
	for (const Lump& lump : lumps) {
		QByteArray entry(16, '\0');
		qToLittleEndian<qint32>(lump.data.isEmpty() ? 0 : offset, entry.data());
		qToLittleEndian<qint32>(static_cast<qint32>(lump.data.size()), entry.data() + 4);
		for (int i = 0; i < 8 && i < lump.name.size(); ++i) {
			entry[8 + i] = lump.name.at(i);
		}
		directory.append(entry);
		body.append(lump.data);
		offset += static_cast<int>(lump.data.size());
	}
	QByteArray header("PWAD", 4);
	header.resize(12);
	qToLittleEndian<qint32>(static_cast<qint32>(lumps.size()), header.data() + 4);
	qToLittleEndian<qint32>(offset, header.data() + 8);
	return header + body + directory;
}

QByteArray doomFixture()
{
	QByteArray palette;
	for (int index = 0; index < 256; ++index) {
		palette.append(static_cast<char>(index));
		palette.append(static_cast<char>((index * 7) % 256));
		palette.append(static_cast<char>(255 - index));
	}
	QByteArray colormap;
	for (int level = 0; level < 34; ++level) {
		for (int index = 0; index < 256; ++index) {
			colormap.append(static_cast<char>(level < 32 ? std::max(0, index - level * 4) : index));
		}
	}
	const auto flat = [](int index) { return QByteArray(64 * 64, static_cast<char>(index)); };
	return doomWad({{"PLAYPAL", palette.repeated(14)}, {"COLORMAP", colormap}, {"F_START", {}}, {"NUKAGE1", flat(100)}, {"NUKAGE2", flat(140)},
		{"NUKAGE3", flat(180)}, {"F_END", {}}});
}

// A Doom wall and a flat both named SAME (Doom keeps the two apart).
QByteArray doomSameNameFixture()
{
	const auto put16 = [](QByteArray* bytes, int value) {
		const qsizetype at = bytes->size();
		bytes->resize(at + 2);
		qToLittleEndian<quint16>(static_cast<quint16>(value), bytes->data() + at);
	};
	const auto put32 = [](QByteArray* bytes, int value) {
		const qsizetype at = bytes->size();
		bytes->resize(at + 4);
		qToLittleEndian<qint32>(value, bytes->data() + at);
	};
	const auto name8 = [](const char* name) {
		QByteArray bytes(name);
		bytes.resize(8, '\0');
		return bytes;
	};
	QByteArray pnames;
	put32(&pnames, 1);
	pnames += name8("PATCHA");
	// A 16 x 16 patch of one colour, one post per column.
	QByteArray patch;
	put16(&patch, 16);
	put16(&patch, 16);
	put16(&patch, 0);
	put16(&patch, 0);
	for (int x = 0; x < 16; ++x) {
		put32(&patch, 8 + 4 * 16 + x * (4 + 16 + 1));
	}
	for (int x = 0; x < 16; ++x) {
		patch.append('\0');
		patch.append(static_cast<char>(16));
		patch.append('\0');
		patch += QByteArray(16, static_cast<char>(60));
		patch.append('\0');
		patch.append('\xFF');
	}
	QByteArray texture1;
	put32(&texture1, 1);
	put32(&texture1, 8);
	texture1 += name8("SAME");
	put32(&texture1, 0);
	put16(&texture1, 16);
	put16(&texture1, 16);
	put32(&texture1, 0);
	put16(&texture1, 1);
	put16(&texture1, 0);
	put16(&texture1, 0);
	put16(&texture1, 0);
	put16(&texture1, 1);
	put16(&texture1, 0);
	return doomWad({{"PNAMES", pnames}, {"TEXTURE1", texture1}, {"P_START", {}}, {"PATCHA", patch}, {"P_END", {}}, {"F_START", {}},
		{"SAME", QByteArray(64 * 64, static_cast<char>(120))}, {"F_END", {}}});
}

QByteArray quake2Wal(const QByteArray& name, quint32 flags, qint32 value)
{
	QByteArray bytes(100, '\0');
	bytes.replace(0, name.size(), name);
	qToLittleEndian<quint32>(32, bytes.data() + 32);
	qToLittleEndian<quint32>(32, bytes.data() + 36);
	quint32 offset = 100;
	for (int level = 0; level < 4; ++level) {
		qToLittleEndian<quint32>(offset, bytes.data() + 40 + 4 * level);
		offset += static_cast<quint32>((32 >> level) * (32 >> level));
	}
	qToLittleEndian<quint32>(flags, bytes.data() + 88);
	qToLittleEndian<qint32>(value, bytes.data() + 96);
	for (int level = 0; level < 4; ++level) {
		const int side = 32 >> level;
		bytes.append(QByteArray(side * side, static_cast<char>(40 + level)));
	}
	return bytes;
}

const char* kShaders = R"(// Workbench fixture
textures/test/glow
{
	surfaceparm nolightmap
	{
		map textures/test/glow.tga
		tcMod scroll 0.5 0
		rgbGen wave sin 0.5 0.5 0 1
	}
}

textures/test/anim
{
	{
		animMap 2 textures/test/a.tga textures/test/b.tga
	}
}

textures/test/bad
{
	{
		map textures/test/glow.tga
		frobnicate 1
	}
}
)";

const char* kMaterials = R"(table pulseTable { { 0, 1 } }

textures/test/panel
{
	diffusemap textures/test/panel_d
	specularmap textures/test/panel_s
}

textures/test/pulse
{
	{
		blend add
		map textures/test/panel_d
		rgb pulseTable[time]
	}
}
)";

std::shared_ptr<PackageArchive> openPackage(const QString& path)
{
	auto archive = std::make_shared<PackageArchive>();
	QString error;
	expect(archive->load(path, &error), "the fixture package opens", error);
	return archive;
}

bool hasNode(MaterialGraphView* graph, const QString& id)
{
	return graph->graph().indexOf(id) >= 0;
}

} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	// MATERIAL_TEST_LOOK=high-contrast-light, -dark, light or dark;
	// MATERIAL_TEST_SCALE=200; MATERIAL_TEST_RTL=1: the same checks in another
	// look, for snapshots of the page at the sizes and directions it supports.
	const QString look = qEnvironmentVariable("MATERIAL_TEST_LOOK");
	const StudioTheme theme = look == QStringLiteral("high-contrast-light") ? StudioTheme::HighContrastLight
		: look == QStringLiteral("high-contrast-dark")						  ? StudioTheme::HighContrastDark
		: look == QStringLiteral("light")									  ? StudioTheme::Light
																			  : StudioTheme::Dark;
	const int scale = qEnvironmentVariableIsSet("MATERIAL_TEST_SCALE") ? qEnvironmentVariableIntValue("MATERIAL_TEST_SCALE") : 100;
	applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, std::clamp(scale, 100, 200)));
	if (qEnvironmentVariableIntValue("MATERIAL_TEST_RTL") == 1) {
		QApplication::setLayoutDirection(Qt::RightToLeft);
	}
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	const QDir root(temp.path());
	const QString q3 = root.filePath(QStringLiteral("q3"));
	writeFile(q3 + QStringLiteral("/textures/test/glow.tga"), tga(qRgb(255, 200, 80), qRgb(30, 20, 10)));
	writeFile(q3 + QStringLiteral("/textures/test/a.tga"), tga(qRgb(255, 0, 0), qRgb(255, 0, 0)));
	writeFile(q3 + QStringLiteral("/textures/test/b.tga"), tga(qRgb(0, 0, 255), qRgb(0, 0, 255)));
	writeFile(q3 + QStringLiteral("/scripts/test.shader"), kShaders);
	const QString d3 = root.filePath(QStringLiteral("d3"));
	writeFile(d3 + QStringLiteral("/textures/test/panel_d.tga"), tga(qRgb(160, 160, 170), qRgb(80, 80, 90)));
	writeFile(d3 + QStringLiteral("/textures/test/panel_s.tga"), tga(qRgb(90, 90, 90), qRgb(20, 20, 20)));
	writeFile(d3 + QStringLiteral("/materials/test.mtr"), kMaterials);
	const QString wad = root.filePath(QStringLiteral("test.wad"));
	writeFile(wad, doomFixture());
	const QString q2 = root.filePath(QStringLiteral("q2"));
	writeFile(q2 + QStringLiteral("/textures/e1u1/water1.wal"), quake2Wal("e1u1/water1", 0x8, 0));

	MaterialWorkbench workbench;
	QStringList staged;
	QHash<QString, QByteArray> stagedBytes;
	QStringList shownTextures;
	MaterialWorkbenchHost host;
	host.stageFile = [&](const QByteArray& bytes, const QString& path, QString*) {
		staged << path;
		stagedBytes.insert(path, bytes);
		return true;
	};
	host.stageLump = [&](const QByteArray& bytes, const QString& name, QString*) {
		staged << QStringLiteral("lump:") + name;
		stagedBytes.insert(QStringLiteral("lump:") + name, bytes);
		return true;
	};
	host.showTexture = [&](const QString& reference) { shownTextures << reference; };
	QStringList openedMaps;
	host.openMapUse = [&](const MaterialMapUse& use, const QString& material, QString*) {
		openedMaps << use.mapPath + QLatin1Char('|') + material;
		return true;
	};
	QStringList revealed;
	host.revealInPackage = [&](const QString& path) { revealed << path; };
	QByteArray savedState;
	host.saveState = [&](const QByteArray& state) { savedState = state; };
	host.loadState = [&]() { return savedState; };
	workbench.setHost(host);
	workbench.resize(1400, 900);
	workbench.show();
	const auto idle = [&]() {
		const bool done = waitFor([&]() { return !workbench.isBusy(); });
		if (!done) {
			std::cerr << "  still busy: " << workbench.busyReason().toStdString() << std::endl;
		}
		return done;
	};
	// Without OpenGL or Vulkan the preview finishes with the reason instead of
	// a picture; everything else on the page works the same.
	const int renderSkip = vibestudio::test_support::exitCodeWithoutRenderer("material-workbench-ui-smoke");
	expect(renderSkip != 1, "VIBESTUDIO_RENDER_REQUIRE is set, but no 3D renderer starts");
	const bool canRender = renderSkip < 0;
	const auto rendered = [&]() {
		// A playing preview always has a frame coming; a drawn one is enough.
		return waitFor([&]() {
			const bool finished = canRender ? !workbench.preview()->lastImage().isNull() : !workbench.preview()->lastResult().error.isEmpty();
			return !workbench.isBusy() && finished && (workbench.preview()->isPlaying() || !workbench.preview()->renderPending());
		});
	};

	// Scanning and choosing.
	auto archive = openPackage(q3);
	workbench.setPackage(archive, q3, QStringLiteral("q3-1"));
	expect(workbench.hasContent(), "a scanning workbench has content to show");
	expect(idle(), "the Quake III package scans");
	expect(workbench.libraryModel()->rowCount() >= 3, "the library lists the shaders", QString::number(workbench.libraryModel()->rowCount()));
	expect(workbench.selectMaterial(QStringLiteral("textures/test/glow")), "a shader can be chosen by name");
	expect(rendered(), "the preview draws the shader");
	expect(workbench.currentTextKind() == MaterialTextKind::Quake3Shader && workbench.currentText().contains(QStringLiteral("textures/test/glow")),
		"the text editor shows the shader script");
	expect(!workbench.preview()->lastResult().fallback, "a valid shader is drawn, not the default shader");
	MaterialGraphView* graph = workbench.graphView();
	expect(hasNode(graph, QStringLiteral("stage/0/tcmod/0")) && hasNode(graph, QStringLiteral("material")), "the graph shows the stage's nodes");

	// A graph edit rewrites the text as one undo step.
	MaterialGraphEdit frequency;
	frequency.kind = MaterialGraphEditKind::SetProperty;
	frequency.node = QStringLiteral("stage/0/rgbgen");
	frequency.property = QStringLiteral("frequency");
	frequency.value = QStringLiteral("2");
	QString error;
	expect(workbench.applyGraphEdit(frequency, &error), "a property edit through the graph applies", error);
	expect(workbench.currentText().contains(QStringLiteral("rgbGen wave sin 0.5 0.5 0 2")), "the graph edit lands in the text");
	expect(workbench.hasUnsavedChanges(), "an edit marks the text unsaved");
	workbench.editor()->undo();
	QCoreApplication::processEvents();
	expect(workbench.currentText().contains(QStringLiteral("rgbGen wave sin 0.5 0.5 0 1")), "Undo takes the graph edit back");
	workbench.editor()->redo();
	QCoreApplication::processEvents();
	expect(workbench.currentText().contains(QStringLiteral("0.5 0.5 0 2")), "Redo brings it again");
	// Quick repeated changes of one property are one undo step.
	MaterialGraphEdit quick = frequency;
	quick.value = QStringLiteral("3");
	expect(workbench.applyGraphEdit(quick, &error), "a quick property change applies", error);
	quick.value = QStringLiteral("4");
	expect(workbench.applyGraphEdit(quick, &error) && workbench.currentText().contains(QStringLiteral("0.5 0.5 0 4")), "a second quick change applies", error);
	workbench.editor()->undo();
	QCoreApplication::processEvents();
	expect(workbench.currentText().contains(QStringLiteral("0.5 0.5 0 2")), "one Undo takes back quick repeated changes of a property",
		workbench.currentText());
	workbench.editor()->redo();
	QCoreApplication::processEvents();
	workbench.applyGraphEdit(frequency, &error);

	// The property panel edits the selected node.
	graph->selectNode(QStringLiteral("stage/0/tcmod/0"));
	QCoreApplication::processEvents();
	auto* field = qobject_cast<QLineEdit*>(workbench.propertyPanel()->editorFor(QStringLiteral("s")));
	expect(field != nullptr, "a tcMod scroll node offers its S speed as a field");
	if (field) {
		field->setText(QStringLiteral("0.75"));
		Q_EMIT field->editingFinished();
		QCoreApplication::processEvents();
		expect(workbench.currentText().contains(QStringLiteral("tcMod scroll 0.75 0")), "editing a property field rewrites the directive",
			workbench.currentText());
	}

	// Typing in the text rebuilds the graph.
	QString text = workbench.currentText();
	text.replace(QStringLiteral("tcMod scroll 0.75 0"), QStringLiteral("tcMod scroll 0.75 0\n\t\ttcMod scale 2 2"));
	workbench.editor()->selectAll();
	workbench.editor()->insertPlainText(text);
	expect(waitFor([&]() { return hasNode(graph, QStringLiteral("stage/0/tcmod/1")); }, 10000), "a typed tcMod appears as a node");

	// Keyboard: Delete removes the selected node; arrows walk the wires.
	graph->setFocus();
	graph->selectNode(QStringLiteral("stage/0/tcmod/1"));
	QTest::keyClick(graph, Qt::Key_Delete);
	QCoreApplication::processEvents();
	expect(!workbench.currentText().contains(QStringLiteral("tcMod scale")), "Delete in the graph removes the node's directive");
	graph->selectNode(QStringLiteral("material"));
	// Right to left the graph flows the other way, and so do the keys.
	QTest::keyClick(graph, QApplication::layoutDirection() == Qt::RightToLeft ? Qt::Key_Right : Qt::Key_Left);
	expect(graph->selectedNode().startsWith(QStringLiteral("stage/")), "the leading arrow from the output walks to a stage feeding it",
		graph->selectedNode());
	QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(graph);
	expect(accessible && accessible->role() == QAccessible::List && accessible->childCount() == graph->nodeCount(),
		"the graph is a list of its nodes to screen readers");
	if (accessible && graph->selectedIndex() >= 0) {
		QAccessibleInterface* child = accessible->child(graph->selectedIndex());
		expect(child && child->text(QAccessible::Name) == graph->nodeAccessibleName(graph->selectedIndex()) && child->state().selected,
			"the selected node is a named, selected list item");
		expect(accessible->indexOfChild(child) == graph->selectedIndex(), "nodes know their place in the list");
	}

	// Filtering by the studio query language. Typing filters after a short
	// pause, so each check waits for it.
	auto* proxy = qobject_cast<QSortFilterProxyModel*>(workbench.libraryView()->model());
	const QString textBeforeFilter = workbench.currentText();
	workbench.filterField()->setText(QStringLiteral("animated=yes kind=shader"));
	expect(proxy && waitFor([&]() { return proxy->rowCount() == 2; }, 5000), "animated=yes keeps the two animated shaders",
		proxy ? QString::number(proxy->rowCount()) : QString());
	workbench.filterField()->setText(QStringLiteral("nosuchkey=1"));
	auto* count = workbench.findChild<QLabel*>(QStringLiteral("materialLibraryCount"));
	expect(count && waitFor([&]() { return count->text().contains(QStringLiteral("nosuchkey")); }, 5000), "a key no material has is named",
		count ? count->text() : QString());
	// A filter that hides the open material leaves it open, edits and all.
	expect(workbench.currentMaterial() == QStringLiteral("textures/test/glow") && workbench.currentText() == textBeforeFilter,
		"a filter that hides the open material does not open another", workbench.currentMaterial());
	// From the Textures page: the materials that read an image.
	workbench.showMaterialsUsingImage(QStringLiteral("textures/test/glow.tga"));
	QCoreApplication::processEvents();
	expect(proxy && proxy->rowCount() == 2 && (workbench.currentMaterial() == QStringLiteral("textures/test/glow")
												   || workbench.currentMaterial() == QStringLiteral("textures/test/bad")),
		"an image lists the materials reading it", proxy ? QString::number(proxy->rowCount()) : QString());
	workbench.filterField()->clear();
	waitFor([&]() { return proxy && proxy->rowCount() == workbench.libraryModel()->rowCount(); }, 5000);
	// From the Levels page: a Quake III map names shaders without textures/.
	workbench.revealMaterial(QStringLiteral("test/anim"));
	QCoreApplication::processEvents();
	expect(workbench.currentMaterial() == QStringLiteral("textures/test/anim"), "a map's texture name finds its shader",
		workbench.currentMaterial());

	// A shader the engine drops is drawn as its fallback, with the reason.
	workbench.selectMaterial(QStringLiteral("textures/test/bad"));
	expect(rendered(), "the rejected shader renders");
	expect(workbench.preview()->lastResult().fallback, "Quake III's default shader stands in for a dropped shader");
	expect(workbench.problemsList()->topLevelItemCount() > 0, "the problems list explains it");

	// Staging a package script.
	workbench.selectMaterial(QStringLiteral("textures/test/glow"));
	expect(idle(), "back to the glow shader");
	staged.clear();
	expect(workbench.save(&error), "saving a package script stages it", error);
	expect(staged.contains(QStringLiteral("scripts/test.shader")) && stagedBytes.value(QStringLiteral("scripts/test.shader")).contains("0.5 0.5 0 2"),
		"the staged script holds the edits", staged.join(QLatin1Char(',')));
	expect(!workbench.hasUnsavedChanges(), "after saving nothing is unsaved");

	// Engine View choices are kept, and the menu shows choices made elsewhere.
	{
		auto* engineView = workbench.findChild<QToolButton*>(QStringLiteral("materialEngineView"));
		QMenu* menu = engineView ? engineView->menu() : nullptr;
		const auto actionNamed = [&](const QString& text) -> QAction* {
			if (!menu) {
				return nullptr;
			}
			Q_EMIT menu->aboutToShow();
			for (QAction* action : menu->actions()) {
				if (action->text() == text) {
					return action;
				}
			}
			return nullptr;
		};
		QAction* overbright = actionNamed(QStringLiteral("Overbright Display"));
		expect(overbright != nullptr, "the Engine View menu offers Overbright Display for a Quake III shader");
		if (overbright) {
			const bool was = overbright->isChecked();
			savedState.clear();
			overbright->trigger();
			const QByteArray wanted = was ? QByteArrayLiteral("\"overbright\":false") : QByteArrayLiteral("\"overbright\":true");
			expect(waitFor([&]() { return savedState.contains(wanted); }, 5000), "an Engine View choice is saved with the page's state",
				QString::fromUtf8(savedState));
		}
		MaterialRenderOptions options = workbench.preview()->renderOptions();
		options.checker = !options.checker;
		workbench.preview()->setRenderOptions(options);
		QAction* checker = actionNamed(QStringLiteral("Checkerboard Behind"));
		expect(checker && checker->isChecked() == options.checker, "the Engine View menu shows a choice made in Preview Settings");
		options.checker = !options.checker;
		workbench.preview()->setRenderOptions(options);
	}

	// New materials from templates.
	expect(workbench.addMaterialFromTemplate(QStringLiteral("q3-glow"), QStringLiteral("textures/test/lamp"), QString(), &error),
		"a template adds a material", error);
	expect(workbench.currentMaterial() == QStringLiteral("textures/test/lamp") && workbench.currentText().contains(QStringLiteral("textures/test/lamp")),
		"the new material is selected in the text");
	expect(!workbench.addMaterialFromTemplate(QStringLiteral("q3-glow"), QStringLiteral("textures/test/lamp"), QString(), &error),
		"a name the script already defines is refused");
	expect(!workbench.addMaterialFromTemplate(QStringLiteral("q3-glow"), QStringLiteral("textures/test/two words"), QString(), &error) && !error.isEmpty(),
		"a name that does not read back as one name is refused", error);
	workbench.revert();

	// A loose script saves in place.
	const QString loose = root.filePath(QStringLiteral("loose/scripts/mine.shader"));
	writeFile(loose, kShaders);
	expect(workbench.openScript(loose, &error), "a loose script opens", error);
	expect(idle(), "the loose script is read with the package");
	expect(workbench.currentDocumentTitle().contains(QStringLiteral("mine.shader")), "the loose script's own definitions are shown",
		workbench.currentDocumentTitle());
	MaterialGraphEdit blend;
	blend.kind = MaterialGraphEditKind::SetProperty;
	blend.node = QStringLiteral("stage/0");
	blend.property = QStringLiteral("blend");
	blend.value = QStringLiteral("add");
	if (workbench.currentMaterial() != QStringLiteral("textures/test/glow")) {
		workbench.selectMaterial(QStringLiteral("textures/test/glow"));
		idle();
	}
	expect(workbench.applyGraphEdit(blend, &error), "the stage's blend can be set from the graph", error);
	expect(workbench.save(&error), "the loose script saves", error);
	expect(readFile(loose).contains("blendFunc GL_ONE GL_ONE") || readFile(loose).contains("blendFunc add"), "the file on disk has the new blend",
		QString::fromUtf8(readFile(loose)));

	// Doom 3: interactions, and expressions as nodes.
	auto doom3 = openPackage(d3);
	workbench.setPackage(doom3, d3, QStringLiteral("d3-1"));
	expect(idle(), "the Doom 3 folder scans");
	expect(workbench.selectMaterial(QStringLiteral("textures/test/pulse")), "a Doom 3 material can be chosen");
	expect(rendered(), "the Doom 3 material renders");
	expect(workbench.currentTextKind() == MaterialTextKind::Doom3Material, "Doom 3 text is a material decl");
	MaterialGraphEdit wrap;
	wrap.kind = MaterialGraphEditKind::WrapExpression;
	wrap.node = QStringLiteral("stage/0/color/red/e");
	wrap.value = QStringLiteral("multiply");
	expect(workbench.applyGraphEdit(wrap, &error), "an expression can be wrapped in an operator", error);
	expect(workbench.currentText().contains(QStringLiteral("pulseTable[ time ] * ")) || workbench.currentText().contains(QStringLiteral("* ")),
		"the wrapped expression is written back", workbench.currentText());

	// Doom: animations as SWANTBLS, staged as Boom lumps.
	auto doom = openPackage(wad);
	workbench.setPackage(doom, wad, QStringLiteral("wad-1"));
	expect(idle(), "the Doom WAD scans");
	// The Doom 3 edit waits under its own package, unsaved; it is never
	// staged into the WAD. (The shell asks about it before another package
	// opens; here it is thrown away.)
	expect(workbench.unsavedTextTitles().contains(QStringLiteral("materials/test.mtr")), "the Doom 3 edit waits for its package",
		workbench.unsavedTextTitles().join(QLatin1Char(',')));
	workbench.discardUnsavedChanges();
	expect(!workbench.hasUnsavedChanges(), "Discard leaves nothing unsaved");
	expect(workbench.selectMaterial(QStringLiteral("NUKAGE1")), "a Doom flat can be chosen");
	expect(workbench.currentTextKind() == MaterialTextKind::DoomSwantbls, "Doom animations are edited as SWANTBLS text");
	MaterialGraphEdit tics;
	tics.kind = MaterialGraphEditKind::SetProperty;
	tics.node = QStringLiteral("animation");
	tics.property = QStringLiteral("tics");
	tics.value = QStringLiteral("4");
	expect(workbench.applyGraphEdit(tics, &error), "the animation's speed is a property", error);
	expect(workbench.currentText().simplified().contains(QStringLiteral("4 NUKAGE3 NUKAGE1")), "the range's speed changes in the table");
	staged.clear();
	expect(workbench.save(&error), "saving Doom tables stages lumps", error);
	expect(staged.contains(QStringLiteral("lump:ANIMATED")) && staged.contains(QStringLiteral("lump:SWITCHES")), "ANIMATED and SWITCHES are staged",
		staged.join(QLatin1Char(',')));

	// A Doom wall and flat of one name: the slot picks which one opens, and
	// the open one stays itself across a rescan.
	{
		const QString sameWad = root.filePath(QStringLiteral("same.wad"));
		writeFile(sameWad, doomSameNameFixture());
		workbench.setPackage(openPackage(sameWad), sameWad, QStringLiteral("same-1"));
		expect(idle(), "the WAD with a wall and a flat of one name scans");
		const auto openKind = [&]() { return workbench.currentDefinition().kind; };
		expect(workbench.selectMaterial(QStringLiteral("SAME"), QStringLiteral("doom-wall")) && idle() && openKind() == QStringLiteral("doom-wall"),
			"a sidedef's name opens the wall", openKind());
		expect(workbench.selectMaterial(QStringLiteral("SAME"), QStringLiteral("doom-flat")) && idle() && openKind() == QStringLiteral("doom-flat"),
			"a sector's name opens the flat of the same name", openKind());
		workbench.setPackage(openPackage(sameWad), sameWad, QStringLiteral("same-2"));
		expect(idle() && workbench.currentMaterial() == QStringLiteral("SAME") && openKind() == QStringLiteral("doom-flat"),
			"the open flat stays the flat when the package is read again", openKind());
		workbench.revealMaterial(QStringLiteral("SAME"), {}, QStringLiteral("doom-wall"));
		expect(idle() && openKind() == QStringLiteral("doom-wall"), "Reveal Material for a sidedef opens the wall", openKind());
	}

	// Quake II: the WAL header as .wal_json.
	auto quake2 = openPackage(q2);
	workbench.setPackage(quake2, q2, QStringLiteral("q2-1"));
	expect(idle(), "the Quake II folder scans");
	expect(workbench.selectMaterial(QStringLiteral("e1u1/water1")), "a WAL can be chosen");
	expect(workbench.currentTextKind() == MaterialTextKind::Quake2WalJson, "a WAL header is edited as .wal_json");
	expect(rendered(), "the warped WAL renders");

	// Whole definitions: duplicate, rename through the material node's name,
	// delete; each one undo step in the text.
	workbench.setPackage(archive, q3, QStringLiteral("q3-defs"));
	expect(idle(), "the Quake III package scans for definition edits");
	workbench.selectMaterial(QStringLiteral("textures/test/glow"));
	expect(idle(), "the glow shader loads for definition edits");
	expect(workbench.duplicateCurrentMaterial(QStringLiteral("textures/test/glow_copy"), &error), "a material can be duplicated", error);
	expect(workbench.currentMaterial() == QStringLiteral("textures/test/glow_copy") && workbench.currentText().contains(QStringLiteral("textures/test/glow\n"))
			&& workbench.currentText().contains(QStringLiteral("textures/test/glow_copy\n")),
		"the copy follows the original and is selected", workbench.currentMaterial());
	MaterialGraphEdit renaming;
	renaming.kind = MaterialGraphEditKind::SetProperty;
	renaming.node = QStringLiteral("material");
	renaming.property = QStringLiteral("name");
	renaming.value = QStringLiteral("textures/test/glow_renamed");
	expect(workbench.applyGraphEdit(renaming, &error), "the material node's name renames it", error);
	expect(workbench.currentMaterial() == QStringLiteral("textures/test/glow_renamed") && !workbench.currentText().contains(QStringLiteral("glow_copy")),
		"the rename keeps the material selected under its new name", workbench.currentMaterial());
	expect(!workbench.renameCurrentMaterial(QStringLiteral("textures/test/glow"), &error), "a rename onto another definition's name is refused");
	expect(!workbench.renameCurrentMaterial(QStringLiteral("textures/test/bad name"), &error) && !error.isEmpty(), "a rename to a name the script cannot hold is refused",
		error);
	expect(workbench.deleteCurrentMaterial(&error), "a material can be deleted", error);
	expect(!workbench.currentText().contains(QStringLiteral("glow_renamed")) && !workbench.currentMaterial().isEmpty(),
		"deleting removes the definition and selects another", workbench.currentMaterial());
	workbench.editor()->undo();
	QCoreApplication::processEvents();
	expect(workbench.currentText().contains(QStringLiteral("glow_renamed")), "Undo brings the deleted definition back");
	workbench.revert();
	idle();

	// Save All: two texts with unsaved changes, both staged.
	workbench.selectMaterial(QStringLiteral("textures/test/glow"));
	idle();
	expect(workbench.applyGraphEdit(frequency, &error), "an edit before Save All", error);
	expect(workbench.addMaterialFromTemplate(QStringLiteral("d3-standard"), QStringLiteral("textures/test/panel2"), QString(), &error),
		"a Doom 3 template starts a second text", error);
	// A Quake III template from the Doom 3 text starts a new shader script;
	// a second Doom 3 template joins the new Doom 3 script rather than start
	// another one at the same path.
	expect(workbench.addMaterialFromTemplate(QStringLiteral("q3-glow"), QStringLiteral("textures/test/lamp2"), QString(), &error),
		"a Quake III template from a Doom 3 text starts a new script", error);
	expect(workbench.addMaterialFromTemplate(QStringLiteral("d3-standard"), QStringLiteral("textures/test/panel3"), QString(), &error),
		"a second Doom 3 template is added", error);
	expect(workbench.currentText().contains(QStringLiteral("textures/test/panel2")) && workbench.currentText().contains(QStringLiteral("textures/test/panel3")),
		"the second Doom 3 template joins the new Doom 3 script", workbench.currentText());
	expect(workbench.unsavedTextTitles().size() == 3, "three texts wait to be saved", workbench.unsavedTextTitles().join(QLatin1Char(',')));
	staged.clear();
	expect(workbench.saveAll(&error), "Save All saves every changed text", error);
	expect(staged.contains(QStringLiteral("scripts/test.shader")) && staged.contains(QStringLiteral("materials/vibestudio.mtr")) && !workbench.hasUnsavedChanges(),
		"both texts are staged and nothing is left unsaved", staged.join(QLatin1Char(',')));
	expect(stagedBytes.value(QStringLiteral("materials/vibestudio.mtr")).contains("textures/test/panel2")
			&& stagedBytes.value(QStringLiteral("materials/vibestudio.mtr")).contains("textures/test/panel3")
			&& staged.count(QStringLiteral("materials/vibestudio.mtr")) == 1,
		"both Doom 3 templates are staged in one script, not one over the other", staged.join(QLatin1Char(',')));
	expect(staged.contains(QStringLiteral("scripts/vibestudio.shader")) && stagedBytes.value(QStringLiteral("scripts/vibestudio.shader")).contains("textures/test/lamp2"),
		"the new shader script is staged with its material", staged.join(QLatin1Char(',')));

	// Problems of the whole package.
	workbench.setWholePackageProblems(true);
	bool listsBad = false;
	for (int row = 0; row < workbench.problemsList()->topLevelItemCount(); ++row) {
		listsBad = listsBad || workbench.problemsList()->topLevelItem(row)->text(2).startsWith(QStringLiteral("textures/test/bad:"));
	}
	expect(listsBad, "the whole package's problems name their material");
	workbench.setWholePackageProblems(false);

	// The Maps tab: the maps that use the material, opened in Levels.
	writeFile(q3 + QStringLiteral("/maps/test.map"),
		"{\n\"classname\" \"worldspawn\"\n{\n( 0 0 0 ) ( 1 0 0 ) ( 0 1 0 ) test/glow 0 0 0 0.5 0.5 0 0 0\n"
		"( 0 0 0 ) ( 0 1 0 ) ( 0 0 1 ) test/glow 0 0 0 0.5 0.5 0 0 0\n}\n}\n");
	auto withMap = openPackage(q3);
	workbench.setPackage(withMap, q3, QStringLiteral("q3-maps"));
	expect(idle(), "the package with a map scans");
	workbench.selectMaterial(QStringLiteral("textures/test/glow"));
	idle();
	workbench.showMapUses();
	// Reading the maps is counted with the window's work (the status bar's
	// Activity ring) and turns a ring beside the summary.
	auto* mapsBusy = workbench.findChild<BusyIndicator*>(QStringLiteral("materialMapsBusy"));
	expect(workbench.usageScanning() && studioWorkTracker().activeCount() > 0 && mapsBusy && mapsBusy->isActive(),
		"reading the maps shows as work in progress", QString::number(studioWorkTracker().activeCount()));
	expect(waitFor([&]() { return !workbench.usageScanning() && workbench.usage(); }), "the maps are read in the background");
	expect(mapsBusy && !mapsBusy->isActive() && !studioWorkTracker().activeLabels().contains(QStringLiteral("Reading maps for material uses")),
		"the work ends with the reading", studioWorkTracker().activeLabels().join(QLatin1Char(',')));
	QTreeWidget* maps = workbench.mapsList();
	expect(maps->topLevelItemCount() == 1 && maps->topLevelItem(0)->text(0) == QStringLiteral("maps/test.map") && maps->topLevelItem(0)->text(1) == QStringLiteral("2"),
		"the Maps tab lists the map and its uses", maps->topLevelItemCount() > 0 ? maps->topLevelItem(0)->text(0) + QLatin1Char(' ') + maps->topLevelItem(0)->text(1) : QString());
	if (maps->topLevelItemCount() > 0) {
		Q_EMIT maps->itemActivated(maps->topLevelItem(0), 0);
		expect(openedMaps.contains(QStringLiteral("maps/test.map|textures/test/glow")), "Enter on a map opens it in Levels for the material",
			openedMaps.join(QLatin1Char(',')));
	}

	// Base packages: the Quake III folder beneath a mod; base materials are
	// read only, and Override in Package copies one into the mod.
	const QString mod = root.filePath(QStringLiteral("mod"));
	writeFile(mod + QStringLiteral("/scripts/mod.shader"), "textures/mod/lamp\n{\n\t{\n\t\tmap textures/test/glow.tga\n\t}\n}\n");
	workbench.setBasePackages({q3});
	workbench.setPackage(openPackage(mod), mod, QStringLiteral("mod-1"));
	expect(idle(), "the mod scans over its base package");
	expect(workbench.selectMaterial(QStringLiteral("textures/test/glow")) && idle(), "a base material is listed");
	expect(workbench.currentIsBase() && workbench.editor()->isReadOnly() && workbench.currentDocumentTitle().contains(QStringLiteral("read only")),
		"a base material's script is shown read only", workbench.currentDocumentTitle());
	expect(!workbench.applyGraphEdit(frequency, &error), "a base material is not edited in place");
	expect(workbench.overrideInPackage(&error), "Override in Package copies the base material", error);
	expect(workbench.currentDocumentTitle().contains(QStringLiteral("zz_vibestudio_overrides")) && !workbench.editor()->isReadOnly()
			&& workbench.currentText().contains(QStringLiteral("textures/test/glow")),
		"the copy is in a script the game reads after the base scripts", workbench.currentDocumentTitle());
	// The core rule (material_override.h) refuses a second copy into the same
	// script, as `material override` does.
	expect(!workbench.overrideInPackage(&error) && error.contains(QStringLiteral("already defines")), "a second override of the same material is refused",
		error);
	staged.clear();
	expect(workbench.save(&error) && staged.contains(QStringLiteral("scripts/zz_vibestudio_overrides.shader")), "the override is staged into the mod", error);
	workbench.selectMaterial(QStringLiteral("textures/mod/lamp"));
	expect(idle() && !workbench.currentIsBase(), "the mod's own material is not base");
	if (canRender) {
		expect(rendered() && !workbench.preview()->lastResult().fallback, "the mod's material draws with the base package's image");
		const QString exported = root.filePath(QStringLiteral("export.png"));
		expect(workbench.exportPreview(exported, 1, QSize(64, 64), &error) && QImage(exported).size() == QSize(64, 64), "the preview exports to PNG",
			error);
		// A sheet of 16 frames of 64 pixels is a 4 x 4 grid.
		const QString sheet = root.filePath(QStringLiteral("sheet.png"));
		expect(workbench.exportPreview(sheet, 16, QSize(64, 64), &error) && QImage(sheet).size() == QSize(256, 256), "an animation sheet exports as a grid",
			error);
		// In the background, with the preview covered while it draws.
		const QString background = root.filePath(QStringLiteral("background.png"));
		workbench.exportPreviewInBackground(background, 4, QSize(32, 32));
		expect(workbench.exportRunning() || QFileInfo::exists(background), "a background export starts");
		expect(waitFor([&]() { return !workbench.exportRunning(); }) && QImage(background).size() == QSize(64, 64),
			"a background export writes its sheet", QString::number(QImage(background).width()));
		// The image changes in the package beneath: the preview reads it again
		// even though the material and its references did not change.
		const QImage before = workbench.preview()->lastImage();
		writeFile(q3 + QStringLiteral("/textures/test/glow.tga"), tga(qRgb(20, 220, 40), qRgb(20, 220, 40)));
		workbench.setPackage(openPackage(mod), mod, QStringLiteral("mod-2"));
		expect(idle() && workbench.currentMaterial() == QStringLiteral("textures/mod/lamp") && rendered(), "the mod scans again");
		expect(workbench.preview()->lastImage() != before, "the preview shows the new image after the package changed");
		writeFile(q3 + QStringLiteral("/textures/test/glow.tga"), tga(qRgb(255, 200, 80), qRgb(30, 20, 10)));
	}
	workbench.setBasePackages({});

	// The find bar on the Text tab.
	workbench.editorTabs()->setCurrentIndex(1);
	QCoreApplication::processEvents();
	expect(workbench.findBar() != nullptr, "the Text tab has a find bar");
	// Save, Revert, New Material and More belong to the document: they show
	// on every tab, not only on Nodes.
	for (const QString& name : {QStringLiteral("materialSave"), QStringLiteral("materialRevert"), QStringLiteral("materialNew"), QStringLiteral("materialMore")}) {
		auto* button = workbench.findChild<QToolButton*>(name);
		expect(button && button->isVisible(), "the document's actions show on the Text tab", name);
	}

	// The view state is kept between sessions.
	workbench.libraryFilter()->setOrder(MaterialLibraryFilter::Order::Name);
	const QByteArray state = workbench.saveState();
	MaterialWorkbench restored;
	MaterialWorkbenchHost restoredHost;
	restoredHost.loadState = [state]() { return state; };
	restored.setHost(restoredHost);
	expect(restored.libraryFilter()->order() == MaterialLibraryFilter::Order::Name, "a new workbench takes the saved library order");

	// Texts belong to their package; the open text keeps its undo history and
	// place across switches; the library, export, settings and lanes behave.
	{
		const QString otherQ3 = root.filePath(QStringLiteral("q3b"));
		writeFile(otherQ3 + QStringLiteral("/textures/test/glow.tga"), tga(qRgb(10, 10, 200), qRgb(10, 10, 60)));
		writeFile(otherQ3 + QStringLiteral("/scripts/test.shader"), "textures/test/glow\n{\n\t{\n\t\tmap textures/test/glow.tga\n\t}\n}\n");
		const std::shared_ptr<PackageArchive> providedBase = openPackage(q3);
		MaterialWorkbench second;
		QStringList secondStaged;
		int baseAsked = 0;
		MaterialWorkbenchHost secondHost;
		secondHost.stageFile = [&](const QByteArray&, const QString& path, QString*) {
			secondStaged << path;
			return true;
		};
		// The studio's own copy of a base package is used, not read again.
		secondHost.baseReader = [&](const QString& path) -> std::shared_ptr<const PackageArchiveReader> {
			++baseAsked;
			return QFileInfo(path).absoluteFilePath().compare(QFileInfo(q3).absoluteFilePath(), Qt::CaseInsensitive) == 0 ? providedBase : nullptr;
		};
		second.setHost(secondHost);
		second.resize(1200, 800);
		second.show();
		const auto secondIdle = [&]() {
			const bool done = waitFor([&]() { return !second.isBusy(); });
			if (!done) {
				std::cerr << "  second still busy: " << second.busyReason().toStdString() << std::endl;
			}
			return done;
		};

		second.setPackage(openPackage(q3), q3, QStringLiteral("e2-a1"));
		expect(secondIdle() && second.selectMaterial(QStringLiteral("textures/test/glow")) && secondIdle(), "the first package opens");
		// Browsing an untouched script reuses the library's parse.
		const int parses = second.scriptParseCount();
		second.selectMaterial(QStringLiteral("textures/test/anim"));
		second.selectMaterial(QStringLiteral("textures/test/bad"));
		secondIdle();
		expect(second.scriptParseCount() == parses, "browsing an unchanged script does not parse it again",
			QString::number(second.scriptParseCount() - parses));
		// Highlighting the problems does not count as a change to parse.
		for (int round = 0; round < 5; ++round) {
			QCoreApplication::processEvents();
		}
		expect(second.busyReason() != QStringLiteral("parsing") && !second.hasUnsavedChanges(), "highlighting alone does not parse or modify the text",
			second.busyReason());
		second.selectMaterial(QStringLiteral("textures/test/glow"));
		secondIdle();
		expect(second.applyGraphEdit(frequency, &error) && second.currentText().contains(QStringLiteral("0.5 0.5 0 2")), "an edit in the first package", error);

		second.setPackage(openPackage(otherQ3), otherQ3, QStringLiteral("e2-b1"));
		expect(secondIdle(), "the second package scans");
		second.selectMaterial(QStringLiteral("textures/test/glow"));
		secondIdle();
		expect(!second.currentText().contains(QStringLiteral("rgbGen wave")), "the second package's script shows its own text, not the first package's edit",
			second.currentText());
		expect(second.hasUnsavedChanges() && second.unsavedTextTitles().contains(QStringLiteral("scripts/test.shader")),
			"the first package's edit waits, unsaved", second.unsavedTextTitles().join(QLatin1Char(',')));
		secondStaged.clear();
		expect(!second.saveAll(&error) && secondStaged.isEmpty(), "Save All does not stage the first package's text into the second",
			secondStaged.join(QLatin1Char(',')) + QLatin1Char(' ') + error);
		// Ctrl+S on the unchanged text stages nothing and points at Save All.
		for (QShortcut* shortcut : second.findChildren<QShortcut*>()) {
			if (shortcut->key() == QKeySequence(QKeySequence::Save)) {
				Q_EMIT shortcut->activated();
			}
		}
		expect(secondStaged.isEmpty(), "Ctrl+S on an unchanged text stages nothing", secondStaged.join(QLatin1Char(',')));

		// Back in the first package: the edit is there, and Undo still reaches it.
		second.setPackage(openPackage(q3), q3, QStringLiteral("e2-a2"));
		expect(secondIdle(), "the first package scans again");
		second.selectMaterial(QStringLiteral("textures/test/glow"));
		secondIdle();
		expect(second.currentText().contains(QStringLiteral("0.5 0.5 0 2")), "back in the first package its edit is there", second.currentText());
		second.editor()->undo();
		QCoreApplication::processEvents();
		expect(second.currentText().contains(QStringLiteral("rgbGen wave sin 0.5 0.5 0 1")), "Undo reaches an edit made before the switch",
			second.currentText());
		second.editor()->redo();
		QCoreApplication::processEvents();

		// The script changes in the package while it is being edited: the
		// edits stay, and the problems say what saving will do.
		const QString third = root.filePath(QStringLiteral("q3c"));
		writeFile(third + QStringLiteral("/textures/test/glow.tga"), tga(qRgb(200, 10, 10), qRgb(60, 10, 10)));
		writeFile(third + QStringLiteral("/scripts/test.shader"), kShaders);
		second.setPackage(openPackage(third), third, QStringLiteral("e2-c1"));
		secondIdle();
		second.selectMaterial(QStringLiteral("textures/test/glow"));
		secondIdle();
		expect(second.applyGraphEdit(frequency, &error), "an edit in the third package", error);
		writeFile(third + QStringLiteral("/scripts/test.shader"), QByteArray("// Changed on the Packages page\n") + kShaders);
		second.setPackage(openPackage(third), third, QStringLiteral("e2-c2"));
		expect(secondIdle(), "the changed package scans");
		bool warned = false;
		for (int row = 0; row < second.problemsList()->topLevelItemCount(); ++row) {
			warned = warned || second.problemsList()->topLevelItem(row)->text(2).contains(QStringLiteral("changed in the package"));
		}
		expect(second.currentText().contains(QStringLiteral("0.5 0.5 0 2")) && warned, "a script changed underneath keeps the edits and says so",
			second.currentText());

		// Discard throws every unsaved text away.
		second.discardUnsavedChanges();
		expect(!second.hasUnsavedChanges() && second.unsavedTextTitles().isEmpty() && second.currentText().startsWith(QStringLiteral("// Changed")),
			"Discard leaves nothing unsaved", second.unsavedTextTitles().join(QLatin1Char(',')));

		// Base packages the studio has read already are used as they are.
		second.setBasePackages({q3});
		second.setPackage(openPackage(mod), mod, QStringLiteral("e10-1"));
		expect(secondIdle() && baseAsked > 0 && second.loadedBasePackage(q3).get() == providedBase.get(),
			"a base package the studio has read is not read again", QString::number(baseAsked));
		second.setBasePackages({});
		second.setPackage(openPackage(q3), q3, QStringLiteral("e2-a3"));
		secondIdle();

		// Swatches are drawn at the screen's pixel density.
		QAbstractItemModel* library = second.libraryView()->model();
		expect(waitFor([&]() { return second.libraryModel()->pendingThumbnails() == 0; }, 60000) && library->rowCount() > 0,
			"the swatches are drawn");
		if (library->rowCount() > 0) {
			const QPixmap swatch = library->index(0, 0).data(Qt::DecorationRole).value<QPixmap>();
			expect(qFuzzyCompare(swatch.devicePixelRatio(), second.libraryModel()->devicePixelRatio())
					&& qFuzzyCompare(second.libraryModel()->devicePixelRatio(), std::max<qreal>(1.0, second.libraryView()->devicePixelRatioF())),
				"a swatch has the screen's pixel density", QString::number(swatch.devicePixelRatio()));
		}

		// Enter in the library moves to the preview; the keyboard's context
		// menu acts on the current material, not where the pointer is.
		second.selectMaterial(QStringLiteral("textures/test/anim"));
		secondIdle();
		second.libraryView()->setFocus();
		QTest::keyClick(second.libraryView(), Qt::Key_Return);
		expect(second.focusWidget() == second.preview(), "Enter in the library moves to the preview");
		QTimer closer;
		closer.setInterval(150);
		QObject::connect(&closer, &QTimer::timeout, []() {
			if (QWidget* popup = QApplication::activePopupWidget()) {
				popup->close();
			}
		});
		closer.start();
		QContextMenuEvent keyboardMenu(QContextMenuEvent::Keyboard, QPoint(2, 2), second.libraryView()->mapToGlobal(QPoint(2, 2)));
		QApplication::sendEvent(second.libraryView(), &keyboardMenu);
		closer.stop();
		expect(second.currentMaterial() == QStringLiteral("textures/test/anim"), "the keyboard's context menu keeps the current material",
			second.currentMaterial());

		// While playing, the timeline's description is not rewritten every frame.
		int descriptionChanges = 0;
		static int* counter = nullptr;
		counter = &descriptionChanges;
		static QObject* timeline = nullptr;
		timeline = second.findChild<QSlider*>(QStringLiteral("materialTimeline"));
		QAccessible::setActive(true);
		const QAccessible::UpdateHandler previous = QAccessible::installUpdateHandler([](QAccessibleEvent* event) {
			if (event->type() == QAccessible::DescriptionChanged && event->object() == timeline && counter) {
				++*counter;
			}
		});
		second.preview()->setPlaying(true);
		QElapsedTimer playing;
		playing.start();
		while (playing.elapsed() < 1000) {
			QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
			QThread::msleep(5);
		}
		second.preview()->setPlaying(false);
		QAccessible::installUpdateHandler(previous);
		QAccessible::setActive(false);
		counter = nullptr;
		expect(descriptionChanges < 5, "the timeline's description stays put while playing", QString::number(descriptionChanges));

		// Preview Settings' Reset rebuilds the dialog in place.
		second.showPreviewSettings();
		QDialog* settings = second.previewSettingsDialog();
		auto* buttons = settings ? settings->findChild<QDialogButtonBox*>() : nullptr;
		QPushButton* reset = buttons ? buttons->button(QDialogButtonBox::Reset) : nullptr;
		expect(reset != nullptr, "Preview Settings has a Reset button");
		if (reset) {
			QTest::mouseClick(reset, Qt::LeftButton);
			expect(waitFor([&]() { return second.previewSettingsDialog() && second.previewSettingsDialog() != settings; }, 5000)
					&& second.previewSettingsDialog()->isVisible(),
				"Reset shows the settings again");
			second.previewSettingsDialog()->close();
		}
	}

	// A task lane keeps one worker and publishes the last of many frames.
	{
		MaterialTaskLane lane;
		int published = -1;
		for (int index = 0; index < 100; ++index) {
			lane.submit(
				[index, &published](const MaterialTaskLane::Cancelled&) -> std::function<void()> {
					return [index, &published]() { published = index; };
				},
				false);
		}
		expect(waitFor([&]() { return !lane.pending(); }, 10000) && published == 99, "the last of many tasks publishes", QString::number(published));
		const std::atomic_bool* outside = MaterialTaskLane::currentStopFlag();
		bool inside = false;
		lane.submit([&inside](const MaterialTaskLane::Cancelled&) -> std::function<void()> {
			inside = MaterialTaskLane::currentStopFlag() != nullptr;
			return {};
		});
		waitFor([&]() { return !lane.pending(); }, 10000);
		expect(outside == nullptr && inside, "a task sees its stop flag on the worker only");
	}

	// Reduced motion: nothing starts playing by itself.
	workbench.setReducedMotion(true);
	workbench.setPackage(archive, q3, QStringLiteral("q3-2"));
	expect(idle(), "the package scans again");
	workbench.selectMaterial(QStringLiteral("textures/test/anim"));
	expect(idle(), "the animated shader loads");
	expect(!workbench.preview()->isPlaying(), "with reduced motion an animated material waits to be played");

	// Every control a screen reader meets has a name.
	for (QWidget* widget : workbench.findChildren<QWidget*>()) {
		if (!widget->isVisibleTo(&workbench) || widget->focusPolicy() == Qt::NoFocus) {
			continue;
		}
		if (qobject_cast<QLineEdit*>(widget) || widget->objectName().startsWith(QStringLiteral("material"))) {
			expect(!widget->accessibleName().isEmpty(), "focusable workbench controls have accessible names", widget->objectName());
		}
	}

	const QString snapshots = qEnvironmentVariable("VIBESTUDIO_TEST_SNAPSHOTS");
	if (!snapshots.isEmpty()) {
		workbench.setPackage(archive, q3, QStringLiteral("q3-3"));
		idle();
		workbench.selectMaterial(QStringLiteral("textures/test/glow"));
		rendered();
		workbench.grab().save(QDir(snapshots).filePath(QStringLiteral("material-workbench.png")));
	}

	if (failures > 0) {
		std::cerr << failures << " material workbench check(s) failed\n";
		return 1;
	}
	std::cout << "material workbench UI smoke passed\n";
	return 0;
}
