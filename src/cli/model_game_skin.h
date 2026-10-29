#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli
{
struct ModelGameSkinCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
// model game-skin: writes the Game preview of one surface at a time
// (core/model_game_preview.h) as PNG images, the same composition the Mesh
// Editor's Shading > Game draws.
ModelGameSkinCliResult runModelGameSkin(const QStringList &arguments);
} // namespace vibestudio::cli
