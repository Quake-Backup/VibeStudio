#include "app/level_map_inspector.h"

#include "app/studio_icons.h"
#include "app/studio_layout.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace vibestudio {

namespace {

constexpr int kClassRole = Qt::UserRole;
constexpr int kCountRole = Qt::UserRole + 1;

// Sorts the class column by name without case and the count columns as
// numbers, ties by name.
class ClassItem final : public QTreeWidgetItem {
public:
	using QTreeWidgetItem::QTreeWidgetItem;

	bool operator<(const QTreeWidgetItem& other) const override
	{
		const int column = treeWidget() ? treeWidget()->sortColumn() : 0;
		const QString name = data(LevelMapClassTable::ClassColumn, kClassRole).toString();
		const QString otherName = other.data(LevelMapClassTable::ClassColumn, kClassRole).toString();
		if (column != LevelMapClassTable::ClassColumn) {
			const int count = data(column, kCountRole).toInt();
			const int otherCount = other.data(column, kCountRole).toInt();
			if (count != otherCount) {
				return count < otherCount;
			}
		}
		const int folded = text(LevelMapClassTable::ClassColumn).compare(other.text(LevelMapClassTable::ClassColumn), Qt::CaseInsensitive);
		return folded != 0 ? folded < 0 : name < otherName;
	}
};

QToolButton* listButton(const QString& objectName, const QString& icon, const QString& text, QWidget* parent)
{
	auto* button = new QToolButton(parent);
	button->setObjectName(objectName);
	button->setIcon(studioIcon(icon));
	button->setText(text);
	button->setToolTip(text);
	button->setAccessibleName(text);
	button->setToolButtonStyle(Qt::ToolButtonIconOnly);
	button->setAutoRaise(true);
	button->setFocusPolicy(Qt::TabFocus);
	setBaseIconSize(button, QSize(16, 16));
	return button;
}

} // namespace

// Workflow reference: NetRadiant Custom map.cpp DoMapInfo's sortable entity
// breakdown at 449778b3 (GPL-2.0-or-later). Original Qt implementation; see
// docs/CREDITS.md.
LevelMapClassTable::LevelMapClassTable(QWidget* parent)
	: QTreeWidget(parent)
{
	setObjectName(QStringLiteral("levelMapEntityClasses"));
	setAccessibleName(tr("Entity classes"));
	setAccessibleDescription(tr("Every entity class in the map, with how many entities have it and how many of those reach the game. "
								"Choose a heading to sort by it; press Enter on a class to list its entities in the Outliner."));
	setColumnCount(3);
	setHeaderLabels({tr("Class"), tr("Count"), tr("In game")});
	headerItem()->setToolTip(CountColumn, tr("Entities of the class in the map."));
	headerItem()->setToolTip(InGameColumn,
		tr("Entities of the class the compiler keeps. It drops lights, misc_model and info_null, and merges func_group, func_detail and _decal into the world."));
	setRootIsDecorated(false);
	setUniformRowHeights(true);
	setAlternatingRowColors(true);
	setSelectionMode(QAbstractItemView::SingleSelection);
	setEditTriggers(QAbstractItemView::NoEditTriggers);
	setSortingEnabled(true);
	sortByColumn(ClassColumn, Qt::AscendingOrder);
	header()->setStretchLastSection(false);
	header()->setSectionResizeMode(ClassColumn, QHeaderView::Stretch);
	header()->setSectionResizeMode(CountColumn, QHeaderView::ResizeToContents);
	header()->setSectionResizeMode(InGameColumn, QHeaderView::ResizeToContents);
	setMinimumHeight(160);
	connect(this, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
		if (item) {
			Q_EMIT classActivated(item->data(ClassColumn, kClassRole).toString());
		}
	});
}

void LevelMapClassTable::setStatistics(const LevelMapDocument& document, const LevelMapStatistics& statistics)
{
	const QString current = currentClassName();
	const int column = sortColumn();
	const Qt::SortOrder order = header()->sortIndicatorOrder();
	setSortingEnabled(false);
	clear();
	const bool doom = document.format == LevelMapFormat::DoomWad;
	setColumnHidden(InGameColumn, doom);
	QHash<int, QString> thingNames;
	if (doom) {
		for (const LevelMapDoomThingType& type : levelMapDoomThingTypes(document.doomFormat)) {
			thingNames.insert(type.type, type.name);
		}
	}
	const QLocale locale;
	for (const LevelMapEntityClassCount& row : statistics.entityClasses) {
		auto* item = new ClassItem(this);
		QString shown = row.className;
		if (doom && row.className.startsWith(QLatin1String("thing:"))) {
			bool known = false;
			const int type = row.className.mid(6).toInt(&known);
			shown = known && thingNames.contains(type) ? tr("%1 (%2)").arg(thingNames.value(type)).arg(type) : tr("Thing %1").arg(row.className.mid(6));
		}
		item->setText(ClassColumn, shown);
		item->setData(ClassColumn, kClassRole, row.className);
		item->setText(CountColumn, locale.toString(row.count));
		item->setData(CountColumn, kCountRole, row.count);
		item->setText(InGameColumn, locale.toString(row.inGameCount));
		item->setData(InGameColumn, kCountRole, row.inGameCount);
		item->setTextAlignment(CountColumn, Qt::AlignTrailing | Qt::AlignVCenter);
		item->setTextAlignment(InGameColumn, Qt::AlignTrailing | Qt::AlignVCenter);
		const int count = row.count;
		const int groups = row.groupCount;
		const int inGame = row.inGameCount;
		QString spoken = doom ? tr("%1: %n thing(s)", nullptr, count).arg(shown) : tr("%1: %n in the map", nullptr, count).arg(shown) + QStringLiteral(", ")
				+ tr("%n in the game", nullptr, inGame);
		if (groups > 0) {
			spoken += QStringLiteral(", ") + tr("%n hold brushes or patches", nullptr, groups);
		}
		item->setData(ClassColumn, Qt::AccessibleTextRole, spoken);
		item->setToolTip(ClassColumn, spoken);
		if (row.className == current) {
			setCurrentItem(item);
		}
	}
	setSortingEnabled(true);
	sortByColumn(column, order);
}

void LevelMapClassTable::clearStatistics()
{
	clear();
}

QString LevelMapClassTable::currentClassName() const
{
	const QTreeWidgetItem* item = currentItem();
	return item ? item->data(ClassColumn, kClassRole).toString() : QString();
}

LevelMapLayerList::LevelMapLayerList(QWidget* parent)
	: QListWidget(parent)
{
	setObjectName(QStringLiteral("levelMapLayers"));
	setAccessibleName(tr("Layers"));
	setAccessibleDescription(tr("The map's layers, with what each holds and its state. Press Enter on a layer to manage it in the Outliner."));
	setSelectionMode(QAbstractItemView::SingleSelection);
	setTextElideMode(Qt::ElideMiddle);
	setBaseIconSize(this, QSize(16, 16));
	connect(this, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
		if (item) {
			Q_EMIT layerActivated(item->data(Qt::UserRole).toString());
		}
	});
}

void LevelMapLayerList::setLayers(const QVector<LevelMapLayerSummary>& layers)
{
	const bool hadCurrent = currentItem() != nullptr;
	const QString current = currentLayerId();
	clear();
	for (const LevelMapLayerSummary& layer : layers) {
		QStringList states;
		if (!layer.visible) {
			states << tr("Hidden");
		}
		if (layer.locked) {
			states << tr("Locked");
		}
		if (layer.omitFromExport) {
			states << tr("Omitted from export");
		}
		if (layer.creation) {
			states << tr("New objects go here");
		}
		const int objects = layer.objects;
		const int groups = layer.groups;
		QString detail = groups > 0 ? tr("%1, %2").arg(tr("%n object(s)", nullptr, objects), tr("%n group(s)", nullptr, groups))
									: tr("%n object(s)", nullptr, objects);
		if (!states.isEmpty()) {
			detail = tr("%1 · %2").arg(detail, states.join(QStringLiteral(", ")));
		}
		auto* item = new QListWidgetItem(studioIcon(QStringLiteral("layers"), layer.visible ? StudioIconTone::Normal : StudioIconTone::Muted),
			QStringLiteral("%1\n%2").arg(layer.name, detail), this);
		item->setData(Qt::UserRole, layer.id);
		item->setData(Qt::AccessibleTextRole, tr("%1: %2").arg(layer.name, detail));
		item->setToolTip(tr("%1: %2").arg(layer.name, detail));
		if (hadCurrent && layer.id == current) {
			setCurrentItem(item);
		}
	}
	fitListHeightToRows(this, 48, 260);
}

QString LevelMapLayerList::currentLayerId() const
{
	const QListWidgetItem* item = currentItem();
	return item ? item->data(Qt::UserRole).toString() : QString();
}

// Workflow reference: TrenchBroom MapPropertiesEditor at 4c756be1
// (GPL-3.0-or-later): three choices, corners as three numbers or one, a
// custom box committed once both corners read. Original Qt implementation;
// see docs/CREDITS.md.
LevelMapPropertiesEditor::LevelMapPropertiesEditor(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("levelMapPropertiesEditor"));
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);
	auto* caption = new QLabel(tr("Soft bounds"), this);
	caption->setObjectName(QStringLiteral("levelMapSoftBoundsCaption"));
	layout->addWidget(caption);
	m_modes = new QButtonGroup(this);
	m_disabled = new QRadioButton(tr("No soft bounds"), this);
	m_disabled->setObjectName(QStringLiteral("levelMapSoftBoundsNone"));
	m_disabled->setToolTip(tr("Nothing is checked against soft bounds. Writes \"none\" to worldspawn's _tb_soft_map_bounds key."));
	m_game = new QRadioButton(tr("The game's bounds"), this);
	m_game->setObjectName(QStringLiteral("levelMapSoftBoundsGame"));
	m_game->setToolTip(tr("The bounds the game can address. Takes worldspawn's _tb_soft_map_bounds key away."));
	m_custom = new QRadioButton(tr("Custom bounds"), this);
	m_custom->setObjectName(QStringLiteral("levelMapSoftBoundsCustom"));
	m_custom->setToolTip(tr("The map's own box, saved in worldspawn's _tb_soft_map_bounds key as TrenchBroom saves it."));
	for (QRadioButton* button : {m_disabled, m_game, m_custom}) {
		button->setAccessibleName(button->text());
		button->setAccessibleDescription(button->toolTip());
		m_modes->addButton(button);
	}
	layout->addWidget(m_disabled);
	layout->addWidget(m_game);
	m_gameBounds = new QLabel(this);
	m_gameBounds->setObjectName(QStringLiteral("levelMapSoftBoundsGameValue"));
	m_gameBounds->setWordWrap(true);
	m_gameBounds->setIndent(24);
	layout->addWidget(m_gameBounds);
	layout->addWidget(m_custom);
	auto* corners = new QFormLayout;
	corners->setContentsMargins(24, 0, 0, 0);
	corners->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_min = new QLineEdit(this);
	m_min->setObjectName(QStringLiteral("levelMapSoftBoundsMin"));
	m_min->setAccessibleName(tr("Minimum corner"));
	m_min->setAccessibleDescription(tr("Three numbers, x y z, or one number for all three."));
	m_min->setPlaceholderText(tr("x y z, or one number"));
	m_max = new QLineEdit(this);
	m_max->setObjectName(QStringLiteral("levelMapSoftBoundsMax"));
	m_max->setAccessibleName(tr("Maximum corner"));
	m_max->setAccessibleDescription(m_min->accessibleDescription());
	m_max->setPlaceholderText(m_min->placeholderText());
	corners->addRow(tr("Minimum"), m_min);
	corners->addRow(tr("Maximum"), m_max);
	layout->addLayout(corners);
	m_status = new QLabel(this);
	m_status->setObjectName(QStringLiteral("levelMapSoftBoundsStatus"));
	m_status->setAccessibleName(tr("Soft bounds status"));
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	layout->addWidget(m_status);

	connect(m_disabled, &QRadioButton::clicked, this, [this]() {
		updateFields();
		Q_EMIT softBoundsRequested({LevelMapSoftBoundsMode::Disabled, {}, {}});
	});
	connect(m_game, &QRadioButton::clicked, this, [this]() {
		updateFields();
		Q_EMIT softBoundsRequested({LevelMapSoftBoundsMode::Game, {}, {}});
	});
	connect(m_custom, &QRadioButton::clicked, this, [this]() {
		updateFields();
		requestCustom(true);
	});
	for (QLineEdit* field : {m_min, m_max}) {
		connect(field, &QLineEdit::editingFinished, this, [this]() {
			// Disabling a field while the map refreshes finishes its editing
			// too; only the user's own edits count.
			if (!m_updating && m_custom->isChecked()) {
				requestCustom(false);
			}
		});
	}
	updateFields();
}

void LevelMapPropertiesEditor::setDocument(const LevelMapDocument& document)
{
	const QScopedValueRollback<bool> updating(m_updating, true);
	m_applies = document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
	const LevelMapSoftBounds bounds = levelMapSoftBounds(document);
	const LevelMapSoftBounds game = levelMapGameSoftBounds(document);
	m_gameBounds->setText(game.valid ? tr("Minimum %1, maximum %2").arg(levelMapSoftBoundsCornerText(game.mins), levelMapSoftBoundsCornerText(game.maxs))
									 : tr("No game bounds for this map."));
	switch (bounds.mode) {
	case LevelMapSoftBoundsMode::Disabled:
		m_disabled->setChecked(true);
		break;
	case LevelMapSoftBoundsMode::Custom:
		m_custom->setChecked(true);
		break;
	case LevelMapSoftBoundsMode::Game:
		m_game->setChecked(true);
		break;
	}
	// The fields hold the map's box, else a starting point: the game's.
	if (bounds.mode == LevelMapSoftBoundsMode::Custom && bounds.valid) {
		m_min->setText(levelMapSoftBoundsCornerText(bounds.mins));
		m_max->setText(levelMapSoftBoundsCornerText(bounds.maxs));
	} else if (game.valid && (m_min->text().trimmed().isEmpty() || !m_custom->isChecked())) {
		m_min->setText(levelMapSoftBoundsCornerText(game.mins));
		m_max->setText(levelMapSoftBoundsCornerText(game.maxs));
	}
	QString status;
	if (!m_applies) {
		status = document.format == LevelMapFormat::DoomWad ? tr("Doom maps have no soft bounds.") : tr("Open a Quake-family map to set its soft bounds.");
	} else if (bounds.malformed) {
		status = tr("Worldspawn's value “%1” is not two corners with each minimum below its maximum, so no bounds are in force. Type new corners, or choose another option.")
					 .arg(bounds.value);
	} else if (bounds.mode == LevelMapSoftBoundsMode::Disabled) {
		status = tr("No soft bounds: nothing is checked against them.");
	} else if (bounds.valid) {
		const int outside = static_cast<int>(levelMapObjectsOutsideBounds(document, bounds.mins, bounds.maxs).size());
		status = outside == 0 ? tr("Everything lies inside these bounds.") : tr("%n object(s) lie outside these bounds.", nullptr, outside);
	}
	m_status->setText(status);
	updateFields();
}

void LevelMapPropertiesEditor::setProblem(const QString& problem)
{
	if (!problem.isEmpty()) {
		m_status->setText(problem);
	}
}

void LevelMapPropertiesEditor::requestCustom(bool fromRadio)
{
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	if (!parseLevelMapSoftBoundsCorner(m_min->text(), &mins) || !parseLevelMapSoftBoundsCorner(m_max->text(), &maxs)) {
		// Choosing Custom with corners still to type commits nothing yet.
		if (!fromRadio) {
			m_status->setText(tr("Type each corner as three numbers, x y z, or one number for all three."));
		}
		return;
	}
	if (mins.x >= maxs.x || mins.y >= maxs.y || mins.z >= maxs.z) {
		m_status->setText(tr("Each minimum must be below its maximum."));
		return;
	}
	Q_EMIT softBoundsRequested({LevelMapSoftBoundsMode::Custom, mins, maxs});
}

void LevelMapPropertiesEditor::updateFields()
{
	for (QRadioButton* button : {m_disabled, m_game, m_custom}) {
		button->setEnabled(m_applies);
	}
	m_min->setEnabled(m_applies && m_custom->isChecked());
	m_max->setEnabled(m_applies && m_custom->isChecked());
}

// Workflow reference: TrenchBroom ModEditor at 4c756be1 (GPL-3.0-or-later):
// the folders beside the game's own as candidates, an ordered enabled list
// with add, remove and move. Original Qt implementation; see
// docs/CREDITS.md.
LevelMapModsEditor::LevelMapModsEditor(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("levelMapModsEditor"));
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);
	auto* add = new QHBoxLayout;
	add->setSpacing(4);
	m_name = new QComboBox(this);
	m_name->setObjectName(QStringLiteral("levelMapModName"));
	m_name->setEditable(true);
	m_name->setInsertPolicy(QComboBox::NoInsert);
	m_name->setAccessibleName(tr("Mod to enable"));
	m_name->setAccessibleDescription(tr("A mod folder of the game, from the list or typed."));
	m_name->setToolTip(m_name->accessibleDescription());
	m_name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	if (m_name->lineEdit()) {
		m_name->lineEdit()->setPlaceholderText(tr("Mod folder"));
		connect(m_name->lineEdit(), &QLineEdit::returnPressed, this, &LevelMapModsEditor::enableTyped);
	}
	m_enable = new QPushButton(studioIcon(QStringLiteral("plus")), tr("Enable"), this);
	m_enable->setObjectName(QStringLiteral("levelMapModEnable"));
	m_enable->setAccessibleName(tr("Enable the mod"));
	m_enable->setToolTip(tr("Add the mod to the end of the map's list."));
	add->addWidget(m_name, 1);
	add->addWidget(m_enable);
	layout->addLayout(add);
	m_list = new QListWidget(this);
	m_list->setObjectName(QStringLiteral("levelMapModList"));
	m_list->setAccessibleName(tr("Enabled mods"));
	m_list->setAccessibleDescription(tr("The mods this map enables, in order, as worldspawn's _tb_mod key keeps them for TrenchBroom."));
	m_list->setSelectionMode(QAbstractItemView::SingleSelection);
	auto* row = new QHBoxLayout;
	row->setSpacing(2);
	m_remove = listButton(QStringLiteral("levelMapModRemove"), QStringLiteral("minus"), tr("Disable the chosen mod"), this);
	m_up = listButton(QStringLiteral("levelMapModUp"), QStringLiteral("chevron-up"), tr("Move the chosen mod up"), this);
	m_down = listButton(QStringLiteral("levelMapModDown"), QStringLiteral("chevron-down"), tr("Move the chosen mod down"), this);
	row->addWidget(m_remove);
	row->addWidget(m_up);
	row->addWidget(m_down);
	row->addStretch(1);
	layout->addWidget(m_list);
	layout->addLayout(row);
	m_status = new QLabel(this);
	m_status->setObjectName(QStringLiteral("levelMapModsStatus"));
	m_status->setAccessibleName(tr("Mods status"));
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	layout->addWidget(m_status);

	connect(m_enable, &QPushButton::clicked, this, &LevelMapModsEditor::enableTyped);
	connect(m_name, &QComboBox::currentTextChanged, this, [this]() { updateButtons(); });
	connect(m_list, &QListWidget::currentRowChanged, this, [this]() { updateButtons(); });
	connect(m_remove, &QToolButton::clicked, this, [this]() {
		const int row = m_list->currentRow();
		if (row >= 0 && row < m_enabled.size()) {
			QStringList mods = m_enabled;
			mods.removeAt(row);
			Q_EMIT modsRequested(mods);
		}
	});
	connect(m_up, &QToolButton::clicked, this, [this]() { move(-1); });
	connect(m_down, &QToolButton::clicked, this, [this]() { move(1); });
	setMods({}, {}, false);
}

void LevelMapModsEditor::setMods(const QStringList& enabled, const QStringList& available, bool applies)
{
	const QString current = m_list->currentItem() ? m_list->currentItem()->text() : QString();
	m_enabled = enabled;
	m_available = available;
	m_applies = applies;
	m_list->clear();
	for (const QString& mod : enabled) {
		auto* item = new QListWidgetItem(studioIcon(QStringLiteral("folder")), mod, m_list);
		if (mod == current) {
			m_list->setCurrentItem(item);
		}
	}
	fitListHeightToRows(m_list, 48, 200);
	{
		const QSignalBlocker blocker(m_name);
		m_name->clear();
		for (const QString& mod : available) {
			if (!enabled.contains(mod, Qt::CaseInsensitive)) {
				m_name->addItem(mod);
			}
		}
		m_name->setCurrentIndex(-1);
		m_name->setEditText(QString());
	}
	setEnabled(applies);
	const int candidates = m_name->count();
	m_status->setText(!applies ? tr("Mods belong to Quake-family maps.")
		: candidates > 0       ? tr("Choose one of the game's %n other mod folder(s), or type a folder name.", nullptr, candidates)
							   : tr("Type a mod's folder name to enable it; choose the game's installation to list its folders."));
	updateButtons();
}

QStringList LevelMapModsEditor::enabledMods() const
{
	return m_enabled;
}

void LevelMapModsEditor::setProblem(const QString& problem)
{
	if (!problem.isEmpty()) {
		m_status->setText(problem);
	}
}

void LevelMapModsEditor::enableTyped()
{
	const QString name = m_name->currentText().trimmed();
	if (const QString problem = levelMapModNameProblem(name); !problem.isEmpty()) {
		m_status->setText(problem);
		return;
	}
	if (m_enabled.contains(name, Qt::CaseInsensitive)) {
		m_status->setText(tr("%1 is already enabled.").arg(name));
		return;
	}
	Q_EMIT modsRequested(m_enabled + QStringList {name});
}

void LevelMapModsEditor::move(int offset)
{
	const int row = m_list->currentRow();
	const int target = row + offset;
	if (row < 0 || target < 0 || target >= m_enabled.size()) {
		return;
	}
	QStringList mods = m_enabled;
	mods.swapItemsAt(row, target);
	const QString moved = mods.at(target);
	Q_EMIT modsRequested(mods);
	// The refreshed list keeps the moved mod chosen.
	for (int index = 0; index < m_list->count(); ++index) {
		if (m_list->item(index)->text() == moved) {
			m_list->setCurrentRow(index);
			break;
		}
	}
}

void LevelMapModsEditor::updateButtons()
{
	const int row = m_list->currentRow();
	m_enable->setEnabled(m_applies && !m_name->currentText().trimmed().isEmpty());
	m_remove->setEnabled(m_applies && row >= 0);
	m_up->setEnabled(m_applies && row > 0);
	m_down->setEnabled(m_applies && row >= 0 && row < m_list->count() - 1);
}

} // namespace vibestudio
