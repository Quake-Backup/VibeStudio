#include "app/model_ik_dialog.h"
#include "app/locale_number.h"
#include "app/dialog_screen_fit.h"
#include "app/model_document_work.h"
#include "app/model_skeletal_working_set_dialog.h"
#include "app/model_viewport.h"
#include "core/model_joint_tools.h"
#include "core/model_skeleton.h"
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>
#include <QVector3D>
#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelIkDialog)
};
void accessible(QWidget *w, const QString &name, const QString &description)
{
	w->setAccessibleName(name);
	w->setAccessibleDescription(description);
	w->setToolTip(description);
}
ModelVec3 sum(ModelVec3 a, ModelVec3 b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}
void row(QFormLayout *form, const QString &text, QWidget *widget)
{
	auto *label = new QLabel(text);
	label->setTextFormat(Qt::PlainText);
	label->setWordWrap(true);
	label->setBuddy(widget);
	form->addRow(label, widget);
}
QString coordinates(ModelVec3 p)
{
	return QStringLiteral("\u2066%1\u2069").arg(localeNumberList({p.x, p.y, p.z}, 9));
}
} // namespace
class ModelIkTable final : public QAbstractTableModel
{
  public:
	explicit ModelIkTable(QObject *parent) : QAbstractTableModel(parent) {}
	QVector<ModelIkSample> samples;
	void setSamples(QVector<ModelIkSample> data)
	{
		beginResetModel();
		samples = std::move(data);
		endResetModel();
	}
	int rowCount(const QModelIndex &parent = {}) const override
	{
		return parent.isValid() ? 0 : int(samples.size());
	}
	int columnCount(const QModelIndex &parent = {}) const override
	{
		return parent.isValid() ? 0 : 5;
	}
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override
	{
		if (role != Qt::DisplayRole)
			return {};
		if (orientation == Qt::Vertical)
			return section + 1;
		return QStringList{Text::tr("Sample"), Text::tr("Target status"), Text::tr("End position"), Text::tr("Target distance"),
						   Text::tr("Tolerance")}
			.value(section);
	}
	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
	{
		if (!index.isValid() || index.row() >= samples.size() ||
			(role != Qt::DisplayRole && role != Qt::AccessibleTextRole && role != Qt::ToolTipRole))
			return {};
		const auto &s = samples[index.row()];
		switch (index.column())
		{
		case 0:
			return s.frame;
		case 1:
			return s.reached ? Text::tr("Reached") : Text::tr("Nearest reachable");
		case 2:
			return coordinates(s.end);
		case 3:
			return localeNumber(s.error, 9);
		case 4:
			return localeNumber(s.tolerance, 9);
		default:
			return {};
		}
	}
};
ModelIkDialog::ModelIkDialog(const ModelMesh &mesh, const QString &joint, int frame, QWidget *parent)
	: QDialog(parent), m_source(mesh), m_result(mesh)
{
	setWindowTitle(Text::tr("Inverse Kinematics"));
	setAccessibleName(windowTitle());
	auto *layout = new QVBoxLayout(this);
	m_tabs = new QTabWidget;
	m_tabs->setObjectName("ikTabs");
	layout->addWidget(m_tabs, 1);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	auto *settings = new QWidget;
	auto *form = new QFormLayout(settings);
	m_form = form;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	scroll->setWidget(settings);
	m_tabs->addTab(scroll, Text::tr("&Chain and target"));
	m_clip = new QComboBox;
	m_clip->setObjectName("ikClip");
	for (int i = 0; i < mesh.skeleton.clips.size(); ++i)
		addModelSkeletalClipItem(m_clip, mesh, i);
	accessible(m_clip, Text::tr("Skeletal clip"),
			   Text::tr("Choose any retained clip, in the working set or not. Sample indices start at zero within this clip."));
	row(form, Text::tr("&Clip"), m_clip);
	m_end = new QComboBox;
	m_end->setObjectName("ikEndJoint");
	for (int i = 0; i < mesh.skeleton.joints.size(); ++i)
	{
		const int parentIndex = mesh.skeleton.joints[i].parent;
		if (parentIndex >= 0 && mesh.skeleton.joints[parentIndex].parent >= 0)
			m_end->addItem(mesh.skeleton.joints[i].name, i);
	}
	accessible(m_end, Text::tr("End joint"),
			   Text::tr("The joint placed at the target. With the two-bone solver its parent and grandparent form the chain; chain "
						"solvers run from the root joint you choose."));
	row(form, Text::tr("&End joint"), m_end);
	m_solver = new QComboBox;
	m_solver->setObjectName("ikSolver");
	m_solver->addItems({Text::tr("Two bones (exact)"), Text::tr("Chain (FABRIK)"), Text::tr("Chain (CCD)")});
	accessible(m_solver, Text::tr("Solver"),
			   Text::tr("Two bones solves an arm or leg exactly. Chain solvers bend any number of joints from the root joint: FABRIK keeps a "
						"smooth bend steered by the pole; CCD turns joints from the end and ignores the pole."));
	row(form, Text::tr("&Solver"), m_solver);
	m_root = new QComboBox;
	m_root->setObjectName("ikRootJoint");
	accessible(m_root, Text::tr("Root joint"), Text::tr("The chain's first joint, two or more bones above the end. It stays in place."));
	row(form, Text::tr("Root &joint"), m_root);
	m_chain = new QLabel;
	m_chain->setWordWrap(true);
	m_chain->setTextFormat(Qt::PlainText);
	m_chain->setObjectName("ikChain");
	accessible(m_chain, Text::tr("Resolved chain"), Text::tr("Root, middle and end joint in hierarchy order."));
	form->addRow(m_chain);
	m_first = new QSpinBox;
	m_first->setObjectName("ikFirst");
	m_last = new QSpinBox;
	m_last->setObjectName("ikLast");
	accessible(m_first, Text::tr("First sample"), Text::tr("First affected clip sample, inclusive."));
	accessible(
		m_last, Text::tr("Last sample"),
		Text::tr("Last affected clip sample, inclusive. Each sample uses its own root and bone lengths with the same model-space target."));
	row(form, Text::tr("&First sample"), m_first);
	row(form, Text::tr("&Last sample"), m_last);
	m_targetSource = new QComboBox;
	m_targetSource->setObjectName("ikTargetSource");
	m_targetSource->addItems({Text::tr("Fixed point"), Text::tr("Follow a joint")});
	accessible(m_targetSource, Text::tr("Target source"),
			   Text::tr("Keep one model-space target for every sample, or follow another joint (a prop, a hand) sample by sample; the target "
						"fields are then an offset in that joint's space."));
	row(form, Text::tr("&Target source"), m_targetSource);
	m_targetJoint = new QComboBox;
	m_targetJoint->setObjectName("ikTargetJoint");
	m_targetJoint->setLayoutDirection(Qt::LeftToRight);
	for (const auto &item : mesh.skeleton.joints)
		m_targetJoint->addItem(item.name, item.name);
	accessible(m_targetJoint, Text::tr("Followed joint"), Text::tr("The joint the end follows. It must not move with the chain itself."));
	row(form, Text::tr("Followed joi&nt"), m_targetJoint);
	const QStringList targetLabels{Text::tr("Target X"), Text::tr("Target Y"), Text::tr("Target Z")};
	const QStringList poleLabels{Text::tr("Pole X"), Text::tr("Pole Y"), Text::tr("Pole Z")};
	for (int i = 0; i < 3; ++i)
	{
		m_target[i] = new QDoubleSpinBox;
		m_pole[i] = new QDoubleSpinBox;
		for (auto *w : {m_target[i], m_pole[i]})
		{
			w->setDecimals(6);
			w->setRange(-1000000, 1000000);
			w->setKeyboardTracking(false);
		}
		m_target[i]->setObjectName(QString("ikTarget%1").arg(i));
		m_pole[i]->setObjectName(QString("ikPole%1").arg(i));
		accessible(m_target[i], targetLabels[i], Text::tr("End-joint target in model-space units."));
		accessible(m_pole[i], poleLabels[i],
				   Text::tr("A point in model space that chooses the elbow or knee bend direction. Keep it off the root-to-target line."));
		row(form, targetLabels[i], m_target[i]);
	}
	for (int i = 0; i < 3; ++i)
		row(form, poleLabels[i], m_pole[i]);
	auto *use = new QPushButton(Text::tr("Use First Sample Pose"));
	use->setObjectName("ikUsePose");
	accessible(use, Text::tr("Use first sample pose"),
			   Text::tr("Set the target to the current end position and the pole to the current bend. A straight chain uses a "
						"perpendicular model axis."));
	form->addRow(use);
	m_targetCursor = new QPushButton(Text::tr("Target at 3D Cursor"));
	m_targetCursor->setObjectName("ikTargetCursor");
	accessible(m_targetCursor, Text::tr("Target at 3D cursor"),
			   Text::tr("Place the target where the editor's 3D cursor is. Place the cursor in the viewport first."));
	form->addRow(m_targetCursor);
	m_poleCursor = new QPushButton(Text::tr("Pole at 3D Cursor"));
	m_poleCursor->setObjectName("ikPoleCursor");
	accessible(m_poleCursor, Text::tr("Pole at 3D cursor"), Text::tr("Place the bend pole where the editor's 3D cursor is."));
	form->addRow(m_poleCursor);
	for (auto *button : {m_targetCursor, m_poleCursor})
		button->setEnabled(false);
	m_reach = new QComboBox;
	m_reach->setObjectName("ikReach");
	m_reach->addItems({Text::tr("Require target"), Text::tr("Nearest reachable")});
	accessible(m_reach, Text::tr("Unreachable targets"),
			   Text::tr("Refuse the entire edit if any target is unreachable, or clamp to the fixed-length chain's reach and report the "
						"remaining distance."));
	row(form, Text::tr("&Reach policy"), m_reach);
	m_orientation = new QComboBox;
	m_orientation->setObjectName("ikOrientation");
	m_orientation->addItems({Text::tr("Keep end orientation"), Text::tr("Follow lower bone")});
	accessible(m_orientation, Text::tr("End orientation"),
			   Text::tr("Keep the hand or foot's original model-space orientation, or rotate it with the lower bone. Its descendants "
						"follow this choice."));
	row(form, Text::tr("End &orientation"), m_orientation);
	m_iterations = new QSpinBox;
	m_iterations->setObjectName("ikIterations");
	m_iterations->setRange(1, 1000);
	m_iterations->setValue(32);
	m_iterations->setLayoutDirection(Qt::LeftToRight);
	accessible(m_iterations, Text::tr("Iterations"), Text::tr("The most solver passes per sample. Each sample stops as soon as the end reaches the target."));
	row(form, Text::tr("&Iterations"), m_iterations);
	m_usePole = new QCheckBox(Text::tr("Steer the bend with the pole"));
	m_usePole->setObjectName("ikUsePole");
	m_usePole->setChecked(true);
	accessible(m_usePole, m_usePole->text(), Text::tr("FABRIK turns the chain's joints towards the pole. Clear it to keep each sample's own bend."));
	form->addRow(m_usePole);
	m_limits = new QCheckBox(Text::tr("Apply joint limits"));
	m_limits->setObjectName("ikLimits");
	accessible(m_limits, m_limits->text(),
			   Text::tr("Keep every joint the chain turns within the limits set in Edit Skeleton > Set joint limits."));
	form->addRow(m_limits);
	auto *previewPage = new QWidget;
	auto *previewLayout = new QVBoxLayout(previewPage);
	auto *previewForm = new QFormLayout;
	previewForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	previewLayout->addLayout(previewForm);
	m_sample = new QSpinBox;
	m_sample->setObjectName("ikPreviewSample");
	accessible(m_sample, Text::tr("Preview sample"), Text::tr("Inspect any sample in the selected clip, including unaffected samples."));
	row(previewForm, Text::tr("Preview &sample"), m_sample);
	m_view = new QComboBox;
	m_view->setObjectName("ikPreviewSource");
	m_view->addItems({Text::tr("Reviewed result"), Text::tr("Original model")});
	accessible(m_view, Text::tr("Preview source"), Text::tr("Compare before and after using the same sample and camera."));
	row(previewForm, Text::tr("Preview &source"), m_view);
	m_handle = new QComboBox;
	m_handle->setObjectName("ikHandle");
	m_handle->addItems({Text::tr("None"), Text::tr("Target"), Text::tr("Pole")});
	m_handle->setCurrentIndex(1);
	accessible(m_handle, Text::tr("Drag handle"),
			   Text::tr("Show the move handle on the target or the pole in the preview. Drag an arrow to move along that axis, or the "
						"centre to move in the view plane; the point fields follow. The fields stay the keyboard route."));
	row(previewForm, Text::tr("Drag &handle"), m_handle);
	m_preview = new ModelViewport;
	m_preview->setObjectName("ikPreview");
	m_preview->setMinimumSize(240, 220);
	m_preview->setShowJoints(true);
	m_preview->setXrayJoints(true);
	previewLayout->addWidget(m_preview, 1);
	// Target and pole markers: a ring and a diamond, each labelled, so the
	// shapes and words tell them apart without colour.
	m_preview->setOverlayPainter(
		[this](QPainter &painter)
		{
			const auto mark = [&](const ModelVec3 &point, bool diamond, const QString &label)
			{
				QPointF screen;
				if (!m_preview->projectToView(point, &screen))
					return;
				painter.setPen(QPen(palette().highlight().color(), 2));
				painter.setBrush(Qt::NoBrush);
				if (diamond)
					painter.drawPolygon(QPolygonF{screen + QPointF(0, -7), screen + QPointF(7, 0), screen + QPointF(0, 7), screen + QPointF(-7, 0)});
				else
					painter.drawEllipse(screen, 6, 6);
				painter.setPen(palette().text().color());
				painter.drawText(screen + QPointF(10, -8), label);
			};
			mark(targetPoint(), false, Text::tr("Target"));
			mark(polePoint(), true, Text::tr("Pole"));
		});
	m_tabs->addTab(previewPage, Text::tr("&Preview"));
	auto *results = new QWidget;
	auto *resultLayout = new QVBoxLayout(results);
	auto *table = new QTableView;
	table->setObjectName("ikSamples");
	m_table = new ModelIkTable(this);
	table->setModel(m_table);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	accessible(table, Text::tr("Solved samples"),
			   Text::tr("Per-sample end positions, target distance and numerical tolerance. Select a row for full details."));
	resultLayout->addWidget(table, 1);
	auto *detail = new QPlainTextEdit;
	detail->setObjectName("ikSampleDetail");
	detail->setReadOnly(true);
	detail->setMaximumHeight(190);
	accessible(detail, Text::tr("Selected sample result"), Text::tr("Full values for the selected sample."));
	resultLayout->addWidget(detail);
	m_tabs->addTab(results, Text::tr("&Results"));
	connect(table->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
			[this, detail](const QModelIndex &index)
			{
				QStringList lines;
				if (index.isValid())
					for (int c = 0; c < 5; ++c)
						lines << Text::tr("%1: %2").arg(m_table->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString(),
														m_table->data(m_table->index(index.row(), c)).toString());
				detail->setPlainText(lines.join('\n'));
			});
	connect(m_table, &QAbstractItemModel::modelReset, detail, &QPlainTextEdit::clear);
	m_status = new LiveStatusLabel;
	m_status->setObjectName("ikStatus");
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	accessible(m_status, Text::tr("Inverse kinematics status"), Text::tr("Review progress, validation failures and target reach summary."));
	layout->addWidget(m_status);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
	layout->addWidget(buttons);
	m_apply = buttons->button(QDialogButtonBox::Apply);
	m_apply->setObjectName("ikApply");
	m_review = buttons->addButton(Text::tr("Review"), QDialogButtonBox::ActionRole);
	m_review->setObjectName("ikReview");
	accessible(m_review, Text::tr("Review inverse kinematics"),
			   Text::tr("Prepare and validate the pose change in a cancellable worker before applying."));
	ModelJointFrame current;
	if (modelJointFrame(mesh, frame, &current))
	{
		m_clip->setCurrentIndex(std::max(0, m_clip->findData(current.clip)));
	}
	const int named = m_end->findText(joint);
	if (named >= 0)
		m_end->setCurrentIndex(named);
	endChanged();
	clipChanged();
	m_first->setValue(current.local);
	m_last->setValue(current.local);
	m_sample->setValue(current.local);
	usePose();
	connect(m_clip, &QComboBox::currentIndexChanged, this,
			[this]
			{
				clipChanged();
				changed();
			});
	connect(m_end, &QComboBox::currentIndexChanged, this, [this] { endChanged(); });
	for (auto *w : {m_end, m_reach, m_orientation, m_solver, m_root, m_targetSource, m_targetJoint})
		connect(w, &QComboBox::currentIndexChanged, this, [this] { changed(); });
	connect(m_iterations, &QSpinBox::valueChanged, this, [this] { changed(); });
	for (auto *box : {m_usePole, m_limits})
		connect(box, &QCheckBox::toggled, this, [this] { changed(); });
	connect(m_targetCursor, &QPushButton::clicked, this,
			[this]
			{
				const float point[3]{m_cursor.x, m_cursor.y, m_cursor.z};
				m_targetSource->setCurrentIndex(0);
				for (int i = 0; i < 3; ++i)
					m_target[i]->setValue(point[i]);
			});
	connect(m_poleCursor, &QPushButton::clicked, this,
			[this]
			{
				const float point[3]{m_cursor.x, m_cursor.y, m_cursor.z};
				for (int i = 0; i < 3; ++i)
					m_pole[i]->setValue(point[i]);
			});
	for (auto *w : {m_first, m_last})
		connect(w, &QSpinBox::valueChanged, this, [this] { changed(); });
	for (int i = 0; i < 3; ++i)
		for (auto *w : {m_target[i], m_pole[i]})
			connect(w, &QDoubleSpinBox::valueChanged, this, [this] { changed(); });
	connect(use, &QPushButton::clicked, this,
			[this]
			{
				usePose();
				changed();
			});
	connect(m_sample, &QSpinBox::valueChanged, this, [this] { showPreview(); });
	connect(m_view, &QComboBox::currentIndexChanged, this, [this] { showPreview(); });
	connect(m_handle, &QComboBox::currentIndexChanged, this, [this] { updateHandle(); });
	connect(m_preview, &ModelViewport::editTransformRequested, this,
			[this](const ModelTransform &transform) { dropHandle(sum(transform.pivot, modelBasisToWorld(transform.translation, transform.basis))); });
	connect(m_preview, &ModelViewport::editTransformPreviewChanged, this,
			[this]
			{
				if (!m_preview->editingMove() || m_handle->currentIndex() == 0)
					return;
				const auto transform = m_preview->editTransform();
				const auto point = sum(transform.pivot, modelBasisToWorld(transform.translation, transform.basis));
				m_status->setText((m_handle->currentIndex() == 1 ? Text::tr("Target at %1, %2, %3.") : Text::tr("Pole at %1, %2, %3."))
									  .arg(localeNumber(point.x, 4), localeNumber(point.y, 4), localeNumber(point.z, 4)));
			});
	connect(m_review, &QPushButton::clicked, this, &ModelIkDialog::review);
	connect(m_apply, &QPushButton::clicked, this,
			[this]
			{
				if (m_ready && !m_busy)
					accept();
			});
	connect(buttons, &QDialogButtonBox::rejected, this, &ModelIkDialog::reject);
	fitDialogToScreen(this, QSize(940, 800));
	changed();
}
ModelEdit ModelIkDialog::request() const
{
	ModelEdit edit;
	const bool chain = m_solver->currentIndex() > 0;
	edit.kind = chain ? ModelEditKind::SolveChainIk : ModelEditKind::SolveTwoBoneIk;
	auto &o = edit.ik;
	o.clip = m_clip->currentIndex() < 0 ? -1 : m_clip->currentData().toInt();
	o.endJoint = m_end->currentText();
	o.first = m_first->value();
	o.last = m_last->value();
	const ModelVec3 point{float(m_target[0]->value()), float(m_target[1]->value()), float(m_target[2]->value())};
	if (m_targetSource->currentIndex() == 1)
	{
		o.targetJoint = m_targetJoint->currentData().toString();
		o.targetOffset = point;
	}
	else
		o.target = point;
	o.pole = {float(m_pole[0]->value()), float(m_pole[1]->value()), float(m_pole[2]->value())};
	o.nearestReachable = m_reach->currentIndex() == 1;
	o.keepEndOrientation = m_orientation->currentIndex() == 0;
	if (chain)
	{
		o.rootJoint = m_root->currentData().toString();
		o.solver = m_solver->currentIndex() == 2 ? ModelIkSolver::Ccd : ModelIkSolver::Fabrik;
		o.iterations = m_iterations->value();
		o.usePole = m_usePole->isChecked() && o.solver == ModelIkSolver::Fabrik;
		o.limits = m_limits->isChecked();
	}
	return edit;
}
void ModelIkDialog::clipChanged()
{
	const int clip = m_clip->currentIndex() < 0 ? -1 : m_clip->currentData().toInt();
	const int count = clip < 0 ? 1 : int(m_source.skeleton.clips[clip].frames.size());
	for (auto *w : {m_first, m_last, m_sample})
	{
		QSignalBlocker blocker(w);
		w->setRange(0, std::max(0, count - 1));
		w->setValue(0);
	}
}
ModelVec3 ModelIkDialog::targetPoint() const
{
	const ModelVec3 point{float(m_target[0]->value()), float(m_target[1]->value()), float(m_target[2]->value())};
	ModelJointMatrix pose;
	if (m_targetSource->currentIndex() == 1 && m_preview->jointPose(m_targetJoint->currentData().toString(), &pose))
		return modelJointTransformPoint(pose, point);
	return point;
}
ModelVec3 ModelIkDialog::polePoint() const
{
	return {float(m_pole[0]->value()), float(m_pole[1]->value()), float(m_pole[2]->value())};
}
void ModelIkDialog::setHandle(int handle)
{
	m_handle->setCurrentIndex(std::clamp(handle, 0, 2));
}
void ModelIkDialog::updateHandle()
{
	const int handle = m_handle->currentIndex();
	m_preview->setTransformGizmo(handle > 0 && !m_busy, ModelTransformTool::Move);
	if (handle > 0)
		m_preview->setTransformPivot(ModelTransformPivot::Custom, handle == 1 ? targetPoint() : polePoint());
	m_preview->update();
}
void ModelIkDialog::dropHandle(const ModelVec3 &point)
{
	const int handle = m_handle->currentIndex();
	if (handle == 0 || m_busy)
		return;
	auto value = point;
	ModelJointMatrix pose;
	if (handle == 1 && m_targetSource->currentIndex() == 1)
	{
		// A followed joint's target is an offset in its space.
		bool invertible = false;
		const auto inverse = m_preview->jointPose(m_targetJoint->currentData().toString(), &pose) ? modelJointInverse(pose, &invertible)
																							  : modelJointIdentity();
		if (!invertible)
			return;
		value = modelJointTransformPoint(inverse, point);
	}
	auto **fields = handle == 1 ? m_target : m_pole;
	const float values[3]{value.x, value.y, value.z};
	for (int i = 0; i < 3; ++i)
	{
		const QSignalBlocker blocker(fields[i]);
		fields[i]->setValue(values[i]);
	}
	changed();
	m_status->setText((handle == 1 ? Text::tr("Target moved to %1, %2, %3. Choose Review to solve.") : Text::tr("Pole moved to %1, %2, %3. Choose Review to solve."))
						  .arg(localeNumber(point.x, 4), localeNumber(point.y, 4), localeNumber(point.z, 4)));
}
void ModelIkDialog::setCursorPoint(const ModelVec3 &point)
{
	m_cursor = point;
	m_hasCursor = true;
	for (auto *button : {m_targetCursor, m_poleCursor})
		button->setEnabled(true);
}
void ModelIkDialog::endChanged()
{
	const auto previous = m_root->currentData().toString();
	QSignalBlocker blocker(m_root);
	m_root->clear();
	if (m_end->currentIndex() >= 0)
	{
		const auto &joints = m_source.skeleton.joints;
		int depth = 0;
		for (int j = m_end->currentData().toInt(); j >= 0; j = joints[j].parent, ++depth)
			if (depth >= 2)
				m_root->addItem(joints[j].name, joints[j].name);
	}
	m_root->setCurrentIndex(std::max(0, m_root->findData(previous)));
}
void ModelIkDialog::usePose()
{
	const auto o = request().ik;
	if (o.clip < 0 || m_end->currentIndex() < 0)
		return;
	const int end = m_end->currentData().toInt(), middle = m_source.skeleton.joints[end].parent,
			  root = m_source.skeleton.joints[middle].parent;
	const auto &pose = m_source.skeleton.clips[o.clip].frames[o.first];
	const auto convert = [](ModelVec3 p) { return QVector3D(p.x, p.y, p.z); };
	const auto a = convert(modelJointTranslation(pose[root])), b = convert(modelJointTranslation(pose[middle])),
			   c = convert(modelJointTranslation(pose[end]));
	auto pole = b;
	const auto direction = (c - a).normalized();
	if (QVector3D::crossProduct(b - a, direction).length() < 0.00001f)
	{
		const QVector3D basis = std::abs(direction.z()) < 0.8f ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0);
		pole = b + (basis - direction * QVector3D::dotProduct(basis, direction)).normalized() * std::max(1.0f, (b - a).length());
	}
	for (int i = 0; i < 3; ++i)
	{
		QSignalBlocker blockTarget(m_target[i]), blockPole(m_pole[i]);
		m_target[i]->setValue(c[i]);
		m_pole[i]->setValue(pole[i]);
	}
}
void ModelIkDialog::changed()
{
	if (m_busy)
		return;
	m_ready = false;
	m_apply->setEnabled(false);
	m_result = m_source;
	m_table->setSamples({});
	QStringList chain;
	const bool chained = m_solver->currentIndex() > 0, following = m_targetSource->currentIndex() == 1;
	const auto root = m_root->currentData().toString();
	if (m_end->currentIndex() >= 0)
		for (int j = m_end->currentData().toInt(), n = 0; (chained || n < 3) && j >= 0; ++n, j = m_source.skeleton.joints[j].parent)
		{
			chain.prepend(QStringLiteral("\u2068%1\u2069").arg(m_source.skeleton.joints[j].name));
			if (chained && m_source.skeleton.joints[j].name == root)
				break;
		}
	m_chain->setText(chain.join(QStringLiteral(" → ")));
	m_form->setRowVisible(m_root, chained);
	m_form->setRowVisible(m_iterations, chained);
	m_form->setRowVisible(m_usePole, chained);
	m_form->setRowVisible(m_limits, chained);
	m_form->setRowVisible(m_targetJoint, following);
	m_usePole->setEnabled(m_solver->currentIndex() == 1);
	const QStringList axes{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
	for (int i = 0; i < 3; ++i)
		accessible(m_target[i], following ? Text::tr("Target offset %1").arg(axes[i]) : Text::tr("Target %1").arg(axes[i]),
				   following ? Text::tr("Offset from the followed joint, in its own space.") : Text::tr("End-joint target in model-space units."));
	m_review->setEnabled(m_clip->count() > 0 && m_end->count() > 0);
	m_status->setText(m_review->isEnabled() ? Text::tr("Choose Review to inspect the solved pose and target distance.")
											: Text::tr("A skeletal clip and a three-joint chain are required."));
	showPreview();
}
void ModelIkDialog::showPreview()
{
	// Clips outside the working set preview one skinned sample at a time.
	const int clip = m_clip->currentIndex() < 0 ? -1 : m_clip->currentData().toInt();
	int frame = -1;
	m_preview->setMesh(modelSkeletalSamplePreview(m_ready && m_view->currentIndex() == 0 ? m_result : m_source, clip, m_sample->value(), &frame), true);
	m_preview->setFrame(std::max(0, frame));
	m_preview->setEditJoint(m_end->currentText());
	updateHandle();
}
void ModelIkDialog::review()
{
	if (m_busy || m_closeAfter)
		return;
	const auto operation = request();
	const auto source = m_source;
	ModelMesh candidate;
	QVector<ModelIkSample> samples;
	QString error;
	m_ready = false;
	m_busy = true;
	m_tabs->setEnabled(false);
	m_apply->setEnabled(false);
	m_review->setEnabled(false);
	m_table->setSamples({});
	m_status->setText(Text::tr("Solving and validating inverse kinematics…"));
	const bool ok = runModelTask(
		this, Text::tr("Review Inverse Kinematics"),
		[source, operation, &candidate, &samples](QString *failure, const ModelWorkControl &control)
		{
			candidate = source;
			if (!applyModelEdit(&candidate, operation, nullptr, failure, control))
				return false;
			samples = modelIkSamples(candidate, operation.ik, operation.kind == ModelEditKind::SolveChainIk);
			return modelWorkCheckpoint(control, ModelWorkPhase::Validating, 0, 0, failure);
		},
		&error, false, &m_cancel);
	m_busy = false;
	m_tabs->setEnabled(true);
	m_review->setEnabled(true);
	if (m_closeAfter)
	{
		QDialog::reject();
		return;
	}
	if (!ok)
	{
		m_result = m_source;
		showPreview();
		m_status->setText(error, LiveStatus::Problem);
		return;
	}
	m_result = std::move(candidate);
	m_reviewed = operation;
	m_ready = true;
	m_apply->setEnabled(true);
	int reached = 0;
	double residual = 0;
	for (const auto &s : samples)
	{
		reached += s.reached;
		residual = std::max(residual, s.error);
	}
	m_status->setText(Text::tr("Samples reviewed: %L1 · targets reached: %L2. Largest remaining distance: %3 model units. Ready to apply.")
						  .arg(samples.size())
						  .arg(reached)
						  .arg(localeNumber(residual, 9)),
					  LiveStatus::Result);
	m_table->setSamples(std::move(samples));
	m_sample->setValue(operation.ik.first);
	showPreview();
}
void ModelIkDialog::reject()
{
	m_closeAfter = true;
	if (m_busy)
	{
		if (m_cancel)
			m_cancel();
		return;
	}
	QDialog::reject();
}
} // namespace vibestudio
