#pragma once

#include "core/model_recovery.h"

#include <optional>

class QWidget;

namespace slopstudio
{
struct RecoveredModelDraft
{
	ModelDocument document;
	ModelRecoveryRecord record;
};

// Header scan, payload verification, and draft preparation run off the UI thread.
// Cancellation leaves both the current editor document and recovery file intact.
std::optional<RecoveredModelDraft> chooseModelRecovery(QWidget *parent, const QString &directory);
} // namespace slopstudio
