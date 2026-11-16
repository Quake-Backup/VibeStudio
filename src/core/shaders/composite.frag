// Copies a target of the same size pixel for pixel, scaled, offset and
// clamped: compositing translucent layers, and display scaling.

#include "common.glsl"

SLOP_UNIFORMS {
	vec4 multiply;
	vec4 add;
};

SLOP_SAMPLER(0) tex0;

SLOP_VARYING_IN(0) vec2 vUv;

SLOP_FRAGMENT_OUT(0) vec4 outColor;

void main()
{
	vec4 color = texelFetch(tex0, ivec2(gl_FragCoord.xy), 0);
	outColor = clamp(color * multiply + add, 0.0, 1.0);
}
