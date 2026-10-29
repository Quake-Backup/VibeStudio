#include "app/model_constraints_dialog.h"
#include "app/dialog_screen_fit.h"
#include "core/model_animation_constraints.h"
#include "core/model_skeleton.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelConstraintsDialog)
};
void accessible(QWidget *widget, const QString &name, const QString &description)
{
	widget->setAccessibleName(name);
	widget->setAccessibleDescription(description);
	widget->setToolTip(description);
}
QString kindTitle(ModelClipConstraintKind kind)
{
	switch (kind)
	{
	case ModelClipConstraintKind::Ik:
		return Text::tr("Inverse kinematics");
	case ModelClipConstraintKind::Aim:
		return Text::tr("Aim");
	case ModelClipConstraintKind::ChildOf:
		return Text::tr("Child of");
	case ModelClipConstraintKind::CopyTransform:
		return Text::tr("Copy transform");
	case ModelClipConstraintKind::FootLock:
		return Text::tr("Foot lock");
	}
	return {};
}
} // namespace

ModelConstraintsDialog::ModelConstraintsDialog(const ModelMesh &mesh, int frame, QWidget *parent) : QDialog(parent), m_mesh(mesh)
{
	setObjectName("modelConstraintsDialog");
	setWindowTitle(Text::tr("Constraints and Layers"));
	accessible(this, windowTitle(),
			   Text::tr("Persistent constraints and animation layers of a skeletal clip, evaluated over its base samples every time the clip "
						"changes."));
	auto *layout = new QVBoxLayout(this);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	auto *content = new QWidget;
	auto *form = new QFormLayout(content);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	scroll->setWidget(content);
	layout->addWidget(scroll, 1);
	m_clip = new QComboBox;
	m_clip->setObjectName("constraintsClip");
	m_clip->setLayoutDirection(Qt::LeftToRight);
	accessible(m_clip, Text::tr("Clip"), Text::tr("The skeletal clip whose constraints and layers to edit."));
	for (int i = 0; i < mesh.skeleton.clips.size(); ++i)
		m_clip->addItem(QStringLiteral("%1: %2").arg(i).arg(mesh.skeleton.clips[i].name), i);
	m_clip->setCurrentIndex(std::max(0, m_clip->findData(mesh.skeleton.bakedClipForFrame.value(frame, 0))));
	form->addRow(Text::tr("&Clip"), m_clip);
	m_constraints = new QListWidget;
	m_constraints->setObjectName("constraintsList");
	m_constraints->setLayoutDirection(Qt::LeftToRight);
	m_constraints->setMinimumHeight(fontMetrics().height() * 5);
	accessible(m_constraints, Text::tr("Constraints"), Text::tr("The clip's constraints in evaluation order. Select one to edit it below."));
	form->addRow(Text::tr("Constraints"), m_constraints);
	const auto combo = [&](const char *id, const QString &label, const QString &description)
	{
		auto *field = new QComboBox;
		field->setObjectName(id);
		field->setLayoutDirection(Qt::LeftToRight);
		accessible(field, label, description);
		form->addRow(label, field);
		return field;
	};
	m_kind = combo("constraintKind", Text::tr("Kind"),
				   Text::tr("Inverse kinematics reaches a target; aim turns a joint towards one; child of and copy transform follow another "
							"joint; foot lock holds a joint where it is at the first sample."));
	for (int kind = 0; kind <= int(ModelClipConstraintKind::FootLock); ++kind)
		m_kind->addItem(kindTitle(ModelClipConstraintKind(kind)), kind);
	m_joint = combo("constraintJoint", Text::tr("Joint"), Text::tr("The constrained joint; its descendants follow it."));
	m_root = combo("constraintRoot", Text::tr("Chain root"), Text::tr("Inverse kinematics and foot lock: the chain's first joint. Two bones uses the joint's grandparent."));
	m_target = combo("constraintTarget", Text::tr("Target joint"),
					 Text::tr("The joint aimed at, reached or followed. With a fixed point the point is in model space; with a joint it is an "
							  "offset in that joint's space."));
	m_root->addItem(Text::tr("Two bones"), QString());
	m_target->addItem(Text::tr("Fixed point"), QString());
	for (const auto &joint : mesh.skeleton.joints)
	{
		m_joint->addItem(joint.name, joint.name);
		m_root->addItem(joint.name, joint.name);
		m_target->addItem(joint.name, joint.name);
	}
	const QStringList axes{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
	const auto vector = [&](const char *prefix, const QString &label, const QString &description, std::array<QDoubleSpinBox *, 3> *fields)
	{
		auto *row = new QWidget;
		auto *grid = new QHBoxLayout(row);
		grid->setContentsMargins(0, 0, 0, 0);
		for (int i = 0; i < 3; ++i)
		{
			auto *spin = new QDoubleSpinBox;
			spin->setObjectName(QStringLiteral("%1%2").arg(QLatin1String(prefix)).arg(i));
			spin->setRange(-1000000, 1000000);
			spin->setDecimals(4);
			spin->setLayoutDirection(Qt::LeftToRight);
			spin->setPrefix(axes[i] + QLatin1Char(' '));
			accessible(spin, Text::tr("%1 %2").arg(label, axes[i]), description);
			grid->addWidget(spin);
			(*fields)[i] = spin;
		}
		form->addRow(label, row);
	};
	vector("constraintPoint", Text::tr("Point"), Text::tr("The fixed target, or the offset from the target joint."), &m_point);
	vector("constraintPole", Text::tr("Pole"), Text::tr("Inverse kinematics: the bend pole. Aim: the up direction."), &m_pole);
	m_usePole = new QCheckBox(Text::tr("Use the pole"));
	m_usePole->setObjectName("constraintUsePole");
	m_usePole->setChecked(true);
	accessible(m_usePole, m_usePole->text(), Text::tr("Steer the bend (inverse kinematics) or the twist (aim) with the pole."));
	form->addRow(m_usePole);
	m_axis = combo("constraintAxis", Text::tr("Aim axis"), Text::tr("The joint axis that points at the target."));
	for (const auto &axis : {QStringLiteral("+X"), QStringLiteral("+Y"), QStringLiteral("+Z"), QStringLiteral("-X"), QStringLiteral("-Y"), QStringLiteral("-Z")})
		m_axis->addItem(axis);
	m_first = new QSpinBox;
	m_first->setObjectName("constraintFirst");
	m_last = new QSpinBox;
	m_last->setObjectName("constraintLast");
	for (auto *spin : {m_first, m_last})
		spin->setLayoutDirection(Qt::LeftToRight);
	m_last->setMinimum(-1);
	m_last->setSpecialValueText(Text::tr("Clip end"));
	accessible(m_first, Text::tr("First sample"), Text::tr("The first sample the constraint applies to."));
	accessible(m_last, Text::tr("Last sample"), Text::tr("The last sample it applies to; Clip end follows the clip's length."));
	form->addRow(Text::tr("&First sample"), m_first);
	form->addRow(Text::tr("&Last sample"), m_last);
	m_weight = new QDoubleSpinBox;
	m_weight->setObjectName("constraintWeight");
	m_weight->setRange(0, 1);
	m_weight->setDecimals(3);
	m_weight->setSingleStep(.1);
	m_weight->setValue(1);
	accessible(m_weight, Text::tr("Weight"), Text::tr("Blend from the unconstrained pose (0) to the constrained one (1)."));
	form->addRow(Text::tr("&Weight"), m_weight);
	m_enabled = new QCheckBox(Text::tr("Enabled"));
	m_enabled->setObjectName("constraintEnabled");
	m_enabled->setChecked(true);
	accessible(m_enabled, m_enabled->text(), Text::tr("A disabled constraint is kept but does not change the clip."));
	form->addRow(m_enabled);
	m_name = new QLineEdit;
	m_name->setObjectName("constraintName");
	m_name->setMaxLength(63);
	accessible(m_name, Text::tr("Constraint name"), Text::tr("An optional name shown in the list and in messages."));
	form->addRow(Text::tr("&Name"), m_name);
	auto *constraintButtons = new QHBoxLayout;
	auto *add = new QPushButton(Text::tr("&Add Constraint"));
	add->setObjectName("constraintAdd");
	accessible(add, Text::tr("Add constraint"), Text::tr("Add the constraint described above to the clip."));
	m_update = new QPushButton(Text::tr("&Update Selected"));
	m_update->setObjectName("constraintUpdate");
	accessible(m_update, Text::tr("Update selected constraint"), Text::tr("Replace the selected constraint with the one described above."));
	m_remove = new QPushButton(Text::tr("&Remove Selected"));
	m_remove->setObjectName("constraintRemove");
	accessible(m_remove, Text::tr("Remove selected constraint"), Text::tr("Remove the selected constraint; its effect leaves the clip."));
	for (auto *button : {add, m_update, m_remove})
		constraintButtons->addWidget(button);
	form->addRow(constraintButtons);
	m_layers = new QListWidget;
	m_layers->setObjectName("constraintsLayers");
	m_layers->setLayoutDirection(Qt::LeftToRight);
	m_layers->setMinimumHeight(fontMetrics().height() * 3);
	accessible(m_layers, Text::tr("Layers"), Text::tr("The clip's animation layers, applied before the constraints. Add layers with model constraints --operation add-layer."));
	form->addRow(Text::tr("Layers"), m_layers);
	m_layerMode = combo("layerMode", Text::tr("Layer mode"), Text::tr("Additive layers add to the base pose; override layers replace the channels they key."));
	m_layerMode->addItems({Text::tr("Additive"), Text::tr("Override")});
	m_layerWeight = new QDoubleSpinBox;
	m_layerWeight->setObjectName("layerWeight");
	m_layerWeight->setRange(0, 1);
	m_layerWeight->setDecimals(3);
	m_layerWeight->setSingleStep(.1);
	accessible(m_layerWeight, Text::tr("Layer weight"), Text::tr("How much of the layer applies, from 0 to 1."));
	form->addRow(Text::tr("Layer weight"), m_layerWeight);
	m_layerEnabled = new QCheckBox(Text::tr("Layer enabled"));
	m_layerEnabled->setObjectName("layerEnabled");
	accessible(m_layerEnabled, m_layerEnabled->text(), Text::tr("A disabled layer is kept but does not change the clip."));
	form->addRow(m_layerEnabled);
	auto *layerButtons = new QHBoxLayout;
	m_updateLayer = new QPushButton(Text::tr("Update &Layer"));
	m_updateLayer->setObjectName("layerUpdate");
	accessible(m_updateLayer, Text::tr("Update selected layer"), Text::tr("Apply the mode, weight and enabled state to the selected layer."));
	m_removeLayer = new QPushButton(Text::tr("Remove La&yer"));
	m_removeLayer->setObjectName("layerRemove");
	accessible(m_removeLayer, Text::tr("Remove selected layer"), Text::tr("Remove the selected layer from the clip."));
	layerButtons->addWidget(m_updateLayer);
	layerButtons->addWidget(m_removeLayer);
	form->addRow(layerButtons);
	m_status = new LiveStatusLabel;
	m_status->setObjectName("constraintsStatus");
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	accessible(m_status, Text::tr("Constraint check"), Text::tr("Whether the constraint can be added, or what to fix."));
	layout->addWidget(m_status);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
	m_bake = buttons->addButton(Text::tr("&Bake Stack"), QDialogButtonBox::ActionRole);
	m_bake->setObjectName("constraintsBake");
	accessible(m_bake, Text::tr("Bake constraints and layers"),
			   Text::tr("Keep the evaluated samples and drop the clip's constraints and layers, so direct pose edits apply again."));
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(add, &QPushButton::clicked, this, [this] { choose(ModelClipStackAction::AddConstraint); });
	connect(m_update, &QPushButton::clicked, this, [this] { choose(ModelClipStackAction::UpdateConstraint); });
	connect(m_remove, &QPushButton::clicked, this, [this] { choose(ModelClipStackAction::RemoveConstraint); });
	connect(m_updateLayer, &QPushButton::clicked, this, [this] { choose(ModelClipStackAction::UpdateLayer); });
	connect(m_removeLayer, &QPushButton::clicked, this, [this] { choose(ModelClipStackAction::RemoveLayer); });
	connect(m_bake, &QPushButton::clicked, this, [this] { choose(ModelClipStackAction::Bake); });
	connect(m_clip, &QComboBox::currentIndexChanged, this, [this] { loadClip(); });
	connect(m_constraints, &QListWidget::currentRowChanged, this, [this] { loadConstraint(); });
	connect(m_layers, &QListWidget::currentRowChanged, this, [this] { loadLayer(); });
	for (auto *field : {m_kind, m_joint, m_root, m_target, m_axis})
		connect(field, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
	fitDialogToScreen(this, QSize(std::max(640, fontMetrics().averageCharWidth() * 80), 760));
	loadClip();
}

int ModelConstraintsDialog::clip() const
{
	return m_clip->currentIndex() < 0 ? -1 : m_clip->currentData().toInt();
}

void ModelConstraintsDialog::loadClip()
{
	const QSignalBlocker constraints(m_constraints), layers(m_layers);
	m_constraints->clear();
	m_layers->clear();
	const int index = clip();
	if (index >= 0)
	{
		const auto &value = m_mesh.skeleton.clips[index];
		for (const auto &c : value.constraints)
			m_constraints->addItem(QStringLiteral("%1 · %2 · %3%4")
									   .arg(c.name.isEmpty() ? kindTitle(c.kind) : c.name, c.joint, c.target.isEmpty() ? QStringLiteral("•") : c.target)
									   .arg(c.enabled ? QString() : QStringLiteral(" ·  –")));
		for (const auto &l : value.layers)
			m_layers->addItem(QStringLiteral("%1 · %2").arg(l.name, QString::number(l.weight)));
		for (auto *spin : {m_first, m_last})
			spin->setMaximum(std::max(0, int(value.frames.size()) - 1));
	}
	m_last->setValue(-1);
	refresh();
}

void ModelConstraintsDialog::loadConstraint()
{
	const int index = clip(), row = m_constraints->currentRow();
	if (index < 0 || row < 0)
	{
		refresh();
		return;
	}
	const auto &c = m_mesh.skeleton.clips[index].constraints[row];
	m_kind->setCurrentIndex(int(c.kind));
	m_joint->setCurrentIndex(std::max(0, m_joint->findData(c.joint)));
	m_root->setCurrentIndex(std::max(0, m_root->findData(c.rootJoint)));
	m_target->setCurrentIndex(std::max(0, m_target->findData(c.target)));
	for (int i = 0; i < 3; ++i)
	{
		m_point[i]->setValue(c.point[i]);
		m_pole[i]->setValue(c.pole[i]);
	}
	m_usePole->setChecked(c.usePole);
	m_axis->setCurrentIndex(c.axis);
	m_first->setValue(c.first);
	m_last->setValue(c.last);
	m_weight->setValue(c.weight);
	m_enabled->setChecked(c.enabled);
	m_name->setText(c.name);
	refresh();
}

void ModelConstraintsDialog::loadLayer()
{
	const int index = clip(), row = m_layers->currentRow();
	if (index >= 0 && row >= 0)
	{
		const auto &l = m_mesh.skeleton.clips[index].layers[row];
		m_layerMode->setCurrentIndex(l.additive ? 0 : 1);
		m_layerWeight->setValue(l.weight);
		m_layerEnabled->setChecked(l.enabled);
	}
	refresh();
}

ModelClipConstraint ModelConstraintsDialog::formConstraint() const
{
	ModelClipConstraint c;
	c.kind = ModelClipConstraintKind(m_kind->currentData().toInt());
	c.joint = m_joint->currentData().toString();
	const bool chain = c.kind == ModelClipConstraintKind::Ik || c.kind == ModelClipConstraintKind::FootLock;
	if (chain)
		c.rootJoint = m_root->currentData().toString();
	if (c.kind != ModelClipConstraintKind::FootLock)
		c.target = m_target->currentData().toString();
	for (int i = 0; i < 3; ++i)
	{
		c.point[i] = m_point[i]->value();
		c.pole[i] = m_pole[i]->value();
	}
	c.usePole = m_usePole->isChecked();
	c.axis = m_axis->currentIndex();
	c.first = m_first->value();
	c.last = m_last->value();
	c.weight = m_weight->value();
	c.enabled = m_enabled->isChecked();
	c.name = m_name->text();
	return c;
}

void ModelConstraintsDialog::refresh()
{
	const int index = clip();
	const auto kind = ModelClipConstraintKind(m_kind->currentData().toInt());
	const bool chain = kind == ModelClipConstraintKind::Ik || kind == ModelClipConstraintKind::FootLock;
	m_root->setEnabled(chain);
	m_target->setEnabled(kind != ModelClipConstraintKind::FootLock);
	m_axis->setEnabled(kind == ModelClipConstraintKind::Aim);
	const bool selected = m_constraints->currentRow() >= 0, layer = m_layers->currentRow() >= 0;
	m_update->setEnabled(selected);
	m_remove->setEnabled(selected);
	m_updateLayer->setEnabled(layer);
	m_removeLayer->setEnabled(layer);
	m_bake->setEnabled(index >= 0 && modelClipHasStack(m_mesh.skeleton.clips[index]));
	if (index < 0)
	{
		m_status->setText(Text::tr("Add a skeletal clip before adding constraints."), LiveStatus::Problem);
		return;
	}
	auto probe = m_mesh.skeleton.clips[index];
	if (probe.base.isEmpty())
		probe.base = probe.frames;
	probe.constraints.append(formConstraint());
	QString problem;
	if (!validModelClipStack(m_mesh.skeleton, probe, &problem))
		m_status->setText(problem, LiveStatus::Problem);
	else
		m_status->setText(Text::tr("Clip %1: %n constraint(s).", nullptr, int(m_mesh.skeleton.clips[index].constraints.size()))
							  .arg(m_mesh.skeleton.clips[index].name),
						  LiveStatus::Result);
}

void ModelConstraintsDialog::choose(ModelClipStackAction action)
{
	const int index = clip();
	if (index < 0)
		return;
	ModelEdit edit;
	edit.kind = ModelEditKind::EditClipStack;
	auto &o = edit.animation.stack;
	o.action = action;
	o.clip = index;
	switch (action)
	{
	case ModelClipStackAction::AddConstraint:
		o.constraint = formConstraint();
		break;
	case ModelClipStackAction::UpdateConstraint:
		o.index = m_constraints->currentRow();
		o.constraint = formConstraint();
		break;
	case ModelClipStackAction::RemoveConstraint:
		o.index = m_constraints->currentRow();
		break;
	case ModelClipStackAction::UpdateLayer:
		o.index = m_layers->currentRow();
		if (o.index >= 0)
		{
			o.layer = m_mesh.skeleton.clips[index].layers[o.index];
			o.layer.additive = m_layerMode->currentIndex() == 0;
			o.layer.weight = m_layerWeight->value();
			o.layer.enabled = m_layerEnabled->isChecked();
		}
		break;
	case ModelClipStackAction::RemoveLayer:
		o.index = m_layers->currentRow();
		break;
	case ModelClipStackAction::AddLayer:
	case ModelClipStackAction::Bake:
		break;
	}
	// A quick check here; the editor applies (and fully validates) the edit in
	// its worker.
	if (action == ModelClipStackAction::AddConstraint || action == ModelClipStackAction::UpdateConstraint)
	{
		auto probe = m_mesh.skeleton.clips[index];
		if (probe.base.isEmpty())
			probe.base = probe.frames;
		if (action == ModelClipStackAction::AddConstraint)
			probe.constraints.append(o.constraint);
		else if (o.index >= 0 && o.index < probe.constraints.size())
			probe.constraints[o.index] = o.constraint;
		QString problem;
		if (!validModelClipStack(m_mesh.skeleton, probe, &problem))
		{
			m_status->setText(problem, LiveStatus::Problem);
			return;
		}
	}
	if (action != ModelClipStackAction::AddConstraint && action != ModelClipStackAction::Bake && o.index < 0)
		return;
	m_edit = edit;
	accept();
}
} // namespace vibestudio
