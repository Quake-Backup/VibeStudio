// map convert-patches and map deform-patch: TrenchBroom's Convert Selection
// to Patches and NetRadiant Custom's Deform, through the same core services
// as the Levels page. Workflow reference: TB MapWindow::
// convertSelectionToPatches at 4c756be1 (GPL-3.0-or-later) and NRC
// radiant/patchmanip.cpp Patch_deform at 449778b3 (GPL-2.0-or-later).
// Original implementation; see docs/CREDITS.md.

#include "cli/level_patch_convert_deform.h"

#include "core/level_material_paint.h"
#include "core/level_materials.h"
#include "core/level_patch_convert.h"
#include "core/level_patch_tools.h"
#include "core/level_scene.h"
#include "core/package_archive.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QSet>

#include <algorithm>
#include <memory>

namespace vibestudio::cli {
namespace {
struct Text {
	Q_DECLARE_TR_FUNCTIONS(LevelPatchToolsCli)
};

struct Parsed {
	QSet<QString> seen;
	QHash<QString, QStringList> values;
	QStringList positional;
};

// Options and flags of one command; false, with `error` set, when they do
// not parse.
bool parseArguments(const QStringList& arguments, const QSet<QString>& flags, const QSet<QString>& options, const QSet<QString>& repeated,
	const QString& command, Parsed* parsed, QString* error)
{
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const QString argument = arguments[i];
		if (!argument.startsWith(QLatin1Char('-'))) {
			parsed->positional << argument;
			continue;
		}
		const auto equals = argument.indexOf(QLatin1Char('='));
		const QString name = equals < 0 ? argument : argument.left(equals);
		if (parsed->seen.contains(name) && !repeated.contains(name)) {
			*error = Text::tr("Repeated option: %1.").arg(name);
			return false;
		}
		parsed->seen.insert(name);
		if (flags.contains(name)) {
			if (equals >= 0) {
				*error = Text::tr("Flag %1 does not take a value.").arg(name);
				return false;
			}
			continue;
		}
		if (!options.contains(name)) {
			*error = Text::tr("Unknown option for %1: %2").arg(command, name);
			return false;
		}
		QString value;
		if (equals >= 0) {
			value = argument.mid(equals + 1);
		} else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith(QStringLiteral("--"))) {
			value = arguments[++i];
		}
		if (value.trimmed().isEmpty()) {
			*error = Text::tr("Missing value for %1.").arg(name);
			return false;
		}
		parsed->values[name] << value;
	}
	return true;
}

// The map path, positionally or with --input.
QString mapPath(const Parsed& parsed, QString* error, const QString& command)
{
	const bool input = parsed.seen.contains(QStringLiteral("--input"));
	if (parsed.positional.size() != (input ? 2 : 3)) {
		*error = Text::tr("%1 needs one input map, positionally or with --input, and --output.").arg(command);
		return {};
	}
	return input ? parsed.values.value(QStringLiteral("--input")).first() : parsed.positional[2];
}

bool loadMap(const Parsed& parsed, const QString& path, LevelMapDocument* document, int* code, QString* error)
{
	if (!loadLevelMap({QFileInfo(path).absoluteFilePath(), parsed.values.value(QStringLiteral("--map-name")).value(0),
						  parsed.values.value(QStringLiteral("--engine")).value(0)},
			document, error)) {
		*code = QFileInfo::exists(path) ? 1 : 3;
		return false;
	}
	if (parsed.seen.contains(QStringLiteral("--context"))
		&& !openLevelSceneGroup(document, parsed.values.value(QStringLiteral("--context")).first(), error)) {
		*code = 4;
		return false;
	}
	return true;
}

bool objectSelection(const QStringList& values, QVector<LevelMapSelectionRef>* selection, QString* error)
{
	for (const QString& value : values) {
		for (const QString& selector : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
			const QStringList parts = selector.trimmed().split(QLatin1Char(':'));
			bool valid = false;
			const int id = parts.value(1).toInt(&valid);
			const LevelMapSelectionKind kind =
				parts.size() == 2 ? levelMapSelectionKindFromId(parts.first().trimmed().toLower()) : LevelMapSelectionKind::None;
			if (kind == LevelMapSelectionKind::None || !valid || id < 0) {
				*error = Text::tr("Unknown map object selector: %1").arg(selector.trimmed());
				return false;
			}
			selection->removeAll(LevelMapSelectionRef {kind, id});
			*selection << LevelMapSelectionRef {kind, id};
		}
	}
	return true;
}

void addSaveFields(const LevelMapSaveReport& saved, QJsonObject* payload)
{
	payload->insert(QStringLiteral("output"), saved.outputPath);
	payload->insert(QStringLiteral("written"), saved.written);
	payload->insert(QStringLiteral("dryRun"), saved.dryRun);
	payload->insert(QStringLiteral("warnings"), QJsonArray::fromStringList(saved.warnings));
}
} // namespace

LevelPatchToolCliResult runLevelPatchConvert(const QStringList& arguments)
{
	LevelPatchToolCliResult result;
	const auto fail = [&result](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QString command = QStringLiteral("map convert-patches");
	Parsed parsed;
	QString error;
	if (!parseArguments(arguments, {"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run", "--keep-brushes"},
			{"--input", "--object", "--face", "--columns", "--rows", "--texture-size", "--package", "--palette", "--context", "--output", "--engine",
				"--map-name", "--settings-file", "--locale", "--catalog-root"},
			{"--object", "--face"}, command, &parsed, &error)) {
		return fail(2, error);
	}
	const QString path = mapPath(parsed, &error, command);
	if (path.isEmpty()) {
		return fail(2, error);
	}
	const QString output = parsed.values.value(QStringLiteral("--output")).value(0);
	if (output.isEmpty()) {
		return fail(2, Text::tr("map convert-patches requires --output <save-as path>."));
	}
	LevelPatchConvertRequest request;
	for (const auto& [option, target] : {std::pair {QStringLiteral("--columns"), &request.columns}, std::pair {QStringLiteral("--rows"), &request.rows}}) {
		if (parsed.seen.contains(option)) {
			bool ok = false;
			*target = parsed.values.value(option).first().toInt(&ok);
			if (!ok || *target < 3 || *target > 31 || *target % 2 == 0) {
				return fail(2, Text::tr("%1 takes an odd number from 3 to 31.").arg(option));
			}
		}
	}
	request.keepBrushes = parsed.seen.contains(QStringLiteral("--keep-brushes"));
	QSize explicitSize;
	if (parsed.seen.contains(QStringLiteral("--texture-size"))) {
		const QStringList parts = parsed.values.value(QStringLiteral("--texture-size")).first().split(QLatin1Char(','));
		bool widthOk = false, heightOk = false;
		const int width = parts.value(0).trimmed().toInt(&widthOk), height = parts.value(1).trimmed().toInt(&heightOk);
		if (parts.size() != 2 || !widthOk || !heightOk || width < 1 || height < 1 || width > 65536 || height > 65536) {
			return fail(2, Text::tr("--texture-size takes the material image's width,height in texels, 1 to 65536."));
		}
		explicitSize = QSize(width, height);
	}
	if (explicitSize.isValid() && parsed.seen.contains(QStringLiteral("--package"))) {
		return fail(2, Text::tr("Give --texture-size or --package, not both."));
	}
	QVector<LevelMapSelectionRef> objects;
	if (!objectSelection(parsed.values.value(QStringLiteral("--object")), &objects, &error)) {
		return fail(2, error);
	}
	for (const LevelMapSelectionRef& object : std::as_const(objects)) {
		if (object.kind != LevelMapSelectionKind::QuakeBrush && object.kind != LevelMapSelectionKind::Entity) {
			return fail(2, Text::tr("--object takes brush:ID, or entity:ID for an entity's brushes."));
		}
	}
	QVector<LevelSurfaceFace> faces;
	for (const QString& selector : parsed.values.value(QStringLiteral("--face"))) {
		LevelMaterialTarget face;
		if (!parseLevelMaterialTarget(selector, &face, &error) || face.kind != LevelMaterialKind::BrushFace) {
			return fail(2, Text::tr("--face takes face:brushId:faceNumber, with a one-based face number."));
		}
		faces << LevelSurfaceFace {face.objectId, face.faceIndex};
	}
	if (objects.isEmpty() && faces.isEmpty()) {
		return fail(2, Text::tr("Name brushes with --object brush:ID or entity:ID, or faces with --face face:brushId:faceNumber."));
	}
	LevelMapDocument document;
	int code = 0;
	if (!loadMap(parsed, path, &document, &code, &error)) {
		return fail(code, error);
	}
	QVector<int> whole;
	for (const LevelMapSelectionRef& object : std::as_const(objects)) {
		for (const LevelMapBrush& brush : std::as_const(document.brushes)) {
			if ((object.kind == LevelMapSelectionKind::QuakeBrush && brush.id == object.objectId)
				|| (object.kind == LevelMapSelectionKind::Entity && brush.entityId == object.objectId)) {
				if (!whole.contains(brush.id)) {
					whole << brush.id;
				}
			}
		}
	}
	for (const LevelMapSelectionRef& object : std::as_const(objects)) {
		const bool exists = object.kind == LevelMapSelectionKind::QuakeBrush
			? std::any_of(document.brushes.cbegin(), document.brushes.cend(), [&](const LevelMapBrush& brush) { return brush.id == object.objectId; })
			: std::any_of(document.entities.cbegin(), document.entities.cend(), [&](const LevelMapEntity& entity) { return entity.id == object.objectId; });
		if (!exists) {
			return fail(4, Text::tr("%1 does not exist.").arg(levelMapSelectionRefId(object)));
		}
	}
	for (const LevelSurfaceFace& face : std::as_const(faces)) {
		const auto brush =
			std::find_if(document.brushes.cbegin(), document.brushes.cend(), [&](const LevelMapBrush& each) { return each.id == face.brushId; });
		if (brush == document.brushes.cend() || face.faceIndex < 0 || face.faceIndex >= brush->faces.size()) {
			return fail(4, Text::tr("Brush %1 has no face %2.").arg(face.brushId).arg(face.faceIndex + 1));
		}
	}
	// Image sizes: one for every material, or each from the package.
	QHash<QString, QSize> packageSizes;
	if (parsed.seen.contains(QStringLiteral("--package"))) {
		PackageArchive archive;
		if (!archive.load(parsed.values.value(QStringLiteral("--package")).first(), &error)) {
			return fail(3, error.isEmpty() ? Text::tr("Unable to open the package.") : error);
		}
		LevelMapDocument assets = document;
		assets.entities.clear();
		assets.patches.clear();
		QSet<int> involved(whole.cbegin(), whole.cend());
		for (const LevelSurfaceFace& face : std::as_const(faces)) {
			involved.insert(face.brushId);
		}
		assets.brushes.removeIf([&](const LevelMapBrush& brush) { return !involved.contains(brush.id); });
		LevelPreviewAssetOptions options;
		options.paletteId = parsed.values.value(QStringLiteral("--palette")).value(0);
		options.modelLimit = 0;
		packageSizes = levelPreviewTextureSizes(resolveLevelPreviewAssets(assets, archive, options));
	}
	request.imageSize = [explicitSize, packageSizes](const QString& material) {
		return explicitSize.isValid() ? explicitSize : packageSizes.value(material.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/')).toCaseFolded());
	};
	LevelPatchConvertReport report;
	if (!convertLevelMapFacesToPatches(&document, faces, whole, request, &report, &error)) {
		return fail(4, error);
	}
	const LevelMapSaveReport saved =
		saveLevelMapAs(document, output, parsed.seen.contains(QStringLiteral("--dry-run")), parsed.seen.contains(QStringLiteral("--overwrite")));
	if (!saved.succeeded()) {
		return fail(4, saved.errors.join(QLatin1Char('\n')));
	}
	QJsonArray ids;
	for (const int id : std::as_const(report.patchIds)) {
		ids << id;
	}
	result.payload = QJsonObject {{QStringLiteral("patches"), ids}, {QStringLiteral("faces"), report.faces},
		{QStringLiteral("brushesRemoved"), report.brushesRemoved}, {QStringLiteral("columns"), request.columns}, {QStringLiteral("rows"), request.rows},
		{QStringLiteral("skipped"), QJsonArray::fromStringList(report.skipped)},
		{QStringLiteral("unsizedMaterials"), QJsonArray::fromStringList(report.unsizedMaterials)}};
	addSaveFields(saved, &result.payload);
	const int made = static_cast<int>(report.patchIds.size());
	result.lines << Text::tr("Converted %n face(s)", nullptr, report.faces) + QLatin1Char(' ')
			+ Text::tr("to %n patch(es).", nullptr, made);
	if (report.brushesRemoved > 0) {
		result.lines << Text::tr("Removed %n converted brush(es).", nullptr, report.brushesRemoved);
	}
	if (!report.unsizedMaterials.isEmpty()) {
		result.lines << Text::tr("No image size for %1; mapped as 64 by 64 texels. Use --texture-size W,H or --package.")
							.arg(report.unsizedMaterials.join(QStringLiteral(", ")));
	}
	for (const QString& reason : std::as_const(report.skipped)) {
		result.lines << Text::tr("Left as it was: %1").arg(reason);
	}
	result.lines << levelMapSaveReportText(saved);
	return result;
}

LevelPatchToolCliResult runLevelPatchDeform(const QStringList& arguments)
{
	LevelPatchToolCliResult result;
	const auto fail = [&result](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QString command = QStringLiteral("map deform-patch");
	Parsed parsed;
	QString error;
	if (!parseArguments(arguments, {"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run"},
			{"--input", "--patch", "--object", "--max", "--axis", "--seed", "--context", "--output", "--engine", "--map-name", "--settings-file",
				"--locale", "--catalog-root"},
			{"--patch", "--object"}, command, &parsed, &error)) {
		return fail(2, error);
	}
	const QString path = mapPath(parsed, &error, command);
	if (path.isEmpty()) {
		return fail(2, error);
	}
	const QString output = parsed.values.value(QStringLiteral("--output")).value(0);
	if (output.isEmpty()) {
		return fail(2, Text::tr("map deform-patch requires --output <save-as path>."));
	}
	LevelPatchDeformRequest request;
	if (parsed.seen.contains(QStringLiteral("--max"))) {
		bool ok = false;
		request.maximum = parsed.values.value(QStringLiteral("--max")).first().toInt(&ok);
		if (!ok || request.maximum < -9999 || request.maximum > 9999) {
			return fail(2, Text::tr("--max takes a whole number of units from -9999 to 9999."));
		}
	}
	if (parsed.seen.contains(QStringLiteral("--axis"))) {
		request.axis = static_cast<int>(
			QStringList {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")}.indexOf(parsed.values.value(QStringLiteral("--axis")).first().trimmed().toLower()));
		if (request.axis < 0) {
			return fail(2, Text::tr("--axis takes x, y or z."));
		}
	}
	// Without --seed each run differs, as upstream's does; the seed it drew is
	// reported, so the run can be repeated.
	request.seed = QRandomGenerator::global()->generate();
	if (parsed.seen.contains(QStringLiteral("--seed"))) {
		bool ok = false;
		request.seed = parsed.values.value(QStringLiteral("--seed")).first().toUInt(&ok);
		if (!ok) {
			return fail(2, Text::tr("--seed takes a whole number from 0 to 4294967295."));
		}
	}
	QVector<int> ids;
	for (const QString& value : parsed.values.value(QStringLiteral("--patch"))) {
		for (const QString& part : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
			bool ok = false;
			const int id = part.trimmed().toInt(&ok);
			if (!ok || id < 0) {
				return fail(2, Text::tr("--patch takes a non-negative patch ID."));
			}
			if (ids.contains(id)) {
				return fail(2, Text::tr("Patch %1 is named more than once.").arg(id));
			}
			ids << id;
		}
	}
	QVector<LevelMapSelectionRef> objects;
	if (!objectSelection(parsed.values.value(QStringLiteral("--object")), &objects, &error)) {
		return fail(2, error);
	}
	for (const LevelMapSelectionRef& object : std::as_const(objects)) {
		if (object.kind != LevelMapSelectionKind::QuakePatch && object.kind != LevelMapSelectionKind::Entity) {
			return fail(2, Text::tr("--object takes patch:ID, or entity:ID for an entity's patches."));
		}
	}
	if (ids.isEmpty() && objects.isEmpty()) {
		return fail(2, Text::tr("Name patches with --patch ID or --object patch:ID or entity:ID."));
	}
	LevelMapDocument document;
	int code = 0;
	if (!loadMap(parsed, path, &document, &code, &error)) {
		return fail(code, error);
	}
	for (const LevelMapSelectionRef& object : std::as_const(objects)) {
		for (const LevelMapPatch& patch : std::as_const(document.patches)) {
			if ((object.kind == LevelMapSelectionKind::QuakePatch && patch.id == object.objectId)
				|| (object.kind == LevelMapSelectionKind::Entity && patch.entityId == object.objectId)) {
				if (!ids.contains(patch.id)) {
					ids << patch.id;
				}
			}
		}
	}
	for (const int id : std::as_const(ids)) {
		if (std::none_of(document.patches.cbegin(), document.patches.cend(), [id](const LevelMapPatch& patch) { return patch.id == id; })) {
			return fail(4, Text::tr("Patch %1 does not exist.").arg(id));
		}
	}
	if (ids.isEmpty()) {
		return fail(4, Text::tr("The named entities have no patches."));
	}
	int changed = 0;
	int skipped = 0;
	if (!deformLevelMapPatches(&document, ids, request, &changed, &skipped, &error)) {
		return fail(4, error);
	}
	const LevelMapSaveReport saved =
		saveLevelMapAs(document, output, parsed.seen.contains(QStringLiteral("--dry-run")), parsed.seen.contains(QStringLiteral("--overwrite")));
	if (!saved.succeeded()) {
		return fail(4, saved.errors.join(QLatin1Char('\n')));
	}
	static const char* const axes[] = {"x", "y", "z"};
	result.payload = QJsonObject {{QStringLiteral("seed"), static_cast<qint64>(request.seed)}, {QStringLiteral("max"), request.maximum},
		{QStringLiteral("axis"), QString::fromLatin1(axes[request.axis])}, {QStringLiteral("changed"), changed}, {QStringLiteral("skipped"), skipped}};
	addSaveFields(saved, &result.payload);
	result.lines << Text::tr("Deformed %n patch(es)", nullptr, changed) + QLatin1Char(' ')
			+ Text::tr("by up to %1 units along %2 with seed %3.").arg(request.maximum).arg(QString::fromLatin1(axes[request.axis]).toUpper()).arg(request.seed);
	if (skipped > 0) {
		result.lines << Text::tr("Left %n hidden or out-of-group patch(es) as they were.", nullptr, skipped);
	}
	result.lines << levelMapSaveReportText(saved);
	return result;
}
} // namespace vibestudio::cli
