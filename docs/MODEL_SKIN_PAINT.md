# Painting Skins

Status: Partial. **UV** > **Paint Skin…** in the Mesh Editor paints a
surface's skin directly on the model, in its own orbit view or in the Mesh
Editor's viewport, with colour, stamp or smudge brushes, and **Apply to
Model** hands the result back: an MDL skin member as one undo step, or an
image staged in the open package. `model paint-skin` replays recorded strokes
and can write the model with the painted MDL skin. Checked on built fixtures,
not yet in the games.

The service is `src/core/model_skin_paint.*`, the dialog
`src/app/model_skin_paint_dialog.*`, the editor's side
`src/app/model_editor_skin_paint.cpp` with the viewport hook
`src/app/model_viewport_skin_paint.cpp`, and the command
`src/cli/model_skin_paint.*`. All of it is original VibeStudio code.

## How a stroke paints

A view looks at the model through a projection and keeps which triangle is
nearest at every pixel. The dialog's view is an orbit (**Yaw**, **Pitch**,
**Zoom**), orthographic and framed to fit; **Paint in View** uses the Mesh
Editor viewport's own camera, orthographic or perspective. Every texel of the
surface's UV layout is placed in 3D by its triangle and projected into the
view. A texel is painted when its projection is under the brush and its
triangle is the one the view shows there (or lies within a hair of it along a
shared edge). So:

- a stroke across a UV seam paints both islands continuously;
- an occluded face, such as the back of the model, is never touched, and
  nothing behind a perspective camera is;
- with a **Mirror** axis, texels whose mirror image across the model's X, Y
  or Z = 0 plane is under the brush, and visible, are painted too.

**Mix** blends by the brush's coverage (full inside the **Hardness** fraction
of the radius, fading to the rim, times **Opacity**); **Replace** sets texels
at least half covered. A **Stamp** paints an image instead of the colour: it
is scaled to the brush's diameter and centred on each brush position, its
colours are painted and its transparency lowers the coverage. **Smudge**
drags the skin along the stroke: at each step a texel under the brush takes
the colour shown where the brush was one step earlier, by **Opacity**; it
does not mirror. Painted texels then bleed two texels past the island's edge,
so filtering does not pull the background into seams.

Indexed skins (a Quake MDL's own skin, or an indexed PCX) are painted with
palette indices: the **Index** you choose, or the palette entry nearest the
colour (a stamp's colours too). They are never blended, and smudging moves
indices without inventing new ones, so the skin stays valid for the game,
fullbright and player-colour ranges included.

## Using it

1. Select a surface and choose **UV** > **Paint Skin…**. The dialog starts
   from the surface's MDL skin, else its material image (read at full size
   from the open package when it can be), else a blank skin.
2. Turn the view with **Yaw**, **Pitch** and **Zoom**; choose **Colour**,
   **Radius**, **Opacity**, **Hardness**, **Blend**, **Mirror** and
   **Stamp**.
3. Drag with the left button to paint. <kbd>Esc</kbd> cancels the stroke in
   progress; **Undo Stroke** and **Redo Stroke** step through strokes.
4. Or turn on **Paint in View** and drag in the Mesh Editor's viewport, with
   its own camera; the other mouse buttons still turn and move it, and the
   viewport shows the painted skin as you go. Painting there uses the frame
   the dialog opened on.
5. **Apply to Model** puts the skin back: for an MDL it replaces the first
   member of the surface's skin as one undo step; for a skin image from the
   open package it stages the image at the same path (PNG, TGA, JPEG, or PCX
   for an indexed skin) as one package step. Its tooltip says which.
6. **Save Skin…** writes a PNG, TGA, PCX (indexed skins) or JPEG.

## Command line

```sh
vibestudio --cli model paint-skin ./progs/ogre.mdl --strokes ./strokes.json --output ./out/ogre_skin.png --json
vibestudio --cli model paint-skin ./ogre.mesh.json --strokes ./strokes.json --output ./out/ogre_skin.png --model-output ./out/ogre_painted.mesh.json
```

The strokes file holds the view and each stroke:

```json
{"camera": {"yaw": 30, "pitch": 20, "zoom": 1, "width": 512, "height": 512},
 "strokes": [{"colour": "#c82828", "radius": 12, "opacity": 1, "hardness": 0.5,
              "mode": "mix", "mirror": "x", "padding": 2,
              "samples": [[250, 240], [262, 241]]},
             {"mode": "smudge", "radius": 10, "opacity": 0.8,
              "samples": [[200, 200], [230, 200]]},
             {"stamp": "decal.png", "radius": 24, "mode": "replace",
              "view": {"matrix": [16 numbers], "perspective": true,
                       "width": 1024, "height": 768, "eye": [0, -1, 0]},
              "samples": [[512, 384]]}]}
```

`mode` is `mix`, `replace` or `smudge`. A stamp is an image path relative to
the strokes file, or `stampPng` (base64 PNG, as the dialog records it). A
stroke painted in the Mesh Editor's viewport keeps its `view`: a row-major
4x4 matrix from model space to view pixels (rows U, V, Z, W; the point is
U/W, V/W and Z/W is the depth, larger nearer). Other strokes use `camera`.

`--texture` paints a copy of an image instead of the model's own skin; a
blank skin takes `--size WxH`. `--model-output FILE.mesh.json` writes the
model with the painted MDL skin in place of the surface's first member (an
error, before anything is written, for a surface without an MDL skin). JSON
reports the texels painted, the dirty rectangle, a SHA-256 of the result and,
with `--model-output`, the skin slot; the same strokes always give the same
skin.

## Still open

- A brush cursor drawn on the model's surface in the viewport (the dialog's
  canvas shows the brush outline).
- Smudge across surfaces, and texture-space (rather than view-space) stamps.
- Painting several surfaces or skin members in one session, and painting
  indexed skins of formats other than MDL and PCX.
