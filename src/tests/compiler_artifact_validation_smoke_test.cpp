#include "core/compiler_artifact_validation.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	return file.write(bytes) == bytes.size();
}

QByteArray minimalQuakeBsp()
{
	QByteArray bytes(4 + 15 * 8, '\0');
	qToLittleEndian<qint32>(29, reinterpret_cast<uchar*>(bytes.data()));
	return bytes;
}

QByteArray minimalQuake3Bsp()
{
	QByteArray bytes(8 + 17 * 8, '\0');
	bytes[0] = 'I';
	bytes[1] = 'B';
	bytes[2] = 'S';
	bytes[3] = 'P';
	qToLittleEndian<qint32>(46, reinterpret_cast<uchar*>(bytes.data() + 4));
	return bytes;
}

QByteArray minimalQbismBsp()
{
	QByteArray bytes(8 + 19 * 8, '\0');
	bytes[0] = 'Q';
	bytes[1] = 'B';
	bytes[2] = 'S';
	bytes[3] = 'P';
	qToLittleEndian<qint32>(38, reinterpret_cast<uchar*>(bytes.data() + 4));
	return bytes;
}

QByteArray hexen2AlignedQuakeHeader()
{
	QByteArray bytes = minimalQuakeBsp();
	const int modelOffset = bytes.size();
	bytes.resize(bytes.size() + 80);
	qToLittleEndian<quint32>(modelOffset, reinterpret_cast<uchar*>(bytes.data() + 4 + 14 * 8));
	qToLittleEndian<quint32>(80, reinterpret_cast<uchar*>(bytes.data() + 4 + 14 * 8 + 4));
	return bytes;
}

slopstudio::CompilerCommandManifest manifestForOutput(const QString& path, const QString& profileId = QStringLiteral("slopmap2-bsp"))
{
	slopstudio::CompilerCommandManifest manifest;
	manifest.profileId = profileId;
	manifest.toolId = profileId.startsWith(QStringLiteral("slopmap3")) ? QStringLiteral("slopmap3") : profileId;
	manifest.engineFamily = profileId.startsWith(QStringLiteral("slopmap3")) ? QStringLiteral("idTech3") : QStringLiteral("idTech2");
	manifest.expectedOutputPaths = {path};
	return manifest;
}

bool containsText(const QStringList& values, const QString& text)
{
	return values.join('\n').contains(text, Qt::CaseInsensitive);
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected temporary directory.");
	}
	QDir root(tempDir.path());

	const QString missingPath = root.filePath(QStringLiteral("maps/convert.bsp"));
	if (!root.mkpath(QStringLiteral("maps")) || !writeFile(root.filePath(QStringLiteral("maps/convert-valve.bsp")), minimalQuakeBsp())) {
		return fail("Expected alternate output fixture.");
	}
	slopstudio::CompilerCommandManifest missingManifest = manifestForOutput(missingPath);
	missingManifest.arguments = {QStringLiteral("-convert"), QStringLiteral("valve")};
	const slopstudio::CompilerArtifactValidationReport missingReport = slopstudio::validateCompilerArtifacts(missingManifest);
	if (!missingReport.hasErrors() || !containsText(missingReport.warnings, QStringLiteral("#213"))) {
		return fail("Expected missing conversion output to report an error and #213 naming warning.");
	}

	const QString emptyPath = root.filePath(QStringLiteral("maps/empty.bsp"));
	if (!writeFile(emptyPath, QByteArray())) {
		return fail("Expected empty BSP fixture.");
	}
	const slopstudio::CompilerArtifactValidationReport emptyReport = slopstudio::validateCompilerArtifacts(manifestForOutput(emptyPath));
	if (!emptyReport.hasErrors() || !containsText(emptyReport.errors, QStringLiteral("empty"))) {
		return fail("Expected empty BSP output error.");
	}

	const QString q3Path = root.filePath(QStringLiteral("maps/q3.bsp"));
	if (!writeFile(q3Path, minimalQuake3Bsp())) {
		return fail("Expected Q3 BSP fixture.");
	}
	const slopstudio::CompilerArtifactValidationReport q3Report = slopstudio::validateCompilerArtifacts(manifestForOutput(q3Path, QStringLiteral("slopmap3-bsp")));
	if (q3Report.hasErrors()) {
		return fail("Expected minimal Quake III BSP header to validate for SlopMap3 profile.");
	}
	// A v46 payload begins after 17 lumps, not after the Quake Live extension.
	QByteArray q3Entities = minimalQuake3Bsp();
	const QByteArray entities = QByteArray("{\n\"classname\" \"worldspawn\"\n}\n") + '\0';
	qToLittleEndian<qint32>(q3Entities.size(), reinterpret_cast<uchar*>(q3Entities.data() + 8));
	qToLittleEndian<qint32>(entities.size(), reinterpret_cast<uchar*>(q3Entities.data() + 12));
	q3Entities += entities;
	if (!writeFile(q3Path, q3Entities) ||
		slopstudio::validateCompilerArtifacts(manifestForOutput(q3Path, QStringLiteral("slopmap3-bsp"))).hasErrors()) {
		return fail("Expected Quake III entity bytes immediately after its 17-lump header to validate.");
	}
	const slopstudio::CompilerArtifactValidationReport wrongProfileReport = slopstudio::validateCompilerArtifacts(manifestForOutput(q3Path, QStringLiteral("slopmap2-bsp")));
	if (!containsText(wrongProfileReport.warnings, QStringLiteral("#278"))) {
		return fail("Expected rough BSP family mismatch warning for SlopMap2 profile.");
	}

	// "-qbism" is a real qbsp target flag; substring matching on "q2bsp"/"quake2" used to miss it
	// and flag valid Qbism output as a family mismatch (ericw-tools qbsp/qbsp.cc).
	const QString qbismPath = root.filePath(QStringLiteral("maps/qbism.bsp"));
	if (!writeFile(qbismPath, minimalQbismBsp())) {
		return fail("Expected Qbism BSP fixture.");
	}
	slopstudio::CompilerCommandManifest qbismManifest = manifestForOutput(qbismPath);
	qbismManifest.arguments = {QStringLiteral("-qbism")};
	const slopstudio::CompilerArtifactValidationReport qbismReport = slopstudio::validateCompilerArtifacts(qbismManifest);
	if (qbismReport.hasErrors() || containsText(qbismReport.warnings, QStringLiteral("does not match the selected compiler profile"))) {
		return fail("Expected qbsp -qbism output to validate as a Quake II family BSP.");
	}
	slopstudio::CompilerCommandManifest qbismWithoutFlagManifest = manifestForOutput(qbismPath);
	if (!containsText(slopstudio::validateCompilerArtifacts(qbismWithoutFlagManifest).warnings, QStringLiteral("does not match the selected compiler profile"))) {
		return fail("Expected Qbism output without a Quake II target flag to still be flagged.");
	}
	const QString q2Path = root.filePath(QStringLiteral("maps/q2.bsp"));
	QByteArray q2 = minimalQbismBsp();
	q2.replace(0, 4, "IBSP");
	if (!writeFile(q2Path, q2)) {
		return fail("Expected Quake II BSP fixture.");
	}
	for (const auto& tool : QStringList{QStringLiteral("slopmap2-vis"), QStringLiteral("slopmap2-light")}) {
		for (const auto& path : QStringList{q2Path, qbismPath}) {
			const auto report = slopstudio::validateCompilerArtifacts(manifestForOutput(path, tool));
			if (report.hasErrors() || !report.warnings.isEmpty()) {
				return fail("Expected SlopMap2 VIS/LIGHT to preserve Quake II and Qbism without QBSP target flags.");
			}
		}
		if (!containsText(slopstudio::validateCompilerArtifacts(manifestForOutput(q3Path, tool)).warnings,
						  QStringLiteral("does not match the selected compiler profile"))) {
			return fail("Expected SlopMap2 VIS/LIGHT to still reject Quake III profile expectations.");
		}
	}
	qToLittleEndian<qint32>(999, reinterpret_cast<uchar*>(q2.data() + 4));
	if (!writeFile(q2Path, q2) ||
		!slopstudio::validateCompilerArtifacts(manifestForOutput(q2Path, QStringLiteral("slopmap2-light"))).hasErrors()) {
		return fail("Expected an unknown IBSP version to fail instead of being treated as Quake II.");
	}

	// "-convert quake2" is a conversion value, not a Quake II target flag.
	const QString convertedPath = root.filePath(QStringLiteral("maps/converted.bsp"));
	if (!writeFile(convertedPath, minimalQuakeBsp())) {
		return fail("Expected converted BSP fixture.");
	}
	slopstudio::CompilerCommandManifest convertedManifest = manifestForOutput(convertedPath);
	convertedManifest.arguments = {QStringLiteral("-convert"), QStringLiteral("quake2")};
	if (containsText(slopstudio::validateCompilerArtifacts(convertedManifest).warnings, QStringLiteral("does not match the selected compiler profile"))) {
		return fail("Expected a -convert value not to be mistaken for a Quake II target flag.");
	}

	const QString hexenPath = root.filePath(QStringLiteral("maps/hexenish.bsp"));
	if (!writeFile(hexenPath, hexen2AlignedQuakeHeader())) {
		return fail("Expected Hexen2-style BSP fixture.");
	}
	const slopstudio::CompilerArtifactValidationReport hexenReport = slopstudio::validateCompilerArtifacts(manifestForOutput(hexenPath));
	if (!containsText(hexenReport.warnings, QStringLiteral("#278"))) {
		return fail("Expected Hexen2-style model layout warning linked to #278.");
	}
	// "-hexen2" is the real flag; "h2bsp" is not an ericw-tools option at all.
	slopstudio::CompilerCommandManifest hexenFlagManifest = manifestForOutput(hexenPath);
	hexenFlagManifest.arguments = {QStringLiteral("-hexen2")};
	if (containsText(slopstudio::validateCompilerArtifacts(hexenFlagManifest).warnings, QStringLiteral("Hexen II"))) {
		return fail("Expected an explicit -hexen2 request to suppress the Hexen II layout warning.");
	}
	slopstudio::CompilerCommandManifest fakeHexenFlagManifest = manifestForOutput(hexenPath);
	fakeHexenFlagManifest.arguments = {QStringLiteral("-h2bsp")};
	if (!containsText(slopstudio::validateCompilerArtifacts(fakeHexenFlagManifest).warnings, QStringLiteral("Hexen II"))) {
		return fail("Expected a non-existent h2bsp flag not to suppress the Hexen II layout warning.");
	}

	const QString bspxPath = root.filePath(QStringLiteral("maps/light.bsp"));
	if (!writeFile(bspxPath, minimalQuakeBsp())) {
		return fail("Expected BSPX metadata gap fixture.");
	}
	slopstudio::CompilerCommandManifest bspxManifest = manifestForOutput(bspxPath, QStringLiteral("slopmap2-light"));
	bspxManifest.arguments = {QStringLiteral("-lmshift"), QStringLiteral("3"), QStringLiteral("-world_units_per_luxel"), QStringLiteral("8")};
	const slopstudio::CompilerArtifactValidationReport bspxReport = slopstudio::validateCompilerArtifacts(bspxManifest);
	if (!containsText(bspxReport.warnings, QStringLiteral("#309")) || !containsText(bspxReport.warnings, QStringLiteral("#399")) || !containsText(bspxReport.warnings, QStringLiteral("#415")) || !containsText(bspxReport.warnings, QStringLiteral("#249"))) {
		return fail("Expected missing BSPX lightmap metadata warnings linked to #309/#399/#415/#249.");
	}

	// "-bspx" is a bare flag; the old needle "-bspx " (with a trailing space) matched no real option.
	const QString bspxFlagPath = root.filePath(QStringLiteral("maps/bspxflag.bsp"));
	if (!writeFile(bspxFlagPath, minimalQuakeBsp())) {
		return fail("Expected bare -bspx fixture.");
	}
	slopstudio::CompilerCommandManifest bspxFlagManifest = manifestForOutput(bspxFlagPath, QStringLiteral("slopmap2-light"));
	bspxFlagManifest.arguments = {QStringLiteral("-bspx")};
	const slopstudio::CompilerArtifactValidationReport bspxFlagReport = slopstudio::validateCompilerArtifacts(bspxFlagManifest);
	if (!containsText(bspxFlagReport.warnings, QStringLiteral("RGBLIGHTING")) || !containsText(bspxFlagReport.warnings, QStringLiteral("LIGHTINGDIR"))) {
		return fail("Expected a bare -bspx request to expect both BSPX lightmap lumps.");
	}

	const QString truncatedPath = root.filePath(QStringLiteral("maps/truncated.bsp"));
	if (!writeFile(truncatedPath, QByteArray("IBSP"))) {
		return fail("Expected truncated BSP fixture.");
	}
	const slopstudio::CompilerArtifactValidationReport truncatedReport = slopstudio::validateCompilerArtifacts(manifestForOutput(truncatedPath));
	if (!truncatedReport.hasErrors() || !containsText(truncatedReport.errors, QStringLiteral("too small"))) {
		return fail("Expected truncated BSP header error.");
	}

	return EXIT_SUCCESS;
}
