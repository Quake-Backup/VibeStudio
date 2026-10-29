#pragma once
#include "app/live_status_label.h"
#include "core/model_document.h"
#include <QDialog>
#include <functional>
class QCheckBox;
class QComboBox;
class QFormLayout;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;
class QPushButton;
class QTabWidget;
namespace vibestudio
{
class ModelViewport;
class ModelIkTable;
class ModelIkDialog final : public QDialog
{
  public:
	ModelIkDialog(const ModelMesh &mesh, const QString &joint, int frame, QWidget *parent = nullptr);
	ModelEdit edit() const
	{
		return m_reviewed;
	}
	bool ready() const
	{
		return m_ready;
	}
	bool busy() const
	{
		return m_busy;
	}
	const ModelMesh &previewMesh() const
	{
		return m_result;
	}
	void review();
	void reject() override;
	// The editor's 3D cursor, offered as a target or pole placed in the viewport.
	void setCursorPoint(const ModelVec3 &point);
	// The preview's drag handle: 0 none, 1 the target, 2 the pole. The handle
	// is the viewports' shared move gizmo; dropping it sets the point.
	void setHandle(int handle);
	// Where the target (in model space, following a joint at the preview
	// sample) and the pole are.
	[[nodiscard]] ModelVec3 targetPoint() const;
	[[nodiscard]] ModelVec3 polePoint() const;
	[[nodiscard]] ModelViewport *preview() const
	{
		return m_preview;
	}

  private:
	ModelMesh m_source, m_result;
	ModelEdit m_reviewed;
	bool m_ready = false, m_busy = false, m_closeAfter = false;
	std::function<void()> m_cancel;
	QComboBox *m_clip = nullptr, *m_end = nullptr, *m_reach = nullptr, *m_orientation = nullptr, *m_view = nullptr, *m_handle = nullptr;
	// Chain solves and target sources.
	QComboBox *m_solver = nullptr, *m_root = nullptr, *m_targetSource = nullptr, *m_targetJoint = nullptr;
	QSpinBox *m_iterations = nullptr;
	QCheckBox *m_limits = nullptr, *m_usePole = nullptr;
	QPushButton *m_targetCursor = nullptr, *m_poleCursor = nullptr;
	QFormLayout *m_form = nullptr;
	ModelVec3 m_cursor;
	bool m_hasCursor = false;
	void endChanged();
	QSpinBox *m_first = nullptr, *m_last = nullptr, *m_sample = nullptr;
	QDoubleSpinBox *m_target[3]{}, *m_pole[3]{};
	QLabel *m_chain = nullptr;
	LiveStatusLabel *m_status = nullptr;
	QPushButton *m_apply = nullptr, *m_review = nullptr;
	QTabWidget *m_tabs = nullptr;
	ModelViewport *m_preview = nullptr;
	ModelIkTable *m_table = nullptr;
	ModelEdit request() const;
	void changed();
	void clipChanged();
	void usePose();
	void showPreview();
	void updateHandle();
	void dropHandle(const ModelVec3 &point);
};
} // namespace vibestudio
