#pragma once

// The Materials page: every texture, shader and material of idTech 1-4 in
// the open package or script, a live preview drawn the way the game draws
// it, and two editors over one text: the script itself and a node graph.
//
// The text is the single source: a graph or property edit becomes a text
// edit (core/material_graph.h) applied to the editor as one undo step, and
// every text change is parsed again to update the preview, the graph, the
// images and the problems. Saving writes a loose script, or stages the
// script, the Doom lumps or the Quake II WAL into the open package.
//
// Base packages (a game installation's, or a PWAD's IWAD) sit beneath the
// package: their images, tables and Doom lumps are used, and their
// materials are listed read-only, to override in the package. The Maps tab
// lists the maps that use the material, and opens them in Levels.

#include "core/material_classic.h"
#include "core/material_graph.h"
#include "core/material_images.h"
#include "core/material_library.h"
#include "core/material_render.h"
#include "core/material_usage.h"
#include "core/package_archive.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QWidget>

#include <functional>
#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QMenu;
class QSlider;
class QModelIndex;
class QSplitter;
class QTabWidget;
class QTextDocument;
class QTextEdit;
class QTimer;
class QToolButton;
class QTreeWidget;

namespace vibestudio {

class BusyIndicator;
class BusyOverlay;
class LoadingPane;
class MaterialGraphView;
class MaterialLibraryFilter;
class MaterialLibraryModel;
class MaterialPreviewView;
class MaterialPropertyPanel;
class MaterialScriptHighlighter;
class MaterialTaskLane;
class CodeFindBar;
class StudioCodeEditor;
struct MaterialLibraryRows;

// What the workbench asks of the studio around it. A missing hook turns the
// matching action off.
struct MaterialWorkbenchHost {
	// Stages a file into the open package, replacing one at the path.
	std::function<bool(const QByteArray& bytes, const QString& virtualPath, QString* error)> stageFile;
	// Stages a lump into the open WAD, replacing one with the name.
	std::function<bool(const QByteArray& bytes, const QString& lumpName, QString* error)> stageLump;
	std::function<void(const QString& reference)> showTexture;
	std::function<void(const QString& filePath, int line)> openInCode;
	// Opens an image (its package path) in the Texture Editor.
	std::function<void(const QString& virtualPath)> editImage;
	// Opens a map in Levels and selects what uses `material` there.
	std::function<bool(const MaterialMapUse& use, const QString& material, QString* error)> openMapUse;
	// Shows a package entry on the Packages page.
	std::function<void(const QString& virtualPath)> revealInPackage;
	// The page's view settings, kept between sessions (opaque bytes).
	std::function<QByteArray()> loadState;
	std::function<void(const QByteArray& state)> saveState;
	// A base package the studio has read already, by file path, or null: the
	// workbench then does not read and hash the same file a second time.
	std::function<std::shared_ptr<const PackageArchiveReader>(const QString& path)> baseReader;
};

class MaterialWorkbench final : public QWidget {
	Q_OBJECT

public:
	explicit MaterialWorkbench(QWidget* parent = nullptr);
	~MaterialWorkbench() override;

	void setHost(MaterialWorkbenchHost host);
	// Saves a pending view state through the host, then forgets the host: the
	// owner calls it before the host's state goes, as when the studio closes.
	void detachHost();
	// The open package as an immutable snapshot (staged edits included), its
	// path, and a revision that changes when its contents do.
	void setPackage(std::shared_ptr<const PackageArchiveReader> reader, const QString& path, const QString& revision);
	void clearPackage();
	// The revision setPackage() last took; the shell skips copying an unchanged view.
	[[nodiscard]] QString packageRevision() const { return m_revision; }
	// A .shader or .mtr file, read with the open package's images.
	bool openScript(const QString& path, QString* error = nullptr);
	void setReducedMotion(bool reduced);
	// Packages the game mounts beneath the open one, by file path, lowest
	// first. Read with the package; an unreadable one is reported and skipped.
	void setBasePackages(const QStringList& paths);
	[[nodiscard]] QStringList basePackages() const { return m_basePaths; }
	// The Quake III game scripts are read for; Automatic works it out.
	void setDialect(MaterialDialect dialect);
	[[nodiscard]] MaterialDialect dialectChoice() const { return m_dialectChoice; }
	// The dialect the library was read with.
	[[nodiscard]] MaterialDialect dialect() const;
	// Lists base materials, or only the package's own.
	void setShowBaseMaterials(bool show);

	[[nodiscard]] bool hasContent() const;
	// One line for the page header.
	[[nodiscard]] QString summary() const;
	// Scanning, reading images or drawing.
	[[nodiscard]] bool isBusy() const;
	// What keeps it busy: "scanning", "images", "drawing" or "parsing".
	[[nodiscard]] QString busyReason() const;
	[[nodiscard]] bool hasUnsavedChanges() const;
	// The titles of every text with unsaved changes, the open one first.
	[[nodiscard]] QStringList unsavedTextTitles() const;
	// Throws away every unsaved text: the open one goes back to its saved
	// text (no undo step) and the others are forgotten.
	void discardUnsavedChanges();
	// What Save and Ctrl+S do: save the open text, or ask where for a new
	// script; with nothing to save here, say where the other unsaved texts are.
	void saveCurrent();
	bool save(QString* error = nullptr);
	bool saveAs(const QString& path, QString* error = nullptr);
	// Saves the current text and every other text with unsaved changes.
	bool saveAll(QString* error = nullptr);
	void revert();
	// `kind` ("doom-wall" or "doom-flat") picks between a Doom wall and flat
	// of the same name; empty takes either.
	bool selectMaterial(const QString& name, const QString& kind = QString());
	[[nodiscard]] QString currentMaterial() const { return m_materialName; }
	[[nodiscard]] const MaterialDefinition& currentDefinition() const { return m_definition; }
	[[nodiscard]] MaterialTextKind currentTextKind() const;
	[[nodiscard]] QString currentText() const;
	[[nodiscard]] QString currentDocumentTitle() const { return m_document.title; }
	bool applyGraphEdit(const MaterialGraphEdit& edit, QString* error = nullptr);
	// Adds a new material from a template to the current script (or starts
	// a new one) and selects it.
	bool addMaterialFromTemplate(const QString& templateId, const QString& name, const QString& image, QString* error = nullptr);
	// Asks for a template, a name and an image, then adds the material.
	void showNewMaterialDialog();
	// Filters the library to the materials that read `image` (a package
	// path or a lump name) and selects the first.
	void showMaterialsUsingImage(const QString& image);
	// Selects the material a map names (Quake III maps leave out textures/),
	// now or once the library is read. When no material has that name,
	// `missing` (when set) runs with it and returns true when it handled the
	// request (the modeller shows the image instead); otherwise the library
	// is filtered to the names that contain it. `kind` as for selectMaterial:
	// a sidedef names a "doom-wall", a sector plane a "doom-flat".
	void revealMaterial(const QString& name, std::function<bool(const QString&)> missing = {}, const QString& kind = QString());

	// Whole-definition edits of the current material, each one undo step in
	// the text: rename it, add a renamed copy after it, or remove it.
	bool renameCurrentMaterial(const QString& name, QString* error = nullptr);
	bool duplicateCurrentMaterial(const QString& name, QString* error = nullptr);
	bool deleteCurrentMaterial(QString* error = nullptr);
	// Copies a base material into the package, in a script the game reads
	// after (or, for Quake III 1.32 and Doom 3, before) the base scripts, so
	// the copy wins; selects the copy.
	bool overrideInPackage(QString* error = nullptr);
	[[nodiscard]] bool currentIsBase() const;
	// Writes the preview to a PNG: one frame now, or `frames` frames over
	// the animation's cycle as a sheet. Draws on the calling thread; a sheet
	// keeps within 8192 pixels a side, with smaller frames when it must.
	bool exportPreview(const QString& path, int frames = 1, const QSize& size = QSize(), QString* error = nullptr);
	// The same in the background, with a cancellable busy overlay over the
	// preview; the result is reported through statusMessage().
	void exportPreviewInBackground(const QString& path, int frames = 1, const QSize& size = QSize());
	[[nodiscard]] bool exportRunning() const;
	// A base package read for the library, by file path, while its file is
	// unchanged; null otherwise. The shell uses it rather than read it again.
	[[nodiscard]] std::shared_ptr<const PackageArchiveReader> loadedBasePackage(const QString& path) const;
	// How many times a script text was parsed on this page, for tests.
	[[nodiscard]] int scriptParseCount() const { return m_parseCount; }

	// The maps that use materials, read in the background the first time the
	// Maps tab shows or Find Uses in Maps asks.
	void scanUsage();
	// Shows the Maps tab, reading the maps when they have not been read.
	void showMapUses();
	// Asks where to write the preview (or a sheet of frames), then writes it.
	void chooseExportImage(bool sheet);
	[[nodiscard]] std::shared_ptr<const MaterialUsageIndex> usage() const { return m_usage; }
	[[nodiscard]] bool usageScanning() const { return m_usageScanning; }
	// Problems of every material in the package, not only the current one.
	void setWholePackageProblems(bool whole);
	void showPreviewSettings();
	[[nodiscard]] QDialog* previewSettingsDialog() const { return m_previewSettings; }

	[[nodiscard]] QByteArray saveState() const;
	void restoreState(const QByteArray& state);

	// Parts, for the shell's focus handling and for tests.
	[[nodiscard]] MaterialPreviewView* preview() const { return m_preview; }
	[[nodiscard]] MaterialGraphView* graphView() const { return m_graph; }
	[[nodiscard]] MaterialPropertyPanel* propertyPanel() const { return m_properties; }
	[[nodiscard]] StudioCodeEditor* editor() const { return m_editor; }
	[[nodiscard]] QListView* libraryView() const { return m_libraryView; }
	[[nodiscard]] QLineEdit* filterField() const { return m_filter; }
	[[nodiscard]] MaterialLibraryModel* libraryModel() const { return m_model; }
	[[nodiscard]] QTabWidget* editorTabs() const { return m_tabs; }
	[[nodiscard]] QTreeWidget* problemsList() const { return m_problems; }
	[[nodiscard]] QTreeWidget* imagesList() const { return m_imageList; }
	[[nodiscard]] QTreeWidget* mapsList() const { return m_mapList; }
	[[nodiscard]] CodeFindBar* findBar() const { return m_findBar; }
	[[nodiscard]] MaterialLibraryFilter* libraryFilter() const { return m_proxy; }

Q_SIGNALS:
	// hasContent() or summary() changed.
	void contentChanged();
	void statusMessage(const QString& message);
	void unsavedChangesChanged(bool unsaved);
	// The Quake III game chosen for the scripts changed (dialectChoice()).
	void dialectChanged();

protected:
	void changeEvent(QEvent* event) override;
	void showEvent(QShowEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	// The text being edited and where saving puts it.
	struct Document {
		// Unique across packages: the package it was read from is part of it.
		QString key;
		MaterialTextKind kind = MaterialTextKind::None;
		MaterialEngine engine = MaterialEngine::Unknown;
		QString title;
		// A base package's script: shown, not edited (Override in Package).
		bool readOnly = false;
		// A script started here that the package does not hold yet.
		bool isNew = false;
		// The package a staged text belongs to (packageIdentity()); a text of
		// another package is never staged into the open one.
		QString package;
		// The same package's path as the user sees it.
		QString packagePath;
		// A loose file written in place, or a package entry or lump staged.
		QString filePath;
		QString virtualPath;
		// Quake II: the WAL the header text belongs to.
		QByteArray walBytes;
		QString savedText;
		// The library script the text was read from (an index into the
		// library it was read with), for reusing its parse while unchanged.
		int scriptIndex = -1;
		// Where the user was, kept while the text waits in a buffer.
		QString material;
		MaterialEngine materialEngine = MaterialEngine::Unknown;
		QString materialKind;
		int anchor = -1;
		int position = -1;
		int scroll = 0;
	};

	void buildUi();
	QWidget* buildLibraryPanel();
	QWidget* buildPreviewPanel();
	QWidget* buildEditorTabs();
	void retranslate();
	void refreshPlayButton();
	void refreshViewModeButton();
	void refreshTabTexts();
	void applyTheme();

	void requestScan();
	void startScan();
	void applyLibrary(std::shared_ptr<MaterialLibrary> library, const MaterialLibraryRows& rows, int looseScript, const QString& select);
	void updateLibraryCount();
	void updateVisibleRows();
	[[nodiscard]] MaterialImageSource imageSource() const;
	[[nodiscard]] QStringList packageImages() const;

	void selectEntry(int entry);
	// The library entry of the open material (its name, engine and, for Doom,
	// wall or flat); -1 when the library has none.
	[[nodiscard]] int currentEntry() const;
	[[nodiscard]] Document documentFor(int entry, const MaterialDefinition& definition) const;
	// Shows `document` in the editor, keeping the open text (its undo history
	// and place) in a buffer when it has unsaved changes. True when the text
	// came back from a buffer with its place.
	bool switchDocument(const Document& document);
	// A text document of the editor's kind, highlighted for `kind`.
	QTextDocument* createTextDocument(MaterialTextKind kind, const QString& text);
	[[nodiscard]] MaterialScriptHighlighter* highlighterOf(QTextDocument* document) const;
	// Runs highlighter work on the open text without taking it for an edit.
	void highlightQuietly(const std::function<void()>& work);
	void forgetBuffer(const QString& key);
	// Shows a buffered text (from Unsaved Texts) at the place it was left.
	void showBufferedText(const QString& key);
	void discardBufferedText(const QString& key);
	[[nodiscard]] bool currentTextModified() const;
	// Who the open package is, for document keys and save checks.
	[[nodiscard]] QString packageIdentity() const;
	[[nodiscard]] QString scriptKey(const QString& package, const QString& virtualPath) const;
	// Applies the typed filter (after a short pause) without changing the
	// open material when the filter hides it.
	void applyFilter(const std::function<void()>& change);
	// Applies the filter field's text now, without waiting for the pause.
	void flushFilter();
	void setFilterText(const QString& text);
	// Emits contentChanged() when hasContent() or summary() changed.
	void notifyContentChanged();
	void setEditorText(const QString& text);
	// One undo step, or (join) part of the previous one.
	void replaceEditorText(const QString& text, bool join = false);
	void scheduleReparse();
	void reparse(bool scrollToDefinition);
	void updateViews(bool materialChanged, bool scrollToDefinition);
	void resolveImages(bool materialChanged);
	void pushPreview(bool materialChanged);
	void updateGraph();
	void updateProblems(const QVector<MaterialDiagnostic>& diagnostics);
	void updateImagesTab();
	void updateDetails();
	void updateNotes();
	void updateTimeline();
	void rebuildLightingMenu();
	void rebuildLibraryMenu();
	void rebuildMoreMenu();
	void showLibraryContextMenu(const QPoint& position);
	void showLibraryContextMenuFor(const QModelIndex& index, const QPoint& position);
	void buildPreviewSettings(bool focusReset);
	[[nodiscard]] QSize exportFrameSize() const;
	void refreshTimeLabelWidth();
	void finishUsageTracking();
	void showImagesContextMenu(const QPoint& position);
	void updateMapsTab();
	void updateWholeProblems();
	void scheduleStateSave();
	void applyLibraryOrder();
	[[nodiscard]] MaterialDialect scriptDialect(MaterialEngine engine) const;
	// Adds `body` (a whole definition named `name`) to the script at
	// `scriptPath`, in its open text or as a new script, and selects it.
	bool addDefinitionText(MaterialEngine engine, const QString& scriptPath, const QString& name, const QString& body, QString* error);
	bool applyWholeDefinitionEdit(const MaterialEdit& edit, const QString& select, QString* error);
	[[nodiscard]] QString scriptFilePath() const;
	void applyRenderOptions();
	void showLine(int line);
	void refreshDocumentState();
	// Writes or stages one document's text; the saved text is updated.
	bool writeDocument(Document* document, const QString& text, QString* problem);
	void chooseSaveAs();
	bool writeLooseFile(const QString& path, const QByteArray& bytes, QString* error);
	[[nodiscard]] std::shared_ptr<const MaterialTableSet> tablesFor(const MaterialScript& script) const;
	[[nodiscard]] double cycleSeconds() const;
	// Reads a package file by path, ignoring case.
	bool readPackageFile(const QString& path, QByteArray* bytes, QString* error = nullptr) const;
	[[nodiscard]] bool packageIsWad() const;
	[[nodiscard]] QString freeScriptPath(MaterialEngine engine) const;
	void rebuildEngineFilter();
	[[nodiscard]] QSize libraryGridSize() const;
	void rebuildAddNodeMenu(const QVector<MaterialGraphNodeTemplate>& templates);

	MaterialWorkbenchHost m_host;
	std::shared_ptr<const PackageArchiveReader> m_reader;
	QString m_packagePath;
	QString m_revision;
	QString m_looseFile;
	std::shared_ptr<MaterialLibrary> m_library;
	std::shared_ptr<MaterialImageCache> m_cache;
	int m_looseScript = -1;
	bool m_scanning = false;
	bool m_scanPending = false;
	bool m_reducedMotion = false;
	QStringList m_basePaths;
	// Base packages read before, by path and modification time.
	QHash<QString, std::shared_ptr<const PackageArchiveReader>> m_baseReaders;
	MaterialDialect m_dialectChoice = MaterialDialect::Automatic;
	bool m_showBase = true;
	std::shared_ptr<const MaterialUsageIndex> m_usage;
	bool m_usageScanning = false;
	bool m_usageStale = true;
	bool m_wholeProblems = false;
	QString m_skyName;
	bool m_restoringState = false;

	Document m_document;
	// Unsaved texts of documents switched away from, by document key, with
	// their undo history; owned by the workbench. And where each one saves.
	QHash<QString, QTextDocument*> m_buffers;
	QHash<QString, Document> m_bufferDocuments;
	// Set when the open script changed in the package while it was being
	// edited: saving replaces the newer version.
	QString m_documentConflict;
	bool m_currentModified = false;
	int m_textRevision = -1;
	// The last property edit from the graph or the properties, so quick
	// repeats of it join one undo step.
	QString m_lastPropertyEdit;
	QPointer<QTextDocument> m_lastPropertyEditDocument;
	int m_lastPropertyEditSteps = -1;
	QElapsedTimer m_lastPropertyEditClock;
	bool m_applyingPropertyEdit = false;
	QString m_materialName;
	// A material asked for before the library was read.
	QString m_requestedName;
	QString m_requestedKind;
	std::function<bool(const QString&)> m_requestedMissing;
	MaterialEngine m_engine = MaterialEngine::Unknown;
	// Doom keeps walls and flats apart: "doom-wall" or "doom-flat" for the
	// open Doom material, so one of each with the same name are told apart.
	// Empty for every other engine.
	QString m_materialKind;
	// The library's definition, for materials with no text (Quake textures,
	// implicit shaders) and as the base classic edits change.
	MaterialDefinition m_baseDefinition;
	MaterialDefinition m_definition;
	QString m_definitionProblem;
	std::shared_ptr<const MaterialImageSet> m_images;
	QStringList m_imageReferences;
	// The image source revision m_images was read from, and the references
	// of the newest request (published or still being read).
	QString m_imagesRevision;
	QStringList m_requestedImageReferences;
	std::shared_ptr<const MaterialTableSet> m_tables;
	// The library's tables, and each unchanged script's tables over them,
	// made once per library.
	std::shared_ptr<const MaterialTableSet> m_libraryTables;
	QHash<int, std::shared_ptr<const MaterialTableSet>> m_scriptTables;
	int m_parseCount = 0;
	bool m_contentKnown = false;
	bool m_lastHasContent = false;
	QString m_lastSummary;
	bool m_refiltering = false;
	int m_usageToken = 0;
	QElapsedTimer m_timelineThrottle;
	MaterialEngine m_previewSettingsEngine = MaterialEngine::Unknown;
	bool m_shapeChosen = false;
	bool m_applyingText = false;
	bool m_lastModified = false;
	// The next parse shows a different material, not a new version of it.
	bool m_materialChangePending = false;
	bool m_developerDefault = false;
	int m_problemCount = 0;
	int m_imageCount = 0;
	// Lower-case package paths to their real spelling, for the reader below.
	mutable QHash<QString, QString> m_pathIndex;
	mutable const PackageArchiveReader* m_pathIndexReader = nullptr;

	MaterialTaskLane* m_scanLane = nullptr;
	MaterialTaskLane* m_imageLane = nullptr;
	MaterialTaskLane* m_usageLane = nullptr;
	MaterialTaskLane* m_exportLane = nullptr;
	QTimer* m_reparseTimer = nullptr;
	QTimer* m_visibleTimer = nullptr;
	QTimer* m_stateTimer = nullptr;
	QTimer* m_filterTimer = nullptr;
	BusyOverlay* m_imagesBusy = nullptr;
	BusyOverlay* m_exportBusy = nullptr;
	BusyIndicator* m_mapsBusy = nullptr;

	LoadingPane* m_state = nullptr;
	QLineEdit* m_filter = nullptr;
	QComboBox* m_engineFilter = nullptr;
	QToolButton* m_viewMode = nullptr;
	QToolButton* m_animateSwatches = nullptr;
	QToolButton* m_libraryOptions = nullptr;
	QMenu* m_libraryMenu = nullptr;
	int m_swatchSide = 72;
	QToolButton* m_newMaterial = nullptr;
	QToolButton* m_save = nullptr;
	QToolButton* m_revert = nullptr;
	QToolButton* m_more = nullptr;
	QMenu* m_moreMenu = nullptr;
	QLabel* m_libraryCount = nullptr;
	QListView* m_libraryView = nullptr;
	MaterialLibraryModel* m_model = nullptr;
	MaterialLibraryFilter* m_proxy = nullptr;

	QLabel* m_materialTitle = nullptr;
	MaterialPreviewView* m_preview = nullptr;
	QToolButton* m_play = nullptr;
	QToolButton* m_restart = nullptr;
	QComboBox* m_speed = nullptr;
	QSlider* m_timeline = nullptr;
	QLabel* m_timeLabel = nullptr;
	QComboBox* m_shape = nullptr;
	QToolButton* m_lighting = nullptr;
	QMenu* m_lightingMenu = nullptr;
	QLabel* m_notes = nullptr;

	QTabWidget* m_tabs = nullptr;
	MaterialGraphView* m_graph = nullptr;
	MaterialPropertyPanel* m_properties = nullptr;
	QToolButton* m_addNode = nullptr;
	QMenu* m_addNodeMenu = nullptr;
	QLabel* m_documentLabel = nullptr;
	StudioCodeEditor* m_editor = nullptr;
	CodeFindBar* m_findBar = nullptr;
	MaterialScriptHighlighter* m_highlighter = nullptr;
	QTreeWidget* m_imageList = nullptr;
	QTreeWidget* m_problems = nullptr;
	QToolButton* m_problemScope = nullptr;
	QWidget* m_mapsTab = nullptr;
	QLabel* m_mapsSummary = nullptr;
	QToolButton* m_mapsRescan = nullptr;
	QTreeWidget* m_mapList = nullptr;
	QPointer<QDialog> m_previewSettings;
	QTextEdit* m_details = nullptr;
	QSplitter* m_mainSplitter = nullptr;
	QSplitter* m_editorSplitter = nullptr;
	QSplitter* m_graphSplitter = nullptr;
};

} // namespace vibestudio
