#include "core/compiler_known_issues.h"
#include "core/compiler_runner.h"

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

} // namespace

int main()
{
	const QVector<slopstudio::CompilerKnownIssueDescriptor> issues = slopstudio::compilerKnownIssueDescriptors();
	const QStringList auditIssueIds = {
		QStringLiteral("485"),
		QStringLiteral("484"),
		QStringLiteral("483"),
		QStringLiteral("482"),
		QStringLiteral("480"),
		QStringLiteral("475"),
		QStringLiteral("470"),
		QStringLiteral("464"),
		QStringLiteral("463"),
		QStringLiteral("456"),
		QStringLiteral("455"),
		QStringLiteral("451"),
		QStringLiteral("450"),
		QStringLiteral("449"),
		QStringLiteral("444"),
		QStringLiteral("443"),
		QStringLiteral("441"),
		QStringLiteral("439"),
		QStringLiteral("437"),
		QStringLiteral("436"),
		QStringLiteral("435"),
		QStringLiteral("425"),
		QStringLiteral("422"),
		QStringLiteral("417"),
		QStringLiteral("416"),
		QStringLiteral("415"),
		QStringLiteral("413"),
		QStringLiteral("411"),
		QStringLiteral("405"),
		QStringLiteral("401"),
		QStringLiteral("400"),
		QStringLiteral("399"),
		QStringLiteral("390"),
		QStringLiteral("389"),
		QStringLiteral("377"),
		QStringLiteral("375"),
		QStringLiteral("374"),
		QStringLiteral("364"),
		QStringLiteral("351"),
		QStringLiteral("350"),
		QStringLiteral("349"),
		QStringLiteral("346"),
		QStringLiteral("344"),
		QStringLiteral("343"),
		QStringLiteral("342"),
		QStringLiteral("335"),
		QStringLiteral("333"),
		QStringLiteral("327"),
		QStringLiteral("310"),
		QStringLiteral("309"),
		QStringLiteral("308"),
		QStringLiteral("301"),
		QStringLiteral("298"),
		QStringLiteral("297"),
		QStringLiteral("293"),
		QStringLiteral("291"),
		QStringLiteral("289"),
		QStringLiteral("288"),
		QStringLiteral("287"),
		QStringLiteral("285"),
		QStringLiteral("281"),
		QStringLiteral("278"),
		QStringLiteral("277"),
		QStringLiteral("270"),
		QStringLiteral("268"),
		QStringLiteral("264"),
		QStringLiteral("262"),
		QStringLiteral("257"),
		QStringLiteral("256"),
		QStringLiteral("255"),
		QStringLiteral("254"),
		QStringLiteral("249"),
		QStringLiteral("247"),
		QStringLiteral("246"),
		QStringLiteral("245"),
		QStringLiteral("241"),
		QStringLiteral("238"),
		QStringLiteral("234"),
		QStringLiteral("233"),
		QStringLiteral("231"),
		QStringLiteral("230"),
		QStringLiteral("225"),
		QStringLiteral("223"),
		QStringLiteral("222"),
		QStringLiteral("221"),
		QStringLiteral("217"),
		QStringLiteral("215"),
		QStringLiteral("214"),
		QStringLiteral("213"),
		QStringLiteral("209"),
		QStringLiteral("207"),
		QStringLiteral("205"),
		QStringLiteral("203"),
		QStringLiteral("201"),
		QStringLiteral("200"),
		QStringLiteral("199"),
		QStringLiteral("195"),
		QStringLiteral("194"),
		QStringLiteral("193"),
		QStringLiteral("192"),
		QStringLiteral("190"),
		QStringLiteral("189"),
		QStringLiteral("186"),
		QStringLiteral("185"),
		QStringLiteral("173"),
		QStringLiteral("172"),
		QStringLiteral("167"),
		QStringLiteral("145"),
		QStringLiteral("140"),
		QStringLiteral("136"),
		QStringLiteral("135"),
		QStringLiteral("134"),
		QStringLiteral("131"),
		QStringLiteral("129"),
		QStringLiteral("122"),
		QStringLiteral("121"),
		QStringLiteral("114"),
		QStringLiteral("109"),
		QStringLiteral("106"),
		QStringLiteral("105"),
		QStringLiteral("103"),
		QStringLiteral("91"),
		QStringLiteral("87"),
		QStringLiteral("81"),
		QStringLiteral("77"),
	};

	// Keep the original 125-ID audit as a closed partition: every report is either
	// retained or explicitly retired against the SlopMap2 3.1.0 audit/changelog.
	// The reasons and pinned source links live in the remaining-bugs resolution matrix.
	const QStringList retiredIssueIds = {
		QStringLiteral("167"), // Effective settings/version manifest: same fix as #483.
		QStringLiteral("173"), // Authored style preserved as oldstyle: same fix as #475.
		QStringLiteral("335"), // Both slopmap2-* and pre-rename vmt-* names are prefixed.
		QStringLiteral("400"), // Bounce debug repair: same fix as #416.
		QStringLiteral("401"), // Saturation preserves brightness.
		QStringLiteral("416"), // Bounce debug modes repaired.
		QStringLiteral("422"), // Region brush-entity culling repaired.
		QStringLiteral("437"), // GoldSrc sun/spot entities supported; not blanket format parity.
		QStringLiteral("444"), // Region areaportals preserved.
		QStringLiteral("451"), // Prefix-free texture layouts supported.
		QStringLiteral("463"), // Hub preview uses owned temporary workspaces.
		QStringLiteral("470"), // Half-Life min/max light scale restored.
		QStringLiteral("475"), // Authored switchable-light style preserved as oldstyle.
		QStringLiteral("483"), // Opt-in -embedsettings LIGHTING_SETTINGS manifest.
		QStringLiteral("485"), // -force_world_units_per_luxel supported.
	};
	const QStringList knownIssueIds = slopstudio::compilerKnownIssueIds();
	if (issues.size() + retiredIssueIds.size() != auditIssueIds.size()) {
		return fail("Expected every audited issue to be retained or explicitly retired, with no unreviewed additions.");
	}
	QStringList accountedIssueIds = knownIssueIds + retiredIssueIds;
	accountedIssueIds.removeDuplicates();
	if (accountedIssueIds.size() != auditIssueIds.size()) {
		return fail("Expected unique, disjoint retained and retired issue IDs.");
	}
	for (const QString& issueId : auditIssueIds) {
		if (!accountedIssueIds.contains(issueId)) {
			return fail("Expected an explicit SlopMap2 disposition for every original ericw-tools audit ID.");
		}
	}
	for (const QString& issueId : retiredIssueIds) {
		if (slopstudio::compilerKnownIssueForId(issueId)) {
			return fail("A fixed or inapplicable upstream issue must not re-enter the active catalogue.");
		}
	}
	for (const QString& issueId : {QStringLiteral("425"), QStringLiteral("480"), QStringLiteral("482")}) {
		slopstudio::CompilerKnownIssueDescriptor note;
		if (!slopstudio::compilerKnownIssueForId(issueId, &note)
			|| note.severity != slopstudio::CompilerKnownIssueSeverity::Info || note.highValue || !note.matchKeywords.isEmpty()) {
			return fail("Expected partial optimisation and platform constraints to remain informational help-only notes.");
		}
	}

	const QStringList highIssueIds = {
		QStringLiteral("484"),
		QStringLiteral("456"),
		QStringLiteral("450"),
		QStringLiteral("441"),
		QStringLiteral("439"),
		QStringLiteral("435"),
		QStringLiteral("417"),
		QStringLiteral("405"),
		QStringLiteral("399"),
		QStringLiteral("390"),
		QStringLiteral("389"),
		QStringLiteral("375"),
		QStringLiteral("364"),
		QStringLiteral("350"),
		QStringLiteral("346"),
		QStringLiteral("344"),
		QStringLiteral("343"),
		QStringLiteral("333"),
		QStringLiteral("308"),
		QStringLiteral("301"),
		QStringLiteral("293"),
		QStringLiteral("289"),
		QStringLiteral("288"),
		QStringLiteral("287"),
		QStringLiteral("278"),
		QStringLiteral("270"),
		QStringLiteral("264"),
		QStringLiteral("257"),
		QStringLiteral("245"),
		QStringLiteral("231"),
		QStringLiteral("230"),
		QStringLiteral("221"),
		QStringLiteral("207"),
		QStringLiteral("201"),
		QStringLiteral("199"),
		QStringLiteral("194"),
		QStringLiteral("192"),
		QStringLiteral("135"),
		QStringLiteral("122"),
		QStringLiteral("114"),
		QStringLiteral("87"),
	};
	for (const QString& issueId : highIssueIds) {
		slopstudio::CompilerKnownIssueDescriptor highIssue;
		if (!slopstudio::compilerKnownIssueForId(issueId, &highIssue) || !highIssue.highValue) {
			return fail("Expected every High-row audit issue to be present and high-value.");
		}
	}

	slopstudio::CompilerKnownIssueDescriptor issue;
	if (!slopstudio::compilerKnownIssueForId(QStringLiteral("#122"), &issue) || issue.clusterId != QStringLiteral("D02") || !issue.highValue) {
		return fail("Expected the unresolved START_OFF issue #122 to remain high-value in D02.");
	}
	if (issue.warningText.isEmpty() || issue.actionText.isEmpty() || issue.matchKeywords.isEmpty()) {
		return fail("Expected issue warning, action, and match keywords.");
	}

	const QVector<slopstudio::CompilerKnownIssueDescriptor> lightIssues = slopstudio::compilerKnownIssuesForProfile(QStringLiteral("slopmap2-light"));
	if (lightIssues.size() < 15) {
		return fail("Expected light profile to expose known lighting issues.");
	}
	const QVector<slopstudio::CompilerKnownIssueDescriptor> prefabIssues = slopstudio::compilerKnownIssuesForCluster(QStringLiteral("D05"));
	if (prefabIssues.size() < 5) {
		return fail("Expected D05 prefab cluster issues.");
	}

	const QVector<slopstudio::CompilerKnownIssueMatch> matches = slopstudio::matchCompilerKnownIssues(
		QStringLiteral("warning: _external_map_classname was missing on misc_external_map"),
		QStringLiteral("slopmap2-bsp"),
		QStringLiteral("slopmap2-bsp"));
	bool foundExternalMapClassname = false;
	bool foundPresentationMetaIssue = false;
	for (const slopstudio::CompilerKnownIssueMatch& match : matches) {
		if (match.issue.issueId == QStringLiteral("194")) {
			foundExternalMapClassname = true;
		}
		if (match.issue.issueId == QStringLiteral("287")) {
			foundPresentationMetaIssue = true;
		}
	}
	if (!foundExternalMapClassname) {
		return fail("Expected keyword matching for external-map classname issue.");
	}
	if (foundPresentationMetaIssue) {
		return fail("Expected the #287 presentation meta-issue never to match compiler output.");
	}

	// Every SlopMap2 run opens with "---- <program> / SlopMap2 <version> ----", the program
	// being the executable's own name
	// (external/compilers/slopmap2/src/common/settings.cc, common_settings::set_parameters), and
	// ordinary maps produce non-fatal WARNING lines and an "N errors" summary. A catalog keyword must
	// therefore never be a tool's own program name or a bare status word, or every healthy compile
	// would be downgraded to Warning with an unrelated upstream issue attached.
	struct OrdinaryRun {
		const char* toolId;
		QString output;
	};
	const QVector<OrdinaryRun> ordinaryRuns = {
		{"slopmap2-bsp", QStringLiteral(
			"---- slopmap2-bsp / SlopMap2 3.1.0 ----\n"
			"Input file: maps/start.map\n"
			"Output file: maps/start.bsp\n"
			"WARNING: 3 microbrushes\n"
			"0 errors\n"
			"1.234 seconds elapsed\n")},
		{"slopmap2-light", QStringLiteral(
			"---- slopmap2-light / SlopMap2 3.1.0 ----\n"
			"running 8 threads\n"
			"0 warnings\n")},
		{"slopmap2-vis", QStringLiteral("---- slopmap2-vis / SlopMap2 3.1.0 ----\nno errors\n")},
		{"slopmap2-bsputil", QStringLiteral("---- slopmap2-bsputil / SlopMap2 3.1.0 ----\n")},
		{"slopmap2-hub", QStringLiteral("---- slopmap2-hub / SlopMap2 3.1.0 ----\n")},
		{"slopmap2-bspinfo", QStringLiteral("---- slopmap2-bspinfo / SlopMap2 3.1.0 ----\n")},
	};
	for (const OrdinaryRun& run : ordinaryRuns) {
		const QString toolId = QString::fromLatin1(run.toolId);
		for (const slopstudio::CompilerKnownIssueMatch& match : slopstudio::matchCompilerKnownIssues(run.output, toolId, toolId)) {
			if (match.issue.highValue) {
				return fail("An ordinary SlopMap2 run must not match a high-value known issue.");
			}
		}
	}

	// The tools also print catalogue words on every healthy run, where they are counts and notices
	// and not problems: qbsp's lump table names "clipnodes" and "marksurfaces"
	// (external/compilers/slopmap2/src/qbsp/writebsp.cc, src/common/bspfile.cc PrintLumpSize), and
	// light reports "shadow-casting skip faces" (src/light/trace_embree.cc) and "INFO: using built-in
	// palette." (src/common/imglib.cc). Matched as plain text these lines hit #136, #255 and #192, so
	// a run matches the catalogue against its diagnostic lines only, and they never reach it.
	const QVector<OrdinaryRun> ordinaryStatusOutput = {
		{"slopmap2-bsp", QStringLiteral(
			"---- slopmap2-bsp / SlopMap2 3.1.0 ----\n"
			"Wrote maps/start.bsp\n"
			"      1 models               64\n"
			"     12 clipnodes            96\n"
			"     40 marksurfaces         80\n"
			"[  0%]  ...\r[ 50%]  est: 00:00:01\r[100%] time elapsed: 00:00:01\n")},
		{"slopmap2-light", QStringLiteral(
			"---- slopmap2-light / SlopMap2 3.1.0 ----\n"
			"INFO: using built-in palette.\n"
			"\t0 sky faces\n"
			"\t3 shadow-casting skip faces\n")},
	};
	for (const OrdinaryRun& run : ordinaryStatusOutput) {
		const QString toolId = QString::fromLatin1(run.toolId);
		if (slopstudio::matchCompilerKnownIssues(run.output, toolId, toolId).isEmpty()) {
			return fail("Expected the status-line samples to use catalogue keywords, or they prove nothing.");
		}
		const QVector<slopstudio::CompilerDiagnostic> diagnostics = slopstudio::compilerOutputDiagnostics(run.output);
		if (!diagnostics.isEmpty() || !slopstudio::compilerKnownIssuesForDiagnostics(diagnostics, toolId, toolId).isEmpty()) {
			return fail("A healthy SlopMap2 run's status lines must not be matched against the known-issue catalogue.");
		}
	}
	// A real diagnostic is still matched, whatever else the run printed around it.
	const QVector<slopstudio::CompilerDiagnostic> paletteDiagnostics = slopstudio::compilerOutputDiagnostics(QStringLiteral(
		"     12 clipnodes            96\n"
		"[ 50%]  est: 00:00:01\rWARNING: unable to load palette from gfx/palette.lmp\n"));
	bool foundPaletteIssue = false;
	for (const slopstudio::CompilerKnownIssueMatch& match : slopstudio::compilerKnownIssuesForDiagnostics(paletteDiagnostics,
			QStringLiteral("slopmap2-light"), QStringLiteral("slopmap2-light"))) {
		foundPaletteIssue = foundPaletteIssue || match.issue.issueId == QStringLiteral("192");
	}
	if (paletteDiagnostics.size() != 1 || paletteDiagnostics.first().message != QStringLiteral("WARNING: unable to load palette from gfx/palette.lmp")
		|| !foundPaletteIssue) {
		return fail("Expected a real compiler warning to keep its known-issue match.");
	}

	// Keywords match whole tokens: "leak" must not fire on the "-leaktest" argument preset, and
	// "Q2" must not fire inside a map name such as q2dm1.
	if (!slopstudio::slopmap2KnownIssuePlanWarnings(
			QStringLiteral("slopmap2-bsp"),
			QStringLiteral("maps/start.map"),
			{QStringLiteral("-leaktest")}).isEmpty()) {
		return fail("Expected the -leaktest preset not to raise leak known-issue plan warnings.");
	}
	for (const slopstudio::CompilerKnownIssueMatch& match : slopstudio::matchCompilerKnownIssues(
			QStringLiteral("maps/q2dm1.bsp"), QStringLiteral("slopmap2-light"), QStringLiteral("slopmap2-light"))) {
		if (match.issue.highValue) {
			return fail("Expected a map name to stay clear of whole-token known-issue keywords.");
		}
	}

	const QString text = slopstudio::compilerKnownIssueText(issue);
	if (!text.contains(QStringLiteral("D02")) || !text.contains(QStringLiteral("https://github.com/ericwa/ericw-tools/issues/122"))) {
		return fail("Expected formatted known-issue text to include cluster and upstream URL.");
	}

	// Supported SlopMap2 lighting switches no longer raise historical feature warnings.
	if (!slopstudio::slopmap2KnownIssuePlanWarnings(QStringLiteral("slopmap2-light"),
			QStringLiteral("maps/start.bsp"), {QStringLiteral("-embedsettings"),
				QStringLiteral("-world_units_per_luxel"), QStringLiteral("32"),
				QStringLiteral("-force_world_units_per_luxel"), QStringLiteral("-bouncedebug")}).isEmpty()) {
		return fail("Expected supported SlopMap2 3.1.0 lighting switches to produce no known-issue plan warnings.");
	}
	if (!slopstudio::matchCompilerKnownIssues(QStringLiteral("temporary directory overwrite Wayland"),
			QStringLiteral("slopmap2-hub"), QStringLiteral("slopmap2-hub")).isEmpty()) {
		return fail("Expected isolated Hub previews and platform guidance not to match compiler diagnostics.");
	}

	const QStringList lightWarnings = slopstudio::slopmap2KnownIssuePlanWarnings(
		QStringLiteral("slopmap2-light"),
		QStringLiteral("maps/test.1.bsp"),
		{QStringLiteral("-bspxhdr"), QStringLiteral("BSPX")});
	int lightingDirWarningCount = 0;
	int dottedNameWarningCount = 0;
	for (const QString& warning : lightWarnings) {
		if (warning.contains(QStringLiteral("#484"))) {
			++lightingDirWarningCount;
		}
		if (warning.contains(QStringLiteral("#230"))) {
			++dottedNameWarningCount;
		}
	}
	if (lightingDirWarningCount != 1 || dottedNameWarningCount != 1) {
		return fail("Expected useful deduplicated light known-issue plan warnings.");
	}

	const QStringList qbspWarnings = slopstudio::slopmap2KnownIssuePlanWarnings(
		QStringLiteral("slopmap2-bsp"),
		QStringLiteral("maps/csg_fail.map"),
		{QStringLiteral("-notex"), QStringLiteral("missing texture")});
	int noTextureWarningCount = 0;
	int csgWarningCount = 0;
	for (const QString& warning : qbspWarnings) {
		if (warning.contains(QStringLiteral("#450"))) {
			++noTextureWarningCount;
		}
		if (warning.contains(QStringLiteral("#114"))) {
			++csgWarningCount;
		}
	}
	if (noTextureWarningCount != 1 || csgWarningCount != 1) {
		return fail("Expected useful deduplicated qbsp known-issue plan warnings.");
	}
	if (!slopstudio::slopmap2KnownIssuePlanWarnings(QStringLiteral("slopmap3-bsp"), QString(), {}).isEmpty()) {
		return fail("Expected SlopMap2 known-issue plan warnings to stay scoped to SlopMap2 profiles.");
	}

	return EXIT_SUCCESS;
}
