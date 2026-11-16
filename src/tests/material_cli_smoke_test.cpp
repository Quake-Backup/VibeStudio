// The `material` CLI family end to end: list, inspect, validate, render,
// graph, edit, templates, new, doom-tables, wal and usage, with base packages
// and Quake III dialects, run as a separate process against packages
// generated here (no game data).

#include "core/level_document.h"
#include "core/level_map.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>

#include <iostream>

namespace {

int failures = 0;

bool expect(bool condition, const std::string& message, const QString& detail = QString())
{
	if (!condition) {
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty()) {
			std::cerr << "\n  " << detail.toStdString();
		}
		std::cerr << '\n';
		++failures;
	}
	return condition;
}

void writeFile(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(bytes);
	}
}

QByteArray readFile(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QImage checker(int size, QRgb a, QRgb b, int cell)
{
	QImage image(size, size, QImage::Format_ARGB32);
	for (int y = 0; y < size; ++y) {
		for (int x = 0; x < size; ++x) {
			image.setPixel(x, y, ((x / cell + y / cell) % 2) == 0 ? a : b);
		}
	}
	return image;
}

// Uncompressed 32-bit TGA, top-left origin.
QByteArray tga(const QImage& source)
{
	const QImage image = source.convertToFormat(QImage::Format_ARGB32);
	QByteArray bytes(18, '\0');
	bytes[2] = 2;
	qToLittleEndian<quint16>(static_cast<quint16>(image.width()), bytes.data() + 12);
	qToLittleEndian<quint16>(static_cast<quint16>(image.height()), bytes.data() + 14);
	bytes[16] = 32;
	bytes[17] = 0x28;
	for (int y = 0; y < image.height(); ++y) {
		for (int x = 0; x < image.width(); ++x) {
			const QRgb pixel = image.pixel(x, y);
			bytes.append(static_cast<char>(qBlue(pixel)));
			bytes.append(static_cast<char>(qGreen(pixel)));
			bytes.append(static_cast<char>(qRed(pixel)));
			bytes.append(static_cast<char>(qAlpha(pixel)));
		}
	}
	return bytes;
}

struct Lump {
	QByteArray name;
	QByteArray data;
};

QByteArray doomWad(const QVector<Lump>& lumps)
{
	QByteArray body;
	QByteArray directory;
	int offset = 12;
	for (const Lump& lump : lumps) {
		QByteArray entry(16, '\0');
		qToLittleEndian<qint32>(lump.data.isEmpty() ? 0 : offset, entry.data());
		qToLittleEndian<qint32>(static_cast<qint32>(lump.data.size()), entry.data() + 4);
		for (int i = 0; i < 8 && i < lump.name.size(); ++i) {
			entry[8 + i] = lump.name.at(i);
		}
		directory.append(entry);
		body.append(lump.data);
		offset += static_cast<int>(lump.data.size());
	}
	QByteArray header("PWAD", 4);
	header.resize(12);
	qToLittleEndian<qint32>(static_cast<qint32>(lumps.size()), header.data() + 4);
	qToLittleEndian<qint32>(offset, header.data() + 8);
	return header + body + directory;
}

QByteArray doomPatch(int width, int height, int index)
{
	QByteArray bytes(8 + 4 * width, '\0');
	qToLittleEndian<qint16>(static_cast<qint16>(width), bytes.data());
	qToLittleEndian<qint16>(static_cast<qint16>(height), bytes.data() + 2);
	for (int x = 0; x < width; ++x) {
		qToLittleEndian<quint32>(static_cast<quint32>(bytes.size()), bytes.data() + 8 + 4 * x);
		bytes.append(static_cast<char>(0));
		bytes.append(static_cast<char>(height));
		bytes.append(static_cast<char>(0));
		for (int y = 0; y < height; ++y) {
			bytes.append(static_cast<char>(index + (y / 8) % 2));
		}
		bytes.append(static_cast<char>(0));
		bytes.append(static_cast<char>(0xFF));
	}
	return bytes;
}

QByteArray doomFixture()
{
	QByteArray palette;
	for (int index = 0; index < 256; ++index) {
		palette.append(static_cast<char>(index));
		palette.append(static_cast<char>((index * 7) % 256));
		palette.append(static_cast<char>(255 - index));
	}
	QByteArray colormap;
	for (int level = 0; level < 34; ++level) {
		for (int index = 0; index < 256; ++index) {
			colormap.append(static_cast<char>(level < 32 ? std::max(0, index - level * 4) : index));
		}
	}
	QByteArray pnames(4, '\0');
	qToLittleEndian<qint32>(1, pnames.data());
	pnames.append(QByteArray("WALLPAT\0", 8));
	QByteArray texture1(8, '\0');
	qToLittleEndian<qint32>(1, texture1.data());
	qToLittleEndian<qint32>(8, texture1.data() + 4);
	QByteArray record(22 + 10, '\0');
	record.replace(0, 8, QByteArray("TESTWALL", 8));
	qToLittleEndian<qint16>(64, record.data() + 12);
	qToLittleEndian<qint16>(128, record.data() + 14);
	qToLittleEndian<qint16>(1, record.data() + 20);
	qToLittleEndian<qint16>(1, record.data() + 22 + 6);
	texture1.append(record);
	const auto flat = [](int index) { return QByteArray(64 * 64, static_cast<char>(index)); };
	return doomWad({{"PLAYPAL", palette.repeated(14)}, {"COLORMAP", colormap}, {"PNAMES", pnames}, {"TEXTURE1", texture1}, {"P_START", {}},
		{"WALLPAT", doomPatch(64, 128, 40)}, {"P_END", {}}, {"F_START", {}}, {"NUKAGE1", flat(100)}, {"NUKAGE2", flat(140)},
		{"NUKAGE3", flat(180)}, {"F_END", {}}});
}

QByteArray quake2Wal(const QByteArray& name, const QByteArray& next, quint32 flags, qint32 value)
{
	QByteArray bytes(100, '\0');
	bytes.replace(0, name.size(), name);
	qToLittleEndian<quint32>(32, bytes.data() + 32);
	qToLittleEndian<quint32>(32, bytes.data() + 36);
	quint32 offset = 100;
	for (int level = 0; level < 4; ++level) {
		qToLittleEndian<quint32>(offset, bytes.data() + 40 + 4 * level);
		offset += static_cast<quint32>((32 >> level) * (32 >> level));
	}
	bytes.replace(56, next.size(), next);
	qToLittleEndian<quint32>(flags, bytes.data() + 88);
	qToLittleEndian<qint32>(value, bytes.data() + 96);
	for (int level = 0; level < 4; ++level) {
		const int side = 32 >> level;
		for (int i = 0; i < side * side; ++i) {
			bytes.append(static_cast<char>((i % side + i / side) % 256));
		}
	}
	return bytes;
}

const char* kShaders = R"(// CLI fixture
textures/test/wall
{
	{
		map $lightmap
	}
	{
		map textures/test/wall.tga
		blendFunc filter
	}
}

textures/test/glow
{
	surfaceparm nolightmap
	{
		map textures/test/glow.tga
		tcMod scroll 0.5 0
		rgbGen wave sin 0.5 0.5 0 1
	}
}

textures/test/anim
{
	{
		animMap 4 textures/test/anim0.tga textures/test/anim1.tga
	}
}

textures/test/missing
{
	{
		map textures/test/not_here.tga
	}
}

textures/test/bad
{
	{
		map textures/test/wall.tga
		frobnicate 1
	}
}
)";

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid() || argc != 2) {
		std::cerr << "usage: material_cli_smoke_test <slopstudio>\n";
		return 1;
	}
	const QString cli = QString::fromLocal8Bit(argv[1]);
	const QDir root(temp.path());
	int lastExit = 0;
	QString lastOutput;
	const auto runCommand = [&](const QStringList& arguments) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(cli, QStringList {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--settings-file"),
							   root.filePath(QStringLiteral("cli.ini"))}
				+ arguments);
		const bool ended = process.waitForStarted() && process.waitForFinished(120000);
		if (!ended) {
			process.kill();
			process.waitForFinished();
		}
		const QByteArray out = process.readAllStandardOutput();
		lastOutput = QString::fromUtf8(out + process.readAllStandardError());
		lastExit = ended && process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
		return QJsonDocument::fromJson(out).object();
	};
	const auto run = [&](const QStringList& arguments) { return runCommand(QStringList {QStringLiteral("material")} + arguments); };
	// Text mode, as a person sees it: stdout and stderr apart.
	QString lastStdout;
	QString lastStderr;
	const auto runText = [&](const QStringList& arguments) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(cli, QStringList {QStringLiteral("--cli"), QStringLiteral("--settings-file"), root.filePath(QStringLiteral("cli.ini")),
							   QStringLiteral("material")}
				+ arguments);
		const bool ended = process.waitForStarted() && process.waitForFinished(120000);
		if (!ended) {
			process.kill();
			process.waitForFinished();
		}
		lastStdout = QString::fromUtf8(process.readAllStandardOutput());
		lastStderr = QString::fromUtf8(process.readAllStandardError());
		lastOutput = lastStdout + lastStderr;
		lastExit = ended && process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
	};
	const auto entryNames = [](const QJsonObject& object) {
		QStringList list;
		for (const QJsonValue& entry : object.value(QStringLiteral("entries")).toArray()) {
			list << entry.toObject().value(QStringLiteral("name")).toString();
		}
		return list;
	};

	// Fixtures.
	const QString q3 = root.filePath(QStringLiteral("q3"));
	writeFile(q3 + QStringLiteral("/textures/test/wall.tga"), tga(checker(32, qRgb(180, 60, 40), qRgb(90, 30, 20), 8)));
	writeFile(q3 + QStringLiteral("/textures/test/glow.tga"), tga(checker(32, qRgb(255, 220, 120), qRgb(0, 0, 0), 8)));
	writeFile(q3 + QStringLiteral("/textures/test/anim0.tga"), tga(checker(32, qRgb(255, 0, 0), qRgb(255, 0, 0), 8)));
	writeFile(q3 + QStringLiteral("/textures/test/anim1.tga"), tga(checker(32, qRgb(0, 0, 255), qRgb(0, 0, 255), 8)));
	writeFile(q3 + QStringLiteral("/scripts/test.shader"), QByteArray(kShaders));
	const QString wad = root.filePath(QStringLiteral("test.wad"));
	writeFile(wad, doomFixture());
	const QString wal = root.filePath(QStringLiteral("q2/textures/e1u1/anim_1.wal"));
	writeFile(wal, quake2Wal("e1u1/anim_1", "e1u1/anim_2", 0x1, 300));
	writeFile(root.filePath(QStringLiteral("q2/textures/e1u1/anim_2.wal")), quake2Wal("e1u1/anim_2", "e1u1/anim_1", 0x1, 300));

	// list
	QJsonObject result = run({QStringLiteral("list"), q3});
	QStringList names;
	for (const QJsonValue& entry : result.value(QStringLiteral("entries")).toArray()) {
		names << entry.toObject().value(QStringLiteral("name")).toString();
	}
	expect(lastExit == 0 && names.contains(QStringLiteral("textures/test/glow")) && names.contains(QStringLiteral("textures/test/bad")),
		"material list reads every shader", lastOutput);
	result = run({QStringLiteral("list"), q3, QStringLiteral("--where"), QStringLiteral("animated=yes kind=shader")});
	names.clear();
	for (const QJsonValue& entry : result.value(QStringLiteral("entries")).toArray()) {
		names << entry.toObject().value(QStringLiteral("name")).toString();
	}
	names.sort();
	expect(names == QStringList {QStringLiteral("textures/test/anim"), QStringLiteral("textures/test/glow")}, "--where keeps the animated shaders",
		names.join(QLatin1Char(',')));
	run({QStringLiteral("list"), q3, QStringLiteral("--where"), QStringLiteral("nosuchkey=1")});
	expect(lastExit == 2, "a --where key no material has is a usage error", lastOutput);
	result = run({QStringLiteral("list"), q3, QStringLiteral("--where"), QStringLiteral("image=textures/test/anim1.tga")});
	names = entryNames(result);
	expect(lastExit == 0 && names.contains(QStringLiteral("textures/test/anim")) && !names.contains(QStringLiteral("textures/test/glow")),
		"image= matches one of a material's several images", names.join(QLatin1Char(',')) + lastOutput);
	result = run({QStringLiteral("list"), wad});
	expect(lastExit == 0 && result.value(QStringLiteral("matched")).toInt() == 4, "material list reads Doom walls and flats", lastOutput);
	result = run({QStringLiteral("list"), q3, QStringLiteral("--where"), QStringLiteral("stages>1 kind=shader")});
	names = entryNames(result);
	expect(lastExit == 0 && names.contains(QStringLiteral("textures/test/wall")) && !names.contains(QStringLiteral("textures/test/glow")),
		"a numeric --where term compares stage counts", names.join(QLatin1Char(',')));
	run({QStringLiteral("list"), q3, QStringLiteral("--where"), QStringLiteral("stages>x")});
	expect(lastExit == 2, "a numeric --where term without a number is a usage error", lastOutput);
	run({QStringLiteral("list"), q3, QStringLiteral("--where"), QStringLiteral("engin!=doom")});
	expect(lastExit == 2, "a mistyped --where key is a usage error even when every material matches", lastOutput);
	result = run({QStringLiteral("list"), q3, QStringLiteral("--where"), QStringLiteral("kind=shader"), QStringLiteral("--limit"), QStringLiteral("2")});
	expect(lastExit == 0 && result.value(QStringLiteral("shown")).toInt() == 2 && result.value(QStringLiteral("matched")).toInt() == 5
			&& result.value(QStringLiteral("entries")).toArray().size() == 2,
		"--limit caps the rows and reports how many matched", lastOutput);
	run({QStringLiteral("list"), q3, QStringLiteral("--where"), QStringLiteral("kind=shader"), QStringLiteral("--where"), QStringLiteral("sky=yes")});
	expect(lastExit == 2 && lastOutput.contains(QStringLiteral("Repeated option")), "a repeated option is a usage error", lastOutput);
	run({QStringLiteral("list"), wad, QStringLiteral("--palette"), QStringLiteral("doom9")});
	expect(lastExit == 2, "an unknown --palette is a usage error", lastOutput);
	run({QStringLiteral("list"), wad, QStringLiteral("--palette"), QStringLiteral("doom")});
	expect(lastExit == 0, "a known --palette is accepted", lastOutput);
	runText({QStringLiteral("list"), q3});
	expect(lastExit == 0 && lastStdout.contains(QStringLiteral("textures/test/bad")) && lastStdout.contains(QStringLiteral("rejected"))
			&& lastStderr.isEmpty(),
		"material list in text mode prints a line per material with its traits", lastOutput);

	// inspect
	result = run({QStringLiteral("inspect"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/glow")});
	const QJsonArray images = result.value(QStringLiteral("images")).toArray();
	expect(lastExit == 0 && images.size() == 1 && images.first().toObject().value(QStringLiteral("status")).toString() == QStringLiteral("image")
			&& result.value(QStringLiteral("text")).toString().contains(QStringLiteral("tcMod scroll")),
		"material inspect resolves the image and shows the text", lastOutput);
	run({QStringLiteral("inspect"), q3, QStringLiteral("--material"), QStringLiteral("textures/none")});
	expect(lastExit == 3, "inspecting a material that is not there is not-found", lastOutput);
	run({QStringLiteral("inspect"), q3});
	expect(lastExit == 2, "inspect without --material on a package of many is a usage error", lastOutput);
	result = run({QStringLiteral("inspect"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/wall"), QStringLiteral("--no-images")});
	expect(lastExit == 0 && result.value(QStringLiteral("sharedWith")).toArray().contains(QStringLiteral("textures/test/bad"))
			&& !result.value(QStringLiteral("sharedWith")).toArray().contains(QStringLiteral("textures/test/wall")),
		"inspect names the materials sharing its images", lastOutput);
	result = run({QStringLiteral("inspect"), root.filePath(QStringLiteral("q2")), QStringLiteral("--material"), QStringLiteral("e1u1/anim_1"),
		QStringLiteral("--no-images")});
	expect(lastExit == 0 && result.value(QStringLiteral("textKind")).toString() == QStringLiteral("wal-json")
			&& result.value(QStringLiteral("text")).toString().contains(QStringLiteral("\"animation\"")),
		"inspect shows a Quake II texture's .wal_json text", lastOutput);
	runText({QStringLiteral("inspect"), root.filePath(QStringLiteral("q2")), QStringLiteral("--material"), QStringLiteral("e1u1/anim_1"),
		QStringLiteral("--no-images"), QStringLiteral("--show-source")});
	expect(lastExit == 0 && lastStdout.contains(QStringLiteral("e1u1/anim_2")), "inspect --show-source prints that text", lastOutput);
	runText({QStringLiteral("inspect"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/missing")});
	expect(lastExit == 0 && lastStdout.contains(QStringLiteral("textures/test/not_here.tga: missing")), "inspect in text mode marks a missing image",
		lastOutput);

	// validate
	result = run({QStringLiteral("validate"), q3});
	QStringList codes;
	for (const QJsonValue& problem : result.value(QStringLiteral("problems")).toArray()) {
		codes << problem.toObject().value(QStringLiteral("code")).toString();
	}
	expect(lastExit == 4 && codes.contains(QStringLiteral("missing-image")) && codes.contains(QStringLiteral("engine-rejects")),
		"material validate reports the missing image and the rejected shader", lastOutput);
	expect(!result.value(QStringLiteral("ok")).toBool() && result.value(QStringLiteral("exitCode")).toObject().value(QStringLiteral("code")).toInt() == 4
			&& !result.value(QStringLiteral("message")).toString().isEmpty() && result.value(QStringLiteral("complete")).toBool()
			&& result.value(QStringLiteral("checked")).toInt() > 0,
		"a failed validate keeps its report in the JSON failure envelope", lastOutput);
	runText({QStringLiteral("validate"), q3});
	expect(lastExit == 4 && lastStdout.contains(QStringLiteral("missing-image")) && lastStdout.contains(QStringLiteral("Checked"))
			&& lastStderr.contains(QStringLiteral("Material validation found errors")),
		"validate in text mode prints the problems on stdout and the verdict on stderr", lastOutput);
	run({QStringLiteral("validate"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/glow")});
	expect(lastExit == 0, "a clean material validates", lastOutput);
	// --strict: a shadowed definition is a warning, which --strict fails.
	const QString shadow = root.filePath(QStringLiteral("shadow"));
	writeFile(shadow + QStringLiteral("/scripts/a.shader"), "textures/s/x\n{\n\t{\n\t\tmap $whiteimage\n\t}\n}\n");
	writeFile(shadow + QStringLiteral("/scripts/b.shader"), "textures/s/x\n{\n\t{\n\t\tmap $whiteimage\n\t}\n}\n");
	result = run({QStringLiteral("validate"), shadow, QStringLiteral("--no-images")});
	expect(lastExit == 0 && result.value(QStringLiteral("warnings")).toInt() > 0, "warnings alone pass without --strict", lastOutput);
	result = run({QStringLiteral("validate"), shadow, QStringLiteral("--no-images"), QStringLiteral("--strict")});
	expect(lastExit == 4 && !result.value(QStringLiteral("passed")).toBool() && result.value(QStringLiteral("message")).toString().contains(QStringLiteral("--strict")),
		"--strict fails on warnings", lastOutput);

	// render
	const QString sheet = root.filePath(QStringLiteral("anim.png"));
	result = run({QStringLiteral("render"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/anim"), QStringLiteral("--frames"),
		QStringLiteral("2"), QStringLiteral("--fps"), QStringLiteral("4"), QStringLiteral("--columns"), QStringLiteral("2"), QStringLiteral("--size"),
		QStringLiteral("64x64"), QStringLiteral("--orthographic"), QStringLiteral("--tiling"), QStringLiteral("1"), QStringLiteral("--output"), sheet});
	if (lastExit == 5) {
		// No OpenGL or Vulkan for the CLI here: the command says why and
		// writes nothing; the drawing checks need a renderer.
		expect(!QFile::exists(sheet) && lastOutput.contains(QStringLiteral("could not be drawn")),
			"material render without a 3D renderer says why and writes nothing", lastOutput);
		expect(qEnvironmentVariableIntValue("SLOPSTUDIO_RENDER_REQUIRE") == 0, "SLOPSTUDIO_RENDER_REQUIRE is set, but material render has no 3D renderer",
			lastOutput);
		std::cout << "No 3D renderer starts for the CLI here: material render drawing checks skipped.\n";
	} else {
		const QImage rendered(sheet);
		expect(lastExit == 0 && rendered.size() == QSize(128, 64), "material render writes a two-frame sheet", lastOutput);
		expect(!result.value(QStringLiteral("renderer")).toString().isEmpty(), "material render names the renderer that drew it", lastOutput);
		if (rendered.size() == QSize(128, 64)) {
			const QRgb first = rendered.pixel(32, 32);
			const QRgb second = rendered.pixel(96, 32);
			expect(qRed(first) > qBlue(first) && qBlue(second) > qRed(second), "animMap frames follow time in the sheet");
		}
		run({QStringLiteral("render"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/anim"), QStringLiteral("--output"), sheet});
		expect(lastExit == 1, "render will not replace a file without --overwrite", lastOutput);
		const QString dry = root.filePath(QStringLiteral("dry.png"));
		result = run({QStringLiteral("render"), wad, QStringLiteral("--material"), QStringLiteral("NUKAGE1"), QStringLiteral("--output"), dry,
			QStringLiteral("--dry-run")});
		expect(lastExit == 0 && !QFile::exists(dry) && !result.value(QStringLiteral("written")).toBool()
				&& result.value(QStringLiteral("shape")).toString() == QStringLiteral("floor"),
			"a dry render writes nothing and puts flats on the floor", lastOutput);
		result = run({QStringLiteral("render"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/bad"), QStringLiteral("--dry-run")});
		expect(result.value(QStringLiteral("fallback")).toBool(), "a shader Quake III drops renders as its fallback", lastOutput);
		// --cycle spreads the frames over one loop as the page's sheet does:
		// animMap 4 over two images is 0.5 s, which the page rounds up to 1 s.
		result = run({QStringLiteral("render"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/anim"), QStringLiteral("--frames"),
			QStringLiteral("2"), QStringLiteral("--cycle"), QStringLiteral("--size"), QStringLiteral("32x32"), QStringLiteral("--dry-run")});
		const QJsonArray cycled = result.value(QStringLiteral("frames")).toArray();
		expect(lastExit == 0 && qFuzzyCompare(result.value(QStringLiteral("cycleSeconds")).toDouble(), 1.0) && cycled.size() == 2
				&& qFuzzyCompare(cycled.at(1).toObject().value(QStringLiteral("time")).toDouble(), 0.5),
			"--cycle spreads the frames over one loop of the material", lastOutput);
		result = run({QStringLiteral("render"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/glow"), QStringLiteral("--swatch"),
			QStringLiteral("--size"), QStringLiteral("48x32"), QStringLiteral("--dry-run")});
		expect(lastExit == 0 && result.value(QStringLiteral("shape")).toString() == QStringLiteral("swatch")
				&& result.value(QStringLiteral("frameWidth")).toInt() == 32 && result.value(QStringLiteral("frameHeight")).toInt() == 32,
			"--swatch draws a square library swatch", lastOutput);
		result = run({QStringLiteral("render"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/glow"), QStringLiteral("--grid-ambient"),
			QStringLiteral("10"), QStringLiteral("--grid-directed"), QStringLiteral("20"), QStringLiteral("--entity-color"), QStringLiteral("255,128,0"),
			QStringLiteral("--globals"), QStringLiteral("0=1"), QStringLiteral("--light-radius"), QStringLiteral("256"), QStringLiteral("--orbit-seconds"),
			QStringLiteral("4"), QStringLiteral("--size"), QStringLiteral("32x32"), QStringLiteral("--dry-run")});
		expect(lastExit == 0, "the light grid, entity colour, globals, light radius and orbit options render", lastOutput);
	}
	run({QStringLiteral("render"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/anim"), QStringLiteral("--renderer"),
		QStringLiteral("software"), QStringLiteral("--dry-run")});
	expect(lastExit == 2, "material render refuses an unknown --renderer", lastOutput);
	const QStringList anim {QStringLiteral("render"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/anim"), QStringLiteral("--dry-run")};
	run(anim + QStringList {QStringLiteral("--grid-ambient"), QStringLiteral("300")});
	expect(lastExit == 2, "--grid-ambient past 255 is a usage error", lastOutput);
	run(anim + QStringList {QStringLiteral("--swatch"), QStringLiteral("--shape"), QStringLiteral("cube")});
	expect(lastExit == 2 && lastOutput.contains(QStringLiteral("--shape")), "--swatch refuses --shape", lastOutput);
	run(anim + QStringList {QStringLiteral("--swatch"), QStringLiteral("--tiling"), QStringLiteral("2")});
	expect(lastExit == 2 && lastOutput.contains(QStringLiteral("--tiling")), "--swatch refuses the framing options it sets itself", lastOutput);
	// Every other render option reaches the swatch (renderMaterialSwatch with base options).
	run(anim + QStringList {QStringLiteral("--swatch"), QStringLiteral("--background"), QStringLiteral("none"), QStringLiteral("--parms"), QStringLiteral("7=1")});
	expect(lastExit == 0, "--swatch takes the background and parms", lastOutput);
	run(anim + QStringList {QStringLiteral("--frames"), QStringLiteral("2"), QStringLiteral("--cycle"), QStringLiteral("--fps"), QStringLiteral("4")});
	expect(lastExit == 2, "--cycle and --fps together are a usage error", lastOutput);
	run(anim + QStringList {QStringLiteral("--entity-color"), QStringLiteral("255,0,0"), QStringLiteral("--parms"), QStringLiteral("0=1")});
	expect(lastExit == 2, "--entity-color and --parms 0-3 together are a usage error", lastOutput);
	run(anim + QStringList {QStringLiteral("--globals"), QStringLiteral("8=1")});
	expect(lastExit == 2, "--globals takes indexes 0 to 7", lastOutput);
	run(anim + QStringList {QStringLiteral("--light-angle"), QStringLiteral("30"), QStringLiteral("--orbit-seconds"), QStringLiteral("4")});
	expect(lastExit == 2, "--light-angle and --orbit-seconds together are a usage error", lastOutput);

	// graph
	result = run({QStringLiteral("graph"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/glow")});
	QStringList nodes;
	for (const QJsonValue& node : result.value(QStringLiteral("graph")).toObject().value(QStringLiteral("nodes")).toArray()) {
		nodes << node.toObject().value(QStringLiteral("id")).toString();
	}
	expect(nodes.contains(QStringLiteral("stage/0/tcmod/0")) && nodes.contains(QStringLiteral("material")), "material graph prints the nodes",
		nodes.join(QLatin1Char(',')));
	const QString graphEdits = root.filePath(QStringLiteral("graph.json"));
	writeFile(graphEdits, R"([{"edit": "set", "node": "stage/0/rgbgen", "property": "frequency", "value": "2"}])");
	const QString graphOut = root.filePath(QStringLiteral("graph.shader"));
	run({QStringLiteral("graph"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/glow"), QStringLiteral("--edits"), graphEdits,
		QStringLiteral("--output"), graphOut});
	expect(lastExit == 0 && readFile(graphOut).contains("rgbGen wave sin 0.5 0.5 0 2"), "a graph edit rewrites the shader text", lastOutput);
	const QString graphDry = root.filePath(QStringLiteral("graph-dry.shader"));
	result = run({QStringLiteral("graph"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/glow"), QStringLiteral("--edits"), graphEdits,
		QStringLiteral("--output"), graphDry, QStringLiteral("--dry-run")});
	expect(lastExit == 0 && !QFile::exists(graphDry) && result.value(QStringLiteral("text")).toString().contains(QStringLiteral("rgbGen wave sin 0.5 0.5 0 2")),
		"a dry graph edit shows the text and writes nothing", lastOutput);
	run({QStringLiteral("graph"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/glow"), QStringLiteral("--dry-run")});
	expect(lastExit == 2, "graph --dry-run needs --edits", lastOutput);
	run({QStringLiteral("graph"), wad, QStringLiteral("--material"), QStringLiteral("NUKAGE1"), QStringLiteral("--text-kind"), QStringLiteral("quake3-shader")});
	expect(lastExit == 2, "a text kind that does not fit the engine is a usage error", lastOutput);
	result = run({QStringLiteral("graph"), wad, QStringLiteral("--material"), QStringLiteral("NUKAGE1"), QStringLiteral("--text-kind"), QStringLiteral("swantbls")});
	expect(lastExit == 0 && !result.value(QStringLiteral("graph")).toObject().value(QStringLiteral("nodes")).toArray().isEmpty(),
		"a Doom flat's graph reads its SWANTBLS text", lastOutput);
	const QString noEdits = root.filePath(QStringLiteral("none.json"));
	writeFile(noEdits, "[]");
	run({QStringLiteral("graph"), wad, QStringLiteral("--material"), QStringLiteral("NUKAGE1"), QStringLiteral("--text-kind"), QStringLiteral("swantbls"),
		QStringLiteral("--edits"), noEdits, QStringLiteral("--output"), wad, QStringLiteral("--overwrite")});
	expect(lastExit == 2 && QFileInfo(wad).size() == doomFixture().size(), "graph will not write over the package it reads", lastOutput);
	// Adding a Doom range runs it to the last flat with the same name.
	const QString zapWad = root.filePath(QStringLiteral("zap.wad"));
	writeFile(zapWad, doomWad({{"F_START", {}}, {"ZAP1", QByteArray(4096, char(20))}, {"ZAP2", QByteArray(4096, char(30))},
						  {"ZAP3", QByteArray(4096, char(40))}, {"FLOORX", QByteArray(4096, char(50))}, {"F_END", {}}}));
	const QString rangeEdit = root.filePath(QStringLiteral("range.json"));
	writeFile(rangeEdit, R"([{"edit": "add", "node": "material", "template": "doom.range"}])");
	result = run({QStringLiteral("graph"), zapWad, QStringLiteral("--material"), QStringLiteral("ZAP1"), QStringLiteral("--text-kind"), QStringLiteral("swantbls"),
		QStringLiteral("--edits"), rangeEdit, QStringLiteral("--dry-run")});
	expect(lastExit == 0 && result.value(QStringLiteral("text")).toString().simplified().contains(QStringLiteral("ZAP3 ZAP1")),
		"adding a Doom range in the graph runs it to the last flat of the name", lastOutput);
	// A wal-json edit written to a .wal rewrites that WAL's header.
	const QString walEdits = root.filePath(QStringLiteral("wal-edits.json"));
	writeFile(walEdits, R"([{"edit": "set", "node": "flags", "property": "value", "value": 777}])");
	const QString graphWal = root.filePath(QStringLiteral("graph.wal"));
	run({QStringLiteral("graph"), root.filePath(QStringLiteral("q2")), QStringLiteral("--material"), QStringLiteral("e1u1/anim_1"), QStringLiteral("--edits"),
		walEdits, QStringLiteral("--output"), graphWal});
	const bool graphWalWritten = lastExit == 0;
	result = run({QStringLiteral("wal"), graphWal});
	expect(graphWalWritten && lastExit == 0 && result.value(QStringLiteral("value")).toInt() == 777 && readFile(graphWal).mid(100) == readFile(wal).mid(100),
		"a wal-json graph edit written to a .wal rewrites its header and keeps its pixels", lastOutput);
	const QString oversized = root.filePath(QStringLiteral("big.json"));
	writeFile(oversized, QByteArray(4 * 1024 * 1024 + 16, ' '));
	run({QStringLiteral("graph"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/glow"), QStringLiteral("--edits"), oversized,
		QStringLiteral("--dry-run")});
	expect(lastExit == 2, "an --edits file over 4 MiB is a usage error, not a missing file", lastOutput);
	run({QStringLiteral("graph"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/glow"), QStringLiteral("--edits"),
		root.filePath(QStringLiteral("nope.json")), QStringLiteral("--dry-run")});
	expect(lastExit == 3, "a missing --edits file is not-found", lastOutput);
	const QString opGraphEdits = root.filePath(QStringLiteral("graph-op.json"));
	writeFile(opGraphEdits, R"([{"op": "set", "node": "stage/0/rgbgen", "property": "frequency", "value": 3}])");
	result = run({QStringLiteral("graph"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/glow"), QStringLiteral("--edits"), opGraphEdits,
		QStringLiteral("--dry-run")});
	expect(lastExit == 0 && result.value(QStringLiteral("text")).toString().contains(QStringLiteral("rgbGen wave sin 0.5 0.5 0 3")),
		"a graph edit may name its kind with 'op'", lastOutput);
	writeFile(opGraphEdits, R"([{"edit": "set", "node": "stage/0/rgbgen", "property": "frequency", "value": [3]}])");
	run({QStringLiteral("graph"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/glow"), QStringLiteral("--edits"), opGraphEdits,
		QStringLiteral("--dry-run")});
	expect(lastExit == 2 && lastOutput.contains(QStringLiteral("'value'")), "a graph edit value given as an array is a usage error", lastOutput);

	// edit
	const QString script = root.filePath(QStringLiteral("edit.shader"));
	writeFile(script, QByteArray(kShaders));
	const QString textEdits = root.filePath(QStringLiteral("edits.json"));
	writeFile(textEdits, R"([{"op": "set", "material": "textures/test/glow", "stage": 1, "keyword": "tcMod", "arguments": "scroll 1 0.25"}])");
	result = run({QStringLiteral("edit"), script, QStringLiteral("--edits"), textEdits, QStringLiteral("--dry-run")});
	expect(lastExit == 0 && result.value(QStringLiteral("text")).toString().contains(QStringLiteral("tcMod scroll 1 0.25"))
			&& readFile(script) == QByteArray(kShaders),
		"a dry edit shows the text and leaves the script alone", lastOutput);
	run({QStringLiteral("edit"), script, QStringLiteral("--edits"), textEdits, QStringLiteral("--output"), script, QStringLiteral("--overwrite")});
	expect(lastExit == 0 && readFile(script).contains("tcMod scroll 1 0.25") && readFile(script).contains("// CLI fixture"),
		"an edit in place keeps the rest of the script", lastOutput);
	const QString badEdits = root.filePath(QStringLiteral("bad-edits.json"));
	writeFile(badEdits, R"([{"op": "set", "material": "textures/test/glow", "stage": 0, "keyword": "cull", "arguments": "none"}])");
	run({QStringLiteral("edit"), script, QStringLiteral("--edits"), badEdits, QStringLiteral("--dry-run")});
	expect(lastExit == 2 && lastOutput.contains(QStringLiteral("global block")), "an edit with stage 0 is a usage error naming the global block", lastOutput);
	writeFile(badEdits, R"([{"op": "set", "material": "textures/test/glow", "stage": 1, "keyword": "blendFunc", "arguments": ["GL_ONE", "GL_ONE"]}])");
	run({QStringLiteral("edit"), script, QStringLiteral("--edits"), badEdits, QStringLiteral("--dry-run")});
	expect(lastExit == 2 && lastOutput.contains(QStringLiteral("'arguments' must be text")), "edit arguments given as an array are a usage error", lastOutput);
	writeFile(badEdits, R"({"op": "set"})");
	run({QStringLiteral("edit"), script, QStringLiteral("--edits"), badEdits, QStringLiteral("--dry-run")});
	expect(lastExit == 2 && lastOutput.contains(QStringLiteral("JSON object")), "an edits file holding one object says so", lastOutput);
	result = run({QStringLiteral("edit"), q3, QStringLiteral("--script"), QStringLiteral("scripts/test.shader"), QStringLiteral("--edits"), textEdits,
		QStringLiteral("--dry-run")});
	expect(lastExit == 0 && result.value(QStringLiteral("text")).toString().contains(QStringLiteral("tcMod scroll 1 0.25")),
		"edit --script picks a script in a package", lastOutput);
	run({QStringLiteral("edit"), q3, QStringLiteral("--script"), QStringLiteral("scripts/nope.shader"), QStringLiteral("--edits"), textEdits,
		QStringLiteral("--dry-run")});
	expect(lastExit == 3 && lastOutput.contains(QStringLiteral("scripts/test.shader")), "an unknown --script names the scripts there are", lastOutput);
	run({QStringLiteral("edit"), script, QStringLiteral("--script"), QStringLiteral("scripts/test.shader"), QStringLiteral("--edits"), textEdits,
		QStringLiteral("--dry-run")});
	expect(lastExit == 2, "--script with a loose script is a usage error", lastOutput);
	// --strict: an edit that leaves an error writes nothing.
	const QString clean = root.filePath(QStringLiteral("clean.shader"));
	const QByteArray cleanText("textures/test/clean\n{\n\t{\n\t\tmap $whiteimage\n\t}\n}\n");
	writeFile(clean, cleanText);
	const QString badEdit = root.filePath(QStringLiteral("bad-edit.json"));
	writeFile(badEdit, R"([{"op": "add", "material": "textures/test/clean", "stage": 1, "keyword": "frobnicate", "arguments": "1"}])");
	result = run({QStringLiteral("edit"), clean, QStringLiteral("--edits"), badEdit, QStringLiteral("--dry-run")});
	expect(lastExit == 0 && result.value(QStringLiteral("errors")).toInt() > 0, "without --strict an edit that leaves an error is reported", lastOutput);
	run({QStringLiteral("edit"), clean, QStringLiteral("--edits"), badEdit, QStringLiteral("--output"), clean, QStringLiteral("--overwrite"),
		QStringLiteral("--strict")});
	expect(lastExit == 4 && readFile(clean) == cleanText, "edit --strict refuses to write a script with errors", lastOutput);

	// templates and new
	result = run({QStringLiteral("templates"), QStringLiteral("--engine"), QStringLiteral("doom3")});
	QStringList templates;
	for (const QJsonValue& each : result.value(QStringLiteral("templates")).toArray()) {
		templates << each.toObject().value(QStringLiteral("id")).toString();
	}
	expect(templates.contains(QStringLiteral("d3-standard")) && !templates.contains(QStringLiteral("q3-glow")), "templates filter by engine",
		templates.join(QLatin1Char(',')));
	run({QStringLiteral("new"), QStringLiteral("--template"), QStringLiteral("q3-glow"), QStringLiteral("--name"), QStringLiteral("textures/test/lamp"),
		QStringLiteral("--output"), script, QStringLiteral("--append")});
	expect(lastExit == 0 && readFile(script).contains("textures/test/lamp"), "new --append adds the material", lastOutput);
	run({QStringLiteral("new"), QStringLiteral("--template"), QStringLiteral("q3-glow"), QStringLiteral("--name"), QStringLiteral("textures/test/lamp"),
		QStringLiteral("--output"), script, QStringLiteral("--append")});
	expect(lastExit == 4, "new --append refuses a name the script already defines", lastOutput);
	const QString wrongEngine = root.filePath(QStringLiteral("wrong.shader"));
	run({QStringLiteral("new"), QStringLiteral("--template"), QStringLiteral("d3-standard"), QStringLiteral("--name"), QStringLiteral("textures/test/d3"),
		QStringLiteral("--output"), wrongEngine});
	expect(lastExit == 2 && !QFile::exists(wrongEngine), "a Doom 3 template is not written into a .shader script", lastOutput);
	run({QStringLiteral("new"), QStringLiteral("--template"), QStringLiteral("d3-standard"), QStringLiteral("--name"), QStringLiteral("textures/test/d3"),
		QStringLiteral("--output"), script, QStringLiteral("--append")});
	expect(lastExit == 2, "a Doom 3 template is not appended to a .shader script", lastOutput);

	// doom-tables
	const QString lumps = root.filePath(QStringLiteral("lumps"));
	result = run({QStringLiteral("doom-tables"), wad, QStringLiteral("--output-dir"), lumps});
	expect(lastExit == 0 && result.value(QStringLiteral("ranges")).toArray().size() == 22 && QFile::exists(lumps + QStringLiteral("/ANIMATED.lmp"))
			&& QFile::exists(lumps + QStringLiteral("/SWITCHES.lmp")),
		"doom-tables writes the Boom lumps from the vanilla tables", lastOutput);
	expect(QString::fromUtf8(readFile(lumps + QStringLiteral("/SWANTBLS.txt"))).simplified().contains(QStringLiteral("8 NUKAGE3 NUKAGE1")),
		"SWANTBLS text uses Boom's speed last first columns");
	result = run({QStringLiteral("doom-tables"), lumps + QStringLiteral("/SWANTBLS.txt")});
	expect(lastExit == 0 && result.value(QStringLiteral("ranges")).toArray().size() == 22
			&& result.value(QStringLiteral("switches")).toArray().size() == 40,
		"SWANTBLS text reads back", lastOutput);
	run({QStringLiteral("doom-tables"), wad, QStringLiteral("--output-dir"), lumps});
	expect(lastExit == 1, "doom-tables will not replace its outputs without --overwrite", lastOutput);
	run({QStringLiteral("doom-tables"), wad, QStringLiteral("--output-dir"), lumps, QStringLiteral("--overwrite")});
	expect(lastExit == 0, "doom-tables --overwrite replaces its outputs", lastOutput);

	// wal
	const QString metadata = root.filePath(QStringLiteral("anim.wal_json"));
	writeFile(metadata, R"({"flags": ["light", "slick"], "value": 1200})");
	const QString walOut = root.filePath(QStringLiteral("anim_1.wal"));
	run({QStringLiteral("wal"), wal, QStringLiteral("--metadata"), metadata, QStringLiteral("--output"), walOut});
	expect(lastExit == 0 && QFile::exists(walOut), "wal --metadata --output writes the rewritten WAL", lastOutput);
	result = run({QStringLiteral("wal"), walOut});
	const QJsonArray flags = result.value(QStringLiteral("surfaceFlagIds")).toArray();
	expect(lastExit == 0 && result.value(QStringLiteral("value")).toInt() == 1200 && flags.contains(QStringLiteral("slick"))
			&& result.value(QStringLiteral("nextFrame")).toString() == QStringLiteral("e1u1/anim_2"),
		"wal --metadata rewrites the header and keeps the chain", lastOutput);
	expect(readFile(walOut).mid(100) == readFile(wal).mid(100), "rewriting a WAL header leaves its pixels alone");
	result = run({QStringLiteral("wal"), root.filePath(QStringLiteral("q2")), QStringLiteral("--material"), QStringLiteral("e1u1/anim_2")});
	expect(lastExit == 0 && result.value(QStringLiteral("name")).toString() == QStringLiteral("e1u1/anim_2"), "wal reads a texture in a package",
		lastOutput);
	const QString walCopy = root.filePath(QStringLiteral("copy.wal"));
	run({QStringLiteral("wal"), wal, QStringLiteral("--output"), walCopy});
	expect(lastExit == 2 && !QFile::exists(walCopy), "wal --output without --metadata refuses a .wal name", lastOutput);
	const QString sidecar = root.filePath(QStringLiteral("sidecar.wal_json"));
	run({QStringLiteral("wal"), wal, QStringLiteral("--output"), sidecar});
	const QJsonObject sidecarJson = QJsonDocument::fromJson(readFile(sidecar)).object();
	expect(lastExit == 0 && sidecarJson.value(QStringLiteral("value")).toInt() == 300
			&& sidecarJson.value(QStringLiteral("animation")).toString() == QStringLiteral("e1u1/anim_2"),
		"wal --output writes the .wal_json sidecar", lastOutput);
	const QString shortWal = root.filePath(QStringLiteral("short.wal"));
	writeFile(shortWal, QByteArray(50, 'x'));
	run({QStringLiteral("wal"), shortWal});
	expect(lastExit == 4, "a WAL shorter than its header is refused", lastOutput);

	// Whole-definition edits without a JSON file.
	const QString shortcuts = root.filePath(QStringLiteral("shortcuts.shader"));
	writeFile(shortcuts, QByteArray(kShaders));
	result = run({QStringLiteral("edit"), shortcuts, QStringLiteral("--material"), QStringLiteral("textures/test/glow"), QStringLiteral("--duplicate"),
		QStringLiteral("textures/test/glow2"), QStringLiteral("--dry-run")});
	QStringList edited;
	for (const QJsonValue& each : result.value(QStringLiteral("materials")).toArray()) {
		edited << each.toString();
	}
	expect(lastExit == 0 && edited.contains(QStringLiteral("textures/test/glow")) && edited.contains(QStringLiteral("textures/test/glow2")),
		"edit --duplicate adds a renamed copy", lastOutput);
	run({QStringLiteral("edit"), shortcuts, QStringLiteral("--material"), QStringLiteral("textures/test/glow"), QStringLiteral("--rename"),
		QStringLiteral("textures/test/shine"), QStringLiteral("--output"), shortcuts, QStringLiteral("--overwrite")});
	expect(lastExit == 0 && readFile(shortcuts).contains("textures/test/shine") && !readFile(shortcuts).contains("textures/test/glow\n"),
		"edit --rename renames in place", lastOutput);
	run({QStringLiteral("edit"), shortcuts, QStringLiteral("--material"), QStringLiteral("textures/test/shine"), QStringLiteral("--remove"),
		QStringLiteral("--output"), shortcuts, QStringLiteral("--overwrite")});
	expect(lastExit == 0 && !readFile(shortcuts).contains("textures/test/shine"), "edit --remove deletes the definition", lastOutput);
	run({QStringLiteral("edit"), shortcuts, QStringLiteral("--rename"), QStringLiteral("x"), QStringLiteral("--dry-run")});
	expect(lastExit == 2, "--rename needs --material", lastOutput);

	// Dialects: a Jedi Knight keyword loads in Jedi Knight and drops the
	// shader in Quake III Arena.
	const QString jedi = root.filePath(QStringLiteral("jedi.shader"));
	writeFile(jedi, "textures/jk/sign\n{\n\t{\n\t\tmap textures/jk/sign.tga\n\t\tglow\n\t}\n}\n");
	result = run({QStringLiteral("inspect"), jedi, QStringLiteral("--no-images")});
	expect(lastExit == 0 && !result.value(QStringLiteral("entry")).toObject().value(QStringLiteral("rejected")).toBool(),
		"a Jedi Knight script is read as Jedi Knight's", lastOutput);
	result = run({QStringLiteral("inspect"), jedi, QStringLiteral("--no-images"), QStringLiteral("--dialect"), QStringLiteral("quake3")});
	expect(lastExit == 0 && result.value(QStringLiteral("entry")).toObject().value(QStringLiteral("rejected")).toBool(),
		"--dialect quake3 reads it as Quake III Arena, which drops it", lastOutput);
	// Image extensions follow the dialect: ioquake3 also tries .png for a
	// .tga name, Quake III 1.32 only retries .tga as .jpg.
	const QString pngOnly = root.filePath(QStringLiteral("pngonly"));
	QDir().mkpath(pngOnly + QStringLiteral("/textures/p"));
	checker(16, qRgb(200, 10, 10), qRgb(10, 10, 200), 4).save(pngOnly + QStringLiteral("/textures/p/img.png"));
	writeFile(pngOnly + QStringLiteral("/scripts/p.shader"), "textures/p/x\n{\n\t{\n\t\tmap textures/p/img.tga\n\t}\n}\n");
	result = run({QStringLiteral("validate"), pngOnly, QStringLiteral("--dialect"), QStringLiteral("quake3")});
	expect(lastExit == 0 && result.value(QStringLiteral("missingImages")).toInt() == 0, "ioquake3 finds a .tga stage image saved as .png", lastOutput);
	result = run({QStringLiteral("validate"), pngOnly, QStringLiteral("--dialect"), QStringLiteral("quake3-1.32")});
	codes.clear();
	for (const QJsonValue& problem : result.value(QStringLiteral("problems")).toArray()) {
		codes << problem.toObject().value(QStringLiteral("code")).toString();
	}
	expect(lastExit == 4 && codes.contains(QStringLiteral("missing-image")), "Quake III 1.32 does not find a .tga stage image saved as .png", lastOutput);

	// Doom 3: a table lookup no loaded script defines.
	const QString tables = root.filePath(QStringLiteral("tables.mtr"));
	writeFile(tables, "textures/d3/pulse\n{\n\t{\n\t\tblend add\n\t\tmap _white\n\t\trgb nosuchTable[ time ]\n\t}\n}\n");
	result = run({QStringLiteral("validate"), tables, QStringLiteral("--no-images")});
	codes.clear();
	for (const QJsonValue& problem : result.value(QStringLiteral("problems")).toArray()) {
		codes << problem.toObject().value(QStringLiteral("code")).toString();
	}
	expect(codes.contains(QStringLiteral("missing-table")), "validate reports a Doom 3 table no script defines", lastOutput);
	result = run({QStringLiteral("inspect"), tables, QStringLiteral("--no-images")});
	expect(lastExit == 0 && QJsonDocument(result.value(QStringLiteral("diagnostics")).toArray()).toJson().contains("missing-table"),
		"inspect reports a Doom 3 table no script defines", lastOutput);
	run({QStringLiteral("new"), QStringLiteral("--template"), QStringLiteral("q3-glow"), QStringLiteral("--name"), QStringLiteral("textures/a b")});
	expect(lastExit == 2, "new refuses a name the engine's lexer splits", lastOutput);
	run({QStringLiteral("edit"), jedi, QStringLiteral("--material"), QStringLiteral("textures/jk/sign"), QStringLiteral("--rename"), QStringLiteral("two words"),
		QStringLiteral("--dry-run")});
	expect(lastExit == 2, "edit --rename refuses a name the engine's lexer splits", lastOutput);
	run({QStringLiteral("list"), q3, QStringLiteral("--dialect"), QStringLiteral("doom9")});
	expect(lastExit == 2, "an unknown dialect is a usage error", lastOutput);
	result = run({QStringLiteral("list"), q3, QStringLiteral("--dialect"), QStringLiteral("quake3-1.32")});
	expect(lastExit == 0 && result.value(QStringLiteral("dialect")).toString() == QStringLiteral("quake3-1.32"), "the listing names its dialect",
		lastOutput);

	// Base packages: a mod over the base folder.
	const QString mod = root.filePath(QStringLiteral("mod"));
	writeFile(mod + QStringLiteral("/scripts/mod.shader"), "textures/mod/lamp\n{\n\t{\n\t\tmap textures/test/glow.tga\n\t}\n}\n");
	run({QStringLiteral("validate"), mod});
	expect(lastExit == 4, "without its base, a mod's borrowed image is missing", lastOutput);
	result = run({QStringLiteral("validate"), mod, QStringLiteral("--base"), q3});
	expect(lastExit == 0 && result.value(QStringLiteral("checked")).toInt() == 1, "with --base the image is found and base materials are not checked",
		lastOutput);
	result = run({QStringLiteral("list"), mod, QStringLiteral("--base"), q3, QStringLiteral("--include-base")});
	names.clear();
	for (const QJsonValue& entry : result.value(QStringLiteral("entries")).toArray()) {
		names << entry.toObject().value(QStringLiteral("name")).toString();
	}
	expect(lastExit == 0 && names.contains(QStringLiteral("textures/test/glow")) && names.contains(QStringLiteral("textures/mod/lamp"))
			&& result.value(QStringLiteral("basePackages")).toInt() == 1,
		"--include-base lists the base package's materials too", lastOutput);
	const QString q3b = root.filePath(QStringLiteral("q3b"));
	writeFile(q3b + QStringLiteral("/textures/other/plate.tga"), tga(checker(16, qRgb(10, 200, 10), qRgb(5, 100, 5), 4)));
	result = run({QStringLiteral("list"), mod, QStringLiteral("--base"), q3 + QDir::listSeparator() + q3b});
	expect(lastExit == 0 && result.value(QStringLiteral("basePackages")).toInt() == 2, "--base takes several packages joined as in PATH", lastOutput);
	run({QStringLiteral("list"), mod, QStringLiteral("--base"), root.filePath(QStringLiteral("nope"))});
	expect(lastExit == 3, "a --base that does not exist is not-found", lastOutput);
	run({QStringLiteral("list"), q3, QStringLiteral("--package"), root.filePath(QStringLiteral("nope"))});
	expect(lastExit == 3, "a --package that does not exist is not-found", lastOutput);

	// map materials reads the base packages beneath the package and the chosen
	// game as the Levels camera does: a mod map's face named after the base
	// game's shader finds it there.
	{
		slopstudio::LevelMapDocument map;
		slopstudio::LevelMapCreateRequest create;
		create.starterRoom = false;
		QString mapError;
		const QString mapPath = mod + QStringLiteral("/maps/lamp.map");
		QDir().mkpath(QFileInfo(mapPath).absolutePath());
		const bool built = slopstudio::createLevelMap(create, &map, &mapError)
			&& slopstudio::addLevelMapBoxBrush(&map, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("test/glow"))
			&& slopstudio::saveLevelMapAs(map, mapPath).succeeded();
		expect(built, "the mod map is written", mapError);
		const auto glow = [](const QJsonObject& object) {
			for (const QJsonValue& value : object.value(QStringLiteral("materials")).toObject().value(QStringLiteral("materials")).toArray()) {
				if (value.toObject().value(QStringLiteral("name")).toString() == QStringLiteral("test/glow")) {
					return value.toObject();
				}
			}
			return QJsonObject();
		};
		const QStringList mapArgs {QStringLiteral("map"), QStringLiteral("materials"), mapPath, QStringLiteral("--engine"), QStringLiteral("idTech3"),
			QStringLiteral("--package"), mod};
		result = runCommand(mapArgs);
		expect(glow(result).value(QStringLiteral("status")).toString() != QStringLiteral("stage-image"),
			"without --base the mod map's face does not find the base shader", lastOutput);
		result = runCommand(mapArgs + QStringList {QStringLiteral("--base"), q3});
		const QJsonObject found = glow(result);
		expect(found.value(QStringLiteral("status")).toString() == QStringLiteral("stage-image")
				&& found.value(QStringLiteral("shaderPath")).toString().startsWith(QStringLiteral("scripts/"))
				&& found.value(QStringLiteral("sourceLayer")).toString() == QStringLiteral("base:0"),
			"with --base the face finds the base shader and its image beneath the package", lastOutput);
		runCommand(mapArgs + QStringList {QStringLiteral("--dialect"), QStringLiteral("doom9")});
		expect(lastExit == 2, "map materials refuses an unknown dialect", lastOutput);
	}

	// override: the page's Override in Package, into the mod folder.
	{
		const QString overrides = mod + QStringLiteral("/scripts/zz_slopstudio_overrides.shader");
		result = run({QStringLiteral("override"), mod, QStringLiteral("--base"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/glow"),
			QStringLiteral("--dry-run")});
		expect(lastExit == 0 && !QFile::exists(overrides)
				&& result.value(QStringLiteral("virtualPath")).toString() == QStringLiteral("scripts/zz_slopstudio_overrides.shader")
				&& result.value(QStringLiteral("text")).toString().contains(QStringLiteral("tcMod scroll")),
			"a dry override names the override script and writes nothing", lastOutput);
		result = run({QStringLiteral("override"), mod, QStringLiteral("--base"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/glow"),
			QStringLiteral("--dialect"), QStringLiteral("quake3-1.32"), QStringLiteral("--dry-run")});
		expect(lastExit == 0 && result.value(QStringLiteral("virtualPath")).toString() == QStringLiteral("scripts/00_slopstudio_overrides.shader"),
			"Quake III 1.32, where the first definition wins, gets an override script read first", lastOutput);
		run({QStringLiteral("override"), mod, QStringLiteral("--base"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/glow")});
		expect(lastExit == 0 && readFile(overrides).contains("textures/test/glow"), "override copies the base shader into the mod", lastOutput);
		result = run({QStringLiteral("list"), mod, QStringLiteral("--base"), q3, QStringLiteral("--include-base"), QStringLiteral("--where"),
			QStringLiteral("name=textures/test/glow")});
		bool modWins = false;
		bool baseShadowed = false;
		for (const QJsonValue& value : result.value(QStringLiteral("entries")).toArray()) {
			const QJsonObject entry = value.toObject();
			// The JSON names the winner in shadowedBy only on a shadowed entry.
			if (entry.value(QStringLiteral("base")).toBool()) {
				baseShadowed = entry.contains(QStringLiteral("shadowedBy"));
			} else {
				modWins = !entry.contains(QStringLiteral("shadowedBy"));
			}
		}
		expect(modWins && baseShadowed, "after the override the mod's copy wins over the base shader", lastOutput);
		run({QStringLiteral("override"), mod, QStringLiteral("--base"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/glow")});
		expect(lastExit == 4, "a material the mod already has is not copied again", lastOutput);
		result = run({QStringLiteral("override"), mod, QStringLiteral("--base"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/wall")});
		expect(lastExit == 0 && result.value(QStringLiteral("appended")).toBool() && readFile(overrides).contains("textures/test/glow")
				&& readFile(overrides).contains("textures/test/wall"),
			"a second override is added to the same script", lastOutput);
		run({QStringLiteral("override"), mod, QStringLiteral("--material"), QStringLiteral("textures/test/glow")});
		expect(lastExit == 2, "override needs --base", lastOutput);
		run({QStringLiteral("override"), mod, QStringLiteral("--base"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/none")});
		expect(lastExit == 3, "overriding a material that is not there is not-found", lastOutput);
		// A Quake II texture: the WAL is copied whole.
		const QString q2mod = root.filePath(QStringLiteral("q2mod"));
		writeFile(q2mod + QStringLiteral("/readme.txt"), "mod\n");
		result = run({QStringLiteral("override"), q2mod, QStringLiteral("--base"), root.filePath(QStringLiteral("q2")), QStringLiteral("--material"),
			QStringLiteral("e1u1/anim_1")});
		expect(lastExit == 0 && readFile(q2mod + QStringLiteral("/textures/e1u1/anim_1.wal")) == readFile(wal)
				&& result.value(QStringLiteral("copy")).toString() == QStringLiteral("file"),
			"overriding a Quake II texture copies its WAL into the mod", lastOutput);
	}

	// usage: the maps that use a material.
	writeFile(q3 + QStringLiteral("/maps/test.map"),
		"{\n\"classname\" \"worldspawn\"\n{\n( 0 0 0 ) ( 1 0 0 ) ( 0 1 0 ) test/glow 0 0 0 0.5 0.5 0 0 0\n"
		"( 0 0 0 ) ( 0 1 0 ) ( 0 0 1 ) test/glow 0 0 0 0.5 0.5 0 0 0\n( 0 0 0 ) ( 0 0 1 ) ( 1 0 0 ) test/anim 0 0 0 0.5 0.5 0 0 0\n}\n}\n");
	result = run({QStringLiteral("usage"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/glow")});
	expect(lastExit == 0 && result.value(QStringLiteral("total")).toInt() == 2 && result.value(QStringLiteral("uses")).toArray().size() == 1,
		"usage counts a material's faces in each map", lastOutput);
	result = run({QStringLiteral("usage"), q3, QStringLiteral("--unused")});
	QStringList unused;
	for (const QJsonValue& each : result.value(QStringLiteral("unused")).toArray()) {
		unused << each.toObject().value(QStringLiteral("name")).toString();
	}
	expect(lastExit == 0 && unused.contains(QStringLiteral("textures/test/bad")) && !unused.contains(QStringLiteral("textures/test/glow")),
		"usage --unused lists the materials no map uses", lastOutput);
	result = run({QStringLiteral("usage"), q3, QStringLiteral("--limit"), QStringLiteral("1")});
	expect(lastExit == 0 && result.value(QStringLiteral("used")).toArray().size() == 1 && result.value(QStringLiteral("matched")).toInt() == 2
			&& result.value(QStringLiteral("complete")).toBool(),
		"usage --limit caps the rows and reports how many matched", lastOutput);
	result = run({QStringLiteral("usage"), mod, QStringLiteral("--base"), q3});
	QStringList maps;
	for (const QJsonValue& each : result.value(QStringLiteral("maps")).toArray()) {
		maps << each.toString();
	}
	expect(lastExit == 0 && maps.contains(QStringLiteral("maps/lamp.map")) && !maps.contains(QStringLiteral("maps/test.map")),
		"usage reads the package's own maps, not the base game's", maps.join(QLatin1Char(',')));
	result = run({QStringLiteral("usage"), mod, QStringLiteral("--base"), q3, QStringLiteral("--include-base")});
	maps.clear();
	for (const QJsonValue& each : result.value(QStringLiteral("maps")).toArray()) {
		maps << each.toString();
	}
	expect(lastExit == 0 && maps.contains(QStringLiteral("maps/lamp.map")) && maps.contains(QStringLiteral("maps/test.map"))
			&& result.value(QStringLiteral("baseMaps")).toArray().contains(QStringLiteral("maps/test.map")),
		"usage --include-base reads the base game's maps too", maps.join(QLatin1Char(',')));
	run({QStringLiteral("usage"), jedi});
	expect(lastExit == 2, "usage needs a package with maps, not a loose script", lastOutput);

	// usage
	run({QStringLiteral("render")});
	expect(lastExit == 2, "render without a source is a usage error", lastOutput);
	run({QStringLiteral("list"), q3, QStringLiteral("--frobnicate")});
	expect(lastExit == 2, "an unknown option is a usage error", lastOutput);
	run({QStringLiteral("nonsense")});
	expect(lastExit == 2, "an unknown material command is a usage error", lastOutput);
	runText({});
	expect(lastExit == 2 && lastStderr.contains(QStringLiteral("Give a material command")) && lastStderr.contains(QStringLiteral("override")),
		"material alone lists the commands", lastOutput);
	run({QStringLiteral("override")});
	expect(lastExit == 2, "override without a source is a usage error", lastOutput);

	if (failures > 0) {
		std::cerr << failures << " material CLI check(s) failed\n";
		return 1;
	}
	std::cout << "material CLI smoke passed\n";
	return 0;
}
