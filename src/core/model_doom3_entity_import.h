#pragma once

#include "core/model_assembly.h"

#include <QJsonObject>
#include <QPair>
#include <QStringList>
#include <QVector>

namespace vibestudio
{
class PackageArchiveReader;

// Doom 3 actors from the .def files of a package, as assembly recipes. The
// layout follows id Software's game/Actor.cpp (GPL-3.0 with id's additional
// terms): idActor::SetupHead puts the `def_head` model at the `head_joint`
// with the entity's own axis and binds it to that joint, keeping that relative
// placement; idActor::Spawn spawns each `def_attach*` entityDef and
// idActor::Attach places it on its `joint`, offset by its `origin` in the
// actor's model space and turned by its `angles` within the joint's axes.
// Inheritance follows idDeclEntityDef (`inherit`, a missing key comes from the
// parent) and model declarations follow parseDoom3ModelDecls.

// One `entityDef` block: its keys in file order (a repeated key keeps the
// last value, as idDict::Set does).
struct ModelDoom3EntityDef
{
	QString name;
	QVector<QPair<QString, QString>> keys;
	QString sourcePath;
	int line = 0;
	[[nodiscard]] QString value(const QString &key) const;
};
// Every entityDef in a .def file's text; other declarations are skipped.
// Problems go to `warnings` with their line numbers.
[[nodiscard]] QVector<ModelDoom3EntityDef> parseModelDoom3EntityDefs(const QString &text, const QString &sourcePath = {},
																	   QStringList *warnings = nullptr);

// An entityDef that names a head or attachments: what Open Doom 3 Entity offers.
struct ModelDoom3EntityListing
{
	QString name, sourcePath, model, head, headJoint;
	int attachments = 0;
};
// Reads every def/*.def entry; the first definition of a name wins, as
// Doom 3's declaration manager keeps it.
bool listModelDoom3Entities(const PackageArchiveReader &archive, QVector<ModelDoom3EntityListing> *entities, QString *error = nullptr,
							const ModelWorkControl &control = {});

struct ModelDoom3EntityImport
{
	QString entity;
	// The body (root, the entity's md5mesh) with the head and each attachment
	// on a joint:<name> tag, all package parts.
	ModelAssembly assembly;
	QStringList notes;
};
// Builds a new assembly from one entityDef. Reads the .def files and the
// body's md5mesh (for its joints' bind pose); the package is never changed.
// Refuses an entity without an md5mesh model, a head without head_joint, and
// a joint the body lacks, as the game stops on them.
bool importModelDoom3Entity(const PackageArchiveReader &archive, const QString &entity, ModelDoom3EntityImport *result,
							QString *error = nullptr, const ModelWorkControl &control = {});

QJsonObject modelDoom3EntityListingJson(const ModelDoom3EntityListing &listing);
QJsonObject modelDoom3EntityImportJson(const ModelDoom3EntityImport &result);
QStringList modelDoom3EntityImportText(const ModelDoom3EntityImport &result);
} // namespace vibestudio
