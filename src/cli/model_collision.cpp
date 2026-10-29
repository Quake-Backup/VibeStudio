#include "cli/model_collision.h"
#include "cli/model_transform_options.h"
#include "core/model_collision.h"
#include "core/model_collision_placement.h"
#include "core/model_collision_hull.h"
#include "core/model_collision_doom3.h"
#include "core/model_document.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <cmath>
#include <tuple>

namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelCollisionCli)
};
ModelCollisionCliResult fail(int code, const QString &error)
{
	return {code, error, {}, {}};
}
bool vector(const QString &text, ModelVec3 *value)
{
	const auto parts = text.split(',');
	if (parts.size() != 3)
	{
		return false;
	}
	float values[3]{};
	for (int i = 0; i < 3; ++i)
	{
		bool ok = false;
		const auto number = parts[i].toDouble(&ok);
		if (!ok || !std::isfinite(number) || std::abs(number) > 1000000)
		{
			return false;
		}
		values[i] = float(number);
	}
	*value = {values[0], values[1], values[2]};
	return true;
}
bool integer(const QString &text, int *value)
{
	static const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
	bool ok = false;
	*value = text.toInt(&ok);
	return ok && digits.match(text).hasMatch();
}
QString vectorText(ModelVec3 value)
{
	return QStringLiteral("%1,%2,%3").arg(value.x, 0, 'g', 9).arg(value.y, 0, 'g', 9).arg(value.z, 0, 'g', 9);
}
} // namespace
ModelCollisionCliResult runModelCollision(const QStringList &arguments)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run"};
	const QSet<QString> globals{"--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> options{"--input",		"--operation",		 "--output",		"--box",		"--name",	   "--centre",
								"--size",		"--rotation",		 "--frame",			"--surface",	"--vertices",  "--faces",
								"--edges",		"--target",			 "--material",		"--origin",		"--map",	   "--offset",
								"--rotate",		"--scale",			 "--pivot",			"--pivot-mode", "--snap-grid", "--snap-angle",
								"--snap-scale", "--transform-space", "--axis-rotation", "--axes-frame", "--instance", "--group",
								"--profile",	"--max-faces"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	for (int i = 1; i < arguments.size(); ++i)
	{
		const auto arg = arguments[i];
		if (!arg.startsWith('-'))
		{
			positional << arg;
			continue;
		}
		const auto equal = arg.indexOf('=');
		const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key))
		{
			return fail(2, Text::tr("Repeated option: %1").arg(key));
		}
		seen.insert(key);
		if (flags.contains(key) && equal < 0)
		{
			continue;
		}
		if (!options.contains(key) && !globals.contains(key))
		{
			return fail(2, Text::tr("Unexpected option: %1").arg(arg));
		}
		const auto value = equal >= 0														? arg.mid(equal + 1)
						   : i + 1 < arguments.size() && !arguments[i + 1].startsWith("--") ? arguments[++i]
																							: QString();
		if (value.trimmed().isEmpty())
		{
			return fail(2, Text::tr("Missing value for %1.").arg(key));
		}
		values.insert(key, value);
	}
	if (positional.size() != (values.contains("--input") ? 2 : 3))
	{
		return fail(2, Text::tr("Use model collision <source.mesh.json> with one explicit input source."));
	}
	const auto operation = values.value("--operation", "inspect");
	const bool exportMap = operation == "export-map", place = operation == "place";
	// Placed collision records in a level (core/model_collision_placement.h).
	const bool refresh = operation == "refresh", placed = operation == "placed";
	// Doom 3 collision (core/model_collision_doom3.h).
	const bool exportAse = operation == "export-ase", exportClip = operation == "export-clipmodel";
	const bool transform = operation == "transform";
	const bool hull = operation == "fit-hull";
	const bool edit = QSet<QString>{"add", "fit", "fit-animated", "fit-hull", "animate", "freeze", "update", "duplicate", "delete", "transform"}
						  .contains(operation);
	if (!edit && !exportMap && !place && !refresh && !placed && !exportAse && !exportClip && operation != "inspect")
	{
		return fail(2, Text::tr("Use inspect, add, fit, fit-animated, fit-hull, animate, freeze, update, transform, duplicate, delete, "
								"export-map, place, placed, refresh, export-ase or export-clipmodel."));
	}
	QSet<QString> allowed{"--input", "--operation", "--cli", "--json", "--quiet", "--verbose"};
	allowed.unite(globals);
	if (operation != "inspect" && !placed)
	{
		allowed.unite({"--output", "--overwrite", "--dry-run"});
	}
	if (operation == "add" || operation == "update")
	{
		allowed.unite({"--name", "--centre", "--size", "--rotation"});
	}
	if (operation == "fit" || operation == "fit-animated" || hull)
	{
		allowed.unite({"--name", "--surface", "--vertices", "--faces", "--edges"});
	}
	if (hull)
		allowed.unite({"--profile", "--max-faces"});
	if (operation == "fit" || hull || operation == "freeze" || operation == "update" || transform || exportMap || place || operation == "inspect")
		allowed << "--frame";
	if (operation == "duplicate")
	{
		allowed << "--name";
	}
	if (transform)
	{
		allowed.unite({"--offset", "--rotate", "--scale", "--pivot", "--pivot-mode", "--snap-grid", "--snap-angle", "--snap-scale",
					   "--transform-space", "--axis-rotation", "--axes-frame"});
	}
	if (operation == "update" || operation == "duplicate" || operation == "delete" || operation == "animate" || operation == "freeze" ||
		transform)
	{
		allowed << "--box";
	}
	if (exportMap || place)
	{
		allowed.unite({"--target", "--material", "--origin"});
	}
	if (place)
	{
		allowed << "--map" << "--instance";
	}
	if (refresh || placed)
	{
		// --target is only a hint for reading the map; records name their own.
		allowed << "--map" << "--target";
	}
	if (refresh)
	{
		allowed << "--group" << "--instance";
	}
	if (exportAse || exportClip)
		allowed.unite({"--target", "--material", "--frame"});
	if (exportClip)
		allowed << "--box";
	if (!(seen - allowed).isEmpty())
	{
		return fail(2, Text::tr("Option does not apply to %1: %2").arg(operation, QStringList((seen - allowed).values()).join(", ")));
	}
	const auto source = values.value("--input", positional.value(2));
	const auto output = values.value("--output");
	if (operation != "inspect" && !placed && output.isEmpty())
	{
		return fail(2, Text::tr("This operation requires --output."));
	}
	if ((place || refresh || placed) && !values.contains("--map"))
		return fail(2, Text::tr("Placement, refresh and listing placed collision require --map."));
	if (place && values.contains("--instance") && values.contains("--origin"))
		return fail(2, Text::tr("--instance places collision through that entity's transform; do not combine it with --origin."));
	if ((exportAse || exportClip) && (values.value("--target") != "doom3" || !output.endsWith(".ase", Qt::CaseInsensitive) ||
									  (exportClip && !values.contains("--box"))))
		return fail(2, Text::tr("Doom 3 collision exports need --target doom3 and an .ase output; export-clipmodel also needs --box."));
	if ((edit && !output.endsWith(".mesh.json", Qt::CaseInsensitive)) ||
		((exportMap || place || refresh) && !output.endsWith(".map", Qt::CaseInsensitive)))
	{
		return fail(2, Text::tr("Authoring outputs use .mesh.json; collision export and placement outputs use .map."));
	}
	const bool dry = seen.contains("--dry-run"), overwrite = seen.contains("--overwrite");
	QString error;
	ModelDocument document;
	if (!document.load(source, &error))
	{
		return fail(4, error);
	}
	int frame = -1;
	if (values.contains("--frame") && values.value("--frame") != "all" &&
		(!integer(values.value("--frame"), &frame) || frame >= document.mesh().frames.size()))
		return fail(2, Text::tr("Use an existing zero-based frame, or all where the operation supports it."));
	const auto entitySelector = [&](const QString &key, int *id) {
		static const QRegularExpression pattern(QStringLiteral("^entity:([0-9]+)$"));
		const auto match = pattern.match(values.value(key));
		bool ok = false;
		*id = match.hasMatch() ? match.captured(1).toInt(&ok) : -1;
		return ok;
	};
	int instanceId = -1, groupId = -1;
	if ((values.contains("--instance") && !entitySelector("--instance", &instanceId)) ||
		(values.contains("--group") && !entitySelector("--group", &groupId)))
		return fail(2, Text::tr("Name entities as entity:N, as map inspect lists them."));
	if ((operation == "freeze" || operation == "inspect" || exportMap || place) && values.contains("--frame") && frame < 0)
		return fail(2, Text::tr("This operation requires one stored frame, not all."));
	if (operation == "freeze" && frame < 0)
		return fail(2, Text::tr("Freeze requires --frame N to select the retained pose."));
	ModelCollisionCliResult result;
	result.payload = {{"operation", operation}, {"written", false}, {"dryRun", dry}};
	QByteArray bytes;
	if (edit)
	{
		ModelEdit change;
		change.kind = transform						? ModelEditKind::TransformCollisionBox
					  : hull						? ModelEditKind::FitCollisionHull
					  : operation == "fit-animated" ? ModelEditKind::FitAnimatedCollisionBox
					  : operation == "animate"		? ModelEditKind::AnimateCollisionBox
					  : operation == "freeze"		? ModelEditKind::FreezeCollisionBox
					  : operation == "add"			? ModelEditKind::AddCollisionBox
					  : operation == "fit"			? ModelEditKind::FitCollisionBox
					  : operation == "update"		? ModelEditKind::UpdateCollisionBox
					  : operation == "duplicate"	? ModelEditKind::DuplicateCollisionBox
													: ModelEditKind::DeleteCollisionBox;
		change.selection.collision = values.value("--box");
		change.frame = frame;
		if (operation == "update")
		{
			const auto box = findModelCollisionBox(document.mesh(), change.selection.collision);
			if (!box)
			{
				return fail(2, Text::tr("Choose an existing --box name."));
			}
			change.collisionBox = *box;
			change.collisionBox.framePoses.clear();
			change.collisionFields =
				(values.contains("--centre") ? 1 : 0) | (values.contains("--size") ? 2 : 0) | (values.contains("--rotation") ? 4 : 0);
		}
		const auto selected = findModelCollisionBox(document.mesh(), change.selection.collision);
		if (selected && selected->framePoses.isEmpty() && frame >= 0 && (transform || operation == "update"))
			return fail(2, Text::tr("A static box has no frame-local edit scope. Animate it first, or use all frames."));
		if (selected && !selected->framePoses.isEmpty() && (transform || (operation == "update" && change.collisionFields)) &&
			!seen.contains("--frame"))
			return fail(2, Text::tr("Animated collision edits require an explicit --frame all|N scope."));
		if (values.contains("--name"))
		{
			change.collisionBox.name = values.value("--name");
		}
		if (operation != "delete" && operation != "animate" && operation != "freeze" && !transform && change.collisionBox.name.isEmpty())
		{
			return fail(2, Text::tr("A new collision box requires --name."));
		}
		for (const auto &[key, field] :
			 std::array<std::pair<const char *, ModelVec3 *>, 3>{{{"--centre", &change.collisionBox.centre},
																  {"--size", &change.collisionBox.size},
																  {"--rotation", &change.collisionBox.rotation}}})
		{
			if (values.contains(key) && !vector(values.value(key), field))
			{
				return fail(2, Text::tr("Use three finite comma-separated numbers for %1.").arg(key));
			}
		}
		if (transform)
		{
			if (!parseModelTransformAxesOptions(values, int(document.mesh().frames.size()), &change, &error))
				return fail(2, error);
			for (const auto &[key, field] : std::array<std::pair<const char *, ModelVec3 *>, 4>{{{"--offset", &change.translation},
																								 {"--rotate", &change.rotation},
																								 {"--scale", &change.scale},
																								 {"--pivot", &change.pivot}}})
			{
				if (values.contains(key) && !vector(values.value(key), field))
				{
					return fail(2, Text::tr("Use three finite comma-separated numbers for %1.").arg(key));
				}
			}
			const auto pivot = values.value("--pivot-mode", values.contains("--pivot") ? "custom" : "selection");
			if (!QSet<QString>{"origin", "selection", "custom"}.contains(pivot) || (pivot != "custom" && values.contains("--pivot")) ||
				(pivot == "custom" && !values.contains("--pivot")))
			{
				return fail(2, Text::tr("Choose --pivot-mode selection|origin|custom; custom requires --pivot X,Y,Z."));
			}
			change.pivotMode = pivot == "selection" ? ModelTransformPivot::SelectionCentre
							   : pivot == "origin"	? ModelTransformPivot::Origin
													: ModelTransformPivot::Custom;
			for (const auto &[key, field] : std::array<std::pair<const char *, double *>, 3>{{{"--snap-grid", &change.translationGrid},
																							  {"--snap-angle", &change.rotationGrid},
																							  {"--snap-scale", &change.scaleGrid}}})
			{
				if (!values.contains(key))
				{
					continue;
				}
				bool valid = false;
				*field = values.value(key).toDouble(&valid);
				ModelVec3 checked;
				if (!valid || !snapModelTranslation({}, change.translationGrid, &checked) ||
					!snapModelRotation({}, change.rotationGrid, &checked) || !snapModelScale({1, 1, 1}, change.scaleGrid, &checked))
				{
					return fail(2, Text::tr("Use a valid transform snap step for %1.").arg(key));
				}
			}
		}
		if (hull)
		{
			change.collisionHullProfile = values.value("--profile", QStringLiteral("quake3"));
			if (values.contains("--max-faces") && !integer(values.value("--max-faces"), &change.collisionHullMaxFaces))
				return fail(2, Text::tr("--max-faces takes a whole number of faces."));
			if (!modelCollisionHullBudget(change.collisionHullProfile, change.collisionHullMaxFaces, nullptr, &error))
				return fail(2, error);
		}
		if (operation == "fit" || operation == "fit-animated" || hull)
		{
			if (values.contains("--surface") && (!integer(values.value("--surface"), &change.selection.surface) ||
												 change.selection.surface >= document.mesh().surfaces.size()))
			{
				return fail(2, Text::tr("Choose an existing zero-based surface."));
			}
			const auto &surface = document.mesh().surfaces[change.selection.surface];
			for (const auto &[key, selected, limit] : std::array<std::tuple<const char *, QSet<int> *, int>, 2>{
					 {{"--vertices", &change.selection.vertices, surface.vertexCount},
					  {"--faces", &change.selection.faces, int(surface.triangles.size())}}})
			{
				if (!values.contains(key))
				{
					continue;
				}
				if (values.value(key) == "all")
				{
					for (int n = 0; n < limit; ++n)
					{
						selected->insert(n);
					}
					continue;
				}
				for (const auto &part : values.value(key).split(','))
				{
					int index = -1;
					if (!integer(part, &index) || index >= limit || selected->contains(index))
					{
						return fail(2, Text::tr("Use distinct existing component indices or all for %1.").arg(key));
					}
					selected->insert(index);
				}
			}
			if (values.contains("--edges"))
			{
				for (const auto &part : values.value("--edges").split(','))
				{
					const auto pair = part.split('-');
					int a = -1, b = -1;
					if (pair.size() != 2 || !integer(pair[0], &a) || !integer(pair[1], &b) || a >= b ||
						change.selection.edges.contains({a, b}))
					{
						return fail(2, Text::tr("Use distinct canonical edge pairs, for example --edges 0-1,1-2."));
					}
					change.selection.edges.insert({a, b});
				}
			}
			if (values.contains("--surface") && change.selection.vertices.isEmpty() && change.selection.faces.isEmpty() &&
				change.selection.edges.isEmpty())
			{
				return fail(2,
							Text::tr("Use --vertices all to fit one surface; without component selectors fitting spans the whole model."));
			}
		}
		if (!document.edit(change, &error))
		{
			return fail(4, error);
		}
		if (const auto *fitted = hull ? findModelCollisionBox(document.mesh(), change.collisionBox.name) : nullptr)
		{
			ModelCollisionHullShape shape;
			if (modelCollisionHullShape(fitted->hull, &shape))
			{
				result.payload.insert("hull", QJsonObject{{"name", fitted->name},
														  {"profile", change.collisionHullProfile},
														  {"vertices", int(shape.vertices.size())},
														  {"edges", int(shape.edges.size())},
														  {"faces", int(shape.faces.size())},
														  {"volume", shape.volume}});
				const int vertices = int(shape.vertices.size()), edges = int(shape.edges.size()), faces = int(shape.faces.size());
				result.lines << Text::tr("Hull %1: %2, %3, %4.")
									.arg(fitted->name, Text::tr("%n vertex(es)", nullptr, vertices), Text::tr("%n edge(s)", nullptr, edges),
										 Text::tr("%n face(s)", nullptr, faces));
			}
		}
		bytes = serializeEditableModel(document.mesh(), &error);
	}
	else if (exportMap || place)
	{
		ModelCollisionExport request{values.value("--target"), values.value("--material"), {}, frame};
		request.sourcePath = source;
		if (!seen.contains("--target") || (values.contains("--origin") && !vector(values.value("--origin"), &request.origin)))
		{
			return fail(2, Text::tr("Use --target quake|quake2|quake3 and an optional finite --origin X,Y,Z."));
		}
		LevelMapDocument sourceMap;
		if (place)
		{
			if (!loadLevelMap({values.value("--map"), {}, request.target == "quake3" ? "idtech3" : "idtech2"}, &sourceMap, &error))
				return fail(4, error);
			if (instanceId >= 0 && !modelCollisionInstanceFromSelection(sourceMap, &request, instanceId, &error))
				return fail(4, error);
		}
		ModelCollisionMap map;
		if (!exportModelCollisionMap(document.mesh(), request, &map, &error))
		{
			return fail(4, error);
		}
		result.payload.insert("notes", QJsonArray::fromStringList(map.notes));
		result.payload.insert("brushes", map.brushCount);
		result.payload.insert("material", map.material);
		result.payload.insert("target", request.target);
		if (request.frame >= 0)
			result.payload.insert("frame", request.frame);
		result.lines += map.notes;
		bytes = map.bytes;
		if (place)
		{
			const auto placement = prepareModelCollisionPlacement(document.mesh(), request, sourceMap);
			if (!placement.succeeded)
			{
				return fail(4, placement.error);
			}
			const auto serialized = serializeLevelMap(placement.document);
			if (!serialized.succeeded())
			{
				return fail(4, serialized.errors.join('\n'));
			}
			bytes = serialized.bytes;
			if (instanceId >= 0)
				result.payload.insert("instance", QStringLiteral("entity:%1").arg(instanceId));
		}
	}
	else if (exportAse || exportClip)
	{
		ModelCollisionSurfaceExport request;
		if (values.contains("--material"))
			request.material = values.value("--material");
		request.frame = std::max(0, frame);
		const bool animated = std::any_of(document.mesh().collisionBoxes.cbegin(), document.mesh().collisionBoxes.cend(),
										  [](const auto &box) { return !box.framePoses.isEmpty(); });
		if (animated && frame < 0)
			return fail(2, Text::tr("Animated collision requires --frame N; Doom 3 receives that pose."));
		result.payload.insert("target", QStringLiteral("doom3"));
		result.payload.insert("material", request.material);
		result.payload.insert("frame", request.frame);
		if (exportAse)
		{
			ModelExportReport report;
			bytes = exportModelAseWithCollision(document.mesh(), request, &error, {}, &report);
			if (bytes.isEmpty())
				return fail(4, error);
			const int volumes = int(document.mesh().collisionBoxes.size());
			result.payload.insert("collisionSurfaces", volumes);
			result.payload.insert("notes", QJsonArray::fromStringList(report.notes));
			result.lines += report.notes;
		}
		else
		{
			ModelDoom3ClipModel clip;
			if (!exportModelDoom3ClipModel(document.mesh(), values.value("--box"), request, &clip, &error))
				return fail(4, error);
			bytes = clip.bytes;
			QJsonObject details{{"box", values.value("--box")}, {"faces", clip.faces}, {"edges", clip.edges}, {"vertices", clip.vertices}};
			if (clip.hasBounds)
			{
				details.insert("mins", QStringLiteral("%1 %2 %3").arg(clip.mins.x, 0, 'g', 9).arg(clip.mins.y, 0, 'g', 9).arg(clip.mins.z, 0, 'g', 9));
				details.insert("maxs", QStringLiteral("%1 %2 %3").arg(clip.maxs.x, 0, 'g', 9).arg(clip.maxs.y, 0, 'g', 9).arg(clip.maxs.z, 0, 'g', 9));
			}
			result.payload.insert("clipModel", details);
			result.payload.insert("notes", QJsonArray::fromStringList(clip.notes));
			result.lines += clip.notes;
		}
	}
	else if (refresh || placed)
	{
		const auto hint = values.value("--target");
		if (!hint.isEmpty() && hint != "quake" && hint != "quake2" && hint != "quake3")
			return fail(2, Text::tr("Use --target quake|quake2|quake3."));
		LevelMapDocument sourceMap;
		const QString engine = hint.isEmpty() ? QString() : hint == "quake3" ? QStringLiteral("idtech3") : QStringLiteral("idtech2");
		if (!loadLevelMap({values.value("--map"), {}, engine}, &sourceMap, &error))
			return fail(4, error);
		// A map with only plain brushes reads as Quake II-style text; Quake III
		// records say which engine their brushes are for.
		if (hint.isEmpty() && sourceMap.format == LevelMapFormat::QuakeMap)
		{
			const auto guessed = levelCollisionRecords(sourceMap);
			if (std::any_of(guessed.cbegin(), guessed.cend(), [](const auto &record) { return record.target == QStringLiteral("quake3"); }) &&
				!loadLevelMap({values.value("--map"), {}, QStringLiteral("idtech3")}, &sourceMap, &error))
				return fail(4, error);
		}
		const auto records = levelCollisionRecords(sourceMap, &document.mesh(), source);
		QJsonArray listed;
		int refreshed = 0;
		LevelMapDocument working = sourceMap;
		for (const auto &record : records)
		{
			// Without --group, refresh every stale record placed from this mesh.
			const bool chosen = refresh && (groupId >= 0 ? record.entityId == groupId : record.status == ModelCollisionRecordStatus::Stale);
			if (chosen)
			{
				const auto updated = prepareModelCollisionRefresh(document.mesh(), source, working, record.entityId, instanceId);
				if (!updated.succeeded)
					return fail(4, updated.error);
				working = updated.document;
				++refreshed;
			}
			listed.append(QJsonObject{{"group", QStringLiteral("entity:%1").arg(record.entityId)},
									  {"status", modelCollisionRecordStatusId(record.status)},
									  {"source", record.source},
									  {"revision", QString::fromLatin1(record.revision.toHex())},
									  {"frame", record.frame},
									  {"target", record.target},
									  {"material", record.material},
									  {"model", record.model},
									  {"brushes", record.brushCount},
									  {"refreshed", chosen},
									  {"problem", record.problem}});
			const auto shownSource = record.source.isEmpty() ? QStringLiteral("-") : record.source;
			if (chosen)
				result.lines << Text::tr("entity:%1 · %2 · %3 · frame %4 · %n brush(es) · refreshed", nullptr, record.brushCount)
									.arg(record.entityId)
									.arg(modelCollisionRecordStatusId(record.status), shownSource)
									.arg(record.frame);
			else
				result.lines << Text::tr("entity:%1 · %2 · %3 · frame %4 · %n brush(es)", nullptr, record.brushCount)
									.arg(record.entityId)
									.arg(modelCollisionRecordStatusId(record.status), shownSource)
									.arg(record.frame);
		}
		result.payload.insert("placed", listed);
		result.payload.insert("mapPath", QFileInfo(values.value("--map")).absoluteFilePath());
		result.payload.insert("revision", QString::fromLatin1(modelCollisionFingerprint(document.mesh()).toHex()));
		if (placed)
		{
			const int recordCount = int(records.size());
			result.lines.prepend(Text::tr("%n placed collision record(s) in %1.", nullptr, recordCount).arg(values.value("--map")));
			return result;
		}
		if (groupId >= 0 && refreshed == 0)
			return fail(4, Text::tr("entity:%1 is not a placed collision record in this map.").arg(groupId));
		if (refreshed == 0)
			return fail(4, Text::tr("No stale collision placed from this mesh was found. Use --group entity:N to refresh one record."));
		result.payload.insert("refreshed", refreshed);
		const auto serialized = serializeLevelMap(working);
		if (!serialized.succeeded())
			return fail(4, serialized.errors.join('\n'));
		bytes = serialized.bytes;
	}
	result.payload.insert("collisionBoxes", modelCollisionJson(document.mesh()));
	result.lines.prepend(Text::tr("%1 collision box(es).").arg(document.mesh().collisionBoxes.size()));
	for (const auto &box : document.mesh().collisionBoxes)
	{
		ModelCollisionBox pose;
		if (!sampleModelCollisionBox(box, std::max(0, frame), std::max(0, frame), 0, &pose))
			return fail(4, Text::tr("The requested collision pose is unavailable."));
		result.lines << Text::tr("%1: centre (%2), size (%3), rotation (%4) degrees.")
							.arg(box.name, vectorText(pose.centre), vectorText(pose.size), vectorText(pose.rotation));
		if (!box.framePoses.isEmpty())
			result.lines << Text::tr("Animated: %1 stored poses; displayed frame %2.").arg(box.framePoses.size()).arg(std::max(0, frame));
	}
	if (operation == "inspect" && frame >= 0)
	{
		ModelMesh sampled;
		for (const auto &box : document.mesh().collisionBoxes)
		{
			ModelCollisionBox pose;
			sampleModelCollisionBox(box, frame, frame, 0, &pose);
			sampled.collisionBoxes << pose;
		}
		result.payload.insert("frame", frame);
		result.payload.insert("sampledBoxes", modelCollisionJson(sampled));
	}
	if (operation == "inspect")
	{
		return result;
	}
	if (bytes.isEmpty() || !error.isEmpty())
	{
		return fail(4, error);
	}
	const auto target = inspectModelWriteTarget(output);
	if (!target.isValid())
	{
		return fail(4, target.error);
	}
	const bool current = edit && !document.path().isEmpty() && modelPathsReferToSameFile(output, document.path());
	if ((!current && modelPathsReferToSameFile(output, source)) ||
		((place || refresh) && modelPathsReferToSameFile(output, values.value("--map"))) ||
		(target.existed && !overwrite && !current))
	{
		return fail(4, Text::tr("Protect source inputs. Use a separate output; existing derivatives require --overwrite."));
	}
	if (current && target.sha256 != document.sourceFingerprint())
	{
		return fail(4, Text::tr("The source changed on disk. Reopen it before saving."));
	}
	if (!(edit ? document.save(output, overwrite, &error, {}, dry)
			   : dry ? validateModelWriteTarget(target, &error) : writeModelFile(target, bytes, &error)))
	{
		return fail(4, error);
	}
	result.payload.insert("outputPath", QFileInfo(output).absoluteFilePath());
	result.payload.insert("written", !dry);
	result.lines << (dry ? Text::tr("Validated without writing: %1") : Text::tr("Wrote: %1")).arg(output);
	return result;
}
} // namespace vibestudio::cli
