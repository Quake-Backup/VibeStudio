#include "cli/model_tools.h"
#include "core/model_document.h"
#include "core/model_skeleton.h"
#include "core/model_joint_tools.h"
#include "core/model_skeletal_working_set.h"
#include "core/model_pose_clipboard.h"
#include "core/model_clip_import.h"
#include "core/model_mesh.h"
#include "core/model_vertex_origins.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>

namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelPoseCli)
};
ModelToolsCliResult fail(int code, const QString &message)
{
	return {code, message, {}, {}};
}
bool integer(const QString &text, int *value)
{
	bool ok = false;
	*value = text.toInt(&ok);
	return ok && *value >= 0 && text == QString::number(*value);
}
// The mirror axis and destination joint pairs for paste and mirror: explicit
// --mirror x|y|z and --mirror-joints left=right,..., or detected from the
// rig's left/right names ("detect", the default for mirror).
bool mirrorSettings(const ModelMesh &mesh, const QSet<QString> &seen, const QHash<QString, QString> &values, bool byDefault,
					ModelPosePasteOptions *o, ModelToolsCliResult *failure)
{
	const QStringList axes{"x", "y", "z"};
	QMap<QString, QString> detected;
	const int detectedAxis = detectModelPoseMirrorAxis(mesh, &detected);
	if (seen.contains("--mirror"))
	{
		o->mirrorAxis = int(axes.indexOf(values.value("--mirror")));
		if (o->mirrorAxis < 0)
		{
			*failure = fail(2, Text::tr("Mirror axis must be x, y or z."));
			return false;
		}
	}
	else if (byDefault || seen.contains("--mirror-joints") || seen.contains("--plane"))
	{
		if (detectedAxis < 0)
		{
			*failure = fail(4, Text::tr("No left/right joint pairs were found to choose a mirror axis. Give --mirror and --mirror-joints."));
			return false;
		}
		o->mirrorAxis = detectedAxis;
	}
	if (o->mirrorAxis < 0)
		return true;
	if (seen.contains("--plane"))
	{
		bool ok = false;
		o->plane = values.value("--plane").toDouble(&ok);
		if (!ok || !std::isfinite(o->plane))
		{
			*failure = fail(2, Text::tr("The mirror plane must be a finite number."));
			return false;
		}
	}
	const auto pairs = values.value("--mirror-joints", "detect");
	if (pairs == "detect")
		o->mirrorJoints = modelMirrorJointPairsForAxis(mesh, detectModelMirrorJointPairs(mesh.skeleton), o->mirrorAxis, -1, .001);
	else
	{
		QString error;
		if (!parseModelMirrorJointMap(pairs, &o->mirrorJoints, &error))
		{
			*failure = fail(2, Text::tr("Use --mirror-joints detect or --mirror-joints left=right,... %1").arg(error));
			return false;
		}
	}
	return true;
}
ModelToolsCliResult runModelPoseImport(const QString &input, const QSet<QString> &seen, const QHash<QString, QString> &values)
{
	const bool dry = seen.contains("--dry-run");
	const auto output = values.value("--output"), sourcePath = values.value("--source");
	if (modelPathsReferToSameFile(input, output) || modelPathsReferToSameFile(sourcePath, output) ||
		(seen.contains("--joint-map") && modelPathsReferToSameFile(values.value("--joint-map"), output)))
		return fail(1, Text::tr("The output cannot replace an input, including aliases of the same file."));
	ModelDocument document;
	QString error;
	if (!document.load(input, &error))
		return fail(1, error);
	// The source: another editable source, or any skeletal model or animation
	// file read with its companions (md5anim, IQM, MDS, MDR, MDX, GLA).
	ModelMesh source;
	if (sourcePath.endsWith(".mesh.json", Qt::CaseInsensitive))
	{
		ModelDocument other;
		if (!other.load(sourcePath, &error))
			return fail(1, error);
		source = other.mesh();
	}
	else
	{
		source = decodeModelMeshFile(QFileInfo(sourcePath).absoluteFilePath());
		if (!source.error.isEmpty())
			return fail(1, source.error);
	}
	if (source.skeleton.clips.isEmpty())
		return fail(4, Text::tr("The source holds no skeletal clips."));
	int sourceClip = 0;
	if (seen.contains("--source-clip"))
	{
		const auto wanted = values.value("--source-clip");
		sourceClip = -1;
		for (int i = 0; i < source.skeleton.clips.size() && sourceClip < 0; ++i)
			if (source.skeleton.clips[i].name == wanted)
				sourceClip = i;
		if (sourceClip < 0 && !integer(wanted, &sourceClip))
			return fail(2, Text::tr("Option --source-clip takes a clip name or index of the source."));
	}
	ModelEdit edit;
	edit.kind = ModelEditKind::ImportSkeletalClip;
	edit.selection = document.selection();
	auto &o = edit.animation.import;
	o.name = values.value("--name");
	o.retarget = seen.contains("--retarget");
	o.fillFromBind = seen.contains("--fill-from-bind");
	if (seen.contains("--fps"))
	{
		bool ok = false;
		o.framesPerSecond = values.value("--fps").toDouble(&ok);
		if (!ok || !std::isfinite(o.framesPerSecond) || o.framesPerSecond < .001 || o.framesPerSecond > 1000)
			return fail(2, Text::tr("FPS must be from 0.001 to 1000."));
	}
	if (seen.contains("--root-scale") && values.value("--root-scale") != "auto")
	{
		bool ok = false;
		o.rootScale = values.value("--root-scale").toDouble(&ok);
		if (!ok || !std::isfinite(o.rootScale) || o.rootScale <= 0 || o.rootScale > 1000)
			return fail(2, Text::tr("Root scale must be auto or a positive factor up to 1000."));
	}
	if (seen.contains("--copy-translation"))
		o.copyTranslation = values.value("--copy-translation").split(',');
	if (seen.contains("--joint-map"))
	{
		QByteArray bytes;
		if (!readModelFile(values.value("--joint-map"), &bytes, &error) || !parseModelJointMapJson(bytes, &o.joints, &error))
			return fail(2, error);
	}
	if (!modelClipImportFromSource(source, sourceClip, QFileInfo(sourcePath).fileName(), &o, &error))
		return fail(4, error);
	if (seen.contains("--loop"))
		o.loops = values.value("--loop") == "yes";
	if (seen.contains("--loop") && values.value("--loop") != "yes" && values.value("--loop") != "no")
		return fail(2, Text::tr("Option --loop requires yes or no."));
	ModelClipImportPlan plan;
	if (!planModelClipImport(document.mesh(), o, &plan, &error) || !document.edit(edit, &error))
		return fail(4, error);
	const auto target = inspectModelWriteTarget(output);
	if (!target.isValid())
		return fail(1, target.error);
	if (target.existed && !seen.contains("--overwrite"))
		return fail(1, Text::tr("The output exists; use --overwrite to replace it."));
	if (!document.save(output, seen.contains("--overwrite"), &error, {}, dry))
		return fail(1, error);
	ModelToolsCliResult result;
	QJsonArray joints;
	int mapped = 0;
	for (const auto &joint : plan.joints)
	{
		mapped += !joint.source.isEmpty();
		joints.append(QJsonObject{{"joint", joint.destination}, {"source", joint.source}, {"translation", joint.translation}});
	}
	const auto &clip = document.mesh().skeleton.clips.last();
	const bool baked = document.mesh().skeleton.bakedClipForFrame.contains(int(document.mesh().skeleton.clips.size()) - 1);
	result.lines << (dry ? Text::tr("Clip import validated; no files written.") : Text::tr("Saved the imported clip to %1.").arg(output))
				 << Text::tr("Imported %1 as clip %2: %3 samples at %4 FPS, %5 of %6 joints mapped.")
						.arg(source.skeleton.clips[sourceClip].name, clip.name)
						.arg(clip.frames.size())
						.arg(clip.framesPerSecond)
						.arg(mapped)
						.arg(plan.joints.size());
	if (o.retarget)
		result.lines << Text::tr("Retargeted; root motion scaled by %1.").arg(plan.rootScale);
	if (!baked)
		result.lines << Text::tr("The clip is outside the working set; add it with --operation working-set to preview it.");
	if (!plan.unusedSourceJoints.isEmpty())
		result.lines << Text::tr("Unused source joints: %1.").arg(plan.unusedSourceJoints.join(QStringLiteral(", ")));
	result.payload = {{"source", QFileInfo(input).absoluteFilePath()},
					  {"operation", "import"},
					  {"animationSource", QFileInfo(sourcePath).absoluteFilePath()},
					  {"sourceClip", source.skeleton.clips[sourceClip].name},
					  {"clip", int(document.mesh().skeleton.clips.size()) - 1},
					  {"name", clip.name},
					  {"samples", int(clip.frames.size())},
					  {"fps", clip.framesPerSecond},
					  {"retarget", o.retarget},
					  {"rootScale", plan.rootScale},
					  {"inWorkingSet", baked},
					  {"joints", joints},
					  {"unusedSourceJoints", QJsonArray::fromStringList(plan.unusedSourceJoints)},
					  {"outputPath", QFileInfo(output).absoluteFilePath()},
					  {"written", !dry},
					  {"dryRun", dry}};
	result.payload.insert("outputSource", modelSourceSaveJson(document));
	if (const auto packed = modelSourceSaveNotice(document); !packed.isEmpty())
		result.lines << packed;
	return result;
}
ModelToolsCliResult runModelPoseClipboard(const QString &input, const QString &operation, const QSet<QString> &seen,
										  const QHash<QString, QString> &values)
{
	const bool copyOut = operation == "copy-out", paste = operation == "paste";
	const bool dry = seen.contains("--dry-run");
	const auto output = values.value("--output");
	if (modelPathsReferToSameFile(input, output) || (paste && modelPathsReferToSameFile(values.value("--clipboard"), output)))
		return fail(1, Text::tr("The output cannot replace an input, including aliases of the same file."));
	ModelDocument document;
	QString error;
	if (!document.load(input, &error))
		return fail(1, error);
	const auto &mesh = document.mesh();
	if (mesh.skeleton.isEmpty())
		return fail(4, Text::tr("The model has no skeleton, so it has no poses to copy or paste."));
	ModelPoseCopyOptions copy;
	int clip = -1;
	if (!(copyOut && values.value("--clip") == "bind") && !integer(values.value("--clip"), &clip))
		return fail(2, copyOut ? Text::tr("Option --clip requires a nonnegative integer or bind.") : Text::tr("Option --clip requires a nonnegative integer."));
	copy.clip = clip;
	for (const auto &field : {qMakePair("--first", &copy.first), qMakePair("--last", &copy.last)})
		if (seen.contains(field.first) && !integer(values.value(field.first), field.second))
			return fail(2, Text::tr("Option %1 requires a nonnegative integer.").arg(field.first));
	if (copyOut && !seen.contains("--last"))
		copy.last = copy.first;
	if (seen.contains("--joints"))
		copy.joints = values.value("--joints").split(',');
	copy.keys = seen.contains("--keys");
	if (seen.contains("--channels"))
	{
		if (!copy.keys)
			return fail(2, Text::tr("Option --channels applies only with --keys."));
		for (const auto &id : values.value("--channels").split(','))
		{
			const int channel = modelCurveChannelIndex(id);
			if (channel < 0)
				return fail(2, Text::tr("Channels are tx, ty, tz, rx, ry, rz, sx, sy and sz."));
			copy.channels.append(channel);
		}
	}
	ModelToolsCliResult result;
	if (copyOut)
	{
		ModelPoseClipboard clipboard;
		if (!copyModelPose(mesh, copy, &clipboard, &error))
			return fail(4, error);
		const auto bytes = modelPoseClipboardBytes(clipboard);
		const auto target = inspectModelWriteTarget(output);
		if (!target.isValid())
			return fail(1, target.error);
		if (target.existed && !seen.contains("--overwrite"))
			return fail(1, Text::tr("The output exists; use --overwrite to replace it."));
		if (dry ? !validateModelWriteTarget(target, &error) : !writeModelFile(target, bytes, &error))
			return fail(1, error);
		result.lines << (dry ? Text::tr("Pose copy validated; no files written.") : Text::tr("Wrote the pose copy to %1.").arg(output))
					 << modelPoseClipboardSummary(clipboard);
		result.payload = {{"source", QFileInfo(input).absoluteFilePath()},
						  {"operation", operation},
						  {"kind", clipboard.kind == ModelPoseClipboardKind::Keys ? "keys" : "pose"},
						  {"mimeType", QString::fromLatin1(modelPoseClipboardMimeType)},
						  {"clip", clip},
						  {"first", copy.first},
						  {"samples", clipboard.samples},
						  {"joints", QJsonArray::fromStringList(clipboard.joints)},
						  {"bytes", int(bytes.size())},
						  {"outputPath", QFileInfo(output).absoluteFilePath()},
						  {"written", !dry},
						  {"dryRun", dry}};
		return result;
	}
	ModelEdit edit;
	edit.kind = ModelEditKind::PastePose;
	edit.selection = document.selection();
	auto &o = edit.animation.paste;
	o.clip = clip;
	o.first = copy.first;
	o.replaceKeys = seen.contains("--replace-keys");
	ModelToolsCliResult failure;
	if (!mirrorSettings(mesh, seen, values, !paste, &o, &failure))
		return failure;
	ModelPoseClipboard clipboard;
	if (paste)
	{
		if (!readModelFile(values.value("--clipboard"), &o.clipboard, &error) || !parseModelPoseClipboard(o.clipboard, &clipboard, &error))
			return fail(2, error);
		if (seen.contains("--joint-map"))
		{
			QByteArray bytes;
			if (modelPathsReferToSameFile(values.value("--joint-map"), output))
				return fail(1, Text::tr("The output cannot replace the joint map input, including aliases of the same file."));
			if (!readModelFile(values.value("--joint-map"), &bytes, &error) || !parseModelJointMapJson(bytes, &o.joints, &error))
				return fail(2, error);
		}
	}
	else
	{
		if (!copyModelPose(mesh, copy, &clipboard, &error))
			return fail(4, error);
		o.clipboard = modelPoseClipboardBytes(clipboard);
	}
	if (!document.edit(edit, &error))
		return fail(4, error);
	const auto target = inspectModelWriteTarget(output);
	if (!target.isValid())
		return fail(1, target.error);
	if (target.existed && !seen.contains("--overwrite"))
		return fail(1, Text::tr("The output exists; use --overwrite to replace it."));
	if (!document.save(output, seen.contains("--overwrite"), &error, {}, dry))
		return fail(1, error);
	const QStringList axes{"none", "x", "y", "z"};
	result.lines << (dry ? Text::tr("Pose paste validated; no files written.") : Text::tr("Saved the pasted pose to %1.").arg(output))
				 << modelPoseClipboardSummary(clipboard);
	const int pairCount = int(o.mirrorJoints.size()) / 2;
	if (o.mirrorAxis >= 0)
		result.lines << Text::tr("Mirrored across %1 = %2 with %n joint pair(s).", nullptr, pairCount)
							.arg(axes[o.mirrorAxis + 1].toUpper())
							.arg(o.plane);
	QJsonObject pairs;
	for (auto it = o.mirrorJoints.constBegin(); it != o.mirrorJoints.constEnd(); ++it)
		pairs.insert(it.key(), it.value());
	result.payload = {{"source", QFileInfo(input).absoluteFilePath()},
					  {"operation", operation},
					  {"kind", clipboard.kind == ModelPoseClipboardKind::Keys ? "keys" : "pose"},
					  {"clip", o.clip},
					  {"first", o.first},
					  {"samples", clipboard.samples},
					  {"joints", QJsonArray::fromStringList(clipboard.joints)},
					  {"mirror", axes[o.mirrorAxis + 1]},
					  {"plane", o.plane},
					  {"mirrorJoints", pairs},
					  {"replaceKeys", o.replaceKeys},
					  {"outputPath", QFileInfo(output).absoluteFilePath()},
					  {"written", !dry},
					  {"dryRun", dry}};
	result.payload.insert("outputSource", modelSourceSaveJson(document));
	if (const auto packed = modelSourceSaveNotice(document); !packed.isEmpty())
		result.lines << packed;
	return result;
}
} // namespace
ModelToolsCliResult runModelPose(const QStringList &arguments)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite", "--keys", "--replace-keys", "--retarget",
							  "--fill-from-bind"};
	const QSet<QString> options{"--input",	"--output", "--operation", "--joint",	 "--name",			"--clip",		  "--frames",
								"--fps",	"--loop",	"--first",	   "--last",	 "--source-clip",	"--source-frame", "--translate",
								"--rotate", "--scale", "--matrix", "--space", "--children", "--clips", "--settings-file", "--locale", "--catalog-root",
								"--clipboard", "--joints", "--channels", "--mirror", "--plane", "--mirror-joints", "--joint-map",
								"--source", "--root-scale", "--copy-translation"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	for (int i = 1; i < arguments.size(); ++i)
	{
		const auto argument = arguments[i];
		if (!argument.startsWith('-'))
		{
			positional.append(argument);
			continue;
		}
		const int equal = argument.indexOf('=');
		const auto key = equal < 0 ? argument : argument.left(equal);
		if (seen.contains(key))
			return fail(2, Text::tr("Repeated option: %1.").arg(key));
		seen.insert(key);
		if (flags.contains(key) && equal < 0)
			continue;
		if (!options.contains(key))
			return fail(2, Text::tr("Unknown or invalid option: %1.").arg(key));
		const auto value = equal < 0 ? arguments.value(++i) : argument.mid(equal + 1);
		if (value.isEmpty() || value.startsWith("--"))
			return fail(2, Text::tr("Option %1 requires a value.").arg(key));
		values.insert(key, value);
	}
	if (positional.size() != (seen.contains("--input") ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "pose")
		return fail(2, Text::tr("Provide one editable model source, positionally or with --input."));
	const auto input = seen.contains("--input") ? values.value("--input") : positional[2];
	const auto operation = values.value("--operation", "list");
	const QHash<QString, ModelEditKind> kinds{
		{"bind", ModelEditKind::TransformBindJoint},		 {"add", ModelEditKind::AddSkeletalClip},
		{"duplicate", ModelEditKind::DuplicateSkeletalClip}, {"rename", ModelEditKind::RenameSkeletalClip},
		{"remove", ModelEditKind::DeleteSkeletalClip},		 {"resize", ModelEditKind::ResizeSkeletalClip},
		{"timing", ModelEditKind::SetSkeletalClipTiming},	 {"transform", ModelEditKind::TransformSkeletalPose},
		{"copy", ModelEditKind::CopySkeletalPose},			 {"interpolate", ModelEditKind::InterpolateSkeletalPoses},
		{"working-set", ModelEditKind::SetSkeletalWorkingSet}};
	const bool list = operation == "list", add = operation == "add", bind = operation == "bind", transform = operation == "transform";
	const bool workingSet = operation == "working-set";
	// Pose clipboard (core/model_pose_clipboard.h): copy-out writes a clipboard
	// file, paste applies one, mirror copies a range and pastes it mirrored.
	const bool copyOut = operation == "copy-out", paste = operation == "paste", mirrorRange = operation == "mirror";
	// Clip import (core/model_clip_import.h): another file's clip, exact or retargeted.
	const bool import = operation == "import";
	if (!list && !kinds.contains(operation) && !copyOut && !paste && !mirrorRange && !import)
		return fail(2, Text::tr("Choose list, bind, add, duplicate, rename, remove, resize, timing, transform, copy, interpolate, working-set, "
								"copy-out, paste, mirror or import."));
	QSet<QString> required;
	if (!list)
		required.insert("--output");
	if (!list && !add && !bind && !workingSet && !import)
		required.insert("--clip");
	if (import)
		required.insert("--source");
	if (paste)
		required.unite({"--clipboard", "--first"});
	if (mirrorRange)
		required.unite({"--first", "--last"});
	if (add || operation == "duplicate" || operation == "rename")
		required.insert("--name");
	if (add || operation == "resize")
		required.insert("--frames");
	if (add || operation == "timing")
		required.insert("--fps");
	if (transform || operation == "copy" || operation == "interpolate")
		required.unite({"--first", "--last"});
	if (bind || transform)
		required.insert("--joint");
	if (operation == "copy")
		required.unite({"--source-clip", "--source-frame"});
	QSet<QString> allowed = required;
	allowed.unite({"--input", "--operation", "--settings-file", "--locale", "--catalog-root"});
	if (list)
		allowed.unite({"--clip", "--first"});
	if (add || operation == "timing")
		allowed.insert("--loop");
	if (bind || transform)
		allowed.unite({"--translate", "--rotate", "--scale", "--matrix", "--space", "--children"});
	if (workingSet)
		allowed.insert("--clips");
	if (copyOut)
		allowed.unite({"--first", "--last", "--joints", "--keys", "--channels"});
	if (paste)
		allowed.unite({"--joint-map", "--mirror", "--plane", "--mirror-joints", "--replace-keys"});
	if (mirrorRange)
		allowed.unite({"--joints", "--mirror", "--plane", "--mirror-joints", "--replace-keys"});
	if (import)
		allowed.unite({"--source-clip", "--joint-map", "--retarget", "--root-scale", "--fill-from-bind", "--copy-translation", "--name", "--fps",
					   "--loop"});
	for (const auto &key : required)
		if (!seen.contains(key))
			return fail(2, Text::tr("Operation %1 requires %2.").arg(operation, key));
	for (const auto &key : seen)
		if ((!flags.contains(key) && !allowed.contains(key)) || (list && (key == "--dry-run" || key == "--overwrite")) ||
			((key == "--keys" || key == "--replace-keys" || key == "--retarget" || key == "--fill-from-bind") && !allowed.contains(key)))
			return fail(2, Text::tr("Option %1 does not apply to %2.").arg(key, operation));
	if (list && seen.contains("--clip") != seen.contains("--first"))
		return fail(2, Text::tr("Pose inspection requires --clip and --first together."));
	if (!list && !copyOut && !values.value("--output").endsWith(".mesh.json", Qt::CaseInsensitive))
		return fail(2, Text::tr("Pose edits require an output ending in .mesh.json."));
	if (copyOut || paste || mirrorRange)
		return runModelPoseClipboard(input, operation, seen, values);
	if (import)
		return runModelPoseImport(input, seen, values);
	ModelEdit edit;
	auto &o = edit.skeletal;
	o.joint = values.value("--joint");
	o.name = values.value("--name");
	if (seen.contains("--matrix"))
	{
		for (const auto &key : {"--translate", "--rotate", "--scale", "--space"})
			if (seen.contains(key)) return fail(2, Text::tr("Matrix deltas cannot be combined with numeric transforms or --space."));
		const auto parts = values.value("--matrix").split(',');
		if (parts.size() != 12) return fail(2, Text::tr("A matrix delta requires twelve row-major, comma-separated numbers."));
		for (int i = 0; i < 12; ++i)
		{
			bool ok = false;
			o.delta.m[i] = parts[i].toFloat(&ok);
			if (!ok) return fail(2, Text::tr("Matrix coordinates must be finite numbers."));
		}
		QString error;
		if (!validModelJointDelta(o.delta, &error)) return fail(2, error);
		o.matrixTransform = true;
	}
	for (const auto &field : {qMakePair("--clip", &o.clip), qMakePair("--first", &o.first), qMakePair("--last", &o.last),
							  qMakePair("--frames", &o.frameCount), qMakePair("--source-frame", &o.sourceFrame)})
		if (seen.contains(field.first) && !integer(values.value(field.first), field.second))
			return fail(2, Text::tr("Option %1 requires a nonnegative integer.").arg(field.first));
	if (seen.contains("--source-clip") && values.value("--source-clip") != "bind" && !integer(values.value("--source-clip"), &o.sourceClip))
		return fail(2, Text::tr("Source clip must be an index or bind."));
	for (const auto &field : {qMakePair("--loop", &o.loops), qMakePair("--children", &o.descendants)})
		if (seen.contains(field.first))
		{
			const auto value = values.value(field.first);
			if (value != "yes" && value != "no")
				return fail(2, Text::tr("Option %1 requires yes or no.").arg(field.first));
			*field.second = value == "yes";
		}
	if (seen.contains("--space"))
	{
		const auto value = values.value("--space");
		if (value != "model" && value != "local")
			return fail(2, Text::tr("Space must be model or local."));
		o.local = value == "local";
	}
	if (seen.contains("--fps"))
	{
		bool ok = false;
		o.rate = values.value("--fps").toDouble(&ok);
		if (!ok || !std::isfinite(o.rate) || o.rate < .001 || o.rate > 1000)
			return fail(2, Text::tr("FPS must be from 0.001 to 1000."));
	}
	for (const auto &field : {qMakePair("--translate", &o.translation), qMakePair("--rotate", &o.rotation), qMakePair("--scale", &o.scale)})
		if (seen.contains(field.first))
		{
			const auto parts = values.value(field.first).split(',');
			if (parts.size() != 3)
				return fail(2, Text::tr("Option %1 requires three comma-separated numbers.").arg(field.first));
			float *axes[]{&field.second->x, &field.second->y, &field.second->z};
			for (int i = 0; i < 3; ++i)
			{
				bool ok = false;
				*axes[i] = parts[i].toFloat(&ok);
				if (!ok || !std::isfinite(*axes[i]))
					return fail(2, Text::tr("Transform coordinates must be finite numbers."));
			}
		}
	ModelDocument document;
	QString error;
	if (!document.load(input, &error))
		return fail(1, error);
	const bool dry = seen.contains("--dry-run");
	if (workingSet)
	{
		// Without --clips, every clip that fits the automatic budget, in order;
		// "-" keeps only the bind pose.
		const auto &mesh = document.mesh();
		if (mesh.skeleton.isEmpty())
			return fail(4, Text::tr("The model has no skeleton, so it has no clips for a working set."));
		if (!seen.contains("--clips"))
			o.workingSet = planModelSkeletalWorkingSet(mesh, nullptr, modelSkeletalAutomaticBudget()).clips;
		else if (values.value("--clips") != "-" &&
				 !modelSkeletalClipIndexes(mesh.skeleton, values.value("--clips").split(','), &o.workingSet, &error))
			return fail(2, error);
	}
	if (!list)
	{
		edit.kind = kinds[operation];
		if (!document.edit(edit, &error))
			return fail(4, error);
		const auto target = inspectModelWriteTarget(values.value("--output"));
		if (!target.isValid())
			return fail(1, target.error);
		if (target.existed && !seen.contains("--overwrite"))
			return fail(1, Text::tr("The output exists; use --overwrite to replace it."));
		if (!document.save(values.value("--output"), seen.contains("--overwrite"), &error, {}, dry))
			return fail(1, error);
	}
	const auto &skeleton = document.mesh().skeleton;
	QJsonArray clips, joints;
	ModelToolsCliResult result;
	for (int i = 0; i < skeleton.clips.size(); ++i)
	{
		const auto &clip = skeleton.clips[i];
		const int first = skeleton.bakedClipForFrame.indexOf(i);
		clips.append(QJsonObject{{"index", i},
								 {"name", clip.name},
								 {"frames", int(clip.frames.size())},
								 {"fps", clip.framesPerSecond},
								 {"loops", clip.loops},
								 {"previewFirst", first},
								 {"inWorkingSet", first >= 0}});
		result.lines << Text::tr("%1: %2 · %3 frames · %4 FPS · loop %5 · preview first %6")
							.arg(i)
							.arg(clip.name)
							.arg(clip.frames.size())
							.arg(clip.framesPerSecond)
							.arg(clip.loops ? Text::tr("yes") : Text::tr("no"))
							.arg(first);
	}
	// The clips baked into editable frames, the budget they use, and why the
	// others are left out.
	const auto workingSetPlan = modelSkeletalWorkingSet(document.mesh());
	if (!skeleton.isEmpty())
		result.lines << modelSkeletalWorkingSetLines(workingSetPlan);
	auto matrices = modelSkeletonBindPose(skeleton);
	const bool inspectPose = list && seen.contains("--clip");
	if (inspectPose)
	{
		if (o.clip >= skeleton.clips.size() || o.first >= skeleton.clips[o.clip].frames.size())
			return fail(4, Text::tr("The inspected clip/frame does not exist."));
		matrices = skeleton.clips[o.clip].frames[o.first];
	}
	result.lines << (inspectPose ? Text::tr("Joint poses: clip %1, pose %2.").arg(o.clip).arg(o.first)
								 : Text::tr("Joint poses: bind pose."));
	for (int i = 0; i < matrices.size(); ++i)
	{
		QJsonArray matrix;
		for (float value : matrices[i].m)
			matrix.append(value);
		joints.append(QJsonObject{{"index", i}, {"name", skeleton.joints[i].name}, {"matrix", matrix}});
		const auto position = modelJointTranslation(matrices[i]);
		result.lines << Text::tr("Joint %1: %2 · origin %3, %4, %5")
							.arg(i)
							.arg(skeleton.joints[i].name)
							.arg(position.x)
							.arg(position.y)
							.arg(position.z);
	}
	result.payload = {{"source", QFileInfo(input).absoluteFilePath()},
					  {"operation", operation},
					  {"clips", clips},
					  {"pose", joints},
					  {"poseSpace", "model"},
					  {"poseClip", inspectPose ? o.clip : -1},
					  {"poseFrame", inspectPose ? o.first : 0},
					  {"workingSet", modelSkeletalWorkingSetJson(workingSetPlan)},
					  {"written", !list && !dry},
					  {"dryRun", dry}};
	if (!list)
	{
		result.payload.insert("outputPath", QFileInfo(values.value("--output")).absoluteFilePath());
		result.lines.prepend(dry ? Text::tr("Skeletal edit validated; no files written.")
								 : Text::tr("Saved skeletal edit to %1.").arg(values.value("--output")));
		result.payload.insert("outputSource", modelSourceSaveJson(document));
		if (const auto packed = modelSourceSaveNotice(document); !packed.isEmpty())
			result.lines << packed;
	}
	return result;
}
} // namespace vibestudio::cli
