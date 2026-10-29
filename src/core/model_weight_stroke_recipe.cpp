#include "core/model_weight_stroke_recipe.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QSet>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelWeightPaint)
};
bool fail(QString *error, const QString &text)
{
	if (error)
		*error = text;
	return false;
}
const QStringList &modeIds()
{
	static const QStringList ids{QStringLiteral("replace"), QStringLiteral("add"), QStringLiteral("subtract"), QStringLiteral("smooth")};
	return ids;
}
} // namespace

QJsonObject modelWeightStrokeRecipeJson(const QVector<ModelWeightStroke> &strokes)
{
	QJsonArray list;
	for (const auto &stroke : strokes)
	{
		QJsonArray samples;
		for (const auto &sample : stroke.samples)
			samples.append(QJsonArray{sample.vertex, sample.opacity});
		QJsonObject object{{QStringLiteral("surface"), stroke.surface},
						   {QStringLiteral("joint"), stroke.joint},
						   {QStringLiteral("mode"), modeIds().value(int(stroke.mode))},
						   {QStringLiteral("value"), stroke.value},
						   {QStringLiteral("strength"), stroke.strength},
						   {QStringLiteral("lockedJoints"), QJsonArray::fromStringList(stroke.lockedJoints)},
						   {QStringLiteral("samples"), samples}};
		if (!stroke.remainderJoint.isEmpty())
			object.insert(QStringLiteral("remainderJoint"), stroke.remainderJoint);
		list.append(object);
	}
	return QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("strokes"), list}};
}

bool parseModelWeightStrokeRecipe(const QJsonObject &root, QVector<ModelWeightStroke> *strokes, QString *error)
{
	if (!strokes)
		return false;
	if (root.size() != 2 || root.value(QStringLiteral("version")) != QJsonValue(1) || !root.value(QStringLiteral("strokes")).isArray())
		return fail(error, Text::tr("A stroke file needs version 1 and a strokes array."));
	const QSet<QString> allowed{QStringLiteral("surface"),		QStringLiteral("joint"),		  QStringLiteral("mode"),
								QStringLiteral("value"),		QStringLiteral("strength"),		  QStringLiteral("lockedJoints"),
								QStringLiteral("remainderJoint"), QStringLiteral("samples")};
	QVector<ModelWeightStroke> parsed;
	qint64 count = 0;
	for (const auto &entry : root.value(QStringLiteral("strokes")).toArray())
	{
		if (!entry.isObject())
			return fail(error, Text::tr("Every stroke must be an object."));
		const auto object = entry.toObject();
		for (auto it = object.constBegin(); it != object.constEnd(); ++it)
			if (!allowed.contains(it.key()))
				return fail(error, Text::tr("Unknown stroke field: %1.").arg(it.key()));
		ModelWeightStroke s;
		const auto surface = object.value(QStringLiteral("surface"));
		const auto mode = object.value(QStringLiteral("mode")).toString(QStringLiteral("replace"));
		if (!surface.isDouble() || surface.toDouble() != surface.toInt(-1) || surface.toInt(-1) < 0 ||
			!object.value(QStringLiteral("joint")).isString() || !object.value(QStringLiteral("samples")).isArray() ||
			!modeIds().contains(mode) || (object.contains(QStringLiteral("mode")) && !object.value(QStringLiteral("mode")).isString()) ||
			(object.contains(QStringLiteral("remainderJoint")) && !object.value(QStringLiteral("remainderJoint")).isString()) ||
			(object.contains(QStringLiteral("lockedJoints")) && !object.value(QStringLiteral("lockedJoints")).isArray()))
			return fail(error, Text::tr("Stroke surface, joint, mode, locks and samples have invalid types or values."));
		s.surface = surface.toInt();
		s.joint = object.value(QStringLiteral("joint")).toString();
		s.mode = ModelWeightPaintMode(modeIds().indexOf(mode));
		s.remainderJoint = object.value(QStringLiteral("remainderJoint")).toString();
		for (const auto &key : {QStringLiteral("value"), QStringLiteral("strength")})
			if (object.contains(key))
			{
				if (!object.value(key).isDouble())
					return fail(error, Text::tr("Stroke numbers must be JSON numbers."));
				(key == QStringLiteral("value") ? s.value : s.strength) = object.value(key).toDouble();
			}
		for (const auto &lock : object.value(QStringLiteral("lockedJoints")).toArray())
		{
			if (!lock.isString())
				return fail(error, Text::tr("Locked joints must be names."));
			s.lockedJoints.append(lock.toString());
		}
		for (const auto &sample : object.value(QStringLiteral("samples")).toArray())
		{
			const auto pair = sample.toArray();
			if (pair.size() != 2 || !pair[0].isDouble() || pair[0].toDouble() != pair[0].toInt(-1) || pair[0].toInt(-1) < 0 ||
				!pair[1].isDouble() || ++count > modelWeightMaxSamples)
				return fail(error, Text::tr("Samples must be [vertex, opacity] pairs within the sample limit."));
			s.samples.append({pair[0].toInt(), pair[1].toDouble()});
		}
		parsed.append(s);
		if (parsed.size() > modelWeightMaxStrokes)
			return fail(error, Text::tr("A recipe supports at most 1024 strokes."));
	}
	*strokes = std::move(parsed);
	return true;
}

} // namespace vibestudio
