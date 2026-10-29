#include "core/model_pose_clipboard.h"
#include "core/model_curve_key_tools.h"
#include "core/model_animation_constraints.h"
#include "core/model_document.h"
#include "core/model_skeleton.h"
#include "core/model_vertex_origins.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelPoseClipboard)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
QJsonArray matrixJson(const ModelJointMatrix &matrix)
{
	QJsonArray values;
	for (float value : matrix.m)
		values.append(double(value));
	return values;
}
bool readMatrix(const QJsonValue &value, ModelJointMatrix *matrix)
{
	const auto values = value.toArray();
	if (values.size() != 12)
		return false;
	for (int i = 0; i < 12; ++i)
	{
		if (!values.at(i).isDouble() || !std::isfinite(values.at(i).toDouble()) || std::abs(values.at(i).toDouble()) > 1e9)
			return false;
		matrix->m[i] = float(values.at(i).toDouble());
	}
	return true;
}
int groupIndex(const QVector<ModelPoseCurves> &groups, const QString &joint)
{
	for (int i = 0; i < groups.size(); ++i)
		if (groups[i].joint == joint)
			return i;
	return -1;
}
QString fingerprint(const QStringList &names, const QStringList &parents)
{
	QCryptographicHash hash(QCryptographicHash::Sha1);
	for (int i = 0; i < names.size(); ++i)
	{
		hash.addData(names[i].toUtf8());
		hash.addData(QByteArrayView("\x1f", 1));
		hash.addData(parents[i].toUtf8());
		hash.addData(QByteArrayView("\x1e", 1));
	}
	return QString::fromLatin1(hash.result().toHex());
}
bool local(const ModelSkeleton &skeleton, const QVector<ModelJointMatrix> &world, int joint, ModelJointMatrix *result)
{
	const int parent = skeleton.joints[joint].parent;
	if (parent < 0)
	{
		*result = world[joint];
		return true;
	}
	bool ok = false;
	const auto inverse = modelJointInverse(world[parent], &ok);
	*result = modelJointMultiply(inverse, world[joint]);
	return ok;
}
// True when the matrix is translation, rotation and positive scale only.
bool rigid(const ModelJointMatrix &matrix)
{
	const auto rebuilt = modelJointMatrix(modelJointRotation(matrix), modelJointTranslation(matrix), modelJointScale(matrix));
	double largest = 1;
	for (float value : matrix.m)
		largest = std::max(largest, double(std::abs(value)));
	return modelJointMatrixIsFinite(matrix) && modelJointMatrixDistance(matrix, rebuilt) <= .0001 * largest;
}
ModelJointMatrix reflection(int axis, double plane)
{
	auto result = modelJointIdentity();
	result.m[axis * 5] = -1;
	result.m[axis * 4 + 3] = float(2 * plane);
	return result;
}
double axisValue(const ModelJointMatrix &matrix, int axis)
{
	return matrix.m[axis * 4 + 3];
}
QString kindId(ModelPoseClipboardKind kind)
{
	return kind == ModelPoseClipboardKind::Keys ? QStringLiteral("keys") : QStringLiteral("pose");
}
} // namespace

QString modelRigFingerprint(const ModelSkeleton &skeleton)
{
	QStringList names, parents;
	for (const auto &joint : skeleton.joints)
	{
		names.append(joint.name);
		parents.append(joint.parent >= 0 && joint.parent < skeleton.joints.size() ? skeleton.joints[joint.parent].name : QString());
	}
	return fingerprint(names, parents);
}
QString modelPoseClipboardFingerprint(const ModelPoseClipboard &clipboard)
{
	QStringList names, parents;
	for (const auto &joint : clipboard.rig)
	{
		names.append(joint.name);
		parents.append(joint.parent);
	}
	return fingerprint(names, parents);
}

bool copyModelPose(const ModelMesh &mesh, const ModelPoseCopyOptions &o, ModelPoseClipboard *result, QString *error,
				   const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!result || !work.check())
		return false;
	const auto &skeleton = mesh.skeleton;
	if (skeleton.isEmpty())
		return fail(error, Text::tr("Copying a pose needs a model with joints."));
	if (!validateModelSkeleton(mesh, error, control))
		return false;
	if (o.clip < -1 || o.clip >= skeleton.clips.size() || (o.keys && o.clip < 0))
		return fail(error, Text::tr("Choose an existing skeletal clip, or the bind pose for a pose copy."));
	const int frames = o.clip < 0 ? 1 : int(skeleton.clips[o.clip].frames.size());
	if (o.first < 0 || o.last < o.first || o.last >= frames)
		return fail(error, Text::tr("Choose an inclusive sample range inside the clip."));
	QSet<QString> wanted;
	for (const auto &name : o.joints)
	{
		if (wanted.contains(name) || modelJointIndex(skeleton, name, Qt::CaseSensitive) < 0)
			return fail(error, Text::tr("Copied joints must be distinct exact joint names from this rig."));
		wanted.insert(name);
	}
	QSet<int> channelSet;
	for (int channel : o.channels)
	{
		if (!o.keys || channel < 0 || channel >= 9 || channelSet.contains(channel))
			return fail(error, Text::tr("Channels apply only to key copies and must be distinct curve channels."));
		channelSet.insert(channel);
	}
	ModelPoseClipboard copy;
	copy.kind = o.keys ? ModelPoseClipboardKind::Keys : ModelPoseClipboardKind::Pose;
	copy.clip = o.clip < 0 ? QString() : skeleton.clips[o.clip].name;
	copy.first = o.first;
	copy.framesPerSecond = o.clip < 0 ? 0 : skeleton.clips[o.clip].framesPerSecond;
	copy.samples = o.last - o.first + 1;
	QVector<int> joints;
	for (int j = 0; j < skeleton.joints.size(); ++j)
	{
		const auto &joint = skeleton.joints[j];
		copy.rig.append({joint.name, joint.parent >= 0 ? skeleton.joints[joint.parent].name : QString(), joint.bind});
		if (wanted.isEmpty() || wanted.contains(joint.name))
		{
			joints.append(j);
			copy.joints.append(joint.name);
		}
	}
	if (qint64(joints.size()) * copy.samples > modelPoseClipboardMaxMatrices)
		return fail(error, Text::tr("The copy would exceed 1,048,576 joint samples. Copy fewer joints or samples."));
	if (!o.keys)
	{
		const auto bind = modelSkeletonBindPose(skeleton);
		for (int f = o.first; f <= o.last; ++f)
		{
			const auto &world = o.clip < 0 ? bind : skeleton.clips[o.clip].frames[f];
			QVector<ModelJointMatrix> sample, model;
			for (int j : joints)
			{
				if (!work.step())
					return false;
				ModelJointMatrix matrix;
				if (!local(skeleton, world, j, &matrix))
					return fail(error, Text::tr("Joint %1 has a singular parent pose in sample %2 and cannot be copied.")
										   .arg(skeleton.joints[j].name)
										   .arg(f));
				sample.append(matrix);
				model.append(world[j]);
			}
			copy.local.append(sample);
			copy.world.append(model);
		}
	}
	else
	{
		for (int c = 0; c < 9; ++c)
			if (channelSet.isEmpty() || channelSet.contains(c))
				copy.channels.append(c);
		const auto &groups = skeleton.clips[o.clip].curves;
		for (int j : joints)
		{
			if (!work.check())
				return false;
			ModelPoseCurves group;
			const int index = groupIndex(groups, skeleton.joints[j].name);
			if (index >= 0)
				group = groups[index];
			else if (!captureModelPoseCurves(mesh, o.clip, skeleton.joints[j].name, &group, error, control))
				return false;
			ModelPoseCurves sliced;
			sliced.joint = group.joint;
			for (int c = 0; c < 9; ++c)
				if (!sliceModelCurve(group.channels[c], o.first, o.last, 0, 1, false, copy.samples, &sliced.channels[c], error, control))
					return false;
			copy.curves.append(sliced);
		}
	}
	if (!work.check())
		return false;
	*result = std::move(copy);
	return true;
}

QByteArray modelPoseClipboardBytes(const ModelPoseClipboard &clipboard)
{
	QJsonArray rig, joints, poses, worlds, curves, channels;
	for (const auto &joint : clipboard.rig)
		rig.append(QJsonObject{{QStringLiteral("name"), joint.name}, {QStringLiteral("parent"), joint.parent}, {QStringLiteral("bind"), matrixJson(joint.bind)}});
	for (const auto &joint : clipboard.joints)
		joints.append(joint);
	for (const auto &sample : clipboard.local)
	{
		QJsonArray matrices;
		for (const auto &matrix : sample)
			matrices.append(matrixJson(matrix));
		poses.append(matrices);
	}
	for (const auto &sample : clipboard.world)
	{
		QJsonArray matrices;
		for (const auto &matrix : sample)
			matrices.append(matrixJson(matrix));
		worlds.append(matrices);
	}
	for (const auto &group : clipboard.curves)
		curves.append(modelPoseCurvesJson(group));
	for (int channel : clipboard.channels)
		channels.append(modelCurveChannelId(channel));
	QJsonObject root{{QStringLiteral("schema"), QStringLiteral("vibestudio.pose-clipboard")},
					 {QStringLiteral("version"), modelPoseClipboardVersion},
					 {QStringLiteral("kind"), kindId(clipboard.kind)},
					 {QStringLiteral("rig"), rig},
					 {QStringLiteral("fingerprint"), modelPoseClipboardFingerprint(clipboard)},
					 {QStringLiteral("clip"), clipboard.clip},
					 {QStringLiteral("first"), clipboard.first},
					 {QStringLiteral("framesPerSecond"), clipboard.framesPerSecond},
					 {QStringLiteral("samples"), clipboard.samples},
					 {QStringLiteral("joints"), joints}};
	if (clipboard.kind == ModelPoseClipboardKind::Pose)
	{
		root.insert(QStringLiteral("poses"), poses);
		root.insert(QStringLiteral("world"), worlds);
	}
	else
	{
		root.insert(QStringLiteral("curves"), curves);
		root.insert(QStringLiteral("channels"), channels);
	}
	return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool parseModelPoseClipboard(const QByteArray &bytes, ModelPoseClipboard *result, QString *error)
{
	const auto malformed = [error] {
		return fail(error, Text::tr("The clipboard does not hold a VibeStudio pose or key copy of a supported version."));
	};
	if (!result || bytes.size() > modelPoseClipboardMaxBytes)
		return malformed();
	QJsonParseError parse;
	const auto document = QJsonDocument::fromJson(bytes, &parse);
	if (parse.error != QJsonParseError::NoError || !document.isObject())
		return malformed();
	const auto root = document.object();
	if (root.value(QStringLiteral("schema")).toString() != QStringLiteral("vibestudio.pose-clipboard") ||
		root.value(QStringLiteral("version")) != QJsonValue(modelPoseClipboardVersion))
		return malformed();
	ModelPoseClipboard copy;
	const auto kind = root.value(QStringLiteral("kind")).toString();
	if (kind != QStringLiteral("pose") && kind != QStringLiteral("keys"))
		return malformed();
	copy.kind = kind == QStringLiteral("keys") ? ModelPoseClipboardKind::Keys : ModelPoseClipboardKind::Pose;
	const auto rig = root.value(QStringLiteral("rig")).toArray();
	if (rig.isEmpty() || rig.size() > 4096)
		return malformed();
	QHash<QString, int> index;
	for (const auto &value : rig)
	{
		const auto object = value.toObject();
		ModelPoseClipboardJoint joint;
		joint.name = object.value(QStringLiteral("name")).toString();
		joint.parent = object.value(QStringLiteral("parent")).toString();
		if (joint.name.isEmpty() || index.contains(joint.name) || (!joint.parent.isEmpty() && !index.contains(joint.parent)) ||
			!readMatrix(object.value(QStringLiteral("bind")), &joint.bind))
			return malformed();
		index.insert(joint.name, int(copy.rig.size()));
		copy.rig.append(joint);
	}
	copy.clip = root.value(QStringLiteral("clip")).toString();
	copy.first = root.value(QStringLiteral("first")).toInt(-1);
	copy.framesPerSecond = root.value(QStringLiteral("framesPerSecond")).toDouble(-1);
	copy.samples = root.value(QStringLiteral("samples")).toInt(0);
	if (copy.first < 0 || !std::isfinite(copy.framesPerSecond) || copy.framesPerSecond < 0 || copy.samples < 1 || copy.samples > 1048576)
		return malformed();
	int previous = -1;
	for (const auto &value : root.value(QStringLiteral("joints")).toArray())
	{
		const int at = index.value(value.toString(), -1);
		if (at <= previous)
			return malformed();
		previous = at;
		copy.joints.append(value.toString());
	}
	if (copy.joints.isEmpty() || qint64(copy.joints.size()) * copy.samples > modelPoseClipboardMaxMatrices)
		return malformed();
	if (copy.kind == ModelPoseClipboardKind::Pose)
	{
		for (const auto &field : {qMakePair(QStringLiteral("poses"), &copy.local), qMakePair(QStringLiteral("world"), &copy.world)})
		{
			const auto poses = root.value(field.first).toArray();
			if (poses.size() != copy.samples)
				return malformed();
			for (const auto &value : poses)
			{
				const auto matrices = value.toArray();
				if (matrices.size() != copy.joints.size())
					return malformed();
				QVector<ModelJointMatrix> sample(matrices.size());
				for (int i = 0; i < matrices.size(); ++i)
					if (!readMatrix(matrices.at(i), &sample[i]))
						return malformed();
				field.second->append(sample);
			}
		}
	}
	else
	{
		const auto curves = root.value(QStringLiteral("curves")).toArray();
		if (curves.size() != copy.joints.size())
			return malformed();
		for (int i = 0; i < curves.size(); ++i)
		{
			ModelPoseCurves group;
			QString problem;
			if (!modelPoseCurvesFromJson(curves.at(i).toObject(), copy.samples, &group, &problem) || group.joint != copy.joints[i])
				return malformed();
			copy.curves.append(group);
		}
		QSet<int> seen;
		for (const auto &value : root.value(QStringLiteral("channels")).toArray())
		{
			const int channel = modelCurveChannelIndex(value.toString());
			if (channel < 0 || seen.contains(channel))
				return malformed();
			seen.insert(channel);
			copy.channels.append(channel);
		}
		if (copy.channels.isEmpty())
			return malformed();
		std::sort(copy.channels.begin(), copy.channels.end());
	}
	*result = std::move(copy);
	return true;
}

QString modelPoseClipboardSummary(const ModelPoseClipboard &clipboard)
{
	const int joints = int(clipboard.joints.size());
	const int samples = clipboard.samples;
	const auto what = clipboard.kind == ModelPoseClipboardKind::Keys ? Text::tr("Keys of %n joint(s)", nullptr, joints)
																	 : Text::tr("Pose of %n joint(s)", nullptr, joints);
	const auto range = Text::tr("%n sample(s)", nullptr, samples);
	return clipboard.clip.isEmpty() ? Text::tr("%1 from the bind pose, %2.").arg(what, range)
									: Text::tr("%1 from clip %2, %3.").arg(what, clipboard.clip, range);
}

int detectModelPoseMirrorAxis(const ModelMesh &mesh, QMap<QString, QString> *pairs)
{
	const auto detected = detectModelMirrorJointPairs(mesh.skeleton);
	int best = -1;
	QMap<QString, QString> bestPairs;
	for (int axis = 0; axis < 3; ++axis)
	{
		const auto candidate = modelMirrorJointPairsForAxis(mesh, detected, axis, -1, .001);
		if (candidate.size() > bestPairs.size())
		{
			best = axis;
			bestPairs = candidate;
		}
	}
	if (pairs)
		*pairs = bestPairs;
	return best;
}

bool parseModelJointMapJson(const QByteArray &bytes, QMap<QString, QString> *map, QString *error)
{
	QJsonParseError parse;
	const auto document = QJsonDocument::fromJson(bytes, &parse);
	if (bytes.size() > 65536 || parse.error != QJsonParseError::NoError || !document.isObject())
		return fail(error, Text::tr("A joint map is a JSON object of at most 64 KiB mapping exact source names to destination names."));
	QMap<QString, QString> result;
	const auto object = document.object();
	for (auto it = object.constBegin(); it != object.constEnd(); ++it)
	{
		if (it.key().isEmpty() || !it.value().isString() || it.value().toString().isEmpty())
			return fail(error, Text::tr("Every joint mapping needs two nonempty names."));
		result.insert(it.key(), it.value().toString());
	}
	if (map)
		*map = result;
	return true;
}

bool applyModelPosePaste(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!mesh || !work.check())
		return false;
	const auto &o = edit.animation.paste;
	if (edit.kind != ModelEditKind::PastePose || !defaultModelSkeletalOptions(edit.skeletal))
		return fail(error, Text::tr("Paste Pose accepts only its own clipboard, destination and mirror settings."));
	if (mesh->skeleton.isEmpty())
		return fail(error, Text::tr("Pasting a pose needs a model with joints."));
	if (!validateModelSkeleton(*mesh, error, control))
		return false;
	ModelPoseClipboard clipboard;
	if (!parseModelPoseClipboard(o.clipboard, &clipboard, error))
		return false;
	const auto &skeleton = mesh->skeleton;
	if (o.clip < 0 || o.clip >= skeleton.clips.size())
		return fail(error, Text::tr("Choose an existing skeletal clip to paste into."));
	const int frames = int(skeleton.clips[o.clip].frames.size());
	if (o.first < 0 || qint64(o.first) + clipboard.samples > frames)
		return fail(error, Text::tr("The %n pasted sample(s) must fit inside clip %1, which has %2 samples. Extend the clip or paste earlier.",
									nullptr, clipboard.samples)
							   .arg(skeleton.clips[o.clip].name)
							   .arg(frames));
	const bool mirror = o.mirrorAxis >= 0;
	if (o.mirrorAxis < -1 || o.mirrorAxis > 2 || !std::isfinite(o.plane) || std::abs(o.plane) > 1000000 ||
		(!mirror && (o.plane != 0 || !o.mirrorJoints.isEmpty())))
		return fail(error, Text::tr("Mirror planes and joint pairs need a mirror axis of X, Y or Z."));
	if (mirror && clipboard.kind == ModelPoseClipboardKind::Keys)
		return fail(error, Text::tr("Mirrored paste applies to copied poses. Copy a pose, not keys, to mirror it."));
	if (o.joints.isEmpty() && modelPoseClipboardFingerprint(clipboard) != modelRigFingerprint(skeleton))
		return fail(error, Text::tr("The clipboard comes from a different skeleton. Supply a joint map from its joints to this rig's joints."));
	QHash<QString, int> sourceIndex;
	for (int i = 0; i < clipboard.rig.size(); ++i)
		sourceIndex.insert(clipboard.rig[i].name, i);
	for (auto it = o.joints.constBegin(); it != o.joints.constEnd(); ++it)
		if (!sourceIndex.contains(it.key()) || modelJointIndex(skeleton, it.value(), Qt::CaseSensitive) < 0)
			return fail(error, Text::tr("Joint map entry %1 = %2 must name a copied rig's joint and a joint of this rig exactly.").arg(it.key(), it.value()));
	QMap<QString, QString> pairs;
	if (mirror && !completeModelMirrorJointMap(skeleton, o.mirrorJoints, &pairs, error))
		return false;
	const auto mapped = [&](const QString &name) { return o.joints.value(name, name); };
	const auto partner = [&](const QString &name) { return mirror ? pairs.value(name, name) : name; };
	const auto bind = modelSkeletonBindPose(skeleton);
	double extent = 1;
	for (const auto &matrix : bind)
		for (int axis = 0; axis < 3; ++axis)
			extent = std::max(extent, std::abs(axisValue(matrix, axis)));
	const auto mirrorMatrix = mirror ? reflection(o.mirrorAxis, o.plane) : modelJointIdentity();
	// Per copied joint: the destination joint and the fixed matrices around its
	// copied local pose (identity unless mirrored).
	struct Target
	{
		int joint = -1;
		ModelJointMatrix before, after;
	};
	QVector<Target> targets;
	QSet<int> used;
	for (const auto &name : clipboard.joints)
	{
		const auto &source = clipboard.rig[sourceIndex.value(name)];
		const auto destinationName = partner(mapped(name));
		const int joint = modelJointIndex(skeleton, destinationName, Qt::CaseSensitive);
		if (joint < 0)
			return fail(error, Text::tr("This rig has no joint %1 to receive copied joint %2. Map it to an existing joint.").arg(destinationName, name));
		if (used.contains(joint))
			return fail(error, Text::tr("Two copied joints would paste onto joint %1. Map each copied joint to its own joint.").arg(destinationName));
		used.insert(joint);
		const int parent = skeleton.joints[joint].parent;
		const auto expectedParent = source.parent.isEmpty() ? QString() : partner(mapped(source.parent));
		const auto actualParent = parent < 0 ? QString() : skeleton.joints[parent].name;
		if (expectedParent != actualParent)
			return fail(error, Text::tr("Joint %1 has parent %2 here, but the copy expects %3. Paste onto a matching hierarchy or map the parents too.")
								   .arg(destinationName, actualParent.isEmpty() ? Text::tr("none") : actualParent,
										expectedParent.isEmpty() ? Text::tr("none") : expectedParent));
		Target target{joint, modelJointIdentity(), modelJointIdentity()};
		if (mirror)
		{
			if (destinationName == mapped(name) && std::abs(axisValue(bind[joint], o.mirrorAxis) - o.plane) > std::max(.01, extent * .0001))
				return fail(error, Text::tr("Joint %1 sits off the mirror plane and has no mirror partner. Pair it with its opposite joint.")
									   .arg(destinationName));
			// local' = (Bp'^-1 S Bp) local (B^-1 S B'): the copied motion relative
			// to its binds, reflected and carried into the partner's binds. With
			// mirror-image binds this is S M S in model space.
			const int sourceParent = source.parent.isEmpty() ? -1 : sourceIndex.value(source.parent);
			bool ok = true, invertible = false;
			const auto parentBind = parent < 0 ? modelJointIdentity() : modelJointInverse(bind[parent], &ok);
			const auto sourceBind = modelJointInverse(source.bind, &invertible);
			if (!ok || !invertible)
				return fail(error, Text::tr("Joint %1 has a singular bind pose and cannot be mirrored.").arg(destinationName));
			target.before = modelJointMultiply(parentBind, modelJointMultiply(mirrorMatrix, sourceParent < 0 ? modelJointIdentity() : clipboard.rig[sourceParent].bind));
			target.after = modelJointMultiply(sourceBind, modelJointMultiply(mirrorMatrix, bind[joint]));
		}
		targets.append(target);
	}
	// The pasted parent-local pose of each destination joint, per pasted sample.
	auto groups = skeleton.clips[o.clip].curves;
	const auto originalGroups = groups;
	QVector<QHash<int, ModelJointMatrix>> overrides(clipboard.samples);
	// Same rig, no map, no mirror: a joint whose parent chain is pasted takes
	// its copied model-space pose exactly.
	QVector<QHash<int, ModelJointMatrix>> exact(clipboard.samples);
	const bool sameRig = !mirror && o.joints.isEmpty();
	if (clipboard.kind == ModelPoseClipboardKind::Pose)
	{
		for (int i = 0; i < clipboard.samples; ++i)
			for (int t = 0; t < targets.size(); ++t)
			{
				if (!work.step())
					return false;
				const auto pasted = modelJointMultiply(targets[t].before, modelJointMultiply(clipboard.local[i][t], targets[t].after));
				if (!rigid(pasted))
					return fail(error, Text::tr("The pasted pose of joint %1 in sample %2 would have shear or a reflection. Check the mirror axis, "
												"plane and joint pairs for this rig.")
										   .arg(skeleton.joints[targets[t].joint].name)
										   .arg(o.first + i));
				const auto &name = skeleton.joints[targets[t].joint].name;
				const int group = groupIndex(groups, name);
				if (group < 0)
				{
					overrides[i].insert(targets[t].joint, pasted);
					const int parent = skeleton.joints[targets[t].joint].parent;
					if (sameRig && (parent < 0 || exact[i].contains(parent)))
						exact[i].insert(targets[t].joint, clipboard.world[i][t]);
					continue;
				}
				// A joint with curves receives a key on every channel instead.
				const int frame = o.first + i;
				std::array<double, 3> previous{};
				for (int c = 0; c < 3; ++c)
					previous[c] = evaluateModelCurve(groups[group].channels[3 + c], frame);
				std::array<double, 9> values{};
				if (!modelPoseCurveValues(pasted, &previous, &values))
					return fail(error, Text::tr("The pasted pose of joint %1 cannot be held by its curves (shear, reflection or zero scale).").arg(name));
				for (int c = 0; c < 9; ++c)
				{
					auto &keys = groups[group].channels[c];
					auto it = std::lower_bound(keys.begin(), keys.end(), frame, [](const ModelPoseCurveKey &k, int f) { return k.frame < f; });
					if (it != keys.end() && it->frame == frame)
					{
						if (!o.replaceKeys)
							return fail(error, Text::tr("Joint %1 already has a %2 key at sample %3. Choose to replace keys, or paste elsewhere.")
												   .arg(name, modelCurveChannelId(c))
												   .arg(frame));
						it->value = values[c];
						if (it->tangent == ModelCurveTangent::Free)
							it->tangent = ModelCurveTangent::Clamped;
					}
					else
					{
						ModelPoseCurveKey key;
						key.frame = frame;
						key.value = values[c];
						keys.insert(it, key);
					}
				}
			}
	}
	else
	{
		QVector<ModelPoseCurves> captured = groups;
		qint64 total = 0;
		for (const auto &clip : skeleton.clips)
			for (const auto &group : clip.curves)
				for (const auto &channel : group.channels)
					total += channel.size();
		const int end = o.first + clipboard.samples - 1;
		for (int t = 0; t < targets.size(); ++t)
		{
			if (!work.check())
				return false;
			const auto &name = skeleton.joints[targets[t].joint].name;
			int group = groupIndex(groups, name);
			const bool authored = group >= 0;
			if (!authored)
			{
				ModelPoseCurves sampled;
				if (!captureModelPoseCurves(*mesh, o.clip, name, &sampled, error, control))
					return false;
				group = int(groups.size());
				groups.append(sampled);
				captured.append(sampled);
				for (const auto &channel : sampled.channels)
					total += channel.size();
			}
			for (int c : clipboard.channels)
			{
				const auto &old = captured[group].channels[c];
				const bool inside = std::any_of(old.cbegin(), old.cend(), [&](const auto &key) { return key.frame >= o.first && key.frame <= end; });
				if (authored && inside && !o.replaceKeys)
					return fail(error, Text::tr("Joint %1 already has %2 keys inside samples %3–%4. Choose to replace keys, or paste elsewhere.")
										   .arg(name, modelCurveChannelId(c))
										   .arg(o.first)
										   .arg(end));
				QVector<ModelPoseCurveKey> copied;
				if (!sliceModelCurve(clipboard.curves[t].channels[c], 0, clipboard.samples - 1, o.first, 1, false, frames, &copied, error, control))
					return false;
				// Keep the destination's outward tangent sides, as range transfer does.
				copied.first().inSlope = sampleModelCurveKey(old, o.first).inSlope;
				const auto right = sampleModelCurveKey(old, end);
				copied.last().outSlope = right.outSlope;
				copied.last().interpolation = right.interpolation;
				QVector<ModelPoseCurveKey> merged;
				for (const auto &key : old)
				{
					if (!work.step())
						return false;
					if (key.frame < o.first || key.frame > end)
						merged.append(sampleModelCurveKey(old, key.frame));
				}
				merged += copied;
				std::sort(merged.begin(), merged.end(), [](const auto &a, const auto &b) { return a.frame < b.frame; });
				total += merged.size() - old.size();
				if (total > modelCurveMaxKeys)
					return fail(error, Text::tr("The pasted keys exceed the model's 1,048,576 key budget. Paste fewer joints or reduce curves first."));
				groups[group].channels[c] = std::move(merged);
			}
			if (!validateModelCurveGroup(groups[group], frames, error))
				return false;
		}
		if (!work.check())
			return false;
		// The shared rebake compares against the captured baseline so unchanged
		// sample matrices stay exact.
		mesh->skeleton.clips[o.clip].curves = std::move(captured);
		return replaceModelCurveGroups(mesh, o.clip, groups, error, control);
	}
	// Joints without curves take the pasted samples; their descendants follow.
	bool changed = false;
	auto samples = modelClipEditableSamples(skeleton.clips[o.clip]);
	for (int i = 0; i < clipboard.samples; ++i)
	{
		if (overrides[i].isEmpty())
			continue;
		auto &pose = samples[o.first + i];
		const auto before = pose;
		QVector<bool> moved(pose.size(), false);
		QVector<ModelJointMatrix> deltas(pose.size());
		for (int j = 0; j < pose.size(); ++j)
		{
			if (!work.step())
				return false;
			const int parent = skeleton.joints[j].parent;
			if (exact[i].contains(j))
				pose[j] = exact[i].value(j);
			else if (overrides[i].contains(j))
				pose[j] = parent < 0 ? overrides[i].value(j) : modelJointMultiply(pose[parent], overrides[i].value(j));
			else if (parent >= 0 && moved[parent])
				pose[j] = modelJointMultiply(deltas[parent], before[j]);
			moved[j] = modelJointMatrixDistance(pose[j], before[j]) != 0;
			if (moved[j])
			{
				bool ok = false;
				const auto inverse = modelJointInverse(before[j], &ok);
				if (!ok)
					return fail(error, Text::tr("An existing joint pose is singular and cannot move its descendants."));
				deltas[j] = modelJointMultiply(pose[j], inverse);
				changed = true;
			}
		}
	}
	if (changed && !replaceModelSkeletalSamples(mesh, o.clip, 0, samples, error, control))
		return false;
	if (groups != originalGroups && !replaceModelCurveGroups(mesh, o.clip, groups, error, control))
		return false;
	return work.check();
}
} // namespace vibestudio
