// Model and level surfaces. The opaque pass keeps fully opaque fragments
// and writes the colour, the triangle under each pixel and its depth. The
// translucent passes peel the remaining fragments front to back, one layer
// per pass, so crossing translucent faces composite in depth order.
//
// Colours are premultiplied. Hatching, selected edges, edges and the hovered
// outline follow the studio's long-standing rules: widths in logical pixels,
// patterns along screen diagonals. The optional face grid and distance fog
// (the Levels camera's Grid on Camera Faces and Fog in Camera) come first,
// so the selection and edges are drawn over them unfaded.

#include "common.glsl"

VIBE_UNIFORMS {
	mat4 viewProjection;
	vec4 cameraEye;
	vec4 light;
	vec4 hatchColor;
	vec4 edgeColor;
	vec4 hoverColor;
	vec4 selectionColor;
	vec4 highlightColor;
	vec4 params;
	ivec4 iparams;
	vec4 view;
	vec4 fogColor;
	vec4 gridParams;
};

// The surface's skin, premultiplied.
VIBE_SAMPLER(0) tex0;
// Translucent passes: the opaque pass's depth, and the previous layer's.
VIBE_SAMPLER(1) tex1;
VIBE_SAMPLER(2) tex2;
// The modeller's Game preview: an unlit layer added after shading (Quake
// fullbrights, Quake III identity stages); read when params.w is 1.
VIBE_SAMPLER(3) tex3;

VIBE_VARYING_IN(0) vec2 vUv;
VIBE_VARYING_IN(1) vec3 vBarycentricW;
VIBE_FLAT_IN(2) vec4 vShade;
VIBE_FLAT_IN(3) int vTriangle;
VIBE_FLAT_IN(4) uint vFlags;
VIBE_VARYING_IN(5) vec3 vWorld;

VIBE_FRAGMENT_OUT(0) vec4 outColor;
VIBE_FRAGMENT_OUT(1) int outTriangle;
VIBE_FRAGMENT_OUT(2) float outDepth;

// Mixes an overlay colour (straight, its alpha the amount) into a
// premultiplied colour without changing its coverage.
vec4 tint(vec4 base, vec4 overlay)
{
	return vec4(base.rgb * (1.0 - overlay.a) + overlay.rgb * base.a * overlay.a, base.a);
}

void main()
{
	// Distances to the three edges, in pixels, from screen-linear
	// barycentrics. Derivatives come first, before any fragment is dropped.
	vec3 barycentric = vBarycentricW * gl_FragCoord.w;
	vec3 gradient = vec3(length(vec2(dFdx(barycentric.x), dFdy(barycentric.x))),
		length(vec2(dFdx(barycentric.y), dFdy(barycentric.y))),
		length(vec2(dFdx(barycentric.z), dFdy(barycentric.z))));
	vec3 edgeDistance = barycentric / max(gradient, vec3(1.0e-20));
	// The face grid lies in the plane of the two world axes the face is most
	// nearly square to, measured in grid cells; how many cells a pixel spans
	// gives the distance to the nearest line in pixels.
	uint axis = (vFlags >> 9u) & 3u;
	vec2 gridPoint = axis == 0u ? vWorld.yz : (axis == 1u ? vWorld.xz : vWorld.xy);
	vec2 gridCell = gridPoint / max(view.z, 1.0e-6);
	vec2 cellsPerPixel = vec2(length(vec2(dFdx(gridCell.x), dFdy(gridCell.x))), length(vec2(dFdx(gridCell.y), dFdy(gridCell.y))));

	vec4 color;
	if ((vFlags & 32u) != 0u) {
		vec4 texel = texture(tex0, vUv);
		if (floor(texel.a * 255.0 + 0.5) <= 0.0) {
			discard;
		}
		color = vec4(floor(texel.rgb * 255.0 * vShade.r + 0.5) / 255.0, texel.a);
		if (params.w > 0.5) {
			vec3 glow = texture(tex3, vUv).rgb;
			color.rgb = min(color.rgb + glow * texel.a, vec3(texel.a));
		}
	} else {
		color = vShade;
	}

	float alpha = floor(color.a * 255.0 + 0.5);
	int pass = iparams.w;
	if (pass == 0) {
		if (alpha < 255.0) {
			discard;
		}
	} else {
		if (alpha >= 255.0) {
			discard;
		}
		ivec2 pixel = ivec2(gl_FragCoord.xy);
		if (gl_FragCoord.z <= texelFetch(tex1, pixel, 0).r) {
			discard;
		}
		if (pass == 2 && gl_FragCoord.z >= texelFetch(tex2, pixel, 0).r) {
			discard;
		}
	}

	float ratio = params.x;
	if ((vFlags & 2048u) != 0u && view.z > 0.0) {
		vec2 pixels = abs(fract(gridCell + 0.5) - 0.5) / max(cellsPerPixel, vec2(1.0e-6));
		vec2 coverage = clamp(vec2(0.5 * gridParams.y * ratio + 0.5) - pixels, 0.0, 1.0);
		// Lines fade as they come closer than eight pixels apart and are gone
		// at four, so a distant or glancing face reads as a face, not as grid.
		vec2 spacing = 1.0 / max(cellsPerPixel, vec2(1.0e-6));
		vec2 fade = clamp((spacing - 4.0 * ratio) / (4.0 * ratio), 0.0, 1.0);
		float line = max(coverage.x * fade.x, coverage.y * fade.y) * gridParams.x;
		if (line > 0.0) {
			// Dark lines on light faces, light lines on dark ones.
			vec3 straight = color.rgb / max(color.a, 1.0 / 255.0);
			float luma = dot(straight, vec3(0.299, 0.587, 0.114));
			color = tint(color, vec4(vec3(luma > 0.55 ? 0.0 : 1.0), line));
		}
	}
	// Fog fades the face, grid included; the selection and edges stay crisp.
	// Perspective views count from the eye, orthographic ones along the view
	// from the front of the geometry.
	if (fogColor.a > 0.0) {
		float eyeDistance = cameraEye.w > 0.5 ? length(vWorld - cameraEye.xyz) : gridParams.z - dot(vWorld, cameraEye.xyz);
		float ramp = clamp((eyeDistance - view.x) / max(view.y - view.x, 1.0e-3), 0.0, 1.0);
		color = tint(color, vec4(fogColor.rgb, fogColor.a * ramp));
	}
	float diagonal = (gl_FragCoord.x + gl_FragCoord.y) / ratio;
	if ((vFlags & 1u) != 0u && mod(diagonal, 8.0) < 1.2) {
		color = tint(color, hatchColor);
	}
	bool marked = false;
	for (int edge = 0; edge < 3; ++edge) {
		if ((vFlags & (4u << uint(edge))) != 0u && edgeDistance[edge] <= 2.8 * ratio && mod(diagonal, 9.0) < 6.0) {
			color = tint(color, selectionColor);
			marked = true;
			break;
		}
	}
	bool hovered = (vFlags & 2u) != 0u;
	if (!marked && (params.y > 0.5 || hovered)) {
		float width = (hovered ? 1.8 : 0.9) * ratio;
		for (int edge = 0; edge < 3; ++edge) {
			// Polygon diagonals keep the plain outline off; the hovered
			// triangle is still outlined on all three sides.
			if (!hovered && (vFlags & (64u << uint(edge))) != 0u) {
				continue;
			}
			if (edgeDistance[edge] <= width) {
				color = tint(color, hovered ? hoverColor : edgeColor);
				break;
			}
		}
	}
	outColor = color;
	outTriangle = vTriangle;
	outDepth = gl_FragCoord.z;
}
