#include "app/application_shell.h"
#include "app/level_map_import_dialog.h"
#include "app/level_map_obj_export_dialog.h"
#include "app/studio_actions.h"
#include "core/game_installation.h"
#include "core/level_map_obj_export.h"
#include "core/level_map_transfer.h"
#include "core/level_materials.h"
#include "core/level_placement.h"
#include "core/package_copy.h"
#include "core/package_copy_store.h"
#include "core/project_manifest.h"

#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextDocument>
#include <QThread>
#include <QVBoxLayout>

// Import Map and Export Selection: NetRadiant Custom radiant/map.cpp ImportMap,
// Map_ImportFile, ExportMap and Map_SaveSelected at 449778b3
// (GPL-2.0-or-later). Export Map: TrenchBroom MapWindow::exportDocumentAsMap
// at 4c756be1 (GPL-3.0-or-later). Workflow references only; original Qt
// implementation, see docs/CREDITS.md.
namespace vibestudio {
namespace {
// Closing asks the worker to stop but keeps the window until it has, as Save
// Map's progress window does, so nothing races the snapshot being written.
class TransferProgress final : public QDialog {
  public:
	using QDialog::QDialog;
	std::function<void()> cancel;
	void reject() override
	{
		if (cancel) {
			cancel();
		}
	}

  protected:
	void closeEvent(QCloseEvent* event) override
	{
		reject();
		event->ignore();
	}
};

// Runs `work` on a worker behind a modal progress window with Cancel.
void runTransfer(QWidget* parent, const QString& title, const QString& text, const QString& cancelling, bool reducedMotion,
	const std::function<void()>& work)
{
	TransferProgress progress(parent);
	progress.setObjectName(QStringLiteral("levelMapTransferProgress"));
	progress.setWindowTitle(title);
	progress.setWindowModality(Qt::WindowModal);
	progress.setAccessibleName(title);
	auto* layout = new QVBoxLayout(&progress);
	auto* label = new QLabel(text);
	label->setObjectName(QStringLiteral("levelMapTransferPhase"));
	label->setWordWrap(true);
	label->setAccessibleName(title);
	layout->addWidget(label);
	auto* bar = new QProgressBar;
	bar->setRange(0, reducedMotion ? 1 : 0);
	bar->setTextVisible(false);
	bar->setAccessibleName(title);
	layout->addWidget(bar);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
	layout->addWidget(buttons);
	auto* worker = QThread::create(work);
	progress.cancel = [&]() {
		worker->requestInterruption();
		buttons->setEnabled(false);
		label->setText(cancelling);
	};
	QObject::connect(buttons, &QDialogButtonBox::rejected, &progress, &QDialog::reject);
	QObject::connect(worker, &QThread::finished, &progress, &QDialog::accept);
	worker->start();
	progress.exec();
	worker->wait();
	delete worker;
}

bool textMap(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
}

QString writeIdentity(const QString& path)
{
	const QFileInfo info(path);
	QString identity = QDir::cleanPath(info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath());
#ifdef Q_OS_WIN
	identity = identity.toCaseFolded();
#endif
	return identity;
}

// The OBJ export window's choices, kept for the session.
LevelMapObjExportOptions& rememberedObjOptions()
{
	static LevelMapObjExportOptions options = levelMapObjDefaultOptions();
	return options;
}
} // namespace

void ApplicationShell::registerLevelMapTransferCommands()
{
	const auto folder = [this]() {
		const auto& source = m_levelMapDocument.sourcePath;
		return source.isEmpty() ? m_settings.currentProjectPath() : QFileInfo(source).absolutePath();
	};
	const auto suggestion = [this, folder](const QString& suffix) {
		const auto base = m_levelMapDocument.sourcePath.isEmpty() ? m_levelMapDocument.mapName : QFileInfo(m_levelMapDocument.sourcePath).completeBaseName();
		return QDir(folder()).filePath(base + suffix);
	};

	StudioCommandRegistration importer;
	importer.commandId = QStringLiteral("map.importMap");
	importer.group = StudioCommandGroup::File;
	importer.label = tr("Import Map…");
	importer.statusTip = tr("Add another map's brushes, patches and entities at their own coordinates, renaming clashing entity names, as one undo step. "
						  "A compiled .bsp is converted with VibeMap3 first.");
	importer.iconName = QStringLiteral("map");
	importer.handler = [this, folder]() {
		if (!textMap(m_levelMapDocument)) {
			statusBar()->showMessage(tr("Open a Quake-family .map before importing another map into it."));
			return;
		}
		// VibeMap3 converts Quake III-family BSPs only, so only those maps offer them.
		const QString filter = m_levelMapDocument.format == LevelMapFormat::Quake3Map
			? tr("Maps (*.map *.bsp)") + QStringLiteral(";;") + tr("Level maps (*.map)") + QStringLiteral(";;") + tr("Compiled maps (*.bsp)")
			: tr("Level maps (*.map)");
		const QString path = QFileDialog::getOpenFileName(this, tr("Import Map"), folder(), filter);
		if (!path.isEmpty()) {
			importLevelMapFile(path);
		}
	};
	m_commands->registerCommand(importer);

	StudioCommandRegistration selection;
	selection.commandId = QStringLiteral("map.exportSelectionAsMap");
	selection.group = StudioCommandGroup::File;
	selection.label = tr("Export Selection as Map…");
	selection.statusTip = tr("Write the selected brushes, patches and entities to a .map of their own, inside a worldspawn with the map's keys. "
							 "The open map is not changed.");
	selection.iconName = QStringLiteral("save");
	selection.handler = [this, suggestion]() {
		if (!textMap(m_levelMapDocument) || m_levelMapDocument.selection.isEmpty()) {
			statusBar()->showMessage(tr("Select brushes, patches or entities of a Quake-family map to export them."));
			return;
		}
		// The second type leaves the worldspawn keys out, for a fragment that
		// takes the keys of whichever map it later joins.
		const QString withKeys = tr("Level map with worldspawn keys (*.map)");
		const QString withoutKeys = tr("Level map without worldspawn keys (*.map)");
		QString chosen = withKeys;
		const QString path = QFileDialog::getSaveFileName(this, tr("Export Selection as Map"), suggestion(QStringLiteral("-selection.map")),
			withKeys + QStringLiteral(";;") + withoutKeys, &chosen);
		if (!path.isEmpty()) {
			exportLevelMapSelectionFile(path, chosen != withoutKeys);
		}
	};
	m_commands->registerCommand(selection);

	StudioCommandRegistration copy;
	copy.commandId = QStringLiteral("map.exportMap");
	copy.group = StudioCommandGroup::File;
	copy.label = tr("Export Map…");
	copy.statusTip = tr("Write a copy of the map to another file, leaving out layers marked Omit from export. The open map keeps its own file.");
	copy.iconName = QStringLiteral("save");
	copy.handler = [this, suggestion]() {
		if (m_levelMapDocument.format == LevelMapFormat::Unknown) {
			return;
		}
		if (m_levelMapDocument.format == LevelMapFormat::DoomWad) {
			const QString path = QFileDialog::getSaveFileName(this, tr("Export Map"), suggestion(QStringLiteral("-export.wad")), tr("Doom WAD (*.wad)"));
			if (!path.isEmpty()) {
				exportLevelMapCopyFile(path);
			}
			return;
		}
		const QString plainType = tr("Level map (*.map)");
		const QString trenchBroomType = tr("Level map with TrenchBroom groups (*.map)");
		QString chosen = m_levelMapDocument.sceneStorage == LevelSceneStorage::TrenchBroom ? trenchBroomType : plainType;
		const QString path = QFileDialog::getSaveFileName(this, tr("Export Map"), suggestion(QStringLiteral("-export.map")),
			plainType + QStringLiteral(";;") + trenchBroomType, &chosen);
		if (!path.isEmpty()) {
			exportLevelMapCopyFile(path, chosen == trenchBroomType ? LevelSceneStorage::TrenchBroom : LevelSceneStorage::VibeStudio);
		}
	};
	m_commands->registerCommand(copy);

	// Export Map as OBJ (TrenchBroom) and Export Selection as OBJ (NetRadiant
	// Custom's brushexport): one options window, which remembers its choices
	// for the session.
	const auto objExport = [this, suggestion](bool selectionOnly) {
		if (!textMap(m_levelMapDocument) || (selectionOnly && m_levelMapDocument.selection.isEmpty())) {
			statusBar()->showMessage(selectionOnly ? tr("Select brushes, patches or entities of a Quake-family map to export them as OBJ.")
												   : tr("Open a Quake-family map to export it as OBJ."));
			return;
		}
		auto& remembered = rememberedObjOptions();
		remembered.selectionOnly = selectionOnly;
		const int selected = static_cast<int>(m_levelMapDocument.selection.size());
		const int objects = static_cast<int>(m_levelMapDocument.brushes.size() + m_levelMapDocument.patches.size());
		// View filters are a way of looking at the map, not a choice of what to
		// export: hidden objects stay out, filtered kinds are written, as
		// TrenchBroom's OBJ export does and as `map export-obj` does.
		const QString summary = (selectionOnly
			? tr("%n selected object(s): their brushes and patches, without hidden ones.", nullptr, selected)
			: tr("The whole map, %n brush(es) and patch(es), without hidden objects and layers marked Omit from export.", nullptr, objects))
			+ QLatin1Char(' ') + tr("View filters do not apply: hide objects to leave them out.");
		LevelMapObjExportDialog dialog(suggestion(selectionOnly ? QStringLiteral("-selection.obj") : QStringLiteral(".obj")), remembered, summary, this);
		if (dialog.exec() != QDialog::Accepted || dialog.path().isEmpty()) {
			return;
		}
		remembered = dialog.options();
		exportLevelMapObjFile(dialog.path(), remembered);
	};
	StudioCommandRegistration objMap;
	objMap.commandId = QStringLiteral("map.exportMapAsObj");
	objMap.group = StudioCommandGroup::File;
	objMap.label = tr("Export Map as OBJ…");
	objMap.statusTip = tr("Write the map's brushes and patches as a Wavefront OBJ with a material library, for Blender and other modellers. "
						  "The open map is not changed.");
	objMap.iconName = QStringLiteral("save");
	objMap.handler = [objExport]() { objExport(false); };
	m_commands->registerCommand(objMap);

	StudioCommandRegistration objSelection;
	objSelection.commandId = QStringLiteral("map.exportSelectionAsObj");
	objSelection.group = StudioCommandGroup::File;
	objSelection.label = tr("Export Selection as OBJ…");
	objSelection.statusTip = tr("Write the selected brushes and patches as a Wavefront OBJ with a material library, leaving out tool materials such as caulk.");
	objSelection.iconName = QStringLiteral("save");
	objSelection.handler = [objExport]() { objExport(true); };
	m_commands->registerCommand(objSelection);
}

void ApplicationShell::refreshLevelMapTransferCommands()
{
	if (!m_commands) {
		return;
	}
	const bool text = textMap(m_levelMapDocument);
	m_commands->setEnabled(QStringLiteral("map.importMap"), text);
	m_commands->setEnabled(QStringLiteral("map.exportSelectionAsMap"), text && !m_levelMapDocument.selection.isEmpty());
	m_commands->setEnabled(QStringLiteral("map.exportMap"), m_levelMapDocument.format != LevelMapFormat::Unknown);
	m_commands->setEnabled(QStringLiteral("map.exportMapAsObj"), text);
	m_commands->setEnabled(QStringLiteral("map.exportSelectionAsObj"), text && !m_levelMapDocument.selection.isEmpty());
}

bool ApplicationShell::importLevelMapFile(const QString& path, QString* error)
{
	const auto refuse = [&](const QString& message) {
		if (error) {
			*error = message;
		}
		statusBar()->showMessage(message);
		return false;
	};
	if (!textMap(m_levelMapDocument)) {
		return refuse(tr("Open a Quake-family .map before importing another map into it."));
	}
	// The compiler context Build uses, and the project's game and installation
	// folders, for a .bsp's VibeMap3 conversion.
	LevelMapBspConversionContext conversion;
	conversion.workspaceRootPath = m_settings.currentProjectPath();
	conversion.executableOverrides = m_settings.compilerToolPathOverrides();
	ProjectManifest manifest;
	if (!conversion.workspaceRootPath.trimmed().isEmpty() && loadProjectManifest(conversion.workspaceRootPath, &manifest)) {
		conversion.extraSearchPaths = effectiveProjectCompilerSearchPaths(manifest, conversion.extraSearchPaths);
		conversion.executableOverrides = effectiveProjectCompilerToolOverrides(manifest, conversion.executableOverrides);
	}
	GameInstallationProfile installation;
	const bool installed = selectedGameInstallation(&installation);
	conversion.gameKey = effectiveProjectGameKey(currentProjectManifestOrDefault(), installed ? &installation : nullptr);
	if (installed && normalizedGameKey(installation.gameKey) == conversion.gameKey) {
		conversion.basePath = installation.rootPath;
		conversion.gameDirectory = defaultGameDirectory(installation);
	}
	LevelMapImportDialog dialog(m_levelMapDocument, path, conversion, this);
	if (dialog.exec() != QDialog::Accepted || !dialog.imported()) {
		return refuse(dialog.problem().isEmpty() ? tr("Import cancelled; the map was not changed.") : dialog.problem());
	}
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::ImportMap;
	request.importSource = dialog.imported();
	request.importOptions = dialog.options();
	const auto warnings = dialog.preview().warnings;
	QString failure;
	if (!runLevelPlacementFromUi(request, &failure)) {
		recordActivity(tr("Map import failed"), path, QStringLiteral("level-map"), OperationState::Failed, failure);
		return refuse(tr("Could not import %1: %2").arg(QFileInfo(path).fileName(), failure));
	}
	const int count = static_cast<int>(m_levelMapDocument.selection.size());
	recordActivity(tr("Map imported"), QDir::toNativeSeparators(path), QStringLiteral("level-map"), OperationState::Warning,
				   tr("Unsaved map edit"), warnings);
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Imported %n object(s) from %1 as one undo step; they are selected.", nullptr, count).arg(QFileInfo(path).fileName()));
	return true;
}

bool ApplicationShell::exportLevelMapSelectionFile(const QString& path, bool worldspawnKeys, QString* error)
{
	const auto refuse = [&](const QString& message) {
		if (error) {
			*error = message;
		}
		statusBar()->showMessage(message);
		return false;
	};
	if (!textMap(m_levelMapDocument) || m_levelMapDocument.selection.isEmpty()) {
		return refuse(tr("Select brushes, patches or entities of a Quake-family map to export them."));
	}
	const auto target = writeIdentity(path);
	if (m_sessionCopies && m_sessionCopies->isValid() && packageCopyStorageContainsPath(m_sessionCopies->path(), path)) {
		return refuse(tr("Temporary package copies are removed when the studio closes. Choose a project or another permanent folder."));
	}
	for (const auto& tab : m_codeTabs) {
		if (writeIdentity(tab.path) == target && tab.document && tab.document->isModified()) {
			return refuse(tr("The same file has unsaved Code edits. Export to another path or resolve those edits first."));
		}
	}
	LevelMapSelectionExportRequest request;
	request.save.path = path;
	request.save.overwrite = true;
	request.save.isCancelled = []() { return QThread::currentThread()->isInterruptionRequested(); };
	request.worldspawnKeys = worldspawnKeys;
	const auto snapshot = m_levelMapDocument;
	LevelMapSelectionExportReport report;
	runTransfer(this, tr("Export Selection as Map"), tr("Writing %1").arg(QDir::toNativeSeparators(path)), tr("Cancelling export…"),
		m_settings.accessibilityPreferences().reducedMotion, [&]() { report = exportLevelMapSelection(snapshot, request); });
	if (!report.save.succeeded()) {
		const auto message = report.save.errors.join(QStringLiteral("; "));
		recordActivity(tr("Selection export failed"), path, QStringLiteral("level-map"), OperationState::Failed, message);
		return refuse(message);
	}
	for (int index = 0; index < m_codeTabs.size(); ++index) {
		if (writeIdentity(m_codeTabs.at(index).path) == target) {
			reloadCodeTab(index);
		}
	}
	QStringList details{report.save.outputPath};
	if (!report.save.backupPath.isEmpty()) {
		details << tr("Backup: %1").arg(report.save.backupPath);
	}
	recordActivity(tr("Selection exported as map"), details.join(QLatin1Char('\n')), QStringLiteral("level-map"), OperationState::Completed,
				   tr("%n object(s) written", nullptr, report.objects), report.save.warnings);
	statusBar()->showMessage(tr("Exported %n selected object(s) to %1.", nullptr, report.objects).arg(QDir::toNativeSeparators(report.save.outputPath)));
	return true;
}

bool ApplicationShell::exportLevelMapCopyFile(const QString& path, std::optional<LevelSceneStorage> storage, QString* error)
{
	const auto refuse = [&](const QString& message) {
		if (error) {
			*error = message;
		}
		statusBar()->showMessage(message);
		return false;
	};
	if (m_levelMapDocument.format == LevelMapFormat::Unknown) {
		return refuse(tr("Open a map before exporting it."));
	}
	const auto target = writeIdentity(path);
	if (!m_levelMapDocument.sourcePath.isEmpty() && target == writeIdentity(m_levelMapDocument.sourcePath)) {
		return refuse(tr("An export cannot replace the open map. Choose a different file name to export to."));
	}
	if (m_sessionCopies && m_sessionCopies->isValid() && packageCopyStorageContainsPath(m_sessionCopies->path(), path)) {
		return refuse(tr("Temporary package copies are removed when the studio closes. Choose a project or another permanent folder."));
	}
	for (const auto& tab : m_codeTabs) {
		if (writeIdentity(tab.path) == target && tab.document && tab.document->isModified()) {
			return refuse(tr("The same file has unsaved Code edits. Export to another path or resolve those edits first."));
		}
	}
	LevelMapExportRequest request;
	request.save.path = path;
	request.save.overwrite = true;
	request.save.sceneStorage = storage;
	request.save.isCancelled = []() { return QThread::currentThread()->isInterruptionRequested(); };
	const auto snapshot = m_levelMapDocument;
	LevelMapExportReport report;
	runTransfer(this, tr("Export Map"), tr("Writing %1").arg(QDir::toNativeSeparators(path)), tr("Cancelling export…"),
		m_settings.accessibilityPreferences().reducedMotion, [&]() { report = exportLevelMapCopy(snapshot, request); });
	if (!report.save.succeeded()) {
		const auto message = report.save.errors.join(QStringLiteral("; "));
		recordActivity(tr("Map export failed"), path, QStringLiteral("level-map"), OperationState::Failed, message);
		return refuse(message);
	}
	for (int index = 0; index < m_codeTabs.size(); ++index) {
		if (writeIdentity(m_codeTabs.at(index).path) == target) {
			reloadCodeTab(index);
		}
	}
	QStringList details{report.save.outputPath};
	if (!report.save.backupPath.isEmpty()) {
		details << tr("Backup: %1").arg(report.save.backupPath);
	}
	const int layers = static_cast<int>(report.omittedLayers.size());
	if (layers > 0) {
		details << tr("Left out %n layer(s): %1", nullptr, layers).arg(report.omittedLayers.join(QStringLiteral(", ")));
	}
	recordActivity(tr("Map exported"), details.join(QLatin1Char('\n')), QStringLiteral("level-map"),
				   report.save.warnings.isEmpty() ? OperationState::Completed : OperationState::Warning, levelMapSaveReportText(report.save),
				   report.save.warnings);
	statusBar()->showMessage(layers > 0
		? tr("Exported the map to %1, leaving out %n layer(s).", nullptr, layers).arg(QDir::toNativeSeparators(report.save.outputPath))
		: tr("Exported the map to %1.").arg(QDir::toNativeSeparators(report.save.outputPath)));
	return true;
}

bool ApplicationShell::exportLevelMapObjFile(const QString& path, const LevelMapObjExportOptions& options, QString* error)
{
	const auto refuse = [&](const QString& message) {
		if (error) {
			*error = message;
		}
		statusBar()->showMessage(message);
		return false;
	};
	if (!textMap(m_levelMapDocument)) {
		return refuse(tr("Open a Quake-family map to export it as OBJ."));
	}
	if (options.selectionOnly && m_levelMapDocument.selection.isEmpty()) {
		return refuse(tr("Select brushes, patches or entities of a Quake-family map to export them as OBJ."));
	}
	const auto objPath = QFileInfo(path).absoluteFilePath();
	const QStringList targets{writeIdentity(objPath), writeIdentity(levelMapObjMaterialLibraryPath(objPath))};
	if (m_sessionCopies && m_sessionCopies->isValid() && packageCopyStorageContainsPath(m_sessionCopies->path(), objPath)) {
		return refuse(tr("Temporary package copies are removed when the studio closes. Choose a project or another permanent folder."));
	}
	for (const auto& tab : m_codeTabs) {
		if (targets.contains(writeIdentity(tab.path)) && tab.document && tab.document->isModified()) {
			return refuse(tr("The same file has unsaved Code edits. Export to another path or resolve those edits first."));
		}
	}
	// The package the Levels camera reads, for image sizes, colours and paths.
	std::shared_ptr<const PackageArchiveReader> archive =
		m_packageStaging.isLoaded() ? std::make_shared<PackageStagingArchive>(m_packageStaging) : m_packageArchive.snapshotReader();
	if (!archive && m_packageArchive.isOpen()) {
		archive = std::make_shared<const PackageArchive>(m_packageArchive);
	}
	LevelMapObjExportRequest request;
	request.save.path = objPath;
	request.save.overwrite = true;
	request.save.isCancelled = []() { return QThread::currentThread()->isInterruptionRequested(); };
	request.options = options;
	// Export-relative image paths start from the package folder, else the
	// game folder of the project's installation, else the package's folder.
	const QString packageSource = m_packageArchive.isOpen() ? m_packageArchive.sourcePath() : QString();
	GameInstallationProfile installation;
	const QString gameFolder = selectedGameInstallation(&installation) && !defaultGameDirectory(installation).isEmpty()
		? QDir(installation.rootPath).filePath(defaultGameDirectory(installation))
		: QString();
	if (QFileInfo(packageSource).isDir()) {
		request.options.materialRoot = packageSource;
	} else if (QFileInfo(gameFolder).isDir()) {
		request.options.materialRoot = gameFolder;
	} else if (!packageSource.isEmpty()) {
		request.options.materialRoot = QFileInfo(packageSource).absolutePath();
	}
	LevelPreviewAssetOptions assetOptions;
	assetOptions.paletteId = activePaletteId();
	assetOptions.materialLimit = 4096;
	assetOptions.modelLimit = 0;
	assetOptions.previewDimension = 32;
	assetOptions.totalReadByteLimit = 512LL * 1024 * 1024;
	const auto snapshot = m_levelMapDocument;
	LevelMapObjExportReport report;
	const QString title = options.selectionOnly ? tr("Export Selection as OBJ") : tr("Export Map as OBJ");
	runTransfer(this, title, tr("Writing %1").arg(QDir::toNativeSeparators(objPath)), tr("Cancelling export…"),
		m_settings.accessibilityPreferences().reducedMotion, [&]() {
			LevelMapDocument exported;
			if (archive && prepareLevelMapObjExport(snapshot, request.options, &exported)) {
				const auto assets = resolveLevelPreviewAssets(exported, *archive, assetOptions,
					[](int, int) { return !QThread::currentThread()->isInterruptionRequested(); });
				request.options.materials = levelMapObjMaterials(assets);
			}
			report = exportLevelMapObj(snapshot, request);
		});
	if (!report.succeeded()) {
		const auto message = report.errors.join(QStringLiteral("; "));
		if (report.cancelled) {
			recordActivity(tr("OBJ export cancelled"), objPath, QStringLiteral("level-map"), OperationState::Cancelled, message);
		} else {
			recordActivity(tr("OBJ export failed"), objPath, QStringLiteral("level-map"), OperationState::Failed, message);
		}
		return refuse(message);
	}
	for (int index = 0; index < m_codeTabs.size(); ++index) {
		if (targets.contains(writeIdentity(m_codeTabs.at(index).path))) {
			reloadCodeTab(index);
		}
	}
	QStringList details{QDir::toNativeSeparators(report.objPath)};
	if (!report.mtlPath.isEmpty()) {
		details << QDir::toNativeSeparators(report.mtlPath);
	}
	for (const auto& backup : {report.objBackup, report.mtlBackup}) {
		if (!backup.isEmpty()) {
			details << tr("Backup: %1").arg(QDir::toNativeSeparators(backup));
		}
	}
	const int count = report.brushes + report.patches;
	const int polygons = report.polygons;
	recordActivity(tr("Map exported as OBJ"), details.join(QLatin1Char('\n')), QStringLiteral("level-map"),
		report.warnings.isEmpty() ? OperationState::Completed : OperationState::Warning, tr("%n face(s) written", nullptr, polygons), report.warnings);
	statusBar()->showMessage(tr("Exported %n brush(es) and patch(es) to %1.", nullptr, count).arg(QDir::toNativeSeparators(report.objPath)));
	return true;
}
} // namespace vibestudio
