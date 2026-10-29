#pragma once

#include "tests/model_uv_test_helpers.h"

namespace vibestudio::tests
{
// A planar quad grid in the XY plane, `columns` 10-unit quads wide and one
// high, with two poses (the second lifted in Z) and UVs u = x / width.
inline ModelMesh strip(int columns)
{
	auto mesh = uvSquare();
	mesh.tags.clear();
	auto &s = mesh.surfaces[0];
	s.texCoords.clear();
	s.triangles.clear();
	for (auto &frame : s.frames)
	{
		frame.positions.clear();
		frame.normals.clear();
	}
	for (int row = 0; row <= 1; ++row)
		for (int x = 0; x <= columns; ++x)
		{
			s.texCoords.append({float(x) / columns, float(row)});
			for (int f = 0; f < s.frames.size(); ++f)
			{
				s.frames[f].positions.append({float(x * 10), float(row * 10), float(f * 8)});
				s.frames[f].normals.append({0, 0, 1});
			}
		}
	for (int x = 0; x < columns; ++x)
	{
		const int a = x, b = x + 1, c = columns + 2 + x, d = columns + 1 + x;
		s.triangles.append({a, b, c});
		s.triangles.append({a, c, d});
	}
	s.vertexCount = s.texCoords.size();
	s.skinPaths = {QStringLiteral("models/health/skin.pcx")};
	// Ten texels per unit in both directions, so the layout is an isometry.
	mesh.md2SkinSize = {10 * columns, 10};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
// Two separate one-quad islands; `mirror` reflects the second in U and
// `stack` places it over the first.
inline ModelMesh twoIslands(bool mirror, bool stack)
{
	auto mesh = strip(1);
	auto &s = mesh.surfaces[0];
	const auto base = s;
	for (int v = 0; v < base.vertexCount; ++v)
	{
		auto uv = base.texCoords[v];
		uv.u = mirror ? 1 - uv.u : uv.u;
		uv.u = stack ? uv.u : uv.u * .5f + .5f;
		s.texCoords.append(uv);
		for (int f = 0; f < s.frames.size(); ++f)
		{
			auto p = base.frames[f].positions[v];
			p.x += 30;
			s.frames[f].positions.append(p);
			s.frames[f].normals.append(base.frames[f].normals[v]);
		}
	}
	if (!stack)
		for (int v = 0; v < base.vertexCount; ++v)
			s.texCoords[v].u *= .5f;
	for (const auto &t : base.triangles)
		s.triangles.append({t.a + base.vertexCount, t.b + base.vertexCount, t.c + base.vertexCount});
	s.vertexCount = s.texCoords.size();
	updateEditableModelMetadata(&mesh);
	return mesh;
}
// The folded strip: the last quad of strip(3) folds back over its neighbour,
// so faces 4 and 5 are flipped and faces 2 to 5 overlap.
inline ModelMesh foldedStrip()
{
	auto mesh = strip(3);
	mesh.surfaces[0].texCoords[3].u = mesh.surfaces[0].texCoords[7].u = .5f;
	return mesh;
}
} // namespace vibestudio::tests
