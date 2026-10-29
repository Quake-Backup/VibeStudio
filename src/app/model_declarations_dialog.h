#pragma once

#include "app/live_status_label.h"
#include "core/model_game_declarations.h"

#include <QDialog>
#include <functional>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace vibestudio
{
// Skeleton > Game Declarations: previews the Doom 3 model declaration, QuakeC
// $frame macros, Quake II frame header or GZDoom MODELDEF generated from the
// model, with Copy, Save As and (Doom 3) Export Model Set, which writes the
// md5mesh, every clip's md5anim and the .def under a folder in a worker.
class ModelDeclarationsDialog final : public QDialog
{
  public:
	// `sourceName` names the source in the generated comment.
	ModelDeclarationsDialog(const ModelMesh &mesh, const QString &sourceName, QWidget *parent = nullptr);
	[[nodiscard]] QString text() const
	{
		return m_text;
	}
	// The target's index in modelGameDeclarationTargets().
	void setTarget(int target);
	// Writes the Doom 3 model set under `folder`; false with the reason shown.
	bool exportModelSet(const QString &folder);
	// Writes the preview to `path`; false with the reason shown.
	bool saveText(const QString &path);
	void reject() override;

  private:
	ModelMesh m_mesh;
	QString m_sourceName, m_text;
	bool m_busy = false;
	std::function<void()> m_cancel;
	QComboBox *m_target = nullptr;
	QLineEdit *m_name = nullptr, *m_path = nullptr, *m_model = nullptr, *m_skin = nullptr, *m_sprite = nullptr;
	QPlainTextEdit *m_preview = nullptr;
	LiveStatusLabel *m_status = nullptr;
	QPushButton *m_copy = nullptr, *m_save = nullptr, *m_set = nullptr;
	void refresh();
	[[nodiscard]] ModelDoom3DefOptions doom3() const;
};
} // namespace vibestudio
