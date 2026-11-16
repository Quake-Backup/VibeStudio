# Baking Texture Maps

Status: Partial. **UV** > **Bake Maps…** in the Mesh Editor and `model bake`
bake ambient occlusion, normal and lighting maps into a model's UV layout, and
**Apply to Model** (or `--model-output`) hands occlusion or lighting to the
skin. They are checked against fixtures with known answers, not yet against
renderbump or other bakers on real models, or in the games.

The baker is `src/core/model_bake.*` with its own bounding volume hierarchy
(`src/core/model_bvh.*`); the dialog is `src/app/model_bake_dialog.*` and the
command `src/cli/model_bake.*`. All of it is original SlopStudio code.

## Maps

| Map | What it holds | Background |
| --- | --- | --- |
| **Ambient occlusion** | How open each texel is: cosine-weighted rays over the hemisphere around its normal, counted as blocked when they hit the model within the occlusion distance. White is open. | White |
| **Tangent-space normals (Doom 3)** | The normal in each triangle's tangent frame: red along +s, green along +t (down the image) and blue out of the surface, along the s and t gradients Doom 3 derives tangents from (`R_DeriveTangents`) and as it reads its bump maps. | (128, 128, 255) |
| **Object-space normals** | The normal in model axes, as red, green and blue from -1 to 1. | Mid grey |
| **Lighting** | An ambient level plus a directional light's Lambert term, with the model's own shadow. | The ambient level |

With a **high-poly** model, normals (and occlusion) come from it: each texel
casts a ray from just outside the low-poly surface (the cage distance along its
normal) back along the normal and takes the nearest high-poly normal, as Doom 3's
`renderbump` projects detail onto a game mesh. A texel whose ray finds nothing
keeps the low-poly normal and is counted as a miss.

## Settings

- **Surfaces**: the active surface, or every surface sharing the UV space.
- **Size**: 64 to 2048 texels square in the dialog; any 8-4096 by 8-4096 from
  the command line.
- **Rays**: occlusion rays a texel (1-1024); more is smoother and slower.
- **Distance**: how far occluders count; automatic is a quarter of the model's
  bounding radius.
- **Cage**: how far outside the low-poly surface high-poly rays start;
  automatic is 5% of the bounding radius.
- **Padding**: texels bled past island edges so filtering and mip maps do not
  pull the background into seams.
- **Seed**: each texel's rays come from the seed and the texel's position, so
  the same settings always give the same map, whatever order texels are baked
  in.

Only the part of each triangle inside the 0-1 UV square is baked. A texel
centre on an edge two triangles share belongs to one of them; a texel truly
covered by two triangles takes the first, and the result notes both. A bake
that would use more than 768 MiB is refused before it starts.

Occlusion and shadow rays leave the surface from just above the face itself.
Where a smoothed normal leans away from its face (low-poly curves), the part
of its hemisphere below the face is mirrored back above it, and a light the
smoothed normal faces but the face turns from is tested along the face, so a
convex model never shadows or occludes itself.

## Using it

1. Choose **UV** > **Bake Maps…**.
2. Pick the map, the surfaces and the size; for normals from a detailed model,
   choose its file under **High-poly**.
3. Choose **Bake**. The bake runs in the background with progress; **Cancel
   Bake** stops it and keeps the previous result.
4. Choose **Save Map…** to write a PNG or TGA, or **Apply to Model** to hand
   the map over (its tooltip says which of these it does):
   - an MDL skin is shaded by an occlusion or lighting map, as one undo step:
     each texel's colour times the map, blended by **Shade strength**, then
     the nearest palette entry below the fullbrights (indices 224 and up keep
     their own, so they stay lit);
   - a skin image from the open package is shaded the same way (an indexed
     PCX keeps its palette) and staged at its own path; a normal map is staged
     beside it instead, as `<name>_local.tga` for Doom 3's tangent-space bump
     map (`_objnormal` for object space).

   The dialog stays open while you edit. **Apply to Model** shades the skin
   as it is when you apply, so painting applied in the meantime is kept. A
   map fits the faces, UVs and pose it was baked from: when an edit has
   changed those since the dialog opened, **Apply to Model** refuses and
   says so; undo the edit or bake again.

## Command line

```sh
slopstudio --cli model bake ./models/crate.mesh.json --type ao --size 512x512 --samples 128 --output ./out/crate_ao.png --json
slopstudio --cli model bake ./models/low.obj --type normal --high ./models/high.obj --cage 2 --output ./out/low_local.tga --json
```

`--type` is `ao`, `normal` (tangent space), `object-normal` or `lighting`.
`--surface a,b`, `--frame`, `--size WxH`, `--samples`, `--distance`, `--cage`,
`--padding`, `--seed` and `--ambient` match the dialog. `--dry-run` bakes and
reports without writing. JSON gives the texels covered, high-poly misses, the
distances used and a SHA-256 of the image, so scripts can compare runs.
`--model-output FILE.mesh.json` shades the first baked surface's MDL skin with
an `ao` or `lighting` map (`--strength 0-1`, default 1) and writes the model;
it is refused, before anything is written, for normal maps or a surface
without an MDL skin.

## Still open

- Writing a Doom 3 material stub that names a staged `_local` map.
- Curvature and thickness maps, cage meshes, and averaged-normal projection.
- Per-vertex tangents. Tangent-space maps are baked against each triangle's
  own tangent frame, while Doom 3 sums face tangents per vertex (splitting
  vertices where the UVs mirror) and interpolates them, so across smooth
  edges whose triangles' texture gradients differ the baked map and the
  game's basis disagree slightly.
- A Doom 3 interaction check of a baked bump map through the Materials
  renderer.
