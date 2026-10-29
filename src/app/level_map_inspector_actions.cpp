// The Levels Map tab's inspector, after TrenchBroom's Map inspector and
// NetRadiant Custom's Map Info: the layer summary, Map Properties (soft
// bounds), Mods and the Statistics section with its class table, and the
// Map Info command. Changes go through core/level_map_inspector.h, the
// service `map properties` uses.
//
// Later Map tab sections, such as an issue summary with quick fixes, are
// built in buildLevelMapInspector() and refreshed in refreshLevelMapInspector()
// with the rest.

#include "app/application_shell.h"
#include "app/level_map_inspector.h"
#include "app/level_scene_panel.h"
#include "app/studio_sidebar.h"
#include "core/game_installation.h"
#include "core/level_map_inspector.h"

#include <QLabel>
#include <QListWidget>
#include <QScrollArea>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace vibestudio {

namespace {

bool quakeFamily(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
}

// The Outliner query that lists a class's entities, or a Doom thing type's
// things.
QString classQuery(const QString& className)
{
	if (className.startsWith(QLatin1String("thing:"))) {
		return QStringLiteral("kind=thing type=%1").arg(className.mid(6));
	}
	const QString value = className.contains(QLatin1Char(' ')) ? QStringLiteral("\"%1\"").arg(className) : className;
	return QStringLiteral("kind=entity class=%1").arg(value);
}

} // namespace

void ApplicationShell::buildLevelMapInspector(SidebarPage* page, QWidget* statistics)
{
	// Layers: what each holds, and the way to the Outliner's layer tools.
	m_levelMapLayerList = new LevelMapLayerList;
	SidebarSection* layers = addLevelSidebarSection(page, QStringLiteral("map.layers"), tr("Layers"), m_levelMapLayerList);
	auto* manage = layers->addHeaderButton(QStringLiteral("tree"), tr("Manage Layers"),
		tr("Open the Outliner's Layers and Groups section to show, hide, lock, rename or add layers."));
	manage->setObjectName(QStringLiteral("levelMapLayersManage"));
	const auto showLayer = [this](const QString& layerId) {
		setMode(StudioMode::Levels);
		showLevelSidebarPage(QStringLiteral("outliner"), QStringLiteral("outliner.scene"), false);
		if (m_levelScenePanel) {
			m_levelScenePanel->showNode(layerId);
		}
	};
	connect(manage, &QToolButton::clicked, this, [this, showLayer]() {
		showLayer(m_levelMapLayerList ? m_levelMapLayerList->currentLayerId() : QString());
	});
	connect(m_levelMapLayerList, &LevelMapLayerList::layerActivated, this, showLayer);

	// Map Properties: TrenchBroom's soft bounds.
	m_levelMapPropertiesEditor = new LevelMapPropertiesEditor;
	addLevelSidebarSection(page, QStringLiteral("map.properties"), tr("Map Properties"), m_levelMapPropertiesEditor);
	connect(m_levelMapPropertiesEditor, &LevelMapPropertiesEditor::softBoundsRequested, this, [this](const LevelMapSoftBoundsChange& change) {
		applyLevelMapSoftBounds(change);
	});

	// Mods: the mod folders the map enables, as TrenchBroom keeps them.
	m_levelMapModsEditor = new LevelMapModsEditor;
	addLevelSidebarSection(page, QStringLiteral("map.mods"), tr("Mods"), m_levelMapModsEditor, 0, false);
	connect(m_levelMapModsEditor, &LevelMapModsEditor::modsRequested, this, [this](const QStringList& mods) { applyLevelMapEnabledMods(mods); });

	// Statistics: the summary lines, then Map Info's class table.
	auto* panel = new QWidget;
	panel->setObjectName(QStringLiteral("levelMapStatisticsPanel"));
	auto* layout = new QVBoxLayout(panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);
	if (statistics) {
		statistics->setMinimumHeight(140);
		layout->addWidget(statistics);
	}
	m_levelMapClassTable = new LevelMapClassTable;
	auto* caption = new QLabel(tr("Entity classes"));
	caption->setObjectName(QStringLiteral("levelMapEntityClassesCaption"));
	caption->setBuddy(m_levelMapClassTable);
	layout->addWidget(caption);
	layout->addWidget(m_levelMapClassTable);
	connect(m_levelMapClassTable, &LevelMapClassTable::classActivated, this, [this](const QString& className) {
		if (!className.isEmpty()) {
			findLevelMapObjectsLike(classQuery(className));
		}
	});
	addLevelSidebarSection(page, QStringLiteral("map.statistics"), tr("Statistics"), panel, 0, false);
}

void ApplicationShell::refreshLevelMapInspector()
{
	if (m_levelMapLayerList) {
		m_levelMapLayerList->setLayers(levelMapLayerSummaries(m_levelMapDocument));
	}
	if (m_levelMapPropertiesEditor) {
		m_levelMapPropertiesEditor->setDocument(m_levelMapDocument);
	}
	if (m_levelMapModsEditor) {
		const bool applies = quakeFamily(m_levelMapDocument);
		QStringList available;
		GameInstallationProfile installation;
		if (applies && selectedGameInstallation(&installation)) {
			// TrenchBroom's candidates: every folder beside the game's own.
			available = levelMapAvailableMods(installation.rootPath, {defaultGameDirectory(installation)});
		}
		m_levelMapModsEditor->setMods(levelMapEnabledMods(m_levelMapDocument), available, applies);
	}
}

bool ApplicationShell::applyLevelMapSoftBounds(const LevelMapSoftBoundsChange& change, QString* error)
{
	const auto revision = m_levelMapDocument.revision;
	QString problem;
	if (!setLevelMapSoftBounds(&m_levelMapDocument, change, &problem)) {
		const QString message = tr("Could not change the soft bounds: %1").arg(problem);
		if (error) {
			*error = message;
		}
		statusBar()->showMessage(message);
		refreshLevelMapInspector();
		if (m_levelMapPropertiesEditor) {
			m_levelMapPropertiesEditor->setProblem(message);
		}
		return false;
	}
	if (error) {
		error->clear();
	}
	if (m_levelMapDocument.revision == revision) {
		refreshLevelMapInspector();
		return true;
	}
	recordActivity(tr("Soft bounds changed"), levelMapInspectorLines(m_levelMapDocument).value(0), QStringLiteral("level-map"), OperationState::Warning,
		tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Soft bounds changed. Undo puts them back."), 4000);
	return true;
}

bool ApplicationShell::applyLevelMapEnabledMods(const QStringList& mods, QString* error)
{
	const auto revision = m_levelMapDocument.revision;
	QString problem;
	if (!setLevelMapEnabledMods(&m_levelMapDocument, mods, &problem)) {
		const QString message = tr("Could not change the mods: %1").arg(problem);
		if (error) {
			*error = message;
		}
		statusBar()->showMessage(message);
		refreshLevelMapInspector();
		if (m_levelMapModsEditor) {
			m_levelMapModsEditor->setProblem(message);
		}
		return false;
	}
	if (error) {
		error->clear();
	}
	if (m_levelMapDocument.revision == revision) {
		refreshLevelMapInspector();
		return true;
	}
	const QStringList enabled = levelMapEnabledMods(m_levelMapDocument);
	recordActivity(tr("Mods changed"), enabled.isEmpty() ? tr("No mods enabled") : enabled.join(QStringLiteral(", ")), QStringLiteral("level-map"),
		OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Mods changed. Undo puts them back."), 4000);
	return true;
}

void ApplicationShell::showLevelMapInfo()
{
	// NetRadiant Custom's Map Info (M): the totals and the class table, here
	// the Statistics section with the keyboard in its class table.
	setMode(StudioMode::Levels);
	if (!showLevelSidebarPage(QStringLiteral("map"), QStringLiteral("map.statistics"), false) || !m_levelMapClassTable) {
		return;
	}
	LevelMapClassTable* table = m_levelMapClassTable;
	if (!table->currentItem() && table->topLevelItemCount() > 0) {
		table->setCurrentItem(table->topLevelItem(0));
	}
	table->setFocus(Qt::ShortcutFocusReason);
	// Once the section has its height, scroll the page to the table.
	QTimer::singleShot(0, table, [table]() {
		for (QWidget* parent = table->parentWidget(); parent; parent = parent->parentWidget()) {
			if (auto* area = qobject_cast<QScrollArea*>(parent)) {
				area->ensureWidgetVisible(table);
				break;
			}
		}
	});
}

} // namespace vibestudio
