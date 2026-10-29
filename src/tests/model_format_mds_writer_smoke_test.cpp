// The RTCW MDS writer (FMT-09, core/model_mds.h): a two-bone skeleton with a
// clip and a tag bone is written, read back by VibeStudio's decoder and, as an
// independent check, by a reader written here from the published mdsHeader_t
// layout and R_CalcBone/RB_SurfaceAnim maths; refusals for stretching bones,
// scaling bones, too many weights and models without a skeleton; and
// truncation and corruption of the written file.
#include "core/model_document.h"
#include "core/model_mds.h"
#include "core/model_mesh.h"
#include "core/model_skeleton.h"
#include "core/parser_fuzz.h"

#include <QCoreApplication>
#include <QtEndian>

#include <cmath>
#include <cstring>
#include <iostream>

using namespace vibestudio;

namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *message)
{
	++checks;
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
constexpr double kPi = 3.14159265358979323846;

ModelQuat axisAngle(double x, double y, double z, double degrees)
{
	const double half = degrees * kPi / 360.0;
	const double s = std::sin(half);
	return ModelQuat{float(x * s), float(y * s), float(z * s), float(std::cos(half))};
}

// Root at the origin, child 10 units along +X, and an unweighted tag bone 4
// units above the child. One clip turns the root about Z (and lifts it) and
// the child about Y, keeping both lengths.
ModelMesh skeletal()
{
	ModelMesh mesh;
	mesh.geometryAvailable = true;
	ModelSkeleton &skeleton = mesh.skeleton;
	skeleton.sourceFormat = "test";
	skeleton.joints = {ModelJoint{"root", -1, modelJointIdentity(), 0}, ModelJoint{"arm", 0, modelJointMatrix({}, {10, 0, 0}), 0},
		ModelJoint{"tag_hand", 1, modelJointMatrix({}, {10, 0, 4}), 0}};
	ModelSkeletalClip clip;
	clip.name = "wave";
	clip.framesPerSecond = 10;
	for (int f = 0; f < 3; ++f)
	{
		const double a = f * 20.0;
		const ModelQuat turn = axisAngle(0, 0, 1, a);
		const ModelVec3 rootAt{0, 0, float(f)};
		const ModelVec3 armAt{float(10 * std::cos(a * kPi / 180)), float(10 * std::sin(a * kPi / 180)), float(f)};
		const ModelQuat armTurn = modelQuatMultiply(turn, axisAngle(0, 1, 0, f * 30.0));
		const ModelJointMatrix arm = modelJointMatrix(armTurn, armAt);
		clip.frames.append({modelJointMatrix(turn, rootAt), arm, modelJointMultiply(arm, modelJointMatrix({}, {0, 0, 4}))});
	}
	skeleton.clips = {clip};
	skeleton.tags = {ModelSkeletalTag{"tag_hand", 2, modelJointIdentity()}};
	ModelSurface surface;
	surface.name = "body";
	surface.skinPaths = {"models/test/body"};
	const QVector<ModelVec3> positions{{0, -1, 0}, {0, 1, 0}, {12, -1, 0}, {12, 1, 0}};
	surface.texCoords = {{0, 0}, {0, 1}, {1, 0}, {1, 1}};
	surface.triangles = {{0, 2, 3}, {0, 3, 1}};
	surface.vertexCount = 4;
	ModelFrameGeometry pose;
	pose.positions = positions;
	pose.normals.fill({0, 0, 1}, 4);
	surface.frames = {pose};
	QString error;
	if (!modelSkinningFromBindPose(skeleton, positions, pose.normals, {0, 0, 1, 1}, {1, 1, 1, 1}, 1, &surface.skinning, &error))
		std::cerr << "skinning: " << qPrintable(error) << '\n';
	mesh.surfaces = {surface};
	if (!bakeModelSkeleton(&mesh, {}, &error))
		std::cerr << "bake: " << qPrintable(error) << '\n';
	updateEditableModelMetadata(&mesh);
	return mesh;
}

double distance(const ModelVec3 &a, const ModelVec3 &b)
{
	return std::sqrt(double(a.x - b.x) * (a.x - b.x) + double(a.y - b.y) * (a.y - b.y) + double(a.z - b.z) * (a.z - b.z));
}

// --- An independent MDS reader: mdsHeader_t, R_CalcBone, RB_SurfaceAnim. ---
qint32 i32(const QByteArray &b, qint64 at)
{
	return qFromLittleEndian<qint32>(b.constData() + at);
}
qint16 i16(const QByteArray &b, qint64 at)
{
	return qFromLittleEndian<qint16>(b.constData() + at);
}
float f32(const QByteArray &b, qint64 at)
{
	const quint32 raw = quint32(i32(b, at));
	float value = 0;
	std::memcpy(&value, &raw, 4);
	return value;
}
// AngleVectors (q_math.c): forward, right, up.
void angleVectors(double pitch, double yaw, double roll, double forward[3], double right[3], double up[3])
{
	const double sp = std::sin(pitch * kPi / 180), cp = std::cos(pitch * kPi / 180), sy = std::sin(yaw * kPi / 180), cy = std::cos(yaw * kPi / 180),
				 sr = std::sin(roll * kPi / 180), cr = std::cos(roll * kPi / 180);
	forward[0] = cp * cy;
	forward[1] = cp * sy;
	forward[2] = -sp;
	right[0] = -sr * sp * cy + cr * sy;
	right[1] = -sr * sp * sy - cr * cy;
	right[2] = -sr * cp;
	up[0] = cr * sp * cy + sr * sy;
	up[1] = cr * sp * sy - sr * cy;
	up[2] = cr * cp;
}
// Every vertex of the first surface in every frame, as the game computes it.
QVector<QVector<ModelVec3>> oraclePositions(const QByteArray &file)
{
	QVector<QVector<ModelVec3>> result;
	const int frames = i32(file, 80), bones = i32(file, 84), ofsFrames = i32(file, 88), ofsBones = i32(file, 92), ofsSurfaces = i32(file, 104);
	for (int f = 0; f < frames; ++f)
	{
		const qint64 frame = ofsFrames + qint64(f) * (52 + bones * 12);
		QVector<std::array<double, 12>> matrix(bones);
		for (int b = 0; b < bones; ++b)
		{
			const qint64 at = frame + 52 + b * 12;
			const double toDegrees = 360.0 / 65536.0;
			double fw[3], rt[3], up[3];
			angleVectors(i16(file, at) * toDegrees, i16(file, at + 2) * toDegrees, i16(file, at + 4) * toDegrees, fw, rt, up);
			const int parent = i32(file, ofsBones + b * 80 + 64);
			const double parentDist = f32(file, ofsBones + b * 80 + 72);
			double t[3] = {f32(file, frame + 40), f32(file, frame + 44), f32(file, frame + 48)};
			if (parent >= 0)
			{
				const double op = i16(file, at + 8) * toDegrees * kPi / 180, oy = i16(file, at + 10) * toDegrees * kPi / 180;
				const double dir[3] = {std::cos(op) * std::cos(oy), std::cos(op) * std::sin(oy), -std::sin(op)};
				for (int i = 0; i < 3; ++i)
					t[i] = matrix[parent][size_t(9 + i)] + parentDist * dir[i];
			}
			// Rows forward, left (= -right) and up; the translation last.
			matrix[b] = {fw[0], fw[1], fw[2], -rt[0], -rt[1], -rt[2], up[0], up[1], up[2], t[0], t[1], t[2]};
		}
		QVector<ModelVec3> positions;
		const int vertexCount = i32(file, ofsSurfaces + 144);
		qint64 cursor = ofsSurfaces + i32(file, ofsSurfaces + 148);
		for (int v = 0; v < vertexCount; ++v)
		{
			const int weights = i32(file, cursor + 20);
			double p[3] = {0, 0, 0};
			for (int w = 0; w < weights; ++w)
			{
				const qint64 at = cursor + 32 + w * 20;
				const int bone = i32(file, at);
				const double weight = f32(file, at + 4);
				const double o[3] = {f32(file, at + 8), f32(file, at + 12), f32(file, at + 16)};
				const auto &m = matrix[bone];
				// LocalAddScaledMatrixTransformVectorTranslate: matrix rows times
				// the offset, plus the translation.
				for (int i = 0; i < 3; ++i)
					p[i] += weight * (m[size_t(i * 3)] * o[0] + m[size_t(i * 3 + 1)] * o[1] + m[size_t(i * 3 + 2)] * o[2] + m[size_t(9 + i)]);
			}
			positions.append(ModelVec3{float(p[0]), float(p[1]), float(p[2])});
			cursor += 32 + qint64(weights) * 20;
		}
		result.append(positions);
	}
	return result;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	const ModelMesh mesh = skeletal();
	expect(validateEditableModel(mesh).isEmpty() && mesh.frames.size() == 4, "the skeletal fixture is editable with a bind pose and three clip frames");

	ModelExportReport report;
	const QByteArray bytes = exportModelMds(mesh, {}, &error, {}, &report);
	if (!expect(!bytes.isEmpty() && bytes.startsWith("MDSW"), "the skeleton writes as MDS"))
	{
		std::cerr << "  " << qPrintable(error) << '\n';
		return 1;
	}
	expect(i32(bytes, 4) == 4 && i32(bytes, 80) == 3 && i32(bytes, 84) == 3 && i32(bytes, 108) == 1 && i32(bytes, 116) == bytes.size(),
		   "the header holds three frames, three bones, one tag and the file's end");

	// The game's own maths, read here independently, reproduces the clip.
	const auto oracle = oraclePositions(bytes);
	double worst = 0;
	bool counted = oracle.size() == 3;
	for (int f = 0; counted && f < 3; ++f)
	{
		const auto &expected = mesh.surfaces[0].frames[f + 1].positions;
		counted = oracle[f].size() == expected.size();
		for (int v = 0; counted && v < expected.size(); ++v)
			worst = std::max(worst, distance(oracle[f][v], expected[v]));
	}
	expect(counted && worst < 0.02, "an independent reader of the bytes puts every vertex where the clip does, within angle rounding");

	// VibeStudio's decoder agrees, and keeps the tag.
	const ModelMesh back = decodeModelMesh(QStringLiteral("models/test/arm.mds"), bytes);
	if (expect(back.error.isEmpty() && back.geometryAvailable && back.skeleton.joints.size() == 3, "the decoder reads the written MDS"))
	{
		expect(back.frames.size() == 3 && back.triangleCount == 2 && back.surfaces[0].skinPaths == mesh.surfaces[0].skinPaths, "frames, triangles and the shader survive");
		double decodedWorst = 0;
		for (int f = 0; f < 3; ++f)
			for (int v = 0; v < 4; ++v)
				decodedWorst = std::max(decodedWorst, distance(back.surfaces[0].frames[f].positions[v], mesh.surfaces[0].frames[f + 1].positions[v]));
		expect(decodedWorst < 0.02, "decoded frames match the source clip within angle rounding");
		expect(back.skeleton.tags.size() == 1 && back.skeleton.tags[0].name == QStringLiteral("tag_hand") && back.skeleton.tags[0].joint == 2,
			   "the tag follows its bone");
		bool winding = true;
		for (int t = 0; t < 2; ++t)
			winding &= back.surfaces[0].triangles[t].a == mesh.surfaces[0].triangles[t].a && back.surfaces[0].triangles[t].b == mesh.surfaces[0].triangles[t].b;
		expect(winding, "triangles keep their counter-clockwise order through the file's clockwise storage");
	}
	expect(!exportEditableModel(mesh, QStringLiteral("mds"), 0, &error).isEmpty(), "model export reaches the writer as mds");
	expect(modelExportFormatIds().contains(QStringLiteral("mds")), "mds is an export id");

	// Refusals.
	ModelMesh stretched = mesh;
	stretched.skeleton.clips[0].frames[2][1] = modelJointMatrix({}, {14, 0, 2});
	expect(exportModelMds(stretched, {}, &error).isEmpty() && error.contains(QStringLiteral("fixed length")), "a bone that stretches is refused");
	ModelMesh scaled = mesh;
	scaled.skeleton.clips[0].frames[1][0] = modelJointMatrix({}, {0, 0, 1}, {2, 2, 2});
	expect(exportModelMds(scaled, {}, &error).isEmpty() && error.contains(QStringLiteral("scales")), "a bone that scales is refused");
	ModelMesh crowded = mesh;
	ModelMdsExportOptions one;
	one.maxInfluences = 0;
	expect(exportModelMds(crowded, one, &error).isEmpty() && error.contains(QStringLiteral("weights")), "too many weights a vertex are refused");
	ModelMesh rigid = mesh;
	rigid.skeleton = {};
	for (auto &surface : rigid.surfaces)
		surface.skinning = {};
	expect(exportModelMds(rigid, {}, &error).isEmpty() && error.contains(QStringLiteral("skeleton")), "a model without a skeleton is refused");

	// Corruption of the written file never crashes the decoder or publishes
	// geometry with an error.
	bool consistent = true;
	for (qsizetype size = 0; size < bytes.size(); ++size)
	{
		const ModelMesh cut = decodeModelMesh(QStringLiteral("cut.mds"), bytes.left(size));
		consistent &= cut.error.isEmpty() || !cut.geometryAvailable;
	}
	for (const FuzzCase &generated : buildFuzzCorpus({bytes}, 0x4d4453ull, 192))
	{
		const ModelMesh flipped = decodeModelMesh(QStringLiteral("fuzz.mds"), generated.data);
		consistent &= flipped.error.isEmpty() || !flipped.geometryAvailable;
	}
	expect(consistent, "truncated and corrupted MDS files decode safely");

	std::cout << "model_format_mds_writer_smoke_test: " << checks << " checks, " << failures << " failure(s)\n";
	return failures == 0 ? 0 : 1;
}
