#include "core/model_outputs.h"

#include "core/model_assembly.h"
#include "core/model_file_io.h"
#include "core/model_lod.h"
#include "core/package_archive.h"
#include "core/studio_manifest.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <memory>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}

QString hex(const QByteArray &bytes)
{
	return QString::fromLatin1(bytes.toHex());
}

QByteArray unhex(const QJsonValue &value)
{
	const auto text = value.toString();
	return text.isEmpty() ? QByteArray() : QByteArray::fromHex(text.toLatin1());
}

// Inside the project: relative with forward slashes; elsewhere absolute.
QString storedPath(const QString &path, const QString &root)
{
	if (path.isEmpty())
		return {};
	const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
	if (!root.isEmpty())
	{
		const QString relative = QDir(root).relativeFilePath(absolute);
		if (!relative.startsWith(QStringLiteral("..")) && !QDir::isAbsolutePath(relative))
			return relative;
	}
	return absolute;
}

QString loadedPath(const QString &path, const QString &root)
{
	if (path.isEmpty())
		return {};
	return QDir::cleanPath(QDir::isAbsolutePath(path) || root.isEmpty() ? path : QDir(root).absoluteFilePath(path));
}

bool samePath(const QString &a, const QString &b)
{
	if (a.isEmpty() || b.isEmpty())
		return false;
#ifdef Q_OS_WIN
	return QDir::cleanPath(a).compare(QDir::cleanPath(b), Qt::CaseInsensitive) == 0;
#else
	return QDir::cleanPath(a) == QDir::cleanPath(b);
#endif
}

bool stopped(const std::function<bool()> &cancelled)
{
	return cancelled && cancelled();
}

// SHA-256 of a package entry, from the open package when it is that package.
QByteArray entrySha256(const ModelOutputFile &file, const PackageArchiveReader *openPackage, bool *missing, QString *problem)
{
	*missing = false;
	std::unique_ptr<PackageArchive> loaded;
	const PackageArchiveReader *reader = nullptr;
	if (openPackage && openPackage->isOpen() && samePath(openPackage->sourcePath(), file.package))
		reader = openPackage;
	else
	{
		if (!QFileInfo::exists(file.package))
		{
			*missing = true;
			return {};
		}
		loaded = std::make_unique<PackageArchive>();
		if (!loaded->load(file.package, problem))
			return {};
		reader = loaded.get();
	}
	const auto entries = reader->entries();
	const auto found = std::find_if(entries.cbegin(), entries.cend(), [&](const PackageEntry &entry)
									{ return entry.kind == PackageEntryKind::File && entry.virtualPath.compare(file.entry, Qt::CaseInsensitive) == 0; });
	if (found == entries.cend())
	{
		*missing = true;
		return {};
	}
	QByteArray bytes;
	if (!reader->readEntryBytes(found->virtualPath, &bytes, problem, modelFileByteLimit + 1) || bytes.size() > modelFileByteLimit)
		return {};
	return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
} // namespace

QString ModelOutputFile::display() const
{
	return package.isEmpty() ? QDir::toNativeSeparators(path) : QStringLiteral("%1!%2").arg(QDir::toNativeSeparators(package), entry);
}

QString ModelOutputRecord::key() const
{
	if (outputs.isEmpty())
		return {};
	const auto &first = outputs.first();
	return first.package.isEmpty() ? QDir::cleanPath(first.path) : QDir::cleanPath(first.package) + QLatin1Char('!') + first.entry;
}

QString modelOutputStateId(ModelOutputState state)
{
	switch (state)
	{
	case ModelOutputState::Current:
		return QStringLiteral("current");
	case ModelOutputState::SourceChanged:
		return QStringLiteral("source-changed");
	case ModelOutputState::OutputChanged:
		return QStringLiteral("output-changed");
	case ModelOutputState::Missing:
		return QStringLiteral("missing");
	case ModelOutputState::Unknown:
		return QStringLiteral("unknown");
	}
	return {};
}

QString modelOutputStateText(ModelOutputState state)
{
	switch (state)
	{
	case ModelOutputState::Current:
		return QCoreApplication::translate("VibeStudioModelOutputs", "Current");
	case ModelOutputState::SourceChanged:
		return QCoreApplication::translate("VibeStudioModelOutputs", "Source changed");
	case ModelOutputState::OutputChanged:
		return QCoreApplication::translate("VibeStudioModelOutputs", "Changed elsewhere");
	case ModelOutputState::Missing:
		return QCoreApplication::translate("VibeStudioModelOutputs", "Missing");
	case ModelOutputState::Unknown:
		return QCoreApplication::translate("VibeStudioModelOutputs", "Unknown");
	}
	return {};
}

bool modelOutputStateIsStale(ModelOutputState state)
{
	return state == ModelOutputState::SourceChanged || state == ModelOutputState::Missing;
}

QString modelOutputManifestKey()
{
	return QStringLiteral("modelOutputs");
}

QJsonObject modelOutputRecordJson(const ModelOutputRecord &record, const QString &projectRoot)
{
	QJsonObject source{{QStringLiteral("path"), storedPath(record.sourcePath, projectRoot)}};
	if (!record.sourceSha256.isEmpty())
		source.insert(QStringLiteral("sha256"), hex(record.sourceSha256));
	QJsonArray inputs, outputs;
	for (const auto &input : record.inputs)
	{
		QJsonObject item{{QStringLiteral("path"), storedPath(input.path, projectRoot)}, {QStringLiteral("sha256"), hex(input.sha256)}};
		if (!input.role.isEmpty())
			item.insert(QStringLiteral("role"), input.role);
		inputs.append(item);
	}
	for (const auto &output : record.outputs)
	{
		QJsonObject item{{QStringLiteral("sha256"), hex(output.sha256)}};
		if (output.package.isEmpty())
			item.insert(QStringLiteral("path"), storedPath(output.path, projectRoot));
		else
		{
			item.insert(QStringLiteral("package"), storedPath(output.package, projectRoot));
			item.insert(QStringLiteral("entry"), output.entry);
		}
		outputs.append(item);
	}
	QJsonObject object{{QStringLiteral("kind"), record.kind},
					   {QStringLiteral("source"), source},
					   {QStringLiteral("recipe"), record.recipe},
					   {QStringLiteral("outputs"), outputs},
					   {QStringLiteral("tool"), record.tool},
					   {QStringLiteral("deliveredUtc"), record.deliveredUtc.toUTC().toString(Qt::ISODate)}};
	if (!inputs.isEmpty())
		object.insert(QStringLiteral("inputs"), inputs);
	return object;
}

bool parseModelOutputRecord(const QJsonObject &object, const QString &projectRoot, ModelOutputRecord *record, QString *error)
{
	ModelOutputRecord result;
	result.kind = object.value(QStringLiteral("kind")).toString();
	const auto source = object.value(QStringLiteral("source")).toObject();
	result.sourcePath = loadedPath(source.value(QStringLiteral("path")).toString(), projectRoot);
	result.sourceSha256 = unhex(source.value(QStringLiteral("sha256")));
	result.recipe = object.value(QStringLiteral("recipe")).toObject();
	result.tool = object.value(QStringLiteral("tool")).toString();
	result.deliveredUtc = QDateTime::fromString(object.value(QStringLiteral("deliveredUtc")).toString(), Qt::ISODate);
	for (const auto &value : object.value(QStringLiteral("inputs")).toArray())
	{
		const auto item = value.toObject();
		result.inputs.append({loadedPath(item.value(QStringLiteral("path")).toString(), projectRoot), unhex(item.value(QStringLiteral("sha256"))),
							  item.value(QStringLiteral("role")).toString()});
	}
	for (const auto &value : object.value(QStringLiteral("outputs")).toArray())
	{
		const auto item = value.toObject();
		ModelOutputFile file;
		file.sha256 = unhex(item.value(QStringLiteral("sha256")));
		if (item.contains(QStringLiteral("package")))
		{
			file.package = loadedPath(item.value(QStringLiteral("package")).toString(), projectRoot);
			file.entry = item.value(QStringLiteral("entry")).toString();
		}
		else
			file.path = loadedPath(item.value(QStringLiteral("path")).toString(), projectRoot);
		result.outputs.append(file);
	}
	const bool outputsValid = std::all_of(result.outputs.cbegin(), result.outputs.cend(), [](const ModelOutputFile &file)
										  { return file.sha256.size() == 32 && (file.package.isEmpty() ? !file.path.isEmpty() : !file.entry.isEmpty()); });
	if (result.kind.isEmpty() || result.outputs.isEmpty() || !outputsValid || (!result.sourceSha256.isEmpty() && result.sourceSha256.size() != 32))
		return fail(error, QCoreApplication::translate("VibeStudioModelOutputs", "A model output receipt is incomplete or damaged."));
	if (record)
		*record = std::move(result);
	return true;
}

QVector<ModelOutputRecord> projectModelOutputs(const ProjectManifest &manifest, QStringList *warnings)
{
	QVector<ModelOutputRecord> records;
	const auto value = manifest.extraFields.value(modelOutputManifestKey());
	if (value.isUndefined())
		return records;
	if (!value.isArray())
	{
		if (warnings)
			*warnings << QCoreApplication::translate("VibeStudioModelOutputs", "The project's model output receipts are not a list and were ignored.");
		return records;
	}
	int damaged = 0;
	for (const auto &item : value.toArray())
	{
		ModelOutputRecord record;
		if (item.isObject() && parseModelOutputRecord(item.toObject(), manifest.rootPath, &record))
			records.append(record);
		else
			++damaged;
	}
	if (damaged > 0 && warnings)
		*warnings << QCoreApplication::translate("VibeStudioModelOutputs", "%n damaged model output receipt(s) were ignored.", nullptr, damaged);
	return records;
}

void setProjectModelOutputs(ProjectManifest *manifest, const QVector<ModelOutputRecord> &records)
{
	if (!manifest)
		return;
	if (records.isEmpty())
	{
		manifest->extraFields.remove(modelOutputManifestKey());
		return;
	}
	QJsonArray array;
	for (const auto &record : records)
		array.append(modelOutputRecordJson(record, manifest->rootPath));
	manifest->extraFields.insert(modelOutputManifestKey(), array);
}

void addProjectModelOutput(ProjectManifest *manifest, const ModelOutputRecord &record)
{
	if (!manifest)
		return;
	auto records = projectModelOutputs(*manifest);
	const auto key = record.key();
	records.erase(std::remove_if(records.begin(), records.end(), [&](const ModelOutputRecord &existing) { return samePath(existing.key(), key); }),
				  records.end());
	records.append(record);
	while (records.size() > modelOutputRecordLimit)
		records.removeFirst();
	setProjectModelOutputs(manifest, records);
}

bool recordModelOutput(const QString &projectRoot, const ModelOutputRecord &record, QString *error, bool createManifest)
{
	if (projectRoot.isEmpty())
		return fail(error, QCoreApplication::translate("VibeStudioModelOutputs", "Open a project to keep receipts of model outputs."));
	ProjectManifest manifest;
	QString loadError;
	if (!loadProjectManifest(projectRoot, &manifest, &loadError))
	{
		// A manifest that exists but cannot be read is never replaced.
		if (QFileInfo::exists(projectManifestPath(projectRoot)) || !createManifest)
			return fail(error, loadError);
		manifest = defaultProjectManifest(projectRoot);
	}
	addProjectModelOutput(&manifest, record);
	return saveProjectManifest(manifest, error);
}

bool forgetModelOutput(const QString &projectRoot, const QString &key, QString *error)
{
	ProjectManifest manifest;
	if (!loadProjectManifest(projectRoot, &manifest, error))
		return false;
	auto records = projectModelOutputs(manifest);
	const auto before = records.size();
	records.erase(std::remove_if(records.begin(), records.end(), [&](const ModelOutputRecord &record) { return samePath(record.key(), key); }),
				  records.end());
	if (records.size() == before)
		return true;
	setProjectModelOutputs(&manifest, records);
	return saveProjectManifest(manifest, error);
}

QByteArray modelOutputFileSha256(const QString &path, const std::function<bool()> &cancelled)
{
	const QFileInfo info(path);
	if (!info.isFile() || info.size() > modelFileByteLimit)
		return {};
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	QCryptographicHash hash(QCryptographicHash::Sha256);
	while (!file.atEnd())
	{
		if (stopped(cancelled))
			return {};
		const auto block = file.read(256 * 1024);
		if (block.isEmpty())
			break;
		hash.addData(block);
	}
	return file.error() == QFileDevice::NoError ? hash.result() : QByteArray();
}

ModelOutputStatus modelOutputStatus(const ModelOutputRecord &record, const PackageArchiveReader *openPackage, const std::function<bool()> &cancelled)
{
	ModelOutputStatus status;
	const auto name = [](const QString &path) { return QDir::toNativeSeparators(path); };
	// Outputs first: one changed by someone else must not be refreshed over.
	bool missing = false;
	QString missingDetail;
	for (const auto &output : record.outputs)
	{
		if (stopped(cancelled))
			return {ModelOutputState::Unknown, QCoreApplication::translate("VibeStudioModelOutputs", "The check was cancelled.")};
		QByteArray current;
		bool gone = false;
		QString problem;
		if (output.package.isEmpty())
		{
			gone = !QFileInfo::exists(output.path);
			if (!gone)
				current = modelOutputFileSha256(output.path, cancelled);
		}
		else
			current = entrySha256(output, openPackage, &gone, &problem);
		if (gone)
		{
			missing = true;
			if (missingDetail.isEmpty())
				missingDetail = QCoreApplication::translate("VibeStudioModelOutputs", "%1 is missing.").arg(output.display());
			continue;
		}
		if (current.isEmpty())
			return {ModelOutputState::Unknown,
					QCoreApplication::translate("VibeStudioModelOutputs", "%1 could not be read. %2").arg(output.display(), problem).trimmed()};
		if (current != output.sha256)
			return {ModelOutputState::OutputChanged,
					QCoreApplication::translate("VibeStudioModelOutputs", "%1 was changed after it was delivered.").arg(output.display())};
	}
	// Then the inputs: the source, then every other input.
	QVector<ModelOutputInput> inputs;
	if (!record.sourcePath.isEmpty())
		inputs.append({record.sourcePath, record.sourceSha256, QStringLiteral("source")});
	inputs += record.inputs;
	for (const auto &input : inputs)
	{
		if (input.sha256.isEmpty())
			return {ModelOutputState::Unknown, QCoreApplication::translate("VibeStudioModelOutputs",
																			"Delivered from unsaved edits of %1; save the source and refresh to track it.")
												   .arg(name(input.path))};
		if (!QFileInfo::exists(input.path))
			return {ModelOutputState::Missing, QCoreApplication::translate("VibeStudioModelOutputs", "The input %1 is missing.").arg(name(input.path))};
		const auto current = modelOutputFileSha256(input.path, cancelled);
		if (current.isEmpty())
			return {ModelOutputState::Unknown, QCoreApplication::translate("VibeStudioModelOutputs", "%1 could not be read.").arg(name(input.path))};
		if (current != input.sha256)
			return {ModelOutputState::SourceChanged,
					QCoreApplication::translate("VibeStudioModelOutputs", "%1 changed after the output was delivered.").arg(name(input.path))};
	}
	if (missing)
		return {ModelOutputState::Missing, missingDetail};
	status.state = ModelOutputState::Current;
	status.detail = QCoreApplication::translate("VibeStudioModelOutputs", "Every input and output matches the receipt.");
	return status;
}

QVector<ModelOutputRecord> modelOutputsForSource(const QVector<ModelOutputRecord> &records, const QString &sourcePath)
{
	QVector<ModelOutputRecord> result;
	const auto wanted = QFileInfo(sourcePath).absoluteFilePath();
	for (const auto &record : records)
	{
		if (samePath(record.sourcePath, wanted))
			result.append(record);
	}
	return result;
}

bool modelSourceSavedAfterOutputs(const QVector<ModelOutputRecord> &records, const QString &sourcePath)
{
	const QFileInfo info(sourcePath);
	if (!info.exists())
		return false;
	const QDateTime saved = info.lastModified().toUTC();
	const auto delivered = modelOutputsForSource(records, sourcePath);
	// A second of slack: the receipt is stamped just after the source is read.
	return std::any_of(delivered.cbegin(), delivered.cend(),
					   [&](const ModelOutputRecord &record) { return record.deliveredUtc.isValid() && record.deliveredUtc.addSecs(1) < saved; });
}

QString modelOutputSourceFor(const QVector<ModelOutputRecord> &records, const QString &output)
{
	const QString wanted = QDir::fromNativeSeparators(output);
	for (auto it = records.crbegin(); it != records.crend(); ++it)
	{
		for (const auto &file : it->outputs)
		{
			const bool matches = file.package.isEmpty()
									 ? samePath(file.path, wanted) || (!QDir::isAbsolutePath(wanted) && QDir::fromNativeSeparators(file.path).endsWith(QLatin1Char('/') + wanted, Qt::CaseInsensitive))
									 : file.entry.compare(wanted, Qt::CaseInsensitive) == 0;
			if (matches && !it->sourcePath.isEmpty())
				return it->sourcePath;
		}
	}
	return {};
}

ModelOutputRecord modelExportOutputRecord(const QString &sourcePath, const QByteArray &sourceSha256, const QString &format,
										  const ModelExportOptions &options, const ModelOutputFile &output, const QByteArray &outputBytes)
{
	ModelOutputRecord record;
	record.kind = output.package.isEmpty() ? QStringLiteral("export") : QStringLiteral("stage");
	record.sourcePath = sourcePath.isEmpty() ? QString() : QFileInfo(sourcePath).absoluteFilePath();
	record.sourceSha256 = sourceSha256;
	record.recipe = {{QStringLiteral("format"), format.toLower()},
					 {QStringLiteral("frame"), options.frame},
					 {QStringLiteral("clip"), options.clip},
					 {QStringLiteral("md5FrameRate"), options.md5FrameRate},
					 {QStringLiteral("strict"), options.strict},
					 {QStringLiteral("target"), options.target}};
	auto file = output;
	if (file.package.isEmpty())
		file.path = QFileInfo(file.path).absoluteFilePath();
	file.sha256 = QCryptographicHash::hash(outputBytes, QCryptographicHash::Sha256);
	record.outputs = {file};
	record.tool = versionString();
	record.deliveredUtc = QDateTime::currentDateTimeUtc();
	return record;
}

ModelOutputRefresh refreshModelOutput(const ModelOutputRecord &record, bool dryRun, const ModelWorkControl &control)
{
	ModelOutputRefresh result;
	result.record = record;
	const auto failed = [&](const QString &message)
	{
		result.error = message;
		return result;
	};
	const bool lodSet = record.kind == QStringLiteral("lod-set");
	if (record.kind != QStringLiteral("export") && !lodSet)
		return failed(QCoreApplication::translate("VibeStudioModelOutputs",
												  "%1 outputs are refreshed by the tool that made them; only exports and detail levels refresh here.")
						  .arg(record.kind));
	if (std::any_of(record.outputs.cbegin(), record.outputs.cend(), [](const ModelOutputFile &file) { return !file.package.isEmpty(); }))
		return failed(QCoreApplication::translate("VibeStudioModelOutputs", "Staged package entries are refreshed by staging the source again."));
	// Every output must still be what was delivered, or be gone.
	for (const auto &output : record.outputs)
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Validating, 0, 0, &result.error))
		{
			result.cancelled = true;
			return result;
		}
		if (QFileInfo::exists(output.path) && modelOutputFileSha256(output.path, control.cancelled) != output.sha256)
			return failed(QCoreApplication::translate("VibeStudioModelOutputs", "%1 was changed after it was delivered, so it is not replaced. "
																				 "Save the source to another output, or forget this receipt.")
							  .arg(output.display()));
	}
	ModelDocument document;
	if (record.sourcePath.isEmpty() || !document.load(record.sourcePath, &result.error, control))
	{
		result.cancelled = !modelWorkCheckpoint(control, ModelWorkPhase::Reading, 0, 0, nullptr);
		if (result.error.isEmpty())
			result.error = QCoreApplication::translate("VibeStudioModelOutputs", "The receipt names no editable source.");
		return result;
	}
	const auto format = record.recipe.value(QStringLiteral("format")).toString(QStringLiteral("md3"));
	ModelExportOptions options;
	options.frame = record.recipe.value(QStringLiteral("frame")).toInt();
	options.clip = record.recipe.value(QStringLiteral("clip")).toInt(-1);
	options.md5FrameRate = record.recipe.value(QStringLiteral("md5FrameRate")).toInt();
	options.strict = record.recipe.value(QStringLiteral("strict")).toBool();
	options.target = record.recipe.value(QStringLiteral("target")).toString();
	// The bytes to write, in the receipt's output order.
	QVector<QByteArray> bytes;
	if (lodSet)
	{
		// The recipe model lod used (hand-made levels included), else generated levels.
		QVector<ModelLodRecipeLevel> recipe;
		const auto stored = record.recipe.value(QStringLiteral("lodRecipe"));
		if (!stored.isUndefined())
		{
			if (!parseModelLodRecipe(stored, &recipe, &result.error))
				return result;
		}
		else
			recipe = modelLodGeneratedRecipe(record.recipe.value(QStringLiteral("levels")).toInt(int(record.outputs.size()) - 1),
											 record.recipe.value(QStringLiteral("ratio")).toDouble(0.5));
		ModelLodSet set;
		if (recipe.size() != record.outputs.size() - 1 ||
			!prepareModelLodFiles(document.mesh(), recipe, QFileInfo(record.sourcePath).absolutePath(), record.outputs.first().path, options.target,
								  options.strict, &set, &result.error, control) ||
			set.files.size() != record.outputs.size())
		{
			result.cancelled = !modelWorkCheckpoint(control, ModelWorkPhase::Serializing, 0, 0, nullptr);
			if (result.error.isEmpty())
				result.error = QCoreApplication::translate("VibeStudioModelOutputs", "The detail-level receipt does not match its outputs.");
			return result;
		}
		for (const auto &file : std::as_const(set.files))
		{
			bytes.append(file.bytes);
			result.notes += file.notes;
		}
	}
	else
	{
		ModelExportReport report;
		const auto exported = exportEditableModel(document.mesh(), format, options, &result.error, control, &report);
		if (exported.isEmpty())
		{
			result.cancelled = !modelWorkCheckpoint(control, ModelWorkPhase::Serializing, 0, 0, nullptr);
			return result;
		}
		result.notes += report.notes;
		bytes.append(exported);
	}
	if (bytes.size() != record.outputs.size())
		return failed(QCoreApplication::translate("VibeStudioModelOutputs", "The receipt's outputs do not match its recipe."));
	// Review every destination before writing any.
	QVector<ModelWriteTarget> targets;
	for (const auto &output : record.outputs)
	{
		const auto target = inspectModelWriteTarget(output.path, control);
		if (!target.isValid())
			return failed(target.error);
		targets.append(target);
		result.outputs << QFileInfo(output.path).absoluteFilePath();
	}
	result.notes.removeDuplicates();
	ModelOutputRecord updated = record;
	updated.sourceSha256 = document.sourceFingerprint();
	updated.tool = versionString();
	updated.deliveredUtc = QDateTime::currentDateTimeUtc();
	for (int index = 0; index < updated.outputs.size(); ++index)
		updated.outputs[index].sha256 = QCryptographicHash::hash(bytes.at(index), QCryptographicHash::Sha256);
	result.record = updated;
	if (dryRun)
	{
		for (const auto &target : targets)
		{
			if (!validateModelWriteTarget(target, &result.error, control))
				return result;
		}
		return result;
	}
	for (int index = 0; index < targets.size(); ++index)
	{
		if (!writeModelFile(targets.at(index), bytes.at(index), &result.error, control))
		{
			result.cancelled = !modelWorkCheckpoint(control, ModelWorkPhase::Serializing, 0, 0, nullptr);
			return result;
		}
		result.written = true;
	}
	return result;
}

bool readModelPackageReceipt(const QString &packagePath, ModelOutputRecord *record, ModelOutputStatus *status, QString *error,
							 const QString &assemblyPath, const ModelWorkControl &control)
{
	PackageArchive archive;
	if (!archive.load(packagePath, error))
		return false;
	QByteArray bytes;
	if (!archive.readEntryBytes(QStringLiteral("vibestudio/player-bundle.json"), &bytes, error, 4 * 1024 * 1024))
		return fail(error, QCoreApplication::translate("VibeStudioModelOutputs", "%1 has no VibeStudio receipt (vibestudio/player-bundle.json).")
							   .arg(QDir::toNativeSeparators(packagePath)));
	const auto receipt = QJsonDocument::fromJson(bytes).object();
	if (receipt.value(QStringLiteral("format")).toString() != QStringLiteral("quake3-player-bundle"))
		return fail(error, QCoreApplication::translate("VibeStudioModelOutputs", "The package receipt is not a player package receipt."));
	ModelOutputRecord result;
	result.kind = QStringLiteral("player-package");
	result.recipe = {{QStringLiteral("model"), receipt.value(QStringLiteral("model"))}, {QStringLiteral("skin"), receipt.value(QStringLiteral("skin"))}};
	const auto packageAbsolute = QFileInfo(packagePath).absoluteFilePath();
	for (const auto &value : receipt.value(QStringLiteral("files")).toArray())
	{
		const auto item = value.toObject();
		ModelOutputFile file;
		file.package = packageAbsolute;
		file.entry = item.value(QStringLiteral("path")).toString();
		file.sha256 = unhex(item.value(QStringLiteral("sha256")));
		result.outputs.append(file);
	}
	ModelOutputStatus checked{ModelOutputState::Current,
							  QCoreApplication::translate("VibeStudioModelOutputs", "Every packaged file matches the receipt.")};
	// The packaged files against the receipt.
	for (const auto &file : std::as_const(result.outputs))
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Validating, 0, 0, error))
			return false;
		bool gone = false;
		QString problem;
		const auto current = entrySha256(file, &archive, &gone, &problem);
		if (gone || current != file.sha256)
		{
			checked = {gone ? ModelOutputState::Missing : ModelOutputState::OutputChanged,
					   QCoreApplication::translate("VibeStudioModelOutputs", "%1 no longer matches the receipt.").arg(file.entry)};
			break;
		}
	}
	// The parts' sources against the assembly's current files.
	ModelAssembly assembly;
	QByteArray assemblyBytes;
	const bool withAssembly = !assemblyPath.isEmpty();
	if (withAssembly && (!readModelFile(assemblyPath, &assemblyBytes, error, control) || !parseModelAssembly(assemblyBytes, &assembly, error)))
		return false;
	for (const auto &value : receipt.value(QStringLiteral("parts")).toArray())
	{
		const auto item = value.toObject();
		const auto partId = item.value(QStringLiteral("part")).toString();
		ModelOutputInput input{partId, unhex(item.value(QStringLiteral("sourceSha256"))), item.value(QStringLiteral("role")).toString()};
		if (withAssembly)
		{
			const auto part = std::find_if(assembly.parts.cbegin(), assembly.parts.cend(), [&](const ModelAssemblyPart &p) { return p.id == partId; });
			if (part == assembly.parts.cend() || part->sourceKind != ModelAssemblySource::File)
			{
				if (checked.state == ModelOutputState::Current)
					checked = {ModelOutputState::Unknown,
							   QCoreApplication::translate("VibeStudioModelOutputs", "The part %1 is not a file in the assembly.").arg(partId)};
			}
			else
			{
				input.path = QDir(QFileInfo(assemblyPath).absolutePath()).absoluteFilePath(part->source);
				const auto current = modelOutputFileSha256(input.path, control.cancelled);
				if (checked.state == ModelOutputState::Current && current != input.sha256)
					checked = {current.isEmpty() ? ModelOutputState::Missing : ModelOutputState::SourceChanged,
							   QCoreApplication::translate("VibeStudioModelOutputs", "The %1 part's source changed after the package was made.")
								   .arg(input.role)};
			}
		}
		result.inputs.append(input);
	}
	if (withAssembly)
		result.sourcePath = QFileInfo(assemblyPath).absoluteFilePath();
	if (record)
		*record = std::move(result);
	if (status)
		*status = checked;
	return true;
}

QJsonObject modelOutputStatusJson(const ModelOutputRecord &record, const ModelOutputStatus &status, const QString &projectRoot)
{
	auto object = modelOutputRecordJson(record, projectRoot);
	object.insert(QStringLiteral("state"), modelOutputStateId(status.state));
	object.insert(QStringLiteral("stale"), modelOutputStateIsStale(status.state));
	object.insert(QStringLiteral("detail"), status.detail);
	object.insert(QStringLiteral("key"), record.key());
	return object;
}

QString modelOutputStatusLine(const ModelOutputRecord &record, const ModelOutputStatus &status)
{
	const QString first = record.outputs.isEmpty() ? QString() : record.outputs.first().display();
	const int more = int(record.outputs.size()) - 1;
	const QString outputs = more > 0 ? QCoreApplication::translate("VibeStudioModelOutputs", "%1 and %n more", nullptr, more).arg(first) : first;
	return QCoreApplication::translate("VibeStudioModelOutputs", "%1 · %2 · %3: %4")
		.arg(modelOutputStateText(status.state), record.kind, outputs, status.detail);
}

} // namespace vibestudio
