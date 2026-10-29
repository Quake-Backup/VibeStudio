#pragma once

#include "app/live_status_label.h"
#include "core/model_document.h"

#include <QDialog>
#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace vibestudio
{
class ModelViewport;
// Skeleton > Import Animation: reads a clip from another file (md5anim, IQM,
// MDS, MDR, MDX, GLA, or another editable source) in a cancellable worker,
// maps its joints onto this rig (same names by default) and reviews the
// import plan before Apply returns the ImportSkeletalClip edit. Update
// Preview imports into a copy on the worker and shows the source and the
// result side by side at the same sample.
class ModelClipImportDialog final : public QDialog
{
  public:
	explicit ModelClipImportDialog(const ModelMesh &mesh, QWidget *parent = nullptr);
	// Reads the source; false with the reason shown in the status line. The
	// review line then says whether the import can be applied (ready()).
	bool loadSource(const QString &path);
	[[nodiscard]] ModelEdit edit() const;
	[[nodiscard]] bool ready() const
	{
		return m_ready;
	}
	void reject() override;
	// Imports into a copy of the model on the worker for the side-by-side
	// preview; false with the reason in the status line.
	bool updatePreview();
	[[nodiscard]] bool previewed() const
	{
		return m_previewed;
	}

  private:
	ModelMesh m_mesh, m_source, m_result;
	QString m_sourcePath;
	bool m_ready = false, m_busy = false, m_previewed = false;
	std::function<void()> m_cancel;
	QLineEdit *m_path = nullptr, *m_name = nullptr;
	QComboBox *m_clip = nullptr, *m_mode = nullptr;
	QDoubleSpinBox *m_fps = nullptr, *m_rootScale = nullptr;
	QCheckBox *m_fill = nullptr, *m_curves = nullptr, *m_events = nullptr;
	QTableWidget *m_mapping = nullptr;
	LiveStatusLabel *m_status = nullptr;
	QPushButton *m_apply = nullptr, *m_choose = nullptr, *m_preview = nullptr;
	QSpinBox *m_sample = nullptr;
	ModelViewport *m_sourceView = nullptr, *m_resultView = nullptr;
	void fillMapping();
	void changed();
	void showSamples();
	[[nodiscard]] ModelClipImportOptions options() const;
};
} // namespace vibestudio
