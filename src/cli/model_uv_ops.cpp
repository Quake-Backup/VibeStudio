#include "cli/model_tools.h"
#include "core/model_document.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <cmath>

namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelUvOpsCli)
};
// The value of an option given once as "--name value" or "--name=value";
// `count` receives how often it appears.
QString value(const QStringList &args, const QString &name, int *count)
{
	QString found;
	*count = 0;
	for (int i = 0; i < args.size(); ++i)
	{
		if (args[i] == name)
		{
			++*count;
			found = args.value(i + 1);
		}
		else if (args[i].startsWith(name + QLatin1Char('=')))
		{
			++*count;
			found = args[i].mid(name.size() + 1);
		}
	}
	return found;
}
bool indices(const QString &text, QSet<int> *result)
{
	for (const auto &part : text.split(QLatin1Char(',')))
	{
		bool ok = false;
		const int index = part.toInt(&ok);
		if (!ok || index < 0 || part != QString::number(index) || result->contains(index))
			return false;
		result->insert(index);
	}
	return !result->isEmpty();
}
bool fail(QString *error, const QString &message)
{
	*error = message;
	return false;
}
} // namespace

bool readModelUvToolOptions(const QStringList &args, ModelEdit *edit, QString *error)
{
	auto &tools = edit->uvTools;
	const auto kind = edit->kind;
	const bool conformalAllowed = kind == ModelEditKind::UnwrapUv;
	struct Option
	{
		QString name;
		bool flag;
		bool allowed;
	};
	const QVector<Option> options{
		{QStringLiteral("--uv-weld-tolerance"), false, kind == ModelEditKind::WeldUv},
		{QStringLiteral("--uv-align"), false, kind == ModelEditKind::AlignUv},
		{QStringLiteral("--uv-snap"), false, kind == ModelEditKind::SnapUvTexels},
		{QStringLiteral("--uv-snap-size"), false, kind == ModelEditKind::SnapUvTexels},
		{QStringLiteral("--uv-method"), false, conformalAllowed},
		{QStringLiteral("--uv-pins"), false, conformalAllowed},
		{QStringLiteral("--no-pack"), true, conformalAllowed},
		{QStringLiteral("--uv-allow-rotation"), true, kind == ModelEditKind::PackUv || conformalAllowed},
		{QStringLiteral("--uv-pack-surfaces"), false, kind == ModelEditKind::PackUv},
	};
	QHash<QString, QString> values;
	for (const auto &option : options)
	{
		int count = 0;
		const auto text = value(args, option.name, &count);
		if (!count)
			continue;
		if (!option.allowed || count > 1 || (!option.flag && (text.isEmpty() || text.startsWith(QStringLiteral("--")))))
			return fail(error, Text::tr("%1 is given more than once, has no value, or does not apply to this operation. Weld takes "
										"--uv-weld-tolerance; align --uv-align; snap-texels --uv-snap and --uv-snap-size; unwrap "
										"--uv-method, --uv-pins, --no-pack and --uv-allow-rotation; pack --uv-allow-rotation and "
										"--uv-pack-surfaces.")
								   .arg(option.name));
		values.insert(option.name, text);
	}
	if (values.contains(QStringLiteral("--uv-weld-tolerance")))
	{
		bool ok = false;
		tools.weldTolerance = values.value(QStringLiteral("--uv-weld-tolerance")).toDouble(&ok);
		if (!ok || !std::isfinite(tools.weldTolerance) || tools.weldTolerance < 0 || tools.weldTolerance > 1)
			return fail(error, Text::tr("--uv-weld-tolerance takes a UV distance from 0 to 1."));
	}
	if (values.contains(QStringLiteral("--uv-align")))
	{
		const auto mode = values.value(QStringLiteral("--uv-align"));
		const QStringList modes{QStringLiteral("u-min"), QStringLiteral("u-centre"), QStringLiteral("u-max"),
								QStringLiteral("v-min"), QStringLiteral("v-centre"), QStringLiteral("v-max"), QStringLiteral("straighten")};
		const int index = int(modes.indexOf(mode));
		if (index < 0)
			return fail(error, Text::tr("--uv-align takes u-min, u-centre, u-max, v-min, v-centre, v-max or straighten."));
		tools.alignAxis = index >= 3 && index < 6 ? 1 : 0;
		tools.alignMode = index == 6 ? ModelUvAlignMode::Straighten
						  : index % 3 == 0 ? ModelUvAlignMode::Minimum
						  : index % 3 == 1 ? ModelUvAlignMode::Centre
										   : ModelUvAlignMode::Maximum;
	}
	if (values.contains(QStringLiteral("--uv-snap")))
	{
		const auto mode = values.value(QStringLiteral("--uv-snap"));
		if (mode != QStringLiteral("centres") && mode != QStringLiteral("corners"))
			return fail(error, Text::tr("--uv-snap takes centres or corners."));
		tools.snapMode = mode == QStringLiteral("corners") ? ModelUvSnapMode::Corners : ModelUvSnapMode::Centres;
	}
	if (values.contains(QStringLiteral("--uv-snap-size")))
	{
		const auto match =
			QRegularExpression(QStringLiteral("\\A([0-9]+)(?:x([0-9]+))?\\z")).match(values.value(QStringLiteral("--uv-snap-size")));
		const int width = match.captured(1).toInt(), height = match.captured(2).isEmpty() ? width : match.captured(2).toInt();
		if (!match.hasMatch() || width < 1 || height < 1 || width > 65536 || height > 65536)
			return fail(error, Text::tr("--uv-snap-size takes N or WIDTHxHEIGHT texels from 1 to 65,536."));
		tools.snapSize = {width, height};
	}
	if (values.contains(QStringLiteral("--uv-method")))
	{
		const auto method = values.value(QStringLiteral("--uv-method"));
		if (method != QStringLiteral("automatic") && method != QStringLiteral("conformal"))
			return fail(error, Text::tr("--uv-method takes automatic or conformal."));
		tools.unwrapMethod = method == QStringLiteral("conformal") ? ModelUvUnwrapMethod::Conformal : ModelUvUnwrapMethod::Automatic;
	}
	const bool conformal = tools.unwrapMethod == ModelUvUnwrapMethod::Conformal;
	if ((values.contains(QStringLiteral("--uv-pins")) || values.contains(QStringLiteral("--no-pack"))) && !conformal)
		return fail(error, Text::tr("--uv-pins and --no-pack need --uv-method conformal."));
	if (kind == ModelEditKind::UnwrapUv && values.contains(QStringLiteral("--uv-allow-rotation")) && (!conformal || values.contains(QStringLiteral("--no-pack"))))
		return fail(error, Text::tr("For uv-unwrap, --uv-allow-rotation needs --uv-method conformal with packing."));
	if (values.contains(QStringLiteral("--uv-pins")))
	{
		QSet<int> pins;
		if (!indices(values.value(QStringLiteral("--uv-pins")), &pins))
			return fail(error, Text::tr("--uv-pins takes unique vertex indices separated by commas."));
		edit->selection.vertices += pins;
	}
	tools.unwrapPack = !values.contains(QStringLiteral("--no-pack"));
	tools.packRotate = values.contains(QStringLiteral("--uv-allow-rotation"));
	if (values.contains(QStringLiteral("--uv-pack-surfaces")))
	{
		QSet<int> surfaces;
		if (!indices(values.value(QStringLiteral("--uv-pack-surfaces")), &surfaces))
			return fail(error, Text::tr("--uv-pack-surfaces takes unique surface indices separated by commas."));
		tools.packSurfaces = surfaces;
	}
	return true;
}
} // namespace vibestudio::cli
