// Model sources as studio documents: dropping, Quick Open and recent files
// open a .mesh.json in the Mesh Editor, a .model.json in Design Prop and an
// .assembly.json in Assemble; the Models page lists the editable sources of
// the project and of a folder package; and choosing Mesh Editor on a game
// model offers its editable source, which keeps what the game file cannot.

#include "app/application_shell.h"

#include "app/model_assembly_dialog.h"
#include "app/model_design_dialog.h"
#include "app/model_editor_dialog.h"
#include "core/level_model_placement.h"
#include "core/model_outputs.h"
#include "core/package_staging.h"
#include "core/project_manifest.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>

namespace vibestudio
{
namespace
{
// Where a Models page row keeps the editable source file it opens.
constexpr int kModelSourceRole = Qt::UserRole + 41;

enum class ModelSourceKind
{
	None,
	Mesh,
	Design,
	Assembly
};

ModelSourceKind modelSourceKind(const QString &path)
{
	if (path.endsWith(QStringLiteral(".mesh.json"), Qt::CaseInsensitive))
		return ModelSourceKind::Mesh;
	if (path.endsWith(QStringLiteral(".model.json"), Qt::CaseInsensitive))
		return ModelSourceKind::Design;
	if (path.endsWith(QStringLiteral(".assembly.json"), Qt::CaseInsensitive))
		return ModelSourceKind::Assembly;
	return ModelSourceKind::None;
}

QString withoutModelSuffix(const QString &path)
{
	const QString suffix = QFileInfo(path).suffix();
	return suffix.isEmpty() ? path : path.left(path.size() - suffix.size() - 1);
}
} // namespace

bool ApplicationShell::openModelSourcePath(const QString &path)
{
	const QFileInfo info(path);
	const auto kind = modelSourceKind(info.fileName());
	if (kind == ModelSourceKind::None || !info.isFile())
		return false;
	const QString absolute = info.absoluteFilePath();
	QString error;
	bool opened = false;
	switch (kind)
	{
	case ModelSourceKind::Mesh:
		showModelEditor(nullptr, absolute);
		opened = m_modelEditorDialog && QFileInfo(m_modelEditorDialog->document().path()) == info;
		break;
	case ModelSourceKind::Design:
		showModelDesign();
		opened = m_modelDesignDialog && m_modelDesignDialog->openDesignPath(absolute, &error);
		break;
	case ModelSourceKind::Assembly:
		showModelAssembly();
		opened = m_modelAssemblyDialog && m_modelAssemblyDialog->openSource(absolute, &error);
		break;
	case ModelSourceKind::None:
		break;
	}
	if (!opened)
	{
		if (!error.isEmpty())
			statusBar()->showMessage(error);
		return true;
	}
	m_settings.recordRecentFile(QStringLiteral("model"), absolute);
	statusBar()->showMessage(tr("Opened %1.").arg(info.fileName()));
	return true;
}

QString ApplicationShell::editableSourceForModelEntry(const QString &virtualPath) const
{
	if (virtualPath.isEmpty() || modelSourceKind(virtualPath) != ModelSourceKind::None)
		return {};
	const ProjectManifest manifest = currentProjectManifestOrDefault();
	// A receipt names the source exactly (core/model_outputs.h).
	const auto recorded = modelOutputSourceFor(projectModelOutputs(manifest), virtualPath);
	if (!recorded.isEmpty() && QFileInfo(recorded).isFile())
		return QFileInfo(recorded).absoluteFilePath();
	// Otherwise a same-named .mesh.json at the same game path, beside a folder
	// package's file or in the project's source folders.
	const QString stem = withoutModelSuffix(virtualPath) + QStringLiteral(".mesh.json");
	QStringList candidates;
	const auto &view = packageViewArchive();
	if (view.isOpen() && view.format() == PackageArchiveFormat::Folder)
		candidates << QDir(view.sourcePath()).filePath(stem);
	if (!manifest.rootPath.isEmpty())
	{
		for (const auto &folder : manifest.sourceFolders)
		{
			const QDir root(QDir(manifest.rootPath).filePath(folder));
			candidates << root.filePath(stem) << root.filePath(QFileInfo(stem).fileName());
		}
	}
	for (const auto &candidate : std::as_const(candidates))
	{
		if (QFileInfo(candidate).isFile())
			return QFileInfo(candidate).absoluteFilePath();
	}
	return {};
}

bool ApplicationShell::chooseModelEditorSource(QString *source)
{
	source->clear();
	const QListWidgetItem *item = m_modelEntries ? m_modelEntries->currentItem() : nullptr;
	const QString entry = item ? item->data(Qt::UserRole).toString() : QString();
	const QString found = editableSourceForModelEntry(entry);
	if (found.isEmpty())
		return true;
	int choice = -1;
	if (modelSourcePrompt)
	{
		choice = modelSourcePrompt(found);
	}
	else
	{
		QMessageBox box(this);
		box.setIcon(QMessageBox::Question);
		box.setWindowTitle(tr("Editable Source Found"));
		box.setText(tr("%1 has an editable source: %2").arg(entry, QDir::toNativeSeparators(found)));
		box.setInformativeText(tr("The source keeps what the game file cannot store, such as connectivity, curves, collision, clips and "
								  "seam marks. Importing the game file starts again from what was exported."));
		auto *open = box.addButton(tr("Open Editable Source"), QMessageBox::AcceptRole);
		auto *import = box.addButton(tr("Import This File"), QMessageBox::ActionRole);
		box.addButton(QMessageBox::Cancel);
		box.setDefaultButton(open);
		box.exec();
		choice = box.clickedButton() == open ? 1 : box.clickedButton() == import ? 0 : -1;
	}
	if (choice < 0)
		return false;
	if (choice > 0)
		*source = found;
	return true;
}

void ApplicationShell::appendModelSourceItems()
{
	if (!m_modelEntries)
		return;
	QStringList sources;
	const auto add = [&](const QString &path)
	{
		const QString absolute = QFileInfo(path).absoluteFilePath();
		for (const auto &existing : std::as_const(sources))
		{
			if (QFileInfo(existing) == QFileInfo(absolute))
				return;
		}
		sources << absolute;
	};
	// A folder package's own sources, then the project's source folders. The
	// scan is bounded so a large project folder cannot stall the page.
	const auto &view = packageViewArchive();
	if (view.isOpen() && view.format() == PackageArchiveFormat::Folder)
	{
		for (const auto &entry : view.entries())
		{
			if (entry.kind == PackageEntryKind::File && modelSourceKind(entry.virtualPath) != ModelSourceKind::None)
				add(QDir(view.sourcePath()).filePath(entry.virtualPath));
		}
	}
	const ProjectManifest manifest = currentProjectManifestOrDefault();
	if (!manifest.rootPath.isEmpty())
	{
		int visited = 0;
		for (const auto &folder : manifest.sourceFolders)
		{
			QDirIterator files(QDir(manifest.rootPath).filePath(folder), {QStringLiteral("*.mesh.json"), QStringLiteral("*.model.json"),
																			QStringLiteral("*.assembly.json")},
							   QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
			while (files.hasNext() && ++visited <= 4096 && sources.size() < 512)
				add(files.next());
		}
	}
	if (sources.isEmpty())
		return;
	// Receipts say what each source delivered; a source saved after its newest
	// receipt has outputs to refresh (checked by time here, by content in Outputs).
	const auto receipts = manifest.rootPath.isEmpty() ? QVector<ModelOutputRecord>{} : projectModelOutputs(manifest);
	auto *header = new QListWidgetItem(tr("Editable sources"));
	header->setFlags(Qt::NoItemFlags);
	m_modelEntries->addItem(header);
	for (const auto &path : std::as_const(sources))
	{
		const QString relative = !manifest.rootPath.isEmpty() && !QDir(manifest.rootPath).relativeFilePath(path).startsWith(QStringLiteral(".."))
									 ? QDir(manifest.rootPath).relativeFilePath(path)
									 : QFileInfo(path).fileName();
		auto *item = new QListWidgetItem(relative);
		item->setData(kModelSourceRole, path);
		const bool stale = modelSourceSavedAfterOutputs(receipts, path);
		if (stale)
			item->setText(tr("%1 (outputs out of date)").arg(relative));
		item->setToolTip(QDir::toNativeSeparators(path) + QLatin1Char('\n') +
						 (stale ? tr("Saved after its exported or staged files; refresh them from the Mesh Editor's Outputs.") + QLatin1Char('\n') : QString()) +
						 tr("Press Enter or double-click to open it in its editor."));
		m_modelEntries->addItem(item);
	}
}

bool ApplicationShell::commitModelHandoff(const std::function<bool(PackageStagingModel *, LevelMapDocument *, QString *)> &stage, bool place,
										  QString *error)
{
	// Both are prepared on copies; the map commits through the guarded committer
	// first, so a map that changed meanwhile refuses and nothing is staged.
	auto staged = m_packageStaging;
	LevelMapDocument candidate;
	if (place)
		candidate = m_levelMapDocument;
	if (!stage(&staged, place ? &candidate : nullptr, error))
		return false;
	int placed = -1;
	if (place)
	{
		if (candidate.entities.size() > m_levelMapDocument.entities.size())
			placed = candidate.entities.last().id;
		const auto commit = levelPlacementCommitter();
		if (!commit(candidate, error))
			return false;
	}
	m_packageStaging = std::move(staged);
	refreshPackageStagingSummary();
	if (place)
	{
		// The new entity is selected, so its keys are in the inspector at once.
		if (placed >= 0)
			setLevelMapSelection(&m_levelMapDocument, {{LevelMapSelectionKind::Entity, placed}});
		refreshLevelMapWorkbench();
		frameLevelMapSelection();
	}
	return true;
}

QStringList ApplicationShell::openModelSourcePaths() const
{
	QStringList paths;
	if (m_modelEditorDialog && !m_modelEditorDialog->document().path().isEmpty())
		paths << m_modelEditorDialog->document().path();
	if (m_modelDesignDialog && !m_modelDesignDialog->path().isEmpty())
		paths << m_modelDesignDialog->path();
	if (m_modelAssemblyDialog && !m_modelAssemblyDialog->document().path().isEmpty())
		paths << m_modelAssemblyDialog->document().path();
	return paths;
}

QString ApplicationShell::selectedLevelEntityModelPath() const
{
	if (m_levelMapDocument.selectionKind != LevelMapSelectionKind::Entity)
		return {};
	for (const auto &entity : m_levelMapDocument.entities)
	{
		if (entity.id == m_levelMapDocument.selectedObjectId)
			return levelEntityModelPath(entity);
	}
	return {};
}

bool ApplicationShell::recordProjectModelOutput(const ModelOutputRecord &record, QString *error)
{
	ProjectManifest manifest = currentProjectManifestOrDefault();
	if (manifest.rootPath.isEmpty())
	{
		if (error)
			*error = tr("Open a project to keep receipts of model outputs.");
		return false;
	}
	// A manifest that exists but cannot be read is not replaced.
	QString loadError;
	if (QFileInfo::exists(projectManifestPath(manifest.rootPath)) && !loadProjectManifest(manifest.rootPath, &manifest, &loadError))
	{
		if (error)
			*error = loadError;
		return false;
	}
	addProjectModelOutput(&manifest, record);
	return saveOwnProjectManifest(manifest, error);
}

void ApplicationShell::openModelSourceItem(QListWidgetItem *item)
{
	const QString path = item ? item->data(kModelSourceRole).toString() : QString();
	if (!path.isEmpty())
		openModelSourcePath(path);
}
} // namespace vibestudio
