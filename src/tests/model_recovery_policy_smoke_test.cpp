// Recovery copies against their sources (unchanged, changed, missing, no
// source), the retention policy (oldest first, never a live owner, dry runs
// remove nothing), the startup inventory, and `model recoveries --json`
// reporting each copy's source state.
#include "app/model_recovery_discovery.h"
#include "core/model_design.h"
#include "core/model_recovery.h"
#include "core/model_recovery_policy.h"
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
