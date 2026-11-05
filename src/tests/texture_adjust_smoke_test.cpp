#include "core/texture_document.h"
#include "core/texture_formats.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;

namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << "FAIL: " << message << '\n'; }
	return value;
}

QJsonArray recipe(const char* json) { return QJsonDocument::fromJson(QByteArray(json)).array(); }

TextureDocument documentOf(const QImage& image)
{
	TextureDocument document; document.reset(image); return document;
}

QImage solid(QSize size, QRgb color) { QImage image(size, QImage::Format_ARGB32); image.fill(color); return image; }
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true; QString error;
	const IdTechPalette palette = generatedIdTechPalette(QStringLiteral("quake"));

	// Colour adjustments: each step on a known pixel, alpha and hidden RGB kept.
	{
		QImage image = solid({4, 2}, qRgba(100, 150, 200, 255));
		image.setPixel(3, 1, qRgba(10, 20, 30, 0));
		auto document = documentOf(image);
		TextureColorAdjustment invert; invert.invert = true;
		ok &= expect(document.adjustColors(invert, &error) && document.activeLayer()->pixels.pixel(0, 0) == qRgba(155, 105, 55, 255), "invert flips each channel");
		ok &= expect(document.activeLayer()->pixels.pixel(3, 1) == qRgba(10, 20, 30, 0), "fully transparent texels keep their hidden colour");
		ok &= expect(document.undoLabel() == QStringLiteral("Adjust colors") && document.undo() && document.activeLayer()->pixels.pixel(0, 0) == qRgba(100, 150, 200, 255), "adjustment is one labelled undo step");
		TextureColorAdjustment bright; bright.brightness = 60;
		ok &= expect(document.adjustColors(bright) && document.activeLayer()->pixels.pixel(0, 0) == qRgba(160, 210, 255, 255), "brightness adds and clamps");
		document.undo();
		TextureColorAdjustment flat; flat.contrast = -100;
		ok &= expect(document.adjustColors(flat) && document.activeLayer()->pixels.pixel(0, 0) == qRgba(128, 128, 128, 255), "contrast -100 collapses to mid-grey");
		document.undo();
		TextureColorAdjustment grey; grey.grayscale = true;
		const int luma = (100 * 299 + 150 * 587 + 200 * 114 + 500) / 1000;
		ok &= expect(document.adjustColors(grey) && document.activeLayer()->pixels.pixel(0, 0) == qRgba(luma, luma, luma, 255), "grayscale uses Rec. 601 luma");
		document.undo();
		TextureColorAdjustment poster; poster.posterize = 2;
		ok &= expect(document.adjustColors(poster) && document.activeLayer()->pixels.pixel(0, 0) == qRgba(0, 255, 255, 255), "posterize to two levels snaps channels");
		document.undo();
		TextureColorAdjustment hue; hue.hue = 180;
		ok &= expect(document.adjustColors(hue) && qBlue(document.activeLayer()->pixels.pixel(0, 0)) < qRed(document.activeLayer()->pixels.pixel(0, 0)), "half-turn hue shift turns blue towards orange");
		document.undo();
		TextureColorAdjustment gamma; gamma.gamma = 200;
		ok &= expect(document.adjustColors(gamma) && qRed(document.activeLayer()->pixels.pixel(0, 0)) > 100, "gamma above 100% brightens midtones");
		document.undo();
		ok &= expect(document.setSelection({0, 0, 1, 1}) && document.adjustColors(invert) && document.activeLayer()->pixels.pixel(1, 0) == qRgba(100, 150, 200, 255) &&
			document.activeLayer()->pixels.pixel(0, 0) == qRgba(155, 105, 55, 255), "the selection clips adjustments");
		const auto revision = document.revision();
		ok &= expect(document.adjustColors({}) && document.revision() == revision, "an identity adjustment changes nothing");
		TextureColorAdjustment invalid; invalid.contrast = 101;
		ok &= expect(!document.adjustColors(invalid, &error) && document.revision() == revision, "out-of-range adjustments fail without editing");
	}

	// Filters.
	{
		QImage image = solid({9, 9}, qRgba(0, 0, 0, 255)); image.setPixel(4, 4, qRgba(255, 255, 255, 255));
		auto document = documentOf(image);
		TextureFilterSettings blur; blur.kind = TextureFilterKind::Blur; blur.radius = 1;
		ok &= expect(document.applyFilter(blur, &error), "box blur applies");
		const QImage& blurred = document.activeLayer()->pixels;
		ok &= expect(qRed(blurred.pixel(4, 4)) == 28 && qRed(blurred.pixel(3, 3)) == 28 && qRed(blurred.pixel(2, 2)) == 0, "a 3 × 3 box spreads one bright texel over nine");
		document.undo();
		QImage edge = solid({8, 4}, qRgba(0, 0, 0, 255));
		for (int y = 0; y < 4; ++y) { edge.setPixel(7, y, qRgba(255, 255, 255, 255)); }
		auto clamped = documentOf(edge), wrapped = documentOf(edge);
		TextureFilterSettings wrapBlur = blur; wrapBlur.wrap = true;
		ok &= expect(clamped.applyFilter(blur) && wrapped.applyFilter(wrapBlur), "edge blurs apply");
		ok &= expect(qRed(clamped.activeLayer()->pixels.pixel(0, 1)) == 0 && qRed(wrapped.activeLayer()->pixels.pixel(0, 1)) > 0, "wrapped filtering reads across the seam; clamped does not");
		QImage soft = solid({8, 8}, qRgba(100, 100, 100, 255)); soft.setPixel(4, 4, qRgba(160, 160, 160, 255));
		auto sharp = documentOf(soft);
		TextureFilterSettings sharpen; sharpen.kind = TextureFilterKind::Sharpen; sharpen.amount = 100;
		ok &= expect(sharp.applyFilter(sharpen) && qRed(sharp.activeLayer()->pixels.pixel(4, 4)) > 160 && qRed(sharp.activeLayer()->pixels.pixel(3, 4)) < 100, "sharpen increases local contrast");
		auto noisyA = documentOf(solid({16, 16}, qRgba(128, 128, 128, 255))), noisyB = noisyA, noisyC = noisyA;
		TextureFilterSettings noise; noise.kind = TextureFilterKind::Noise; noise.amount = 30; noise.seed = 7;
		ok &= expect(noisyA.applyFilter(noise) && noisyB.applyFilter(noise) && noisyA.activeLayer()->pixels == noisyB.activeLayer()->pixels, "noise is deterministic for a seed");
		noise.seed = 8;
		ok &= expect(noisyC.applyFilter(noise) && noisyC.activeLayer()->pixels != noisyA.activeLayer()->pixels, "another seed draws other noise");
		bool mono = true;
		for (int x = 0; x < 16; ++x) { const QRgb p = noisyA.activeLayer()->pixels.pixel(x, 3); mono &= qRed(p) == qGreen(p) && qGreen(p) == qBlue(p) && std::abs(qRed(p) - 128) <= 30; }
		ok &= expect(mono, "monochrome noise stays grey and within its range");
		auto edges = documentOf(edge);
		TextureFilterSettings edgeFilter; edgeFilter.kind = TextureFilterKind::Edges; edgeFilter.amount = 100;
		ok &= expect(edges.applyFilter(edgeFilter) && qRed(edges.activeLayer()->pixels.pixel(6, 1)) > 0 && qRed(edges.activeLayer()->pixels.pixel(2, 1)) == 0, "find edges marks only the boundary");
		auto relief = documentOf(edge);
		TextureFilterSettings emboss; emboss.kind = TextureFilterKind::Emboss;
		ok &= expect(relief.applyFilter(emboss) && relief.activeLayer()->pixels != edge, "emboss shades relief");
		TextureFilterSettings badFilter; badFilter.radius = 17;
		ok &= expect(!relief.applyFilter(badFilter, &error), "out-of-range filter settings fail");
	}

	// Gradients.
	{
		auto document = documentOf(solid({11, 3}, qRgba(0, 0, 0, 255)));
		TextureGradient linear; linear.from = {0, 1}; linear.to = {10, 1}; linear.first = qRgba(0, 0, 0, 255); linear.second = qRgba(200, 100, 0, 255);
		ok &= expect(document.drawGradient(linear, &error), "linear gradient draws");
		const QImage& pixels = document.activeLayer()->pixels;
		ok &= expect(pixels.pixel(0, 0) == qRgba(0, 0, 0, 255) && pixels.pixel(10, 2) == qRgba(200, 100, 0, 255) && pixels.pixel(5, 1) == qRgba(100, 50, 0, 255), "linear gradient interpolates between pixel centres");
		document.undo();
		TextureGradient reflected = linear; reflected.shape = TextureGradientShape::Reflected; reflected.from = {5, 1};
		ok &= expect(document.drawGradient(reflected) && document.activeLayer()->pixels.pixel(4, 1) == document.activeLayer()->pixels.pixel(6, 1), "reflected gradients mirror about the start");
		document.undo();
		TextureGradient radial = linear; radial.shape = TextureGradientShape::Radial; radial.from = {5, 1}; radial.to = {10, 1};
		ok &= expect(document.drawGradient(radial) && document.activeLayer()->pixels.pixel(5, 1) == qRgba(0, 0, 0, 255) && document.activeLayer()->pixels.pixel(0, 1) == qRgba(200, 100, 0, 255), "radial gradients grow with distance");
		document.undo();
		TextureGradient dithered = linear; dithered.dither = true; dithered.second = qRgba(3, 0, 0, 255);
		ok &= expect(document.drawGradient(dithered), "dithered gradient draws");
		QSet<int> reds; for (int x = 0; x < 11; ++x) { reds.insert(qRed(document.activeLayer()->pixels.pixel(x, 1))); }
		ok &= expect(reds.size() >= 3, "ordered dithering spreads a shallow ramp over its levels");
		document.undo();
		TextureGradient blend = linear; blend.mode = TexturePaintMode::SourceOver; blend.second = qRgba(255, 0, 0, 0);
		ok &= expect(document.drawGradient(blend) && document.activeLayer()->pixels.pixel(10, 1) == qRgba(0, 0, 0, 255), "a blended gradient keeps pixels where it is transparent");
		TextureGradient outside = linear; outside.to = {11, 1};
		ok &= expect(!document.drawGradient(outside, &error), "gradient endpoints must lie on the canvas");
	}

	// Replace colour and seamless blending.
	{
		QImage image = solid({4, 4}, qRgba(10, 10, 10, 255)); image.setPixel(1, 1, qRgba(12, 9, 10, 128)); image.setPixel(2, 2, qRgba(200, 0, 0, 255));
		auto document = documentOf(image);
		ok &= expect(document.replaceColor(QColor(10, 10, 10), QColor(0, 0, 255), 0, true, &error), "exact colour replacement");
		ok &= expect(document.activeLayer()->pixels.pixel(0, 0) == qRgba(0, 0, 255, 255) && document.activeLayer()->pixels.pixel(1, 1) == qRgba(12, 9, 10, 128) && document.activeLayer()->pixels.pixel(2, 2) == qRgba(200, 0, 0, 255), "only exact matches change at tolerance 0");
		document.undo();
		ok &= expect(document.replaceColor(QColor(10, 10, 10, 128), QColor(0, 255, 0), 127, true) && document.activeLayer()->pixels.pixel(1, 1) == qRgba(0, 255, 0, 128), "tolerance matches near colours and keeps their alpha");
		QImage seam(16, 16, QImage::Format_ARGB32);
		for (int y = 0; y < 16; ++y) { for (int x = 0; x < 16; ++x) { seam.setPixel(x, y, qRgba(x * 16, y * 16, 0, 255)); } }
		auto tile = documentOf(seam);
		const int before = std::abs(qRed(seam.pixel(15, 3)) - qRed(seam.pixel(0, 3)));
		ok &= expect(tile.makeSeamless(4, &error), "make seamless applies");
		const QImage& tiled = tile.activeLayer()->pixels;
		ok &= expect(std::abs(qRed(tiled.pixel(15, 3)) - qRed(tiled.pixel(0, 3))) < before / 4 && std::abs(qGreen(tiled.pixel(4, 15)) - qGreen(tiled.pixel(4, 0))) < 240 / 4, "the wrap edges nearly meet after blending");
		ok &= expect(tiled.pixel(8, 4) == seam.pixel(8, 4), "texels outside the bands are unchanged");
		ok &= expect(!tile.makeSeamless(9, &error) && !tile.makeSeamless(0, &error), "the band must fit half the shorter side");
	}

	// Clone, mirror and pressure strokes.
	{
		QImage image = solid({8, 8}, qRgba(0, 0, 0, 255)); image.setPixel(1, 1, qRgba(255, 0, 0, 255));
		auto document = documentOf(image);
		TextureBrush clone; clone.mode = TexturePaintMode::Clone; clone.cloneOffset = {-4, -4};
		ok &= expect(document.paintStroke({QPoint(5, 5)}, Qt::transparent, clone, &error) && document.activeLayer()->pixels.pixel(5, 5) == qRgba(255, 0, 0, 255), "clone copies the texel at the source offset");
		ok &= expect(document.undoLabel() == QStringLiteral("Clone pixels"), "clone strokes have their own undo label");
		TextureBrush outsideClone = clone; outsideClone.cloneOffset = {-6, 0};
		ok &= expect(document.paintStroke({QPoint(2, 2)}, Qt::transparent, outsideClone) && document.activeLayer()->pixels.pixel(2, 2) == qRgba(0, 0, 0, 255), "unwrapped clone ignores samples outside the canvas");
		TextureBrush wrappedClone = outsideClone; wrappedClone.wrap = true;
		ok &= expect(document.paintStroke({QPoint(7, 1)}, Qt::transparent, wrappedClone) && document.activeLayer()->pixels.pixel(7, 1) == qRgba(255, 0, 0, 255), "wrapped clone samples across the edge");
		auto mirrored = documentOf(solid({8, 6}, qRgba(0, 0, 0, 255)));
		TextureBrush both; both.mirrorX = true; both.mirrorY = true;
		ok &= expect(mirrored.paintStroke({QPoint(1, 1)}, Qt::white, both), "mirrored stroke paints");
		const QImage& m = mirrored.activeLayer()->pixels;
		ok &= expect(m.pixel(1, 1) == qRgb(255, 255, 255) && m.pixel(6, 1) == qRgb(255, 255, 255) && m.pixel(1, 4) == qRgb(255, 255, 255) && m.pixel(6, 4) == qRgb(255, 255, 255) && m.pixel(3, 3) == qRgb(0, 0, 0), "mirroring repeats the stroke in each quadrant");
		auto shapes = documentOf(solid({8, 6}, qRgba(0, 0, 0, 255)));
		TextureBrush horizontal; horizontal.mirrorX = true;
		ok &= expect(shapes.drawShape(TextureShape::Line, {0, 0}, {1, 0}, Qt::white, horizontal, false) && shapes.activeLayer()->pixels.pixel(7, 0) == qRgb(255, 255, 255) && shapes.activeLayer()->pixels.pixel(6, 0) == qRgb(255, 255, 255), "mirroring applies to shapes");
		auto pressure = documentOf(solid({16, 5}, qRgba(0, 0, 0, 255)));
		ok &= expect(pressure.paintStroke({QPoint(2, 2), QPoint(6, 2), QPoint(12, 2)}, Qt::white, TextureBrush{5}, {1, 1, 5}, &error), "a stroke with per-point widths paints");
		ok &= expect(pressure.activeLayer()->pixels.pixel(2, 0) == qRgb(0, 0, 0) && pressure.activeLayer()->pixels.pixel(12, 0) == qRgb(255, 255, 255), "widths vary along the stroke");
		ok &= expect(!pressure.paintStroke({QPoint(2, 2)}, Qt::white, TextureBrush{5}, {0}, &error), "per-point widths stay within 1–128");
		ok &= expect(pressure.beginStroke({1, 1}, Qt::white, TextureBrush{3}) && pressure.setStrokeWidth(1) && pressure.continueStroke({3, 1}) && pressure.endStroke() && !pressure.setStrokeWidth(2), "interactive strokes accept a width change only while active");
	}

	// Recipes reach every new operation and stay atomic.
	{
		TextureDocument document; document.create({16, 16}, QColor(40, 80, 120));
		const auto operations = recipe(R"([
			{"op": "adjust", "brightness": 10, "contrast": 5, "hue": 20, "saturation": -10, "lightness": 5, "gamma": 110, "posterize": 0, "invert": false, "grayscale": false},
			{"op": "filter", "kind": "noise", "amount": 4, "seed": 3, "monochrome": false, "wrap": true},
			{"op": "filter", "kind": "blur", "radius": 2, "wrap": true},
			{"op": "gradient", "x": 0, "y": 0, "x2": 15, "y2": 15, "color": "#ff000000", "color2": "#80ffffff", "shape": "radial", "mode": "source-over", "dither": true},
			{"op": "replace-color", "from": "#ff000000", "to": "#ff102030", "tolerance": 2, "keepAlpha": true},
			{"op": "seamless", "width": 4},
			{"op": "stroke", "points": [[1, 1, 1], [8, 8, 4]], "color": "white", "width": 4, "mirror": "both"},
			{"op": "clone", "points": [[12, 12]], "x": -4, "y": -4, "width": 3, "brush": "round"}
		])");
		error.clear();
		ok &= expect(applyTextureOperations(&document, operations, palette, &error), "a recipe with every new operation applies");
		if (!error.isEmpty()) { std::cerr << error.toStdString() << '\n'; }
		const auto before = document.activeLayer()->pixels; const auto revision = document.revision();
		for (const char* bad : {R"([{"op": "adjust", "gamma": 5}])", R"([{"op": "filter", "kind": "smudge"}])", R"([{"op": "gradient", "x": 0, "y": 0, "x2": 3, "y2": 3, "color": "red"}])",
				R"([{"op": "clone", "points": [[1, 1]], "x": 0, "y": 0}])", R"([{"op": "clone", "points": [[1, 1]], "x": 2, "y": 0, "mode": "replace"}])",
				R"([{"op": "stroke", "points": [[1, 1], [2, 2, 3]], "color": "red"}])", R"([{"op": "stroke", "points": [[1, 1]], "color": "red", "mirror": "diagonal"}])",
				R"([{"op": "seamless", "width": 9}])", R"([{"op": "adjust", "invert": "yes"}])"}) {
			ok &= expect(!applyTextureOperations(&document, recipe(bad), palette, &error) && document.revision() == revision && document.activeLayer()->pixels == before, bad);
		}
		ok &= expect(!applyTextureOperations(&document, recipe(R"([{"op": "adjust", "invert": true}, {"op": "seamless", "width": 99}])"), palette, &error) && document.activeLayer()->pixels == before, "a later failure rolls back earlier new operations");
	}

	// Frames, palettes and colour statistics.
	{
		TextureDocument document; document.create({4, 4}, QColor(255, 0, 0));
		document.addLayer(QStringLiteral("Second")); document.paintStroke({QPoint(0, 0)}, Qt::green, TextureBrush{1});
		document.addLayer(QStringLiteral("Hidden")); document.setLayerProperties(QStringLiteral("Hidden"), false, false, 100, TextureBlendMode::Normal);
		document.selectLayer(1); document.setLayerProperties(QStringLiteral("Second"), true, false, 50, TextureBlendMode::Multiply);
		const auto frames = document.layerFrames();
		ok &= expect(frames.size() == 2 && frames[0].pixel(1, 1) == qRgb(255, 0, 0) && std::abs(qAlpha(frames[1].pixel(0, 0)) - 128) <= 2 && qAlpha(frames[1].pixel(1, 1)) == 0, "frames are the visible layers alone, at their opacity, without blending");
		QImage few(3, 1, QImage::Format_ARGB32); few.setPixel(0, 0, qRgb(10, 10, 10)); few.setPixel(1, 0, qRgb(200, 0, 0)); few.setPixel(2, 0, qRgba(0, 0, 255, 0));
		const auto exact = buildTexturePalette({few}, 256);
		ok &= expect(exact.size() == 2 && exact.contains(qRgb(10, 10, 10)) && exact.contains(qRgb(200, 0, 0)), "few colours are kept exactly, ignoring transparent texels");
		QImage ramp(256, 4, QImage::Format_ARGB32);
		for (int y = 0; y < 4; ++y) { for (int x = 0; x < 256; ++x) { ramp.setPixel(x, y, qRgb(x, 255 - x, (x * 7) & 255)); } }
		const auto cut = buildTexturePalette({ramp}, 16);
		ok &= expect(cut.size() <= 16 && cut.size() >= 12 && buildTexturePalette({ramp}, 16) == cut, "median cut is bounded and deterministic");
		ok &= expect(textureDistinctColors(ramp) == 256 && textureDistinctColors(ramp, 100) == 101, "distinct colours are counted up to a cap");
		QImage onPalette(2, 1, QImage::Format_ARGB32); onPalette.setPixel(0, 0, palette.colorAt(3)); onPalette.setPixel(1, 0, qRgb(1, 2, 254));
		const bool missing = !palette.colors.contains(qRgb(1, 2, 254) | 0xff000000u);
		ok &= expect(textureOffPalettePixels(onPalette, palette.colors) == (missing ? 1 : 0), "off-palette pixels are those without an exact palette entry");
	}

	std::cout << (ok ? "texture adjust smoke passed" : "texture adjust smoke failed") << '\n';
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
