// Quake II player packages (ATT-Q2PLAYER-01).
#include "core/model_q2_player_bundle.h"
#include "tests/model_assembly_test_helpers.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>

#include <iostream>

using namespace vibestudio;
namespace
{
int checks = 0;
bool expect(bool value, const char *message, const QString &detail = {})
{
	++checks;
	if (!value)
		std::cerr << "FAIL: " << message << ' ' << qPrintable(detail) << '\n';
	return value;
}
bool write(const QString &path, const QByteArray &data)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
// An original 256-colour palette (not Quake II's) in a 1 x 1 PCX, as
// pics/colormap.pcx carries the game palette.
QByteArray palettePcx(QByteArray *palette)
{
	QByteArray bytes(128, '\0');
	bytes[0] = 10;
	bytes[1] = 5;
	bytes[2] = 1;
	bytes[3] = 8;
	bytes[65] = 1;
	qToLittleEndian<quint16>(2, bytes.data() + 66);
	qToLittleEndian<quint16>(1, bytes.data() + 68);
	bytes += QByteArray(2, '\0');
	bytes += char(12);
	palette->clear();
	for (int i = 0; i < 256; ++i)
	{
		palette->append(char(i));
		palette->append(char(255 - i));
		palette->append(char((i * 7) & 255));
	}
	return bytes + *palette;
}
QByteArray wav()
{
	QByteArray data(2 * 1100, '\0');
	for (int i = 0; i < 1100; ++i)
		qToLittleEndian<qint16>(qint16((i % 50) * 400 - 10000), data.data() + 2 * i);
	QByteArray bytes("RIFF");
	bytes += QByteArray(4, '\0');
	bytes += "WAVEfmt ";
	QByteArray format(16, '\0');
	qToLittleEndian<quint16>(1, format.data());
	qToLittleEndian<quint16>(1, format.data() + 2);
	qToLittleEndian<quint32>(11025, format.data() + 4);
	qToLittleEndian<quint32>(22050, format.data() + 8);
	qToLittleEndian<quint16>(2, format.data() + 12);
	qToLittleEndian<quint16>(16, format.data() + 14);
	QByteArray size(4, '\0');
	qToLittleEndian<quint32>(16, size.data());
	bytes += size + format + "data";
	qToLittleEndian<quint32>(quint32(data.size()), size.data());
	bytes += size + data;
	qToLittleEndian<quint32>(quint32(bytes.size() - 8), bytes.data() + 4);
	return bytes;
}
ModelMesh animated(int frames, int surfaces = 1)
{
	auto mesh = tests::assemblyModel();
	mesh.tags.clear();
	auto &surface = mesh.surfaces[0];
	const auto first = surface.frames[0];
	surface.frames.clear();
	mesh.frames.clear();
	for (int f = 0; f < frames; ++f)
	{
		auto frame = first;
		for (auto &p : frame.positions)
			p.z = float(f % 10);
		surface.frames << frame;
		ModelFrameInfo info;
		info.name = QStringLiteral("frame%1").arg(f);
		mesh.frames << info;
	}
	if (surfaces == 2)
	{
		auto second = surface;
		second.name = QStringLiteral("second");
		mesh.surfaces << second;
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}
QByteArray image(QColor colour, int size = 32)
{
	QImage picture(size, size, QImage::Format_ARGB32);
	picture.fill(colour);
	QByteArray png;
	QBuffer buffer(&png);
	buffer.open(QIODevice::WriteOnly);
	picture.save(&buffer, "PNG");
	return png;
}
const ModelPlayerBundleFile *fileAt(const ModelQ2PlayerBundle &bundle, const QString &path)
{
	for (const auto &file : bundle.package.files)
		if (file.path == path)
			return &file;
	return nullptr;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("q2-player-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	bool ok = true;
	QString error;
	QByteArray palette;
	ok &= expect(QDir().mkpath(path("game/pics")) && QDir().mkpath(path("empty")) && write(path("game/pics/colormap.pcx"), palettePcx(&palette)) &&
					 write(path("empty/readme.txt"), "no palette") &&
					 write(path("body.mesh.json"), serializeEditableModel(animated(198), &error)) &&
					 write(path("gun.mesh.json"), serializeEditableModel(animated(198), &error)) &&
					 write(path("shotgun.mesh.json"), serializeEditableModel(animated(198), &error)) &&
					 write(path("short.mesh.json"), serializeEditableModel(animated(197), &error)) &&
					 write(path("split.mesh.json"), serializeEditableModel(animated(198, 2), &error)) && write(path("red.png"), image(Qt::red)) &&
					 write(path("blue.png"), image(Qt::blue)) && write(path("big.png"), image(Qt::green, 64)) && write(path("death.wav"), wav()),
				 "fixtures written", error);
	// The same inputs as entries of the game package, for package:PATH.
	ok &= expect(QDir().mkpath(path("game/src")) && write(path("game/src/body.mesh.json"), read(path("body.mesh.json"))) &&
					 write(path("game/src/gun.mesh.json"), read(path("gun.mesh.json"))) &&
					 write(path("game/src/shotgun.mesh.json"), read(path("shotgun.mesh.json"))) &&
					 write(path("game/src/red.png"), read(path("red.png"))) && write(path("game/src/blue.png"), read(path("blue.png"))) &&
					 write(path("game/src/death.wav"), read(path("death.wav"))),
				 "package inputs written");
	auto archive = std::make_shared<PackageArchive>();
	auto empty = std::make_shared<PackageArchive>();
	ok &= expect(archive->load(path("game"), &error) && empty->load(path("empty"), &error), "packages open", error);
	ModelQ2PlayerOptions options;
	options.modelName = QStringLiteral("vamp");
	options.bodySource = QStringLiteral("body.mesh.json");
	options.weaponSource = QStringLiteral("gun.mesh.json");
	options.skins = {{QStringLiteral("red"), QStringLiteral("red.png"), {}}, {QStringLiteral("blue"), QStringLiteral("blue.png"), QStringLiteral("blue.png")}};
	options.sounds = {{QStringLiteral("death1"), QStringLiteral("death.wav")}};
	options.viewWeapons = {{QStringLiteral("shotgun"), QStringLiteral("shotgun.mesh.json")}};
	ModelQ2PlayerBundle bundle, again;
	ok &= expect(prepareModelQ2PlayerBundle(options, temporary.path(), archive, &bundle, &error) &&
					 prepareModelQ2PlayerBundle(options, temporary.path(), archive, &again, &error),
				 "the Quake II player reviews", error);
	if (!ok)
		return 1;
	const QString folder = QStringLiteral("players/vamp/");
	for (const auto *name : {"tris.md2", "weapon.md2", "w_shotgun.md2", "red.pcx", "red_i.pcx", "blue.pcx", "blue_i.pcx", "death1.wav"})
		ok &= expect(fileAt(bundle, folder + QString::fromLatin1(name)), "every player file is at the client's path", QString::fromLatin1(name));
	ok &= expect(fileAt(bundle, "vibestudio/q2-player-vamp.json") && bundle.package.files.size() == 9, "the receipt completes the package");
	bool same = bundle.package.files.size() == again.package.files.size();
	for (int i = 0; same && i < bundle.package.files.size(); ++i)
		same = bundle.package.files[i].sha256 == again.package.files[i].sha256;
	ok &= expect(same && !QJsonDocument(bundle.package.receipt).toJson().contains(temporary.path().toUtf8()),
				 "review is deterministic and the receipt holds no machine paths");
	// The body names its skins in the MD2 header and keeps 198 frames.
	const auto body = decodeModelMesh(QStringLiteral("tris.md2"), fileAt(bundle, folder + "tris.md2")->bytes);
	ok &= expect(body.frames.size() == 198 && body.surfaces.size() == 1 &&
					 body.surfaces[0].skinPaths == QStringList{folder + "red.pcx", folder + "blue.pcx"},
				 "tris.md2 keeps 198 frames and names players/vamp/<skin>.pcx", body.error);
	const auto weapon = decodeModelMesh(QStringLiteral("weapon.md2"), fileAt(bundle, folder + "weapon.md2")->bytes);
	ok &= expect(weapon.frames.size() == 198 && weapon.surfaces.value(0).skinPaths == QStringList{folder + "weapon.pcx"},
				 "weapon.md2 is frame-locked and names weapon.pcx");
	// Skins are 8-bit PCX carrying the package's palette.
	const auto skin = fileAt(bundle, folder + "red.pcx")->bytes;
	ok &= expect(skin.size() > 769 && skin.at(skin.size() - 769) == 12 && skin.right(768) == palette && skin.at(3) == 8,
				 "skins are 8-bit PCX in the Quake II palette from the package");
	const auto decoded = decodeIdTechImage(QStringLiteral("red.pcx"), skin, {});
	ok &= expect(decoded.decoded && decoded.image.size() == QSize(32, 32), "the PCX skin decodes at the skin size", decoded.error);
	ok &= expect(bundle.package.notes.join(' ').contains("scaled to 32"), "an icon made from the skin is noted");

	// Refusals.
	const auto refused = [&](ModelQ2PlayerOptions changed, const std::shared_ptr<PackageArchive> &package, const char *message,
							 const char *contains = nullptr) {
		ModelQ2PlayerBundle rejected;
		const bool failed = !prepareModelQ2PlayerBundle(changed, temporary.path(), package, &rejected, &error);
		ok &= expect(failed && (!contains || error.contains(QString::fromUtf8(contains))), message, error);
	};

	// Package entries as inputs: the same bytes give the same package.
	auto packaged = options;
	packaged.bodySource = QStringLiteral("package:src/body.mesh.json");
	packaged.weaponSource = QStringLiteral("package:src/gun.mesh.json");
	packaged.skins = {{QStringLiteral("red"), QStringLiteral("package:src/red.png"), {}},
					  {QStringLiteral("blue"), QStringLiteral("package:src/blue.png"), QStringLiteral("package:src/blue.png")}};
	packaged.sounds = {{QStringLiteral("death1"), QStringLiteral("package:src/death.wav")}};
	packaged.viewWeapons = {{QStringLiteral("shotgun"), QStringLiteral("package:src/shotgun.mesh.json")}};
	ModelQ2PlayerBundle fromPackage;
	ok &= expect(prepareModelQ2PlayerBundle(packaged, temporary.path(), archive, &fromPackage, &error), "package entries review as inputs",
				 error);
	bool identical = fromPackage.package.files.size() == bundle.package.files.size();
	for (int i = 0; identical && i < bundle.package.files.size(); ++i)
		identical = fromPackage.package.files[i].path == bundle.package.files[i].path &&
					fromPackage.package.files[i].sha256 == bundle.package.files[i].sha256;
	ok &= expect(identical && !fromPackage.package.protectedPaths.join(' ').contains("body.mesh.json"),
				 "package entries give the same files as the files they copy, and protect no local path");
	auto missingEntry = packaged;
	missingEntry.bodySource = QStringLiteral("package:src/missing.mesh.json");
	refused(missingEntry, archive, "a missing package entry is refused");
	auto escaping = packaged;
	escaping.skins[0].image = QStringLiteral("package:../red.png");
	refused(escaping, archive, "an unnormalized package path is refused", "package:");
	auto shortBody = options;
	shortBody.bodySource = QStringLiteral("short.mesh.json");
	refused(shortBody, archive, "a body without 198 frames is refused with the standard ranges", "stand 0–39");
	auto shortWeapon = options;
	shortWeapon.weaponSource = QStringLiteral("short.mesh.json");
	refused(shortWeapon, archive, "a weapon out of frame lock is refused", "frame lock");
	auto shortView = options;
	shortView.viewWeapons = {{QStringLiteral("shotgun"), QStringLiteral("short.mesh.json")}};
	refused(shortView, archive, "a view weapon out of frame lock is refused");
	auto split = options;
	split.bodySource = QStringLiteral("split.mesh.json");
	refused(split, archive, "a body with two surfaces is refused");
	refused(options, empty, "a package without the Quake II palette is refused", "colormap.pcx");
	auto mixed = options;
	mixed.skins[1].image = QStringLiteral("big.png");
	refused(mixed, archive, "skins of different sizes are refused");
	auto unknown = options;
	unknown.sounds = {{QStringLiteral("scream"), QStringLiteral("death.wav")}};
	refused(unknown, archive, "an unknown sound name is refused");

	// Package output: PAK or zip, never over an input.
	const auto pak = writeModelQ2PlayerBundle(bundle, path("vamp.pak"), false, false);
	const auto zip = writeModelQ2PlayerBundle(bundle, path("vamp.pk2"), false, false);
	ok &= expect(pak.succeeded() && zip.succeeded() && pak.determinismVerified && read(path("vamp.pak")).startsWith("PACK"),
				 "the package writes as PAK and as zip");
	PackageArchive reopened;
	ok &= expect(reopened.load(path("vamp.pak"), &error) && reopened.entries().size() >= 9, "the PAK reopens with every file", error);
	ok &= expect(!writeModelQ2PlayerBundle(bundle, path("body.mesh.json"), true, false).succeeded() &&
					 !writeModelQ2PlayerBundle(bundle, path("vamp.pk3"), false, false).succeeded(),
				 "outputs never replace inputs, and need a Quake II package extension");

	if (argc > 1)
	{
		QJsonObject output;
		const auto run = [&](QStringList args, int code = 0) {
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--settings-file", path("settings.ini")} + args + QStringList{"--json"});
			if (!process.waitForFinished(120000))
			{
				process.kill();
				process.waitForFinished();
				return false;
			}
			output = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
			if (process.exitCode() != code)
				std::cerr << qPrintable(QString::fromUtf8(process.readAllStandardError())) << '\n';
			return process.exitStatus() == QProcess::NormalExit && process.exitCode() == code;
		};
		const QStringList review{"model", "assembly", "--operation", "q2-player-review", "--player-name", "vamp", "--body", "body.mesh.json",
								 "--weapon", "gun.mesh.json", "--q2-skin", "red=red.png", "--q2-skin", "blue=blue.png,blue.png",
								 "--sound", "death1=death.wav", "--view-weapon", "shotgun=shotgun.mesh.json", "--package", "game"};
		ok &= expect(run(review) && output.value("files").toArray().size() == 9 && output.value("frameRanges").toArray().size() == 20,
					 "CLI reviews the Quake II player and lists the standard ranges");
		auto exportArgs = review;
		exportArgs[3] = QStringLiteral("q2-player-export");
		ok &= expect(run(exportArgs + QStringList{"--output", "cli-vamp.pak"}) && output.value("written").toBool() &&
						 QFileInfo::exists(path("cli-vamp.pak")),
					 "CLI exports the PAK");
		ok &= expect(run({"model", "assembly", "--operation", "q2-player-review", "--player-name", "vamp", "--body",
						  "package:src/body.mesh.json", "--weapon", "package:src/gun.mesh.json", "--q2-skin",
						  "red=package:src/red.png", "--sound", "death1=package:src/death.wav", "--package", "game"}) &&
						 output.value("files").toArray().size() == 6,
					 "CLI takes package entries as Quake II player inputs");
		ok &= expect(run(exportArgs + QStringList{"--output", "cli-vamp.pk3"}, 2) &&
						 run({"model", "assembly", "--operation", "q2-player-review", "--player-name", "vamp", "--package", "game"}, 2),
					 "CLI needs a Quake II package extension and the models");
	}
	std::cout << checks << " Quake II player checks\n";
	return ok ? 0 : 1;
}
