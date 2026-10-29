#include "app/model_clip_import_dialog.h"
#include "app/dialog_screen_fit.h"
#include "app/locale_number.h"
#include "app/model_document_work.h"
#include "app/model_skeletal_working_set_dialog.h"
#include "app/model_viewport.h"
#include "core/model_clip_import.h"
#include "core/model_skeleton.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelClipImportDialog)
};
void accessible(QWidget *widget, const QString &name, const QString &description)
{
	widget->setAccessibleName(name);
	widget->setAccessibleDescription(description);
	widget->setToolTip(description);
}
} // namespace

ModelClipImportDialog::ModelClipImportDialog(const ModelMesh &mesh, QWidget *parent) : QDialog(parent), m_mesh(mesh)
{
	setObjectName("modelClipImportDialog");
	setWindowTitle(Text::tr("Import Animation"));
	accessible(this, windowTitle(), Text::tr("Add a clip from another file to this rig, exactly or retargeted, as one undoable edit."));
	auto *layout = new QVBoxLayout(this);
	auto *form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	layout->addLayout(form);
	auto *pathRow = new QWidget;
	auto *pathLayout = new QHBoxLayout(pathRow);
	pathLayout->setContentsMargins(0, 0, 0, 0);
	m_path = new QLineEdit;
	m_path->setObjectName("clipImportPath");
	m_path->setReadOnly(true);
	m_path->setLayoutDirection(Qt::LeftToRight);
	accessible(m_path, Text::tr("Source file"), Text::tr("The file the clip comes from."));
	m_choose = new QPushButton(Text::tr("Choose File…"));
	m_choose->setObjectName("clipImportChoose");
	accessible(m_choose, Text::tr("Choose source file"),
			   Text::tr("Pick an md5anim, IQM, MDS, MDR, MDX or GLA file, or another editable source, to read its clips."));
	pathLayout->addWidget(m_path, 1);
	pathLayout->addWidget(m_choose);
	form->addRow(Text::tr("&Source"), pathRow);
	m_clip = new QComboBox;
	m_clip->setObjectName("clipImportClip");
	m_clip->setLayoutDirection(Qt::LeftToRight);
	accessible(m_clip, Text::tr("Source clip"), Text::tr("The clip of the source file to import."));
	form->addRow(Text::tr("&Clip"), m_clip);
	m_name = new QLineEdit;
	m_name->setObjectName("clipImportName");
	m_name->setMaxLength(63);
	m_name->setLayoutDirection(Qt::LeftToRight);
	accessible(m_name, Text::tr("New clip name"),
			   Text::tr("A unique name using ASCII letters, digits, underscores, dots or hyphens, starting with a letter or underscore."));
	form->addRow(Text::tr("&Name"), m_name);
	m_fps = new QDoubleSpinBox;
	m_fps->setObjectName("clipImportFps");
	m_fps->setRange(.001, 1000);
	m_fps->setDecimals(3);
	m_fps->setValue(24);
	m_fps->setLayoutDirection(Qt::LeftToRight);
	accessible(m_fps, Text::tr("Clip FPS"), Text::tr("The new clip's playback rate; the source's rate is filled in."));
	form->addRow(Text::tr("&FPS"), m_fps);
	m_mode = new QComboBox;
	m_mode->setObjectName("clipImportMode");
	m_mode->addItems({Text::tr("Exact (same skeleton)"), Text::tr("Retarget (different proportions)")});
	accessible(m_mode, Text::tr("Import mode"),
			   Text::tr("Exact copies each joint's local pose and needs matching parents. Retarget transfers rotations relative to each "
						"rig's bind pose, keeps this rig's bone lengths and scales the root's motion."));
	form->addRow(Text::tr("&Mode"), m_mode);
	m_rootScale = new QDoubleSpinBox;
	m_rootScale->setObjectName("clipImportRootScale");
	m_rootScale->setRange(0, 1000);
	m_rootScale->setDecimals(4);
	m_rootScale->setSpecialValueText(Text::tr("Automatic"));
	m_rootScale->setLayoutDirection(Qt::LeftToRight);
	accessible(m_rootScale, Text::tr("Root motion scale"),
			   Text::tr("Retargeting scales the root's movement by this factor; Automatic uses the ratio of the two rigs' root heights."));
	form->addRow(Text::tr("Root &scale"), m_rootScale);
	m_fill = new QCheckBox(Text::tr("Keep unmatched joints in their bind pose"));
	m_fill->setObjectName("clipImportFill");
	accessible(m_fill, m_fill->text(), Text::tr("Without this, a joint of this rig with no joint in the source refuses the import."));
	form->addRow(m_fill);
	m_curves = new QCheckBox(Text::tr("Import the clip's animation curves"));
	m_curves->setObjectName("clipImportCurves");
	m_curves->setChecked(true);
	accessible(m_curves, m_curves->text(),
			   Text::tr("Exact imports keep the source's keys on the mapped joints. Retargeting cannot keep them, so it captures one key per "
						"sample for the joints the source keyed."));
	form->addRow(m_curves);
	m_events = new QCheckBox(Text::tr("Import the clip's events"));
	m_events->setObjectName("clipImportEvents");
	m_events->setChecked(true);
	accessible(m_events, m_events->text(), Text::tr("Keep the source clip's frame commands, such as footstep sounds, on the same samples."));
	form->addRow(m_events);
	m_mapping = new QTableWidget(0, 2);
	m_mapping->setObjectName("clipImportMapping");
	m_mapping->setHorizontalHeaderLabels({Text::tr("This rig's joint"), Text::tr("Source joint")});
	m_mapping->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_mapping->verticalHeader()->setVisible(false);
	accessible(m_mapping, Text::tr("Joint mapping"),
			   Text::tr("Choose which source joint drives each joint of this rig. Joints with the same name match automatically."));
	layout->addWidget(m_mapping, 1);
	// Side-by-side preview: the source clip and this model with the import,
	// at the same sample, each through its own camera.
	auto *previewBar = new QHBoxLayout;
	m_preview = new QPushButton(Text::tr("Update &Preview"));
	m_preview->setObjectName("clipImportPreview");
	accessible(m_preview, Text::tr("Update preview"),
			   Text::tr("Import into a copy of this model on the worker and show the source and the result side by side. Nothing changes "
						"until Apply."));
	m_sample = new QSpinBox;
	m_sample->setObjectName("clipImportSample");
	m_sample->setLayoutDirection(Qt::LeftToRight);
	accessible(m_sample, Text::tr("Preview sample"), Text::tr("The sample both previews show, numbered from zero."));
	auto *sampleLabel = new QLabel(Text::tr("Sa&mple"));
	sampleLabel->setBuddy(m_sample);
	previewBar->addWidget(m_preview);
	previewBar->addWidget(sampleLabel);
	previewBar->addWidget(m_sample);
	previewBar->addStretch(1);
	layout->addLayout(previewBar);
	auto *views = new QHBoxLayout;
	m_sourceView = new ModelViewport;
	m_sourceView->setObjectName("clipImportSourceView");
	m_resultView = new ModelViewport;
	m_resultView->setObjectName("clipImportResultView");
	for (auto *view : {m_sourceView, m_resultView})
	{
		view->setMinimumSize(200, 180);
		view->setShowJoints(true);
		view->setXrayJoints(true);
		views->addWidget(view, 1);
	}
	m_sourceView->setAccessibleName(Text::tr("Source clip preview"));
	m_resultView->setAccessibleName(Text::tr("Imported result preview"));
	layout->addLayout(views, 1);
	m_status = new LiveStatusLabel;
	m_status->setObjectName("clipImportStatus");
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	accessible(m_status, Text::tr("Import review"), Text::tr("What the import will do, or why it cannot."));
	layout->addWidget(m_status);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
	m_apply = buttons->button(QDialogButtonBox::Apply);
	m_apply->setObjectName("clipImportApply");
	accessible(m_apply, Text::tr("Import the clip"), Text::tr("Add the reviewed clip as one undoable edit."));
	layout->addWidget(buttons);
	connect(m_apply, &QPushButton::clicked, this,
			[this]
			{
				if (m_ready && !m_busy)
					accept();
			});
	connect(buttons, &QDialogButtonBox::rejected, this, &ModelClipImportDialog::reject);
	connect(m_choose, &QPushButton::clicked, this,
			[this]
			{
				QStringList patterns;
				for (const auto &suffix : modelMeshFileSuffixes())
					patterns << QStringLiteral("*.") + suffix;
				patterns << QStringLiteral("*.mesh.json");
				const auto path = QFileDialog::getOpenFileName(this, Text::tr("Import Animation"), QString(),
															   Text::tr("Models and animations (%1)").arg(patterns.join(QLatin1Char(' '))));
				if (!path.isEmpty())
					loadSource(path);
			});
	connect(m_clip, &QComboBox::currentIndexChanged, this,
			[this]
			{
				ModelClipImportOptions base;
				const int clip = m_clip->currentData().toInt();
				if (clip >= 0 && clip < m_source.skeleton.clips.size() && modelClipImportFromSource(m_source, clip, QFileInfo(m_sourcePath).fileName(), &base))
				{
					const QSignalBlocker blockName(m_name), blockFps(m_fps);
					m_name->setText(base.name);
					m_fps->setValue(base.framesPerSecond);
				}
				changed();
			});
	connect(m_mode, &QComboBox::currentIndexChanged, this, [this] { changed(); });
	connect(m_name, &QLineEdit::textChanged, this, [this] { changed(); });
	connect(m_fps, &QDoubleSpinBox::valueChanged, this, [this] { changed(); });
	connect(m_rootScale, &QDoubleSpinBox::valueChanged, this, [this] { changed(); });
	connect(m_fill, &QCheckBox::toggled, this, [this] { changed(); });
	connect(m_curves, &QCheckBox::toggled, this, [this] { changed(); });
	connect(m_events, &QCheckBox::toggled, this, [this] { changed(); });
	connect(m_preview, &QPushButton::clicked, this, [this] { updatePreview(); });
	connect(m_sample, &QSpinBox::valueChanged, this, [this] { showSamples(); });
	fitDialogToScreen(this, QSize(std::max(760, fontMetrics().averageCharWidth() * 90), 860));
	changed();
}

bool ModelClipImportDialog::loadSource(const QString &path)
{
	if (m_busy)
		return false;
	ModelMesh decoded;
	QString error;
	m_busy = true;
	m_status->setText(Text::tr("Reading %1…").arg(QFileInfo(path).fileName()));
	const bool ok = runModelTask(
		this, Text::tr("Read Animation"),
		[path, &decoded](QString *failure, const ModelWorkControl &control)
		{
			if (path.endsWith(QStringLiteral(".mesh.json"), Qt::CaseInsensitive))
			{
				ModelDocument document;
				if (!document.load(path, failure, control))
					return false;
				decoded = document.mesh();
				return true;
			}
			decoded = decodeModelMeshFile(path, nullptr, control);
			if (!decoded.error.isEmpty())
			{
				if (failure)
					*failure = decoded.error;
				return false;
			}
			return modelWorkCheckpoint(control, ModelWorkPhase::Validating, 0, 0, failure);
		},
		&error, false, &m_cancel);
	m_busy = false;
	if (!ok || decoded.skeleton.clips.isEmpty())
	{
		m_status->setText(ok ? Text::tr("The file %1 has no skeletal clips.").arg(QFileInfo(path).fileName()) : error, LiveStatus::Problem);
		return false;
	}
	m_source = std::move(decoded);
	m_sourcePath = path;
	m_path->setText(QDir::toNativeSeparators(path));
	fillMapping();
	{
		const QSignalBlocker blocker(m_clip);
		m_clip->clear();
		for (int i = 0; i < m_source.skeleton.clips.size(); ++i)
			m_clip->addItem(QStringLiteral("\u2066%1: %2 (%3)\u2069").arg(i).arg(m_source.skeleton.clips[i].name).arg(m_source.skeleton.clips[i].frames.size()), i);
	}
	m_clip->setCurrentIndex(-1);
	m_clip->setCurrentIndex(0);
	return true;
}

void ModelClipImportDialog::fillMapping()
{
	m_mapping->setRowCount(int(m_mesh.skeleton.joints.size()));
	for (int row = 0; row < m_mesh.skeleton.joints.size(); ++row)
	{
		const auto &name = m_mesh.skeleton.joints[row].name;
		auto *item = new QTableWidgetItem(name);
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
		m_mapping->setItem(row, 0, item);
		auto *combo = new QComboBox;
		combo->setLayoutDirection(Qt::LeftToRight);
		combo->setAccessibleName(Text::tr("Source joint for %1").arg(name));
		const bool sameName = modelJointIndex(m_source.skeleton, name, Qt::CaseSensitive) >= 0;
		// A same-named source joint always drives its joint unless it is
		// remapped, so only joints without one can keep their bind pose.
		if (!sameName)
			combo->addItem(Text::tr("(bind pose)"), QString());
		for (const auto &joint : m_source.skeleton.joints)
			combo->addItem(joint.name, joint.name);
		combo->setCurrentIndex(std::max(0, combo->findData(name)));
		connect(combo, &QComboBox::currentIndexChanged, this, [this] { changed(); });
		m_mapping->setCellWidget(row, 1, combo);
	}
}

ModelClipImportOptions ModelClipImportDialog::options() const
{
	ModelClipImportOptions o;
	const int clip = m_clip->currentData().toInt();
	if (m_source.skeleton.isEmpty() || clip < 0 || !modelClipImportFromSource(m_source, clip, QFileInfo(m_sourcePath).fileName(), &o))
		return {};
	o.name = m_name->text();
	o.framesPerSecond = m_fps->value();
	o.retarget = m_mode->currentIndex() == 1;
	o.rootScale = o.retarget ? m_rootScale->value() : 0;
	o.fillFromBind = m_fill->isChecked();
	for (int row = 0; row < m_mapping->rowCount(); ++row)
	{
		const auto *combo = qobject_cast<QComboBox *>(m_mapping->cellWidget(row, 1));
		const auto source = combo ? combo->currentData().toString() : QString();
		const auto destination = m_mapping->item(row, 0)->text();
		if (!source.isEmpty() && source != destination)
			o.joints.insert(source, destination);
	}
	if (!m_curves->isChecked())
		o.curves.clear();
	if (!m_events->isChecked())
		o.events.clear();
	return o;
}
bool ModelClipImportDialog::updatePreview()
{
	if (m_busy || !m_ready)
		return false;
	ModelEdit operation = edit();
	const auto mesh = m_mesh;
	ModelMesh candidate;
	QString error;
	m_busy = true;
	m_preview->setEnabled(false);
	m_status->setText(Text::tr("Importing into a preview copy…"));
	const bool ok = runModelTask(
		this, Text::tr("Preview Animation Import"),
		[mesh, operation, &candidate](QString *failure, const ModelWorkControl &control)
		{
			candidate = mesh;
			return applyModelEdit(&candidate, operation, nullptr, failure, control);
		},
		&error, false, &m_cancel);
	m_busy = false;
	m_preview->setEnabled(true);
	if (!ok)
	{
		m_previewed = false;
		m_status->setText(error, LiveStatus::Problem);
		return false;
	}
	m_result = std::move(candidate);
	m_previewed = true;
	m_sample->setRange(0, std::max(0, int(m_result.skeleton.clips.last().frames.size()) - 1));
	showSamples();
	m_status->setText(Text::tr("Preview updated: the source on the left, this model with the imported clip on the right."), LiveStatus::Result);
	return true;
}
void ModelClipImportDialog::showSamples()
{
	const int sourceClip = m_clip->currentData().toInt();
	if (sourceClip >= 0 && sourceClip < m_source.skeleton.clips.size())
	{
		int frame = -1;
		m_sourceView->setMesh(modelSkeletalSamplePreview(m_source, sourceClip, m_sample->value(), &frame), true);
		m_sourceView->setFrame(std::max(0, frame));
	}
	if (m_previewed && !m_result.skeleton.clips.isEmpty())
	{
		int frame = -1;
		m_resultView->setMesh(modelSkeletalSamplePreview(m_result, int(m_result.skeleton.clips.size()) - 1, m_sample->value(), &frame), true);
		m_resultView->setFrame(std::max(0, frame));
	}
}

void ModelClipImportDialog::changed()
{
	m_ready = false;
	// A changed setting makes the result preview stale until it is updated.
	if (m_previewed)
	{
		m_previewed = false;
		m_resultView->setMesh(m_mesh, true);
	}
	const int sourceClip = m_clip->currentData().toInt();
	if (sourceClip >= 0 && sourceClip < m_source.skeleton.clips.size())
	{
		m_sample->setRange(0, std::max(0, int(m_source.skeleton.clips[sourceClip].frames.size()) - 1));
		showSamples();
	}
	m_rootScale->setEnabled(m_mode->currentIndex() == 1);
	m_preview->setEnabled(false);
	if (m_source.skeleton.isEmpty())
	{
		m_status->setText(Text::tr("Choose a file with skeletal animation to import."));
		m_apply->setEnabled(false);
		return;
	}
	const auto o = options();
	ModelClipImportPlan plan;
	QString error;
	if (o.clip.isEmpty() || !planModelClipImport(m_mesh, o, &plan, &error))
	{
		m_status->setText(error.isEmpty() ? Text::tr("The source clip cannot be read.") : error, LiveStatus::Problem);
		m_apply->setEnabled(false);
		return;
	}
	int mapped = 0;
	for (const auto &joint : plan.joints)
		mapped += !joint.source.isEmpty();
	const int total = int(plan.joints.size());
	auto text = Text::tr("%n sample(s) will become clip %1.", nullptr, plan.samples).arg(o.name) + QLatin1Char(' ') +
				Text::tr("Joints that follow the source: %1 of %2.").arg(QLocale().toString(mapped), QLocale().toString(total));
	if (o.retarget)
		text += QLatin1Char(' ') + Text::tr("Root motion is scaled by %1.").arg(localeNumber(plan.rootScale, 4));
	if (!plan.unusedSourceJoints.isEmpty())
		text += QLatin1Char(' ') + Text::tr("Unused source joints: %1.").arg(plan.unusedSourceJoints.join(QStringLiteral(", ")));
	if (plan.curves > 0)
		text += QLatin1Char(' ') + (plan.capturedCurves ? Text::tr("Joints with curves captured from the retargeted samples: %1.").arg(QLocale().toString(plan.curves))
														: Text::tr("Joints keeping the source's curves: %1.").arg(QLocale().toString(plan.curves)));
	if (plan.events > 0)
		text += QLatin1Char(' ') + Text::tr("Events kept: %1.").arg(QLocale().toString(plan.events));
	m_status->setText(text, LiveStatus::Result);
	m_ready = !o.name.isEmpty();
	m_apply->setEnabled(m_ready);
	m_preview->setEnabled(m_ready);
}

ModelEdit ModelClipImportDialog::edit() const
{
	ModelEdit result;
	result.kind = ModelEditKind::ImportSkeletalClip;
	result.animation.import = options();
	return result;
}

void ModelClipImportDialog::reject()
{
	if (m_busy)
	{
		if (m_cancel)
			m_cancel();
		return;
	}
	QDialog::reject();
}
} // namespace vibestudio
