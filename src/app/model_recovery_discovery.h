#pragma once

// Startup discovery of model recovery copies: a worker applies the mesh
// retention policy (core/model_recovery_policy.h), then counts the mesh and
// assembly copies and the mesh copies whose source changed since. The studio
// shows a notice with Review Models; nothing is opened or restored by itself.

#include <QObject>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>

class QThread;

namespace vibestudio
{

struct ModelRecoveryInventory
{
	int meshCopies = 0;
	int assemblyCopies = 0;
	// Mesh copies whose source was saved, replaced or removed since.
	int changedSources = 0;
	// A scan reached its limit; there may be more copies.
	bool limited = false;
	// Old copies the retention policy removed.
	QStringList pruned;
	QString error;
	bool cancelled = false;
};

// Runs the discovery once on a worker; `finished` runs on the owner's thread.
ModelRecoveryInventory discoverModelRecoveries(const QString &meshDirectory, const QString &assemblyDirectory,
											   const std::function<bool()> &cancelled = {});

class ModelRecoveryDiscovery final : public QObject
{
  public:
	explicit ModelRecoveryDiscovery(QObject *parent = nullptr);
	~ModelRecoveryDiscovery() override;
	void start(const QString &meshDirectory, const QString &assemblyDirectory);
	void cancel();
	[[nodiscard]] bool busy() const { return m_thread != nullptr; }
	std::function<void(const ModelRecoveryInventory &)> finished;

  private:
	QThread *m_thread = nullptr;
	std::shared_ptr<std::atomic_bool> m_cancel;
};

} // namespace vibestudio
