#include "app/material_property_panel.h"

#include "core/material_script.h"

#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <functional>
#include <utility>

namespace vibestudio {

namespace {

// A number field: Up and Down step it (Shift ten times, Ctrl a tenth). The
// steps are sent when the key is let go, or a moment after the last step
// while it is held, so the preview follows without an edit per repeat.
class NumberField final : public QLineEdit {
public:
	std::function<void()> stepped;
	std::function<void()> stepFinished;

	using QLineEdit::QLineEdit;

protected:
	void keyPressEvent(QKeyEvent* event) override
	{
		if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
			bool ok = false;
			const double value = text().trimmed().toDouble(&ok);
			if (ok && !isReadOnly()) {
				double step = 0.1;
				if (event->modifiers() & Qt::ShiftModifier) {
					step = 1.0;
				} else if (event->modifiers() & Qt::ControlModifier) {
					step = 0.01;
				}
				const double next = value + (event->key() == Qt::Key_Up ? step : -step);
				// Round away float noise: 0.30000000000000004 reads as 0.3.
				setText(QString::number(std::round(next * 10000.0) / 10000.0, 'g', 10));
				if (stepped) {
					stepped();
				}
				event->accept();
				return;
			}
		}
		QLineEdit::keyPressEvent(event);
	}

	void keyReleaseEvent(QKeyEvent* event) override
	{
		if ((event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) && !event->isAutoRepeat() && stepFinished) {
			stepFinished();
			event->accept();
			return;
		}
		QLineEdit::keyReleaseEvent(event);
	}
};

} // namespace

MaterialPropertyPanel::MaterialPropertyPanel(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("materialProperties"));
	setAccessibleName(tr("Node properties"));
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(6);
	m_title = new QLabel(this);
	m_title->setObjectName(QStringLiteral("materialPropertiesTitle"));
	QFont titleFont = m_title->font();
	titleFont.setBold(true);
	m_title->setFont(titleFont);
	m_title->setWordWrap(true);
	m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_subtitle = new QLabel(this);
	m_subtitle->setWordWrap(true);
	m_subtitle->setForegroundRole(QPalette::PlaceholderText);
	m_subtitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_empty = new QLabel(tr("Select a node to edit its properties."), this);
	m_empty->setWordWrap(true);
	m_empty->setAlignment(Qt::AlignCenter);
	m_empty->setForegroundRole(QPalette::PlaceholderText);
	m_scroll = new QScrollArea(this);
	m_scroll->setWidgetResizable(true);
	m_scroll->setFrameShape(QFrame::NoFrame);
	m_scroll->setAccessibleName(tr("Node properties"));
	m_stepTimer = new QTimer(this);
	m_stepTimer->setSingleShot(true);
	m_stepTimer->setInterval(kStepDelayMs);
	connect(m_stepTimer, &QTimer::timeout, this, &MaterialPropertyPanel::flushPendingStep);
	layout->addWidget(m_title);
	layout->addWidget(m_subtitle);
	layout->addWidget(m_empty, 1);
	layout->addWidget(m_scroll, 1);
	rebuild();
}

void MaterialPropertyPanel::setImageChoices(const QStringList& images)
{
	m_images = images;
}

QString MaterialPropertyPanel::signature(const MaterialGraphNode& node) const
{
	QStringList parts {node.id};
	for (const MaterialGraphProperty& property : node.properties) {
		parts << property.id + QLatin1Char(':') + materialGraphPropertyTypeId(property.type) + QLatin1Char(':')
				+ property.options.join(QLatin1Char('|'));
	}
	return parts.join(QLatin1Char(';'));
}

void MaterialPropertyPanel::setNode(const MaterialGraphNode* node, bool readOnly)
{
	// A step still waiting belongs to the node shown now.
	if (!node || !m_hasNode || node->id != m_node.id) {
		flushPendingStep();
	}
	if (!node) {
		m_hasNode = false;
		m_node = MaterialGraphNode();
		m_readOnly = readOnly;
		rebuild();
		return;
	}
	const bool sameShape = m_hasNode && readOnly == m_readOnly && signature(*node) == signature(m_node);
	m_node = *node;
	m_hasNode = true;
	m_readOnly = readOnly;
	if (sameShape) {
		const QString title = materialGraphNodeLabel(m_node);
		m_title->setText(title);
		m_title->setAccessibleName(title);
		m_subtitle->setText(m_node.subtitle);
		m_subtitle->setVisible(!m_node.subtitle.isEmpty());
		refreshValues();
	} else {
		rebuild();
	}
}

QString MaterialPropertyPanel::focusedProperty() const
{
	QWidget* focus = QApplication::focusWidget();
	if (!focus && window()) {
		focus = window()->focusWidget();
	}
	if (!focus || !m_formHost || !m_formHost->isAncestorOf(focus)) {
		return QString();
	}
	for (auto it = m_editors.cbegin(); it != m_editors.cend(); ++it) {
		QWidget* editor = it.value();
		if (editor && (editor == focus || editor->isAncestorOf(focus))) {
			return it.key();
		}
	}
	return QString();
}

void MaterialPropertyPanel::rebuild()
{
	// Focus goes back to the same property in the new controls.
	const QString focused = focusedProperty();
	const bool hadFocus = !focused.isEmpty() || (m_formHost && m_formHost->isAncestorOf(QApplication::focusWidget()));
	m_editors.clear();
	m_problems.clear();
	// The old controls may be the ones whose signal brought us here: they are
	// taken out now and deleted later, never under their own feet. The new
	// host is in place first, so anything the old ones send on the way out
	// is ignored (commit checks where the sender lives).
	QWidget* previous = m_formHost;
	m_formHost = new QWidget;
	m_formHost->setObjectName(QStringLiteral("materialPropertiesForm"));
	m_form = new QFormLayout(m_formHost);
	m_form->setContentsMargins(0, 0, 0, 0);
	m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	if (previous) {
		if (m_scroll->widget() == previous) {
			m_scroll->takeWidget();
		}
		previous->hide();
		previous->deleteLater();
	}
	m_title->setVisible(m_hasNode);
	m_subtitle->setVisible(m_hasNode && !m_node.subtitle.isEmpty());
	m_empty->setVisible(!m_hasNode);
	m_scroll->setVisible(m_hasNode);
	if (m_hasNode) {
		const QString title = materialGraphNodeLabel(m_node);
		m_title->setText(title);
		m_title->setAccessibleName(title);
		m_subtitle->setText(m_node.subtitle);
		if (m_node.properties.isEmpty()) {
			auto* none = new QLabel(tr("This node has nothing to set."), m_formHost);
			none->setWordWrap(true);
			m_form->addRow(none);
		}
		for (const MaterialGraphProperty& property : m_node.properties) {
			QWidget* editor = createEditor(property);
			auto* label = new QLabel(property.label, m_formHost);
			label->setBuddy(editor);
			label->setToolTip(property.help);
			auto* problem = new QLabel(m_formHost);
			problem->setWordWrap(true);
			problem->setVisible(false);
			problem->setForegroundRole(QPalette::BrightText);
			auto* field = new QWidget(m_formHost);
			auto* fieldLayout = new QVBoxLayout(field);
			fieldLayout->setContentsMargins(0, 0, 0, 0);
			fieldLayout->setSpacing(2);
			fieldLayout->addWidget(editor);
			fieldLayout->addWidget(problem);
			m_form->addRow(label, field);
			m_editors.insert(property.id, editor);
			m_problems.insert(property.id, problem);
		}
	}
	m_scroll->setWidget(m_formHost);
	if (!m_hasNode || !hadFocus) {
		return;
	}
	if (QWidget* editor = m_editors.value(focused); editor && editor->isEnabled()) {
		editor->setFocus(Qt::OtherFocusReason);
	} else {
		focusFirstField();
	}
}

bool MaterialPropertyPanel::eventFilter(QObject* watched, QEvent* event)
{
	// Scrolling the panel over a closed choice or a spin box scrolls; only a
	// control the user has focused takes the wheel.
	if (event->type() == QEvent::Wheel) {
		auto* widget = qobject_cast<QWidget*>(watched);
		if (widget && !widget->hasFocus()) {
			event->ignore();
			return true;
		}
	}
	return QWidget::eventFilter(watched, event);
}

QString MaterialPropertyPanel::valueText(const MaterialGraphProperty& property, const QString& value) const
{
	const int option = static_cast<int>(property.options.indexOf(value));
	if (option >= 0 && option < property.optionLabels.size()) {
		return property.optionLabels.at(option);
	}
	if (property.type == MaterialGraphPropertyType::Bool) {
		return value == QStringLiteral("true") ? tr("on") : tr("off");
	}
	return value;
}

QWidget* MaterialPropertyPanel::createEditor(const MaterialGraphProperty& property)
{
	const QString id = property.id;
	// Paths, numbers, vectors and expressions read left to right in every
	// language; a Doom 3 description is ordinary text.
	const bool technical = id != QStringLiteral("description");
	QWidget* editor = nullptr;
	switch (property.type) {
	case MaterialGraphPropertyType::Bool: {
		auto* box = new QCheckBox(m_formHost);
		box->setChecked(property.value == QStringLiteral("true"));
		box->setEnabled(!m_readOnly);
		connect(box, &QCheckBox::toggled, this, [this, id, box](bool checked) {
			commit(box, id, checked ? QStringLiteral("true") : QStringLiteral("false"));
		});
		editor = box;
		break;
	}
	case MaterialGraphPropertyType::Enum: {
		auto* combo = new QComboBox(m_formHost);
		for (int option = 0; option < property.options.size(); ++option) {
			combo->addItem(property.optionLabels.value(option, property.options.at(option)), property.options.at(option));
		}
		if (!property.value.isEmpty() && combo->findData(property.value) < 0) {
			combo->addItem(property.value, property.value);
		}
		combo->setCurrentIndex(combo->findData(property.value));
		combo->setEnabled(!m_readOnly);
		combo->setFocusPolicy(Qt::StrongFocus);
		combo->installEventFilter(this);
		connect(combo, &QComboBox::activated, this, [this, id, combo](int) { commit(combo, id, combo->currentData().toString()); });
		editor = combo;
		break;
	}
	case MaterialGraphPropertyType::Integer: {
		auto* spin = new QSpinBox(m_formHost);
		spin->setRange(-1000000, 1000000);
		spin->setKeyboardTracking(false);
		spin->setValue(property.value.toInt());
		spin->setEnabled(!m_readOnly);
		spin->setFocusPolicy(Qt::StrongFocus);
		spin->setLayoutDirection(Qt::LeftToRight);
		spin->installEventFilter(this);
		// Arrow and wheel steps are sent once the stepping stops; Enter or
		// leaving the field sends at once.
		connect(spin, &QSpinBox::valueChanged, this, [this, id]() { scheduleStep(id); });
		connect(spin, &QSpinBox::editingFinished, this, [this, id]() {
			if (m_stepProperty == id) {
				flushPendingStep();
			}
		});
		editor = spin;
		break;
	}
	case MaterialGraphPropertyType::Number: {
		auto* field = new NumberField(property.value, m_formHost);
		field->setReadOnly(m_readOnly);
		field->setLayoutDirection(Qt::LeftToRight);
		field->setToolTip(tr("Up and Down step by 0.1; Shift steps by 1 and Ctrl by 0.01."));
		field->stepped = [this, id]() { scheduleStep(id); };
		field->stepFinished = [this, id]() {
			if (m_stepProperty == id) {
				flushPendingStep();
			}
		};
		connect(field, &QLineEdit::editingFinished, this, [this, id, field]() {
			if (m_stepProperty == id) {
				m_stepProperty.clear();
				m_stepTimer->stop();
			}
			const MaterialGraphProperty* current = m_node.property(id);
			if (!current || field->text().trimmed() != current->value) {
				commitNumber(field, id);
			}
		});
		editor = field;
		break;
	}
	case MaterialGraphPropertyType::Expression: {
		auto* field = new QLineEdit(property.value, m_formHost);
		field->setReadOnly(m_readOnly);
		field->setLayoutDirection(Qt::LeftToRight);
		const auto check = [this, id, field]() {
			QVector<MaterialExpressionNode> nodes;
			QString error;
			const bool ok = parseMaterialExpression(field->text(), &nodes, &error) >= 0;
			if (QLabel* problem = m_problems.value(id)) {
				problem->setText(ok ? QString() : tr("Not an expression: %1").arg(error));
				problem->setVisible(!ok);
			}
			field->setAccessibleDescription(ok ? QString() : error);
			return ok;
		};
		connect(field, &QLineEdit::textEdited, this, [check]() { check(); });
		connect(field, &QLineEdit::editingFinished, this, [this, id, field, check]() {
			const MaterialGraphProperty* current = m_node.property(id);
			if ((!current || field->text().trimmed() != current->value) && check()) {
				commit(field, id, field->text().trimmed());
			}
		});
		editor = field;
		break;
	}
	case MaterialGraphPropertyType::Image: {
		auto* host = new QWidget(m_formHost);
		auto* row = new QHBoxLayout(host);
		row->setContentsMargins(0, 0, 0, 0);
		row->setSpacing(4);
		auto* field = new QLineEdit(property.value, host);
		field->setReadOnly(m_readOnly);
		field->setAccessibleName(property.label);
		field->setLayoutDirection(Qt::LeftToRight);
		if (!m_images.isEmpty()) {
			auto* completer = new QCompleter(m_images, field);
			completer->setCaseSensitivity(Qt::CaseInsensitive);
			completer->setFilterMode(Qt::MatchContains);
			field->setCompleter(completer);
		}
		auto* show = new QToolButton(host);
		show->setText(tr("Show"));
		show->setAccessibleName(tr("Show %1 in Textures").arg(property.label));
		show->setToolTip(tr("Show this image on the Textures page."));
		connect(show, &QToolButton::clicked, this, [this, field]() { Q_EMIT showImageRequested(field->text().trimmed()); });
		connect(field, &QLineEdit::editingFinished, this, [this, id, field]() {
			const MaterialGraphProperty* current = m_node.property(id);
			if (!current || field->text().trimmed() != current->value) {
				commit(field, id, field->text().trimmed());
			}
		});
		row->addWidget(field, 1);
		row->addWidget(show);
		host->setFocusProxy(field);
		editor = host;
		break;
	}
	case MaterialGraphPropertyType::ReadOnly: {
		auto* label = new QLabel(property.value.isEmpty() ? QStringLiteral("-") : property.value, m_formHost);
		label->setWordWrap(true);
		label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
		label->setFocusPolicy(Qt::TabFocus);
		editor = label;
		break;
	}
	case MaterialGraphPropertyType::Text: {
		auto* field = new QLineEdit(property.value, m_formHost);
		field->setReadOnly(m_readOnly);
		if (technical) {
			field->setLayoutDirection(Qt::LeftToRight);
		}
		connect(field, &QLineEdit::editingFinished, this, [this, id, field]() {
			const MaterialGraphProperty* current = m_node.property(id);
			if (!current || field->text() != current->value) {
				commit(field, id, field->text());
			}
		});
		editor = field;
		break;
	}
	}
	editor->setObjectName(QStringLiteral("materialProperty_%1").arg(id));
	if (editor->accessibleName().isEmpty()) {
		editor->setAccessibleName(property.label);
	}
	if (!property.help.isEmpty()) {
		editor->setToolTip(editor->toolTip().isEmpty() ? property.help : property.help + QLatin1Char('\n') + editor->toolTip());
		if (editor->accessibleDescription().isEmpty()) {
			editor->setAccessibleDescription(property.help);
		}
	}
	return editor;
}

void MaterialPropertyPanel::refreshValues()
{
	for (const MaterialGraphProperty& property : m_node.properties) {
		QWidget* editor = m_editors.value(property.id);
		if (!editor || editor->hasFocus() || editor->isAncestorOf(QApplication::focusWidget())) {
			continue;
		}
		const QSignalBlocker blocker(editor);
		if (auto* box = qobject_cast<QCheckBox*>(editor)) {
			box->setChecked(property.value == QStringLiteral("true"));
		} else if (auto* combo = qobject_cast<QComboBox*>(editor)) {
			if (combo->findData(property.value) < 0) {
				combo->addItem(property.value, property.value);
			}
			combo->setCurrentIndex(combo->findData(property.value));
		} else if (auto* spin = qobject_cast<QSpinBox*>(editor)) {
			spin->setValue(property.value.toInt());
		} else if (auto* field = qobject_cast<QLineEdit*>(editor)) {
			field->setText(property.value);
		} else if (auto* label = qobject_cast<QLabel*>(editor)) {
			label->setText(property.value.isEmpty() ? QStringLiteral("-") : property.value);
		} else if (auto* field = editor->findChild<QLineEdit*>()) {
			const QSignalBlocker inner(field);
			field->setText(property.value);
		}
		if (QLabel* problem = m_problems.value(property.id)) {
			problem->setVisible(false);
		}
	}
}

void MaterialPropertyPanel::scheduleStep(const QString& propertyId)
{
	if (!m_stepProperty.isEmpty() && m_stepProperty != propertyId) {
		flushPendingStep();
	}
	m_stepProperty = propertyId;
	m_stepTimer->start();
}

void MaterialPropertyPanel::flushPendingStep()
{
	m_stepTimer->stop();
	const QString id = std::exchange(m_stepProperty, QString());
	if (id.isEmpty()) {
		return;
	}
	QWidget* editor = m_editors.value(id);
	if (auto* spin = qobject_cast<QSpinBox*>(editor)) {
		commit(spin, id, QString::number(spin->value()));
	} else if (auto* field = qobject_cast<QLineEdit*>(editor)) {
		commitNumber(field, id);
	}
}

void MaterialPropertyPanel::commitNumber(QLineEdit* field, const QString& propertyId)
{
	bool ok = false;
	field->text().trimmed().toDouble(&ok);
	if (QLabel* problem = m_problems.value(propertyId)) {
		problem->setText(ok ? QString() : tr("Enter a number, such as 0.5."));
		problem->setVisible(!ok);
	}
	field->setAccessibleDescription(ok ? QString() : tr("Enter a number, such as 0.5."));
	if (ok) {
		commit(field, propertyId, field->text().trimmed());
	}
}

void MaterialPropertyPanel::commit(QWidget* editor, const QString& propertyId, const QString& value)
{
	if (!m_hasNode || m_readOnly) {
		return;
	}
	// A control of a node no longer shown, saying goodbye as it is replaced.
	if (editor && (!m_formHost || !m_formHost->isAncestorOf(editor))) {
		return;
	}
	const MaterialGraphProperty* current = m_node.property(propertyId);
	if (current && current->value == value) {
		return;
	}
	const QString nodeId = m_node.id;
	const QString key = nodeId + QLatin1Char('\n') + propertyId + QLatin1Char('\n') + value;
	if (key == m_pendingCommit) {
		return;
	}
	m_pendingCommit = key;
	const QString label = current ? current->label : propertyId;
	// Sent once the control's own signal has returned: the edit rebuilds the
	// graph, and with it perhaps these controls.
	QMetaObject::invokeMethod(
		this,
		[this, nodeId, propertyId, value, label, key]() {
			if (m_pendingCommit == key) {
				m_pendingCommit.clear();
			}
			Q_EMIT propertyEdited(nodeId, propertyId, value);
			// The workbench shows the edited node again before the signal
			// returns; the value it now has says whether the edit applied.
			if (m_hasNode && m_node.id == nodeId) {
				if (const MaterialGraphProperty* now = m_node.property(propertyId); now && now->value == value) {
					QAccessibleAnnouncementEvent announcement(this, tr("%1 set to %2.").arg(label, valueText(*now, value)));
					QAccessible::updateAccessibility(&announcement);
				}
			}
		},
		Qt::QueuedConnection);
}

void MaterialPropertyPanel::focusFirstField()
{
	for (const MaterialGraphProperty& property : m_node.properties) {
		QWidget* editor = m_editors.value(property.id);
		if (editor && editor->isEnabled() && property.type != MaterialGraphPropertyType::ReadOnly) {
			editor->setFocus(Qt::OtherFocusReason);
			return;
		}
	}
	if (!m_editors.isEmpty()) {
		if (QWidget* any = m_editors.begin().value()) {
			any->setFocus(Qt::OtherFocusReason);
		}
	}
}

QWidget* MaterialPropertyPanel::editorFor(const QString& propertyId) const
{
	return m_editors.value(propertyId);
}

void MaterialPropertyPanel::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::LanguageChange) {
		setAccessibleName(tr("Node properties"));
		m_scroll->setAccessibleName(tr("Node properties"));
		m_empty->setText(tr("Select a node to edit its properties."));
		// Show, the step hints and "nothing to set" are made with the rows.
		rebuild();
	}
}

} // namespace vibestudio
