#include "cli/model_frames.h"

#include "core/model_document.h"
#include "core/model_frame_import.h"
#include "core/model_frame_tools.h"
#include "core/model_tag_follow.h"
#include "core/model_topology.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelFramesCli)
};
ModelToolsCliResult fail(int code, const QString &message)
{
	ModelToolsCliResult result;
	result.exitCode = code;
	result.error = message;
	return result;
}
bool integer(const QString &text, int *value)
{
	bool ok = false;
	*value = text.toInt(&ok);
	return ok && *value >= 0 && text == QString::number(*value);
}
bool number(const QString &text, double *value)
{
	bool ok = false;
	*value = text.toDouble(&ok);
	return ok && std::isfinite(*value);
}
bool vector(const QString &text, ModelVec3 *value)
{
	const auto parts = text.split(QLatin1Char(','));
	double v[3]{};
	if (parts.size() != 3)
		return false;
	for (int i = 0; i < 3; ++i)
		if (!number(parts[i], &v[i]) || std::abs(v[i]) > 1000000)
			return false;
	*value = {float(v[0]), float(v[1]), float(v[2])};
	return true;
}
QJsonArray clipChecksJson(const ModelMesh &mesh)
{
	QJsonArray clips;
	const auto checks = modelNativeClipRoundTrip(mesh);
	for (const auto &check : checks)
	{
		const auto &clip = mesh.animations[check.clip];
		clips.append(QJsonObject{{"index", check.clip},
								 {"name", check.name},
								 {"first", check.first},
								 {"last", check.last},
								 {"fps", clip.framesPerSecond},
								 {"survivesNativeNames", check.survives},
								 {"rateLost", check.rateLost},
								 {"rebuiltName", check.rebuiltName},
								 {"rebuiltFirst", check.rebuiltFirst},
								 {"rebuiltLast", check.rebuiltLast}});
	}
	return clips;
}
// The options each operation accepts besides the common ones.
QHash<QString, QStringList> allowedOptions()
{
	const QStringList range{"--first", "--last"};
	const QStringList selection{"--surface", "--faces", "--vertices", "--edges"};
	const QStringList resample{"--clip", "--fps", "--frames", "--source-fps", "--method", "--cyclic"};
	const QStringList loop{"--clip", "--mode", "--blend-frames"};
	return {{"", {}},
			{"move", range + QStringList{"--to"}},
			{"duplicate", range},
			{"delete", range},
			{"reverse", range},
			{"ping-pong", range},
			{"name-from-clips", {"--clip", "--start", "--keep-clip-names"}},
			{"resample", resample},
			{"close-loop", loop},
			{"resample-skeletal", resample + QStringList{"--bake-curves"}},
			{"close-skeletal-loop", loop + QStringList{"--bake-curves"}},
			{"follow-tag", selection + QStringList{"--tag", "--reference-frame", "--mode", "--first", "--last", "--no-anchor"}},
			{"refit-tags", {"--tag"}},
			{"transform-range",
			 range + selection + QStringList{"--falloff", "--offset", "--rotate", "--scale", "--pivot-mode", "--pivot", "--pivot-frame"}},
			{"blend-pose", range + selection + QStringList{"--falloff", "--source-frame", "--amount"}},
			{"relative-pose", range + selection + QStringList{"--falloff", "--pose-frame", "--base-frame", "--weight"}},
			{"import",
			 {"--source", "--match", "--tolerance", "--reference-frame", "--insert-after", "--replace-from", "--clip-name", "--clip-fps", "--obj-up",
			  "--obj-scale"}},
			{"export-obj", range}};
}
} // namespace

ModelToolsCliResult runModelFrames(const QStringList &args)
{
	const QSet<QString> flags{"--cli",	   "--json",		  "--quiet",  "--verbose",	   "--dry-run",
							  "--overwrite", "--keep-clip-names", "--cyclic", "--bake-curves", "--no-anchor"};
	const QSet<QString> options{"--input",	   "--output",	   "--operation",	"--first",		  "--last",		  "--to",
								"--clip",	   "--start",	   "--fps",			"--frames",		  "--source-fps", "--method",
								"--mode",	   "--blend-frames", "--tag",		"--surface",	  "--faces",	  "--vertices",
								"--edges",	   "--reference-frame", "--falloff", "--offset",	  "--rotate",	  "--scale",
								"--pivot-mode", "--pivot",	   "--pivot-frame", "--source-frame", "--amount",	  "--pose-frame",
								"--base-frame", "--weight",	   "--source",		"--match",		  "--tolerance",  "--insert-after",
								"--replace-from", "--clip-name", "--clip-fps",	"--obj-up",		  "--obj-scale",  "--settings-file",
								"--locale",	   "--catalog-root"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional, sources;
	for (int i = 1; i < args.size(); ++i)
	{
		const auto arg = args[i];
		if (!arg.startsWith('-'))
		{
			positional.append(arg);
			continue;
		}
		const int equal = arg.indexOf('=');
		const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key) && key != "--source")
			return fail(2, Text::tr("Repeated option: %1.").arg(key));
		seen.insert(key);
		if (flags.contains(key) && equal < 0)
			continue;
		if (!options.contains(key))
			return fail(2, Text::tr("Unknown or invalid option: %1.").arg(key));
		const auto value = equal < 0 ? args.value(++i) : arg.mid(equal + 1);
		if (value.isEmpty() || value.startsWith("--"))
			return fail(2, Text::tr("Option %1 requires a value.").arg(key));
		if (key == "--source")
			sources.append(value);
		else
			values[key] = value;
	}
	const bool input = seen.contains("--input");
	if (positional.size() != (input ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "frames")
		return fail(2, Text::tr("Provide one editable model source, positionally or with --input."));
	const auto operation = values.value("--operation");
	const auto allowed = allowedOptions();
	if (!allowed.contains(operation) || (seen.contains("--operation") && operation.isEmpty()))
		return fail(2, Text::tr("Choose --operation move, duplicate, delete, reverse, ping-pong, name-from-clips, resample, close-loop, "
								"resample-skeletal, close-skeletal-loop, follow-tag, refit-tags, transform-range, blend-pose, relative-pose, "
								"import or export-obj, or omit it to check which clips come back from MDL, MD2 and MD3 frame names."));
	const QSet<QString> common{"--cli", "--json", "--quiet", "--verbose", "--input", "--operation", "--settings-file", "--locale", "--catalog-root"};
	const bool inspecting = operation.isEmpty();
	for (const auto &key : std::as_const(seen))
	{
		const bool output = key == "--output" || key == "--dry-run" || key == "--overwrite";
		if (common.contains(key) || allowed[operation].contains(key) || (output && !inspecting))
			continue;
		return fail(2, Text::tr("%1 does not apply to %2.").arg(key, inspecting ? Text::tr("the clip check") : operation));
	}
	const QHash<QString, ModelEditKind> kinds{{"move", ModelEditKind::MoveFrames},
											  {"duplicate", ModelEditKind::DuplicateFrameRange},
											  {"delete", ModelEditKind::DeleteFrameRange},
											  {"reverse", ModelEditKind::ReverseFrameRange},
											  {"ping-pong", ModelEditKind::ReverseFrameRange},
											  {"name-from-clips", ModelEditKind::RenameFramesFromClips},
											  {"resample", ModelEditKind::ResampleAnimation},
											  {"close-loop", ModelEditKind::CloseAnimationLoop},
											  {"resample-skeletal", ModelEditKind::ResampleSkeletalClip},
											  {"close-skeletal-loop", ModelEditKind::CloseSkeletalLoop},
											  {"follow-tag", ModelEditKind::FollowTagComponents},
											  {"refit-tags", ModelEditKind::RefitAnchoredTags},
											  {"transform-range", ModelEditKind::TransformFrameRange},
											  {"blend-pose", ModelEditKind::BlendFramePose},
											  {"relative-pose", ModelEditKind::ApplyRelativePose},
											  {"import", ModelEditKind::ImportFrames}};
	const bool exporting = operation == "export-obj";
	if (!inspecting && !exporting && !values.value("--output").endsWith(".mesh.json", Qt::CaseInsensitive))
		return fail(2, Text::tr("Frame operations need --output ending in .mesh.json."));
	if (exporting && !values.value("--output").endsWith(".obj", Qt::CaseInsensitive))
		return fail(2, Text::tr("export-obj needs --output naming the OBJ files, such as walk_###.obj; # marks the frame number."));
	const auto source = input ? values["--input"] : positional[2];
	if (!inspecting && !exporting && modelPathsReferToSameFile(source, values["--output"]))
		return fail(2, Text::tr("Choose an output different from the source, including aliases of the same file."));
	ModelDocument document;
	QString error;
	if (!document.load(source, &error))
		return fail(1, error);
	const auto &mesh = document.mesh();
	ModelEdit edit;
	edit.kind = kinds.value(operation, ModelEditKind::MoveFrames);
	// Shared option readers.
	const auto readInt = [&](const char *key, int *target, bool required) -> bool
	{
		const QString name = QString::fromLatin1(key);
		if (!seen.contains(name))
			return !required;
		return integer(values.value(name), target);
	};
	const auto falloff = [&](ModelFrameFalloff *target)
	{
		static const QHash<QString, ModelFrameFalloff> names{{"constant", ModelFrameFalloff::Constant},
															 {"ramp-in", ModelFrameFalloff::RampIn},
															 {"ramp-out", ModelFrameFalloff::RampOut},
															 {"smooth", ModelFrameFalloff::Smooth}};
		const auto value = values.value("--falloff", "constant");
		if (!names.contains(value))
			return false;
		*target = names.value(value);
		return true;
	};
	const auto selectComponents = [&](QString *problem)
	{
		int surface = 0;
		if (!readInt("--surface", &surface, false) || surface >= mesh.surfaces.size())
		{
			*problem = Text::tr("--surface needs an existing zero-based surface index.");
			return false;
		}
		edit.selection.surface = surface;
		const auto &target = mesh.surfaces[surface];
		const auto indices = [&](const char *key, int count, QSet<int> *result)
		{
			const QString name = QString::fromLatin1(key);
			if (!seen.contains(name))
				return true;
			const auto value = values.value(name);
			if (value == "all")
			{
				for (int i = 0; i < count; ++i)
					result->insert(i);
				return true;
			}
			for (const auto &word : value.split(QLatin1Char(',')))
			{
				int index = -1;
				if (!integer(word, &index) || index >= count)
					return false;
				result->insert(index);
			}
			return true;
		};
		if (!indices("--faces", int(target.triangles.size()), &edit.selection.faces) ||
			!indices("--vertices", target.vertexCount, &edit.selection.vertices))
		{
			*problem = Text::tr("--faces and --vertices need all or comma-separated existing indices.");
			return false;
		}
		if (seen.contains("--edges"))
		{
			const auto available = modelSurfaceEdges(target);
			for (const auto &word : values.value("--edges").split(QLatin1Char(',')))
			{
				const auto pair = word.split(QLatin1Char(':'));
				int a = -1, b = -1;
				if (pair.size() != 2 || !integer(pair[0], &a) || !integer(pair[1], &b) ||
					!std::binary_search(available.cbegin(), available.cend(), modelEdge(a, b)))
				{
					*problem = Text::tr("--edges needs comma-separated existing vertex pairs a:b.");
					return false;
				}
				edit.selection.edges.insert(modelEdge(a, b));
			}
		}
		if (edit.selection.faces.isEmpty() && edit.selection.vertices.isEmpty() && edit.selection.edges.isEmpty())
		{
			*problem = Text::tr("Select components with --faces, --vertices or --edges.");
			return false;
		}
		return true;
	};
	QString problem;
	auto &range = edit.frameRange;
	auto &resample = edit.resample;
	auto &pose = edit.poseRange;
	if (operation == "move" || operation == "duplicate" || operation == "delete" || operation == "reverse" || operation == "ping-pong")
	{
		if (!readInt("--first", &range.first, true) || !readInt("--last", &range.last, true))
			return fail(2, Text::tr("Range operations need --first and --last: inclusive zero-based frame indices."));
		if (operation == "move" && !readInt("--to", &range.destination, true))
			return fail(2, Text::tr("move needs --to: the zero-based index the first moved frame takes."));
		range.pingPong = operation == "ping-pong";
	}
	else if (operation == "name-from-clips")
	{
		if (!readInt("--clip", &range.clip, false))
			return fail(2, Text::tr("--clip needs a zero-based clip index. Use model animations to list clips."));
		if (!readInt("--start", &range.startNumber, false) || range.startNumber > 1000)
			return fail(2, Text::tr("--start needs the first frame number, from 0 to 1000 (default 1)."));
		range.renameClips = !seen.contains("--keep-clip-names");
	}
	else if (operation == "resample" || operation == "resample-skeletal")
	{
		if (!readInt("--clip", &resample.clip, true))
			return fail(2, Text::tr("%1 needs --clip: a zero-based clip index.").arg(operation));
		if ((seen.contains("--fps") && !number(values["--fps"], &resample.targetFps)) ||
			(seen.contains("--source-fps") && !number(values["--source-fps"], &resample.sourceFps)) ||
			!readInt("--frames", &resample.targetFrames, false) || seen.contains("--fps") == seen.contains("--frames"))
			return fail(2, Text::tr("Give either --fps (0.001 to 1000) or --frames (1 to 1024), and optionally --source-fps."));
		const auto method = values.value("--method", "linear");
		if (method != "linear" && method != "nearest")
			return fail(2, Text::tr("--method takes linear or nearest."));
		resample.method = method == "nearest" ? ModelResampleMethod::Nearest : ModelResampleMethod::Linear;
		resample.cyclic = seen.contains("--cyclic");
		resample.bakeCurves = seen.contains("--bake-curves");
	}
	else if (operation == "close-loop" || operation == "close-skeletal-loop")
	{
		if (!readInt("--clip", &resample.clip, true))
			return fail(2, Text::tr("%1 needs --clip: a zero-based clip index.").arg(operation));
		const auto mode = values.value("--mode", "replace");
		if (mode != "replace" && mode != "append" && mode != "crossfade")
			return fail(2, Text::tr("--mode takes replace, append or crossfade."));
		resample.loopClose = mode == "append" ? ModelLoopClose::AppendFirst : mode == "crossfade" ? ModelLoopClose::CrossFade : ModelLoopClose::ReplaceLast;
		if (!readInt("--blend-frames", &resample.blendFrames, resample.loopClose == ModelLoopClose::CrossFade) ||
			(resample.loopClose != ModelLoopClose::CrossFade && seen.contains("--blend-frames")))
			return fail(2, Text::tr("A cross-fade needs --blend-frames N; other modes take none."));
		resample.bakeCurves = seen.contains("--bake-curves");
	}
	else if (operation == "follow-tag")
	{
		auto &follow = edit.tagFollow;
		follow.tag = values.value("--tag");
		if (follow.tag.isEmpty())
			return fail(2, Text::tr("follow-tag needs --tag: an existing or new tag name."));
		if (!readInt("--reference-frame", &follow.referenceFrame, false))
			return fail(2, Text::tr("--reference-frame needs a zero-based frame index."));
		const auto mode = values.value("--mode", "rigid");
		if (mode != "rigid" && mode != "centroid" && mode != "surface")
			return fail(2, Text::tr("--mode takes rigid, centroid or surface."));
		follow.mode = mode == "surface" ? ModelTagFollowMode::Surface : mode == "centroid" ? ModelTagFollowMode::Centroid : ModelTagFollowMode::Rigid;
		if (seen.contains("--first") != seen.contains("--last") || !readInt("--first", &follow.first, false) ||
			!readInt("--last", &follow.last, false))
			return fail(2, Text::tr("Give both --first and --last, or neither for every frame."));
		if (!selectComponents(&problem))
			return fail(2, problem);
		follow.keepAnchor = !seen.contains("--no-anchor");
	}
	else if (operation == "refit-tags")
		edit.tagFollow.tag = values.value("--tag");
	else if (operation == "transform-range" || operation == "blend-pose" || operation == "relative-pose")
	{
		if (!readInt("--first", &pose.first, true) || !readInt("--last", &pose.last, true))
			return fail(2, Text::tr("%1 needs --first and --last: the inclusive frame range.").arg(operation));
		if (!falloff(&pose.falloff))
			return fail(2, Text::tr("--falloff takes constant, ramp-in, ramp-out or smooth."));
		if (!selectComponents(&problem))
			return fail(2, problem);
		if (operation == "transform-range")
		{
			if ((seen.contains("--offset") && !vector(values["--offset"], &edit.translation)) ||
				(seen.contains("--rotate") && !vector(values["--rotate"], &edit.rotation)) ||
				(seen.contains("--scale") && !vector(values["--scale"], &edit.scale)) ||
				(seen.contains("--pivot") && !vector(values["--pivot"], &edit.pivot)))
				return fail(2, Text::tr("--offset, --rotate, --scale and --pivot take x,y,z numbers."));
			const auto pivot = values.value("--pivot-mode", seen.contains("--pivot") ? "custom" : "selection");
			if (pivot != "origin" && pivot != "selection" && pivot != "custom")
				return fail(2, Text::tr("--pivot-mode takes origin, selection or custom."));
			edit.pivotMode = pivot == "origin" ? ModelTransformPivot::Origin : pivot == "custom" ? ModelTransformPivot::Custom : ModelTransformPivot::SelectionCentre;
			if (!readInt("--pivot-frame", &edit.pivotFrame, false))
				return fail(2, Text::tr("--pivot-frame needs a zero-based frame index."));
			if (!seen.contains("--pivot-frame"))
				edit.pivotFrame = pose.first;
		}
		else if (operation == "blend-pose")
		{
			if (!readInt("--source-frame", &pose.sourceFrame, true) || (seen.contains("--amount") && !number(values["--amount"], &pose.amount)))
				return fail(2, Text::tr("blend-pose needs --source-frame, and optionally --amount from 0 to 1."));
		}
		else if (!readInt("--pose-frame", &pose.sourceFrame, true) || !readInt("--base-frame", &pose.baseFrame, true) ||
				 (seen.contains("--weight") && !number(values["--weight"], &pose.amount)))
			return fail(2, Text::tr("relative-pose needs --pose-frame and --base-frame, and optionally --weight from -10 to 10."));
	}
	else if (operation == "import")
	{
		auto &o = edit.frameImport;
		if (sources.isEmpty())
			return fail(2, Text::tr("import needs one or more --source files: an OBJ sequence or an MD2, MD3, MDL or editable source."));
		const auto match = values.value("--match", "corners");
		if (match != "corners" && match != "exact" && match != "nearest")
			return fail(2, Text::tr("--match takes corners, exact or nearest."));
		o.match = match == "exact" ? ModelFrameMatch::Exact : match == "nearest" ? ModelFrameMatch::Nearest : ModelFrameMatch::Corners;
		if ((seen.contains("--tolerance") && !number(values["--tolerance"], &o.tolerance)) || !readInt("--reference-frame", &o.referenceFrame, false))
			return fail(2, Text::tr("--tolerance takes a distance and --reference-frame a zero-based frame index."));
		if (seen.contains("--insert-after") && seen.contains("--replace-from"))
			return fail(2, Text::tr("Use --insert-after or --replace-from, not both."));
		if (!readInt("--insert-after", &o.insertAfter, false) || !readInt("--replace-from", &o.replaceFirst, false))
			return fail(2, Text::tr("--insert-after and --replace-from need a zero-based frame index."));
		o.clipName = values.value("--clip-name");
		if (seen.contains("--clip-fps") && !number(values["--clip-fps"], &o.clipFps))
			return fail(2, Text::tr("--clip-fps takes a rate from 0.001 to 1000."));
		ModelObjImportOptions obj;
		const auto up = values.value("--obj-up", "z");
		if (up != "z" && up != "y")
			return fail(2, Text::tr("--obj-up takes z or y."));
		obj.up = up == "y" ? ModelObjUpAxis::Y : ModelObjUpAxis::Z;
		if (seen.contains("--obj-scale") && !number(values["--obj-scale"], &obj.scale))
			return fail(2, Text::tr("--obj-scale takes a factor from 0.000001 to 1,000,000."));
		if (!readModelFrameSources(naturalSortedModelFramePaths(sources), obj, values.value("--output"), &o.sources, &o.sourceNames, &error))
			return fail(4, error);
		for (const auto &path : sources)
			if (modelPathsReferToSameFile(path, source))
				return fail(2, Text::tr("%1 is the model's own source; choose other files to import frames from.").arg(path));
	}
	ModelToolsCliResult result;
	result.payload = {{"source", QFileInfo(source).absoluteFilePath()}, {"written", false}, {"dryRun", seen.contains("--dry-run")}};
	if (exporting)
	{
		int first = 0, last = int(mesh.frames.size()) - 1;
		if (!readInt("--first", &first, false) || !readInt("--last", &last, false))
			return fail(2, Text::tr("--first and --last need zero-based frame indices."));
		QStringList written;
		const bool dry = seen.contains("--dry-run");
		if (!exportModelFrameObjSequence(mesh, first, last, values["--output"], seen.contains("--overwrite"), dry, {QFileInfo(source).absoluteFilePath()},
										 &written, &error))
			return fail(4, error);
		QJsonArray files;
		for (const auto &file : written)
			files.append(QFileInfo(file).absoluteFilePath());
		result.payload.insert("operation", operation);
		result.payload.insert("files", files);
		result.payload.insert("written", !dry);
		const int count = int(written.size());
		result.lines << (dry ? Text::tr("Would write %n OBJ files.", nullptr, count) : Text::tr("Wrote %n OBJ files.", nullptr, count));
		return result;
	}
	ModelTagFollowResult follow;
	if (!inspecting)
	{
		const auto target = inspectModelWriteTarget(values["--output"]);
		if (!target.isValid())
			return fail(1, target.error);
		if (target.existed && !seen.contains("--overwrite"))
			return fail(1, Text::tr("The output already exists; use --overwrite to replace it."));
		if (edit.kind == ModelEditKind::FollowTagComponents && !solveModelTagFollow(mesh, edit.selection, edit.tagFollow, &follow, &error))
			return fail(4, error);
		ModelEditReport report;
		if (!document.edit(edit, &error, {}, &report))
			return fail(4, error);
		const bool dry = seen.contains("--dry-run");
		if (!document.save(values["--output"], seen.contains("--overwrite"), &error, {}, dry))
			return fail(1, error);
		result.payload.insert("operation", operation);
		result.payload.insert("output", QFileInfo(values["--output"]).absoluteFilePath());
		result.payload.insert("written", !dry);
		result.payload.insert("outputSource", modelSourceSaveJson(document));
		const auto provenance = modelSkinProvenanceSummary(report.skin);
		if (!provenance.isEmpty())
			result.lines << provenance;
		if (edit.kind == ModelEditKind::FollowTagComponents)
		{
			QJsonArray residuals;
			for (double residual : std::as_const(follow.residuals))
				residuals.append(residual);
			result.payload.insert("tagFollow", QJsonObject{{"tag", edit.tagFollow.tag},
														   {"created", follow.created},
														   {"points", follow.points},
														   {"frames", int(follow.poses.size())},
														   {"residuals", residuals},
														   {"maxResidual", follow.maxResidual},
														   {"worstFrame", follow.worstFrame}});
			result.lines << modelTagFollowSummary(edit.tagFollow, follow);
		}
		if (edit.kind == ModelEditKind::ImportFrames)
			result.payload.insert("importedFrames", modelFrameSourceCount(edit.frameImport.sources));
		if (edit.kind == ModelEditKind::FollowTagComponents || edit.kind == ModelEditKind::RefitAnchoredTags)
		{
			QJsonArray anchors;
			for (const auto &anchor : document.mesh().tagAnchors)
				anchors.append(QJsonObject{{"tag", anchor.tag},
										   {"surface", anchor.surface},
										   {"vertices", int(anchor.vertices.size())},
										   {"removed", anchor.removed},
										   {"problem", modelTagAnchorProblem(document.mesh(), anchor)}});
			result.payload.insert("tagAnchors", anchors);
		}
		if (edit.kind == ModelEditKind::ResampleSkeletalClip || edit.kind == ModelEditKind::CloseSkeletalLoop)
		{
			const auto &clip = document.mesh().skeleton.clips.value(resample.clip);
			result.payload.insert("skeletalClip", QJsonObject{{"name", clip.name}, {"frames", int(clip.frames.size())}, {"fps", clip.framesPerSecond}});
		}
	}
	const auto &after = document.mesh();
	QJsonArray names;
	for (const auto &frame : after.frames)
		names.append(frame.name);
	result.payload.insert("frames", int(after.frames.size()));
	result.payload.insert("frameNames", names);
	result.payload.insert("clips", clipChecksJson(after));
	const auto checks = modelNativeClipRoundTrip(after);
	const auto summary = modelNativeClipSummary(checks);
	result.payload.insert("nativeClipSummary", summary);
	const int frameCount = int(after.frames.size());
	result.lines << Text::tr("%n frames.", nullptr, frameCount);
	if (!summary.isEmpty())
		result.lines << summary;
	for (const auto &check : checks)
	{
		if (check.survives)
			result.lines << Text::tr("Clip %1 %2: frames %3–%4; comes back from frame names.")
								.arg(check.clip)
								.arg(check.name)
								.arg(check.first)
								.arg(check.last);
		else
			result.lines << Text::tr("Clip %1 %2: frames %3–%4; frame names give %5 (frames %6–%7) instead.")
								.arg(check.clip)
								.arg(check.name)
								.arg(check.first)
								.arg(check.last)
								.arg(check.rebuiltName)
								.arg(check.rebuiltFirst)
								.arg(check.rebuiltLast);
	}
	return result;
}
} // namespace vibestudio::cli
