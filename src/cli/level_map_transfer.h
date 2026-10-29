#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli {
struct LevelMapTransferCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
// map import <map> --from <other.map|other.bsp> --output <path> and
// map export <map> --output <path> (core/level_map_transfer.h).
LevelMapTransferCliResult runLevelMapTransfer(const QStringList& arguments);
// map export-obj <map> --output <file.obj> (core/level_map_obj_export.h).
LevelMapTransferCliResult runLevelMapObjExport(const QStringList& arguments);
} // namespace vibestudio::cli
