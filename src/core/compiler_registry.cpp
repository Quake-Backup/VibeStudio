#include "core/compiler_registry.h"

#include "core/studio_manifest.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QStringDecoder>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace slopstudio {

namespace {

QString executableName(const QString& baseName)
{
#if defined(Q_OS_WIN)
	return baseName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) ? baseName : QStringLiteral("%1.exe").arg(baseName);
#else
	return baseName;
#endif
}

QString normalizedId(const QString& value)
{
	return value.trimmed().toLower().replace('_', '-');
}

QString absolutePath(const QString& rootPath, const QString& relativePath)
{
	const QString root = rootPath.trimmed().isEmpty() ? QDir::currentPath() : rootPath;
	return QDir::cleanPath(QDir(root).absoluteFilePath(relativePath));
}

QString absoluteExecutableOverride(const QString& executablePath, const QString& rootPath)
{
	const QString trimmed = executablePath.trimmed();
	if (trimmed.isEmpty()) {
		return {};
	}
	const QFileInfo info(trimmed);
	const QString absolutePath = info.isAbsolute() ? info.absoluteFilePath() : QDir(rootPath).absoluteFilePath(trimmed);
	return QDir::cleanPath(absolutePath);
}

QString findExecutableOverride(const CompilerToolDescriptor& descriptor, const QString& rootPath, const QVector<CompilerToolPathOverride>& overrides, QStringList* warnings)
{
	const QString descriptorId = normalizedId(descriptor.id);
	for (const CompilerToolPathOverride& override : overrides) {
		if (normalizedId(override.toolId) != descriptorId) {
			continue;
		}
		const QString candidate = absoluteExecutableOverride(override.executablePath, rootPath);
		if (candidate.isEmpty()) {
			continue;
		}
		if (QFileInfo(candidate).isFile()) {
			return candidate;
		}
		if (warnings) {
			warnings->push_back(QCoreApplication::translate("SlopStudioCompilerRegistry", "Configured compiler executable does not exist: %1").arg(QDir::toNativeSeparators(candidate)));
		}
		return {};
	}
	return {};
}

QString findExecutableCandidate(const CompilerToolDescriptor& descriptor, const QString& rootPath, const QStringList& extraSearchPaths)
{
	for (const QString& relativePath : descriptor.candidateRelativePaths) {
		const QString candidate = absolutePath(rootPath, relativePath);
		if (QFileInfo(candidate).isFile()) {
			return candidate;
		}
	}

	for (const QString& searchPath : extraSearchPaths) {
		for (const QString& executable : descriptor.executableNames) {
			const QString candidate = QDir(searchPath).absoluteFilePath(executable);
			if (QFileInfo(candidate).isFile()) {
				return QDir::cleanPath(candidate);
			}
		}
	}

	for (const QString& executable : descriptor.executableNames) {
		const QString found = QStandardPaths::findExecutable(executable);
		if (!found.isEmpty()) {
			return QDir::cleanPath(found);
		}
	}

	return {};
}

QString quoteCommandPart(const QString& part)
{
	if (part.isEmpty()) {
		return QStringLiteral("\"\"");
	}
	bool needsQuotes = false;
	for (const QChar ch : part) {
		if (ch.isSpace() || ch == '"' || ch == '\'') {
			needsQuotes = true;
			break;
		}
	}
	if (!needsQuotes) {
		return part;
	}
	QString escaped = part;
	escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
	escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
	return QStringLiteral("\"%1\"").arg(escaped);
}

QString commandLineText(const QString& program, const QStringList& arguments)
{
	QStringList parts;
	if (!program.isEmpty()) {
		parts << quoteCommandPart(program);
	}
	for (const QString& argument : arguments) {
		parts << quoteCommandPart(argument);
	}
	return parts.join(' ');
}

// SlopMap2 builds each tool under build/src/<tool directory>, inside a Release folder for
// multi-config generators, and installs every executable flat into the install prefix
// (external/compilers/slopmap2/src/*/CMakeLists.txt).
QStringList slopmap2ToolCandidatePaths(const QString& sourceDirectory, const QStringList& baseNames)
{
	const QStringList directories = {
		QStringLiteral("external/compilers/slopmap2/build/src/%1").arg(sourceDirectory),
		QStringLiteral("external/compilers/slopmap2/build/src/%1/Release").arg(sourceDirectory),
		QStringLiteral("external/compilers/slopmap2/install"),
	};
	QStringList paths;
	for (const QString& directory : directories) {
		for (const QString& baseName : baseNames) {
			paths << QStringLiteral("%1/%2.exe").arg(directory, baseName) << QStringLiteral("%1/%2").arg(directory, baseName);
		}
	}
	return paths;
}

// SlopMap3 presets build into build/<preset>/bin (external/compilers/slopmap3/CMakePresets.json)
// and install into <prefix>/bin.
QStringList slopmap3CandidatePaths(const QStringList& baseNames)
{
	const QStringList directories = {
		QStringLiteral("external/compilers/slopmap3/build/release/bin"),
		QStringLiteral("external/compilers/slopmap3/build/cli/bin"),
		QStringLiteral("external/compilers/slopmap3/build/cpu-only/bin"),
		QStringLiteral("external/compilers/slopmap3/install/bin"),
	};
	QStringList paths;
	for (const QString& directory : directories) {
		for (const QString& baseName : baseNames) {
			paths << QStringLiteral("%1/%2.exe").arg(directory, baseName) << QStringLiteral("%1/%2").arg(directory, baseName);
		}
	}
	return paths;
}

QString firstUsefulProbeLine(const QString& text)
{
	const QStringList lines = text.split('\n');
	for (QString line : lines) {
		line = line.trimmed();
		if (!line.isEmpty()) {
			return line.left(240);
		}
	}
	return {};
}

// SlopMap2 prints "---- <tool> / SlopMap2 <version> ----" (src/common/settings.cc), SlopMap3
// answers --version with "SlopMap3 <version> (NRC <revision>)" (tools/quake3/q3map2/main.cpp),
// ZDBSP prints "ZDBSP <version> (...)" and ZokumBSP prints "ZokumBSP Version: <version> ...".
// The pre-rename banners (VibeyMapTools, q3mapx) and their upstreams' (ericw-tools, q3map2)
// are still recognised for executables configured by hand.
QString decodeProbeOutput(const QByteArray& bytes)
{
	if (bytes.isEmpty()) {
		return {};
	}
	QStringDecoder utf8(QStringConverter::Utf8);
	const QString decoded = utf8.decode(bytes);
	if (utf8.hasError()) {
		return QString::fromLocal8Bit(bytes);
	}
	return decoded;
}

QString versionBannerLine(const QString& text)
{
	static const QRegularExpression bannerPattern(QStringLiteral(R"regex((?:slopmap2|slopmap3|vibeymaptools|q3mapx|ericw-tools|zdbsp|zokumbsp|zennode|q3map|netradiant|version)\b)regex"), QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression numberPattern(QStringLiteral(R"regex(\b\d+\.\d+)regex"));
	const QStringList lines = text.split('\n');
	for (QString line : lines) {
		line = line.trimmed();
		if (line.isEmpty()) {
			continue;
		}
		if (bannerPattern.match(line).hasMatch() && numberPattern.match(line).hasMatch()) {
			return line.left(240);
		}
	}
	return {};
}

void probeCompilerVersion(CompilerToolDiscovery* discovery, int startTimeoutMs, int finishTimeoutMs)
{
	if (!discovery || !discovery->executableAvailable || !discovery->descriptor.versionProbeSupported) {
		return;
	}

	discovery->versionProbeAttempted = true;
	discovery->versionProbeCommandLine = commandLineText(discovery->executablePath, discovery->descriptor.versionProbeArguments);

	QProcess process;
	process.setProgram(discovery->executablePath);
	process.setArguments(discovery->descriptor.versionProbeArguments);
	process.setProcessChannelMode(QProcess::MergedChannels);
	process.start();
	if (!process.waitForStarted(std::max(100, startTimeoutMs))) {
		discovery->versionProbeOutcome = CompilerVersionProbeOutcome::Failed;
		discovery->warnings << QCoreApplication::translate("SlopStudioCompilerRegistry", "Version probe could not start.");
		return;
	}
	if (!process.waitForFinished(std::max(250, finishTimeoutMs))) {
		process.kill();
		process.waitForFinished(500);
		discovery->versionProbeOutcome = CompilerVersionProbeOutcome::Failed;
		discovery->warnings << QCoreApplication::translate("SlopStudioCompilerRegistry", "Version probe timed out.");
		return;
	}

	discovery->versionProbeExitCode = process.exitCode();
	// SlopMap2 formats its banner with fmt and emits UTF-8; SlopMap3 (like q3map2) echoes
	// the narrow argv it was handed, which on Windows is the ANSI codepage. Decoding
	// UTF-8 first with a local-8-bit fallback reads both correctly, and matches
	// what the compiler runner does with streamed output.
	const QString output = decodeProbeOutput(process.readAllStandardOutput());
	if (process.exitStatus() != QProcess::NormalExit) {
		discovery->versionProbeOutcome = CompilerVersionProbeOutcome::Failed;
		discovery->warnings << QCoreApplication::translate("SlopStudioCompilerRegistry", "Version probe crashed.");
		return;
	}

	const QString banner = versionBannerLine(output);
	if (!banner.isEmpty()) {
		discovery->versionText = banner;
		discovery->versionAvailable = true;
		discovery->versionProbeOutcome = CompilerVersionProbeOutcome::Probed;
		return;
	}

	const QString usefulLine = firstUsefulProbeLine(output);
	if (!usefulLine.isEmpty()) {
		// Output arrived but no recognisable banner; report it as inferred rather than claiming success.
		discovery->versionText = usefulLine;
		discovery->versionAvailable = false;
		discovery->versionProbeOutcome = CompilerVersionProbeOutcome::Inferred;
		discovery->warnings << QCoreApplication::translate("SlopStudioCompilerRegistry", "Version probe produced output without a recognizable version banner; the recorded text is a best guess.");
		return;
	}

	discovery->versionProbeOutcome = CompilerVersionProbeOutcome::Failed;
	discovery->warnings << QCoreApplication::translate("SlopStudioCompilerRegistry", "Version probe produced no usable output (exit code %1).").arg(discovery->versionProbeExitCode);
}

CompilerToolDescriptor tool(
	const QString& id,
	const QString& integrationId,
	const QString& displayName,
	const QString& engineFamily,
	const QString& role,
	const QString& sourcePath,
	const QStringList& baseExecutableNames,
	const QStringList& candidateRelativePaths,
	const QStringList& versionProbeArguments,
	const QStringList& capabilityFlags,
	const QStringList& readinessWarnings = {},
	bool versionProbeSupported = true)
{
	QStringList executableNames;
	for (const QString& name : baseExecutableNames) {
		executableNames << executableName(name);
	}
	return {id, integrationId, displayName, engineFamily, role, sourcePath, executableNames, candidateRelativePaths, versionProbeArguments, capabilityFlags, readinessWarnings, versionProbeSupported};
}

} // namespace

QString compilerVersionProbeOutcomeId(CompilerVersionProbeOutcome outcome)
{
	switch (outcome) {
	case CompilerVersionProbeOutcome::Probed:
		return QStringLiteral("probed");
	case CompilerVersionProbeOutcome::Inferred:
		return QStringLiteral("inferred");
	case CompilerVersionProbeOutcome::Failed:
		return QStringLiteral("failed");
	case CompilerVersionProbeOutcome::NotAttempted:
		break;
	}
	return QStringLiteral("not-attempted");
}

QString compilerVersionProbeOutcomeText(CompilerVersionProbeOutcome outcome)
{
	switch (outcome) {
	case CompilerVersionProbeOutcome::Probed:
		return QCoreApplication::translate("SlopStudioCompilerRegistry", "Probed");
	case CompilerVersionProbeOutcome::Inferred:
		return QCoreApplication::translate("SlopStudioCompilerRegistry", "Inferred");
	case CompilerVersionProbeOutcome::Failed:
		return QCoreApplication::translate("SlopStudioCompilerRegistry", "Failed");
	case CompilerVersionProbeOutcome::NotAttempted:
		break;
	}
	return QCoreApplication::translate("SlopStudioCompilerRegistry", "Not attempted");
}

OperationState CompilerToolDiscovery::state() const
{
	if (executableAvailable) {
		return OperationState::Completed;
	}
	if (sourceAvailable) {
		return OperationState::Warning;
	}
	return OperationState::Failed;
}

OperationState CompilerRegistrySummary::overallState() const
{
	if (tools.isEmpty()) {
		return OperationState::Idle;
	}
	if (executableAvailableCount == tools.size()) {
		return OperationState::Completed;
	}
	if (sourceAvailableCount > 0 || executableAvailableCount > 0) {
		return OperationState::Warning;
	}
	return OperationState::Failed;
}

QVector<CompilerToolDescriptor> compilerToolDescriptors()
{
	return {
		tool(QStringLiteral("slopmap2-bsp"), QStringLiteral("slopmap2"), QCoreApplication::translate("SlopStudioCompilerRegistry", "SlopMap2 bsp"), QStringLiteral("idTech2"), QCoreApplication::translate("SlopStudioCompilerRegistry", "Quake BSP compiler"), QStringLiteral("external/compilers/slopmap2"), {QStringLiteral("slopmap2-bsp"), QStringLiteral("vmt-bsp")}, slopmap2ToolCandidatePaths(QStringLiteral("qbsp"), {QStringLiteral("slopmap2-bsp"), QStringLiteral("vmt-bsp")}), {}, {QStringLiteral("quake-map-to-bsp"), QStringLiteral("bsp2"), QStringLiteral("lit-support"), QStringLiteral("slopmap2")}),
		tool(QStringLiteral("slopmap2-vis"), QStringLiteral("slopmap2"), QCoreApplication::translate("SlopStudioCompilerRegistry", "SlopMap2 vis"), QStringLiteral("idTech2"), QCoreApplication::translate("SlopStudioCompilerRegistry", "Quake visibility compiler"), QStringLiteral("external/compilers/slopmap2"), {QStringLiteral("slopmap2-vis"), QStringLiteral("vmt-vis")}, slopmap2ToolCandidatePaths(QStringLiteral("vis"), {QStringLiteral("slopmap2-vis"), QStringLiteral("vmt-vis")}), {}, {QStringLiteral("quake-vis"), QStringLiteral("fastvis"), QStringLiteral("slopmap2")}),
		tool(QStringLiteral("slopmap2-light"), QStringLiteral("slopmap2"), QCoreApplication::translate("SlopStudioCompilerRegistry", "SlopMap2 light"), QStringLiteral("idTech2"), QCoreApplication::translate("SlopStudioCompilerRegistry", "Quake light compiler"), QStringLiteral("external/compilers/slopmap2"), {QStringLiteral("slopmap2-light"), QStringLiteral("vmt-light")}, slopmap2ToolCandidatePaths(QStringLiteral("light"), {QStringLiteral("slopmap2-light"), QStringLiteral("vmt-light")}), {}, {QStringLiteral("quake-light"), QStringLiteral("bounce-light"), QStringLiteral("lit-output"), QStringLiteral("slopmap2")}),
		tool(QStringLiteral("slopmap2-bspinfo"), QStringLiteral("slopmap2"), QCoreApplication::translate("SlopStudioCompilerRegistry", "SlopMap2 bspinfo"), QStringLiteral("idTech2"), QCoreApplication::translate("SlopStudioCompilerRegistry", "Quake BSP inspection helper"), QStringLiteral("external/compilers/slopmap2"), {QStringLiteral("slopmap2-bspinfo"), QStringLiteral("vmt-bspinfo")}, slopmap2ToolCandidatePaths(QStringLiteral("bspinfo"), {QStringLiteral("slopmap2-bspinfo"), QStringLiteral("vmt-bspinfo")}), {}, {QStringLiteral("bsp-inspection"), QStringLiteral("bsp-metadata"), QStringLiteral("captured-output-log"), QStringLiteral("slopmap2-helper"), QStringLiteral("slopmap2"), QStringLiteral("helper-probe-limited"), QStringLiteral("upstream-issue-225"), QStringLiteral("upstream-issue-289")}, {QCoreApplication::translate("SlopStudioCompilerRegistry", "Helper discovery currently verifies executable presence and help/version output only; operation-level bspinfo diagnostics still need smoke-test coverage before automation depends on them.")}),
		tool(QStringLiteral("slopmap2-bsputil"), QStringLiteral("slopmap2"), QCoreApplication::translate("SlopStudioCompilerRegistry", "SlopMap2 bsputil"), QStringLiteral("idTech2"), QCoreApplication::translate("SlopStudioCompilerRegistry", "Quake BSP utility helper"), QStringLiteral("external/compilers/slopmap2"), {QStringLiteral("slopmap2-bsputil"), QStringLiteral("vmt-bsputil")}, slopmap2ToolCandidatePaths(QStringLiteral("bsputil"), {QStringLiteral("slopmap2-bsputil"), QStringLiteral("vmt-bsputil")}), {}, {QStringLiteral("bsp-utility"), QStringLiteral("bsp-inspection"), QStringLiteral("bsp-mutation-risk"), QStringLiteral("slopmap2-helper"), QStringLiteral("slopmap2"), QStringLiteral("helper-probe-limited"), QStringLiteral("argument-parser-risk"), QStringLiteral("upstream-issue-289"), QStringLiteral("upstream-issue-435")}, {QCoreApplication::translate("SlopStudioCompilerRegistry", "The historical bsputil argument-parsing report (#435) is not explicitly resolved by the SlopMap2 audit; keep BSP-changing operations covered by operation-specific smoke tests."), QCoreApplication::translate("SlopStudioCompilerRegistry", "Helper discovery currently verifies executable presence and help/version output only; operation-level bsputil diagnostics still need smoke-test coverage before automation depends on them.")}),
		tool(QStringLiteral("slopmap2-hub"), QStringLiteral("slopmap2"), QCoreApplication::translate("SlopStudioCompilerRegistry", "SlopMap2 hub"), QStringLiteral("idTech2"), QCoreApplication::translate("SlopStudioCompilerRegistry", "Quake build and lighting preview hub"), QStringLiteral("external/compilers/slopmap2"), {QStringLiteral("slopmap2-hub"), QStringLiteral("vmt-hub")}, slopmap2ToolCandidatePaths(QStringLiteral("hub"), {QStringLiteral("slopmap2-hub"), QStringLiteral("vmt-hub")}), {}, {QStringLiteral("build-hub"), QStringLiteral("lighting-preview"), QStringLiteral("gui-helper"), QStringLiteral("platform-launch-constraints"), QStringLiteral("isolated-preview-workspace"), QStringLiteral("slopmap2-helper"), QStringLiteral("slopmap2"), QStringLiteral("helper-probe-limited"), QStringLiteral("upstream-issue-480")}, {QCoreApplication::translate("SlopStudioCompilerRegistry", "Hub launch readiness is unverified by SlopStudio. SlopMap2 documents Qt/graphics platform requirements and a Wayland-to-XCB fallback (#480); preview workspace isolation is fixed in SlopMap2 3.1.0."), QCoreApplication::translate("SlopStudioCompilerRegistry", "Helper discovery currently verifies executable presence only for the hub; SlopStudio does not launch GUI helpers during registry probes.")}, false),
		tool(QStringLiteral("slopmap3"), QStringLiteral("slopmap3"), QCoreApplication::translate("SlopStudioCompilerRegistry", "SlopMap3"), QStringLiteral("idTech3"), QCoreApplication::translate("SlopStudioCompilerRegistry", "Quake III BSP compiler"), QStringLiteral("external/compilers/slopmap3"), {QStringLiteral("slopmap3"), QStringLiteral("q3mapx")}, slopmap3CandidatePaths({QStringLiteral("slopmap3"), QStringLiteral("q3mapx")}), {QStringLiteral("--version")}, {QStringLiteral("idtech3-bsp"), QStringLiteral("meta"), QStringLiteral("vis"), QStringLiteral("light"), QStringLiteral("shader-aware"), QStringLiteral("slopmap3")}),
		tool(QStringLiteral("zdbsp"), QStringLiteral("zdbsp"), QCoreApplication::translate("SlopStudioCompilerRegistry", "ZDBSP"), QStringLiteral("idTech1"), QCoreApplication::translate("SlopStudioCompilerRegistry", "Doom node builder"), QStringLiteral("external/compilers/zdbsp"), {QStringLiteral("zdbsp")}, {QStringLiteral("external/compilers/zdbsp/build/zdbsp.exe"), QStringLiteral("external/compilers/zdbsp/build/zdbsp"), QStringLiteral("external/compilers/zdbsp/zdbsp.exe"), QStringLiteral("external/compilers/zdbsp/zdbsp")}, {QStringLiteral("-V")}, {QStringLiteral("doom-nodes"), QStringLiteral("extended-nodes"), QStringLiteral("gl-nodes")}),
		tool(QStringLiteral("zokumbsp"), QStringLiteral("zokumbsp"), QCoreApplication::translate("SlopStudioCompilerRegistry", "ZokumBSP"), QStringLiteral("idTech1"), QCoreApplication::translate("SlopStudioCompilerRegistry", "Doom node/blockmap/reject builder"), QStringLiteral("external/compilers/zokumbsp"), {QStringLiteral("zokumbsp"), QStringLiteral("zennode")}, {QStringLiteral("external/compilers/zokumbsp/build/zokumbsp.exe"), QStringLiteral("external/compilers/zokumbsp/build/zokumbsp"), QStringLiteral("external/compilers/zokumbsp/src/zokumbsp/zokumbsp.exe"), QStringLiteral("external/compilers/zokumbsp/src/zokumbsp/zokumbsp"), QStringLiteral("external/compilers/zokumbsp/src/zokumbsp/zennode.exe"), QStringLiteral("external/compilers/zokumbsp/src/zokumbsp/zennode")}, {}, {QStringLiteral("doom-nodes"), QStringLiteral("blockmap"), QStringLiteral("reject"), QStringLiteral("visplane-aware")}),
	};
}

bool compilerToolDescriptorForId(const QString& id, CompilerToolDescriptor* out)
{
	const QString requested = normalizedId(id);
	for (const CompilerToolDescriptor& descriptor : compilerToolDescriptors()) {
		if (normalizedId(descriptor.id) == requested) {
			if (out) {
				*out = descriptor;
			}
			return true;
		}
	}
	return false;
}

QString renamedCompilerId(const QString& retiredId)
{
	static const QHash<QString, QString> renames = {
		{QStringLiteral("ericw-qbsp"), QStringLiteral("slopmap2-bsp")},
		{QStringLiteral("ericw-vis"), QStringLiteral("slopmap2-vis")},
		{QStringLiteral("ericw-light"), QStringLiteral("slopmap2-light")},
		{QStringLiteral("ericw-bspinfo"), QStringLiteral("slopmap2-bspinfo")},
		{QStringLiteral("ericw-bsputil"), QStringLiteral("slopmap2-bsputil")},
		{QStringLiteral("ericw-bsputil-check"), QStringLiteral("slopmap2-bsputil-check")},
		{QStringLiteral("ericw-bsputil-extract-entities"), QStringLiteral("slopmap2-bsputil-extract-entities")},
		{QStringLiteral("ericw-bsputil-extract-textures"), QStringLiteral("slopmap2-bsputil-extract-textures")},
		{QStringLiteral("ericw-lightpreview"), QStringLiteral("slopmap2-hub")},
		{QStringLiteral("q3map2"), QStringLiteral("slopmap3")},
		{QStringLiteral("q3map2-probe"), QStringLiteral("slopmap3-probe")},
		{QStringLiteral("q3map2-bsp"), QStringLiteral("slopmap3-bsp")},
		{QStringLiteral("q3map2-vis"), QStringLiteral("slopmap3-vis")},
		{QStringLiteral("q3map2-light"), QStringLiteral("slopmap3-light")},
		{QStringLiteral("q3map2-convert"), QStringLiteral("slopmap3-convert")},
		{QStringLiteral("q3map2-pk3"), QStringLiteral("slopmap3-pk3")},
	};
	return renames.value(normalizedId(retiredId));
}

QString unknownCompilerToolIdText(const QString& id)
{
	const QString renamed = renamedCompilerId(id);
	if (!renamed.isEmpty()) {
		return QCoreApplication::translate("SlopStudioCompilerRegistry", "Unknown compiler tool id: %1. It was renamed %2 when SlopStudio moved to SlopMap2 and SlopMap3.").arg(id, renamed);
	}
	return QCoreApplication::translate("SlopStudioCompilerRegistry", "Unknown compiler tool id: %1").arg(id);
}

CompilerRegistrySummary discoverCompilerTools(const QString& workspaceRootPath, const QStringList& extraSearchPaths)
{
	CompilerRegistryOptions options;
	options.workspaceRootPath = workspaceRootPath;
	options.extraSearchPaths = extraSearchPaths;
	return discoverCompilerTools(options);
}

CompilerRegistrySummary discoverCompilerTools(const CompilerRegistryOptions& options)
{
	CompilerRegistrySummary summary;
	const QString rootPath = options.workspaceRootPath.trimmed().isEmpty() ? QDir::currentPath() : options.workspaceRootPath;

	for (const CompilerToolDescriptor& descriptor : compilerToolDescriptors()) {
		CompilerToolDiscovery discovery;
		discovery.descriptor = descriptor;
		discovery.capabilityFlags = descriptor.capabilityFlags;
		discovery.warnings.append(descriptor.readinessWarnings);
		discovery.sourcePath = absolutePath(rootPath, descriptor.sourcePath);
		discovery.sourceAvailable = QFileInfo(discovery.sourcePath).isDir();
		discovery.executablePath = findExecutableOverride(descriptor, rootPath, options.executableOverrides, &discovery.warnings);
		discovery.executablePathOverridden = !discovery.executablePath.isEmpty();
		if (discovery.executablePath.isEmpty()) {
			discovery.executablePath = findExecutableCandidate(descriptor, rootPath, options.extraSearchPaths);
		}
		discovery.executableAvailable = !discovery.executablePath.isEmpty();
		if (options.probeVersions) {
			probeCompilerVersion(&discovery, options.versionProbeStartTimeoutMs, options.versionProbeTimeoutMs);
		}
		if (!discovery.sourceAvailable) {
			discovery.warnings << QCoreApplication::translate("SlopStudioCompilerRegistry", "Compiler source directory is missing.");
		}
		if (!discovery.executableAvailable) {
			discovery.warnings << QCoreApplication::translate("SlopStudioCompilerRegistry", "Compiler executable was not found in known build paths or PATH.");
		}

		if (discovery.sourceAvailable) {
			++summary.sourceAvailableCount;
		}
		if (discovery.executableAvailable) {
			++summary.executableAvailableCount;
		}
		summary.warningCount += discovery.warnings.size();
		summary.tools.push_back(discovery);
	}

	return summary;
}

QString compilerRegistrySummaryText(const CompilerRegistrySummary& summary)
{
	QStringList lines;
	lines << QCoreApplication::translate("SlopStudioCompilerRegistry", "Compiler registry");
	lines << QCoreApplication::translate("SlopStudioCompilerRegistry", "Tools: %1").arg(summary.tools.size());
	lines << QCoreApplication::translate("SlopStudioCompilerRegistry", "Source available: %1").arg(summary.sourceAvailableCount);
	lines << QCoreApplication::translate("SlopStudioCompilerRegistry", "Executables available: %1").arg(summary.executableAvailableCount);
	lines << QCoreApplication::translate("SlopStudioCompilerRegistry", "Warnings: %1").arg(summary.warningCount);
	for (const CompilerToolDiscovery& discovery : summary.tools) {
		lines << QString();
		lines << QStringLiteral("%1 [%2]").arg(discovery.descriptor.displayName, discovery.descriptor.id);
		lines << QCoreApplication::translate("SlopStudioCompilerRegistry", "Engine: %1").arg(discovery.descriptor.engineFamily);
		lines << QCoreApplication::translate("SlopStudioCompilerRegistry", "Role: %1").arg(discovery.descriptor.role);
		lines << QCoreApplication::translate("SlopStudioCompilerRegistry", "Source: %1").arg(QDir::toNativeSeparators(discovery.sourcePath));
		lines << QCoreApplication::translate("SlopStudioCompilerRegistry", "Executable: %1").arg(discovery.executablePath.isEmpty() ? QCoreApplication::translate("SlopStudioCompilerRegistry", "not found") : QDir::toNativeSeparators(discovery.executablePath));
		lines << QCoreApplication::translate("SlopStudioCompilerRegistry", "Executable override: %1").arg(discovery.executablePathOverridden ? QCoreApplication::translate("SlopStudioCompilerRegistry", "yes") : QCoreApplication::translate("SlopStudioCompilerRegistry", "no"));
		lines << QCoreApplication::translate("SlopStudioCompilerRegistry", "Version: %1").arg(discovery.versionText.isEmpty() ? QCoreApplication::translate("SlopStudioCompilerRegistry", "unknown") : discovery.versionText);
		lines << QCoreApplication::translate("SlopStudioCompilerRegistry", "Version probe: %1").arg(compilerVersionProbeOutcomeText(discovery.versionProbeOutcome));
		lines << QCoreApplication::translate("SlopStudioCompilerRegistry", "Capabilities: %1").arg(discovery.capabilityFlags.isEmpty() ? QCoreApplication::translate("SlopStudioCompilerRegistry", "none") : discovery.capabilityFlags.join(QStringLiteral(", ")));
		for (const QString& warning : discovery.warnings) {
			lines << QCoreApplication::translate("SlopStudioCompilerRegistry", "Warning: %1").arg(warning);
		}
	}
	return lines.join('\n');
}

} // namespace slopstudio
