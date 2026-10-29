#pragma once

#include "app/live_status_label.h"
#include "core/model_document.h"

#include <QDialog>
#include <array>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace vibestudio
{
class ModelAnimationCurveView;
// Skeleton > Constraints and Layers: a clip's persistent constraints (IK, aim,
// child of, copy transform, foot lock) and animation layers. Each button
// returns one EditClipStack edit: add, update or remove the selected entry,
// or bake the stack into samples. Layers are keyed here: the selected layer
// (or a new one) is a draft whose joint channels and weight keys Set Key,
// Remove Key and the graph change before Add Layer or Update Layer.
class ModelConstraintsDialog final : public QDialog
{
  public:
	ModelConstraintsDialog(const ModelMesh &mesh, int frame, QWidget *parent = nullptr);
	[[nodiscard]] ModelEdit edit() const
	{
		return m_edit;
	}
	// The constraint the form describes.
	[[nodiscard]] ModelClipConstraint formConstraint() const;
	// Sets m_edit for `action` and accepts when the form allows it.
	void choose(ModelClipStackAction action);
	// Layer draft keys: channels 0-8 of a joint (tx ... sz), or 9 for the
	// layer's weight keys (any joint). False with the reason shown.
	bool setLayerKey(const QString &joint, int channel, int sample, double value);
	bool removeLayerKey(const QString &joint, int channel, int sample);
	[[nodiscard]] const ModelClipLayer &layerDraft() const
	{
		return m_layerDraft;
	}

  private:
	ModelMesh m_mesh;
	ModelEdit m_edit;
	QComboBox *m_clip = nullptr, *m_kind = nullptr, *m_joint = nullptr, *m_root = nullptr, *m_target = nullptr, *m_axis = nullptr;
	QListWidget *m_constraints = nullptr, *m_layers = nullptr;
	std::array<QDoubleSpinBox *, 3> m_point{}, m_pole{};
	QSpinBox *m_first = nullptr, *m_last = nullptr;
	QDoubleSpinBox *m_weight = nullptr, *m_layerWeight = nullptr;
	QCheckBox *m_usePole = nullptr, *m_enabled = nullptr, *m_layerEnabled = nullptr, *m_limits = nullptr;
	QComboBox *m_layerMode = nullptr, *m_layerJoint = nullptr, *m_layerChannel = nullptr;
	QLineEdit *m_name = nullptr, *m_weightKeys = nullptr, *m_layerName = nullptr;
	QSpinBox *m_keySample = nullptr;
	QDoubleSpinBox *m_keyValue = nullptr;
	ModelAnimationCurveView *m_layerCurve = nullptr;
	ModelClipLayer m_layerDraft;
	LiveStatusLabel *m_status = nullptr;
	QPushButton *m_update = nullptr, *m_remove = nullptr, *m_bake = nullptr, *m_updateLayer = nullptr, *m_removeLayer = nullptr,
				*m_addLayer = nullptr, *m_setKey = nullptr, *m_removeKey = nullptr;
	void loadClip();
	void loadConstraint();
	void loadLayer();
	void refresh();
	void showLayerCurve(int selected = -1);
	bool layerGroup(const QString &joint, ModelPoseCurves *group, QString *error) const;
	[[nodiscard]] QVector<ModelPoseCurveKey> layerKeys(const QString &joint, int channel) const;
	[[nodiscard]] ModelClipLayer formLayer() const;
	[[nodiscard]] int clip() const;
	[[nodiscard]] int frameCount() const;
};
} // namespace vibestudio
