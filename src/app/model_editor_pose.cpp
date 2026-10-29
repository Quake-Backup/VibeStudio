// Pose clipboard commands of the Mesh Editor: Copy Pose, Paste Pose, Paste
// Pose Mirrored and Mirror Pose (core/model_pose_clipboard.h). They work on the
// skeletal sample the viewport shows: the selected joint with its descendants,
// or every joint when no joint is selected. Pastes are one undoable edit.
#include "app/model_editor_tools.h"

#include "app/model_editor_pose.h"
#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"
#include "core/model_pose_clipboard.h"
#include "core/model_skeleton.h"

#include <QAction>
#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMimeData>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelTools)
};
// The skeletal clip and its sample the viewport frame shows; clip -1 for the
// bind pose frame (sample 0) and for frames outside every clip (sample -1).
QPair<int, int> shownSample(const ModelMesh &mesh, int frame)
{
	const int clip = mesh.skeleton.bakedClipForFrame.value(frame, -2);
	if (clip == -2)
		return {-1, -1};
	if (clip < 0)
		return {-1, 0};
	return {clip, frame - int(mesh.skeleton.bakedClipForFrame.indexOf(clip))};
}
QStringList jointWithDescendants(const ModelSkeleton &skeleton, const QString &name)
{
	const int joint = modelJointIndex(skeleton, name, Qt::CaseSensitive);
	if (joint < 0)
		return {};
	QVector<bool> inside(skeleton.joints.size(), false);
	QStringList result;
	for (int j = joint; j < skeleton.joints.size(); ++j)
	{
		inside[j] = j == joint || (skeleton.joints[j].parent >= 0 && inside[skeleton.joints[j].parent]);
		if (inside[j])
			result.append(skeleton.joints[j].name);
	}
	return result;
}
} // namespace

QByteArray modelPoseClipboardFromSystem()
{
	const auto *data = QGuiApplication::clipboard()->mimeData();
	if (!data)
		return {};
	if (data->hasFormat(QString::fromLatin1(modelPoseClipboardMimeType)))
		return data->data(QString::fromLatin1(modelPoseClipboardMimeType));
	// Copies written to a file or another program arrive as text.
	const auto text = data->text().toUtf8();
	ModelPoseClipboard parsed;
	return parseModelPoseClipboard(text, &parsed) ? text : QByteArray();
}

void modelPoseClipboardToSystem(const QByteArray &bytes)
{
	auto *data = new QMimeData;
	data->setData(QString::fromLatin1(modelPoseClipboardMimeType), bytes);
	data->setText(QString::fromUtf8(bytes));
	QGuiApplication::clipboard()->setMimeData(data);
}

void ModelEditorTools::buildPoseActions()
{
	addAction(QStringLiteral("meshCopyPose"), Text::tr("Copy Pose"), {},
			  [this]
			  {
				  const auto &mesh = m_editor->m_document.mesh();
				  if (mesh.skeleton.isEmpty())
				  {
					  showBanner(Text::tr("This model has no joints, so it has no pose to copy."), {}, LiveStatus::Problem);
					  return;
				  }
				  const auto shown = shownSample(mesh, m_viewport->frame());
				  if (shown.second < 0)
				  {
					  showBanner(Text::tr("Show a skeletal clip frame or the bind pose to copy its pose."), {}, LiveStatus::Problem);
					  return;
				  }
				  ModelPoseCopyOptions options;
				  options.clip = shown.first;
				  options.first = options.last = shown.second;
				  options.joints = jointWithDescendants(mesh.skeleton, m_editor->m_document.selection().joint);
				  ModelPoseClipboard clipboard;
				  QString error;
				  if (!copyModelPose(mesh, options, &clipboard, &error))
				  {
					  showBanner(error, {}, LiveStatus::Problem);
					  return;
				  }
				  modelPoseClipboardToSystem(modelPoseClipboardBytes(clipboard));
				  showBanner(Text::tr("Copied. %1").arg(modelPoseClipboardSummary(clipboard)));
			  });
	const auto paste = [this](bool mirrored, bool inPlace)
	{
		const auto &mesh = m_editor->m_document.mesh();
		const auto shown = shownSample(mesh, m_viewport->frame());
		if (mesh.skeleton.isEmpty() || shown.first < 0)
		{
			showBanner(Text::tr("Show a frame of a skeletal clip to paste a pose into it."), {}, LiveStatus::Problem);
			return;
		}
		ModelEdit edit;
		edit.kind = ModelEditKind::PastePose;
		edit.selection = m_editor->m_document.selection();
		auto &o = edit.animation.paste;
		o.clip = shown.first;
		o.first = shown.second;
		// A paste at the shown sample replaces keys there, as setting a key would.
		o.replaceKeys = true;
		QString error;
		if (inPlace)
		{
			ModelPoseCopyOptions options;
			options.clip = shown.first;
			options.first = options.last = shown.second;
			ModelPoseClipboard clipboard;
			if (!copyModelPose(mesh, options, &clipboard, &error))
			{
				showBanner(error, {}, LiveStatus::Problem);
				return;
			}
			o.clipboard = modelPoseClipboardBytes(clipboard);
		}
		else
		{
			o.clipboard = modelPoseClipboardFromSystem();
			if (o.clipboard.isEmpty())
			{
				showBanner(Text::tr("The clipboard holds no copied pose. Use Copy Pose first."), {}, LiveStatus::Problem);
				return;
			}
		}
		if (mirrored)
		{
			o.mirrorAxis = detectModelPoseMirrorAxis(mesh, &o.mirrorJoints);
			if (o.mirrorAxis < 0)
			{
				showBanner(Text::tr("No left/right joint pairs were found, so the mirror is unknown. Name joints with L/R or Left/Right, "
									"or use model pose --operation paste with --mirror-joints."),
						   {}, LiveStatus::Problem);
				return;
			}
		}
		if (!m_editor->applyEdit(edit, &error))
		{
			showBanner(error, {}, LiveStatus::Problem);
			return;
		}
		const QStringList axes{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
		const int pairs = int(o.mirrorJoints.size()) / 2;
		showBanner(inPlace	   ? Text::tr("Mirrored the pose across %1 with %n joint pair(s).", nullptr, pairs).arg(axes.value(o.mirrorAxis))
				   : mirrored ? Text::tr("Pasted the pose mirrored across %1 with %n joint pair(s).", nullptr, pairs).arg(axes.value(o.mirrorAxis))
							  : Text::tr("Pasted the pose."));
	};
	addAction(QStringLiteral("meshPastePose"), Text::tr("Paste Pose"), {}, [paste] { paste(false, false); });
	addAction(QStringLiteral("meshPastePoseMirrored"), Text::tr("Paste Pose Mirrored"), {}, [paste] { paste(true, false); });
	addAction(QStringLiteral("meshMirrorPose"), Text::tr("Mirror Pose"), {}, [paste] { paste(true, true); });
}
} // namespace vibestudio
