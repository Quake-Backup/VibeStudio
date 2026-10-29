#include "core/box_resize.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << "FAIL: " << message << '\n'; }
	return value;
}
BoxResizeRay rayAt(BoxResizePoint point, int axis, bool perspective)
{
	BoxResizeRay ray; ray.forwardOnly = perspective;
	for (int i = 0; i < 3; ++i) { ray.direction[i] = i == axis ? 0 : -1; ray.origin[i] = point[i] - ray.direction[i]*100; }
	return ray;
}
}
int main()
{
	bool ok = true;
	const ResizeBox source{{-4.5,-8.25,0.125},{12.5,23.75,64.125}};
	for (bool perspective : {false,true}) { for (double grid : {0.0,1.0,16.0}) { for (int handle = 0; handle < 6; ++handle) {
		BoxResizePoint point; ok &= boxResizeHandle(source,handle,&point);
		const int axis = handle/2; const bool maximum = handle%2;
		const auto press = rayAt(point,axis,perspective); BoxResizeDrag drag;
		ok &= expect(beginBoxResize(source,handle,{1,1,1},press,grid,&drag),"all six axes begin in perspective and orthographic views");
		ResizeBox result;
		ok &= expect(updateBoxResize(drag,press,&result) && result == source,"a press without travel never snaps an off-grid box");
		point[axis] += maximum ? 23.75 : -23.75;
		ok &= expect(updateBoxResize(drag,rayAt(point,axis,perspective),&result),"outward resize succeeds");
		const double target = grid ? std::round(point[axis]/grid)*grid : point[axis];
		ok &= expect((maximum ? result.maxs[axis] : result.mins[axis]) == target,"moved face snaps to absolute map grid");
		for (int i = 0; i < 3; ++i) {
			if (i != axis) { ok &= expect(result.mins[i] == source.mins[i] && result.maxs[i] == source.maxs[i],"unaffected coordinates retain exact source precision"); }
		}
		ok &= expect((maximum ? result.mins[axis] : result.maxs[axis]) == (maximum ? source.mins[axis] : source.maxs[axis]),"opposite face remains fixed");
		point[axis] += maximum ? -10000 : 10000;
		ok &= expect(updateBoxResize(drag,rayAt(point,axis,perspective),&result)
			&& std::abs(result.maxs[axis]-result.mins[axis]-std::min(grid ? grid : 1.0,source.maxs[axis]-source.mins[axis])) < 1e-9,
			"crossing the opposite face clamps to the same minimum span as plan resizing");
		auto invalid = press; invalid.direction = {0,0,0}; const auto kept = result;
		ok &= expect(!updateBoxResize(drag,invalid,&result) && result == kept,"invalid ray leaves last valid result untouched");
	} } }
	BoxResizePoint point; BoxResizeDrag drag;
	ok &= boxResizeHandle(source,1,&point);
	const auto press = rayAt(point,0,true);
	ok &= expect(!beginBoxResize(source,1,{1,0,0},press,0,&drag),"axis facing straight at the camera has no honest drag plane");
	auto behind = press; behind.direction = {0,1,1};
	ok &= expect(!beginBoxResize(source,1,{1,1,1},behind,0,&drag),"perspective intersections behind the eye are refused");
	const double nan = std::numeric_limits<double>::quiet_NaN();
	ok &= expect(!beginBoxResize(source,-1,{1,1,1},press,0,&drag)
		&& !beginBoxResize(source,1,{1,1,1},press,nan,&drag)
		&& !beginBoxResize(source,1,{nan,1,1},press,0,&drag),"invalid handles, grids and directions are refused");
	const ResizeBox thin{{0,0,0},{0.125,4,0}};
	ok &= expect(!boxResizeHandle(thin,5,&point),"collapsed source axes do not advertise a resize handle");
	ok &= boxResizeHandle(thin,1,&point);
	ok &= beginBoxResize(thin,1,{1,1,1},rayAt(point,0,true),16,&drag);
	point[0] = -100;
	ResizeBox result;
	ok &= expect(updateBoxResize(drag,rayAt(point,0,true),&result) && result == thin,"sub-grid source thickness is preserved when clamping");
	const ResizeBox to{{-4.5,-8.25,0.125},{29.5,23.75,64.125}};
	const auto moved = resizeBoxPoint(source,to,{4,7.75,32.125});
	ok &= expect(moved == BoxResizePoint{12.5,7.75,32.125},"preview maps source points by the same affine bounds transform as committed resizing");

	// TrenchBroom's Scale tool: edge and corner handles, Shift and Alt.
	for (int handle = -1; handle <= kBoxResizeHandles; ++handle) {
		const auto sides = boxResizeHandleSides(handle);
		const int moved = int(sides[0] != 0) + int(sides[1] != 0) + int(sides[2] != 0);
		const int expected = handle < 0 || handle >= kBoxResizeHandles ? 0 : handle < 6 ? 1 : handle < 18 ? 2 : 3;
		ok &= expect(moved == expected && (moved == 0 ? boxResizeHandleFor(sides) == -1 : boxResizeHandleFor(sides) == handle),
			"each handle's sides name it back: faces move one axis, edges two, corners three");
	}
	const ResizeBox cube{{0,0,0},{32,32,32}};
	const auto handleFor = [](int x, int y, int z) { return boxResizeHandleFor({x,y,z}); };
	ok &= expect(boxResizeHandle(cube,handleFor(1,1,0),&point) && point == BoxResizePoint{32,32,16},"an edge handle sits at its edge's middle");
	ok &= expect(boxResizeHandle(cube,handleFor(-1,1,-1),&point) && point == BoxResizePoint{0,32,0},"a corner handle sits at its corner");
	ResizeBox out;
	ok &= expect(resizeBoxAlongHandle(cube,1,16,16,{true,0},&out) && out == ResizeBox{{-16,0,0},{48,32,32}},"Alt: the opposite face mirrors about the centre");
	const ResizeBox slab{{0,0,0},{32,16,64}};
	ok &= expect(resizeBoxAlongHandle(slab,1,32,0,{false,2},&out) && out == ResizeBox{{0,-8,0},{64,24,64}},
		"Shift: a proportional axis follows the face's ratio about the centre; the others stay");
	const double diagonal = std::hypot(32.0,32.0);
	ok &= expect(resizeBoxAlongHandle(cube,handleFor(1,1,0),diagonal,0,{},&out) && out == ResizeBox{{0,0,0},{64,64,32}},
		"an edge handle slides along the diagonal from the opposite edge, doubling both axes");
	ok &= expect(resizeBoxAlongHandle(cube,handleFor(1,1,0),diagonal*0.9,16,{},&out) && std::abs(out.maxs[0]-64) < 1e-9 && std::abs(out.maxs[1]-64) < 1e-9,
		"the edge handle lands where its line crosses a grid plane");
	ok &= expect(resizeBoxAlongHandle(cube,handleFor(1,1,0),diagonal,0,{false,4},&out) && out == ResizeBox{{0,0,-16},{64,64,48}},
		"Shift scales an edge's third axis by the same ratio");
	ok &= expect(resizeBoxAlongHandle(cube,handleFor(-1,-1,-1),std::hypot(16.0,16.0,16.0),0,{true,0},&out)
			&& std::abs(out.mins[0]+16) < 1e-9 && std::abs(out.maxs[2]-48) < 1e-9,
		"a corner with Alt grows about the centre");
	ok &= expect(resizeBoxAlongHandle(cube,handleFor(1,1,1),-1000,16,{},&out) && std::abs(out.maxs[0]-16) < 1e-9 && std::abs(out.maxs[1]-16) < 1e-9
			&& out.mins == cube.mins,
		"a corner stops at the smallest size, keeping the box's shape");
	ok &= expect(resizeBoxAlongHandle(cube,handleFor(1,1,0),0,16,{true,7},&out) && out == cube,"no travel changes nothing, whatever the keys");
	// A drag on an edge handle from straight above: the plane through its
	// line faces the camera, and the pointer's travel along the diagonal scales.
	const auto above = [](double x, double y) { BoxResizeRay ray; ray.origin = {x,y,100}; ray.direction = {0,0,-1}; return ray; };
	BoxResizeDrag edge;
	ok &= expect(beginBoxResize(cube,handleFor(1,1,0),{0,0,-1},above(32,32),0,&edge),"an edge drag begins from above");
	ok &= expect(updateBoxResize(edge,above(48,48),&out) && std::abs(out.maxs[0]-48) < 1e-9 && std::abs(out.maxs[1]-48) < 1e-9 && out.maxs[2] == 32,
		"the edge follows the pointer along its diagonal");
	ok &= expect(updateBoxResize(edge,above(48,48),{true,4},&out) && std::abs(out.mins[0]+16) < 1e-9 && std::abs(out.maxs[1]-48) < 1e-9
			&& std::abs(out.mins[2]+16) < 1e-9 && std::abs(out.maxs[2]-48) < 1e-9,
		"Alt and Shift change the same drag: about the centre, with Z in proportion");
	BoxResizeDrag corner;
	ok &= expect(!beginBoxResize(cube,handleFor(1,1,1),{1,1,1},above(32,32),0,&corner),"a corner line pointing at the camera has no honest drag plane");
	std::cout << "36 axis/scale/projection cases, invalid-ray/thin-box checks and edge/corner/modifier checks completed.\n";
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
