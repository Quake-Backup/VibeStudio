#include "core/box_resize.h"
#include <algorithm>
#include <cmath>

// Edge and corner handles and the Shift/Alt modifiers follow TrenchBroom's
// Scale tool: ui/ScaleTool moveBBoxSide, moveBBoxEdge, moveBBoxCorner and
// handleLineForHit, and ui/ScaleToolController modifierSettingsForInputState
// at 4c756be1 (GPL-3.0-or-later). Original implementation; see docs/CREDITS.md.
namespace vibestudio {
namespace {
bool finite(BoxResizePoint point)
{
	return std::all_of(point.begin(),point.end(),[](double value) { return std::isfinite(value); });
}
double dot(BoxResizePoint a, BoxResizePoint b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
int movedAxes(const std::array<int,3>& sides) { return int(sides[0] != 0) + int(sides[1] != 0) + int(sides[2] != 0); }
// Where the ray meets the drag plane: for a face handle the coordinate along
// its axis, for an edge or corner handle the distance along its line from
// the handle's point.
bool intersection(const BoxResizeDrag& drag, BoxResizeRay ray, double* coordinate)
{
	if (!coordinate || drag.handle < 0 || drag.handle >= kBoxResizeHandles || !finite(ray.origin) || !finite(ray.direction)) { return false; }
	const double length = std::hypot(ray.direction[0],ray.direction[1],ray.direction[2]);
	if (!std::isfinite(length) || length <= 1e-12) { return false; }
	for (auto& value : ray.direction) { value /= length; }
	const double facing = dot(ray.direction,drag.normal);
	if (std::abs(facing) < 1e-4) { return false; }
	BoxResizePoint relative;
	for (int axis = 0; axis < 3; ++axis) { relative[axis] = drag.anchor[axis] - ray.origin[axis]; }
	const double distance = dot(relative,drag.normal) / facing;
	const double limit = std::max(1.0,std::hypot(relative[0],relative[1],relative[2])) * 64;
	if (!std::isfinite(distance) || std::abs(distance) > limit || (ray.forwardOnly && distance <= 0)) { return false; }
	BoxResizePoint at;
	for (int axis = 0; axis < 3; ++axis) { at[axis] = ray.origin[axis] + ray.direction[axis] * distance; }
	if (!finite(at)) { return false; }
	if (drag.handle < kBoxResizeFaceHandles) { *coordinate = at[drag.handle / 2]; return true; }
	for (int axis = 0; axis < 3; ++axis) { at[axis] -= drag.anchor[axis]; }
	*coordinate = dot(at,drag.direction);
	return std::isfinite(*coordinate);
}
// The point opposite a handle: the other side of each moved axis, the
// centre of the others.
BoxResizePoint oppositePoint(const ResizeBox& box, const std::array<int,3>& sides)
{
	BoxResizePoint point;
	for (int axis = 0; axis < 3; ++axis) {
		point[axis] = sides[axis] > 0 ? box.mins[axis] : sides[axis] < 0 ? box.maxs[axis] : box.mins[axis] + (box.maxs[axis]-box.mins[axis])*0.5;
	}
	return point;
}
}

std::array<int,3> boxResizeHandleSides(int handle)
{
	std::array<int,3> sides{0,0,0};
	if (handle >= 0 && handle < kBoxResizeFaceHandles) {
		sides[static_cast<size_t>(handle/2)] = handle % 2 ? 1 : -1;
	} else if (handle >= kBoxResizeFaceHandles && handle < 18) {
		// Four edges run along each axis; bit 0 picks the next axis' side and
		// bit 1 the one after.
		const int along = (handle-kBoxResizeFaceHandles)/4, bits = (handle-kBoxResizeFaceHandles)%4;
		sides[static_cast<size_t>((along+1)%3)] = bits & 1 ? 1 : -1;
		sides[static_cast<size_t>((along+2)%3)] = bits & 2 ? 1 : -1;
	} else if (handle >= 18 && handle < kBoxResizeHandles) {
		for (int axis = 0; axis < 3; ++axis) { sides[static_cast<size_t>(axis)] = (handle-18) & (1 << axis) ? 1 : -1; }
	}
	return sides;
}

int boxResizeHandleFor(const std::array<int,3>& sides)
{
	const auto side = [&sides](int axis) { return sides[static_cast<size_t>(axis)]; };
	switch (movedAxes(sides)) {
	case 1:
		for (int axis = 0; axis < 3; ++axis) { if (side(axis)) { return axis*2 + (side(axis) > 0 ? 1 : 0); } }
		break;
	case 2:
		for (int along = 0; along < 3; ++along) {
			if (!side(along)) { return kBoxResizeFaceHandles + along*4 + (side((along+1)%3) > 0 ? 1 : 0) + (side((along+2)%3) > 0 ? 2 : 0); }
		}
		break;
	case 3:
		return 18 + (side(0) > 0 ? 1 : 0) + (side(1) > 0 ? 2 : 0) + (side(2) > 0 ? 4 : 0);
	default:
		break;
	}
	return -1;
}

bool validResizeBox(const ResizeBox& box)
{
	if (!finite(box.mins) || !finite(box.maxs)) { return false; }
	for (int axis = 0; axis < 3; ++axis) {
		if (box.mins[axis] > box.maxs[axis] || !std::isfinite(box.maxs[axis]-box.mins[axis])) { return false; }
	}
	return true;
}

bool boxResizeHandle(const ResizeBox& box, int handle, BoxResizePoint* point)
{
	const auto sides = boxResizeHandleSides(handle);
	if (!point || !validResizeBox(box) || !movedAxes(sides)) { return false; }
	for (int axis = 0; axis < 3; ++axis) {
		if (sides[static_cast<size_t>(axis)] && box.maxs[axis]-box.mins[axis] <= 1e-6) { return false; }
	}
	for (int axis = 0; axis < 3; ++axis) {
		const int side = sides[static_cast<size_t>(axis)];
		(*point)[axis] = side > 0 ? box.maxs[axis] : side < 0 ? box.mins[axis] : box.mins[axis] + (box.maxs[axis]-box.mins[axis])*0.5;
	}
	return true;
}

bool beginBoxResize(const ResizeBox& box, int handle, BoxResizePoint viewDirection,
	BoxResizeRay ray, double grid, BoxResizeDrag* drag)
{
	BoxResizeDrag next; next.source = box; next.handle = handle; next.grid = grid;
	if (!drag || !finite(viewDirection) || !std::isfinite(grid) || grid < 0 || grid > 1e6
		|| (grid > 0 && grid < 1e-6) || !boxResizeHandle(box,handle,&next.anchor)) { return false; }
	// The handle's line: a face's outward axis, or from the opposite edge or
	// corner through this one, as TrenchBroom drags them.
	const auto sides = boxResizeHandleSides(handle);
	const auto opposite = oppositePoint(box,sides);
	for (int axis = 0; axis < 3; ++axis) { next.direction[axis] = next.anchor[axis] - opposite[axis]; }
	const double span = std::hypot(next.direction[0],next.direction[1],next.direction[2]);
	if (!std::isfinite(span) || span <= 1e-9) { return false; }
	for (auto& value : next.direction) { value /= span; }
	// The plane through the line that faces the camera most.
	const double along = dot(viewDirection,next.direction);
	for (int axis = 0; axis < 3; ++axis) { next.normal[axis] = viewDirection[axis] - along*next.direction[axis]; }
	const double length = std::hypot(next.normal[0],next.normal[1],next.normal[2]);
	const double viewLength = std::hypot(viewDirection[0],viewDirection[1],viewDirection[2]);
	if (!std::isfinite(viewLength) || viewLength <= 0 || length / viewLength < 0.05) { return false; }
	for (auto& value : next.normal) { value /= length; }
	if (!intersection(next,ray,&next.pressCoordinate)) { return false; }
	*drag = next; return true;
}

bool updateBoxResize(const BoxResizeDrag& drag, BoxResizeRay ray, ResizeBox* box)
{
	return updateBoxResize(drag,ray,BoxResizeModifiers{},box);
}

bool updateBoxResize(const BoxResizeDrag& drag, BoxResizeRay ray, const BoxResizeModifiers& modifiers, ResizeBox* box)
{
	double coordinate = 0;
	if (!box || !validResizeBox(drag.source) || !intersection(drag,ray,&coordinate)) { return false; }
	double travel = coordinate - drag.pressCoordinate;
	// A face's travel is along its world axis; outward is its side.
	if (drag.handle < kBoxResizeFaceHandles && drag.handle % 2 == 0) { travel = -travel; }
	return resizeBoxAlongHandle(drag.source,drag.handle,travel,drag.grid,modifiers,box);
}

bool resizeBoxAlongHandle(const ResizeBox& source, int handle, double travel, double grid, const BoxResizeModifiers& modifiers,
	ResizeBox* box)
{
	const auto sides = boxResizeHandleSides(handle);
	BoxResizePoint point;
	if (!box || !std::isfinite(travel) || !std::isfinite(grid) || grid < 0 || !boxResizeHandle(source,handle,&point)) { return false; }
	if (travel == 0) { *box = source; return true; }
	BoxResizePoint size, centre, smallest;
	for (int axis = 0; axis < 3; ++axis) {
		size[axis] = source.maxs[axis]-source.mins[axis];
		centre[axis] = source.mins[axis] + size[axis]*0.5;
		// A box already thinner than the grid may keep its size.
		smallest[axis] = std::min(grid > 0 ? grid : 1.0,size[axis]);
	}
	auto next = source;
	double ratio = 1;
	int moving = 0;
	if (handle < kBoxResizeFaceHandles) {
		const int axis = handle/2, side = sides[static_cast<size_t>(axis)];
		moving = 1 << axis;
		double target = point[axis] + side*travel;
		if (grid > 0) { target = std::round(target / grid) * grid; }
		if (!std::isfinite(target)) { return false; }
		// The face stops short of what stays put: the opposite face, or the
		// centre when both sides move.
		const double anchor = modifiers.centre ? centre[axis] : side > 0 ? source.mins[axis] : source.maxs[axis];
		const double least = modifiers.centre ? smallest[axis]*0.5 : smallest[axis];
		const double moved = side*(target-anchor) < least ? anchor + side*least : target;
		if (side > 0) { next.maxs[axis] = moved; } else { next.mins[axis] = moved; }
		if (modifiers.centre) {
			if (side > 0) { next.mins[axis] = 2*centre[axis] - moved; } else { next.maxs[axis] = 2*centre[axis] - moved; }
		}
		ratio = size[axis] > 0 ? (next.maxs[axis]-next.mins[axis]) / size[axis] : 1;
	} else {
		// TrenchBroom's edge and corner handles slide along the line from the
		// opposite edge or corner, so the axes they move keep their ratio.
		const auto opposite = oppositePoint(source,sides);
		BoxResizePoint line;
		for (int axis = 0; axis < 3; ++axis) { line[axis] = point[axis]-opposite[axis]; }
		const double span = std::hypot(line[0],line[1],line[2]);
		if (!std::isfinite(span) || span <= 1e-9) { return false; }
		for (auto& value : line) { value /= span; }
		if (grid > 0) {
			// The handle lands where its line crosses the nearest grid plane.
			double best = travel, nearest = -1;
			for (int axis = 0; axis < 3; ++axis) {
				if (!sides[static_cast<size_t>(axis)] || std::abs(line[axis]) < 1e-9) { continue; }
				const double at = point[axis] + line[axis]*travel;
				const double candidate = (std::round(at / grid) * grid - point[axis]) / line[axis];
				if (std::isfinite(candidate) && (nearest < 0 || std::abs(candidate-travel) < nearest)) { nearest = std::abs(candidate-travel); best = candidate; }
			}
			travel = best;
		}
		const double reach = modifiers.centre ? span*0.5 : span;
		ratio = (reach + travel) / reach;
		double least = 0;
		for (int axis = 0; axis < 3; ++axis) {
			if (sides[static_cast<size_t>(axis)]) { moving |= 1 << axis; least = std::max(least,smallest[axis]/size[axis]); }
		}
		ratio = std::max(ratio,least);
		if (!std::isfinite(ratio)) { return false; }
		for (int axis = 0; axis < 3; ++axis) {
			const int side = sides[static_cast<size_t>(axis)];
			if (!side) { continue; }
			if (modifiers.centre) {
				next.mins[axis] = centre[axis] - size[axis]*ratio*0.5; next.maxs[axis] = centre[axis] + size[axis]*ratio*0.5;
			} else if (side > 0) {
				next.maxs[axis] = source.mins[axis] + size[axis]*ratio;
			} else {
				next.mins[axis] = source.maxs[axis] - size[axis]*ratio;
			}
		}
	}
	// Shift: the other axes follow the same ratio about the centre.
	for (int axis = 0; axis < 3; ++axis) {
		if ((moving & (1 << axis)) || !(modifiers.proportionalAxes & (1 << axis)) || size[axis] <= 0) { continue; }
		next.mins[axis] = centre[axis] - size[axis]*ratio*0.5; next.maxs[axis] = centre[axis] + size[axis]*ratio*0.5;
	}
	if (!validResizeBox(next)) { return false; }
	*box = next; return true;
}

BoxResizePoint resizeBoxPoint(const ResizeBox& from, const ResizeBox& to, BoxResizePoint point)
{
	for (int axis = 0; axis < 3; ++axis) {
		const double extent = from.maxs[axis]-from.mins[axis];
		point[axis] = to.mins[axis] + (point[axis]-from.mins[axis]) * (extent > 1e-6 ? (to.maxs[axis]-to.mins[axis])/extent : 1);
	}
	return point;
}

} // namespace vibestudio
