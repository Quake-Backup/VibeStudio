// Malformed-input coverage for the legacy decoders and the decode -> edit ->
// export chain (FMT-10).
//
// - Every-byte truncation and seeded flips of rich MDL, MD2 and MD3 seeds
//   (tests/model_legacy_fuzz_seeds.h): skin and frame groups, onseam
//   vertices, strip and fan GL commands, two surfaces with tags.
// - OBJ line and token mutations: index sign and overflow, corner counts,
//   continuation lines, a byte-order mark, numeric extremes, plus byte flips.
// - The chain: each mutated input that decodes and passes editable validation
//   (omissions accepted) goes through every export id; each export must either
//   refuse with an error or write bytes that decode again without error and
//   with consistent counts.
//
// Nothing is random at run time: the corpus comes from kDefaultSeed unless
// VIBESTUDIO_FUZZ_SEED overrides it, and a failure prints the seed and the
// case so it can be re-run (the parser-fuzz convention). The run stops adding
// cases after kTimeBudgetMs so CI stays bounded on slow machines.
#include "core/model_decode_issues.h"
#include "core/model_document.h"
#include "core/model_mesh.h"
#include "core/model_obj.h"
#include "core/parser_fuzz.h"
#include "tests/model_legacy_fuzz_seeds.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLoggingCategory>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <iterator>

using namespace vibestudio;
using namespace vibestudio::tests;

namespace
{
constexpr quint64 kDefaultSeed = 0x46d7a10c3be20010ull;
constexpr const char *kSeedVariable = "VIBESTUDIO_FUZZ_SEED";
constexpr int kFlipCasesPerSeed = 192;
constexpr int kObjCases = 384;
constexpr qint64 kTimeBudgetMs = 90000;

int g_checks = 0;
int g_failures = 0;
int g_chained = 0;
int g_exports = 0;
quint64 g_seed = kDefaultSeed;
QElapsedTimer g_clock;

void silence(QtMsgType, const QMessageLogContext &, const QString &) {}

void report(const char *target, const QString &caseId, const QString &problem)
{
	++g_failures;
	if (g_failures <= 8)
	{
		std::cerr << "fuzz target " << target << " failed\n"
				  << "  re-run:  " << kSeedVariable << "=0x" << std::hex << g_seed << std::dec << "\n"
				  << "  case:    " << qUtf8Printable(caseId) << "\n"
				  << "  problem: " << qUtf8Printable(problem) << "\n";
	}
}

bool overBudget()
{
	return g_clock.elapsed() > kTimeBudgetMs;
}

// What any decode must uphold, accepted or not.
QString checkDecode(const ModelMesh &mesh, qsizetype inputSize)
{
	if (!mesh.error.isEmpty())
	{
		return mesh.geometryAvailable || !mesh.surfaces.isEmpty() ? QStringLiteral("a refused decode published geometry") : QString();
	}
	if (!mesh.isValid() && !mesh.geometryAvailable && mesh.frames.isEmpty())
	{
		return QStringLiteral("a decode without an error produced nothing");
	}
	if (mesh.surfaces.size() != mesh.surfaceCount || mesh.frames.size() != mesh.frameCount)
	{
		return QStringLiteral("published counts disagree with the published lists");
	}
	int triangles = 0;
	for (const ModelSurface &surface : mesh.surfaces)
	{
		triangles += int(surface.triangles.size());
		if (surface.texCoords.size() != surface.vertexCount || surface.frames.size() != mesh.frames.size())
			return QStringLiteral("surface %1 has %2 UVs for %3 vertices or %4 poses for %5 frames")
				.arg(surface.name)
				.arg(surface.texCoords.size())
				.arg(surface.vertexCount)
				.arg(surface.frames.size())
				.arg(mesh.frames.size());
		for (const ModelTriangle &t : surface.triangles)
		{
			if (t.a < 0 || t.b < 0 || t.c < 0 || t.a >= surface.vertexCount || t.b >= surface.vertexCount || t.c >= surface.vertexCount)
				return QStringLiteral("surface %1 publishes a triangle index outside its vertices").arg(surface.name);
		}
		for (const ModelFrameGeometry &pose : surface.frames)
		{
			if (pose.positions.size() != surface.vertexCount)
				return QStringLiteral("surface %1 has a pose with the wrong vertex count").arg(surface.name);
		}
	}
	if (triangles != mesh.triangleCount)
		return QStringLiteral("triangleCount %1 disagrees with %2 published triangles").arg(mesh.triangleCount).arg(triangles);
	if (qint64(mesh.surfaces.size()) > inputSize || qint64(mesh.frames.size()) > inputSize)
		return QStringLiteral("more records than the %1 byte input could hold").arg(inputSize);
	const auto issues = modelDecodeIssues(mesh);
	if (issues.size() < mesh.warnings.size())
		return QStringLiteral("a warning has no classified issue");
	return {};
}

// The decode -> edit -> export chain for one accepted decode.
QString checkChain(const ModelMesh &decoded)
{
	if (!decoded.error.isEmpty() || !decoded.geometryAvailable)
		return {};
	ModelMesh mesh = decoded;
	acceptModelDecodeOmissions(&mesh);
	if (!validateEditableModel(mesh).isEmpty())
		return {};
	updateEditableModelMetadata(&mesh);
	++g_chained;
	for (const QString &format : modelExportFormatIds())
	{
		QString error;
		const QByteArray bytes = exportEditableModel(mesh, format, 0, &error);
		++g_exports;
		++g_checks;
		if (bytes.isEmpty())
		{
			if (error.isEmpty())
				return QStringLiteral("%1 export wrote nothing without an error").arg(format);
			continue;
		}
		const ModelMesh back = decodeModelMesh(QStringLiteral("chain/out.") + format, bytes);
		if (!back.error.isEmpty())
			return QStringLiteral("%1 export does not decode again: %2").arg(format, back.error);
		const QString problem = checkDecode(back, bytes.size());
		if (!problem.isEmpty())
			return QStringLiteral("%1 export decodes inconsistently: %2").arg(format, problem);
		if (format == QStringLiteral("md5anim"))
			continue;
		if (!back.geometryAvailable || back.triangleCount <= 0 || back.frames.isEmpty())
			return QStringLiteral("%1 export decodes without geometry").arg(format);
		if (format != QStringLiteral("obj") && format != QStringLiteral("ase") && back.triangleCount > mesh.triangleCount)
			return QStringLiteral("%1 export gained triangles (%2 from %3)").arg(format).arg(back.triangleCount).arg(mesh.triangleCount);
	}
	return {};
}

void runCase(const char *target, const QString &caseId, const QString &path, const QByteArray &bytes, bool chain)
{
	++g_checks;
	const ModelMesh mesh = decodeModelMesh(path, bytes);
	QString problem = checkDecode(mesh, bytes.size());
	if (problem.isEmpty() && chain)
		problem = checkChain(mesh);
	if (!problem.isEmpty())
		report(target, caseId, problem);
}

void fuzzSeed(const LegacyModelSeed &seed)
{
	const QString path = QString::fromLatin1(seed.path);
	const ModelMesh clean = decodeModelMesh(path, seed.bytes);
	++g_checks;
	if (!clean.error.isEmpty() || !clean.geometryAvailable)
	{
		report(seed.name, QStringLiteral("seed"), QStringLiteral("the seed itself does not decode: %1").arg(clean.error));
		return;
	}
	if (!modelDecodeBlockingIssues(clean).isEmpty())
		report(seed.name, QStringLiteral("seed"), QStringLiteral("the seed decodes with damage: %1").arg(modelDecodeBlockingIssues(clean).join(QLatin1Char(' '))));
	const QString chained = checkChain(clean);
	if (!chained.isEmpty())
		report(seed.name, QStringLiteral("seed-chain"), chained);
	// Every truncation, as the newer decoders' suites do.
	for (qsizetype size = 0; size < seed.bytes.size() && !overBudget(); ++size)
		runCase(seed.name, QStringLiteral("truncate-%1").arg(size), path, seed.bytes.left(size), false);
	// Seeded corruption, with the chain on everything that still decodes.
	const quint64 corpusSeed = g_seed ^ qHash(QByteArray(seed.name));
	for (const FuzzCase &generated : buildFuzzCorpus({seed.bytes}, corpusSeed, kFlipCasesPerSeed))
	{
		if (overBudget())
			break;
		runCase(seed.name, generated.id, path, generated.data, true);
	}
}

// OBJ text mutations: the importer is the main artist entry point.
QByteArray objSeed()
{
	return QByteArrayLiteral("# rich OBJ seed\n"
							 "v 0 0 0\nv 32 0 0\nv 32 32 0\nv 0 32 0\nv 16 16 24\n"
							 "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvt 0.5 0.5\n"
							 "vn 0 0 -1\nvn 0 0.6 0.8\n"
							 "g base\nusemtl models/test/base.tga\n"
							 "f 1/1/1 4/4/1 3/3/1 2/2/1\n"
							 "g cap\nusemtl models/test/cap.tga\ns 1\n"
							 "f -5/-5/-1 -4/-4/-1 \\\n -1/-1/-1\n"
							 "f 2/2 3/3 5/5\nf 3 4 5\nf 4//2 1//2 5//2\n");
}

QByteArray mutateObj(const QByteArray &seed, FuzzRandom *random, QString *operation)
{
	QList<QByteArray> lines = seed.split('\n');
	const int line = int(random->bounded(quint64(lines.size())));
	QList<QByteArray> tokens = lines[line].split(' ');
	static const QByteArray extremes[] = {"-0", "0", "-1", "-2147483648", "2147483647", "2147483648", "99999999999999999999", "1e309", "-1e309",
										 "nan", "inf", "-inf", "1e-320", "0x10", "1/", "/1", "1//", "//", "1/2/3/4", "+3", "3.5", ""};
	switch (random->bounded(9))
	{
	case 0: // index or number extreme
	{
		const int token = int(random->bounded(quint64(tokens.size())));
		const QByteArray value = extremes[random->bounded(std::size(extremes))];
		*operation = QStringLiteral("token-%1-%2").arg(line).arg(QString::fromLatin1(value));
		tokens[token] = value;
		lines[line] = tokens.join(' ');
		break;
	}
	case 1: // index sign
	{
		const int token = int(random->bounded(quint64(tokens.size())));
		*operation = QStringLiteral("sign-%1-%2").arg(line).arg(token);
		tokens[token] = tokens[token].startsWith('-') ? tokens[token].mid(1) : QByteArray("-") + tokens[token];
		lines[line] = tokens.join(' ');
		break;
	}
	case 2: // drop a corner
		*operation = QStringLiteral("drop-corner-%1").arg(line);
		if (tokens.size() > 1)
			tokens.removeLast();
		lines[line] = tokens.join(' ');
		break;
	case 3: // repeat corners
		*operation = QStringLiteral("repeat-corners-%1").arg(line);
		lines[line] = tokens.join(' ') + ' ' + tokens.mid(1).join(' ');
		break;
	case 4: // continuation
		*operation = QStringLiteral("continue-%1").arg(line);
		lines[line] += " \\";
		break;
	case 5: // byte-order mark
		*operation = QStringLiteral("bom");
		lines[0].prepend("\xEF\xBB\xBF");
		break;
	case 6: // duplicate or delete a line
		*operation = QStringLiteral("line-%1").arg(line);
		if (random->bounded(2))
			lines.insert(line, lines[line]);
		else
			lines.removeAt(line);
		break;
	case 7: // carriage returns and tabs
		*operation = QStringLiteral("whitespace-%1").arg(line);
		lines[line].replace(' ', random->bounded(2) ? QByteArray("\t") : QByteArray(" \r "));
		break;
	default: // byte flip
	{
		QByteArray bytes = lines.join('\n');
		const auto offset = qsizetype(random->bounded(quint64(bytes.size())));
		const auto bit = int(random->bounded(8));
		*operation = QStringLiteral("flip-%1-%2").arg(offset).arg(bit);
		bytes[offset] = char(bytes[offset] ^ (1 << bit));
		return bytes;
	}
	}
	return lines.join('\n');
}

void fuzzObj()
{
	const QByteArray seed = objSeed();
	const ModelMesh clean = decodeModelMesh(QStringLiteral("models/test/rich.obj"), seed);
	++g_checks;
	if (!clean.error.isEmpty() || !clean.geometryAvailable)
	{
		report("obj", QStringLiteral("seed"), QStringLiteral("the OBJ seed does not decode: %1").arg(clean.error));
		return;
	}
	const QString chained = checkChain(clean);
	if (!chained.isEmpty())
		report("obj", QStringLiteral("seed-chain"), chained);
	FuzzRandom random(g_seed ^ 0x6f626aull);
	for (int index = 0; index < kObjCases && !overBudget(); ++index)
	{
		QByteArray bytes = seed;
		QString operations;
		// One to three mutations per case.
		const int steps = 1 + int(random.bounded(3));
		for (int step = 0; step < steps; ++step)
		{
			QString operation;
			bytes = mutateObj(bytes, &random, &operation);
			operations += (step ? QStringLiteral("+") : QString()) + operation;
		}
		runCase("obj", QStringLiteral("%1:%2").arg(index).arg(operations), QStringLiteral("models/test/rich.obj"), bytes, true);
	}
	for (qsizetype size = 0; size < seed.size() && !overBudget(); ++size)
		runCase("obj", QStringLiteral("truncate-%1").arg(size), QStringLiteral("models/test/rich.obj"), seed.left(size), false);
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	g_clock.start();
	bool parsed = false;
	const QByteArray override = qgetenv(kSeedVariable);
	if (!override.isEmpty())
	{
		g_seed = override.toULongLong(&parsed, 0);
		if (!parsed)
		{
			std::cerr << kSeedVariable << " must be a number such as 0x46d7a10c3be20010\n";
			return EXIT_FAILURE;
		}
	}
	qInstallMessageHandler(silence);
	for (const LegacyModelSeed &seed : richLegacyModelSeeds())
		fuzzSeed(seed);
	fuzzObj();
	qInstallMessageHandler(nullptr);
	if (g_chained == 0 || g_exports == 0)
	{
		std::cerr << "the chain never ran: no mutated input reached the writers\n";
		++g_failures;
	}
	std::cout << "model_format_legacy_fuzz_smoke_test: seed 0x" << std::hex << g_seed << std::dec << ", " << g_checks << " checks, "
			  << g_chained << " chained models, " << g_exports << " exports, " << g_clock.elapsed() << " ms"
			  << (overBudget() ? " (time budget reached)" : "") << ", " << g_failures << " failure(s)\n";
	return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
