// map move --camera and map repeat-transforms, and the Levels Translate,
// Rotate, Scale, Shear and Transform tools' routes of map move, map rotate,
// map resize and map shear.
//
// map move --camera x,y,z centres the --object selection on that point by
// Paste to Camera's translation, snapped to --grid (core/level_paste.h), as
// the Levels page's Move to Camera does: one undo step that keeps the
// objects' identities and source lines. map repeat-transforms copies the
// --object selection and applies --scale, --shear, --rotate and --translate
// to the copies about their centre or --pivot, or applies them to the --set
// components, as NetRadiant Custom's Repeat Transforms does
// (core/level_repeat.h).
//
// Workflow reference: NetRadiant Custom radiant/select.cpp MoveToCamera,
// TranslateToCamera and "RepeatTransforms", and radiant/selection.cpp
// RadiantSelectionSystem::repeatTransforms at 449778b3 (GPL-2.0-or-later).
// Original implementation; see docs/CREDITS.md.

#include "cli/level_move_repeat.h"
#include "core/entity_definitions.h"
#include "core/level_brush.h"
#include "core/level_object_manipulator.h"
#include "core/level_paste.h"
#include "core/level_placement.h"
#include "core/level_repeat.h"
#include "core/level_scene.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <cmath>

namespace vibestudio::cli {
namespace {
struct Text {
	Q_DECLARE_TR_FUNCTIONS(LevelMoveRepeatCli)
};

QJsonObject vectorJson(const LevelMapVec3& value)
{
	return {{QStringLiteral("x"), value.x}, {QStringLiteral("y"), value.y}, {QStringLiteral("z"), value.z}};
}

// Three finite numbers separated by commas.
bool parseTriple(const QString& text, double* a, double* b, double* c)
{
	const QStringList parts = text.split(QLatin1Char(','));
	if (parts.size() != 3) {
		return false;
	}
	double values[3] = {};
	for (int i = 0; i < 3; ++i) {
		bool ok = false;
		values[i] = parts[i].trimmed().toDouble(&ok);
		if (!ok || !std::isfinite(values[i])) {
			return false;
		}
	}
	*a = values[0];
	*b = values[1];
	*c = values[2];
	return true;
}

int axisIndex(const QString& text)
{
	const QString name = text.trimmed().toLower();
	return name == QLatin1String("x") ? 0 : name == QLatin1String("y") ? 1 : name == QLatin1String("z") ? 2 : -1;
}

// The strict option scan both commands share: known flags and options, no
// repeats except `repeated`, and a value for every option.
struct Parsed {
	QSet<QString> seen;
	QHash<QString, QStringList> values;
	QStringList positional;
	QString error;
};

Parsed parseArguments(const QStringList& arguments, const QString& command, const QSet<QString>& flags, const QSet<QString>& options,
	const QSet<QString>& repeated)
{
	Parsed parsed;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const QString argument = arguments[i];
		if (!argument.startsWith('-') || (argument.size() > 1 && (argument[1].isDigit() || argument[1] == QLatin1Char('.')))) {
			parsed.positional << argument;
			continue;
		}
		const auto equals = argument.indexOf('=');
		const QString name = equals < 0 ? argument : argument.left(equals);
		if (parsed.seen.contains(name) && !repeated.contains(name)) {
			parsed.error = Text::tr("Repeated option: %1.").arg(name);
			return parsed;
		}
		parsed.seen.insert(name);
		if (flags.contains(name)) {
			if (equals >= 0) {
				parsed.error = Text::tr("Flag %1 does not take a value.").arg(name);
				return parsed;
			}
			continue;
		}
		if (!options.contains(name)) {
			parsed.error = Text::tr("Unknown option for %1: %2").arg(command, name);
			return parsed;
		}
		QString value;
		if (equals >= 0) {
			value = argument.mid(equals + 1);
		} else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith(QStringLiteral("--"))) {
			value = arguments[++i];
		}
		if (value.trimmed().isEmpty()) {
			parsed.error = Text::tr("Missing value for %1.").arg(name);
			return parsed;
		}
		parsed.values[name] << value;
	}
	return parsed;
}

bool selectObjects(LevelMapDocument* document, const QStringList& objects, QString* error)
{
	QVector<LevelMapSelectionRef> selection;
	for (const QString& value : objects) {
		for (const QString& selector : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
			const QStringList parts = selector.trimmed().split(QLatin1Char(':'));
			bool valid = false;
			const int id = parts.value(1).toInt(&valid);
			const LevelMapSelectionKind kind = parts.size() == 2 ? levelMapSelectionKindFromId(parts.first().trimmed().toLower()) : LevelMapSelectionKind::None;
			if (kind == LevelMapSelectionKind::None || !valid || id < 0) {
				*error = Text::tr("Unknown map object selector: %1").arg(selector.trimmed());
				return false;
			}
			selection.removeAll(LevelMapSelectionRef {kind, id});
			selection << LevelMapSelectionRef {kind, id};
		}
	}
	QString problem;
	if (!setLevelMapSelection(document, selection, &problem)) {
		*error = Text::tr("Unable to select map objects: %1").arg(problem);
		return false;
	}
	return true;
}

bool parseComponents(const QStringList& selections, QVector<LevelMapComponentSet>* sets, QString* error)
{
	static const QRegularExpression pattern(QStringLiteral("^(brush|patch):([0-9]+)/(vertex|edge|face)/([0-9]+(?:,[0-9]+)*)$"));
	if (selections.size() > 256) {
		*error = Text::tr("Repeat --set for at most 256 objects.");
		return false;
	}
	for (const QString& text : selections) {
		const auto match = pattern.match(text);
		bool valid = false;
		const int id = match.captured(2).toInt(&valid);
		if (!match.hasMatch() || !valid) {
			*error = Text::tr("Invalid --set: %1. Use brush:0/face/4,5 or patch:0/vertex/0,1.").arg(text);
			return false;
		}
		LevelMapComponentSet set;
		set.object = {match.captured(1) == QLatin1String("brush") ? LevelMapSelectionKind::QuakeBrush : LevelMapSelectionKind::QuakePatch, id};
		set.kind = match.captured(3) == QLatin1String("vertex") ? LevelBrushComponent::Vertex
			: match.captured(3) == QLatin1String("edge")		  ? LevelBrushComponent::Edge
																  : LevelBrushComponent::Face;
		for (const QString& index : match.captured(4).split(',')) {
			const int component = index.toInt(&valid);
			if (!valid) {
				*error = Text::tr("Component ID is out of range in --set: %1").arg(text);
				return false;
			}
			set.components << component;
		}
		*sets << set;
	}
	return true;
}

bool loadMap(const Parsed& parsed, const QString& path, LevelMapDocument* document, LevelMoveRepeatCliResult* result)
{
	QString error;
	if (!loadLevelMap({QFileInfo(path).absoluteFilePath(), parsed.values.value(QStringLiteral("--map-name")).value(0),
						  parsed.values.value(QStringLiteral("--engine")).value(0)},
			document, &error)) {
		result->exitCode = QFileInfo::exists(path) ? 1 : 3;
		result->error = error;
		return false;
	}
	if (parsed.seen.contains(QStringLiteral("--context"))
		&& !openLevelSceneGroup(document, parsed.values.value(QStringLiteral("--context")).first(), &error)) {
		result->exitCode = 4;
		result->error = error;
		return false;
	}
	return true;
}

void addSave(const LevelMapSaveReport& saved, LevelMoveRepeatCliResult* result)
{
	result->payload.insert(QStringLiteral("output"), saved.outputPath);
	result->payload.insert(QStringLiteral("written"), saved.written);
	result->payload.insert(QStringLiteral("dryRun"), saved.dryRun);
	result->payload.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(saved.warnings));
	result->lines << levelMapSaveReportText(saved);
}

const QSet<QString>& commonOptions()
{
	static const QSet<QString> options {"--input", "--object", "--texture-lock", "--context", "--output", "--engine", "--map-name",
		"--settings-file", "--locale", "--catalog-root"};
	return options;
}

} // namespace

LevelMoveRepeatCliResult runLevelMoveToCamera(const QStringList& arguments)
{
	LevelMoveRepeatCliResult result;
	const auto fail = [&result](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QString command = QStringLiteral("map move");
	QSet<QString> options = commonOptions();
	options += QSet<QString> {"--camera", "--grid", "--definitions"};
	const Parsed parsed = parseArguments(arguments, command,
		{"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run", "--allow-valve220"}, options, {"--object"});
	if (!parsed.error.isEmpty()) {
		return fail(2, parsed.error);
	}
	if (parsed.positional.size() != (parsed.seen.contains(QStringLiteral("--input")) ? 2 : 3)) {
		return fail(2, Text::tr("map move --camera needs one input map, positionally or with --input, plus --object and --output."));
	}
	const QString path = parsed.seen.contains(QStringLiteral("--input")) ? parsed.values.value(QStringLiteral("--input")).first() : parsed.positional[2];
	const QString output = parsed.values.value(QStringLiteral("--output")).value(0);
	if (output.isEmpty()) {
		return fail(2, Text::tr("map move requires --output <save-as path>."));
	}
	LevelPastePlacement placement;
	placement.mode = LevelPasteMode::Camera;
	placement.point.valid = parseTriple(parsed.values.value(QStringLiteral("--camera")).value(0), &placement.point.x, &placement.point.y, &placement.point.z);
	if (!placement.point.valid) {
		return fail(2, Text::tr("--camera takes the camera position as x,y,z."));
	}
	bool gridOk = true;
	placement.grid = parsed.values.value(QStringLiteral("--grid")).value(0, QStringLiteral("0")).toDouble(&gridOk);
	if (!gridOk || !std::isfinite(placement.grid) || placement.grid < 0 || placement.grid > 32768) {
		return fail(2, Text::tr("--grid takes units from 0 (no snapping) to 32768."));
	}
	const QStringList objects = parsed.values.value(QStringLiteral("--object"));
	if (objects.isEmpty()) {
		return fail(2, Text::tr("map move --camera requires --object kind:id, repeatable."));
	}
	const QString textureLock = parsed.values.value(QStringLiteral("--texture-lock")).value(0, QStringLiteral("on"));
	if (textureLock != QLatin1String("on") && textureLock != QLatin1String("off")) {
		return fail(2, Text::tr("--texture-lock requires on or off."));
	}
	const bool allowValve220 = parsed.seen.contains(QStringLiteral("--allow-valve220"));
	if (allowValve220 && textureLock == QLatin1String("off")) {
		return fail(2, Text::tr("--allow-valve220 requires texture lock."));
	}
	if (parsed.seen.contains(QStringLiteral("--definitions"))) {
		const auto definitions = loadEntityDefinitions({parsed.values.value(QStringLiteral("--definitions")).first()});
		if (!definitions.error.isEmpty() || definitions.isEmpty()) {
			return fail(4, Text::tr("Could not load entity definitions: %1").arg(definitions.error));
		}
		for (const auto& definition : definitions.classes) {
			if (definition.kind == EntityClassKind::Point && definition.hasSize) {
				placement.entityBounds.insert(definition.className.toCaseFolded(),
					{{definition.mins[0], definition.mins[1], definition.mins[2]}, {definition.maxs[0], definition.maxs[1], definition.maxs[2]}});
			}
		}
	}

	LevelMapDocument document;
	if (!loadMap(parsed, path, &document, &result)) {
		return result;
	}
	QString error;
	if (!selectObjects(&document, objects, &error)) {
		return fail(2, error);
	}
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::MoveToCamera;
	request.paste = placement;
	request.textures = {textureLock == QLatin1String("on"), allowValve220};
	auto prepared = prepareLevelPlacement(document, request);
	if (!prepared.succeeded) {
		return fail(4, prepared.error);
	}
	document = std::move(prepared.document);
	const LevelMapSaveReport saved = saveLevelMapAs(document, output, parsed.seen.contains(QStringLiteral("--dry-run")), parsed.seen.contains(QStringLiteral("--overwrite")));
	if (!saved.succeeded()) {
		return fail(4, saved.errors.join(QLatin1Char('\n')));
	}
	const LevelMapVec3& delta = prepared.pasteOffset;
	const int moved = static_cast<int>(document.selection.size());
	result.payload = QJsonObject {{QStringLiteral("placement"), QStringLiteral("camera")}, {QStringLiteral("camera"), vectorJson(placement.point)},
		{QStringLiteral("delta"), vectorJson(delta)}, {QStringLiteral("grid"), placement.grid}, {QStringLiteral("objects"), moved},
		{QStringLiteral("textureLockPolicy"), textureLock == QLatin1String("on") ? QStringLiteral("locked") : QStringLiteral("source-parameters")},
		{QStringLiteral("allowValve220"), allowValve220}};
	result.lines << Text::tr("Moved %n object(s) by %1, %2, %3 to centre them on the camera.", nullptr, moved).arg(delta.x).arg(delta.y).arg(delta.z);
	addSave(saved, &result);
	return result;
}

// The Translate tool's move (core/level_object_manipulator.h): the whole
// --object selection by --delta, or so that its pivot (the bounds' centre, or
// --pivot) lands on --to, snapped as the tool snaps it, as one undo step.
// Workflow reference: NetRadiant Custom radiant/selection_mtable_translate.h
// TranslateAxis and aabb_snap_translation at 449778b3 (GPL-2.0-or-later).
LevelMoveRepeatCliResult runLevelObjectTranslate(const QStringList& arguments)
{
	LevelMoveRepeatCliResult result;
	const auto fail = [&result](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QString command = QStringLiteral("map move");
	QSet<QString> options = commonOptions();
	options += QSet<QString> {"--delta", "--to", "--pivot", "--snap", "--grid", "--select"};
	const Parsed parsed = parseArguments(arguments, command,
		{"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run", "--allow-valve220"}, options, {"--object", "--select"});
	if (!parsed.error.isEmpty()) {
		return fail(2, parsed.error);
	}
	if (parsed.positional.size() != (parsed.seen.contains(QStringLiteral("--input")) ? 2 : 3)) {
		return fail(2, Text::tr("map move needs one input map, positionally or with --input, plus --object and --output."));
	}
	const QString path = parsed.seen.contains(QStringLiteral("--input")) ? parsed.values.value(QStringLiteral("--input")).first() : parsed.positional[2];
	const QString output = parsed.values.value(QStringLiteral("--output")).value(0);
	if (output.isEmpty()) {
		return fail(2, Text::tr("map move requires --output <save-as path>."));
	}
	const auto value = [&parsed](const char* name) { return parsed.values.value(QLatin1String(name)).value(0); };
	LevelObjectTranslateRequest request;
	if (parsed.seen.contains(QStringLiteral("--delta")) && parsed.seen.contains(QStringLiteral("--to"))) {
		return fail(2, Text::tr("map move takes --delta x,y,z or --to x,y,z, not both."));
	}
	if (!parsed.seen.contains(QStringLiteral("--delta")) && !parsed.seen.contains(QStringLiteral("--to"))) {
		return fail(2, Text::tr("map move needs --delta x,y,z or --to x,y,z."));
	}
	if (parsed.seen.contains(QStringLiteral("--delta"))
		&& !parseTriple(value("--delta"), &request.delta.x, &request.delta.y, &request.delta.z)) {
		return fail(2, Text::tr("--delta takes x,y,z."));
	}
	if (parsed.seen.contains(QStringLiteral("--to"))) {
		request.destination.valid = parseTriple(value("--to"), &request.destination.x, &request.destination.y, &request.destination.z);
		if (!request.destination.valid) {
			return fail(2, Text::tr("--to takes the point the pivot moves to, as x,y,z."));
		}
	}
	if (parsed.seen.contains(QStringLiteral("--pivot"))) {
		if (!parsed.seen.contains(QStringLiteral("--to"))) {
			return fail(2, Text::tr("--pivot applies only with --to."));
		}
		request.pivot.valid = parseTriple(value("--pivot"), &request.pivot.x, &request.pivot.y, &request.pivot.z);
		if (!request.pivot.valid) {
			return fail(2, Text::tr("--pivot takes x,y,z."));
		}
	}
	const QString snap = value("--snap").isEmpty() ? QStringLiteral("none") : value("--snap").toLower();
	if (snap != QLatin1String("none") && snap != QLatin1String("grid") && snap != QLatin1String("bounds")) {
		return fail(2, Text::tr("--snap takes none, grid or bounds."));
	}
	request.snap = snap == QLatin1String("grid") ? LevelObjectTranslateSnap::Grid
		: snap == QLatin1String("bounds")	  ? LevelObjectTranslateSnap::Bounds
											  : LevelObjectTranslateSnap::None;
	bool gridOk = true;
	request.grid = value("--grid").isEmpty() ? 0.0 : value("--grid").toDouble(&gridOk);
	if (!gridOk || !std::isfinite(request.grid) || request.grid < 0 || request.grid > 32768) {
		return fail(2, Text::tr("--grid takes units from 0 (no snapping) to 32768."));
	}
	if (request.snap != LevelObjectTranslateSnap::None && request.grid <= 0) {
		return fail(2, Text::tr("--snap grid and --snap bounds need --grid N."));
	}
	const QStringList objects = parsed.values.value(QStringLiteral("--object")) + parsed.values.value(QStringLiteral("--select"));
	if (objects.isEmpty()) {
		return fail(2, Text::tr("map move requires --object kind:id, repeatable."));
	}
	const QString textureLock = value("--texture-lock").isEmpty() ? QStringLiteral("on") : value("--texture-lock");
	if (textureLock != QLatin1String("on") && textureLock != QLatin1String("off")) {
		return fail(2, Text::tr("--texture-lock requires on or off."));
	}
	const bool allowValve220 = parsed.seen.contains(QStringLiteral("--allow-valve220"));
	if (allowValve220 && textureLock == QLatin1String("off")) {
		return fail(2, Text::tr("--allow-valve220 requires texture lock."));
	}
	request.textures = {textureLock == QLatin1String("on"), allowValve220};

	LevelMapDocument document;
	if (!loadMap(parsed, path, &document, &result)) {
		return result;
	}
	QString error;
	if (!selectObjects(&document, objects, &error)) {
		return fail(2, error);
	}
	LevelObjectTranslateReport report;
	if (!translateLevelMapObjects(&document, request, &report, &error)) {
		return fail(4, error);
	}
	const LevelMapSaveReport saved = saveLevelMapAs(document, output, parsed.seen.contains(QStringLiteral("--dry-run")), parsed.seen.contains(QStringLiteral("--overwrite")));
	if (!saved.succeeded()) {
		return fail(4, saved.errors.join(QLatin1Char('\n')));
	}
	QJsonArray moved;
	for (const LevelMapSelectionRef& ref : document.selection) {
		moved.append(levelMapSelectionRefId(ref));
	}
	result.payload = QJsonObject {{QStringLiteral("placement"), parsed.seen.contains(QStringLiteral("--to")) ? QStringLiteral("pivot") : QStringLiteral("delta")},
		{QStringLiteral("delta"), vectorJson(report.delta)}, {QStringLiteral("pivotBefore"), vectorJson(report.pivotBefore)},
		{QStringLiteral("pivotAfter"), vectorJson(report.pivotAfter)}, {QStringLiteral("mins"), vectorJson(report.mins)},
		{QStringLiteral("maxs"), vectorJson(report.maxs)}, {QStringLiteral("objects"), report.objects}, {QStringLiteral("selection"), moved},
		{QStringLiteral("snap"), snap}, {QStringLiteral("grid"), request.grid},
		{QStringLiteral("textureLockPolicy"), textureLock == QLatin1String("on") ? QStringLiteral("locked") : QStringLiteral("source-parameters")},
		{QStringLiteral("allowValve220"), allowValve220}};
	if (parsed.seen.contains(QStringLiteral("--context"))) {
		result.payload.insert(QStringLiteral("context"), value("--context"));
	}
	result.lines << Text::tr("Translated %n object(s) by %1, %2, %3; the pivot moved to %4, %5, %6.", nullptr, report.objects)
		.arg(report.delta.x).arg(report.delta.y).arg(report.delta.z).arg(report.pivotAfter.x).arg(report.pivotAfter.y).arg(report.pivotAfter.z);
	addSave(saved, &result);
	return result;
}

// The Rotate tool's turn (core/level_object_manipulator.h): the whole --object
// selection by --degrees about --around x,y,z (or --axis x|y|z) through the
// centre of its bounds or --pivot, rounded to --angle-step, as one undo step.
// A world axis is one plain turn; any other axis is NetRadiant Custom's Euler
// X, Y, Z turn. Workflow reference: NetRadiant Custom
// radiant/selection_mtable_rotate.h RotateAxis and RotateFree at 449778b3
// (GPL-2.0-or-later).
LevelMoveRepeatCliResult runLevelObjectRotate(const QStringList& arguments)
{
	LevelMoveRepeatCliResult result;
	const auto fail = [&result](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QString command = QStringLiteral("map rotate");
	QSet<QString> options = commonOptions();
	options += QSet<QString> {"--around", "--axis", "--degrees", "--pivot", "--angle-step", "--select"};
	const Parsed parsed = parseArguments(arguments, command,
		{"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run", "--allow-valve220"}, options, {"--object", "--select"});
	if (!parsed.error.isEmpty()) {
		return fail(2, parsed.error);
	}
	if (parsed.positional.size() != (parsed.seen.contains(QStringLiteral("--input")) ? 2 : 3)) {
		return fail(2, Text::tr("map rotate needs one input map, positionally or with --input, plus --object, --degrees and --output."));
	}
	const QString path = parsed.seen.contains(QStringLiteral("--input")) ? parsed.values.value(QStringLiteral("--input")).first() : parsed.positional[2];
	const QString output = parsed.values.value(QStringLiteral("--output")).value(0);
	if (output.isEmpty()) {
		return fail(2, Text::tr("map rotate requires --output <save-as path>."));
	}
	const auto value = [&parsed](const char* name) { return parsed.values.value(QLatin1String(name)).value(0); };
	LevelObjectRotateRequest request;
	if (parsed.seen.contains(QStringLiteral("--around")) && parsed.seen.contains(QStringLiteral("--axis"))) {
		return fail(2, Text::tr("map rotate takes --around x,y,z or --axis x|y|z, not both."));
	}
	if (parsed.seen.contains(QStringLiteral("--around"))) {
		if (!parseTriple(value("--around"), &request.axis.x, &request.axis.y, &request.axis.z)) {
			return fail(2, Text::tr("--around takes the direction to turn about, as x,y,z."));
		}
	} else if (parsed.seen.contains(QStringLiteral("--axis"))) {
		const int axis = axisIndex(value("--axis"));
		if (axis < 0) {
			return fail(2, Text::tr("--axis takes x, y or z."));
		}
		request.axis = {axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0, true};
	}
	bool degreesOk = false;
	request.degrees = value("--degrees").toDouble(&degreesOk);
	if (!degreesOk || !std::isfinite(request.degrees)) {
		return fail(2, Text::tr("map rotate needs --degrees N with --around, --angle-step or --context."));
	}
	if (parsed.seen.contains(QStringLiteral("--pivot"))) {
		request.pivot.valid = parseTriple(value("--pivot"), &request.pivot.x, &request.pivot.y, &request.pivot.z);
		if (!request.pivot.valid) {
			return fail(2, Text::tr("--pivot takes x,y,z."));
		}
	}
	bool stepOk = true;
	request.angleStep = value("--angle-step").isEmpty() ? 0.0 : value("--angle-step").toDouble(&stepOk);
	if (!stepOk || !std::isfinite(request.angleStep) || request.angleStep < 0 || request.angleStep > 360) {
		return fail(2, Text::tr("--angle-step takes degrees from 0 (no snapping) to 360."));
	}
	const QStringList objects = parsed.values.value(QStringLiteral("--object")) + parsed.values.value(QStringLiteral("--select"));
	if (objects.isEmpty()) {
		return fail(2, Text::tr("map rotate requires --object kind:id, repeatable."));
	}
	const QString textureLock = value("--texture-lock").isEmpty() ? QStringLiteral("on") : value("--texture-lock");
	if (textureLock != QLatin1String("on") && textureLock != QLatin1String("off")) {
		return fail(2, Text::tr("--texture-lock requires on or off."));
	}
	const bool allowValve220 = parsed.seen.contains(QStringLiteral("--allow-valve220"));
	if (allowValve220 && textureLock == QLatin1String("off")) {
		return fail(2, Text::tr("--allow-valve220 requires texture lock."));
	}
	request.textures = {textureLock == QLatin1String("on"), allowValve220};

	LevelMapDocument document;
	if (!loadMap(parsed, path, &document, &result)) {
		return result;
	}
	QString error;
	if (!selectObjects(&document, objects, &error)) {
		return fail(2, error);
	}
	LevelObjectRotateReport report;
	if (!rotateLevelMapObjects(&document, request, &report, &error)) {
		return fail(4, error);
	}
	const LevelMapSaveReport saved = saveLevelMapAs(document, output, parsed.seen.contains(QStringLiteral("--dry-run")), parsed.seen.contains(QStringLiteral("--overwrite")));
	if (!saved.succeeded()) {
		return fail(4, saved.errors.join(QLatin1Char('\n')));
	}
	QJsonArray turned;
	for (const LevelMapSelectionRef& ref : document.selection) {
		turned.append(levelMapSelectionRefId(ref));
	}
	result.payload = QJsonObject {{QStringLiteral("placement"), QStringLiteral("manipulator")}, {QStringLiteral("axis"), vectorJson(report.axis)},
		{QStringLiteral("degrees"), report.degrees}, {QStringLiteral("pivot"), vectorJson(report.pivot)}, {QStringLiteral("mins"), vectorJson(report.mins)},
		{QStringLiteral("maxs"), vectorJson(report.maxs)}, {QStringLiteral("objects"), report.objects}, {QStringLiteral("selection"), turned},
		{QStringLiteral("angleStep"), request.angleStep},
		{QStringLiteral("textureLockPolicy"), textureLock == QLatin1String("on") ? QStringLiteral("locked") : QStringLiteral("source-parameters")},
		{QStringLiteral("allowValve220"), allowValve220}};
	if (report.worldAxis >= 0) {
		result.payload.insert(QStringLiteral("worldAxis"), QString(QLatin1Char("xyz"[report.worldAxis])));
	} else {
		result.payload.insert(QStringLiteral("eulerXyz"), QJsonArray {report.eulerXyz[0], report.eulerXyz[1], report.eulerXyz[2]});
	}
	if (parsed.seen.contains(QStringLiteral("--context"))) {
		result.payload.insert(QStringLiteral("context"), value("--context"));
	}
	result.lines << Text::tr("Rotated %n object(s) by %1° about %2, %3, %4 through %5, %6, %7.", nullptr, report.objects)
		.arg(report.degrees).arg(report.axis.x).arg(report.axis.y).arg(report.axis.z).arg(report.pivot.x).arg(report.pivot.y).arg(report.pivot.z);
	addSave(saved, &result);
	return result;
}

LevelMoveRepeatCliResult runLevelObjectScale(const QStringList& arguments)
{
	LevelMoveRepeatCliResult result;
	const auto fail = [&result](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QString command = QStringLiteral("map resize");
	QSet<QString> options = commonOptions();
	options += QSet<QString> {"--axis", "--drag", "--view", "--factors", "--pivot", "--grid", "--size-grid", "--select"};
	const Parsed parsed = parseArguments(arguments, command,
		{"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run", "--allow-valve220", "--equal"}, options, {"--object", "--select"});
	if (!parsed.error.isEmpty()) {
		return fail(2, parsed.error);
	}
	if (parsed.positional.size() != (parsed.seen.contains(QStringLiteral("--input")) ? 2 : 3)) {
		return fail(2, Text::tr("map resize needs one input map, positionally or with --input, plus --object, --output and a scale."));
	}
	const QString path = parsed.seen.contains(QStringLiteral("--input")) ? parsed.values.value(QStringLiteral("--input")).first() : parsed.positional[2];
	const QString output = parsed.values.value(QStringLiteral("--output")).value(0);
	if (output.isEmpty()) {
		return fail(2, Text::tr("map resize requires --output <save-as path>."));
	}
	const auto value = [&parsed](const char* name) { return parsed.values.value(QLatin1String(name)).value(0); };
	const bool byFactors = parsed.seen.contains(QStringLiteral("--factors"));
	const bool byDrag = parsed.seen.contains(QStringLiteral("--drag"));
	if (byFactors == byDrag) {
		return fail(2, Text::tr("map resize with the Scale tool's options takes --drag N with --axis or --view, or --factors x,y,z."));
	}
	LevelObjectScaleRequest request;
	LevelObjectScaleDrag drag;
	if (byFactors) {
		if (!parseTriple(value("--factors"), &request.factors[0], &request.factors[1], &request.factors[2])) {
			return fail(2, Text::tr("--factors takes x,y,z scale factors."));
		}
		for (const char* option : {"--axis", "--view", "--equal", "--grid", "--size-grid"}) {
			if (parsed.seen.contains(QLatin1String(option))) {
				return fail(2, Text::tr("%1 goes with --drag, not --factors.").arg(QLatin1String(option)));
			}
		}
	} else {
		// NetRadiant Custom's Scale handles: --axis drags that axis' handle,
		// --view the square across a view's two axes.
		if (parsed.seen.contains(QStringLiteral("--axis")) == parsed.seen.contains(QStringLiteral("--view"))) {
			return fail(2, Text::tr("--drag needs --axis x|y|z or --view top|front|side, not both."));
		}
		bool dragOk = false;
		const double distance = value("--drag").toDouble(&dragOk);
		if (!dragOk || !std::isfinite(distance)) {
			return fail(2, Text::tr("--drag takes how far the handle moves, in map units."));
		}
		if (parsed.seen.contains(QStringLiteral("--axis"))) {
			drag.handle = axisIndex(value("--axis"));
			if (drag.handle < 0) {
				return fail(2, Text::tr("--axis takes x, y or z."));
			}
			drag.delta = {drag.handle == 0 ? distance : 0.0, drag.handle == 1 ? distance : 0.0, drag.handle == 2 ? distance : 0.0, true};
		} else {
			const QString view = value("--view").trimmed().toLower();
			drag.viewAxis = view == QLatin1String("top") ? 2 : view == QLatin1String("front") ? 1 : view == QLatin1String("side") ? 0 : -1;
			if (drag.viewAxis < 0) {
				return fail(2, Text::tr("--view takes top, front or side."));
			}
			// The square moves the same distance along both of the view's axes.
			drag.handle = 3;
			drag.delta = {drag.viewAxis == 0 ? 0.0 : distance, drag.viewAxis == 1 ? 0.0 : distance, drag.viewAxis == 2 ? 0.0 : distance, true};
		}
		drag.equal = parsed.seen.contains(QStringLiteral("--equal"));
		for (const auto& [option, target] : {std::pair {"--grid", &drag.grid}, std::pair {"--size-grid", &drag.sizeGrid}}) {
			if (!parsed.seen.contains(QLatin1String(option))) {
				continue;
			}
			bool ok = false;
			*target = value(option).toDouble(&ok);
			if (!ok || !std::isfinite(*target) || *target <= 0 || *target > 32768) {
				return fail(2, Text::tr("%1 takes a size from above 0 to 32768 units.").arg(QLatin1String(option)));
			}
		}
	}
	if (parsed.seen.contains(QStringLiteral("--pivot"))) {
		request.pivot.valid = parseTriple(value("--pivot"), &request.pivot.x, &request.pivot.y, &request.pivot.z);
		if (!request.pivot.valid) {
			return fail(2, Text::tr("--pivot takes x,y,z."));
		}
	}
	const QStringList objects = parsed.values.value(QStringLiteral("--object")) + parsed.values.value(QStringLiteral("--select"));
	if (objects.isEmpty()) {
		return fail(2, Text::tr("map resize requires --object kind:id, repeatable."));
	}
	// As map resize: textures keep their parameters unless --texture-lock on.
	const QString textureLock = value("--texture-lock").isEmpty() ? QStringLiteral("off") : value("--texture-lock");
	if (textureLock != QLatin1String("on") && textureLock != QLatin1String("off")) {
		return fail(2, Text::tr("--texture-lock requires on or off."));
	}
	const bool allowValve220 = parsed.seen.contains(QStringLiteral("--allow-valve220"));
	if (allowValve220 && textureLock == QLatin1String("off")) {
		return fail(2, Text::tr("--allow-valve220 requires texture lock."));
	}
	request.textures = {textureLock == QLatin1String("on"), allowValve220};

	LevelMapDocument document;
	if (!loadMap(parsed, path, &document, &result)) {
		return result;
	}
	QString error;
	if (!selectObjects(&document, objects, &error)) {
		return fail(2, error);
	}
	if (byDrag) {
		// The drag's factors, from the bounds and pivot the tool would see.
		const LevelObjectManipulatorTarget target = levelObjectManipulatorTarget(document);
		if (!target.available) {
			return fail(4, target.reason);
		}
		drag.mins = target.mins;
		drag.maxs = target.maxs;
		drag.pivot = levelObjectManipulatorPivot(target, request.pivot);
		request.factors = levelObjectScaleFactors(drag);
	}
	LevelObjectScaleReport report;
	if (!scaleLevelMapObjects(&document, request, &report, &error)) {
		return fail(4, error);
	}
	const LevelMapSaveReport saved = saveLevelMapAs(document, output, parsed.seen.contains(QStringLiteral("--dry-run")), parsed.seen.contains(QStringLiteral("--overwrite")));
	if (!saved.succeeded()) {
		return fail(4, saved.errors.join(QLatin1Char('\n')));
	}
	QJsonArray scaled;
	for (const LevelMapSelectionRef& ref : document.selection) {
		scaled.append(levelMapSelectionRefId(ref));
	}
	result.payload = QJsonObject {{QStringLiteral("placement"), QStringLiteral("manipulator")},
		{QStringLiteral("factors"), QJsonArray {report.factors[0], report.factors[1], report.factors[2]}}, {QStringLiteral("pivot"), vectorJson(report.pivot)},
		{QStringLiteral("mins"), vectorJson(report.mins)}, {QStringLiteral("maxs"), vectorJson(report.maxs)}, {QStringLiteral("objects"), report.objects},
		{QStringLiteral("selection"), scaled},
		{QStringLiteral("textureLockPolicy"), textureLock == QLatin1String("on") ? QStringLiteral("locked") : QStringLiteral("source-parameters")},
		{QStringLiteral("allowValve220"), allowValve220}};
	if (byDrag) {
		result.payload.insert(QStringLiteral("handle"), drag.handle < 3 ? QString(QLatin1Char("xyz"[drag.handle])) : value("--view").trimmed().toLower());
		result.payload.insert(QStringLiteral("drag"), value("--drag").toDouble());
		result.payload.insert(QStringLiteral("equal"), drag.equal);
		result.payload.insert(QStringLiteral("grid"), drag.grid);
		result.payload.insert(QStringLiteral("sizeGrid"), drag.sizeGrid);
	}
	if (parsed.seen.contains(QStringLiteral("--context"))) {
		result.payload.insert(QStringLiteral("context"), value("--context"));
	}
	result.lines << Text::tr("Scaled %n object(s) by %1, %2, %3 about %4, %5, %6.", nullptr, report.objects)
		.arg(report.factors[0]).arg(report.factors[1]).arg(report.factors[2]).arg(report.pivot.x).arg(report.pivot.y).arg(report.pivot.z);
	addSave(saved, &result);
	return result;
}

LevelMoveRepeatCliResult runLevelObjectShear(const QStringList& arguments)
{
	LevelMoveRepeatCliResult result;
	const auto fail = [&result](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QString command = QStringLiteral("map shear");
	QSet<QString> options = commonOptions();
	options += QSet<QString> {"--side", "--offset", "--pivot", "--select"};
	const Parsed parsed =
		parseArguments(arguments, command, {"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run", "--allow-valve220"}, options, {"--object", "--select"});
	if (!parsed.error.isEmpty()) {
		return fail(2, parsed.error);
	}
	if (parsed.positional.size() != (parsed.seen.contains(QStringLiteral("--input")) ? 2 : 3)) {
		return fail(2, Text::tr("map shear needs one input map, positionally or with --input, plus --object, --side, --offset and --output."));
	}
	const QString path = parsed.seen.contains(QStringLiteral("--input")) ? parsed.values.value(QStringLiteral("--input")).first() : parsed.positional[2];
	const QString output = parsed.values.value(QStringLiteral("--output")).value(0);
	if (output.isEmpty()) {
		return fail(2, Text::tr("map shear requires --output <save-as path>."));
	}
	const auto value = [&parsed](const char* name) { return parsed.values.value(QLatin1String(name)).value(0); };
	// The side that slides: x+ is the box's maximum along X, x- its minimum.
	LevelObjectShearRequest request;
	const QString side = value("--side").trimmed().toLower();
	request.axis = side.size() == 2 ? axisIndex(side.left(1)) : -1;
	request.side = side.size() == 2 && side[1] == QLatin1Char('+') ? 1 : side.size() == 2 && side[1] == QLatin1Char('-') ? -1 : 0;
	if (request.axis < 0 || request.side == 0) {
		return fail(2, Text::tr("--side takes x+, x-, y+, y-, z+ or z-, the side of the selection's box that slides."));
	}
	if (!parseTriple(value("--offset"), &request.offset.x, &request.offset.y, &request.offset.z)) {
		return fail(2, Text::tr("--offset takes dx,dy,dz, how far the side slides."));
	}
	if (parsed.seen.contains(QStringLiteral("--pivot"))) {
		request.pivot.valid = parseTriple(value("--pivot"), &request.pivot.x, &request.pivot.y, &request.pivot.z);
		if (!request.pivot.valid) {
			return fail(2, Text::tr("--pivot takes x,y,z."));
		}
	}
	const QStringList objects = parsed.values.value(QStringLiteral("--object")) + parsed.values.value(QStringLiteral("--select"));
	if (objects.isEmpty()) {
		return fail(2, Text::tr("map shear requires --object kind:id, repeatable."));
	}
	// As map shear's other form: texture lock is on unless --texture-lock off.
	const QString textureLock = value("--texture-lock").isEmpty() ? QStringLiteral("on") : value("--texture-lock");
	if (textureLock != QLatin1String("on") && textureLock != QLatin1String("off")) {
		return fail(2, Text::tr("--texture-lock requires on or off."));
	}
	const bool allowValve220 = parsed.seen.contains(QStringLiteral("--allow-valve220"));
	if (allowValve220 && textureLock == QLatin1String("off")) {
		return fail(2, Text::tr("--allow-valve220 requires texture lock."));
	}
	request.textures = {textureLock == QLatin1String("on"), allowValve220};

	LevelMapDocument document;
	if (!loadMap(parsed, path, &document, &result)) {
		return result;
	}
	QString error;
	if (!selectObjects(&document, objects, &error)) {
		return fail(2, error);
	}
	LevelObjectShearReport report;
	if (!shearLevelMapObjects(&document, request, &report, &error)) {
		return fail(4, error);
	}
	const LevelMapSaveReport saved = saveLevelMapAs(document, output, parsed.seen.contains(QStringLiteral("--dry-run")), parsed.seen.contains(QStringLiteral("--overwrite")));
	if (!saved.succeeded()) {
		return fail(4, saved.errors.join(QLatin1Char('\n')));
	}
	QJsonArray sheared;
	for (const LevelMapSelectionRef& ref : document.selection) {
		sheared.append(levelMapSelectionRefId(ref));
	}
	QJsonArray matrix;
	for (const double entry : report.matrix) {
		matrix.append(entry);
	}
	result.payload = QJsonObject {{QStringLiteral("placement"), QStringLiteral("side-shear")}, {QStringLiteral("side"), side},
		{QStringLiteral("offset"), vectorJson(request.offset)}, {QStringLiteral("anchor"), report.anchor},
		{QStringLiteral("factors"), QJsonArray {report.factors[0], report.factors[1], report.factors[2]}}, {QStringLiteral("matrix"), matrix},
		{QStringLiteral("mins"), vectorJson(report.mins)}, {QStringLiteral("maxs"), vectorJson(report.maxs)}, {QStringLiteral("objects"), report.objects},
		{QStringLiteral("selection"), sheared},
		{QStringLiteral("textureLockPolicy"), textureLock == QLatin1String("on") ? QStringLiteral("locked") : QStringLiteral("source-parameters")},
		{QStringLiteral("allowValve220"), allowValve220}};
	if (request.pivot.valid) {
		result.payload.insert(QStringLiteral("pivot"), vectorJson(request.pivot));
	}
	if (parsed.seen.contains(QStringLiteral("--context"))) {
		result.payload.insert(QStringLiteral("context"), value("--context"));
	}
	result.lines << Text::tr("Sheared %n object(s): side %1 slid %2, %3, %4; the plane at %5 along %6 stays.", nullptr, report.objects)
		.arg(side).arg(request.offset.x).arg(request.offset.y).arg(request.offset.z).arg(report.anchor).arg(QLatin1Char("XYZ"[request.axis]));
	addSave(saved, &result);
	return result;
}

LevelMoveRepeatCliResult runLevelRepeatTransforms(const QStringList& arguments)
{
	LevelMoveRepeatCliResult result;
	const auto fail = [&result](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QString command = QStringLiteral("map repeat-transforms");
	QSet<QString> options = commonOptions();
	options += QSet<QString> {"--set", "--translate", "--rotate", "--scale", "--shear", "--pivot", "--uv-lock"};
	const Parsed parsed = parseArguments(arguments, command,
		{"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run", "--allow-valve220", "--allow-collapse"}, options,
		{"--object", "--set"});
	if (!parsed.error.isEmpty()) {
		return fail(2, parsed.error);
	}
	if (parsed.positional.size() != (parsed.seen.contains(QStringLiteral("--input")) ? 2 : 3)) {
		return fail(2, Text::tr("map repeat-transforms needs one input map, positionally or with --input, plus --object or --set, a transform and --output."));
	}
	const QString path = parsed.seen.contains(QStringLiteral("--input")) ? parsed.values.value(QStringLiteral("--input")).first() : parsed.positional[2];
	const QString output = parsed.values.value(QStringLiteral("--output")).value(0);
	if (output.isEmpty()) {
		return fail(2, Text::tr("map repeat-transforms requires --output <save-as path>."));
	}

	// The transforms, as NetRadiant Custom's Repeat menu lists them.
	LevelRepeatTransforms transforms;
	const auto value = [&parsed](const char* name) { return parsed.values.value(QLatin1String(name)).value(0); };
	if (parsed.seen.contains(QStringLiteral("--translate"))
		&& !parseTriple(value("--translate"), &transforms.translation[0], &transforms.translation[1], &transforms.translation[2])) {
		return fail(2, Text::tr("--translate takes x,y,z."));
	}
	if (parsed.seen.contains(QStringLiteral("--scale"))
		&& (!parseTriple(value("--scale"), &transforms.scale[0], &transforms.scale[1], &transforms.scale[2])
			|| transforms.scale[0] == 0 || transforms.scale[1] == 0 || transforms.scale[2] == 0)) {
		return fail(2, Text::tr("--scale takes three nonzero factors x,y,z; a negative factor mirrors."));
	}
	if (parsed.seen.contains(QStringLiteral("--rotate"))) {
		const QStringList parts = value("--rotate").split(QLatin1Char(','));
		bool ok = false;
		transforms.rotationAxis = axisIndex(parts.value(0));
		transforms.rotationDegrees = parts.value(1).trimmed().toDouble(&ok);
		if (parts.size() != 2 || transforms.rotationAxis < 0 || !ok || !std::isfinite(transforms.rotationDegrees)) {
			return fail(2, Text::tr("--rotate takes an axis and degrees, such as z,90."));
		}
	}
	if (parsed.seen.contains(QStringLiteral("--shear"))) {
		const QStringList parts = value("--shear").split(QLatin1Char(','));
		bool ok = false;
		transforms.skewAxis = axisIndex(parts.value(0));
		transforms.skewAlong = axisIndex(parts.value(1));
		transforms.skewAmount = parts.value(2).trimmed().toDouble(&ok);
		if (parts.size() != 3 || transforms.skewAxis < 0 || transforms.skewAlong < 0 || transforms.skewAxis == transforms.skewAlong || !ok
			|| !std::isfinite(transforms.skewAmount) || std::abs(transforms.skewAmount) > 1024) {
			return fail(2, Text::tr("--shear takes the axis that slides, the axis it slides by and a factor, such as x,z,0.5."));
		}
	}
	if (transforms.isIdentity()) {
		return fail(2, Text::tr("Name at least one transform with --translate, --rotate, --scale or --shear."));
	}
	LevelMapVec3 pivot;
	if (parsed.seen.contains(QStringLiteral("--pivot"))) {
		pivot.valid = parseTriple(value("--pivot"), &pivot.x, &pivot.y, &pivot.z);
		if (!pivot.valid) {
			return fail(2, Text::tr("--pivot takes x,y,z."));
		}
	}

	const QStringList objects = parsed.values.value(QStringLiteral("--object"));
	const QStringList selections = parsed.values.value(QStringLiteral("--set"));
	if (objects.isEmpty() == selections.isEmpty()) {
		return fail(2, Text::tr("Name objects with --object kind:id, or picked components with --set brush:ID/vertex|edge|face/IDS or patch:ID/vertex/IDS, not both."));
	}
	const bool componentsMode = !selections.isEmpty();
	if (componentsMode && parsed.seen.contains(QStringLiteral("--texture-lock"))) {
		return fail(2, Text::tr("--texture-lock applies to --object copies; components take --uv-lock."));
	}
	if (!componentsMode && (parsed.seen.contains(QStringLiteral("--uv-lock")) || parsed.seen.contains(QStringLiteral("--allow-collapse")))) {
		return fail(2, Text::tr("--uv-lock and --allow-collapse apply only to --set components."));
	}
	const QString textureLock = parsed.values.value(QStringLiteral("--texture-lock")).value(0, QStringLiteral("on"));
	if (textureLock != QLatin1String("on") && textureLock != QLatin1String("off")) {
		return fail(2, Text::tr("--texture-lock requires on or off."));
	}
	const QString uvLock = parsed.values.value(QStringLiteral("--uv-lock")).value(0, QStringLiteral("off"));
	if (uvLock != QLatin1String("off") && uvLock != QLatin1String("on") && uvLock != QLatin1String("fit")) {
		return fail(2, Text::tr("--uv-lock requires off, on or fit."));
	}
	const bool allowValve220 = parsed.seen.contains(QStringLiteral("--allow-valve220"));
	if (allowValve220 && (componentsMode ? uvLock == QLatin1String("off") : textureLock == QLatin1String("off"))) {
		return fail(2, Text::tr("--allow-valve220 requires texture lock or UV lock."));
	}

	LevelPlacementRequest request;
	QString error;
	if (componentsMode) {
		if (!parseComponents(selections, &request.components, &error)) {
			return fail(2, error);
		}
		request.operation = LevelPlacementOperation::TransformComponents;
		request.componentTransform.pivot = pivot;
		request.componentTransform.textureLock = uvLock != QLatin1String("off");
		request.componentTransform.fitUvLock = uvLock == QLatin1String("fit");
		request.componentTransform.allowValve220 = allowValve220;
		request.componentTransform.allowCollapse = parsed.seen.contains(QStringLiteral("--allow-collapse"));
		if (!levelRepeatTransformsComponentRequest(transforms, &request.componentTransform, &error)) {
			return fail(2, error);
		}
	} else {
		request.operation = LevelPlacementOperation::RepeatTransforms;
		request.repeatPivot = pivot;
		request.textures = {textureLock == QLatin1String("on"), allowValve220};
	}

	LevelMapDocument document;
	if (!loadMap(parsed, path, &document, &result)) {
		return result;
	}
	if (!componentsMode && !selectObjects(&document, objects, &error)) {
		return fail(2, error);
	}
	document.repeat.transforms = transforms;
	++document.repeat.revision;
	auto prepared = prepareLevelPlacement(document, request);
	if (!prepared.succeeded) {
		return fail(4, prepared.error);
	}
	document = std::move(prepared.document);
	const LevelMapSaveReport saved = saveLevelMapAs(document, output, parsed.seen.contains(QStringLiteral("--dry-run")), parsed.seen.contains(QStringLiteral("--overwrite")));
	if (!saved.succeeded()) {
		return fail(4, saved.errors.join(QLatin1Char('\n')));
	}
	const QString applied = levelRepeatTransformsDescription(transforms).join(QStringLiteral("; "));
	result.payload = QJsonObject {{QStringLiteral("transforms"), levelRepeatTransformsJson(transforms)},
		{QStringLiteral("mode"), componentsMode ? QStringLiteral("components") : QStringLiteral("objects")},
		{QStringLiteral("textureLockPolicy"), componentsMode ? uvLock : (textureLock == QLatin1String("on") ? QStringLiteral("locked") : QStringLiteral("source-parameters"))},
		{QStringLiteral("allowValve220"), allowValve220}};
	if (componentsMode) {
		const LevelMapComponentReport& report = prepared.componentReport;
		result.payload.insert(QStringLiteral("brushes"), report.brushes);
		result.payload.insert(QStringLiteral("patches"), report.patches);
		result.payload.insert(QStringLiteral("collapsedVertices"), report.collapsedVertices);
		result.payload.insert(QStringLiteral("fittedFaces"), report.fittedFaces);
		if (pivot.valid) {
			result.payload.insert(QStringLiteral("pivot"), vectorJson(pivot));
		}
		result.lines << Text::tr("Repeated %1 on the components of %n object(s).", nullptr, report.brushes + report.patches).arg(applied);
	} else {
		QJsonArray copies;
		for (const LevelMapSelectionRef& ref : document.selection) {
			copies.append(levelMapSelectionRefId(ref));
		}
		result.payload.insert(QStringLiteral("copies"), copies);
		result.payload.insert(QStringLiteral("pivot"), vectorJson(prepared.repeatPivot));
		const int copied = static_cast<int>(copies.size());
		result.lines << Text::tr("Copied %n object(s) and repeated %1 about %2, %3, %4.", nullptr, copied)
			.arg(applied).arg(prepared.repeatPivot.x).arg(prepared.repeatPivot.y).arg(prepared.repeatPivot.z);
	}
	addSave(saved, &result);
	return result;
}
} // namespace vibestudio::cli
