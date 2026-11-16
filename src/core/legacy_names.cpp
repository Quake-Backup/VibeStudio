#include "core/legacy_names.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace slopstudio {

namespace {

// The former name's first syllable, letter by letter mapped to the current one.
constexpr char kLegacySyllable[] = "vibe";
constexpr char kCurrentSyllable[] = "slop";

QString legacyForm(const QString& value)
{
	QString result = value;
	for (qsizetype index = 0; index + 4 <= result.size(); ++index) {
		if (result.mid(index, 4).compare(QLatin1String(kCurrentSyllable), Qt::CaseInsensitive) != 0) {
			continue;
		}
		for (int letter = 0; letter < 4; ++letter) {
			const QChar original = result.at(index + letter);
			const QChar replacement = QLatin1Char(kLegacySyllable[letter]);
			result[index + letter] = original.isUpper() ? replacement.toUpper() : replacement;
		}
		index += 3;
	}
	return result;
}

QString environmentName(const char* prefix, const char* name)
{
	return QString::fromLatin1(prefix) + QLatin1Char('_') + QString::fromLatin1(name);
}

} // namespace

QString legacyStudioName()
{
	return QStringLiteral("VibeStudio");
}

QString withCurrentStudioName(const QString& value)
{
	QString result = value;
	for (qsizetype index = 0; index + 4 <= result.size(); ++index) {
		if (result.mid(index, 4).compare(QLatin1String(kLegacySyllable), Qt::CaseInsensitive) != 0) {
			continue;
		}
		bool ascii = true;
		for (int letter = 0; letter < 4; ++letter) {
			ascii = ascii && result.at(index + letter).unicode() < 0x80;
		}
		if (!ascii) {
			continue;
		}
		for (int letter = 0; letter < 4; ++letter) {
			const QChar original = result.at(index + letter);
			const QChar replacement = QLatin1Char(kCurrentSyllable[letter]);
			result[index + letter] = original.isUpper() ? replacement.toUpper() : replacement;
		}
		index += 3;
	}
	return result;
}

QString legacyStudioId(const QString& current)
{
	return legacyForm(current);
}

bool isCurrentOrLegacyStudioName(const QString& value, const QString& current, Qt::CaseSensitivity sensitivity)
{
	return value.compare(current, sensitivity) == 0 || withCurrentStudioName(value).compare(current, sensitivity) == 0;
}

bool hasCurrentOrLegacySuffix(const QString& path, const QString& currentSuffix)
{
	return path.endsWith(currentSuffix, Qt::CaseInsensitive) || path.endsWith(legacyForm(currentSuffix), Qt::CaseInsensitive);
}

bool isPackageDraftPath(const QString& path)
{
	return hasCurrentOrLegacySuffix(path, QStringLiteral(".sloppackage"));
}

bool isWorkspaceDocumentPath(const QString& path)
{
	return hasCurrentOrLegacySuffix(path, QStringLiteral(".slopworkspace"));
}

QString studioEnvironmentVariable(const char* name, const QString& defaultValue)
{
	const QString current = environmentName("SLOPSTUDIO", name);
	if (qEnvironmentVariableIsSet(current.toLatin1().constData())) {
		return qEnvironmentVariable(current.toLatin1().constData());
	}
	const QString legacy = environmentName("VIBESTUDIO", name);
	return qEnvironmentVariable(legacy.toLatin1().constData(), defaultValue);
}

int studioEnvironmentVariableIntValue(const char* name)
{
	const QByteArray current = environmentName("SLOPSTUDIO", name).toLatin1();
	if (qEnvironmentVariableIsSet(current.constData())) {
		return qEnvironmentVariableIntValue(current.constData());
	}
	return qEnvironmentVariableIntValue(environmentName("VIBESTUDIO", name).toLatin1().constData());
}

bool studioEnvironmentVariableIsSet(const char* name)
{
	return qEnvironmentVariableIsSet(environmentName("SLOPSTUDIO", name).toLatin1().constData())
		|| qEnvironmentVariableIsSet(environmentName("VIBESTUDIO", name).toLatin1().constData());
}

QString legacyProjectDirectoryName()
{
	return QStringLiteral(".vibestudio");
}

bool migrateLegacyProjectDirectory(const QString& projectRootPath, QString* error)
{
	if (error) {
		error->clear();
	}
	if (projectRootPath.trimmed().isEmpty()) {
		return true;
	}
	// projectManifestPath() calls this on every lookup, so each root is
	// checked once per process rather than costing two file checks per call.
	static QMutex mutex;
	static QSet<QString> checkedRoots;
	{
		const QMutexLocker lock(&mutex);
		if (checkedRoots.contains(projectRootPath)) {
			return true;
		}
		checkedRoots.insert(projectRootPath);
	}
	const QDir root(projectRootPath);
	const QString current = root.filePath(QStringLiteral(".slopstudio"));
	const QString legacy = root.filePath(legacyProjectDirectoryName());
	const QFileInfo legacyInfo(legacy);
	if (QFileInfo::exists(current) || !legacyInfo.isDir() || legacyInfo.isSymLink()) {
		return true;
	}
	if (QDir().rename(legacy, current)) {
		return true;
	}
	if (error) {
		*error = QCoreApplication::translate("SlopStudioLegacyNames", "The VibeStudio project folder %1 could not be renamed to %2.")
			.arg(QDir::toNativeSeparators(legacy), QDir::toNativeSeparators(current));
	}
	return false;
}

int copyLegacySettings(const QSettings& legacy, QSettings& current)
{
	if (!current.allKeys().isEmpty()) {
		return 0;
	}
	const QStringList keys = legacy.allKeys();
	for (const QString& key : keys) {
		current.setValue(key, legacy.value(key));
	}
	if (!keys.isEmpty()) {
		current.sync();
	}
	return static_cast<int>(keys.size());
}

int migrateLegacySettingsStore()
{
	QSettings current;
	if (!current.allKeys().isEmpty()) {
		return 0;
	}
	const QSettings legacy(QCoreApplication::organizationName(), legacyStudioName());
	return copyLegacySettings(legacy, current);
}

QString legacyDataDirectory(const QString& currentPath)
{
	const QString application = QCoreApplication::applicationName();
	if (application.isEmpty()) {
		return {};
	}
	const QString cleaned = QDir::cleanPath(currentPath);
	const QString marker = QLatin1Char('/') + application;
	const qsizetype at = cleaned.lastIndexOf(marker);
	if (at < 0 || (at + marker.size() != cleaned.size() && cleaned.at(at + marker.size()) != QLatin1Char('/'))) {
		return {};
	}
	return cleaned.left(at + 1) + legacyStudioName() + cleaned.mid(at + marker.size());
}

int migrateLegacyDataDirectories()
{
	int moved = 0;
	const QStandardPaths::StandardLocation locations[] = {
		QStandardPaths::AppDataLocation,
		QStandardPaths::AppLocalDataLocation,
		QStandardPaths::AppConfigLocation,
		QStandardPaths::CacheLocation,
	};
	for (const auto location : locations) {
		const QString current = QStandardPaths::writableLocation(location);
		const QString legacy = legacyDataDirectory(current);
		if (current.isEmpty() || legacy.isEmpty() || QFileInfo::exists(current)) {
			continue;
		}
		const QFileInfo legacyInfo(legacy);
		if (!legacyInfo.isDir() || legacyInfo.isSymLink()) {
			continue;
		}
		QDir().mkpath(QFileInfo(current).absolutePath());
		if (QDir().rename(legacy, current)) {
			++moved;
		}
	}
	return moved;
}

} // namespace slopstudio
