#pragma once

#include "core/model_assembly_recovery.h"
#include <optional>

class QWidget;

namespace slopstudio
{
struct RecoveredAssemblyDraft
{
	ModelAssemblyDocument document;
	ModelAssemblyRecoverySnapshot snapshot;
	ModelAssemblyRecoveryRecord record;
};
std::optional<RecoveredAssemblyDraft> chooseModelAssemblyRecovery(QWidget *parent, const QString &directory);
} // namespace slopstudio
