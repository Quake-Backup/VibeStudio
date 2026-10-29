#pragma once

// The Levels Map tab's inspector widgets, after TrenchBroom's Map inspector
// (layers, map properties and mods) and NetRadiant Custom's Map Info (the
// entity class breakdown). Each widget shows values worked out by
// core/level_map_inspector.h and core/level_map.h and asks for changes by
// signal; the shell makes them through the shared services, so the Map tab,
// command search and `map properties` change the map the same way.
//
// The shell builds them into the Map tab in buildLevelMapInspector()
// (app/level_map_inspector_actions.cpp), the one place later Map tab
// sections, such as an issue summary with quick fixes, join it.

#include "core/level_map.h"
#include "core/level_map_inspector.h"

#include <QListWidget>
#include <QTreeWidget>
#include <QWidget>

class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QToolButton;

namespace vibestudio {

// Map Info's class table: every entity class with how many entities have it
// and how many reach the game, sorted by any column.
class LevelMapClassTable final : public QTreeWidget {
	Q_OBJECT

public:
	enum Column { ClassColumn, CountColumn, InGameColumn };

	explicit LevelMapClassTable(QWidget* parent = nullptr);
	// Keeps the sort order and the current class across refreshes.
	void setStatistics(const LevelMapDocument& document, const LevelMapStatistics& statistics);
	void clearStatistics();
	[[nodiscard]] QString currentClassName() const;

Q_SIGNALS:
	// Enter or a double click on a class.
	void classActivated(const QString& className);
};

// One row per layer: its objects and groups, and whether it is hidden,
// locked, left out of exports or where new objects go, said in words.
class LevelMapLayerList final : public QListWidget {
	Q_OBJECT

public:
	explicit LevelMapLayerList(QWidget* parent = nullptr);
	void setLayers(const QVector<LevelMapLayerSummary>& layers);
	[[nodiscard]] QString currentLayerId() const;

Q_SIGNALS:
	void layerActivated(const QString& layerId);
};

// TrenchBroom's Map Properties: soft bounds from the game, the map's own or
// none.
class LevelMapPropertiesEditor final : public QWidget {
	Q_OBJECT

public:
	explicit LevelMapPropertiesEditor(QWidget* parent = nullptr);
	void setDocument(const LevelMapDocument& document);
	// Says why a change was refused, or clears the message.
	void setProblem(const QString& problem);

Q_SIGNALS:
	void softBoundsRequested(const vibestudio::LevelMapSoftBoundsChange& change);

private:
	void requestCustom(bool fromRadio);
	void updateFields();

	QButtonGroup* m_modes = nullptr;
	QRadioButton* m_game = nullptr;
	QRadioButton* m_custom = nullptr;
	QRadioButton* m_disabled = nullptr;
	QLabel* m_gameBounds = nullptr;
	QLineEdit* m_min = nullptr;
	QLineEdit* m_max = nullptr;
	QLabel* m_status = nullptr;
	bool m_updating = false;
	bool m_applies = false;
};

// TrenchBroom's Mods editor: the mods the map enables, in order, chosen from
// the folders beside the game's own or typed.
class LevelMapModsEditor final : public QWidget {
	Q_OBJECT

public:
	explicit LevelMapModsEditor(QWidget* parent = nullptr);
	// `available` lists the installation's mod folders, `applies` whether the
	// map can have mods at all.
	void setMods(const QStringList& enabled, const QStringList& available, bool applies);
	[[nodiscard]] QStringList enabledMods() const;
	void setProblem(const QString& problem);

Q_SIGNALS:
	void modsRequested(const QStringList& mods);

private:
	void enableTyped();
	void move(int offset);
	void updateButtons();

	QComboBox* m_name = nullptr;
	QPushButton* m_enable = nullptr;
	QListWidget* m_list = nullptr;
	QToolButton* m_remove = nullptr;
	QToolButton* m_up = nullptr;
	QToolButton* m_down = nullptr;
	QLabel* m_status = nullptr;
	QStringList m_enabled;
	QStringList m_available;
	bool m_applies = false;
};

} // namespace vibestudio
