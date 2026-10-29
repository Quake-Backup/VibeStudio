#include "core/model_weapon_bundle.h"

#include "core/model_tags.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <array>

// Weapon model layout: id Software Quake III Arena code/cgame/cg_weapons.c,
// CG_RegisterWeapon (<w>.md3, <w>_flash.md3, <w>_barrel.md3, <w>_hand.md3) and
// CG_AddPlayerWeapon (tag_weapon, tag_barrel, tag_flash, barrel roll).
// GPL-2.0-or-later reference, reviewed 2026-10-28; original implementation.
// https://github.com/id-Software/Quake-III-Arena/blob/master/code/cgame/cg_weapons.c
namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelWeaponBundle)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
QString hex(const QByteArray &bytes)
{
	return QString::fromLatin1(bytes.toHex());
}
const ModelAssemblyPart *partNamed(const ModelAssembly &assembly, const QString &id)
{
	for (const auto &part : assembly.parts)
		if (part.id == id)
			return &part;
	return nullptr;
}
const ModelAssemblyInput *inputNamed(const ModelAssemblyResolved &resolved, const QString &id)
{
	for (const auto &input : resolved.inputs)
		if (input.part == id)
			return &input;
	return nullptr;
}
bool hasTag(const ModelMesh &mesh, const QString &name)
{
	return std::any_of(mesh.tags.cbegin(), mesh.tags.cend(), [&](const auto &tag) { return tag.name == name; });
}
// The product of every ancestor's uniform scale: what a part inherits.
double inheritedScale(const ModelAssembly &assembly, const ModelAssemblyPart &part)
{
	double scale = 1;
	for (auto *parent = partNamed(assembly, part.parent); parent; parent = partNamed(assembly, parent->parent))
		scale *= parent->scale;
	return scale;
}
} // namespace

bool prepareModelWeaponBundle(const ModelAssembly &assembly, const ModelAssemblyResolved &resolved, const ModelAssemblyContext &context,
							  const ModelWeaponBundleOptions &options, const QString &sourcePath, ModelWeaponBundle *result, QString *error,
							  const ModelWorkControl &control)
{
	if (error)
		error->clear();
	if (!result || !modelWorkCheckpoint(control, ModelWorkPhase::Validating, 0, 0, error))
		return false;
	if (resolved.recipeSha256 != modelAssemblyFingerprint(assembly) || resolved.inputs.size() != assembly.parts.size())
		return fail(error, Text::tr("Reload the assembly inputs before preparing a weapon package."));
	if (!context.archive || !context.archive->isOpen())
		return fail(error, Text::tr("Open the project asset package or folder before preparing a weapon package."));
	static const QRegularExpression id(QStringLiteral("^[A-Za-z][A-Za-z0-9_-]{0,23}$"));
	const auto directory = QStringLiteral("models/weapons2/%1/").arg(options.weaponName);
	if (!id.match(options.weaponName).hasMatch() || (directory + options.weaponName + QStringLiteral("_barrel.md3")).size() > 63 ||
		!isSafePackageVirtualPath(directory + options.weaponName + QStringLiteral(".md3")))
		return fail(error, Text::tr("Use a weapon ID of 1–24 ASCII letters, digits, underscores or hyphens, starting with a letter; its "
									"paths must fit 63 characters."));
	struct Role
	{
		QString role, part, suffix;
	};
	QVector<Role> roles{{QStringLiteral("weapon"), options.weaponPart, QString()}};
	if (!options.handPart.isEmpty())
		roles << Role{QStringLiteral("hand"), options.handPart, QStringLiteral("_hand")};
	if (!options.barrelPart.isEmpty())
		roles << Role{QStringLiteral("barrel"), options.barrelPart, QStringLiteral("_barrel")};
	if (!options.flashPart.isEmpty())
		roles << Role{QStringLiteral("flash"), options.flashPart, QStringLiteral("_flash")};
	QSet<QString> used;
	for (const auto &role : roles)
	{
		if (!partNamed(assembly, role.part) || used.contains(role.part))
			return fail(error, Text::tr("Choose distinct existing parts for the weapon roles; %1 is missing or repeated.").arg(role.part));
		used.insert(role.part);
	}
	const auto *weapon = partNamed(assembly, options.weaponPart);
	const auto *weaponInput = inputNamed(resolved, options.weaponPart);
	if (!weapon || !weaponInput)
		return fail(error, Text::tr("Choose the weapon part."));
	// CG_AddPlayerWeapon draws the gun on tag_weapon: the hand's in first
	// person, the player torso's in third person.
	if (!weapon->parent.isEmpty() && weapon->tag != QStringLiteral("tag_weapon"))
		return fail(error, Text::tr("Put the weapon at the root, or on a tag_weapon (the hand's or a player's), as the game holds it."));
	if (!options.handPart.isEmpty() && (weapon->parent != options.handPart || !partNamed(assembly, options.handPart)->parent.isEmpty()))
		return fail(error, Text::tr("The hand must be the root, holding the weapon on its tag_weapon."));
	if (!options.barrelPart.isEmpty() &&
		(partNamed(assembly, options.barrelPart)->parent != weapon->id || partNamed(assembly, options.barrelPart)->tag != QStringLiteral("tag_barrel")))
		return fail(error, Text::tr("The barrel must sit on the weapon's tag_barrel."));
	if (!options.flashPart.isEmpty() &&
		(partNamed(assembly, options.flashPart)->parent != weapon->id || partNamed(assembly, options.flashPart)->tag != QStringLiteral("tag_flash")))
		return fail(error, Text::tr("The flash must sit on the weapon's tag_flash."));
	// The muzzle flash, its light and many weapon effects start at tag_flash.
	if (!hasTag(weaponInput->mesh, QStringLiteral("tag_flash")))
		return fail(error, Text::tr("Weapon part %1 has no tag_flash, so the game would draw the muzzle flash and effects at the weapon's "
									"origin. Add a tag_flash at the muzzle.")
							   .arg(weapon->id));
	if (!options.barrelPart.isEmpty() && !hasTag(weaponInput->mesh, QStringLiteral("tag_barrel")))
		return fail(error, Text::tr("Weapon part %1 has no tag_barrel for its barrel model.").arg(weapon->id));
	if (!options.handPart.isEmpty() && !hasTag(inputNamed(resolved, options.handPart)->mesh, QStringLiteral("tag_weapon")))
		return fail(error, Text::tr("Hand part %1 has no tag_weapon, so the game could not hold the weapon in first person.").arg(options.handPart));
	if (!options.flashPart.isEmpty() && inputNamed(resolved, options.flashPart)->mesh.frames.size() != 1)
		return fail(error, Text::tr("The flash model must have one pose; the game draws it whole for a moment after each shot."));

	ModelWeaponBundle candidate;
	candidate.options = options;
	auto &package = candidate.package;
	package.recipeSha256 = resolved.recipeSha256;
	package.sourceArchive = context.archive;
	protectModelBundleInput(&package, sourcePath);
	ModelMesh materials;
	materials.sourcePath = directory;
	QJsonArray parts;
	QStringList notes;
	for (const auto &role : roles)
	{
		const auto *part = partNamed(assembly, role.part);
		const auto *input = inputNamed(resolved, role.part);
		if (input->sourceKind == ModelAssemblySource::File)
			protectModelBundleInput(&package, input->source);
		if (input->skin && input->skin->sourceKind == ModelAssemblySource::File)
		{
			protectModelBundleInput(&package, input->skin->source);
			protectModelBundleInput(&package, input->skin->resolvedPath);
		}
		auto mesh = input->mesh;
		// A weapon placed on another model (a player's tag_weapon) keeps only its
		// own transform: the game places it on that tag itself.
		const double weaponBase = options.handPart.isEmpty() ? 1.0 : inheritedScale(assembly, *weapon);
		const double scale = role.role == QStringLiteral("hand")	? 1.0
							 : role.role == QStringLiteral("weapon") ? weaponBase
																	 : weaponBase * weapon->scale;
		if (!transformModelBundlePart(&mesh, *part, scale, error, control))
			return false;
		ModelExportReport report;
		const auto md3 = exportEditableModel(mesh, QStringLiteral("md3"), 0, error, control, &report);
		if (md3.isEmpty())
			return fail(error, Text::tr("The %1 model: %2").arg(role.role, error ? *error : QString()));
		if (!addModelBundleFile(&package, directory + options.weaponName + role.suffix + QStringLiteral(".md3"), role.role + "-model", md3, error))
			return false;
		materials.surfaces += mesh.surfaces;
		if (part->spinRpm != 0)
			notes << Text::tr("Part %1 spins in the preview only; the game rolls a barrel itself while firing.").arg(part->id);
		if (role.role == QStringLiteral("hand") && mesh.frames.size() > 1)
			notes << Text::tr("The hand keeps its %n frame(s); the game maps the player's torso animation onto them "
							  "(CG_MapTorsoToWeaponFrame).",
							  nullptr, int(mesh.frames.size()));
		else if (role.role != QStringLiteral("hand") && mesh.frames.size() > 1)
			notes << Text::tr("The %1 model keeps %n frame(s), but the game draws its first frame only.", nullptr, int(mesh.frames.size()))
						 .arg(role.role);
		notes += report.notes;
		QJsonObject receipt{{"part", part->id},
							{"role", role.role},
							{"sourceSha256", hex(input->sha256)},
							{"sourceBytes", input->bytes},
							{"frames", mesh.frames.size()},
							{"surfaces", mesh.surfaces.size()},
							{"tags", mesh.tags.size()}};
		if (input->skin)
			receipt.insert("skinSha256", hex(input->skin->sha256));
		parts.append(receipt);
	}
	if (!captureModelBundleDependencies(materials, context, &package, error, control))
		return false;
	if (options.flashPart.isEmpty())
		notes << Text::tr("No flash model: the game shows the muzzle flash light only.");
	if (options.handPart.isEmpty())
		notes << Text::tr("No hand model: first-person view shows the weapon without a hand.");
	if (!hasTag(weaponInput->mesh, QStringLiteral("tag_barrel")) || options.barrelPart.isEmpty())
		notes << Text::tr("No barrel model: nothing spins while firing.");
	notes.prepend(Text::tr("Publishes the weapon's native models and their material dependencies at %1. Local transforms and inherited "
						   "scale are baked into each model. Register the weapon in the game code that loads this path.")
					  .arg(directory));
	notes << Text::tr("Collision volumes and editable sources stay in the project. Firing, effects and gameplay still need testing in "
					  "the target game.");
	notes.removeDuplicates();
	package.notes = notes;
	package.protectedPaths.removeDuplicates();
	QJsonArray files;
	for (const auto &file : package.files)
		files.append(QJsonObject{{"path", file.path}, {"role", file.role}, {"bytes", file.bytes.size()}, {"sha256", hex(file.sha256)}});
	// Content identities and virtual paths only: no machine paths or times.
	package.receipt = {{"schemaVersion", 1},
					   {"format", "quake3-weapon-bundle"},
					   {"weapon", options.weaponName},
					   {"parts", parts},
					   {"files", files}};
	if (!addModelBundleFile(&package, QStringLiteral("vibestudio/weapon-%1.json").arg(options.weaponName), "receipt",
							QJsonDocument(package.receipt).toJson(), error) ||
		!modelWorkCheckpoint(control, ModelWorkPhase::Validating, package.totalBytes, package.totalBytes, error))
		return false;
	std::sort(package.files.begin(), package.files.end(), [](const auto &a, const auto &b) { return a.path < b.path; });
	*result = std::move(candidate);
	return true;
}

QJsonObject modelWeaponBundleJson(const ModelWeaponBundle &bundle)
{
	const auto &package = bundle.package;
	QJsonArray files;
	for (const auto &file : package.files)
		files.append(QJsonObject{{"path", file.path}, {"role", file.role}, {"bytes", file.bytes.size()}, {"sha256", hex(file.sha256)}});
	return {{"weapon", bundle.options.weaponName},
			{"weaponPart", bundle.options.weaponPart},
			{"handPart", bundle.options.handPart},
			{"barrelPart", bundle.options.barrelPart},
			{"flashPart", bundle.options.flashPart},
			{"bytes", package.totalBytes},
			{"files", files},
			{"receipt", package.receipt},
			{"recipeSha256", hex(package.recipeSha256)},
			{"dependencies", levelDependencyReportJson(package.dependencies)},
			{"notes", QJsonArray::fromStringList(package.notes)}};
}

QStringList modelWeaponBundleText(const ModelWeaponBundle &bundle)
{
	const auto &package = bundle.package;
	const int count = int(package.files.size());
	QStringList lines{Text::tr("Weapon %1: %n file(s), %2 bytes.", nullptr, count).arg(bundle.options.weaponName).arg(package.totalBytes)};
	for (const auto &file : package.files)
		lines << QStringLiteral("%1 · %2 bytes · %3").arg(file.path).arg(file.bytes.size()).arg(hex(file.sha256));
	return lines + package.notes;
}

PackageWriteReport writeModelWeaponBundle(const ModelWeaponBundle &bundle, const QString &path, bool overwrite, bool dryRun,
										  const ModelWorkControl &control)
{
	return writeModelPlayerBundle(bundle.package, path, overwrite, dryRun, control);
}
} // namespace vibestudio
