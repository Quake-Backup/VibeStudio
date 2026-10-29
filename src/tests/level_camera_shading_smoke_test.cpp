// The Levels camera's shading, fog and face grid choices
// (core/level_camera_render.h): their defaults, the fog range clamps and the
// way either end makes room, the fog ramp the surface shader repeats, the face
// grid following Show Grid and the grid size, the brush-face triangle marks of
// a real map preview, the saved fog range text, and the translated
// announcements.

#include "core/level_camera_render.h"
#include "core/level_map.h"
#include "core/map_preview_mesh.h"

#include <QCoreApplication>
#include <QStringList>

#include <array>
#include <cmath>
#include <iostream>
#include <utility>

using namespace vibestudio;

namespace {

bool ok = true;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << label << ": " << detail.toUtf8().constData() << std::endl;
	}
	return value;
}

QString describe(const LevelCameraShadingState& state)
{
	return QStringLiteral("shade=%1 fog=%2 range=%3..%4 grid=%5")
		.arg(int(state.shadeFaces))
		.arg(int(state.fog))
		.arg(state.fogStart)
		.arg(state.fogEnd)
		.arg(int(state.faceGrid));
}

bool near(double a, double b)
{
	return std::abs(a - b) < 1.0e-9;
}

// An axis-aligned box brush, as the camera render tests write one.
QString box(std::array<double, 3> lo, std::array<double, 3> hi)
{
	QStringList lines {QStringLiteral("{")};
	const auto point = [](const std::array<double, 3>& v) {
		return QStringLiteral("( %1 %2 %3 )").arg(v[0], 0, 'g', 10).arg(v[1], 0, 'g', 10).arg(v[2], 0, 'g', 10);
	};
	for (int axis = 0; axis < 3; ++axis) {
		for (const bool positive : {false, true}) {
			std::array<double, 3> p = lo;
			p[size_t(axis)] = positive ? hi[size_t(axis)] : lo[size_t(axis)];
			auto a = p;
			auto b = p;
			int first = (axis + 1) % 3;
			int second = (axis + 2) % 3;
			if (positive) {
				std::swap(first, second);
			}
			a[size_t(first)] += 1;
			b[size_t(second)] += 1;
			lines << QStringLiteral("%1 %2 %3 base/wall 0 0 0 0.5 0.5 0 0 0").arg(point(p), point(a), point(b));
		}
	}
	lines << QStringLiteral("}");
	return lines.join(QLatin1Char('\n'));
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);

	// Shading on, as the camera has always drawn; fog and the face grid off.
	const LevelCameraShadingState defaults;
	expect(defaults.shadeFaces && !defaults.fog && !defaults.faceGrid && defaults.fogStart == kLevelCameraFogDefaultStart
			&& defaults.fogEnd == kLevelCameraFogDefaultEnd && defaults.fogEnd - defaults.fogStart >= kLevelCameraFogMinimumGap,
		"defaults", describe(defaults));
	expect(!(defaults == LevelCameraShadingState {false}), "states compare by value");

	// The fog range: clamped to the limits, and the end moves out for a start
	// that comes too close, or the start comes in for an edited end.
	LevelCameraShadingState range = levelCameraWithFogRange(defaults, 1000, 3000);
	expect(range.fogStart == 1000 && range.fogEnd == 3000 && range.shadeFaces == defaults.shadeFaces, "a plain range", describe(range));
	range = levelCameraWithFogRange(defaults, 5000, 4096);
	expect(range.fogStart == 5000 && range.fogEnd == 5000 + kLevelCameraFogMinimumGap, "a start past the end pushes the end out", describe(range));
	range = levelCameraWithFogRange(defaults, 512, 100, true);
	expect(range.fogEnd == 100 && range.fogStart == 100 - kLevelCameraFogMinimumGap, "an end before the start pulls the start in", describe(range));
	range = levelCameraWithFogRange(defaults, -50, 2, true);
	expect(range.fogStart == 0 && range.fogEnd == kLevelCameraFogMinimumGap, "distances never go below zero", describe(range));
	range = levelCameraWithFogRange(defaults, 1 << 30, 1 << 30);
	expect(range.fogEnd == kLevelCameraFogMaximumDistance && range.fogStart == kLevelCameraFogMaximumDistance - kLevelCameraFogMinimumGap,
		"distances stop at the largest world", describe(range));

	// The fog ramp: nothing with fog off or before the start, linear up to
	// the strength at the end, never more beyond it.
	LevelCameraShadingState fog = levelCameraWithFogRange(defaults, 1000, 3000);
	expect(near(levelCameraFogAmount(fog, 5000), 0.0), "no fog while it is off");
	fog.fog = true;
	expect(near(levelCameraFogAmount(fog, 0), 0.0) && near(levelCameraFogAmount(fog, 1000), 0.0), "no fog before the start");
	expect(near(levelCameraFogAmount(fog, 2000), kLevelCameraFogStrength * 0.5), "halfway, half the strength",
		QString::number(levelCameraFogAmount(fog, 2000)));
	expect(near(levelCameraFogAmount(fog, 3000), kLevelCameraFogStrength) && near(levelCameraFogAmount(fog, 1.0e9), kLevelCameraFogStrength),
		"the strength at and beyond the end");
	expect(kLevelCameraFogStrength > 0.0 && kLevelCameraFogStrength < 1.0, "distant faces keep some of their colour");
	expect(near(levelCameraFogAmount(fog, std::nan("")), 0.0), "a distance that is not a number takes no fog");

	// The face grid needs its own choice and Show Grid, and uses the grid size.
	LevelCameraShadingState grid = defaults;
	expect(levelCameraFaceGridSpacing(grid, true, 16) == 0.0, "no face grid while it is off");
	grid.faceGrid = true;
	expect(levelCameraFaceGridSpacing(grid, true, 16) == 16.0 && levelCameraFaceGridSpacing(grid, true, 0.25) == 0.25,
		"the face grid uses the grid size");
	expect(levelCameraFaceGridSpacing(grid, false, 16) == 0.0, "Show Grid off hides the face grid");
	expect(levelCameraFaceGridSpacing(grid, true, 0) == 0.0 && levelCameraFaceGridSpacing(grid, true, std::nan("")) == 0.0,
		"an unusable grid size draws none");

	// The saved fog range.
	LevelCameraShadingState saved = levelCameraWithFogRange(defaults, 768, 9000);
	expect(levelCameraFogRangeText(saved) == QByteArrayLiteral("768,9000"), "the saved text", QString::fromLatin1(levelCameraFogRangeText(saved)));
	LevelCameraShadingState read = defaults;
	expect(levelCameraFogRangeFromText(" 768 , 9000 ", &read) && read.fogStart == 768 && read.fogEnd == 9000, "the text reads back", describe(read));
	read = defaults;
	expect(levelCameraFogRangeFromText("9000,10", &read) && read.fogStart == 9000 && read.fogEnd == 9000 + kLevelCameraFogMinimumGap,
		"reading clamps", describe(read));
	for (const char* bad : {"", "512", "a,b", "1,2,3", "1.5,2000"}) {
		LevelCameraShadingState untouched = defaults;
		expect(!levelCameraFogRangeFromText(QByteArray(bad), &untouched) && untouched == defaults, "unreadable text is refused", QString::fromLatin1(bad));
	}

	// A real preview: two brushes (twelve quads, 24 triangles) and a point
	// entity; only brush-face triangles are marked.
	const QString source = QStringLiteral("{\n\"classname\" \"worldspawn\"\n%1\n%2\n}\n{\n\"classname\" \"info_player_start\"\n\"origin\" \"0 0 64\"\n}\n")
							   .arg(box({0, 0, 0}, {64, 64, 64}), box({128, 0, 0}, {192, 64, 32}));
	LevelMapDocument document;
	QString error;
	LevelMapLoadRequest request;
	request.path = QStringLiteral("shading.map");
	if (expect(loadLevelMapBytes(request, source.toUtf8(), &document, &error), "fixture parses", error)) {
		const LevelMapPreviewMesh preview = buildLevelMapPreviewMesh(document);
		const QBitArray marks = levelCameraFaceGridTriangles(preview);
		int brushTriangles = 0;
		bool agree = marks.size() == preview.owners.size();
		for (qsizetype index = 0; agree && index < preview.owners.size(); ++index) {
			const bool brush = preview.owners.at(index).kind == LevelMapSelectionKind::QuakeBrush && preview.ownerFaces.value(index, -1) >= 0;
			agree = marks.testBit(index) == brush;
			brushTriangles += brush ? 1 : 0;
		}
		expect(agree && brushTriangles == 24 && marks.count(true) == 24, "brush-face triangles are marked, others not",
			QStringLiteral("%1 marked of %2, %3 brush").arg(marks.count(true)).arg(marks.size()).arg(brushTriangles));
	}
	expect(levelCameraFaceGridTriangles(LevelMapPreviewMesh {}).isEmpty(), "an empty preview marks nothing");

	// Announcements.
	LevelCameraShadingState flat = defaults;
	flat.shadeFaces = false;
	QStringList sentences {levelCameraShadingSummary(defaults), levelCameraShadingSummary(flat), levelCameraFogSummary(defaults), levelCameraFogSummary(fog),
		levelCameraFaceGridSummary(defaults, true, 16), levelCameraFaceGridSummary(grid, false, 16), levelCameraFaceGridSummary(grid, true, 0.5)};
	for (const QString& sentence : std::as_const(sentences)) {
		expect(!sentence.isEmpty() && sentence.endsWith(QLatin1Char('.')), "an announcement is a sentence", sentence);
	}
	sentences.removeDuplicates();
	expect(sentences.size() == 7, "each announcement differs");
	expect(levelCameraFogSummary(fog).contains(QStringLiteral("1000")) && levelCameraFogSummary(fog).contains(QStringLiteral("3000")),
		"the fog announcement names its distances", levelCameraFogSummary(fog));
	expect(levelCameraFaceGridSummary(grid, true, 0.5).contains(QStringLiteral("0.5")), "the face grid announcement names the spacing",
		levelCameraFaceGridSummary(grid, true, 0.5));

	if (ok) {
		std::cout << "Level camera shading: shading, fog range and ramp, face grid spacing and marks, saved text and announcements passed." << std::endl;
	}
	return ok ? 0 : 1;
}
