#include "core/compiler_known_issues.h"

#include <QCoreApplication>
#include <QFileInfo>

namespace slopstudio {

namespace {

QString normalizedId(QString value)
{
	value = value.trimmed().toLower().replace('_', '-');
	if (value.startsWith('#')) {
		value.remove(0, 1);
	}
	return value;
}

QString upstreamIssueUrl(const QString& issueId)
{
	return QStringLiteral("https://github.com/ericwa/ericw-tools/issues/%1").arg(normalizedId(issueId));
}

bool containsNormalizedId(const QStringList& ids, const QString& id)
{
	const QString requested = normalizedId(id);
	for (const QString& candidate : ids) {
		if (normalizedId(candidate) == requested) {
			return true;
		}
	}
	return false;
}

bool isKeywordWordCharacter(QChar character)
{
	return character.isLetterOrNumber() || character == QLatin1Char('_');
}

// Keywords are matched as whole tokens, never as bare substrings: "leak" must not fire on the
// "-leaktest" argument preset and "Q2" must not fire on a path such as maps/q2dm1.bsp. Only ends
// that are themselves word characters are anchored, because many catalog keywords deliberately
// begin with '-', '_', '.' or '\' ("-notex", "_minlight", ".1.bsp", "\b") where a symmetric word
// boundary would anchor on the wrong side.
bool textContainsKeyword(const QString& text, const QString& keyword)
{
	if (keyword.isEmpty()) {
		return false;
	}
	const bool anchorStart = isKeywordWordCharacter(keyword.front());
	const bool anchorEnd = isKeywordWordCharacter(keyword.back());
	int index = text.indexOf(keyword, 0, Qt::CaseInsensitive);
	while (index >= 0) {
		const int end = index + keyword.size();
		const bool startFits = !anchorStart || index == 0 || !isKeywordWordCharacter(text.at(index - 1));
		const bool endFits = !anchorEnd || end >= text.size() || !isKeywordWordCharacter(text.at(end));
		if (startFits && endFits) {
			return true;
		}
		index = text.indexOf(keyword, index + 1, Qt::CaseInsensitive);
	}
	return false;
}

bool matchesScope(const CompilerKnownIssueDescriptor& issue, const QString& toolId, const QString& profileId)
{
	if (toolId.trimmed().isEmpty() && profileId.trimmed().isEmpty()) {
		return true;
	}
	if (!toolId.trimmed().isEmpty() && containsNormalizedId(issue.affectedToolIds, toolId)) {
		return true;
	}
	if (!profileId.trimmed().isEmpty() && containsNormalizedId(issue.affectedProfileIds, profileId)) {
		return true;
	}
	return false;
}

CompilerKnownIssueDescriptor knownIssue(
	const QString& issueId,
	const QString& clusterId,
	CompilerKnownIssueSeverity severity,
	bool highValue,
	const QStringList& affectedToolIds,
	const QStringList& affectedProfileIds,
	const QStringList& matchKeywords,
	const char* summaryText,
	const char* warningText,
	const char* actionText)
{
	return {
		normalizedId(issueId),
		clusterId,
		severity,
		highValue,
		affectedToolIds,
		affectedProfileIds,
		matchKeywords,
		upstreamIssueUrl(issueId),
		QCoreApplication::translate("SlopStudioCompilerKnownIssues", summaryText),
		QCoreApplication::translate("SlopStudioCompilerKnownIssues", warningText),
		QCoreApplication::translate("SlopStudioCompilerKnownIssues", actionText),
	};
}

QStringList qbspTool()
{
	return {QStringLiteral("slopmap2-bsp")};
}

QStringList visTool()
{
	return {QStringLiteral("slopmap2-vis")};
}

QStringList lightTool()
{
	return {QStringLiteral("slopmap2-light")};
}

QStringList qbspProfile()
{
	return {QStringLiteral("slopmap2-bsp")};
}

QStringList visProfile()
{
	return {QStringLiteral("slopmap2-vis")};
}

QStringList lightProfile()
{
	return {QStringLiteral("slopmap2-light")};
}

QStringList bsputilTool()
{
	return {QStringLiteral("slopmap2-bsputil")};
}

QStringList bsputilProfile()
{
	return {QStringLiteral("slopmap2-bsputil")};
}

QStringList hubTool()
{
	return {QStringLiteral("slopmap2-hub")};
}

QStringList hubProfile()
{
	return {QStringLiteral("slopmap2-hub")};
}

} // namespace

QString compilerKnownIssueSeverityId(CompilerKnownIssueSeverity severity)
{
	switch (severity) {
	case CompilerKnownIssueSeverity::Info:
		return QStringLiteral("info");
	case CompilerKnownIssueSeverity::Warning:
		return QStringLiteral("warning");
	case CompilerKnownIssueSeverity::Error:
		return QStringLiteral("error");
	case CompilerKnownIssueSeverity::Critical:
		return QStringLiteral("critical");
	}
	return QStringLiteral("warning");
}

QString compilerKnownIssueSeverityText(CompilerKnownIssueSeverity severity)
{
	switch (severity) {
	case CompilerKnownIssueSeverity::Info:
		return QCoreApplication::translate("SlopStudioCompilerKnownIssues", "Info");
	case CompilerKnownIssueSeverity::Warning:
		return QCoreApplication::translate("SlopStudioCompilerKnownIssues", "Warning");
	case CompilerKnownIssueSeverity::Error:
		return QCoreApplication::translate("SlopStudioCompilerKnownIssues", "Error");
	case CompilerKnownIssueSeverity::Critical:
		return QCoreApplication::translate("SlopStudioCompilerKnownIssues", "Critical");
	}
	return QCoreApplication::translate("SlopStudioCompilerKnownIssues", "Warning");
}

// Audited against SlopMap2 3.1.0 at 4495049a9e4c1f6deadae3a76b8256614840af35.
// Fixed and inapplicable upstream reports are retired in the coverage test and in
// docs/plans/ericw-tools-remaining-bugs-resolution.md, including the evidence for each decision.
// Remaining reports are conservative risks, not proof of a defect in this fork.
// Match keywords must discriminate. Two invariants keep the catalog from firing on healthy runs:
// a keyword may never be a tool's own program name, because SlopMap2 prints
// "---- <program_name> / SlopMap2 <version> ----" as the first line of every run
// (external/compilers/slopmap2/src/common/settings.cc, common_settings::set_parameters), and a
// keyword may never be a word that ordinary compiler output uses for its own status lines
// ("error", "warning", "failed"). An entry with nothing distinctive to match on carries an empty
// keyword list and stays a documentation-only note.
QVector<CompilerKnownIssueDescriptor> compilerKnownIssueDescriptors()
{
	return {
		knownIssue(QStringLiteral("399"), QStringLiteral("D01"), CompilerKnownIssueSeverity::Error, true, lightTool(), lightProfile(), {QStringLiteral("LMSHIFT"), QStringLiteral("_lmscale"), QStringLiteral("lightmap scale"), QStringLiteral("BSPX")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "LMSHIFT metadata can be missing from 2.0 alpha builds."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Custom lightmap scale metadata may be absent from BSPX output."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Warn on custom _lmscale use, preserve the compiler version, and verify BSPX lightmap metadata after compile.")),
		knownIssue(QStringLiteral("415"), QStringLiteral("D01"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("BSPX"), QStringLiteral("light-specific lump"), QStringLiteral("LIGHTINGDIR"), QStringLiteral("LMSHIFT")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Light-specific BSPX lump documentation is incomplete upstream."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "BSPX inspectors may encounter light metadata whose compiler-side contract is not fully documented."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Present BSPX light metadata as inspectable evidence and avoid implying undocumented lumps are guaranteed across compiler versions.")),
		knownIssue(QStringLiteral("309"), QStringLiteral("D01"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("lightmap dimensions"), QStringLiteral("BSPX"), QStringLiteral("luxel"), QStringLiteral("_lmscale")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "BSPX output may not record compiler-calculated lightmap dimensions."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Engines and editor inspectors can disagree about lightmap dimensions when the metadata is absent."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Keep profile-required BSPX metadata checks separate from the compile and preserve the command manifest for triage.")),

		knownIssue(QStringLiteral("122"), QStringLiteral("D02"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("START_OFF"), QStringLiteral("start off"), QStringLiteral("spawnflag"), QStringLiteral("targetname")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Grouped toggled lights can disagree about START_OFF."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Lights sharing a targetname may start in inconsistent on/off states."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Validate grouped lights and require a consistent start-off spawnflag policy.")),

		knownIssue(QStringLiteral("77"), QStringLiteral("D03"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("-dirtdebug"), QStringLiteral("dirtdebug"), QStringLiteral("_dirt -1")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "-dirtdebug can draw ambient occlusion despite _dirt -1."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Dirt debug output may show AO for entities that disabled dirt."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Explain that debug visualization can disagree with intended entity dirt settings.")),

		knownIssue(QStringLiteral("293"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("absolute WAD"), QStringLiteral("wad path"), QStringLiteral("strip directories"), QStringLiteral("private path")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Absolute WAD paths can leak private directories into output."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Entity lumps may expose local filesystem paths or exceed engine buffer expectations."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Sanitize exported WAD lists and keep project-relative package paths where possible.")),
		knownIssue(QStringLiteral("288"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("escape sequence"), QStringLiteral("backslash"), QStringLiteral("\\b"), QStringLiteral("entity key")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Backslash escape warnings can be unclear."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Entity keys containing backslashes may be parsed differently than the author expects."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Preflight suspicious backslashes and offer a map-safe escaped replacement.")),
		// Deliberately unmatchable: this is a user-experience meta-issue about how compiler output is
		// presented, and no compiler line can legitimately indicate it. Its former keywords ("error",
		// "warning", "failed") appear in the normal output of every successful SlopMap2 run, which
		// downgraded clean compiles to Warning and cited an unrelated upstream issue. It stays
		// high-value so the notes for these profiles keep listing it.
		knownIssue(QStringLiteral("287"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Warning, true, {QStringLiteral("slopmap2-bsp"), QStringLiteral("slopmap2-vis"), QStringLiteral("slopmap2-light")}, {QStringLiteral("slopmap2-bsp"), QStringLiteral("slopmap2-vis"), QStringLiteral("slopmap2-light")}, {},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Common compiler errors need richer user-facing guidance."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Raw compiler output may not explain the practical fix."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Layer known-issue explanations over parsed diagnostics and keep raw logs available.")),
		knownIssue(QStringLiteral("245"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("long key"), QStringLiteral("long value"), QStringLiteral("WAD list"), QStringLiteral("entity key")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Excessively long entity values can crash or confuse engines."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Large key values, especially WAD lists, may make a map fragile after compile."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Measure entity key/value lengths before compile and suggest package-relative cleanup.")),
		knownIssue(QStringLiteral("135"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("invalid numeric"), QStringLiteral("entity coordinates"), QStringLiteral("parse warning")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Numeric parse warnings may lack useful entity coordinates."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Invalid numeric entity values can be hard to locate from compiler output alone."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Attach parser warnings to editor entities and include map coordinates in diagnostics.")),
		knownIssue(QStringLiteral("87"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("escape sequence"), QStringLiteral("double quote"), QStringLiteral("WAD path")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "qbsp escape-sequence warnings need preflight handling."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Quotes and backslashes in map text can create misleading compiler warnings."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Run SlopStudio map-text validation before invoking qbsp.")),
		knownIssue(QStringLiteral("129"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Warning, false, qbspTool(), qbspProfile(), {QStringLiteral("\\b"), QStringLiteral("backspace"), QStringLiteral("escape handling")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Backslash escape handling may happen later than users expect."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Map text that depends on escape interpretation can be reported inconsistently across pipeline stages."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Use SlopStudio parser warnings as the stable user-facing explanation before qbsp runs.")),
		knownIssue(QStringLiteral("121"), QString(), CompilerKnownIssueSeverity::Info, false, qbspTool(), qbspProfile(), {QStringLiteral("entdict_t"), QStringLiteral("case-insensitive"), QStringLiteral("entity key"), QStringLiteral("classname")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Entity key case-sensitivity expectations are not fully settled upstream."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Maps using keys that differ only by case may be interpreted differently by tools, engines, or editor services."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Normalize editor-authored keys where the game profile requires it and warn before exporting ambiguous entity dictionaries.")),
		knownIssue(QStringLiteral("105"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("_sunlight_mangle"), QStringLiteral("mangle"), QStringLiteral("status output"), QStringLiteral("sunlight")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "_sunlight_mangle status output can be hard to parse."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Compiler logs may describe sunlight direction in a format that is awkward for users and diagnostics."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Display SlopStudio's parsed sun direction beside the raw log instead of relying on upstream wording alone.")),

		knownIssue(QStringLiteral("333"), QStringLiteral("D05"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("_external_map"), QStringLiteral("entities"), QStringLiteral("prefab")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "External maps do not import full entity content yet."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Prefab maps may lose entity behavior when compiled through current SlopMap2 support."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Flag prefab sources containing entities and document what will be merged.")),
		knownIssue(QStringLiteral("231"), QStringLiteral("D05"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("misc_external_map"), QStringLiteral("_phong"), QStringLiteral("lighting key")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "External-map lighting keys can be ignored."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Prefab lighting settings such as _phong may not carry into imported geometry."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Warn on lighting keys attached to misc_external_map and suggest baking them into the prefab map.")),
		knownIssue(QStringLiteral("207"), QStringLiteral("D05"), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("func_group"), QStringLiteral("external map"), QStringLiteral("corrupt")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "External maps made entirely of func_group brushes can corrupt output."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Grouped prefab content may compile incorrectly when imported as an external map."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Validate prefab entity composition and require at least one safe import target path.")),
		knownIssue(QStringLiteral("199"), QStringLiteral("D05"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("_external_map"), QStringLiteral("relative path"), QStringLiteral("map-file-relative")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "External-map paths need map-file-relative handling."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Prefab references may break when the working directory differs from the source map location."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Resolve prefab paths through SlopStudio project services and pass a controlled working directory.")),
		knownIssue(QStringLiteral("194"), QStringLiteral("D05"), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("_external_map_classname"), QStringLiteral("misc_external_map"), QStringLiteral("assert")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Missing external-map classname can crash the compile."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "A misc_external_map without _external_map_classname may hit an upstream assertion."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Require a classname during preflight, defaulting to func_detail only after user confirmation.")),
		knownIssue(QStringLiteral("193"), QStringLiteral("D05"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("0 360 0"), QStringLiteral("external map"), QStringLiteral("rotation"), QStringLiteral("shadow")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "External-map rotations can produce odd shadows."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Certain prefab rotations may light differently than expected."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Normalize prefab rotations and highlight transformed prefab lighting as a QA checkpoint.")),
		knownIssue(QStringLiteral("327"), QStringLiteral("D05"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("misc_external_map"), QStringLiteral("merge"), QStringLiteral("target entity"), QStringLiteral("prefab composition")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Multiple external-map pieces cannot be assumed to merge into one target entity."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Advanced prefab composition may produce separate or unexpected brush entities."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Show composed prefab structure in the compile preview and keep grouped-target behavior documented as upstream work.")),

		knownIssue(QStringLiteral("417"), QStringLiteral("D06"), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("region+"), QStringLiteral("rotating door"), QStringLiteral("origin brush"), QStringLiteral("leak")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "region+ can leak with rotating doors that have origin brushes."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "A Q2 region+ compile may leak when rotating bmodels use origin brushes."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Warn on origin-brush rotating doors in region+ profiles and suggest a full compile for validation.")),
		knownIssue(QStringLiteral("390"), QStringLiteral("D06"), CompilerKnownIssueSeverity::Info, true, qbspTool(), qbspProfile(), {QStringLiteral("multiple region brushes"), QStringLiteral("region brush"), QStringLiteral("partial compile")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Multiple region brushes are not supported as a union."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Editor selections using several regions may not match SlopMap2 region behavior."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Collapse region requests to one temporary region volume or explain the limitation before compile.")),

		knownIssue(QStringLiteral("484"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("LIGHTINGDIR"), QStringLiteral("_sunlight2"), QStringLiteral("Q2"), QStringLiteral("BSPX")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Q2 BSPX LIGHTINGDIR can show discrete _sunlight2 direction steps."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "The legacy sunlight dome can produce directional steps; SlopMap2 documents sampling guidance rather than claiming an algorithmic fix."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Compare -sunsamples and -extra4 previews, allowing for the legacy dome brightness trade-off.")),
		knownIssue(QStringLiteral("456"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("wrong shadows"), QStringLiteral("func_detail"), QStringLiteral("func_illusionary"), QStringLiteral("visblocker")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Brush-model luxel placement needs an explicit inside/outside policy."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "SlopMap2 retains the current placement policy because changing illusionary geometry globally can alter intended shadows."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Validate affected brush-model lighting in a preview; a mapper-controlled placement policy still needs compiler design and regression tests.")),
		knownIssue(QStringLiteral("441"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, {QStringLiteral("slopmap2-bsp"), QStringLiteral("slopmap2-vis")}, {QStringLiteral("slopmap2-bsp"), QStringLiteral("slopmap2-vis")}, {QStringLiteral("func_illusionary_visblocker"), QStringLiteral("0.18.1"), QStringLiteral("visible faces")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "func_illusionary_visblocker behavior changed from older releases."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Maps relying on old visblocker behavior may render or vis differently."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Record compiler version and prompt for a regression check when visblockers are present.")),
		knownIssue(QStringLiteral("439"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("light bleed"), QStringLiteral("brush model"), QStringLiteral("2.0 alpha")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "2.0 alpha builds can show light bleed on brush model edges."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Brush models may receive edge lighting artifacts compared with older releases."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Warn on affected version/profile combinations and preserve visual regression screenshots.")),
		knownIssue(QStringLiteral("405"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("_phong"), QStringLiteral("phong_angle"), QStringLiteral("light bleed")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Phong settings can cause confusing smoothing and light bleed."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Phong-smoothed geometry may light incorrectly on hard cube-like edges."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Warn on high-risk _phong and phong_angle combinations in the entity inspector.")),
		knownIssue(QStringLiteral("389"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Error, true, lightTool(), lightProfile(), {QStringLiteral("surface lights"), QStringLiteral("alpha 3"), QStringLiteral("Q2"), QStringLiteral("emit")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Q2 surface lights can stop emitting in alpha builds."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Surface-light textures may appear bright without lighting the room."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Track compiler version and run a small surface-light fixture for Q2 profiles.")),
		knownIssue(QStringLiteral("375"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("relighting"), QStringLiteral("old BSP"), QStringLiteral("too bright"), QStringLiteral("fullbright")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Relighting old Q2 BSPs can produce extreme brightness."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Existing BSP relight workflows may become fullbright, too bright, or too dark."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Require a preview pass and preserve the original BSP before relighting.")),
		knownIssue(QStringLiteral("364"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("corner artifacts"), QStringLiteral("grid point"), QStringLiteral("Q2 lightmaps")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Q2 lightmaps can show corner and grid-point artifacts."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Recent compiler ranges may introduce visible lightmap artifacts at face corners."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Keep a versioned lighting regression fixture for Q2 maps.")),
		knownIssue(QStringLiteral("221"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("sunlight"), QStringLiteral("shining through world"), QStringLiteral("sky geometry")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Sunlight can leak through world or sky geometry."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Sun lighting may appear in areas that should be occluded."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Flag sunlight-heavy maps for visual QA and keep leak/sky diagnostics visible.")),
		knownIssue(QStringLiteral("351"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("bouncescale"), QStringLiteral("_bouncescale"), QStringLiteral("per-light bounce"), QStringLiteral("bounce scale")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Per-light bounce scale has reported incorrect behavior."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Individual light bounce controls may not produce the expected indirect lighting contribution."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Warn when per-light bouncescale keys are present and compare against a conservative profile preview.")),
		knownIssue(QStringLiteral("349"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("_dirt"), QStringLiteral("_dirtscale"), QStringLiteral("bmodels")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Dirt lighting keys can fail on brush models."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Brush entities may ignore dirt settings that work on worldspawn."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Warn when bmodels carry dirt keys and suggest checking baked lighting previews.")),
		knownIssue(QStringLiteral("291"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("deviance"), QStringLiteral("penumbra"), QStringLiteral("sky light entity"), QStringLiteral("_sunlight_penumbra")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Sky-light entity penumbra can behave differently from worldspawn sunlight."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Non-worldspawn sun setups may ignore or misapply deviance and penumbra values."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Prefer profile-visible sun settings and mark entity-based sky lighting for preview comparison.")),
		knownIssue(QStringLiteral("268"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("_sun 1"), QStringLiteral("deviance"), QStringLiteral("penumbra"), QStringLiteral("sun entity")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Deviance can fail on light entities marked as sun sources."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Sunlight authored through `_sun 1` entities may not match penumbra expectations."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Flag `_sun 1` entities with deviance settings as lighting-regression candidates.")),
		knownIssue(QStringLiteral("262"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("_mirrorinside"), QStringLiteral("mirrorinside"), QStringLiteral("shadow regression"), QStringLiteral("inside shadow")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "_mirrorinside has a tracked shadow regression."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Shadow output may differ from older compiler builds when mirrored-inside lighting is enabled."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Keep compiler version visible and ask for visual QA when `_mirrorinside` appears in a map.")),
		knownIssue(QStringLiteral("256"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("_switchableshadow"), QStringLiteral("switchable shadow"), QStringLiteral("_shadow 1"), QStringLiteral("shadow casting")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Switchable shadows may not imply normal shadow casting."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Authors can expect switchable-shadow keys to cast shadows even when `_shadow` is not explicitly enabled."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Warn on switchable-shadow entities without an explicit shadow policy in the inspector.")),
		knownIssue(QStringLiteral("255"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("_switchableshadow"), QStringLiteral("skip"), QStringLiteral("bmodel"), QStringLiteral("switchable shadows")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Switchable shadows can interact badly with skip-textured bmodels."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Brush models using skip faces and switchable shadows may light incorrectly."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Preflight this entity combination and recommend a small preview compile before shipping.")),
		knownIssue(QStringLiteral("247"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("misc_model"), QStringLiteral("cast shadow"), QStringLiteral("model shadow"), QStringLiteral("prop shadow")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "misc_model shadow casting is a requested upstream feature."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Model props cannot be assumed to contribute baked shadows in current SlopMap2 compiles."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Expose model shadow expectations in asset diagnostics and suggest brush proxies when the target engine needs baked shadows.")),
		knownIssue(QStringLiteral("238"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("penumbra"), QStringLiteral("pitch"), QStringLiteral("yaw"), QStringLiteral("sunlight")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Sunlight penumbra can deviate differently in pitch and yaw."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Mostly vertical sunlight may produce softness that does not match the authored penumbra values."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Show sun-vector previews and keep vertical sun setups in the lighting QA checklist.")),
		knownIssue(QStringLiteral("217"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("_surface"), QStringLiteral("liquid"), QStringLiteral("surface light"), QStringLiteral("water")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Surface light settings can collide with liquid textures."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Liquid surface lights may be ignored, renamed, or matched differently than expected."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Validate `_surface` light names against liquid texture usage and show ambiguous matches before compile.")),
		knownIssue(QStringLiteral("214"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("-gate 4"), QStringLiteral("gate"), QStringLiteral("sunlight"), QStringLiteral("sun")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "High gate values can suppress sunlight."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Lighting profiles using `-gate 4` may accidentally remove expected sun contribution."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Warn on high gate values in sunlight-heavy profiles and offer a lower-gate preview.")),
		knownIssue(QStringLiteral("203"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("disable bounce"), QStringLiteral("sunlight2"), QStringLiteral("sun bounce"), QStringLiteral("bounce")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Disabling bounce for sun sources is requested upstream behavior."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Profiles cannot assume sun or `_sunlight2` bounce can be disabled independently by the compiler."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Represent sun-bounce suppression as a profile limitation until a supported compiler option exists.")),
		knownIssue(QStringLiteral("190"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("bounce"), QStringLiteral("soft"), QStringLiteral("extra4"), QStringLiteral("messed up lighting")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Some bounce/soft/extra lighting combinations have reported visual regressions."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "High-quality lighting presets can still produce map-specific artifacts."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Keep high-quality preset output under visual regression review and preserve the exact compiler arguments.")),
		knownIssue(QStringLiteral("185"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("black seams"), QStringLiteral("shib5"), QStringLiteral("shib1"), QStringLiteral("seams")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Specific maps have reported black lighting seams."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Seam artifacts may persist even when the compile completes without errors."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Use seam reports as compatibility fixtures and keep visual inspection separate from process success.")),
		knownIssue(QStringLiteral("172"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("spotlight"), QStringLiteral("_phong 1"), QStringLiteral("models"), QStringLiteral("phong")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Spotlights can behave incorrectly on Phong-smoothed models."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Model or brush surfaces using `_phong 1` may receive invalid spotlighting."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Warn when spotlights target Phong-smoothed model geometry and suggest a controlled preview pass.")),
		knownIssue(QStringLiteral("145"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("H-shaped artifacts"), QStringLiteral("angled beam"), QStringLiteral("pedestals"), QStringLiteral("artifact")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Some angled-beam layouts have reported lighting artifacts."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Map-specific artifact reports can indicate compiler-version sensitivity rather than author error."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Keep artifact-prone maps as optional regression fixtures and cite the upstream issue in QA notes.")),
		knownIssue(QStringLiteral("140"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("_dirtmode"), QStringLiteral("_dirtdepth"), QStringLiteral("_dirtgain"), QStringLiteral("func_group")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Dirt option support on non-worldspawn entities is incomplete upstream."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Brush entities may not honor the same dirt controls that worldspawn uses."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Group this with bmodel dirt diagnostics and show which entity keys are compiler-supported for the active profile.")),
		knownIssue(QStringLiteral("134"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("hard lines"), QStringLiteral("dirtmap"), QStringLiteral("shadow regression"), QStringLiteral("hard shadow")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Hard dirt and shadow lines are tracked as a lighting regression."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Compiled lighting can show visible line artifacts despite valid map data."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Preserve compiler version data and include hard-line checks in visual regression passes.")),
		knownIssue(QStringLiteral("109"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("surface light"), QStringLiteral("submodel"), QStringLiteral("nudge"), QStringLiteral("bmodel")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Surface lights may need nudging on submodels."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Bmodel surface lights can sit exactly on geometry and fail to illuminate as expected."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Warn on surface lights attached to submodels and suggest a small offset or preview check.")),
		knownIssue(QStringLiteral("91"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("contrasting textures"), QStringLiteral("bounce color"), QStringLiteral("subdivision"), QStringLiteral("bounce light")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Bounce color averaging can be too coarse on contrasting textures."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Large faces with varied texture colors may bounce an averaged color that does not match the visual detail."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Explain this as a lighting-quality limitation and favor authored fill lights when exact bounced color matters.")),
		knownIssue(QStringLiteral("377"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("autominlight"), QStringLiteral("bmodel"), QStringLiteral("minlight")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Global autominlight can override explicit bmodel minlight intent."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Brush-model minlight may not match author expectations when autominlight is enabled."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Warn when global autominlight and bmodel minlight are both present.")),

		knownIssue(QStringLiteral("455"), QString(), CompilerKnownIssueSeverity::Info, false, qbspTool(), qbspProfile(), {QStringLiteral("func_detail_null"), QStringLiteral("compiler-only brush"), QStringLiteral("null detail"), QStringLiteral("shadow helper")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "func_detail_null is a requested compiler-only brush entity."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Advanced lighting or shadow helper brushes cannot be assumed to disappear safely at compile time."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Represent helper geometry as SlopStudio export metadata and show exactly what will reach qbsp.")),
		knownIssue(QStringLiteral("450"), QStringLiteral("D08"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("-notex"), QStringLiteral("non-WAD"), QStringLiteral("missing texture")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Non-WAD compiles can still report missing texture data."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Generated or external-texture BSP workflows may warn even when texture data is intentionally omitted."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Explain -notex/non-WAD profile behavior and separate expected warnings from real missing assets.")),
		knownIssue(QStringLiteral("346"), QStringLiteral("D08"), CompilerKnownIssueSeverity::Info, true, {QStringLiteral("slopmap2-bsp"), QStringLiteral("slopmap2-light")}, {QStringLiteral("slopmap2-bsp"), QStringLiteral("slopmap2-light")}, {QStringLiteral("PCX"), QStringLiteral("PNG"), QStringLiteral("external textures"), QStringLiteral("palette")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "External texture format support is still expanding."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Some custom texture formats may not be accepted by SlopMap2."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Validate replacement textures through SlopStudio asset tools before starting a compile.")),
		knownIssue(QStringLiteral("201"), QStringLiteral("D08"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("-outputdir"), QStringLiteral("-waddir"), QStringLiteral("working directory")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Explicit output and WAD directories are a requested upstream feature."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Wrapper-controlled paths can be fragile when a compiler relies on its working directory."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Run compiles from an isolated, predictable working directory and record expected outputs.")),
		knownIssue(QStringLiteral("192"), QStringLiteral("D08"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("custom palette"), QStringLiteral("palette"), QStringLiteral("bounce lighting")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Light may assume the stock Quake palette in custom projects."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Bounce lighting can be wrong for total conversions with custom palettes."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Add a custom-palette profile warning and require visual QA for relit maps.")),

		knownIssue(QStringLiteral("435"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Error, true, {QStringLiteral("slopmap2-bsputil")}, {QStringLiteral("slopmap2-bsputil")}, {QStringLiteral("argument parsing"), QStringLiteral("dummy argument")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "bsputil argument parsing has an unverified historical upstream report."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "SlopMap2 hardens utility parsing, but its audit does not establish that every operation from ericw-tools #435 is resolved."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Keep operation-specific smoke coverage before relying on BSP-changing commands; retain original inputs and captured diagnostics.")),
		knownIssue(QStringLiteral("482"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Info, false, {QStringLiteral("slopmap2")}, {QStringLiteral("slopmap2")}, {},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Embree 4 and oneTBB have documented platform requirements."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Building SlopMap2 on older Linux distributions may require newer dependencies; this is a build constraint, not a BSP defect."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Use the SlopMap2 build guide to check Embree 4 and oneTBB discovery before building locally.")),
		knownIssue(QStringLiteral("480"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Info, false, hubTool(), hubProfile(), {},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "The hub has documented Qt and graphics platform requirements."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Some Linux Wayland configurations need the Qt XCB fallback described in the SlopMap2 build guide."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Follow the build guide for the Wayland-to-XCB fallback and inspect launch diagnostics; the hub remains optional.")),
		knownIssue(QStringLiteral("449"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Info, false, {QStringLiteral("slopmap2")}, {QStringLiteral("slopmap2")}, {QStringLiteral("std::format"), QStringLiteral("fmt"), QStringLiteral("logging"), QStringLiteral("format")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Upstream logging format internals may change."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Forks or log parsers that depend on third-party formatting details could be fragile."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Parse stable diagnostic content rather than assuming a particular upstream logging library.")),
		knownIssue(QStringLiteral("289"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Info, true, {QStringLiteral("slopmap2-bsputil")}, {QStringLiteral("slopmap2-bsputil")}, {QStringLiteral("bspinfo"), QStringLiteral("diagnostics"), QStringLiteral("light stats"), QStringLiteral("entity extraction")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Richer bspinfo-style diagnostics are requested upstream."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Current tools may not expose every statistic SlopStudio wants in a structured way."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Keep SlopStudio diagnostics pane tolerant of missing fields and preserve raw tool output.")),
		knownIssue(QStringLiteral("225"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Info, false, bsputilTool(), bsputilProfile(), {QStringLiteral("bspinfo"), QStringLiteral("log file"), QStringLiteral("write log"), QStringLiteral("diagnostics log")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "bspinfo does not provide every requested log-file workflow upstream."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Helper-tool diagnostics may only be available through captured process output."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Capture stdout and stderr in SlopStudio manifests and expose them as the durable diagnostic log.")),
		knownIssue(QStringLiteral("223"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Info, false, {QStringLiteral("slopmap2")}, {QStringLiteral("slopmap2")}, {QStringLiteral("Darwin"), QStringLiteral("macOS"), QStringLiteral("package suffix"), QStringLiteral("release artifact")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "macOS release artifact naming is tracked upstream."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Automated tool discovery may encounter package names that use older platform wording."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Match release artifacts by descriptor metadata and checksums instead of display suffix alone.")),
		knownIssue(QStringLiteral("215"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Info, false, {QStringLiteral("slopmap2")}, {QStringLiteral("slopmap2")}, {QStringLiteral("Flatpak"), QStringLiteral("Flathub"), QStringLiteral("Linux package"), QStringLiteral("bundle")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Flatpak packaging is an upstream distribution request."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Linux users may not have a single supported package route for SlopMap2."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Keep SlopStudio setup able to use user-selected executables, submodule builds, or future package channels.")),

		knownIssue(QStringLiteral("464"), QStringLiteral("D10"), CompilerKnownIssueSeverity::Info, false, visTool(), visProfile(), {QStringLiteral("func_viscluster"), QStringLiteral("VIS cluster"), QStringLiteral("optimization")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Manual func_viscluster merging remains a design decision in SlopMap2."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Generic cluster merging can weaken intentional occlusion; the fork keeps conservative visibility behaviour."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Use PRT2 detail clustering or -fast for current workflows; manual clustering needs conservative PVS regression tests.")),
		knownIssue(QStringLiteral("443"), QString(), CompilerKnownIssueSeverity::Info, false, qbspTool(), qbspProfile(), {QStringLiteral("entity order"), QStringLiteral("map hack"), QStringLiteral("custom model"), QStringLiteral("*n model")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Stable entity order for map-hack custom models is requested upstream."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Map hacks that refer to numbered bmodels can break when compiler output ordering changes."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Show entity and bmodel ordering as fragile compatibility data and avoid reordering source entities silently.")),
		knownIssue(QStringLiteral("413"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("translucency"), QStringLiteral("light transmission"), QStringLiteral("tinting"), QStringLiteral("thin material")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Translucency lighting support is requested upstream."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Thin or translucent material lighting cannot be assumed in current SlopMap2 profiles."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Keep translucent-material intent visible in the asset inspector and mark compiler transmission as unsupported unless detected.")),
		knownIssue(QStringLiteral("411"), QString(), CompilerKnownIssueSeverity::Info, false, bsputilTool(), bsputilProfile(), {QStringLiteral("decompiled output"), QStringLiteral("special texture"), QStringLiteral("no nodes"), QStringLiteral("round trip")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "A special decompile texture is requested for round-trip output."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Decompiler helper textures may accidentally generate nodes, clipnodes, or faces if reused in source maps."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Mark decompiled maps as review-required and keep helper textures isolated from normal project assets.")),
		knownIssue(QStringLiteral("205"), QStringLiteral("D10"), CompilerKnownIssueSeverity::Info, false, visTool(), visProfile(), {QStringLiteral("one-way VIS"), QStringLiteral("one-way leaf"), QStringLiteral("window")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "One-way VIS leaves are a requested feature."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Special window-like visibility behavior cannot be assumed in current compiles."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Warn profile authors that this requires engine/compiler support, not just editor metadata.")),
		knownIssue(QStringLiteral("131"), QStringLiteral("D10"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("visapprox"), QStringLiteral("occluding bars"), QStringLiteral("falloff light")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "visapprox can be misled by long falloff lights through occluders."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Approximate lighting may differ from final visibility-aware lighting in complex layouts."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Mark approximate lighting previews as provisional for maps with thin occluders.")),
		knownIssue(QStringLiteral("103"), QStringLiteral("D10"), CompilerKnownIssueSeverity::Info, false, visTool(), visProfile(), {QStringLiteral("hint brush"), QStringLiteral("liquids"), QStringLiteral("horizontal")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Hint brushes have reported VIS edge cases."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Liquid or horizontal hint layouts may not optimize visibility as expected."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Keep hint-brush troubleshooting guidance available from VIS diagnostics.")),

		knownIssue(QStringLiteral("436"), QStringLiteral("D11"), CompilerKnownIssueSeverity::Info, true, lightTool(), lightProfile(), {QStringLiteral("disable lightmaps"), QStringLiteral("fullbright"), QStringLiteral("skip lightmap"), QStringLiteral("lightmap allocation")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Disabling lightmaps per face or brush is requested upstream."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Fullbright or special-case surfaces may still consume normal lightmap handling."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Expose this as a compile-efficiency limitation and keep export-time alternatives separate from compiler behavior.")),
		knownIssue(QStringLiteral("425"), QStringLiteral("D11"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Lightgrid batching is improved in SlopMap2 3.1.0; further optimisation remains open."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Ordered lightgrid ray batches reduce submission overhead while preserving deterministic output; VPL batching remains benchmark-driven work."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Measure remaining lightgrid costs on representative maps before requesting further compiler optimisation.")),
		knownIssue(QStringLiteral("374"), QStringLiteral("D11"), CompilerKnownIssueSeverity::Info, true, lightTool(), lightProfile(), {QStringLiteral("embedded lightmaps"), QStringLiteral("bake lightmaps"), QStringLiteral("diffuse textures"), QStringLiteral("export")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Embedded lightmap baking is a requested upstream feature."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Engines without standard lightmap support may need a separate export transform."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Treat baked diffuse-lightmap output as SlopStudio packaging work unless a compiler version provides native support.")),
		knownIssue(QStringLiteral("249"), QStringLiteral("D11"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("-lmscale"), QStringLiteral("-lmshift"), QStringLiteral("legacy lightmap scale"), QStringLiteral("BSPX")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Legacy lightmap-scale compatibility remains under upstream discussion."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Profiles that mix old `-lmscale` or `-lmshift` behavior with BSPX metadata can confuse compatibility expectations."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Prefer explicit profile wording around modern BSPX metadata and keep legacy flags visible in manifests.")),
		knownIssue(QStringLiteral("343"), QStringLiteral("D12"), CompilerKnownIssueSeverity::Info, true, qbspTool(), qbspProfile(), {QStringLiteral("conditional compile"), QStringLiteral("entity set"), QStringLiteral("build profile")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Conditional entity compilation is not native upstream behavior."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Maps with alternate mod/entity sets need SlopStudio-side build-profile handling."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Route conditional entities through SlopStudio export transforms before qbsp.")),
		knownIssue(QStringLiteral("301"), QStringLiteral("D12"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("hull sizes"), QStringLiteral("total conversion"), QStringLiteral("custom hull")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Custom hull sizes are a requested total-conversion feature."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Nonstandard player hulls may not compile correctly with stock assumptions."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Warn in custom-engine profiles and keep hull settings explicit in project metadata.")),

		knownIssue(QStringLiteral("350"), QString(), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("leak"), QStringLiteral("face splitting"), QStringLiteral("excessive")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Leaking maps can trigger excessive face splitting."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Leak compiles may produce noisy or expensive geometry output."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Prioritize leak repair diagnostics before trusting compile metrics.")),
		knownIssue(QStringLiteral("344"), QString(), CompilerKnownIssueSeverity::Critical, true, qbspTool(), qbspProfile(), {QStringLiteral("bad surface extents"), QStringLiteral("surface extents"), QStringLiteral("crash")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Bad surface extents can create BSPs that crash in-game."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Compiled geometry may be accepted by the tool but fail at runtime."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Run post-compile BSP validation and block packaging on critical surface extent errors.")),
		knownIssue(QStringLiteral("308"), QString(), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("origin key"), QStringLiteral("brush entity"), QStringLiteral("leak")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Brush entities with origin keys can affect leak detection."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Entity origins may cause false or confusing leak results."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Validate brush-entity origin usage before qbsp and point leaks back to source entities.")),
		knownIssue(QStringLiteral("278"), QString(), CompilerKnownIssueSeverity::Critical, true, {QStringLiteral("slopmap2-bsp"), QStringLiteral("slopmap2-light")}, {QStringLiteral("slopmap2-bsp"), QStringLiteral("slopmap2-light")}, {QStringLiteral("corrupt BSP"), QStringLiteral("Hexen2"), QStringLiteral("misidentified")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Corrupt Q1 BSPs can be misidentified as Hexen2."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "A compile/light pipeline may produce a BSP with the wrong detected format."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Inspect BSP headers and lump sanity after compile before registering the artifact.")),
		knownIssue(QStringLiteral("270"), QString(), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("outside filling"), QStringLiteral("24 units"), QStringLiteral("monsters"), QStringLiteral("floor")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Floor-offset monsters may not block outside filling consistently."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Leak/outside-fill results can change around entities placed above the floor."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Show entity placement warnings near outside-fill boundaries and verify the leak path visually.")),
		knownIssue(QStringLiteral("264"), QString(), CompilerKnownIssueSeverity::Critical, true, qbspTool(), qbspProfile(), {QStringLiteral("leafs with content 0"), QStringLiteral("Hexen2"), QStringLiteral("invisible")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Hexen2 BSP2 maps can contain invalid content-zero leafs."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Entities may become invisible in affected BSP output."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Run BSP leaf-content validation for Hexen2/BSP2 profiles.")),
		knownIssue(QStringLiteral("257"), QString(), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("non-integer vertices"), QStringLiteral("missing faces"), QStringLiteral("grid")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Non-integer brush vertices can produce missing faces."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Off-grid geometry may compile with absent faces or wrong contents."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Warn on unsnapped brush vertices and provide a safe snap/repair action.")),
		knownIssue(QStringLiteral("342"), QString(), CompilerKnownIssueSeverity::Warning, false, bsputilTool(), bsputilProfile(), {QStringLiteral("empty texture name"), QStringLiteral("decompile"), QStringLiteral("extract textures"), QStringLiteral("texture scale")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Empty texture names can break decompile and texture extraction workflows."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Round-tripped maps may receive incorrect texture mapping or scale when unnamed textures are present."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Validate BSP texture names before extraction and label decompile output as lossy when names are empty.")),
		knownIssue(QStringLiteral("310"), QString(), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("angle"), QStringLiteral("mangle"), QStringLiteral("Q2 light"), QStringLiteral("sun direction")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Q2 light `angle` can interfere with `mangle`."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Directional lights may ignore the intended mangle when a nonzero angle is also present."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Warn on Q2 light entities that define both keys and ask the user to choose one direction source.")),
		knownIssue(QStringLiteral("298"), QString(), CompilerKnownIssueSeverity::Info, false, qbspTool(), qbspProfile(), {QStringLiteral("mapversion 220"), QStringLiteral("engine warning"), QStringLiteral("metadata"), QStringLiteral("strip mapversion")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Editor mapversion metadata may need export cleanup."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Some engines can warn about `mapversion 220` even when the compiler accepts it."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Offer export cleanup as a SlopStudio transform and keep the original source map unchanged.")),
		knownIssue(QStringLiteral("297"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("_dirt_off_radius"), QStringLiteral("_dirt_on_radius"), QStringLiteral("dirt radius"), QStringLiteral("documentation")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Dirt radius keys need clearer upstream documentation."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Users may not know whether `_dirt_off_radius` and `_dirt_on_radius` are supported in the active compiler build."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Show these keys through profile-aware help only when the selected tool version is known to support them.")),
		knownIssue(QStringLiteral("285"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("constant color"), QStringLiteral("translucency"), QStringLiteral("translucent color"), QStringLiteral("material")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Constant translucency color is requested upstream."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Material workflows cannot assume the compiler can force a fixed transmission color."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Represent constant-color translucency as a future material feature and keep current lighting previews conservative.")),
		knownIssue(QStringLiteral("281"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("-nosunlight"), QStringLiteral("disable sunlight"), QStringLiteral("sunlight override"), QStringLiteral("sun")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "A global no-sunlight option is requested upstream."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Quick compile profiles cannot rely on a native `-nosunlight` switch unless the selected compiler supports it."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Offer SlopStudio-side export/profile alternatives and clearly mark them as local transforms.")),
		knownIssue(QStringLiteral("277"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("alternate position"), QStringLiteral("moving door"), QStringLiteral("secret door"), QStringLiteral("alternate lightmaps")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Alternate-position lighting for moving models is requested upstream."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Animated bmodels may be baked only at their authored position."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Show moving-model lighting as an advanced QA risk and preserve any alternate-lighting metadata separately.")),
		knownIssue(QStringLiteral("254"), QString(), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("-forcegoodtree"), QStringLiteral("planenum"), QStringLiteral("Hexen2"), QStringLiteral("BSP2")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "`-forcegoodtree` can hit a planenum assertion for Hexen2 BSP2."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Risky tree-building flags may crash or fail the compile for some Hexen2 profiles."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Warn on `-forcegoodtree` in Hexen2/BSP2 profiles and suggest a standard qbsp pass first.")),
		knownIssue(QStringLiteral("246"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("brightness"), QStringLiteral("contrast"), QStringLiteral("final lightmap"), QStringLiteral("post adjustment")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Final lightmap brightness and contrast adjustment is requested upstream."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Compile profiles cannot assume the compiler can apply final post-lighting contrast controls."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Keep post-lightmap adjustment as an explicit export or preview operation unless native support appears.")),
		knownIssue(QStringLiteral("241"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("brush as light"), QStringLiteral("light volume"), QStringLiteral("skip brush"), QStringLiteral("lighting design")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Brush-volume lighting is a requested authoring feature."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Brushes used as lighting guides may still affect qbsp unless SlopStudio exports them safely."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Model brush-as-light workflows as editor metadata and show exactly what is stripped before qbsp.")),
		knownIssue(QStringLiteral("234"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("HSV"), QStringLiteral("hue"), QStringLiteral("saturation"), QStringLiteral("value")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Global HSV lightmap adjustment is requested upstream."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Color-grading style controls may not be available in the selected compiler."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Expose global HSV changes as preview/export processing only when the compiler lacks native support.")),
		knownIssue(QStringLiteral("233"), QString(), CompilerKnownIssueSeverity::Warning, false, qbspTool(), qbspProfile(), {QStringLiteral("no valid brushes"), QStringLiteral("func_detail_illusionary"), QStringLiteral("valid brushes"), QStringLiteral("generated map")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "func_detail_illusionary can trigger no-valid-brush diagnostics."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Generated or model-style maps may produce confusing brush-validity failures."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Attach the diagnostic to the source entity and show which brushes are compiler-eligible.")),
		knownIssue(QStringLiteral("222"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("rangescale"), QStringLiteral("Q2"), QStringLiteral("range scale"), QStringLiteral("cleanup")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Q2 rangescale defaults are tracked for upstream cleanup."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Profiles should not depend on undocumented internal rangescale representation."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Keep Q2 light-range assumptions explicit in SlopStudio profile metadata.")),
		knownIssue(QStringLiteral("213"), QString(), CompilerKnownIssueSeverity::Warning, false, qbspTool(), qbspProfile(), {QStringLiteral("-convert valve"), QStringLiteral("valve postfix"), QStringLiteral("destination filename"), QStringLiteral("-valve")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Valve conversion can append an unexpected postfix."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Wrapper-provided destination filenames may still receive a `-valve` suffix from qbsp."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Predict and display the expected output path before launch, then register only the artifact SlopStudio requested.")),
		knownIssue(QStringLiteral("209"), QString(), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("_project_texture"), QStringLiteral("_surface"), QStringLiteral("projected texture"), QStringLiteral("surface light")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Projected textures on surface lights are tracked as broken."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Advanced surface-light projection may not affect lighting as authored."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Warn when `_project_texture` is combined with `_surface` and preserve the setup for upstream regression reporting.")),
		knownIssue(QStringLiteral("200"), QString(), CompilerKnownIssueSeverity::Info, false, qbspTool(), qbspProfile(), {QStringLiteral("random texture tiling"), QStringLiteral("texture randomization"), QStringLiteral("tiling"), QStringLiteral("variation")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Random texture tiling is a requested compiler feature."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Texture variation cannot be assumed at compile time for current SlopMap2 profiles."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Keep random tiling as an editor/export feature unless a selected compiler version advertises support.")),
		knownIssue(QStringLiteral("195"), QString(), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("AD start.bsp"), QStringLiteral("Arcane Dimensions"), QStringLiteral("odd face"), QStringLiteral("visual compatibility")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "A specific Arcane Dimensions face has reported odd lighting or rendering."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Compatibility maps can reveal visual regressions that do not show up as compiler errors."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Treat this as optional compatibility-fixture coverage and keep generated outputs versioned.")),
		knownIssue(QStringLiteral("189"), QString(), CompilerKnownIssueSeverity::Info, false, qbspTool(), qbspProfile(), {QStringLiteral("_noskydeletion"), QStringLiteral("sky brushes"), QStringLiteral("skybox"), QStringLiteral("lightmap workflow")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Preserving brushes inside sky is requested upstream."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Sky-contained helper brushes may be deleted or compiled differently than an advanced skybox workflow expects."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Show sky-deletion assumptions in export previews and keep helper geometry marked as editor-owned.")),
		knownIssue(QStringLiteral("186"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("_falloff"), QStringLiteral("delay 1"), QStringLiteral("delay 2"), QStringLiteral("delay 5")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Falloff control is not available for every requested delay mode."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Light entities using unsupported delay/falloff combinations may not match author expectations."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Validate falloff keys against the active profile and explain unsupported combinations before compile.")),
		knownIssue(QStringLiteral("136"), QString(), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("clipnodes"), QStringLiteral("marksurfaces"), QStringLiteral("txqbsp-xt"), QStringLiteral("compile metrics")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Clipnode and marksurface counts can differ from other compilers."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Maps may approach engine limits sooner with one compiler than another."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Track compile metrics in manifests and show comparisons as guidance rather than claiming compiler equivalence.")),
		knownIssue(QStringLiteral("106"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("specific lights"), QStringLiteral("sunlight bounce"), QStringLiteral("per-light bounce"), QStringLiteral("bounce control")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Per-light and sunlight bounce controls are requested upstream."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Current profiles may not be able to target bounce behavior at the exact light-source level."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Expose per-source bounce as an advanced limitation and keep authored intent visible in the inspector.")),
		knownIssue(QStringLiteral("81"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("flip projected textures"), QStringLiteral("projected texture"), QStringLiteral("texture projection"), QStringLiteral("flip")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Projected texture flipping is requested upstream."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Texture projection controls may lack a native flip operation in current light compiles."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Keep projection orientation editable in SlopStudio metadata and warn when the compiler cannot express it.")),
		knownIssue(QStringLiteral("230"), QString(), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("double extension"), QStringLiteral("dotted filename"), QStringLiteral(".1.bsp")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Dotted BSP filenames can be handled incorrectly by light."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Output paths such as my-map.1.bsp may lose part of the intended name."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Use wrapper-controlled output paths and include dotted filenames in path smoke tests.")),
		knownIssue(QStringLiteral("114"), QString(), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("csg_fail"), QStringLiteral("leaf contents"), QStringLiteral("wrong contents"), QStringLiteral("content type")},
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Known CSG edge cases can assign the wrong leaf contents."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Problematic brush/content layouts may compile with incorrect contents classification."),
			QT_TRANSLATE_NOOP("SlopStudioCompilerKnownIssues", "Run geometry/content preflight fixtures and keep suspicious CSG failures visible in map-health diagnostics.")),
	};
}

QStringList compilerKnownIssueIds()
{
	QStringList ids;
	for (const CompilerKnownIssueDescriptor& issue : compilerKnownIssueDescriptors()) {
		ids.push_back(issue.issueId);
	}
	return ids;
}

bool compilerKnownIssueForId(const QString& issueId, CompilerKnownIssueDescriptor* out)
{
	const QString requested = normalizedId(issueId);
	for (const CompilerKnownIssueDescriptor& issue : compilerKnownIssueDescriptors()) {
		if (normalizedId(issue.issueId) == requested) {
			if (out) {
				*out = issue;
			}
			return true;
		}
	}
	return false;
}

QVector<CompilerKnownIssueDescriptor> compilerKnownIssuesForCluster(const QString& clusterId)
{
	QVector<CompilerKnownIssueDescriptor> issues;
	const QString requested = normalizedId(clusterId);
	for (const CompilerKnownIssueDescriptor& issue : compilerKnownIssueDescriptors()) {
		if (!requested.isEmpty() && normalizedId(issue.clusterId) == requested) {
			issues.push_back(issue);
		}
	}
	return issues;
}

QVector<CompilerKnownIssueDescriptor> compilerKnownIssuesForTool(const QString& toolId)
{
	QVector<CompilerKnownIssueDescriptor> issues;
	for (const CompilerKnownIssueDescriptor& issue : compilerKnownIssueDescriptors()) {
		if (containsNormalizedId(issue.affectedToolIds, toolId)) {
			issues.push_back(issue);
		}
	}
	return issues;
}

QVector<CompilerKnownIssueDescriptor> compilerKnownIssuesForProfile(const QString& profileId)
{
	QVector<CompilerKnownIssueDescriptor> issues;
	for (const CompilerKnownIssueDescriptor& issue : compilerKnownIssueDescriptors()) {
		if (containsNormalizedId(issue.affectedProfileIds, profileId)) {
			issues.push_back(issue);
		}
	}
	return issues;
}

QVector<CompilerKnownIssueMatch> matchCompilerKnownIssues(const QString& text, const QString& toolId, const QString& profileId)
{
	QVector<CompilerKnownIssueMatch> matches;
	if (text.trimmed().isEmpty()) {
		return matches;
	}

	for (const CompilerKnownIssueDescriptor& issue : compilerKnownIssueDescriptors()) {
		if (!matchesScope(issue, toolId, profileId)) {
			continue;
		}
		for (const QString& keyword : issue.matchKeywords) {
			if (textContainsKeyword(text, keyword)) {
				matches.push_back({issue, keyword});
				break;
			}
		}
	}
	return matches;
}

QString compilerKnownIssueText(const CompilerKnownIssueDescriptor& issue)
{
	QStringList lines;
	lines << QCoreApplication::translate("SlopStudioCompilerKnownIssues", "ericw-tools issue #%1").arg(issue.issueId);
	if (!issue.clusterId.isEmpty()) {
		lines << QCoreApplication::translate("SlopStudioCompilerKnownIssues", "Cluster: %1").arg(issue.clusterId);
	}
	lines << QCoreApplication::translate("SlopStudioCompilerKnownIssues", "Severity: %1").arg(compilerKnownIssueSeverityText(issue.severity));
	lines << QCoreApplication::translate("SlopStudioCompilerKnownIssues", "High value: %1").arg(issue.highValue ? QCoreApplication::translate("SlopStudioCompilerKnownIssues", "yes") : QCoreApplication::translate("SlopStudioCompilerKnownIssues", "no"));
	if (!issue.affectedToolIds.isEmpty()) {
		lines << QCoreApplication::translate("SlopStudioCompilerKnownIssues", "Tools: %1").arg(issue.affectedToolIds.join(QStringLiteral(", ")));
	}
	if (!issue.affectedProfileIds.isEmpty()) {
		lines << QCoreApplication::translate("SlopStudioCompilerKnownIssues", "Profiles: %1").arg(issue.affectedProfileIds.join(QStringLiteral(", ")));
	}
	lines << issue.summaryText;
	lines << issue.warningText;
	lines << issue.actionText;
	lines << issue.upstreamUrl;
	return lines.join('\n');
}

QStringList slopmap2KnownIssuePlanWarnings(const QString& profileId, const QString& inputPath, const QStringList& arguments)
{
	QStringList warnings;
	if (!profileId.startsWith(QStringLiteral("slopmap2-"), Qt::CaseInsensitive)) {
		return warnings;
	}

	// Only genuinely matched, actionable issues belong here. The "N tracked issues" summary is
	// informational and is surfaced through the plan's known-issue notes instead, so that merely
	// having a catalog entry can never downgrade a clean run to Warning.
	QStringList appendedIssueIds;
	auto appendIssue = [&warnings, &appendedIssueIds](const QString& issueId) {
		CompilerKnownIssueDescriptor issue;
		if (compilerKnownIssueForId(issueId, &issue) && !appendedIssueIds.contains(issue.issueId)) {
			appendedIssueIds << issue.issueId;
			warnings << QCoreApplication::translate("SlopStudioCompilerKnownIssues", "ericw-tools #%1: %2 Action: %3").arg(issue.issueId, issue.warningText, issue.actionText);
		}
	};

	const QString context = QStringLiteral("%1\n%2").arg(inputPath, arguments.join(QLatin1Char('\n')));
	for (const CompilerKnownIssueMatch& match : matchCompilerKnownIssues(context, QString(), profileId)) {
		if (match.issue.highValue) {
			appendIssue(match.issue.issueId);
		}
	}

	if (profileId.compare(QStringLiteral("slopmap2-light"), Qt::CaseInsensitive) == 0 && QFileInfo(inputPath).completeBaseName().contains('.')) {
		appendIssue(QStringLiteral("230"));
	}
	if (profileId.compare(QStringLiteral("slopmap2-light"), Qt::CaseInsensitive) == 0
		&& (arguments.contains(QStringLiteral("-bspxhdr"), Qt::CaseInsensitive) || arguments.contains(QStringLiteral("-bspxlux"), Qt::CaseInsensitive) || arguments.contains(QStringLiteral("-wrnormals"), Qt::CaseInsensitive))) {
		appendIssue(QStringLiteral("484"));
	}
	if (profileId.compare(QStringLiteral("slopmap2-bsp"), Qt::CaseInsensitive) == 0 && arguments.contains(QStringLiteral("-notex"), Qt::CaseInsensitive)) {
		appendIssue(QStringLiteral("450"));
	}

	warnings.removeDuplicates();
	return warnings;
}

} // namespace slopstudio
