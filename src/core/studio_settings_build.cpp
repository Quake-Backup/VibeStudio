// Build and launch memory: per-installation extra launch arguments, the user's
// own launch profiles, the build each project ran last (Run Last Build), the
// compiler profile last run from Levels for each game and the programs the
// user trusted for a project.
#include "core/studio_settings.h"

#include "core/build_pipeline.h"
#include "core/build_program_trust.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

namespace vibestudio {

namespace {

QString extraArgumentsKey(const QString& installationId)
{
	return QStringLiteral("launchExtraArguments/%1").arg(installationId);
}

QString userProfilesKey()
{
	return QStringLiteral("launch/userProfiles");
}

// Project folders are named by a hash of their cleaned path, so the key
// needs no escaping; "no project" has a key of its own.
QString lastBuildKey(const QString& projectPath)
{
	const QString cleaned = projectPath.trimmed().isEmpty() ? QString() : QDir::cleanPath(QFileInfo(projectPath.trimmed()).absoluteFilePath());
#ifdef Q_OS_WIN
	const QString folded = cleaned.toLower();
#else
	const QString folded = cleaned;
#endif
	const QString name = folded.isEmpty()
		? QStringLiteral("none")
		: QString::fromLatin1(QCryptographicHash::hash(folded.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
	return QStringLiteral("build/lastRun/%1").arg(name);
}

QString levelProfileKey(const QString& gameKey)
{
	return QStringLiteral("levels/compilerProfile/%1").arg(gameKey.trimmed().toLower());
}

QString trustedProgramsKey()
{
	return QStringLiteral("build/trustedPrograms");
}

bool plainKey(const QString& value)
{
	if (value.trimmed().isEmpty()) {
		return false;
	}
	for (const QChar character : value.trimmed()) {
		if (!character.isLetterOrNumber() && character != QLatin1Char('-') && character != QLatin1Char('_') && character != QLatin1Char('.')) {
			return false;
		}
	}
	return true;
}

} // namespace

QString StudioSettings::launchExtraArguments(const QString& installationId) const
{
	if (!plainKey(installationId)) {
		return {};
	}
	return m_settings->value(extraArgumentsKey(installationId.trimmed())).toString().trimmed();
}

void StudioSettings::setLaunchExtraArguments(const QString& installationId, const QString& arguments)
{
	if (!plainKey(installationId)) {
		return;
	}
	const QString key = extraArgumentsKey(installationId.trimmed());
	if (arguments.trimmed().isEmpty()) {
		removeKey(key);
	} else {
		writeValue(key, arguments.trimmed());
	}
}

QVector<GameLaunchProfile> StudioSettings::userGameLaunchProfiles() const
{
	QVector<GameLaunchProfile> profiles;
	const QJsonDocument document = QJsonDocument::fromJson(m_settings->value(userProfilesKey()).toString().toUtf8());
	for (const QJsonValue& value : document.array()) {
		GameLaunchProfile profile;
		if (!gameLaunchProfileFromJson(value.toObject(), &profile)) {
			continue;
		}
		bool duplicate = false;
		for (const GameLaunchProfile& existing : std::as_const(profiles)) {
			duplicate = duplicate || existing.id == profile.id;
		}
		if (!duplicate) {
			profiles.push_back(profile);
		}
	}
	return profiles;
}

void StudioSettings::setUserGameLaunchProfiles(const QVector<GameLaunchProfile>& profiles)
{
	QJsonArray array;
	for (GameLaunchProfile profile : profiles) {
		profile.userDefined = true;
		if (userGameLaunchProfileProblem(profile).isEmpty()) {
			array.push_back(gameLaunchProfileJson(profile));
		}
	}
	if (array.isEmpty()) {
		removeKey(userProfilesKey());
	} else {
		writeValue(userProfilesKey(), QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
	}
}

StudioBuildRunRecord StudioSettings::lastBuildRun(const QString& projectPath) const
{
	StudioBuildRunRecord record;
	const QJsonObject object = QJsonDocument::fromJson(m_settings->value(lastBuildKey(projectPath)).toString().toUtf8()).object();
	record.kind = object.value(QStringLiteral("kind")).toString();
	record.id = object.value(QStringLiteral("id")).toString();
	record.launch = object.value(QStringLiteral("launch")).toBool();
	record.inputPath = object.value(QStringLiteral("inputPath")).toString();
	record.timeUtc = QDateTime::fromString(object.value(QStringLiteral("timeUtc")).toString(), Qt::ISODate);
	return record.isValid() ? record : StudioBuildRunRecord {};
}

void StudioSettings::setLastBuildRun(const QString& projectPath, const StudioBuildRunRecord& record)
{
	const QString key = lastBuildKey(projectPath);
	if (!record.isValid()) {
		removeKey(key);
		return;
	}
	QJsonObject object;
	object.insert(QStringLiteral("kind"), record.kind);
	object.insert(QStringLiteral("id"), record.id.trimmed());
	object.insert(QStringLiteral("launch"), record.launch);
	object.insert(QStringLiteral("inputPath"), record.inputPath);
	object.insert(QStringLiteral("timeUtc"), (record.timeUtc.isValid() ? record.timeUtc : QDateTime::currentDateTimeUtc()).toUTC().toString(Qt::ISODate));
	writeValue(key, QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
}

QString StudioSettings::levelCompilerProfile(const QString& gameKey) const
{
	if (!plainKey(gameKey)) {
		return {};
	}
	return m_settings->value(levelProfileKey(gameKey)).toString().trimmed();
}

void StudioSettings::setLevelCompilerProfile(const QString& gameKey, const QString& profileId)
{
	if (!plainKey(gameKey)) {
		return;
	}
	if (profileId.trimmed().isEmpty()) {
		removeKey(levelProfileKey(gameKey));
	} else {
		writeValue(levelProfileKey(gameKey), profileId.trimmed());
	}
}

QVector<BuildProgramTrustRecord> StudioSettings::trustedBuildPrograms() const
{
	QVector<BuildProgramTrustRecord> records;
	const QJsonDocument document = QJsonDocument::fromJson(m_settings->value(trustedProgramsKey()).toString().toUtf8());
	for (const QJsonValue& value : document.array()) {
		const QJsonObject object = value.toObject();
		BuildProgramTrustRecord record;
		record.scope = object.value(QStringLiteral("scope")).toString();
		record.program = object.value(QStringLiteral("program")).toString();
		record.sha256 = object.value(QStringLiteral("sha256")).toString();
		record.trustedUtc = QDateTime::fromString(object.value(QStringLiteral("trustedUtc")).toString(), Qt::ISODate);
		if (!record.scope.isEmpty() && !record.program.isEmpty() && record.sha256.size() == 64) {
			records.push_back(record);
		}
	}
	return records;
}

void StudioSettings::setTrustedBuildPrograms(const QVector<BuildProgramTrustRecord>& records)
{
	QJsonArray array;
	for (const BuildProgramTrustRecord& record : records) {
		if (record.scope.trimmed().isEmpty() || record.program.trimmed().isEmpty() || record.sha256.size() != 64) {
			continue;
		}
		QJsonObject object;
		object.insert(QStringLiteral("scope"), record.scope);
		object.insert(QStringLiteral("program"), record.program);
		object.insert(QStringLiteral("sha256"), record.sha256.toLower());
		object.insert(QStringLiteral("trustedUtc"), (record.trustedUtc.isValid() ? record.trustedUtc : QDateTime::currentDateTimeUtc()).toUTC().toString(Qt::ISODate));
		array.push_back(object);
	}
	if (array.isEmpty()) {
		removeKey(trustedProgramsKey());
	} else {
		writeValue(trustedProgramsKey(), QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
	}
}

} // namespace vibestudio
