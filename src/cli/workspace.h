#pragma once
#include <QJsonObject>
#include <QStringList>

namespace slopstudio::cli {
struct WorkspaceCliResult { int exitCode = 0; QString error; QJsonObject payload; QStringList lines; };
WorkspaceCliResult runWorkspaceCommand(const QStringList& arguments);
} // namespace slopstudio::cli
