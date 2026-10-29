// Polygon modelling tools, part A: Align Vertices (flatten, set coordinate,
// snap to grid) and the topology tools that follow it, through the shared
// document service and `model tool`.
#include "cli/model_tools.h"
#include "core/model_collision.h"
#include "core/model_design.h"
#include "core/model_skeleton.h"
#include "core/model_whole_transform.h"
#include "core/model_document.h"
#include "core/model_geometric_topology.h"
#include "core/model_intersections.h"
#include "core/model_mesh_tools.h"
#include "core/model_selection_tools.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRandomGenerator>
#include <QTemporaryDir>

#include <cmath>
#include <functional>
#include <iostream>

using namespace vibestudio;
namespace
{
int checks = 0, failures = 0;
bool expect(bool condition, const char *message, const QString &detail = {})
{
	++checks;
	if (!condition)
	{
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty())
			std::cerr << ": " << qPrintable(detail);
		std::cerr << '\n';
		++failures;
	}
	return condition;
}
bool near(double a, double b, double tolerance = 1e-4)
{
	return std::abs(a - b) <= tolerance;
}
// A two-pose mesh; the second pose is the first raised by 8 units.
ModelMesh meshOf(const QVector<ModelVec3> &positions, const QVector<ModelTriangle> &triangles, const QVector<ModelTexCoord> &uvs = {})
{
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	auto mesh = buildModelDesignMesh(design);
	mesh.frames << mesh.frames[0];
	mesh.frames[1].name = QStringLiteral("pose2");
	auto &surface = mesh.surfaces[0];
	surface.triangles = triangles;
	surface.vertexCount = positions.size();
	if (uvs.isEmpty())
	{
		surface.texCoords.clear();
		for (const auto &p : positions)
			surface.texCoords.append({p.x / 64.0f, p.y / 64.0f});
	}
	else
		surface.texCoords = uvs;
	ModelFrameGeometry frame;
	frame.positions = positions;
	frame.normals.fill({0, 0, 1}, positions.size());
	surface.frames = {frame, frame};
	for (auto &point : surface.frames[1].positions)
		point.z += 8;
	surface.uvSeams.clear();
	surface.topologyVertices.clear();
	updateEditableModelMetadata(&mesh);
	return mesh;
}
// A flat grid of quads (two triangles each) from (x0, y0) with `size` cells.
ModelMesh gridMesh(int nx, int ny, double size = 8, double x0 = 0, double y0 = 0)
{
	QVector<ModelVec3> positions;
	QVector<ModelTriangle> triangles;
	for (int j = 0; j <= ny; ++j)
		for (int i = 0; i <= nx; ++i)
			positions.append({float(x0 + i * size), float(y0 + j * size), 0});
	const auto at = [&](int i, int j) { return j * (nx + 1) + i; };
	for (int j = 0; j < ny; ++j)
		for (int i = 0; i < nx; ++i)
		{
			triangles.append({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
			triangles.append({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
		}
	return meshOf(positions, triangles);
}
ModelEdit alignEdit(ModelSelection selection, int axes, ModelAlignTarget target, std::array<double, 3> value = {0, 0, 0}, double grid = 0)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::AlignVertices;
	edit.selection = selection;
	edit.tool.align.axes = axes;
	edit.tool.align.target = target;
	edit.tool.align.value = value;
	edit.tool.align.grid = grid;
	return edit;
}
bool samePositions(const QVector<ModelVec3> &a, const QVector<ModelVec3> &b)
{
	if (a.size() != b.size())
		return false;
	for (int i = 0; i < a.size(); ++i)
		if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].z != b[i].z)
			return false;
	return true;
}
QByteArray bytesOf(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}

void alignChecks()
{
	QString error;
	// Flatten a tilted quad onto its centroid plane, in every pose.
	{
		auto mesh = gridMesh(1, 1);
		auto &surface = mesh.surfaces[0];
		const float heights[4]{0, 1, 2, 5};
		for (int f = 0; f < 2; ++f)
			for (int v = 0; v < 4; ++v)
				surface.frames[f].positions[v].z = heights[v] + f * 8;
		ModelSelection selection{0, {0, 1, 2, 3}, {}};
		expect(applyModelEdit(&mesh, alignEdit(selection, 4, ModelAlignTarget::Centroid), &selection, &error), "flatten a quad on Z", error);
		bool flat = true;
		for (int f = 0; f < 2; ++f)
			for (int v = 0; v < 4; ++v)
				flat &= near(mesh.surfaces[0].frames[f].positions[v].z, 2 + f * 8);
		expect(flat, "each pose flattens onto its own centroid plane");
		bool up = true;
		for (int v = 0; v < 4; ++v)
			up &= near(mesh.surfaces[0].frames[0].normals[v].z, 1, 1e-3);
		expect(up, "normals are refreshed on the flattened quad");
	}
	// Set X = 0 on one copy of a seam vertex: every copy follows.
	{
		// Two quads side by side sharing the edge x = 8 through split UV copies.
		const QVector<ModelVec3> p{{0, 0, 0}, {8, 0, 0}, {8, 8, 0}, {0, 8, 0}, {8.25f, 0, 0}, {16, 0, 0}, {16, 8, 0}, {8.25f, 8, 0}};
		auto mesh = meshOf(p, {{0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7}});
		auto &surface = mesh.surfaces[0];
		for (int f = 0; f < 2; ++f)
		{
			surface.frames[f].positions[4].x = surface.frames[f].positions[1].x;
			surface.frames[f].positions[7].x = surface.frames[f].positions[2].x;
		}
		surface.texCoords[4] = {0.5f, 0.5f};
		ModelSelection selection{0, {1}, {}};
		expect(applyModelEdit(&mesh, alignEdit(selection, 1, ModelAlignTarget::Value, {4, 0, 0}), &selection, &error), "set X on a seam vertex", error);
		const auto &s = mesh.surfaces[0];
		expect(s.frames[0].positions[1].x == 4.0f && s.frames[0].positions[4].x == 4.0f && s.frames[1].positions[1].x == 4.0f &&
				   s.frames[1].positions[4].x == 4.0f && s.frames[0].positions[2].x == 8.0f,
			   "every seam copy takes the exact value in every pose, and nothing else moves");
	}
	// Current-frame scope leaves the other pose alone.
	{
		auto mesh = gridMesh(2, 1);
		ModelSelection selection{0, {0, 3}, {}};
		auto edit = alignEdit(selection, 1, ModelAlignTarget::Value, {-2, 0, 0});
		edit.frame = 1;
		const auto before = mesh.surfaces[0].frames[0].positions;
		expect(applyModelEdit(&mesh, edit, &selection, &error), "align in the current frame only", error);
		expect(samePositions(mesh.surfaces[0].frames[0].positions, before) && mesh.surfaces[0].frames[1].positions[0].x == -2.0f &&
				   mesh.surfaces[0].frames[1].positions[3].x == -2.0f,
			   "frame scope: pose 1 aligned, pose 0 unchanged");
	}
	// Grid snap of 100 scattered vertices is exact and one revision.
	{
		auto mesh = gridMesh(9, 9, 8);
		QRandomGenerator random(42);
		for (int f = 0; f < 2; ++f)
			for (auto &p : mesh.surfaces[0].frames[f].positions)
			{
				p.x += float(random.bounded(1.4) - 0.7);
				p.y += float(random.bounded(1.4) - 0.7);
				p.z += float(random.bounded(0.8) - 0.4);
			}
		ModelDocument document;
		expect(document.setMesh(mesh, &error), "adopt the scattered grid", error);
		ModelSelection all{0, {}, {}};
		for (int v = 0; v < 100; ++v)
			all.vertices.insert(v);
		expect(document.edit(alignEdit(all, 7, ModelAlignTarget::Grid, {0, 0, 0}, 0.5), &error), "snap 100 vertices to a half-unit grid", error);
		bool exact = true;
		for (const auto &frame : document.mesh().surfaces[0].frames)
			for (const auto &p : frame.positions)
				exact &= std::fmod(double(p.x) * 2, 1) == 0 && std::fmod(double(p.y) * 2, 1) == 0 && std::fmod(double(p.z) * 2, 1) == 0;
		expect(exact && document.undoSteps() == 1, "every coordinate is an exact grid multiple after one undo step");
		expect(document.undo() && bytesOf(document.mesh()) == bytesOf(mesh), "one undo restores every scattered vertex");
	}
	// A face that would collapse refuses the whole edit.
	{
		auto mesh = meshOf({{0, 0, 0}, {8, 0, 0}, {0, 0, 8}, {8, 8, 0}}, {{0, 1, 2}, {0, 3, 1}});
		const auto before = bytesOf(mesh);
		ModelSelection selection{0, {0, 1, 2}, {}};
		expect(!applyModelEdit(&mesh, alignEdit(selection, 4, ModelAlignTarget::Centroid), &selection, &error) && bytesOf(mesh) == before &&
				   error.contains(QStringLiteral("collapse")),
			   "flattening a triangle along its own plane's normal is refused atomically", error);
		ModelSelection none{0, {}, {}};
		expect(!applyModelEdit(&mesh, alignEdit(none, 1, ModelAlignTarget::Value), &none, &error), "an empty selection is refused");
		auto bad = alignEdit({0, {0}, {}}, 0, ModelAlignTarget::Value);
		expect(!applyModelEdit(&mesh, bad, nullptr, &error), "an edit without axes is refused");
		auto grid = alignEdit({0, {0}, {}}, 1, ModelAlignTarget::Value, {0, 0, 0}, 2);
		expect(!applyModelEdit(&mesh, grid, nullptr, &error), "a grid step outside grid snapping is refused");
		ModelEdit other;
		other.kind = ModelEditKind::SmoothVertices;
		other.selection = {0, {0}, {}};
		other.tool.align.axes = 1;
		expect(!applyModelEdit(&mesh, other, nullptr, &error), "alignment settings on another tool are refused");
	}
}

// ---- Transform Model ------------------------------------------------------

using Rows = std::array<std::array<double, 3>, 3>;
// Rz(90) * 2 + (1, 2, 3): the transform the checks below apply.
ModelVec3 moved(ModelVec3 p)
{
	return {float(-2 * p.y + 1), float(2 * p.x + 2), float(2 * p.z + 3)};
}
ModelVec3 turned(ModelVec3 v)
{
	return {-v.y, v.x, v.z};
}
bool close(ModelVec3 a, ModelVec3 b, double tolerance = 2e-3)
{
	return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}
bool orthonormal(const float *axis)
{
	const std::array<double, 3> f{axis[0], axis[1], axis[2]}, l{axis[3], axis[4], axis[5]}, u{axis[6], axis[7], axis[8]};
	const auto dot = [](const std::array<double, 3> &a, const std::array<double, 3> &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
	const double det = f[0] * (l[1] * u[2] - l[2] * u[1]) - f[1] * (l[0] * u[2] - l[2] * u[0]) + f[2] * (l[0] * u[1] - l[1] * u[0]);
	return near(dot(f, f), 1, 1e-4) && near(dot(l, l), 1, 1e-4) && near(dot(u, u), 1, 1e-4) && near(dot(f, l), 0, 1e-4) &&
		   near(dot(f, u), 0, 1e-4) && near(dot(l, u), 0, 1e-4) && det > 0;
}
ModelEdit modelEdit(ModelVec3 translation, ModelVec3 rotation, ModelVec3 scale)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::TransformModel;
	edit.translation = translation;
	edit.rotation = rotation;
	edit.scale = scale;
	return edit;
}
// Two surfaces, two poses, an animated tag, a static and an animated collision
// box, and an MDL eye position.
ModelMesh propMesh()
{
	auto mesh = gridMesh(2, 1);
	auto second = mesh.surfaces[0];
	second.name = QStringLiteral("second");
	for (auto &frame : second.frames)
		for (auto &p : frame.positions)
			p.z += 4;
	mesh.surfaces.append(second);
	for (int f = 0; f < 2; ++f)
	{
		ModelTag tag;
		tag.name = QStringLiteral("tag_weapon");
		tag.frameIndex = f;
		tag.origin = {4, 1, float(8 + f)};
		const double a = f * 0.5;
		const float axis[9]{float(std::cos(a)), float(std::sin(a)), 0, float(-std::sin(a)), float(std::cos(a)), 0, 0, 0, 1};
		std::copy(axis, axis + 9, tag.axis);
		mesh.tags.append(tag);
	}
	ModelCollisionBox still;
	still.name = QStringLiteral("body");
	still.centre = {2, 3, 4};
	still.size = {8, 6, 4};
	still.rotation = {0, 0, 20};
	ModelCollisionBox animated;
	animated.name = QStringLiteral("swing");
	animated.size = {4, 4, 4};
	animated.framePoses = {{{0, 0, 0}, {4, 4, 4}, {0, 0, 0}}, {{1, 0, 0}, {4, 4, 6}, {0, 0, 45}}};
	animated.centre = animated.framePoses[0].centre;
	animated.size = animated.framePoses[0].size;
	animated.rotation = animated.framePoses[0].rotation;
	mesh.collisionBoxes = {still, animated};
	mesh.mdl.eyePosition = {0, 0, 22};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
// A two-joint skeleton with one clip, every vertex half on each joint, baked.
ModelMesh riggedMesh()
{
	auto mesh = gridMesh(2, 2);
	ModelJoint root, child;
	root.name = QStringLiteral("root");
	child.name = QStringLiteral("child");
	child.parent = 0;
	root.bind = modelJointFromQuakeAngles(0, 0, 0, {0, 0, 0});
	child.bind = modelJointFromQuakeAngles(0, 30, 0, {8, 0, 0});
	mesh.skeleton.joints = {root, child};
	ModelSkeletalClip clip;
	clip.name = QStringLiteral("wave");
	clip.frames = {{root.bind, modelJointFromQuakeAngles(0, 60, 0, {8, 0, 2})}, {modelJointFromQuakeAngles(0, 10, 0, {0, 0, 1}), child.bind}};
	mesh.skeleton.clips = {clip};
	auto &surface = mesh.surfaces[0];
	for (int v = 0; v < surface.vertexCount; ++v)
	{
		surface.skinning.first.append(surface.skinning.influences.size());
		surface.skinning.count.append(2);
		for (int j = 0; j < 2; ++j)
		{
			const auto inverse = modelJointInverse(mesh.skeleton.joints[j].bind);
			// The child's weight rises linearly with X, from 0.2 to 0.8.
			const float child = 0.2f + 0.6f * surface.frames[0].positions[v].x / 16;
			surface.skinning.influences.append({j, j == 1 ? child : 1 - child, modelJointTransformPoint(inverse, surface.frames[0].positions[v]),
												modelJointTransformVector(inverse, surface.frames[0].normals[v])});
		}
	}
	QString error;
	expect(bakeModelSkeleton(&mesh, {}, &error), "bake the two-joint fixture", error);
	updateEditableModelMetadata(&mesh);
	return mesh;
}

void wholeTransformChecks()
{
	QString error;
	{
		const auto before = propMesh();
		auto mesh = before;
		expect(applyModelEdit(&mesh, modelEdit({1, 2, 3}, {0, 0, 90}, {2, 2, 2}), nullptr, &error), "transform a whole prop", error);
		bool positions = true, normals = true;
		for (int s = 0; s < 2; ++s)
			for (int f = 0; f < 2; ++f)
				for (int v = 0; v < before.surfaces[s].vertexCount; ++v)
				{
					positions &= close(mesh.surfaces[s].frames[f].positions[v], moved(before.surfaces[s].frames[f].positions[v]));
					normals &= close(mesh.surfaces[s].frames[f].normals[v], turned(before.surfaces[s].frames[f].normals[v]));
				}
		expect(positions && normals, "every surface and pose moves, and normals turn with it");
		bool tags = mesh.tags.size() == 2;
		for (int t = 0; tags && t < 2; ++t)
		{
			const auto &tag = mesh.tags[t];
			const auto &old = before.tags[t];
			tags &= close(tag.origin, moved(old.origin)) && orthonormal(tag.axis) &&
					close({tag.axis[0], tag.axis[1], tag.axis[2]}, turned({old.axis[0], old.axis[1], old.axis[2]}), 1e-4);
		}
		expect(tags, "tags follow in every frame with orthonormal turned axes");
		const auto box = mesh.collisionBoxes[0];
		const auto axes = modelCollisionAxes(box), oldAxes = modelCollisionAxes(before.collisionBoxes[0]);
		expect(close(box.centre, moved(before.collisionBoxes[0].centre)) && close(box.size, {16, 12, 8}) && close(axes[0], turned(oldAxes[0]), 1e-4),
			   "the static collision box moves, turns and scales");
		const auto &swing = mesh.collisionBoxes[1];
		expect(swing.framePoses.size() == 2 && close(swing.framePoses[1].centre, moved({1, 0, 0})) && close(swing.framePoses[1].size, {8, 8, 12}) &&
				   close(swing.centre, swing.framePoses[0].centre),
			   "every collision pose follows and the box still mirrors frame zero");
		expect(close(mesh.mdl.eyePosition, moved({0, 0, 22})), "the MDL eye position moves");
		expect(validateEditableModel(mesh).isEmpty(), "the transformed prop validates");
		ModelDocument document;
		expect(document.setMesh(before, &error) && document.edit(modelEdit({1, 2, 3}, {0, 0, 90}, {2, 2, 2}), &error) && document.undoSteps() == 1 &&
				   document.undo() && bytesOf(document.mesh()) == bytesOf(before),
			   "Transform Model is one undo step", error);
	}
	// Non-uniform scale: axis-aligned boxes stretch; a rotated one refuses.
	{
		auto mesh = propMesh();
		const auto before = bytesOf(mesh);
		expect(!applyModelEdit(&mesh, modelEdit({}, {}, {1, 2, 1}), nullptr, &error) && bytesOf(mesh) == before && error.contains(QStringLiteral("shear")),
			   "a non-uniform scale of a rotated collision box is refused atomically", error);
		mesh.collisionBoxes[0].rotation = {};
		mesh.collisionBoxes[1].framePoses[1].rotation = {0, 0, 90};
		expect(applyModelEdit(&mesh, modelEdit({}, {}, {1, 2, 1}), nullptr, &error) && close(mesh.collisionBoxes[0].size, {8, 12, 4}) &&
				   close(mesh.collisionBoxes[1].framePoses[1].size, {8, 4, 6}),
			   "axis-aligned boxes, and quarter-turned ones, stretch along the scaled axes", error);
		bool tags = true;
		for (const auto &tag : mesh.tags)
			tags &= orthonormal(tag.axis);
		expect(tags, "tags stay orthonormal under a non-uniform scale");
	}
	// A mirror reverses winding everywhere, keeps tags right-handed and exports.
	{
		const auto before = propMesh();
		auto mesh = before;
		expect(applyModelEdit(&mesh, modelEdit({}, {}, {-1, 1, 1}), nullptr, &error), "mirror the whole model across X", error);
		const auto &t = mesh.surfaces[0].triangles[0], &o = before.surfaces[0].triangles[0];
		expect(t.a == o.a && t.b == o.c && t.c == o.b, "a mirror reverses winding");
		const auto &s = mesh.surfaces[0].frames[0];
		const auto a = s.positions[t.a], b = s.positions[t.b], c = s.positions[t.c];
		const double nz = double(b.x - a.x) * (c.y - a.y) - double(b.y - a.y) * (c.x - a.x);
		expect(nz > 0 && s.normals[t.a].z > 0, "faces still face up after the mirror, as their normals do");
		bool tags = true;
		for (const auto &tag : mesh.tags)
			tags &= orthonormal(tag.axis) && tag.axis[8] > 0.99f;
		expect(tags, "mirrored tags stay right-handed and keep their up axis");
		for (const auto &format : {QStringLiteral("md3")})
		{
			auto native = mesh;
			for (auto &surface : native.surfaces)
				surface.skinPaths = {QStringLiteral("models/prop/skin.pcx")};
			const auto data = exportEditableModel(native, format, 0, &error);
			const auto restored = decodeModelMesh(QStringLiteral("prop.") + format, data);
			expect(!data.isEmpty() && restored.isValid() && restored.frames.size() == 2, "a mirrored model exports and reloads", error);
		}
	}
	// Skeletal models: joints, clips and offsets follow, so a new bake agrees.
	{
		const auto before = riggedMesh();
		auto mesh = before;
		expect(applyModelEdit(&mesh, modelEdit({1, 2, 3}, {0, 0, 90}, {2, 2, 2}), nullptr, &error), "transform a skinned model", error);
		auto rebaked = mesh;
		expect(bakeModelSkeleton(&rebaked, {}, &error), "bake the transformed skeleton again", error);
		bool agrees = rebaked.surfaces[0].frames.size() == mesh.surfaces[0].frames.size();
		bool follows = true;
		for (int f = 0; agrees && f < mesh.surfaces[0].frames.size(); ++f)
			for (int v = 0; v < mesh.surfaces[0].vertexCount; ++v)
			{
				agrees &= close(rebaked.surfaces[0].frames[f].positions[v], mesh.surfaces[0].frames[f].positions[v]);
				follows &= close(mesh.surfaces[0].frames[f].positions[v], moved(before.surfaces[0].frames[f].positions[v]));
			}
		expect(agrees && follows, "baking after the transform equals the transformed bake from before");
		const auto bind = mesh.skeleton.joints[1].bind;
		expect(close({bind.m[3], bind.m[7], bind.m[11]}, moved({before.skeleton.joints[1].bind.m[3], before.skeleton.joints[1].bind.m[7],
																 before.skeleton.joints[1].bind.m[11]})),
			   "bind joints move with the model");
		const auto snapshot = bytesOf(mesh);
		expect(!applyModelEdit(&mesh, modelEdit({}, {}, {1, 2, 1}), nullptr, &error) && bytesOf(mesh) == snapshot, "skeletal models refuse non-uniform scale",
			   error);
		expect(!applyModelEdit(&mesh, modelEdit({}, {}, {-1, -1, -1}), nullptr, &error) && bytesOf(mesh) == snapshot, "skeletal models refuse mirroring",
			   error);
	}
	// Presets.
	{
		const auto mesh = propMesh();
		ModelVec3 offset;
		expect(modelOriginOffset(mesh, 0, ModelOriginTarget::BoundsBottom, {}, &offset, &error) && close(offset, {-8, -4, 0}),
			   "Origin to Bounds Bottom puts the feet on the origin");
		expect(modelOriginOffset(mesh, 1, ModelOriginTarget::BoundsCentre, {}, &offset, &error) && close(offset, {-8, -4, -10}),
			   "Origin to Bounds Centre measures the chosen pose");
		double factor = 0;
		expect(modelHeightScale(mesh, 0, 56, &factor, &error) && near(factor, 14), "Scale to Height 56 scales a 4-unit model by 14");
		ModelEdit frameOnly = modelEdit({1, 0, 0}, {}, {1, 1, 1});
		frameOnly.frame = 0;
		auto copy = mesh;
		expect(!applyModelEdit(&copy, frameOnly, nullptr, &error), "Transform Model refuses a single-frame scope");
	}
}

// ---- Connect -----------------------------------------------------------------

ModelEdit connectEdit(ModelEditKind kind, ModelSelection selection)
{
	ModelEdit edit;
	edit.kind = kind;
	edit.selection = selection;
	return edit;
}
int boundaryEdges(const ModelSurface &surface)
{
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology))
		return -1;
	int count = 0;
	for (auto it = topology.edgeFaces.cbegin(); it != topology.edgeFaces.cend(); ++it)
	{
		if (it.value().size() > 2)
			return -1;
		count += it.value().size() == 1;
	}
	return count;
}
bool hasEdge(const ModelSurface &surface, int a, int b)
{
	for (const auto &t : surface.triangles)
		for (const auto &[x, y] : {std::pair{t.a, t.b}, std::pair{t.b, t.c}, std::pair{t.c, t.a}})
			if (modelEdge(x, y) == modelEdge(a, b))
				return true;
	return false;
}

void connectChecks()
{
	QString error;
	// Opposite corners of a quad: the diagonal turns, no vertex is added.
	{
		auto mesh = gridMesh(1, 1);
		ModelSelection selection{0, {1, 2}, {}};
		expect(applyModelEdit(&mesh, connectEdit(ModelEditKind::ConnectVertices, selection), &selection, &error), "connect opposite quad corners", error);
		const auto &s = mesh.surfaces[0];
		expect(s.vertexCount == 4 && s.triangles.size() == 2 && hasEdge(s, 1, 2) && !hasEdge(s, 0, 3) && selection.edges.contains(modelEdge(1, 2)),
			   "the new edge joins the corners and replaces the old diagonal");
		ModelSelection adjacent{0, {0, 1}, {}};
		expect(!applyModelEdit(&mesh, connectEdit(ModelEditKind::ConnectVertices, adjacent), &adjacent, &error), "vertices on one edge refuse");
	}
	// A three-vertex path across a grid, in three poses.
	{
		auto mesh = gridMesh(3, 3);
		mesh.frames << mesh.frames[1];
		mesh.frames[2].name = QStringLiteral("pose3");
		auto pose = mesh.surfaces[0].frames[0];
		for (auto &p : pose.positions)
			p.z = p.x * 0.5f;
		mesh.surfaces[0].frames << pose;
		updateEditableModelMetadata(&mesh);
		auto edit = connectEdit(ModelEditKind::ConnectVertices, {0, {0, 7, 14}, {}});
		edit.tool.topology.vertices = {0, 7, 14};
		ModelSelection selection;
		ModelDocument document;
		expect(document.setMesh(mesh, &error) && document.edit(edit, &error) && document.undoSteps() == 1, "connect a three-vertex path", error);
		const auto &s = document.mesh().surfaces[0];
		bool onPath = s.vertexCount == 22, poses = true;
		for (int v = 16; v < s.vertexCount; ++v)
		{
			const auto p = s.frames[0].positions[v];
			onPath &= near(p.y, p.x / 3, 1e-3) || near(2 * (24 - p.x), p.y - 8, 1e-3);
			poses &= near(s.frames[1].positions[v].z, 8) && near(s.frames[2].positions[v].z, p.x * 0.5, 1e-3);
		}
		expect(onPath && poses, "six new vertices sit on the two straight legs, in every pose");
		expect(boundaryEdges(s) == 12, "the cut leaves no T-junction or open edge");
		expect(document.selection().edges.size() == 8, "the eight new path edges are selected");
		expect(document.undo() && document.mesh().surfaces[0].vertexCount == 16, "one undo removes the path");
	}
	// Across a UV seam, each side keeps its own UVs.
	{
		const QVector<ModelVec3> p{{0, 0, 0}, {8, 0, 0}, {8, 8, 0}, {0, 8, 0}, {8, 0, 0}, {16, 0, 0}, {16, 8, 0}, {8, 8, 0}};
		const QVector<ModelTexCoord> uv{{0, 0}, {0.5f, 0}, {0.5f, 1}, {0, 1}, {0.6f, 0}, {1, 0}, {1, 1}, {0.6f, 1}};
		auto mesh = meshOf(p, {{0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7}}, uv);
		ModelSelection selection{0, {0, 6}, {}};
		expect(applyModelEdit(&mesh, connectEdit(ModelEditKind::ConnectVertices, selection), &selection, &error), "connect across a UV seam", error);
		const auto &s = mesh.surfaces[0];
		int left = 0, right = 0;
		for (int v = 8; v < s.vertexCount; ++v)
		{
			const auto q = s.frames[0].positions[v];
			if (near(q.x, 8) && near(q.y, 4))
			{
				left += near(s.texCoords[v].u, 0.5) && near(s.texCoords[v].v, 0.5);
				right += near(s.texCoords[v].u, 0.6) && near(s.texCoords[v].v, 0.5);
			}
		}
		expect(s.vertexCount == 10 && left == 1 && right == 1, "the seam point has one copy per side with that side's UVs");
	}
	// Skin influences interpolate along the cut.
	{
		auto mesh = riggedMesh();
		auto edit = connectEdit(ModelEditKind::ConnectVertices, {0, {1, 6}, {}});
		ModelSelection selection;
		expect(applyModelEdit(&mesh, edit, &selection, &error), "connect on a skinned surface", error);
		const auto &s = mesh.surfaces[0];
		bool found = false;
		for (int v = 9; v < s.vertexCount; ++v)
		{
			const auto q = s.frames[0].positions[v];
			if (!near(q.x, 4) || !near(q.y, 8))
				continue;
			double child = 0, total = 0;
			for (int i = 0; i < s.skinning.count[v]; ++i)
			{
				const auto &influence = s.skinning.influences[s.skinning.first[v] + i];
				total += influence.weight;
				child += influence.joint == 1 ? influence.weight : 0;
			}
			found = near(total, 1, 1e-4) && near(child, 0.35, 1e-4);
		}
		expect(found, "the point halfway along an edge takes the blended weights");
	}
	// Two separate pieces cannot connect.
	{
		const QVector<ModelVec3> p{{0, 0, 0}, {8, 0, 0}, {8, 8, 0}, {0, 8, 0}, {20, 0, 0}, {28, 0, 0}, {28, 8, 0}, {20, 8, 0}};
		auto mesh = meshOf(p, {{0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7}});
		const auto before = bytesOf(mesh);
		ModelSelection selection{0, {0, 6}, {}};
		expect(!applyModelEdit(&mesh, connectEdit(ModelEditKind::ConnectVertices, selection), &selection, &error) && bytesOf(mesh) == before,
			   "vertices on separate pieces are refused atomically", error);
	}
	// Connect Edges: two cuts across a partial ring, slid along it.
	{
		auto mesh = gridMesh(3, 1);
		const ModelSelection ring{0, {}, {}, {modelEdge(0, 4), modelEdge(1, 5), modelEdge(2, 6)}};
		auto edit = connectEdit(ModelEditKind::ConnectEdges, ring);
		edit.tool.cuts = 2;
		auto plain = mesh;
		ModelSelection selection;
		expect(applyModelEdit(&plain, edit, &selection, &error), "connect two quads of a ring", error);
		const auto &s = plain.surfaces[0];
		bool even = s.vertexCount == 14 && s.triangles.size() == 16;
		for (int v = 8; v < s.vertexCount; ++v)
			even &= near(s.frames[0].positions[v].y, 8.0 / 3, 1e-4) || near(s.frames[0].positions[v].y, 16.0 / 3, 1e-4);
		expect(even, "six evenly spaced points and no diagonal vertices");
		expect(boundaryEdges(s) == 10 && selection.edges.size() == 4, "the ring's end face takes the points too, and the four cuts are selected");
		edit.tool.slide = 0.5;
		expect(applyModelEdit(&mesh, edit, &selection, &error), "connect with a slide", error);
		bool slid = true;
		for (int v = 8; v < mesh.surfaces[0].vertexCount; ++v)
			slid &= near(mesh.surfaces[0].frames[0].positions[v].y, 4, 1e-4) || near(mesh.surfaces[0].frames[0].positions[v].y, 8 * (2.5 / 3), 1e-4);
		expect(slid, "a slide moves every cut the same way along the ring");
		auto single = gridMesh(3, 1);
		auto lone = connectEdit(ModelEditKind::ConnectEdges, {0, {}, {}, {modelEdge(0, 4)}});
		expect(!applyModelEdit(&single, lone, nullptr, &error), "one side of a quad has nothing to connect to");
	}
}

// ---- Delete and dissolve ---------------------------------------------------

// A closed, fully shared cube: 8 vertices and 12 outward triangles.
ModelMesh cubeMesh(float h = 8)
{
	const QVector<ModelVec3> p{{-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h}, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}};
	const QVector<ModelTriangle> t{{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
								   {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}};
	auto mesh = meshOf(p, t);
	for (auto &frame : mesh.surfaces[0].frames)
		for (int v = 0; v < 8; ++v)
		{
			const auto q = p[v];
			frame.normals[v] = {q.x / (h * 1.7320508f), q.y / (h * 1.7320508f), q.z / (h * 1.7320508f)};
		}
	return mesh;
}
ModelEdit toolOf(ModelEditKind kind, ModelSelection selection)
{
	ModelEdit edit;
	edit.kind = kind;
	edit.selection = selection;
	return edit;
}

void dissolveChecks()
{
	QString error;
	// Loop-cut a cube, then dissolve the loop: back to the cube.
	{
		auto mesh = cubeMesh();
		ModelSelection selection;
		auto cut = toolOf(ModelEditKind::LoopCut, {0, {}, {}, {modelEdge(0, 4)}});
		expect(applyModelEdit(&mesh, cut, &selection, &error) && mesh.surfaces[0].triangles.size() == 20 && selection.edges.size() == 4,
			   "loop-cut the cube around its sides", error);
		auto dissolve = toolOf(ModelEditKind::DissolveEdges, selection);
		ModelDocument document;
		expect(document.setMesh(mesh, &error) && document.edit(dissolve, &error), "dissolve the loop's edges", error);
		const auto &s = document.mesh().surfaces[0];
		expect(s.triangles.size() == 12 && s.vertexCount == 8 && boundaryEdges(s) == 0, "the loop's faces merge and its vertices go: 12 triangles again");
		ModelSelectRequest request;
		request.operation = ModelSelectOperation::Loop;
		request.mode = ModelSelectionMode::Edges;
		ModelSelection loop;
		const auto &t = s.triangles[4];
		request.edge = modelEdge(t.a, t.b);
		expect(selectModelComponents(document.mesh(), {}, request, &loop, &error) && !loop.edges.isEmpty(), "edge loops still select on the result", error);
		expect(document.undo() && document.mesh().surfaces[0].triangles.size() == 20, "one undo brings the loop back");
		auto keep = dissolve;
		keep.tool.topology.dissolveVertices = false;
		auto kept = mesh;
		expect(applyModelEdit(&kept, keep, nullptr, &error) && kept.surfaces[0].vertexCount == 12, "keeping vertices leaves them on the merged faces",
			   error);
	}
	// Limited dissolve of a subdivided plane leaves two triangles.
	{
		auto mesh = gridMesh(4, 4);
		ModelSelection selection;
		expect(applyModelEdit(&mesh, toolOf(ModelEditKind::LimitedDissolve, {0, {}, {}}), &selection, &error) &&
				   mesh.surfaces[0].triangles.size() == 2 && mesh.surfaces[0].vertexCount == 4,
			   "limited dissolve of a flat grid gives two triangles", error);
		bool corners = true;
		for (const auto &p : mesh.surfaces[0].frames[0].positions)
			corners &= (near(p.x, 0) || near(p.x, 32)) && (near(p.y, 0) || near(p.y, 32));
		expect(corners && selection.faces.size() == 2, "only the corners remain and the new faces are selected");
		auto again = mesh;
		expect(!applyModelEdit(&again, toolOf(ModelEditKind::LimitedDissolve, {0, {}, {}}), nullptr, &error), "nothing left to simplify is refused");
	}
	// Limited dissolve keeps UV islands apart.
	{
		QVector<ModelVec3> p;
		QVector<ModelTexCoord> uv;
		for (int island = 0; island < 2; ++island)
			for (int j = 0; j <= 1; ++j)
				for (int i = 0; i <= 2; ++i)
				{
					p.append({float(island * 16 + i * 8), float(j * 8), 0});
					uv.append({float(island) * 0.5f + float(i) * 0.2f, float(j)});
				}
		QVector<ModelTriangle> t;
		for (int island = 0; island < 2; ++island)
			for (int i = 0; i < 2; ++i)
			{
				const int base = island * 6;
				t.append({base + i, base + i + 1, base + i + 4});
				t.append({base + i, base + i + 4, base + i + 3});
			}
		auto mesh = meshOf(p, t, uv);
		expect(applyModelEdit(&mesh, toolOf(ModelEditKind::LimitedDissolve, {0, {}, {}}), nullptr, &error) && mesh.surfaces[0].triangles.size() == 4 &&
				   mesh.surfaces[0].vertexCount == 8,
			   "each UV island simplifies on its own", error);
		bool kept = true;
		for (int v = 0; v < mesh.surfaces[0].vertexCount; ++v)
		{
			const auto q = mesh.surfaces[0].frames[0].positions[v];
			const int island = q.x > 16.5f || (near(q.x, 16) && mesh.surfaces[0].texCoords[v].u > 0.45f) ? 1 : 0;
			const int i = int(std::lround((q.x - island * 16) / 8));
			kept &= near(mesh.surfaces[0].texCoords[v].u, island * 0.5 + i * 0.2) && near(mesh.surfaces[0].frames[1].positions[v].z, 8);
		}
		expect(kept, "remaining vertices keep their UVs and poses");
	}
	// Delete Vertices removes the vertex's whole fan; Delete Edges the faces on an edge.
	{
		auto mesh = gridMesh(2, 2);
		ModelSelection selection;
		auto vertices = toolOf(ModelEditKind::DeleteGeometry, {0, {4}, {}});
		vertices.tool.topology.deleteMode = ModelDeleteMode::Vertices;
		expect(applyModelEdit(&mesh, vertices, &selection, &error) && mesh.surfaces[0].triangles.size() == 2 && mesh.surfaces[0].vertexCount == 6,
			   "deleting the centre vertex removes its six faces", error);
		auto edges = gridMesh(2, 2);
		auto byEdge = toolOf(ModelEditKind::DeleteGeometry, {0, {}, {}, {modelEdge(1, 4)}});
		byEdge.tool.topology.deleteMode = ModelDeleteMode::Edges;
		expect(applyModelEdit(&edges, byEdge, nullptr, &error) && edges.surfaces[0].triangles.size() == 6, "deleting an edge removes both its faces", error);
		auto all = gridMesh(1, 1);
		const auto before = bytesOf(all);
		auto everything = toolOf(ModelEditKind::DeleteGeometry, {0, {0}, {}});
		everything.tool.topology.deleteMode = ModelDeleteMode::Vertices;
		expect(!applyModelEdit(&all, everything, nullptr, &error) && bytesOf(all) == before, "a delete that empties the surface is refused atomically");
	}
	// Dissolve Edges refuses seams, borders and non-manifold edges.
	{
		const QVector<ModelVec3> p{{0, 0, 0}, {8, 0, 0}, {8, 8, 0}, {0, 8, 0}, {8, 0, 0}, {16, 0, 0}, {16, 8, 0}, {8, 8, 0}};
		const QVector<ModelTexCoord> uv{{0, 0}, {0.5f, 0}, {0.5f, 1}, {0, 1}, {0.6f, 0}, {1, 0}, {1, 1}, {0.6f, 1}};
		auto seam = meshOf(p, {{0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7}}, uv);
		expect(!applyModelEdit(&seam, toolOf(ModelEditKind::DissolveEdges, {0, {}, {}, {modelEdge(1, 2)}}), nullptr, &error) && error.contains("UV"),
			   "a UV seam edge is refused", error);
		auto grid = gridMesh(1, 1);
		expect(!applyModelEdit(&grid, toolOf(ModelEditKind::DissolveEdges, {0, {}, {}, {modelEdge(0, 1)}}), nullptr, &error), "a border edge is refused");
		auto fin = gridMesh(1, 1);
		auto &s = fin.surfaces[0];
		s.vertexCount = 5;
		s.texCoords.append({0, 0});
		for (int f = 0; f < 2; ++f)
		{
			s.frames[f].positions.append({4, 4, 8.0f + f * 8});
			s.frames[f].normals.append({0, 0, 1});
		}
		s.triangles.append({0, 3, 4});
		updateEditableModelMetadata(&fin);
		expect(!applyModelEdit(&fin, toolOf(ModelEditKind::DissolveEdges, {0, {}, {}, {modelEdge(0, 3)}}), nullptr, &error),
			   "an edge with three faces is refused");
	}
	// Skinned surfaces keep complete influences.
	{
		auto mesh = riggedMesh();
		expect(applyModelEdit(&mesh, toolOf(ModelEditKind::LimitedDissolve, {0, {}, {}}), nullptr, &error) &&
				   mesh.surfaces[0].skinning.first.size() == mesh.surfaces[0].vertexCount && validateEditableModel(mesh).isEmpty(),
			   "limited dissolve on a skinned surface keeps each remaining vertex's influences", error);
	}
}

// ---- Recalculate Outside ----------------------------------------------------

// A cube whose six sides each have their own four vertices (UV seams on every
// edge), centred on `centre`.
ModelMesh seamedCube(ModelVec3 centre = {0, 0, 0}, float h = 8)
{
	QVector<ModelVec3> p;
	QVector<ModelTriangle> t;
	QVector<ModelTexCoord> uv;
	for (int axis = 0; axis < 3; ++axis)
		for (int sign : {-1, 1})
		{
			const int u = (axis + 1) % 3, v = (axis + 2) % 3;
			const int base = p.size();
			for (const auto &[a, b] : {std::pair{-1, -1}, std::pair{1, -1}, std::pair{1, 1}, std::pair{-1, 1}})
			{
				float c[3]{};
				c[axis] = sign * h;
				c[u] = a * h;
				c[v] = b * h;
				p.append({centre.x + c[0], centre.y + c[1], centre.z + c[2]});
				uv.append({(a + 1) * 0.25f + (axis + 1) * 0.1f, (b + 1) * 0.25f + (sign > 0 ? 0.5f : 0)});
			}
			// Counter-clockwise seen from outside: (u, v, axis) is right-handed.
			if (sign > 0)
				t << ModelTriangle{base, base + 1, base + 2} << ModelTriangle{base, base + 2, base + 3};
			else
				t << ModelTriangle{base, base + 2, base + 1} << ModelTriangle{base, base + 3, base + 2};
		}
	auto mesh = meshOf(p, t, uv);
	// Hard edges: each side's copies take that side's normal.
	for (auto &frame : mesh.surfaces[0].frames)
		for (int v = 0; v < frame.normals.size(); ++v)
		{
			const int side = v / 4, axis = side / 2;
			const float sign = side % 2 ? 1.0f : -1.0f;
			frame.normals[v] = {axis == 0 ? sign : 0.0f, axis == 1 ? sign : 0.0f, axis == 2 ? sign : 0.0f};
		}
	return mesh;
}
// Every face's normal points away from `centre`, in every pose.
bool outward(const ModelMesh &mesh, ModelVec3 centre, int surface = 0)
{
	const auto &s = mesh.surfaces[surface];
	for (int f = 0; f < s.frames.size(); ++f)
		for (const auto &t : s.triangles)
		{
			const auto a = s.frames[f].positions[t.a], b = s.frames[f].positions[t.b], c = s.frames[f].positions[t.c];
			const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
			const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
			const double cx = (a.x + b.x + c.x) / 3 - centre.x, cy = (a.y + b.y + c.y) / 3 - centre.y, cz = (a.z + b.z + c.z) / 3 - centre.z - f * 8;
			if (nx * cx + ny * cy + nz * cz <= 0)
				return false;
			const auto n = s.frames[f].normals[t.a];
			if (n.x * cx + n.y * cy + n.z * cz <= 0)
				return false;
		}
	return true;
}

void outsideChecks()
{
	QString error;
	// A seamed cube with faces flipped on several sides ends fully outward.
	{
		auto mesh = seamedCube();
		expect(outward(mesh, {}), "the seamed cube starts outward");
		for (int face : {0, 3, 4, 7, 10})
			std::swap(mesh.surfaces[0].triangles[face].b, mesh.surfaces[0].triangles[face].c);
		ModelDocument document;
		expect(document.setMesh(mesh, &error) && document.edit(toolOf(ModelEditKind::RecalculateOutside, {0, {}, {}}), &error),
			   "recalculate outside across UV seams", error);
		expect(outward(document.mesh(), {}) && document.undoSteps() == 1, "every face points outward in both poses, with normals to match");
		auto again = document.mesh();
		expect(!applyModelEdit(&again, toolOf(ModelEditKind::RecalculateOutside, {0, {}, {}}), nullptr, &error), "an outward cube has nothing to change");
		auto inside = toolOf(ModelEditKind::RecalculateOutside, {0, {}, {}});
		inside.tool.topology.inside = true;
		expect(applyModelEdit(&again, inside, nullptr, &error) && !outward(again, {}), "Recalculate Inside turns every face inward", error);
	}
	// An inverted closed piece, and a second piece beside it.
	{
		auto mesh = seamedCube();
		auto other = seamedCube({40, 0, 0});
		auto &s = mesh.surfaces[0];
		const int offset = s.vertexCount;
		for (auto t : other.surfaces[0].triangles)
			s.triangles.append({t.a + offset, t.c + offset, t.b + offset});
		for (int f = 0; f < 2; ++f)
		{
			s.frames[f].positions += other.surfaces[0].frames[f].positions;
			s.frames[f].normals += other.surfaces[0].frames[f].normals;
		}
		s.texCoords += other.surfaces[0].texCoords;
		s.vertexCount += offset;
		updateEditableModelMetadata(&mesh);
		expect(applyModelEdit(&mesh, toolOf(ModelEditKind::RecalculateOutside, {0, {}, {}}), nullptr, &error), "fix an inverted second piece", error);
		ModelMesh first = mesh, second = mesh;
		first.surfaces[0].triangles = mesh.surfaces[0].triangles.mid(0, 12);
		second.surfaces[0].triangles = mesh.surfaces[0].triangles.mid(12);
		expect(outward(first, {}) && outward(second, {40, 0, 0}), "each piece faces out of its own inside");
	}
	// An open cup (a box without its top), built inside out.
	{
		auto mesh = seamedCube();
		auto &s = mesh.surfaces[0];
		s.triangles.remove(10, 2);
		for (auto &t : s.triangles)
			std::swap(t.b, t.c);
		expect(applyModelEdit(&mesh, toolOf(ModelEditKind::RecalculateOutside, {0, {}, {}}), nullptr, &error) && outward(mesh, {0, 0, 2}),
			   "an open cup is turned to face outward", error);
	}
	// A one-sided strip is refused.
	{
		QVector<ModelVec3> p;
		for (int i = 0; i < 5; ++i)
		{
			const double angle = i * 2 * 3.14159265358979 / 5;
			p << ModelVec3{float(10 * std::cos(angle)), float(10 * std::sin(angle)), 4} << ModelVec3{float(10 * std::cos(angle)), float(10 * std::sin(angle)), -4};
		}
		QVector<ModelTriangle> t;
		for (int i = 0; i < 4; ++i)
		{
			const int top = 2 * i, bottom = 2 * i + 1, nextTop = 2 * i + 2, nextBottom = 2 * i + 3;
			t << ModelTriangle{top, bottom, nextBottom} << ModelTriangle{top, nextBottom, nextTop};
		}
		// The closing quad joins with a half twist: top meets bottom.
		t << ModelTriangle{8, 9, 0} << ModelTriangle{8, 0, 1};
		auto mesh = meshOf(p, t);
		const auto before = bytesOf(mesh);
		expect(!applyModelEdit(&mesh, toolOf(ModelEditKind::RecalculateOutside, {0, {}, {}}), nullptr, &error) && bytesOf(mesh) == before &&
				   error.contains(QStringLiteral("Möbius")),
			   "a one-sided strip is refused atomically", error);
	}
}

// ---- Mark and clear sharp ---------------------------------------------------

// The edges around the cube's top face (z = h), as index pairs of every copy.
QSet<ModelEdge> topEdges(const ModelSurface &surface, float h = 8)
{
	QSet<ModelEdge> edges;
	for (const auto &t : surface.triangles)
		for (const auto &[a, b] : {std::pair{t.a, t.b}, std::pair{t.b, t.c}, std::pair{t.c, t.a}})
		{
			const auto p = surface.frames[0].positions[a], q = surface.frames[0].positions[b];
			if (near(p.z, h) && near(q.z, h) && (near(p.x, q.x) || near(p.y, q.y)))
				edges.insert(modelEdge(a, b));
		}
	return edges;
}

void sharpChecks()
{
	QString error;
	// Marking the top face's edges splits each top corner into a top and a side vertex.
	{
		auto mesh = cubeMesh();
		ModelSelection selection;
		auto markEdit = toolOf(ModelEditKind::MarkSharp, {0, {}, {}, topEdges(mesh.surfaces[0])});
		ModelDocument document;
		expect(document.setMesh(mesh, &error) && document.edit(markEdit, &error) && document.undoSteps() == 1, "mark the top edges sharp", error);
		const auto &s = document.mesh().surfaces[0];
		expect(s.vertexCount == 12, "each of the four top corners gains one vertex");
		bool normals = true;
		for (int face = 0; face < s.triangles.size(); ++face)
		{
			const auto &t = s.triangles[face];
			bool top = true;
			for (int v : {t.a, t.b, t.c})
				top &= near(s.frames[0].positions[v].z, 8);
			for (int v : {t.a, t.b, t.c})
				for (int f = 0; f < 2; ++f)
				{
					const auto n = s.frames[f].normals[v];
					if (top)
						normals &= near(n.z, 1, 1e-4);
					else if (near(s.frames[0].positions[v].z, 8))
						normals &= near(n.z, 0, 1e-4) && near(std::hypot(n.x, n.y), 1, 1e-4);
				}
		}
		expect(normals, "the top keeps a flat normal and the sides a horizontal one, in every pose");
		auto clearEdit = toolOf(ModelEditKind::ClearSharp, {0, {}, {}, topEdges(s)});
		expect(document.edit(clearEdit, &error) && document.mesh().surfaces[0].vertexCount == 8, "clearing welds the copies back", error);
		const auto n = document.mesh().surfaces[0].frames[0].normals[6];
		expect(n.x > 0.3f && n.y > 0.3f && n.z > 0.3f, "the corner is smooth again");
		auto again = document.mesh();
		expect(!applyModelEdit(&again, toolOf(ModelEditKind::ClearSharp, {0, {}, {}, topEdges(again.surfaces[0])}), nullptr, &error),
			   "clearing smooth edges changes nothing and is refused");
	}
	// On a UV-seamed cube, clearing smooths normals but keeps every UV copy.
	{
		auto mesh = seamedCube();
		auto clearEdit = toolOf(ModelEditKind::ClearSharp, {0, {}, {}, topEdges(mesh.surfaces[0])});
		expect(applyModelEdit(&mesh, clearEdit, nullptr, &error) && mesh.surfaces[0].vertexCount == 24, "clear sharp across UV seams", error);
		const auto &s = mesh.surfaces[0];
		bool smooth = true;
		for (int v = 0; v < s.vertexCount; ++v)
		{
			const auto p = s.frames[0].positions[v], n = s.frames[0].normals[v];
			if (near(p.z, 8) && near(p.x, 8) && near(p.y, 8))
				smooth &= n.x > 0.3f && n.z > 0.3f;
		}
		expect(smooth, "every top corner copy shares the smooth normal");
		auto border = gridMesh(1, 1);
		expect(!applyModelEdit(&border, toolOf(ModelEditKind::MarkSharp, {0, {}, {}, {modelEdge(0, 1)}}), nullptr, &error), "a border edge is refused");
	}
}

// ---- Target weld, auto-merge and explicit positions -------------------------

// The top corners' normals point straight up on the top face and sideways on the sides.
bool topSplit(const ModelSurface &s)
{
	bool ok = s.vertexCount == 12;
	for (const auto &t : s.triangles)
	{
		bool top = true;
		for (int v : {t.a, t.b, t.c})
			top &= near(s.frames[0].positions[v].z, 8);
		for (int v : {t.a, t.b, t.c})
		{
			const auto n = s.frames[0].normals[v];
			if (top)
				ok &= near(n.z, 1, 1e-4);
			else if (near(s.frames[0].positions[v].z, 8))
				ok &= near(n.z, 0, 1e-4);
		}
	}
	return ok;
}

void sharpPersistenceChecks()
{
	QString error;
	auto mesh = cubeMesh();
	expect(applyModelEdit(&mesh, toolOf(ModelEditKind::MarkSharp, {0, {}, {}, topEdges(mesh.surfaces[0])}), nullptr, &error) &&
			   mesh.surfaces[0].sharpEdges.size() == 4,
		   "Mark Sharp stores the four top edges", error);
	// The editable source keeps them (version 16), readable and packed.
	const auto json = editableModelJson(mesh);
	expect(json.value("version").toInt() == 16 && json.value("surfaces").toArray().first().toObject().value("sharpEdges").toArray().size() == 4,
		   "the source writes sharpEdges at version 16");
	for (auto encoding : {ModelSourceEncoding::Readable, ModelSourceEncoding::Packed})
	{
		ModelMesh reloaded;
		const auto bytes = serializeEditableModel(mesh, &error, {}, encoding);
		expect(parseEditableModel(bytes, &reloaded, &error) && reloaded.surfaces[0].sharpEdges == mesh.surfaces[0].sharpEdges,
			   "sharp edges survive a save and reload", error);
	}
	// Recalculate Normals and Shade Smooth keep the marked edges hard.
	ModelEdit recalc;
	recalc.kind = ModelEditKind::RecalculateNormals;
	auto recalculated = mesh;
	expect(applyModelEdit(&recalculated, recalc, nullptr, &error) && topSplit(recalculated.surfaces[0]), "Recalculate Normals keeps the top hard", error);
	auto smooth = toolOf(ModelEditKind::ShadeSmooth, {0, {}, {}});
	for (int face = 0; face < 12; ++face)
		smooth.selection.faces.insert(face);
	auto smoothed = mesh;
	expect(applyModelEdit(&smoothed, smooth, nullptr, &error) && topSplit(smoothed.surfaces[0]) && smoothed.surfaces[0].sharpEdges.size() == 4,
		   "Shade Smooth welds, then splits again along the stored edges", error);
	// Edits that renumber vertices keep the marks on the same edges.
	auto poked = mesh;
	auto poke = toolOf(ModelEditKind::PokeFaces, {0, {}, {0}});
	expect(applyModelEdit(&poked, poke, nullptr, &error) && poked.surfaces[0].sharpEdges.size() == 4 && applyModelEdit(&poked, recalc, nullptr, &error),
		   "the marks survive a topology edit elsewhere", error);
	// A smoothing group of the top face is hard against the sides.
	auto grouped = cubeMesh();
	auto group = toolOf(ModelEditKind::MarkSharp, {0, {}, {2, 3}});
	group.tool.topology.smoothingGroup = true;
	expect(applyModelEdit(&grouped, group, nullptr, &error) && topSplit(grouped.surfaces[0]) && grouped.surfaces[0].sharpEdges.size() == 4,
		   "a smoothing group of the top face marks its border sharp", error);
	auto noFaces = toolOf(ModelEditKind::MarkSharp, {0, {}, {}, topEdges(cubeMesh().surfaces[0])});
	noFaces.tool.topology.smoothingGroup = true;
	auto plain = cubeMesh();
	expect(!applyModelEdit(&plain, noFaces, nullptr, &error), "a smoothing group needs faces");
	// Clearing removes the marks; MD3 export carries the split normals.
	auto cleared = mesh;
	expect(applyModelEdit(&cleared, toolOf(ModelEditKind::ClearSharp, {0, {}, {}, topEdges(cleared.surfaces[0])}), nullptr, &error) &&
			   cleared.surfaces[0].sharpEdges.isEmpty() && editableModelJson(cleared).value("version").toInt() < 16,
		   "Clear Sharp removes the marks and the source drops back below version 16", error);
	auto native = mesh;
	native.surfaces[0].skinPaths = {QStringLiteral("models/cube/skin.pcx")};
	const auto md3 = exportEditableModel(native, QStringLiteral("md3"), 0, &error);
	const auto restored = decodeModelMesh(QStringLiteral("cube.md3"), md3);
	bool upright = false, sideways = false;
	if (restored.isValid())
		for (int v = 0; v < restored.surfaces[0].vertexCount; ++v)
		{
			const auto p = restored.surfaces[0].frames[0].positions[v], n = restored.surfaces[0].frames[0].normals[v];
			if (near(p.z, 8, 0.05) && near(p.x, 8, 0.05) && near(p.y, 8, 0.05))
			{
				upright |= n.z > 0.95f;
				sideways |= std::abs(n.z) < 0.05f;
			}
		}
	expect(restored.isValid() && upright && sideways, "the MD3 keeps a hard top edge as two normals at the corner", error);
}

void weldChecks()
{
	QString error;
	// Merge at Target keeps the target's position, UV and normal.
	{
		auto mesh = gridMesh(2, 1);
		mesh.surfaces[0].frames[0].normals[4] = {0, 0.6f, 0.8f};
		auto edit = toolOf(ModelEditKind::MergeVertices, {0, {5}, {}});
		edit.tool.merge = ModelMergeMode::Target;
		edit.tool.topology.targetVertex = 4;
		ModelSelection selection;
		ModelDocument document;
		expect(document.setMesh(mesh, &error) && document.edit(edit, &error) && document.undoSteps() == 1, "weld a vertex onto a target", error);
		const auto &s = document.mesh().surfaces[0];
		expect(s.vertexCount == 5 && s.triangles.size() == 3, "the source vertex and the face it collapsed are gone");
		const auto p = s.frames[0].positions[4], q = s.frames[1].positions[4], n = s.frames[0].normals[4];
		expect(near(p.x, 8) && near(p.y, 8) && near(q.z, 8) && near(s.texCoords[4].u, 8.0 / 64) && near(n.y, 0.6, 1e-4),
			   "the target keeps its position in every pose, its UV and its normal");
		expect(document.selection().vertices == QSet<int>{4}, "the target is selected");
	}
	// Across a UV seam, each side keeps its own copy of the target.
	{
		const QVector<ModelVec3> p{{0, 0, 0}, {8, 0, 0}, {8, 8, 0}, {0, 8, 0}, {8, 0, 0}, {16, 0, 0}, {16, 8, 0}, {8, 8, 0}};
		const QVector<ModelTexCoord> uv{{0, 0}, {0.5f, 0}, {0.5f, 1}, {0, 1}, {0.6f, 0}, {1, 0}, {1, 1}, {0.6f, 1}};
		auto mesh = meshOf(p, {{0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7}}, uv);
		auto edit = toolOf(ModelEditKind::MergeVertices, {0, {6}, {}});
		edit.tool.merge = ModelMergeMode::Target;
		edit.tool.topology.targetVertex = 2;
		expect(applyModelEdit(&mesh, edit, nullptr, &error), "weld across a seam", error);
		const auto &s = mesh.surfaces[0];
		bool rightCopy = false;
		for (const auto &t : s.triangles)
			for (int v : {t.a, t.b, t.c})
				rightCopy |= near(s.texCoords[v].u, 0.6) && near(s.texCoords[v].v, 1) && near(s.frames[0].positions[v].x, 8);
		expect(s.vertexCount == 7 && s.triangles.size() == 3 && rightCopy, "the right island welds onto its own copy of the target");
	}
	// Auto-merge after a move welds once; a weld that would make a fin is refused.
	{
		auto mesh = gridMesh(2, 1);
		ModelEdit move = toolOf(ModelEditKind::WeightedTransform, {0, {5}, {}});
		move.translation = {-7.9995f, 0, 0};
		move.pivotMode = ModelTransformPivot::SelectionCentre;
		move.tool.topology.autoMergeDistance = 0.01;
		ModelSelection selection;
		expect(applyModelEdit(&mesh, move, &selection, &error) && mesh.surfaces[0].vertexCount == 5 && mesh.surfaces[0].triangles.size() == 3,
			   "a vertex moved onto another welds to it", error);
		const QVector<ModelVec3> p{{0, 0, 0}, {8, 0, 0}, {0, 8, 0}, {8, 8, 0}, {100, 0, 0}, {108, 8, 0}, {104, 4, 6}};
		auto fin = meshOf(p, {{0, 1, 3}, {0, 3, 2}, {4, 5, 6}});
		const auto before = bytesOf(fin);
		ModelEdit slide = toolOf(ModelEditKind::WeightedTransform, {0, {4, 5, 6}, {}});
		slide.translation = {-100, 0, 0};
		slide.pivotMode = ModelTransformPivot::SelectionCentre;
		slide.tool.topology.autoMergeDistance = 0.01;
		expect(!applyModelEdit(&fin, slide, nullptr, &error) && bytesOf(fin) == before, "an auto-merge that joins three faces at an edge is refused", error);
		auto scoped = gridMesh(2, 1);
		move.frame = 0;
		expect(!applyModelEdit(&scoped, move, nullptr, &error), "auto-merge in one frame only is refused");
	}
	// Set Vertex Positions: all frames keep their offsets; one frame changes alone.
	{
		auto mesh = gridMesh(1, 1);
		auto place = toolOf(ModelEditKind::SetVertexPositions, {0, {}, {}});
		place.tool.topology.vertices = {3};
		place.tool.topology.positions = {{9, 9, 1}};
		auto all = mesh;
		expect(applyModelEdit(&all, place, nullptr, &error), "place a vertex in every pose", error);
		const auto a = all.surfaces[0].frames[0].positions[3], b = all.surfaces[0].frames[1].positions[3];
		expect(near(a.x, 9) && near(a.z, 1) && near(b.x, 9) && near(b.z, 9), "the reference pose takes the position and the other keeps its offset");
		place.frame = 1;
		auto one = mesh;
		expect(applyModelEdit(&one, place, nullptr, &error) && near(one.surfaces[0].frames[1].positions[3].z, 1) &&
				   near(one.surfaces[0].frames[0].positions[3].x, 8),
			   "a frame-scoped placement changes that pose only", error);
	}
}

// ---- Quad-preferring refill ----------------------------------------------------

void quadRefillChecks()
{
	QString error;
	// Dissolve the inner edges of two neighbouring cells, keeping the side
	// midpoints: the six-cornered polygon is filled again as two squares.
	auto mesh = gridMesh(3, 3);
	const auto &s = mesh.surfaces[0];
	QSet<ModelEdge> inner;
	QHash<ModelEdge, int> uses;
	for (int face : {0, 1, 2, 3})
	{
		const auto &t = s.triangles[face];
		for (auto e : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
			++uses[e];
	}
	for (auto it = uses.cbegin(); it != uses.cend(); ++it)
		if (it.value() == 2)
			inner.insert(it.key());
	auto edit = toolOf(ModelEditKind::DissolveEdges, {0, {}, {}, inner});
	edit.tool.topology.dissolveVertices = false;
	ModelSelection selection;
	expect(inner.size() == 3 && applyModelEdit(&mesh, edit, &selection, &error) && selection.faces.size() == 4, "dissolve two cells into one polygon",
		   error);
	const auto &after = mesh.surfaces[0];
	ModelQuadTopology quads;
	int squares = 0;
	if (expect(buildModelQuadTopology(after, 0, 40, &quads, &error), "pair the result into quads", error))
		for (const auto &element : std::as_const(quads.elements))
			if (element.quad() && selection.faces.contains(element.faces[0]) && selection.faces.contains(element.faces[1]))
				++squares;
	bool shared = false;
	for (int face : std::as_const(selection.faces))
	{
		const auto &t = after.triangles[face];
		for (const auto &[a, b] : {std::pair{t.a, t.b}, std::pair{t.b, t.c}, std::pair{t.c, t.a}})
		{
			const auto p = after.frames[0].positions[a], q = after.frames[0].positions[b];
			shared |= near(p.x, 8) && near(q.x, 8) && std::abs(p.y - q.y) > 7;
		}
	}
	expect(squares == 2 && shared, "the refill is two quads that share the side between the cells");
	// A 2x2 block keeping its side midpoints: an eight-cornered square. Ear
	// clipping leaves a diamond the quad view pairs only once; the refill
	// the quad view reads as the most quads pairs two of its three.
	auto block = gridMesh(3, 3);
	QSet<int> blockFaces;
	for (int cell : {0, 1, 3, 4})
		blockFaces << 2 * cell << 2 * cell + 1;
	QHash<ModelEdge, int> blockUses;
	for (int face : std::as_const(blockFaces))
	{
		const auto &bt = block.surfaces[0].triangles[face];
		for (auto e : {modelEdge(bt.a, bt.b), modelEdge(bt.b, bt.c), modelEdge(bt.c, bt.a)})
			++blockUses[e];
	}
	QSet<ModelEdge> blockInner;
	for (auto it = blockUses.cbegin(); it != blockUses.cend(); ++it)
		if (it.value() == 2)
			blockInner.insert(it.key());
	auto blockEdit = toolOf(ModelEditKind::DissolveEdges, {0, {}, {}, blockInner});
	blockEdit.tool.topology.dissolveVertices = false;
	ModelSelection refilled;
	expect(applyModelEdit(&block, blockEdit, &refilled, &error) && refilled.faces.size() == 6, "dissolve a 2x2 block into an eight-cornered polygon",
		   error);
	ModelQuadTopology blockQuads;
	int paired = 0;
	if (expect(buildModelQuadTopology(block.surfaces[0], 0, 40, &blockQuads, &error), "pair the block's refill", error))
		for (const auto &element : std::as_const(blockQuads.elements))
			if (element.quad() && refilled.faces.contains(element.faces[0]) && refilled.faces.contains(element.faces[1]))
				++paired;
	expect(paired == 2, "the block's refill pairs into two quads and one pair of triangles");
}

// ---- Intersection guard -------------------------------------------------------

ModelEdit deepInset(bool refuse)
{
	// The cube's top face, inset and pushed down through the bottom.
	auto edit = toolOf(ModelEditKind::InsetFaces, {0, {}, {2, 3}});
	edit.tool.insetThickness = 2;
	edit.tool.insetDepth = -20;
	edit.tool.topology.refuseIntersections = refuse;
	return edit;
}

void intersectionChecks()
{
	QString error;
	{
		auto mesh = cubeMesh();
		ModelEditReport report;
		ModelSelection selection;
		expect(applyModelEdit(&mesh, deepInset(false), &selection, &error, {}, &report), "an inset pushed through the bottom is kept", error);
		expect(!report.introducedIntersections.isEmpty() && !report.intersectionCheckLimited, "the edit reports the faces it made cross");
		bool valid = true;
		for (const auto &[first, second] : std::as_const(report.introducedIntersections))
			valid &= first < second && second < mesh.surfaces[0].triangles.size();
		expect(valid, "reported pairs name faces of the result, lower first");
		auto refused = cubeMesh();
		const auto before = bytesOf(refused);
		expect(!applyModelEdit(&refused, deepInset(true), nullptr, &error) && bytesOf(refused) == before && error.contains(QStringLiteral("cross")),
			   "Refuse New Intersections refuses the same edit atomically", error);
		auto shallow = cubeMesh();
		auto edit = deepInset(true);
		edit.tool.insetDepth = -2;
		ModelEditReport clean;
		expect(applyModelEdit(&shallow, edit, nullptr, &error, {}, &clean) && clean.introducedIntersections.isEmpty(),
			   "a shallow inset makes nothing cross and passes the guard", error);
	}
	// A crossing that was already there is not reported again.
	{
		auto mesh = cubeMesh();
		auto &s = mesh.surfaces[0];
		const int base = s.vertexCount;
		const QVector<ModelVec3> blade{{-12, 0, -4}, {12, 0, -4}, {12, 0, 4}, {-12, 0, 4}};
		for (int f = 0; f < 2; ++f)
			for (const auto &p : blade)
			{
				s.frames[f].positions.append({p.x, p.y, p.z + f * 8});
				s.frames[f].normals.append({0, 1, 0});
			}
		for (int i = 0; i < 4; ++i)
			s.texCoords.append({0, 0});
		s.vertexCount += 4;
		s.triangles << ModelTriangle{base, base + 1, base + 2} << ModelTriangle{base, base + 2, base + 3};
		updateEditableModelMetadata(&mesh);
		auto edit = toolOf(ModelEditKind::InsetFaces, {0, {}, {2, 3}});
		edit.tool.insetThickness = 2;
		ModelEditReport report;
		expect(applyModelEdit(&mesh, edit, nullptr, &error, {}, &report) && report.introducedIntersections.isEmpty(),
			   "a blade through the cube from before the edit is not reported", error);
	}
	// Other surfaces: a plate through the cube's walls (crossing them before the
	// edit) is crossed anew only by the walls of a shallow inset.
	{
		auto mesh = cubeMesh();
		const auto plate = meshOf({{-12, -12, 4}, {12, -12, 4}, {12, 12, 4}, {-12, 12, 4}}, {{0, 1, 2}, {0, 2, 3}});
		auto surface = plate.surfaces[0];
		surface.name = QStringLiteral("plate");
		mesh.surfaces << surface;
		updateEditableModelMetadata(&mesh);
		auto edit = toolOf(ModelEditKind::InsetFaces, {0, {}, {2, 3}});
		edit.tool.insetThickness = 2;
		edit.tool.insetDepth = -6;
		ModelEditReport report;
		auto kept = mesh;
		expect(applyModelEdit(&kept, edit, nullptr, &error, {}, &report) && report.introducedIntersections.isEmpty() &&
				   !report.introducedCrossings.isEmpty(),
			   "the inset's walls are reported crossing the plate", error);
		bool named = true;
		for (const auto &crossing : std::as_const(report.introducedCrossings))
		{
			const auto &s = kept.surfaces[0];
			const bool face = crossing[0] >= 0 && crossing[0] < s.triangles.size();
			// A face of the inset ring (|x|, |y| up to 6), not an old wall (8).
			bool inner = false;
			if (face)
				for (int v : {s.triangles[crossing[0]].a, s.triangles[crossing[0]].b, s.triangles[crossing[0]].c})
					inner |= std::max(std::abs(s.frames[0].positions[v].x), std::abs(s.frames[0].positions[v].y)) < 7.5f;
			named &= crossing[1] == 1 && crossing[2] >= 0 && crossing[2] < 2 && face && inner;
		}
		expect(named, "each crossing names a new face of the cube and a face of the plate; the old walls are not listed");
		auto refused = mesh;
		edit.tool.topology.refuseIntersections = true;
		const auto before = bytesOf(refused);
		expect(!applyModelEdit(&refused, edit, nullptr, &error) && bytesOf(refused) == before && error.contains(QStringLiteral("surface 1")),
			   "Refuse New Intersections refuses a crossing of another surface", error);
	}
	// Dense meshes: the guard scans only the faces an edit makes or moves.
	{
		const int cells = 128;
		QVector<ModelVec3> positions;
		QVector<ModelTriangle> triangles;
		for (int j = 0; j <= cells; ++j)
			for (int i = 0; i <= cells; ++i)
				positions.append({float(i), float(j), float(((i * 7 + j * 3) % 5) * 0.05)});
		const auto at = [&](int i, int j) { return j * (cells + 1) + i; };
		for (int j = 0; j < cells; ++j)
			for (int i = 0; i < cells; ++i)
			{
				triangles.append({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
				triangles.append({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
			}
		const auto dense = meshOf(positions, triangles);
		// The two faces of the middle cell.
		const int middle = 2 * ((cells / 2) * cells + cells / 2);
		auto edit = toolOf(ModelEditKind::InsetFaces, {0, {}, {middle, middle + 1}});
		edit.tool.insetThickness = 0.1;
		QElapsedTimer timer;
		auto plain = dense;
		timer.start();
		const bool plainOk = applyModelEdit(&plain, edit, nullptr, &error);
		const qint64 plainMs = timer.elapsed();
		auto guarded = dense;
		ModelEditReport report;
		timer.restart();
		const bool guardedOk = applyModelEdit(&guarded, edit, nullptr, &error, {}, &report);
		const qint64 guardedMs = timer.elapsed();
		ModelIntersectionReport full;
		timer.restart();
		const bool scanned = inspectModelIntersections(guarded, {}, &full, &error);
		const qint64 fullMs = timer.elapsed();
		expect(plainOk && guardedOk && scanned && report.introducedIntersections.isEmpty() && !report.intersectionCheckLimited && full.findings.isEmpty(),
			   "a dense grid's inset passes the guard", error);
		std::cout << "intersection guard on " << triangles.size() << " triangles in 2 poses: inset " << plainMs << " ms, inset with guard "
				  << guardedMs << " ms, whole-mesh scan " << fullMs << " ms\n";
	}
}

cli::ModelToolsCliResult runTool(const QStringList &arguments)
{
	return cli::runModelTool(QStringList{QStringLiteral("vibestudio"), QStringLiteral("model"), QStringLiteral("tool")} + arguments);
}

void alignCli(const QString &input, const QString &output)
{
	auto result = runTool({input, "--tool", "align", "--axis", "x", "--value", "0", "--vertices", "0,3", "--output", output, "--dry-run"});
	expect(result.exitCode == 0 && !QFileInfo::exists(output) && result.payload.value("align").toObject().value("axes").toString() == "x" &&
			   result.payload.value("align").toObject().value("target").toString() == "value",
		   "align --value validates without writing and reports its axes", result.error);
	result = runTool({input, "--tool", "align", "--axis", "z", "--to", "centroid", "--vertices", "0,5", "--frame", "1", "--output", output});
	expect(result.exitCode == 0 && QFileInfo::exists(output) && result.payload.value("align").toObject().value("frame").toInt() == 1,
		   "align --to centroid on one frame writes the result", result.error);
	result = runTool({input, "--tool", "snap-grid", "--grid", "3", "--vertices", "all", "--output", output, "--overwrite"});
	expect(result.exitCode == 0 && result.payload.value("align").toObject().value("grid").toDouble() == 3, "snap-grid writes with --grid",
		   result.error);
	expect(runTool({input, "--tool", "align", "--axis", "x", "--vertices", "0", "--output", output, "--overwrite"}).exitCode == 2,
		   "align without --value or --to is a usage error");
	expect(runTool({input, "--tool", "align", "--axis", "xx", "--value", "0", "--vertices", "0", "--output", output, "--overwrite"}).exitCode == 2,
		   "a repeated axis is a usage error");
	expect(runTool({input, "--tool", "snap-grid", "--vertices", "0", "--output", output, "--overwrite"}).exitCode == 2, "snap-grid needs --grid");
	result = runTool({input, "--tool", "connect", "--vertices", "8,15", "--output", output, "--overwrite"});
	expect(result.exitCode == 0 && result.payload.value("selection").toObject().value("edges").toArray().size() == 4, "connect --vertices cuts a path",
		   result.error);
	expect(runTool({input, "--tool", "connect", "--vertices", "7", "--output", output, "--overwrite"}).exitCode == 2, "connect needs two vertices");
	result = runTool({input, "--tool", "connect-edges", "--edges", "0:4,1:5", "--cuts", "2", "--slide", "0.5", "--output", output, "--overwrite"});
	expect(result.exitCode == 0 && result.payload.value("surface").toObject().value("vertices").toInt() == 20, "connect-edges cuts one quad twice",
		   result.error);
	result = runTool({input, "--tool", "delete", "--mode", "vertices", "--vertices", "5", "--output", output, "--overwrite"});
	expect(result.exitCode == 0 && result.payload.value("surface").toObject().value("triangles").toInt() == 12, "delete --mode vertices removes a fan",
		   result.error);
	expect(runTool({input, "--tool", "delete", "--mode", "corners", "--vertices", "5", "--output", output, "--overwrite"}).exitCode == 2,
		   "an unknown delete mode is a usage error");
	expect(runTool({input, "--tool", "dissolve-edges", "--edges", "0:1", "--output", output, "--overwrite"}).exitCode == 4,
		   "dissolving a border edge is a refused edit");
	result = runTool({input, "--tool", "limited-dissolve", "--faces", "all", "--angle", "5", "--output", output, "--overwrite"});
	expect(result.exitCode == 0 && result.payload.value("surface").toObject().value("triangles").toInt() < 18, "limited-dissolve simplifies the grid",
		   result.error);
	{
		auto inverted = seamedCube();
		for (auto &t : inverted.surfaces[0].triangles)
			std::swap(t.b, t.c);
		ModelDocument document;
		QString error;
		const auto cube = QFileInfo(input).absolutePath() + QStringLiteral("/inverted.mesh.json");
		expect(document.setMesh(inverted, &error) && document.save(cube, true, &error), "save an inverted cube", error);
		ModelDocument smooth;
		const auto smoothCube = QFileInfo(input).absolutePath() + QStringLiteral("/smooth.mesh.json");
		expect(smooth.setMesh(cubeMesh(), &error) && smooth.save(smoothCube, true, &error), "save a smooth cube", error);
		QStringList top;
		for (auto edge : topEdges(cubeMesh().surfaces[0]))
			top << QStringLiteral("%1:%2").arg(edge.first).arg(edge.second);
		result = runTool({smoothCube, "--tool", "mark-sharp", "--edges", top.join(QLatin1Char(',')), "--output", output, "--overwrite"});
		expect(result.exitCode == 0 && result.payload.value("surface").toObject().value("vertices").toInt() == 12, "mark-sharp splits the top corners",
			   result.error);
		result = runTool({smoothCube, "--tool", "smoothing-group", "--faces", "2,3", "--output", output, "--overwrite"});
		ModelDocument grouped;
		expect(result.exitCode == 0 && grouped.load(output, &error) && grouped.mesh().surfaces[0].sharpEdges.size() == 4,
			   "smoothing-group saves the region's hard border", result.error);
		result = runTool({smoothCube, "--tool", "inset", "--faces", "2,3", "--thickness", "2", "--depth", "-20", "--output", output, "--overwrite"});
		expect(result.exitCode == 0 && !result.payload.value("introducedIntersections").toArray().isEmpty(),
			   "JSON lists the faces an inset made cross", result.error);
		expect(runTool({smoothCube, "--tool", "inset", "--faces", "2,3", "--thickness", "2", "--depth", "-20", "--refuse-intersections", "--output", output,
						"--overwrite"})
					   .exitCode == 4,
			   "--refuse-intersections refuses with exit code 4");
		result = runTool({cube, "--tool", "recalculate-outside", "--output", output, "--overwrite"});
		expect(result.exitCode == 0, "recalculate-outside fixes an inverted cube", result.error);
		expect(runTool({output, "--tool", "recalculate-outside", "--inside", "--output", cube, "--overwrite"}).exitCode == 0 &&
				   runTool({cube, "--tool", "recalculate-outside", "--inside", "--output", output, "--overwrite"}).exitCode == 4,
			   "recalculate-outside --inside turns faces in, and refuses when they already are");
	}
	result = runTool({input, "--tool", "merge", "--merge", "target", "--target-vertex", "0", "--vertices", "1", "--output", output, "--overwrite"});
	expect(result.exitCode == 0 && result.payload.value("surface").toObject().value("vertices").toInt() == 15, "merge --merge target welds onto a vertex",
		   result.error);
	expect(runTool({input, "--tool", "merge", "--merge", "target", "--vertices", "1", "--output", output, "--overwrite"}).exitCode == 2,
		   "a target weld needs --target-vertex");
	result = runTool({input, "--tool", "place", "--vertices", "0,3", "--positions", "-1,-1,0;25,-1,0", "--output", output, "--overwrite"});
	expect(result.exitCode == 0, "place moves vertices to explicit positions", result.error);
	result = runTool({input, "--tool", "transform", "--vertices", "1", "--offset", "-7.9995,0,0", "--auto-merge", "0.01", "--output", output, "--overwrite"});
	expect(result.exitCode == 0 && result.payload.value("surface").toObject().value("vertices").toInt() == 15, "transform --auto-merge welds",
		   result.error);
	expect(runTool({input, "--tool", "connect-edges", "--edges", "0:4", "--output", output, "--overwrite"}).exitCode == 4,
		   "connect-edges without opposite sides is a refused edit");
	expect(runTool({input, "--tool", "align", "--axis", "x", "--value", "0", "--grid", "1", "--vertices", "0", "--output", output, "--overwrite"}).exitCode == 2,
		   "align refuses --grid");
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("topology-tools-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	alignChecks();
	wholeTransformChecks();
	connectChecks();
	dissolveChecks();
	quadRefillChecks();
	outsideChecks();
	sharpChecks();
	sharpPersistenceChecks();
	weldChecks();
	intersectionChecks();
	QString error;
	ModelDocument document;
	const auto input = temporary.filePath("grid.mesh.json");
	auto source = gridMesh(3, 3);
	for (auto &frame : source.surfaces[0].frames)
		frame.positions[5].z += 4;
	expect(document.setMesh(source, &error) && document.save(input, false, &error), "save the CLI source", error);
	alignCli(input, temporary.filePath("aligned.mesh.json"));
	if (argc > 1)
	{
		// Real process runs check routing and JSON through cli.cpp.
		const auto run = [&](const QStringList &arguments, QJsonObject *json = nullptr)
		{
			QProcess process;
			process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--settings-file", temporary.filePath("settings.ini")} + arguments);
			if (!process.waitForFinished(60000))
			{
				process.kill();
				process.waitForFinished();
				return -1;
			}
			if (json)
				*json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
			return process.exitCode();
		};
		QJsonObject json;
		expect(run({"model", "tool", input, "--tool", "snap-grid", "--grid", "5", "--vertices", "all", "--output", temporary.filePath("snapped.mesh.json"),
					"--json"},
				   &json) == 0 &&
				   json.value("align").toObject().value("grid").toDouble() == 5,
			   "vibestudio --cli model tool --tool snap-grid reports JSON");
		const auto output = temporary.filePath("moved.mesh.json");
		expect(run({"model", "edit", input, "--operation", "transform-model", "--origin", "bounds-bottom", "--output", output, "--dry-run", "--json"}, &json) == 0 &&
				   !QFileInfo::exists(output),
			   "model edit --operation transform-model --origin validates without writing");
		expect(run({"model", "edit", input, "--operation", "transform-model", "--rotate", "0,0,90", "--scale", "2,2,2", "--offset", "1,2,3", "--output", output}) == 0,
			   "transform-model writes a turned, scaled model");
		ModelDocument loaded;
		expect(loaded.load(output, &error) && close(loaded.mesh().surfaces[0].frames[0].positions[1], moved(source.surfaces[0].frames[0].positions[1])),
			   "the CLI result matches the core transform", error);
		expect(run({"model", "edit", input, "--operation", "transform-model", "--origin", "point", "--output", output, "--overwrite"}) == 2,
			   "--origin point needs --at");
		expect(run({"model", "edit", input, "--operation", "transform-model", "--origin", "bounds-centre", "--offset", "1,0,0", "--output", output,
					"--overwrite"}) == 2,
			   "--origin and --offset are exclusive");
		expect(run({"model", "edit", input, "--operation", "transform-model", "--faces", "0", "--offset", "1,0,0", "--output", output, "--overwrite"}) == 2,
			   "transform-model takes no component selectors");
	}
	std::cout << checks << " topology tool checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
