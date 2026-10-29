#include "core/model_work.h"
#include "core/model_work_defaults.h"

#include <QCoreApplication>

namespace vibestudio
{
namespace
{
ModelWorkControl &defaults()
{
	static ModelWorkControl value;
	return value;
}
} // namespace

void setModelWorkDefaults(ModelWorkControl control) { defaults() = std::move(control); }

const ModelWorkControl &modelWorkDefaults() { return defaults(); }

bool modelWorkCheckpoint(const ModelWorkControl &control, ModelWorkPhase phase, qint64 completed, qint64 total, QString *error)
{
	// Fields the caller left empty fall back to the process defaults
	// (core/model_work_defaults.h): the command line's cancellation token.
	const auto &fallback = defaults();
	const auto &stop = control.cancelled ? control.cancelled : fallback.cancelled;
	const auto &report = control.progress ? control.progress : fallback.progress;
	const auto cancelled = [&]
	{
		if (!stop || !stop())
		{
			return false;
		}
		if (error)
		{
			*error = QCoreApplication::translate("VibeStudioModelDocument", "Model operation cancelled.");
		}
		return true;
	};
	if (cancelled())
	{
		return false;
	}
	if (report)
	{
		report(phase, completed, total);
	}
	return !cancelled();
}
} // namespace vibestudio
