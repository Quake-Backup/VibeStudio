#include "app/texture_editor_dialog.h"
#include "core/asset_formats.h"
#include "app/texture_preview_worker.h"

#include "app/asset_views.h"
#include "app/texture_canvas.h"
#include "app/texture_recovery.h"
#include "app/texture_export_panel.h"
#include "core/studio_settings.h"
#include "core/texture_output.h"
#include "core/texture_handoff.h"
#include "core/texture_adjust.h"
#include "core/texture_formats.h"
#include "core/texture_generation.h"
#include "core/texture_output.h"

#include <QAction>
#include <QActionGroup>
#include <QFile>
#include <QHBoxLayout>
#include <QLocale>
#include <QPainter>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QSplitter>
#include <QTabWidget>
#include <QTabBar>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QUuid>

#include <memory>
#include <algorithm>
#include <atomic>

namespace vibestudio {

struct TextureEditorTask {
	std::atomic_bool cancelled{false};
	std::atomic_int progress{-1};
};

TextureEditorDialog::TextureEditorDialog(QWidget* parent) : QDialog(parent)
{
	setObjectName(QStringLiteral("textureEditorDialog"));
	setWindowTitle(QCoreApplication::translate("VibeStudioTextureEditor", "Texture Editor"));
	setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Texture editor"));
	setAttribute(Qt::WA_DeleteOnClose); resize(1180, 800);
	m_document.create({64, 64}, Qt::transparent); m_document.markSaved();
	m_palette.palette = generatedIdTechPalette(QStringLiteral("quake"));
	auto* layout = new QVBoxLayout(this);
	m_tools = new QToolBar(QCoreApplication::translate("VibeStudioTextureEditor", "Texture actions"));
	m_tools->setAccessibleName(m_tools->windowTitle()); m_tools->setToolButtonStyle(Qt::ToolButtonTextOnly);
	const auto action = [&](const QString& label, const QString& id, auto callback, QKeySequence shortcut = {}) {
		auto* result = m_tools->addAction(label); result->setObjectName(id);
		connect(result, &QAction::triggered, this, callback);
		if (!shortcut.isEmpty()) { result->setShortcut(shortcut); result->setShortcutContext(Qt::WidgetWithChildrenShortcut); addAction(result); }
		return result;
	};
	action(QCoreApplication::translate("VibeStudioTextureEditor", "New"), QStringLiteral("newTexture"), [this]() { newTexture(); }, QKeySequence::New);
	action(QCoreApplication::translate("VibeStudioTextureEditor", "Open…"), QStringLiteral("openTexture"), [this]() { openFile(); }, QKeySequence::Open);
	action(QCoreApplication::translate("VibeStudioTextureEditor", "Save Project"), QStringLiteral("saveTexture"), [this]() {
		if (m_projectIdentity.path.isEmpty()) { saveAs(); } else { saveProjectToPath(m_projectIdentity.path, true); }
	}, QKeySequence::Save);
	auto saveAsShortcut = QKeySequence(QKeySequence::SaveAs);
	// Some platform themes provide no binding for this standard key.
	if (saveAsShortcut.isEmpty()) { saveAsShortcut = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S); }
	action(QCoreApplication::translate("VibeStudioTextureEditor", "Save Project As…"), QStringLiteral("saveTextureAs"), [this]() { saveAs(); }, saveAsShortcut);
	action(QCoreApplication::translate("VibeStudioTextureEditor", "Export…"), QStringLiteral("exportTexture"), [this]() {
		findChild<QComboBox*>(QStringLiteral("texturePropertySection"))->setCurrentIndex(7); previewExport();
	});
	m_tools->addSeparator();
	m_undo = action(QCoreApplication::translate("VibeStudioTextureEditor", "Undo"), QStringLiteral("undoTexture"), [this]() { undo(); }, QKeySequence::Undo);
	m_redo = action(QCoreApplication::translate("VibeStudioTextureEditor", "Redo"), QStringLiteral("redoTexture"), [this]() { redo(); }, QKeySequence::Redo);
	m_tools->addSeparator();
	m_stage = action(QCoreApplication::translate("VibeStudioTextureEditor", "Stage Export"), QStringLiteral("stageTexture"), [this]() { stage(); });
	m_apply = action(QCoreApplication::translate("VibeStudioTextureEditor", "Stage and Apply"), QStringLiteral("applyTexture"), [this]() { stage(true); });
	m_stage->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Encode the selected export profile into the package plan. Review its preview and warnings. Save the package separately."));
	m_apply->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Stage a PNG or TGA and apply its texture path to the selection in a Quake III map. Map and package saves remain separate."));
	layout->addWidget(m_tools);
	m_contextLabel = new QLabel; m_contextLabel->setTextFormat(Qt::PlainText); m_contextLabel->setWordWrap(true);
	m_contextLabel->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Texture project context")); layout->addWidget(m_contextLabel);
	auto* splitter = new QSplitter;
	auto* canvasPanel = new QWidget; auto* canvasLayout = new QVBoxLayout(canvasPanel); canvasLayout->setContentsMargins(0, 0, 0, 0);
	auto* view = new QToolBar(QCoreApplication::translate("VibeStudioTextureEditor", "Canvas view"));
	view->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Canvas view"));
	m_canvas = new TextureCanvas(&m_document);
	connect(view->addAction(QCoreApplication::translate("VibeStudioTextureEditor", "Fit")), &QAction::triggered, this, [this]() { m_canvas->fit(); });
	connect(view->addAction(QStringLiteral("1:1")), &QAction::triggered, this, [this]() { m_canvas->setZoom(1); });
	connect(view->addAction(QCoreApplication::translate("VibeStudioTextureEditor", "Zoom In")), &QAction::triggered, this, [this]() { m_canvas->setZoom(m_canvas->zoom() * 1.25); });
	connect(view->addAction(QCoreApplication::translate("VibeStudioTextureEditor", "Zoom Out")), &QAction::triggered, this, [this]() { m_canvas->setZoom(m_canvas->zoom() / 1.25); });
	auto* tiles = view->addAction(QCoreApplication::translate("VibeStudioTextureEditor", "Tile Preview")); tiles->setCheckable(true);
	tiles->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Preview seams in a 3 × 3 repeat. Enable Wrap painting to edit across the repeated tiles."));
	connect(tiles, &QAction::toggled, this, [this](bool checked) { m_canvas->setTiled(checked); m_canvas->fit(); });
	auto* grid = view->addAction(QCoreApplication::translate("VibeStudioTextureEditor", "Pixel Grid")); grid->setCheckable(true); grid->setChecked(true);
	connect(grid, &QAction::toggled, this, [this](bool checked) { m_canvas->setGrid(checked); });
	m_zoomLabel = new QLabel; m_zoomLabel->setObjectName(QStringLiteral("textureZoomLevel"));
	m_zoomLabel->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Zoom level")); view->addSeparator(); view->addWidget(m_zoomLabel);
	m_canvas->zoomChanged = [this](double zoom) { m_zoomLabel->setText(QCoreApplication::translate("VibeStudioTextureEditor", "Zoom %1%").arg(QLocale().toString(qRound(zoom * 100)))); };
	m_canvas->zoomChanged(m_canvas->zoom());
	// Tool keys act while the canvas has focus, so letters still type into
	// the inspector's fields and combo boxes keep their keyboard search.
	m_toolStrip = new QToolBar(QCoreApplication::translate("VibeStudioTextureEditor", "Drawing tools"));
	m_toolStrip->setObjectName(QStringLiteral("textureToolStrip")); m_toolStrip->setAccessibleName(m_toolStrip->windowTitle());
	m_toolStrip->setOrientation(Qt::Vertical); m_toolStrip->setToolButtonStyle(Qt::ToolButtonTextOnly);
	m_toolActions = new QActionGroup(this); m_toolActions->setExclusive(true);
	const QList<QPair<QString, QKeySequence>> toolKeys{
		{QCoreApplication::translate("VibeStudioTextureEditor", "Pencil"), QKeySequence(Qt::Key_P)}, {QCoreApplication::translate("VibeStudioTextureEditor", "Eraser"), QKeySequence(Qt::Key_E)},
		{QCoreApplication::translate("VibeStudioTextureEditor", "Fill"), QKeySequence(Qt::Key_G)}, {QCoreApplication::translate("VibeStudioTextureEditor", "Eyedropper"), QKeySequence(Qt::Key_I)},
		{QCoreApplication::translate("VibeStudioTextureEditor", "Rectangle Selection"), QKeySequence(Qt::Key_M)}, {QCoreApplication::translate("VibeStudioTextureEditor", "Brush"), QKeySequence(Qt::Key_B)},
		{QCoreApplication::translate("VibeStudioTextureEditor", "Line"), QKeySequence(Qt::Key_L)}, {QCoreApplication::translate("VibeStudioTextureEditor", "Rectangle"), QKeySequence(Qt::Key_R)},
		{QCoreApplication::translate("VibeStudioTextureEditor", "Ellipse"), QKeySequence(Qt::Key_O)}, {QCoreApplication::translate("VibeStudioTextureEditor", "Gradient"), QKeySequence(Qt::SHIFT | Qt::Key_G)},
		{QCoreApplication::translate("VibeStudioTextureEditor", "Clone Stamp"), QKeySequence(Qt::Key_S)}};
	for (int index = 0; index < toolKeys.size(); ++index) {
		auto* toolAction = m_toolStrip->addAction(toolKeys[index].first); toolAction->setCheckable(true); toolAction->setActionGroup(m_toolActions);
		toolAction->setObjectName(QStringLiteral("textureToolAction%1").arg(index)); toolAction->setData(index);
		toolAction->setShortcut(toolKeys[index].second); toolAction->setShortcutContext(Qt::WidgetShortcut); m_canvas->addAction(toolAction);
		toolAction->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "%1 (%2 while the canvas has focus)").arg(toolKeys[index].first, toolKeys[index].second.toString(QKeySequence::NativeText)));
		connect(toolAction, &QAction::triggered, this, [this, index]() { m_tool->setCurrentIndex(index); });
	}
	m_toolStrip->addSeparator();
	auto* swap = m_toolStrip->addAction(QCoreApplication::translate("VibeStudioTextureEditor", "Swap Colors")); swap->setObjectName(QStringLiteral("swapTextureColors"));
	swap->setShortcut(QKeySequence(Qt::Key_X)); swap->setShortcutContext(Qt::WidgetShortcut); m_canvas->addAction(swap);
	swap->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Exchange the paint and secondary colors (X while the canvas has focus). Right-click paints with the secondary color."));
	connect(swap, &QAction::triggered, this, &TextureEditorDialog::swapColors);
	canvasLayout->addWidget(view);
	auto* canvasRow = new QHBoxLayout; canvasRow->setContentsMargins(0, 0, 0, 0);
	canvasRow->addWidget(m_toolStrip); canvasRow->addWidget(m_canvas, 1); canvasLayout->addLayout(canvasRow, 1);
	m_dimensions = new QLabel; m_dimensions->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Texture dimensions and save state"));
	m_pixelLabel = new QLabel; m_pixelLabel->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Pixel cursor and color"));
	canvasLayout->addWidget(m_dimensions); canvasLayout->addWidget(m_pixelLabel); splitter->addWidget(canvasPanel);
	auto* properties = new QTabWidget; m_properties = properties;
	properties->setObjectName(QStringLiteral("textureProperties"));
	properties->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Texture properties"));
	const auto propertyTab = [&](const QString& label) {
		auto* panel = new QWidget; auto* fields = new QFormLayout(panel);
		fields->setRowWrapPolicy(QFormLayout::WrapAllRows); fields->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
		auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setWidget(panel); scroll->setAccessibleName(label);
		properties->addTab(scroll, label); return fields;
	};
	auto* form = propertyTab(QCoreApplication::translate("VibeStudioTextureEditor", "Paint"));
	const auto spin = [&](const QString& label, const QString& name, int minimum, int maximum) {
		auto* field = new QSpinBox; field->setObjectName(name); field->setAccessibleName(label); field->setRange(minimum, maximum); field->setKeyboardTracking(false); form->addRow(label, field); return field;
	};
	const auto button = [&](const QString& label, const QString& id, auto callback) {
		auto* result = new QPushButton(label); result->setObjectName(id); result->setAccessibleName(label); result->setAutoDefault(false);
		connect(result, &QPushButton::clicked, this, callback); form->addRow(result); return result;
	};
	const auto anchorChoice = [&](const QString& label, const QString& id) {
		auto* field = new QComboBox; field->setObjectName(id); field->setAccessibleName(label);
		const QStringList labels{QCoreApplication::translate("VibeStudioTextureEditor", "Top left"), QCoreApplication::translate("VibeStudioTextureEditor", "Top"),
			QCoreApplication::translate("VibeStudioTextureEditor", "Top right"), QCoreApplication::translate("VibeStudioTextureEditor", "Left"),
			QCoreApplication::translate("VibeStudioTextureEditor", "Center"), QCoreApplication::translate("VibeStudioTextureEditor", "Right"),
			QCoreApplication::translate("VibeStudioTextureEditor", "Bottom left"), QCoreApplication::translate("VibeStudioTextureEditor", "Bottom"),
			QCoreApplication::translate("VibeStudioTextureEditor", "Bottom right")};
		for (int index = 0; index < labels.size(); ++index) { field->addItem(labels[index], textureAnchorId(static_cast<TextureAnchor>(index))); }
		field->setCurrentIndex(int(TextureAnchor::Center)); form->addRow(label, field); return field;
	};
	m_tool = new QComboBox; m_tool->setObjectName(QStringLiteral("textureTool"));
	m_tool->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Drawing tool"));
	m_tool->addItems({QCoreApplication::translate("VibeStudioTextureEditor", "Pencil"), QCoreApplication::translate("VibeStudioTextureEditor", "Eraser"),
		QCoreApplication::translate("VibeStudioTextureEditor", "Fill"), QCoreApplication::translate("VibeStudioTextureEditor", "Eyedropper"), QCoreApplication::translate("VibeStudioTextureEditor", "Rectangle Selection"),
		QCoreApplication::translate("VibeStudioTextureEditor", "Brush"), QCoreApplication::translate("VibeStudioTextureEditor", "Line"),
		QCoreApplication::translate("VibeStudioTextureEditor", "Rectangle"), QCoreApplication::translate("VibeStudioTextureEditor", "Ellipse"),
		QCoreApplication::translate("VibeStudioTextureEditor", "Gradient"), QCoreApplication::translate("VibeStudioTextureEditor", "Clone Stamp")});
	form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Tool"), m_tool);
	connect(m_tool, &QComboBox::currentIndexChanged, this, [this](int index) {
		m_canvas->setTool(static_cast<TextureCanvas::Tool>(index));
		if (auto* action = m_toolActions->actions().value(index)) { action->setChecked(true); }
	});
	m_toolActions->actions().first()->setChecked(true);
	auto* brush = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Brush width"), QStringLiteral("textureBrushWidth"), 1, 128);
	connect(brush, &QSpinBox::valueChanged, this, [this](int width) { m_canvas->setBrushWidth(width); });
	auto* brushShape = new QComboBox; brushShape->setObjectName(QStringLiteral("textureBrushShape"));
	brushShape->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Brush shape"));
	brushShape->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Square"), QStringLiteral("square"));
	brushShape->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Round"), QStringLiteral("round")); brushShape->setCurrentIndex(1);
	form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Brush shape"), brushShape);
	connect(brushShape, &QComboBox::currentIndexChanged, this, [this, brushShape]() {
		TextureBrushShape shape; if (textureBrushShapeFromId(brushShape->currentData().toString(), &shape)) { m_canvas->setBrushShape(shape); }
	});
	auto* paintMode = new QComboBox; paintMode->setObjectName(QStringLiteral("texturePaintMode"));
	paintMode->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Paint mode"));
	paintMode->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Replace RGBA"), QStringLiteral("replace"));
	paintMode->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Blend over"), QStringLiteral("source-over")); paintMode->setCurrentIndex(1);
	paintMode->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Blend over applies color alpha once per pixel in each stroke or shape. Pencil always replaces RGBA; Eraser writes transparent pixels."));
	form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Paint mode"), paintMode);
	connect(paintMode, &QComboBox::currentIndexChanged, this, [this, paintMode]() {
		TexturePaintMode mode; if (texturePaintModeFromId(paintMode->currentData().toString(), &mode)) { m_canvas->setPaintMode(mode); }
	});
	auto* filledShapes = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Filled shapes")); filledShapes->setObjectName(QStringLiteral("textureFilledShapes"));
	filledShapes->setAccessibleName(filledShapes->text()); form->addRow(filledShapes);
	connect(filledShapes, &QCheckBox::toggled, this, [this](bool filled) { m_canvas->setFilledShapes(filled); });
	auto* tolerance = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Fill tolerance"), QStringLiteral("textureFillTolerance"), 0, 255);
	tolerance->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Maximum difference in each original RGBA channel from the starting pixel, 0–255. Fill always replaces RGBA."));
	auto* wrap = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Wrap painting")); wrap->setObjectName(QStringLiteral("textureWrapPainting"));
	wrap->setAccessibleName(wrap->text());
	wrap->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Cross canvas edges while painting, drawing shapes, or filling. The selection still clips edits. Tile Preview also allows editing repeated tiles."));
	form->addRow(wrap); connect(wrap, &QCheckBox::toggled, this, [this](bool enabled) { m_canvas->setWrappedPainting(enabled); });
	// Long translated items must not widen the inspector beyond its fields.
	const auto compact = [](QComboBox* field) { field->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); field->setMinimumContentsLength(10); };
	m_mirror = new QComboBox; m_mirror->setObjectName(QStringLiteral("textureMirror")); compact(m_mirror);
	m_mirror->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Mirror painting"));
	m_mirror->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "No mirror"), QStringLiteral("none"));
	m_mirror->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Mirror left and right"), QStringLiteral("horizontal"));
	m_mirror->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Mirror top and bottom"), QStringLiteral("vertical"));
	m_mirror->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Mirror both ways"), QStringLiteral("both"));
	m_mirror->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Repeat strokes and shapes across the canvas centre lines, shown as dashed guides. Mirrored copies blend once per pixel."));
	form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Mirror"), m_mirror);
	connect(m_mirror, &QComboBox::currentIndexChanged, this, [this]() {
		TextureBrush mirror; textureMirrorFromId(m_mirror->currentData().toString(), &mirror); m_canvas->setMirror(mirror.mirrorX, mirror.mirrorY);
	});
	auto* pressure = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Pen pressure sets brush width")); pressure->setObjectName(QStringLiteral("texturePressure"));
	pressure->setAccessibleName(pressure->text()); pressure->setChecked(true);
	pressure->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "With a graphics tablet, lighter pressure paints narrower strokes, down to one texel. The mouse always paints at full width."));
	form->addRow(pressure); connect(pressure, &QCheckBox::toggled, this, [this](bool enabled) { m_canvas->setPressureEnabled(enabled); });
	m_gradientShape = new QComboBox; m_gradientShape->setObjectName(QStringLiteral("textureGradientShape")); compact(m_gradientShape);
	m_gradientShape->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Gradient shape"));
	m_gradientShape->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Linear"), textureGradientShapeId(TextureGradientShape::Linear));
	m_gradientShape->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Radial"), textureGradientShapeId(TextureGradientShape::Radial));
	m_gradientShape->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Reflected"), textureGradientShapeId(TextureGradientShape::Reflected));
	m_gradientShape->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Drag from the paint color to the secondary color. A right-button drag runs the other way."));
	form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Gradient"), m_gradientShape);
	m_gradientDither = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Dither gradient")); m_gradientDither->setObjectName(QStringLiteral("textureGradientDither"));
	m_gradientDither->setAccessibleName(m_gradientDither->text());
	m_gradientDither->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Ordered dithering hides banding, especially after remapping to a game palette."));
	form->addRow(m_gradientDither);
	m_cloneAligned = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Aligned clone")); m_cloneAligned->setObjectName(QStringLiteral("textureCloneAligned"));
	m_cloneAligned->setAccessibleName(m_cloneAligned->text()); m_cloneAligned->setChecked(true);
	m_cloneAligned->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Keep the first stroke's offset from the source for later strokes. Off: every stroke starts copying at the source again."));
	form->addRow(m_cloneAligned); connect(m_cloneAligned, &QCheckBox::toggled, this, [this](bool aligned) { m_canvas->setCloneAligned(aligned); });
	auto* cloneSource = button(QCoreApplication::translate("VibeStudioTextureEditor", "Set Clone Source at Cursor"), QStringLiteral("setTextureCloneSource"), [this]() { m_canvas->setCloneSource(m_canvas->cursorPixel()); });
	cloneSource->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Copy from the pixel cursor. On the canvas, Ctrl+click or Ctrl+Space does the same."));
	const auto updatePaintControls = [this, brush, brushShape, paintMode, filledShapes, tolerance, wrap, pressure, cloneSource]() {
		const auto tool = static_cast<TextureCanvas::Tool>(m_tool->currentIndex());
		const bool stroke = tool == TextureCanvas::Tool::Pencil || tool == TextureCanvas::Tool::Brush || tool == TextureCanvas::Tool::Eraser || tool == TextureCanvas::Tool::Clone;
		const bool shape = tool == TextureCanvas::Tool::Line || tool == TextureCanvas::Tool::Rectangle || tool == TextureCanvas::Tool::Ellipse;
		brush->setEnabled(stroke || shape);
		brushShape->setEnabled(tool == TextureCanvas::Tool::Brush || tool == TextureCanvas::Tool::Eraser || tool == TextureCanvas::Tool::Line || tool == TextureCanvas::Tool::Clone);
		paintMode->setEnabled(tool == TextureCanvas::Tool::Brush || shape || tool == TextureCanvas::Tool::Gradient);
		filledShapes->setEnabled(tool == TextureCanvas::Tool::Rectangle || tool == TextureCanvas::Tool::Ellipse);
		tolerance->setEnabled(tool == TextureCanvas::Tool::Fill);
		wrap->setEnabled(tool != TextureCanvas::Tool::Pick && tool != TextureCanvas::Tool::Select && tool != TextureCanvas::Tool::Gradient);
		m_mirror->setEnabled(stroke || shape); pressure->setEnabled(stroke);
		m_gradientShape->setEnabled(tool == TextureCanvas::Tool::Gradient); m_gradientDither->setEnabled(tool == TextureCanvas::Tool::Gradient);
		m_cloneAligned->setEnabled(tool == TextureCanvas::Tool::Clone); cloneSource->setEnabled(tool == TextureCanvas::Tool::Clone);
		updateCanvasEnabled();
	};
	connect(m_tool, &QComboBox::currentIndexChanged, this, updatePaintControls); updatePaintControls();
	m_color = new QLineEdit(QStringLiteral("#ffffffff")); m_color->setObjectName(QStringLiteral("textureColor"));
	m_color->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Paint color"));
	m_color->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "A color name, #RRGGBB, or #AARRGGBB. Alpha comes first. Pencil and Fill replace RGBA; Brush and shapes use the Paint mode."));
	form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Color"), m_color);
	connect(m_color, &QLineEdit::textChanged, this, [this](const QString& text) {
		const QColor color(text); if (color.isValid()) { m_canvas->setColor(color); }
		updateCanvasEnabled();
		if (!color.isValid()) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Enter a valid color before painting.")); }
	});
	updateCanvasEnabled();
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Choose Color…"), QStringLiteral("chooseTextureColor"), [this]() {
		const auto color = QColorDialog::getColor(QColor(m_color->text()), this, QCoreApplication::translate("VibeStudioTextureEditor", "Paint color"), QColorDialog::ShowAlphaChannel);
		if (color.isValid()) { m_color->setText(color.name(QColor::HexArgb)); }
	});
	m_secondaryColor = new QLineEdit(QStringLiteral("#ff000000")); m_secondaryColor->setObjectName(QStringLiteral("textureSecondaryColor"));
	m_secondaryColor->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Secondary color"));
	m_secondaryColor->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Right-click painting, gradient end and Shift+Space use this color. Right-click with the Eyedropper picks it."));
	form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Secondary color"), m_secondaryColor);
	connect(m_secondaryColor, &QLineEdit::textChanged, this, [this](const QString& text) { const QColor color(text); if (color.isValid()) { m_canvas->setSecondaryColor(color); } });
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Swap Colors"), QStringLiteral("swapTextureColorsButton"), [this]() { swapColors(); });
	m_recentColors = new QWidget; m_recentColors->setObjectName(QStringLiteral("textureRecentColors"));
	m_recentColors->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Recent colors"));
	auto* recentLayout = new QHBoxLayout(m_recentColors); recentLayout->setContentsMargins(0, 0, 0, 0); recentLayout->setSpacing(2); recentLayout->addStretch(1);
	form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Recent colors"), m_recentColors);
	form = propertyTab(QCoreApplication::translate("VibeStudioTextureEditor", "Transform"));
	m_width = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Width"), QStringLiteral("textureWidth"), 1, TextureDocument::MaximumDimension); m_width->setValue(64);
	m_height = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Height"), QStringLiteral("textureHeight"), 1, TextureDocument::MaximumDimension); m_height->setValue(64);
	m_canvasAnchor = anchorChoice(QCoreApplication::translate("VibeStudioTextureEditor", "Canvas content anchor"), QStringLiteral("textureCanvasAnchor"));
	m_canvasAnchor->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Align existing pixels within the new canvas. Center snaps half-pixel offsets toward the top left."));
	m_canvasSizeHint = new QLabel; m_canvasSizeHint->setObjectName(QStringLiteral("textureCanvasSizeHint")); m_canvasSizeHint->setWordWrap(true);
	m_canvasSizeHint->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Canvas size preview")); form->addRow(m_canvasSizeHint);
	m_canvasSizeButton = button(QCoreApplication::translate("VibeStudioTextureEditor", "Set Canvas Size"), QStringLiteral("setTextureCanvasSize"), [this]() {
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("canvas-size")}, {QStringLiteral("width"), m_width->value()}, {QStringLiteral("height"), m_height->value()}, {QStringLiteral("anchor"), m_canvasAnchor->currentData().toString()}}});
	});
	m_canvasSizeButton->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Pad with transparency or crop every layer, including hidden and locked layers. Pixel size stays unchanged. Undo restores cropped pixels."));
	m_smooth = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Smooth resampling")); m_smooth->setAccessibleName(m_smooth->text()); form->addRow(m_smooth);
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Resample Canvas"), QStringLiteral("resizeTexture"), [this]() {
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("resize")}, {QStringLiteral("width"), m_width->value()}, {QStringLiteral("height"), m_height->value()}, {QStringLiteral("smooth"), m_smooth->isChecked()}}});
	});
	const QStringList labels{QCoreApplication::translate("VibeStudioTextureEditor", "Crop X"), QCoreApplication::translate("VibeStudioTextureEditor", "Crop Y"),
		QCoreApplication::translate("VibeStudioTextureEditor", "Crop width"), QCoreApplication::translate("VibeStudioTextureEditor", "Crop height")};
	for (int i = 0; i < 4; ++i) { m_crop[i] = spin(labels[i], QStringLiteral("textureCrop%1").arg(i), i < 2 ? 0 : 1, TextureDocument::MaximumDimension); }
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Crop Canvas"), QStringLiteral("cropTexture"), [this]() {
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("crop")}, {QStringLiteral("x"), m_crop[0]->value()}, {QStringLiteral("y"), m_crop[1]->value()}, {QStringLiteral("width"), m_crop[2]->value()}, {QStringLiteral("height"), m_crop[3]->value()}}});
	});
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Rotate Canvas Clockwise"), QStringLiteral("rotateTexture"), [this]() { applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("rotate")}}}); });
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Flip Horizontal"), QStringLiteral("flipTextureHorizontal"), [this]() { applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("flip")}, {QStringLiteral("axis"), QStringLiteral("horizontal")}}}); });
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Flip Vertical"), QStringLiteral("flipTextureVertical"), [this]() { applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("flip")}, {QStringLiteral("axis"), QStringLiteral("vertical")}}}); });
	auto* offsetX = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Offset X"), QStringLiteral("textureOffsetX"), -TextureDocument::MaximumDimension, TextureDocument::MaximumDimension);
	auto* offsetY = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Offset Y"), QStringLiteral("textureOffsetY"), -TextureDocument::MaximumDimension, TextureDocument::MaximumDimension);
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Offset and Wrap"), QStringLiteral("offsetTexture"), [this, offsetX, offsetY]() {
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("offset")}, {QStringLiteral("x"), offsetX->value()}, {QStringLiteral("y"), offsetY->value()}}});
	})->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Move the active layer's pixels, wrapping within the selection or full canvas."));
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Offset Half Size"), QStringLiteral("offsetTextureHalf"), [this]() {
		const auto size = m_document.selection().isEmpty() ? m_document.size() : m_document.selection().size();
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("offset")}, {QStringLiteral("x"), size.width() / 2}, {QStringLiteral("y"), size.height() / 2}}});
	})->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Move edge seams into the center for repair. Uses half the selection or canvas size, rounded down."));
	form = propertyTab(QCoreApplication::translate("VibeStudioTextureEditor", "Palette"));
	m_paletteChoice = new QComboBox;
	m_paletteChoice->setObjectName(QStringLiteral("textureEditorPalette"));
	m_paletteChoice->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Game palette"));
	for (const auto& descriptor : idTechPaletteDescriptors()) { m_paletteChoice->addItem(descriptor.displayName, descriptor.id); }
	form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Game palette"), m_paletteChoice);
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Refresh Palette Source"), QStringLiteral("refreshTexturePalette"), [this]() {
		refreshPaletteSource();
	})->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Reload this palette from the staged package or selected installation. Existing document pixels keep their colors until you remap or export."));
	connect(m_paletteChoice, &QComboBox::currentIndexChanged, this, [this]() {
		refreshPaletteSource(m_paletteChoice->currentData().toString());
	});
	m_paletteLabel = new QLabel; m_paletteLabel->setTextFormat(Qt::PlainText); m_paletteLabel->setWordWrap(true);
	m_paletteLabel->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Palette provenance")); form->addRow(m_paletteLabel);
	m_swatches = new PaletteSwatchView; m_swatches->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Paint palette")); form->addRow(m_swatches);
	connect(m_swatches, &PaletteSwatchView::indexSelected, this, [this](int index) { m_color->setText(QColor::fromRgba(m_palette.palette.colorAt(index)).name(QColor::HexArgb)); });
	m_dither = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Dither palette conversion")); m_dither->setAccessibleName(m_dither->text()); form->addRow(m_dither);
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Remap to Palette"), QStringLiteral("remapTexture"), [this]() {
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("palette")}, {QStringLiteral("dither"), m_dither->isChecked()}}});
	})->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Quantize the current pixels. Later painting can introduce colors outside the palette; remap again before saving if needed."));
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Load Palette File…"), QStringLiteral("loadTexturePalette"), [this]() {
		const auto path = QFileDialog::getOpenFileName(this, QCoreApplication::translate("VibeStudioTextureEditor", "Load Palette"), {},
			QCoreApplication::translate("VibeStudioTextureEditor", "Palettes (*.lmp *.pal *.pcx PLAYPAL*);;All files (*)"));
		if (!path.isEmpty()) { loadPaletteFromPath(path); }
	})->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Use a raw 768-byte palette, the first palette of a PLAYPAL, or a PCX file's palette. Pixels keep their colors until you remap."));
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Save Palette As…"), QStringLiteral("saveTexturePalette"), [this]() {
		const auto path = QFileDialog::getSaveFileName(this, QCoreApplication::translate("VibeStudioTextureEditor", "Save Palette"), QStringLiteral("palette.lmp"),
			QCoreApplication::translate("VibeStudioTextureEditor", "Raw palette (*.lmp *.pal)"));
		if (path.isEmpty()) { return; }
		QString error; setStatus(savePaletteToPath(path, &error) ? QCoreApplication::translate("VibeStudioTextureEditor", "Saved the palette: %1").arg(path) : error);
	})->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Write the 256 colors as raw RGB bytes, the layout of Quake's gfx/palette.lmp."));
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Analyze Colors"), QStringLiteral("analyzeTextureColors"), [this]() { analyzeColors(); })
		->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Count the visible composite's distinct colors and the pixels a palette export would change."));
	m_colorStats = new QLabel; m_colorStats->setObjectName(QStringLiteral("textureColorStats")); m_colorStats->setWordWrap(true); m_colorStats->setTextFormat(Qt::PlainText);
	m_colorStats->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Color analysis")); form->addRow(m_colorStats);
	form = propertyTab(QCoreApplication::translate("VibeStudioTextureEditor", "Package"));
	m_virtualPath = new QLineEdit(QStringLiteral("textures/custom/texture.png")); m_virtualPath->setObjectName(QStringLiteral("texturePackagePath"));
	m_virtualPath->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Package texture path")); form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Package path"), m_virtualPath);
	m_virtualPath->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Package-relative filename with the export extension, or a bare lump name for WAD. Quake miptexture names must match the export name."));
	m_replace = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Replace existing entry")); m_replace->setObjectName(QStringLiteral("replaceTextureEntry")); m_replace->setAccessibleName(m_replace->text()); form->addRow(m_replace);
	m_companionNaming = new QComboBox; m_companionNaming->setObjectName(QStringLiteral("textureCompanionNaming"));
	m_companionNaming->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); m_companionNaming->setMinimumContentsLength(10);
	m_companionNaming->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Companion map names"));
	m_companionNaming->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "DarkPlaces, FTE, QuakeSpasm (_norm, _gloss, _glow)"), QStringLiteral("darkplaces"));
	m_companionNaming->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "ioquake3 OpenGL2 (_n, _s, _glow)"), QStringLiteral("ioquake3"));
	form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Companion maps"), m_companionNaming);
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Stage Companion Maps"), QStringLiteral("stageTextureCompanions"), [this]() { stageCompanionMaps(); })
		->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Derive normal, gloss and glow maps from the visible composite and stage them as PNGs beside the package path, for source ports that read them."));
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Export Companion Maps…"), QStringLiteral("exportTextureCompanions"), [this]() {
		const auto path = QFileDialog::getSaveFileName(this, QCoreApplication::translate("VibeStudioTextureEditor", "Export Companion Maps"), QStringLiteral("texture.png"),
			QCoreApplication::translate("VibeStudioTextureEditor", "Texture name (*.png)"));
		if (path.isEmpty()) { return; }
		QString error;
		if (exportCompanionMaps(path, true, &error)) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Exported companion maps beside %1.").arg(path)); } else { setStatus(error); }
	})->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Write the companion maps as PNG files named after the chosen texture file."));
	form = propertyTab(QCoreApplication::translate("VibeStudioTextureEditor", "Layers"));
	m_layers = new QListWidget; m_layers->setObjectName(QStringLiteral("textureLayers"));
	m_layers->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Layers, top to bottom"));
	m_layers->setSelectionMode(QAbstractItemView::SingleSelection); m_layers->setIconSize({40, 40}); m_layers->setMinimumHeight(170);
	m_layers->setDragDropMode(QAbstractItemView::InternalMove); m_layers->setDefaultDropAction(Qt::MoveAction);
	m_layers->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Tick a layer to show it. Drag layers, or use Move Layer Up and Down, to reorder them."));
	form->addRow(m_layers);
	connect(m_layers, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
		if (m_busy || !item) { return; }
		const int index = item->data(Qt::UserRole).toInt();
		if (index < 0 || index >= m_document.layers().size()) { return; }
		const bool visible = item->checkState() == Qt::Checked;
		if (m_document.layers()[index].visible == visible) { return; }
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("layer-select")}, {QStringLiteral("index"), index}},
			QJsonObject{{QStringLiteral("op"), QStringLiteral("layer-properties")}, {QStringLiteral("visible"), visible}}});
	});
	connect(m_layers->model(), &QAbstractItemModel::rowsMoved, this, [this](const QModelIndex&, int first, int, const QModelIndex&, int row) {
		if (m_busy) { return; }
		// The list shows layers top to bottom; a drop at `row` lands above it.
		const int to = row > first ? row - 1 : row;
		auto* item = m_layers->item(to);
		if (!item) { refreshLayers(); return; }
		const int from = item->data(Qt::UserRole).toInt(), target = int(m_document.layers().size()) - 1 - to;
		if (from == target) { refreshLayers(); return; }
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("layer-select")}, {QStringLiteral("index"), from}},
			QJsonObject{{QStringLiteral("op"), QStringLiteral("layer-move")}, {QStringLiteral("index"), target}}});
	});
	connect(m_layers, &QListWidget::currentRowChanged, this, [this](int row) {
		if (row >= 0 && m_layers->item(row)) { selectLayer(m_layers->item(row)->data(Qt::UserRole).toInt()); }
	});
	m_layerName = new QLineEdit; m_layerName->setObjectName(QStringLiteral("textureLayerName")); m_layerName->setMaxLength(128);
	m_layerName->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Layer name"));
	form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Name"), m_layerName);
	m_layerVisible = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Visible")); m_layerVisible->setObjectName(QStringLiteral("textureLayerVisible"));
	m_layerVisible->setAccessibleName(m_layerVisible->text()); form->addRow(m_layerVisible);
	m_layerLocked = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Lock pixels")); m_layerLocked->setObjectName(QStringLiteral("textureLayerLocked"));
	m_layerLocked->setAccessibleName(m_layerLocked->text()); form->addRow(m_layerLocked);
	m_layerOpacity = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Opacity"), QStringLiteral("textureLayerOpacity"), 0, 100); m_layerOpacity->setSuffix(QStringLiteral("%"));
	m_layerBlend = new QComboBox; m_layerBlend->setObjectName(QStringLiteral("textureLayerBlend"));
	m_layerBlend->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Layer blend mode"));
	m_layerBlend->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Normal"), QStringLiteral("normal"));
	m_layerBlend->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Multiply"), QStringLiteral("multiply"));
	m_layerBlend->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Screen"), QStringLiteral("screen"));
	m_layerBlend->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Add"), QStringLiteral("add"));
	form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Blend"), m_layerBlend);
	connect(m_layerName, &QLineEdit::editingFinished, this, &TextureEditorDialog::updateLayerProperties);
	connect(m_layerVisible, &QCheckBox::toggled, this, &TextureEditorDialog::updateLayerProperties);
	connect(m_layerLocked, &QCheckBox::toggled, this, &TextureEditorDialog::updateLayerProperties);
	connect(m_layerOpacity, &QSpinBox::valueChanged, this, &TextureEditorDialog::updateLayerProperties);
	connect(m_layerBlend, &QComboBox::currentIndexChanged, this, &TextureEditorDialog::updateLayerProperties);
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Add Layer"), QStringLiteral("addTextureLayer"), [this]() {
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("layer-add")}, {QStringLiteral("name"), QCoreApplication::translate("VibeStudioTextureEditor", "Layer %1").arg(m_document.layers().size() + 1)}}});
	});
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Import Layer…"), QStringLiteral("importTextureLayer"), [this]() { importLayer(); });
	const auto layerCommand = [&](const QString& label, const QString& id, const QString& operation) {
		button(label, id, [this, operation]() { applyOperations({QJsonObject{{QStringLiteral("op"), operation}}}); });
	};
	layerCommand(QCoreApplication::translate("VibeStudioTextureEditor", "Duplicate Layer"), QStringLiteral("duplicateTextureLayer"), QStringLiteral("layer-duplicate"));
	layerCommand(QCoreApplication::translate("VibeStudioTextureEditor", "Remove Layer"), QStringLiteral("removeTextureLayer"), QStringLiteral("layer-remove"));
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Move Layer Up"), QStringLiteral("raiseTextureLayer"), [this]() { applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("layer-move")}, {QStringLiteral("index"), m_document.activeLayerIndex() + 1}}}); });
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Move Layer Down"), QStringLiteral("lowerTextureLayer"), [this]() { applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("layer-move")}, {QStringLiteral("index"), m_document.activeLayerIndex() - 1}}}); });
	layerCommand(QCoreApplication::translate("VibeStudioTextureEditor", "Merge Down"), QStringLiteral("mergeTextureLayer"), QStringLiteral("merge-down"));
	layerCommand(QCoreApplication::translate("VibeStudioTextureEditor", "Flatten"), QStringLiteral("flattenTextureLayers"), QStringLiteral("flatten"));
	form = propertyTab(QCoreApplication::translate("VibeStudioTextureEditor", "Selection"));
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Select All"), QStringLiteral("selectTextureAll"), [this]() { m_canvas->setSelection(QRect(QPoint(), m_document.size())); });
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Deselect"), QStringLiteral("deselectTexture"), [this]() { m_canvas->setSelection({}); });
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Copy Pixels"), QStringLiteral("copyTexturePixels"), [this]() {
		const auto pixels = m_document.copySelection();
		if (pixels.isNull()) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Unable to copy the selected pixels.")); return; }
		QApplication::clipboard()->setImage(pixels);
	});
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Cut Pixels"), QStringLiteral("cutTexturePixels"), [this]() {
		const auto* layer = m_document.activeLayer(); if (!layer || layer->locked || !layer->visible) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Select a visible, unlocked layer to cut pixels.")); return; }
		const auto pixels = m_document.copySelection();
		if (pixels.isNull()) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Unable to copy the selected pixels.")); return; }
		QApplication::clipboard()->setImage(pixels); applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("clear")}}});
	});
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Paste at Cursor"), QStringLiteral("pasteTexturePixels"), [this]() {
		const QImage pixels = QApplication::clipboard()->image(); const QPoint position = m_canvas->cursorPixel();
		runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Pasting pixels…"), [pixels, position](TextureDocument& document, QString* error, const TextureProgress&) { return document.pastePixels(pixels, position, error); });
	});
	layerCommand(QCoreApplication::translate("VibeStudioTextureEditor", "Clear Pixels"), QStringLiteral("clearTexturePixels"), QStringLiteral("clear"));
	auto* moveX = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Move X"), QStringLiteral("textureMoveX"), -4096, 4096);
	auto* moveY = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Move Y"), QStringLiteral("textureMoveY"), -4096, 4096);
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Move Selected Pixels"), QStringLiteral("moveTexturePixels"), [this, moveX, moveY]() { applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("move-pixels")}, {QStringLiteral("x"), moveX->value()}, {QStringLiteral("y"), moveY->value()}}}); });
	m_selectionHint = new QLabel; m_selectionHint->setObjectName(QStringLiteral("textureSelectionHint")); m_selectionHint->setWordWrap(true);
	m_selectionHint->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Selection transform preview")); form->addRow(m_selectionHint);
	m_selectionWidth = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Selection width"), QStringLiteral("textureSelectionWidth"), 1, TextureDocument::MaximumDimension);
	m_selectionHeight = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Selection height"), QStringLiteral("textureSelectionHeight"), 1, TextureDocument::MaximumDimension);
	m_selectionAnchor = anchorChoice(QCoreApplication::translate("VibeStudioTextureEditor", "Selection anchor"), QStringLiteral("textureSelectionAnchor"));
	m_selectionAnchor->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Keep this edge or corner fixed when resizing or rotating selected pixels. Center snaps half-pixel offsets toward the top left."));
	m_selectionSmooth = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Smooth selection resampling")); m_selectionSmooth->setAccessibleName(m_selectionSmooth->text()); form->addRow(m_selectionSmooth);
	m_resizeSelection = button(QCoreApplication::translate("VibeStudioTextureEditor", "Resize Selected Pixels"), QStringLiteral("resizeTextureSelection"), [this]() {
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("resize-selection")}, {QStringLiteral("width"), m_selectionWidth->value()}, {QStringLiteral("height"), m_selectionHeight->value()},
			{QStringLiteral("smooth"), m_selectionSmooth->isChecked()}, {QStringLiteral("anchor"), m_selectionAnchor->currentData().toString()}}});
	});
	m_rotateSelection = button(QCoreApplication::translate("VibeStudioTextureEditor", "Rotate Selected Pixels Clockwise"), QStringLiteral("rotateTextureSelection"), [this]() {
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("rotate-selection")}, {QStringLiteral("anchor"), m_selectionAnchor->currentData().toString()}}});
	});
	const auto transformTip = QCoreApplication::translate("VibeStudioTextureEditor", "Transform only the active layer. Vacated pixels become transparent; transformed pixels replace their destination. The result must fit within the canvas.");
	m_resizeSelection->setToolTip(transformTip); m_rotateSelection->setToolTip(transformTip);
	for (auto* field : {m_width, m_height, m_selectionWidth, m_selectionHeight}) { connect(field, &QSpinBox::valueChanged, this, &TextureEditorDialog::refreshTransforms); }
	for (auto* field : {m_canvasAnchor, m_selectionAnchor}) { connect(field, &QComboBox::currentIndexChanged, this, &TextureEditorDialog::refreshTransforms); }
	form = propertyTab(QCoreApplication::translate("VibeStudioTextureEditor", "Recovery"));
	m_recoveryEnabled = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Automatic recovery"));
	m_recoveryEnabled->setObjectName(QStringLiteral("textureRecoveryEnabled")); m_recoveryEnabled->setAccessibleName(m_recoveryEnabled->text());
	m_recoveryEnabled->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Keep unsaved layers and palette settings in a separate local checkpoint. Turning this off retains existing checkpoints."));
	m_recoveryEnabled->setChecked(StudioSettings().textureRecoveryEnabled()); form->addRow(m_recoveryEnabled);
	auto* interval = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Interval (seconds)"), QStringLiteral("textureRecoveryInterval"), 5, 600);
	interval->setValue(StudioSettings().textureRecoveryIntervalSeconds());
	const QString profile = StudioSettings::overrideFilePath();
	m_recoveryDirectory = QDir(profile.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) : QFileInfo(profile).absolutePath()).filePath(QStringLiteral("texture-recovery"));
	m_recoveryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	m_recovery = new TextureRecoveryWriter(m_recoveryDirectory, this);
	auto* recoveryDirectory = new QLineEdit(QDir::toNativeSeparators(m_recoveryDirectory)); recoveryDirectory->setReadOnly(true); recoveryDirectory->setToolTip(recoveryDirectory->text());
	recoveryDirectory->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Recovery folder")); form->addRow(recoveryDirectory);
	auto* checkpointNow = button(QCoreApplication::translate("VibeStudioTextureEditor", "Checkpoint Now"), QStringLiteral("checkpointTexture"), [this]() {
		if (!hasUnsavedChanges()) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "No unsaved changes to checkpoint.")); return; }
		checkpointRecovery();
	});
	checkpointNow->setEnabled(m_recoveryEnabled->isChecked()); connect(m_recoveryEnabled, &QCheckBox::toggled, checkpointNow, &QPushButton::setEnabled);
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Refresh Recovery List"), QStringLiteral("refreshTextureRecoveries"), [this]() { refreshRecoveries(); });
	m_recoveryList = new QListWidget; m_recoveryList->setObjectName(QStringLiteral("textureRecoveryList"));
	m_recoveryList->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Available texture recovery checkpoints"));
	m_recoveryList->setMinimumHeight(180); m_recoveryList->setWordWrap(true); m_recoveryList->setResizeMode(QListView::Adjust); form->addRow(m_recoveryList);
	auto* restore = button(QCoreApplication::translate("VibeStudioTextureEditor", "Restore Selected as Draft"), QStringLiteral("restoreTextureRecovery"), [this]() {
		const auto* item = m_recoveryList->currentItem();
		if (item && item->data(Qt::UserRole + 2).toBool()) { restoreRecovery(item->data(Qt::UserRole).toString(), item->data(Qt::UserRole + 1).toByteArray()); }
	});
	restore->setEnabled(false);
	connect(m_recoveryList, &QListWidget::currentRowChanged, this, [this, restore]() {
		const auto* item = m_recoveryList->currentItem(); restore->setEnabled(item && item->data(Qt::UserRole + 2).toBool());
	});
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Open Recovery File…"), QStringLiteral("openTextureRecovery"), [this]() {
		const auto path = QFileDialog::getOpenFileName(this, QCoreApplication::translate("VibeStudioTextureEditor", "Restore Texture Recovery"), m_recoveryDirectory,
			QCoreApplication::translate("VibeStudioTextureEditor", "Texture recovery (*.vtrecovery)"));
		if (!path.isEmpty()) { restoreRecovery(path); }
	});
	m_recoveryStatus = new QLabel; m_recoveryStatus->setObjectName(QStringLiteral("textureRecoveryStatus")); m_recoveryStatus->setTextFormat(Qt::PlainText); m_recoveryStatus->setWordWrap(true);
	m_recoveryStatus->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Texture recovery status")); canvasLayout->addWidget(m_recoveryStatus);
	m_recoveryTimer = new QTimer(this); m_recoveryTimer->setInterval(interval->value() * 1000);
	connect(m_recoveryTimer, &QTimer::timeout, this, &TextureEditorDialog::checkpointRecovery);
	connect(interval, &QSpinBox::valueChanged, this, [this](int seconds) {
		StudioSettings settings; settings.setTextureRecoveryIntervalSeconds(seconds); settings.sync(); m_recoveryTimer->setInterval(seconds * 1000);
	});
	connect(m_recoveryEnabled, &QCheckBox::toggled, this, [this](bool enabled) {
		StudioSettings settings; settings.setTextureRecoveryEnabled(enabled); settings.sync();
		if (enabled) { m_recoveryTimer->start(); checkpointRecovery(); }
		else { m_recoveryTimer->stop(); m_recoveryStatus->setText(QCoreApplication::translate("VibeStudioTextureEditor", "Automatic recovery is off. Existing checkpoints are retained.")); }
	});
	m_recoveryProgressTimer = new QTimer(this); m_recoveryProgressTimer->setInterval(200);
	connect(m_recoveryProgressTimer, &QTimer::timeout, this, [this]() {
		if (!m_recovery->busy()) { m_recoveryProgressTimer->stop(); return; }
		m_recoveryStatus->setText(QCoreApplication::translate("VibeStudioTextureEditor", "Updating local checkpoint: %1%").arg(m_recovery->progress() / 10));
	});
	m_recovery->finished = [this](const QString& id, const QString& path, const QString& error) {
		if (!error.isEmpty()) { m_recoveryStatus->setText(QCoreApplication::translate("VibeStudioTextureEditor", "Recovery failed: %1").arg(error)); }
		else if (id == m_recoveryId) { m_recoveryStatus->setText(QCoreApplication::translate("VibeStudioTextureEditor", "Local checkpoint updated at %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate))); m_recoveryStatus->setToolTip(path); }
	};
	if (m_recoveryEnabled->isChecked()) { m_recoveryTimer->start(); }
	m_recoveryStatus->setText(m_recoveryEnabled->isChecked() ? QCoreApplication::translate("VibeStudioTextureEditor", "Local recovery enabled.") : QCoreApplication::translate("VibeStudioTextureEditor", "Automatic recovery is off. Existing checkpoints are retained."));
	m_export = new TextureExportPanel;
	auto* exportScroll = new QScrollArea; exportScroll->setWidgetResizable(true); exportScroll->setWidget(m_export);
	exportScroll->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Export"));
	properties->addTab(exportScroll, exportScroll->accessibleName());
	m_export->changed = [this]() { refresh(); };
	m_export->previewRequested = [this]() { previewExport(); };
	m_export->exportRequested = [this]() { exportFile(); };
	form = propertyTab(QCoreApplication::translate("VibeStudioTextureEditor", "Adjust"));
	auto* brightness = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Brightness"), QStringLiteral("textureBrightness"), -255, 255);
	auto* contrast = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Contrast"), QStringLiteral("textureContrast"), -100, 100); contrast->setSuffix(QStringLiteral("%"));
	auto* hue = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Hue"), QStringLiteral("textureHue"), -180, 180); hue->setSuffix(QStringLiteral("°"));
	auto* saturation = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Saturation"), QStringLiteral("textureSaturation"), -100, 100); saturation->setSuffix(QStringLiteral("%"));
	auto* lightness = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Lightness"), QStringLiteral("textureLightness"), -100, 100); lightness->setSuffix(QStringLiteral("%"));
	auto* gamma = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Gamma"), QStringLiteral("textureGamma"), 10, 1000); gamma->setSuffix(QStringLiteral("%")); gamma->setValue(100);
	gamma->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "100% leaves tones unchanged; higher values brighten shadows and midtones."));
	auto* posterize = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Posterize levels"), QStringLiteral("texturePosterize"), 0, 64);
	posterize->setSpecialValueText(QCoreApplication::translate("VibeStudioTextureEditor", "Off"));
	auto* invert = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Invert colors")); invert->setObjectName(QStringLiteral("textureInvert")); invert->setAccessibleName(invert->text()); form->addRow(invert);
	auto* grayscale = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Grayscale")); grayscale->setObjectName(QStringLiteral("textureGrayscale")); grayscale->setAccessibleName(grayscale->text()); form->addRow(grayscale);
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Apply Adjustment"), QStringLiteral("applyTextureAdjustment"), [this, brightness, contrast, hue, saturation, lightness, gamma, posterize, invert, grayscale]() {
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("adjust")}, {QStringLiteral("brightness"), brightness->value()}, {QStringLiteral("contrast"), contrast->value()},
			{QStringLiteral("hue"), hue->value()}, {QStringLiteral("saturation"), saturation->value()}, {QStringLiteral("lightness"), lightness->value()},
			{QStringLiteral("gamma"), gamma->value()}, {QStringLiteral("posterize"), posterize->value() == 1 ? 2 : posterize->value()},
			{QStringLiteral("invert"), invert->isChecked()}, {QStringLiteral("grayscale"), grayscale->isChecked()}}});
	})->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Adjust the active layer within the selection: invert, brightness, contrast, hue, saturation, lightness, grayscale, posterize, then gamma. Transparent pixels are unchanged."));
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Reset Adjustment"), QStringLiteral("resetTextureAdjustment"), [brightness, contrast, hue, saturation, lightness, gamma, posterize, invert, grayscale]() {
		for (auto* field : {brightness, contrast, hue, saturation, lightness, posterize}) { field->setValue(0); }
		gamma->setValue(100); invert->setChecked(false); grayscale->setChecked(false);
	});
	auto* filterKind = new QComboBox; filterKind->setObjectName(QStringLiteral("textureFilterKind")); filterKind->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Filter"));
	filterKind->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); filterKind->setMinimumContentsLength(10);
	filterKind->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Blur"), textureFilterKindId(TextureFilterKind::Blur));
	filterKind->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Sharpen"), textureFilterKindId(TextureFilterKind::Sharpen));
	filterKind->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Add noise"), textureFilterKindId(TextureFilterKind::Noise));
	filterKind->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Emboss"), textureFilterKindId(TextureFilterKind::Emboss));
	filterKind->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Find edges"), textureFilterKindId(TextureFilterKind::Edges));
	form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Filter"), filterKind);
	auto* radius = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Filter radius"), QStringLiteral("textureFilterRadius"), 1, 16);
	auto* amount = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Filter amount"), QStringLiteral("textureFilterAmount"), 1, 255); amount->setValue(50);
	amount->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Sharpen, emboss and edge strength in percent, or the noise range in color levels."));
	auto* seed = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Noise seed"), QStringLiteral("textureFilterSeed"), 0, 100000); seed->setValue(1);
	auto* monochrome = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Monochrome noise")); monochrome->setObjectName(QStringLiteral("textureFilterMonochrome"));
	monochrome->setAccessibleName(monochrome->text()); monochrome->setChecked(true); form->addRow(monochrome);
	auto* filterWrap = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Filter across edges")); filterWrap->setObjectName(QStringLiteral("textureFilterWrap"));
	filterWrap->setAccessibleName(filterWrap->text()); filterWrap->setChecked(true); form->addRow(filterWrap);
	filterWrap->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Sample the opposite edge of the selection or canvas, so filtering a tiling texture adds no seam."));
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Apply Filter"), QStringLiteral("applyTextureFilter"), [this, filterKind, radius, amount, seed, monochrome, filterWrap]() {
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("filter")}, {QStringLiteral("kind"), filterKind->currentData().toString()}, {QStringLiteral("radius"), radius->value()},
			{QStringLiteral("amount"), amount->value()}, {QStringLiteral("seed"), seed->value()}, {QStringLiteral("monochrome"), monochrome->isChecked()}, {QStringLiteral("wrap"), filterWrap->isChecked()}}});
	});
	const auto updateFilterFields = [filterKind, radius, seed, monochrome]() {
		const auto kind = filterKind->currentData().toString();
		radius->setEnabled(kind == QStringLiteral("blur") || kind == QStringLiteral("sharpen"));
		seed->setEnabled(kind == QStringLiteral("noise")); monochrome->setEnabled(kind == QStringLiteral("noise"));
	};
	connect(filterKind, &QComboBox::currentIndexChanged, this, updateFilterFields); updateFilterFields();
	auto* seamWidth = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Seam blend width"), QStringLiteral("textureSeamlessWidth"), 1, TextureDocument::MaximumDimension / 2); seamWidth->setValue(8);
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Make Seamless"), QStringLiteral("makeTextureSeamless"), [this, seamWidth]() {
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("seamless")}, {QStringLiteral("width"), seamWidth->value()}}});
	})->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Cross-fade each edge band with the opposite edge so the selection or canvas tiles without a hard seam. Check the result with Tile Preview."));
	auto* replaceFrom = new QLineEdit(QStringLiteral("#ff000000")); replaceFrom->setObjectName(QStringLiteral("textureReplaceFrom"));
	replaceFrom->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Color to replace"));
	replaceFrom->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "A color name, #RRGGBB, or #AARRGGBB. Every matching pixel in the selection changes, connected or not."));
	form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Color to replace"), replaceFrom);
	auto* replaceTolerance = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Replace tolerance"), QStringLiteral("textureReplaceTolerance"), 0, 255);
	auto* keepAlpha = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Keep each pixel's alpha")); keepAlpha->setObjectName(QStringLiteral("textureReplaceKeepAlpha"));
	keepAlpha->setAccessibleName(keepAlpha->text()); keepAlpha->setChecked(true); form->addRow(keepAlpha);
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Replace with Paint Color"), QStringLiteral("replaceTextureColor"), [this, replaceFrom, replaceTolerance, keepAlpha]() {
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("replace-color")}, {QStringLiteral("from"), replaceFrom->text()}, {QStringLiteral("to"), m_color->text()},
			{QStringLiteral("tolerance"), replaceTolerance->value()}, {QStringLiteral("keepAlpha"), keepAlpha->isChecked()}}});
	});
	form = propertyTab(QCoreApplication::translate("VibeStudioTextureEditor", "Frames"));
	auto* framesHelp = new QLabel(QCoreApplication::translate("VibeStudioTextureEditor", "Visible layers are the frames of the sprite export profiles, bottom to top. Edit Selected on a sprite opens every frame as a layer."));
	framesHelp->setWordWrap(true); framesHelp->setObjectName(QStringLiteral("textureFramesHelp")); framesHelp->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Frames help")); form->addRow(framesHelp);
	m_framePreview = new QLabel; m_framePreview->setObjectName(QStringLiteral("textureFramePreview")); m_framePreview->setAlignment(Qt::AlignCenter);
	m_framePreview->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Frame preview")); m_framePreview->setMinimumWidth(1);
	m_framePreview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); m_framePreview->setFixedHeight(fontMetrics().height() * 9); form->addRow(m_framePreview);
	m_frameIndex = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Frame"), QStringLiteral("textureFrameIndex"), 1, 1);
	connect(m_frameIndex, &QSpinBox::valueChanged, this, [this](int frame) { showFrame(frame - 1); });
	m_frameRate = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Frames per second"), QStringLiteral("textureFrameRate"), 1, 60); m_frameRate->setValue(10);
	m_frameRate->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Preview speed only. Quake and Quake II animate sprite frames at 10 per second; Half-Life sprites use the entity's framerate."));
	m_framePlay = button(QCoreApplication::translate("VibeStudioTextureEditor", "Play Frames"), QStringLiteral("playTextureFrames"), [this]() { setFramePlaying(!framePlaying()); });
	m_onionSkin = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Onion skin: show the layer below")); m_onionSkin->setObjectName(QStringLiteral("textureOnionSkin"));
	m_onionSkin->setAccessibleName(m_onionSkin->text()); form->addRow(m_onionSkin);
	m_onionSkin->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Draw the next visible layer below the active one translucently over the canvas, to line up animation frames. It is not exported."));
	connect(m_onionSkin, &QCheckBox::toggled, this, [this]() { refreshOnionSkin(); });
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Import Frames…"), QStringLiteral("importTextureFrames"), [this]() {
		const auto paths = QFileDialog::getOpenFileNames(this, QCoreApplication::translate("VibeStudioTextureEditor", "Import Frames"), {}, assetImageOpenFilter(false));
		if (!paths.isEmpty()) { importFramesFromPaths(paths); }
	})->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Add each image as a layer above the active one, in the order chosen. Smaller images are centred; larger ones are refused."));
	m_frameStatus = new QLabel; m_frameStatus->setObjectName(QStringLiteral("textureFrameStatus")); m_frameStatus->setWordWrap(true); m_frameStatus->setTextFormat(Qt::PlainText);
	m_frameStatus->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Frame status")); form->addRow(m_frameStatus);
	m_frameTimer = new QTimer(this);
	connect(m_frameTimer, &QTimer::timeout, this, [this]() { if (!m_frames.isEmpty()) { m_frameIndex->setValue(m_frameIndex->value() % int(m_frames.size()) + 1); } });
	connect(m_frameRate, &QSpinBox::valueChanged, this, [this](int rate) { m_frameTimer->setInterval(1000 / rate); });
	m_frameTimer->setInterval(1000 / m_frameRate->value());
	connect(properties, &QTabWidget::currentChanged, this, [this]() { refreshFrames(); });
	properties->setMinimumWidth(std::max(300, fontMetrics().horizontalAdvance(m_dither->text()) + 80));
	for (auto* toolbar : {m_toolStrip}) { for (auto* toolButton : toolbar->findChildren<QToolButton*>()) { toolButton->setFocusPolicy(Qt::StrongFocus); } }
	auto* propertyPanel = new QWidget; auto* propertyLayout = new QVBoxLayout(propertyPanel); propertyLayout->setContentsMargins(0, 0, 0, 0);
	auto* section = new QComboBox; section->setObjectName(QStringLiteral("texturePropertySection"));
	section->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Inspector section"));
	for (int i = 0; i < properties->count(); ++i) { section->addItem(properties->tabText(i)); }
	connect(section, &QComboBox::currentIndexChanged, properties, &QTabWidget::setCurrentIndex);
	connect(properties, &QTabWidget::currentChanged, section, &QComboBox::setCurrentIndex);
	connect(section, &QComboBox::currentIndexChanged, this, [this](int index) { if (index == 6 && !m_recoveryScanned && !m_busy) { refreshRecoveries(); } });
	properties->tabBar()->hide(); propertyLayout->addWidget(section); propertyLayout->addWidget(properties, 1); m_properties = propertyPanel;
	splitter->addWidget(propertyPanel); splitter->setStretchFactor(0, 1); splitter->setSizes({800, properties->minimumWidth()}); layout->addWidget(splitter, 1);
	m_progress = new QProgressBar; m_progress->setObjectName(QStringLiteral("textureOperationProgress")); m_progress->setRange(0, 0); m_progress->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Texture operation progress")); m_progress->hide(); layout->addWidget(m_progress);
	m_progressTimer = new QTimer(this); m_progressTimer->setInterval(50);
	connect(m_progressTimer, &QTimer::timeout, this, [this]() {
		if (!m_task) { return; } const int completed = m_task->progress.load();
		if (completed >= 0) { m_progress->setRange(0, 1000); m_progress->setValue(completed); }
	});
	m_cancel = new QPushButton(QCoreApplication::translate("VibeStudioTextureEditor", "Cancel Operation")); m_cancel->setObjectName(QStringLiteral("cancelTextureOperation")); m_cancel->setAccessibleName(m_cancel->text()); m_cancel->setAutoDefault(false); m_cancel->hide();
	connect(m_cancel, &QPushButton::clicked, this, &TextureEditorDialog::cancelPending); layout->addWidget(m_cancel);
	m_status = new QLabel; m_status->setObjectName(QStringLiteral("textureEditorStatus")); m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true);
	m_status->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Texture operation status")); layout->addWidget(m_status);
	m_canvas->changed = [this]() {
		const auto tool = static_cast<TextureCanvas::Tool>(m_tool->currentIndex());
		if (tool == TextureCanvas::Tool::Pencil || tool == TextureCanvas::Tool::Brush) { rememberColor(m_canvas->lastPaintColor()); }
		refresh();
	};
	m_canvas->statusChanged = [this](const QString& text) { m_pixelLabel->setText(text); };
	m_canvas->operationFailed = [this](const QString& text) { setStatus(text); };
	m_canvas->colorPicked = [this](QColor color) { m_color->setText(color.name(QColor::HexArgb)); };
	m_canvas->secondaryColorPicked = [this](QColor color) { m_secondaryColor->setText(color.name(QColor::HexArgb)); };
	m_canvas->fillRequested = [this, tolerance, wrap](QPoint point, QColor color) {
		rememberColor(color);
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("fill")}, {QStringLiteral("x"), point.x()}, {QStringLiteral("y"), point.y()},
			{QStringLiteral("color"), color.name(QColor::HexArgb)}, {QStringLiteral("tolerance"), tolerance->value()}, {QStringLiteral("wrap"), wrap->isChecked()}}});
	};
	m_canvas->shapeRequested = [this](TextureShape shape, QPoint from, QPoint to, const TextureBrush& brush, bool filled, QColor color) {
		rememberColor(color);
		const QString operation = shape == TextureShape::Line ? QStringLiteral("line") : (shape == TextureShape::Rectangle ? QStringLiteral("rectangle") : QStringLiteral("ellipse"));
		applyOperations({QJsonObject{{QStringLiteral("op"), operation}, {QStringLiteral("x"), from.x()}, {QStringLiteral("y"), from.y()},
			{QStringLiteral("x2"), to.x()}, {QStringLiteral("y2"), to.y()}, {QStringLiteral("color"), color.name(QColor::HexArgb)}, {QStringLiteral("width"), brush.width},
			{QStringLiteral("brush"), textureBrushShapeId(brush.shape)}, {QStringLiteral("mode"), texturePaintModeId(brush.mode)},
			{QStringLiteral("wrap"), brush.wrap}, {QStringLiteral("filled"), filled}, {QStringLiteral("mirror"), textureMirrorId(brush)}}});
	};
	m_canvas->gradientRequested = [this, paintMode](QPoint from, QPoint to, QColor first, QColor second) {
		rememberColor(first); rememberColor(second);
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("gradient")}, {QStringLiteral("x"), from.x()}, {QStringLiteral("y"), from.y()},
			{QStringLiteral("x2"), to.x()}, {QStringLiteral("y2"), to.y()}, {QStringLiteral("color"), first.name(QColor::HexArgb)}, {QStringLiteral("color2"), second.name(QColor::HexArgb)},
			{QStringLiteral("shape"), m_gradientShape->currentData().toString()}, {QStringLiteral("mode"), paintMode->currentData().toString()}, {QStringLiteral("dither"), m_gradientDither->isChecked()}}});
	};
	m_canvas->selectionChanged = [this](QRect rect) { m_crop[0]->setValue(rect.x()); m_crop[1]->setValue(rect.y()); m_crop[2]->setValue(rect.width()); m_crop[3]->setValue(rect.height()); refreshTransforms(); };
	connect(m_virtualPath, &QLineEdit::textChanged, this, [this]() { refresh(); });
	setPaletteResolution(m_palette); m_savedMetadata = projectMetadata(); refresh();
	for (auto* toolbar : {m_tools, view, m_toolStrip}) {
		for (auto* button : toolbar->findChildren<QToolButton*>()) {
			button->setFocusPolicy(Qt::StrongFocus);
			if (!button->defaultAction()) {
				const auto name = QCoreApplication::translate("VibeStudioTextureEditor", "More %1").arg(toolbar->accessibleName());
				button->setAccessibleName(name); button->setToolTip(name);
			}
		}
	}
	updateInspectorWidth();
	setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Ready. New creates a transparent texture using the width and height fields."));
}

TextureEditorDialog::~TextureEditorDialog()
{
	// Workers own document snapshots only, never widgets. Shutdown waits before
	// destroying the thread object; ordinary Close is blocked while busy.
	if (m_task) { m_task->cancelled.store(true); }
	if (m_worker) { m_worker->disconnect(this); m_worker->wait(); delete m_worker; }
	if (!m_closeApproved) { m_busy = false; m_canvas->finishStroke(); checkpointRecovery(); }
	delete m_recovery; m_recovery = nullptr;
}

void TextureEditorDialog::setStatus(const QString& text) { m_status->setText(text); }
void TextureEditorDialog::updateCanvasEnabled()
{
	const auto tool = static_cast<TextureCanvas::Tool>(m_tool->currentIndex());
	m_canvas->setEnabled(!m_busy && ((m_color && QColor(m_color->text()).isValid()) || tool == TextureCanvas::Tool::Eraser || tool == TextureCanvas::Tool::Pick ||
		tool == TextureCanvas::Tool::Select || tool == TextureCanvas::Tool::Clone));
}
void TextureEditorDialog::setPaletteResolution(const IdTechPaletteResolution& resolution, bool markChanged)
{
	m_palette = resolution; m_swatches->setPalette(resolution.palette);
	m_export->invalidatePreview();
	const QSignalBlocker blocker(m_paletteChoice);
	m_paletteChoice->setCurrentIndex(m_paletteChoice->findData(resolution.palette.id));
	m_paletteLabel->setText(idTechPaletteSummaryLines(resolution).join(QLatin1Char('\n')));
	if (!markChanged) { m_savedMetadata = projectMetadata(); }
	refresh();
}

void TextureEditorDialog::refreshPaletteSource(const QString& paletteId, bool markChanged)
{
	if (m_busy) { return; }
	const auto source = paletteSource ? paletteSource() : TexturePreviewSource{};
	QString id = paletteId.isEmpty() ? m_paletteChoice->currentData().toString() : paletteId;
	if (id.isEmpty()) { id = m_palette.requestedPaletteId; }
	if (id.isEmpty()) { id = source.paletteId; }
	auto resolved = std::make_shared<IdTechPaletteResolution>();
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Loading palette source…"),
		[source, id, resolved](TextureDocument&, QString* error, const TextureProgress& progress) {
			*resolved = resolveTexturePreviewPalette(source, id, [&progress] { return progress && !progress(0, 0); });
			if (!resolved->palette.isValid()) {
				if (error) { *error = QCoreApplication::translate("VibeStudioTextureEditor", "The palette could not be loaded."); }
				return false;
			}
			return !progress || progress(1, 1);
		}, [this, source, resolved, markChanged] {
			if (paletteSource && paletteSource().revision != source.revision) {
				setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Palette sources changed while loading. Refresh the palette source again."));
				return;
			}
			setPaletteResolution(*resolved, markChanged);
			setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Palette source loaded. Existing pixels keep their colors until you remap or export."));
		});
}

bool TextureEditorDialog::setImage(const QImage& image, const QString& source, QString* error)
{
	if (m_busy) { return false; }
	if (!m_document.reset(image, error)) { return false; }
	retireRecovery();
	m_source = source; m_savedPath.clear(); m_projectIdentity = {}; m_extraMetadata = {}; m_replace->setChecked(false);
	m_export->setOptions({});
	m_recoveredDraft = false;
	const QString base = source.contains(QLatin1Char('/')) ? source.left(source.lastIndexOf(QLatin1Char('/')) + 1) : QStringLiteral("textures/custom/");
	m_virtualPath->setText(base + QFileInfo(source).completeBaseName() + QStringLiteral(".png"));
	m_savedMetadata = projectMetadata(); refresh(); m_canvas->fit();
	setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Editing one decoded image. Export settings control game output; Save Project retains editable layers."));
	return true;
}

bool TextureEditorDialog::editImage(const QImage& image, const QString& source, const IdTechPaletteResolution& palette)
{
	if (!confirmDiscard([this, image, source, palette]() { editImage(image, source, palette); })) { return false; }
	QString error;
	if (!setImage(image, source, &error)) { setStatus(error); return false; }
	setPaletteResolution(palette);
	m_savedMetadata = projectMetadata(); refresh();
	return true;
}

bool TextureEditorDialog::editDecodedImage(const IdTechImageDecodeResult& decoded, const QString& source, const IdTechPaletteResolution& palette, int displayedFrame)
{
	if (!decoded.decoded) { setStatus(decoded.error); return false; }
	if (!confirmDiscard([this, decoded, source, palette, displayedFrame]() { editDecodedImage(decoded, source, palette, displayedFrame); })) { return false; }
	QString error;
	if (!setImage(decoded.image, source, &error)) { setStatus(error); return false; }
	setPaletteResolution(palette); auto options = textureExportOptionsForImage(decoded);
	QVector<QPair<QImage, QPoint>> frames;
	for (const auto& frame : decoded.frames) {
		if (frame.image.isNull()) { continue; }
		// Each frame's top-left corner relative to the sprite origin, y down.
		const QPoint corner = options.format == TextureExportFormat::Quake2Sprite ? QPoint(-frame.originX, frame.originY - frame.image.height()) : QPoint(frame.originX, -frame.originY);
		frames.push_back({frame.image, corner});
	}
	if (frames.size() > 1 && (options.format == TextureExportFormat::QuakeSprite || options.format == TextureExportFormat::HalfLifeSprite || options.format == TextureExportFormat::Quake2Sprite)) {
		// Sprite frames become layers aligned on their shared origin.
		QRect bounds;
		for (const auto& frame : frames) { bounds |= QRect(frame.second, frame.first.size()); }
		QVector<TextureLayer> layers; const int kept = std::min<int>(int(frames.size()), TextureDocument::MaximumLayers);
		for (int index = 0; index < kept; ++index) {
			TextureLayer layer; layer.id = index + 1; layer.name = QCoreApplication::translate("VibeStudioTextureEditor", "Frame %1").arg(index + 1);
			layer.pixels = QImage(bounds.size(), QImage::Format_ARGB32); layer.pixels.fill(Qt::transparent);
			QPainter painter(&layer.pixels); painter.setCompositionMode(QPainter::CompositionMode_Source);
			painter.drawImage(frames[index].second - bounds.topLeft(), frames[index].first.convertToFormat(QImage::Format_ARGB32)); painter.end();
			layers << layer;
		}
		const int active = std::clamp(displayedFrame, 0, kept - 1);
		// Restoring into the open document keeps its revision counter monotonic.
		if (m_document.restoreLayers(bounds.size(), layers, active, &error)) {
			options.centeredOrigin = false; options.leftOffset = -bounds.left(); options.topOffset = -bounds.top();
			setStatus(frames.size() > kept ? QCoreApplication::translate("VibeStudioTextureEditor", "Opened the first %1 of %2 sprite frames as layers. A document holds at most 32 layers.").arg(kept).arg(frames.size()) :
				QCoreApplication::translate("VibeStudioTextureEditor", "Opened %n sprite frame(s) as layers, aligned on the sprite origin. Visible layers export as frames.", nullptr, kept));
		} else { setStatus(error); }
	}
	m_export->setOptions(options);
	QString target = source;
	if ((!context || context().wadMagic.isEmpty()) && QFileInfo(source).suffix().compare(textureExportSuffix(options.format), Qt::CaseInsensitive) != 0) {
		const auto slash = source.lastIndexOf(QLatin1Char('/'));
		target = source.left(slash + 1) + QFileInfo(source).completeBaseName() + QLatin1Char('.') + textureExportSuffix(options.format);
	}
	m_virtualPath->setText(target); m_savedMetadata = projectMetadata(); refresh(); return true;
}

void TextureEditorDialog::refreshContext()
{
	const auto current = context ? context() : TextureEditorContext{};
	if (current.packagePath != m_contextKey) { m_contextKey = current.packagePath; m_replace->setChecked(false); }
	if (current.packageTargetKey != m_packageTargetKey) { m_packageTargetKey = current.packageTargetKey; m_export->invalidatePreview(); }
	m_contextLabel->setText(current.packagePath.isEmpty() ? QCoreApplication::translate("VibeStudioTextureEditor", "No package open · export to a project asset folder.") :
		QCoreApplication::translate("VibeStudioTextureEditor", "Package: %1 · Map: %2").arg(current.packagePath, current.mapPath.isEmpty() ? QCoreApplication::translate("VibeStudioTextureEditor", "none") : current.mapPath));
	m_stage->setEnabled(!m_busy && current.canStage && bool(handoff));
	TextureExportOptions options;
	QString problem;
	const bool validReference = m_export->options(&options, &problem) &&
		!textureExportMapReference(options, m_virtualPath->text(), current.mapFormat, current.wadMagic, &problem).isEmpty();
	m_apply->setEnabled(!m_busy && current.canStage && current.canApply && bool(handoff) && validReference);
	m_apply->setToolTip(!validReference ? problem : QCoreApplication::translate("VibeStudioTextureEditor", "Stage the texture and apply its name to the selected brushes or patches. Save the package and map separately."));
}

void TextureEditorDialog::refresh()
{
	// A cancelled or stale palette request retains the previous palette choice.
	if (!m_busy) { const QSignalBlocker blocker(m_paletteChoice); m_paletteChoice->setCurrentIndex(m_paletteChoice->findData(m_palette.palette.id)); }
	if (m_previewRevision != m_document.revision()) { m_export->invalidatePreview(); m_previewRevision = m_document.revision(); }
	m_undo->setEnabled(!m_busy && m_document.canUndo()); m_redo->setEnabled(!m_busy && m_document.canRedo());
	m_undo->setText(m_document.canUndo() ? QCoreApplication::translate("VibeStudioTextureEditor", "Undo %1").arg(m_document.undoLabel()) : QCoreApplication::translate("VibeStudioTextureEditor", "Undo"));
	m_redo->setText(m_document.canRedo() ? QCoreApplication::translate("VibeStudioTextureEditor", "Redo %1").arg(m_document.redoLabel()) : QCoreApplication::translate("VibeStudioTextureEditor", "Redo"));
	m_canvas->refresh();
	refreshLayers();
	m_width->setValue(m_document.image().width()); m_height->setValue(m_document.image().height());
	refreshTransforms();
	m_dimensions->setText(QCoreApplication::translate("VibeStudioTextureEditor", "%1 × %2 · %3").arg(m_document.image().width()).arg(m_document.image().height()).arg(
		hasUnsavedChanges() ? QCoreApplication::translate("VibeStudioTextureEditor", "Unsaved changes") : QCoreApplication::translate("VibeStudioTextureEditor", "Unmodified")));
	setWindowModified(hasUnsavedChanges());
	setWindowTitle(QCoreApplication::translate("VibeStudioTextureEditor", "Texture Editor — %1[*]").arg(m_source.isEmpty() ? QCoreApplication::translate("VibeStudioTextureEditor", "Untitled") : m_source));
	refreshContext();
	refreshFrames();
	refreshOnionSkin();
}

QJsonObject TextureEditorDialog::projectMetadata() const
{
	auto metadata = m_extraMetadata;
	metadata.insert(QStringLiteral("palette"), texturePaletteMetadata(m_palette));
	metadata.insert(QStringLiteral("packageTexturePath"), m_virtualPath->text());
	metadata.insert(QStringLiteral("export"), m_export->settings());
	return metadata;
}

bool TextureEditorDialog::hasUnsavedChanges() const { return m_document.isDirty() || projectMetadata() != m_savedMetadata; }

void TextureEditorDialog::refreshTransforms()
{
	const QSize size(m_width->value(), m_height->value());
	const auto offset = textureAnchorOffset(m_document.size(), size, static_cast<TextureAnchor>(m_canvasAnchor->currentIndex()));
	const bool validSize = qint64(size.width()) * size.height() <= TextureDocument::MaximumPixels &&
		qint64(size.width()) * size.height() * m_document.layers().size() <= TextureDocument::MaximumLayerPixels;
	m_canvasSizeButton->setEnabled(!m_busy && validSize);
	m_canvasSizeHint->setText(validSize ? QCoreApplication::translate("VibeStudioTextureEditor", "%1 × %2 → %3 × %4 · content offset %5, %6")
		.arg(m_document.size().width()).arg(m_document.size().height()).arg(size.width()).arg(size.height()).arg(offset.x()).arg(offset.y()) :
		QCoreApplication::translate("VibeStudioTextureEditor", "Canvas dimensions exceed the document pixel limit."));
	const QRect selection = m_document.selection();
	if (selection != m_transformSelection) {
		const QSignalBlocker widthBlock(m_selectionWidth), heightBlock(m_selectionHeight);
		m_selectionWidth->setValue(selection.width()); m_selectionHeight->setValue(selection.height()); m_transformSelection = selection;
	}
	const auto* layer = m_document.activeLayer();
	const bool editable = !m_busy && !selection.isEmpty() && layer && layer->visible && !layer->locked;
	m_selectionWidth->setEnabled(editable); m_selectionHeight->setEnabled(editable); m_selectionAnchor->setEnabled(editable); m_selectionSmooth->setEnabled(editable);
	const QSize requested(m_selectionWidth->value(), m_selectionHeight->value());
	const auto anchor = static_cast<TextureAnchor>(m_selectionAnchor->currentIndex());
	const QRect resized(selection.topLeft() + textureAnchorOffset(requested, selection.size(), anchor), requested);
	const QRect rotated(selection.topLeft() + textureAnchorOffset(selection.size().transposed(), selection.size(), anchor), selection.size().transposed());
	const QRect canvas(QPoint(), m_document.size());
	const bool fits = canvas.contains(resized) && qint64(requested.width()) * requested.height() <= TextureDocument::MaximumPixels;
	m_resizeSelection->setEnabled(editable && fits); m_rotateSelection->setEnabled(editable && canvas.contains(rotated));
	QString status = selection.isEmpty() ? QCoreApplication::translate("VibeStudioTextureEditor", "No selection") :
		QCoreApplication::translate("VibeStudioTextureEditor", "%1 × %2 at %3, %4 → %5 × %6 at %7, %8")
			.arg(selection.width()).arg(selection.height()).arg(selection.x()).arg(selection.y()).arg(requested.width()).arg(requested.height()).arg(resized.x()).arg(resized.y());
	if (!selection.isEmpty() && layer && (!layer->visible || layer->locked)) { status += QLatin1Char('\n') + QCoreApplication::translate("VibeStudioTextureEditor", "Active layer is hidden or locked."); }
	else if (!selection.isEmpty() && !fits) { status += QLatin1Char('\n') + QCoreApplication::translate("VibeStudioTextureEditor", "Resize would exceed the canvas."); }
	if (!selection.isEmpty() && !canvas.contains(rotated)) { status += QLatin1Char('\n') + QCoreApplication::translate("VibeStudioTextureEditor", "Rotation would exceed the canvas."); }
	m_selectionHint->setText(status);
}

void TextureEditorDialog::refreshLayers()
{
	if (!m_layers) { return; }
	const QSignalBlocker listBlock(m_layers), nameBlock(m_layerName), visibleBlock(m_layerVisible), lockedBlock(m_layerLocked), opacityBlock(m_layerOpacity), blendBlock(m_layerBlend);
	m_layers->clear();
	for (int index = int(m_document.layers().size()) - 1; index >= 0; --index) {
		const auto& layer = m_document.layers()[index];
		QStringList state;
		if (!layer.visible) { state << QCoreApplication::translate("VibeStudioTextureEditor", "Hidden"); }
		if (layer.locked) { state << QCoreApplication::translate("VibeStudioTextureEditor", "Locked"); }
		state << QStringLiteral("%1%").arg(layer.opacity);
		auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(layer.pixels.scaled(40, 40, Qt::KeepAspectRatio))), layer.name + QLatin1Char('\n') + state.join(QStringLiteral(" · ")), m_layers);
		item->setData(Qt::UserRole, index); item->setData(Qt::AccessibleTextRole, item->text());
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled); item->setCheckState(layer.visible ? Qt::Checked : Qt::Unchecked);
		if (index == m_document.activeLayerIndex()) { m_layers->setCurrentItem(item); }
	}
	if (const auto* layer = m_document.activeLayer()) {
		m_layerName->setText(layer->name); m_layerVisible->setChecked(layer->visible); m_layerLocked->setChecked(layer->locked);
		m_layerOpacity->setValue(layer->opacity); m_layerBlend->setCurrentIndex(m_layerBlend->findData(textureBlendModeId(layer->blend)));
	}
}

void TextureEditorDialog::selectLayer(int index)
{
	if (m_busy) { return; }
	m_canvas->finishStroke(); if (m_document.selectLayer(index)) { refresh(); }
}

void TextureEditorDialog::updateLayerProperties()
{
	if (m_busy || !m_document.activeLayer()) { return; }
	const auto& layer = *m_document.activeLayer();
	if (layer.name == m_layerName->text().trimmed() && layer.visible == m_layerVisible->isChecked() && layer.locked == m_layerLocked->isChecked() &&
		layer.opacity == m_layerOpacity->value() && textureBlendModeId(layer.blend) == m_layerBlend->currentData().toString()) { return; }
	applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("layer-properties")}, {QStringLiteral("name"), m_layerName->text()},
		{QStringLiteral("visible"), m_layerVisible->isChecked()}, {QStringLiteral("locked"), m_layerLocked->isChecked()},
		{QStringLiteral("opacity"), m_layerOpacity->value()}, {QStringLiteral("blend"), m_layerBlend->currentData().toString()}}});
}

void TextureEditorDialog::importLayer()
{
	const QString path = QFileDialog::getOpenFileName(this, QCoreApplication::translate("VibeStudioTextureEditor", "Import Texture Layer"), {},
		assetImageOpenFilter(false));
	if (path.isEmpty()) { return; }
	const auto palette = m_palette.palette;
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Importing layer…"), [path, palette](TextureDocument& document, QString* error, const TextureProgress& progress) {
		TextureDocument imported;
		return loadTextureFile(path, palette, &imported, error, nullptr, progress) && document.addLayer(QFileInfo(path).completeBaseName().left(128), imported.image(), error);
	});
}

void TextureEditorDialog::runJob(const QString& title, std::function<bool(TextureDocument&, QString*, const TextureProgress&)> operation, std::function<void()> completed, bool cancellable)
{
	if (m_busy) { return; }
	m_canvas->finishStroke(); m_busy = true; m_cancelled = false; m_cancellable = cancellable;
	m_tools->setEnabled(false); m_properties->setEnabled(false); m_canvas->setEnabled(false);
	m_task = std::make_shared<TextureEditorTask>(); m_progressTimer->start(); m_progress->setRange(0, 0);
	m_progress->show(); m_cancel->setVisible(cancellable); m_cancel->setEnabled(true); setStatus(title); refreshContext();
	struct Result { TextureDocument document; QString error; bool ok = false; };
	auto result = std::make_shared<Result>(); result->document = m_document;
	m_worker = QThread::create([result, task = m_task, operation = std::move(operation)]() {
		const auto progress = [task](qint64 completed, qint64 total) {
			if (task->cancelled.load()) { return false; }
			if (total > 0) { task->progress.store(int(std::clamp<qint64>(completed * 1000 / total, 0, 1000))); }
			return true;
		};
		result->ok = progress(0, 0) && operation(result->document, &result->error, progress);
		// Compose transformed layers on the worker before publishing to the UI.
		if (result->ok && progress(1, 1) && result->document.image().isNull()) {
			result->ok = false;
			result->error = QCoreApplication::translate("VibeStudioTextureEditor", "Unable to compose the texture preview.");
		}
	});
	connect(m_worker, &QThread::finished, this, [this, result, completed = std::move(completed)]() {
		m_worker->wait(); m_worker->deleteLater(); m_worker = nullptr;
		m_progressTimer->stop(); m_task.reset();
		m_busy = false; m_cancellable = false; m_tools->setEnabled(true); m_properties->setEnabled(true); updateCanvasEnabled();
		m_progress->hide(); m_cancel->hide();
		if (m_cancelled) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Cancelled. The texture is unchanged.")); }
		else if (!result->ok) { setStatus(result->error); }
		else { m_document = std::move(result->document); setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Completed.")); if (completed) { completed(); } }
		refresh();
	});
	m_worker->start();
}

void TextureEditorDialog::applyOperations(const QJsonArray& operations)
{
	const auto palette = m_palette.palette;
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Applying texture operation…"), [operations, palette](TextureDocument& document, QString* error, const TextureProgress& progress) {
		return applyTextureOperations(&document, operations, palette, error, progress);
	});
}

void TextureEditorDialog::cancelPending()
{
	if (!m_busy || !m_cancellable) { return; }
	m_cancelled = true; m_cancel->setEnabled(false);
	if (m_task) { m_task->cancelled.store(true); }
	setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Cancelling texture operation…"));
}

void TextureEditorDialog::undo()
{
	if (m_busy) { return; }
	m_canvas->finishStroke(); auto previous = m_document;
	if (!previous.undo()) { return; }
	if (qint64(previous.size().width()) * previous.size().height() * previous.layers().size() > TextureDocument::MaximumPixels) {
		runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Undoing texture edit…"), [previous = std::move(previous)](TextureDocument& document, QString*, const TextureProgress&) { document = previous; return true; });
	} else { m_document = std::move(previous); refresh(); }
}
void TextureEditorDialog::redo()
{
	if (m_busy) { return; }
	m_canvas->finishStroke(); auto next = m_document;
	if (!next.redo()) { return; }
	if (qint64(next.size().width()) * next.size().height() * next.layers().size() > TextureDocument::MaximumPixels) {
		runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Redoing texture edit…"), [next = std::move(next)](TextureDocument& document, QString*, const TextureProgress&) { document = next; return true; });
	} else { m_document = std::move(next); refresh(); }
}

bool TextureEditorDialog::confirmDiscard(std::function<void()> afterSave)
{
	if (m_busy) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Wait for the current operation to finish.")); return false; }
	m_canvas->finishStroke();
	if (!hasUnsavedChanges()) { return true; }
	const auto choice = QMessageBox::warning(this, QCoreApplication::translate("VibeStudioTextureEditor", "Unsaved texture"),
		QCoreApplication::translate("VibeStudioTextureEditor", "Save the texture project before continuing? The project preserves layers and palette settings. Exported images and staged packages are separate."),
		QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
	if (choice == QMessageBox::Save) {
		if (m_projectIdentity.path.isEmpty()) { saveAs(std::move(afterSave)); }
		else { saveProjectToPath(m_projectIdentity.path, true, std::move(afterSave)); }
		return false;
	}
	return choice == QMessageBox::Discard;
}

void TextureEditorDialog::newTexture()
{
	const QSize size(m_width->value(), m_height->value());
	if (!confirmDiscard([this, size]() { m_width->setValue(size.width()); m_height->setValue(size.height()); newTexture(); })) { return; }
	QString error;
	if (!m_document.create(size, Qt::transparent, &error)) { setStatus(error); return; }
	retireRecovery();
	m_source.clear(); m_savedPath.clear(); m_projectIdentity = {}; m_extraMetadata = {}; m_replace->setChecked(false); m_virtualPath->setText(QStringLiteral("textures/custom/texture.png"));
	m_export->setOptions({});
	m_recoveredDraft = false;
	refresh(); m_canvas->fit();
	setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "New transparent texture. Paint pixels or import an image."));
}

void TextureEditorDialog::openFile()
{
	const QString path = QFileDialog::getOpenFileName(this, QCoreApplication::translate("VibeStudioTextureEditor", "Open Texture"), m_savedPath,
		assetImageOpenFilter(true));
	if (path.isEmpty()) { return; }
	openFromPath(path);
}

void TextureEditorDialog::openFromPath(const QString& path)
{
	if (!confirmDiscard([this, path]() { openFromPath(path); })) { return; }
	const auto palette = m_palette.palette;
	struct Loaded { TextureProjectIdentity identity; QJsonObject metadata; IdTechPaletteResolution palette; bool hasPalette = false; };
	auto loaded = std::make_shared<Loaded>();
	const bool project = QFileInfo(path).suffix().compare(QStringLiteral("vtexture"), Qt::CaseInsensitive) == 0;
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Loading texture…"), [path, palette, project, loaded](TextureDocument& document, QString* error, const TextureProgress& progress) {
		if (!project) {
			IdTechImageDecodeResult decoded;
			if (!loadTextureFile(path, palette, &document, error, &decoded, progress)) { return false; }
			loaded->metadata.insert(QStringLiteral("export"), textureExportOptionsJson(textureExportOptionsForImage(decoded))); return true;
		}
		if (!readTextureProject(path, &document, &loaded->identity, &loaded->metadata, error, progress)) { return false; }
		loaded->hasPalette = loaded->metadata.contains(QStringLiteral("palette"));
		return !loaded->hasPalette || texturePaletteFromMetadata(loaded->metadata.value(QStringLiteral("palette")).toObject(), &loaded->palette, error);
	}, [this, path, project, loaded]() {
		retireRecovery();
		m_source = path; m_savedPath.clear(); m_projectIdentity = loaded->identity; m_extraMetadata = loaded->metadata; m_replace->setChecked(false);
		m_recoveredDraft = false;
		if (loaded->hasPalette) { setPaletteResolution(loaded->palette); }
		loadExportSettings(loaded->metadata);
		m_virtualPath->setText(loaded->metadata.value(QStringLiteral("packageTexturePath")).toString(QStringLiteral("textures/custom/") + QFileInfo(path).completeBaseName() + QStringLiteral(".png")));
		m_savedMetadata = projectMetadata(); m_canvas->fit();
		setStatus(project ? QCoreApplication::translate("VibeStudioTextureEditor", "Loaded texture project with editable layers.") :
			QCoreApplication::translate("VibeStudioTextureEditor", "Loaded one image. Supported native metadata is available in Export; mipmaps are regenerated from edited pixels. Sprite frames are not editable here."));
	});
}

void TextureEditorDialog::saveAs(std::function<void()> completed)
{
	if (m_busy) { return; }
	QString path = QFileDialog::getSaveFileName(this, QCoreApplication::translate("VibeStudioTextureEditor", "Save Texture Project"),
		m_projectIdentity.path.isEmpty() ? QStringLiteral("texture.vtexture") : m_projectIdentity.path, QCoreApplication::translate("VibeStudioTextureEditor", "VibeStudio texture (*.vtexture)"));
	if (path.isEmpty()) { return; }
	if (QFileInfo(path).suffix().isEmpty()) { path += QStringLiteral(".vtexture"); }
	saveProjectToPath(path, true, std::move(completed));
}

void TextureEditorDialog::saveProjectToPath(const QString& path, bool overwrite, std::function<void()> completed)
{
	TextureProjectSaveRequest request; request.path = path; request.overwrite = overwrite;
	if (m_recoveredDraft) {
		request.overwrite = false;
		if (QFileInfo::exists(path)) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Save the recovered draft to a new project file. Existing files cannot be replaced by its first save.")); return; }
	}
	if (!m_projectIdentity.path.isEmpty() && (QFileInfo(path).absoluteFilePath() == m_projectIdentity.path || QFileInfo(path).canonicalFilePath() == m_projectIdentity.canonicalPath)) {
		request.expectedSha256 = m_projectIdentity.sha256; request.expectedCanonicalPath = m_projectIdentity.canonicalPath;
	}
	const auto metadata = projectMetadata(); auto prepared = std::make_shared<TextureProjectPreparedSave>();
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Preparing texture project…"), [request, metadata, prepared](TextureDocument& document, QString* error, const TextureProgress& progress) {
		*prepared = prepareTextureProjectSave(document, request, metadata, progress);
		*error = prepared->report.error; return prepared->report.succeeded;
	}, [this, metadata, prepared, completed = std::move(completed)]() {
		auto result = std::make_shared<TextureProjectSaveReport>();
		runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Saving texture project…"), [prepared, result](TextureDocument& document, QString* error, const TextureProgress&) {
			*result = publishTextureProjectSave(*prepared); if (!result->succeeded) { *error = result->error; return false; }
			document.markSaved(); return true;
		}, [this, metadata, result, completed]() {
			m_projectIdentity = result->identity; m_savedMetadata = metadata; m_source = result->identity.path;
			m_recoveredDraft = false;
			retireRecovery();
			setStatus(result->backupPath.isEmpty() ? QCoreApplication::translate("VibeStudioTextureEditor", "Saved texture project: %1").arg(m_source) :
				QCoreApplication::translate("VibeStudioTextureEditor", "Saved texture project: %1\nPrevious version: %2").arg(m_source, result->backupPath));
			if (activity) { activity(QCoreApplication::translate("VibeStudioTextureEditor", "Texture Project Saved"), m_source, QCoreApplication::translate("VibeStudioTextureEditor", "%n layer(s) saved.", nullptr, int(m_document.layers().size()))); }
			if (completed) { completed(); }
		}, false);
	});
}

void TextureEditorDialog::exportFile()
{
	if (m_busy) { return; }
	TextureExportOptions options; QString error;
	if (!m_export->options(&options, &error)) { setStatus(error); return; }
	const QString suffix = textureExportSuffix(options.format);
	const auto suggested = m_savedPath.isEmpty() ? QStringLiteral("texture.") + suffix : QFileInfo(m_savedPath).absolutePath() + QLatin1Char('/') + QFileInfo(m_savedPath).completeBaseName() + QLatin1Char('.') + suffix;
	QString path = QFileDialog::getSaveFileName(this, QCoreApplication::translate("VibeStudioTextureEditor", "Export Texture"), suggested,
		QCoreApplication::translate("VibeStudioTextureEditor", "Texture output (*.%1)").arg(suffix));
	if (path.isEmpty()) { return; }
	if (QFileInfo(path).suffix().isEmpty()) { path += QLatin1Char('.') + suffix; }
	saveExportToPath(path, true);
}

void TextureEditorDialog::loadExportSettings(const QJsonObject& metadata)
{
	TextureExportOptions options; textureExportOptionsFromJson(metadata.value(QStringLiteral("export")).toObject(), &options);
	m_export->setOptions(options);
}
void TextureEditorDialog::setExportOptions(const TextureExportOptions& options) { if (!m_busy) { m_export->setOptions(options); refresh(); } }
bool TextureEditorDialog::exportOptions(TextureExportOptions* options, QString* error) const { return m_export->options(options, error); }
void TextureEditorDialog::previewExport()
{
	if (m_busy) { return; }
	TextureExportOptions options; QString error;
	if (!m_export->options(&options, &error)) { setStatus(error); return; }
	m_export->invalidatePreview(); const auto palette = m_palette; auto result = std::make_shared<TextureExportResult>();
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Preparing export preview…"), [options, palette, result](TextureDocument& document, QString* error, const TextureProgress& progress) {
		*result = encodeTextureDocumentExport(document, options, palette, progress); *error = result->error; return result->succeeded;
	}, [this, result, palette]() { m_previewRevision = m_document.revision(); m_export->showResult(*result, palette); setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Export validated. Review the preview and warnings before packaging.")); });
}
void TextureEditorDialog::saveExportToPath(const QString& path, bool overwrite, std::function<void()> completed)
{
	if (m_busy) { return; }
	TextureExportOptions options; QString error;
	if (!m_export->options(&options, &error)) { setStatus(error); return; }
	if (QFileInfo(path).suffix().compare(textureExportSuffix(options.format), Qt::CaseInsensitive) != 0) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "The filename extension must match the selected export profile.")); return; }
	const auto palette = m_palette; auto result = std::make_shared<TextureExportResult>(); auto target = std::make_shared<TextureOutputTarget>();
	auto companions = std::make_shared<QVector<TextureOutputTarget>>();
	// Capture before encoding. Cancellation can discard prepared bytes; publication
	// is a separate short, non-cancellable job so a committed file is never reported cancelled.
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Encoding texture export…"), [path, overwrite, target, companions, options, palette, result](TextureDocument& document, QString* error, const TextureProgress& progress) {
		if (!inspectTextureOutputTarget(path, overwrite, target.get(), error, [&] { return !progress(0, 1); })) { return false; }
		*result = encodeTextureDocumentExport(document, options, palette, progress); *error = result->error;
		if (!result->succeeded) { return false; }
		// Sprite frames named by the output are published beside it, first.
		for (const auto& companion : result->companions) {
			TextureOutputTarget companionTarget;
			if (!inspectTextureOutputTarget(textureExportCompanionPath(path, companion), overwrite, &companionTarget, error, [&] { return !progress(0, 1); })) { return false; }
			companions->push_back(companionTarget);
		}
		return true;
	}, [this, path, target, companions, result, palette, completed = std::move(completed)]() {
		runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Publishing texture export…"), [target, companions, result](TextureDocument&, QString* error, const TextureProgress&) {
			for (qsizetype index = 0; index < companions->size(); ++index) { if (!writeTextureOutput(companions->at(index), result->companions[index].bytes, false, error)) { return false; } }
			return writeTextureOutput(*target, result->bytes, false, error);
		},
			[this, path, result, palette, completed]() {
				m_savedPath = path; m_previewRevision = m_document.revision(); m_export->showResult(*result, palette);
				if (activity) { activity(QCoreApplication::translate("VibeStudioTextureEditor", "Texture Exported"), path, QLocale().formattedDataSize(result->bytes.size())); }
				setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Exported: %1. Save Project preserves editable layers and export settings.").arg(path)); if (completed) { completed(); }
			}, false);
	});
}

void TextureEditorDialog::saveToPath(const QString& path, bool overwrite, std::function<void()> completed)
{
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Saving PNG…"), [path, overwrite](TextureDocument& document, QString* error, const TextureProgress&) {
		return document.savePng(path, overwrite, false, error);
	}, [this, path, completed = std::move(completed)]() { m_savedPath = path; setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Exported PNG: %1. Save the project to preserve editable layers.").arg(path)); if (completed) { completed(); } }, false);
}

void TextureEditorDialog::stage(bool applyToMap)
{
	refreshContext();
	if (m_busy || !(applyToMap ? m_apply : m_stage)->isEnabled()) { return; }
	const auto current = context(); const auto path = m_virtualPath->text(); const bool replace = m_replace->isChecked();
	TextureExportOptions options; QString error;
	if (!m_export->options(&options, &error)) { setStatus(error); return; }
	const auto palette = m_palette; auto result = std::make_shared<TextureExportResult>();
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Preparing texture for package staging…"), [result, options, palette](TextureDocument& document, QString* error, const TextureProgress& progress) {
		*result = encodeTextureDocumentExport(document, options, palette, progress); *error = result->error; return result->succeeded;
	}, [this, result, options, palette, path, replace, applyToMap, current]() {
			const auto now = context();
			if (!now.canStage || now.packagePath != current.packagePath || now.packageTargetKey != current.packageTargetKey || (applyToMap && (!now.canApply || now.mapPath != current.mapPath || now.mapTargetKey != current.mapTargetKey))) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "The project context changed. Stage again after reviewing the target.")); return; }
			QString error;
			if (!handoff(*result, options, path, replace, applyToMap, &error)) { setStatus(error); return; }
			m_previewRevision = m_document.revision(); m_export->showResult(*result, palette);
			setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Texture staged. Save the package separately; map edits also remain unsaved."));
		});
}

QVector<TextureExportCompanion> TextureEditorDialog::companionMaps(const QString& basePath) const
{
	const bool ioquake3 = m_companionNaming && m_companionNaming->currentData().toString() == QStringLiteral("ioquake3");
	const QFileInfo info(basePath);
	const QString directory = basePath.contains(QLatin1Char('/')) ? basePath.left(basePath.lastIndexOf(QLatin1Char('/')) + 1) : QString();
	const QString stem = directory + info.completeBaseName();
	const QImage image = m_document.image().convertToFormat(QImage::Format_ARGB32);
	QVector<TextureExportCompanion> maps;
	const auto add = [&](const QString& suffix, const QImage& map) {
		if (map.isNull()) { return; }
		QString error; const QByteArray png = encodeTextureOutputPng(map, &error);
		if (!png.isEmpty()) { maps.push_back({stem + suffix + QStringLiteral(".png"), png}); }
	};
	add(ioquake3 ? QStringLiteral("_n") : QStringLiteral("_norm"), deriveTextureNormalMap(image, 2.5, true));
	add(ioquake3 ? QStringLiteral("_s") : QStringLiteral("_gloss"), deriveTextureGlossMap(image));
	add(QStringLiteral("_glow"), deriveTextureGlowMap(image, 0.85));
	return maps;
}

void TextureEditorDialog::stageCompanionMaps()
{
	refreshContext();
	const auto current = context ? context() : TextureEditorContext{};
	if (m_busy || !current.canStage || !stageFiles || !current.wadMagic.isEmpty()) {
		setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Open a folder, PAK, ZIP or PK3 package to stage companion maps. WADs cannot hold PNG files."));
		return;
	}
	const auto maps = companionMaps(m_virtualPath->text());
	QString error;
	if (maps.isEmpty() || !stageFiles(maps, m_replace->isChecked(), &error)) { setStatus(error.isEmpty() ? QCoreApplication::translate("VibeStudioTextureEditor", "No companion maps could be derived.") : error); return; }
	QStringList paths; for (const auto& map : maps) { paths << map.path; }
	setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Staged %1. Save the package separately.").arg(paths.join(QStringLiteral(", "))));
}

bool TextureEditorDialog::exportCompanionMaps(const QString& basePngPath, bool overwrite, QString* error)
{
	const auto maps = companionMaps(QDir::fromNativeSeparators(basePngPath));
	if (maps.isEmpty()) { if (error) { *error = QCoreApplication::translate("VibeStudioTextureEditor", "No companion maps could be derived."); } return false; }
	QVector<TextureOutputTarget> targets;
	for (const auto& map : maps) {
		TextureOutputTarget target;
		if (!inspectTextureOutputTarget(map.path, overwrite, &target, error)) { return false; }
		targets << target;
	}
	for (qsizetype index = 0; index < targets.size(); ++index) { if (!writeTextureOutput(targets[index], maps[index].bytes, false, error)) { return false; } }
	if (activity) { activity(QCoreApplication::translate("VibeStudioTextureEditor", "Companion Maps Exported"), basePngPath, QCoreApplication::translate("VibeStudioTextureEditor", "%n map(s) written.", nullptr, int(maps.size()))); }
	return true;
}

void TextureEditorDialog::swapColors()
{
	const QString primary = m_color->text(); m_color->setText(m_secondaryColor->text()); m_secondaryColor->setText(primary);
}

void TextureEditorDialog::rememberColor(const QColor& color)
{
	if (!color.isValid()) { return; }
	const QString name = color.name(QColor::HexArgb);
	m_recent.removeAll(name); m_recent.prepend(name);
	while (m_recent.size() > 8) { m_recent.removeLast(); }
	refreshRecentColors();
}

void TextureEditorDialog::refreshRecentColors()
{
	auto* layout = qobject_cast<QHBoxLayout*>(m_recentColors->layout());
	for (auto* old : m_recentColors->findChildren<QToolButton*>()) { layout->removeWidget(old); old->hide(); old->deleteLater(); }
	for (int index = 0; index < m_recent.size(); ++index) {
		const QColor color(m_recent[index]);
		auto* swatch = new QToolButton; swatch->setObjectName(QStringLiteral("textureRecentColor%1").arg(index)); swatch->setAutoRaise(false);
		const int side = fontMetrics().height();
		QPixmap pixmap(side, side); pixmap.fill(Qt::transparent);
		QPainter painter(&pixmap); painter.fillRect(pixmap.rect(), palette().color(QPalette::Base)); painter.fillRect(pixmap.rect().adjusted(1, 1, -1, -1), color);
		painter.setPen(palette().color(QPalette::Text)); painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1)); painter.end();
		swatch->setIcon(QIcon(pixmap)); swatch->setIconSize(pixmap.size()); swatch->setFocusPolicy(Qt::StrongFocus);
		swatch->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Recent color %1").arg(m_recent[index]));
		swatch->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "%1 · click for the paint color, Shift+click for the secondary color").arg(m_recent[index]));
		const QString name = m_recent[index];
		connect(swatch, &QToolButton::clicked, this, [this, name]() {
			(QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier) ? m_secondaryColor : m_color)->setText(name);
		});
		layout->insertWidget(index, swatch);
	}
}

void TextureEditorDialog::refreshFrames()
{
	if (!m_frameIndex) { return; }
	auto* tabs = findChild<QTabWidget*>(QStringLiteral("textureProperties"));
	const bool shown = tabs && tabs->currentIndex() == tabs->count() - 1;
	if (!shown && !framePlaying()) { return; }
	if (m_framesRevision != m_document.revision() || m_frames.isEmpty()) {
		int visible = 0; for (const auto& layer : m_document.layers()) { visible += layer.visible; }
		m_frames = m_document.layerFrames(); m_framesRevision = m_document.revision();
		const QSignalBlocker blocker(m_frameIndex);
		m_frameIndex->setRange(1, std::max(1, int(m_frames.size()))); m_frameIndex->setEnabled(m_frames.size() > 1);
		m_framePlay->setEnabled(m_frames.size() > 1);
		if (m_frames.size() < 2) { setFramePlaying(false); }
		m_frameStatus->setText(m_frames.isEmpty() ? QCoreApplication::translate("VibeStudioTextureEditor", "No visible layers: show a layer to preview it as a frame.") :
			QCoreApplication::translate("VibeStudioTextureEditor", "%n visible layer(s) export as sprite frames.", nullptr, visible));
	}
	showFrame(m_frameIndex->value() - 1);
}

void TextureEditorDialog::showFrame(int frame)
{
	if (!m_framePreview) { return; }
	if (frame < 0 || frame >= m_frames.size()) { m_framePreview->clear(); return; }
	const QImage& image = m_frames[frame];
	const QSize size = image.size().scaled(QSize(std::max(1, m_framePreview->width()), m_framePreview->height()), Qt::KeepAspectRatio);
	QPixmap pixmap(size); QPainter painter(&pixmap);
	const int cell = std::max(4, fontMetrics().height() / 2);
	for (int y = 0; y < size.height(); y += cell) { for (int x = 0; x < size.width(); x += cell) { painter.fillRect(QRect(x, y, cell, cell), ((x / cell + y / cell) % 2) ? palette().color(QPalette::Base) : palette().color(QPalette::AlternateBase)); } }
	painter.drawImage(QRect(QPoint(), size), image); painter.end(); m_framePreview->setPixmap(pixmap);
	m_framePreview->setAccessibleDescription(QCoreApplication::translate("VibeStudioTextureEditor", "Frame %1 of %2, %3 by %4 pixels.").arg(frame + 1).arg(m_frames.size()).arg(image.width()).arg(image.height()));
	if (m_frameIndex->value() != frame + 1) { const QSignalBlocker blocker(m_frameIndex); m_frameIndex->setValue(frame + 1); }
}

bool TextureEditorDialog::framePlaying() const { return m_frameTimer && m_frameTimer->isActive(); }

void TextureEditorDialog::setFramePlaying(bool playing)
{
	if (!m_frameTimer) { return; }
	if (playing && m_frames.size() > 1) { m_frameTimer->start(); } else { m_frameTimer->stop(); }
	m_framePlay->setText(framePlaying() ? QCoreApplication::translate("VibeStudioTextureEditor", "Stop Frames") : QCoreApplication::translate("VibeStudioTextureEditor", "Play Frames"));
	m_framePlay->setAccessibleName(m_framePlay->text());
}

void TextureEditorDialog::refreshOnionSkin()
{
	if (!m_onionSkin || !m_onionSkin->isChecked()) { m_canvas->setOnionSkin({}); return; }
	// The nearest visible layer below the active one, at its own opacity.
	for (int index = m_document.activeLayerIndex() - 1; index >= 0; --index) {
		const auto& layer = m_document.layers()[index];
		if (layer.visible) { m_canvas->setOnionSkin(layer.pixels.convertToFormat(QImage::Format_ARGB32)); return; }
	}
	m_canvas->setOnionSkin({});
}

void TextureEditorDialog::importFramesFromPaths(const QStringList& paths)
{
	if (paths.isEmpty()) { return; }
	const auto palette = m_palette.palette;
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Importing frames…"), [paths, palette](TextureDocument& document, QString* error, const TextureProgress& progress) {
		for (qsizetype index = 0; index < paths.size(); ++index) {
			TextureDocument imported;
			if (!loadTextureFile(paths[index], palette, &imported, error, nullptr, [&](qint64 done, qint64 total) { return progress(index * 1000 + (total ? done * 1000 / total : 0), paths.size() * 1000); })) { return false; }
			const QImage pixels = imported.image();
			if (pixels.width() > document.size().width() || pixels.height() > document.size().height()) {
				if (error) { *error = QCoreApplication::translate("VibeStudioTextureEditor", "%1 is larger than the canvas. Enlarge the canvas first.").arg(QFileInfo(paths[index]).fileName()); }
				return false;
			}
			QImage frame(document.size(), QImage::Format_ARGB32); frame.fill(Qt::transparent);
			QPainter painter(&frame); painter.setCompositionMode(QPainter::CompositionMode_Source);
			painter.drawImage(textureAnchorOffset(pixels.size(), document.size(), TextureAnchor::Center), pixels.convertToFormat(QImage::Format_ARGB32)); painter.end();
			if (!document.addLayer(QFileInfo(paths[index]).completeBaseName().left(128), frame, error)) { return false; }
		}
		return true;
	}, [this, count = paths.size()]() { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Imported %n frame(s) as layers.", nullptr, int(count))); });
}

void TextureEditorDialog::loadPaletteFromPath(const QString& path)
{
	const QString id = m_paletteChoice->currentData().toString().isEmpty() ? m_palette.palette.id : m_paletteChoice->currentData().toString();
	auto resolved = std::make_shared<IdTechPaletteResolution>();
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Loading palette file…"), [path, id, resolved](TextureDocument&, QString* error, const TextureProgress&) {
		QFile file(path);
		if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
			if (error) { *error = QCoreApplication::translate("VibeStudioTextureEditor", "Cannot read the palette file, or it exceeds 4 MiB."); } return false;
		}
		const auto bytes = file.read(4 * 1024 * 1024 + 1);
		const bool pcx = QFileInfo(path).suffix().compare(QStringLiteral("pcx"), Qt::CaseInsensitive) == 0;
		if (!(pcx ? parsePcxPalette(bytes, id, &resolved->palette, error) : parseIdTechPaletteBytes(bytes, id, &resolved->palette, error))) { return false; }
		resolved->requestedPaletteId = id; resolved->sourceVirtualPath = QFileInfo(path).absoluteFilePath();
		resolved->palette.sourceDescription = resolved->sourceVirtualPath; resolved->palette.generated = false;
		return true;
	}, [this, resolved, path]() {
		setPaletteResolution(*resolved);
		setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Palette loaded from %1. Existing pixels keep their colors until you remap or export.").arg(path));
	});
}

bool TextureEditorDialog::savePaletteToPath(const QString& path, QString* error) const
{
	if (!m_palette.palette.isValid()) { if (error) { *error = QCoreApplication::translate("VibeStudioTextureEditor", "There is no 256-color palette to save."); } return false; }
	// The file dialog has confirmed any replacement; publication is guarded.
	TextureOutputTarget target;
	return inspectTextureOutputTarget(path, true, &target, error) && writeTextureOutput(target, encodeRawTexturePalette(m_palette.palette.colors), false, error);
}

void TextureEditorDialog::analyzeColors()
{
	const auto palette = m_palette.palette; auto stats = std::make_shared<QPair<int, qint64>>();
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Analyzing colors…"), [palette, stats](TextureDocument& document, QString* error, const TextureProgress& progress) {
		stats->first = textureDistinctColors(document.image());
		stats->second = palette.isValid() ? textureOffPalettePixels(document.image(), palette.colors, progress) : 0;
		if (stats->second < 0) { if (error) { *error = QCoreApplication::translate("VibeStudioTextureEditor", "Texture operation cancelled."); } return false; }
		return true;
	}, [this, stats]() {
		const QLocale locale;
		const QString distinct = stats->first > 65536 ? QCoreApplication::translate("VibeStudioTextureEditor", "more than 65,536") : locale.toString(stats->first);
		m_colorStats->setText(QCoreApplication::translate("VibeStudioTextureEditor", "Distinct colors: %1 · Pixels outside %2: %3").arg(distinct, m_palette.palette.displayName.isEmpty() ? m_palette.palette.id : m_palette.palette.displayName, locale.toString(stats->second)));
		setStatus(stats->second ? QCoreApplication::translate("VibeStudioTextureEditor", "Some pixels are outside the palette. Remap to Palette, or review the export preview.") :
			QCoreApplication::translate("VibeStudioTextureEditor", "Every visible pixel uses a palette color."));
	});
}

QString TextureEditorDialog::recoveryPath() const { return textureRecoveryPath(m_recoveryDirectory, m_recoveryId); }
bool TextureEditorDialog::recoveryBusy() const { return m_recovery && m_recovery->busy(); }

void TextureEditorDialog::retireRecovery()
{
	if (m_recoveryStatus) { m_recoveryStatus->clear(); m_recoveryStatus->setToolTip({}); }
	if (m_recovery) { m_recovery->retire(m_recoveryId); }
	m_recoveryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void TextureEditorDialog::checkpointRecovery()
{
	// Wait for a gesture or foreground operation to commit; never split a stroke
	// or serialize a partially edited layer under its previous revision number.
	if (m_busy || m_document.strokeActive() || m_closeApproved || !m_recovery) { return; }
	if (!hasUnsavedChanges()) { retireRecovery(); return; }
	if (!m_recoveryEnabled->isChecked()) { return; }
	TextureRecoverySnapshot snapshot{m_document.storageSnapshot(), projectMetadata(), m_projectIdentity, QFileInfo(m_source).fileName()};
	if (snapshot.displayName.isEmpty()) { snapshot.displayName = QCoreApplication::translate("VibeStudioTextureEditor", "Untitled texture"); }
	if (m_recovery->checkpoint(m_recoveryId, std::move(snapshot))) {
		m_recoveryStatus->setText(QCoreApplication::translate("VibeStudioTextureEditor", "Updating local checkpoint…")); m_recoveryProgressTimer->start();
	}
}

void TextureEditorDialog::refreshRecoveries()
{
	if (m_busy) { return; }
	auto records = std::make_shared<TextureRecoveryList>();
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Inspecting texture checkpoints…"), [directory = m_recoveryDirectory, records](TextureDocument&, QString* error, const TextureProgress& progress) {
		*records = listTextureRecoveries(directory, progress);
		if (!records->error.isEmpty()) { *error = records->error; return false; } return !records->cancelled;
	}, [this, records]() {
		m_recoveryScanned = true; m_recoveryList->clear();
		for (const auto& record : records->records) {
			const QString name = record.displayName.isEmpty() ? QFileInfo(record.path).fileName() : record.displayName;
			auto* item = new QListWidgetItem(record.isValid() ? QCoreApplication::translate("VibeStudioTextureEditor", "%1\n%2\n%3 × %4 · Layers: %5").arg(name, record.writtenUtc.toLocalTime().toString(Qt::ISODate)).arg(record.size.width()).arg(record.size.height()).arg(record.layerCount) :
				QCoreApplication::translate("VibeStudioTextureEditor", "%1\nUnavailable: %2").arg(name, record.error), m_recoveryList);
			item->setData(Qt::UserRole, record.path); item->setData(Qt::UserRole + 1, record.recordSha256); item->setData(Qt::UserRole + 2, record.isValid()); item->setData(Qt::AccessibleTextRole, item->text());
			item->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Checkpoint: %1\nRecorded source: %2").arg(record.path, record.sourcePath));
		}
		setStatus(records->truncated ? QCoreApplication::translate("VibeStudioTextureEditor", "Recovery scan is bounded to 8,192 directory entries and 256 files. Use Open Recovery File for an unlisted checkpoint.") :
			QCoreApplication::translate("VibeStudioTextureEditor", "Recovery checkpoints found: %1. Restore creates an unsaved draft; save it as a new project.").arg(records->records.size()));
	});
}

void TextureEditorDialog::restoreRecovery(const QString& path, const QByteArray& expectedRecordSha256)
{
	if (m_busy) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Wait for the current operation to finish.")); return; }
	const QFileInfo selected(path), current(recoveryPath());
	const bool ownCheckpoint = selected == current || (!selected.canonicalFilePath().isEmpty() && QFileInfo(selected.canonicalFilePath()) == QFileInfo(current.canonicalFilePath()));
	if (ownCheckpoint && recoveryBusy()) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Wait for the local checkpoint write to finish, then restore again.")); return; }
	// Detach our selected checkpoint before prompting: choosing Save must not
	// retire the very file about to be restored. It stays available on Cancel.
	if (ownCheckpoint) { m_recoveryId = QUuid::createUuid().toString(QUuid::WithoutBraces); }
	if (!confirmDiscard([this, path, expectedRecordSha256]() { restoreRecovery(path, expectedRecordSha256); })) { return; }
	struct Restored { QJsonObject metadata; IdTechPaletteResolution palette; bool hasPalette = false; };
	auto restored = std::make_shared<Restored>();
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Restoring texture checkpoint…"), [path, expectedRecordSha256, restored](TextureDocument& document, QString* error, const TextureProgress& progress) {
		if (!restoreTextureRecovery(path, &document, &restored->metadata, nullptr, error, expectedRecordSha256, progress)) { return false; }
		restored->hasPalette = restored->metadata.contains(QStringLiteral("palette"));
		return !restored->hasPalette || texturePaletteFromMetadata(restored->metadata.value(QStringLiteral("palette")).toObject(), &restored->palette, error);
	}, [this, restored]() {
		// Restoration never adopts the checkpoint's recorded source identity.
		retireRecovery();
		m_projectIdentity = {}; m_savedPath.clear(); m_extraMetadata = restored->metadata;
		loadExportSettings(restored->metadata);
		m_recoveredDraft = true;
		m_source = QCoreApplication::translate("VibeStudioTextureEditor", "Recovered texture"); m_replace->setChecked(false);
		if (restored->hasPalette) { setPaletteResolution(restored->palette); }
		m_virtualPath->setText(restored->metadata.value(QStringLiteral("packageTexturePath")).toString(QStringLiteral("textures/custom/recovered.png")));
		m_savedMetadata = {}; m_document.markUnsaved(); m_canvas->fit(); checkpointRecovery();
		setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Checkpoint restored as an unsaved draft. Save Project As chooses a new destination. The original project and selected checkpoint are unchanged."));
	});
}

bool TextureEditorDialog::requestClose(std::function<void()> afterDeferredSave)
{
	return approveClose(std::move(afterDeferredSave)) && close();
}

bool TextureEditorDialog::approveClose(std::function<void()> afterDeferredSave)
{
	if (m_closeApproved) { return true; }
	if (confirmDiscard([this, afterDeferredSave]() {
		// Recheck in case another studio surface changed metadata during Save.
		if (requestClose(afterDeferredSave) && afterDeferredSave) { afterDeferredSave(); }
	})) {
		m_closeApproved = true; retireRecovery(); return true;
	}
	return false;
}

void TextureEditorDialog::closeEvent(QCloseEvent* event)
{
	if (approveClose()) { event->accept(); } else { event->ignore(); }
}
void TextureEditorDialog::reject() { close(); }
void TextureEditorDialog::updateInspectorWidth()
{
	if (!m_properties) { return; }
	auto* properties = m_properties->findChild<QTabWidget*>(QStringLiteral("textureProperties"));
	if (!properties) { return; }
	int minimum = 300;
	for (auto* scroll : properties->findChildren<QScrollArea*>()) {
		if (!scroll->widget()) { continue; }
		scroll->widget()->layout()->invalidate();
		minimum = std::max(minimum, scroll->widget()->minimumSizeHint().width() + scroll->verticalScrollBar()->sizeHint().width() + 2 * scroll->frameWidth() + 8);
	}
	properties->setMinimumWidth(minimum);
}
void TextureEditorDialog::changeEvent(QEvent* event)
{
	QDialog::changeEvent(event);
	if (event->type() == QEvent::ActivationChange && isActiveWindow()) { refreshContext(); }
	if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
		// Inherited fonts/style propagate to children after this event. Size the
		// inspector from the completed control layouts, including translated text.
		QTimer::singleShot(0, this, [this]() { updateInspectorWidth(); });
	}
}

} // namespace vibestudio
