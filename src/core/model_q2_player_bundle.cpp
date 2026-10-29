#include "core/model_q2_player_bundle.h"

#include "core/audio_delivery.h"
#include "core/model_archive.h"
#include "core/texture_export.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

// Player layout: id Software Quake II game/m_player.h (frame ranges),
// game/p_client.c and game/p_view.c (sexed "*name.wav" sounds), client
// cl_parse.c CL_LoadClientinfo (players/<model>/tris.md2, weapon.md2,
// <skin>.pcx, <skin>_i.pcx and view weapons). GPL-2.0-or-later reference,
// reviewed 2026-10-28; original implementation, no code copied.
// https://github.com/id-Software/Quake-2/blob/master/game/m_player.h
namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelQ2Player)
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
QString absolute(const QString &directory, const QString &path)
{
	return QDir::cleanPath(QDir(directory).absoluteFilePath(path));
}
bool loadModel(const QString &path, ModelMesh *mesh, QByteArray *sha256, QString *error, const ModelWorkControl &control)
{
	QByteArray bytes;
	if (!readModelFile(path, &bytes, error, control) || !importEditableModel(path, bytes, mesh, error, nullptr, control))
		return fail(error, Text::tr("%1 cannot be read as a model: %2").arg(path, error ? *error : QString()));
	*sha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
	return true;
}
bool readImage(const QString &path, QImage *image, QString *error, const ModelWorkControl &control)
{
	QByteArray bytes;
	if (!readModelFile(path, &bytes, error, control))
		return false;
	const auto decoded = decodeIdTechImage(path, bytes, {});
	if (!decoded.decoded || decoded.image.isNull() || !decoded.frames.isEmpty() || decoded.image.width() > 640 ||
		decoded.image.height() > 480)
		return fail(error, Text::tr("%1 must be a single image up to 640 × 480 pixels. %2").arg(path, decoded.error));
	*image = decoded.image;
	return true;
}
} // namespace

const QVector<ModelQ2PlayerRange> &modelQ2PlayerFrameRanges()
{
	// FRAME_stand01 .. FRAME_death308 in game/m_player.h.
	static const QVector<ModelQ2PlayerRange> ranges{
		{"stand", 0, 40},	  {"run", 40, 6},	   {"attack", 46, 8},	  {"pain1", 54, 4},	   {"pain2", 58, 4},	{"pain3", 62, 4},
		{"jump", 66, 6},	  {"flip", 72, 12},	   {"salute", 84, 11},	  {"taunt", 95, 17},   {"wave", 112, 11},	{"point", 123, 12},
		{"crstand", 135, 19}, {"crwalk", 154, 6},  {"crattack", 160, 9},  {"crpain", 169, 4},  {"crdeath", 173, 5}, {"death1", 178, 6},
		{"death2", 184, 6},	  {"death3", 190, 8}};
	return ranges;
}

const QStringList &modelQ2PlayerSoundNames()
{
	static const QStringList names{QStringLiteral("death1"),	QStringLiteral("death2"),	 QStringLiteral("death3"),
								   QStringLiteral("death4"),	QStringLiteral("fall1"),	 QStringLiteral("fall2"),
								   QStringLiteral("gurp1"),		QStringLiteral("gurp2"),	 QStringLiteral("jump1"),
								   QStringLiteral("pain25_1"),	QStringLiteral("pain25_2"),	 QStringLiteral("pain50_1"),
								   QStringLiteral("pain50_2"),	QStringLiteral("pain75_1"),	 QStringLiteral("pain75_2"),
								   QStringLiteral("pain100_1"), QStringLiteral("pain100_2"), QStringLiteral("drown1")};
	return names;
}

bool prepareModelQ2PlayerBundle(const ModelQ2PlayerOptions &options, const QString &directory,
								const std::shared_ptr<const PackageArchiveReader> &archive, ModelQ2PlayerBundle *result, QString *error,
								const ModelWorkControl &control)
{
	if (error)
		error->clear();
	if (!result || !modelWorkCheckpoint(control, ModelWorkPhase::Validating, 0, 0, error))
		return false;
	static const QRegularExpression id(QStringLiteral("^[A-Za-z][A-Za-z0-9_-]{0,23}$"));
	const auto folder = QStringLiteral("players/%1/").arg(options.modelName);
	if (!id.match(options.modelName).hasMatch() || options.skins.isEmpty() || options.skins.size() > 16 ||
		std::any_of(options.skins.cbegin(), options.skins.cend(), [&](const auto &skin) { return !id.match(skin.name).hasMatch(); }))
		return fail(error, Text::tr("Use a player ID and 1–16 skin IDs of 1–24 ASCII letters, digits, underscores or hyphens, each "
									"starting with a letter."));
	if (options.bodySource.isEmpty() || options.weaponSource.isEmpty())
		return fail(error, Text::tr("Choose the body model (tris.md2) and the weapon model (weapon.md2)."));
	if (!archive || !archive->isOpen())
		return fail(error, Text::tr("Open the Quake II game or project package that holds pics/colormap.pcx; the skins need its palette."));
	const auto palette = resolveIdTechPalette(*archive, QStringLiteral("quake2"));
	if (!palette.fromPackage)
		return fail(error, Text::tr("The open package has no Quake II palette (pics/colormap.pcx). Open the game's pak0.pak or a package "
									"holding it; a generated palette would show wrong colours."));
	ModelQ2PlayerBundle candidate;
	candidate.options = options;
	auto &package = candidate.package;
	package.sourceArchive = archive;
	QJsonArray models;
	QStringList notes;
	// Body: one surface, the standard 198 frames.
	ModelMesh body;
	QByteArray bodyHash;
	const auto bodyPath = absolute(directory, options.bodySource);
	protectModelBundleInput(&package, bodyPath);
	if (!loadModel(bodyPath, &body, &bodyHash, error, control))
		return false;
	if (body.surfaces.size() != 1)
		return fail(error, Text::tr("A Quake II player model has one surface; the body has %n. Join its surfaces in the Mesh Editor first.",
									nullptr, int(body.surfaces.size())));
	const int frames = int(body.frames.size());
	if (frames != modelQ2PlayerFrameCount)
	{
		QStringList ranges;
		for (const auto &range : modelQ2PlayerFrameRanges())
			ranges << QStringLiteral("%1 %2–%3").arg(QString::fromLatin1(range.name)).arg(range.first).arg(range.first + range.count - 1);
		return fail(error, Text::tr("The body has %n frame(s); a Quake II player has 198, in the ranges of m_player.h: %1.", nullptr, frames)
							   .arg(ranges.join(QStringLiteral(", "))));
	}
	// Skins: one size, the MD2 header's.
	QSize skinSize;
	QVector<QImage> skinImages;
	for (const auto &skin : options.skins)
	{
		QImage image;
		const auto path = absolute(directory, skin.image);
		protectModelBundleInput(&package, path);
		if (!readImage(path, &image, error, control))
			return false;
		if (!skinSize.isEmpty() && image.size() != skinSize)
			return fail(error, Text::tr("Skin %1 is %2 × %3 pixels, but the first skin is %4 × %5. Every skin of a model shares one size.")
								   .arg(skin.name)
								   .arg(image.width())
								   .arg(image.height())
								   .arg(skinSize.width())
								   .arg(skinSize.height()));
		skinSize = image.size();
		skinImages << image;
	}
	QStringList skinPaths;
	for (const auto &skin : options.skins)
		skinPaths << folder + skin.name + QStringLiteral(".pcx");
	body.md2SkinSize = skinSize;
	body.surfaces[0].skinPaths = skinPaths;
	TextureExportOptions pcx;
	pcx.format = TextureExportFormat::Pcx;
	pcx.dither = false;
	const auto encode = [&](const QImage &image, const QString &path, const QString &role) {
		const auto encoded = encodeTextureExport(image, pcx, palette, [&](qint64 done, qint64 total) {
			return modelWorkCheckpoint(control, ModelWorkPhase::Serializing, done, total, error);
		});
		if (!encoded.succeeded)
			return fail(error, Text::tr("%1: %2").arg(path, encoded.error));
		return addModelBundleFile(&package, path, role, encoded.bytes, error);
	};
	for (int s = 0; s < options.skins.size(); ++s)
	{
		const auto &skin = options.skins[s];
		if (!encode(skinImages[s], skinPaths[s], QStringLiteral("skin")))
			return false;
		QImage icon;
		if (!skin.icon.isEmpty())
		{
			const auto path = absolute(directory, skin.icon);
			protectModelBundleInput(&package, path);
			if (!readImage(path, &icon, error, control))
				return false;
		}
		else
		{
			icon = skinImages[s].scaled(32, 32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
			notes << Text::tr("Skin %1 has no icon image; its icon is the skin scaled to 32 × 32 pixels.").arg(skin.name);
		}
		if (icon.size() != QSize(32, 32))
			notes << Text::tr("The %1 icon is %2 × %3 pixels; Quake II's own icons are 32 × 32.").arg(skin.name).arg(icon.width()).arg(icon.height());
		if (!encode(icon, folder + skin.name + QStringLiteral("_i.pcx"), QStringLiteral("icon")))
			return false;
	}
	const auto md2 = [&](ModelMesh mesh, const QString &path, const QString &role, const QByteArray &sourceHash, const QString &label) {
		ModelExportReport report;
		const auto bytes = exportEditableModel(mesh, QStringLiteral("md2"), 0, error, control, &report);
		if (bytes.isEmpty())
			return fail(error, Text::tr("%1: %2").arg(label, error ? *error : QString()));
		notes += report.notes;
		models.append(QJsonObject{{"path", path},
								  {"role", role},
								  {"sourceSha256", hex(sourceHash)},
								  {"frames", mesh.frames.size()},
								  {"triangles", mesh.triangleCount},
								  {"skins", QJsonArray::fromStringList(mesh.surfaces.value(0).skinPaths)}});
		return addModelBundleFile(&package, path, role, bytes, error);
	};
	if (!md2(body, folder + QStringLiteral("tris.md2"), QStringLiteral("body"), bodyHash, Text::tr("The body")))
		return false;
	// Weapon and view weapons play in frame lock with the body.
	const auto locked = [&](const QString &source, const QString &label, ModelMesh *mesh, QByteArray *hash) {
		const auto path = absolute(directory, source);
		protectModelBundleInput(&package, path);
		if (!loadModel(path, mesh, hash, error, control))
			return false;
		if (mesh->surfaces.size() != 1)
			return fail(error, Text::tr("%1 needs one surface; join its surfaces first.").arg(label));
		const int count = int(mesh->frames.size());
		if (count != frames)
			return fail(error, Text::tr("%1 has %n frame(s), but it plays in frame lock with the body's 198.", nullptr, count).arg(label));
		return true;
	};
	ModelMesh weapon;
	QByteArray weaponHash;
	if (!locked(options.weaponSource, Text::tr("The weapon model"), &weapon, &weaponHash))
		return false;
	weapon.surfaces[0].skinPaths = {folder + QStringLiteral("weapon.pcx")};
	if (!options.weaponSkin.isEmpty())
	{
		QImage image;
		const auto path = absolute(directory, options.weaponSkin);
		protectModelBundleInput(&package, path);
		if (!readImage(path, &image, error, control) || !encode(image, folder + QStringLiteral("weapon.pcx"), QStringLiteral("weapon-skin")))
			return false;
		weapon.md2SkinSize = image.size();
	}
	else
		notes << Text::tr("No weapon skin is packaged: weapon.md2 names players/%1/weapon.pcx, which the package must supply.").arg(options.modelName);
	if (!md2(weapon, folder + QStringLiteral("weapon.md2"), QStringLiteral("weapon"), weaponHash, Text::tr("The weapon model")))
		return false;
	QSet<QString> names;
	for (const auto &view : options.viewWeapons)
	{
		static const QRegularExpression weaponId(QStringLiteral("^[a-z][a-z0-9_]{0,23}$"));
		if (!weaponId.match(view.name).hasMatch() || names.contains(view.name))
			return fail(error, Text::tr("View weapon names are unique lower-case IDs such as shotgun (for w_shotgun.md2)."));
		names.insert(view.name);
		ModelMesh mesh;
		QByteArray hash;
		if (!locked(view.source, Text::tr("View weapon %1").arg(view.name), &mesh, &hash))
			return false;
		// View weapons share the player's weapon skin.
		mesh.surfaces[0].skinPaths = {folder + QStringLiteral("weapon.pcx")};
		if (!md2(mesh, folder + QStringLiteral("w_%1.md2").arg(view.name), QStringLiteral("view-weapon"), hash, view.name))
			return false;
	}
	QJsonArray soundReceipts;
	names.clear();
	for (const auto &sound : options.sounds)
	{
		if (!modelQ2PlayerSoundNames().contains(sound.name) || names.contains(sound.name))
			return fail(error, Text::tr("Quake II plays these player sounds: %1. Each once.").arg(modelQ2PlayerSoundNames().join(QStringLiteral(", "))));
		names.insert(sound.name);
		const auto path = absolute(directory, sound.source);
		protectModelBundleInput(&package, path);
		QByteArray bytes;
		if (!readModelFile(path, &bytes, error, control))
			return false;
		const AudioWorkControl audioControl{control.cancelled};
		const auto decoded = decodeAudioClip(path, bytes, audioControl);
		if (!decoded.succeeded())
			return fail(error, Text::tr("Sound %1 cannot be read as audio: %2").arg(sound.name, decoded.error));
		AudioDeliveryOptions delivery;
		delivery.preset = AudioDeliveryPreset::Quake2;
		const auto rendered = renderAudioDelivery(decoded.clip, delivery, audioControl);
		if (!rendered.succeeded())
			return fail(error, Text::tr("Sound %1 cannot be converted for Quake II: %2").arg(sound.name, rendered.error));
		if (!addModelBundleFile(&package, folder + sound.name + QStringLiteral(".wav"), QStringLiteral("sound"), rendered.bytes, error))
			return false;
		soundReceipts.append(QJsonObject{{"name", sound.name}, {"sourceSha256", hex(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256))}});
	}
	if (options.sounds.size() < modelQ2PlayerSoundNames().size())
		notes << Text::tr("Sounds packaged: %1 of %2. The game plays the male sound for each of the others.")
					 .arg(options.sounds.size())
					 .arg(modelQ2PlayerSoundNames().size());
	notes.prepend(Text::tr("Publishes players/%1/: tris.md2 with %n skin(s) named in its header, weapon.md2, the skins and icons as 8-bit "
						   "PCX in the package's Quake II palette, sounds and view weapons. Select it as %1/<skin>.",
						   nullptr, int(options.skins.size()))
					  .arg(options.modelName));
	notes << Text::tr("Gameplay, animation timing and view weapons still need testing in the target game.");
	notes.removeDuplicates();
	package.notes = notes;
	package.protectedPaths.removeDuplicates();
	QJsonArray files;
	for (const auto &file : package.files)
		files.append(QJsonObject{{"path", file.path}, {"role", file.role}, {"bytes", file.bytes.size()}, {"sha256", hex(file.sha256)}});
	package.receipt = {{"schemaVersion", 1},	{"format", "quake2-player-bundle"}, {"model", options.modelName},
					   {"models", models},		{"sounds", soundReceipts},			{"palette", palette.sourceVirtualPath},
					   {"files", files}};
	if (!addModelBundleFile(&package, QStringLiteral("vibestudio/q2-player-%1.json").arg(options.modelName), QStringLiteral("receipt"),
							QJsonDocument(package.receipt).toJson(), error))
		return false;
	std::sort(package.files.begin(), package.files.end(), [](const auto &a, const auto &b) { return a.path < b.path; });
	*result = std::move(candidate);
	return true;
}

QJsonObject modelQ2PlayerBundleJson(const ModelQ2PlayerBundle &bundle)
{
	QJsonArray files, ranges;
	for (const auto &file : bundle.package.files)
		files.append(QJsonObject{{"path", file.path}, {"role", file.role}, {"bytes", file.bytes.size()}, {"sha256", hex(file.sha256)}});
	for (const auto &range : modelQ2PlayerFrameRanges())
		ranges.append(QJsonObject{{"name", QString::fromLatin1(range.name)}, {"first", range.first}, {"count", range.count}});
	return {{"model", bundle.options.modelName},
			{"bytes", bundle.package.totalBytes},
			{"files", files},
			{"frameRanges", ranges},
			{"receipt", bundle.package.receipt},
			{"notes", QJsonArray::fromStringList(bundle.package.notes)}};
}

QStringList modelQ2PlayerBundleText(const ModelQ2PlayerBundle &bundle)
{
	const int count = int(bundle.package.files.size());
	QStringList lines{
		Text::tr("Quake II player %1: %n file(s), %2 bytes.", nullptr, count).arg(bundle.options.modelName).arg(bundle.package.totalBytes)};
	for (const auto &file : bundle.package.files)
		lines << QStringLiteral("%1 · %2 bytes · %3").arg(file.path).arg(file.bytes.size()).arg(hex(file.sha256));
	return lines + bundle.package.notes;
}

PackageWriteReport writeModelQ2PlayerBundle(const ModelQ2PlayerBundle &bundle, const QString &path, bool overwrite, bool dryRun,
											const ModelWorkControl &control)
{
	const auto suffix = QFileInfo(path).suffix().toLower();
	const auto format = suffix == QStringLiteral("pak") ? PackageArchiveFormat::Pak : PackageArchiveFormat::Zip;
	return writeModelBundleArchive(bundle.package, path, format, overwrite, dryRun, control);
}
} // namespace vibestudio
