#pragma once

#include <array>

namespace vibestudio {

using BoxResizePoint = std::array<double,3>;
struct ResizeBox {
	BoxResizePoint mins{}, maxs{};
	bool operator==(const ResizeBox&) const = default;
};
struct BoxResizeRay {
	BoxResizePoint origin{}, direction{};
	bool forwardOnly = false;
};
// TrenchBroom's Scale tool modifiers (ui/ScaleToolController): `centre` (Alt)
// scales about the box's centre instead of the opposite side, and the axes in
// `proportionalAxes` (Shift; bit 0 X, 1 Y, 2 Z) keep their proportion to the
// size a face or edge handle sets, about the box's centre.
struct BoxResizeModifiers {
	bool centre = false;
	int proportionalAxes = 0;
	bool operator==(const BoxResizeModifiers&) const = default;
};
struct BoxResizeDrag {
	ResizeBox source;
	BoxResizePoint anchor{}, normal{};
	// The unit line the handle slides along: a face's outward normal, or from
	// the opposite edge or corner through an edge or corner handle.
	BoxResizePoint direction{};
	double pressCoordinate = 0, grid = 0;
	int handle = -1;
};

// Handles 0 to 5 are the six face centres: X min/max, Y min/max, Z min/max.
// TrenchBroom's Scale tool adds 6 to 17, the twelve edge midpoints (four along
// each axis), and 18 to 25, the eight corners.
inline constexpr int kBoxResizeFaceHandles = 6;
inline constexpr int kBoxResizeHandles = 26;
// The side of each axis a handle moves: -1 its minimum, +1 its maximum, 0 none.
// All zero for an unknown handle.
std::array<int,3> boxResizeHandleSides(int handle);
// The handle that moves these sides; -1 when none moves.
int boxResizeHandleFor(const std::array<int,3>& sides);

// Collapsed axes have no handle. Bounds and unchanged coordinates retain
// double source precision.
bool validResizeBox(const ResizeBox& box);
// A handle's point: its moved axes at the box's sides, the others at the
// centre. False when a moved axis has no size.
bool boxResizeHandle(const ResizeBox& box, int handle, BoxResizePoint* point);
// A camera-facing plane contains the handle's line. Degenerate/grazing or
// backward rays fail without changing output. Grid zero disables snapping.
bool beginBoxResize(const ResizeBox& box, int handle, BoxResizePoint viewDirection,
	BoxResizeRay ray, double grid, BoxResizeDrag* drag);
bool updateBoxResize(const BoxResizeDrag& drag, BoxResizeRay ray, ResizeBox* box);
bool updateBoxResize(const BoxResizeDrag& drag, BoxResizeRay ray, const BoxResizeModifiers& modifiers, ResizeBox* box);
// The box after dragging `handle` `travel` units outward along its line
// (core of the plan and camera resize drags). A face lands on the absolute
// grid and stops short of the opposite face (or the centre) at the smaller of
// the grid (1 unit without one) and its size. An edge or corner handle scales
// the axes it moves by one ratio, as TrenchBroom's diagonal handle lines do;
// its point lands where the line crosses a grid plane. No travel returns the
// source exactly.
bool resizeBoxAlongHandle(const ResizeBox& source, int handle, double travel, double grid, const BoxResizeModifiers& modifiers,
	ResizeBox* box);
// Affine geometry preview; point-owned visuals use the transformed origin's
// translation instead, preserving the size of entity models and thing markers.
BoxResizePoint resizeBoxPoint(const ResizeBox& from, const ResizeBox& to, BoxResizePoint point);

} // namespace vibestudio
