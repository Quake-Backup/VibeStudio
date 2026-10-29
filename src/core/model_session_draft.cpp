#include "core/model_session_draft.h"

#include "core/model_animation_curves.h"
#include "core/model_document.h"
#include "core/model_weight_stroke_recipe.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <limits>

namespace vibestudio
{
namespace
{
const QString suffix = QStringLiteral(".vsmeshsession");
const QString format = QStringLiteral("vibestudio-model-session");
constexpr qint64 headerLimit = 65536;
constexpr int listLimit = 256;

bool fail(QString *error, const QString &text)
{
	if (error)
		*error = text;
	return false;
}
bool validOwner(const QString &id)
{
	const QUuid value(id);
	return !value.isNull() && value.toString(QUuid::WithoutBraces) == id;
}
bool safeFolder(const QString &path)
{
	const QFileInfo info(path);
	return info.isDir() && !info.isSymLink() && !info.isJunction();
}
bool kindFromId(const QString &id, ModelSessionDraftKind *kind)
{
	for (const auto candidate : {ModelSessionDraftKind::WeightPaint, ModelSessionDraftKind::AnimationCurves})
		if (modelSessionDraftKindId(candidate) == id)
		{
			*kind = candidate;
			return true;
		}
	return false;
}
qint64 curveKeys(const ModelPoseCurves &curves)
{
	qint64 keys = 0;
	for (const auto &channel : curves.channels)
		keys += channel.size();
	return keys;
}
QString invalidText() { return QCoreApplication::translate("VibeStudioModelRecovery", "The staged-session file is damaged or not one."); }
// Fills the header fields of `draft` from `object`; false when it is not a
// sidecar header this build reads.
bool parseHeader(const QJsonObject &object, ModelSessionDraft *draft, QByteArray *payloadSha256, qint64 *payloadBytes)
{
	ModelSessionDraftKind kind;
	const auto owner = object.value(QStringLiteral("owner")).toString();
	const auto base = QByteArray::fromHex(object.value(QStringLiteral("baseFingerprint")).toString().toLatin1());
	const auto sha = QByteArray::fromHex(object.value(QStringLiteral("payloadSha256")).toString().toLatin1());
	if (object.value(QStringLiteral("format")).toString() != format || object.value(QStringLiteral("version")) != QJsonValue(1) ||
		!kindFromId(object.value(QStringLiteral("kind")).toString(), &kind) || !validOwner(owner) || base.isEmpty() || sha.size() != 32 ||
		!object.value(QStringLiteral("payloadBytes")).isDouble())
		return false;
	draft->kind = kind;
	draft->ownerId = owner;
	draft->baseFingerprint = base;
	draft->title = object.value(QStringLiteral("title")).toString().left(255);
	draft->ownerProcessId = qint64(object.value(QStringLiteral("ownerProcessId")).toDouble());
	draft->writtenUtc = QDateTime::fromString(object.value(QStringLiteral("writtenUtc")).toString(), Qt::ISODateWithMs);
	draft->surface = object.value(QStringLiteral("surface")).toInt();
	draft->strokeCount = qint64(object.value(QStringLiteral("strokes")).toDouble());
	draft->sampleCount = qint64(object.value(QStringLiteral("samples")).toDouble());
	draft->clip = object.value(QStringLiteral("clip")).toInt(-1);
	draft->frameCount = object.value(QStringLiteral("frameCount")).toInt();
	draft->joint = object.value(QStringLiteral("joint")).toString().left(255);
	draft->keyCount = qint64(object.value(QStringLiteral("keys")).toDouble());
	draft->bake = object.value(QStringLiteral("bake")).toBool();
	*payloadSha256 = sha;
	*payloadBytes = qint64(object.value(QStringLiteral("payloadBytes")).toDouble());
	return true;
}
// The header line of the file at `path`; the file stays open at the payload.
bool readHeader(QFile *file, ModelSessionDraft *draft, QByteArray *payloadSha256, qint64 *payloadBytes)
{
	if (!file->open(QIODevice::ReadOnly))
		return false;
	const auto line = file->readLine(headerLimit);
	if (!line.endsWith('\n'))
		return false;
	QJsonParseError parse;
	const auto document = QJsonDocument::fromJson(line, &parse);
	return parse.error == QJsonParseError::NoError && document.isObject() &&
		   parseHeader(document.object(), draft, payloadSha256, payloadBytes);
}
} // namespace

QString modelSessionDraftKindId(ModelSessionDraftKind kind)
{
	return kind == ModelSessionDraftKind::WeightPaint ? QStringLiteral("weight-paint") : QStringLiteral("animation-curves");
}

QString modelSessionDraftPath(const QString &directory, const QString &ownerId, ModelSessionDraftKind kind)
{
	if (directory.isEmpty() || !validOwner(ownerId))
		return {};
	return QDir(directory).absoluteFilePath(ownerId + QLatin1Char('.') + modelSessionDraftKindId(kind) + suffix);
}

bool writeModelSessionDraft(const QString &directory, const ModelSessionDraft &draft, QString *error)
{
	if (error)
		error->clear();
	const auto path = modelSessionDraftPath(directory, draft.ownerId, draft.kind);
	if (path.isEmpty() || draft.baseFingerprint.isEmpty())
		return fail(error, QCoreApplication::translate("VibeStudioModelRecovery", "Invalid staged-session destination."));
	if (!QFileInfo::exists(directory))
		QDir().mkpath(directory);
	if (!safeFolder(directory))
		return fail(error,
					QCoreApplication::translate("VibeStudioModelRecovery", "The model recovery folder is unavailable or is a link."));
	QJsonObject payload;
	qint64 samples = 0, keys = 0;
	if (draft.kind == ModelSessionDraftKind::WeightPaint)
	{
		payload = modelWeightStrokeRecipeJson(draft.strokes);
		for (const auto &stroke : draft.strokes)
			samples += stroke.samples.size();
	}
	else
	{
		payload = QJsonObject{{QStringLiteral("clip"), draft.clip},
							  {QStringLiteral("frameCount"), draft.frameCount},
							  {QStringLiteral("bake"), draft.bake},
							  {QStringLiteral("curves"), modelPoseCurvesJson(draft.curves)}};
		keys = curveKeys(draft.curves);
	}
	const auto bytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
	if (bytes.size() > modelDocumentMaxSourceBytes)
		return fail(error, QCoreApplication::translate("VibeStudioModelRecovery",
													   "The staged session would be %1, over the 64 MiB limit, so it was not kept.")
							   .arg(modelMebibytesText(bytes.size())));
	const QJsonObject header{
		{QStringLiteral("format"), format},
		{QStringLiteral("version"), 1},
		{QStringLiteral("kind"), modelSessionDraftKindId(draft.kind)},
		{QStringLiteral("owner"), draft.ownerId},
		{QStringLiteral("baseFingerprint"), QString::fromLatin1(draft.baseFingerprint.toHex())},
		{QStringLiteral("title"), draft.title.left(255)},
		{QStringLiteral("ownerProcessId"), double(draft.ownerProcessId > 0 ? draft.ownerProcessId : QCoreApplication::applicationPid())},
		{QStringLiteral("writtenUtc"), (draft.writtenUtc.isValid() ? draft.writtenUtc : QDateTime::currentDateTimeUtc()).toString(Qt::ISODateWithMs)},
		{QStringLiteral("surface"), draft.surface},
		{QStringLiteral("strokes"), double(draft.kind == ModelSessionDraftKind::WeightPaint ? draft.strokes.size() : 0)},
		{QStringLiteral("samples"), double(samples)},
		{QStringLiteral("clip"), draft.clip},
		{QStringLiteral("frameCount"), draft.frameCount},
		{QStringLiteral("joint"), draft.kind == ModelSessionDraftKind::AnimationCurves ? draft.curves.joint.left(255) : QString()},
		{QStringLiteral("keys"), double(keys)},
		{QStringLiteral("bake"), draft.bake},
		{QStringLiteral("payloadSha256"), QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
		{QStringLiteral("payloadBytes"), double(bytes.size())}};
	const QFileInfo target(path);
	if (target.isSymLink() || target.isJunction() || (target.exists() && !target.isFile()))
		return fail(error, QCoreApplication::translate("VibeStudioModelRecovery", "Only regular staged-session files may be replaced."));
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(header).toJson(QJsonDocument::Compact) + '\n') < 0 ||
		file.write(bytes) != bytes.size() || !file.commit())
		return fail(error, QCoreApplication::translate("VibeStudioModelRecovery", "The staged session could not be written: %1")
							   .arg(file.errorString()));
	return true;
}

QVector<ModelSessionDraft> inspectModelSessionDrafts(const QString &directory, const QString &ownerId)
{
	QVector<ModelSessionDraft> drafts;
	if (directory.isEmpty() || !safeFolder(directory) || (!ownerId.isEmpty() && !validOwner(ownerId)))
		return drafts;
	QVector<QFileInfo> files;
	QDirIterator entries(directory, {(ownerId.isEmpty() ? QStringLiteral("*") : ownerId + QStringLiteral(".*")) + suffix},
						 QDir::Files | QDir::NoDotAndDotDot);
	while (entries.hasNext() && files.size() < listLimit)
		files << QFileInfo(entries.next());
	std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b)
			  { return a.lastModified() == b.lastModified() ? a.fileName() < b.fileName() : a.lastModified() > b.lastModified(); });
	for (const auto &info : std::as_const(files))
	{
		ModelSessionDraft draft;
		draft.path = info.absoluteFilePath();
		QFile file(draft.path);
		QByteArray sha;
		qint64 bytes = 0;
		if (info.isSymLink() || info.isJunction() || !readHeader(&file, &draft, &sha, &bytes) ||
			modelSessionDraftPath(directory, draft.ownerId, draft.kind) != QDir(directory).absoluteFilePath(info.fileName()))
			draft.error = invalidText();
		drafts.append(draft);
	}
	return drafts;
}

bool readModelSessionDraft(const QString &path, ModelSessionDraft *draft, QString *error)
{
	if (error)
		error->clear();
	if (!draft)
		return false;
	const QFileInfo info(path);
	ModelSessionDraft result;
	result.path = info.absoluteFilePath();
	QFile file(path);
	QByteArray sha;
	qint64 bytes = 0;
	if (info.isSymLink() || info.isJunction() || !info.isFile() || !readHeader(&file, &result, &sha, &bytes) || bytes < 0 ||
		bytes > modelDocumentMaxSourceBytes)
		return fail(error, invalidText());
	const auto payload = file.read(modelDocumentMaxSourceBytes + 1);
	if (payload.size() != bytes || QCryptographicHash::hash(payload, QCryptographicHash::Sha256) != sha)
		return fail(error, invalidText());
	QJsonParseError parse;
	const auto document = QJsonDocument::fromJson(payload, &parse);
	if (parse.error != QJsonParseError::NoError || !document.isObject())
		return fail(error, invalidText());
	QString problem;
	if (result.kind == ModelSessionDraftKind::WeightPaint)
	{
		if (!parseModelWeightStrokeRecipe(document.object(), &result.strokes, &problem))
			return fail(error, problem);
	}
	else
	{
		const auto object = document.object();
		result.clip = object.value(QStringLiteral("clip")).toInt(-1);
		result.frameCount = object.value(QStringLiteral("frameCount")).toInt();
		result.bake = object.value(QStringLiteral("bake")).toBool();
		if (result.clip < 0 || result.frameCount < 1 ||
			!modelPoseCurvesFromJson(object.value(QStringLiteral("curves")).toObject(), result.frameCount, &result.curves, &problem))
			return fail(error, problem.isEmpty() ? invalidText() : problem);
		result.joint = result.curves.joint;
		result.keyCount = curveKeys(result.curves);
	}
	*draft = std::move(result);
	return true;
}

bool removeModelSessionDrafts(const QString &directory, const QString &ownerId, QString *error, const ModelSessionDraftKind *kind)
{
	if (error)
		error->clear();
	if (directory.isEmpty() || !validOwner(ownerId))
		return fail(error, QCoreApplication::translate("VibeStudioModelRecovery", "Invalid model recovery identifier."));
	if (!QFileInfo::exists(directory))
		return true;
	if (!safeFolder(directory))
		return fail(error,
					QCoreApplication::translate("VibeStudioModelRecovery", "The model recovery folder is unavailable or is a link."));
	for (const auto each : {ModelSessionDraftKind::WeightPaint, ModelSessionDraftKind::AnimationCurves})
	{
		if (kind && *kind != each)
			continue;
		const auto path = modelSessionDraftPath(directory, ownerId, each);
		const QFileInfo info(path);
		if (!info.exists() && !info.isSymLink())
			continue;
		if (info.isSymLink() || info.isJunction() || !info.isFile() || !QFile::remove(path))
			return fail(error, QCoreApplication::translate("VibeStudioModelRecovery", "A staged-session file could not be discarded."));
	}
	return true;
}

QString modelSessionDraftSummary(const ModelSessionDraft &draft)
{
	if (draft.kind == ModelSessionDraftKind::WeightPaint)
	{
		const int strokes = int(draft.strokeCount ? draft.strokeCount : draft.strokes.size());
		const int samples = int(std::min<qint64>(draft.sampleCount, std::numeric_limits<int>::max()));
		return QCoreApplication::translate("VibeStudioModelRecovery", "%n staged weight-paint stroke(s)", nullptr, strokes) +
			   QStringLiteral(" (") + QCoreApplication::translate("VibeStudioModelRecovery", "%n sample(s)", nullptr, samples) +
			   QLatin1Char(')');
	}
	const int keys = int(std::min<qint64>(draft.keyCount, std::numeric_limits<int>::max()));
	const auto joint = draft.joint.isEmpty() ? draft.curves.joint : draft.joint;
	const auto where = draft.bake ? QCoreApplication::translate("VibeStudioModelRecovery", "Staged removal of the curves of joint %1 in clip %2")
							   : QCoreApplication::translate("VibeStudioModelRecovery", "Staged curves of joint %1 in clip %2");
	return where.arg(joint).arg(draft.clip + 1) + QStringLiteral(" (") +
		   QCoreApplication::translate("VibeStudioModelRecovery", "%n key(s)", nullptr, keys) + QLatin1Char(')');
}

QJsonObject modelSessionDraftJson(const ModelSessionDraft &draft)
{
	QJsonObject object{{QStringLiteral("kind"), modelSessionDraftKindId(draft.kind)},
					   {QStringLiteral("owner"), draft.ownerId},
					   {QStringLiteral("path"), draft.path},
					   {QStringLiteral("baseFingerprint"), QString::fromLatin1(draft.baseFingerprint.toHex())},
					   {QStringLiteral("writtenUtc"), draft.writtenUtc.toString(Qt::ISODateWithMs)},
					   {QStringLiteral("ownerProcessId"), double(draft.ownerProcessId)},
					   {QStringLiteral("valid"), draft.error.isEmpty()}};
	if (!draft.error.isEmpty())
		object.insert(QStringLiteral("error"), draft.error);
	if (draft.kind == ModelSessionDraftKind::WeightPaint)
	{
		object.insert(QStringLiteral("surface"), draft.surface);
		object.insert(QStringLiteral("strokes"), double(draft.strokeCount));
		object.insert(QStringLiteral("samples"), double(draft.sampleCount));
	}
	else
	{
		object.insert(QStringLiteral("clip"), draft.clip);
		object.insert(QStringLiteral("joint"), draft.joint);
		object.insert(QStringLiteral("keys"), double(draft.keyCount));
		object.insert(QStringLiteral("bake"), draft.bake);
	}
	return object;
}

} // namespace vibestudio
