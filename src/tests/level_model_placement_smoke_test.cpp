// Level model placement (core/level_model_placement.h), the one service the
// Levels Models tab, the modeller's Stage and Place and `map place-model`
// share: class choice with and without definitions, the formats each map
// family loads, refusals, angles and modelscale keys, and the reverse step from
// an entity to its model. With the studio as argv[1], `map place-model` places
// an MDL in a Quake map and an ASE in a Quake III map.
#include "core/entity_definitions.h"
#include "core/level_map.h"
#include "core/level_model_placement.h"
#include "core/model_design.h"
#include "core/model_document.h"
#include "tests/model_mdl_test_helpers.h"
#include "tests/package_subset_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
const char kMap[] = "{\n\"classname\" \"worldspawn\"\n}\n";
QString key(const LevelMapEntity &entity, const QString &name)
{
	for (const auto &property : entity.properties)
	{
		if (property.key == name)
			return property.value;
	}
	return {};
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	bool ok = true;
	LevelMapDocument quake, quake3, doom;
	ok &= expect(loadLevelMapBytes({QStringLiteral("q1.map"), {}, QStringLiteral("idTech2")}, kMap, &quake, &error) &&
					 quake.format == LevelMapFormat::QuakeMap,
				 "load a Quake map");
	ok &= expect(loadLevelMapBytes({QStringLiteral("q3.map"), {}, QStringLiteral("idTech3")}, kMap, &quake3, &error) &&
					 quake3.format == LevelMapFormat::Quake3Map,
				 "load a Quake III map");
	doom.format = LevelMapFormat::DoomWad;

	// Class choice.
	const auto definitions = parseEntityDefinitions(QStringLiteral("game.def"),
													"/*QUAKED misc_gamemodel (1 0 0) (-8 -8 -8) (8 8 8)\nA model.\n*/\n",
													EntityDefinitionFormat::Unknown);
	ok &= expect(chooseLevelModelEntityClass(nullptr) == QStringLiteral("misc_model") &&
					 chooseLevelModelEntityClass(&definitions) == QStringLiteral("misc_gamemodel"),
				 "the class comes from the definitions, else misc_model");

	// Quake maps: MDL and MD2 without warnings; other formats warn.
	LevelModelPlacementResult result;
	LevelModelPlacementRequest request;
	request.virtualPath = QStringLiteral("progs/crate.mdl");
	request.origin = {16, 32, 48, true};
	request.angles = {0, 90, 0, true};
	request.definitions = &definitions;
	ok &= expect(placeLevelModelEntity(&quake, request, &result, &error) && result.entityId >= 0 &&
					 result.entityClass == QStringLiteral("misc_gamemodel") && result.warnings.isEmpty(),
				 "a Quake map places an MDL as the defined class");
	auto placed = std::find_if(quake.entities.cbegin(), quake.entities.cend(), [&](const LevelMapEntity &e) { return e.id == result.entityId; });
	ok &= expect(placed != quake.entities.cend() && key(*placed, QStringLiteral("model")) == QStringLiteral("progs/crate.mdl") &&
					 key(*placed, QStringLiteral("angle")) == QStringLiteral("90") && key(*placed, QStringLiteral("modelscale")).isEmpty() &&
					 levelEntityModelPath(*placed) == QStringLiteral("progs/crate.mdl"),
				 "a yaw alone writes angle, a unit scale writes nothing, and the entity names its model");
	request.virtualPath = QStringLiteral("models/crate.md2");
	request.angles = {10, 20, 30, true};
	request.modelscale = 2.5;
	request.definitions = nullptr;
	ok &= expect(placeLevelModelEntity(&quake, request, &result, &error) && result.warnings.isEmpty(), "a Quake map places an MD2");
	placed = std::find_if(quake.entities.cbegin(), quake.entities.cend(), [&](const LevelMapEntity &e) { return e.id == result.entityId; });
	ok &= expect(placed != quake.entities.cend() && key(*placed, QStringLiteral("angles")) == QStringLiteral("10 20 30") &&
					 key(*placed, QStringLiteral("modelscale")) == QStringLiteral("2.5"),
				 "pitch and roll write angles; a scale writes modelscale");
	request.virtualPath = QStringLiteral("models/crate.md3");
	request.definitions = &definitions;
	request.entityClass = QStringLiteral("func_prop");
	ok &= expect(placeLevelModelEntity(&quake, request, &result, &error) && result.warnings.size() == 2,
				 "a source-port format and an undeclared class place with warnings");

	// Quake III maps: MD3 and ASE bake; MDL is refused.
	request = {};
	request.virtualPath = QStringLiteral("models/props/crate.md3");
	request.origin = {0, 0, 0, true};
	ok &= expect(placeLevelModelEntity(&quake3, request, &result, &error) && result.entityClass == QStringLiteral("misc_model"),
				 "a Quake III map places an MD3 as misc_model");
	request.virtualPath = QStringLiteral("models/props/crate.ase");
	ok &= expect(placeLevelModelEntity(&quake3, request, &result, &error), "a Quake III map places an ASE for q3map2");
	const int before = int(quake3.entities.size());
	request.virtualPath = QStringLiteral("progs/crate.mdl");
	ok &= expect(!placeLevelModelEntity(&quake3, request, &result, &error) && !error.isEmpty() && quake3.entities.size() == before,
				 "a Quake III map refuses an MDL without changing the map");

	// Refusals.
	request.virtualPath = QStringLiteral("models/crate.md3");
	ok &= expect(!placeLevelModelEntity(&doom, request, &result, &error), "Doom maps have no model entities");
	request.virtualPath = QStringLiteral("../escape.md3");
	ok &= expect(!placeLevelModelEntity(&quake3, request, &result, &error), "an unsafe path is refused");
	request.virtualPath = QStringLiteral("models/") + QString(60, QLatin1Char('a')) + QStringLiteral(".md3");
	ok &= expect(!placeLevelModelEntity(&quake3, request, &result, &error), "a path of 64 bytes or more is refused");
	request.virtualPath = QStringLiteral("models/crate.md3");
	request.origin.x = std::numeric_limits<double>::quiet_NaN();
	ok &= expect(!placeLevelModelEntity(&quake3, request, &result, &error), "a non-finite origin is refused");
	request.origin.x = 0;
	request.modelscale = 0;
	ok &= expect(!placeLevelModelEntity(&quake3, request, &result, &error), "a zero scale is refused");
	ok &= expect(quake3.entities.size() == before, "refusals leave the map unchanged");

	// `map place-model` shares the service: an MDL in a Quake map, an ASE in a Quake III map.
	if (argc > 1)
	{
		const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
		QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("level-model-placement-XXXXXX")));
		const auto path = [&](const QString &name) { return QDir(temporary.path()).filePath(name); };
		QFile map(path(QStringLiteral("start.map")));
		ok &= expect(temporary.isValid() && map.open(QIODevice::WriteOnly) && map.write(kMap) > 0, "write the map fixture");
		map.close();
		ModelDesign design;
		design.parts << ModelDesignPart{};
		const auto ase = exportEditableModel(buildModelDesignMesh(design), QStringLiteral("ase"), 0, &error);
		ok &= expect(!ase.isEmpty() && subset_test::pak(path(QStringLiteral("pak0.pak")),
														{{"progs/crate.mdl", tests::groupedMdlFixture().bytes}, {"models/crate.ase", ase}}),
					 "write the package fixture");
		QJsonObject output;
		const auto run = [&](const QStringList &args, int code)
		{
			QProcess process;
			process.start(QString::fromLocal8Bit(argv[1]), QStringList{QStringLiteral("--cli")} + args + QStringList{QStringLiteral("--json")});
			if (!process.waitForFinished(60000))
				return false;
			output = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
			if (process.exitCode() != code)
				std::cerr << qPrintable(args.join(' ')) << " exit=" << process.exitCode() << ' ' << process.readAllStandardError().constData() << '\n';
			return process.exitCode() == code;
		};
		ok &= expect(run({QStringLiteral("map"), QStringLiteral("place-model"), path(QStringLiteral("start.map")), QStringLiteral("--package"),
						  path(QStringLiteral("pak0.pak")), QStringLiteral("--entry"), QStringLiteral("progs/crate.mdl"), QStringLiteral("--origin"),
						  QStringLiteral("0,0,24"), QStringLiteral("--angles"), QStringLiteral("0,45,0"), QStringLiteral("--modelscale"),
						  QStringLiteral("2"), QStringLiteral("--engine"), QStringLiteral("idTech2"), QStringLiteral("--output"),
						  path(QStringLiteral("dry.map")), QStringLiteral("--dry-run")},
						 0) &&
						 !QFileInfo::exists(path(QStringLiteral("dry.map"))) && output.value(QStringLiteral("entityId")).toInt(-1) >= 0,
					 "map place-model dry-runs an MDL placement in a Quake map");
		ok &= expect(run({QStringLiteral("map"), QStringLiteral("place-model"), path(QStringLiteral("start.map")), QStringLiteral("--package"),
						  path(QStringLiteral("pak0.pak")), QStringLiteral("--entry"), QStringLiteral("progs/crate.mdl"), QStringLiteral("--origin"),
						  QStringLiteral("0,0,24"), QStringLiteral("--class"), QStringLiteral("misc_gamemodel"), QStringLiteral("--engine"),
						  QStringLiteral("idTech2"), QStringLiteral("--output"), path(QStringLiteral("placed.map"))},
						 0),
					 "map place-model writes an MDL placement with a chosen class");
		QFile placedMap(path(QStringLiteral("placed.map")));
		ok &= expect(placedMap.open(QIODevice::ReadOnly) && placedMap.readAll().contains("\"classname\" \"misc_gamemodel\""),
					 "the placed map names the chosen class");
		ok &= expect(run({QStringLiteral("map"), QStringLiteral("place-model"), path(QStringLiteral("start.map")), QStringLiteral("--package"),
						  path(QStringLiteral("pak0.pak")), QStringLiteral("--entry"), QStringLiteral("models/crate.ase"), QStringLiteral("--origin"),
						  QStringLiteral("0,0,0"), QStringLiteral("--engine"), QStringLiteral("idTech3"), QStringLiteral("--output"),
						  path(QStringLiteral("ase.map")), QStringLiteral("--dry-run")},
						 0),
					 "map place-model places an ASE in a Quake III map");
		ok &= expect(run({QStringLiteral("map"), QStringLiteral("place-model"), path(QStringLiteral("start.map")), QStringLiteral("--package"),
						  path(QStringLiteral("pak0.pak")), QStringLiteral("--entry"), QStringLiteral("progs/crate.mdl"), QStringLiteral("--origin"),
						  QStringLiteral("0,0,0"), QStringLiteral("--engine"), QStringLiteral("idTech3"), QStringLiteral("--output"),
						  path(QStringLiteral("bad.map"))},
						 4) &&
						 !QFileInfo::exists(path(QStringLiteral("bad.map"))),
					 "map place-model refuses an MDL in a Quake III map");
	}
	if (!ok)
	{
		std::cerr << qPrintable(error) << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
