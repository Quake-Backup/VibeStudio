#include "app/model_viewport.h"
#include "app/model_material_worker.h"
#include "app/model_viewport_p.h"
#include "core/model_document.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QImage>
#include <QMouseEvent>
#include <QThread>
#include <QTimer>
#include <QTranslator>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>

using namespace slopstudio;
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

class ExpandedStatus final : public QTranslator
{
  public:
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		if (QByteArray(context) == "slopstudio::ModelViewport" && QString::fromUtf8(source) == QStringLiteral("Rendering model…"))
		{
			return QStringLiteral("Rendering the model preview with an expanded translated status message…");
		}
		return {};
	}
};

ModelMesh crossing(bool perspective)
{
	ModelMesh mesh;
	mesh.format = ModelMeshFormat::Quake3Md3;
	mesh.sourcePath = QStringLiteral("tests/crossing.md3");
	mesh.frames.resize(1);
	mesh.frames[0].name = QStringLiteral("test");
	for (int surface = 0; surface < 2; ++surface)
	{
		ModelSurface part;
		part.name = surface == 0 ? QStringLiteral("red") : QStringLiteral("blue");
		part.triangles = {{0, 1, 2}};
		part.texCoords = {{0, 0}, {1, 0}, {0, 1}};
		ModelFrameGeometry frame;
		if (perspective)
		{
			const float depth[] = {surface == 0 ? 8.0f : 4.0f, surface == 0 ? 2.0f : 4.0f, surface == 0 ? 8.0f : 4.0f};
			// All triangles project to (96,96), (416,96), (96,416).
			frame.positions = {{depth[0], depth[0] * 0.625f, depth[0] * 0.625f},
							   {depth[1], -depth[1] * 0.625f, depth[1] * 0.625f},
							   {depth[2], depth[2] * 0.625f, -depth[2] * 0.625f}};
		}
		else
		{
			frame.positions = {
				{surface == 0 ? 0.0f : 4.0f, -40, 40}, {surface == 0 ? 10.0f : 4.0f, 40, 40}, {surface == 0 ? 0.0f : 4.0f, -40, -40}};
		}
		frame.normals = {{1, 0, 0}, {1, 0, 0}, {1, 0, 0}};
		part.frames.append(frame);
		mesh.surfaces.append(part);
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}

bool red(const QImage &image, const QPoint &point)
{
	const auto color = image.pixelColor(point);
	return color.red() > 50 && color.red() > color.blue() * 2;
}

bool consistentStrokePicks(ModelViewport& viewport)
{
	// Sample subpixel silhouette/occlusion boundaries before the stroke, then
	// require the held gesture to resolve exactly the same source triangles.
	// No shell worker is attached: all hits belong to this owned test widget.
	QVector<QPair<QPointF, int>> samples;
	for (int y = 80; y < 440; y += 3) {
		for (int x = 80; x < 440; x += 3) {
			for (double offset : {0.0, 0.25, 0.75}) {
				const QPointF point(x+offset,y+offset); const auto hit = viewport.hitAt(point);
				samples.append({point,hit.valid ? hit.triangle : -1});
			}
		}
	}
	const auto first = std::find_if(samples.cbegin(),samples.cend(),[](const auto& sample) { return sample.second >= 0; });
	if (!expect(first != samples.cend(),"stroke oracle contains visible surfaces")) { return false; }
	viewport.setMaterialGesturesEnabled(true);
	int touched = -1;
	const auto connection = QObject::connect(&viewport,&ModelViewport::materialStrokeTouched,&viewport,
		[&](int triangle,CameraMaterialGesture) { touched = triangle; });
	bool ok = expect(viewport.beginMaterialStroke(first->first,Qt::MiddleButton,Qt::ShiftModifier),"stroke oracle begins");
	const auto check = [&] {
		int mismatch = 0;
		for (const auto& sample : samples) {
			touched = -1;
			const bool hit = viewport.extendMaterialStroke(sample.first,Qt::ShiftModifier);
			mismatch += touched != sample.second || hit != (sample.second >= 0);
		}
		if (mismatch) { std::cerr << "Stroke picking mismatches: " << mismatch << '\n'; }
		return expect(mismatch == 0,"held stroke and initial picking agree at pixel boundaries");
	};
	ok &= check();
	auto preview = viewport.mesh();
	std::reverse(preview.surfaces.begin(),preview.surfaces.end());
	viewport.setMaterialStrokePreview(preview,{}, {},0);
	ok &= expect(tests::settleModelViewport(viewport),"regrouped stroke preview renders");
	ok &= check();
	viewport.finishMaterialStroke(false);
	QObject::disconnect(connection);
	viewport.setMaterialGesturesEnabled(false);
	ok &= expect(tests::settleModelViewport(viewport),"cancel restores original stroke frame");
	return ok;
}

// A flat two-triangle sheet seen from above, with `frames` poses.
ModelMesh sheetModel(int frames = 1)
{
	ModelMesh sheet;
	sheet.geometryAvailable = true;
	sheet.format = ModelMeshFormat::Quake3Md3;
	sheet.sourcePath = QStringLiteral("tests/render-savings-sheet.md3");
	sheet.frames.resize(frames);
	ModelSurface surface;
	surface.name = QStringLiteral("sheet");
	surface.triangles = {{0, 1, 2}, {1, 3, 2}};
	surface.texCoords = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
	for (int frame = 0; frame < frames; ++frame)
	{
		sheet.frames[frame].name = QStringLiteral("pose%1").arg(frame);
		const float lift = float(frame) * 4;
		surface.frames.append(
			{{{-40, -40, lift}, {40, -40, lift}, {-40, 40, lift}, {40, 40, lift}}, {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}}});
	}
	sheet.surfaces.append(surface);
	updateEditableModelMetadata(&sheet);
	return sheet;
}

void prepareSheetView(ModelViewport &view, const ModelMesh &mesh, int side = 256)
{
	view.resize(side, side);
	view.setShowGrid(false);
	view.setShowAxes(false);
	view.setBackfaceCulling(false);
	view.setMesh(mesh);
	view.setOrbit(0, 90);
	view.show();
}

void sendMouse(ModelViewport &view, QEvent::Type type, const QPointF &point, Qt::MouseButton button, Qt::MouseButtons buttons)
{
	QMouseEvent event(type, point, view.mapToGlobal(point), button, buttons, Qt::NoModifier);
	QApplication::sendEvent(&view, &event);
}

// Where the presented frame drew each triangle: the centre of its hits.
QVector<QPointF> triangleCentres(ModelViewport &view, int triangles)
{
	QVector<QPointF> sum(triangles);
	QVector<int> count(triangles);
	for (int y = 4; y < view.height(); y += 6)
	{
		for (int x = 4; x < view.width(); x += 6)
		{
			const auto hit = view.hitAt(QPointF(x, y));
			if (hit.valid && hit.triangle >= 0 && hit.triangle < triangles)
			{
				sum[hit.triangle] += QPointF(x, y);
				++count[hit.triangle];
			}
		}
	}
	for (int index = 0; index < triangles; ++index)
	{
		sum[index] = count[index] ? sum[index] / count[index] : QPointF(-1, -1);
	}
	return sum;
}

int pixelsLike(const QImage &image, const std::function<bool(const QColor &)> &test)
{
	int count = 0;
	for (int y = 0; y < image.height(); ++y)
	{
		for (int x = 0; x < image.width(); ++x)
		{
			count += test(image.pixelColor(x, y)) ? 1 : 0;
		}
	}
	return count;
}

// Pointing at another triangle repaints the hover outline over the frame on
// show, in every display mode and palette, without rendering again.
bool hoverWithoutRendering()
{
	bool ok = true;
	ModelViewport view;
	prepareSheetView(view, sheetModel());
	QImage grey(8, 8, QImage::Format_RGB32);
	grey.fill(QColor(96, 96, 96));
	view.setSkin(grey);
	int renders = 0;
	QObject::connect(&view, &ModelViewport::renderCompleted, &view, [&] { ++renders; });
	for (const bool highContrast : {false, true})
	{
		for (const auto mode : {ModelViewportRenderMode::FlatShaded, ModelViewportRenderMode::Textured, ModelViewportRenderMode::Wireframe})
		{
			view.setHighContrast(highContrast);
			view.setRenderMode(mode);
			QEvent leave(QEvent::Leave);
			QApplication::sendEvent(&view, &leave);
			ok &= expect(tests::settleModelViewport(view), "hover fixture renders");
			const auto centres = triangleCentres(view, 2);
			if (!expect(centres[0].x() >= 0 && centres[1].x() >= 0, "both sheet triangles are visible"))
			{
				return false;
			}
			const auto hover = [highContrast](const QColor &c) {
				return highContrast ? c.red() > 230 && c.green() > 230 && c.blue() < 60 : c.red() > 235 && c.green() > 170 && c.blue() < 140;
			};
			QImage image(view.size(), QImage::Format_RGB32);
			view.render(&image);
			ok &= expect(pixelsLike(image, hover) == 0, "no hover outline is drawn before the pointer arrives");
			renders = 0;
			QImage previous;
			for (const auto &centre : centres)
			{
				sendMouse(view, QEvent::MouseMove, centre, Qt::NoButton, Qt::NoButton);
				view.render(&image);
				ok &= expect(!view.isRendering(), "pointing at another triangle starts no render");
				ok &= expect(pixelsLike(image, hover) > 100, "the hovered triangle is outlined in the hover colour");
				ok &= expect(previous.isNull() || previous != image, "the outline follows the pointer to the next triangle");
				previous = image;
			}
			QApplication::processEvents();
			ok &= expect(renders == 0 && !view.isRendering(), "hover changes leave the frame and the render worker alone");
		}
	}
	return ok;
}

// A mirror pane whose copy of the display is unchanged keeps its frame when
// the main view finishes one of its own; a real change redraws it.
bool mirrorsRenderOnlyOnChange()
{
	bool ok = true;
	ModelViewport main, mirror;
	prepareSheetView(main, sheetModel(2));
	mirror.resize(main.size());
	mirror.show();
	ok &= expect(tests::settleModelViewport(main), "main view renders");
	mirror.mirrorDisplayFrom(main);
	mirror.setOrbit(30, 30);
	ok &= expect(tests::settleModelViewport(mirror), "mirror renders its first frame");
	int renders = 0;
	QObject::connect(&mirror, &ModelViewport::renderCompleted, &mirror, [&] { ++renders; });
	QImage probe(mirror.size(), QImage::Format_ARGB32_Premultiplied);
	main.setOrbit(10, 60);
	ok &= expect(tests::settleModelViewport(main), "main view renders its own camera move");
	mirror.mirrorDisplayFrom(main);
	mirror.render(&probe);
	ok &= expect(!mirror.isRendering() && renders == 0, "an unchanged display does not redraw the mirror");
	main.setHighlightedTriangles({1});
	ok &= expect(tests::settleModelViewport(main), "main view renders the selection");
	mirror.mirrorDisplayFrom(main);
	mirror.render(&probe);
	ok &= expect(mirror.isRendering() && tests::settleModelViewport(mirror) && mirror.highlightedTriangleCount() == 1,
				 "a new selection redraws the mirror");
	main.setFrame(1);
	ok &= expect(tests::settleModelViewport(main), "main view renders the next pose");
	mirror.mirrorDisplayFrom(main);
	mirror.render(&probe);
	ok &= expect(mirror.isRendering() && tests::settleModelViewport(mirror) && mirror.frame() == 1, "a new frame redraws the mirror");
	renders = 0;
	mirror.mirrorDisplayFrom(main);
	mirror.render(&probe);
	ok &= expect(!mirror.isRendering() && renders == 0, "syncing again changes nothing");
	return ok;
}

// The edit surface's projected vertices survive a redraw that keeps the
// camera and pose, and match a view that never had them.
bool vertexProjectionReuse()
{
	bool ok = true;
	ModelViewport view, fresh;
	for (auto *each : {&view, &fresh})
	{
		prepareSheetView(*each, sheetModel());
		each->setEditSelection(0, {0, 3});
		each->setVertexPicking(true);
	}
	fresh.setHighlightedTriangles({0});
	ok &= expect(tests::settleModelViewport(view) && tests::settleModelViewport(fresh), "vertex fixtures render");
	const auto before = view.editVertexProjection();
	view.setHighlightedTriangles({0});
	ok &= expect(tests::settleModelViewport(view), "selection redraw completes");
	const auto after = view.editVertexProjection();
	const auto expected = fresh.editVertexProjection();
	ok &= expect(before && after == before, "a redraw with the same camera and pose keeps the projected vertices");
	ok &= expect(after && expected && after->positions == expected->positions && after->visible == expected->visible,
				 "kept vertices match those of a view drawn from scratch");
	ok &= expect(view.vertexAt(view.vertexScreenPosition(0, 3)).vertex == 3, "kept vertices still pick");
	view.setOrbit(20, 60);
	ok &= expect(tests::settleModelViewport(view) && view.editVertexProjection() != before, "a camera change projects the vertices again");
	return ok;
}

// An in-view skin-paint stroke ends when the view loses the button, focus or
// visibility, as Escape ends it, so hover never paints.
bool skinPaintStrokeEnds()
{
	bool ok = true;
	ModelViewport view;
	prepareSheetView(view, sheetModel());
	ok &= expect(tests::settleModelViewport(view), "skin paint fixture renders");
	QVector<ModelViewportStrokePhase> phases;
	view.setSkinPaintHandler([&](ModelViewportStrokePhase phase, const QPointF &) {
		phases << phase;
		return true;
	});
	const QPointF at(128, 128);
	sendMouse(view, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
	sendMouse(view, QEvent::MouseMove, at + QPointF(4, 0), Qt::NoButton, Qt::LeftButton);
	ok &= expect(view.skinPainting() && phases == QVector<ModelViewportStrokePhase>{ModelViewportStrokePhase::Begin, ModelViewportStrokePhase::Extend},
				 "a held drag paints");
	sendMouse(view, QEvent::MouseMove, at + QPointF(8, 0), Qt::NoButton, Qt::NoButton);
	ok &= expect(!view.skinPainting() && phases.last() == ModelViewportStrokePhase::Cancel && phases.size() == 3,
				 "a move without the button cancels the stroke instead of painting on hover");
	sendMouse(view, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
	ok &= expect(view.skinPainting() && phases.last() == ModelViewportStrokePhase::Begin, "the next press starts a new stroke");
	QFocusEvent focusOut(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
	QApplication::sendEvent(&view, &focusOut);
	ok &= expect(!view.skinPainting() && phases.last() == ModelViewportStrokePhase::Cancel, "losing focus cancels the stroke");
	sendMouse(view, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
	view.hide();
	ok &= expect(!view.skinPainting() && phases.last() == ModelViewportStrokePhase::Cancel, "hiding the view cancels the stroke");
	view.setSkinPaintHandler({});
	return ok;
}

// The Game preview takes new pixels on the same surfaces without blanking
// the view, and the same images without drawing again.
bool gameSkinsKeepTheFrame()
{
	bool ok = true;
	ModelViewport view;
	prepareSheetView(view, sheetModel());
	view.setRenderMode(ModelViewportRenderMode::Textured);
	QImage grey(8, 8, QImage::Format_RGB32), painted(8, 8, QImage::Format_RGB32);
	grey.fill(QColor(96, 96, 96));
	painted.fill(Qt::red);
	view.setSkin(grey);
	view.setGameSkins({{0, grey}}, {}, false);
	ok &= expect(view.gameSkinsActive() && tests::settleModelViewport(view), "Game preview renders");
	QImage image(view.size(), QImage::Format_RGB32);
	view.setGameSkins({{0, grey}}, {}, false);
	view.render(&image);
	ok &= expect(!view.isRendering(), "the same Game preview images do not draw again");
	view.setGameSkins({{0, painted}}, {}, false);
	view.render(&image);
	ok &= expect(image.pixelColor(128, 128) != QColor(20, 22, 27), "new pixels on the same surfaces keep the frame on show");
	ok &= expect(tests::settleModelViewport(view), "the repainted skin renders");
	view.render(&image);
	ok &= expect(red(image, {128, 128}), "the frame shows the new pixels");
	return ok;
}

// A hidden view stops ticking playback and resumes in place when shown.
bool hiddenPlaybackPauses()
{
	bool ok = true;
	ModelViewport view;
	prepareSheetView(view, sheetModel(3));
	view.setReducedMotion(false);
	view.setFramesPerSecond(10);
	view.play();
	auto *timer = view.findChild<QTimer *>(QStringLiteral("modelPlaybackTimer"));
	ok &= expect(timer && view.isPlaying() && timer->isActive(), "playback ticks while shown");
	view.hide();
	ok &= expect(timer && view.isPlaying() && !timer->isActive(), "a hidden view does not tick playback");
	const int frame = view.frame();
	view.show();
	ok &= expect(timer && timer->isActive() && view.isPlaying() && view.frame() == frame, "showing the view resumes playback where it was");
	view.pause();
	return ok;
}

// Cancelling a transform keeps the last frame on show until the next one is ready.
bool cancelledTransformKeepsTheFrame()
{
	bool ok = true;
	ModelViewport view;
	prepareSheetView(view, sheetModel());
	view.setEditSelection(0, {0, 1});
	view.setTransformGizmo(true, ModelTransformTool::Move);
	ok &= expect(tests::settleModelViewport(view), "transform fixture renders");
	QImage original(view.size(), QImage::Format_RGB32), image(view.size(), QImage::Format_RGB32);
	view.render(&original);
	const auto surface = [](const QColor &c) { return c.blue() > 150 && c.red() < 200; };
	auto centre = view.transformGizmoPoints()[3];
	// A gesture that never moved anything showed no preview: the frame stays.
	ok &= expect(view.beginEditTransform(centre), "the transform starts");
	view.finishEditTransform(false);
	view.render(&image);
	ok &= expect(pixelsLike(image, surface) > 2000, "cancelling a transform that showed no preview does not blank the view");
	ok &= expect(tests::settleModelViewport(view), "the view settles after the untravelled cancel");
	view.render(&image);
	ok &= expect(image == original, "an untravelled cancel leaves the pose as it was");
	// A cancelled preview leaves the view at once and the source pose returns.
	centre = view.transformGizmoPoints()[3];
	ok &= expect(view.beginEditTransform(centre) && view.updateEditTransform(centre + QPointF(24, 0)) && tests::settleModelViewport(view),
				 "the transform preview renders");
	view.finishEditTransform(false);
	ok &= expect(tests::settleModelViewport(view), "the view renders after the cancel");
	view.render(&image);
	ok &= expect(image == original, "the cancelled transform draws the pose it started from");
	return ok;
}

// The material worker asks again when any embedded texture changes.
bool materialRequestsEveryEmbeddedSkin()
{
	bool ok = true;
	ModelMaterialWorker worker;
	int starts = 0;
	worker.started = [&] { ++starts; };
	auto mesh = sheetModel();
	for (int index = 0; index < 2; ++index)
	{
		ModelEmbeddedSkin skin;
		skin.index = index;
		skin.name = QStringLiteral("skin%1").arg(index);
		skin.image = QImage(4, 4, QImage::Format_RGB32);
		skin.image.fill(Qt::white);
		mesh.embeddedSkins.append(skin);
	}
	worker.request(mesh, {});
	worker.request(mesh, {});
	ok &= expect(starts == 1, "an unchanged request is not repeated");
	mesh.embeddedSkins[1].image.setPixelColor(0, 0, Qt::black);
	worker.request(mesh, {});
	ok &= expect(starts == 2, "editing a later embedded texture requests the materials again");
	worker.reset();
	return ok;
}

// Translucency is found by scanning the image itself, in any format.
bool skinAlphaScan()
{
	bool ok = true;
	QImage opaque(5, 3, QImage::Format_RGB32), solid(5, 3, QImage::Format_ARGB32), indexed(2, 2, QImage::Format_Indexed8);
	opaque.fill(Qt::red);
	solid.fill(Qt::red);
	ok &= expect(!modelSkinHasAlpha(opaque) && !modelSkinHasAlpha(solid) && !modelSkinHasAlpha(QImage()), "opaque images have no alpha");
	solid.setPixelColor(4, 2, QColor(255, 0, 0, 254));
	ok &= expect(modelSkinHasAlpha(solid) && modelSkinHasAlpha(solid.convertToFormat(QImage::Format_ARGB32_Premultiplied)),
				 "one translucent pixel in the last corner counts");
	solid.setPixelColor(4, 2, Qt::red);
	ok &= expect(!modelSkinHasAlpha(solid), "a changed image is scanned again");
	indexed.setColorTable({qRgba(0, 0, 0, 255), qRgba(0, 0, 0, 0)});
	indexed.fill(0);
	ok &= expect(!modelSkinHasAlpha(indexed), "an opaque palette image has no alpha");
	indexed.setPixel(1, 1, 1);
	ok &= expect(modelSkinHasAlpha(indexed), "a palette image with a clear entry in use has alpha");
	return ok;
}
} // namespace

int main(int argc, char **argv)
{
	// Direct widget calls and in-process QImage rendering; no input injection or
	// desktop/window captures are used by this integration test.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	// Draws in 3D: skip where no OpenGL or Vulkan renderer starts.
	if (const int skip = slopstudio::test_support::exitCodeWithoutRenderer("model-viewport-smoke"); skip >= 0) {
		return skip;
	}
	const auto savePreview = [&](ModelViewport &preview, const QString &name)
	{
		const auto evidence = qEnvironmentVariable("SLOPSTUDIO_MODELLER_EVIDENCE");
		if (evidence.isEmpty())
		{
			return true;
		}
		const auto ratio = preview.devicePixelRatioF();
		QImage capture(preview.size() * ratio, QImage::Format_ARGB32_Premultiplied);
		capture.setDevicePixelRatio(ratio);
		preview.render(&capture);
		return QDir().mkpath(evidence) && capture.save(QDir(evidence).filePath(name));
	};
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	bool ok = true;
	ModelViewport viewport;
	viewport.resize(512, 512);
	viewport.setBackfaceCulling(false);
	viewport.setShowGrid(false);
	viewport.setShowAxes(false);
	QImage redSkin(4, 4, QImage::Format_RGB32), blueSkin(4, 4, QImage::Format_RGB32);
	redSkin.fill(Qt::red);
	blueSkin.fill(Qt::blue);
	viewport.setRenderMode(ModelViewportRenderMode::Textured);
	for (bool perspective : {false, true})
	{
		viewport.setMesh(crossing(perspective));
		CameraViewControls controls;
		controls.perspective = perspective;
		controls.surfaceValuesButton = Qt::MiddleButton;
		controls.surfaceValuesModifiers = Qt::ShiftModifier;
		viewport.setCameraControls(controls);
		if (perspective)
		{
			viewport.setCameraView({0, 0, 0}, 0, 0);
		}
		else
		{
			viewport.setOrbit(0, 0);
		}
		viewport.setSurfaceSkins({{0, redSkin}, {1, blueSkin}});
		viewport.show();
		app.processEvents();
		const QPoint rear = perspective ? QPoint(120, 150) : QPoint(int(256 - 30 * viewport.zoom()), int(256 - 20 * viewport.zoom()));
		const QPoint front = perspective ? QPoint(370, 130) : QPoint(int(256 + 20 * viewport.zoom()), int(256 - 30 * viewport.zoom()));
		QImage image(viewport.size(), QImage::Format_RGB32);
		ok &= expect(tests::settleModelViewport(viewport), "camera preview worker completes");
		viewport.render(&image);
		ok &= expect(!red(image, rear) && red(image, front), "both camera projections draw intersecting surfaces at their actual depth");
		ok &= expect(viewport.hitAt(rear).surface == 1 && viewport.hitAt(front).surface == 0, "widget hit query follows visible depth");
		const QPointF nearEdge = perspective ? QPointF(130, 97) : QPointF(256 - 30 * viewport.zoom(), 256 - 40 * viewport.zoom() + 1);
		const auto edge = viewport.edgeAt(nearEdge);
		ok &= expect(edge.valid && edge.surface == 1 && edge.a == 0 && edge.b == 1 && !viewport.edgeAt(rear, 0.25).valid,
					 "edge picking follows the visible face in both camera projections and respects its distance tolerance");
		viewport.setHighlightedEdges(0, {{0, 1}});
		ok &= expect(viewport.highlightedEdgeCount() == 1 && tests::settleModelViewport(viewport),
					 "selected indexed edge completes its raster update");
		viewport.render(&image);
		ok &= expect(!red(image, rear), "hidden selected edges do not draw through the front surface");
		viewport.setHighlightedEdges(-1, {});
		const auto evidence = qEnvironmentVariable("SLOPSTUDIO_MODELLER_EVIDENCE");
		if (!evidence.isEmpty())
		{
			QDir().mkpath(evidence);
			ok &= expect(image.save(QDir(evidence).filePath(perspective ? QStringLiteral("intersection-perspective.png")
																		: QStringLiteral("intersection-orthographic.png"))),
						 "save viewport-owned render evidence");
		}
		viewport.setHighlightedTriangles({0});
		ok &= expect(tests::settleModelViewport(viewport), "selection preview worker completes");
		viewport.render(&image);
		ok &= expect(!red(image, rear), "selected mesh remains occluded by foreground geometry");
		viewport.setHighlightedTriangles({});
		ok &= expect(tests::settleModelViewport(viewport),"stroke oracle starts from a completed frame");
		ok &= consistentStrokePicks(viewport);
	}
	auto clipped = crossing(true);
	clipped.surfaces.resize(1);
	clipped.surfaces[0].frames[0].positions = {{0.5f, 0, 1}, {4, 2, -2}, {4, -2, -2}};
	clipped.surfaces[0].texCoords = {{0.5f, 0}, {0, 1}, {1, 1}};
	updateEditableModelMetadata(&clipped);
	viewport.setMesh(clipped);
	viewport.setCameraView({0, 0, 0}, 0, 0);
	viewport.setSkin(redSkin);
	QImage image(viewport.size(), QImage::Format_RGB32);
	ok &= expect(tests::settleModelViewport(viewport), "near-clipped preview worker completes");
	viewport.render(&image);
	ok &= expect(red(image, {256, 250}) && viewport.hitAt({256, 250}).surface == 0,
				 "near-plane clipping retains texture coordinates and picking");
	ok &= consistentStrokePicks(viewport);
	const auto unselected = image.copy();
	ok &= expect(!viewport.edgeAt({256, 112}).valid && viewport.edgeAt({158, 246}).valid && viewport.edgeAt({158, 246}).a == 0 &&
					 viewport.edgeAt({158, 246}).b == 1,
				 "near-plane cut is not a model edge while clipped original edges remain pickable");
	viewport.setHighlightedEdges(0, {{0, 1}, {0, 2}, {1, 2}});
	ok &= expect(tests::settleModelViewport(viewport), "clipped edge highlighting completes");
	viewport.render(&image);
	const auto differences = [&](QRect region)
	{
		int changed = 0;
		for (int y = region.top(); y <= region.bottom(); ++y)
		{
			for (int x = region.left(); x <= region.right(); ++x)
			{
				changed += image.pixel(x, y) != unselected.pixel(x, y);
			}
		}
		return changed;
	};
	ok &= expect(differences({230, 110, 50, 5}) == 0 && differences({220, 220, 70, 60}) == 0 && differences({149, 239, 18, 12}) > 4,
				 "selected original edges remain visible without highlighting the clipping boundary or internal fan diagonal");
	viewport.setRenderMode(ModelViewportRenderMode::Wireframe);
	ok &= expect(tests::settleModelViewport(viewport), "clipped selected wireframe finishes");
	viewport.render(&image);
	const auto orangePixels = [&](QRect area)
	{
		int count = 0;
		for (int y = area.top(); y <= area.bottom(); ++y)
		{
			for (int x = area.left(); x <= area.right(); ++x)
			{
				const auto color = image.pixelColor(x, y);
				count += color.red() > 100 && color.red() > color.green() * 1.25 && color.red() > color.blue() * 2;
			}
		}
		return count;
	};
	ok &= expect(orangePixels({230, 107, 50, 7}) == 0 && orangePixels({149, 239, 18, 16}) > 3 &&
					 image.pixelColor(256, 250) == QColor(20, 22, 27),
				 "wireframe preserves clipped source edges without a selected clipping cut, fan diagonal or filled interior");
	ok &= expect(savePreview(viewport, QStringLiteral("wireframe-clipped-selection.png")), "save clipped wireframe evidence");
	viewport.setRenderMode(ModelViewportRenderMode::Textured);
	for (auto &point : clipped.surfaces[0].frames[0].positions)
	{
		point.x = 0.5f;
	}
	updateEditableModelMetadata(&clipped);
	viewport.setMesh(clipped);
	viewport.setCameraView({0, 0, 0}, 0, 0);
	ok &= expect(tests::settleModelViewport(viewport), "fully clipped preview retires prior work");
	viewport.render(&image);
	ok &= expect(!viewport.hitAt({256, 250}).valid && !red(image, {256, 250}),
				 "fully clipped triangles leave no stale render or pick result");
	viewport.setMesh(crossing(true));
	viewport.setCameraView({0, 0, 0}, 0, 0);
	viewport.render(&image);
	ok &= expect(viewport.isRendering(), "paint queues raster work and returns without waiting for the worker");
	for (int i = 0; i < 16; ++i)
	{
		viewport.setSkin(i % 2 == 0 ? redSkin : blueSkin);
		viewport.setCameraView({0, 0, 0}, i % 2, 0);
		viewport.render(&image);
	}
	int runningThreads = 0;
	for (auto *thread : viewport.findChildren<QThread *>())
	{
		runningThreads += thread->isRunning() ? 1 : 0;
	}
	ok &= expect(runningThreads <= 1 && viewport.isRendering(), "rapid changes keep at most one render worker active");
	viewport.setCameraView({0, 0, 0}, 0, 0);
	ok &= expect(tests::settleModelViewport(viewport), "latest requested preview eventually completes after cancellation");
	viewport.render(&image);
	ok &= expect(!red(image, {370, 130}), "retired renders cannot overwrite the latest blue material");
	viewport.setSkin(redSkin);
	viewport.render(&image);
	viewport.clearMesh();
	app.processEvents();
	ok &= expect(!viewport.hitAt({256, 250}).valid, "clearing the mesh clears picking data");
	{
		ModelMesh sheet;
		sheet.geometryAvailable = true;
		sheet.format = ModelMeshFormat::Quake3Md3;
		sheet.sourcePath = QStringLiteral("tests/wire-sheet.md3");
		sheet.frames.resize(1);
		sheet.frames[0].name = QStringLiteral("sheet");
		ModelSurface surface;
		surface.name = QStringLiteral("sheet");
		surface.triangles = {{0, 1, 2}, {1, 3, 2}};
		surface.texCoords = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
		surface.frames.append({{{-40, -40, 0}, {40, -40, 0}, {-40, 40, 0}, {40, 40, 0}}, {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}}});
		sheet.surfaces.append(surface);
		updateEditableModelMetadata(&sheet);
		ok &= expect(validateEditableModel(sheet).isEmpty() && sheet.vertexCount == 4,
					 "wireframe selection fixture is a valid editable mesh");
		ModelViewport wire;
		wire.resize(512, 512);
		wire.setShowGrid(false);
		wire.setShowAxes(false);
		wire.setBackfaceCulling(false);
		wire.setRenderMode(ModelViewportRenderMode::Wireframe);
		wire.setMesh(sheet);
		wire.setOrbit(0, 90);
		wire.setHighlightedTriangles({0});
		ok &= expect(tests::settleModelViewport(wire), "selected shared-edge wireframe completes");
		QImage first(wire.size(), QImage::Format_ARGB32_Premultiplied), second = first;
		wire.render(&first);
		std::swap(sheet.surfaces[0].triangles[0], sheet.surfaces[0].triangles[1]);
		wire.setMesh(sheet);
		wire.setOrbit(0, 90);
		wire.setHighlightedTriangles({1});
		ok &= expect(tests::settleModelViewport(wire), "reordered selected wireframe completes");
		wire.render(&second);
		ok &= expect(first.copy(50, 70, 412, 392) == second.copy(50, 70, 412, 392),
					 "unselected neighbouring faces cannot cover the selected wire's shared edge");
		wire.setHighContrast(true);
		ok &= expect(tests::settleModelViewport(wire), "high-visibility wireframe completes");
		wire.render(&second);
		int selectedPixels = 0;
		for (int y = 70; y < 462; ++y)
		{
			for (int x = 50; x < 462; ++x)
			{
				const auto color = second.pixelColor(x, y);
				selectedPixels += color.red() > 180 && color.blue() > 180 && color.green() < 80;
			}
		}
		ok &= expect(selectedPixels > 100 && second.pixelColor(256, 360) == QColor(Qt::black),
					 "high-visibility wireframe retains selected edge cues and an unfilled interior");
		ok &= expect(savePreview(wire, QStringLiteral("wireframe-high-visibility.png")), "save high-visibility wireframe evidence");
		ExpandedStatus expansion;
		app.installTranslator(&expansion);
		QFont expandedFont = wire.font();
		expandedFont.setPointSizeF(expandedFont.pointSizeF() * 2);
		wire.setFont(expandedFont);
		wire.resize(280, 420);
		wire.setLayoutDirection(Qt::RightToLeft);
		wire.setOrbit(15, 75);
		ok &= expect(savePreview(wire, QStringLiteral("wireframe-expanded-pending.png")), "save expanded rendering-status evidence");
		ok &= expect(tests::settleModelViewport(wire), "expanded viewport remains usable after rendering");
		app.removeTranslator(&expansion);
	}
	{
		// A texture coordinate preview (the level camera's UV tool) rewrites
		// only its triangles' uvs in the last upload: the frame must match a
		// mesh built with those coordinates, and clearing it the original.
		const auto sheetWith = [](bool mirrored) {
			ModelMesh sheet;
			sheet.geometryAvailable = true;
			sheet.format = ModelMeshFormat::Quake3Md3;
			sheet.sourcePath = QStringLiteral("tests/uv-sheet.md3");
			sheet.frames.resize(1);
			sheet.frames[0].name = QStringLiteral("sheet");
			ModelSurface surface;
			surface.name = QStringLiteral("sheet");
			// Two quads with their own corners, as a level mesh's polygons have.
			surface.triangles = {{0, 1, 2}, {1, 3, 2}, {4, 5, 6}, {5, 7, 6}};
			const float u0 = mirrored ? 1.0f : 0.0f, u1 = mirrored ? 0.0f : 1.0f;
			surface.texCoords = {{u0, 0}, {u1, 0}, {u0, 1}, {u1, 1}, {0, 0}, {1, 0}, {0, 1}, {1, 1}};
			ModelFrameGeometry frame;
			frame.positions = {{-40, -40, 0}, {-4, -40, 0}, {-40, 40, 0}, {-4, 40, 0}, {4, -40, 0}, {40, -40, 0}, {4, 40, 0}, {40, 40, 0}};
			frame.normals = QVector<ModelVec3>(8, ModelVec3 {0, 0, 1});
			surface.frames.append(frame);
			sheet.surfaces.append(surface);
			updateEditableModelMetadata(&sheet);
			return sheet;
		};
		QImage twoTone(2, 1, QImage::Format_RGB32);
		twoTone.setPixelColor(0, 0, Qt::red);
		twoTone.setPixelColor(1, 0, Qt::blue);
		const auto frameOf = [&](ModelViewport& view) {
			QImage frame(view.size(), QImage::Format_RGB32);
			view.render(&frame);
			return frame;
		};
		const auto prepare = [&](ModelViewport& view, const ModelMesh& mesh) {
			view.resize(256, 256);
			view.setShowGrid(false);
			view.setShowAxes(false);
			view.setBackfaceCulling(false);
			view.setRenderMode(ModelViewportRenderMode::Textured);
			view.setMesh(mesh);
			view.setSkin(twoTone);
			view.setOrbit(0, 90);
			return tests::settleModelViewport(view);
		};
		ModelViewport live, built;
		ok &= expect(prepare(live, sheetWith(false)) && prepare(built, sheetWith(true)), "texture coordinate preview fixtures render");
		const QImage original = frameOf(live);
		live.setTexCoordPreview({0, 1}, [](const ModelVec3& position) { return QPointF(1.0 - (position.x + 40.0) / 36.0, (position.y + 40.0) / 80.0); });
		ok &= expect(live.texCoordPreviewActive() && tests::settleModelViewport(live), "texture coordinate preview renders");
		const QImage previewed = frameOf(live);
		ok &= expect(previewed != original && previewed == frameOf(built), "a texture coordinate preview draws exactly as a mesh with those coordinates");
		live.clearTexCoordPreview();
		ok &= expect(!live.texCoordPreviewActive() && tests::settleModelViewport(live) && frameOf(live) == original,
					 "clearing the texture coordinate preview draws the mesh's own coordinates again");
		live.setTexCoordPreview({2, 3}, [](const ModelVec3& position) { return QPointF((position.x - 4.0) / 36.0, (position.y + 40.0) / 80.0); });
		live.setMesh(sheetWith(false));
		ok &= expect(!live.texCoordPreviewActive(), "a new mesh drops the texture coordinate preview");
	}
	ok &= hoverWithoutRendering();
	ok &= mirrorsRenderOnlyOnChange();
	ok &= vertexProjectionReuse();
	ok &= skinPaintStrokeEnds();
	ok &= gameSkinsKeepTheFrame();
	ok &= hiddenPlaybackPauses();
	ok &= cancelledTransformKeepsTheFrame();
	ok &= materialRequestsEveryEmbeddedSkin();
	ok &= skinAlphaScan();
	std::cout << "Viewport device pixel ratio: " << viewport.devicePixelRatioF() << '\n';
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
