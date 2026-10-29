#pragma once

// UV > Bake Maps… (UVSKIN-06): bakes ambient occlusion, normal or lighting
// maps (core/model_bake.h) for the open model on a worker thread, with
// progress, Cancel, a preview and Save. Apply to Model hands the map to the
// editor's handler (shading the skin by Strength, or staging a normal map
// beside it); without one the dialog never changes the model.

#include "core/model_bake.h"

#include <QDialog>

#include <atomic>
#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QThread;

namespace vibestudio
{

class ModelBakeDialog final : public QDialog
{
  public:
	ModelBakeDialog(const ModelMesh &mesh, int surface, int frame, QWidget *parent = nullptr);
	~ModelBakeDialog() override;

	bool start();
	void cancel();
	[[nodiscard]] bool running() const { return m_thread != nullptr; }
	[[nodiscard]] const ModelBakeResult &result() const { return m_result; }
	// Saves the last bake as PNG or TGA; false with the reason in the status.
	bool saveTo(const QString &path);
	// Apply to Model: what it does (its tooltip) and the editor's handler,
	// given the map, its type and the shade strength.
	void setApplyTarget(const QString &description, std::function<bool(const QImage &, ModelBakeType, double, QString *)> apply);
	bool applyToModel();

  protected:
	void reject() override;

  private:
	void finished(const ModelBakeResult &result, const QString &error, bool ok);
	void setRunning(bool running);
	void waitForWorker();

	ModelMesh m_mesh;
	int m_surface = 0;
	int m_frame = 0;
	QComboBox *m_type = nullptr, *m_size = nullptr, *m_scope = nullptr;
	QSpinBox *m_samples = nullptr, *m_padding = nullptr, *m_seed = nullptr;
	QDoubleSpinBox *m_distance = nullptr, *m_cage = nullptr, *m_strength = nullptr;
	QPushButton *m_apply = nullptr;
	std::function<bool(const QImage &, ModelBakeType, double, QString *)> m_applyHandler;
	ModelBakeType m_resultType = ModelBakeType::AmbientOcclusion;
	QLineEdit *m_high = nullptr;
	QPushButton *m_browse = nullptr, *m_bake = nullptr, *m_cancel = nullptr, *m_save = nullptr, *m_close = nullptr;
	QProgressBar *m_progress = nullptr;
	QLabel *m_status = nullptr, *m_preview = nullptr;
	QThread *m_thread = nullptr;
	std::shared_ptr<std::atomic_bool> m_cancelled;
	ModelBakeResult m_result;
	ModelBakeOptions m_lastOptions;
};

} // namespace vibestudio
