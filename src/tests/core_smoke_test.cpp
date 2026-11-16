#include "core/studio_manifest.h"

#include <QCoreApplication>

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);

	const QVector<slopstudio::StudioModule> modules = slopstudio::plannedModules();
	if (modules.size() < 6) {
		std::cerr << "Expected at least six studio modules.\n";
		return EXIT_FAILURE;
	}
	bool hasActiveLevelEditor = false;
	bool hasActivePackageManager = false;
	for (const slopstudio::StudioModule& module : modules) {
		hasActiveLevelEditor = hasActiveLevelEditor || (module.id == QStringLiteral("level-editor") && module.maturity.contains(QStringLiteral("active")));
		hasActivePackageManager = hasActivePackageManager || (module.id == QStringLiteral("package-manager") && module.maturity == QStringLiteral("active"));
	}
	if (!hasActiveLevelEditor || !hasActivePackageManager) {
		std::cerr << "Studio module maturity should reflect implemented MVP package and level-editor slices.\n";
		return EXIT_FAILURE;
	}

	const QVector<slopstudio::CompilerIntegration> compilers = slopstudio::compilerIntegrations();
	if (compilers.size() < 4) {
		std::cerr << "Expected imported compiler integrations.\n";
		return EXIT_FAILURE;
	}

	bool hasSlopMap3 = false;
	bool hasIdTech1 = false;
	for (const slopstudio::CompilerIntegration& compiler : compilers) {
		hasSlopMap3 = hasSlopMap3 || compiler.id == "slopmap3";
		hasIdTech1 = hasIdTech1 || compiler.engines.contains("idTech1");
		if (compiler.upstreamUrl.isEmpty() || compiler.pinnedRevision.size() < 12) {
			std::cerr << "Compiler manifest entry is missing provenance.\n";
			return EXIT_FAILURE;
		}
	}

	if (!hasSlopMap3 || !hasIdTech1) {
		std::cerr << "Compiler manifest is missing SlopMap3 or idTech1 coverage.\n";
		return EXIT_FAILURE;
	}

	if (slopstudio::aboutDocuments().size() < 4 || !slopstudio::aboutSurfaceText().contains(QStringLiteral("GPLv3"))) {
		std::cerr << "About, credits, and license surface metadata is incomplete.\n";
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
