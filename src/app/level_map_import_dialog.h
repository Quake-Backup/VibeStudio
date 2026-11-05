#pragma once

#include "core/compiler_profiles.h"
#include "core/level_map_transfer.h"

#include <QCoreApplication>
#include <QDialog>
#include <memory>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTemporaryDir;
class QTimer;

namespace vibestudio {
struct LevelMapImportTask;

// Import Map's preview: reads the chosen map on a worker, first converting a
// .bsp with VibeMap3's convert profile, then shows what the import adds and
// which entity names it renames. Import accepts; Cancel stops the worker.
// The insertion itself runs afterwards as an ImportMap placement.
class LevelMapImportDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(LevelMapImportDialog)
  public:
	// `conversion` carries the project's game and installation folders and the
	// compiler context (tool overrides, search paths and workspace) for a .bsp.
	LevelMapImportDialog(const LevelMapDocument& destination, const QString& path, const LevelMapBspConversionContext& conversion,
		QWidget* parent = nullptr);
	~LevelMapImportDialog() override;

	[[nodiscard]] bool ready() const { return m_ready; }
	[[nodiscard]] std::shared_ptr<const LevelMapDocument> imported() const { return m_imported; }
	[[nodiscard]] LevelMapImportOptions options() const;
	[[nodiscard]] const LevelMapImportPreview& preview() const { return m_preview; }
	// Why the map cannot be imported, once reading ends; empty otherwise.
	[[nodiscard]] QString problem() const { return m_problem; }
	void reject() override;

  private:
	void start();
	void poll();
	void finish();
	void refreshPreview();

	LevelMapDocument m_destination;
	QString m_path;
	LevelMapBspConversionContext m_conversion;
	std::shared_ptr<LevelMapImportTask> m_task;
	// The size of the map read, measured on the worker.
	qint64 m_bytes = -1;
	std::unique_ptr<QTemporaryDir> m_converted;
	std::shared_ptr<const LevelMapDocument> m_imported;
	LevelMapImportPreview m_preview;
	QString m_problem;
	bool m_ready = false;
	QLabel* m_status = nullptr;
	QProgressBar* m_progress = nullptr;
	QLabel* m_summary = nullptr;
	QLabel* m_warnings = nullptr;
	QCheckBox* m_rename = nullptr;
	QPlainTextEdit* m_details = nullptr;
	QPushButton* m_import = nullptr;
	QTimer* m_poll = nullptr;
};
} // namespace vibestudio
