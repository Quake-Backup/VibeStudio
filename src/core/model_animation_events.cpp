#include "core/model_animation_events.h"
#include "core/model_animation_authoring.h"
#include "core/model_document.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelAnimationEvents)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
bool validCommand(const QString &text)
{
	static const QRegularExpression pattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_.-]{0,63}$"));
	return pattern.match(text).hasMatch();
}
bool validArgument(const QString &text)
{
	if (text.isEmpty() || text.size() > 256 || text.contains(QStringLiteral("//")) || text.contains(QStringLiteral("/*")))
		return false;
	for (const QChar c : text)
		if (c.unicode() < 0x21 || c.unicode() > 0x7e || c == QLatin1Char('"') || c == QLatin1Char('{') || c == QLatin1Char('}') ||
			c == QLatin1Char('(') || c == QLatin1Char(')'))
			return false;
	return true;
}
// Absolute frames of every vertex animation's events, by animation name.
QHash<QString, QVector<int>> absoluteEvents(const ModelMesh &mesh)
{
	QHash<QString, QVector<int>> result;
	for (const auto &animation : mesh.animations)
	{
		QVector<int> frames;
		for (const auto &event : animation.events)
			frames.append(animation.firstFrame + event.frame);
		result.insert(animation.name, frames);
	}
	return result;
}
} // namespace

bool validModelAnimationEvent(const ModelAnimationEvent &event, int frameCount, QString *error)
{
	if (event.frame < 0 || event.frame >= frameCount)
		return fail(error, Text::tr("Event %1 is on frame %2, outside the clip's %3 frames. Move or remove it first.")
							   .arg(event.command)
							   .arg(event.frame)
							   .arg(frameCount));
	if (!validCommand(event.command))
		return fail(error, Text::tr("An event command is one word of up to 64 ASCII letters, digits, underscores, dots or hyphens, starting "
									"with a letter or underscore."));
	if (event.args.size() > 16)
		return fail(error, Text::tr("An event takes at most 16 arguments."));
	for (const auto &argument : event.args)
		if (!validArgument(argument))
			return fail(error, Text::tr("Event argument \"%1\" must be printable ASCII without spaces, quotes, braces, parentheses or "
										"comment marks.")
								   .arg(argument));
	return true;
}

bool validModelAnimationEvents(const QVector<ModelAnimationEvent> &events, int frameCount, QString *error)
{
	if (events.size() > modelAnimationMaxEventsPerClip)
		return fail(error, Text::tr("A clip holds at most 4,096 events."));
	for (const auto &event : events)
		if (!validModelAnimationEvent(event, frameCount, error))
			return false;
	return true;
}

QVector<ModelAnimationEvent> sortedModelAnimationEvents(QVector<ModelAnimationEvent> events)
{
	std::stable_sort(events.begin(), events.end(), [](const auto &a, const auto &b) { return a.frame < b.frame; });
	return events;
}

QString modelAnimationEventCommandLine(const ModelAnimationEvent &event)
{
	return (QStringList{event.command} + event.args).join(QLatin1Char(' '));
}

bool parseModelAnimationEventCommandLine(const QString &text, int frame, ModelAnimationEvent *event, QString *error)
{
	static const QRegularExpression spaces(QStringLiteral("\\s+"));
	auto words = text.split(spaces, Qt::SkipEmptyParts);
	if (words.isEmpty())
		return fail(error, Text::tr("Type an event command, such as sound_body snd_footstep."));
	ModelAnimationEvent result;
	result.frame = frame;
	result.command = words.takeFirst();
	result.args = words;
	if (!validModelAnimationEvent(result, std::numeric_limits<int>::max(), error))
		return false;
	if (event)
		*event = result;
	return true;
}

QJsonArray modelAnimationEventsJson(const QVector<ModelAnimationEvent> &events)
{
	QJsonArray result;
	for (const auto &event : events)
	{
		QJsonObject object{{QStringLiteral("frame"), event.frame}, {QStringLiteral("command"), event.command}};
		if (!event.args.isEmpty())
			object.insert(QStringLiteral("args"), QJsonArray::fromStringList(event.args));
		result.append(object);
	}
	return result;
}

bool modelAnimationEventsFromJson(const QJsonValue &value, int frameCount, QVector<ModelAnimationEvent> *events, QString *error)
{
	if (!value.isArray() || value.toArray().size() > modelAnimationMaxEventsPerClip)
		return fail(error, Text::tr("The source's animation events are malformed."));
	QVector<ModelAnimationEvent> result;
	for (const auto &entry : value.toArray())
	{
		const auto object = entry.toObject();
		ModelAnimationEvent event;
		if (!object.value(QStringLiteral("frame")).isDouble() || !object.value(QStringLiteral("command")).isString() ||
			(object.contains(QStringLiteral("args")) && !object.value(QStringLiteral("args")).isArray()))
			return fail(error, Text::tr("The source's animation events are malformed."));
		const double frame = object.value(QStringLiteral("frame")).toDouble();
		if (frame != std::floor(frame) || frame < 0 || frame > 1048576)
			return fail(error, Text::tr("The source's animation events are malformed."));
		event.frame = int(frame);
		event.command = object.value(QStringLiteral("command")).toString();
		for (const auto &argument : object.value(QStringLiteral("args")).toArray())
		{
			if (!argument.isString())
				return fail(error, Text::tr("The source's animation events are malformed."));
			event.args.append(argument.toString());
		}
		result.append(event);
	}
	if (!validModelAnimationEvents(result, frameCount, error))
		return false;
	*events = result;
	return true;
}

bool validateModelAnimationEvents(const ModelMesh &mesh, QString *error)
{
	for (const auto &clip : mesh.skeleton.clips)
		if (!clip.events.isEmpty() && !validModelAnimationEvents(clip.events, int(clip.frames.size()), error))
			return error ? fail(error, Text::tr("Skeletal clip %1: %2").arg(clip.name, *error)) : false;
	for (const auto &animation : mesh.animations)
	{
		if (animation.events.isEmpty())
			continue;
		if (!mesh.skeleton.isEmpty())
			return fail(error, Text::tr("Animation %1 keeps events, but a model with joints keeps events on its skeletal clips. Remove them "
										"from the animation first.")
								   .arg(animation.name));
		if (!validModelAnimationEvents(animation.events, animation.frameCount, error))
			return error ? fail(error, Text::tr("Animation %1: %2").arg(animation.name, *error)) : false;
	}
	return true;
}

bool remapModelAnimationEvents(const ModelMesh &before, ModelMesh *after, const ModelEdit &edit, QString *error)
{
	if (!after || std::none_of(before.animations.cbegin(), before.animations.cend(), [](const auto &a) { return !a.events.isEmpty(); }))
		return true;
	const auto kind = edit.kind;
	const bool duplicate = kind == ModelEditKind::DuplicateFrame, remove = kind == ModelEditKind::DeleteFrame,
			   inbetweens = kind == ModelEditKind::InsertInbetweens;
	if (duplicate || remove || inbetweens)
	{
		// Animation names are unique; ranges move with their frames.
		const auto frames = absoluteEvents(before);
		for (auto &animation : after->animations)
		{
			const auto source = std::find_if(before.animations.cbegin(), before.animations.cend(),
											 [&](const auto &a) { return a.name == animation.name; });
			if (source == before.animations.cend() || source->events.isEmpty())
				continue;
			QVector<ModelAnimationEvent> events;
			const auto absolute = frames.value(animation.name);
			for (int i = 0; i < source->events.size(); ++i)
			{
				int frame = absolute[i];
				if (remove && frame == edit.frame)
					return fail(error, Text::tr("Frame %1 carries event %2 of animation %3. Move or remove the event before deleting the frame.")
										   .arg(edit.frame)
										   .arg(source->events[i].command, animation.name));
				if ((duplicate && frame > edit.frame) || (inbetweens && frame > edit.frame))
					frame += duplicate ? 1 : edit.inbetweenCount;
				else if (remove && frame > edit.frame)
					--frame;
				auto event = source->events[i];
				event.frame = frame - animation.firstFrame;
				events.append(event);
			}
			animation.events = events;
		}
		for (const auto &animation : before.animations)
			if (!animation.events.isEmpty() &&
				std::none_of(after->animations.cbegin(), after->animations.cend(), [&](const auto &a) { return a.name == animation.name; }))
				return fail(error, Text::tr("This edit would remove animation %1 and its events. Remove its events first.").arg(animation.name));
		return true;
	}
	// Edits of the animation list itself keep each animation's events with it;
	// any other edit that moves an evented range cannot say where its events go.
	if (kind == ModelEditKind::AddAnimation || kind == ModelEditKind::RenameAnimation || kind == ModelEditKind::SetAnimationRange ||
		kind == ModelEditKind::SetAnimationRate || kind == ModelEditKind::DeleteAnimation || kind == ModelEditKind::EditAnimationEvents)
		return true;
	for (const auto &animation : before.animations)
	{
		if (animation.events.isEmpty())
			continue;
		const auto same = std::find_if(after->animations.cbegin(), after->animations.cend(), [&](const auto &a) {
			return a.name == animation.name && a.firstFrame == animation.firstFrame && a.frameCount == animation.frameCount;
		});
		if (same == after->animations.cend())
			return fail(error, Text::tr("This edit moves or removes frames of animation %1, which has events. Remove its events first, then "
										"add them again.")
								   .arg(animation.name));
	}
	return true;
}

bool applyModelAnimationEvents(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!mesh || !work.check())
		return false;
	const auto &o = edit.animation.events;
	if (edit.kind != ModelEditKind::EditAnimationEvents || !defaultModelSkeletalOptions(edit.skeletal))
		return fail(error, Text::tr("Event edits accept only their own clip and event settings."));
	if ((o.clip >= 0) == (o.animation >= 0))
		return fail(error, Text::tr("Choose either a skeletal clip or a vertex animation for the events."));
	QVector<ModelAnimationEvent> *events = nullptr;
	int frameCount = 0;
	if (o.clip >= 0)
	{
		if (o.clip >= mesh->skeleton.clips.size())
			return fail(error, Text::tr("Choose an existing skeletal clip."));
		events = &mesh->skeleton.clips[o.clip].events;
		frameCount = int(mesh->skeleton.clips[o.clip].frames.size());
	}
	else
	{
		if (o.animation >= mesh->animations.size())
			return fail(error, Text::tr("Choose an existing animation."));
		if (!mesh->skeleton.isEmpty())
			return fail(error, Text::tr("A model with joints keeps events on its skeletal clips. Choose a clip instead of an animation."));
		events = &mesh->animations[o.animation].events;
		frameCount = mesh->animations[o.animation].frameCount;
	}
	const bool add = o.action == ModelAnimationEventAction::Add, removeEvent = o.action == ModelAnimationEventAction::Remove;
	const bool move = o.action == ModelAnimationEventAction::Move, replace = o.action == ModelAnimationEventAction::Replace;
	if ((!add && !(o.event == ModelAnimationEvent{})) || (!(removeEvent || move) && o.index != -1) || (!move && o.frame != 0) ||
		(!replace && !o.events.isEmpty()))
		return fail(error, Text::tr("The event settings do not match the add, remove, move or replace operation."));
	auto updated = *events;
	if (add)
	{
		if (!validModelAnimationEvent(o.event, frameCount, error))
			return false;
		updated.append(o.event);
	}
	else if (removeEvent || move)
	{
		if (o.index < 0 || o.index >= updated.size())
			return fail(error, Text::tr("Choose an existing event by its index."));
		if (removeEvent)
			updated.removeAt(o.index);
		else
		{
			updated[o.index].frame = o.frame;
			if (!validModelAnimationEvent(updated[o.index], frameCount, error))
				return false;
		}
	}
	else if (replace)
	{
		if (!validModelAnimationEvents(o.events, frameCount, error))
			return false;
		updated = o.events;
	}
	else
		return fail(error, Text::tr("Choose to add, remove, move or replace events."));
	updated = sortedModelAnimationEvents(updated);
	if (!validModelAnimationEvents(updated, frameCount, error))
		return false;
	*events = updated;
	return work.check();
}
} // namespace vibestudio
