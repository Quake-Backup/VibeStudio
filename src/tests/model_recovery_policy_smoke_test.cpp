// Recovery copies against their sources (unchanged, changed, missing, no
// source), the retention policy (oldest first, never a live owner, dry runs
// remove nothing), the startup inventory, and `model recoveries --json`
// reporting each copy's source state.
#include "app/model_recovery_discovery.h"
#include "core/model_design.h"
#include "core/model_recovery.h"
#include "core/model_recovery_policy.h"
#include "core/model_session_draft.h"
#include "core/model_weight_stroke_recipe.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>

#include <algorithm>
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
bool expect(bool value, const char *message, const QString &detail)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << ": " << qPrintable(detail) << '\n';
	}
	return value;
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
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("mesh-recovery-policy-XXXXXX")));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	const auto settings = QDir(temporary.path()).filePath(QStringLiteral("settings.ini"));
	StudioSettings::setOverrideFilePath(settings);
	const auto directory = modelRecoveryDirectory();
	bool ok = true;
	QString error;
	ModelDesign design;
	design.parts << ModelDesignPart{};
	const auto mesh = buildModelDesignMesh(design);

	// One copy per source state.
	const auto source = [&](const QString &name) { return QDir(temporary.path()).filePath(name); };
	const auto copy = [&](const QString &sourcePath, const QByteArray &sha256)
	{
		ModelRecoverySnapshot snapshot;
		snapshot.mesh = mesh;
		snapshot.title = QFileInfo(sourcePath).fileName();
		snapshot.sourcePath = sourcePath;
		snapshot.sourceSha256 = sha256;
		const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		return writeModelRecovery(snapshot, directory, id, &error).isEmpty() ? QString() : id;
	};
	QStringList ids;
	QHash<QString, ModelRecoverySourceState> expected;
	for (const auto &[name, state] : {std::pair{QStringLiteral("unchanged.mesh.json"), ModelRecoverySourceState::Unchanged},
									  std::pair{QStringLiteral("changed.mesh.json"), ModelRecoverySourceState::Changed},
									  std::pair{QStringLiteral("missing.mesh.json"), ModelRecoverySourceState::Missing},
									  std::pair{QString(), ModelRecoverySourceState::Unsaved}})
	{
		ModelDocument document;
		QString path;
		QByteArray sha256;
		if (!name.isEmpty())
		{
			path = source(name);
			ok &= expect(document.setMesh(mesh, &error) && document.save(path, true, &error), "write a source");
			sha256 = document.sourceFingerprint();
		}
		const auto id = copy(path, sha256);
		ok &= expect(!id.isEmpty(), "write a recovery copy");
		ids << id;
		expected.insert(id, state);
		QThread::msleep(20);
	}
	QFile changed(source(QStringLiteral("changed.mesh.json")));
	ok &= expect(changed.open(QIODevice::Append) && changed.write("\n") == 1, "change one source after its copy");
	changed.close();
	ok &= expect(QFile::remove(source(QStringLiteral("missing.mesh.json"))), "remove one source after its copy");
	const auto scan = listModelRecoveries(directory);
	ok &= expect(scan.records.size() == 4, "four copies are listed");
	for (const auto &record : scan.records)
	{
		ok &= expect(modelRecoverySourceState(record) == expected.value(record.id),
					 "each copy reports whether its source is unchanged, changed, missing or never saved");
	}
	ok &= expect(modelRecoverySourceStateId(ModelRecoverySourceState::Changed) == QStringLiteral("changed") &&
					 !modelRecoverySourceStateText(ModelRecoverySourceState::Missing).isEmpty(),
				 "states have stable ids and readable text");

	// The startup inventory counts copies and changed sources; a live owner
	// (this process) keeps every copy.
	auto inventory = discoverModelRecoveries(directory, QDir(temporary.path()).filePath(QStringLiteral("assemblies")));
	ok &= expect(inventory.meshCopies == 0 && inventory.pruned.isEmpty(),
				 "copies this running studio writes are not offered as crash leftovers or pruned");

	// Retention: newest two kept; a dry run removes nothing; live owners stay.
	const auto live = pruneModelRecoveries(directory, {2, 1LL << 40}, [](qint64) { return true; });
	ok &= expect(live.removed.isEmpty() && live.keptLive == 2 && live.kept == 2, "live owners are never pruned");
	const auto dry = pruneModelRecoveries(directory, {2, 1LL << 40}, [](qint64) { return false; }, {}, true);
	ok &= expect(dry.removed.size() == 2 && listModelRecoveries(directory).records.size() == 4, "a dry run reports without removing");
	const auto pruned = pruneModelRecoveries(directory, {2, 1LL << 40}, [](qint64) { return false; });
	const auto left = listModelRecoveries(directory).records;
	ok &= expect(pruned.removed == QStringList{ids.at(0), ids.at(1)} && left.size() == 2 &&
					 ((left.at(0).id == ids.at(3) && left.at(1).id == ids.at(2)) || (left.at(0).id == ids.at(2) && left.at(1).id == ids.at(3))),
				 "the oldest copies beyond the policy are removed, oldest first");
	const auto bySize = pruneModelRecoveries(directory, {32, 1}, [](qint64) { return false; });
	ok &= expect(bySize.removed.size() == 2 && listModelRecoveries(directory).records.isEmpty(), "the size limit applies too");

	// Staged-session sidecars (core/model_session_draft.h): the stroke recipe
	// round-trips, headers list without the payload, a damaged payload is
	// refused, and sidecars go with their copy or, orphaned, with retention.
	ModelWeightStroke stroke;
	stroke.joint = QStringLiteral("tip");
	stroke.mode = ModelWeightPaintMode::Add;
	stroke.value = .25;
	stroke.lockedJoints = {QStringLiteral("root")};
	stroke.samples = {{0, 1}, {2, .5}};
	QVector<ModelWeightStroke> parsed;
	ok &= expect(parseModelWeightStrokeRecipe(modelWeightStrokeRecipeJson({stroke, stroke}), &parsed, &error) &&
					 parsed == QVector<ModelWeightStroke>{stroke, stroke},
				 "the stroke recipe round-trips");
	auto unknown = modelWeightStrokeRecipeJson({stroke});
	unknown.insert(QStringLiteral("extra"), 1);
	ok &= expect(!parseModelWeightStrokeRecipe(unknown, &parsed, &error) && parsed.size() == 2, "a recipe with unknown fields is refused");
	const auto owner = copy(QString(), {});
	ModelSessionDraft weights;
	weights.ownerId = owner;
	weights.baseFingerprint = QByteArray(32, '\x07');
	weights.strokes = {stroke, stroke, stroke};
	ModelSessionDraft curves;
	curves.kind = ModelSessionDraftKind::AnimationCurves;
	curves.ownerId = owner;
	curves.baseFingerprint = weights.baseFingerprint;
	curves.clip = 0;
	curves.frameCount = 2;
	curves.curves.joint = QStringLiteral("tip");
	for (int channel = 0; channel < 9; ++channel)
		curves.curves.channels[channel] = {ModelPoseCurveKey{0, double(channel)}, ModelPoseCurveKey{1, double(channel) + 1}};
	ok &= expect(writeModelSessionDraft(directory, weights, &error) && writeModelSessionDraft(directory, curves, &error), "write sidecars", error);
	const auto headers = inspectModelSessionDrafts(directory, owner);
	ok &= expect(headers.size() == 2 && std::all_of(headers.cbegin(), headers.cend(), [](const ModelSessionDraft &d) { return d.isValid(); }),
				 "both sidecar headers list");
	for (const auto &header : headers)
		ok &= expect(header.kind == ModelSessionDraftKind::WeightPaint ? header.strokeCount == 3 && header.sampleCount == 6 && header.strokes.isEmpty()
																	   : header.keyCount == 18 && header.joint == QStringLiteral("tip"),
					 "headers carry counts without reading the payload");
	ModelSessionDraft read;
	ok &= expect(readModelSessionDraft(modelSessionDraftPath(directory, owner, ModelSessionDraftKind::WeightPaint), &read, &error) &&
					 read.strokes == weights.strokes && read.baseFingerprint == weights.baseFingerprint,
				 "the weight sidecar reads back its strokes and base", error);
	ok &= expect(readModelSessionDraft(modelSessionDraftPath(directory, owner, ModelSessionDraftKind::AnimationCurves), &read, &error) &&
					 read.curves == curves.curves && read.clip == 0 && !modelSessionDraftSummary(read).isEmpty(),
				 "the curve sidecar reads back its curves", error);
	{
		QFile damaged(modelSessionDraftPath(directory, owner, ModelSessionDraftKind::WeightPaint));
		ok &= expect(damaged.open(QIODevice::Append) && damaged.write(" ") == 1, "damage a sidecar");
	}
	ok &= expect(!readModelSessionDraft(modelSessionDraftPath(directory, owner, ModelSessionDraftKind::WeightPaint), &read, &error),
				 "a damaged payload is refused");
	ok &= expect(removeModelRecovery(directory, owner, &error) && inspectModelSessionDrafts(directory, owner).isEmpty(),
				 "discarding a copy discards its sidecars", error);
	// An orphan (its copy gone) goes with retention once its studio has gone.
	auto orphan = weights;
	orphan.ownerId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	ok &= expect(writeModelSessionDraft(directory, orphan, &error), "write an orphaned sidecar", error);
	ok &= expect(pruneModelRecoveries(directory, {}, [](qint64) { return true; }).removedSessions == 0 &&
					 pruneModelRecoveries(directory, {}, [](qint64) { return false; }, {}, true).removedSessions == 1 &&
					 inspectModelSessionDrafts(directory).size() == 1 &&
					 pruneModelRecoveries(directory, {}, [](qint64) { return false; }).removedSessions == 1 &&
					 inspectModelSessionDrafts(directory).isEmpty(),
				 "an orphaned sidecar stays while its studio runs and is pruned after");

	// The CLI reports each copy's source state.
	if (argc > 1)
	{
		ok &= expect(!copy(source(QStringLiteral("unchanged.mesh.json")), [&]
						   {
							   ModelDocument reread;
							   return reread.load(source(QStringLiteral("unchanged.mesh.json")), &error) ? reread.sourceFingerprint() : QByteArray();
						   }())
						  .isEmpty(),
					 "write a copy for the CLI");
		QProcess process;
		process.start(QString::fromLocal8Bit(argv[1]),
					  {QStringLiteral("--cli"), QStringLiteral("--settings-file"), settings, QStringLiteral("model"), QStringLiteral("recoveries"),
					   QStringLiteral("--json")});
		ok &= expect(process.waitForFinished(60000) && process.exitCode() == 0, "model recoveries runs");
		const auto records = QJsonDocument::fromJson(process.readAllStandardOutput()).object().value(QStringLiteral("records")).toArray();
		ok &= expect(records.size() == 1 && records.first().toObject().value(QStringLiteral("sourceState")).toString() == QStringLiteral("unchanged"),
					 "model recoveries --json reports the source state");
	}
	StudioSettings::setOverrideFilePath(QString());
	if (!ok)
	{
		std::cerr << qPrintable(error) << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
