#include "app/model_constraints_dialog.h"
#include "app/dialog_screen_fit.h"
#include "app/model_animation_curve_view.h"
#include "core/model_animation_constraints.h"
#include "core/model_animation_curves.h"
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
#include <algorithm>

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
	m_weightKeys = new QLineEdit;
	m_weightKeys->setObjectName("constraintWeightKeys");
	m_weightKeys->setLayoutDirection(Qt::LeftToRight);
	m_weightKeys->setPlaceholderText(QStringLiteral("0:0, 12:1"));
	accessible(m_weightKeys, Text::tr("Weight keys"),
			   Text::tr("Optional sample:value pairs from 0 to 1, linear between them, that scale the weight over the clip, such as 0:0, 12:1 to "
						"blend the constraint in. Empty keeps the weight constant."));
	form->addRow(Text::tr("Weight &keys"), m_weightKeys);
	m_limits = new QCheckBox(Text::tr("Keep joints within their limits"));
	m_limits->setObjectName("constraintLimits");
	accessible(m_limits, m_limits->text(),
			   Text::tr("Inverse kinematics and foot lock: keep every chain joint within the limits set in Edit Skeleton > Set joint limits."));
	form->addRow(m_limits);
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
	accessible(m_layers, Text::tr("Layers"),
			   Text::tr("The clip's animation layers, applied before the constraints. Select one to key it below, or key a new layer and add it."));
	form->addRow(Text::tr("Layers"), m_layers);
	m_layerName = new QLineEdit;
	m_layerName->setObjectName("layerName");
	m_layerName->setMaxLength(63);
	m_layerName->setLayoutDirection(Qt::LeftToRight);
	accessible(m_layerName, Text::tr("Layer name"), Text::tr("The layer's name, shown in the list and in messages."));
	form->addRow(Text::tr("Layer &name"), m_layerName);
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
	m_layerEnabled->setChecked(true);
	accessible(m_layerEnabled, m_layerEnabled->text(), Text::tr("A disabled layer is kept but does not change the clip."));
	form->addRow(m_layerEnabled);
	// Layer keys: one joint channel (or the layer's weight) in the shared
	// curve graph, with numeric Set Key and Remove Key beside it.
	m_layerJoint = combo("layerJoint", Text::tr("Keyed joint"), Text::tr("The joint whose layer channels to key."));
	for (const auto &joint : mesh.skeleton.joints)
		m_layerJoint->addItem(joint.name, joint.name);
	m_layerChannel = combo("layerChannel", Text::tr("Layer channel"),
						   Text::tr("Additive layers add translation and degrees and multiply scale; override layers set them. Weight scales the "
									"whole layer over the clip, from 0 to 1."));
	m_layerChannel->addItems({Text::tr("Translation X"), Text::tr("Translation Y"), Text::tr("Translation Z"), Text::tr("Rotation X"),
							  Text::tr("Rotation Y"), Text::tr("Rotation Z"), Text::tr("Scale X"), Text::tr("Scale Y"), Text::tr("Scale Z"),
							  Text::tr("Layer weight")});
	m_layerCurve = new ModelAnimationCurveView;
	m_layerCurve->setObjectName("layerCurve");
	m_layerCurve->setMinimumHeight(fontMetrics().height() * 10);
	form->addRow(m_layerCurve);
	auto *keyRow = new QWidget;
	auto *keyLayout = new QHBoxLayout(keyRow);
	keyLayout->setContentsMargins(0, 0, 0, 0);
	m_keySample = new QSpinBox;
	m_keySample->setObjectName("layerKeySample");
	m_keySample->setLayoutDirection(Qt::LeftToRight);
	accessible(m_keySample, Text::tr("Key sample"), Text::tr("The clip sample, from zero, that Set Key and Remove Key use."));
	m_keyValue = new QDoubleSpinBox;
	m_keyValue->setObjectName("layerKeyValue");
	m_keyValue->setRange(-1000000, 1000000);
	m_keyValue->setDecimals(6);
	m_keyValue->setLayoutDirection(Qt::LeftToRight);
	accessible(m_keyValue, Text::tr("Key value"), Text::tr("The channel's value at the key sample: units, degrees, a scale factor or a weight."));
	m_setKey = new QPushButton(Text::tr("Set &Key"));
	m_setKey->setObjectName("layerSetKey");
	accessible(m_setKey, Text::tr("Set layer key"), Text::tr("Insert or replace the key at the key sample in the layer draft."));
	m_removeKey = new QPushButton(Text::tr("Remove K&ey"));
	m_removeKey->setObjectName("layerRemoveKey");
	accessible(m_removeKey, Text::tr("Remove layer key"), Text::tr("Remove the key at the key sample from the layer draft."));
	for (QWidget *widget : std::initializer_list<QWidget *>{m_keySample, m_keyValue, m_setKey, m_removeKey})
		keyLayout->addWidget(widget);
	form->addRow(Text::tr("Key"), keyRow);
	auto *layerButtons = new QHBoxLayout;
	m_addLayer = new QPushButton(Text::tr("Add La&yer"));
	m_addLayer->setObjectName("layerAdd");
	accessible(m_addLayer, Text::tr("Add layer"), Text::tr("Add the keyed layer draft to the clip as a new layer."));
	layerButtons->addWidget(m_addLayer);
	m_updateLayer = new QPushButton(Text::tr("Update &Layer"));
	m_updateLayer->setObjectName("layerUpdate");
	accessible(m_updateLayer, Text::tr("Update selected layer"),
			   Text::tr("Apply the name, mode, weight, enabled state and keys of the draft to the selected layer."));
	m_removeLayer = new QPushButton(Text::tr("Remo&ve Layer"));
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
	connect(m_addLayer, &QPushButton::clicked, this, [this] { choose(ModelClipStackAction::AddLayer); });
	connect(m_setKey, &QPushButton::clicked, this,
			[this] { setLayerKey(m_layerJoint->currentData().toString(), m_layerChannel->currentIndex(), m_keySample->value(), m_keyValue->value()); });
	connect(m_removeKey, &QPushButton::clicked, this,
			[this] { removeLayerKey(m_layerJoint->currentData().toString(), m_layerChannel->currentIndex(), m_keySample->value()); });
	connect(m_layerJoint, &QComboBox::currentIndexChanged, this, [this] { showLayerCurve(); });
	connect(m_layerChannel, &QComboBox::currentIndexChanged, this, [this] { showLayerCurve(); });
	connect(m_weightKeys, &QLineEdit::textChanged, this, [this] { refresh(); });
	connect(m_layerName, &QLineEdit::textChanged, this, [this] { refresh(); });
	connect(m_limits, &QCheckBox::toggled, this, [this] { refresh(); });
	m_layerCurve->selected = [this](int index)
	{
		const auto keys = layerKeys(m_layerJoint->currentData().toString(), m_layerChannel->currentIndex());
		if (index >= 0 && index < keys.size())
		{
			m_keySample->setValue(keys[index].frame);
			m_keyValue->setValue(keys[index].value);
		}
	};
	m_layerCurve->edited = [this](int old, const ModelPoseCurveKey &key)
	{
		const auto joint = m_layerJoint->currentData().toString();
		const int channel = m_layerChannel->currentIndex();
		const auto before = m_layerDraft;
		if (old >= 0 && old != key.frame && !removeLayerKey(joint, channel, old))
			return;
		if (!setLayerKey(joint, channel, key.frame, key.value))
		{
			m_layerDraft = before;
			showLayerCurve();
		}
	};
	m_layerCurve->removed = [this](int sample) { removeLayerKey(m_layerJoint->currentData().toString(), m_layerChannel->currentIndex(), sample); };
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
	m_layerDraft = {};
	m_layerName->setText(QStringLiteral("layer"));
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
		for (auto *spin : {m_first, m_last, m_keySample})
			spin->setMaximum(std::max(0, int(value.frames.size()) - 1));
	}
	m_last->setValue(-1);
	showLayerCurve();
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
	m_weightKeys->setText(modelWeightKeysText(c.weightKeys));
	m_limits->setChecked(c.limits);
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
		m_layerName->setText(l.name);
		m_layerDraft = l;
		if (!l.curves.isEmpty())
			m_layerJoint->setCurrentIndex(std::max(0, m_layerJoint->findData(l.curves.first().joint)));
	}
	showLayerCurve();
	refresh();
}
int ModelConstraintsDialog::frameCount() const
{
	const int index = clip();
	return index < 0 ? 0 : int(m_mesh.skeleton.clips[index].frames.size());
}
QVector<ModelPoseCurveKey> ModelConstraintsDialog::layerKeys(const QString &joint, int channel) const
{
	if (channel == 9)
		return m_layerDraft.weightKeys;
	for (const auto &group : m_layerDraft.curves)
		if (group.joint == joint && channel >= 0 && channel < 9)
			return group.channels[channel];
	return {};
}
bool ModelConstraintsDialog::layerGroup(const QString &joint, ModelPoseCurves *group, QString *error) const
{
	for (const auto &existing : m_layerDraft.curves)
		if (existing.joint == joint)
		{
			*group = existing;
			return true;
		}
	// A joint's first keys change nothing: neutral offsets on an additive
	// layer, the base pose's own values at every sample on an override layer.
	*group = {};
	group->joint = joint;
	if (m_layerMode->currentIndex() == 0)
	{
		for (int c = 0; c < 9; ++c)
			group->channels[c] = {ModelPoseCurveKey{0, c >= 6 ? 1.0 : 0.0}};
		return true;
	}
	const int j = modelJointIndex(m_mesh.skeleton, joint, Qt::CaseSensitive);
	if (j < 0)
		return false;
	const auto &samples = modelClipEditableSamples(m_mesh.skeleton.clips[clip()]);
	const int parent = m_mesh.skeleton.joints[j].parent;
	std::array<double, 3> previous{};
	for (int f = 0; f < samples.size(); ++f)
	{
		const auto local = parent < 0 ? samples[f][j] : modelJointMultiply(modelJointInverse(samples[f][parent]), samples[f][j]);
		std::array<double, 9> values{};
		if (!modelPoseCurveValues(local, f > 0 ? &previous : nullptr, &values))
		{
			if (error)
				*error = Text::tr("Joint %1's pose has shear or zero scale, so an override layer cannot key it.").arg(joint);
			return false;
		}
		for (int c = 0; c < 9; ++c)
			group->channels[c].append(ModelPoseCurveKey{f, values[c]});
		previous = {values[3], values[4], values[5]};
	}
	return true;
}
bool ModelConstraintsDialog::setLayerKey(const QString &joint, int channel, int sample, double value)
{
	const int frames = frameCount();
	if (clip() < 0 || channel < 0 || channel > 9 || sample < 0 || sample >= frames)
		return false;
	QString error;
	if (channel == 9)
	{
		auto keys = m_layerDraft.weightKeys;
		auto at = std::lower_bound(keys.begin(), keys.end(), sample, [](const ModelPoseCurveKey &k, int f) { return k.frame < f; });
		if (at != keys.end() && at->frame == sample)
			at->value = value;
		else
			keys.insert(at, ModelPoseCurveKey{sample, value});
		if (!validModelWeightKeys(keys, frames, &error))
		{
			m_status->setText(error, LiveStatus::Problem);
			return false;
		}
		m_layerDraft.weightKeys = keys;
	}
	else
	{
		ModelPoseCurves group;
		if (!layerGroup(joint, &group, &error))
		{
			m_status->setText(error, LiveStatus::Problem);
			return false;
		}
		ModelCurveEditOptions o;
		o.action = ModelCurveAction::SetKey;
		o.joint = joint;
		o.channel = channel;
		o.frame = sample;
		o.key = ModelPoseCurveKey{sample, value};
		if (!editModelCurveGroup(&group, frames, o, &error))
		{
			m_status->setText(error, LiveStatus::Problem);
			return false;
		}
		const auto existing = std::find_if(m_layerDraft.curves.begin(), m_layerDraft.curves.end(), [&](const auto &g) { return g.joint == joint; });
		if (existing != m_layerDraft.curves.end())
			*existing = group;
		else
			m_layerDraft.curves.append(group);
	}
	int selected = -1;
	const auto keys = layerKeys(joint, channel);
	for (int i = 0; i < keys.size(); ++i)
		if (keys[i].frame == sample)
			selected = i;
	showLayerCurve(selected);
	refresh();
	m_status->setText(Text::tr("Layer draft keyed. Add Layer or Update Layer applies it to the clip."), LiveStatus::Result);
	return true;
}
bool ModelConstraintsDialog::removeLayerKey(const QString &joint, int channel, int sample)
{
	QString error;
	if (channel == 9)
	{
		auto &keys = m_layerDraft.weightKeys;
		keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const auto &k) { return k.frame == sample; }), keys.end());
	}
	else
	{
		const auto existing = std::find_if(m_layerDraft.curves.begin(), m_layerDraft.curves.end(), [&](const auto &g) { return g.joint == joint; });
		if (existing == m_layerDraft.curves.end())
			return false;
		ModelCurveEditOptions o;
		o.action = ModelCurveAction::RemoveKey;
		o.joint = joint;
		o.channel = channel;
		o.frame = sample;
		if (!editModelCurveGroup(&*existing, frameCount(), o, &error))
		{
			m_status->setText(error, LiveStatus::Problem);
			return false;
		}
	}
	showLayerCurve();
	refresh();
	return true;
}
void ModelConstraintsDialog::showLayerCurve(int selected)
{
	const int channel = std::max(0, m_layerChannel->currentIndex());
	m_layerCurve->setCurve(layerKeys(m_layerJoint->currentData().toString(), channel), std::max(1, frameCount()), channel, selected);
	m_layerCurve->setEnabled(clip() >= 0);
	m_keyValue->setRange(m_layerCurve->minimumValue(), m_layerCurve->maximumValue());
}
ModelClipLayer ModelConstraintsDialog::formLayer() const
{
	auto layer = m_layerDraft;
	layer.name = m_layerName->text();
	layer.additive = m_layerMode->currentIndex() == 0;
	layer.weight = m_layerWeight->value();
	layer.enabled = m_layerEnabled->isChecked();
	return layer;
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
	c.limits = chain && m_limits->isChecked();
	// Unreadable keys are reported by refresh() and refused by choose().
	parseModelWeightKeys(m_weightKeys->text(), frameCount(), &c.weightKeys);
	return c;
}

void ModelConstraintsDialog::refresh()
{
	const int index = clip();
	const auto kind = ModelClipConstraintKind(m_kind->currentData().toInt());
	const bool chain = kind == ModelClipConstraintKind::Ik || kind == ModelClipConstraintKind::FootLock;
	m_root->setEnabled(chain);
	m_limits->setEnabled(chain);
	m_target->setEnabled(kind != ModelClipConstraintKind::FootLock);
	m_axis->setEnabled(kind == ModelClipConstraintKind::Aim);
	const bool selected = m_constraints->currentRow() >= 0, layer = m_layers->currentRow() >= 0;
	m_update->setEnabled(selected);
	m_remove->setEnabled(selected);
	m_updateLayer->setEnabled(layer);
	m_removeLayer->setEnabled(layer);
	m_addLayer->setEnabled(index >= 0 && !m_layerDraft.curves.isEmpty() && !m_layerName->text().isEmpty());
	for (QWidget *widget : std::initializer_list<QWidget *>{m_setKey, m_removeKey, m_keySample, m_keyValue})
		widget->setEnabled(index >= 0);
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
	QVector<ModelPoseCurveKey> keys;
	if (!parseModelWeightKeys(m_weightKeys->text(), frameCount(), &keys, &problem))
		m_status->setText(problem, LiveStatus::Problem);
	else if (!validModelClipStack(m_mesh.skeleton, probe, &problem))
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
			o.layer = formLayer();
		break;
	case ModelClipStackAction::AddLayer:
		o.layer = formLayer();
		break;
	case ModelClipStackAction::RemoveLayer:
		o.index = m_layers->currentRow();
		break;
	case ModelClipStackAction::Bake:
		break;
	}
	// A quick check here; the editor applies (and fully validates) the edit in
	// its worker.
	QString problem;
	QVector<ModelPoseCurveKey> keys;
	if ((action == ModelClipStackAction::AddConstraint || action == ModelClipStackAction::UpdateConstraint) &&
		!parseModelWeightKeys(m_weightKeys->text(), frameCount(), &keys, &problem))
	{
		m_status->setText(problem, LiveStatus::Problem);
		return;
	}
	if (action == ModelClipStackAction::AddConstraint || action == ModelClipStackAction::UpdateConstraint ||
		action == ModelClipStackAction::AddLayer || action == ModelClipStackAction::UpdateLayer)
	{
		auto probe = m_mesh.skeleton.clips[index];
		if (probe.base.isEmpty())
			probe.base = probe.frames;
		if (action == ModelClipStackAction::AddConstraint)
			probe.constraints.append(o.constraint);
		else if (action == ModelClipStackAction::UpdateConstraint && o.index >= 0 && o.index < probe.constraints.size())
			probe.constraints[o.index] = o.constraint;
		else if (action == ModelClipStackAction::AddLayer)
			probe.layers.append(o.layer);
		else if (action == ModelClipStackAction::UpdateLayer && o.index >= 0 && o.index < probe.layers.size())
			probe.layers[o.index] = o.layer;
		if (!validModelClipStack(m_mesh.skeleton, probe, &problem))
		{
			m_status->setText(problem, LiveStatus::Problem);
			return;
		}
	}
	if (action != ModelClipStackAction::AddConstraint && action != ModelClipStackAction::AddLayer && action != ModelClipStackAction::Bake &&
		o.index < 0)
		return;
	m_edit = edit;
	accept();
}
} // namespace vibestudio
