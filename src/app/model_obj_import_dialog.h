#pragma once

#include "core/model_obj.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;

namespace vibestudio
{
// OBJ import settings for the Mesh Editor's Open or Import Model: up axis,
// scale, material libraries and loose data (core/model_obj.h). Opening an
// OBJ asks for them, starting from the last settings chosen this session.
class ModelObjImportDialog final : public QDialog
{
  public:
	explicit ModelObjImportDialog(QWidget *parent = nullptr);
	[[nodiscard]] ModelObjImportOptions options() const;
	void setOptions(const ModelObjImportOptions &options);
	// Shows the dialog; false when cancelled. Remembers the choice for the
	// rest of the session.
	static bool ask(QWidget *parent, ModelObjImportOptions *options);

  private:
	QComboBox *m_up = nullptr, *m_materials = nullptr, *m_loose = nullptr;
	QDoubleSpinBox *m_scale = nullptr;
};
} // namespace vibestudio
