// Animated materials on the Levels camera: a face whose Quake III shader
// scrolls gets new frames while the animator runs, a still face keeps its
// image, turning it off puts the still image back, and reduced motion stops
// the frames. Every file is generated here; no game data.

#include "app/level_material_animator.h"
#include "app/model_viewport.h"
#include "core/model_mesh.h"
#include "core/package_archive.h"
#include "tests/render_test_support.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
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

	if (failures > 0) {
		std::cerr << failures << " level material animation check(s) failed\n";
		return 1;
	}
	std::cout << "level material animation UI smoke passed\n";
	return 0;
}
