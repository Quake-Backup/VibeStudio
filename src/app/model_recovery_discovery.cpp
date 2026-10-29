#include "app/model_recovery_discovery.h"

#include "app/studio_runtime.h"
#include "core/model_assembly_recovery.h"
#include "core/model_recovery.h"
#include "core/model_recovery_policy.h"

#include <QThread>

namespace vibestudio
{

ModelRecoveryInventory discoverModelRecoveries(const QString &meshDirectory, const QString &assemblyDirectory,
											   const std::function<bool()> &cancelled)
{
	ModelRecoveryInventory inventory;
	const auto prune = pruneModelRecoveries(meshDirectory, {}, [](qint64 owner) { return studioProcessIsRunning(owner); }, cancelled);
	inventory.pruned = prune.removed;
	const auto mesh = listModelRecoveries(meshDirectory, cancelled);
	inventory.error = !mesh.error.isEmpty() ? mesh.error : prune.error;
	inventory.limited = mesh.limited;
	for (const auto &record : mesh.records)
	{
		if (cancelled && cancelled())
		{
			inventory.cancelled = true;
			return inventory;
		}
		if (!record.isValid())
			continue;
		++inventory.meshCopies;
		// A copy this or another running studio is writing is not a crash leftover.
		if (record.ownerProcessId > 0 && studioProcessIsRunning(record.ownerProcessId))
		{
			--inventory.meshCopies;
			continue;
		}
		const auto state = modelRecoverySourceState(record, cancelled);
		if (state == ModelRecoverySourceState::Changed || state == ModelRecoverySourceState::Missing)
			++inventory.changedSources;
	}
	ModelWorkControl control;
	control.cancelled = cancelled;
	const auto assemblies = listModelAssemblyRecoveries(assemblyDirectory, control);
	for (const auto &record : assemblies.records)
	{
		if (record.isValid())
			++inventory.assemblyCopies;
	}
	inventory.limited = inventory.limited || assemblies.limited;
	if (inventory.error.isEmpty())
		inventory.error = assemblies.error;
	inventory.cancelled = cancelled && cancelled();
	return inventory;
}

ModelRecoveryDiscovery::ModelRecoveryDiscovery(QObject *parent) : QObject(parent), m_cancel(std::make_shared<std::atomic_bool>(false)) {}

ModelRecoveryDiscovery::~ModelRecoveryDiscovery()
{
	finished = {};
	cancel();
	if (m_thread)
	{
		m_thread->wait();
		delete m_thread;
	}
}

void ModelRecoveryDiscovery::cancel()
{
	m_cancel->store(true);
}

void ModelRecoveryDiscovery::start(const QString &meshDirectory, const QString &assemblyDirectory)
{
	if (busy())
		return;
	m_cancel = std::make_shared<std::atomic_bool>(false);
	auto result = std::make_shared<ModelRecoveryInventory>();
	m_thread = QThread::create([meshDirectory, assemblyDirectory, result, cancel = m_cancel]()
							   { *result = discoverModelRecoveries(meshDirectory, assemblyDirectory, [cancel] { return cancel->load(); }); });
	connect(m_thread, &QThread::finished, this,
			[this, result]()
			{
				m_thread->deleteLater();
				m_thread = nullptr;
				if (!m_cancel->load() && finished)
					finished(*result);
			});
	m_thread->start();
}

} // namespace vibestudio
