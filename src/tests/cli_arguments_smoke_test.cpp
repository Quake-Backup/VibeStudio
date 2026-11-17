// How the command line reads its own arguments, through the real program:
// --settings-file needs a path (and takes one in any script), --quiet keeps a
// failing command's report, a value that cannot be used is a usage error
// rather than a silent default, an option is never another option's value,
// near misses of the shared options and unknown options are named (in JSON
// too), and package preview exits alike in text and JSON.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include <iostream>

namespace {
bool ok = true;

bool expect(bool pass, const char* label, const QString& detail = {})
{
	if (!pass) {
		ok = false;
		std::cerr << "FAIL: " << label << ": " << qPrintable(detail) << std::endl;
	}
	return pass;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// A closed brush, a light and a player start.
QByteArray mapText()
{
	return QByteArrayLiteral("{\n\"classname\" \"worldspawn\"\n{\n( 0 0 0 ) ( 0 1 0 ) ( 1 0 0 ) base/floor 0 0 0 1 1\n"
							 "( 0 0 64 ) ( 1 0 64 ) ( 0 1 64 ) base/floor 0 0 0 1 1\n( 0 0 0 ) ( 1 0 0 ) ( 0 0 1 ) base/floor 0 0 0 1 1\n"
							 "( 0 64 0 ) ( 0 64 1 ) ( 1 64 0 ) base/floor 0 0 0 1 1\n( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) base/floor 0 0 0 1 1\n"
							 "( 64 0 0 ) ( 64 1 0 ) ( 64 0 1 ) base/floor 0 0 0 1 1\n}\n}\n{\n\"classname\" \"light\"\n\"origin\" \"32 32 64\"\n}\n"
							 "{\n\"classname\" \"info_player_start\"\n\"origin\" \"32 32 24\"\n}\n");
}

struct Outcome {
	bool finished = false;
	int code = -1;
	QByteArray out;
	QByteArray err;

	QJsonObject json() const { return QJsonDocument::fromJson(out).object(); }
	QString all() const { return QStringLiteral("exit %1\n").arg(code) + QString::fromUtf8(out) + QString::fromUtf8(err); }
};

class Cli {
public:
	Cli(const QString& executable, const QString& folder)
		: m_executable(executable), m_folder(folder), m_settings(QDir(folder).filePath(QStringLiteral("settings.ini")))
	{
	}

	// The arguments exactly as given: nothing is added.
	Outcome raw(const QStringList& args) const
	{
		Outcome outcome;
		QProcess process;
		process.setWorkingDirectory(m_folder);
		// A run that reaches the studio by mistake must not open a window.
		QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
		environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
		process.setProcessEnvironment(environment);
		process.start(m_executable, args);
		outcome.finished = process.waitForStarted() && process.waitForFinished(120000);
		if (!outcome.finished) {
			process.kill();
			process.waitForFinished();
		}
		outcome.out = process.readAllStandardOutput();
		outcome.err = process.readAllStandardError();
		outcome.code = outcome.finished && process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
		return outcome;
	}

	// A command against this test's own settings file.
	Outcome run(const QStringList& args, int code, const char* label) const
	{
		const Outcome outcome = raw(QStringList {QStringLiteral("--cli"), QStringLiteral("--settings-file"), m_settings} + args);
		expect(outcome.code == code, label, args.join(QLatin1Char(' ')) + QStringLiteral(" -> ") + outcome.all());
		return outcome;
	}

	QString path(const QString& relative) const { return QDir(m_folder).filePath(relative); }

private:
	QString m_executable;
	QString m_folder;
	QString m_settings;
};

void settingsFile(const Cli& cli)
{
	// No path, an empty or blank one, or another option in its place: the run
	// stops before any settings are read, in the studio as on the command line.
	const QVector<QStringList> missing {
		{QStringLiteral("--cli"), QStringLiteral("--settings-file"), QString(), QStringLiteral("--version")},
		{QStringLiteral("--cli"), QStringLiteral("--settings-file"), QStringLiteral("--json"), QStringLiteral("--version")},
		{QStringLiteral("--cli"), QStringLiteral("--settings-file="), QStringLiteral("--version")},
		{QStringLiteral("--cli"), QStringLiteral("--settings-file=   "), QStringLiteral("--version")},
		{QStringLiteral("--cli"), QStringLiteral("--settings-file=--json"), QStringLiteral("--version")},
		{QStringLiteral("--cli"), QStringLiteral("--version"), QStringLiteral("--settings-file")},
		{QStringLiteral("--settings-file"), QStringLiteral("--self-test")},
	};
	for (const QStringList& args : missing) {
		const Outcome outcome = cli.raw(args);
		expect(outcome.code == 2 && outcome.err.contains("--settings-file requires a path.") && outcome.out.isEmpty(),
			"--settings-file without a path stops the run", args.join(QLatin1Char('|')) + QStringLiteral(" -> ") + outcome.all());
	}
	expect(!QFileInfo::exists(cli.path(QStringLiteral("--json"))) && !QFileInfo::exists(cli.path(QStringLiteral("--self-test"))),
		"no settings file is named after an option");
	const Outcome version = cli.run({QStringLiteral("--version")}, 0, "a real settings path is accepted");
	expect(version.out.contains("SlopStudio"), "the version prints", version.all());

	// A path outside the ANSI code page names the same file on every system.
	const QString folder = cli.path(QString::fromUtf8("\xce\xa9 \xd0\xbd\xd0\xb0\xd1\x81\xd1\x82\xd1\x80\xd0\xbe\xd0\xb9\xd0\xba\xd0\xb8"));
	const QString unicodeSettings = QDir(folder).filePath(QString::fromUtf8("\xd1\x81\xd1\x82\xd1\x83\xd0\xb4\xd0\xb8\xd1\x8f \xce\xa9.ini"));
	expect(QDir().mkpath(folder), "unicode folder");
	const Outcome remembered = cli.raw({QStringLiteral("--cli"), QStringLiteral("--settings-file"), unicodeSettings, QStringLiteral("--add-recent-project"),
		cli.path(QStringLiteral("package"))});
	expect(remembered.code == 0 && QFileInfo::exists(unicodeSettings), "a --settings-file path in another script is written where it says",
		remembered.all());
	const Outcome inlined = cli.raw({QStringLiteral("--cli"), QStringLiteral("--settings-file=") + unicodeSettings, QStringLiteral("--settings-report")});
	expect(inlined.code == 0 && QString::fromUtf8(inlined.out).contains(QFileInfo(unicodeSettings).fileName()),
		"the settings report names the unicode file", inlined.all());
}

void quiet(const Cli& cli, const QString& map)
{
	// A command whose failure report is printed on standard output.
	const QString existing = cli.path(QStringLiteral("quiet-existing.map"));
	expect(writeFile(existing, mapText()), "quiet fixture");
	const QStringList refused {QStringLiteral("map"), QStringLiteral("edit"), map, QStringLiteral("--entity"), QStringLiteral("1"), QStringLiteral("--set"),
		QStringLiteral("light=200"), QStringLiteral("--output"), existing};
	const Outcome plain = cli.run(refused, 4, "an existing output without --overwrite is refused");
	const QByteArray firstLine = plain.out.split('\n').value(0).trimmed();
	expect(!firstLine.isEmpty(), "the refusal is reported on standard output", plain.all());
	const Outcome hushed = cli.run(refused + QStringList {QStringLiteral("--quiet")}, 4, "--quiet keeps the exit code");
	expect(hushed.out.isEmpty() && !firstLine.isEmpty() && hushed.err.contains(firstLine), "--quiet sends a failing command's report to standard error",
		hushed.all());
	expect(readFile(existing) == mapText(), "the refused output is untouched");

	const QString fresh = cli.path(QStringLiteral("quiet-new.map"));
	const Outcome silent = cli.run({QStringLiteral("map"), QStringLiteral("edit"), map, QStringLiteral("--entity"), QStringLiteral("1"), QStringLiteral("--set"),
		QStringLiteral("light=200"), QStringLiteral("--output"), fresh, QStringLiteral("--quiet")}, 0, "a quiet command that succeeds");
	expect(silent.out.isEmpty() && silent.err.isEmpty() && readFile(fresh).contains("\"light\" \"200\""), "--quiet says nothing on success", silent.all());
}

void invalidValues(const Cli& cli, const QString& map)
{
	const QStringList render {QStringLiteral("map"), QStringLiteral("render"), map};
	const QVector<QPair<QStringList, QByteArray>> refused {
		{{QStringLiteral("--width"), QStringLiteral("wide")}, "--width"},
		{{QStringLiteral("--width"), QStringLiteral("9000")}, "--width"},
		{{QStringLiteral("--height"), QStringLiteral("0")}, "--height"},
		{{QStringLiteral("--grid"), QStringLiteral("-4")}, "--grid"},
		{{QStringLiteral("--grid"), QStringLiteral("fine")}, "--grid"},
		{{QStringLiteral("--highlight"), QStringLiteral("entity:99")}, "--highlight"},
	};
	for (const auto& [options, named] : refused) {
		const Outcome outcome = cli.run(render + options, 2, "map render refuses a value it cannot use");
		expect(outcome.err.contains(named) && outcome.out.isEmpty(), "the refused option is named", options.join(QLatin1Char(' ')) + QLatin1Char('\n') + outcome.all());
	}
	const Outcome sized = cli.run(render + QStringList {QStringLiteral("--width"), QStringLiteral("256"), QStringLiteral("--height"), QStringLiteral("128"),
		QStringLiteral("--grid"), QStringLiteral("0.5"), QStringLiteral("--highlight"), QStringLiteral("entity:1"), QStringLiteral("--json")}, 0,
		"map render with usable values");
	expect(sized.json().value(QStringLiteral("width")).toInt() == 256 && sized.json().value(QStringLiteral("height")).toInt() == 128,
		"map render draws the size it was given", sized.all());

	// --timeout-ms: a whole number, 0 for no limit.
	const QStringList compile {QStringLiteral("compiler"), QStringLiteral("run"), QStringLiteral("slopmap2-bsp"), QStringLiteral("--input"), map,
		QStringLiteral("--dry-run"), QStringLiteral("--json")};
	for (const QString& value : {QStringLiteral("soon"), QStringLiteral("-5"), QStringLiteral("1.5")}) {
		const Outcome outcome = cli.run(compile + QStringList {QStringLiteral("--timeout-ms"), value}, 2, "compiler run refuses a --timeout-ms it cannot use");
		expect(outcome.json().value(QStringLiteral("message")).toString().contains(QStringLiteral("--timeout-ms")), "--timeout-ms is named", outcome.all());
	}
	const Outcome unlimited = cli.raw(QStringList {QStringLiteral("--cli"), QStringLiteral("--settings-file"), cli.path(QStringLiteral("settings.ini"))} + compile
		+ QStringList {QStringLiteral("--timeout-ms"), QStringLiteral("0")});
	expect(unlimited.finished && unlimited.code != 2 && !unlimited.json().value(QStringLiteral("message")).toString().contains(QStringLiteral("--timeout-ms")),
		"--timeout-ms 0 is accepted as no limit", unlimited.all());

	const QString manifest = cli.path(QStringLiteral("extensions/sample/slopstudio.extension.json"));
	expect(writeFile(manifest, QByteArrayLiteral("{\n  \"schemaVersion\": 1,\n  \"id\": \"sample-tools\",\n  \"displayName\": \"Sample Tools\",\n"
		"  \"version\": \"1.0.0\",\n  \"trustLevel\": \"workspace\",\n  \"sandbox\": \"staged-files\",\n  \"capabilities\": [\"shader\"],\n"
		"  \"commands\": [{\n    \"id\": \"make-file\",\n    \"displayName\": \"Make File\",\n    \"program\": \"echo\",\n    \"arguments\": [\"generated\"],\n"
		"    \"generatedFiles\": [{\"virtualPath\": \"scripts/generated.shader\", \"summary\": \"Generated shader\"}]\n  }]\n}\n")),
		"extension manifest");
	const QStringList extension {QStringLiteral("extension"), QStringLiteral("run"), manifest, QStringLiteral("make-file"), QStringLiteral("--dry-run")};
	for (const QString& value : {QStringLiteral("0"), QStringLiteral("later")}) {
		const Outcome outcome = cli.run(extension + QStringList {QStringLiteral("--timeout-ms"), value}, 2, "extension run refuses a --timeout-ms it cannot use");
		expect(outcome.err.contains("--timeout-ms"), "extension run names --timeout-ms", outcome.all());
	}
	cli.run(extension + QStringList {QStringLiteral("--timeout-ms"), QStringLiteral("1500")}, 0, "extension run with a usable --timeout-ms");

	// Speech: the ranges the message has always promised. Nothing is spoken.
	const QVector<QStringList> speech {
		{QStringLiteral("--rate"), QStringLiteral("11")}, {QStringLiteral("--pitch"), QStringLiteral("-11")},
		{QStringLiteral("--volume"), QStringLiteral("101")}, {QStringLiteral("--volume"), QStringLiteral("loud")},
	};
	for (const QStringList& options : speech) {
		const Outcome outcome = cli.run(QStringList {QStringLiteral("accessibility"), QStringLiteral("speak"), QStringLiteral("--test")} + options, 2,
			"accessibility speak refuses a value out of range");
		expect(outcome.err.contains("--rate and --pitch take -10 to 10"), "the speech ranges are named", outcome.all());
	}
}

void previewExitCodes(const Cli& cli, const QString& package)
{
	for (const QString& command : {QStringLiteral("package preview"), QStringLiteral("asset inspect")}) {
		const QStringList words = command.split(QLatin1Char(' '));
		const Outcome plain = cli.run(words + QStringList {package, QStringLiteral("textures/missing.txt")}, 3, "a missing entry is not found, in text");
		expect(plain.err.contains("Error:") && !plain.out.contains("Error:"), "the text error goes to standard error", plain.all());
		const Outcome json = cli.run(words + QStringList {package, QStringLiteral("textures/missing.txt"), QStringLiteral("--json")}, 3,
			"a missing entry is not found, in JSON");
		expect(!json.json().value(QStringLiteral("ok")).toBool(true) && !json.json().value(QStringLiteral("message")).toString().isEmpty(),
			"the JSON failure says why", json.all());
		const Outcome found = cli.run(words + QStringList {package, QStringLiteral("textures/wall.txt")}, 0, "a present entry previews");
		expect(found.out.contains("Kind: text") && found.err.isEmpty(), "the preview is printed", found.all());
	}
}

void unknownOptions(const Cli& cli, const QString& map, const QString& package)
{
	// An option nothing knows, with and without --json.
	const Outcome json = cli.run({QStringLiteral("--json"), QStringLiteral("--frobnicate")}, 2, "an unknown option is a usage error");
	const QJsonObject object = json.json();
	expect(!object.isEmpty() && !object.value(QStringLiteral("ok")).toBool(true) && object.value(QStringLiteral("message")).toString().contains(QStringLiteral("--frobnicate")),
		"an unknown option is one JSON object that names it", json.all());
	const Outcome text = cli.run({QStringLiteral("--frobnicate")}, 2, "an unknown option in text");
	expect(text.err.contains("Unknown SlopStudio CLI option --frobnicate") && text.out.isEmpty(), "the text error names the option", text.all());

	// An unknown command names itself and lists its family.
	const Outcome unknown = cli.run({QStringLiteral("--json"), QStringLiteral("package"), QStringLiteral("frobnicate")}, 2, "an unknown command");
	const QString message = unknown.json().value(QStringLiteral("message")).toString();
	expect(message.contains(QStringLiteral("Unknown SlopStudio CLI subcommand")) && message.contains(QStringLiteral("package frobnicate"))
			&& message.contains(QStringLiteral("save-as")) && message.contains(QStringLiteral("validate")),
		"an unknown command is named with its family's commands", unknown.all());

	// A near miss of --dry-run must not become a real run.
	const Outcome added = cli.run({QStringLiteral("install"), QStringLiteral("add"), package, QStringLiteral("--install-game"), QStringLiteral("quake"),
		QStringLiteral("--install-name"), QStringLiteral("CLI arguments"), QStringLiteral("--json")}, 0, "install add");
	const QString id = added.json().value(QStringLiteral("installation")).toObject().value(QStringLiteral("id")).toString();
	expect(!id.isEmpty(), "the added installation has an id", added.all());
	const auto listed = [&]() {
		const Outcome list = cli.run({QStringLiteral("install"), QStringLiteral("list"), QStringLiteral("--json")}, 0, "install list");
		return QString::fromUtf8(list.out).contains(QLatin1Char('"') + id + QLatin1Char('"'));
	};
	expect(listed(), "the installation is listed");
	const Outcome mistyped = cli.run({QStringLiteral("install"), QStringLiteral("remove"), id, QStringLiteral("--dryrun")}, 2, "--dryrun is refused");
	expect(mistyped.err.contains("Unknown option --dryrun; did you mean --dry-run?") && listed(), "--dryrun removes nothing and names --dry-run", mistyped.all());
	const Outcome underscored = cli.run({QStringLiteral("install"), QStringLiteral("remove"), id, QStringLiteral("--dry_run=1"), QStringLiteral("--json")}, 2,
		"--dry_run is refused");
	expect(underscored.json().value(QStringLiteral("message")).toString().contains(QStringLiteral("did you mean --dry-run?"))
			&& underscored.json().value(QStringLiteral("command")).toString() == QStringLiteral("install remove") && listed(),
		"the near miss is one JSON object for its command", underscored.all());
	for (const auto& [option, intended] : {std::pair {QStringLiteral("--JSON"), QByteArrayLiteral("--json")}, std::pair {QStringLiteral("-quiet"), QByteArrayLiteral("--quiet")},
			std::pair {QStringLiteral("--over-write"), QByteArrayLiteral("--overwrite")}, std::pair {QStringLiteral("--Verbose"), QByteArrayLiteral("--verbose")}}) {
		const Outcome outcome = cli.run({QStringLiteral("install"), QStringLiteral("list"), option}, 2, "a near miss of a shared option is refused");
		expect(outcome.err.contains("did you mean " + intended + "?"), "the intended option is named", option + QLatin1Char('\n') + outcome.all());
	}
	cli.run({QStringLiteral("install"), QStringLiteral("remove"), id, QStringLiteral("--dry-run")}, 0, "--dry-run itself still works");
	expect(listed(), "a dry run removes nothing");

	// A value is a value, whatever it looks like: a compiler's own -verbose.
	const Outcome compilerSwitch = cli.run({QStringLiteral("compiler"), QStringLiteral("plan"), QStringLiteral("slopmap2-bsp"), QStringLiteral("--input"), map,
		QStringLiteral("--extra-args"), QStringLiteral("-verbose")}, 0, "a compiler switch is not a mistyped option");
	expect(compilerSwitch.out.contains("-verbose"), "the compiler switch reaches the plan", compilerSwitch.all());
}

void optionValues(const Cli& cli, const QString& map, const QString& package)
{
	// --output followed by another option has no value; the option keeps its meaning.
	const Outcome missing = cli.run({QStringLiteral("map"), QStringLiteral("edit"), map, QStringLiteral("--entity"), QStringLiteral("1"), QStringLiteral("--set"),
		QStringLiteral("light=200"), QStringLiteral("--output"), QStringLiteral("--overwrite")}, 2, "an option is never another option's value");
	expect(missing.err.contains("--output") && !QFileInfo::exists(cli.path(QStringLiteral("--overwrite"))), "no file is named after an option", missing.all());

	// The inline form still carries a value that is spelled like an option.
	expect(writeFile(QDir(package).filePath(QStringLiteral("scripts/watch.cfg")), QByteArrayLiteral("alias run \"compile --watch\"\n")), "find fixture");
	const Outcome found = cli.run({QStringLiteral("asset"), QStringLiteral("find"), package, QStringLiteral("--find=--watch")}, 0, "an inline value may look like an option");
	expect(found.out.contains("Matches: 1"), "the option-like text is found", found.all());

	// Negative numbers are values.
	const QString moved = cli.path(QStringLiteral("moved.map"));
	cli.run({QStringLiteral("map"), QStringLiteral("move"), map, QStringLiteral("--object"), QStringLiteral("entity:1"), QStringLiteral("--delta"),
		QStringLiteral("-16,0,0"), QStringLiteral("--output"), moved}, 0, "a negative delta is a value");
	expect(readFile(moved).contains("\"origin\" \"16 32 64\""), "the light moved by the negative delta", QString::fromUtf8(readFile(moved)));

	// A switch read with hasOption() takes no value: before the map path it
	// leaves the path alone. The map is read (its one material is asked for),
	// and the package, which has no such texture, makes the check exit 4.
	const Outcome switched = cli.run({QStringLiteral("map"), QStringLiteral("materials"), QStringLiteral("--geometry"), map, QStringLiteral("--package"), package,
		QStringLiteral("--json")}, 4, "a switch before the path keeps the path");
	expect(switched.json().value(QStringLiteral("materials")).toObject().value(QStringLiteral("requestedMaterials")).toInt() == 1
			&& switched.json().contains(QStringLiteral("geometry")),
		"map materials read the map and --geometry", switched.all());
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	if (argc != 2) {
		std::cerr << "usage: cli_arguments_smoke_test <slopstudio executable>\n";
		return 2;
	}
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	const Cli cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath(), temp.path());
	const QString map = cli.path(QStringLiteral("maps/start.map"));
	const QString package = cli.path(QStringLiteral("package"));
	expect(writeFile(map, mapText()) && writeFile(QDir(package).filePath(QStringLiteral("textures/wall.txt")), QByteArrayLiteral("wall")), "fixtures");

	settingsFile(cli);
	quiet(cli, map);
	invalidValues(cli, map);
	previewExitCodes(cli, package);
	unknownOptions(cli, map, package);
	optionValues(cli, map, package);

	if (ok) {
		std::cout << "CLI arguments: settings file, quiet failures, refused values, preview codes, unknown and mistyped options, option values passed." << std::endl;
	}
	return ok ? 0 : 1;
}
