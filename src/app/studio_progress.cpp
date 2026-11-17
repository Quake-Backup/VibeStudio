#include "app/studio_progress.h"
#include "app/studio_accessibility.h"
#include "app/studio_layout.h"
#include "app/studio_theme.h"

#include <QAccessible>
#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QTimerEvent>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace slopstudio {

// ---- Start-up stages -------------------------------------------------------

namespace {

struct StartupStageState {
	StartupStageListener listener;
	int reported = 0;
	QString last;
};

StartupStageState& startupStageState()
{
	static StartupStageState state;
	return state;
}

} // namespace

void setStartupStageListener(StartupStageListener listener)
{
	startupStageState().listener = std::move(listener);
}

void reportStartupStage(const QString& label)
{
	StartupStageState& state = startupStageState();
	++state.reported;
	state.last = label;
	if (state.listener) {
		state.listener(label, state.reported);
	}
}

int startupStagesReported()
{
	return startupStageState().reported;
}

QString lastStartupStage()
{
	return startupStageState().last;
}

void resetStartupStages()
{
	StartupStageState& state = startupStageState();
	state.reported = 0;
	state.last.clear();
}

// ---- Work tracker ------------------------------------------------------------

int WorkTracker::begin(const QString& label, OperationProgress progress)
{
	Item item;
	item.token = m_nextToken++;
	item.label = label.trimmed();
	item.progress = progress;
	m_items.push_back(item);
	emit changed();
	return item.token;
}

void WorkTracker::update(int token, const QString& label, OperationProgress progress)
{
	for (Item& item : m_items) {
		if (item.token != token) {
			continue;
		}
		const QString trimmed = label.trimmed();
		if (item.label == trimmed && item.progress.current == progress.current && item.progress.total == progress.total) {
			return;
		}
		item.label = trimmed;
		item.progress = progress;
		emit changed();
		return;
	}
}

void WorkTracker::setProgress(int token, OperationProgress progress)
{
	for (Item& item : m_items) {
		if (item.token != token) {
			continue;
		}
		if (item.progress.current == progress.current && item.progress.total == progress.total) {
			return;
		}
		item.progress = progress;
		emit changed();
		return;
	}
}

void WorkTracker::end(int token)
{
	for (int index = 0; index < m_items.size(); ++index) {
		if (m_items.at(index).token == token) {
			m_items.removeAt(index);
			emit changed();
			return;
		}
	}
}

bool WorkTracker::contains(int token) const
{
	return std::any_of(m_items.cbegin(), m_items.cend(), [token](const Item& item) { return item.token == token; });
}

int WorkTracker::activeCount() const
{
	return m_items.size();
}

QStringList WorkTracker::activeLabels() const
{
	QStringList labels;
	for (const Item& item : m_items) {
		if (!item.label.isEmpty() && !labels.contains(item.label)) {
			labels.push_back(item.label);
		}
	}
	return labels;
}

QVector<WorkTracker::Item> WorkTracker::items() const
{
	return m_items;
}

OperationProgress WorkTracker::progress() const
{
	OperationProgress total;
	for (const Item& item : m_items) {
		if (item.progress.total > 0) {
			total.total += item.progress.total;
			total.current += std::clamp(item.progress.current, 0, item.progress.total);
		}
	}
	return total;
}

QString WorkTracker::summaryText() const
{
	if (m_items.isEmpty()) {
		return tr("Nothing is running.");
	}
	const QStringList labels = activeLabels();
	if (m_items.size() == 1) {
		return labels.isEmpty() ? tr("One task is running.") : labels.first();
	}
	const int count = m_items.size();
	return tr("%n tasks are running.", nullptr, count);
}

void WorkTracker::clear()
{
	if (m_items.isEmpty()) {
		return;
	}
	m_items.clear();
	emit changed();
}

WorkTracker& studioWorkTracker()
{
	static WorkTracker tracker;
	return tracker;
}

// ---- Busy indicator ---------------------------------------------------------

namespace {

bool indeterminate(const OperationProgress& progress)
{
	return progress.total <= 0;
}

int defaultDiameter(const QWidget* widget)
{
	const QFontMetrics metrics = widget ? widget->fontMetrics() : QFontMetrics(QGuiApplication::font());
	return std::max(16, static_cast<int>(std::lround(metrics.height() * 1.4)));
}

void paintRing(QPainter& painter, const QRectF& bounds, qreal angle, const OperationProgress& progress, const QColor& arc, const QColor& track, const QFont& font, const QColor& text)
{
	const qreal diameter = std::min(bounds.width(), bounds.height());
	const qreal stroke = std::max<qreal>(2.0, diameter / 8.0);
	const QRectF ring(bounds.center().x() - diameter / 2 + stroke / 2, bounds.center().y() - diameter / 2 + stroke / 2, diameter - stroke, diameter - stroke);
	painter.setRenderHint(QPainter::Antialiasing, true);
	QPen pen(track, stroke);
	pen.setCapStyle(Qt::FlatCap);
	painter.setPen(pen);
	painter.setBrush(Qt::NoBrush);
	painter.drawEllipse(ring);
	pen.setColor(arc);
	pen.setCapStyle(Qt::RoundCap);
	painter.setPen(pen);
	// QPainter angles run counter-clockwise from three o'clock in sixteenths
	// of a degree; the ring reads clockwise from the top.
	if (indeterminate(progress)) {
		const int start = static_cast<int>(std::lround((90.0 - angle) * 16.0));
		painter.drawArc(ring, start, -90 * 16);
		return;
	}
	const int percent = operationProgressPercent(progress);
	if (percent > 0) {
		painter.drawArc(ring, 90 * 16, -static_cast<int>(std::lround(percent * 3.6 * 16.0)));
	}
	if (diameter >= 34) {
		QFont small = font;
		small.setPointSizeF(std::max(6.0, std::min(font.pointSizeF(), diameter / 4.0)));
		painter.setFont(small);
		painter.setPen(text);
		painter.drawText(ring, Qt::AlignCenter, directionIsolated(QStringLiteral("%1%").arg(percent)));
	}
}

} // namespace

BusyIndicator::BusyIndicator(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("busyIndicator"));
	setAttribute(Qt::WA_TransparentForMouseEvents, true);
	setFocusPolicy(Qt::NoFocus);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	m_reducedMotion = studioReducedMotion();
	refreshAccessibleText();
}

void BusyIndicator::setActive(bool active)
{
	if (m_active == active) {
		return;
	}
	m_active = active;
	refreshTimer();
	refreshAccessibleText();
	updateGeometry();
	update();
}

bool BusyIndicator::isActive() const
{
	return m_active;
}

void BusyIndicator::setProgress(OperationProgress progress)
{
	progress.total = std::max(0, progress.total);
	progress.current = std::clamp(progress.current, 0, progress.total);
	if (m_progress.current == progress.current && m_progress.total == progress.total) {
		return;
	}
	m_progress = progress;
	refreshTimer();
	refreshAccessibleText();
	update();
}

OperationProgress BusyIndicator::progress() const
{
	return m_progress;
}

void BusyIndicator::setReducedMotion(bool reducedMotion)
{
	if (m_reducedMotion == reducedMotion) {
		return;
	}
	m_reducedMotion = reducedMotion;
	m_angle = 0;
	refreshTimer();
	update();
}

bool BusyIndicator::reducedMotion() const
{
	return m_reducedMotion;
}

void BusyIndicator::setDiameter(int diameter)
{
	m_diameter = std::max(0, diameter);
	updateGeometry();
	update();
}

int BusyIndicator::diameter() const
{
	return m_diameter > 0 ? m_diameter : defaultDiameter(this);
}

bool BusyIndicator::isTurning() const
{
	return m_timerId != 0;
}

qreal BusyIndicator::angle() const
{
	return m_angle;
}

QSize BusyIndicator::sizeHint() const
{
	if (!m_active) {
		return QSize(0, 0);
	}
	const int size = diameter();
	return QSize(size, size);
}

QSize BusyIndicator::minimumSizeHint() const
{
	return sizeHint();
}

QPixmap BusyIndicator::framePixmap(int diameter, qreal devicePixelRatio, qreal angle, OperationProgress progress, const QColor& arc, const QColor& track)
{
	const StudioThemeTokens& tokens = currentStudioTheme();
	const int size = std::max(1, diameter);
	const qreal ratio = devicePixelRatio > 0 ? devicePixelRatio : 1.0;
	QPixmap pixmap(static_cast<int>(std::ceil(size * ratio)), static_cast<int>(std::ceil(size * ratio)));
	pixmap.setDevicePixelRatio(ratio);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	paintRing(painter, QRectF(0, 0, size, size), angle, progress,
		arc.isValid() ? arc : tokens.colors.selection, track.isValid() ? track : tokens.colors.border, QGuiApplication::font(), tokens.colors.text);
	return pixmap;
}

void BusyIndicator::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event);
	if (!m_active) {
		return;
	}
	QPainter painter(this);
	const QPalette colours = palette();
	const StudioThemeTokens& tokens = currentStudioTheme();
	const int size = std::min({diameter(), width(), height()});
	const QRectF bounds(QPointF((width() - size) / 2.0, (height() - size) / 2.0), QSizeF(size, size));
	paintRing(painter, bounds, m_angle, m_progress, colours.color(QPalette::Highlight), tokens.colors.border, font(), colours.color(QPalette::WindowText));
}

void BusyIndicator::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	refreshTimer();
}

void BusyIndicator::hideEvent(QHideEvent* event)
{
	QWidget::hideEvent(event);
	refreshTimer();
}

void BusyIndicator::timerEvent(QTimerEvent* event)
{
	if (event->timerId() != m_timerId) {
		QWidget::timerEvent(event);
		return;
	}
	m_angle = std::fmod(m_angle + kDegreesPerTick, 360.0);
	update();
}

void BusyIndicator::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange) {
		updateGeometry();
	}
}

void BusyIndicator::refreshTimer()
{
	const bool wanted = m_active && isVisible() && indeterminate(m_progress) && !m_reducedMotion;
	if (wanted && m_timerId == 0) {
		// Precise: a coarse timer is starved behind slow paints on Windows,
		// and the ring would stutter exactly when it should reassure.
		m_timerId = startTimer(kTickIntervalMs, Qt::PreciseTimer);
	} else if (!wanted && m_timerId != 0) {
		killTimer(m_timerId);
		m_timerId = 0;
		m_angle = 0;
	}
}

void BusyIndicator::refreshAccessibleText()
{
	if (!m_active) {
		setAccessibleName(QString());
		setAccessibleDescription(QString());
		return;
	}
	setAccessibleName(tr("Working"));
	setAccessibleDescription(indeterminate(m_progress) ? tr("Work is in progress.") : tr("%1% complete.").arg(operationProgressPercent(m_progress)));
}

// ---- Busy overlay -------------------------------------------------------------

BusyOverlay::BusyOverlay(QWidget* host)
	: QWidget(host)
	, m_host(host)
{
	setObjectName(QStringLiteral("busyOverlay"));
	setFocusPolicy(Qt::StrongFocus);
	setAttribute(Qt::WA_NoSystemBackground, true);
	hide();

	m_card = new QWidget(this);
	m_card->setObjectName(QStringLiteral("busyOverlayCard"));
	auto* cardLayout = new QVBoxLayout(m_card);
	cardLayout->setContentsMargins(24, 20, 24, 20);
	cardLayout->setSpacing(10);
	m_indicator = new BusyIndicator(m_card);
	m_indicator->setDiameter(40);
	cardLayout->addWidget(m_indicator, 0, Qt::AlignHCenter);
	m_titleLabel = new QLabel(m_card);
	m_titleLabel->setObjectName(QStringLiteral("busyOverlayTitle"));
	m_titleLabel->setAlignment(Qt::AlignCenter);
	m_titleLabel->setWordWrap(true);
	m_titleLabel->setTextFormat(Qt::PlainText);
	cardLayout->addWidget(m_titleLabel);
	m_detailLabel = new QLabel(m_card);
	m_detailLabel->setObjectName(QStringLiteral("busyOverlayDetail"));
	m_detailLabel->setAlignment(Qt::AlignCenter);
	m_detailLabel->setWordWrap(true);
	m_detailLabel->setTextFormat(Qt::PlainText);
	cardLayout->addWidget(m_detailLabel);
	m_progressLabel = new QLabel(m_card);
	m_progressLabel->setObjectName(QStringLiteral("busyOverlayProgress"));
	m_progressLabel->setAlignment(Qt::AlignCenter);
	m_progressLabel->setTextFormat(Qt::PlainText);
	cardLayout->addWidget(m_progressLabel);
	m_cancelButton = createButton(tr("Cancel"), QStringLiteral("cancel"));
	m_cancelButton->setObjectName(QStringLiteral("busyOverlayCancel"));
	m_cancelButton->setParent(m_card);
	m_cancelButton->setVisible(false);
	connect(m_cancelButton, &QPushButton::clicked, this, &BusyOverlay::cancelRequested);
	cardLayout->addWidget(m_cancelButton, 0, Qt::AlignHCenter);

	m_appearanceTimer = new QTimer(this);
	m_appearanceTimer->setSingleShot(true);
	m_appearanceTimer->setTimerType(Qt::PreciseTimer);
	connect(m_appearanceTimer, &QTimer::timeout, this, &BusyOverlay::present);

	if (m_host) {
		m_host->installEventFilter(this);
	}
	refreshText();
}

BusyOverlay::~BusyOverlay()
{
	if (m_trackerToken != 0) {
		studioWorkTracker().end(m_trackerToken);
		m_trackerToken = 0;
	}
}

void BusyOverlay::begin(const QString& title, const QString& detail)
{
	m_title = title.trimmed();
	m_detail = detail.trimmed();
	m_progress = {};
	m_active = true;
	refreshText();
	refreshTracker();
	if (isVisible()) {
		return;
	}
	if (m_appearanceDelay <= 0) {
		present();
	} else if (!m_appearanceTimer->isActive()) {
		m_appearanceTimer->start(m_appearanceDelay);
	}
}

void BusyOverlay::beginNow(const QString& title, const QString& detail)
{
	begin(title, detail);
	m_appearanceTimer->stop();
	present();
	paintNow(this);
}

void BusyOverlay::setTitle(const QString& title)
{
	m_title = title.trimmed();
	refreshText();
	refreshTracker();
}

void BusyOverlay::setDetail(const QString& detail)
{
	m_detail = detail.trimmed();
	refreshText();
}

void BusyOverlay::setProgress(OperationProgress progress)
{
	progress.total = std::max(0, progress.total);
	progress.current = std::clamp(progress.current, 0, progress.total);
	m_progress = progress;
	m_indicator->setProgress(progress);
	refreshText();
	refreshTracker();
}

void BusyOverlay::setCancellable(bool cancellable)
{
	m_cancellable = cancellable;
	m_cancelButton->setVisible(cancellable);
	if (cancellable && isVisible()) {
		m_cancelButton->setFocus(Qt::OtherFocusReason);
	}
}

void BusyOverlay::setReducedMotion(bool reducedMotion)
{
	m_indicator->setReducedMotion(reducedMotion);
}

void BusyOverlay::setAppearanceDelay(int milliseconds)
{
	m_appearanceDelay = std::max(0, milliseconds);
}

void BusyOverlay::finish()
{
	m_appearanceTimer->stop();
	m_active = false;
	refreshTracker();
	if (!isVisible()) {
		return;
	}
	// Focus goes back to the host's surface rather than vanishing.
	const bool hadFocus = isAncestorOf(QWidget::focusWidget()) || hasFocus();
	hide();
	if (hadFocus && m_host) {
		m_host->setFocus(Qt::OtherFocusReason);
	}
}

bool BusyOverlay::isActive() const
{
	return m_active;
}

QString BusyOverlay::title() const
{
	return m_title;
}

QString BusyOverlay::detail() const
{
	return m_detail;
}

OperationProgress BusyOverlay::progress() const
{
	return m_progress;
}

bool BusyOverlay::isCancellable() const
{
	return m_cancellable;
}

int BusyOverlay::appearanceDelay() const
{
	return m_appearanceDelay;
}

BusyIndicator* BusyOverlay::indicator() const
{
	return m_indicator;
}

bool BusyOverlay::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == m_host && (event->type() == QEvent::Resize || event->type() == QEvent::LayoutDirectionChange)) {
		fitHost();
	}
	return QWidget::eventFilter(watched, event);
}

void BusyOverlay::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event);
	QPainter painter(this);
	const StudioThemeTokens& tokens = currentStudioTheme();
	// The surface shows through dimly, so the user knows where they are; the
	// high-visibility themes cover it fully, since a dimmed page there would
	// be a page that fails its own contrast.
	QColor veil = tokens.colors.surface;
	veil.setAlphaF(tokens.highContrast ? 1.0 : 0.82);
	painter.fillRect(rect(), veil);
	painter.setRenderHint(QPainter::Antialiasing, true);
	QPainterPath card;
	card.addRoundedRect(QRectF(m_card->geometry()).adjusted(0.5, 0.5, -0.5, -0.5), tokens.metrics.radius, tokens.metrics.radius);
	painter.fillPath(card, tokens.colors.panel);
	painter.setPen(QPen(tokens.highContrast ? tokens.colors.borderStrong : tokens.colors.border, tokens.metrics.borderWidth));
	painter.drawPath(card);
}

void BusyOverlay::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Escape && m_cancellable) {
		event->accept();
		emit cancelRequested();
		return;
	}
	// Keys do not fall through to the covered surface.
	event->accept();
}

void BusyOverlay::mousePressEvent(QMouseEvent* event)
{
	event->accept();
}

void BusyOverlay::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	fitHost();
	raise();
	if (m_cancellable) {
		m_cancelButton->setFocus(Qt::OtherFocusReason);
	} else {
		setFocus(Qt::OtherFocusReason);
	}
	if (studioAccessibilityPreferences().screenReaderAnnouncements && !m_title.isEmpty()) {
		announceToScreenReader(this, m_detail.isEmpty() ? m_title : tr("%1 %2").arg(m_title, m_detail));
	}
}

void BusyOverlay::hideEvent(QHideEvent* event)
{
	QWidget::hideEvent(event);
}

void BusyOverlay::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange) {
		fitHost();
	}
}

void BusyOverlay::present()
{
	if (!m_active || !m_host) {
		return;
	}
	m_indicator->setActive(true);
	fitHost();
	show();
	raise();
}

void BusyOverlay::fitHost()
{
	if (!m_host) {
		return;
	}
	setGeometry(m_host->rect());
	const int margin = 24;
	const int maxWidth = std::max(120, std::min(440, width() - 2 * margin));
	m_card->setFixedWidth(maxWidth);
	m_card->adjustSize();
	const QSize cardSize(maxWidth, std::min(m_card->sizeHint().height(), std::max(60, height() - 2 * margin)));
	m_card->resize(cardSize);
	m_card->move((width() - cardSize.width()) / 2, (height() - cardSize.height()) / 2);
}

void BusyOverlay::refreshText()
{
	m_titleLabel->setText(m_title);
	m_titleLabel->setVisible(!m_title.isEmpty());
	m_detailLabel->setText(m_detail);
	m_detailLabel->setVisible(!m_detail.isEmpty());
	const bool measurable = m_progress.total > 0;
	m_progressLabel->setText(measurable ? tr("%1 of %2").arg(QLocale().toString(m_progress.current), QLocale().toString(m_progress.total)) : QString());
	m_progressLabel->setVisible(measurable);
	setAccessibleName(m_title.isEmpty() ? tr("Working") : m_title);
	setAccessibleDescription(m_detail);
	if (isVisible()) {
		fitHost();
	}
}

void BusyOverlay::refreshTracker()
{
	WorkTracker& tracker = studioWorkTracker();
	if (m_active && m_trackerToken == 0) {
		m_trackerToken = tracker.begin(m_title, m_progress);
	} else if (m_active) {
		tracker.update(m_trackerToken, m_title, m_progress);
	} else if (m_trackerToken != 0) {
		tracker.end(m_trackerToken);
		m_trackerToken = 0;
	}
}

// ---- Responsiveness monitor -------------------------------------------------

UiResponsivenessMonitor::UiResponsivenessMonitor(QObject* parent)
	: QObject(parent)
{
}

void UiResponsivenessMonitor::setStallThreshold(int milliseconds)
{
	m_threshold = std::max(1, milliseconds);
}

int UiResponsivenessMonitor::stallThreshold() const
{
	return m_threshold;
}

void UiResponsivenessMonitor::setInterval(int milliseconds)
{
	m_interval = std::max(1, milliseconds);
	if (m_timerId != 0) {
		stop();
		start();
	}
}

int UiResponsivenessMonitor::interval() const
{
	return m_interval;
}

void UiResponsivenessMonitor::start()
{
	if (m_timerId != 0) {
		return;
	}
	// A monotonic clock: the wall clock moves when the user or a time server
	// sets it, and that is not the interface being held.
	m_beatClock.start();
	// Precise, so that the gap it measures is the stall and not the timer's
	// own slack.
	m_timerId = startTimer(m_interval, Qt::PreciseTimer);
}

void UiResponsivenessMonitor::stop()
{
	if (m_timerId == 0) {
		return;
	}
	killTimer(m_timerId);
	m_timerId = 0;
}

bool UiResponsivenessMonitor::isRunning() const
{
	return m_timerId != 0;
}

int UiResponsivenessMonitor::stallCount() const
{
	return m_stallCount;
}

qint64 UiResponsivenessMonitor::longestStallMs() const
{
	return m_longestStallMs;
}

qint64 UiResponsivenessMonitor::totalStallMs() const
{
	return m_totalStallMs;
}

QString UiResponsivenessMonitor::summaryText() const
{
	if (m_stallCount == 0) {
		return tr("The interface has not paused this session.");
	}
	return tr("%n interface pause(s) this session, the longest %1.", nullptr, m_stallCount)
		.arg(QLocale().toString(m_longestStallMs / 1000.0, 'f', 1) + QStringLiteral(" s"));
}

void UiResponsivenessMonitor::timerEvent(QTimerEvent* event)
{
	if (event->timerId() != m_timerId) {
		QObject::timerEvent(event);
		return;
	}
	noteBeat(m_beatClock.isValid() ? m_beatClock.restart() : 0);
}

void UiResponsivenessMonitor::noteBeat(qint64 gapMilliseconds)
{
	// The timer was due `interval` after the last beat; what it waited beyond
	// that is the time the interface thread was held.
	const qint64 stall = gapMilliseconds - m_interval;
	if (stall < m_threshold) {
		return;
	}
	// A gap of minutes is the computer asleep or the process suspended (the
	// monotonic clock runs on through both on some systems), not a window
	// that did not answer: it goes to the log and is neither counted nor
	// reported as a pause.
	if (gapMilliseconds >= kSuspensionGapMs) {
		qInfo("ui-responsiveness: a gap of %lld ms is taken as the computer asleep or the process suspended, not as a pause",
			static_cast<long long>(gapMilliseconds));
		return;
	}
	++m_stallCount;
	m_longestStallMs = std::max(m_longestStallMs, stall);
	m_totalStallMs += stall;
	emit stallDetected(stall);
}

// ---- Helpers -------------------------------------------------------------------

QString directionIsolated(const QString& text)
{
	if (text.isEmpty()) {
		return text;
	}
	return QChar(0x2068) + text + QChar(0x2069);
}

void paintNow(QWidget* widget)
{
	if (!widget || !widget->isVisible()) {
		return;
	}
	// Posted paints and layouts for this window settle first, so what the
	// screen gets is the widget as laid out, not its last frame.
	QCoreApplication::sendPostedEvents(widget->window(), QEvent::LayoutRequest);
	QCoreApplication::sendPostedEvents(widget->window(), QEvent::UpdateRequest);
	widget->repaint();
}

} // namespace slopstudio
