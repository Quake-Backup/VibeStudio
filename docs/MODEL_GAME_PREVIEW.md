# Game Preview

Status: Partial. The Mesh Editor's **Shading** > **Game** draws each surface
with what the engine would put on it, not just its image, and
`model game-skin` writes the same composition to PNG. It has been checked
against fixtures built from the published behaviour, not yet side by side with
the games.

The work lives in `src/core/model_game_preview.*` (composition),
`src/app/model_editor_game_preview.cpp` (the editor's controls and timer),
`src/app/model_viewport_render.cpp` with `src/core/shaders/model_surface.frag`
(the unlit layer and point sampling on the GPU) and `src/cli/model_game_skin.*`.

## What it draws

Each surface gets two layers at the current moment: a lit layer, shaded by the
view's light like the **Material** mode, and an unlit glow layer added after
shading.

### Quake models (MDL)

MDL skins keep their palette indices, so the classic renderers are emulated
from them. Choose the renderer in the Game row:

| Renderer | Fullbright indices 224-255 | Sampling | Flood fill |
| --- | --- | --- | --- |
| **Modern port** (QuakeSpasm and friends) | Unlit, at full value | Bilinear | Yes |
| **GLQuake** | Lit like any texel | Bilinear | Yes |
| **Software** | Unlit, at full value | Point | No |

- **Top** and **Bottom** set the player colour rows (0-13), as Quake's
  `color` command does: indices 16-31 and 96-111 are translated, rows below 8
  bright to dark and later rows the other way (`R_TranslatePlayerSkin`).
- **Flood fill** reproduces GLQuake's `Mod_FloodFillSkin`: the region of the
  skin's top-left texel colour connected to it takes its neighbours' colour,
  which is why unused skin space never bleeds into seams in the GL renderers.
- Skin groups show the member whose cumulative end time is past the current
  time within the group's cycle (`R_AliasSetupSkin`).

The software renderer's colormap light levels are approximated by the view's
shade.

### Quake III shaders (MD3 and other models)

With a package open, each surface's shader (its first skin path without the
extension, or an implicit model shader when no script defines it) is composed
in texture space, stage by stage, as `tr_shade.c` would draw it:

- stage images (`map`, `clampMap`, `animMap` frames at the time, `$whiteimage`);
- `blendFunc` with every source and destination factor;
- `alphaFunc GT0`, `LT128` and `GE128` cut-outs;
- `rgbGen` identity, identityLighting, const, wave, entity and
  `lightingDiffuse`/`vertex`, and `alphaGen` const and wave;
- `tcMod scroll`, `scale`, `rotate`, `stretch`, `transform` and `turb`.

Stages lit by the light grid (`rgbGen lightingDiffuse`, `vertex`) form the
lit layer; identity and constant stages are fullbright in Quake III and form
the glow layer. A shader with no opaque stage shows as translucent light.

### Not simulated

- `tcGen environment` depends on the view; the stage is mapped like the base
  coordinates, and a note says so.
- `deformVertexes`, skies and fog change geometry or the scene and are not
  drawn.
- Doom 3 bump and specular interaction: the diffuse image is shown.
- `$lightmap` stages: models have no lightmap.

## Using it

1. Choose **Shading** > **Game** (or the **Game** item of the shading list).
2. Pick the **Quake renderer**, player colours and **Flood fill** in the Game
   row under the views; the row's status says what is approximated.
3. Time runs while the preview animates (skin groups, `animMap`, `tcMod`,
   waves). With reduced motion it holds at the first moment.

Picking, selection and the edit tools work as in the other modes.

## Command line

```sh
vibestudio --cli model game-skin ./progs/player.mdl --top 4 --bottom 12 --quake-renderer software --output ./out/player.png --json
vibestudio --cli model game-skin ./models/weapon.md3 --package ./baseq3/pak0.pk3 --time 0.5 --output ./out/weapon.png --glow-output ./out/weapon_glow.png
```

`--output` is the lit layer and `--glow-output` the unlit layer. JSON reports
the size, the skin group member or `animMap` frame shown, the stages drawn,
whether the engine samples point-wise, SHA-256 hashes of both images and the
notes. Outputs are new files unless `--overwrite`; the model and the package are
never written.

## Sources

Behaviour follows the released GPL sources of Quake (`R_TranslatePlayerSkin`,
`R_AliasSetupSkin`, the colormap fullbrights), GLQuake (`Mod_FloodFillSkin`)
and Quake III Arena (`tr_shade.c`, `tr_shade_calc.c`); see
[credits](CREDITS.md#model-decode-issues-gltf-import-and-legacy-fuzzing-2026-10-29).
No code is copied.

## Still open

- Drawing the shader stages per pixel on the GPU through the Materials
  renderer (`core/material_render`), which would add `tcGen environment`,
  deforms and Doom 3 interaction; a `model render` command for whole-model
  images.
- Side-by-side checks against the games' own renderers.
