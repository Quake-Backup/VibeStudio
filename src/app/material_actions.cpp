// The Materials workbench inside the shell: it reads the package view (staged
// edits included) over the selected game installation's base packages, and
// saves through the package's staging, so material edits join the same undo,
// review and save as every other package change. Its Maps tab opens maps in
// Levels with the material's faces selected.

#include "app/application_shell.h"

#include "app/asset_views.h"
#include "app/level_material_animator.h"
#include "app/material_workbench.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/game_installation.h"
#include "core/level_map.h"
#include "core/material_library.h"
#include "core/package_staging.h"

#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>

#include <algorithm>
#include <memory>

namespace vibestudio {

QWidget* ApplicationShell::buildMaterialWorkbench()
{
	m_materialWorkbench = new MaterialWorkbench;
	const auto packageBusy = [this](QString* error) {
		if (m_packageSaveRunning || m_packageReadRunning || m_packageExtractionRunning) {
			if (error) {
				*error = tr("The package is busy; save the material again when it is idle.");
			}
			return true;
		}
		if (!m_packageStaging.isLoaded()) {
			if (error) {
				*error = tr("Open the package the material belongs to before saving into it.");
			}
			return true;
		}
		return false;
	};
	MaterialWorkbenchHost host;
	host.stageFile = [this, packageBusy](const QByteArray& bytes, const QString& virtualPath, QString* error) {
		if (packageBusy(error)) {
			return false;
		}
		if (m_packageStaging.sourceFormat() == PackageArchiveFormat::Wad) {
			if (error) {
				*error = tr("A WAD holds lumps, not files: %1 cannot be staged into it.").arg(virtualPath);
			}
			return false;
		}
		if (!m_packageStaging.addBytes(bytes, virtualPath, error, PackageStageConflictResolution::ReplaceExisting)) {
			return false;
		}
		refreshPackageStagingSummary();
		recordActivity(tr("Material staged"), virtualPath, QStringLiteral("shader"), OperationState::Warning, tr("Package save pending"));
		syncMaterialWorkbench();
		return true;
	};
	host.stageLump = [this, packageBusy](const QByteArray& bytes, const QString& name, QString* error) {
		if (packageBusy(error)) {
			return false;
		}
		if (m_packageStaging.sourceFormat() != PackageArchiveFormat::Wad) {
			if (error) {
				*error = tr("Only a WAD takes the %1 lump.").arg(name);
			}
			return false;
		}
		if (!m_packageStaging.addWadBytes(bytes, name, QString(), 0, true, error)) {
			return false;
		}
		refreshPackageStagingSummary();
		recordActivity(tr("Lump staged"), name, QStringLiteral("shader"), OperationState::Warning, tr("Package save pending"));
		syncMaterialWorkbench();
		return true;
	};
	host.showTexture = [this](const QString& reference) { showTextureReference(reference); };
	host.openInCode = [this](const QString& path, int line) { openCodeFileAt(path, std::max(1, line)); };
	host.openMapUse = [this](const MaterialMapUse& use, const QString& material, QString* error) { return openMaterialMapUse(use, material, error); };
	host.editImage = [this](const QString& reference) { editMaterialImageInTextureEditor(reference); };
	host.revealInPackage = [this](const QString& path) { revealPackageEntry(path); };
	host.loadState = [this]() { return m_settings.shellLayoutState(QStringLiteral("materialsWorkbench")); };
	host.saveState = [this](const QByteArray& state) { m_settings.setShellLayoutState(QStringLiteral("materialsWorkbench"), state); };
	m_materialWorkbench->setHost(host);
	connect(m_materialWorkbench, &MaterialWorkbench::statusMessage, this, [this](const QString& message) { statusBar()->showMessage(message); });
	connect(m_materialWorkbench, &MaterialWorkbench::contentChanged, this, [this]() { refreshSurfaceStates(); });
	syncMaterialWorkbench();
	return m_materialWorkbench;
}

void ApplicationShell::syncMaterialWorkbench()
{
	if (!m_materialWorkbench) {
		return;
	}
	m_materialWorkbench->setReducedMotion(m_settings.accessibilityPreferences().reducedMotion);
	if (m_levelMaterialAnimator) {
		m_levelMaterialAnimator->setReducedMotion(m_settings.accessibilityPreferences().reducedMotion);
	}
	if (!m_packageArchive.isOpen()) {
		m_materialWorkbench->clearPackage();
		return;
	}
	// A different game installation changes what lies beneath the package.
	m_materialWorkbench->setBasePackages(materialBasePackagePaths());
	// The view only changes with the staging revision; copy it only then.
	const QString key = packageViewKey();
	if (key == m_materialWorkbench->packageRevision()) {
		return;
	}
	m_materialWorkbench->setPackage(std::make_shared<PackageArchive>(packageViewArchive()), packageOpenPath(), key);
}

QStringList ApplicationShell::materialBasePackagePaths() const
{
	GameInstallationProfile installation;
	if (!m_packageArchive.isOpen() || !selectedGameInstallation(&installation)) {
		return {};
	}
	// Only an installation of the package's own engine: a Quake III install
	// says nothing about a Doom PWAD's textures.
	const PackageArchive& view = packageViewArchive();
	const MaterialEngine engine = detectMaterialEngine(view);
	const GameEngineFamily family = installation.engineFamily;
	bool matches = family == GameEngineFamily::Unknown;
	switch (engine) {
	case MaterialEngine::Doom:
		matches = matches || family == GameEngineFamily::IdTech1;
		break;
	case MaterialEngine::Quake:
	case MaterialEngine::Quake2:
		matches = matches || family == GameEngineFamily::IdTech2;
		break;
	case MaterialEngine::Quake3:
		matches = matches || family == GameEngineFamily::IdTech3;
		break;
	case MaterialEngine::Doom3:
		break;
	case MaterialEngine::Unknown:
		matches = view.format() == PackageArchiveFormat::Wad ? family == GameEngineFamily::IdTech1 : true;
		break;
	}
	if (!matches) {
		return {};
	}
	QStringList paths;
	const QString open = QFileInfo(packageOpenPath()).absoluteFilePath();
	for (const QString& path : installation.basePackagePaths) {
		const QString absolute = QFileInfo(QDir(installation.rootPath).absoluteFilePath(path)).absoluteFilePath();
		// The package itself is never beneath itself.
		if (QFileInfo::exists(absolute) && absolute.compare(open, Qt::CaseInsensitive) != 0) {
			paths << absolute;
		}
	}
	return paths;
}

namespace {

const QString kAnimateMaterialsKey = QStringLiteral("levelAnimateMaterials");

} // namespace

void ApplicationShell::registerLevelMaterialAnimationCommand()
{
	if (!m_commands) {
		return;
	}
	StudioCommandRegistration command;
	command.commandId = QStringLiteral("map.animateMaterials");
	command.group = StudioCommandGroup::View;
	command.label = tr("Animate Materials in Camera");
	command.statusTip = tr("Play scrolling, pulsing and animated materials on the camera's faces, drawn as the Materials page draws them. "
						   "Reduce motion keeps them still. The choice is kept.");
	command.iconName = QStringLiteral("film");
	command.checkable = true;
	command.menuSection = tr("Camera Display");
	command.handler = [this]() {
		const bool on = m_settings.shellLayoutState(kAnimateMaterialsKey) != QByteArrayLiteral("on");
		m_settings.setShellLayoutState(kAnimateMaterialsKey, on ? QByteArrayLiteral("on") : QByteArrayLiteral("off"));
		if (QAction* action = m_commands->action(QStringLiteral("map.animateMaterials")); action && action->isChecked() != on) {
			action->setChecked(on);
		}
		if (m_levelMaterialAnimator) {
			m_levelMaterialAnimator->setEnabled(on);
		}
		const bool still = m_settings.accessibilityPreferences().reducedMotion;
		statusBar()->showMessage(!on ? tr("The camera shows materials still.")
			: still					  ? tr("Animate Materials in Camera is on, but Reduce motion keeps them still.")
									  : tr("Animated materials play in the camera."),
			5000);
	};
	if (QAction* action = m_commands->registerCommand(command)) {
		action->setChecked(m_settings.shellLayoutState(kAnimateMaterialsKey) == QByteArrayLiteral("on"));
	}
}

void ApplicationShell::refreshLevelMaterialAnimation(const ModelMesh& mesh, const QHash<int, QImage>& stillImages)
{
	if (!m_levelMap3D) {
		return;
	}
	if (!m_levelMaterialAnimator) {
		m_levelMaterialAnimator = new LevelMaterialAnimator(m_levelMap3D, this);
	}
	m_levelMaterialAnimator->setReducedMotion(m_settings.accessibilityPreferences().reducedMotion);
	m_levelMaterialAnimator->setEnabled(m_settings.shellLayoutState(kAnimateMaterialsKey) == QByteArrayLiteral("on"));
	// Doom faces carry the camera's own flat and wall keys, not material names.
	if (mesh.surfaces.isEmpty() || m_levelMapDocument.format == LevelMapFormat::DoomWad || !m_packageArchive.isOpen()) {
		m_levelMaterialAnimator->stop();
		return;
	}
	const QStringList basePaths = materialBasePackagePaths();
	const QString key = packageViewKey() + QLatin1Char('|') + basePaths.join(QLatin1Char('|'));
	if (key != m_levelMaterialAnimatorKey) {
		m_levelMaterialAnimatorKey = key;
		m_levelMaterialAnimator->setPackage(std::make_shared<PackageArchive>(packageViewArchive()), basePaths);
	}
	const MaterialEngine engine = m_levelMapDocument.format == LevelMapFormat::Quake3Map ? MaterialEngine::Quake3 : MaterialEngine::Unknown;
	m_levelMaterialAnimator->setSurfaces(mesh, engine, stillImages);
}

void ApplicationShell::editMaterialImageInTextureEditor(const QString& reference)
{
	if (resolveTextureReference(reference).isEmpty()) {
		showTextureReference(reference);
		return;
	}
	showTextureReference(reference);
	// The Texture Editor edits the texture the Textures page has decoded: wait
	// for this one to finish decoding, then open it.
	auto* wait = new QTimer(this);
	wait->setInterval(100);
	auto started = std::make_shared<QElapsedTimer>();
	started->start();
	connect(wait, &QTimer::timeout, this, [this, wait, started]() {
		const bool ready = m_texturePreview && m_texturePreview->hasImage() && !m_texturePreviewPending;
		if (!ready && started->elapsed() < 20000) {
			return;
		}
		wait->stop();
		wait->deleteLater();
		if (ready) {
			showTextureEditor(true);
		} else {
			statusBar()->showMessage(tr("The texture is still loading; choose Edit in Texture Editor on the Textures page once it shows."));
		}
	});
	wait->start();
}

void ApplicationShell::showMaterialMapUses()
{
	if (!m_materialWorkbench) {
		return;
	}
	syncMaterialWorkbench();
	setMode(StudioMode::Shaders);
	if (m_shaderPageTabs) {
		m_shaderPageTabs->setCurrentIndex(0);
	}
	m_materialWorkbench->showMapUses();
}

bool ApplicationShell::openMaterialMapUse(const MaterialMapUse& use, const QString& material, QString* error)
{
	const auto failed = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (use.kind == MaterialMapKind::Bsp) {
		return failed(tr("Compiled maps do not open in Levels; open the map's .map source."));
	}
	// The map's file: the open WAD itself, a file of a folder package, or a
	// copy of an entry inside an archive.
	const QString packagePath = packageOpenPath();
	QString path;
	if (use.kind == MaterialMapKind::DoomMap && use.mapPath.isEmpty()) {
		path = packagePath;
	} else if (QFileInfo(packagePath).isDir()) {
		path = QDir(packagePath).filePath(use.mapPath);
	}
	if (path.isEmpty() || !QFileInfo(path).isFile()) {
		const QVector<PackageEntry> entries = packageViewArchive().entries();
		const bool inPackage = std::any_of(entries.cbegin(), entries.cend(),
			[&](const PackageEntry& entry) { return entry.virtualPath.compare(use.mapPath, Qt::CaseInsensitive) == 0; });
		if (!inPackage) {
			return failed(tr("%1 is in a base package; open it from that package.").arg(use.mapPath));
		}
		path = extractPackageEntryCopy(use.mapPath);
		if (path.isEmpty()) {
			return failed(tr("%1 could not be copied out of the package.").arg(use.mapPath));
		}
	}
	if (!confirmLevelMapEditsHandled(tr("Save the open map before opening %1?").arg(QFileInfo(path).fileName()))) {
		return failed(tr("The open map was kept."));
	}
	// A WAD map is named by its marker; Levels reads the name from its field.
	if (m_levelMapName && use.kind == MaterialMapKind::DoomMap) {
		m_levelMapName->setEditText(use.mapName);
	}
	loadLevelMapPath(path);
	if (QDir::cleanPath(m_levelMapDocument.sourcePath).compare(QDir::cleanPath(path), Qt::CaseInsensitive) != 0) {
		return failed(tr("%1 did not open; see Levels for why.").arg(QDir::toNativeSeparators(path)));
	}
	setMode(StudioMode::Levels);
	selectLevelMapObjectsByTexture(use.textureName.isEmpty() ? material : use.textureName);
	return true;
}

void ApplicationShell::showMaterialsForImage(const QString& virtualPath)
{
	if (!m_materialWorkbench) {
		return;
	}
	syncMaterialWorkbench();
	setMode(StudioMode::Shaders);
	if (m_shaderPageTabs) {
		m_shaderPageTabs->setCurrentIndex(0);
	}
	m_materialWorkbench->showMaterialsUsingImage(virtualPath);
}

void ApplicationShell::showMaterialNamed(const QString& name)
{
	if (!m_materialWorkbench) {
		return;
	}
	syncMaterialWorkbench();
	// Before the page shows (which reads the package), so the material is
	// picked when the library is ready.
	m_materialWorkbench->revealMaterial(name);
	setMode(StudioMode::Shaders);
	if (m_shaderPageTabs) {
		m_shaderPageTabs->setCurrentIndex(0);
	}
}

void ApplicationShell::openMaterialScript(const QString& path)
{
	if (!m_materialWorkbench) {
		return;
	}
	syncMaterialWorkbench();
	QString error;
	if (!m_materialWorkbench->openScript(path, &error)) {
		statusBar()->showMessage(error);
		return;
	}
	if (m_shaderPageTabs) {
		m_shaderPageTabs->setCurrentIndex(0);
	}
	recordActivity(tr("Material script opened"), QDir::toNativeSeparators(path), QStringLiteral("shader"), OperationState::Completed,
		QFileInfo(path).fileName());
}

} // namespace vibestudio
