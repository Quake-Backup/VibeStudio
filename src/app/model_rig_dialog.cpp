#include "app/model_rig_dialog.h"
#include "app/locale_number.h"
#include "app/dialog_screen_fit.h"
#include "core/model_skeleton.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelRigDialog)
};
void accessible(QWidget *widget, const QString &name, const QString &description)
{
	widget->setAccessibleName(name);
	widget->setAccessibleDescription(description);
	widget->setToolTip(description);
}
} // namespace
ModelRigDialog::ModelRigDialog(const ModelMesh &mesh, const ModelSelection &selection, const QString &joint, QWidget *parent)
	: QDialog(parent), m_mesh(mesh), m_selection(selection)
{
	setObjectName("modelRigDialog");
	setWindowTitle(Text::tr("Edit Skeleton"));
	accessible(this, windowTitle(),
			   Text::tr("Edit joint hierarchy or assign weights to the selected "
						"mesh components as one undoable operation."));
	auto *layout = new QVBoxLayout(this);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	auto *content = new QWidget;
	m_form = new QFormLayout(content);
	m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	scroll->setWidget(content);
	layout->addWidget(scroll);
	m_operation = new QComboBox;
	m_operation->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_operation->setMinimumContentsLength(16);
	m_operation->setObjectName("rigOperation");
	accessible(m_operation, Text::tr("Skeleton operation"),
			   Text::tr("Choose a joint or weight operation. Apply returns to "
						"the editor for validation."));
	m_operation->addItem(Text::tr("Add joint"), int(ModelEditKind::AddJoint));
	m_operation->addItem(Text::tr("Rename joint"), int(ModelEditKind::RenameJoint));
	m_operation->addItem(Text::tr("Reparent joint"), int(ModelEditKind::ReparentJoint));
	m_operation->addItem(Text::tr("Delete unused joint"), int(ModelEditKind::DeleteJoint));
	m_operation->addItem(Text::tr("Assign vertex weight"), int(ModelEditKind::SetJointWeight));
	m_operation->addItem(Text::tr("Set joint limits"), int(ModelEditKind::SetJointLimits));
	m_form->addRow(Text::tr("&Operation"), m_operation);
	const auto combo = [&](const char *name, const QString &label, const QString &description) {
		auto *box = new QComboBox;
		box->setObjectName(name);
		box->setLayoutDirection(Qt::LeftToRight);
		box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		box->setMinimumContentsLength(16);
		accessible(box, label, description);
		m_form->addRow(label, box);
		return box;
	};
	m_joint = combo("rigJoint", Text::tr("&Joint"), Text::tr("The existing joint to edit. Names are matched exactly."));
	m_parent = combo("rigParent", Text::tr("&Parent"),
					 Text::tr("The parent joint, or no parent for a root. "
							  "Reparenting preserves every model-space pose."));
	m_parent->addItem(Text::tr("No parent"), QString());
	for (const auto &item : mesh.skeleton.joints)
	{
		m_joint->addItem(item.name, item.name);
		m_parent->addItem(item.name, item.name);
	}
	m_joint->setCurrentIndex(std::max(0, m_joint->findData(joint)));
	m_name = new QLineEdit;
	m_name->setObjectName("rigName");
	m_name->setLayoutDirection(Qt::LeftToRight);
	m_name->setMaxLength(63);
	accessible(m_name, Text::tr("Joint name"),
			   Text::tr("A unique name using ASCII letters, digits, underscores, "
						"dots or hyphens. Start with a letter or underscore."));
	m_form->addRow(Text::tr("&Name"), m_name);
	m_origin = new QWidget;
	auto *originLayout = new QVBoxLayout(m_origin);
	originLayout->setContentsMargins(0, 0, 0, 0);
	for (int axis = 0; axis < 3; ++axis)
	{
		auto *spin = new QDoubleSpinBox;
		spin->setObjectName(QStringLiteral("rigOrigin%1").arg(axis));
		spin->setRange(-1000000, 1000000);
		spin->setDecimals(4);
		const auto label = QString(QChar('X' + axis));
		spin->setPrefix(label + QLatin1Char(' '));
		accessible(spin, Text::tr("Joint origin %1").arg(label), Text::tr("Model-space bind origin for the new joint, in model units."));
		originLayout->addWidget(spin);
		m_axes[axis] = spin;
	}
	m_form->addRow(Text::tr("Origin"), m_origin);
	m_weight = new QDoubleSpinBox;
	m_weight->setObjectName("rigWeight");
	m_weight->setRange(0, 1);
	m_weight->setDecimals(6);
	m_weight->setSingleStep(0.05);
	m_weight->setValue(1);
	accessible(m_weight, Text::tr("Joint weight"),
			   Text::tr("The selected joint's weight on each selected vertex. Other "
						"influences share the remainder proportionally. Zero removes "
						"this influence; one assigns the vertex entirely."));
	m_form->addRow(Text::tr("&Weight"), m_weight);
	m_limitsEnabled = new QComboBox;
	m_limitsEnabled->setObjectName("rigLimitsEnabled");
	m_limitsEnabled->addItem(Text::tr("Limit rotation"), true);
	m_limitsEnabled->addItem(Text::tr("No limits"), false);
	accessible(m_limitsEnabled, Text::tr("Joint limits"),
			   Text::tr("Chain inverse kinematics keeps a limited joint's rotation, relative to its bind pose, within these angles."));
	m_form->addRow(Text::tr("&Limits"), m_limitsEnabled);
	m_limitMode = new QComboBox;
	m_limitMode->setObjectName("rigLimitMode");
	m_limitMode->addItems({Text::tr("Euler angles"), Text::tr("Swing and twist")});
	accessible(m_limitMode, Text::tr("Limit type"),
			   Text::tr("Euler angles bound each axis, applied X then Y then Z. Swing and twist bound the twist about the bone's own axis "
						"and keep the swing inside the ellipse the other two axes' angles span, as shoulders and hips need."));
	m_form->addRow(Text::tr("Limit &type"), m_limitMode);
	m_twistAxis = new QComboBox;
	m_twistAxis->setObjectName("rigTwistAxis");
	m_twistAxis->setLayoutDirection(Qt::LeftToRight);
	m_twistAxis->addItems({QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")});
	accessible(m_twistAxis, Text::tr("Twist axis"),
			   Text::tr("The joint's own axis, in its bind pose, that the bone points along; the twist turns about it."));
	m_form->addRow(Text::tr("T&wist axis"), m_twistAxis);
	m_limits = new QWidget;
	auto *limitsLayout = new QGridLayout(m_limits);
	limitsLayout->setContentsMargins(0, 0, 0, 0);
	const QStringList bounds{Text::tr("Minimum"), Text::tr("Maximum")};
	for (int bound = 0; bound < 2; ++bound)
		for (int axis = 0; axis < 3; ++axis)
		{
			auto *spin = new QDoubleSpinBox;
			spin->setObjectName(QStringLiteral("rigLimit%1%2").arg(bound ? QStringLiteral("Max") : QStringLiteral("Min")).arg(axis));
			spin->setRange(-180, 180);
			spin->setDecimals(2);
			spin->setValue(bound ? 180 : -180);
			spin->setLayoutDirection(Qt::LeftToRight);
			const auto label = QString(QChar('X' + axis));
			spin->setPrefix(label + QLatin1Char(' '));
			spin->setSuffix(QStringLiteral("°"));
			accessible(spin, Text::tr("%1 %2 angle").arg(bounds[bound], label),
					   Text::tr("Degrees about %1, applied X then Y then Z, relative to the joint's bind pose.").arg(label));
			limitsLayout->addWidget(spin, bound, axis);
			m_limitAxes[bound * 3 + axis] = spin;
		}
	m_form->addRow(Text::tr("Angles"), m_limits);
	m_summary = new QLabel;
	m_summary->setObjectName("rigSummary");
	m_summary->setTextFormat(Qt::PlainText);
	m_summary->setWordWrap(true);
	m_summary->setMinimumWidth(0);
	m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_summary->setAccessibleName(Text::tr("Skeleton edit scope"));
	m_form->addRow(m_summary);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Apply)->setObjectName("applyRigEdit");
	accessible(buttons->button(QDialogButtonBox::Apply), Text::tr("Apply skeleton edit"),
			   Text::tr("Validate and apply one undoable edit through the model worker. "
						"Invalid or cancelled operations leave the model unchanged."));
	connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	connect(m_operation, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
	connect(m_joint, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
	connect(m_limitsEnabled, &QComboBox::currentIndexChanged, this,
			[this]
			{
				const bool enabled = m_limitsEnabled->currentData().toBool();
				m_limits->setEnabled(enabled);
				m_limitMode->setEnabled(enabled);
				m_twistAxis->setEnabled(enabled && m_limitMode->currentIndex() == 1);
			});
	connect(m_limitMode, &QComboBox::currentIndexChanged, this,
			[this] { m_twistAxis->setEnabled(m_limitsEnabled->currentData().toBool() && m_limitMode->currentIndex() == 1); });
	const auto updateApply = [this, apply = buttons->button(QDialogButtonBox::Apply)] {
		const auto kind = ModelEditKind(m_operation->currentData().toInt());
		const bool add = kind == ModelEditKind::AddJoint;
		const bool named = (kind != ModelEditKind::RenameJoint && !add) || !m_name->text().isEmpty();
		const bool selected = kind != ModelEditKind::SetJointWeight ||
			(!m_selection.vertices.isEmpty() || !m_selection.edges.isEmpty() || !m_selection.faces.isEmpty());
		apply->setEnabled(named && selected && (add || m_joint->currentIndex() >= 0));
	};
	connect(m_operation, &QComboBox::currentIndexChanged, this, updateApply);
	connect(m_joint, &QComboBox::currentIndexChanged, this, updateApply);
	connect(m_name, &QLineEdit::textChanged, this, updateApply);
	refresh();
	updateApply();
	fitDialogToScreen(this, QSize(std::max(620, fontMetrics().averageCharWidth() * 65), 500));
}
ModelEdit ModelRigDialog::edit() const
{
	ModelEdit result;
	result.selection = m_selection;
	result.kind = ModelEditKind(m_operation->currentData().toInt());
	if (result.kind != ModelEditKind::AddJoint)
		result.rig.joint = m_joint->currentData().toString();
	if (result.kind == ModelEditKind::AddJoint || result.kind == ModelEditKind::RenameJoint)
		result.rig.name = m_name->text();
	if (result.kind == ModelEditKind::AddJoint || result.kind == ModelEditKind::ReparentJoint)
		result.rig.parent = m_parent->currentData().toString();
	if (result.kind == ModelEditKind::AddJoint)
		result.rig.origin = {float(m_axes[0]->value()), float(m_axes[1]->value()), float(m_axes[2]->value())};
	if (result.kind == ModelEditKind::SetJointLimits)
	{
		auto &o = result.animation.limits;
		o.joint = result.rig.joint;
		result.rig.joint.clear();
		o.limits.enabled = m_limitsEnabled->currentData().toBool();
		if (o.limits.enabled)
		{
			for (int axis = 0; axis < 3; ++axis)
			{
				o.limits.minimum[axis] = m_limitAxes[axis]->value();
				o.limits.maximum[axis] = m_limitAxes[3 + axis]->value();
			}
			o.limits.swingTwist = m_limitMode->currentIndex() == 1;
			o.limits.twistAxis = o.limits.swingTwist ? m_twistAxis->currentIndex() : 0;
		}
	}
	if (result.kind == ModelEditKind::SetJointWeight)
		result.rig.weight = m_weight->value();
	return result;
}
void ModelRigDialog::refresh()
{
	const auto kind = ModelEditKind(m_operation->currentData().toInt());
	const bool add = kind == ModelEditKind::AddJoint, weight = kind == ModelEditKind::SetJointWeight;
	m_form->setRowVisible(m_joint, !add);
	m_form->setRowVisible(m_name, add || kind == ModelEditKind::RenameJoint);
	m_form->setRowVisible(m_parent, add || kind == ModelEditKind::ReparentJoint);
	m_form->setRowVisible(m_origin, add);
	m_form->setRowVisible(m_weight, weight);
	const bool limits = kind == ModelEditKind::SetJointLimits;
	m_form->setRowVisible(m_limitsEnabled, limits);
	m_form->setRowVisible(m_limitMode, limits);
	m_form->setRowVisible(m_twistAxis, limits);
	m_form->setRowVisible(m_limits, limits);
	m_limits->setEnabled(m_limitsEnabled->currentData().toBool());
	if (kind == ModelEditKind::RenameJoint)
		m_name->setText(m_joint->currentData().toString());
	QString summary;
	if (limits)
	{
		const int joint = modelJointIndex(m_mesh.skeleton, m_joint->currentData().toString(), Qt::CaseSensitive);
		if (joint >= 0)
		{
			// A joint without limits starts from the full range, ready to narrow.
			const auto &current = m_mesh.skeleton.joints[joint].limits;
			const QSignalBlocker blocker(m_limitsEnabled);
			m_limitsEnabled->setCurrentIndex(0);
			for (int axis = 0; axis < 3; ++axis)
			{
				m_limitAxes[axis]->setValue(current.minimum[axis]);
				m_limitAxes[3 + axis]->setValue(current.maximum[axis]);
			}
			m_limitMode->setCurrentIndex(current.swingTwist ? 1 : 0);
			m_twistAxis->setCurrentIndex(current.twistAxis);
			m_limits->setEnabled(true);
			m_limitMode->setEnabled(true);
			m_twistAxis->setEnabled(current.swingTwist);
		}
		summary = Text::tr("Limits bound the joint's rotation relative to its bind pose when chain inverse kinematics solves with "
						   "Apply joint limits. They do not change existing poses.");
	}
	else if (add)
		summary = m_mesh.skeleton.isEmpty() ? Text::tr("The first joint binds every vertex of a single-frame "
													   "model. Existing vertex animation requires a separate "
													   "static source.")
											: Text::tr("A new child follows its parent in every skeletal clip. "
													   "Existing geometry and weights stay unchanged.");
	else if (kind == ModelEditKind::ReparentJoint)
		summary = Text::tr("Reparenting retains bind and animation poses. Joint indices may "
						   "change; influences and attachments follow the renamed indices.");
	else if (kind == ModelEditKind::DeleteJoint)
		summary = Text::tr("Only an unused joint without children or attachments "
						   "can be deleted. At least one joint must remain.");
	else if (weight)
	{
		summary = Text::tr("Surface %1 · vertices: %2 · edges: %3 · faces: %4. "
						   "Skeletal poses update; independent edits are protected.")
					  .arg(m_selection.surface)
					  .arg(m_selection.vertices.size())
					  .arg(m_selection.edges.size())
					  .arg(m_selection.faces.size());
		const int joint = modelJointIndex(m_mesh.skeleton, m_joint->currentData().toString(), Qt::CaseSensitive);
		if (joint >= 0 && !m_selection.vertices.isEmpty())
		{
			const auto &skinning = m_mesh.surfaces[m_selection.surface].skinning;
			double minimum = 1, maximum = 0;
			for (int vertex : m_selection.vertices)
			{
				double value = 0;
				if (!skinning.isEmpty())
					for (int i = skinning.first[vertex]; i < skinning.first[vertex] + skinning.count[vertex]; ++i)
						if (skinning.influences[i].joint == joint)
							value += skinning.influences[i].weight;
				minimum = std::min(minimum, value);
				maximum = std::max(maximum, value);
			}
			summary += QLatin1Char(' ') + Text::tr("Current vertex weights: %1–%2.").arg(localeNumber(minimum, 6)).arg(localeNumber(maximum, 6));
		}
	}
	else
		summary = Text::tr("Renaming preserves hierarchy, influences and every pose. Update any "
						   "external scripts that refer to the old joint name.");
	m_summary->setText(summary);
}
} // namespace vibestudio
