#pragma once

#include "core/model_uv_layout.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace vibestudio
{
// What Export UV Layout… writes: a skin template of some surfaces' UVs.
struct ModelUvLayoutRequest
{
	QVector<int> surfaces;
	QSize size;
	ModelUvLayoutOptions options;
	// "png", "tga" or "pcx", from the path's suffix.
	QString format;
	// A file path, or a package path when staging.
	QString path;
	bool stage = false;
	// The user confirmed replacing an existing file in the save dialog.
	bool overwrite = false;
};

// The settings of Export UV Layout…; ModelEditorDialog renders, encodes and
// writes the layout on its worker after the dialog is accepted.
class ModelUvLayoutDialog final : public QDialog
{
  public:
	ModelUvLayoutDialog(const ModelMesh &mesh, int surface, QSize skinSize, bool canStage, QWidget *parent = nullptr);
	[[nodiscard]] ModelUvLayoutRequest request() const;
	void accept() override;

  private:
	const ModelMesh &m_mesh;
	int m_surface = 0;
	QSize m_skinSize;
	bool m_overwrite = false;
	QSpinBox *m_width = nullptr, *m_height = nullptr;
	QComboBox *m_scope = nullptr, *m_background = nullptr, *m_destination = nullptr;
	QDoubleSpinBox *m_fill = nullptr, *m_lineWidth = nullptr;
	QCheckBox *m_tints = nullptr, *m_seams = nullptr, *m_antialias = nullptr, *m_wrap = nullptr;
	QLineEdit *m_path = nullptr;
	QPushButton *m_browse = nullptr;
	void browse();
	[[nodiscard]] QString problem() const;
};
} // namespace vibestudio
