#pragma once

// How the Levels camera draws the map, shared by the View > Camera Display
// commands, the View tab, the status-strip Textures box and the tests.
//
// Three saved choices make up the picture: whether faces show package images
// (Textures in Camera) or flat colours, whether faces are drawn at all (3D
// Wireframe hides them and leaves the edges), and whether brush polygon edges
// are drawn over the faces (Edges in Camera). TrenchBroom's View Filter sets
// the face mode outright (Show Textures, Hide Textures, Hide Faces) and
// toggles the edges; NetRadiant Custom's Next and Previous Camera Mode step
// through Wireframe, Flat Shaded, Textured and Textured with Edges, wrapping
// at either end. Hiding faces keeps the textures and edges choices, so
// turning 3D Wireframe off brings back the look it replaced.
//
// Workflow reference: TB MapViewBase::showMaterials, hideMaterials, hideFaces
// and toggleShowEdges at 4c756be1 (GPL-3.0-or-later) / NRC camwindow.cpp
// CameraModeNext and CameraModePrev at 449778b3 (GPL-2.0-or-later). Original
// Qt implementation; see docs/CREDITS.md.

#include <QBitArray>
#include <QByteArray>
#include <QString>

namespace vibestudio {

struct LevelMapPreviewMesh;

// What the camera does with brush, patch and placed-model faces.
enum class LevelCameraFaceMode {
	// Package images and static shader previews.
	Textured,
	// Flat-shaded colours, no images.
	Flat,
	// No faces: the polygon edges alone, as 3D Wireframe draws them.
	Hidden,
};

// NetRadiant Custom's camera draw modes, in the order its cycle walks them.
enum class LevelCameraDrawMode {
	Wireframe,
	Flat,
	Textured,
	TexturedEdges,
};

inline constexpr int kLevelCameraDrawModeCount = 4;

// The saved camera display choices. The defaults are the camera as it has
// always looked: textured faces, no edge overlay.
struct LevelCameraRenderState {
	bool textures = true;
	bool facesHidden = false;
	bool edges = false;

	friend bool operator==(const LevelCameraRenderState&, const LevelCameraRenderState&) = default;
};

[[nodiscard]] LevelCameraFaceMode levelCameraFaceMode(const LevelCameraRenderState& state);
// The state with `mode` as its face mode. Textured and Flat show the faces
// with or without images; Hidden hides them and keeps the textures choice for
// when they come back. The edges choice is always kept.
[[nodiscard]] LevelCameraRenderState levelCameraWithFaceMode(LevelCameraRenderState state, LevelCameraFaceMode mode);
// Whether the camera outlines the faces it draws: only with Edges in Camera
// on and the faces shown (hidden faces leave the edges as the whole picture).
[[nodiscard]] bool levelCameraDrawsEdgeOverlay(const LevelCameraRenderState& state);

// Where `state` sits in the cycle: hidden faces are Wireframe, flat faces are
// Flat with or without edges, and textured faces are Textured or Textured
// with Edges.
[[nodiscard]] LevelCameraDrawMode levelCameraDrawMode(const LevelCameraRenderState& state);
// The state that draws `mode`. Wireframe only hides the faces, keeping the
// other choices; the rest set the faces, images and edges exactly.
[[nodiscard]] LevelCameraRenderState levelCameraWithDrawMode(LevelCameraRenderState state, LevelCameraDrawMode mode);
// `steps` modes forward (positive) or back (negative) from `mode`, wrapping
// at either end of the cycle.
[[nodiscard]] LevelCameraDrawMode levelCameraSteppedDrawMode(LevelCameraDrawMode mode, int steps);
// Position in the cycle, counted from 1 for display.
[[nodiscard]] int levelCameraDrawModeNumber(LevelCameraDrawMode mode);

// Stable identifiers: "wireframe", "flat", "textured", "textured-edges".
[[nodiscard]] QString levelCameraDrawModeId(LevelCameraDrawMode mode);
[[nodiscard]] bool levelCameraDrawModeFromId(const QString& id, LevelCameraDrawMode* mode);
// Translated names, such as "Textured with Edges".
[[nodiscard]] QString levelCameraDrawModeName(LevelCameraDrawMode mode);
// A translated sentence describing what the camera now draws, such as
// "Camera: flat-shaded faces with edges."
[[nodiscard]] QString levelCameraRenderSummary(const LevelCameraRenderState& state);
// A translated sentence for a step through the cycle, such as
// "Camera mode 3 of 4: Textured."
[[nodiscard]] QString levelCameraDrawModeAnnouncement(LevelCameraDrawMode mode);

// Shading, fog and the face grid: three more saved camera choices, which the
// face mode and the cycle leave alone.
//
// Shade Faces in Camera (on by default, as the camera has always drawn)
// darkens each face by its direction to a light that follows the camera, so
// neighbouring planes read apart; off, every face shows its texture or flat
// colour at full brightness. Fog in Camera (off by default) blends faces
// towards the camera's background as their distance from the eye (in the
// orthographic orbit camera, their depth behind the front of the map) goes
// from the fog start to the fog end, up to kLevelCameraFogStrength, as a depth
// cue that never hides distant geometry outright. Grid on Camera Faces (off by
// default) draws the current grid size on brush faces while Show Grid is on,
// in the plane of the two world axes each face is most nearly square to.
//
// Workflow reference: TB "View Filter > Shade faces" and "Use fog"
// (MapViewBase::toggleShadeFaces and toggleShowFog, the ShadeFaces and
// ShowFog preferences) and the grid TB draws on brush faces while the
// document grid is visible (MapViewBase passes grid.visible() and
// grid.actualSize() to its render context) at 4c756be1 (GPL-3.0-or-later).
// Original Qt implementation; see docs/CREDITS.md.

inline constexpr int kLevelCameraFogDefaultStart = 512;
inline constexpr int kLevelCameraFogDefaultEnd = 4096;
// Distances are whole units from 0 to the largest Quake III world, and the
// end stays at least kLevelCameraFogMinimumGap past the start.
inline constexpr int kLevelCameraFogMaximumDistance = 131072;
inline constexpr int kLevelCameraFogMinimumGap = 16;
// The most fog a face takes, at and beyond the end distance.
inline constexpr double kLevelCameraFogStrength = 0.8;

struct LevelCameraShadingState {
	bool shadeFaces = true;
	bool fog = false;
	int fogStart = kLevelCameraFogDefaultStart;
	int fogEnd = kLevelCameraFogDefaultEnd;
	bool faceGrid = false;

	friend bool operator==(const LevelCameraShadingState&, const LevelCameraShadingState&) = default;
};

// The state with the fog range `start` to `end`, clamped to the distance
// limits. When the two are closer than the minimum gap, the end moves out to
// make room, or with `keepEnd` (the end was the one edited) the start comes in.
[[nodiscard]] LevelCameraShadingState levelCameraWithFogRange(LevelCameraShadingState state, int start, int end, bool keepEnd = false);
// How much of the fog colour a face `distance` units from the eye takes:
// 0 with fog off or before the start, rising linearly to
// kLevelCameraFogStrength at the end. The surface shader computes the same.
[[nodiscard]] double levelCameraFogAmount(const LevelCameraShadingState& state, double distance);
// The spacing the camera draws on brush faces: the grid size while Grid on
// Camera Faces and Show Grid are both on, otherwise 0 (no face grid).
[[nodiscard]] double levelCameraFaceGridSpacing(const LevelCameraShadingState& state, bool showGrid, double gridUnits);
// Which triangles of a map preview carry the face grid: brush faces only,
// not patches, placed models or Doom surfaces. Indexed like the preview's
// owners, as the camera flattens its triangles.
[[nodiscard]] QBitArray levelCameraFaceGridTriangles(const LevelMapPreviewMesh& preview);

// The saved fog range as "start,end", and back. Reading clamps as
// levelCameraWithFogRange does and refuses anything but two whole numbers.
[[nodiscard]] QByteArray levelCameraFogRangeText(const LevelCameraShadingState& state);
[[nodiscard]] bool levelCameraFogRangeFromText(const QByteArray& text, LevelCameraShadingState* state);

// Translated sentences for the status bar, such as
// "Camera: faces shaded by their direction."
[[nodiscard]] QString levelCameraShadingSummary(const LevelCameraShadingState& state);
[[nodiscard]] QString levelCameraFogSummary(const LevelCameraShadingState& state);
[[nodiscard]] QString levelCameraFaceGridSummary(const LevelCameraShadingState& state, bool showGrid, double gridUnits);

// Far Clip in Camera: NetRadiant Custom's cubic clip, a saved draw distance
// beyond which the camera draws nothing, to speed up and declutter dense
// maps. Off by default. The distance comes in 23 steps, each about 1.41
// times the last, from 16 to 32768 units: step s is 2^((s + 7) / 2) units
// rounded to a whole unit, NRC's cubic scale, starting at its step 14 (1448
// units). Shorter and Longer Draw Distance move one step and keep the step
// while the clip is off, as NRC's CubicClipZoomIn and CubicClipZoomOut do.
//
// The clip is a far plane in the camera's view-projection, so both GPU
// backends, picking and the drawn-triangle count agree on it. In the
// perspective camera it is a plane that distance ahead of the eye; in the
// orthographic orbit camera it is that depth behind the front of the map,
// where its fog also starts counting.
//
// With Fog in Camera on as well, the fog ends no farther than the clip
// distance, so faces reach their fullest fog before they are cut off and the
// edge fades rather than ending abruptly; the saved fog range is not changed
// and comes back when the clip is turned off. (TrenchBroom's fog is
// independent of its fixed far plane; NRC's cubic clip cuts faces off with no
// fade. This keeps both as separate choices and joins them only where they
// meet.)
//
// Workflow reference: NRC camwindow.cpp Camera_getFarClipPlane,
// Camera_ToggleFarClip, Camera_CubeIn and Camera_CubeOut and the CubicClipping
// and CubicScale preferences at 449778b3 (GPL-2.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.

inline constexpr int kLevelCameraFarClipMinimumStep = 1;
inline constexpr int kLevelCameraFarClipMaximumStep = 23;
inline constexpr int kLevelCameraFarClipDefaultStep = 14;

struct LevelCameraFarClipState {
	bool enabled = false;
	int step = kLevelCameraFarClipDefaultStep;

	friend bool operator==(const LevelCameraFarClipState&, const LevelCameraFarClipState&) = default;
};

// The draw distance of `step` (clamped to the step range), in whole units.
[[nodiscard]] int levelCameraFarClipDistance(int step);
// The far plane the camera uses: the step's distance while the clip is on,
// otherwise 0 (no far plane).
[[nodiscard]] double levelCameraFarClipPlane(const LevelCameraFarClipState& state);
// The state at `step`, clamped to the step range; the on/off choice is kept.
[[nodiscard]] LevelCameraFarClipState levelCameraWithFarClipStep(LevelCameraFarClipState state, int step);
// The step whose distance is nearest `distance` units (the shorter on a tie).
[[nodiscard]] int levelCameraFarClipStepForDistance(double distance);
// The fog the camera draws with the clip: the fog end pulled in to the clip
// distance (and the start with it, to keep the minimum gap) while both are on
// and the end lies beyond it; otherwise `shading` unchanged.
[[nodiscard]] LevelCameraShadingState levelCameraFogWithinFarClip(LevelCameraShadingState shading, const LevelCameraFarClipState& clip);

// How a perspective camera maps view depth (distance ahead of the eye, which
// is also clip w) to clip z: z = scale * depth + offset, so z / w is 1 at the
// near plane and falls towards 0 with distance. With no far plane (`farPlane`
// 0, not finite or not beyond `nearPlane`) scale is 0 and offset is the near
// plane: an infinite far plane. With one, z / w reaches 0 at it and goes
// negative beyond, where both GPU backends clip.
struct LevelCameraDepthMapping {
	double scale = 0.0;
	double offset = 1.0;
};
[[nodiscard]] LevelCameraDepthMapping levelCameraPerspectiveDepth(double nearPlane, double farPlane);

// The saved choice: the studio settings key it is kept under (with the
// camera's other display choices) and its text, "on,14" or "off,14". Reading
// clamps the step and refuses anything else.
[[nodiscard]] QString levelCameraFarClipSettingsKey();
[[nodiscard]] QByteArray levelCameraFarClipText(const LevelCameraFarClipState& state);
[[nodiscard]] bool levelCameraFarClipFromText(const QByteArray& text, LevelCameraFarClipState* state);

// "1448 units", in the user's locale.
[[nodiscard]] QString levelCameraFarClipDistanceText(int step);
// Translated sentences for the status bar: what the clip now does, with a
// note when it ends the fog early; and a step's announcement, which says
// when the step is already at either end or the clip is off.
[[nodiscard]] QString levelCameraFarClipSummary(const LevelCameraFarClipState& state, const LevelCameraShadingState& shading);
[[nodiscard]] QString levelCameraFarClipStepAnnouncement(const LevelCameraFarClipState& before, const LevelCameraFarClipState& after);

} // namespace vibestudio
