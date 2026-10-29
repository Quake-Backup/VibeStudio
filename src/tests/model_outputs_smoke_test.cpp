// Model output receipts (core/model_outputs.h): an export is Current, then
// SourceChanged after the source changes, OutputChanged after someone rewrites
// it and Missing once deleted; refresh reproduces it with the recorded
// settings, never over a changed output, and a cancelled or dry refresh
// leaves every output byte-identical. Receipts round-trip through the project
// manifest with relative paths and stay bounded. A player package's embedded
// receipt is checked against its files and its assembly's parts. With the
// studio executable as argv[1], `model outputs` reports the same.
#include "core/model_assembly.h"
#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_file_io.h"
#include "core/model_outputs.h"
#include "core/project_manifest.h"
#include "core/release_plan.h"
#include "core/studio_settings.h"
#include "tests/package_subset_test_helpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
QByteArray readBytes(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool writeBytes(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
ModelMesh prop(int parts)
{
	ModelDesign design;
	for (int index = 0; index < parts; ++index)
	{
		ModelDesignPart part;
		part.name = QStringLiteral("part%1").arg(index);
		part.origin = {float(80 * index), 0, 0};
		design.parts << part;
	}
	return buildModelDesignMesh(design);
}
QByteArray sha(const QByteArray &bytes)
{
	return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("model-outputs-XXXXXX")));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	const auto settings = QDir(temporary.path()).filePath(QStringLiteral("settings.ini"));
	StudioSettings::setOverrideFilePath(settings);
	bool ok = true;
	QString error;
	const QDir project(QDir(temporary.path()).filePath(QStringLiteral("project")));
	QDir().mkpath(project.filePath(QStringLiteral("models")));
	QDir().mkpath(project.filePath(QStringLiteral("build")));
	ok &= expect(saveProjectManifest(defaultProjectManifest(project.path()), &error), "create the project manifest");
	const auto sourcePath = project.filePath(QStringLiteral("models/crate.mesh.json"));
	const auto outputPath = project.filePath(QStringLiteral("build/crate.md3"));
	ModelDocument source;
	ok &= expect(source.setMesh(prop(1), &error) && source.save(sourcePath, false, &error), "save the source");

	// Export and record, as a guarded export does after its commit.
	ModelExportOptions options;
	const auto exported = exportEditableModel(source.mesh(), QStringLiteral("md3"), options, &error);
	ModelOutputFile file;
	file.path = outputPath;
	ok &= expect(!exported.isEmpty() && writeModelFile(inspectModelWriteTarget(outputPath), exported, &error), "write the export");
	const auto record = modelExportOutputRecord(sourcePath, source.sourceFingerprint(), QStringLiteral("md3"), options, file, exported);
	ok &= expect(recordModelOutput(project.path(), record, &error), "record the receipt in the manifest");
	ProjectManifest manifest;
	ok &= expect(loadProjectManifest(project.path(), &manifest, &error), "reload the manifest");
	auto records = projectModelOutputs(manifest);
	const auto stored = manifest.extraFields.value(modelOutputManifestKey()).toArray().first().toObject();
	ok &= expect(records.size() == 1 && stored.value(QStringLiteral("source")).toObject().value(QStringLiteral("path")).toString() ==
										   QStringLiteral("models/crate.mesh.json") &&
					 stored.value(QStringLiteral("outputs")).toArray().first().toObject().value(QStringLiteral("path")).toString() ==
						 QStringLiteral("build/crate.md3"),
				 "the receipt round-trips with project-relative paths");
	ok &= expect(modelOutputStatus(records.first()).state == ModelOutputState::Current, "a fresh export is current");
	ok &= expect(modelOutputSourceFor(records, QStringLiteral("crate.md3")).isEmpty() &&
					 QFileInfo(modelOutputSourceFor(records, outputPath)) == QFileInfo(sourcePath),
				 "an output finds the source it was made from");

	// The source changes: stale; refresh reproduces the output from it.
	ModelDocument edited;
	ok &= expect(edited.setMesh(prop(2), &error) && edited.save(sourcePath, true, &error), "change the source");
	ok &= expect(modelOutputStatus(records.first()).state == ModelOutputState::SourceChanged &&
					 modelOutputStateIsStale(ModelOutputState::SourceChanged),
				 "a changed source makes the output stale");
	const auto beforeRefresh = readBytes(outputPath);
	const auto dry = refreshModelOutput(records.first(), true);
	ok &= expect(dry.error.isEmpty() && !dry.written && readBytes(outputPath) == beforeRefresh, "a refresh dry run writes nothing");
	// Cancelled once the export is being written out: nothing is published.
	bool late = false;
	ModelWorkControl cancelLate;
	cancelLate.progress = [&](ModelWorkPhase phase, qint64, qint64) { late = late || phase >= ModelWorkPhase::Serializing; };
	cancelLate.cancelled = [&] { return late; };
	const auto cancelled = refreshModelOutput(records.first(), false, cancelLate);
	ok &= expect(!cancelled.written && readBytes(outputPath) == beforeRefresh, "a cancelled refresh leaves the output byte-identical");
	const auto refreshed = refreshModelOutput(records.first(), false);
	ok &= expect(refreshed.error.isEmpty() && refreshed.written && readBytes(outputPath) != beforeRefresh &&
					 recordModelOutput(project.path(), refreshed.record, &error),
				 "refresh rewrites the output with the recorded settings");
	ok &= expect(loadProjectManifest(project.path(), &manifest, &error) && projectModelOutputs(manifest).size() == 1 &&
					 modelOutputStatus(projectModelOutputs(manifest).first()).state == ModelOutputState::Current,
				 "the refreshed receipt replaces the old one and is current");
	records = projectModelOutputs(manifest);

	// Someone else rewrites the output: changed elsewhere, never refreshed over.
	ok &= expect(writeBytes(outputPath, QByteArray("not the export")), "rewrite the output elsewhere");
	ok &= expect(modelOutputStatus(records.first()).state == ModelOutputState::OutputChanged, "an output rewritten elsewhere is reported");
	const auto refused = refreshModelOutput(records.first(), false);
	ok &= expect(!refused.error.isEmpty() && readBytes(outputPath) == QByteArray("not the export"), "refresh refuses to replace a changed output");
	ok &= expect(QFile::remove(outputPath) && modelOutputStatus(records.first()).state == ModelOutputState::Missing,
				 "a deleted output is missing");
	const auto restored = refreshModelOutput(records.first(), false);
	ok &= expect(restored.written && QFileInfo::exists(outputPath), "refresh recreates a missing output");

	// Exported from unsaved edits: unknown, not stale.
	auto unsaved = record;
	unsaved.sourceSha256.clear();
	ok &= expect(modelOutputStatus(unsaved).state == ModelOutputState::Unknown, "an export of unsaved edits cannot be judged");

	// Detail levels: a recorded set refreshes every level.
	const auto lodBase = project.filePath(QStringLiteral("build/crate_lod.md3"));
	auto lods = record;
	lods.kind = QStringLiteral("lod-set");
	lods.sourceSha256 = edited.sourceFingerprint();
	lods.recipe.insert(QStringLiteral("levels"), 1);
	lods.recipe.insert(QStringLiteral("ratio"), 0.5);
	ModelOutputFile base, level;
	base.path = lodBase;
	level.path = project.filePath(QStringLiteral("build/crate_lod_1.md3"));
	lods.outputs = {base, level};
	const auto lodRefresh = refreshModelOutput(lods, false);
	ok &= expect(lodRefresh.written && lodRefresh.outputs.size() == 2 && QFileInfo::exists(level.path) &&
					 modelOutputStatus(lodRefresh.record).state == ModelOutputState::Current,
				 "a detail-level receipt refreshes the base and every level");

	// Bounded, replaced by key, damaged receipts ignored.
	ProjectManifest bounded = manifest;
	for (int index = 0; index < modelOutputRecordLimit + 20; ++index)
	{
		auto many = record;
		many.outputs.first().path = project.filePath(QStringLiteral("build/many_%1.md3").arg(index));
		addProjectModelOutput(&bounded, many);
	}
	addProjectModelOutput(&bounded, record);
	QStringList warnings;
	auto array = bounded.extraFields.value(modelOutputManifestKey()).toArray();
	array.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("export")}});
	bounded.extraFields.insert(modelOutputManifestKey(), array);
	const auto boundedRecords = projectModelOutputs(bounded, &warnings);
	ok &= expect(boundedRecords.size() == modelOutputRecordLimit && boundedRecords.last().key() == record.key() && warnings.size() == 1,
				 "receipts stay bounded, a newer one replaces its key, and a damaged one is ignored with a warning");

	// A player package's embedded receipt.
	const auto partPath = project.filePath(QStringLiteral("models/lower.md3"));
	ok &= expect(writeBytes(partPath, exported), "write an assembly part");
	ModelAssemblyPart lower;
	lower.id = QStringLiteral("lower");
	lower.source = QStringLiteral("lower.md3");
	ModelAssembly assembly;
	assembly.parts << lower;
	const auto assemblyPath = project.filePath(QStringLiteral("models/player.assembly.json"));
	ok &= expect(writeBytes(assemblyPath, QJsonDocument(modelAssemblyJson(assembly)).toJson()), "write the assembly");
	const QByteArray packaged = exported;
	const QJsonObject receipt{
		{QStringLiteral("schemaVersion"), 1},
		{QStringLiteral("format"), QStringLiteral("quake3-player-bundle")},
		{QStringLiteral("model"), QStringLiteral("crate")},
		{QStringLiteral("skin"), QStringLiteral("default")},
		{QStringLiteral("parts"), QJsonArray{QJsonObject{{QStringLiteral("part"), QStringLiteral("lower")},
														 {QStringLiteral("role"), QStringLiteral("lower")},
														 {QStringLiteral("sourceSha256"), QString::fromLatin1(sha(exported).toHex())}}}},
		{QStringLiteral("files"), QJsonArray{QJsonObject{{QStringLiteral("path"), QStringLiteral("models/players/crate/lower.md3")},
														 {QStringLiteral("sha256"), QString::fromLatin1(sha(packaged).toHex())}}}}};
	const auto pk3 = project.filePath(QStringLiteral("build/crate.pk3"));
	ok &= expect(subset_test::zip(pk3, {{"models/players/crate/lower.md3", packaged},
										{"vibestudio/player-bundle.json", QJsonDocument(receipt).toJson()}}),
				 "write a player package with its receipt");
	ModelOutputRecord packageRecord;
	ModelOutputStatus packageStatus;
	ok &= expect(readModelPackageReceipt(pk3, &packageRecord, &packageStatus, &error, assemblyPath) &&
					 packageStatus.state == ModelOutputState::Current && packageRecord.kind == QStringLiteral("player-package") &&
					 packageRecord.outputs.size() == 1 && packageRecord.inputs.size() == 1,
				 "the embedded receipt matches its files and the assembly's parts");
	ok &= expect(writeBytes(partPath, exported + "\n") && readModelPackageReceipt(pk3, nullptr, &packageStatus, &error, assemblyPath) &&
					 packageStatus.state == ModelOutputState::SourceChanged,
				 "a changed part makes the package stale");
	ok &= expect(subset_test::zip(pk3, {{"models/players/crate/lower.md3", packaged + "x"},
										{"vibestudio/player-bundle.json", QJsonDocument(receipt).toJson()}}) &&
					 readModelPackageReceipt(pk3, nullptr, &packageStatus, &error) && packageStatus.state == ModelOutputState::OutputChanged,
				 "a package whose files no longer match its receipt is reported");

	// A release of the project reports the stale output, as it reports a stale map.
	{
		ModelDocument newer;
		ok &= expect(newer.setMesh(prop(4), &error) && newer.save(sourcePath, true, &error), "change the source before a release");
		ProjectManifest releaseManifest;
		ok &= expect(loadProjectManifest(project.path(), &releaseManifest, &error), "load the manifest for a release");
		ReleaseRequest release;
		release.manifest = releaseManifest;
		release.gameKey = QStringLiteral("quake3");
		const auto plan = planRelease(release);
		ok &= expect(std::any_of(plan.problems.cbegin(), plan.problems.cend(),
								 [](const ReleaseProblem &problem) { return problem.kind == QStringLiteral("stale-model") && !problem.blocking; }),
					 "the release plan reports a stale model output");
		const auto again = refreshModelOutput(projectModelOutputs(releaseManifest).first(), false);
		ok &= expect(again.written && recordModelOutput(project.path(), again.record, &error), "refresh the output after the release check");
	}

	// The CLI reports and refreshes the same receipts.
	if (argc > 1)
	{
		QJsonObject output;
		const auto run = [&](const QStringList &args, int code)
		{
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			process.start(QString::fromLocal8Bit(argv[1]), QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"), settings} + args +
															   QStringList{QStringLiteral("--json")});
			if (!process.waitForFinished(60000))
			{
				process.kill();
				process.waitForFinished();
				return false;
			}
			output = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
			if (process.exitCode() != code)
				std::cerr << qPrintable(args.join(' ')) << " exit=" << process.exitCode() << '\n' << process.readAllStandardError().constData() << '\n';
			return process.exitCode() == code && !output.isEmpty();
		};
		ok &= expect(run({QStringLiteral("model"), QStringLiteral("outputs"), QStringLiteral("--project"), project.path()}, 0) &&
						 output.value(QStringLiteral("records")).toArray().size() == 1 &&
						 output.value(QStringLiteral("records")).toArray().first().toObject().value(QStringLiteral("state")).toString() ==
							 QStringLiteral("current"),
					 "model outputs lists a current receipt and exits 0");
		ModelDocument again;
		ok &= expect(again.setMesh(prop(3), &error) && again.save(sourcePath, true, &error), "change the source again");
		ok &= expect(run({QStringLiteral("model"), QStringLiteral("outputs"), sourcePath}, 4) &&
						 output.value(QStringLiteral("stale")).toInt() == 1,
					 "a stale output exits 4, found from the source's project");
		const auto beforeCli = readBytes(outputPath);
		ok &= expect(run({QStringLiteral("model"), QStringLiteral("outputs"), sourcePath, QStringLiteral("--refresh"), QStringLiteral("stale"),
						  QStringLiteral("--dry-run")},
						 0) &&
						 readBytes(outputPath) == beforeCli,
					 "--refresh stale --dry-run writes nothing");
		ok &= expect(run({QStringLiteral("model"), QStringLiteral("outputs"), sourcePath, QStringLiteral("--refresh"), QStringLiteral("stale")}, 0) &&
						 readBytes(outputPath) != beforeCli &&
						 run({QStringLiteral("model"), QStringLiteral("outputs"), sourcePath}, 0),
					 "--refresh stale rewrites the output and its receipt");
		ok &= expect(run({QStringLiteral("model"), QStringLiteral("outputs"), QStringLiteral("--receipt"), pk3}, 4) &&
						 output.value(QStringLiteral("state")).toString() == QStringLiteral("output-changed"),
					 "--receipt checks a player package's embedded receipt");
		ok &= expect(run({QStringLiteral("model"), QStringLiteral("outputs"), QStringLiteral("--project"), project.path(), QStringLiteral("--forget"),
						  outputPath},
						 0) &&
						 run({QStringLiteral("model"), QStringLiteral("outputs"), QStringLiteral("--project"), project.path()}, 0) &&
						 output.value(QStringLiteral("records")).toArray().isEmpty(),
					 "--forget drops a receipt");
		ok &= expect(run({QStringLiteral("model"), QStringLiteral("outputs"), QStringLiteral("--refresh"), QStringLiteral("x"), QStringLiteral("--forget"),
						  QStringLiteral("y"), QStringLiteral("--project"), project.path()},
						 2),
					 "conflicting actions are a usage error");
	}
	StudioSettings::setOverrideFilePath(QString());
	if (!ok)
	{
		std::cerr << qPrintable(error) << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
