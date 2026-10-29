#include "core/model_weapon_bundle.h"

#include "core/model_lod.h"
#include "core/model_tags.h"
#include "core/model_targets.h"

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
	// The longest name: the barrel, or its last detail level (_barrel_N.md3).
	const auto longest = directory + options.weaponName +
						 (options.detailLevels == ModelPlayerDetailLevels::None ? QStringLiteral("_barrel.md3") : QStringLiteral("_barrel_2.md3"));
	if (!id.match(options.weaponName).hasMatch() || longest.size() > 63 ||
		!isSafePackageVirtualPath(directory + options.weaponName + QStringLiteral(".md3")))
		return fail(error, Text::tr("Use a weapon ID of 1–24 ASCII letters, digits, underscores or hyphens, starting with a letter; its "
									"paths must fit 63 characters."));
	ModelTargetProfile quake3;
	findModelTargetProfile(QStringLiteral("md3"), QStringLiteral("quake3"), &quake3);
	if (options.detailLevels == ModelPlayerDetailLevels::Generated &&
		(options.lodLevels < 1 || options.lodLevels > std::max(1, quake3.limits.detailLevels) || !(options.lodRatio >= .05 && options.lodRatio <= .95)))
		return fail(error, Text::tr("Quake III loads up to %n detail level(s) per model; choose that many or fewer, each keeping 0.05 to 0.95 of "
									"the level above.",
									nullptr, quake3.limits.detailLevels));
	static const QRegularExpression soundName(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]{0,31}$"));
	if (options.sounds.size() > modelWeaponSoundLimit)
		return fail(error, Text::tr("A weapon package carries at most %1 sounds.").arg(modelWeaponSoundLimit));
	QSet<QString> soundNames;
	for (const auto &sound : options.sounds)
	{
		const auto path = QStringLiteral("sound/weapons/%1/%2.wav").arg(options.weaponName, sound.name);
		if (!soundName.match(sound.name).hasMatch() || path.size() > 63 || !isSafePackageVirtualPath(path))
			return fail(error, Text::tr("Name weapon sounds with 1–32 ASCII letters, digits, underscores or hyphens; %1 does not fit.")
								   .arg(sound.name));
		if (soundNames.contains(sound.name.toLower()))
			return fail(error, Text::tr("Weapon sound %1 is listed twice.").arg(sound.name));
		soundNames.insert(sound.name.toLower());
	}
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
	QJsonArray parts, levelReceipts;
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
		// Detail levels are made in the part's own space and then placed like
		// the part, so they keep the baked transform and the base model's tags.
		QVector<ModelLodLevel> levels;
		if (options.detailLevels == ModelPlayerDetailLevels::Generated)
		{
			if (!buildModelLods(input->mesh, options.lodLevels, options.lodRatio, &levels, error, control))
				return fail(error, Text::tr("Detail levels of the %1 model: %2").arg(role.role, error ? *error : QString()));
		}
		else if (options.detailLevels == ModelPlayerDetailLevels::Stored && !input->mesh.detailLevels.isEmpty())
		{
			const bool handMade =
				std::any_of(input->mesh.detailLevels.cbegin(), input->mesh.detailLevels.cend(), [](const auto &level) { return !level.generated; });
			if (handMade && input->sourceKind != ModelAssemblySource::File)
				return fail(error, Text::tr("Part %1 is read from the package, so its hand-made detail levels cannot be found. Use a source file "
											"or generated levels.")
									   .arg(part->id));
			QString recipeError;
			if (!validateModelLodRecipe(input->mesh.detailLevels, quake3.limits.detailLevels, &recipeError))
				return fail(error, Text::tr("Detail levels of the %1 model: %2").arg(role.role, recipeError));
			const auto baseDirectory = input->sourceKind == ModelAssemblySource::File ? QFileInfo(input->source).absolutePath() : QString();
			for (const auto &level : input->mesh.detailLevels)
				if (!level.generated)
					protectModelBundleInput(&package, modelLodRecipeSourcePath(level, baseDirectory));
			if (!buildModelLodsFromRecipe(input->mesh, input->mesh.detailLevels, baseDirectory, &levels, error, control))
				return fail(error, Text::tr("Detail levels of the %1 model: %2").arg(role.role, error ? *error : QString()));
		}
		else if (options.detailLevels == ModelPlayerDetailLevels::Stored)
			notes << Text::tr("No detail levels are stored for the %1 model, so it is drawn at full detail at every distance.").arg(role.role);
		QVector<ModelMesh> levelMeshes;
		for (auto &level : levels)
		{
			if (!transformModelBundlePart(&level.mesh, *part, scale, error, control))
				return false;
			levelMeshes << level.mesh;
		}
		if (!levels.isEmpty())
		{
			ModelLodValidation validation;
			if (!validateModelLodSet(mesh, levelMeshes, quake3, &validation, error, control))
				return false;
			if (!validation.ok())
				return fail(error, Text::tr("The detail levels of the %1 model would not load correctly:\n%2")
									   .arg(role.role, modelLodValidationLines(validation).join(QLatin1Char('\n'))));
			for (int level = 1; level <= levels.size(); ++level)
			{
				ModelExportReport levelReport;
				const auto bytes = exportEditableModel(levelMeshes[level - 1], QStringLiteral("md3"), 0, error, control, &levelReport);
				if (bytes.isEmpty() || !addModelBundleFile(&package, directory + options.weaponName + role.suffix + QStringLiteral("_%1.md3").arg(level),
														   role.role + "-detail", bytes, error))
					return false;
				levelReceipts.append(QJsonObject{{"role", role.role},
												 {"level", level},
												 {"triangles", levels[level - 1].triangles},
												 {"handMade", !levels[level - 1].source.isEmpty()}});
				notes += levels[level - 1].notes;
			}
		}
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
	QJsonArray soundReceipts;
	for (const auto &sound : options.sounds)
	{
		QByteArray bytes;
		QJsonObject receipt;
		QString note;
		if (!modelBundleSoundBytes(context, sound, &package, &bytes, &receipt, &note, error, control) ||
			!addModelBundleFile(&package, QStringLiteral("sound/weapons/%1/%2.wav").arg(options.weaponName, sound.name), "sound", bytes, error))
			return false;
		if (!note.isEmpty())
			notes << note;
		soundReceipts.append(receipt);
	}
	if (!options.sounds.isEmpty())
		notes << Text::tr("%n weapon sound(s) at sound/weapons/%1/. The game code that registers the weapon assigns its flash, firing, "
						  "ready and missile sounds (CG_RegisterWeapon); point it at these files.",
						  nullptr, int(options.sounds.size()))
					 .arg(options.weaponName);
	if (!levelReceipts.isEmpty())
		notes << Text::tr("Detail levels beside each model load with it; the renderer switches by distance, r_lodscale and r_lodbias.");
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
	// Schema 2 adds detail levels and sounds; a package without them stays 1.
	if (!levelReceipts.isEmpty() || !soundReceipts.isEmpty())
	{
		package.receipt.insert("schemaVersion", 2);
		package.receipt.insert("detailLevels", levelReceipts);
		package.receipt.insert("sounds", soundReceipts);
	}
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
	QJsonArray sounds;
	for (const auto &sound : bundle.options.sounds)
		sounds.append(QJsonObject{{"name", sound.name}, {"source", modelPlayerReferenceText(sound.source)}});
	const auto detail = bundle.options.detailLevels == ModelPlayerDetailLevels::Generated ? QStringLiteral("generated")
						: bundle.options.detailLevels == ModelPlayerDetailLevels::Stored  ? QStringLiteral("stored")
																						  : QStringLiteral("none");
	return {{"weapon", bundle.options.weaponName},
			{"detailLevels", detail},
			{"sounds", sounds},
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
