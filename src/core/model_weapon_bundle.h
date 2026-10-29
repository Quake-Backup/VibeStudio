#pragma once

#include "core/model_player_bundle.h"

namespace vibestudio
{
// Quake III weapon model sets, from an assembly. cgame's CG_RegisterWeapon
// (code/cgame/cg_weapons.c) loads models/weapons2/<w>/<w>.md3 and, beside it,
// <w>_flash.md3 (drawn on the weapon's tag_flash), <w>_barrel.md3 (on
// tag_barrel, rolled for spinning weapons) and <w>_hand.md3 (the first-person
// hand, whose tag_weapon holds the gun). CG_AddPlayerWeapon draws the weapon on
// the player's tag_weapon.
struct ModelWeaponBundleOptions
{
	// 1-24 ASCII letters, digits, underscores or hyphens, starting with a letter.
	QString weaponName;
	// Assembly part IDs. The weapon is required; the others are optional.
	QString weaponPart, handPart, barrelPart, flashPart;
};
struct ModelWeaponBundle
{
	ModelWeaponBundleOptions options;
	// The files, receipt, notes, dependency report and protected inputs, in the
	// shape every native bundle uses (core/model_player_bundle.h).
	ModelPlayerBundle package;
};
// The weapon must be a root or sit on a tag_weapon (the hand's, or a player
// torso's for preview); the barrel and flash sit on the weapon's tag_barrel and
// tag_flash. The weapon needs tag_flash, and tag_barrel with a barrel; the hand
// needs tag_weapon; the flash has one pose. Each role's local transform and
// inherited scale are baked into its MD3, as for player packages. Publishes
// the role models, the material dependency closure and a receipt,
// vibestudio/weapon-<w>.json. Preview spin is not stored in MD3.
bool prepareModelWeaponBundle(const ModelAssembly &assembly, const ModelAssemblyResolved &resolved, const ModelAssemblyContext &context,
							  const ModelWeaponBundleOptions &options, const QString &sourcePath, ModelWeaponBundle *result,
							  QString *error = nullptr, const ModelWorkControl &control = {});
QJsonObject modelWeaponBundleJson(const ModelWeaponBundle &bundle);
QStringList modelWeaponBundleText(const ModelWeaponBundle &bundle);
// The player package writer: atomic, guarded, deterministic PK3.
PackageWriteReport writeModelWeaponBundle(const ModelWeaponBundle &bundle, const QString &path, bool overwrite, bool dryRun,
										  const ModelWorkControl &control = {});
} // namespace vibestudio
