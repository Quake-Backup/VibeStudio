# Painting Skins

Status: Partial. **UV** > **Paint Skin…** in the Mesh Editor paints a
surface's skin directly on the model, and `model paint-skin` replays recorded
strokes. Checked on built fixtures; the painted skin is saved as an image and
is not yet handed back into the model or package.

The service is `src/core/model_skin_paint.*`, the dialog
`src/app/model_skin_paint_dialog.*` and the command
`src/cli/model_skin_paint.*`. All of it is original VibeStudio code.

## How a stroke paints

The dialog looks at the model from an orbit (**Yaw**, **Pitch**, **Zoom**),
framed to fit, and keeps which triangle is nearest at every pixel. Every texel
of the surface's UV layout is placed in 3D by its triangle and projected into
the view. A texel is painted when its projection is under the brush and its
triangle is the one the view shows there (or lies within a hair of it along a
shared edge). So:

- a stroke across a UV seam paints both islands continuously;
- an occluded face, such as the back of the model, is never touched;
- with a **Mirror** axis, texels whose mirror image across the model's X, Y
  or Z = 0 plane is under the brush, and visible, are painted too.

**Mix** blends by the brush's coverage (full inside the **Hardness** fraction
of the radius, fading to the rim, times **Opacity**); **Replace** sets texels
at least half covered. Painted texels then bleed two texels past the island's
edge, so filtering does not pull the background into seams.

Indexed skins (a Quake MDL's own skin) are painted with palette indices: the
**Index** you choose, or the palette entry nearest the colour. They are never
blended, so the skin stays valid for the game, fullbright and player-colour
ranges included.

## Using it

1. Select a surface and choose **UV** > **Paint Skin…**. The dialog starts
   from the surface's MDL skin, else its material image, else a blank skin.
2. Turn the view with **Yaw**, **Pitch** and **Zoom**; choose **Colour**,
   **Radius**, **Opacity**, **Hardness**, **Blend** and **Mirror**.
3. Drag with the left button to paint. <kbd>Esc</kbd> cancels the stroke in
   progress; **Undo Stroke** and **Redo Stroke** step through strokes.
4. **Save Skin…** writes a PNG or TGA (indexed skins stay indexed in PNG).

## Command line

```sh
vibestudio --cli model paint-skin ./progs/ogre.mdl --strokes ./strokes.json --output ./out/ogre_skin.png --json
```

The strokes file holds the view and each stroke:

```json
{"camera": {"yaw": 30, "pitch": 20, "zoom": 1, "width": 512, "height": 512},
 "strokes": [{"colour": "#c82828", "radius": 12, "opacity": 1, "hardness": 0.5,
              "mode": "mix", "mirror": "x", "padding": 2,
              "samples": [[250, 240], [262, 241]]}]}
```

`--texture` paints a copy of an image instead of the model's own skin; a
blank skin takes `--size WxH`. JSON reports the texels painted, the dirty
rectangle and a SHA-256 of the result, and the same strokes always give the
same skin.

## Still open

- Applying the painted skin to the model in one undo step (replacing an MDL
  skin member, or staging an external skin into the package).
- Painting in the main viewport with its own camera, a brush cursor that
  follows the surface, texture stamps and smudge.
