#include "app/model_frame_timeline.h"

#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QStyleOptionFocusRect>
#include <QHash>
#include <QHelpEvent>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
constexpr int maximumLanes = 3;

// 1, 2, 5, 10, 20, 50 ... at least `minimum`.
int niceStep(double minimum)
{
	int base = 1;
	while (true)
	{
		for (int factor : {1, 2, 5})
			if (base * factor >= minimum)
				return base * factor;
		if (base > 100000)
			return base * 10;
		base *= 10;
	}
}
} // namespace

ModelFrameTimeline::ModelFrameTimeline(QWidget *parent) : QAbstractSlider(parent)
{
	setObjectName(QStringLiteral("meshFrameTimeline"));
	setOrientation(Qt::Horizontal);
	setFocusPolicy(Qt::StrongFocus);
	setRange(0, 0);
	setSingleStep(1);
	setPageStep(10);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Frame timeline"));
	setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
										   "Click or drag to choose a frame. Shift-click or Shift-drag selects the frame range the range "
										   "tools use. Arrow keys step, Page Up and Page Down jump ten frames, Home and End go to the "
										   "ends; hold Shift to extend the range."));
}

void ModelFrameTimeline::setModel(const ModelMesh &mesh)
{
	m_names.clear();
	for (const auto &frame : mesh.frames)
		m_names.append(frame.name);
	m_clips = mesh.animations;
	m_groups.clear();
	if (mesh.mdl.enabled)
		for (const auto &group : mesh.mdl.frameGroups)
			if (!group.intervals.isEmpty())
				m_groups.append(group);
	// Overlapping clips stack in lanes, earliest first.
	QVector<int> order(m_clips.size());
	for (int i = 0; i < order.size(); ++i)
		order[i] = i;
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return m_clips[a].firstFrame < m_clips[b].firstFrame; });
	m_clipLane.fill(0, m_clips.size());
	QVector<int> laneEnd;
	for (int index : order)
	{
		int lane = 0;
		while (lane < laneEnd.size() && laneEnd[lane] >= m_clips[index].firstFrame)
			++lane;
		if (lane == laneEnd.size())
			laneEnd.append(-1);
		laneEnd[lane] = m_clips[index].firstFrame + m_clips[index].frameCount - 1;
		m_clipLane[index] = lane;
	}
	// Event markers at absolute frames: a vertex clip's events count from its
	// first frame, a skeletal clip's from its first baked frame.
	m_events.clear();
	const auto eventText = [](const ModelAnimationEvent &event)
	{ return event.args.isEmpty() ? event.command : event.command + QLatin1Char(' ') + event.args.join(QLatin1Char(' ')); };
	for (const auto &clip : std::as_const(m_clips))
		for (const auto &event : clip.events)
			if (event.frame >= 0 && event.frame < clip.frameCount)
				m_events.append({clip.firstFrame + event.frame, eventText(event)});
	QHash<int, int> firstBaked;
	for (int frame = 0; frame < mesh.skeleton.bakedClipForFrame.size(); ++frame)
		if (const int clip = mesh.skeleton.bakedClipForFrame[frame]; clip >= 0 && !firstBaked.contains(clip))
			firstBaked.insert(clip, frame);
	for (auto it = firstBaked.cbegin(); it != firstBaked.cend(); ++it)
		if (it.key() < mesh.skeleton.clips.size())
			for (const auto &event : mesh.skeleton.clips[it.key()].events)
				if (event.frame >= 0 && it.value() + event.frame < mesh.frames.size() &&
					mesh.skeleton.bakedClipForFrame.value(it.value() + event.frame, -1) == it.key())
					m_events.append({it.value() + event.frame, eventText(event)});
	std::stable_sort(m_events.begin(), m_events.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	const int last = std::max(0, int(m_names.size()) - 1);
	setMaximum(last);
	m_rangeFirst = std::min(m_rangeFirst, last);
	m_rangeLast = std::min(m_rangeLast, last);
	updateGeometry();
	describe();
	update();
}

void ModelFrameTimeline::setFrameRange(int first, int last)
{
	selectRange(first, last, false);
}

void ModelFrameTimeline::selectRange(int first, int last, bool notify)
{
	const int top = maximum();
	const int a = std::clamp(std::min(first, last), 0, top), b = std::clamp(std::max(first, last), 0, top);
	const bool changed = a != m_rangeFirst || b != m_rangeLast;
	m_rangeFirst = a;
	m_rangeLast = b;
	if (changed)
	{
		describe();
		update();
	}
	if (notify && m_rangeChanged)
		m_rangeChanged(a, b);
}

void ModelFrameTimeline::setHighContrast(bool enabled)
{
	if (m_highContrast == enabled)
		return;
	m_highContrast = enabled;
	update();
}

int ModelFrameTimeline::lanes() const
{
	// No clips, no lane: the transport stays one row high at large text.
	if (m_clips.isEmpty())
		return 0;
	int count = 0;
	for (int lane : m_clipLane)
		count = std::max(count, lane + 1);
	return std::clamp(count, 1, maximumLanes);
}

double ModelFrameTimeline::frameX(int frame) const
{
	const double margin = fontMetrics().averageCharWidth();
	const double usable = std::max(1.0, width() - 2 * margin);
	return margin + (frame + 0.5) * usable / (maximum() + 1);
}

int ModelFrameTimeline::frameAt(double x) const
{
	const double margin = fontMetrics().averageCharWidth();
	const double usable = std::max(1.0, width() - 2 * margin);
	return std::clamp(int(std::floor((x - margin) * (maximum() + 1) / usable)), 0, maximum());
}

QSize ModelFrameTimeline::sizeHint() const
{
	const auto metrics = fontMetrics();
	const int height = metrics.height() + 8 + lanes() * (metrics.height() + 4) + (m_groups.isEmpty() ? 0 : metrics.height() / 2 + 4) + 4;
	return {std::clamp((maximum() + 1) * 8, metrics.averageCharWidth() * 30, metrics.averageCharWidth() * 120), height};
}

QSize ModelFrameTimeline::minimumSizeHint() const
{
	return {fontMetrics().averageCharWidth() * 16, sizeHint().height()};
}

QString ModelFrameTimeline::frameDescription(int frame) const
{
	const QLocale locale;
	QString text = QCoreApplication::translate("VibeStudioModelEditor", "Frame %1 of %2: %3.")
					   .arg(locale.toString(frame))
					   .arg(locale.toString(maximum() + 1))
					   .arg(m_names.value(frame));
	QStringList clips;
	for (const auto &clip : m_clips)
		if (frame >= clip.firstFrame && frame < clip.firstFrame + clip.frameCount)
			clips.append(clip.name);
	if (!clips.isEmpty())
		text += QLatin1Char(' ') + QCoreApplication::translate("VibeStudioModelEditor", "In clips: %1.").arg(clips.join(QStringLiteral(", ")));
	if (const auto here = eventsAt(frame); !here.isEmpty())
		text += QLatin1Char(' ') + QCoreApplication::translate("VibeStudioModelEditor", "Events: %1.").arg(here.join(QStringLiteral("; ")));
	text += QLatin1Char(' ') + QCoreApplication::translate("VibeStudioModelEditor", "Range %1 to %2.")
								   .arg(locale.toString(m_rangeFirst))
								   .arg(locale.toString(m_rangeLast));
	return text;
}

QStringList ModelFrameTimeline::eventsAt(int frame) const
{
	QStringList result;
	for (const auto &event : m_events)
		if (event.first == frame)
			result.append(event.second);
	return result;
}

bool ModelFrameTimeline::event(QEvent *event)
{
	// Hovering an event marker names its commands; elsewhere the usual tooltip.
	if (event->type() == QEvent::ToolTip && !m_events.isEmpty())
	{
		const auto *help = static_cast<QHelpEvent *>(event);
		const int frame = frameAt(help->pos().x());
		if (const auto here = eventsAt(frame); !here.isEmpty())
		{
			QToolTip::showText(help->globalPos(),
							   QCoreApplication::translate("VibeStudioModelEditor", "Frame %1 events: %2")
								   .arg(QLocale().toString(frame), here.join(QStringLiteral("; "))),
							   this);
			return true;
		}
	}
	return QAbstractSlider::event(event);
}

void ModelFrameTimeline::describe()
{
	setAccessibleDescription(frameDescription(value()));
}

void ModelFrameTimeline::sliderChange(SliderChange change)
{
	QAbstractSlider::sliderChange(change);
	if (change == SliderValueChange || change == SliderRangeChange)
		describe();
	update();
}

void ModelFrameTimeline::changeEvent(QEvent *event)
{
	QAbstractSlider::changeEvent(event);
	if (event->type() == QEvent::FontChange || event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
	{
		updateGeometry();
		update();
	}
}

void ModelFrameTimeline::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	const auto colours = palette();
	const QColor background = colours.color(QPalette::Base), text = colours.color(QPalette::Text);
	const QColor bar = colours.color(QPalette::Button), barText = colours.color(QPalette::ButtonText);
	const QColor accent = colours.color(QPalette::Highlight);
	painter.fillRect(rect(), background);
	const int frames = maximum() + 1;
	if (m_names.isEmpty())
		return;
	const auto metrics = fontMetrics();
	const double spacing = (frameX(1) - frameX(0));
	const double half = spacing / 2;
	const int labelTop = 2, tickTop = labelTop + metrics.height(), laneTop = tickTop + 6;
	const int laneHeight = metrics.height() + 4;
	const int groupTop = laneTop + lanes() * laneHeight;
	painter.setRenderHint(QPainter::Antialiasing, false);
	// The selected range: a tinted band with a dashed outline, so it does not
	// rely on colour alone.
	{
		const QRectF band(frameX(m_rangeFirst) - half, 1, frameX(m_rangeLast) - frameX(m_rangeFirst) + spacing, height() - 2);
		if (!m_highContrast)
		{
			auto tint = accent;
			tint.setAlpha(48);
			painter.fillRect(band, tint);
		}
		QPen outline(m_highContrast ? text : accent, m_highContrast ? 2 : 1, Qt::DashLine);
		painter.setPen(outline);
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(band.adjusted(0.5, 0.5, -0.5, -0.5));
	}
	// Frame ticks and numbers.
	const int tickEvery = niceStep(4.0 / std::max(spacing, 1e-6));
	const QLocale locale;
	const int labelWidth = metrics.horizontalAdvance(locale.toString(frames - 1)) + metrics.averageCharWidth() * 2;
	const int labelEvery = std::max(tickEvery, niceStep(labelWidth / std::max(spacing, 1e-6)));
	painter.setPen(QPen(text, 1));
	for (int frame = 0; frame < frames; frame += tickEvery)
	{
		const double x = std::round(frameX(frame));
		const bool labelled = frame % labelEvery == 0;
		painter.drawLine(QPointF(x, tickTop + (labelled ? 0 : 3)), QPointF(x, tickTop + 5));
		if (labelled)
		{
			const auto label = locale.toString(frame);
			const int w = metrics.horizontalAdvance(label);
			const double left = std::clamp(x - w / 2.0, 0.0, double(width() - w));
			painter.drawText(QPointF(left, labelTop + metrics.ascent()), label);
		}
	}
	// Clip bars, stacked in lanes.
	int hidden = 0;
	for (int i = 0; i < m_clips.size(); ++i)
	{
		const auto &clip = m_clips[i];
		if (m_clipLane[i] >= maximumLanes)
		{
			++hidden;
			continue;
		}
		const QRectF box(frameX(clip.firstFrame) - half + 1, laneTop + m_clipLane[i] * laneHeight + 1,
						 std::max(2.0, clip.frameCount * spacing - 2), laneHeight - 2);
		painter.setPen(QPen(text, 1));
		painter.setBrush(bar);
		painter.drawRect(box);
		painter.setPen(barText);
		const auto name = metrics.elidedText(clip.name, Qt::ElideRight, int(box.width()) - 4);
		painter.drawText(box.adjusted(2, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter, name);
	}
	if (hidden > 0)
	{
		painter.setPen(text);
		const auto more = QCoreApplication::translate("VibeStudioModelEditor", "+%1").arg(locale.toString(hidden));
		painter.drawText(QRectF(0, laneTop + (maximumLanes - 1) * laneHeight, width() - 2, laneHeight), Qt::AlignRight | Qt::AlignVCenter, more);
	}
	// Animation events: outlined diamonds on the tick band, so they read by
	// shape as well as colour.
	if (!m_events.isEmpty())
	{
		painter.setRenderHint(QPainter::Antialiasing, true);
		const double r = std::clamp(metrics.height() / 5.0, 3.0, 6.0), y = tickTop + 3;
		painter.setPen(QPen(text, 1));
		painter.setBrush(m_highContrast ? background : accent);
		int previous = -1;
		for (const auto &event : m_events)
		{
			if (event.first == previous)
				continue;
			previous = event.first;
			const double ex = frameX(event.first);
			const QPointF diamond[4] = {{ex, y - r}, {ex + r, y}, {ex, y + r}, {ex - r, y}};
			painter.drawPolygon(diamond, 4);
		}
		painter.setRenderHint(QPainter::Antialiasing, false);
	}
	// Timed MDL native groups as brackets.
	if (!m_groups.isEmpty())
	{
		painter.setPen(QPen(text, 1));
		const int y = groupTop + metrics.height() / 4 + 2;
		for (const auto &group : m_groups)
		{
			const double left = frameX(group.firstFrame) - half + 2, right = frameX(group.firstFrame + group.frameCount() - 1) + half - 2;
			painter.drawLine(QPointF(left, y), QPointF(right, y));
			painter.drawLine(QPointF(left, y - 3), QPointF(left, y + 3));
			painter.drawLine(QPointF(right, y - 3), QPointF(right, y + 3));
		}
	}
	// The current frame: a heavy line with a marker on top.
	painter.setRenderHint(QPainter::Antialiasing, true);
	const double x = frameX(value());
	const QColor current = m_highContrast ? text : accent;
	painter.setPen(QPen(current, 3));
	painter.drawLine(QPointF(x, tickTop), QPointF(x, height() - 1));
	QPainterPath marker;
	marker.moveTo(x - 5, tickTop - 1);
	marker.lineTo(x + 5, tickTop - 1);
	marker.lineTo(x, tickTop + 5);
	marker.closeSubpath();
	painter.fillPath(marker, current);
	if (hasFocus())
	{
		QStyleOptionFocusRect focus;
		focus.initFrom(this);
		focus.backgroundColor = background;
		style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, &painter, this);
	}
}

void ModelFrameTimeline::keyPressEvent(QKeyEvent *event)
{
	// Time runs left to right in every layout direction.
	int target = value();
	switch (event->key())
	{
	case Qt::Key_Left:
		target -= 1;
		break;
	case Qt::Key_Right:
		target += 1;
		break;
	case Qt::Key_PageUp:
		target -= pageStep();
		break;
	case Qt::Key_PageDown:
		target += pageStep();
		break;
	case Qt::Key_Home:
		target = 0;
		break;
	case Qt::Key_End:
		target = maximum();
		break;
	default:
		QAbstractSlider::keyPressEvent(event);
		return;
	}
	target = std::clamp(target, 0, maximum());
	if (event->modifiers() & Qt::ShiftModifier)
	{
		if (m_anchor < 0)
			m_anchor = value();
		selectRange(m_anchor, target, true);
	}
	else
		m_anchor = -1;
	setValue(target);
	event->accept();
}

void ModelFrameTimeline::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
	{
		QAbstractSlider::mousePressEvent(event);
		return;
	}
	const int frame = frameAt(event->position().x());
	m_extending = event->modifiers() & Qt::ShiftModifier;
	if (m_extending)
	{
		if (m_anchor < 0)
			m_anchor = value();
		selectRange(m_anchor, frame, false);
	}
	else
		m_anchor = frame;
	m_dragging = true;
	setFocus(Qt::MouseFocusReason);
	setValue(frame);
	event->accept();
}

void ModelFrameTimeline::mouseMoveEvent(QMouseEvent *event)
{
	if (!m_dragging)
		return;
	const int frame = frameAt(event->position().x());
	if (m_extending)
		selectRange(m_anchor, frame, false);
	setValue(frame);
	event->accept();
}

void ModelFrameTimeline::mouseReleaseEvent(QMouseEvent *event)
{
	if (m_dragging && m_extending)
		selectRange(m_rangeFirst, m_rangeLast, true);
	m_dragging = false;
	m_extending = false;
	event->accept();
}
} // namespace vibestudio
