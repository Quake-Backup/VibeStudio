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
// Skeleton > Constraints and Layers: a clip's persistent constraints (IK, aim,
// child of, copy transform, foot lock) and animation layers. Each button
// returns one EditClipStack edit: add, update or remove the selected entry,
// or bake the stack into samples.
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

  private:
	ModelMesh m_mesh;
	ModelEdit m_edit;
	QComboBox *m_clip = nullptr, *m_kind = nullptr, *m_joint = nullptr, *m_root = nullptr, *m_target = nullptr, *m_axis = nullptr;
	QListWidget *m_constraints = nullptr, *m_layers = nullptr;
	std::array<QDoubleSpinBox *, 3> m_point{}, m_pole{};
	QSpinBox *m_first = nullptr, *m_last = nullptr;
	QDoubleSpinBox *m_weight = nullptr, *m_layerWeight = nullptr;
	QCheckBox *m_usePole = nullptr, *m_enabled = nullptr, *m_layerEnabled = nullptr;
	QComboBox *m_layerMode = nullptr;
	QLineEdit *m_name = nullptr;
	LiveStatusLabel *m_status = nullptr;
	QPushButton *m_update = nullptr, *m_remove = nullptr, *m_bake = nullptr, *m_updateLayer = nullptr, *m_removeLayer = nullptr;
	void loadClip();
	void loadConstraint();
	void loadLayer();
	void refresh();
	[[nodiscard]] int clip() const;
};
} // namespace vibestudio
