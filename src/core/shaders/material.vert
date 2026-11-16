// Material preview shapes: world-space vertices with the frame each engine
// shades with, and per-stage coordinates and colours worked out on the CPU
// the way the engines compute them per vertex. Every pass of a frame draws
// the same positions through this program; `invariant` keeps their depth
// identical so equal-depth passes line up exactly.

#include "material_common.glsl"

SLOP_ATTRIBUTE(0) vec3 aPosition;
SLOP_ATTRIBUTE(1) vec3 aNormal;
SLOP_ATTRIBUTE(2) vec3 aTangent;
SLOP_ATTRIBUTE(3) vec3 aBitangent;
// xy: the stage's texture coordinates; zw: lightmap coordinates.
SLOP_ATTRIBUTE(4) vec4 aTexCoord;
SLOP_ATTRIBUTE(5) vec4 aColor;

SLOP_VARYING_OUT(0) vec3 vPosition;
SLOP_VARYING_OUT(1) vec3 vNormal;
SLOP_VARYING_OUT(2) vec3 vTangent;
SLOP_VARYING_OUT(3) vec3 vBitangent;
SLOP_VARYING_OUT(4) vec4 vTexCoord;
SLOP_VARYING_OUT(5) vec4 vColor;
// Distance along the view direction, as the engines' depth.
SLOP_VARYING_OUT(6) float vDepth;

invariant gl_Position;

void main()
{
	vPosition = aPosition;
	vNormal = aNormal;
	vTangent = aTangent;
	vBitangent = aBitangent;
	vTexCoord = aTexCoord;
	vColor = aColor;
	vDepth = dot(aPosition - eye.xyz, forward.xyz);
	gl_Position = SLOP_CLIP(viewProjection * vec4(aPosition, 1.0));
}
