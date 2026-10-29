#include "app/model_events_dialog.h"
#include "app/dialog_screen_fit.h"
#include "core/model_animation_events.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelEventsDialog)
};
void accessible(QWidget *widget, const QString &name, const QString &description)
{
	widget->setAccessibleName(name);
	widget->setAccessibleDescription(description);
	widget->setToolTip(description);
}
} // namespace

ModelEventsDialog::ModelEventsDialog(const ModelMesh &mesh, int frame, QWidget *parent) : QDialog(parent), m_mesh(mesh), m_frame(frame)
{
	setObjectName("modelEventsDialog");
	setWindowTitle(Text::tr("Animation Events"));
	accessible(this, windowTitle(),
			   Text::tr("Game-side frame commands, such as sounds, footsteps and attack triggers, on frames of a clip. Doom 3 model "
						"declarations write them as frame commands."));
	auto *layout = new QVBoxLayout(this);
	auto *form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	layout->addLayout(form);
	m_clip = new QComboBox;
	m_clip->setObjectName("eventsClip");
	m_clip->setLayoutDirection(Qt::LeftToRight);
	accessible(m_clip, Text::tr("Clip"),
			   skeletal() ? Text::tr("The skeletal clip whose events to edit.") : Text::tr("The animation whose events to edit."));
	int current = 0;
	if (skeletal())
	{
		for (int i = 0; i < mesh.skeleton.clips.size(); ++i)
			m_clip->addItem(QStringLiteral("%1: %2").arg(i).arg(mesh.skeleton.clips[i].name), i);
		current = std::max(0, mesh.skeleton.bakedClipForFrame.value(frame, 0));
	}
	else
		for (int i = 0; i < mesh.animations.size(); ++i)
		{
			const auto &animation = mesh.animations[i];
			m_clip->addItem(QStringLiteral("%1: %2").arg(i).arg(animation.name), i);
			if (frame >= animation.firstFrame && frame < animation.firstFrame + animation.frameCount)
				current = i;
		}
	m_clip->setCurrentIndex(std::min(current, m_clip->count() - 1));
	form->addRow(Text::tr("&Clip"), m_clip);
	m_table = new QTableWidget(0, 2);
	m_table->setObjectName("eventsTable");
	m_table->setHorizontalHeaderLabels({Text::tr("Frame"), Text::tr("Command")});
	m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	m_table->setLayoutDirection(Qt::LeftToRight);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	accessible(m_table, Text::tr("Events"),
			   Text::tr("One row per event: the frame, numbered from zero within the clip, and the command with its arguments, such as "
						"sound_body snd_footstep."));
	layout->addWidget(m_table, 1);
	auto *rowButtons = new QHBoxLayout;
	auto *add = new QPushButton(Text::tr("&Add Event"));
	add->setObjectName("eventsAdd");
	accessible(add, Text::tr("Add event"), Text::tr("Add an event at the frame shown in the editor. Then type its command."));
	m_remove = new QPushButton(Text::tr("&Remove Event"));
	m_remove->setObjectName("eventsRemove");
	accessible(m_remove, Text::tr("Remove event"), Text::tr("Remove the selected events."));
	rowButtons->addWidget(add);
	rowButtons->addWidget(m_remove);
	rowButtons->addStretch(1);
	layout->addLayout(rowButtons);
	m_status = new LiveStatusLabel;
	m_status->setObjectName("eventsStatus");
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	accessible(m_status, Text::tr("Event check"), Text::tr("Whether the events can be applied, or what to fix."));
	layout->addWidget(m_status);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
	m_apply = buttons->button(QDialogButtonBox::Apply);
	m_apply->setObjectName("eventsApply");
	accessible(m_apply, Text::tr("Apply events"), Text::tr("Replace the clip's events with the table as one undoable edit."));
	layout->addWidget(buttons);
	connect(m_apply, &QPushButton::clicked, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(add, &QPushButton::clicked, this, &ModelEventsDialog::addEvent);
	connect(m_remove, &QPushButton::clicked, this,
			[this]
			{
				QList<int> rows;
				for (const auto &range : m_table->selectedRanges())
					for (int row = range.topRow(); row <= range.bottomRow(); ++row)
						rows.append(row);
				std::sort(rows.begin(), rows.end(), std::greater<int>());
				for (int row : rows)
					m_table->removeRow(row);
				validate();
			});
	connect(m_clip, &QComboBox::currentIndexChanged, this, [this] { load(); });
	connect(m_table, &QTableWidget::itemChanged, this, [this] { validate(); });
	fitDialogToScreen(this, QSize(std::max(560, fontMetrics().averageCharWidth() * 70), 520));
	load();
}

bool ModelEventsDialog::skeletal() const
{
	return !m_mesh.skeleton.isEmpty();
}

int ModelEventsDialog::frameCount() const
{
	const int index = m_clip->currentData().toInt();
	if (m_clip->currentIndex() < 0)
		return 0;
	return skeletal() ? int(m_mesh.skeleton.clips[index].frames.size()) : m_mesh.animations[index].frameCount;
}

void ModelEventsDialog::load()
{
	const QSignalBlocker blocker(m_table);
	m_table->setRowCount(0);
	if (m_clip->currentIndex() >= 0)
	{
		const int index = m_clip->currentData().toInt();
		const auto &events = skeletal() ? m_mesh.skeleton.clips[index].events : m_mesh.animations[index].events;
		for (const auto &event : events)
		{
			const int row = m_table->rowCount();
			m_table->insertRow(row);
			m_table->setItem(row, 0, new QTableWidgetItem(QString::number(event.frame)));
			m_table->setItem(row, 1, new QTableWidgetItem(modelAnimationEventCommandLine(event)));
		}
	}
	validate();
}

void ModelEventsDialog::addEvent()
{
	if (m_clip->currentIndex() < 0)
		return;
	const int index = m_clip->currentData().toInt();
	int frame = 0;
	if (skeletal())
	{
		const int first = int(m_mesh.skeleton.bakedClipForFrame.indexOf(index));
		if (first >= 0 && m_mesh.skeleton.bakedClipForFrame.value(m_frame, -2) == index)
			frame = m_frame - first;
	}
	else if (m_frame >= m_mesh.animations[index].firstFrame && m_frame < m_mesh.animations[index].firstFrame + m_mesh.animations[index].frameCount)
		frame = m_frame - m_mesh.animations[index].firstFrame;
	const int row = m_table->rowCount();
	{
		const QSignalBlocker blocker(m_table);
		m_table->insertRow(row);
		m_table->setItem(row, 0, new QTableWidgetItem(QString::number(frame)));
		m_table->setItem(row, 1, new QTableWidgetItem(QString()));
	}
	m_table->setCurrentCell(row, 1);
	m_table->editItem(m_table->item(row, 1));
	validate();
}

bool ModelEventsDialog::tableEvents(QVector<ModelAnimationEvent> *events) const
{
	QVector<ModelAnimationEvent> result;
	for (int row = 0; row < m_table->rowCount(); ++row)
	{
		bool ok = false;
		const auto *frameItem = m_table->item(row, 0), *commandItem = m_table->item(row, 1);
		const int frame = frameItem ? frameItem->text().trimmed().toInt(&ok) : 0;
		ModelAnimationEvent event;
		QString problem;
		if (!ok || !commandItem || !parseModelAnimationEventCommandLine(commandItem->text(), frame, &event, &problem) ||
			!validModelAnimationEvent(event, frameCount(), &problem))
		{
			m_status->setText(Text::tr("Row %1: %2").arg(row + 1).arg(problem.isEmpty() ? Text::tr("type a whole frame number.") : problem),
							  LiveStatus::Problem);
			return false;
		}
		result.append(event);
	}
	if (events)
		*events = sortedModelAnimationEvents(result);
	return true;
}

void ModelEventsDialog::validate()
{
	m_remove->setEnabled(m_table->rowCount() > 0);
	if (m_clip->currentIndex() < 0)
	{
		m_status->setText(skeletal() ? Text::tr("Add a skeletal clip before adding events.") : Text::tr("Add an animation before adding events."));
		m_apply->setEnabled(false);
		return;
	}
	QVector<ModelAnimationEvent> events;
	const bool ok = tableEvents(&events);
	if (ok)
		m_status->setText(Text::tr("%n event(s) on a clip of %1 frames.", nullptr, int(events.size())).arg(frameCount()), LiveStatus::Result);
	m_apply->setEnabled(ok);
}

ModelEdit ModelEventsDialog::edit() const
{
	ModelEdit result;
	result.kind = ModelEditKind::EditAnimationEvents;
	auto &o = result.animation.events;
	o.action = ModelAnimationEventAction::Replace;
	(skeletal() ? o.clip : o.animation) = m_clip->currentData().toInt();
	tableEvents(&o.events);
	return result;
}
} // namespace vibestudio
