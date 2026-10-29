#pragma once

// The project's own build pipelines (Build > Build Pipelines…): each a name,
// an ordered list of compiler profile stages with arguments, an on/off switch
// and an input, and variables. The preview plans the pipeline against the
// Build page's input, so the command lines are the ones a run would use.

#include "core/build_pipeline.h"

#include <QDialog>
#include <QVector>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTimer;

namespace vibestudio {

class BuildPipelinesDialog final : public QDialog {
	Q_OBJECT

public:
	// `pipelines` are the project's own; Add starts a new one from `starter`,
	// the pipeline chosen on the Build page.
	BuildPipelinesDialog(const QVector<BuildPipelineDescriptor>& pipelines, const BuildPipelineDescriptor& starter, QWidget* parent = nullptr);

	[[nodiscard]] QVector<BuildPipelineDescriptor> pipelines() const;
	[[nodiscard]] QString currentPipelineId() const;
	// The request a run from the Build page would make; the preview plans the
	// pipeline being edited with it.
	void setPreviewRequest(const BuildPipelineRequest& request);
	void selectPipeline(const QString& id);

private:
	void addPipeline();
	void duplicatePipeline();
	void removePipeline();
	void showPipeline(int row);
	void addStage();
	void removeStage();
	void moveStage(int offset);
	void showStage(int row);
	void storeStage();
	void addVariable();
	void removeVariable();
	void storeVariables();
	void refreshPipelineItems();
	void refreshStageItems();
	void refreshState();
	void refreshPreview();
	[[nodiscard]] QStringList problemsFor(int row) const;
	[[nodiscard]] BuildPipelineDescriptor* current();

	QVector<BuildPipelineDescriptor> m_pipelines;
	// Pipelines made in this window: their ids follow their names until saved;
	// a saved pipeline keeps its id, which scripts use.
	QStringList m_createdIds;
	BuildPipelineDescriptor m_starter;
	BuildPipelineRequest m_previewRequest;
	int m_row = -1;
	int m_stageRow = -1;
	bool m_updating = false;
	QListWidget* m_list = nullptr;
	QPushButton* m_duplicate = nullptr;
	QPushButton* m_remove = nullptr;
	QLineEdit* m_name = nullptr;
	QLabel* m_id = nullptr;
	QLineEdit* m_description = nullptr;
	QListWidget* m_stages = nullptr;
	QPushButton* m_stageAdd = nullptr;
	QPushButton* m_stageRemove = nullptr;
	QPushButton* m_stageUp = nullptr;
	QPushButton* m_stageDown = nullptr;
	QLineEdit* m_stageName = nullptr;
	QComboBox* m_stageProfile = nullptr;
	QComboBox* m_stageInput = nullptr;
	QLineEdit* m_stageArguments = nullptr;
	QTableWidget* m_variables = nullptr;
	QPushButton* m_variableAdd = nullptr;
	QPushButton* m_variableRemove = nullptr;
	QPlainTextEdit* m_preview = nullptr;
	QLabel* m_problem = nullptr;
	QDialogButtonBox* m_buttons = nullptr;
	QTimer* m_previewTimer = nullptr;
};

} // namespace vibestudio
