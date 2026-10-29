// Quake III weapon model sets with review and package output (ATT-WEAPON-01).
#include "core/model_weapon_bundle.h"
#include "core/model_tags.h"
#include "core/texture_export.h"
#include "tests/model_assembly_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
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
ModelMesh part(const QStringList &tags, int frames = 1)
{
	auto mesh = tests::assemblyModel();
	mesh.surfaces[0].skinPaths = {QStringLiteral("textures/weapon/gun.tga")};
	mesh.frames.resize(frames);
	mesh.surfaces[0].frames.resize(frames);
	mesh.tags.clear();
	for (int f = 0; f < frames; ++f)
		for (int t = 0; t < tags.size(); ++t)
		{
			ModelTag tag;
			tag.name = tags[t];
			tag.frameIndex = f;
			tag.origin = {float(8 + 4 * t), 0, float(2 * t)};
			mesh.tags << tag;
		}
	updateEditableModelMetadata(&mesh);
	return mesh;
}
const ModelPlayerBundleFile *fileAt(const ModelWeaponBundle &bundle, const QString &path)
{
	for (const auto &file : bundle.package.files)
		if (file.path == path)
			return &file;
	return nullptr;
}
bool closeTo(ModelVec3 a, ModelVec3 b, double tolerance)
{
	return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.z - b.z) <= tolerance;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("weapon-bundle-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	bool ok = true;
	QString error;
	QImage image(16, 16, QImage::Format_ARGB32);
	image.fill(QColor(200, 120, 40));
	TextureExportOptions tga;
	tga.format = TextureExportFormat::Targa;
	ok &= expect(QDir().mkpath(path("assets/textures/weapon")) &&
					 write(path("assets/textures/weapon/gun.tga"), encodeTextureExport(image, tga).bytes) &&
					 write(path("hand.mesh.json"), serializeEditableModel(part({"tag_weapon"}, 2), &error)) &&
					 write(path("gun.mesh.json"), serializeEditableModel(part({"tag_flash", "tag_barrel"}), &error)) &&
					 write(path("bare.mesh.json"), serializeEditableModel(part({}), &error)) &&
					 write(path("flash2.mesh.json"), serializeEditableModel(part({}, 2), &error)),
				 "fixtures written", error);
	auto archive = std::make_shared<PackageArchive>();
	ok &= expect(archive->load(path("assets"), &error), "asset folder opens", error);
	const ModelAssemblyContext context{temporary.path(), archive, {}};
	ModelAssembly recipe;
	recipe.name = QStringLiteral("Rail");
	const auto add = [&](const char *id, const char *source, const char *parent, const char *tag) {
		ModelAssemblyPart p;
		p.id = QString::fromLatin1(id);
		p.source = QString::fromLatin1(source);
		p.parent = QString::fromLatin1(parent);
		p.tag = QString::fromLatin1(tag);
		p.framesPerSecond = 0;
		recipe.parts << p;
	};
	add("hand", "hand.mesh.json", "", "");
	add("gun", "gun.mesh.json", "hand", "tag_weapon");
	add("barrel", "bare.mesh.json", "gun", "tag_barrel");
	add("flash", "bare.mesh.json", "gun", "tag_flash");
	recipe.parts[1].translation = {1, 2, 3};
	recipe.parts[1].rotation = {0, 0, 10};
	recipe.parts[1].scale = 1.5;
	recipe.parts[2].spinRpm = 150;
	ModelAssemblyResolved resolved;
	ok &= expect(resolveModelAssembly(recipe, context, &resolved, &error), "resolve the weapon set", error);
	if (!ok)
		return 1;
	ModelWeaponBundleOptions options{QStringLiteral("rail"), QStringLiteral("gun"), QStringLiteral("hand"), QStringLiteral("barrel"),
									 QStringLiteral("flash")};
	ModelWeaponBundle bundle, again;
	ok &= expect(prepareModelWeaponBundle(recipe, resolved, context, options, path("rail.assembly.json"), &bundle, &error) &&
					 prepareModelWeaponBundle(recipe, resolved, context, options, path("rail.assembly.json"), &again, &error),
				 "the weapon set reviews", error);
	if (!ok)
		return 1;
	const QString base = QStringLiteral("models/weapons2/rail/rail");
	ok &= expect(fileAt(bundle, base + ".md3") && fileAt(bundle, base + "_hand.md3") && fileAt(bundle, base + "_barrel.md3") &&
					 fileAt(bundle, base + "_flash.md3") && fileAt(bundle, "textures/weapon/gun.tga") &&
					 fileAt(bundle, "vibestudio/weapon-rail.json") && bundle.package.files.size() == 6,
				 "the package holds the four role models, the texture and the receipt at the game's paths");
	bool same = bundle.package.files.size() == again.package.files.size();
	for (int i = 0; same && i < bundle.package.files.size(); ++i)
		same = bundle.package.files[i].path == again.package.files[i].path && bundle.package.files[i].sha256 == again.package.files[i].sha256;
	ok &= expect(same && !QJsonDocument(bundle.package.receipt).toJson().contains(temporary.path().toUtf8()),
				 "review is deterministic and the receipt holds no machine paths");
	ok &= expect(bundle.package.notes.join(' ').contains("preview only"), "the barrel spin is noted as preview-only");

	// Baked transforms: the gun's tag_flash moves with the gun's local transform.
	ModelMesh gun;
	ok &= expect(importEditableModel("rail.md3", fileAt(bundle, base + ".md3")->bytes, &gun, &error), "the gun MD3 reopens", error);
	const auto *flashTag = findModelTag(gun, QStringLiteral("tag_flash"), 0);
	const double r = 10 * 3.14159265358979323846 / 180;
	// tag_flash sits at (8,0,0): scaled 1.5, turned 10 degrees about Z, moved by (1,2,3).
	const ModelVec3 expected{float(12 * std::cos(r) + 1), float(12 * std::sin(r) + 2), 3};
	ok &= expect(flashTag && closeTo(flashTag->origin, expected, 1e-3) && findModelTag(gun, QStringLiteral("tag_barrel"), 0),
				 "the re-decoded gun's tags carry the baked local transform");
	ModelMesh hand;
	ok &= expect(importEditableModel("rail_hand.md3", fileAt(bundle, base + "_hand.md3")->bytes, &hand, &error) && hand.frames.size() == 2 &&
					 findModelTag(hand, QStringLiteral("tag_weapon"), 1),
				 "the hand keeps its frames and tag_weapon", error);

	// Refusals.
	const auto refused = [&](ModelAssembly changed, ModelAssemblyResolved inputs, ModelWeaponBundleOptions chosen, const char *message) {
		inputs.recipeSha256 = modelAssemblyFingerprint(changed);
		ModelWeaponBundle rejected;
		ok &= expect(!prepareModelWeaponBundle(changed, inputs, context, chosen, {}, &rejected, &error), message);
	};
	auto noFlash = resolved;
	noFlash.inputs[1].mesh.tags.removeIf([](const auto &tag) { return tag.name == "tag_flash"; });
	refused(recipe, noFlash, options, "a weapon without tag_flash is refused");
	auto noBarrel = resolved;
	noBarrel.inputs[1].mesh.tags.removeIf([](const auto &tag) { return tag.name == "tag_barrel"; });
	refused(recipe, noBarrel, options, "a barrel without the weapon's tag_barrel is refused");
	auto noWeaponTag = resolved;
	noWeaponTag.inputs[0].mesh.tags.clear();
	refused(recipe, noWeaponTag, options, "a hand without tag_weapon is refused");
	auto flashy = resolved;
	ModelMesh twoPose;
	importEditableModel(path("flash2.mesh.json"), read(path("flash2.mesh.json")), &twoPose, &error);
	flashy.inputs[3].mesh = twoPose;
	refused(recipe, flashy, options, "a multi-pose flash is refused");
	auto misplaced = recipe;
	misplaced.parts[2].tag = QStringLiteral("tag_flash");
	refused(misplaced, resolved, options, "a barrel on the wrong tag is refused");
	auto badName = options;
	badName.weaponName = QStringLiteral("9mm");
	refused(recipe, resolved, badName, "a weapon ID starting with a digit is refused");
	auto repeated = options;
	repeated.flashPart = QStringLiteral("barrel");
	refused(recipe, resolved, repeated, "one part cannot take two roles");

	// Optional roles: the weapon alone.
	ModelWeaponBundleOptions alone{QStringLiteral("rail"), QStringLiteral("gun"), {}, {}, {}};
	auto single = recipe;
	single.parts = {recipe.parts[1]};
	single.parts[0].parent.clear();
	single.parts[0].tag.clear();
	ModelAssemblyResolved singleInputs;
	ModelWeaponBundle lone;
	ok &= expect(resolveModelAssembly(single, context, &singleInputs, &error) &&
					 prepareModelWeaponBundle(single, singleInputs, context, alone, {}, &lone, &error) && lone.package.files.size() == 3 &&
					 lone.package.notes.join(' ').contains("No hand model"),
				 "a weapon alone packages with notes for the missing roles", error);

	// Preview spin about the part's own X axis.
	ModelAssembly spinning;
	spinning.name = QStringLiteral("Spin");
	ModelAssemblyPart spun;
	spun.id = QStringLiteral("barrel");
	spun.source = QStringLiteral("bare.mesh.json");
	spun.framesPerSecond = 0;
	spun.spinRpm = 60; // a quarter turn in 0.25 s
	spinning.parts = {spun};
	ModelAssemblyResolved spinInputs;
	ModelAssemblyPose start, turned;
	ok &= expect(resolveModelAssembly(spinning, context, &spinInputs, &error) && sampleModelAssembly(spinning, spinInputs, 0, &start, &error) &&
					 sampleModelAssembly(spinning, spinInputs, .25, &turned, &error) &&
					 closeTo(start.mesh.surfaces[0].frames[0].positions[2], {0, 4, 0}, 1e-5) &&
					 closeTo(turned.mesh.surfaces[0].frames[0].positions[2], {0, 0, 4}, 1e-4),
				 "spin rolls the part about its own X axis at the given rate", error);
	const auto json = modelAssemblyJson(recipe);
	ModelAssembly parsed;
	ok &= expect(json.value("version").toInt() == 4 && parseModelAssembly(QJsonDocument(json).toJson(), &parsed, &error) &&
					 parsed.parts[2].spinRpm == 150,
				 "schema 4 round trips spin", error);

	// Publication through the shared PK3 writer.
	const auto dry = writeModelWeaponBundle(bundle, path("rail.pk3"), false, true);
	const auto real = writeModelWeaponBundle(bundle, path("rail.pk3"), false, false);
	ok &= expect(dry.succeeded() && real.succeeded() && real.determinismVerified && QFileInfo::exists(path("rail.pk3")),
				 "the PK3 writes atomically and deterministically");
	const auto overInput = writeModelWeaponBundle(bundle, path("gun.mesh.json"), true, false);
	ok &= expect(!overInput.succeeded() && read(path("gun.mesh.json")).size() > 0, "outputs never replace inputs");

	if (argc > 1)
	{
		ok &= expect(write(path("rail.assembly.json"), QJsonDocument(modelAssemblyJson(recipe)).toJson()), "recipe saved");
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
		const QStringList review{"model", "assembly", "rail.assembly.json", "--operation", "weapon-review", "--weapon-name", "rail",
								 "--weapon-part", "gun", "--hand-part", "hand", "--barrel-part", "barrel", "--flash-part", "flash",
								 "--package", "assets"};
		ok &= expect(run(review) && output.value("files").toArray().size() == 6, "CLI reviews the weapon set");
		auto exportArgs = review;
		exportArgs[4] = QStringLiteral("weapon-export");
		ok &= expect(run(exportArgs + QStringList{"--output", "cli-rail.pk3"}) && output.value("written").toBool() &&
						 QFileInfo::exists(path("cli-rail.pk3")),
					 "CLI exports the weapon PK3");
		ok &= expect(run(exportArgs + QStringList{"--output", "cli-rail.zip"}, 2) &&
						 run({"model", "assembly", "rail.assembly.json", "--operation", "weapon-review", "--weapon-name", "rail",
							  "--package", "assets"},
							 2),
					 "CLI requires a .pk3 output and the weapon part");
	}
	std::cout << checks << " weapon bundle checks\n";
	return ok ? 0 : 1;
}
