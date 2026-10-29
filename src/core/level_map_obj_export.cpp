#include "core/level_map_obj_export.h"

#include "core/level_map_transfer.h"
#include "core/level_materials.h"
#include "core/level_scene_layers.h"
#include "core/level_visibility.h"
#include "core/map_preview_mesh.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

// Workflow references: TrenchBroom ObjExportDialog and
// MapWindow::exportDocumentAsObj at 4c756be1 (GPL-3.0-or-later) and NetRadiant
// Custom contrib/brushexport/export.cpp ExportDataAsWavefront at 449778b3
// (GPL-2.0-or-later). Original Qt implementation; see docs/CREDITS.md.
namespace vibestudio {
namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioLevelMapObjExport)
};

constexpr int kShortMaterialName = 20;
constexpr int kListedNames = 8;

bool fail(QString* error, const QString& message)
{
	if (error) {
		*error = message;
	}
	return false;
}

QString materialKey(const QString& name)
{
	return name.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/')).toCaseFolded();
}

bool worldspawn(const LevelMapEntity& entity)
{
	return entity.className.compare(QLatin1String("worldspawn"), Qt::CaseInsensitive) == 0;
}

// OBJ and MTL names are single tokens.
QString token(const QString& text, const QString& fallback)
{
	static const QRegularExpression space(QStringLiteral("\\s+"));
	QString name = text.trimmed();
	name.replace(space, QStringLiteral("_"));
	return name.isEmpty() ? fallback : name;
}

QByteArray number(double value, int decimals)
{
	if (!std::isfinite(value)) {
		value = 0.0;
	}
	QByteArray text = QByteArray::number(value, 'f', decimals);
	if (text.contains('.')) {
		while (text.endsWith('0')) {
			text.chop(1);
		}
		if (text.endsWith('.')) {
			text.chop(1);
		}
	}
	if (text == "-0") {
		text = "0";
	}
	return text;
}

QColor averageColour(const QImage& image)
{
	if (image.isNull()) {
		return {};
	}
	const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
	qint64 red = 0, green = 0, blue = 0;
	const qint64 count = static_cast<qint64>(rgb.width()) * rgb.height();
	if (count <= 0) {
		return {};
	}
	for (int y = 0; y < rgb.height(); ++y) {
		const auto* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
		for (int x = 0; x < rgb.width(); ++x) {
			red += qRed(line[x]);
			green += qGreen(line[x]);
			blue += qBlue(line[x]);
		}
	}
	return QColor(static_cast<int>(red / count), static_cast<int>(green / count), static_cast<int>(blue / count));
}

QString listed(const QStringList& names)
{
	QStringList shown = names.mid(0, kListedNames);
	if (names.size() > kListedNames) {
		shown << QStringLiteral("…");
	}
	return shown.join(QStringLiteral(", "));
}

struct Polygon {
	int surface = -1;
	QVector<int> corners;
	LevelMapSelectionRef owner;
	int face = -1;
	QString material;
	// The material's place in name order.
	int rank = 0;
};

} // namespace

QStringList levelMapObjDefaultIgnoredMaterials()
{
	return {QStringLiteral("caulk"), QStringLiteral("nodraw"), QStringLiteral("nodrawnonsolid"), QStringLiteral("clip"), QStringLiteral("playerclip"),
		QStringLiteral("monsterclip"), QStringLiteral("weapclip"), QStringLiteral("botclip"), QStringLiteral("fullclip"), QStringLiteral("trigger"),
		QStringLiteral("hint"), QStringLiteral("hintskip"), QStringLiteral("skip"), QStringLiteral("origin"), QStringLiteral("areaportal"),
		QStringLiteral("clusterportal"), QStringLiteral("donotenter"), QStringLiteral("lightgrid"), QStringLiteral("antiportal")};
}

LevelMapObjExportOptions levelMapObjDefaultOptions()
{
	LevelMapObjExportOptions options;
	options.ignoredMaterials = levelMapObjDefaultIgnoredMaterials();
	return options;
}

bool levelMapObjMaterialIgnored(const QString& material, const QStringList& ignored)
{
	const QString name = materialKey(material);
	const QString last = name.mid(name.lastIndexOf(QLatin1Char('/')) + 1);
	for (const auto& entry : ignored) {
		const QString wanted = materialKey(entry);
		if (wanted.isEmpty()) {
			continue;
		}
		if (wanted.contains(QLatin1Char('/')) ? (name == wanted || name.endsWith(QLatin1Char('/') + wanted)) : last == wanted) {
			return true;
		}
	}
	return false;
}

QString levelMapObjGroupingId(LevelMapObjGrouping grouping)
{
	switch (grouping) {
	case LevelMapObjGrouping::Material:
		return QStringLiteral("material");
	case LevelMapObjGrouping::Single:
		return QStringLiteral("single");
	case LevelMapObjGrouping::Object:
		break;
	}
	return QStringLiteral("object");
}

bool levelMapObjGroupingFromId(const QString& id, LevelMapObjGrouping* grouping)
{
	const QString wanted = id.trimmed().toLower();
	for (const auto value : {LevelMapObjGrouping::Object, LevelMapObjGrouping::Material, LevelMapObjGrouping::Single}) {
		if (levelMapObjGroupingId(value) == wanted) {
			if (grouping) {
				*grouping = value;
			}
			return true;
		}
	}
	return false;
}

QHash<QString, LevelMapObjMaterialSource> levelMapObjMaterials(const LevelPreviewAssets& assets)
{
	QHash<QString, LevelMapObjMaterialSource> materials;
	for (const auto& material : assets.materials) {
		LevelMapObjMaterialSource source;
		source.imagePath = material.imagePath;
		if (material.ready()) {
			source.size = material.sourceSize;
			source.colour = averageColour(material.image);
		}
		if (!source.imagePath.isEmpty() || source.size.isValid()) {
			materials.insert(materialKey(material.key), source);
		}
	}
	return materials;
}

QString levelMapObjMaterialLibraryPath(const QString& objPath)
{
	const QFileInfo info(objPath);
	// An OBJ's mtllib record lists library names separated by spaces, so the
	// library's own name has none: "my room.obj" names "my_room.mtl".
	QString name = info.completeBaseName();
	for (QChar& character : name) {
		if (character.isSpace()) {
			character = QLatin1Char('_');
		}
	}
	return info.absoluteDir().filePath(name + QStringLiteral(".mtl"));
}

bool prepareLevelMapObjExport(const LevelMapDocument& document, const LevelMapObjExportOptions& options, LevelMapDocument* exported,
	LevelMapObjExportReport* report, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!exported) {
		return false;
	}
	if (document.format == LevelMapFormat::DoomWad) {
		return fail(error, Text::tr("OBJ export writes the brushes and patches of Quake-family maps; Doom maps cannot be exported as OBJ yet."));
	}
	if (document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map) {
		return fail(error, Text::tr("Open a Quake-family map before exporting it as OBJ."));
	}
	LevelMapObjExportReport local;
	auto& counts = report ? *report : local;
	counts.omittedLayers.clear();
	counts.omittedObjects = 0;
	counts.hiddenObjects = 0;
	QSet<int> selectedBrushes, selectedPatches, selectedEntities;
	QSet<QString> omitted;
	QSet<int> omittedEntities;
	if (options.selectionOnly) {
		if (document.selection.isEmpty()) {
			return fail(error, Text::tr("Select brushes, patches or entities to export them as OBJ."));
		}
		for (const auto& ref : document.selection) {
			if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
				selectedBrushes.insert(ref.objectId);
			} else if (ref.kind == LevelMapSelectionKind::QuakePatch) {
				selectedPatches.insert(ref.objectId);
			} else if (ref.kind == LevelMapSelectionKind::Entity) {
				selectedEntities.insert(ref.objectId);
			}
		}
	} else {
		// TrenchBroom's OBJ export leaves out the layers its map export does.
		LevelMapExportRequest layers;
		layers.omitLayers = options.omitLayers;
		layers.includeOmittedLayers = options.includeOmittedLayers;
		QStringList layerIds;
		if (!levelMapExportOmittedLayers(document, layers, &layerIds, error)) {
			return false;
		}
		if (!layerIds.isEmpty()) {
			const QSet<QString> wanted(layerIds.cbegin(), layerIds.cend());
			const auto objectLayers = levelSceneObjectLayers(document, document.scene);
			for (auto it = objectLayers.cbegin(); it != objectLayers.cend(); ++it) {
				if (!wanted.contains(it.value())) {
					continue;
				}
				omitted.insert(it.key());
				if (levelMapSelectionKindFromId(it.key().section(QLatin1Char(':'), 0, 0)) == LevelMapSelectionKind::Entity) {
					omittedEntities.insert(it.key().section(QLatin1Char(':'), 1).toInt());
				}
			}
			for (const auto& node : document.scene.nodes) {
				if (wanted.contains(node.id)) {
					counts.omittedLayers << node.name;
				}
			}
		}
	}
	const auto hidden = levelMapHiddenObjects(document);
	LevelMapDocument result;
	result.format = document.format;
	result.engineFamily = document.engineFamily;
	result.sourcePath = document.sourcePath;
	result.mapName = document.mapName;
	result.lineEnding = document.lineEnding;
	// Worldspawn keeps its keys (a Quake map's wad list) for material lookups.
	for (const auto& entity : document.entities) {
		if (worldspawn(entity)) {
			result.entities << entity;
			break;
		}
	}
	const auto keep = [&](LevelMapSelectionKind kind, int id, int entityId, bool selected) {
		if (options.selectionOnly) {
			if (!selected && !selectedEntities.contains(entityId)) {
				return false;
			}
		} else if (omitted.contains(levelMapSelectionRefId({kind, id})) || omittedEntities.contains(entityId)) {
			++counts.omittedObjects;
			return false;
		}
		if (hidden.contains(levelMapSelectionRefId({kind, id}))) {
			++counts.hiddenObjects;
			return false;
		}
		return true;
	};
	for (const auto& brush : document.brushes) {
		if (keep(LevelMapSelectionKind::QuakeBrush, brush.id, brush.entityId, selectedBrushes.contains(brush.id))) {
			result.brushes << brush;
		}
	}
	for (const auto& patch : document.patches) {
		if (keep(LevelMapSelectionKind::QuakePatch, patch.id, patch.entityId, selectedPatches.contains(patch.id))) {
			result.patches << patch;
		}
	}
	counts.brushes = static_cast<int>(result.brushes.size());
	counts.patches = static_cast<int>(result.patches.size());
	if (result.brushes.isEmpty() && result.patches.isEmpty()) {
		return fail(error, options.selectionOnly ? Text::tr("The selection holds no visible brushes or patches to export as OBJ.")
												 : Text::tr("The map holds no visible brushes or patches to export as OBJ."));
	}
	*exported = std::move(result);
	return true;
}

bool buildLevelMapObj(const LevelMapDocument& exported, const QString& objPath, const LevelMapObjExportOptions& options, LevelMapObjFiles* files,
	LevelMapObjExportReport* report, QString* error, const std::function<bool()>& isCancelled)
{
	if (error) {
		error->clear();
	}
	if (!files) {
		return false;
	}
	LevelMapObjExportReport local;
	auto& counts = report ? *report : local;
	const auto cancelled = [&] {
		if (isCancelled && isCancelled()) {
			counts.cancelled = true;
			return true;
		}
		return false;
	};
	const auto stopped = [&] { return fail(error, Text::tr("The export was cancelled; nothing was written.")); };

	// Material sizes, with 64 x 64 for classic and Valve 220 faces whose
	// image the packages do not hold, so their UVs keep their proportions.
	QHash<QString, QString> displayNames;
	QSet<QString> texelMaterials;
	for (const auto& brush : exported.brushes) {
		const bool texels = !brush.primitiveKind.startsWith(QLatin1String("brushDef"));
		for (const auto& face : brush.faces) {
			const auto key = materialKey(face.textureName);
			if (!displayNames.contains(key)) {
				displayNames.insert(key, face.textureName.trimmed());
			}
			if (texels) {
				texelMaterials.insert(key);
			}
		}
	}
	for (const auto& patch : exported.patches) {
		const auto key = materialKey(patch.textureName);
		if (!displayNames.contains(key)) {
			displayNames.insert(key, patch.textureName.trimmed());
		}
	}
	LevelMapPreviewMeshOptions meshOptions;
	meshOptions.triangleLimit = std::max(1, options.triangleLimit);
	meshOptions.isCancelled = isCancelled;
	counts.unsizedMaterials.clear();
	counts.unresolvedMaterials.clear();
	for (auto it = displayNames.cbegin(); it != displayNames.cend(); ++it) {
		const auto found = options.materials.constFind(it.key());
		if (found != options.materials.cend() && found->size.isValid() && !found->size.isEmpty()) {
			meshOptions.textureSizes.insert(it.key(), found->size);
		} else {
			meshOptions.textureSizes.insert(it.key(), QSize(64, 64));
			if (texelMaterials.contains(it.key()) && !levelMapObjMaterialIgnored(it.value(), options.ignoredMaterials)) {
				counts.unsizedMaterials << it.value();
			}
		}
	}
	counts.unsizedMaterials.sort(Qt::CaseInsensitive);
	if (cancelled()) {
		return stopped();
	}
	const auto preview = buildLevelMapPreviewMesh(exported, meshOptions);
	if (preview.cancelled || cancelled()) {
		counts.cancelled = true;
		return stopped();
	}
	if (preview.truncated) {
		return fail(error, Text::tr("The export holds more than %1 triangles. Export a selection, or hide part of the map, instead.")
							   .arg(QLocale().toString(meshOptions.triangleLimit)));
	}
	const ModelMesh& mesh = preview.mesh;

	QHash<int, int> brushIndex, patchIndex;
	for (int index = 0; index < exported.brushes.size(); ++index) {
		brushIndex.insert(exported.brushes.at(index).id, index);
	}
	for (int index = 0; index < exported.patches.size(); ++index) {
		patchIndex.insert(exported.patches.at(index).id, index);
	}

	// Each brush face and patch quad went in as a fan from its first corner;
	// consecutive triangles that share that corner and owner are one polygon.
	QVector<Polygon> polygons;
	int flat = 0;
	for (int s = 0; s < mesh.surfaces.size(); ++s) {
		const auto& surface = mesh.surfaces.at(s);
		Polygon current;
		bool open = false;
		for (const auto& triangle : surface.triangles) {
			const auto owner = preview.owners.value(flat);
			const int face = preview.ownerFaces.value(flat, -1);
			++flat;
			if (open && triangle.a == current.corners.first() && triangle.b == current.corners.last() && owner == current.owner && face == current.face) {
				current.corners << triangle.c;
				continue;
			}
			if (open) {
				polygons << current;
			}
			current = Polygon{s, {triangle.a, triangle.b, triangle.c}, owner, face, {}, 0};
			open = true;
		}
		if (open) {
			polygons << current;
		}
		if ((s & 63) == 0 && cancelled()) {
			return stopped();
		}
	}

	// Materials, ignored faces and what is left.
	QSet<int> keptBrushes, keptPatches, ignoredPatches;
	QVector<int> kept;
	kept.reserve(polygons.size());
	counts.ignoredFaces = 0;
	for (int index = 0; index < polygons.size(); ++index) {
		auto& polygon = polygons[index];
		if (polygon.owner.kind == LevelMapSelectionKind::QuakeBrush) {
			const auto& brush = exported.brushes.at(brushIndex.value(polygon.owner.objectId));
			polygon.material = polygon.face >= 0 && polygon.face < brush.faces.size() ? brush.faces.at(polygon.face).textureName.trimmed() : QString();
		} else if (polygon.owner.kind == LevelMapSelectionKind::QuakePatch) {
			polygon.material = exported.patches.at(patchIndex.value(polygon.owner.objectId)).textureName.trimmed();
		} else {
			continue;
		}
		if (levelMapObjMaterialIgnored(polygon.material, options.ignoredMaterials)) {
			if (polygon.owner.kind == LevelMapSelectionKind::QuakeBrush) {
				++counts.ignoredFaces;
			} else if (!ignoredPatches.contains(polygon.owner.objectId)) {
				ignoredPatches.insert(polygon.owner.objectId);
				++counts.ignoredFaces;
			}
			continue;
		}
		(polygon.owner.kind == LevelMapSelectionKind::QuakeBrush ? keptBrushes : keptPatches).insert(polygon.owner.objectId);
		kept << index;
	}
	if (kept.isEmpty()) {
		return fail(error, Text::tr("Every face of the export uses an ignored material, so there is nothing to write. Change the ignored materials."));
	}
	counts.brushes = static_cast<int>(keptBrushes.size());
	counts.patches = static_cast<int>(keptPatches.size());

	// Material names as usemtl and newmtl write them, unique after shortening.
	QHash<QString, QString> materialNames;
	QStringList materialKeys;
	{
		QSet<QString> used;
		QSet<QString> seen;
		for (const int index : std::as_const(kept)) {
			const auto key = materialKey(polygons.at(index).material);
			if (!seen.contains(key)) {
				seen.insert(key);
				materialKeys << key;
			}
		}
		std::sort(materialKeys.begin(), materialKeys.end(), [&](const QString& a, const QString& b) {
			return displayNames.value(a).compare(displayNames.value(b), Qt::CaseInsensitive) < 0
				|| (displayNames.value(a).compare(displayNames.value(b), Qt::CaseInsensitive) == 0 && a < b);
		});
		for (const auto& key : std::as_const(materialKeys)) {
			const QString full = token(displayNames.value(key), QStringLiteral("unnamed"));
			QString name = options.shortMaterialNames ? full.right(kShortMaterialName) : full;
			for (int copy = 2; used.contains(name.toCaseFolded()); ++copy) {
				const QString suffix = QStringLiteral("~%1").arg(copy);
				name = (options.shortMaterialNames ? full.right(kShortMaterialName - static_cast<int>(suffix.size())) : full) + suffix;
			}
			used.insert(name.toCaseFolded());
			materialNames.insert(key, name);
		}
	}
	counts.materials = static_cast<int>(materialKeys.size());
	QHash<QString, int> materialRank;
	for (int rank = 0; rank < materialKeys.size(); ++rank) {
		materialRank.insert(materialKeys.at(rank), rank);
	}
	for (const int index : std::as_const(kept)) {
		polygons[index].rank = materialRank.value(materialKey(polygons.at(index).material));
	}

	// Objects or groups, in document order, by material name, or one.
	struct Group {
		QString name;
		int order = 0;
		QVector<int> polygons;
	};
	QVector<Group> groups;
	QHash<QString, int> groupIndex;
	const QString mapName = token(exported.sourcePath.isEmpty() ? exported.mapName : QFileInfo(exported.sourcePath).completeBaseName(), QStringLiteral("map"));
	for (const int index : std::as_const(kept)) {
		const auto& polygon = polygons.at(index);
		QString key, name;
		int order = 0;
		switch (options.grouping) {
		case LevelMapObjGrouping::Object:
			if (polygon.owner.kind == LevelMapSelectionKind::QuakeBrush) {
				const int at = brushIndex.value(polygon.owner.objectId);
				key = QStringLiteral("b%1").arg(polygon.owner.objectId);
				name = QStringLiteral("entity%1_brush%2").arg(exported.brushes.at(at).entityId).arg(polygon.owner.objectId);
				order = at;
			} else {
				const int at = patchIndex.value(polygon.owner.objectId);
				key = QStringLiteral("p%1").arg(polygon.owner.objectId);
				name = QStringLiteral("entity%1_patch%2").arg(exported.patches.at(at).entityId).arg(polygon.owner.objectId);
				order = static_cast<int>(exported.brushes.size()) + at;
			}
			break;
		case LevelMapObjGrouping::Material:
			key = materialKey(polygon.material);
			name = materialNames.value(key);
			order = polygon.rank;
			break;
		case LevelMapObjGrouping::Single:
			key = QStringLiteral("all");
			name = mapName;
			break;
		}
		auto found = groupIndex.constFind(key);
		if (found == groupIndex.cend()) {
			found = groupIndex.insert(key, static_cast<int>(groups.size()));
			groups.push_back({name, order, {}});
		}
		groups[*found].polygons << index;
	}
	std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) { return a.order < b.order; });

	const auto place = [&](const ModelVec3& point) {
		return options.keepZUp ? std::array<double, 3>{point.x, point.y, point.z} : std::array<double, 3>{point.x, point.z, -static_cast<double>(point.y)};
	};
	QByteArray obj;
	obj.reserve(static_cast<qsizetype>(std::min<qint64>(kLevelMapMaxDocumentBytes, static_cast<qint64>(preview.triangles) * 96 + 4096)));
	obj += "# Wavefront OBJ written by VibeStudio\n";
	obj += "# map " + token(exported.sourcePath.isEmpty() ? exported.mapName : QFileInfo(exported.sourcePath).fileName(), QStringLiteral("map")).toUtf8() + '\n';
	obj += "# brushes " + QByteArray::number(counts.brushes) + ", patches " + QByteArray::number(counts.patches) + (options.keepZUp ? ", Z up\n" : ", Y up\n");
	if (options.materialLibrary) {
		obj += "mtllib " + QFileInfo(levelMapObjMaterialLibraryPath(objPath)).fileName().toUtf8() + '\n';
	}
	qint64 vertexCount = 0, texCoordCount = 0, normalCount = 0;
	int tick = 0;
	counts.polygons = 0;
	counts.triangles = 0;
	for (auto& group : groups) {
		// Faces sorted by material, so each material starts once per group.
		std::stable_sort(group.polygons.begin(), group.polygons.end(), [&](int a, int b) { return polygons.at(a).rank < polygons.at(b).rank; });
		QByteArray vertices, texCoords, normals, faces;
		QHash<QByteArray, qint64> vertexIndex, texCoordIndex, normalIndex;
		QString lastMaterial;
		bool firstMaterial = true;
		for (const int index : std::as_const(group.polygons)) {
			if ((++tick & 1023) == 0 && cancelled()) {
				return stopped();
			}
			const auto& polygon = polygons.at(index);
			const auto& surface = mesh.surfaces.at(polygon.surface);
			const auto& frame = surface.frames.first();
			QVector<int> corners = polygon.corners;
			// Counter-clockwise seen from the front, as OBJ expects, whichever
			// way the solver wound the face (Newell's normal against the face's).
			double nx = 0.0, ny = 0.0, nz = 0.0;
			for (int corner = 0; corner < corners.size(); ++corner) {
				const auto& a = frame.positions.value(corners.at(corner));
				const auto& b = frame.positions.value(corners.at((corner + 1) % corners.size()));
				nx += (static_cast<double>(a.y) - b.y) * (static_cast<double>(a.z) + b.z);
				ny += (static_cast<double>(a.z) - b.z) * (static_cast<double>(a.x) + b.x);
				nz += (static_cast<double>(a.x) - b.x) * (static_cast<double>(a.y) + b.y);
			}
			const auto facing = frame.normals.value(corners.first());
			if (nx * facing.x + ny * facing.y + nz * facing.z < 0.0) {
				std::reverse(corners.begin(), corners.end());
			}
			QVector<QByteArray> references;
			references.reserve(corners.size());
			for (const int corner : std::as_const(corners)) {
				const auto position = place(frame.positions.value(corner));
				const QByteArray v = number(position[0], 4) + ' ' + number(position[1], 4) + ' ' + number(position[2], 4);
				qint64 vi = 0;
				if (options.weld && vertexIndex.contains(v)) {
					vi = vertexIndex.value(v);
				} else {
					vi = ++vertexCount;
					vertices += "v " + v + '\n';
					if (options.weld) {
						vertexIndex.insert(v, vi);
					}
				}
				// OBJ measures V from the bottom of the image, idTech from the top.
				const auto st = surface.texCoords.value(corner);
				const QByteArray vt = number(st.u, 6) + ' ' + number(1.0 - static_cast<double>(st.v), 6);
				qint64 ti = texCoordIndex.value(vt);
				if (ti == 0) {
					ti = ++texCoordCount;
					texCoordIndex.insert(vt, ti);
					texCoords += "vt " + vt + '\n';
				}
				const auto normal = place(frame.normals.value(corner));
				const QByteArray vn = number(normal[0], 6) + ' ' + number(normal[1], 6) + ' ' + number(normal[2], 6);
				qint64 ni = normalIndex.value(vn);
				if (ni == 0) {
					ni = ++normalCount;
					normalIndex.insert(vn, ni);
					normals += "vn " + vn + '\n';
				}
				references << QByteArray::number(vi) + '/' + QByteArray::number(ti) + '/' + QByteArray::number(ni);
			}
			if (options.materialLibrary) {
				const auto& name = materialNames[materialKeys.at(polygon.rank)];
				if (firstMaterial || name != lastMaterial) {
					faces += "usemtl " + name.toUtf8() + '\n';
					lastMaterial = name;
					firstMaterial = false;
				}
			}
			if (options.triangulate) {
				for (int corner = 1; corner + 1 < references.size(); ++corner) {
					faces += "f " + references.at(0) + ' ' + references.at(corner) + ' ' + references.at(corner + 1) + '\n';
				}
			} else {
				faces += 'f';
				for (const auto& reference : std::as_const(references)) {
					faces += ' ' + reference;
				}
				faces += '\n';
			}
			++counts.polygons;
			counts.triangles += static_cast<int>(references.size()) - 2;
		}
		obj += '\n';
		obj += (options.objects ? "o " : "g ") + group.name.toUtf8() + '\n';
		obj += vertices;
		obj += texCoords;
		obj += normals;
		obj += faces;
		if (obj.size() > kLevelMapMaxDocumentBytes) {
			return fail(error, Text::tr("The OBJ would be larger than 512 MiB. Export a selection instead."));
		}
	}
	counts.vertices = static_cast<int>(std::min<qint64>(vertexCount, std::numeric_limits<int>::max()));

	QByteArray mtl;
	if (options.materialLibrary) {
		const QDir objFolder = QFileInfo(objPath).absoluteDir();
		mtl += "# Wavefront material library written by VibeStudio\n";
		mtl += "# materials " + QByteArray::number(materialKeys.size()) + '\n';
		bool rootless = false;
		for (const auto& key : std::as_const(materialKeys)) {
			const auto source = options.materials.value(key);
			mtl += "\nnewmtl " + materialNames.value(key).toUtf8() + '\n';
			if (source.colour.isValid()) {
				mtl += "Kd " + number(source.colour.redF(), 4) + ' ' + number(source.colour.greenF(), 4) + ' ' + number(source.colour.blueF(), 4) + '\n';
			} else {
				mtl += "Kd 0.8 0.8 0.8\n";
			}
			if (source.imagePath.isEmpty()) {
				counts.unresolvedMaterials << displayNames.value(key);
				continue;
			}
			QString path = QDir::fromNativeSeparators(source.imagePath);
			if (options.materialPaths == LevelMapObjMaterialPaths::Export) {
				if (options.materialRoot.trimmed().isEmpty()) {
					rootless = true;
				} else {
					path = QDir::fromNativeSeparators(objFolder.relativeFilePath(QDir(options.materialRoot).filePath(source.imagePath)));
				}
			}
			mtl += "map_Kd " + path.toUtf8() + '\n';
		}
		if (rootless) {
			counts.warnings << Text::tr("No folder holds the packages' images, so the material paths are relative to the game folder instead.");
		}
	}
	if (!counts.unsizedMaterials.isEmpty()) {
		const int unsized = static_cast<int>(counts.unsizedMaterials.size());
		counts.warnings << Text::tr("%n material(s) have no image in the open packages, so their UVs assume 64 × 64 texels: %1", nullptr, unsized)
								.arg(listed(counts.unsizedMaterials));
	}
	if (options.materialLibrary && !counts.unresolvedMaterials.isEmpty()) {
		const int unresolved = static_cast<int>(counts.unresolvedMaterials.size());
		counts.warnings << Text::tr("%n material(s) have no image path, so their MTL entries hold a colour only: %1", nullptr, unresolved)
								.arg(listed(counts.unresolvedMaterials));
	}
	files->obj = std::move(obj);
	files->mtl = std::move(mtl);
	return true;
}

LevelMapObjExportReport exportLevelMapObj(const LevelMapDocument& document, const LevelMapObjExportRequest& request)
{
	LevelMapObjExportReport report;
	const auto& options = request.options;
	report.objPath = QFileInfo(request.save.path).absoluteFilePath();
	report.dryRun = request.save.dryRun;
	const auto cancelled = [&] { return request.save.isCancelled && request.save.isCancelled(); };
	if (request.save.path.trimmed().isEmpty()) {
		report.errors << Text::tr("Choose a .obj file to export to.");
		return report;
	}
	if (QFileInfo(report.objPath).suffix().compare(QLatin1String("obj"), Qt::CaseInsensitive) != 0) {
		report.errors << Text::tr("Name the export with the .obj extension; the material library is written beside it as a .mtl file.");
		return report;
	}
	report.mtlPath = options.materialLibrary ? levelMapObjMaterialLibraryPath(report.objPath) : QString();
	LevelMapDocument exported;
	QString error;
	if (!prepareLevelMapObjExport(document, options, &exported, &report, &error)) {
		report.errors << error;
		return report;
	}
	LevelMapObjFiles files;
	if (!buildLevelMapObj(exported, report.objPath, options, &files, &report, &error, request.save.isCancelled)) {
		report.cancelled = report.cancelled || cancelled();
		report.errors << error;
		return report;
	}
	report.objBytes = files.obj.size();
	report.mtlBytes = files.mtl.size();
	if (report.hiddenObjects > 0) {
		const int hidden = report.hiddenObjects;
		report.warnings << Text::tr("%n hidden brush(es) or patch(es) were left out.", nullptr, hidden);
	}
	// Both destinations are checked before either is written.
	QStringList targets{report.objPath};
	if (!files.mtl.isEmpty()) {
		targets << report.mtlPath;
	}
	for (const auto& path : std::as_const(targets)) {
		// Said here for an OBJ export; the shared writer words it for maps.
		if (const QFileInfo target(path); target.isDir() || target.isSymLink()) {
			report.errors << Text::tr("%1 is a folder or a symbolic link. Choose a file name to export to.").arg(QDir::toNativeSeparators(path));
			return report;
		}
		if (!request.save.overwrite && QFileInfo::exists(path)) {
			report.errors << Text::tr("%1 exists. Choose another name, or allow replacing it.").arg(QDir::toNativeSeparators(path));
			return report;
		}
		auto check = request.save;
		check.path = path;
		check.dryRun = true;
		const auto checked = writeLevelMapBytes(path == report.objPath ? files.obj : files.mtl, check, document.sourcePath);
		if (!checked.succeeded()) {
			report.cancelled = cancelled();
			report.errors += checked.errors;
			return report;
		}
	}
	if (request.save.dryRun) {
		for (const auto& path : std::as_const(targets)) {
			report.warnings << (QFileInfo::exists(path) ? Text::tr("Would replace %1.").arg(QDir::toNativeSeparators(path))
														: Text::tr("Would write %1.").arg(QDir::toNativeSeparators(path)));
		}
		return report;
	}
	if (const QString folder = QFileInfo(report.objPath).absolutePath(); !QDir().mkpath(folder)) {
		report.errors << Text::tr("Could not create the folder %1 for the export.").arg(QDir::toNativeSeparators(folder));
		return report;
	}
	// The library first, so the OBJ never names one that is not there.
	if (!files.mtl.isEmpty()) {
		auto save = request.save;
		save.path = report.mtlPath;
		const auto saved = writeLevelMapBytes(files.mtl, save, document.sourcePath);
		if (!saved.succeeded()) {
			report.cancelled = cancelled();
			report.errors += saved.errors;
			return report;
		}
		report.mtlBackup = saved.backupPath;
	}
	auto save = request.save;
	save.path = report.objPath;
	const auto saved = writeLevelMapBytes(files.obj, save, document.sourcePath);
	if (!saved.succeeded()) {
		report.cancelled = cancelled();
		report.errors += saved.errors;
		if (!files.mtl.isEmpty()) {
			report.errors << Text::tr("The material library %1 was written, but the OBJ was not.").arg(QDir::toNativeSeparators(report.mtlPath));
		}
		return report;
	}
	report.objBackup = saved.backupPath;
	report.written = true;
	return report;
}

QJsonObject levelMapObjExportReportJson(const LevelMapObjExportReport& report)
{
	return {{QStringLiteral("output"), report.objPath},
		{QStringLiteral("materialLibrary"), report.mtlPath},
		{QStringLiteral("written"), report.written},
		{QStringLiteral("dryRun"), report.dryRun},
		{QStringLiteral("backup"), report.objBackup},
		{QStringLiteral("materialLibraryBackup"), report.mtlBackup},
		{QStringLiteral("objBytes"), report.objBytes},
		{QStringLiteral("mtlBytes"), report.mtlBytes},
		{QStringLiteral("brushes"), report.brushes},
		{QStringLiteral("patches"), report.patches},
		{QStringLiteral("polygons"), report.polygons},
		{QStringLiteral("triangles"), report.triangles},
		{QStringLiteral("vertices"), report.vertices},
		{QStringLiteral("materials"), report.materials},
		{QStringLiteral("ignoredFaces"), report.ignoredFaces},
		{QStringLiteral("hiddenObjects"), report.hiddenObjects},
		{QStringLiteral("omittedLayers"), QJsonArray::fromStringList(report.omittedLayers)},
		{QStringLiteral("omittedObjects"), report.omittedObjects},
		{QStringLiteral("unsizedMaterials"), QJsonArray::fromStringList(report.unsizedMaterials)},
		{QStringLiteral("unresolvedMaterials"), QJsonArray::fromStringList(report.unresolvedMaterials)},
		{QStringLiteral("cancelled"), report.cancelled},
		{QStringLiteral("warnings"), QJsonArray::fromStringList(report.warnings)}};
}

} // namespace vibestudio
