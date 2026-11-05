#pragma once

#include "core/level_map_obj_export.h"

#include <QCoreApplication>
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace vibestudio {

// Export Map as OBJ's options, as TrenchBroom's OBJ export dialog and
// NetRadiant Custom's brushexport window offer them: the file, how faces are
// grouped, objects or groups, welding, triangles, the material library and its
// paths, short names, axes and the ignored materials. Export accepts.
class LevelMapObjExportDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(LevelMapObjExportDialog)
  public:
	// `summary` says what is exported, above the options.
	LevelMapObjExportDialog(const QString& path, const LevelMapObjExportOptions& options, const QString& summary, QWidget* parent = nullptr);

	// The chosen file, with .obj added when it has another extension.
	[[nodiscard]] QString path() const;
	// The options the window started with, as the controls set them.
	[[nodiscard]] LevelMapObjExportOptions options() const;

  private:
	void refresh();

	LevelMapObjExportOptions m_options;
	QLineEdit* m_path = nullptr;
	QComboBox* m_grouping = nullptr;
	QCheckBox* m_objects = nullptr;
	QCheckBox* m_weld = nullptr;
	QCheckBox* m_triangulate = nullptr;
	QCheckBox* m_library = nullptr;
	QCheckBox* m_shortNames = nullptr;
	QCheckBox* m_zUp = nullptr;
	QComboBox* m_materialPaths = nullptr;
	QPlainTextEdit* m_ignored = nullptr;
	QLabel* m_replace = nullptr;
	QPushButton* m_export = nullptr;
};

} // namespace vibestudio
