#include "core/studio_manifest.h"

#include "slopstudio_config.h"

namespace slopstudio {

QVector<StudioModule> plannedModules()
{
	return {
		{
			"workspace",
			"Workspace",
			"Production",
			"active",
			"Project hub for installations, open packages, tasks, asset graph, and build output.",
			{"idTech1", "idTech2", "idTech3"},
		},
		{
			"level-editor",
			"Level Editor",
			"World Building",
			"mvp-active",
			"Map inspection, safe edit, profile-routed controls, health summaries, and compiler orchestration workspace.",
			{"idTech1", "idTech2", "idTech3"},
		},
		{
			"asset-editors",
			"Asset Editors",
			"Content",
			"mvp-active",
			"Texture, sprite, model, audio, shader, and script preview/editing slices sharing package and project context.",
			{"idTech1", "idTech2", "idTech3"},
		},
		{
			"package-manager",
			"Package Manager",
			"Packaging",
			"active",
			"PakFu-derived archive management for WAD, PAK, PK3, folders, and nested packages.",
			{"idTech1", "idTech2", "idTech3"},
		},
		{
			"script-ide",
			"Script And Code IDE",
			"Code",
			"mvp-active",
			"Script, shader, config, QuakeC, QVM, search, diagnostics, symbol, build, and launch-profile slices.",
			{"idTech1", "idTech2", "idTech3"},
		},
		{
			"shader-graph",
			"idTech3 Shader Graph",
			"Materials",
			"mvp-active",
			"Quake III shader script graph inspection, stage previews, dependency checks, and text round-tripping.",
			{"idTech3"},
		},
	};
}

QVector<CompilerIntegration> compilerIntegrations()
{
	return {
		{
			"slopmap2",
			"SlopMap2",
			"idTech2 / Quake BSP family",
			"SlopStudio's Quake and Quake II compiler suite (bsp, vis, light, bspinfo, bsputil, hub), derived from ericw-tools.",
			"external/compilers/slopmap2",
			"https://github.com/themuffinator/VibeyMapTools",
			"4495049a9e4c1f6deadae3a76b8256614840af35",
			"GPL-3.0",
		},
		{
			"slopmap3",
			"SlopMap3",
			"idTech3 / Quake III BSP family",
			"SlopStudio's Quake III compiler, a continuation of q3map2 from NetRadiant Custom under tools/quake3/q3map2.",
			"external/compilers/slopmap3",
			"https://github.com/themuffinator/q3mapx",
			"897524439cb58d2b736bc96b16c231d22dc5ddce",
			"GPL-3.0-or-later; imported q3map2 files keep their GPL-2.0-or-later notices",
		},
		{
			"zdbsp",
			"ZDBSP",
			"idTech1 / Doom WAD node building",
			"Fast Doom-family node builder with vanilla, Hexen, GL, and UDMF-aware modes.",
			"external/compilers/zdbsp",
			"https://github.com/rheit/zdbsp",
			"bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659",
			"GPL-2.0-or-later",
		},
		{
			"zokumbsp",
			"ZokumBSP",
			"idTech1 / Doom blockmap and node building",
			"Advanced Doom-family node, blockmap, and reject builder for vanilla-conscious maps.",
			"external/compilers/zokumbsp",
			"https://github.com/zokum-no/zokumbsp",
			"22af6defeb84ce836e0b184d6be5e80f127d9451",
			"GPL-2.0",
		},
	};
}

QVector<AboutDocument> aboutDocuments()
{
	return {
		{
			"license",
			"Project License",
			"LICENSE",
			"GPLv3 license text for SlopStudio-owned code.",
		},
		{
			"credits",
			"Credits",
			"docs/CREDITS.md",
			"Project, PakFu lineage, compiler toolchain, editor inspiration, AI reference, accessibility, and community credits.",
		},
		{
			"dependencies",
			"Dependencies",
			"docs/DEPENDENCIES.md",
			"Required, planned, optional, imported, and service-integration dependency notes.",
		},
		{
			"packaging",
			"Packaging",
			"docs/PACKAGING.md",
			"Portable packaging skeleton, release packaging plan, and license bundle expectations.",
		},
	};
}

QString versionString()
{
	return QString::fromUtf8(SLOPSTUDIO_VERSION);
}

QString githubRepository()
{
	return QString::fromUtf8(SLOPSTUDIO_GITHUB_REPO);
}

QString updateChannel()
{
	return QString::fromUtf8(SLOPSTUDIO_UPDATE_CHANNEL);
}

QString projectLicenseSummary()
{
	return QStringLiteral("SlopStudio-owned code is distributed under GPLv3. r8brain-free-src (MIT), its Ooura FFT, external compiler submodules, and other third-party components retain their own licenses; see docs/CREDITS.md and the bundled license notices.");
}

QString aboutSurfaceText()
{
	QStringList lines;
	lines << QStringLiteral("SlopStudio %1").arg(versionString());
	lines << QStringLiteral("Repository: %1").arg(githubRepository());
	lines << QStringLiteral("Update channel: %1").arg(updateChannel());
	lines << QStringLiteral("License: %1").arg(projectLicenseSummary());
	lines << QString();
	lines << QStringLiteral("Credits and license documents:");
	for (const AboutDocument& document : aboutDocuments()) {
		lines << QStringLiteral("- %1 [%2]: %3").arg(document.title, document.path, document.description);
	}
	lines << QString();
	lines << QStringLiteral("Imported compiler tools are separate submodules with their own upstream licenses; SlopStudio should invoke them as external tools until a documented source-level license review says otherwise.");
	return lines.join('\n');
}

} // namespace slopstudio
