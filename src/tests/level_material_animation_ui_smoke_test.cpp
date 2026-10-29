// Animated materials on the Levels camera: a face whose Quake III shader
// scrolls gets new frames while the animator runs, a still face keeps its
// image, turning it off puts the still image back, and reduced motion stops
// the frames. Then the real shell: View > Camera Display > Animate Materials
// in Camera plays a map's animated shader on the Levels camera, and the
// Materials page's choice of Quake III game reaches the Levels page. Every
// file is generated here; no game data.

#include "app/application_shell.h"
#include "app/level_material_animator.h"
#include "app/material_workbench.h"
#include "app/model_viewport.h"
#include "core/game_installation.h"
#include "core/model_mesh.h"
#include "core/package_archive.h"
#include "core/package_staging.h"
#include "tests/level_material_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAction>
#include <QApplication>
#include <QListWidget>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>
#include <QtEndian>

#include <functional>
#include <iostream>

using namespace vibestudio;

namespace {

int failures = 0;

bool expect(bool condition, const std::string& message, const QString& detail = QString())
{
	if (!condition) {
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty()) {
			std::cerr << "\n  " << detail.toUtf8().constData();
		}
		std::cerr << '\n';
		++failures;
	}
	return condition;
}

bool waitFor(const std::function<bool()>& condition, int timeoutMs = 60000)
{
	QElapsedTimer timer;
	timer.start();
	while (!condition()) {
		if (timer.elapsed() > timeoutMs) {
			return false;
		}
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QThread::msleep(5);
	}
	return true;
}

void writeFile(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(bytes);
	}
}

// A 16 x 16 TGA: two colours in vertical stripes, so scrolling shows.
QByteArray stripes(QRgb a, QRgb b)
{
	QByteArray bytes(18, '\0');
	bytes[2] = 2;
	qToLittleEndian<quint16>(16, bytes.data() + 12);
	qToLittleEndian<quint16>(16, bytes.data() + 14);
	bytes[16] = 32;
	bytes[17] = 0x28;
	for (int y = 0; y < 16; ++y) {
		for (int x = 0; x < 16; ++x) {
			const QRgb pixel = x < 8 ? a : b;
			bytes.append(static_cast<char>(qBlue(pixel)));
			bytes.append(static_cast<char>(qGreen(pixel)));
			bytes.append(static_cast<char>(qRed(pixel)));
			bytes.append('\xFF');
		}
	}
	return bytes;
}

QImage solid(QRgb color)
{
	QImage image(8, 8, QImage::Format_ARGB32_Premultiplied);
	image.fill(color);
	return image;
}

} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	const QString root = temp.path();
	StudioSettings::setOverrideFilePath(QDir(root).filePath(QStringLiteral("settings.ini")));
	writeFile(root + QStringLiteral("/scripts/test.shader"),
		"textures/test/scroll\n{\n\t{\n\t\tmap textures/test/scroll.tga\n\t\ttcMod scroll 1 0\n\t}\n}\n"
		"textures/test/still\n{\n\t{\n\t\tmap textures/test/still.tga\n\t}\n}\n");
	writeFile(root + QStringLiteral("/textures/test/scroll.tga"), stripes(qRgb(250, 40, 40), qRgb(40, 40, 250)));
	writeFile(root + QStringLiteral("/textures/test/still.tga"), stripes(qRgb(40, 250, 40), qRgb(40, 250, 40)));
	auto archive = std::make_shared<PackageArchive>();
	QString error;
	if (!expect(archive->load(root, &error), "the fixture folder opens", error)) {
		return 1;
	}

	// Two faces, named as a Quake III map names shaders.
	const QByteArray obj = "v 0 0 0\nv 64 0 0\nv 0 64 0\nv 64 64 0\nvt 0 0\nvt 1 0\nvt 0 1\nvt 1 1\n"
						   "g scroll\nf 1/1 2/2 3/3\ng still\nf 2/2 4/4 3/3\n";
	ModelMesh mesh = decodeModelMesh(QStringLiteral("models/faces.obj"), obj);
	if (!expect(mesh.surfaces.size() == 2, "the fixture mesh has two surfaces", QString::number(mesh.surfaces.size()))) {
		return 1;
	}
	mesh.surfaces[0].name = QStringLiteral("test/scroll");
	mesh.surfaces[1].name = QStringLiteral("test/still");
	ModelViewport view;
	view.resize(320, 240);
	view.show();
	view.setMesh(mesh);
	const QHash<int, QImage> stills {{mesh.surfaces[0].index, solid(qRgb(200, 0, 0))}, {mesh.surfaces[1].index, solid(qRgb(0, 0, 200))}};
	view.setSurfaceSkins(stills);
	const QImage stillScroll = view.surfaceSkinImage(mesh.surfaces[0].index);

	LevelMaterialAnimator animator(&view);
	animator.setPackage(archive, {});
	animator.setSurfaces(mesh, MaterialEngine::Quake3, stills);
	expect(!animator.isEnabled() && animator.animatedMaterials().isEmpty(), "nothing animates until it is turned on");
	animator.setEnabled(true);
	expect(waitFor([&]() { return !animator.isPreparing() && !animator.animatedMaterials().isEmpty(); }), "the package's materials are read");
	expect(animator.animatedMaterials() == QStringList {QStringLiteral("test/scroll")}, "only the scrolling shader animates",
		animator.animatedMaterials().join(QLatin1Char(',')));

	const int renderSkip = vibestudio::test_support::exitCodeWithoutRenderer("level-material-animation-ui-smoke");
	expect(renderSkip != 1, "VIBESTUDIO_RENDER_REQUIRE is set, but no 3D renderer starts");
	if (renderSkip < 0) {
		expect(waitFor([&]() { return animator.framesShown() >= 3; }), "frames keep arriving", QString::number(animator.framesShown()));
		const QImage animated = view.surfaceSkinImage(mesh.surfaces[0].index);
		expect(!animated.isNull() && animated.size() != stillScroll.size(), "the scrolling face shows the material's frames");
		expect(view.surfaceSkinImage(mesh.surfaces[1].index).pixel(1, 1) == solid(qRgb(0, 0, 200)).pixel(1, 1), "the still face keeps its image");
		// Reduced motion stops the frames and puts the still image back.
		animator.setReducedMotion(true);
		const int frames = animator.framesShown();
		waitFor([]() { return false; }, 600);
		expect(animator.framesShown() == frames && view.surfaceSkinImage(mesh.surfaces[0].index).size() == stillScroll.size(),
			"reduced motion keeps every face still");
		animator.setReducedMotion(false);
		expect(waitFor([&]() { return animator.framesShown() > frames; }), "frames come back without reduced motion");
	}
	animator.setEnabled(false);
	expect(view.surfaceSkinImage(mesh.surfaces[0].index).size() == stillScroll.size()
			&& view.surfaceSkinImage(mesh.surfaces[0].index).pixel(1, 1) == stillScroll.pixel(1, 1),
		"turning it off puts the still image back");

	// The real shell, with a package and a map opened as a user opens them.
	{
		StudioSettings settings;
		settings.setReducedMotion(false);
		settings.sync();
		const QString shellRoot = QDir(root).filePath(QStringLiteral("shell"));
		LevelMapDocument map;
		expect(tests::createMaterialFixture(shellRoot, &map, &error), "the Levels fixture is written", error);
		PackageArchive assets;
		expect(assets.load(QDir(shellRoot).filePath(QStringLiteral("assets")), &error), "the fixture assets open", error);
		PackageStagingModel staging;
		staging.loadBaseArchive(assets, &error);
		PackageWriteRequest write;
		write.destinationPath = QDir(shellRoot).filePath(QStringLiteral("fixture.pk3"));
		expect(staging.writeArchive(write).succeeded(), "the fixture package is written");
		LevelDocumentSaveRequest save;
		save.path = QDir(shellRoot).filePath(QStringLiteral("fixture.map"));
		expect(writeLevelDocument(map, save).succeeded(), "the fixture map is written");
		// A Quake III installation whose base package declares the shader the
		// map's floor names: the camera reads it beneath the package.
		const QString baseRoot = QDir(shellRoot).filePath(QStringLiteral("base-files"));
		writeFile(QDir(baseRoot).filePath(QStringLiteral("textures/base/look.png")), tests::materialImage(16, 48));
		writeFile(QDir(baseRoot).filePath(QStringLiteral("scripts/base.shader")), "textures/studio/grid\n{\nqer_editorimage textures/base/look.png\n}\n");
		PackageArchive baseFiles;
		PackageStagingModel baseStaging;
		expect(baseFiles.load(baseRoot, &error) && baseStaging.loadBaseArchive(baseFiles, &error), "the base package files open", error);
		PackageWriteRequest baseWrite;
		baseWrite.destinationPath = QDir(shellRoot).filePath(QStringLiteral("base.pk3"));
		expect(baseStaging.writeArchive(baseWrite).succeeded(), "the base package is written");
		GameInstallationProfile installation;
		installation.gameKey = QStringLiteral("quake3");
		installation.engineFamily = GameEngineFamily::IdTech3;
		installation.displayName = QStringLiteral("Test Quake III");
		installation.rootPath = shellRoot;
		installation.executablePath = QCoreApplication::applicationFilePath();
		installation.basePackagePaths = {QStringLiteral("base.pk3")};
		settings.upsertGameInstallation(installation);
		for (const GameInstallationProfile& profile : settings.gameInstallations()) {
			if (profile.displayName == installation.displayName) {
				settings.setSelectedGameInstallation(profile.id);
			}
		}
		settings.sync();

		std::cerr << "Shell: constructing" << std::endl;
		ApplicationShell shell;
		shell.resize(1440, 950);
		shell.show();
		shell.openPathFromCommandLine(write.destinationPath);
		shell.openPathFromCommandLine(save.path);
		auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
		auto* cameraButton = shell.findChild<QToolButton*>(QStringLiteral("levelMap3DButton"));
		auto* animate = shell.findChild<QAction*>(QStringLiteral("map.animateMaterials"));
		auto* workbench = shell.findChild<MaterialWorkbench*>();
		if (!expect(camera && cameraButton && animate && workbench, "the shell has the Levels camera, the command and the Materials page")) {
			return 1;
		}
		std::cerr << "Shell: showing the Levels camera" << std::endl;
		cameraButton->setChecked(true);
		expect(waitFor([&]() { return camera->isEnabled() && camera->hasMesh() && camera->hasSkin(); }), "the Levels camera shows the textured map");
		int animatedSurface = -1;
		int floorSurface = -1;
		for (const ModelSurface& surface : camera->mesh().surfaces) {
			if (surface.name == QStringLiteral("studio/animated")) {
				animatedSurface = surface.index;
			} else if (surface.name == QStringLiteral("studio/grid")) {
				floorSurface = surface.index;
			}
		}
		expect(animatedSurface >= 0 && floorSurface >= 0, "the camera has the animated shader's face and the floor");
		// The base package loads in the background; the camera reads again then.
		expect(waitFor([&]() { return camera->isEnabled() && camera->surfaceSkinImage(floorSurface).size() == QSize(16, 48); }),
			"the floor shows the base package's shader image, beneath the package",
			QStringLiteral("%1x%2").arg(camera->surfaceSkinImage(floorSurface).width()).arg(camera->surfaceSkinImage(floorSurface).height()));
		// The texture list says so in words.
		const auto baseTip = [&shell]() {
			auto* textures = shell.findChild<QListWidget*>(QStringLiteral("levelMapTextures"));
			for (int row = 0; textures && row < textures->count(); ++row) {
				if (textures->item(row)->data(Qt::UserRole).toString() == QStringLiteral("studio/grid")) {
					return textures->item(row)->toolTip();
				}
			}
			return QString();
		};
		expect(waitFor([&]() { return baseTip().contains(QStringLiteral("from the game's base packages")); }),
			"the Levels texture list says the floor's image comes from the game's base packages", baseTip());
		const QImage still = camera->surfaceSkinImage(animatedSurface);
		expect(animate->isCheckable() && !animate->isChecked(), "Animate Materials in Camera is off until turned on");
		std::cerr << "Shell: animating materials" << std::endl;
		animate->trigger();
		auto* shellAnimator = dynamic_cast<LevelMaterialAnimator*>(shell.findChild<QObject*>(QStringLiteral("levelMaterialAnimator")));
		expect(animate->isChecked() && shellAnimator && shellAnimator->isEnabled(), "the command turns the camera's animator on");
		if (shellAnimator) {
			expect(waitFor([&]() { return !shellAnimator->isPreparing() && shellAnimator->animatedMaterials().contains(QStringLiteral("studio/animated")); }),
				"the map's animated shader is found through the Materials lookup", shellAnimator->animatedMaterials().join(QLatin1Char(',')));
			if (renderSkip < 0) {
				expect(waitFor([&]() { return shellAnimator->framesShown() >= 2 && camera->surfaceSkinImage(animatedSurface).size() != still.size(); }),
					"the animated face shows the material's frames in the shell", QString::number(shellAnimator->framesShown()));
			}
			// The Materials page's game choice reaches Levels: the preview reads
			// again and the animator reads the package for that game.
			std::cerr << "Shell: changing the Materials page's game" << std::endl;
			workbench->setDialect(MaterialDialect::JediKnight);
			expect(waitFor([&]() { return shellAnimator->dialect() == MaterialDialect::JediKnight && camera->isEnabled() && camera->hasSkin(); }),
				"Levels follows the Materials page's Quake III game");
			workbench->setDialect(MaterialDialect::Automatic);
			expect(waitFor([&]() { return shellAnimator->dialect() == MaterialDialect::Automatic && camera->isEnabled() && camera->hasSkin(); }),
				"Levels follows the Materials page back to Automatic");
		}
		std::cerr << "Shell: stopping the animation" << std::endl;
		animate->trigger();
		expect(!animate->isChecked() && (!shellAnimator || !shellAnimator->isEnabled()), "the command turns the animator off again");
		expect(waitFor([&]() { return camera->surfaceSkinImage(animatedSurface).size() == still.size(); }),
			"turning it off puts the face's still image back");
		expect(StudioSettings().shellLayoutState(QStringLiteral("levelAnimateMaterials")) == QByteArrayLiteral("off"), "the choice is kept");
		expect(shell.close(), "the shell closes cleanly");
	}

	if (failures > 0) {
		std::cerr << failures << " level material animation check(s) failed\n";
		return 1;
	}
	std::cout << "level material animation UI smoke passed\n";
	return 0;
}
