#include "app/model_session_draft_writer.h"

#include <QThread>

namespace vibestudio
{

ModelSessionDraftWriter::ModelSessionDraftWriter(QString directory, QObject *parent) : QObject(parent), m_directory(std::move(directory)) {}

ModelSessionDraftWriter::~ModelSessionDraftWriter()
{
	m_pending.reset();
	waitForActive();
}

void ModelSessionDraftWriter::write(ModelSessionDraft draft)
{
	m_pending = std::move(draft);
	if (!m_thread)
		startNext();
}

bool ModelSessionDraftWriter::remove(const QString &ownerId, QString *error, const ModelSessionDraftKind *kind)
{
	if (m_pending && m_pending->ownerId == ownerId && (!kind || m_pending->kind == *kind))
		m_pending.reset();
	// An active write of this owner would recreate the file after removal.
	waitForActive();
	const bool removed = removeModelSessionDrafts(m_directory, ownerId, error, kind);
	if (m_pending && !m_thread)
		startNext();
	return removed;
}

bool ModelSessionDraftWriter::busy() const { return m_thread || m_pending; }

void ModelSessionDraftWriter::waitForActive()
{
	if (!m_thread)
		return;
	disconnect(m_thread, nullptr, this, nullptr);
	m_thread->wait();
	delete m_thread;
	m_thread = nullptr;
	if (finished && m_result)
		finished(m_result->path, m_result->error);
	m_result.reset();
}

void ModelSessionDraftWriter::startNext()
{
	if (!m_pending)
		return;
	auto draft = std::move(*m_pending);
	m_pending.reset();
	auto result = std::make_shared<Result>();
	m_result = result;
	m_thread = QThread::create(
		[directory = m_directory, draft = std::move(draft), result]
		{
			// A reader holding the sidecar open (the recovery chooser, another
			// studio's discovery) makes replacing it fail on Windows for a moment.
			QString error;
			for (int attempt = 0; attempt < 5; ++attempt)
			{
				if (writeModelSessionDraft(directory, draft, &error))
				{
					result->path = modelSessionDraftPath(directory, draft.ownerId, draft.kind);
					error.clear();
					break;
				}
				QThread::msleep(50);
			}
			result->error = error;
		});
	connect(m_thread, &QThread::finished, this,
			[this, result]
			{
				if (!m_thread || m_result != result)
					return;
				m_thread->deleteLater();
				m_thread = nullptr;
				m_result.reset();
				if (finished)
					finished(result->path, result->error);
				startNext();
			});
	m_thread->start();
}

} // namespace vibestudio
