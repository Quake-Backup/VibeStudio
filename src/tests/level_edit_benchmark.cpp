// A developer measurement of how the level document's edits scale with the
// size of the map: one click's worth of work (select, move, undo, add) on a
// large map, and whole-selection edits. Not a CI timing threshold; run it at
// two sizes and compare. Times are this thread's CPU time where the platform
// reports it, so other work on the machine does not distort them.
//
//   level_edit_benchmark [brushes=4000]
#include "core/level_document.h"
#include "core/level_map.h"
#include "tests/level_geometry_test_helpers.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <functional>
#include <iostream>
#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(Q_OS_UNIX)
#include <ctime>
#endif

using namespace vibestudio;

namespace {
#if defined(Q_OS_WIN)
// Cycles this thread ran, scaled by a short calibration against its CPU time.
quint64 threadCycles()
{
	ULONG64 cycles = 0;
	QueryThreadCycleTime(GetCurrentThread(), &cycles);
	return cycles;
}
double cyclesPerMillisecond()
{
	static const double rate = [] {
		const auto cpu100ns = [] {
			FILETIME created, exited, kernel, user;
			GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user);
			return ((quint64(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime) + ((quint64(user.dwHighDateTime) << 32) | user.dwLowDateTime);
		};
		const quint64 cpuStart = cpu100ns();
		const quint64 cycleStart = threadCycles();
		volatile double sink = 0;
		while (cpu100ns() - cpuStart < 3000000) {
			for (int i = 0; i < 100000; ++i) { sink = sink + i * 0.5; }
		}
		return double(threadCycles() - cycleStart) / (double(cpu100ns() - cpuStart) / 1e4);
	}();
	return rate;
}
double cpuMilliseconds() { return double(threadCycles()) / cyclesPerMillisecond(); }
#elif defined(Q_OS_UNIX)
double cpuMilliseconds()
{
	timespec stamp {};
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &stamp);
	return double(stamp.tv_sec) * 1e3 + double(stamp.tv_nsec) / 1e6;
}
#else
// No per-thread clock: wall time, best on an idle machine.
double cpuMilliseconds()
{
	static QElapsedTimer clock = [] { QElapsedTimer started; started.start(); return started; }();
	return clock.nsecsElapsed() / 1e6;
}
#endif

QJsonObject report;
bool ok = true;
QString failure;
void measure(const QString& name, int repeats, const std::function<bool()>& work)
{
	const double before = cpuMilliseconds();
	if (!work() && ok) { ok = false; failure = name; }
	const double spent = cpuMilliseconds() - before;
	report.insert(name, repeats > 1 ? spent / repeats : spent);
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	bool valid = true;
	const int count = argc > 1 ? QString::fromLocal8Bit(argv[1]).toInt(&valid) : 4000;
	if (!valid || count < 100 || count > 100000) { return 2; }
	(void) cpuMilliseconds();
	LevelMapDocument fixture; QString error;
	if (!tests::createGeometryFixture(count, &fixture, &error)) { return 1; }
	const QByteArray bytes = serializeLevelMap(fixture).bytes;
	fixture = {};
	LevelMapDocument map;
	LevelMapLoadRequest request; request.path = QStringLiteral("benchmark.map"); request.engineHint = QStringLiteral("quake3");
	measure(QStringLiteral("loadMs"), 1, [&] { return loadLevelMapBytes(request, bytes, &map, &error); });
	if (!ok) { std::cerr << qPrintable(error) << std::endl; return 1; }
	report.insert(QStringLiteral("brushes"), count);
	report.insert(QStringLiteral("sourceBytes"), bytes.size());
	measure(QStringLiteral("statisticsMs"), 1, [&] { (void) levelMapInspectionSummary(map); return true; });
	measure(QStringLiteral("saveUneditedMs"), 1, [&] { return !serializeLevelMap(map).bytes.isEmpty(); });

	// One object at a time: each should cost about the same on any size of map.
	const int repeats = 50;
	const auto brushAt = [&](int index) { return LevelMapSelectionRef {LevelMapSelectionKind::QuakeBrush, map.brushes.at(index % map.brushes.size()).id}; };
	measure(QStringLiteral("selectOneMs"), repeats, [&] {
		bool done = true;
		for (int i = 0; i < repeats; ++i) { done &= changeLevelMapSelection(&map, {brushAt(i * 7 + 3)}, &error); }
		return done;
	});
	measure(QStringLiteral("moveOneMs"), repeats, [&] {
		bool done = true;
		for (int i = 0; i < repeats; ++i) { done &= moveLevelMapSelection(&map, i % 2 ? 8 : -8, 0, 0, &error); }
		return done;
	});
	measure(QStringLiteral("undoOneMs"), repeats, [&] {
		bool done = true;
		for (int i = 0; i < repeats; ++i) { done &= undoLevelMapEdit(&map, &error); }
		return done;
	});
	measure(QStringLiteral("redoOneMs"), repeats, [&] {
		bool done = true;
		for (int i = 0; i < repeats; ++i) { done &= redoLevelMapEdit(&map, &error); }
		return done;
	});
	measure(QStringLiteral("addEntityMs"), repeats, [&] {
		bool done = true;
		for (int i = 0; i < repeats; ++i) { done &= addLevelMapEntity(&map, QStringLiteral("light"), {double(i * 16), -64, 32, true}, {}, nullptr, &error); }
		return done;
	});
	measure(QStringLiteral("addBrushMs"), repeats, [&] {
		bool done = true;
		for (int i = 0; i < repeats; ++i) {
			done &= addLevelMapBoxBrush(&map, {double(i * 48), -256, 0, true}, {double(i * 48 + 32), -224, 32, true}, QStringLiteral("studio/new"), nullptr, &error);
		}
		return done;
	});

	// The whole map at once: each should grow in step with the map.
	QVector<LevelMapSelectionRef> all;
	measure(QStringLiteral("selectAllMs"), 1, [&] { all = levelMapSelectAllObjects(map, false); return setLevelMapSelection(&map, all, &error); });
	report.insert(QStringLiteral("selected"), levelMapSelectionCount(map));
	measure(QStringLiteral("copyAllMs"), 1, [&] { return !levelMapSelectionText(map, &error).isEmpty(); });
	measure(QStringLiteral("moveAllMs"), 1, [&] { return moveLevelMapSelection(&map, 16, 0, 0, &error); });
	measure(QStringLiteral("undoMoveAllMs"), 1, [&] { return undoLevelMapEdit(&map, &error); });
	measure(QStringLiteral("redoMoveAllMs"), 1, [&] { return redoLevelMapEdit(&map, &error); });
	measure(QStringLiteral("rotateAllMs"), 1, [&] { return rotateLevelMapSelection(&map, 2, 1, &error); });
	measure(QStringLiteral("textureAllMs"), 1, [&] { return applyLevelMapTexture(&map, QStringLiteral("studio/other"), nullptr, &error); });
	measure(QStringLiteral("replaceTextureAllMs"), 1, [&] {
		return replaceLevelMapTexture(&map, QStringLiteral("studio/other"), QStringLiteral("studio/third"), false, nullptr, &error);
	});
	measure(QStringLiteral("duplicateAllMs"), 1, [&] { return duplicateLevelMapSelection(&map, 0, 0, 64, &error); });
	measure(QStringLiteral("deleteCopiesMs"), 1, [&] { return deleteLevelMapSelection(&map, &error); });
	measure(QStringLiteral("undoDeleteMs"), 1, [&] { return undoLevelMapEdit(&map, &error); });
	// Every second brush, so restoring them weaves through the rest of the map.
	QVector<LevelMapSelectionRef> alternate;
	for (int index = 0; index < map.brushes.size(); index += 2) { alternate.push_back(brushAt(index)); }
	measure(QStringLiteral("deleteAlternateMs"), 1, [&] { return deleteLevelMapObjects(&map, alternate, &error); });
	measure(QStringLiteral("undoDeleteAlternateMs"), 1, [&] { return undoLevelMapEdit(&map, &error); });
	measure(QStringLiteral("saveEditedMs"), 1, [&] { return !serializeLevelMap(map).bytes.isEmpty(); });

	report.insert(QStringLiteral("ok"), ok);
	if (!ok) { report.insert(QStringLiteral("failed"), failure); report.insert(QStringLiteral("error"), error); }
	std::cout << QJsonDocument(report).toJson(QJsonDocument::Indented).constData();
	return ok ? 0 : 1;
}
