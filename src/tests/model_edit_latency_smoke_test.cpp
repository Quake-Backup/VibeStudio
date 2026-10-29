// Maximum-size edit evidence: how long representative edits take on the
// largest editable models, how quickly a cancelled edit returns when it is
// cancelled early, half way and late, and how much memory history and the
// process hold. Functional checks always run: a finished edit leaves a valid
// model, a cancelled one leaves the revision, selection and history as they
// were. Timings are evidence for docs, not CI thresholds: budgets apply only
// when VIBESTUDIO_MODELLER_MAX_EDIT_MS / VIBESTUDIO_MODELLER_MAX_CANCEL_MS /
// VIBESTUDIO_MODELLER_MAX_EDIT_GAP_MS are set. By default one fixture and two
// edits run; VIBESTUDIO_MODELLER_FULL_LATENCY=1 runs every fixture and edit.
// VIBESTUDIO_MODELLER_EVIDENCE names a JSON file for the measurements.
#include "app/model_editor_dialog.h"
#include "app/studio_theme.h"
#include "core/model_document.h"
#include "core/model_recovery.h"
#include "core/studio_settings.h"
#include "tests/model_scale_test_helpers.h"
#include "tests/model_skeletal_budget_test_helpers.h"
#include "tests/render_test_support.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <cstdlib>
#include <iostream>

#ifdef Q_OS_WIN
#define PSAPI_VERSION 2
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << "FAIL: " << message << std::endl;
	return value;
}
// The process working set in bytes, or -1 where it cannot be read.
qint64 workingSetBytes()
{
#ifdef Q_OS_WIN
	PROCESS_MEMORY_COUNTERS counters{};
	if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
		return qint64(counters.WorkingSetSize);
	return -1;
#else
	QFile statm(QStringLiteral("/proc/self/statm"));
	if (!statm.open(QIODevice::ReadOnly))
		return -1;
	const auto fields = statm.readAll().split(' ');
	return fields.size() > 1 ? fields.at(1).toLongLong() * 4096 : -1;
#endif
}
ModelEdit editFor(const QString &operation, const ModelMesh &mesh)
{
	ModelEdit edit;
	edit.selection.surface = 0;
	const auto &surface = mesh.surfaces.first();
	if (operation == QStringLiteral("transform-all"))
	{
		edit.kind = ModelEditKind::Transform;
		for (int face = 0; face < surface.triangles.size(); ++face)
			edit.selection.faces.insert(face);
		edit.translation = {0.5f, 0, 1.25f};
	}
	else if (operation == QStringLiteral("normals"))
	{
		edit.kind = ModelEditKind::RecalculateNormals;
		for (int face = 0; face < surface.triangles.size(); ++face)
			edit.selection.faces.insert(face);
	}
	else if (operation == QStringLiteral("delete-faces"))
	{
		edit.kind = ModelEditKind::DeleteFaces;
		for (int face = 0; face < surface.triangles.size(); face += 7)
			edit.selection.faces.insert(face);
	}
	else if (operation == QStringLiteral("extrude"))
	{
		edit.kind = ModelEditKind::Extrude;
		for (int face = 0; face < std::min<int>(surface.triangles.size(), 2048); ++face)
			edit.selection.faces.insert(face);
		edit.translation = {0, 0, 4};
	}
	else if (operation == QStringLiteral("flip-faces"))
	{
		edit.kind = ModelEditKind::FlipFaces;
		for (int face = 0; face < surface.triangles.size(); ++face)
			edit.selection.faces.insert(face);
	}
	return edit;
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("edit-latency-XXXXXX")));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	const bool full = qEnvironmentVariableIntValue("VIBESTUDIO_MODELLER_FULL_LATENCY") != 0;
	const int editBudget = qEnvironmentVariableIntValue("VIBESTUDIO_MODELLER_MAX_EDIT_MS");
	const int cancelBudget = qEnvironmentVariableIntValue("VIBESTUDIO_MODELLER_MAX_CANCEL_MS");
	const int gapBudget = qEnvironmentVariableIntValue("VIBESTUDIO_MODELLER_MAX_EDIT_GAP_MS");
	bool ok = true;
	QString error;
	QJsonArray evidence;

	struct Fixture
	{
		QString name;
		ModelMesh mesh;
		QStringList operations;
	};
	QVector<Fixture> fixtures{{QStringLiteral("integer-grid"), tests::maximumEditableGrid(), {QStringLiteral("transform-all"), QStringLiteral("normals")}}};
	if (full)
	{
		fixtures.first().operations << QStringLiteral("delete-faces") << QStringLiteral("extrude") << QStringLiteral("flip-faces");
		fixtures.append({QStringLiteral("realistic-grid"), tests::realisticMaximumEditableGrid(),
						 {QStringLiteral("transform-all"), QStringLiteral("normals"), QStringLiteral("delete-faces"), QStringLiteral("flip-faces")}});
		fixtures.append({QStringLiteral("skeletal"), tests::realisticSkeletalDocument(), {QStringLiteral("transform-all"), QStringLiteral("normals")}});
	}

	for (const auto &fixture : std::as_const(fixtures))
	{
		ModelDocument document;
		QElapsedTimer clock;
		clock.start();
		ok &= expect(document.setMesh(fixture.mesh, &error), "the maximum fixture is a valid document");
		const double setMs = clock.nsecsElapsed() / 1e6;
		for (const auto &operation : fixture.operations)
		{
			const auto edit = editFor(operation, document.mesh());
			// The full edit, counting its progress reports.
			auto probe = document;
			qint64 reports = 0;
			ModelWorkControl counting;
			counting.progress = [&](ModelWorkPhase, qint64, qint64) { ++reports; };
			clock.restart();
			const bool finished = probe.edit(edit, &error, counting);
			const double completeMs = clock.nsecsElapsed() / 1e6;
			ok &= expect(finished && validateEditableModel(probe.mesh()).isEmpty(), "a maximum-size edit finishes with a valid model");
			// Cancelled early, half way and late: how long until edit() returns,
			// and the document is exactly as it was.
			QJsonArray cancels;
			for (const double fraction : {0.0, 0.5, 0.9})
			{
				auto candidate = document;
				const auto revision = candidate.revisionFingerprint();
				const auto selection = candidate.selection();
				const int undo = candidate.undoSteps();
				const qint64 trigger = std::max<qint64>(1, qint64(reports * fraction));
				qint64 seen = 0;
				bool requested = false;
				QElapsedTimer sinceCancel;
				ModelWorkControl control;
				control.progress = [&](ModelWorkPhase, qint64, qint64)
				{
					if (!requested && ++seen >= trigger)
					{
						requested = true;
						sinceCancel.start();
					}
				};
				control.cancelled = [&] { return requested; };
				const bool result = candidate.edit(edit, &error, control);
				const double cancelMs = requested ? sinceCancel.nsecsElapsed() / 1e6 : -1;
				ok &= expect(!result || !requested, "a cancelled edit reports that it did not finish");
				ok &= expect(result || (candidate.revisionFingerprint() == revision && candidate.selection() == selection && candidate.undoSteps() == undo),
							 "a cancelled edit leaves revision, selection and history unchanged");
				if (cancelBudget > 0 && cancelMs > cancelBudget)
				{
					std::cerr << "cancel budget exceeded: " << qPrintable(fixture.name) << ' ' << qPrintable(operation) << ' ' << cancelMs << " ms\n";
					ok = false;
				}
				cancels.append(QJsonObject{{QStringLiteral("at"), fraction}, {QStringLiteral("ms"), cancelMs}, {QStringLiteral("finished"), result}});
			}
			// Adopt the edit so history grows as an editing session's would.
			ok &= expect(document.edit(edit, &error), "the edit applies to the session document");
			if (editBudget > 0 && completeMs > editBudget)
			{
				std::cerr << "edit budget exceeded: " << qPrintable(fixture.name) << ' ' << qPrintable(operation) << ' ' << completeMs << " ms\n";
				ok = false;
			}
			evidence.append(QJsonObject{{QStringLiteral("fixture"), fixture.name},
										{QStringLiteral("operation"), operation},
										{QStringLiteral("vertices"), document.mesh().vertexCount},
										{QStringLiteral("triangles"), document.mesh().triangleCount},
										{QStringLiteral("frames"), document.mesh().frameCount},
										{QStringLiteral("setMeshMs"), setMs},
										{QStringLiteral("completeMs"), completeMs},
										{QStringLiteral("progressReports"), reports},
										{QStringLiteral("cancel"), cancels},
										{QStringLiteral("historyBytes"), document.historyBytes()},
										{QStringLiteral("undoSteps"), document.undoSteps()},
										{QStringLiteral("workingSetBytes"), workingSetBytes()}});
		}
		// Save and a recovery copy at this size.
		const auto sourcePath = QDir(temporary.path()).filePath(fixture.name + QStringLiteral(".mesh.json"));
		clock.restart();
		ok &= expect(document.save(sourcePath, true, &error), "the maximum document saves");
		const double saveMs = clock.nsecsElapsed() / 1e6;
		ModelRecoverySnapshot snapshot;
		snapshot.mesh = document.mesh();
		snapshot.selection = document.selection();
		clock.restart();
		const auto copy = writeModelRecovery(snapshot, QDir(temporary.path()).filePath(QStringLiteral("recovery")),
											 QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
		const double recoveryMs = clock.nsecsElapsed() / 1e6;
		ok &= expect(!copy.isEmpty(), "a recovery copy of the maximum document is written");
		evidence.append(QJsonObject{{QStringLiteral("fixture"), fixture.name},
									{QStringLiteral("operation"), QStringLiteral("save-and-recovery")},
									{QStringLiteral("saveMs"), saveMs},
									{QStringLiteral("recoveryMs"), recoveryMs},
									{QStringLiteral("sourceBytes"), QFileInfo(sourcePath).size()},
									{QStringLiteral("workingSetBytes"), workingSetBytes()}});
	}

	// The editor's worker path: the largest event-loop gap while a maximum
	// edit runs, and how quickly Cancel stops it.
	if (vibestudio::test_support::exitCodeWithoutRenderer("model-edit-latency-smoke") < 0)
	{
		applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
		ModelEditorDialog editor;
		editor.resize(1400, 900);
		editor.show();
		app.processEvents();
		ok &= expect(editor.setMesh(tests::maximumEditableGrid(), &error), "the editor opens the maximum grid");
		const auto edit = editFor(QStringLiteral("transform-all"), editor.document().mesh());
		QElapsedTimer beat;
		qint64 largestGap = 0;
		QTimer heartbeat;
		heartbeat.setInterval(5);
		QObject::connect(&heartbeat, &QTimer::timeout, &editor,
						 [&]
						 {
							 if (beat.isValid())
								 largestGap = std::max(largestGap, beat.restart());
							 else
								 beat.start();
						 });
		heartbeat.start();
		QElapsedTimer clock;
		clock.start();
		ok &= expect(editor.applyEdit(edit, &error), "the worker applies the maximum edit");
		const double workerMs = clock.nsecsElapsed() / 1e6;
		const auto revision = editor.document().revisionFingerprint();
		QElapsedTimer sinceCancel;
		QTimer::singleShot(30, &editor,
						   [&]
						   {
							   sinceCancel.start();
							   editor.cancelOperation();
						   });
		const bool cancelledResult = editor.applyEdit(edit, &error);
		const double cancelMs = sinceCancel.isValid() ? sinceCancel.nsecsElapsed() / 1e6 : -1;
		heartbeat.stop();
		ok &= expect(cancelledResult || cancelMs < 0 || editor.document().revisionFingerprint() != revision,
					 "Cancel stops the worker edit or the edit finished first");
		ok &= expect(cancelledResult || editor.document().revisionFingerprint() == revision, "a cancelled worker edit changes nothing");
		if (gapBudget > 0 && largestGap > gapBudget)
		{
			std::cerr << "event-loop gap budget exceeded: " << largestGap << " ms\n";
			ok = false;
		}
		evidence.append(QJsonObject{{QStringLiteral("fixture"), QStringLiteral("integer-grid")},
									{QStringLiteral("operation"), QStringLiteral("editor-worker-transform-all")},
									{QStringLiteral("completeMs"), workerMs},
									{QStringLiteral("largestEventLoopGapMs"), largestGap},
									{QStringLiteral("cancelMs"), cancelMs},
									{QStringLiteral("cancelled"), !cancelledResult},
									{QStringLiteral("historyBytes"), editor.document().historyBytes()},
									{QStringLiteral("workingSetBytes"), workingSetBytes()}});
		editor.hide();
	}

	const auto json = QJsonDocument(QJsonObject{{QStringLiteral("evidence"), evidence}, {QStringLiteral("full"), full}}).toJson(QJsonDocument::Compact);
	std::cout << "EVIDENCE " << json.constData() << std::endl;
	if (const auto path = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE"); !path.isEmpty())
	{
		QFile file(path);
		ok &= expect(file.open(QIODevice::WriteOnly) && file.write(json) == json.size(), "write the evidence file");
	}
	StudioSettings::setOverrideFilePath(QString());
	if (!ok)
		std::cerr << qPrintable(error) << '\n';
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
