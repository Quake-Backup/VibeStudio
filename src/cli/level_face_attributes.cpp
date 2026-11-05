// map face-attributes: brush faces' texture offset, scale and rotation, read
// or set through the same core service as the Face section of the Levels
// Surfaces tab, including the pivot its turn and stretch handles keep in
// place. Workflow reference: TrenchBroom's Face inspector (FaceAttribsEditor,
// UVRotateTool and UVScaleTool at 4c756be1, GPL-3.0-or-later). Original
// implementation; see docs/CREDITS.md.

#include "cli/level_face_attributes.h"

#include "core/level_face_attributes.h"
#include "core/level_materials.h"
#include "core/level_scene.h"
#include "core/package_archive.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QLocale>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace vibestudio::cli {
namespace {
struct Text {
	Q_DECLARE_TR_FUNCTIONS(LevelFaceAttributesCli)
};

bool parseNumbers(const QString& text, int count, double* values)
{
	const QStringList parts = text.split(QLatin1Char(','));
	if (parts.size() != count) {
		return false;
	}
	for (int i = 0; i < count; ++i) {
		bool ok = false;
		values[i] = QLocale::c().toDouble(parts[i].trimmed(), &ok);
		if (!ok || !std::isfinite(values[i])) {
			return false;
		}
	}
	return true;
}

QString formatId(const LevelMapBrushFace& face)
{
	return face.explicitTextureMatrix ? QStringLiteral("brush-primitive") : face.explicitTextureAxes ? QStringLiteral("valve220") : QStringLiteral("classic");
}

QString materialKey(QString name)
{
	return name.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/')).toCaseFolded();
}

QJsonValue valuesJson(const LevelMapBrushFace& face, QSize size, QString* problem)
{
	LevelTextureValues values;
	if (!levelTextureValues(face, size, &values, problem)) {
		return QJsonValue(QJsonValue::Null);
	}
	return QJsonObject{{QStringLiteral("offsetU"), values.shiftU}, {QStringLiteral("offsetV"), values.shiftV}, {QStringLiteral("scaleU"), values.scaleU},
		{QStringLiteral("scaleV"), values.scaleV}, {QStringLiteral("rotation"), values.rotation}};
}
} // namespace

LevelFaceAttributesCliResult runLevelFaceAttributes(const QStringList& arguments)
{
	LevelFaceAttributesCliResult result;
	const auto fail = [&result](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run"};
	const QSet<QString> options{"--input", "--object", "--face", "--offset-u", "--offset-v", "--scale-u", "--scale-v", "--rotation", "--move", "--pivot",
		"--texture-size", "--package", "--palette", "--context", "--output", "--engine", "--map-name", "--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> repeated{"--object", "--face"};
	QSet<QString> seen;
	QHash<QString, QStringList> values;
	QStringList positional;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const QString argument = arguments[i];
		// Negative numbers (--offset-u -8, --move -4,0) are values.
		if (!argument.startsWith(QStringLiteral("--"))) {
			positional << argument;
			continue;
		}
		const auto equals = argument.indexOf('=');
		const QString name = equals < 0 ? argument : argument.left(equals);
		if (seen.contains(name) && !repeated.contains(name)) {
			return fail(2, Text::tr("Repeated option: %1. Only --object and --face may be repeated.").arg(name));
		}
		seen.insert(name);
		if (flags.contains(name)) {
			if (equals >= 0) {
				return fail(2, Text::tr("Flag %1 does not take a value.").arg(name));
			}
			continue;
		}
		if (!options.contains(name)) {
			return fail(2, Text::tr("Unknown option for map face-attributes: %1").arg(name));
		}
		QString value;
		if (equals >= 0) {
			value = argument.mid(equals + 1);
		} else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith(QStringLiteral("--"))) {
			value = arguments[++i];
		}
		if (value.trimmed().isEmpty()) {
			return fail(2, Text::tr("Missing value for %1.").arg(name));
		}
		values[name] << value.trimmed();
		if (values[name].size() > 16384) {
			return fail(2, Text::tr("At most 16,384 values are allowed for %1.").arg(name));
		}
	}
	if (positional.size() != (seen.contains(QStringLiteral("--input")) ? 2 : 3)) {
		return fail(2, Text::tr("map face-attributes needs one input map, positionally or with --input, and --object or --face targets; changes also "
								"need --output."));
	}
	const QString path = seen.contains(QStringLiteral("--input")) ? values.value(QStringLiteral("--input")).first() : positional[2];
	const QStringList valueOptions{QStringLiteral("--offset-u"), QStringLiteral("--offset-v"), QStringLiteral("--scale-u"), QStringLiteral("--scale-v"),
		QStringLiteral("--rotation")};
	const bool settingValues = std::any_of(valueOptions.cbegin(), valueOptions.cend(), [&seen](const QString& option) { return seen.contains(option); });
	const bool changing = settingValues || seen.contains(QStringLiteral("--move"));
	const QString output = values.value(QStringLiteral("--output")).value(0);
	if (changing && output.isEmpty()) {
		return fail(2, Text::tr("Changing texture values requires --output <save-as path>; use --dry-run to check the change."));
	}
	if (!changing && (!output.isEmpty() || seen.contains(QStringLiteral("--dry-run")) || seen.contains(QStringLiteral("--overwrite"))
			|| seen.contains(QStringLiteral("--pivot")))) {
		return fail(2, Text::tr("--output, --dry-run, --overwrite and --pivot go with a change: --offset-u, --offset-v, --scale-u, --scale-v, --rotation "
								"or --move."));
	}
	if (seen.contains(QStringLiteral("--pivot")) && !settingValues) {
		return fail(2, Text::tr("--pivot names the point a new scale or rotation keeps in place; give --scale-u, --scale-v or --rotation with it."));
	}
	LevelFaceAttributesEdit edit;
	{
		struct Field {
			const char* option;
			bool* set;
			double* value;
		} fields[] {{"--offset-u", &edit.setShiftU, &edit.shiftU}, {"--offset-v", &edit.setShiftV, &edit.shiftV}, {"--scale-u", &edit.setScaleU, &edit.scaleU},
			{"--scale-v", &edit.setScaleV, &edit.scaleV}, {"--rotation", &edit.setRotation, &edit.rotation}};
		for (const Field& field : fields) {
			const QString option = QString::fromLatin1(field.option);
			if (!seen.contains(option)) {
				continue;
			}
			if (!parseNumbers(values.value(option).first(), 1, field.value)) {
				return fail(2, Text::tr("%1 takes one number.").arg(option));
			}
			*field.set = true;
		}
		double pair[3] = {};
		if (seen.contains(QStringLiteral("--move"))) {
			if (!parseNumbers(values.value(QStringLiteral("--move")).first(), 2, pair)) {
				return fail(2, Text::tr("--move takes texels to move the texture by as U,V."));
			}
			edit.moveTexels = {pair[0], pair[1]};
		}
		if (seen.contains(QStringLiteral("--pivot"))) {
			if (!parseNumbers(values.value(QStringLiteral("--pivot")).first(), 3, pair)) {
				return fail(2, Text::tr("--pivot takes a map point as X,Y,Z."));
			}
			edit.keepPivot = true;
			edit.pivot = {pair[0], pair[1], pair[2], true};
		}
	}
	QSize explicitSize;
	if (seen.contains(QStringLiteral("--texture-size"))) {
		double size[2] = {};
		if (!parseNumbers(values.value(QStringLiteral("--texture-size")).first(), 2, size) || size[0] < 1 || size[1] < 1 || size[0] > 16384
			|| size[1] > 16384 || std::floor(size[0]) != size[0] || std::floor(size[1]) != size[1]) {
			return fail(2, Text::tr("--texture-size takes whole width,height values from 1 through 16384."));
		}
		explicitSize = QSize(static_cast<int>(size[0]), static_cast<int>(size[1]));
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap({QFileInfo(path).absoluteFilePath(), values.value(QStringLiteral("--map-name")).value(0), values.value(QStringLiteral("--engine")).value(0)},
			&document, &error)) {
		return fail(QFileInfo::exists(path) ? 1 : 3, error);
	}
	if (document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map) {
		return fail(4, Text::tr("Texture values belong to brush faces. Open a Quake, Quake II or Quake III map."));
	}
	if (seen.contains(QStringLiteral("--context")) && !openLevelSceneGroup(&document, values.value(QStringLiteral("--context")).first(), &error)) {
		return fail(4, error);
	}

	// Targets: the faces of --object brushes and entities, then --face picks.
	QVector<LevelSurfaceFace> faces;
	const QStringList objects = values.value(QStringLiteral("--object"));
	if (!objects.isEmpty()) {
		QVector<LevelMapSelectionRef> selection;
		for (const QString& value : objects) {
			for (const QString& selector : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
				const QStringList parts = selector.trimmed().split(QLatin1Char(':'));
				bool valid = false;
				const int id = parts.value(1).toInt(&valid);
				const LevelMapSelectionKind kind = parts.size() == 2 ? levelMapSelectionKindFromId(parts.first().trimmed().toLower()) : LevelMapSelectionKind::None;
				if (!valid || id < 0 || (kind != LevelMapSelectionKind::QuakeBrush && kind != LevelMapSelectionKind::Entity)) {
					return fail(2, Text::tr("--object takes brush:ID or entity:ID; patches keep their own texture coordinates: %1").arg(selector.trimmed()));
				}
				selection.removeAll(LevelMapSelectionRef{kind, id});
				selection << LevelMapSelectionRef{kind, id};
			}
		}
		if (!setLevelMapSelection(&document, selection, &error)) {
			return fail(2, Text::tr("Unable to select map objects: %1").arg(error));
		}
		faces = levelMapSelectedSurfaces(document);
	}
	for (const QString& token : values.value(QStringLiteral("--face"))) {
		const QStringList parts = token.trimmed().split(QLatin1Char(':'));
		bool brushOk = false, faceOk = false;
		const int brush = parts.value(0).toInt(&brushOk), face = parts.value(1).toInt(&faceOk);
		if (parts.size() != 2 || !brushOk || !faceOk || brush < 0 || face < 1) {
			return fail(2, Text::tr("--face takes brushId:faceNumber; face numbers start at one."));
		}
		if (!faces.contains(LevelSurfaceFace{brush, face - 1})) {
			faces.append({brush, face - 1});
		}
	}
	QHash<int, const LevelMapBrush*> brushes;
	for (const LevelMapBrush& brush : document.brushes) {
		brushes.insert(brush.id, &brush);
	}
	for (const LevelSurfaceFace& target : std::as_const(faces)) {
		const LevelMapBrush* brush = brushes.value(target.brushId);
		if (!brush || target.faceIndex < 0 || target.faceIndex >= brush->faces.size()) {
			return fail(4, Text::tr("Brush %1 has no face %2.").arg(target.brushId).arg(target.faceIndex + 1));
		}
	}

	// Image sizes for brush-primitive faces: explicit for every material, or
	// read from the package.
	QHash<QString, QSize> sizes;
	QString sizeSource = QStringLiteral("none");
	if (explicitSize.isValid()) {
		for (const LevelSurfaceFace& target : std::as_const(faces)) {
			sizes.insert(materialKey(brushes.value(target.brushId)->faces[target.faceIndex].textureName), explicitSize);
		}
		sizeSource = QStringLiteral("explicit");
	} else if (seen.contains(QStringLiteral("--package"))) {
		PackageArchive archive;
		if (!archive.load(values.value(QStringLiteral("--package")).first(), &error)) {
			return fail(3, error.isEmpty() ? Text::tr("Unable to open the package.") : error);
		}
		QSet<int> wanted;
		for (const LevelSurfaceFace& target : std::as_const(faces)) {
			wanted.insert(target.brushId);
		}
		LevelMapDocument assets = document;
		assets.entities.clear();
		assets.patches.clear();
		assets.brushes.removeIf([&wanted](const LevelMapBrush& brush) { return !wanted.contains(brush.id); });
		LevelPreviewAssetOptions assetOptions;
		assetOptions.paletteId = values.value(QStringLiteral("--palette")).value(0);
		sizes = levelPreviewTextureSizes(resolveLevelPreviewAssets(assets, archive, assetOptions));
		sizeSource = QStringLiteral("package");
	}

	const auto rows = [&](const LevelMapDocument& map) {
		QHash<int, const LevelMapBrush*> current;
		for (const LevelMapBrush& brush : map.brushes) {
			current.insert(brush.id, &brush);
		}
		QJsonArray list;
		for (const LevelSurfaceFace& target : std::as_const(faces)) {
			if (list.size() >= 4096) {
				break;
			}
			const LevelMapBrushFace& face = current.value(target.brushId)->faces[target.faceIndex];
			QString problem;
			const QJsonValue read = valuesJson(face, sizes.value(materialKey(face.textureName)), &problem);
			QJsonObject row{{QStringLiteral("face"), QStringLiteral("%1:%2").arg(target.brushId).arg(target.faceIndex + 1)},
				{QStringLiteral("texture"), face.textureName}, {QStringLiteral("format"), formatId(face)}, {QStringLiteral("values"), read}};
			if (!problem.isEmpty()) {
				row.insert(QStringLiteral("problem"), problem);
			}
			list.append(row);
		}
		return list;
	};
	result.payload.insert(QStringLiteral("targetFaces"), static_cast<int>(faces.size()));
	result.payload.insert(QStringLiteral("imageSizeSource"), sizeSource);

	if (!changing) {
		// Read only: each face's values, and what the Face section's fields show.
		result.payload.insert(QStringLiteral("mapWritten"), false);
		result.payload.insert(QStringLiteral("faces"), rows(document));
		const LevelFaceAttributesSummary summary = levelFaceAttributesSummary(document, faces, sizes);
		const auto field = [&summary](bool mixed, double value) { return mixed || summary.readable == 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(value); };
		result.payload.insert(QStringLiteral("summary"),
			QJsonObject{{QStringLiteral("faces"), summary.faces}, {QStringLiteral("readable"), summary.readable}, {QStringLiteral("unreadable"), summary.unreadable},
				{QStringLiteral("offsetU"), field(summary.shiftUMixed, summary.values.shiftU)},
				{QStringLiteral("offsetV"), field(summary.shiftVMixed, summary.values.shiftV)},
				{QStringLiteral("scaleU"), field(summary.scaleUMixed, summary.values.scaleU)},
				{QStringLiteral("scaleV"), field(summary.scaleVMixed, summary.values.scaleV)},
				{QStringLiteral("rotation"), field(summary.rotationMixed, summary.values.rotation)},
				{QStringLiteral("material"), summary.materialMixed ? QJsonValue(QJsonValue::Null) : QJsonValue(summary.material)},
				{QStringLiteral("materialMixed"), summary.materialMixed}});
		for (const QJsonValue& row : result.payload.value(QStringLiteral("faces")).toArray()) {
			const QJsonObject object = row.toObject();
			const QJsonObject read = object.value(QStringLiteral("values")).toObject();
			if (read.isEmpty()) {
				result.lines << Text::tr("Face %1 (%2, %3): %4")
									.arg(object.value(QStringLiteral("face")).toString(), object.value(QStringLiteral("texture")).toString(),
										object.value(QStringLiteral("format")).toString(), object.value(QStringLiteral("problem")).toString());
				continue;
			}
			result.lines << Text::tr("Face %1 (%2, %3): offset %4, %5; scale %6, %7; rotation %8")
								.arg(object.value(QStringLiteral("face")).toString(), object.value(QStringLiteral("texture")).toString(),
									object.value(QStringLiteral("format")).toString())
								.arg(read.value(QStringLiteral("offsetU")).toDouble(), 0, 'g', 10)
								.arg(read.value(QStringLiteral("offsetV")).toDouble(), 0, 'g', 10)
								.arg(read.value(QStringLiteral("scaleU")).toDouble(), 0, 'g', 10)
								.arg(read.value(QStringLiteral("scaleV")).toDouble(), 0, 'g', 10)
								.arg(read.value(QStringLiteral("rotation")).toDouble(), 0, 'g', 10);
		}
		if (faces.isEmpty()) {
			result.lines << Text::tr("Name brush faces with --object or --face to read their texture values.");
		}
		return result;
	}

	if (faces.isEmpty()) {
		return fail(2, Text::tr("Name the brush faces to change with --object brush:ID, entity:ID or --face brushId:faceNumber."));
	}
	LevelFaceAttributesReport report;
	if (!setLevelMapFaceAttributes(&document, faces, edit, sizes, &report, &error)) {
		return fail(4, error);
	}
	const LevelMapSaveReport saved = saveLevelMapAs(document, output, seen.contains(QStringLiteral("--dry-run")), seen.contains(QStringLiteral("--overwrite")));
	if (!saved.succeeded()) {
		return fail(4, saved.errors.join(QLatin1Char('\n')));
	}
	const auto optional = [](bool set, double value) { return set ? QJsonValue(value) : QJsonValue(QJsonValue::Null); };
	result.payload.insert(QStringLiteral("changedFaces"), report.changedFaces);
	result.payload.insert(QStringLiteral("brushes"), report.brushes);
	result.payload.insert(QStringLiteral("set"),
		QJsonObject{{QStringLiteral("offsetU"), optional(edit.setShiftU, edit.shiftU)}, {QStringLiteral("offsetV"), optional(edit.setShiftV, edit.shiftV)},
			{QStringLiteral("scaleU"), optional(edit.setScaleU, edit.scaleU)}, {QStringLiteral("scaleV"), optional(edit.setScaleV, edit.scaleV)},
			{QStringLiteral("rotation"), optional(edit.setRotation, edit.rotation)}});
	result.payload.insert(QStringLiteral("move"), QJsonArray{edit.moveTexels.x(), edit.moveTexels.y()});
	result.payload.insert(QStringLiteral("pivot"), edit.keepPivot ? QJsonValue(QJsonArray{edit.pivot.x, edit.pivot.y, edit.pivot.z}) : QJsonValue(QJsonValue::Null));
	result.payload.insert(QStringLiteral("faces"), rows(document));
	result.payload.insert(QStringLiteral("mapWritten"), saved.written);
	result.payload.insert(QStringLiteral("output"), saved.outputPath);
	result.payload.insert(QStringLiteral("written"), saved.written);
	result.payload.insert(QStringLiteral("dryRun"), saved.dryRun);
	result.payload.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(saved.warnings));
	result.lines << (report.changedFaces > 0 ? Text::tr("Set the texture values of %n brush face(s) in one undo step.", nullptr, report.changedFaces)
											 : Text::tr("The faces already have these texture values; nothing changed."));
	result.lines << levelMapSaveReportText(saved);
	return result;
}
} // namespace vibestudio::cli
