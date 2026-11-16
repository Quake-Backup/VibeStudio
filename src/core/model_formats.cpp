// Shared plumbing for the model format decoders: path helpers, the
// companion-file budget, and the package and file-system companion sources.
#include "core/model_formats_p.h"

#include "core/model_archive.h"
#include "core/model_file_io.h"
#include "core/package_archive.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <memory>
#include <mutex>

namespace vibestudio {

namespace model_formats {

QString pathDirectory(const QString& path)
{
	QString normalized = path;
	normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
	const int slash = normalized.lastIndexOf(QLatin1Char('/'));
	return slash < 0 ? QString() : normalized.left(slash);
}

QString pathStem(const QString& path)
{
	QString normalized = path;
	normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
	QString name = normalized.mid(normalized.lastIndexOf(QLatin1Char('/')) + 1);
	const int dot = name.lastIndexOf(QLatin1Char('.'));
	return dot > 0 ? name.left(dot) : name;
}

QString joinPath(const QString& directory, const QString& name)
{
	if (directory.isEmpty()) {
		return name;
	}
	return directory.endsWith(QLatin1Char('/')) ? directory + name : directory + QLatin1Char('/') + name;
}

Companions::Companions(const ModelCompanionSource* source, ModelMesh* mesh)
	: m_source(source)
	, m_mesh(mesh)
{
}

bool Companions::canRead() const
{
	return m_source && static_cast<bool>(m_source->read) && !m_exhausted;
}

bool Companions::canList() const
{
	return m_source && static_cast<bool>(m_source->list);
}

bool Companions::exhausted() const
{
	return m_exhausted;
}

bool Companions::read(const QString& path, QByteArray* bytes, QString* error)
{
	if (!m_source || !m_source->read) {
		if (error) { *error = QCoreApplication::translate("VibeStudioModelMesh", "Companion files cannot be read here."); }
		return false;
	}
	if (m_exhausted || m_files >= m_source->maxFiles) {
		m_exhausted = true;
		if (error) {
			*error = QCoreApplication::translate("VibeStudioModelMesh", "The model reads more than %1 companion file(s); the rest were left out.")
				.arg(m_source->maxFiles);
		}
		return false;
	}
	QByteArray data;
	QString problem;
	if (!m_source->read(path, &data, &problem)) {
		if (error) { *error = problem; }
		return false;
	}
	if (qint64(data.size()) > m_source->maxBytes - m_bytes) {
		m_exhausted = true;
		if (error) {
			*error = QCoreApplication::translate("VibeStudioModelMesh", "The model's companion files pass %1 MiB; the rest were left out.")
				.arg(m_source->maxBytes / (1024 * 1024));
		}
		return false;
	}
	++m_files;
	m_bytes += data.size();
	if (m_mesh) {
		m_mesh->companionPaths.append(path);
	}
	*bytes = data;
	return true;
}

QStringList Companions::list(const QString& directory, const QStringList& suffixes) const
{
	if (!canList()) {
		return {};
	}
	return m_source->list(directory, suffixes);
}

} // namespace model_formats

ModelCompanionSource modelCompanionsFromArchive(const PackageArchiveReader& archive, const ModelWorkControl& control)
{
	auto reader = std::make_shared<ModelArchiveReader>(archive, control);
	ModelCompanionSource source;
	source.read = [reader](const QString& path, QByteArray* bytes, QString* error) {
		QString normalized = path;
		normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
		while (normalized.startsWith(QLatin1Char('/'))) {
			normalized.remove(0, 1);
		}
		if (reader->readEntryBytes(normalized, bytes, error)) {
			return true;
		}
		// Game file systems match names case-insensitively.
		for (const PackageEntry& entry : reader->entries()) {
			if (entry.kind == PackageEntryKind::File && entry.virtualPath.compare(normalized, Qt::CaseInsensitive) == 0) {
				return reader->readEntryBytes(entry.virtualPath, bytes, error);
			}
		}
		return false;
	};
	source.list = [reader](const QString& directory, const QStringList& suffixes) {
		QString folder = directory;
		folder.replace(QLatin1Char('\\'), QLatin1Char('/'));
		while (folder.endsWith(QLatin1Char('/'))) {
			folder.chop(1);
		}
		QStringList paths;
		for (const PackageEntry& entry : reader->entries()) {
			if (entry.kind != PackageEntryKind::File) {
				continue;
			}
			if (model_formats::pathDirectory(entry.virtualPath).compare(folder, Qt::CaseInsensitive) != 0) {
				continue;
			}
			const QString suffix = QFileInfo(entry.virtualPath).suffix().toLower();
			if (suffixes.contains(suffix)) {
				paths.append(entry.virtualPath);
			}
		}
		std::sort(paths.begin(), paths.end(), [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
		return paths;
	};
	return source;
}

namespace {

#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity kFileNameCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kFileNameCase = Qt::CaseSensitive;
#endif

bool isWindowsDeviceName(const QString& component)
{
	// Windows opens a device for these names whatever the folder or suffix.
	QString base = component.left(component.indexOf(QLatin1Char('.')));
	while (base.endsWith(QLatin1Char(' '))) {
		base.chop(1);
	}
	base = base.toUpper();
	if (base == QLatin1String("CON") || base == QLatin1String("PRN") || base == QLatin1String("AUX") || base == QLatin1String("NUL")
		|| base == QLatin1String("CONIN$") || base == QLatin1String("CONOUT$") || base == QLatin1String("CLOCK$")) {
		return true;
	}
	if (base.size() == 4 && (base.startsWith(QLatin1String("COM")) || base.startsWith(QLatin1String("LPT")))) {
		const QChar digit = base.at(3);
		return digit.isDigit() || digit == QChar(0x00B9) || digit == QChar(0x00B2) || digit == QChar(0x00B3);
	}
	return false;
}

// Companion names usually come from inside the model file, so they must not
// reach another machine (UNC and device paths), climb out of the folders the
// lookup searches ("..") or open devices. `absolute` reports a rooted name.
bool companionNameIsSafe(const QString& name, bool* absolute)
{
	QString normalized = name;
	normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
	*absolute = false;
	if (normalized.isEmpty() || normalized.startsWith(QLatin1String("//"))) {
		return false;
	}
	for (const QChar ch : std::as_const(normalized)) {
		if (ch.unicode() < 0x20) {
			return false;
		}
	}
	QStringList parts = normalized.split(QLatin1Char('/'));
	const bool driveRooted = parts.size() > 1 && parts.first().size() == 2 && parts.first().at(1) == QLatin1Char(':')
		&& parts.first().at(0).isLetter() && parts.first().at(0).unicode() < 0x80;
	if (driveRooted) {
		parts.removeFirst();
	}
	*absolute = driveRooted || normalized.startsWith(QLatin1Char('/'));
	for (const QString& part : std::as_const(parts)) {
		// ':' would make a drive-relative name ("C:x") or an alternate data stream.
		if (part == QLatin1String("..") || part.contains(QLatin1Char(':')) || (!part.isEmpty() && isWindowsDeviceName(part))) {
			return false;
		}
	}
	return true;
}

bool pathIsInside(const QString& path, const QString& root)
{
	const QString cleanPath = QDir::cleanPath(path);
	QString cleanRoot = QDir::cleanPath(root);
	if (cleanPath.compare(cleanRoot, kFileNameCase) == 0) {
		return true;
	}
	if (!cleanRoot.endsWith(QLatin1Char('/'))) {
		cleanRoot += QLatin1Char('/');
	}
	return cleanPath.startsWith(cleanRoot, kFileNameCase);
}

struct FileSystemCompanionState {
	std::mutex mutex;
	qint64 bytesRead = 0;
	// Absolute paths this source listed; a decoder may read them back.
	QSet<QString> listed;
};

QString listedKey(const QString& path)
{
	const QString clean = QDir::cleanPath(path);
	return kFileNameCase == Qt::CaseInsensitive ? clean.toLower() : clean;
}

} // namespace

ModelCompanionSource modelCompanionsFromFileSystem(const QString& modelPath)
{
	const QString modelDirectory = QFileInfo(modelPath).absolutePath();
	ModelCompanionSource source;
	auto state = std::make_shared<FileSystemCompanionState>();
	const qint64 maxBytes = source.maxBytes;
	source.read = [modelDirectory, state, maxBytes](const QString& path, QByteArray* bytes, QString* error) {
		const auto refuse = [error, &path] {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioModelMesh", "The companion file name %1 points outside the model's folders and was not read.").arg(path);
			}
			return false;
		};
		bool absolute = false;
		if (!companionNameIsSafe(path, &absolute)) {
			return refuse();
		}
		// 0 found and read, 1 not there, 2 there but refused (error set).
		const auto readFile = [bytes, error, state, maxBytes](const QString& file) {
			const QFileInfo info(file);
			if (!info.isFile()) {
				return 1;
			}
			qint64 remaining = 0;
			{
				const std::lock_guard<std::mutex> lock(state->mutex);
				remaining = std::min(maxBytes - state->bytesRead, modelFileByteLimit);
			}
			if (info.size() > remaining) {
				if (error) {
					*error = QCoreApplication::translate("VibeStudioModelMesh", "The companion file %1 is larger than the %2 MiB left for companion files and was not read.")
						.arg(file, QString::number(std::max<qint64>(0, remaining) / (1024 * 1024)));
				}
				return 2;
			}
			QByteArray data;
			QString problem;
			if (!readModelFile(file, &data, &problem)) {
				if (error) {
					*error = QCoreApplication::translate("VibeStudioModelMesh", "The companion file %1 could not be read.").arg(file);
				}
				return 2;
			}
			{
				const std::lock_guard<std::mutex> lock(state->mutex);
				if (qint64(data.size()) > maxBytes - state->bytesRead) {
					if (error) {
						*error = QCoreApplication::translate("VibeStudioModelMesh", "The model's companion files pass %1 MiB; the rest were left out.")
							.arg(maxBytes / (1024 * 1024));
					}
					return 2;
				}
				state->bytesRead += data.size();
			}
			*bytes = std::move(data);
			if (error) { error->clear(); }
			return 0;
		};
		if (absolute) {
			// A rooted name is read only inside the model's own folder, or when
			// this source listed it.
			bool wasListed = false;
			{
				const std::lock_guard<std::mutex> lock(state->mutex);
				wasListed = state->listed.contains(listedKey(path));
			}
			if (!wasListed && !pathIsInside(QDir::fromNativeSeparators(path), modelDirectory)) {
				return refuse();
			}
			const int result = readFile(path);
			if (result != 1) {
				return result == 0;
			}
		} else {
			// A game-relative path: try it against the model's folder and every
			// folder above it, so "models/x.gla" resolves from the game folder.
			QDir folder(modelDirectory);
			for (int depth = 0; depth < 32; ++depth) {
				const int result = readFile(folder.filePath(path));
				if (result != 1) {
					return result == 0;
				}
				if (!folder.cdUp()) {
					break;
				}
			}
		}
		if (error) { *error = QCoreApplication::translate("VibeStudioModelMesh", "Companion file not found: %1").arg(path); }
		return false;
	};
	source.list = [modelDirectory, state](const QString& directory, const QStringList& suffixes) {
		bool absoluteDirectory = false;
		if (!directory.isEmpty() && !companionNameIsSafe(directory, &absoluteDirectory)) {
			return QStringList();
		}
		// A game-relative folder ("def") is looked for beside the model and in
		// every folder above it, as reads are.
		QDir folder(QFileInfo(directory).isAbsolute() ? directory : QDir(modelDirectory).filePath(directory));
		if (!QFileInfo(directory).isAbsolute()) {
			QDir ancestor(modelDirectory);
			for (int depth = 0; depth < 32; ++depth) {
				if (QDir(ancestor.filePath(directory)).exists()) {
					folder = QDir(ancestor.filePath(directory));
					break;
				}
				if (!ancestor.cdUp()) {
					break;
				}
			}
		}
		QStringList filters;
		for (const QString& suffix : suffixes) {
			// Name filters are case-sensitive on some file systems.
			filters << QStringLiteral("*.") + suffix << QStringLiteral("*.") + suffix.toUpper();
		}
		QStringList paths;
		const QFileInfoList files = folder.entryInfoList(filters, QDir::Files, QDir::Name | QDir::IgnoreCase);
		for (const QFileInfo& file : files) {
			if (suffixes.contains(file.suffix().toLower()) && !paths.contains(file.absoluteFilePath().replace(QLatin1Char('\\'), QLatin1Char('/')))) {
				QString path = file.absoluteFilePath();
				path.replace(QLatin1Char('\\'), QLatin1Char('/'));
				paths.append(path);
			}
		}
		const std::lock_guard<std::mutex> lock(state->mutex);
		for (const QString& path : std::as_const(paths)) {
			state->listed.insert(listedKey(path));
		}
		return paths;
	};
	return source;
}

} // namespace vibestudio
