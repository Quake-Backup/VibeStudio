#include "app/studio_splash.h"
#include "app/studio_accessibility.h"
#include "app/studio_layout.h"
#include "app/studio_progress.h"
#include "app/studio_runtime.h"
#include "app/studio_theme.h"
#include "core/studio_manifest.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLocale>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QStyle>
#include <QThread>
#include <QTimer>
#include <QVariantAnimation>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace vibestudio {

namespace {

// The brand typeface for the wordmark alone (docs/BRANDING.md); everything
// else on the screen is set in the interface font, as the studio is.
QString loadWordmarkFamily()
{
	static const QString family = []() {
		const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/branding/Manrope-ExtraBold.ttf"));
		if (id < 0) {
			return QString();
		}
		const QStringList families = QFontDatabase::applicationFontFamilies(id);
		return families.isEmpty() ? QString() : families.first();
	}();
	return family;
}

} // namespace

StudioSplash::StudioSplash(QWidget* parent)
	: QWidget(parent, Qt::SplashScreen | Qt::FramelessWindowHint)
{
	setObjectName(QStringLiteral("studioSplash"));
	setAttribute(Qt::WA_DeleteOnClose, false);
	setFocusPolicy(Qt::NoFocus);
	m_shadowMargin = prepareFloatingPanel(this);
	m_reducedMotion = studioReducedMotion();
	m_wordmarkFamily = loadWordmarkFamily();
	m_versionText = tr("Version %1 · Pre-alpha preview").arg(directionIsolated(versionString()));
	m_indicator = new BusyIndicator(this);
	m_indicator->setReducedMotion(m_reducedMotion);
	m_indicator->setDiameter(std::max(14, fontMetrics().height()));
	fitToContent();
	refreshAccessibleText();
}

StudioSplash::~StudioSplash()
{
	if (m_watchedWindow) {
		m_watchedWindow->removeEventFilter(this);
	}
}

bool StudioSplash::platformShowsSplash()
{
	const QString platform = QGuiApplication::platformName();
	return !platform.isEmpty() && platform != QLatin1String("offscreen") && platform != QLatin1String("minimal");
}

void StudioSplash::setExpectedStages(int count)
{
	m_expectedStages = std::max(1, count);
	update();
}

int StudioSplash::expectedStages() const
{
	return m_expectedStages;
}

void StudioSplash::setStage(const QString& label, int completed)
{
	m_stageLabel = label.trimmed();
	m_completedStages = std::clamp(completed, 0, m_expectedStages);
	refreshAccessibleText();
	update();
	paintNow(this);
}

QString StudioSplash::stageLabel() const
{
	return m_stageLabel;
}

int StudioSplash::completedStages() const
{
	return m_completedStages;
}

qreal StudioSplash::progressFraction() const
{
	return m_expectedStages > 0 ? static_cast<qreal>(m_completedStages) / m_expectedStages : 0.0;
}

void StudioSplash::listenForStartupStages()
{
	setStartupStageListener([this](const QString& label, int completed) {
		setStage(label, completed);
	});
}

void StudioSplash::setReducedMotion(bool reducedMotion)
{
	m_reducedMotion = reducedMotion;
	m_indicator->setReducedMotion(reducedMotion);
}

bool StudioSplash::reducedMotion() const
{
	return m_reducedMotion;
}

void StudioSplash::setVersionText(const QString& text)
{
	m_versionText = text;
	fitToContent();
	update();
}

QString StudioSplash::versionText() const
{
	return m_versionText;
}

void StudioSplash::present()
{
	fitToContent();
	if (QScreen* screen = QGuiApplication::primaryScreen()) {
		const QRect available = screen->availableGeometry();
		move(available.center() - QPoint(width() / 2, height() / 2));
	}
	show();
	// The first frame needs the window system's expose to arrive; only this
	// window exists yet, so there is nothing else for these events to reach.
	for (int attempt = 0; attempt < 20; ++attempt) {
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
		if (windowHandle() && windowHandle()->isExposed()) {
			break;
		}
		QThread::msleep(5);
	}
	paintNow(this);
}

void StudioSplash::finish(QWidget* window)
{
	if (m_finishing) {
		return;
	}
	m_finishing = true;
	m_window = window;
	setStartupStageListener({});
	setStage(tr("Ready"), m_expectedStages);
	QWindow* handle = window ? window->windowHandle() : nullptr;
	if (!handle || handle->isExposed()) {
		// Shown already, or never a window: go after the next pass of the loop
		// so its first frame is on screen.
		QTimer::singleShot(0, this, &StudioSplash::beginFade);
		return;
	}
	m_watchedWindow = handle;
	handle->installEventFilter(this);
	// Should the window never come, the splash does not stay for ever.
	QTimer::singleShot(10000, this, &StudioSplash::beginFade);
}

bool StudioSplash::isFinishing() const
{
	return m_finishing;
}

BusyIndicator* StudioSplash::indicator() const
{
	return m_indicator;
}

void StudioSplash::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event);
	const StudioThemeTokens& theme = currentStudioTheme();
	const StudioThemeColors& c = theme.colors;
	if (m_shadowMargin > 0) {
		paintFloatingPanel(this, m_shadowMargin);
	}
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setLayoutDirection(layoutDirection());
	if (m_shadowMargin <= 0) {
		painter.setPen(QPen(theme.highContrast ? c.border : c.borderStrong, theme.highContrast ? 2.0 : 1.0));
		painter.setBrush(c.panel);
		painter.drawRect(rect().adjusted(0, 0, -1, -1));
	}
	const Metrics m = metrics();
	const QRect content = contentRect();
	const bool mirrored = layoutDirection() == Qt::RightToLeft;
	const auto visual = [&](const QRect& logical) { return QStyle::visualRect(layoutDirection(), content, logical); };

	// The mark, then the wordmark and its version line beside it.
	const QRect markRect = visual(QRect(content.left(), content.top(), m.mark, m.mark));
	if (!m_mark.isNull()) {
		painter.drawPixmap(markRect, m_mark);
	}
	const int textLeft = content.left() + m.mark + m.gap;
	const int textWidth = content.width() - m.mark - m.gap;
	QFont heavy = wordmarkFont();
	painter.setFont(heavy);
	const QFontMetrics heavyMetrics(heavy);
	const QRect wordmarkRect = visual(QRect(textLeft, content.top() + 2, textWidth, heavyMetrics.height()));
	painter.setPen(c.text);
	painter.drawText(wordmarkRect, Qt::AlignLeading | Qt::AlignVCenter | Qt::TextSingleLine, QStringLiteral("VibeStudio"));
	QFont body = font();
	painter.setFont(body);
	const QFontMetrics bodyMetrics(body);
	const QRect versionRect = visual(QRect(textLeft, wordmarkRect.bottom() + 4, textWidth, bodyMetrics.height()));
	painter.setPen(c.textMuted);
	painter.drawText(versionRect, Qt::AlignLeading | Qt::AlignVCenter | Qt::TextSingleLine, bodyMetrics.elidedText(m_versionText, Qt::ElideRight, textWidth));

	// The stage line: the ring, the stage's name, and how far along start-up is.
	const int ring = m_indicator->width();
	// One isolate round the whole phrase: isolating each number alone lets a
	// right-to-left line reorder an untranslated "2 of 4" into "4 of 2".
	const QString count = directionIsolated(tr("%1 of %2").arg(QLocale().toString(m_completedStages), QLocale().toString(m_expectedStages)));
	const int countWidth = bodyMetrics.horizontalAdvance(count);
	const int stageTop = content.bottom() - m.track - 10 - bodyMetrics.height();
	const int stageLeft = content.left() + ring + 8;
	const int stageWidth = std::max(20, content.width() - ring - 8 - countWidth - 12);
	const QRect stageRect = visual(QRect(stageLeft, stageTop, stageWidth, bodyMetrics.height()));
	painter.setPen(c.text);
	painter.drawText(stageRect, Qt::AlignLeading | Qt::AlignVCenter | Qt::TextSingleLine, bodyMetrics.elidedText(m_stageLabel, Qt::ElideRight, stageWidth));
	const QRect countRect = visual(QRect(content.right() - countWidth, stageTop, countWidth, bodyMetrics.height()));
	painter.setPen(c.textMuted);
	painter.drawText(countRect, Qt::AlignTrailing | Qt::AlignVCenter | Qt::TextSingleLine, count);

	// The progress track fills from the leading edge.
	const QRectF track(content.left(), content.bottom() - m.track, content.width(), m.track);
	painter.setPen(Qt::NoPen);
	painter.setBrush(theme.highContrast ? c.border : c.borderSubtle);
	painter.drawRoundedRect(track, m.track / 2.0, m.track / 2.0);
	const qreal filled = std::round(track.width() * progressFraction());
	if (filled > 0) {
		const QRectF fill(mirrored ? track.right() - filled : track.left(), track.top(), filled, track.height());
		painter.setBrush(c.selection);
		painter.drawRoundedRect(fill, m.track / 2.0, m.track / 2.0);
	}
}

void StudioSplash::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	placeIndicator();
}

void StudioSplash::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	placeIndicator();
	if (!m_announced && studioAccessibilityPreferences().screenReaderAnnouncements) {
		m_announced = true;
		announceToScreenReader(this, tr("VibeStudio is starting."));
	}
}

void StudioSplash::keyPressEvent(QKeyEvent* event)
{
	// Nothing to do here yet; the keys are the studio's once it shows.
	event->accept();
}

void StudioSplash::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange || event->type() == QEvent::LayoutDirectionChange) {
		fitToContent();
		placeIndicator();
	}
}

bool StudioSplash::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == m_watchedWindow && event->type() == QEvent::Expose) {
		auto* handle = qobject_cast<QWindow*>(watched);
		if (handle && handle->isExposed()) {
			handle->removeEventFilter(this);
			m_watchedWindow = nullptr;
			// One more pass, so the window's first frame has reached the screen
			// before this one goes.
			QTimer::singleShot(0, this, &StudioSplash::beginFade);
		}
	}
	return QWidget::eventFilter(watched, event);
}

void StudioSplash::fitToContent()
{
	const Metrics m = metrics();
	const QFontMetrics bodyMetrics(font());
	const QFontMetrics heavyMetrics(wordmarkFont());
	// Wide enough for the wordmark line and a sentence of stage text at this
	// text scale; the height follows from the rows.
	const int width = std::clamp(bodyMetrics.averageCharWidth() * 62, 440, 980) + 2 * m_shadowMargin;
	const int header = std::max(m.mark, heavyMetrics.height() + 4 + bodyMetrics.height() + 2);
	const int height = 2 * m.padding + header + 26 + bodyMetrics.height() + 10 + m.track + 2 * m_shadowMargin;
	setFixedSize(width, height);
	const qreal ratio = devicePixelRatioF() > 0 ? devicePixelRatioF() : 1.0;
	m_mark = studioApplicationIcon().pixmap(QSize(m.mark, m.mark), ratio);
	m_indicator->setDiameter(std::max(14, bodyMetrics.height()));
}

void StudioSplash::placeIndicator()
{
	const Metrics m = metrics();
	const QRect content = contentRect();
	const QFontMetrics bodyMetrics(font());
	const int ring = m_indicator->diameter();
	const int stageTop = content.bottom() - m.track - 10 - bodyMetrics.height();
	const QRect logical(content.left(), stageTop + (bodyMetrics.height() - ring) / 2, ring, ring);
	m_indicator->setGeometry(QStyle::visualRect(layoutDirection(), content, logical));
	m_indicator->show();
}

void StudioSplash::beginFade()
{
	if (m_fade || !m_finishing) {
		return;
	}
	if (m_reducedMotion || !isVisible()) {
		hide();
		deleteLater();
		return;
	}
	m_fade = new QVariantAnimation(this);
	m_fade->setDuration(kFadeDurationMs);
	m_fade->setStartValue(1.0);
	m_fade->setEndValue(0.0);
	connect(m_fade, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
		setWindowOpacity(value.toReal());
	});
	connect(m_fade, &QVariantAnimation::finished, this, [this]() {
		hide();
		deleteLater();
	});
	m_fade->start();
}

void StudioSplash::refreshAccessibleText()
{
	setAccessibleName(tr("VibeStudio is starting"));
	setAccessibleDescription(m_stageLabel.isEmpty() ? m_versionText : tr("%1 (%2 of %3)").arg(m_stageLabel).arg(m_completedStages).arg(m_expectedStages));
}

StudioSplash::Metrics StudioSplash::metrics() const
{
	Metrics m;
	const int lineHeight = fontMetrics().height();
	m.padding = std::max(20, lineHeight * 3 / 2);
	m.mark = std::clamp(lineHeight * 4, 56, 112);
	m.gap = std::max(12, lineHeight);
	return m;
}

QRect StudioSplash::contentRect() const
{
	const Metrics m = metrics();
	return rect().adjusted(m_shadowMargin + m.padding, m_shadowMargin + m.padding, -m_shadowMargin - m.padding, -m_shadowMargin - m.padding);
}

QFont StudioSplash::wordmarkFont() const
{
	QFont heavy = font();
	if (!m_wordmarkFamily.isEmpty()) {
		heavy.setFamily(m_wordmarkFamily);
	}
	heavy.setWeight(QFont::ExtraBold);
	heavy.setPointSizeF(std::max(14.0, font().pointSizeF() * 2.2));
	return heavy;
}

} // namespace vibestudio
