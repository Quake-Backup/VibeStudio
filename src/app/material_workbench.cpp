#include "app/material_workbench.h"

#include "app/code_editor.h"
#include "app/material_graph_view.h"
#include "app/material_library_model.h"
#include "app/material_preview_view.h"
#include "app/material_property_panel.h"
#include "app/material_script_highlighter.h"
#include "app/material_tasks.h"
#include "app/studio_icons.h"
#include "app/studio_layout.h"
#include "app/studio_progress.h"
#include "app/studio_theme.h"
#include "app/syntax_highlight.h"
#include "app/ui_primitives.h"
#include "core/material_eval.h"
#include "core/material_override.h"
#include "core/material_render.h"
#include "core/material_script.h"
#include "core/material_video.h"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextDocumentLayout>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace slopstudio {

namespace {

constexpr int kReparseDelayMs = 250;
constexpr int kStateSaveDelayMs = 600;
constexpr int kFilterDelayMs = 150;
constexpr int kWholeProblemLimit = 2000;
// An exported sheet keeps within this many pixels a side, and each of its
// frames (or a single exported frame) within the frame limit.
constexpr int kSheetSideLimit = 8192;
constexpr int kFrameSideLimit = 2048;
constexpr int kSingleFrameSideLimit = 4096;
// The tab order of the editors.
constexpr int kNodesTab = 0;
constexpr int kTextTab = 1;
constexpr int kImagesTab = 2;
constexpr int kProblemsTab = 3;
constexpr int kDetailsTab = 4;
constexpr int kMapsTab = 5;

MaterialDiagnostic diagnosticOf(MaterialDiagnosticSeverity severity, const QString& code, const QString& message)
{
	MaterialDiagnostic diagnostic;
	diagnostic.severity = severity;
	diagnostic.code = code;
	diagnostic.message = message;
	return diagnostic;
}

QString severityName(MaterialDiagnosticSeverity severity)
{
	switch (severity) {
	case MaterialDiagnosticSeverity::Error:
		return MaterialWorkbench::tr("Error");
	case MaterialDiagnosticSeverity::Warning:
		return MaterialWorkbench::tr("Warning");
	case MaterialDiagnosticSeverity::Info:
		return MaterialWorkbench::tr("Note");
	}
	return {};
}

QString imageStatusName(const QString& status)
{
	if (status == QStringLiteral("image")) {
		return MaterialWorkbench::tr("Found");
	}
	if (status == QStringLiteral("composite")) {
		return MaterialWorkbench::tr("Composed from patches");
	}
	if (status == QStringLiteral("builtin")) {
		return MaterialWorkbench::tr("Built into the engine");
	}
	if (status == QStringLiteral("generated")) {
		return MaterialWorkbench::tr("Generated stand-in");
	}
	if (status == QStringLiteral("program")) {
		return MaterialWorkbench::tr("Image program");
	}
	if (status == QStringLiteral("video")) {
		return MaterialWorkbench::tr("Video");
	}
	if (status == QStringLiteral("render-target")) {
		return MaterialWorkbench::tr("Render target");
	}
	if (status == QStringLiteral("unreadable")) {
		return MaterialWorkbench::tr("Unreadable");
	}
	if (status == QStringLiteral("missing")) {
		return MaterialWorkbench::tr("Missing");
	}
	return status;
}

// The frames of an export and the size of each, kept within the limits.
struct PreviewSheetPlan {
	int count = 1;
	int columns = 1;
	int rows = 1;
	QSize cell;
	bool shrunk = false;
};

PreviewSheetPlan planPreviewSheet(int frames, QSize cell)
{
	PreviewSheetPlan plan;
	plan.count = std::clamp(frames, 1, 256);
	plan.columns = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(plan.count))));
	plan.rows = (plan.count + plan.columns - 1) / plan.columns;
	if (!cell.isValid() || cell.isEmpty()) {
		cell = QSize(512, 512);
	}
	const int limit = plan.count == 1 ? kSingleFrameSideLimit : std::min(kFrameSideLimit, kSheetSideLimit / std::max(plan.columns, plan.rows));
	if (cell.width() > limit || cell.height() > limit) {
		cell = cell.scaled(limit, limit, Qt::KeepAspectRatio);
		plan.shrunk = true;
	}
	plan.cell = QSize(std::max(1, cell.width()), std::max(1, cell.height()));
	return plan;
}

struct PreviewSheetResult {
	QImage image;
	QString problem;
	bool cancelled = false;
};

// Draws an export's frames, on any thread; `progress` hears each frame.
PreviewSheetResult drawPreviewSheet(const MaterialDefinition& definition, const MaterialImageSet& images, const MaterialTableSet& tables,
	MaterialRenderOptions options, const PreviewSheetPlan& plan, double cycle, double now, const std::function<bool()>& cancelled,
	const std::function<void(int done, int total)>& progress)
{
	PreviewSheetResult sheet;
	options.size = plan.cell;
	for (int frame = 0; frame < plan.count; ++frame) {
		if (cancelled && cancelled()) {
			sheet.cancelled = true;
			return sheet;
		}
		options.time = plan.count == 1 ? now : cycle * frame / plan.count;
		const MaterialRenderResult result = renderMaterial(definition, images, tables, options, cancelled);
		if (result.cancelled) {
			sheet.cancelled = true;
			return sheet;
		}
		if (result.image.isNull()) {
			sheet.problem = result.error.isEmpty() ? MaterialWorkbench::tr("The preview could not be drawn.") : result.error;
			return sheet;
		}
		if (plan.count == 1) {
			sheet.image = result.image;
		} else {
			if (sheet.image.isNull()) {
				const QSize size(result.image.width() * plan.columns, result.image.height() * plan.rows);
				sheet.image = QImage(size, QImage::Format_ARGB32_Premultiplied);
				if (sheet.image.isNull()) {
					sheet.problem = MaterialWorkbench::tr("There is not enough memory for a %1 x %2 pixel sheet; ask for fewer frames or a smaller size.")
										.arg(size.width())
										.arg(size.height());
					return sheet;
				}
				sheet.image.fill(Qt::transparent);
			}
			QPainter painter(&sheet.image);
			painter.drawImage((frame % plan.columns) * result.image.width(), (frame / plan.columns) * result.image.height(), result.image);
		}
		if (progress) {
			progress(frame + 1, plan.count);
		}
	}
	return sheet;
}

} // namespace

MaterialWorkbench::MaterialWorkbench(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("materialWorkbench"));
	m_cache = std::make_shared<MaterialImageCache>(256LL * 1024 * 1024);
	m_library = std::make_shared<MaterialLibrary>();
	m_scanLane = new MaterialTaskLane(this);
	m_imageLane = new MaterialTaskLane(this);
	m_usageLane = new MaterialTaskLane(this);
	m_exportLane = new MaterialTaskLane(this);
	m_filterTimer = new QTimer(this);
	m_filterTimer->setSingleShot(true);
	m_filterTimer->setInterval(kFilterDelayMs);
	m_stateTimer = new QTimer(this);
	m_stateTimer->setSingleShot(true);
	m_stateTimer->setInterval(kStateSaveDelayMs);
	connect(m_stateTimer, &QTimer::timeout, this, [this]() {
		if (m_host.saveState) {
			m_host.saveState(saveState());
		}
	});
	m_reparseTimer = new QTimer(this);
	m_reparseTimer->setSingleShot(true);
	m_reparseTimer->setInterval(kReparseDelayMs);
	connect(m_reparseTimer, &QTimer::timeout, this, [this]() { reparse(false); });
	m_visibleTimer = new QTimer(this);
	m_visibleTimer->setSingleShot(true);
	m_visibleTimer->setInterval(120);
	connect(m_visibleTimer, &QTimer::timeout, this, &MaterialWorkbench::updateVisibleRows);
	buildUi();
	retranslate();
	applyTheme();
	refreshDocumentState();
}

MaterialWorkbench::~MaterialWorkbench()
{
	// Background work publishes into members; stop it before they go.
	delete m_scanLane;
	m_scanLane = nullptr;
	delete m_imageLane;
	m_imageLane = nullptr;
	delete m_usageLane;
	m_usageLane = nullptr;
	delete m_exportLane;
	m_exportLane = nullptr;
	finishUsageTracking();
	if (m_stateTimer->isActive() && m_host.saveState) {
		m_host.saveState(saveState());
	}
}

void MaterialWorkbench::detachHost()
{
	if (m_stateTimer->isActive() && m_host.saveState) {
		m_stateTimer->stop();
		m_host.saveState(saveState());
	}
	m_host = {};
}

void MaterialWorkbench::setHost(MaterialWorkbenchHost host)
{
	m_host = std::move(host);
	m_graph->setImageEditing(static_cast<bool>(m_host.editImage));
	if (m_host.loadState) {
		restoreState(m_host.loadState());
	}
	refreshDocumentState();
}

// --- Layout ------------------------------------------------------------------

void MaterialWorkbench::buildUi()
{
	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(6);
	m_state = new LoadingPane(this);
	m_state->setObjectName(QStringLiteral("materialLibraryState"));
	m_state->setVisible(false);
	root->addWidget(m_state);

	m_mainSplitter = createSplitter(Qt::Horizontal, QStringLiteral("materialsSplitter"), tr("Materials layout"));
	m_mainSplitter->addWidget(buildLibraryPanel());
	m_editorSplitter = createSplitter(Qt::Vertical, QStringLiteral("materialsEditorSplitter"), tr("Material preview and editors"));
	m_editorSplitter->addWidget(buildPreviewPanel());
	m_editorSplitter->addWidget(buildEditorTabs());
	m_editorSplitter->setStretchFactor(0, 3);
	m_editorSplitter->setStretchFactor(1, 4);
	m_mainSplitter->addWidget(m_editorSplitter);
	m_mainSplitter->setStretchFactor(0, 2);
	m_mainSplitter->setStretchFactor(1, 5);
	root->addWidget(m_mainSplitter, 1);
	for (QSplitter* splitter : {m_mainSplitter, m_editorSplitter, m_graphSplitter}) {
		connect(splitter, &QSplitter::splitterMoved, this, &MaterialWorkbench::scheduleStateSave);
	}

	// Ctrl+S does what the Save button does; Ctrl+Alt+S saves every text.
	auto* saveShortcut = new QShortcut(QKeySequence::Save, this);
	saveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	connect(saveShortcut, &QShortcut::activated, this, &MaterialWorkbench::saveCurrent);
	auto* saveAllShortcut = new QShortcut(QKeySequence(Qt::ControlModifier | Qt::AltModifier | Qt::Key_S), this);
	saveAllShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	connect(saveAllShortcut, &QShortcut::activated, this, [this]() {
		if (hasUnsavedChanges()) {
			saveAll();
		}
	});
	// Undo and redo reach the text from the graph and the properties too:
	// both edit the same document.
	for (QWidget* scope : {static_cast<QWidget*>(m_graph), static_cast<QWidget*>(m_properties)}) {
		auto* undo = new QShortcut(QKeySequence::Undo, scope);
		undo->setContext(Qt::WidgetWithChildrenShortcut);
		connect(undo, &QShortcut::activated, m_editor, &QPlainTextEdit::undo);
		auto* redo = new QShortcut(QKeySequence::Redo, scope);
		redo->setContext(Qt::WidgetWithChildrenShortcut);
		connect(redo, &QShortcut::activated, m_editor, &QPlainTextEdit::redo);
	}
}

QWidget* MaterialWorkbench::buildLibraryPanel()
{
	auto* panel = new QWidget(this);
	panel->setObjectName(QStringLiteral("materialLibraryPanel"));
	auto* layout = new QVBoxLayout(panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(6);

	m_filter = new QLineEdit(panel);
	m_filter->setObjectName(QStringLiteral("materialFilter"));
	m_filter->setClearButtonEnabled(true);
	m_filter->addAction(studioIcon(QStringLiteral("search"), StudioIconTone::Muted), QLineEdit::LeadingPosition);
	clearOnEscape(m_filter);
	layout->addWidget(m_filter);

	auto* row = new QHBoxLayout;
	row->setSpacing(4);
	m_engineFilter = new QComboBox(panel);
	m_engineFilter->setObjectName(QStringLiteral("materialEngineFilter"));
	m_viewMode = createToolButton(QStringLiteral("list"), QString(), QString(), false);
	m_viewMode->setObjectName(QStringLiteral("materialViewMode"));
	m_viewMode->setCheckable(true);
	m_animateSwatches = createToolButton(QStringLiteral("film"), QString(), QString(), false);
	m_animateSwatches->setObjectName(QStringLiteral("materialAnimateSwatches"));
	m_animateSwatches->setCheckable(true);
	m_libraryOptions = createToolButton(QStringLiteral("settings"), QString(), QString(), false);
	m_libraryOptions->setObjectName(QStringLiteral("materialLibraryOptions"));
	m_libraryMenu = new QMenu(m_libraryOptions);
	m_libraryMenu->setToolTipsVisible(true);
	m_libraryOptions->setMenu(m_libraryMenu);
	m_libraryOptions->setPopupMode(QToolButton::InstantPopup);
	connect(m_libraryMenu, &QMenu::aboutToShow, this, &MaterialWorkbench::rebuildLibraryMenu);
	row->addWidget(m_engineFilter, 1);
	row->addWidget(m_viewMode);
	row->addWidget(m_animateSwatches);
	row->addWidget(m_libraryOptions);
	layout->addLayout(row);

	m_model = new MaterialLibraryModel(this);
	m_model->setThumbnailSide(m_swatchSide);
	m_proxy = new MaterialLibraryFilter(this);
	m_proxy->setSourceModel(m_model);
	m_libraryView = new QListView(panel);
	m_libraryView->setObjectName(QStringLiteral("materialLibrary"));
	m_libraryView->setModel(m_proxy);
	m_libraryView->setViewMode(QListView::IconMode);
	m_libraryView->setResizeMode(QListView::Adjust);
	m_libraryView->setMovement(QListView::Static);
	m_libraryView->setUniformItemSizes(true);
	m_libraryView->setWrapping(true);
	// One line, elided in the middle, keeps both ends of a name in view.
	m_libraryView->setWordWrap(false);
	m_libraryView->setTextElideMode(Qt::ElideMiddle);
	m_libraryView->setIconSize(QSize(m_swatchSide, m_swatchSide));
	m_libraryView->setGridSize(libraryGridSize());
	m_libraryView->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_libraryView, &QWidget::customContextMenuRequested, this, &MaterialWorkbench::showLibraryContextMenu);
	m_model->setItemSize(libraryGridSize() - QSize(6, 6));
	m_libraryView->setSelectionMode(QAbstractItemView::SingleSelection);
	m_libraryView->setSpacing(4);
	m_libraryView->setMinimumWidth(180);
	layout->addWidget(m_libraryView, 1);

	m_libraryCount = new QLabel(panel);
	m_libraryCount->setObjectName(QStringLiteral("materialLibraryCount"));
	m_libraryCount->setForegroundRole(QPalette::PlaceholderText);
	m_libraryCount->setWordWrap(true);
	layout->addWidget(m_libraryCount);

	// Typing filters after a short pause, not on every key: a big library
	// refilters in one go once the word is there.
	// The page's own changes to the field apply at once (flushFilter()).
	connect(m_filter, &QLineEdit::textChanged, this, [this]() { m_filterTimer->start(); });
	connect(m_filterTimer, &QTimer::timeout, this, &MaterialWorkbench::flushFilter);
	connect(m_engineFilter, &QComboBox::activated, this, [this](int) {
		const auto engine = static_cast<MaterialEngine>(m_engineFilter->currentData().toInt());
		applyFilter([this, engine]() { m_proxy->setEngine(engine); });
	});
	connect(m_viewMode, &QToolButton::toggled, this, [this](bool list) {
		m_libraryView->setViewMode(list ? QListView::ListMode : QListView::IconMode);
		m_libraryView->setIconSize(list ? QSize(28, 28) : QSize(m_swatchSide, m_swatchSide));
		m_libraryView->setGridSize(list ? QSize() : libraryGridSize());
		m_model->setItemSize(list ? QSize() : libraryGridSize() - QSize(6, 6));
		m_libraryView->setWrapping(!list);
		refreshViewModeButton();
		m_visibleTimer->start();
		scheduleStateSave();
	});
	connect(m_animateSwatches, &QToolButton::toggled, this, [this](bool animate) {
		m_model->setAnimateThumbnails(animate && !m_reducedMotion);
		updateVisibleRows();
		scheduleStateSave();
	});
	connect(m_libraryView->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& current) {
		// A refilter that hides the current row moves the current index; that
		// is not the user choosing another material.
		if (current.isValid() && !m_refiltering) {
			selectEntry(m_proxy->mapToSource(current).row());
		}
	});
	// Enter moves to the preview (see eventFilter); a click, which some
	// styles also report as activation, leaves focus in the library.
	m_libraryView->installEventFilter(this);
	m_libraryView->viewport()->installEventFilter(this);
	connect(m_libraryView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() { m_visibleTimer->start(); });
	return panel;
}

QWidget* MaterialWorkbench::buildPreviewPanel()
{
	auto* panel = new QWidget(this);
	panel->setObjectName(QStringLiteral("materialPreviewPanel"));
	auto* layout = new QVBoxLayout(panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);

	m_materialTitle = new QLabel(panel);
	m_materialTitle->setObjectName(QStringLiteral("materialTitle"));
	QFont titleFont = m_materialTitle->font();
	titleFont.setBold(true);
	m_materialTitle->setFont(titleFont);
	m_materialTitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_materialTitle->setWordWrap(true);
	layout->addWidget(m_materialTitle);

	auto* controls = new QHBoxLayout;
	controls->setSpacing(4);
	m_play = createToolButton(QStringLiteral("play"), QString(), QString(), false);
	m_play->setObjectName(QStringLiteral("materialPlay"));
	m_restart = createToolButton(QStringLiteral("repeat"), QString(), QString(), false);
	m_restart->setObjectName(QStringLiteral("materialRestart"));
	m_speed = new QComboBox(panel);
	m_speed->setObjectName(QStringLiteral("materialSpeed"));
	for (const double speed : {0.25, 0.5, 1.0, 2.0, 4.0}) {
		m_speed->addItem(QString(), speed);
	}
	m_speed->setCurrentIndex(2);
	m_timeline = new QSlider(Qt::Horizontal, panel);
	m_timeline->setObjectName(QStringLiteral("materialTimeline"));
	m_timeline->setRange(0, 1000);
	m_timeline->setPageStep(100);
	m_timeLabel = new QLabel(panel);
	m_timeLabel->setObjectName(QStringLiteral("materialTime"));
	m_timeLabel->setAlignment(Qt::AlignVCenter | Qt::AlignTrailing);
	m_shape = new QComboBox(panel);
	m_shape->setObjectName(QStringLiteral("materialShape"));
	for (MaterialPreviewShape shape : materialPreviewShapes()) {
		m_shape->addItem(QString(), static_cast<int>(shape));
	}
	m_lighting = createToolButton(QStringLiteral("settings"), QString(), QString(), true);
	m_lighting->setObjectName(QStringLiteral("materialEngineView"));
	m_lightingMenu = new QMenu(m_lighting);
	m_lighting->setMenu(m_lightingMenu);
	m_lighting->setPopupMode(QToolButton::InstantPopup);
	// Built as it opens, so its checks match choices made elsewhere (Preview
	// Settings, Shift+drag of the light).
	connect(m_lightingMenu, &QMenu::aboutToShow, this, &MaterialWorkbench::rebuildLightingMenu);
	controls->addWidget(m_play);
	controls->addWidget(m_restart);
	controls->addWidget(m_speed);
	controls->addWidget(m_timeline, 1);
	controls->addWidget(m_timeLabel);
	controls->addWidget(m_shape);
	controls->addWidget(m_lighting);
	layout->addLayout(controls);

	m_preview = new MaterialPreviewView(panel);
	layout->addWidget(m_preview, 1);
	// While a new material's images are read, and while an export draws, the
	// preview says so rather than show the last material under a new name.
	m_imagesBusy = new BusyOverlay(m_preview);
	m_imagesBusy->setObjectName(QStringLiteral("materialImagesBusy"));
	m_exportBusy = new BusyOverlay(m_preview);
	m_exportBusy->setObjectName(QStringLiteral("materialExportBusy"));
	m_exportBusy->setCancellable(true);
	connect(m_exportBusy, &BusyOverlay::cancelRequested, this, [this]() {
		m_exportLane->cancel();
		m_exportBusy->finish();
		Q_EMIT statusMessage(tr("The export was cancelled."));
	});

	m_notes = new QLabel(panel);
	m_notes->setObjectName(QStringLiteral("materialNotes"));
	m_notes->setWordWrap(true);
	m_notes->setForegroundRole(QPalette::PlaceholderText);
	m_notes->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(m_notes);

	connect(m_play, &QToolButton::clicked, this, [this]() { m_preview->setPlaying(!m_preview->isPlaying()); });
	connect(m_restart, &QToolButton::clicked, this, [this]() { m_preview->setTime(0.0); });
	connect(m_speed, &QComboBox::activated, this, [this](int) { m_preview->setSpeed(m_speed->currentData().toDouble()); });
	connect(m_timeline, &QSlider::valueChanged, this, [this](int value) {
		if (m_timeline->isSliderDown() || m_timeline->hasFocus()) {
			m_preview->setTime(value / 1000.0 * cycleSeconds());
		}
	});
	connect(m_shape, &QComboBox::activated, this, [this](int) {
		m_shapeChosen = true;
		m_preview->setShape(static_cast<MaterialPreviewShape>(m_shape->currentData().toInt()));
	});
	connect(m_preview, &MaterialPreviewView::playingChanged, this, [this](bool) {
		refreshPlayButton();
		// Paused, the timeline says where it stopped at once.
		m_timelineThrottle.invalidate();
		updateTimeline();
	});
	connect(m_preview, &MaterialPreviewView::timeChanged, this, [this](double) { updateTimeline(); });
	connect(m_preview, &MaterialPreviewView::frameRendered, this, &MaterialWorkbench::updateNotes);
	return panel;
}

QWidget* MaterialWorkbench::buildEditorTabs()
{
	m_tabs = createPanelTabs(tr("Material editors"));
	m_tabs->setObjectName(QStringLiteral("materialEditorTabs"));

	// Nodes.
	auto* nodes = new QWidget(m_tabs);
	nodes->setObjectName(QStringLiteral("materialNodesTab"));
	auto* nodesLayout = new QVBoxLayout(nodes);
	nodesLayout->setContentsMargins(0, 4, 0, 0);
	nodesLayout->setSpacing(4);
	auto* nodeBar = new QHBoxLayout;
	m_addNode = createToolButton(QStringLiteral("add"), QString(), QString(), true);
	m_addNode->setObjectName(QStringLiteral("materialAddNode"));
	m_addNodeMenu = new QMenu(m_addNode);
	m_addNodeMenu->setToolTipsVisible(true);
	m_addNode->setMenu(m_addNodeMenu);
	m_addNode->setPopupMode(QToolButton::InstantPopup);
	auto* fit = createToolButton(QStringLiteral("fit"), QString(), QString(), false);
	fit->setObjectName(QStringLiteral("materialFitGraph"));
	m_newMaterial = createToolButton(QStringLiteral("plus"), QString(), QString(), true);
	m_newMaterial->setObjectName(QStringLiteral("materialNew"));
	m_save = createToolButton(QStringLiteral("save"), QString(), QString(), true);
	m_save->setObjectName(QStringLiteral("materialSave"));
	m_revert = createToolButton(QStringLiteral("undo"), QString(), QString(), false);
	m_revert->setObjectName(QStringLiteral("materialRevert"));
	m_more = createToolButton(QStringLiteral("chevron-down"), QString(), QString(), true);
	m_more->setObjectName(QStringLiteral("materialMore"));
	m_moreMenu = new QMenu(m_more);
	m_moreMenu->setToolTipsVisible(true);
	m_more->setMenu(m_moreMenu);
	m_more->setPopupMode(QToolButton::InstantPopup);
	connect(m_moreMenu, &QMenu::aboutToShow, this, &MaterialWorkbench::rebuildMoreMenu);
	nodeBar->addWidget(m_addNode);
	nodeBar->addWidget(fit);
	nodeBar->addStretch(1);
	nodesLayout->addLayout(nodeBar);
	// The document's actions belong to the document, not to one editor:
	// they sit beside the tabs, whichever tab is open (mirrored right to left).
	auto* documentBar = new QWidget(m_tabs);
	documentBar->setObjectName(QStringLiteral("materialDocumentActions"));
	auto* documentLayout = new QHBoxLayout(documentBar);
	documentLayout->setContentsMargins(0, 0, 0, 0);
	documentLayout->setSpacing(4);
	documentLayout->addWidget(m_newMaterial);
	documentLayout->addWidget(m_revert);
	documentLayout->addWidget(m_save);
	documentLayout->addWidget(m_more);
	m_tabs->setCornerWidget(documentBar, Qt::TopRightCorner);
	auto* graphSplitter = createSplitter(Qt::Horizontal, QStringLiteral("materialGraphSplitter"), tr("Node graph and properties"));
	m_graphSplitter = graphSplitter;
	m_graph = new MaterialGraphView(graphSplitter);
	m_properties = new MaterialPropertyPanel(graphSplitter);
	graphSplitter->addWidget(m_graph);
	graphSplitter->addWidget(m_properties);
	graphSplitter->setStretchFactor(0, 3);
	graphSplitter->setStretchFactor(1, 1);
	nodesLayout->addWidget(graphSplitter, 1);

	// Text.
	auto* text = new QWidget(m_tabs);
	text->setObjectName(QStringLiteral("materialTextTab"));
	auto* textLayout = new QVBoxLayout(text);
	textLayout->setContentsMargins(0, 4, 0, 0);
	textLayout->setSpacing(4);
	m_documentLabel = new QLabel(text);
	m_documentLabel->setObjectName(QStringLiteral("materialDocument"));
	m_documentLabel->setForegroundRole(QPalette::PlaceholderText);
	m_documentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_documentLabel->setWordWrap(true);
	m_editor = new StudioCodeEditor(text);
	m_editor->setObjectName(QStringLiteral("materialEditor"));
	m_editor->setBaseFont(studioMonospaceFont());
	m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
	m_highlighter = new MaterialScriptHighlighter(m_editor->document());
	m_findBar = new CodeFindBar(m_editor, text);
	m_findBar->setVisible(false);
	// The Code page finds its own bar by these names; this one is the
	// Materials page's.
	for (QObject* child : m_findBar->findChildren<QObject*>()) {
		if (child->objectName().startsWith(QStringLiteral("code"))) {
			child->setObjectName(QStringLiteral("material") + child->objectName().mid(4));
		}
	}
	m_findBar->setObjectName(QStringLiteral("materialFindBar"));
	textLayout->addWidget(m_documentLabel);
	textLayout->addWidget(m_findBar);
	textLayout->addWidget(m_editor, 1);
	auto* find = new QShortcut(QKeySequence::Find, text);
	find->setContext(Qt::WidgetWithChildrenShortcut);
	connect(find, &QShortcut::activated, this, [this]() { m_findBar->activate(); });
	auto* replace = new QShortcut(QKeySequence::Replace, text);
	replace->setContext(Qt::WidgetWithChildrenShortcut);
	connect(replace, &QShortcut::activated, this, [this]() { m_findBar->activateReplace(); });
	auto* complete = new QShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_Space), m_editor);
	complete->setContext(Qt::WidgetShortcut);
	connect(complete, &QShortcut::activated, this, [this]() {
		if (!m_editor->showLocalCompletions()) {
			Q_EMIT statusMessage(tr("No keyword or image starts with what is typed here."));
		}
	});
	// Keywords of the material's engine (and Quake III game), then the
	// package's images.
	m_editor->completionsFor = [this](const QString& prefix) {
		QStringList names;
		const MaterialEngine engine = m_document.engine;
		for (const QString& keyword : materialGlobalKeywords(engine, scriptDialect(engine)) + materialStageKeywords(engine, scriptDialect(engine))) {
			if (keyword.startsWith(prefix, Qt::CaseInsensitive) && !names.contains(keyword)) {
				names << keyword;
			}
		}
		for (const QString& image : packageImages()) {
			QString stem = image;
			const QString suffix = QFileInfo(image).suffix();
			if (!suffix.isEmpty()) {
				stem.chop(suffix.size() + 1);
			}
			if ((stem.startsWith(prefix, Qt::CaseInsensitive) || QFileInfo(stem).fileName().startsWith(prefix, Qt::CaseInsensitive))
				&& names.size() < 400) {
				names << stem;
			}
		}
		return names;
	};

	// Images.
	m_imageList = new QTreeWidget(m_tabs);
	m_imageList->setObjectName(QStringLiteral("materialImages"));
	m_imageList->setColumnCount(4);
	m_imageList->setRootIsDecorated(false);
	m_imageList->setUniformRowHeights(true);
	m_imageList->setIconSize(QSize(32, 32));
	m_imageList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_imageList->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_imageList, &QWidget::customContextMenuRequested, this, &MaterialWorkbench::showImagesContextMenu);

	// Problems, of this material or of the whole package.
	auto* problemsPage = new QWidget(m_tabs);
	problemsPage->setObjectName(QStringLiteral("materialProblemsTab"));
	auto* problemsLayout = new QVBoxLayout(problemsPage);
	problemsLayout->setContentsMargins(0, 4, 0, 0);
	problemsLayout->setSpacing(4);
	auto* problemsBar = new QHBoxLayout;
	m_problemScope = createToolButton(QStringLiteral("diagnostics"), QString(), QString(), false);
	m_problemScope->setObjectName(QStringLiteral("materialProblemScope"));
	m_problemScope->setCheckable(true);
	problemsBar->addWidget(m_problemScope);
	problemsBar->addStretch(1);
	problemsLayout->addLayout(problemsBar);
	m_problems = new QTreeWidget(problemsPage);
	m_problems->setObjectName(QStringLiteral("materialProblems"));
	m_problems->setColumnCount(3);
	m_problems->setRootIsDecorated(false);
	m_problems->setUniformRowHeights(true);
	m_problems->header()->setStretchLastSection(true);
	problemsLayout->addWidget(m_problems, 1);
	connect(m_problemScope, &QToolButton::toggled, this, &MaterialWorkbench::setWholePackageProblems);

	// Maps that use the material.
	m_mapsTab = new QWidget(m_tabs);
	m_mapsTab->setObjectName(QStringLiteral("materialMapsTab"));
	auto* mapsLayout = new QVBoxLayout(m_mapsTab);
	mapsLayout->setContentsMargins(0, 4, 0, 0);
	mapsLayout->setSpacing(4);
	auto* mapsBar = new QHBoxLayout;
	m_mapsSummary = new QLabel(m_mapsTab);
	m_mapsSummary->setObjectName(QStringLiteral("materialMapsSummary"));
	m_mapsSummary->setWordWrap(true);
	m_mapsSummary->setForegroundRole(QPalette::PlaceholderText);
	m_mapsRescan = createToolButton(QStringLiteral("refresh"), QString(), QString(), false);
	m_mapsRescan->setObjectName(QStringLiteral("materialMapsRescan"));
	m_mapsBusy = new BusyIndicator(m_mapsTab);
	m_mapsBusy->setObjectName(QStringLiteral("materialMapsBusy"));
	m_mapsBusy->setActive(false);
	mapsBar->addWidget(m_mapsBusy);
	mapsBar->addWidget(m_mapsSummary, 1);
	mapsBar->addWidget(m_mapsRescan);
	mapsLayout->addLayout(mapsBar);
	m_mapList = new QTreeWidget(m_mapsTab);
	m_mapList->setObjectName(QStringLiteral("materialMaps"));
	m_mapList->setColumnCount(3);
	m_mapList->setRootIsDecorated(false);
	m_mapList->setUniformRowHeights(true);
	m_mapList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	mapsLayout->addWidget(m_mapList, 1);
	connect(m_mapsRescan, &QToolButton::clicked, this, [this]() {
		m_usageStale = true;
		scanUsage();
	});

	// Details.
	m_details = new QTextEdit(m_tabs);
	m_details->setObjectName(QStringLiteral("materialDetails"));
	m_details->setReadOnly(true);
	applyMonospaceContentFont(m_details);

	m_tabs->addTab(nodes, studioIcon(QStringLiteral("shaders")), QString());
	m_tabs->addTab(text, studioIcon(QStringLiteral("code")), QString());
	m_tabs->addTab(m_imageList, studioIcon(QStringLiteral("image")), QString());
	m_tabs->addTab(problemsPage, studioIcon(QStringLiteral("diagnostics")), QString());
	m_tabs->addTab(m_details, studioIcon(QStringLiteral("details")), QString());
	m_tabs->addTab(m_mapsTab, studioIcon(QStringLiteral("map")), QString());
	connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
		if (index == kMapsTab && (m_usageStale || !m_usage) && !m_usageScanning) {
			scanUsage();
		}
		// Details are worked out only while they show.
		if (index == kDetailsTab) {
			updateDetails();
		}
		scheduleStateSave();
	});

	connect(fit, &QToolButton::clicked, m_graph, &MaterialGraphView::fitToView);
	connect(m_newMaterial, &QToolButton::clicked, this, &MaterialWorkbench::showNewMaterialDialog);
	connect(m_save, &QToolButton::clicked, this, &MaterialWorkbench::saveCurrent);
	connect(m_revert, &QToolButton::clicked, this, &MaterialWorkbench::revert);
	connect(m_graph, &MaterialGraphView::selectionChanged, this, [this](const QString& id) {
		m_properties->setNode(m_graph->graph().node(id), m_graph->isReadOnly());
	});
	connect(m_graph, &MaterialGraphView::editRequested, this, [this](const MaterialGraphEdit& edit) { applyGraphEdit(edit); });
	connect(m_graph, &MaterialGraphView::nodeActivated, this, [this](const QString&) { m_properties->focusFirstField(); });
	connect(m_graph, &MaterialGraphView::showTextRequested, this, [this, text](int line) {
		m_tabs->setCurrentWidget(text);
		showLine(line);
	});
	connect(m_graph, &MaterialGraphView::showImageRequested, this, [this](const QString& reference) {
		if (m_host.showTexture) {
			m_host.showTexture(reference);
		}
	});
	connect(m_graph, &MaterialGraphView::editImageRequested, this, [this](const QString& reference) {
		const MaterialTexturePtr texture = m_images ? m_images->find(reference) : MaterialTexturePtr();
		if (m_host.editImage) {
			m_host.editImage(texture && !texture->path.isEmpty() ? texture->path : reference);
		}
	});
	connect(m_properties, &MaterialPropertyPanel::propertyEdited, this, [this](const QString& node, const QString& property, const QString& value) {
		MaterialGraphEdit edit;
		edit.kind = MaterialGraphEditKind::SetProperty;
		edit.node = node;
		edit.property = property;
		edit.value = value;
		applyGraphEdit(edit);
	});
	connect(m_properties, &MaterialPropertyPanel::showImageRequested, this, [this](const QString& reference) {
		if (m_host.showTexture) {
			m_host.showTexture(reference);
		}
	});
	connect(m_editor, &QPlainTextEdit::textChanged, this, [this]() {
		// Highlighting changes formats and fires textChanged too, but leaves
		// the document's revision alone; only a change to the text itself
		// needs parsing. Undo and redo move the revision as well.
		const int revision = m_editor->document()->revision();
		if (m_applyingText || revision == m_textRevision) {
			return;
		}
		m_textRevision = revision;
		// Typing, Undo or Redo between two property edits keeps them apart.
		if (!m_applyingPropertyEdit) {
			m_lastPropertyEdit.clear();
		}
		scheduleReparse();
		refreshDocumentState();
	});
	connect(m_problems, &QTreeWidget::itemActivated, this, [this, text](QTreeWidgetItem* item) {
		if (!item) {
			return;
		}
		const int line = item->data(0, Qt::UserRole).toInt();
		// A whole-package problem names its material: go there first.
		const QString material = item->data(1, Qt::UserRole).toString();
		if (!material.isEmpty() && materialLookupKey(material) != materialLookupKey(m_materialName)) {
			const int entry = item->data(2, Qt::UserRole).toInt();
			const QModelIndex proxyIndex = m_proxy->mapFromSource(m_model->index(entry));
			if (proxyIndex.isValid()) {
				m_libraryView->setCurrentIndex(proxyIndex);
				m_libraryView->scrollTo(proxyIndex);
			}
			selectEntry(entry);
		}
		if (line > 0) {
			m_tabs->setCurrentWidget(text);
			showLine(line);
		}
	});
	connect(m_mapList, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
		if (!item || !m_usage) {
			return;
		}
		const QVector<MaterialMapUse> uses = m_usage->usesOf(m_materialName);
		const int index = item->data(0, Qt::UserRole).toInt();
		if (index < 0 || index >= uses.size()) {
			return;
		}
		const MaterialMapUse& use = uses.at(index);
		QString error;
		if (use.kind == MaterialMapKind::Bsp) {
			if (m_host.revealInPackage) {
				m_host.revealInPackage(use.mapPath);
				Q_EMIT statusMessage(tr("Compiled maps do not open in Levels; %1 is shown on the Packages page.").arg(use.mapPath));
			}
		} else if (m_host.openMapUse && !m_host.openMapUse(use, m_materialName, &error)) {
			Q_EMIT statusMessage(error);
		}
	});
	connect(m_imageList, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
		if (item && m_host.showTexture) {
			m_host.showTexture(item->data(0, Qt::UserRole).toString());
		}
	});
	return m_tabs;
}

void MaterialWorkbench::retranslate()
{
	setAccessibleName(tr("Materials workbench"));
	m_state->setAccessibleName(tr("Material library state"));
	m_filter->setPlaceholderText(tr("Filter materials"));
	m_filter->setAccessibleName(tr("Material filter"));
	m_filter->setToolTip(tr("Words search names. key=value, key:text, key<n and key>n test engine, kind, source, stages, images, frames, blend, "
							"sort, cull, errors and warnings; animated, sky, light, fog, translucent, rejected and shadowed take yes or no; "
							"surface parameters and flags too: animated=yes, engine=doom3, surfaceparm=nolightmap."));
	m_engineFilter->setAccessibleName(tr("Engine"));
	m_engineFilter->setToolTip(tr("Show the materials of one engine."));
	refreshViewModeButton();
	m_animateSwatches->setText(tr("Animate Swatches"));
	m_animateSwatches->setToolTip(tr("Play animated materials in the library while they are on screen."));
	m_animateSwatches->setAccessibleName(tr("Animate swatches"));
	m_libraryOptions->setText(tr("Library Options"));
	m_libraryOptions->setToolTip(tr("Sort, swatch size, base game materials and the Quake III game the scripts are read for."));
	m_libraryOptions->setAccessibleName(tr("Library options"));
	m_libraryView->setAccessibleName(tr("Material library"));
	m_libraryView->setAccessibleDescription(tr("Every texture, shader and material in the open package. Arrow keys choose one; "
											   "Enter moves to its preview."));
	m_materialTitle->setAccessibleName(tr("Material"));
	refreshPlayButton();
	m_restart->setText(tr("Restart"));
	m_restart->setToolTip(tr("Go back to time zero."));
	m_restart->setAccessibleName(tr("Restart animation"));
	const QStringList speeds {tr("0.25x"), tr("0.5x"), tr("1x"), tr("2x"), tr("4x")};
	for (int index = 0; index < m_speed->count() && index < speeds.size(); ++index) {
		m_speed->setItemText(index, speeds.at(index));
	}
	m_speed->setAccessibleName(tr("Playback speed"));
	m_speed->setToolTip(tr("How fast time runs in the preview."));
	m_timeline->setAccessibleName(tr("Time"));
	m_timeline->setToolTip(tr("Scrub through the material's animation."));
	for (int index = 0; index < m_shape->count(); ++index) {
		m_shape->setItemText(index, materialPreviewShapeDisplayName(static_cast<MaterialPreviewShape>(m_shape->itemData(index).toInt())));
	}
	m_shape->setAccessibleName(tr("Preview shape"));
	m_shape->setToolTip(tr("The surface the material is drawn on."));
	m_lighting->setText(tr("Engine View"));
	m_lighting->setToolTip(tr("Lighting and drawing choices of the material's engine: overbright, sector light, light styles, renderer, "
							  "the Doom 3 light."));
	m_lighting->setAccessibleName(tr("Engine view options"));
	m_notes->setAccessibleName(tr("Preview notes"));
	m_tabs->setAccessibleName(tr("Material editors"));
	refreshTabTexts();
	if (auto* fit = findChild<QToolButton*>(QStringLiteral("materialFitGraph"))) {
		fit->setText(tr("Fit Graph"));
		fit->setToolTip(tr("Fit the whole graph in view (Ctrl+0 in the graph)."));
		fit->setAccessibleName(tr("Fit graph"));
	}
	m_addNode->setText(tr("Add Node"));
	m_addNode->setToolTip(tr("Add a node to the material or to the selected node's stage."));
	m_addNode->setAccessibleName(tr("Add node"));
	m_newMaterial->setText(tr("New Material"));
	m_newMaterial->setToolTip(tr("Start a material from a template."));
	m_newMaterial->setAccessibleName(tr("New material"));
	m_save->setText(tr("Save"));
	m_save->setAccessibleName(tr("Save material text"));
	m_revert->setText(tr("Revert"));
	m_revert->setToolTip(tr("Go back to the saved text; Undo brings the edits back."));
	m_revert->setAccessibleName(tr("Revert to saved"));
	m_more->setText(tr("More"));
	m_more->setToolTip(tr("Save all, export, copy, rename, duplicate, delete, override in the package and open in Code."));
	m_more->setAccessibleName(tr("More material actions"));
	m_findBar->setAccessibleName(tr("Find in material text"));
	m_editor->setAccessibleName(tr("Material text"));
	m_editor->setAccessibleDescription(tr("The script the graph shows. Every change updates the preview and the graph."));
	m_imageList->setHeaderLabels({tr("Image"), tr("Status"), tr("Size"), tr("Read from")});
	m_imageList->setAccessibleName(tr("Material images"));
	m_imageList->setAccessibleDescription(tr("Every image the material reads and how the engine finds it. Enter shows one on the Textures page."));
	m_problems->setHeaderLabels({tr("Severity"), tr("Line"), tr("Problem")});
	m_problems->setAccessibleName(tr("Material problems"));
	m_problems->setAccessibleDescription(tr("What the engine would warn about or reject. Enter goes to the line."));
	m_problemScope->setText(tr("Whole Package"));
	m_problemScope->setToolTip(tr("List the problems of every material in the package, not only this one."));
	m_problemScope->setAccessibleName(tr("Problems of the whole package"));
	m_mapList->setHeaderLabels({tr("Map"), tr("Uses"), tr("Where")});
	m_mapList->setAccessibleName(tr("Maps using the material"));
	m_mapList->setAccessibleDescription(tr("Every map in the package that uses this material and how often. Enter opens a map in Levels "
											"with its uses selected."));
	m_mapsRescan->setText(tr("Read Maps Again"));
	m_mapsRescan->setToolTip(tr("Read the package's maps again for material uses."));
	m_mapsRescan->setAccessibleName(tr("Read maps again"));
	m_mapsSummary->setAccessibleName(tr("Map use summary"));
	m_details->setAccessibleName(tr("Material details"));
	refreshTimeLabelWidth();
	updateLibraryCount();
	refreshDocumentState();
}

void MaterialWorkbench::refreshTimeLabelWidth()
{
	// Wide enough for the longest time, so the controls do not shift as the
	// digits change; measured again when the text size or language changes.
	m_timeLabel->setMinimumWidth(m_timeLabel->fontMetrics().horizontalAdvance(tr("%1 s").arg(88888.8, 0, 'f', 1)));
}

void MaterialWorkbench::refreshPlayButton()
{
	const bool playing = m_preview->isPlaying();
	m_play->setIcon(studioIcon(playing ? QStringLiteral("pause") : QStringLiteral("play")));
	m_play->setText(playing ? tr("Pause") : tr("Play"));
	m_play->setToolTip(playing ? tr("Pause the animation (Space in the preview).") : tr("Play the animation in real time (Space in the preview)."));
	m_play->setAccessibleName(playing ? tr("Pause animation") : tr("Play animation"));
}

void MaterialWorkbench::refreshViewModeButton()
{
	const bool list = m_viewMode->isChecked();
	m_viewMode->setIcon(studioIcon(list ? QStringLiteral("grid") : QStringLiteral("list")));
	m_viewMode->setText(list ? tr("Show as Grid") : tr("Show as List"));
	m_viewMode->setToolTip(list ? tr("Show swatches in a grid.") : tr("Show materials as a list."));
	m_viewMode->setAccessibleName(tr("List view"));
}

void MaterialWorkbench::refreshTabTexts()
{
	setPanelTabText(m_tabs, kNodesTab, tr("Nodes"));
	setPanelTabText(m_tabs, kTextTab, tr("Text"));
	setPanelTabText(m_tabs, kImagesTab, m_imageCount > 0 ? tr("Images (%1)").arg(m_imageCount) : tr("Images"));
	setPanelTabText(m_tabs, kProblemsTab, m_problemCount > 0 ? tr("Problems (%1)").arg(m_problemCount) : tr("Problems"));
	setPanelTabText(m_tabs, kDetailsTab, tr("Details"));
	const int uses = m_usage ? m_usage->useCount(m_materialName) : -1;
	setPanelTabText(m_tabs, kMapsTab, uses > 0 ? tr("Maps (%1)").arg(uses) : tr("Maps"));
}

void MaterialWorkbench::applyTheme()
{
	const StudioThemeTokens& tokens = currentStudioTheme();
	const StudioSyntaxTheme theme = studioSyntaxTheme(tokens.light, tokens.highContrast);
	highlightQuietly([&]() { m_highlighter->setTheme(theme); });
	// Buffered texts show in the new colours when they come back.
	for (QTextDocument* buffered : std::as_const(m_buffers)) {
		if (MaterialScriptHighlighter* highlighter = highlighterOf(buffered)) {
			highlighter->setTheme(theme);
		}
	}
}

void MaterialWorkbench::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);
	switch (event->type()) {
	case QEvent::LanguageChange:
		retranslate();
		rebuildLightingMenu();
		updateGraph();
		updateDetails();
		updateMapsTab();
		break;
	case QEvent::PaletteChange:
	case QEvent::StyleChange:
	case QEvent::FontChange:
		applyTheme();
		if (!m_viewMode->isChecked()) {
			m_libraryView->setGridSize(libraryGridSize());
			m_model->setItemSize(libraryGridSize() - QSize(6, 6));
		}
		if (event->type() == QEvent::FontChange) {
			refreshTimeLabelWidth();
		}
		// Swatch backgrounds and badges are painted in the theme's colours.
		if (event->type() == QEvent::PaletteChange) {
			m_model->refreshThumbnails();
		}
		break;
	case QEvent::DevicePixelRatioChange:
		m_model->setDevicePixelRatio(m_libraryView->devicePixelRatioF());
		break;
	default:
		break;
	}
}

QSize MaterialWorkbench::libraryGridSize() const
{
	// Room for the swatch and a line of name, wider when text is larger.
	const QFontMetrics metrics(m_libraryView->font());
	return {std::max(m_swatchSide + 36, metrics.averageCharWidth() * 14), m_swatchSide + metrics.height() + 14};
}

// --- Sources -----------------------------------------------------------------

void MaterialWorkbench::setPackage(std::shared_ptr<const PackageArchiveReader> reader, const QString& path, const QString& revision)
{
	// A new snapshot of the same revision holds the same files.
	if (!revision.isEmpty() && revision == m_revision && path == m_packagePath) {
		return;
	}
	m_reader = std::move(reader);
	m_packagePath = path;
	m_revision = revision;
	m_pathIndex.clear();
	m_pathIndexReader = nullptr;
	m_usageStale = true;
	requestScan();
}

void MaterialWorkbench::clearPackage()
{
	if (!m_reader && m_packagePath.isEmpty()) {
		return;
	}
	m_reader.reset();
	m_packagePath.clear();
	m_revision.clear();
	m_pathIndex.clear();
	m_pathIndexReader = nullptr;
	m_usage.reset();
	m_usageStale = true;
	requestScan();
}

void MaterialWorkbench::setBasePackages(const QStringList& paths)
{
	if (paths == m_basePaths) {
		return;
	}
	m_basePaths = paths;
	m_usageStale = true;
	requestScan();
}

void MaterialWorkbench::setDialect(MaterialDialect dialect)
{
	if (dialect == m_dialectChoice) {
		return;
	}
	m_dialectChoice = dialect;
	scheduleStateSave();
	requestScan();
	Q_EMIT dialectChanged();
}

MaterialDialect MaterialWorkbench::dialect() const
{
	return m_library ? m_library->dialect : MaterialDialect::Quake3;
}

void MaterialWorkbench::setShowBaseMaterials(bool show)
{
	if (show == m_showBase) {
		return;
	}
	m_showBase = show;
	scheduleStateSave();
	requestScan();
}

MaterialDialect MaterialWorkbench::scriptDialect(MaterialEngine engine) const
{
	if (engine != MaterialEngine::Quake3) {
		return MaterialDialect::Automatic;
	}
	// The library's dialect covers the package's scripts; a loose script with
	// no package says its own.
	const bool packageShaders = m_library && std::any_of(m_library->scripts.cbegin(), m_library->scripts.cend(),
		[](const MaterialScript& script) { return script.engine == MaterialEngine::Quake3; });
	if (m_dialectChoice != MaterialDialect::Automatic) {
		return m_dialectChoice;
	}
	return packageShaders ? m_library->dialect : MaterialDialect::Automatic;
}

bool MaterialWorkbench::openScript(const QString& path, QString* error)
{
	const QFileInfo info(path);
	if (!info.isFile() || !isMaterialScriptPath(path)) {
		if (error) {
			*error = tr("%1 is not a .shader or .mtr script.").arg(QDir::toNativeSeparators(path));
		}
		return false;
	}
	m_looseFile = info.absoluteFilePath();
	m_materialName.clear();
	requestScan();
	return true;
}

void MaterialWorkbench::requestScan()
{
	// A big package takes a while to read; only read it once it is seen.
	if (isVisible()) {
		startScan();
	} else {
		m_scanPending = true;
		notifyContentChanged();
	}
}

void MaterialWorkbench::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	if (m_scanPending) {
		startScan();
	}
}

void MaterialWorkbench::setReducedMotion(bool reduced)
{
	m_reducedMotion = reduced;
	m_preview->setReducedMotion(reduced);
	m_model->setAnimateThumbnails(m_animateSwatches->isChecked() && !reduced);
	m_state->setReducedMotion(reduced);
	m_imagesBusy->setReducedMotion(reduced);
	m_exportBusy->setReducedMotion(reduced);
	m_mapsBusy->setReducedMotion(reduced);
}

void MaterialWorkbench::startScan()
{
	m_scanPending = false;
	m_scanning = true;
	m_state->setVisible(true);
	m_state->setTitle(tr("Reading materials"));
	m_state->setDetail(!m_packagePath.isEmpty() ? QDir::toNativeSeparators(m_packagePath) : QDir::toNativeSeparators(m_looseFile));
	m_state->setState(OperationState::Running, tr("Scanning"));
	m_state->setProgress({});
	const std::shared_ptr<const PackageArchiveReader> reader = m_reader;
	const QString looseFile = m_looseFile;
	const QString select = m_materialName;
	const QStringList basePaths = m_basePaths;
	QHash<QString, std::shared_ptr<const PackageArchiveReader>> knownBases = m_baseReaders;
	// Base packages the studio has read already (for Levels) are used as they
	// are rather than read and hashed a second time.
	if (m_host.baseReader) {
		for (const QString& path : basePaths) {
			const QFileInfo info(path);
			const QString key = info.absoluteFilePath() + QLatin1Char('|') + QString::number(info.lastModified().toMSecsSinceEpoch());
			if (!knownBases.contains(key)) {
				if (std::shared_ptr<const PackageArchiveReader> shared = m_host.baseReader(info.absoluteFilePath())) {
					knownBases.insert(key, std::move(shared));
				}
			}
		}
	}
	MaterialLibraryOptions options;
	options.dialect = m_dialectChoice;
	options.listBaseMaterials = m_showBase;
	QPointer<MaterialWorkbench> self(this);
	m_scanLane->submit([this, reader, looseFile, select, self, basePaths, knownBases, options](
						   const MaterialTaskLane::Cancelled& cancelled) mutable -> std::function<void()> {
		auto library = std::make_shared<MaterialLibrary>();
		const auto report = [self](const QString& phase, int done, int total) {
			QMetaObject::invokeMethod(
				QCoreApplication::instance(),
				[self, phase, done, total]() {
					if (self && self->m_scanning) {
						self->m_state->setProgress({done, total});
						self->m_state->setDetail(phase);
					}
				},
				Qt::QueuedConnection);
		};
		// Base packages, read again only when their file changed.
		QHash<QString, std::shared_ptr<const PackageArchiveReader>> bases;
		QStringList baseWarnings;
		for (const QString& path : basePaths) {
			const QFileInfo info(path);
			const QString key = info.absoluteFilePath() + QLatin1Char('|') + QString::number(info.lastModified().toMSecsSinceEpoch());
			std::shared_ptr<const PackageArchiveReader> base = knownBases.value(key);
			if (!base) {
				auto archive = std::make_shared<PackageArchive>();
				QString error;
				// A big base package takes a while: the read can be stopped, and
				// says how far it is.
				const QString phase = tr("Reading base package %1").arg(info.fileName());
				report(phase, 0, 0);
				QElapsedTimer throttle;
				throttle.start();
				PackageReadControl control;
				control.isCancelled = cancelled;
				control.progress = [report, phase, throttle](const QString&, qint64 completed, qint64 total) mutable {
					if (throttle.elapsed() > 150) {
						throttle.restart();
						// Counts may be bytes; the pane shows only how far along.
						const int done = total > 0 ? static_cast<int>(std::clamp<qint64>(completed * 1000 / total, 0, 1000)) : 0;
						report(phase, done, total > 0 ? 1000 : 0);
					}
				};
				if (!archive->load(path, &error, control)) {
					if (cancelled()) {
						return {};
					}
					baseWarnings << tr("The base package %1 could not be read: %2").arg(QDir::toNativeSeparators(path), error);
					continue;
				}
				base = archive;
			}
			bases.insert(key, base);
			options.baseReaders.push_back(base);
			if (cancelled()) {
				return {};
			}
		}
		if (reader) {
			QElapsedTimer throttle;
			throttle.start();
			*library = scanMaterialLibrary(reader, options,
				[cancelled, self, throttle](int done, int total, const QString& phase) mutable {
					if (throttle.elapsed() > 100 || done == total) {
						throttle.restart();
						QMetaObject::invokeMethod(
							QCoreApplication::instance(),
							[self, done, total, phase]() {
								if (self && self->m_scanning) {
									self->m_state->setProgress({done, total});
									self->m_state->setDetail(phase);
								}
							},
							Qt::QueuedConnection);
					}
					return !cancelled();
				});
		}
		library->warnings = baseWarnings + library->warnings;
		int loose = -1;
		if (!looseFile.isEmpty()) {
			MaterialScript script;
			// Read as the package's game reads its scripts.
			const bool packageShaders = std::any_of(library->scripts.cbegin(), library->scripts.cend(),
				[](const MaterialScript& each) { return each.engine == MaterialEngine::Quake3; });
			MaterialDialect dialect = options.dialect;
			if (dialect == MaterialDialect::Automatic && packageShaders) {
				dialect = library->dialect;
			}
			if (loadMaterialScript(looseFile, &script, nullptr, MaterialEngine::Unknown, dialect)) {
				const QString root = reader ? reader->sourcePath() : QString();
				loose = library->adoptScript(script, reader ? materialScriptVirtualPath(looseFile, root, script.engine) : QString());
				if (!packageShaders && script.engine == MaterialEngine::Quake3) {
					library->dialect = script.dialect;
				}
			}
		}
		if (library->sourcePath.isEmpty()) {
			library->sourcePath = reader ? reader->sourcePath() : looseFile;
		}
		if (cancelled()) {
			return {};
		}
		const MaterialLibraryRows rows = prepareMaterialLibraryRows(library);
		return [this, library, rows, loose, select, bases]() {
			m_baseReaders = bases;
			applyLibrary(library, rows, loose, select);
		};
	});
	notifyContentChanged();
}

void MaterialWorkbench::applyLibrary(std::shared_ptr<MaterialLibrary> library, const MaterialLibraryRows& rows, int looseScript, const QString& select)
{
	m_scanning = false;
	m_library = std::move(library);
	m_looseScript = looseScript;
	// The tables every parse reads, made once per library rather than per step.
	m_libraryTables = std::make_shared<const MaterialTableSet>(m_library->tables);
	m_scriptTables.clear();
	m_model->setDevicePixelRatio(m_libraryView->devicePixelRatioF());
	m_model->setRows(rows, imageSource(), m_cache);
	applyLibraryOrder();
	rebuildEngineFilter();
	m_properties->setImageChoices(packageImages());
	const int total = static_cast<int>(m_library->entries.size());
	if (!m_library->warnings.isEmpty()) {
		m_state->setTitle(tr("Materials read with warnings"));
		m_state->setDetail(m_library->warnings.join(QLatin1Char('\n')));
		const int warnings = static_cast<int>(m_library->warnings.size());
		m_state->setState(OperationState::Warning, tr("%n warning(s)", nullptr, warnings));
		m_state->setVisible(true);
	} else {
		m_state->setState(OperationState::Completed, tr("Ready"));
		m_state->setVisible(false);
	}
	updateLibraryCount();
	// A material asked for while reading comes first; then the one in view;
	// then the loose script's first, then the library's first.
	int entry = -1;
	if (!m_requestedName.isEmpty()) {
		const QString requested = m_requestedName;
		const QString requestedKind = m_requestedKind;
		const auto missing = std::move(m_requestedMissing);
		m_requestedName.clear();
		m_requestedKind.clear();
		m_requestedMissing = {};
		// As revealMaterial does: the name, then under textures/, then (for a
		// Doom slot) a picture of the other kind.
		entry = m_library->indexOfKind(requested, MaterialEngine::Unknown, requestedKind);
		if (entry < 0 && !requested.startsWith(QStringLiteral("textures/"), Qt::CaseInsensitive)) {
			entry = m_library->indexOfKind(QStringLiteral("textures/") + requested, MaterialEngine::Unknown, requestedKind);
		}
		if (entry < 0 && !requestedKind.isEmpty()) {
			entry = m_library->indexOf(requested);
		}
		if (entry < 0 && !(missing && missing(requested))) {
			setFilterText(requested);
		}
	}
	if (entry < 0 && !select.isEmpty()) {
		// The open Doom material keeps its kind, so a wall stays the wall.
		entry = m_library->indexOfKind(select, m_engine, select == m_materialName ? m_materialKind : QString());
		if (entry < 0) {
			entry = m_library->indexOf(select, m_engine);
		}
		if (looseScript >= 0) {
			for (int index = 0; index < total; ++index) {
				if (m_library->entries.at(index).scriptIndex == looseScript
					&& materialLookupKey(m_library->entries.at(index).name) == materialLookupKey(select)) {
					entry = index;
					break;
				}
			}
		}
	}
	if (entry < 0 && looseScript >= 0) {
		for (int index = 0; index < total && entry < 0; ++index) {
			if (m_library->entries.at(index).scriptIndex == looseScript) {
				entry = index;
			}
		}
	}
	if (entry < 0 && total > 0) {
		entry = 0;
	}
	// With a filter typed, prefer a material the filter keeps.
	if (entry >= 0 && !m_proxy->mapFromSource(m_model->index(entry)).isValid() && m_proxy->rowCount() > 0) {
		entry = m_proxy->mapToSource(m_proxy->index(0, 0)).row();
	}
	if (entry >= 0) {
		const QModelIndex proxyIndex = m_proxy->mapFromSource(m_model->index(entry));
		if (proxyIndex.isValid()) {
			// Selected once, below: the selection signal does not fire when
			// the row stays current, and need not when it moves.
			m_refiltering = true;
			m_libraryView->setCurrentIndex(proxyIndex);
			m_refiltering = false;
			m_libraryView->scrollTo(proxyIndex);
		}
		selectEntry(entry);
	} else {
		m_materialName.clear();
		m_imageLane->cancel();
		m_imagesBusy->finish();
		m_preview->clearMaterial(m_reader || !m_looseFile.isEmpty() ? tr("This package has no textures, shaders or materials.")
																	: tr("Open a package or a .shader or .mtr script to see its materials."));
		m_graph->clearGraph(tr("No material selected."));
		m_properties->setNode(nullptr, true);
	}
	m_visibleTimer->start();
	if (m_usageStale && (m_tabs->currentIndex() == kMapsTab || m_proxy->order() == MaterialLibraryFilter::Order::MostUsed)) {
		scanUsage();
	} else {
		updateMapsTab();
	}
	if (m_wholeProblems) {
		updateWholeProblems();
	}
	// The detected Quake III game may have changed.
	rebuildLightingMenu();
	notifyContentChanged();
}

void MaterialWorkbench::notifyContentChanged()
{
	// The shell refreshes every page header on this signal: only say so when
	// what it shows changed.
	const bool content = hasContent();
	const QString text = summary();
	if (m_contentKnown && content == m_lastHasContent && text == m_lastSummary) {
		return;
	}
	m_contentKnown = true;
	m_lastHasContent = content;
	m_lastSummary = text;
	Q_EMIT contentChanged();
}

void MaterialWorkbench::applyFilter(const std::function<void()>& change)
{
	// A refilter that hides the open material moves the view's current row;
	// that does not open another material. The open one stays selected when
	// the filter keeps it.
	m_refiltering = true;
	change();
	const int entry = currentEntry();
	const QModelIndex kept = entry >= 0 ? m_proxy->mapFromSource(m_model->index(entry)) : QModelIndex();
	if (kept.isValid()) {
		m_libraryView->setCurrentIndex(kept);
	} else {
		m_libraryView->selectionModel()->clearSelection();
	}
	m_refiltering = false;
	updateLibraryCount();
	m_visibleTimer->start();
}

void MaterialWorkbench::flushFilter()
{
	m_filterTimer->stop();
	const QString text = m_filter->text();
	if (text != m_proxy->queryText()) {
		applyFilter([this, text]() { m_proxy->setQuery(text); });
	}
}

void MaterialWorkbench::setFilterText(const QString& text)
{
	m_filter->setText(text);
	flushFilter();
}

void MaterialWorkbench::applyLibraryOrder()
{
	QHash<int, int> counts;
	if (m_usage && m_library) {
		for (int row = 0; row < m_library->entries.size(); ++row) {
			const int uses = m_usage->useCount(m_library->entries.at(row).name);
			if (uses > 0) {
				counts.insert(row, uses);
			}
		}
	}
	m_proxy->setUseCounts(counts);
	m_proxy->setOrder(m_proxy->order());
}

void MaterialWorkbench::rebuildEngineFilter()
{
	const MaterialEngine current = static_cast<MaterialEngine>(m_engineFilter->currentData().toInt());
	m_engineFilter->clear();
	m_engineFilter->addItem(tr("All engines"), static_cast<int>(MaterialEngine::Unknown));
	for (MaterialEngine engine : m_library->engines()) {
		const int count = m_library->count(engine);
		m_engineFilter->addItem(tr("%1 (%2)").arg(materialEngineDisplayName(engine)).arg(count), static_cast<int>(engine));
	}
	const int keep = m_engineFilter->findData(static_cast<int>(current));
	m_engineFilter->setCurrentIndex(std::max(0, keep));
	m_refiltering = true;
	m_proxy->setEngine(static_cast<MaterialEngine>(m_engineFilter->currentData().toInt()));
	m_refiltering = false;
	m_engineFilter->setVisible(m_engineFilter->count() > 2 || current != MaterialEngine::Unknown);
}

void MaterialWorkbench::updateLibraryCount()
{
	if (!m_libraryCount || !m_proxy) {
		return;
	}
	const int total = m_model->rowCount();
	const int shown = m_proxy->rowCount();
	QString text;
	if (m_scanning) {
		text = tr("Reading materials...");
	} else if (total == 0) {
		text = tr("No materials.");
	} else if (shown == total) {
		text = tr("%n material(s)", nullptr, total);
	} else {
		text = tr("Showing %1 of %n material(s)", nullptr, total).arg(shown);
		const QStringList unknown = m_proxy->unknownKeys();
		if (shown == 0 && !unknown.isEmpty()) {
			text += QLatin1Char(' ') + tr("No material has %1.").arg(unknown.join(QStringLiteral(", ")));
		}
	}
	m_libraryCount->setText(text);
	m_libraryCount->setAccessibleName(text);
}

void MaterialWorkbench::updateVisibleRows()
{
	QVector<int> rows;
	const QRect area = m_libraryView->viewport()->rect();
	QSize step = m_libraryView->gridSize();
	if (!step.isValid() || step.isEmpty()) {
		step = QSize(std::max(24, area.width()), m_libraryView->sizeHintForRow(0) > 0 ? m_libraryView->sizeHintForRow(0) : 24);
	}
	QSet<int> seen;
	for (int y = step.height() / 2; y < area.height() + step.height(); y += std::max(8, step.height())) {
		for (int x = step.width() / 2; x < area.width() + step.width(); x += std::max(8, step.width())) {
			const QModelIndex index = m_libraryView->indexAt(QPoint(std::min(x, area.width() - 1), std::min(y, area.height() - 1)));
			if (index.isValid()) {
				const int row = m_proxy->mapToSource(index).row();
				if (!seen.contains(row)) {
					seen.insert(row);
					rows.push_back(row);
				}
			}
		}
	}
	m_model->setVisibleRows(rows);
}

MaterialImageSource MaterialWorkbench::imageSource() const
{
	MaterialImageSource source;
	// The package over its base packages when it has some.
	source.archive = m_library && m_library->reader ? m_library->reader : m_reader;
	if (!source.archive) {
		for (const auto& base : m_baseReaders) {
			source.fallbacks.push_back(base);
		}
	}
	source.revision = (m_revision.isEmpty() ? m_packagePath + QLatin1Char('|') + m_looseFile : m_revision) + QLatin1Char('|')
		+ m_basePaths.join(QLatin1Char('|')) + QLatin1Char('|') + materialDialectId(dialect());
	// The extensions the game tries for an image name follow its dialect;
	// the library's swatches read with this same source.
	source.extensionRule = materialImageExtensionRule(dialect());
	source.doomCatalog = m_library ? m_library->doomCatalog : nullptr;
	source.developerDefault = m_developerDefault;
	source.quake2Sky = m_skyName;
	source.doomSky = m_skyName;
	source.cache = m_cache.get();
	return source;
}

std::shared_ptr<const PackageArchiveReader> MaterialWorkbench::loadedBasePackage(const QString& path) const
{
	const QFileInfo info(path);
	return m_baseReaders.value(info.absoluteFilePath() + QLatin1Char('|') + QString::number(info.lastModified().toMSecsSinceEpoch()));
}

QStringList MaterialWorkbench::packageImages() const
{
	QStringList images;
	const std::shared_ptr<const PackageArchiveReader> reader = m_library && m_library->reader ? m_library->reader : m_reader;
	if (!reader) {
		return images;
	}
	static const QStringList suffixes {QStringLiteral("tga"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
		QStringLiteral("pcx"), QStringLiteral("bmp"), QStringLiteral("dds"), QStringLiteral("wal")};
	for (const PackageEntry& entry : reader->entries()) {
		if (entry.kind != PackageEntryKind::File) {
			continue;
		}
		const QString suffix = QFileInfo(entry.virtualPath).suffix().toLower();
		if (suffixes.contains(suffix)) {
			images << entry.virtualPath;
		}
	}
	images.sort(Qt::CaseInsensitive);
	return images;
}

bool MaterialWorkbench::readPackageFile(const QString& path, QByteArray* bytes, QString* error) const
{
	if (!m_reader) {
		if (error) {
			*error = tr("No package is open.");
		}
		return false;
	}
	if (m_pathIndexReader != m_reader.get()) {
		m_pathIndex.clear();
		for (const PackageEntry& entry : m_reader->entries()) {
			if (entry.kind == PackageEntryKind::File) {
				m_pathIndex.insert(entry.virtualPath.toLower(), entry.virtualPath);
			}
		}
		m_pathIndexReader = m_reader.get();
	}
	const QString actual = m_pathIndex.value(path.toLower());
	if (actual.isEmpty()) {
		if (error) {
			*error = tr("%1 is not in the package.").arg(path);
		}
		return false;
	}
	return m_reader->readEntryBytes(actual, bytes, error, 16LL * 1024 * 1024);
}

bool MaterialWorkbench::packageIsWad() const
{
	return m_reader && m_reader->format() == PackageArchiveFormat::Wad;
}

QString MaterialWorkbench::freeScriptPath(MaterialEngine engine) const
{
	const QString folder = engine == MaterialEngine::Doom3 ? QStringLiteral("materials/") : QStringLiteral("scripts/");
	const QString suffix = engine == MaterialEngine::Doom3 ? QStringLiteral(".mtr") : QStringLiteral(".shader");
	for (int number = 0; number < 1000; ++number) {
		const QString candidate = folder + QStringLiteral("slopstudio") + (number == 0 ? QString() : QString::number(number)) + suffix;
		bool used = false;
		for (const MaterialScript& script : m_library->scripts) {
			used = used || script.path.compare(candidate, Qt::CaseInsensitive) == 0;
		}
		// A new script not saved yet holds its path too, so a second one never
		// shares it and replaces it on save.
		const QString package = packageIdentity();
		const auto holds = [&](const Document& document) {
			return document.package == package && document.virtualPath.compare(candidate, Qt::CaseInsensitive) == 0;
		};
		used = used || holds(m_document);
		for (const Document& document : std::as_const(m_bufferDocuments)) {
			used = used || holds(document);
		}
		if (!used) {
			return candidate;
		}
	}
	return folder + QStringLiteral("slopstudio") + suffix;
}

// --- Selection and documents -------------------------------------------------

bool MaterialWorkbench::hasContent() const
{
	return m_scanning || m_scanPending || (m_library && !m_library->entries.isEmpty()) || m_document.kind != MaterialTextKind::None;
}

QString MaterialWorkbench::summary() const
{
	if (m_scanning || m_scanPending) {
		return tr("Reading materials...");
	}
	const int count = m_library ? static_cast<int>(m_library->entries.size()) : 0;
	if (count == 0) {
		return tr("No materials");
	}
	QStringList engines;
	for (MaterialEngine engine : m_library->engines()) {
		engines << materialEngineDisplayName(engine);
	}
	QString text = tr("%n material(s)", nullptr, count);
	if (!engines.isEmpty()) {
		text += QStringLiteral(" · ") + engines.join(QStringLiteral(", "));
	}
	if (hasUnsavedChanges()) {
		text += QStringLiteral(" · ") + tr("unsaved changes");
	}
	return text;
}

QString MaterialWorkbench::busyReason() const
{
	if (m_scanning || m_scanPending) {
		return QStringLiteral("scanning");
	}
	if (m_imageLane && m_imageLane->pending()) {
		return QStringLiteral("images");
	}
	if (!m_preview->isPlaying() && m_preview->renderPending()) {
		return QStringLiteral("drawing");
	}
	if (m_reparseTimer->isActive()) {
		return QStringLiteral("parsing");
	}
	if (m_usageScanning) {
		return QStringLiteral("maps");
	}
	if (exportRunning()) {
		return QStringLiteral("exporting");
	}
	return {};
}

bool MaterialWorkbench::isBusy() const
{
	// A playing preview always has a frame coming; that is not being busy.
	return m_scanning || (m_imageLane && m_imageLane->pending()) || (!m_preview->isPlaying() && m_preview->renderPending())
		|| m_reparseTimer->isActive() || m_usageScanning || exportRunning();
}

bool MaterialWorkbench::exportRunning() const
{
	return m_exportLane && m_exportLane->pending();
}

bool MaterialWorkbench::hasUnsavedChanges() const
{
	return m_currentModified || !m_buffers.isEmpty();
}

bool MaterialWorkbench::currentTextModified() const
{
	if (m_document.kind == MaterialTextKind::None || m_document.readOnly) {
		return false;
	}
	// The document knows when it is back at its clean state (undo included),
	// which saves copying a big text to compare.
	if (!m_editor->document()->isModified()) {
		return false;
	}
	return m_editor->toPlainText() != m_document.savedText;
}

QStringList MaterialWorkbench::unsavedTextTitles() const
{
	QStringList titles;
	if (m_currentModified) {
		titles << m_document.title;
	}
	for (auto it = m_bufferDocuments.cbegin(); it != m_bufferDocuments.cend(); ++it) {
		if (m_buffers.contains(it.key())) {
			titles << it.value().title;
		}
	}
	return titles;
}

QString MaterialWorkbench::packageIdentity() const
{
	return m_packagePath.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(m_packagePath).absoluteFilePath()).toLower();
}

QString MaterialWorkbench::scriptKey(const QString& package, const QString& virtualPath) const
{
	return QStringLiteral("script:") + package + QLatin1Char('|') + virtualPath.toLower();
}

MaterialTextKind MaterialWorkbench::currentTextKind() const
{
	return m_document.kind;
}

QString MaterialWorkbench::currentText() const
{
	return m_editor->toPlainText();
}

int MaterialWorkbench::currentEntry() const
{
	return m_library ? m_library->indexOfKind(m_materialName, m_engine, m_materialKind) : -1;
}

bool MaterialWorkbench::selectMaterial(const QString& name, const QString& kind)
{
	if (!m_library) {
		return false;
	}
	const int entry = m_library->indexOfKind(name, MaterialEngine::Unknown, kind);
	if (entry < 0) {
		return false;
	}
	const QModelIndex proxyIndex = m_proxy->mapFromSource(m_model->index(entry));
	if (proxyIndex.isValid()) {
		m_libraryView->setCurrentIndex(proxyIndex);
		m_libraryView->scrollTo(proxyIndex);
	}
	// A Doom wall and flat of one name are different materials.
	const MaterialLibraryEntry& item = m_library->entries.at(entry);
	if (m_materialName != item.name || (item.engine == MaterialEngine::Doom && m_materialKind != item.kind)) {
		selectEntry(entry);
	}
	return true;
}

void MaterialWorkbench::revealMaterial(const QString& name, std::function<bool(const QString&)> missing, const QString& kind)
{
	const QString wanted = name.trimmed();
	if (wanted.isEmpty()) {
		return;
	}
	setFilterText(QString());
	if (m_scanning || m_scanPending) {
		m_requestedName = wanted;
		m_requestedKind = kind;
		m_requestedMissing = std::move(missing);
		return;
	}
	m_requestedMissing = {};
	m_requestedKind.clear();
	if (selectMaterial(wanted, kind)) {
		return;
	}
	// Quake III maps name shaders without their textures/ folder.
	if (!wanted.startsWith(QStringLiteral("textures/"), Qt::CaseInsensitive) && selectMaterial(QStringLiteral("textures/") + wanted, kind)) {
		return;
	}
	// ZDoom draws a flat on a wall (and a wall on a plane) when that is the
	// only picture of the name.
	if (!kind.isEmpty() && selectMaterial(wanted)) {
		return;
	}
	if (missing && missing(wanted)) {
		return;
	}
	setFilterText(wanted);
	Q_EMIT statusMessage(tr("No material is named %1; showing the ones whose names contain it.").arg(wanted));
}

void MaterialWorkbench::showMaterialsUsingImage(const QString& image)
{
	// Materials list their images without extensions where the engine adds
	// one, so the path is matched without its own.
	QString stem = image.trimmed();
	const QString suffix = QFileInfo(stem).suffix();
	if (!suffix.isEmpty()) {
		stem.chop(suffix.size() + 1);
	}
	if (stem.isEmpty()) {
		return;
	}
	const QString term = stem.contains(QLatin1Char(' ')) ? QStringLiteral("image:\"%1\"").arg(stem) : QStringLiteral("image:%1").arg(stem);
	setFilterText(term);
	if (m_proxy->rowCount() > 0) {
		const QModelIndex first = m_proxy->index(0, 0);
		m_libraryView->setCurrentIndex(first);
		m_libraryView->scrollTo(first);
	}
	const int count = m_proxy->rowCount();
	Q_EMIT statusMessage(m_scanning || m_scanPending ? tr("Reading materials; the filter %1 applies when they are ready.").arg(term)
							 : count == 0 ? tr("No material reads %1.").arg(stem)
										  : tr("%n material(s) read %1.", nullptr, count).arg(stem));
}

MaterialWorkbench::Document MaterialWorkbench::documentFor(int entry, const MaterialDefinition& definition) const
{
	Document document;
	const MaterialLibraryEntry& item = m_library->entries.at(entry);
	document.engine = item.engine;
	// Texts are told apart by the package they came from too: the same path
	// in another package is another text.
	const QString package = packageIdentity();
	document.package = package;
	document.packagePath = m_packagePath;
	if (item.scriptIndex >= 0 && item.scriptIndex < m_library->scripts.size()) {
		const MaterialScript& script = m_library->scripts.at(item.scriptIndex);
		document.kind = script.engine == MaterialEngine::Doom3 ? MaterialTextKind::Doom3Material : MaterialTextKind::Quake3Shader;
		document.engine = script.engine;
		document.savedText = script.text;
		document.scriptIndex = item.scriptIndex;
		if (item.scriptIndex == m_looseScript && !m_looseFile.isEmpty()) {
			document.filePath = m_looseFile;
			document.title = QDir::toNativeSeparators(m_looseFile);
			document.package.clear();
			document.packagePath.clear();
			document.key = QStringLiteral("file:") + QDir::cleanPath(m_looseFile).toLower();
			return document;
		}
		document.virtualPath = script.path;
		if (item.base) {
			document.readOnly = true;
			document.title = tr("%1 (base game, read only)").arg(script.path);
			document.key = QStringLiteral("base:") + package + QLatin1Char('|') + script.path.toLower();
		} else {
			document.title = script.path;
			document.key = scriptKey(package, script.path);
		}
		return document;
	}
	if ((item.kind == QStringLiteral("doom-wall") || item.kind == QStringLiteral("doom-flat")) && m_library->doomCatalog) {
		const DoomMaterialCatalog& catalog = *m_library->doomCatalog;
		if (catalog.hasAnimdefs) {
			document.kind = MaterialTextKind::DoomAnimdefs;
			document.savedText = catalog.animdefsText;
			document.virtualPath = catalog.animdefsPath.isEmpty() ? QStringLiteral("ANIMDEFS") : catalog.animdefsPath;
			document.title = document.virtualPath;
			document.key = QStringLiteral("animdefs:") + package + QLatin1Char('|') + document.virtualPath.toLower();
		} else {
			document.kind = MaterialTextKind::DoomSwantbls;
			document.savedText = swantblsText(catalog.ranges, catalog.switches);
			document.virtualPath = QStringLiteral("ANIMATED");
			document.title = tr("Animations and switches (Boom SWANTBLS, saved as ANIMATED and SWITCHES)");
			document.key = QStringLiteral("swantbls:") + package;
		}
		return document;
	}
	if (item.kind == QStringLiteral("quake2-wal")) {
		QByteArray bytes;
		Quake2WalInfo info;
		if (readPackageFile(item.sourcePath, &bytes) && readQuake2WalInfo(bytes, &info)) {
			document.kind = MaterialTextKind::Quake2WalJson;
			document.savedText = quake2WalInfoText(info);
			document.walBytes = bytes;
			document.virtualPath = item.sourcePath;
			document.title = tr("%1 header (ericw-tools .wal_json)").arg(item.sourcePath);
			document.key = QStringLiteral("wal:") + package + QLatin1Char('|') + item.sourcePath.toLower();
			return document;
		}
	}
	document.kind = MaterialTextKind::None;
	document.title = definition.sourcePath.isEmpty() ? definition.name : definition.sourcePath;
	document.key = QStringLiteral("none:") + package + QLatin1Char('|') + materialEngineId(item.engine) + QLatin1Char(':') + definition.name.toLower();
	return document;
}

void MaterialWorkbench::selectEntry(int entry)
{
	if (!m_library || entry < 0 || entry >= m_library->entries.size()) {
		return;
	}
	const MaterialLibraryEntry& item = m_library->entries.at(entry);
	const MaterialDefinition definition = m_library->definition(entry);
	const QString kind = item.engine == MaterialEngine::Doom ? item.kind : QString();
	m_materialChangePending = m_materialChangePending || item.name != m_materialName || item.engine != m_engine || kind != m_materialKind;
	m_materialName = item.name;
	m_engine = item.engine;
	m_materialKind = kind;
	m_baseDefinition = definition;
	const Document document = documentFor(entry, definition);
	bool scrollToDefinition = true;
	if (document.key != m_document.key) {
		// Back to a buffered text at the material last shown in it: the
		// place it was left is where the user was.
		const QString previous = m_bufferDocuments.value(document.key).material;
		if (switchDocument(document) && materialLookupKey(previous) == materialLookupKey(item.name)) {
			scrollToDefinition = false;
		}
	} else {
		// The same document, read again (after a save or a rescan): take the
		// new saved text, and show it if nothing was typed since.
		const bool untouched = !m_currentModified;
		const bool changedUnderneath = document.savedText != m_document.savedText;
		m_document.savedText = document.savedText;
		m_document.walBytes = document.walBytes;
		m_document.scriptIndex = document.scriptIndex;
		m_document.title = document.title;
		m_document.readOnly = document.readOnly;
		m_document.isNew = false;
		const QString text = m_editor->toPlainText();
		if (untouched && text != document.savedText) {
			setEditorText(document.savedText);
		} else if (!untouched && changedUnderneath && text != document.savedText) {
			// Changed in the package (on the Packages or Code page) while being
			// edited here: keep the edits, and say what Save will do.
			m_documentConflict = tr("%1 changed in the package since you started editing it; saving replaces that version with this text, "
									"and Revert goes to it.")
									 .arg(document.title);
			Q_EMIT statusMessage(m_documentConflict);
		}
		if (text == m_document.savedText) {
			m_editor->document()->setModified(false);
		}
		// Read again for the same material: the place in the text stays.
		scrollToDefinition = m_materialChangePending;
	}
	reparse(scrollToDefinition);
	notifyContentChanged();
}

bool MaterialWorkbench::switchDocument(const Document& document)
{
	// Guarded: the editor deletes its own first document when it is replaced.
	const QPointer<QTextDocument> current = m_editor->document();
	m_documentConflict.clear();
	bool keepCurrent = false;
	if (!m_document.key.isEmpty() && m_document.kind != MaterialTextKind::None && currentTextModified()) {
		// The open text waits in a buffer, with its undo history and place.
		Document kept = m_document;
		kept.material = m_materialName;
		kept.materialEngine = m_engine;
		kept.materialKind = m_materialKind;
		const QTextCursor cursor = m_editor->textCursor();
		kept.anchor = cursor.anchor();
		kept.position = cursor.position();
		kept.scroll = m_editor->verticalScrollBar()->value();
		forgetBuffer(m_document.key);
		// Never the editor's own first document, which the editor deletes.
		if (current->parent() != m_editor) {
			current->setParent(m_editor);
		}
		m_buffers.insert(m_document.key, current);
		m_bufferDocuments.insert(m_document.key, kept);
		keepCurrent = true;
	} else if (!m_document.key.isEmpty()) {
		forgetBuffer(m_document.key);
	}
	Document stored = m_bufferDocuments.take(document.key);
	QTextDocument* next = m_buffers.take(document.key);
	const bool restored = next != nullptr;
	m_document = document;
	if (!next) {
		next = createTextDocument(document.kind, document.savedText);
	}
	next->setDefaultFont(m_editor->font());
	m_applyingText = true;
	// The editor's line, bracket and match marks point into the old text;
	// they go first, as that text may go too. The editor draws new ones.
	m_editor->setExtraSelections({});
	m_editor->setDocument(next);
	m_applyingText = false;
	// Tab stops live in the document's text option, so each one takes the
	// editor's.
	m_editor->setTabStopDistance(QFontMetricsF(m_editor->font()).horizontalAdvance(QLatin1Char(' ')) * 4.0);
	m_highlighter = highlighterOf(next);
	m_textRevision = next->revision();
	// A new text is highlighted now, while its change signals are ignored,
	// rather than by the highlighter's own deferred pass.
	if (!restored && m_highlighter) {
		highlightQuietly([this]() { m_highlighter->rehighlight(); });
	}
	// A text nobody keeps goes with its highlighter; the editor's first
	// document belongs to the editor itself.
	if (!keepCurrent && current && current != next && current->parent() == m_editor) {
		current->deleteLater();
	}
	if (restored && stored.position >= 0) {
		const int last = std::max(0, next->characterCount() - 1);
		QTextCursor cursor(next);
		cursor.setPosition(std::clamp(stored.anchor, 0, last));
		cursor.setPosition(std::clamp(stored.position, 0, last), QTextCursor::KeepAnchor);
		m_editor->setTextCursor(cursor);
		m_editor->verticalScrollBar()->setValue(stored.scroll);
	}
	m_editor->setReadOnly(document.kind == MaterialTextKind::None || document.readOnly);
	if (document.kind == MaterialTextKind::None) {
		m_editor->setPlaceholderText(m_engine == MaterialEngine::Quake
				? tr("Quake textures have no material text: the name sets animation (+0 to +9, +a to +j), liquids (*) and skies (sky).")
				: tr("The engine makes this material from the image alone. New Material writes a script for it."));
	} else {
		m_editor->setPlaceholderText(QString());
	}
	refreshDocumentState();
	return restored;
}

QTextDocument* MaterialWorkbench::createTextDocument(MaterialTextKind kind, const QString& text)
{
	// The editor owns the texts it shows; buffered ones wait beside it.
	auto* document = new QTextDocument(m_editor);
	document->setDocumentLayout(new QPlainTextDocumentLayout(document));
	document->setDefaultFont(m_editor->font());
	document->setPlainText(text);
	document->setModified(false);
	document->clearUndoRedoStacks();
	// Highlighted once the text is in, for its kind, rather than twice.
	auto* highlighter = new MaterialScriptHighlighter(nullptr);
	highlighter->setParent(document);
	highlighter->setTextKind(kind);
	const StudioThemeTokens& tokens = currentStudioTheme();
	highlighter->setTheme(studioSyntaxTheme(tokens.light, tokens.highContrast));
	highlighter->setDocument(document);
	return document;
}

void MaterialWorkbench::highlightQuietly(const std::function<void()>& work)
{
	// Highlighting runs in edit blocks: the editor reports a change and the
	// document's revision moves, though the text is the same. That is not an
	// edit to parse again (a parse highlights its problems, so it would never
	// settle).
	const bool applying = m_applyingText;
	m_applyingText = true;
	work();
	m_applyingText = applying;
	m_textRevision = m_editor->document()->revision();
}

MaterialScriptHighlighter* MaterialWorkbench::highlighterOf(QTextDocument* document) const
{
	return document ? document->findChild<MaterialScriptHighlighter*>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
}

void MaterialWorkbench::forgetBuffer(const QString& key)
{
	if (QTextDocument* buffered = m_buffers.take(key); buffered && buffered != m_editor->document()) {
		delete buffered;
	}
	m_bufferDocuments.remove(key);
}

void MaterialWorkbench::setEditorText(const QString& text)
{
	m_applyingText = true;
	m_editor->setPlainText(text);
	m_applyingText = false;
	m_textRevision = m_editor->document()->revision();
	m_editor->document()->setModified(text != m_document.savedText);
}

void MaterialWorkbench::replaceEditorText(const QString& text, bool join)
{
	// One undo step changing only the part that differs, so the cursor and
	// the scroll position stay where they were.
	const QString current = m_editor->toPlainText();
	if (current == text) {
		return;
	}
	int prefix = 0;
	const int limit = static_cast<int>(std::min(current.size(), text.size()));
	while (prefix < limit && current.at(prefix) == text.at(prefix)) {
		++prefix;
	}
	int suffix = 0;
	while (suffix < limit - prefix && current.at(current.size() - 1 - suffix) == text.at(text.size() - 1 - suffix)) {
		++suffix;
	}
	QTextCursor cursor(m_editor->document());
	if (join) {
		cursor.joinPreviousEditBlock();
	} else {
		cursor.beginEditBlock();
	}
	cursor.setPosition(prefix);
	cursor.setPosition(static_cast<int>(current.size()) - suffix, QTextCursor::KeepAnchor);
	cursor.insertText(text.mid(prefix, text.size() - prefix - suffix));
	cursor.endEditBlock();
}

void MaterialWorkbench::scheduleReparse()
{
	m_reparseTimer->start();
}

std::shared_ptr<const MaterialTableSet> MaterialWorkbench::tablesFor(const MaterialScript& script) const
{
	// The edited script's tables first, then the rest of the library's.
	auto tables = std::make_shared<MaterialTableSet>();
	tables->addAll(script.tables);
	if (m_library) {
		for (const QString& name : m_library->tables.names()) {
			if (const MaterialTable* table = m_library->tables.find(name)) {
				tables->add(*table);
			}
		}
	}
	tables->addStandIns();
	return tables;
}

void MaterialWorkbench::reparse(bool scrollToDefinition)
{
	m_reparseTimer->stop();
	const QString text = m_editor->toPlainText();
	QVector<MaterialDiagnostic> diagnostics;
	m_definitionProblem.clear();
	MaterialDefinition definition = m_baseDefinition;
	if (!m_libraryTables) {
		m_libraryTables = std::make_shared<const MaterialTableSet>(m_library ? m_library->tables : MaterialTableSet());
	}
	std::shared_ptr<const MaterialTableSet> tables = m_libraryTables;
	const bool flat = m_baseDefinition.kind == QStringLiteral("doom-flat");
	switch (m_document.kind) {
	case MaterialTextKind::Quake3Shader:
	case MaterialTextKind::Doom3Material: {
		// An unchanged library script was parsed with the library: browsing
		// it reuses that parse instead of reading the whole text again.
		const int index = m_document.scriptIndex;
		const bool unchanged = m_library && index >= 0 && index < m_library->scripts.size() && text == m_document.savedText
			&& m_library->scripts.at(index).text == text;
		MaterialScript parsed;
		if (!unchanged) {
			const QString path = m_document.virtualPath.isEmpty() ? m_document.filePath : m_document.virtualPath;
			parsed = parseMaterialScript(text, m_document.engine, path, scriptDialect(m_document.engine));
			++m_parseCount;
		}
		if (!unchanged) {
			tables = tablesFor(parsed);
			// Doom 3 table lookups against every table the game would see, as
			// the library checks its own scripts: Problems lists the undefined
			// ones (an unchanged library script was checked with the library).
			if (parsed.engine == MaterialEngine::Doom3) {
				const std::shared_ptr<const MaterialTableSet> lookup = tables;
				for (MaterialDefinition& each : parsed.materials) {
					checkMaterialTables(&each, [&lookup](const QString& name) { return lookup->find(name); });
				}
			}
		}
		const MaterialScript& script = unchanged ? m_library->scripts.at(index) : parsed;
		diagnostics = script.allDiagnostics();
		if (const MaterialDefinition* found = script.find(m_materialName)) {
			definition = *found;
		} else if (!m_materialName.isEmpty()) {
			m_definitionProblem = tr("The text no longer defines %1; the preview shows the last version that did.").arg(m_materialName);
			definition = m_definition.name.isEmpty() ? m_baseDefinition : m_definition;
		}
		if (unchanged) {
			auto cached = m_scriptTables.constFind(index);
			if (cached == m_scriptTables.constEnd()) {
				cached = m_scriptTables.insert(index, tablesFor(script));
			}
			tables = cached.value();
		}
		break;
	}
	case MaterialTextKind::DoomSwantbls: {
		QVector<DoomAnimationRange> ranges;
		QVector<DoomSwitchPair> switches;
		parseSwantblsText(text, &ranges, &switches, &diagnostics);
		if (m_library && m_library->doomCatalog) {
			DoomMaterialCatalog catalog = *m_library->doomCatalog;
			catalog.ranges = ranges;
			catalog.switches = switches;
			catalog.animationSource = QStringLiteral("boom-animated");
			catalog.switchSource = QStringLiteral("boom-switches");
			definition = doomMaterialDefinition(catalog, m_materialName, flat);
		}
		break;
	}
	case MaterialTextKind::DoomAnimdefs: {
		const QVector<DoomAnimdefsEntry> entries = parseAnimdefs(text, &diagnostics);
		if (m_library && m_library->doomCatalog) {
			DoomMaterialCatalog catalog = *m_library->doomCatalog;
			catalog.animdefs = entries;
			catalog.hasAnimdefs = true;
			catalog.animdefsText = text;
			definition = doomMaterialDefinition(catalog, m_materialName, flat);
		}
		break;
	}
	case MaterialTextKind::Quake2WalJson: {
		Quake2WalInfo info;
		QString error;
		if (readQuake2WalInfo(m_document.walBytes, &info, &error) && parseQuake2WalInfoText(text, &info, &error)) {
			definition = quake2MaterialDefinition(m_materialName, info, [this](const QString& name, Quake2WalInfo* out) {
				QByteArray bytes;
				return readPackageFile(quake2WalPath(name), &bytes) && readQuake2WalInfo(bytes, out);
			});
		} else {
			MaterialDiagnostic problem = diagnosticOf(MaterialDiagnosticSeverity::Error, QStringLiteral("bad-metadata"), error);
			problem.line = 1;
			diagnostics.push_back(problem);
		}
		break;
	}
	case MaterialTextKind::None:
		break;
	}
	m_definition = definition;
	m_tables = tables;
	highlightQuietly([&]() { m_highlighter->setDiagnostics(diagnostics); });
	updateProblems(diagnostics);
	const bool materialChanged = m_materialChangePending;
	m_materialChangePending = false;
	updateViews(materialChanged, scrollToDefinition);
}

void MaterialWorkbench::updateViews(bool materialChanged, bool scrollToDefinition)
{
	QString title = m_definition.name.isEmpty() ? m_materialName : m_definition.name;
	if (!title.isEmpty()) {
		title = tr("%1 · %2").arg(title, materialEngineDisplayName(m_definition.engine));
	}
	m_materialTitle->setText(title);
	updateGraph();
	resolveImages(materialChanged);
	updateDetails();
	if (materialChanged) {
		rebuildLightingMenu();
		// Preview Settings shows the rows of the material's engine.
		if (m_previewSettings && m_previewSettings->isVisible() && m_definition.engine != m_previewSettingsEngine) {
			buildPreviewSettings(false);
		}
	}
	updateTimeline();
	updateNotes();
	if (scrollToDefinition && m_definition.span.line > 0 && m_document.kind != MaterialTextKind::None) {
		const QTextBlock block = m_editor->document()->findBlockByNumber(m_definition.span.line - 1);
		if (block.isValid()) {
			QTextCursor cursor(block);
			m_editor->setTextCursor(cursor);
			m_editor->centerCursor();
		}
	}
	refreshDocumentState();
}

void MaterialWorkbench::resolveImages(bool materialChanged)
{
	const QStringList references = m_definition.imageReferences();
	const MaterialImageSource source = imageSource();
	// The images in hand serve only when they were read for these references
	// from the package as it is now (a staged or replaced image, or other base
	// packages, read them again), and no newer request is still being read:
	// a pending one for other references would otherwise publish over this.
	const bool pendingOther = m_imageLane->pending() && references != m_requestedImageReferences;
	if (!materialChanged && m_images && references == m_imageReferences && source.revision == m_imagesRevision && !pendingOther) {
		if (!m_imageLane->pending()) {
			m_imagesBusy->finish();
		}
		pushPreview(false);
		return;
	}
	const MaterialDefinition definition = m_definition;
	const std::shared_ptr<MaterialImageCache> cache = m_cache;
	m_requestedImageReferences = references;
	if (materialChanged) {
		// Until its images arrive, the preview says the new material is coming
		// instead of showing the previous one under its name.
		m_imagesBusy->begin(tr("Reading images"), m_definition.name.isEmpty() ? m_materialName : m_definition.name);
	}
	updateNotes();
	const QString revision = source.revision;
	m_imageLane->submit(
		[this, definition, source, cache, references, materialChanged, revision](const MaterialTaskLane::Cancelled& cancelled) -> std::function<void()> {
			auto set = std::make_shared<MaterialImageSet>(resolveMaterialImages(definition, source, cancelled));
			if (set->cancelled) {
				return {};
			}
			QHash<QString, QImage> thumbnails;
			for (const QString& reference : references) {
				if (const MaterialTexturePtr texture = set->find(reference)) {
					if (texture->usable()) {
						thumbnails.insert(reference, texture->toImage().scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
					}
				}
			}
			return [this, set, references, thumbnails, materialChanged, revision]() {
				m_imagesBusy->finish();
				// Read for references the text has moved on from: a newer
				// request follows, or the next parse asks again.
				if (references != m_definition.imageReferences()) {
					if (!m_imageLane->pending()) {
						resolveImages(materialChanged);
					}
					return;
				}
				m_images = set;
				m_imageReferences = references;
				m_imagesRevision = revision;
				m_graph->setThumbnails(thumbnails);
				updateImagesTab();
				pushPreview(materialChanged);
				updateNotes();
			};
		});
}

void MaterialWorkbench::pushPreview(bool materialChanged)
{
	if (m_definition.name.isEmpty() && m_materialName.isEmpty()) {
		return;
	}
	if (materialChanged && !m_shapeChosen) {
		const MaterialPreviewShape shape = defaultMaterialPreviewShape(m_definition);
		m_preview->setShape(shape);
		const int index = m_shape->findData(static_cast<int>(shape));
		if (index >= 0) {
			m_shape->setCurrentIndex(index);
		}
	}
	m_preview->setMaterial(m_definition, m_images, m_tables);
	if (materialChanged) {
		// Animated materials play by themselves unless motion is reduced;
		// still ones cost nothing.
		m_preview->setPlaying(m_definition.isAnimated() && !m_reducedMotion);
	}
}

void MaterialWorkbench::updateGraph()
{
	if (m_definition.name.isEmpty() && m_materialName.isEmpty()) {
		m_graph->clearGraph(tr("No material selected."));
		m_properties->setNode(nullptr, true);
		return;
	}
	const MaterialTextKind kind = m_document.kind == MaterialTextKind::None ? defaultMaterialTextKind(m_definition) : m_document.kind;
	const MaterialGraph graph = buildMaterialGraph(m_definition, kind);
	const bool readOnly = m_document.kind == MaterialTextKind::None;
	const QVector<MaterialGraphNodeTemplate> templates = readOnly ? QVector<MaterialGraphNodeTemplate>() : materialGraphNodeTemplates(m_definition.engine, kind);
	m_graph->setReadOnly(readOnly);
	m_graph->setNodeTemplates(templates);
	m_graph->setGraph(graph, m_materialName);
	m_properties->setNode(m_graph->graph().node(m_graph->selectedNode()), readOnly);
	rebuildAddNodeMenu(templates);
}

void MaterialWorkbench::rebuildAddNodeMenu(const QVector<MaterialGraphNodeTemplate>& templates)
{
	m_addNodeMenu->clear();
	QHash<QString, QMenu*> categories;
	for (const MaterialGraphNodeTemplate& each : templates) {
		QMenu* menu = categories.value(each.category);
		if (!menu) {
			menu = m_addNodeMenu->addMenu(each.category);
			menu->setToolTipsVisible(true);
			categories.insert(each.category, menu);
		}
		QAction* action = menu->addAction(each.title);
		action->setToolTip(each.description);
		const QString id = each.id;
		const bool needsStage = each.needsStage;
		connect(action, &QAction::triggered, this, [this, id, needsStage]() {
			const MaterialGraphNode* node = m_graph->graph().node(m_graph->selectedNode());
			MaterialGraphEdit edit;
			edit.kind = MaterialGraphEditKind::AddNode;
			edit.nodeTemplate = id;
			if (needsStage) {
				if (!node || node->stage < 0) {
					Q_EMIT statusMessage(tr("Select a stage, or a node inside one, first."));
					return;
				}
				edit.node = QStringLiteral("stage/%1").arg(node->stage);
			} else {
				edit.node = QStringLiteral("material");
			}
			applyGraphEdit(edit);
		});
	}
	m_addNode->setEnabled(!templates.isEmpty());
}

void MaterialWorkbench::updateProblems(const QVector<MaterialDiagnostic>& diagnostics)
{
	// The whole package's list stays while the text changes; the underlines
	// in the text still follow every edit.
	if (m_wholeProblems) {
		return;
	}
	m_problems->clear();
	int count = 0;
	QVector<MaterialDiagnostic> all = diagnostics;
	if (!m_definition.engineRejection.isEmpty()) {
		bool explained = false;
		for (const MaterialDiagnostic& diagnostic : diagnostics) {
			explained = explained || diagnostic.message == m_definition.engineRejection;
		}
		if (!explained) {
			MaterialDiagnostic rejection = diagnosticOf(MaterialDiagnosticSeverity::Error, QStringLiteral("engine-rejects"), m_definition.engineRejection);
			rejection.line = m_definition.span.line;
			all.push_back(rejection);
		}
	}
	if (!m_definitionProblem.isEmpty()) {
		all.push_back(diagnosticOf(MaterialDiagnosticSeverity::Warning, QStringLiteral("material-gone"), m_definitionProblem));
	}
	if (!m_documentConflict.isEmpty()) {
		all.push_back(diagnosticOf(MaterialDiagnosticSeverity::Warning, QStringLiteral("changed-in-package"), m_documentConflict));
	}
	for (const MaterialDiagnostic& diagnostic : std::as_const(all)) {
		auto* item = new QTreeWidgetItem(m_problems);
		const QString icon = diagnostic.severity == MaterialDiagnosticSeverity::Error ? QStringLiteral("error")
			: diagnostic.severity == MaterialDiagnosticSeverity::Warning			  ? QStringLiteral("warning")
																					  : QStringLiteral("info");
		item->setIcon(0, studioIcon(icon));
		item->setText(0, severityName(diagnostic.severity));
		item->setText(1, diagnostic.line > 0 ? QString::number(diagnostic.line) : QString());
		QString message = diagnostic.message;
		if (!diagnostic.material.isEmpty() && materialLookupKey(diagnostic.material) != materialLookupKey(m_materialName)) {
			message = tr("%1: %2").arg(diagnostic.material, message);
		}
		item->setText(2, message);
		item->setToolTip(2, message);
		item->setData(0, Qt::UserRole, diagnostic.line);
		if (diagnostic.severity != MaterialDiagnosticSeverity::Info) {
			++count;
		}
	}
	m_problemCount = count;
	refreshTabTexts();
}

void MaterialWorkbench::updateImagesTab()
{
	m_imageList->clear();
	for (const QString& reference : m_definition.imageReferences()) {
		auto* item = new QTreeWidgetItem(m_imageList);
		item->setText(0, reference);
		item->setData(0, Qt::UserRole, reference);
		if (const MaterialTexturePtr texture = m_images ? m_images->find(reference) : nullptr) {
			item->setText(1, imageStatusName(texture->status));
			item->setText(2, texture->isValid() ? tr("%1 x %2").arg(texture->width).arg(texture->height) : QString());
			item->setText(3, texture->note.isEmpty() ? texture->path : (texture->path.isEmpty() ? texture->note : texture->path + QStringLiteral(" · ") + texture->note));
			item->setToolTip(3, texture->note);
			if (texture->usable()) {
				item->setIcon(0, QIcon(QPixmap::fromImage(texture->toImage().scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation))));
			} else {
				item->setIcon(0, studioIcon(QStringLiteral("warning")));
			}
		} else if (const MaterialCubeTexturePtr cube = m_images ? m_images->findCube(reference) : nullptr) {
			item->setText(1, cube->isValid() ? tr("Cube map") : tr("Cube map with missing faces"));
			item->setText(3, cube->note);
			item->setIcon(0, studioIcon(cube->isValid() ? QStringLiteral("cube") : QStringLiteral("warning")));
		} else {
			item->setText(1, m_images ? imageStatusName(QStringLiteral("missing")) : tr("Reading..."));
			item->setIcon(0, studioIcon(m_images ? QStringLiteral("warning") : QStringLiteral("clock")));
		}
	}
	m_imageCount = static_cast<int>(m_definition.imageReferences().size());
	refreshTabTexts();
}

void MaterialWorkbench::updateDetails()
{
	// Looking up what else reads the same images walks the whole library:
	// only worth it while the tab shows (it is filled again when it opens).
	if (m_tabs->currentIndex() != kDetailsTab) {
		return;
	}
	if (m_definition.name.isEmpty()) {
		m_details->clear();
		return;
	}
	QStringList lines = materialDefinitionSummaryLines(m_definition);
	if (!m_definition.engineRejection.isEmpty()) {
		lines << QString() << tr("The engine drops this material: %1").arg(m_definition.engineRejection);
	}
	// What else in the package reads the same images.
	QStringList usedBy;
	if (m_library) {
		QSet<int> seen;
		for (const QString& reference : m_definition.imageReferences()) {
			for (int entry : m_library->entriesUsingImage(reference)) {
				if (!seen.contains(entry) && materialLookupKey(m_library->entries.at(entry).name) != materialLookupKey(m_definition.name)) {
					seen.insert(entry);
					usedBy << m_library->entries.at(entry).name;
				}
			}
		}
	}
	if (!usedBy.isEmpty()) {
		usedBy.sort(Qt::CaseInsensitive);
		const int shown = std::min(40, static_cast<int>(usedBy.size()));
		lines << QString() << tr("Materials sharing its images:");
		for (int index = 0; index < shown; ++index) {
			lines << QStringLiteral("  ") + usedBy.at(index);
		}
		if (usedBy.size() > shown) {
			const int more = static_cast<int>(usedBy.size()) - shown;
			lines << QStringLiteral("  ") + tr("and %n more", nullptr, more);
		}
	}
	m_details->setPlainText(lines.join(QLatin1Char('\n')));
}

void MaterialWorkbench::updateNotes()
{
	QStringList notes;
	const MaterialRenderResult& result = m_preview->lastResult();
	if (!result.frameName.isEmpty()) {
		const int frames = static_cast<int>(m_definition.classic.frames.size());
		notes << (frames > 0 ? tr("Frame %1 (%2 of %3)").arg(result.frameName).arg(result.frameIndex + 1).arg(frames) : tr("Frame %1").arg(result.frameName));
	}
	if (result.fallback && !m_definition.engineRejection.isEmpty()) {
		notes << tr("The engine would not draw this material, so the preview shows what it draws instead: %1").arg(m_definition.engineRejection);
	}
	for (const QString& note : result.notes) {
		if (!notes.contains(note)) {
			notes << note;
		}
	}
	if (m_imageLane && m_imageLane->pending()) {
		notes << tr("Reading images...");
	} else if (m_images) {
		const int missing = static_cast<int>(m_images->missing().size());
		if (missing > 0) {
			notes << tr("%n image(s) not found; see Images.", nullptr, missing);
		}
		if (m_images->paletteGenerated && !m_images->palette.isEmpty()
			&& (m_definition.engine == MaterialEngine::Doom || m_definition.engine == MaterialEngine::Quake || m_definition.engine == MaterialEngine::Quake2)) {
			notes << tr("No game palette in the package: colours use a stand-in.");
		}
	}
	m_notes->setText(notes.join(QStringLiteral(" · ")));
	m_notes->setVisible(!notes.isEmpty());
}

double MaterialWorkbench::cycleSeconds() const
{
	// Shared with `material render --cycle`, so the CLI's sheets loop alike.
	static const MaterialImageSet noImages;
	return materialCycleSeconds(m_definition, m_images ? *m_images : noImages);
}

void MaterialWorkbench::updateTimeline()
{
	const double cycle = cycleSeconds();
	const double time = m_preview->time();
	if (!m_timeline->isSliderDown()) {
		const QSignalBlocker blocker(m_timeline);
		m_timeline->setValue(static_cast<int>(std::fmod(time, cycle) / cycle * 1000.0));
	}
	// While playing, the time label (in tenths) changes at most ten times a
	// second, and the timeline's description not at all: a screen reader is
	// not flooded with a change every frame. Paused, both say where it is.
	const bool playing = m_preview->isPlaying();
	if (playing && m_timelineThrottle.isValid() && m_timelineThrottle.elapsed() < 100) {
		return;
	}
	m_timelineThrottle.start();
	const QString label = tr("%1 s").arg(time, 0, 'f', 1);
	if (m_timeLabel->text() != label) {
		m_timeLabel->setText(label);
	}
	const QString description = playing ? tr("Playing a %1 second cycle").arg(cycle, 0, 'f', 1)
										: tr("%1 of a %2 second cycle").arg(std::fmod(time, cycle), 0, 'f', 1).arg(cycle, 0, 'f', 1);
	if (m_timeline->accessibleDescription() != description) {
		m_timeline->setAccessibleDescription(description);
	}
}

void MaterialWorkbench::rebuildLightingMenu()
{
	m_lightingMenu->clear();
	const MaterialRenderOptions options = m_preview->renderOptions();
	// Engine View choices are kept between sessions like Preview Settings.
	const auto change = [this](const std::function<void(MaterialRenderOptions&)>& apply) {
		MaterialRenderOptions next = m_preview->renderOptions();
		apply(next);
		m_preview->setRenderOptions(next);
		scheduleStateSave();
	};
	const auto toggle = [this, change](QMenu* menu, const QString& text, bool checked, const std::function<void(MaterialRenderOptions&, bool)>& apply) {
		QAction* action = menu->addAction(text);
		action->setCheckable(true);
		action->setChecked(checked);
		connect(action, &QAction::toggled, this, [change, apply](bool on) { change([&](MaterialRenderOptions& next) { apply(next, on); }); });
		return action;
	};
	using Choice = std::pair<QString, std::function<void(MaterialRenderOptions&)>>;
	const auto choices = [this, change](QMenu* parent, const QString& title, const QVector<Choice>& items, int current) {
		QMenu* menu = parent->addMenu(title);
		auto* group = new QActionGroup(menu);
		group->setExclusive(true);
		for (int index = 0; index < items.size(); ++index) {
			QAction* action = menu->addAction(items.at(index).first);
			action->setCheckable(true);
			action->setChecked(index == current);
			group->addAction(action);
			const auto apply = items.at(index).second;
			connect(action, &QAction::triggered, this, [change, apply]() { change(apply); });
		}
		return menu;
	};
	const MaterialPreviewLighting& lighting = options.lighting;
	switch (m_definition.engine) {
	case MaterialEngine::Quake3: {
		toggle(m_lightingMenu, tr("Overbright Display"), lighting.overbright, [](MaterialRenderOptions& o, bool on) { o.lighting.overbright = on; });
		toggle(m_lightingMenu, tr("Lightmap Spot"), lighting.lightmapSpot, [](MaterialRenderOptions& o, bool on) { o.lighting.lightmapSpot = on; });
		const QVector<double> levels {0.5, 1.0, 1.5, 2.0};
		QVector<Choice> items;
		for (double level : levels) {
			items.push_back({tr("%1x").arg(level), [level](MaterialRenderOptions& o) { o.lighting.lightmap = level; }});
		}
		choices(m_lightingMenu, tr("Lightmap Brightness"), items,
			static_cast<int>(std::distance(levels.begin(), std::find(levels.begin(), levels.end(), lighting.lightmap))));
		choices(m_lightingMenu, tr("Surface"),
			{{tr("World Brush"), [](MaterialRenderOptions& o) { o.context = MaterialSurfaceContext::World; }},
				{tr("Model"), [](MaterialRenderOptions& o) { o.context = MaterialSurfaceContext::Model; }},
				{tr("2D Interface"), [](MaterialRenderOptions& o) { o.context = MaterialSurfaceContext::TwoD; }}},
			static_cast<int>(options.context));
		// Which game reads the scripts: its keywords load, others drop the shader.
		QMenu* game = m_lightingMenu->addMenu(tr("Game"));
		game->setToolTipsVisible(true);
		auto* games = new QActionGroup(game);
		games->setExclusive(true);
		QVector<MaterialDialect> dialects {MaterialDialect::Automatic};
		dialects += materialDialects();
		for (MaterialDialect each : std::as_const(dialects)) {
			const QString label = each == MaterialDialect::Automatic ? tr("Automatic (%1)").arg(materialDialectDisplayName(dialect()))
																	 : materialDialectDisplayName(each);
			QAction* action = game->addAction(label);
			action->setCheckable(true);
			action->setChecked(each == m_dialectChoice);
			action->setToolTip(tr("Read every shader script as this game does: keywords it does not know drop the shader, and it decides which "
								  "of two scripts defining a name wins."));
			games->addAction(action);
			connect(action, &QAction::triggered, this, [this, each]() { setDialect(each); });
		}
		break;
	}
	case MaterialEngine::Doom: {
		const QVector<int> levels {255, 208, 160, 112, 64};
		QVector<Choice> items;
		for (int level : levels) {
			items.push_back({QString::number(level), [level](MaterialRenderOptions& o) { o.lighting.doomLight = level; }});
		}
		choices(m_lightingMenu, tr("Sector Light"), items,
			static_cast<int>(std::distance(levels.begin(), std::find(levels.begin(), levels.end(), lighting.doomLight))));
		toggle(m_lightingMenu, tr("Darken with Distance"), lighting.doomDistance, [](MaterialRenderOptions& o, bool on) { o.lighting.doomDistance = on; });
		toggle(m_lightingMenu, tr("Fake Contrast"), lighting.doomFakeContrast, [](MaterialRenderOptions& o, bool on) { o.lighting.doomFakeContrast = on; });
		toggle(m_lightingMenu, tr("Boom Texture Wrapping"), lighting.doomBoomWrapping, [](MaterialRenderOptions& o, bool on) { o.lighting.doomBoomWrapping = on; });
		if (!m_definition.classic.switchPartner.isEmpty()) {
			toggle(m_lightingMenu, tr("Switch Pressed"), options.alternate, [](MaterialRenderOptions& o, bool on) { o.alternate = on; });
		}
		break;
	}
	case MaterialEngine::Quake: {
		choices(m_lightingMenu, tr("Renderer"),
			{{tr("GLQuake"), [](MaterialRenderOptions& o) { o.lighting.quakeRenderer = MaterialQuakeRenderer::GLQuake; }},
				{tr("Modern Port"), [](MaterialRenderOptions& o) { o.lighting.quakeRenderer = MaterialQuakeRenderer::ModernPort; }},
				{tr("Software"), [](MaterialRenderOptions& o) { o.lighting.quakeRenderer = MaterialQuakeRenderer::Software; }}},
			static_cast<int>(lighting.quakeRenderer));
		QVector<Choice> styles;
		int currentStyle = -1;
		const QVector<QuakeLightStyle> known = quakeLightStyles();
		for (int index = 0; index < known.size(); ++index) {
			const QString pattern = known.at(index).pattern;
			if (pattern == lighting.quakeStyle) {
				currentStyle = index;
			}
			styles.push_back({tr("%1: %2").arg(known.at(index).number).arg(known.at(index).name), [pattern](MaterialRenderOptions& o) { o.lighting.quakeStyle = pattern; }});
		}
		choices(m_lightingMenu, tr("Light Style"), styles, currentStyle);
		if (!m_definition.classic.alternateFrames.isEmpty()) {
			toggle(m_lightingMenu, tr("Alternate Frames (+a)"), options.alternate, [](MaterialRenderOptions& o, bool on) { o.alternate = on; });
		}
		break;
	}
	case MaterialEngine::Quake2: {
		const QVector<double> intensities {1.0, 2.0, 3.0};
		QVector<Choice> items;
		for (double intensity : intensities) {
			items.push_back({QString::number(intensity), [intensity](MaterialRenderOptions& o) { o.lighting.quake2Intensity = intensity; }});
		}
		choices(m_lightingMenu, tr("Intensity"), items,
			static_cast<int>(std::distance(intensities.begin(), std::find(intensities.begin(), intensities.end(), lighting.quake2Intensity))));
		toggle(m_lightingMenu, tr("Lightmap Spot"), lighting.lightmapSpot, [](MaterialRenderOptions& o, bool on) { o.lighting.lightmapSpot = on; });
		break;
	}
	case MaterialEngine::Doom3: {
		choices(m_lightingMenu, tr("Interaction Shading"),
			{{tr("Doom 3"), [](MaterialRenderOptions& o) { o.lighting.doom3Shading = MaterialDoom3Shading::Vanilla; }},
				{tr("BFG Edition"), [](MaterialRenderOptions& o) { o.lighting.doom3Shading = MaterialDoom3Shading::Bfg; }}},
			static_cast<int>(lighting.doom3Shading));
		toggle(m_lightingMenu, tr("Light Orbits"), lighting.lightOrbit, [](MaterialRenderOptions& o, bool on) { o.lighting.lightOrbit = on; });
		toggle(m_lightingMenu, tr("Specular"), lighting.doom3Specular, [](MaterialRenderOptions& o, bool on) { o.lighting.doom3Specular = on; });
		const QVector<double> ambients {0.0, 0.1, 0.25};
		QVector<Choice> items;
		for (double ambient : ambients) {
			items.push_back({ambient == 0.0 ? tr("None") : tr("%1%").arg(ambient * 100.0), [ambient](MaterialRenderOptions& o) { o.lighting.doom3Ambient = ambient; }});
		}
		choices(m_lightingMenu, tr("Ambient Light"), items,
			static_cast<int>(std::distance(ambients.begin(), std::find(ambients.begin(), ambients.end(), lighting.doom3Ambient))));
		QAction* colour = m_lightingMenu->addAction(tr("Light Colour..."));
		connect(colour, &QAction::triggered, this, [this, change]() {
			const QColor chosen = QColorDialog::getColor(m_preview->renderOptions().lighting.lightColor, this, tr("Light Colour"));
			if (chosen.isValid()) {
				change([chosen](MaterialRenderOptions& o) { o.lighting.lightColor = chosen; });
			}
		});
		QAction* developer = m_lightingMenu->addAction(tr("Developer Default Image"));
		developer->setCheckable(true);
		developer->setChecked(m_developerDefault);
		developer->setToolTip(tr("Draw _default as the developer grid instead of the release build's transparent black."));
		connect(developer, &QAction::toggled, this, [this, change](bool on) {
			m_developerDefault = on;
			change([on](MaterialRenderOptions& o) { o.developerDefault = on; });
			resolveImages(true);
		});
		break;
	}
	case MaterialEngine::Unknown:
		break;
	}
	if (!m_lightingMenu->isEmpty()) {
		m_lightingMenu->addSeparator();
	}
	choices(m_lightingMenu, tr("Filtering"),
		{{tr("As the Engine Does"), [](MaterialRenderOptions& o) { o.filtering = MaterialFiltering::Engine; }},
			{tr("Nearest"), [](MaterialRenderOptions& o) { o.filtering = MaterialFiltering::Nearest; }},
			{tr("Bilinear"), [](MaterialRenderOptions& o) { o.filtering = MaterialFiltering::Bilinear; }}},
		static_cast<int>(options.filtering));
	toggle(m_lightingMenu, tr("Show Editor Image"), options.editorImage, [](MaterialRenderOptions& o, bool on) { o.editorImage = on; });
	toggle(m_lightingMenu, tr("Show What the Engine Draws Instead"), options.honourRejection,
		[](MaterialRenderOptions& o, bool on) { o.honourRejection = on; })
		->setToolTip(tr("When the engine would drop the material, draw its fallback (the default shader, _default) rather than the stages."));
	toggle(m_lightingMenu, tr("Checkerboard Behind"), options.checker, [](MaterialRenderOptions& o, bool on) { o.checker = on; });
	m_lightingMenu->addSeparator();
	QAction* settings = m_lightingMenu->addAction(tr("Preview Settings..."));
	settings->setToolTip(tr("Every preview choice: tiling, field of view, background, entity colour, shader parms, sound level, light and sky."));
	connect(settings, &QAction::triggered, this, &MaterialWorkbench::showPreviewSettings);
	m_lightingMenu->setToolTipsVisible(true);
}

void MaterialWorkbench::showLine(int line)
{
	const QTextBlock block = m_editor->document()->findBlockByNumber(std::max(0, line - 1));
	if (!block.isValid()) {
		return;
	}
	QTextCursor cursor(block);
	m_editor->setTextCursor(cursor);
	m_editor->centerCursor();
	m_editor->setFocus(Qt::OtherFocusReason);
}

void MaterialWorkbench::refreshDocumentState()
{
	if (!m_editor || !m_save) {
		return;
	}
	const bool hasText = m_document.kind != MaterialTextKind::None && !m_document.readOnly;
	// Worked out once per change and kept: every keystroke asks.
	const bool modified = currentTextModified();
	m_currentModified = modified;
	if (!modified) {
		m_documentConflict.clear();
	}
	QString label = m_document.title;
	if (modified) {
		label = tr("%1 (modified)").arg(label);
	}
	if (!m_buffers.isEmpty()) {
		const int others = static_cast<int>(m_buffers.size());
		label += QStringLiteral(" · ") + tr("%n other text(s) with unsaved changes (More, Unsaved Texts)", nullptr, others);
	}
	m_documentLabel->setText(label);
	m_documentLabel->setAccessibleName(label);
	const bool canStage = static_cast<bool>(m_host.stageFile) || static_cast<bool>(m_host.stageLump);
	const bool ownPackage = m_document.package == packageIdentity();
	const bool canSave = hasText && (!m_document.filePath.isEmpty() || (m_reader && canStage && ownPackage) || m_document.virtualPath.isEmpty());
	m_save->setEnabled(modified && canSave);
	m_save->setToolTip(!m_document.filePath.isEmpty() ? tr("Save to %1 (Ctrl+S).").arg(QDir::toNativeSeparators(m_document.filePath))
		: m_document.virtualPath.isEmpty()			 ? tr("Save the new script to a file (Ctrl+S).")
													 : tr("Stage the change into the open package (Ctrl+S); saving the package publishes it."));
	if (!m_document.virtualPath.isEmpty() && m_document.filePath.isEmpty() && !ownPackage) {
		m_save->setToolTip(tr("This text belongs to another package; open that package again to save it."));
	}
	if (!m_documentConflict.isEmpty()) {
		m_save->setToolTip(m_save->toolTip() + QLatin1Char('\n') + m_documentConflict);
	}
	m_revert->setEnabled(modified);
	m_newMaterial->setEnabled(true);
	const bool unsaved = hasUnsavedChanges();
	if (unsaved != m_lastModified) {
		m_lastModified = unsaved;
		Q_EMIT unsavedChangesChanged(unsaved);
		notifyContentChanged();
	}
}

// --- Editing -----------------------------------------------------------------

bool MaterialWorkbench::applyGraphEdit(const MaterialGraphEdit& requested, QString* error)
{
	QString problem;
	if (m_document.kind == MaterialTextKind::None) {
		problem = tr("This material has no text to edit.");
	} else if (m_document.readOnly) {
		problem = tr("This material comes from the base game; Override in Package copies it into your package to edit.");
	}
	MaterialGraphEdit edit = requested;
	const bool addsRange = edit.kind == MaterialGraphEditKind::AddNode
		&& (edit.nodeTemplate == QStringLiteral("doom.range") || edit.nodeTemplate == QStringLiteral("doom.animdefs.range"));
	if (addsRange && edit.related.isEmpty() && m_library) {
		// A new range runs to a later picture of the same stem: the names
		// after the material in its namespace (flats or walls), in order.
		const QString kind = m_baseDefinition.kind == QStringLiteral("doom-flat") ? QStringLiteral("doom-flat") : QStringLiteral("doom-wall");
		const QString own = materialLookupKey(m_materialName);
		bool after = false;
		for (const MaterialLibraryEntry& entry : m_library->entries) {
			if (entry.engine != MaterialEngine::Doom || entry.kind != kind) {
				continue;
			}
			if (after) {
				edit.related << entry.name;
			} else if (materialLookupKey(entry.name) == own) {
				after = true;
			}
		}
	}
	if (edit.related.isEmpty()) {
		edit.related << m_materialName;
		for (const MaterialFrame& frame : m_definition.classic.frames) {
			edit.related << frame.name;
		}
	}
	if (addsRange && edit.value.isEmpty()) {
		edit.value = m_baseDefinition.kind == QStringLiteral("doom-flat") ? QStringLiteral("flat") : QStringLiteral("texture");
	}
	MaterialGraphEditResult result;
	if (problem.isEmpty()) {
		const QString path = m_document.virtualPath.isEmpty() ? m_document.filePath : m_document.virtualPath;
		result = applyMaterialGraphEdit(m_editor->toPlainText(), m_document.kind, m_materialName, edit, path, scriptDialect(m_document.engine));
		if (!result.ok) {
			problem = result.error;
		}
	}
	if (!problem.isEmpty()) {
		if (error) {
			*error = problem;
		}
		Q_EMIT statusMessage(problem);
		m_notes->setText(problem);
		m_notes->setVisible(true);
		return false;
	}
	if (!result.renamedTo.isEmpty()) {
		// The graph's layout follows the material to its new name.
		m_graph->renameLayoutKey(m_materialName, result.renamedTo);
		m_materialName = result.renamedTo;
	}
	// Quick repeated changes of one property (a spin box stepped, a slider
	// dragged) are one undo step, so Undo does not walk back every value.
	const QString editKey = QStringLiteral("%1|%2").arg(edit.node, edit.property);
	const bool join = edit.kind == MaterialGraphEditKind::SetProperty && result.renamedTo.isEmpty() && editKey == m_lastPropertyEdit
		&& m_lastPropertyEditDocument == m_editor->document() && m_lastPropertyEditSteps == m_editor->document()->availableUndoSteps()
		&& m_lastPropertyEditClock.isValid() && m_lastPropertyEditClock.elapsed() < 1000;
	m_applyingPropertyEdit = true;
	replaceEditorText(result.text, join);
	m_applyingPropertyEdit = false;
	if (edit.kind == MaterialGraphEditKind::SetProperty) {
		m_lastPropertyEdit = editKey;
		m_lastPropertyEditDocument = m_editor->document();
		m_lastPropertyEditSteps = m_editor->document()->availableUndoSteps();
		m_lastPropertyEditClock.start();
	} else {
		m_lastPropertyEdit.clear();
	}
	reparse(false);
	if (!result.focusNode.isEmpty()) {
		m_graph->selectNode(result.focusNode);
	}
	if (!result.renamedTo.isEmpty()) {
		Q_EMIT statusMessage(tr("Renamed to %1; maps and models that use the old name need the new one.").arg(result.renamedTo));
	}
	return true;
}

bool MaterialWorkbench::addMaterialFromTemplate(const QString& templateId, const QString& name, const QString& image, QString* error)
{
	MaterialTemplate materialTemplate;
	QString problem;
	const QString trimmed = name.trimmed();
	if (!materialTemplateById(templateId, &materialTemplate)) {
		problem = tr("Unknown template %1.").arg(templateId);
	} else if (QString why; !isValidMaterialName(materialTemplate.engine, trimmed, &why)) {
		// The script's own rule: one word to its lexer, not a keyword.
		problem = why.isEmpty() ? tr("%1 cannot be a material name.").arg(trimmed) : why;
	}
	const MaterialTextKind kind = materialTemplate.engine == MaterialEngine::Doom3 ? MaterialTextKind::Doom3Material : MaterialTextKind::Quake3Shader;
	const QString body = problem.isEmpty() ? instantiateMaterialTemplate(materialTemplate, trimmed, image) : QString();
	if (problem.isEmpty() && m_document.kind == kind) {
		const QString path = m_document.virtualPath.isEmpty() ? m_document.filePath : m_document.virtualPath;
		const MaterialScript script = parseMaterialScript(m_editor->toPlainText(), materialTemplate.engine, path);
		if (script.find(trimmed)) {
			problem = tr("This script already defines %1.").arg(trimmed);
		} else {
			MaterialEdit edit;
			edit.kind = MaterialEditKind::AddDefinition;
			edit.text = body;
			const MaterialEditResult result = applyMaterialEdit(script, edit);
			if (!result.ok) {
				problem = result.error;
			} else {
				m_materialName = trimmed;
				m_engine = materialTemplate.engine;
				m_materialKind.clear();
				m_baseDefinition = MaterialDefinition();
				m_materialChangePending = true;
				replaceEditorText(result.text);
				reparse(true);
			}
		}
	} else if (problem.isEmpty()) {
		// A new script, staged into the package on save or saved as a file. A
		// second template of the same engine joins the new script that is not
		// saved yet, rather than starting another at the same path.
		QString scriptPath;
		const QString package = packageIdentity();
		for (auto it = m_bufferDocuments.cbegin(); it != m_bufferDocuments.cend() && scriptPath.isEmpty(); ++it) {
			const Document& each = it.value();
			if (each.isNew && each.engine == materialTemplate.engine && each.package == package && m_buffers.contains(it.key())) {
				scriptPath = each.virtualPath.isEmpty() ? freeScriptPath(materialTemplate.engine) : each.virtualPath;
			}
		}
		if (scriptPath.isEmpty()) {
			scriptPath = freeScriptPath(materialTemplate.engine);
		}
		if (!addDefinitionText(materialTemplate.engine, scriptPath, trimmed, body, &problem) && problem.isEmpty()) {
			problem = tr("%1 could not be added.").arg(trimmed);
		}
		if (!problem.isEmpty()) {
			if (error) {
				*error = problem;
			}
			return false;
		}
	}
	if (!problem.isEmpty()) {
		if (error) {
			*error = problem;
		}
		Q_EMIT statusMessage(problem);
		return false;
	}
	Q_EMIT statusMessage(tr("Added %1; save to keep it.").arg(trimmed));
	return true;
}

void MaterialWorkbench::showNewMaterialDialog()
{
	QDialog dialog(this);
	dialog.setWindowTitle(tr("New Material"));
	dialog.setAccessibleName(tr("New material"));
	auto* form = new QFormLayout;
	auto* templates = new QComboBox(&dialog);
	templates->setAccessibleName(tr("Template"));
	MaterialEngine preferred = m_document.engine;
	if (preferred != MaterialEngine::Quake3 && preferred != MaterialEngine::Doom3) {
		preferred = m_library && m_library->count(MaterialEngine::Doom3) > m_library->count(MaterialEngine::Quake3) ? MaterialEngine::Doom3
																													: MaterialEngine::Quake3;
	}
	auto* description = new QLabel(&dialog);
	description->setWordWrap(true);
	description->setForegroundRole(QPalette::PlaceholderText);
	for (const MaterialTemplate& each : materialTemplates()) {
		templates->addItem(tr("%1: %2").arg(materialEngineDisplayName(each.engine), each.displayName), each.id);
		templates->setItemData(templates->count() - 1, each.description, Qt::ToolTipRole);
		if (each.engine == preferred && templates->count() > 0 && templates->currentData().toString().isEmpty()) {
			templates->setCurrentIndex(templates->count() - 1);
		}
	}
	const auto describe = [templates, description]() {
		MaterialTemplate chosen;
		if (materialTemplateById(templates->currentData().toString(), &chosen)) {
			description->setText(chosen.description);
		}
	};
	connect(templates, &QComboBox::currentIndexChanged, &dialog, describe);
	for (int index = 0; index < templates->count(); ++index) {
		MaterialTemplate each;
		if (materialTemplateById(templates->itemData(index).toString(), &each) && each.engine == preferred) {
			templates->setCurrentIndex(index);
			break;
		}
	}
	describe();
	auto* name = new QLineEdit(&dialog);
	name->setAccessibleName(tr("Material name"));
	const QString folder = m_materialName.contains(QLatin1Char('/')) ? m_materialName.section(QLatin1Char('/'), 0, -2) : QStringLiteral("textures/custom");
	name->setText(folder + QStringLiteral("/new_material"));
	auto* image = new QLineEdit(&dialog);
	image->setAccessibleName(tr("Main image"));
	image->setPlaceholderText(tr("The material's name"));
	const QStringList images = packageImages();
	if (!images.isEmpty()) {
		auto* completer = new QCompleter(images, image);
		completer->setCaseSensitivity(Qt::CaseInsensitive);
		completer->setFilterMode(Qt::MatchContains);
		image->setCompleter(completer);
	}
	form->addRow(tr("Template"), templates);
	form->addRow(QString(), description);
	form->addRow(tr("Name"), name);
	form->addRow(tr("Image"), image);
	auto* problem = new QLabel(&dialog);
	problem->setWordWrap(true);
	problem->setVisible(false);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	auto* layout = new QVBoxLayout(&dialog);
	layout->addLayout(form);
	layout->addWidget(problem);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
		QString error;
		if (addMaterialFromTemplate(templates->currentData().toString(), name->text(), image->text(), &error)) {
			dialog.accept();
		} else {
			problem->setText(error);
			problem->setVisible(true);
			problem->setAccessibleName(error);
		}
	});
	name->setFocus();
	name->selectAll();
	dialog.exec();
}

bool MaterialWorkbench::writeLooseFile(const QString& path, const QByteArray& bytes, QString* error)
{
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
		if (error) {
			*error = tr("Could not write %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
		}
		return false;
	}
	return true;
}

bool MaterialWorkbench::writeDocument(Document* document, const QString& text, QString* problem)
{
	bool ok = false;
	if (document->readOnly) {
		*problem = tr("Base game scripts are read only; Override in Package copies a material into your package.");
		return false;
	}
	// A text read from another package is never staged into this one, even
	// at the same path: it would replace a different file.
	if (document->filePath.isEmpty() && !document->package.isEmpty() && document->package != packageIdentity()) {
		*problem = tr("This text was read from %1, which is not the open package; open that package again to save it, or discard it.")
					   .arg(QDir::toNativeSeparators(document->packagePath));
		return false;
	}
	switch (document->kind) {
	case MaterialTextKind::None:
		*problem = tr("This material has no text to save.");
		break;
	case MaterialTextKind::Quake3Shader:
	case MaterialTextKind::Doom3Material:
		if (!document->filePath.isEmpty()) {
			ok = writeLooseFile(document->filePath, text.toUtf8(), problem);
		} else if (!document->virtualPath.isEmpty() && m_reader && m_host.stageFile) {
			ok = m_host.stageFile(text.toUtf8(), document->virtualPath, problem);
		} else {
			*problem = tr("There is no package to stage this script into; use Save As.");
		}
		break;
	case MaterialTextKind::DoomSwantbls: {
		QVector<DoomAnimationRange> ranges;
		QVector<DoomSwitchPair> switches;
		QVector<MaterialDiagnostic> diagnostics;
		if (!parseSwantblsText(text, &ranges, &switches, &diagnostics)) {
			*problem = diagnostics.isEmpty() ? tr("The table text has errors.") : tr("Line %1: %2").arg(diagnostics.first().line).arg(diagnostics.first().message);
			break;
		}
		const QByteArray animated = boomAnimatedLump(ranges);
		const QByteArray switchLump = boomSwitchesLump(switches);
		if (packageIsWad() && m_host.stageLump) {
			ok = m_host.stageLump(animated, QStringLiteral("ANIMATED"), problem) && m_host.stageLump(switchLump, QStringLiteral("SWITCHES"), problem);
		} else if (m_reader && m_host.stageFile) {
			ok = m_host.stageFile(animated, QStringLiteral("ANIMATED.lmp"), problem) && m_host.stageFile(switchLump, QStringLiteral("SWITCHES.lmp"), problem);
		} else {
			*problem = tr("Open the WAD or package the tables belong to.");
		}
		break;
	}
	case MaterialTextKind::DoomAnimdefs:
		if (packageIsWad() && m_host.stageLump) {
			ok = m_host.stageLump(text.toUtf8(), QStringLiteral("ANIMDEFS"), problem);
		} else if (m_reader && m_host.stageFile) {
			ok = m_host.stageFile(text.toUtf8(), document->virtualPath, problem);
		} else {
			*problem = tr("Open the WAD or package ANIMDEFS belongs to.");
		}
		break;
	case MaterialTextKind::Quake2WalJson: {
		Quake2WalInfo info;
		QByteArray bytes = document->walBytes;
		if (!readQuake2WalInfo(bytes, &info, problem) || !parseQuake2WalInfoText(text, &info, problem) || !rewriteQuake2WalHeader(&bytes, info, problem)) {
			break;
		}
		if (m_reader && m_host.stageFile) {
			ok = m_host.stageFile(bytes, document->virtualPath, problem);
			if (ok) {
				document->walBytes = bytes;
			}
		} else {
			*problem = tr("Open the package the WAL belongs to.");
		}
		break;
	}
	}
	if (ok) {
		document->savedText = text;
	}
	return ok;
}

bool MaterialWorkbench::save(QString* error)
{
	QString problem;
	const QString text = m_editor->toPlainText();
	const bool ok = writeDocument(&m_document, text, &problem);
	if (!ok) {
		if (problem.isEmpty()) {
			problem = tr("Saving is not available here.");
		}
		if (error) {
			*error = problem;
		}
		Q_EMIT statusMessage(problem);
		return false;
	}
	forgetBuffer(m_document.key);
	m_documentConflict.clear();
	if (m_editor->toPlainText() == m_document.savedText) {
		m_editor->document()->setModified(false);
	}
	refreshDocumentState();
	if (!m_document.filePath.isEmpty() && m_document.filePath == m_looseFile) {
		// A loose script: read it again so the library shows what was saved.
		startScan();
	}
	Q_EMIT statusMessage(m_document.filePath.isEmpty() ? tr("Staged %1 into the package; save the package to publish it.").arg(m_document.title)
														: tr("Saved %1.").arg(QDir::toNativeSeparators(m_document.filePath)));
	return true;
}

bool MaterialWorkbench::saveAs(const QString& path, QString* error)
{
	if (m_document.kind != MaterialTextKind::Quake3Shader && m_document.kind != MaterialTextKind::Doom3Material) {
		if (error) {
			*error = tr("Only scripts can be saved to a file of their own.");
		}
		return false;
	}
	QString problem;
	if (!writeLooseFile(path, m_editor->toPlainText().toUtf8(), &problem)) {
		if (error) {
			*error = problem;
		}
		Q_EMIT statusMessage(problem);
		return false;
	}
	const QFileInfo info(path);
	forgetBuffer(m_document.key);
	m_document.filePath = info.absoluteFilePath();
	m_document.virtualPath.clear();
	m_document.package.clear();
	m_document.packagePath.clear();
	m_document.isNew = false;
	m_document.scriptIndex = -1;
	m_document.title = QDir::toNativeSeparators(info.absoluteFilePath());
	m_document.savedText = m_editor->toPlainText();
	m_document.key = QStringLiteral("file:") + QDir::cleanPath(info.absoluteFilePath()).toLower();
	// A file of that name with unsaved text of its own is the same file now.
	forgetBuffer(m_document.key);
	m_documentConflict.clear();
	m_editor->document()->setModified(false);
	m_looseFile = info.absoluteFilePath();
	refreshDocumentState();
	startScan();
	Q_EMIT statusMessage(tr("Saved %1.").arg(QDir::toNativeSeparators(info.absoluteFilePath())));
	return true;
}

void MaterialWorkbench::chooseSaveAs()
{
	const bool doom3 = m_document.kind == MaterialTextKind::Doom3Material;
	const QString filter = doom3 ? tr("Doom 3 materials (*.mtr)") : tr("Quake III shaders (*.shader)");
	const QString suggested = m_document.virtualPath.isEmpty() ? (doom3 ? QStringLiteral("slopstudio.mtr") : QStringLiteral("slopstudio.shader"))
															   : QFileInfo(m_document.virtualPath).fileName();
	const QString path = QFileDialog::getSaveFileName(this, tr("Save Material Script"), suggested, filter);
	if (!path.isEmpty()) {
		saveAs(path);
	}
}

void MaterialWorkbench::revert()
{
	if (m_document.kind == MaterialTextKind::None) {
		return;
	}
	m_documentConflict.clear();
	replaceEditorText(m_document.savedText);
	reparse(false);
}

void MaterialWorkbench::saveCurrent()
{
	// Ctrl+S and the Save button: one behaviour.
	if (!m_save->isEnabled()) {
		if (!m_buffers.isEmpty()) {
			const int others = static_cast<int>(m_buffers.size());
			Q_EMIT statusMessage(tr("This text has nothing to save; %n other text(s) have unsaved changes: Save All (Ctrl+Alt+S) saves them.",
				nullptr, others));
		} else if (m_currentModified && m_document.package != packageIdentity() && m_document.filePath.isEmpty()) {
			Q_EMIT statusMessage(tr("This text belongs to another package; open that package again to save it."));
		}
		return;
	}
	if (m_document.filePath.isEmpty() && m_document.virtualPath.isEmpty()) {
		chooseSaveAs();
	} else {
		save();
	}
}

void MaterialWorkbench::discardUnsavedChanges()
{
	const QStringList keys = m_buffers.keys();
	for (const QString& key : keys) {
		forgetBuffer(key);
	}
	m_documentConflict.clear();
	if (m_document.kind != MaterialTextKind::None && currentTextModified()) {
		// No undo step: the edits are thrown away, not reverted.
		setEditorText(m_document.savedText);
		m_editor->document()->clearUndoRedoStacks();
		reparse(false);
	}
	refreshDocumentState();
}

void MaterialWorkbench::discardBufferedText(const QString& key)
{
	if (!m_buffers.contains(key)) {
		return;
	}
	const QString title = m_bufferDocuments.value(key).title;
	forgetBuffer(key);
	refreshDocumentState();
	Q_EMIT statusMessage(tr("Discarded the unsaved changes to %1.").arg(title));
}

void MaterialWorkbench::showBufferedText(const QString& key)
{
	if (!m_buffers.contains(key) || !m_bufferDocuments.contains(key)) {
		return;
	}
	const Document document = m_bufferDocuments.value(key);
	const QString material = document.material;
	const MaterialEngine engine = document.materialEngine;
	switchDocument(document);
	// The material last shown in that text, as the library has it when it
	// is still there.
	m_materialName = material;
	m_engine = engine == MaterialEngine::Unknown ? document.engine : engine;
	m_materialKind = m_engine == MaterialEngine::Doom ? document.materialKind : QString();
	const int entry = currentEntry();
	m_baseDefinition = entry >= 0 ? m_library->definition(entry) : MaterialDefinition();
	m_materialChangePending = true;
	if (entry >= 0) {
		const QModelIndex proxyIndex = m_proxy->mapFromSource(m_model->index(entry));
		if (proxyIndex.isValid()) {
			m_refiltering = true;
			m_libraryView->setCurrentIndex(proxyIndex);
			m_refiltering = false;
			m_libraryView->scrollTo(proxyIndex);
		}
	} else {
		m_libraryView->selectionModel()->clearSelection();
	}
	reparse(false);
	m_tabs->setCurrentIndex(kTextTab);
	notifyContentChanged();
}

// --- Saving everything ---------------------------------------------------------

bool MaterialWorkbench::saveAll(QString* error)
{
	QStringList failures;
	int saved = 0;
	if (currentTextModified()) {
		QString problem;
		if (save(&problem)) {
			++saved;
		} else {
			failures << tr("%1: %2").arg(m_document.title, problem);
		}
	}
	const QStringList keys = m_buffers.keys();
	for (const QString& key : keys) {
		Document document = m_bufferDocuments.value(key);
		QTextDocument* buffered = m_buffers.value(key);
		if (document.key.isEmpty() || !buffered) {
			continue;
		}
		QString problem;
		if (writeDocument(&document, buffered->toPlainText(), &problem)) {
			forgetBuffer(key);
			++saved;
		} else {
			failures << tr("%1: %2").arg(document.title, problem);
		}
	}
	refreshDocumentState();
	if (!failures.isEmpty()) {
		const QString message = tr("Saved %n text(s); these were not:", nullptr, saved) + QLatin1Char(' ') + failures.join(QStringLiteral("; "));
		if (error) {
			*error = message;
		}
		Q_EMIT statusMessage(message);
		return false;
	}
	Q_EMIT statusMessage(saved > 0 ? tr("Saved %n material text(s).", nullptr, saved) : tr("Nothing to save."));
	return true;
}

// --- Whole definitions ---------------------------------------------------------

bool MaterialWorkbench::currentIsBase() const
{
	if (!m_library) {
		return false;
	}
	const int entry = currentEntry();
	return entry >= 0 && m_library->entries.at(entry).base;
}

bool MaterialWorkbench::applyWholeDefinitionEdit(const MaterialEdit& requested, const QString& select, QString* error)
{
	QString problem;
	MaterialEditResult result;
	if (m_document.kind != MaterialTextKind::Quake3Shader && m_document.kind != MaterialTextKind::Doom3Material) {
		problem = tr("Only Quake III shaders and Doom 3 materials are whole definitions to rename, copy or delete.");
	} else if (m_document.readOnly) {
		problem = tr("This material comes from the base game; Override in Package copies it into your package to edit.");
	} else if (QString why; (requested.kind == MaterialEditKind::RenameDefinition || requested.kind == MaterialEditKind::DuplicateDefinition)
			   && !isValidMaterialName(m_document.engine, requested.text, &why)) {
		// A new name must read back as one name in this engine's script.
		problem = why.isEmpty() ? tr("%1 cannot be a material name.").arg(requested.text) : why;
	} else {
		const QString path = m_document.virtualPath.isEmpty() ? m_document.filePath : m_document.virtualPath;
		const MaterialScript script = parseMaterialScript(m_editor->toPlainText(), m_document.engine, path, scriptDialect(m_document.engine));
		MaterialEdit edit = requested;
		edit.material = m_materialName;
		if (!script.find(m_materialName)) {
			problem = tr("The text does not define %1.").arg(m_materialName);
		} else {
			result = applyMaterialEdit(script, edit);
			if (!result.ok) {
				problem = result.error;
			}
		}
	}
	if (!problem.isEmpty()) {
		if (error) {
			*error = problem;
		}
		Q_EMIT statusMessage(problem);
		return false;
	}
	QString next = select;
	if (next.isEmpty() && !result.script.materials.isEmpty()) {
		next = result.script.materials.first().name;
	}
	if (requested.kind == MaterialEditKind::RenameDefinition) {
		// The graph's layout follows the material to its new name, and the
		// same material stays in view rather than counting as another.
		m_graph->renameLayoutKey(m_materialName, next);
		m_materialChangePending = false;
	} else {
		m_materialChangePending = materialLookupKey(next) != materialLookupKey(m_materialName);
	}
	m_materialName = next;
	replaceEditorText(result.text);
	reparse(true);
	return true;
}

bool MaterialWorkbench::renameCurrentMaterial(const QString& name, QString* error)
{
	MaterialEdit edit;
	edit.kind = MaterialEditKind::RenameDefinition;
	edit.text = name.trimmed();
	if (!applyWholeDefinitionEdit(edit, edit.text, error)) {
		return false;
	}
	Q_EMIT statusMessage(tr("Renamed to %1; maps and models that use the old name need the new one.").arg(edit.text));
	return true;
}

bool MaterialWorkbench::duplicateCurrentMaterial(const QString& name, QString* error)
{
	MaterialEdit edit;
	edit.kind = MaterialEditKind::DuplicateDefinition;
	edit.text = name.trimmed();
	if (!applyWholeDefinitionEdit(edit, edit.text, error)) {
		return false;
	}
	Q_EMIT statusMessage(tr("Added %1 as a copy; save to keep it.").arg(edit.text));
	return true;
}

bool MaterialWorkbench::deleteCurrentMaterial(QString* error)
{
	const QString removed = m_materialName;
	MaterialEdit edit;
	edit.kind = MaterialEditKind::RemoveDefinition;
	if (!applyWholeDefinitionEdit(edit, QString(), error)) {
		return false;
	}
	Q_EMIT statusMessage(tr("Removed %1 from the text; save to keep the change, or Undo to bring it back.").arg(removed));
	return true;
}

bool MaterialWorkbench::addDefinitionText(MaterialEngine engine, const QString& scriptPath, const QString& name, const QString& body, QString* error)
{
	const MaterialTextKind kind = engine == MaterialEngine::Doom3 ? MaterialTextKind::Doom3Material : MaterialTextKind::Quake3Shader;
	Document document;
	QString text;
	bool exists = false;
	// A package script and a new one there share a key, so a new script
	// that was staged is the same text once the library lists it. With no
	// package, a new script of each engine waits for Save As.
	const QString package = packageIdentity();
	const QString key = m_reader ? scriptKey(package, scriptPath) : QStringLiteral("unsaved:") + materialEngineId(engine);
	const QString currentPath = m_document.virtualPath.isEmpty() ? m_document.filePath : m_document.virtualPath;
	if (m_document.kind == kind && !m_document.readOnly
		&& (m_document.key == key || (m_document.package == package && currentPath.compare(scriptPath, Qt::CaseInsensitive) == 0))) {
		document = m_document;
		text = m_editor->toPlainText();
		exists = true;
	} else if (m_buffers.contains(key) && m_bufferDocuments.contains(key)) {
		document = m_bufferDocuments.value(key);
		text = m_buffers.value(key)->toPlainText();
		exists = true;
	} else if (m_library && m_reader) {
		for (int index = 0; index < m_library->scripts.size(); ++index) {
			const MaterialScript& script = m_library->scripts.at(index);
			if (script.path.compare(scriptPath, Qt::CaseInsensitive) == 0 && !m_library->basePaths.contains(script.path.toLower())) {
				document.kind = kind;
				document.engine = engine;
				document.package = package;
				document.packagePath = m_packagePath;
				document.virtualPath = script.path;
				document.title = script.path;
				document.key = scriptKey(package, script.path);
				document.savedText = script.text;
				document.scriptIndex = index;
				text = script.text;
				exists = true;
				break;
			}
		}
	}
	if (!exists) {
		document.kind = kind;
		document.engine = engine;
		document.isNew = true;
		document.package = m_reader ? package : QString();
		document.packagePath = m_reader ? m_packagePath : QString();
		document.virtualPath = m_reader ? scriptPath : QString();
		document.title = m_reader ? tr("%1 (new)").arg(scriptPath) : tr("New script (not saved)");
		document.key = key;
	}
	// The same rule as `material override`: refuse a name the script already
	// defines, else append the definition (a new or empty script is just it).
	MaterialOverride addition;
	addition.name = name;
	addition.engine = engine;
	addition.virtualPath = scriptPath;
	addition.definitionText = body;
	QString newText;
	QString problem;
	if (!materialOverrideScriptText(exists ? text : QString(), addition, scriptDialect(engine), &newText, &problem)) {
		if (error) {
			*error = problem;
		}
		Q_EMIT statusMessage(problem);
		return false;
	}
	if (document.key != m_document.key) {
		switchDocument(document);
	}
	m_materialName = name;
	m_engine = engine;
	m_materialKind.clear();
	m_baseDefinition = MaterialDefinition();
	m_materialChangePending = true;
	replaceEditorText(newText);
	reparse(true);
	return true;
}

bool MaterialWorkbench::overrideInPackage(QString* error)
{
	QString problem;
	// The rule (which script, which entries, the Quake II file copy) is the
	// core one `material override` uses (core/material_override.h).
	MaterialOverride copy;
	const int entry = currentEntry();
	if (!m_library || !materialOverrideForEntry(*m_library, entry, dialect(), &copy, &problem)) {
		if (problem.isEmpty()) {
			problem = QCoreApplication::translate("SlopStudioMaterials", "Only a base game material can be overridden in the package.");
		}
	} else if (!m_reader) {
		problem = tr("Open the package to put the copy in.");
	} else if (copy.copiesFile()) {
		// A Quake II WAL: the package gets its own copy of the file.
		if (!m_host.stageFile || !m_host.stageFile(copy.fileBytes, copy.virtualPath, &problem)) {
			problem = problem.isEmpty() ? tr("The package cannot take files.") : problem;
		} else {
			Q_EMIT statusMessage(tr("Copied %1 into the package; its header is now yours to edit.").arg(copy.virtualPath));
			return true;
		}
	} else if (addDefinitionText(copy.engine, copy.virtualPath, copy.name, copy.definitionText, &problem)) {
		Q_EMIT statusMessage(tr("Copied %1 into %2; save to stage it into the package.").arg(copy.name, copy.virtualPath));
		return true;
	}
	if (error) {
		*error = problem;
	}
	Q_EMIT statusMessage(problem);
	return false;
}

QString MaterialWorkbench::scriptFilePath() const
{
	if (!m_document.filePath.isEmpty()) {
		return m_document.filePath;
	}
	// A script in a folder package is a file on disk too.
	if (!m_document.virtualPath.isEmpty() && !m_document.readOnly && QFileInfo(m_packagePath).isDir()) {
		const QString path = QDir(m_packagePath).filePath(m_document.virtualPath);
		if (QFileInfo(path).isFile()) {
			return path;
		}
	}
	return QString();
}

// --- Export ------------------------------------------------------------------

bool MaterialWorkbench::exportPreview(const QString& path, int frames, const QSize& requested, QString* error)
{
	QString problem;
	QImage output;
	PreviewSheetPlan plan;
	if (m_definition.name.isEmpty() || !m_images || !m_tables) {
		problem = tr("Choose a material and wait for its images first.");
	} else {
		plan = planPreviewSheet(frames, requested.isValid() ? requested : exportFrameSize());
		const PreviewSheetResult sheet =
			drawPreviewSheet(m_definition, *m_images, *m_tables, m_preview->renderOptions(), plan, cycleSeconds(), m_preview->time(), {}, {});
		problem = sheet.problem;
		output = sheet.image;
	}
	if (problem.isEmpty() && !output.save(path, "PNG")) {
		problem = tr("Could not write %1.").arg(QDir::toNativeSeparators(path));
	}
	if (!problem.isEmpty()) {
		if (error) {
			*error = problem;
		}
		Q_EMIT statusMessage(problem);
		return false;
	}
	Q_EMIT statusMessage(plan.shrunk ? tr("Wrote %1, with frames of %2 x %3 pixels to keep the image within its size limit.")
										   .arg(QDir::toNativeSeparators(path))
										   .arg(plan.cell.width())
										   .arg(plan.cell.height())
									 : tr("Wrote %1.").arg(QDir::toNativeSeparators(path)));
	return true;
}

QSize MaterialWorkbench::exportFrameSize() const
{
	// The preview's own size at full resolution: a playing preview draws
	// fewer pixels to keep its pace, which is no size for an export.
	const double dpr = m_preview->devicePixelRatioF();
	const int width = std::clamp(static_cast<int>(std::lround(m_preview->width() * dpr)), 16, kFrameSideLimit);
	const int height = std::clamp(static_cast<int>(std::lround(m_preview->height() * dpr)), 16, kFrameSideLimit);
	return {width, height};
}

void MaterialWorkbench::exportPreviewInBackground(const QString& path, int frames, const QSize& requested)
{
	if (m_definition.name.isEmpty() || !m_images || !m_tables) {
		Q_EMIT statusMessage(tr("Choose a material and wait for its images first."));
		return;
	}
	const PreviewSheetPlan plan = planPreviewSheet(frames, requested.isValid() ? requested : exportFrameSize());
	const MaterialDefinition definition = m_definition;
	const std::shared_ptr<const MaterialImageSet> images = m_images;
	const std::shared_ptr<const MaterialTableSet> tables = m_tables;
	MaterialRenderOptions options = m_preview->renderOptions();
	options.cancelFlag = nullptr;
	const double cycle = cycleSeconds();
	const double now = m_preview->time();
	m_exportBusy->setProgress({0, plan.count});
	m_exportBusy->begin(plan.count == 1 ? tr("Exporting the preview") : tr("Exporting an animation sheet"), QDir::toNativeSeparators(path));
	QPointer<MaterialWorkbench> self(this);
	m_exportLane->submit([self, path, plan, definition, images, tables, options, cycle, now](
							 const MaterialTaskLane::Cancelled& cancelled) -> std::function<void()> {
		// Cancel stops a frame on the graphics device as well.
		MaterialRenderOptions frameOptions = options;
		frameOptions.cancelFlag = MaterialTaskLane::currentStopFlag();
		const PreviewSheetResult sheet = drawPreviewSheet(definition, *images, *tables, frameOptions, plan, cycle, now, cancelled, [self](int done, int total) {
			QMetaObject::invokeMethod(
				QCoreApplication::instance(),
				[self, done, total]() {
					if (self && self->m_exportBusy->isActive()) {
						self->m_exportBusy->setProgress({done, total});
					}
				},
				Qt::QueuedConnection);
		});
		if (sheet.cancelled) {
			return {};
		}
		QString problem = sheet.problem;
		if (problem.isEmpty() && !sheet.image.save(path, "PNG")) {
			problem = MaterialWorkbench::tr("Could not write %1.").arg(QDir::toNativeSeparators(path));
		}
		return [self, path, plan, problem]() {
			if (!self) {
				return;
			}
			self->m_exportBusy->finish();
			if (!problem.isEmpty()) {
				Q_EMIT self->statusMessage(problem);
			} else if (plan.shrunk) {
				Q_EMIT self->statusMessage(tr("Wrote %1, with frames of %2 x %3 pixels to keep the image within its size limit.")
											   .arg(QDir::toNativeSeparators(path))
											   .arg(plan.cell.width())
											   .arg(plan.cell.height()));
			} else {
				Q_EMIT self->statusMessage(tr("Wrote %1.").arg(QDir::toNativeSeparators(path)));
			}
		};
	});
}

void MaterialWorkbench::chooseExportImage(bool sheet)
{
	int frames = 1;
	if (sheet) {
		bool ok = false;
		frames = QInputDialog::getInt(this, tr("Export Animation Sheet"), tr("Frames over one cycle (%1 s)").arg(cycleSeconds(), 0, 'f', 1), 16, 2, 256,
			1, &ok);
		if (!ok) {
			return;
		}
	}
	QString suggested = QFileInfo(m_materialName).fileName();
	suggested = (suggested.isEmpty() ? QStringLiteral("material") : suggested) + (sheet ? QStringLiteral("_sheet.png") : QStringLiteral(".png"));
	const QString path = QFileDialog::getSaveFileName(this, sheet ? tr("Export Animation Sheet") : tr("Export Preview Image"), suggested,
		tr("PNG images (*.png)"));
	if (!path.isEmpty()) {
		exportPreviewInBackground(path, frames);
	}
}

// --- Maps ----------------------------------------------------------------------

void MaterialWorkbench::scanUsage()
{
	const std::shared_ptr<const PackageArchiveReader> reader = m_library && m_library->reader ? m_library->reader : m_reader;
	if (!reader) {
		m_usageLane->cancel();
		m_usage.reset();
		m_usageStale = false;
		m_usageScanning = false;
		finishUsageTracking();
		updateMapsTab();
		return;
	}
	m_usageScanning = true;
	m_usageStale = false;
	// Counted with the rest of the window's work, so the status bar's
	// Activity ring turns while the maps are read.
	finishUsageTracking();
	m_usageToken = studioWorkTracker().begin(tr("Reading maps for material uses"));
	m_mapsBusy->setProgress({});
	updateMapsTab();
	QPointer<MaterialWorkbench> self(this);
	const int token = m_usageToken;
	m_usageLane->submit([reader, self, token](const MaterialTaskLane::Cancelled& cancelled) -> std::function<void()> {
		QElapsedTimer throttle;
		throttle.start();
		auto usage = std::make_shared<MaterialUsageIndex>(
			scanMaterialUsage(*reader, [cancelled, self, token, throttle](int done, int total, const QString&) mutable {
				if (throttle.elapsed() > 100 || done == total) {
					throttle.restart();
					QMetaObject::invokeMethod(
						QCoreApplication::instance(),
						[self, token, done, total]() {
							if (self && self->m_usageScanning && self->m_usageToken == token) {
								self->m_mapsBusy->setProgress({done, total});
								studioWorkTracker().setProgress(token, {done, total});
							}
						},
						Qt::QueuedConnection);
				}
				return !cancelled();
			}));
		if (usage->cancelled) {
			return {};
		}
		return [self, usage]() {
			if (!self) {
				return;
			}
			self->m_usage = usage;
			self->m_usageScanning = false;
			self->finishUsageTracking();
			self->applyLibraryOrder();
			self->updateMapsTab();
			self->refreshTabTexts();
		};
	});
}

void MaterialWorkbench::finishUsageTracking()
{
	if (m_usageToken != 0) {
		studioWorkTracker().end(m_usageToken);
		m_usageToken = 0;
	}
}

void MaterialWorkbench::showMapUses()
{
	m_tabs->setCurrentIndex(kMapsTab);
	if ((m_usageStale || !m_usage) && !m_usageScanning) {
		scanUsage();
	}
}

void MaterialWorkbench::updateMapsTab()
{
	if (!m_mapList) {
		return;
	}
	m_mapList->clear();
	QString summary;
	if (m_usageScanning) {
		summary = tr("Reading the package's maps...");
	} else if (!m_usage) {
		summary = m_reader ? tr("The maps are read when this tab opens.") : tr("Open a package to see the maps that use its materials.");
	} else {
		const QVector<MaterialMapUse> uses = m_usage->usesOf(m_materialName);
		int total = 0;
		for (int index = 0; index < uses.size(); ++index) {
			const MaterialMapUse& use = uses.at(index);
			auto* item = new QTreeWidgetItem(m_mapList);
			QString where = use.mapPath;
			if (!use.mapName.isEmpty()) {
				where = where.isEmpty() ? use.mapName : tr("%1, map %2").arg(use.mapPath, use.mapName);
			}
			item->setText(0, where);
			item->setText(1, QString::number(use.count));
			QStringList lines;
			for (int line : use.lines) {
				lines << QString::number(line);
			}
			const QString kind = use.kind == MaterialMapKind::Bsp ? tr("Compiled map") : use.kind == MaterialMapKind::DoomMap ? tr("Doom map") : tr("Map source");
			item->setText(2, lines.isEmpty() ? kind : tr("%1, lines %2").arg(kind, lines.join(QStringLiteral(", "))));
			item->setIcon(0, studioIcon(use.kind == MaterialMapKind::Bsp ? QStringLiteral("package") : QStringLiteral("map")));
			item->setData(0, Qt::UserRole, index);
			item->setToolTip(0, use.kind == MaterialMapKind::Bsp ? tr("Enter shows the compiled map on the Packages page.")
																 : tr("Enter opens the map in Levels with these uses selected."));
			total += use.count;
		}
		const int maps = static_cast<int>(uses.size());
		summary = m_materialName.isEmpty() ? tr("%n map(s) read.", nullptr, static_cast<int>(m_usage->maps.size()))
			: maps == 0 ? tr("No map in the package uses %1 (%n map(s) read).", nullptr, static_cast<int>(m_usage->maps.size())).arg(m_materialName)
						: tr("%1 is used %2 time(s) in %n map(s).", nullptr, maps).arg(m_materialName).arg(total);
		if (!m_usage->warnings.isEmpty()) {
			summary += QLatin1Char(' ') + tr("%n map(s) could not be read.", nullptr, static_cast<int>(m_usage->warnings.size()));
			m_mapsSummary->setToolTip(m_usage->warnings.join(QLatin1Char('\n')));
		}
	}
	m_mapsSummary->setText(summary);
	m_mapsSummary->setAccessibleDescription(summary);
	m_mapsBusy->setActive(m_usageScanning);
	m_mapsBusy->setReducedMotion(m_reducedMotion);
	m_mapsRescan->setEnabled(m_reader != nullptr && !m_usageScanning);
	refreshTabTexts();
}

// --- Problems of the whole package ---------------------------------------------

void MaterialWorkbench::setWholePackageProblems(bool whole)
{
	if (m_problemScope->isChecked() != whole) {
		const QSignalBlocker blocker(m_problemScope);
		m_problemScope->setChecked(whole);
	}
	m_wholeProblems = whole;
	if (whole) {
		updateWholeProblems();
	} else {
		reparse(false);
	}
	scheduleStateSave();
}

void MaterialWorkbench::updateWholeProblems()
{
	m_problems->clear();
	if (!m_library) {
		return;
	}
	int count = 0;
	int shown = 0;
	for (int entry = 0; entry < m_library->entries.size() && shown < kWholeProblemLimit; ++entry) {
		const MaterialLibraryEntry& item = m_library->entries.at(entry);
		if (item.errors == 0 && item.warnings == 0 && !item.rejected) {
			continue;
		}
		const MaterialDefinition definition = m_library->definition(entry);
		QVector<MaterialDiagnostic> diagnostics = definition.diagnostics;
		if (!definition.engineRejection.isEmpty()
			&& std::none_of(diagnostics.cbegin(), diagnostics.cend(), [&](const MaterialDiagnostic& each) { return each.message == definition.engineRejection; })) {
			diagnostics.push_back(diagnosticOf(MaterialDiagnosticSeverity::Error, QStringLiteral("engine-rejects"), definition.engineRejection));
			diagnostics.last().line = definition.span.line;
		}
		for (const MaterialDiagnostic& diagnostic : std::as_const(diagnostics)) {
			if (diagnostic.severity == MaterialDiagnosticSeverity::Info || shown >= kWholeProblemLimit) {
				continue;
			}
			auto* row = new QTreeWidgetItem(m_problems);
			row->setIcon(0, studioIcon(diagnostic.severity == MaterialDiagnosticSeverity::Error ? QStringLiteral("error") : QStringLiteral("warning")));
			row->setText(0, severityName(diagnostic.severity));
			row->setText(1, diagnostic.line > 0 ? QString::number(diagnostic.line) : QString());
			const QString message = tr("%1: %2").arg(item.name, diagnostic.message);
			row->setText(2, message);
			row->setToolTip(2, item.sourcePath.isEmpty() ? message : tr("%1\n%2").arg(message, item.sourcePath));
			row->setData(0, Qt::UserRole, diagnostic.line);
			row->setData(1, Qt::UserRole, item.name);
			row->setData(2, Qt::UserRole, entry);
			++count;
			++shown;
		}
	}
	if (shown >= kWholeProblemLimit) {
		auto* row = new QTreeWidgetItem(m_problems);
		row->setText(2, tr("Only the first %1 problems are listed; the filter errors>0 finds the rest.").arg(kWholeProblemLimit));
	}
	m_problemCount = count;
	refreshTabTexts();
}

// --- Preview settings ------------------------------------------------------------

void MaterialWorkbench::showPreviewSettings()
{
	buildPreviewSettings(false);
}

void MaterialWorkbench::buildPreviewSettings(bool focusReset)
{
	// A dialog already open is replaced where it is (never deleted while one
	// of its own buttons is still being handled).
	QRect geometry;
	bool keepGeometry = false;
	if (m_previewSettings) {
		keepGeometry = m_previewSettings->isVisible();
		geometry = m_previewSettings->geometry();
		m_previewSettings->hide();
		m_previewSettings->deleteLater();
		m_previewSettings = nullptr;
	}
	auto* dialog = new QDialog(this);
	m_previewSettings = dialog;
	m_previewSettingsEngine = m_definition.engine;
	dialog->setObjectName(QStringLiteral("materialPreviewSettings"));
	dialog->setWindowTitle(tr("Preview Settings"));
	dialog->setAccessibleName(tr("Preview settings"));
	dialog->setAttribute(Qt::WA_DeleteOnClose, false);
	const MaterialRenderOptions options = m_preview->renderOptions();
	const auto change = [this](const std::function<void(MaterialRenderOptions&)>& apply) {
		MaterialRenderOptions next = m_preview->renderOptions();
		apply(next);
		m_preview->setRenderOptions(next);
		scheduleStateSave();
	};
	auto* form = new QFormLayout;
	const auto number = [&](const QString& label, const QString& accessible, double minimum, double maximum, double step, int decimals, double value,
							const std::function<void(MaterialRenderOptions&, double)>& apply, const QString& special = QString()) {
		auto* box = new QDoubleSpinBox(dialog);
		box->setRange(minimum, maximum);
		box->setSingleStep(step);
		box->setDecimals(decimals);
		box->setValue(value);
		box->setAccessibleName(accessible);
		if (!special.isEmpty()) {
			box->setSpecialValueText(special);
		}
		connect(box, &QDoubleSpinBox::valueChanged, this, [change, apply](double next) { change([&](MaterialRenderOptions& o) { apply(o, next); }); });
		form->addRow(label, box);
		return box;
	};
	const auto check = [&](const QString& label, bool value, const std::function<void(MaterialRenderOptions&, bool)>& apply) {
		auto* box = new QCheckBox(label, dialog);
		box->setChecked(value);
		connect(box, &QCheckBox::toggled, this, [change, apply](bool on) { change([&](MaterialRenderOptions& o) { apply(o, on); }); });
		form->addRow(QString(), box);
		return box;
	};
	const auto colour = [&](const QString& label, const QString& accessible, const QColor& value, const std::function<void(MaterialRenderOptions&, const QColor&)>& apply) {
		auto* button = new QPushButton(dialog);
		button->setAccessibleName(accessible);
		const auto show = [button](const QColor& shown) { button->setText(shown.name(QColor::HexArgb).toUpper()); };
		show(value);
		connect(button, &QPushButton::clicked, this, [this, button, change, apply, show, label]() {
			const QColor chosen = QColorDialog::getColor(QColor(button->text()), this, label, QColorDialog::ShowAlphaChannel);
			if (chosen.isValid()) {
				show(chosen);
				change([&](MaterialRenderOptions& o) { apply(o, chosen); });
			}
		});
		form->addRow(label, button);
		return button;
	};
	number(tr("Tiling"), tr("Texture repeats across the shape"), 0.0, 64.0, 0.5, 1, options.tiling,
		[](MaterialRenderOptions& o, double v) { o.tiling = v; }, tr("Automatic"));
	number(tr("Field of view"), tr("Field of view in degrees"), 10.0, 150.0, 5.0, 0, options.fieldOfView,
		[](MaterialRenderOptions& o, double v) { o.fieldOfView = v; });
	check(tr("Orthographic (face-on, no perspective)"), options.orthographic, [](MaterialRenderOptions& o, bool on) { o.orthographic = on; });
	colour(tr("Background"), tr("Background colour"), options.background, [](MaterialRenderOptions& o, const QColor& c) { o.background = c; });
	check(tr("Checkerboard behind"), options.checker, [](MaterialRenderOptions& o, bool on) { o.checker = on; });
	switch (m_definition.engine) {
	case MaterialEngine::Quake3: {
		const QColor entity = QColor::fromRgbF(static_cast<float>(std::clamp(options.eval.parms[0], 0.0, 1.0)),
			static_cast<float>(std::clamp(options.eval.parms[1], 0.0, 1.0)), static_cast<float>(std::clamp(options.eval.parms[2], 0.0, 1.0)),
			static_cast<float>(std::clamp(options.eval.parms[3], 0.0, 1.0)));
		colour(tr("Entity colour"), tr("Entity colour for rgbGen entity and alphaGen entity"), entity, [](MaterialRenderOptions& o, const QColor& c) {
			o.eval.parms[0] = c.redF();
			o.eval.parms[1] = c.greenF();
			o.eval.parms[2] = c.blueF();
			o.eval.parms[3] = c.alphaF();
		});
		number(tr("Light grid ambient"), tr("Light grid ambient level"), 0, 255, 8, 0, options.lighting.gridAmbient,
			[](MaterialRenderOptions& o, double v) { o.lighting.gridAmbient = static_cast<int>(v); });
		number(tr("Light grid directed"), tr("Light grid directed level"), 0, 255, 8, 0, options.lighting.gridDirected,
			[](MaterialRenderOptions& o, double v) { o.lighting.gridDirected = static_cast<int>(v); });
		break;
	}
	case MaterialEngine::Doom:
		number(tr("Extra light"), tr("Doom extra light, as a gun flash adds"), 0, 2, 1, 0, options.lighting.doomExtraLight,
			[](MaterialRenderOptions& o, double v) { o.lighting.doomExtraLight = static_cast<int>(v); });
		break;
	case MaterialEngine::Quake:
		number(tr("Lightmap level"), tr("Quake lightmap level"), 0, 255, 8, 0, options.lighting.quakeLightmap,
			[](MaterialRenderOptions& o, double v) { o.lighting.quakeLightmap = static_cast<int>(v); });
		break;
	case MaterialEngine::Doom3: {
		number(tr("Light radius"), tr("Light radius in units"), 0, 4096, 16, 0, options.lighting.lightRadius,
			[](MaterialRenderOptions& o, double v) { o.lighting.lightRadius = v; }, tr("Sized to the shape"));
		number(tr("Light angle"), tr("Light angle in degrees when it does not orbit"), -360, 360, 5, 0, options.lighting.lightAngle,
			[](MaterialRenderOptions& o, double v) { o.lighting.lightAngle = v; });
		number(tr("Orbit time"), tr("Seconds the light takes to orbit"), 1, 120, 1, 0, options.lighting.lightOrbitSeconds,
			[](MaterialRenderOptions& o, double v) { o.lighting.lightOrbitSeconds = v; });
		number(tr("Sound level"), tr("Entity sound amplitude for the sound expression"), 0, 1, 0.05, 2, options.eval.sound,
			[](MaterialRenderOptions& o, double v) { o.eval.sound = v; });
		auto* parms = new QWidget(dialog);
		auto* grid = new QGridLayout(parms);
		grid->setContentsMargins(0, 0, 0, 0);
		for (int parm = 0; parm < static_cast<int>(options.eval.parms.size()); ++parm) {
			auto* box = new QDoubleSpinBox(parms);
			box->setRange(-1.0e6, 1.0e6);
			box->setDecimals(3);
			box->setSingleStep(0.1);
			box->setValue(options.eval.parms[static_cast<size_t>(parm)]);
			box->setAccessibleName(tr("Shader parm %1").arg(parm));
			box->setToolTip(tr("parm%1").arg(parm));
			connect(box, &QDoubleSpinBox::valueChanged, this, [change, parm](double v) {
				change([&](MaterialRenderOptions& o) { o.eval.parms[static_cast<size_t>(parm)] = v; });
			});
			grid->addWidget(box, parm / 4, parm % 4);
		}
		form->addRow(tr("Shader parms 0-11"), parms);
		auto* globals = new QWidget(dialog);
		auto* globalGrid = new QGridLayout(globals);
		globalGrid->setContentsMargins(0, 0, 0, 0);
		for (int global = 0; global < static_cast<int>(options.eval.globals.size()); ++global) {
			auto* box = new QDoubleSpinBox(globals);
			box->setRange(-1.0e6, 1.0e6);
			box->setDecimals(3);
			box->setSingleStep(0.1);
			box->setValue(options.eval.globals[static_cast<size_t>(global)]);
			box->setAccessibleName(tr("Global %1").arg(global));
			box->setToolTip(tr("global%1").arg(global));
			connect(box, &QDoubleSpinBox::valueChanged, this, [change, global](double v) {
				change([&](MaterialRenderOptions& o) { o.eval.globals[static_cast<size_t>(global)] = v; });
			});
			globalGrid->addWidget(box, global / 4, global % 4);
		}
		form->addRow(tr("Globals 0-7"), globals);
		break;
	}
	case MaterialEngine::Quake2:
	case MaterialEngine::Unknown:
		break;
	}
	if (m_definition.engine == MaterialEngine::Quake2 || m_definition.engine == MaterialEngine::Doom) {
		auto* sky = new QLineEdit(m_skyName, dialog);
		sky->setAccessibleName(tr("Sky for sky surfaces"));
		sky->setPlaceholderText(m_definition.engine == MaterialEngine::Doom ? QStringLiteral("SKY1") : QStringLiteral("unit1_"));
		sky->setToolTip(m_definition.engine == MaterialEngine::Doom ? tr("The wall texture F_SKY1 shows.")
																	 : tr("The env/ sky box sky surfaces show, as a map's sky key names it."));
		connect(sky, &QLineEdit::editingFinished, this, [this, sky]() {
			if (sky->text().trimmed() != m_skyName) {
				m_skyName = sky->text().trimmed();
				resolveImages(true);
			}
		});
		form->addRow(tr("Sky"), sky);
	}
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close | QDialogButtonBox::Reset, dialog);
	connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
	connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, this, [this]() {
		// The defaults, keeping the shape and the Engine View choices.
		MaterialRenderOptions defaults;
		MaterialRenderOptions next = m_preview->renderOptions();
		next.tiling = defaults.tiling;
		next.fieldOfView = defaults.fieldOfView;
		next.orthographic = defaults.orthographic;
		next.background = defaults.background;
		next.checker = defaults.checker;
		next.eval = defaults.eval;
		next.lighting.gridAmbient = defaults.lighting.gridAmbient;
		next.lighting.gridDirected = defaults.lighting.gridDirected;
		next.lighting.doomExtraLight = defaults.lighting.doomExtraLight;
		next.lighting.quakeLightmap = defaults.lighting.quakeLightmap;
		next.lighting.lightRadius = defaults.lighting.lightRadius;
		next.lighting.lightAngle = defaults.lighting.lightAngle;
		next.lighting.lightOrbitSeconds = defaults.lighting.lightOrbitSeconds;
		m_preview->setRenderOptions(next);
		scheduleStateSave();
		// The fields show the defaults once this click has been handled; the
		// Reset button keeps the keyboard focus.
		QTimer::singleShot(0, this, [this]() { buildPreviewSettings(true); });
	});
	auto* layout = new QVBoxLayout(dialog);
	auto* note = new QLabel(tr("Changes show in the preview as you make them."), dialog);
	note->setWordWrap(true);
	note->setForegroundRole(QPalette::PlaceholderText);
	layout->addWidget(note);
	layout->addLayout(form);
	layout->addWidget(buttons);
	if (keepGeometry) {
		dialog->setGeometry(geometry);
	}
	dialog->show();
	dialog->raise();
	if (focusReset) {
		buttons->button(QDialogButtonBox::Reset)->setFocus(Qt::OtherFocusReason);
	}
}

// --- Menus -----------------------------------------------------------------------

void MaterialWorkbench::rebuildLibraryMenu()
{
	m_libraryMenu->clear();
	QMenu* sort = m_libraryMenu->addMenu(tr("Sort"));
	auto* orders = new QActionGroup(sort);
	using Order = MaterialLibraryFilter::Order;
	const QVector<std::pair<Order, QString>> choices {{Order::Library, tr("As the Game Reads Them")}, {Order::Name, tr("By Name")},
		{Order::Engine, tr("By Engine")}, {Order::Source, tr("By Script or File")}, {Order::Problems, tr("Problems First")},
		{Order::MostUsed, tr("Most Used in Maps")}};
	for (const auto& [order, label] : choices) {
		QAction* action = sort->addAction(label);
		action->setCheckable(true);
		action->setChecked(m_proxy->order() == order);
		orders->addAction(action);
		connect(action, &QAction::triggered, this, [this, order = order]() {
			m_proxy->setOrder(order);
			if (order == Order::MostUsed && (m_usageStale || !m_usage)) {
				scanUsage();
			}
			m_visibleTimer->start();
			scheduleStateSave();
		});
	}
	QMenu* size = m_libraryMenu->addMenu(tr("Swatch Size"));
	auto* sizes = new QActionGroup(size);
	for (const auto& [side, label] : QVector<std::pair<int, QString>> {{48, tr("Small")}, {72, tr("Medium")}, {112, tr("Large")}, {160, tr("Extra Large")}}) {
		QAction* action = size->addAction(label);
		action->setCheckable(true);
		action->setChecked(m_swatchSide == side);
		sizes->addAction(action);
		connect(action, &QAction::triggered, this, [this, side = side]() {
			m_swatchSide = side;
			m_model->setThumbnailSide(side);
			if (!m_viewMode->isChecked()) {
				m_libraryView->setIconSize(QSize(side, side));
				m_libraryView->setGridSize(libraryGridSize());
				m_model->setItemSize(libraryGridSize() - QSize(6, 6));
			}
			m_visibleTimer->start();
			scheduleStateSave();
		});
	}
	m_libraryMenu->addSeparator();
	QAction* base = m_libraryMenu->addAction(tr("Show Base Game Materials"));
	base->setCheckable(true);
	base->setChecked(m_showBase);
	base->setEnabled(!m_basePaths.isEmpty());
	base->setToolTip(m_basePaths.isEmpty() ? tr("No base packages: choose the game installation in the project or Settings.")
										   : tr("List the base packages' materials too (read only), beside the package's own."));
	connect(base, &QAction::toggled, this, &MaterialWorkbench::setShowBaseMaterials);
	QAction* shadowed = m_libraryMenu->addAction(tr("Show Shadowed Definitions"));
	shadowed->setCheckable(true);
	shadowed->setChecked(m_proxy->showShadowed());
	connect(shadowed, &QAction::toggled, this, [this](bool show) { applyFilter([this, show]() { m_proxy->setShowShadowed(show); }); });
	if (m_library && std::any_of(m_library->scripts.cbegin(), m_library->scripts.cend(),
						 [](const MaterialScript& script) { return script.engine == MaterialEngine::Quake3; })) {
		QMenu* game = m_libraryMenu->addMenu(tr("Quake III Game"));
		auto* games = new QActionGroup(game);
		QVector<MaterialDialect> dialects {MaterialDialect::Automatic};
		dialects += materialDialects();
		for (MaterialDialect each : std::as_const(dialects)) {
			QAction* action = game->addAction(each == MaterialDialect::Automatic ? tr("Automatic (%1)").arg(materialDialectDisplayName(dialect()))
																				 : materialDialectDisplayName(each));
			action->setCheckable(true);
			action->setChecked(each == m_dialectChoice);
			games->addAction(action);
			connect(action, &QAction::triggered, this, [this, each]() { setDialect(each); });
		}
	}
}

void MaterialWorkbench::rebuildMoreMenu()
{
	m_moreMenu->clear();
	const bool script = (m_document.kind == MaterialTextKind::Quake3Shader || m_document.kind == MaterialTextKind::Doom3Material) && !m_document.readOnly;
	const bool named = !m_materialName.isEmpty();
	QAction* saveEverything = m_moreMenu->addAction(studioIcon(QStringLiteral("save")), tr("Save All"));
	saveEverything->setEnabled(hasUnsavedChanges());
	saveEverything->setToolTip(tr("Save every material text with unsaved changes."));
	saveEverything->setShortcut(QKeySequence(Qt::ControlModifier | Qt::AltModifier | Qt::Key_S));
	saveEverything->setShortcutContext(Qt::WidgetShortcut);
	connect(saveEverything, &QAction::triggered, this, [this]() { saveAll(); });
	// Texts with unsaved changes that are not open: go back to one, or
	// throw its changes away.
	QMenu* unsaved = m_moreMenu->addMenu(tr("Unsaved Texts"));
	unsaved->setToolTipsVisible(true);
	unsaved->setEnabled(!m_buffers.isEmpty());
	const QString package = packageIdentity();
	for (auto it = m_bufferDocuments.cbegin(); it != m_bufferDocuments.cend(); ++it) {
		if (!m_buffers.contains(it.key())) {
			continue;
		}
		const QString key = it.key();
		const Document& document = it.value();
		const bool elsewhere = document.filePath.isEmpty() && !document.package.isEmpty() && document.package != package;
		QMenu* text = unsaved->addMenu(elsewhere ? tr("%1 (in %2)").arg(document.title, QFileInfo(document.packagePath).fileName()) : document.title);
		QAction* show = text->addAction(tr("Show"));
		// A text of another package is shown in that package, not over this one.
		show->setEnabled(!elsewhere);
		show->setToolTip(elsewhere ? tr("Open %1 again to go on editing or save this text.").arg(QDir::toNativeSeparators(document.packagePath))
								   : tr("Go back to this text where you left it."));
		connect(show, &QAction::triggered, this, [this, key]() { showBufferedText(key); });
		QAction* discard = text->addAction(studioIcon(QStringLiteral("delete")), tr("Discard Changes..."));
		connect(discard, &QAction::triggered, this, [this, key]() {
			const QString title = m_bufferDocuments.value(key).title;
			if (QMessageBox::question(this, tr("Discard Changes"), tr("Throw away the unsaved changes to %1? This cannot be undone.").arg(title))
				== QMessageBox::Yes) {
				discardBufferedText(key);
			}
		});
	}
	QAction* saveAs = m_moreMenu->addAction(tr("Save Script As..."));
	saveAs->setEnabled(m_document.kind == MaterialTextKind::Quake3Shader || m_document.kind == MaterialTextKind::Doom3Material);
	connect(saveAs, &QAction::triggered, this, &MaterialWorkbench::chooseSaveAs);
	m_moreMenu->addSeparator();
	QAction* rename = m_moreMenu->addAction(studioIcon(QStringLiteral("rename")), tr("Rename Material..."));
	rename->setEnabled(script && named);
	connect(rename, &QAction::triggered, this, [this]() {
		bool ok = false;
		const QString name = QInputDialog::getText(this, tr("Rename Material"), tr("New name"), QLineEdit::Normal, m_materialName, &ok);
		if (ok && !name.trimmed().isEmpty() && name.trimmed() != m_materialName) {
			renameCurrentMaterial(name);
		}
	});
	QAction* duplicate = m_moreMenu->addAction(studioIcon(QStringLiteral("copy")), tr("Duplicate Material..."));
	duplicate->setEnabled(script && named);
	connect(duplicate, &QAction::triggered, this, [this]() {
		bool ok = false;
		const QString name = QInputDialog::getText(this, tr("Duplicate Material"), tr("Name of the copy"), QLineEdit::Normal,
			m_materialName + QStringLiteral("_copy"), &ok);
		if (ok && !name.trimmed().isEmpty()) {
			duplicateCurrentMaterial(name);
		}
	});
	QAction* remove = m_moreMenu->addAction(studioIcon(QStringLiteral("delete")), tr("Delete Material"));
	remove->setEnabled(script && named);
	connect(remove, &QAction::triggered, this, [this]() {
		if (QMessageBox::question(this, tr("Delete Material"), tr("Remove %1 from the text? Undo brings it back until you save.").arg(m_materialName))
			== QMessageBox::Yes) {
			deleteCurrentMaterial();
		}
	});
	QAction* override = m_moreMenu->addAction(studioIcon(QStringLiteral("import")), tr("Override in Package"));
	override->setEnabled(currentIsBase() && m_reader);
	override->setToolTip(tr("Copy this base game material into the package, where it wins over the base game's."));
	connect(override, &QAction::triggered, this, [this]() { overrideInPackage(); });
	m_moreMenu->addSeparator();
	QAction* copyName = m_moreMenu->addAction(tr("Copy Name"));
	copyName->setEnabled(named);
	connect(copyName, &QAction::triggered, this, [this]() { QApplication::clipboard()->setText(m_materialName); });
	QAction* copyImage = m_moreMenu->addAction(tr("Copy Preview Image"));
	copyImage->setEnabled(!m_preview->lastImage().isNull());
	connect(copyImage, &QAction::triggered, this, [this]() { QApplication::clipboard()->setImage(m_preview->lastImage()); });
	QAction* exportImage = m_moreMenu->addAction(studioIcon(QStringLiteral("export")), tr("Export Preview Image..."));
	exportImage->setEnabled(named && m_images != nullptr);
	connect(exportImage, &QAction::triggered, this, [this]() { chooseExportImage(false); });
	QAction* exportSheet = m_moreMenu->addAction(tr("Export Animation Sheet..."));
	exportSheet->setEnabled(named && m_images != nullptr && m_definition.isAnimated());
	connect(exportSheet, &QAction::triggered, this, [this]() { chooseExportImage(true); });
	m_moreMenu->addSeparator();
	const QString file = scriptFilePath();
	QAction* code = m_moreMenu->addAction(studioIcon(QStringLiteral("code")), tr("Open Script in Code"));
	code->setEnabled(!file.isEmpty() && m_host.openInCode);
	code->setToolTip(file.isEmpty() ? tr("Only scripts that are files on disk open in Code.") : QDir::toNativeSeparators(file));
	connect(code, &QAction::triggered, this, [this, file]() { m_host.openInCode(file, std::max(1, m_definition.span.line)); });
	const QString packagePath = m_document.virtualPath;
	QAction* reveal = m_moreMenu->addAction(studioIcon(QStringLiteral("package")), tr("Show in Packages"));
	reveal->setEnabled(!packagePath.isEmpty() && !m_document.readOnly && m_host.revealInPackage && !m_document.isNew
		&& m_document.package == packageIdentity());
	connect(reveal, &QAction::triggered, this, [this, packagePath]() { m_host.revealInPackage(packagePath); });
	QAction* uses = m_moreMenu->addAction(studioIcon(QStringLiteral("map")), tr("Find Uses in Maps"));
	uses->setEnabled(named && m_reader);
	connect(uses, &QAction::triggered, this, [this]() { m_tabs->setCurrentIndex(kMapsTab); });
	QAction* settings = m_moreMenu->addAction(studioIcon(QStringLiteral("settings")), tr("Preview Settings..."));
	connect(settings, &QAction::triggered, this, &MaterialWorkbench::showPreviewSettings);
}

void MaterialWorkbench::showLibraryContextMenu(const QPoint& position)
{
	showLibraryContextMenuFor(m_libraryView->indexAt(position), position);
}

bool MaterialWorkbench::eventFilter(QObject* watched, QEvent* event)
{
	if (m_libraryView && (watched == m_libraryView || watched == m_libraryView->viewport())) {
		if (event->type() == QEvent::KeyPress && watched == m_libraryView) {
			// Enter (not a click, which some styles report as activation too)
			// moves on to the material's preview.
			const auto* key = static_cast<QKeyEvent*>(event);
			if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && key->modifiers() == Qt::NoModifier
				&& m_libraryView->currentIndex().isValid()) {
				m_preview->setFocus(Qt::OtherFocusReason);
				return true;
			}
		}
		if (event->type() == QEvent::ContextMenu) {
			// From the keyboard (Menu key, Shift+F10) the menu is for the
			// current material, wherever the pointer happens to be.
			const auto* menuEvent = static_cast<QContextMenuEvent*>(event);
			if (menuEvent->reason() == QContextMenuEvent::Keyboard) {
				const QModelIndex current = m_libraryView->currentIndex();
				if (current.isValid()) {
					showLibraryContextMenuFor(current, m_libraryView->visualRect(current).center());
				}
				return true;
			}
		}
	}
	return QWidget::eventFilter(watched, event);
}

void MaterialWorkbench::showLibraryContextMenuFor(const QModelIndex& index, const QPoint& position)
{
	if (!index.isValid()) {
		return;
	}
	const int entry = m_proxy->mapToSource(index).row();
	if (entry < 0 || !m_library || entry >= m_library->entries.size()) {
		return;
	}
	if (m_libraryView->currentIndex() != index) {
		m_libraryView->setCurrentIndex(index);
	}
	if (m_library->entries.at(entry).name != m_materialName) {
		selectEntry(entry);
	}
	rebuildMoreMenu();
	QMenu menu(this);
	for (QAction* action : m_moreMenu->actions()) {
		menu.addAction(action);
	}
	const QStringList images = m_library->entries.at(entry).images;
	if (!images.isEmpty()) {
		menu.addSeparator();
		const QString first = images.first();
		QAction* show = menu.addAction(studioIcon(QStringLiteral("image")), tr("Show Image in Textures"));
		show->setEnabled(static_cast<bool>(m_host.showTexture));
		connect(show, &QAction::triggered, this, [this, first]() { m_host.showTexture(first); });
	}
	menu.exec(m_libraryView->viewport()->mapToGlobal(position));
}

void MaterialWorkbench::showImagesContextMenu(const QPoint& position)
{
	QTreeWidgetItem* item = m_imageList->itemAt(position);
	if (!item) {
		return;
	}
	const QString reference = item->data(0, Qt::UserRole).toString();
	const MaterialTexturePtr texture = m_images ? m_images->find(reference) : MaterialTexturePtr();
	const QString path = texture && !texture->path.isEmpty() ? texture->path : reference;
	QMenu menu(this);
	QAction* show = menu.addAction(studioIcon(QStringLiteral("image")), tr("Show in Textures"));
	show->setEnabled(static_cast<bool>(m_host.showTexture));
	connect(show, &QAction::triggered, this, [this, reference]() { m_host.showTexture(reference); });
	QAction* edit = menu.addAction(studioIcon(QStringLiteral("paint")), tr("Edit Image in Texture Editor"));
	edit->setEnabled(static_cast<bool>(m_host.editImage) && texture && texture->usable() && texture->status == QStringLiteral("image"));
	connect(edit, &QAction::triggered, this, [this, path]() { m_host.editImage(path); });
	QAction* reveal = menu.addAction(studioIcon(QStringLiteral("package")), tr("Show in Packages"));
	reveal->setEnabled(static_cast<bool>(m_host.revealInPackage) && texture && !texture->path.isEmpty());
	connect(reveal, &QAction::triggered, this, [this, path]() { m_host.revealInPackage(path); });
	QAction* copy = menu.addAction(tr("Copy Path"));
	connect(copy, &QAction::triggered, this, [path]() { QApplication::clipboard()->setText(path); });
	menu.exec(m_imageList->viewport()->mapToGlobal(position));
}

// --- State kept between sessions ---------------------------------------------------

void MaterialWorkbench::scheduleStateSave()
{
	if (!m_restoringState && m_host.saveState) {
		m_stateTimer->start();
	}
}

QByteArray MaterialWorkbench::saveState() const
{
	QJsonObject state;
	state.insert(QStringLiteral("version"), 1);
	state.insert(QStringLiteral("list"), m_viewMode->isChecked());
	state.insert(QStringLiteral("animateSwatches"), m_animateSwatches->isChecked());
	state.insert(QStringLiteral("swatch"), m_swatchSide);
	state.insert(QStringLiteral("order"), static_cast<int>(m_proxy->order()));
	state.insert(QStringLiteral("showBase"), m_showBase);
	state.insert(QStringLiteral("dialect"), materialDialectId(m_dialectChoice));
	state.insert(QStringLiteral("wholeProblems"), m_wholeProblems);
	state.insert(QStringLiteral("tab"), m_tabs->currentIndex());
	state.insert(QStringLiteral("mainSplitter"), QString::fromLatin1(m_mainSplitter->saveState().toBase64()));
	state.insert(QStringLiteral("editorSplitter"), QString::fromLatin1(m_editorSplitter->saveState().toBase64()));
	state.insert(QStringLiteral("graphSplitter"), QString::fromLatin1(m_graphSplitter->saveState().toBase64()));
	const MaterialRenderOptions options = m_preview->renderOptions();
	QJsonObject preview;
	preview.insert(QStringLiteral("filtering"), materialFilteringId(options.filtering));
	preview.insert(QStringLiteral("checker"), options.checker);
	preview.insert(QStringLiteral("background"), options.background.name(QColor::HexArgb));
	preview.insert(QStringLiteral("tiling"), options.tiling);
	preview.insert(QStringLiteral("fieldOfView"), options.fieldOfView);
	preview.insert(QStringLiteral("orthographic"), options.orthographic);
	preview.insert(QStringLiteral("overbright"), options.lighting.overbright);
	preview.insert(QStringLiteral("lightmapSpot"), options.lighting.lightmapSpot);
	preview.insert(QStringLiteral("lightOrbit"), options.lighting.lightOrbit);
	preview.insert(QStringLiteral("doom3Specular"), options.lighting.doom3Specular);
	preview.insert(QStringLiteral("doom3Bfg"), options.lighting.doom3Shading == MaterialDoom3Shading::Bfg);
	preview.insert(QStringLiteral("honourRejection"), options.honourRejection);
	state.insert(QStringLiteral("preview"), preview);
	return QJsonDocument(state).toJson(QJsonDocument::Compact);
}

void MaterialWorkbench::restoreState(const QByteArray& bytes)
{
	const QJsonObject state = QJsonDocument::fromJson(bytes).object();
	if (state.value(QStringLiteral("version")).toInt() != 1) {
		return;
	}
	m_restoringState = true;
	m_viewMode->setChecked(state.value(QStringLiteral("list")).toBool());
	m_animateSwatches->setChecked(state.value(QStringLiteral("animateSwatches")).toBool());
	const int swatch = std::clamp(state.value(QStringLiteral("swatch")).toInt(72), 32, 192);
	if (swatch != m_swatchSide) {
		m_swatchSide = swatch;
		m_model->setThumbnailSide(swatch);
		if (!m_viewMode->isChecked()) {
			m_libraryView->setIconSize(QSize(swatch, swatch));
			m_libraryView->setGridSize(libraryGridSize());
			m_model->setItemSize(libraryGridSize() - QSize(6, 6));
		}
	}
	const int order = state.value(QStringLiteral("order")).toInt();
	if (order >= 0 && order <= static_cast<int>(MaterialLibraryFilter::Order::MostUsed)) {
		m_proxy->setOrder(static_cast<MaterialLibraryFilter::Order>(order));
	}
	m_showBase = state.value(QStringLiteral("showBase")).toBool(true);
	MaterialDialect dialect = MaterialDialect::Automatic;
	if (materialDialectFromId(state.value(QStringLiteral("dialect")).toString(), &dialect)) {
		m_dialectChoice = dialect;
	}
	if (state.value(QStringLiteral("wholeProblems")).toBool()) {
		m_problemScope->setChecked(true);
	}
	const int tab = state.value(QStringLiteral("tab")).toInt();
	if (tab >= 0 && tab < m_tabs->count() && tab != kMapsTab) {
		m_tabs->setCurrentIndex(tab);
	}
	m_mainSplitter->restoreState(QByteArray::fromBase64(state.value(QStringLiteral("mainSplitter")).toString().toLatin1()));
	m_editorSplitter->restoreState(QByteArray::fromBase64(state.value(QStringLiteral("editorSplitter")).toString().toLatin1()));
	m_graphSplitter->restoreState(QByteArray::fromBase64(state.value(QStringLiteral("graphSplitter")).toString().toLatin1()));
	const QJsonObject preview = state.value(QStringLiteral("preview")).toObject();
	MaterialRenderOptions options = m_preview->renderOptions();
	MaterialFiltering filtering = options.filtering;
	if (materialFilteringFromId(preview.value(QStringLiteral("filtering")).toString(), &filtering)) {
		options.filtering = filtering;
	}
	options.checker = preview.value(QStringLiteral("checker")).toBool(options.checker);
	const QColor background = QColor::fromString(preview.value(QStringLiteral("background")).toString());
	if (background.isValid()) {
		options.background = background;
	}
	options.tiling = std::clamp(preview.value(QStringLiteral("tiling")).toDouble(options.tiling), 0.0, 64.0);
	options.fieldOfView = std::clamp(preview.value(QStringLiteral("fieldOfView")).toDouble(options.fieldOfView), 10.0, 150.0);
	options.orthographic = preview.value(QStringLiteral("orthographic")).toBool(options.orthographic);
	options.lighting.overbright = preview.value(QStringLiteral("overbright")).toBool(options.lighting.overbright);
	options.lighting.lightmapSpot = preview.value(QStringLiteral("lightmapSpot")).toBool(options.lighting.lightmapSpot);
	options.lighting.lightOrbit = preview.value(QStringLiteral("lightOrbit")).toBool(options.lighting.lightOrbit);
	options.lighting.doom3Specular = preview.value(QStringLiteral("doom3Specular")).toBool(options.lighting.doom3Specular);
	options.lighting.doom3Shading = preview.value(QStringLiteral("doom3Bfg")).toBool() ? MaterialDoom3Shading::Bfg : MaterialDoom3Shading::Vanilla;
	options.honourRejection = preview.value(QStringLiteral("honourRejection")).toBool(options.honourRejection);
	m_preview->setRenderOptions(options);
	m_restoringState = false;
}

} // namespace slopstudio
