// Bulk model audit (FMT-11, core/model_audit.h, model audit): per-entry
// classification over a folder and a PAK built in code, round trips within
// tolerance, cancellation mid-scan, deterministic JSON, the read-only
// guarantee and the command's exit codes.
#include "core/model_audit.h"
#include "core/model_decode_issues.h"
#include "core/model_document.h"
#include "tests/model_decode_issue_fixtures.h"
#include "tests/model_q3_animation_test_helpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>

#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;

namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *message)
{
	++checks;
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
bool writeFile(const QString &path, const QByteArray &bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
// Quake PAK: "PACK", directory offset and length, then 64-byte records.
QByteArray pak(const QVector<QPair<QByteArray, QByteArray>> &files)
{
	QByteArray data, directory;
	for (const auto &[name, bytes] : files)
	{
		QByteArray record = name.left(55);
		record.append(QByteArray(56 - record.size(), '\0'));
		char number[4];
		qToLittleEndian<qint32>(qint32(12 + data.size()), number);
		record.append(number, 4);
		qToLittleEndian<qint32>(qint32(bytes.size()), number);
		record.append(number, 4);
		directory += record;
		data += bytes;
	}
	QByteArray header("PACK");
	char number[4];
	qToLittleEndian<qint32>(qint32(12 + data.size()), number);
	header.append(number, 4);
	qToLittleEndian<qint32>(qint32(directory.size()), number);
	header.append(number, 4);
	return header + data + directory;
}
// Every file under `root` with its hash, to prove the audit wrote nothing.
QMap<QString, QByteArray> snapshot(const QString &root)
{
	QMap<QString, QByteArray> files;
	QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
	while (it.hasNext())
	{
		const QString path = it.next();
		QFile file(path);
		files.insert(path, file.open(QIODevice::ReadOnly) ? QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256) : QByteArray("unreadable"));
	}
	return files;
}
const ModelAuditEntry *entryNamed(const ModelAuditReport &report, const QString &path)
{
	for (const auto &entry : report.entries)
		if (entry.path == path)
			return &entry;
	return nullptr;
}
int runCli(const QString &program, const QStringList &arguments, QJsonObject *json = nullptr)
{
	QProcess process;
	process.start(program, QStringList{QStringLiteral("--cli")} + arguments);
	if (!process.waitForFinished(180000))
	{
		process.kill();
		return -1;
	}
	if (json)
		*json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
	return process.exitCode();
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (argc != 2 || root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("model-audit-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const QString program = QString::fromLocal8Bit(argv[1]);
	const QString game = temporary.filePath(QStringLiteral("game"));
	QString error;

	// A small installation: loose models, and a PAK with two more.
	ModelMesh source = q3Model();
	{
		auto &surface = source.surfaces[0];
		surface.texCoords.append({1, 1});
		surface.triangles.append({1, 3, 2});
		for (auto &frame : surface.frames)
		{
			frame.positions.append({10, 10, frame.positions[0].z});
			frame.normals.append({0, 0, 1});
		}
		updateEditableModelMetadata(&source);
	}
	const QByteArray md3 = exportEditableModel(source, QStringLiteral("md3"), 0, &error);
	QByteArray padded = md3 + QByteArray(16, '\0');
	QByteArray broken = md3;
	const qint32 surfaceOffset = qFromLittleEndian<qint32>(broken.constData() + 100);
	qToLittleEndian<qint32>(999, broken.data() + surfaceOffset + qFromLittleEndian<qint32>(broken.constData() + surfaceOffset + 88));
	const QByteArray iqm = exportEditableModel(source, QStringLiteral("iqm"), 0, &error);
	expect(!md3.isEmpty() && !iqm.isEmpty(), "build the fixture models");
	expect(writeFile(game + "/models/ok.md3", md3) && writeFile(game + "/models/padded.md3", padded) && writeFile(game + "/models/broken.md3", broken) &&
			   writeFile(game + "/models/layered.lwo", layeredLightWave(true)) && writeFile(game + "/models/notes.txt", "not a model") &&
			   writeFile(game + "/pak0.pak", pak({{"models/packed.md3", md3}, {"models/packed.iqm", iqm}, {"models/junk.md3", QByteArray("IDP3junk")}})),
		   "write the installation");
	const auto before = snapshot(game);

	ModelAuditOptions options;
	options.roundTrip = true;
	ModelAuditReport report;
	if (expect(auditModels(game, options, &report, &error) && !report.cancelled, "audit the installation"))
	{
		expect(report.containers.size() == 2 && report.entries.size() == 7, "loose files and the PAK's models are audited, other files ignored");
		const auto *ok = entryNamed(report, "models/ok.md3");
		expect(ok && ok->verdict == "clean" && ok->editable && ok->roundTrip.attempted && ok->roundTrip.withinTolerance(),
			   "a clean MD3 is editable and round-trips within 1/64 unit");
		const auto *advisory = entryNamed(report, "models/padded.md3");
		expect(advisory && advisory->verdict == "advisories" && advisory->editable && !advisory->needsAcceptance, "an MD3 advisory stays editable");
		const auto *layered = entryNamed(report, "models/layered.lwo");
		expect(layered && layered->verdict == "accept-omissions" && layered->editable && layered->needsAcceptance && !layered->roundTrip.attempted,
			   "an LWO omission is editable once accepted and has no writer to round-trip");
		const auto *damaged = entryNamed(report, "models/broken.md3");
		expect(damaged && damaged->verdict == "damaged" && !damaged->editable && !damaged->editableError.isEmpty(), "a dropped triangle is damage");
		const auto *junk = entryNamed(report, "models/junk.md3");
		expect(junk && junk->verdict == "failed" && !junk->decoded && !junk->error.isEmpty(), "an undecodable entry fails with its error");
		const auto *packedIqm = entryNamed(report, "models/packed.iqm");
		expect(packedIqm && packedIqm->editable && packedIqm->roundTrip.attempted && packedIqm->roundTrip.withinTolerance(), "an IQM inside the PAK round-trips");
		bool md3Summary = false;
		for (const auto &summary : report.formats)
			if (summary.format == "md3")
				md3Summary = summary.entries == 5 && summary.clean == 2 && summary.advisories == 1 && summary.damaged == 1 && summary.failed == 1 &&
							 summary.roundTrips == 3 && summary.roundTripsWithin == 3;
		expect(md3Summary, "the MD3 summary counts every verdict");
		expect(!modelAuditLines(report).isEmpty(), "the text report has lines");
	}
	expect(snapshot(game) == before, "the audit never writes into its source");

	// The JSON is stable: two runs agree byte for byte.
	ModelAuditReport again;
	expect(auditModels(game, options, &again, &error) &&
			   QJsonDocument(modelAuditJson(report)).toJson() == QJsonDocument(modelAuditJson(again)).toJson(),
		   "two runs give the same JSON");
	expect(modelAuditJson(report).value("entries").toArray().first().toObject().value("path").toString() == "models/broken.md3",
		   "entries are sorted by container and path");

	// A format filter, and a single package.
	ModelAuditOptions only;
	only.formats = {QStringLiteral("iqm")};
	ModelAuditReport filtered;
	expect(auditModels(game + "/pak0.pak", only, &filtered, &error) && filtered.entries.size() == 1 && filtered.entries[0].format == "iqm",
		   "a package audit honours the format filter");

	// Cancellation mid-scan keeps what was done.
	ModelAuditOptions cancelling;
	int seen = 0;
	cancelling.progress = [&seen](int, int, const QString &) { ++seen; };
	ModelWorkControl control;
	control.cancelled = [&seen] { return seen >= 3; };
	ModelAuditReport partial;
	expect(auditModels(game, cancelling, &partial, &error, control) && partial.cancelled && partial.entries.size() < 7,
		   "a cancelled audit stops and reports what it did");
	ModelAuditReport missing;
	expect(!auditModels(temporary.filePath("absent"), {}, &missing, &error) && !error.isEmpty(), "a missing path is an error");

	// CLI: JSON, --strict, a refused report path and a missing source.
	QJsonObject json;
	expect(runCli(program, {"model", "audit", game, "--round-trip", "--json"}, &json) == 0 && json.value("entries").toArray().size() == 7 &&
			   json.value("schema").toString() == "vibestudio.model-audit",
		   "model audit --json reports every entry");
	expect(runCli(program, {"model", "audit", game, "--strict", "--json"}, &json) == 4 && json.value("formats").toArray().size() >= 2,
		   "--strict exits 4 on damage and still prints the report");
	expect(runCli(program, {"model", "audit", game, "--report", game + "/audit.json"}) == 2 && !QFileInfo::exists(game + "/audit.json"),
		   "a report inside the source is refused");
	const QString reportPath = temporary.filePath("audit.json");
	expect(runCli(program, {"model", "audit", game, "--formats", "md3", "--report", reportPath}) == 0 &&
			   QJsonDocument::fromJson([&] {
				   QFile file(reportPath);
				   return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
			   }())
					   .object()
					   .value("entries")
					   .toArray()
					   .size() == 5,
		   "--report writes the JSON");
	expect(runCli(program, {"model", "audit", temporary.filePath("absent")}) == 3, "a missing source exits 3");
	expect(runCli(program, {"model", "audit", game, "--formats", "nope"}) == 2, "an unknown format is a usage error");
	expect(snapshot(game) == before, "the command never writes into its source");

	std::cout << "model_audit_smoke_test: " << checks << " checks, " << failures << " failure(s)\n";
	return failures == 0 ? 0 : 1;
}
