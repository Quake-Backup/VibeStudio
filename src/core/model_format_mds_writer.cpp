// The MDS writer (core/model_mds.h). The inverse of decodeMds in
// model_format_mds.cpp: see that file's comment for the bone maths the game
// applies (R_CalcBone, RB_SurfaceAnim in RTCW src/renderer/tr_animation.c).
#include "core/model_mds.h"

#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vibestudio {

namespace {

constexpr int kMaxBones = 128;           // MDS_MAX_BONES
constexpr qint64 kHeaderBytes = 120;     // mdsHeader_t
constexpr qint64 kFrameHeaderBytes = 52; // mdsFrame_t before its bones
constexpr qint64 kBoneFrameBytes = 12;   // mdsBoneFrameCompressed_t
constexpr qint64 kBoneInfoBytes = 80;    // mdsBoneInfo_t
constexpr qint64 kSurfaceBytes = 176;    // mdsSurface_t
constexpr qint64 kVertexBytes = 32;      // mdsVertex_t before its weights
constexpr qint64 kWeightBytes = 20;      // mdsWeight_t
constexpr qint64 kTagBytes = 72;         // mdsTag_t
constexpr quint32 kBoneFlagTag = 1;      // BONEFLAG_TAG
constexpr double kRadians = 3.14159265358979323846 / 180.0;

void putI32(QByteArray* bytes, qint32 value)
{
	char raw[4];
	qToLittleEndian(value, raw);
	bytes->append(raw, 4);
}
void putF32(QByteArray* bytes, float value)
{
	quint32 raw = 0;
	std::memcpy(&raw, &value, 4);
	putI32(bytes, qint32(raw));
}
void putI16(QByteArray* bytes, qint16 value)
{
	char raw[2];
	qToLittleEndian(value, raw);
	bytes->append(raw, 2);
}
void putVec(QByteArray* bytes, const ModelVec3& v)
{
	putF32(bytes, v.x);
	putF32(bytes, v.y);
	putF32(bytes, v.z);
}
void putName(QByteArray* bytes, const QString& name)
{
	QByteArray latin = name.toLatin1().left(63);
	latin.append(QByteArray(64 - latin.size(), '\0'));
	bytes->append(latin);
}
void setI32(QByteArray* bytes, qint64 at, qint32 value)
{
	qToLittleEndian(value, bytes->data() + at);
}

// ANGLE2SHORT, wrapped to a signed short as the file stores it.
qint16 angleToShort(double degrees)
{
	const int value = int(std::lround(degrees * 65536.0 / 360.0)) & 0xffff;
	return qint16(value >= 0x8000 ? value - 0x10000 : value);
}
double shortToAngle(qint16 value)
{
	return value * 360.0 / 65536.0;
}

struct Angles {
	double pitch = 0, yaw = 0, roll = 0;
};

// The (pitch, yaw, roll) whose AngleVectors give forward f, left l and up u
// (q_math.c: right = -left).
Angles anglesFromAxes(const double f[3], const double l[3], const double u[3])
{
	Angles a;
	a.pitch = std::asin(std::clamp(-f[2], -1.0, 1.0)) / kRadians;
	const double cp = std::cos(a.pitch * kRadians);
	if (std::abs(cp) > 1e-6) {
		a.yaw = std::atan2(f[1], f[0]) / kRadians;
		a.roll = std::atan2(l[2], u[2]) / kRadians;
	} else {
		// Looking straight up or down: yaw folds into roll.
		a.yaw = 0.0;
		const double sp = std::sin(a.pitch * kRadians);
		a.roll = std::atan2(l[0] / sp, l[1]) / kRadians;
	}
	return a;
}

bool fail(QString* error, const QString& message)
{
	if (error) {
		*error = message;
	}
	return false;
}

} // namespace

QByteArray exportModelMds(const ModelMesh& mesh, const ModelMdsExportOptions& options, QString* error, const ModelWorkControl& control,
	ModelExportReport* report)
{
	ModelExportReport result;
	const ModelSkeleton& skeleton = mesh.skeleton;
	const int boneCount = int(skeleton.joints.size());
	if (skeleton.isEmpty()) {
		fail(error, QCoreApplication::translate("VibeStudioModelMds", "An MDS needs a skeleton; this model animates by frames. Export MD3 instead."));
		return {};
	}
	if (boneCount > kMaxBones) {
		fail(error, QCoreApplication::translate("VibeStudioModelMds", "The skeleton has %1 bones; RTCW's renderer holds at most %2.").arg(boneCount).arg(kMaxBones));
		return {};
	}
	if (options.torsoParent < -1 || options.torsoParent >= boneCount) {
		fail(error, QCoreApplication::translate("VibeStudioModelMds", "The torso parent must be a bone of the skeleton."));
		return {};
	}
	ModelWorkProgress work(control, ModelWorkPhase::Serializing, error);
	// The frames: every clip in order, or the bind pose alone.
	QVector<QVector<ModelJointMatrix>> frames;
	for (const ModelSkeletalClip& clip : skeleton.clips) {
		frames += clip.frames;
	}
	if (frames.isEmpty()) {
		frames.append(modelSkeletonBindPose(skeleton));
	}
	// Weighted bones store their axes as the vertex matrix's rows; a tag bone
	// that moves no vertex keeps them as columns, as the decoder reads it.
	QVector<bool> weighted(boneCount, false), tagged(boneCount, false);
	for (const ModelSurface& surface : mesh.surfaces) {
		if (surface.skinning.isEmpty()) {
			fail(error, QCoreApplication::translate("VibeStudioModelMds", "Surface %1 does not follow the skeleton; every MDS vertex needs a bone.").arg(surface.name));
			return {};
		}
		for (int v = 0; v < surface.skinning.count.size(); ++v) {
			if (surface.skinning.count.at(v) > options.maxInfluences) {
				fail(error, QCoreApplication::translate("VibeStudioModelMds", "Vertex %1 of surface %2 has %3 bone weights; MDS export keeps at most %4.")
					.arg(v).arg(surface.name).arg(surface.skinning.count.at(v)).arg(options.maxInfluences));
				return {};
			}
		}
		for (const ModelJointInfluence& influence : surface.skinning.influences) {
			if (influence.joint >= 0 && influence.joint < boneCount) {
				weighted[influence.joint] = true;
			}
		}
	}
	for (const ModelSkeletalTag& tag : skeleton.tags) {
		if (tag.joint >= 0 && tag.joint < boneCount) {
			tagged[tag.joint] = true;
		}
	}
	QVector<bool> columnAxes(boneCount, false);
	for (int bone = 0; bone < boneCount; ++bone) {
		columnAxes[bone] = ((skeleton.joints.at(bone).flags & kBoneFlagTag) != 0 || tagged.at(bone)) && !weighted.at(bone);
	}

	// Per-bone constants: the parent distance, from the first frame.
	QVector<double> parentDist(boneCount, 0.0);
	for (int bone = 0; bone < boneCount; ++bone) {
		const int parent = skeleton.joints.at(bone).parent;
		if (parent >= 0) {
			const ModelVec3 a = modelJointTranslation(frames.first().at(bone)), b = modelJointTranslation(frames.first().at(parent));
			parentDist[bone] = std::sqrt(double(a.x - b.x) * (a.x - b.x) + double(a.y - b.y) * (a.y - b.y) + double(a.z - b.z) * (a.z - b.z));
		}
	}
	// Frame records, reconstructing every translation the way the game will so
	// rounding never accumulates down the hierarchy.
	QByteArray frameBlock;
	double worstStretch = 0.0, worstRotation = 0.0;
	for (int f = 0; f < frames.size(); ++f) {
		if (!work.step()) {
			return {};
		}
		const QVector<ModelJointMatrix>& pose = frames.at(f);
		ModelVec3 rootOffset{};
		bool rootSet = false;
		QVector<ModelVec3> rebuilt(boneCount);
		QByteArray bones;
		for (int bone = 0; bone < boneCount; ++bone) {
			const ModelJointMatrix& m = pose.at(bone);
			if (!modelJointMatrixIsFinite(m)) {
				fail(error, QCoreApplication::translate("VibeStudioModelMds", "Bone %1 has a pose that is not a finite number.").arg(skeleton.joints.at(bone).name));
				return {};
			}
			const ModelVec3 scale = modelJointScale(m);
			if (std::abs(scale.x - 1.0f) > 1e-3f || std::abs(scale.y - 1.0f) > 1e-3f || std::abs(scale.z - 1.0f) > 1e-3f) {
				fail(error, QCoreApplication::translate("VibeStudioModelMds", "Bone %1 scales in frame %2; MDS bones only rotate.").arg(skeleton.joints.at(bone).name).arg(f));
				return {};
			}
			// The axes AnglesToAxis must produce: the rows of the vertex
			// matrix, or its columns for a column-axes tag bone.
			double fwd[3], left[3], up[3];
			for (int i = 0; i < 3; ++i) {
				fwd[i] = columnAxes.at(bone) ? m.m[i * 4 + 0] : m.m[0 * 4 + i];
				left[i] = columnAxes.at(bone) ? m.m[i * 4 + 1] : m.m[1 * 4 + i];
				up[i] = columnAxes.at(bone) ? m.m[i * 4 + 2] : m.m[2 * 4 + i];
			}
			const Angles angles = anglesFromAxes(fwd, left, up);
			const qint16 pitch = angleToShort(angles.pitch), yaw = angleToShort(angles.yaw), roll = angleToShort(angles.roll);
			// How far rounding turned the bone: compare the rebuilt axes.
			const ModelJointMatrix check = modelJointFromQuakeAngles(float(shortToAngle(pitch)), float(shortToAngle(yaw)), float(shortToAngle(roll)), {});
			for (int i = 0; i < 3; ++i) {
				worstRotation = std::max(worstRotation, std::abs(double(check.m[i * 4 + 0]) - fwd[i]));
			}
			const ModelVec3 translation = modelJointTranslation(m);
			const int parent = skeleton.joints.at(bone).parent;
			qint16 offsetPitch = 0, offsetYaw = 0;
			if (parent < 0) {
				if (!rootSet) {
					rootOffset = translation;
					rootSet = true;
				} else if (std::abs(translation.x - rootOffset.x) > options.distanceTolerance || std::abs(translation.y - rootOffset.y) > options.distanceTolerance
					|| std::abs(translation.z - rootOffset.z) > options.distanceTolerance) {
					fail(error, QCoreApplication::translate("VibeStudioModelMds", "Root bones %1 and the first root sit apart in frame %2; MDS roots share one position.")
						.arg(skeleton.joints.at(bone).name).arg(f));
					return {};
				}
				rebuilt[bone] = rootOffset;
			} else {
				const ModelVec3 from = rebuilt.at(parent);
				const double d[3] = {double(translation.x) - from.x, double(translation.y) - from.y, double(translation.z) - from.z};
				const double distance = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
				const double stretch = std::abs(distance - parentDist.at(bone));
				worstStretch = std::max(worstStretch, stretch);
				if (stretch > std::max(options.distanceTolerance, parentDist.at(bone) * 1e-3)) {
					fail(error, QCoreApplication::translate("VibeStudioModelMds",
						"Bone %1 is %2 units from its parent in frame %3 but %4 in the first frame; MDS bones keep a fixed length.")
						.arg(skeleton.joints.at(bone).name, QString::number(distance, 'g', 6), QString::number(f), QString::number(parentDist.at(bone), 'g', 6)));
					return {};
				}
				if (parentDist.at(bone) > 1e-9 && distance > 1e-9) {
					offsetPitch = angleToShort(std::asin(std::clamp(-d[2] / distance, -1.0, 1.0)) / kRadians);
					offsetYaw = angleToShort(std::atan2(d[1], d[0]) / kRadians);
				}
				// LocalAngleVector of the stored offset angles, as R_CalcBone.
				const double op = shortToAngle(offsetPitch) * kRadians, oy = shortToAngle(offsetYaw) * kRadians;
				rebuilt[bone] = ModelVec3{float(from.x + parentDist.at(bone) * std::cos(op) * std::cos(oy)),
					float(from.y + parentDist.at(bone) * std::cos(op) * std::sin(oy)), float(from.z - parentDist.at(bone) * std::sin(op))};
			}
			putI16(&bones, pitch);
			putI16(&bones, yaw);
			putI16(&bones, roll);
			putI16(&bones, 0);
			putI16(&bones, offsetPitch);
			putI16(&bones, offsetYaw);
		}
		// Bounds from the posed vertices.
		ModelVec3 low{1e30f, 1e30f, 1e30f}, high{-1e30f, -1e30f, -1e30f};
		for (const ModelSurface& surface : mesh.surfaces) {
			ModelFrameGeometry posed;
			if (!skinModelSurface(surface, pose, &posed, error)) {
				return {};
			}
			for (const ModelVec3& p : posed.positions) {
				low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
				high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
			}
		}
		if (low.x > high.x) {
			low = high = rootOffset;
		}
		const ModelVec3 centre{(low.x + high.x) / 2, (low.y + high.y) / 2, (low.z + high.z) / 2};
		const ModelVec3 half{high.x - centre.x, high.y - centre.y, high.z - centre.z};
		putVec(&frameBlock, low);
		putVec(&frameBlock, high);
		putVec(&frameBlock, centre);
		putF32(&frameBlock, std::sqrt(half.x * half.x + half.y * half.y + half.z * half.z));
		putVec(&frameBlock, rootOffset);
		frameBlock.append(bones);
	}

	// Bone infos.
	QByteArray boneBlock;
	for (int bone = 0; bone < boneCount; ++bone) {
		const ModelJoint& joint = skeleton.joints.at(bone);
		putName(&boneBlock, joint.name);
		putI32(&boneBlock, joint.parent);
		putF32(&boneBlock, 0.0f);
		putF32(&boneBlock, float(parentDist.at(bone)));
		putI32(&boneBlock, qint32(joint.flags));
	}

	// Surfaces.
	QByteArray surfaceBlock;
	const qint64 surfaceStart = kHeaderBytes + frameBlock.size() + boneBlock.size();
	const QVector<ModelJointMatrix> bind = modelSkeletonBindPose(skeleton);
	const int bindFrame = modelBindPoseFrame(mesh);
	for (const ModelSurface& surface : mesh.surfaces) {
		if (!work.step()) {
			return {};
		}
		QByteArray vertices;
		QSet<int> referenced;
		for (int v = 0; v < surface.vertexCount; ++v) {
			const int first = surface.skinning.first.value(v), count = surface.skinning.count.value(v);
			if (count < 1) {
				fail(error, QCoreApplication::translate("VibeStudioModelMds", "Vertex %1 of surface %2 has no bone weight.").arg(v).arg(surface.name));
				return {};
			}
			// The normal in the first bone's space (RB_SurfaceAnim rotates it
			// by that bone alone).
			const ModelJointInfluence& lead = surface.skinning.influences.at(first);
			ModelVec3 normal = lead.normalOffset;
			double length = std::sqrt(double(normal.x) * normal.x + double(normal.y) * normal.y + double(normal.z) * normal.z);
			if (length < 1e-9 && bindFrame >= 0 && bindFrame < surface.frames.size() && v < surface.frames.at(bindFrame).normals.size()) {
				const ModelVec3 n = surface.frames.at(bindFrame).normals.at(v);
				const ModelJointMatrix& r = bind.at(lead.joint);
				// The bind rotation's inverse is its transpose.
				normal = {r.m[0] * n.x + r.m[4] * n.y + r.m[8] * n.z, r.m[1] * n.x + r.m[5] * n.y + r.m[9] * n.z, r.m[2] * n.x + r.m[6] * n.y + r.m[10] * n.z};
				length = std::sqrt(double(normal.x) * normal.x + double(normal.y) * normal.y + double(normal.z) * normal.z);
			}
			if (length > 1e-9) {
				normal = {float(normal.x / length), float(normal.y / length), float(normal.z / length)};
			}
			putVec(&vertices, normal);
			putF32(&vertices, surface.texCoords.value(v).u);
			putF32(&vertices, surface.texCoords.value(v).v);
			putI32(&vertices, count);
			putI32(&vertices, 0); // fixedParent, not read by the renderer
			putF32(&vertices, 0.0f); // fixedDist
			for (int i = first; i < first + count; ++i) {
				const ModelJointInfluence& influence = surface.skinning.influences.at(i);
				putI32(&vertices, influence.joint);
				putF32(&vertices, influence.weight);
				putVec(&vertices, influence.offset);
				referenced.insert(influence.joint);
			}
		}
		QByteArray triangles;
		for (const ModelTriangle& t : surface.triangles) {
			// Clockwise in the file, as MD3.
			putI32(&triangles, t.a);
			putI32(&triangles, t.c);
			putI32(&triangles, t.b);
		}
		QByteArray collapse;
		for (int v = 0; v < surface.vertexCount; ++v) {
			putI32(&collapse, v);
		}
		QList<int> sorted = referenced.values();
		std::sort(sorted.begin(), sorted.end());
		QByteArray references;
		for (const int bone : std::as_const(sorted)) {
			putI32(&references, bone);
		}
		const qint64 ofsVerts = kSurfaceBytes, ofsTriangles = ofsVerts + vertices.size(), ofsCollapse = ofsTriangles + triangles.size(),
					 ofsReferences = ofsCollapse + collapse.size(), ofsEnd = ofsReferences + references.size();
		QByteArray header;
		putI32(&header, 0);
		putName(&header, surface.name);
		putName(&header, surface.skinPaths.value(0));
		putI32(&header, 0); // shaderIndex, set when loaded
		putI32(&header, surface.vertexCount); // minLod: nothing collapses
		putI32(&header, qint32(-(surfaceStart + surfaceBlock.size())));
		putI32(&header, surface.vertexCount);
		putI32(&header, qint32(ofsVerts));
		putI32(&header, int(surface.triangles.size()));
		putI32(&header, qint32(ofsTriangles));
		putI32(&header, qint32(ofsCollapse));
		putI32(&header, int(sorted.size()));
		putI32(&header, qint32(ofsReferences));
		putI32(&header, qint32(ofsEnd));
		surfaceBlock += header + vertices + triangles + collapse + references;
	}

	// Tags follow their bone exactly.
	QByteArray tagBlock;
	int offsetTags = 0;
	for (const ModelSkeletalTag& tag : skeleton.tags) {
		putName(&tagBlock, tag.name);
		putF32(&tagBlock, 0.0f);
		putI32(&tagBlock, tag.joint);
		if (modelJointMatrixDistance(tag.offset, modelJointIdentity()) > 1e-4) {
			++offsetTags;
		}
	}
	if (offsetTags > 0) {
		result.omissions << QCoreApplication::translate("VibeStudioModelMds",
			"%n tag(s) sit off their bone; MDS tags follow the bone itself, so the offset is left out.", nullptr, offsetTags);
	}
	if (!mesh.collisionBoxes.isEmpty()) {
		result.omissions << QCoreApplication::translate("VibeStudioModelMds", "Collision boxes stay in the editable source; MDS does not store them.");
	}
	if (skeleton.clips.size() > 1) {
		result.notes << QCoreApplication::translate("VibeStudioModelMds",
			"%1 clips were written one after another as %2 frames; name their ranges in the game's animation script.")
			.arg(skeleton.clips.size()).arg(frames.size());
	}

	// The blocks must have the sizes the layout gives them.
	qint64 vertexRecords = 0;
	for (const ModelSurface& surface : mesh.surfaces) {
		vertexRecords += qint64(surface.vertexCount) * kVertexBytes + qint64(surface.skinning.influences.size()) * kWeightBytes;
	}
	if (frameBlock.size() != qint64(frames.size()) * (kFrameHeaderBytes + qint64(boneCount) * kBoneFrameBytes)
		|| boneBlock.size() != qint64(boneCount) * kBoneInfoBytes || tagBlock.size() != qint64(skeleton.tags.size()) * kTagBytes
		|| surfaceBlock.size() < qint64(mesh.surfaces.size()) * kSurfaceBytes + vertexRecords) {
		fail(error, QCoreApplication::translate("VibeStudioModelMds", "The MDS blocks came out the wrong size; nothing was written."));
		return {};
	}
	QByteArray bytes("MDSW");
	putI32(&bytes, 4);
	putName(&bytes, mesh.sourcePath.isEmpty() ? QStringLiteral("model") : mesh.sourcePath.section(QLatin1Char('/'), -1));
	putF32(&bytes, options.lodScale);
	putF32(&bytes, options.lodBias);
	putI32(&bytes, int(frames.size()));
	putI32(&bytes, boneCount);
	putI32(&bytes, 0); // ofsFrames
	putI32(&bytes, 0); // ofsBones
	putI32(&bytes, options.torsoParent);
	putI32(&bytes, int(mesh.surfaces.size()));
	putI32(&bytes, 0); // ofsSurfaces
	putI32(&bytes, int(skeleton.tags.size()));
	putI32(&bytes, 0); // ofsTags
	putI32(&bytes, 0); // ofsEnd
	const qint64 ofsFrames = kHeaderBytes, ofsBones = ofsFrames + frameBlock.size(), ofsSurfaces = ofsBones + boneBlock.size(),
				 ofsTags = ofsSurfaces + surfaceBlock.size(), ofsEnd = ofsTags + tagBlock.size();
	setI32(&bytes, 88, qint32(ofsFrames));
	setI32(&bytes, 92, qint32(ofsBones));
	setI32(&bytes, 104, qint32(ofsSurfaces));
	setI32(&bytes, 112, qint32(ofsTags));
	setI32(&bytes, 116, qint32(ofsEnd));
	bytes += frameBlock + boneBlock + surfaceBlock + tagBlock;
	if (!work.check()) {
		return {};
	}
	result.notes << QCoreApplication::translate("VibeStudioModelMds", "Frames: %1, bones: %2; angles round to 1/65536 of a turn.")
		.arg(frames.size()).arg(boneCount);
	result.maxJointTranslationError = worstStretch;
	result.maxJointRotationDegrees = std::asin(std::min(1.0, worstRotation)) / kRadians;
	result.notes += result.omissions;
	if (report) {
		*report = std::move(result);
	}
	return bytes;
}

} // namespace vibestudio
