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
class QTableWidget;

namespace vibestudio
{
// Skeleton > Import Animation: reads a clip from another file (md5anim, IQM,
// MDS, MDR, MDX, GLA, or another editable source) in a cancellable worker,
// maps its joints onto this rig (same names by default) and reviews the
// import plan before Apply returns the ImportSkeletalClip edit.
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

  private:
	ModelMesh m_mesh, m_source;
	QString m_sourcePath;
	bool m_ready = false, m_busy = false;
	std::function<void()> m_cancel;
	QLineEdit *m_path = nullptr, *m_name = nullptr;
	QComboBox *m_clip = nullptr, *m_mode = nullptr;
	QDoubleSpinBox *m_fps = nullptr, *m_rootScale = nullptr;
	QCheckBox *m_fill = nullptr;
	QTableWidget *m_mapping = nullptr;
	LiveStatusLabel *m_status = nullptr;
	QPushButton *m_apply = nullptr, *m_choose = nullptr;
	void fillMapping();
	void changed();
	[[nodiscard]] ModelClipImportOptions options() const;
};
} // namespace vibestudio
