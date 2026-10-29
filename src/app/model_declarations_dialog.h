#pragma once

#include "app/live_status_label.h"
#include "core/model_game_declarations.h"

#include <QDialog>
#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace vibestudio
{
// Skeleton > Game Declarations: previews the Doom 3 model declaration, QuakeC
// $frame macros, Quake II frame header, GZDoom MODELDEF or (for a KVX voxel)
// GZDoom VOXELDEF generated from the model, with Copy, Save As and, for Doom 3,
// Export Model Set, which writes the md5mesh, every clip's md5anim and the
// .def under a folder in a worker, and Stage in Package, which stages the same
// files into the active package as one package step.
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
	// Builds the Doom 3 model set in a worker and hands it to the stager; false
	// with the reason shown.
	bool stageModelSet();
	// Set by the editor when a package is open for staging: stages the files,
	// replacing files already there when the bool is set. Shows Stage in
	// Package.
	void setStager(std::function<bool(const QVector<ModelGameFile> &, bool, QString *)> stager);
	void reject() override;

  private:
	ModelMesh m_mesh;
	QString m_sourceName, m_text;
	bool m_busy = false;
	std::function<void()> m_cancel;
	QComboBox *m_target = nullptr;
	QFormLayout *m_form = nullptr;
	QLineEdit *m_name = nullptr, *m_path = nullptr, *m_model = nullptr, *m_skin = nullptr, *m_sprite = nullptr, *m_sprites = nullptr;
	// VOXELDEF options.
	QDoubleSpinBox *m_scale = nullptr, *m_angle = nullptr;
	QSpinBox *m_spin = nullptr;
	QCheckBox *m_palette = nullptr, *m_replace = nullptr;
	QPlainTextEdit *m_preview = nullptr;
	LiveStatusLabel *m_status = nullptr;
	QPushButton *m_copy = nullptr, *m_save = nullptr, *m_set = nullptr, *m_stage = nullptr;
	std::function<bool(const QVector<ModelGameFile> &, bool, QString *)> m_stager;
	void refresh();
	[[nodiscard]] ModelDoom3DefOptions doom3() const;
};
} // namespace vibestudio
