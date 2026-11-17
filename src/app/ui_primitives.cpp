#include "app/ui_primitives.h"
#include "app/studio_accessibility.h"
#include "app/studio_layout.h"
#include "app/studio_progress.h"
#include "app/studio_theme.h"
#include "app/wrapping_action_button.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyle>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>

namespace slopstudio {

namespace {

// A width-dependent header keeps ordinary panes compact and gives long
// translations their own action rows. Qt still owns each widget's focus,
// accessibility and height-for-width sizing.
class DetailHeaderLayout final : public QLayout {
public:
	DetailHeaderLayout() { setContentsMargins(0, 0, 0, 0); setSpacing(6); }
	~DetailHeaderLayout() override { while (auto* item = takeAt(0)) { delete item; } }
	void addItem(QLayoutItem* item) override { m_items.append(item); }
	int count() const override { return m_items.size(); }
	QLayoutItem* itemAt(int index) const override { return index >= 0 && index < count() ? m_items.at(index) : nullptr; }
	QLayoutItem* takeAt(int index) override { return index >= 0 && index < count() ? m_items.takeAt(index) : nullptr; }
	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int width) const override { return arrange(QRect(0, 0, qMax(1, width), 0), false); }
	QSize minimumSize() const override
	{
		int width = 0, height = 0;
		for (const auto* item : m_items) {
			if (item->isEmpty()) { continue; }
			width = qMax(width, item->minimumSize().width()); height = qMax(height, item->minimumSize().height());
		}
		return {width, height};
	}
	QSize sizeHint() const override { const int width = qMax(400, minimumSize().width()); return {width, heightForWidth(width)}; }
	void setGeometry(const QRect& rect) override { QLayout::setGeometry(rect); arrange(rect, true); }
private:
	int arrange(const QRect& rect, bool place) const
	{
		if (m_items.size() < 2) { return 0; }
		const int width = qMax(1, rect.width()), gap = spacing();
		QVector<QLayoutItem*> actions; int actionWidth = 0;
		for (int i = 2; i < m_items.size(); ++i) {
			if (m_items.at(i)->isEmpty()) { continue; }
			actions.append(m_items.at(i)); actionWidth += m_items.at(i)->sizeHint().width();
		}
		actionWidth += qMax(0, int(actions.size()) - 1) * gap;
		const bool inlineActions = actions.isEmpty() || m_items.at(0)->sizeHint().width() + gap + actionWidth <= width;
		const int titleWidth = inlineActions && !actions.isEmpty() ? qMax(1, width - actionWidth - gap) : width;
		const auto height = [](QLayoutItem* item, int available) {
			return item->hasHeightForWidth() ? qMax(item->minimumSize().height(), item->heightForWidth(available)) : item->sizeHint().height();
		};
		const auto position = [&](QLayoutItem* item, int x, int y, int w, int h) {
			if (place) { item->setGeometry(QStyle::visualRect(parentWidget()->layoutDirection(), rect, QRect(rect.x() + x, rect.y() + y, w, h))); }
		};
		const int titleHeight = height(m_items.at(0), titleWidth);
		const int subtitleHeight = height(m_items.at(1), titleWidth);
		position(m_items.at(0), 0, 0, titleWidth, titleHeight);
		position(m_items.at(1), 0, titleHeight + 1, titleWidth, subtitleHeight);
		const int textHeight = titleHeight + 1 + subtitleHeight;
		if (actions.isEmpty()) { return textHeight; }
		const bool actionRow = inlineActions || actionWidth <= width;
		int x = inlineActions ? width - actionWidth : 0, y = inlineActions ? 0 : textHeight + gap, rowHeight = 0;
		for (auto* item : actions) {
			const int w = actionRow ? item->sizeHint().width() : width, h = height(item, w);
			position(item, x, y, w, h); rowHeight = qMax(rowHeight, h);
			if (actionRow) { x += w + gap; } else { y += h + gap; }
		}
		return inlineActions ? qMax(textHeight, rowHeight) : actionRow ? y + rowHeight : y - gap;
	}
	QVector<QLayoutItem*> m_items;
};

class DetailTextView final : public QTextEdit {
public:
	using QTextEdit::QTextEdit;

protected:
	void changeEvent(QEvent* event) override
	{
		QTextEdit::changeEvent(event);
		if (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange || event->type() == QEvent::StyleChange) {
			// Restyling the parent can precede the child's stylesheet font reset.
			// Apply the fixed-pitch face and current scale to the text view itself.
			if (font() != studioMonospaceFont()) { applyMonospaceContentFont(this); }
		}
	}
};

QString normalizedSectionId(const QString& value)
{
	QString normalized = value.trimmed().toLower();
	normalized.replace('_', '-');
	normalized.replace(' ', '-');
	return normalized;
}

QString localizedStateName(OperationState state)
{
	switch (state) {
	case OperationState::Idle:
		return QCoreApplication::translate("SlopStudioUiPrimitives", "Idle");
	case OperationState::Queued:
		return QCoreApplication::translate("SlopStudioUiPrimitives", "Queued");
	case OperationState::Loading:
		return QCoreApplication::translate("SlopStudioUiPrimitives", "Loading");
	case OperationState::Running:
		return QCoreApplication::translate("SlopStudioUiPrimitives", "Running");
	case OperationState::Warning:
		return QCoreApplication::translate("SlopStudioUiPrimitives", "Warning");
	case OperationState::Failed:
		return QCoreApplication::translate("SlopStudioUiPrimitives", "Failed");
	case OperationState::Cancelled:
		return QCoreApplication::translate("SlopStudioUiPrimitives", "Cancelled");
	case OperationState::Completed:
		return QCoreApplication::translate("SlopStudioUiPrimitives", "Completed");
	}
	return QCoreApplication::translate("SlopStudioUiPrimitives", "Idle");
}

QStringList defaultPlaceholderRows()
{
	return {
		QCoreApplication::translate("SlopStudioUiPrimitives", "Summary"),
		QCoreApplication::translate("SlopStudioUiPrimitives", "Metadata"),
		QCoreApplication::translate("SlopStudioUiPrimitives", "Diagnostics"),
	};
}

bool isBusyState(OperationState state)
{
	return state == OperationState::Queued || state == OperationState::Loading || state == OperationState::Running;
}

// Same conservative glyph set the charts use, so a state reads the same in a
// chip, a chart, and a list row without depending on colour.
QString stateGlyph(OperationState state)
{
	switch (state) {
	case OperationState::Idle:
		return QString(QChar(0x00b7));
	case OperationState::Queued:
		return QString(QChar(0x00bb));
	case OperationState::Loading:
		return QString(QChar(0x25cb));
	case OperationState::Running:
		return QString(QChar(0x25b6));
	case OperationState::Warning:
		return QString(QChar(0x25b2));
	case OperationState::Failed:
		return QString(QChar(0x00d7));
	case OperationState::Cancelled:
		return QStringLiteral("||");
	case OperationState::Completed:
		return QString(QChar(0x2713));
	}
	return QString(QChar(0x00b7));
}

void repolish(QWidget* widget)
{
	if (widget && widget->style()) {
		widget->style()->unpolish(widget);
		widget->style()->polish(widget);
	}
}

} // namespace

void applyMonospaceContentFont(QTextEdit* content)
{
	if (!content) {
		return;
	}
	const QFont font = studioMonospaceFont();
	content->setFont(font);
	content->setTabStopDistance(QFontMetricsF(font).horizontalAdvance(QLatin1Char(' ')) * 4.0);
}

QFont studioMonospaceFont()
{
	static const QStringList preferred = {
		QStringLiteral("Cascadia Mono"),
		QStringLiteral("Consolas"),
		QStringLiteral("JetBrains Mono"),
		QStringLiteral("SF Mono"),
		QStringLiteral("Menlo"),
		QStringLiteral("DejaVu Sans Mono"),
		QStringLiteral("Liberation Mono"),
		QStringLiteral("Noto Sans Mono"),
	};
	QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	for (const QString& family : preferred) {
		if (QFontDatabase::hasFamily(family)) {
			font = QFont(family);
			break;
		}
	}
	font.setStyleHint(QFont::Monospace);
	font.setFixedPitch(true);
	const qreal base = QApplication::font().pointSizeF();
	if (base > 0) {
		font.setPointSizeF(base);
	}
	return font;
}

QVector<UiPrimitiveDescriptor> uiPrimitiveDescriptors()
{
	return {
		{
			QStringLiteral("loading-pane"),
			QCoreApplication::translate("SlopStudioUiPrimitives", "Loading pane"),
			QCoreApplication::translate("SlopStudioUiPrimitives", "Reusable pane and preview loading surface with state text, progress, reduced-motion behavior, and context-specific skeleton rows."),
			{
				QCoreApplication::translate("SlopStudioUiPrimitives", "package loading"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "preview generation"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "compiler stages"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "AI requests"),
			},
		},
		{
			QStringLiteral("detail-drawer"),
			QCoreApplication::translate("SlopStudioUiPrimitives", "Detail drawer"),
			QCoreApplication::translate("SlopStudioUiPrimitives", "Reusable collapsible drawer for summary-first logs, metadata, manifests, raw diagnostics, and support-copy text."),
			{
				QCoreApplication::translate("SlopStudioUiPrimitives", "task logs"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "package metadata"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "compiler manifests"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "raw diagnostics"),
			},
		},
		{
			QStringLiteral("status-chip"),
			QCoreApplication::translate("SlopStudioUiPrimitives", "Status chip"),
			QCoreApplication::translate("SlopStudioUiPrimitives", "Compact non-color-only status indicator for project, package, compiler, installation, AI, validation, setup, and localization state."),
			{
				QCoreApplication::translate("SlopStudioUiPrimitives", "project health"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "package staging"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "compiler readiness"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "AI-free mode"),
			},
		},
		{
			QStringLiteral("shortcut-registry"),
			QCoreApplication::translate("SlopStudioUiPrimitives", "Shortcut registry"),
			QCoreApplication::translate("SlopStudioUiPrimitives", "Shared keyboard shortcut metadata for command surfaces, conflict checks, accessible labels, and future user remapping."),
			{
				QCoreApplication::translate("SlopStudioUiPrimitives", "global commands"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "package commands"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "compiler commands"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "activity commands"),
			},
		},
		{
			QStringLiteral("command-palette"),
			QCoreApplication::translate("SlopStudioUiPrimitives", "Command palette shell"),
			QCoreApplication::translate("SlopStudioUiPrimitives", "Searchable command metadata shell with categories, summaries, shortcut hints, and staged/destructive flags."),
			{
				QCoreApplication::translate("SlopStudioUiPrimitives", "workspace commands"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "support commands"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "QA commands"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "AI review commands"),
			},
		},
		{
			QStringLiteral("busy-indicator"),
			QCoreApplication::translate("SlopStudioUiPrimitives", "Busy indicator"),
			QCoreApplication::translate("SlopStudioUiPrimitives", "A ring that turns while work runs and fills as measurable work progresses; it stands still under reduced motion, and one sits beside the status bar's Activity button while anything in the window is busy."),
			{
				QCoreApplication::translate("SlopStudioUiPrimitives", "status bar activity"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "start-up stages"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "dialog work"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "preview generation"),
			},
		},
		{
			QStringLiteral("busy-overlay"),
			QCoreApplication::translate("SlopStudioUiPrimitives", "Busy overlay"),
			QCoreApplication::translate("SlopStudioUiPrimitives", "A translucent cover for a surface whose content is being prepared, with a title, detail, progress and an optional Cancel; it appears after a short delay so brief work never flashes, and counts in the window's running work."),
			{
				QCoreApplication::translate("SlopStudioUiPrimitives", "session restore"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "large file opens"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "dialog preparation"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "work that holds the interface"),
			},
		},
		{
			QStringLiteral("start-up-screen"),
			QCoreApplication::translate("SlopStudioUiPrimitives", "Start-up screen"),
			QCoreApplication::translate("SlopStudioUiPrimitives", "The branded screen shown while the studio starts, naming each stage with a progress line, following the theme, text scale, layout direction and reduced-motion preference, and fading once the window has painted."),
			{
				QCoreApplication::translate("SlopStudioUiPrimitives", "application start"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "session restore"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "renderer start"),
				QCoreApplication::translate("SlopStudioUiPrimitives", "slow first runs"),
			},
		},
	};
}

LoadingPane::LoadingPane(QWidget* parent)
	: QFrame(parent)
{
	setObjectName("loadingPane");
	setAccessibleName(QCoreApplication::translate("SlopStudioUiPrimitives", "Loading pane"));
	setAccessibleDescription(QCoreApplication::translate("SlopStudioUiPrimitives", "Shows operation state, progress, and placeholder rows while pane content is loading."));

	// One slim status line: the state in words (its edge carries its colour),
	// the title, the detail beside it, and a progress bar that only appears
	// while work is queued or running. The detail wraps onto more lines only
	// when it has to. Skeleton rows appear beneath it for the same busy states.
	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(12, 6, 10, 6);
	root->setSpacing(6);

	auto* header = new QHBoxLayout;
	header->setSpacing(10);
	m_stateLabel = new QLabel;
	m_stateLabel->setObjectName("statusChip");
	m_stateLabel->setAccessibleName(QCoreApplication::translate("SlopStudioUiPrimitives", "Loading pane state"));
	m_stateLabel->setAlignment(Qt::AlignLeading | Qt::AlignVCenter);
	header->addWidget(m_stateLabel, 0, Qt::AlignTop);

	m_titleLabel = new QLabel;
	m_titleLabel->setObjectName("loadingTitle");
	m_titleLabel->setAccessibleName(QCoreApplication::translate("SlopStudioUiPrimitives", "Loading pane title"));
	m_titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	header->addWidget(m_titleLabel, 0, Qt::AlignTop);

	m_detailLabel = new QLabel;
	m_detailLabel->setObjectName("loadingDetail");
	m_detailLabel->setAccessibleName(QCoreApplication::translate("SlopStudioUiPrimitives", "Loading pane detail"));
	m_detailLabel->setWordWrap(true);
	m_detailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	header->addWidget(m_detailLabel, 1, Qt::AlignTop);

	m_progress = new QProgressBar;
	m_progress->setObjectName("loadingProgress");
	m_progress->setAccessibleName(QCoreApplication::translate("SlopStudioUiPrimitives", "Loading pane progress"));
	m_progress->setTextVisible(true);
	m_progress->setFixedWidth(190);
	header->addWidget(m_progress, 0, Qt::AlignVCenter);
	root->addLayout(header);

	m_placeholderHost = new QWidget;
	m_placeholderHost->setObjectName("skeletonHost");
	m_placeholderHost->setAccessibleName(QCoreApplication::translate("SlopStudioUiPrimitives", "Loading placeholders"));
	m_placeholderHost->setAccessibleDescription(QCoreApplication::translate("SlopStudioUiPrimitives", "Context-specific placeholder rows for loading or pending pane content."));
	m_placeholderLayout = new QVBoxLayout(m_placeholderHost);
	m_placeholderLayout->setContentsMargins(0, 0, 0, 0);
	m_placeholderLayout->setSpacing(6);
	m_placeholderLayout->addStretch(1);
	root->addWidget(m_placeholderHost);

	setTitle(QCoreApplication::translate("SlopStudioUiPrimitives", "Loading"));
	setDetail(QCoreApplication::translate("SlopStudioUiPrimitives", "Preparing content."));
	setPlaceholderRows(defaultPlaceholderRows());
	refresh();
}

LoadingPane::~LoadingPane()
{
	syncWorkTracker(false, QString());
}

void LoadingPane::syncWorkTracker(bool busy, const QString& label)
{
	WorkTracker& tracker = studioWorkTracker();
	if (busy && m_workToken == 0) {
		m_workToken = tracker.begin(label, m_progressValue);
	} else if (busy) {
		tracker.update(m_workToken, label, m_progressValue);
	} else if (m_workToken != 0) {
		tracker.end(m_workToken);
		m_workToken = 0;
	}
}

void LoadingPane::setTitle(const QString& title)
{
	m_title = title.trimmed();
	refresh();
}

void LoadingPane::setDetail(const QString& detail)
{
	m_detail = detail.trimmed();
	refresh();
}

void LoadingPane::setState(OperationState state, const QString& statusText)
{
	m_state = state;
	m_statusText = statusText.trimmed();
	refresh();
}

void LoadingPane::setProgress(OperationProgress progress)
{
	m_progressValue.total = std::max(0, progress.total);
	m_progressValue.current = std::clamp(progress.current, 0, m_progressValue.total);
	refresh();
}

void LoadingPane::setPlaceholderRows(const QStringList& rows)
{
	m_placeholderRows.clear();
	for (const QString& row : rows) {
		const QString normalized = row.trimmed();
		if (!normalized.isEmpty()) {
			m_placeholderRows.push_back(normalized);
		}
	}
	rebuildPlaceholders();
	refresh();
}

void LoadingPane::setReducedMotion(bool reducedMotion)
{
	m_reducedMotion = reducedMotion;
	refresh();
}

QString LoadingPane::title() const
{
	return m_title;
}

QString LoadingPane::detail() const
{
	return m_detail;
}

OperationState LoadingPane::state() const
{
	return m_state;
}

OperationProgress LoadingPane::progress() const
{
	return m_progressValue;
}

QStringList LoadingPane::placeholderRows() const
{
	return m_placeholderRows;
}

bool LoadingPane::reducedMotion() const
{
	return m_reducedMotion;
}

void LoadingPane::refresh()
{
	if (!m_titleLabel || !m_progress) {
		return;
	}

	const QString stateName = m_statusText.isEmpty() ? localizedStateName(m_state) : m_statusText;
	const QString titleText = m_title.isEmpty() ? QCoreApplication::translate("SlopStudioUiPrimitives", "Loading") : m_title;
	m_titleLabel->setText(titleText);
	m_stateLabel->setText(QStringLiteral("%1 %2").arg(stateGlyph(m_state), stateName));
	m_stateLabel->setToolTip(stateName);
	const QString stateId = operationStateId(m_state);
	if (m_stateLabel->property("operationState").toString() != stateId) {
		m_stateLabel->setProperty("operationState", stateId);
		repolish(m_stateLabel);
	}
	// The strip's leading edge takes the state's colour (see paintEvent()).
	if (property("operationState").toString() != stateId) {
		setProperty("operationState", stateId);
		update();
	}

	m_detailLabel->setText(m_detail.isEmpty() ? QCoreApplication::translate("SlopStudioUiPrimitives", "Preparing content.") : m_detail);

	const bool busy = isBusyState(m_state);
	syncWorkTracker(busy, titleText);
	if (m_progressValue.total > 0) {
		m_progress->setRange(0, m_progressValue.total);
		m_progress->setValue(m_progressValue.current);
		m_progress->setFormat(QCoreApplication::translate("SlopStudioUiPrimitives", "%1%").arg(operationProgressPercent(m_progressValue)));
	} else if (busy) {
		setBusyProgress(m_progress, m_reducedMotion, stateName);
	} else {
		m_progress->setRange(0, 1);
		m_progress->setValue(operationStateIsTerminal(m_state) ? 1 : 0);
		m_progress->setFormat(stateName);
	}
	// A finished or idle pane has nothing to measure, so the bar only takes
	// space while work is queued or running.
	m_progress->setVisible(busy);
	m_placeholderHost->setVisible(busy);
}

void LoadingPane::paintEvent(QPaintEvent* event)
{
	QFrame::paintEvent(event);
	const StudioThemeTokens& theme = currentStudioTheme();
	QColor edge = studioThemeStateColor(theme, operationStateId(m_state));
	if (!edge.isValid()) {
		edge = theme.colors.borderStrong;
	}
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	// Clipped to the strip's own rounded outline, so the edge follows its
	// corners on the leading side.
	QPainterPath outline;
	const qreal radius = theme.metrics.radiusSmall;
	outline.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
	painter.setClipPath(outline);
	const qreal width = 3.0;
	const bool mirrored = layoutDirection() == Qt::RightToLeft;
	painter.fillRect(QRectF(mirrored ? rect().width() - width : 0.0, 0.0, width, rect().height()), edge);
}

void LoadingPane::rebuildPlaceholders()
{
	if (!m_placeholderLayout) {
		return;
	}

	const QStringList rows = m_placeholderRows.isEmpty() ? defaultPlaceholderRows() : m_placeholderRows;
	while (m_placeholderLabels.size() < rows.size()) {
		auto* label = new QLabel(m_placeholderHost);
		label->setObjectName("skeletonRow");
		label->setMinimumHeight(24);
		m_placeholderLayout->insertWidget(m_placeholderLabels.size(), label);
		m_placeholderLabels.push_back(label);
	}

	for (int index = 0; index < m_placeholderLabels.size(); ++index) {
		QLabel* label = m_placeholderLabels[index];
		const bool active = index < rows.size();
		if (active) {
			const QString& row = rows[index];
			label->setText(QCoreApplication::translate("SlopStudioUiPrimitives", "%1 loading placeholder").arg(row));
			label->setAccessibleName(QCoreApplication::translate("SlopStudioUiPrimitives", "%1 placeholder").arg(row));
		}
		label->setVisible(active);
	}
}

DetailDrawer::DetailDrawer(QWidget* parent)
	: QFrame(parent)
{
	setObjectName("detailDrawer");
	setAccessibleName(QCoreApplication::translate("SlopStudioUiPrimitives", "Detail drawer"));
	setAccessibleDescription(QCoreApplication::translate("SlopStudioUiPrimitives", "Collapsible details for logs, metadata, manifests, and raw diagnostics."));

	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(12, 10, 12, 10);
	root->setSpacing(8);

	auto* header = new DetailHeaderLayout;
	m_titleLabel = new QLabel;
	m_titleLabel->setObjectName("drawerTitle");
	m_titleLabel->setAccessibleName(QCoreApplication::translate("SlopStudioUiPrimitives", "Detail drawer title"));
	m_titleLabel->setWordWrap(true);
	m_titleLabel->setTextFormat(Qt::PlainText);
	m_subtitleLabel = new ElidedLabel;
	m_subtitleLabel->setObjectName("drawerSubtitle");
	m_subtitleLabel->setAccessibleName(QCoreApplication::translate("SlopStudioUiPrimitives", "Detail drawer subtitle"));
	m_subtitleLabel->setTextFormat(Qt::PlainText);
	m_subtitleLabel->setElideMode(Qt::ElideMiddle);
	header->addWidget(m_titleLabel);
	header->addWidget(m_subtitleLabel);

	m_copyButton = new WrappingActionButton(QCoreApplication::translate("SlopStudioUiPrimitives", "Copy"), 0);
	m_copyButton->setAccessibleName(QCoreApplication::translate("SlopStudioUiPrimitives", "Copy selected detail text"));
	m_copyButton->setToolTip(QCoreApplication::translate("SlopStudioUiPrimitives", "Copy the selected section to the clipboard."));
	m_copyButton->setProperty("variant", QStringLiteral("ghost"));
	connect(m_copyButton, &QPushButton::clicked, this, [this]() {
		copyCurrentSection();
	});
	header->addWidget(m_copyButton);

	m_toggleButton = new WrappingActionButton(QCoreApplication::translate("SlopStudioUiPrimitives", "Hide Details"), 0);
	m_toggleButton->setAccessibleName(QCoreApplication::translate("SlopStudioUiPrimitives", "Show or hide detail drawer"));
	m_toggleButton->setProperty("variant", QStringLiteral("ghost"));
	connect(m_toggleButton, &QPushButton::clicked, this, [this]() {
		setExpanded(!m_expanded);
	});
	header->addWidget(m_toggleButton);
	root->addLayout(header);

	m_body = new QWidget;
	auto* bodyLayout = new QVBoxLayout(m_body);
	bodyLayout->setContentsMargins(0, 0, 0, 0);
	bodyLayout->setSpacing(8);

	m_emptyLabel = new QLabel(QCoreApplication::translate("SlopStudioUiPrimitives", "No details yet."));
	m_emptyLabel->setObjectName("drawerEmpty");
	m_emptyLabel->setAccessibleName(QCoreApplication::translate("SlopStudioUiPrimitives", "Detail drawer empty state"));
	m_emptyLabel->setWordWrap(true);
	m_emptyLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	bodyLayout->addWidget(m_emptyLabel);

	m_sectionsList = new QListWidget;
	m_sectionsList->setObjectName("detailSections");
	m_sectionsList->setAccessibleName(QCoreApplication::translate("SlopStudioUiPrimitives", "Detail sections"));
	m_sectionsList->setAccessibleDescription(QCoreApplication::translate("SlopStudioUiPrimitives", "Available summary, log, metadata, manifest, and raw diagnostic sections."));
	m_sectionsList->setMaximumHeight(132);
	m_sectionsList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_sectionsList->setTextElideMode(Qt::ElideRight);
	connect(m_sectionsList, &QListWidget::itemSelectionChanged, this, [this]() {
		refreshCurrentSection();
	});
	bodyLayout->addWidget(m_sectionsList);

	m_content = new DetailTextView;
	m_content->setObjectName("detailContent");
	m_content->setAccessibleName(QCoreApplication::translate("SlopStudioUiPrimitives", "Detail content"));
	m_content->setAccessibleDescription(QCoreApplication::translate("SlopStudioUiPrimitives", "Selected detail content ready for copying into support notes or diagnostics."));
	m_content->setReadOnly(true);
	m_content->setLineWrapMode(QTextEdit::WidgetWidth);
	// Logs, manifests, and command lines line up in a fixed-pitch font, with
	// tabs four columns wide the way source files are usually written.
	applyMonospaceContentFont(m_content);
	bodyLayout->addWidget(m_content, 1);

	root->addWidget(m_body, 1);

	setTitle(QCoreApplication::translate("SlopStudioUiPrimitives", "Details"));
	setSubtitle(QCoreApplication::translate("SlopStudioUiPrimitives", "Select a section for deeper context."));
	setSections({});
}

void DetailDrawer::changeEvent(QEvent* event)
{
	// An explicitly set font does not follow QApplication::setFont(), so the
	// fixed-pitch content re-derives its size when the text scale changes.
	if ((event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange) && m_content) {
		applyMonospaceContentFont(m_content);
	}
	QFrame::changeEvent(event);
}

void DetailDrawer::setEmbedded(bool embedded)
{
	setProperty("embedded", embedded);
	// Inside a tab the drawer is the whole panel, so hiding it would leave an
	// empty tab; only the copy action stays in its header.
	if (m_toggleButton) {
		m_toggleButton->setVisible(!embedded);
	}
	if (layout()) {
		layout()->setContentsMargins(embedded ? 0 : 12, embedded ? 4 : 10, embedded ? 0 : 12, embedded ? 0 : 10);
	}
	repolish(this);
}

void DetailDrawer::setTitle(const QString& title)
{
	m_title = title.trimmed();
	refreshHeader();
}

void DetailDrawer::setSubtitle(const QString& subtitle)
{
	m_subtitle = subtitle.trimmed();
	refreshHeader();
}

void DetailDrawer::setSections(const QVector<DetailSection>& sections)
{
	m_sections.clear();
	for (DetailSection section : sections) {
		section.id = normalizedSectionId(section.id.isEmpty() ? section.title : section.id);
		section.title = section.title.trimmed();
		section.summary = section.summary.trimmed();
		section.content = section.content.trimmed();
		if (!section.id.isEmpty() && !section.title.isEmpty()) {
			m_sections.push_back(section);
		}
	}
	if (!findSection(m_currentSectionId)) {
		m_currentSectionId = m_sections.isEmpty() ? QString() : m_sections.front().id;
	}
	refreshSections();
	refreshCurrentSection();
}

void DetailDrawer::showSection(const QString& sectionId)
{
	const QString normalized = normalizedSectionId(sectionId);
	if (!findSection(normalized)) {
		return;
	}
	if (m_currentSectionId == normalized) { return; }
	m_currentSectionId = normalized;
	// Navigation changes the current row, not the section catalogue. Keep
	// persistent indexes, list scroll position and accessibility rows intact.
	const QSignalBlocker blocker(m_sectionsList);
	for (int row = 0; row < m_sectionsList->count(); ++row) {
		if (m_sectionsList->item(row)->data(Qt::UserRole).toString() == normalized) {
			m_sectionsList->setCurrentRow(row); break;
		}
	}
	refreshCurrentSection();
}

void DetailDrawer::setExpanded(bool expanded)
{
	m_expanded = expanded;
	if (m_body) {
		m_body->setVisible(m_expanded);
	}
	if (m_toggleButton) {
		m_toggleButton->setText(m_expanded ? QCoreApplication::translate("SlopStudioUiPrimitives", "Hide Details") : QCoreApplication::translate("SlopStudioUiPrimitives", "Show Details"));
	}
}

QString DetailDrawer::title() const
{
	return m_title;
}

QString DetailDrawer::subtitle() const
{
	return m_subtitle;
}

QVector<DetailSection> DetailDrawer::sections() const
{
	return m_sections;
}

QString DetailDrawer::currentSectionId() const
{
	return m_currentSectionId;
}

QString DetailDrawer::currentSectionText() const
{
	const DetailSection* section = findSection(m_currentSectionId);
	return section ? section->content : QString();
}

bool DetailDrawer::isExpanded() const
{
	return m_expanded;
}

void DetailDrawer::refreshHeader()
{
	if (!m_titleLabel) {
		return;
	}
	m_titleLabel->setText(m_title.isEmpty() ? QCoreApplication::translate("SlopStudioUiPrimitives", "Details") : m_title);
	m_subtitleLabel->setText(m_subtitle.isEmpty() ? QCoreApplication::translate("SlopStudioUiPrimitives", "Select a section for deeper context.") : m_subtitle);
}

void DetailDrawer::refreshSections()
{
	if (!m_sectionsList) {
		return;
	}

	QSignalBlocker blocker(m_sectionsList);
	bool sameRows = m_sectionsList->count() == m_sections.size();
	for (int row = 0; sameRows && row < m_sections.size(); ++row) {
		sameRows = m_sectionsList->item(row)->data(Qt::UserRole).toString() == m_sections.at(row).id;
	}
	if (!sameRows) { m_sectionsList->clear(); }
	const bool empty = m_sections.isEmpty();
	// With nothing to show, one quiet line replaces the section list and the
	// content box instead of two empty frames.
	if (m_emptyLabel) {
		m_emptyLabel->setVisible(empty);
	}
	m_content->setVisible(!empty);
	if (empty) {
		m_sectionsList->setVisible(false);
		m_copyButton->setEnabled(false);
		return;
	}
	// A single section needs no chooser.
	m_sectionsList->setVisible(m_sections.size() > 1);

	for (int row = 0; row < m_sections.size(); ++row) {
		const auto& section = m_sections.at(row);
		const QString label = section.summary.isEmpty()
			? QStringLiteral("%1 %2").arg(stateGlyph(section.state), section.title)
			: QStringLiteral("%1 %2  %3  %4").arg(stateGlyph(section.state), section.title, QString(QChar(0x2014)), section.summary);
		auto* item = sameRows ? m_sectionsList->item(row) : new QListWidgetItem;
		item->setText(label);
		item->setToolTip(QStringLiteral("%1 (%2)\n%3").arg(section.title, localizedStateName(section.state), section.summary));
		item->setData(Qt::AccessibleTextRole, QStringLiteral("%1, %2. %3").arg(section.title, localizedStateName(section.state), section.summary));
		item->setData(Qt::UserRole, section.id);
		item->setData(Qt::UserRole + 1, operationStateId(section.state));
		if (!sameRows) { m_sectionsList->addItem(item); }
		if (section.id == m_currentSectionId) {
			m_sectionsList->setCurrentItem(item);
		}
	}
	if (!m_sectionsList->currentItem()) {
		m_sectionsList->setCurrentRow(0);
	}
	m_copyButton->setEnabled(true);
}

void DetailDrawer::refreshCurrentSection()
{
	if (!m_content) {
		return;
	}

	const QListWidgetItem* item = m_sectionsList ? m_sectionsList->currentItem() : nullptr;
	if (item && !item->data(Qt::UserRole).toString().isEmpty()) {
		m_currentSectionId = item->data(Qt::UserRole).toString();
	}

	const DetailSection* section = findSection(m_currentSectionId);
	const QString text = !section || section->content.isEmpty()
		? QCoreApplication::translate("SlopStudioUiPrimitives", "No details available.") : section->content;
	// A repeated context refresh must not erase a reader's text selection or
	// scroll position, or relayout a large unchanged log/manifest.
	if (m_displayedSectionId == m_currentSectionId && m_displayedContent == text) { return; }
	// A log that grew (a compiler streaming its output) gains only its new
	// lines: replacing the whole text would lay all of it out again for each
	// one, drop the reader's selection and send the view back to the top. A
	// reader at the end of the log stays at the end; one who scrolled up to
	// read stays where they are.
	const bool grew = m_displayedSectionId == m_currentSectionId && !m_displayedContent.isEmpty()
		&& text.size() > m_displayedContent.size() && text.startsWith(m_displayedContent);
	if (grew) {
		QScrollBar* bar = m_content->verticalScrollBar();
		const int position = bar ? bar->value() : 0;
		const bool atEnd = bar && position >= bar->maximum();
		QTextCursor cursor(m_content->document());
		cursor.movePosition(QTextCursor::End);
		cursor.insertText(text.mid(m_displayedContent.size()));
		if (bar) { bar->setValue(atEnd ? bar->maximum() : position); }
		m_displayedContent = text;
		return;
	}
	m_displayedSectionId = m_currentSectionId;
	m_displayedContent = text;
	m_content->setPlainText(text);
}

void DetailDrawer::copyCurrentSection()
{
	const QString text = currentSectionText();
	if (text.isEmpty()) {
		return;
	}
	if (QClipboard* clipboard = QGuiApplication::clipboard()) {
		clipboard->setText(text);
	}
}

const DetailSection* DetailDrawer::findSection(const QString& sectionId) const
{
	const QString normalized = normalizedSectionId(sectionId);
	const auto match = std::find_if(m_sections.cbegin(), m_sections.cend(), [&normalized](const DetailSection& section) {
		return section.id == normalized;
	});
	return match == m_sections.cend() ? nullptr : &(*match);
}

} // namespace slopstudio
