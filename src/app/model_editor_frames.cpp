#include "app/model_editor_frames.h"

#include "app/live_status_label.h"
#include "app/model_editor_dialog.h"
#include "app/model_frame_timeline.h"
#include "app/model_viewport.h"
#include "app/status_line_label.h"
#include "app/wrapping_action_button.h"
#include "app/wrapping_check_box.h"
#include "core/model_frame_import.h"
#include "core/model_frame_tools.h"
#include "core/model_tag_follow.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QAction>
#include <QToolBar>

#include <algorithm>
#include <memory>

namespace vibestudio
{
namespace
{
void describe(QWidget *field, const QString &label, const QString &description)
{
	field->setAccessibleName(label);
	field->setAccessibleDescription(description);
	field->setToolTip(description);
}
QSpinBox *spinBox(QObject *owner, QFormLayout *form, const char *id, const QString &label, const QString &description, int minimum,
				  int maximum, std::function<void()> changed)
{
	auto *field = new QSpinBox;
	field->setObjectName(QString::fromLatin1(id));
	field->setRange(minimum, maximum);
	field->setLayoutDirection(Qt::LeftToRight);
	field->setMinimumWidth(0);
	field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	describe(field, label, description);
	form->addRow(label, field);
	QObject::connect(field, &QSpinBox::valueChanged, owner, [changed](int) { changed(); });
	return field;
}
QDoubleSpinBox *doubleBox(QFormLayout *form, const char *id, const QString &label, const QString &description, double minimum,
						  double maximum, int decimals)
{
	auto *field = new QDoubleSpinBox;
	field->setObjectName(QString::fromLatin1(id));
	field->setRange(minimum, maximum);
	field->setDecimals(decimals);
	field->setKeyboardTracking(false);
	field->setLayoutDirection(Qt::LeftToRight);
	field->setMinimumWidth(0);
	field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	describe(field, label, description);
	form->addRow(label, field);
	return field;
}
QComboBox *comboBox(QFormLayout *form, const char *id, const QString &label, const QString &description, const QStringList &items)
{
	auto *field = new QComboBox;
	field->setObjectName(QString::fromLatin1(id));
	field->addItems(items);
	field->setMinimumContentsLength(8);
	field->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	describe(field, label, description);
	form->addRow(label, field);
	return field;
}
QCheckBox *checkBox(QFormLayout *form, const char *id, const QString &label, const QString &description)
{
	auto *field = new WrappingCheckBox(label);
	field->setObjectName(QString::fromLatin1(id));
	describe(field, label, description);
	form->addRow(field);
	return field;
}
bool componentsSelected(const ModelSelection &selection)
{
	return selection.tag.isEmpty() && selection.collision.isEmpty() && selection.joint.isEmpty() && selection.surfaces.isEmpty() &&
		   (!selection.vertices.isEmpty() || !selection.faces.isEmpty() || !selection.edges.isEmpty());
}
} // namespace

ModelEditorAnimationPanel::ModelEditorAnimationPanel(ModelEditorDialog *editor, QFormLayout *animation)
	: QObject(editor), m_editor(editor), m_viewport(editor->m_preview)
{
	setObjectName(QStringLiteral("meshAnimationPanel"));
	// The page is not attached to the dialog yet while it is built, so look
	// inside the page itself.
	m_page = animation->parentWidget();
	int row = animation->rowCount();
	if (auto *clips = m_page ? m_page->findChild<QGroupBox *>(QStringLiteral("meshAnimationClips")) : nullptr)
	{
		int found = -1;
		QFormLayout::ItemRole role;
		animation->getWidgetPosition(clips, &found, &role);
		if (found >= 0)
			row = found + 1;
	}
	buildFrameRange(animation, &row);
	buildResample(animation, &row);
	buildSculpt(animation, &row);
	buildImport(animation, &row);
	buildTagFollow();
	buildTimeline();
	buildRangeScope();
	connect(editor->m_preview, &ModelViewport::playbackChanged, this, [this](bool) { refresh(); });
}

ModelEditorAnimationPanel::~ModelEditorAnimationPanel()
{
	for (const auto &pane : std::as_const(m_onionPanes))
		if (pane)
			pane->setAnimationOverlayPainter({});
}

QPushButton *ModelEditorAnimationPanel::addButton(QFormLayout *form, const char *id, const QString &label, const QString &description)
{
	auto *control = new WrappingActionButton(label, 2);
	control->setObjectName(QString::fromLatin1(id));
	control->setAccessibleName(label);
	control->setAccessibleDescription(description);
	control->setToolTip(description);
	control->setProperty("frameRangeDescription", description);
	form->addRow(control);
	return control;
}

QGroupBox *ModelEditorAnimationPanel::addGroup(QFormLayout *animation, int *row, const char *id, const QString &title, QFormLayout **form)
{
	auto *group = new QGroupBox(title);
	group->setObjectName(QString::fromLatin1(id));
	group->setAccessibleName(title);
	auto *layout = new QFormLayout(group);
	layout->setContentsMargins(4, 6, 4, 6);
	layout->setRowWrapPolicy(QFormLayout::WrapAllRows);
	layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	animation->insertRow((*row)++, group);
	*form = layout;
	return group;
}

void ModelEditorAnimationPanel::buildFrameRange(QFormLayout *animation, int *row)
{
	QFormLayout *form = nullptr;
	addGroup(animation, row, "meshFrameRange", QCoreApplication::translate("VibeStudioModelEditor", "Frame range"), &form);
	const auto changed = [this] { refresh(); };
	m_first = spinBox(this, form, "meshFrameRangeFirst", QCoreApplication::translate("VibeStudioModelEditor", "Range first frame"),
					  QCoreApplication::translate("VibeStudioModelEditor",
												  "First frame of the range, using the transport's zero-based frame index. Shift-drag on "
												  "the timeline to set the range."),
					  0, modelDocumentMaxFrames - 1, changed);
	m_last = spinBox(this, form, "meshFrameRangeLast", QCoreApplication::translate("VibeStudioModelEditor", "Range last frame"),
					 QCoreApplication::translate("VibeStudioModelEditor", "Last frame of the range, included."), 0, modelDocumentMaxFrames - 1,
					 changed);
	m_destination = spinBox(this, form, "meshFrameRangeDestination", QCoreApplication::translate("VibeStudioModelEditor", "Move to frame"),
							QCoreApplication::translate("VibeStudioModelEditor", "The index the first moved frame takes after the move."), 0,
							modelDocumentMaxFrames - 1, changed);
	m_move = addButton(form, "moveMeshFrames", QCoreApplication::translate("VibeStudioModelEditor", "Move Frames"),
					   QCoreApplication::translate("VibeStudioModelEditor",
												   "Move the range so its first frame lands at Move to frame. Clip ranges, attachment tags, "
												   "collision poses and MDL native groups follow the frames."));
	connect(m_move, &QPushButton::clicked, this, [this] { run(ModelEditKind::MoveFrames); });
	m_duplicate = addButton(form, "duplicateMeshFrameRange", QCoreApplication::translate("VibeStudioModelEditor", "Duplicate Range"),
							QCoreApplication::translate("VibeStudioModelEditor",
														"Insert a copy of the range right after it. A clip holding the range's last frame grows."));
	connect(m_duplicate, &QPushButton::clicked, this, [this] { run(ModelEditKind::DuplicateFrameRange); });
	m_delete = addButton(form, "deleteMeshFrameRange", QCoreApplication::translate("VibeStudioModelEditor", "Delete Range"),
						 QCoreApplication::translate("VibeStudioModelEditor",
													 "Remove every frame in the range. Clips shrink, and a clip left with no frames is removed."));
	connect(m_delete, &QPushButton::clicked, this, [this] { run(ModelEditKind::DeleteFrameRange); });
	m_reverse = addButton(form, "reverseMeshFrameRange", QCoreApplication::translate("VibeStudioModelEditor", "Reverse Range"),
						  QCoreApplication::translate("VibeStudioModelEditor", "Reverse the order of the frames in the range, in place."));
	connect(m_reverse, &QPushButton::clicked, this, [this] { run(ModelEditKind::ReverseFrameRange); });
	m_pingPong = addButton(form, "pingPongMeshFrameRange", QCoreApplication::translate("VibeStudioModelEditor", "Add Ping-Pong Return"),
						   QCoreApplication::translate("VibeStudioModelEditor",
													   "Insert the frames between the range's ends in reverse after it (1 2 3 4 3 2), so a "
													   "looping clip returns to its first pose."));
	connect(m_pingPong, &QPushButton::clicked, this, [this] { run(ModelEditKind::ReverseFrameRange, true); });
	m_clipCheck = new LiveStatusLabel;
	m_clipCheck->setObjectName(QStringLiteral("meshNativeClipCheck"));
	m_clipCheck->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Native clip check"));
	m_clipCheck->setTextFormat(Qt::PlainText);
	m_clipCheck->setWordWrap(true);
	m_clipCheck->setMinimumWidth(0);
	m_clipCheck->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	form->addRow(m_clipCheck);
	m_startNumber = spinBox(this, form, "meshFrameNameStart", QCoreApplication::translate("VibeStudioModelEditor", "First frame number"),
							QCoreApplication::translate("VibeStudioModelEditor",
														"The number each clip's first frame gets: 1 names them stand1, stand2 and so on."),
							0, 1000, changed);
	m_startNumber->setValue(1);
	m_renameClips = checkBox(form, "meshFrameNameRenameClips", QCoreApplication::translate("VibeStudioModelEditor", "Rename clips to their frame stem"),
							 QCoreApplication::translate("VibeStudioModelEditor",
														 "Clip names with spaces or other characters frame names cannot hold become the stem "
														 "their frames use, so the clip name comes back from native files too."));
	m_renameClips->setChecked(true);
	m_name = addButton(form, "nameMeshFramesFromClips", QCoreApplication::translate("VibeStudioModelEditor", "Name Frames From Clips"),
					   QCoreApplication::translate("VibeStudioModelEditor",
												   "Name the frames of the clip chosen above, or of every clip when All frames is chosen, "
												   "stem1, stem2 and so on within 15 characters, so MDL, MD2 and MD3 files bring the clips back."));
	connect(m_name, &QPushButton::clicked, this, [this] { run(ModelEditKind::RenameFramesFromClips); });
	// Onion skin: ghosted neighbouring frames in the 3D view.
	m_onion = checkBox(form, "meshOnionSkin", QCoreApplication::translate("VibeStudioModelEditor", "Onion skin"),
					   QCoreApplication::translate("VibeStudioModelEditor",
												   "Draw neighbouring frames as ghosted wireframes in the 3D view: earlier frames dashed, later "
												   "frames dotted, fainter with distance. Hidden while the animation plays."));
	connect(m_onion, &QCheckBox::toggled, this, [this](bool) { refresh(); });
	m_onionBefore = spinBox(this, form, "meshOnionBefore", QCoreApplication::translate("VibeStudioModelEditor", "Frames before"),
							QCoreApplication::translate("VibeStudioModelEditor", "How many earlier frames the onion skin shows."), 0, 5, changed);
	m_onionBefore->setValue(1);
	m_onionAfter = spinBox(this, form, "meshOnionAfter", QCoreApplication::translate("VibeStudioModelEditor", "Frames after"),
						   QCoreApplication::translate("VibeStudioModelEditor", "How many later frames the onion skin shows."), 0, 5, changed);
	m_onionAfter->setValue(1);
	m_onionStep = spinBox(this, form, "meshOnionStep", QCoreApplication::translate("VibeStudioModelEditor", "Onion step"),
						  QCoreApplication::translate("VibeStudioModelEditor", "Show every frame, or every second, third and so on."), 1, 10,
						  changed);
}

void ModelEditorAnimationPanel::buildResample(QFormLayout *animation, int *row)
{
	QFormLayout *form = nullptr;
	addGroup(animation, row, "meshResample", QCoreApplication::translate("VibeStudioModelEditor", "Resample and loop"), &form);
	m_sourceFps = doubleBox(form, "meshResampleSourceFps", QCoreApplication::translate("VibeStudioModelEditor", "Current FPS"),
							QCoreApplication::translate("VibeStudioModelEditor",
														"The rate the clip was made at. Clip's FPS uses the saved rate of the clip chosen "
														"under Animation clips."),
							0, 1000, 3);
	m_sourceFps->setSpecialValueText(QCoreApplication::translate("VibeStudioModelEditor", "Clip's FPS"));
	m_targetFps = doubleBox(form, "meshResampleTargetFps", QCoreApplication::translate("VibeStudioModelEditor", "New FPS"),
							QCoreApplication::translate("VibeStudioModelEditor",
														"The rate to resample to, such as 10 for Quake and Quake II frame steps. Use frame "
														"count resamples to New frame count instead."),
							0, 1000, 3);
	m_targetFps->setSpecialValueText(QCoreApplication::translate("VibeStudioModelEditor", "Use frame count"));
	m_targetFps->setValue(10);
	m_targetFrames = spinBox(this, form, "meshResampleTargetFrames", QCoreApplication::translate("VibeStudioModelEditor", "New frame count"),
							 QCoreApplication::translate("VibeStudioModelEditor", "The number of frames the clip gets, when New FPS is Use frame count."),
							 0, modelDocumentMaxFrames, [this] { refresh(); });
	m_targetFrames->setSpecialValueText(QCoreApplication::translate("VibeStudioModelEditor", "Use FPS"));
	m_method = comboBox(form, "meshResampleMethod", QCoreApplication::translate("VibeStudioModelEditor", "Sampling"),
						QCoreApplication::translate("VibeStudioModelEditor",
													"Blend between stored frames (positions, normals, tags along the shortest arc and collision "
													"boxes), or copy the nearer stored frame exactly."),
						{QCoreApplication::translate("VibeStudioModelEditor", "Blend"), QCoreApplication::translate("VibeStudioModelEditor", "Nearest frame")});
	m_cyclic = checkBox(form, "meshResampleCyclic", QCoreApplication::translate("VibeStudioModelEditor", "Looping clip"),
						QCoreApplication::translate("VibeStudioModelEditor",
													"The clip loops: the step from its last frame back to its first is sampled too, and the "
													"new frames divide the whole loop evenly."));
	m_resample = addButton(form, "resampleMeshClip", QCoreApplication::translate("VibeStudioModelEditor", "Resample Clip"),
						   QCoreApplication::translate("VibeStudioModelEditor",
													   "Replace the chosen clip's frames with frames sampled at the new rate or count, in one "
													   "undo step. Frames are renamed after the clip; later clips, tags, collision poses and MDL "
													   "groups follow."));
	connect(m_resample, &QPushButton::clicked, this, [this] { run(ModelEditKind::ResampleAnimation); });
	m_loopMode = comboBox(form, "meshLoopMode", QCoreApplication::translate("VibeStudioModelEditor", "Close loop by"),
						  QCoreApplication::translate("VibeStudioModelEditor",
													  "Replace the last frame with the first pose, add a copy of the first pose at the end, or "
													  "blend the last frames toward the first pose."),
						  {QCoreApplication::translate("VibeStudioModelEditor", "Replacing the last frame"),
						   QCoreApplication::translate("VibeStudioModelEditor", "Appending the first pose"),
						   QCoreApplication::translate("VibeStudioModelEditor", "Cross-fading")});
	connect(m_loopMode, &QComboBox::currentIndexChanged, this, [this](int) { refresh(); });
	m_blendFrames = spinBox(this, form, "meshLoopBlendFrames", QCoreApplication::translate("VibeStudioModelEditor", "Cross-fade frames"),
							QCoreApplication::translate("VibeStudioModelEditor", "How many of the last frames blend toward the first pose."), 1,
							modelDocumentMaxFrames - 1, [this] { refresh(); });
	m_blendFrames->setValue(2);
	m_closeLoop = addButton(form, "closeMeshClipLoop", QCoreApplication::translate("VibeStudioModelEditor", "Close Clip Loop"),
							QCoreApplication::translate("VibeStudioModelEditor",
														"Make the chosen clip loop without a jump from its last pose to its first, in one undo step."));
	connect(m_closeLoop, &QPushButton::clicked, this, [this] { run(ModelEditKind::CloseAnimationLoop); });
	m_skeletalClip = comboBox(form, "meshResampleSkeletalClip", QCoreApplication::translate("VibeStudioModelEditor", "Skeletal clip"),
							  QCoreApplication::translate("VibeStudioModelEditor",
														  "A skeletal clip to resample or close with the settings above. Joints interpolate in "
														  "parent space; animation curves are rescaled."),
							  {});
	m_bakeCurves = checkBox(form, "meshResampleBakeCurves", QCoreApplication::translate("VibeStudioModelEditor", "Bake curves"),
							QCoreApplication::translate("VibeStudioModelEditor",
														"Sample a clip's animation curves into poses and remove the curves, instead of requiring "
														"every key to land on a whole frame of the result."));
	m_resampleSkeletal = addButton(form, "resampleMeshSkeletalClip", QCoreApplication::translate("VibeStudioModelEditor", "Resample Skeletal Clip"),
								   QCoreApplication::translate("VibeStudioModelEditor",
															   "Resample the chosen skeletal clip to the new rate or count and re-bake its frames."));
	connect(m_resampleSkeletal, &QPushButton::clicked, this, [this] { run(ModelEditKind::ResampleSkeletalClip); });
	m_closeSkeletal = addButton(form, "closeMeshSkeletalLoop", QCoreApplication::translate("VibeStudioModelEditor", "Close Skeletal Loop"),
								QCoreApplication::translate("VibeStudioModelEditor",
															"Close the chosen skeletal clip's loop, as Close loop by says, and re-bake its frames."));
	connect(m_closeSkeletal, &QPushButton::clicked, this, [this] { run(ModelEditKind::CloseSkeletalLoop); });
}

void ModelEditorAnimationPanel::buildSculpt(QFormLayout *animation, int *row)
{
	QFormLayout *form = nullptr;
	addGroup(animation, row, "meshRangeSculpt", QCoreApplication::translate("VibeStudioModelEditor", "Range sculpt"), &form);
	m_falloff = comboBox(form, "meshRangeFalloff", QCoreApplication::translate("VibeStudioModelEditor", "Falloff over the range"),
						 QCoreApplication::translate("VibeStudioModelEditor",
													 "How strongly each frame of the range is changed: fully, growing toward the last frame, "
													 "fading after the first, or a smooth bump strongest in the middle."),
						 {QCoreApplication::translate("VibeStudioModelEditor", "Constant"),
						  QCoreApplication::translate("VibeStudioModelEditor", "Ramp in"),
						  QCoreApplication::translate("VibeStudioModelEditor", "Ramp out"),
						  QCoreApplication::translate("VibeStudioModelEditor", "Smooth")});
	m_rangeTransform = addButton(form, "transformMeshFrameRange", QCoreApplication::translate("VibeStudioModelEditor", "Transform Selection Over Range"),
								 QCoreApplication::translate("VibeStudioModelEditor",
															 "Move, rotate and scale the selected vertices by the Geometry page's values in every "
															 "frame of the range, scaled by the falloff. The pivot is resolved once, in the "
															 "displayed frame."));
	connect(m_rangeTransform, &QPushButton::clicked, this, [this] { run(ModelEditKind::TransformFrameRange); });
	const auto changed = [this] { refresh(); };
	m_blendSource = spinBox(this, form, "meshBlendSourceFrame", QCoreApplication::translate("VibeStudioModelEditor", "Blend from frame"),
							QCoreApplication::translate("VibeStudioModelEditor", "The frame whose pose the selection blends toward."), 0,
							modelDocumentMaxFrames - 1, changed);
	m_blendAmount = doubleBox(form, "meshBlendAmount", QCoreApplication::translate("VibeStudioModelEditor", "Blend amount"),
							  QCoreApplication::translate("VibeStudioModelEditor", "0 keeps each pose; 1 takes the source pose fully."), 0, 1, 3);
	m_blendAmount->setSingleStep(0.1);
	m_blendAmount->setValue(1);
	m_blendPose = addButton(form, "blendMeshFramePose", QCoreApplication::translate("VibeStudioModelEditor", "Blend Selection From Frame"),
							QCoreApplication::translate("VibeStudioModelEditor",
														"Move only the selected vertices toward Blend from frame's pose in every frame of the "
														"range. Tags, collision and unselected vertices stay as they are."));
	connect(m_blendPose, &QPushButton::clicked, this, [this] { run(ModelEditKind::BlendFramePose); });
	m_poseA = spinBox(this, form, "meshRelativePoseA", QCoreApplication::translate("VibeStudioModelEditor", "Pose frame"),
					  QCoreApplication::translate("VibeStudioModelEditor", "The pose of the difference: Pose frame minus Base frame."), 0,
					  modelDocumentMaxFrames - 1, changed);
	m_poseB = spinBox(this, form, "meshRelativePoseB", QCoreApplication::translate("VibeStudioModelEditor", "Base frame"),
					  QCoreApplication::translate("VibeStudioModelEditor", "The base the pose differs from."), 0, modelDocumentMaxFrames - 1, changed);
	m_poseWeight = doubleBox(form, "meshRelativePoseWeight", QCoreApplication::translate("VibeStudioModelEditor", "Difference weight"),
							 QCoreApplication::translate("VibeStudioModelEditor", "How much of the difference to add, from -10 to 10."), -10, 10, 3);
	m_poseWeight->setSingleStep(0.1);
	m_poseWeight->setValue(1);
	m_relativePose = addButton(form, "applyMeshRelativePose", QCoreApplication::translate("VibeStudioModelEditor", "Add Pose Difference"),
							   QCoreApplication::translate("VibeStudioModelEditor",
														   "Add the selected vertices' movement from Base frame to Pose frame to every frame of the "
														   "range, like a shape key, scaled by the weight and falloff."));
	connect(m_relativePose, &QPushButton::clicked, this, [this] { run(ModelEditKind::ApplyRelativePose); });
}

void ModelEditorAnimationPanel::buildImport(QFormLayout *animation, int *row)
{
	QFormLayout *form = nullptr;
	addGroup(animation, row, "meshFrameImport", QCoreApplication::translate("VibeStudioModelEditor", "Import and export frames"), &form);
	m_importMatch = comboBox(form, "meshFrameImportMatch", QCoreApplication::translate("VibeStudioModelEditor", "Match vertices by"),
							 QCoreApplication::translate("VibeStudioModelEditor",
														 "Face corners: the same faces in the same order, however the exporter split "
														 "vertices. Exact: identical vertex lists. Nearest: the same UV at the same place as the "
														 "displayed frame."),
							 {QCoreApplication::translate("VibeStudioModelEditor", "Face corners"),
							  QCoreApplication::translate("VibeStudioModelEditor", "Exact vertex order"),
							  QCoreApplication::translate("VibeStudioModelEditor", "Nearest position and UV")});
	m_importAfter = spinBox(this, form, "meshFrameImportAfter", QCoreApplication::translate("VibeStudioModelEditor", "Insert after frame"),
							QCoreApplication::translate("VibeStudioModelEditor",
														"The new frames follow this frame; End appends them. With Replace frames, the first "
														"frame they replace."),
							-1, modelDocumentMaxFrames - 1, [this] { refresh(); });
	m_importAfter->setSpecialValueText(QCoreApplication::translate("VibeStudioModelEditor", "End"));
	m_importAfter->setValue(-1);
	m_importReplace = checkBox(form, "meshFrameImportReplace", QCoreApplication::translate("VibeStudioModelEditor", "Replace frames"),
							   QCoreApplication::translate("VibeStudioModelEditor",
														   "Overwrite existing frames from Insert after frame instead of inserting new ones."));
	m_importClip = new QLineEdit;
	m_importClip->setObjectName(QStringLiteral("meshFrameImportClip"));
	m_importClip->setMaxLength(128);
	m_importClip->setMinimumWidth(0);
	describe(m_importClip, QCoreApplication::translate("VibeStudioModelEditor", "New clip name"),
			 QCoreApplication::translate("VibeStudioModelEditor",
										 "Optional: a clip over the imported frames. Its frames are then named after it (run1, run2 ...)."));
	form->addRow(QCoreApplication::translate("VibeStudioModelEditor", "New clip name"), m_importClip);
	m_importFps = doubleBox(form, "meshFrameImportFps", QCoreApplication::translate("VibeStudioModelEditor", "New clip FPS"),
							QCoreApplication::translate("VibeStudioModelEditor", "The new clip's saved rate; zero leaves it unspecified."), 0, 1000, 3);
	m_importFps->setSpecialValueText(QCoreApplication::translate("VibeStudioModelEditor", "Unspecified"));
	m_objUp = comboBox(form, "meshObjUpAxis", QCoreApplication::translate("VibeStudioModelEditor", "OBJ up axis"),
					   QCoreApplication::translate("VibeStudioModelEditor",
												   "Blender and most modellers export OBJ with Y up and -Z forward; idTech models are Z up."),
					   {QCoreApplication::translate("VibeStudioModelEditor", "Z up (as idTech)"),
						QCoreApplication::translate("VibeStudioModelEditor", "Y up (Blender default)")});
	m_objScale = doubleBox(form, "meshObjScale", QCoreApplication::translate("VibeStudioModelEditor", "OBJ scale"),
						   QCoreApplication::translate("VibeStudioModelEditor", "Multiplies OBJ positions after the axis conversion."), 0.000001,
						   1000000, 6);
	m_objScale->setValue(1);
	m_importButton = addButton(form, "importMeshFrames", QCoreApplication::translate("VibeStudioModelEditor", "Import Frames…"),
							   QCoreApplication::translate("VibeStudioModelEditor",
														   "Choose an OBJ sequence (one frame per file, in file-name order) or an MD2, MD3, MDL "
														   "or editable source of the same mesh. Every frame is matched to this model's vertices "
														   "and added in one undo step; a mismatch changes nothing."));
	connect(m_importButton, &QPushButton::clicked, this,
			[this]
			{
				const auto paths = QFileDialog::getOpenFileNames(
					m_editor, QCoreApplication::translate("VibeStudioModelEditor", "Import Frames"), QFileInfo(m_editor->m_document.path()).absolutePath(),
					QCoreApplication::translate("VibeStudioModelEditor", "Models (*.obj *.md2 *.md3 *.mdl *.mesh.json)"));
				if (!paths.isEmpty())
					importFrames(paths);
			});
	m_exportObj = addButton(form, "exportMeshFramesObj", QCoreApplication::translate("VibeStudioModelEditor", "Export Range as OBJ…"),
							QCoreApplication::translate("VibeStudioModelEditor",
														"Write each frame of the frame range as its own OBJ, numbered where the name has #, "
														"such as walk_###.obj, for editing in another modeller."));
	connect(m_exportObj, &QPushButton::clicked, this,
			[this]
			{
				auto pattern = QFileDialog::getSaveFileName(
					m_editor, QCoreApplication::translate("VibeStudioModelEditor", "Export Range as OBJ"),
					QFileInfo(m_editor->m_document.path()).absolutePath(), QCoreApplication::translate("VibeStudioModelEditor", "OBJ (*.obj)"),
					nullptr, QFileDialog::DontConfirmOverwrite);
				if (pattern.isEmpty())
					return;
				if (!pattern.endsWith(QStringLiteral(".obj"), Qt::CaseInsensitive))
					pattern += QStringLiteral(".obj");
				if (!pattern.contains(QLatin1Char('#')))
					pattern.insert(pattern.size() - 4, QStringLiteral("_###"));
				exportFramesObj(pattern, false);
			});
}

void ModelEditorAnimationPanel::buildTagFollow()
{
	auto *group = m_page ? m_page->findChild<QGroupBox *>(QStringLiteral("meshTagControls")) : nullptr;
	auto *form = group ? qobject_cast<QFormLayout *>(group->layout()) : nullptr;
	if (!form)
		return;
	m_followMode = comboBox(form, "meshTagFollowMode", QCoreApplication::translate("VibeStudioModelEditor", "Follow by"),
							QCoreApplication::translate("VibeStudioModelEditor",
														"Rigid: the selection's best-fit rotation and movement. Centroid: its centre only, axes "
														"kept. Surface: the selected faces' normal and first edge."),
							{QCoreApplication::translate("VibeStudioModelEditor", "Rigid fit"),
							 QCoreApplication::translate("VibeStudioModelEditor", "Centroid"),
							 QCoreApplication::translate("VibeStudioModelEditor", "Surface")});
	m_followReference = spinBox(this, form, "meshTagFollowReference", QCoreApplication::translate("VibeStudioModelEditor", "Reference frame"),
								QCoreApplication::translate("VibeStudioModelEditor",
															"The frame whose tag pose sets the tag's offset from the selection."),
								0, modelDocumentMaxFrames - 1, [this] { refresh(); });
	m_followRange = checkBox(form, "meshTagFollowRange", QCoreApplication::translate("VibeStudioModelEditor", "Only the frame range"),
							 QCoreApplication::translate("VibeStudioModelEditor",
														 "Fit only the frames of Frame range; otherwise every frame."));
	m_follow = addButton(form, "followMeshTagSelection", QCoreApplication::translate("VibeStudioModelEditor", "Follow Selection"),
						 QCoreApplication::translate("VibeStudioModelEditor",
													 "Select vertices, edges or faces, type the tag's name in Name, then fit the tag to them in "
													 "every frame, keeping its offset from the reference frame. A new name adds the tag at the "
													 "selection's centre. The result is ordinary per-frame tags, in one undo step."));
	connect(m_follow, &QPushButton::clicked, this, [this] { run(ModelEditKind::FollowTagComponents); });
	m_followAnchor = checkBox(form, "meshTagFollowAnchor", QCoreApplication::translate("VibeStudioModelEditor", "Remember the components"),
							  QCoreApplication::translate("VibeStudioModelEditor",
														  "Keep an anchor in the editable source, so Refit Anchored Tags can fit the tag to the "
														  "same components again after geometry edits."));
	m_followAnchor->setChecked(true);
	m_refitAnchors = addButton(form, "refitMeshAnchoredTags", QCoreApplication::translate("VibeStudioModelEditor", "Refit Anchored Tags"),
							   QCoreApplication::translate("VibeStudioModelEditor",
														   "Fit every anchored tag to its remembered components again, in one undo step, after "
														   "moving, sculpting or importing frames."));
	connect(m_refitAnchors, &QPushButton::clicked, this, [this] { run(ModelEditKind::RefitAnchoredTags); });
	m_anchorSummary = new QLabel;
	m_anchorSummary->setObjectName(QStringLiteral("meshTagAnchorSummary"));
	m_anchorSummary->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Anchored tags"));
	m_anchorSummary->setTextFormat(Qt::PlainText);
	m_anchorSummary->setWordWrap(true);
	m_anchorSummary->setMinimumWidth(0);
	m_anchorSummary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	form->addRow(m_anchorSummary);
	m_followResult = new LiveStatusLabel;
	m_followResult->setObjectName(QStringLiteral("meshTagFollowResult"));
	m_followResult->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Tag fit result"));
	m_followResult->setTextFormat(Qt::PlainText);
	m_followResult->setWordWrap(true);
	m_followResult->setMinimumWidth(0);
	m_followResult->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	form->addRow(m_followResult);
	if (m_editor->m_tagName)
		connect(m_editor->m_tagName, &QLineEdit::textChanged, this, [this] { refresh(); });
}

void ModelEditorAnimationPanel::buildRangeScope()
{
	// Geometry > Over the frame range, beside Apply to.
	auto *scope = m_editor->m_frameScope;
	auto *form = scope && scope->parentWidget() ? qobject_cast<QFormLayout *>(scope->parentWidget()->layout()) : nullptr;
	if (!form)
		return;
	int row = -1;
	QFormLayout::ItemRole role;
	form->getWidgetPosition(scope, &row, &role);
	m_rangeScope = new WrappingCheckBox(QCoreApplication::translate("VibeStudioModelEditor", "Over the frame range"));
	m_rangeScope->setObjectName(QStringLiteral("meshFrameRangeScope"));
	describe(m_rangeScope, m_rangeScope->text(),
			 QCoreApplication::translate("VibeStudioModelEditor",
										 "Move, rotate and scale the selected components in every frame of Animation > Frame range, "
										 "scaled by Range sculpt's falloff, instead of the frames Apply to chooses. The viewport's "
										 "live preview shows the displayed frame's share of the change. Proportional editing, mirror, "
										 "tags and collision boxes keep Apply to."));
	form->insertRow(row < 0 ? form->rowCount() : row + 1, m_rangeScope);
	connect(m_rangeScope, &QCheckBox::toggled, this, [this](bool) { refresh(); });
	connect(m_falloff, &QComboBox::currentIndexChanged, this, [this](int) { refresh(); });
}

bool ModelEditorAnimationPanel::rangeScopeApplies(const ModelEdit &edit) const
{
	return m_rangeScope && m_rangeScope->isChecked() &&
		   (edit.kind == ModelEditKind::Transform || edit.kind == ModelEditKind::WeightedTransform) && componentsSelected(edit.selection);
}

bool ModelEditorAnimationPanel::rangeScoped(const ModelEdit &edit, ModelEdit *scoped, QString *error) const
{
	const auto refuse = [&](const QString &reason)
	{
		if (error)
			*error = reason;
		return false;
	};
	if (edit.kind != ModelEditKind::Transform)
		return refuse(QCoreApplication::translate("VibeStudioModelEditor",
												  "Over the frame range moves the selection without proportional editing or mirror. Turn those "
												  "off, or clear Over the frame range."));
	ModelEdit result;
	result.kind = ModelEditKind::TransformFrameRange;
	result.selection = edit.selection;
	QString problem;
	if (!snapModelTranslation(edit.translation, edit.translationGrid, &result.translation, &problem) ||
		!snapModelRotation(edit.rotation, edit.rotationGrid, &result.rotation, &problem) ||
		!snapModelScale(edit.scale, edit.scaleGrid, &result.scale, &problem))
		return refuse(problem);
	result.pivot = edit.pivot;
	result.pivotMode = edit.pivotMode;
	result.pivotFrame = edit.frame >= 0 ? edit.frame : edit.pivotFrame;
	result.transformSpace = edit.transformSpace;
	result.axisRotation = edit.axisRotation;
	result.axesFrame = edit.axesFrame;
	result.poseRange.first = m_first->value();
	result.poseRange.last = m_last->value();
	result.poseRange.falloff = ModelFrameFalloff(m_falloff->currentIndex());
	*scoped = result;
	return true;
}

QVector<double> ModelEditorAnimationPanel::rangeScopeWeights() const
{
	const int frames = int(m_editor->m_document.mesh().frames.size());
	if (!m_rangeScope || !m_rangeScope->isChecked() || frames <= 0)
		return {};
	QVector<double> weights(frames, 0.0);
	const int first = m_first->value(), last = std::min(m_last->value(), frames - 1);
	const auto falloff = ModelFrameFalloff(m_falloff->currentIndex());
	for (int frame = first; frame <= last; ++frame)
		weights[frame] = modelFrameFalloffWeight(falloff, frame - first, last - first + 1);
	return weights;
}

void ModelEditorAnimationPanel::syncPreviewWeights()
{
	// A dragged move, rotate or scale previews the displayed frame's share of
	// the range transform in every view.
	const auto weights = rangeScopeWeights();
	auto views = m_editor->panes();
	if (m_viewport && !views.contains(m_viewport.data()))
		views.append(m_viewport.data());
	for (auto *view : std::as_const(views))
		if (view)
			view->setEditPreviewWeights(weights);
}

void ModelEditorAnimationPanel::refreshAnchors()
{
	if (!m_anchorSummary)
		return;
	const auto &mesh = m_editor->m_document.mesh();
	QStringList lines;
	for (const auto &anchor : mesh.tagAnchors)
	{
		const auto problem = modelTagAnchorProblem(mesh, anchor);
		const int points = int(anchor.vertices.size());
		lines << (problem.isEmpty() ? QCoreApplication::translate("VibeStudioModelEditor", "%1 follows %n vertices of %2.", nullptr, points)
										  .arg(anchor.tag, anchor.surfaceName)
									: problem);
	}
	m_anchorSummary->setText(lines.isEmpty() ? QCoreApplication::translate("VibeStudioModelEditor", "No tag is anchored to mesh components.")
											 : lines.join(QLatin1Char('\n')));
	m_anchorSummary->setAccessibleDescription(m_anchorSummary->text());
}

void ModelEditorAnimationPanel::buildTimeline()
{
	// The transport holds the frame list.
	auto *toolbar = m_editor->m_frame ? qobject_cast<QToolBar *>(m_editor->m_frame->parentWidget()) : nullptr;
	if (!toolbar)
		return;
	m_timeline = new ModelFrameTimeline;
	m_timelineAction = toolbar->addWidget(m_timeline);
	connect(m_timeline, &QAbstractSlider::valueChanged, this,
			[this](int frame)
			{
				if (m_editor->m_refreshing || m_editor->m_working || frame == m_editor->m_frame->currentIndex())
					return;
				m_editor->m_frame->setCurrentIndex(frame);
			});
	m_timeline->setRangeCallback([this](int first, int last) { setFrameRange(first, last); });
}

void ModelEditorAnimationPanel::setFrameRange(int first, int last)
{
	{
		const QSignalBlocker blockFirst(m_first), blockLast(m_last);
		m_first->setValue(std::min(first, last));
		m_last->setValue(std::max(first, last));
	}
	refresh();
}

int ModelEditorAnimationPanel::rangeFirst() const { return m_first->value(); }
int ModelEditorAnimationPanel::rangeLast() const { return m_last->value(); }

QVector<int> ModelEditorAnimationPanel::onionFrames() const
{
	QVector<int> frames;
	const int count = int(m_editor->m_document.mesh().frames.size());
	const int current = m_editor->m_frame->currentIndex();
	if (!m_onion || !m_onion->isChecked() || current < 0)
		return frames;
	const int step = m_onionStep->value();
	for (int i = m_onionBefore->value(); i >= 1; --i)
		if (current - i * step >= 0)
			frames.append(current - i * step);
	for (int i = 1; i <= m_onionAfter->value(); ++i)
		if (current + i * step < count)
			frames.append(current + i * step);
	return frames;
}

void ModelEditorAnimationPanel::refreshOnion()
{
	if (!m_viewport)
		return;
	const bool on = m_onion->isChecked();
	for (auto *field : {m_onionBefore, m_onionAfter, m_onionStep})
		field->setEnabled(on);
	syncOnionPanes();
	const auto frames = onionFrames();
	QStringList shown;
	for (int frame : frames)
		shown.append(QLocale().toString(frame));
	const auto base = m_onion->property("onionDescription").isValid() ? m_onion->property("onionDescription").toString()
																	  : m_onion->accessibleDescription();
	m_onion->setProperty("onionDescription", base);
	m_onion->setAccessibleDescription(frames.isEmpty() ? base
													   : base + QLatin1Char(' ') +
															 QCoreApplication::translate("VibeStudioModelEditor", "Showing frames %1.")
																 .arg(shown.join(QStringLiteral(", "))));
}

void ModelEditorAnimationPanel::syncOnionPanes()
{
	// Every pane of the four views draws the ghosts through its own camera;
	// panes the layout adds later join when the main view next paints.
	QList<QPointer<ModelViewport>> panes;
	if (m_onion->isChecked() && m_viewport)
	{
		for (auto *pane : m_editor->panes())
			panes.append(pane);
		if (!panes.contains(m_viewport))
			panes.append(m_viewport);
	}
	for (const auto &pane : std::as_const(m_onionPanes))
		if (pane && !panes.contains(pane))
			pane->setAnimationOverlayPainter({});
	for (const auto &pane : std::as_const(panes))
		if (pane && !m_onionPanes.contains(pane))
		{
			const QPointer<ModelViewport> view = pane;
			pane->setAnimationOverlayPainter(
				[this, view](QPainter &painter)
				{
					if (view)
						paintOnion(painter, view.data());
				});
		}
	m_onionPanes = panes;
}

void ModelEditorAnimationPanel::paintOnion(QPainter &painter, ModelViewport *view)
{
	if (!m_viewport || !view || m_viewport->isPlaying() || !m_onion->isChecked())
		return;
	if (view == m_viewport)
		syncOnionPanes();
	const auto &mesh = m_editor->m_document.mesh();
	const auto revision = m_editor->m_document.revisionFingerprint();
	if (revision != m_edgeRevision)
	{
		m_edgeRevision = revision;
		m_edges.clear();
		for (const auto &surface : mesh.surfaces)
		{
			QSet<QPair<int, int>> unique;
			for (const auto &t : surface.triangles)
				for (const auto &[a, b] : {std::pair{t.a, t.b}, std::pair{t.b, t.c}, std::pair{t.c, t.a}})
					unique.insert({std::min(a, b), std::max(a, b)});
			auto edges = unique.values().toVector();
			std::sort(edges.begin(), edges.end());
			m_edges.append(edges);
		}
	}
	const int current = m_editor->m_frame->currentIndex();
	const auto colours = m_viewport->palette();
	const bool contrast = m_editor->m_highContrast;
	const int step = std::max(1, m_onionStep->value());
	// Bound the work per paint for very large models.
	qint64 budget = 400000;
	for (int frame : onionFrames())
	{
		const bool earlier = frame < current;
		const int distance = std::max(1, std::abs(frame - current) / step);
		QColor colour = contrast ? colours.color(QPalette::WindowText) : colours.color(earlier ? QPalette::Highlight : QPalette::Link);
		if (!contrast)
			colour.setAlpha(std::max(60, 190 / distance));
		QPen pen(colour, contrast ? 2 : 1, earlier ? Qt::DashLine : Qt::DotLine);
		pen.setCosmetic(true);
		painter.setPen(pen);
		for (int s = 0; s < mesh.surfaces.size() && s < m_edges.size(); ++s)
		{
			const auto &positions = mesh.surfaces[s].frames.value(frame).positions;
			budget -= positions.size();
			if (budget < 0)
				return;
			QVector<QPointF> projected(positions.size());
			QVector<bool> visible(positions.size(), false);
			for (int v = 0; v < positions.size(); ++v)
				visible[v] = view->projectToView(positions[v], &projected[v]);
			QVector<QLineF> lines;
			lines.reserve(m_edges[s].size());
			for (const auto &[a, b] : m_edges[s])
				if (a < visible.size() && b < visible.size() && visible[a] && visible[b])
					lines.append(QLineF(projected[a], projected[b]));
			painter.drawLines(lines);
		}
	}
}

void ModelEditorAnimationPanel::gate(QPushButton *control, const QString &reason, bool editable)
{
	if (!control)
		return;
	const auto description = control->property("frameRangeDescription").toString();
	control->setEnabled(editable && reason.isEmpty());
	control->setToolTip(reason.isEmpty() ? description : reason);
	control->setAccessibleDescription(reason.isEmpty() ? description : reason);
}

ModelEdit ModelEditorAnimationPanel::edit(ModelEditKind kind) const
{
	ModelEdit result;
	result.kind = kind;
	result.selection = m_editor->m_document.selection();
	const int clip = m_editor->m_animationClip ? m_editor->m_animationClip->currentData().toInt() : -1;
	switch (kind)
	{
	case ModelEditKind::MoveFrames:
		result.frameRange.destination = m_destination->value();
		[[fallthrough]];
	case ModelEditKind::DuplicateFrameRange:
	case ModelEditKind::DeleteFrameRange:
	case ModelEditKind::ReverseFrameRange:
		result.frameRange.first = m_first->value();
		result.frameRange.last = m_last->value();
		break;
	case ModelEditKind::RenameFramesFromClips:
		result.frameRange.clip = clip;
		result.frameRange.startNumber = m_startNumber->value();
		result.frameRange.renameClips = m_renameClips->isChecked();
		break;
	case ModelEditKind::ResampleAnimation:
	case ModelEditKind::ResampleSkeletalClip:
		result.resample.clip = kind == ModelEditKind::ResampleAnimation ? clip : m_skeletalClip->currentIndex();
		result.resample.sourceFps = m_sourceFps->value();
		result.resample.targetFps = m_targetFps->value();
		result.resample.targetFrames = m_targetFps->value() > 0 ? 0 : m_targetFrames->value();
		result.resample.method = m_method->currentIndex() == 1 ? ModelResampleMethod::Nearest : ModelResampleMethod::Linear;
		result.resample.cyclic = m_cyclic->isChecked();
		result.resample.bakeCurves = kind == ModelEditKind::ResampleSkeletalClip && m_bakeCurves->isChecked();
		break;
	case ModelEditKind::CloseAnimationLoop:
	case ModelEditKind::CloseSkeletalLoop:
		result.resample.clip = kind == ModelEditKind::CloseAnimationLoop ? clip : m_skeletalClip->currentIndex();
		result.resample.loopClose = ModelLoopClose(m_loopMode->currentIndex());
		result.resample.blendFrames = result.resample.loopClose == ModelLoopClose::CrossFade ? m_blendFrames->value() : 0;
		result.resample.bakeCurves = kind == ModelEditKind::CloseSkeletalLoop && m_bakeCurves->isChecked();
		break;
	case ModelEditKind::TransformFrameRange:
	case ModelEditKind::BlendFramePose:
	case ModelEditKind::ApplyRelativePose:
		if (kind == ModelEditKind::TransformFrameRange)
		{
			const auto geometry = m_editor->operation(ModelEditKind::Transform);
			result.translation = geometry.translation;
			result.rotation = geometry.rotation;
			result.scale = geometry.scale;
			result.pivot = geometry.pivot;
			result.pivotMode = geometry.pivotMode;
			result.pivotFrame = std::max(0, m_editor->m_frame->currentIndex());
		}
		result.poseRange.first = m_first->value();
		result.poseRange.last = m_last->value();
		result.poseRange.falloff = ModelFrameFalloff(m_falloff->currentIndex());
		if (kind == ModelEditKind::BlendFramePose)
		{
			result.poseRange.sourceFrame = m_blendSource->value();
			result.poseRange.amount = m_blendAmount->value();
		}
		else if (kind == ModelEditKind::ApplyRelativePose)
		{
			result.poseRange.sourceFrame = m_poseA->value();
			result.poseRange.baseFrame = m_poseB->value();
			result.poseRange.amount = m_poseWeight->value();
		}
		break;
	case ModelEditKind::FollowTagComponents:
		result.tagFollow.tag = m_editor->m_tagName ? m_editor->m_tagName->text().trimmed() : QString();
		result.tagFollow.mode = ModelTagFollowMode(m_followMode ? m_followMode->currentIndex() : 0);
		result.tagFollow.referenceFrame = m_followReference ? m_followReference->value() : 0;
		if (m_followRange && m_followRange->isChecked())
		{
			result.tagFollow.first = m_first->value();
			result.tagFollow.last = m_last->value();
		}
		result.tagFollow.keepAnchor = !m_followAnchor || m_followAnchor->isChecked();
		break;
	default:
		break;
	}
	return result;
}

void ModelEditorAnimationPanel::refresh()
{
	const auto &mesh = m_editor->m_document.mesh();
	if (mesh.frames.isEmpty() || !m_first)
		return;
	const int frames = int(mesh.frames.size());
	const int current = std::clamp(m_editor->m_frame->currentIndex(), 0, frames - 1);
	{
		for (auto *field : {m_first, m_last, m_destination, m_blendSource, m_poseA, m_poseB, m_followReference})
			if (field)
			{
				const QSignalBlocker blocker(field);
				field->setMaximum(frames - 1);
			}
		const QSignalBlocker blocker(m_importAfter);
		m_importAfter->setMaximum(frames - 1);
		const QSignalBlocker destination(m_destination);
		m_destination->setMaximum(std::max(0, frames - std::max(1, m_last->value() - m_first->value() + 1)));
		const QSignalBlocker blend(m_blendFrames);
		m_blendFrames->setMaximum(std::max(1, frames - 1));
	}
	const bool editable = !m_editor->m_preview->isPlaying() && !m_editor->m_working;
	const auto rangeReason = [&](ModelEditKind kind, bool pingPong)
	{
		auto e = edit(kind);
		e.frameRange.pingPong = pingPong;
		QVector<int> order;
		QString reason;
		if (modelFrameRangeOrder(mesh, e, &order, &reason))
			reason = modelFrameRangeSkeletalRefusal(mesh, e);
		return reason;
	};
	gate(m_move, rangeReason(ModelEditKind::MoveFrames, false), editable);
	gate(m_duplicate, rangeReason(ModelEditKind::DuplicateFrameRange, false), editable);
	gate(m_delete, rangeReason(ModelEditKind::DeleteFrameRange, false), editable);
	gate(m_reverse, rangeReason(ModelEditKind::ReverseFrameRange, false), editable);
	gate(m_pingPong, rangeReason(ModelEditKind::ReverseFrameRange, true), editable);
	gate(m_name,
		 mesh.animations.isEmpty() ? QCoreApplication::translate("VibeStudioModelEditor", "Add a clip under Animation clips first.") : QString(),
		 editable);
	const int clip = m_editor->m_animationClip ? m_editor->m_animationClip->currentData().toInt() : -1;
	const auto chooseClip = QCoreApplication::translate("VibeStudioModelEditor", "Choose a clip under Animation clips first.");
	gate(m_resample, clip < 0 ? chooseClip : rangeReason(ModelEditKind::ResampleAnimation, false), editable);
	gate(m_closeLoop, clip < 0 ? chooseClip : rangeReason(ModelEditKind::CloseAnimationLoop, false), editable);
	m_targetFrames->setEnabled(m_targetFps->value() == 0);
	m_blendFrames->setEnabled(m_loopMode->currentIndex() == int(ModelLoopClose::CrossFade));
	const auto revision = m_editor->m_document.revisionFingerprint();
	if (revision != m_timelineRevision)
	{
		const QSignalBlocker blocked(m_skeletalClip);
		const int wanted = m_skeletalClip->currentIndex();
		m_skeletalClip->clear();
		for (const auto &skeletal : mesh.skeleton.clips)
			m_skeletalClip->addItem(skeletal.name);
		if (!mesh.skeleton.clips.isEmpty())
			m_skeletalClip->setCurrentIndex(std::clamp(wanted, 0, int(mesh.skeleton.clips.size()) - 1));
	}
	const auto noSkeletal = mesh.skeleton.clips.isEmpty()
								? QCoreApplication::translate("VibeStudioModelEditor", "This model has no skeletal clips.")
								: QString();
	m_skeletalClip->setEnabled(noSkeletal.isEmpty());
	m_bakeCurves->setEnabled(noSkeletal.isEmpty());
	gate(m_resampleSkeletal, noSkeletal, editable);
	gate(m_closeSkeletal, noSkeletal, editable);
	const auto &selection = m_editor->m_document.selection();
	const auto selectFirst = componentsSelected(selection)
								 ? QString()
								 : QCoreApplication::translate("VibeStudioModelEditor", "Select vertices, edges or faces first.");
	gate(m_rangeTransform, selectFirst, editable);
	gate(m_blendPose, selectFirst, editable);
	gate(m_relativePose,
		 selectFirst.isEmpty() && m_poseA->value() == m_poseB->value()
			 ? QCoreApplication::translate("VibeStudioModelEditor", "Choose different pose and base frames.")
			 : selectFirst,
		 editable);
	gate(m_importButton, QString(), editable);
	gate(m_exportObj, m_last->value() < m_first->value() ? QCoreApplication::translate("VibeStudioModelEditor", "Choose a frame range first.") : QString(),
		 editable);
	if (m_follow)
	{
		QString reason = selectFirst;
		if (reason.isEmpty() && m_editor->m_tagName && m_editor->m_tagName->text().trimmed().isEmpty())
			reason = QCoreApplication::translate("VibeStudioModelEditor", "Type the tag's name in Name first.");
		gate(m_follow, reason, editable);
		QString stale;
		for (const auto &anchor : mesh.tagAnchors)
			if (stale.isEmpty())
				stale = modelTagAnchorProblem(mesh, anchor);
		gate(m_refitAnchors,
			 mesh.tagAnchors.isEmpty() ? QCoreApplication::translate("VibeStudioModelEditor", "No tag is anchored yet. Fit one with Follow Selection.")
									   : stale,
			 editable);
		if (revision != m_anchorRevision)
		{
			m_anchorRevision = revision;
			refreshAnchors();
		}
	}
	if (m_timeline)
	{
		if (revision != m_timelineRevision)
			m_timeline->setModel(mesh);
		const QSignalBlocker blocked(m_timeline.data());
		m_timeline->setValue(current);
		m_timeline->setFrameRange(m_first->value(), m_last->value());
		m_timeline->setHighContrast(m_editor->m_highContrast);
		m_timeline->setEnabled(!m_editor->m_working);
		// A single pose has nothing to scrub; keep the transport compact.
		if (m_timelineAction)
			m_timelineAction->setVisible(mesh.frames.size() > 1);
	}
	m_timelineRevision = revision;
	if (revision != m_checkedRevision)
	{
		m_checkedRevision = revision;
		const auto summary = modelNativeClipSummary(modelNativeClipRoundTrip(mesh));
		m_clipCheck->setText(summary.isEmpty() ? QCoreApplication::translate("VibeStudioModelEditor",
																			 "No clips yet. MDL, MD2 and MD3 files rebuild clips from frame names.")
											   : summary);
	}
	refreshOnion();
	syncPreviewWeights();
}

bool ModelEditorAnimationPanel::run(ModelEditKind kind, bool pingPong, QString *error)
{
	if (m_editor->m_working || m_editor->m_preview->isPlaying())
		return false;
	auto operation = edit(kind);
	operation.frameRange.pingPong = pingPong;
	const auto &mesh = m_editor->m_document.mesh();
	ModelTagFollowResult follow;
	QString failure;
	if (kind == ModelEditKind::FollowTagComponents && !solveModelTagFollow(mesh, operation.selection, operation.tagFollow, &follow, &failure))
	{
		m_followResult->setText(failure, LiveStatus::Problem);
		m_editor->m_status->setText(failure, LiveStatus::Problem);
		if (error)
			*error = failure;
		return false;
	}
	const int clipFrames = operation.resample.clip >= 0 && operation.resample.clip < mesh.animations.size()
							   ? mesh.animations[operation.resample.clip].frameCount
							   : 0;
	if (!m_editor->applyEdit(operation, &failure))
	{
		m_editor->m_status->setText(failure, LiveStatus::Problem);
		if (error)
			*error = failure;
		return false;
	}
	const auto &after = m_editor->m_document.mesh();
	const int frames = int(after.frames.size());
	const auto &range = operation.frameRange;
	const int length = range.last - range.first + 1;
	int shown = m_editor->m_frame->currentIndex();
	QString message;
	switch (kind)
	{
	case ModelEditKind::MoveFrames:
		shown = range.destination;
		message = QCoreApplication::translate("VibeStudioModelEditor", "Moved %n frames to start at frame %L1.", nullptr, length).arg(shown);
		break;
	case ModelEditKind::DuplicateFrameRange:
		shown = range.last + 1;
		message = QCoreApplication::translate("VibeStudioModelEditor", "Copied %n frames after frame %L1.", nullptr, length).arg(range.last);
		break;
	case ModelEditKind::DeleteFrameRange:
		shown = std::min(range.first, frames - 1);
		message = QCoreApplication::translate("VibeStudioModelEditor", "Deleted %n frames.", nullptr, length);
		break;
	case ModelEditKind::ReverseFrameRange:
		if (pingPong)
		{
			shown = range.last + 1;
			const int added = length - 2;
			message = QCoreApplication::translate("VibeStudioModelEditor", "Added %n return frames after frame %L1.", nullptr, added).arg(range.last);
		}
		else
		{
			shown = range.first;
			message = QCoreApplication::translate("VibeStudioModelEditor", "Reversed %n frames.", nullptr, length);
		}
		break;
	case ModelEditKind::ResampleAnimation:
	case ModelEditKind::CloseAnimationLoop:
	{
		const auto &clip = after.animations.value(operation.resample.clip);
		shown = clip.firstFrame;
		const int count = clip.frameCount;
		message = kind == ModelEditKind::ResampleAnimation
					  ? QCoreApplication::translate("VibeStudioModelEditor", "Clip %1 now has %n frames (was %L2).", nullptr, count)
							.arg(clip.name)
							.arg(clipFrames)
					  : QCoreApplication::translate("VibeStudioModelEditor", "Closed the loop of clip %1.").arg(clip.name);
		break;
	}
	case ModelEditKind::ResampleSkeletalClip:
	case ModelEditKind::CloseSkeletalLoop:
	{
		const auto &clip = after.skeleton.clips.value(operation.resample.clip);
		const int count = int(clip.frames.size());
		message = QCoreApplication::translate("VibeStudioModelEditor", "Skeletal clip %1 now has %n frames.", nullptr, count).arg(clip.name);
		break;
	}
	case ModelEditKind::TransformFrameRange:
	case ModelEditKind::BlendFramePose:
	case ModelEditKind::ApplyRelativePose:
	{
		const int count = operation.poseRange.last - operation.poseRange.first + 1;
		message = QCoreApplication::translate("VibeStudioModelEditor", "Edited the selection in %n frames.", nullptr, count);
		break;
	}
	case ModelEditKind::FollowTagComponents:
		message = modelTagFollowSummary(operation.tagFollow, follow, QLocale());
		m_followResult->setText(message, LiveStatus::Result);
		break;
	case ModelEditKind::RefitAnchoredTags:
	{
		const int count = int(after.tagAnchors.size());
		message = QCoreApplication::translate("VibeStudioModelEditor", "Refitted %n anchored tags.", nullptr, count);
		m_followResult->setText(message, LiveStatus::Result);
		break;
	}
	default:
		message = modelNativeClipSummary(modelNativeClipRoundTrip(after));
		break;
	}
	shown = std::clamp(shown, 0, frames - 1);
	if (m_editor->m_frame->currentIndex() != shown)
		m_editor->m_frame->setCurrentIndex(shown);
	if (isModelFrameRangeEdit(kind) && kind != ModelEditKind::RenameFramesFromClips && kind != ModelEditKind::ResampleAnimation &&
		kind != ModelEditKind::CloseAnimationLoop)
		setFrameRange(std::min(range.first, frames - 1), std::min(range.last, frames - 1));
	m_editor->m_status->setText(message, LiveStatus::Result);
	refresh();
	return true;
}

bool ModelEditorAnimationPanel::importFrames(const QStringList &paths, QString *error)
{
	if (m_editor->m_working || m_editor->m_preview->isPlaying())
		return false;
	ModelEdit operation;
	operation.kind = ModelEditKind::ImportFrames;
	operation.selection = m_editor->m_document.selection();
	auto &o = operation.frameImport;
	o.match = ModelFrameMatch(m_importMatch->currentIndex() == 0 ? int(ModelFrameMatch::Corners)
																   : (m_importMatch->currentIndex() == 1 ? int(ModelFrameMatch::Exact)
																										 : int(ModelFrameMatch::Nearest)));
	o.referenceFrame = std::max(0, m_editor->m_frame->currentIndex());
	if (m_importReplace->isChecked())
		o.replaceFirst = std::max(0, m_importAfter->value());
	else
		o.insertAfter = m_importAfter->value();
	o.clipName = m_importClip->text().trimmed();
	o.clipFps = o.clipName.isEmpty() ? 0 : m_importFps->value();
	ModelObjImportOptions obj;
	obj.up = m_objUp->currentIndex() == 1 ? ModelObjUpAxis::Y : ModelObjUpAxis::Z;
	obj.scale = m_objScale->value();
	const auto sorted = naturalSortedModelFramePaths(paths);
	const auto protectedPath = m_editor->m_document.path();
	const auto added = std::make_shared<int>(0);
	QString failure;
	const bool ok = m_editor->performWork(
		QCoreApplication::translate("VibeStudioModelEditor", "Import Frames"),
		[operation, obj, sorted, protectedPath, added](ModelDocument &candidate, QString *problem, const ModelWorkControl &control)
		{
			auto request = operation;
			if (!readModelFrameSources(sorted, obj, protectedPath, &request.frameImport.sources, &request.frameImport.sourceNames, problem, control))
				return false;
			*added = modelFrameSourceCount(request.frameImport.sources);
			return candidate.edit(request, problem, control);
		},
		&failure);
	m_editor->refresh();
	if (!ok)
	{
		m_editor->m_status->setText(failure, LiveStatus::Problem);
		if (error)
			*error = failure;
		return false;
	}
	const int count = *added;
	const int first = o.replaceFirst >= 0 ? o.replaceFirst : (o.insertAfter < 0 ? int(m_editor->m_document.mesh().frames.size()) - count : o.insertAfter + 1);
	m_editor->m_frame->setCurrentIndex(std::clamp(first, 0, int(m_editor->m_document.mesh().frames.size()) - 1));
	m_editor->m_status->setText(QCoreApplication::translate("VibeStudioModelEditor", "Imported %n frames.", nullptr, count), LiveStatus::Result);
	refresh();
	return true;
}

bool ModelEditorAnimationPanel::exportFramesObj(const QString &pattern, bool overwrite, QString *error)
{
	QStringList written;
	QString failure;
	QStringList protectedFiles;
	if (!m_editor->m_document.path().isEmpty())
		protectedFiles << m_editor->m_document.path();
	if (!exportModelFrameObjSequence(m_editor->m_document.mesh(), m_first->value(), m_last->value(), pattern, overwrite, false, protectedFiles,
									 &written, &failure))
	{
		m_editor->m_status->setText(failure, LiveStatus::Problem);
		if (error)
			*error = failure;
		return false;
	}
	const int count = int(written.size());
	m_editor->m_status->setText(QCoreApplication::translate("VibeStudioModelEditor", "Wrote %n OBJ files.", nullptr, count), LiveStatus::Result);
	return true;
}
} // namespace vibestudio
