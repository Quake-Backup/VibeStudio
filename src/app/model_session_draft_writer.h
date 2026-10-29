#pragma once

#include "core/model_session_draft.h"

#include <QObject>
#include <functional>
#include <memory>
#include <optional>

class QThread;

namespace vibestudio
{

// Writes staged-session sidecars (core/model_session_draft.h) off the UI
// thread: one write at a time, and a newer draft replaces one still waiting,
// so a long painting session never queues stale work. remove() drops the
// waiting draft, lets an active write finish and deletes the owner's files.
class ModelSessionDraftWriter final : public QObject
{
  public:
	explicit ModelSessionDraftWriter(QString directory, QObject *parent = nullptr);
	~ModelSessionDraftWriter() override;
	void write(ModelSessionDraft draft);
	bool remove(const QString &ownerId, QString *error = nullptr, const ModelSessionDraftKind *kind = nullptr);
	[[nodiscard]] bool busy() const;
	// The outcome of each write: the sidecar path, or an error.
	std::function<void(const QString &path, const QString &error)> finished;

  private:
	struct Result
	{
		QString path, error;
	};
	QString m_directory;
	std::optional<ModelSessionDraft> m_pending;
	QThread *m_thread = nullptr;
	std::shared_ptr<Result> m_result;
	void startNext();
	void waitForActive();
};

} // namespace vibestudio
