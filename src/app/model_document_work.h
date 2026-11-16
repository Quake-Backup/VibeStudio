#pragma once

#include "core/model_document.h"

#include <functional>

class QWidget;

namespace vibestudio
{
using ModelDocumentJob = std::function<bool(ModelDocument &candidate, QString *error, const ModelWorkControl &control)>;
using ModelTask = std::function<bool(QString *error, const ModelWorkControl &control)>;

// Shared cancellable worker/progress surface for mesh and assembly services.
// The caller owns its candidate values and adopts them only after success.
bool runModelTask(QWidget *parent, const QString &title, ModelTask task, QString *error = nullptr, bool durableWrite = false,
				  std::function<void()> *cancelAction = nullptr);

// Runs a value-only worker while servicing the UI event loop. The caller keeps
// document-mutating controls disabled for this call. A delayed window-modal
// progress surface provides cancellation; only a successful candidate is adopted.
// A completed durable write remains successful if cancellation arrives late.
bool runModelDocumentWork(QWidget *parent, const QString &title, ModelDocument *document, ModelDocumentJob job, QString *error = nullptr,
						  bool durableWrite = false, std::function<void()> *cancelAction = nullptr);

// Hears every worker the functions above run, so the window's activity
// indicator counts it (app/studio_progress.h, "Visible Work" in
// docs/UX_DESIGN.md): `begin` takes the title and returns a token (0 for
// none), `progress` gets per mille done or -1 while there is no total, and
// `end` closes the token, whatever way the work ended. The Mesh Editor
// installs the studio's work tracker; without a reporter nothing is told.
struct ModelWorkReporter
{
	std::function<int(const QString &title)> begin;
	std::function<void(int token, int perMille)> progress;
	std::function<void(int token)> end;
};
void setModelWorkReporter(ModelWorkReporter reporter);
} // namespace vibestudio
