#include "core/model_editor_controls.h"
#include "core/legacy_names.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequence>
#include <QSet>

namespace slopstudio {

namespace {

// --- Commands --------------------------------------------------------------

struct CommandEntry {
	const char* id;
	const char* category;
	const char* title;
	// Default (studio) keys, '|' between them.
	const char* keys;
};

// Every modeller command and its studio keys, which follow Blender's. The ids
// are the editor actions' object names. Titles are translated where read.
constexpr CommandEntry kCommands[] = {
	// Files and history.
	{"openMesh", "file", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Open or Import"), "Ctrl+O"},
	{"saveMesh", "file", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Save"), "Ctrl+S"},
	{"undoMesh", "edit", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Undo"), "Ctrl+Z"},
	{"redoMesh", "edit", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Redo"), "Ctrl+Shift+Z|Ctrl+Y"},
	// Modes.
	{"meshToggleEditMode", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Toggle Object/Edit Mode"), "Tab"},
	{"meshModeVertex", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Vertex Select Mode"), "1"},
	{"meshModeEdge", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Edge Select Mode"), "2"},
	{"meshModeFace", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Face Select Mode"), "3"},
	{"meshModeBorder", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Border Select Mode"), ""},
	{"meshModeElement", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Element Select Mode"), ""},
	{"meshModeObject", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Object (Surface) Select Mode"), ""},
	{"meshModeTag", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Tag and Joint Mode"), ""},
	// Selection.
	{"meshSelectAll", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Select All"), "A"},
	{"meshSelectNone", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Select None"), "Alt+A"},
	{"meshSelectInvert", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Invert Selection"), "Ctrl+I"},
	{"meshSelectBox", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Box Select"), "B"},
	{"meshSelectCircle", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Circle Select"), "C"},
	{"meshSelectLinked", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Select Linked"), "Ctrl+L"},
	{"meshSelectLinkedPick", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Select Linked Under Pointer"), "L"},
	{"meshSelectMore", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Grow Selection"), "Ctrl+=|Ctrl+Num++"},
	{"meshSelectLess", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Shrink Selection"), "Ctrl+-|Ctrl+Num+-"},
	// Keyboard routes to Alt+click and Ctrl+Alt+click, as 3ds Max binds them.
	{"meshSelectLoop", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Select Edge Loops of Selection"), "Alt+L"},
	{"meshSelectRing", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Select Edge Rings of Selection"), "Alt+R"},
	{"meshSelectNonManifold", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Select Non-Manifold"), "Shift+Ctrl+Alt+M"},
	{"meshSelectMirror", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Mirror Selection"), "Shift+Ctrl+M"},
	{"meshSimilarMenu", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Select Similar Menu"), "Shift+G"},
	// Visibility.
	{"meshHide", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Hide Selected"), "H"},
	{"meshHideUnselected", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Hide Unselected"), "Shift+H"},
	{"meshReveal", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Reveal Hidden"), "Alt+H"},
	{"meshIsolate", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Isolate Selection"), "Num+/"},
	// Transforms.
	{"meshMove", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Move"), "G"},
	{"meshRotate", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Rotate"), "R"},
	{"meshScale", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Scale"), "S"},
	{"meshToolSelect", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Select Tool"), ""},
	{"meshToolMove", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Move Tool"), ""},
	{"meshToolRotate", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Rotate Tool"), ""},
	{"meshToolScale", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Scale Tool"), ""},
	{"meshAxisX", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Constrain to X"), ""},
	{"meshAxisY", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Constrain to Y"), ""},
	{"meshAxisZ", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Constrain to Z"), ""},
	{"meshAxisFree", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Free or Planar Constraint"), ""},
	{"meshToggleSnap", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Toggle Snapping"), "Shift+Tab"},
	{"meshShrinkFatten", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Shrink/Fatten"), "Alt+S"},
	{"meshFlattenX", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Flatten X"), ""},
	{"meshFlattenY", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Flatten Y"), ""},
	{"meshFlattenZ", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Flatten Z"), ""},
	{"meshSnapSelectionToGrid", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Snap Selection to Grid"), ""},
	{"meshToggleProportional", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Proportional Editing"), "O"},
	{"meshCycleFalloff", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Next Proportional Falloff"), "Shift+O"},
	// Mesh editing.
	{"meshExtrude", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Extrude"), "E"},
	{"meshInset", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Inset Faces"), "I"},
	{"meshExtrudeMenu", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Extrude Menu"), "Alt+E"},
	{"meshExtrudeNormals", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Extrude Faces Along Normals"), ""},
	{"meshExtrudeIndividual", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Extrude Individual Faces"), ""},
	{"meshDuplicate", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Duplicate"), "Shift+D"},
	{"meshLoopCut", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Loop Cut"), "Ctrl+R"},
	{"meshEdgeSlide", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Edge Slide"), ""},
	{"meshMakeFace", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Make Face"), "F"},
	{"meshPolyBuild", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Poly Build"), ""},
	{"meshIntersectCut", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Intersect (Knife)"), ""},
	{"meshIntersectUnion", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Intersect (Union)"), ""},
	{"meshIntersectDifference", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Intersect (Difference)"), ""},
	{"meshIntersectShared", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Intersect (Shared Volume)"), ""},
	{"meshBevelVertices", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Bevel Vertices"), "Ctrl+Shift+B"},
	{"meshBevelEdges", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Bevel Edges"), "Ctrl+B"},
	{"meshKnife", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Knife"), "K"},
	{"meshConnectVertexPath", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Connect Vertex Path"), "J"},
	{"meshConnectEdges", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Connect Edges"), ""},
	{"meshSpin", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Spin"), ""},
	{"meshRip", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Rip"), "V"},
	{"meshRipFill", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Rip and Fill"), "Alt+V"},
	{"meshSpinDuplicate", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Spin Duplicates"), ""},
	{"meshScrew", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Screw"), ""},
	{"meshArray", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Array"), ""},
	{"meshMergeCentre", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Merge at Centre"), ""},
	{"meshMergeCollapse", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Collapse"), ""},
	{"meshMergeTarget", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Merge at Target Under Pointer"), ""},
	{"meshAutoMerge", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Auto Merge Vertices"), ""},
	{"meshPlaceVertex", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Place Vertex"), ""},
	{"meshPivotTarget", "transform", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Pivot at Target Under Pointer"), ""},
	{"meshMergeDistance", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Merge by Distance"), ""},
	{"meshDeleteFaces", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Delete Faces"), ""},
	{"meshDeleteVertices", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Delete Vertices"), ""},
	{"meshDeleteEdges", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Delete Edges"), ""},
	{"meshDissolveEdges", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Dissolve Edges"), ""},
	{"meshLimitedDissolve", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Limited Dissolve"), ""},
	{"meshDissolveVertices", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Dissolve Vertices"), ""},
	{"meshFillHoles", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Fill Boundary Loops"), ""},
	{"meshBridgeChains", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Bridge Edge Chains"), ""},
	{"meshBridgeFaces", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Bridge Faces"), ""},
	{"meshMirrorFacesX", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Mirror Faces Across X"), ""},
	{"meshMirrorFacesY", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Mirror Faces Across Y"), ""},
	{"meshMirrorFacesZ", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Mirror Faces Across Z"), ""},
	{"meshSubdivide", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Subdivide"), ""},
	{"meshSplitEdges", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Split Edges"), ""},
	{"meshPoke", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Poke Faces"), ""},
	{"meshSmooth", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Smooth Vertices"), ""},
	{"meshFlip", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Flip Normals"), ""},
	{"meshRecalculateNormals", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Recalculate Normals"), "Shift+N"},
	{"meshRecalculateOutside", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Recalculate Outside"), ""},
	{"meshRecalculateInside", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Recalculate Inside"), ""},
	{"meshMarkSharp", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Mark Sharp"), ""},
	{"meshSmoothingGroup", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Make Smoothing Group"), ""},
	{"meshSelectIntroducedIntersections", "select", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Select New Intersections"), ""},
	{"meshClearSharp", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Clear Sharp"), ""},
	{"meshShadeSmooth", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Shade Smooth"), ""},
	{"meshAddMenu", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Add Menu"), "Shift+A"},
	{"meshDeleteMenu", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Delete Menu"), "X|Del"},
	{"meshMergeMenu", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Merge Menu"), "M"},
	{"meshSnapMenu", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Snap Menu"), "Shift+S"},
	{"meshVertexMenu", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Vertex Menu"), "Ctrl+V"},
	{"meshEdgeMenu", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Edge Menu"), "Ctrl+E"},
	{"meshFaceMenu", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Face Menu"), "Ctrl+F"},
	{"meshNormalsMenu", "mesh", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Normals Menu"), "Alt+N"},
	// UVs.
	{"meshUvMenu", "uv", QT_TRANSLATE_NOOP("SlopStudioModelControls", "UV Mapping Menu"), "U"},
	{"meshUvDensity", "uv", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Texel Density"), ""},
	{"meshUnwrapConformal", "uv", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Unwrap (Seams Only)"), ""},
	{"meshUvWeld", "uv", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Weld UVs"), ""},
	{"meshUvStitch", "uv", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Stitch UVs"), ""},
	{"meshUvAlign", "uv", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Align UVs"), ""},
	{"meshUvSnapTexels", "uv", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Snap UVs to Texels"), ""},
	{"meshUvPin", "uv", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Pin UVs"), ""},
	{"meshUvUnpin", "uv", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Unpin UVs"), ""},
	{"meshUvQuakeFrontBack", "uv", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Quake Front/Back Skin Layout"), ""},
	{"meshUvCheckHealth", "uv", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Check UV Health"), ""},
	{"meshUvExportLayout", "uv", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Export UV Layout"), ""},
	{"meshUvTransferSkin", "uv", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Transfer Skin to New UVs"), ""},
	{"meshUvEditSkin", "uv", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Edit Skin"), ""},
	{"meshUvApplySkin", "uv", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Apply Skin from Textures"), ""},
	{"meshShowUvEditor", "uv", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Texture Coordinate Editor"), ""},
	{"meshAssignMaterial", "uv", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Assign Material to Selection"), ""},
	// View.
	{"meshViewAll", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Frame All"), "Home"},
	{"meshViewSelected", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Frame Selected"), "Num+.|Num+Del"},
	{"meshViewFront", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Front View"), "Num+1"},
	{"meshViewBack", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Back View"), "Ctrl+Num+1"},
	{"meshViewRight", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Right View"), "Num+3"},
	{"meshViewLeft", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Left View"), "Ctrl+Num+3"},
	{"meshViewTop", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Top View"), "Num+7"},
	{"meshViewBottom", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Bottom View"), "Ctrl+Num+7"},
	{"meshViewOpposite", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Opposite View"), "Num+9"},
	{"meshViewPerspective", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Perspective/Orthographic"), "Num+5"},
	{"meshViewUser", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "User View"), ""},
	{"meshToggleQuadView", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Toggle Four Views"), "Ctrl+Alt+Q"},
	{"meshMaximizeView", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Maximise View"), "Ctrl+Space"},
	// The active pane of four views, in reading order. Page keys have no
	// direction to mirror in right-to-left layouts, and desktops on Linux
	// take Ctrl+Alt with the arrow keys.
	{"meshNextView", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Next View"), "Ctrl+Alt+PgDown"},
	{"meshPreviousView", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Previous View"), "Ctrl+Alt+PgUp"},
	{"meshToggleSidebars", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Toggle Sidebars"), "N"},
	{"meshToggleToolShelf", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Toggle Tool Shelf"), "T"},
	{"meshShadingMenu", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Shading Menu"), "Z"},
	{"meshToggleWireframe", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Toggle Wireframe"), "Shift+Z"},
	{"meshToggleXray", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Toggle X-Ray"), "Alt+Z"},
	{"meshToggleGrid", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Toggle Grid"), ""},
	{"meshToggleEdges", "view", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Toggle Edged Faces"), ""},
	// Animation.
	{"meshPlay", "animation", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Play or Pause"), ""},
	{"meshFrameNext", "animation", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Next Frame"), ""},
	{"meshFramePrevious", "animation", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Previous Frame"), ""},
	{"meshFrameFirst", "animation", QT_TRANSLATE_NOOP("SlopStudioModelControls", "First Frame"), ""},
	{"meshFrameLast", "animation", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Last Frame"), ""},
	{"meshCopyPoseToFrame", "animation", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Copy Pose to Current Frame"), ""},
	{"meshCopyPose", "animation", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Copy Pose"), ""},
	{"meshPastePose", "animation", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Paste Pose"), ""},
	{"meshPastePoseMirrored", "animation", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Paste Pose Mirrored"), ""},
	{"meshMirrorPose", "animation", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Mirror Pose"), ""},
	{"meshToggleOnionSkin", "animation", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Toggle Onion Skin"), ""},
	// Tools and search.
	{"meshOperatorSearch", "tools", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Search Commands"), "F3"},
	{"meshAdjustLast", "tools", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Adjust Last Operation"), "F9"},
	{"meshRepeatLast", "tools", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Repeat Last"), "Shift+R"},
	{"meshTypeIn", "tools", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Numeric Transform"), ""},
	{"meshCursorToOrigin", "tools", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Cursor to World Origin"), "Shift+C"},
};

// --- Profiles --------------------------------------------------------------

void bind(ModelEditorControls& controls, const char* command, std::initializer_list<const char*> keys)
{
	ModelEditorKeyBinding binding;
	binding.commandId = QString::fromLatin1(command);
	for (const char* key : keys) {
		binding.keys << QString::fromLatin1(key);
	}
	for (ModelEditorKeyBinding& existing : controls.keys) {
		if (existing.commandId == binding.commandId) {
			existing = binding;
			return;
		}
	}
	controls.keys.append(binding);
}

// Blender-style navigation shared by the studio and Blender profiles:
// middle-drag orbits, Shift+middle pans, Ctrl+middle zooms, the wheel zooms.
void blenderNavigation(ModelNavigationControls& navigation)
{
	for (CameraViewControls* camera : {&navigation.view3D, &navigation.orthographic}) {
		camera->orbitButton = Qt::MiddleButton;
		camera->orbitModifiers = Qt::NoModifier;
		camera->panButtons = {Qt::MiddleButton};
		camera->panModifiers = Qt::ShiftModifier;
		camera->leftPanModifiers = Qt::NoModifier;
		camera->toggleModifiers = Qt::ShiftModifier;
		camera->wheel = CameraWheel::Zoom;
	}
	navigation.zoomButton = Qt::MiddleButton;
	navigation.zoomModifiers = Qt::ControlModifier;
	navigation.orbitLeavesOrthographic = true;
}

ModelEditorControls studioControls()
{
	ModelEditorControls controls;
	controls.profileId = QStringLiteral("studio");
	blenderNavigation(controls.navigation);
	controls.navigation.emulateMiddleButton = true;
	controls.layout.family = QStringLiteral("studio");
	controls.inheritDefaultKeys = true;
	return controls;
}

ModelEditorControls blenderControls()
{
	ModelEditorControls controls = studioControls();
	controls.profileId = QStringLiteral("blender");
	controls.layout.family = QStringLiteral("blender");
	controls.layout.startInPerspective = true;
	// Blender leaves three-button emulation off until the user asks for it.
	controls.navigation.emulateMiddleButton = false;
	// Blender redoes with Shift+Ctrl+Z only; Ctrl+Y is free.
	bind(controls, "redoMesh", {"Ctrl+Shift+Z"});
	// Blender's Space plays the animation in every editor; arrows step frames
	// only outside the 3D view, which keeps them for view orbit steps here.
	bind(controls, "meshPlay", {"Space"});
	bind(controls, "meshFrameFirst", {"Shift+Ctrl+Left"});
	bind(controls, "meshFrameLast", {"Shift+Ctrl+Right"});
	// Blender's Shift+N winds faces outward; vertex normals stay a menu command.
	bind(controls, "meshRecalculateOutside", {"Shift+N"});
	bind(controls, "meshRecalculateInside", {"Ctrl+Shift+N"});
	bind(controls, "meshRecalculateNormals", {});
	return controls;
}

ModelEditorControls maxControls()
{
	ModelEditorControls controls;
	controls.profileId = QStringLiteral("3ds-max");
	// Middle drag pans, Alt+middle orbits (Arc Rotate), Ctrl+Alt+middle zooms.
	for (CameraViewControls* camera : {&controls.navigation.view3D, &controls.navigation.orthographic}) {
		camera->orbitButton = Qt::MiddleButton;
		camera->orbitModifiers = Qt::AltModifier;
		camera->panButtons = {Qt::MiddleButton};
		camera->panModifiers = Qt::NoModifier;
		camera->leftPanModifiers = Qt::NoModifier;
		camera->toggleModifiers = Qt::ControlModifier;
		camera->wheel = CameraWheel::Zoom;
	}
	controls.navigation.zoomButton = Qt::MiddleButton;
	controls.navigation.zoomModifiers = Qt::ControlModifier | Qt::AltModifier;
	controls.navigation.orbitLeavesOrthographic = true;
	controls.selection.extendModifiers = Qt::ControlModifier;
	controls.selection.extendToggles = true;
	controls.selection.subtractModifiers = Qt::AltModifier;
	// Alt subtracts, so loops come from double-clicks and Alt+L / Alt+R.
	controls.selection.loopModifiers = Qt::NoModifier;
	controls.selection.ringModifiers = Qt::NoModifier;
	controls.selection.pathModifiers = Qt::NoModifier;
	controls.selection.doubleClickSelectsLoop = true;
	controls.selection.lassoButton = Qt::NoButton;
	controls.selection.lassoModifiers = Qt::NoModifier;
	controls.selection.cursorButton = Qt::NoButton;
	controls.selection.cursorModifiers = Qt::NoModifier;
	controls.transform.style = ModelTransformStyle::ToolMode;
	controls.transform.dragSelectionTransforms = true;
	controls.transform.duplicateDragModifiers = Qt::ShiftModifier;
	controls.transform.precisionModifiers = Qt::NoModifier;
	controls.transform.snapModifiers = Qt::NoModifier;
	controls.layout.layout = ModelViewLayout::FourViews;
	controls.layout.panes = {ModelPaneView::Top, ModelPaneView::Front, ModelPaneView::Left, ModelPaneView::Perspective};
	controls.layout.startInPerspective = true;
	controls.layout.family = QStringLiteral("max");
	controls.inheritDefaultKeys = false;
	// Defaults from Autodesk's 3ds Max keyboard shortcut reference.
	bind(controls, "openMesh", {"Ctrl+O"});
	bind(controls, "saveMesh", {"Ctrl+S"});
	bind(controls, "undoMesh", {"Ctrl+Z"});
	bind(controls, "redoMesh", {"Ctrl+Y"});
	bind(controls, "meshToggleEditMode", {"Ctrl+B"});
	bind(controls, "meshModeVertex", {"1"});
	bind(controls, "meshModeEdge", {"2"});
	bind(controls, "meshModeBorder", {"3"});
	bind(controls, "meshModeFace", {"4"});
	bind(controls, "meshModeElement", {"5"});
	bind(controls, "meshModeObject", {"6"});
	bind(controls, "meshSelectAll", {"Ctrl+A"});
	bind(controls, "meshSelectNone", {"Ctrl+D"});
	bind(controls, "meshSelectInvert", {"Ctrl+I"});
	bind(controls, "meshSelectMore", {"Ctrl+PgUp", "Ctrl+Up"});
	bind(controls, "meshSelectLess", {"Ctrl+PgDown", "Ctrl+Down"});
	bind(controls, "meshSelectLoop", {"Alt+L"});
	bind(controls, "meshSelectRing", {"Alt+R"});
	bind(controls, "meshHide", {"Alt+H"});
	bind(controls, "meshHideUnselected", {"Alt+I"});
	bind(controls, "meshReveal", {"Alt+U"});
	bind(controls, "meshIsolate", {"Alt+Q"});
	bind(controls, "meshToolSelect", {"Q"});
	bind(controls, "meshToolMove", {"W"});
	bind(controls, "meshToolRotate", {"E"});
	bind(controls, "meshToolScale", {"R"});
	bind(controls, "meshAxisX", {"F5"});
	bind(controls, "meshAxisY", {"F6"});
	bind(controls, "meshAxisZ", {"F7"});
	bind(controls, "meshAxisFree", {"F8"});
	bind(controls, "meshToggleSnap", {"S"});
	bind(controls, "meshTypeIn", {"F12"});
	bind(controls, "meshDuplicate", {"Ctrl+V"});
	bind(controls, "meshDeleteFaces", {"Del"});
	bind(controls, "meshDissolveVertices", {"Backspace"});
	// Remove an edge with its vertices.
	bind(controls, "meshDissolveEdges", {"Ctrl+Backspace"});
	bind(controls, "meshExtrude", {"Shift+E"});
	bind(controls, "meshBevelVertices", {"Ctrl+Shift+C"});
	bind(controls, "meshConnectEdges", {"Ctrl+Shift+E"});
	bind(controls, "meshMergeCollapse", {"Ctrl+Alt+C"});
	bind(controls, "meshFillHoles", {"Alt+P"});
	bind(controls, "meshSubdivide", {"Ctrl+M"});
	bind(controls, "meshRepeatLast", {";"});
	bind(controls, "meshOperatorSearch", {"X"});
	bind(controls, "meshViewAll", {"Ctrl+Alt+Z", "Shift+Ctrl+Z"});
	bind(controls, "meshViewSelected", {"Z"});
	bind(controls, "meshViewTop", {"T"});
	bind(controls, "meshViewBottom", {"B"});
	bind(controls, "meshViewFront", {"F"});
	bind(controls, "meshViewLeft", {"L"});
	bind(controls, "meshViewPerspective", {"P"});
	bind(controls, "meshViewUser", {"U"});
	bind(controls, "meshMaximizeView", {"Alt+W"});
	bind(controls, "meshNextView", {"Ctrl+Alt+PgDown"});
	bind(controls, "meshPreviousView", {"Ctrl+Alt+PgUp"});
	bind(controls, "meshToggleGrid", {"G"});
	bind(controls, "meshToggleWireframe", {"F3"});
	bind(controls, "meshToggleEdges", {"F4"});
	bind(controls, "meshToggleXray", {"Alt+X"});
	bind(controls, "meshShowUvEditor", {"Ctrl+E"});
	bind(controls, "meshAssignMaterial", {"M"});
	bind(controls, "meshPlay", {"/"});
	bind(controls, "meshFrameNext", {"."});
	bind(controls, "meshFramePrevious", {","});
	bind(controls, "meshFrameFirst", {"Home"});
	bind(controls, "meshFrameLast", {"End"});
	bind(controls, "meshCopyPoseToFrame", {"K"});
	return controls;
}

ModelEditorControls milkShapeControls()
{
	ModelEditorControls controls;
	controls.profileId = QStringLiteral("milkshape-3d");
	// 2D views: Ctrl+left drag pans, Shift+left drag zooms, the wheel zooms;
	// they never rotate. The 3D view: left drag rotates, the same Ctrl and
	// Shift drags pan and zoom, and the wheel runs the other way.
	CameraViewControls& view3D = controls.navigation.view3D;
	view3D.orbitButton = Qt::LeftButton;
	view3D.orbitModifiers = Qt::NoModifier;
	view3D.panButtons = {Qt::MiddleButton};
	view3D.panModifiers = Qt::NoModifier;
	view3D.leftPanModifiers = Qt::ControlModifier;
	view3D.toggleModifiers = Qt::ShiftModifier;
	view3D.wheel = CameraWheel::Zoom;
	CameraViewControls& plan = controls.navigation.orthographic;
	plan.orbitButton = Qt::NoButton;
	plan.orbitModifiers = Qt::NoModifier;
	plan.panButtons = {Qt::MiddleButton};
	plan.panModifiers = Qt::NoModifier;
	plan.leftPanModifiers = Qt::ControlModifier;
	plan.toggleModifiers = Qt::ShiftModifier;
	plan.wheel = CameraWheel::Zoom;
	controls.navigation.zoomButton = Qt::LeftButton;
	controls.navigation.zoomModifiers = Qt::ShiftModifier;
	controls.navigation.invertWheel3D = true;
	controls.navigation.orbitLeavesOrthographic = false;
	// Shift+left adds; Shift+right drag removes.
	controls.selection.extendModifiers = Qt::ShiftModifier;
	controls.selection.extendToggles = false;
	controls.selection.subtractModifiers = Qt::NoModifier;
	controls.selection.subtractDragButton = Qt::RightButton;
	controls.selection.subtractDragModifiers = Qt::ShiftModifier;
	controls.selection.loopModifiers = Qt::NoModifier;
	controls.selection.ringModifiers = Qt::NoModifier;
	controls.selection.pathModifiers = Qt::NoModifier;
	controls.selection.lassoButton = Qt::NoButton;
	controls.selection.lassoModifiers = Qt::NoModifier;
	controls.selection.cursorButton = Qt::NoButton;
	controls.selection.cursorModifiers = Qt::NoModifier;
	controls.transform.style = ModelTransformStyle::ToolMode;
	controls.transform.dragSelectionTransforms = true;
	controls.transform.precisionModifiers = Qt::NoModifier;
	controls.transform.snapModifiers = Qt::NoModifier;
	controls.layout.layout = ModelViewLayout::FourViews;
	controls.layout.panes = {ModelPaneView::Front, ModelPaneView::Top, ModelPaneView::Right, ModelPaneView::Perspective};
	controls.layout.startInPerspective = true;
	controls.layout.family = QStringLiteral("milkshape");
	controls.inheritDefaultKeys = false;
	// MilkShape 3D 1.8 defaults (Tools > Edit Shortcuts), see docs/CREDITS.md.
	bind(controls, "openMesh", {"Ctrl+O"});
	bind(controls, "saveMesh", {"Ctrl+S"});
	bind(controls, "undoMesh", {"Ctrl+Z"});
	bind(controls, "redoMesh", {"Ctrl+R"});
	bind(controls, "meshDuplicate", {"Ctrl+D"});
	bind(controls, "meshDeleteFaces", {"Del"});
	bind(controls, "meshSelectAll", {"Ctrl+A"});
	bind(controls, "meshSelectNone", {"Ctrl+Shift+A"});
	bind(controls, "meshSelectInvert", {"Ctrl+I"});
	bind(controls, "meshHide", {"Ctrl+H"});
	bind(controls, "meshReveal", {"Ctrl+U"});
	bind(controls, "meshMergeCentre", {"Ctrl+N"});
	bind(controls, "meshSnapSelectionToGrid", {"Ctrl+G"});
	bind(controls, "meshMergeDistance", {"Ctrl+W"});
	bind(controls, "meshSplitEdges", {"Ctrl+P"});
	bind(controls, "meshFlattenX", {"Ctrl+Shift+X"});
	bind(controls, "meshFlattenY", {"Ctrl+Shift+Y"});
	bind(controls, "meshFlattenZ", {"Ctrl+Shift+Z"});
	bind(controls, "meshPoke", {"Ctrl+3"});
	bind(controls, "meshSubdivide", {"Ctrl+4"});
	bind(controls, "meshShadeSmooth", {"Ctrl+M"});
	bind(controls, "meshCopyPoseToFrame", {"Ctrl+K"});
	bind(controls, "meshShowUvEditor", {"Ctrl+T"});
	bind(controls, "meshToolSelect", {"F1"});
	bind(controls, "meshToolMove", {"F2"});
	bind(controls, "meshToolRotate", {"F3"});
	bind(controls, "meshToolScale", {"F4"});
	bind(controls, "meshModeVertex", {"F5"});
	bind(controls, "meshModeFace", {"F6"});
	bind(controls, "meshExtrude", {"F7"});
	bind(controls, "meshModeTag", {"F8"});
	bind(controls, "meshAssignMaterial", {"F10"});
	bind(controls, "meshOperatorSearch", {"Ctrl+Shift+P"});
	bind(controls, "meshViewAll", {"Home"});
	// Not MilkShape's: a keyboard route between its four views.
	bind(controls, "meshNextView", {"Ctrl+Alt+PgDown"});
	bind(controls, "meshPreviousView", {"Ctrl+Alt+PgUp"});
	return controls;
}

struct ProfileEntry {
	const char* id;
	const char* name;
	const char* shortName;
	const char* description;
	const char* adaptation;
	const char* url;
	const char* aliases;
	ModelEditorControls (*controls)();
};

const ProfileEntry kProfiles[] = {
	{"studio", QT_TRANSLATE_NOOP("SlopStudioModelControls", "SlopStudio (Blender-style)"), "SlopStudio",
		QT_TRANSLATE_NOOP("SlopStudioModelControls", "The studio's own controls, following Blender: middle-drag orbits, Shift+middle pans, Ctrl+middle zooms, and Alt+left drag orbits too. G, R and S transform at once; 1, 2 and 3 pick vertices, edges and faces; Tab switches between whole surfaces and their components."),
		QT_TRANSLATE_NOOP("SlopStudioModelControls", "Starts in the orthographic orbit view. Ctrl+Y also redoes. Alt+L and Alt+R grow the selected edges to loops and rings, as in 3ds Max, and Ctrl+Alt+Page Down and Page Up step between four views."),
		"https://docs.blender.org/manual/en/latest/editors/3dview/navigate/index.html", "slopstudio|default|slopstudio-default", &studioControls},
	{"blender", QT_TRANSLATE_NOOP("SlopStudioModelControls", "Blender"), "Blender",
		QT_TRANSLATE_NOOP("SlopStudioModelControls", "Blender 4 defaults: middle-drag orbits, Shift+middle pans, Ctrl+middle zooms, numpad views, G/R/S modal transforms with X/Y/Z constraints and typed values, E extrude, I inset, Ctrl+R loop cut, H/Alt+H hide and reveal, Tab edit mode, Ctrl+Alt+Q four views and Space to play."),
		QT_TRANSLATE_NOOP("SlopStudioModelControls", "Three-button emulation is off, as in Blender. Ctrl+B bevels edges, Ctrl+Shift+B bevels vertices, K starts Knife, V rips and Alt+V rips with a fill. Spin is available in the Edge and Face menus. Z and Shift+S open ordinary menus. Arrow keys orbit the view in steps rather than stepping frames. Alt+L and Alt+R (loops and rings of the selected edges) and Ctrl+Alt+Page Down and Page Up (next and previous of four views) are not Blender keys."),
		"https://docs.blender.org/manual/en/latest/interface/keymap/blender_default.html", "blender-3d|blender4|b3d", &blenderControls},
	{"3ds-max", QT_TRANSLATE_NOOP("SlopStudioModelControls", "3ds Max"), "3ds Max",
		QT_TRANSLATE_NOOP("SlopStudioModelControls", "Autodesk 3ds Max defaults: four views (Top, Front, Left, Perspective), middle-drag pan, Alt+middle orbit, Ctrl+Alt+middle zoom, Q/W/E/R select, move, rotate and scale tools, Ctrl adds and Alt removes, 1-6 sub-object levels, T/F/L/P/U views, Z zoom extents and Alt+W to maximise a view."),
		QT_TRANSLATE_NOOP("SlopStudioModelControls", "Border (3) switches to edges and selects the open boundary loops of the selection; Element (5) selects whole connected pieces. Shift+drag with the Move tool duplicates the selection rather than cloning an object. Chamfer (Ctrl+Shift+C) bevels vertices; Connect (Ctrl+Shift+E) adds edges between the selected edges' opposite sides; MeshSmooth (Ctrl+M) subdivides. Cut, Quickslice, Swift Loop, soft selection, the Modifier Stack and quad menus are not emulated; right-click opens the context menu. Ctrl+Alt+Page Down and Page Up step between the four views, which 3ds Max does not do."),
		"https://help.autodesk.com/view/3DSMAX/2024/ENU/?guid=GUID-A73E1B09-7BFE-4A22-8153-1D3D2237B8E9", "max|3dsmax|3d-studio-max|3ds|autodesk-3ds-max", &maxControls},
	{"milkshape-3d", QT_TRANSLATE_NOOP("SlopStudioModelControls", "MilkShape 3D"), "MilkShape 3D",
		QT_TRANSLATE_NOOP("SlopStudioModelControls", "MilkShape 3D 1.8 defaults: four views (Front, Top, Side, 3D), Ctrl+left drag pans, Shift+left drag zooms, left drag rotates the 3D view, F1-F4 select, move, rotate and scale tools, Shift adds and Shift+right drag removes, Ctrl+W welds, Ctrl+N snaps together, Ctrl+H/Ctrl+U hide and unhide."),
		QT_TRANSLATE_NOOP("SlopStudioModelControls", "Groups are surfaces and Joints are tags and skeleton joints. F5 and F6 switch to vertex and face selection; Poly Build (Vertex menu) creates vertices and faces corner by corner as MilkShape's vertex and face tools do, and Customise Controls can put it on F6. F8 picks tags and joints. Snap Together (Ctrl+N) merges the vertices at their centre; Divide Edge (Ctrl+P) splits the selected edges; Subdivide 3 and 4 poke and subdivide faces. Set Keyframe (Ctrl+K) copies the pose to the current frame. Delete All, the sphere, geosphere, box, cylinder and text tools' click-to-create gestures and the Comments tab are not emulated. Ctrl+Alt+Page Down and Page Up step between the four views, which MilkShape 3D does not do; edge loops and rings of the selection are in the Select menu without keys."),
		"https://chumbalum.swissquake.ch/", "milkshape|ms3d|milkshape3d|milk-shape", &milkShapeControls},
};

const ProfileEntry* profileEntry(const QString& id)
{
	for (const ProfileEntry& entry : kProfiles) {
		if (id == QLatin1String(entry.id)) {
			return &entry;
		}
	}
	return nullptr;
}

// --- Gesture text ----------------------------------------------------------

QString buttonText(Qt::MouseButton button)
{
	switch (button) {
	case Qt::LeftButton:
		return QStringLiteral("Left");
	case Qt::MiddleButton:
		return QStringLiteral("Middle");
	case Qt::RightButton:
		return QStringLiteral("Right");
	default:
		break;
	}
	return QStringLiteral("None");
}

QString describeGesture(Qt::MouseButton button, Qt::KeyboardModifiers modifiers, bool drag)
{
	if (button == Qt::NoButton) {
		return {};
	}
	return mouseGestureText(button, modifiers, drag ? QCoreApplication::translate("SlopStudioModelControls", "drag") : QCoreApplication::translate("SlopStudioModelControls", "click"));
}

} // namespace

// --- Profiles --------------------------------------------------------------

QString defaultModelEditorProfileId()
{
	return QStringLiteral("studio");
}

QString normalizedModelEditorProfileId(const QString& id)
{
	// VibeStudio, the studio's former name, saved "vibestudio-default".
	QString key = withCurrentStudioName(id.trimmed().toLower());
	key.replace(QLatin1Char(' '), QLatin1Char('-'));
	key.replace(QLatin1Char('_'), QLatin1Char('-'));
	for (const ProfileEntry& entry : kProfiles) {
		if (key == QLatin1String(entry.id)) {
			return key;
		}
		for (const QString& alias : QString::fromLatin1(entry.aliases).split(QLatin1Char('|'))) {
			if (key == alias) {
				return QString::fromLatin1(entry.id);
			}
		}
	}
	return {};
}

QVector<ModelEditorProfile> modelEditorProfiles()
{
	QVector<ModelEditorProfile> profiles;
	for (const ProfileEntry& entry : kProfiles) {
		ModelEditorProfile profile;
		profile.id = QString::fromLatin1(entry.id);
		profile.displayName = QCoreApplication::translate("SlopStudioModelControls", entry.name);
		profile.shortName = QString::fromUtf8(entry.shortName);
		profile.description = QCoreApplication::translate("SlopStudioModelControls", entry.description);
		profile.adaptations = {QCoreApplication::translate("SlopStudioModelControls", entry.adaptation)};
		profile.referenceUrl = QString::fromLatin1(entry.url);
		profile.aliases = QString::fromLatin1(entry.aliases).split(QLatin1Char('|'));
		profile.controls = entry.controls();
		profiles.append(profile);
	}
	return profiles;
}

QStringList modelEditorProfileIds()
{
	QStringList ids;
	for (const ProfileEntry& entry : kProfiles) {
		ids << QString::fromLatin1(entry.id);
	}
	return ids;
}

bool modelEditorProfileForId(const QString& id, ModelEditorProfile* out)
{
	const QString normalized = normalizedModelEditorProfileId(id);
	if (normalized.isEmpty()) {
		return false;
	}
	for (const ModelEditorProfile& profile : modelEditorProfiles()) {
		if (profile.id == normalized) {
			if (out) { *out = profile; }
			return true;
		}
	}
	return false;
}

ModelEditorControls modelEditorControlsForProfile(const QString& id)
{
	const ProfileEntry* entry = profileEntry(normalizedModelEditorProfileId(id));
	return entry ? entry->controls() : studioControls();
}

QString modelEditorProfileForLevelProfile(const QString& levelProfileId)
{
	const QString id = levelProfileId.trimmed().toLower();
	if (id == QLatin1String("blender") || id == QLatin1String("blender-3d")) {
		return QStringLiteral("blender");
	}
	return defaultModelEditorProfileId();
}

// --- Commands --------------------------------------------------------------

QVector<ModelEditorCommand> modelEditorCommands()
{
	QVector<ModelEditorCommand> commands;
	for (const CommandEntry& entry : kCommands) {
		ModelEditorCommand command;
		command.id = QString::fromLatin1(entry.id);
		command.category = QString::fromLatin1(entry.category);
		command.title = QCoreApplication::translate("SlopStudioModelControls", entry.title);
		const QString keys = QString::fromLatin1(entry.keys);
		if (!keys.isEmpty()) {
			command.defaultKeys = keys.split(QLatin1Char('|'));
		}
		commands.append(command);
	}
	return commands;
}

bool modelEditorCommandForId(const QString& id, ModelEditorCommand* out)
{
	for (const CommandEntry& entry : kCommands) {
		if (id == QLatin1String(entry.id)) {
			if (out) {
				out->id = id;
				out->category = QString::fromLatin1(entry.category);
				out->title = QCoreApplication::translate("SlopStudioModelControls", entry.title);
				const QString keys = QString::fromLatin1(entry.keys);
				out->defaultKeys = keys.isEmpty() ? QStringList{} : keys.split(QLatin1Char('|'));
			}
			return true;
		}
	}
	return false;
}

QStringList modelEditorCommandKeys(const ModelEditorControls& controls, const QString& commandId)
{
	for (const ModelEditorKeyBinding& binding : controls.keys) {
		if (binding.commandId == commandId) {
			return binding.keys;
		}
	}
	if (!controls.inheritDefaultKeys) {
		return {};
	}
	ModelEditorCommand command;
	return modelEditorCommandForId(commandId, &command) ? command.defaultKeys : QStringList{};
}

QVector<ModelEditorKeyBinding> modelEditorEffectiveKeys(const ModelEditorControls& controls)
{
	QVector<ModelEditorKeyBinding> bindings;
	for (const CommandEntry& entry : kCommands) {
		const QString id = QString::fromLatin1(entry.id);
		bindings.append({id, modelEditorCommandKeys(controls, id)});
	}
	return bindings;
}

namespace {

// Qt's Ctrl is the Command key on macOS, where the system keeps Command+Tab
// for its application switcher and never passes it on. There the physical
// Control key (Qt's Meta) moves focus instead, as Control+Tab does out of
// any macOS control that takes Tab itself.
constexpr Qt::KeyboardModifier kFocusModifier =
#ifdef Q_OS_MACOS
	Qt::MetaModifier;
#else
	Qt::ControlModifier;
#endif

// A single key with Tab (Qt reports Shift+Tab as Backtab) and its modifiers.
bool tabKey(const QString& key, Qt::KeyboardModifiers* modifiers)
{
	const QKeySequence sequence = QKeySequence::fromString(key, QKeySequence::PortableText);
	if (sequence.count() != 1) {
		return false;
	}
	const QKeyCombination combination = sequence[0];
	*modifiers = combination.keyboardModifiers();
	return combination.key() == Qt::Key_Tab || combination.key() == Qt::Key_Backtab;
}

} // namespace

QStringList modelEditorFocusKeys(bool forward)
{
	const Qt::KeyboardModifiers modifiers = Qt::KeyboardModifiers(kFocusModifier) | (forward ? Qt::NoModifier : Qt::ShiftModifier);
	return {QKeySequence(QKeyCombination(modifiers, Qt::Key_Tab)).toString(QKeySequence::PortableText)};
}

QString modelEditorFocusKeyText()
{
	const auto native = [](bool forward) {
		return QKeySequence::fromString(modelEditorFocusKeys(forward).value(0), QKeySequence::PortableText).toString(QKeySequence::NativeText);
	};
	return QCoreApplication::translate("SlopStudioModelControls", "%1 and %2 always move keyboard focus between panels.").arg(native(true), native(false));
}

bool isModelEditorFocusKey(const QString& key)
{
	Qt::KeyboardModifiers modifiers;
	return tabKey(key, &modifiers) && modifiers.testFlag(kFocusModifier);
}

bool isModelEditorTabKey(const QString& key)
{
	Qt::KeyboardModifiers modifiers;
	return tabKey(key, &modifiers) && (modifiers & ~Qt::ShiftModifier) == Qt::NoModifier;
}

// --- Ids -------------------------------------------------------------------

QString modelViewLayoutId(ModelViewLayout layout)
{
	return layout == ModelViewLayout::FourViews ? QStringLiteral("four-views") : QStringLiteral("single");
}

bool modelViewLayoutForId(const QString& id, ModelViewLayout* out)
{
	if (id == QLatin1String("single")) {
		*out = ModelViewLayout::Single;
		return true;
	}
	if (id == QLatin1String("four-views") || id == QLatin1String("quad")) {
		*out = ModelViewLayout::FourViews;
		return true;
	}
	return false;
}

QString modelPaneViewId(ModelPaneView view)
{
	switch (view) {
	case ModelPaneView::Perspective: return QStringLiteral("perspective");
	case ModelPaneView::Top: return QStringLiteral("top");
	case ModelPaneView::Bottom: return QStringLiteral("bottom");
	case ModelPaneView::Front: return QStringLiteral("front");
	case ModelPaneView::Back: return QStringLiteral("back");
	case ModelPaneView::Left: return QStringLiteral("left");
	case ModelPaneView::Right: return QStringLiteral("right");
	}
	return QStringLiteral("perspective");
}

bool modelPaneViewForId(const QString& id, ModelPaneView* out)
{
	for (ModelPaneView view : {ModelPaneView::Perspective, ModelPaneView::Top, ModelPaneView::Bottom, ModelPaneView::Front,
			 ModelPaneView::Back, ModelPaneView::Left, ModelPaneView::Right}) {
		if (id == modelPaneViewId(view)) {
			*out = view;
			return true;
		}
	}
	return false;
}

QString modelPaneViewDisplayName(ModelPaneView view)
{
	switch (view) {
	case ModelPaneView::Perspective: return QCoreApplication::translate("SlopStudioModelControls", "Perspective");
	case ModelPaneView::Top: return QCoreApplication::translate("SlopStudioModelControls", "Top");
	case ModelPaneView::Bottom: return QCoreApplication::translate("SlopStudioModelControls", "Bottom");
	case ModelPaneView::Front: return QCoreApplication::translate("SlopStudioModelControls", "Front");
	case ModelPaneView::Back: return QCoreApplication::translate("SlopStudioModelControls", "Back");
	case ModelPaneView::Left: return QCoreApplication::translate("SlopStudioModelControls", "Left");
	case ModelPaneView::Right: return QCoreApplication::translate("SlopStudioModelControls", "Right");
	}
	return QCoreApplication::translate("SlopStudioModelControls", "Perspective");
}

QString modelTransformStyleId(ModelTransformStyle style)
{
	return style == ModelTransformStyle::ToolMode ? QStringLiteral("tool") : QStringLiteral("modal");
}

bool modelTransformStyleForId(const QString& id, ModelTransformStyle* out)
{
	if (id == QLatin1String("modal")) {
		*out = ModelTransformStyle::Modal;
		return true;
	}
	if (id == QLatin1String("tool")) {
		*out = ModelTransformStyle::ToolMode;
		return true;
	}
	return false;
}

// --- Gesture text ----------------------------------------------------------

QString modelModifierText(Qt::KeyboardModifiers modifiers)
{
	QStringList parts;
	if (modifiers & Qt::ControlModifier) { parts << QStringLiteral("Ctrl"); }
	if (modifiers & Qt::AltModifier) { parts << QStringLiteral("Alt"); }
	if (modifiers & Qt::ShiftModifier) { parts << QStringLiteral("Shift"); }
	if (modifiers & Qt::MetaModifier) { parts << QStringLiteral("Meta"); }
	return parts.isEmpty() ? QStringLiteral("None") : parts.join(QLatin1Char('+'));
}

bool parseModelModifiers(const QString& text, Qt::KeyboardModifiers* modifiers)
{
	Qt::KeyboardModifiers result = Qt::NoModifier;
	const QString trimmed = text.trimmed();
	if (trimmed.isEmpty() || trimmed.compare(QLatin1String("None"), Qt::CaseInsensitive) == 0) {
		*modifiers = Qt::NoModifier;
		return true;
	}
	for (const QString& part : trimmed.split(QLatin1Char('+'), Qt::SkipEmptyParts)) {
		const QString name = part.trimmed().toLower();
		if (name == QLatin1String("ctrl") || name == QLatin1String("control")) {
			result |= Qt::ControlModifier;
		} else if (name == QLatin1String("alt")) {
			result |= Qt::AltModifier;
		} else if (name == QLatin1String("shift")) {
			result |= Qt::ShiftModifier;
		} else if (name == QLatin1String("meta") || name == QLatin1String("cmd")) {
			result |= Qt::MetaModifier;
		} else {
			return false;
		}
	}
	*modifiers = result;
	return true;
}

QString modelGestureText(Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
	if (button == Qt::NoButton) {
		return QStringLiteral("None");
	}
	const QString keys = modifiers == Qt::NoModifier ? QString() : modelModifierText(modifiers) + QLatin1Char('+');
	return keys + buttonText(button);
}

bool parseModelGesture(const QString& text, Qt::MouseButton* button, Qt::KeyboardModifiers* modifiers)
{
	const QString trimmed = text.trimmed();
	if (trimmed.isEmpty() || trimmed.compare(QLatin1String("None"), Qt::CaseInsensitive) == 0) {
		*button = Qt::NoButton;
		*modifiers = Qt::NoModifier;
		return true;
	}
	const int plus = trimmed.lastIndexOf(QLatin1Char('+'));
	const QString buttonName = (plus < 0 ? trimmed : trimmed.mid(plus + 1)).trimmed().toLower();
	Qt::MouseButton parsedButton = Qt::NoButton;
	if (buttonName == QLatin1String("left")) {
		parsedButton = Qt::LeftButton;
	} else if (buttonName == QLatin1String("middle")) {
		parsedButton = Qt::MiddleButton;
	} else if (buttonName == QLatin1String("right")) {
		parsedButton = Qt::RightButton;
	} else {
		return false;
	}
	Qt::KeyboardModifiers parsedModifiers = Qt::NoModifier;
	if (plus > 0 && !parseModelModifiers(trimmed.left(plus), &parsedModifiers)) {
		return false;
	}
	*button = parsedButton;
	*modifiers = parsedModifiers;
	return true;
}

// --- Reference rows and problems -------------------------------------------

QVector<ModelEditorControlRow> modelEditorControlRows(const ModelEditorControls& controls)
{
	QVector<ModelEditorControlRow> rows;
	const auto row = [&rows](const QString& section, const QString& view, const QString& action, const QString& gesture) {
		if (!gesture.isEmpty()) {
			rows.append({section, view, action, gesture});
		}
	};
	const QString view3D = QCoreApplication::translate("SlopStudioModelControls", "3D view");
	const QString panes = QCoreApplication::translate("SlopStudioModelControls", "Orthographic views");
	const QString selection = QCoreApplication::translate("SlopStudioModelControls", "Selection");
	const QString transforms = QCoreApplication::translate("SlopStudioModelControls", "Transforms");
	const QString layout = QCoreApplication::translate("SlopStudioModelControls", "Layout");
	const QString keys = QCoreApplication::translate("SlopStudioModelControls", "Keys");
	for (const auto& [view, camera] : {std::pair<QString, const CameraViewControls*>{view3D, &controls.navigation.view3D},
			 std::pair<QString, const CameraViewControls*>{panes, &controls.navigation.orthographic}}) {
		row(QStringLiteral("navigation"), view, QCoreApplication::translate("SlopStudioModelControls", "Orbit"), describeGesture(camera->orbitButton, camera->orbitModifiers, true));
		for (Qt::MouseButton button : camera->panButtons) {
			row(QStringLiteral("navigation"), view, QCoreApplication::translate("SlopStudioModelControls", "Pan"), describeGesture(button, camera->panModifiers, true));
		}
		if (camera->leftPanModifiers != Qt::NoModifier) {
			row(QStringLiteral("navigation"), view, QCoreApplication::translate("SlopStudioModelControls", "Pan"), describeGesture(Qt::LeftButton, camera->leftPanModifiers, true));
		}
		row(QStringLiteral("navigation"), view, QCoreApplication::translate("SlopStudioModelControls", "Zoom"),
			describeGesture(controls.navigation.zoomButton, controls.navigation.zoomModifiers, true));
		if (camera == &controls.navigation.view3D && controls.navigation.invertWheel3D) {
			row(QStringLiteral("navigation"), view, QCoreApplication::translate("SlopStudioModelControls", "Zoom"), QCoreApplication::translate("SlopStudioModelControls", "Wheel up zooms out"));
		} else {
			row(QStringLiteral("navigation"), view, QCoreApplication::translate("SlopStudioModelControls", "Zoom"), QCoreApplication::translate("SlopStudioModelControls", "Wheel"));
		}
		// Three-button emulation turns Alt+left drag into a middle drag in every view.
		if (controls.navigation.emulateMiddleButton) {
			row(QStringLiteral("navigation"), view, QCoreApplication::translate("SlopStudioModelControls", "Orbit"), describeGesture(Qt::LeftButton, Qt::AltModifier, true));
		}
	}
	row(QStringLiteral("navigation"), panes, QCoreApplication::translate("SlopStudioModelControls", "Orbiting"),
		controls.navigation.orbitLeavesOrthographic ? QCoreApplication::translate("SlopStudioModelControls", "Turns the view into a user view") : QCoreApplication::translate("SlopStudioModelControls", "Orthographic views never rotate"));
	const ModelSelectionControls& s = controls.selection;
	row(QStringLiteral("selection"), selection, QCoreApplication::translate("SlopStudioModelControls", "Select"), describeGesture(Qt::LeftButton, Qt::NoModifier, false));
	if (s.extendModifiers != Qt::NoModifier) {
		row(QStringLiteral("selection"), selection, s.extendToggles ? QCoreApplication::translate("SlopStudioModelControls", "Add or remove") : QCoreApplication::translate("SlopStudioModelControls", "Add"),
			describeGesture(Qt::LeftButton, s.extendModifiers, false));
	}
	if (s.subtractModifiers != Qt::NoModifier) {
		// When the path modifier is the same, a click picks a path and only a
		// box drag removes, as Ctrl does in Blender.
		row(QStringLiteral("selection"), selection, QCoreApplication::translate("SlopStudioModelControls", "Remove"),
			describeGesture(Qt::LeftButton, s.subtractModifiers, s.subtractModifiers == s.pathModifiers));
	}
	row(QStringLiteral("selection"), selection, QCoreApplication::translate("SlopStudioModelControls", "Remove enclosed"), describeGesture(s.subtractDragButton, s.subtractDragModifiers, true));
	if (s.emptyDragBoxSelects) {
		row(QStringLiteral("selection"), selection, QCoreApplication::translate("SlopStudioModelControls", "Box select"), describeGesture(Qt::LeftButton, Qt::NoModifier, true));
	}
	if (s.loopModifiers != Qt::NoModifier) {
		row(QStringLiteral("selection"), selection, QCoreApplication::translate("SlopStudioModelControls", "Edge loop"), describeGesture(Qt::LeftButton, s.loopModifiers, false));
	}
	if (s.ringModifiers != Qt::NoModifier) {
		row(QStringLiteral("selection"), selection, QCoreApplication::translate("SlopStudioModelControls", "Edge ring"), describeGesture(Qt::LeftButton, s.ringModifiers, false));
	}
	if (s.pathModifiers != Qt::NoModifier) {
		row(QStringLiteral("selection"), selection, QCoreApplication::translate("SlopStudioModelControls", "Shortest path"), describeGesture(Qt::LeftButton, s.pathModifiers, false));
	}
	if (s.doubleClickSelectsLoop) {
		row(QStringLiteral("selection"), selection, QCoreApplication::translate("SlopStudioModelControls", "Edge loop"), QCoreApplication::translate("SlopStudioModelControls", "Double-click an edge"));
	}
	row(QStringLiteral("selection"), selection, QCoreApplication::translate("SlopStudioModelControls", "Lasso"), describeGesture(s.lassoButton, s.lassoModifiers, true));
	row(QStringLiteral("selection"), selection, QCoreApplication::translate("SlopStudioModelControls", "Place 3D cursor"), describeGesture(s.cursorButton, s.cursorModifiers, false));
	if (s.rightClickMenu) {
		row(QStringLiteral("selection"), selection, QCoreApplication::translate("SlopStudioModelControls", "Context menu"), describeGesture(Qt::RightButton, Qt::NoModifier, false));
	}
	const ModelTransformControls& t = controls.transform;
	row(QStringLiteral("transform"), transforms, QCoreApplication::translate("SlopStudioModelControls", "Style"),
		t.style == ModelTransformStyle::Modal ? QCoreApplication::translate("SlopStudioModelControls", "Keys start a transform that follows the pointer")
											  : QCoreApplication::translate("SlopStudioModelControls", "Keys pick a tool; drag the selection or its handles"));
	if (t.dragSelectionTransforms) {
		row(QStringLiteral("transform"), transforms, QCoreApplication::translate("SlopStudioModelControls", "Transform"), QCoreApplication::translate("SlopStudioModelControls", "Drag the selection with a tool"));
	}
	if (t.duplicateDragModifiers != Qt::NoModifier) {
		row(QStringLiteral("transform"), transforms, QCoreApplication::translate("SlopStudioModelControls", "Duplicate"), describeGesture(Qt::LeftButton, t.duplicateDragModifiers, true));
	}
	if (t.precisionModifiers != Qt::NoModifier) {
		row(QStringLiteral("transform"), transforms, QCoreApplication::translate("SlopStudioModelControls", "Fine control"), modelModifierText(t.precisionModifiers));
	}
	if (t.snapModifiers != Qt::NoModifier) {
		row(QStringLiteral("transform"), transforms, QCoreApplication::translate("SlopStudioModelControls", "Snap while held"), modelModifierText(t.snapModifiers));
	}
	if (controls.layout.layout == ModelViewLayout::FourViews) {
		QStringList names;
		for (ModelPaneView pane : controls.layout.panes) {
			names << modelPaneViewDisplayName(pane);
		}
		row(QStringLiteral("layout"), layout, QCoreApplication::translate("SlopStudioModelControls", "Views"), names.join(QStringLiteral(", ")));
	} else {
		row(QStringLiteral("layout"), layout, QCoreApplication::translate("SlopStudioModelControls", "Views"), controls.layout.startInPerspective ? QCoreApplication::translate("SlopStudioModelControls", "One perspective view") : QCoreApplication::translate("SlopStudioModelControls", "One orbit view"));
	}
	// Every profile keeps these, so a view that takes Tab never traps focus.
	const auto native = [](const QStringList& portable) {
		QStringList text;
		for (const QString& key : portable) {
			text << QKeySequence::fromString(key, QKeySequence::PortableText).toString(QKeySequence::NativeText);
		}
		return text.join(QStringLiteral(", "));
	};
	row(QStringLiteral("keys"), keys, QCoreApplication::translate("SlopStudioModelControls", "Move focus to the next panel"), native(modelEditorFocusKeys(true)));
	row(QStringLiteral("keys"), keys, QCoreApplication::translate("SlopStudioModelControls", "Move focus to the previous panel"), native(modelEditorFocusKeys(false)));
	for (const ModelEditorKeyBinding& binding : modelEditorEffectiveKeys(controls)) {
		if (binding.keys.isEmpty()) {
			continue;
		}
		ModelEditorCommand command;
		if (!modelEditorCommandForId(binding.commandId, &command)) {
			continue;
		}
		row(QStringLiteral("keys"), keys, command.title, native(binding.keys));
	}
	return rows;
}

QStringList modelEditorControlProblems(const ModelEditorControls& controls)
{
	QStringList problems;
	QHash<QString, QString> keyOwner;
	for (const ModelEditorKeyBinding& binding : controls.keys) {
		if (!modelEditorCommandForId(binding.commandId)) {
			problems << QCoreApplication::translate("SlopStudioModelControls", "Unknown command: %1.").arg(binding.commandId);
		}
	}
	for (const ModelEditorKeyBinding& binding : modelEditorEffectiveKeys(controls)) {
		for (const QString& key : binding.keys) {
			const QKeySequence sequence = QKeySequence::fromString(key, QKeySequence::PortableText);
			if (sequence.isEmpty()) {
				problems << QCoreApplication::translate("SlopStudioModelControls", "%1 has a key Qt cannot read: %2.").arg(binding.commandId, key);
				continue;
			}
			const QString canonical = sequence.toString(QKeySequence::PortableText);
			if (isModelEditorFocusKey(canonical)) {
				problems << QCoreApplication::translate("SlopStudioModelControls", "%1 cannot use %2: %3")
					.arg(binding.commandId, canonical, modelEditorFocusKeyText());
				continue;
			}
			if (keyOwner.contains(canonical) && keyOwner.value(canonical) != binding.commandId) {
				problems << QCoreApplication::translate("SlopStudioModelControls", "%1 is bound to both %2 and %3.").arg(canonical, keyOwner.value(canonical), binding.commandId);
			} else {
				keyOwner.insert(canonical, binding.commandId);
			}
		}
	}
	// Two roles on one left-button modifier set in the selection gestures.
	const ModelSelectionControls& s = controls.selection;
	QHash<int, QString> leftRoles;
	const auto claim = [&](Qt::KeyboardModifiers modifiers, const QString& role) {
		if (modifiers == Qt::NoModifier) {
			return;
		}
		const int key = int(modifiers);
		if (leftRoles.contains(key)) {
			problems << QCoreApplication::translate("SlopStudioModelControls", "%1 click is both %2 and %3.").arg(modelModifierText(modifiers), leftRoles.value(key), role);
		} else {
			leftRoles.insert(key, role);
		}
	};
	// Subtract is for regions and for clicks no click role claims, so it
	// may share keys with the loop, ring and path clicks (Blender's Ctrl).
	claim(s.extendModifiers, QCoreApplication::translate("SlopStudioModelControls", "extend"));
	if (s.subtractModifiers != Qt::NoModifier && s.subtractModifiers == s.extendModifiers) {
		problems << QCoreApplication::translate("SlopStudioModelControls", "%1 both adds to and removes from the selection.")
			.arg(modelModifierText(s.subtractModifiers));
	}
	claim(s.loopModifiers, QCoreApplication::translate("SlopStudioModelControls", "loop"));
	claim(s.ringModifiers, QCoreApplication::translate("SlopStudioModelControls", "ring"));
	claim(s.pathModifiers, QCoreApplication::translate("SlopStudioModelControls", "path"));
	// A navigation drag on the left button must not shadow selection.
	for (const CameraViewControls* camera : {&controls.navigation.view3D, &controls.navigation.orthographic}) {
		if (camera->orbitButton == Qt::LeftButton && camera->orbitModifiers == Qt::NoModifier && controls.selection.emptyDragBoxSelects
			&& camera == &controls.navigation.orthographic) {
			problems << QCoreApplication::translate("SlopStudioModelControls", "A plain left drag both orbits the orthographic views and draws a selection box.");
		}
	}
	if (controls.navigation.zoomButton != Qt::NoButton) {
		for (const CameraViewControls* camera : {&controls.navigation.view3D, &controls.navigation.orthographic}) {
			if (camera->orbitButton == controls.navigation.zoomButton && camera->orbitModifiers == controls.navigation.zoomModifiers) {
				problems << QCoreApplication::translate("SlopStudioModelControls", "%1 both orbits and zooms.").arg(modelGestureText(controls.navigation.zoomButton, controls.navigation.zoomModifiers));
			}
			for (Qt::MouseButton button : camera->panButtons) {
				if (button == controls.navigation.zoomButton && camera->panModifiers == controls.navigation.zoomModifiers) {
					problems << QCoreApplication::translate("SlopStudioModelControls", "%1 both pans and zooms.").arg(modelGestureText(controls.navigation.zoomButton, controls.navigation.zoomModifiers));
				}
			}
		}
	}
	if (controls.layout.panes.size() != 4) {
		problems << QCoreApplication::translate("SlopStudioModelControls", "The four-view layout needs exactly four panes.");
	}
	problems.removeDuplicates();
	return problems;
}

// --- Customisation ---------------------------------------------------------

namespace {

QJsonArray keysJson(const QStringList& keys)
{
	QJsonArray array;
	for (const QString& key : keys) {
		array.append(key);
	}
	return array;
}

QJsonObject navigationJson(const ModelNavigationControls& n)
{
	QJsonObject object;
	object.insert(QStringLiteral("orbit"), modelGestureText(n.view3D.orbitButton, n.view3D.orbitModifiers));
	object.insert(QStringLiteral("orthographicOrbit"), modelGestureText(n.orthographic.orbitButton, n.orthographic.orbitModifiers));
	object.insert(QStringLiteral("pan"), modelGestureText(n.view3D.panButtons.isEmpty() ? Qt::NoButton : n.view3D.panButtons.first(),
		n.view3D.panModifiers));
	object.insert(QStringLiteral("leftPan"), modelModifierText(n.view3D.leftPanModifiers));
	object.insert(QStringLiteral("zoom"), modelGestureText(n.zoomButton, n.zoomModifiers));
	object.insert(QStringLiteral("invertWheel3D"), n.invertWheel3D);
	object.insert(QStringLiteral("orbitLeavesOrthographic"), n.orbitLeavesOrthographic);
	object.insert(QStringLiteral("emulateMiddleButton"), n.emulateMiddleButton);
	return object;
}

QJsonObject selectionJson(const ModelSelectionControls& s)
{
	QJsonObject object;
	object.insert(QStringLiteral("extend"), modelModifierText(s.extendModifiers));
	object.insert(QStringLiteral("extendToggles"), s.extendToggles);
	object.insert(QStringLiteral("subtract"), modelModifierText(s.subtractModifiers));
	object.insert(QStringLiteral("subtractDrag"), modelGestureText(s.subtractDragButton, s.subtractDragModifiers));
	object.insert(QStringLiteral("emptyDragBoxSelects"), s.emptyDragBoxSelects);
	object.insert(QStringLiteral("loop"), modelModifierText(s.loopModifiers));
	object.insert(QStringLiteral("ring"), modelModifierText(s.ringModifiers));
	object.insert(QStringLiteral("path"), modelModifierText(s.pathModifiers));
	object.insert(QStringLiteral("doubleClickSelectsLoop"), s.doubleClickSelectsLoop);
	object.insert(QStringLiteral("lasso"), modelGestureText(s.lassoButton, s.lassoModifiers));
	object.insert(QStringLiteral("cursor"), modelGestureText(s.cursorButton, s.cursorModifiers));
	object.insert(QStringLiteral("rightClickMenu"), s.rightClickMenu);
	return object;
}

QJsonObject transformJson(const ModelTransformControls& t)
{
	QJsonObject object;
	object.insert(QStringLiteral("style"), modelTransformStyleId(t.style));
	object.insert(QStringLiteral("dragSelectionTransforms"), t.dragSelectionTransforms);
	object.insert(QStringLiteral("duplicateDrag"), modelModifierText(t.duplicateDragModifiers));
	object.insert(QStringLiteral("precision"), modelModifierText(t.precisionModifiers));
	object.insert(QStringLiteral("snap"), modelModifierText(t.snapModifiers));
	return object;
}

QJsonObject layoutJson(const ModelLayoutControls& l)
{
	QJsonObject object;
	object.insert(QStringLiteral("layout"), modelViewLayoutId(l.layout));
	QJsonArray panes;
	for (ModelPaneView pane : l.panes) {
		panes.append(modelPaneViewId(pane));
	}
	object.insert(QStringLiteral("panes"), panes);
	object.insert(QStringLiteral("startInPerspective"), l.startInPerspective);
	object.insert(QStringLiteral("family"), l.family);
	object.insert(QStringLiteral("timeline"), l.timelineVisible);
	object.insert(QStringLiteral("toolShelf"), l.toolShelfVisible);
	return object;
}

QJsonObject keysObject(const ModelEditorControls& controls)
{
	QJsonObject object;
	for (const ModelEditorKeyBinding& binding : modelEditorEffectiveKeys(controls)) {
		object.insert(binding.commandId, keysJson(binding.keys));
	}
	return object;
}

QJsonObject difference(const QJsonObject& base, const QJsonObject& edited)
{
	QJsonObject result;
	for (auto it = edited.begin(); it != edited.end(); ++it) {
		if (base.value(it.key()) != it.value()) {
			result.insert(it.key(), it.value());
		}
	}
	return result;
}

} // namespace

QJsonObject modelEditorControlsJson(const ModelEditorControls& controls)
{
	QJsonObject object;
	object.insert(QStringLiteral("profile"), controls.profileId);
	object.insert(QStringLiteral("navigation"), navigationJson(controls.navigation));
	object.insert(QStringLiteral("selection"), selectionJson(controls.selection));
	object.insert(QStringLiteral("transform"), transformJson(controls.transform));
	object.insert(QStringLiteral("layout"), layoutJson(controls.layout));
	object.insert(QStringLiteral("keys"), keysObject(controls));
	return object;
}

bool applyModelEditorControlOverrides(const ModelEditorControls& base, const QJsonObject& overrides, ModelEditorControls* out,
	QStringList* warnings, QString* error)
{
	ModelEditorControls result = base;
	QStringList notes;
	const auto warn = [&notes](const QString& message) { notes << message; };
	const QSet<QString> sections{QStringLiteral("keys"), QStringLiteral("navigation"), QStringLiteral("selection"), QStringLiteral("transform"),
		QStringLiteral("layout")};
	for (auto it = overrides.begin(); it != overrides.end(); ++it) {
		if (!sections.contains(it.key())) {
			warn(QCoreApplication::translate("SlopStudioModelControls", "Skipped the unknown section \"%1\".").arg(it.key()));
		} else if (!it.value().isObject()) {
			warn(QCoreApplication::translate("SlopStudioModelControls", "Skipped \"%1\": it must be an object.").arg(it.key()));
		}
	}
	const auto gesture = [&](const QJsonObject& section, const char* name, Qt::MouseButton* button, Qt::KeyboardModifiers* modifiers) {
		const QString key = QString::fromLatin1(name);
		if (!section.contains(key)) {
			return;
		}
		Qt::MouseButton parsedButton = Qt::NoButton;
		Qt::KeyboardModifiers parsedModifiers = Qt::NoModifier;
		if (!section.value(key).isString() || !parseModelGesture(section.value(key).toString(), &parsedButton, &parsedModifiers)) {
			warn(QCoreApplication::translate("SlopStudioModelControls", "Skipped \"%1\": expected a gesture such as \"Alt+Middle\".").arg(key));
			return;
		}
		*button = parsedButton;
		*modifiers = parsedModifiers;
	};
	const auto modifier = [&](const QJsonObject& section, const char* name, Qt::KeyboardModifiers* modifiers) {
		const QString key = QString::fromLatin1(name);
		if (!section.contains(key)) {
			return;
		}
		Qt::KeyboardModifiers parsed = Qt::NoModifier;
		if (!section.value(key).isString() || !parseModelModifiers(section.value(key).toString(), &parsed)) {
			warn(QCoreApplication::translate("SlopStudioModelControls", "Skipped \"%1\": expected keys such as \"Ctrl+Alt\" or \"None\".").arg(key));
			return;
		}
		*modifiers = parsed;
	};
	const auto flag = [&](const QJsonObject& section, const char* name, bool* value) {
		const QString key = QString::fromLatin1(name);
		if (!section.contains(key)) {
			return;
		}
		if (!section.value(key).isBool()) {
			warn(QCoreApplication::translate("SlopStudioModelControls", "Skipped \"%1\": expected true or false.").arg(key));
			return;
		}
		*value = section.value(key).toBool();
	};

	const QJsonObject keys = overrides.value(QStringLiteral("keys")).toObject();
	for (auto it = keys.begin(); it != keys.end(); ++it) {
		if (!modelEditorCommandForId(it.key())) {
			warn(QCoreApplication::translate("SlopStudioModelControls", "Skipped keys for the unknown command \"%1\".").arg(it.key()));
			continue;
		}
		QStringList list;
		bool valid = it.value().isArray();
		bool focusKey = false;
		for (const QJsonValue& value : it.value().toArray()) {
			if (!value.isString() || QKeySequence::fromString(value.toString(), QKeySequence::PortableText).isEmpty()) {
				valid = false;
				break;
			}
			const QString key = QKeySequence::fromString(value.toString(), QKeySequence::PortableText).toString(QKeySequence::PortableText);
			// The way out of a view stays with every profile. The warning
			// reaches the editor's status line, so it names the command and
			// the key as the user sees them.
			if (isModelEditorFocusKey(key)) {
				ModelEditorCommand command;
				static_cast<void>(modelEditorCommandForId(it.key(), &command));
				warn(QCoreApplication::translate("SlopStudioModelControls", "Skipped %1 for %2: %3")
					.arg(QKeySequence::fromString(key, QKeySequence::PortableText).toString(QKeySequence::NativeText), command.title, modelEditorFocusKeyText()));
				focusKey = true;
				continue;
			}
			list << key;
		}
		if (!valid) {
			warn(QCoreApplication::translate("SlopStudioModelControls", "Skipped keys for \"%1\": expected a list of key sequences.").arg(it.key()));
			continue;
		}
		if (focusKey && list.isEmpty()) {
			continue;
		}
		ModelEditorKeyBinding binding{it.key(), list};
		bool replaced = false;
		for (ModelEditorKeyBinding& existing : result.keys) {
			if (existing.commandId == it.key()) {
				existing = binding;
				replaced = true;
			}
		}
		if (!replaced) {
			result.keys.append(binding);
		}
	}

	const QJsonObject navigation = overrides.value(QStringLiteral("navigation")).toObject();
	Qt::MouseButton orbitButton = result.navigation.view3D.orbitButton;
	Qt::KeyboardModifiers orbitModifiers = result.navigation.view3D.orbitModifiers;
	gesture(navigation, "orbit", &orbitButton, &orbitModifiers);
	result.navigation.view3D.orbitButton = orbitButton;
	result.navigation.view3D.orbitModifiers = orbitModifiers;
	gesture(navigation, "orthographicOrbit", &result.navigation.orthographic.orbitButton, &result.navigation.orthographic.orbitModifiers);
	if (navigation.contains(QStringLiteral("orbit")) && !navigation.contains(QStringLiteral("orthographicOrbit"))
		&& result.navigation.orbitLeavesOrthographic) {
		result.navigation.orthographic.orbitButton = orbitButton;
		result.navigation.orthographic.orbitModifiers = orbitModifiers;
	}
	if (navigation.contains(QStringLiteral("pan"))) {
		Qt::MouseButton button = Qt::NoButton;
		Qt::KeyboardModifiers modifiers = Qt::NoModifier;
		gesture(navigation, "pan", &button, &modifiers);
		for (CameraViewControls* camera : {&result.navigation.view3D, &result.navigation.orthographic}) {
			camera->panButtons = button == Qt::NoButton ? QVector<Qt::MouseButton>{} : QVector<Qt::MouseButton>{button};
			camera->panModifiers = modifiers;
		}
	}
	if (navigation.contains(QStringLiteral("leftPan"))) {
		Qt::KeyboardModifiers modifiers = result.navigation.view3D.leftPanModifiers;
		modifier(navigation, "leftPan", &modifiers);
		result.navigation.view3D.leftPanModifiers = modifiers;
		result.navigation.orthographic.leftPanModifiers = modifiers;
	}
	gesture(navigation, "zoom", &result.navigation.zoomButton, &result.navigation.zoomModifiers);
	flag(navigation, "invertWheel3D", &result.navigation.invertWheel3D);
	flag(navigation, "orbitLeavesOrthographic", &result.navigation.orbitLeavesOrthographic);
	flag(navigation, "emulateMiddleButton", &result.navigation.emulateMiddleButton);
	if (!result.navigation.orbitLeavesOrthographic) {
		result.navigation.orthographic.orbitButton = Qt::NoButton;
		result.navigation.orthographic.orbitModifiers = Qt::NoModifier;
	}

	const QJsonObject selection = overrides.value(QStringLiteral("selection")).toObject();
	modifier(selection, "extend", &result.selection.extendModifiers);
	flag(selection, "extendToggles", &result.selection.extendToggles);
	modifier(selection, "subtract", &result.selection.subtractModifiers);
	gesture(selection, "subtractDrag", &result.selection.subtractDragButton, &result.selection.subtractDragModifiers);
	flag(selection, "emptyDragBoxSelects", &result.selection.emptyDragBoxSelects);
	modifier(selection, "loop", &result.selection.loopModifiers);
	modifier(selection, "ring", &result.selection.ringModifiers);
	modifier(selection, "path", &result.selection.pathModifiers);
	flag(selection, "doubleClickSelectsLoop", &result.selection.doubleClickSelectsLoop);
	gesture(selection, "lasso", &result.selection.lassoButton, &result.selection.lassoModifiers);
	gesture(selection, "cursor", &result.selection.cursorButton, &result.selection.cursorModifiers);
	flag(selection, "rightClickMenu", &result.selection.rightClickMenu);

	const QJsonObject transform = overrides.value(QStringLiteral("transform")).toObject();
	if (transform.contains(QStringLiteral("style"))) {
		ModelTransformStyle style = result.transform.style;
		if (!modelTransformStyleForId(transform.value(QStringLiteral("style")).toString(), &style)) {
			warn(QCoreApplication::translate("SlopStudioModelControls", "Skipped \"style\": expected \"modal\" or \"tool\"."));
		} else {
			result.transform.style = style;
		}
	}
	flag(transform, "dragSelectionTransforms", &result.transform.dragSelectionTransforms);
	modifier(transform, "duplicateDrag", &result.transform.duplicateDragModifiers);
	modifier(transform, "precision", &result.transform.precisionModifiers);
	modifier(transform, "snap", &result.transform.snapModifiers);

	const QJsonObject layout = overrides.value(QStringLiteral("layout")).toObject();
	if (layout.contains(QStringLiteral("layout"))) {
		ModelViewLayout parsed = result.layout.layout;
		if (!modelViewLayoutForId(layout.value(QStringLiteral("layout")).toString(), &parsed)) {
			warn(QCoreApplication::translate("SlopStudioModelControls", "Skipped \"layout\": expected \"single\" or \"four-views\"."));
		} else {
			result.layout.layout = parsed;
		}
	}
	if (layout.contains(QStringLiteral("panes"))) {
		QVector<ModelPaneView> panes;
		for (const QJsonValue& value : layout.value(QStringLiteral("panes")).toArray()) {
			ModelPaneView pane = ModelPaneView::Perspective;
			if (!modelPaneViewForId(value.toString(), &pane)) {
				panes.clear();
				break;
			}
			panes.append(pane);
		}
		if (panes.size() != 4) {
			warn(QCoreApplication::translate("SlopStudioModelControls", "Skipped \"panes\": expected four of perspective, top, bottom, front, back, left and right."));
		} else {
			result.layout.panes = panes;
		}
	}
	flag(layout, "startInPerspective", &result.layout.startInPerspective);
	flag(layout, "timeline", &result.layout.timelineVisible);
	flag(layout, "toolShelf", &result.layout.toolShelfVisible);

	if (warnings) { *warnings = notes; }
	if (error) { error->clear(); }
	*out = result;
	return true;
}

QJsonObject modelEditorControlOverrides(const ModelEditorControls& base, const ModelEditorControls& edited)
{
	QJsonObject overrides;
	const QJsonObject before = modelEditorControlsJson(base);
	const QJsonObject after = modelEditorControlsJson(edited);
	for (const char* section : {"keys", "navigation", "selection", "transform", "layout"}) {
		const QString key = QString::fromLatin1(section);
		QJsonObject changed = difference(before.value(key).toObject(), after.value(key).toObject());
		changed.remove(QStringLiteral("family"));
		if (!changed.isEmpty()) {
			overrides.insert(key, changed);
		}
	}
	return overrides;
}

QByteArray modelEditorControlsFile(const QString& profileId, const QJsonObject& overrides)
{
	QJsonObject object;
	object.insert(QStringLiteral("format"), QStringLiteral("slopstudio.modeller-controls"));
	object.insert(QStringLiteral("version"), 1);
	object.insert(QStringLiteral("profile"), profileId);
	object.insert(QStringLiteral("overrides"), overrides);
	return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

bool parseModelEditorControlsFile(const QByteArray& bytes, QString* profileId, QJsonObject* overrides, QString* error)
{
	const auto fail = [error](const QString& message) {
		if (error) { *error = message; }
		return false;
	};
	QJsonParseError parse;
	const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
	if (parse.error != QJsonParseError::NoError || !document.isObject()) {
		return fail(QCoreApplication::translate("SlopStudioModelControls", "The controls file is not a JSON object: %1.").arg(parse.errorString()));
	}
	const QJsonObject object = document.object();
	if (!isCurrentOrLegacyStudioName(object.value(QStringLiteral("format")).toString(), QStringLiteral("slopstudio.modeller-controls"))) {
		return fail(QCoreApplication::translate("SlopStudioModelControls", "The file is not a SlopStudio modeller controls file."));
	}
	if (object.value(QStringLiteral("version")).toInt() != 1) {
		return fail(QCoreApplication::translate("SlopStudioModelControls", "The controls file version %1 is not supported.").arg(object.value(QStringLiteral("version")).toInt()));
	}
	const QString id = normalizedModelEditorProfileId(object.value(QStringLiteral("profile")).toString());
	if (id.isEmpty()) {
		return fail(QCoreApplication::translate("SlopStudioModelControls", "The controls file names an unknown profile: %1.").arg(object.value(QStringLiteral("profile")).toString()));
	}
	if (!object.value(QStringLiteral("overrides")).isObject() && object.contains(QStringLiteral("overrides"))) {
		return fail(QCoreApplication::translate("SlopStudioModelControls", "The controls file's overrides must be an object."));
	}
	*profileId = id;
	*overrides = object.value(QStringLiteral("overrides")).toObject();
	if (error) { error->clear(); }
	return true;
}

} // namespace slopstudio
