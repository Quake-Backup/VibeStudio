#include "app/application_shell.h"
#include "app/studio_actions.h"
#include "core/level_linked_groups.h"
#include "core/level_protected_properties.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace slopstudio
{
namespace
{
bool editableEntities(const LevelMapDocument& document, const QVector<int>& ids)
{
	if (ids.isEmpty() || document.format == LevelMapFormat::Unknown || document.format == LevelMapFormat::DoomWad ||
	    !document.scene.problem.isEmpty() || !document.scene.opaqueMetadata.isEmpty()) {
		return false;
	}
	const auto locked = levelSceneLockedObjects(document);
	// The selected entities in one pass over the map's, the first record of
	// each id as before. A pass per selected entity made this quadratic, and
	// it runs after every selection change.
	const QSet<int> wanted(ids.cbegin(), ids.cend());
	QHash<int, const LevelMapEntity*> entities;
	entities.reserve(wanted.size());
	for (const auto& entity : document.entities) {
		if (wanted.contains(entity.id) && !entities.contains(entity.id)) {
			entities.insert(entity.id, &entity);
			if (entities.size() == wanted.size()) {
				break;
			}
		}
	}
	for (int id : ids) {
		const LevelMapEntity* entity = entities.value(id);
		if (!entity || entity->className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0 ||
		    locked.contains(QStringLiteral("entity:%1").arg(id))) {
			return false;
		}
	}
	return true;
}

// Whether a selected entity has protected properties to clear: it has some and
// sits in a linked group with at least one other copy.
bool clearableProtectedProperties(const LevelMapDocument& document, const QVector<int>& ids)
{
	// Few entities have protected properties, so ask that first; the scene is
	// then read once for all of them rather than once per selected entity.
	QSet<QString> wanted;
	if (!document.scene.protectedProperties.isEmpty()) {
		for (int id : ids) {
			if (!levelProtectedEntityProperties(document, id).isEmpty()) {
				wanted.insert(QStringLiteral("entity:%1").arg(id));
			}
		}
	}
	if (wanted.isEmpty()) {
		return false;
	}
	for (const auto& node : document.scene.nodes) {
		for (const auto& object : node.objects) {
			// The first node that holds an object is its membership.
			if (wanted.remove(object) && !node.linkId.isEmpty() && levelLinkedGroupNodes(document.scene, node.id).size() > 1) {
				return true;
			}
		}
		if (wanted.isEmpty()) {
			break;
		}
	}
	return false;
}
} // namespace

void ApplicationShell::refreshLevelProtectedPropertyCommands()
{
	if (!m_commands) {
		return;
	}
	const auto ids = selectedLevelMapEntityIds();
	const bool editable = editableEntities(m_levelMapDocument, ids);
	// Clearing is offered only for an editable selection.
	const bool clear = editable && clearableProtectedProperties(m_levelMapDocument, ids);
	m_commands->setEnabled(QStringLiteral("map.addProtectedProperty"), editable);
	m_commands->setEnabled(QStringLiteral("map.clearProtectedProperties"), editable && clear);
}

void ApplicationShell::refreshEntityProtectionControls(const QString& previousKey, int previousColumn)
{
	const auto ids = selectedLevelMapEntityIds();
	if (!m_entityInspector || ids.isEmpty() || m_levelMapDocument.format == LevelMapFormat::DoomWad) {
		return;
	}
	bool show = false;
	QSet<QString> protectedKeys;
	for (int id : ids) {
		for (const auto& entity : m_levelMapDocument.entities) {
			if (entity.id == id && entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0) {
				return;
			}
		}
		const auto* node =
		    levelSceneNode(m_levelMapDocument.scene, levelSceneMembership(m_levelMapDocument.scene, QStringLiteral("entity:%1").arg(id)));
		show |= node && node->kind == LevelSceneNodeKind::Group;
		for (const auto& key : levelProtectedEntityProperties(m_levelMapDocument, id)) {
			protectedKeys.insert(key);
		}
	}
	show |= !protectedKeys.isEmpty();
	m_entityInspector->setColumnHidden(2, !show);
	if (!show) {
		return;
	}
	const bool editable = editableEntities(m_levelMapDocument, ids);
	const QSignalBlocker blocker(m_entityInspector);
	QSet<QString> represented;
	for (QTreeWidgetItemIterator it(m_entityInspector); *it; ++it) {
		const auto key = (*it)->data(0, Qt::UserRole).toString().toCaseFolded();
		if (!key.isEmpty()) {
			represented.insert(key);
		}
	}
	QStringList missing = (protectedKeys - represented).values();
	missing.sort();
	if (!missing.isEmpty()) {
		auto* group = new QTreeWidgetItem(m_entityInspector, {tr("Protected keys without values")});
		group->setFirstColumnSpanned(true);
		group->setExpanded(true);
		for (const auto& key : missing) {
			auto* row = new QTreeWidgetItem(group, {key, QString()});
			row->setData(0, Qt::UserRole, key);
			row->setData(1, Qt::AccessibleTextRole, tr("Not set"));
			row->setToolTip(1, tr("This property is absent. Protection keeps it absent when linked groups update."));
			row->setFlags(row->flags() | Qt::ItemIsEditable);
		}
	}
	QTreeWidgetItem* previous = nullptr;
	for (QTreeWidgetItemIterator it(m_entityInspector); *it; ++it) {
		auto* row = *it;
		const auto key = row->data(0, Qt::UserRole).toString();
		if (key.isEmpty()) {
			continue;
		}
		int count = 0;
		for (int id : ids) {
			count += levelEntityPropertyProtected(m_levelMapDocument, id, key) ? 1 : 0;
		}
		const auto state = count == ids.size() ? Qt::Checked : count == 0 ? Qt::Unchecked : Qt::PartiallyChecked;
		const auto label = state == Qt::Checked ? tr("Local") : state == Qt::Unchecked ? tr("Shared") : tr("Mixed");
		row->setFlags(editable ? row->flags() | Qt::ItemIsUserCheckable : row->flags() & ~Qt::ItemIsUserCheckable);
		row->setCheckState(2, state);
		row->setText(2, label);
		row->setData(2, Qt::AccessibleTextRole, tr("%1 protection: %2").arg(key, label));
		row->setToolTip(2, tr("Protected on %1 of %2 entities. Keep this property's value or absence local when linked groups update. "
		                      "Unprotecting restores an existing unprotected copy's value, when available.")
		                       .arg(count)
		                       .arg(ids.size()));
		if (!editable) {
			row->setToolTip(2, tr("Unlock the selected entities before changing property protection."));
		}
		if (key.compare(previousKey, Qt::CaseInsensitive) == 0) {
			previous = row;
		}
	}
	if (previous && previousColumn == 2) {
		m_entityInspector->setCurrentItem(previous, 2);
	}
	m_entityInspector->setAccessibleDescription(
	    m_entityInspector->accessibleDescription() +
	    tr(" The Protected column reports Local, Shared or Mixed. Space or Enter toggles protection for the selected entities."));
}

void ApplicationShell::applyEntityProtectionCheck(QTreeWidgetItem* item)
{
	if (!item) {
		return;
	}
	const auto key = item->data(0, Qt::UserRole).toString();
	if (key.isEmpty()) {
		return;
	}
	const bool protect = item->checkState(2) != Qt::Unchecked;
	QString error;
	if (!setLevelEntityPropertyProtection(&m_levelMapDocument, selectedLevelMapEntityIds(), {key}, protect, &error)) {
		statusBar()->showMessage(error, 8000);
	} else {
		statusBar()->showMessage(
		    protect ? tr("%1 is protected for this selection.").arg(key) : tr("%1 follows unprotected linked groups.").arg(key), 5000);
	}
	// QTreeWidget still uses the changed item after emitting itemChanged.
	// Rebuilding its rows inside that signal invalidates the active checkbox.
	// The document transaction is synchronous; refresh after Qt returns.
	QTimer::singleShot(0, this, [this] { refreshLevelMapWorkbench(); });
}

void ApplicationShell::clearProtectedEntityPropertiesFromUi()
{
	QString error;
	if (!clearLevelEntityPropertyProtection(&m_levelMapDocument, selectedLevelMapEntityIds(), &error)) {
		statusBar()->showMessage(error, 8000);
		return;
	}
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Cleared protected properties on the selected linked entities."), 5000);
}

void ApplicationShell::addProtectedEntityPropertyFromUi()
{
	const auto ids = selectedLevelMapEntityIds();
	if (!editableEntities(m_levelMapDocument, ids)) {
		statusBar()->showMessage(tr("Select unlocked non-worldspawn entities to add a protected property."), 6000);
		return;
	}
	auto candidate = m_levelMapDocument;
	const auto publish = levelPlacementCommitter();
	QDialog dialog(this);
	dialog.setLayoutDirection(layoutDirection());
	dialog.setObjectName(QStringLiteral("levelProtectedPropertyDialog"));
	dialog.setWindowTitle(tr("Add Protected Property"));
	dialog.setAccessibleName(dialog.windowTitle());
	dialog.setAccessibleDescription(
	    tr("Add a key and value for the selected entities. Existing corresponding values in linked groups are preserved."));
	auto* layout = new QVBoxLayout(&dialog);
	auto* form = new QFormLayout;
	auto* key = new QLineEdit(&dialog);
	key->setObjectName(QStringLiteral("protectedPropertyKey"));
	key->setAccessibleName(tr("Property key"));
	key->setMaxLength(1024);
	auto* value = new QLineEdit(&dialog);
	value->setObjectName(QStringLiteral("protectedPropertyValue"));
	value->setAccessibleName(tr("Property value"));
	form->addRow(tr("&Key"), key);
	form->addRow(tr("&Value"), value);
	layout->addLayout(form);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
	connect(key, &QLineEdit::textChanged, &dialog,
	        [=](const QString& text) { buttons->button(QDialogButtonBox::Ok)->setEnabled(!text.trimmed().isEmpty()); });
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	key->setFocus();
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	QString error;
	if (!setLevelProtectedEntityProperty(&candidate, ids, key->text(), {value->text()}, &error)) {
		statusBar()->showMessage(error, 8000);
		return;
	}
	if (!publish(candidate, &error)) {
		statusBar()->showMessage(tr("The map, selection or project changed. Reopen the protected-property editor."), 8000);
		return;
	}
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Set protected property %1.").arg(key->text().trimmed()), 5000);
}
} // namespace slopstudio
