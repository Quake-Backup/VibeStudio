#include "core/build_program_trust.h"

#include "core/build_pipeline.h"
#include "core/compiler_profiles.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>

#include <algorithm>

namespace vibestudio {

namespace {

QString cleanAbsolute(const QString& path)
{
	if (path.trimmed().isEmpty()) {
		return {};
	}
	const QFileInfo info(path.trimmed());
	const QString canonical = info.canonicalFilePath();
	return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
}

bool samePath(const QString& left, const QString& right)
{
	const QString a = cleanAbsolute(left);
	const QString b = cleanAbsolute(right);
	if (a.isEmpty() || b.isEmpty()) {
		return false;
	}
#ifdef Q_OS_WIN
	return a.compare(b, Qt::CaseInsensitive) == 0;
#else
	return a == b;
#endif
}

bool insideFolder(const QString& path, const QString& folder)
{
	const QString file = cleanAbsolute(path);
	QString root = cleanAbsolute(folder);
	if (file.isEmpty() || root.isEmpty()) {
		return false;
	}
	if (!root.endsWith(QLatin1Char('/'))) {
		root += QLatin1Char('/');
	}
#ifdef Q_OS_WIN
	return file.startsWith(root, Qt::CaseInsensitive);
#else
	return file.startsWith(root);
#endif
}

bool sameFamily(const QString& left, const QString& right)
{
	return left.trimmed().compare(right.trimmed(), Qt::CaseInsensitive) == 0;
}

} // namespace

bool BuildProgramReview::needsConfirmation() const
{
	return trust.contains(BuildProgramTrust::Untrusted);
}

QStringList BuildProgramReview::untrustedPrograms() const
{
	QStringList programs;
	for (qsizetype index = 0; index < uses.size() && index < trust.size(); ++index) {
		if (trust.at(index) != BuildProgramTrust::Untrusted) {
			continue;
		}
		bool listed = false;
		for (const QString& program : std::as_const(programs)) {
			listed = listed || samePath(program, uses.at(index).program);
		}
		if (!listed) {
			programs << cleanAbsolute(uses.at(index).program);
		}
	}
	return programs;
}

CompilerRegistryOptions userCompilerRegistryOptions(const QVector<CompilerToolPathOverride>& userOverrides, const QStringList& userSearchPaths)
{
	CompilerRegistryOptions options;
	// No project root: the studio's own compiler folders are looked for from
	// the current folder, as discovery does without a project.
	options.executableOverrides = userOverrides;
	options.extraSearchPaths = userSearchPaths;
	options.probeVersions = false;
	return options;
}

QVector<KnownCompilerProgram> knownCompilerPrograms(const CompilerRegistryOptions& userOptions)
{
	CompilerRegistryOptions options = userOptions;
	options.probeVersions = false;
	QVector<KnownCompilerProgram> known;
	for (const CompilerToolDiscovery& tool : discoverCompilerTools(options).tools) {
		if (!tool.executableAvailable || tool.executablePath.isEmpty()) {
			continue;
		}
		KnownCompilerProgram program;
		program.toolId = tool.descriptor.id;
		program.engineFamily = tool.descriptor.engineFamily;
		program.path = cleanAbsolute(tool.executablePath);
		program.configured = tool.executablePathOverridden;
		for (const QString& searchPath : options.extraSearchPaths) {
			program.configured = program.configured || insideFolder(program.path, searchPath);
		}
		known.push_back(program);
	}
	return known;
}

QString buildProgramTrustScope(const QString& folder)
{
	return cleanAbsolute(folder);
}

QString buildProgramFingerprint(const QString& program)
{
	QFile file(program);
	if (program.trimmed().isEmpty() || !file.open(QIODevice::ReadOnly)) {
		return {};
	}
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!hash.addData(&file)) {
		return {};
	}
	return QString::fromLatin1(hash.result().toHex());
}

BuildProgramUse compilerManifestProgramUse(const CompilerCommandManifest& manifest)
{
	BuildProgramUse use;
	use.program = manifest.program;
	use.commandLine = manifest.commandLine.trimmed().isEmpty() ? compilerCommandLineText(manifest.program, manifest.arguments) : manifest.commandLine;
	use.workingDirectory = manifest.workingDirectory;
	use.label = manifest.profileId;
	// The family of the profile the run claims, which the program must be a
	// compiler of; the manifest's own field when the profile is gone.
	CompilerProfileDescriptor profile;
	use.engineFamily = compilerProfileForId(manifest.profileId, &profile) ? profile.engineFamily : manifest.engineFamily;
	return use;
}

BuildProgramUse compilerPlanProgramUse(const CompilerCommandPlan& plan)
{
	BuildProgramUse use;
	use.program = plan.program;
	use.commandLine = plan.commandLine;
	use.workingDirectory = plan.workingDirectory;
	use.label = plan.profile.id;
	use.engineFamily = plan.profile.engineFamily;
	return use;
}

QVector<BuildProgramUse> buildPipelineProgramUses(const BuildPipelineResult& plan)
{
	QVector<BuildProgramUse> uses;
	for (const BuildPipelineStageResult& stage : plan.stages) {
		if (stage.skipped || stage.plan.program.trimmed().isEmpty()) {
			continue;
		}
		BuildProgramUse use = compilerPlanProgramUse(stage.plan);
		use.label = QCoreApplication::translate("VibeStudioBuildTrust", "Stage %1 (%2)").arg(stage.stage.displayName.isEmpty() ? stage.stage.id : stage.stage.displayName,
			stage.stage.profileId);
		uses.push_back(use);
	}
	return uses;
}

BuildProgramReview reviewBuildPrograms(const QVector<BuildProgramUse>& uses, const QString& scope, const QVector<KnownCompilerProgram>& known,
	const QVector<BuildProgramTrustRecord>& trusted)
{
	BuildProgramReview review;
	review.scope = buildProgramTrustScope(scope);
	review.uses = uses;
	QHash<QString, QString> fingerprints;
	for (const BuildProgramUse& use : uses) {
		const QString program = cleanAbsolute(use.program);
		if (program.isEmpty() || !QFileInfo(program).isFile()) {
			review.trust.push_back(BuildProgramTrust::Missing);
			continue;
		}
		bool isKnown = false;
		for (const KnownCompilerProgram& compiler : known) {
			if (!samePath(compiler.path, program) || (!use.engineFamily.trimmed().isEmpty() && !sameFamily(compiler.engineFamily, use.engineFamily))) {
				continue;
			}
			// Discovery from a folder the build itself comes from proves nothing.
			isKnown = isKnown || compiler.configured || review.scope.isEmpty() || !insideFolder(program, review.scope);
		}
		if (isKnown) {
			review.trust.push_back(BuildProgramTrust::Known);
			continue;
		}
		bool isTrusted = false;
		for (const BuildProgramTrustRecord& record : trusted) {
			if (isTrusted || review.scope.isEmpty() || !samePath(record.scope, review.scope) || !samePath(record.program, program)) {
				continue;
			}
			if (!fingerprints.contains(program)) {
				fingerprints.insert(program, buildProgramFingerprint(program));
			}
			const QString fingerprint = fingerprints.value(program);
			isTrusted = !fingerprint.isEmpty() && fingerprint.compare(record.sha256, Qt::CaseInsensitive) == 0;
		}
		review.trust.push_back(isTrusted ? BuildProgramTrust::Trusted : BuildProgramTrust::Untrusted);
	}
	return review;
}

QVector<BuildProgramTrustRecord> withTrustedPrograms(const BuildProgramReview& review, QVector<BuildProgramTrustRecord> records)
{
	if (review.scope.isEmpty()) {
		return records;
	}
	for (const QString& program : review.untrustedPrograms()) {
		records.erase(std::remove_if(records.begin(), records.end(), [&](const BuildProgramTrustRecord& record) {
			return samePath(record.scope, review.scope) && samePath(record.program, program);
		}), records.end());
		BuildProgramTrustRecord record;
		record.scope = review.scope;
		record.program = program;
		record.sha256 = buildProgramFingerprint(program);
		record.trustedUtc = QDateTime::currentDateTimeUtc();
		if (!record.sha256.isEmpty()) {
			records.push_back(record);
		}
	}
	return records;
}

QString buildProgramTrustId(BuildProgramTrust trust)
{
	switch (trust) {
	case BuildProgramTrust::Known:
		return QStringLiteral("known");
	case BuildProgramTrust::Trusted:
		return QStringLiteral("trusted");
	case BuildProgramTrust::Untrusted:
		return QStringLiteral("untrusted");
	case BuildProgramTrust::Missing:
		break;
	}
	return QStringLiteral("missing");
}

QString buildProgramReviewText(const BuildProgramReview& review)
{
	QStringList lines;
	for (qsizetype index = 0; index < review.uses.size() && index < review.trust.size(); ++index) {
		if (review.trust.at(index) != BuildProgramTrust::Untrusted) {
			continue;
		}
		const BuildProgramUse& use = review.uses.at(index);
		if (!lines.isEmpty()) {
			lines << QString();
		}
		if (!use.label.trimmed().isEmpty()) {
			lines << use.label;
		}
		lines << QCoreApplication::translate("VibeStudioBuildTrust", "Program: %1").arg(QDir::toNativeSeparators(cleanAbsolute(use.program)));
		lines << QCoreApplication::translate("VibeStudioBuildTrust", "Command line: %1").arg(use.commandLine);
		if (!use.workingDirectory.trimmed().isEmpty()) {
			lines << QCoreApplication::translate("VibeStudioBuildTrust", "Folder: %1").arg(QDir::toNativeSeparators(use.workingDirectory));
		}
	}
	return lines.join(QLatin1Char('\n'));
}

QJsonObject buildProgramReviewJson(const BuildProgramReview& review)
{
	QJsonObject object;
	object.insert(QStringLiteral("scope"), review.scope);
	object.insert(QStringLiteral("needsConfirmation"), review.needsConfirmation());
	QJsonArray programs;
	for (qsizetype index = 0; index < review.uses.size() && index < review.trust.size(); ++index) {
		const BuildProgramUse& use = review.uses.at(index);
		QJsonObject entry;
		entry.insert(QStringLiteral("label"), use.label);
		entry.insert(QStringLiteral("program"), use.program);
		entry.insert(QStringLiteral("commandLine"), use.commandLine);
		entry.insert(QStringLiteral("workingDirectory"), use.workingDirectory);
		entry.insert(QStringLiteral("engineFamily"), use.engineFamily);
		entry.insert(QStringLiteral("trust"), buildProgramTrustId(review.trust.at(index)));
		programs.append(entry);
	}
	object.insert(QStringLiteral("programs"), programs);
	return object;
}

} // namespace vibestudio
