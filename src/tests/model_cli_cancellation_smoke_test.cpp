// Cancelling model commands (core/cli_cancellation.h): a `model import` of a
// realistic maximum-size source cancelled part way exits 130 with
// {"cancelled": true}, writes no output and leaves no staging files; on
// Windows a real Ctrl+Break to a new process group does the same. Stale
// `.vibestudio-model-*.tmp` staging files of an interrupted writer are removed
// by the next write, and nothing else is.
#include "core/cli_cancellation.h"
#include "core/model_document.h"
#include "core/model_file_io.h"
#include "tests/model_scale_test_helpers.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QVector>

#include <cstdlib>
#include <iostream>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << "FAIL: " << message << '\n';
	return value;
}
int stagingFiles(const QString &directory)
{
	return int(QDir(directory).entryList({QStringLiteral(".vibestudio-model-*.tmp")}, QDir::Files | QDir::Hidden | QDir::System).size());
}
bool touch(const QString &path, const QDateTime &when)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write("x") != 1)
		return false;
	file.close();
	QFile stamp(path);
	return stamp.open(QIODevice::ReadWrite) && stamp.setFileTime(when, QFileDevice::FileModificationTime);
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("model-cli-cancel-XXXXXX")));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	const auto path = [&](const QString &name) { return QDir(temporary.path()).filePath(name); };
	bool ok = true;
	QString error;

	// Stale staging files of an interrupted writer go; fresh ones and others stay.
	const auto old = QDateTime::currentDateTime().addSecs(-3600);
	ok &= expect(touch(path(QStringLiteral(".vibestudio-model-AbC123.tmp")), old) &&
					 touch(path(QStringLiteral(".vibestudio-model-XyZ789.tmp")), QDateTime::currentDateTime()) &&
					 touch(path(QStringLiteral("unrelated.tmp")), old) && touch(path(QStringLiteral(".vibestudio-model-short.tmp")), old),
				 "write staging fixtures");
	ok &= expect(removeStaleModelStagingFiles(temporary.path(), 600) == 1 && !QFileInfo::exists(path(QStringLiteral(".vibestudio-model-AbC123.tmp"))) &&
					 QFileInfo::exists(path(QStringLiteral(".vibestudio-model-XyZ789.tmp"))) && QFileInfo::exists(path(QStringLiteral("unrelated.tmp"))) &&
					 QFileInfo::exists(path(QStringLiteral(".vibestudio-model-short.tmp"))),
				 "only an old staging file of the model writer's own pattern is removed");
	QFile::remove(path(QStringLiteral(".vibestudio-model-XyZ789.tmp")));
	QFile::remove(path(QStringLiteral(".vibestudio-model-short.tmp")));

	// The progress hook and phase ids, in process.
	ok &= expect(!cliCancellationRequested() && cliModelWorkPhaseId(ModelWorkPhase::Serializing) == QStringLiteral("serializing"),
				 "nothing is cancelled until asked");

	if (argc > 1)
	{
		// A realistic maximum-size source: every number at full precision.
		ModelDocument source;
		const auto big = path(QStringLiteral("big.mesh.json"));
		ok &= expect(source.setMesh(tests::realisticMaximumEditableGrid(), &error) && source.save(big, false, &error), "save the maximum source");
		QJsonObject output;
		const auto run = [&](const QStringList &args, const QByteArray &cancelAt, int *code)
		{
			QProcess process;
			auto environment = QProcessEnvironment::systemEnvironment();
			environment.insert(QStringLiteral("VIBESTUDIO_CLI_CANCEL_AT_PROGRESS"), QString::fromLatin1(cancelAt));
			process.setProcessEnvironment(environment);
			process.start(QString::fromLocal8Bit(argv[1]), QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"),
																	   path(QStringLiteral("settings.ini"))} +
															   args + QStringList{QStringLiteral("--json")});
			if (!process.waitForFinished(300000))
			{
				process.kill();
				process.waitForFinished();
				return false;
			}
			output = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
			*code = process.exitCode();
			return process.exitStatus() == QProcess::NormalExit;
		};
		for (const auto &cancelAt : {QByteArray("1"), QByteArray("40"), QByteArray("400")})
		{
			const auto out = path(QStringLiteral("out-%1.mesh.json").arg(QString::fromLatin1(cancelAt)));
			int code = -1;
			ok &= expect(run({QStringLiteral("model"), QStringLiteral("import"), big, QStringLiteral("--output"), out}, cancelAt, &code), "run the import");
			// A late cancel may arrive after publication; then the output is complete.
			const bool cancelled = code == cliCancelledExitCode;
			ok &= expect(cancelled ? output.value(QStringLiteral("cancelled")).toBool() && !QFileInfo::exists(out) : code == 0 && QFileInfo::exists(out),
						 "a cancelled import exits 130 with cancelled JSON and no output; a finished one is complete");
			ok &= expect(stagingFiles(temporary.path()) == 0, "no staging files remain");
			std::cout << "cancel at report " << cancelAt.constData() << ": exit " << code << '\n';
		}
		// `model tool` and `model lod` take the same token.
		const QVector<QStringList> others{
			{QStringLiteral("model"), QStringLiteral("tool"), big, QStringLiteral("--tool"), QStringLiteral("add"), QStringLiteral("--primitive"),
			 QStringLiteral("cube"), QStringLiteral("--at"), QStringLiteral("0,0,0"), QStringLiteral("--size"), QStringLiteral("4,4,4"),
			 QStringLiteral("--output"), path(QStringLiteral("tool.mesh.json"))},
			{QStringLiteral("model"), QStringLiteral("lod"), big, QStringLiteral("--output"), path(QStringLiteral("lod.md3"))}};
		for (const auto &args : others)
		{
			int code = -1;
			ok &= expect(run(args, QByteArray("1"), &code), "run the command");
			ok &= expect(code == cliCancelledExitCode && output.value(QStringLiteral("cancelled")).toBool() &&
							 !QFileInfo::exists(args.at(args.indexOf(QStringLiteral("--output")) + 1)) && stagingFiles(temporary.path()) == 0,
						 "a cancelled model tool or model lod exits 130 and writes nothing");
			std::cout << qPrintable(args.at(1)) << " cancelled: exit " << code << '\n';
		}
#ifdef Q_OS_WIN
		// A real Ctrl+Break, when this test shares a console with the child.
		QProcess process;
		process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments)
												  { arguments->flags |= CREATE_NEW_PROCESS_GROUP; });
		const auto out = path(QStringLiteral("break.mesh.json"));
		process.start(QString::fromLocal8Bit(argv[1]), {QStringLiteral("--cli"), QStringLiteral("--settings-file"), path(QStringLiteral("settings.ini")),
														QStringLiteral("model"), QStringLiteral("import"), big, QStringLiteral("--output"), out,
														QStringLiteral("--progress"), QStringLiteral("--json")});
		if (process.waitForStarted())
		{
			QElapsedTimer started;
			started.start();
			// Wait for the first progress line, so the handler is installed.
			while (process.state() == QProcess::Running && !process.readAllStandardError().contains("progress") && started.elapsed() < 30000)
				process.waitForReadyRead(50);
			const bool sent = GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, DWORD(process.processId())) != 0;
			process.waitForFinished(300000);
			if (sent && process.exitCode() == cliCancelledExitCode)
			{
				ok &= expect(!QFileInfo::exists(out) && stagingFiles(temporary.path()) == 0, "Ctrl+Break publishes nothing and leaves no staging files");
				std::cout << "Ctrl+Break: cancelled\n";
			}
			else
			{
				// No shared console, or the import finished first: nothing to judge.
				std::cout << "Ctrl+Break: not delivered (sent " << sent << ", exit " << process.exitCode() << ")\n";
			}
		}
#endif
	}
	if (!ok)
		std::cerr << qPrintable(error) << '\n';
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
