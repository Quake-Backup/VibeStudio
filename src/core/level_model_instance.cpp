#include "core/level_model_instance.h"

#include <QRegularExpression>

#include <cmath>

namespace vibestudio
{
namespace
{
QString property(const LevelMapEntity &entity, const QString &key)
{
	for (const auto &item : entity.properties)
	{
		if (item.key.compare(key, Qt::CaseInsensitive) == 0)
			return item.value;
	}
	return {};
}
QStringList words(const QString &value)
{
	return value.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}
// 3x3 row-major products for the double-precision affine.
std::array<double, 9> multiply(const std::array<double, 9> &a, const std::array<double, 9> &b)
{
	std::array<double, 9> r{};
	for (int row = 0; row < 3; ++row)
		for (int column = 0; column < 3; ++column)
			r[row * 3 + column] = a[row * 3] * b[column] + a[row * 3 + 1] * b[3 + column] + a[row * 3 + 2] * b[6 + column];
	return r;
}
std::array<double, 9> rotation(double degrees, int axis)
{
	// Right-handed turn about the axis, as QMatrix4x4::rotate.
	const double radians = degrees * 3.14159265358979323846 / 180.0;
	const double c = std::cos(radians), s = std::sin(radians);
	if (axis == 2)
		return {c, -s, 0, s, c, 0, 0, 0, 1};
	if (axis == 1)
		return {c, 0, s, 0, 1, 0, -s, 0, c};
	return {1, 0, 0, 0, c, -s, 0, s, c};
}
} // namespace

LevelModelInstanceTransform levelModelInstanceTransform(const LevelMapEntity &entity)
{
	// The parsing below is the level preview's, moved here unchanged so the
	// preview and collision placement read an instance the same way.
	LevelModelInstanceTransform result;
	result.origin = {entity.origin.x, entity.origin.y, entity.origin.z, true};
	const auto angles = words(property(entity, QStringLiteral("angles")));
	if (angles.size() == 3)
	{
		result.hasAngles = true;
		result.pitch = angles[0].toFloat();
		result.yaw = angles[1].toFloat();
		result.roll = angles[2].toFloat();
	}
	else
	{
		result.yaw = property(entity, QStringLiteral("angle")).toFloat();
	}
	const auto scales = words(property(entity, QStringLiteral("modelscale_vec")));
	if (scales.size() == 3)
	{
		result.scale = {scales[0].toFloat(), scales[1].toFloat(), scales[2].toFloat()};
		result.scaleKeys = 3;
	}
	else
	{
		bool ok = false;
		const float scale = property(entity, QStringLiteral("modelscale")).toFloat(&ok);
		if (ok)
		{
			result.scale = {scale, scale, scale};
			result.scaleKeys = 1;
		}
	}
	return result;
}

QMatrix4x4 levelModelInstanceMatrix(const LevelModelInstanceTransform &transform)
{
	QMatrix4x4 matrix;
	matrix.translate(float(transform.origin.x), float(transform.origin.y), float(transform.origin.z));
	if (transform.hasAngles)
	{
		matrix.rotate(float(transform.yaw), 0, 0, 1);
		matrix.rotate(float(transform.pitch), 0, 1, 0);
		matrix.rotate(float(transform.roll), 1, 0, 0);
	}
	else
	{
		matrix.rotate(float(transform.yaw), 0, 0, 1);
	}
	if (transform.scaleKeys == 3)
		matrix.scale(float(transform.scale[0]), float(transform.scale[1]), float(transform.scale[2]));
	else if (transform.scaleKeys == 1)
		matrix.scale(float(transform.scale[0]));
	return matrix;
}

std::array<double, 12> levelModelInstanceAffine(const LevelModelInstanceTransform &transform)
{
	auto linear = rotation(transform.yaw, 2);
	if (transform.hasAngles)
		linear = multiply(multiply(linear, rotation(transform.pitch, 1)), rotation(transform.roll, 0));
	const std::array<double, 9> scale{transform.scale[0], 0, 0, 0, transform.scale[1], 0, 0, 0, transform.scale[2]};
	linear = multiply(linear, scale);
	return {linear[0], linear[1], linear[2], transform.origin.x, linear[3], linear[4], linear[5], transform.origin.y,
			linear[6], linear[7], linear[8], transform.origin.z};
}

LevelMapVec3 applyLevelModelInstanceAffine(const std::array<double, 12> &m, double x, double y, double z)
{
	return {m[0] * x + m[1] * y + m[2] * z + m[3], m[4] * x + m[5] * y + m[6] * z + m[7], m[8] * x + m[9] * y + m[10] * z + m[11], true};
}

bool levelEntityPlacesModel(const LevelMapEntity &entity)
{
	const auto model = property(entity, QStringLiteral("model")).trimmed();
	return !model.isEmpty() && !model.startsWith(QLatin1Char('*'));
}
} // namespace vibestudio
