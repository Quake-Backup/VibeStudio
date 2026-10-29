#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli
{
struct ModelBakeCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
// model bake: ambient occlusion, normal (object or Doom 3 tangent space,
// optionally from a high-poly model) and lighting maps baked into a model's
// UV layout (core/model_bake.h), written as PNG or TGA.
ModelBakeCliResult runModelBake(const QStringList &arguments);
} // namespace vibestudio::cli
