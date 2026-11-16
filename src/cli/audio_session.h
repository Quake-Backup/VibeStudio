#pragma once
#include <QJsonObject>
#include <QStringList>

namespace slopstudio::cli
{
struct AudioSessionCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
AudioSessionCliResult runAudioSession(const QStringList &arguments);
} // namespace slopstudio::cli
