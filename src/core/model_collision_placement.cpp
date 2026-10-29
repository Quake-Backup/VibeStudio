#include "core/model_collision_placement.h"

#include "core/level_paste.h"
#include "core/level_scene.h"
#include "core/model_document.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelCollision)
};
QString property(const LevelMapEntity &entity, const QString &key)
{
	for (const auto &item : entity.properties)
	{
		if (item.key.compare(key, Qt::CaseInsensitive) == 0)
			return item.value;
	}
	return {};
}
bool hasProperty(const LevelMapEntity &entity, const QString &key)
{
	return std::any_of(entity.properties.cbegin(), entity.properties.cend(),
					   [&](const auto &item) { return item.key.compare(key, Qt::CaseInsensitive) == 0; });
}
QString mapFolder(const LevelMapDocument &map)
{
	return map.sourcePath.isEmpty() ? QString() : QFileInfo(map.sourcePath).absolutePath();
}
QString recordedSource(const QString &source, const LevelMapDocument &map)
{
	if (source.isEmpty())
		return {};
	const auto absolute = QDir::cleanPath(QFileInfo(source).absoluteFilePath());
	const auto folder = mapFolder(map);
	if (folder.isEmpty())
		return absolute;
	const auto relative = QDir(folder).relativeFilePath(absolute);
	// Another drive stays absolute; a relative path keeps the record portable
	// when the project folder moves with the map.
	return QDir::fromNativeSeparators(QFileInfo(relative).isAbsolute() ? absolute : relative);
}
QString resolvedSource(const QString &source, const LevelMapDocument &map)
{
	if (source.isEmpty())
		return {};
	if (QFileInfo(source).isAbsolute())
		return QDir::cleanPath(source);
	const auto folder = mapFolder(map);
	return folder.isEmpty() ? QString() : QDir::cleanPath(QDir(folder).absoluteFilePath(source));
}
QString number(double value)
{
	return QString::number(value, 'g', 17);
}
const LevelMapEntity *entityWithId(const LevelMapDocument &map, int id)
{
	for (const auto &entity : map.entities)
	{
		if (entity.id == id)
			return &entity;
	}
	return nullptr;
}
} // namespace

QByteArray modelCollisionFingerprint(const ModelMesh &mesh)
{
	return QCryptographicHash::hash(QJsonDocument(modelCollisionJson(mesh)).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
}

QString modelCollisionRecordStatusId(ModelCollisionRecordStatus status)
{
	switch (status)
	{
	case ModelCollisionRecordStatus::Current:
		return QStringLiteral("current");
	case ModelCollisionRecordStatus::Stale:
		return QStringLiteral("stale");
	case ModelCollisionRecordStatus::OtherSource:
		return QStringLiteral("other-source");
	case ModelCollisionRecordStatus::Invalid:
		break;
	}
	return QStringLiteral("invalid");
}

QVector<LevelMapProperty> modelCollisionRecordProperties(const ModelMesh &mesh, const ModelCollisionExport &request,
														 const LevelMapDocument &map)
{
	std::array<double, 12> affine{1, 0, 0, request.origin.x, 0, 1, 0, request.origin.y, 0, 0, 1, request.origin.z};
	if (request.instance)
		affine = *request.instance;
	QStringList transform;
	for (double value : affine)
		transform << number(value);
	QVector<LevelMapProperty> keys;
	const auto source = recordedSource(request.sourcePath, map);
	// Engines read keys and values up to 1,024 bytes; a longer path is not recorded.
	if (!source.isEmpty() && source.toUtf8().size() < 1000)
		keys.append({QString::fromLatin1(modelCollisionRecordSource), source});
	keys.append({QString::fromLatin1(modelCollisionRecordRevision), QString::fromLatin1(modelCollisionFingerprint(mesh).toHex())});
	keys.append({QString::fromLatin1(modelCollisionRecordFrame), QString::number(request.frame)});
	keys.append({QString::fromLatin1(modelCollisionRecordTarget), request.target});
	if (request.target == QStringLiteral("quake3"))
		keys.append({QString::fromLatin1(modelCollisionRecordMaterial), request.material});
	keys.append({QString::fromLatin1(modelCollisionRecordTransform), transform.join(' ')});
	if (request.instance && !request.instanceModel.isEmpty() && request.instanceModel.toUtf8().size() < 1000)
		keys.append({QString::fromLatin1(modelCollisionRecordModel), request.instanceModel});
	return keys;
}

QVector<ModelCollisionRecord> levelCollisionRecords(const LevelMapDocument &map, const ModelMesh *mesh, const QString &meshSource)
{
	QVector<ModelCollisionRecord> records;
	const auto fingerprint = mesh ? modelCollisionFingerprint(*mesh) : QByteArray();
	for (const auto &entity : map.entities)
	{
		if (entity.className.compare(QStringLiteral("func_group"), Qt::CaseInsensitive) != 0 ||
			!hasProperty(entity, QString::fromLatin1(modelCollisionRecordRevision)))
			continue;
		ModelCollisionRecord record;
		record.entityId = entity.id;
		record.source = property(entity, QString::fromLatin1(modelCollisionRecordSource));
		record.resolvedSource = resolvedSource(record.source, map);
		record.revision = QByteArray::fromHex(property(entity, QString::fromLatin1(modelCollisionRecordRevision)).toLatin1());
		record.target = property(entity, QString::fromLatin1(modelCollisionRecordTarget));
		record.material = property(entity, QString::fromLatin1(modelCollisionRecordMaterial));
		record.model = property(entity, QString::fromLatin1(modelCollisionRecordModel));
		bool frameValid = false;
		record.frame = property(entity, QString::fromLatin1(modelCollisionRecordFrame)).toInt(&frameValid);
		const auto values = property(entity, QString::fromLatin1(modelCollisionRecordTransform)).split(' ', Qt::SkipEmptyParts);
		bool transformValid = values.size() == 12;
		for (int i = 0; transformValid && i < 12; ++i)
		{
			record.transform[i] = values[i].toDouble(&transformValid);
			transformValid = transformValid && std::isfinite(record.transform[i]);
		}
		for (const auto &brush : map.brushes)
			record.brushCount += brush.entityId == entity.id ? 1 : 0;
		const bool targetValid = record.target == QStringLiteral("quake") || record.target == QStringLiteral("quake2") ||
								 record.target == QStringLiteral("quake3");
		if (record.revision.size() != 32 || !frameValid || record.frame < -1 || !transformValid || !targetValid)
		{
			record.problem = Text::tr("The collision record on entity %1 is incomplete or malformed.").arg(entity.id);
			records << record;
			continue;
		}
		const bool sameSource = !record.resolvedSource.isEmpty() && modelPathsReferToSameFile(record.resolvedSource, meshSource);
		if (!mesh || !sameSource)
			record.status = ModelCollisionRecordStatus::OtherSource;
		else
			record.status = record.revision == fingerprint ? ModelCollisionRecordStatus::Current : ModelCollisionRecordStatus::Stale;
		records << record;
	}
	return records;
}

bool modelCollisionInstanceFromSelection(const LevelMapDocument &map, ModelCollisionExport *request, int entityId, QString *error)
{
	if (error)
		error->clear();
	const int id = entityId >= 0 ? entityId : map.selectionKind == LevelMapSelectionKind::Entity ? map.selectedObjectId : -1;
	const auto *entity = id >= 0 ? entityWithId(map, id) : nullptr;
	if (!request || !entity || !levelEntityPlacesModel(*entity))
	{
		if (error)
			*error = Text::tr("Select a model entity (one with a model key, such as misc_model) in the level first.");
		return false;
	}
	request->instance = levelModelInstanceAffine(levelModelInstanceTransform(*entity));
	request->instanceModel = property(*entity, QStringLiteral("model"));
	request->origin = {};
	return true;
}

LevelPlacementResult prepareModelCollisionRefresh(const ModelMesh &mesh, const QString &meshSource, const LevelMapDocument &map, int entityId,
												  int instanceEntityId, bool allowUnrecordedSource, const ModelWorkControl &control)
{
	LevelPlacementResult result;
	const auto records = levelCollisionRecords(map, &mesh, meshSource);
	const auto found =
		std::find_if(records.cbegin(), records.cend(), [&](const ModelCollisionRecord &record) { return record.entityId == entityId; });
	if (found == records.cend())
	{
		result.error = Text::tr("Entity %1 is not a placed collision record. Choose a func_group placed by the modeller.").arg(entityId);
		return result;
	}
	const auto &record = *found;
	if (record.status == ModelCollisionRecordStatus::Invalid)
	{
		result.error = record.problem;
		return result;
	}
	if (record.status == ModelCollisionRecordStatus::OtherSource && !(allowUnrecordedSource && record.source.isEmpty()))
	{
		result.error = Text::tr("Collision record %1 was placed from %2, not from this model. Open that model to refresh it.")
						   .arg(entityId)
						   .arg(record.source.isEmpty() ? Text::tr("an unsaved model") : record.source);
		return result;
	}
	ModelCollisionExport request;
	request.target = record.target;
	request.material = record.material;
	request.frame = record.frame;
	request.sourcePath = record.resolvedSource.isEmpty() ? meshSource : record.resolvedSource;
	request.instanceModel = record.model;
	if (instanceEntityId >= 0)
	{
		if (!modelCollisionInstanceFromSelection(map, &request, instanceEntityId, &result.error))
			return result;
	}
	else
	{
		// The recorded rows, reused exactly rather than re-derived from angles.
		request.instance = record.transform;
	}
	const bool q3 = map.format == LevelMapFormat::Quake3Map;
	if ((map.format != LevelMapFormat::QuakeMap && !q3) || q3 != (request.target == QStringLiteral("quake3")))
	{
		result.error = Text::tr("The collision export target must match the open Quake-family map.");
		return result;
	}
	ModelCollisionMap exported;
	if (!exportModelCollisionMap(mesh, request, &exported, &result.error, control, modelCollisionRecordProperties(mesh, request, map)))
	{
		result.cancelled = control.cancelled && control.cancelled();
		return result;
	}
	const QString node = levelSceneMembership(map.scene, levelMapSelectionRefId({LevelMapSelectionKind::Entity, entityId}));
	// The new func_group may reuse the removed one's id.
	QSet<int> before;
	for (const auto &entity : map.entities)
		if (entity.id != entityId)
			before.insert(entity.id);
	LevelMapDocument candidate = map;
	// Room for the steps folded below, so none is trimmed before it is counted.
	const int undoLimit = candidate.undoLimit;
	candidate.undoLimit = std::max(undoLimit > 0 ? undoLimit : 200, int(candidate.undoStack.size()) + 8);
	const auto revision = candidate.undoStack.size();
	int steps = 0;
	QString error;
	LevelPlacementControl placementControl{control.cancelled, [progress = control.progress](const LevelPlacementProgress &p) {
											   if (progress)
												   progress(ModelWorkPhase::Editing, p.completed, p.total);
										   }};
	// The func_group takes its brushes with it; scene locks refuse the deletion.
	if (!deleteLevelMapObjects(&candidate, {{LevelMapSelectionKind::Entity, entityId}}, &error))
	{
		result.error = error;
		return result;
	}
	if (control.cancelled && control.cancelled())
	{
		result.cancelled = true;
		result.error = Text::tr("Refresh cancelled. The map was not changed.");
		return result;
	}
	steps = int(candidate.undoStack.size() - revision);
	const auto afterRemoval = candidate.undoStack.size();
	LevelPlacementRequest insertion;
	insertion.operation = LevelPlacementOperation::Paste;
	insertion.text = QString::fromUtf8(exported.bytes);
	auto inserted = prepareLevelPlacement(candidate, insertion, placementControl);
	if (!inserted.succeeded)
	{
		result.error = inserted.error;
		result.cancelled = inserted.cancelled;
		return result;
	}
	candidate = std::move(inserted.document);
	steps += int(candidate.undoStack.size() - afterRemoval);
	int created = -1;
	QStringList objects;
	for (const auto &entity : candidate.entities)
	{
		if (!before.contains(entity.id) && entity.className.compare(QStringLiteral("func_group"), Qt::CaseInsensitive) == 0)
			created = entity.id;
	}
	if (created < 0)
	{
		result.error = Text::tr("The refreshed collision could not be placed.");
		return result;
	}
	if (!node.isEmpty())
	{
		objects << levelMapSelectionRefId({LevelMapSelectionKind::Entity, created});
		for (const auto &brush : candidate.brushes)
		{
			if (brush.entityId == created)
				objects << levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush, brush.id});
		}
		const auto beforeAssign = candidate.undoStack.size();
		if (levelSceneMembership(candidate.scene, objects.first()) != node &&
			!assignLevelSceneObjects(&candidate, node, objects, &error))
		{
			result.error = error;
			return result;
		}
		steps += int(candidate.undoStack.size() - beforeAssign);
	}
	if (steps > 1 &&
		!collapseLevelMapUndoSteps(&candidate, steps, Text::tr("Refresh collision from %1").arg(QFileInfo(request.sourcePath).fileName()),
								   Text::tr("Restore the previous collision brushes"), &error))
	{
		result.error = error;
		return result;
	}
	candidate.undoLimit = undoLimit;
	result.document = std::move(candidate);
	result.succeeded = true;
	return result;
}
} // namespace vibestudio
