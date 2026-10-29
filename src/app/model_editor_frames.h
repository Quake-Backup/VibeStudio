#pragma once

#include "core/model_document.h"

#include <QHash>
#include <QObject>
#include <QPointer>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLineEdit;
class QPainter;
class QPushButton;
class QSpinBox;
class QWidget;

namespace vibestudio
{
class LiveStatusLabel;
class ModelEditorDialog;
class ModelFrameTimeline;
class ModelViewport;

// The Mesh Editor's vertex-animation tools on the Animation page, owned by
// the dialog (a friend) and created after its clip and tag controls:
// - Frame range: move, duplicate, delete, reverse, ping-pong, frame naming
//   after clips and the native clip check (core/model_frame_tools.h);
// - the frame timeline beside the transport and the viewport onion skin;
// - Resample and loop for vertex and skeletal clips;
// - Range sculpt: range transforms, partial and relative pose blends;
// - Import frames from OBJ sequences and other models, and OBJ sequence
//   export (core/model_frame_import.h);
// - Follow Selection in Attachment tags (core/model_tag_follow.h).
// Every edit goes through ModelEditorDialog::applyEdit or its worker, so
// undo, recovery and saves cover it.
class ModelEditorAnimationPanel final : public QObject
{
  public:
	ModelEditorAnimationPanel(ModelEditorDialog *editor, QFormLayout *animation);
	~ModelEditorAnimationPanel() override;
	// Called from ModelEditorDialog::refreshAnimationControls.
	void refresh();
	// The range the range operations use; the timeline sets it.
	void setFrameRange(int first, int last);
	[[nodiscard]] int rangeFirst() const;
	[[nodiscard]] int rangeLast() const;
	// Runs one operation with the panel's settings (also the buttons' path).
	bool run(ModelEditKind kind, bool pingPong = false, QString *error = nullptr);
	// Reads `paths` (natural order) and imports their frames with the
	// panel's settings, on the document worker.
	bool importFrames(const QStringList &paths, QString *error = nullptr);
	// Writes the frame range as an OBJ sequence named by `pattern`.
	bool exportFramesObj(const QString &pattern, bool overwrite, QString *error = nullptr);
	// The frames the onion skin draws, before then after the current frame.
	[[nodiscard]] QVector<int> onionFrames() const;

  private:
	QPushButton *addButton(QFormLayout *form, const char *id, const QString &label, const QString &description);
	QGroupBox *addGroup(QFormLayout *animation, int *row, const char *id, const QString &title, QFormLayout **form);
	void buildFrameRange(QFormLayout *animation, int *row);
	void buildResample(QFormLayout *animation, int *row);
	void buildSculpt(QFormLayout *animation, int *row);
	void buildImport(QFormLayout *animation, int *row);
	void buildTagFollow();
	void buildTimeline();
	void gate(QPushButton *control, const QString &reason, bool editable);
	void paintOnion(QPainter &painter);
	void refreshOnion();
	ModelEdit edit(ModelEditKind kind) const;
	ModelEditorDialog *m_editor = nullptr;
	QPointer<ModelViewport> m_viewport;
	QWidget *m_page = nullptr;
	QPointer<ModelFrameTimeline> m_timeline;
	QSpinBox *m_first = nullptr, *m_last = nullptr, *m_destination = nullptr, *m_startNumber = nullptr;
	QCheckBox *m_renameClips = nullptr;
	QPushButton *m_move = nullptr, *m_duplicate = nullptr, *m_delete = nullptr, *m_reverse = nullptr, *m_pingPong = nullptr,
				*m_name = nullptr;
	LiveStatusLabel *m_clipCheck = nullptr;
	QCheckBox *m_onion = nullptr;
	QSpinBox *m_onionBefore = nullptr, *m_onionAfter = nullptr, *m_onionStep = nullptr;
	QDoubleSpinBox *m_targetFps = nullptr, *m_sourceFps = nullptr;
	QSpinBox *m_targetFrames = nullptr, *m_blendFrames = nullptr;
	QComboBox *m_method = nullptr, *m_loopMode = nullptr, *m_skeletalClip = nullptr;
	QCheckBox *m_cyclic = nullptr, *m_bakeCurves = nullptr;
	QPushButton *m_resample = nullptr, *m_closeLoop = nullptr, *m_resampleSkeletal = nullptr, *m_closeSkeletal = nullptr;
	QComboBox *m_falloff = nullptr;
	QSpinBox *m_blendSource = nullptr, *m_poseA = nullptr, *m_poseB = nullptr;
	QDoubleSpinBox *m_blendAmount = nullptr, *m_poseWeight = nullptr;
	QPushButton *m_rangeTransform = nullptr, *m_blendPose = nullptr, *m_relativePose = nullptr;
	QComboBox *m_importMatch = nullptr, *m_objUp = nullptr;
	QSpinBox *m_importAfter = nullptr;
	QCheckBox *m_importReplace = nullptr;
	QLineEdit *m_importClip = nullptr;
	QDoubleSpinBox *m_importFps = nullptr, *m_objScale = nullptr;
	QPushButton *m_importButton = nullptr, *m_exportObj = nullptr;
	QComboBox *m_followMode = nullptr;
	QSpinBox *m_followReference = nullptr;
	QCheckBox *m_followRange = nullptr;
	QPushButton *m_follow = nullptr;
	LiveStatusLabel *m_followResult = nullptr;
	QByteArray m_checkedRevision, m_timelineRevision, m_edgeRevision;
	// Unique edges per surface for the onion skin, rebuilt per revision.
	QVector<QVector<QPair<int, int>>> m_edges;
};
} // namespace vibestudio
