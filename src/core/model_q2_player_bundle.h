#pragma once

#include "core/model_player_bundle.h"

namespace vibestudio
{
// Quake II player packages. The client loads players/<model>/tris.md2 and
// weapon.md2 (the weapon plays in frame lock with the body), <skin>.pcx with
// its <skin>_i.pcx icon, optional sexed sounds (players/<model>/<name>.wav)
// and optional view weapons w_<weapon>.md2, also frame-locked.

// The standard player animation ranges of game/m_player.h (Quake II GPL
// source): 198 frames from stand01 to death308.
struct ModelQ2PlayerRange
{
	const char *name;
	int first, count;
};
const QVector<ModelQ2PlayerRange> &modelQ2PlayerFrameRanges();
inline constexpr int modelQ2PlayerFrameCount = 198;
// The sexed sounds the game plays for a player ("*death1.wav" and so on).
const QStringList &modelQ2PlayerSoundNames();

// Every input below is a file, relative to the directory given to
// prepareModelQ2PlayerBundle, or "package:PATH", one entry of the open package
// (the one holding the palette), found as linked skins are.
struct ModelQ2PlayerSkin
{
	QString name;  // 1-24 ASCII letters, digits, underscores or hyphens
	QString image; // the skin, converted to an 8-bit PCX in the Quake II palette
	QString icon;  // the <skin>_i.pcx icon image; empty reuses a small copy of the skin
};
struct ModelQ2PlayerExtra
{
	QString name;	// a sound name, or a view weapon name (w_<name>.md2)
	QString source; // a file or package:PATH
};
struct ModelQ2PlayerOptions
{
	QString modelName;
	// Editable sources or native models (MD2, MD3, OBJ...).
	QString bodySource, weaponSource;
	// Optional image for players/<model>/weapon.pcx, the weapon model's skin.
	QString weaponSkin;
	QVector<ModelQ2PlayerSkin> skins;
	QVector<ModelQ2PlayerExtra> sounds, viewWeapons;
};
struct ModelQ2PlayerBundle
{
	ModelQ2PlayerOptions options;
	ModelPlayerBundle package;
};
// Validates the frame layout (body 198 frames; weapon and view weapons the
// same count), MD2 limits (one surface), skins (the body's MD2 skin size), the
// Quake II palette (pics/colormap.pcx in `archive`) and sounds (Quake II
// audio delivery), then captures every file. Outputs never replace inputs.
bool prepareModelQ2PlayerBundle(const ModelQ2PlayerOptions &options, const QString &directory,
								const std::shared_ptr<const PackageArchiveReader> &archive, ModelQ2PlayerBundle *result,
								QString *error = nullptr, const ModelWorkControl &control = {});
QJsonObject modelQ2PlayerBundleJson(const ModelQ2PlayerBundle &bundle);
QStringList modelQ2PlayerBundleText(const ModelQ2PlayerBundle &bundle);
// .pak, or .pk2/.zip for ports that read zip packages.
PackageWriteReport writeModelQ2PlayerBundle(const ModelQ2PlayerBundle &bundle, const QString &path, bool overwrite, bool dryRun,
											const ModelWorkControl &control = {});
} // namespace vibestudio
