#include "app/model_assembly_dialog.h"
#include "core/model_doom3_entity_import.h"

#include <QCoreApplication>
#include <QInputDialog>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelAssemblyDoom3Import)
};
} // namespace
bool ModelAssemblyDialog::openDoom3Entity(const QString &entity, QString *error)
{
	if (error)
		error->clear();
	play(false);
	const auto refuse = [&](const QString &message) {
		report(message);
		if (error)
			*error = message;
		return false;
	};
	const auto archive = m_materialSource.archive;
	if (!archive || !archive->isOpen())
		return refuse(Text::tr("Open a Doom 3 package or folder before opening an entity."));
	ModelDoom3EntityImport imported;
	if (!performWork(
			Text::tr("Open Doom 3 Entity"),
			[&](QString *failure, const ModelWorkControl &control) { return importModelDoom3Entity(*archive, entity, &imported, failure, control); },
			error))
		return false;
	if (archive != m_materialSource.archive)
		return refuse(Text::tr("The package changed while the entity was read. Open the entity again."));
	ModelAssemblyDocument candidate;
	QString failure;
	if (!candidate.setAssembly(imported.assembly, m_document.directory(), &failure))
		return refuse(failure);
	QString inputError;
	if (!adopt(std::move(candidate), &inputError, true))
	{
		if (error)
			*error = inputError;
		return false;
	}
	QStringList status{Text::tr("Opened entity %1 as a new, unsaved assembly. Save it to keep it; the package is not changed.").arg(imported.entity)};
	if (!inputError.isEmpty())
		status << Text::tr("Preview unavailable: %1").arg(inputError);
	// The first note repeats what the status says.
	status += imported.notes.mid(1);
	report(status.join(QLatin1Char(' ')));
	if (error)
		*error = inputError;
	return true;
}
void ModelAssemblyDialog::chooseDoom3Entity()
{
	if (m_working)
		return;
	play(false);
	const auto source = m_materialSource;
	if (!source.archive || !source.archive->isOpen())
	{
		report(Text::tr("Open a Doom 3 package or folder before opening an entity."));
		return;
	}
	QVector<ModelDoom3EntityListing> entities;
	QString error;
	if (!performWork(
			Text::tr("Find Doom 3 Entities"),
			[&](QString *failure, const ModelWorkControl &control) { return listModelDoom3Entities(*source.archive, &entities, failure, control); },
			&error))
		return;
	if (entities.isEmpty())
	{
		report(Text::tr("The current package has no entityDef with a def_head or def_attach in def/*.def."));
		return;
	}
	QStringList names;
	for (const auto &entity : std::as_const(entities))
		names << entity.name;
	bool accepted = false;
	const auto chosen = QInputDialog::getItem(this, Text::tr("Open Doom 3 Entity"),
											  Text::tr("Entities with a head or attachments, from the package's def/ files:"), names, 0, false,
											  &accepted);
	if (accepted && !chosen.isEmpty())
		openDoom3Entity(chosen, &error);
}
} // namespace vibestudio
