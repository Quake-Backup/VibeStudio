#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli {
struct LevelFaceAttributesCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
// map face-attributes: reads or sets the texture offset, scale and rotation
// of brush faces, and moves, turns or stretches a texture about a pivot,
// through the Levels Face section's service (core/level_face_attributes.h).
LevelFaceAttributesCliResult runLevelFaceAttributes(const QStringList& arguments);
} // namespace vibestudio::cli
