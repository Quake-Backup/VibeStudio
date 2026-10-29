#include "core/model_collision_doom3.h"

#include "core/material_library.h"
#include "core/model_collision_hull.h"
#include "core/model_document.h"
#include "core/model_transform.h"
#include "core/package_archive.h"

#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelCollision)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
// The volume's points in its box frame: a hull's own, else the unit corners.
QVector<ModelVec3> localPoints(const ModelCollisionBox &box)
{
	if (isModelCollisionHull(box))
		return box.hull;
	QVector<ModelVec3> corners;
	for (int i = 0; i < 8; ++i)
		corners << ModelVec3{i & 1 ? .5f : -.5f, i & 2 ? .5f : -.5f, i & 4 ? .5f : -.5f};
	return corners;
}
ModelVec3 world(const ModelCollisionBox &pose, ModelVec3 local)
{
	const auto r = rotateModelVector({local.x * pose.size.x, local.y * pose.size.y, local.z * pose.size.z}, pose.rotation);
	return {r.x + pose.centre.x, r.y + pose.centre.y, r.z + pose.centre.z};
}
bool validMaterial(const QString &material)
{
	static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_./-]{0,126}$"));
	return pattern.match(material).hasMatch() && !material.contains(QStringLiteral("..")) && !material.contains(QStringLiteral("//"));
}
bool surfaceFor(const ModelMesh &mesh, const ModelCollisionBox &box, int shapeFrame, const QString &material, ModelSurface *surface, QString *error,
				const ModelWorkControl &control)
{
	ModelCollisionHullShape shape;
	// A hull with a shape per pose takes the shape of the exported frame.
	ModelCollisionBox reference;
	if (!sampleModelCollisionBox(box, shapeFrame, shapeFrame, 0, &reference))
		reference = box;
	if (!modelCollisionHullShape(localPoints(reference), &shape, error, control))
		return false;
	ModelSurface result;
	result.name = QStringLiteral("collision_") + box.name;
	result.skinPaths = {material};
	// Each face gets its own corners, so normals stay flat.
	QVector<QPair<int, int>> corners; // face, vertex index in the shape
	for (int f = 0; f < shape.faces.size(); ++f)
	{
		const int first = int(corners.size());
		for (int v : shape.faces[f])
			corners << QPair<int, int>{f, v};
		// The faces wind counter-clockwise seen from outside, so a fan keeps
		// cross(b - a, c - a) pointing outwards.
		for (int i = 1; i + 1 < shape.faces[f].size(); ++i)
			result.triangles << ModelTriangle{first, first + i, first + i + 1};
	}
	result.vertexCount = int(corners.size());
	result.texCoords.fill({0, 0}, result.vertexCount);
	for (int frame = 0; frame < mesh.frames.size(); ++frame)
	{
		ModelCollisionBox pose;
		if (!sampleModelCollisionBox(box, frame, frame, 0, &pose))
			return fail(error, Text::tr("The collision pose for frame %1 is unavailable.").arg(frame));
		ModelFrameGeometry geometry;
		QVector<ModelVec3> normals(shape.faces.size());
		for (int f = 0; f < shape.faces.size(); ++f)
		{
			const auto &face = shape.faces[f];
			const auto a = world(pose, shape.vertices[face[0]]), b = world(pose, shape.vertices[face[1]]),
					   c = world(pose, shape.vertices[face[2]]);
			const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
			const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
			const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
			if (!(length > 0))
				return fail(error, Text::tr("Collision volume %1 has a collapsed face.").arg(box.name));
			normals[f] = {float(nx / length), float(ny / length), float(nz / length)};
		}
		for (const auto &[face, vertex] : corners)
		{
			geometry.positions << world(pose, shape.vertices[vertex]);
			geometry.normals << normals[face];
		}
		result.frames << geometry;
	}
	*surface = std::move(result);
	return true;
}
} // namespace

bool modelCollisionSurfaces(const ModelMesh &mesh, const ModelCollisionSurfaceExport &request, QVector<ModelSurface> *surfaces,
							QString *error, const ModelWorkControl &control)
{
	if (mesh.collisionBoxes.isEmpty())
		return fail(error, Text::tr("Add at least one collision box or hull first."));
	if (!validMaterial(request.material))
		return fail(error, Text::tr("Choose a Doom 3 material path of letters, digits and _ . / - only, such as "
									"textures/common/collision."));
	const bool animated =
		std::any_of(mesh.collisionBoxes.cbegin(), mesh.collisionBoxes.cend(), [](const auto &box) { return !box.framePoses.isEmpty(); });
	if (request.frame < 0 || request.frame >= mesh.frames.size() || (animated && request.frame < 0))
		return fail(error, Text::tr("Choose an existing frame for the collision surfaces."));
	QVector<ModelSurface> result;
	for (const auto &box : mesh.collisionBoxes)
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Serializing, result.size(), mesh.collisionBoxes.size(), error))
			return false;
		ModelSurface surface;
		if (!surfaceFor(mesh, box, request.frame, request.material, &surface, error, control))
			return false;
		result << surface;
	}
	if (surfaces)
		*surfaces = std::move(result);
	return true;
}

QByteArray exportModelAseWithCollision(const ModelMesh &mesh, const ModelCollisionSurfaceExport &request, QString *error,
									   const ModelWorkControl &control, ModelExportReport *report)
{
	QVector<ModelSurface> collision;
	if (!modelCollisionSurfaces(mesh, request, &collision, error, control))
		return {};
	auto combined = mesh;
	for (const auto &surface : collision)
	{
		if (std::any_of(combined.surfaces.cbegin(), combined.surfaces.cend(), [&](const auto &s) { return s.name == surface.name; }))
		{
			fail(error, Text::tr("A render surface is already named %1. Rename it before exporting collision surfaces.").arg(surface.name));
			return {};
		}
		combined.surfaces << surface;
	}
	if (combined.surfaces.size() > 32)
	{
		fail(error, Text::tr("Render and collision surfaces together exceed the 32-surface limit."));
		return {};
	}
	// The volumes are now surfaces, so the export does not report them omitted.
	combined.collisionBoxes.clear();
	updateEditableModelMetadata(&combined);
	auto bytes = exportEditableModel(combined, QStringLiteral("ase"), request.frame, error, control, report);
	if (!bytes.isEmpty() && report)
	{
		const int count = int(collision.size());
		report->notes << Text::tr("%n collision surface(s) use %1. In Doom 3, when any surface's material has the collision keyword, "
								  "only those surfaces collide; the render surfaces no longer do. Define the material with the "
								  "collision keyword in the game's material scripts.",
								  nullptr, count)
							 .arg(request.material);
	}
	return bytes;
}

bool exportModelDoom3ClipModel(const ModelMesh &mesh, const QString &volume, const ModelCollisionSurfaceExport &request,
							   ModelDoom3ClipModel *result, QString *error, const ModelWorkControl &control)
{
	const auto *box = findModelCollisionBox(mesh, volume);
	if (!box)
		return fail(error, Text::tr("Choose an existing collision box or hull for the clip model."));
	if (!validMaterial(request.material) || request.frame < 0 || request.frame >= mesh.frames.size())
		return fail(error, Text::tr("Choose a valid material and an existing frame for the clip model."));
	ModelCollisionHullShape shape;
	ModelCollisionBox reference;
	if (!sampleModelCollisionBox(*box, request.frame, request.frame, 0, &reference))
		reference = *box;
	if (!modelCollisionHullShape(localPoints(reference), &shape, error, control))
		return false;
	ModelDoom3ClipModel clip;
	clip.faces = int(shape.faces.size());
	clip.edges = int(shape.edges.size());
	clip.vertices = int(shape.vertices.size());
	// MAX_TRACEMODEL_POLYS, MAX_TRACEMODEL_EDGES and MAX_TRACEMODEL_VERTS in
	// Doom 3's idlib/geometry/TraceModel.h (GPL-3.0-or-later).
	if (clip.faces > 16 || clip.edges > 32 || clip.vertices > 32)
		return fail(error, Text::tr("Volume %1 has %2, %3 and %4; a Doom 3 trace model allows 16 faces, 32 edges and 32 vertices. Fit "
									"a hull with the Doom 3 trace model budget.")
							   .arg(volume, Text::tr("%n face(s)", nullptr, clip.faces), Text::tr("%n edge(s)", nullptr, clip.edges),
									Text::tr("%n vertex(es)", nullptr, clip.vertices)));
	ModelSurface surface;
	if (!surfaceFor(mesh, *box, request.frame, request.material, &surface, error, control))
		return false;
	ModelMesh model;
	model.geometryAvailable = true;
	model.sourcePath = mesh.sourcePath;
	model.formatId = QStringLiteral("mesh");
	model.formatName = QStringLiteral("VibeStudio Mesh");
	model.md2SkinSize = mesh.md2SkinSize;
	model.frames = mesh.frames;
	model.surfaces = {surface};
	updateEditableModelMetadata(&model);
	ModelExportReport report;
	clip.bytes = exportEditableModel(model, QStringLiteral("ase"), request.frame, error, control, &report);
	if (clip.bytes.isEmpty())
		return false;
	ModelCollisionBox pose;
	sampleModelCollisionBox(*box, request.frame, request.frame, 0, &pose);
	if (!isModelCollisionHull(pose) && pose.rotation.x == 0 && pose.rotation.y == 0 && pose.rotation.z == 0)
	{
		clip.hasBounds = true;
		clip.mins = {pose.centre.x - pose.size.x / 2, pose.centre.y - pose.size.y / 2, pose.centre.z - pose.size.z / 2};
		clip.maxs = {pose.centre.x + pose.size.x / 2, pose.centre.y + pose.size.y / 2, pose.centre.z + pose.size.z / 2};
		clip.notes << Text::tr("This axis-aligned box can also be given to an entity as \"mins\" and \"maxs\" keys instead of a clip "
							   "model.");
	}
	clip.notes << Text::tr("Name this file in the entity's \"clipmodel\" key. Doom 3 builds a trace model from its single convex "
						   "volume; this export does not run the game.");
	clip.notes += report.notes;
	if (result)
		*result = std::move(clip);
	return true;
}
namespace
{
// Plain decimals: idLexer reads these without exponents; -0 is written as 0.
QString cmNumber(double value)
{
	auto text = QString::number(value, 'f', 6);
	if (text.contains(QLatin1Char('.')))
	{
		while (text.endsWith(QLatin1Char('0')))
			text.chop(1);
		if (text.endsWith(QLatin1Char('.')))
			text.chop(1);
	}
	return text == QStringLiteral("-0") ? QStringLiteral("0") : text;
}
QString cmVector(double x, double y, double z)
{
	return QStringLiteral("( %1 %2 %3 )").arg(cmNumber(x), cmNumber(y), cmNumber(z));
}
} // namespace

bool exportModelDoom3CollisionModelFile(const ModelMesh &mesh, const QString &modelPath, const ModelCollisionSurfaceExport &request,
										ModelDoom3CollisionModelFile *result, QString *error, const ModelWorkControl &control)
{
	static const QRegularExpression pathPattern(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_./-]{0,250}\\.(ase|lwo|ma)$"),
												QRegularExpression::CaseInsensitiveOption);
	if (!pathPattern.match(modelPath).hasMatch() || modelPath.contains(QStringLiteral("..")) || modelPath.contains(QStringLiteral("//")))
		return fail(error, Text::tr("Give the model's game path ending .ase, .lwo or .ma, such as models/props/crate.ase; Doom 3 reads "
									"the collision model beside it."));
	if (mesh.collisionBoxes.isEmpty())
		return fail(error, Text::tr("Add at least one collision box or hull first."));
	if (!validMaterial(request.material) || request.frame < 0 || request.frame >= mesh.frames.size())
		return fail(error, Text::tr("Choose a valid material and an existing frame for the collision model."));
	QVector<ModelVec3> vertices;
	QVector<QPair<int, int>> edges{{0, 0}}; // edge 0 is the dummy every edge number skips
	QVector<int> users{0};
	QStringList polygons;
	for (const auto &box : mesh.collisionBoxes)
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Serializing, polygons.size(), mesh.collisionBoxes.size(), error))
			return false;
		ModelCollisionBox pose;
		if (!sampleModelCollisionBox(box, request.frame, request.frame, 0, &pose))
			return fail(error, Text::tr("The collision pose for frame %1 is unavailable.").arg(request.frame));
		ModelCollisionHullShape shape;
		if (!modelCollisionHullShape(localPoints(pose), &shape, error, control))
			return false;
		const int base = int(vertices.size());
		for (const auto &point : shape.vertices)
			vertices << world(pose, point);
		// Each undirected edge once: the first face to use it stores it in its own
		// direction and the neighbour refers to it negated (reversed).
		QHash<QPair<int, int>, int> known;
		for (const auto &face : shape.faces)
		{
			QStringList numbers;
			double nx = 0, ny = 0, nz = 0, cx = 0, cy = 0, cz = 0;
			ModelVec3 lo = vertices[base + face[0]], hi = lo;
			for (int i = 0; i < face.size(); ++i)
			{
				const int a = base + face[i], b = base + face[(i + 1) % face.size()];
				int number = 0;
				if (const auto found = known.constFind({b, a}); found != known.cend())
					number = -found.value();
				else if (known.contains({a, b}))
					return fail(error, Text::tr("Collision volume %1 is not a closed convex volume.").arg(box.name));
				else
				{
					number = int(edges.size());
					known.insert({a, b}, number);
					edges << QPair<int, int>{a, b};
					users << 0;
				}
				++users[std::abs(number)];
				numbers << QString::number(number);
				// Newell's normal, and the polygon's centre and bounds.
				const auto p = vertices[a], q = vertices[b];
				nx += (double(p.y) - q.y) * (double(p.z) + q.z);
				ny += (double(p.z) - q.z) * (double(p.x) + q.x);
				nz += (double(p.x) - q.x) * (double(p.y) + q.y);
				cx += p.x;
				cy += p.y;
				cz += p.z;
				lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
				hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
			}
			const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
			if (!(length > 0))
				return fail(error, Text::tr("Collision volume %1 has a collapsed face.").arg(box.name));
			nx /= length;
			ny /= length;
			nz /= length;
			const double count = double(face.size()), dist = (nx * cx + ny * cy + nz * cz) / count;
			polygons << QStringLiteral("%1 ( %2 ) %3 %4 %5 %6 \"%7\"")
							.arg(face.size())
							.arg(numbers.join(QLatin1Char(' ')), cmVector(nx, ny, nz), cmNumber(dist), cmVector(lo.x, lo.y, lo.z),
								 cmVector(hi.x, hi.y, hi.z), request.material);
		}
	}
	if (std::any_of(users.cbegin() + 1, users.cend(), [](int count) { return count != 2; }))
		return fail(error, Text::tr("A collision volume is not a closed convex volume."));
	QString text;
	QTextStream out(&text);
	out << "CM \"1.00\"\n\n0\n\n";
	out << "collisionModel \"" << modelPath << "\" {\n";
	out << "\tvertices { /* numVertices = */ " << vertices.size() << "\n";
	for (int i = 0; i < vertices.size(); ++i)
		out << "\t/* " << i << " */ " << cmVector(vertices[i].x, vertices[i].y, vertices[i].z) << "\n";
	out << "\t}\n\tedges { /* numEdges = */ " << edges.size() << "\n";
	for (int i = 0; i < edges.size(); ++i)
		out << "\t/* " << i << " */ ( " << edges[i].first << ' ' << edges[i].second << " ) 0 " << users[i] << "\n";
	out << "\t}\n\tnodes {\n\t( -1 0 )\n\t}\n\tpolygons {\n";
	for (const auto &polygon : polygons)
		out << '\t' << polygon << "\n";
	out << "\t}\n}\n";
	out.flush();
	ModelDoom3CollisionModelFile file;
	file.bytes = text.toUtf8();
	file.modelPath = modelPath;
	file.filePath = modelPath.left(modelPath.lastIndexOf(QLatin1Char('.'))) + QStringLiteral(".cm");
	file.polygons = int(polygons.size());
	file.edges = int(edges.size()) - 1;
	file.vertices = int(vertices.size());
	file.notes << Text::tr("Doom 3 reads %1 instead of converting %2's surfaces when it loads that model as a static collision "
						   "model; keep the two in step. The polygons use %3, whose definition gives their contents. This export "
						   "does not run the game.")
					  .arg(file.filePath, modelPath, request.material);
	if (result)
		*result = std::move(file);
	return true;
}

ModelDoom3CollisionMaterialCheck checkModelDoom3CollisionMaterial(const PackageArchiveReader &reader, const QString &material,
																	const ModelWorkControl &control)
{
	ModelDoom3CollisionMaterialCheck check;
	check.material = material;
	if (!reader.isOpen() || !validMaterial(material))
		return check;
	MaterialLibraryOptions options;
	options.engines = {MaterialEngine::Doom3};
	options.includeImplicit = false;
	options.includeClassic = false;
	const auto library = scanMaterialLibrary(reader, options, [&](int, int, const QString &) { return !(control.cancelled && control.cancelled()); });
	if (library.cancelled)
		return check;
	const int index = library.indexOf(material, MaterialEngine::Doom3);
	if (index < 0)
	{
		check.state = ModelDoom3CollisionMaterialState::Undefined;
		check.note = Text::tr("No material script in the package defines %1. Unless the game's own materials define it with the "
							  "collision keyword, Doom 3 draws the collision surfaces and collides with every surface.")
						 .arg(material);
		return check;
	}
	check.sourcePath = library.entries[index].sourcePath;
	if (!library.definition(index).surfaceParms.contains(QStringLiteral("collision"), Qt::CaseInsensitive))
	{
		check.state = ModelDoom3CollisionMaterialState::NoCollisionKey;
		check.note = Text::tr("Material %1 in %2 has no collision keyword, so Doom 3 draws the collision surfaces and collides with "
							  "every surface. Add collision to its definition.")
						 .arg(material, check.sourcePath);
		return check;
	}
	check.state = ModelDoom3CollisionMaterialState::Defined;
	return check;
}
QString modelDoom3CollisionMaterialStateId(ModelDoom3CollisionMaterialState state)
{
	switch (state)
	{
	case ModelDoom3CollisionMaterialState::Defined:
		return QStringLiteral("defined");
	case ModelDoom3CollisionMaterialState::Undefined:
		return QStringLiteral("undefined");
	case ModelDoom3CollisionMaterialState::NoCollisionKey:
		return QStringLiteral("no-collision-keyword");
	case ModelDoom3CollisionMaterialState::Unchecked:
		break;
	}
	return QStringLiteral("unchecked");
}
} // namespace vibestudio
