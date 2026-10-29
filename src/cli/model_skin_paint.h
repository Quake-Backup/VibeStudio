#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli
{
struct ModelSkinPaintCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
// model paint-skin: replays recorded 3D paint strokes (core/model_skin_paint.h)
// onto a surface's skin and writes the result.
ModelSkinPaintCliResult runModelSkinPaint(const QStringList &arguments);
} // namespace vibestudio::cli
