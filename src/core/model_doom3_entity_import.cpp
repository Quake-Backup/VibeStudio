#include "core/model_doom3_entity_import.h"

#include "core/model_archive.h"
#include "core/model_md5.h"
#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

// Doom 3 actor heads and attachments: id Software DOOM-3 game/Actor.cpp
// (idActor::SetupHead, idActor::Spawn's def_attach loop, idActor::Attach),
// framework/DeclEntityDef.cpp (inherit) and DeclManager.cpp (the first
// declaration of a name wins). GPL-3.0 with id's additional terms, reviewed
// 2026-10-29; behaviour only, original implementation, no code copied.
// https://github.com/id-Software/DOOM-3/blob/master/neo/game/Actor.cpp
namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelDoom3Entity)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
constexpr qint64 defLimit = 8 * 1024 * 1024;

// Tokens of a declaration file: quoted strings, braces and bare words, with
// // and /* */ comments skipped.
struct Token
{
	QString text;
	int line = 0;
	bool quoted = false;
};
QVector<Token> tokens(const QString &text)
{
	QVector<Token> result;
	int line = 1;
	for (qsizetype i = 0; i < text.size();)
	{
		const QChar c = text[i];
		if (c == QLatin1Char('\n'))
		{
			++line;
			++i;
		}
		else if (c.isSpace())
			++i;
		else if (c == QLatin1Char('/') && i + 1 < text.size() && text[i + 1] == QLatin1Char('/'))
		{
			while (i < text.size() && text[i] != QLatin1Char('\n'))
				++i;
		}
		else if (c == QLatin1Char('/') && i + 1 < text.size() && text[i + 1] == QLatin1Char('*'))
		{
			i += 2;
			while (i + 1 < text.size() && !(text[i] == QLatin1Char('*') && text[i + 1] == QLatin1Char('/')))
				line += text[i++] == QLatin1Char('\n') ? 1 : 0;
			i += 2;
		}
		else if (c == QLatin1Char('"'))
		{
			// An empty "" value is a value (not null), unlike a missing key.
			Token token{QLatin1String(""), line, true};
			++i;
			while (i < text.size() && text[i] != QLatin1Char('"'))
			{
				if (text[i] == QLatin1Char('\n'))
					++line;
				token.text += text[i++];
			}
			++i;
			result << token;
		}
		else if (c == QLatin1Char('{') || c == QLatin1Char('}'))
		{
			result << Token{QString(c), line, false};
			++i;
		}
		else
		{
			Token token{{}, line, false};
			while (i < text.size() && !text[i].isSpace() && text[i] != QLatin1Char('{') && text[i] != QLatin1Char('}') &&
				   text[i] != QLatin1Char('"') && !(text[i] == QLatin1Char('/') && i + 1 < text.size() &&
													(text[i + 1] == QLatin1Char('/') || text[i + 1] == QLatin1Char('*'))))
				token.text += text[i++];
			result << token;
		}
	}
	return result;
}

QString modelPathFor(const QHash<QString, Doom3ModelDecl> &models, const QString &value, ModelVec3 *offset, QString *declared)
{
	// A model declaration (following inherit for the mesh), else a model file.
	QString name = value.toLower();
	for (int depth = 0; depth < 16 && models.contains(name); ++depth)
	{
		const auto &decl = *models.constFind(name);
		if (declared && declared->isEmpty())
			*declared = decl.name;
		if (offset && depth == 0)
			*offset = decl.offset;
		if (!decl.meshPath.isEmpty())
			return decl.meshPath;
		name = decl.inherit.toLower();
	}
	static const QRegularExpression file(QStringLiteral("\\.(md5mesh|lwo|ase|ma|obj|md3)$"), QRegularExpression::CaseInsensitiveOption);
	return file.match(value).hasMatch() ? value : QString();
}

struct Index
{
	QHash<QString, ModelDoom3EntityDef> entities; // by lower-case name
	QHash<QString, Doom3ModelDecl> models;		  // by lower-case name
	QStringList order;							  // entity names in file order
	QStringList warnings;
};
bool readIndex(const PackageArchiveReader &archive, Index *index, QString *error, const ModelWorkControl &control)
{
	const ModelArchiveReader reader(archive, control);
	const auto entries = archive.entries();
	for (qsizetype i = 0; i < entries.size(); ++i)
	{
		const auto &path = entries[i].virtualPath;
		if (!path.startsWith(QStringLiteral("def/"), Qt::CaseInsensitive) || !path.endsWith(QStringLiteral(".def"), Qt::CaseInsensitive))
			continue;
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, i, entries.size(), error))
			return false;
		QByteArray bytes;
		if (!reader.readEntryAt(i, &bytes, error, defLimit))
			return false;
		const auto text = QString::fromUtf8(bytes);
		for (auto &entity : parseModelDoom3EntityDefs(text, path, &index->warnings))
		{
			const auto key = entity.name.toLower();
			if (index->entities.contains(key))
				continue;
			index->order << entity.name;
			index->entities.insert(key, std::move(entity));
		}
		QStringList warnings;
		for (const auto &decl : parseDoom3ModelDecls(text, &warnings))
			if (!index->models.contains(decl.name.toLower()))
				index->models.insert(decl.name.toLower(), decl);
		for (const auto &warning : std::as_const(warnings))
			index->warnings << QStringLiteral("%1: %2").arg(path, warning);
	}
	return modelWorkCheckpoint(control, ModelWorkPhase::Reading, entries.size(), entries.size(), error);
}
// The entity's keys with its inherited ones after them, own keys first.
bool resolved(const Index &index, const QString &name, ModelDoom3EntityDef *result, QString *error)
{
	const auto found = index.entities.constFind(name.toLower());
	const auto *entity = found != index.entities.cend() ? &found.value() : nullptr;
	if (!entity)
		return fail(error, Text::tr("No entityDef named %1 is in the package's def/ files.").arg(name));
	*result = *entity;
	QSet<QString> seen{name.toLower()};
	QString parent = entity->value(QStringLiteral("inherit"));
	while (!parent.isEmpty())
	{
		if (seen.contains(parent.toLower()) || seen.size() > 32)
			return fail(error, Text::tr("EntityDef %1 inherits in a loop through %2.").arg(name, parent));
		seen.insert(parent.toLower());
		const auto inherited = index.entities.constFind(parent.toLower());
		const auto *base = inherited != index.entities.cend() ? &inherited.value() : nullptr;
		if (!base)
			return fail(error, Text::tr("EntityDef %1 inherits %2, which no def/ file defines.").arg(name, parent));
		for (const auto &[key, value] : base->keys)
			if (key.compare(QStringLiteral("inherit"), Qt::CaseInsensitive) != 0 && result->value(key).isNull())
				result->keys.append({key, value});
		parent = base->value(QStringLiteral("inherit"));
	}
	return true;
}
QStringList attachKeys(const ModelDoom3EntityDef &entity)
{
	QStringList values;
	for (const auto &[key, value] : entity.keys)
		if (key.startsWith(QStringLiteral("def_attach"), Qt::CaseInsensitive) && !value.isEmpty())
			values << value;
	return values;
}
std::array<double, 3> numbers(const QString &text)
{
	const auto parts = text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
	std::array<double, 3> result{0, 0, 0};
	for (int i = 0; i < 3 && i < parts.size(); ++i)
	{
		bool ok = false;
		const double value = parts[i].toDouble(&ok);
		result[i] = ok && std::isfinite(value) ? value : 0;
	}
	return result;
}
// Degrees about X, then Y, then Z (rotateModelVector) for a rotation matrix,
// R = Rz * Ry * Rx with column vectors.
ModelVec3 eulerDegrees(const double r[3][3])
{
	const double toDegrees = 180 / std::numbers::pi;
	const double sy = std::clamp(-r[2][0], -1.0, 1.0);
	double x = 0, y = std::asin(sy), z = 0;
	if (std::abs(sy) < 0.999999)
	{
		x = std::atan2(r[2][1], r[2][2]);
		z = std::atan2(r[1][0], r[0][0]);
	}
	else
		z = std::atan2(-r[0][1], r[1][1]);
	const auto clean = [&](double radians) {
		const double degrees = radians * toDegrees;
		return float(std::abs(degrees) < 1e-9 ? 0.0 : degrees);
	};
	return {clean(x), clean(y), clean(z)};
}
QString partId(const QString &name, QSet<QString> *used)
{
	QString id;
	for (const QChar c : name)
		id += c.isLetterOrNumber() && c.unicode() < 128 ? c : QLatin1Char('_');
	if (id.isEmpty() || !id[0].isLetter())
		id.prepend(QStringLiteral("a_"));
	id = id.left(28);
	QString unique = id;
	for (int n = 2; used->contains(unique.toLower()); ++n)
		unique = id + QStringLiteral("_%1").arg(n);
	used->insert(unique.toLower());
	return unique;
}
} // namespace

QString ModelDoom3EntityDef::value(const QString &key) const
{
	QString result;
	for (const auto &[name, text] : keys)
		if (name.compare(key, Qt::CaseInsensitive) == 0)
			result = text;
	return result;
}

QVector<ModelDoom3EntityDef> parseModelDoom3EntityDefs(const QString &text, const QString &sourcePath, QStringList *warnings)
{
	QVector<ModelDoom3EntityDef> result;
	const auto list = tokens(text);
	for (qsizetype i = 0; i < list.size();)
	{
		const auto &head = list[i];
		// A declaration: <type> <name> { ... }; anything else is skipped.
		if (head.quoted || head.text == QStringLiteral("{") || head.text == QStringLiteral("}") || i + 2 >= list.size() ||
			list[i + 2].text != QStringLiteral("{") || list[i + 2].quoted)
		{
			if (!head.quoted && head.text == QStringLiteral("{"))
			{
				// An unnamed block: skip it whole.
				int depth = 0;
				for (; i < list.size(); ++i)
				{
					if (!list[i].quoted && list[i].text == QStringLiteral("{"))
						++depth;
					else if (!list[i].quoted && list[i].text == QStringLiteral("}") && --depth == 0)
					{
						++i;
						break;
					}
				}
				continue;
			}
			++i;
			continue;
		}
		const bool entity = head.text.compare(QStringLiteral("entityDef"), Qt::CaseInsensitive) == 0;
		ModelDoom3EntityDef def{list[i + 1].text, {}, sourcePath, head.line};
		int depth = 0;
		qsizetype j = i + 2;
		for (; j < list.size(); ++j)
		{
			const auto &token = list[j];
			if (!token.quoted && token.text == QStringLiteral("{"))
				++depth;
			else if (!token.quoted && token.text == QStringLiteral("}"))
			{
				if (--depth == 0)
					break;
			}
			else if (entity && depth == 1)
			{
				if (j + 1 >= list.size() || (!list[j + 1].quoted && (list[j + 1].text == QStringLiteral("{") || list[j + 1].text == QStringLiteral("}"))))
				{
					if (warnings)
						*warnings << Text::tr("%1:%2: key %3 in entityDef %4 has no value.").arg(sourcePath).arg(token.line).arg(token.text, def.name);
					continue;
				}
				def.keys.append({token.text, list[j + 1].text});
				++j;
			}
		}
		if (depth != 0 && warnings)
			*warnings << Text::tr("%1:%2: declaration %3 is not closed.").arg(sourcePath).arg(head.line).arg(def.name);
		if (entity)
			result << def;
		i = j + 1;
	}
	return result;
}

bool listModelDoom3Entities(const PackageArchiveReader &archive, QVector<ModelDoom3EntityListing> *entities, QString *error,
							const ModelWorkControl &control)
{
	if (!entities)
		return false;
	Index index;
	if (!readIndex(archive, &index, error, control))
		return false;
	QVector<ModelDoom3EntityListing> result;
	for (const auto &name : std::as_const(index.order))
	{
		ModelDoom3EntityDef entity;
		if (!resolved(index, name, &entity, nullptr))
			continue;
		const auto attachments = attachKeys(entity);
		if (entity.value(QStringLiteral("def_head")).isEmpty() && attachments.isEmpty())
			continue;
		result << ModelDoom3EntityListing{entity.name,
										  entity.sourcePath,
										  entity.value(QStringLiteral("model")),
										  entity.value(QStringLiteral("def_head")),
										  entity.value(QStringLiteral("head_joint")),
										  int(attachments.size())};
	}
	std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
	*entities = std::move(result);
	return true;
}

bool importModelDoom3Entity(const PackageArchiveReader &archive, const QString &entityName, ModelDoom3EntityImport *result, QString *error,
							const ModelWorkControl &control)
{
	if (!result)
		return false;
	Index index;
	if (!readIndex(archive, &index, error, control))
		return false;
	ModelDoom3EntityDef entity;
	if (!resolved(index, entityName, &entity, error))
		return false;
	QStringList notes;
	ModelVec3 bodyOffset;
	QString bodyDecl;
	const auto bodyPath = modelPathFor(index.models, entity.value(QStringLiteral("model")), &bodyOffset, &bodyDecl);
	if (!bodyPath.endsWith(QStringLiteral(".md5mesh"), Qt::CaseInsensitive))
		return fail(error, Text::tr("EntityDef %1 has no md5mesh model, so it has no joints for a head or attachments.").arg(entity.name));
	// The body's joints in their bind pose, read as the assembly reads them.
	const auto body = decodeModelMeshFromArchive(archive, bodyPath, {}, control);
	if (!body.geometryAvailable || body.skeleton.isEmpty())
		return fail(error, Text::tr("The body model %1 cannot be read: %2").arg(bodyPath, body.error));
	const auto bind = modelSkeletonBindPose(body.skeleton);
	// The joint's orthonormal axes as columns, as joint attachments use them.
	const auto jointFrame = [&](const QString &name, QString *jointName, double frame[3][3]) {
		const int joint = modelJointIndex(body.skeleton, name, Qt::CaseInsensitive);
		if (joint < 0 || joint >= bind.size())
			return false;
		*jointName = body.skeleton.joints[joint].name;
		double axes[3][3];
		for (int axis = 0; axis < 3; ++axis)
		{
			for (int c = 0; c < 3; ++c)
				axes[axis][c] = bind[joint].m[c * 4 + axis];
			for (int previous = 0; previous < axis; ++previous)
			{
				const double along = axes[axis][0] * axes[previous][0] + axes[axis][1] * axes[previous][1] + axes[axis][2] * axes[previous][2];
				for (int c = 0; c < 3; ++c)
					axes[axis][c] -= along * axes[previous][c];
			}
			const double length = std::sqrt(axes[axis][0] * axes[axis][0] + axes[axis][1] * axes[axis][1] + axes[axis][2] * axes[axis][2]);
			if (!(length > 1e-9))
				return false;
			for (int c = 0; c < 3; ++c)
				frame[c][axis] = axes[axis][c] / length;
		}
		return true;
	};
	ModelAssembly assembly;
	assembly.name = entity.name;
	QSet<QString> used;
	ModelAssemblyPart root;
	root.id = partId(QStringLiteral("body"), &used);
	root.source = bodyPath;
	root.sourceKind = ModelAssemblySource::Package;
	assembly.parts << root;
	const auto attachPart = [&](const QString &id, const QString &path) {
		ModelAssemblyPart part;
		part.id = partId(id, &used);
		part.source = path;
		part.sourceKind = ModelAssemblySource::Package;
		part.parent = root.id;
		return part;
	};
	// SetupHead: the head sits at the joint with the entity's axes, so its
	// rotation within the joint undoes the joint's (the transpose).
	const auto head = entity.value(QStringLiteral("def_head"));
	if (!head.isEmpty())
	{
		const auto jointText = entity.value(QStringLiteral("head_joint"));
		QString joint;
		double frame[3][3];
		if (jointText.isEmpty())
			return fail(error, Text::tr("EntityDef %1 has def_head but no head_joint; Doom 3 stops with an error.").arg(entity.name));
		if (!jointFrame(jointText, &joint, frame))
			return fail(error, Text::tr("The body %1 has no joint %2 for the head of %3.").arg(bodyPath, jointText, entity.name));
		const auto headPath = modelPathFor(index.models, head, nullptr, nullptr);
		if (headPath.isEmpty())
			notes << Text::tr("The head %1 names no model declaration or model file in the package, so it is left out.").arg(head);
		else
		{
			auto part = attachPart(QStringLiteral("head"), headPath);
			part.tag = modelAssemblyJointPrefix() + joint;
			double inverse[3][3];
			for (int r = 0; r < 3; ++r)
				for (int c = 0; c < 3; ++c)
					inverse[r][c] = frame[c][r];
			part.rotation = eulerDegrees(inverse);
			assembly.parts << part;
		}
	}
	// Attach: each def_attach entity on its joint, offset by origin in the
	// actor's model space and turned by angles (pitch yaw roll) within the joint.
	for (const auto &className : attachKeys(entity))
	{
		ModelDoom3EntityDef attached;
		QString attachError;
		if (!resolved(index, className, &attached, &attachError))
		{
			notes << Text::tr("Attachment %1 is left out: %2").arg(className, attachError);
			continue;
		}
		const auto model = attached.value(QStringLiteral("model"));
		const auto path = modelPathFor(index.models, model, nullptr, nullptr);
		if (path.isEmpty())
		{
			notes << Text::tr("Attachment %1 has no model in the package (an attached light or effect, or a missing model), so it is left "
							  "out.")
						 .arg(className);
			continue;
		}
		const auto jointText = attached.value(QStringLiteral("joint"));
		QString joint;
		double frame[3][3];
		if (!jointFrame(jointText, &joint, frame))
			return fail(error, Text::tr("The body %1 has no joint %2 for attachment %3; Doom 3 stops with an error.")
								   .arg(bodyPath, jointText.isEmpty() ? QStringLiteral("\"\"") : jointText, className));
		auto part = attachPart(className, path);
		part.tag = modelAssemblyJointPrefix() + joint;
		const auto origin = numbers(attached.value(QStringLiteral("origin")));
		const auto angles = numbers(attached.value(QStringLiteral("angles"))); // pitch, yaw, roll
		// Model-space offset into the joint's frame: the transpose of its axes.
		part.translation = {float(frame[0][0] * origin[0] + frame[1][0] * origin[1] + frame[2][0] * origin[2]),
							float(frame[0][1] * origin[0] + frame[1][1] * origin[1] + frame[2][1] * origin[2]),
							float(frame[0][2] * origin[0] + frame[1][2] * origin[1] + frame[2][2] * origin[2])};
		// idAngles::ToMat3 is Rz(yaw) * Ry(pitch) * Rx(roll): roll about X first.
		part.rotation = {float(angles[2]), float(angles[0]), float(angles[1])};
		assembly.parts << part;
	}
	if (bodyOffset.x != 0 || bodyOffset.y != 0 || bodyOffset.z != 0)
		notes << Text::tr("The model declaration %1 moves the whole actor by its offset; the recipe leaves it out, which keeps the parts in "
						  "place relative to each other.")
					 .arg(bodyDecl);
	if (!entity.value(QStringLiteral("skin")).isEmpty() || !entity.value(QStringLiteral("skin_head")).isEmpty())
		notes << Text::tr("Skin keys are not applied; the parts show their own materials.");
	notes << Text::tr("The recipe shows the bind pose: the declaration's animations are not imported. Doom 3 binds the head and "
					  "attachments with the placement they have when the actor spawns.");
	notes.prepend(Text::tr("Opened entityDef %1 from %2: %n part(s) from the package.", nullptr, int(assembly.parts.size()))
					  .arg(entity.name, entity.sourcePath));
	result->entity = entity.name;
	result->assembly = std::move(assembly);
	result->notes = notes;
	return true;
}

QJsonObject modelDoom3EntityListingJson(const ModelDoom3EntityListing &listing)
{
	return {{"name", listing.name},
			{"source", listing.sourcePath},
			{"model", listing.model},
			{"head", listing.head},
			{"headJoint", listing.headJoint},
			{"attachments", listing.attachments}};
}

QJsonObject modelDoom3EntityImportJson(const ModelDoom3EntityImport &result)
{
	QJsonArray parts;
	for (const auto &part : result.assembly.parts)
		parts.append(QJsonObject{{"id", part.id},
								 {"source", part.source},
								 {"parent", part.parent},
								 {"tag", part.tag},
								 {"translation", QJsonArray{part.translation.x, part.translation.y, part.translation.z}},
								 {"rotation", QJsonArray{part.rotation.x, part.rotation.y, part.rotation.z}}});
	return {{"entity", result.entity}, {"parts", parts}, {"notes", QJsonArray::fromStringList(result.notes)}};
}

QStringList modelDoom3EntityImportText(const ModelDoom3EntityImport &result)
{
	QStringList lines;
	for (const auto &part : result.assembly.parts)
		lines << (part.parent.isEmpty() ? QStringLiteral("%1: %2").arg(part.id, part.source)
										: QStringLiteral("%1: %2 on %3 %4").arg(part.id, part.source, part.parent, part.tag));
	return lines + result.notes;
}
} // namespace vibestudio
