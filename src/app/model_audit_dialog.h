#pragma once

// Packages > Audit Models (FMT-11): runs core/model_audit.h on a worker
// thread for a package, folder or installation, with a progress strip,
// Cancel, a summary per format and every entry with its decode issues, and
// saves the JSON report. Read-only: the audit never writes into its source.

#include "core/model_audit.h"

#include <QDialog>
#include <QPointer>

#include <atomic>
#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QThread;
class QTreeWidget;

namespace vibestudio
{

class ModelAuditDialog final : public QDialog
{
  public:
	explicit ModelAuditDialog(const QString &path = {}, QWidget *parent = nullptr);
	~ModelAuditDialog() override;

	// Starts an audit of the path field; false (with the reason in the status)
	// when one is running or the path is empty.
	bool start();
	void cancel();
	[[nodiscard]] bool running() const { return m_thread != nullptr; }
	[[nodiscard]] const ModelAuditReport &report() const { return m_report; }
	// Called on the GUI thread when an audit ends (finished or cancelled).
	std::function<void(const ModelAuditReport &, const QString &error)> finished;

  protected:
	void closeEvent(QCloseEvent *event) override;
	void reject() override;

  private:
	void progress(int done, int total, const QString &label);
	void completed(const ModelAuditReport &report, const QString &error, bool ok);
	void showReport();
	void showEntry();
	void saveReport();
	void setRunning(bool running);
	void waitForWorker();

	QLineEdit *m_path = nullptr;
	QLineEdit *m_formats = nullptr;
	QCheckBox *m_roundTrip = nullptr;
	QPushButton *m_start = nullptr, *m_cancel = nullptr, *m_save = nullptr, *m_close = nullptr;
	QPushButton *m_browsePackage = nullptr, *m_browseFolder = nullptr;
	QLabel *m_status = nullptr;
	QProgressBar *m_progress = nullptr;
	QTableWidget *m_summary = nullptr;
	QComboBox *m_show = nullptr;
	QTreeWidget *m_entries = nullptr;
	QPlainTextEdit *m_details = nullptr;
	QThread *m_thread = nullptr;
	std::shared_ptr<std::atomic_bool> m_cancelled;
	ModelAuditReport m_report;
};

} // namespace vibestudio
