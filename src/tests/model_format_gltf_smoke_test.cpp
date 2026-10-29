// glTF 2.0 import (FMT-05, core/model_format_gltf.cpp). Every fixture is
// built here from the Khronos glTF 2.0 specification; no asset files are used.
// Covers .gltf with an embedded buffer, .glb, an external .bin, axis
// conversion and winding, a two-joint skin with a clip, morph target
// animation, cubic splines, strips and fans, refusals, every-byte truncation
// and seeded corruption, editable import with MD3, IQM and MD5 round trips,
// and the model import and inspect commands.
#include "core/model_decode_issues.h"
#include "core/model_document.h"
#include "core/model_mesh.h"
#include "core/model_skeleton.h"
#include "core/parser_fuzz.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>

using namespace vibestudio;

namespace
{
int checks = 0;
int failures = 0;
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
bool near(float a, float b, float epsilon = 1e-4f)
{
	return std::abs(a - b) <= epsilon;
}
bool nearVec(const ModelVec3 &v, float x, float y, float z, float epsilon = 1e-4f)
{
	return near(v.x, x, epsilon) && near(v.y, y, epsilon) && near(v.z, z, epsilon);
}

// Builds buffer views and accessors over one binary buffer.
class Builder
{
  public:
	int floats(const QVector<float> &values, const char *type, bool vertexAttribute = true)
	{
		align();
		const int offset = int(m_buffer.size());
		for (float value : values)
		{
			quint32 raw = 0;
			std::memcpy(&raw, &value, 4);
			for (int shift = 0; shift < 32; shift += 8)
				m_buffer.append(char((raw >> shift) & 0xff));
		}
		const int components = QByteArray(type) == "SCALAR" ? 1 : QByteArray(type) == "VEC2" ? 2 : QByteArray(type) == "VEC3" ? 3 : QByteArray(type) == "VEC4" ? 4 : 16;
		return accessor(offset, int(values.size()) * 4, 5126, int(values.size()) / components, type, false, vertexAttribute);
	}
	int shorts(const QVector<int> &values, const char *type, int componentType = 5123)
	{
		align();
		const int offset = int(m_buffer.size());
		for (int value : values)
		{
			if (componentType == 5121)
			{
				m_buffer.append(char(value & 0xff));
				continue;
			}
			m_buffer.append(char(value & 0xff));
			m_buffer.append(char((value >> 8) & 0xff));
		}
		const int components = QByteArray(type) == "SCALAR" ? 1 : 4;
		return accessor(offset, int(m_buffer.size()) - offset, componentType, int(values.size()) / components, type, false, false);
	}
	QJsonArray views;
	QJsonArray accessors;
	QByteArray buffer() const { return m_buffer; }

  private:
	void align()
	{
		while (m_buffer.size() % 4)
			m_buffer.append('\0');
	}
	int accessor(int offset, int length, int componentType, int count, const char *type, bool normalized, bool vertexAttribute)
	{
		QJsonObject view{{"buffer", 0}, {"byteOffset", offset}, {"byteLength", length}};
		if (vertexAttribute)
			view.insert("target", 34962);
		views.append(view);
		QJsonObject object{{"bufferView", int(views.size()) - 1}, {"componentType", componentType}, {"count", count}, {"type", QString::fromLatin1(type)}};
		if (normalized)
			object.insert("normalized", true);
		accessors.append(object);
		return int(accessors.size()) - 1;
	}
	QByteArray m_buffer;
};

QJsonObject document(const Builder &builder, QJsonObject root, const QString &uri)
{
	root.insert("asset", QJsonObject{{"version", "2.0"}, {"generator", "VibeStudio test"}});
	QJsonObject buffer{{"byteLength", int(builder.buffer().size())}};
	if (!uri.isNull())
		buffer.insert("uri", uri);
	root.insert("buffers", QJsonArray{buffer});
	root.insert("bufferViews", builder.views);
	root.insert("accessors", builder.accessors);
	return root;
}
QByteArray embedded(const Builder &builder, const QJsonObject &root)
{
	return QJsonDocument(document(builder, root, QStringLiteral("data:application/octet-stream;base64,") +
														QString::fromLatin1(builder.buffer().toBase64())))
		.toJson(QJsonDocument::Compact);
}
QByteArray glb(const Builder &builder, const QJsonObject &root)
{
	QByteArray json = QJsonDocument(document(builder, root, QString())).toJson(QJsonDocument::Compact);
	while (json.size() % 4)
		json.append(' ');
	QByteArray bin = builder.buffer();
	while (bin.size() % 4)
		bin.append('\0');
	QByteArray bytes("glTF");
	const auto u32 = [&bytes](quint32 value) {
		for (int shift = 0; shift < 32; shift += 8)
			bytes.append(char((value >> shift) & 0xff));
	};
	u32(2);
	u32(quint32(12 + 8 + json.size() + 8 + bin.size()));
	u32(quint32(json.size()));
	u32(0x4E4F534Au);
	bytes.append(json);
	u32(quint32(bin.size()));
	u32(0x004E4942u);
	bytes.append(bin);
	return bytes;
}

// A textured quad facing glTF +Z (towards the viewer), in metres.
QJsonObject quad(Builder *builder, int mode = 4)
{
	const int positions = builder->floats({0, 0, 0, 1, 0, 0, 1, 2, 0, 0, 2, 0}, "VEC3");
	const int normals = builder->floats({0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1}, "VEC3");
	const int uvs = builder->floats({0, 1, 1, 1, 1, 0, 0, 0}, "VEC2");
	const QVector<int> list = mode == 4 ? QVector<int>{0, 1, 2, 0, 2, 3} : mode == 5 ? QVector<int>{0, 1, 3, 2} : QVector<int>{0, 1, 2, 3};
	const int indices = builder->shorts(list, "SCALAR");
	QJsonObject primitive{{"attributes", QJsonObject{{"POSITION", positions}, {"NORMAL", normals}, {"TEXCOORD_0", uvs}}}, {"indices", indices}, {"material", 0}};
	if (mode != 4)
		primitive.insert("mode", mode);
	QJsonObject root;
	root.insert("meshes", QJsonArray{QJsonObject{{"name", "crate"}, {"primitives", QJsonArray{primitive}}}});
	root.insert("nodes", QJsonArray{QJsonObject{{"name", "crate"}, {"mesh", 0}}});
	root.insert("scenes", QJsonArray{QJsonObject{{"nodes", QJsonArray{0}}}});
	root.insert("scene", 0);
	root.insert("materials", QJsonArray{QJsonObject{{"name", "crate_mat"}, {"pbrMetallicRoughness", QJsonObject{{"baseColorTexture", QJsonObject{{"index", 0}}}}}}});
	root.insert("textures", QJsonArray{QJsonObject{{"source", 0}}});
	root.insert("images", QJsonArray{QJsonObject{{"uri", "textures/crate%20a.png"}}});
	return root;
}

// Two joints along glTF +Y; four vertices, two on each; one clip turning the
// upper joint a quarter turn about glTF +Z over one second.
QJsonObject skinned(Builder *builder, const char *interpolation = "LINEAR")
{
	const int positions = builder->floats({-0.5f, 0, 0, 0.5f, 0, 0, -0.5f, 2, 0, 0.5f, 2, 0}, "VEC3");
	const int joints = builder->shorts({0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}, "VEC4", 5121);
	const int weights = builder->floats({1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}, "VEC4");
	const int indices = builder->shorts({0, 1, 3, 0, 3, 2}, "SCALAR");
	// Column-major inverse binds: identity, and a translation of -1 on Y.
	const int inverseBinds = builder->floats({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1}, "MAT4", false);
	const bool cubic = QByteArray(interpolation) == "CUBICSPLINE";
	const int times = builder->floats({0, 1}, "SCALAR", false);
	const float s = float(std::sqrt(0.5));
	const int rotations = cubic ? builder->floats({0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, s, s, 0, 0, 0, 0}, "VEC4", false)
								: builder->floats({0, 0, 0, 1, 0, 0, s, s}, "VEC4", false);
	QJsonObject root;
	const QJsonObject attributes{{"POSITION", positions}, {"JOINTS_0", joints}, {"WEIGHTS_0", weights}};
	const QJsonObject primitive{{"attributes", attributes}, {"indices", indices}};
	root.insert("meshes", QJsonArray{QJsonObject{{"name", "arm"}, {"primitives", QJsonArray{primitive}}}});
	root.insert("nodes", QJsonArray{QJsonObject{{"name", "arm"}, {"mesh", 0}, {"skin", 0}}, QJsonObject{{"name", "shoulder"}, {"children", QJsonArray{2}}},
		QJsonObject{{"name", "elbow"}, {"translation", QJsonArray{0, 1, 0}}}});
	root.insert("skins", QJsonArray{QJsonObject{{"joints", QJsonArray{1, 2}}, {"inverseBindMatrices", inverseBinds}}});
	root.insert("scenes", QJsonArray{QJsonObject{{"nodes", QJsonArray{0, 1}}}});
	root.insert("animations", QJsonArray{QJsonObject{{"name", "bend"},
		{"samplers", QJsonArray{QJsonObject{{"input", times}, {"output", rotations}, {"interpolation", interpolation}}}},
		{"channels", QJsonArray{QJsonObject{{"sampler", 0}, {"target", QJsonObject{{"node", 2}, {"path", "rotation"}}}}}}}});
	return root;
}

// A triangle whose third vertex a morph target lifts one metre on glTF +Y,
// with its weight animated from 0 to 1 over one second.
QJsonObject morphing(Builder *builder)
{
	const int positions = builder->floats({0, 0, 0, 1, 0, 0, 0, 1, 0}, "VEC3");
	const int delta = builder->floats({0, 0, 0, 0, 0, 0, 0, 1, 0}, "VEC3");
	const int times = builder->floats({0, 1}, "SCALAR", false);
	const int weights = builder->floats({0, 1}, "SCALAR", false);
	QJsonObject root;
	root.insert("meshes", QJsonArray{QJsonObject{{"name", "blob"}, {"weights", QJsonArray{0}},
		{"primitives", QJsonArray{QJsonObject{{"attributes", QJsonObject{{"POSITION", positions}}}, {"targets", QJsonArray{QJsonObject{{"POSITION", delta}}}}}}}}});
	root.insert("nodes", QJsonArray{QJsonObject{{"name", "blob"}, {"mesh", 0}}});
	root.insert("animations", QJsonArray{QJsonObject{{"name", "swell"},
		{"samplers", QJsonArray{QJsonObject{{"input", times}, {"output", weights}}}},
		{"channels", QJsonArray{QJsonObject{{"sampler", 0}, {"target", QJsonObject{{"node", 0}, {"path", "weights"}}}}}}}});
	return root;
}

std::shared_ptr<QHash<QString, QByteArray>> g_files;
ModelCompanionSource fakeFiles()
{
	ModelCompanionSource source;
	source.read = [](const QString &path, QByteArray *bytes, QString *error) {
		if (!g_files->contains(path))
		{
			*error = QStringLiteral("missing %1").arg(path);
			return false;
		}
		*bytes = g_files->value(path);
		return true;
	};
	return source;
}

QJsonObject runCli(const QString &program, const QStringList &arguments, int *exitCode)
{
	QProcess process;
	process.start(program, QStringList{QStringLiteral("--cli")} + arguments);
	if (!process.waitForFinished(120000))
	{
		process.kill();
		*exitCode = -1;
		return {};
	}
	*exitCode = process.exitCode();
	return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	g_files = std::make_shared<QHash<QString, QByteArray>>();
	QString error;

	// Static quad: axis conversion, winding, UVs and the material image.
	Builder quadBuilder;
	const QJsonObject quadRoot = quad(&quadBuilder);
	const QByteArray quadJson = embedded(quadBuilder, quadRoot);
	const ModelMesh mesh = decodeModelMesh(QStringLiteral("models/test/crate.gltf"), quadJson);
	if (expect(mesh.error.isEmpty() && mesh.geometryAvailable, "an embedded-buffer .gltf decodes"))
	{
		expect(mesh.format == ModelMeshFormat::Gltf && mesh.formatId == QStringLiteral("gltf") && mesh.version == 2, "glTF is detected as version 2");
		expect(mesh.surfaces.size() == 1 && mesh.frames.size() == 1 && mesh.triangleCount == 2, "one surface, one frame, two triangles");
		const auto &surface = mesh.surfaces.constFirst();
		const auto &positions = surface.frames.constFirst().positions;
		// glTF (1, 2, 0) is game (1, 0, 2): Y up becomes Z up.
		expect(nearVec(positions.at(2), 1, 0, 2) && nearVec(positions.at(1), 1, 0, 0), "glTF Y-up positions become Z-up game positions");
		const auto &t = surface.triangles.constFirst();
		const ModelVec3 a = positions.at(t.a), b = positions.at(t.b), c = positions.at(t.c);
		const ModelVec3 e1{b.x - a.x, b.y - a.y, b.z - a.z}, e2{c.x - a.x, c.y - a.y, c.z - a.z};
		const float crossY = e1.z * e2.x - e1.x * e2.z;
		expect(crossY < 0 && nearVec(surface.frames.constFirst().normals.at(0), 0, -1, 0), "a quad facing glTF +Z faces game -Y counter-clockwise");
		expect(near(surface.texCoords.at(2).u, 1) && near(surface.texCoords.at(2).v, 0), "texture coordinates keep v = 0 at the top");
		expect(surface.skinPaths == QStringList{QStringLiteral("textures/crate a.png")}, "the base colour image becomes the surface material");
		expect(mesh.issues.isEmpty(), "a clean glTF has no decode issues");
	}
	const ModelMesh binary = decodeModelMesh(QStringLiteral("models/test/crate.glb"), glb(quadBuilder, quadRoot));
	expect(binary.error.isEmpty() && binary.surfaces.size() == 1 &&
			   nearVec(binary.surfaces.constFirst().frames.constFirst().positions.at(2), 1, 0, 2),
		   "a .glb with a binary chunk decodes the same quad");
	g_files->insert(QStringLiteral("models/test/crate.bin"), quadBuilder.buffer());
	const QByteArray external = QJsonDocument(document(quadBuilder, quadRoot, QStringLiteral("crate.bin"))).toJson();
	const ModelMesh separate = decodeModelMesh(QStringLiteral("models/test/crate.gltf"), external, nullptr, {}, fakeFiles());
	expect(separate.error.isEmpty() && separate.triangleCount == 2 && separate.companionPaths.contains(QStringLiteral("models/test/crate.bin")),
		   "an external .bin is read as a companion");
	expect(!decodeModelMesh(QStringLiteral("models/test/crate.gltf"), external).error.isEmpty(), "a missing .bin is an error");
	const QByteArray escaping = QJsonDocument(document(quadBuilder, quadRoot, QStringLiteral("../../secret.bin"))).toJson();
	expect(!decodeModelMesh(QStringLiteral("models/test/crate.gltf"), escaping, nullptr, {}, fakeFiles()).error.isEmpty(),
		   "a buffer outside the model's folder is refused");
	for (const int mode : {5, 6})
	{
		Builder strip;
		const ModelMesh drawn = decodeModelMesh(QStringLiteral("models/test/strip.gltf"), embedded(strip, quad(&strip, mode)));
		expect(drawn.error.isEmpty() && drawn.triangleCount == 2, "triangle strips and fans become triangles");
	}
	{
		Builder points;
		QJsonObject root = quad(&points);
		auto meshes = root.value("meshes").toArray();
		auto first = meshes[0].toObject();
		auto primitives = first.value("primitives").toArray();
		auto pointPrimitive = primitives[0].toObject();
		pointPrimitive.insert("mode", 0);
		primitives.append(pointPrimitive);
		first.insert("primitives", primitives);
		meshes[0] = first;
		root.insert("meshes", meshes);
		const ModelMesh mixed = decodeModelMesh(QStringLiteral("models/test/points.gltf"), embedded(points, root));
		const auto issues = modelDecodeIssues(mixed);
		expect(mixed.error.isEmpty() && mixed.surfaces.size() == 1 && issues.size() == 1 && issues[0].id == QStringLiteral("gltf-primitive-mode") &&
				   issues[0].severity == ModelDecodeIssueSeverity::Omission,
			   "point primitives are left out as an omission");
		auto required = root;
		required.insert("extensionsRequired", QJsonArray{"KHR_draco_mesh_compression"});
		expect(!decodeModelMesh(QStringLiteral("models/test/draco.gltf"), embedded(points, required)).error.isEmpty(),
			   "an unsupported required extension is refused");
		auto future = root;
		future.insert("asset", QJsonObject{{"version", "3.0"}});
		expect(!decodeModelMesh(QStringLiteral("models/test/future.gltf"), QJsonDocument(future).toJson()).error.isEmpty(), "glTF 3 is refused");
	}

	// Skinned: joints, weights summing to one, the clip and its frames.
	{
		const char *interpolation = "LINEAR";
		Builder skin;
		const ModelMesh arm = decodeModelMesh(QStringLiteral("models/test/arm.glb"), glb(skin, skinned(&skin, interpolation)));
		if (!expect(arm.error.isEmpty() && arm.geometryAvailable, "a skinned glb decodes"))
			std::cerr << "  " << qPrintable(arm.error) << '\n';
		const auto &skeleton = arm.skeleton;
		const bool armDecoded = arm.error.isEmpty() && arm.surfaces.size() == 1 && skeleton.joints.size() == 2;
		expect(armDecoded && skeleton.joints[0].name == QStringLiteral("shoulder") && skeleton.joints[1].parent == 0,
			   "the skin's joints become a skeleton, parents first");
		expect(armDecoded && nearVec(modelJointTranslation(skeleton.joints[1].bind), 0, 0, 1), "the elbow's bind sits one unit up in game space");
		bool summed = armDecoded;
		for (int v = 0; armDecoded && v < arm.surfaces[0].skinning.count.size(); ++v)
		{
			float total = 0;
			for (int i = 0; i < arm.surfaces[0].skinning.count[v]; ++i)
				total += arm.surfaces[0].skinning.influences[arm.surfaces[0].skinning.first[v] + i].weight;
			summed &= near(total, 1.0f);
		}
		expect(summed, "skin weights sum to one");
		if (expect(armDecoded && skeleton.clips.size() == 1 && skeleton.clips[0].name == QStringLiteral("bend") && arm.frames.size() == 26,
				   "the animation becomes a skeletal clip baked after the bind pose"))
		{
			const auto &clip = skeleton.clips[0];
			expect(clip.framesPerSecond == 24 && clip.frames.size() == 25, "key times on whole frames choose 24 fps and 25 frames");
			// At the end the elbow has turned a quarter about game -Y (glTF +Z):
			// the upper vertices swing from +Z towards -X.
			const int last = int(arm.frames.size()) - 1;
			const auto &positions = arm.surfaces[0].frames[last].positions;
			expect(nearVec(positions.at(2), -1, 0, 0.5f, 1e-3f),
				   "the last baked frame bends the upper vertices with the elbow");
			const auto &middle = arm.surfaces[0].frames[1 + 12].positions;
			expect(!nearVec(middle.at(2), -0.5f, 0, 2, 1e-3f), "the middle frame is between the poses");
		}
		ModelMesh editable;
		ModelImportReport report;
		if (expect(importEditableModel(QStringLiteral("models/test/arm.glb"), glb(skin, skinned(&skin, interpolation)), &editable, ModelImportOptions{}, &report, &error),
				   "the skinned glb imports as editable"))
		{
			for (const char *format : {"iqm", "md5mesh", "md3"})
			{
				const QByteArray bytes = exportEditableModel(editable, QString::fromLatin1(format), 0, &error);
				const ModelMesh back = decodeModelMesh(QStringLiteral("rt/arm.") + QString::fromLatin1(format), bytes);
				expect(!bytes.isEmpty() && back.error.isEmpty() && back.triangleCount == 2, "the glTF import exports to IQM, MD5 and MD3 and decodes again");
				if (QByteArray(format) != "md3")
					expect(back.skeleton.joints.size() == 2, "skeletal exports keep both joints");
			}
		}
	}
	{
		// Cubic spline with zero tangents eases: halfway in time is halfway in value.
		Builder skin;
		const ModelMesh eased = decodeModelMesh(QStringLiteral("models/test/arm.glb"), glb(skin, skinned(&skin, "CUBICSPLINE")));
		expect(eased.error.isEmpty() && eased.skeleton.clips.size() == 1 && eased.skeleton.clips[0].frames.size() == 25, "a cubic spline clip decodes");
		if (eased.error.isEmpty() && eased.skeleton.clips.size() == 1)
		{
			const auto rotation = modelJointRotation(eased.skeleton.clips[0].frames[12][1]);
			const double angle = 2.0 * std::acos(std::min(1.0, std::abs(double(rotation.w))));
			expect(std::abs(angle - 0.78539816339744830962) < 0.02, "the cubic spline midpoint turns half way");
		}
	}

	// Morph targets become vertex frames.
	Builder blob;
	const ModelMesh morph = decodeModelMesh(QStringLiteral("models/test/blob.gltf"), embedded(blob, morphing(&blob)));
	if (expect(morph.error.isEmpty() && morph.skeleton.isEmpty(), "a morph target animation decodes without a skeleton"))
	{
		expect(morph.frames.size() == 26 && morph.animations.size() == 1 && morph.animations[0].firstFrame == 1 && morph.animations[0].frameCount == 25,
			   "the rest frame comes first, then the clip's 25 frames");
		expect(nearVec(morph.surfaces[0].frames[0].positions.at(2), 0, 0, 1) && nearVec(morph.surfaces[0].frames[25].positions.at(2), 0, 0, 2),
			   "the morph weight lifts the vertex over the clip");
		ModelMesh editable;
		expect(importEditableModel(QStringLiteral("models/test/blob.gltf"), embedded(blob, morphing(&blob)), &editable, &error) &&
				   !exportEditableModel(editable, QStringLiteral("md3"), 0, &error).isEmpty(),
			   "a morph animation imports and exports as an animated MD3");
	}

	// Corruption: every truncation and seeded flips of the glb never crash
	// and never publish geometry with an error.
	Builder fuzzBuilder;
	const QByteArray seed = glb(fuzzBuilder, skinned(&fuzzBuilder));
	int decoded = 0;
	for (qsizetype size = 0; size < seed.size(); ++size)
	{
		const ModelMesh cut = decodeModelMesh(QStringLiteral("fuzz.glb"), seed.left(size));
		if (!cut.error.isEmpty() && cut.geometryAvailable)
			expect(false, "a truncated glb publishes geometry with an error");
		decoded += cut.error.isEmpty();
	}
	expect(decoded == 0, "no truncation of the glb decodes");
	for (const FuzzCase &generated : buildFuzzCorpus({seed, quadJson}, 0x67c7f2a0ull, 256))
	{
		const ModelMesh flipped = decodeModelMesh(generated.data.startsWith("glTF") ? QStringLiteral("fuzz.glb") : QStringLiteral("fuzz.gltf"), generated.data);
		if ((!flipped.error.isEmpty() && flipped.geometryAvailable) || (flipped.error.isEmpty() && !flipped.geometryAvailable))
		{
			expect(false, "a corrupted glTF decodes inconsistently");
			std::cerr << "  case " << qPrintable(generated.id) << '\n';
		}
	}

	// CLI.
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (argc == 2 && !root.isEmpty() && QDir().mkpath(root))
	{
		QTemporaryDir temporary(QDir(root).filePath("gltf-XXXXXX"));
		const QString program = QString::fromLocal8Bit(argv[1]);
		const QString input = temporary.filePath(QStringLiteral("arm.glb"));
		QFile file(input);
		Builder skin;
		const QByteArray bytes = glb(skin, skinned(&skin));
		expect(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write the glb fixture");
		file.close();
		int code = -1;
		const auto inspect = runCli(program, {QStringLiteral("model"), QStringLiteral("inspect"), input, QStringLiteral("--json")}, &code);
		const auto model = inspect.value("model").toObject();
		// The skinned fixture has no texture coordinates: an advisory, never damage.
		expect(code == 0 && model.value("format").toString() == QStringLiteral("gltf") && model.value("decodeVerdict").toString() == QStringLiteral("advisories"),
			   "model inspect reads a glb");
		const auto imported = runCli(program, {QStringLiteral("model"), QStringLiteral("import"), input, QStringLiteral("--output"),
											   temporary.filePath(QStringLiteral("arm.mesh.json")), QStringLiteral("--json")},
									 &code);
		expect(code == 0 && imported.value("triangles").toInt() == 2 && QFileInfo::exists(temporary.filePath(QStringLiteral("arm.mesh.json"))),
			   "model import turns a glb into an editable source");
	}
	std::cout << "model_format_gltf_smoke_test: " << checks << " checks, " << failures << " failure(s)\n";
	return failures == 0 ? 0 : 1;
}
