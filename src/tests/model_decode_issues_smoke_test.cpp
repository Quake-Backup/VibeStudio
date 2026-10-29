// Decode issue severities and the editable gate (FMT-02, core/model_decode_issues.h):
// advisories import, omissions import once accepted, damage never does; the
// source records what was accepted; model inspect and model import report
// severities. Every fixture is built in code; no game data is used.
#include "core/model_decode_issues.h"
#include "core/model_document.h"
#include "core/model_mesh.h"
#include "tests/model_decode_issue_fixtures.h"
#include "tests/model_q3_animation_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;

namespace
{
int checks = 0;
int failures = 0;
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

const ModelDecodeIssue *findIssue(const QVector<ModelDecodeIssue> &issues, const char *id)
{
	for (const auto &issue : issues)
	{
		if (issue.id == QLatin1String(id))
			return &issue;
	}
	return nullptr;
}

QByteArray readFile(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

struct CliRun
{
	int exitCode = -1;
	QJsonObject json;
};
CliRun runCli(const QString &program, const QStringList &arguments)
{
	QProcess process;
	process.start(program, QStringList{QStringLiteral("--cli")} + arguments);
	CliRun run;
	if (!process.waitForFinished(120000))
	{
		process.kill();
		return run;
	}
	run.exitCode = process.exitCode();
	run.json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
	return run;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (argc != 2 || root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("decode-issues-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const QString program = QString::fromLocal8Bit(argv[1]);
	QString error;

	// Severities have stable ids and names, and unclassified warnings are damage.
	ModelDecodeIssueSeverity parsed = ModelDecodeIssueSeverity::Advisory;
	expect(modelDecodeIssueSeverityId(ModelDecodeIssueSeverity::Omission) == QStringLiteral("omission") &&
			   modelDecodeIssueSeverityFromId(QStringLiteral("damage"), &parsed) && parsed == ModelDecodeIssueSeverity::Damage &&
			   !modelDecodeIssueSeverityFromId(QStringLiteral("warning"), &parsed),
		   "severity ids round-trip and unknown ids are refused");
	ModelMesh handMade = q3Model();
	handMade.warnings << QStringLiteral("Something nobody classified.");
	const auto handIssues = modelDecodeIssues(handMade);
	expect(handIssues.size() == 1 && handIssues[0].id == QStringLiteral("unclassified") && handIssues[0].severity == ModelDecodeIssueSeverity::Damage,
		   "a warning without a recorded issue counts as damage");
	expect(!validateEditableModel(handMade).isEmpty(), "unclassified damage keeps a model from being editable");

	// MD3: an end offset that does not match the file is an advisory. Two
	// triangles, so dropping one leaves the surface.
	ModelMesh md3Source = q3Model();
	{
		auto &surface = md3Source.surfaces[0];
		surface.texCoords.append({1, 1});
		surface.triangles.append({1, 3, 2});
		for (auto &frame : surface.frames)
		{
			frame.positions.append({10, 10, frame.positions[0].z});
			frame.normals.append({0, 0, 1});
		}
		updateEditableModelMetadata(&md3Source);
	}
	QByteArray md3 = exportEditableModel(md3Source, QStringLiteral("md3"), 0, &error);
	if (expect(!md3.isEmpty(), "the synthetic model exports as MD3"))
	{
		md3.append(QByteArray(16, '\0'));
		const ModelMesh decoded = decodeModelMesh(QStringLiteral("models/test/padded.md3"), md3);
		const auto *end = findIssue(decoded.issues, "md3-end-offset");
		expect(decoded.error.isEmpty() && end && end->severity == ModelDecodeIssueSeverity::Advisory && end->surface < 0,
			   "an MD3 padded past its end offset decodes with an advisory");
		ModelMesh imported;
		ModelImportReport report;
		expect(importEditableModel(QStringLiteral("models/test/padded.md3"), md3, &imported, ModelImportOptions{}, &report, &error) &&
				   !report.omissionsPending && imported.warnings.isEmpty() && imported.issues.size() == 1 &&
				   imported.issues[0].id == QStringLiteral("md3-end-offset"),
			   "an MD3 with an advisory imports as editable and keeps the advisory");
		ModelDocument document;
		expect(document.setMesh(imported, &error) && document.mesh().issues == imported.issues, "the document keeps the import record");

		// Damage: a triangle index out of range is dropped and refuses the import.
		QByteArray damaged = md3;
		const qint32 surfaceOffset = qFromLittleEndian<qint32>(damaged.constData() + 100);
		const qint32 triangleOffset = qFromLittleEndian<qint32>(damaged.constData() + surfaceOffset + 88);
		qToLittleEndian<qint32>(999, damaged.data() + surfaceOffset + triangleOffset);
		const ModelMesh broken = decodeModelMesh(QStringLiteral("models/test/broken.md3"), damaged);
		const auto brokenIssues = modelDecodeIssues(broken);
		const bool hasDamage = std::any_of(brokenIssues.cbegin(), brokenIssues.cend(),
										   [](const ModelDecodeIssue &issue) { return issue.severity == ModelDecodeIssueSeverity::Damage && issue.surface == 0; });
		expect(broken.error.isEmpty() && hasDamage, "a dropped triangle is surface damage");
		ModelImportOptions accept;
		accept.acceptOmissions = true;
		ModelMesh untouched = md3Source;
		expect(!importEditableModel(QStringLiteral("models/test/broken.md3"), damaged, &untouched, accept, &report, &error) &&
				   !report.omissionsPending && untouched.surfaces.size() == md3Source.surfaces.size() && error.contains(QStringLiteral("Damage")),
			   "damage refuses the import even with omissions accepted, naming its severity");
	}

	// LightWave: a second layer is an omission that needs acceptance.
	const QByteArray layered = layeredLightWave(true);
	const QString lwoPath = temporary.filePath(QStringLiteral("crate.lwo"));
	{
		QFile file(lwoPath);
		expect(file.open(QIODevice::WriteOnly) && file.write(layered) == layered.size(), "write the LightWave fixture");
	}
	const ModelMesh lwo = decodeModelMesh(QStringLiteral("models/test/crate.lwo"), layered);
	const auto *extra = findIssue(lwo.issues, "lwo-extra-layers");
	expect(lwo.error.isEmpty() && extra && extra->severity == ModelDecodeIssueSeverity::Omission, "an extra LightWave layer is an omission");
	expect(modelDecodeNeedsOmissionAcceptance(lwo), "only the omission blocks the layered object");
	ModelMesh refused;
	ModelImportReport report;
	expect(!importEditableModel(lwoPath, layered, &refused, ModelImportOptions{}, &report, &error) && report.omissionsPending &&
			   findIssue(report.issues, "lwo-extra-layers") && refused.surfaces.isEmpty(),
		   "an omission refuses the import until accepted and reports the issues");
	ModelImportOptions accept;
	accept.acceptOmissions = true;
	ModelMesh accepted;
	if (expect(importEditableModel(lwoPath, layered, &accepted, accept, &report, &error), "accepting the omission imports the object"))
	{
		const auto *record = findIssue(accepted.issues, "lwo-extra-layers");
		expect(record && record->accepted && accepted.warnings.isEmpty(), "the accepted omission is recorded and warnings clear");
		const auto json = editableModelJson(accepted, &error);
		ModelMesh reread;
		expect(json.value(QStringLiteral("version")).toInt() == 15 && json.value(QStringLiteral("importIssues")).toArray().size() == accepted.issues.size() &&
				   parseEditableModel(QJsonDocument(json).toJson(), &reread, &error) && reread.issues == accepted.issues,
			   "the import record round-trips through a version 15 source");
		auto early = json;
		early.insert(QStringLiteral("version"), 14);
		expect(!parseEditableModel(QJsonDocument(early).toJson(), &reread, &error), "a version 14 source cannot carry import issues");
		auto forged = json;
		auto list = forged.value(QStringLiteral("importIssues")).toArray();
		auto first = list[0].toObject();
		first.insert(QStringLiteral("severity"), QStringLiteral("damage"));
		list[0] = first;
		forged.insert(QStringLiteral("importIssues"), list);
		expect(!parseEditableModel(QJsonDocument(forged).toJson(), &reread, &error), "a source cannot record damage as accepted");
		const auto lines = modelDecodeIssueLines(accepted.issues);
		expect(lines.size() == accepted.issues.size() && lines.join(QLatin1Char('\n')).contains(QStringLiteral("(accepted)")),
			   "issue lines name accepted omissions");
		expect(!modelDecodeIssueSummary(accepted.issues).isEmpty(), "the summary counts the record");
	}
	ModelMesh single;
	expect(importEditableModel(QStringLiteral("models/test/crate.lwo"), layeredLightWave(false), &single, &error) && single.issues.isEmpty(),
		   "a single-layer object imports without issues");

	// CLI: inspect reports severities and the verdict; import needs --accept-omissions.
	auto inspect = runCli(program, {QStringLiteral("model"), QStringLiteral("inspect"), lwoPath, QStringLiteral("--json")});
	const auto inspected = inspect.json.value(QStringLiteral("model")).toObject();
	const auto issues = inspected.value(QStringLiteral("issues")).toArray();
	bool omissionListed = false;
	for (const auto &issue : issues)
		omissionListed |= issue.toObject().value(QStringLiteral("id")).toString() == QStringLiteral("lwo-extra-layers") &&
						  issue.toObject().value(QStringLiteral("severity")).toString() == QStringLiteral("omission");
	expect(inspect.exitCode == 0 && omissionListed && inspected.value(QStringLiteral("decodeVerdict")).toString() == QStringLiteral("accept-omissions") &&
			   inspected.value(QStringLiteral("issueCounts")).toObject().value(QStringLiteral("omission")).toInt() == 1,
		   "model inspect --json lists severities and the decode verdict");
	const QString output = temporary.filePath(QStringLiteral("crate.mesh.json"));
	auto plain = runCli(program, {QStringLiteral("model"), QStringLiteral("import"), lwoPath, QStringLiteral("--output"), output, QStringLiteral("--json")});
	expect(plain.exitCode != 0 && plain.json.value(QStringLiteral("omissionsPending")).toBool() &&
			   plain.json.value(QStringLiteral("message")).toString().contains(QStringLiteral("--accept-omissions")) && !QFileInfo::exists(output),
		   "model import refuses an omission and suggests --accept-omissions");
	auto accepting = runCli(program, {QStringLiteral("model"), QStringLiteral("import"), lwoPath, QStringLiteral("--accept-omissions"),
									  QStringLiteral("--output"), output, QStringLiteral("--json")});
	const auto recorded = accepting.json.value(QStringLiteral("issues")).toArray();
	bool acceptedListed = false;
	for (const auto &issue : recorded)
		acceptedListed |= issue.toObject().value(QStringLiteral("accepted")).toBool();
	expect(accepting.exitCode == 0 && acceptedListed && QJsonDocument::fromJson(readFile(output)).object().contains(QStringLiteral("importIssues")),
		   "model import --accept-omissions writes a source that records the omission");

	std::cout << "model_decode_issues_smoke_test: " << checks << " checks, " << failures << " failure(s)\n";
	return failures == 0 ? 0 : 1;
}
