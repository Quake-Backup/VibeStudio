// How the Levels camera draws the map (core/level_camera_render.h).
//
// Workflow reference: TB MapViewBase::showMaterials, hideMaterials, hideFaces
// and toggleShowEdges at 4c756be1 (GPL-3.0-or-later) / NRC camwindow.cpp
// CameraModeNext and CameraModePrev at 449778b3 (GPL-2.0-or-later). Original
// Qt implementation; see docs/CREDITS.md.

#include "core/level_camera_render.h"

#include "core/level_editor_controls.h"
#include "core/map_preview_mesh.h"

#include <QCoreApplication>
#include <QList>
#include <QLocale>

#include <algorithm>
#include <cmath>

namespace vibestudio {

LevelCameraFaceMode levelCameraFaceMode(const LevelCameraRenderState& state)
{
	if (state.facesHidden) {
		return LevelCameraFaceMode::Hidden;
	}
	return state.textures ? LevelCameraFaceMode::Textured : LevelCameraFaceMode::Flat;
}

LevelCameraRenderState levelCameraWithFaceMode(LevelCameraRenderState state, LevelCameraFaceMode mode)
{
	switch (mode) {
	case LevelCameraFaceMode::Textured:
		state.facesHidden = false;
		state.textures = true;
		break;
	case LevelCameraFaceMode::Flat:
		state.facesHidden = false;
		state.textures = false;
		break;
	case LevelCameraFaceMode::Hidden:
		state.facesHidden = true;
		break;
	}
	return state;
}

bool levelCameraDrawsEdgeOverlay(const LevelCameraRenderState& state)
{
	return state.edges && !state.facesHidden;
}

LevelCameraDrawMode levelCameraDrawMode(const LevelCameraRenderState& state)
{
	if (state.facesHidden) {
		return LevelCameraDrawMode::Wireframe;
	}
	if (!state.textures) {
		return LevelCameraDrawMode::Flat;
	}
	return state.edges ? LevelCameraDrawMode::TexturedEdges : LevelCameraDrawMode::Textured;
}

LevelCameraRenderState levelCameraWithDrawMode(LevelCameraRenderState state, LevelCameraDrawMode mode)
{
	switch (mode) {
	case LevelCameraDrawMode::Wireframe:
		state.facesHidden = true;
		break;
	case LevelCameraDrawMode::Flat:
		state = {false, false, false};
		break;
	case LevelCameraDrawMode::Textured:
		state = {true, false, false};
		break;
	case LevelCameraDrawMode::TexturedEdges:
		state = {true, false, true};
		break;
	}
	return state;
}

LevelCameraDrawMode levelCameraSteppedDrawMode(LevelCameraDrawMode mode, int steps)
{
	const int count = kLevelCameraDrawModeCount;
	const int index = ((static_cast<int>(mode) + steps % count) % count + count) % count;
	return static_cast<LevelCameraDrawMode>(index);
}

int levelCameraDrawModeNumber(LevelCameraDrawMode mode)
{
	return static_cast<int>(mode) + 1;
}

QString levelCameraDrawModeId(LevelCameraDrawMode mode)
{
	switch (mode) {
	case LevelCameraDrawMode::Wireframe:
		return QStringLiteral("wireframe");
	case LevelCameraDrawMode::Flat:
		return QStringLiteral("flat");
	case LevelCameraDrawMode::Textured:
		return QStringLiteral("textured");
	case LevelCameraDrawMode::TexturedEdges:
		return QStringLiteral("textured-edges");
	}
	return {};
}

bool levelCameraDrawModeFromId(const QString& id, LevelCameraDrawMode* mode)
{
	const QString wanted = id.trimmed().toLower();
	for (int index = 0; index < kLevelCameraDrawModeCount; ++index) {
		const auto candidate = static_cast<LevelCameraDrawMode>(index);
		if (levelCameraDrawModeId(candidate) == wanted) {
			if (mode) {
				*mode = candidate;
			}
			return true;
		}
	}
	return false;
}

QString levelCameraDrawModeName(LevelCameraDrawMode mode)
{
	switch (mode) {
	case LevelCameraDrawMode::Wireframe:
		return QCoreApplication::translate("VibeStudioLevelCameraRender", "Wireframe");
	case LevelCameraDrawMode::Flat:
		return QCoreApplication::translate("VibeStudioLevelCameraRender", "Flat Shaded");
	case LevelCameraDrawMode::Textured:
		return QCoreApplication::translate("VibeStudioLevelCameraRender", "Textured");
	case LevelCameraDrawMode::TexturedEdges:
		return QCoreApplication::translate("VibeStudioLevelCameraRender", "Textured with Edges");
	}
	return {};
}

QString levelCameraRenderSummary(const LevelCameraRenderState& state)
{
	switch (levelCameraFaceMode(state)) {
	case LevelCameraFaceMode::Hidden:
		return QCoreApplication::translate("VibeStudioLevelCameraRender", "Camera: wireframe, faces hidden.");
	case LevelCameraFaceMode::Flat:
		return state.edges ? QCoreApplication::translate("VibeStudioLevelCameraRender", "Camera: flat-shaded faces with edges.")
						   : QCoreApplication::translate("VibeStudioLevelCameraRender", "Camera: flat-shaded faces.");
	case LevelCameraFaceMode::Textured:
		break;
	}
	return state.edges ? QCoreApplication::translate("VibeStudioLevelCameraRender", "Camera: textured faces with edges.")
					   : QCoreApplication::translate("VibeStudioLevelCameraRender", "Camera: textured faces.");
}

QString levelCameraDrawModeAnnouncement(LevelCameraDrawMode mode)
{
	return QCoreApplication::translate("VibeStudioLevelCameraRender", "Camera mode %1 of %2: %3.")
		.arg(levelCameraDrawModeNumber(mode))
		.arg(kLevelCameraDrawModeCount)
		.arg(levelCameraDrawModeName(mode));
}

// Workflow reference: TB MapViewBase::toggleShadeFaces and toggleShowFog and
// the grid its render context draws on brush faces at 4c756be1
// (GPL-3.0-or-later). Original Qt implementation; see docs/CREDITS.md.

LevelCameraShadingState levelCameraWithFogRange(LevelCameraShadingState state, int start, int end, bool keepEnd)
{
	start = std::clamp(start, 0, kLevelCameraFogMaximumDistance - kLevelCameraFogMinimumGap);
	end = std::clamp(end, kLevelCameraFogMinimumGap, kLevelCameraFogMaximumDistance);
	if (end - start < kLevelCameraFogMinimumGap) {
		// Both are within their ranges, so either can make room.
		if (keepEnd) {
			start = end - kLevelCameraFogMinimumGap;
		} else {
			end = start + kLevelCameraFogMinimumGap;
		}
	}
	state.fogStart = start;
	state.fogEnd = end;
	return state;
}

double levelCameraFogAmount(const LevelCameraShadingState& state, double distance)
{
	if (!state.fog || !std::isfinite(distance)) {
		return 0.0;
	}
	const double span = std::max(1.0, double(state.fogEnd) - double(state.fogStart));
	return kLevelCameraFogStrength * std::clamp((distance - double(state.fogStart)) / span, 0.0, 1.0);
}

double levelCameraFaceGridSpacing(const LevelCameraShadingState& state, bool showGrid, double gridUnits)
{
	return state.faceGrid && showGrid && std::isfinite(gridUnits) && gridUnits > 0.0 ? gridUnits : 0.0;
}

QBitArray levelCameraFaceGridTriangles(const LevelMapPreviewMesh& preview)
{
	const qsizetype count = std::min(preview.owners.size(), preview.ownerFaces.size());
	QBitArray triangles(count);
	for (qsizetype index = 0; index < count; ++index) {
		if (preview.owners.at(index).kind == LevelMapSelectionKind::QuakeBrush && preview.ownerFaces.at(index) >= 0) {
			triangles.setBit(index);
		}
	}
	return triangles;
}

QByteArray levelCameraFogRangeText(const LevelCameraShadingState& state)
{
	return QByteArray::number(state.fogStart) + ',' + QByteArray::number(state.fogEnd);
}

bool levelCameraFogRangeFromText(const QByteArray& text, LevelCameraShadingState* state)
{
	const QList<QByteArray> parts = text.trimmed().split(',');
	if (parts.size() != 2) {
		return false;
	}
	bool startOk = false;
	bool endOk = false;
	const int start = parts.at(0).trimmed().toInt(&startOk);
	const int end = parts.at(1).trimmed().toInt(&endOk);
	if (!startOk || !endOk) {
		return false;
	}
	if (state) {
		*state = levelCameraWithFogRange(*state, start, end);
	}
	return true;
}

QString levelCameraShadingSummary(const LevelCameraShadingState& state)
{
	return state.shadeFaces ? QCoreApplication::translate("VibeStudioLevelCameraRender", "Camera: faces shaded by their direction.")
							: QCoreApplication::translate("VibeStudioLevelCameraRender", "Camera: faces unshaded, at full brightness.");
}

QString levelCameraFogSummary(const LevelCameraShadingState& state)
{
	if (!state.fog) {
		return QCoreApplication::translate("VibeStudioLevelCameraRender", "Camera fog off.");
	}
	return QCoreApplication::translate("VibeStudioLevelCameraRender", "Camera fog on: faces fade towards the background from %1 to %2 units away.")
		.arg(state.fogStart)
		.arg(state.fogEnd);
}

QString levelCameraFaceGridSummary(const LevelCameraShadingState& state, bool showGrid, double gridUnits)
{
	if (!state.faceGrid) {
		return QCoreApplication::translate("VibeStudioLevelCameraRender", "Grid on camera faces off.");
	}
	if (!showGrid) {
		return QCoreApplication::translate("VibeStudioLevelCameraRender", "Grid on camera faces on: it shows while Show Grid is on.");
	}
	return QCoreApplication::translate("VibeStudioLevelCameraRender", "Grid on camera faces on: lines every %1 units on brush faces.")
		.arg(levelEditorGridSizeText(gridUnits));
}

// Workflow reference: NRC camwindow.cpp Camera_getFarClipPlane,
// Camera_ToggleFarClip, Camera_CubeIn and Camera_CubeOut at 449778b3
// (GPL-2.0-or-later). Original Qt implementation; see docs/CREDITS.md.

int levelCameraFarClipDistance(int step)
{
	const int clamped = std::clamp(step, kLevelCameraFarClipMinimumStep, kLevelCameraFarClipMaximumStep);
	return static_cast<int>(std::lround(std::pow(2.0, (clamped + 7) / 2.0)));
}

double levelCameraFarClipPlane(const LevelCameraFarClipState& state)
{
	return state.enabled ? double(levelCameraFarClipDistance(state.step)) : 0.0;
}

LevelCameraFarClipState levelCameraWithFarClipStep(LevelCameraFarClipState state, int step)
{
	state.step = std::clamp(step, kLevelCameraFarClipMinimumStep, kLevelCameraFarClipMaximumStep);
	return state;
}

int levelCameraFarClipStepForDistance(double distance)
{
	int best = kLevelCameraFarClipMinimumStep;
	if (!std::isfinite(distance)) {
		return distance > 0.0 ? kLevelCameraFarClipMaximumStep : best;
	}
	for (int step = kLevelCameraFarClipMinimumStep + 1; step <= kLevelCameraFarClipMaximumStep; ++step) {
		if (std::abs(levelCameraFarClipDistance(step) - distance) < std::abs(levelCameraFarClipDistance(best) - distance)) {
			best = step;
		}
	}
	return best;
}

LevelCameraShadingState levelCameraFogWithinFarClip(LevelCameraShadingState shading, const LevelCameraFarClipState& clip)
{
	const int distance = levelCameraFarClipDistance(clip.step);
	if (!shading.fog || !clip.enabled || shading.fogEnd <= distance) {
		return shading;
	}
	// The shortest distance is the minimum gap, so the start always fits.
	return levelCameraWithFogRange(shading, std::min(shading.fogStart, distance - kLevelCameraFogMinimumGap), distance, true);
}

LevelCameraDepthMapping levelCameraPerspectiveDepth(double nearPlane, double farPlane)
{
	LevelCameraDepthMapping mapping;
	mapping.offset = nearPlane;
	if (!std::isfinite(nearPlane) || !std::isfinite(farPlane) || nearPlane <= 0.0 || farPlane <= nearPlane) {
		return mapping;
	}
	// z / w = offset / depth + scale: 1 at the near plane, 0 at the far one.
	const double span = farPlane - nearPlane;
	mapping.scale = -nearPlane / span;
	mapping.offset = nearPlane * farPlane / span;
	return mapping;
}

QString levelCameraFarClipSettingsKey()
{
	return QStringLiteral("levelPreviewFarClip");
}

QByteArray levelCameraFarClipText(const LevelCameraFarClipState& state)
{
	return (state.enabled ? QByteArrayLiteral("on,") : QByteArrayLiteral("off,")) + QByteArray::number(state.step);
}

bool levelCameraFarClipFromText(const QByteArray& text, LevelCameraFarClipState* state)
{
	const QList<QByteArray> parts = text.trimmed().split(',');
	if (parts.size() != 2) {
		return false;
	}
	const QByteArray choice = parts.at(0).trimmed();
	if (choice != QByteArrayLiteral("on") && choice != QByteArrayLiteral("off")) {
		return false;
	}
	bool numeric = false;
	const int step = parts.at(1).trimmed().toInt(&numeric);
	if (!numeric) {
		return false;
	}
	if (state) {
		state->enabled = choice == QByteArrayLiteral("on");
		*state = levelCameraWithFarClipStep(*state, step);
	}
	return true;
}

QString levelCameraFarClipDistanceText(int step)
{
	// Without group separators, as the studio writes numbers people read
	// beside the fields that edit them.
	QLocale locale;
	locale.setNumberOptions(locale.numberOptions() | QLocale::OmitGroupSeparator);
	return QCoreApplication::translate("VibeStudioLevelCameraRender", "%1 units").arg(locale.toString(levelCameraFarClipDistance(step)));
}

QString levelCameraFarClipSummary(const LevelCameraFarClipState& state, const LevelCameraShadingState& shading)
{
	if (!state.enabled) {
		return QCoreApplication::translate("VibeStudioLevelCameraRender", "Camera far clip off: the camera draws everything in front of it.");
	}
	QString summary = QCoreApplication::translate("VibeStudioLevelCameraRender", "Camera far clip on: nothing beyond %1 is drawn (step %2 of %3).")
						  .arg(levelCameraFarClipDistanceText(state.step))
						  .arg(state.step)
						  .arg(kLevelCameraFarClipMaximumStep);
	if (levelCameraFogWithinFarClip(shading, state) != shading) {
		summary += QLatin1Char(' ') + QCoreApplication::translate("VibeStudioLevelCameraRender", "The fog ends there too.");
	}
	return summary;
}

QString levelCameraFarClipStepAnnouncement(const LevelCameraFarClipState& before, const LevelCameraFarClipState& after)
{
	const QString distance = levelCameraFarClipDistanceText(after.step);
	if (after.step == before.step) {
		return after.step <= kLevelCameraFarClipMinimumStep
			? QCoreApplication::translate("VibeStudioLevelCameraRender", "The draw distance is already at its shortest, %1.").arg(distance)
			: QCoreApplication::translate("VibeStudioLevelCameraRender", "The draw distance is already at its longest, %1.").arg(distance);
	}
	if (!after.enabled) {
		return QCoreApplication::translate("VibeStudioLevelCameraRender", "Draw distance %1 (step %2 of %3); it applies once Far Clip in Camera is on.")
			.arg(distance)
			.arg(after.step)
			.arg(kLevelCameraFarClipMaximumStep);
	}
	return QCoreApplication::translate("VibeStudioLevelCameraRender", "Draw distance %1 (step %2 of %3).")
		.arg(distance)
		.arg(after.step)
		.arg(kLevelCameraFarClipMaximumStep);
}

} // namespace vibestudio
